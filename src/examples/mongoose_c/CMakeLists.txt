# ============================================================
# mongoose 纯 C 教学 demo(单文件 main.c,零业务零依赖)
# 目标: mongoose_c_demo,监听 http://127.0.0.1:8766
# 页面内嵌在 main.c 里,不依赖 www/ 目录,整目录可单独拷走学习
# ============================================================
set(MONGOOSE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/mongoose-7.23")

# mongoose 静态库通常已由 src/examples/mongoose_cpp 定义;加上守卫,
# 本目录单独拷出去配置时也能自给自足
if(NOT TARGET mongoose)
  add_library(mongoose STATIC "${MONGOOSE_DIR}/mongoose.c")
  target_include_directories(mongoose PUBLIC "${MONGOOSE_DIR}")
endif()

set(TARGET_NAME "mongoose_c_demo")
add_executable(${TARGET_NAME}
  main.c
  )

target_link_libraries(${TARGET_NAME} PRIVATE mongoose)
