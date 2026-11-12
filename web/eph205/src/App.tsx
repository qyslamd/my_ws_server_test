import { useCallback, useEffect, useRef, useState } from 'react'
import {
  App as AntApp,
  Badge,
  ConfigProvider,
  Layout,
  Menu,
  Space,
  Tag,
  Typography,
} from 'antd'
import zhCN from 'antd/locale/zh_CN'
import { api, type JobProgress } from './api'
import MeasurePage from './pages/MeasurePage'
import DebugPage from './pages/DebugPage'
import HistoryPage from './pages/HistoryPage'
import ToolsPage from './pages/ToolsPage'
import HelpPage from './pages/HelpPage'

const { Sider, Content } = Layout
const { Text } = Typography

const MENU = [
  { key: 'measure', label: '测试' },
  { key: 'debug', label: '调试' },
  { key: 'history', label: '历史' },
  { key: 'tools', label: '工具' },
  { key: 'help', label: '帮助' },
]

function Shell() {
  const [wsStatus, setWsStatus] = useState('connecting')
  const [page, setPage] = useState('measure')
  const [state, setState] = useState<any>(null)
  const [progress, setProgress] = useState<JobProgress | null>(null)
  const [progressLog, setProgressLog] = useState<JobProgress[]>([])
  const mounted = useRef(true)

  const refresh = useCallback(async () => {
    if (!api.wsOpen) {
      return
    }
    try {
      const next = await api.getState()
      if (mounted.current) {
        setState(next)
      }
    } catch {
      // 会话断开时忽略，状态条会提示
    }
  }, [])

  useEffect(() => {
    mounted.current = true
    const offEvent = api.onEvent((name, payload) => {
      if (name === 'eph205.job.progress') {
        setProgress(payload)
        setProgressLog((prev) => [...prev.slice(-9), payload])
      }
      if (name === 'core.state' || name === 'core.module.state') {
        void refresh()
      }
    })
    const offStatus = api.onStatus((status) => setWsStatus(status))
    api.connect().then(
      () => {
        setWsStatus('open')
        void refresh()
      },
      () => setWsStatus('error'),
    )
    const timer = window.setInterval(() => void refresh(), 2000)
    return () => {
      mounted.current = false
      window.clearInterval(timer)
      offEvent()
      offStatus()
      api.close()
    }
  }, [refresh])

  const deviceConnected = !!state?.connected
  const device = state?.device

  return (
    <Layout style={{ height: '100%' }}>
      <div className="app-header">
        <span className="title">EPH205 综合测试系统</span>
        <Badge
          status={wsStatus === 'open' ? 'success' : wsStatus === 'connecting' ? 'processing' : 'error'}
          text={
            <span style={{ color: '#d0d5dd' }}>
              {wsStatus === 'open' ? '通讯正常' : wsStatus === 'connecting' ? '连接中' : '已断开'}
            </span>
          }
        />
        <span className="spacer" />
        <Space size={12}>
          <Tag color={deviceConnected ? 'green' : 'default'}>
            {deviceConnected ? `已连接 ${state?.port ?? ''}` : '未连接'}
          </Tag>
          {device && (
            <Text style={{ color: '#98a2b3', fontSize: 12 }}>
              固件 {device.firmware} · P{device.pCount}/N{device.nCount} · MUX{' '}
              {device.muxBoards}
            </Text>
          )}
          <Text style={{ color: '#667085', fontSize: 12 }}>sid {api.sessionId}</Text>
        </Space>
      </div>
      <Layout>
        <Sider width={120} theme="light">
          <Menu
            mode="inline"
            selectedKeys={[page]}
            items={MENU}
            onClick={(e) => setPage(e.key)}
            style={{ height: '100%', borderInlineEnd: '1px solid #e6e8ec' }}
          />
        </Sider>
        <Content className="app-body">
          {/* 测试页常驻：切到别的页再回来，导入的测试项和结果还在 */}
          <div style={{ display: page === 'measure' ? 'block' : 'none' }}>
            <MeasurePage
              state={state}
              refresh={refresh}
              progress={progress}
              progressLog={progressLog}
              wsOpen={wsStatus === 'open'}
            />
          </div>
          {page === 'debug' && (
            <DebugPage state={state} refresh={refresh} wsOpen={wsStatus === 'open'} />
          )}
          {page === 'history' && <HistoryPage wsOpen={wsStatus === 'open'} />}
          {page === 'tools' && (
            <ToolsPage state={state} refresh={refresh} wsOpen={wsStatus === 'open'} />
          )}
          {page === 'help' && <HelpPage />}
        </Content>
      </Layout>
    </Layout>
  )
}

export default function App() {
  return (
    <ConfigProvider
      locale={zhCN}
      // 中文两字按钮不要自动加空格，自动化和肉眼都好对
      button={{ autoInsertSpace: false }}
      theme={{
        token: { fontSize: 13, borderRadius: 4, colorPrimary: '#1d4ed8' },
        components: { Table: { cellPaddingBlockSM: 4, cellPaddingInlineSM: 8 } },
      }}
    >
      <AntApp>
        <Shell />
      </AntApp>
    </ConfigProvider>
  )
}
