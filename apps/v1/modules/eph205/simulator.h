#pragma once

// 无硬件仿真：按固件行为实现状态机与状态码，数值由调用方配置。
//
// 只模拟协议层语义，不模拟电气行为：阻值直接给定，量程只影响读回字段。
// 仿真器通过 com0com 的另一端挂在真实串口上，和客户端走同一条链路，
// 避免"进程内旁路"掩盖串口层的问题。

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "protocol.h"
#include "serial_port.h"

namespace eph205 {

class Simulator {
 public:
  void SetDefaultResistance(float ohms);
  void SetResistance(int p, int n, float ohms);
  void SetFailure(int p, int n, Status status);
  void ClearOverrides();
  nlohmann::json Describe() const;

  // 以下都在仿真链路线程里调用
  void ResetAll();
  DeviceInfo Info() const;
  bool SetMode(Mode mode, Source source);
  bool SetWire(WireMode wire);
  bool SetRange(int range);
  bool SetSettle(int units);
  DeviceParams Params() const;
  void GetMode(Mode* mode, Source* source) const;
  Reading SimulateOne(int p, int n) const;

 private:
  static std::uint32_t KeyOf(int p, int n);
  struct Entry {
    float ohms = 0.0f;
    Status status = Status::kOk;
  };

  Mode mode_ = Mode::kCurrent;
  Source source_ = Source::kHowland;
  WireMode wire_ = WireMode::kTwoWire;
  std::uint8_t current_range_ = kRangeUnset;
  std::uint8_t voltage_range_ = kRangeUnset;
  std::uint8_t settle_delay_ = 0;
  std::uint8_t parallel_load_ = 0;
  float default_ohms_ = 100.0f;
  std::unordered_map<std::uint32_t, Entry> overrides_;
};

// 把 Simulator 挂到一个真实串口上。
class SimulatorLink {
 public:
  SimulatorLink() = default;
  ~SimulatorLink();

  bool Start(const std::string& port_name, int baud_rate, std::string& err);
  void Stop();
  bool running() const { return running_.load(); }
  std::string port_name() const;

  void SetDefaultResistance(float ohms);
  void SetResistance(int p, int n, float ohms);
  void SetFailure(int p, int n, Status status);
  void ClearOverrides();
  nlohmann::json Describe() const;

  std::vector<std::string> TakeFrameLog();

 private:
  void Loop();
  void HandleFrame(const Frame& frame);
  void Send(Cmd cmd, const std::uint8_t* data, int len);
  void SendAck(Status status);
  void AppendLog(const std::string& line);

  SerialPort port_;
  Simulator sim_;
  mutable std::mutex mu_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};
  std::vector<std::uint8_t> rx_buffer_;
  std::vector<std::string> frame_log_;
};

}  // namespace eph205
