# YiGuai 项目梳理：Qt 里嵌 CEF 做得好的地方

> 位置：`D:\workspace\self\works\workspace\YiGuai`
> 规模：`src/` 约 1.5 万行 C++（不含 3rdparty）
> 技术栈：Qt Widgets + CEF（Windows/Linux 双平台），qmake 构建
> 用途：自己以前做的浏览器外壳，完整度不低——标签页、书签、历史、下载、
> 查找、缩放、无痕窗口、全屏、DevTools 都有。
> 这份文档只挑"做得好的"，作为我们现在这套 205 客户端的参照。

---

## 一、整体分层

三层，边界很清楚：

```text
┌──────────────────────────────────────────────┐
│ 应用层   MainWindow / TabPageToolBar /        │
│          BookmarkBar / 各种 Popup / Widget    │
│          —— 只认 Qt，不认 CEF                  │
├──────────────────────────────────────────────┤
│ 适配层   CefQWidget（QWidget）                 │
│          —— CEF 事件 → Qt signal，Qt 调用 → CEF │
├──────────────────────────────────────────────┤
│ CEF 层   CefClientHandler（CefClient + 各 handler）│
│          —— 只认 CEF，通过 Delegate 往外抛      │
└──────────────────────────────────────────────┘
```

关键点：**CEF 层不知道 Qt 存在**。`CefClientHandler` 只依赖
`cef_client_handler.h` 里的 `Delegate` 抽象类；`CefQWidget` 实现这个
Delegate，再把回调翻译成 Qt 信号。想换掉 Qt（比如换 Win32 或换别的 UI 框架），
下面两层的代码一行不用改。

---

## 二、做得好的十处

### 1. 子进程分发放在 QApplication 之前

`src/main.cpp:77`：

```cpp
CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
ClientApp::ProcessType process_type = ClientApp::GetProcessType(command_line);
if (process_type != ClientApp::BrowserProcess) {
    if (process_type == RendererProcess || ZygoteProcess) app = new CefAppRender();
    else if (process_type == OtherProcess)                  app = new CefAppOther();
    int exit_code = CefExecuteProcess(main_args, app, nullptr);
    if (exit_code >= 0) return exit_code;      // 子进程到此结束
}
QApplication qt_app(argc, argv);               // 只有浏览器进程才建 Qt
```

好处很实在：渲染进程不会去初始化 Qt、不会建窗口、不会读配置。
另外给渲染进程留了一个 `NEED_CONSOLE` 开关（`AllocConsole`），
现场调试渲染侧问题时可以直接挂控制台——这种"调试口子"平时不用，用起来救命。

### 2. 把 Chromium 的消息泵接进 Qt 的事件循环

`src/main.cpp:213-253`：

```cpp
settings.no_sandbox = true;
settings.multi_threaded_message_loop = command_line->HasSwitch("multi-threaded-message-loop");
settings.external_message_pump = true;          // 关键

if (settings.multi_threaded_message_loop)  message_loop.reset(new MainMessageLoopMultithreadedWin);
else if (settings.external_message_pump)   message_loop = MainMessageLoopExternalPump::Create();
else                                       message_loop.reset(new MainMessageLoopStd);

CefInitialize(main_args, settings, app, nullptr);

MainWndMgr::Instance().createWindow(cfg);
message_loop->Run();        // 把泵挂在 Qt 的事件循环上
int result = qt_app.exec();
CefShutdown();
```

三种消息循环实现都留着（`browser/message_loop/` 下三个类），用命令行开关选。
`ExternalPump` 这一路是 Qt 嵌入的标准做法：CEF 的任务通过定时器在 Qt 的
事件循环里跑，界面不会被阻塞，也不需要额外线程。

**这一点我们 205 客户端没做**——我们是纯 Win32，直接用 `CefRunMessageLoop()`，
没有外部 UI 框架要迁就。如果将来要嵌进别人的宿主程序，这里是标准答案。

### 3. CefClientHandler 与 UI 完全解耦

`browser/cef_client_handler.h` 里那个 `Delegate` 抽象类列了 20 多个纯虚函数：
`OnBrowserCreated / OnBrowserClosing / onBrowserAddressChange /
onBrowserLoadingStateChange / onBrowserPreHookEvent ...`。

它不是"为了接口而接口"：`CefQWidget` 只实现自己关心的，其余留空；
`CefClientHandler` 里再包一层 `NotifyXxx()`，负责把回调切到主线程
（`CURRENTLY_ON_MAIN_THREAD()` 判断 + `MAIN_POST_CLOSURE` 重投递）。

