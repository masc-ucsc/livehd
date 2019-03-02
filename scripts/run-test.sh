#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# Defaults for configuration variables used by script
: ${LGRAPH_SRC:=${HOME}/lgraph}
: ${LGRAPH_BUILD_MODE:=fastbuild}

if [ ! -e ${LGRAPH_SRC}/WORKSPACE ]; then
  echo "ERROR: '${LGRAPH_SRC}' does not contain LGRAPH source code"
  exit -1
fi

cd ${LGRAPH_SRC}

bazel test -c ${LGRAPH_BUILD_MODE} //...
if [ $? -ne 0 ]; then
  echo "run-test.sh: short test failed"
  exit 1
fi

if [ "$RUN_TYPE" == "long" ]; then
  # Not manual test
  bazel test -c ${LGRAPH_BUILD_MODE} --test_tag_filters "long1,long2,long3,long4,long5,long6,long7,long8" //...
  if [ $? -ne 0 ]; then
    echo "run-test.sh: long test failed"
    exit 1
  fi
elif [ "$RUN_TYPE" == "long1" ]; then
  # Not manual test
  bazel test -c ${LGRAPH_BUILD_MODE} --test_tag_filters "long1" //...
  if [ $? -ne 0 ]; then
    echo "run-test.sh: long1 test failed"
    exit 1
  fi
elif [ "$RUN_TYPE" == "long2" ]; then
  # Not manual test
  bazel test -c ${LGRAPH_BUILD_MODE} --test_tag_filters "long2" //...
  if [ $? -ne 0 ]; then
    echo "run-test.sh: long2 test failed"
    exit 1
  fi
elif [ "$RUN_TYPE" != "" ]; then
  echo "run-test.sh: unknown ${RUN_TYPE} run option"
  exit 1
fi

exit 0

