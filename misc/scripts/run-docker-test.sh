#!/bin/bash

if [ "$#" -lt 1 ]; then
  echo "Usage: <lgraph_src_dir> [build_type] [docker_image]"
  exit -1
fi

LGRAPH_SRC=$1
BUILD_TYPE=${2:-Debug}
DOCKER_IMAGE=${3:-mascucsc/archlinux-masc:latest}

: ${LGRAPH_HOST_PROCS:=$(nproc)}

DOCKER_LGRAPH_SRC='/root/projs/lgraph'
DOCKER_BUILD_DIR='/root/projs/build'

if [ ! -e ${LGRAPH_SRC}/CMakeLists.txt ]; then
  echo "BUILD ERROR: '${LGRAPH_SRC}' does not contain LGRAPH source code"
  exit -1
fi

# possibly add back -t command later
docker run  \
  -v $LGRAPH_SRC:$DOCKER_LGRAPH_SRC \
  -e LGRAPH_SRC=${DOCKER_LGRAPH_SRC} \
  -e LGRAPH_BUILD_DIR=${DOCKER_BUILD_DIR} \
  -e LGRAPH_BUILD_TYPE=${BUILD_TYPE} \
  -e LGRAPH_HOST_PROCS=${LGRAPH_HOST_PROCS} \
  ${DOCKER_IMAGE} /root/projs/lgraph/misc/scripts/build-and-run.sh

