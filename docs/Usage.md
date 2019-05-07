# Usage

This is a high level description of how to compile LGraph and use it

## Requirements

LGraph is heavily tested with the latest arch linux distributions. This is the list of packages needed:

### Arch Linux: (64bit)

```bash
pacman -S cmake
pacman -S clang llvm
pacman -S make
pacman -S yaml-cpp tcl
pacman -S mercurial
sudo pip install ptpython
```

## Documentation

The documentation is written in markdown. To generate PDFs any markdown converter
can be used. We recommend pandoc, to install pandoc:

```bash
pacman -S pandoc
```

To generate PDFs:

```bash
pandoc -S -N -V geometry:margin=1in ./docs/Usage.md -o ./docs/Usage.pdf
```

## Build/clone

```bash
# Clone the directory the first time
git clone  git@github.com:masc-ucsc/lgraph.git
```

You need either debug or release, if you are developing, use the debug option.

See Bazel.md for more details

For a simple release build, simply type:

```
bazel build //main:lgshell
```

A binary will be created in:

```
$ ls ./bazel-bin/main/lgshell
```

## Sample usage

Some sample usages for the various functions implemented.

In this section we differentiate between the bash prompt ($) and the LGraph prompt (lgraph>).

### To use the lgraph comand line

```bash
./bazel-bin/main/lgshell
lgraph> help
```

### Read and Write verilog files in/out of LGraph

To read a verilog with yosys and create an LGraph

```bash
$ ./bazel-bin/main/lgshell
lgraph> inou.yosys.tolg files:./inou/yosys/tests/simple_add.v
lgraph> exit
$ ls lgdb
```

To dump an lgraph (and submodules) to verilog
```bash
$ ./bazel-bin/main/lgshell
lgraph> lgraph.open name:simple_add |> inou.yosys.fromlg odir:lgdb
lgraph> exit
$ ls lgdb/*.v
```

### Generating json file from a graph

```bash
$ ./bazel-bin/main/lgshell

lgraph> inou.yosys.tolg files:./inou/yosys/tests/trivial.v |> @a
lgraph> @a |> inou.json.fromlg output:trivial.json
lgraph> exit

$ ls trivial.json
```

### Low level directed build

To compile an individual pass:

```bash
bazel build -c dbg //pass/sample:pass_sample
bazel build -c dbg //inou/yosys:all
```

To build yosys module:

```bash
bazel build -c dbg //inou/yosys:all
```

To read a module with yosys directly

```bash
yosys -m ./bazel-bin/inou/yosys/liblgraph_yosys.so
>read_verilog -sv ./inou/yosys/tests/trivial.v ; proc ; opt -fast ; pmuxtree ; memory_dff ; memory_share ; memory_collect
>yosys2lg
```

To create a verilog from lgraph

```bash
yosys -m ./bazel-bin/inou/yosys/liblgraph_yosys.so
>lg2yosys -name trivial
>write_verilog trivial.v
```