这个"回调线程归一"的细节很值钱——CEF 的回调不全在主线程，
直接在回调里操作 Qt 对象是踩过的坑（他们显然是踩过才写的）。

### 4. 一个 CefQWidget = 一个浏览器 = 一个标签页

`cef_qwidget.h` 的公开接口就是浏览器能力清单：
`Navigate / GoBack / GoForward / Refresh / StopLoading / ZoomIn / ZoomOut /
Print / ShowDevTool / MuteAudio`，事件用 `browserXxx` 信号往外发。

好处：
- 标签页管理就是"往 `QStackedWidget` 里塞/删 widget"，不需要额外的浏览器表；
- 多窗口 = 多个 MainWindow，每个里面一组 CefQWidget，不用特殊处理；
- 谁拥有浏览器一目了然，销毁顺序不会乱。

### 5. 关窗协议（多窗口下尤其关键）

`cef_client_handler.cpp:174`：

```cpp
bool CefClientHandler::DoClose(CefRefPtr<CefBrowser>) {
    NotifyBrowserClosing(browser);
#ifdef Q_OS_LINUX
    return false;
#else
    return true;    // Windows：别再给顶层窗口发关闭事件，窗口我自己销毁
#endif
}
```

`cef_qwidget.cpp`：

```cpp
void CefQWidget::closeEvent(QCloseEvent *event) {
    if (!is_closing_) {
        browser_->GetHost()->CloseBrowser(false);
        event->ignore();                 // 第一轮不真关
    }
}
void CefQWidget::OnBrowserClosing(...) { // 由 DoClose 回调
    is_closing_ = true;
    close();                             // 第二轮才真关
}
```

还有 `static int gBrowserCount`：`OnAfterCreated` 加一、`OnBeforeClose` 减一，
日志一直打剩余数量。多窗口时靠它判断"最后一个走了没有"。

**为什么这个写法对**：`DoClose` 返回 false 的语义是"允许关闭"，
CEF 会给**顶层窗口**再发一次关闭事件——多窗口时那个事件落到哪个窗口上不可控，
容易连带关掉别的窗口。返回 true 就把这条路径切断，销毁权始终在自己手里。

（我们的 205 客户端一开始返回 false，就踩了"点 X 关不掉"的坑，
后来照这套改成返回 true + 自己销毁，0.4 秒退出。）

### 6. 全局配置集中在一个单例

`managers/CefManager.h` 把所有 CEF 相关的全局项收成一处：

```cpp
static bool seperate_sub_process;          // 独立子进程模式
static CefString browser_sub_process_path; // 子进程可执行体路径
static CefString resource_directory_path;
static CefString locales_directory_path;
static CefString cache_path / root_cache_path / user_data_path;
static uint remote_debugging_port;
static QMap<int, QString> zoom_map;        // 缩放档位
```

`populateSettings(CefSettings&, argc, argv)` 一处填完。改缓存目录、
改 UA、改调试端口，都只动这一个文件。

配套的 `appRender/` 是独立子进程可执行体（单独的 `.pro`）——大项目里
把渲染进程拆出主程序，主程序升级不影响子进程，也方便单独签名。

### 7. 自定义 scheme handler 的注册点很干净

`browser/scheme_handler.h` 里 `ClientSchemeHandler` 实现
`CefResourceHandler`（实现内存里的 `Open/Read/GetResponseHeaders`），
配套一个 `ClientSchemeHandlerFactory`，`main.cpp` 末尾一句
`custom_scheme::RegisterSchemeHandlers();` 注册。

这个模式正好是我们 P3"把 www 嵌进 exe"要用的：页面资源打包进资源文件，
用自定义 scheme（比如 `app://`）直接从内存吐给浏览器，客户既看不到
`www/` 目录，也不怕被误删。

### 8. UI 按浏览器语义拆分，而不是一个大 MainWindow

```text
toolbars/   NavigationBar  AddressBar  TabPageBar  BookmarkBar
            FindBar  NotificationBar  SearchBar
popups/     AddBookmarkPopup  SiteInfoPopup  ZoomPopup  UserInfoPopup  InprivatePopup  StyledMenu
widgets/    BookmarkWidget  HistoryWidget  DownloadWidget  AppConfigWidget
            TabThumbnailWidget  FullscnHint
dialogs/    AboutDialog  AlertDialog  PageDialog
```

