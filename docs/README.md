---
title: docs 目录索引
aliases:
  - docs 索引
tags:
  - 文档/索引
created: 2026-11-16
status: active
---

# docs 目录索引

各目录内的文件名前缀是**阅读顺序**，不是版本号：先读 01，再读 02。

| 位置 | 放什么 | 内容 |
|---|---|---|
| `spec/` | 协议规范（活文档，跟代码演进） | v0 背景、v1 协议正文、落地检查清单 |
| `guide/` | 入门与操作指南 | HTTP/WebSocket/CEF 入门、205 web 主文档、旧项目梳理、CEF 使用、自动化调试 |
| `plan/` | 方案与储备（定稿但未实施，或设计记录） | 传输层抽象、CEF 客户端、web/Qt 双前端 |
| `todo.md` | 待办 | 项目待办与设计储备 |

## 文件清单

```text
spec/
  01-ws-protocol.md             v0 demo 规范（存档，先读它了解背景）
  02-ws-protocol-v1.md          v1 协议正文（设计稿，唯一正文）
guide/
  01-什么是http协议.md            HTTP/REST 入门：资源、方法、状态码
  02-什么是websocket协议.md       WebSocket 入门：为什么要它、怎么握手、帧怎么传
  03-什么是CEF.md                CEF 入门：多进程模型、关键接口
  04-eph205-web.md              EPH205 Web 版主文档（结构/API/运行/验证）
  05-YiGuai-项目梳理.md          旧 Qt 项目里嵌 CEF 的可复用经验
  06-在CEF中使用当前的205web.md   CEF 109 里跑当前 web 的验证记录
  07-automation-debugging.md    自动化调试通用指南（EdgeDriver/CEF）
plan/
  01-v1-传输层抽象方案.md         协议逻辑与通讯库解耦（定稿暂不实施）
  02-CEF客户端方案.md            Win32 + CEF 109 桌面客户端设计
  03-web与Qt双前端策略.md        零 Qt 核心 + 双前端的需求与实现路径
todo.md                        项目待办与设计储备
```

维护约定：新文档先想清楚归哪类再放；同一目录内按 01、02 递增命名，编号即阅读
顺序；协议相关改动同步更新 `spec/` 下的活文档（02 正文、03 检查单）；已实施的
方案留在 `plan/` 作为设计记录，不迁去 guide。

## 仓库顶层（页面归各自的 app，不设顶层 www）

```text
src/framework/v1/      基础通讯框架（不含设备代码）
src/apps/              一个目录一个产品
  eph_205/             EPH205：module/ + app/ + e2e/ + packaging/
  eph_666/             第二个产品的骨架
src/common/cef_shell/  CEF 窗口外壳静态库（业务无关）
src/common/log/        日志门面
src/examples/          mongoose_c、mongoose_cpp、cpp_httplib 三个 demo
src/tests/             单元测试
docs/                  本文档（spec/guide/plan/lesson，待办在 todo.md）
tools/                 通用构建、调试小工具、端到端调度（run_all.py）
3rdparty/              mongoose、sqlite、spdlog、googletest 等（cef 不入库）
```

静态页不属于"仓库级资源"，而属于提供它的那个程序：`src/apps/eph_205/eph205_module/www` 由
通讯层提供（构建时复制到构建目录的 `www/`），CEF 外壳直接复制它，
`src/examples/mongoose_cpp/www` 只服务那个 demo，复制到构建目录的 `www-demo/`。
