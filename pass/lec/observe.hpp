// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cvc5/cvc5.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "encode.hpp"

// pass/lec — bit-level OBSERVABILITY (cone-of-influence) of flop state for the
// single-step inductive miter.
//
// A flop bit is OBSERVABLE iff it can reach a compared obligation that is not a
// next state of a candidate flop -- a primary output, a box input, a property,
// a memory write/read -- through combinational logic and other observable flop
// bits. An unobservable bit cannot affect any compared output, so it does not
// have to take part in the equivalence obligation: the relation the induction
// proves only needs to equate the observable bits, and an unobservable bit's
// current value may be left free per side.
//
// The analysis here only PROPOSES that smaller relation. It never excuses an
// obligation by itself: query.cpp re-encodes the side whose unobservable bits
// become free symbols and the solver still checks every output and every kept
// next-state bit. A relation that is weaker than full state equality is still
// initially true whenever full equality is, so an imprecise analysis costs at
// most a proof, never soundness.
namespace livehd::lec {

// Dense bit set sized to one term (bit i = that term's bit i; a Boolean or an
// array term is one "bit" meaning "needed at all").
class Bit_set {
public:
  Bit_set() = default;
  explicit Bit_set(int width) : width_(width), words_(static_cast<size_t>((width + 63) / 64), 0) {}

  [[nodiscard]] int  width() const { return width_; }
  [[nodiscard]] bool test(int i) const {
    return i >= 0 && i < width_ && ((words_[static_cast<size_t>(i >> 6)] >> (i & 63)) & 1U) != 0;
  }
  void set(int i) {
    if (i >= 0 && i < width_) {
      words_[static_cast<size_t>(i >> 6)] |= uint64_t{1} << (i & 63);
    }
  }
  void set_range(int lo, int hi_inclusive) {
    for (int i = std::max(0, lo); i <= hi_inclusive && i < width_; ++i) {
      set(i);
    }
  }
  void                           set_all() { set_range(0, width_ - 1); }
  [[nodiscard]] bool             any() const;
  [[nodiscard]] int              count() const;
  [[nodiscard]] bool             all() const { return count() == width_; }
  [[nodiscard]] int              lowest() const;   // -1 when empty
  [[nodiscard]] int              highest() const;  // -1 when empty
  // this |= o (same width); returns the bits that were NEW.
  Bit_set                        merge(const Bit_set& o);
  [[nodiscard]] std::vector<int> bits() const;

private:
  int                   width_ = 0;
  std::vector<uint64_t> words_;
};

// Value of a GROUND bit-vector/Boolean term (no free symbol anywhere below
// it), LSB first, one byte per bit (a Boolean is one bit). The encoder builds
// comptime arithmetic as terms -- `wins#[(i*N)..+N]` is an extract of a shift
// whose amount is `(zext 5 #b0100)` -- and cvc5 does not fold them at mkTerm,
// so the demand transfer needs the constant behind them. nullopt: the term
// reads a symbol, or uses an operator this evaluator does not model.
class Ground_eval {
public:
  using Bits = std::vector<uint8_t>;
  const std::optional<Bits>& eval(const cvc5::Term& t);

private:
  std::unordered_map<cvc5::Term, std::optional<Bits>> memo_;
};

// Backward bit-demand propagation over a cvc5 term DAG. `demand` marks bits of
// a term as needed; `run` pushes the demand down to the operands with a
// per-operator transfer function (exact for slices, concatenation, extensions,
// bitwise logic -- including the bits a constant AND/OR operand forces --
// constant shifts/rotates and ITE arms; a low-bit prefix for add/sub/neg/mul;
// every bit of every operand for everything else). A registered TARGET term
// stops the walk: new bits reaching it go to the callback, which may demand
// more (the fixpoint over flop next states).
class Bit_demand {
public:
  using Target_cb = std::function<void(int id, const Bit_set& new_bits)>;

  void add_target(const cvc5::Term& t, int id) { targets_.emplace(t, id); }
  void demand(const cvc5::Term& t, const Bit_set& bits);
  void demand_all(const cvc5::Term& t);
  void run(const Target_cb& cb);

  [[nodiscard]] static int width_of(const cvc5::Term& t);

private:
  struct Slot {
    Bit_set seen;     // every bit demanded so far
    Bit_set pending;  // demanded but not yet pushed to the operands
    bool    queued = false;
  };
  Slot& slot(const cvc5::Term& t);
  void  push(const cvc5::Term& t, const Bit_set& d);

  Ground_eval                          ground_;
  std::unordered_map<cvc5::Term, Slot> slots_;
  std::unordered_map<cvc5::Term, int>  targets_;
  std::vector<cvc5::Term>              work_;
};

// One flop cut the inductive miter may reduce: both sides hold it, its current
// state was seeded from the shared symbol, and both sides emit its next state.
struct Obs_candidate {
  std::string key;      // flop cut key (the `\x01nxt:<key>` suffix)
  Val         cur[2];   // [0]=ref [1]=impl seeded current state (Encoded::inputs[key])
  Val         next[2];  // next state (Encoded::outputs["\x01nxt:" + key])
};

// The reduced obligation for one cut: `keep` are the positions (0 .. min
// width-1, ascending) whose current value stays tied across the sides and
// whose next value is compared; every other bit of either side is free.
struct Obs_plan {
  int              w_ref  = 0;
  int              w_impl = 0;
  std::vector<int> keep;
};

// Joint observability fixpoint over both encodings. Roots are every
// obligation that is not a candidate's next state: the other outputs
// (primary outputs, box inputs, properties, other cuts' next states), memory
// next states / ports / bulk updates, registered reads and the encoder's side
// equalities. A candidate bit demanded on either side is kept on both (the
// compare is joint), so its next state is demanded on both sides. A demand on
// a wide side's bit above the narrower side's width keeps the whole cut.
// Returns only the cuts that actually shrink.
absl::flat_hash_map<std::string, Obs_plan> plan_observability(const Encoded& re, const Encoded& ie,
                                                              const std::vector<Obs_candidate>& cands);

// ONE side's demanded current-state bits of the flop cuts `keys` (every key
// must have its current state in e.inputs[key] and its next state in
// e.outputs["\x01nxt:" + key], same width; others are skipped). Roots are
// every obligation of the side except those keys' own next states; a demanded
// bit of a key demands the same bit of its next state (the fixpoint). A bit
// absent from the result can never reach an output of this side. Used to place
// a narrower flop's bits inside a wider counterpart (query.cpp state windows).
absl::flat_hash_map<std::string, Bit_set> state_bit_demand(const Encoded& e, const std::vector<std::string>& keys);

// `v` (width v.width) with every bit NOT in `keep` replaced by a fresh symbol
// named `<tag>` + position. Bits in `keep` must be < v.width.
Val free_unkept_bits(cvc5::TermManager& tm, const Val& v, int width, const std::vector<int>& keep, const std::string& tag);

// The concatenation (MSB = highest position) of `t`'s bits at `keep`.
cvc5::Term extract_kept(cvc5::TermManager& tm, const cvc5::Term& t, const std::vector<int>& keep);

}  // namespace livehd::lec
