cmake_minimum_required(VERSION 3.21)
project(my_ws_server_test C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 可执行文件统一输出到 build/ 根目录,便于从 build/ 直接启动
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(MSVC)
  # 全局按 UTF-8 解析源码:本项目源码含中文注释,
  # 且 mongoose 源码在 GBK 代码页下会产生 C4819
  add_compile_options(/utf-8)
endif()

# ============================================================
# 共享前端资源 www/:服务端从磁盘提供页面,
# 每次构建同步到 build/,保证 cwd 在 build/ 时也能找到
# ============================================================
add_custom_target(copy_www ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/www" "${CMAKE_BINARY_DIR}/www"
  COMMENT "Sync www/ -> build/www"
  VERBATIM)

# ============================================================
# 子目标:mongoose 版双通道 WS 服务端
# ============================================================
add_subdirectory(apps/mongoose)
