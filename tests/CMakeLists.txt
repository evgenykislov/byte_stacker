cmake_minimum_required(VERSION 3.18)

project(tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

set(SOURCES_FILES
  "close_connect.cpp"
  "fixture_direct_pipe.cpp"
  "set_connect.cpp"
)

set(HEADERS_FILES
  "fixture_direct_pipe.h"
)

find_package(GTest CONFIG REQUIRED)

add_executable(${PROJECT_NAME} ${HEADERS_FILES} ${SOURCES_FILES})

#target_include_directories(${PROJECT_NAME} PRIVATE "../common")

target_link_libraries(${PROJECT_NAME} GTest::gtest_main)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  # If the debug configuration pass the DEBUG define to the compiler 
  $<$<CONFIG:Debug>:_DEBUG>
)
