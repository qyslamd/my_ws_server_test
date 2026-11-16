#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace v1 {

// 通讯底座只认识这个接口；具体设备/协议全部由业务模块实现。
struct StreamInfo {
  uint32_t id = 0;
  std::string name;
  std::string cls;
  std::string meta;
};

class Module {
 public:
  virtual ~Module() = default;

  virtual std::string ns() const = 0;
  virtual const std::vector<StreamInfo>& streams() const = 0;
  virtual bool running() const = 0;

  virtual bool Start(std::string& err) = 0;
  virtual bool Stop(std::string& err) = 0;
  virtual nlohmann::json State() const = 0;
  virtual nlohmann::json Describe() const = 0;

  virtual bool Command(const std::string& action,
                       const nlohmann::json& payload,
                       nlohmann::json& result, std::string& err) = 0;

  // 底座是否要求 admin 角色才能执行该命令。
  // 默认全部放行；业务模块按自己的语义声明，底座只在启用认证时生效。
  virtual bool AdminOnly(const std::string& /*action*/) const { return false; }

  virtual nlohmann::json GetParams(const std::string& scope) const = 0;
  virtual bool SetParams(const std::string& scope,
                         const nlohmann::json& values, bool replace,
                         nlohmann::json& applied, std::string& err) = 0;
  virtual bool OwnsParamScope(const std::string& scope) const = 0;

  // 生产一帧：data/records 二选一，meta 为该流的描述。
  virtual bool Produce(uint32_t stream_id, uint64_t now_ms,
                       std::vector<uint8_t>& data,
                       std::vector<uint8_t>& records,
                       std::string& meta) = 0;

  virtual bool StartJob(const std::string& type,
                        const nlohmann::json& input, std::string& job_id,
                        std::string& err) = 0;
  virtual bool JobTick(uint64_t now_ms, std::string& event_name,
                       nlohmann::json& payload) = 0;
  virtual nlohmann::json JobStatus(const std::string& id) const = 0;
  virtual bool CancelJob(const std::string& id, std::string& err,
                         nlohmann::json& payload) = 0;

  virtual nlohmann::json Logs() const = 0;
};

// 媒体由业务模块注册；底座只负责按 id 取帧并通过 HTTP 提供。
// 这样"视频走 HTTP"是底座能力，BMP/JPEG/RTSP 的具体格式属于业务。
class MediaSource {
 public:
  virtual ~MediaSource() = default;
  virtual std::string id() const = 0;
  virtual std::string content_type() const = 0;
  virtual std::vector<uint8_t> NextFrame(int frame_no) = 0;
};

}  // namespace v1
