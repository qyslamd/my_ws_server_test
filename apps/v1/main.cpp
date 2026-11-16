#include <cstdlib>
#include <string>

#include "base/server.h"

int main(int argc, char** argv) {
  int port = 8770;
  bool auth_required = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--auth") {
      auth_required = true;
    } else {
      port = std::atoi(argv[i]);
    }
  }

  // 业务无关的通讯底座：业务模块通过 RegisterModule / RegisterMediaSource
  // 注册进来。本仓库不包含任何具体设备或项目代码。
  v1::V1Server server(port, auth_required);
  server.Run();
  return 0;
}
