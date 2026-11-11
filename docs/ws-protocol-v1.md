# 工控通讯模型 v1:HTTP REST + WebSocket 推送

> 定位:**万能底座**。只规定"怎么连、怎么订、怎么送、怎么停",
> 不规定任何 AD/DA/DIO/CAN/1553B/ARINC429/FlexRay/AFDX 的业务语义。
> 业务通过注册流和命令来扩展,协议本身零业务逻辑。
>
> 传输:HTTP/1.1 REST(请求-响应)+ RFC 6455 WebSocket(服务端主动推送)。
> 状态:设计稿(v1),取代 `docs/ws-protocol.md`(下方称"v0 demo 规范")。

---

## 0. 一页摘要

**两种传输,各司其职:**

| 传输 | 职责 | 方向 |
|---|---|---|
| HTTP REST | 能力/流发现、订阅、参数、任务启停、业务命令、状态查询 | 客户端发起,服务端应答 |
| WebSocket | 二进制遥测帧、JSON 事件 | 服务端主动推送 |

WS 只做它最擅长的事:服务端主动推数据。凡是"客户端发起、要一个结果"的操作,
一律走 HTTP,用标准方法、状态码和资源语义表达,不再自定义命令/应答信封。

四个核心概念:

| 概念 | 一句话 |
|---|---|
| **会话 Session** | 由一条 WS 连接锚定的 1:1 关系;WS 断开即安全联锁 |
| **流 Stream** | 独立订阅、独立序号、独立时间基的数据源;业务注册,协议不解释 |
| **帧 Frame** | WS 上的二进制自描述容器:24 字节头 + TLV 块 |
| **事件 Event** | WS 上的 JSON 文本消息,服务端主动通知 |

五条不变式:

1. **底座零业务**:协议不认识 CAN,也不认识波形;它只搬 `streamId` + 字节 + META。
2. **WS 只推送**:客户端在 WS 上只允许说"credit"和协议级 Pong,其余全走 HTTP。
3. **无损默认**:数据流默认不丢、保序;丢数据必须是显式事件,不能静默发生。
4. **采集永不阻塞**:业务/采集线程只写有界缓冲,绝不等待网络。
5. **控制断则停机**:WS 连接断开或被判死,服务端必须安全收尾,不重试、不恢复。

---

## 1. 目标与非目标

### 1.1 目标

- **一套通讯层,所有项目复用**:AD、DA、DIO、CAN、RS232/485/422、1553B、
  ARINC429、FlexRay、AFDX 等都只是"流"和"HTTP 命令"的实例。
- **服务端只搭架子**:提供会话、流注册、命令路由、背压、生命周期;
  业务模块独立注册,不污染协议层。
- **同一份实现两种部署**:嵌入 CEF 当"伪 CS"跑在 `127.0.0.1`,
  或局域网一机采集、另一机控制显示。
- **客户端自由**:每个项目自己的 UI/UX;通讯层提供稳定的 REST + WS 契约。
- **可长期演进**:新增流、新增命令、新增块类型不破坏旧客户端。

### 1.2 非目标(明确不做,防止跑偏)

| 非目标 | 原因 |
|---|---|
| 定义任何总线报文格式 | 业务语义,属于业务模块与 META |
| 多客户端/多操作员 | 使用场景 1:1;接口不写死,但 v1 不做权限仲裁 |
| 跨总线时间对齐 | 实际场景各总线独立处理,不做统一时基 |
| 服务端录制/回放/补发 | WS 断开即安全停机,重连从空闲态开始 |
| 由服务端描述驱动前端动态渲染 | 客户端是换技术栈重写的定制上位机,不是低代码平台 |
| 文件/固件传输 | v1 的配置是结构化参数,不是文件搬运 |
| **视频/媒体传输** | **v1 完全不做;后续用 HTTP 单独定义**(见 §16) |
| 压缩、加密、鉴权体系 | 局域网/本机部署;预留扩展位,不在 v1 强制 |

### 1.3 成功标准

- 新项目接入一个总线,只新增业务模块与前端 UI,**不改协议、不改底座**。
- CEF 本机与局域网跨机两种部署,使用同一套协议与同一份服务端代码。
- 拔网线/杀客户端后,服务端在心跳超时内进入安全态,且不残留输出。
- 慢客户端导致缓冲溢出时,客户端能收到明确的 overrun 事件,而不是"数据悄悄少了"。
- 所有控制操作都能用 curl / Postman 直接验证,不需要私有调试工具。

---

## 2. 术语

| 术语 | 含义 |
|---|---|
| Session | 一次客户端-服务端关系;由一条 WS 连接锚定,携带 `sid` |
| Stream | 业务注册的独立数据源;有 `streamId`、类型、序号、时间基 |
| Frame | WS 上的一个二进制消息 |
| Block | 帧内的 TLV 单元:`[type u8][len u32][data]` |
| Record | RECORD 类流中的一条带时间戳的记录;负载由业务定义 |
| META | UTF-8 JSON,描述流的类型与负载布局 |
| Timebase | 流的时间基声明;不同流之间不承诺可比 |
| Capability | 能力标记,通过 HTTP 与 WS 协商 |

---

## 3. 总体架构

```text
┌────────────────────────── 客户端(每项目定制 UI) ──────────────────────────┐
│  UI / 图表 / 控件           业务适配层(知道 CAN 帧、波形含义)              │
│        └──────────┬────────────────────────┬──────────────────────────────┘
│             HTTP Client                WS Client                          │
└───────────────────┼────────────────────────┼──────────────────────────────┘
                    │ HTTP/1.1               │ WebSocket
┌───────────────────┴────────────────────────┴──────────────────────────────┐
│                        服务端底座(纯 C/C++,零业务)                         │
│  HTTP Router ─┐   WS Endpoint ─┐                                          │
│               └─ Session Manager ── Stream Engine ── Registry             │
│                                        │                  │               │
│                                业务模块注册表(命令 / 流源 / 参数 provider)  │
│                                        │                  │               │
│                                业务模块:ad0、can0、1553…                   │
└────────────────────────────────────────────────────────────────────────────┘
```

