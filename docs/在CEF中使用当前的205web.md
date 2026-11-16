# 在本地编译的cef中使用205web试试看

## 其它信息记录
- [旧版本的chromeDriver下载地址](https://chromedriver.storage.googleapis.com/index.html)

## 我给你(claude Desktop)提供的信息
- 我已经编译好了cef。放在：`D:\trae_workspace\qt5-cef5414.120-base-env\build-cef-109`
- `"D:\trae_workspace\qt5-cef5414.120-base-env\build_cef_self_x64.bat"` 是构建它的脚本
- `"D:\trae_workspace\qt5-cef5414.120-base-env\chromedriver_win32\chromedriver.exe"` 是我下载的109的ChromeDriver
- `test_cef.py`是我 `pip install selenium -i https://pypi.tuna.tsinghua.edu.cn/simple` 之后测试的脚本

## 来吧，你给在cef上测试测试
---

## 验证结论（2026-11-12）

**可用。** 在 build-cef-109 的 cefclient（Chrome 109.0.5414.120）里完整跑通，
无 JS 错误，三维视图走 WebGL 2.0 硬件渲染（ANGLE / D3D11）。

实测流程：登录 → 启动仿真 → 连接 COM7 → 导入 CSV（7 项）→ 测量
（7/7，5 通过 1 失败，71.4%）→ 历史 → 三维（12 个部件，canvas 1361×460）。

脚本：`tools/cef_test/eph205_cef_check.py`

```bat
python -I tools\cef_test\eph205_cef_check.py
```

脚本会自己在 8791 起一个 `--auth` 的服务端、拉起 cefclient（调试端口 9223）
和 ChromeDriver（9516），跑完都保留，方便接着手动点。

### 两个坑

1. **Selenium 4.52 附加 cefclient 会卡住**：`webdriver.Chrome(service=..., options=...)`
   加了 `debuggerAddress` 后长时间无响应。改成用标准库直接调 WebDriver 的
   HTTP 接口（`POST /session` + `goog:chromeOptions.debuggerAddress`）一次成功。
2. **一个服务端实例只挂一个浏览器**（1:1 会话）。CEF 和 Edge 同时连会 409，
   所以测试用独立的 8791，日常调试的 8790 不受影响。

### 兼容性

| 特性 | CEF 109 |
|---|---|
| ES module / import map / 动态 import | 正常 |
| WebSocket + ArrayBuffer 二进制帧 | 正常 |
| Canvas 2D、WebGL2 | 正常（硬件） |
| CSS 变量 / flex / sticky 表头 / ResizeObserver | 正常 |

页面上没有未捕获异常。`app.js` 现在会把未捕获错误收进 `window.__errors`，
内嵌环境排障时可以直接读。
