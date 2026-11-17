#include "base/stream_buffers.h"

#include <cstdint>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

std::vector<uint8_t> Bytes(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::string Text(const std::vector<uint8_t>& data) {
  return std::string(data.begin(), data.end());
}

v1::Record MakeRecord(uint64_t ts_ns, const std::string& text) {
  v1::Record record;
  record.ts_ns = ts_ns;
  record.payload = Bytes(text);
  return record;
}

}  // namespace

TEST(StreamBuffers, PushThenTakeKeepsPayloadAndSeq) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(1, 1024, false));

  const std::vector<uint8_t> payload = Bytes("hello");
  ASSERT_TRUE(buffers.PushFrame(1, payload.data(), payload.size(), 900));

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  ASSERT_TRUE(buffers.TakeFrame(1, 1234, &out, &seq, &ts));
  EXPECT_EQ("hello", Text(out));
  EXPECT_EQ(1u, seq) << "第一个帧的序号从 1 开始";
  EXPECT_EQ(900u, ts) << "帧时间戳用写入时的值，不是取出的时刻";
}

TEST(StreamBuffers, SeqIncreasesAcrossPushes) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(1, 1024, false));

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  for (uint32_t expected = 1; expected <= 3; ++expected) {
    const std::vector<uint8_t> payload = Bytes("x");
    ASSERT_TRUE(buffers.PushFrame(1, payload.data(), payload.size(), 0));
    ASSERT_TRUE(buffers.TakeFrame(1, 0, &out, &seq, &ts));
    EXPECT_EQ(expected, seq);
  }
}

TEST(StreamBuffers, UnknownStreamIsRejected) {
  v1::StreamBuffers buffers;
  const std::vector<uint8_t> payload = Bytes("x");
  EXPECT_FALSE(buffers.PushFrame(7, payload.data(), payload.size(), 0))
      << "没订阅就不入队";

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  EXPECT_FALSE(buffers.TakeFrame(7, 0, &out, &seq, &ts));
}

TEST(StreamBuffers, ClosedStreamStopsAccepting) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(1, 1024, false));
  buffers.Close(1);

  const std::vector<uint8_t> payload = Bytes("x");
  EXPECT_FALSE(buffers.PushFrame(1, payload.data(), payload.size(), 0));

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  EXPECT_FALSE(buffers.TakeFrame(1, 0, &out, &seq, &ts));
}

// 一批记录要按 [ts u64][len u32][payload] 逐条编进同一个负载里。
TEST(StreamBuffers, PushRecordsEncodesEachRecord) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(1, 1024, false));

  std::vector<v1::Record> records = {MakeRecord(100, "ab"),
                                     MakeRecord(200, "xyz")};
  ASSERT_TRUE(buffers.PushRecords(1, records));

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  ASSERT_TRUE(buffers.TakeFrame(1, 0, &out, &seq, &ts));

  ASSERT_EQ(29u, out.size());
  EXPECT_EQ(100u, ts) << "帧时间戳取第一条记录的时间";
  // 第一条：ts=100(len 8B) + len=2(4B) + "ab"
  uint64_t first_ts = 0;
  for (int i = 0; i < 8; ++i) {
    first_ts |= static_cast<uint64_t>(out[i]) << (8 * i);
  }
  EXPECT_EQ(100u, first_ts);
  uint32_t first_len = 0;
  for (int i = 0; i < 4; ++i) {
    first_len |= static_cast<uint32_t>(out[8 + i]) << (8 * i);
  }
  EXPECT_EQ(2u, first_len);
  EXPECT_EQ("ab",
            Text(std::vector<uint8_t>(out.begin() + 12, out.begin() + 14)));
}

// 预算要把描述符算进去：小帧场景下它和字节区是同一个量级。
TEST(StreamBuffers, ChargeCountsBytesAndDescriptors) {
  const std::size_t charge =
      v1::StreamBuffers::ChargeFor(8 * 1024 * 1024, false);
  EXPECT_EQ(8u * 1024 * 1024 + 65536u * 24u, charge)
      << "8 MiB 字节区 + 65536 条描述符 × 24 字节";
  EXPECT_EQ(0u, v1::StreamBuffers::ChargeFor(4 * 1024 * 1024, true))
      << "state 不建环，不占预算";
}

