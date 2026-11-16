#include "common/log.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "gtest/gtest.h"

namespace {

std::string ReadAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

TEST(AppLog, WritesLevelAndMessageToFile) {
  const std::string path = "app_log_test.log";
  std::error_code ec;
  std::filesystem::remove(path, ec);

  app_log::Init(path);
  app_log::Info("hello 日志");
  app_log::Warn("warn 走一遍");
  app_log::Error("boom");

  ASSERT_TRUE(std::filesystem::exists(path));
  const std::string text = ReadAll(path);
  EXPECT_NE(std::string::npos, text.find("[info] hello 日志"));
  EXPECT_NE(std::string::npos, text.find("[warning] warn 走一遍"));
  EXPECT_NE(std::string::npos, text.find("[error] boom"));
}

}  // namespace
