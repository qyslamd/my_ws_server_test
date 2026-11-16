// v1 底座客户端：REST 控制/查询 + WS 事件与数据流。
// 会话是 1:1 的，WS 必须先建立，REST 才带得上 X-Session-Id。

export class ApiError extends Error {
  constructor(status, message, detail) {
    super(message);
    this.status = status;
    this.detail = detail;
  }
}

export class Client {
  constructor() {
    this.sid = 'sid-' + Math.random().toString(36).slice(2, 10);
    this.ws = null;
    this.eventHandlers = [];
    this.streamHandlers = [];
    this.statusHandlers = [];
    this.subscribed = new Set();
  }

  get sessionId() {
    return this.sid;
  }

  get wsOpen() {
    return this.ws && this.ws.readyState === WebSocket.OPEN;
  }

  onEvent(fn) {
    this.eventHandlers.push(fn);
  }

  onStream(fn) {
    this.streamHandlers.push(fn);
  }

  onStatus(fn) {
    this.statusHandlers.push(fn);
  }

  connect() {
    return new Promise((resolve, reject) => {
      const proto = location.protocol === 'https:' ? 'wss' : 'ws';
      const ws = new WebSocket(`${proto}://${location.host}/ws?sid=${this.sid}`);
      ws.binaryType = 'arraybuffer';
      let settled = false;
      ws.onopen = () => {
        settled = true;
        this.statusHandlers.forEach((f) => f('open'));
        resolve();
      };
      ws.onerror = () => {
        if (!settled) {
          settled = true;
          reject(new ApiError(0, 'WS 连接失败'));
        }
      };
      ws.onclose = () => this.statusHandlers.forEach((f) => f('closed'));
      ws.onmessage = (ev) => {
        if (typeof ev.data === 'string') {
          let msg;
          try {
            msg = JSON.parse(ev.data);
          } catch {
            return;
          }
          if (msg.type === 'evt') this.eventHandlers.forEach((f) => f(msg.name, msg.payload));
          return;
        }
        const frame = parseFrame(ev.data);
        if (!frame) return;
        const items = [];
        if (frame.data) items.push(...decodeJsonLines(frame.data));
        const decoder = new TextDecoder();
        for (const rec of frame.records) {
          const text = decoder.decode(rec.payload);
          try {
            items.push(JSON.parse(text));
          } catch {
            items.push(...decodeJsonLines(rec.payload));
          }
        }
        this.streamHandlers.forEach((f) => f(frame, items));
      };
      this.ws = ws;
    });
  }

  close() {
    if (this.ws) this.ws.close();
    this.ws = null;
  }

