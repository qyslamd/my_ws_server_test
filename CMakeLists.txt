cmake_minimum_required(VERSION 3.21)
project(cef109_ws_base_env C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出扁平化：exe 与运行期资源(www、data)都在构建目录根，
# 静态库进 lib/。exe 用相对工作目录找页面，这样从构建目录直接启动即可。
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(MSVC)
  # 全局按 UTF-8 解析源码:本项目源码含中文注释,
  # 且 mongoose 源码在 GBK 代码页下会产生 C4819
  add_compile_options(/utf-8)

  # 统一静态运行库:/MT。CEF 的 libcef_dll_wrapper 默认就是 /MT
  # (见 3rdparty/cef-109/cmake/cef_variables.cmake 的
  #  CEF_RUNTIME_LIBRARY_FLAG),wrapper 编成静态库后要链进 EPH205.exe,
  # 两边运行库不一致会报 LNK2038。所有目标(含 vendored 库)一律 /MT。
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

#-------------------------------------------------------------------------------
# clangd 支持
#-------------------------------------------------------------------------------

# 导出 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 生成 .clangd 指向本次构建目录（该文件不入库）
set(CLANGD_COMPILE_COMMANDS_DIR "${CMAKE_BINARY_DIR}")
file(TO_CMAKE_PATH "${CLANGD_COMPILE_COMMANDS_DIR}" CLANGD_COMPILE_COMMANDS_DIR)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/template/.clangd.in"
    "${CMAKE_SOURCE_DIR}/.clangd"
    @ONLY
)

# ============================================================
# vendored 第三方库（源码入库，版本写进目录名）
#   mongoose-7.23     HTTP/WS
#   sqlite-3540100    SQLite amalgamation（业务落库用）
#   googletest-1.17.0 单元测试框架（只留源码，用法见 tests/）
#   spdlog-1.17.0     日志（只留源码，捆绑 fmt；入口是 src/common/log/log.h）
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
# spdlog：日志
#
# 编成静态库（不用 header-only），包含目录标成 SYSTEM，避免 /W4 下把
# 我们自己的代码淹没在第三方头的告警里。
# ============================================================
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_SYSTEM_INCLUDES ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/spdlog-1.17.0"
                 spdlog_build EXCLUDE_FROM_ALL)

# ============================================================
# 子目标：公共基础设施（日志门面）
# ============================================================
add_subdirectory(src/common/log)

# ============================================================
# 子目标：基础通讯框架（纯 C/C++，零 Qt）
# ============================================================
add_subdirectory(src/framework/v1)

# ============================================================
# 子目标：EPH205 产品（模块 + 页面 + 入口）
# ============================================================
add_subdirectory(src/apps/eph205)

# ============================================================
# 子目标：示例
#   mongoose_cpp  双通道 WS 波形（按协议改造过的参考实现）
#   mongoose_c    纯 C 教学（单文件，零业务）
# ============================================================
add_subdirectory(src/examples/mongoose_cpp)
add_subdirectory(src/examples/mongoose_c)

# ============================================================
# 单元测试:googletest(vendored) + ctest
#   cmake --build build
#   ctest --test-dir build --output-on-failure
# ============================================================
include(CTest)

if(BUILD_TESTING)
  add_subdirectory(src/tests)
endif()

# ============================================================
# 子目标：桌面客户端（CEF 窗口外壳 + 进程内服务端）
#
# CEF 二进制放在 3rdparty/cef-109（不入库），默认不参与构建：
#   cmake -DBUILD_CEF_SHELL=ON ...
# ============================================================
option(BUILD_CEF_SHELL "构建 CEF 桌面客户端（需要 3rdparty/cef-109）" OFF)

if(BUILD_CEF_SHELL)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/cef-109/cmake/FindCEF.cmake")
    message(FATAL_ERROR
      "找不到 CEF: ${CMAKE_SOURCE_DIR}/3rdparty/cef-109\n"
      "把 CEF 109 的二进制约解压到 3rdparty/cef-109 后再开 BUILD_CEF_SHELL。")
  endif()
  set(CEF_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/cef-109" CACHE PATH "CEF binary distribution")
  list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
  find_package(CEF REQUIRED)
  # wrapper 从源码编译，保证和我们的 CRT/编译选项一致
  add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper EXCLUDE_FROM_ALL)
  add_subdirectory(src/common/cef_shell)
  add_subdirectory(src/apps/eph205_app)
endif()
