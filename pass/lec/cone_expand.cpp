// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cone_expand.hpp"

#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace livehd::lec {
std::vector<cvc5::Term> expand_cone_definitions(cvc5::TermManager&                                    tm,
                                                const std::vector<std::pair<cvc5::Term, cvc5::Term>>& definitions,
                                                const std::vector<cvc5::Term>&                        obligations) {
  using cvc5::Kind;
  using cvc5::Term;
  std::unordered_map<Term, Term> defs, memo;
  std::unordered_set<Term>       ambiguous, active;
  for (const auto& [symbol, value] : definitions) {
    if (symbol.getKind() != Kind::CONSTANT || !symbol.getSort().isBitVector() || symbol.getSort() != value.getSort()) {
      continue;
    }
    auto [it, fresh] = defs.emplace(symbol, value);
    if (!fresh && it->second != value) {
      ambiguous.insert(symbol);
    }
  }
  for (const auto& symbol : ambiguous) {
    defs.erase(symbol);
  }
  std::function<Term(const Term&)> expand = [&](const Term& term) -> Term {
    if (const auto found = memo.find(term); found != memo.end()) {
      return found->second;
    }
    // Cyclic definitions are still asserted in the full solver. Stop expansion
    // here, leaving an unconstrained boundary atom for the cone accelerator.
    if (!active.insert(term).second) {
      return term;
    }
    Term result = term;
    if (const auto definition = defs.find(term); definition != defs.end()) {
      result = expand(definition->second);
    } else if (term.getNumChildren() != 0) {
      std::vector<Term> children;
      bool              changed = false;
      for (const auto& child : term) {
        children.push_back(expand(child));
        changed |= children.back() != child;
      }
      if (changed) {
        result = term.hasOp() ? tm.mkTerm(term.getOp(), children) : tm.mkTerm(term.getKind(), children);
      }
      if (result.getKind() == Kind::SELECT) {
        const Term                         address = result[1];
        Term                               array   = result[0];
        std::vector<std::pair<Term, Term>> stores;
        while (array.getKind() == Kind::STORE) {
          stores.emplace_back(array[1], array[2]);
          array = array[0];
        }
        if (!stores.empty()) {
          result = tm.mkTerm(Kind::SELECT, {array, address});
          for (size_t i = stores.size(); i-- > 0;) {
            const auto& [index, value] = stores[i];
            if (index == address) {
              result = value;
            } else if (index.isBitVectorValue() && address.isBitVectorValue()) {
              continue;
            } else {
              result = tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::EQUAL, {address, index}), value, result});
            }
          }
        }
      }
    }
    active.erase(term);
    memo.emplace(term, result);
    return result;
  };
  std::vector<Term> result;
  result.reserve(obligations.size());
  for (const auto& obligation : obligations) {
    result.push_back(expand(obligation));
  }
  return result;
}
}  // namespace livehd::lec
