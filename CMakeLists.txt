# CMakeLists.txt --- CMake project settings
##############################################################################

# CMake minimum version
cmake_minimum_required(VERSION 3.10)

# project name, version, and languages
project(exegrep VERSION 1.1 LANGUAGES CXX)

# UTF-8 source code
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# Use Boyer-Moore algorithm?
option(USE_BOYER_MOORE "Use Boyer-Moore algorithm" ON)
if(USE_BOYER_MOORE)
    add_definitions(-DUSE_BOYER_MOORE)
endif()
message(STATUS "USE_BOYER_MOORE = ${USE_BOYER_MOORE}")

##############################################################################

# exegrep.exe
add_executable(exegrep exegrep.cpp)
target_link_libraries(exegrep shlwapi)

##############################################################################
