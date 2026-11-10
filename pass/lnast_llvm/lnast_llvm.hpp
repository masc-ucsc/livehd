//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Lnast;

// Milestone 3 prototype (pass/lg2lnast/README.md): generate a native LLVM state
// kernel straight from ONE typed LNAST function (what pass.lg2lnast emits: every
// statement result carries a type_spec from the proven range), without going
// through the lgraph cgen/color pipeline. The kernel is `void step(uint64_t*
// state, void**)` built with Cgen_llvm: it reads the inputs and registers from
// `state`, publishes the post-edge registers and the outputs settled on them
// (what `lhd sim`'s `step` shows a testbench).
namespace livehd::lnast_llvm {

struct Field {
  std::string name;
  uint32_t    bits   = 0;
  bool        sign   = false;  // signed carrier (min < 0)
  size_t      word   = 0;      // first state word (little endian, whole words)
  uint64_t    init   = 0;      // registers: power-on value (low word)
};

struct Result {
  bool                      ok = false;
  std::string               why;  // first unsupported construct (ok == false)
  std::string               function;
  std::vector<Field>        inputs, outputs, regs;
  size_t                    words = 0;
  size_t                    instructions = 0;
};

// Builds the kernel and writes `<dir>/kernel.o` and `<dir>/kernel.json` (field
// layout for the driver). `lib` holds every lowered module (instances are inlined).
Result generate(const Lnast& ln, const std::vector<std::shared_ptr<Lnast>>& lib, const std::string& dir);

}  // namespace livehd::lnast_llvm
