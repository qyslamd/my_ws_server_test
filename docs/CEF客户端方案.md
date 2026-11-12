# 用 CEF 打包 205 web 成桌面客户端：方案设计

> 状态：P0–P4 已实施（2026-11-12）
> 前提：CEF 109 已在本机编译通过，205 web 已在其中验证可用
> 约束：零 Qt；CEF 放工作区 `3rdparty/`，不入库，用 `find_package` 引入

---

## 1. 前因后果

205 的界面已经做成了纯 web（`www/v1/eph205/`），在 Edge 里跑得没问题，
在本地编译的 CEF 109 里也完整验证过：登录、连接、测量、实时读数、历史对比、
三维视图全部正常，WebGL 走硬件 ANGLE/D3D11，页面无 JS 错误。

但交付形态还是"让客户开个浏览器访问 127.0.0.1"——这对工控客户来说是减分的：

- 浏览器有地址栏、标签页、刷新、书签，看起来"不像个设备软件"；
- 客户会担心"我关了浏览器程序是不是还在跑"；
- 车间机器上可能还开着别的网页，切来切去容易误操作；
- 一个 EXE 双击就能用，和"装个浏览器再输网址"是两种交付观感。

所以把同一个 web 塞进一个自己的 Win32 窗口里，做成一个标准的桌面程序：
**双击 → 出窗口 → 就是那个界面**，没有浏览器痕迹。

CEF 是这个方案里唯一合理的运行时：版本完全固定、离线可用、支持 Win7
（109 是最后一个支持 Win7 的 Chromium），而 WebView2 依赖系统 Edge 运行时，
版本随系统更新走，工控场景里不可接受。

---

## 2. 需求与约束（原始输入）

你给的要求，逐条落在这里，后面设计对着它走：

1. CEF 放到工作区的 `3rdparty/` 下，**不入库**（体积太大），用 `find_package` 引入。
2. 借鉴 `cefclient` 的实现，但**不要**它那套东西：
   - 前进/后退按钮
   - 地址栏
   - 工具栏、标签页
   - 这些对客户端没有意义。
3. 要借鉴的是它写的那些 **handler**：把"浏览器才有的行为"关掉——
   刷新、右键菜单等等，这些不该出现在客户端程序里。
4. 零 Qt。
5. 最终形态是"像一个客户端程序"，不是"打开了一个浏览器"。
6. 先出文档，评审后再写代码。

---

## 3. 总体架构

单进程、三线程。窗口和 web 界面是同一程序的两个面。

```text
EPH205.exe
├─ UI 线程（main）            Win32 消息循环 + CEF 浏览器进程
│    └─ CefBrowser ──► http://127.0.0.1:<随机端口>/eph205/?v=...
│
├─ server 线程                mongoose 事件循环
│    └─ v1 底座（HTTP/WS、会话、作业、静态文件）
│         └─ eph205 模块（命令/参数/作业/读数流）
│
├─ module worker 线程         串口事务（连接、测量、重试）
│
└─ 仿真链路线程               COM8 上的 SimulatorLink（仅调试时启动）
```

关键点：**页面和 C++ 之间不需要任何桥**。原来的架构里，页面就是通过
HTTP/WS 和服务端说话的；现在服务端和窗口在同一个进程里，这条边界不用改。
QCefView 那种 JS↔C++ 消息路由在这个方案里是**不需要**的（见 §9）。

### 3.1 必须先改的一处

现在 `main()` 里是 `server.Run()` 阻塞跑 mongoose 循环。客户端里主线程
要交给 CEF 的消息循环，所以服务端必须移到一个独立线程：

```cpp
v1::V1Server server(port);
std::thread server_thread([&]{ server.Run(); });
// ... CEF 消息循环 ...
server.Stop();              // 新增：让 Run() 退出
server_thread.join();
```

`V1Server` 需要补一个 `Stop()` 和一个 `running_` 标志（约 15 行）。
这是整个方案里唯一要动底座的地方。

