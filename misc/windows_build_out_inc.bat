# Compilation
pushd ..\byte_stacker_out

rem rd /S /Q build-win-release
rem cmake -D CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -B build-win-release
cmake --build build-win-release --config Release

popd
