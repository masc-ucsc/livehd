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

bazel test -c ${LIVEHD_BUILD_MODE} //...
if [ $? -ne 0 ]; then
  echo "run-test.sh: short test failed"
  exit 1
fi

if [ "$RUN_TYPE" == "long" ]; then
  # Not manual test
  bazel test -c ${LIVEHD_BUILD_MODE} --test_tag_filters "long1,long2,long3,long4,long5,long6,long7,long8" //...
  if [ $? -ne 0 ]; then
    echo "run-test.sh: long test failed"
    exit 1
  fi
elif [ "$RUN_TYPE" == "long1" ]; then
  # Not manual test
  bazel test -c ${LIVEHD_BUILD_MODE} --test_tag_filters "long1" //...
  if [ $? -ne 0 ]; then
    echo "run-test.sh: long1 test failed"
    exit 1
  fi
elif [ "$RUN_TYPE" == "long2" ]; then
  # Not manual test
  bazel test -c ${LIVEHD_BUILD_MODE} --test_tag_filters "long2" //...
  if [ $? -ne 0 ]; then
    echo "run-test.sh: long2 test failed"
    exit 1
  fi
elif [ "$RUN_TYPE" != "" ]; then
  echo "run-test.sh: unknown ${RUN_TYPE} run option"
  exit 1
fi

exit 0