### 3.2 端口

- 默认 `--port=0`：由系统分配临时端口，避免多实例、避免和调试用的 8790 冲突。
  绑定后通过 `mg_mgr` 的连接列表或 `getsockname` 取回实际端口，传给浏览器。
- 允许 `--port=8790` 覆盖，便于开发时用固定地址调试。
- 只监听 `127.0.0.1`，不对外。

---

## 4. CEF 的引入方式

### 4.1 放哪、怎么不入库

```text
D:\trae_workspace\my_ws_server_test\
  3rdparty\
    cef-109\                    ← 整个目录 gitignore
      cmake\FindCEF.cmake
      include\                  （92 个头文件）
      Release\libcef.lib        （导入库）
      Release\libcef.dll        （186 MB）
      Release\*.pak \ icudtl.dat \ snapshot_blob.bin \ locales\
      libcef_dll\               （wrapper 源码，参与编译）
```

`.gitignore` 加一条：

```gitignore
/3rdparty/cef-109/
```

体积参考（Release 运行时，实测）：

| 文件 | 大小 |
|---|---|
| libcef.dll | 185.9 MB |
| icudtl.dat | 10.0 MB |
| resources.pak | 7.2 MB |
| libGLESv2.dll | 7.1 MB |
| vk_swiftshader.dll | 4.7 MB |
| d3dcompiler_47.dll | 4.7 MB |
| chrome_elf.dll | 1.3 MB |
| 其余（pak/snapshot/locales） | ~4 MB |
| **合计** | **约 257 MB** |

### 4.2 CMake 接入

CEF 官方发行包里自带 `cmake/FindCEF.cmake`，走的就是 `find_package` 这条路：

```cmake
# 顶层 CMakeLists.txt
option(EPH205_BUILD_CLIENT "构建 CEF 桌面客户端" OFF)   # 默认关，没 CEF 也能编服务端

if(EPH205_BUILD_CLIENT)
  set(CEF_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/cef-109" CACHE PATH "CEF binary distribution")
  list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
  find_package(CEF REQUIRED)
  add_subdirectory(apps/eph205_client)
endif()
```

`apps/eph205_client/CMakeLists.txt`：

```cmake
add_executable(EPH205 WIN32
  main.cpp
  main_window.cpp
  client_handler.cpp
  # ...
)

target_include_directories(EPH205 PRIVATE ${CEF_INCLUDE_DIRS})
target_link_libraries(EPH205 PRIVATE
  libcef_lib          # FindCEF.cmake 提供的导入目标
  libcef_dll_wrapper  # 见下
  v1_base             # 通讯底座
  eph205_module       # 业务模块
)

# 运行时依赖：编译后把 CEF 的 dll/pak/locales 拷到输出目录
add_custom_command(TARGET EPH205 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CEF_BINARY_FILES} $<TARGET_FILE_DIR:EPH205>)
```

**wrapper 怎么来**：`libcef_dll_wrapper` 是 CEF 的 C++ 封装层（几十个源文件）。
两种做法：

| 做法 | 优点 | 缺点 |
|---|---|---|
| A. 每次编译 `libcef_dll/` 源码 | ABI 与我们的 CRT 完全一致，升级 CEF 只换目录 | 首次编译多 1~2 分钟 |
| B. 复用你已经编好的 `libcef_dll_wrapper.lib` | 快 | CRT/编译选项必须完全一致，换机器容易踩坑 |

**建议 A**：在 `apps/eph205_client/CMakeLists.txt` 里把 `${CEF_ROOT}/libcef_dll` 下的
`.cc` 收进一个静态库目标。源码在 3rdparty（不入库），编译规则在我们仓库里（入库）。

### 4.3 一个必须记住的点：子进程

CEF 是多进程的：渲染进程、GPU 进程都是**同一个 exe** 以不同 `--type=` 启动的。
所以 `main()` 的第一件事必须是：

