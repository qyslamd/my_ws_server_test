/* =========================================================================
 * mongoose 纯 C 教学 demo —— 单文件,零依赖,读完这一个文件就懂骨架
 *
 * 和 apps/mongoose_cpp(双通道波形,已按协议改造)不同,这个 demo 不做任何
 * 业务,只演示 mongoose 7.x 的最小骨架:
 *
 *   1. 事件循环:  mg_mgr_init -> mg_http_listen -> for( mg_mgr_poll )
 *   2. 事件处理器: 一个函数响应一个连接生命周期里的所有事件(MG_EV_*)
 *   3. 三类事件:   HTTP 请求 / WebSocket 消息 / 定时器
 *
 * 功能(刻意保持最小):
 *   - GET /            内嵌的最小测试页(不依赖 www/ 目录)
 *   - GET /api/uptime  JSON:返回服务端已运行的秒数
 *   - WS  /ws          文本回显 + 每 2 秒向所有 WS 连接广播心跳
 *
 * 编译后独立运行,工作目录任意:
 *   build\mongoose_c_demo.exe     然后浏览器开 http://127.0.0.1:8766/
 *
 * 学习建议:把下面 fn() 里的 is_hexdumping 打开,重编译再看一次日志,
 * 所有收发字节会以十六进制打出来,握手、帧格式一目了然。
 * ========================================================================= */

#include <stdio.h>

#include "mongoose.h"

/* 监听地址。端口避开 mongoose_cpp_demo 的 8765,两者可同时跑。 */
static const char* s_listen_url = "http://127.0.0.1:8766";

/* 已运行的秒数,由定时器累加,/api/uptime 读它返回。 */
static int s_uptime_seconds = 0;

/* -------------------------------------------------------------------------
 * 内嵌测试页。
 * 演示"最小页面直接当字符串发出去"的做法:学习阶段不必为 demo 配 www 目录。
 * 页面里统一用单引号,免得在 C 字符串里转义双引号。
 * ------------------------------------------------------------------------- */
static const char* s_index_html =
    "<!DOCTYPE html>\n"
    "<html><head><meta charset='utf-8'>"
    "<title>mongoose_c demo</title></head>\n"
    "<body>\n"
    "<h3>mongoose 纯 C demo</h3>\n"
    "<p>状态: <span id='st'>未连接</span></p>\n"
    "<button id='c'>连接</button> <button id='d' disabled>断开</button>\n"
    "<p><input id='msg' value='hello'> <button id='s' "
    "disabled>发送</button></p>\n"
    "<pre id='log'></pre>\n"
    "<script>\n"
    "var ws = null;\n"
    "function log(s) {\n"
    "  var el = document.getElementById('log');\n"
    "  el.textContent += s + '\\n';\n"
    "}\n"
    "function set(st) { document.getElementById('st').textContent = st; }\n"
    "document.getElementById('c').onclick = function () {\n"
    "  ws = new WebSocket('ws://' + location.host + '/ws');\n"
    "  ws.onopen = function () {\n"
    "    set('已连接');\n"
    "    document.getElementById('s').disabled = false;\n"
    "    document.getElementById('d').disabled = false;\n"
    "  };\n"
    "  ws.onmessage = function (e) { log(e.data); };\n" /* 回显和广播都走这里 */
    "  ws.onclose = function () {\n"
    "    set('已断开');\n"
    "    document.getElementById('s').disabled = true;\n"
    "    document.getElementById('d').disabled = true;\n"
    "  };\n"
    "};\n"
    "document.getElementById('d').onclick = function () { ws.close(); };\n"
    "document.getElementById('s').onclick = function () {\n"
    "  var v = document.getElementById('msg').value;\n"
    "  ws.send(v);\n"
    "  log('我发了: ' + v);\n" /* 服务端会回 'echo: ' + v */
    "};\n"
    "</script></body></html>\n";

/* -------------------------------------------------------------------------
 * 向所有 WebSocket 连接广播一行文本。
 *
 * mongoose 没有内置"广播"概念:所谓广播就是自己遍历 mgr->conns 链表,
 * 对每个 is_websocket 的连接 mg_ws_send 一遍。连接对象全在这个单链表里。
 * ------------------------------------------------------------------------- */
static void broadcast(struct mg_mgr* mgr, const char* text) {
  struct mg_connection* c;
  for (c = mgr->conns; c != NULL; c = c->next) {
    if (!c->is_websocket)
      continue; /* 跳过监听 socket、HTTP 连接 */
    mg_ws_printf(c, WEBSOCKET_OP_TEXT, "%s", text);
  }
}

