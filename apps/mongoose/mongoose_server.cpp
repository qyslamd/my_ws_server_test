#include "mongoose_server.h"
#include "nlohmann/json.hpp"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>

using json = nlohmann::json;

Mongoose_Server* Mongoose_Server::instance_ = nullptr;
static const double PI = 3.14159265358979323846;

// ---------- 从 URL 中提取 query 参数 (使用源码中正确的 API) ----------
static std::string get_query_param(struct mg_http_message *hm, const char *key) {
    char buf[64];
    int len = mg_http_get_var(&hm->query, key, buf, sizeof(buf));
    if (len > 0) {
        return std::string(buf, len);
    }
    return "";
}

// ---------- 二进制帧构造 ----------
static void build_waveform_frame(std::vector<uint8_t> &out, uint32_t stream_id,
                                 uint32_t seq, double t, uint64_t ts_ns, bool is_snapshot) {
    const int N = 1024;
    const char *meta = "{\"kind\":\"waveform\",\"dataType\":\"float32\",\"components\":1,"
                       "\"count\":1024,\"unit\":\"V\",\"sampleRate\":1000}";
    size_t metaLen = is_snapshot ? strlen(meta) : 0;
    size_t dataLen = N * sizeof(float);
    
    size_t total = 12 + 4 + dataLen + (is_snapshot ? (4 + metaLen) : 0) + 4 + 8;
    out.resize(total);
    uint8_t *p = out.data();

    p[0] = 0xA5;
    p[1] = 0x01;
    p[2] = is_snapshot ? 0x01 : 0x02;
    p[3] = 0x01 | 0x20 | (is_snapshot ? 0x10 : 0x00); 
    memcpy(p + 4, &stream_id, 4);
    memcpy(p + 8, &seq, 4);
    p += 12;

    uint32_t len = (uint32_t)dataLen;
    memcpy(p, &len, 4); p += 4;
    float *samples = (float *)p;
    double freq = 50.0 + 20.0 * std::sin(2.0 * PI * 0.5 * t);
    for (int i = 0; i < N; i++) {
        samples[i] = (float)std::sin(2.0 * PI * freq * (t + i / 1000.0));
    }
    p += dataLen;

    if (is_snapshot) {
        len = (uint32_t)metaLen;
        memcpy(p, &len, 4); p += 4;
        memcpy(p, meta, metaLen); p += metaLen;
    }

    len = 8;
    memcpy(p, &len, 4); p += 4;
    memcpy(p, &ts_ns, 8);
}

// ---------- 辅助函数:发送错误 ----------
static void send_error(struct mg_connection *c, const std::string& id, int code, const std::string& msg) {
    json resp = {
        {"v", 1}, {"type", "err"}, {"id", id}, {"name", "error"},
        {"payload", {{"code", code}, {"msg", msg}}}, {"ts", (long long)time(NULL) * 1000}
    };
    std::string s = resp.dump();
    mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
    MG_ERROR(("Protocol Error [%d]: %s (id=%s)", code, msg.c_str(), id.c_str()));
}

// ---------- WebSocket 升级 ----------
void Mongoose_Server::ws_upgrade(struct mg_connection *c, struct mg_http_message *hm) {
    mg_ws_upgrade(c, hm, NULL);
}