```cpp
int main(int argc, char* argv[]) {
  CefMainArgs args(GetModuleHandle(nullptr));
  int exit_code = CefExecuteProcess(args, nullptr, nullptr);
  if (exit_code >= 0) return exit_code;      // 子进程在这里就退出了
  // 主进程继续
}
```

漏了这段，表现是"窗口能出来但白屏/进程反复重启"。

---

## 5. 窗口与生命周期

### 5.1 窗口

标准 Win32 窗口，**系统标题栏**：

- 标题：`EP-H205 综合测试系统`
- 图标：`.ico` 编进 exe（RC 资源）
- 尺寸：1280×800 默认，记住上次大小/位置（写注册表或 ini）
- 无菜单栏、无工具栏、无状态栏——所有功能都在页面里
- 最小尺寸限制 1024×700（页面按这个宽度做的）

**不做无边框、不自绘非客户区。** 定制 Win32 的非客户区（拖动区、最小化/关闭按钮、
高 DPI、贴边吸附、多显示器）是个独立的活，这一阶段不做；无边框窗口在工控机上
观感也更差——客户要的是"像个正常软件"，不是"像个全屏面板"。

### 5.2 启动顺序

```text
1. 单实例检查（CreateMutex）→ 已有实例则激活它并退出
2. CefExecuteProcess（子进程直接返回）
3. 解析命令行：--port --devtools --kiosk
4. 初始化 CEF（CefInitialize）
5. 启动服务端线程，拿到实际端口
6. 创建主窗口 → CreateBrowser(url)
7. 进入消息循环（GetMessage/DispatchMessage）
8. 窗口关闭 → CloseBrowser(true) → OnBeforeClose
9. 停服务端（Stop → 串口断开、作业取消）→ join
10. CefShutdown → 退出
```

第 8→9 的顺序很重要：浏览器先关，WS 自然断开，底座的 SAFE_STOP 会
把正在跑的模块停掉；然后我们再做一次显式 Stop 兜底。**关窗口 = 安全停机**，
这是这套架构白送的好处。

### 5.3 单实例

`CreateMutex(L"Global\\EPH205_SingleInstance")`，已存在则
`FindWindow` + `ShowWindow(SW_RESTORE)` + `SetForegroundWindow`，然后退出。
理由：串口是独占资源，两个窗口抢同一组 COM 口只会互相报错。

---

## 6. Handler 设计（借鉴 cefclient 的部分）

cefclient 把这些 handler 拆在 `tests/shared/browser/client_app_browser.cc`
和 `tests/cefclient/browser/client_handler*.cc` 里。我们不要它的工具栏、
地址栏、标签页，只借 handler 的骨架和下面这些开关。

| Handler | 我们的实现 | 目的 |
|---|---|---|
| `CefLifeSpanHandler` | `OnBeforePopup` 返回 true 直接拒绝；`OnBeforeClose` 通知主窗口退出 | 客户端不开新窗口 |
| `CefLoadHandler` | 记录加载状态；`OnLoadError` 显示本地错误页（"服务未启动，请重启程序"） | 不白屏 |
| `CefDisplayHandler` | 忽略页面 title 变化，标题固定；忽略 favicon | 标题栏不出现 `127.0.0.1:…` |
| `CefContextMenuHandler` | `OnBeforeContextMenu` 里 `model->Clear()` | **去掉右键菜单**（刷新/另存为/检查都在里面） |
| `CefKeyboardHandler` | 白名单式拦截（见 §7） | 去掉刷新、缩放、打印、源码 |
| `CefRequestHandler` | `OnBeforeBrowse` 只放行 `http://127.0.0.1:<port>`；`OnBeforeResourceLoad` 拒绝外部域名；`OnCertificateError` 拒绝；`OnRenderProcessTerminated` 自动重载一次 | 页面不能被导航走、不能外联 |
| `CefJSDialogHandler` | `OnJSDialog` 直接返回 true（吞掉）；或转成原生 MessageBox | 不出现浏览器样式的 alert |
| `CefPermissionHandler` | 全部拒绝（摄像头/麦克风/定位/剪贴板/通知/全屏） | 测试系统不需要 |
| `CefDragHandler` | `OnDragEnter` 返回 true | 防止把文件拖进窗口导致导航 |
| `CefDownloadHandler` | 保留：`OnBeforeDownload` 把文件落到固定目录并提示路径 | CSV 导出要用 |
| `CefDialogHandler` | 保留文件选择（`OnFileDialog` 用原生 `GetOpenFileName`） | 导入 Excel/CSV 依赖 `<input type=file>` |
| `CefRenderHandler` | 不需要（窗口模式，不用 OSR） | — |
| `CefFocusHandler` / `CefFindHandler` 等 | 不实现 | — |

