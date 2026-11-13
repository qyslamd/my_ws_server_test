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
# vendored 第三方库（源码入库，版本写进目录名）
#   mongoose-7.23     HTTP/WS
#   sqlite-3540100    SQLite amalgamation（业务落库用）
#   3rdparty/cef-109  CEF 二进制约 260 MB，不入库，见 docs/CEF客户端方案.md
# ============================================================
add_library(sqlite3 STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/sqlite-3540100/sqlite3.c"
)
target_include_directories(sqlite3 PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/sqlite-3540100"
)
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_THREADSAFE=1              # 多线程模式：一个连接一个线程
  SQLITE_DQS=0                     # 禁止用双引号当字符串字面量
  SQLITE_OMIT_LOAD_EXTENSION       # 不加载外部扩展
  SQLITE_DEFAULT_FOREIGN_KEYS=1
)
if(MSVC)
  # 第三方源码不参与我们的 /W4，避免淹没自己的告警
  target_compile_options(sqlite3 PRIVATE /W0)
endif()

# ============================================================
# 子目标:mongoose 版双通道 WS 服务端
# ============================================================
add_subdirectory(apps/mongoose)

# ============================================================
# 子目标:v1 业务无关通讯底座(纯 C/C++,零 Qt)
# ============================================================
add_subdirectory(apps/v1)

# ============================================================
# 子目标:EPH205 桌面客户端(CEF)
#
# CEF 二进制放在 3rdparty/cef-109(不入库),默认不参与构建:
#   cmake -DEPH205_BUILD_CLIENT=ON ...
# ============================================================
option(EPH205_BUILD_CLIENT "构建 CEF 桌面客户端(需要 3rdparty/cef-109)" OFF)

if(EPH205_BUILD_CLIENT)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/cef-109/cmake/FindCEF.cmake")
    message(FATAL_ERROR
      "找不到 CEF: ${CMAKE_SOURCE_DIR}/3rdparty/cef-109
"
      "把 CEF 109 的二进制约解压到 3rdparty/cef-109 后再开 EPH205_BUILD_CLIENT。")
  endif()
  set(CEF_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/cef-109" CACHE PATH "CEF binary distribution")
  list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
  find_package(CEF REQUIRED)
  # wrapper 从源码编译，保证和我们的 CRT/编译选项一致
  add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper EXCLUDE_FROM_ALL)
  add_subdirectory(apps/eph205_client)
endif()
