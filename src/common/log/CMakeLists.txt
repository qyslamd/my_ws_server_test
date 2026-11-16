# ============================================================
# 日志门面
#
# log.h 是 spdlog 的唯一入口：logger 与 sink 只在 log.cpp 里出现。
# 调用方会连带 spdlog 捆绑的 fmt（log.h 里的 {} 变参格式化要用），
# 所以把它的包含目录以 SYSTEM 传给调用方：不进 /W4 的告警范围。
# 调用方以 "common/log/log.h" 引用。
# ============================================================
add_library(app_log STATIC
  log.cpp
  log.h
)

target_include_directories(app_log PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)

# PUBLIC 有两条理由：调用方的 log.h 要用 spdlog 捆绑的 fmt 头，
# 且必须和库里编进去的那份用同一种模式（SPDLOG_COMPILED_LIB 是 PUBLIC 定义），
# 否则 fmt 的符号会在头文件里再实例化一份，链接期重复定义。
# spdlog 的包含目录按 SYSTEM 传，不参与本项目的 /W4。
target_link_libraries(app_log PUBLIC spdlog::spdlog)

target_compile_definitions(app_log PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

if(MSVC)
  target_compile_options(app_log PRIVATE /utf-8 /W4 /WX-)
endif()
