#include "eph205_client.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace eph205 {

namespace {

constexpr int kAckTimeoutMs = 500;
constexpr int kTimeoutMarginMs = 300;
constexpr int kMinSingleTimeoutMs = 500;
constexpr int kDefaultBaud = 115200;
// 单点实测：二线 70–75 ms、四线 77–81 ms。超时按最坏值取 81 ms
constexpr int kPerPointMs = 81;
constexpr std::size_t kMaxFrameLog = 400;

void SetError(DeviceError* error, IoError io, std::uint8_t cmd,
              const std::string& message) {
  if (error == nullptr) {
    return;
  }
  error->io = io;
  error->cmd = cmd;
  error->message = message;
}

void SetStatusError(DeviceError* error, Status status, std::uint8_t cmd) {
  if (error == nullptr) {
    return;
  }
  error->status = status;
  error->cmd = cmd;
  error->message = StatusText(status);
}

std::string HexDump(const std::uint8_t* data, int len) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(static_cast<std::size_t>(len) * 3);
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

Eph205Client::~Eph205Client() { Close(); }

bool Eph205Client::Open(const std::string& port_name, int baud_rate,
                        DeviceError* error) {
  Close();
  std::string err;
  if (!port_.Open(port_name, baud_rate > 0 ? baud_rate : kDefaultBaud, err)) {
    SetError(error, IoError::kDisconnected, 0, err);
    return false;
  }
  rx_buffer_.clear();
  settle_delay_units_ = 0;

  // 握手失败就当没连上，避免界面停在"已连接但发不出命令"的中间态
  DeviceInfo info;
  if (!QueryDeviceInfo(&info, error)) {
    port_.Close();
    return false;
  }
  return true;
}

void Eph205Client::Close() {
  port_.Close();
  rx_buffer_.clear();
}

int Eph205Client::SingleTimeoutMs() const {
  const int deadline = settle_delay_units_ * 10 + kPerPointMs + kTimeoutMarginMs;
  return std::max(kMinSingleTimeoutMs, deadline);
}

int Eph205Client::BatchTimeoutMs(int groups) const {
  const int per_group = settle_delay_units_ * 10 + kPerPointMs;
  return std::max(kMinSingleTimeoutMs, per_group * groups + kTimeoutMarginMs);
}

void Eph205Client::AppendLog(const std::string& line) {
  frame_log_.push_back(line);
  if (frame_log_.size() > kMaxFrameLog) {
    frame_log_.erase(frame_log_.begin(),
                     frame_log_.begin() + (frame_log_.size() - kMaxFrameLog));
  }
}

std::vector<std::string> Eph205Client::TakeFrameLog() {
  std::vector<std::string> out;
  out.swap(frame_log_);
  return out;
}

bool Eph205Client::WriteFrame(Cmd request, const std::uint8_t* data, int len,
                              DeviceError* error) {
  std::uint8_t buffer[kMaxFrameLen] = {};
  const int size = BuildFrame(request, data, len, buffer, sizeof(buffer));
  if (size <= 0) {
    SetError(error, IoError::kFrameError, static_cast<std::uint8_t>(request),
             "帧超长");
    return false;
  }
  std::string err;
  if (!port_.Write(buffer, size, err)) {
    SetError(error, IoError::kDisconnected,
             static_cast<std::uint8_t>(request), err);
    return false;
  }
  AppendLog("TX " + std::string(CmdText(request)) + " | " +
            HexDump(buffer, size));
  return true;
}

bool Eph205Client::AwaitResponse(Cmd request, int timeout_ms, Frame* response,
                                 DeviceError* error,
                                 const std::atomic<bool>* cancel) {
  expected_response_ = static_cast<std::uint8_t>(ExpectedResponse(request));
  const std::uint8_t alt = alt_response_;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);

