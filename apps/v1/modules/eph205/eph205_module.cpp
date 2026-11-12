#include "eph205_module.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <set>

namespace eph205 {

namespace {

constexpr std::uint32_t kReadingStreamId = 1;
constexpr int kMaxReadingsPerFrame = 64;
constexpr int kMaxLogs = 300;
constexpr int kDeviceCommandTimeoutMs = 4000;
constexpr int kConnectTimeoutMs = 8000;
constexpr int kStopTimeoutMs = 1500;

std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string FirmwareText(std::uint16_t version) {
  const int major = (version >> 8) & 0xFF;
  const int minor = version & 0xFF;
  return std::to_string(major) + "." + std::to_string(minor);
}

nlohmann::json InfoToJson(const DeviceInfo& info) {
  return {{"firmwareVersion", static_cast<int>(info.firmware_version)},
          {"firmware", FirmwareText(info.firmware_version)},
          {"pCount", info.p_count},
          {"nCount", info.n_count},
          {"muxBoards", info.mux_boards},
          {"rangeCount", info.range_count},
          {"mode", static_cast<int>(info.mode)},
          {"source", static_cast<int>(info.source)},
          {"deviceId", info.device_id}};
}

nlohmann::json ParamsToJson(const DeviceParams& params) {
  return {{"wire", static_cast<int>(params.wire)},
          {"range", static_cast<int>(params.range)},
          {"averageTimes", params.average_times},
          {"settleDelay", params.settle_delay},
          {"timeout", params.timeout},
          {"outputEnabled", params.output_enabled},
          {"parallelLoad", params.parallel_load}};
}

// 显式判断类型：nlohmann 的 value() 遇到 null 会抛 type_error，
// 而测试项里的下限/上限经常是空值。
double NumValue(const nlohmann::json& obj, const char* key, double def) {
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_number()) {
    return def;
  }
  return it->get<double>();
}

bool HasNumValue(const nlohmann::json& obj, const char* key) {
  const auto it = obj.find(key);
  return it != obj.end() && it->is_number();
}

bool BoolValue(const nlohmann::json& obj, const char* key, bool def) {
  const auto it = obj.find(key);
  if (it == obj.end() || it->is_null()) {
    return def;
  }
  if (it->is_boolean()) {
    return it->get<bool>();
  }
  if (it->is_number()) {
    return it->get<double>() != 0.0;
  }
  if (it->is_string()) {
    const std::string text = it->get<std::string>();
    return !(text == "0" || text == "否" || text == "n" || text == "N" ||
             text == "false");
  }
  return def;
}

const char* IoText(IoError io) {
  switch (io) {
    case IoError::kNone:
      return "";
    case IoError::kTimeout:
      return "超时";
    case IoError::kSumError:
      return "校验和错误";
    case IoError::kFrameError:
      return "帧错误";
    case IoError::kSeqMismatch:
      return "序号不符";
    case IoError::kDisconnected:
      return "串口未连接";
    case IoError::kBadResponse:
      return "应答异常";
    case IoError::kBusy:
      return "设备忙";
  }
  return "链路错误";
}

std::string TimeText(std::int64_t ms) {
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm_buf = {};
  localtime_s(&tm_buf, &t);
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);
  return buffer;
}

}  // namespace

Eph205Module::Eph205Module() {
  params_ = {{"port", "COM7"},
             {"baud", 115200},
             {"simPort", "COM8"},
             {"autoStartSim", false},
             {"dataDir", "data/eph205"}};

  streams_.push_back(
      {kReadingStreamId, "eph205.readings", "record",
       nlohmann::json{{"format", "jsonl"},
                      {"fields",
                       {"seq", "p", "n", "ohms", "status", "result"}},
                      {"rate", "on_progress"}}
           .dump()});

  const std::string dir = ParamString("dataDir", "data/eph205");
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::string store_err;
  if (!store_.Open(dir + "/measurements.jsonl", store_err)) {
    Log("存储打开失败: " + store_err);
  }

  worker_ = std::thread(&Eph205Module::WorkerLoop, this);
  Log(std::string("模块就绪，数据目录 ") + dir);
}

Eph205Module::~Eph205Module() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (job_) {
      job_->cancel.store(true);
    }
  }
  {
    std::lock_guard<std::mutex> lock(task_mu_);
    quit_ = true;
  }
  task_cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  sim_.Stop();
}

