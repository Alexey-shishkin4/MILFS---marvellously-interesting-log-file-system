cmake_minimum_required(VERSION 3.16)

project(milfs LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# FUSE3 first
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3)

include_directories(src)

add_subdirectory(src)
add_subdirectory(test)

# FUSE target
add_executable(milfs_fuse 
    src/fuse_main.cpp 
    src/fs_fuse.cpp
)
target_link_libraries(milfs_fuse 
    PRIVATE milfs_lib 
    PkgConfig::FUSE3
)
target_include_directories(milfs_fuse PRIVATE src/headers)