底座只做五件事:接受 HTTP 请求、维护 WS 会话、路由命令、管理流订阅、搬运二进制帧。
业务模块通过注册接口挂上来;底座不知道也不关心模块内部。

---

## 4. 连接与会话

### 4.1 地址

| 端点 | URL | 用途 |
|---|---|---|
| HTTP API | `http://<host>:<port>/api/v1/...` | 控制、配置、查询 |
| WS | `ws://<host>:<port>/ws?sid=<sid>` | 服务端推送 |

同端口、同源;前端页面由同一服务端提供时不存在跨域问题。
若页面独立部署,服务端需按 §6.10 配置 CORS。

### 4.2 建连顺序

```text
1. 客户端生成 sid(建议 UUID),打开 ws://host:port/ws?sid=<sid>
2. 服务端校验 sid 格式与唯一性,绑定会话,返回 WS 1000(正常建立)
3. 客户端用 HTTP 带 X-Session-Id: <sid> 调用 /api/v1/*
4. POST /api/v1/subscriptions 订阅流,响应里带回 META
5. POST /api/v1/tasks/start 开始任务,WS 开始推帧
```

- **WS 连接是会话锚点**:没有活动 WS 时,所有需要会话的 HTTP 请求返回
  `409 Conflict` + `code 2001`。
- 同一 `sid` 只允许一条 WS;第二条连接被拒绝(关闭码 `4001`)。
- 已有活动会话时,新 `sid` 的 WS 也会被拒绝(`4001`),HTTP 同样 409。
- `sid` 由客户端生成,长度 8–64,字符集 `[A-Za-z0-9_-]`;服务端只校验格式与唯一性。

### 4.3 两种传输的关系

- HTTP 响应是**权威结果**;WS 事件只是**通知**。
- 同一条 WS 内的消息保序;跨 HTTP 与 WS **不承诺顺序**。
- 客户端必须以 HTTP 响应更新自身状态,WS 事件用于刷新显示与处理异步变化。
- 服务端不得依赖客户端"先收到事件再发请求"这类假设。

### 4.4 部署形态

| 形态 | 地址 | 说明 |
|---|---|---|
| CEF 嵌入 | `http://127.0.0.1:<port>` + `ws://127.0.0.1:<port>/ws` | 同机,零配置 |
| 局域网 | `http://<采集机IP>:<port>` + 对应 WS | 服务端绑定指定网卡,防火墙放行 |

v1 默认明文。需要加密时整体换 `https://` + `wss://`,协议不变。

---

## 5. 会话生命周期与安全联锁

### 5.1 状态机

```text
服务端视角:

  WS open ──► IDLE
                 │ POST /tasks/start
                 ▼
              RUNNING ──POST /tasks/stop──► STOPPING ──► IDLE
                 │
        WS 断开/心跳超时
                 ▼
             SAFE_STOP ──► IDLE ──► 关闭会话
```

- `IDLE`:WS 已建立,业务未运行;可改参数、可订阅。
- `RUNNING`:`start` 成功后;采集与推流进行中。
- `STOPPING`:收到显式 `stop`,等待业务收尾。
- `SAFE_STOP`:控制端失联触发的强制收尾,语义比 `STOPPING` 更硬:
  停采集、把输出打到安全值、不等待业务"优雅完成"。

### 5.2 断线 = 安全联锁

**WS 断开、或心跳超时、或服务端主动关闭,服务端必须:**

1. 立即进入 `SAFE_STOP`;
2. 调用所有业务模块的 `on_session_end(reason)`;
3. 停止采集、停止推流、把 DA/DIO 等输出置为业务定义的安全值;
4. 使该 `sid` 失效,后续 HTTP 请求返回 `409` + `code 2001`;
5. 回到空闲,等待新的 WS 会话。

**明确不做**:自动重连、任务续跑、缓存补发。客户端重连建立的是**新会话**,
只能从 IDLE 重新开始。

### 5.3 心跳与判死

- 服务端每 5 s 在 WS 上发 Ping;15 s 内未收到 Pong 或任何帧 → 判死。
- v1 依赖 WS Ping/Pong 作为唯一存活判据;浏览器会自动回 Pong,客户端无需额外代码。
  HTTP keep-alive 不参与存活判定。
- 判死等同断开,触发安全联锁。
- 理由:TCP 半开时对端收不到 FIN,只靠 socket 状态无法发现失联。
- HTTP 的 keep-alive 不承担存活判定,避免"HTTP 还通但 WS 已死"的歧义。

### 5.4 显式停机

- `POST /api/v1/tasks/stop` 触发 `STOPPING`;
- 业务必须在有限时间内完成(建议 ≤ 5 s);超时由服务端强制进入安全值并记录告警;
- 停机后 WS 保持,会话仍在 IDLE,可再次 start。

### 5.5 单会话约束

- 同一时刻只允许一个活动会话。
- 同一 `sid` 重复打开 WS → 旧连接收到 `4000`,新连接生效(便于客户端崩溃后快速恢复)。
- 不同 `sid` 在已有活动会话时 → `4001`。

---

## 6. HTTP REST API

### 6.1 通用约定

- 前缀 `/api/v1`,资源名为复数,动作用子资源或 `:action`。
- 请求/响应体为 UTF-8 JSON;时间字段用毫秒整数(与 WS 一致)。
- 需要会话的请求必须带 `X-Session-Id: <sid>`,且该会话的 WS 必须存活。
- 幂等性:`GET/PUT/DELETE` 天然幂等;`POST` 命令建议带 `Idempotency-Key`
  (同一 key 在 60 s 内重复提交返回首次结果)。
