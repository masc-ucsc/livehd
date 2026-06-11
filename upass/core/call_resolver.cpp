// See call_resolver.hpp. Bodies moved verbatim from
// uPass_constprop::collect_call_actuals (cursor sugar expanded to the
// Lnast_manager calls).

#include "call_resolver.hpp"

#include <format>

#include "absl/strings/str_cat.h"
#include "diag.hpp"
#include "lnast_ntype.hpp"

namespace upass::call_resolver {

std::optional<std::vector<Call_actual>> collect_call_actuals(
    Lnast_manager& lm, const Symbol_table& st,
    const std::unordered_map<std::string, std::shared_ptr<Lnast>>& function_registry,
    const std::function<std::optional<Const>()>&                   resolve_scalar_at_cursor) {
  std::vector<Call_actual> actuals;

  const auto is_type = [&](Lnast_ntype::Lnast_ntype_int t) { return lm.get_raw_ntype() == t; };

  // Helper: when the current cursor sits on a ref whose target is a
  // *non-trivial* bundle (tuple) in the symbol table, materialize it as a
  // flat key-map {field-path: scalar} so the inliner can route per-field
  // reads. Skipped for trivial-scalar bundles (key "0" only) — those route
  // through the normal scalar path and let typecast/built-in folds keep
  // working.
  auto try_collect_bundle = [&]() -> std::optional<std::unordered_map<std::string, Const>> {
    if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
      return std::nullopt;
    }
    auto name   = std::string(lm.current_text());
    auto bundle = st.get_bundle(name);
    if (!bundle || bundle->is_trivial_scalar()) {
      return std::nullopt;
    }
    std::unordered_map<std::string, Const> out;
    for (const auto& [k, ep] : bundle->non_attr_entries()) {
      if (ep.trivial.is_invalid()) {
        return std::nullopt;
      }
      out.emplace(std::string(k), ep.trivial);
    }
    if (out.empty()) {
      return std::nullopt;
    }
    return out;
  };

  while (lm.move_to_sibling()) {
    if (is_type(Lnast_ntype::Lnast_ntype_store)) {
      if (!lm.move_to_child()) {
        continue;
      }
      Call_actual actual;
      const auto  key = lm.current_text();
      // __ref_arg (pass-by-ref) and __ufcs_arg (task 1k UFCS receiver) are
      // POSITIONAL markers, not named arguments.
      actual.is_named = key != ref_arg_marker && key != ufcs_arg_marker;
      if (actual.is_named) {
        actual.name = std::string(key);
      }
      if (!lm.move_to_sibling()) {
        lm.move_to_parent();
        continue;
      }
      if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
        actual.var_name = std::string(lm.current_text());
        // Tuple actual: try to extract a flat field-map from the caller's
        // symbol table before falling back to the scalar path.
        if (auto bundle = try_collect_bundle(); bundle.has_value()) {
          actual.is_bundle    = true;
          actual.bundle_value = std::move(*bundle);
          lm.move_to_parent();
          actuals.emplace_back(std::move(actual));
          continue;
        }
        // Function-name actual (closure_capture, fcall6 style): when the
        // referenced name resolves to a known function in the registry,
        // surface the qualified function name as a string Const so the
        // callee's body can dispatch through it via inner_fname lookup.
        std::string qualified = std::string(lm.get_top_module_name()) + "." + actual.var_name;
        if (function_registry.count(qualified)) {
          actual.value = *Dlop::from_string(qualified);
          lm.move_to_parent();
          actuals.emplace_back(std::move(actual));
          continue;
        }
      }
      auto value = resolve_scalar_at_cursor();
      lm.move_to_parent();
      if (!value.has_value() || value->is_invalid()) {
        // Unresolved actual: still emit a slot so positional binding stays
        // consistent. Body stmts that don't read this param will still fold;
        // ones that do will defer/abort.
        actual.is_unresolved = true;
        actuals.emplace_back(std::move(actual));
        continue;
      }
      actual.value = *value;
      actuals.emplace_back(std::move(actual));
      continue;
    }

    std::string var_name;
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      var_name = std::string(lm.current_text());
      if (auto bundle = try_collect_bundle(); bundle.has_value()) {
        Call_actual actual;
        actual.is_named     = false;
        actual.var_name     = std::move(var_name);
        actual.is_bundle    = true;
        actual.bundle_value = std::move(*bundle);
        actuals.emplace_back(std::move(actual));
        continue;
      }
      // Function-name actual.
      std::string qualified = std::string(lm.get_top_module_name()) + "." + var_name;
      if (function_registry.count(qualified)) {
        Call_actual actual;
        actual.is_named = false;
        actual.var_name = std::move(var_name);
        actual.value    = *Dlop::from_string(qualified);
        actuals.emplace_back(std::move(actual));
        continue;
      }
    }
    auto value = resolve_scalar_at_cursor();
    if (!value.has_value() || value->is_invalid()) {
      Call_actual unres;
      unres.is_named      = false;
      unres.var_name      = std::move(var_name);
      unres.is_unresolved = true;
      actuals.emplace_back(std::move(unres));
      continue;
    }
    actuals.emplace_back(Call_actual{.is_named = false, .name = {}, .value = *value, .var_name = std::move(var_name)});
  }

  return actuals;
}

