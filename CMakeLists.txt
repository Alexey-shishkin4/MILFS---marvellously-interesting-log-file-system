cmake_minimum_required(VERSION 3.27)
get_filename_component(PROJECT_NAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
project(${PROJECT_NAME})


enable_testing()

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fsanitize=address")

include_directories(src)

add_subdirectory(src)
add_subdirectory(test)
