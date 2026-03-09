
cmake_minimum_required (VERSION 3.25)

project (file_generator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


set(SOURCE_FILES
  "file_generator.cpp"
)

set(HEADER_FILES
)

add_executable (${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})
