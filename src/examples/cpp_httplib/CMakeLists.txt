# cpp-httplib 教学 demo：单文件，服务端和客户端在同一个进程里。
#
# 只演示用法，不参与产品。产品服务端是 mongoose。
# cpp-httplib 是头文件库，包含目录标 SYSTEM，/W4 下不淹自己的告警。
# 它走异常，所以这个目标单独开 /EHsc。

set(HTTPLIB_DIR "${CMAKE_SOURCE_DIR}/3rdparty/cpp-httplib-0.66.0")

add_executable(cpp_httplib_demo main.cpp)
target_include_directories(cpp_httplib_demo SYSTEM PRIVATE "${HTTPLIB_DIR}")

if(MSVC)
  # /wd4127：httplib 里有一处常量条件告警
  target_compile_options(cpp_httplib_demo PRIVATE /utf-8 /W4 /WX- /EHsc /wd4127)
endif()

if(WIN32)
  target_link_libraries(cpp_httplib_demo PRIVATE ws2_32)
endif()