std::string Eph205Module::ParamString(const std::string& key,
                                      const std::string& def) const {
  std::lock_guard<std::mutex> lock(mu_);
  return params_.value(key, def);
}

int Eph205Module::ParamInt(const std::string& key, int def) const {
  std::lock_guard<std::mutex> lock(mu_);
  return params_.value(key, def);
}

bool Eph205Module::IsConnected() const {
  std::lock_guard<std::mutex> lock(mu_);
  return connected_;
}

bool Eph205Module::running() const { return IsConnected(); }

void Eph205Module::Log(const std::string& line) {
  std::lock_guard<std::mutex> lock(mu_);
  logs_.push_back(TimeText(NowMs()) + "  " + line);
  while (static_cast<int>(logs_.size()) > kMaxLogs) {
    logs_.pop_front();
  }
}

// ------------------------------------------------------------- worker 线程

void Eph205Module::WorkerLoop() {
  for (;;) {
    std::shared_ptr<Task> task;
    {
      std::unique_lock<std::mutex> lock(task_mu_);
      task_cv_.wait(lock, [this] { return quit_ || !tasks_.empty(); });
      if (quit_ && tasks_.empty()) {
        return;
      }
      task = tasks_.front();
      tasks_.pop_front();
    }
    if (task->fn) {
      task->fn();
    }
  }
}

void Eph205Module::Post(std::function<void()> fn) {
  auto task = std::make_shared<Task>();
  task->fn = std::move(fn);
  {
    std::lock_guard<std::mutex> lock(task_mu_);
    if (quit_) {
      return;
    }
    tasks_.push_back(task);
  }
  task_cv_.notify_one();
}

bool Eph205Module::RunSync(const SyncFn& fn, nlohmann::json& result,
                           std::string& err, int timeout_ms) {
  struct State {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
    nlohmann::json result;
    std::string err;
  };
  auto state = std::make_shared<State>();
  auto abandoned = std::make_shared<std::atomic<bool>>(false);
  auto task = std::make_shared<Task>();
  task->fn = [fn, state, abandoned] {
    if (abandoned->load()) {
      return;
    }
    std::string local_err;
    nlohmann::json local_result;
    const bool ok = fn(local_result, local_err);
    {
      std::lock_guard<std::mutex> lock(state->mu);
      state->ok = ok;
      state->result = std::move(local_result);
      state->err = local_err;
      state->done = true;
    }
    state->cv.notify_all();
  };

  {
    std::lock_guard<std::mutex> lock(task_mu_);
    if (quit_) {
      err = "模块已停止";
      return false;
    }
    tasks_.push_back(task);
  }
  task_cv_.notify_one();

  std::unique_lock<std::mutex> lock(state->mu);
  if (!state->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [&] { return state->done; })) {
    abandoned->store(true);
    err = "设备忙或响应超时";
    return false;
  }
  if (!state->ok) {
    err = state->err;
    return false;
  }
  result = state->result;
  return true;
}

// ------------------------------------------------------------------ 生命周期

bool Eph205Module::Start(std::string& err) {
  if (IsConnected()) {
    return true;
  }
  const std::string port = ParamString("port", "COM7");
  const int baud = ParamInt("baud", 115200);
  nlohmann::json result;
  return RunSync(
      [this, port, baud](nlohmann::json& out, std::string& e) {
        DeviceError de;
        if (!client_.Open(port, baud, &de)) {
          e = de.message.empty() ? std::string(IoText(de.io)) : de.message;
          return false;
        }
        DeviceInfo info;
        if (!client_.QueryDeviceInfo(&info, &de)) {
          client_.Close();
          e = de.message.empty() ? std::string(IoText(de.io)) : de.message;
          return false;
        }
        {
          std::lock_guard<std::mutex> lock(mu_);
          connected_ = true;
          connected_port_ = port;
          last_info_ = InfoToJson(info);
        }
        out = InfoToJson(info);
        return true;
      },
      result, err, kConnectTimeoutMs);
}

