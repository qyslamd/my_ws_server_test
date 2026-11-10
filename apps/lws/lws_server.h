#pragma once

#include <stdint.h>
#include <libwebsockets.h>

#ifdef __cplusplus

// libwebsockets 版双通道 WS 服务端封装
//   stream   二进制通道(0xA5 帧,服务端单向推流,按子协议路由)
//   control  文本通道(JSON 信封,双向,按子协议路由)
// 静态页面由 "/" -> www/ 挂载点从磁盘提供(lws_http_mount)。
class LWS_Server
{
public:
    explicit LWS_Server(uint16_t port);
    ~LWS_Server();

    // 阻塞事件循环(~100Hz 节流推流);返回 0 表示正常退出
    int run();

private:
    // lws 协议回调(lws 无 per-server 用户指针,实例经文件内 g_self 获取)
    static int http_cb(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);
    static int stream_cb(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len);
    static int control_cb(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len);

    struct lws_context *ctx_;
    struct lws *stream_wsi_;     // 当前的 stream 连接
    uint32_t seq_;
    uint64_t last_push_us_;      // 推流节流时间基
    uint16_t port_;

    // 协议表与挂载点:回调绑定到类的静态成员函数(私有静态成员)
    static struct lws_protocols k_protocols_[];
    static const struct lws_http_mount k_mount_www_;
};

extern "C" {
#endif /* __cplusplus */

// C 入口(供 lws_main.c 调用):阻塞运行,正常路径不返回
int lws_server_main(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
