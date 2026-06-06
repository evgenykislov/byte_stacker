#!/bin/bash

# Generate report
rm -rfd build_coverage/coverage
mkdir build_coverage/coverage

gcovr --html build_coverage/coverage/index.html --html-details --gcov-ignore-parse-errors
