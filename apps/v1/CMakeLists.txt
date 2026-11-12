# ============================================================
# v1 业务无关通讯底座（纯 C/C++，零 Qt）
#
# base/           HTTP/WS、会话、帧、订阅、作业路由、Blob、配置、认证
# main.cpp        纯底座入口：不含任何设备代码
#
# 底座编成静态库，业务实例各自带 main：
#   v1_server        只有底座（develop 上就到这里）
#   eph205_server    EPH205 业务实例（仅本地分支）
# ============================================================
set(MONGOOSE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/mongoose-7.23")
set(JSON_DIR "${CMAKE_SOURCE_DIR}/3rdparty")

add_library(v1_base STATIC
  base/server.cpp
  base/server.h
  base/module.h
  base/frame.cpp
  base/frame.h
)

set_target_properties(v1_base PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_include_directories(v1_base PUBLIC
  ${MONGOOSE_DIR}
  ${JSON_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
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

# ------------------------------------------------------------
# EPH205 业务模块（com0com 仿真 + 串口客户端）
# ------------------------------------------------------------
add_executable(eph205_server
  modules/eph205/main_eph205.cpp
  modules/eph205/eph205_module.cpp
  modules/eph205/eph205_module.h
  modules/eph205/eph205_client.cpp
  modules/eph205/eph205_client.h
  modules/eph205/protocol.cpp
  modules/eph205/protocol.h
  modules/eph205/serial_port.cpp
  modules/eph205/serial_port.h
  modules/eph205/simulator.cpp
  modules/eph205/simulator.h
  modules/eph205/measurement_store.cpp
  modules/eph205/measurement_store.h
  modules/eph205/win_registry.cpp
  modules/eph205/win_registry.h
)

target_link_libraries(eph205_server PRIVATE v1_base)
if(MSVC)
  # advapi32 用于注册表枚举串口
  target_link_libraries(eph205_server PRIVATE advapi32)
  target_compile_options(eph205_server PRIVATE /utf-8 /W4 /WX-)
endif()
