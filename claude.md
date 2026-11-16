# claude.md

本仓库对 agent 的约定，优先于默认行为。

## 工作方式

- 测试/验证用的临时文件统一放在仓库根目录的 `temp/`（已被 git 忽略）。
- 改动先留在工作区，经用户确认后再提交；不自动 commit / push，不发邮件。

## C++

遵循 Google C++ Style，以下按本仓库现状：

- 源文件用 `.cpp` / `.h`；头文件用 `#pragma once`。
- 缩进 2 空格，行宽 80；注释用中文。
- 命名：类型与函数 `CamelCase`，变量与参数 `snake_case`，
  成员变量尾下划线 `member_`，常量 `kCamelCase`，宏 `UPPER_SNAKE`。
- 业务代码不主动抛异常；失败走 `bool` 返回值 + `std::string& err` 出参。
  第三方库（nlohmann/json、SQLite）抛异常时在调用边界兜住。
- include 顺序：对应头文件 → C 系统头 → C++ 标准库 → 第三方 → 本仓库。

## 脚本

- `.bat` 一律纯 ASCII：注释和 echo 都不写中文。批处理按控制台代码页解析，
  UTF-8 源文件在 GBK 代码页下会乱码，严重时会影响解析。
- `.ps1` 可以写中文注释，源文件保持 UTF-8。
