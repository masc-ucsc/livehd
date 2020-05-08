#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

if [ "$#" -lt 1 ]; then
  echo "Usage: <livehd_src_dir> [build_mode] [docker_image]"
  exit 1
fi

LIVEHD_SRC=$1
LIVEHD_BUILD_MODE=${2:fastbuild} # opt dbg fastbuild
DOCKER_IMAGE=${3:-mascucsc/archlinux-masc:latest}

DOCKER_LIVEHD_SRC=${4:-/root/livehd}
#DOCKER_LIVEHD_SRC='/root/livehd'
LIVEHD_COMPILER=${5:g++}

COVERAGE_RUN=$6

RUN_TYPE=$7

if [ "${TRAVIS_EVENT_TYPE}" == "cron" ]; then
  RUN_TYPE=long
fi

if [ ! -e ${LIVEHD_SRC}/WORKSPACE ]; then
  echo "BUILD ERROR: '${LIVEHD_SRC}' does not contain LIVEHD source code"
  exit 1
fi

# possibly add back -t command later
export LIVEHD_SRC
export LIVEHD_BUILD_MODE
export LIVEHD_COMPILER
export COVERAGE_RUN
export RUN_TYPE

./scripts/build-and-run.sh

exit 0
