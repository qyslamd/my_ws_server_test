#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

#include <libwebsockets.h>

#define CONN_NONE    0
#define CONN_STREAM  1
#define CONN_CONTROL 2

static const char *s_index_html =
"<!DOCTYPE html>"
"<html><head><meta charset='utf-8'><title>LWS 双通道波形</title>"
"<style>body{font-family:monospace;background:#1e1e1e;color:#ddd;margin:20px;}"
"canvas{background:#111;border:1px solid #444;display:block;margin:12px 0;}"
"#log{background:#111;color:#0f0;padding:8px;height:140px;overflow-y:auto;font-size:12px;}"
"button{margin-right:8px;padding:4px 12px;}</style></head><body>"
"<h2>LWS 双通道 WebSocket 波形测试</h2>"
"<button id='btnConnect'>连接</button><button id='btnDisconnect'>断开</button>"
"<span id='status'>未连接</span>"
"<canvas id='canvas' width='1024' height='240'></canvas><pre id='log'></pre>"
"<script>"
"const canvas=document.getElementById('canvas');"
"const ctx=canvas.getContext('2d');"
"const logEl=document.getElementById('log');"
"const statusEl=document.getElementById('status');"
"let wsBin=null,wsTxt=null,frameCount=0;"
"function log(m){const t=new Date().toLocaleTimeString();logEl.textContent+='['+t+'] '+m+'\\n';logEl.scrollTop=logEl.scrollHeight;}"
"function drawWaveform(buf,meta){"
"  const dv=new DataView(buf);const count=meta.count;"
"  const W=canvas.width,H=canvas.height;"
"  ctx.fillStyle='#111';ctx.fillRect(0,0,W,H);"
"  ctx.strokeStyle='#333';ctx.beginPath();ctx.moveTo(0,H/2);ctx.lineTo(W,H/2);ctx.stroke();"
"  let maxV=1e-6;for(let i=0;i<count;i++){const v=Math.abs(dv.getFloat32(i*4,true));if(v>maxV)maxV=v;}"
"  ctx.strokeStyle='#0f0';ctx.lineWidth=1;ctx.beginPath();"
"  for(let i=0;i<count;i++){"
"    const v=dv.getFloat32(i*4,true);"
"    const x=(i/(count-1))*W;const y=H/2-(v/maxV)*(H/2-4);"
"    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);"
"  }"
"  ctx.stroke();"
"}"
"function onBinaryMessage(ev){"
"  const buf=ev.data;if(!(buf instanceof ArrayBuffer))return;"
"  if(buf.byteLength<12)return;"
"  const dv=new DataView(buf);"
"  if(dv.getUint8(0)!==0xA5){log('bad magic');return;}"
"  const flags=dv.getUint8(3);const seq=dv.getUint32(8,true);"
"  let off=12;const blocks={};"
"  const order=['DATA','INDEX','COLOR','NORMAL','META','TS'];"
"  for(let i=0;i<order.length;i++){"
"    if(!(flags&(1<<i)))continue;"
"    if(off+4>buf.byteLength)break;"
"    const len=dv.getUint32(off,true);off+=4;"
"    if(off+len>buf.byteLength)break;"
"    blocks[order[i]]=buf.slice(off,off+len);off+=len;"
"  }"
"  if(blocks.META){"
"    let meta;try{meta=JSON.parse(new TextDecoder().decode(blocks.META));}"
"    catch(e){log('bad meta: '+e);return;}"
"    if(meta.kind==='waveform'&&blocks.DATA){"
"      drawWaveform(blocks.DATA,meta);frameCount++;"
"      if(frameCount%10===0)log('stream seq='+seq+' count='+meta.count);"
"    }"
"  }"
"}"
"function connect(){"
"  if(wsBin&&wsBin.readyState===WebSocket.OPEN)return;"
"  const proto=location.protocol==='https:'?'wss://':'ws://';"
"  const base=proto+location.host;"
"   wsTxt=new WebSocket(base+'/control', ['control']);"
"  wsTxt.onopen=()=>{log('[control] open');"
"    wsTxt.send(JSON.stringify({v:1,type:'cmd',id:'c-0001',name:'hello',"
"      payload:{proto:1,client:'web/1.0.0',caps:['stream']},ts:Date.now()}));"
"  };"
"  wsTxt.onmessage=(ev)=>log('[control] '+ev.data);"
"  wsTxt.onclose=()=>log('[control] closed');"
"  wsBin=new WebSocket(base+'/stream', ['stream']);"
"  wsBin.binaryType='arraybuffer';"
"  wsBin.onopen=()=>{log('[stream] open');statusEl.textContent='已连接';frameCount=0;};"
"  wsBin.onmessage=onBinaryMessage;"
"  wsBin.onclose=()=>{log('[stream] closed');statusEl.textContent='已断开';};"
"}"
"function disconnect(){"
"  if(wsBin){wsBin.close();wsBin=null;}if(wsTxt){wsTxt.close();wsTxt=null;}"
"  statusEl.textContent='未连接';"
"}"
"document.getElementById('btnConnect').onclick=connect;"
"document.getElementById('btnDisconnect').onclick=disconnect;"
"</script></body></html>";

