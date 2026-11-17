#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "base/module.h"
#include "base/stream_buffers.h"
#include "base/stream_sink.h"

struct mg_connection;
struct mg_http_message;
struct mg_ws_message;
struct mg_mgr;

namespace v1 {

// 业务无关的通讯层：HTTP/WS、会话、订阅、帧、作业路由、Blob、配置、认证。
// 具体设备/协议通过 Module / MediaSource 注册进来。
class V1Server {
 public:
  explicit V1Server(int port, bool auth_required = false);
  ~V1Server();
  // Run() = Start() + 事件循环；桌面客户端里主线程要交给 CEF，
  // 所以把这两步拆开，服务端在自己的线程里跑。
  bool Start();
  void Stop();
  void Run();

  void RegisterModule(Module* module);
  void RegisterMediaSource(MediaSource* source);

  static V1Server* instance_;

 private:
  struct Subscription {
    uint64_t window = 1048576;
    uint64_t outstanding = 0;
    bool meta_sent = false;
    // 额度耗尽开始的时刻（0 表示没耗尽），用于节流日志
    uint64_t stalled_ms = 0;
    // 还没报出去的溢出区间：第一次立刻报，之后 500 毫秒合并一次
    StreamRing::Overrun pending_overrun;
    uint64_t last_overrun_ms = 0;
  };
  struct Session {
    std::string sid;
    mg_connection* ws = nullptr;
    uint64_t last_ms = 0;
    std::string user;
    std::string role;
    std::string token;
    // 心跳判死用的采样：本次静默窗口对应哪一次收到消息，以及窗口内见过的最小积压
    uint64_t backlog_base_ms = 0;
    std::size_t backlog_base = 0;
    std::size_t backlog_min = 0;
    std::map<uint32_t, Subscription> subs;
    // 每个订阅流一个缓冲，跟着会话走。
    // 用 shared_ptr：会话结束时把 active_buffers_ 置空（detached），
    // 已经在飞的 Push 还能安全用完，之后再来的直接拿到空指针
    std::shared_ptr<StreamBuffers> buffers;
  };
  struct Blob {
    std::string content_type;
    std::vector<uint8_t> data;
  };

  // 推给"当前会话"的写入口，业务模块拿到的是它的地址
  class Sink : public StreamSink {
   public:
    explicit Sink(V1Server* server) : server_(server) {}
    bool PushSample(uint32_t stream_id, uint64_t ts_ns, const uint8_t* data,
                    std::size_t len) override;
    bool PushRecords(uint32_t stream_id,
                     const std::vector<Record>& records) override;
    void SetState(uint32_t stream_id, const uint8_t* data,
                  std::size_t len) override;

   private:
    std::shared_ptr<StreamBuffers> Buffers() const;

    V1Server* server_;
  };

  // 按流类型选缓冲容量：sample 4 MiB、record 8 MiB（规范 §8.4）
  static std::size_t BufferBytesFor(const std::string& cls);

  static void EventFn(mg_connection* c, int ev, void* ev_data);
  static void TickFn(void* arg);
  static void PingFn(void* arg);

  void OnHttp(mg_connection* c, mg_http_message* hm);
  void OnWsOpen(mg_connection* c);
  void OnWsMsg(mg_connection* c, mg_ws_message* wm);
  void OnClose(mg_connection* c);
  void Tick();
  void Ping();
  // 每 tick 检查一次：静默超时且发送缓冲不再推进就判死（规范 5.3）
  void CheckHeartbeat(uint64_t now);

  // 一次批量停模块。Stop 跑在独立线程里，事件循环只看进度：
  // 业务卡死不会拖住心跳，到点照样销毁会话（规范 §5.4）
  struct StopBatch {
    std::atomic<int> outstanding{0};
    std::atomic<bool> failed{false};
    std::mutex mutex;
    std::string first_err;
    uint64_t deadline_ms = 0;

    void Finish(bool ok, const std::string& err);
  };
  struct PendingStop {
    mg_connection* conn = nullptr;
    std::string ns;
    std::shared_ptr<StopBatch> batch;
  };

  std::shared_ptr<StopBatch> StartStopBatch(
      const std::vector<std::string>& ns_list);
  void TickStop(uint64_t now);
  void TickPendingStops(uint64_t now);
  void FinishSafeStop();
  void DropPendingStop(mg_connection* c);

  // Tick 的发送侧：sample/record 按配额连发，state 走每 tick 总量闸
  void SendStreamFrames(Session* s, uint64_t now);
  void SendStateFrames(Session* s, uint64_t now);
  void SendStreamFrame(Session* s, uint32_t id, Subscription& sub,
                       const StreamInfo* stream, uint32_t seq, uint64_t ts_ns,
                       const std::vector<uint8_t>& data,
                       const std::vector<uint8_t>& records, bool charge);
  // 把缓冲里的溢出统计变成 core.overrun 事件和 opcode=ERROR 帧
  void ReportOverrun(Session* s, uint32_t id, Subscription& sub, uint64_t now);

  Module* FindModule(const std::string& ns) const;
  Module* FindStreamModule(uint32_t stream_id, const StreamInfo** stream) const;
  Module* FindParamModule(const std::string& scope) const;
  Module* FindJobModule(const std::string& job_id) const;

  Session* ActiveSession();
  Session* SessionFromHeader(mg_http_message* hm);
  void SendEvent(Session* s, const std::string& name,
                 const nlohmann::json& payload);
  // 开始一次异步收尾：断生产、尽力发 ending、停模块，到点销毁。
  // 幂等，重复调用只算第一次
  void SafeStop(const std::string& reason, const std::string& sid = "");
  void Log(const std::string& msg);
  std::string AddBlob(const std::string& content_type,
                      const std::vector<uint8_t>& data);
  void LoadConfig();
  void SaveConfig();
  void LoadBlobs();

  int port_;
  bool auth_required_ = false;
  mg_mgr* mgr_ = nullptr;
  std::atomic<bool> running_{false};
  uint64_t started_ms_ = 0;

  std::map<std::string, Module*> modules_;
  std::map<std::string, MediaSource*> media_sources_;
  std::map<std::string, Module*> job_owner_;
  std::map<std::string, Session> sessions_;
  // 模块线程拿到的就是这个口子背后的对象。会话结束置空 = 立刻断生产，
  // 比"等模块停完再释放"更稳，也不用去推理线程退出的时机（方案 5.1）
  std::shared_ptr<StreamBuffers> active_buffers_;
  std::mutex buffers_mutex_;
  // state 流的轮转游标：下一次从比它大的 id 开始发，避免固定几路饿死
  uint32_t state_rr_ = 0;
  // 收尾状态：进入之后拒绝新会话，直到模块停完或到点
  bool ending_ = false;
  std::string ending_sid_;
  std::string ending_reason_;
  std::shared_ptr<StopBatch> ending_batch_;
  uint64_t ending_deadline_ms_ = 0;
  // 显式 stop 请求：响应要等工作线程回来，期间连接必须还在
  std::vector<PendingStop> pending_stops_;
  // 交给业务模块的推送口；模块只拿到它的基类指针
  Sink sink_{this};
  std::map<std::string, int> revisions_;
  std::map<std::string, Blob> blobs_;
  int blob_counter_ = 0;
  int token_counter_ = 0;
  std::map<std::string, nlohmann::json> config_;
  std::map<std::string, int> config_rev_;
  std::vector<std::string> logs_;
};

}  // namespace v1
