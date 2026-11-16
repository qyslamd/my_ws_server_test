#include "common/cef_shell/main_window.h"

#include "common/cef_shell/client.h"
#include "common/log/log.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"

MainWindow::MainWindow(const ShellOptions& options) : options_(options) {}

MainWindow::~MainWindow() = default;

bool MainWindow::Create(HINSTANCE instance) {
  instance_ = instance;

  const std::wstring class_name = CefString(options_.window_class).ToWString();
  const std::wstring title = CefString(options_.title).ToWString();

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = &MainWindow::WndProc;
  wc.hInstance = instance;
  wc.lpszClassName = class_name.c_str();
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hIconSm = wc.hIcon;
  RegisterClassExW(&wc);

  hwnd_ = CreateWindowExW(0, class_name.c_str(), title.c_str(),
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          1280, 800, nullptr, nullptr, instance, this);
  if (hwnd_ == nullptr) {
    return false;
  }
  // 显式再设一次：创建期间的 WM_SETTEXT 依赖默认处理，这里兜底
  SetWindowTextW(hwnd_, title.c_str());

  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);

  RECT rect = {};
  GetClientRect(hwnd_, &rect);

  CefWindowInfo info;
  info.SetAsChild(hwnd_, CefRect(0, 0, rect.right, rect.bottom));
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 245, 246, 250);

  client_ = new ClientHandler(this, options_);
  CefBrowserHost::CreateBrowser(info, client_.get(), options_.url, settings,
                                nullptr, nullptr);
  return true;
}

void MainWindow::SetBrowser(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                     LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    // 要交给默认处理：窗口标题是在创建过程中由 WM_SETTEXT 设进去的
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  auto* self =
      reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  return self->HandleMessage(hwnd, message, wparam, lparam);
}

// 排查关窗问题用：日志统一由 app_log 写 %TEMP%\eph205-client.log
void WriteClientLog(const char* text) { app_log::Info(text); }

void MainWindow::Log(const char* text) { WriteClientLog(text); }

void MainWindow::RequestClose() {
  Log("RequestClose");
  // 只负责把关闭请求交给 CEF；真正的销毁在 DoClose 里做
  if (browser_ && browser_->GetHost()) {
    browser_->GetHost()->CloseBrowser(true);
    return;
  }
  Destroy();
}

void MainWindow::PostDestroy() {
  if (hwnd_) {
    PostMessageW(hwnd_, WM_APP + 1, 0, 0);
  }
}

void MainWindow::Destroy() {
  Log("Destroy");
  if (hwnd_) {
    HWND hwnd = hwnd_;
    hwnd_ = nullptr;
    DestroyWindow(hwnd);
  }
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam) {
  switch (message) {
    case WM_SIZE: {
      // CEF 窗口模式下，必须手动调整子窗口尺寸，WasResized 无效
      if (browser_ && browser_->GetHost()) {
        if (HWND child = browser_->GetHost()->GetWindowHandle()) {
          // 最小化时 lp 是 0×0，SetWindowPos 成 0 尺寸无害；
          // 也可以加 type != SIZE_MINIMIZED 判断后跳过。
          SetWindowPos(child, nullptr, 0, 0, LOWORD(lparam), HIWORD(lparam),
                       SWP_NOZORDER | SWP_NOACTIVATE);
        }
      }
      return 0;
    }
    case WM_ENTERSIZEMOVE:
      if (browser_ && browser_->GetHost()) {
        browser_->GetHost()->NotifyMoveOrResizeStarted();
      }
      return 0;

    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.x = 1024;
      info->ptMinTrackSize.y = 700;
      return 0;
    }

    case WM_CLOSE:
      // 对应 YiGuai 的 closeEvent：只是"请求关闭"，转给 CEF，
      // 窗口等 DoClose 里再销毁
      Log("WM_CLOSE");
      RequestClose();
      return 0;

    case WM_APP + 1:
      Log("WM_APP_CLOSE");
      Destroy();
      return 0;

    case WM_DESTROY:
      Log("WM_DESTROY");
      hwnd_ = nullptr;
      return 0;

    default:
      // 注意用参数里的 hwnd：创建期间 hwnd_ 还没赋值
      return DefWindowProcW(hwnd, message, wparam, lparam);
  }
}
