#include "client.h"

#include <windows.h>
#include <shellapi.h>

#include <memory>

#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"
#include "main_window.h"

namespace {

std::unique_ptr<MainWindow> g_window;

std::string ToUtf8(const CefString& text) { return text.ToString(); }

}  // namespace

MainWindow* GetMainWindow() { return g_window.get(); }

void DestroyMainWindow() { g_window.reset(); }

// ---------------------------------------------------------------- ClientHandler

ClientHandler::ClientHandler(MainWindow* window, const AppOptions& options)
    : window_(window), options_(options) {}

bool ClientHandler::IsInternalUrl(const std::string& url) const {
  const std::string prefix = "http://127.0.0.1:" + std::to_string(options_.port);
  return url.rfind(prefix, 0) == 0;
}

// 浏览器快捷键一律拦掉：刷新/打印/另存为/查看源码/缩放/开发者工具
bool ClientHandler::IsBlockedKey(const CefKeyEvent& event) const {
  const bool ctrl = (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
  const bool shift = (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;

  switch (event.windows_key_code) {
    case VK_F5:
    case VK_BROWSER_REFRESH:
    case VK_BROWSER_BACK:
    case VK_BROWSER_FORWARD:
      return true;
    case VK_F12:
      return !options_.devtools;
    case VK_OEM_PLUS:
    case VK_OEM_MINUS:
    case VK_ADD:
    case VK_SUBTRACT:
    case '0':
      return ctrl;  // 缩放
    default:
      break;
  }
  if (!ctrl) {
    return false;
  }
  switch (event.windows_key_code) {
    case 'R':  // 刷新 / 强制刷新
    case 'P':  // 打印
    case 'S':  // 另存为
    case 'U':  // 查看源码
    case 'J':  // 下载页
      return true;
    case 'I':  // 开发者工具
      return shift && !options_.devtools;
    default:
      return false;
  }
}

bool ClientHandler::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  const CefString& target_url,
                                  const CefString& target_frame_name,
                                  CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                                  bool user_gesture, const CefPopupFeatures& popupFeatures,
                                  CefWindowInfo& windowInfo, CefRefPtr<CefClient>& client,
                                  CefBrowserSettings& settings,
                                  CefRefPtr<CefDictionaryValue>& extra_info,
                                  bool* no_javascript_access) {
  // 客户端不弹新窗口
  return true;
}

void ClientHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (window_) {
    window_->SetBrowser(browser);
  }
}

bool ClientHandler::DoClose(CefRefPtr<CefBrowser> browser) {
  WriteClientLog("DoClose");
  // 学 YiGuai：Windows 上返回 true，告诉 CEF"别再给顶层窗口发关闭事件了"，
  // 窗口由我们自己销毁。Qt 版在这里调 close()，我们延后一拍 DestroyWindow。
  // 多窗口场景下返回 false 会让 CEF 逐个给顶层窗口发关闭事件，容易误关别的窗口。
  if (window_) {
    window_->PostDestroy();
  }
  return true;
}

void ClientHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  WriteClientLog("OnBeforeClose");
  if (window_) {
    window_->SetBrowser(nullptr);
    window_->Destroy();   // 浏览器没了，父窗口也一起收掉
  }
  CefQuitMessageLoop();
}

void ClientHandler::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefContextMenuParams> params,
                                        CefRefPtr<CefMenuModel> model) {
  // 右键菜单里全是"刷新/另存为/检查"，一律清空
  model->Clear();
}

bool ClientHandler::OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                                  CefEventHandle os_event, bool* is_keyboard_shortcut) {
  if (event.type != KEYEVENT_RAWKEYDOWN && event.type != KEYEVENT_KEYDOWN) {
    return false;
  }
  if (IsBlockedKey(event)) {
    return true;  // 已处理，不再下发
  }
  return false;
}

bool ClientHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request, bool user_gesture,
                                   bool is_redirect) {
  const std::string url = ToUtf8(request->GetURL());
  if (IsInternalUrl(url)) {
    return false;  // 放行
  }
  // 外链交给系统浏览器，窗口里不导航
  if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
    const std::wstring wide = CefString(url).ToWString();
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
  return true;
}

bool ClientHandler::OnCertificateError(CefRefPtr<CefBrowser> browser, cef_errorcode_t cert_error,
                                       const CefString& request_url,
                                       CefRefPtr<CefSSLInfo> ssl_info,
                                       CefRefPtr<CefCallback> callback) {
  return true;  // 拒绝，不弹证书选择
}

void ClientHandler::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                              TerminationStatus status) {
  // 渲染进程崩了就重新加载一次，别停在白屏
  if (browser) {
    browser->GetMainFrame()->LoadURL(browser->GetMainFrame()->GetURL());
  }
}

bool ClientHandler::OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString& origin_url,
                               JSDialogType dialog_type, const CefString& message_text,
                               const CefString& default_prompt_text,
                               CefRefPtr<CefJSDialogCallback> callback,
                               bool& suppress_message) {
  // 页面里没有原生对话框，出现就说明是意外，直接吞掉
  suppress_message = true;
  return true;
}

bool ClientHandler::OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser, uint64_t prompt_id,
                                           const CefString& requesting_origin,
                                           uint32_t requested_permissions,
                                           CefRefPtr<CefPermissionPromptCallback> callback) {
  if (callback) {
    callback->Continue(CEF_PERMISSION_RESULT_DENY);
  }
  return true;
}

bool ClientHandler::OnDragEnter(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDragData> drag_data,
                                DragOperationsMask mask) {
  return true;  // 禁止把文件拖进窗口
}

void ClientHandler::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefDownloadItem> download_item,
                                     const CefString& suggested_name,
                                     CefRefPtr<CefBeforeDownloadCallback> callback) {
  if (!callback) {
    return;
  }
  std::string name = ToUtf8(suggested_name);
  if (name.empty()) {
    name = "export.csv";
  }
  std::string path = options_.exports_dir;
  if (!path.empty() && path.back() != '\\' && path.back() != '/') {
    path += '\\';
  }
  path += name;
  callback->Continue(CefString(path), false);  // false = 不弹另存为
}

// ---------------------------------------------------------------- ClientApp

void ClientApp::OnBeforeCommandLineProcessing(const CefString& process_type,
                                              CefRefPtr<CefCommandLine> command_line) {
  if (process_type.empty()) {
    // 浏览器进程才加这些开关
    command_line->AppendSwitch("no-first-run");
    command_line->AppendSwitch("no-default-browser-check");
    command_line->AppendSwitch("disable-pinch");
    command_line->AppendSwitchWithValue("disable-features",
                                        "Translate,AutofillServerCommunication");
    if (!options_.devtools) {
      command_line->AppendSwitch("disable-dev-shm-usage");
    }
  }
}

void ClientApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  g_window = std::make_unique<MainWindow>(options_);
  g_window->Create(GetModuleHandle(nullptr));
}