bool Eph205Module::Stop(std::string& err) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (job_) {
      job_->cancel.store(true);
    }
  }
  nlohmann::json result;
  RunSync(
      [this](nlohmann::json& out, std::string&) {
        client_.Close();
        {
          std::lock_guard<std::mutex> lock(mu_);
          connected_ = false;
          connected_port_.clear();
        }
        out = {{"ok", true}};
        return true;
      },
      result, err, kStopTimeoutMs);
  {
    std::lock_guard<std::mutex> lock(mu_);
    connected_ = false;
    connected_port_.clear();
  }
  return true;
}

nlohmann::json Eph205Module::State() const {
  std::lock_guard<std::mutex> lock(mu_);
  nlohmann::json job = nlohmann::json::object();
  if (job_) {
    job = {{"jobId", job_->id},
           {"state", job_->state},
           {"done", job_->done.load()},
           {"total", job_->total.load()},
           {"passed", job_->passed.load()},
           {"failed", job_->failed.load()},
           {"measureFailed", job_->measure_failed.load()}};
  }
  return {{"connected", connected_},
          {"port", connected_port_},
          {"device", last_info_},
          {"params", last_params_},
          {"job", job},
          {"sim", sim_.Describe()},
          {"records", store_.Count()},
          {"storedPort", params_.value("port", "")},
          {"simPort", params_.value("simPort", "")}};
}

nlohmann::json Eph205Module::Describe() const {
  return {{"ns", "eph205"},
          {"name", "EPH205 电阻测量"},
          {"version", "0.1.0"},
          {"paramsScope", "eph205"},
          {"streams", streams_.size()},
          {"commands",
           {"ports", "connect", "disconnect", "device_info", "set_mode",
            "set_param", "get_params", "measure", "reset", "frame_log",
            "sim_start", "sim_stop", "sim_set", "sim_config", "sim_clear",
            "history_list", "history_get", "history_delete",
            "history_export"}},
          {"jobs", {"measure_run"}}};
}

nlohmann::json Eph205Module::GetParams(const std::string& scope) const {
  if (scope != "eph205") {
    return nlohmann::json::object();
  }
  std::lock_guard<std::mutex> lock(mu_);
  return params_;
}

bool Eph205Module::SetParams(const std::string& scope,
                             const nlohmann::json& values, bool replace,
                             nlohmann::json& applied, std::string& err) {
  if (scope != "eph205") {
    err = "未知参数作用域";
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (connected_ && values.contains("port") &&
      values.value("port", "") != params_.value("port", "")) {
    err = "已连接，请先断开再改端口";
    return false;
  }
  if (replace) {
    params_ = values;
  } else {
    params_.update(values);
  }
  applied = params_;
  return true;
}

// ---------------------------------------------------------------- 命令分派

bool Eph205Module::Command(const std::string& action,
                           const nlohmann::json& payload,
                           nlohmann::json& result, std::string& err) {
  bool job_running = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    job_running = job_ && !job_->finished.load();
  }
  static const std::set<std::string> kDeviceCommands = {
      "connect",       "disconnect", "device_info", "set_mode",
      "set_param",     "get_params", "measure",     "reset",
      "frame_log",     "sim_start",  "sim_stop",    "sim_set",
      "sim_config",    "sim_clear"};
  if (job_running && kDeviceCommands.count(action) > 0) {
    err = "测量进行中，请先停止";
    return false;
  }
  return HandleCommand(action, payload, result, err);
}

