# tools

不参与编译、但要手动运行的脚本。

| 目录 | 放什么 | 前置条件 |
|---|---|---|
| `build/` | 构建与打包：`build_client.bat`、`build_debug.bat`、`package_client.bat`、`installer.iss` | VS2019 + CMake + Ninja；打包另需 CEF |
| `e2e/api/` | 端到端测试，只走 HTTP 与 WebSocket | 服务端已启动；多数用例需要 com0com 的串口对 |
| `e2e/ui/` | 端到端测试，需要浏览器或客户端进程与对应驱动；`capture_window.ps1` 是截图辅助 | EdgeDriver 9515 或 ChromeDriver 9516 + CEF 客户端 |
| `dev/` | 手动调试小工具（CDP 操作台），不做断言 | 一个开了远程调试端口的浏览器 |

**单元测试不在这里。** C++ 测试在 `src/tests/`，由 CMake 注册给 ctest：

```bat
ctest --test-dir build/ninja-x64-release --output-on-failure
```

判断标准：能 `ctest` 一键跑、不依赖外部进程的，放 `src/tests`；要起真实进程、
依赖浏览器或串口的，放 `tools/e2e`。
