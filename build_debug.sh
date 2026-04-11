#!/bin/bash

rm -rfd build_debug

cmake -DCMAKE_BUILD_TYPE=Debug -B build_debug
cmake --build build_debug
