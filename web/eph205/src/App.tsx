import { useCallback, useEffect, useRef, useState } from 'react'
import {
  App as AntApp,
  Badge,
  Button,
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
import LoginPage from './pages/LoginPage'

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
  const [auth, setAuth] = useState<{ user: string; role: string } | null>(null)
  const [authChecked, setAuthChecked] = useState(false)
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
      async () => {
        setWsStatus('open')
        // 会话刚建立，问一下底座当前登录状态
        try {
          const me = await api.me()
          if (me.authenticated) {
            setAuth({ user: me.user, role: me.role })
          }
        } catch {
          // 没登录过就是未认证
        }
        setAuthChecked(true)
        void refresh()
      },
      () => {
        setWsStatus('error')
        setAuthChecked(true)
      },
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
  const isAdmin = auth?.role === 'admin'
  const menuItems = MENU.filter((item) => item.key !== 'debug' || isAdmin)

  const logout = async () => {
    await api.logout()
    // 换人登录后不该看到上一个人的作业进度
    setProgress(null)
    setProgressLog([])
    setState(null)
    setAuth(null)
    setPage('measure')
  }

  if (!authChecked) {
    return (
      <div
        style={{
          height: '100%',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          color: '#667085',
        }}
      >
        {wsStatus === 'error' ? '无法连接服务端' : '正在连接…'}
      </div>
    )
  }

  if (!auth) {
    return (
      <LoginPage
        onLogin={(user) => {
          setProgress(null)
          setProgressLog([])
          setAuth(user)
          setPage('measure')
        }}
      />
    )
  }

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
          <Tag color={isAdmin ? 'gold' : 'default'}>
            {auth.user} · {isAdmin ? '管理员' : '操作员'}
          </Tag>
          <Button size="small" onClick={logout} ghost>
            退出
          </Button>
        </Space>
      </div>
      <Layout>
        <Sider width={120} theme="light">
          <Menu
            mode="inline"
            selectedKeys={[page]}
            items={menuItems}
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
              role={auth.role}
            />
          </div>
          {page === 'debug' && (
            <DebugPage state={state} refresh={refresh} wsOpen={wsStatus === 'open'} />
          )}
          {page === 'history' && (
            <HistoryPage wsOpen={wsStatus === 'open'} role={auth.role} />
          )}
          {page === 'tools' && (
            <ToolsPage
              state={state}
              refresh={refresh}
              wsOpen={wsStatus === 'open'}
              role={auth.role}
            />
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
