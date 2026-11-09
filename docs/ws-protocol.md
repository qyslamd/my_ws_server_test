# WebSocket 通信协议规范 v1.0

> 适用：工控上位机 / CAE 前后端分离架构
> 参考：MIT EngSketchPad (ESP) 的 wvServer 双通道设计
> 传输层：RFC 6455 WebSocket

---

## 1. 设计原则

1. **按方向拆通道，不按消息类型拆**：大数据只下行，控制消息双向，两条物理连接互不阻塞。
2. **二进制帧自描述**：固定头 + 位掩码 + 长度前缀数据块，前端按位读块，不依赖完整 schema。
3. **控制消息极简**：前端主动发的只是低频命令（启动/停止/改参/查询），不做复杂 RPC 队列。
4. **后端数据驱动**：计算/采集结果主动推送，前端被动渲染；前端不轮询。

---

## 2. 连接模型

前端与后端建立 **两个独立的 WebSocket 连接**：

| 连接 | URL | 子协议 | 方向 | 用途 |
|---|---|---|---|---|
| 二进制 | `ws://127.0.0.1:<port>/stream` | `wv.binary` | 后端 → 前端（**单向**） | 采样数据、几何、波形、点云 |
| 文本 | `ws://127.0.0.1:<port>/control` | `wv.text` | 前端 ↔ 后端（**双向**） | 命令、状态、心跳、告警 |

浏览器侧建立：

