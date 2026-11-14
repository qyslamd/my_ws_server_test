# ESP（Engineering Sketch Pad Web 前端）

## 来源

| 项 | 内容 |
|---|---|
| 上游 | EngSketchPad（The Engineering Sketch Pad） |
| 本地来源 | `3rdparty/EngSketchPad-master/ESP`（该目录不入库） |
| 文件日期 | 2012-10-20（发行包时间戳） |
| 内容 | ESP 的 Web 前端源码：HTML / JS / CSS + `WebViewer/` |
| 文件数 / 体积 | 10 个 / 约 324 KB |

## 目录说明

```text
ESP/
├── ESP.html / ESP.css / ESP.js   主界面与交互脚本
├── ESP-addBrch.html/.js          增加分支
├── ESP-editBrch.html/.js         编辑分支
├── ESP-help.html                 帮助（说明插图已移除）
├── WebViewer/                    WebGL 三维查看器（8 个 JS）
└── PROVENANCE.md                 本文件
```

## 精简说明（相对上游）

| 处理 | 内容 | 原因 |
|---|---|---|
| 删除 | `ESP-fig01..22.png`（22 张，约 4.6 MB） | 仅需源代码；插图占体积 93% |
| 修改 | `ESP-help.html` 移除 22 处 `<img>` 引用，页首加一行说明 | 避免帮助页出现碎图 |

除此之外未改动任何源码。

## 许可

原发行包内**未包含 LICENSE / COPYING 文件**，README.txt 也未声明许可条款。
EngSketchPad 上游通常以 NASA/AFRL 开源方式发布，但此处无法从本地文件确认。

**使用前须确认许可**：若用于对外交付，请从上游仓库获取正式许可声明，
或在内部仅作参考/演示用途，避免闭源分发。

## 接入方式

- 纯静态资源，不参与 CMake 构建。
- 需要使用时复制到 `www/` 下对应目录（如 `www/vendor/esp/`），加载 `ESP.html`。
- 与底座通过 WebSocket 通信（`WebViewer/wv-socket.js`），端口在页面输入框填写。
