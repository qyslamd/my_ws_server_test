// 集成冒烟：起真的服务端，用 cpp-httplib 当客户端走一遍。
//
// 与 framework/ 下的单测不同，这里要起线程、占端口，验的是
// "服务端 + mongoose + 网络" 这条链路本身通不通。
// 协议细节（帧、配额、溢出）不在这个文件的范围内。

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "base/server.h"
#include "httplib.h"

namespace {

// 端口不写死一个：被占用时 Start() 返回 false，顺着往后试。
constexpr int kFirstPort = 18870;
constexpr int kPortTries = 30;

struct RunningServer {
  std::unique_ptr<v1::V1Server> server;
  std::thread loop;
  int port = 0;
};

bool StartOnFreePort(v1::Module* module, RunningServer* out) {
  for (int port = kFirstPort; port < kFirstPort + kPortTries; ++port) {
    auto server = std::make_unique<v1::V1Server>(port);
    if (module != nullptr) {
      server->RegisterModule(module);
    }
    if (!server->Start()) {
      continue;
    }
    out->server = std::move(server);
    out->port = port;
    // Run() 内部会再判一次 running_，不会重复监听
    v1::V1Server* raw = out->server.get();
    out->loop = std::thread([raw] { raw->Run(); });
    return true;
  }
  return false;
}

void StopServer(RunningServer* s) {
  if (s->server != nullptr) {
    s->server->Stop();
  }
  if (s->loop.joinable()) {
    s->loop.join();
  }
}

// 最小可用的假模块：两条流，不碰任何硬件。用它把
// "流清单 → 订阅 → 归还配额" 这条链路端到端跑起来。
class FakeModule : public v1::Module {
 public:
  FakeModule() {
    streams_ = {
        {1, "fake.state", "state",
         R"({"kind":"fake","class":"state","schemaVer":1,)"
         R"("timebase":"unix_ns","layout":{"state":{"fields":[)"
         R"({"name":"v","type":"u32"}]}}})"},
        {2, "fake.wave", "sample",
         R"({"kind":"fake","class":"sample","schemaVer":1,)"
         R"("timebase":"unix_ns","layout":{"dataType":"f32",)"
         R"("components":1,"count":1}})"},
    };
  }

  std::string ns() const override { return "fake"; }
  const std::vector<v1::StreamInfo>& streams() const override {
    return streams_;
  }
  bool running() const override { return running_; }
  void AttachSink(v1::StreamSink* sink) override { sink_ = sink; }
  bool Start(std::string&) override {
    running_ = true;
    return true;
  }
  bool Stop(std::string&) override {
    running_ = false;
    return true;
  }
  nlohmann::json State() const override { return {{"running", running_}}; }
  nlohmann::json Describe() const override {
    return {
        {"ns", "fake"}, {"running", running_}, {"streams", streams_.size()}};
  }
  bool Command(const std::string&, const nlohmann::json&, nlohmann::json&,
               std::string&) override {
    return false;
  }
  bool OwnsParamScope(const std::string&) const override { return false; }
  nlohmann::json GetParams(const std::string&) const override {
    return nlohmann::json::object();
  }
  bool SetParams(const std::string&, const nlohmann::json&, bool,
                 nlohmann::json&, std::string&) override {
    return true;
  }
  nlohmann::json Logs() const override { return nlohmann::json::array(); }

  v1::StreamSink* sink() const { return sink_; }

 private:
  std::vector<v1::StreamInfo> streams_;
  v1::StreamSink* sink_ = nullptr;
  bool running_ = false;
};

}  // namespace

class ServerSmokeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(StartOnFreePort(&fake_, &running_)) << "no free port in range";
    WaitReady();
  }

  void TearDown() override { StopServer(&running_); }

  // 监听成功不等于事件循环已经在转，轮询一次健康检查等它应答
  void WaitReady() {
    httplib::Client cli("127.0.0.1", running_.port);
    cli.set_connection_timeout(1, 0);
    cli.set_read_timeout(1, 0);
    for (int i = 0; i < 100; ++i) {
      auto res = cli.Get("/api/v1/health");
      if (res && res->status == 200) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    FAIL() << "server did not answer within 2s";
  }

  std::string WsUrl(const char* sid) const {
    return "ws://127.0.0.1:" + std::to_string(running_.port) + "/ws?sid=" + sid;
  }

  // 先声明、后销毁：服务端活着的时候模块必须在
  FakeModule fake_;
  RunningServer running_;
};

// 能力查询不需要会话，客户端靠它先确认服务端支持什么
TEST_F(ServerSmokeTest, CapabilitiesNeedNoSession) {
  httplib::Client cli("127.0.0.1", running_.port);
  auto res = cli.Get("/api/v1/capabilities");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_NE(res->body.find("\"proto\":1"), std::string::npos);
  // 规范 §6.3：v1 只声明 credit，分片与作业不在范围内
  EXPECT_NE(res->body.find("\"caps\":[\"credit\"]"), std::string::npos);
  EXPECT_EQ(res->body.find("\"frag\""), std::string::npos);
  EXPECT_EQ(res->body.find("\"jobs\""), std::string::npos);
  EXPECT_NE(res->body.find("\"send_queue_drain_ms\":"), std::string::npos);
  EXPECT_NE(res->body.find("\"stop_timeout_ms\":"), std::string::npos);
  EXPECT_EQ(res->body.find("\"frag_timeout_ms\":"), std::string::npos);
}

// 没有 WS 就没有会话，需要会话的接口一律被挡住
TEST_F(ServerSmokeTest, HttpWithoutSessionIsRejected) {
  httplib::Client cli("127.0.0.1", running_.port);
  auto res = cli.Get("/api/v1/streams");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 409);
  EXPECT_NE(res->body.find("\"code\":2001"), std::string::npos);
}

