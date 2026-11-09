#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

#include "mongoose.h"

static const char *s_listen_url = "http://127.0.0.1:8765";
static const double PI = 3.14159265358979323846;

#define CONN_NONE    0
#define CONN_STREAM  1
#define CONN_CONTROL 2

// ---------- 内嵌前端测试页 ----------
static const char *s_index_html = R"html(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>双通道 WS 波形测试</title>
<style>
body { font-family: monospace; background: #1e1e1e; color: #ddd; margin: 20px; }
canvas { background: #111; border: 1px solid #444; display: block; margin: 12px 0; }
#log { background: #111; color: #0f0; padding: 8px; height: 140px; overflow-y: auto; font-size: 12px; margin: 0; }
button { margin-right: 8px; padding: 4px 12px; }
#status { margin-left: 8px; color: #0f0; }
</style>
</head>
<body>
<h2>双通道 WebSocket 波形测试</h2>
<div>
  <button id="btnConnect">连接</button>
  <button id="btnDisconnect">断开</button>
  <span id="status">未连接</span>
</div>
<canvas id="canvas" width="1024" height="240"></canvas>
<pre id="log"></pre>
<script>
const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
const logEl = document.getElementById('log');
const statusEl = document.getElementById('status');

let wsBin = null;
let wsTxt = null;
let frameCount = 0;

function log(msg) {
  const t = new Date().toLocaleTimeString();
  logEl.textContent += '[' + t + '] ' + msg + '\n';
  logEl.scrollTop = logEl.scrollHeight;
}

function drawWaveform(dataBuf, meta) {
  const dv = new DataView(dataBuf);
  const count = meta.count;
  const W = canvas.width, H = canvas.height;
  ctx.fillStyle = '#111';
  ctx.fillRect(0, 0, W, H);
  ctx.strokeStyle = '#333';
  ctx.beginPath(); ctx.moveTo(0, H/2); ctx.lineTo(W, H/2); ctx.stroke();

  let maxV = 1e-6;
  for (let i = 0; i < count; i++) {
    const v = Math.abs(dv.getFloat32(i * 4, true));
    if (v > maxV) maxV = v;
  }
  ctx.strokeStyle = '#0f0';
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let i = 0; i < count; i++) {
    const v = dv.getFloat32(i * 4, true);
    const x = (i / (count - 1)) * W;
    const y = H/2 - (v / maxV) * (H/2 - 4);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

function onBinaryMessage(ev) {
  const buf = ev.data;
  if (!(buf instanceof ArrayBuffer)) return;
  if (buf.byteLength < 12) return;
  const dv = new DataView(buf);
  if (dv.getUint8(0) !== 0xA5) { log('bad magic'); return; }
  const ver    = dv.getUint8(1);
  const opcode = dv.getUint8(2);
  const flags  = dv.getUint8(3);
  const streamId = dv.getUint32(4, true);
  const seq      = dv.getUint32(8, true);

  let off = 12;
  const blocks = {};
  const order = ['DATA', 'INDEX', 'COLOR', 'NORMAL', 'META', 'TS'];
  for (let i = 0; i < order.length; i++) {
    if (!(flags & (1 << i))) continue;
    if (off + 4 > buf.byteLength) break;
    const len = dv.getUint32(off, true); off += 4;
    if (off + len > buf.byteLength) break;
    blocks[order[i]] = buf.slice(off, off + len); off += len;
  }

  if (blocks.META) {
    let meta;
    try { meta = JSON.parse(new TextDecoder().decode(blocks.META)); }
    catch (e) { log('bad meta: ' + e); return; }
    if (meta.kind === 'waveform' && blocks.DATA) {
      drawWaveform(blocks.DATA, meta);
      frameCount++;
      if (frameCount % 10 === 0) {
        log('stream seq=' + seq + ' id=' + streamId + ' count=' + meta.count);
      }
    }
  }
}

function connect() {
  if (wsBin && wsBin.readyState === WebSocket.OPEN) return;
  const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
  const base = proto + location.host;

  wsTxt = new WebSocket(base + '/control');
  wsTxt.onopen = () => {
    log('[control] open');
    wsTxt.send(JSON.stringify({v:1, type:'cmd', id:'c-0001', name:'hello',
      payload:{proto:1, client:'web/1.0.0', caps:['stream']}, ts: Date.now()}));
    wsTxt.send(JSON.stringify({v:1, type:'cmd', id:'c-0002', name:'subscribe',
      payload:{streams:['waveform']}, ts: Date.now()}));
  };
  wsTxt.onmessage = (ev) => log('[control] ' + ev.data);
  wsTxt.onclose = () => log('[control] closed');
  wsTxt.onerror = () => log('[control] error');

  wsBin = new WebSocket(base + '/stream');
  wsBin.binaryType = 'arraybuffer';
  wsBin.onopen = () => { log('[stream] open'); statusEl.textContent = '已连接'; frameCount = 0; };
  wsBin.onmessage = onBinaryMessage;
  wsBin.onclose = () => { log('[stream] closed'); statusEl.textContent = '已断开'; };
  wsBin.onerror = () => log('[stream] error');
}

function disconnect() {
  if (wsBin) { wsBin.close(); wsBin = null; }
  if (wsTxt) { wsTxt.close(); wsTxt = null; }
  statusEl.textContent = '未连接';
}

document.getElementById('btnConnect').onclick = connect;
document.getElementById('btnDisconnect').onclick = disconnect;
</script>
</body>
</html>)html";

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
    memcpy(p, meta, metaLen); p += metaLen;

    len = 8;
    memcpy(p, &len, 4); p += 4;
    memcpy(p, &ts_ns, 8);
}

// ---------- 极简 JSON 字段提取（仅字符串值，用于骨架）----------
static std::string json_get_str(const std::string &s, const std::string &key) {
    std::string pat = "\"" + key + "\":\"";
    size_t p = s.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    size_t e = s.find('"', p);
    if (e == std::string::npos) return "";
    return s.substr(p, e - p);
}

static void handle_control_text(struct mg_connection *c, struct mg_ws_message *wm) {
    std::string msg(wm->data.buf, wm->data.len);
    std::string name = json_get_str(msg, "name");
    std::string id = json_get_str(msg, "id");
    if (id.empty()) id = "c-0000";

    std::string resp;
    if (name == "hello") {
        resp = "{\"v\":1,\"type\":\"ack\",\"id\":\"" + id +
               "\",\"name\":\"hello\",\"payload\":{\"proto\":1,"
               "\"server\":\"ws-skeleton/1.0\",\"caps\":[\"stream\",\"snapshot\"],"
               "\"session\":\"s-001\"},\"ts\":0}";
    } else if (name == "subscribe") {
        resp = "{\"v\":1,\"type\":\"ack\",\"id\":\"" + id +
               "\",\"name\":\"subscribe\",\"payload\":{\"ok\":true},\"ts\":0}";
    } else if (name == "unsubscribe") {
        resp = "{\"v\":1,\"type\":\"ack\",\"id\":\"" + id +
               "\",\"name\":\"unsubscribe\",\"payload\":{\"ok\":true},\"ts\":0}";
    } else if (name == "ping") {
        resp = "{\"v\":1,\"type\":\"ack\",\"id\":\"" + id +
               "\",\"name\":\"ping\",\"payload\":{},\"ts\":0}";
    } else {
        resp = "{\"v\":1,\"type\":\"err\",\"id\":\"" + id + "\",\"name\":\"" +
               (name.empty() ? std::string("unknown") : name) +
               "\",\"payload\":{\"code\":1005,\"msg\":\"unknown command\"},\"ts\":0}";
    }
    mg_ws_send(c, resp.data(), resp.size(), WEBSOCKET_OP_TEXT);
    MG_INFO(("control rx: name=%s id=%s", name.empty() ? "(empty)" : name.c_str(), id.c_str()));
}

// ---------- 定时推送 ----------
static uint32_t s_seq = 0;

static void push_timer(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;
    uint64_t now_ms = mg_millis();
    double t = now_ms / 1000.0;
    uint64_t ts_ns = now_ms * 1000000ULL;

    std::vector<uint8_t> frame;
    build_waveform_frame(frame, 1, s_seq++, t, ts_ns);

    for (struct mg_connection *c = mgr->conns; c != NULL; c = c->next) {
        if (c->is_websocket && c->data[0] == CONN_STREAM) {
            mg_ws_send(c, frame.data(), frame.size(), WEBSOCKET_OP_BINARY);
        }
    }
}

// ---------- 主事件处理 ----------
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        if (mg_strcmp(hm->uri, mg_str("/stream")) == 0) {
            mg_ws_upgrade(c, hm, NULL);
            c->data[0] = CONN_STREAM;
            MG_INFO(("upgrade /stream"));
            return;
        }
        if (mg_strcmp(hm->uri, mg_str("/control")) == 0) {
            mg_ws_upgrade(c, hm, NULL);
            c->data[0] = CONN_CONTROL;
            MG_INFO(("upgrade /control"));
            return;
        }
        mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", s_index_html);
        return;
    }

    if (ev == MG_EV_WS_OPEN) {
        MG_INFO(("ws open (type=%d)", (int)c->data[0]));
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        if (c->data[0] == CONN_CONTROL) {
            handle_control_text(c, wm);
        }
        // stream 通道收到前端消息：防御性丢弃，不处理
    } else if (ev == MG_EV_CLOSE) {
        MG_INFO(("close (type=%d)", (int)c->data[0]));
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

    mg_timer_add(&mgr, 100, MG_TIMER_REPEAT, push_timer, &mgr);

    for (;;) mg_mgr_poll(&mgr, 10);

    mg_mgr_free(&mgr);
    return 0;
}