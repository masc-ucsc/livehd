
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

## Debug

```bash
mkdir -p ~/build/lgraphd
cd ~/build/lgraphd
cmake -DCMAKE_BUILD_TYPE=Debug ~/projs/lgraph
make VERBOSE=1
```

## Release
```bash
mkdir -p ~/build/lgraph
cd ~/build/lgraph
cmake ~/projs/lgraph
make
```

# Sample usage

Some sample usages for the various functions implemented.

## Generate random graph

```bash
# inside build/lgraph
./inou/rand/lgrand --help

# Create a random graph named "0" (counter increased per graph)
./inou/rand/lgrand --lgdb=foo
ls foo
```
> lgraph\_0\_delay	lgraph\_0\_inputs  lgraph\_0\_nodes  lgraph\_0\_outputs  lgraph\_0\_type

```bash
ls output.yaml

# Each file has a structure used by the lgraph
```

## Generate a yaml representation of the graph (human readable)

```bash
# read the binary graph dump and generate a yaml output
./inou/yaml/lgyaml --lgdb=foo --graph_name 0 --yaml_output same.yaml

# it should be the same as output.yaml
diff same.yaml output.yaml

cp output.yaml input.yaml
./inou/yaml/lgyaml
# Recreate the output.yaml from the given input.yaml
diff input.yaml output.yaml # DIFFERENT!!!!

#They are the same graph but the node ids may have change
#so a diff does not show the equivalence
```
## Generate a json representation of the Graph (human readable)

```bash
# the json representation usage is very similar to a yaml input/output
./inou/json/lgjson --lgdb=foo --graph_name 0 --json_output same.json


## Reading verilog and creating a graph out of it

```bash
# read the verilog file using the yosys input
# the output will be on ./lgdb by default
./inou/yosys/lgyosys ./inou/yosys/tests/iwls_adder.v

# create a yaml representation for the newly generated graph
# note that top is the module name in the iwls_adder.v file
./inou/yaml/lgyaml --graph_name top --yaml_output adder.yaml


# alternativelly, pass a whole directory
./inout/yosys/lgyosys --testdir ./inou/yosys/tests/
```

## Generating a verilog file from a graph

```bash
# create a graph from verilog
./inou/yosys/lgyosys ./inou/yosys/tests/trivial.v

# create a yaml representation (optional)
./inou/yaml/lgyaml --graph_name trivial --yaml_output trivial.yaml

# read the graph and create a verilog file
./inou/yosys/lgyosys -gtrivial

# check the output
cat ./trivial.v
```

## Generating a pyrope file from a graph

```bash
# create a graph from verilog
./inou/yosys/lgyosys ./inou/yosys/tests/trivial.v

# create a yaml representation (optional)
./inou/yaml/lgyaml --graph_name trivial --yaml_output trivial.yaml

# read the graph and create a pyrope file
./inou/pyrope/lgpyrope --graph_name trivial --pyrope_output trivial_py

# check the output
cat ./trivial_py
```


## Creating and traversing a graph from the code interface

If you are a developer for lgraph, you will likely have to create graph objects
from the code.

```cpp
```


# Updating submodules from origin

If you need to update a submodule to the latest version and want lgraph to point
to the latest commit in the child repository, you need to:

```bash
cd subs/<repo>
git pull origin master
cd ..
git commit
git push
```
