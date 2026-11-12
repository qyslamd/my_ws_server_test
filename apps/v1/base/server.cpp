#include "base/server.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "base/frame.h"
#include "mongoose.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace v1 {
namespace {

const char* kCorsHeaders =
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET,POST,PUT,PATCH,DELETE,OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, X-Session-Id\r\n";

struct ClientCtx {
  std::string sid;
  bool is_ws = false;
};

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
  }
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
      active->ws->is_closing = 1;
      active->ws = nullptr;
    }
    Session s;
    s.sid = sid;
    s.last_ms = mg_millis();
    sessions_[sid] = s;
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
                  {{"max_data_frame", 8388608},
                   {"default_window", 1048576},
                   {"buffer_bytes", {{"sample", 4194304},
                                     {"record", 8388608}}}}}});
      return;
    }
    if (r == "time" && method == "GET") {
      ReplyJson(c, 200, {{"serverTime", mg_millis()}});
      return;
    }
    if (r == "health" && method == "GET") {
      ReplyJson(c, 200, {{"ok", true},
                         {"uptimeMs", mg_millis() - started_ms_}});
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
        ReplyJson(c, 200, {{"token", s->token},
                           {"user", s->user},
                           {"role", s->role}});
      } else {
        ReplyError(c, 401, 4001, "invalid credentials");
      }
      return;
    }
    if (r == "auth" && parts.size() == 4 && method == "GET" &&
        parts[3] == "me") {
      ReplyJson(c, 200, {{"user", s->user},
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
        ReplyJson(c, 200, {{"scope", scope},
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
        ReplyJson(c, 200, {{"scope", scope},
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
      ReplyJson(c, 201, {{"blobId", id},
                         {"size", data.size()},
                         {"contentType", ct}});
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
          ReplyError(c, 404, 2002, "stream not found",
                     {{"streamId", id}});
          return;
        }
        Subscription sub;
        sub.window = item.value("window", 1048576ULL);
        sub.meta_sent = false;
        s->subs[id] = sub;
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
        ReplyJson(c, 200, {{"scope", scope},
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
        ReplyJson(c, 200, {{"scope", scope},
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
      ReplyJson(c, 200, {{"server", server_logs},
                         {"modules", module_logs}});
      return;
    }
    ReplyError(c, 404, 2004, "unknown endpoint");
    return;
  }

  struct mg_http_serve_opts opts = {};
  opts.root_dir = "www/v1";
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
  json ev = {{"v", 1}, {"type", "evt"}, {"name", name},
             {"payload", payload}, {"ts", mg_millis()}};
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
  SendEvent(&it->second, "core.state",
            {{"state", running.empty() ? "idle" : "running"},
             {"modules", running}});
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
  json msg = json::parse(std::string(wm->data.buf, wm->data.len), nullptr,
                         false);
  if (msg.is_discarded() || msg.value("type", "") != "credit") {
    return;
  }
  if (!msg.contains("streams") || !msg["streams"].is_array()) {
    return;
  }
  for (const auto& item : msg["streams"]) {
    const uint32_t id = item.value("id", 0u);
    const uint64_t bytes = item.value("bytes", 0ULL);
    auto sub = it->second.subs.find(id);
    if (sub != it->second.subs.end()) {
      sub->second.outstanding =
          bytes >= sub->second.outstanding ? 0
                                           : sub->second.outstanding - bytes;
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
      event_payload["artifacts"] = json::array(
          {{{"blobId", blob},
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
  for (auto& kv : s->subs) {
    const uint32_t id = kv.first;
    Subscription& sub = kv.second;
    const StreamInfo* stream = nullptr;
    Module* owner = FindStreamModule(id, &stream);
    if (owner == nullptr) {
      continue;
    }
    std::vector<uint8_t> data;
    std::vector<uint8_t> records;
    std::string meta;
    if (!owner->Produce(id, now, data, records, meta)) {
      continue;
    }
    if (sub.window > 0 && sub.outstanding >= sub.window) {
      continue;
    }
    const bool with_meta = !sub.meta_sent;
    const uint32_t seq = ++seqs_[id];
    const auto frame =
        BuildFrame(id, seq, now * 1000000ULL,
                   with_meta ? kSnapshot : kDelta, with_meta, meta, data,
                   records);
    mg_ws_send(s->ws, frame.data(), frame.size(), WEBSOCKET_OP_BINARY);
    sub.meta_sent = true;
    sub.outstanding += data.size() + records.size();
  }
}

void V1Server::Ping() {
  const uint64_t now = mg_millis();
  for (auto& kv : sessions_) {
    Session& s = kv.second;
    if (s.ws == nullptr) {
      continue;
    }
    if (now > s.last_ms && now - s.last_ms > 15000) {
      Log("session timeout sid=" + s.sid);
      SafeStop("heartbeat timeout");
      s.ws->is_closing = 1;
      continue;
    }
    mg_ws_send(s.ws, "", 0, WEBSOCKET_OP_PING);
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