static uint32_t s_seq = 0;
static struct lws_context *s_context = NULL;
static struct lws *g_stream_wsi = NULL;  // 记录当前的 stream 连接

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

static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    if (reason == LWS_CALLBACK_HTTP) {
        size_t html_len = strlen(s_index_html);
        
        // 构造响应头
        unsigned char header[LWS_PRE + 1024];
        unsigned char *start = &header[LWS_PRE];
        unsigned char *p = start;

        if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, start + sizeof(header))) return 1;
        if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE, 
                                         (unsigned char *)"text/html; charset=utf-8", 24, 
                                         &p, start + sizeof(header))) return 1;
        // 关键：添加 Content-Length，告诉浏览器实际大小
        if (lws_add_http_header_content_length(wsi, html_len, &p, start + sizeof(header))) return 1;
        if (lws_finalize_http_header(wsi, &p, start + sizeof(header))) return 1;

        // 发送头
        if (lws_write(wsi, start, p - start, LWS_WRITE_HTTP_HEADERS) < 0) return -1;

        // 发送体
        unsigned char body[LWS_PRE + 65536];
        memcpy(&body[LWS_PRE], s_index_html, html_len);
        if (lws_write(wsi, &body[LWS_PRE], html_len, LWS_WRITE_HTTP) < 0) return -1;

        // 指示事务完成
        lws_http_transaction_completed(wsi);
        return -1;
    }
    return 0;
}

static int callback_stream(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        fprintf(stderr, "[lws] stream established\n");
        g_stream_wsi = wsi;
        // 连接建立时，立刻请求一次写入
        lws_callback_on_writable(wsi);
        break;
    case LWS_CALLBACK_RECEIVE:
        break;
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        unsigned char buf[LWS_PRE + 12 + 4 + 4096 + 4 + 256 + 4 + 8];
        size_t frame_len = 0;
        build_waveform_frame(&buf[LWS_PRE], &frame_len, 1, s_seq++, (double)time(NULL));
        
        int n = lws_write(wsi, &buf[LWS_PRE], frame_len, LWS_WRITE_BINARY);
        if (n < 0) fprintf(stderr, "[lws] stream write error\n");
        
        // 发送完成后，如果是持续推流，可以在这里再次请求
        // 但为了防止过快，可以借用主循环的 lws_service(10) 节流
        break;
    }
    case LWS_CALLBACK_CLOSED:
        fprintf(stderr, "[lws] stream closed\n");
        if (g_stream_wsi == wsi) g_stream_wsi = NULL;
        break;
    default:
        break;
    }
    return 0;
}

static int callback_control(struct lws *wsi, enum lws_callback_reasons reason,
                            void *user, void *in, size_t len) {
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

static struct lws_protocols protocols[] = {
    { "http", callback_http, 0, 0, 0, NULL, 0 },
    { "stream", callback_stream, 0, 0, 0, NULL, 0 },
    { "control", callback_control, 0, 0, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

int main(void) {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = 8766;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    // info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

    s_context = lws_create_context(&info);
    if (!s_context) {
        fprintf(stderr, "[lws] context creation failed\n");
        return 1;
    }

    fprintf(stderr, "[lws] listening on http://127.0.0.1:8766/\n");

    while (1) {
        lws_service(s_context, 10); // 每 10ms 唤醒一次，相当于 100Hz 推流
        if (g_stream_wsi) {
            lws_callback_on_writable(g_stream_wsi);
        }
    }

    lws_context_destroy(s_context);
    return 0;
}