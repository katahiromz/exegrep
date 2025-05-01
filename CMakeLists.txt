# CMakeLists.txt --- CMake project settings
##############################################################################

# CMake minimum version
cmake_minimum_required(VERSION 3.10)

# project name and languages
project(exegrep CXX)

# UTF-8 source code
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

##############################################################################

# exegrep.exe
add_executable(exegrep exegrep.cpp)
target_link_libraries(exegrep shlwapi)

##############################################################################
