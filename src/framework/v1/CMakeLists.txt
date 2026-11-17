# ============================================================
# 基础通讯框架（纯 C/C++，零 Qt）
#
# base/     HTTP/WS、会话、帧、订阅、流控、配置
# main.cpp  只含框架的入口，不含任何设备代码
#
#   v1_base    静态库
#   v1_server  纯框架实例
# 业务实例（apps/eph_205）注册自己的模块后自带 main。
# ============================================================
set(MONGOOSE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/mongoose-7.23")
set(JSON_DIR "${CMAKE_SOURCE_DIR}/3rdparty")

add_library(v1_base STATIC
  base/frame.cpp
  base/frame.h
  base/module.h
  base/server.cpp
  base/server.h
  base/stream_ring.cpp
  base/stream_ring.h
  base/stream_buffers.cpp
  base/stream_buffers.h
)

set_target_properties(v1_base PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

# 调用方以 "base/xxx.h" 引用，所以公开的是本目录
target_include_directories(v1_base PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${MONGOOSE_DIR}
  ${JSON_DIR}
)

target_link_libraries(v1_base PUBLIC mongoose)

if(MSVC)
  target_compile_options(v1_base PRIVATE /utf-8 /W4 /WX-)
endif()

add_executable(v1_server main.cpp)
target_link_libraries(v1_server PRIVATE v1_base)

if(MSVC)
  target_compile_options(v1_server PRIVATE /utf-8 /W4 /WX-)
endif()
