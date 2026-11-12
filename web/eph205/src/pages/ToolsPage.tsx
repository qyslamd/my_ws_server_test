import { useEffect, useState } from 'react'
import {
  App,
  Button,
  Descriptions,
  InputNumber,
  Switch,
  Table,
  Tag,
} from 'antd'
import { api } from '../api'

interface Props {
  state: any
  refresh: () => void
  wsOpen: boolean
}

export default function ToolsPage({ state, refresh, wsOpen }: Props) {
  const { message } = App.useApp()
  const [defaultOhms, setDefaultOhms] = useState(100)
  const [p, setP] = useState(1)
  const [n, setN] = useState(2)
  const [ohms, setOhms] = useState(100)
  const [over, setOver] = useState(false)
  const [logs, setLogs] = useState<string[]>([])
  const [serverLogs, setServerLogs] = useState<string[]>([])
  const [busy, setBusy] = useState(false)

  const sim = state?.sim

  useEffect(() => {
    if (sim) {
      setDefaultOhms(sim.defaultOhms ?? 100)
    }
  }, [sim?.defaultOhms])

  useEffect(() => {
    if (!wsOpen) return
    let alive = true
    const tick = async () => {
      try {
        const out = await api.logs()
        if (!alive) return
        setServerLogs(out.server ?? [])
        setLogs(out.modules?.eph205 ?? [])
      } catch {
        // ignore
      }
    }
    void tick()
    const timer = window.setInterval(tick, 2000)
    return () => {
      alive = false
      window.clearInterval(timer)
    }
  }, [wsOpen])

  const run = async (fn: () => Promise<void>) => {
    setBusy(true)
    try {
      await fn()
    } catch (err: any) {
      message.error(err.message)
    } finally {
      setBusy(false)
      refresh()
    }
  }

  const applyDefault = () =>
    run(async () => {
      await api.command('sim_config', { defaultOhms: Number(defaultOhms) })
      message.success('默认阻值已更新')
    })

  const applyPoint = () =>
    run(async () => {
      await api.command('sim_set', { p, n, ohms: Number(ohms), over })
      message.success(`已设置 P${p}-N${n}`)
    })

  const clearOverrides = () =>
    run(async () => {
      await api.command('sim_clear')
      message.success('已清除全部点值覆盖')
    })

  const overrideColumns = [
    { title: 'P', dataIndex: 'p', width: 70 },
    { title: 'N', dataIndex: 'n', width: 70 },
    {
      title: '阻值 Ω',
      dataIndex: 'ohms',
      width: 110,
      render: (v: number | null) =>
        v === null || v === undefined ? '超量程' : Number(v).toFixed(3),
    },
  ]

  return (
    <div>
      <div className="panel">
        <div className="panel-title">
          仿真器
          <Tag color={sim?.running ? 'blue' : 'default'}>
            {sim?.running ? `运行中 ${sim?.port ?? ''}` : '未启动'}
          </Tag>
        </div>
        <div className="toolbar" style={{ marginBottom: 8 }}>
          <span>默认阻值 Ω</span>
          <InputNumber
            min={0}
            max={100000}
            value={defaultOhms}
            onChange={(v) => setDefaultOhms(Number(v ?? 100))}
            style={{ width: 120 }}
          />
          <Button onClick={applyDefault} loading={busy} disabled={!sim?.running}>
            应用
          </Button>
        </div>
        <div className="toolbar" style={{ marginBottom: 8 }}>
          <span>P</span>
          <InputNumber min={1} max={640} value={p} onChange={(v) => setP(Number(v ?? 1))} />
          <span>N</span>
          <InputNumber min={1} max={640} value={n} onChange={(v) => setN(Number(v ?? 1))} />
          <span>阻值 Ω</span>
          <InputNumber min={0} value={ohms} onChange={(v) => setOhms(Number(v ?? 100))} disabled={over} />
          <span>超量程</span>
          <Switch size="small" checked={over} onChange={setOver} />
          <Button onClick={applyPoint} loading={busy} disabled={!sim?.running}>
            设置点值
          </Button>
          <Button onClick={clearOverrides} loading={busy} disabled={!sim?.running}>
            清除覆盖
          </Button>
        </div>
        <Table
          size="small"
          rowKey={(row: any) => `${row.p}-${row.n}`}
          columns={overrideColumns as any}
          dataSource={sim?.overrides ?? []}
          pagination={false}
          scroll={{ y: 200 }}
          locale={{ emptyText: '没有点值覆盖，全部使用默认阻值' }}
        />
      </div>

      <div className="panel">
        <div className="panel-title">运行信息</div>
        <Descriptions size="small" column={4}>
          <Descriptions.Item label="串口">{state?.storedPort || '-'}</Descriptions.Item>
          <Descriptions.Item label="仿真串口">{state?.simPort || '-'}</Descriptions.Item>
          <Descriptions.Item label="历史记录数">{state?.records ?? 0}</Descriptions.Item>
          <Descriptions.Item label="设备">{state?.connected ? '已连接' : '未连接'}</Descriptions.Item>
        </Descriptions>
      </div>

      <div className="panel">
        <div className="panel-title">模块日志</div>
        <div className="frame-log mono">
          {logs.length === 0 ? '暂无日志' : logs.join('\n')}
        </div>
      </div>

      <div className="panel">
        <div className="panel-title">底座日志</div>
        <div className="frame-log mono">
          {serverLogs.length === 0 ? '暂无日志' : serverLogs.join('\n')}
        </div>
      </div>
    </div>
  )
}
