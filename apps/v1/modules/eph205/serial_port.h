#pragma once

// 最小 Win32 串口封装：只覆盖 8N1、无流控、按字节收发。
// 读是非阻塞的（无数据立刻返回 0），调用方自己控制等待节奏。

#include <cstdint>
#include <string>
#include <vector>

namespace eph205 {

class SerialPort {
 public:
  SerialPort() = default;
  ~SerialPort();
  SerialPort(const SerialPort&) = delete;
  SerialPort& operator=(const SerialPort&) = delete;

  bool Open(const std::string& port_name, int baud_rate, std::string& err);
  void Close();
  bool is_open() const { return handle_ != nullptr; }
  const std::string& port_name() const { return port_name_; }

  bool Write(const std::uint8_t* data, int len, std::string& err);

  // 最多读 len 字节；timeout_ms 内没有任何数据返回 0。
  int Read(std::uint8_t* out, int len, int timeout_ms);

  // 丢掉接收缓冲里的残留字节。
  void FlushInput();

  // 枚举本机串口（SERIALCOMM + com0com），用于界面下拉框。
  static std::vector<std::string> ListPorts();

 private:
  void* handle_ = nullptr;  // HANDLE
  std::string port_name_;
};

}  // namespace eph205