/* 定时器回调:mg_timer_add 每 2 秒调一次。 */
static void timer_fn(void* arg) {
  struct mg_mgr* mgr = (struct mg_mgr*)arg;
  char buf[64];

  s_uptime_seconds += 2;
  snprintf(buf, sizeof(buf), "tick #%d (up %ds)", s_uptime_seconds / 2,
           s_uptime_seconds);
  broadcast(mgr, buf);
}

/* -------------------------------------------------------------------------
 * 事件处理器:mongoose 的核心。
 *
 * 一个连接从创建到销毁,发生的每件事都以 ev 通知到这个函数:
 *   MG_EV_OPEN     连接对象创建(监听 socket 也会走到)
 *   MG_EV_ACCEPT   有客户端连上来(TCP 三次握手完成)
 *   MG_EV_HTTP_MSG 收到一个完整 HTTP 请求
 *   MG_EV_WS_OPEN  WebSocket 握手完成
 *   MG_EV_WS_MSG   收到 WebSocket 数据帧
 *   MG_EV_WS_CTL   收到 WebSocket 控制帧(ping/pong/close)
 *   MG_EV_CLOSE    连接关闭
 *
 * 3 参数签名是 mongoose 7.14+ 的新式写法;每连接私有数据不再通过函数参数
 * 传,而是用 c->fn_data(继承自 mg_http_listen 时给的值)或 c->data[32]
 * 小缓冲。本 demo 无状态,两个都不用。
 * ------------------------------------------------------------------------- */
static void fn(struct mg_connection* c, int ev, void* ev_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message* hm = (struct mg_http_message*)ev_data;

    if (mg_match(hm->uri, mg_str("/api/uptime"), NULL)) {
      /* 最简单的 REST:直接回一段 JSON。 */
      mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                    "{\"uptime\":%d}\n", s_uptime_seconds);
    } else if (mg_match(hm->uri, mg_str("/ws"), NULL)) {
      /* 升级成 WebSocket。之后这个连接收到的就是 MG_EV_WS_MSG。 */
      mg_ws_upgrade(c, hm, NULL);
    } else {
      /* 其余路径一律回内嵌页。真实项目在这里换成 mg_http_serve_dir。 */
      mg_http_reply(c, 200, "Content-Type: text/html; charset=utf-8\r\n", "%s",
                    s_index_html);
    }
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;

    /* 回显:把收到的文本加个前缀发回去,页面日志立刻能看到。 */
    mg_ws_printf(c, WEBSOCKET_OP_TEXT, "echo: %.*s", (int)wm->data.len,
                 wm->data.buf);
  } else if (ev == MG_EV_WS_OPEN) {
    /* 新 WS 连接:打开十六进制转储,便于观察握手后的帧格式。 */
    c->is_hexdumping = 1;
    mg_ws_send(c, "welcome", 7, WEBSOCKET_OP_TEXT);
  } else if (ev == MG_EV_CLOSE) {
    /* 这里适合做连接清理。本 demo 无状态,只打一行日志。 */
    if (c->is_websocket)
      printf("ws 连接关闭\n");
  }

  /* 其它事件(MG_EV_OPEN/ACCEPT/POLL...)不关心就静默通过,
   * 这是 mongoose 的常态:处理器只挑自己要的事件响应。 */
}

int main(void) {
  struct mg_mgr mgr; /* 全局管理器:所有连接、定时器都挂在它上面 */

  mg_log_set(MG_LL_INFO); /* 学习期可改 MG_LL_DEBUG 看更多内部日志 */
  mg_mgr_init(&mgr);

  /* 定时器:每 2000ms 调一次 timer_fn,参数是 mgr(用来广播)。 */
  mg_timer_add(&mgr, 2000, MG_TIMER_REPEAT, timer_fn, &mgr);

  /* 监听。最后一个参数会落到每个连接的 c->fn_data 上,本 demo 不用。 */
  mg_http_listen(&mgr, s_listen_url, fn, NULL);
  printf("mongoose_c demo 监听 %s\n", s_listen_url);

  /* 事件循环:mongoose 的全部"运行时"就是这个循环。
   * 200 是阻塞上限(ms),到点或任一连接有事件就返回。 */
  for (;;) {
    mg_mgr_poll(&mgr, 200);
  }

  /* 上面是死循环,这里永远走不到; respectable 退出应是 SIGINT 里
   * mg_mgr_free(&mgr) 后 return 0。留着这行只是形式完整。 */
  mg_mgr_free(&mgr);
  return 0;
}
