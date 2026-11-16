#include "base/frame.h"

#include <cstring>

namespace v1 {
namespace {

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void PutU64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
  }
}

void AppendBlock(std::vector<uint8_t>& out, uint8_t type,
                 const uint8_t* data, uint32_t len) {
  out.push_back(type);
  PutU32(out, len);
  if (len > 0) {
    out.insert(out.end(), data, data + len);
  }
}

}  // namespace

void AppendRecord(std::vector<uint8_t>& out, uint64_t ts_ns,
                  const std::vector<uint8_t>& payload) {
  PutU64(out, ts_ns);
  PutU32(out, static_cast<uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> BuildFrame(uint32_t stream_id, uint32_t seq,
                                uint64_t ts_ns, uint8_t opcode, bool with_meta,
                                const std::string& meta,
                                const std::vector<uint8_t>& data,
                                const std::vector<uint8_t>& records) {
  std::vector<uint8_t> body;
  if (!data.empty()) {
    AppendBlock(body, kBlockData, data.data(), static_cast<uint32_t>(data.size()));
  }
  if (with_meta && !meta.empty()) {
    AppendBlock(body, kBlockMeta,
                reinterpret_cast<const uint8_t*>(meta.data()),
                static_cast<uint32_t>(meta.size()));
  }
  if (!records.empty()) {
    AppendBlock(body, kBlockRecords, records.data(),
                static_cast<uint32_t>(records.size()));
  }

  std::vector<uint8_t> frame;
  frame.reserve(24 + body.size());
  frame.push_back(0xA5);
  frame.push_back(0x01);
  frame.push_back(opcode);
  frame.push_back(0x00);
  PutU32(frame, stream_id);
  PutU32(frame, seq);
  PutU64(frame, ts_ns);
  PutU32(frame, static_cast<uint32_t>(body.size()));
  frame.insert(frame.end(), body.begin(), body.end());
  return frame;
}

}  // namespace v1
