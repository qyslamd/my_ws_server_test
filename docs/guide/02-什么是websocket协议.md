# 什么是 WebSocket 协议

> [!info] `http`协议是一问一答的，客户端每次发起`http`请求之后，服务端响应之后，这个连接就关闭了。如果服务器有消息要主动推送给客户端是做不到的。
> HTTP/1.1 的**长连接（Keep-Alive）** 虽然复用了 TCP 连接，但**通信模式依然是请求-响应**，服务器无法主动发起消息。这与 WebSocket 的全双工推送能力有本质区别。

>[!important] WebSocket 是一种在单个 TCP 连接上进行**全双工通信**的协议。它于 2011 年被定为标准 RFC 6455，核心价值在于解决了传统 HTTP 协议无法由服务器主动向客户端推送数据的问题。 
> **协议前缀**：`ws://`或者`wss://`

## 为什么要弄websocket协议？
在 WebSocket 出现前，要实现“实时更新”，主流方案是**轮询（Polling）**：客户端每隔几秒就发一次 HTTP 请求问服务器“有新数据吗？”。这种方式的问题很明显：
- **开销巨大**：每次 HTTP 请求都携带完整的头部（User-Agent、Cookie 等），而有效数据可能只有几个字节，造成带宽浪费。
- **实时性差**：数据更新有延迟，最长可达一个轮询间隔。
-** 服务器无法主动**：本质上仍是“客户端拉取”，服务器没有“推送”的能力

WebSocket 改变了这个模式：**只需一次 HTTP 握手，就能将连接“升级”为全双工通道**，之后服务器和客户端都可以随时主动发送数据，实现了真正的实时双向通信。

## websocket如何连接的？
WebSocket 连接的起点巧妙地借用了 HTTP，以确保能穿过防火墙和代理。过程如下：
1. **客户端发起 HTTP 请求**，但携带特殊的头部，请求“升级”协议：
```http
GET /chat HTTP/1.1
Host: example.com
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```
`Sec-WebSocket-Key` 是一个随机值，用于防止缓存代理错误地复用连接。
2. **服务器同意升级**，返回状态码 `101 Switching Protocols`：
```http
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```
`Sec-WebSocket-Accept` 是根据客户端的 Key 计算得出的，用于确认服务器理解了 WebSocket 协议。

握手完成后，**底层的 TCP 连接保持不变，但通信协议已从 HTTP 切换为 WebSocket**，后续数据便不再遵循 HTTP 的请求-响应模式。

## webscoket的数据如何传输的？
WebSocket 传输数据的基本单位是**帧（Frame）**。一个消息可以由一个或多个帧组成。
相比 HTTP，WebSocket 帧的头部开销极小。在**客户端到服务器方向，最小的帧头只有 6 字节**（2 字节基础头 + 4 字节掩码），远小于 HTTP 每次请求的完整头部。这种轻量设计是 WebSocket 适合高频通信的关键。

帧只有两种类型：
- **数据帧**：承载文本（Text）或二进制（Binary）数据
- **控制帧**：用于连接管理，如 Ping、Pong（心跳保活）和 Close（关闭连接）

## 帧格式：协议的最小单位


## 典型的应用场景
WebSocket 适用于**需要服务器主动、高频、低延迟推送数据**的场景：
- **即时通讯与聊天**：消息的实时送达和已读回执。
- **实时协作**：多人文档编辑、在线白板，需要毫秒级同步光标和操作
- **金融与行情**：股票报价、加密货币价格的实时变动推送。
- **在线游戏**：游戏状态和玩家操作的实时同步。
- **直播互动**：弹幕、点赞、礼物等实时互动消息。
- **IoT 与远程监控**：设备状态实时上报和指令下发。
