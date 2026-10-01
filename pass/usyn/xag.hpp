// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "unate.hpp"

namespace livehd::usyn {

struct Xsignal {
  Id      id       = 0;
  bool    inverted = false;
  Xsignal operator~() const { return {id, !inverted}; }
  bool    operator==(const Xsignal&) const  = default;
  auto    operator<=>(const Xsignal&) const = default;
};

// Append-only, structurally hashed AND/XOR graph. State and external outputs
// are kept by its owner; each source is an independent boundary signal. No
// recipe option lowers XOR into ANDs. Rewrites construct a new graph so saved
// windows never silently change their Boolean meaning.
class Xag {
public:
  enum class Kind : uint8_t { constant, source, and_gate, xor_gate };
  struct Node {
    Kind                   kind = Kind::constant;
    std::array<Xsignal, 2> inputs{};
    uint32_t               fanouts      = 0;
    uint32_t               level        = 0;
    uint32_t               source_index = 0;
  };
  Xag();
  Xsignal                         constant(bool value) const { return {0, value}; }
  Xsignal                         input(std::string name);
  Xsignal                         land(Xsignal a, Xsignal b);
  Xsignal                         lxor(Xsignal a, Xsignal b);
  Xsignal                         lor(Xsignal a, Xsignal b) { return ~land(~a, ~b); }
  Xsignal                         mux(Xsignal select, Xsignal when_true, Xsignal when_false);
  const Node&                     node(Id id) const { return nodes.at(id); }
  size_t                          size() const { return nodes.size(); }
  const std::vector<std::string>& input_names() const { return names; }

private:
  struct Key {
    Kind    kind;
    Xsignal a, b;
    bool    operator==(const Key&) const = default;
  };
  struct Hash {
    size_t operator()(const Key& k) const;
  };
  std::vector<Node>                 nodes;
  std::vector<std::string>          names;
  std::unordered_map<Key, Id, Hash> index;
  void                              check(Xsignal signal) const;
  Xsignal                           intern(Kind kind, Xsignal a, Xsignal b);
};

struct Window_limits {
  uint32_t inputs = 16;
  uint32_t nodes  = 100000;
};

struct Xag_window {
  Status          status = Status::invalid;
  Xsignal         root;
  std::vector<Id> leaves;    // variable order; positive signal of each node
  std::vector<Id> interior;  // topological order, constants excluded
  bool            whole_cone = false;
  std::string     reason;
};

// Exact structural cone collection stops at the provided leaves. A missing
// source is an invalid cut; over-budget/over-size is search exhaustion, not
// Boolean infeasibility. No tables are allocated during collection.
Xag_window collect_window(const Xag& graph, Xsignal root, std::span<const Id> leaves, const Window_limits& limits, Budget& work);
Xag_window whole_cone(const Xag& graph, Xsignal root, const Window_limits& limits, Budget& work);
// Collect a divisor on the subset of a common basis it actually reaches.
// Unlike collect_window, unused basis signals are allowed; unbounded sources
// beyond that basis are still rejected.
Xag_window collect_subwindow(const Xag& graph, Xsignal root, std::span<const Id> basis, const Window_limits& limits, Budget& work);
// Greedy reconvergence-aware seed, expanding one leaf at a time and charging
// for distinct new signals. This is not a guarantee of optimal boundary inputs.
Xag_window grow_window(const Xag& graph, Xsignal root, const Window_limits& limits, Budget& work);

struct Window_function {
  Status      status = Status::invalid;
  Truth_table table;
};
// Lift a compact window table to a common ordered independent basis. Every
// old input must occur exactly once in that basis; unused basis inputs remain
// independent and do not change the function.
Window_function lift_function(const Truth_table& table, std::span<const Id> inputs, std::span<const Id> basis, Budget& work);
Window_function basis_function(const Xag& graph, Xsignal root, std::span<const Id> basis, const Window_limits& limits,
                               Budget& work);
// Simulates 64 assignments at a time. Workspace is O(window nodes + 2^inputs)
// rather than one 2^inputs table per node. The window is validated again before
// use; complements and common leaf identities are preserved exactly.
Window_function window_function(const Xag& graph, const Xag_window& window, Budget& work);

}  // namespace livehd::usyn
