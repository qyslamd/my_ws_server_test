# ============================================================
# libwebsockets Demo (基于 4.3.3 LTS 版本)
# 目标: lws_demo,监听 http://127.0.0.1:8766
# 静态页面从 www/ 挂载点提供(见 lws_main.c 的 lws_http_mount)
# ============================================================
set(LWS_DIR "${CMAKE_SOURCE_DIR}/3rdparty/libwebsockets-4.3.3")

# 核心裁剪:关闭测试、关闭动态库
set(LWS_WITHOUT_TESTAPPS ON CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_SERVER ON CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_CLIENT ON CACHE BOOL "" FORCE)
set(LWS_WITH_STATIC ON CACHE BOOL "" FORCE)
set(LWS_WITH_SHARED OFF CACHE BOOL "" FORCE)

# 关闭不需要的协议和特性,避免引入多余依赖
set(LWS_WITH_HTTP3 OFF CACHE BOOL "" FORCE)   # 避免依赖 GnuTLS
set(LWS_WITH_SSL OFF CACHE BOOL "" FORCE)     # 避免依赖 OpenSSL
set(LWS_WITHOUT_SSH ON CACHE BOOL "" FORCE)
set(LWS_WITHOUT_EXTENSIONS ON CACHE BOOL "" FORCE)

add_subdirectory(${LWS_DIR} ${CMAKE_BINARY_DIR}/lws-build EXCLUDE_FROM_ALL)

add_executable(lws_demo lws_main.c
  lws_server.cpp
  lws_server.h
  )

target_link_libraries(lws_demo PRIVATE websockets)

if(MSVC)
  # /utf-8 由顶层统一提供,此处补 lws 源码在 MSVC 下的警告补丁
  target_compile_definitions(lws_demo PRIVATE _CRT_SECURE_NO_WARNINGS)

  target_compile_options(websockets PRIVATE
    /utf-8    # 关键:强制将 libwebsockets 源码按 UTF-8 解析,解决 C4819
    /wd4819   # 兜底:如果仍有无法解析的字符,强行抑制该警告
    /wd4142   # 抑制重复类型定义警告
    /wd4267   # 抑制 size_t 到 int 的转换
    /wd4996   # 抑制 deprecated 警告
  )
endif()