TEST(StreamBuffers, ChargedBytesTracksOpenAndClose) {
  v1::StreamBuffers buffers;
  EXPECT_EQ(0u, buffers.ChargedBytes());

  ASSERT_TRUE(buffers.Open(1, 1024, false));
  const std::size_t one = v1::StreamBuffers::ChargeFor(1024, false);
  EXPECT_EQ(one, buffers.ChargedBytes());

  ASSERT_TRUE(buffers.Open(2, 2048, false));
  EXPECT_EQ(one + v1::StreamBuffers::ChargeFor(2048, false),
            buffers.ChargedBytes());

  buffers.Close(1);
  EXPECT_EQ(v1::StreamBuffers::ChargeFor(2048, false), buffers.ChargedBytes())
      << "退订要把预算还回来";
}

TEST(StreamBuffers, PeekReportsFrontFrameLength) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(1, 1024, false));
  const std::vector<uint8_t> payload = Bytes("12345");
  ASSERT_TRUE(buffers.PushFrame(1, payload.data(), payload.size(), 0));

  std::size_t len = 0;
  ASSERT_TRUE(buffers.Peek(1, &len));
  EXPECT_EQ(5u, len);
}

TEST(StreamBuffers, EmptyTakeReturnsFalse) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(1, 1024, false));

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  EXPECT_FALSE(buffers.TakeFrame(1, 0, &out, &seq, &ts));
}

// state 只保留最新值：连写三次，只取出一帧，内容是最新那次。
TEST(StreamBuffers, StateCoalescesToLatestValue) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(1, 4096, true));

  for (const std::string& text : {"v1", "v2", "v3"}) {
    const std::vector<uint8_t> payload = Bytes(text);
    buffers.SetState(1, payload.data(), payload.size());
  }

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  ASSERT_TRUE(buffers.TakeFrame(1, 5000, &out, &seq, &ts));
  EXPECT_EQ("v3", Text(out));
  EXPECT_EQ(1u, seq) << "中间值不占号";
  EXPECT_EQ(5000u, ts) << "state 帧的时间取发出的时刻";

  EXPECT_FALSE(buffers.TakeFrame(1, 5000, &out, &seq, &ts)) << "取过就不许再出";
}

TEST(StreamBuffers, StateSeqAdvancesOnlyWhenTaken) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(1, 4096, true));

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;

  const std::vector<uint8_t> first = Bytes("a");
  buffers.SetState(1, first.data(), first.size());
  ASSERT_TRUE(buffers.TakeFrame(1, 1, &out, &seq, &ts));
  EXPECT_EQ(1u, seq);

  const std::vector<uint8_t> second = Bytes("b");
  buffers.SetState(1, second.data(), second.size());
  ASSERT_TRUE(buffers.TakeFrame(1, 2, &out, &seq, &ts));
  EXPECT_EQ(2u, seq);
  EXPECT_EQ("b", Text(out));
}

// 还没人订阅时写下的状态值也要留着：订阅者一上来就该看到当前值
TEST(StreamBuffers, StateKeptBeforeSubscribe) {
  v1::StreamBuffers buffers;
  const std::vector<uint8_t> payload = Bytes("idle");
  buffers.SetState(7, payload.data(), payload.size());

  ASSERT_TRUE(buffers.Open(7, 0, true));

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  ASSERT_TRUE(buffers.TakeFrame(7, 100, &out, &seq, &ts));
  EXPECT_EQ("idle", Text(out));
}

// 退订只停发送，值留到会话结束（规范 §12），重新订阅还能拿到
TEST(StreamBuffers, StateSurvivesUnsubscribe) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(7, 0, true));
  const std::vector<uint8_t> payload = Bytes("ready");
  buffers.SetState(7, payload.data(), payload.size());

  buffers.Close(7);
  ASSERT_TRUE(buffers.Open(7, 0, true));

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  ASSERT_TRUE(buffers.TakeFrame(7, 100, &out, &seq, &ts));
  EXPECT_EQ("ready", Text(out));
}

// 环写满后淘汰的统计要能透传出来，否则 core.overrun 报不出去。
TEST(StreamBuffers, OverrunIsReported) {
  v1::StreamBuffers buffers;
  ASSERT_TRUE(buffers.Open(1, 20, false));

  for (uint32_t i = 0; i < 3; ++i) {
    const std::vector<uint8_t> payload = Bytes("0123456789");
    ASSERT_TRUE(buffers.PushFrame(1, payload.data(), payload.size(), 0));
  }

  v1::StreamRing::Overrun overrun;
  ASSERT_TRUE(buffers.TakeOverrun(1, &overrun));
  EXPECT_EQ(1u, overrun.dropped_frames);
  EXPECT_EQ(1u, overrun.first_seq);
  EXPECT_EQ(1u, overrun.last_seq);
}