void process_import_call(Lnast_manager& lm, Symbol_table& st,
                         const std::unordered_map<std::string, std::shared_ptr<Lnast>>& function_registry,
                         const std::unordered_set<std::string>&                         ambiguous_units,
                         const std::function<void(const std::string&)>&                 pend_import,
                         const std::function<void(std::string_view, const Const&)>&     store_trivial,
                         const std::string&                                             dst) {
  if (!lm.move_to_sibling()) {
    return;  // malformed call (no argument) — leave unfolded
  }
  std::string text(lm.current_raw_text());
  if (text.size() >= 2 && (text.front() == '\'' || text.front() == '"') && text.back() == text.front()) {
    text = text.substr(1, text.size() - 2);
  }
  if (text.empty()) {
    return;
  }
  auto pend = [&]() { pend_import(text); };
  auto str_const = [](std::string_view s) { return *Dlop::from_pyrope(absl::StrCat("'", s, "'")); };
  // §2 — a name provided by more than one input is ambiguous when imported
  // (a permanent error, not a defer; non-imported collisions never reach here).
  auto ambiguous_fail = [&](std::string_view name) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "import-ambiguous",
        .category = "name",
        .pass     = "upass.call_resolver",
        .message  = std::format("ambiguous import \"{}\": `{}` is provided by more than one input", text, name),
        .hint     = "the same unit/tree name was loaded from multiple `ln:` inputs; drop or rename the duplicate"});
  };

  if (text.starts_with("ln:")) {
    // Whole remaining string = the tree name, verbatim (may contain dots).
    const std::string tree = text.substr(3);
    if (ambiguous_units.count(tree)) {
      ambiguous_fail(tree);
      return;
    }
    if (!function_registry.count(tree)) {
      pend();
      return;
    }
    store_trivial(dst, str_const(tree));
    return;
  }
  if (text.starts_with("lg:")) {
    // Bind the url itself; the call sites lower to Sub instances at tolg
    // (task 1m-E wires the GraphLibrary lookup + missing-graph diagnostics).
    store_trivial(dst, str_const(text));
    return;
  }
  if (ambiguous_units.count(text)) {  // tuple form — the unit name itself is ambiguous
    ambiguous_fail(text);
    return;
  }

  // Tuple form — the unit's whole pub namespace.
  auto build_namespace = [&](std::string_view                                        unit,
                             const std::vector<std::pair<std::string, std::string>>& values,
                             const std::vector<Lnast_pub_entry>&                     pubs) {
    auto b = std::make_shared<Bundle>(dst);
    for (const auto& [path, val_text] : values) {
      b->set(path, *Dlop::from_pyrope(val_text));
    }
    for (const auto& p : pubs) {
      if (p.kind == "value") {
        continue;
      }
      b->set(p.name, str_const(absl::StrCat(unit, ".", p.name)));
      b->set_attr(p.name, "pub", str_const(p.kind));
    }
    // Marks "import namespace" (the runner's UFCS check exempts these; the
    // read-only guarantee rides the importing `const` declaration).
    b->set_attr("pub_unit", str_const(unit));
    st.set(dst, b);
  };

  // Cross-invocation: a loaded `<unit>.__pub` wrapper tree (the durable pub
  // list). Walk its synthesized shape: store(ref path, const text) leaves +
  // attr_set(ref name, '__pub', '<kind>') lambda markers.
  if (auto wit = function_registry.find(text + ".__pub"); wit != function_registry.end()) {
    const auto&                                      w = wit->second;
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<Lnast_pub_entry>                     pubs;
    auto                                             root = w->get_root();
    for (auto stmts : w->children(root)) {
      if (!Lnast_ntype::is_stmts(w->get_type(stmts))) {
        continue;
      }
      for (auto stmt : w->children(stmts)) {
        auto c0 = w->get_first_child(stmt);
        if (c0.is_invalid()) {
          continue;
        }
        auto c1 = w->get_sibling_next(c0);
        if (c1.is_invalid()) {
          continue;
        }
        if (Lnast_ntype::is_attr_set(w->get_type(stmt)) && w->get_name(c1) == "__pub") {
          auto c2 = w->get_sibling_next(c1);
          if (!c2.is_invalid()) {
            std::string kind(w->get_name(c2));
            if (kind.size() >= 2 && kind.front() == '\'' && kind.back() == '\'') {
              kind = kind.substr(1, kind.size() - 2);
            }
            pubs.push_back({std::string(w->get_name(c0)), kind});
          }
          continue;
        }
        if (Lnast_ntype::is_store(w->get_type(stmt))) {
          std::string val(w->get_name(c1));
          // A lambda url leaf ('ln:u.tree') is re-bound by the pubs loop
          // (tree-name string); skip it here.
          if (val.starts_with("'ln:")) {
            continue;
          }
          values.emplace_back(std::string(w->get_name(c0)), std::move(val));
        }
      }
    }
    build_namespace(text, values, pubs);
    return;
  }

  // Same-invocation: the source unit publishes through its Lnast side
  // channels — pub_list_ from the parse, pub_values_ stamped by THIS pass
  // when the unit's file-scope walk completed. Values still pending →
  // defer (whole-file retry once the exporter finishes).
  if (auto uit = function_registry.find(text); uit != function_registry.end() && uit->second->get_lambda_kind().empty()) {
    const auto& src  = uit->second;
    const auto& pubs = src->get_pub_list();
    bool        needs_values = false;
    for (const auto& p : pubs) {
      if (p.kind == "value") {
        needs_values = true;
        break;
      }
    }
    if (needs_values && src->get_pub_values().empty()) {
      pend();  // exporter not yet completed this invocation
      return;
    }
    build_namespace(text, src->get_pub_values(), pubs);
    return;
  }

  pend();  // no matching unit among the explicit inputs
}

}  // namespace upass::call_resolver
