# Edge 自动化调试脚本

基于 Python 标准库，直接使用 EdgeDriver / CDP，不依赖 Selenium。
适用于任意实现 v1 协议的页面（当前底座自带一个通用状态页）。

## 依赖

- Edge：`C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe`
- EdgeDriver：`3rdparty\edgedriver_win64\msedgedriver.exe`
- 两者版本必须一致（用 `--version` 核对）。

## 启动

```bat
:: 服务端（自动化专用端口，避免和日常端口抢 1:1 会话）
cd build
v1_server.exe 8780

:: EdgeDriver
3rdparty\edgedriver_win64\msedgedriver.exe --port=9515
```

## 脚本

| 文件 | 模式 | 作用 |
|---|---|---|
| `edge_auto.py` | 无头 | 打开页面，检查连接、Capabilities、Modules、Streams、日志并截图 |
| `edge_headed.py` | 有头 + DevTools | 打开可见 Edge 和 DevTools，保持 60 秒 |
| `cdp_breakpoint.py` | CDP | 在 `app.js` 的 `log()` 下断点，暂停、查看变量、慢速单步、截图 |

### 无头冒烟

```bash
python -I tools/edge_webdriver/edge_auto.py
```

实测输出（底座无业务模块）：

```text
title: v1 通讯底座
status: sid-...
capabilities: { "caps": [ "credit", "frag", "jobs", "blobs" ... ] }
modules: { "modules": [] }
streams: { "streams": [] }
log: ... core.state ...
screenshot: temp/edge_auto.png
```

### 有头 + DevTools

```bash
python -I tools/edge_webdriver/edge_headed.py
```

会打开可见 Edge 窗口和 DevTools，保持 60 秒。

### CDP 断点

先独立启动 Edge（不需要 EdgeDriver）：

```bat
msedge.exe --remote-debugging-port=9222 ^
  --user-data-dir=D:\trae_workspace\my_ws_server_test\temp\cdp-base-profile ^
  --auto-open-devtools-for-tabs --no-first-run --no-proxy-server ^
  --window-size=1400,900 http://127.0.0.1:8780/
```

```bash
python -I tools/edge_webdriver/cdp_breakpoint.py
```

脚本在 `app.js` 第 8 行（0-based 7）的 `log()` 内下断点，然后执行
`log('CDP breakpoint demo')`。DevTools 会停在该行，Call Stack 显示
`log`，Scope 里能看到 `text = "CDP breakpoint demo"`。每步停 6 秒，
并保存 `temp/cdp_paused.png` 和桌面截图 `temp/cdp_devtools.png`。

## 说明

- WebDriver 能打开 DevTools、操作页面、向 Console 写日志；
- 要程序化控制 Debugger/Network/Console，需走 CDP；
- 服务端 1:1 会话被占用时 HTTP 返回 `409`，换独立端口或重启服务端。
