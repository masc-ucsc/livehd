#pragma once
// Call_resolver: call/return resolution extracted out of the passes
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

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hlop/dlop.hpp"
#include "lnast.hpp"
#include "lexical_callee.hpp"
#include "lnast_manager.hpp"
#include "str_tools.hpp"
#include "symbol_table.hpp"

namespace upass::call_resolver {

// POSITIONAL marker pseudo-names (not named arguments).
constexpr std::string_view ref_arg_marker  = "__ref_arg";   // pass-by-ref
constexpr std::string_view ufcs_arg_marker = "__ufcs_arg";  // UFCS receiver

struct Call_actual {
  bool        is_named = false;
  std::string name     = {};
  Dlop       value    = {};
  // When the actual is a bare ref to a caller variable, remember the name
  // so a `ref` param can write back into the caller's scope after the
  // body is folded. Empty when the actual is a const literal or named with
  // a non-ref value (write-back only happens when the param is `ref`).
  std::string var_name = {};
  // When the actual is a tuple-typed value (passed by ref to a caller
  // bundle), bundle_value carries the bundle's flat-keyed field values
  // (e.g. {"x": 2, "y": 11}). Set together with is_bundle=true; `value`
  // is then unused.
  bool                                   is_bundle = false;
  std::unordered_map<std::string, Dlop> bundle_value = {};
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
    const absl::flat_hash_map<std::string, std::shared_ptr<Lnast>>&   function_registry,
    const std::function<std::optional<Dlop>()>& resolve_scalar_at_cursor);

template <typename Registry>
std::shared_ptr<Lnast> lookup_callee(const Registry& registry, std::string_view name, std::string_view caller_unit = {}) {
  return upass::resolve_lexical_callee(
      name,
      caller_unit,
      [&](std::string_view key) -> std::shared_ptr<Lnast> {
        auto it = registry.find(key);
        return it == registry.end() ? nullptr : it->second;
      },
      [&](std::string_view suffix) -> std::shared_ptr<Lnast> {
        std::shared_ptr<Lnast> match;
        for (const auto& [key, body] : registry) {
          if (key.size() > suffix.size() && key.ends_with(suffix)) {
            if (match) {
              return nullptr;
            }
            match = body;
          }
        }
        return match;
      });
}

// `import("…")` resolution: `ln:` url → lambda tree-name string bind,
// `lg:` url → the url itself (Sub instances lower at tolg), tuple form → the
// unit's pub-namespace bundle on the dest. Ambiguity = permanent error;
// unknown unit / values-pending = pend_import (kernel whole-file retry).
void process_import_call(Lnast_manager& lm, Symbol_table& st,
                         const absl::flat_hash_map<std::string, std::shared_ptr<Lnast>>&      function_registry,
                         const absl::flat_hash_set<std::string>&                              ambiguous_units,
                         const std::function<void(const std::string&)>& pend_import,
                         const std::function<void(std::string_view, const Dlop&)>& store_trivial,
                         const std::string&                                        dst);

}  // namespace upass::call_resolver