- 所有响应带 `X-Request-Id`;错误响应见 6.9。

### 6.2 资源总表

| 方法与路径 | 用途 | 会话 |
|---|---|---|
| `GET /api/v1/capabilities` | 协议版本、能力、限制 | 否 |
| `GET /api/v1/state` | 当前会话状态 | 是 |
| `GET /api/v1/streams` | 流列表(摘要) | 是 |
| `GET /api/v1/streams/{id}` | 单流详情(含 META) | 是 |
| `POST /api/v1/subscriptions` | 订阅流,返回 META | 是 |
| `DELETE /api/v1/subscriptions/{streamId}` | 退订 | 是 |
| `GET /api/v1/params/{scope}` | 读取参数 | 是 |
| `PATCH /api/v1/params/{scope}` | 增量修改(merge) | 是 |
| `PUT /api/v1/params/{scope}` | 整包替换(replace) | 是 |
| `POST /api/v1/tasks/start` | 开始任务 | 是 |
| `POST /api/v1/tasks/stop` | 停止任务 | 是 |
| `POST /api/v1/commands/{ns}/{action}` | 业务命令 | 是 |
| `GET /api/v1/commands` | 可用业务命令列表 | 是 |
| `DELETE /api/v1/session` | 主动结束会话(触发安全收尾) | 是 |

> 媒体/视频端点不在 v1 范围内(见 §16)。

### 6.3 capabilities

```http
GET /api/v1/capabilities
```

```json
{
  "proto": 1,
  "server": "daqd/1.0.0",
  "caps": ["credit", "frag", "describe-full"],
  "limits": {
    "max_data_frame": 8388608,
    "default_window": 1048576,
    "buffer_bytes": {"sample": 4194304, "record": 8388608},
    "send_queue_bytes": 67108864,
    "max_control_bytes": 1048576
  }
}
```

- 无需会话,可用于探活与版本检查。
- `caps` 是服务端支持的能力;客户端能力在 WS 建连的 `Sec-WebSocket-Protocol`
  或首个 credit 消息中不需要重复声明——v1 只有服务端能力,客户端必须全部支持。

### 6.4 流发现与订阅

```http
GET /api/v1/streams
→ 200 [{"id":1,"name":"ad0.ch0","class":"sample","timebase":"device",
        "rateHz":1000,"bufferBytes":4194304}]
```

```http
GET /api/v1/streams/1
→ 200 {"id":1,"name":"ad0.ch0","class":"sample","timebase":"device",
       "rateHz":1000,"bufferBytes":4194304,"meta":{...完整 META...}}
```

```http
POST /api/v1/subscriptions
Content-Type: application/json
X-Session-Id: s-7f3a2c

{"streams":[{"id":1,"window":1048576},{"id":2,"window":0}]}
```

```json
200 OK
{
  "subscriptions": [
    {"streamId":1,"window":1048576,"bufferBytes":4194304,"meta":{...}},
    {"streamId":2,"window":0,"bufferBytes":8388608,"meta":{...}}
  ]
}
```

- **订阅响应里携带完整 META**,客户端不必等 WS 帧就能建立解析器。
- `window` 缺省用 `default_window`;`0` 表示"先只注册、不推数据",之后用 WS credit 放量。
- 订阅是**全有或全无**:任一流不存在 → `404` + `code 2002`,不建立任何订阅。
- 重复订阅同一流是幂等的,不重复返回错误;`window` 以最后一次为准。
- 退订:`DELETE /api/v1/subscriptions/1` → `204`;未订阅 → `404` + `code 2003`。

### 6.5 参数

```http
GET  /api/v1/params/can0
PUT  /api/v1/params/can0
PATCH /api/v1/params/can0
```

```json
PUT /api/v1/params/can0
{"values":{"bitrate":500000,"samplePoint":0.875}}
```

```json
200 OK
{"scope":"can0","revision":7,"values":{"bitrate":500000,"samplePoint":0.875}}
```

- `PUT` = replace(整体替换该 scope),`PATCH` = merge(按 key 合并)。
- `values` 完全由业务定义,底座不解释、不校验。
- `revision` 每个 scope 独立单调递增,只在成功写入后 +1;失败时值与 revision 均不变。
- 参数默认**只保存在内存**,会话结束即丢弃;需要跨重启保留由业务模块自行持久化。
- 在 RUNNING 下允许读写,业务可以拒绝(返回 `422` + 3xxx)。

### 6.6 任务启停

```http
POST /api/v1/tasks/start
X-Session-Id: s-7f3a2c

{"task":"rail-brake-test","params":{"pressure":0.6}}
```

```json
200 OK
{"state":"running","task":"rail-brake-test","startedAt":1731234567890}
```

- `params` 先按 merge 写入 `session` 作用域并校验,**通过后才调用业务 `on_start`**;
  失败则整体不生效,会话留在 `IDLE`。
- 任一模块启动失败 → 回滚已启动模块,返回 `500` + `code 2007`(带 `detail.module`)。
- `POST /api/v1/tasks/stop` → `{"reason":"operator"}` → `200 {"state":"idle"}`。
- 若业务声明收尾可能超过 10 s,服务端返回 `202 Accepted` + `{"state":"stopping"}`,
  最终状态通过 WS 事件 `core.state` 通知。

### 6.7 业务命令

```http
POST /api/v1/commands/can0/send
{"channel":0,"id":291,"ext":false,"data":"0102030405060708"}
```

```json
200 OK
{"ok":true,"result":{...}}
```

