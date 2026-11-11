#pragma once
#include "mongoose.h"
#include <map>
#include <cstdint>

class Mongoose_Server
{
public:
    explicit Mongoose_Server(const char *listen_url);
    ~Mongoose_Server();
    void run();

private:
    static void event_fn(struct mg_connection *c, int ev, void *ev_data);
    static void push_timer(void *arg);
    void on_http_msg(struct mg_connection *c, struct mg_http_message *hm);
    void on_ws_msg(struct mg_connection *c, struct mg_ws_message *wm);
    void push_waveform();
    static void ws_upgrade(struct mg_connection *c, struct mg_http_message *hm);
    void handle_control_text(struct mg_connection *c, struct mg_ws_message *wm);

    const char *listen_url_;
    struct mg_mgr mgr_;
    uint32_t seq_;
    bool listening_;
    
    // 记录每个数据流当前的订阅者数量
    std::map<uint32_t, int> stream_subscriber_counts_; 
};