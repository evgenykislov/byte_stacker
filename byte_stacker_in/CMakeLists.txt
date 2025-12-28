# CMakeList.txt : CMake project for byte_stacker_in, include source and define
# project specific logic here.
#

project (byte_stacker_in LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_FILES
  "byte_stacker_in.cpp"
  "inlink.cpp"
  "../common/trunklink.cpp"
)

set(HEADER_FILES
  "byte_stacker_in.h"
  "inlink.h"
  "../common/data.h"
  "../common/trunklink.h"
)

add_executable (${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET byte_stacker_in PROPERTY CXX_STANDARD 20)
endif()
