#include "measurement_store.h"

#include <sqlite3.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace eph205 {

namespace {

// ---- JSON 取值：显式判类型，null 不能当数字用 ----

double Num(const nlohmann::json& obj, const char* key, double def) {
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_number()) {
    return def;
  }
  return it->get<double>();
}

bool HasNum(const nlohmann::json& obj, const char* key) {
  const auto it = obj.find(key);
  return it != obj.end() && it->is_number();
}

std::string Str(const nlohmann::json& obj, const char* key,
                const std::string& def = std::string()) {
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_string()) {
    return def;
  }
  return it->get<std::string>();
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return needle.empty() || haystack.find(needle) != std::string::npos;
}

std::string CsvCell(const std::string& text) {
  if (text.find_first_of(",\"\r\n") == std::string::npos) {
    return text;
  }
  std::string out = "\"";
  for (char ch : text) {
    if (ch == '"') {
      out.push_back('"');
    }
    out.push_back(ch);
  }
  out.push_back('"');
  return out;
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

std::string TimeText(std::int64_t ms) {
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm_buf = {};
  localtime_s(&tm_buf, &t);
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);
  return buffer;
}

// 语句 RAII：prepare 失败时 stmt_ 为 null，调用方检查 ok()。
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      stmt_ = nullptr;
    }
  }
  ~Stmt() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  bool ok() const { return stmt_ != nullptr; }
  sqlite3_stmt* get() const { return stmt_; }

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

void BindText(sqlite3_stmt* stmt, int index, const std::string& text) {
  sqlite3_bind_text(stmt, index, text.c_str(), -1, SQLITE_TRANSIENT);
}

void BindNumOrNull(sqlite3_stmt* stmt, int index, const nlohmann::json& obj,
                   const char* key) {
  if (HasNum(obj, key)) {
    sqlite3_bind_double(stmt, index, Num(obj, key, 0.0));
  } else {
    sqlite3_bind_null(stmt, index);
  }
}

nlohmann::json ColumnNumOrNull(sqlite3_stmt* stmt, int index) {
  if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
    return nlohmann::json();
  }
  return nlohmann::json(sqlite3_column_double(stmt, index));
}

}  // namespace

MeasurementStore::~MeasurementStore() {
  std::lock_guard<std::mutex> lock(mu_);
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool MeasurementStore::Exec(const char* sql, std::string& err) const {
  char* message = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
    err = message != nullptr ? message : "sql error";
    if (message != nullptr) {
      sqlite3_free(message);
    }
    return false;
  }
  return true;
}

bool MeasurementStore::Open(const std::string& path, std::string& err) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    if (path.empty()) {
      err = "存储路径为空";
      return false;
    }
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, ec);
    }
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
      err = db_ != nullptr ? sqlite3_errmsg(db_) : "无法打开数据库";
      if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
      }
      return false;
    }
    path_ = path;

    const char* kSchema =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS measurements ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  created_at INTEGER NOT NULL DEFAULT 0,"
        "  started_at INTEGER NOT NULL DEFAULT 0,"
        "  finished_at INTEGER NOT NULL DEFAULT 0,"
        "  model TEXT NOT NULL DEFAULT '',"
        "  code TEXT NOT NULL DEFAULT '',"
        "  figure TEXT NOT NULL DEFAULT '',"
        "  operator TEXT NOT NULL DEFAULT '',"
        "  status TEXT NOT NULL DEFAULT '',"
        "  device_source TEXT NOT NULL DEFAULT '',"
        "  port TEXT NOT NULL DEFAULT '',"
        "  firmware TEXT NOT NULL DEFAULT '',"
        "  config_json TEXT NOT NULL DEFAULT '{}',"
        "  total INTEGER NOT NULL DEFAULT 0,"
        "  passed INTEGER NOT NULL DEFAULT 0,"
        "  failed INTEGER NOT NULL DEFAULT 0,"
        "  measure_failed INTEGER NOT NULL DEFAULT 0,"
        "  not_measured INTEGER NOT NULL DEFAULT 0,"
        "  pass_rate REAL NOT NULL DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS measurement_items ("
        "  measurement_id INTEGER NOT NULL,"
        "  seq INTEGER NOT NULL,"
        "  p INTEGER NOT NULL DEFAULT 0,"
        "  n INTEGER NOT NULL DEFAULT 0,"
        "  wire INTEGER NOT NULL DEFAULT 0,"
        "  range_idx INTEGER NOT NULL DEFAULT 0,"
        "  min_ohms REAL,"
        "  max_ohms REAL,"
        "  measure INTEGER NOT NULL DEFAULT 1,"
        "  value REAL,"
        "  result TEXT NOT NULL DEFAULT '',"
        "  reason TEXT NOT NULL DEFAULT '',"
        "  PRIMARY KEY (measurement_id, seq)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_measurements_created"
        "  ON measurements(created_at DESC);";
    if (!Exec(kSchema, err)) {
      sqlite3_close(db_);
      db_ = nullptr;
      return false;
    }
  }

  // 迁移放在锁外：Append 自己会加锁
  ImportLegacyJsonl(
      (std::filesystem::path(path).parent_path() / "measurements.jsonl")
          .string());
  return true;
}

