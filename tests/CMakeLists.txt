cmake_minimum_required(VERSION 3.25)

project(tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

set(SOURCES_FILES
  "block_resources_run.cpp"
  "close_connect.cpp"
  "fixture_direct_pipe.cpp"
  "set_connect.cpp"
  "single_byte_transfer.cpp"
)

set(HEADERS_FILES
  "fixture_direct_pipe.h"
)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(GTest REQUIRED)

add_executable(${PROJECT_NAME} ${HEADERS_FILES} ${SOURCES_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE GTest::gtest_main Boost::system Boost::filesystem)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  # If the debug configuration pass the DEBUG define to the compiler 
  $<$<CONFIG:Debug>:_DEBUG>
)