**不实现的 handler 会让 CEF 走默认行为**，所以"要禁的"必须显式实现，
不能靠"不写"。

---

## 7. 禁止清单

这些是"浏览器才有、客户端不该有"的行为，逐条对应到实现位置：

| 行为 | 触发方式 | 处理 |
|---|---|---|
| 刷新 | F5 / Ctrl+R / Ctrl+Shift+R / 右键菜单 | 键盘拦截 + 清空右键菜单 |
| 前进 / 后退 | Alt+← / Alt+→ / 鼠标侧键 | `OnBeforeBrowse` 拒绝导航 |
| 地址栏 / 工具栏 / 标签页 | — | 本来就没有（不用 CEF 的 chrome） |
| 开发者工具 | F12 / Ctrl+Shift+I | 默认拦截；`--devtools` 或 Debug 构建才放行 |
| 页面缩放 | Ctrl+滚轮 / Ctrl+± / Ctrl+0 | 键盘拦截；`SetZoomLevel(0)` 锁定 |
| 打印 | Ctrl+P | 键盘拦截 |
| 另存为 / 查看源码 | Ctrl+S / Ctrl+U | 键盘拦截（右键菜单已清空） |
| 下载页面的"保存" | 右键菜单 | 同上 |
| 拖放导航 | 拖文件进窗口 | `OnDragEnter` 取消 |
| 弹窗 / 新窗口 | `target=_blank`、`window.open` | `OnBeforePopup` 返回 true |
| 外部链接 | 点击 http(s) 外链 | `OnBeforeBrowse` 拦下，用 `ShellExecute` 交给系统浏览器 |
| 首次运行提示 / 默认浏览器检测 | — | `--no-first-run --no-default-browser-check` |
| 自动填充 / 翻译气泡 | — | `--disable-features=Translate,AutofillServerCommunication` |
| 文本选择蓝框 | 鼠标拖选 | 页面 CSS `user-select:none`（表格输入框除外） |

---

## 8. 页面与服务端

### 8.1 静态资源：开发用目录，发布嵌入 exe

| 模式 | 做法 | 用途 |
|---|---|---|
| 目录 | 服务端从 `www/` 读（现状） | 开发调试，改完刷新即生效 |
| 内嵌 | `www/` 打成 RCDATA 编进 exe，服务端从内存取 | 发布：客户看不到、删不掉 |

内嵌需要给底座加一个约 80 行的 `ResourceStaticHandler`：
按请求路径查嵌入表 → 返回内容 + Content-Type。
文件总量约 7.5 MB（含 help3d 的 OBJ/MTL 4.6 MB 和 vendor 3.1 MB），
压进 exe 后 exe 约 9 MB，可接受。

**建议**：发布用内嵌；同时保留 `--www=<dir>` 参数，现场排查时指向外部目录。

### 8.2 本机其它进程不能随便驱动设备

服务端只监听 127.0.0.1，但本机任何程序都能访问它。两道可选防护：

