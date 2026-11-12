import { useEffect, useState } from 'react'
import {
  App,
  Button,
  Descriptions,
  Input,
  InputNumber,
  Select,
  Space,
  Table,
} from 'antd'
import { api } from '../api'

interface Props {
  state: any
  refresh: () => void
  wsOpen: boolean
}

const PARAM_TYPES = [
  { value: 1, label: '1 线制' },
  { value: 2, label: '2 量程' },
  { value: 3, label: '3 平均次数' },
  { value: 4, label: '4 稳定延时' },
  { value: 5, label: '5 超时' },
]
const MODES = [
  { value: 0, label: '恒流' },
  { value: 1, label: '恒压' },
]
const SOURCES = [
  { value: 1, label: '分离源' },
  { value: 2, label: 'Howland' },
  { value: 3, label: 'AD7124' },
]

export default function DebugPage({ state, refresh, wsOpen }: Props) {
  const { message } = App.useApp()
  const [p, setP] = useState(1)
  const [n, setN] = useState(2)
  const [reading, setReading] = useState<any>(null)
  const [paramType, setParamType] = useState(1)
  const [paramValue, setParamValue] = useState(0)
  const [params, setParams] = useState<any>(null)
  const [mode, setMode] = useState(0)
  const [source, setSource] = useState(2)
  const [frames, setFrames] = useState<{ side: string; text: string }[]>([])
  const [busy, setBusy] = useState(false)
  const [rawCmd, setRawCmd] = useState(1)
  const [rawData, setRawData] = useState('')
  const [rawOut, setRawOut] = useState<any>(null)
  const [checkP, setCheckP] = useState(1)
  const [checkN, setCheckN] = useState(2)
  const [checkOut, setCheckOut] = useState<any>(null)

  const connected = !!state?.connected

  useEffect(() => {
    if (!wsOpen || !connected) {
      return
    }
    let alive = true
    const tick = async () => {
      try {
        const out = await api.command('frame_log')
        if (!alive) return
        const merged: { side: string; text: string }[] = []
        ;(out.client ?? []).forEach((t: string) =>
          merged.push({ side: '上位机', text: t }),
        )
        ;(out.sim ?? []).forEach((t: string) =>
          merged.push({ side: '仿真器', text: t }),
        )
        if (merged.length > 0) {
          setFrames((prev) => [...prev, ...merged].slice(-300))
        }
      } catch {
        // 测量作业进行中会返回忙，忽略
      }
    }
    const timer = window.setInterval(tick, 1500)
    return () => {
      alive = false
      window.clearInterval(timer)
    }
  }, [wsOpen, connected])

  const guard = async (fn: () => Promise<void>) => {
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

  const measure = () =>
    guard(async () => {
      const out = await api.command('measure', { p, n })
      setReading(out)
    })

  const readParams = () =>
    guard(async () => {
      const out = await api.command('get_params')
      setParams(out)
      setMode(out.mode ?? 0)
      setSource(out.source ?? 2)
    })

  const writeParam = () =>
    guard(async () => {
      await api.command('set_param', { type: paramType, value: paramValue })
      message.success('参数已下发')
      const out = await api.command('get_params')
      setParams(out)
    })

  const writeMode = () =>
    guard(async () => {
      await api.command('set_mode', { mode, source })
      message.success('模式已下发')
    })

  const reset = () =>
    guard(async () => {
      await api.command('reset')
      message.success('设备已复位')
    })

  const sendRaw = () =>
    guard(async () => {
      const out = await api.command('raw', { cmd: rawCmd, data: rawData })
      setRawOut(out)
      message.success(`已收到 ${out.cmdText}`)
    })

  const checkPoint = () =>
    guard(async () => {
      const out = await api.command('check_point', { p: checkP, n: checkN })
      setCheckOut(out)
    })

  return (
    <div>
      <div className="panel">
        <div className="panel-title">单点测量</div>
        <div className="toolbar">
          <span>P</span>
          <InputNumber min={1} max={640} value={p} onChange={(v) => setP(Number(v ?? 1))} />
          <span>N</span>
          <InputNumber min={1} max={640} value={n} onChange={(v) => setN(Number(v ?? 1))} />
          <Button type="primary" onClick={measure} loading={busy} disabled={!connected}>
            测量
          </Button>
          <Button onClick={readParams} disabled={!connected}>
            读取参数
          </Button>
          <Button onClick={reset} disabled={!connected}>
            复位设备
          </Button>
        </div>
        {reading && (
          <Descriptions size="small" column={4} style={{ marginTop: 10 }}>
            <Descriptions.Item label="阻值">
              {reading.ohms === null || reading.ohms === undefined
                ? '-'
                : Number(reading.ohms).toFixed(3) + ' Ω'}
            </Descriptions.Item>
            <Descriptions.Item label="状态">{reading.statusText}</Descriptions.Item>
            <Descriptions.Item label="原始 ADC">{reading.rawAdc}</Descriptions.Item>
            <Descriptions.Item label="差分">{reading.differentialUv} µV</Descriptions.Item>
          </Descriptions>
        )}
      </div>

      <div className="panel">
        <div className="panel-title">参数与模式</div>
        <div className="toolbar" style={{ marginBottom: 8 }}>
          <span>SET_PARAM</span>
          <Select style={{ width: 140 }} options={PARAM_TYPES} value={paramType} onChange={setParamType} />
          <InputNumber min={0} max={255} value={paramValue} onChange={(v) => setParamValue(Number(v ?? 0))} />
          <Button onClick={writeParam} disabled={!connected}>
            下发
          </Button>
          <span style={{ width: 12 }} />
          <span>模式</span>
          <Select style={{ width: 90 }} options={MODES} value={mode} onChange={setMode} />
          <Select style={{ width: 110 }} options={SOURCES} value={source} onChange={setSource} />
          <Button onClick={writeMode} disabled={!connected}>
            下发模式
          </Button>
        </div>
        {params && (
          <Descriptions size="small" column={4}>
            <Descriptions.Item label="线制">{params.wire === 0 ? '二线' : '四线'}</Descriptions.Item>
            <Descriptions.Item label="量程">
              {params.range === 255 ? '未设置' : `R${params.range}`}
            </Descriptions.Item>
            <Descriptions.Item label="平均次数">{params.averageTimes}</Descriptions.Item>
            <Descriptions.Item label="稳定延时">{params.settleDelay}</Descriptions.Item>
            <Descriptions.Item label="模式">{params.mode === 0 ? '恒流' : '恒压'}</Descriptions.Item>
            <Descriptions.Item label="源">{params.source}</Descriptions.Item>
            <Descriptions.Item label="输出">{params.outputEnabled ? '开' : '关'}</Descriptions.Item>
            <Descriptions.Item label="并联负载">{params.parallelLoad}</Descriptions.Item>
          </Descriptions>
        )}
      </div>

      <div className="panel">
        <div className="panel-title">
          原始命令
          <span style={{ color: '#667085', fontWeight: 400, fontSize: 12 }}>
            直接下发一帧，DATA 用十六进制，如 01 00 02 00
          </span>
        </div>
        <div className="toolbar" style={{ marginBottom: 8 }}>
          <Select
            style={{ width: 190 }}
            value={rawCmd}
            onChange={setRawCmd}
            options={[
              { value: 1, label: '0x01 设备信息' },
              { value: 2, label: '0x02 单点测量' },
              { value: 5, label: '0x05 读参数' },
              { value: 6, label: '0x06 校准' },
              { value: 7, label: '0x07 点检测试' },
              { value: 8, label: '0x08 复位' },
              { value: 9, label: '0x09 原始 ADC' },
              { value: 10, label: '0x0A 设置模式' },
              { value: 11, label: '0x0B 读模式' },
            ]}
          />
          <Input
            style={{ width: 240 }}
            placeholder="DATA（十六进制，可空）"
            value={rawData}
            onChange={(e) => setRawData(e.target.value)}
          />
          <Button onClick={sendRaw} loading={busy} disabled={!connected}>
            发送
          </Button>
        </div>
        {rawOut && (
          <div className="mono" style={{ lineHeight: 1.7 }}>
            应答 {rawOut.cmdText}（0x
            {Number(rawOut.cmd).toString(16).padStart(2, '0').toUpperCase()}）
            · {rawOut.len} 字节
            {rawOut.statusText !== undefined && (
              <>
                {' '}
                · 状态 <b>{rawOut.statusText}</b>
              </>
            )}
            <div style={{ color: '#344054' }}>
              {rawOut.dataHex || '（空）'}
            </div>
          </div>
        )}
      </div>

      <div className="panel">
        <div className="panel-title">
          点检测位
          <span style={{ color: '#667085', fontWeight: 400, fontSize: 12 }}>
            0x07，当前固件未实现，用来验证错误路径
          </span>
        </div>
        <div className="toolbar" style={{ marginBottom: 8 }}>
          <span>P</span>
          <InputNumber min={1} max={640} value={checkP} onChange={(v) => setCheckP(Number(v ?? 1))} />
          <span>N</span>
          <InputNumber min={1} max={640} value={checkN} onChange={(v) => setCheckN(Number(v ?? 1))} />
          <Button onClick={checkPoint} loading={busy} disabled={!connected}>
            点检
          </Button>
        </div>
        {checkOut && (
          <div className="mono">
            应答 {checkOut.cmdText} · {checkOut.len} 字节
            {checkOut.statusText !== undefined && (
              <>
                {' '}
                · 状态 <b>{checkOut.statusText}</b>
              </>
            )}
            <div style={{ color: '#344054' }}>{checkOut.dataHex || '（空）'}</div>
          </div>
        )}
      </div>

      <div className="panel">
        <div className="panel-title">原始帧</div>
        <div className="frame-log mono">
          {frames.length === 0
            ? '连接并执行命令后，这里显示收发帧'
            : frames.map((f, i) => `${f.side}  ${f.text}`).join('\n')}
        </div>
      </div>
    </div>
  )
}
