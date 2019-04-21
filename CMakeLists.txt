cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(tunnel)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(Boost_USE_MULTITHREAD ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wpedantic")

find_package(GTest REQUIRED)
find_package(glog 0.4.0 REQUIRED)
find_package(gflags REQUIRED)
find_package(Boost 1.42.0 REQUIRED COMPONENTS system thread)

enable_testing()

if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
  link_directories(${Boost_LIBRARY_DIRS})
endif(Boost_FOUND)

set(Boost_LIBS ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY})

set(SRC_DIR "src")
set(TEST_DIR "tests")

add_library(tunnelserver "${SRC_DIR}/server.cpp")
target_include_directories(tunnelserver PUBLIC ./)
target_link_libraries(tunnelserver ${Boost_LIBS} glog::glog)

add_executable(main "${SRC_DIR}/main.cpp")
target_link_libraries(main tunnelserver)

add_executable(tests "${TEST_DIR}/test.cpp")
target_include_directories(tests PUBLIC)
target_link_libraries(tests glog::glog gtest tunnelserver)

add_test(tests tests)
