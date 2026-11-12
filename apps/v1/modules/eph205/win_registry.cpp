#include "win_registry.h"

namespace eph205::winreg {

std::vector<std::pair<std::string, std::string>> EnumValues(HKEY root,
                                                            const char* path) {
  std::vector<std::pair<std::string, std::string>> out;
  HKEY key = nullptr;
  if (RegOpenKeyExA(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return out;
  }
  for (DWORD i = 0;; ++i) {
    char name[512] = {0};
    char data[512] = {0};
    DWORD name_len = sizeof(name);
    DWORD data_len = sizeof(data);
    DWORD type = 0;
    const LONG rc = RegEnumValueA(key, i, name, &name_len, nullptr, &type,
                                  reinterpret_cast<BYTE*>(data), &data_len);
    if (rc == ERROR_NO_MORE_ITEMS) {
      break;
    }
    if (rc != ERROR_SUCCESS) {
      continue;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
      continue;
    }
    out.emplace_back(name, data);
  }
  RegCloseKey(key);
  return out;
}

std::vector<std::string> EnumSubKeys(HKEY root, const char* path) {
  std::vector<std::string> out;
  HKEY key = nullptr;
  if (RegOpenKeyExA(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return out;
  }
  for (DWORD i = 0;; ++i) {
    char name[512] = {0};
    DWORD name_len = sizeof(name);
    const LONG rc =
        RegEnumKeyExA(key, i, name, &name_len, nullptr, nullptr, nullptr,
                      nullptr);
    if (rc == ERROR_NO_MORE_ITEMS) {
      break;
    }
    if (rc != ERROR_SUCCESS) {
      continue;
    }
    out.emplace_back(name);
  }
  RegCloseKey(key);
  return out;
}

std::string ReadString(HKEY root, const char* path, const char* name) {
  HKEY key = nullptr;
  if (RegOpenKeyExA(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return {};
  }
  char data[1024] = {0};
  DWORD data_len = sizeof(data);
  DWORD type = 0;
  std::string out;
  if (RegQueryValueExA(key, name, nullptr, &type,
                       reinterpret_cast<BYTE*>(data), &data_len) ==
          ERROR_SUCCESS &&
      (type == REG_SZ || type == REG_EXPAND_SZ)) {
    out = data;
  }
  RegCloseKey(key);
  return out;
}

}  // namespace eph205::winreg
