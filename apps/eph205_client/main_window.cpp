#include "main_window.h"

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

  hwnd_ = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                          CW_USEDEFAULT, 1280, 800, nullptr, nullptr, instance, this);
  if (hwnd_ == nullptr) {
    return false;
  }

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
  CefBrowserHost::CreateBrowser(info, client_.get(), url, settings, nullptr, nullptr);
  return true;
}

void MainWindow::SetBrowser(CefRefPtr<CefBrowser> browser) { browser_ = browser; }

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      if (browser_ && browser_->GetHost()) {
        browser_->GetHost()->WasResized();
      }
      return 0;

    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.x = 1024;
      info->ptMinTrackSize.y = 700;
      return 0;
    }

    case WM_CLOSE:
      if (browser_ && browser_->GetHost()) {
        browser_->GetHost()->CloseBrowser(true);
        return 0;  // 等 OnBeforeClose 再收尾
      }
      DestroyWindow(hwnd_);
      return 0;

    case WM_DESTROY:
      hwnd_ = nullptr;
      return 0;

    default:
      return DefWindowProcW(hwnd_, message, wparam, lparam);
  }
}
