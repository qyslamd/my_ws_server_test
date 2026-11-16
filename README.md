# cef109_ws_base_env

一台"设备上位机"的完整实现。

**上位机**指站在操作员这一侧、控制仪器并显示结果的软件（相对于跑在仪器
里的固件）。本项目控制的是 EPH205 电阻导通测试仪：它有一排测试点，测两点
之间通不通、阻值多少，用来检查线束、连接器有没有断线或虚接。

界面是网页（浏览器里打开），也可以装成桌面程序（一个只有窗口、没有地址栏
的浏览器外壳）。两种形态共用同一套服务端与同一套页面。

---

## 1. 项目由两部分组成

| 部分                | 位置                      | 职责                                                                                                          |
| ------------------- | ------------------------- | ------------------------------------------------------------------------------------------------------------- |
| **v1 通讯层**       | `src/framework/v1/base/`           | 通用的设备上位机框架：HTTP 接口、WebSocket 推送、会话、作业、订阅与流控。**不含任何设备代码**，换产品只换模块 |
| **EPH205 业务模块** | `src/apps/eph205/` | 串口协议、命令、批量测量、结果判定、历史落库                                                                  |

"**模块**"是通讯层的扩展点：业务方实现一个接口（`v1::Module`，见
`src/framework/v1/base/module.h`），把它注册进通讯层，就得到一套完整的 REST + 推送
服务。通讯层自己不知道"电阻"是什么。

另外三个程序是**学习与对照用的 demo**，不属于产品：

| 程序                     | 用途                                                                                                             |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------- |
| `src/examples/mongoose_c/`       | 单文件纯 C，演示 mongoose 的事件循环、HTTP 路由、WebSocket、定时器。想看框架本身长什么样，读这一个文件           |
| `src/examples/mongoose_cpp/`     | 双通道波形 demo，早期按协议改造过的参考实现                                                                      |
| `src/apps/eph205_app/` | 桌面客户端外壳：开窗、加载一个网址、关掉浏览器的刷新/右键/新窗口。**不知道 EPH205 是什么**，换产品只改网址和图标 |

---

## 2. 当前状态

- 分支只有 `main`（2026-11-16 起）。`develop`、`feature/*` 已删除，不要再建长期分支。
- 里程碑 M0–M5 全部完成：垂直切片、实时数据流、三维视图、Excel 导入、登录与角色、历史对比与批量导出。
- 已知缺口（都记在文档里，不是遗忘）：
  - 通讯层不持有环形缓冲，所以"来不及发送而丢弃数据"无法上报为 `core.overrun` 事件；
    模块自己的 `readings_` 队列满 4096 条时是静默丢弃（`eph205_module.cpp`）。
  - 非法的 `credit` 消息只被忽略，没有回 `core.error`（通讯层错误通道尚未建立）。
  - 自动化脚本 `tools/e2e/ui/edge_webdriver/eph205_e2e.py` 里还有几处按旧界面写的选择器
    （用 `.tag` 类找"管理员/操作员"标识，新界面是 `.chip`），跑之前需要先更新。
  - Qt QWidget 前端只有方案没有实现，见 `docs/plan/03-web与Qt双前端策略.md`。
  - 页面尚未嵌进 exe，客户机仍需要 `www/` 目录；安装包（Inno Setup）未做。

---

## 3. 跑起来（30 秒版）

需要 Windows + Visual Studio 2019（含 C++ 工具集）+ CMake + Ninja。

```bat
build.bat                      :: x64 + release
build.bat x64 release client   :: 连桌面客户端一起
build.bat x64 debug            :: 调试版
```

产物在 `build/ninja-<arch>-<config>/` 的根目录：exe、`www/`、`data/` 在同一层，
静态库进该目录下的 `lib/`。目录名由参数决定，`x64 release` 即
`build/ninja-x64-release/`。

**没有真机也能完整体验**：用 com0com 建一对虚拟串口 `COM7 ↔ COM8`，让程序
自己跟自己通信。

```bash
cd build/ninja-x64-release
./eph205_server.exe 8790
```

然后浏览器打开 <http://127.0.0.1:8790/eph205/>，用 `admin / admin` 登录，
点「启动仿真」→「连接」→ 导入测试项 →「开始测试」。

> **必须从构建目录启动。** 页面（`www/`）与数据目录（`data/eph205/`）都是
> 相对当前目录找的，在别处启动会 404 且数据写到别处。

桌面客户端另需 CEF 二进制（约 260 MB，不入库，放到 `3rdparty/cef-109/`）：

```bat
build.bat x64 release client
build\ninja-x64-release\EPH205.exe
```

---

## 4. 仓库结构

