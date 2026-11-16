#include "base/frame.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

// 帧头固定 24 字节，字段全部小端：
// magic(1) ver(1) opcode(1) flags(1) streamId(u32) seq(u32) ts(u64)
// bodyLen(u32)；body 由若干块拼成，每块是 type(1) + len(u32) + payload。
constexpr size_t kHeaderSize = 24;

uint32_t ReadU32(const std::vector<uint8_t>& buf, size_t off) {
  return static_cast<uint32_t>(buf[off]) |
         (static_cast<uint32_t>(buf[off + 1]) << 8) |
         (static_cast<uint32_t>(buf[off + 2]) << 16) |
         (static_cast<uint32_t>(buf[off + 3]) << 24);
}

uint64_t ReadU64(const std::vector<uint8_t>& buf, size_t off) {
  uint64_t v = 0;
  for (size_t i = 0; i < 8; ++i) {
    v |= static_cast<uint64_t>(buf[off + i]) << (8 * i);
  }
  return v;
}

std::vector<uint8_t> Slice(const std::vector<uint8_t>& buf, size_t off,
                           size_t len) {
  return std::vector<uint8_t>(buf.begin() + static_cast<ptrdiff_t>(off),
                              buf.begin() + static_cast<ptrdiff_t>(off + len));
}

// 一帧只有头、没有 body，用于验证头部字段本身。
std::vector<uint8_t> HeaderOnly(uint32_t stream_id, uint32_t seq,
                                uint64_t ts_ns, uint8_t opcode) {
  return v1::BuildFrame(stream_id, seq, ts_ns, opcode, false, "", {}, {});
}

TEST(FrameHeader, MagicVersionOpcodeFlags) {
  const std::vector<uint8_t> frame =
      HeaderOnly(1, 0, 0, static_cast<uint8_t>(v1::kSnapshot));
  ASSERT_EQ(kHeaderSize, frame.size());
  EXPECT_EQ(0xA5, frame[0]);
  EXPECT_EQ(0x01, frame[1]);
  EXPECT_EQ(static_cast<uint8_t>(v1::kSnapshot), frame[2]);
  EXPECT_EQ(0x00, frame[3]);
}

TEST(FrameHeader, EmptyFrameEndsAfterHeader) {
  const std::vector<uint8_t> frame = HeaderOnly(7, 0, 0, v1::kHeartbeat);
  EXPECT_EQ(kHeaderSize, frame.size());
  // bodyLen 为 0，且头之后确实没有多余字节
  EXPECT_EQ(0u, ReadU32(frame, 20));
}

TEST(FrameHeader, NumbersAreLittleEndian) {
  const std::vector<uint8_t> frame =
      HeaderOnly(0x01020304u, 0x0A0B0C0Du, 0x1122334455667788ull,
                 static_cast<uint8_t>(v1::kDelta));
  EXPECT_EQ(0x01020304u, ReadU32(frame, 4));
  EXPECT_EQ(0x0A0B0C0Du, ReadU32(frame, 8));
  EXPECT_EQ(0x1122334455667788ull, ReadU64(frame, 12));
  // 逐字节再核一次，防止读写两边一起错
  EXPECT_EQ(0x04, frame[4]);
  EXPECT_EQ(0x03, frame[5]);
  EXPECT_EQ(0x02, frame[6]);
  EXPECT_EQ(0x01, frame[7]);
  EXPECT_EQ(0x88, frame[12]);
  EXPECT_EQ(0x11, frame[19]);
}

TEST(FrameBody, DataBlockWrapsPayload) {
  const std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
  const std::vector<uint8_t> frame =
      v1::BuildFrame(1, 1, 0, v1::kSnapshot, false, "", data, {});

  ASSERT_EQ(kHeaderSize + 1 + 4 + data.size(), frame.size());
  EXPECT_EQ(9u, ReadU32(frame, 20));  // bodyLen = type + len + payload
  EXPECT_EQ(static_cast<uint8_t>(v1::kBlockData), frame[24]);
  EXPECT_EQ(4u, ReadU32(frame, 25));
  EXPECT_EQ(data, Slice(frame, 29, data.size()));
}

