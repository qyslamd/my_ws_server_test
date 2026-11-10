#include "lws_server.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>

// 单实例服务:回调经此指针访问成员(lws 回调无 per-server 用户指针)
static LWS_Server *g_self = nullptr;

// ---------- 二进制帧构造 ----------
static void build_waveform_frame(unsigned char *buf, size_t *len,
                                 uint32_t stream_id, uint32_t seq, double t) {
    unsigned char *p = buf;
    p[0] = 0xA5; p[1] = 0x01; p[2] = 0x02;
    p[3] = 0x01 | 0x10 | 0x20;
    memcpy(p + 4, &stream_id, 4);
    memcpy(p + 8, &seq, 4);
    p += 12;

    const int N = 1024;
    const char *meta = "{\"kind\":\"waveform\",\"dataType\":\"float32\",\"components\":1,"
                       "\"count\":1024,\"unit\":\"V\",\"sampleRate\":1000}";
    uint32_t metaLen = (uint32_t)strlen(meta);
    uint32_t dataLen = N * sizeof(float);

    memcpy(p, &dataLen, 4); p += 4;
    float *samples = (float *)p;
    double freq = 50.0 + 20.0 * sin(2.0 * 3.14159265358979323846 * 0.5 * t);
    for (int i = 0; i < N; i++) {
        samples[i] = (float)sin(2.0 * 3.14159265358979323846 * freq * (t + i / 1000.0));
    }
    p += dataLen;

    memcpy(p, &metaLen, 4); p += 4;
    memcpy(p, meta, metaLen); p += metaLen;

    uint32_t tsLen = 8;
    memcpy(p, &tsLen, 4); p += 4;
    uint64_t ts = (uint64_t)time(NULL) * 1000000000ULL;
    memcpy(p, &ts, 8); p += 8;

    *len = (size_t)(p - buf);
}

int LWS_Server::stream_cb(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len) {
    (void)user; (void)in; (void)len;
    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        fprintf(stderr, "[lws] stream established\n");
        if (g_self) {
            g_self->stream_wsi_ = wsi;
            // 连接建立时,立刻请求一次写入
            lws_callback_on_writable(wsi);
        }
        break;
    case LWS_CALLBACK_RECEIVE:
        break;
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        // 写入点节流:WRITEABLE 的实际触发频率不受 arm 频率约束(实测
        // 可达数千次/秒),帧率必须在此处封顶,目标 ~100Hz
        uint64_t now = (uint64_t)lws_now_usecs();
        if (now - g_self->last_push_us_ < 10000) break;
        g_self->last_push_us_ = now;

        unsigned char buf[LWS_PRE + 12 + 4 + 4096 + 4 + 256 + 4 + 8];
        size_t frame_len = 0;
        build_waveform_frame(&buf[LWS_PRE], &frame_len, 1, g_self->seq_++, (double)time(NULL));

        int n = lws_write(wsi, &buf[LWS_PRE], frame_len, LWS_WRITE_BINARY);
        if (n < 0) fprintf(stderr, "[lws] stream write error\n");
        break;
    }
    case LWS_CALLBACK_CLOSED:
        fprintf(stderr, "[lws] stream closed\n");
        if (g_self && g_self->stream_wsi_ == wsi) g_self->stream_wsi_ = nullptr;
        break;
    default:
        break;
    }
    return 0;
}

int LWS_Server::control_cb(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    (void)user;
    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        fprintf(stderr, "[lws] control established\n");
        break;
    case LWS_CALLBACK_RECEIVE: {
        char msg[4096];
        if (len < sizeof(msg)) {
            memcpy(msg, in, len);
            msg[len] = '\0';
            fprintf(stderr, "[lws] control rx: %s\n", msg);
            char resp[512];
            snprintf(resp, sizeof(resp),
                     "{\"v\":1,\"type\":\"ack\",\"id\":\"c-0001\","
                     "\"name\":\"hello\",\"payload\":{\"proto\":1,"
                     "\"server\":\"lws-skeleton/1.0\",\"caps\":[\"stream\"]},\"ts\":0}");
            unsigned char buf[LWS_PRE + 512];
            memcpy(&buf[LWS_PRE], resp, strlen(resp));
            lws_write(wsi, &buf[LWS_PRE], strlen(resp), LWS_WRITE_TEXT);
        }
        break;
    }
    case LWS_CALLBACK_CLOSED:
        fprintf(stderr, "[lws] control closed\n");
        break;
    default:
        break;
    }
    return 0;
}

