#include "serial_port.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include "win_registry.h"

namespace eph205 {

namespace {

constexpr int kPollMs = 1;

std::string WinErrorText(DWORD code) {
  char* text = nullptr;
  const DWORD len = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<char*>(&text), 0, nullptr);
  std::string out;
  if (len > 0 && text != nullptr) {
    out.assign(text, len);
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) {
      out.pop_back();
    }
  } else {
    out = "win32 error " + std::to_string(code);
  }
  if (text != nullptr) {
    LocalFree(text);
  }
  return out;
}

}  // namespace

SerialPort::~SerialPort() { Close(); }

bool SerialPort::Open(const std::string& port_name, int baud_rate,
                      std::string& err) {
  Close();
  // COM10 以上必须用 \\.\ 前缀，统一都加上
  const std::string path = "\\\\.\\" + port_name;
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                         nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    err = "打开 " + port_name + " 失败: " + WinErrorText(GetLastError());
    return false;
  }

  DCB dcb = {};
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(h, &dcb)) {
    err = "读取串口参数失败: " + WinErrorText(GetLastError());
    CloseHandle(h);
    return false;
  }
  dcb.BaudRate = static_cast<DWORD>(baud_rate > 0 ? baud_rate : 115200);
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fParity = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  if (!SetCommState(h, &dcb)) {
    err = "设置串口参数失败: " + WinErrorText(GetLastError());
    CloseHandle(h);
    return false;
  }

  // 读：有数据立刻返回，没有也不等；写：最多等 1 秒
  COMMTIMEOUTS timeouts = {};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 1000;
  if (!SetCommTimeouts(h, &timeouts)) {
    err = "设置串口超时失败: " + WinErrorText(GetLastError());
    CloseHandle(h);
    return false;
  }

  SetupComm(h, 64 * 1024, 64 * 1024);
  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

  handle_ = h;
  port_name_ = port_name;
  return true;
}

void SerialPort::Close() {
  if (handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
  port_name_.clear();
}

bool SerialPort::Write(const std::uint8_t* data, int len, std::string& err) {
  if (handle_ == nullptr) {
    err = "串口未打开";
    return false;
  }
  if (len <= 0) {
    return true;
  }
  DWORD written = 0;
  if (!WriteFile(static_cast<HANDLE>(handle_), data,
                 static_cast<DWORD>(len), &written, nullptr)) {
    err = "串口写失败: " + WinErrorText(GetLastError());
    return false;
  }
  if (static_cast<int>(written) != len) {
    err = "串口写不完整: " + std::to_string(written) + "/" +
          std::to_string(len);
    return false;
  }
  return true;
}

int SerialPort::Read(std::uint8_t* out, int len, int timeout_ms) {
  if (handle_ == nullptr || len <= 0) {
    return 0;
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(handle_), out,
                  static_cast<DWORD>(len), &got, nullptr)) {
      return -1;
    }
    if (got > 0) {
      return static_cast<int>(got);
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
}

void SerialPort::FlushInput() {
  if (handle_ != nullptr) {
    PurgeComm(static_cast<HANDLE>(handle_), PURGE_RXCLEAR);
  }
}

std::vector<std::string> SerialPort::ListPorts() {
  std::vector<std::string> ports;
  const auto values = winreg::EnumValues(
      HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM");
  for (const auto& kv : values) {
    if (!kv.second.empty()) {
      ports.push_back(kv.second);
    }
  }
  std::sort(ports.begin(), ports.end());
  ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
  return ports;
}

}  // namespace eph205
