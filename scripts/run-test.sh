#!/bin/bash

# Defaults for configuration variables used by script
: ${LGRAPH_SRC:=${HOME}/lgraph}
: ${LGRAPH_BUILD_MODE:=fastbuild}

if [ ! -e ${LGRAPH_SRC}/WORKSPACE ]; then
  echo "ERROR: '${LGRAPH_SRC}' does not contain LGRAPH source code"
  exit -1
fi

cd ${LGRAPH_SRC}

bazer test -c ${LGRAPH_BUILD_MODE} //...

if [ $? -eq 0 ]; then
  exit 0
else
  exit $?
fi

