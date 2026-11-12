import { useEffect, useMemo, useState } from 'react'
import {
  App,
  Button,
  Input,
  InputNumber,
  Popconfirm,
  Progress,
  Select,
  Space,
  Switch,
  Table,
  Tag,
  Upload,
} from 'antd'
import { DownloadOutlined, UploadOutlined } from '@ant-design/icons'
import { api, type JobProgress } from '../api'
import LiveReadings, { type LivePoint } from '../components/LiveReadings'
import {
  RESULT_TEXT,
  REASON_TEXT,
  downloadText,
  itemsToJson,
  makeItem,
  parseImport,
  type ResultRow,
  type TestItem,
} from '../model'

interface Props {
  state: any
  refresh: () => void
  progress: JobProgress | null
  progressLog: JobProgress[]
  wsOpen: boolean
  role: string
}

const WIRE_OPTIONS = [
  { value: 0, label: '二线' },
  { value: 1, label: '四线' },
]
const RANGE_OPTIONS = [0, 1, 2, 3, 4, 5].map((v) => ({
  value: v,
  label: `R${v}`,
}))
const RESULT_COLOR: Record<string, string> = {
  pass: 'green',
  fail: 'red',
  measure_failed: 'orange',
  not_measured: 'default',
}

export default function MeasurePage({
  state,
  refresh,
  progress,
  wsOpen,
  role,
}: Props) {
  const { message } = App.useApp()
  const isAdmin = role === 'admin'
  const [ports, setPorts] = useState<string[]>([])
  const [port, setPort] = useState<string>('')
  const [simPort, setSimPort] = useState<string>('')
  const [busy, setBusy] = useState(false)
  const [items, setItems] = useState<TestItem[]>([])
  const [model, setModel] = useState('')
  const [code, setCode] = useState('')
  const [operator, setOperator] = useState('tester')
  const [presetWire, setPresetWire] = useState(0)
  const [presetRange, setPresetRange] = useState(0)
  const [jobId, setJobId] = useState<string | null>(null)
  const [results, setResults] = useState<ResultRow[]>([])
  const [summary, setSummary] = useState<any>(null)
  const [recordId, setRecordId] = useState<number | null>(null)
  const [onlyFail, setOnlyFail] = useState(false)
  const [live, setLive] = useState<LivePoint[]>([])

  const connected = !!state?.connected
  const simRunning = !!state?.sim?.running
  const running = progress?.state === 'running'

  useEffect(() => {
    if (!wsOpen) {
      return
    }
    api
      .command('ports')
      .then((out: any) => {
        setPorts(out.ports ?? [])
        setPort(out.storedPort || out.ports?.[0] || '')
        setSimPort(out.simPort || out.ports?.[1] || '')
      })
      .catch(() => undefined)
  }, [wsOpen])

  // 订阅读数流：推进进度事件之外，测量结果也会逐条推下来
  useEffect(() => {
    if (!wsOpen) {
      return
    }
    let alive = true
    api.subscribe(1).catch(() => undefined)
    const off = api.onStream((frame, items) => {
      if (frame.streamId !== 1 || items.length === 0) {
        return
      }
      const bytes =
        frame.records.reduce((n, r) => n + r.payload.length, 0) +
        (frame.data?.length ?? 0)
      api.credit(1, bytes)
      if (!alive) {
        return
      }
      setLive((prev) => [...prev, ...(items as LivePoint[])].slice(-400))
    })
    return () => {
      alive = false
      off()
      void api.unsubscribe(1)
    }
  }, [wsOpen])

  useEffect(() => {
    if (!progress || !wsOpen) {
      return
    }
    if (progress.state !== 'running' && progress.recordId) {
      void loadRecord(progress.recordId)
    }
  }, [progress, wsOpen])

  async function loadRecord(id: number) {
    try {
      const record = await api.command('history_get', { id })
      setResults(record.items ?? [])
      setSummary(record.summary ?? null)
      setRecordId(id)
      setModel(record.product?.model ?? '')
      setCode(record.product?.code ?? '')
    } catch (err: any) {
      message.error('读取结果失败: ' + err.message)
    }
  }

  const update = (key: string, patch: Partial<TestItem>) => {
    setItems((prev) => prev.map((i) => (i.key === key ? { ...i, ...patch } : i)))
  }

  const addRow = () => {
    setItems((prev) => {
      const last = prev[prev.length - 1]
      const item = makeItem(last ? last.p + 1 : 1, last ? last.n + 1 : 2)
      item.wire = (last?.wire ?? presetWire) as 0 | 1
      item.range = last?.range ?? presetRange
      return [...prev, item]
    })
  }

  const applyPreset = () => {
    setItems((prev) =>
      prev.map((i) => ({ ...i, wire: presetWire as 0 | 1, range: presetRange })),
    )
  }

  const onImport = async (file: File) => {
    try {
      const text = await file.text()
      const parsed = parseImport(text, file.name)
      if (parsed.items.length === 0) {
        message.error('没有解析到有效测试项（需要 P/N 至少两列）')
        return false
      }
      setItems(parsed.items)
      if (parsed.product?.model) setModel(parsed.product.model)
      if (parsed.product?.code) setCode(parsed.product.code)
      message.success(
        `导入 ${parsed.items.length} 项` +
          (parsed.skipped ? `，跳过 ${parsed.skipped} 行` : ''),
      )
    } catch (err: any) {
      message.error('导入失败: ' + err.message)
    }
    return false
  }

  const connect = async () => {
    if (!port) {
      message.warning('请选择串口')
      return
    }
    setBusy(true)
    try {
      await api.setParams({ port, simPort })
      await api.command('connect', { port })
      message.success(`已连接 ${port}`)
      refresh()
    } catch (err: any) {
      message.error('连接失败: ' + err.message)
    } finally {
      setBusy(false)
    }
  }

  const disconnect = async () => {
    setBusy(true)
    try {
      await api.command('disconnect')
      message.info('已断开')
      refresh()
    } catch (err: any) {
      message.error('断开失败: ' + err.message)
    } finally {
      setBusy(false)
    }
  }

  const toggleSim = async () => {
    setBusy(true)
    try {
      if (simRunning) {
        await api.command('sim_stop')
        message.info('仿真器已停止')
      } else {
        await api.setParams({ simPort })
        await api.command('sim_start', { port: simPort })
        message.success(`仿真器已启动 ${simPort}`)
      }
      refresh()
    } catch (err: any) {
      message.error('仿真器操作失败: ' + err.message)
    } finally {
      setBusy(false)
    }
  }

  const start = async () => {
    if (items.length === 0) {
      message.warning('请先导入或添加测试项')
      return
    }
    if (!connected) {
      message.warning('请先连接设备')
      return
    }
    setBusy(true)
    setResults([])
    setSummary(null)
    setRecordId(null)
    setLive([])
    try {
      const id = await api.startJob('measure_run', {
        product: { model, code },
        operator,
        items: itemsToJson(items),
        deviceSource: simRunning ? 'simulation' : 'serial',
      })
      setJobId(id)
      message.success(`测量已开始 ${id}`)
    } catch (err: any) {
      message.error('启动失败: ' + err.message)
    } finally {
      setBusy(false)
    }
  }

  const stop = async () => {
    if (!jobId) {
      return
    }
    try {
      await api.cancelJob(jobId)
      message.info('已请求停止，等待当前帧结束')
    } catch (err: any) {
      message.error('停止失败: ' + err.message)
    }
  }

  const exportCsv = async () => {
    if (!recordId) {
      message.warning('还没有可导出的结果')
      return
    }
    try {
      const out = await api.command('history_export', { id: recordId })
      downloadText(`measurement-${recordId}.csv`, out.csv)
    } catch (err: any) {
      message.error('导出失败: ' + err.message)
    }
  }

  const itemColumns = [
    {
      title: '序号',
      dataIndex: 'seq',
      width: 70,
      render: (_: any, row: TestItem, index: number) => (
        <InputNumber
          size="small"
          min={1}
          value={row.seq || index + 1}
          onChange={(v) => update(row.key, { seq: Number(v ?? index + 1) })}
          style={{ width: 58 }}
        />
      ),
    },
    {
      title: 'P',
      dataIndex: 'p',
      width: 70,
      render: (_: any, row: TestItem) => (
        <InputNumber
          size="small"
          min={1}
          max={640}
          value={row.p}
          onChange={(v) => update(row.key, { p: Number(v ?? 1) })}
          style={{ width: 58 }}
        />
      ),
    },
    {
      title: 'N',
      dataIndex: 'n',
      width: 70,
      render: (_: any, row: TestItem) => (
        <InputNumber
          size="small"
          min={1}
          max={640}
          value={row.n}
          onChange={(v) => update(row.key, { n: Number(v ?? 1) })}
          style={{ width: 58 }}
        />
      ),
    },
    {
      title: '线制',
      dataIndex: 'wire',
      width: 80,
      render: (_: any, row: TestItem) => (
        <Select
          size="small"
          options={WIRE_OPTIONS}
          value={row.wire}
          onChange={(v) => update(row.key, { wire: v as 0 | 1 })}
          style={{ width: 68 }}
        />
      ),
    },
    {
      title: '量程',
      dataIndex: 'range',
      width: 80,
      render: (_: any, row: TestItem) => (
        <Select
          size="small"
          options={RANGE_OPTIONS}
          value={row.range}
          onChange={(v) => update(row.key, { range: v })}
          style={{ width: 68 }}
        />
      ),
    },
    {
      title: '下限 Ω',
      dataIndex: 'minOhms',
      width: 100,
      render: (_: any, row: TestItem) => (
        <InputNumber
          size="small"
          value={row.minOhms ?? undefined}
          onChange={(v) => update(row.key, { minOhms: v === null ? null : Number(v) })}
          style={{ width: 86 }}
        />
      ),
    },
    {
      title: '上限 Ω',
      dataIndex: 'maxOhms',
      width: 100,
      render: (_: any, row: TestItem) => (
        <InputNumber
          size="small"
          value={row.maxOhms ?? undefined}
          onChange={(v) => update(row.key, { maxOhms: v === null ? null : Number(v) })}
          style={{ width: 86 }}
        />
      ),
    },
    {
      title: '测量',
      dataIndex: 'measure',
      width: 70,
      render: (_: any, row: TestItem) => (
        <Switch
          size="small"
          checked={row.measure}
          onChange={(v) => update(row.key, { measure: v })}
        />
      ),
    },
    {
      title: '',
      width: 60,
      render: (_: any, row: TestItem) => (
        <Button
          size="small"
          type="link"
          danger
          onClick={() => setItems((prev) => prev.filter((i) => i.key !== row.key))}
        >
          删除
        </Button>
      ),
    },
  ]

  const shownResults = useMemo(
    () =>
      onlyFail
        ? results.filter((r) => r.result !== 'pass')
        : results,
    [results, onlyFail],
  )

  const resultColumns = [
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
      render: (v: number | null) =>
        v === null || v === undefined ? '-' : v.toFixed(3),
    },
    {
      title: '判定',
      dataIndex: 'result',
      width: 90,
      render: (v: string) => (
        <Tag color={RESULT_COLOR[v]}>{RESULT_TEXT[v] ?? v}</Tag>
      ),
    },
    {
      title: '原因',
      dataIndex: 'reason',
      render: (v: string) => REASON_TEXT[v] ?? v ?? '',
    },
  ]

  const done = progress?.done ?? 0
  const total = progress?.total ?? (summary?.total ?? items.length)
  const passed = progress?.passed ?? summary?.passed ?? 0
  const failed = progress?.failed ?? summary?.failed ?? 0
  const measureFailed = progress?.measureFailed ?? summary?.measureFailed ?? 0
  const percent = total > 0 ? Math.round((done / total) * 100) : 0

  return (
    <div>
      <div className="panel">
        <div className="panel-title">设备连接</div>
        <div className="toolbar">
          <span>串口</span>
          <Select
            style={{ width: 130 }}
            value={port || undefined}
            placeholder="选择串口"
            options={ports.map((p) => ({ value: p, label: p }))}
            onChange={setPort}
            disabled={connected || busy}
          />
          <Button type="primary" onClick={connect} loading={busy} disabled={connected || !wsOpen}>
            连接
          </Button>
          <Button onClick={disconnect} disabled={!connected}>
            断开
          </Button>
          <span style={{ width: 12 }} />
          <span>仿真串口</span>
          <Select
            style={{ width: 130 }}
            value={simPort || undefined}
            options={ports.map((p) => ({ value: p, label: p }))}
            onChange={setSimPort}
            disabled={simRunning || busy}
          />
          <Button onClick={toggleSim} loading={busy} disabled={!wsOpen || !isAdmin}>
            {simRunning ? '停止仿真' : '启动仿真'}
          </Button>
          <Tag color={simRunning ? 'blue' : 'default'}>
            {simRunning ? `仿真中 ${state?.sim?.port ?? ''}` : '仿真未启动'}
          </Tag>
          {connected && state?.device && (
            <Tag color="green">
              固件 {state.device.firmware} · 量程 {state.device.rangeCount} 档
            </Tag>
          )}
        </div>
      </div>

      <div className="panel">
        <div className="panel-title">产品与操作员</div>
        <div className="toolbar">
          <span>型号</span>
          <Input style={{ width: 160 }} value={model} onChange={(e) => setModel(e.target.value)} />
          <span>编号</span>
          <Input style={{ width: 160 }} value={code} onChange={(e) => setCode(e.target.value)} />
          <span>操作员</span>
          <Input style={{ width: 120 }} value={operator} onChange={(e) => setOperator(e.target.value)} />
        </div>
      </div>

      <div className="panel">
        <div className="panel-title">
          测试项
          <Tag>{items.length}</Tag>
        </div>
        <div className="toolbar" style={{ marginBottom: 8 }}>
          <Upload accept=".csv,.json,.txt" showUploadList={false} beforeUpload={onImport}>
            <Button icon={<UploadOutlined />}>导入 CSV / JSON</Button>
          </Upload>
          <Button onClick={addRow}>添加一行</Button>
          <Popconfirm title="清空全部测试项？" onConfirm={() => setItems([])}>
            <Button danger disabled={items.length === 0}>
              清空
            </Button>
          </Popconfirm>
          <span style={{ width: 12 }} />
          <span>统一线制</span>
          <Select style={{ width: 80 }} options={WIRE_OPTIONS} value={presetWire} onChange={setPresetWire} />
          <span>量程</span>
          <Select style={{ width: 80 }} options={RANGE_OPTIONS} value={presetRange} onChange={setPresetRange} />
          <Button onClick={applyPreset} disabled={items.length === 0}>
            应用到全部
          </Button>
        </div>
        <Table
          size="small"
          rowKey="key"
          columns={itemColumns as any}
          dataSource={items}
          pagination={{ pageSize: 50, size: 'small', showSizeChanger: false }}
          scroll={{ y: 196 }}
          locale={{ emptyText: '还没有测试项：导入 CSV/JSON 或点“添加一行”' }}
        />
      </div>

      <div className="panel">
        <div className="panel-title">执行</div>
        <div className="toolbar" style={{ marginBottom: 10 }}>
          <Button type="primary" onClick={start} loading={busy} disabled={!connected || running}>
            开始测试
          </Button>
          <Button danger onClick={stop} disabled={!running}>
            停止
          </Button>
          <Button icon={<DownloadOutlined />} onClick={exportCsv} disabled={!recordId}>
            导出 CSV
          </Button>
          <span style={{ width: 12 }} />
          <Switch checked={onlyFail} onChange={setOnlyFail} size="small" />
          <span>只看失败</span>
          <span className="spacer" style={{ flex: 1 }} />
          {progress && (
            <Tag color={progress.state === 'completed' ? 'green' : running ? 'blue' : 'orange'}>
              {progress.state}
            </Tag>
          )}
        </div>
        <Progress percent={percent} status={running ? 'active' : 'normal'} />
        <div className="stat-row" style={{ margin: '8px 0 12px' }}>
          <div>
            <div className="stat-value">{done}/{total}</div>
            <div className="stat-label">进度</div>
          </div>
          <div>
            <div className="stat-value" style={{ color: '#15803d' }}>{passed}</div>
            <div className="stat-label">通过</div>
          </div>
          <div>
            <div className="stat-value" style={{ color: '#b91c1c' }}>{failed}</div>
            <div className="stat-label">失败</div>
          </div>
          <div>
            <div className="stat-value" style={{ color: '#b45309' }}>{measureFailed}</div>
            <div className="stat-label">测量失败</div>
          </div>
          <div>
            <div className="stat-value">
              {total > 0 ? ((passed * 100) / total).toFixed(1) : '0.0'}%
            </div>
            <div className="stat-label">通过率</div>
          </div>
          {recordId && (
            <div>
              <div className="stat-value">#{recordId}</div>
              <div className="stat-label">记录号</div>
            </div>
          )}
        </div>
        <div style={{ borderTop: '1px solid #eef0f4', paddingTop: 10, marginBottom: 10 }}>
          <div className="panel-title" style={{ marginBottom: 6 }}>
            实时读数
          </div>
          <LiveReadings points={live} />
        </div>
        <Table
          size="small"
          rowKey="seq"
          columns={resultColumns as any}
          dataSource={shownResults}
          pagination={{ pageSize: 50, size: 'small', showSizeChanger: false }}
          scroll={{ y: 200 }}
          locale={{ emptyText: '测试完成后在这里显示逐项结果' }}
        />
      </div>
    </div>
  )
}
