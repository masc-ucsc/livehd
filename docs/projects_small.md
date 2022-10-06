
# Open Small/Intro Projects 

Those are simpler than MS thesis but useful for LiveHD. They are ranked from
"most" to less "urgent". Each has a easy/medium/hard level. Easy should take a
few days to implement, hard could take a quarter.

## [medium] C++ Cell library (dops)

A related project to the sops is the dops (Dynamic or with unknown bit sizes at
compile time). This is more advanced than the sops, but it shares all the
common fix size optimizations.

The verification should be done against lconst because the goal is to replace
the lconst class with the dops class.

## [medium] C++ Cell library (sops)

LiveHD has Lgraph cells and LNAST ops as basic operations. Each Lgraph cell has
an equivalent LNAST operation, but some LNAST operations do not have a one to
one mapping to Lgraph cells.

Currently Lconst performs cell and LNAST operations. Typical cell operations
are sum_op, xor_op... The idea is to replace to avoid the boost library and to
optimize for our case that when performance is critical, we know the bit size.
This avoid dynamic memory allocation and many checks needed in the boost
library.


The code is partially done in core/?ops.???. dops is for dynamic (variable
size), bops is the base class that both dops and sops use.

dops can be used early in the compilation flow but it should be slow like
lconst, It does not know the bit sizes of the operations. It dynamically
allocates memory as needed. This is great for flexibility but not for
performance.


After the bitwidth pass, before code generation, we know all the bit sizes. A
more efficient C++ library could be used. ESSENT uint/sint have the same
optimization. The goal is to create a new sconst (static const) quite similar
to sint but that performs the LiveHD operations. Some operations like divide
could be shared with the original sint library, but most need a new
implementation.

To save space (cache hit improvement), the sops that need 8 or less bits should
use an byte for storage, between 8 and 16 a short, between 16 and 32 a int32_t,
then a int64_t. Anything bigger should use multiples of int64_t.

Summary of sop tasks:
* Static, compile time, bit size (like sint.hpp)
* Size optimization (8,16,32,64, or 64xn sizes)
* Deterministically randomize when a 'x' (verilog/Pyrope ?) unknown is read
  from text to binary


## [medium] Large HIF files

Not much testing on large HIF files that require multiple IDs (0.??, 1.??). It
may not be even supported (unclear about the status). If not supported, fix it.

## [hard] Fix lnast_fromlg 

Add unit tests (more) and make sure that it is working. LNAST is a super-set of
Lgraph, so nodes are fine, but the idea is to create a bit nicer LNAST (it has
more operators and allows nesting ifs...)

## [medium] Graph core iterator

Finish Graph_core class to have efficient fast/forward/backward iterators.

core/graph_core.???

## [hard] Rust chunmky parser

Create a https://github.com/zesterer/chumsky for Pyrope. The reason is to
allow/test RUST integration in LiveHD flow, but more importantly to leverage
the "super nice" error format system for compile errors and leverage chumsky
for the lexer/parser errors.

This is NOT to replace the tree-sitter parser, but to run in parallel. It will allow to
generate a much cleaner error for lexer/parser syntax errors.

Since it is for errors, it can have a more strict grammar than tree-sitter
which relies in passes to detect errors.

The error reporting should be exposed to the C++17 API.

## [medium] Tree class

Build a C++17 Tree class without pointers optimized for LNAST operations.

core/lhtree.hpp has several comments.

## [medium] Loc/Col to pos1/pos2

Compilers can report lines with Line of Code (LoC) and Column or with start/end
(pos1/pos2) position in bytes. Both are useful. The idea is to keep pos1/pos2 in LNAST (and Lgraph)
but to have an efficient C++ class that allows to translate back and forth.

```
Class Source_info {
  Source_info * create(std::string_view filename);

  std::pair<int,int> get_loc_col(int pos1, int po2) const;
  std::pair<int,int> get_position(int loc, int col) const;
};
```

## [medium] Javascript Cell library (jops)

LiveHD has cell operations. Typical cell operations are sum_op, xor_op... 

This project is mostly done, the idea is to add some testing and complete whatever instruction is left. (livehd/jops/)

## [medium] Cleanup post online neovim tree-sitter pyrope

We have a tree-sitter pyrope grammar. We use neovim, we should have a clean
setup and populate most packages on github to include Pyrope by default. 

## [easy] lgshell autocompletion

Upgrade lgshell to latest version. Fix auto-completion for path: files: src_path: odir:....


## [hard] Jupyter or Polybook for LiveHD

Create a jupyter app that allows to run livehd against a server or local

## [medium] OpenWare

We need a set of set of implementations with different trade-offs for each basic Lgraph gate. The implementations
could optimize for FPGA and ASIC.

Dependence: Pyrope v3 and mockturtle

Main features:

