// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "array_blast.hpp"

#include <format>
#include <utility>

namespace livehd::lec {

using cvc5::Kind;
using cvc5::Term;

bool Array_blaster::is_small_array(const cvc5::Sort& s) const {
  if (!s.isArray()) {
    return false;
  }
  const auto is = s.getArrayIndexSort();
  const auto es = s.getArrayElementSort();
  return is.isBitVector() && es.isBitVector() && is.getBitVectorSize() <= max_index_bits_;
}

Term Array_blaster::index_eq(const Term& idx, size_t e) {
  return tm_.mkTerm(Kind::EQUAL, {idx, tm_.mkBitVector(idx.getSort().getBitVectorSize(), static_cast<uint64_t>(e))});
}

Term Array_blaster::array_value(const cvc5::Sort& sort, const std::vector<Term>& vals) {
  if (vals.empty()) {
    return {};
  }
  Term out = tm_.mkConstArray(sort, vals.front());
  for (size_t e = 1; e < vals.size(); ++e) {
    if (vals[e] != vals.front()) {
      out = tm_.mkTerm(Kind::STORE,
                       {out, tm_.mkBitVector(sort.getArrayIndexSort().getBitVectorSize(), static_cast<uint64_t>(e)), vals[e]});
    }
  }
  return out;
}

const std::vector<Term>& Array_blaster::entries(const Term& a) {
  static const std::vector<Term> none;
  if (failed_) {
    return none;
  }
  if (auto it = arr_memo_.find(a); it != arr_memo_.end()) {
    return it->second;
  }
  visit(a);
  if (failed_) {
    return none;
  }
  return arr_memo_.at(a);
}

Term Array_blaster::rewrite(const Term& t) {
  if (failed_) {
    return {};
  }
  if (auto it = memo_.find(t); it != memo_.end()) {
    return it->second;
  }
  visit(t);
  if (failed_) {
    return {};
  }
  return memo_.at(t);
}

// Iterative post-order walk: BMC unrollings thread terms across many steps,
// and a recursive walk over them overflows the stack.
void Array_blaster::visit(const Term& root) {
  std::vector<std::pair<Term, bool>> stack;
  stack.emplace_back(root, false);
  while (!stack.empty() && !failed_) {
    auto [t, expanded] = stack.back();
    stack.pop_back();
    const bool small = is_small_array(t.getSort());
    if (small ? arr_memo_.contains(t) : memo_.contains(t)) {
      continue;
    }
    if (!expanded) {
      stack.emplace_back(t, true);
      for (size_t i = t.getNumChildren(); i-- > 0;) {
        const Term c = t[i];
        if (is_small_array(c.getSort()) ? !arr_memo_.contains(c) : !memo_.contains(c)) {
          stack.emplace_back(c, false);
        }
      }
      continue;
    }
    finish(t);
  }
}

// All children of `t` are already rewritten.
void Array_blaster::finish(const Term& t) {
  const Kind k = t.getKind();
  if (is_small_array(t.getSort())) {
    const auto   sort = t.getSort();
    const size_t n    = size_t{1} << sort.getArrayIndexSort().getBitVectorSize();
    std::vector<Term> es;
    es.reserve(n);
    if (t.getNumChildren() == 0) {
      if (t.isConstArray()) {
        const Term base = t.getConstArrayBase();
        es.assign(n, base);
      } else if (k == Kind::CONSTANT) {
        const std::string name = t.hasSymbol() ? t.getSymbol() : std::format("arr{}", t.getId());
        for (size_t e = 0; e < n; ++e) {
          es.push_back(tm_.mkConst(sort.getArrayElementSort(), std::format("{}@{}", name, e)));
        }
      } else {
        failed_ = true;  // a bound variable or an unknown leaf kind
        return;
      }
    } else if (k == Kind::STORE) {
      const auto& base = arr_memo_.at(t[0]);
      const Term  idx  = memo_.at(t[1]);
      const Term  val  = memo_.at(t[2]);
      es               = base;
      if (idx.isBitVectorValue()) {
        es[static_cast<size_t>(std::stoull(idx.getBitVectorValue(10)))] = val;
      } else {
        for (size_t e = 0; e < n; ++e) {
          es[e] = tm_.mkTerm(Kind::ITE, {index_eq(idx, e), val, base[e]});
        }
      }
    } else if (k == Kind::ITE) {
      const Term  c = memo_.at(t[0]);
      const auto& a = arr_memo_.at(t[1]);
      const auto& b = arr_memo_.at(t[2]);
      for (size_t e = 0; e < n; ++e) {
        es.push_back(a[e] == b[e] ? a[e] : tm_.mkTerm(Kind::ITE, {c, a[e], b[e]}));
      }
    } else {
      failed_ = true;  // e.g. a lambda or an array-valued function
      return;
    }
    arr_memo_.emplace(t, std::move(es));
    return;
  }
  if (t.getSort().isArray()) {
    saw_large_array_ = true;
  }
  const size_t nc = t.getNumChildren();
  if (nc == 0) {
    memo_.emplace(t, t);
    return;
  }
  if (k == Kind::SELECT && is_small_array(t[0].getSort())) {
    const auto& a   = arr_memo_.at(t[0]);
    const Term  idx = memo_.at(t[1]);
    Term        out;
    if (idx.isBitVectorValue()) {
      out = a[static_cast<size_t>(std::stoull(idx.getBitVectorValue(10)))];
    } else {
      out = a.front();
      for (size_t e = 1; e < a.size(); ++e) {
        out = a[e] == out ? out : tm_.mkTerm(Kind::ITE, {index_eq(idx, e), a[e], out});
      }
    }
    memo_.emplace(t, out);
    return;
  }
  if ((k == Kind::EQUAL || k == Kind::DISTINCT) && is_small_array(t[0].getSort())) {
    if (nc != 2) {
      failed_ = true;
      return;
    }
    const auto&       a = arr_memo_.at(t[0]);
    const auto&       b = arr_memo_.at(t[1]);
    std::vector<Term> eqs;
    for (size_t e = 0; e < a.size(); ++e) {
      if (a[e] != b[e]) {
        eqs.push_back(tm_.mkTerm(Kind::EQUAL, {a[e], b[e]}));
      }
    }
    Term all = eqs.empty() ? tm_.mkTrue() : eqs.size() == 1 ? eqs.front() : tm_.mkTerm(Kind::AND, eqs);
    memo_.emplace(t, k == Kind::EQUAL ? all : tm_.mkTerm(Kind::NOT, {all}));
    return;
  }
  // Any other construct: rebuild over the rewritten children. A small array
  // may not appear as a direct child of anything else.
  std::vector<Term> kids;
  kids.reserve(nc);
  bool changed = false;
  for (size_t i = 0; i < nc; ++i) {
    const Term c = t[i];
    if (is_small_array(c.getSort())) {
      failed_ = true;
      return;
    }
    Term r  = memo_.at(c);
    changed = changed || r != c;
    kids.push_back(std::move(r));
  }
  if (!changed) {
    memo_.emplace(t, t);
    return;
  }
  memo_.emplace(t, t.hasOp() ? tm_.mkTerm(t.getOp(), kids) : tm_.mkTerm(k, kids));
}

}  // namespace livehd::lec
