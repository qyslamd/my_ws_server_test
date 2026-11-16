#pragma once

#include <string>

// 客户端运行参数：由 main.cpp 解析，传给 App / Handler
struct AppOptions {
  int port = 0;             // 本进程服务端的实际端口
  bool devtools = false;    // F12 / 远程调试是否放行
  std::string exports_dir;  // CSV 导出落盘目录
};
