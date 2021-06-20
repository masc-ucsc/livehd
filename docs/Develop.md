# Developer documentation
This document provides links and points to the main information available to potential LGraph developers.

As a developer, you should become familiar with the following documents:
1. [Usage](./Usage.md), which describes how to build and run the LGraph shell.
2. [Concepts](./Concepts.md), which contains information about LGraph and how to traverse it.
3. [Bazel](./Bazel.md), which explains in detail how to use the [Bazel](https://bazel.build) build system.
4. [GitHub](./GitHub-use.md), which explains how to use Git/GitHub with LGraph, how to
handle branches, merges and the lack of submodules.

If you are going to create a new pass and/or inou, the
[CreateInouPass](./CreateInouPass.md) provides an introduction on how to create
an example pass and integrate it with lgshell.

Outlined below are various ways to build, test, and debug LGraph.

## Using clang when building

The regression system builds for both gcc and clang. To force a clang build, set the following environment variables before building:
```
$ CXX=clang++ CC=clang bazel build -c dbg //...
```
## Perf in lgbench

Use lgbench to gather statistics in your code block. It also allows to run perf record
for the code section (from lgbench construction to destruction). To enable perf record
set LGBENCH_PERF environment variable
```
$ export LGBENCH_PERF=1
```
## GDB/LLDB usage

For most tests, you can debug with
```
$ gdb ./bazel-bin/main/lgshell
```
or
```
$ lldb ./bazel-bin/main/lgshell
```
Note that breakpoint locations may not resolve until lgshell is started and the relevant LGraph libraries are loaded.

## Address Sanitizer

LiveHD has the option to run it with address sanitizer to detect memory leaks.

```
$ bazel build -c dbg --config asan //...
```

## Thread Sanitizer

To debug with concurrent data race.

```
$ bazel build -c dbg --config tsan //...
```



## Debugging a broken Docker image

The travis/azure regressions run several docker images. To debug the issue, run the same as the failing
docker image. c++ OPT with archlinux-masc image

1. Create some directory to share data in/out the docker run (to avoid
   mistakes/issues, I would not share home directory unless you have done it
   several times before)

```
$ mkdir $HOME/docker
```

2. Run the docker image (in some masc docker images you can change the user to not being root)

```
$ docker run --rm --cap-add SYS_ADMIN -it  -e LOCAL_USER_ID=$(id -u $USER) -v ${HOME}/docker:/home/user mascucsc/archlinux-masc                                                                                                                         
# Once inside docker image. Create local "user" at /home/user with your userid
$ /usr/local/bin/entrypoint.sh
```

3. If the docker image did not have the livehd repo, clone it
```
$ git clone https://github.com/masc-ucsc/livehd.git
```

4. Build with the failing options and debug
```
$ CXX=g++ CC=gcc bazel build -c opt //...
```

A docker distro that specially fails (address randomizing and muslc vs libc) is alpine. The command line to debug it:
```
$ docker run --rm --cap-add SYS_ADMIN -it -e LOCAL_USER_ID=$(id -u $USER) -v $HOME:/home/user -v/local/scrap:/local/scrap mascucsc/alpine-masc
```

