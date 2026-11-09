#include <cstdio>
#include <cstring>

#include "mongoose.h"

static const char *s_listen_url = "http://127.0.0.1:8765";

// 内嵌测试页：浏览器打开 http://127.0.0.1:8765/ 即可
static const char *s_index_html = R"html(<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>Mongoose WS Echo</title></head>
<body style="font-family:monospace">
  <h2>Mongoose WebSocket Echo Test</h2>
  <button id="btn">Connect & Send</button>
  <pre id="log" style="background:#111;color:#0f0;padding:8px;min-height:200px"></pre>
  <script>
    let ws;
    const log = (m) => { document.getElementById('log').textContent += m + "\n"; };
    document.getElementById('btn').onclick = () => {
      if (ws && ws.readyState === WebSocket.OPEN) { ws.close(); return; }
      ws = new WebSocket('ws://' + location.host + '/ws');
      ws.onopen = () => {
        log('[open]');
        ws.send(JSON.stringify({v:1,type:'req',id:'c-0001',method:'echo',payload:{msg:'hello'}}));
      };
      ws.onmessage = (e) => { log('[recv] ' + e.data); };
      ws.onerror  = () => { log('[error]'); };
      ws.onclose  = () => { log('[closed]'); };
    };
  </script>
</body>
</html>)html";

static void fn(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    if (mg_strcmp(hm->uri, mg_str("/ws")) == 0) {
      mg_ws_upgrade(c, hm, NULL);
      return;
    }

    mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", s_index_html);
    return;
  }

  if (ev == MG_EV_WS_OPEN) {
    MG_INFO(("WS open"));
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    MG_INFO(("WS recv %u bytes: %.*s", (unsigned)wm->data.len,
             (int)wm->data.len, wm->data.buf));
    // 原样回发
    mg_ws_send(c, wm->data.buf, wm->data.len, WEBSOCKET_OP_TEXT);
  } else if (ev == MG_EV_WS_CTL) {
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    if ((wm->flags & 0x0f) == WEBSOCKET_OP_CLOSE) {
      MG_INFO(("WS close"));
    }
  }
}

int main() {
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_log_set(MG_LL_INFO);

  MG_INFO(("Listening on %s", s_listen_url));
  if (mg_http_listen(&mgr, s_listen_url, fn, NULL) == NULL) {
    MG_ERROR(("Cannot listen on %s", s_listen_url));
    mg_mgr_free(&mgr);
    return 1;
  }

  for (;;) mg_mgr_poll(&mgr, 1000);

  mg_mgr_free(&mgr);
  return 0;
}