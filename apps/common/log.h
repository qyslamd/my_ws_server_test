#ifndef APPS_COMMON_LOG_H_
#define APPS_COMMON_LOG_H_

#include <string>

// 极薄的日志门面：spdlog 只出现在 log.cpp 里。
//
// 为什么不让 spdlog 头走头文件：#include <spdlog/spdlog.h> 的翻译单元
// 实测比裸 TU 多花约 2.9 秒（模板 + 捆绑的 fmt）。项目里只有这一层
// 碰 spdlog，别的地方包含本文件不会有这个代价。
namespace app_log {

// 初始化：写滚动文件 + 同时输出到调试器（OutputDebugString）。
//
// file_path 为空表示只输出到调试器。重复调用只生效第一次；文件打不开
// （路径非法、权限不足）时自动降级为只输出调试器，不抛异常。
void Init(const std::string& file_path);

// 未初始化前调用不会崩，消息只走调试器。
void Info(const std::string& msg);
void Warn(const std::string& msg);
void Error(const std::string& msg);

}  // namespace app_log

#endif  // APPS_COMMON_LOG_H_