- `{ns}` 必须匹配已注册模块;未知命名空间/动作 → `404` + `code 2004`。
- 业务校验失败 → `422` + 3xxx + `detail`。
- 命令在事件循环上同步执行,业务必须快速返回;长任务应自行转工作线程并立即应答。

### 6.8 状态查询

```http
GET /api/v1/state
→ 200 {"state":"running","task":"rail-brake-test","session":"s-7f3a2c",
        "streams":2,"startedAt":1731234567890}
```

### 6.9 状态码与错误体

| HTTP | 含义 | 典型 code |
|---|---|---|
| 200 | 成功 | — |
| 202 | 已接受,异步完成 | — |
| 204 | 成功无内容(退订) | — |
| 400 | 请求格式错误 | 1001 非法 JSON、1003 缺字段、1004 版本不支持 |
| 401 | 缺少/无效会话凭据 | 4001 未授权 |
| 404 | 资源不存在 | 2002 流不存在、2003 未订阅、2004 未知命令 |
| 409 | 会话/状态冲突 | 2001 会话忙/无会话、2005 额度非法、2006 状态不允许 |
| 413 | 请求体过大 | 2008 describe 过大 |
| 422 | 业务校验失败 | 3xxx |
| 500 | 内部/模块错误 | 2007 模块启动失败、4004 内部异常 |
| 503 | 服务暂不可用 | 4005 |

错误体:

```json
{"code":3001,"msg":"bitrate out of range","retryable":false,
 "detail":{"field":"bitrate","min":10000,"max":1000000}}
```

- `retryable` 缺省 `false`。
- 业务错误码必须落在 3xxx;底座不解释 `detail`。
- 响应同时带 `X-Request-Id`,便于与服务端日志对齐。

### 6.10 CORS

页面与服务端同源时无需配置。独立部署时服务端必须:

- 允许 `Origin` 白名单(默认只允许同源与本机);
- 允许 `OPTIONS` 预检,回 `Access-Control-Allow-Methods` 与
  `Access-Control-Allow-Headers: Content-Type, X-Session-Id, Idempotency-Key`;
- 不回 `Access-Control-Allow-Credentials`,v1 不使用 Cookie。

---

## 7. WebSocket 推送协议

### 7.1 连接与上行消息

- 地址:`ws://host:port/ws?sid=<sid>`。
- 服务端 → 客户端:二进制帧(§7.2)+ JSON 事件(§7.9)。
- 客户端 → 服务端:**只允许**以下两类消息,其它一律忽略并记日志:

```json
{"v":1,"type":"credit","streams":[{"id":1,"bytes":262144}]}
```

- credit 是唯一的业务上行消息;服务端不 ack,错误通过事件 `core.error` 通知。
- 协议级 Pong 由浏览器自动回复,客户端不需要处理。

### 7.2 二进制帧结构

固定头 **24 字节**,所有多字节整数小端:

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0 | magic | u8 | 固定 `0xA5` |
| 1 | version | u8 | 帧格式版本,当前 `0x01` |
| 2 | opcode | u8 | 帧类型,见 7.3 |
| 3 | flags | u8 | 帧级标志,见 7.3 |
| 4 | streamId | u32 | 流 ID,0 保留 |
| 8 | seq | u32 | 流内递增序号,回绕后继续 |
| 12 | ts | u64 | 帧时间戳,纳秒,按流的 timebase 解释 |
| 20 | bodyLen | u32 | 后续块区域总字节数 |
| 24 | blocks | — | TLV 块序列 |

块格式:

```text
[type: u8][len: u32 LE][data: len bytes]
```

- 接收端必须校验 `bodyLen` 与块长度之和;不一致 → 整帧丢弃并上报。
- 未知 `type` 的块必须跳过,不得报错(向前兼容)。
- `len` 为 0 表示存在但为空,接收端跳过。
- `bodyLen` 永远描述**本帧自己的块区域**,不表示逻辑帧总长度;
  逻辑帧边界由 `flags.LAST` 决定(见 7.8)。

### 7.3 枚举

**opcode**

| 值 | 名称 | 语义 |
|---|---|---|
| 0x01 | SNAPSHOT | 订阅/元数据变更后的首帧,必带 META |
| 0x02 | DELTA | 常规增量帧 |
| 0x03 | EVENT | 流级事件(开始/结束/错误) |
| 0x04 | ERROR | 该流或该通道的错误,负载为 JSON |
| 0x05 | HEARTBEAT | **保留不用**;保活一律用 WS Ping/Pong,收到静默忽略 |

**flags**

| bit | 名称 | 语义 |
|---|---|---|
| 0 | FRAG | 本帧是分片帧 |
| 1 | LAST | 分片的最后一片 |
| 2..7 | 保留 | 必须为 0,接收端忽略 |

**块类型(core 保留)**

| 值 | 名称 | 内容 |
|---|---|---|
| 0x01 | DATA | 主数据块,SAMPLE/STATE 类使用;布局由 META 描述 |
| 0x02 | META | UTF-8 JSON;仅在 SNAPSHOT 或流元数据变更时出现 |
| 0x03 | RECORDS | 标准记录容器,见 7.6 |
| 0x04 | AUX | 辅助数据(校准、索引、业务自定义),底座不解释 |
| 0x80–0xFE | 业务扩展 | 由业务模块定义,必须在 META 中声明 |
| 0xFF | 保留 | 禁止使用 |

### 7.4 流类型(class)

| class | 语义 | 典型用途 | 可靠性 |
|---|---|---|---|
| `sample` | 等间隔采样数组 | AD 波形、DA 回读、DIO 位图快照 | 无损 |
| `record` | 变长记录序列,每条带时间戳 | CAN、1553B、ARINC429、FlexRay、AFDX、DIO 边沿 | 无损 |
| `state` | 最新值;订阅时全量,变化时增量 | 设备状态、温度、连接状态 | 无损(可合并) |

