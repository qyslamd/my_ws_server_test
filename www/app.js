// 双通道波形测试前端(mongoose / lws 两个服务端共用同一份)
// 线路协议见 docs/ws-protocol.md:
//   /control  文本通道(JSON 信封,双向)
//   /stream   二进制通道(0xA5 帧,服务端单向推流)
// 两个服务端均通过 Sec-WebSocket-Protocol 子协议区分通道。

class WaveformClient {
  constructor({ canvasId = 'canvas', logId = 'log', statusId = 'status' } = {}) {
    this.canvas = document.getElementById(canvasId);
    this.ctx = this.canvas.getContext('2d');
    this.logEl = document.getElementById(logId);
    this.statusEl = document.getElementById(statusId);
    this.wsBin = null;
    this.wsTxt = null;
    this.frameCount = 0;
    
    // 新增：记录前端当前订阅的 streamId
    this.subscribedStreams = new Set();
  }

  log(msg) {
    const t = new Date().toLocaleTimeString();
    this.logEl.textContent += '[' + t + '] ' + msg + '\n';
    this.logEl.scrollTop = this.logEl.scrollHeight;
  }

  drawWaveform(dataBuf, meta) {
    const dv = new DataView(dataBuf);
    const count = meta.count;
    const W = this.canvas.width, H = this.canvas.height;
    const ctx = this.ctx;
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

  // 解析 0xA5 帧:固定头 12B + 按 flags 位序排列的 [len][data] 块
  onBinaryMessage(ev) {
    const buf = ev.data;
    if (!(buf instanceof ArrayBuffer)) return;
    if (buf.byteLength < 12) return;
    const dv = new DataView(buf);
    if (dv.getUint8(0) !== 0xA5) { this.log('bad magic'); return; }
    
    const flags    = dv.getUint8(3);
    const streamId = dv.getUint32(4, true);
    const seq      = dv.getUint32(8, true);

    // 新增：本地过滤，服务端虽然广播了，但我没订阅的话直接丢弃
    if (!this.subscribedStreams.has(streamId)) {
      return;
    }

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
      catch (e) { this.log('bad meta: ' + e); return; }
      if (meta.kind === 'waveform' && blocks.DATA) {
        this.drawWaveform(blocks.DATA, meta);
        this.frameCount++;
        if (this.frameCount % 10 === 0) {
          this.log('stream seq=' + seq + ' id=' + streamId + ' count=' + meta.count);
        }
      }
    }
  }

  connect() {
    if (this.wsBin && this.wsBin.readyState === WebSocket.OPEN) return;
    const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
    const base = proto + location.host;

    this.wsTxt = new WebSocket(base + '/control', ['control']);
    this.wsTxt.onopen = () => {
      this.log('[control] open');
      this.wsTxt.send(JSON.stringify({v:1, type:'cmd', id:'c-0001', name:'hello',
        payload:{proto:1, client:'web/1.0.0', caps:['stream']}, ts: Date.now()}));
      
      // 只发一次订阅，订阅 streamId 为 1 的数据流
      this.wsTxt.send(JSON.stringify({v:1, type:'cmd', id:'c-0002', name:'subscribe',
        payload:{streams:[1]}, ts: Date.now()}));
        
      // 本地记录状态
      this.subscribedStreams.add(1);
    };
    this.wsTxt.onmessage = (ev) => this.log('[control] ' + ev.data);
    this.wsTxt.onclose = () => this.log('[control] closed');
    this.wsTxt.onerror = () => this.log('[control] error');

    this.wsBin = new WebSocket(base + '/stream', ['stream']);
    this.wsBin.binaryType = 'arraybuffer';
    this.wsBin.onopen = () => { this.log('[stream] open'); this.statusEl.textContent = '已连接'; this.frameCount = 0; };
    this.wsBin.onmessage = (ev) => this.onBinaryMessage(ev);
    this.wsBin.onclose = () => { this.log('[stream] closed'); this.statusEl.textContent = '未连接'; };
    this.wsBin.onerror = () => this.log('[stream] error');
  }

  disconnect() {
    if (this.wsBin) { this.wsBin.close(); this.wsBin = null; }
    if (this.wsTxt) { this.wsTxt.close(); this.wsTxt = null; }
    this.statusEl.textContent = '未连接';
    this.subscribedStreams.clear(); // 清空本地订阅状态
  }

  // 新增：前端主动退订接口
  unsubscribe(streamIds) {
    if (!this.wsTxt || this.wsTxt.readyState !== WebSocket.OPEN) return;
    
    // 发送退订指令给后端
    this.wsTxt.send(JSON.stringify({
      v: 1, type: 'cmd', id: 'c-' + Date.now(), name: 'unsubscribe',
      payload: { streams: streamIds }, ts: Date.now()
    }));
    
    // 移除本地状态
    streamIds.forEach(s => this.subscribedStreams.delete(s));
    
    // 清空画布
    this.ctx.fillStyle = '#111';
    this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    this.log('unsubscribed from ' + streamIds.join(','));
  }
}

const client = new WaveformClient();
document.getElementById('btnConnect').onclick = () => client.connect();
document.getElementById('btnDisconnect').onclick = () => client.disconnect();