void MeasurementStore::ImportLegacyJsonl(const std::string& jsonl_path) {
  std::error_code ec;
  if (!std::filesystem::exists(jsonl_path, ec)) {
    return;
  }
  if (Count() != 0) {
    return;
  }
  int imported = 0;
  {
    // 读完必须先关文件：Windows 下对打开中的文件 rename 会失败
    std::ifstream in(jsonl_path, std::ios::binary);
    if (!in) {
      return;
    }
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) {
        continue;
      }
      const auto record = nlohmann::json::parse(line, nullptr, false);
      if (record.is_discarded() || !record.is_object()) {
        continue;
      }
      std::string err;
      if (Append(record, err) >= 0) {
        ++imported;
      }
    }
  }
  // 导入过就改名，避免下次重复导
  if (imported > 0) {
    std::error_code rename_ec;
    std::filesystem::rename(jsonl_path, jsonl_path + ".imported", rename_ec);
  }
}

std::int64_t MeasurementStore::Append(const nlohmann::json& record,
                                      std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  if (db_ == nullptr) {
    err = "存储未打开";
    return -1;
  }
  if (!Exec("BEGIN IMMEDIATE", err)) {
    return -1;
  }

  const auto product = record.value("product", nlohmann::json::object());
  const auto summary = record.value("summary", nlohmann::json::object());
  const auto config = record.value("config", nlohmann::json::object());

  Stmt header(db_,
              "INSERT INTO measurements (created_at, started_at, finished_at,"
              " model, code, figure, operator, status, device_source, port,"
              " firmware, config_json, total, passed, failed, measure_failed,"
              " not_measured, pass_rate)"
              " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,"
              "?16,?17,?18)");
  if (!header.ok()) {
    Exec("ROLLBACK", err);
    err = "语句准备失败";
    return -1;
  }
  sqlite3_stmt* h = header.get();
  sqlite3_bind_int64(h, 1, static_cast<sqlite3_int64>(Num(record, "createdAt", 0)));
  sqlite3_bind_int64(h, 2, static_cast<sqlite3_int64>(Num(record, "startedAt", 0)));
  sqlite3_bind_int64(h, 3, static_cast<sqlite3_int64>(Num(record, "finishedAt", 0)));
  BindText(h, 4, Str(product, "model"));
  BindText(h, 5, Str(product, "code"));
  BindText(h, 6, Str(product, "figure"));
  BindText(h, 7, Str(record, "operator"));
  BindText(h, 8, Str(record, "status"));
  BindText(h, 9, Str(record, "deviceSource"));
  BindText(h, 10, Str(record, "port"));
  BindText(h, 11, Str(record, "firmware"));
  BindText(h, 12, config.is_object() ? config.dump() : std::string("{}"));
  sqlite3_bind_int(h, 13, static_cast<int>(Num(summary, "total", 0)));
  sqlite3_bind_int(h, 14, static_cast<int>(Num(summary, "passed", 0)));
  sqlite3_bind_int(h, 15, static_cast<int>(Num(summary, "failed", 0)));
  sqlite3_bind_int(h, 16, static_cast<int>(Num(summary, "measureFailed", 0)));
  sqlite3_bind_int(h, 17, static_cast<int>(Num(summary, "notMeasured", 0)));
  sqlite3_bind_double(h, 18, Num(summary, "passRate", 0));

  if (sqlite3_step(h) != SQLITE_DONE) {
    err = sqlite3_errmsg(db_);
    Exec("ROLLBACK", err);
    return -1;
  }
  const std::int64_t id =
      static_cast<std::int64_t>(sqlite3_last_insert_rowid(db_));

  Stmt item(db_,
            "INSERT INTO measurement_items (measurement_id, seq, p, n, wire,"
            " range_idx, min_ohms, max_ohms, measure, value, result, reason)"
            " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)");
  if (!item.ok()) {
    err = "明细语句准备失败";
    Exec("ROLLBACK", err);
    return -1;
  }
  std::int64_t seq_fallback = 0;
  for (const auto& row : record.value("items", nlohmann::json::array())) {
    ++seq_fallback;
    sqlite3_stmt* s = item.get();
    sqlite3_reset(s);
    sqlite3_clear_bindings(s);
    sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(id));
    sqlite3_bind_int(s, 2, static_cast<int>(Num(row, "seq", seq_fallback)));
    sqlite3_bind_int(s, 3, static_cast<int>(Num(row, "p", 0)));
    sqlite3_bind_int(s, 4, static_cast<int>(Num(row, "n", 0)));
    sqlite3_bind_int(s, 5, static_cast<int>(Num(row, "wire", 0)));
    sqlite3_bind_int(s, 6, static_cast<int>(Num(row, "range", 0)));
    BindNumOrNull(s, 7, row, "minOhms");
    BindNumOrNull(s, 8, row, "maxOhms");
    sqlite3_bind_int(s, 9, Num(row, "measure", 1) != 0 ? 1 : 0);
    BindNumOrNull(s, 10, row, "value");
    BindText(s, 11, Str(row, "result"));
    BindText(s, 12, Str(row, "reason"));
    if (sqlite3_step(s) != SQLITE_DONE) {
      err = sqlite3_errmsg(db_);
      Exec("ROLLBACK", err);
      return -1;
    }
  }

  if (!Exec("COMMIT", err)) {
    Exec("ROLLBACK", err);
    return -1;
  }
  return id;
}

