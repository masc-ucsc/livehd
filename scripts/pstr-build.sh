#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

CXX=clang++ CC=clang bazel test -c opt //mmap_lib:all
#./bazel-bin/mmap_lib/bench_pstr_use
#./bazel-bin/mmap_lib/bench_pstr_use

exit 2

