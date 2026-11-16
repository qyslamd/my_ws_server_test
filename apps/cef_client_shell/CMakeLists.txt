# ============================================================
# CEF 客户端外壳：Win32 窗口 + 内嵌 CEF，业务无关
#
# 它只做三件事：开窗口、加载一个 URL、把浏览器行为（右键/刷新/
# 新窗口/拖放/权限）全部关掉。页面内容由 www/ 决定，壳本身不知道
# EPH205 是什么，换产品只改 URL 与图标。
#
# 依赖 3rdparty/cef-109（不入库，由顶层 CMake 引入）；零 Qt。
# 目标名 cef_client_shell，产物名保持 EPH205.exe（打包与自动化脚本
# 按此名找进程），换产品时只改下面的 OUTPUT_NAME。
# ============================================================
set(TARGET_NAME cef_client_shell)

add_executable(${TARGET_NAME} WIN32
  main.cpp
  client.cpp
  client.h
  main_window.cpp
  main_window.h
)

set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME EPH205)

target_include_directories(${TARGET_NAME} PRIVATE
  ${CEF_INCLUDE_PATH}
  ${CMAKE_SOURCE_DIR}/apps/v1
)

target_link_libraries(${TARGET_NAME} PRIVATE
  eph205_module
  libcef_dll_wrapper
  "${CEF_LIB_RELEASE}"      # 只引入 Release 的 libcef.lib；Debug 版没有入库
  ${CEF_STANDARD_LIBS}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /W4 /WX-)

# 运行时依赖：CEF 的 dll / pak / locales，以及页面
set(_cef_files
  libcef.dll
  chrome_elf.dll
  libEGL.dll
  libGLESv2.dll
  d3dcompiler_47.dll
  vk_swiftshader.dll
  vk_swiftshader_icd.json
  vulkan-1.dll
  snapshot_blob.bin
  v8_context_snapshot.bin
)

foreach(_file IN LISTS _cef_files)
  add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_BINARY_DIR_RELEASE}/${_file}" "$<TARGET_FILE_DIR:${TARGET_NAME}>"
    VERBATIM)
endforeach()

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:${TARGET_NAME}>"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/apps/v1/www" "$<TARGET_FILE_DIR:${TARGET_NAME}>/www"
  COMMENT "复制 CEF 运行时与页面到输出目录"
  VERBATIM)
