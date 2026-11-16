# ============================================================
# Mongoose Demo (7.23):双通道 WS 波形,按协议改造过的参考实现
# 目标: mongoose_cpp_demo,监听 http://127.0.0.1:8765
# 页面在本目录 www/,构建时复制到 build/apps/mongoose_cpp/www
# ============================================================
set(MONGOOSE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/mongoose-7.23")
# nlohmann/json 以 vendored 单头文件形式入库(3rdparty/nlohmann/json.hpp),
# 不依赖本地解压的完整源码包
set(JSON_DIR "${CMAKE_SOURCE_DIR}/3rdparty")

add_library(mongoose STATIC "${MONGOOSE_DIR}/mongoose.c")
target_include_directories(mongoose PUBLIC "${MONGOOSE_DIR}")

set(TARGET_NAME "mongoose_cpp_demo")
add_executable(${TARGET_NAME}
  main.cpp
  mongoose_server.cpp
  mongoose_server.h
  )

# 引入 nlohmann/json 头文件
target_include_directories(${TARGET_NAME} PRIVATE "${JSON_DIR}")

target_link_libraries(${TARGET_NAME} PRIVATE mongoose)

# 页面归本 demo 所有(apps/mongoose_cpp/www);生产前端在 apps/v1/www。
# 复制到 build/apps/mongoose_cpp/www,
# mongoose_server.cpp 里的 root_dir 与之对应(cwd 用 build\ 启动)。
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/www"
          "${CMAKE_BINARY_DIR}/apps/mongoose_cpp/www"
  COMMENT "复制 demo 页面到 build/apps/mongoose_cpp/www"
  VERBATIM)