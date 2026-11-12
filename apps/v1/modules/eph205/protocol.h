#pragma once

// EPH205 二进制协议：AA 55 CMD LEN DATA SUM8
//
// CMD 起逐字节求和，自然溢出取低 8 位，覆盖 CMD/LEN/DATA。
// 帧总长 = 5 + LEN。
//
// 本文件只做编解码与常量，不含任何设备状态、串口或线程逻辑。

#include <cstdint>

namespace eph205 {

constexpr std::uint8_t kFrameHead0 = 0xAA;
constexpr std::uint8_t kFrameHead1 = 0x55;
constexpr int kFrameOverhead = 5;
constexpr int kMaxDataLen = 255;
constexpr int kMaxFrameLen = kFrameOverhead + kMaxDataLen;

// 一帧最多 62 组：5 + 4*62 = 253，63 组会超出单字节 LEN
constexpr int kMaxBatchGroups = 62;

constexpr int kMinPoint = 1;
constexpr int kMaxPoint = 640;

enum class Cmd : std::uint8_t {
  kGetDeviceInfo = 0x01,
  kMeasureSingle = 0x02,
  kMeasureBatch = 0x03,
  kSetParam = 0x04,
  kGetParam = 0x05,
  kCalibrate = 0x06,
  kCheckPoint = 0x07,
  kResetDevice = 0x08,
  kReadRawAdc = 0x09,
  kSetMode = 0x0A,
  kGetMode = 0x0B,
  kAck = 0x80,
  kDeviceInfo = 0x81,
  kSingleResult = 0x82,
  kBatchResult = 0x83,
  kParamResult = 0x84,
  kCheckResult = 0x85,
  kModeResult = 0x86,
};

enum class Status : std::uint8_t {
  kOk = 0x00,
  kSumError = 0x01,
  kCmdUnsupported = 0x02,
  kParamOutOfRange = 0x03,
  kPointInvalid = 0x04,
  kBusy = 0x05,
  kOverRange = 0x06,
  kFrameError = 0x07,
  kCalibFail = 0x08,
  kReserved09 = 0x09,
  kReserved0A = 0x0A,
  kAdcTimeout = 0x0B,
  kI2cError = 0x0C,
  kSpiError = 0x0D,
  kMuxError = 0x0E,
  kOutputNotReady = 0x0F,
};

enum class ParamType : std::uint8_t {
  kWireMode = 0x01,
  kRange = 0x02,
  kAverageTimes = 0x03,
  kSettleDelay = 0x04,
  kTimeout = 0x05,
};

enum class Mode : std::uint8_t { kCurrent = 0x00, kVoltage = 0x01 };

enum class Source : std::uint8_t {
  kDiscrete = 1,
  kHowland = 2,
  kAd7124 = 3,
};

enum class WireMode : std::uint8_t { kTwoWire = 0, kFourWire = 1 };

// 未设置过的恒流量程；GET_PARAM 会原样返回
constexpr std::uint8_t kRangeUnset = 0xFF;

// 链路/宿主侧错误：不是设备状态码，由设备层合成
enum class IoError : std::uint8_t {
  kNone = 0,
  kTimeout,
  kSumError,
  kFrameError,
  kSeqMismatch,
  kDisconnected,
  kBadResponse,
  kBusy,
};

enum class ParseState {
  kNeedMore,
  kFrame,
  kResync,
};

struct Frame {
  Cmd cmd = Cmd::kAck;
  std::uint8_t data[kMaxDataLen] = {};
  int len = 0;
};

struct ParseOutcome {
  ParseState state = ParseState::kNeedMore;
  int consumed = 0;
};

std::uint8_t Sum8(const std::uint8_t* data, int size);

// 组帧；缓冲不足或参数非法返回 0，否则返回写入字节数
int BuildFrame(Cmd cmd, const std::uint8_t* data, int len, std::uint8_t* out,
               int capacity);

// 从缓冲区头部解一帧。kNeedMore 不消费；kResync 按 consumed 丢弃后重调。
ParseOutcome ParseFrame(const std::uint8_t* buffer, int size, Frame* frame);

float ReadFloatLe(const std::uint8_t* in);
void WriteFloatLe(float value, std::uint8_t* out);

std::uint16_t ReadU16Le(const std::uint8_t* in);
std::uint32_t ReadU32Le(const std::uint8_t* in);
std::int32_t ReadI32Le(const std::uint8_t* in);
void PutU16Le(std::uint8_t* out, int value);
void PutU32Le(std::uint8_t* out, std::uint32_t value);

// 应答的固定 DATA 长度；-1 表示可变长度（仅 0x03）
int ExpectedResponseLen(Cmd request);

// 请求对应的应答命令码。0x04/0x06/0x08/0x0A 回 0x80，
// 0x02/0x09 回 0x82——靠"同刻只有一个未决请求"区分。
Cmd ExpectedResponse(Cmd request);

// 状态码转可读文本，界面与日志共用。
const char* StatusText(Status status);
const char* CmdText(Cmd cmd);

// 设备信息（0x81 应答）
struct DeviceInfo {
  std::uint16_t firmware_version = 0;
  std::uint16_t p_count = 0;
  std::uint16_t n_count = 0;
  std::uint8_t mux_boards = 0;
  std::uint8_t range_count = 0;
  Mode mode = Mode::kCurrent;
  Source source = Source::kHowland;
  std::uint16_t device_id = 0;
};

struct DeviceParams {
  WireMode wire = WireMode::kTwoWire;
  std::uint8_t range = kRangeUnset;
  std::uint8_t average_times = 1;
  std::uint8_t settle_delay = 0;
  std::uint8_t timeout = 0;
  bool output_enabled = false;
  std::uint8_t parallel_load = 0;
};

struct Reading {
  Status status = Status::kOk;
  float ohms = 0.0f;  // 失败时为 NaN
  std::uint32_t raw_adc = 0;
  std::int32_t differential_uv = 0;
};

struct PointPair {
  int p = 0;
  int n = 0;
};

}  // namespace eph205
