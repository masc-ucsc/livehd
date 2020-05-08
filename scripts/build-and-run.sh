#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# Default for LIVEHD source code location
if [[ -v LIVEHD_SRC ]]; then
  echo "LIVEHD_SRC defined as "$LIVEHD_SRC
else
  echo "LIVEHD_SRC undefined"
  export LIVEHD_SRC=${HOME}/livehd
fi

if [ ! -e ${LIVEHD_SRC}/WORKSPACE ]; then
  echo "BUILD ERROR: '${LIVEHD_SRC}' does not contain LIVEHD source code"
  exit 1
fi

if [[ $(which lcov) && ${COVERAGE_RUN} == "coverage" ]] ; then
  cd $LIVEHD_SRC
  ./scripts/gencoverage.sh
  ./scripts/gencoveralls.sh
  if [ ! -f ./cov/coverage.info ]; then
    echo "build-and-run.sh: coverage error"
    exit 1
  fi
else
  ${LIVEHD_SRC}/scripts/build.sh
  if [ $? -ne 0 ]; then
    echo "build-and-run.sh: build error"
    exit 1
  fi

  ${LIVEHD_SRC}/scripts/run-test.sh
  if [ $? -ne 0 ]; then
    echo "build-and-run.sh: test error"
    exit 1
  fi
fi

exit 0

