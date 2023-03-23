cmake_minimum_required(VERSION 3.16)
project(ipc_test)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GFLAGS REQUIRED gflags)

add_subdirectory(cpp-ipc)

add_executable(ipc_test main.cpp)
target_include_directories(ipc_test PUBLIC cpp-ipc/include ${GFLAGS_INCLUDE_DIRS} ThreadPool)
target_link_libraries(ipc_test ${GFLAGS_LIBRARIES} ipc)
