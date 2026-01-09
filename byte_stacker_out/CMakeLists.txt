project (byte_stacker_out LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_FILES
  "byte_stacker_out.cpp"
  "outlink.cpp"
  "../common/parser.cpp"
  "../common/trunklink.cpp"
)

set(HEADER_FILES
  "byte_stacker_out.h"
  "outlink.h"
  "../common/data.h"
  "../common/parser.h"
  "../common/trunklink.h"
)

add_executable (${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})

include_directories(${PROJECT_NAME} "../common")

