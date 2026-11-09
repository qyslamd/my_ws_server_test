cmake_minimum_required(VERSION 3.21)
project(my_ws_server_test C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Mongoose Demo
# ============================================================
set(MONGOOSE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mongoose-7.23")

add_library(mongoose STATIC "${MONGOOSE_DIR}/mongoose.c")
target_include_directories(mongoose PUBLIC "${MONGOOSE_DIR}")

add_executable(my_ws_server_test main.cpp)
target_link_libraries(my_ws_server_test PRIVATE mongoose)

if(MSVC)
  target_compile_options(my_ws_server_test PRIVATE /utf-8)
endif()

# ============================================================
# libwebsockets Demo (基于 4.3.3 LTS 版本)
# ============================================================
set(LWS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libwebsockets-4.3.3")

# 核心裁剪：关闭测试、关闭动态库
set(LWS_WITHOUT_TESTAPPS ON CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_SERVER ON CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_CLIENT ON CACHE BOOL "" FORCE)
set(LWS_WITH_STATIC ON CACHE BOOL "" FORCE)
set(LWS_WITH_SHARED OFF CACHE BOOL "" FORCE)

# 关闭不需要的协议和特性，避免引入多余依赖
set(LWS_WITH_HTTP3 OFF CACHE BOOL "" FORCE)   # 避免依赖 GnuTLS
set(LWS_WITH_SSL OFF CACHE BOOL "" FORCE)     # 避免依赖 OpenSSL
set(LWS_WITHOUT_SSH ON CACHE BOOL "" FORCE)
set(LWS_WITHOUT_EXTENSIONS ON CACHE BOOL "" FORCE)

add_subdirectory(${LWS_DIR} ${CMAKE_BINARY_DIR}/lws-build EXCLUDE_FROM_ALL)

add_executable(lws_demo lws_main.c)
target_link_libraries(lws_demo PRIVATE websockets)

if(MSVC)
  target_compile_options(lws_demo PRIVATE /utf-8)
  target_compile_definitions(lws_demo PRIVATE _CRT_SECURE_NO_WARNINGS)
  
  # 4.3.3 下针对源码字符集和警告的补丁
  target_compile_options(websockets PRIVATE
    /utf-8    # 关键：强制将 libwebsockets 源码按 UTF-8 解析，解决 C4819
    /wd4819   # 兜底：如果仍有无法解析的字符，强行抑制该警告
    /wd4142   # 抑制有符号/无符号不匹配
    /wd4267   # 抑制 size_t 到 int 的转换
    /wd4996   # 抑制 deprecated 警告
  )
endif()