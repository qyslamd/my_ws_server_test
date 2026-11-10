#pragma once

#include "mongoose.h"

// Mongoose 版双通道 WS 服务端封装
//   /control  文本通道(JSON 信封,双向:命令/应答/心跳)
//   /stream   二进制通道(0xA5 帧,服务端单向定时推流)
// 静态页面由 www/ 目录提供(见 event_fn 中的 mg_http_serve_dir)。
class Mongoose_Server
{
public:
    explicit Mongoose_Server(const char *listen_url);
    ~Mongoose_Server();

    // 阻塞事件循环(100ms 定时推流);监听失败时立即返回
    void run();

private:
    // mg 事件回调(经 fn_data 携带实例指针)
    static void event_fn(struct mg_connection *c, int ev, void *ev_data);
    static void push_timer(void *arg);   // mg_timer_add 回调,arg = this

    void on_http_msg(struct mg_connection *c, struct mg_http_message *hm);
    void on_ws_msg(struct mg_connection *c, struct mg_ws_message *wm);
    void push_waveform();                // 向所有 stream 连接推送一帧

    // 子协议白名单校验(mongoose 自动回显子协议头,这里不手动追加)
    static void ws_upgrade(struct mg_connection *c, struct mg_http_message *hm);
    static void handle_control_text(struct mg_connection *c, struct mg_ws_message *wm);

    const char *listen_url_;
    struct mg_mgr mgr_;
    uint32_t seq_;
    bool listening_;
};
