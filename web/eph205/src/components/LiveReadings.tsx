// 实时读数：数据来自 v1 的 WS 数据流（stream id 1），不是轮询。
// 曲线用原生 canvas 画，避免为一个折线图再引一个图表库。

import { useEffect, useRef } from 'react'
import { Empty, Tag } from 'antd'

export interface LivePoint {
  seq: number
  p: number
  n: number
  ohms: number
  result: string
}

interface Props {
  points: LivePoint[]
}

export default function LiveReadings({ points }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) {
      return
    }
    const dpr = window.devicePixelRatio || 1
    const width = canvas.clientWidth || 600
    const height = canvas.clientHeight || 140
    canvas.width = Math.round(width * dpr)
    canvas.height = Math.round(height * dpr)
    const ctx = canvas.getContext('2d')
    if (!ctx) {
      return
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.clearRect(0, 0, width, height)
    ctx.fillStyle = '#0b1220'
    ctx.fillRect(0, 0, width, height)

    const values = points
      .map((p) => Number(p.ohms))
      .filter((v) => Number.isFinite(v))
    if (values.length < 2) {
      return
    }
    const min = Math.min(...values)
    const max = Math.max(...values)
    // 仿真器默认所有点同值，此时把线画在中间，别贴着底边
    const flat = max - min < 1e-9
    const span = Math.max(max - min, Math.abs(max) * 0.02, 0.001)
    const padTop = 10
    const padBottom = 16
    const usable = height - padTop - padBottom
    const xOf = (i: number) => (i / (values.length - 1)) * (width - 8) + 4
    const yOf = (v: number) =>
      flat ? padTop + usable / 2 : padTop + usable - ((v - min) / span) * usable

    // 参考线：均值
    const mean = values.reduce((a, b) => a + b, 0) / values.length
    ctx.strokeStyle = '#2a3550'
    ctx.setLineDash([4, 4])
    ctx.beginPath()
    ctx.moveTo(0, yOf(mean))
    ctx.lineTo(width, yOf(mean))
    ctx.stroke()
    ctx.setLineDash([])

    ctx.strokeStyle = '#4c8dff'
    ctx.lineWidth = 1.5
    ctx.beginPath()
    values.forEach((v, i) => {
      if (i === 0) {
        ctx.moveTo(xOf(i), yOf(v))
      } else {
        ctx.lineTo(xOf(i), yOf(v))
      }
    })
    ctx.stroke()

    // 失败点标红
    points.forEach((p, i) => {
      if (p.result === 'pass' || !Number.isFinite(Number(p.ohms))) {
        return
      }
      ctx.fillStyle = '#ff5c5c'
      ctx.beginPath()
      ctx.arc(xOf(i), yOf(Number(p.ohms)), 3, 0, Math.PI * 2)
      ctx.fill()
    })

    ctx.fillStyle = '#8ea0c0'
    ctx.font = '11px Consolas, monospace'
    if (flat) {
      ctx.fillText(`恒定 ${max.toFixed(3)} Ω`, 6, 12)
    } else {
      ctx.fillText(max.toFixed(3), 6, 12)
      ctx.fillText(min.toFixed(3), 6, height - 4)
      ctx.fillText(`均值 ${mean.toFixed(3)} Ω`, width - 110, height - 4)
    }
  }, [points])

  const last = points[points.length - 1]

  return (
    <div>
      <div className="toolbar" style={{ marginBottom: 6 }}>
        <span className="stat-value" style={{ fontSize: 16 }}>
          {last && Number.isFinite(Number(last.ohms))
            ? `${Number(last.ohms).toFixed(3)} Ω`
            : '-'}
        </span>
        {last && (
          <Tag color={last.result === 'pass' ? 'green' : 'red'}>
            P{last.p}-N{last.n} {last.result === 'pass' ? '通过' : '失败'}
          </Tag>
        )}
        <span style={{ color: '#667085', fontSize: 12 }}>
          已推送 {points.length} 条（WS 数据流）
        </span>
      </div>
      {points.length === 0 ? (
        <Empty
          image={Empty.PRESENTED_IMAGE_SIMPLE}
          description="开始测试后，这里按服务端推送实时出数"
          style={{ margin: '12px 0' }}
        />
      ) : (
        <canvas
          ref={canvasRef}
          style={{ width: '100%', height: 140, borderRadius: 4, display: 'block' }}
        />
      )}
    </div>
  )
}
