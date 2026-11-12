#pragma once

// 测量结果存储：一行一次测量的 JSONL 追加文件 + CSV 导出。
//
// 参考实现用 SQLite；v1 底座不引第三方依赖，这里用追加式 JSONL，
// 全量载入内存后按条件过滤。单次测量一条记录、明细内嵌，量级足够。
// 换 SQLite 只需替换本类实现。

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace eph205 {

class MeasurementStore {
 public:
  MeasurementStore() = default;
  MeasurementStore(const MeasurementStore&) = delete;
  MeasurementStore& operator=(const MeasurementStore&) = delete;

  // 打开（或新建）存储文件并载入已有记录。
  bool Open(const std::string& path, std::string& err);
  const std::string& path() const { return path_; }

  // 写入一条完整记录（含 items），返回分配到的 id；<0 表示失败。
  std::int64_t Append(const nlohmann::json& record, std::string& err);

  bool Get(std::int64_t id, nlohmann::json& out) const;
  bool Remove(std::int64_t id, std::string& err);

  struct Filter {
    std::string model;      // 模糊匹配，空 = 全部
    std::string operator_name;
    std::string status;
    std::int64_t from_ms = 0;  // 0 = 不限
    std::int64_t to_ms = 0;
  };

  // 返回列表行（不含 items），total 为符合条件总数。
  std::vector<nlohmann::json> List(const Filter& filter, int offset, int limit,
                                   int* total) const;

  // 导出为 CSV 文本；失败返回空串并写 err。
  std::string ExportCsv(std::int64_t id, std::string& err) const;

  // 多条记录合成一张表，前面多出记录 ID/时间/型号等列。
  std::string ExportCsvMany(const std::vector<std::int64_t>& ids,
                            std::string& err) const;

  int Count() const;

 private:
  void Load();
  void Rewrite() const;

  mutable std::mutex mu_;
  std::string path_;
  std::int64_t next_id_ = 1;
  std::vector<nlohmann::json> records_;
};

}  // namespace eph205
