#ifndef SRC_FRAMEWORK_V1_BASE_STREAM_SINK_H_
#define SRC_FRAMEWORK_V1_BASE_STREAM_SINK_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "base/stream_buffers.h"

namespace v1 {

// 业务模块写数据的唯一入口。
//
// 线程安全，且不会因为网络慢而卡住：内部只写缓冲，不等发送。
// 返回 false 表示这次数据没被接收（没人订阅、流不存在、单帧过大），
// 模块可以据此计数，但不许重试、不许反压采集。
class StreamSink {
 public:
  virtual ~StreamSink() = default;

  // sample 类：一个负载就是一帧。ts_ns 是本帧第一个采样点的采集时刻，
  // 不是调用时刻——两者隔着整帧的采集时长（规范 §7.7）
  virtual bool PushSample(uint32_t stream_id, uint64_t ts_ns,
                          const uint8_t* data, std::size_t len) = 0;

  // record 类：一批记录打成一帧，时间戳取第一条。
  virtual bool PushRecords(uint32_t stream_id,
                           const std::vector<Record>& records) = 0;

  // state 类：只保留最新值，不占号、不排队。
  virtual void SetState(uint32_t stream_id, const uint8_t* data,
                        std::size_t len) = 0;
};

}  // namespace v1

#endif  // SRC_FRAMEWORK_V1_BASE_STREAM_SINK_H_
