#include "base/stream_ring.h"

#include <cstdint>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

// 用可辨认的内容造帧：第 n 帧就是 n 个重复字符，读出时一眼能看出串没串。
std::vector<uint8_t> Frame(char fill, size_t len) {
  return std::vector<uint8_t>(len, static_cast<uint8_t>(fill));
}

std::string Text(const std::vector<uint8_t>& data) {
  return std::string(data.begin(), data.end());
}

}  // namespace

TEST(StreamRing, EmptyRingHasNothingToPop) {
  v1::StreamRing ring(64);
  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;

  EXPECT_FALSE(ring.Pop(&out, &seq, &ts));
  EXPECT_EQ(0u, ring.Used());
  EXPECT_EQ(0u, ring.Frames());
}

TEST(StreamRing, PushedFrameComesBackUnchanged) {
  v1::StreamRing ring(1024);
  const std::vector<uint8_t> payload = Frame('a', 11);

  ASSERT_TRUE(ring.Push(payload.data(), payload.size(), 7, 123456789));
  EXPECT_EQ(11u, ring.Used());
  EXPECT_EQ(1u, ring.Frames());

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  ASSERT_TRUE(ring.Pop(&out, &seq, &ts));
  EXPECT_EQ("aaaaaaaaaaa", Text(out));
  EXPECT_EQ(7u, seq);
  EXPECT_EQ(123456789u, ts);
  EXPECT_EQ(0u, ring.Used());
  EXPECT_EQ(0u, ring.Frames());
}

TEST(StreamRing, FrameLargerThanCapacityIsRejected) {
  v1::StreamRing ring(16);
  const std::vector<uint8_t> payload = Frame('x', 17);

  EXPECT_FALSE(ring.Push(payload.data(), payload.size(), 1, 0));
  EXPECT_EQ(0u, ring.Used());
}

TEST(StreamRing, ZeroLengthFrameIsRejected) {
  v1::StreamRing ring(16);
  EXPECT_FALSE(ring.Push(nullptr, 0, 1, 0));
  EXPECT_EQ(0u, ring.Frames());
}

// 环只有 30 字节，每帧 10 字节：第 4 帧进来时必须挤掉第 1 帧。
TEST(StreamRing, FullRingDropsOldestFrame) {
  v1::StreamRing ring(30);
  for (uint32_t seq = 1; seq <= 4; ++seq) {
    const std::vector<uint8_t> payload =
        Frame(static_cast<char>('a' + seq), 10);
    ASSERT_TRUE(ring.Push(payload.data(), payload.size(), seq, seq));
  }

  EXPECT_EQ(3u, ring.Frames());
  EXPECT_EQ(30u, ring.Used());

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  ASSERT_TRUE(ring.Pop(&out, &seq, &ts));
  EXPECT_EQ(2u, seq) << "最旧的第 1 帧应该已经被挤掉";
}

TEST(StreamRing, OverrunCoversEveryDroppedFrame) {
  v1::StreamRing ring(30);
  for (uint32_t seq = 1; seq <= 5; ++seq) {
    const std::vector<uint8_t> payload = Frame('a', 10);
    ASSERT_TRUE(ring.Push(payload.data(), payload.size(), seq, seq));
  }

  v1::StreamRing::Overrun overrun;
  ASSERT_TRUE(ring.TakeOverrun(&overrun));
  EXPECT_EQ(2u, overrun.dropped_frames);
  EXPECT_EQ(20u, overrun.dropped_bytes);
  EXPECT_EQ(1u, overrun.first_seq);
  EXPECT_EQ(2u, overrun.last_seq);
}

TEST(StreamRing, OverrunIsTakenOnlyOnce) {
  v1::StreamRing ring(20);
  for (uint32_t seq = 1; seq <= 3; ++seq) {
    const std::vector<uint8_t> payload = Frame('a', 10);
    ASSERT_TRUE(ring.Push(payload.data(), payload.size(), seq, seq));
  }

  v1::StreamRing::Overrun overrun;
  ASSERT_TRUE(ring.TakeOverrun(&overrun));
  EXPECT_FALSE(ring.TakeOverrun(&overrun)) << "取过一次就该清零";
}

// 容量 16、帧长 5：写到第 4 帧时正好跨过环尾，内容不许错位。
TEST(StreamRing, FrameAcrossRingTailKeepsContent) {
  v1::StreamRing ring(16);
  const std::vector<uint8_t> a = Frame('a', 5);
  const std::vector<uint8_t> b = Frame('b', 5);
  const std::vector<uint8_t> c = Frame('c', 5);
  const std::vector<uint8_t> d = Frame('d', 5);

  ASSERT_TRUE(ring.Push(a.data(), a.size(), 1, 1));
  ASSERT_TRUE(ring.Push(b.data(), b.size(), 2, 2));
  ASSERT_TRUE(ring.Push(c.data(), c.size(), 3, 3));

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  ASSERT_TRUE(ring.Pop(&out, &seq, &ts));

  // 读指针到 5，接下来两帧会跨过 16 字节的环尾
  ASSERT_TRUE(ring.Push(d.data(), d.size(), 4, 4));

  ASSERT_TRUE(ring.Pop(&out, &seq, &ts));
  EXPECT_EQ("bbbbb", Text(out));
  ASSERT_TRUE(ring.Pop(&out, &seq, &ts));
  EXPECT_EQ("ccccc", Text(out));
  ASSERT_TRUE(ring.Pop(&out, &seq, &ts));
  EXPECT_EQ("ddddd", Text(out)) << "跨环尾写入后读出来要一字不差";
}

// 发送侧要先知道下一帧多大，再决定发不发：看，但不能取走。
TEST(StreamRing, PeekFrontReportsLengthWithoutConsuming) {
  v1::StreamRing ring(64);
  const std::vector<uint8_t> first = Frame('a', 7);
  const std::vector<uint8_t> second = Frame('b', 9);
  ASSERT_TRUE(ring.Push(first.data(), first.size(), 1, 1));
  ASSERT_TRUE(ring.Push(second.data(), second.size(), 2, 2));

  std::size_t len = 0;
  ASSERT_TRUE(ring.PeekFront(&len));
  EXPECT_EQ(7u, len);

  std::vector<uint8_t> out;
  uint32_t seq = 0;
  uint64_t ts = 0;
  ASSERT_TRUE(ring.Pop(&out, &seq, &ts));
  EXPECT_EQ("aaaaaaa", Text(out)) << "Peek 不许把帧取走";
  EXPECT_EQ(1u, seq);
}

TEST(StreamRing, PeekFrontOnEmptyReturnsFalse) {
  v1::StreamRing ring(64);
  std::size_t len = 0;
  EXPECT_FALSE(ring.PeekFront(&len));
}

// max_frames 是描述符数量的硬上限：小帧再多也不能让元数据无限涨。
TEST(StreamRing, FrameCountIsCapped) {
  v1::StreamRing ring(1024, 3);
  for (uint32_t seq = 1; seq <= 5; ++seq) {
    const std::vector<uint8_t> payload = Frame('a', 10);
    ASSERT_TRUE(ring.Push(payload.data(), payload.size(), seq, seq));
  }

  EXPECT_EQ(3u, ring.Frames());
  v1::StreamRing::Overrun overrun;
  ASSERT_TRUE(ring.TakeOverrun(&overrun));
  EXPECT_EQ(2u, overrun.dropped_frames);
  EXPECT_EQ(1u, overrun.first_seq);
  EXPECT_EQ(2u, overrun.last_seq);
}