v1 **不定义媒体类**。视频/音频走后续的 HTTP 方案(§16)。

底座只实现这三类的投递语义;每类内部的负载布局由 META 描述,业务解释。

### 7.5 META 约定

META 是流的自描述:

- 订阅响应里必须携带完整 META;WS 的 SNAPSHOT 也会再带一次,作为冗余校验。
- 流元数据变化时,服务端发新的 SNAPSHOT(带新 META),客户端替换缓存。
- 客户端在收到 META 前不得解析该流的数据块。
- 未知字段必须忽略。

公共字段:

| 字段 | 必填 | 说明 |
|---|---|---|
| kind | 是 | 业务数据类型名,如 `waveform`、`can_frame` |
| class | 是 | `sample` / `record` / `state` |
| schemaVer | 是 | 载荷布局版本,业务自增 |
| timebase | 是 | `unix_ns` / `monotonic_ns` / `device` |
| layout | 视 class | 负载布局描述,见下 |

`sample` 的 layout:

```json
{"dataType":"float32","components":1,"count":1024,"rateHz":1000,"unit":"V"}
```

`record` 的 layout:

```json
{"record":{"fields":[
  {"name":"id","type":"u32"},
  {"name":"flags","type":"u8"},
  {"name":"data","type":"bytes"}
]}}
```

> 底座只要求上述字段存在且类型正确,不校验业务字段的语义。

### 7.6 记录容器(RECORDS)

`record` 类流统一使用块类型 `0x03`:

```text
repeat:
  [ts: u64 LE]      记录时间戳,纳秒(timebase 由 META 声明)
  [len: u32 LE]     记录负载字节数
  [payload: len]    业务定义,按 META.layout 解释
```

一条帧可包含 0..N 条记录;`bodyLen` 负责边界校验。业务只定义 `payload`
字段布局,底座统一处理时间戳、分片和序号。

### 7.7 序号与时间

**seq 的分配规则:**

- `seq` 按流独立递增,在**逻辑帧进入发送队列时**分配一次;
  分片共享同一个 `seq`,不单独占号。
- `state` 的合并在**分配 seq 之前**完成(只保留最新值),不制造假缺口。
- `sample` / `record` 在生产写入环形缓冲时即分配 `seq`;缓冲溢出丢弃的帧
  会留下真实缺口,由 `core.overrun` 的 `firstSeq/lastSeq` 定位。
- `SNAPSHOT` **不重置** `seq`;客户端以订阅后收到的第一个帧作为基线。
- `core.overrun` 里的 `firstSeq/lastSeq` 使用同一个计数器。

- `ts` 是帧级时间;`record` 内每条记录另带自己的 `ts`。
- **不承诺跨流时间可比**。每条流独立使用自己的 timebase;
  需要对时由业务在其 payload 中自行携带基准信息。

### 7.8 分片

- 单条 WS 消息上限由 `limits.max_data_frame` 声明(默认 8 MiB)。
- 分片是对**逻辑帧块区域(body)的字节级切分**,允许在任意字节处切开,
  包括把一个 TLV 块切成两半;接收端不得逐片解析 TLV。
- 每个分片都是完整帧:24 字节头 + 本片字节;所有分片共用 `streamId`、`seq`、`ts`。
- `flags.FRAG=1` 表示属于分片序列,`flags.LAST=1` 表示逻辑帧结束。
- `bodyLen` 是本片自身的字节数;协议**不携带**总长度或偏移量,
  重组方式就是按序拼接各片 body,直到 `LAST`。
- 同一逻辑帧的分片必须连续发送;不同流之间可以轮转,但同一流的分片序列中间
  不得插入其它帧。
- 任一分片丢失/被丢弃 → 整个逻辑帧作废,禁止部分交付。
- 单片负载上限 = `max_data_frame - 24`。
- **仅 `AUX` 块允许分片**;`sample` / `record` 必须单帧发送,
  业务应通过拆流或控制帧大小来适配。

### 7.9 事件(JSON 文本帧)

服务端通过 WS 文本消息推送事件:

```json
{"v":1,"type":"evt","name":"core.state","payload":{"state":"running"},
 "ts":1731234567890}
```

| 事件名 | payload | 说明 |
|---|---|---|
| `core.state` | `{"state":"idle\|running\|stopping"}` | 会话状态变化 |
| `core.stream.added` | `{"stream":{...}}` | 运行时注册新流 |
| `core.stream.removed` | `{"streamId":7}` | 流被移除 |
| `core.overrun` | `{"streamId":1,"droppedFrames":3,"droppedBytes":12288,"firstSeq":40,"lastSeq":42}` | 缓冲溢出 |
| `core.error` | `{"code":2005,"msg":"invalid credit","streamId":1}` | 上行消息或异步错误 |
| `core.session.ending` | `{"reason":"..."}` | 安全收尾开始,随后关闭 |
| `biz.*` | 业务定义 | 告警、进度、模块状态 |

- 事件不带 `id`;客户端不得依赖事件顺序跨流一致。
- `core.overrun` 按流 500 ms 节流聚合,避免事件风暴。
- 事件与二进制帧共用一条 WS,小事件可能被一个大帧延迟;
  控制结果以 HTTP 响应为准,事件只用于显示与异步状态。

### 7.10 WS 关闭码

| 码 | 含义 |
|---|---|
| 4000 | 会话结束(安全收尾/被新会话替换) |
| 4001 | 会话忙 |
| 4002 | 协议错误 |
| 4003 | 未授权/会话无效 |

---

## 8. 背压与流控

### 8.1 不变式

