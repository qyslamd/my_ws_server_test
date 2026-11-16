# ============================================================
# EPH205 桌面客户端：Win32 窗口 + 内嵌 CEF
#
# 依赖 3rdparty/cef-109（不入库，由顶层 CMake 引入）
# 零 Qt：只有 Win32 + CEF + 本仓的 v1 底座
# ============================================================
set(TARGET_NAME EPH205)

add_executable(${TARGET_NAME} WIN32
  main.cpp
  client.cpp
  client.h
  main_window.cpp
  main_window.h
)

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
          "${CMAKE_SOURCE_DIR}/www/v1" "$<TARGET_FILE_DIR:${TARGET_NAME}>/www/v1"
  COMMENT "复制 CEF 运行时与页面到输出目录"
  VERBATIM)
