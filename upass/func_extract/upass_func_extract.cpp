//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_func_extract.hpp"

#include <functional>
#include <utility>

#include "dlop.hpp"
#include "lnast_ntype.hpp"

static upass::uPass_plugin plugin_func_extract("func_extract", upass::uPass_wrapper<uPass_func_extract>::get_upass);

uPass_func_extract::uPass_func_extract(std::shared_ptr<upass::Lnast_manager>& _lm) : uPass(_lm) {}

std::string uPass_func_extract::strip_io_prefix(std::string_view name) { return std::string(name); }

void uPass_func_extract::copy_current_subtree(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent) {
  const auto type       = lm->current_type();
  auto       new_parent = (Lnast_ntype::is_ref(type) || Lnast_ntype::is_const(type)) ? dst->add_child(parent, lm->current_node())
                                                                                     : dst->add_child(parent, type);
  if (!lm->has_child()) {
    return;
  }

  move_to_child();
  do {
    copy_current_subtree(dst, new_parent);
  } while (move_to_sibling());
  move_to_parent();
}

void uPass_func_extract::copy_current_children(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent) {
  if (!lm->has_child()) {
    return;
  }

  move_to_child();
  do {
    copy_current_subtree(dst, parent);
  } while (move_to_sibling());
  move_to_parent();
}

// Copy the func_def's input/output `tuple_add(assign(ref,default,type?)...)`
// signature subtree into the extracted lnast under `io_idx`. The shape is
// preserved verbatim: composite tuple types stay as a `tuple_add` 3rd child
// of their assign. SSA will later expand those into flat dotted leaves.
bool uPass_func_extract::emit_io_tuple_from_decl(const std::shared_ptr<Lnast>& dst, const Lnast_nid& io_idx) {
  if (!lm->has_child()) {
    // Empty signature slot. Emit `tuple_add(ref '__empty_tuple')` so
    // constprop's inline path (which keys on the first-child ref name —
    // __input_tuple_ref / __output_tuple_ref / __empty_tuple — to read
    // signatures back) sees a marker rather than a shapeless tuple_add.
    // Without it, callees with no inputs look identical to a malformed
    // tuple_add and constprop falls through to its body-statement path.
    auto tup_idx = dst->add_child(io_idx, Lnast_ntype::create_tuple_add());
    dst->add_child(tup_idx, Lnast_node::create_ref("__empty_tuple"));
    return false;
  }
  auto tup_idx = dst->add_child(io_idx, Lnast_ntype::create_tuple_add());
  copy_current_children(dst, tup_idx);
  return true;
}

void uPass_func_extract::process_stmts() { ++stmts_depth; }

void uPass_func_extract::process_stmts_post() { --stmts_depth; }

// Resolve a child cursor node to a Const. Const literals decode via
// from_pyrope; refs resolve through latest_outer_value first (named outer
// scalars), then temp_scalar_value (SSA temps). Returns nullopt on
// anything we can't statically resolve.
static std::optional<Const> resolve_child_scalar(const std::string& name, bool is_ref, bool is_const,
                                                 const std::unordered_map<std::string, Const>& latest,
                                                 const std::unordered_map<std::string, Const>& temps) {
  if (is_const) {
    try {
      return *Dlop::from_pyrope(name);
    } catch (...) {
      return std::nullopt;
    }
  }
  if (is_ref) {
    auto it = latest.find(name);
    if (it != latest.end()) {
      return it->second;
    }
    auto it2 = temps.find(name);
    if (it2 != temps.end()) {
      return it2->second;
    }
  }
  return std::nullopt;
}