// ---------- 业务:处理控制通道文本指令 ----------
void Mongoose_Server::handle_control_text(struct mg_connection *c, struct mg_ws_message *wm) {
    ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);
    if (!ctx) return;

    std::string msg(wm->data.buf, wm->data.len);
    try {
        auto j = json::parse(msg);
        
        if (!j.contains("type") || !j.contains("name")) {
            send_error(c, "c-0000", 1003, "missing 'type' or 'name' field");
            return;
        }

        std::string type = j["type"];
        std::string name = j["name"];
        std::string id = j.value("id", "c-0000");

        if (type != "cmd") {
            send_error(c, id, 1001, "invalid message type, expected 'cmd'");
            return;
        }

        if (name != "hello" && name != "ping" && !ctx->is_handshaked) {
            send_error(c, id, 1002, "not handshaked, send 'hello' first");
            return;
        }

        if (name == "hello") {
            int client_proto = j.value("payload", json::object()).value("proto", 0);
            if (client_proto != 1) {
                send_error(c, id, 1004, "unsupported protocol version");
                mg_ws_send(c, "", 0, WEBSOCKET_OP_CLOSE); 
                return;
            }
            ctx->is_handshaked = true; 
            json resp = {
                {"v", 1}, {"type", "ack"}, {"id", id}, {"name", "hello"},
                {"payload", {
                    {"proto", 1}, {"server", "ws-skeleton/1.0"},
                    {"caps", {"stream", "snapshot"}}, {"session", ctx->session_id}
                }}, {"ts", (long long)time(NULL) * 1000}
            };
            std::string s = resp.dump();
            mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
        } 
        else if (name == "subscribe") {
            if (j.contains("payload") && j["payload"].contains("streams")) {
                for (auto& s : j["payload"]["streams"]) {
                    if (s.is_number()) {
                        uint32_t streamId = s.get<uint32_t>();
                        session_subscriptions_[ctx->session_id].insert(streamId);
                        MG_INFO(("Session %s subscribed to stream=%u", ctx->session_id.c_str(), streamId));
                    }
                }
            }
            json resp = {
                {"v", 1}, {"type", "ack"}, {"id", id}, {"name", "subscribe"},
                {"payload", {{"ok", true}}}, {"ts", (long long)time(NULL) * 1000}
            };
            std::string s = resp.dump();
            mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
        } 
        else if (name == "unsubscribe") {
            if (j.contains("payload") && j["payload"].contains("streams")) {
                for (auto& s : j["payload"]["streams"]) {
                    if (s.is_number()) {
                        uint32_t streamId = s.get<uint32_t>();
                        if (session_subscriptions_.count(ctx->session_id)) {
                            session_subscriptions_[ctx->session_id].erase(streamId);
                        }
                    }
                }
            }
            json resp = {
                {"v", 1}, {"type", "ack"}, {"id", id}, {"name", "unsubscribe"},
                {"payload", {{"ok", true}}}, {"ts", (long long)time(NULL) * 1000}
            };
            std::string s = resp.dump();
            mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
        } 
        else if (name == "ping") {
            json resp = {
                {"v", 1}, {"type", "ack"}, {"id", id}, {"name", "ping"},
                {"payload", {}}, {"ts", (long long)time(NULL) * 1000}
            };
            std::string s = resp.dump();
            mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
        } 
        else {
            send_error(c, id, 1005, "unknown command");
        }
    } 
    catch (const json::parse_error& e) {
        send_error(c, "c-0000", 1001, "invalid JSON format");
    }
    catch (const std::exception& e) {
        send_error(c, "c-0000", 3001, "internal exception");
    }
}

// ---------- 处理底层控制帧 (Pong 等) ----------
void Mongoose_Server::on_ws_ctl(struct mg_connection *c, struct mg_ws_message *wm) {
    ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);
    if (!ctx) return;
    
    // 收到任何底层控制帧（包括 Pong），都刷新活跃时间
    ctx->last_active_ms = mg_millis(); 

    uint8_t opcode = wm->flags & 0x0F;
    if (opcode == WEBSOCKET_OP_PONG) {
        // MG_VERBOSE(("Got PONG from %d", ctx->conn_type));
    }
}

// ---------- 定时推流 ----------
void Mongoose_Server::push_waveform() {
    bool has_subscriber = false;
    for (const auto& pair : session_subscriptions_) {
        if (pair.second.count(1)) { has_subscriber = true; break; }
    }
    if (!has_subscriber) return;

    uint64_t now_ms = mg_millis();
    double t = now_ms / 1000.0;
    uint64_t ts_ns = now_ms * 1000000ULL;

    std::vector<uint8_t> snap_frame;
    build_waveform_frame(snap_frame, 1, seq_, t, ts_ns, true);
    
    std::vector<uint8_t> delta_frame;
    build_waveform_frame(delta_frame, 1, seq_++, t, ts_ns, false);

    for (struct mg_connection *c = mgr_.conns; c != NULL; c = c->next) {
        if (c->is_websocket) {
            ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);
            if (ctx && ctx->conn_type == CONN_STREAM) {
                bool subscribed = false;
                if (session_subscriptions_.count(ctx->session_id)) {
                    subscribed = session_subscriptions_[ctx->session_id].count(1) > 0;
                }

                if (subscribed) {
                    if (!ctx->snapshot_sent) {
                        mg_ws_send(c, snap_frame.data(), snap_frame.size(), WEBSOCKET_OP_BINARY);
                        ctx->snapshot_sent = true;
                        MG_INFO(("Sent SNAPSHOT to stream client (sid=%s)", ctx->session_id.c_str()));
                    } else {
                        mg_ws_send(c, delta_frame.data(), delta_frame.size(), WEBSOCKET_OP_BINARY);
                    }
                }
            }
        }
    }
}

// ---------- 协议级 Ping 探活与僵尸连接清理 ----------
void Mongoose_Server::ws_ping_timer(void *arg) {
    Mongoose_Server *self = static_cast<Mongoose_Server*>(arg);
    uint64_t now = mg_millis();

    for (struct mg_connection *c = self->mgr_.conns; c != NULL; c = c->next) {
        if (c->is_websocket) {
            ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);
            if (ctx) {
                // 如果超过 30 秒没有收到任何数据或 Pong（防呆判断，避免 now < last_active）
                if (now > ctx->last_active_ms && now - ctx->last_active_ms > 30000) {
                    MG_ERROR(("Client timeout, kicking (sid=%s, type=%d)", 
                              ctx->session_id.c_str(), ctx->conn_type));
                    c->is_closing = 1; // 踢下线，触发 MG_EV_CLOSE
                } else {
                    // 发送底层的 WebSocket Ping 帧（浏览器会自动回 Pong）
                    mg_ws_send(c, "", 0, WEBSOCKET_OP_PING);
                }
            }
        }
    }
}

