# CMakeList.txt : CMake project for byte_stacker_in, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (byte_stacker_in "byte_stacker_in.cpp" "byte_stacker_in.h")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET byte_stacker_in PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
