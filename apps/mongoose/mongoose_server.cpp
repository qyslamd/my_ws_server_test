#include "mongoose_server.h"
#include "nlohmann/json.hpp"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using json = nlohmann::json;

// 连接类型标签(存于 c->data[0])
enum { CONN_NONE = 0, CONN_STREAM = 1, CONN_CONTROL = 2 };
static const double PI = 3.14159265358979323846;

// ---------- 二进制帧构造 ----------
static void build_waveform_frame(std::vector<uint8_t> &out, uint32_t stream_id,
                                 uint32_t seq, double t, uint64_t ts_ns) {
    const int N = 1024;
    const char *meta = "{\"kind\":\"waveform\",\"dataType\":\"float32\",\"components\":1,"
                       "\"count\":1024,\"unit\":\"V\",\"sampleRate\":1000}";
    size_t metaLen = strlen(meta);
    size_t dataLen = N * sizeof(float);
    size_t total = 12 + 4 + dataLen + 4 + metaLen + 4 + 8;
    out.resize(total);
    uint8_t *p = out.data();

    p[0] = 0xA5;
    p[1] = 0x01;
    p[2] = 0x02;                    // DELTA
    p[3] = 0x01 | 0x10 | 0x20;      // DATA | META | TS
    memcpy(p + 4, &stream_id, 4);
    memcpy(p + 8, &seq, 4);
    p += 12;

    uint32_t len = (uint32_t)dataLen;
    memcpy(p, &len, 4); p += 4;
    float *samples = (float *)p;
    double freq = 50.0 + 20.0 * std::sin(2.0 * PI * 0.5 * t);  // 频率 30~70 Hz 摆动
    for (int i = 0; i < N; i++) {
        double tt = t + i / 1000.0;
        samples[i] = (float)std::sin(2.0 * PI * freq * tt);
    }
    p += dataLen;

    len = (uint32_t)metaLen;
    memcpy(p, &len, 4); p += 4;
    memcpy(p, meta, metaLen);

    len = 8;
    memcpy(p + 4 + metaLen, &len, 4);
    memcpy(p + 8 + metaLen, &ts_ns, 8);
}

// ---------- 辅助函数:发送错误 ----------
static void send_error(struct mg_connection *c, const std::string& id, int code, const std::string& msg) {
    json resp = {
        {"v", 1}, {"type", "err"}, {"id", id}, {"name", "error"},
        {"payload", {{"code", code}, {"msg", msg}}}, {"ts", 0}
    };
    std::string s = resp.dump();
    mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
}

// ---------- WebSocket 升级(子协议白名单校验) ----------
void Mongoose_Server::ws_upgrade(struct mg_connection *c, struct mg_http_message *hm) {
    struct mg_str *sp = mg_http_get_header(hm, "Sec-WebSocket-Protocol");
    if (sp != NULL) {
        bool supported = false;
        size_t i = 0;
        while (i < sp->len) {
            while (i < sp->len && (sp->buf[i] == ' ' || sp->buf[i] == ',')) i++;
            size_t s = i;
            while (i < sp->len && sp->buf[i] != ',') i++;
            size_t e = i;
            while (e > s && (sp->buf[e-1] == ' ' || sp->buf[e-1] == '\t')) e--;
            size_t n = e - s;
            if ((n == 6 && memcmp(sp->buf + s, "stream", 6) == 0) ||
                (n == 7 && memcmp(sp->buf + s, "control", 7) == 0)) {
                supported = true;
                break;
            }
        }
        if (!supported) {
            mg_http_reply(c, 400, "", "unsupported subprotocol\n");
            return;
        }
    }
    mg_ws_upgrade(c, hm, NULL);
}