```text
业务/采集线程 ──写──► 每流有界环形缓冲 ──按 credit 取──► 发送队列 ──► TCP
        (永不阻塞)              (满则 overrun)         (满则暂停取数)
```

1. 业务线程写缓冲**不允许阻塞**,也不能因为网络慢而停采。
2. 每条流一个环形缓冲,容量由 `limits` 或业务配置决定;按**整帧淘汰**,
   不允许覆盖半帧(否则 `droppedFrames` 无法计算)。
3. 客户端订阅时声明 `window`(字节),之后通过 WS `credit` 消息归还额度。
4. 服务端只发送"已获额度"范围内的数据;额度耗尽时暂停该流,不丢数据。
5. 环形缓冲被写满 → 覆盖最旧的**完整帧**,并发 `core.overrun`。
6. `state` 类流**不参与 credit**,只保留最新值,不产生 overrun。
7. **没有订阅者时不产生投递义务**:`sample/record` 不入队、不占 seq、不报 overrun;
   `state` 直接丢弃。这样"没人看"不会把缓冲撑爆。

### 8.2 额度语义

- `window` 与 `credit.bytes` 的单位都是**负载字节数**,不含帧头与块头。
- 服务端发出一个流的帧后,从该流额度中扣除 `payloadLen`。
- 额度可以多次累加;超过 `window` 上限的部分**截断不报错**,通过
  `core.error` 的 `detail` 告知客户端,便于发现记账 bug。
- credit 只对 `sample` / `record` 生效;对 `state` 发 credit 是无操作,
  服务端忽略并发一次 `core.error`(`code 2005`,可关闭)。
- `window:0` 时只注册订阅、不推数据;客户端拿到订阅响应中的 META 后可放心放量。

### 8.3 过载的可观测性

- `core.overrun`:`{streamId, droppedFrames, droppedBytes, firstSeq, lastSeq}`,
  按流 500 ms 节流聚合;
- WS 上另发一帧 `opcode=ERROR`,payload 为同样信息的 JSON,保证即使事件通道
  拥塞也能看到。该帧约定:
  - `streamId` = 出问题的流(不是保留的 0);
  - `seq` = 该流当前序号,**不占号、不推进**;
  - `ts` = 事件发生时刻;
  - JSON 放在 `DATA` 块(类型 0x01),长度 ≤ 512 B;
  - 不占 credit。
- 服务端日志必须记录 overrun 的流、持续时长、丢弃量;
- 客户端 UI 应显式提示"数据缺口",不得静默继续画。

### 8.4 容量默认值(可覆盖)

| 项 | 默认值 | 说明 |
|---|---|---|
| `max_data_frame` | 8 MiB | 单条 WS 二进制消息上限 |
| `default_window` | 1 MiB | 订阅未声明 window 时的默认额度 |
| `buffer_bytes.sample` | 4 MiB/流 | 环形缓冲 |
| `buffer_bytes.record` | 8 MiB/流 | 环形缓冲 |
| 发送队列 | 64 MiB | 超限即视为该 WS 不可用,断开 |

规划建议:聚合吞吐不超过链路速率的 60%;单流不超过 20 MB/s;
超过这个量级的场景应拆流。默认值不是协议约束,真实数值以
`GET /capabilities` 返回的 `limits` 为准。

---

## 9. 错误模型

### 9.1 错误码区间

| 区间 | 类别 | 例子 |
|---|---|---|
| 1xxx | 请求/信封 | 1001 非法 JSON、1003 缺字段、1004 版本不支持 |
| 2xxx | 会话/传输 | 2001 会话忙或无会话、2002 流不存在、2003 未订阅、2004 未知命令、2005 额度非法、2006 状态不允许、2007 模块启动失败、2008 请求过大 |
| 3xxx | 业务 | 由业务模块定义,如 3001 参数越界 |
| 4xxx | 系统/安全 | 4001 未授权、4002 协议错误、4003 会话令牌无效、4004 内部异常、4005 服务不可用 |

### 9.2 错误体(HTTP)

```json
{"code":3001,"msg":"bitrate out of range","retryable":false,
 "detail":{"field":"bitrate","min":10000,"max":1000000}}
```

- 同时带 HTTP 状态码(映射见 6.9)与 `X-Request-Id`。
- `retryable` 缺省 `false`。
- 业务错误码必须落在 3xxx;底座不解释 `detail`。

### 9.3 WS 错误

- 上行 credit 非法、流级错误 → 事件 `core.error`;
- 数据帧级错误 → `opcode=ERROR` 帧;
- 会话级致命错误 → 先发 `core.session.ending`,再以 `4002/4003` 关闭。

---

## 10. 版本与扩展

- HTTP 版本在 URL:`/api/v1`;不兼容变更升 `/api/v2`。
- WS 帧格式版本在帧头 `version`;控制事件版本在 `v`。
- 新增 HTTP 资源、新增 WS opcode、新增块类型、新增事件属于兼容变更。
- 客户端必须:忽略未知事件、忽略未知块、跳过未知 opcode 的帧。
- 服务端必须:对未知 HTTP 路径回 404,对未知业务命令回 2004,不得静默忽略。
- 能力通过 `/capabilities` 的 `caps` 声明;使用未声明能力视为协议错误。
- 业务 META 的 `schemaVer` 与协议版本无关,由业务自行演进;客户端按
  `schemaVer` 选择解析器,不认识的版本应停止渲染该流并提示升级。

**v1 能力标记表:**

| cap | 含义 | 缺失时的行为 |
|---|---|---|
| `credit` | 支持流控(核心,必须实现) | 不适用;v1 客户端必须支持 |
| `frag` | 支持分片帧 | 服务端不得发送超过 `max_data_frame` 的逻辑帧 |
| `describe-full` | `GET /streams/{id}` 返回完整 META | 只返回摘要 |

