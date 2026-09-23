// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// One region's bit-level logic as the blaster produced it, before any backend
// sees it. pass.abc replays the tape into its ABC netlist (the mapper's
// PI/PO/latch skeleton and the built-in flow's input); pass.synth reads the
// logic straight from the tape, so evaluating the unate optimizer involves no
// ABC logic synthesis (no strash, no AIG import).
//
// A bit is an index into `nodes`. Fanins precede their node. The ops fold only
// what the blaster's constructors fold (constants, x op x): the tape keeps the
// exact creation order, so the replayed ABC network is the one pass.abc built
// before the tape existed.
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace livehd::abc {

struct Blast_tape {
  static constexpr uint32_t kNone = std::numeric_limits<uint32_t>::max();
  enum class Op : uint8_t {
    const0,
    const1,
    inv,    // a
    and2,   // a, b
    or2,    // a, b
    xor2,   // a, b
    pi,     // a = index into pis
    latch,  // a = index into latches; the node is the latch Q
    po,     // a = the observed bit, b = index into pos (not a bit)
  };
  struct Node {
    Op       op;
    uint32_t a = 0, b = 0;
  };
  struct Pi {
    std::string name;  // empty: an unnamed black-box output
  };
  struct Latch {
    std::string name;
    char        init = 'x';  // '0', '1' (a power-on value) or 'x' (don't care)
    uint32_t    q    = kNone;
    uint32_t    d    = kNone;  // the next state, set after the logic is blasted
  };
  struct Po {
    std::string name;
    uint32_t    bit = kNone;
  };
  std::vector<Node>  nodes;
  std::vector<Pi>    pis;      // PI (CI) order
  std::vector<Latch> latches;  // box order: after the PIs among the CIs
  std::vector<Po>    pos;      // PO order: before the latch inputs among the COs

  [[nodiscard]] const Node& node(uint32_t bit) const { return nodes[bit]; }
  [[nodiscard]] bool        is_const(uint32_t bit, bool v) const { return nodes[bit].op == (v ? Op::const1 : Op::const0); }
};

}  // namespace livehd::abc
