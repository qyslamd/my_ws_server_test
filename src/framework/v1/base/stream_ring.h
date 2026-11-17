#ifndef SRC_FRAMEWORK_V1_BASE_STREAM_RING_H_
#define SRC_FRAMEWORK_V1_BASE_STREAM_RING_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace v1 {

// 单个流的定长字节环：按整帧写入、按整帧淘汰。
//
// 一个生产者线程写、事件循环读，内部用一把锁串起来。
// 写满时淘汰最旧的整帧，并把被淘汰的序号区间累计下来，
// 交给发送侧合并成 core.overrun 上报。
class StreamRing {
 public:
  // 一段连续被淘汰的帧。first_seq/last_seq 给出缺口的两端，
  // 客户端据此判断丢了多少、从哪丢到哪。
  struct Overrun {
    uint64_t dropped_frames = 0;
    uint64_t dropped_bytes = 0;
    uint32_t first_seq = 0;
    uint32_t last_seq = 0;
  };

  // capacity_bytes 是字节区容量，max_frames 是描述符条数上限。
  // 小帧场景下描述符本身会吃掉可观的内存，所以它要有独立的硬上限。
  explicit StreamRing(std::size_t capacity_bytes,
                      std::size_t max_frames = 65536);

  StreamRing(const StreamRing&) = delete;
  StreamRing& operator=(const StreamRing&) = delete;

  // 写一帧。返回 false 表示这一帧被拒收：长度为 0，或者比整个环还大。
  bool Push(const uint8_t* data, std::size_t len, uint32_t seq, uint64_t ts_ns);

  // 取最旧的一帧。没有数据时返回 false，out 不被改动。
  bool Pop(std::vector<uint8_t>* out, uint32_t* seq, uint64_t* ts_ns);

  // 只看最旧一帧的长度，不取出。发送侧要先按配额判断发不发得出去。
  bool PeekFront(std::size_t* len) const;

  // 取走累计的溢出统计并清零。没有溢出时返回 false。
  bool TakeOverrun(Overrun* out);

  std::size_t Capacity() const;
  std::size_t Used() const;
  std::size_t Frames() const;

 private:
  struct Slot {
    uint64_t offset;  // 单调写指针上的位置，访问时对容量取模
    uint32_t len;
    uint32_t seq;
    uint64_t ts;
  };

  void CopyIn(uint64_t offset, const uint8_t* data, std::size_t len);
  void CopyOut(uint64_t offset, uint8_t* data, std::size_t len);
  void EvictOldest();

  mutable std::mutex mutex_;
  std::vector<uint8_t> buffer_;
  std::deque<Slot> slots_;
  std::size_t max_frames_;
  uint64_t write_pos_ = 0;
  uint64_t read_pos_ = 0;
  Overrun overrun_;
};

}  // namespace v1

#endif  // SRC_FRAMEWORK_V1_BASE_STREAM_RING_H_
