
cmake_minimum_required (VERSION 3.25)

project (udp_processor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


set(SOURCE_FILES
  "udp_processor.cpp"
  "processor.cpp"
  "../../common/parser.cpp"
)

set(HEADER_FILES
  "processor.h"
  "processor_sender.h"
  "processor_skip.h"
  "../../common/parser.h"
)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

add_executable (${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})

include_directories(${PROJECT_NAME} "../../common")

target_link_libraries(${PROJECT_NAME} PRIVATE Boost::system)
