# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

LiveHD uses Bazel as its build system. The main workspace is defined in `MODULE.bazel` and `WORKSPACE` files.

### Core Commands

- **Build all targets**: `bazel build //...`
- **Run tests**: `bazel test //...` or use `./scripts/run-test.sh`
- **Build main executable**: `bazel build //main:lgshell`
- **Run LiveHD shell**: `./bazel-bin/main/lgshell`
- Formatted with `clang-format` for all the C++ files

### Test Categories

- **Short tests**: Run by default with `bazel test //...`
- **Long tests**: Use `--test_tag_filters long1,long2,long3,long4,long5,long6,long7,long8` or set `RUN_TYPE=long`

## Architecture Overview

LiveHD is a hardware compiler infrastructure with several key components designed for fast incremental hardware development:

### Compilation Flow

1. **Source Code** → **Parse Tree/Language-specific AST**
2. **Parse Tree** → **LNAST** (Language Neutral AST)
3. **LNAST Passes** (type and bitsize inference, optimizations)
4. **LNAST** → **LGraph** (Low-level Intermediate Representation)
5. **LGraph Passes** (synthesis optimizations, transformations)
6. **Code Generation** (Verilog, C++, etc.)

### Core Components

- **LGraph**: Hierarchical graph representation optimized for synthesis/simulation (`lgraph/`)
  - Bi-directional edges, hierarchical traversal support
  - Nodes represent operations (logic, arithmetic, muxes, registers, memories, sub-graphs)
  - Pins connect nodes (driver pins output, sink pins input)
- **LNAST**: Language Neutral AST for high-level IR (`lnast/`)
  - High-level intermediate representation bridging different HDLs
  - Supports type inference, bitwidth analysis, and optimizations
- **Core**: Low-level utilities and data structures (`core/`)

### Input/Output Modules (`inou/`)

- `yosys/`: Integration with Yosys synthesis tool
- `slang/`: SystemVerilog parsing using Slang
- `pyrope/`: Pyrope HDL support
- `json/`: JSON import/export
- `verilog/`: Verilog processing
- `graphviz/`: Graph visualization

### Transformation Passes (`pass/`)

- `bitwidth/`: Bitwidth analysis and inference
- `cprop/`: Constant propagation
- `lnast_tolg/`: LNAST to LGraph conversion
- `lnast_fromlg/`: LGraph to LNAST conversion
- `compiler/`: Main compilation orchestration
- `semantic/`: Semantic analysis and checking
- `opentimer/`: Static timing analysis integration
- `mockturtle/`: Logic optimization using Mockturtle

### Code Generation

- `inou/code_gen/`: Multi-language code generation
- `inou/cgen/`: C++ code generation
- `simlib/`: Simulation library and runtime

## Development Workflow

### Testing

The test suite is comprehensive and includes both unit tests and integration tests. Tests are tagged:
- Default: Quick tests that run in CI
- `long1-long8`: Extended test suites for thorough validation
- `fixme`: Known issues or tests under development

### Main Executable

The primary tool is `lgshell` (`//main:lgshell`), which provides an interactive shell for LiveHD operations. It integrates all the input/output modules and transformation passes.

### Scripting with EPRP

LiveHD uses EPRP (Extensible Pipelined Research Platform) scripting language for automation:
- Pipe-based design: `command |> next_command`
- Variable storage: `#variable_name`
- Example: `inou.yosys.read_verilog file:input.v |> pass.bitwidth |> inou.yosys.write_verilog file:output.v`

### LGraph Traversal Methods

Key traversal options for graph analysis:
- `fast()`: Random order traversal (fastest)
- `forward()`: Const nodes first, then ordered traversal
- `backward()`: Reverse of forward traversal
- Add `true` parameter for recursive subgraph traversal
- Hierarchical traversal available through `ref_htree()` method

## Key Dependencies

- **Bazel**: Build system
- **Abseil**: C++ utilities
- **GoogleTest**: Testing framework
- **Yosys**: Synthesis backend
- **ABC**: Logic optimization
- **Slang**: SystemVerilog parsing
- **Boost**: C++ libraries
- **RapidJSON**: JSON processing

## File Organization

- Source files use `.cpp/.hpp` extensions
- Build files are `BUILD` (Bazel)
- Tests typically end with `_test.cpp`
- Shell scripts for automation in `scripts/`
- Documentation in `docs/`
- Hardware libraries in `ware/`

## Design Goals

LiveHD aims to be:
- **Fast**: Parallel, scalable, incremental flow targeting "seconds" for synthesis/simulation results
- **Friendly**: Improved error reporting and user experience
- **Trustworthy**: Comprehensive CI, random testing, logic equivalence checks (LEC)

## Notes

- The codebase is actively developed with semantic versioning 0.x (API changes expected)
- Supports multiple HDLs: Verilog, Pyrope, SystemVerilog
- Designed for fast incremental compilation ("live" development)
- Comprehensive CI with logic equivalence checking (LEC)
- LNAST expansion for tuples and macros is still under development