* Custom implementations for all the Lgraph cells: reductions, add/sub, mult, shift, barrel shifter...
* The OpenWare library is built in Pyrope, with efficient verilog generated
* Benchmark the OpenWare against FPGA and ASIC default (designWare) targets
* Specific target implementations for FPGA (Xilinx) and ASIC (generic)
* Several speeds/trade-offs for each major block. E.g: adder RCA/Kogge/...

## [medium] Parallel HIF read

HIF is designed to have a parallel read (0.??, 1.??....) but it is not implemented.

## [medium] Dynamic Power Model

Add clock gating efficiency for registers (If clock switches and values do not
switch. If valid exists and data switches,.... )

## [simple] Setup a vagrant image

We have several dockers for testing, a simple vagrant (ubuntu based?) for most users may
be nice to have. Maybe based on https://github.com/VLSIDA/openram-vagrant-image

## [simple] lgshell ctrl+C

* Intercept the CTRL+C and finish the current command. Do not kill/terminate the lgshell
* This may require to spawn the commands as threads and kill them for ctrl+c

## [medium] LNAST Fuzzer

Randomly create valid LNAST statements. No matter the LNAST (as long as it is
valid), it should not crash/fail.

## [medium] Query shell (not lgshell) to query graphs

Dependence: Coordinate with termwave

* Based on replxx (like lgshell)
* Query bits, ports...  like
    * https://github.com/rubund/netlist-analyzer
    * https://www.jameswhanlon.com/querying-logical-paths-in-a-verilog-design.html
* It would be cool if subsections (selected) parts can be visualized with something like https://github.com/nturley/netlistsvg
* The shell may be expanded to support simulation in the future

Example of queries: show path, show driver/sink of, do topo traversal,....

As an interesting extension would be to have some simple embedded language (TCL or ChaiScript or ???) to control queries more
easily and allow to build functions/libraries.

Maybe an idea/alternative is to have a javascript so that a browser can use the query shell. This will
allow future integration with wavedrom.

The idea is to have a hierarchy view/debug support. Some exmples in javascript:
https://github.com/wavedrom/inspect or https://8bitworkshop.com "Debug Tree".

## [medium] Short demos

Using ascinema, create some small demos to show how to use LiveHD. 

It would be good to have demos on how to "start to create a pass".

https://asciinema.org/

The demos should have a testing, so that they become part of the regression.

## [medium] Term record/replay for benchmarking/testing

Use automatic asciinema generation. Compare the test speed and summarize the
performance difference from "a user" point of view. The results should allow to
track performance changes.

Maybe expand tmt_test and main_test to be a more stand-alone testing setup.

## [medium] `prp format` Aligned code generation

Read pyrope, and indent/align code. It may be nicer to have it integrated with
the LNAST to Pyrope pass, but the aligned can work by itself.

(We have an old/rejected paper that we never finished. Maybe finish do arvix with the Pyrope aligner implemented).

Sample of code alignment:

```
  mut as  = my_call      (a=1 , b=3123)
  let foo = my_other_call(a=22, b=33  )

  x   = 3
  foo = 4
```

Non-alnum values have a higher weight in the alignment constrains. Space have no negative weight, but constant overhead...

The idea is to align only adjacent lines.

If a line is "too different", a newline may be added.



## [easy] https://github.com/chubin/cheat.sh

Populate it for Pyrope basic examples. E.g: get msb, find leading zero, compute checksum....

## [easy] Improve the iassert

(C++17 skills)

Make iassert (I) to be constexpr. Now constexpr methods can not have I()

```
constexpr int add1(const int i) {
   I(i>0); // compile error
   assert(i>0); // OK
}
```

Extend iassert class to have debug break code from:
https://github.com/scottt/debugbreak

Integrate with this?
https://github.com/bombela/backward-cpp

## [medium] Termwave (https://github.com/masc-ucsc/termwave)

Evolve improve termwave to be a separate project but allow future integration with LiveHD.
LiveHD has a vcd reader that handles many more cases than termwave (pass/opentimer/power_vcd.???). Maybe
extract the code and make it part of termwave and fix issues.

A C++ library that can display short waveforms in text. A bit like pyRTL
waveforms
https://raw.githubusercontent.com/UCSBarchlab/PyRTL/master/docs/screenshots/pyrtl-statemachine.png?raw=true

A console may use a C++ API has things like:

* add_monitor("var")
* del_monitor("var")
* show(from, totime)
* Read a vcd file
* update("var", time, value)
* get("var", time) // returns value


## [easy] Check WASM LiveHD

Try https://github.com/emscripten-core/emsdk/tree/main/bazel with LiveHD and mark blocks/subsections
that need to be rewritten.

Some parts may be easy to patch and be compatible (no need to rewrite like
lgedge.cpp that uses weird pointer arithmetic). If so, patch to allow enscripten.

