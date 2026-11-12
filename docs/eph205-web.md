# EPH205 Web 版（本地分支）

> 分支：`feature/eph205-web`，**只留本地，不 push、不推公网**。
> 对照物：`D:\sb\eph205\workspace\eph2805-new`（Qt5 版）。
> 底座文档：`docs/ws-protocol-v1.md`、`docs/v1-checklist.md`。

---

## 1. 目标

用 v1 通讯底座 + 浏览器 UI 复刻 Qt 版**当前已有**的功能：

- 连接/断开、设备信息、模式与参数
- 测试项导入、批量测量、进度、通过率、逐项判定
- 结果落库、历史查询、CSV 导出
- 调试命令与原始帧
- 仿真（com0com 真实串口对）

不做：XLSX/PDF 报表、旧固定单元格 Excel、自学习、点检、校准。

---

## 2. 结构

```text
apps/v1/base/                 业务无关通讯底座（可公开）
apps/v1/main.cpp              纯底座入口 -> v1_server.exe
apps/v1/modules/eph205/       EPH205 业务模块（仅本地）
  protocol.*                  帧编解码 AA 55 CMD LEN DATA SUM8
  serial_port.*               Win32 串口 + 端口枚举
  eph205_client.*             一问一答帧事务（阻塞，仅工作线程调用）
  simulator.*                 固件行为仿真 + 挂到串口的 SimulatorLink
  measurement_store.*         JSONL 落库 + CSV 导出
  eph205_module.*             v1::Module 实现（命令/参数/作业/流）
  main_eph205.cpp             业务实例入口 -> eph205_server.exe
www/v1/eph205/                前端（原生 ES module + CSS，无构建，仅本地）
www/v1/eph205/vendor/         three.js 与 SheetJS 的本地副本
www/v1/eph205/help3d/         三维资产
www/v1/eph205/                前端构建产物（gitignore）
```

底座与业务的分界：底座把 `base/` 编成 `v1_base.lib`，业务实例自带 `main`。
`v1_server.exe` 里没有任何设备代码。

---

### 前端形态：无框架、无构建

`www/v1/eph205/` 里的就是上线用的文件，没有打包步骤：

```text
index.html       import map 把 "three" 指到 ./vendor/three.module.js
app.js           启动、登录、页签、状态轮询
lib/             api（REST + WS 帧解析）、ui（表格/抽屉/toast）、model（导入导出）
pages/           测试 / 调试 / 历史 / 工具 / 帮助
vendor/          three.module.js + three.core.js + 三个 addon + xlsx.full.min.js
```

- 不用 React / Vue：页面就五个，状态只有"设备、作业进度、登录用户"，直接操作 DOM。
- 不用 antd：表格、抽屉、toast 在 `lib/ui.js` 里自己实现（约 250 行）。
- three.js 用 `import` 静态引入但只在帮助页动态 `import()`，首屏不加载。
- xlsx 用 `<script>` 懒加载，第一次导入 Excel 才下载。
- 视觉按桌面版 QSS 的 token 复刻：主色 `#0078D4`、页面底 `#f5f6fa`、
  表头 `#f5f6fa`、表格选中 `#cce4f7`、标题靛蓝 `#1a237e`。

## 3. 线程模型

| 线程 | 职责 |
|---|---|
| 主线程（Mongoose 事件循环） | HTTP/WS、模块命令分派、JobTick 取事件 |
| 模块 worker 线程 | 所有串口事务、批量测量 |
| SimulatorLink 线程 | COM8 上的仿真应答 |

- 主线程调用 `RunSync` 把活派给 worker，最多等一个超时；超时后任务作废。
- 批量测量跑成作业，进度经 `JobTick` 每 10 ms 推一条事件。
- 仿真器自带互斥，主线程可随时改点值。

---

## 4. 对外 API

底座通用（任何模块都能用）：

```text
GET  /api/v1/capabilities|time|health
GET  /api/v1/modules|streams|state
POST /api/v1/modules/eph205/start|stop
GET/PUT/PATCH /api/v1/params/eph205
POST /api/v1/commands/eph205/{action}
POST /api/v1/jobs            { ns, type, input }
GET  /api/v1/jobs/{id}
DELETE /api/v1/jobs/{id}
GET  /api/v1/logs
GET  /ws?sid=...             1:1 会话（先建 WS，REST 带 X-Session-Id）
```

