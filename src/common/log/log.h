#ifndef SRC_COMMON_LOG_LOG_H_
#define SRC_COMMON_LOG_LOG_H_

#include <cstddef>
#include <spdlog/fmt/fmt.h>
#include <string>
#include <utility>

// 日志门面。spdlog 的 logger 与 sink 只出现在 log.cpp 里；头文件只借它的
// 格式化器，让调用点写 {} 变参而不用自己拼字符串。
//
// 为什么不全借：实测每个翻译单元 <spdlog/fmt/fmt.h> 多花约 0.7 秒，
// <spdlog/spdlog.h> 多花约 2.7 秒。格式化值这个价，logger 不值。
namespace app_log {

enum class Level { kDebug, kInfo, kWarn, kError };

// 调用点位置，由下面的宏自动填
struct SourceLoc {
  const char* file = "";
  int line = 0;
};

struct Options {
  // 空表示只输出到调试器（OutputDebugString）
  std::string file_path;
  std::size_t max_bytes = 4 * 1024 * 1024;
  std::size_t max_files = 3;
  // 启动时删掉同名前缀、超过这么多天的旧日志；<= 0 表示不清理
  int keep_days = 7;
  Level level = Level::kInfo;
};

// Init 之前打的日志先缓存，初始化后按原顺序补写（前缀 [启动期]）。
//
// 文件开不了时不抛异常也不罢工：降级为只输出调试器，返回 false 并在 err
// 里说明原因，调用方可以照常继续。重复调用只生效第一次。
bool Init(const Options& options, std::string& err);
bool Init(const std::string& file_path, std::string& err);

// 运行期调整级别；已经打出去的照旧
void SetLevel(Level level);
bool IsEnabled(Level level);

// 冲刷队列后关闭。之后再打的日志只会进内存缓冲，不再落盘
void Shutdown();

// 下面两个是宏展开用的，不要直接调
void Emit(Level level, const char* module, SourceLoc loc,
          const std::string& text);

template <typename... Args>
void Log(Level level, const char* module, SourceLoc loc,
         fmt::format_string<Args...> pattern, Args&&... args) {
  if (!IsEnabled(level)) {
    return;
  }
  Emit(level, module, loc, fmt::format(pattern, std::forward<Args>(args)...));
}

}  // namespace app_log

// module 用短模块名，如 "EPH205"、"CEF"、"SERIAL"，会出现在每条日志前面。
// 格式串是 fmt 语法（{} 占位），写错在编译期就报错。
#define APP_LOG_DEBUG(module, ...)                 \
  ::app_log::Log(::app_log::Level::kDebug, module, \
                 ::app_log::SourceLoc{__FILE__, __LINE__}, __VA_ARGS__)

#define APP_LOG_INFO(module, ...)                 \
  ::app_log::Log(::app_log::Level::kInfo, module, \
                 ::app_log::SourceLoc{__FILE__, __LINE__}, __VA_ARGS__)

#define APP_LOG_WARN(module, ...)                 \
  ::app_log::Log(::app_log::Level::kWarn, module, \
                 ::app_log::SourceLoc{__FILE__, __LINE__}, __VA_ARGS__)

#define APP_LOG_ERROR(module, ...)                 \
  ::app_log::Log(::app_log::Level::kError, module, \
                 ::app_log::SourceLoc{__FILE__, __LINE__}, __VA_ARGS__)

#endif  // SRC_COMMON_LOG_LOG_H_
