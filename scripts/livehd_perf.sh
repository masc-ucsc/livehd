#!/bin/bash

git clean -fdx
bazel build -c opt //...

export NOCHECK=1
for a in $(bazel query "tests(//...)" 2>/dev/null | grep ^\/ | sed -e 's/^\//.\/bazel-bin/g' | sed -e 's/:/\//g')
do
  PFILE=$(echo $(echo ${a} | sed -e 's/\//_/g' | sed -e 's/bazel-bin_//g'))
  perf stat -o ${PFILE}.perf_stat ${a} >${PFILE}.log 2>${PFILE}.err
done