bool MeasurementStore::QueryRecord(std::int64_t id, nlohmann::json& out) const {
  Stmt header(db_,
              "SELECT id, created_at, started_at, finished_at, model, code,"
              " figure, operator, status, device_source, port, firmware,"
              " config_json, total, passed, failed, measure_failed,"
              " not_measured, pass_rate FROM measurements WHERE id = ?1");
  if (!header.ok()) {
    return false;
  }
  sqlite3_bind_int64(header.get(), 1, static_cast<sqlite3_int64>(id));
  if (sqlite3_step(header.get()) != SQLITE_ROW) {
    return false;
  }
  sqlite3_stmt* h = header.get();

  out = nlohmann::json::object();
  out["id"] = sqlite3_column_int64(h, 0);
  out["createdAt"] = sqlite3_column_int64(h, 1);
  out["startedAt"] = sqlite3_column_int64(h, 2);
  out["finishedAt"] = sqlite3_column_int64(h, 3);
  const auto text = [h](int col) -> std::string {
    const char* value =
        reinterpret_cast<const char*>(sqlite3_column_text(h, col));
    return value != nullptr ? value : "";
  };
  out["product"] = {{"model", text(4)}, {"code", text(5)}, {"figure", text(6)}};
  out["operator"] = text(7);
  out["status"] = text(8);
  out["deviceSource"] = text(9);
  out["port"] = text(10);
  out["firmware"] = text(11);
  out["config"] = nlohmann::json::parse(text(12), nullptr, false);
  if (out["config"].is_discarded()) {
    out["config"] = nlohmann::json::object();
  }
  out["summary"] = {{"total", sqlite3_column_int(h, 13)},
                    {"passed", sqlite3_column_int(h, 14)},
                    {"failed", sqlite3_column_int(h, 15)},
                    {"measureFailed", sqlite3_column_int(h, 16)},
                    {"notMeasured", sqlite3_column_int(h, 17)},
                    {"passRate", sqlite3_column_double(h, 18)}};

  Stmt items(db_,
             "SELECT seq, p, n, wire, range_idx, min_ohms, max_ohms, measure,"
             " value, result, reason FROM measurement_items"
             " WHERE measurement_id = ?1 ORDER BY seq ASC");
  if (!items.ok()) {
    return false;
  }
  sqlite3_bind_int64(items.get(), 1, static_cast<sqlite3_int64>(id));
  nlohmann::json rows = nlohmann::json::array();
  for (;;) {
    const int rc = sqlite3_step(items.get());
    if (rc != SQLITE_ROW) {
      break;
    }
    sqlite3_stmt* s = items.get();
    nlohmann::json row;
    row["seq"] = sqlite3_column_int(s, 0);
    row["p"] = sqlite3_column_int(s, 1);
    row["n"] = sqlite3_column_int(s, 2);
    row["wire"] = sqlite3_column_int(s, 3);
    row["range"] = sqlite3_column_int(s, 4);
    row["minOhms"] = ColumnNumOrNull(s, 5);
    row["maxOhms"] = ColumnNumOrNull(s, 6);
    row["measure"] = sqlite3_column_int(s, 7) != 0;
    row["value"] = ColumnNumOrNull(s, 8);
    const char* result = reinterpret_cast<const char*>(sqlite3_column_text(s, 9));
    row["result"] = result != nullptr ? result : "";
    const char* reason = reinterpret_cast<const char*>(sqlite3_column_text(s, 10));
    row["reason"] = reason != nullptr ? reason : "";
    rows.push_back(std::move(row));
  }
  out["items"] = std::move(rows);
  return true;
}

