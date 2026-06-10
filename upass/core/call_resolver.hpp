#pragma once
// 2b/F — Call_resolver: call/return resolution extracted out of the passes
// (first slice: constprop's actual collection — pure code motion; the
// runner-side name/arity/vararg resolution + error surface accretes here).
//
// The resolver walks the func_call node under the cursor and materializes
// the ordered actuals: named (`store(name, value)` children), positional
// (bare const/ref children), pass-by-ref / UFCS markers (positional),
// tuple-typed bundles (flattened field maps), function-name actuals
// (qualified registry names as string Consts), and unresolved slots
// (runtime-only drivers — kept so positional binding stays consistent).

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "const.hpp"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "symbol_table.hpp"

namespace upass::call_resolver {

// POSITIONAL marker pseudo-names (not named arguments).
constexpr std::string_view ref_arg_marker  = "__ref_arg";   // pass-by-ref
constexpr std::string_view ufcs_arg_marker = "__ufcs_arg";  // task 1k UFCS receiver

struct Call_actual {
  bool        is_named = false;
  std::string name;
  Const       value;
  // When the actual is a bare ref to a caller variable, remember the name
  // so a `ref` param can write back into the caller's scope after the
  // body is folded. Empty when the actual is a const literal or named with
  // a non-ref value (write-back only happens when the param is `ref`).
  std::string var_name;
  // When the actual is a tuple-typed value (passed by ref to a caller
  // bundle), bundle_value carries the bundle's flat-keyed field values
  // (e.g. {"x": 2, "y": 11}). Set together with is_bundle=true; `value`
  // is then unused.
  bool                                   is_bundle = false;
  std::unordered_map<std::string, Const> bundle_value;
  // True when the actual could not be folded to a concrete scalar/bundle
  // (e.g. runtime-only Flop driver). The inliner binds nothing for these
  // params but still folds body stmts that don't depend on them.
  bool is_unresolved = false;
};

// Cursor contract: positioned ON the callee-name child of the func_call
// (collection starts at the NEXT sibling) — the same contract the
// constprop-resident original had. `resolve_scalar_at_cursor` supplies the
// caller's scalar folding for value operands (constprop's
// resolve_current_scalar today).
std::optional<std::vector<Call_actual>> collect_call_actuals(
    Lnast_manager& lm, const Symbol_table& st,
    const std::unordered_map<std::string, std::shared_ptr<Lnast>>& function_registry,
    const std::function<std::optional<Const>()>&                   resolve_scalar_at_cursor);


// 2b/F — callee NAME resolution: the exact-name module plus any unique
// "<module>.<name>" body; a candidate with a real comb signature (non-empty
// io) wins over an empty-io top module of the same name (a file's top can
// share its comb's name — the extracted body must win or the inline bails).
// nullptr = not found / ambiguous suffix. Templated over the registry map
// (the runner keys an absl::flat_hash_map, constprop a std::unordered_map).
template <typename Registry>
std::shared_ptr<Lnast> lookup_callee(const Registry& function_registry, std::string_view name) {
  std::shared_ptr<Lnast> exact;
  if (auto it = function_registry.find(std::string(name)); it != function_registry.end()) {
    exact = it->second;
  }
  const std::string      suffix = "." + std::string(name);
  std::shared_ptr<Lnast> suffix_body;
  int                    suffix_matches = 0;
  for (const auto& [k, v] : function_registry) {
    if (k.size() > suffix.size() && k.compare(k.size() - suffix.size(), suffix.size(), suffix) == 0) {
      suffix_body = v;
      ++suffix_matches;
    }
  }
  // Prefer a candidate carrying a real comb signature (non-empty io).
  auto has_sig = [](const std::shared_ptr<Lnast>& l) { return l && !l->io_meta().empty(); };
  if (has_sig(exact)) {
    return exact;
  }
  if (suffix_matches == 1 && has_sig(suffix_body)) {
    return suffix_body;
  }
  if (exact) {
    return exact;  // empty-io exact (e.g. void marker) — let try_inline decide
  }
  return suffix_matches == 1 ? suffix_body : nullptr;
}

// 2b/F — `import("…")` resolution: `ln:` url → lambda tree-name string bind,
// `lg:` url → the url itself (Sub instances lower at tolg), tuple form → the
// unit's pub-namespace bundle on the dest. Ambiguity = permanent error;
// unknown unit / values-pending = pend_import (kernel whole-file retry).
void process_import_call(Lnast_manager& lm, Symbol_table& st,
                         const std::unordered_map<std::string, std::shared_ptr<Lnast>>& function_registry,
                         const std::unordered_set<std::string>&                         ambiguous_units,
                         const std::function<void(const std::string&)>&                 pend_import,
                         const std::function<void(std::string_view, const Const&)>&     store_trivial,
                         const std::string&                                             dst);

}  // namespace upass::call_resolver
