# ============================================================
# EPH205 产品：业务模块 + 页面 + 入口
#
# 模块实现基础通讯框架的 v1::Module 接口，注册后得到完整的
# REST + 推送服务；www/ 是本产品的静态页，构建时复制到 exe 同级。
#
#   eph205_module  静态库（协议、串口、仿真、落库）
#   eph205_server  入口
# ============================================================
add_library(eph205_module STATIC
  eph205_client.cpp
  eph205_client.h
  eph205_module.cpp
  eph205_module.h
  measurement_store.cpp
  measurement_store.h
  protocol.cpp
  protocol.h
  serial_port.cpp
  serial_port.h
  simulator.cpp
  simulator.h
  win_registry.cpp
  win_registry.h
)

# 调用方以 "eph205/eph205_module.h" 引用
target_include_directories(eph205_module PUBLIC
  ${CMAKE_SOURCE_DIR}/src/apps
)

target_link_libraries(eph205_module PUBLIC v1_base sqlite3)

if(MSVC)
  # advapi32 用于注册表枚举串口
  target_link_libraries(eph205_module PUBLIC advapi32)
  target_compile_options(eph205_module PRIVATE /utf-8 /W4 /WX-)
endif()

add_executable(eph205_server main.cpp)
target_link_libraries(eph205_server PRIVATE eph205_module)

if(MSVC)
  target_compile_options(eph205_server PRIVATE /utf-8 /W4 /WX-)
endif()

# 页面跟着提供它的程序走：复制到 exe 同级，服务端按工作目录的 "www" 找。
# 挂在目标上而不是 ALL，只构建别的目标时不会白复制一遍。
add_custom_command(TARGET eph205_server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/www"
          "$<TARGET_FILE_DIR:eph205_server>/www"
  COMMENT "复制页面到 $<TARGET_FILE_DIR:eph205_server>/www"
  VERBATIM)