- **令牌**：启动时生成随机 32 字节，页面 URL 带 `?t=`，前端取到后放在
  `X-App-Token` 头里；服务端校验。约 40 行。
- **进程校验**：Windows 上拿不到对端 PID（HTTP 层面），意义不大。

建议做令牌。代价是前端所有请求要带一个头，`lib/api.js` 里一处改动。

### 8.3 关窗即安全停机

浏览器关闭 → WS 断开 → 底座 `SafeStop` → 模块 `Stop()`（断串口、取消作业）。
这条路径已经在 Edge 里验证过（每次关标签页都会触发），CEF 里同样成立。

---

## 9. 为什么不需要 JS↔C++ 桥

QCefView 提供的是 `CefMessageRouter`：页面 `window.cefQuery(...)` → C++ 处理。
我们能不用就不用，理由是：

1. 现有页面已经通过 HTTP/WS 和底座通信，接口齐全；
2. 桥会把"通信模型"分裂成两套（HTTP 一套、桥一套），维护成本翻倍；
3. 桥跑在 UI 线程，长操作会卡界面，等于把已经解决的无阻塞问题又引回来。

只有一种情况需要桥：**需要在页面里调用系统能力**（打开文件管理器、
选择保存路径、读注册表、弹原生对话框）。这些如果将来出现，再加一个小而窄的桥
（四个方法以内），不铺开。

---

## 10. 打包形态

### 10.1 目录

```text
EPH205\
  EPH205.exe                 主程序（含 www 资源，~9 MB）
  libcef.dll                 186 MB
  chrome_elf.dll             1.3 MB
  libEGL.dll / libGLESv2.dll 7.1 MB（WebGL / 三维视图）
  d3dcompiler_47.dll         4.7 MB
  vk_swiftshader.dll + .json 4.7 MB（无独显时的软件渲染兜底）
  vulkan-1.dll               0.9 MB
  icudtl.dat                 10 MB
  *.pak                      8 MB
  snapshot_blob.bin 等       0.5 MB
  locales\                   只留 zh-CN.pak、en-US.pak（~1 MB）
  （运行时创建）data\        测量记录 JSONL
  （运行时创建）logs\        cef.log、程序日志
```

合计约 **255 MB**。可裁剪项（按需）：

| 项 | 省 | 代价 |
|---|---|---|
| vk_swiftshader + vulkan-1 | 5.6 MB | 无独显/驱动异常时三维视图不可用 |
| chrome_200_percent.pak | 0.9 MB | 200% DPI 下图标略糊 |
| 多余的 locales | ~3 MB | 只保留中文/英文 |

不建议裁 libGLESv2/ANGLE——三维视图就靠它。

### 10.2 分发

| 形态 | 说明 |
|---|---|
| 便携 zip | 解压即用，最省事，适合试用 |
| Inno Setup 安装包 | 开始菜单/桌面快捷方式、卸载、装到 Program Files |
| 静默安装 | 车间批量部署用 |

第一期做便携 zip + 一个简单的 Inno 脚本即可。

### 10.3 运行时目录

```text
%LOCALAPPDATA%\EPH205\
  cef-cache\     浏览器缓存（可删，几十 MB）
  logs\          cef.log / app.log
  config.ini     窗口位置、端口、上次目录
data\ 放哪？ → 记录属于业务数据，放程序目录下的 data\ 或 %PROGRAMDATA%\EPH205\
```

建议：**业务数据跟程序目录走**（便携），缓存和日志跟 `%LOCALAPPDATA%`。

---

## 11. 和 WebView2 / QCefView 的对比

| | 本方案（CEF 109） | WebView2 | QCefView |
|---|---|---|---|
| 运行时来源 | 随程序打包 257 MB | 系统 Edge 运行时 | 同 CEF |
| 版本可控 | 完全固定 | 随系统更新 | 固定 |
| 离线部署 | 拷贝即用 | 需预装运行时 | 同 CEF |
| Win7 | 支持（109 是最后一版） | 不支持 | 支持 |
| 依赖 Qt | 无 | 无 | 有 |
| 开发量 | 中（自己写窗口和 handler） | 小 | 小 |
| 观感可控 | 完全可控 | 中等（Edge 的一些行为去不掉） | 完全可控 |

