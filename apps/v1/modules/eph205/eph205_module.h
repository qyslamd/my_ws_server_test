#pragma once

// EPH205 业务模块：实现 v1::Module，把设备能力暴露成
// 命令 / 参数 / 作业 / 流。
//
// 线程模型：
//   - 底座（HTTP/WS）只在主线程调用本类；
//   - 串口事务全部在 worker_ 线程执行，主线程通过 RunSync 等待结果；
//   - 长时间测量跑成作业，进度通过 JobTick 每 10 ms 推一条事件；
//   - 仿真器有自己的线程（SimulatorLink），主线程直接读写它的配置。

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

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
  const std::vector<v1::StreamInfo>& streams() const override { return streams_; }
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

  bool Produce(uint32_t stream_id, uint64_t now_ms,
               std::vector<uint8_t>& data, std::vector<uint8_t>& records,
               std::string& meta) override;

  bool StartJob(const std::string& type, const nlohmann::json& input,
                std::string& job_id, std::string& err) override;
  bool JobTick(uint64_t now_ms, std::string& event_name,
               nlohmann::json& payload) override;
  nlohmann::json JobStatus(const std::string& id) const override;
  bool CancelJob(const std::string& id, std::string& err,
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
  std::deque<nlohmann::json> readings_;
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
