# TODO

> 项目待办与设计储备。当前代码刻意保持"易于理解的 demo"形态,
> 以下重构在痛点真实出现前不实施(避免新手困惑)。

---

## [储备] 传输抽象层:封装通讯层,后端 WS 库可换

**状态**:设计已定稿,暂不实施
**提出**:2026-11-10(完成双目标模块化 + 服务端类化之后)

### 为什么现在不做

当前 `Mongoose_Server` / `LWS_Server` 两个类直白易读,每个文件从上到下
就是"连接→升级→收发→推流"一条线,适合团队新手理解 WebSocket 服务端
的本质。提前抽象会引入一层间接(接口/回调/不透明句柄),读代码要先理解
接口再看实现——对 demo 阶段是净负担。

### 目标架构(三层)

```
┌─────────────────────────────────────────────┐
│ common/protocol.{h,cpp}   ← 业务逻辑,唯一一份 │
│   帧构造/解析、命令分发、推流节流、心跳           │
│   (现在两份拷贝的 build_waveform_frame、        │
│    control 分发等全部坍缩到这里)                │
├─────────────────────────────────────────────┤
│ transport.h               ← 传输抽象接口      │
│   start / run / send_text / send_binary /     │
│   broadcast_binary / on_open|close|text|binary │
├──────────────┬──────────────────────────────┤
│ transport_mongoose │ transport_lws           │
│ (路径路由→channel) │ (子协议路由→channel)     │
├──────────────┴──────────────────────────────┤
│ main.cpp ×2,每个 ~10 行,只做组装               │
└─────────────────────────────────────────────┘
```

### 接口关键决策(已议定)

1. **双通道语义归一**:mongoose 按 URL 路径路由(/stream、/control),
   lws 按子协议名路由。抽象层定义 `Channel::Stream / Channel::Control`
   枚举,路由差异吃在各实现内部,业务层只见通道名。
2. **HTTP 静态文件不进接口**:两边都是库内部机制(`mg_http_serve_dir` /
   `lws_http_mount`),各实现在 GET 兜底里处理 `www/` 目录。
3. **节流放业务层,不放传输层**:lws 教训(WRITEABLE 触发频率不受
   arm 频率约束,实测 8250 帧/s)证明节流是业务节奏(100Hz),应由
   `ProtocolCore::tick()` 驱动并调 `broadcast_binary`。
4. **ConnHandle 用不透明指针**(void*/uintptr_t):mongoose 的
   `mg_connection*` 与 lws 的 `lws*` 对业务层只是"连接标识"。

### 真正的收益(不只是换库)

- **协议逻辑单份化**:两份拷贝的组帧/分发代码坍缩进 common/,
  消除"改一处漏一处"的分叉风险
- **可测试性**:协议层可用 mock Transport 单测(帧格式、命令分发、
  节流),无需起 socket——当前"PS 全套验证"本质是人肉做这件事

### 代价(已知)

- `send_binary` 下 lws 实现需处理 LWS_PRE 前置缓冲(一次拷贝,
  100Hz 下无感)
- 一次性重构量:中等(现有两类 ~80% 代码可直接平移)

### 何时启动(触发条件,满足其一)

- [ ] 协议规范趋于稳定(v1 冻结),开始补自动化测试
- [ ] 需要第三个传输后端(如 uWebSockets / WebSocket++ / 进程内 mock)
- [ ] 协议层 bug 出现"修一个库漏另一个库"的分叉
- [ ] 团队对两个 demo 的理解已普遍到位

---

## [评审遗留] 已知小问题(重构前后都可做)

- [ ] lws 版 control 通道:命令分发 + id 回显(现在硬编码 hello ack;
      mongoose 版 handle_control_text 是参照实现)
- [ ] lws 版波形时间基 time(NULL) 秒级精度(波形每秒冻结约 1s;
      改毫秒时钟,如 GetTickCount64/lws_now_usecs)
- [ ] lws 版多客户端推流(单一 stream_wsi_ 成员,后连者抢走推送目标;
      可用 lws_callback_on_writable_all_protocol)
- [ ] 两版共用协议层后(见上),删除各自残留的重复实现


## 协议改进记录
```txt
A. 实现协议里的“握手鉴权”与“订阅机制”：把现在“无脑广播”的代码改掉，只有发过 subscribe 的客户端，才能收到数据，不订阅的连一个字节都收不到。
B. 引入 cJSON 完善 /control 的状态机：严格按文档 3.2 和 5.1 实现，处理 start / stop / set_param 指令，如果参数不合法，按你的错误码返回 err。
C. 优化二进制帧（实现 SNAPSHOT 与 DELTA 分离）：落实上面提到的“META 只在 SNAPSHOT 发一次”的优化。
```