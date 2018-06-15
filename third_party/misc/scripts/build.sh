#!/bin/bash

# Defaults for configuration variables used by script
: ${LGRAPH_SRC:=${HOME}/lgraph}
: ${LGRAPH_BUILD_DIR:=${HOME}/build}
: ${LGRAPH_BUILD_TYPE:=Debug}
: ${LGRAPH_HOST_PROCS:=$(nproc)}

BUILD_DIR=${LGRAPH_BUILD_DIR}/${LGRAPH_BUILD_TYPE,,}

if [ ! -e ${LGRAPH_SRC}/CMakeLists.txt ]; then
  echo "BUILD ERROR: '${LGRAPH_SRC}' does not contain LGraph source code"
  exit -1
fi

echo "build.sh: Building....."

echo "build.sh: Getting sub directories..."
cd ${LGRAPH_SRC}
git submodule update --init --recursive

echo "build.sh: compiling..."
if [ -d ${BUILD_DIR} ]; then
  if [ -e ${BUILD_DIR}/Makefile ]; then
    cd ${BUILD_DIR}
    make -j${LGRAPH_HOST_PROCS}
    echo "build.sh: cmake is broken, re-run make without -j"
    make
  else
    echo "BUILD ERROR: '${BUILD_DIR}' already exists"
    exit -2
  fi
else
  mkdir -p ${BUILD_DIR}
  cd ${BUILD_DIR}
  cmake -DCMAKE_BUILD_TYPE=${LGRAPH_BUILD_TYPE} ${LGRAPH_SRC}
  make -j${LGRAPH_HOST_PROCS}
  echo "build.sh: cmake is broken, re-run make without -j"
  make
fi

