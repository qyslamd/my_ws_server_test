#include <cstdlib>
#include <string>

#include "base/server.h"
#include "modules/eph205/eph205_module.h"

// EPH205 业务实例：底座 + 一个业务模块。
// 底座自身不包含任何设备代码，main.cpp 仍是纯底座入口。
int main(int argc, char** argv) {
  int port = 8790;
  bool auth_required = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--auth") {
      auth_required = true;
    } else {
      port = std::atoi(argv[i]);
    }
  }

  v1::V1Server server(port, auth_required);
  eph205::Eph205Module module;
  server.RegisterModule(&module);
  server.Run();
  return 0;
}
