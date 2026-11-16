#pragma once

// 测量结果存储：SQLite（两表：头 + 明细）。
//
// 对外仍然是 JSON 形状的接口——调用方（模块/前端/Qt 门面）看到的记录结构和
// 之前 JSONL 版本完全一致，换存储不影响任何上层代码。
//
// 线程模型：一个连接 + 一把互斥锁。访问频率是"每次测量写一条、翻历史翻几页"，
// 用串行模式最省心；SQLite 本身是 SQLITE_THREADSAFE=1（serialized），
// 锁只是把复合操作（事务 + 多条语句）包成原子。

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct sqlite3;

namespace eph205 {

class MeasurementStore {
 public:
  MeasurementStore() = default;
  ~MeasurementStore();

  MeasurementStore(const MeasurementStore&) = delete;
  MeasurementStore& operator=(const MeasurementStore&) = delete;

  // 打开（或新建）数据库并建表。文件不存在时自动创建。
  bool Open(const std::string& path, std::string& err);
  const std::string& path() const { return path_; }

  // 写入一条完整记录（含 items），返回新 id；失败返回 -1。
  std::int64_t Append(const nlohmann::json& record, std::string& err);

  bool Get(std::int64_t id, nlohmann::json& out) const;
  bool Remove(std::int64_t id, std::string& err);

  struct Filter {
    std::string model;  // 模糊匹配，空 = 全部
    std::string operator_name;
    std::string status;
    std::int64_t from_ms = 0;  // 0 = 不限
    std::int64_t to_ms = 0;
  };

  // 返回列表行（不含 items），total 为符合条件总数；按时间倒序。
  std::vector<nlohmann::json> List(const Filter& filter, int offset, int limit,
                                   int* total) const;

  std::string ExportCsv(std::int64_t id, std::string& err) const;
  std::string ExportCsvMany(const std::vector<std::int64_t>& ids,
                            std::string& err) const;

  int Count() const;

 private:
  bool Exec(const char* sql, std::string& err) const;
  // 调用方必须已持有 mu_
  bool QueryRecord(std::int64_t id, nlohmann::json& out) const;
  // 老版本落的是 measurements.jsonl；库为空时顺手导一次，避免历史"消失"。
  void ImportLegacyJsonl(const std::string& jsonl_path);

  mutable std::mutex mu_;
  sqlite3* db_ = nullptr;
  std::string path_;
};

}  // namespace eph205
