#include "protocol.h"

#include <cstring>

namespace eph205 {

namespace {

int FindHead(const std::uint8_t* buffer, int size) {
  for (int i = 0; i + 1 < size; ++i) {
    if (buffer[i] == kFrameHead0 && buffer[i + 1] == kFrameHead1) {
      return i;
    }
  }
  return size;
}

}  // namespace

std::uint8_t Sum8(const std::uint8_t* data, int size) {
  std::uint8_t sum = 0;
  for (int i = 0; i < size; ++i) {
    sum = static_cast<std::uint8_t>(sum + data[i]);
  }
  return sum;
}

int BuildFrame(Cmd cmd, const std::uint8_t* data, int len, std::uint8_t* out,
               int capacity) {
  if (out == nullptr || len < 0 || len > kMaxDataLen) {
    return 0;
  }
  if (len > 0 && data == nullptr) {
    return 0;
  }
  const int total = kFrameOverhead + len;
  if (capacity < total) {
    return 0;
  }
  out[0] = kFrameHead0;
  out[1] = kFrameHead1;
  out[2] = static_cast<std::uint8_t>(cmd);
  out[3] = static_cast<std::uint8_t>(len);
  if (len > 0) {
    std::memcpy(out + 4, data, static_cast<std::size_t>(len));
  }
  out[4 + len] = Sum8(out + 2, 2 + len);
  return total;
}

ParseOutcome ParseFrame(const std::uint8_t* buffer, int size, Frame* frame) {
  ParseOutcome outcome;
  if (buffer == nullptr || size <= 0) {
    return outcome;
  }

  const int head = FindHead(buffer, size);
  if (head >= size) {
    // 整段没有帧头：保留最后 1 字节，它可能是下一个帧头的前半
    outcome.consumed = size > 1 ? size - 1 : 0;
    if (size == 1 && buffer[0] != kFrameHead0) {
      outcome.consumed = 1;
    }
    outcome.state =
        outcome.consumed > 0 ? ParseState::kResync : ParseState::kNeedMore;
    return outcome;
  }
  if (head > 0) {
    outcome.state = ParseState::kResync;
    outcome.consumed = head;
    return outcome;
  }

  if (size < 4) {
    return outcome;
  }
  const int data_len = buffer[3];
  const int total = kFrameOverhead + data_len;
  if (size < total) {
    return outcome;
  }

  const std::uint8_t expected = Sum8(buffer + 2, 2 + data_len);
  if (expected != buffer[4 + data_len]) {
    // 只丢 1 字节：帧头可能出现在伪帧中间
    outcome.state = ParseState::kResync;
    outcome.consumed = 1;
    return outcome;
  }

  if (frame != nullptr) {
    frame->cmd = static_cast<Cmd>(buffer[2]);
    frame->len = data_len;
    if (data_len > 0) {
      std::memcpy(frame->data, buffer + 4, static_cast<std::size_t>(data_len));
    }
  }
  outcome.state = ParseState::kFrame;
  outcome.consumed = total;
  return outcome;
}

float ReadFloatLe(const std::uint8_t* in) {
  const std::uint32_t bits = static_cast<std::uint32_t>(in[0]) |
                             (static_cast<std::uint32_t>(in[1]) << 8) |
                             (static_cast<std::uint32_t>(in[2]) << 16) |
                             (static_cast<std::uint32_t>(in[3]) << 24);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void WriteFloatLe(float value, std::uint8_t* out) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  out[0] = static_cast<std::uint8_t>(bits & 0xFF);
  out[1] = static_cast<std::uint8_t>((bits >> 8) & 0xFF);
  out[2] = static_cast<std::uint8_t>((bits >> 16) & 0xFF);
  out[3] = static_cast<std::uint8_t>((bits >> 24) & 0xFF);
}

std::uint16_t ReadU16Le(const std::uint8_t* in) {
  return static_cast<std::uint16_t>(in[0] | (in[1] << 8));
}

std::uint32_t ReadU32Le(const std::uint8_t* in) {
  return static_cast<std::uint32_t>(in[0]) |
         (static_cast<std::uint32_t>(in[1]) << 8) |
         (static_cast<std::uint32_t>(in[2]) << 16) |
         (static_cast<std::uint32_t>(in[3]) << 24);
}

std::int32_t ReadI32Le(const std::uint8_t* in) {
  return static_cast<std::int32_t>(ReadU32Le(in));
}

void PutU16Le(std::uint8_t* out, int value) {
  out[0] = static_cast<std::uint8_t>(value & 0xFF);
  out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void PutU32Le(std::uint8_t* out, std::uint32_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xFF);
  out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  out[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  out[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

int ExpectedResponseLen(Cmd request) {
  switch (request) {
    case Cmd::kGetDeviceInfo:
      return 14;
    case Cmd::kMeasureSingle:
    case Cmd::kReadRawAdc:
      return 13;
    case Cmd::kMeasureBatch:
      return -1;  // 6 + 4*N
    case Cmd::kSetParam:
    case Cmd::kCalibrate:
    case Cmd::kResetDevice:
    case Cmd::kSetMode:
      return 1;
    case Cmd::kGetParam:
      return 7;
    case Cmd::kCheckPoint:
      return 2;
    case Cmd::kGetMode:
      return 2;
    default:
      return -1;
  }
}

Cmd ExpectedResponse(Cmd request) {
  switch (request) {
    case Cmd::kGetDeviceInfo:
      return Cmd::kDeviceInfo;
    case Cmd::kMeasureSingle:
    case Cmd::kReadRawAdc:
      return Cmd::kSingleResult;
    case Cmd::kMeasureBatch:
      return Cmd::kBatchResult;
    case Cmd::kGetParam:
      return Cmd::kParamResult;
    case Cmd::kCheckPoint:
      return Cmd::kCheckResult;
    case Cmd::kGetMode:
      return Cmd::kModeResult;
    default:
      return Cmd::kAck;
  }
}

const char* StatusText(Status status) {
  switch (status) {
    case Status::kOk:
      return "OK";
    case Status::kSumError:
      return "校验和错误";
    case Status::kCmdUnsupported:
      return "命令不支持";
    case Status::kParamOutOfRange:
      return "参数越界";
    case Status::kPointInvalid:
      return "点位无效";
    case Status::kBusy:
      return "设备忙";
    case Status::kOverRange:
      return "超量程";
    case Status::kFrameError:
      return "帧格式错误";
    case Status::kCalibFail:
      return "校准失败";
    case Status::kReserved09:
      return "保留状态 0x09";
    case Status::kReserved0A:
      return "保留状态 0x0A";
    case Status::kAdcTimeout:
      return "ADC 超时";
    case Status::kI2cError:
      return "I2C 错误";
    case Status::kSpiError:
      return "SPI 错误";
    case Status::kMuxError:
      return "MUX 错误";
    case Status::kOutputNotReady:
      return "输出未就绪";
  }
  return "未知状态";
}

const char* CmdText(Cmd cmd) {
  switch (cmd) {
    case Cmd::kGetDeviceInfo:
      return "GET_DEVICE_INFO";
    case Cmd::kMeasureSingle:
      return "MEASURE_SINGLE";
    case Cmd::kMeasureBatch:
      return "MEASURE_BATCH";
    case Cmd::kSetParam:
      return "SET_PARAM";
    case Cmd::kGetParam:
      return "GET_PARAM";
    case Cmd::kCalibrate:
      return "CALIBRATE";
    case Cmd::kCheckPoint:
      return "CHECK_POINT";
    case Cmd::kResetDevice:
      return "RESET_DEVICE";
    case Cmd::kReadRawAdc:
      return "READ_RAW_ADC";
    case Cmd::kSetMode:
      return "SET_MODE";
    case Cmd::kGetMode:
      return "GET_MODE";
    case Cmd::kAck:
      return "ACK";
    case Cmd::kDeviceInfo:
      return "DEVICE_INFO";
    case Cmd::kSingleResult:
      return "SINGLE_RESULT";
    case Cmd::kBatchResult:
      return "BATCH_RESULT";
    case Cmd::kParamResult:
      return "PARAM_RESULT";
    case Cmd::kCheckResult:
      return "CHECK_RESULT";
    case Cmd::kModeResult:
      return "MODE_RESULT";
  }
  return "UNKNOWN";
}

}  // namespace eph205