bool Eph205Module::HandleCommand(const std::string& action,
                                 const nlohmann::json& payload,
                                 nlohmann::json& result, std::string& err) {
  if (action == "ports") {
    result = {{"ports", SerialPort::ListPorts()},
              {"storedPort", ParamString("port", "")},
              {"simPort", ParamString("simPort", "")}};
    return true;
  }

  if (action == "connect") {
    const std::string port = payload.value("port", ParamString("port", "COM7"));
    const int baud = payload.value("baud", ParamInt("baud", 115200));
    if (IsConnected()) {
      std::lock_guard<std::mutex> lock(mu_);
      result = {{"alreadyConnected", true},
                {"port", connected_port_},
                {"device", last_info_}};
      return true;
    }
    const bool ok = RunSync(
        [this, port, baud](nlohmann::json& out, std::string& e) {
          DeviceError de;
          if (!client_.Open(port, baud, &de)) {
            e = de.message.empty() ? std::string(IoText(de.io)) : de.message;
            return false;
          }
          DeviceInfo info;
          if (!client_.QueryDeviceInfo(&info, &de)) {
            client_.Close();
            e = de.message.empty() ? std::string(IoText(de.io)) : de.message;
            return false;
          }
          {
            std::lock_guard<std::mutex> lock(mu_);
            connected_ = true;
            connected_port_ = port;
            last_info_ = InfoToJson(info);
          }
          out = {{"port", port}, {"device", InfoToJson(info)}};
          return true;
        },
        result, err, kConnectTimeoutMs);
    if (ok) {
      Log("已连接 " + port);
    }
    return ok;
  }

  if (action == "disconnect") {
    return RunSync(
        [this](nlohmann::json& out, std::string&) {
          client_.Close();
          {
            std::lock_guard<std::mutex> lock(mu_);
            connected_ = false;
            connected_port_.clear();
          }
          out = {{"ok", true}};
          return true;
        },
        result, err, kStopTimeoutMs);
  }

  if (action == "device_info") {
    return RunSync(
        [this](nlohmann::json& out, std::string& e) {
          DeviceError de;
          DeviceInfo info;
          if (!client_.QueryDeviceInfo(&info, &de)) {
            e = de.message.empty() ? std::string(IoText(de.io)) : de.message;
            return false;
          }
          {
            std::lock_guard<std::mutex> lock(mu_);
            last_info_ = InfoToJson(info);
          }
          out = InfoToJson(info);
          return true;
        },
        result, err, kDeviceCommandTimeoutMs);
  }

  if (action == "set_mode") {
    const int mode = payload.value("mode", 0);
    const int source = payload.value("source", 2);
    return RunSync(
        [this, mode, source](nlohmann::json& out, std::string& e) {
          DeviceError de;
          if (!client_.SetMode(static_cast<Mode>(mode),
                               static_cast<Source>(source), &de)) {
            e = de.message.empty() ? std::string(StatusText(de.status))
                                   : de.message;
            return false;
          }
          out = {{"ok", true}, {"mode", mode}, {"source", source}};
          return true;
        },
        result, err, kDeviceCommandTimeoutMs);
  }

  if (action == "set_param") {
    const int type = payload.value("type", 0);
    const int value = payload.value("value", 0);
    if (type < 1 || type > 5) {
      err = "参数类型必须是 1..5";
      return false;
    }
    return RunSync(
        [this, type, value](nlohmann::json& out, std::string& e) {
          DeviceError de;
          if (!client_.SetParam(static_cast<ParamType>(type), value, &de)) {
            e = de.message.empty() ? std::string(StatusText(de.status))
                                   : de.message;
            return false;
          }
          out = {{"ok", true}, {"type", type}, {"value", value}};
          return true;
        },
        result, err, kDeviceCommandTimeoutMs);
  }

  if (action == "get_params") {
    return RunSync(
        [this](nlohmann::json& out, std::string& e) {
          DeviceError de;
          DeviceParams params;
          if (!client_.GetParams(&params, &de)) {
            e = de.message.empty() ? std::string(IoText(de.io)) : de.message;
            return false;
          }
          Mode mode = Mode::kCurrent;
          Source source = Source::kHowland;
          client_.GetMode(&mode, &source, &de);
          {
            std::lock_guard<std::mutex> lock(mu_);
            last_params_ = ParamsToJson(params);
            last_params_["mode"] = static_cast<int>(mode);
            last_params_["source"] = static_cast<int>(source);
          }
          out = ParamsToJson(params);
          out["mode"] = static_cast<int>(mode);
          out["source"] = static_cast<int>(source);
          return true;
        },
        result, err, kDeviceCommandTimeoutMs);
  }

  if (action == "measure") {
    const int p = payload.value("p", 0);
    const int n = payload.value("n", 0);
    return RunSync(
        [this, p, n](nlohmann::json& out, std::string& e) {
          DeviceError de;
          Reading reading;
          if (!client_.Measure(p, n, &reading, &de)) {
            e = de.message.empty() ? std::string(IoText(de.io)) : de.message;
            return false;
          }
          out = {{"p", p},
                 {"n", n},
                 {"status", static_cast<int>(reading.status)},
                 {"statusText", StatusText(reading.status)},
                 {"ohms",
                  std::isnan(reading.ohms) ? nlohmann::json()
                                           : nlohmann::json(reading.ohms)},
                 {"rawAdc", reading.raw_adc},
                 {"differentialUv", reading.differential_uv},
                 {"at", NowMs()}};
          return true;
        },
        result, err, kDeviceCommandTimeoutMs);
  }

  if (action == "reset") {
    return RunSync(
        [this](nlohmann::json& out, std::string& e) {
          DeviceError de;
          if (!client_.Reset(&de)) {
            e = de.message.empty() ? std::string(StatusText(de.status))
                                   : de.message;
            return false;
          }
          out = {{"ok", true}};
          return true;
        },
        result, err, kDeviceCommandTimeoutMs);
  }

  if (action == "frame_log") {
    nlohmann::json client_log;
    // 只通过 out 回传，避免 lambda 捕获局部引用后超时造成的悬垂访问
    const bool ok = RunSync(
        [this](nlohmann::json& out, std::string&) {
          nlohmann::json lines = nlohmann::json::array();
          for (const auto& line : client_.TakeFrameLog()) {
            lines.push_back(line);
          }
          out = lines;
          return true;
        },
        client_log, err, kDeviceCommandTimeoutMs);
    if (!ok) {
      client_log = nlohmann::json::array();
    }
    result = {{"client", client_log}, {"sim", sim_.TakeFrameLog()}};
    return true;
  }

  if (action == "sim_start") {
    if (sim_.running()) {
      result = {{"alreadyRunning", true}, {"port", sim_.port_name()}};
      return true;
    }
    const std::string port =
        payload.value("port", ParamString("simPort", "COM8"));
    const int baud = payload.value("baud", ParamInt("baud", 115200));
    std::string e;
    if (!sim_.Start(port, baud, e)) {
      err = e;
      return false;
    }
    Log("仿真器已启动 " + port);
    result = {{"running", true}, {"port", port}};
    return true;
  }

  if (action == "sim_stop") {
    sim_.Stop();
    Log("仿真器已停止");
    result = {{"running", false}};
    return true;
  }

  if (action == "sim_config") {
    if (payload.contains("defaultOhms")) {
      sim_.SetDefaultResistance(payload.value("defaultOhms", 100.0f));
    }
    result = sim_.Describe();
    return true;
  }

  if (action == "sim_set") {
    const int p = payload.value("p", 0);
    const int n = payload.value("n", 0);
    if (payload.value("over", false)) {
      sim_.SetFailure(p, n, Status::kOverRange);
    } else {
      sim_.SetResistance(p, n, payload.value("ohms", 100.0f));
    }
    result = sim_.Describe();
    return true;
  }

  if (action == "sim_clear") {
    sim_.ClearOverrides();
    result = sim_.Describe();
    return true;
  }

  if (action == "history_list") {
    MeasurementStore::Filter filter;
    filter.model = payload.value("model", "");
    filter.operator_name = payload.value("operator", "");
    filter.status = payload.value("status", "");
    filter.from_ms = payload.value("from", 0LL);
    filter.to_ms = payload.value("to", 0LL);
    const int offset = payload.value("offset", 0);
    const int limit = payload.value("limit", 50);
    int total = 0;
    result = {{"rows", store_.List(filter, offset, limit, &total)},
              {"total", total},
              {"offset", offset},
              {"limit", limit}};
    return true;
  }

  if (action == "history_get") {
    const std::int64_t id = payload.value("id", 0LL);
    nlohmann::json record;
    if (!store_.Get(id, record)) {
      err = "记录不存在";
      return false;
    }
    result = record;
    return true;
  }

  if (action == "history_delete") {
    const std::int64_t id = payload.value("id", 0LL);
    std::string e;
    if (!store_.Remove(id, e)) {
      err = e;
      return false;
    }
    result = {{"ok", true}, {"id", id}};
    return true;
  }

  if (action == "history_export") {
    const std::int64_t id = payload.value("id", 0LL);
    std::string e;
    const std::string csv = store_.ExportCsv(id, e);
    if (csv.empty()) {
      err = e.empty() ? "导出失败" : e;
      return false;
    }
    result = {{"id", id}, {"csv", csv}, {"bytes", csv.size()}};
    return true;
  }

  err = "未知命令: " + action;
  return false;
}

