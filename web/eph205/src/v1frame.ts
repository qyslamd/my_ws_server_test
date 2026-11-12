// v1 二进制帧解析（与 apps/v1/base/frame.h 对应）
//
// 24 字节小端头：
//   magic(u8=0xA5) version(u8) opcode(u8) flags(u8)
//   streamId(u32) seq(u32) ts(u64) bodyLen(u32)
// body 是若干块：[type(u8)][len(u32)][payload]
// records 块内是重复的 [ts(u64)][len(u32)][payload]

export const OP_SNAPSHOT = 0x01
export const OP_DELTA = 0x02
export const OP_EVENT = 0x03
export const OP_ERROR = 0x04
export const OP_HEARTBEAT = 0x05

const BLOCK_DATA = 0x01
const BLOCK_META = 0x02
const BLOCK_RECORDS = 0x03
const BLOCK_AUX = 0x04

export interface FrameRecord {
  tsNs: number
  payload: Uint8Array
}

export interface V1Frame {
  opcode: number
  streamId: number
  seq: number
  timestampNs: number
  meta?: string
  data?: Uint8Array
  aux?: Uint8Array
  records: FrameRecord[]
}

function readU64(view: DataView, offset: number): number {
  const lo = view.getUint32(offset, true)
  const hi = view.getUint32(offset + 4, true)
  return hi * 4294967296 + lo
}

export function parseV1Frame(buffer: ArrayBuffer): V1Frame | null {
  if (buffer.byteLength < 24) {
    return null
  }
  const view = new DataView(buffer)
  if (view.getUint8(0) !== 0xa5) {
    return null
  }
  const opcode = view.getUint8(2)
  const streamId = view.getUint32(4, true)
  const seq = view.getUint32(8, true)
  const timestampNs = readU64(view, 12)
  const bodyLen = view.getUint32(20, true)
  const end = Math.min(24 + bodyLen, buffer.byteLength)

  const frame: V1Frame = { opcode, streamId, seq, timestampNs, records: [] }
  const decoder = new TextDecoder()
  let offset = 24
  while (offset + 5 <= end) {
    const type = view.getUint8(offset)
    const len = view.getUint32(offset + 1, true)
    const start = offset + 5
    const stop = start + len
    if (stop > end) {
      break
    }
    const payload = new Uint8Array(buffer, start, len)
    if (type === BLOCK_META) {
      frame.meta = decoder.decode(payload)
    } else if (type === BLOCK_DATA) {
      frame.data = payload
    } else if (type === BLOCK_AUX) {
      frame.aux = payload
    } else if (type === BLOCK_RECORDS) {
      let cursor = 0
      while (cursor + 12 <= payload.byteLength) {
        const recView = new DataView(
          payload.buffer,
          payload.byteOffset + cursor,
          payload.byteLength - cursor,
        )
        const tsNs = readU64(recView, 0)
        const recLen = recView.getUint32(8, true)
        if (cursor + 12 + recLen > payload.byteLength) {
          break
        }
        frame.records.push({
          tsNs,
          payload: payload.slice(cursor + 12, cursor + 12 + recLen),
        })
        cursor += 12 + recLen
      }
    }
    offset = stop
  }
  return frame
}

// 业务模块把 records 块当作 JSONL 用（一行一个 JSON），
// 这里按行切；如果服务端改成标准分条记录，走 frame.records。
export function decodeJsonLines(bytes: Uint8Array): any[] {
  const text = new TextDecoder().decode(bytes)
  const out: any[] = []
  for (const line of text.split('\n')) {
    const trimmed = line.trim()
    if (!trimmed) {
      continue
    }
    try {
      out.push(JSON.parse(trimmed))
    } catch {
      // 半行数据（跨帧）直接丢弃
    }
  }
  return out
}
