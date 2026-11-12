#pragma once

// 一个最小的 Win32 窗口：没有菜单栏、没有工具栏、没有地址栏。
// 整个客户区就是一个 CEF 子窗口。

#include <windows.h>

#include <string>

#include "app_options.h"
#include "include/cef_browser.h"

class ClientHandler;

class MainWindow {
 public:
  explicit MainWindow(const AppOptions& options);
  ~MainWindow();

  bool Create(HINSTANCE instance);

  void SetBrowser(CefRefPtr<CefBrowser> browser);
  CefRefPtr<CefBrowser> browser() const { return browser_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  AppOptions options_;
  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<ClientHandler> client_;
};
