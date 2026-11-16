#include "common/log/log.h"

#include <process.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <spdlog/async.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace app_log {
namespace {

// 默认不含源位置；排查时把 %s:%# 加进来即可，Emit 一直在传
constexpr char kPattern[] = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";
// 队列满时阻塞而不是丢日志：日志丢了下游排查就断了
constexpr std::size_t kQueueSize = 8192;
// 启动期最多攒这么多条，满了丢新的：最早的几条通常最要紧
constexpr std::size_t kMaxPending = 256;

struct Pending {
  Level level;
  std::string module;
  SourceLoc loc;
  std::string text;
};

std::mutex g_mutex;
std::shared_ptr<spdlog::logger> g_logger;
bool g_initialized = false;
std::atomic<int> g_level{static_cast<int>(Level::kInfo)};
std::vector<Pending> g_pending;

spdlog::level::level_enum ToSpdlog(Level level) {
  switch (level) {
    case Level::kDebug:
      return spdlog::level::debug;
    case Level::kWarn:
      return spdlog::level::warn;
    case Level::kError:
      return spdlog::level::err;
    case Level::kInfo:
      break;
  }
  return spdlog::level::info;
}

std::filesystem::path ParentDir(const std::filesystem::path& file) {
  return file.has_parent_path() ? file.parent_path()
                                : std::filesystem::path(".");
}

// 删掉同名前缀且超期的日志。日志目录是 %TEMP% 这类公共目录，
// 所以只认自己的前缀，不碰别人的 *.log。
void RemoveExpiredLogs(const std::filesystem::path& dir,
                       const std::string& prefix, int keep_days) {
  if (keep_days <= 0) {
    return;
  }
  namespace fs = std::filesystem;
  // 轮转出来的旧文件叫 <名字>.1.log，stem 仍以原名开头，前缀匹配够用
  const auto now = fs::file_time_type::clock::now();
  std::error_code iter_ec;
  for (fs::directory_iterator it(dir, iter_ec), end; !iter_ec && it != end;
       it.increment(iter_ec)) {
    const fs::directory_entry& entry = *it;
    std::error_code ec;
    if (!entry.is_regular_file(ec) || ec) {
      continue;
    }
    const fs::path& path = entry.path();
    if (path.extension() != ".log" ||
        path.stem().string().rfind(prefix, 0) != 0) {
      continue;
    }
    const fs::file_time_type mtime = fs::last_write_time(path, ec);
    if (ec) {
      continue;
    }
    const auto age =
        std::chrono::duration_cast<std::chrono::hours>(now - mtime);
    if (age.count() / 24 > keep_days) {
      std::error_code remove_ec;
      fs::remove(path, remove_ec);
    }
  }
}

// 打不开就退到带进程号的独立文件：多实例或文件被占用时，
// 轮转要重命名被占用的文件会抛异常，不能让它把程序带走。
spdlog::sink_ptr OpenFileSink(const Options& options, std::string& err) {
  namespace fs = std::filesystem;
  const fs::path target(options.file_path);
  const fs::path dir = ParentDir(target);

  // rotating_file_sink 自己不建目录，目录不在就会抛异常并被降级吞掉
  std::error_code ec;
  fs::create_directories(dir, ec);
  RemoveExpiredLogs(dir, target.stem().string(), options.keep_days);

  const std::string pid = std::to_string(_getpid());
  const std::vector<std::string> candidates = {
      options.file_path,
      (dir / (target.stem().string() + "-" + pid + ".log")).string(),
  };
  for (const std::string& candidate : candidates) {
    try {
      return std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          candidate, options.max_bytes, options.max_files);
    } catch (const spdlog::spdlog_ex& e) {
      err = e.what();
    }
  }
  return nullptr;
}

void ReplayPending() {
  std::vector<Pending> pending;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    pending.swap(g_pending);
  }
  // 标出来源：这些行的时间早于初始化完成，不标记会被当成之后发生的
  for (const Pending& record : pending) {
    Emit(record.level, record.module.c_str(), record.loc,
         "[启动期] " + record.text);
  }
}

}  // namespace

bool Init(const Options& options, std::string& err) {
  err.clear();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
      return true;
    }
    g_initialized = true;
  }
  g_level.store(static_cast<int>(options.level));

  std::vector<spdlog::sink_ptr> sinks;
  sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());

  bool file_ok = true;
  if (!options.file_path.empty()) {
    spdlog::sink_ptr file_sink = OpenFileSink(options, err);
    file_ok = file_sink != nullptr;
    if (file_ok) {
      sinks.push_back(std::move(file_sink));
    }
  }

  spdlog::init_thread_pool(kQueueSize, 1);
  auto logger = std::make_shared<spdlog::async_logger>(
      "app", sinks.begin(), sinks.end(), spdlog::thread_pool(),
      spdlog::async_overflow_policy::block);
  logger->set_pattern(kPattern);
  logger->set_level(ToSpdlog(options.level));
  // warning 及以上立刻落盘，info 交给每秒一次的周期冲刷
  logger->flush_on(spdlog::level::warn);
  spdlog::register_logger(logger);
  spdlog::set_default_logger(logger);
  spdlog::flush_every(std::chrono::seconds(1));

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_logger = std::move(logger);
  }
  ReplayPending();
  return file_ok;
}

bool Init(const std::string& file_path, std::string& err) {
  Options options;
  options.file_path = file_path;
  return Init(options, err);
}

void SetLevel(Level level) {
  g_level.store(static_cast<int>(level));
  std::shared_ptr<spdlog::logger> logger;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    logger = g_logger;
  }
  if (logger) {
    logger->set_level(ToSpdlog(level));
  }
}

bool IsEnabled(Level level) {
  return static_cast<int>(level) >= g_level.load(std::memory_order_relaxed);
}

void Shutdown() {
  std::shared_ptr<spdlog::logger> logger;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    logger = std::move(g_logger);
    g_logger.reset();
    g_initialized = false;
    g_pending.clear();
    // 回到默认级别：下次 Init 之前打的日志按默认级别判定，不带上一次的设置
    g_level.store(static_cast<int>(Level::kInfo));
  }
  if (logger) {
    logger->flush();
  }
  spdlog::shutdown();
}

void Emit(Level level, const char* module, SourceLoc loc,
          const std::string& text) {
  std::shared_ptr<spdlog::logger> logger;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_logger) {
      if (g_pending.size() < kMaxPending) {
        g_pending.push_back(
            Pending{level, module != nullptr ? module : "", loc, text});
      }
      return;
    }
    logger = g_logger;
  }

  std::string line;
  line.reserve(text.size() + 16);
  if (module != nullptr && *module != '\0') {
    line += '[';
    line += module;
    line += "] ";
  }
  line += text;
  logger->log(spdlog::source_loc{loc.file, loc.line, ""}, ToSpdlog(level),
              line);
}

}  // namespace app_log
