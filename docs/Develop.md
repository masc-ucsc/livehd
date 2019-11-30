
This document provides links and points to the main information available to
potential LGraph developers.

As developer, you should become familiar with the [Usage](./Usage.md) guide. It
provides an intro to the basic commands and how to run LGraph.

LGraph uses bazel as build system, you may want to go over the
[Bazel](./Bazel.md) documentation to see basic bazel commands, and how to
run/compile/debug basic examples.

If you are going to create a new pass and/or inou project, the
[CreateInouPass](./CreateInouPass.md) provides an introduction on how to create
a pass and integrate it with lgshell.

[GitHub-use](GitHub-use.md) explains how to operated with git and LGraph, how to
handle branches, merges and the lack of submodules.

# Use clang and gcc for the builds

The regression system builds both for gcc and clang. To force clang build in
the command line (better warnings, faster compile, but a bit worse execution
time in some cases).

     CXX=clang++ CC=clang bazel build -c dbg //...

# Perf in lgbench

Use lgbench to gather statistics in your code block. It also allows to run perf record
for the code section (from lgbench construction to destruction). To enable perf record
set LGBENCH_PERF environment variable

     export LGBENCH_PERF=1

# GDB usage

For most tests, you can debug with

     gdb ./bazel-bin/main/lgshell

Sometimes the failure is yosys/lgraph bridge. In this case, you need to gdb yosys directly

     gdb `which gdb`
     (gdb) r -m ./bazel-bin/inou/yosys/liblgraph_yosys.so 