// -------------------------------------------------------------------- 作业

bool Eph205Module::StartJob(const std::string& type,
                            const nlohmann::json& input, std::string& job_id,
                            std::string& err) {
  if (type != "measure_run") {
    err = "未知作业类型: " + type;
    return false;
  }
  if (!input.contains("items") || !input["items"].is_array() ||
      input["items"].empty()) {
    err = "items 不能为空";
    return false;
  }
  if (!IsConnected()) {
    err = "设备未连接";
    return false;
  }

  auto job = std::make_shared<Job>();
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (job_ && !job_->finished.load()) {
      err = "已有测量任务在运行";
      return false;
    }
    job->id = "job-" + std::to_string(++job_counter_);
    job->total = static_cast<int>(input["items"].size());
    job_ = job;
  }
  job_id = job->id;
  Log("开始测量 " + job->id + "，共 " + std::to_string(job->total.load()) +
      " 项");

  auto items = input;
  Post([this, job, items] {
    // 作业里任何未捕获异常都会带走工作线程，这里统一兜住并结束作业
    try {
      DoMeasureRun(job, items);
    } catch (const std::exception& e) {
      Log(std::string("测量异常: ") + e.what());
      {
        std::lock_guard<std::mutex> lock(mu_);
        job->summary = {{"status", "aborted"}, {"error", e.what()}};
        job->state = "aborted";
      }
      FinishJob(job);
    }
  });
  return true;
}

