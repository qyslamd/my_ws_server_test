#pragma once
#include "mongoose.h"
#include <map>
#include <set>
#include <string>
#include <cstdint>

// 连接类型标签
enum { CONN_NONE = 0, CONN_STREAM = 1, CONN_CONTROL = 2 };

// 每个 WebSocket 连接专属的上下文
struct ClientContext {
    int conn_type = CONN_NONE;
    std::string session_id;     // 用于关联 control 和 stream
    bool is_handshaked = false; // 控制通道是否已 hello
    bool snapshot_sent = false; // 数据通道是否已发快照
    uint64_t last_active_ms = 0; // 最后活跃时间（收到数据或 Pong）
};

class Mongoose_Server
{
public:
    explicit Mongoose_Server(const char *listen_url);
    ~Mongoose_Server();
    void run();

    // 静态实例，供 event_fn 静态函数访问 Server 成员
    static Mongoose_Server* instance_;

private:
    static void event_fn(struct mg_connection *c, int ev, void *ev_data);
    static void push_timer(void *arg);
    static void ws_ping_timer(void *arg); // 协议级探活定时器
    
    void on_http_msg(struct mg_connection *c, struct mg_http_message *hm);
    void on_ws_msg(struct mg_connection *c, struct mg_ws_message *wm);
    void on_ws_ctl(struct mg_connection *c, struct mg_ws_message *wm);
    void push_waveform();
    static void ws_upgrade(struct mg_connection *c, struct mg_http_message *hm);
    void handle_control_text(struct mg_connection *c, struct mg_ws_message *wm);

    const char *listen_url_;
    struct mg_mgr mgr_;
    uint32_t seq_;
    bool listening_;

    // 基于 Session 的订阅管理
    std::map<std::string, std::set<uint32_t>> session_subscriptions_;
};