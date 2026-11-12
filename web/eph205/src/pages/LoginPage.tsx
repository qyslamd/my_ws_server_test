import { useState } from 'react'
import { Alert, Button, Card, Form, Input, Space, Tag, Typography } from 'antd'
import { LockOutlined, UserOutlined } from '@ant-design/icons'
import { api } from '../api'

const { Title, Text } = Typography

interface Props {
  onLogin: (user: { user: string; role: string }) => void
}

export default function LoginPage({ onLogin }: Props) {
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')

  const submit = async (values: { username: string; password: string }) => {
    setBusy(true)
    setError('')
    try {
      const out = await api.login(values.username, values.password)
      onLogin({ user: out.user, role: out.role })
    } catch (err: any) {
      setError(err.message === 'invalid credentials' ? '用户名或密码错误' : err.message)
    } finally {
      setBusy(false)
    }
  }

  return (
    <div
      style={{
        height: '100%',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        background: 'linear-gradient(160deg, #101828 0%, #1d2939 55%, #243b53 100%)',
      }}
    >
      <Card style={{ width: 380, boxShadow: '0 12px 40px rgba(0,0,0,.35)' }}>
        <Space direction="vertical" size={4} style={{ marginBottom: 18 }}>
          <Title level={4} style={{ margin: 0 }}>
            EPH205 综合测试系统
          </Title>
          <Text type="secondary">请登录后使用</Text>
        </Space>
        <Form
          layout="vertical"
          initialValues={{ username: 'admin', password: 'admin' }}
          onFinish={submit}
        >
          <Form.Item
            label="用户名"
            name="username"
            rules={[{ required: true, message: '请输入用户名' }]}
          >
            <Input prefix={<UserOutlined />} autoComplete="username" />
          </Form.Item>
          <Form.Item
            label="密码"
            name="password"
            rules={[{ required: true, message: '请输入密码' }]}
          >
            <Input.Password prefix={<LockOutlined />} autoComplete="current-password" />
          </Form.Item>
          {error && (
            <Alert type="error" showIcon message={error} style={{ marginBottom: 12 }} />
          )}
          <Button type="primary" htmlType="submit" block loading={busy}>
            登录
          </Button>
        </Form>
        <div style={{ marginTop: 14, color: '#98a2b3', fontSize: 12 }}>
          内置账号：
          <Tag color="blue" style={{ marginLeft: 4 }}>admin/admin 管理员</Tag>
          <Tag>operator/operator 操作员</Tag>
        </div>
      </Card>
    </div>
  )
}
