# ============================================================
# 单元测试
#
# googletest 1.17.0 已 vendored 在 3rdparty/googletest-1.17.0（只留源码）。
# 构建与运行：
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# 目录与源码镜像：framework/ 测通讯层，modules/ 测业务模块，common/ 测公共设施。
# 每个被测单元一个可执行文件，命名 test_<单元>，用 add_test 注册。
# ============================================================
set(GOOGLETEST_DIR "${CMAKE_SOURCE_DIR}/3rdparty/googletest-1.17.0")

# 只要库本体：自带的测试、示例、安装规则一律关掉，否则 configure
# 会去找已删掉的 test/、samples/ 目录
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_build_tests OFF CACHE BOOL "" FORCE)
set(gtest_build_samples OFF CACHE BOOL "" FORCE)
set(gmock_build_tests OFF CACHE BOOL "" FORCE)
add_subdirectory("${GOOGLETEST_DIR}" googletest_build EXCLUDE_FROM_ALL)

# ------------------------------------------------------------
# 通讯层：帧编解码（24 字节头 + data/meta/records 分块）
# ------------------------------------------------------------
add_executable(test_frame framework/frame_test.cpp)
target_link_libraries(test_frame PRIVATE v1_base gtest_main)

if(MSVC)
  target_compile_options(test_frame PRIVATE /utf-8 /W4 /WX-)
endif()

add_test(NAME frame COMMAND test_frame)

# ------------------------------------------------------------
# 通讯层：环形缓冲（按整帧写入、按整帧淘汰、溢出统计）
# ------------------------------------------------------------
add_executable(test_ring framework/stream_ring_test.cpp)
target_link_libraries(test_ring PRIVATE v1_base gtest_main)

if(MSVC)
  target_compile_options(test_ring PRIVATE /utf-8 /W4 /WX-)
endif()

add_test(NAME ring COMMAND test_ring)

# ------------------------------------------------------------
# 通讯层：每个流的缓冲管理（写入口、序号分配、state 合并）
# ------------------------------------------------------------
add_executable(test_buffers framework/stream_buffers_test.cpp)
target_link_libraries(test_buffers PRIVATE v1_base gtest_main)

if(MSVC)
  target_compile_options(test_buffers PRIVATE /utf-8 /W4 /WX-)
endif()

add_test(NAME buffers COMMAND test_buffers)

# ------------------------------------------------------------
# 公共设施：日志门面（级别、中文、落盘）
# ------------------------------------------------------------
add_executable(test_log common/log_test.cpp)
target_link_libraries(test_log PRIVATE app_log gtest_main)

if(MSVC)
  target_compile_options(test_log PRIVATE /utf-8 /W4 /WX-)
endif()

add_test(NAME log COMMAND test_log)

# ------------------------------------------------------------
# 集成测试：起真服务端 + 独立实现的客户端（cpp-httplib）
#
# cpp-httplib 只给这一层用，不要链进 v1_base 或产品目标。
# 它走异常，所以单独开 /EHsc；其余代码仍按仓库约定用 err 返回。
# 单头 862 KB，一个测试文件只 include 一次，别散到多个 TU。
# ------------------------------------------------------------
set(HTTPLIB_DIR "${CMAKE_SOURCE_DIR}/3rdparty/cpp-httplib-0.66.0")

add_executable(test_server_smoke integration/server_smoke_test.cpp)
target_link_libraries(test_server_smoke PRIVATE v1_base gtest_main)
# SYSTEM：第三方头的告警不参与我们自己的 /W4
target_include_directories(test_server_smoke SYSTEM PRIVATE "${HTTPLIB_DIR}")

if(MSVC)
  # /wd4127：httplib 里有一处常量条件告警，压掉以免淹没自己的输出
  target_compile_options(test_server_smoke PRIVATE /utf-8 /W4 /WX- /EHsc /wd4127)
endif()

add_test(NAME server_smoke COMMAND test_server_smoke)

# ------------------------------------------------------------
# 业务模块：串口协议解析、判定规则等（按需新增到 modules/）
# ------------------------------------------------------------