bool MeasurementStore::Get(std::int64_t id, nlohmann::json& out) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (db_ == nullptr) {
    return false;
  }
  return QueryRecord(id, out);
}

bool MeasurementStore::Remove(std::int64_t id, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  if (db_ == nullptr) {
    err = "存储未打开";
    return false;
  }
  if (!Exec("BEGIN IMMEDIATE", err)) {
    return false;
  }
  Stmt del_items(db_, "DELETE FROM measurement_items WHERE measurement_id = ?1");
  Stmt del_header(db_, "DELETE FROM measurements WHERE id = ?1");
  if (!del_items.ok() || !del_header.ok()) {
    Exec("ROLLBACK", err);
    err = "语句准备失败";
    return false;
  }
  sqlite3_bind_int64(del_items.get(), 1, static_cast<sqlite3_int64>(id));
  sqlite3_bind_int64(del_header.get(), 1, static_cast<sqlite3_int64>(id));
  sqlite3_step(del_items.get());
  sqlite3_step(del_header.get());
  if (sqlite3_changes(db_) == 0) {
    Exec("ROLLBACK", err);
    err = "记录不存在";
    return false;
  }
  return Exec("COMMIT", err);
}

std::vector<nlohmann::json> MeasurementStore::List(const Filter& filter,
                                                   int offset, int limit,
                                                   int* total) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<nlohmann::json> page;
  if (db_ == nullptr) {
    if (total != nullptr) {
      *total = 0;
    }
    return page;
  }
  if (offset < 0) {
    offset = 0;
  }
  if (limit <= 0) {
    limit = 50;
  }

  std::string where = " WHERE 1=1";
  if (!filter.model.empty()) where += " AND model LIKE ?";
  if (!filter.operator_name.empty()) where += " AND operator = ?";
  if (!filter.status.empty()) where += " AND status = ?";
  if (filter.from_ms > 0) where += " AND created_at >= ?";
  if (filter.to_ms > 0) where += " AND created_at <= ?";

  const std::string count_sql = "SELECT COUNT(*) FROM measurements" + where;
  Stmt count_stmt(db_, count_sql.c_str());
  if (!count_stmt.ok()) {
    if (total != nullptr) {
      *total = 0;
    }
    return page;
  }
  int index = 1;
  if (!filter.model.empty()) {
    BindText(count_stmt.get(), index++, "%" + filter.model + "%");
  }
  if (!filter.operator_name.empty()) {
    BindText(count_stmt.get(), index++, filter.operator_name);
  }
  if (!filter.status.empty()) {
    BindText(count_stmt.get(), index++, filter.status);
  }
  if (filter.from_ms > 0) {
    sqlite3_bind_int64(count_stmt.get(), index++, filter.from_ms);
  }
  if (filter.to_ms > 0) {
    sqlite3_bind_int64(count_stmt.get(), index++, filter.to_ms);
  }
  if (sqlite3_step(count_stmt.get()) == SQLITE_ROW && total != nullptr) {
    *total = sqlite3_column_int(count_stmt.get(), 0);
  }

  const std::string sql =
      "SELECT id, created_at, model, code, operator, status, device_source,"
      " total, passed, failed, measure_failed, not_measured, pass_rate"
      " FROM measurements" + where +
      " ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?";
  Stmt stmt(db_, sql.c_str());
  if (!stmt.ok()) {
    return page;
  }
  index = 1;
  if (!filter.model.empty()) {
    BindText(stmt.get(), index++, "%" + filter.model + "%");
  }
  if (!filter.operator_name.empty()) {
    BindText(stmt.get(), index++, filter.operator_name);
  }
  if (!filter.status.empty()) {
    BindText(stmt.get(), index++, filter.status);
  }
  if (filter.from_ms > 0) {
    sqlite3_bind_int64(stmt.get(), index++, filter.from_ms);
  }
  if (filter.to_ms > 0) {
    sqlite3_bind_int64(stmt.get(), index++, filter.to_ms);
  }
  sqlite3_bind_int(stmt.get(), index++, limit);
  sqlite3_bind_int(stmt.get(), index++, offset);

  for (;;) {
    const int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_ROW) {
      break;
    }
    sqlite3_stmt* s = stmt.get();
    const auto text = [s](int col) -> std::string {
      const char* value = reinterpret_cast<const char*>(sqlite3_column_text(s, col));
      return value != nullptr ? value : "";
    };
    nlohmann::json row;
    row["id"] = sqlite3_column_int64(s, 0);
    row["createdAt"] = sqlite3_column_int64(s, 1);
    row["product"] = {{"model", text(2)}, {"code", text(3)}};
    row["operator"] = text(4);
    row["status"] = text(5);
    row["deviceSource"] = text(6);
    row["summary"] = {{"total", sqlite3_column_int(s, 7)},
                      {"passed", sqlite3_column_int(s, 8)},
                      {"failed", sqlite3_column_int(s, 9)},
                      {"measureFailed", sqlite3_column_int(s, 10)},
                      {"notMeasured", sqlite3_column_int(s, 11)},
                      {"passRate", sqlite3_column_double(s, 12)}};
    page.push_back(std::move(row));
  }
  return page;
}

