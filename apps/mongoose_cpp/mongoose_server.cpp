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

// ---------- 从 URL 中提取 query 参数 ----------
static std::string get_query_param(struct mg_http_message *hm, const char *key) {
    char buf[64];
    int len = mg_http_get_var(&hm->query, key, buf, sizeof(buf));
    if (len > 0) return std::string(buf, len);
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

        json resp = {
            {"v", 1}, {"type", "ack"}, {"id", id}, 
            {"payload", {{"ok", true}}}, {"ts", (long long)time(NULL) * 1000}
        };

        if (name == "hello") {
            int client_proto = j.value("payload", json::object()).value("proto", 0);
            if (client_proto != 1) {
                send_error(c, id, 1004, "unsupported protocol version");
                mg_ws_send(c, "", 0, WEBSOCKET_OP_CLOSE); 
                return;
            }
            ctx->is_handshaked = true; 
            resp["name"] = "hello";
            resp["payload"] = {
                {"proto", 1}, {"server", "ws-skeleton/1.0"},
                {"caps", {"stream", "snapshot"}}, {"session", ctx->session_id}
            };
        } 
        else if (name == "start") {
            // 开启采集状态，并重置所有数据通道的快照标志
            is_sampling_ = true;
            for (struct mg_connection *conn = mgr_.conns; conn != NULL; conn = conn->next) {
                if (conn->is_websocket) {
                    ClientContext* cctx = static_cast<ClientContext*>(conn->fn_data);
                    if (cctx && cctx->conn_type == CONN_STREAM) {
                        cctx->snapshot_sent = false; 
                    }
                }
            }
            resp["name"] = "start";
            MG_INFO(("System status: SAMPLING STARTED"));
        }
        else if (name == "stop") {
            // 停止采集状态
            is_sampling_ = false;
            resp["name"] = "stop";
            MG_INFO(("System status: SAMPLING STOPPED"));
        }
        else if (name == "subscribe") {
            if (j.contains("payload") && j["payload"].contains("streams")) {
                for (auto& s : j["payload"]["streams"]) {
                    if (s.is_number()) {
                        uint32_t streamId = s.get<uint32_t>();
                        session_subscriptions_[ctx->session_id].insert(streamId);
                    }
                }
            }
            resp["name"] = "subscribe";
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
            resp["name"] = "unsubscribe";
        } 
        else if (name == "ping") {
            resp["name"] = "ping";
        } 
        else {
            send_error(c, id, 1005, "unknown command");
            return;
        }
        
        std::string s = resp.dump();
        mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
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
    if (ctx) {
        ctx->last_active_ms = mg_millis(); 
    }
}

// ---------- 定时推流 ----------
void Mongoose_Server::push_waveform() {
    // 如果系统未开启采集，直接返回，不推流
    if (!is_sampling_) return;

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
                if (now > ctx->last_active_ms && now - ctx->last_active_ms > 30000) {
                    MG_ERROR(("Client timeout, kicking (sid=%s, type=%d)", ctx->session_id.c_str(), ctx->conn_type));
                    c->is_closing = 1; 
                } else {
                    mg_ws_send(c, "", 0, WEBSOCKET_OP_PING);
                }
            }
        }
    }
}

// ---------- 路由与事件 ----------
void Mongoose_Server::on_http_msg(struct mg_connection *c, struct mg_http_message *hm) {
    ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);
    if (mg_match(hm->uri, mg_str("/stream"), NULL)) {
        ctx->conn_type = CONN_STREAM;
        ctx->session_id = get_query_param(hm, "sid");
        ws_upgrade(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/control"), NULL)) {
        ctx->conn_type = CONN_CONTROL;
        ctx->session_id = get_query_param(hm, "sid");
        ws_upgrade(c, hm);
        return;
    }
    struct mg_http_serve_opts opts = {};
    opts.root_dir = "www";
    mg_http_serve_dir(c, hm, &opts);
}

void Mongoose_Server::on_ws_msg(struct mg_connection *c, struct mg_ws_message *wm) {
    ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);
    if (ctx) {
        ctx->last_active_ms = mg_millis(); 
        if (ctx->conn_type == CONN_CONTROL) {
            handle_control_text(c, wm);
        }
    }
}

void Mongoose_Server::event_fn(struct mg_connection *c, int ev, void *ev_data) {
    Mongoose_Server *self = Mongoose_Server::instance_;
    ClientContext* ctx = static_cast<ClientContext*>(c->fn_data);

    if (ev == MG_EV_OPEN) {
        ClientContext* new_ctx = new ClientContext();
        new_ctx->last_active_ms = mg_millis(); 
        c->fn_data = new_ctx;
        return;
    }

    if (ev == MG_EV_HTTP_MSG) {
        self->on_http_msg(c, static_cast<struct mg_http_message *>(ev_data));
    } else if (ev == MG_EV_WS_OPEN) {
        ctx->last_active_ms = mg_millis(); 
    } else if (ev == MG_EV_WS_MSG) {
        self->on_ws_msg(c, static_cast<struct mg_ws_message *>(ev_data));
    } else if (ev == MG_EV_WS_CTL) {
        self->on_ws_ctl(c, static_cast<struct mg_ws_message *>(ev_data));
    } else if (ev == MG_EV_CLOSE) {
        if (ctx) {
            if (ctx->conn_type == CONN_CONTROL) {
                self->session_subscriptions_.erase(ctx->session_id);
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
    : listen_url_(listen_url), seq_(0), listening_(false), is_sampling_(false) {
    instance_ = this;
    mg_mgr_init(&mgr_);
    
    // 设置日志等级，屏蔽底层收发报文
    mg_log_set(MG_LL_INFO);

    if (mg_http_listen(&mgr_, listen_url_, &event_fn, this) == NULL) {
        MG_ERROR(("Cannot listen on %s", listen_url_));
    } else {
        listening_ = true;
        MG_INFO(("Listening on %s", listen_url_));
        mg_timer_add(&mgr_, 100, MG_TIMER_REPEAT, &push_timer, this);
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