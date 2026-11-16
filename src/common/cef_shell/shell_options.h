#ifndef SRC_COMMON_CEF_SHELL_SHELL_OPTIONS_H_
#define SRC_COMMON_CEF_SHELL_SHELL_OPTIONS_H_

#include <string>

// 外壳参数：宿主填，壳不认识具体业务。
struct ShellOptions {
  std::string title;         // 窗口标题；页面改标题不生效
  std::string url;           // 首屏地址
  std::string origin;        // 允许在窗口内导航的前缀，如 http://127.0.0.1:8791
  std::string window_class;  // 窗口类名，单实例激活按它找窗口
  std::string cache_dir;     // CEF 缓存目录，空则用 CEF 默认
  std::string log_file;      // CEF 自己的日志，空则不落盘
  std::string download_dir;  // 下载目录，空则弹另存为
  bool devtools = false;     // 放行 F12 / Ctrl+Shift+I 与远程调试端口
};

#endif  // SRC_COMMON_CEF_SHELL_SHELL_OPTIONS_H_
