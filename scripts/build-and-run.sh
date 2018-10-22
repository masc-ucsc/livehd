#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# Default for LGRAPH source code location
: LGRAPH_SRC=${LGRAPH_SRC:=${HOME}/lgraph}

if [ ! -e ${LGRAPH_SRC}/WORKSPACE ]; then
  echo "BUILD ERROR: '${LGRAPH_SRC}' does not contain LGRAPH source code"
  exit -1
fi

${LGRAPH_SRC}/scripts/build.sh
if [ $? -ne 0 ]; then
  echo "build-and-run.sh: build error"
  exit -1
fi

if [[ $(which lcov) && $LGRAPH_BUILD_MODE == "fastbuild" ]]; then
  cd $LGRAPH_SRC
  ./scripts/gencoverage.sh
  ./scripts/gencoveralls.sh
  if [ ! -f ./cov/coverage.info ]; then
    echo "build-and-run.sh: coverage error"
    exit -1
  fi
else
  ${LGRAPH_SRC}/scripts/run-test.sh
  if [ $? -ne 0 ]; then
    echo "build-and-run.sh: test error"
    exit -1
  fi
fi

exit 0

