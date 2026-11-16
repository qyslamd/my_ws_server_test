# ============================================================
# 桌面客户端（exe）
#
# 只负责组装：进程分流（CEF 子进程）、单实例、起进程内服务端、
# 把地址交给 cef_shell 库。窗口与浏览器行为都在库里。
#
# 产物名保持 EPH205.exe：打包与自动化脚本按这个名字找进程。
# ============================================================
add_executable(eph205_app WIN32
  main.cpp
)

set_target_properties(eph205_app PROPERTIES OUTPUT_NAME EPH205)

target_link_libraries(eph205_app PRIVATE
  cef_shell
  app_log
  eph205_module
)

target_compile_definitions(eph205_app PRIVATE
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

if(MSVC)
  target_compile_options(eph205_app PRIVATE /utf-8 /W4 /WX-)
endif()

# 运行时依赖：CEF 的 dll / pak / locales，以及本产品的页面。
# 都挂在目标上，按需复制。
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
  add_custom_command(TARGET eph205_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_BINARY_DIR_RELEASE}/${_file}" "$<TARGET_FILE_DIR:eph205_app>"
    VERBATIM)
endforeach()

add_custom_command(TARGET eph205_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:eph205_app>"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/src/apps/eph205/www"
          "$<TARGET_FILE_DIR:eph205_app>/www"
  COMMENT "复制 CEF 运行时与页面到输出目录"
  VERBATIM)
