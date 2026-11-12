#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "base/module.h"

struct mg_connection;
struct mg_http_message;
struct mg_ws_message;
struct mg_mgr;

namespace v1 {

// 业务无关的通讯底座：HTTP/WS、会话、订阅、帧、作业路由、Blob、配置、认证。
// 具体设备/协议通过 Module / MediaSource 注册进来。
class V1Server {
 public:
  explicit V1Server(int port, bool auth_required = false);
  ~V1Server();
  void Run();

  void RegisterModule(Module* module);
  void RegisterMediaSource(MediaSource* source);

  static V1Server* instance_;

 private:
  struct Subscription {
    uint64_t window = 1048576;
    uint64_t outstanding = 0;
    bool meta_sent = false;
  };
  struct Session {
    std::string sid;
    mg_connection* ws = nullptr;
    uint64_t last_ms = 0;
    std::string user;
    std::string role;
    std::string token;
    std::map<uint32_t, Subscription> subs;
  };
  struct Blob {
    std::string content_type;
    std::vector<uint8_t> data;
  };

  static void EventFn(mg_connection* c, int ev, void* ev_data);
  static void TickFn(void* arg);
  static void PingFn(void* arg);

  void OnHttp(mg_connection* c, mg_http_message* hm);
  void OnWsOpen(mg_connection* c);
  void OnWsMsg(mg_connection* c, mg_ws_message* wm);
  void OnClose(mg_connection* c);
  void Tick();
  void Ping();

  Module* FindModule(const std::string& ns) const;
  Module* FindStreamModule(uint32_t stream_id,
                           const StreamInfo** stream) const;
  Module* FindParamModule(const std::string& scope) const;
  Module* FindJobModule(const std::string& job_id) const;

  Session* ActiveSession();
  Session* SessionFromHeader(mg_http_message* hm);
  void SendEvent(Session* s, const std::string& name,
                 const nlohmann::json& payload);
  void SafeStop(const std::string& reason);
  void Log(const std::string& msg);
  std::string AddBlob(const std::string& content_type,
                      const std::vector<uint8_t>& data);
  void LoadConfig();
  void SaveConfig();
  void LoadBlobs();

  int port_;
  bool auth_required_ = false;
  mg_mgr* mgr_ = nullptr;
  uint64_t started_ms_ = 0;

  std::map<std::string, Module*> modules_;
  std::map<std::string, MediaSource*> media_sources_;
  std::map<std::string, Module*> job_owner_;
  std::map<std::string, Session> sessions_;
  std::map<uint32_t, uint32_t> seqs_;
  std::map<std::string, int> revisions_;
  std::map<std::string, Blob> blobs_;
  int blob_counter_ = 0;
  int token_counter_ = 0;
  std::map<std::string, nlohmann::json> config_;
  std::map<std::string, int> config_rev_;
  std::vector<std::string> logs_;
};

}  // namespace v1