```js
const wsBin = new WebSocket('ws://127.0.0.1:8765/stream', ['wv.binary']);
wsBin.binaryType = 'arraybuffer';

const wsTxt = new WebSocket('ws://127.0.0.1:8765/control', ['wv.text']);
服务端在 Sec-WebSocket-Protocol 响应头中回选对应子协议；若客户端请求的子协议不在支持列表，服务端返回 400 拒绝升级。

二进制通道的 RECEIVE 回调为防御性空实现：前端在本通道上发送的任何帧一律丢弃，不处理。

3. 文本通道（wv.text）
3.1 消息信封
所有文本消息为 UTF-8 JSON 对象，单行，字段如下：

json
{
  "v": 1,
  "type": "cmd | ack | evt | err",
  "id": "c-0001",
  "name": "start",
  "payload": { },
  "ts": 1731234567890
}
字段	类型	必填	说明
v	int	是	协议版本，当前 1
type	string	是	cmd 命令 / ack 应答 / evt 事件 / err 错误
id	string	cmd/ack/err 必填	请求-应答关联 ID，格式 c-<序号>
name	string	cmd/evt/err 必填	命令名 / 事件名 / 错误类别
payload	object	否	业务载荷，缺省为 {}
ts	int64	是	毫秒时间戳（UTC）
3.2 命令表（前端 → 后端，type=cmd）
name	payload 示例	语义
hello	{"proto":1,"client":"web/1.0.0","caps":["stream","snapshot"]}	握手，交换能力
start	{"streams":["temp","pressure"]}	启动采集/计算
stop	{"streams":["temp"]}	停止指定流
set_param	{"key":"sample_rate","value":1000}	修改运行参数
query_state	{}	查询后端全局状态
subscribe	{"streams":["temp"]}	订阅数据流（后端开始向二进制通道推送）
unsubscribe	{"streams":["temp"]}	退订
ping	{}	应用层心跳
3.3 应答与事件（后端 → 前端）
ack（对 cmd 的应答）

json
{"v":1,"type":"ack","id":"c-0001","name":"start","payload":{"ok":true},"ts":1731234567891}
evt（后端主动推送，无需 id）

json
{"v":1,"type":"evt","name":"state","payload":{"running":true,"streams":2},"ts":1731234567892}
{"v":1,"type":"evt","name":"alarm","payload":{"level":"warn","code":2101,"msg":"温度超限"},"ts":...}
err（命令失败）

json
{"v":1,"type":"err","id":"c-0001","name":"start","payload":{"code":2003,"msg":"stream not found"},"ts":...}
3.4 握手流程
text
前端 → 后端  {"v":1,"type":"cmd","id":"c-0000","name":"hello","payload":{"proto":1,"caps":["stream","snapshot","batch"]}}
后端 → 前端  {"v":1,"type":"ack","id":"c-0000","name":"hello","payload":{"proto":1,"server":"svc/1.0.0","caps":["stream","snapshot"],"session":"s-7f3a"}}
后端只返回双方都支持的能力交集；

版本不兼容时后端回 err 并关闭连接；

未握手前，除 hello / ping 外的一切命令被拒绝（err code=1002）。

3.5 心跳
前端每 5 秒 发 {"type":"cmd","name":"ping"}；

后端立即回 ack；

前端 15 秒 未收到任何文本消息即判定断线，主动重连并重走握手 + 重新 subscribe；

重连后后端推送一次 全量快照（见 4.3 opcode=0x01），不补发历史增量。

4. 二进制通道（wv.binary）
4.1 帧结构总览
text
┌──────────── 固定头 12 字节 ────────────┬────── 数据块 0..N ──────┐
│ magic | ver | opcode | flags | streamId | seq │ [len][data] ... │
└────────────────────────────────────────┴─────────────────────────┘
固定头：

偏移	字段	类型	说明
0	magic	uint8	固定 0xA5，帧起始校验
1	version	uint8	协议版本，当前 0x01
2	opcode	uint8	帧类型，见 4.2
3	flags	uint8	数据块存在位掩码，见 4.2
4	streamId	uint32 (LE)	数据流 ID，后端分配
8	seq	uint32 (LE)	流内递增序号，用于丢包检测
固定头之后，按 flags 的 bit 从低到高 顺序，依次排列存在的每个数据块。每个数据块格式：

text
[len: uint32 LE][data: len bytes]
len 为 0 表示该块存在但为空，前端应跳过。

4.2 枚举定义
opcode（帧类型）

值	名称	语义
0x01	SNAPSHOT	全量快照（订阅建立或重连后首帧）
0x02	DELTA	增量数据（常规推送）
0x03	CONTROL	控制回执数据（如命令执行结果附带的采样）
0x04	ERROR	二进制通道级错误
0x05	HEARTBEAT	二进制通道保活（无数据块）
flags（数据块位掩码）

bit	掩码	名称	数据内容
0	0x01	DATA	主数据块：采样数组 / 顶点数组（float32 或 int16，由 META 声明）
1	0x02	INDEX	索引块：uint16 或 uint32
2	0x04	COLOR	颜色块：uint8 RGB 或 RGBA
3	0x08	NORMAL	法线块：float32 xyz
4	0x10	META	元数据块：UTF-8 JSON，描述本帧各块的数据类型、维度、单位
5	0x20	TS	时间戳块：uint64 LE 纳秒时间戳（数据采集时刻）
未定义的 bit 必须为 0。接收端忽略不认识的 bit 对应的数据块（向前兼容）。

4.3 META 块约定
META 为 UTF-8 JSON，是本帧的自描述头。示例：

json
{
  "kind": "waveform",
  "dataType": "float32",
  "components": 1,
  "count": 1024,
  "unit": "V",
  "sampleRate": 1000
}
字段	说明
kind	数据种类：waveform / pointcloud / geometry / scalar
dataType	float32 / float64 / int16 / int32 / uint8
components	每采样点数（波形 1，点云 3，几何顶点 3）
count	采样点/元素总数
unit	物理单位（可选）
sampleRate	采样率 Hz（波形类必填）
4.4 字节级示例：一条 1024 点 float32 波形
text
A5 01 02 31 01 00 00 00 2A 00 00 00   ; 头: magic/ver/opcode=DELTA/flags=0x31(DATA|META|TS), streamId=1, seq=42
00 04 00 00                            ; DATA 块 len = 1024 bytes
<1024 字节 float32 采样>               ; 4 * 256 点 或 1 * 1024 点，由 META 解释
5A 00 00 00                            ; META 块 len = 90
<90 字节 UTF-8 JSON>                   ; {"kind":"waveform","dataType":"float32",...}
08 00 00 00                            ; TS 块 len = 8
<8 字节 uint64 LE 纳秒时间戳>
4.5 前端解析骨架（JS）
js
wsBin.onmessage = (ev) => {
  const buf = ev.data;                    // ArrayBuffer
  const dv = new DataView(buf);
  if (dv.getUint8(0) !== 0xA5) return;    // magic 校验
  const ver    = dv.getUint8(1);
  const opcode = dv.getUint8(2);
  const flags  = dv.getUint8(3);
  const stream = dv.getUint32(4, true);
  const seq    = dv.getUint32(8, true);

  let off = 12;
  const blocks = {};
  const order = ['DATA','INDEX','COLOR','NORMAL','META','TS'];
  for (let i = 0; i < order.length; i++) {
    if (!(flags & (1 << i))) continue;
    const len = dv.getUint32(off, true); off += 4;
    blocks[order[i]] = buf.slice(off, off + len); off += len;
  }

  if (blocks.META) {
    const meta = JSON.parse(new TextDecoder().decode(blocks.META));
    // 按 meta.kind 分发到对应的渲染器
  }
  // 用 blocks.DATA / blocks.TS 更新视图
};
4.6 后端发送骨架（C++ / Mongoose）
cpp
void sendWaveform(mg_connection *c, uint32_t streamId, uint32_t seq,
                  const float *samples, size_t n, uint64_t tsNs,
                  const std::string &metaJson) {
  size_t metaLen = metaJson.size();
  size_t dataLen = n * sizeof(float);
  size_t total = 12 + 4 + dataLen + 4 + metaLen + 4 + 8;
  std::vector<uint8_t> f(total);
  uint8_t *p = f.data();

  p[0] = 0xA5; p[1] = 0x01; p[2] = 0x02;
  p[3] = 0x01 | 0x10 | 0x20;               // DATA | META | TS
  memcpy(p + 4, &streamId, 4);
  memcpy(p + 8, &seq, 4);
  p += 12;

  memcpy(p, &(uint32_t){ (uint32_t)dataLen }, 4); p += 4;
  memcpy(p, samples, dataLen);                    p += dataLen;

  memcpy(p, &(uint32_t){ (uint32_t)metaLen }, 4); p += 4;
  memcpy(p, metaJson.data(), metaLen);            p += metaLen;

  memcpy(p, &(uint32_t){ 8 }, 4); p += 4;
  memcpy(p, &tsNs, 8);

  mg_ws_send(c, f.data(), f.size(), WEBSOCKET_OP_BINARY);
}
5. 错误码
区间	类别	示例
1xxx	协议层	1001 非法 JSON / 1002 未握手 / 1003 缺字段 / 1004 版本不支持 / 1005 未知命令
2xxx	业务层	2001 权限不足 / 2002 资源忙 / 2003 流不存在 / 2004 参数越界
3xxx	系统层	3001 内部异常 / 3002 超时 / 3003 依赖不可用
协议层错误：前端展示并断开重连；

业务层错误：前端提示用户，连接保持；

系统层错误：前端降级 UI，上报日志。

6. 版本演进
v 字段只在不兼容变更时递增（如信封结构、握手流程）；

新增命令 / 新增 opcode / 新增 flags bit 属于兼容变更，不改 v；

接收端遇到未知 opcode 或未知 flags bit：跳过该帧 / 该块，不报错；

握手时 caps 能力交集决定双方可用的扩展特性。

7. 附录：与 ESP 的对应关系
ESP (wvServer)	本规范	差异
PROTOCOL_GPRIM_BINARY	wv.binary 通道	保留「只下行」语义
PROTOCOL_UI_TEXT	wv.text 通道	保留「双向控制」语义
8 字节头（stripeIndex/opcode + nameLen/vflag/gtype）	12 字节头（magic/ver/opcode/flags/streamId/seq）	增加 magic、版本、流 ID、序号
vflag 位掩码	flags 位掩码	保留自描述模式，扩展 META/TS
每块 [len][data]	每块 [len][data]	完全一致
无心跳	应用层 ping/pong	工控必需
无版本协商	hello + caps	支持长期演进
text

---

### 说明几点

1. **双连接**：ESP 的 `server.c` 注册了两个 libwebsockets 协议，浏览器端实际是开两条 WS 连接分别协商。文档沿用了这个模型，没有用单连接 + 消息头分流，因为前者更彻底地隔离了两类流量。
2. **12 字节头**：比 ESP 的 8 字节多了 `magic`（帧校验）、`version`（协议演进）、`streamId`+`seq`（多流并存 + 丢包检测），这对工控多传感器场景是必要的。
3. **META 块**：ESP 靠 `gtype` + `vflag` 硬编码解释数据，工控数据类型更多变，所以把解释权交给一个 JSON META 块，前端按 `meta.kind` 分发。