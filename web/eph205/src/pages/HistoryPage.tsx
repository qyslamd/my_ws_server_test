import { useEffect, useState } from 'react'
import {
  App,
  Button,
  Drawer,
  Input,
  InputNumber,
  Popconfirm,
  Select,
  Space,
  Table,
  Tag,
} from 'antd'
import { api } from '../api'
import {
  RESULT_TEXT,
  REASON_TEXT,
  downloadText,
  type ResultRow,
} from '../model'

interface Props {
  wsOpen: boolean
  role: string
}

const RESULT_COLOR: Record<string, string> = {
  pass: 'green',
  fail: 'red',
  measure_failed: 'orange',
  not_measured: 'default',
}
const STATUS_OPTIONS = [
  { value: '', label: '全部' },
  { value: 'completed', label: '已完成' },
  { value: 'cancelled', label: '已取消' },
  { value: 'aborted', label: '中断' },
]

function timeText(ms: number) {
  if (!ms) return '-'
  const d = new Date(ms)
  const pad = (v: number) => String(v).padStart(2, '0')
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(
    d.getHours(),
  )}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}

export default function HistoryPage({ wsOpen, role }: Props) {
  const isAdmin = role === 'admin'
  const { message } = App.useApp()
  const [rows, setRows] = useState<any[]>([])
  const [total, setTotal] = useState(0)
  const [page, setPage] = useState(1)
  const [pageSize, setPageSize] = useState(20)
  const [model, setModel] = useState('')
  const [operator, setOperator] = useState('')
  const [status, setStatus] = useState('')
  const [detail, setDetail] = useState<any>(null)
  const [loading, setLoading] = useState(false)

  const load = async (pageIndex = page, size = pageSize) => {
    if (!wsOpen) return
    setLoading(true)
    try {
      const out = await api.command('history_list', {
        model,
        operator,
        status,
        offset: (pageIndex - 1) * size,
        limit: size,
      })
      setRows(out.rows ?? [])
      setTotal(out.total ?? 0)
    } catch (err: any) {
      message.error('查询失败: ' + err.message)
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    if (wsOpen) {
      void load(1, pageSize)
      setPage(1)
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [wsOpen])

  const openDetail = async (id: number) => {
    try {
      setDetail(await api.command('history_get', { id }))
    } catch (err: any) {
      message.error('读取失败: ' + err.message)
    }
  }

  const exportRecord = async (id: number) => {
    try {
      const out = await api.command('history_export', { id })
      downloadText(`measurement-${id}.csv`, out.csv)
    } catch (err: any) {
      message.error('导出失败: ' + err.message)
    }
  }

  const remove = async (id: number) => {
    try {
      await api.command('history_delete', { id })
      message.success('已删除 #' + id)
      void load()
    } catch (err: any) {
      message.error('删除失败: ' + err.message)
    }
  }

  const columns = [
    { title: 'ID', dataIndex: 'id', width: 60 },
    {
      title: '时间',
      dataIndex: 'createdAt',
      width: 160,
      render: (v: number) => timeText(v),
    },
    {
      title: '型号',
      width: 160,
      render: (_: any, row: any) => row.product?.model || '-',
    },
    {
      title: '编号',
      width: 120,
      render: (_: any, row: any) => row.product?.code || '-',
    },
    {
      title: '状态',
      dataIndex: 'status',
      width: 90,
      render: (v: string) => (
        <Tag color={v === 'completed' ? 'green' : 'orange'}>{v}</Tag>
      ),
    },
    {
      title: '总数/通过/失败',
      width: 150,
      render: (_: any, row: any) => {
        const s = row.summary ?? {}
        return `${s.total ?? 0} / ${s.passed ?? 0} / ${s.failed ?? 0}`
      },
    },
    {
      title: '通过率',
      width: 90,
      render: (_: any, row: any) =>
        `${Number(row.summary?.passRate ?? 0).toFixed(1)}%`,
    },
    { title: '操作员', dataIndex: 'operator', width: 100 },
    {
      title: '来源',
      dataIndex: 'deviceSource',
      width: 90,
      render: (v: string) => (v === 'simulation' ? '仿真' : '真机'),
    },
    {
      title: '操作',
      render: (_: any, row: any) => (
        <Space size={4}>
          <Button size="small" type="link" onClick={() => void openDetail(row.id)}>
            详情
          </Button>
          <Button size="small" type="link" onClick={() => void exportRecord(row.id)}>
            导出
          </Button>
          {isAdmin && (
            <Popconfirm title={`删除记录 #${row.id}？`} onConfirm={() => void remove(row.id)}>
              <Button size="small" type="link" danger>
                删除
              </Button>
            </Popconfirm>
          )}
        </Space>
      ),
    },
  ]

  const itemColumns = [
    { title: '序号', dataIndex: 'seq', width: 60 },
    { title: 'P', dataIndex: 'p', width: 60 },
    { title: 'N', dataIndex: 'n', width: 60 },
    {
      title: '线制',
      dataIndex: 'wire',
      width: 60,
      render: (v: number) => (v === 0 ? '二线' : '四线'),
    },
    { title: '量程', dataIndex: 'range', width: 60, render: (v: number) => `R${v}` },
    {
      title: '测量值 Ω',
      dataIndex: 'value',
      width: 110,
      render: (v: number | null) => (v === null || v === undefined ? '-' : Number(v).toFixed(3)),
    },
    {
      title: '判定',
      dataIndex: 'result',
      width: 90,
      render: (v: string) => <Tag color={RESULT_COLOR[v]}>{RESULT_TEXT[v] ?? v}</Tag>,
    },
    {
      title: '原因',
      dataIndex: 'reason',
      render: (v: string) => REASON_TEXT[v] ?? v ?? '',
    },
  ]

  return (
    <div>
      <div className="panel">
        <div className="panel-title">历史记录</div>
        <div className="toolbar" style={{ marginBottom: 8 }}>
          <span>型号</span>
          <Input
            style={{ width: 150 }}
            value={model}
            onChange={(e) => setModel(e.target.value)}
            allowClear
          />
          <span>操作员</span>
          <Input
            style={{ width: 120 }}
            value={operator}
            onChange={(e) => setOperator(e.target.value)}
            allowClear
          />
          <span>状态</span>
          <Select style={{ width: 110 }} options={STATUS_OPTIONS} value={status} onChange={setStatus} />
          <Button
            type="primary"
            onClick={() => {
              setPage(1)
              void load(1, pageSize)
            }}
          >
            查询
          </Button>
          <span className="spacer" style={{ flex: 1 }} />
          <span>共 {total} 条</span>
        </div>
        <Table
          size="small"
          rowKey="id"
          loading={loading}
          columns={columns as any}
          dataSource={rows}
          pagination={{
            current: page,
            pageSize,
            total,
            size: 'small',
            showSizeChanger: true,
            onChange: (nextPage, nextSize) => {
              setPage(nextPage)
              setPageSize(nextSize)
              void load(nextPage, nextSize)
            },
          }}
          scroll={{ y: 420 }}
        />
      </div>

      <Drawer
        width={860}
        title={
          detail
            ? `记录 #${detail.id} · ${detail.product?.model || '未知型号'}`
            : '详情'
        }
        open={!!detail}
        onClose={() => setDetail(null)}
        extra={
          detail && (
            <Button onClick={() => void exportRecord(detail.id)}>导出 CSV</Button>
          )
        }
      >
        {detail && (
          <>
            <div className="toolbar" style={{ marginBottom: 10 }}>
              <Tag color="green">总数 {detail.summary?.total ?? 0}</Tag>
              <Tag color="blue">通过 {detail.summary?.passed ?? 0}</Tag>
              <Tag color="red">失败 {detail.summary?.failed ?? 0}</Tag>
              <Tag color="orange">测量失败 {detail.summary?.measureFailed ?? 0}</Tag>
              <Tag>通过率 {Number(detail.summary?.passRate ?? 0).toFixed(1)}%</Tag>
            </div>
            <Table
              size="small"
              rowKey="seq"
              columns={itemColumns as any}
              dataSource={(detail.items ?? []) as ResultRow[]}
              pagination={{ pageSize: 100, size: 'small' }}
              scroll={{ y: 480 }}
            />
          </>
        )}
      </Drawer>
    </div>
  )
}
