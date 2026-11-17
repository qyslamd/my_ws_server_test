#include "base/server.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "base/frame.h"
#include "mongoose.h"

using json = nlohmann::json;

namespace v1 {
namespace {

const char* kCorsHeaders =
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET,POST,PUT,PATCH,DELETE,OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, X-Session-Id\r\n";

// WS 关闭码。和 HTTP 错误码（也是四位数）刻意错开，免得同一份日志里
// 看到 4001 有两种解释。见规范 §7.10。目前只有会话结束这一档有发送点，
// 4101/4102 留给"会话级致命错误"，那条路径还没出现
constexpr uint16_t kCloseSessionEnd = 4100;

// 发送队列硬上限（规范 §8.4）。超过说明链路已经跟不上，
// 继续堆只会让心跳也排不出去
constexpr uint64_t kSendQueueBytes = 64ull * 1024 * 1024;

// state 每 tick 的发送闸。state 不参与配额，没有这道闸的话，
// 一路刷得勤的状态流就能把发送队列占满，把整场会话带下去（方案第六节）
constexpr std::size_t kMaxStateFramesPerTick = 8;
constexpr std::size_t kMaxStateBytesPerTick = 256 * 1024;

// 一条会话里所有环合计的预算（字节区 + 描述符），规范 §8.4
constexpr std::size_t kRingTotalBytes = 64u * 1024 * 1024;

// 单条 WS 消息的负载上限与默认配额，规范 §8.4：
// 两者取同一个值，一帧恰好装得满一次额度，不必靠超发
constexpr std::size_t kMaxDataFrameBytes = 1024u * 1024;
constexpr uint64_t kDefaultWindowBytes = 1024ull * 1024;

struct ClientCtx {
  std::string sid;
  bool is_ws = false;
};

// 发带原因码的关闭帧。RFC 6455 规定关闭帧前两字节是大端的码，
// 后面可跟原因；原因一律用 ASCII，避免截断时把多字节字符切成半个。
void SendClose(mg_connection* c, uint16_t code, const char* reason) {
  if (c == nullptr) {
    return;
  }
  std::string payload;
  payload.push_back(static_cast<char>((code >> 8) & 0xFF));
  payload.push_back(static_cast<char>(code & 0xFF));
  payload += reason;
  mg_ws_send(c, payload.data(), payload.size(), WEBSOCKET_OP_CLOSE);
}

std::string ToStr(const mg_str& s) { return std::string(s.buf, s.len); }

std::vector<std::string> SplitPath(const std::string& uri) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < uri.size()) {
    while (i < uri.size() && uri[i] == '/') {
      ++i;
    }
    size_t j = i;
    while (j < uri.size() && uri[j] != '/') {
      ++j;
    }
    if (j > i) {
      out.push_back(uri.substr(i, j - i));
    }
    i = j;
  }
  return out;
}

std::string Header(mg_http_message* hm, const char* name) {
  mg_str* h = mg_http_get_header(hm, name);
  return h ? ToStr(*h) : std::string();
}

std::string Body(mg_http_message* hm) {
  return std::string(hm->body.buf, hm->body.len);
}

void ReplyJson(mg_connection* c, int status, const json& j) {
  std::string s = j.dump();
  mg_http_reply(c, status, kCorsHeaders, "%s", s.c_str());
}

void ReplyError(mg_connection* c, int status, int code, const std::string& msg,
                const json& detail = json()) {
  json j = {{"code", code}, {"msg", msg}, {"retryable", false}};
  if (!detail.is_null()) {
    j["detail"] = detail;
  }
  ReplyJson(c, status, j);
}

void ReplyBinary(mg_connection* c, const std::string& content_type,
                 const std::vector<uint8_t>& data) {
  mg_printf(c,
            "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n",
            content_type.c_str(), static_cast<int>(data.size()));
  if (!data.empty()) {
    mg_send(c, data.data(), data.size());
  }
}

