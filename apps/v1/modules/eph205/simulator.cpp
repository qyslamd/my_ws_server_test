#include "simulator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace eph205 {

namespace {

constexpr std::uint16_t kFirmwareVersion = 0x0100;
constexpr std::uint16_t kPCount = 640;
constexpr std::uint16_t kNCount = 640;
constexpr std::uint8_t kMuxBoards = 3;
constexpr std::uint8_t kRangeCount = 6;
constexpr std::uint16_t kDeviceId = 0x0205;
constexpr int kBaudRate = 115200;
constexpr std::size_t kMaxFrameLog = 400;

float QuietNaN() { return std::numeric_limits<float>::quiet_NaN(); }

std::string HexDump(const std::uint8_t* data, int len) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  for (int i = 0; i < len; ++i) {
    if (i > 0) {
      out.push_back(' ');
    }
    out.push_back(kHex[(data[i] >> 4) & 0x0F]);
    out.push_back(kHex[data[i] & 0x0F]);
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------- Simulator

std::uint32_t Simulator::KeyOf(int p, int n) {
  return (static_cast<std::uint32_t>(p) << 16) | static_cast<std::uint32_t>(n);
}

void Simulator::SetDefaultResistance(float ohms) { default_ohms_ = ohms; }

void Simulator::SetResistance(int p, int n, float ohms) {
  overrides_[KeyOf(p, n)] = Entry{ohms, Status::kOk};
}

void Simulator::SetFailure(int p, int n, Status status) {
  overrides_[KeyOf(p, n)] = Entry{QuietNaN(), status};
}

void Simulator::ClearOverrides() { overrides_.clear(); }

nlohmann::json Simulator::Describe() const {
  nlohmann::json overrides = nlohmann::json::array();
  for (const auto& kv : overrides_) {
    if (overrides.size() >= 200) {
      break;
    }
    overrides.push_back({{"p", kv.first >> 16},
                         {"n", kv.first & 0xFFFF},
                         {"ohms", kv.second.ohms},
                         {"status", static_cast<int>(kv.second.status)}});
  }
  return {{"defaultOhms", default_ohms_},
          {"overrideCount", overrides_.size()},
          {"overrides", overrides}};
}

void Simulator::ResetAll() {
  mode_ = Mode::kCurrent;
  source_ = Source::kHowland;
  wire_ = WireMode::kTwoWire;
  current_range_ = kRangeUnset;
  voltage_range_ = kRangeUnset;
  settle_delay_ = 0;
  parallel_load_ = 0;
}

DeviceInfo Simulator::Info() const {
  DeviceInfo info;
  info.firmware_version = kFirmwareVersion;
  info.p_count = kPCount;
  info.n_count = kNCount;
  info.mux_boards = kMuxBoards;
  info.range_count = kRangeCount;
  info.mode = mode_;
  info.source = source_;
  info.device_id = kDeviceId;
  return info;
}

bool Simulator::SetMode(Mode mode, Source source) {
  if (mode != Mode::kCurrent && mode != Mode::kVoltage) {
    return false;
  }
  const int source_value = static_cast<int>(source);
  if (source_value < static_cast<int>(Source::kDiscrete) ||
      source_value > static_cast<int>(Source::kAd7124)) {
    return false;
  }
  mode_ = mode;
  source_ = source;
  return true;
}

bool Simulator::SetWire(WireMode wire) {
  if (wire != WireMode::kTwoWire && wire != WireMode::kFourWire) {
    return false;
  }
  wire_ = wire;
  return true;
}

bool Simulator::SetRange(int range) {
  if (range < 0 || range >= kRangeCount) {
    return false;
  }
  // 固件按当前模式分别保存量程，设置后关闭输出并释放并联负载
  if (mode_ == Mode::kCurrent) {
    current_range_ = static_cast<std::uint8_t>(range);
  } else {
    voltage_range_ = static_cast<std::uint8_t>(range);
  }
  parallel_load_ = 0;
  return true;
}

bool Simulator::SetSettle(int units) {
  if (units < 0 || units > 255) {
    return false;
  }
  settle_delay_ = static_cast<std::uint8_t>(units);
  return true;
}

DeviceParams Simulator::Params() const {
  DeviceParams params;
  params.wire = wire_;
  // 恒压模式下固件固定返回 0，恒流量程未设置时为 0xFF
  params.range = mode_ == Mode::kCurrent ? current_range_ : 0;
  params.average_times = 1;
  params.settle_delay = settle_delay_;
  params.timeout = 0;
  params.output_enabled = false;
  params.parallel_load = parallel_load_;
  return params;
}

void Simulator::GetMode(Mode* mode, Source* source) const {
  if (mode != nullptr) {
    *mode = mode_;
  }
  if (source != nullptr) {
    *source = source_;
  }
}

Reading Simulator::SimulateOne(int p, int n) const {
  Reading reading;
  if (p < kMinPoint || p > kMaxPoint || n < kMinPoint || n > kMaxPoint) {
    reading.status = Status::kPointInvalid;
    reading.ohms = QuietNaN();
    return reading;
  }
  const auto it = overrides_.find(KeyOf(p, n));
  if (it != overrides_.end()) {
    reading.status = it->second.status;
    reading.ohms = it->second.ohms;
  } else {
    reading.ohms = default_ohms_;
  }
  return reading;
}

// ------------------------------------------------------------ SimulatorLink

SimulatorLink::~SimulatorLink() { Stop(); }

bool SimulatorLink::Start(const std::string& port_name, int baud_rate,
                          std::string& err) {
  if (running_.load()) {
    Stop();
  }
  if (!port_.Open(port_name, baud_rate > 0 ? baud_rate : kBaudRate, err)) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    sim_.ResetAll();
    rx_buffer_.clear();
  }
  stop_.store(false);
  running_.store(true);
  thread_ = std::thread(&SimulatorLink::Loop, this);
  return true;
}

void SimulatorLink::Stop() {
  stop_.store(true);
  if (thread_.joinable()) {
    thread_.join();
  }
  running_.store(false);
  port_.Close();
}

std::string SimulatorLink::port_name() const {
  return const_cast<SimulatorLink*>(this)->port_.port_name();
}

void SimulatorLink::SetDefaultResistance(float ohms) {
  std::lock_guard<std::mutex> lock(mu_);
  sim_.SetDefaultResistance(ohms);
}

void SimulatorLink::SetResistance(int p, int n, float ohms) {
  std::lock_guard<std::mutex> lock(mu_);
  sim_.SetResistance(p, n, ohms);
}

void SimulatorLink::SetFailure(int p, int n, Status status) {
  std::lock_guard<std::mutex> lock(mu_);
  sim_.SetFailure(p, n, status);
}

void SimulatorLink::ClearOverrides() {
  std::lock_guard<std::mutex> lock(mu_);
  sim_.ClearOverrides();
}

nlohmann::json SimulatorLink::Describe() const {
  std::lock_guard<std::mutex> lock(mu_);
  auto out = sim_.Describe();
  out["running"] = running_.load();
  out["port"] = port_.port_name();
  return out;
}

void SimulatorLink::AppendLog(const std::string& line) {
  frame_log_.push_back(line);
  if (frame_log_.size() > kMaxFrameLog) {
    frame_log_.erase(frame_log_.begin(),
                     frame_log_.begin() + (frame_log_.size() - kMaxFrameLog));
  }
}

std::vector<std::string> SimulatorLink::TakeFrameLog() {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> out;
  out.swap(frame_log_);
  return out;
}

void SimulatorLink::Loop() {
  std::uint8_t chunk[512];
  while (!stop_.load()) {
    const int got = port_.Read(chunk, sizeof(chunk), 5);
    if (got < 0) {
      break;
    }
    if (got == 0) {
      continue;
    }
    rx_buffer_.insert(rx_buffer_.end(), chunk, chunk + got);
    for (;;) {
      Frame frame;
      const ParseOutcome outcome =
          ParseFrame(rx_buffer_.data(), static_cast<int>(rx_buffer_.size()),
                     &frame);
      if (outcome.consumed > 0) {
        rx_buffer_.erase(rx_buffer_.begin(),
                         rx_buffer_.begin() + outcome.consumed);
      }
      if (outcome.state == ParseState::kNeedMore) {
        break;
      }
      if (outcome.state == ParseState::kResync) {
        continue;
      }
      AppendLog("RX " + std::string(CmdText(frame.cmd)) + " | " +
                HexDump(frame.data, frame.len));
      HandleFrame(frame);
    }
  }
}

void SimulatorLink::Send(Cmd cmd, const std::uint8_t* data, int len) {
  std::uint8_t buffer[kMaxFrameLen] = {};
  const int size = BuildFrame(cmd, data, len, buffer, sizeof(buffer));
  if (size <= 0) {
    return;
  }
  std::string err;
  port_.Write(buffer, size, err);
  AppendLog("TX " + std::string(CmdText(cmd)) + " | " + HexDump(buffer, size));
}

void SimulatorLink::SendAck(Status status) {
  const std::uint8_t data[1] = {static_cast<std::uint8_t>(status)};
  Send(Cmd::kAck, data, 1);
}

void SimulatorLink::HandleFrame(const Frame& frame) {
  std::lock_guard<std::mutex> lock(mu_);

  switch (frame.cmd) {
    case Cmd::kGetDeviceInfo: {
      const DeviceInfo info = sim_.Info();
      std::uint8_t data[14] = {};
      data[0] = static_cast<std::uint8_t>(Status::kOk);
      PutU16Le(&data[1], info.firmware_version);
      PutU16Le(&data[3], info.p_count);
      PutU16Le(&data[5], info.n_count);
      data[7] = info.mux_boards;
      data[8] = info.range_count;
      data[9] = static_cast<std::uint8_t>(info.mode);
      data[10] = static_cast<std::uint8_t>(info.source);
      PutU16Le(&data[11], info.device_id);
      data[13] = 0;
      Send(Cmd::kDeviceInfo, data, 14);
      return;
    }
    case Cmd::kMeasureSingle: {
      if (frame.len != 4) {
        SendAck(Status::kFrameError);
        return;
      }
      const Reading reading =
          sim_.SimulateOne(ReadU16Le(&frame.data[0]), ReadU16Le(&frame.data[2]));
      std::uint8_t data[13] = {};
      data[0] = static_cast<std::uint8_t>(reading.status);
      WriteFloatLe(reading.ohms, &data[1]);
      PutU32Le(&data[5], reading.raw_adc);
      PutU32Le(&data[9], static_cast<std::uint32_t>(reading.differential_uv));
      Send(Cmd::kSingleResult, data, 13);
      return;
    }
    case Cmd::kMeasureBatch: {
      if (frame.len < 5) {
        SendAck(Status::kFrameError);
        return;
      }
      const int groups = frame.data[4];
      if (groups < 1 || groups > kMaxBatchGroups ||
          frame.len != 5 + 4 * groups) {
        SendAck(Status::kFrameError);
        return;
      }
      const std::uint32_t start_seq = ReadU32Le(&frame.data[0]);
      std::uint8_t data[kMaxDataLen] = {};
      int offset = 0;
      PutU32Le(&data[offset], start_seq);
      offset += 4;
      data[offset++] = static_cast<std::uint8_t>(groups);
      std::uint8_t first_error = static_cast<std::uint8_t>(Status::kOk);
      std::uint8_t values[kMaxBatchGroups * 4] = {};
      for (int i = 0; i < groups; ++i) {
        const Reading reading =
            sim_.SimulateOne(ReadU16Le(&frame.data[5 + 4 * i]),
                             ReadU16Le(&frame.data[7 + 4 * i]));
        // 整体状态取第一个错误码，其余组的有效性由 NaN 表示
        if (reading.status != Status::kOk &&
            first_error == static_cast<std::uint8_t>(Status::kOk)) {
          first_error = static_cast<std::uint8_t>(reading.status);
        }
        WriteFloatLe(reading.ohms, &values[4 * i]);
      }
      data[offset++] = first_error;
      for (int i = 0; i < groups * 4; ++i) {
        data[offset++] = values[i];
      }
      Send(Cmd::kBatchResult, data, offset);
      return;
    }
    case Cmd::kSetParam: {
      if (frame.len != 2) {
        SendAck(Status::kFrameError);
        return;
      }
      const ParamType type = static_cast<ParamType>(frame.data[0]);
      const int value = frame.data[1];
      bool ok = false;
      switch (type) {
        case ParamType::kWireMode:
          ok = sim_.SetWire(static_cast<WireMode>(value));
          break;
        case ParamType::kRange:
          ok = sim_.SetRange(value);
          break;
        case ParamType::kSettleDelay:
          ok = sim_.SetSettle(value);
          break;
        default:
          // 平均次数与超时在固件里固定返回参数越界
          SendAck(Status::kParamOutOfRange);
          return;
      }
      SendAck(ok ? Status::kOk : Status::kParamOutOfRange);
      return;
    }
    case Cmd::kGetParam: {
      const DeviceParams params = sim_.Params();
      std::uint8_t data[7] = {};
      data[0] = static_cast<std::uint8_t>(params.wire);
      data[1] = params.range;
      data[2] = params.average_times;
      data[3] = params.settle_delay;
      data[4] = params.timeout;
      data[5] = params.output_enabled ? 1 : 0;
      data[6] = params.parallel_load;
      Send(Cmd::kParamResult, data, 7);
      return;
    }
    case Cmd::kSetMode: {
      if (frame.len != 2) {
        SendAck(Status::kFrameError);
        return;
      }
      const bool ok = sim_.SetMode(static_cast<Mode>(frame.data[0]),
                                   static_cast<Source>(frame.data[1]));
      SendAck(ok ? Status::kOk : Status::kParamOutOfRange);
      return;
    }
    case Cmd::kGetMode: {
      Mode mode = Mode::kCurrent;
      Source source = Source::kHowland;
      sim_.GetMode(&mode, &source);
      const std::uint8_t data[2] = {static_cast<std::uint8_t>(mode),
                                    static_cast<std::uint8_t>(source)};
      Send(Cmd::kModeResult, data, 2);
      return;
    }
    case Cmd::kResetDevice: {
      sim_.ResetAll();
      SendAck(Status::kOk);
      return;
    }
    default:
      // 校准、点检测与原始 ADC 未实现
      SendAck(Status::kCmdUnsupported);
      return;
  }
}

}  // namespace eph205
