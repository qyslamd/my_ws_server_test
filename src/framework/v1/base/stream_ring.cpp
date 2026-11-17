#include "base/stream_ring.h"

#include <algorithm>
#include <cstring>

namespace v1 {

StreamRing::StreamRing(std::size_t capacity_bytes, std::size_t max_frames)
    : buffer_(capacity_bytes), max_frames_(max_frames == 0 ? 1 : max_frames) {}

std::size_t StreamRing::Capacity() const { return buffer_.size(); }

std::size_t StreamRing::Used() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::size_t>(write_pos_ - read_pos_);
}

std::size_t StreamRing::Frames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return slots_.size();
}

// 从单调位置拷进环里。跨环尾时拆成两段，尾部的空洞不浪费。
void StreamRing::CopyIn(uint64_t offset, const uint8_t* data, std::size_t len) {
  const std::size_t cap = buffer_.size();
  const std::size_t pos = static_cast<std::size_t>(offset % cap);
  const std::size_t first = std::min(len, cap - pos);
  std::memcpy(buffer_.data() + pos, data, first);
  if (len > first) {
    std::memcpy(buffer_.data(), data + first, len - first);
  }
}

void StreamRing::CopyOut(uint64_t offset, uint8_t* data, std::size_t len) {
  const std::size_t cap = buffer_.size();
  const std::size_t pos = static_cast<std::size_t>(offset % cap);
  const std::size_t first = std::min(len, cap - pos);
  std::memcpy(data, buffer_.data() + pos, first);
  if (len > first) {
    std::memcpy(data + first, buffer_.data(), len - first);
  }
}

// 淘汰最旧的一帧并记账。调用方已持锁。
void StreamRing::EvictOldest() {
  const Slot oldest = slots_.front();
  slots_.pop_front();
  if (overrun_.dropped_frames == 0) {
    overrun_.first_seq = oldest.seq;
  }
  overrun_.last_seq = oldest.seq;
  ++overrun_.dropped_frames;
  overrun_.dropped_bytes += oldest.len;
  read_pos_ += oldest.len;
}

bool StreamRing::Push(const uint8_t* data, std::size_t len, uint32_t seq,
                      uint64_t ts_ns) {
  if (data == nullptr || len == 0 || len > buffer_.size()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  // 字节装不下、或者描述符到顶，都从最旧的整帧开始丢
  while (write_pos_ - read_pos_ + len > buffer_.size() ||
         slots_.size() >= max_frames_) {
    if (slots_.empty()) {
      return false;
    }
    EvictOldest();
  }

  CopyIn(write_pos_, data, len);
  slots_.push_back(Slot{write_pos_, static_cast<uint32_t>(len), seq, ts_ns});
  write_pos_ += len;
  return true;
}

bool StreamRing::Pop(std::vector<uint8_t>* out, uint32_t* seq,
                     uint64_t* ts_ns) {
  if (out == nullptr || seq == nullptr || ts_ns == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (slots_.empty()) {
    return false;
  }

  const Slot oldest = slots_.front();
  slots_.pop_front();
  out->resize(oldest.len);
  CopyOut(oldest.offset, out->data(), oldest.len);
  read_pos_ += oldest.len;
  *seq = oldest.seq;
  *ts_ns = oldest.ts;
  return true;
}

bool StreamRing::PeekFront(std::size_t* len) const {
  if (len == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (slots_.empty()) {
    return false;
  }
  *len = slots_.front().len;
  return true;
}

bool StreamRing::TakeOverrun(Overrun* out) {
  if (out == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (overrun_.dropped_frames == 0) {
    return false;
  }
  *out = overrun_;
  overrun_ = Overrun{};
  return true;
}

}  // namespace v1
