// EPH205 桌面客户端：进程组装。
//
// 窗口与浏览器行为都在 cef_shell 库里。这里只做三件事：
//   1. CEF 子进程分流（多进程共用同一个 exe）
//   2. 起进程内的 v1 服务端，把地址交给外壳
//   3. 单实例、关窗后的收尾

// clang-format off
#include <windows.h>
#include <shellapi.h>

// clang-format on

#include <memory>
#include <string>
#include <thread>

#include "base/server.h"
#include "common/cef_shell/cef_shell.h"
#include "common/log/log.h"
#include "eph205/eph205_module.h"
#include "include/cef_app.h"

namespace {

constexpr char kWindowTitle[] = "EP-H205 综合测试系统";
// 单实例激活时按类名找窗口，改这里要同步改 kMutexName 的语义
constexpr char kWindowClass[] = "EPH205MainWindow";
constexpr wchar_t kMutexName[] = L"Local\\EPH205_SingleInstance";

struct Options {
  int port = 8791;
  bool devtools = false;
};

std::string Narrow(const std::wstring& text) {
  if (text.empty()) {
    return {};
  }
  const int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                      static_cast<int>(text.size()), nullptr, 0,
                                      nullptr, nullptr);
  std::string out(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      out.data(), len, nullptr, nullptr);
  return out;
}

std::string ExeDir() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  const size_t pos = dir.find_last_of(L"\\/");
  return Narrow(pos == std::wstring::npos ? dir : dir.substr(0, pos));
}

// 先用宽字符接口取路径再转 UTF-8，中文用户名下不会乱码
std::string ClientLogPath() {
  wchar_t dir[MAX_PATH] = {};
  if (GetTempPathW(MAX_PATH, dir) == 0) {
    return {};
  }
  return Narrow(std::wstring(dir) + L"eph205-client.log");
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
  const std::wstring cls = CefString(kWindowClass).ToWString();
  HWND existing = FindWindowW(cls.c_str(), nullptr);
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

  // 页面与数据都相对 exe 目录找
  SetCurrentDirectoryA(ExeDir().c_str());

  std::string log_err;
  if (!app_log::Init(ClientLogPath(), log_err)) {
    APP_LOG_WARN("MAIN", "日志文件不可用，只写调试器: {}", log_err);
  }

  // 串口是独占资源，只允许一个实例
  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    APP_LOG_INFO("MAIN", "已有实例在运行，激活它");
    ActivateExisting();
    app_log::Shutdown();
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
    APP_LOG_ERROR("MAIN", "端口 {}-{} 都被占用", options.port,
                  options.port + 19);
    MessageBoxW(nullptr, L"无法在本机绑定服务端口（8791-8810 都被占用）。",
                L"EP-H205", MB_ICONERROR | MB_OK);
    if (mutex) {
      ReleaseMutex(mutex);
      CloseHandle(mutex);
    }
    app_log::Shutdown();
    return 1;
  }

  std::thread server_thread([&server] { server->Run(); });
  APP_LOG_INFO("MAIN", "服务端已就绪，端口 {}", actual_port);

  const std::string origin = "http://127.0.0.1:" + std::to_string(actual_port);

  ShellOptions shell;
  shell.title = kWindowTitle;
  shell.url = origin + "/eph205/";
  shell.origin = origin;
  shell.window_class = kWindowClass;
  shell.cache_dir = LocalAppDataDir("cef-cache");
  shell.log_file = JoinPath(LocalAppDataDir("logs"), "cef.log");
  shell.download_dir = JoinPath(ExeDir(), "exports");
  shell.devtools = options.devtools;
  EnsureDir(shell.download_dir);

  const int rc = RunCefShell(shell, main_args);

  // 关窗 -> 浏览器关闭 -> WS 断开 -> 通讯层 SafeStop，这里再显式停一次兜底
  server->Stop();
  server_thread.join();
  APP_LOG_INFO("MAIN", "窗口已关闭，退出码 {}", rc);

  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  app_log::Shutdown();
  return rc;
}