结论：**离线 + 版本固定 + 零 Qt** 这三条决定了用 CEF。

---

## 12. 分阶段

| 阶段 | 内容 | 验收 |
|---|---|---|
| P0 | 3rdparty 放入 CEF、CMake `find_package` 跑通、最小窗口加载 `127.0.0.1:8790` | 窗口里能看到登录页 |
| P1 | handler 清单实现，禁止清单全部生效 | 按 F5 没反应、右键没菜单、拖文件没反应 |
| P2 | 服务端线程化 + 随机端口 + 单实例 + 日志 | 双击即用，两个实例不会同时开 |
| P3 | www 内嵌、`--devtools` 开关、错误页 | 只发一个目录，去掉 www 也能跑 |
| P4 | 打包（zip / Inno Setup）、图标、签名 | 装到干净机器上一次成功 |
| — | ~~无边框自绘标题栏~~ | 已否决：用系统标题栏 |

P0~P2 是核心，估计 2~3 天；P3~P4 再加 1~2 天。

---

## 13. 风险与注意

1. **CEF 升级成本**：升级要重新编译 wrapper、重跑一遍页面验证（我们已有
   `tools/cef_test/eph205_cef_check.py` 可以复用）。固定 109 是好事，但要有
   升级预案（Chromium 每 4 周一个版本，安全补丁会积压）。
2. **杀毒误报**：未签名的 exe + 186 MB 的 libcef.dll，容易被拦。建议代码签名。
3. **首次启动**：会创建缓存目录，第一次可能慢 1~2 秒；用启动画面或先建窗口再加载。
4. **GPU 兼容**：老工控机可能没有可用的 D3D11。已打包 vk_swiftshader 兜底，
   但三维视图在软件渲染下会卡；需要的话加一个"禁用三维"的配置项。
5. **输入法**：CEF 对中文输入法的支持在 109 上没问题（已实测输入框可用），
   但自绘标题栏时要额外处理。
6. **多显示器 / 高 DPI**：`--force-device-scale-factor` 或 `CefSettings` 的
   DPI 感知；页面按 1024×768 设计，高 DPI 下需要验证一次。

---

## 14. 需要你确认的

1. **发布形态**：便携 zip / Inno Setup 安装包 / 两者都要？
2. ~~窗口：系统标题栏还是自绘？~~ **已定：系统标题栏，不自绘非客户区。**
3. **全屏 / kiosk**：车间触摸屏需不需要"只能开这一个程序"的模式？
4. **产品名与图标**：现在叫「EP-H205 综合测试系统」，图标有没有现成的 `.ico`？
5. **CSV 导出落盘**：每次弹保存框 / 固定目录（比如 `文档\EPH205\exports`）/ 系统下载目录？
6. **生产版是否保留 F12**：默认关闭、加 `--devtools` 参数才开，可以吗？
7. **目标系统**：只 Win10/11，还是也要 Win7/8？（影响是否能升 CEF）
8. **www 内嵌**：同意把页面打进 exe 吗？（发布看不到文件夹，但现场改页面就得重编）
9. **CEF 二进制来源**：每台开发机自己解压到 `3rdparty/cef-109`，还是放共享盘
   由 CMake 通过 `CEF_ROOT` 指过去？
10. **单实例**：确认要限制吗？（串口独占，建议要）
11. **令牌防护**：本机其它程序访问 127.0.0.1 的问题，要不要现在做？

---

## 15. 附：本轮已经验证过的事实

- CEF 109 里页面的完整功能可用（登录/测量/实时读数/三维/历史），
  脚本 `tools/cef_test/eph205_cef_check.py`，结论见
  `docs/在CEF中使用当前的205web.md`。
