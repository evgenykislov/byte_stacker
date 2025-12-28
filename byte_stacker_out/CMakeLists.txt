project (byte_stacker_out LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_FILES
  "byte_stacker_out.cpp"
  "outlink.cpp"
  "../common/trunklink.cpp"
)

set(HEADER_FILES
  "byte_stacker_out.h"
  "outlink.h"
  "../common/data.h"
  "../common/trunklink.h"
)

add_executable (${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})
