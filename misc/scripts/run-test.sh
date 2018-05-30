#!/bin/bash

# Defaults for configuration variables used by script
: ${LGRAPH_SRC:=${HOME}/projs/esesc}
: ${LGRAPH_BUILD_DIR:=${HOME}/build}
: ${LGRAPH_BUILD_TYPE:=Debug}
: ${LGRAPH_ENABLE_LIVE:=0}

BUILD_DIR=${LGRAPH_BUILD_DIR}/${LGRAPH_BUILD_TYPE,,}

if [ ! -e ${LGRAPH_SRC}/CMakeLists.txt ]; then
  echo "ERROR: '${LGRAPH_SRC}' does not contain LGRAPH source code"
  exit -1
fi

if [ ! -e ${BUILD_DIR}/inou/json/lgjson ]; then
  echo "ERROR: '${BUILD_DIR}' does not contain LGRAPH binary"
  exit -2
fi

cd ${BUILD_DIR}

make test
if [ $? -eq 0 ]; then
  exit 0
else
  exit $?
fi

