#pragma once

// 极小的注册表读取工具，只为枚举串口与 com0com 端口对。
// 只依赖 windows.h，不引入任何第三方库。

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

namespace eph205::winreg {

// 枚举某个键下所有 REG_SZ 值，返回 (值名, 数据)。
std::vector<std::pair<std::string, std::string>> EnumValues(HKEY root,
                                                            const char* path);

// 枚举子键名。
std::vector<std::string> EnumSubKeys(HKEY root, const char* path);

// 读一个字符串值；不存在返回空串。
std::string ReadString(HKEY root, const char* path, const char* name);

}  // namespace eph205::winreg
