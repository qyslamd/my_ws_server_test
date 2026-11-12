#include "measurement_store.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace eph205 {

namespace {

bool Contains(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  return haystack.find(needle) != std::string::npos;
}

std::string CsvCell(const std::string& text) {
  if (text.find_first_of(",\"\r\n") == std::string::npos) {
    return text;
  }
  std::string out = "\"";
  for (char c : text) {
    if (c == '"') {
      out.push_back('"');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string TimeText(std::int64_t ms) {
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm_buf = {};
  localtime_s(&tm_buf, &t);
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);
  return buffer;
}

std::string NumberText(const nlohmann::json& value) {
  if (value.is_null()) {
    return {};
  }
  if (value.is_number_float()) {
    const double d = value.get<double>();
    std::ostringstream oss;
    oss.precision(6);
    oss << std::fixed << d;
    std::string text = oss.str();
    while (text.size() > 1 && text.back() == '0') {
      text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
      text.pop_back();
    }
    return text;
  }
  if (value.is_string()) {
    return value.get<std::string>();
  }
  return value.dump();
}

}  // namespace

bool MeasurementStore::Open(const std::string& path, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  path_ = path;
  if (path_.empty()) {
    err = "存储路径为空";
    return false;
  }
  std::error_code ec;
  const auto parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
  }
  Load();
  return true;
}

void MeasurementStore::Load() {
  records_.clear();
  next_id_ = 1;
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    return;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    nlohmann::json record = nlohmann::json::parse(line, nullptr, false);
    if (record.is_discarded() || !record.is_object()) {
      continue;
    }
    const std::int64_t id = record.value("id", 0LL);
    if (id >= next_id_) {
      next_id_ = id + 1;
    }
    records_.push_back(std::move(record));
  }
}

void MeasurementStore::Rewrite() const {
  std::ofstream out(path_, std::ios::binary | std::ios::trunc);
  if (!out) {
    return;
  }
  for (const auto& record : records_) {
    out << record.dump() << '\n';
  }
}

std::int64_t MeasurementStore::Append(const nlohmann::json& record,
                                      std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  if (path_.empty()) {
    err = "存储未初始化";
    return -1;
  }
  std::error_code ec;
  const auto parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
  }
  nlohmann::json stored = record;
  const std::int64_t id = next_id_++;
  stored["id"] = id;

  std::ofstream out(path_, std::ios::binary | std::ios::app);
  if (!out) {
    err = "无法写入 " + path_;
    return -1;
  }
  out << stored.dump() << '\n';
  if (!out) {
    err = "写入失败";
    return -1;
  }
  records_.push_back(std::move(stored));
  return id;
}

bool MeasurementStore::Get(std::int64_t id, nlohmann::json& out) const {
  std::lock_guard<std::mutex> lock(mu_);
  for (const auto& record : records_) {
    if (record.value("id", 0LL) == id) {
      out = record;
      return true;
    }
  }
  return false;
}

bool MeasurementStore::Remove(std::int64_t id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  const auto before = records_.size();
  records_.erase(std::remove_if(records_.begin(), records_.end(),
                                [id](const nlohmann::json& r) {
                                  return r.value("id", 0LL) == id;
                                }),
                 records_.end());
  if (records_.size() == before) {
    err = "记录不存在";
    return false;
  }
  Rewrite();
  return true;
}

std::vector<nlohmann::json> MeasurementStore::List(const Filter& filter,
                                                   int offset, int limit,
                                                   int* total) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<nlohmann::json> matched;
  for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
    const auto& record = *it;
    const auto product = record.value("product", nlohmann::json::object());
    const std::string model = product.value("model", "");
    const std::string op = record.value("operator", "");
    const std::string status = record.value("status", "");
    const std::int64_t created = record.value("createdAt", 0LL);
    if (!Contains(model, filter.model)) {
      continue;
    }
    if (!filter.operator_name.empty() && op != filter.operator_name) {
      continue;
    }
    if (!filter.status.empty() && status != filter.status) {
      continue;
    }
    if (filter.from_ms > 0 && created < filter.from_ms) {
      continue;
    }
    if (filter.to_ms > 0 && created > filter.to_ms) {
      continue;
    }
    nlohmann::json row = record;
    row.erase("items");
    matched.push_back(std::move(row));
  }
  if (total != nullptr) {
    *total = static_cast<int>(matched.size());
  }
  std::vector<nlohmann::json> page;
  if (offset < 0) {
    offset = 0;
  }
  if (limit <= 0) {
    limit = 50;
  }
  for (int i = offset; i < static_cast<int>(matched.size()) && i < offset + limit;
       ++i) {
    page.push_back(std::move(matched[static_cast<std::size_t>(i)]));
  }
  return page;
}

