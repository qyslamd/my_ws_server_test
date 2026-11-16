# lint.md
> [!abstract] 踩坑记录
> 记录在使用ai agent过程中的踩坑记录。可以无限长
> 每一条记录都需要描述清楚问题现象，触发条件，解决办法。


## 1. CEF 109 不认 MD3 里的 `color-mix()` 和 `font:` 简写

**现象**：按 Material Design 3 规范写的界面，在 CEF 109 里跑起来“没有报错，但处处不对”——悬停、按下没有底色变化，状态点没有半透明光晕；所有标题退回浏览器默认字号，字号层级全丢。DevTools 里 `color-mix()` 那条声明干脆看不到（解析期就被丢弃），`font: var(...)` 那条会显示但被划掉（invalid at computed-value time）。

**触发条件**：宿主是 CEF 109（Chromium 109），样式却照 Chrome 111+ 的写法来：

- `color-mix()` 从 Chrome 111 才有，Chromium 109 的 CSS 解析器不认识，整条 `background` / `box-shadow` 声明作废；
- 用变量承载 `font` 简写（`font: var(--md-title-medium)`）在 Chromium 里属于“计算期无效”，整条回退成初始值——所以标题不是“变小了”，是“回到默认字号”。

两条都只在“新规范喂旧内核”时出现，在 Edge / Chrome 里预览完全正常，最容易漏过去。

**解决办法**：

1. 颜色不用 `color-mix()`，改成预置 rgba 语义变量：`:root` 里把 `--hover-1`、`--hover-2`、`--ring-*` 各写一份浅色值，深色主题覆盖一次；组件只引用变量。（逐条替代值见源文档 §2.4 的对照表）
2. 排版不用 `font` 简写，拆成 `font-size` / `line-height` / `font-weight` 三个长写属性；令牌也只存这三个值。
3. 复查：全库搜 `color-mix(` 与 `font: var(` 应为 0 命中；改完在 CEF 109 里把浅色、深色两套主题各过一遍。

**来源**：`D:\sbb\airplane-analyse\code\docs\MD3-设计规范.md`（v1.0 · 2026-11-15）§0 三条硬约束、§2.4、§11 实现陷阱；本条记录于 2026-11-16。同一份文档还记了几条与 CEF 版本无关的前端坑（Grid 子项缺 `min-width:0` 撑破页面、表格列挤压、深色主题写死 hex、PrintWindow 抓不到 GPU 合成层），需要时再单独拆条目。
