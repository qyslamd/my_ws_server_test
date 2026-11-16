# 自动化调试指南

> 适用于本仓库的 `v1_server` 与 `www/v1` 通用状态页。
> 覆盖：WebDriver、有头 Edge + DevTools、CDP JS 断点、LLDB、CDB。
> 以下命令均在本机实际走通。

---

## 1. 工具与版本

| 工具 | 路径 / 版本 | 用途 |
|---|---|---|
| VS2019 x64 | `vcvars64.bat` | 编译 |
| CMake + Ninja | 4.x / 1.10+ | 构建 |
| Python | 3.12 | 自动化脚本（标准库） |
| Edge | `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe` | 浏览器 |
| EdgeDriver | `3rdparty\edgedriver_win64\msedgedriver.exe` | WebDriver |
| LLDB | `D:\Qt22\Tools\QtCreator\bin\clang\bin\lldb.exe` | C++ 调试 |
| CDB | `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe` | C++ 调试（MSVC PDB 更完整） |

版本核对：

```bat
msedge --version
3rdparty\edgedriver_win64\msedgedriver.exe --version
```

两者必须一致。

---

## 2. 构建

### 2.1 RelWithDebInfo

```bat
build.bat
```

产物：`build\v1_server.exe`、`build\v1_server.pdb`。

### 2.2 Debug

```bat
tools\build_debug.bat
```

产物：`build-debug\v1_server.exe`、`build-debug\v1_server.pdb`。

> 构建前先停掉已运行的 `v1_server.exe`，否则链接会报
> `LNK1104: 无法打开 v1_server.exe`。

---

## 3. 端口

| 端口 | 用途 |
|---|---|
| 8770 | 日常/默认 |
| 8780 | 自动化专用 |
| 9515 | EdgeDriver |
| 9222 | Edge CDP |

v1 是 1:1 会话；自动化建议用 8780 独立服务端，避免和 8770 抢会话。
会话占用时 HTTP 返回 `409 Conflict`，重启服务端即可清空。

```bat
cd build
v1_server.exe 8780
```

---

## 4. WebDriver

### 4.1 启动

```bat
3rdparty\edgedriver_win64\msedgedriver.exe --port=9515
```

### 4.2 无头冒烟

```bash
python -I tools/edge_webdriver/edge_auto.py
```

脚本会：

- 创建无头 Edge 会话；
- 打开 `http://127.0.0.1:8780/`；
- 等 WS 连接；
- 读取 `#status`、`#capabilities`、`#modules`、`#streams`、`#log`；
- 截图 `temp/edge_auto.png`。

底座无业务模块时的实测输出：

```text
title: v1 通讯底座
status: sid-...
capabilities: { "caps": [ "credit", "frag", "jobs", "blobs" ... ] }
modules: { "modules": [] }
streams: { "streams": [] }
log: ... core.state ...
```

### 4.3 有头 + DevTools

```bash
python -I tools/edge_webdriver/edge_headed.py
```

去掉 `--headless`，加：

```text
--auto-open-devtools-for-tabs
```

会打开可见 Edge 窗口，DevTools 自动停靠，保持 60 秒。

### 4.4 页面参数

当前通用页支持：

| 参数 | 作用 |
|---|---|
| `?noconnect=1` | 不建立 WS，只渲染静态布局 |

---

## 5. CDP：在 DevTools 里断点调试 JS

WebDriver 不能直接点 DevTools 界面；CDP 可以程序化控制 Debugger。

### 5.1 启动带 CDP 的 Edge

```bat
msedge.exe --remote-debugging-port=9222 ^
  --user-data-dir=D:\trae_workspace\my_ws_server_test\temp\cdp-base-profile ^
  --auto-open-devtools-for-tabs --no-first-run --no-proxy-server ^
  --window-size=1400,900 http://127.0.0.1:8780/
```

确认：

```bash
curl http://127.0.0.1:9222/json
```

### 5.2 运行断点脚本

```bash
python -I tools/edge_webdriver/cdp_breakpoint.py
```

脚本流程：

1. 从 `/json` 找 `http://127.0.0.1:8780/` 页面 target；
2. WebSocket 连 `webSocketDebuggerUrl`；
3. `Debugger.enable`；
4. `Debugger.setBreakpointByUrl` 在 `app.js` 第 8 行
   （0-based 7）的 `log()` 内；
5. 执行 `Runtime.evaluate("log('CDP breakpoint demo')")`；
6. 等 `Debugger.paused`，打印调用栈与 `text`；
7. `Debugger.stepOver` ×2，每步停 6 秒；
8. 截图 `temp/cdp_paused.png`、桌面 `temp/cdp_devtools.png`；
9. 移除断点、恢复，保持 30 秒。

