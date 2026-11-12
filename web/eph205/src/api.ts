// v1 底座客户端：REST 控制/查询 + WS 事件。
// 底座是 1:1 会话，WS 必须先建立，REST 才带得上 X-Session-Id。

export type EventHandler = (name: string, payload: any) => void

export type Port = string

export interface DeviceInfo {
  firmware: string
  firmwareVersion: number
  pCount: number
  nCount: number
  muxBoards: number
  rangeCount: number
  mode: number
  source: number
  deviceId: number
}

export interface JobProgress {
  jobId: string
  state: 'running' | 'completed' | 'cancelled' | 'aborted'
  done: number
  total: number
  passed?: number
  failed?: number
  measureFailed?: number
  recordId?: number
  summary?: Record<string, any>
}

export class ApiError extends Error {
  status: number
  detail: any
  constructor(status: number, message: string, detail?: any) {
    super(message)
    this.status = status
    this.detail = detail
  }
}

export class Client {
  readonly sid: string
  private ws: WebSocket | null = null
  private handlers: EventHandler[] = []
  private statusHandlers: ((status: string) => void)[] = []

  constructor() {
    this.sid = 'sid-' + Math.random().toString(36).slice(2, 10)
  }

  get sessionId() {
    return this.sid
  }

  get wsOpen() {
    return this.ws?.readyState === WebSocket.OPEN
  }

  onEvent(handler: EventHandler) {
    this.handlers.push(handler)
    return () => {
      this.handlers = this.handlers.filter((h) => h !== handler)
    }
  }

  onStatus(handler: (status: string) => void) {
    this.statusHandlers.push(handler)
    return () => {
      this.statusHandlers = this.statusHandlers.filter((h) => h !== handler)
    }
  }

  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      const proto = location.protocol === 'https:' ? 'wss' : 'ws'
      const ws = new WebSocket(`${proto}://${location.host}/ws?sid=${this.sid}`)
      ws.binaryType = 'arraybuffer'
      let settled = false
      ws.onopen = () => {
        settled = true
        this.statusHandlers.forEach((h) => h('open'))
        resolve()
      }
      ws.onerror = () => {
        if (!settled) {
          settled = true
          reject(new ApiError(0, 'WS 连接失败'))
        }
      }
      ws.onclose = () => {
        this.statusHandlers.forEach((h) => h('closed'))
      }
      ws.onmessage = (ev) => {
        if (typeof ev.data !== 'string') {
          return // 二进制是数据流帧，页面暂不消费
        }
        try {
          const msg = JSON.parse(ev.data)
          if (msg.type === 'evt') {
            this.handlers.forEach((h) => h(msg.name, msg.payload))
          }
        } catch {
          // 忽略非 JSON 文本
        }
      }
      this.ws = ws
    })
  }

  close() {
    this.ws?.close()
    this.ws = null
  }

  private async call<T = any>(
    method: string,
    path: string,
    body?: any,
    timeoutMs = 20000,
  ): Promise<T> {
    const controller = new AbortController()
    const timer = setTimeout(() => controller.abort(), timeoutMs)
    try {
      const res = await fetch(path, {
        method,
        headers: {
          'X-Session-Id': this.sid,
          ...(body !== undefined ? { 'Content-Type': 'application/json' } : {}),
        },
        body: body !== undefined ? JSON.stringify(body) : undefined,
        signal: controller.signal,
      })
      const text = await res.text()
      const data = text ? JSON.parse(text) : null
      if (!res.ok) {
        const err = data?.error
        throw new ApiError(
          res.status,
          err?.message || `HTTP ${res.status}`,
          data,
        )
      }
      return data as T
    } finally {
      clearTimeout(timer)
    }
  }

  async command<T = any>(action: string, payload: any = {}): Promise<T> {
    const out = await this.call<{ ok: boolean; result: T }>(
      'POST',
      `/api/v1/commands/eph205/${action}`,
      payload,
    )
    return out.result
  }

  async getModules(): Promise<any[]> {
    const out = await this.call<{ modules: any[] }>('GET', '/api/v1/modules')
    return out.modules
  }

  async getState(): Promise<any> {
    const out = await this.call<{ modules: any[] }>('GET', '/api/v1/state')
    return out.modules.find((m) => m.ns === 'eph205')?.state
  }

  async getParams(): Promise<Record<string, any>> {
    const out = await this.call<{ values: Record<string, any> }>(
      'GET',
      '/api/v1/params/eph205',
    )
    return out.values
  }

  async setParams(values: Record<string, any>): Promise<Record<string, any>> {
    const out = await this.call<{ values: Record<string, any> }>(
      'PATCH',
      '/api/v1/params/eph205',
      { values },
    )
    return out.values
  }

  async startJob(type: string, input: any): Promise<string> {
    const out = await this.call<{ jobId: string }>('POST', '/api/v1/jobs', {
      ns: 'eph205',
      type,
      input,
    })
    return out.jobId
  }

  async jobStatus(jobId: string): Promise<any> {
    return this.call('GET', `/api/v1/jobs/${jobId}`)
  }

  async cancelJob(jobId: string): Promise<any> {
    return this.call('DELETE', `/api/v1/jobs/${jobId}`)
  }

  async logs(): Promise<any> {
    return this.call('GET', '/api/v1/logs')
  }
}

export const api = new Client()
