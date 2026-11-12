import { useState } from 'react'
import { Collapse, Tabs, Tag, Typography } from 'antd'
import ThreeViewer from '../components/ThreeViewer'

const { Paragraph, Text } = Typography

const PROTOCOL = `AA 55 CMD LEN DATA SUM8

帧总长 = 5 + LEN，SUM8 从 CMD 起逐字节求和取低 8 位。

请求                      应答
0x01 GET_DEVICE_INFO  ->  0x81 14 字节
0x02 MEASURE_SINGLE   ->  0x82 13 字节
0x03 MEASURE_BATCH    ->  0x83 6 + 4N 字节
0x04 SET_PARAM        ->  0x80 1 字节
0x05 GET_PARAM        ->  0x84 7 字节
0x08 RESET_DEVICE     ->  0x80 1 字节
0x0A SET_MODE         ->  0x80 1 字节
0x0B GET_MODE         ->  0x86 2 字节

批量单帧最多 62 组（5 + 4×62 = 253）。`

export default function HelpPage() {
  const [tab, setTab] = useState('guide')

  return (
    <div>
      <div className="panel">
        <div className="panel-title">
          EPH205 综合测试系统
          <Tag color="blue">v0.1</Tag>
          <Tag>React + v1 底座</Tag>
        </div>
        <Tabs
          activeKey={tab}
          onChange={setTab}
          items={[
            {
              key: 'guide',
              label: '使用说明',
              children: (
                <Collapse
                  defaultActiveKey={['1', '2']}
                  items={[
                    {
                      key: '1',
                      label: '快速开始（无硬件，com0com 仿真）',
                      children: (
                        <Paragraph>
                          <ol>
                            <li>
                              确认本机有 com0com 端口对（当前为 COM7 ↔ COM8）。
                            </li>
                            <li>在「测试」页选择仿真串口 COM8，点「启动仿真」。</li>
                            <li>选择串口 COM7，点「连接」。</li>
                            <li>
                              导入 CSV/JSON 测试项，或点「添加一行」手工录入。
                            </li>
                            <li>点「开始测试」，看进度与逐项结果。</li>
                            <li>完成后点「导出 CSV」。</li>
                          </ol>
                        </Paragraph>
                      ),
                    },
                    {
                      key: '2',
                      label: '导入格式',
                      children: (
                        <>
                          <Paragraph>
                            CSV 带表头（推荐）：
                            <br />
                            <Text code>
                              序号,P,N,线制,量程,下限,上限,测量
                            </Text>
                          </Paragraph>
                          <Paragraph>
                            <ul>
                              <li>线制：二线 / 四线，或 0 / 1</li>
                              <li>量程：0..5</li>
                              <li>上限留空表示只判下限</li>
                              <li>测量：0/否 表示本项跳过</li>
                            </ul>
                          </Paragraph>
                          <Paragraph>
                            Excel（.xlsx / .xls）：读取第一个工作表；若存在名为
                            <Text code>测试项</Text> 的工作表则优先用它。
                            表头与 CSV 相同，空单元格按“未填”处理。
                            旧的固定单元格版式不支持。
                          </Paragraph>
                          <Paragraph>
                            JSON：数组，或 <Text code>{'{ items: [...], product: {...} }'}</Text>。
                            字段名支持 p/n/wire/range/minOhms/maxOhms/measure。
                          </Paragraph>
                        </>
                      ),
                    },
                    {
                      key: '3',
                      label: '判定规则',
                      children: (
                        <ul>
                          <li>测量值为 NaN（超量程）→ 测量失败</li>
                          <li>整帧失败 → 本帧全部测量失败，原因记为整帧失败</li>
                          <li>勾选取消 → 未测</li>
                          <li>其余按上下限判通过/失败，上限为空只判下限</li>
                        </ul>
                      ),
                    },
                  ]}
                />
              ),
            },
            {
              key: 'protocol',
              label: '协议速查',
              children: <pre className="frame-log mono">{PROTOCOL}</pre>,
            },
            {
              key: 'stack',
              label: '技术栈',
              children: (
                <DescriptionsLike />
              ),
            },
            {
              key: '3d',
              label: '三维视图',
              children: <ThreeViewer />,
            },
          ]}
        />
      </div>
    </div>
  )
}

function DescriptionsLike() {
  return (
    <ul>
      <li>服务端：C++17，v1 通讯底座（Mongoose + nlohmann/json），零 Qt</li>
      <li>设备链路：Win32 串口，com0com 虚拟串口对做仿真</li>
      <li>前端：React + Ant Design + TypeScript + Vite</li>
      <li>控制/查询：HTTP REST；进度与事件：WebSocket</li>
      <li>存储：追加式 JSONL（data/eph205/measurements.jsonl）</li>
    </ul>
  )
}