// ---------- 路由与事件 ----------
void Mongoose_Server::on_http_msg(struct mg_connection *c, struct mg_http_message *hm) {
    ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);

    // 用 mg_match 比较 URI，自动剥离 query 参数
    if (mg_match(hm->uri, mg_str("/stream"), NULL)) {
        // 必须先给上下文赋值，再调用 ws_upgrade（它会同步触发 WS_OPEN）
        ctx->conn_type = CONN_STREAM;
        ctx->session_id = get_query_param(hm, "sid");
        ws_upgrade(c, hm);
        MG_INFO(("upgrade /stream (sid=%s)", ctx->session_id.c_str()));
        return;
    }
    if (mg_match(hm->uri, mg_str("/control"), NULL)) {
        ctx->conn_type = CONN_CONTROL;
        ctx->session_id = get_query_param(hm, "sid");
        ws_upgrade(c, hm);
        MG_INFO(("upgrade /control (sid=%s)", ctx->session_id.c_str()));
        return;
    }
    struct mg_http_serve_opts opts = {};
    opts.root_dir = "www";
    mg_http_serve_dir(c, hm, &opts);
}

void Mongoose_Server::on_ws_msg(struct mg_connection *c, struct mg_ws_message *wm) {
    ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);
    if (ctx) {
        ctx->last_active_ms = mg_millis(); // 收到应用层消息，刷新时间
        if (ctx->conn_type == CONN_CONTROL) {
            handle_control_text(c, wm);
        }
    }
}

void Mongoose_Server::event_fn(struct mg_connection *c, int ev, void *ev_data) {
    Mongoose_Server *self = Mongoose_Server::instance_;
    ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);

    // === 核心修复：在连接建立瞬间，初始化时间戳 ===
    if (ev == MG_EV_OPEN) {
        ClientContext* new_ctx = new ClientContext();
        new_ctx->last_active_ms = mg_millis(); 
        c->fn_data = new_ctx;
        return;
    }

    if (ev == MG_EV_HTTP_MSG) {
        self->on_http_msg(c, static_cast<struct mg_http_message *>(ev_data));
    } else if (ev == MG_EV_WS_OPEN) {
        ctx->last_active_ms = mg_millis(); // 握手成功，再次刷新
    } else if (ev == MG_EV_WS_MSG) {
        self->on_ws_msg(c, static_cast<struct mg_ws_message *>(ev_data));
    } else if (ev == MG_EV_WS_CTL) {
        // 拦截底层 Pong 帧
        self->on_ws_ctl(c, static_cast<struct mg_ws_message *>(ev_data));
    } else if (ev == MG_EV_CLOSE) {
        if (ctx) {
            if (ctx->conn_type == CONN_CONTROL) {
                self->session_subscriptions_.erase(ctx->session_id);
                MG_INFO(("Cleaned up subscriptions for sid=%s", ctx->session_id.c_str()));
            }
            delete ctx;
            c->fn_data = nullptr;
        }
    }
}

void Mongoose_Server::push_timer(void *arg) {
    static_cast<Mongoose_Server *>(arg)->push_waveform();
}

Mongoose_Server::Mongoose_Server(const char *listen_url)
    : listen_url_(listen_url), seq_(0), listening_(false) {
    instance_ = this;
    mg_mgr_init(&mgr_);

     // === 新增：设置日志等级 ===
    // 设为 MG_LL_INFO 即可屏蔽底层 write_conn 的刷屏，但保留你写的 upgrade / subscribe 等重要日志
    // 设为 MG_LL_ERROR 则只打印错误
    mg_log_set(MG_LL_INFO);

    if (mg_http_listen(&mgr_, listen_url_, &event_fn, this) == NULL) {
        MG_ERROR(("Cannot listen on %s", listen_url_));
    } else {
        listening_ = true;
        MG_INFO(("Listening on %s", listen_url_));
        // 100ms 推流定时器
        mg_timer_add(&mgr_, 100, MG_TIMER_REPEAT, &push_timer, this);
        // 10 秒底层 Ping 探活定时器
        mg_timer_add(&mgr_, 10000, MG_TIMER_REPEAT, &ws_ping_timer, this);
    }
}

Mongoose_Server::~Mongoose_Server() {
    mg_mgr_free(&mgr_);
}

void Mongoose_Server::run() {
    if (!listening_) return;
    for (;;) mg_mgr_poll(&mgr_, 10);
}