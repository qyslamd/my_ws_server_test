# ============================================================
# 日志门面
#
# log.h 是 spdlog 的唯一入口：spdlog 头只在 log.cpp 里出现，
# 这样别的翻译单元不会被模板和 fmt 拖慢编译。
# 调用方以 "common/log/log.h" 引用。
# ============================================================
add_library(app_log STATIC
  log.cpp
  log.h
)

target_include_directories(app_log PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)

# PRIVATE：spdlog 的头不进调用方的编译单元
target_link_libraries(app_log PRIVATE spdlog::spdlog)

target_compile_definitions(app_log PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

if(MSVC)
  target_compile_options(app_log PRIVATE /utf-8 /W4 /WX-)
endif()
