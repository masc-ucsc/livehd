#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# Defaults for configuration variables used by script
: ${LIVEHD_SRC:=${HOME}/livehd}
: ${LIVEHD_BUILD_MODE:=fastbuild}

if [ ! -e ${LIVEHD_SRC}/WORKSPACE ]; then
  echo "ERROR: '${LIVEHD_SRC}' does not contain LIVEHD source code"
  exit 1
fi

cd ${LIVEHD_SRC}

if [ $LIVEHD_COMPILER == "g++" ]; then
  echo "test.sh: g++"
  export CXX=g++
  export CC=gcc
elif [ $LIVEHD_COMPILER == "clang++" ]; then
  echo "test.sh: clang++"
  export CXX=clang++
  export CC=clang
elif [ $LIVEHD_COMPILER == "clang++-8" ]; then
  echo "test.sh: clang++-8"
  export CXX=clang++-8
  export CC=clang-8
elif [ $LIVEHD_COMPILER == "g++-8" ]; then
  echo "test.sh: g++-8"
  export CXX=g++-8
  export CC=gcc-8
else
  echo "test.sh: ERROR, unrecognized $LIVEHD_COMPILER option"
  exit 8
fi

# Legacy long selections now run the same complete suite.
if [[ -n "$RUN_TYPE" && "$RUN_TYPE" != "long" && "$RUN_TYPE" != "long1" && "$RUN_TYPE" != "long2" ]]; then
  echo "run-test.sh: unknown ${RUN_TYPE} run option"
  exit 1
fi

CXX=${CXX} CC=${CC} bazel test -c ${LIVEHD_BUILD_MODE} //...
if [ $? -ne 0 ]; then
  echo "run-test.sh: test failed"
  exit 1
fi

exit 0

