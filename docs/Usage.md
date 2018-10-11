
# Introduction

This is a high level description of how to compile lgraph and use it

# Requirements

lgraph is heavily tested with the latest arch linux distributions. This is the list of packages needed:

# Arch Linux: (64bit)

```bash
pacman -S cmake
pacman -S clang llvm
pacman -S make
pacman -S yaml-cpp tcl
pacman -S mercurial
sudo pip install ptpython
```

# Documentation

The documentation is written in markdown. To generate PDFs any markdown converter
can be used. We recommend pandoc, to install pandoc:

```bash
pacman -S pandoc
```

To generate PDFs:

```bash
pandoc -S -N -V geometry:margin=1in ./docs/Usage.md -o ./docs/Usage.pdf
```

# Build/clone

```bash
# Clone the directory the first time
git clone  git@github.com:masc-ucsc/lgraph.git
# Must clone submodules like yosys too
git submodule update --init --recursive

# If you cloned already, use this to update submodules
# This command will update the submodules to the commit specified in lgraph
git pull --recurse-submodules
```

You need either debug or release, if you are developing, use the debug option.

See Bazel.md for more details

# Sample usage

Some sample usages for the various functions implemented.

## Read and Write verilog files in/out of lgraph

To read a verilog with yosys and create an lgraph

```bash
$./inou/yosys/lgyosys ./inou/yosys/tests/simple_add.v

process_module \simple_add
Successfully parsed yosys file ./inou/yosys/tests/simple_add.v
```

To dump an lgraph (and submodules) to verilog
```bash
$./inou/yosys/lgyosys -gsimple_add

lgraph_simple_add
Successfully generated verilog file simple_add.v
```

## To use the lgraph comand line

```bash
./bazel-bin/main/lgraph
lgraph> help
```

## Generating a pyrope file from a graph

```bash
# create a graph from verilog
./inou/yosys/lgyosys ./inou/yosys/tests/trivial.v

# create a yaml representation (optional)
./inou/yaml/lgyaml --graph_name trivial --yaml_output trivial.yaml

# read the graph and create a pyrope file
./inou/pyrope/lg2prp --graph_name trivial --pyrope_output trivial_py

# check the output
cat ./trivial_py
```

## Creating and traversing a graph from the code interface

If you are a developer for lgraph, you will likely have to create graph objects
from the code.

```cpp
```

