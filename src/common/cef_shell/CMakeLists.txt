# ============================================================
# CEF 窗口外壳（静态库）
#
# 只做三件事：开一个无边框外的普通窗口、内嵌 CEF 显示一个地址、
# 把浏览器的刷新/右键/新窗口/拖放/权限全部关掉。
# 它不认识任何业务：地址、标题、缓存目录都由宿主传入。
#
# 需要 3rdparty/cef-109（不入库），由顶层在 BUILD_CEF_SHELL 打开时引入。
# ============================================================
add_library(cef_shell STATIC
  cef_shell.cpp
  cef_shell.h
  client.cpp
  client.h
  main_window.cpp
  main_window.h
  shell_options.h
)

target_include_directories(cef_shell PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CEF_INCLUDE_PATH}
)

# CEF 的头与库暴露给宿主，宿主自己不必再写一遍
target_link_libraries(cef_shell PUBLIC
  libcef_dll_wrapper
  "${CEF_LIB_RELEASE}"
  ${CEF_STANDARD_LIBS}
)

target_link_libraries(cef_shell PRIVATE app_log)

target_compile_definitions(cef_shell PRIVATE
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

if(MSVC)
  target_compile_options(cef_shell PRIVATE /utf-8 /W4 /WX-)
endif()