// ---------- 业务:处理控制通道文本指令 ----------
void Mongoose_Server::handle_control_text(struct mg_connection *c, struct mg_ws_message *wm) {
    std::string msg(wm->data.buf, wm->data.len);
    try {
        auto j = json::parse(msg);
        std::string type = j.value("type", "");
        std::string name = j.value("name", "");
        std::string id = j.value("id", "c-0000");

        if (type != "cmd") {
            send_error(c, id, 1001, "must be cmd type");
            return;
        }

        if (name == "hello") {
            json resp = {
                {"v", 1}, {"type", "ack"}, {"id", id}, {"name", "hello"},
                {"payload", {
                    {"proto", 1}, {"server", "ws-skeleton/1.0"},
                    {"caps", {"stream", "snapshot"}}, {"session", "s-001"}
                }}, {"ts", 0}
            };
            std::string s = resp.dump();
            mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
        } 
        else if (name == "subscribe") {
            if (j.contains("payload") && j["payload"].contains("streams")) {
                for (auto& s : j["payload"]["streams"]) {
                    if (s.is_number()) {
                        uint32_t streamId = s.get<uint32_t>();
                        stream_subscriber_counts_[streamId]++;
                        MG_INFO(("Subscribe: stream=%u, current subscribers=%d", 
                                 streamId, stream_subscriber_counts_[streamId]));
                    }
                }
            }
            json resp = {
                {"v", 1}, {"type", "ack"}, {"id", id}, {"name", "subscribe"},
                {"payload", {{"ok", true}}}, {"ts", 0}
            };
            std::string s = resp.dump();
            mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
        } 
        else if (name == "unsubscribe") {
            if (j.contains("payload") && j["payload"].contains("streams")) {
                for (auto& s : j["payload"]["streams"]) {
                    if (s.is_number()) {
                        uint32_t streamId = s.get<uint32_t>();
                        if (stream_subscriber_counts_.count(streamId) > 0) {
                            stream_subscriber_counts_[streamId]--;
                            if (stream_subscriber_counts_[streamId] <= 0) {
                                stream_subscriber_counts_.erase(streamId);
                            }
                        }
                        MG_INFO(("Unsubscribe: stream=%u, current subscribers=%d", 
                                 streamId, stream_subscriber_counts_.count(streamId) ? stream_subscriber_counts_[streamId] : 0));
                    }
                }
            }
            json resp = {
                {"v", 1}, {"type", "ack"}, {"id", id}, {"name", "unsubscribe"},
                {"payload", {{"ok", true}}}, {"ts", 0}
            };
            std::string s = resp.dump();
            mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
        } 
        else if (name == "ping") {
            json resp = {
                {"v", 1}, {"type", "ack"}, {"id", id}, {"name", "ping"},
                {"payload", {}}, {"ts", 0}
            };
            std::string s = resp.dump();
            mg_ws_send(c, s.c_str(), s.length(), WEBSOCKET_OP_TEXT);
        } 
        else {
            send_error(c, id, 1005, "unknown command");
        }
    } 
    catch (const std::exception& e) {
        MG_ERROR(("JSON parse error: %s", e.what()));
        send_error(c, "c-0000", 1001, "invalid JSON");
    }
}

// ---------- 定时推流 ----------
void Mongoose_Server::push_waveform() {
    // 如果没有客户端订阅 stream 1，则直接返回
    if (stream_subscriber_counts_.count(1) == 0 || stream_subscriber_counts_[1] <= 0) {
        return;
    }

    uint64_t now_ms = mg_millis();
    double t = now_ms / 1000.0;
    uint64_t ts_ns = now_ms * 1000000ULL;

    std::vector<uint8_t> frame;
    build_waveform_frame(frame, 1, seq_++, t, ts_ns);

    for (struct mg_connection *c = mgr_.conns; c != NULL; c = c->next) {
        if (c->is_websocket && c->data[0] == CONN_STREAM) {
            mg_ws_send(c, frame.data(), frame.size(), WEBSOCKET_OP_BINARY);
        }
    }
}

// ---------- 路由与事件 ----------
void Mongoose_Server::on_http_msg(struct mg_connection *c, struct mg_http_message *hm) {
    if (mg_strcmp(hm->uri, mg_str("/stream")) == 0) {
        ws_upgrade(c, hm);
        c->data[0] = CONN_STREAM;
        MG_INFO(("upgrade /stream"));
        return;
    }
    if (mg_strcmp(hm->uri, mg_str("/control")) == 0) {
        ws_upgrade(c, hm);
        c->data[0] = CONN_CONTROL;
        MG_INFO(("upgrade /control"));
        return;
    }
    struct mg_http_serve_opts opts = {};
    opts.root_dir = "www";
    mg_http_serve_dir(c, hm, &opts);
}

void Mongoose_Server::on_ws_msg(struct mg_connection *c, struct mg_ws_message *wm) {
    if (c->data[0] == CONN_CONTROL) {
        handle_control_text(c, wm);
    }
}

void Mongoose_Server::event_fn(struct mg_connection *c, int ev, void *ev_data) {
    Mongoose_Server *self = static_cast<Mongoose_Server *>(c->fn_data);

    if (ev == MG_EV_HTTP_MSG) {
        self->on_http_msg(c, static_cast<struct mg_http_message *>(ev_data));
    } else if (ev == MG_EV_WS_OPEN) {
        MG_INFO(("ws open (type=%d)", (int)c->data[0]));
    } else if (ev == MG_EV_WS_MSG) {
        self->on_ws_msg(c, static_cast<struct mg_ws_message *>(ev_data));
    } else if (ev == MG_EV_CLOSE) {
        MG_INFO(("close (type=%d)", (int)c->data[0]));
    }
}

void Mongoose_Server::push_timer(void *arg) {
    static_cast<Mongoose_Server *>(arg)->push_waveform();
}

Mongoose_Server::Mongoose_Server(const char *listen_url)
    : listen_url_(listen_url), seq_(0), listening_(false) {
    mg_mgr_init(&mgr_);

    if (mg_http_listen(&mgr_, listen_url_, &event_fn, this) == NULL) {
        MG_ERROR(("Cannot listen on %s", listen_url_));
    } else {
        listening_ = true;
        MG_INFO(("Listening on %s", listen_url_));
        mg_timer_add(&mgr_, 100, MG_TIMER_REPEAT, &push_timer, this);
    }
}

Mongoose_Server::~Mongoose_Server() {
    mg_mgr_free(&mgr_);
}

void Mongoose_Server::run() {
    if (!listening_) return;
    for (;;) mg_mgr_poll(&mgr_, 10);
}