TEST(FrameBody, MetaIsSkippedWhenFlagIsOff) {
  const std::vector<uint8_t> data = {0x01};
  const std::vector<uint8_t> frame =
      v1::BuildFrame(1, 1, 0, v1::kSnapshot, false, "ignored", data, {});
  // 只有 data 块：给 with_meta=false 传了 meta 也不该写进去
  EXPECT_EQ(6u, ReadU32(frame, 20));
  EXPECT_EQ(static_cast<uint8_t>(v1::kBlockData), frame[24]);
}

TEST(FrameBody, EmptyMetaIsNotWritten) {
  const std::vector<uint8_t> frame =
      v1::BuildFrame(1, 1, 0, v1::kSnapshot, true, "", {0x01}, {});
  EXPECT_EQ(6u, ReadU32(frame, 20));  // 空 meta 不占一个块
}

TEST(FrameBody, BlocksAreOrderedDataMetaRecords) {
  std::vector<uint8_t> records;
  v1::AppendRecord(records, 5, {0x11});

  const std::vector<uint8_t> frame =
      v1::BuildFrame(1, 1, 0, v1::kSnapshot, true, "hi", {0xAA, 0xBB}, records);

  // data: 1+4+2 = 7，meta: 1+4+2 = 7，records: 1+4+13 = 18
  ASSERT_EQ(kHeaderSize + 7 + 7 + 18, frame.size());
  EXPECT_EQ(32u, ReadU32(frame, 20));

  EXPECT_EQ(static_cast<uint8_t>(v1::kBlockData), frame[24]);
  EXPECT_EQ(2u, ReadU32(frame, 25));
  EXPECT_EQ((std::vector<uint8_t>{0xAA, 0xBB}), Slice(frame, 29, 2));

  EXPECT_EQ(static_cast<uint8_t>(v1::kBlockMeta), frame[31]);
  EXPECT_EQ(2u, ReadU32(frame, 32));
  EXPECT_EQ((std::vector<uint8_t>{'h', 'i'}), Slice(frame, 36, 2));

  EXPECT_EQ(static_cast<uint8_t>(v1::kBlockRecords), frame[38]);
  EXPECT_EQ(13u, ReadU32(frame, 39));
}

TEST(FrameRecords, RecordIsTimestampThenLengthThenPayload) {
  std::vector<uint8_t> records;
  v1::AppendRecord(records, 0x1122334455667788ull, {0x01, 0x02});
  ASSERT_EQ(8 + 4 + 2, records.size());
  EXPECT_EQ(0x1122334455667788ull, ReadU64(records, 0));
  EXPECT_EQ(2u, ReadU32(records, 8));
  EXPECT_EQ((std::vector<uint8_t>{0x01, 0x02}), Slice(records, 12, 2));
}

TEST(FrameRecords, RecordsBlockKeepsBackToBackRecords) {
  std::vector<uint8_t> records;
  v1::AppendRecord(records, 5, {0x11});
  v1::AppendRecord(records, 6, {});

  const std::vector<uint8_t> frame =
      v1::BuildFrame(1, 1, 0, v1::kSnapshot, false, "", {}, records);

  ASSERT_EQ(kHeaderSize + 1 + 4 + records.size(), frame.size());
  EXPECT_EQ(static_cast<uint8_t>(v1::kBlockRecords), frame[24]);
  EXPECT_EQ(records.size(), ReadU32(frame, 25));
  EXPECT_EQ(records, Slice(frame, 29, records.size()));
  // 第一条记录 13 字节（8 时间戳 + 4 长度 + 1 载荷）：
  // 第二条的 ts 从 29+13 起，其长度字段在再往后 8 字节，值为 0
  EXPECT_EQ(0u, ReadU32(frame, 29 + 13 + 8));
}

}  // namespace
