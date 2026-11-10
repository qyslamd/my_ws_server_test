// libwebsockets 版入口:服务端逻辑在 lws_server.h / lws_server.cpp
// (类封装 + extern "C" 接口,本文件保持纯 C)
#include "lws_server.h"

int main(void) {
    return lws_server_main();
}
