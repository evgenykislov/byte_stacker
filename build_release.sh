#!/bin/bash

rm -rfd build_release

cmake -DCMAKE_BUILD_TYPE=Release -B build_release
cmake --build build_release
