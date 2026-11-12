// 测试项模型与导入导出。浏览器端解析，服务端只收结构化数组。

export interface TestItem {
  key: string
  seq: number
  p: number
  n: number
  wire: 0 | 1 // 0=二线 1=四线
  range: number
  minOhms: number | null
  maxOhms: number | null
  measure: boolean
}

export interface ResultRow {
  seq: number
  p: number
  n: number
  wire: number
  range: number
  minOhms: number | null
  maxOhms: number | null
  measure: boolean
  value: number | null
  result: 'pass' | 'fail' | 'measure_failed' | 'not_measured'
  reason: string
}

export const RESULT_TEXT: Record<string, string> = {
  pass: '通过',
  fail: '失败',
  measure_failed: '测量失败',
  not_measured: '未测',
}

export const REASON_TEXT: Record<string, string> = {
  over_range: '超量程',
  frame_failed: '整帧失败',
  setup_failed: '组参数设置失败',
  unchecked: '未勾选',
  cancelled: '已取消',
  aborted: '中断',
  disconnected: '连接断开',
}

let counter = 0
export function nextKey(): string {
  counter += 1
  return `k${counter}`
}

export function makeItem(p = 1, n = 2): TestItem {
  return {
    key: nextKey(),
    seq: 0,
    p,
    n,
    wire: 0,
    range: 0,
    minOhms: null,
    maxOhms: null,
    measure: true,
  }
}

function pick(row: Record<string, string>, ...names: string[]): string {
  for (const name of names) {
    const key = Object.keys(row).find(
      (k) => k.trim().toLowerCase() === name.toLowerCase(),
    )
    if (key !== undefined && row[key] !== '') {
      return row[key]
    }
  }
  return ''
}

function toNumber(text: string): number | null {
  if (text === undefined || text === null || text.trim() === '') {
    return null
  }
  const value = Number(text)
  return Number.isFinite(value) ? value : null
}

function splitCsvLine(line: string): string[] {
  const out: string[] = []
  let cell = ''
  let quoted = false
  for (let i = 0; i < line.length; i += 1) {
    const ch = line[i]
    if (quoted) {
      if (ch === '"' && line[i + 1] === '"') {
        cell += '"'
        i += 1
      } else if (ch === '"') {
        quoted = false
      } else {
        cell += ch
      }
    } else if (ch === '"') {
      quoted = true
    } else if (ch === ',') {
      out.push(cell)
      cell = ''
    } else {
      cell += ch
    }
  }
  out.push(cell)
  return out
}

export interface ImportResult {
  items: TestItem[]
  skipped: number
  product?: { model?: string; code?: string }
}

// 支持：
//   - 新表头 CSV：序号,P,N,线制,量程,下限,上限,测量
//   - 无表头 CSV：P,N[,下限,上限]
//   - JSON：数组，或 { items: [...], product: {...} }
export function parseImport(text: string, fileName: string): ImportResult {
  const trimmed = text.trim()
  if (fileName.toLowerCase().endsWith('.json') || trimmed.startsWith('[') ||
      trimmed.startsWith('{')) {
    return parseJson(trimmed)
  }
  return parseCsv(trimmed)
}

function normalizeItem(raw: any, index: number): TestItem {
  const item = makeItem()
  item.seq = Number(raw.seq ?? index + 1)
  item.p = Number(raw.p ?? raw.P ?? 0)
  item.n = Number(raw.n ?? raw.N ?? 0)
  const wire = raw.wire ?? raw['线制'] ?? 0
  item.wire = Number(wire) === 1 || wire === '四线' ? 1 : 0
  item.range = Number(raw.range ?? raw['量程'] ?? 0)
  const min = raw.minOhms ?? raw.min ?? raw['下限']
  const max = raw.maxOhms ?? raw.max ?? raw['上限']
  item.minOhms = toNumber(min === undefined ? '' : String(min))
  item.maxOhms = toNumber(max === undefined ? '' : String(max))
  const measure = raw.measure ?? raw['测量'] ?? true
  item.measure = !(measure === false || measure === 0 || measure === '0' ||
    measure === '否' || measure === 'N' || measure === 'n')
  return item
}

function parseJson(text: string): ImportResult {
  const data = JSON.parse(text)
  const list: any[] = Array.isArray(data) ? data : data.items ?? []
  const items = list.map((raw, i) => normalizeItem(raw, i))
  return {
    items,
    skipped: 0,
    product: data && !Array.isArray(data) ? data.product : undefined,
  }
}

function parseCsv(text: string): ImportResult {
  const lines = text.split(/\r?\n/).filter((l) => l.trim() !== '' &&
    !l.trim().startsWith('#') && !l.trim().startsWith(';'))
  if (lines.length === 0) {
    return { items: [], skipped: 0 }
  }
  const first = splitCsvLine(lines[0]).map((c) => c.trim())
  const hasHeader = first.some((c) =>
    /^(p|n|序号|线制|量程|下限|上限|测量)$/i.test(c),
  )
  const header = hasHeader ? first : ['P', 'N', '下限', '上限']
  const body = hasHeader ? lines.slice(1) : lines

  const items: TestItem[] = []
  let skipped = 0
  body.forEach((line, index) => {
    const cells = splitCsvLine(line)
    const row: Record<string, string> = {}
    header.forEach((name, i) => {
      row[name] = (cells[i] ?? '').trim()
    })
    const p = toNumber(pick(row, 'P', 'p'))
    const n = toNumber(pick(row, 'N', 'n'))
    if (p === null || n === null || p <= 0 || n <= 0) {
      skipped += 1
      return
    }
    const item = makeItem()
    item.seq = toNumber(pick(row, '序号', 'seq')) ?? index + 1
    item.p = p
    item.n = n
    const wire = pick(row, '线制', 'wire')
    item.wire = wire === '四线' || wire === '1' || wire.toLowerCase() === '4w'
      ? 1
      : 0
    item.range = toNumber(pick(row, '量程', 'range')) ?? 0
    item.minOhms = toNumber(pick(row, '下限', 'minOhms', 'min'))
    item.maxOhms = toNumber(pick(row, '上限', 'maxOhms', 'max'))
    const measure = pick(row, '测量', 'measure')
    if (measure !== '') {
      item.measure = !(measure === '0' || measure === '否' ||
        measure.toLowerCase() === 'n' || measure.toLowerCase() === 'false')
    }
    items.push(item)
  })
  return { items, skipped }
}

export function itemsToJson(items: TestItem[]) {
  return items.map((item, index) => ({
    seq: item.seq || index + 1,
    p: item.p,
    n: item.n,
    wire: item.wire,
    range: item.range,
    minOhms: item.minOhms,
    maxOhms: item.maxOhms,
    measure: item.measure,
  }))
}

export function downloadText(fileName: string, text: string) {
  const blob = new Blob([text], { type: 'text/csv;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = fileName
  link.click()
  URL.revokeObjectURL(url)
}
