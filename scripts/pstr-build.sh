#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

bazel build -c dbg //mmap_lib:all
#./bazel-bin/mmap_lib/bench_pstr_use
./bazel-bin/mmap_lib/mmap_str_test

exit 2

