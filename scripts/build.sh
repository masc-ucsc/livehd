#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# Defaults for configuration variables used by script
: ${LGRAPH_SRC:=${HOME}/lgraph}
: ${LGRAPH_BUILD_MODE:=fastbuild}
: ${LGRAPH_COMPILER:=g++}

if [ ! -e ${LGRAPH_SRC}/WORKSPACE ]; then
  echo "ERROR: '${LGRAPH_SRC}' does not contain LGRAPH source code"
  exit -1
fi

echo "build.sh: Building....."
cd ${LGRAPH_SRC}

if [ $LGRAPH_COMPILER == "g++" ]; then
  CXX=g++ CC=gcc bazel build -c ${LGRAPH_BUILD_MODE} //...
  if [ $? -eq 0 ]; then
    echo "build.sh: g++ build completed correctly"
    exit 0
  fi
elif [ $LGRAPH_COMPILER == "clang++" ]; then
  CXX=clang++ CC=clang bazel build -c ${LGRAPH_BUILD_MODE} //...
  if [ $? -eq 0 ]; then
    echo "build.sh: clang++ build completed correctly"
    exit 0
  fi
else
  echo "build.sh: ERROR, unrecognized $LGRAPH_COMPILER option"
  exit -4
fi

echo "build.sh: Build had an exit code condition"
exit 2