std::string MeasurementStore::ExportCsv(std::int64_t id, std::string& err) const {
  nlohmann::json record;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (db_ == nullptr) {
      err = "存储未打开";
      return {};
    }
    if (!QueryRecord(id, record)) {
      err = "记录不存在";
      return {};
    }
  }

  const auto product = record.value("product", nlohmann::json::object());
  const auto items = record.value("items", nlohmann::json::array());
  const auto summary = record.value("summary", nlohmann::json::object());

  std::ostringstream out;
  out << "# 型号," << CsvCell(Str(product, "model")) << '\n';
  out << "# 产品编号," << CsvCell(Str(product, "code")) << '\n';
  out << "# 操作员," << CsvCell(Str(record, "operator")) << '\n';
  out << "# 端口," << CsvCell(Str(record, "port")) << '\n';
  out << "# 来源," << CsvCell(Str(record, "deviceSource")) << '\n';
  out << "# 状态," << CsvCell(Str(record, "status")) << '\n';
  out << "# 总数," << static_cast<int>(Num(summary, "total", 0)) << ",通过,"
      << static_cast<int>(Num(summary, "passed", 0)) << ",失败,"
      << static_cast<int>(Num(summary, "failed", 0)) << '\n';
  out << "序号,P,N,线制,量程,下限(Ω),上限(Ω),测量值(Ω),结果,原因\n";
  for (const auto& item : items) {
    out << static_cast<int>(Num(item, "seq", 0)) << ','
        << static_cast<int>(Num(item, "p", 0)) << ','
        << static_cast<int>(Num(item, "n", 0)) << ','
        << (Num(item, "wire", 0) == 0 ? "二线" : "四线") << ','
        << static_cast<int>(Num(item, "range", 0)) << ','
        << NumberText(item.contains("minOhms") ? item["minOhms"] : nlohmann::json())
        << ','
        << NumberText(item.contains("maxOhms") ? item["maxOhms"] : nlohmann::json())
        << ','
        << NumberText(item.contains("value") ? item["value"] : nlohmann::json())
        << ',' << CsvCell(Str(item, "result")) << ','
        << CsvCell(Str(item, "reason")) << '\n';
  }
  return out.str();
}