```text
src/framework/v1/             基础通讯框架（不含设备代码）
  base/                       HTTP/WS、会话、作业、订阅与流控
  main.cpp                    纯框架入口            -> v1_server.exe
src/apps/eph205/              EPH205 产品
  protocol.*                  设备协议编解码 AA 55 CMD LEN DATA SUM8
  serial_port.*               Win32 串口读写与端口枚举
  eph205_client.*             一问一答的帧事务（阻塞，只在工作线程调用）
  simulator.*                 固件行为仿真 + 挂在另一个串口上的应答线程
  measurement_store.*         SQLite 落库与 CSV 导出
  eph205_module.*             v1::Module 实现：命令/参数/作业/流
  main.cpp                    产品入口               -> eph205_server.exe
  www/                        静态根，构建时复制到构建目录的 www/
src/apps/eph205_app/          桌面客户端（组装：进程内服务端 + 外壳）
                              -> EPH205.exe
src/common/cef_shell/         CEF 窗口外壳静态库（业务无关）
src/common/log/               日志门面（spdlog 只在 .cpp 里出现）
src/examples/mongoose_c/      mongoose 纯 C 教学 demo  -> mongoose_c_demo.exe
src/examples/mongoose_cpp/    mongoose 双通道波形 demo -> mongoose_cpp_demo.exe
src/tests/                    单元测试（framework/ common/ modules/）
docs/                         文档，索引见 docs/README.md
tools/build/                  构建与打包脚本（build_client、package_client、installer.iss）
tools/e2e/                    端到端测试：api/（纯协议） ui/（浏览器与客户端）
tools/dev/                    手动调试用的小工具（CDP 操作台）
3rdparty/                     mongoose / sqlite / spdlog / googletest 等（源码入库）
```

`3rdparty/esp-src/` 是历史遗留的第三方源码（约 8.9 万行），**不参与构建、
与本产品无关**，不要照着它改代码。

---

## 5. 端口与运行期文件

| 程序                      | 端口                      | 说明                                            |
| ------------------------- | ------------------------- | ----------------------------------------------- |
| `eph205_server.exe`       | 8790（默认，可传参改）    | 产品服务端                                      |
| `v1_server.exe`           | 8770（默认）              | 纯通讯层，无业务模块                            |
| `EPH205.exe`（客户端）    | 8791 起，占用则顺延 20 个 | 进程内起同一个服务端；可用 `--port=8791` 改起点 |
| `mongoose_cpp_demo.exe`   | 8765                      | demo                                            |
| `mongoose_c_demo.exe`     | 8766                      | demo                                            |
| 客户端 `--devtools`       | 9223                      | Chrome 调试端口，仅调试用                       |
| EdgeDriver / ChromeDriver | 9515 / 9516               | 自动化脚本用                                    |

运行期文件（都不入库）：

```text
data/eph205/measurements.db    测量记录（SQLite，WAL 模式）
data/eph205/*.imported         旧版 JSONL 自动导入后改名留档
<exe 同级>/exports/            客户端导出 CSV 的落盘目录
v1_config.json                 通讯层参数持久化（工作目录下）
```

---

## 6. 一次测量是怎么走通的

按"要解决什么问题 → 怎么解决"的顺序：

1. **浏览器不能让串口开合、计时、纠错。** 所以所有设备操作都在服务端，
   浏览器只发命令、收结果。
2. **命令走 HTTP，数据走 WebSocket。** 命令（连接、下发参数、开始测试）
   需要明确的成功/失败应答，用 HTTP；测量结果每秒几百条、要求连续，
   用 WebSocket 单向下推。**WebSocket**（简称 WS）是一种在一条 TCP 连接上
   双向传消息的协议，浏览器原生支持。
3. **下推不是"无脑广播"。** 客户端先订阅（`POST /api/v1/subscriptions`），
   服务端才推；并且按**流控**发——客户端声明一个额度（窗口，单位字节），
   服务端发出去的字节从额度里扣，客户端处理完再把额度还回来（`credit`）。
   额度用完就暂停，防止服务端把慢客户端淹没。
   **流控**指发送方按接收方给的额度限速，原型是 HTTP/2 的滑动窗口。
4. **串口是独占且慢的。** 所有串口事务都在一个工作线程里串行执行；HTTP
   线程只把任务投进去并等结果（`RunSync`），不直接碰串口。
5. **批量测量跑成"作业"。** 作业有 id、进度、取消；进度经事件
   `eph205.job.progress` 推给浏览器。测完写成一条记录进 SQLite，返回记录号。

设备协议本身（帧格式、命令码、状态码）见 `docs/spec/02-ws-protocol-v1.md`
与 `src/apps/eph205/protocol.h`。

---

## 7. 常用命令

构建与打包（脚本都在 `tools/build/`）：

```bat
build.bat                              :: x64 + release，全部目标（不含客户端）
build.bat x64 release client           :: 连桌面客户端一起（需要 CEF）
build.bat x64 debug                    :: 调试版
tools\build\build_client.bat           :: 上面那条的别名，留着手熟
tools\build\build_debug.bat            :: x64 debug 的别名
tools\build\package_client.bat         :: 打包：dist\EPH205 + zip
                                       :: 装了 Inno Setup 6 时再出安装包
```

