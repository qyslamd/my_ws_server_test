#include "common/cef_shell/cef_shell.h"

#include "common/cef_shell/client.h"

namespace {

// 远程调试端口只在调试开关打开时监听；固定值是为了让自动化脚本有处可找。
constexpr int kDevtoolsPort = 9223;

}  // namespace

int RunCefShell(const ShellOptions& options, CefMainArgs& main_args) {
  CefSettings settings;
  settings.no_sandbox = true;
  settings.multi_threaded_message_loop = false;
  settings.windowless_rendering_enabled = false;
  settings.log_severity = LOGSEVERITY_INFO;
  settings.persist_session_cookies = false;
  settings.remote_debugging_port = options.devtools ? kDevtoolsPort : 0;
  if (!options.cache_dir.empty()) {
    CefString(&settings.cache_path) = options.cache_dir;
  }
  if (!options.log_file.empty()) {
    CefString(&settings.log_file) = options.log_file;
  }
  CefString(&settings.locale) = "zh-CN";

  CefRefPtr<ClientApp> app(new ClientApp(options));
  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    return 1;
  }

  CefRunMessageLoop();
  CefShutdown();
  DestroyMainWindow();
  return 0;
}
