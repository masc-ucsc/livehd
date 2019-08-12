#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

if [ "$#" -lt 1 ]; then
  echo "Usage: <lgraph_src_dir> [build_mode] [docker_image]"
  exit -1
fi

LGRAPH_SRC=$1
LGRAPH_BUILD_MODE=${2:fastbuild} # opt dbg fastbuild
DOCKER_IMAGE=${3:-mascucsc/archlinux-masc:latest}

DOCKER_LGRAPH_SRC=${4:-/root/lgraph}
#DOCKER_LGRAPH_SRC='/root/lgraph'
LGRAPH_COMPILER=${5:g++}

COVERAGE_RUN=$6

RUN_TYPE=$7

if [ "${TRAVIS_EVENT_TYPE}" == "cron" ]; then
  RUN_TYPE=long
fi

if [ ! -e ${LGRAPH_SRC}/WORKSPACE ]; then
  echo "BUILD ERROR: '${LGRAPH_SRC}' does not contain LGRAPH source code"
  exit -1
fi

# possibly add back -t command later
export LGRAPH_SRC
export LGRAPH_BUILD_MODE
export LGRAPH_COMPILER
export COVERAGE_RUN
export RUN_TYPE

echo scripts/build-and-run.sh

