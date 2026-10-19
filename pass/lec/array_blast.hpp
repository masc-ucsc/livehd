// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cvc5/cvc5.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace livehd::lec {

// Small-array elimination for a solver's input (Fresh_solver applies it to
// every assertion before handing them to cvc5).
//
// An array whose index is a bit-vector of at most `max_index_bits` bits and
// whose elements are bit-vectors has exactly 2^w entries, so it can be
// represented EXACTLY by one bit-vector term per entry:
//   free array A          -> fresh symbols A@0 .. A@(2^w-1)
//   constant array        -> its base value in every entry
//   store(a, i, v)        -> entry e = ite(i == e, v, a[e])
//   ite(c, a, b)          -> entry e = ite(c, a[e], b[e])
//   select(a, i)          -> ite chain over the entries (a[i] for a constant i)
//   a = b / distinct(a,b) -> conjunction of the entry equalities (negated)
// The rewrite is an equivalence (a bijection between array values and entry
// tuples), so satisfiability, models and UNSAT verdicts are preserved.
//
// Why: BMC of a small memory under the reference's unknown-state tracking
// (formal.lec.gold_x=ignore) made cvc5's lazy array solver case-split on every
// pair of symbolic read/write addresses of every unrolled step (two 4x8 lanes
// written through an unknown address: 65 s at bound 2, timeout at bound 3).
// As plain bit-vectors the same query is settled by the bit-blaster at once.
// It also keeps the cvc5 "write-chains connecting two different constant
// arrays" abort impossible for those memories: no array term remains.
//
// Larger arrays (and arrays of any other sort) are left untouched. A small
// array used by any other construct (e.g. passed to an uninterpreted
// function) makes the blaster give up for good (`failed()`): the caller must
// then fall back to the original terms everywhere, because the entry symbols
// minted for an array are only related to it through this rewrite.
// Index width (bits) up to which LEC blasts a memory's array: 32 entries.
inline constexpr uint32_t kArray_blast_index_bits = 5;

class Array_blaster {
public:
  explicit Array_blaster(cvc5::TermManager& tm, uint32_t max_index_bits = kArray_blast_index_bits)
      : tm_(tm), max_index_bits_(max_index_bits) {}

  // The rewrite of a non-array term (Boolean or bit-vector), or `t` itself
  // when it contains no small array. Null after a failure.
  cvc5::Term rewrite(const cvc5::Term& t);
  // A value term for `t` (any sort) from `value_of`, which evaluates rewritten
  // non-array terms (e.g. cvc5::Solver::getValue).
  template <typename Value_of>
  cvc5::Term value(const cvc5::Term& t, Value_of&& value_of) {
    if (!is_small_array(t.getSort())) {
      return value_of(rewrite(t));
    }
    const auto& es = entries(t);
    if (failed_) {
      return {};
    }
    std::vector<cvc5::Term> vals;
    vals.reserve(es.size());
    for (const auto& e : es) {
      vals.push_back(value_of(e));
    }
    return array_value(t.getSort(), vals);
  }
  [[nodiscard]] bool failed() const { return failed_; }
  // True once some small array was actually rewritten.
  [[nodiscard]] bool active() const { return !arr_memo_.empty(); }
  [[nodiscard]] bool is_small_array(const cvc5::Sort& s) const;
  // True once a term of a (non-small) array sort was seen: that array stays.
  [[nodiscard]] bool saw_large_array() const { return saw_large_array_; }

private:
  const std::vector<cvc5::Term>& entries(const cvc5::Term& a);
  void                           visit(const cvc5::Term& root);
  void                           finish(const cvc5::Term& t);
  cvc5::Term                     index_eq(const cvc5::Term& idx, size_t e);
  cvc5::Term                     array_value(const cvc5::Sort& sort, const std::vector<cvc5::Term>& vals);

  cvc5::TermManager&                                       tm_;
  uint32_t                                                 max_index_bits_;
  bool                                                     failed_          = false;
  bool                                                     saw_large_array_ = false;
  std::unordered_map<cvc5::Term, cvc5::Term>               memo_;      // non-array term -> rewrite
  std::unordered_map<cvc5::Term, std::vector<cvc5::Term>> arr_memo_;  // small array term -> entries
};

}  // namespace livehd::lec
