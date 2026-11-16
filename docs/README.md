# docs 目录索引

| 目录 | 放什么 | 内容 |
|---|---|---|
| `spec/` | 协议规范（活文档，跟代码演进） | v1 协议正文、落地检查清单、v0 demo 规范存档 |
| `guide/` | 上手与操作指南 | 205 web 版主文档、CEF 客户端使用、自动化调试 |
| `plan/` | 方案与储备（定稿但未实施，或设计记录） | 传输层抽象、web/Qt 双前端策略、CEF 客户端方案 |
| `thinking/` | 分析、复盘、梳理 | v1 复盘、工控适用性分析、YiGuai 项目梳理 |
| `todo/` | 待办 | 项目待办与设计储备 |

## 文件清单

```text
spec/
  ws-protocol-v1.md        v1 协议正文（设计稿）
  v1-checklist.md          v1 落地检查清单（活文档）
  ws-protocol.md           v0 demo 规范（存档）
guide/
  eph205-web.md            EPH205 Web 版主文档（结构/API/运行/验证）
  在CEF中使用当前的205web.md   CEF 109 里跑当前 web 的验证记录
  automation-debugging.md  自动化调试通用指南（EdgeDriver/CEF）
plan/
  v1-传输层抽象方案.md        协议逻辑与通讯库解耦（定稿暂不实施）
  web与Qt双前端策略.md        零 Qt 核心 + 双前端的需求与实现路径
  CEF客户端方案.md           Win32 + CEF 109 桌面客户端设计
thinking/
  v1-retrospective.md      v1 设计 vs 实现的差距复盘
  v1-工控适用性分析.md        协议能否满足工控上位机的分析
  YiGuai-项目梳理.md         旧 Qt 项目的可复用经验梳理
todo/
  todo.md                  项目待办与设计储备
```

维护约定：新文档先想清楚归哪类再放；协议相关改动同步更新 `spec/` 下两份
活文档；已实施的方案留在 `plan/` 作为设计记录，不迁去 guide。

## 仓库顶层（页面归各自的 app，不设顶层 www）

```text
apps/v1/            通讯层 + 产品：base、modules/eph205、www/（静态根）
apps/cef_client_shell/  CEF 客户端外壳（业务无关，产物 EPH205.exe）
apps/mongoose_c/    mongoose 纯 C 教学 demo（页面内嵌在源码里）
apps/mongoose_cpp/  mongoose 双通道波形 demo（页面在自带 www/）
docs/               本文档（spec/guide/plan/thinking/todo）
tools/              构建、打包、自动化脚本
3rdparty/           mongoose、sqlite、json、cef（cef 不入库）
```

静态页不属于"仓库级资源"，而属于提供它的那个程序：`apps/v1/www` 由
v1 服务端提供（构建时复制到 `build/www`），CEF 外壳直接复制它，
`apps/mongoose_cpp/www` 只服务那个 demo。