  async call(method, path, body, timeoutMs = 20000) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    try {
      const res = await fetch(path, {
        method,
        headers: {
          'X-Session-Id': this.sid,
          ...(body !== undefined ? { 'Content-Type': 'application/json' } : {}),
        },
        body: body !== undefined ? JSON.stringify(body) : undefined,
        signal: controller.signal,
      });
      const text = await res.text();
      const data = text ? JSON.parse(text) : null;
      if (!res.ok) {
        const message = (data && (data.msg || (data.error && data.error.message))) || `HTTP ${res.status}`;
        throw new ApiError(res.status, message, data);
      }
      return data;
    } finally {
      clearTimeout(timer);
    }
  }

  async command(action, payload = {}) {
    const out = await this.call('POST', `/api/v1/commands/eph205/${action}`, payload);
    return out.result;
  }

  async getState() {
    const out = await this.call('GET', '/api/v1/state');
    const mod = (out.modules || []).find((m) => m.ns === 'eph205');
    return mod ? mod.state : null;
  }

  async getParams() {
    const out = await this.call('GET', '/api/v1/params/eph205');
    return out.values;
  }

  async setParams(values) {
    const out = await this.call('PATCH', '/api/v1/params/eph205', { values });
    return out.values;
  }

  async login(username, password) {
    return this.call('POST', '/api/v1/auth/login', { username, password });
  }

  async logout() {
    try {
      await this.call('POST', '/api/v1/auth/logout');
    } catch {
      /* 会话可能已经没了 */
    }
  }

  async me() {
    return this.call('GET', '/api/v1/auth/me');
  }

  async subscribe(streamId, windowBytes = 4 * 1024 * 1024) {
    const out = await this.call('POST', '/api/v1/subscriptions', {
      streams: [{ id: streamId, window: windowBytes }],
    });
    this.subscribed.add(streamId);
    return out;
  }

  async unsubscribe(streamId) {
    if (!this.subscribed.delete(streamId)) return;
    try {
      await this.call('DELETE', `/api/v1/subscriptions/${streamId}`);
    } catch {
      /* 会话换了就忽略 */
    }
  }

  credit(streamId, bytes) {
    if (!this.wsOpen) return;
    this.ws.send(JSON.stringify({ type: 'credit', streams: [{ id: streamId, bytes }] }));
  }

  async startJob(type, input) {
    const out = await this.call('POST', '/api/v1/jobs', { ns: 'eph205', type, input });
    return out.jobId;
  }

  async jobStatus(id) {
    return this.call('GET', `/api/v1/jobs/${id}`);
  }

  async cancelJob(id) {
    return this.call('DELETE', `/api/v1/jobs/${id}`);
  }

  async logs() {
    return this.call('GET', '/api/v1/logs');
  }
}

// ---------------------------------------------------------------- v1 帧

const BLOCK_DATA = 0x01;
const BLOCK_META = 0x02;
const BLOCK_RECORDS = 0x03;

function readU64(view, offset) {
  const lo = view.getUint32(offset, true);
  const hi = view.getUint32(offset + 4, true);
  return hi * 4294967296 + lo;
}

/** 24 字节头：A5 ver opcode flags streamId(u32) seq(u32) ts(u64) bodyLen(u32) */
export function parseFrame(buffer) {
  if (buffer.byteLength < 24) return null;
  const view = new DataView(buffer);
  if (view.getUint8(0) !== 0xa5) return null;
  const opcode = view.getUint8(2);
  const streamId = view.getUint32(4, true);
  const seq = view.getUint32(8, true);
  const timestampNs = readU64(view, 12);
  const bodyLen = view.getUint32(20, true);
  const end = Math.min(24 + bodyLen, buffer.byteLength);
  const frame = { opcode, streamId, seq, timestampNs, records: [] };
  const decoder = new TextDecoder();
  let offset = 24;
  while (offset + 5 <= end) {
    const type = view.getUint8(offset);
    const len = view.getUint32(offset + 1, true);
    const start = offset + 5;
    const stop = start + len;
    if (stop > end) break;
    const payload = new Uint8Array(buffer, start, len);
    if (type === BLOCK_META) frame.meta = decoder.decode(payload);
    else if (type === BLOCK_DATA) frame.data = payload;
    else if (type === BLOCK_RECORDS) {
      let cursor = 0;
      while (cursor + 12 <= payload.byteLength) {
        const recView = new DataView(
          payload.buffer,
          payload.byteOffset + cursor,
          payload.byteLength - cursor,
        );
        const tsNs = readU64(recView, 0);
        const recLen = recView.getUint32(8, true);
        if (cursor + 12 + recLen > payload.byteLength) break;
        frame.records.push({ tsNs, payload: payload.slice(cursor + 12, cursor + 12 + recLen) });
        cursor += 12 + recLen;
      }
    }
    offset = stop;
  }
  return frame;
}

export function decodeJsonLines(bytes) {
  const text = new TextDecoder().decode(bytes);
  const out = [];
  for (const line of text.split('\n')) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    try {
      out.push(JSON.parse(trimmed));
    } catch {
      /* 半行数据（跨帧）丢掉 */
    }
  }
  return out;
}

export const api = new Client();