  for (;;) {
    if (cancel != nullptr && cancel->load()) {
      SetError(error, IoError::kTimeout, static_cast<std::uint8_t>(request),
               "已取消");
      return false;
    }
    std::uint8_t chunk[512];
    const bool no_more =
        std::chrono::steady_clock::now() >= deadline;
    const int got = port_.Read(chunk, sizeof(chunk), no_more ? 0 : 5);
    if (got < 0) {
      SetError(error, IoError::kDisconnected,
               static_cast<std::uint8_t>(request), "串口读失败");
      return false;
    }
    if (got > 0) {
      rx_buffer_.insert(rx_buffer_.end(), chunk, chunk + got);
    }

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
      // 不是当前请求的应答：多为重发后的迟到帧，丢弃继续等
      const std::uint8_t code = static_cast<std::uint8_t>(frame.cmd);
      if (code != expected_response_ && !(alt != 0 && code == alt)) {
        continue;
      }
      if (response != nullptr) {
        *response = frame;
      }
      return true;
    }

    if (no_more) {
      SetError(error, IoError::kTimeout, static_cast<std::uint8_t>(request),
               "应答超时");
      return false;
    }
  }
}

bool Eph205Client::Transact(Cmd request, const std::uint8_t* data, int len,
                            int timeout_ms, int retries, Frame* response,
                            DeviceError* error,
                            const std::atomic<bool>* cancel,
                            std::uint8_t alt_response) {
  alt_response_ = alt_response;
  if (!port_.is_open()) {
    SetError(error, IoError::kDisconnected, static_cast<std::uint8_t>(request),
             "串口未打开");
    return false;
  }

  const int attempts = retries + 1;
  for (int attempt = 0; attempt < attempts; ++attempt) {
    if (cancel != nullptr && cancel->load()) {
      SetError(error, IoError::kTimeout, static_cast<std::uint8_t>(request),
               "已取消");
      return false;
    }
    // 重发前丢掉上一轮的迟到帧，否则同一条命令会被旧应答顶替
    rx_buffer_.clear();
    port_.FlushInput();
    if (!WriteFrame(request, data, len, error)) {
      return false;
    }
    if (AwaitResponse(request, timeout_ms, response, error, cancel)) {
      return true;
    }
    if (error != nullptr && error->io == IoError::kDisconnected) {
      return false;
    }
  }
  return false;
}

bool Eph205Client::QueryDeviceInfo(DeviceInfo* info, DeviceError* error) {
  Frame frame;
  if (!Transact(Cmd::kGetDeviceInfo, nullptr, 0, kAckTimeoutMs, 3, &frame,
                error, nullptr)) {
    return false;
  }
  if (frame.len != 14) {
    SetError(error, IoError::kBadResponse,
             static_cast<std::uint8_t>(Cmd::kGetDeviceInfo),
             "设备信息长度 " + std::to_string(frame.len));
    return false;
  }
  const Status status = static_cast<Status>(frame.data[0]);
  if (status != Status::kOk) {
    SetStatusError(error, status, static_cast<std::uint8_t>(Cmd::kGetDeviceInfo));
    return false;
  }
  if (info != nullptr) {
    info->firmware_version = ReadU16Le(&frame.data[1]);
    info->p_count = ReadU16Le(&frame.data[3]);
    info->n_count = ReadU16Le(&frame.data[5]);
    info->mux_boards = frame.data[7];
    info->range_count = frame.data[8];
    info->mode = static_cast<Mode>(frame.data[9]);
    info->source = static_cast<Source>(frame.data[10]);
    info->device_id = ReadU16Le(&frame.data[11]);
  }
  return true;
}

bool Eph205Client::SetMode(Mode mode, Source source, DeviceError* error) {
  const std::uint8_t data[2] = {static_cast<std::uint8_t>(mode),
                                static_cast<std::uint8_t>(source)};
  Frame frame;
  if (!Transact(Cmd::kSetMode, data, 2, kAckTimeoutMs, 3, &frame, error,
                nullptr)) {
    return false;
  }
  if (frame.len != 1) {
    SetError(error, IoError::kBadResponse,
             static_cast<std::uint8_t>(Cmd::kSetMode),
             "ACK 长度 " + std::to_string(frame.len));
    return false;
  }
  const Status status = static_cast<Status>(frame.data[0]);
  if (status != Status::kOk) {
    SetStatusError(error, status, static_cast<std::uint8_t>(Cmd::kSetMode));
    return false;
  }
  return true;
}

