// Mongoose 版入口:服务端逻辑在 mongoose_server.h / mongoose_server.cpp
// 页面与脚本来自仓库根的 www/,由服务端从磁盘提供。
#include "mongoose_server.h"

int main() {
    Mongoose_Server server("http://127.0.0.1:8765");
    server.run();
    return 0;
}