`eph205` 命令：

| 命令 | 说明 |
|---|---|
| `ports` | 枚举串口 + 当前配置的端口 |
| `connect` / `disconnect` | 打开串口并完成 0x01 握手 / 关闭 |
| `device_info` | 0x01 |
| `set_mode` | 0x0A，`{mode, source}` |
| `set_param` | 0x04，`{type, value}`，type 1..5 |
| `get_params` | 0x05 + 0x0B |
| `measure` | 0x02，`{p, n}` |
| `reset` | 0x08 |
| `raw` | 原样下发一帧，`{cmd, data(hex), timeoutMs}` |
| `check_point` | 0x07 点检测位，`{p, n}` |
| `frame_log` | 取走收发帧（上位机 + 仿真器） |
| `sim_start` / `sim_stop` | 仿真端开关 |
| `sim_set` / `sim_config` / `sim_clear` | 点值覆盖 / 默认阻值 / 清空 |
| `history_list` / `history_get` / `history_delete` / `history_export` | 历史 |
| `history_export_many` | 多条记录合并导出，`{ids:[...]}` |

作业 `measure_run` 输入：

```json
{
  "product": {"model": "", "code": ""},
  "operator": "tester",
  "deviceSource": "simulation",
  "items": [
    {"seq":1,"p":1,"n":2,"wire":0,"range":0,
     "minOhms":90,"maxOhms":110,"measure":true}
  ]
}
```

进度事件 `eph205.job.progress`：

```json
{"jobId":"job-1","state":"running","done":62,"total":640,
 "passed":58,"failed":2,"measureFailed":2}
```

完成时带 `recordId` 与 `summary`（total/passed/failed/measureFailed/
notMeasured/passRate/durationMs）。

---

## 5. 测试项导入

三种格式，解析都在浏览器里做，服务端只收结构化数组：

| 格式 | 说明 |
|---|---|
| CSV | 表头 `序号,P,N,线制,量程,下限,上限,测量`；无表头按 `P,N[,下限,上限]` |
| Excel `.xlsx/.xls` | 读第一个工作表；有名为 `测试项` 的表则优先；表头同 CSV |
| JSON | 数组，或 `{items:[...], product:{...}}` |

