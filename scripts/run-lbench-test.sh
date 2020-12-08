#!/bin/bash

: ${LIVEHD_SRC:=${PWD}/..}
: ${LIVEHD_BUILD_MODE:=opt}

if [ ! -e ${LIVEHD_SRC}/WORKSPACE ]; then
  echo "ERROR: '${LIVEHD_SRC}' does not contain LIVEHD source code"
  exit 1
fi

cd ${LIVEHD_SRC}

export LGBENCH_PERF=1
export CXX=g++
export CC=gcc

# Run only tests that create lbench.trace
# When a new trace is added, it should be added to TEST_LIST.
#TEST_LIST='core inou/liveparse inou/firrtl lemu main mmap_lib pass/compiler pass/mockturtle pass/sample pass/lnast_fromlg task'
# Here we exclude core and main tests because the tests take long time
TEST_LIST='inou/liveparse inou/firrtl lemu mmap_lib pass/compiler pass/mockturtle pass/sample pass/lnast_fromlg task'
for TEST in $TEST_LIST
do
  echo $TEST
  for SUB_TEST in `bazel query "tests(//$TEST:all)" 2>/dev/null`
  do
    echo $SUB_TEST
    bazel run --nocache_test_results -c $LIVEHD_BUILD_MODE $SUB_TEST
  done
done
