cmake_minimum_required(VERSION 3.21)
project(my_ws_server_test C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MONGOOSE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mongoose-7.23")

add_library(mongoose STATIC "${MONGOOSE_DIR}/mongoose.c")
target_include_directories(mongoose PUBLIC "${MONGOOSE_DIR}")

add_executable(my_ws_server_test main.cpp)
target_link_libraries(my_ws_server_test PRIVATE mongoose)

if(MSVC)
  target_compile_options(my_ws_server_test PRIVATE /utf-8)
endif()