cmake_minimum_required(VERSION 3.16)

project(milfs LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(src)

add_subdirectory(src)
add_subdirectory(test)