bool ParseBody(mg_http_message* hm, json& out) {
  try {
    out = json::parse(Body(hm));
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

V1Server* V1Server::instance_ = nullptr;

V1Server::V1Server(int port, bool auth_required)
    : port_(port), auth_required_(auth_required) {
  instance_ = this;
  mgr_ = new mg_mgr();
  mg_mgr_init(mgr_);
  started_ms_ = mg_millis();
  LoadConfig();
  LoadBlobs();
}

V1Server::~V1Server() {
  mg_mgr_free(mgr_);
  delete mgr_;
  instance_ = nullptr;
}

void V1Server::RegisterModule(Module* module) {
  if (module != nullptr) {
    modules_[module->ns()] = module;
    // 推送口由通讯层提供，模块只拿到基类指针
    module->AttachSink(&sink_);
  }
}

std::size_t V1Server::BufferBytesFor(const std::string& cls) {
  // 规范 §8.4 的默认值：record 比 sample 大一倍
  return cls == "record" ? 8u * 1024 * 1024 : 4u * 1024 * 1024;
}

std::shared_ptr<v1::StreamBuffers> V1Server::Sink::Buffers() const {
  // 先拷一份 shared_ptr 再出锁：会话在中间被销毁也不影响这次写入，
  // 缓冲对象会活到最后一个引用释放为止
  std::lock_guard<std::mutex> lock(server_->buffers_mutex_);
  return server_->active_buffers_;
}

bool V1Server::Sink::PushSample(uint32_t stream_id, const uint8_t* data,
                                std::size_t len) {
  // 超过协议上限的帧直接拒收：进环也没用，发送侧发不出去
  if (data == nullptr || len == 0 || len > kMaxDataFrameBytes) {
    return false;
  }
  const auto buffers = Buffers();
  if (buffers == nullptr) {
    return false;
  }
  return buffers->PushFrame(stream_id, data, len, mg_millis() * 1000000ULL);
}

bool V1Server::Sink::PushRecords(uint32_t stream_id,
                                 const std::vector<Record>& records) {
  // 每条记录带 12 字节头（时间戳 8 + 长度 4），合计不能超过单帧上限
  std::size_t total = 0;
  for (const Record& record : records) {
    total += 12 + record.payload.size();
  }
  if (total == 0 || total > kMaxDataFrameBytes) {
    return false;
  }
  const auto buffers = Buffers();
  if (buffers == nullptr) {
    return false;
  }
  // 模块不持有通讯层的时钟：记录没带时间戳时由这里补
  const uint64_t now_ns = mg_millis() * 1000000ULL;
  std::vector<Record> stamped;
  stamped.reserve(records.size());
  for (const Record& record : records) {
    Record copy = record;
    if (copy.ts_ns == 0) {
      copy.ts_ns = now_ns;
    }
    stamped.push_back(std::move(copy));
  }
  return buffers->PushRecords(stream_id, stamped);
}

void V1Server::Sink::SetState(uint32_t stream_id, const uint8_t* data,
                              std::size_t len) {
  if (data == nullptr || len == 0 || len > kMaxDataFrameBytes) {
    return;
  }
  const auto buffers = Buffers();
  if (buffers == nullptr) {
    return;
  }
  buffers->SetState(stream_id, data, len);
}

void V1Server::RegisterMediaSource(MediaSource* source) {
  if (source != nullptr) {
    media_sources_[source->id()] = source;
  }
}

void V1Server::Log(const std::string& msg) {
  logs_.push_back(msg);
  if (logs_.size() > 200) {
    logs_.erase(logs_.begin());
  }
  MG_INFO(("v1: %s", msg.c_str()));
}

bool V1Server::Start() {
  mg_log_set(MG_LL_ERROR);
  const std::string url = "http://127.0.0.1:" + std::to_string(port_);
  if (mg_http_listen(mgr_, url.c_str(), EventFn, this) == nullptr) {
    MG_ERROR(("cannot listen on %s", url.c_str()));
    return false;
  }
  Log("listening on " + url);
  mg_timer_add(mgr_, 10, MG_TIMER_REPEAT, TickFn, this);
  mg_timer_add(mgr_, 5000, MG_TIMER_REPEAT, PingFn, this);
  running_.store(true);
  return true;
}

void V1Server::Stop() {
  // 事件循环每 10 ms 醒一次，置位后最多 10 ms 就会退出
  running_.store(false);
}

void V1Server::Run() {
  if (!running_.load() && !Start()) {
    return;
  }
  while (running_.load()) {
    mg_mgr_poll(mgr_, 10);
  }
}

void V1Server::EventFn(mg_connection* c, int ev, void* ev_data) {
  V1Server* self = instance_;
  if (self == nullptr) {
    return;
  }
  ClientCtx* ctx = static_cast<ClientCtx*>(c->fn_data);
  if (ev == MG_EV_OPEN) {
    c->fn_data = new ClientCtx();
    return;
  }
  if (ev == MG_EV_HTTP_MSG) {
    self->OnHttp(c, static_cast<mg_http_message*>(ev_data));
    return;
  }
  if (ev == MG_EV_WS_OPEN) {
    if (ctx != nullptr) {
      ctx->is_ws = true;
    }
    self->OnWsOpen(c);
    return;
  }
  if (ev == MG_EV_WS_MSG) {
    self->OnWsMsg(c, static_cast<mg_ws_message*>(ev_data));
    return;
  }
  if (ev == MG_EV_WS_CTL) {
    if (ctx != nullptr) {
      auto it = self->sessions_.find(ctx->sid);
      if (it != self->sessions_.end()) {
        it->second.last_ms = mg_millis();
      }
    }
    return;
  }
  if (ev == MG_EV_CLOSE) {
    if (ctx != nullptr && ctx->is_ws) {
      self->OnClose(c);
    }
    delete ctx;
    c->fn_data = nullptr;
  }
}

void V1Server::OnHttp(mg_connection* c, mg_http_message* hm) {
  const std::string method = ToStr(hm->method);
  const std::string uri = ToStr(hm->uri);

  if (method == "OPTIONS") {
    mg_http_reply(c, 204, kCorsHeaders, "");
    return;
  }

  if (uri == "/ws" && mg_http_get_header(hm, "Sec-WebSocket-Key") != nullptr) {
    char sid_buf[128] = {0};
    const int n = mg_http_get_var(&hm->query, "sid", sid_buf, sizeof(sid_buf));
    const std::string sid = n > 0 ? std::string(sid_buf, n) : std::string();
    if (sid.empty()) {
      ReplyError(c, 400, 1003, "missing sid");
      return;
    }
    Session* active = ActiveSession();
    if (active != nullptr && active->sid != sid) {
      ReplyError(c, 409, 2001, "session busy");
      return;
    }
    if (active != nullptr && active->sid == sid && active->ws != nullptr) {
      // 先告诉旧连接为什么被关，再断开；客户端据此能区分
      // "被顶替"和"网络断了"。is_draining 是"发完缓冲再关"，
      // 用 is_closing 会把关闭帧直接丢掉
      SendClose(active->ws, kCloseSessionEnd, "replaced by new session");
      active->ws->is_draining = 1;
      active->ws = nullptr;
    }
    // 先把旧会话连同它的缓冲整块丢掉，再原地建新的：
    // 重连不该继承上一条连接的任何状态
    sessions_.erase(sid);
    Session& fresh = sessions_[sid];
    fresh.sid = sid;
    fresh.last_ms = mg_millis();
    fresh.buffers = std::make_shared<StreamBuffers>();
    {
      // 先断旧生产，再让新的缓冲生效（方案 5.1 第 1 步）
      std::lock_guard<std::mutex> lock(buffers_mutex_);
      active_buffers_ = fresh.buffers;
    }
    auto* ctx = static_cast<ClientCtx*>(c->fn_data);
    if (ctx != nullptr) {
      ctx->sid = sid;
    }
    mg_ws_upgrade(c, hm, NULL);
    sessions_[sid].ws = c;
    Log("session open sid=" + sid);
    return;
  }

  if (uri.rfind("/media/", 0) == 0 && method == "GET") {
    const std::string id = uri.substr(7);
    auto it = media_sources_.find(id);
    if (it == media_sources_.end()) {
      ReplyError(c, 404, 2002, "media source not found");
      return;
    }
    static std::map<std::string, int> frame_no;
    ReplyBinary(c, it->second->content_type(),
                it->second->NextFrame(frame_no[id]++));
    return;
  }

  const std::vector<std::string> parts = SplitPath(uri);
  if (parts.size() >= 2 && parts[0] == "api" && parts[1] == "v1") {
    const std::string& r = parts.size() > 2 ? parts[2] : "";

    if (r == "capabilities" && method == "GET") {
      ReplyJson(c, 200,
                {{"proto", 1},
                 {"server", "v1-base/0.1"},
                 {"caps", {"credit", "frag", "jobs", "blobs"}},
                 {"limits",
                  {{"max_data_frame", kMaxDataFrameBytes},
                   {"default_window", kDefaultWindowBytes},
                   {"buffer_bytes", {{"sample", 4194304}, {"record", 8388608}}},
                   {"send_queue_bytes", kSendQueueBytes},
                   {"frag_timeout_ms", 5000},
                   {"ring_total_bytes", kRingTotalBytes}}}});
      return;
    }
    if (r == "time" && method == "GET") {
      ReplyJson(c, 200, {{"serverTime", mg_millis()}});
      return;
    }
    if (r == "health" && method == "GET") {
      ReplyJson(c, 200,
                {{"ok", true}, {"uptimeMs", mg_millis() - started_ms_}});
      return;
    }

    Session* s = SessionFromHeader(hm);
    if (s == nullptr) {
      ReplyError(c, 409, 2001, "no active session");
      return;
    }

    if (r == "auth" && parts.size() == 4 && method == "POST" &&
        parts[3] == "login") {
      json body;
      if (!ParseBody(hm, body)) {
        ReplyError(c, 400, 1001, "invalid json");
        return;
      }
      const std::string user = body.value("username", "");
      const std::string pass = body.value("password", "");
      if ((user == "admin" && pass == "admin") ||
          (user == "operator" && pass == "operator")) {
        s->user = user;
        s->role = user == "admin" ? "admin" : "operator";
        s->token = "tok-" + std::to_string(++token_counter_);
        ReplyJson(c, 200,
                  {{"token", s->token}, {"user", s->user}, {"role", s->role}});
      } else {
        ReplyError(c, 401, 4001, "invalid credentials");
      }
      return;
    }
    if (r == "auth" && parts.size() == 4 && method == "GET" &&
        parts[3] == "me") {
      ReplyJson(c, 200,
                {{"user", s->user},
                 {"role", s->role},
                 {"authenticated", !s->user.empty()}});
      return;
    }
    if (r == "auth" && parts.size() == 4 && method == "POST" &&
        parts[3] == "logout") {
      s->user.clear();
      s->role.clear();
      s->token.clear();
      ReplyJson(c, 200, {{"ok", true}});
      return;
    }

    if (r == "config" && parts.size() == 4) {
      const std::string& scope = parts[3];
      if (method == "GET") {
        json values = config_.count(scope) ? config_[scope] : json::object();
        ReplyJson(c, 200,
                  {{"scope", scope},
                   {"revision", config_rev_[scope]},
                   {"values", values}});
        return;
      }
      if (method == "PUT" || method == "PATCH") {
        json body;
        if (!ParseBody(hm, body) || !body.contains("values") ||
            !body["values"].is_object()) {
          ReplyError(c, 400, 1003, "body must contain values object");
          return;
        }
        if (method == "PUT" || !config_.count(scope)) {
          config_[scope] = body["values"];
        } else {
          config_[scope].update(body["values"]);
        }
        ++config_rev_[scope];
        SaveConfig();
        ReplyJson(c, 200,
                  {{"scope", scope},
                   {"revision", config_rev_[scope]},
                   {"values", config_[scope]}});
        return;
      }
    }

    if (r == "blobs" && parts.size() == 3 && method == "POST") {
      if (hm->body.len > 8 * 1024 * 1024) {
        ReplyError(c, 413, 2008, "blob too large");
        return;
      }
      std::vector<uint8_t> data(hm->body.buf, hm->body.buf + hm->body.len);
      std::string ct = Header(hm, "Content-Type");
      if (ct.empty()) {
        ct = "application/octet-stream";
      }
      const std::string id = AddBlob(ct, data);
      ReplyJson(c, 201,
                {{"blobId", id}, {"size", data.size()}, {"contentType", ct}});
      return;
    }
    if (r == "blobs" && parts.size() == 3 && method == "GET") {
      json list = json::array();
      for (const auto& kv : blobs_) {
        list.push_back({{"blobId", kv.first},
                        {"size", kv.second.data.size()},
                        {"contentType", kv.second.content_type}});
      }
      ReplyJson(c, 200, {{"blobs", list}});
      return;
    }
    if (r == "blobs" && parts.size() == 4 && method == "GET") {
      auto it = blobs_.find(parts[3]);
      if (it == blobs_.end()) {
        ReplyError(c, 404, 2002, "blob not found");
        return;
      }
      ReplyBinary(c, it->second.content_type, it->second.data);
      return;
    }
    if (r == "blobs" && parts.size() == 4 && method == "DELETE") {
      if (blobs_.erase(parts[3]) == 0) {
        ReplyError(c, 404, 2002, "blob not found");
        return;
      }
      mg_http_reply(c, 204, kCorsHeaders, "");
      return;
    }

    if (r == "state" && method == "GET") {
      json modules = json::array();
      for (const auto& kv : modules_) {
        modules.push_back({{"ns", kv.first}, {"state", kv.second->State()}});
      }
      ReplyJson(c, 200, {{"session", s->sid}, {"modules", modules}});
      return;
    }
    if (r == "modules" && parts.size() == 3 && method == "GET") {
      json modules = json::array();
      for (const auto& kv : modules_) {
        modules.push_back(kv.second->Describe());
      }
      ReplyJson(c, 200, {{"modules", modules}});
      return;
    }
    if (r == "modules" && parts.size() == 5 &&
        (parts[4] == "start" || parts[4] == "stop")) {
      if (auth_required_ && s->role != "admin") {
        ReplyError(c, 403, 4001, "admin role required");
        return;
      }
      Module* m = FindModule(parts[3]);
      if (m == nullptr) {
        ReplyError(c, 404, 2002, "module not found");
        return;
      }
      std::string err;
      const bool ok = parts[4] == "start" ? m->Start(err) : m->Stop(err);
      if (!ok) {
        ReplyError(c, 409, 2006, err);
        return;
      }
      SendEvent(s, "core.module.state",
                {{"ns", m->ns()}, {"running", m->running()}});
      SendEvent(s, "core.state",
                {{"module", m->ns()}, {"running", m->running()}});
      ReplyJson(c, 200, {{"ok", true}, {"state", m->State()}});
      return;
    }
    if (r == "streams" && parts.size() == 3 && method == "GET") {
      json streams = json::array();
      for (const auto& kv : modules_) {
        for (const auto& st : kv.second->streams()) {
          streams.push_back({{"id", st.id},
                             {"name", st.name},
                             {"class", st.cls},
                             {"meta", json::parse(st.meta)}});
        }
      }
      ReplyJson(c, 200, {{"streams", streams}});
      return;
    }
    if (r == "subscriptions" && parts.size() == 3 && method == "POST") {
      json body;
      if (!ParseBody(hm, body) || !body.contains("streams") ||
          !body["streams"].is_array()) {
        ReplyError(c, 400, 1003, "body must contain streams[]");
        return;
      }
      json out = json::array();
      for (const auto& item : body["streams"]) {
        const uint32_t id = item.value("id", 0u);
        const StreamInfo* desc = nullptr;
        Module* owner = FindStreamModule(id, &desc);
        if (owner == nullptr || desc == nullptr) {
          ReplyError(c, 404, 2002, "stream not found", {{"streamId", id}});
          return;
        }
        Subscription sub;
        sub.window = item.value("window", kDefaultWindowBytes);
        sub.meta_sent = false;
        // 重订阅不清欠账，否则客户端靠"再订阅一次"就能把窗口翻倍
        auto existing = s->subs.find(id);
        if (existing != s->subs.end()) {
          sub.outstanding = existing->second.outstanding;
        }
        // 缓冲预算：一个会话里所有环合计不能超过上限，
        // 否则一个写坏的客户端反复订阅就能把内存顶上去（方案第八节）
        const std::size_t capacity = BufferBytesFor(desc->cls);
        const bool is_state = desc->cls == "state";
        const std::size_t charge = StreamBuffers::ChargeFor(capacity, is_state);
        if (!s->subs.count(id) &&
            s->buffers->ChargedBytes() + charge > kRingTotalBytes) {
          ReplyError(c, 409, 2006, "ring budget exhausted",
                     {{"streamId", id},
                      {"limit", kRingTotalBytes},
                      {"used", s->buffers->ChargedBytes()}});
          return;
        }
        s->subs[id] = sub;
        // 订阅时按流类型开一块缓冲：模块推过来的数据落在这里
        s->buffers->Open(id, capacity, is_state);
        out.push_back({{"streamId", id},
                       {"window", sub.window},
                       {"meta", json::parse(desc->meta)}});
      }
      ReplyJson(c, 200, {{"subscriptions", out}});
      return;
    }
    if (r == "subscriptions" && parts.size() == 4 && method == "DELETE") {
      const uint32_t id = static_cast<uint32_t>(std::stoul(parts[3]));
      if (s->subs.erase(id) == 0) {
        ReplyError(c, 404, 2003, "not subscribed");
        return;
      }
      s->buffers->Close(id);
      mg_http_reply(c, 204, kCorsHeaders, "");
      return;
    }
    if (r == "params" && parts.size() == 4) {
      const std::string& scope = parts[3];
      Module* m = FindParamModule(scope);
      if (m == nullptr) {
        ReplyError(c, 404, 2002, "param scope not found");
        return;
      }
      if (method == "GET") {
        ReplyJson(c, 200,
                  {{"scope", scope},
                   {"revision", revisions_[scope]},
                   {"values", m->GetParams(scope)}});
        return;
      }
      if (method == "PUT" || method == "PATCH") {
        json body;
        if (!ParseBody(hm, body) || !body.contains("values")) {
          ReplyError(c, 400, 1003, "body must contain values");
          return;
        }
        json applied;
        std::string err;
        if (!m->SetParams(scope, body["values"], method == "PUT", applied,
                          err)) {
          ReplyError(c, 422, 3001, err);
          return;
        }
        ++revisions_[scope];
        ReplyJson(c, 200,
                  {{"scope", scope},
                   {"revision", revisions_[scope]},
                   {"values", applied}});
        return;
      }
    }
    if (r == "commands" && parts.size() == 5 && method == "POST") {
      Module* m = FindModule(parts[3]);
      if (m == nullptr) {
        ReplyError(c, 404, 2004, "unknown module");
        return;
      }
      if (auth_required_ && s->role != "admin" && m->AdminOnly(parts[4])) {
        ReplyError(c, 403, 4001, "admin role required");
        return;
      }
      json body;
      if (!ParseBody(hm, body)) {
        ReplyError(c, 400, 1001, "invalid json");
        return;
      }
      json result;
      std::string err;
      if (!m->Command(parts[4], body, result, err)) {
        ReplyError(c, 422, 3001, err);
        return;
      }
      ReplyJson(c, 200, {{"ok", true}, {"result", result}});
      return;
    }
    if (r == "jobs" && parts.size() == 3 && method == "POST") {
      json body;
      if (!ParseBody(hm, body)) {
        ReplyError(c, 400, 1001, "invalid json");
        return;
      }
      std::string ns = body.value("ns", "");
      if (ns.empty()) {
        if (modules_.size() == 1) {
          ns = modules_.begin()->first;
        } else {
          ReplyError(c, 400, 1003, "body must contain ns");
          return;
        }
      }
      Module* m = FindModule(ns);
      if (m == nullptr) {
        ReplyError(c, 404, 2004, "unknown module");
        return;
      }
      std::string job_id;
      std::string err;
      if (!m->StartJob(body.value("type", "job"),
                       body.value("input", json::object()), job_id, err)) {
        ReplyError(c, 409, 2006, err);
        return;
      }
      job_owner_[job_id] = m;
      ReplyJson(c, 202, {{"jobId", job_id}});
      return;
    }
    if (r == "jobs" && parts.size() == 4 && method == "GET") {
      Module* m = FindJobModule(parts[3]);
      if (m == nullptr) {
        ReplyError(c, 404, 2002, "job not found");
        return;
      }
      json st = m->JobStatus(parts[3]);
      if (st.is_null()) {
        ReplyError(c, 404, 2002, "job not found");
        return;
      }
      ReplyJson(c, 200, st);
      return;
    }
    if (r == "jobs" && parts.size() == 4 && method == "DELETE") {
      Module* m = FindJobModule(parts[3]);
      if (m == nullptr) {
        ReplyError(c, 404, 2002, "job not found");
        return;
      }
      json payload;
      std::string err;
      if (!m->CancelJob(parts[3], err, payload)) {
        ReplyError(c, 409, 2006, err);
        return;
      }
      ReplyJson(c, 200, payload);
      return;
    }
    if (r == "logs" && method == "GET") {
      json server_logs = json::array();
      for (const auto& l : logs_) {
        server_logs.push_back(l);
      }
      json module_logs = json::object();
      for (const auto& kv : modules_) {
        module_logs[kv.first] = kv.second->Logs();
      }
      ReplyJson(c, 200, {{"server", server_logs}, {"modules", module_logs}});
      return;
    }
    ReplyError(c, 404, 2004, "unknown endpoint");
    return;
  }

  struct mg_http_serve_opts opts = {};
  // 静态根归通讯层自己(apps/v1/www):/ 是通讯层自检页,/eph205/ 是产品前端。
  // 构建时由 copy_www 复制到 build/www,所以这里用相对工作目录的 "www"。
  opts.root_dir = "www";
  mg_http_serve_dir(c, hm, &opts);
}

V1Server::Session* V1Server::ActiveSession() {
  for (auto& kv : sessions_) {
    if (kv.second.ws != nullptr) {
      return &kv.second;
    }
  }
  return nullptr;
}

V1Server::Session* V1Server::SessionFromHeader(mg_http_message* hm) {
  const std::string sid = Header(hm, "X-Session-Id");
  if (sid.empty()) {
    return nullptr;
  }
  auto it = sessions_.find(sid);
  if (it == sessions_.end() || it->second.ws == nullptr) {
    return nullptr;
  }
  return &it->second;
}

void V1Server::SendEvent(Session* s, const std::string& name,
                         const json& payload) {
  if (s == nullptr || s->ws == nullptr || !s->ws->is_websocket) {
    return;
  }
  json ev = {{"v", 1},
             {"type", "evt"},
             {"name", name},
             {"payload", payload},
             {"ts", mg_millis()}};
  const std::string text = ev.dump();
  mg_ws_send(s->ws, text.c_str(), text.size(), WEBSOCKET_OP_TEXT);
}

void V1Server::OnWsOpen(mg_connection* c) {
  auto* ctx = static_cast<ClientCtx*>(c->fn_data);
  if (ctx == nullptr) {
    return;
  }
  auto it = sessions_.find(ctx->sid);
  if (it == sessions_.end()) {
    c->is_closing = 1;
    return;
  }
  it->second.ws = c;
  it->second.last_ms = mg_millis();
  json running = json::array();
  for (const auto& kv : modules_) {
    if (kv.second->running()) {
      running.push_back(kv.first);
    }
  }
  SendEvent(
      &it->second, "core.state",
      {{"state", running.empty() ? "idle" : "running"}, {"modules", running}});
}

void V1Server::OnWsMsg(mg_connection* c, mg_ws_message* wm) {
  auto* ctx = static_cast<ClientCtx*>(c->fn_data);
  if (ctx == nullptr) {
    return;
  }
  auto it = sessions_.find(ctx->sid);
  if (it == sessions_.end()) {
    return;
  }
  it->second.last_ms = mg_millis();
  if (wm->data.len == 0 || wm->data.buf[0] != '{') {
    return;
  }
  json msg =
      json::parse(std::string(wm->data.buf, wm->data.len), nullptr, false);
  if (msg.is_discarded() || msg.value("type", "") != "credit") {
    return;
  }
  if (!msg.contains("streams") || !msg["streams"].is_array()) {
    return;
  }
  bool reported_bad = false;
  for (const auto& item : msg["streams"]) {
    if (!item.is_object()) {
      continue;
    }
    // 只接受非负整数：负数或字符串会让 get<uint64_t>() 抛异常，
    // 异常穿过 mongoose 的回调栈不是我们想要的死法。
    const auto id_it = item.find("id");
    const auto bytes_it = item.find("bytes");
    if (id_it == item.end() || !id_it->is_number_unsigned() ||
        bytes_it == item.end() || !bytes_it->is_number_unsigned()) {
      // 一条消息只报一次，免得客户端记账有 bug 时把事件通道刷屏
      if (!reported_bad) {
        reported_bad = true;
        SendEvent(&it->second, "core.error",
                  {{"code", 2005}, {"msg", "invalid credit"}});
      }
      continue;
    }
    const uint32_t id = static_cast<uint32_t>(id_it->get<uint64_t>());
    const uint64_t bytes = bytes_it->get<uint64_t>();
    auto sub = it->second.subs.find(id);
    if (sub == it->second.subs.end()) {
      continue;
    }
    // state 不参与配额，给它还额度等于什么都没做（规范 §8.2）
    const StreamInfo* stream = nullptr;
    FindStreamModule(id, &stream);
    if (stream != nullptr && stream->cls == "state") {
      SendEvent(&it->second, "core.error",
                {{"code", 2005},
                 {"msg", "credit ignored: state stream"},
                 {"streamId", id}});
      continue;
    }
    // 还的比欠的多：数值合法但超出窗口，超出部分截断（规范 §8.2）
    if (bytes > sub->second.outstanding) {
      const uint64_t excess = bytes - sub->second.outstanding;
      sub->second.outstanding = 0;
      SendEvent(&it->second, "core.error",
                {{"code", 2009},
                 {"msg", "credit exceeds window, truncated"},
                 {"streamId", id},
                 {"truncatedBytes", excess}});
    } else {
      sub->second.outstanding -= bytes;
    }
  }
}

void V1Server::OnClose(mg_connection* c) {
  auto* ctx = static_cast<ClientCtx*>(c->fn_data);
  if (ctx == nullptr) {
    return;
  }
  auto it = sessions_.find(ctx->sid);
  if (it == sessions_.end()) {
    return;
  }
  if (it->second.ws != nullptr && it->second.ws != c) {
    return;
  }
  Log("session close sid=" + ctx->sid);
  SafeStop("ws closed");
  {
    // 先断生产：此后的 Push 立刻拿到空指针，在飞的那些自己会跑完
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    active_buffers_.reset();
  }
  sessions_.erase(it);
}

void V1Server::SafeStop(const std::string& reason) {
  bool stopped = false;
  for (auto& kv : modules_) {
    if (kv.second->running()) {
      std::string err;
      kv.second->Stop(err);
      stopped = true;
    }
  }
  if (!stopped) {
    return;
  }
  Log("SAFE_STOP: " + reason);
  Session* s = ActiveSession();
  if (s != nullptr) {
    SendEvent(s, "core.session.ending", {{"reason", reason}});
  }
}

void V1Server::Tick() {
  const uint64_t now = mg_millis();
  CheckHeartbeat(now);

  for (auto& job : modules_) {
    std::string event_name;
    json event_payload;
    if (!job.second->JobTick(now, event_name, event_payload)) {
      continue;
    }
    if (event_name == "core.job.progress" &&
        event_payload.value("state", "") == "completed") {
      const std::string text = event_payload.dump(2);
      std::vector<uint8_t> bytes(text.begin(), text.end());
      const std::string blob = AddBlob("application/json", bytes);
      event_payload["artifacts"] =
          json::array({{{"blobId", blob},
                        {"name", "job-result.json"},
                        {"contentType", "application/json"}}});
      job_owner_[event_payload.value("jobId", "")] = job.second;
    }
    Session* active = ActiveSession();
    if (active != nullptr) {
      SendEvent(active, event_name, event_payload);
    }
  }

  Session* s = ActiveSession();
  if (s == nullptr || s->ws == nullptr) {
    return;
  }

  // 积压超过硬上限：链路已经跟不上，继续堆只会把心跳也埋掉。
  // 先告诉客户端是带宽问题再断开（规范 §8.5 的过载停机路径）
  if (s->ws->send.len > kSendQueueBytes) {
    Log("session overload sid=" + s->sid + "，发送队列积压 " +
        std::to_string(s->ws->send.len) + " B");
    SendEvent(s, "core.session.ending", {{"reason", "send queue overflow"}});
    SafeStop("send queue overflow");
    // 积压的几十 MB 没必要再发：丢掉，只把关闭帧送出去
    mg_iobuf_del(&s->ws->send, 0, s->ws->send.len);
    SendClose(s->ws, kCloseSessionEnd, "send queue overflow");
    s->ws->is_draining = 1;
    return;
  }

  SendStreamFrames(s, now);
  SendStateFrames(s, now);
}

// sample / record：按配额连发，欠账就不发
void V1Server::SendStreamFrames(Session* s, uint64_t now) {
  for (auto& kv : s->subs) {
    const uint32_t id = kv.first;
    Subscription& sub = kv.second;
    const StreamInfo* stream = nullptr;
    if (FindStreamModule(id, &stream) == nullptr || stream == nullptr) {
      continue;
    }
    if (stream->cls == "state") {
      continue;  // state 另走一轮
    }
    ReportOverrun(s, id, sub, now);

    const uint64_t available =
        sub.window > sub.outstanding ? sub.window - sub.outstanding : 0;
    if (available == 0) {
      // 没配额就不取数：先取再丢属于"数据被消费但发不出去"，规范不允许
      if (sub.stalled_ms == 0) {
        sub.stalled_ms = now;
        if (sub.window == 0) {
          Log("stream " + std::to_string(id) + " window=0，只订阅不推数据");
        } else {
          Log("stream " + std::to_string(id) +
              " 额度耗尽，暂停推送（window=" + std::to_string(sub.window) +
              "，未归还=" + std::to_string(sub.outstanding) + "）");
        }
      }
      continue;
    }
    if (sub.stalled_ms != 0) {
      Log("stream " + std::to_string(id) + " 恢复推送（暂停 " +
          std::to_string(now - sub.stalled_ms) + " ms）");
      sub.stalled_ms = 0;
    }

    // 每取一帧都重算配额：在循环外算一次会让"超发"变成连发（方案 3.3）
    while (true) {
      std::size_t len = 0;
      if (!s->buffers->Peek(id, &len)) {
        break;
      }
      const uint64_t rest =
          sub.window > sub.outstanding ? sub.window - sub.outstanding : 0;
      const bool overshoot = len > rest;
      if (overshoot && sub.outstanding != 0) {
        break;  // 已经欠着，等客户端归还（规范 §8.2）
      }

      std::vector<uint8_t> payload;
      uint32_t seq = 0;
      uint64_t ts_ns = 0;
      if (!s->buffers->TakeFrame(id, now * 1000000ULL, &payload, &seq,
                                 &ts_ns)) {
        break;
      }
      if (overshoot) {
        Log("stream " + std::to_string(id) + " 单帧 " +
            std::to_string(payload.size()) + " B 超过可用额度 " +
            std::to_string(rest) + " B，超发一帧");
      }
      SendStreamFrame(s, id, sub, stream, seq, ts_ns, payload, {}, true);
      if (overshoot) {
        break;  // 超发一帧后收手，等归还
      }
    }
  }
}

// state：每 tick 有总量闸，按轮转保证每一路都轮得到
void V1Server::SendStateFrames(Session* s, uint64_t now) {
  std::vector<uint32_t> ids;
  for (auto& kv : s->subs) {
    const StreamInfo* stream = nullptr;
    if (FindStreamModule(kv.first, &stream) != nullptr && stream != nullptr &&
        stream->cls == "state") {
      ids.push_back(kv.first);
    }
  }
  if (ids.empty()) {
    return;
  }

  // 从上次停下的下一个开始，避免固定的前几路永远抢到名额
  std::size_t start = 0;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (ids[i] > state_rr_) {
      start = i;
      break;
    }
  }

  std::size_t frames = 0;
  std::size_t bytes = 0;
  for (std::size_t n = 0; n < ids.size(); ++n) {
    if (frames >= kMaxStateFramesPerTick || bytes >= kMaxStateBytesPerTick) {
      break;  // 剩下的下一个 tick 再发：state 只保证最终一致
    }
    const uint32_t id = ids[(start + n) % ids.size()];
    Subscription& sub = s->subs[id];
    std::vector<uint8_t> payload;
    uint32_t seq = 0;
    uint64_t ts_ns = 0;
    if (!s->buffers->TakeFrame(id, now * 1000000ULL, &payload, &seq, &ts_ns)) {
      continue;
    }
    const StreamInfo* stream = nullptr;
    FindStreamModule(id, &stream);
    SendStreamFrame(s, id, sub, stream, seq, ts_ns, payload, {}, false);
    ++frames;
    bytes += payload.size();
    state_rr_ = id;
  }
}

void V1Server::SendStreamFrame(Session* s, uint32_t id, Subscription& sub,
                               const StreamInfo* stream, uint32_t seq,
                               uint64_t ts_ns, const std::vector<uint8_t>& data,
                               const std::vector<uint8_t>& records,
                               bool charge) {
  const bool with_meta = !sub.meta_sent;
  const std::string meta =
      with_meta && stream != nullptr ? stream->meta : std::string();
  const auto frame = BuildFrame(id, seq, ts_ns, with_meta ? kSnapshot : kDelta,
                                with_meta, meta, data, records);
  mg_ws_send(s->ws, frame.data(), frame.size(), WEBSOCKET_OP_BINARY);
  sub.meta_sent = true;
  if (charge) {
    sub.outstanding += data.size() + records.size();
  }
}

void V1Server::ReportOverrun(Session* s, uint32_t id, Subscription& sub,
                             uint64_t now) {
  StreamRing::Overrun fresh;
  if (s->buffers->TakeOverrun(id, &fresh)) {
    if (sub.pending_overrun.dropped_frames == 0) {
      sub.pending_overrun.first_seq = fresh.first_seq;
    }
    sub.pending_overrun.last_seq = fresh.last_seq;
    sub.pending_overrun.dropped_frames += fresh.dropped_frames;
    sub.pending_overrun.dropped_bytes += fresh.dropped_bytes;
  }
  if (sub.pending_overrun.dropped_frames == 0) {
    return;
  }
  // 第一声立刻报；同一段持续溢出才按 500 毫秒合并（规范 §8.1 与 §8.3 的折中）
  if (sub.last_overrun_ms != 0 && now - sub.last_overrun_ms < 500) {
    return;
  }

  const json payload = {{"streamId", id},
                        {"droppedFrames", sub.pending_overrun.dropped_frames},
                        {"droppedBytes", sub.pending_overrun.dropped_bytes},
                        {"firstSeq", sub.pending_overrun.first_seq},
                        {"lastSeq", sub.pending_overrun.last_seq}};
  SendEvent(s, "core.overrun", payload);

  // 再补一帧 opcode=ERROR：事件通道拥挤时也能看到；seq 固定 0，不占号
  const std::string text = payload.dump();
  std::vector<uint8_t> data(text.begin(), text.end());
  const auto frame =
      BuildFrame(id, 0, now * 1000000ULL, kError, false, "", data, {});
  mg_ws_send(s->ws, frame.data(), frame.size(), WEBSOCKET_OP_BINARY);

  sub.last_overrun_ms = now;
  sub.pending_overrun = StreamRing::Overrun{};
}

void V1Server::Ping() {
  for (auto& kv : sessions_) {
    Session& s = kv.second;
    if (s.ws != nullptr) {
      mg_ws_send(s.ws, "", 0, WEBSOCKET_OP_PING);
    }
  }
}

// 判死看两件事：静默多久、这段时间里发送缓冲有没有往前走过。
// 积压本身不能当免死牌：拔网线后缓冲同样非零，而且会一直涨（规范 §5.3）
void V1Server::CheckHeartbeat(uint64_t now) {
  for (auto& kv : sessions_) {
    Session& s = kv.second;
    if (s.ws == nullptr) {
      continue;
    }

    const std::size_t queued = s.ws->send.len;
    if (s.backlog_base_ms != s.last_ms) {
      // 刚收到过消息：以这一刻的积压作为新窗口的基准
      s.backlog_base_ms = s.last_ms;
      s.backlog_base = queued;
      s.backlog_min = queued;
    } else if (queued < s.backlog_min) {
      s.backlog_min = queued;
    }

    const uint64_t silent_ms = now > s.last_ms ? now - s.last_ms : 0;
    const bool progressing = s.backlog_min < s.backlog_base;
    const uint64_t limit_ms = progressing ? 30000 : 15000;
    if (silent_ms <= limit_ms) {
      continue;
    }

    Log("session timeout sid=" + s.sid + "，静默 " + std::to_string(silent_ms) +
        " ms，发送缓冲 " + std::to_string(queued) + " B" +
        (progressing ? "（有推进）" : "（无推进）"));
    SafeStop("heartbeat timeout");
    SendClose(s.ws, kCloseSessionEnd, "heartbeat timeout");
    s.ws->is_draining = 1;
  }
}

std::string V1Server::AddBlob(const std::string& content_type,
                              const std::vector<uint8_t>& data) {
  const std::string id = "b-" + std::to_string(++blob_counter_);
  blobs_[id] = Blob{content_type, data};
  std::error_code ec;
  std::filesystem::create_directories("blobs", ec);
  std::ofstream f("blobs/" + id, std::ios::binary);
  if (f && !data.empty()) {
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
  }
  std::ofstream meta("blobs/" + id + ".meta");
  if (meta) {
    meta << content_type;
  }
  return id;
}

void V1Server::LoadConfig() {
  std::ifstream f("v1_config.json");
  if (!f) {
    return;
  }
  json j;
  try {
    f >> j;
  } catch (...) {
    return;
  }
  if (j.contains("values") && j["values"].is_object()) {
    for (auto& item : j["values"].items()) {
      config_[item.key()] = item.value();
    }
  }
  if (j.contains("revisions") && j["revisions"].is_object()) {
    for (auto& item : j["revisions"].items()) {
      config_rev_[item.key()] = item.value().get<int>();
    }
  }
}

void V1Server::SaveConfig() {
  json j;
  j["values"] = config_;
  j["revisions"] = config_rev_;
  std::ofstream f("v1_config.json");
  if (f) {
    f << j.dump(2);
  }
}

void V1Server::LoadBlobs() {
  std::error_code ec;
  if (!std::filesystem::exists("blobs", ec)) {
    return;
  }
  for (const auto& entry : std::filesystem::directory_iterator("blobs", ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string id = entry.path().filename().string();
    if (id.size() > 5 && id.substr(id.size() - 5) == ".meta") {
      continue;
    }
    std::ifstream f(entry.path(), std::ios::binary);
    if (!f) {
      continue;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    std::string content_type = "application/octet-stream";
    std::ifstream meta(entry.path().string() + ".meta");
    if (meta) {
      std::getline(meta, content_type);
    }
    blobs_[id] = Blob{content_type, data};
    if (id.rfind("b-", 0) == 0) {
      try {
        const int n = std::stoi(id.substr(2));
        if (n > blob_counter_) {
          blob_counter_ = n;
        }
      } catch (...) {
      }
    }
  }
}

Module* V1Server::FindModule(const std::string& ns) const {
  auto it = modules_.find(ns);
  return it == modules_.end() ? nullptr : it->second;
}

Module* V1Server::FindStreamModule(uint32_t stream_id,
                                   const StreamInfo** stream) const {
  for (const auto& kv : modules_) {
    for (const auto& st : kv.second->streams()) {
      if (st.id == stream_id) {
        if (stream != nullptr) {
          *stream = &st;
        }
        return kv.second;
      }
    }
  }
  return nullptr;
}

Module* V1Server::FindParamModule(const std::string& scope) const {
  for (const auto& kv : modules_) {
    if (kv.second->OwnsParamScope(scope)) {
      return kv.second;
    }
  }
  return nullptr;
}

Module* V1Server::FindJobModule(const std::string& job_id) const {
  auto it = job_owner_.find(job_id);
  return it == job_owner_.end() ? nullptr : it->second;
}

void V1Server::TickFn(void* arg) { static_cast<V1Server*>(arg)->Tick(); }
void V1Server::PingFn(void* arg) { static_cast<V1Server*>(arg)->Ping(); }

}  // namespace v1
