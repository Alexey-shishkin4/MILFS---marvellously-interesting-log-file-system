set(BINARY ${CMAKE_PROJECT_NAME})
file(GLOB_RECURSE SOURCES LIST_DIRECTORIES true *.h *.cpp)
set(SOURCES ${SOURCES})

add_executable(${BINARY}_run ${SOURCES})

target_compile_options(${BINARY}_run PRIVATE -g -O0 -fsanitize=address)
target_link_options(${BINARY}_run PRIVATE -fsanitize=address)

add_library(${BINARY}_lib STATIC ${SOURCES})
