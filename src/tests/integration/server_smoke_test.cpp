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

bool StartOnFreePort(RunningServer* out) {
  for (int port = kFirstPort; port < kFirstPort + kPortTries; ++port) {
    auto server = std::make_unique<v1::V1Server>(port);
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

}  // namespace

class ServerSmokeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(StartOnFreePort(&running_)) << "no free port in range";
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

  RunningServer running_;
};

// 能力查询不需要会话，客户端靠它先确认服务端支持什么
TEST_F(ServerSmokeTest, CapabilitiesNeedNoSession) {
  httplib::Client cli("127.0.0.1", running_.port);
  auto res = cli.Get("/api/v1/capabilities");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_NE(res->body.find("\"proto\":1"), std::string::npos);
  EXPECT_NE(res->body.find("\"credit\""), std::string::npos);
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
