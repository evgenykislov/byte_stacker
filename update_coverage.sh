#!/bin/bash

# Generate report
rm -rfd build_coverage/coverage
mkdir build_coverage/coverage

gcovr --html build_coverage/coverage/index.html --html-details --exclude-throw-branches --exclude-unreachable-branches --gcov-ignore-parse-errors
