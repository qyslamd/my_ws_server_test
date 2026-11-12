#pragma once

// 设备层：一次一问一答的帧事务。
//
// 全部调用都是阻塞的，并且只允许在同一个线程里使用（模块把它放在工作线程）。
// 上层不应在 HTTP/事件循环线程直接调用，否则会把整个底座卡住。

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "protocol.h"
#include "serial_port.h"

namespace eph205 {

// 失败分两层：io 非 kNone 是链路/宿主问题，否则看 status（设备状态码）
struct DeviceError {
  IoError io = IoError::kNone;
  Status status = Status::kOk;
  std::uint8_t cmd = 0;
  std::string message;

  bool ok() const { return io == IoError::kNone && status == Status::kOk; }
};

struct BatchResult {
  // false 表示链路级失败（超时/校验错/帧错/序号不符），items 不可信
  bool frame_ok = false;
  std::uint32_t start_seq = 0;
  Status status = Status::kOk;  // 帧内第一个错误码
  std::vector<Reading> items;   // 与入参同序；失败项 ohms 为 NaN
};

class Eph205Client {
 public:
  Eph205Client() = default;
  ~Eph205Client();

  // 打开并完成 0x01 握手；握手失败视为没连上
  bool Open(const std::string& port_name, int baud_rate, DeviceError* error);
  void Close();
  bool is_open() const { return port_.is_open(); }
  const std::string& port_name() const { return port_.port_name(); }

  bool QueryDeviceInfo(DeviceInfo* info, DeviceError* error);
  bool SetMode(Mode mode, Source source, DeviceError* error);
  bool SetParam(ParamType type, int value, DeviceError* error);
  bool GetParams(DeviceParams* params, DeviceError* error);
  bool GetMode(Mode* mode, Source* source, DeviceError* error);
  bool Measure(int p, int n, Reading* reading, DeviceError* error);
  bool MeasureBatch(std::uint32_t start_seq,
                    const std::vector<PointPair>& pairs, BatchResult* result,
                    DeviceError* error,
                    const std::atomic<bool>* cancel = nullptr);
  bool Reset(DeviceError* error);

  // 原样发一帧并收一帧，供调试页手工发命令。
  // 除正常应答外也接受 ACK（0x80），这样"命令不支持"这类
  // 通用错误能直接看到状态码，而不是一律超时。
  bool Raw(Cmd cmd, const std::vector<std::uint8_t>& data, int timeout_ms,
           Frame* response, DeviceError* error);

  int settle_delay_units() const { return settle_delay_units_; }

  // 调试页要看的原始帧记录，取走即清空。
  std::vector<std::string> TakeFrameLog();

 private:
  bool Transact(Cmd request, const std::uint8_t* data, int len, int timeout_ms,
                int retries, Frame* response, DeviceError* error,
                const std::atomic<bool>* cancel,
                std::uint8_t alt_response = 0);
  bool WriteFrame(Cmd request, const std::uint8_t* data, int len,
                  DeviceError* error);
  bool AwaitResponse(Cmd request, int timeout_ms, Frame* response,
                     DeviceError* error, const std::atomic<bool>* cancel);

  int SingleTimeoutMs() const;
  int BatchTimeoutMs(int groups) const;

  void AppendLog(const std::string& line);

  SerialPort port_;
  std::vector<std::uint8_t> rx_buffer_;
  std::vector<std::string> frame_log_;
  int settle_delay_units_ = 0;
  std::uint8_t expected_response_ = 0;
  std::uint8_t alt_response_ = 0;  // 0 表示只认 expected_response_
};

}  // namespace eph205