void Eph205Module::PushEvent(const std::string& name, nlohmann::json payload) {
  std::lock_guard<std::mutex> lock(mu_);
  events_.emplace_back(name, std::move(payload));
}

void Eph205Module::PushReading(const nlohmann::json& reading) {
  std::lock_guard<std::mutex> lock(mu_);
  readings_.push_back(reading);
  while (readings_.size() > 4096) {
    readings_.pop_front();
  }
}

void Eph205Module::FinishJob(const std::shared_ptr<Job>& job) {
  nlohmann::json summary;
  {
    std::lock_guard<std::mutex> lock(mu_);
    job->finished.store(true);
    summary = job->summary;
  }
  PushEvent("eph205.job.progress",
            {{"jobId", job->id},
             {"state", summary.value("status", "completed")},
             {"done", job->done.load()},
             {"total", job->total.load()},
             {"passed", job->passed.load()},
             {"failed", job->failed.load()},
             {"measureFailed", job->measure_failed.load()},
             {"recordId", job->record_id},
             {"summary", summary}});
  Log("测量结束 " + job->id + "，通过 " + std::to_string(job->passed.load()) +
      "，失败 " + std::to_string(job->failed.load()));
}

void Eph205Module::DoMeasureRun(const std::shared_ptr<Job>& job,
                                const nlohmann::json& input) {
  const auto items = input.value("items", nlohmann::json::array());
  const int total = static_cast<int>(items.size());
  const std::int64_t started_ms = NowMs();

  const auto product = input.value("product", nlohmann::json::object());
  const std::string operator_name = input.value("operator", "");
  const std::string device_source = input.value("deviceSource", "serial");

  std::vector<nlohmann::json> out_items(static_cast<std::size_t>(total));
  int done = 0;
  int passed = 0;
  int failed = 0;
  int measure_failed = 0;

  PushEvent("eph205.job.progress", {{"jobId", job->id},
                                    {"state", "running"},
                                    {"done", 0},
                                    {"total", total}});

  auto mark = [&](int index, const nlohmann::json& item, const char* result,
                  const char* reason, const nlohmann::json& value) {
    nlohmann::json row;
    row["seq"] = static_cast<int>(NumValue(item, "seq", index + 1));
    row["p"] = static_cast<int>(NumValue(item, "p", 0));
    row["n"] = static_cast<int>(NumValue(item, "n", 0));
    row["wire"] = static_cast<int>(NumValue(item, "wire", 0));
    row["range"] = static_cast<int>(NumValue(item, "range", 0));
    row["minOhms"] = HasNumValue(item, "minOhms") ? item["minOhms"]
                                                  : nlohmann::json();
    row["maxOhms"] = HasNumValue(item, "maxOhms") ? item["maxOhms"]
                                                  : nlohmann::json();
    row["measure"] = BoolValue(item, "measure", true);
    row["value"] = value;
    row["result"] = result;
    row["reason"] = reason;
    out_items[static_cast<std::size_t>(index)] = std::move(row);
  };

  int i = 0;
  int current_wire = -1;
  int current_range = -1;
  bool setup_ok = true;

  while (i < total) {
    if (job->cancel.load()) {
      break;
    }
    const auto& item = items[i];
    if (!BoolValue(item, "measure", true)) {
      mark(i, item, "not_measured", "unchecked", nlohmann::json());
      ++done;
      ++i;
      continue;
    }
    const int wire = static_cast<int>(NumValue(item, "wire", 0));
    const int range = static_cast<int>(NumValue(item, "range", 0));

    if (wire != current_wire) {
      DeviceError de;
      if (!client_.SetParam(ParamType::kWireMode, wire, &de)) {
        setup_ok = false;
      } else {
        current_wire = wire;
        setup_ok = true;
      }
    }
    if (setup_ok && range != current_range) {
      DeviceError de;
      if (!client_.SetParam(ParamType::kRange, range, &de)) {
        setup_ok = false;
      } else {
        current_range = range;
        setup_ok = true;
      }
    }
    if (!setup_ok) {
      mark(i, item, "measure_failed", "setup_failed", nlohmann::json());
      ++done;
      ++measure_failed;
      ++i;
      continue;
    }

    // 同一 (线制, 量程) 的连续项打成一批，单帧最多 62 组
    std::vector<PointPair> pairs;
    std::vector<int> indices;
    int j = i;
    while (j < total && static_cast<int>(pairs.size()) < kMaxBatchGroups) {
      const auto& next = items[j];
      if (!BoolValue(next, "measure", true)) {
        break;
      }
      if (static_cast<int>(NumValue(next, "wire", 0)) != wire ||
          static_cast<int>(NumValue(next, "range", 0)) != range) {
        break;
      }
      pairs.push_back(PointPair{
          static_cast<int>(NumValue(next, "p", 0)),
          static_cast<int>(NumValue(next, "n", 0))});
      indices.push_back(j);
      ++j;
    }
    if (pairs.empty()) {
      ++i;
      continue;
    }

    DeviceError de;
    BatchResult batch;
    const bool ok = client_.MeasureBatch(static_cast<std::uint32_t>(i), pairs,
                                         &batch, &de, &job->cancel);
    if (!ok) {
      const char* reason = "frame_failed";
      if (job->cancel.load()) {
        reason = "cancelled";
      }
      for (std::size_t k = 0; k < indices.size(); ++k) {
        mark(indices[k], items[indices[k]], "measure_failed", reason,
             nlohmann::json());
      }
      done += static_cast<int>(indices.size());
      measure_failed += static_cast<int>(indices.size());
      job->done.store(done);
      job->measure_failed.store(measure_failed);
      if (job->cancel.load()) {
        break;
      }
      i = j;
      continue;
    }

    for (std::size_t k = 0; k < indices.size(); ++k) {
      const auto& item_k = items[indices[k]];
      const float ohms = batch.items[k].ohms;
      if (std::isnan(ohms)) {
        mark(indices[k], item_k, "measure_failed", "over_range",
             nlohmann::json());
        ++measure_failed;
      } else {
        const double value = static_cast<double>(ohms);
        const bool has_min = HasNumValue(item_k, "minOhms");
        const double min_ohms = NumValue(item_k, "minOhms", 0.0);
        const double max_ohms = NumValue(item_k, "maxOhms", 0.0);
        const bool has_max = HasNumValue(item_k, "maxOhms") && max_ohms > 0.0;
        const bool pass = (!has_min || value >= min_ohms) &&
                          (!has_max || value <= max_ohms);
        mark(indices[k], item_k, pass ? "pass" : "fail", "", value);
        if (pass) {
          ++passed;
        } else {
          ++failed;
        }
        PushReading({{"seq", static_cast<int>(NumValue(item_k, "seq", indices[k] + 1))},
                     {"p", static_cast<int>(NumValue(item_k, "p", 0))},
                     {"n", static_cast<int>(NumValue(item_k, "n", 0))},
                     {"ohms", value},
                     {"status", 0},
                     {"result", pass ? "pass" : "fail"}});
      }
    }
    done += static_cast<int>(indices.size());
    job->done.store(done);
    job->passed.store(passed);
    job->failed.store(failed);
    job->measure_failed.store(measure_failed);
    PushEvent("eph205.job.progress",
              {{"jobId", job->id},
               {"state", "running"},
               {"done", done},
               {"total", total},
               {"passed", passed},
               {"failed", failed},
               {"measureFailed", measure_failed}});
    i = j;
  }

  // 取消或中断后，剩余项标记为未测
  const bool cancelled = job->cancel.load();
  for (int k = 0; k < total; ++k) {
    if (!out_items[static_cast<std::size_t>(k)].is_object()) {
      mark(k, items[k], "not_measured", cancelled ? "cancelled" : "aborted",
           nlohmann::json());
    }
  }

  // 未测项也要计入进度，否则界面会停在 total-未测数
  job->done.store(total);
  const std::int64_t finished_ms = NowMs();
  const int not_measured = total - passed - failed - measure_failed;
  const double pass_rate =
      total > 0 ? static_cast<double>(passed) * 100.0 / total : 0.0;

  nlohmann::json summary = {
      {"status", cancelled ? "cancelled" : "completed"},
      {"total", total},
      {"passed", passed},
      {"failed", failed},
      {"measureFailed", measure_failed},
      {"notMeasured", not_measured},
      {"passRate", pass_rate},
      {"startedAt", started_ms},
      {"finishedAt", finished_ms},
      {"durationMs", finished_ms - started_ms}};

  nlohmann::json record = {{"createdAt", finished_ms},
                           {"startedAt", started_ms},
                           {"finishedAt", finished_ms},
                           {"product", product},
                           {"operator", operator_name},
                           {"status", summary["status"]},
                           {"deviceSource", device_source},
                           {"port", ParamString("port", "")},
                           {"firmware",
                            last_info_.value("firmware", std::string())},
                           {"config",
                            {{"items", total},
                             {"settleDelay",
                              static_cast<int>(
                                  NumValue(input, "settleDelay", 0))}}},
                           {"summary", summary},
                           {"items", out_items}};

  std::string store_err;
  const std::int64_t record_id = store_.Append(record, store_err);
  if (record_id < 0) {
    Log("落库失败: " + store_err);
  } else {
    job->record_id = record_id;
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    job->summary = summary;
    job->state = summary["status"];
  }
  FinishJob(job);
}