- ChromeDriver 109 用 `debuggerAddress` 附加 cefclient 稳定；
  Selenium 4.52 自带的服务管理会卡住（所以脚本用标准库 HTTP）。
- WebGL 2.0 走 ANGLE/D3D11 硬件路径，不需要开关。
- 一个服务端实例只能挂一个浏览器（1:1 会话），客户端天然满足。

---

## 16. 实施结果（2026-11-12）

### 已完成

| 阶段 | 内容 | 产物 |
|---|---|---|
| P0 | CEF 放 `3rdparty/cef-109`（gitignore）+ `find_package(CEF)` + 最小窗口 | `apps/eph205_client/` |
| P1 | handler 清单：右键菜单清空、F5/Ctrl+R/Ctrl+P/Ctrl+S/Ctrl+U/缩放全拦、弹窗拦截、拖放拦截、权限拒绝、外链交给系统浏览器 | `client.cpp` |
| P2 | 服务端线程化（`V1Server::Start/Stop`）+ 端口冲突自动顺延（8791..8810）+ 单实例 + CEF 日志 | `main.cpp` |
| P3 | `--devtools` 开关（仅调试版开 9223）、导出落到 `exports\` | — |
| P4 | 打包脚本：目录 + zip | `tools/package_client.bat` |

### 验证

`tools/cef_test/eph205_client_check.py`（附加到 `EPH205.exe --devtools`）：

```text
F5:     页面没有刷新
Ctrl+R: 页面没有刷新
登录 → 启动仿真 → 连接 COM7 → 导入 7 项 → 测量
统计: 7/7，通过 5，失败 1，71.4%，记录 #1
导出 CSV: exports\measurement-1.csv
三维: canvas 1234，WebGL 2.0
页面错误: 无
```

打包后 `dist\EPH205\`（263 MB）+ `dist\EPH205-client.zip`（114 MB），
拷走后双击 `EPH205.exe` 即可，服务端与页面同进程。

### 和方案不一致 / 尚未做的

1. **www 还是目录形态**，没有嵌进 exe。原因：嵌入需要给底座加一个
   资源静态处理器（约 80 行）+ 12 个文件的 RC 清单，这次先保证主链路。
   现在发布包里 `www\` 是可见的（客户能删），下一轮补。
2. **加载失败没有错误页**。服务端起不来的极端情况下是白屏（正常情况下
   服务端先于浏览器启动，不会发生）。
3. **没有图标和 DPI 清单**。窗口用的是系统默认图标；高 DPI 下依赖
   CEF 的缩放，还没在 200% 缩放的机器上验证。
4. **没有 Inno Setup 安装包**，只有便携 zip。

### 两个实现细节（值得记下来）

1. CEF 109 的 `CefDownloadHandler::OnBeforeDownload` 返回 **void**（不是 bool），
   `OnCertificateError` 比文档多一个 `CefCallback` 参数——签名以头文件为准。
2. 我们的目标没有定义 `UNICODE`，`IDC_ARROW` 会展开成窄字符版本，
   `LoadCursorW` 直接编译失败。CMake 里补了
   `UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX`。

### 关不掉的问题（用户反馈后修好了）

**现象**：点窗口的 X 没反应，进程一直在，只能用任务管理器强杀。

**定位**：加了一行文件日志（`%TEMP%\eph205-client.log`）后，序列很清楚：

```text
WM_CLOSE → RequestClose → DoClose(返回 false)
WM_CLOSE → RequestClose        ← CEF 又给父窗口发了一次
（然后什么也没有）
```

第一次 WM_CLOSE 调 `CloseBrowser(true)`，CEF 回调 `DoClose`；我们返回 false
表示"允许关闭"，于是 CEF 给父窗口再发一次 WM_CLOSE，等父窗口被销毁后才继续
销毁浏览器、回调 `OnBeforeClose`。我们的第二次 WM_CLOSE 又去调
`CloseBrowser`（没有效果），窗口没销毁 → CEF 等窗口 → 我们等 `OnBeforeClose`
→ 互相等死。

**修复**：两段式关闭。

```cpp
if (!closing_ && browser_ && browser_->GetHost()) {
  closing_ = true;
  browser_->GetHost()->CloseBrowser(true);   // 第一次：征求浏览器同意
  return;
}
Destroy();                                    // 第二次：真正销毁父窗口
```

销毁父窗口后子窗口跟着没，`OnBeforeClose` 才到，再 `CefQuitMessageLoop()`。
修完 WM_CLOSE 后 **0.6 秒**退出，日志完整走完
`WM_CLOSE → DoClose → WM_CLOSE → Destroy → WM_DESTROY → OnBeforeClose`。

**顺带修掉**：窗口标题一直是空的（任务栏看不到程序名）。原因是
`WM_NCCREATE` 里存完 `this` 就 `return TRUE`，跳过了默认处理，创建期间那次
`WM_SETTEXT` 落空。改成交给 `DefWindowProcW`，创建后再显式 `SetWindowTextW` 兜底。

### 参考：YiGuai（Qt 嵌入 CEF，多窗口）怎么做的

`D:\workspace\self\works\workspace\YiGuai` 是同类场景的成熟实现，关窗做法值得照抄：

```cpp
// cef_client_handler.cpp
bool CefClientHandler::DoClose(CefRefPtr<CefBrowser> browser) {
    NotifyBrowserClosing(browser);
#ifdef Q_OS_LINUX
    return false;
#else
    return true;   // Windows：别再给顶层窗口发关闭事件，窗口我们自己销毁
#endif
}

