# Usage

This is a high level description of how to compile LiveHD and use it.

## Requirements

Although LiveHD should run on most common Linux distributions, it is heavily tested on both Arch and Kali (Debian based).

The following programs are assumed to be present when building LiveHD:
 - GCC 8+ or Clang 8+ (c++17 support is required*)
 - Yosys**
 - Bazel

The following programs are optional:
 - pandoc (for better viewing of markdown documentation)

It is also assumed that bash is used to compile LiveHD.

\* If you're unsure if your copy of gcc or clang is new enough, you can check the version by typing 
  ```$ g++ --version```
  or
  ```$ clang++ --version```

\*\* LiveHD requires a specific commit of Yosys which needs to be built from source.

## Installation

1. **Download LiveHD source**  
    ```$ git clone https://github.com/masc-ucsc/livehd```
2. **Build Yosys from source**  
    LiveHD requires a specific commit of Yosys in order to function properly.  Versions of Yosys installed through apt, pacman, etc. will not work.

    When building Yosys from source, it will pull in additional dependancies it needs.  Check https://github.com/YosysHQ/yosys/#setup for more information.

    - Download the Yosys source (the exact directory doesn't matter as long as it's not inside LiveHD)  
        ```$ git clone https://github.com/YosysHQ/yosys```  
        ```$ cd yosys```
    - Find the Yosys commit LiveHD uses and check out that commit  
        ```git checkout `$ grep -C2 BUILD.yosys <absolute path to LiveHD>/WORKSPACE  | grep commit | cut -d\" -f2` ```
    - Tell Yosys that we'll use gcc to compile it  
        ```$ make config-gcc```
    - Install Yosys  
        ```$ sudo make install```
3. **Install Bazel**
    - ```$ sudo pacman -Syu bazel``` (Arch)
    - ```$ sudo apt-get install bazel``` (Kali/Debian/Ubuntu)
4. **Build LiveHD**  
    LiveHD has both release and debug build options.  Release is for regular users, and debug is for those who want to contribute to LiveHD.  See [Bazel.md](Bazel.md) for more information.

    - Build LiveHD  
        ```$ bazel build //main:all``` (release mode)
        ```$ bazel build //main:all -c dbg``` (debug mode)

    A binary will be created in `livehd/bazel-bin/main/lgshell`

5. **Install pandoc (optional)**
    - ```$ sudo pacman -Syu pandoc```

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

To generate PDFs:

```bash
pandoc -S -N -V geometry:margin=1in ./docs/Usage.md -o ./docs/Usage.pdf
```