bool Eph205Module::JobTick(uint64_t /*now_ms*/, std::string& event_name,
                           nlohmann::json& payload) {
  std::lock_guard<std::mutex> lock(mu_);
  if (events_.empty()) {
    return false;
  }
  event_name = events_.front().first;
  payload = std::move(events_.front().second);
  events_.pop_front();
  return true;
}

nlohmann::json Eph205Module::JobStatus(const std::string& id) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!job_ || job_->id != id) {
    return nlohmann::json();
  }
  return {{"jobId", job_->id},
          {"type", job_->type},
          {"state", job_->finished.load() ? job_->state : std::string("running")},
          {"done", job_->done.load()},
          {"total", job_->total.load()},
          {"passed", job_->passed.load()},
          {"failed", job_->failed.load()},
          {"measureFailed", job_->measure_failed.load()},
          {"recordId", job_->record_id},
          {"summary", job_->summary}};
}

bool Eph205Module::CancelJob(const std::string& id, std::string& err,
                             nlohmann::json& payload) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!job_ || job_->id != id) {
    err = "作业不存在";
    return false;
  }
  if (job_->finished.load()) {
    err = "作业已结束";
    return false;
  }
  job_->cancel.store(true);
  payload = {{"jobId", id}, {"cancelling", true}};
  return true;
}

// -------------------------------------------------------------------- 数据流

bool Eph205Module::Produce(uint32_t stream_id, uint64_t /*now_ms*/,
                           std::vector<uint8_t>& data,
                           std::vector<uint8_t>& records,
                           std::string& /*meta*/) {
  if (stream_id != kReadingStreamId) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (readings_.empty()) {
    return false;
  }
  std::string text;
  int count = 0;
  while (!readings_.empty() && count < kMaxReadingsPerFrame) {
    text += readings_.front().dump();
    text.push_back('\n');
    readings_.pop_front();
    ++count;
  }
  records.assign(text.begin(), text.end());
  return true;
}

nlohmann::json Eph205Module::Logs() const {
  std::lock_guard<std::mutex> lock(mu_);
  nlohmann::json out = nlohmann::json::array();
  for (const auto& line : logs_) {
    out.push_back(line);
  }
  return out;
}

}  // namespace eph205