// 握手带上 sid 就建起会话：先收到一条 core.state，
// 这个 sid 立刻对 HTTP 生效
TEST_F(ServerSmokeTest, WebSocketCreatesSession) {
  const std::string url =
      "ws://127.0.0.1:" + std::to_string(running_.port) + "/ws?sid=smoke-1";
  httplib::ws::WebSocketClient ws(url);
  ASSERT_TRUE(ws.connect()) << "ws handshake failed";

  ws.set_read_timeout(3, 0);
  std::string msg;
  EXPECT_EQ(ws.read(msg), httplib::ws::ReadResult::Text);
  EXPECT_NE(msg.find("core.state"), std::string::npos);

  httplib::Client cli("127.0.0.1", running_.port);
  cli.set_default_headers({{"X-Session-Id", "smoke-1"}});
  auto res = cli.Get("/api/v1/streams");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  ws.close();
}

// 单会话且没有顶替：第一条还在时，第二条换个 sid 也会被挡在握手阶段
TEST_F(ServerSmokeTest, SecondWebSocketIsRejected) {
  httplib::ws::WebSocketClient first(WsUrl("smoke-1"));
  ASSERT_TRUE(first.connect());
  first.set_read_timeout(3, 0);
  std::string msg;
  ASSERT_EQ(first.read(msg), httplib::ws::ReadResult::Text);

  httplib::ws::WebSocketClient second(WsUrl("smoke-2"));
  EXPECT_FALSE(second.connect());

  // 关掉之后会话销毁，新连接又能进来
  first.close();
  for (int i = 0; i < 50; ++i) {
    httplib::ws::WebSocketClient again(WsUrl("smoke-3"));
    if (again.connect()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
  FAIL() << "reconnect still refused 2s after close";
}

// 流编号的字段名统一叫 streamId（规范 §6.4）：用旧的 id 订阅就是未知流
TEST_F(ServerSmokeTest, StreamsUseStreamIdField) {
  httplib::ws::WebSocketClient ws(WsUrl("smoke-1"));
  ASSERT_TRUE(ws.connect());

  httplib::Client cli("127.0.0.1", running_.port);
  cli.set_default_headers({{"X-Session-Id", "smoke-1"}});

  auto list = cli.Get("/api/v1/streams");
  ASSERT_TRUE(list);
  EXPECT_NE(list->body.find("\"streamId\":1"), std::string::npos);
  EXPECT_EQ(list->body.find("\"id\":1"), std::string::npos);

  auto ok = cli.Post("/api/v1/subscriptions", R"({"streams":[{"streamId":2}]})",
                     "application/json");
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->status, 200);

  auto old_field = cli.Post("/api/v1/subscriptions",
                            R"({"streams":[{"id":2}]})", "application/json");
  ASSERT_TRUE(old_field);
  EXPECT_EQ(old_field->status, 404) << "旧的 id 字段不该再被认";

  ws.close();
}

// 未知流、state 流的 credit 一律静默忽略（规范 §8.2）。回 core.error
// 的话，客户端对订阅响应里所有流统一归还时会被错误码刷屏
TEST_F(ServerSmokeTest, CreditForUnknownOrStateStreamIsSilent) {
  httplib::ws::WebSocketClient ws(WsUrl("smoke-1"));
  ASSERT_TRUE(ws.connect());

  httplib::Client cli("127.0.0.1", running_.port);
  cli.set_default_headers({{"X-Session-Id", "smoke-1"}});
  auto sub = cli.Post("/api/v1/subscriptions",
                      R"({"streams":[{"streamId":1}]})", "application/json");
  ASSERT_TRUE(sub);
  ASSERT_EQ(sub->status, 200);

  ASSERT_TRUE(ws.send(R"({"v":1,"type":"credit","streams":[)"
                      R"({"streamId":1,"bytes":1024},)"
                      R"({"streamId":999,"bytes":1024}]})"));

  ws.set_read_timeout(1, 0);
  std::string msg;
  for (int i = 0; i < 5; ++i) {
    const auto result = ws.read(msg);
    if (result == httplib::ws::ReadResult::Timeout) {
      return;  // 安静，符合预期
    }
    ASSERT_NE(result, httplib::ws::ReadResult::Fail);
    EXPECT_EQ(msg.find("core.error"), std::string::npos) << msg;
  }
  FAIL() << "unexpected event stream";
}

// Stop 交给工作线程，响应晚一个 tick 回来；停完再停要被挡住
TEST_F(ServerSmokeTest, StopRunsOffTheEventLoop) {
  httplib::ws::WebSocketClient ws(WsUrl("smoke-1"));
  ASSERT_TRUE(ws.connect());

  httplib::Client cli("127.0.0.1", running_.port);
  cli.set_default_headers({{"X-Session-Id", "smoke-1"}});

  auto start = cli.Post("/api/v1/modules/fake/start", "", "application/json");
  ASSERT_TRUE(start);
  EXPECT_EQ(start->status, 200);

  auto stop = cli.Post("/api/v1/modules/fake/stop", "", "application/json");
  ASSERT_TRUE(stop);
  EXPECT_EQ(stop->status, 200);
  EXPECT_NE(stop->body.find("\"ok\":true"), std::string::npos);

  auto again = cli.Post("/api/v1/modules/fake/stop", "", "application/json");
  ASSERT_TRUE(again);
  EXPECT_EQ(again->status, 409);
  EXPECT_NE(again->body.find("\"code\":2006"), std::string::npos);

  ws.close();
}
