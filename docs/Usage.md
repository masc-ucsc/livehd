# Usage

This is a high level description of how to compile LiveHD and use it.

## Requirements

Although LiveHD should run on most common Linux distributions, it is heavily tested on both Arch and Kali (Debian based).

The following programs are assumed to be present when building LiveHD:
 - GCC 8+ or Clang 8+* (C++17 support is required**)
 - Yosys***
 - Bazel

The following programs are optional:
 - pandoc (for better viewing of markdown documentation)

It is also assumed that bash is used to compile LiveHD.

\* gcc can have better compile times, but clang offers better warnings and execution speed.  
\*\* If you're unsure if your copy of gcc or clang is new enough, you can check the version by typing 
  ```$ g++ --version```
  or
  ```$ clang++ --version```  
\*\*\* LiveHD requires a specific commit of Yosys which needs to be built from source.

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
  - Tell Yosys the compiler we want to compile it  
      ```$ make config-gcc``` (if using gcc)  
      ```$ make config-clang``` (if using clang)
  - Make Yosys  
      ```$ make -j<number of CPU cores * 2>```
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
    (A binary will be created in `livehd/bazel-bin/main/lgshell`)

5. **Install pandoc (optional)**
  - ```$ sudo pacman -Syu pandoc``` (Arch)  
  - ```$ sudo apt-get install pandoc``` (Kali/Debian/Ubuntu)

## Sample usage

Below are some sample usages of the LiveHD shell.  A bash prompt is indicated by `$`, a LiveHD prompt is indicated by `livehd>`, and a Yosys prompt is indicated by a `yosys>`.  The LiveHD shell supports color output and autocompletion using the tab key.

### Starting and exiting the shell

```
$ ./bazel-bin/main/lgshell
livehd> help
  ...
livehd> exit
```

### Reading and writing verilog files with of LiveHD

- To read a Verilog file with Yosys and create an LGraph:
  ```
  $ ./bazel-bin/main/lgshell
  livehd> inou.yosys.tolg files:./inou/yosys/tests/simple_add.v
  livehd> exit
  $ ls lgdb
  ```
- To dump an LGraph (and submodules) to Verilog:
  ```
  $ ./bazel-bin/main/lgshell
  livehd> lgraph.open name:simple_add |> inou.yosys.fromlg odir:lgdb
  livehd> exit
  $ ls lgdb/*.v
  ```

### Generating json file from an LGraph

```
$ ./bazel-bin/main/lgshell
livehd> inou.yosys.tolg files:./inou/yosys/tests/trivial.v |> @a
livehd> @a |> inou.json.fromlg output:trivial.json
livehd> exit
$ less trivial.json
```

### RocketChip example pass

- Load RocketChip to the DB for the first time
  ```
  $ ./bazel-bin/main/lgshell
  livehd> files path:projects/rocketchip/ |> inou.liveparse
  livehd> inou.yosys.tolg files:lgdb/parse/file_freechips.rocketchip.system.DefaultConfig.v
  livehd> lgraph.open name:RocketTile |> pass.sample.wirecount
  ```
- Perform a pass over RocketTile (the top level module in RocketChip)
  ```
  $ ./bazel-bin/main/lgshell
  livehd> lgraph.open name:RocketTile |> pass.sample.wirecount
  ```

### Low level directed build

- To compile an individual pass:
  ```
  $ bazel build -c dbg //pass/sample:pass_sample
  $ bazel build -c dbg //inou/yosys:all
  ```
- To build a Yosys module:
  ```
  $ bazel build -c dbg //inou/yosys:all
  ```
- To read a module with Yosys directly:
  ```
  $ yosys -m ./bazel-bin/inou/yosys/liblgraph_yosys.so
  yosys> read_verilog -sv ./inou/yosys/tests/trivial.v
  yosys> proc
  yosys> opt -fast
  yosys> pmuxtree
  yosys> memory_dff
  yosys> memory_share
  yosys> memory_collect
  yosys> yosys2lg
  ```
- To create a Verilog file from LGraph:
  ```
  yosys -m ./bazel-bin/inou/yosys/liblgraph_yosys.so
  yosys> lg2yosys -name trivial
  yosys> write_verilog trivial.v
  ```

## Documentation

LiveHD uses Markdown for documentation.  You can view the documentation in GitHub or in any viewer that supports Markdown.  If you would like to generate a pdf of the documentation, we recommend using pandoc (see the Installation section for installation details)

To generate PDFs, run
```
$ pandoc -S -N -V geometry:margin=1in ./docs/Usage.md -o ./docs/Usage.pdf
```
