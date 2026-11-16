#ifndef SRC_COMMON_CEF_SHELL_CEF_SHELL_H_
#define SRC_COMMON_CEF_SHELL_CEF_SHELL_H_

#include <windows.h>

#include "common/cef_shell/shell_options.h"
#include "include/cef_app.h"

// 在 CefExecuteProcess 之后调用。
// 内部完成 CefInitialize、开窗、跑消息循环；窗口关闭后 CefShutdown
// 并返回退出码。
int RunCefShell(const ShellOptions& options, CefMainArgs& main_args);

#endif  // SRC_COMMON_CEF_SHELL_CEF_SHELL_H_