std::string MeasurementStore::ExportCsv(std::int64_t id,
                                        std::string& err) const {
  nlohmann::json record;
  {
    std::lock_guard<std::mutex> lock(mu_);
    bool found = false;
    for (const auto& r : records_) {
      if (r.value("id", 0LL) == id) {
        record = r;
        found = true;
        break;
      }
    }
    if (!found) {
      err = "记录不存在";
      return {};
    }
  }

  const auto product = record.value("product", nlohmann::json::object());
  const auto items = record.value("items", nlohmann::json::array());
  const auto summary = record.value("summary", nlohmann::json::object());

  std::ostringstream out;
  out << "# 型号," << CsvCell(product.value("model", "")) << '\n';
  out << "# 产品编号," << CsvCell(product.value("code", "")) << '\n';
  out << "# 操作员," << CsvCell(record.value("operator", "")) << '\n';
  out << "# 端口," << CsvCell(record.value("port", "")) << '\n';
  out << "# 来源," << CsvCell(record.value("deviceSource", "")) << '\n';
  out << "# 状态," << CsvCell(record.value("status", "")) << '\n';
  out << "# 总数," << summary.value("total", 0) << ",通过,"
      << summary.value("passed", 0) << ",失败," << summary.value("failed", 0)
      << '\n';
  out << "序号,P,N,线制,量程,下限(Ω),上限(Ω),测量值(Ω),结果,原因\n";
  for (const auto& item : items) {
    out << item.value("seq", 0) << ',' << item.value("p", 0) << ','
        << item.value("n", 0) << ','
        << (item.value("wire", 0) == 0 ? "二线" : "四线") << ','
        << item.value("range", 0) << ','
        << NumberText(item.value("minOhms", nlohmann::json())) << ','
        << NumberText(item.value("maxOhms", nlohmann::json())) << ','
        << NumberText(item.value("value", nlohmann::json())) << ','
        << CsvCell(item.value("result", "")) << ','
        << CsvCell(item.value("reason", "")) << '\n';
  }
  return out.str();
}

std::string MeasurementStore::ExportCsvMany(
    const std::vector<std::int64_t>& ids, std::string& err) const {
  std::vector<nlohmann::json> picked;
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (std::int64_t id : ids) {
      for (const auto& record : records_) {
        if (record.value("id", 0LL) == id) {
          picked.push_back(record);
          break;
        }
      }
    }
  }
  if (picked.empty()) {
    err = "没有可导出的记录";
    return {};
  }

  std::ostringstream out;
  out << "记录ID,时间,型号,编号,操作员,序号,P,N,线制,量程,下限(Ω),上限(Ω),"
         "测量值(Ω),结果,原因\n";
  for (const auto& record : picked) {
    const auto product = record.value("product", nlohmann::json::object());
    const std::int64_t id = record.value("id", 0LL);
    const std::string when = TimeText(record.value("createdAt", 0LL));
    const std::string model = product.value("model", "");
    const std::string code = product.value("code", "");
    const std::string op = record.value("operator", "");
    for (const auto& item : record.value("items", nlohmann::json::array())) {
      out << id << ',' << CsvCell(when) << ',' << CsvCell(model) << ','
          << CsvCell(code) << ',' << CsvCell(op) << ','
          << item.value("seq", 0) << ',' << item.value("p", 0) << ','
          << item.value("n", 0) << ','
          << (item.value("wire", 0) == 0 ? "二线" : "四线") << ','
          << item.value("range", 0) << ','
          << NumberText(item.value("minOhms", nlohmann::json())) << ','
          << NumberText(item.value("maxOhms", nlohmann::json())) << ','
          << NumberText(item.value("value", nlohmann::json())) << ','
          << CsvCell(item.value("result", "")) << ','
          << CsvCell(item.value("reason", "")) << '\n';
    }
  }
  return out.str();
}

int MeasurementStore::Count() const {
  std::lock_guard<std::mutex> lock(mu_);
  return static_cast<int>(records_.size());
}

}  // namespace eph205
