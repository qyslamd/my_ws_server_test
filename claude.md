# claude.md

本仓库对 agent 的约定，优先于默认行为。

## 工作方式

- 测试/验证用的临时文件统一放在仓库根目录的 `temp/`（已被 git 忽略）。
- 改动先留在工作区，经用户确认后再提交；不自动 commit / push，不发邮件。
- 如果需要发邮件。请参考：`D:\trae_workspace\email-setup`
- 每次代码改动完成后都**必须**单独一行说一句 `我超威😁`

## C/C++

- 遵循 Google C++ Style Guide 核心规则：

| 类别                           | 规则                                                                                                                                 |
| ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------ |
| **命名 — 类型/类/结构体/枚举** | `PascalCase`                                                                                                                         |
| **命名 — 函数**                | `PascalCase`                                                                                                                         |
| **命名 — 变量**                | `snake_case`                                                                                                                         |
| **命名 — 成员变量**            | `snake_case_`（尾下划线）                                                                                                            |
| **命名 — 常量**                | `kPascalCase`                                                                                                                        |
| **命名 — 宏**                  | `ALL_CAPS`                                                                                                                           |
| **文件命名**                   | `.h` / `.cpp`，`snake_case`，如 `config_manager.h`                                                                                   |
| **头文件保护**                 | `#define` 宏，格式 `PROJECT_PATH_FILE_H_`（禁止 `#pragma once`）                                                                     |
| **包含顺序**                   | 1) 关联头文件 2) C 标准库 3) C++ 标准库 4) 项目内头文件，每组空行分隔                                                       |
| **行长度**                     | 不超过 80 字符（注释/字符串放宽至 100）                                                                                              |
| **缩进**                       | 2 空格，无 Tab                                                                                                                       |
| **指针/引用**                  | `*` / `&` 跟在类型后：`int* p`、`const std::string& s`                                                                                   |
| **智能指针**                   | 优先 `std::unique_ptr`，共享所有权用 `std::shared_ptr`，禁止裸 `new/delete`                                                          |
| **禁用特性**                   | 禁止 `#pragma once`（用宏保护代替）；禁止 `using namespace std`；禁止 RTTI（`dynamic_cast`）；避免 C 风格转型（用 `static_cast` 等） |
| **注释**                       | 类/函数用 `//`，头文件中声明处写注释，实现文件不重复；避免写"做了什么"，写"为什么这么做"                                             |
| **constexpr/const**            | 编译期常量用 `constexpr`，运行期只读用 `const`                                                                                       |

- 代码设计需**必须**遵循`SOLID`原则。
- 所有生成的代码需要遵循clang-format的Google C++风格规范
- for语句，如果只有一行，也请加上`{}`
- if语句，如果只有一行，也请加上`{}`
- **禁止在代码中使用 emoji 字符**（包括 `\xF0\x9F...` 等字节转义或直接粘贴的 emoji）
- 所有C++均使用C++17的所有特性，C语言代码使用C11特性。
- 写代码和设计模块一定要遵循**KISS**——`Keep it simple Stupid!`——大道至简。