---

## 11. 安全与部署

- v1 默认明文 HTTP/WS,适用于本机 CEF 与受控局域网。
- 局域网部署:服务端只绑定指定网卡;防火墙只放行控制网段。
- 会话凭据是 `sid`;可选服务端令牌:HTTP 带 `Authorization: Bearer <token>`,
  WS 用 query 参数或首个消息携带。令牌不写日志。
- 可选 TLS:整体换 `https://` + `wss://`,协议不变。
- CORS 默认只允许同源与本机(见 6.10)。
- 协议本身不做用户/角色/权限;需要时由业务在命令或参数层自行判断。
- 安全联锁优先于可用性:无法确认控制端存活时,一律停机。

---

## 12. 服务端骨架契约(参考实现)

底座只暴露四类注册点,业务模块不接触网络细节:

```cpp
// 0) 模块生命周期:底座只认这一个入口
struct Module {
  virtual ~Module() = default;
  virtual const char *ns() const = 0;              // "ad0" / "can0" / "1553"
  // 在事件循环上调用;禁止阻塞。失败返回 false 并填 err。
  virtual bool on_start(const Json &params, Error &err) = 0;
  virtual bool on_stop(const char *reason, Error &err) = 0;
  virtual void on_session_end(const char *reason) = 0;  // 安全收尾,禁止阻塞
};

// 1) 流源:业务把数据源注册进来
struct StreamSource {
  virtual ~StreamSource() = default;
  virtual StreamDesc describe() const = 0;       // id/name/class/timebase/meta
  virtual void start(StreamSink &sink) = 0;      // 由模块在自己线程里调用
  virtual void stop() = 0;
};
// StreamSink::push() 线程安全、非阻塞、可被任意生产者线程调用;
// 返回 false 表示该流无人订阅或缓冲已满,生产者不得等待。

// 2) 命令处理器:映射到 POST /commands/{ns}/{action}
struct CommandHandler {
  virtual ~CommandHandler() = default;
  virtual const char *ns() const = 0;
  virtual bool handle(const std::string &action,
                      const Json &payload, Json &result,
                      Error &err) = 0;           // 事件循环上调用,禁止阻塞
  virtual void list_actions(Json &out) const {}  // 可选:GET /commands
};

// 3) 参数 provider:映射到 /params/{scope}
struct ParamProvider {
  virtual ~ParamProvider() = default;
  virtual bool get(const std::string &scope, Json &values) = 0;
  virtual bool set(const std::string &scope, const Json &values,
                   bool replace, Json &applied, Error &err) = 0;
  virtual void describe_schema(Json &out) const {}  // 可选
};
```

**运行契约(所有业务模块必须遵守):**

| 项 | 约定 |
|---|---|
| 调用线程 | `Module::on_*`、`CommandHandler::handle`、`ParamProvider::*` 全部在事件循环线程执行,必须快速返回;长任务自行转工作线程 |
| 采集线程 | 由业务模块自己拥有;通过 `StreamSink::push()` 交给底座,不得直接碰网络 |
| 背压 | `push()` 永不阻塞;返回 false 表示无订阅或已满,由业务决定丢弃/降频 |
| 启动失败 | `on_start` 返回 false → 底座回滚已启动模块,返回 2007,会话留 IDLE |
| 停止语义 | `POST /tasks/stop` → `on_stop`;安全联锁/WS 断开 → `on_session_end`(不保证 `on_stop` 先执行) |
| 顺序 | `start` 按注册顺序调用,`stop` 逆序;HTTP 响应在全部返回后发出 |
| 异常 | 底座不捕获 C++ 异常;业务不得让异常穿过边界 |

**HTTP 路由约定:** 底座只实现 `/api/v1` 下的核心资源;业务命令统一走
`/api/v1/commands/{ns}/{action}`,不往底座里加新路由。这样底座路由表是固定的,
业务扩展只发生在注册表里。

---

## 13. 附录 A:业务扩展示例(非规范)

> 以下只是"业务可以怎么用这个底座"的示例,**不属于 v1 强制规定**。

### A.1 AD 波形(sample)

```json
META: {"kind":"waveform","class":"sample","schemaVer":1,"timebase":"device",
       "layout":{"dataType":"float32","components":1,"count":1024,
                 "rateHz":1000,"unit":"V"}}
帧:   SNAPSHOT, DATA=4096B, META, ts=采集起始时刻
```

### A.2 CAN 报文(record)

```json
META: {"kind":"can_frame","class":"record","schemaVer":1,"timebase":"device",
       "layout":{"record":{"fields":[
         {"name":"canId","type":"u32"},
         {"name":"ext","type":"u8"},
         {"name":"rtr","type":"u8"},
         {"name":"dlc","type":"u8"},
         {"name":"data","type":"bytes"}]}}}
帧:   DELTA, RECORDS=[{ts, len, payload}...]
```

发送一条 CAN 帧(业务命令):

```http
POST /api/v1/commands/can0/send
{"channel":0,"canId":291,"ext":false,"data":"0102030405060708"}
```

### A.3 DIO 边沿(record)与输出控制

```json
META layout.record.fields = [{"name":"channel","type":"u16"},
                             {"name":"level","type":"u8"}]
```

```http
POST /api/v1/commands/dio0/set
{"channel":3,"level":1}
```

### A.4 1553B / ARINC429 / FlexRay / AFDX

同一模式:一条或一组记录,字段换成 RT/SA、label/SDI、slot/cycle、VL/seq。
字段布局写在 META,底座不感知。

### A.5 参数包

```http
PUT /api/v1/params/ad0
{"values":{"channels":[{"id":0,"range":"±10V","enabled":true}]}}
```

---

