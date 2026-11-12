#pragma once

// CEF 的 App / Client / Handler。
// 只实现这个客户端需要的部分：不要浏览器外壳，只保留"显示页面"的能力。

#include <string>

#include "app_options.h"
#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_drag_handler.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request_handler.h"

class MainWindow;

// main.cpp 用来在退出时收掉窗口对象
MainWindow* GetMainWindow();
void DestroyMainWindow();

class ClientHandler : public CefClient,
                      public CefLifeSpanHandler,
                      public CefDisplayHandler,
                      public CefContextMenuHandler,
                      public CefKeyboardHandler,
                      public CefRequestHandler,
                      public CefJSDialogHandler,
                      public CefPermissionHandler,
                      public CefDragHandler,
                      public CefDownloadHandler {
 public:
  ClientHandler(MainWindow* window, const AppOptions& options);

  // CefClient
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }

  // CefLifeSpanHandler
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     const CefString& target_url, const CefString& target_frame_name,
                     CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                     bool user_gesture, const CefPopupFeatures& popupFeatures,
                     CefWindowInfo& windowInfo, CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings, CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefDisplayHandler：标题固定，不跟随页面
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {}

  // CefContextMenuHandler：清空菜单（刷新/另存为/检查都在里面）
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;

  // CefKeyboardHandler：拦截浏览器快捷键
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                     CefEventHandle os_event, bool* is_keyboard_shortcut) override;

  // CefRequestHandler：只放行本机页面
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool user_gesture,
                      bool is_redirect) override;
  bool OnCertificateError(CefRefPtr<CefBrowser> browser, cef_errorcode_t cert_error,
                          const CefString& request_url, CefRefPtr<CefSSLInfo> ssl_info,
                          CefRefPtr<CefCallback> callback) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status) override;

  // CefJSDialogHandler：吞掉 alert/confirm/prompt
  bool OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString& origin_url,
                  JSDialogType dialog_type, const CefString& message_text,
                  const CefString& default_prompt_text,
                  CefRefPtr<CefJSDialogCallback> callback,
                  bool& suppress_message) override;

  // CefPermissionHandler：一律拒绝
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser, uint64_t prompt_id,
                              const CefString& requesting_origin,
                              uint32_t requested_permissions,
                              CefRefPtr<CefPermissionPromptCallback> callback) override;

  // CefDragHandler：禁止把文件拖进窗口
  bool OnDragEnter(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDragData> drag_data,
                   DragOperationsMask mask) override;

  // CefDownloadHandler：CSV 导出落到固定目录
  void OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;

  bool IsInternalUrl(const std::string& url) const;
  bool IsBlockedKey(const CefKeyEvent& event) const;

 private:
  MainWindow* window_;
  AppOptions options_;

  IMPLEMENT_REFCOUNTING(ClientHandler);
  DISALLOW_COPY_AND_ASSIGN(ClientHandler);
};

// 进程级 App：负责在上下文初始化后开窗口
class ClientApp : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit ClientApp(const AppOptions& options) : options_(options) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;
  void OnContextInitialized() override;

 private:
  AppOptions options_;
  IMPLEMENT_REFCOUNTING(ClientApp);
  DISALLOW_COPY_AND_ASSIGN(ClientApp);
};
