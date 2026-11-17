// cpp-httplib 教学 demo：一个进程里同时演服务端和客户端。
//
// 它只回答一个问题：cpp-httplib 怎么用。服务端和客户端都写在这一个
// 文件里，跑起来自己连自己，不需要先起别的东西。
//
// 演示的内容：
//   1. 服务端：HTTP 路由（GET/POST）、WebSocket 回显、优雅停止
//   2. 客户端：同步 GET/POST、WebSocket 收发二进制
//
// 产品里不用它：通讯层是 mongoose。这个 demo 不参与产品构建。
// 输出一律 ASCII：这个翻译单元里的中文字符串字面量会让 MSVC 报
// C4819（与格式串分析有关），注释里的中文没这个问题。

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "httplib.h"

namespace {

constexpr int kDefaultPort = 8767;

// 回显时在前面加 1 字节类型，便于看出二进制没有被改写
constexpr char kEchoType = 0x01;

std::string HexDump(const std::string& s) {
  std::string out;
  char buf[8];
  for (unsigned char ch : s) {
    std::snprintf(buf, sizeof(buf), "%02x ", ch);
    out += buf;
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

bool WaitListening(int port) {
  for (int i = 0; i < 100; ++i) {
    httplib::Client probe("127.0.0.1", port);
    probe.set_connection_timeout(0, 200 * 1000);
    if (probe.Get("/api/health")) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  const int port = argc > 1 ? std::atoi(argv[1]) : kDefaultPort;

  // ---------------------------------------------------------- 服务端
  httplib::Server server;
  server.Get("/", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("cpp-httplib demo\n", "text/plain");
  });
  server.Get("/api/health",
             [](const httplib::Request&, httplib::Response& res) {
               res.set_content(R"({"ok":true,"server":"cpp-httplib-demo"})",
                               "application/json");
             });
  server.Post("/api/echo",
              [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(req.body, "text/plain");
              });

  // WebSocket：连上先打个招呼，之后收到的每一条加个类型字节回过去
  server.WebSocket("/ws",
                   [](const httplib::Request&, httplib::ws::WebSocket& ws) {
                     // send(std::string) 发文本帧，send(ptr, len)
                     // 才是二进制帧。 文本帧里塞非法 UTF-8
                     // 对端会判协议错误，所以回显两个都用指针版
                     ws.send("welcome");
                     std::string msg;
                     for (;;) {
                       const auto got = ws.read(msg);
                       if (got == httplib::ws::ReadResult::Fail) {
                         break;
                       }
                       if (got == httplib::ws::ReadResult::Timeout) {
                         continue;
                       }
                       std::string out(1, kEchoType);
                       out += msg;
                       if (!ws.send(out.data(), out.size())) {
                         break;
                       }
                     }
                   });

  if (!server.bind_to_port("127.0.0.1", port)) {
    std::cout << "port " << port << " is busy, try another one\n";
    return 1;
  }
  std::thread serving([&server] { server.listen_after_bind(); });

  if (!WaitListening(port)) {
    std::cout << "server did not come up\n";
    server.stop();
    serving.join();
    return 1;
  }
  std::cout << "server ready: http://127.0.0.1:" << port << "/\n\n";

  // ---------------------------------------------------------- 客户端
  httplib::Client cli("127.0.0.1", port);
  cli.set_connection_timeout(2, 0);
  cli.set_read_timeout(2, 0);

  if (auto res = cli.Get("/"); res) {
    std::cout << "[HTTP] GET /           -> " << res->status << " "
              << res->body;
  } else {
    std::cout << "[HTTP] GET / failed: " << httplib::to_string(res.error())
              << "\n";
  }

  if (auto res = cli.Get("/api/health"); res) {
    std::cout << "[HTTP] GET /api/health -> " << res->status << " " << res->body
              << "\n";
  } else {
    std::cout << "[HTTP] GET /api/health failed\n";
  }

  if (auto res = cli.Post("/api/echo", "ping", "text/plain"); res) {
    std::cout << "[HTTP] POST /api/echo  -> " << res->status << " " << res->body
              << "\n";
  } else {
    std::cout << "[HTTP] POST /api/echo failed\n";
  }

  std::cout << "\n";
  const std::string url = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
  httplib::ws::WebSocketClient ws(url);
  ws.set_connection_timeout(2, 0);
  ws.set_read_timeout(2, 0);
  if (!ws.connect()) {
    std::cout << "[WS] connect failed\n";
    server.stop();
    serving.join();
    return 1;
  }

  std::string msg;
  if (ws.read(msg) == httplib::ws::ReadResult::Text) {
    std::cout << "[WS] welcome: " << msg << "\n";
  }
  // 欢迎语之后服务端只发二进制，所以后面一律用指针版发送

  // 一段二进制：0xde 0xad 0xbe 0xef，看它原样回来
  const std::string payload = "\xde\xad\xbe\xef";
  std::cout << "[WS] send " << payload.size() << " bytes: " << HexDump(payload)
            << "\n";
  if (ws.send(payload.data(), payload.size())) {
    msg.clear();
    const auto got = ws.read(msg);
    const bool binary = got == httplib::ws::ReadResult::Binary;
    std::cout << "[WS] recv " << (binary ? "binary" : "text") << " "
              << msg.size() << " bytes: " << HexDump(msg) << "\n";
    // 首字节是类型，剩下的是原样的负载
    if (msg.size() == payload.size() + 1 &&
        msg.compare(1, payload.size(), payload) == 0) {
      std::cout << "[WS] echo matches\n";
    } else {
      std::cout << "[WS] echo MISMATCH\n";
    }
  }
  ws.close();

  // ---------------------------------------------------------- 收尾
  server.stop();
  serving.join();
  std::cout << "\n[done]\n";
  return 0;
}