## 14. 附录 B:与 v0 demo 规范的差异

| 项 | v0(demo) | v1 |
|---|---|---|
| 传输 | 两条 WS(stream/control) | HTTP REST + 一条 WS |
| 控制 | 自定义 cmd/ack/err 信封 | HTTP 方法/状态码/资源语义 |
| 通道 | 按流量方向拆 | 按"请求-响应 vs 主动推送"拆 |
| 块编码 | flags 位掩码,最多 8 种 | TLV,类型字节 + 长度,可扩展 |
| 帧头 | 12 字节 | 24 字节(增加 bodyLen、帧级 ts) |
| 流控 | 无 | credit + 有界缓冲 + overrun |
| 多流 | streamId 语义单一 | 完整流表 + 注册 + 动态增删 |
| 参数 | 单 key/value | scope + PUT/PATCH + revision |
| 安全 | 断开无强制收尾 | WS 断开 = 安全联锁 |
| 时间 | 单帧 ts | 帧 ts + 记录 ts + timebase 声明 |
| 媒体 | 无 | v1 不做,后续走 HTTP |
| 发现 | 无 | `/capabilities`、`/streams` |
| 错误 | 3 段区间 | 4 段 + HTTP 映射 + retryable + detail |
| 调试 | 私有前端 | curl / Postman / OpenAPI 可直接验证 |

v0 的 `docs/ws-protocol.md` 保留作为当前 demo 实现的说明;v1 落地后,
demo 代码按 v1 重构。

---

## 15. 验收清单(实现前先写测试)

| # | 用例 | 通过标准 |
|---|---|---|
| 1 | 会话锚点 | 无活动 WS 调用 `/subscriptions` → 409 + 2001 |
| 2 | 单会话 | 第二个 sid 的 WS 收到 4001;同 sid 重连替换旧连接 |
| 3 | 安全联锁 | 拔掉 WS,15 s 内进入 SAFE_STOP,业务钩子被调用,输出置安全值 |
| 4 | 订阅 | 订阅响应带完整 META;退订后不再推帧 |
| 5 | 无损投递 | 1 小时不间断采样,seq 无缺口;慢消费时数据在缓冲内不丢 |
| 6 | 背压 | window=0 时只注册不推;credit 放量;缓冲满时整帧淘汰并收到 overrun |
| 7 | 记录容器 | 多条 record 一帧,逐条 ts 正确;字段按 META 解析 |
| 8 | 分片 | 超过 max_data_frame 的 AUX 正确重组;丢一片时整帧丢弃 |
| 9 | 兼容 | 注入未知块类型/未知事件,客户端不崩、不误解 |
| 10 | REST 语义 | 404/409/422 与 code 映射正确;错误体带 X-Request-Id |
| 11 | 状态机 | RUNNING 下发 start → 409 + 2006;IDLE 下发 stop → 409 + 2006 |
| 12 | 启动回滚 | 模块 A 成功、模块 B 失败 → 500 + 2007,A 被 stop,会话留 IDLE |
| 13 | 参数原子性 | `tasks/start` 的 params 校验失败 → 不进入 RUNNING,参数与 revision 不变 |
| 14 | CORS | 独立源页面可完成 OPTIONS 预检与带 X-Session-Id 的请求 |
| 15 | 部署 | CEF 本机与局域网跨机均通过 1~14 |

---

## 15.1 附录 C:定稿取舍(需要评审确认)

以下是评审问题中**没有用户输入、由协议设计侧定稿**的条目,
集中列出便于一次性推翻或确认:

| # | 决定 | 理由 |
|---|---|---|
| 1 | WS 连接是会话锚点;HTTP 全部带 `X-Session-Id` | 一个存活判据,避免"HTTP 通但 WS 死"的歧义 |
| 2 | 订阅走 HTTP,响应直接带完整 META | 消除 `window:0` 时拿不到解析信息的死锁,也省掉 META-only 帧 |
| 3 | credit 是 WS 上唯一的业务上行消息 | 高频且极小,走 HTTP 会产生无谓请求量 |
| 4 | seq 在入队时分配;state 合并在分配之前 | 只有真实丢帧才产生缺口,避免假告警 |
| 5 | 分片 = body 字节级顺序拼接,共享 seq/ts,无偏移与总长 | 实现最简单,且能复用 TLV 解析器;仅 AUX 允许 |
| 6 | credit 只作用于 sample/record;state 豁免 | state 本就允许合并,流控无意义 |
| 7 | 参数默认内存存储,持久化交给业务 | 底座零业务;掉电策略属于安全相关业务决策 |
| 8 | 视频从 v1 移除,后续用 HTTP 单独定义 | 编码格式未定,MJPEG/fMP4+MSE 选型不影响主协议 |
| 9 | 业务命令统一走 `/commands/{ns}/{action}` | 底座路由表固定,业务扩展只动注册表 |
| 10 | 容量默认值 8 MiB 帧 / 1 MiB 窗口 / 4、8 MiB 流缓冲 / 64 MiB 队列 | 给实现共同基线,可用 limits 覆盖 |

---

## 16. 后续版本候选(v1 不做)

- **视频/音频**:走 HTTP。预览优先 MJPEG(`multipart/x-mixed-replace`,
  `<img>` 原生显示);若源是 H.264/H.265,再考虑 fMP4 + MSE 或 WebRTC。
  具体格式与端点由后续文档定义,v1 只保证不与主协议冲突。
- 多客户端与主从/旁观角色;
- 跨总线时间同步(PTP/IRIG-B)与统一时间轴;
- 服务端录制与断线补发/回放;
- 文件与固件传输(HTTP 天然适合,后续加资源即可);
- 压缩、TLS 强制化、鉴权与审计;
- 流控升级为按流优先级与带宽配额。

---
