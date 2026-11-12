// 两次测量对比：按 (P,N) 对齐，逐点看阻值变化与判定变化。
// 数据来源是两条 history_get 的完整记录，服务端不用参与比对。

import { useMemo } from 'react'
import { Alert, Drawer, Table, Tag, Typography } from 'antd'
import { RESULT_TEXT } from '../model'

const { Text } = Typography

const RESULT_COLOR: Record<string, string> = {
  pass: 'green',
  fail: 'red',
  measure_failed: 'orange',
  not_measured: 'default',
}

interface Props {
  open: boolean
  onClose: () => void
  base: any | null
  other: any | null
}

interface CompareRow {
  key: string
  p: number
  n: number
  baseValue: number | null
  otherValue: number | null
  baseResult: string
  otherResult: string
  delta: number | null
  resultChanged: boolean
  onlyBase: boolean
  onlyOther: boolean
}

function timeText(ms: number) {
  if (!ms) return '-'
  const d = new Date(ms)
  const pad = (v: number) => String(v).padStart(2, '0')
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(
    d.getHours(),
  )}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}

function indexItems(record: any): Map<string, any> {
  const map = new Map<string, any>()
  for (const item of record?.items ?? []) {
    map.set(`${item.p}-${item.n}`, item)
  }
  return map
}

function num(value: any): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null
}

export default function CompareDrawer({ open, onClose, base, other }: Props) {
  const rows = useMemo<CompareRow[]>(() => {
    if (!base || !other) {
      return []
    }
    const a = indexItems(base)
    const b = indexItems(other)
    const keys = new Set<string>([...a.keys(), ...b.keys()])
    const out: CompareRow[] = []
    for (const key of keys) {
      const left = a.get(key)
      const right = b.get(key)
      const sample = left ?? right
      const baseValue = num(left?.value)
      const otherValue = num(right?.value)
      out.push({
        key,
        p: sample?.p ?? 0,
        n: sample?.n ?? 0,
        baseValue,
        otherValue,
        baseResult: left?.result ?? '',
        otherResult: right?.result ?? '',
        delta:
          baseValue !== null && otherValue !== null
            ? otherValue - baseValue
            : null,
        resultChanged: !!left && !!right && left.result !== right.result,
        onlyBase: !!left && !right,
        onlyOther: !left && !!right,
      })
    }
    out.sort((x, y) => (x.p - y.p) || (x.n - y.n))
    return out
  }, [base, other])

  const stats = useMemo(() => {
    let common = 0
    let onlyBase = 0
    let onlyOther = 0
    let changed = 0
    let maxDelta = 0
    for (const row of rows) {
      if (row.onlyBase) onlyBase += 1
      else if (row.onlyOther) onlyOther += 1
      else common += 1
      if (row.resultChanged) changed += 1
      if (row.delta !== null) {
        maxDelta = Math.max(maxDelta, Math.abs(row.delta))
      }
    }
    return { common, onlyBase, onlyOther, changed, maxDelta }
  }, [rows])

  const columns = [
    { title: 'P', dataIndex: 'p', width: 60 },
    { title: 'N', dataIndex: 'n', width: 60 },
    {
      title: '基准值 Ω',
      dataIndex: 'baseValue',
      width: 100,
      render: (v: number | null) => (v === null ? '-' : v.toFixed(3)),
    },
    {
      title: '对比值 Ω',
      dataIndex: 'otherValue',
      width: 100,
      render: (v: number | null) => (v === null ? '-' : v.toFixed(3)),
    },
    {
      title: '差值 Ω',
      dataIndex: 'delta',
      width: 100,
      render: (v: number | null) =>
        v === null ? (
          '-'
        ) : (
          <span style={{ color: Math.abs(v) > 0.05 ? '#b45309' : undefined }}>
            {v > 0 ? '+' : ''}
            {v.toFixed(3)}
          </span>
        ),
    },
    {
      title: '基准判定',
      dataIndex: 'baseResult',
      width: 100,
      render: (v: string) =>
        v ? <Tag color={RESULT_COLOR[v]}>{RESULT_TEXT[v] ?? v}</Tag> : '-',
    },
    {
      title: '对比判定',
      dataIndex: 'otherResult',
      width: 100,
      render: (v: string) =>
        v ? <Tag color={RESULT_COLOR[v]}>{RESULT_TEXT[v] ?? v}</Tag> : '-',
    },
    {
      title: '变化',
      width: 110,
      render: (_: any, row: CompareRow) => {
        if (row.onlyBase) return <Tag>仅基准有</Tag>
        if (row.onlyOther) return <Tag color="blue">仅对比有</Tag>
        if (row.resultChanged) return <Tag color="red">判定变化</Tag>
        return <Text type="secondary">不变</Text>
      },
    },
  ]

  const meta = (record: any, title: string, color: string) => (
    <div style={{ flex: 1, minWidth: 260 }}>
      <Tag color={color}>{title}</Tag>
      <div className="mono" style={{ lineHeight: 1.8 }}>
        记录 #{record.id} · {timeText(record.createdAt)}
        <br />
        型号 {record.product?.model || '-'} / 编号 {record.product?.code || '-'}
        <br />
        操作员 {record.operator || '-'} · 来源{' '}
        {record.deviceSource === 'simulation' ? '仿真' : '真机'}
        <br />
        通过率 {Number(record.summary?.passRate ?? 0).toFixed(1)}%（{record.summary?.passed ?? 0}/
        {record.summary?.total ?? 0}）
      </div>
    </div>
  )

  return (
    <Drawer width={980} title="测量对比" open={open} onClose={onClose}>
      {base && other ? (
        <>
          <div style={{ display: 'flex', gap: 24, marginBottom: 12 }}>
            {meta(base, '基准（较早）', 'default')}
            {meta(other, '对比（较晚）', 'blue')}
          </div>
          <div className="toolbar" style={{ marginBottom: 10 }}>
            <Tag>共同点 {stats.common}</Tag>
            <Tag color={stats.onlyBase ? 'orange' : undefined}>
              仅基准 {stats.onlyBase}
            </Tag>
            <Tag color={stats.onlyOther ? 'blue' : undefined}>
              仅对比 {stats.onlyOther}
            </Tag>
            <Tag color={stats.changed ? 'red' : undefined}>
              判定变化 {stats.changed}
            </Tag>
            <Tag>最大差值 {stats.maxDelta.toFixed(3)} Ω</Tag>
          </div>
          {stats.changed > 0 && (
            <Alert
              type="warning"
              showIcon
              style={{ marginBottom: 10 }}
              message={`有 ${stats.changed} 个点的判定和基准不同，已在表格里标红。`}
            />
          )}
          <Table
            size="small"
            rowKey="key"
            columns={columns as any}
            dataSource={rows}
            pagination={{ pageSize: 100, size: 'small' }}
            scroll={{ y: 520 }}
            rowClassName={(row: CompareRow) =>
              row.resultChanged
                ? 'row-changed'
                : row.onlyBase || row.onlyOther
                  ? 'row-partial'
                  : ''
            }
          />
          <div style={{ marginTop: 8, color: '#667085', fontSize: 12 }}>
            对齐键是 (P,N)；差值 = 对比值 − 基准值；超过 ±0.05 Ω 的差值标橙。
          </div>
        </>
      ) : (
        <Text type="secondary">请选择两条记录</Text>
      )}
    </Drawer>
  )
}