安装包由 `tools/build/installer.iss` 描述（Inno Setup 6）。`package_client.bat` 检测到
`ISCC.exe` 会自动编译，产物是 `dist\EP-H205-<版本>-x64-setup.exe`；没装就跳过，
只留免安装版目录与 zip。

端到端测试（`tools/e2e/`）。`api/` 只走 HTTP 与 WebSocket，`ui/` 需要浏览器或
客户端进程与对应驱动：

```bat
python -I tools\e2e\api\eph205_api_test.py            :: 纯接口：连接→单点→作业→历史→CSV
python -I tools\e2e\api\eph205_auth_test.py           :: 登录与角色拦截
python -I tools\e2e\api\eph205_fc_test.py             :: 流控回归：window=0 不推、额度耗尽暂停、归还恢复
python -I tools\e2e\ui\edge_webdriver\eph205_e2e.py  :: 真实 UI 全流程（EdgeDriver 9515）
python -I tools\e2e\ui\cef_test\eph205_client_check.py :: 桌面客户端（ChromeDriver 9516 + --devtools）
```

---

## 8. 改动前必读（都是实际踩过的坑）

1. **静态页归各自的程序，没有仓库级 `www/`。** 产品页面在 `src/apps/eph205/www`，
   由服务端提供；demo 的页面在自己目录下。加页面时放到对应 app 里。
2. **前端改样式只在 `src/apps/eph205/www/eph205/assets/` 里加变量。** 组件里写死十六进制
   颜色会让深色主题失效；CEF 109（Chrome 109）不支持 `color-mix()`，交互态用
   预置的 rgba 变量。
3. **页面切换靠 `.page:not(.active)`。** 页根节点挂着布局类 `.stack`，权重相同、
   定义在后，用 `.page { display:none }` 会被覆盖，结果五个页面全堆在一屏。
4. **切页要调 `page.setActive(bool)`。** 历史页的首次查询、调试与工具页的日志
   轮询都挂在它上面，不调就永远是空列表。
5. **`icon()` 返回的是字符串。** 当子节点传给 `el()` 会被转义成文本，屏幕上直接
   出现 `<svg …>`；需要图标与文字并排时用 `iconEl()`。
6. **CEF 里离开三维页要停渲染循环。** 不停会 60 帧空转，还会让调试端截图卡死。
7. **`.bat` 一律纯 ASCII。** 批处理按控制台代码页解析，UTF-8 中文在 GBK 代码页
   下会乱码，严重时影响解析。`.ps1` 可以写中文，保持 UTF-8。
8. **换行符由 `.gitattributes` 管**：除 `.bat` 是 CRLF，其余统一 LF。
9. **`*.obj` 在 `.gitignore` 里被当成编译产物忽略**，Wavefront 模型靠
   `!/src/apps/eph205/www/eph205/**/*.obj` 这条例外保留。移动模型目录时记得同步改。
10. **CEF 客户端跑久了 ChromeDriver 截图会整个卡死**（JS 仍能执行，重启进程恢复）。
    自动化脚本要为此留退路，这不是界面 bug。

---

## 9. 文档地图

完整索引见 [`docs/README.md`](docs/README.md)。

| 想知道什么                                        | 看哪里                                  |
| ------------------------------------------------- | --------------------------------------- |
| 设备协议、帧格式、命令码、错误码                  | `docs/spec/02-ws-protocol-v1.md`        |
| 协议落地到什么程度、还差什么                      | `docs/spec/03-v1-checklist.md`          |
| 怎么跑、前端结构、API 清单                        | `docs/guide/04-eph205-web.md`           |
| 自动化调试怎么搭                                  | `docs/guide/07-automation-debugging.md` |
| 以后要做的方案（Qt 前端、传输层抽象、CEF 客户端） | `docs/plan/`                            |
| 待办                                              | `docs/todo.md`                          |

---

## 10. 给 agent 的约定

本仓库的规则以 [`claude.md`](claude.md) 为准，优先于任何默认行为。要点：

- 临时文件放 `temp/`（已忽略），不要散落在仓库里。
- **改动先留在工作区，等用户确认后再提交；不自动 commit、不自动 push、不发邮件。**
- C++ 遵循 Google Style，本仓库的差异写在 `claude.md`：2 空格、80 列、中文注释、
  `#pragma once`、失败走 `bool` + `std::string& err` 而不是异常。
- 写文档禁止黑话：自造词当场定义，缩写首次出现给全称，先说问题再说方案。
- 不要做的事：不要恢复 libwebsockets 或 `develop` 分支；不要把设备代码写进
  `src/framework/v1/base/`；不要为 demo 引入新依赖。