每条工具栏、每个浮层都是独立类，MainWindow 只负责布局和转发。
结果是 `mainwindow.h` 里看不到任何 CEF 类型——应用层被彻底隔离在 CEF 之外。

### 9. 窗口管理器承担多窗口策略

`managers/MainWindowManager.h`：`MainWndMgr` 单例管所有窗口。

- `createWindow(MainWindowConfig)`：新窗口带偏移级联（`newWndOffsetX/Y`），
  不会叠在一起；
- `lastWindowGeometry()`：记住上次的位置尺寸；
- `closeAllInprivate()` / `inprivateCount()`：无痕窗口计数与批量关闭；
- `quitApplication()`：统一退出入口。

多窗口的"策略"集中在一处，窗口自己只管自己。

### 10. 快捷键抽象成命令

`globaldef.h` 里的 `CefShortcutCmd` 枚举 + `browserShortcut(CefShortcutCmd)` /
`devToolShortcut(CefShortcutCmd)` 信号：CEF 层只负责"识别出这是哪个命令"，
具体执行（新建标签、关标签、刷新、缩放……）由 UI 层决定。

好处是快捷键表可以整体改，也可以按场景屏蔽（比如焦点在 DevTools 里时走另一套）。

---

## 三、其他值得记一笔的

- **`widgets/QtFramelessWnd.h` / `QtWinFrameless`**：自绘标题栏 + 窗口拖动 +
  Aero 贴边。做得完整，但这也是维护成本最高的一块（高 DPI、多显示器、
  窗口动画都要自己处理）。205 客户端现在用系统标题栏，就是不想背这个。
- **第三方选型克制**：`3rdparty/` 只有三个——`Capturer`（截图）、
  `htmlcxx`（HTML 解析）、`qt-material-widgets`（Material 控件）。都很小，
  没有引入第二个 UI 框架或第二个网络栈。
- **国际化**：`QTranslator` + `.qm`，QSS 单独放 `resources/styles/normal.qss`，
  主题和逻辑分开。
- **调试口子**：`NEED_CONSOLE` 宏、`OnConsoleMessage` 转发到 qInfo、
  统一 `qInfo()<<__FUNCTION__` 打点——出问题时有迹可循。

---

## 四、对照：哪些已经搬到 205 客户端

| YiGuai 的做法 | 205 客户端现状 |
|---|---|
| 子进程分发在 UI 初始化之前 | 已做（`CefExecuteProcess` 放 `wWinMain` 第一句） |
| `DoClose` Windows 返回 true + 两轮关闭 | 已做（`f4d1563`） |
| 浏览器计数（`gBrowserCount`） | 单窗口，暂不需要；加多窗口时照搬 |
| CEF 配置集中管理 | 已做（`main.cpp` 里集中填 `CefSettings`，可再抽成 CefManager） |
| 自定义 scheme | 未做（P3：www 嵌 exe 时用） |
| 快捷键命令化 | 已做（`IsBlockedKey` + 一个白名单，比命令化更简单） |
| UI 与 CEF 分层 | 已做（`ClientHandler` 不碰窗口，窗口不碰 CEF 类型） |
| 消息循环适配 | 不需要（纯 Win32，直接 `CefRunMessageLoop`） |
| 窗口管理单例 | 单窗口，暂不需要 |

---

## 五、如果继续往下做，有两点可以留意

1. **无边框自绘标题栏的成本会随时间涨**：高 DPI、多显示器、贴边分屏、
   系统主题切换，每一个都要单独适配。205 客户端面向工控客户，系统标题栏
   反而更稳，建议不跟。
2. **全局单例（CefManager / MainWndMgr）在多窗口时好用，但也把状态摊到了全局**。
   205 客户端现在只有单窗口单浏览器，状态留在 `main.cpp` 里更直观；
   等真要加多窗口，再引管理器不迟。

---

## 附：文件地图

```text
src/main.cpp                     进程入口、CEF 初始化、消息循环装配
src/cef_qwidget.*                Qt 适配层（一个 widget = 一个浏览器）
src/browser/cef_client_handler.* CEF 层（Delegate + 各 handler + 线程归一）
src/browser/cef_app_*.h          三类 CefApp（browser / render / other）
src/browser/message_loop/*       三种消息循环实现
src/browser/scheme_handler.*     自定义 scheme 的资源处理器
src/managers/*                   配置、书签、历史、下载、窗口、显示
src/toolbars/ src/popups/ src/widgets/ src/dialogs/   UI 组件
appRender/                       独立子进程可执行体
```
