#pragma once

// EPH205 业务模块：实现 v1::Module，把设备能力暴露成
// 命令 / 参数 / 作业 / 流。
//
// 线程模型：
//   - 通讯层（HTTP/WS）只在主线程调用本类；
//   - 串口事务全部在 worker_ 线程执行，主线程通过 RunSync 等待结果；
//   - 长时间测量由 measure_run 命令启动，进度经 PollEvent 推出；
//   - 仿真器有自己的线程（SimulatorLink），主线程直接读写它的配置。

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "base/module.h"
#include "eph205_client.h"
#include "measurement_store.h"
#include "simulator.h"

namespace eph205 {

class Eph205Module : public v1::Module {
 public:
  Eph205Module();
  ~Eph205Module() override;

  std::string ns() const override { return "eph205"; }
  const std::vector<v1::StreamInfo>& streams() const override {
    return streams_;
  }
  bool running() const override;

  bool Start(std::string& err) override;
  bool Stop(std::string& err) override;
  nlohmann::json State() const override;
  nlohmann::json Describe() const override;

  bool Command(const std::string& action, const nlohmann::json& payload,
               nlohmann::json& result, std::string& err) override;
  bool AdminOnly(const std::string& action) const override;

  nlohmann::json GetParams(const std::string& scope) const override;
  bool SetParams(const std::string& scope, const nlohmann::json& values,
                 bool replace, nlohmann::json& applied,
                 std::string& err) override;
  bool OwnsParamScope(const std::string& scope) const override {
    return scope == "eph205";
  }

  // 通讯层注册时把推送口交进来；拿到之后读数直接推到缓冲，不再自己排队
  void AttachSink(v1::StreamSink* sink) override;

  // 测量进度先排进 events_，通讯层每 tick 来取一次并转发
  bool PollEvent(uint64_t now_ms, std::string& event_name,
                 nlohmann::json& payload) override;

  nlohmann::json Logs() const override;

 private:
  struct Task {
    std::function<void()> fn;
    bool done = false;
    std::mutex mu;
    std::condition_variable cv;
  };

  struct Job {
    std::string id;
    std::string type = "measure_run";
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    std::atomic<int> done{0};
    std::atomic<int> total{0};
    std::atomic<int> passed{0};
    std::atomic<int> failed{0};
    std::atomic<int> measure_failed{0};
    std::string state = "running";
    nlohmann::json summary = nlohmann::json::object();
    std::string error;
    std::int64_t record_id = 0;
  };

  // worker 线程
  void WorkerLoop();
  void Post(std::function<void()> fn);

  // 在 worker 线程执行 fn 并等待结果；超时后放弃执行，返回 false。
  using SyncFn = std::function<bool(nlohmann::json& out, std::string& err)>;
  bool RunSync(const SyncFn& fn, nlohmann::json& result, std::string& err,
               int timeout_ms);

  // 在 worker 线程里跑
  void DoMeasureRun(const std::shared_ptr<Job>& job,
                    const nlohmann::json& input);

  // 长任务的入口与取消：都走业务命令，不再有框架侧的作业抽象
  bool StartMeasureJob(const nlohmann::json& input, nlohmann::json& result,
                       std::string& err);
  bool CancelMeasureJob(const nlohmann::json& input, nlohmann::json& result,
                        std::string& err);

  // 作业事件与实时读数
  void PushEvent(const std::string& name, nlohmann::json payload);
  void PushReading(const nlohmann::json& reading);
  void FinishJob(const std::shared_ptr<Job>& job);

  void Log(const std::string& line);

  // 快照（主线程读）
  bool IsConnected() const;

  // 参数
  std::string ParamString(const std::string& key, const std::string& def) const;
  int ParamInt(const std::string& key, int def) const;

  // 命令分派
  bool HandleCommand(const std::string& action, const nlohmann::json& payload,
                     nlohmann::json& result, std::string& err);

  mutable std::mutex mu_;
  bool connected_ = false;
  nlohmann::json params_;
  std::string connected_port_;
  nlohmann::json last_info_;
  nlohmann::json last_params_;
  std::deque<std::string> logs_;
  std::deque<std::pair<std::string, nlohmann::json>> events_;
  // 读数的缓冲在通讯层，这里只留一个推送口
  v1::StreamSink* sink_ = nullptr;
  std::shared_ptr<Job> job_;

  std::vector<v1::StreamInfo> streams_;

  // worker
  std::thread worker_;
  std::mutex task_mu_;
  std::condition_variable task_cv_;
  std::deque<std::shared_ptr<Task>> tasks_;
  bool quit_ = false;

  // 只归 worker 线程所有
  Eph205Client client_;

  // 仿真器（自己管线程）
  SimulatorLink sim_;

  MeasurementStore store_;
  int job_counter_ = 0;
};

}  // namespace eph205