void CefQWidget::OnBrowserClosing(CefRefPtr<CefBrowser>) {
    is_closing_ = true;
    emit browserClosing();
    close();                        // 第二次 close() 才会真的关
}

void CefQWidget::closeEvent(QCloseEvent *event) {
    if (!is_closing_) {
        browser_->GetHost()->CloseBrowser(false);   // 先问浏览器
        event->ignore();                            // 本轮不关
    }
}
```

要点：

1. **Windows 上 `DoClose` 返回 true**。返回 false 会让 CEF 给顶层窗口再发一次关闭事件；
   多窗口时这是灾难——你不知道那个事件会落到哪个窗口上，容易误关。
2. **关闭走两轮**：第一轮只发 `CloseBrowser`，并 `ignore()` 掉这次关闭；
   `DoClose` 回调里置标记并主动 `close()`，第二轮才真正销毁。
3. **`gBrowserCount` 静态计数**：`OnAfterCreated` 加一、`OnBeforeClose` 减一，
   全部关完才算程序退出。多窗口时靠它判断"最后一个走了没有"。

我们据此改成了同一套（单窗口，计数退化为 1）：

```cpp
bool ClientHandler::DoClose(...) {
  window_->PostDestroy();   // 延后一拍 DestroyWindow（对齐 Qt 的 close() 时序）
  return true;              // 不让 CEF 再发 WM_CLOSE
}

// WM_CLOSE：只把请求转给浏览器，不销毁窗口（对应 event->ignore()）
// WM_APP+1：DoClose 里 Post 过来的，真正 DestroyWindow
```

实测点 X 后 0.4 秒退出：`WM_CLOSE → DoClose → WM_APP_CLOSE → Destroy →
WM_DESTROY → OnBeforeClose`。以后要加多窗口，只需把 `gBrowserCount` 那套计数搬过来。

### 截图怎么抓的

`PrintWindow` 抓不到 CEF 的 GPU 合成内容（只能拍到白底 + 标题栏）。
截图时用 `EPH205.exe --devtools --disable-gpu` 启动，走软件合成，
`tools/capture_window.ps1` 就能把窗口连同标题栏一起拍下来。
这是拍照手段，不影响正常使用时的硬件渲染。
