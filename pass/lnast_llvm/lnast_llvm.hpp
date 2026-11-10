//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Lnast;

// Generate native LLVM state kernels straight from the typed LNAST pass.lg2lnast emits (every
// statement result carries a type_spec from the proven range), without going through the lgraph
// cgen/color pipeline (pass/lg2lnast/README.md).
namespace livehd::lnast_llvm {

struct Field {
  std::string name;
  uint32_t    bits   = 0;
  bool        sign   = false;  // signed carrier (min < 0)
  size_t      word   = 0;      // first state word (little endian, whole words)
  uint64_t    init   = 0;      // registers: power-on value (low word)
  // Registers only. `has_init`: the register declares an initial/reset value (`init_words`, two's
  // complement over `bits`, little endian). `entries` > 0: a memory of that many entries of
  // `entry_bits` bits each, stored in the natural layout (Cgen_llvm::array_state_words); `bits` is
  // then the whole footprint.
  bool                  has_init = false;
  // Power-on value of state without an initializer (lnast_sim_rt.hpp, `fill`): 0 zero, 1 unknown or
  // zero under sim.init_zero, 2 unknown.
  uint8_t               fill     = 0;
  std::vector<uint64_t> init_words;
  uint64_t              entries    = 0;
  uint32_t              entry_bits = 0;
  // Inputs only, with activity gating: the kernels (their dirty bits) that read this input.
  std::vector<uint32_t> marks;
};

// The generated simulator: a list of state kernels `void k(uint64_t* state, void**)` over one flat
// state allocation of `words` 64-bit words. One clock period is: write the inputs, call every
// `edge_functions` kernel in order (clock low: the next state, memory writes), then `commit_function`
// (when not empty: the rising edge -- registers take their next value, or their reset value), then
// the `fall_functions` kernels and `fall_commit_function` (clock high, then the falling edge: present
// only for a design with negedge registers or latches open while the clock is high), then every
// `settle_functions` kernel in order (clock low again: the outputs, on the new state). The settle
// kernels alone settle the outputs on the current state without an edge (power-on, a loaded
// checkpoint).
//
// Activity gating (`gate_bits` > 0): kernel number g -- counting through `edge_functions`, then
// `fall_functions`, then `settle_functions` -- owns bit g of the `gate_bits` dirty bits that start at
// state word `gate_word`. The caller runs a kernel only when its bit is set and clears the bit BEFORE
// the call (a kernel may set its own again); the commit kernels always run. The kernels set the bits
// themselves when they change what another kernel reads. The caller sets every bit after it wrote
// state on its own (power-on, a loaded checkpoint), and the bits in an input's `marks` whenever it
// stores a different value into that input.
struct Result {
  bool                      ok = false;
  std::string               why;  // first unsupported construct (ok == false)
  std::vector<std::string>  edge_functions, fall_functions, settle_functions;
  std::string               commit_function, fall_commit_function;
  std::vector<std::string>  objects;  // every native object written
  std::vector<Field>        inputs, outputs, regs;
  std::vector<std::string>  clocks;  // top inputs that are clocks (no state word)
  size_t                    words = 0;
  size_t                    gate_word = 0, gate_bits = 0;
  size_t                    instructions = 0;
};

// Writes `<dir>/kernel*.llvm.o` and `<dir>/kernel.json` (the layout and the kernel lists, for
// lnast_llvm_run). `lib` holds every lowered module (instances are inlined).
Result generate(const Lnast& ln, const std::vector<std::shared_ptr<Lnast>>& lib, const std::string& dir);

// The `lhd sim` form. `object` is the path stem of the objects: `<stem minus .llvm.o>-e<N>.llvm.o`
// (edge), `-c.llvm.o` (commit) and `-s<N>.llvm.o` (settle); kernels are named `<symbol>_e<N>` and so
// on. `json`: optional layout file.
struct Sim_objects {
  std::string symbol;
  std::string object;
  std::string json;
};
Result generate_sim(const Lnast& ln, const std::vector<std::shared_ptr<Lnast>>& lib, const Sim_objects& out);

}  // namespace livehd::lnast_llvm
