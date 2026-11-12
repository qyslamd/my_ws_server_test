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
