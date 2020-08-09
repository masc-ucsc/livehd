# Usage

This is a high level description of how to compile LiveHD and use it

## Requirements

LiveHD is heavily tested with the latest arch and Kali (debian based) linux
distributions. In general, you need to install YOSYS with the required packages
by yosys, and the capacity to compile c++ with static libraries for gcc or
clang version 8 or newer.

A simple hello.cpp program like the following will check if your gcc/clang is
new enough with needed libraries: 

```cpp
#include <string_view>
#include <filesystem>
#include <iostream>

int main() {
  std::string_view file{"a.out"};

  if (std::filesystem::exists(file))
    std::cout << "Hello a.out\n";
  else
    std::cout << "Where is the a.out?\n";

  return 0;
}
```

The previous code should compile correctly with (or clang++):

```bash
g++ -std=c++17 -static hello.cpp
```

## Install the correct Yosys version

To read/write verilog through the yosys platform, you need to install the
correct yosys version.  Notice that Yosys requires more packages to install.
Check https://github.com/YosysHQ/yosys/#setup for a more detailed list of
packages.

```bash
cd livehd
cd ../
git clone https://github.com/YosysHQ/yosys
cd yosys
git checkout `grep -C2 BUILD.yosys **PATH_TO_LIVEHD**/WORKSPACE  | grep commit | cut -d\" -f2`
make config-gcc
sudo make install
```

## Build/clone

```bash
# Clone the directory the first time
git clone  git@github.com:masc-ucsc/livehd.git
```

You need either debug or release, if you are developing, use the debug option.

See Bazel.md for more details

For a simple release build, simply type:

```
bazel build //main:all
```

A binary will be created in:

```
$ ls ./bazel-bin/main/lgshell
```

## Sample usage

Some sample usages for the various functions implemented.

In this section we differentiate between the bash prompt ($) and the LiveHD prompt (livehd>).

### To use the lgshell comand line

```bash
./bazel-bin/main/lgshell
livehd> help
```

### Read and Write verilog files in/out of LiveHD

To read a verilog with yosys and create an LGraph

```bash
$ ./bazel-bin/main/lgshell
livehd> inou.yosys.tolg files:./inou/yosys/tests/simple_add.v
livehd> exit
$ ls lgdb
```

To dump an lgraph (and submodules) to verilog
```bash
$ ./bazel-bin/main/lgshell
livehd> lgraph.open name:simple_add |> inou.yosys.fromlg odir:lgdb
livehd> exit
$ ls lgdb/*.v
```

### Generating json file from a graph

```bash
$ ./bazel-bin/main/lgshell
livehd> inou.yosys.tolg files:./inou/yosys/tests/trivial.v |> @a
livehd> @a |> inou.json.fromlg output:trivial.json
livehd> exit

$ ls trivial.json
```

### RocketChip example pass

Load RocketChip to the DB for the first time
```bash
$ ./bazel-bin/main/lgshell
livehd> files path:projects/rocketchip/ |> inou.liveparse
livehd> inou.yosys.tolg files:lgdb/parse/file_freechips.rocketchip.system.DefaultConfig.v
livehd> lgraph.open name:RocketTile |> pass.sample.wirecount
```

Perform a pass over RocketTile (top level module in RocketChip)
```bash
$ ./bazel-bin/main/lgshell
livehd> lgraph.open name:RocketTile |> pass.sample.wirecount
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

To create a verilog from LGraph

```bash
yosys -m ./bazel-bin/inou/yosys/liblgraph_yosys.so
>lg2yosys -name trivial
>write_verilog trivial.v
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