实测输出：

```text
--- hit: paused (other) ---
  #0 log @ 7
  #1 (anonymous) @ 0
  locals: {"text":"CDP breakpoint demo"}

--- step 1: paused (step) --- #0 log @ 8
--- step 2: paused (step) --- #0 log @ 9
```

DevTools 会停在 `app.js` 第 8 行，Scope 里能看到 `text`。

### 5.3 调整

- 断点行：`cdp_breakpoint.py` 的 `BREAK_LINE`（0-based）；
- 观察时长：`time.sleep(6)`；
- 想断其它位置：改 `setBreakpointByUrl` 的 `url`/`lineNumber`，
  或用 `Debugger.setBreakpointOnFunctionCall`。

---

## 6. LLDB

### 6.1 命令

```bash
cd build-debug
lldb v1_server.exe
(lldb) breakpoint set --file server.cpp --line 114
(lldb) run 8773
(lldb) bt
(lldb) frame variable
(lldb) next
```

实测：

- 文件/行断点命中，frame 0 是 `v1::V1Server::RegisterModule`；
- `frame variable` 能看到 `this`、`module`；
- `next` 单步正常；
- 按 C++ 方法名（`breakpoint set --name 'v1::V1Server::RegisterModule'`）
  在 LLDB 21.1.2 读 MSVC PDB 时无法解析，即使 Debug 版也一样。

结论：LLDB 用**文件/行断点**。

---

## 7. CDB

CDB 原生支持 MSVC PDB，方法名断点和局部变量完整。

### 7.1 方法名断点

```bat
cd build-debug
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" ^
  -y "D:\trae_workspace\my_ws_server_test\build-debug" ^
  -c ".reload /f v1_server.exe; bp v1_server!v1::V1Server::RegisterModule; g; k; dv; q" ^
  "D:\trae_workspace\my_ws_server_test\build-debug\v1_server.exe" 8773
```

实测输出：

```text
Breakpoint 0 hit
v1_server!v1::V1Server::RegisterModule
v1_server!main+0x13b
...
this   = 0x...
module = 0x...
```

关键点：

- `-y build-debug`：符号路径；
- `.reload /f v1_server.exe`：加载私有 PDB；
- `bp v1_server!v1::V1Server::RegisterModule`：模块名限定的方法断点；
- `g` 运行，`k` 栈，`dv` 局部变量，`q` 退出。

### 7.2 源码行断点

```bat
cdb -y "D:\trae_workspace\my_ws_server_test\build-debug" ^
  -c ".reload /f v1_server.exe; bp `server.cpp:114`; g; k; q" ^
  "D:\trae_workspace\my_ws_server_test\build-debug\v1_server.exe" 8773
```

### 7.3 常用命令

| 命令 | 作用 |
|---|---|
| `bp v1_server!ns::Class::Method` | 方法断点 |
| ``bp `server.cpp:114` `` | 源码行断点 |
| `g` | 继续 |
| `k` | 调用栈 |
| `dv` | 当前帧局部变量 |
| `p variable` | 打印变量 |
| `t` / `p` | 单步进入 / 跳过 |
| `q` | 退出 |

### 7.4 为什么 CDB 行、LLDB 不行

CDB 直接读 MSVC PDB 私有符号，方法名解析完整；LLDB 的 Windows PDB
支持不完整，类方法名常解析不到。两者都能用文件/行断点，但 CDB 的
方法名和局部变量更可靠。

---

## 8. 故障排查

| 现象 | 处理 |
|---|---|
| `LNK1104: 无法打开 v1_server.exe` | 旧服务端占用 exe，先停进程再构建 |
| EdgeDriver 启动失败 | 驱动与 Edge 版本不一致，核对 `--version` |
| `409 session busy` | 1:1 会话被占用；换 8780 或重启服务端 |
| CDP `/json` 404 | Edge 没带 `--remote-debugging-port=9222` |
| DevTools 没自动打开 | 加 `--auto-open-devtools-for-tabs` |
| LLDB 方法名断点 pending | 用文件/行断点，或换 CDB |
| CDB `bp` 无法解析 | 加 `-y` 和 `.reload /f`，方法名用模块限定 |
| headless 截图挂起 | 用 `?noconnect=1` 只渲染布局 |

---

## 9. 产物

```text
tools/build_debug.bat
tools/edge_webdriver/
  README.md
  edge_auto.py
  edge_headed.py
  cdp_breakpoint.py

temp/
  edge_auto.png
  cdp_paused.png
  cdp_devtools.png
```

`temp/` 为运行期产物，不入库。
