#include "common/log/log.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "gtest/gtest.h"

namespace {

std::string ReadAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

// 每个用例用独立文件，避免残留内容让断言误判
void RemoveFile(const std::string& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

}  // namespace

TEST(AppLog, WritesLevelModuleAndMessage) {
  const std::string path = "app_log_test_basic.log";
  RemoveFile(path);

  app_log::Options options;
  options.file_path = path;
  options.level = app_log::Level::kDebug;
  std::string err;
  ASSERT_TRUE(app_log::Init(options, err)) << err;

  APP_LOG_INFO("EPH205", "连接 {} 成功", "COM7");
  APP_LOG_WARN("SERIAL", "应答超时 {} ms", 200);
  app_log::Shutdown();

  const std::string text = ReadAll(path);
  EXPECT_NE(std::string::npos, text.find("[info] [EPH205] 连接 COM7 成功"));
  EXPECT_NE(std::string::npos, text.find("[warning] [SERIAL] 应答超时 200 ms"));
}

TEST(AppLog, LevelFilterDropsLowerLevels) {
  const std::string path = "app_log_test_level.log";
  RemoveFile(path);

  app_log::Options options;
  options.file_path = path;
  options.level = app_log::Level::kWarn;
  std::string err;
  ASSERT_TRUE(app_log::Init(options, err)) << err;

  APP_LOG_INFO("EPH205", "这条不该出现");
  APP_LOG_ERROR("EPH205", "这条要留下");
  app_log::Shutdown();

  const std::string text = ReadAll(path);
  EXPECT_EQ(std::string::npos, text.find("这条不该出现"));
  EXPECT_NE(std::string::npos, text.find("这条要留下"));
}

TEST(AppLog, ReplaysLogsWrittenBeforeInit) {
  const std::string path = "app_log_test_early.log";
  RemoveFile(path);

  APP_LOG_INFO("EARLY", "第 {} 条", 1);

  std::string err;
  ASSERT_TRUE(app_log::Init(path, err)) << err;
  app_log::Shutdown();

  const std::string text = ReadAll(path);
  EXPECT_NE(std::string::npos, text.find("[启动期] 第 1 条"));
}

TEST(AppLog, ReturnsFalseAndKeepsLoggingWhenPathUnusable) {
  // 拿一个普通文件当目录用，两条候选路径都打不开
  const std::string blocker = "app_log_test_blocker";
  {
    std::ofstream out(blocker);
    out << "x";
  }

  app_log::Options options;
  options.file_path = blocker + "\\nested.log";
  std::string err;
  EXPECT_FALSE(app_log::Init(options, err));
  EXPECT_FALSE(err.empty());

  // 降级成只写调试器，但调用点不该崩
  APP_LOG_ERROR("EPH205", "文件开不了也要能打出去");
  app_log::Shutdown();

  RemoveFile(blocker);
}
