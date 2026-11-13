#include "main_window.h"

#include <cstdio>
#include <ctime>

#include "client.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"

namespace {

const wchar_t kClassName[] = L"EPH205MainWindow";
const wchar_t kTitle[] = L"EP-H205 综合测试系统";

}  // namespace

MainWindow::MainWindow(const AppOptions& options) : options_(options) {}

MainWindow::~MainWindow() = default;

bool MainWindow::Create(HINSTANCE instance) {
  instance_ = instance;

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = &MainWindow::WndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kClassName;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hIconSm = wc.hIcon;
  RegisterClassExW(&wc);

  hwnd_ = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, nullptr,
                          nullptr, instance, this);
  if (hwnd_ == nullptr) {
    return false;
  }
  // 显式再设一次：创建期间的 WM_SETTEXT 依赖默认处理，这里兜底
  SetWindowTextW(hwnd_, kTitle);

  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);

  RECT rect = {};
  GetClientRect(hwnd_, &rect);

  CefWindowInfo info;
  info.SetAsChild(hwnd_, CefRect(0, 0, rect.right, rect.bottom));
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 245, 246, 250);

  client_ = new ClientHandler(this, options_);
  const std::string url =
      "http://127.0.0.1:" + std::to_string(options_.port) + "/eph205/";
  CefBrowserHost::CreateBrowser(info, client_.get(), url, settings, nullptr,
                                nullptr);
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

// 排查关窗问题用：写到 %TEMP%\eph205-client.log
void WriteClientLog(const char* text) {
  char dir[MAX_PATH] = {};
  GetTempPathA(MAX_PATH, dir);
  std::string file = std::string(dir) + "eph205-client.log";
  FILE* fp = nullptr;
  if (fopen_s(&fp, file.c_str(), "a") == 0 && fp) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    fprintf(fp, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, text);
    fclose(fp);
  }
}

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
