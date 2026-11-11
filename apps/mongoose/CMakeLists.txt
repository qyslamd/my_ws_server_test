# ============================================================
# Mongoose Demo (7.23)
# 目标: mongoose_demo,监听 http://127.0.0.1:8765
# 静态页面从 www/ 目录提供(见 main.cpp 的 mg_http_serve_dir)
# ============================================================
set(MONGOOSE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/mongoose-7.23")
# nlohmann/json 以 vendored 单头文件形式入库(3rdparty/nlohmann/json.hpp),
# 不依赖本地解压的完整源码包
set(JSON_DIR "${CMAKE_SOURCE_DIR}/3rdparty")

add_library(mongoose STATIC "${MONGOOSE_DIR}/mongoose.c")
target_include_directories(mongoose PUBLIC "${MONGOOSE_DIR}")

add_executable(mongoose_demo mongoose_main.cpp
  mongoose_server.cpp
  mongoose_server.h
  )

# 引入 nlohmann/json 头文件
target_include_directories(mongoose_demo PRIVATE "${JSON_DIR}")

target_link_libraries(mongoose_demo PRIVATE mongoose)