std::string MeasurementStore::ExportCsvMany(
    const std::vector<std::int64_t>& ids, std::string& err) const {
  std::vector<nlohmann::json> picked;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (db_ == nullptr) {
      err = "存储未打开";
      return {};
    }
    for (std::int64_t id : ids) {
      nlohmann::json record;
      if (QueryRecord(id, record)) {
        picked.push_back(std::move(record));
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
    const auto id = static_cast<int>(Num(record, "id", 0));
    const std::string when =
        TimeText(static_cast<std::int64_t>(Num(record, "createdAt", 0)));
    const std::string model = Str(product, "model");
    const std::string code = Str(product, "code");
    const std::string op = Str(record, "operator");
    for (const auto& item : record.value("items", nlohmann::json::array())) {
      out << id << ',' << CsvCell(when) << ',' << CsvCell(model) << ','
          << CsvCell(code) << ',' << CsvCell(op) << ','
          << static_cast<int>(Num(item, "seq", 0)) << ','
          << static_cast<int>(Num(item, "p", 0)) << ','
          << static_cast<int>(Num(item, "n", 0)) << ','
          << (Num(item, "wire", 0) == 0 ? "二线" : "四线") << ','
          << static_cast<int>(Num(item, "range", 0)) << ','
          << NumberText(item.contains("minOhms") ? item["minOhms"]
                                                 : nlohmann::json())
          << ','
          << NumberText(item.contains("maxOhms") ? item["maxOhms"]
                                                 : nlohmann::json())
          << ','
          << NumberText(item.contains("value") ? item["value"]
                                               : nlohmann::json())
          << ',' << CsvCell(Str(item, "result")) << ','
          << CsvCell(Str(item, "reason")) << '\n';
    }
  }
  return out.str();
}

int MeasurementStore::Count() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (db_ == nullptr) {
    return 0;
  }
  Stmt stmt(db_, "SELECT COUNT(*) FROM measurements");
  if (!stmt.ok()) {
    return 0;
  }
  if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    return sqlite3_column_int(stmt.get(), 0);
  }
  return 0;
}

}  // namespace eph205
