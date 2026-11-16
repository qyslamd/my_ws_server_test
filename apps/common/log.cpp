#include "common/log.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "spdlog/sinks/msvc_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/spdlog.h"

namespace app_log {
namespace {

// 单份 4 MB × 3 份：客户端排障够用，又不会把用户磁盘写满
constexpr size_t kMaxFileBytes = 4 * 1024 * 1024;
constexpr size_t kMaxFiles = 3;

constexpr char kPattern[] = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";

std::mutex g_mutex;
std::shared_ptr<spdlog::logger> g_logger;
bool g_initialized = false;

std::shared_ptr<spdlog::logger> MakeLogger(
    const std::vector<spdlog::sink_ptr>& sinks) {
  auto logger =
      std::make_shared<spdlog::logger>("app", sinks.begin(), sinks.end());
  logger->set_pattern(kPattern);
  logger->set_level(spdlog::level::debug);
  // warn 及以上立刻落盘；info/debug 交给 spdlog 的周期刷新
  logger->flush_on(spdlog::level::warn);
  return logger;
}

// 取当前 logger；还没初始化就先用只输出调试器的临时实例，
// 保证「日志点比初始化早」时也不会崩、不会丢得无影无踪。
std::shared_ptr<spdlog::logger> Current() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_logger) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    g_logger = MakeLogger(sinks);
  }
  return g_logger;
}

}  // namespace

void Init(const std::string& file_path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized) {
    return;
  }
  g_initialized = true;

  std::vector<spdlog::sink_ptr> sinks;
  sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
  if (!file_path.empty()) {
    try {
      sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          file_path, kMaxFileBytes, kMaxFiles));
    } catch (const spdlog::spdlog_ex&) {
      // 文件开不了不该拖垮客户端：保留调试器输出就够了
    }
  }
  g_logger = MakeLogger(sinks);
}

void Info(const std::string& msg) { Current()->info(msg); }

void Warn(const std::string& msg) { Current()->warn(msg); }

void Error(const std::string& msg) { Current()->error(msg); }

}  // namespace app_log
