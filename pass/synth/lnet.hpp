// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The flat k-LUT network (`Lnet`, abc_cleanup.md section 4, domino.md 5.1):
// one region's (or one proof cone's) logic, owned by one worker. Nodes are
// numbered densely in topological order; node 0 is the constant 0. A node is
// a constant, a source (a PI or a latch output) or a LUT of at most
// kMaxFanin fanins whose function is a truth table over its fanins in order
// (fanin i = variable i). There are no complemented edges: inversion lives in
// the table. Inputs, outputs and latches are boundary tables, not nodes.
//
// A RAW network (lnet_ops.hpp) is the blaster's translation exactly as
// recorded: constants created lazily, explicit inverters, 2-input gates, and
// the creation position of every output, so a backend replays it object for
// object. A STRASH network (strash()) is its normalized form: the sources in
// CI order (inputs, then latch outputs), then structurally hashed 2-input
// gates with complemented edges folded into their tables, and nothing no
// output reaches. A cover (pass/usyn) is a coarse network over the same
// boundary: one LUT per gate, with its SOP as side data.
#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace livehd::synth {

using Lid = uint32_t;

class Lnet {
public:
  static constexpr Lid      kNone     = std::numeric_limits<Lid>::max();
  static constexpr Lid      kConst0   = 0;  // node 0
  static constexpr uint32_t kMaxFanin = 8;
  enum class Kind : uint8_t { constant, source, lut };

  // Replicated 64-bit tables (bit x = f(x)) of the RAW gates.
  static constexpr uint64_t kVar0 = 0xAAAAAAAAAAAAAAAAULL;  // f = x0
  static constexpr uint64_t kNot  = ~kVar0;                 // f = !x0
  static constexpr uint64_t kAnd2 = 0x8888888888888888ULL;
  static constexpr uint64_t kOr2  = 0xEEEEEEEEEEEEEEEEULL;
  static constexpr uint64_t kXor2 = 0x6666666666666666ULL;

  struct Input {
    Lid         node = kNone;
    std::string name;  // empty: an unnamed black-box output
  };
  struct Latch {
    std::string name;
    char        init = 'x';    // '0', '1' (a power-on value) or 'x' (don't care)
    Lid         q    = kNone;  // its output (a source node)
    Lid         d    = kNone;  // the next state; kNone until set
  };
  struct Output {
    Lid         node = kNone;
    std::string name;
    Lid         created = 0;  // the network size when it was recorded: its creation position
  };
  // A sum of products over a LUT's fanins (bit i of care/ones = fanin i):
  // f = OR of the cubes, or its complement when `complemented`.
  struct Cube {
    uint32_t care = 0;
    uint32_t ones = 0;
  };
  struct Sop {
    std::vector<Cube> cubes;
    bool              complemented = false;
  };

  Lnet();

  [[nodiscard]] size_t               size() const { return kind_.size(); }
  [[nodiscard]] Kind                 kind(Lid n) const { return kind_[n]; }
  [[nodiscard]] uint32_t             fanin_count(Lid n) const { return count_[n]; }
  [[nodiscard]] Lid                  fanin(Lid n, uint32_t i) const { return fanin_[n][i]; }
  [[nodiscard]] std::span<const Lid> fanins(Lid n) const { return {fanin_[n].data(), count_[n]}; }
  // The table (<= 6 fanins, replicated; a constant's is all zeros or all
  // ones). A LUT of 7 or 8 fanins has only table().
  [[nodiscard]] uint64_t             fn(Lid n) const { return fn_[n]; }
  // The table's words: one for <= 6 fanins, 2 or 4 (bit x of word x/64 is
  // f(x)) for 7 or 8.
  [[nodiscard]] std::span<const uint64_t> table(Lid n) const {
    return count_[n] <= 6 ? std::span<const uint64_t>(&fn_[n], 1) : std::span<const uint64_t>(wide_[fn_[n]].data(), count_[n] == 7 ? 2 : 4);
  }
  // f(x) for the fanin assignment x (bit i = fanin i).
  [[nodiscard]] bool                 eval(Lid n, uint32_t x) const { return (table(n)[x >> 6] >> (x & 63)) & 1; }
  // The LUT's SOP side data, or null.
  [[nodiscard]] const Sop*           sop(Lid n) const { return n < sop_of_.size() && sop_of_[n] != kNone ? &sops_[sop_of_[n]] : nullptr; }
  // Readers among nodes, outputs and latch inputs.
  [[nodiscard]] uint32_t             fanout_count(Lid n) const { return fanout_[n]; }
  // LUT levels from the sources and constants (0).
  [[nodiscard]] uint32_t             level(Lid n) const { return level_[n]; }
  // A source node's input index, or its latch index when is_latch_source().
  [[nodiscard]] uint32_t             source_index(Lid n) const { return aux_[n] & ~kLatchBit; }
  [[nodiscard]] bool                 is_latch_source(Lid n) const { return kind_[n] == Kind::source && (aux_[n] & kLatchBit) != 0; }

