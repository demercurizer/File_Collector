cmake_minimum_required(VERSION 3.10)
project(FileCollectorProject LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_SOURCE_DIR} ${GTEST_INCLUDE_DIRS})

find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

add_library(FileCollector STATIC FileCollector.cpp)

enable_testing()

add_executable(FileCollectorTests tests/test_filecollector.cpp)

target_link_libraries(FileCollectorTests FileCollector
                                    GTest::GTest GTest::Main Threads::Threads)

add_test(NAME FileCollectorTests COMMAND FileCollectorTests)