bool Eph205Client::SetParam(ParamType type, int value, DeviceError* error) {
  if (value < 0 || value > 255) {
    SetError(error, IoError::kBadResponse, static_cast<std::uint8_t>(type),
             "参数超出单字节范围");
    return false;
  }
  const std::uint8_t data[2] = {static_cast<std::uint8_t>(type),
                                static_cast<std::uint8_t>(value)};
  Frame frame;
  if (!Transact(Cmd::kSetParam, data, 2, kAckTimeoutMs, 3, &frame, error,
                nullptr)) {
    return false;
  }
  if (frame.len != 1) {
    SetError(error, IoError::kBadResponse,
             static_cast<std::uint8_t>(Cmd::kSetParam),
             "ACK 长度 " + std::to_string(frame.len));
    return false;
  }
  const Status status = static_cast<Status>(frame.data[0]);
  if (status != Status::kOk) {
    SetStatusError(error, status, static_cast<std::uint8_t>(Cmd::kSetParam));
    return false;
  }
  if (type == ParamType::kSettleDelay) {
    settle_delay_units_ = value;
  }
  return true;
}

bool Eph205Client::GetParams(DeviceParams* params, DeviceError* error) {
  Frame frame;
  if (!Transact(Cmd::kGetParam, nullptr, 0, kAckTimeoutMs, 3, &frame, error,
                nullptr)) {
    return false;
  }
  // 0x84 的 DATA 没有状态字节
  if (frame.len != 7) {
    SetError(error, IoError::kBadResponse,
             static_cast<std::uint8_t>(Cmd::kGetParam),
             "参数长度 " + std::to_string(frame.len));
    return false;
  }
  if (params != nullptr) {
    params->wire = static_cast<WireMode>(frame.data[0]);
    params->range = frame.data[1];
    params->average_times = frame.data[2];
    params->settle_delay = frame.data[3];
    params->timeout = frame.data[4];
    params->output_enabled = frame.data[5] != 0;
    params->parallel_load = frame.data[6];
  }
  return true;
}

bool Eph205Client::GetMode(Mode* mode, Source* source, DeviceError* error) {
  Frame frame;
  if (!Transact(Cmd::kGetMode, nullptr, 0, kAckTimeoutMs, 3, &frame, error,
                nullptr)) {
    return false;
  }
  // 0x86 的 DATA 没有状态字节
  if (frame.len != 2) {
    SetError(error, IoError::kBadResponse,
             static_cast<std::uint8_t>(Cmd::kGetMode),
             "模式长度 " + std::to_string(frame.len));
    return false;
  }
  if (mode != nullptr) {
    *mode = static_cast<Mode>(frame.data[0]);
  }
  if (source != nullptr) {
    *source = static_cast<Source>(frame.data[1]);
  }
  return true;
}

bool Eph205Client::Measure(int p, int n, Reading* reading, DeviceError* error) {
  std::uint8_t data[4] = {};
  PutU16Le(&data[0], p);
  PutU16Le(&data[2], n);
  Frame frame;
  if (!Transact(Cmd::kMeasureSingle, data, 4, SingleTimeoutMs(), 2, &frame,
                error, nullptr)) {
    return false;
  }
  if (frame.len != 13) {
    SetError(error, IoError::kBadResponse,
             static_cast<std::uint8_t>(Cmd::kMeasureSingle),
             "单点结果长度 " + std::to_string(frame.len));
    return false;
  }
  if (reading != nullptr) {
    reading->status = static_cast<Status>(frame.data[0]);
    reading->ohms = ReadFloatLe(&frame.data[1]);
    reading->raw_adc = ReadU32Le(&frame.data[5]);
    reading->differential_uv = ReadI32Le(&frame.data[9]);
  }
  return true;
}