  // A constant node besides node 0 (a RAW network keeps its lazy constants).
  Lid add_constant(bool value);
  // A LUT over at most 6 `fanins` computing `fn` (replicated from its low 2^k
  // bits), or over up to 8 with the table's words (see table()).
  Lid add_lut(std::span<const Lid> fanins, uint64_t fn);
  Lid add_lut(std::initializer_list<Lid> fanins, uint64_t fn) { return add_lut(std::span<const Lid>(fanins.begin(), fanins.size()), fn); }
  Lid add_lut(std::span<const Lid> fanins, std::span<const uint64_t> table);
  void set_sop(Lid n, Sop sop);

  // A primary input: a demanded bit of a region input or of a black-box output.
  Lid      add_input(std::string name);
  // A latch; its output node is latch(k).q.
  uint32_t add_latch(std::string name, char init);
  void     set_latch_input(uint32_t latch, Lid d);
  void     add_output(Lid bit, std::string name);

  [[nodiscard]] const std::vector<Input>&  inputs() const { return inputs_; }
  [[nodiscard]] const std::vector<Latch>&  latches() const { return latches_; }
  [[nodiscard]] const std::vector<Output>& outputs() const { return outputs_; }
  [[nodiscard]] const Latch&               latch(uint32_t k) const { return latches_[k]; }

private:
  static constexpr uint32_t kLatchBit = 0x80000000U;
  Lid                       add_node(Kind kind, std::span<const Lid> fanins, uint64_t fn, uint32_t aux);

  std::vector<Kind>                          kind_;
  std::vector<uint8_t>                       count_;
  std::vector<std::array<Lid, kMaxFanin>>    fanin_;
  std::vector<uint64_t>                      fn_;
  std::vector<uint32_t>                      fanout_;
  std::vector<uint32_t>                      level_;
  std::vector<uint32_t>                      aux_;
  std::vector<Input>                         inputs_;
  std::vector<Latch>                         latches_;
  std::vector<Output>                        outputs_;
  std::vector<std::array<uint64_t, 4>>       wide_;    // tables of 7-8 fanin LUTs (fn_ indexes)
  std::vector<Sop>                           sops_;
  std::vector<uint32_t>                      sop_of_;  // node -> sops_ index or kNone (sized on demand)
};

// The combinational outputs of a network in CO order: its outputs, then its
// latch inputs (kNone for a latch whose input was never set).
[[nodiscard]] std::vector<Lid> combinational_outputs(const Lnet& net);

struct Strash_options {
  // Keep an XOR as one 2-input node; false builds it from three ANDs.
  bool                  xor_nodes = true;
  // Consulted every 1024 RAW nodes; false abandons the rebuild.
  std::function<bool()> admit;
};
// The STRASH form of a RAW network: the same inputs, latches and outputs (the
// sources in CI order), structurally hashed 2-input gates with complemented
// edges folded into their tables (AND and XOR up to input and output
// complement), node 0 as the only constant, and a shared inverter per
// complemented output. nullopt when `admit` refused, a latch input is unset
// or a LUT is not a RAW gate.
[[nodiscard]] std::optional<Lnet> strash(const Lnet& raw, const Strash_options& options = {});

}  // namespace livehd::synth
