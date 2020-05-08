#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

if [ "$#" -lt 1 ]; then
  echo "Usage: <run-docker-test.sh> [build_mode] [docker_image]"
  exit 1
fi

LIVEHD_SRC=$1
LIVEHD_BUILD_MODE=${2:fastbuild} # opt dbg fastbuild
DOCKER_IMAGE=${3:-mascucsc/archlinux-masc:latest}

DOCKER_LIVEHD_SRC=${4:-/root/livehd}
LIVEHD_COMPILER=${5:g++}

COVERAGE_RUN=$6

RUN_TYPE=$7

if [ "${TRAVIS_EVENT_TYPE}" == "cron" ]; then
  RUN_TYPE=long
fi

if [ ! -e ${LIVEHD_SRC}/WORKSPACE ]; then
  echo "BUILD ERROR: '${LIVEHD_SRC}' does not contain LIVEHD source code"
  exit 2
fi

# possibly add back -t command later
docker run \
  -v $LIVEHD_SRC:$DOCKER_LIVEHD_SRC \
  -e LIVEHD_SRC=${DOCKER_LIVEHD_SRC} \
  -e LIVEHD_BUILD_MODE=${LIVEHD_BUILD_MODE} \
  -e LIVEHD_COMPILER=${LIVEHD_COMPILER} \
  -e COVERAGE_RUN=${COVERAGE_RUN} \
  -e RUN_TYPE=${RUN_TYPE} \
  ${DOCKER_IMAGE} ${DOCKER_LIVEHD_SRC}/scripts/build-and-run.sh

