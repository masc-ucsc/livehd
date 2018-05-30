#!/bin/bash

# Default for LGRAPH source code location
: LGRAPH_SRC=${LGRAPH_SRC:=${HOME}/projs/esesc}

if [ ! -e ${LGRAPH_SRC}/CMakeLists.txt ]; then
  echo "BUILD ERROR: '${LGRAPH_SRC}' does not contain LGRAPH source code"
  exit -1
fi

if ! ${LGRAPH_SRC}/misc/scripts/build.sh
then
  echo "LGRAPH build error"
  exit -1
fi

${LGRAPH_SRC}/misc/scripts/run-test.sh

