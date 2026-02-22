#!/bin/bash

rm -rfd build_coverage

cmake -DCMAKE_BUILD_TYPE=Debug -DGEN_COVERAGE=ON -B build_coverage
cmake --build build_coverage

# run code tests
pushd build_coverage/bin
./tests_code
popd

# run script tests
export BIN_PATH=$(pwd)/build_coverage/bin
bash-tester.sh ./tests_bash

# Generate report
rm -rfd build_coverage/coverage
mkdir build_coverage/coverage

gcovr --html build_coverage/coverage/index.html --html-details
