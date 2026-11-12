# ============================================================
# v1 业务无关通讯底座（纯 C/C++，零 Qt）
#
# base/        HTTP/WS、会话、帧、订阅、作业路由、Blob、配置、认证
# main.cpp     仅启动底座；业务模块由使用方自行注册
# ============================================================
set(TARGET_NAME v1_server)
set(MONGOOSE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/mongoose-7.23")
set(JSON_DIR "${CMAKE_SOURCE_DIR}/3rdparty")

add_executable(${TARGET_NAME}
  main.cpp
  base/server.cpp
  base/server.h
  base/module.h
  base/frame.cpp
  base/frame.h
)

set_target_properties(${TARGET_NAME} PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_include_directories(${TARGET_NAME} PRIVATE
  ${MONGOOSE_DIR}
  ${JSON_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME} PRIVATE mongoose)

if(MSVC)
  target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /W4 /WX-)
endif()
