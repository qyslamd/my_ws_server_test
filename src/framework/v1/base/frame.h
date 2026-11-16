#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace v1 {

enum Opcode : uint8_t {
  kSnapshot = 0x01,
  kDelta = 0x02,
  kEvent = 0x03,
  kError = 0x04,
  kHeartbeat = 0x05,
};

enum BlockType : uint8_t {
  kBlockData = 0x01,
  kBlockMeta = 0x02,
  kBlockRecords = 0x03,
  kBlockAux = 0x04,
};

// 24-byte little-endian header:
// magic, version, opcode, flags, streamId(u32), seq(u32), ts(u64), bodyLen(u32)
std::vector<uint8_t> BuildFrame(uint32_t stream_id, uint32_t seq, uint64_t ts_ns,
                                uint8_t opcode, bool with_meta,
                                const std::string& meta,
                                const std::vector<uint8_t>& data,
                                const std::vector<uint8_t>& records);

// records: repeated [ts u64][len u32][payload]
void AppendRecord(std::vector<uint8_t>& out, uint64_t ts_ns,
                  const std::vector<uint8_t>& payload);

}  // namespace v1
