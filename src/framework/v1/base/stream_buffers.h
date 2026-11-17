#ifndef SRC_FRAMEWORK_V1_BASE_STREAM_BUFFERS_H_
#define SRC_FRAMEWORK_V1_BASE_STREAM_BUFFERS_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "base/stream_ring.h"

namespace v1 {

// record 类流里的一条：自己的时间戳 + 业务负载。
struct Record {
  uint64_t ts_ns = 0;
  std::vector<uint8_t> payload;
};

// 一个会话里所有订阅流的缓冲集合。
//
// 写侧（模块线程）调 PushFrame / PushRecords / SetState，
// 读侧（事件循环）调 TakeFrame。两类流的规则不同：
//   sample/record 进环形缓冲，写满淘汰最旧帧，序号在写入时分配；
//   state 只留最新一份，序号在真正发出时才分配（合并先于分配）。
class StreamBuffers {
 public:
  StreamBuffers() = default;
  StreamBuffers(const StreamBuffers&) = delete;
  StreamBuffers& operator=(const StreamBuffers&) = delete;

  // 订阅时登记。已经登记过的流保持原样，不重置序号。
  bool Open(uint32_t stream_id, std::size_t capacity_bytes, bool is_state);

  // 一个流要占多少预算：字节区 + 描述符上界（state 流不建环，记 0）。
  // 描述符必须算进来，小帧场景下它和字节区是同一个量级。
  static std::size_t ChargeFor(std::size_t capacity_bytes, bool is_state);

  // 当前已计入预算的合计，供会话级总量上限判断。
  std::size_t ChargedBytes() const;

  // 退订或会话结束时注销。
  void Close(uint32_t stream_id);

  // 写一帧（sample 类）。返回 false：没订阅、空帧、或帧比整个环还大。
  bool PushFrame(uint32_t stream_id, const uint8_t* data, std::size_t len,
                 uint64_t ts_ns);

  // 写一批记录（record 类）。通讯层负责按 [ts][len][payload] 逐条编码。
  bool PushRecords(uint32_t stream_id, const std::vector<Record>& records);

  // 写一个状态值（state 类）。只保留最新，不占号。
  void SetState(uint32_t stream_id, const uint8_t* data, std::size_t len);

  // 看下一帧有多长（不取出），供发送侧先按配额判断。state 流不看。
  bool Peek(uint32_t stream_id, std::size_t* len) const;

  // 取下一帧。state 流在这里才分配序号，没有新值就返回 false。
  // now_ns 只用于 state 帧的时间戳。
  bool TakeFrame(uint32_t stream_id, uint64_t now_ns, std::vector<uint8_t>* out,
                 uint32_t* seq, uint64_t* ts_ns);

  // 取走该流累计的溢出统计。
  bool TakeOverrun(uint32_t stream_id, StreamRing::Overrun* out);

  // 该流当前占用的字节数（描述符不计）。
  std::size_t Used(uint32_t stream_id) const;

 private:
  struct Entry {
    std::unique_ptr<StreamRing> ring;  // state 流不建环
    bool is_state = false;
    // 每个流一个计数器：sample/record 在写入时自增，state 在发出时自增
    std::atomic<uint32_t> seq{0};

    // state 用：最新值 + 脏标记，两者必须在同一把锁里读写
    std::mutex mutex;
    std::vector<uint8_t> state_payload;
    bool state_dirty = false;
  };

  std::shared_ptr<Entry> Find(uint32_t stream_id) const;

  mutable std::mutex mutex_;
  std::map<uint32_t, std::shared_ptr<Entry>> entries_;
  std::size_t charged_ = 0;
};

}  // namespace v1

#endif  // SRC_FRAMEWORK_V1_BASE_STREAM_BUFFERS_H_
