cmake_minimum_required(VERSION 3.16)

project(milfs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BINARY ${CMAKE_PROJECT_NAME})

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/headers/*.h
)

file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

# Отдельно main.cpp
set(MAIN_SOURCE ${CMAKE_SOURCE_DIR}/src/main.cpp)

# Библиотека с логикой проекта
add_library(${BINARY}_lib STATIC
    ${LIB_SOURCES}
    ${HEADERS}
)

target_include_directories(${BINARY}_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_options(${BINARY}_lib PRIVATE
    -g
    -O0
    -fsanitize=address
)

target_link_options(${BINARY}_lib PRIVATE
    -fsanitize=address
)

# Исполняемый файл
add_executable(${BINARY}_run
    ${MAIN_SOURCE}
)

target_link_libraries(${BINARY}_run PRIVATE
    ${BINARY}_lib
)

target_compile_options(${BINARY}_run PRIVATE
    -g
    -O0
    -fsanitize=address
)

target_link_options(${BINARY}_run PRIVATE
    -fsanitize=address
)

