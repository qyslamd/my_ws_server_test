#include "base/stream_buffers.h"

#include <string>
#include <utility>

#include "base/frame.h"

namespace v1 {
namespace {

// 描述符上限与单条大小，跟 StreamRing 的默认值保持一致
constexpr std::size_t kMaxFramesPerStream = 65536;
constexpr std::size_t kSlotBytes = 24;

}  // namespace

std::size_t StreamBuffers::ChargeFor(std::size_t capacity_bytes,
                                     bool is_state) {
  if (is_state) {
    return 0;  // state 不建环，只留一份最新值
  }
  return capacity_bytes + kMaxFramesPerStream * kSlotBytes;
}

std::size_t StreamBuffers::ChargedBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return charged_;
}

bool StreamBuffers::Open(uint32_t stream_id, std::size_t capacity_bytes,
                         bool is_state) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (entries_.find(stream_id) != entries_.end()) {
    return true;  // 已经登记过：保持原样，序号不重置
  }
  auto entry = std::make_shared<Entry>();
  entry->is_state = is_state;
  if (!is_state) {
    entry->ring = std::make_unique<StreamRing>(capacity_bytes);
  }
  entries_.emplace(stream_id, std::move(entry));
  charged_ += ChargeFor(capacity_bytes, is_state);
  return true;
}

void StreamBuffers::Close(uint32_t stream_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = entries_.find(stream_id);
  if (it == entries_.end()) {
    return;
  }
  // state 的值常驻到会话结束（规范 §12）：退订只是不再发送，值要留着，
  // 否则重新订阅拿不到当前值。它不建环，也就没有预算要退。
  if (it->second->is_state) {
    return;
  }
  charged_ -= ChargeFor(it->second->ring->Capacity(), false);
  entries_.erase(it);
}

std::shared_ptr<StreamBuffers::Entry> StreamBuffers::Find(
    uint32_t stream_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = entries_.find(stream_id);
  return it == entries_.end() ? nullptr : it->second;
}

bool StreamBuffers::PushFrame(uint32_t stream_id, const uint8_t* data,
                              std::size_t len, uint64_t ts_ns) {
  const std::shared_ptr<Entry> entry = Find(stream_id);
  if (entry == nullptr || entry->ring == nullptr) {
    return false;
  }
  const uint32_t seq = entry->seq.fetch_add(1) + 1;
  return entry->ring->Push(data, len, seq, ts_ns);
}

bool StreamBuffers::PushRecords(uint32_t stream_id,
                                const std::vector<Record>& records) {
  if (records.empty()) {
    return false;
  }
  std::vector<uint8_t> body;
  for (const Record& record : records) {
    AppendRecord(body, record.ts_ns, record.payload);
  }
  // 帧时间戳取第一条记录的时间：同一批记录本来就来自同一个采样时刻
  return PushFrame(stream_id, body.data(), body.size(), records.front().ts_ns);
}

void StreamBuffers::SetState(uint32_t stream_id, const uint8_t* data,
                             std::size_t len) {
  if (data == nullptr || len == 0) {
    return;
  }
  std::shared_ptr<Entry> entry = Find(stream_id);
  if (entry == nullptr) {
    // 还没订阅也要把值留下：订阅者一上来就该看到当前值（规范 §8.1 第 7 条）
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(stream_id);
    if (it == entries_.end()) {
      auto fresh = std::make_shared<Entry>();
      fresh->is_state = true;  // 只有 state 流会走 SetState
      it = entries_.emplace(stream_id, std::move(fresh)).first;
    }
    entry = it->second;
  }
  if (!entry->is_state) {
    return;  // 业务把 SetState 用在了非 state 流上：忽略
  }
  std::lock_guard<std::mutex> lock(entry->mutex);
  entry->state_payload.assign(data, data + len);
  entry->state_dirty = true;
}

bool StreamBuffers::Peek(uint32_t stream_id, std::size_t* len) const {
  const std::shared_ptr<Entry> entry = Find(stream_id);
  if (entry == nullptr || entry->ring == nullptr) {
    return false;
  }
  return entry->ring->PeekFront(len);
}

bool StreamBuffers::TakeFrame(uint32_t stream_id, uint64_t now_ns,
                              std::vector<uint8_t>* out, uint32_t* seq,
                              uint64_t* ts_ns) {
  const std::shared_ptr<Entry> entry = Find(stream_id);
  if (entry == nullptr) {
    return false;
  }
  if (entry->ring != nullptr) {
    return entry->ring->Pop(out, seq, ts_ns);
  }

  // state：取值和清脏在同一把锁里做完，否则生产者在这中间写入的新值会被吞掉
  std::lock_guard<std::mutex> lock(entry->mutex);
  if (!entry->state_dirty) {
    return false;
  }
  *out = entry->state_payload;
  *seq = entry->seq.fetch_add(1) + 1;
  *ts_ns = now_ns;
  entry->state_dirty = false;
  return true;
}

bool StreamBuffers::TakeOverrun(uint32_t stream_id, StreamRing::Overrun* out) {
  const std::shared_ptr<Entry> entry = Find(stream_id);
  if (entry == nullptr || entry->ring == nullptr) {
    return false;
  }
  return entry->ring->TakeOverrun(out);
}

std::size_t StreamBuffers::Used(uint32_t stream_id) const {
  const std::shared_ptr<Entry> entry = Find(stream_id);
  if (entry == nullptr) {
    return 0;
  }
  if (entry->ring != nullptr) {
    return entry->ring->Used();
  }
  std::lock_guard<std::mutex> lock(entry->mutex);
  return entry->state_payload.size();
}

}  // namespace v1
