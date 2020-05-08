#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# Defaults for configuration variables used by script
: ${LIVEHD_SRC:=${HOME}/livehd}
: ${LIVEHD_BUILD_MODE:=fastbuild}
: ${LIVEHD_COMPILER:=g++}

if [ ! -e ${LIVEHD_SRC}/WORKSPACE ]; then
  echo "ERROR: '${LIVEHD_SRC}' does not contain LIVEHD source code"
  exit 1
fi

echo "build.sh: Building....."
cd ${LIVEHD_SRC}

if [ $LIVEHD_COMPILER == "g++" ]; then
  echo "build.sh: g++"
  export CXX=g++
  export CC=gcc
elif [ $LIVEHD_COMPILER == "clang++" ]; then
  echo "build.sh: clang++"
  export CXX=clang++
  export CC=clang
elif [ $LIVEHD_COMPILER == "clang++-8" ]; then
  echo "build.sh: clang++-8"
  export CXX=clang++-8
  export CC=clang-8
elif [ $LIVEHD_COMPILER == "g++-8" ]; then
  echo "build.sh: g++-8"
  export CXX=g++-8
  export CC=gcc-8
else
  echo "build.sh: ERROR, unrecognized $LIVEHD_COMPILER option"
  exit 8
fi

bazel build -c ${LIVEHD_BUILD_MODE} //...
if [ $? -eq 0 ]; then
  echo "build.sh: clang++ build completed correctly"
  exit 0
fi

echo "build.sh: Build had an exit code condition"
exit 2

