# Usage

This is a high level description of how to use LiveHD.

## Sample usage

Below are some sample usages of the LiveHD shell (lgshell).  A bash prompt is indicated by `$`, an lgshell prompt is indicated by `livehd>`, and a Yosys prompt is indicated by a `yosys>`.  Lgshell supports color output and autocompletion using the tab key.

### General concepts

When Verilog file(s) are imported into lgshell through the `inou.yosys.tolg` command (see below for examples), the Verilog modules get converted to an internal representation and are stored in `livehd/lgdb`.  If a problem occurs while importing Verilog files (due to a syntax error, use of un-synthesizable Verilog, or something else), the corresponding error from Yosys will be printed.  Once a hierarchy has been created, other lgshell commands can read, modify, or export this hierarchy freely.

The command `lgraph.match` can be used to specify a (sub)hierarchy to operate over, which can then be moved from pass to pass using the pipe (`|>`) operator.

### Starting and exiting the shell

```
$ ./bazel-bin/main/lgshell
livehd> help
  ...
livehd> help pass.sample
livehd> exit
```

### Reading and writing Verilog files with of LiveHD
- To read a single-module Verilog file with Yosys and create an LGraph:
  ```
  $ ./bazel-bin/main/lgshell
  livehd> inou.yosys.tolg files:./inou/yosys/tests/simple_add.v
  livehd> exit
  $ ls lgdb
  ```
- To read a Verilog file with more than one module:
  ```
  $ ./bazel-bin/main/lgshell
  livehd> inou.yosys.tolg files:./inou/yosys/tests/hierarchy.v top:<top module name>
  livehd> exit
  $ ls lgdb
  ```
- Print information about an existing LGraph:
  ```
  $ ./bazel-bin/main/lgshell
  livehd> inou.yosys.tolg files:./inou/yosys/tests/trivial.v
  livehd> lgraph.match |> lgraph.stats
  ```
- To dump an LGraph (and submodules) to Verilog:
  ```
  $ ./bazel-bin/main/lgshell
  livehd> lgraph.open name:simple_add |> inou.yosys.fromlg odir:lgdb
  livehd> exit
  $ ls lgdb/*.v
  ```
`lgraph.match` picks up any LGraphs matching the regex passed (or everything if no regex is provided) and treats every single one as the top of the hierarchy, whereas `lgraph.open name:<root module>` will just open the root module as the top of the hierarchy.

### Running a custom pass

```
$ ./bazel-bin/main/lgshell
livehd> inou.yosys.tolg files:./inou/yosys/tests/trivial.v
livehd> lgraph.match |> <pass name>
```

### Generating json file from an LGraph

```
$ ./bazel-bin/main/lgshell
livehd> inou.yosys.tolg files:./inou/yosys/tests/trivial.v |> inou.json.fromlg output:trivial.json
livehd> exit
$ less trivial.json
```

### RocketChip example pass

- Load RocketChip (a RISC-V core) to the DB for the first time
  ```
  $ ./bazel-bin/main/lgshell
  livehd> inou.yosys.tolg files:lgdb/parse/file_freechips.rocketchip.system.DefaultConfig.v
  livehd> lgraph.open name:RocketTile |> pass.sample.wirecount
  ```
- Perform a pass over RocketTile (the top level module in RocketChip)
  ```
  $ ./bazel-bin/main/lgshell
  livehd> lgraph.open name:RocketTile |> pass.sample.wirecount
  ```
Other example projects are located in the `projects` folder.  Keep in mind that the `BoomConfig` verilog file contains almost 500,000 lines of code!

- Perform a pass over BoomTile
  ```
  $ ./bazel-bin/main/lgshell
  livehd> inou.yosys.tolg files:projects/boom/AsyncResetReg.v,projects/boom/SimDTM.v,projects/boom/boom.system.TestHarness.BoomConfig.v
  livehd> lgraph.match |> lgraph.stats
  ```

### Low level directed build

- To compile an individual pass:
  ```
  $ bazel build -c dbg //pass/sample:pass_sample
  $ bazel build -c dbg //inou/yosys:all
  ```
- To build a direct Yosys executable that has LiveHD embedded:
  ```
  $ bazel build -c dbg //inou/yosys:all
  $./bazel-bin/inou/yosys/yosys2
  ```

## Documentation

LiveHD uses Markdown for documentation.  You can view the documentation in GitHub or in any viewer that supports Markdown.  If you would like to generate a pdf of the documentation, we recommend using pandoc (see the Installation section for installation details)

To generate PDFs, run
```
$ pandoc -S -N -V geometry:margin=1in ./docs/Usage.md -o ./docs/Usage.pdf
```

## Developing LiveHD

If you're interested in working on LiveHD, refer to [Develop](./Develop.md).