Excel 用 SheetJS（`xlsx@0.20.3`，官方 CDN 版；npm 上的 0.18.5 带
[CVE-2023-30533](https://github.com/advisories/GHSA-4r6h-8v6p-xvw6)）。

库文件已 vendor 到 `www/v1/eph205/vendor/xlsx.full.min.js`，
**只在第一次导入 Excel 时用 `<script>` 懒加载**，不进首屏。

## 6. 判定与落库

- 测量值为 NaN → 测量失败（原因 `over_range`）
- 整帧失败 → 本帧全部测量失败（`frame_failed`）
- 勾掉测量 → 未测（`unchecked`）
- 其余按上下限判定；上限≤0 视为只判下限
- 落库：`data/eph205/measurements.jsonl`，一行一条记录，明细内嵌
- CSV：浏览器端下载，服务端只回文本

---

## 7. 运行

构建：

```bat
build.bat
cmake --build build --target copy_www
```

启动（工作目录必须是 `build\`，静态页从 `www/v1` 取）：

```bat
cd build
eph205_server.exe 8790 --auth
```

不加 `--auth` 时服务端不校验角色，界面仍会按角色隐藏入口。

访问 `http://127.0.0.1:8790/eph205/`。

前端没有构建步骤：改 `www/v1/eph205/` 下的文件，重新 `copy_www` 后刷新即可。
调试时直接改 `build/www/v1/eph205/` 里的副本也能立即生效（下一次 full build 会被覆盖）。

---

## 8. 自动化验证

```bat
3rdparty\edgedriver_win64\msedgedriver.exe --port=9515

python -I tools\eph205_api_test.py           :: 纯接口：连接→单点→作业→历史→CSV
python -I tools\edge_webdriver\eph205_e2e.py :: 真实 UI：仿真→连接→导入→测量→五个页面
```

`eph205_e2e.py` 会在没有服务端时自己拉起一个，退出时结束。
期望统计：7 项 → 通过 5 / 失败 1 / 未测 1，通过率 71.4%。

---

## 9. 进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| M0 | 协议/串口/仿真/模块 + React 脚手架 + 垂直切片 | 完成 |
| M1 | 调试页原始命令与点检、流订阅实时读数 | 完成 |
| M2 | 三维视图（three.js 加载 help3d 的 OBJ/MTL） | 完成 |
| M3 | Excel 导入（SheetJS） | 完成 |
| M4 | 登录与角色（界面 + 服务端强制） | 完成 |
| M5 | 历史对比、批量导出 | 完成 |

---

## 10. 历史对比与批量导出

历史页多选：

- 选 2 条 → 「对比」：按 **(P,N)** 对齐两次测量，逐点显示基准值/对比值/差值，
  并标出判定变化（红底）、只有一边有的点（灰底），顶部给出共同点、仅基准、
  仅对比、判定变化、最大差值。对齐和差值都在浏览器里算，服务端只提供
  `history_get`。
- 选任意条 → 「批量导出 CSV」：走 `history_export_many`，服务端合并成一张表，
  前几列是记录 ID/时间/型号/编号/操作员，后面是逐项结果，便于拿 Excel 透视。

## 11. 实时读数（WS 数据流）

测量过程中，模块把每条结果压进 `readings_`，底座每 10 ms 取一次，
按 v1 帧格式推给订阅者：

```text
stream id 1  eph205.readings  class=record
records 块  重复的 [ts u64][len u32][payload]，payload 是一个 JSON 对象
```

前端 `src/v1frame.ts` 解析 24 字节头与各块，`api.onStream` 分发；
`MeasurePage` 订阅 id 1，收到后立刻发 `{type:"credit"}` 归还窗口，
否则窗口（默认 4 MB）用完后底座会停推。

调试页的「原始命令」走 `raw`：服务端除正常应答外也接受 ACK（0x80），
所以发未实现的命令（如 0x07 点检）能直接看到「命令不支持」而不是超时。

## 12. 登录与角色

两个内置账号（底座 `/api/v1/auth/*`）：

| 账号 | 角色 | 能做什么 |
|---|---|---|
| `admin/admin` | 管理员 | 全部 |
| `operator/operator` | 操作员 | 连接、测量、查/导出历史、看日志 |

仅管理员可执行的命令（模块 `AdminOnly`）：

```text
raw check_point reset set_param set_mode frame_log
sim_start sim_stop sim_set sim_config sim_clear history_delete
```

服务端在 `--auth` 下对命令做前置拦截，返回 `403 admin role required`；
界面同时隐藏「调试」页、禁用仿真按钮、隐藏历史删除。
两边都要有——只隐藏界面等于没有权限。

底座的钩子是 `v1::Module::AdminOnly(action)`，默认返回 false，
不带认证时底座不调用它，所以对其它业务模块零影响。

## 13. 三维视图

资产来自参考项目 `src/app/assets/help3d/`，原样拷到 `www/v1/eph205/help3d/`。

约定（照抄 Blender 导出）：

- OBJ 是 **Z-up、顶点在局部坐标**；
- `system_3d.json` 的 `position` 是 **Y-up 世界坐标**，Y 为高度；
- 所以渲染时每个部件先绕 X 轴 -90° 转成 Y-up，再平移到 `position`；
- 同一份 OBJ 复用多次（三块多路器板）时先加载一次再克隆，克隆后逐个复制材质，
  否则高亮一个部件会把同款全点亮。

实现：`www/v1/eph205/lib/viewer3d.js`，OrbitControls 旋转/缩放/平移，
点击部件高亮并在工具栏显示名称。

## 14. 注意事项

- 本分支的任何内容都不得合入 `develop`、不得推送。
- 参考项目只读，不要把它整段业务代码粘进来。
- 串口是独占资源：上一个会话没断开时，另一个进程打不开同一端口。
- WS 断开会让底座执行 SAFE_STOP（模块 `Stop`），这是设计行为；
  仿真器不受影响，需要单独停。
