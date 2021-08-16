
# Creating a Pass

This document provides some minimal suggestion on how to build a new LiveHD pass.


Most LiveHD passes reside inside `inou` or `pass`. The only difference is that
`inou` focuses on translation from some external tool to/from LiveHD while
`pass` works on transformations from LiveHD to LiveHD.

## Create a pass

Check the `pass/sample` directory for how to create a trivial pass.

* Create pass/XXX directory

The typical is to have these files:

* pass/XXX/pass_XXX.[cpp|hpp]: C++ and Header file to interface with lgshell
* pass/XXX/XXX.[cpp|hpp]: C++ file to perform the pass over a Lgraph or LNAST API
* pass/XXX/BUILD: the [Bazel](bazel.md) build configuration file
* pass/XXX/tests/XXX_test.cpp: A google test checking the pass


Finally, add the new pass to `main/BUILD`


## Pass Parameters and Common variables

 One of the main goals is to have a uniform set of passes in lgshell. lgshell should use this common
variable names when possible

```bash
    name:foo        lgraph name
    path:lgdb       lgraph database path (lgdb)
    files:foo,var   comma separated list of files used for INPUT
    odir:.          output directory to generate files like verilog/pyrope...
```

## Some hints/comments useful for developers

### Using clang when building

The regression system builds for both gcc and clang. To force a clang build, set the following environment variables before building:

```sh
CXX=clang++ CC=clang bazel build -c dbg //...
```

### Perf in lgbench

Use lgbench to gather statistics in your code block. It also allows to run perf record
for the code section (from lgbench construction to destruction). To enable perf record
set LGBENCH_PERF environment variable

```sh
export LGBENCH_PERF=1
```

### GDB/LLDB usage

For most tests, you can debug with

```sh
gdb ./bazel-bin/main/lgshell
```

or

```sh
lldb ./bazel-bin/main/lgshell
```

Note that breakpoint locations may not resolve until lgshell is started and the relevant LGraph libraries are loaded.

### Address Sanitizer

LiveHD has the option to run it with address sanitizer to detect memory leaks.

```sh
bazel build -c dbg --config asan //...
```

### Thread Sanitizer

To debug with concurrent data race.

```sh
bazel build -c dbg --config tsan //...
```

### Debugging a broken Docker image

The travis/azure regressions run several docker images. To debug the issue, run the same as the failing
docker image. c++ OPT with archlinux-masc image

1. Create some directory to share data in/out the docker run (to avoid
   mistakes/issues, I would not share home directory unless you have done it
   several times before)

```sh
mkdir $HOME/docker
```

2. Run the docker image (in some masc docker images you can change the user to not being root)

```sh
docker run --rm --cap-add SYS_ADMIN -it  -e LOCAL_USER_ID=$(id -u $USER) -v ${HOME}/docker:/home/user mascucsc/archlinux-masc

# Once inside docker image. Create local "user" at /home/user with your userid
/usr/local/bin/entrypoint.sh
```

3. If the docker image did not have the livehd repo, clone it

```sh
git clone https://github.com/masc-ucsc/livehd.git
```

4. Build with the failing options and debug

```sh
CXX=g++ CC=gcc bazel build -c opt //...
```

A docker distro that specially fails (address randomizing and muslc vs libc) is alpine. The command line to debug it:

```sh
docker run --rm --cap-add SYS_ADMIN -it -e LOCAL_USER_ID=$(id -u $USER) -v $HOME:/home/user -v/local/scrap:/local/scrap mascucsc/alpine-masc
```