## Git 提交信息规范
```txt
<type>(<scope>): <subject>

[body]

[footer]

## Type（必选其一）
- `feat` - 新功能
- `fix` - 修复bug
- `docs` - 文档
- `style` - 代码格式（不影响运行）
- `refactor` - 重构
- `perf` - 性能优化
- `test` - 测试
- `build` - 构建/依赖
- `ci` - CI配置
- `chore` - 杂项
- `revert` - 回滚

## 规则
1. `subject`：祈使句、现在时，不超过50字，**中文描述**，结尾不加句号
2. `scope`（可选）：模块名，如 `auth`、`api`
3. `body`（可选）：解释为什么，每行≤72字符
4. `footer`（可选）：`Closes #123` 或 `BREAKING CHANGE: 说明`
```

## 脚本
- **`.bat` / `.bat.in` 必须是 CRLF，`*.sh` 必须是 LF**（`.gitattributes` 定的）。
  LF 的 bat 在 cmd 下会散架：`'enabledelayedexpansion' is not recognized`、参数错位、
  vcvars 退化成 x64。两个来源：Write 工具写出来是 LF；`sed -i` 会把 CRLF 剥成 LF。
  新建或整体重写脚本后补一次检查，修正按字节归一化，别用 sed：

  ```bash
  git ls-files --eol | grep "eol=crlf" | grep "w/lf"     # 该 CRLF 却是 LF
  git ls-files --eol | grep "eol=lf"   | grep "w/crlf"   # 该 LF 却是 CRLF
  ```

- **启动 `.bat` 用绝对路径。** shell 设了 `NoDefaultCurrentDirectoryInExePath` 时，
  `cd` 过去再 `cmd /c xxx.bat` 报 "not recognized"——文件是在的，是 cmd 被禁止在
  当前目录找可执行文件。Bash（MSYS）还会把 `cmd /c` 的 `/c` 转成 `C:\`，要写
  `cmd //c`。两种表现都是"命令跑完了但什么也没干"。

## 写作规范:禁止黑话

文档的默认读者是"会编程、但完全不懂本项目领域"的工程师(比如让前端
工程师读工控协议)。写方案、设计文档、README 时必须遵守:

1. **自造词即定义**:第一次使用自己发明的术语,当场用括号定义。
   ❌"通讯层负责流控"  ✅"通讯层(指协议自带的通用框架层,管会话/路由/
   流控,不含业务逻辑)负责流控"。

2. **缩写必展开**:所有缩写首次出现 = 全称 + 一句人话解释,否则禁用。
   ❌"块采用 TLV 编码"  ✅"块采用 TLV(Type-Length-Value,一种通用
   二进制格式:1 字节类型 + 4 字节长度 + 数据)编码"。

3. **模式必点名**:借鉴现有方案时直接说出原型和出处,禁止换皮造新词
   让读者猜。✅"流控机制类似 HTTP/2 的滑动窗口(RFC 7540)"。

4. **行业黑话加翻译**:跨领域术语首次出现时给圈外人的说法。
   ❌"WS 断开即触发安全联锁"  ✅"WS 断开后立即停机并把输出置为安全值
   (即工控的 fail-safe 惯例)"。

5. **先问题,后方案**:每个机制按"要解决什么问题 → 怎么解决"来讲,
   只描述机制不说明动机的段落视为不合格。

6. **优先用日常词**:用"绑定"不用"锚定",用"丢弃旧数据"不用"按整帧
   淘汰"(除非该短语是领域标准术语且已按第 4 条解释)。

7. **一句一义**:一句话只说一件事;规则超过 3 条必须拆成列表,每条
   独立成句,禁止用分号塞多个条件的超长句。

8. **写完自检**:通读全文,凡是让自己也需要停下来想的句子,重写;
   凡是首次出现的名词,确认它要么已有定义,要么属于通用编程常识。
   数字编号、交叉引用(如"见 §7.6")必须真实存在。
9. "交付前把文档朗读一遍，凡是需要深呼吸才能读完的句子，拆。"


## 杂项（未归类）的规则
- 文档统一存放在`docs/`目录下，按意图放入到不同的目录中
- `clang-tidy`检查在开发机器上太慢了，除非用户明确提示，不然`AI Agent`默认不做`clang-tidy`检查。安装位置:`D:\Qt22\Tools\QtCreator\bin\clang\bin\clang-tidy.exe`
- **头文件顺序有要求的，必须要在包含头文件的地方关掉clang-format**，不然容易被clang-format自动调整头文件顺序导致出问题：尤其是`windows.h`相关的
- **代码注释必须言简意赅**。只写意图，不要写emoji，也无需引用docs目录下的文档，不必为了写注释而注释，一定要简洁，如果非要写一大段注释，那我认为就是代码没写好！！！
- **提交前，改动的 C++ 文件必须先过 clang-format**。clang-fromat安装路径:`D:\Qt22\Tools\QtCreator\bin\clang\bin\clang-format.exe`