// Walk a (lhs, args...) shaped stmt and fold via `op` if all args resolve to
// scalars. Sets temp_scalar_value[lhs] on success. Captures from outer-scope
// constants and prior temp folds; bails otherwise. Used by every binary/n-ary
// arithmetic process_* hook (plus, minus, mult, ...) so derived comptime
// values like `const DERIVED = K + A` survive the func_extract walk.
template <typename Op>
static void fold_temp_nary(upass::Lnast_manager* lm, Op op, const std::unordered_map<std::string, Const>& latest_outer_value,
                           const std::unordered_map<std::string, Const>& temp_scalar_value,
                           std::unordered_map<std::string, Const>&       out_temps) {
  if (!lm->has_child()) {
    return;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  if (!Lnast_ntype::is_ref(lm->current_type())) {
    lm->restore_cursor(saved);
    return;
  }
  std::string lhs_name(lm->current_text());
  if (!lm->move_to_sibling()) {
    lm->restore_cursor(saved);
    return;
  }
  std::optional<Const> acc = resolve_child_scalar(std::string(lm->current_text()),
                                                  Lnast_ntype::is_ref(lm->current_type()),
                                                  Lnast_ntype::is_const(lm->current_type()),
                                                  latest_outer_value,
                                                  temp_scalar_value);
  if (!acc.has_value() || acc->is_invalid()) {
    lm->restore_cursor(saved);
    return;
  }
  while (lm->move_to_sibling()) {
    auto rhs = resolve_child_scalar(std::string(lm->current_text()),
                                    Lnast_ntype::is_ref(lm->current_type()),
                                    Lnast_ntype::is_const(lm->current_type()),
                                    latest_outer_value,
                                    temp_scalar_value);
    if (!rhs.has_value() || rhs->is_invalid()) {
      lm->restore_cursor(saved);
      return;
    }
    try {
      *acc = op(*acc, *rhs);
    } catch (...) {
      lm->restore_cursor(saved);
      return;
    }
    if (acc->is_invalid()) {
      lm->restore_cursor(saved);
      return;
    }
  }
  out_temps[lhs_name] = *acc;
  lm->restore_cursor(saved);
}

void uPass_func_extract::process_plus() {
  fold_temp_nary(
      lm.get(),
      [](const Const& a, const Const& b) { return *a.add_op(b); },
      latest_outer_value,
      temp_scalar_value,
      temp_scalar_value);
}
void uPass_func_extract::process_minus() {
  fold_temp_nary(
      lm.get(),
      [](const Const& a, const Const& b) { return *a.sub_op(b); },
      latest_outer_value,
      temp_scalar_value,
      temp_scalar_value);
}
void uPass_func_extract::process_mult() {
  fold_temp_nary(
      lm.get(),
      [](const Const& a, const Const& b) { return *a.mult_op(b); },
      latest_outer_value,
      temp_scalar_value,
      temp_scalar_value);
}
void uPass_func_extract::process_div() {
  fold_temp_nary(
      lm.get(),
      [](const Const& a, const Const& b) { return *a.div_op(b); },
      latest_outer_value,
      temp_scalar_value,
      temp_scalar_value);
}
void uPass_func_extract::process_bit_and() {
  fold_temp_nary(
      lm.get(),
      [](const Const& a, const Const& b) { return *a.and_op(b); },
      latest_outer_value,
      temp_scalar_value,
      temp_scalar_value);
}
void uPass_func_extract::process_bit_or() {
  fold_temp_nary(
      lm.get(),
      [](const Const& a, const Const& b) { return *a.or_op(b); },
      latest_outer_value,
      temp_scalar_value,
      temp_scalar_value);
}
void uPass_func_extract::process_bit_xor() {
  fold_temp_nary(
      lm.get(),
      [](const Const& a, const Const& b) { return *a.xor_op(b); },
      latest_outer_value,
      temp_scalar_value,
      temp_scalar_value);
}

// Capture tuple-literal bundles produced at the outer scope. Shape:
//   tuple_add(ref ___N, assign(ref key, const|ref val) ...)
// Only purely-named entries with const (or already-known temp) values
// participate; anything positional/unresolved skips the temp.
void uPass_func_extract::process_tuple_add() {
  if (!lm->has_child()) {
    return;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  if (!Lnast_ntype::is_ref(lm->current_type())) {
    lm->restore_cursor(saved);
    return;
  }
  std::string                            lhs_name(lm->current_text());
  std::unordered_map<std::string, Const> fields;
  while (lm->move_to_sibling()) {
    if (!Lnast_ntype::is_store(lm->current_type()) || !lm->has_child()) {
      lm->restore_cursor(saved);
      return;
    }
    lm->move_to_child();
    if (!Lnast_ntype::is_ref(lm->current_type())) {
      lm->move_to_parent();
      lm->restore_cursor(saved);
      return;
    }
    std::string key(lm->current_text());
    if (!lm->move_to_sibling()) {
      lm->move_to_parent();
      lm->restore_cursor(saved);
      return;
    }
    auto val = resolve_child_scalar(std::string(lm->current_text()),
                                    Lnast_ntype::is_ref(lm->current_type()),
                                    Lnast_ntype::is_const(lm->current_type()),
                                    latest_outer_value,
                                    temp_scalar_value);
    lm->move_to_parent();
    if (!val.has_value() || val->is_invalid()) {
      lm->restore_cursor(saved);
      return;
    }
    fields[key] = *val;
  }
  if (!fields.empty()) {
    temp_bundle_value[lhs_name] = std::move(fields);
  }
  lm->restore_cursor(saved);
}

// Task 1m — record `fcall(___N, 'import', '<str>')` so a following
// `store NAME ___N` can promote NAME to an import-binding capture
// (latest_outer_import). Only the canonical const-callee shape matches.
void uPass_func_extract::process_func_call() {
  if (!lm->has_child()) {
    return;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  std::string dst;
  if (Lnast_ntype::is_ref(lm->current_type())) {
    dst = std::string(lm->current_text());
  }
  if (!dst.empty() && lm->move_to_sibling() && Lnast_ntype::is_const(lm->current_type()) && lm->current_raw_text() == "import"
      && lm->move_to_sibling() && Lnast_ntype::is_const(lm->current_type())) {
    temp_import_text[dst] = std::string(lm->current_raw_text());
  }
  lm->restore_cursor(saved);
}

void uPass_func_extract::process_assign() {
  // Definition-time closure-capture tracking. A name is recorded as a
  // visible constant for any comb defined later in this scope IFF its
  // only write so far was an unconditional, top-level `assign <ref>
  // <const>`. Any of the following pushes it into `outer_non_const`
  // permanently, so it never escapes the function boundary:
  //   * RHS is not a trivial Const (ref, computed, bundle).
  //   * The assign sits inside any nested stmts (an if-arm with a stmts
  //     wrapper, a loop body, a scope_statement, …).
  //   * The assign sits directly under an `if` node (the flat
  //     when/unless gated form — `c = 100 when cond`).
  //   * A second write to the same name (any shape, any place).
  if (!lm->has_child()) {
    return;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  std::string lhs_name;
  if (Lnast_ntype::is_ref(lm->current_type())) {
    lhs_name = std::string(lm->current_text());
  }
  if (lhs_name.empty() || !lm->move_to_sibling()) {
    lm->restore_cursor(saved);
    return;
  }
  const bool  rhs_is_const = Lnast_ntype::is_const(lm->current_type());
  const bool  rhs_is_ref   = Lnast_ntype::is_ref(lm->current_type());
  std::string rhs_text;
  if (rhs_is_const || rhs_is_ref) {
    rhs_text = std::string(lm->current_text());
  }
  lm->restore_cursor(saved);

  auto invalidate = [&]() {
    latest_outer_value.erase(lhs_name);
    latest_outer_bundle.erase(lhs_name);
    latest_outer_import.erase(lhs_name);
    outer_non_const.insert(lhs_name);
  };

  if (outer_non_const.contains(lhs_name)) {
    return;
  }
  // Second write to the same name → no longer a stable constant.
  if (latest_outer_value.contains(lhs_name) || latest_outer_bundle.contains(lhs_name) || latest_outer_import.contains(lhs_name)) {
    invalidate();
    return;
  }
  if (stmts_depth > 1) {
    invalidate();
    return;
  }
  if (rhs_is_const) {
    try {
      latest_outer_value[lhs_name] = *Dlop::from_pyrope(rhs_text);
    } catch (...) {
      invalidate();
    }
    return;
  }
  if (rhs_is_ref) {
    // RHS is a ref to either a previously-folded scalar temp (DERIVED = ___2
    // where ___2 came from `plus K A`) or a tuple-literal temp (CFG = ___1
    // where ___1 came from `tuple_add(gain=2, offset=5)`). Both feed the
    // capture prelude — scalars via latest_outer_value, bundles via
    // latest_outer_bundle.
    auto sit = temp_scalar_value.find(rhs_text);
    if (sit != temp_scalar_value.end()) {
      latest_outer_value[lhs_name] = sit->second;
      return;
    }
    auto bit = temp_bundle_value.find(rhs_text);
    if (bit != temp_bundle_value.end()) {
      latest_outer_bundle[lhs_name] = bit->second;
      return;
    }
    // Task 1m — `store b ___N` where ___N is an import-call dst: capture the
    // import string so bodies reading `b` get the call replicated.
    auto imit = temp_import_text.find(rhs_text);
    if (imit != temp_import_text.end()) {
      latest_outer_import[lhs_name] = imit->second;
      return;
    }
    // Aliasing an existing outer-scope value (`COPY = K`) is also a
    // legitimate comptime capture; propagate without re-reading the temp
    // map. Bundle alias is rarer but handled symmetrically.
    auto lvit = latest_outer_value.find(rhs_text);
    if (lvit != latest_outer_value.end()) {
      latest_outer_value[lhs_name] = lvit->second;
      return;
    }
    auto lbit = latest_outer_bundle.find(rhs_text);
    if (lbit != latest_outer_bundle.end()) {
      latest_outer_bundle[lhs_name] = lbit->second;
      return;
    }
  }
  invalidate();
}

void uPass_func_extract::collect_body_refs(const std::shared_ptr<Lnast>& body, std::unordered_set<std::string>& refs) const {
  if (!body) {
    return;
  }
  std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
    if (nid.is_invalid()) {
      return;
    }
    if (Lnast_ntype::is_ref(body->get_type(nid))) {
      refs.insert(std::string(body->get_name(nid)));
    }
    for (auto c = body->get_child(nid); !c.is_invalid(); c = body->get_sibling_next(c)) {
      walk(c);
    }
  };
  walk(body->get_root());
}

void uPass_func_extract::process_func_def() {
  drop_current_func_def = false;

  if (!lm->has_child()) {
    return;
  }

  move_to_child();
  const auto func_name = std::string(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  const auto func_kind = std::string(current_text());

  // Task 1q — `pipe` lambdas extract like `comb`: the spawned io-form LNAST
  // carries the per-output stages(min,max) annotation (copied verbatim with
  // the signature below), the LN pipe upass inserts the output flop, and
  // tolg lowers it. Task 1r — plain `mod` lambdas extract too (each mod is
  // its own module/partition; calls to it lower to instances in a later
  // phase). A `ref self` mod is DIFFERENT: that is a method / mod-init
  // constructor (`mod mix_tup_init(ref self:Mix_tup)`,
  // 06b-instantiation.md), not a standalone hardware module — it keeps the
  // method-splice path: the runner's inliner needs it in the registry to
  // splice `mut y:Mix_tup = mix_tup_init` (and explicit `y.method(...)`
  // calls). Both stamp the durable lambda kind on the extracted Lnast; the
  // ref-self method shape is recognized downstream by its `self` io entry.
  if ((func_kind != "comb" && func_kind != "pipe" && func_kind != "mod") || func_name.empty()) {
    move_to_parent();
    return;
  }

  const auto extracted_name = std::string(lm->get_top_module_name()) + "." + func_name;
  drop_current_func_def     = true;

  if (extracted_names.contains(extracted_name)) {
    move_to_parent();
    return;
  }
  extracted_names.insert(extracted_name);

  auto new_lnast = std::make_shared<Lnast>(extracted_name);
  // Task 1r — durable kind: downstream consumers (uPass_pipe, the inliner's
  // mod decline, the future Sub-instance lowering) gate on this, never on
  // stages_min>0. A ref-self mod keeps kind "mod" but is recognized as a
  // method by its `self` io entry.
  new_lnast->set_lambda_kind(func_kind);
  auto root_nid  = new_lnast->set_root(Lnast_ntype::create_top());

  // Skip generics for this first comb-only extraction slice. The func_def
  // shape after the captures-slot removal is:
  //   func_def(name, kind, generics, inputs, outputs, stmts)
  // so from `kind` we need two sibling steps to reach `inputs`.
  // No signature => bare `top -> stmts` (no `io` child).
  if (!move_to_sibling() || !move_to_sibling()) {
    new_lnast->add_child(root_nid, Lnast_ntype::create_stmts());
    move_to_parent();
    extracted_lnasts.emplace_back(std::move(new_lnast));
    return;
  }

  // Signature present => `top -> [io, stmts]`. The io node directly contains
  // two tuple_add children — the inputs and outputs signatures copied
  // verbatim from the func_def (composite tuple-typed entries still nested).
  // SSA later rewrites tuple-typed entries into flat dotted-leaf form.
  auto io_idx = new_lnast->add_child(root_nid, Lnast_ntype::create_io());
  auto stmts  = new_lnast->add_child(root_nid, Lnast_ntype::create_stmts());

  emit_io_tuple_from_decl(new_lnast, io_idx);

  if (move_to_sibling()) {
    emit_io_tuple_from_decl(new_lnast, io_idx);
  } else {
    // No outputs slot in the source func_def — emit
    // `tuple_add(ref '__empty_tuple')` so io always has the
    // [inputs, outputs] pair and downstream readers can distinguish
    // "no slot" from "empty slot".
    auto tup_idx = new_lnast->add_child(io_idx, Lnast_ntype::create_tuple_add());
    new_lnast->add_child(tup_idx, Lnast_node::create_ref("__empty_tuple"));
  }

  if (move_to_sibling()) {
    // Closure capture for `comb`: any outer name the body reads and whose
    // current value was set by a trivial-Const `assign` in the surrounding
    // scope gets inlined at the head of the extracted body's stmts. The
    // copied body content lands AFTER these capture stmts, so constprop's
    // ordinary top-down walk sees the binding before the use — no
    // cross-pass closure plumbing, no extra branch in `try_eval_comb_call`,
    // no special symbol-table rule. Non-trivial outer bindings (bundles,
    // unresolved refs) are absent from `latest_outer_value` by
    // construction, so they naturally don't cross the function boundary —
    // that's the "constants flow up, non-constants stop at the function
    // scope" rule, expressed structurally.
    if (!latest_outer_value.empty() || !latest_outer_bundle.empty() || !latest_outer_import.empty()) {
      // Scan the SOURCE body (`lm` cursor is at the func_def's stmts) for
      // every name it reads, so we only inline captures the body actually
      // consumes.
      std::unordered_set<std::string> body_refs;
      {
        const auto&                           src      = lm->get_lnast();
        const auto                            body_nid = lm->get_current_nid();
        std::function<void(const Lnast_nid&)> walk     = [&](const Lnast_nid& nid) {
          if (nid.is_invalid()) {
            return;
          }
          if (Lnast_ntype::is_ref(src->get_type(nid))) {
            body_refs.insert(std::string(src->get_name(nid)));
          }
          for (auto c = src->get_child(nid); !c.is_invalid(); c = src->get_sibling_next(c)) {
            walk(c);
          }
        };
        walk(body_nid);
      }
      for (const auto& name : body_refs) {
        auto it = latest_outer_value.find(name);
        if (it != latest_outer_value.end()) {
          auto a_idx = new_lnast->add_child(stmts, Lnast_ntype::create_store());
          new_lnast->add_child(a_idx, Lnast_node::create_ref(name));
          new_lnast->add_child(a_idx, Lnast_node::create_const(it->second.to_pyrope()));
          continue;
        }
        // Task 1m — import bindings replicate as the import CALL itself
        // (resolution happens later, in constprop, against the registry):
        // `fcall(ref name, 'import', '<str>')` binds the namespace bundle /
        // lambda ref inside this body exactly like at file scope.
        if (auto iit = latest_outer_import.find(name); iit != latest_outer_import.end()) {
          auto f_idx = new_lnast->add_child(stmts, Lnast_ntype::create_func_call());
          new_lnast->add_child(f_idx, Lnast_node::create_ref(name));
          new_lnast->add_child(f_idx, Lnast_node::create_const("import"));
          new_lnast->add_child(f_idx, Lnast_node::create_const(iit->second));
          continue;
        }
        auto bit = latest_outer_bundle.find(name);
        if (bit != latest_outer_bundle.end()) {
          // Materialize the bundle as flat dotted-leaf assigns
          // (`assign CFG.gain 2`). The inliner's local_values is already
          // keyed by these dotted paths, so a downstream
          // `tuple_get ___N CFG gain` resolves with no extra plumbing.
          for (const auto& [field, val] : bit->second) {
            auto a_idx = new_lnast->add_child(stmts, Lnast_ntype::create_store());
            new_lnast->add_child(a_idx, Lnast_node::create_ref(name + "." + field));
            new_lnast->add_child(a_idx, Lnast_node::create_const(val.to_pyrope()));
          }
        }
      }
    }
    copy_current_children(new_lnast, stmts);
  }

  move_to_parent();
  extracted_lnasts.emplace_back(std::move(new_lnast));
}

upass::Emit_decision uPass_func_extract::classify_statement() {
  if (!drop_current_func_def) {
    return upass::Emit_decision::emit_node();
  }

  drop_current_func_def = false;
  return upass::Emit_decision::drop();
}

std::vector<std::shared_ptr<Lnast>> uPass_func_extract::take_new_lnasts() { return std::move(extracted_lnasts); }
