#pragma once

// 一个最小的 Win32 窗口：没有菜单栏、没有工具栏、没有地址栏。
// 整个客户区就是一个 CEF 子窗口。
// clang-format off
#include <windows.h>

// clang-format on

#include <string>

#include "app_options.h"
#include "include/cef_browser.h"

class ClientHandler;

// 排障日志：%TEMP%/eph205-client.log
void WriteClientLog(const char* text);

class MainWindow {
 public:
  explicit MainWindow(const AppOptions& options);
  ~MainWindow();

  bool Create(HINSTANCE instance);

  void SetBrowser(CefRefPtr<CefBrowser> browser);
  CefRefPtr<CefBrowser> browser() const { return browser_; }

  // 关窗：先让浏览器关，浏览器关了再真正销毁窗口
  void RequestClose();
  // 延后一拍销毁（对齐 Qt 版 DoClose 里 close() 的时序，避免在 CEF 回调里重入）
  void PostDestroy();
  void Destroy();

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  void Log(const char* text);

  AppOptions options_;
  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<ClientHandler> client_;
  bool closing_ = false;
};
