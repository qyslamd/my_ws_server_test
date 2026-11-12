// EPH205 桌面客户端：一个 Win32 窗口 + 内嵌 CEF，页面来自本进程里的服务端。
//
// 进程结构：
//   UI 线程      CEF 消息循环 + 窗口
//   server 线程  v1 底座（HTTP/WS）
//   其余线程     由业务模块自己管理（串口、仿真）
//
// 关窗顺序：CloseBrowser → OnBeforeClose → CefQuitMessageLoop →
//           server->Stop() → join → CefShutdown

#include <windows.h>
#include <shellapi.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "base/server.h"
#include "client.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "main_window.h"
#include "modules/eph205/eph205_module.h"

namespace {

struct Options {
  int port = 8791;
  bool devtools = false;
};

std::string Narrow(const std::wstring& text) {
  if (text.empty()) {
    return {};
  }
  const int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                      static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len,
                      nullptr, nullptr);
  return out;
}

std::string ExeDir() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  const size_t pos = dir.find_last_of(L"\\/");
  return Narrow(pos == std::wstring::npos ? dir : dir.substr(0, pos));
}

std::string JoinPath(const std::string& a, const std::string& b) {
  if (a.empty()) {
    return b;
  }
  std::string out = a;
  if (out.back() != '\\' && out.back() != '/') {
    out += '\\';
  }
  return out + b;
}

void EnsureDir(const std::string& path) {
  std::string current;
  for (size_t i = 0; i < path.size(); ++i) {
    current.push_back(path[i]);
    if (path[i] == '\\' || path[i] == '/') {
      CreateDirectoryA(current.c_str(), nullptr);
    }
  }
  CreateDirectoryA(path.c_str(), nullptr);
}

std::string LocalAppDataDir(const std::string& sub) {
  char buf[MAX_PATH] = {};
  const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH);
  const std::string base = n > 0 ? std::string(buf, n) : ExeDir();
  const std::string dir = JoinPath(JoinPath(base, "EPH205"), sub);
  EnsureDir(dir);
  return dir;
}

Options ParseOptions() {
  Options options;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--devtools") {
      options.devtools = true;
    } else if (arg.rfind(L"--port=", 0) == 0) {
      options.port = std::stoi(arg.substr(7));
    }
  }
  if (argv) {
    LocalFree(argv);
  }
  return options;
}

// 第二个实例不再开窗口，把已经在跑的那个激活
bool ActivateExisting() {
  HWND existing = FindWindowW(L"EPH205MainWindow", nullptr);
  if (existing == nullptr) {
    return false;
  }
  ShowWindow(existing, SW_RESTORE);
  SetForegroundWindow(existing);
  return true;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  // CEF 是多进程：子进程（渲染/GPU）用同一个 exe 启动，必须第一时间分流
  CefMainArgs main_args(instance);
  const int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  const Options options = ParseOptions();

  SetCurrentDirectoryA(ExeDir().c_str());

  // 串口是独占资源，只允许一个实例
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\EPH205_SingleInstance");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    ActivateExisting();
    return 0;
  }

  // 服务端：端口被占就往后顺延
  auto module = std::make_unique<eph205::Eph205Module>();
  std::unique_ptr<v1::V1Server> server;
  int actual_port = 0;
  for (int port = options.port; port < options.port + 20; ++port) {
    auto candidate = std::make_unique<v1::V1Server>(port);
    candidate->RegisterModule(module.get());
    if (candidate->Start()) {
      server = std::move(candidate);
      actual_port = port;
      break;
    }
  }
  if (!server) {
    MessageBoxW(nullptr, L"无法在本机绑定服务端口（8791-8810 都被占用）。", L"EP-H205",
                MB_ICONERROR | MB_OK);
    return 1;
  }

  std::thread server_thread([&server] { server->Run(); });

  AppOptions app_options;
  app_options.port = actual_port;
  app_options.devtools = options.devtools;
  app_options.exports_dir = JoinPath(ExeDir(), "exports");
  EnsureDir(app_options.exports_dir);

  CefSettings settings;
  settings.no_sandbox = true;
  settings.multi_threaded_message_loop = false;
  settings.windowless_rendering_enabled = false;
  settings.log_severity = LOGSEVERITY_INFO;
  settings.persist_session_cookies = false;
  settings.remote_debugging_port = options.devtools ? 9223 : 0;
  CefString(&settings.cache_path) = LocalAppDataDir("cef-cache");
  CefString(&settings.log_file) = JoinPath(LocalAppDataDir("logs"), "cef.log");
  CefString(&settings.locale) = "zh-CN";

  CefRefPtr<ClientApp> app(new ClientApp(app_options));
  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    MessageBoxW(nullptr, L"CEF 初始化失败，请查看 logs\\cef.log。", L"EP-H205",
                MB_ICONERROR | MB_OK);
    server->Stop();
    server_thread.join();
    return 1;
  }

  CefRunMessageLoop();

  // 关窗 -> 浏览器关闭 -> WS 断开 -> 底座 SafeStop，这里再显式停一次兜底
  server->Stop();
  server_thread.join();

  CefShutdown();
  DestroyMainWindow();

  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  return 0;
}
