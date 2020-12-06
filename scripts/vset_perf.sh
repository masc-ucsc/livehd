#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

bazel build -c opt //mmap_lib:all
#echo -e "\nStandard Set Performance"
#time ./bazel-bin/mmap_lib/bench_set_use std > out.txt
echo -e "\nmmap map Performance"
time ./bazel-bin/mmap_lib/bench_set_use mmap > out.txt
echo -e "\nmmap vset Performance"
time ./bazel-bin/mmap_lib/bench_set_use vset > out.txt
rm out.txt