int LWS_Server::http_cb(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len) {
    (void)user; (void)in; (void)len;
    if (reason == LWS_CALLBACK_HTTP) {
        // 静态文件由 "/" -> ./www 挂载点在 lws 内部处理,
        // 此分支仅在请求落到挂载点之外时兜底(正常情况不会触发)
        lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, NULL);
        return 1;
    }
    if (reason == LWS_CALLBACK_HTTP_FILE_COMPLETION) {
        // 本构建下 h1 keep-alive 连接复用存在缺陷:第二个请求会收到
        // 上一响应头的原样重放、正文永远不出,15s 后连接超时(实测
        // 浏览器表现为页面加载极慢 + ERR_CONTENT_LENGTH_MISMATCH)。
        // 与 lws 自带 test-server 一致:每个事务完成后关闭连接,不做复用。
        return -1;
    }
    return 0;
}

// 协议表与挂载点:回调绑定到类的静态成员函数(私有静态成员的类外定义)
struct lws_protocols LWS_Server::k_protocols_[] = {
    { "http",    LWS_Server::http_cb,    0, 0, 0, NULL, 0 },
    { "stream",  LWS_Server::stream_cb,  0, 0, 0, NULL, 0 },
    { "control", LWS_Server::control_cb, 0, 0, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

// www/ 静态资源挂载点:GET / 与静态文件均由 lws 内部从磁盘服务
const struct lws_http_mount LWS_Server::k_mount_www_ = {
    NULL,             /* .mount_next */
    "/",              /* .mountpoint */
    "./www",          /* .origin: 从磁盘提供 */
    "index.html",     /* .def: 默认页 */
    NULL,             /* .protocol */
    NULL,             /* .cgienv */
    NULL,             /* .extra_mimetypes */
    NULL,             /* .interpret */
    0,                /* .cgi_timeout */
    0,                /* .cache_max_age */
    0,                /* .auth_mask */
    0, 0, 0,          /* .cache_reusable / .cache_revalidate / .cache_intermediaries */
    LWSMPRO_FILE,     /* .origin_protocol */
    1,                /* .mountpoint_len: strlen("/") */
    NULL              /* .basic_auth_login_file */
};

LWS_Server::LWS_Server(uint16_t port)
    : ctx_(nullptr), stream_wsi_(nullptr), seq_(0), last_push_us_(0), port_(port) {
    g_self = this;
}

LWS_Server::~LWS_Server() {
    if (ctx_) lws_context_destroy(ctx_);
    if (g_self == this) g_self = nullptr;
}

int LWS_Server::run() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port_;
    info.protocols = k_protocols_;
    info.mounts = &k_mount_www_;
    info.gid = -1;
    info.uid = -1;
    // info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

    ctx_ = lws_create_context(&info);
    if (!ctx_) {
        fprintf(stderr, "[lws] context creation failed\n");
        return 1;
    }

    fprintf(stderr, "[lws] listening on http://127.0.0.1:%u/\n", (unsigned)port_);

    for (;;) {
        lws_service(ctx_, 10); // 每 10ms 唤醒一次,相当于 100Hz 推流
        if (stream_wsi_) {
            // 持续请求可写;推流节奏由 stream_cb 的写入点节流封顶(~100Hz)
            lws_callback_on_writable(stream_wsi_);
        }
    }
}

int lws_server_main(void) {
    LWS_Server server(8766);
    return server.run();
}