bool Eph205Client::MeasureBatch(std::uint32_t start_seq,
                                const std::vector<PointPair>& pairs,
                                BatchResult* result, DeviceError* error,
                                const std::atomic<bool>* cancel) {
  if (pairs.empty() || pairs.size() > static_cast<std::size_t>(kMaxBatchGroups)) {
    SetError(error, IoError::kFrameError,
             static_cast<std::uint8_t>(Cmd::kMeasureBatch),
             "批组数 " + std::to_string(pairs.size()) + " 越界");
    if (result != nullptr) {
      result->frame_ok = false;
    }
    return false;
  }

  std::uint8_t data[kMaxDataLen] = {};
  int offset = 0;
  PutU32Le(&data[offset], start_seq);
  offset += 4;
  data[offset++] = static_cast<std::uint8_t>(pairs.size());
  for (const PointPair& pair : pairs) {
    PutU16Le(&data[offset], pair.p);
    offset += 2;
    PutU16Le(&data[offset], pair.n);
    offset += 2;
  }

  Frame frame;
  if (!Transact(Cmd::kMeasureBatch, data, offset,
                BatchTimeoutMs(static_cast<int>(pairs.size())), 3, &frame,
                error, cancel)) {
    if (result != nullptr) {
      result->frame_ok = false;
    }
    return false;
  }

  const int expected = 6 + 4 * static_cast<int>(pairs.size());
  if (frame.len != expected) {
    SetError(error, IoError::kBadResponse,
             static_cast<std::uint8_t>(Cmd::kMeasureBatch),
             "批量结果长度 " + std::to_string(frame.len) + "，应为 " +
                 std::to_string(expected));
    if (result != nullptr) {
      result->frame_ok = false;
    }
    return false;
  }

  const std::uint32_t echoed = ReadU32Le(&frame.data[0]);
  const int groups = frame.data[4];
  if (echoed != start_seq) {
    SetError(error, IoError::kSeqMismatch,
             static_cast<std::uint8_t>(Cmd::kMeasureBatch),
             "起始序号 " + std::to_string(echoed) + " != " +
                 std::to_string(start_seq));
    if (result != nullptr) {
      result->frame_ok = false;
    }
    return false;
  }
  if (groups != static_cast<int>(pairs.size())) {
    SetError(error, IoError::kBadResponse,
             static_cast<std::uint8_t>(Cmd::kMeasureBatch),
             "组数 " + std::to_string(groups) + " != " +
                 std::to_string(pairs.size()));
    if (result != nullptr) {
      result->frame_ok = false;
    }
    return false;
  }

  if (result != nullptr) {
    result->frame_ok = true;
    result->start_seq = echoed;
    result->status = static_cast<Status>(frame.data[5]);
    result->items.clear();
    result->items.reserve(static_cast<std::size_t>(groups));
    for (int i = 0; i < groups; ++i) {
      Reading reading;
      reading.ohms = ReadFloatLe(&frame.data[6 + 4 * i]);
      // 组级失败只有 NaN 这一个信号
      reading.status =
          std::isnan(reading.ohms) ? Status::kOverRange : Status::kOk;
      result->items.push_back(reading);
    }
  }
  return true;
}

bool Eph205Client::Raw(Cmd cmd, const std::vector<std::uint8_t>& data,
                       int timeout_ms, Frame* response, DeviceError* error) {
  return Transact(cmd, data.data(), static_cast<int>(data.size()),
                  timeout_ms > 0 ? timeout_ms : 800, 0, response, error,
                  nullptr, static_cast<std::uint8_t>(Cmd::kAck));
}

bool Eph205Client::Reset(DeviceError* error) {
  Frame frame;
  if (!Transact(Cmd::kResetDevice, nullptr, 0, kAckTimeoutMs, 3, &frame, error,
                nullptr)) {
    return false;
  }
  if (frame.len != 1) {
    SetError(error, IoError::kBadResponse,
             static_cast<std::uint8_t>(Cmd::kResetDevice),
             "ACK 长度 " + std::to_string(frame.len));
    return false;
  }
  const Status status = static_cast<Status>(frame.data[0]);
  if (status != Status::kOk) {
    SetStatusError(error, status, static_cast<std::uint8_t>(Cmd::kResetDevice));
    return false;
  }
  settle_delay_units_ = 0;
  return true;
}

}  // namespace eph205
