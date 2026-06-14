//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// ── Design note: dead-code elimination & (future) demand-driven emit ─────────
//
// The post-walk DCE below (dead_code_eliminate_staging) is textbook aggressive
// dead-code elimination on SSA: mark live from a fixed set of roots, sweep the
// rest. Roots are the nodes that cannot be dropped or reordered away — function
// IO (declared at the function boundary, not via variable writes), side-effecting
// statements (cassert always; cputs/func_call unless their dst const-folds), and
// state elements (attr_set type='reg'|'mut'). Everything else — every pure value
// def, named or temporary — is dead unless transitively demanded by a root.
//
// The intended evolution is to compute that liveness *demand-first* instead of
// mark-then-sweep, and to materialize each pure def lazily at its use site
// (folding to a const when possible, else copying the node from the source
// tree). That merges constprop's emit, the const-at-use fold, and this DCE into
// one pass and yields free sinking. In compiler terms this is late scheduling of
// a sea-of-nodes: "roots" are pinned nodes, pure value defs float and are
// scheduled near their uses, and an unscheduled (undemanded) node is simply
// never emitted. Place a floated def at the dominator-tree LCA of all its uses
// (then sink as deep as legal) — NOT at the textual first use, which is wrong for
// a value consumed in multiple branches.
//
// Why total reorder is safe here: our side effects are clean. There are no
// memory references or aliasing, so every read-after-write dependence is visible
// through SSA names (upass/ssa renames mut writes apart). That is what licenses
// free code motion of pure defs. CAUTION: when arrays land, an array index
// reintroduces alias/ordering that resembles a memory access — write a[i] vs read
// a[j] cannot be reordered without proving i != j. Treat aliasing array ops as
// pinned/ordered and revisit this assumption before relying on total reorder for
// array-bearing code.
//
// References:
//   Click & Paleczny, "A Simple Graph-Based Intermediate Representation"
//     (sea of nodes), ACM SIGPLAN Workshop on IR (IR'95).
//   Click, "Global Code Motion / Global Value Numbering", PLDI'95 (schedule
//     floating nodes at the dominator LCA of uses, then sink out of loops).
//   Knoop, Rüthing & Steffen, "Partial Dead Code Elimination", PLDI'94 (sink
//     assignments toward uses; eliminate on paths that never use them).
//   Knoop, Rüthing & Steffen, "Lazy Code Motion", PLDI'92 (as-late-as-possible
//     placement machinery).
//   Cytron, Ferrante, Rosen, Wegman & Zadeck, "Efficiently Computing SSA Form
//     and the Control Dependence Graph", ACM TOPLAS 1991 (SSA + basis for SSA DCE).
//   Wegman & Zadeck, "Constant Propagation with Conditional Branches" (SCCP),
//     ACM TOPLAS 1991 (const-fold lattice paired with the DCE sweep).
//   Weise, Crew, Ernst & Steensgaard, "Value Dependence Graphs: Representation
//     Without Taxation", POPL'94 (codegen = scheduling a demand graph).
//   Cooper & Torczon, "Engineering a Compiler", ch. 10 (mark-sweep aggressive
//     DCE with side-effect roots).
// ─────────────────────────────────────────────────────────────────────────────

#include "upass_runner.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "call_resolver.hpp"
#include "decl_facts.hpp"
#include "diag.hpp"

namespace {

// Emit a function-call argument diagnostic (06-functions.md §"Argument naming")
// and abort the walk. The record is flushed to the JSONL sink crash-safe before
// the throw unwinds up to main's top-level catch, so the error-test harness sees
// it even though the throw aborts the remaining pipeline stages.
[[noreturn]] void fcall_arg_fail(const livehd::diag::Span& span, std::string_view code, const std::string& msg,
                                 std::string_view hint) {
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = std::string{code},
                                                     .category = "name",
                                                     .pass     = "upass.runner",
                                                     .message  = msg,
                                                     .span     = span,
                                                     .hint     = std::string{hint}});
  throw std::runtime_error(msg);
}

bool prp_is_tmp_name(std::string_view n) { return n.size() >= 3 && n[0] == '_' && n[1] == '_' && n[2] == '_'; }

// prp2lnast wraps the UFCS receiver of `obj.method(...)` in a
// `store(__ufcs_arg, obj)` marker (positional, like __ref_arg) so the runner
// can reject the UFCS form when the callee declares no `self`.
constexpr std::string_view call_ufcs_arg_marker = "__ufcs_arg";

// prp2lnast wraps each explicit call-site generic binding (`f<int,string>(…)`)
// in a `store(__generic_arg, type_ref)` marker, in declaration order, ahead of
// the normal actuals. The type_ref names a `'type'` declare tmp (primitive /
// constrained types) or a named type directly.
constexpr std::string_view call_generic_arg_marker = "__generic_arg";

// prp2lnast wraps a call-argument spread (`f(..., ...rest)`) in a
// `store(__spread_arg, rest)` marker; the runner expands rest's bundle fields
// into named (non-numeric key) / positional (numeric key) actuals at gather.
constexpr std::string_view call_spread_arg_marker = "__spread_arg";

// Collect the NON-temporary variables that the `while` CONDITION reads
// (transitively). `cond_ref` is the while's child-0 ref name; we trace its
// definition backward through the while's preceding siblings (the cond
// computation `ne ___1 c 0`, `and ___3 ___1 ___2`, …), gathering every non-temp
// operand. These are exactly the variables that decide termination: if they
// recur with the same values, the condition recurs and the loop can never exit.
//
// Tracking ONLY the condition's inputs (rather than every loop variable) is
// deliberate: (1) it catches a frozen condition even when other vars diverge
// (`while c != 0 { d += 1 }`), and (2) it never folds an accumulator at the loop
// boundary — folding a built-up accumulator there perturbs constprop's deferred
// writes and can drop the real loop-carried update.
void collect_cond_vars(const Lnast& ln, const Lnast_nid& while_nid, std::string_view cond_ref, bool cond_is_ref,
                       absl::flat_hash_set<std::string>& out) {
  if (!cond_is_ref || cond_ref.empty()) {
    return;  // literal condition (`while true`/`false`) — no input vars to track
  }
  if (!prp_is_tmp_name(cond_ref)) {
    out.emplace(cond_ref);  // `while flag { … }` — the bare var is the only input
    return;
  }
  absl::flat_hash_set<std::string> needed;
  needed.emplace(cond_ref);
  for (auto s = ln.get_sibling_prev(while_nid); !s.is_invalid(); s = ln.get_sibling_prev(s)) {
    const auto def = ln.get_first_child(s);
    if (def.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(def)) || !needed.contains(std::string(ln.get_name(def)))) {
      continue;  // this statement does not define a temp the condition (transitively) needs
    }
    const bool is_call = Lnast_ntype::is_func_call(ln.get_type(s));
    int        idx     = 0;
    for (auto c : ln.children(s)) {
      // operands are children after the def (child 0); skip a func_call callee (child 1)
      if (idx >= 1 && !(is_call && idx == 1) && Lnast_ntype::is_ref(ln.get_type(c))) {
        const auto nm = ln.get_name(c);
        if (!nm.empty()) {
          if (prp_is_tmp_name(nm)) {
            needed.emplace(nm);  // chase this temp's own definition further back
          } else {
            out.emplace(nm);
          }
        }
      }
      ++idx;
    }
  }
}

// Collect every NON-tmp variable WRITTEN (store/declare target) anywhere in the
// loop body subtree, excluding nested lambda (func_def) bodies. Used to broaden
// the non-termination state signature: a cond-var-only signature false-positives
// a loop whose exit flag flips late — `while not done { n+=1; if n==8 {done=true} }`
// (done is frozen for iterations 1..7) — even though a real progress var (n)
// advances. Including the body's written vars makes a state REPEAT mean genuine
// non-progress; a still-advancing var simply never repeats (bounded by the fuel cap).
void collect_body_assigned_vars(const Lnast& ln, const Lnast_nid& nid, absl::flat_hash_set<std::string>& out) {
  if (nid.is_invalid()) {
    return;
  }
  const auto t = ln.get_type(nid);
  if (Lnast_ntype::is_func_def(t)) {
    return;  // a nested lambda's writes are its own scope, not loop-carried
  }
  if (Lnast_ntype::is_store(t) || Lnast_ntype::is_declare(t)) {
    const auto def = ln.get_first_child(nid);
    if (!def.is_invalid() && Lnast_ntype::is_ref(ln.get_type(def))) {
      const auto nm = ln.get_name(def);
      if (!nm.empty() && !prp_is_tmp_name(nm)) {
        out.emplace(nm);
      }
    }
  }
  for (auto c : ln.children(nid)) {
    collect_body_assigned_vars(ln, c, out);
  }
}

// Emit a fatal comptime-loop diagnostic anchored at the `while` node's source
// span (prp2lnast's attach_loc survives the lnastfmt round-trip), then abort the
// walk — mirrors fcall_arg_fail's crash-safe flush-before-throw.
[[noreturn]] void loop_fail(const livehd::diag::Span& span, std::string_view code, const std::string& msg, std::string_view hint) {
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = std::string{code},
                                                     .category = "comptime",
                                                     .pass     = "upass.runner",
                                                     .message  = msg,
                                                     .span     = span,
                                                     .hint     = std::string{hint}});
  throw std::runtime_error(msg);
}

}  // namespace

uPass_runner::uPass_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string>& upass_names,
                           upass::Options_map options)
    : uPass_struct(_lm) {
  root_lnast_                = _lm->get_lnast();  // no inline frame is active at construction
  auto        upass_registry = upass::uPass_plugin::get_registry();
  std::string order_error;
  auto        resolved = resolve_order(upass_names, &order_error);
  if (!order_error.empty()) {
    configuration_error     = true;
    configuration_error_msg = order_error;
  }
  if (!resolved.empty()) {
    std::print("uPass - resolved order:");
    for (const auto& name : resolved) {
      std::print(" {}", name);
    }
    std::print("\n");
  }

  for (const auto& name : resolved) {
    const auto it = upass_registry.find(name);
    if (it == upass_registry.end()) {
      std::print("{} is not defined.\n", name);
      continue;
    }

    std::print("uPass - add {}\n", name);
    upasses.emplace_back(Pass_entry{.name = name, .pass = it->second.setup_fn(_lm)});
  }

  // Wire runner-backed fold callback + options into every pass. Done once,
  // after all passes are constructed, so the callback sees the full pass
  // list and each pass can pick the options it recognizes.
  auto emit_at_fn = [this](const Lnast_nid& src) { emit_op_with_fold_at(src); };
  for (auto& entry : upasses) {
    entry.pass->set_runner_emit_at_fn(emit_at_fn);
    entry.pass->set_runner_symbol_table(&symbol_table_);  // One shared scope-aware table
    entry.pass->set_options(options);
  }
  set_runner_symbol_table(&symbol_table_);  // the runner's own uPass surface sees it too

  // Pre-cache pass subsets so the hot any_pass_drops loop only visits
  // passes that actually override the relevant virtual.
  classify_capable_passes.reserve(upasses.size());
  for (auto& entry : upasses) {
    if (entry.pass->overrides_classify_statement()) {
      classify_capable_passes.push_back(entry.pass.get());
    }
  }
}

std::vector<std::string> uPass_runner::resolve_order(const std::vector<std::string>& requested_names,
                                                     std::string*                    error_msg) const {
  const auto& upass_registry = upass::uPass_plugin::get_registry();

  enum class Mark { kUnseen, kVisiting, kDone };
  std::unordered_map<std::string, Mark> marks;
  std::vector<std::string>              ordered;

  std::function<bool(const std::string&)> dfs = [&](const std::string& name) {
    const auto it = upass_registry.find(name);
    if (it == upass_registry.end()) {
      std::print("{} is not defined.\n", name);
      if (error_msg && error_msg->empty()) {
        *error_msg = std::format("unknown pass '{}'", name);
      }
      return false;
    }

    const auto mit = marks.find(name);
    if (mit != marks.end()) {
      if (mit->second == Mark::kVisiting) {
        std::print(stderr, "uPass dependency cycle detected at {}\n", name);
        if (error_msg && error_msg->empty()) {
          *error_msg = std::format("dependency cycle detected at '{}'", name);
        }
        return false;
      }
      return mit->second == Mark::kDone;
    }

    marks.emplace(name, Mark::kVisiting);
    for (const auto& dep : it->second.depends_on) {
      if (!dfs(dep)) {
        std::print(stderr, "uPass dependency chain for {} is invalid\n", name);
        if (error_msg && error_msg->empty()) {
          *error_msg = std::format("dependency chain for '{}' is invalid", name);
        }
        marks[name] = Mark::kDone;
        return false;
      }
    }
    marks[name] = Mark::kDone;
    ordered.emplace_back(name);
    return true;
  };

  for (const auto& name : requested_names) {
    dfs(name);
  }

  return ordered;
}

// ── Staging emit helpers ──────────────────────────────────────────────────────

void uPass_runner::carry_srcid(const Lnast_nid& staged) {
  const auto& src_ln = lm->get_lnast();
  auto        nid    = lm->get_current_nid();
  auto        id     = src_ln->get_srcid(nid);
  // The read cursor's node may be a child of the id-bearing statement (and a
  // scratch tree carries its id on the ROOT only) — walk up so synthesized
  // nodes really inherit the enclosing statement's id, as documented.
  while (id == hhds::SourceId_invalid && nid.is_valid()) {
    nid = src_ln->get_parent(nid);
    if (!nid.is_valid()) {
      break;
    }
    id = src_ln->get_srcid(nid);
  }
  if (id == hhds::SourceId_invalid) {
    return;
  }
  if (src_ln.get() != root_lnast_.get()) {
    // Inline frame: the id was minted in the callee Lnast's locator — re-mint
    // it into the root's so it stays resolvable after replace_body.
    id = root_lnast_->source_locator().import_from(src_ln->source_locator(), id);
    // Combined id for spliced bodies: primary anchor = the callee
    // def (the diag span), secondary = the call site (rendered as a note).
    // Nested inlining composes naturally — the inner call site is itself a
    // combine whose parents chain to the outer one.
    if (id != hhds::SourceId_invalid && !inline_call_sites_.empty()) {
      const auto call_site = inline_call_sites_.back();
      if (call_site != hhds::SourceId_invalid && call_site != id) {
        id = root_lnast_->source_locator().combine({id, call_site});
      }
    }
  }
  staging->set_srcid(staged, id);
}

void uPass_runner::stamp_scratch_srcid(const std::shared_ptr<Lnast>& scratch, const Lnast_nid& root) {
  const auto& src_ln = lm->get_lnast();
  auto        nid    = lm->get_current_nid();
  auto        id     = src_ln->get_srcid(nid);
  while (id == hhds::SourceId_invalid && nid.is_valid()) {
    nid = src_ln->get_parent(nid);
    if (!nid.is_valid()) {
      break;
    }
    id = src_ln->get_srcid(nid);
  }
  if (id == hhds::SourceId_invalid) {
    return;
  }
  // Re-mint into the scratch tree's own locator: carry_srcid resolves the id
  // through the SOURCE tree's locator when the scratch walk emits.
  id = scratch->source_locator().import_from(src_ln->source_locator(), id);
  if (id != hhds::SourceId_invalid) {
    scratch->set_srcid(root, id);
  }
}

void uPass_runner::emit_push(Lnast_ntype::Lnast_ntype_int type) {
  if (!materialize_) {
    // No staging build (toln:0 && tolg:0): keep the parent stack balanced for
    // the matching emit_pop, but append nothing.
    staging_parent_stack.push(staging_parent);
    return;
  }
  auto nid = staging->add_child(staging_parent, type);
  staging_parent_stack.push(staging_parent);
  staging_parent = nid;

  // General carry — one integer attr, copied unconditionally for every
  // def-bearing kind (the old per-node-kind whitelist is gone). A synthesized
  // node whose type differs from the read cursor's inherits the enclosing
  // statement's id, which is the documented fallback anchor.
  if (Lnast::srcid_carries(type)) {
    carry_srcid(nid);
  }
}

void uPass_runner::emit_pop() {
  staging_parent = staging_parent_stack.top();
  staging_parent_stack.pop();
}

void uPass_runner::emit_leaf(Lnast_ntype::Lnast_ntype_int type) {
  if (!materialize_) {
    return;
  }
  auto nid = staging->add_child(staging_parent, type);
  if (Lnast::srcid_carries(type)) {
    carry_srcid(nid);
  }
}

void uPass_runner::emit_leaf(const Lnast_node& node) {
  if (!materialize_) {
    return;
  }
  staging->add_child(staging_parent, node);
}

void uPass_runner::emit_subtree_verbatim() {
  if (!materialize_) {
    return;  // pure emission, no dispatch — cursor untouched (the walk is balanced)
  }
  auto type = lm->current_type();
  if (lm->has_child()) {
    emit_push(type);
    lm->move_to_child();
    do {
      emit_subtree_verbatim();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
    emit_pop();
  } else {
    if (Lnast_ntype::is_ref(type) || Lnast_ntype::is_const(type)) {
      emit_leaf(lm->current_node());
    } else {
      emit_leaf(type);
    }
  }
}

std::optional<Dlop> uPass_runner::try_fold_ref(std::string_view name) {
  // Every pass's fold values land on the runner-owned table now
  // (constprop trivials, attr_get/`is` results, wrap/sat narrowing), so read
  // it directly — with constprop's STRICT inlining gates: is_known_const
  // rejects unknown-carrying values (inlining them broke trivial_if/mem_*
  // LEC), and only a trivial scalar inlines (a multi-entry tuple or a named
  // 1-tuple would silently truncate to its position-0 value).
  return symbol_table_.known_const_scalar(name);
}

std::optional<std::vector<std::pair<std::string, Dlop>>> uPass_runner::try_bundle_fields(std::string_view name) {
  // Direct table read (ex constprop::provide_bundle_fields). A
  // binding with no DATA entries (a declare/type_spec bake created it for
  // its typed facts) is "not a known bundle": an empty field list would make
  // the does-fold and the inliner treat it as a resolved empty tuple.
  auto b = symbol_table_.get_bundle(name);
  if (!b) {
    return std::nullopt;
  }
  if (b->is_empty() || (!b->has_named_top() && b->unnamed_top_count() == 0)) {
    return std::nullopt;
  }
  std::vector<std::pair<std::string, Dlop>> out;
  for (const auto& [k, ep] : b->non_attr_entries()) {
    if (!ep.trivial.is_invalid()) {
      out.emplace_back(k, ep.trivial);
    }
  }
  return out;
}

std::string uPass_runner::try_typename(std::string_view name) {
  // The typename rides the binding ("typename" residual attr).
  const auto b = symbol_table_.get_bundle(name);
  if (!b) {
    return {};
  }
  const auto& v = b->get_attr("typename");
  if (v.is_invalid() || !v.is_string()) {
    return {};
  }
  return v.to_field();
}

std::optional<upass::uPass::Decl_scalar_type> uPass_runner::try_decl_type(std::string_view name) {
  // Only a real `:type` annotation with a concrete integer range
  // qualifies (the inliner leaves the param untyped otherwise).
  const auto f = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), name);
  if (!f || !f->has_type_spec || (!f->range_max && !f->range_min)) {
    return std::nullopt;
  }
  return upass::uPass::Decl_scalar_type{.range_max = f->range_max, .range_min = f->range_min};
}

std::optional<std::tuple<Dlop, Dlop, Dlop>> uPass_runner::try_range(std::string_view name) {
  // Folded range bounds ride the range tmp's binding attrs.
  const auto b = symbol_table_.get_bundle(name);
  if (!b) {
    return std::nullopt;
  }
  const Dlop start = b->get_attr("rng_s");
  if (start.is_invalid()) {
    return std::nullopt;
  }
  Dlop step = b->get_attr("rng_step");  // per-range stride (process_range stamps 1)
  if (step.is_invalid()) {
    step = *Dlop::create_integer(1);  // baseline stride for ranges with no attr
  }
  return std::make_tuple(start, Dlop(b->get_attr("rng_e")), step);
}

std::optional<std::string> uPass_runner::try_tuple_slot_ref(std::string_view name, std::string_view slot) {
  auto it = symbol_table_.tuple_slot_ref.find(std::string(name));
  if (it == symbol_table_.tuple_slot_ref.end()) {
    return std::nullopt;
  }
  auto sit = it->second.find(std::string(slot));
  if (sit == it->second.end()) {
    return std::nullopt;
  }
  return sit->second;
}

std::optional<std::vector<std::pair<std::string, bool>>> uPass_runner::try_tuple_shape(std::string_view name) {
  // Merge the bundle's comptime top-level slots with runtime-only
  // slots (tuple_slot_ref), dedup, positional (numeric asc) before named
  // (alpha) so `for x in t` iterates a stable, source-like order.
  std::vector<std::pair<std::string, bool>> out;
  std::set<std::string>                     seen;
  auto is_positional = [](const std::string& k) { return !k.empty() && k.find_first_not_of("0123456789") == std::string::npos; };
  if (auto b = symbol_table_.get_bundle(name); b) {
    for (const auto& tl : b->top_levels()) {
      std::string key = tl.pos >= 0 ? std::to_string(tl.pos) : std::string(tl.name);
      if (!key.empty() && seen.insert(key).second) {
        out.emplace_back(key, is_positional(key));
      }
    }
  }
  if (auto it = symbol_table_.tuple_slot_ref.find(std::string(name)); it != symbol_table_.tuple_slot_ref.end()) {
    for (const auto& [slot, _] : it->second) {
      if (seen.insert(slot).second) {
        out.emplace_back(slot, is_positional(slot));
      }
    }
  }
  if (out.empty()) {
    return std::nullopt;
  }
  std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) {
      return a.second;
    }
    if (a.second) {
      return a.first.size() != b.first.size() ? a.first.size() < b.first.size() : a.first < b.first;
    }
    return a.first < b.first;
  });
  return out;
}

std::optional<upass::uPass::Field_decl_type> uPass_runner::try_field_type(std::string_view name) {
  // Declared kind + range from the shared derivation.
  const auto f = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), name);
  if (!f || !f->has_type_spec) {
    return std::nullopt;
  }
  upass::uPass::Field_decl_type ft;
  switch (f->kind) {
    case upass::decl_facts::Num::unsigned_int:
    case upass::decl_facts::Num::signed_int  : ft.kind = Io_kind::integer; break;
    case upass::decl_facts::Num::boolean     : ft.kind = Io_kind::boolean; break;
    case upass::decl_facts::Num::string      : ft.kind = Io_kind::string; break;
    case upass::decl_facts::Num::none        : ft.kind = Io_kind::none; break;
  }
  if (ft.kind == Io_kind::none && (f->range_max || f->range_min)) {
    ft.kind = Io_kind::integer;
  }
  ft.range_max = f->range_max;
  ft.range_min = f->range_min;
  return ft;
}

Io_kind uPass_runner::try_scalar_kind(std::string_view name) {
  // The inferred kind lattice off the binding (ex typecheck's
  // provide_scalar_kind): multi-shape → none (a tuple), else the producer-
  // stamped value kind, else the "0" Entry's declared kind.
  const auto b = symbol_table_.get_bundle(name);
  if (!b) {
    return Io_kind::none;
  }
  if (b->has_named_top() || b->unnamed_top_count() > 1) {
    return Io_kind::none;
  }
  upass::Kind k = b->get_value_kind();
  if (k == upass::Kind::unknown) {
    k = b->get_entry("0").kind;
  }
  switch (k) {
    case upass::Kind::integer: return Io_kind::integer;
    case upass::Kind::boolean: return Io_kind::boolean;
    case upass::Kind::string : return Io_kind::string;
    default                  : return Io_kind::none;
  }
}

upass::uPass::Decl_storage uPass_runner::try_decl_storage(std::string_view name) {
  const auto f = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), name);
  if (!f) {
    return upass::uPass::Decl_storage::unknown;
  }
  switch (f->mode) {
    case upass::Mode::mut_kind  : return upass::uPass::Decl_storage::mut_storage;
    case upass::Mode::const_kind: return upass::uPass::Decl_storage::const_storage;
    case upass::Mode::reg_kind  : return upass::uPass::Decl_storage::reg_storage;
    case upass::Mode::await_kind: return upass::uPass::Decl_storage::await_storage;
    case upass::Mode::type_kind : return upass::uPass::Decl_storage::type_storage;
    default                     : return upass::uPass::Decl_storage::unknown;
  }
}

void uPass_runner::check_self_does(const livehd::diag::Span& span, std::string_view callee_name, std::string_view decl_tn,
                                   const Lnast_node& receiver) {
  // Structural `does`-check (07b-structtype.md): every field of the
  // declared self type must exist on the receiver with a matching scalar kind;
  // integer fields additionally need receiver-range ⊆ declared-range. Checks
  // run per flat dotted leaf (bundle keys are canonical dotted paths), which
  // makes the recursion over tuple fields implicit. NEVER a typename
  // comparison — a superset receiver passes.
  auto decl_fields = try_bundle_fields(decl_tn);
  if (!decl_fields || decl_fields->empty()) {
    return;  // unknown / scalar named type — nothing structural to check
  }

  const std::string                                        recv_name(receiver.is_ref() ? receiver.get_name() : std::string_view{});
  const std::string                                        recv_tn = recv_name.empty() ? std::string{} : try_typename(recv_name);
  std::optional<std::vector<std::pair<std::string, Dlop>>> recv_fields;
  if (!recv_name.empty()) {
    recv_fields = try_bundle_fields(recv_name);
  }
  // The receiver's own TYPE bundle (when it has a declared typename) is the
  // reliable field directory: its defaults are always comptime, while a
  // receiver field holding a non-comptime value is skipped by
  // provide_bundle_fields and would otherwise read as "missing".
  std::optional<std::vector<std::pair<std::string, Dlop>>> recv_type_fields;
  if (!recv_tn.empty()) {
    recv_type_fields = try_bundle_fields(recv_tn);
  }

  auto fail = [&](const std::string& why) {
    const std::string recv_disp = recv_name.empty() ? std::string{"<expression>"} : recv_name;
    fcall_arg_fail(span,
                   "fcall-self-does",
                   std::format("receiver `{}`{} does not satisfy `self:{}` in call to `{}`: {}",
                               recv_disp,
                               recv_tn.empty() ? std::string{} : std::format(" (type `{}`)", recv_tn),
                               decl_tn,
                               callee_name,
                               why),
                   "the receiver must structurally provide every field of the declared self type (`does`: same field "
                   "names, matching kinds, integer ranges within the declared bounds)");
  };

  auto find_field
      = [](const std::optional<std::vector<std::pair<std::string, Dlop>>>& fields, std::string_view key) -> const Dlop* {
    if (!fields) {
      return nullptr;
    }
    for (const auto& [k, v] : *fields) {
      if (k == key) {
        return &v;
      }
    }
    return nullptr;
  };

  for (const auto& [fld, dval] : *decl_fields) {
    const Dlop* rv = find_field(recv_fields, fld);
    if (rv == nullptr && find_field(recv_type_fields, fld) == nullptr) {
      fail(std::format("missing field `{}`", fld));
    }

    // Declared field type — absent (untyped field) means presence suffices.
    const auto dft = try_field_type(std::format("{}.{}", decl_tn, fld));
    if (!dft || dft->kind == Io_kind::none) {
      continue;
    }
    // Receiver field type: an inline-typed receiver records it on the var
    // path; a named-typed receiver on its typename path.
    std::optional<upass::uPass::Field_decl_type> rft;
    if (!recv_name.empty()) {
      rft = try_field_type(std::format("{}.{}", recv_name, fld));
    }
    if (!rft && !recv_tn.empty()) {
      rft = try_field_type(std::format("{}.{}", recv_tn, fld));
    }
    // Kind: prefer the receiver's declared kind; fall back to the comptime
    // value's kind. Unknown on both → lenient (presence already verified).
    Io_kind rkind = rft ? rft->kind : Io_kind::none;
    if (rkind == Io_kind::none && rv != nullptr) {
      if (rv->is_string()) {
        rkind = Io_kind::string;
      } else if (rv->is_bool()) {
        rkind = Io_kind::boolean;
      } else if (rv->is_integer()) {
        rkind = Io_kind::integer;
      }
    }
    auto kind_name = [](Io_kind k) -> std::string_view {
      switch (k) {
        case Io_kind::integer: return "integer";
        case Io_kind::boolean: return "bool";
        case Io_kind::string : return "string";
        case Io_kind::none   : break;
      }
      return "untyped";
    };
    if (rkind != Io_kind::none && rkind != dft->kind) {
      fail(std::format("field `{}` kind mismatch (declared {}, receiver has {})", fld, kind_name(dft->kind), kind_name(rkind)));
    }
    // Integer range-subset: receiver ⊆ declared, checked bound-by-bound when
    // both sides are known. An unbounded declared side accepts anything; an
    // unknown receiver range is lenient (no declared type to compare).
    if (dft->kind == Io_kind::integer && rft && rft->kind == Io_kind::integer) {
      if (dft->range_max && rft->range_max && rft->range_max->gt_op(*dft->range_max)->is_known_true()) {
        fail(std::format("field `{}` range exceeds declared (max {} > {})",
                         fld,
                         rft->range_max->to_pyrope(),
                         dft->range_max->to_pyrope()));
      }
      if (dft->range_min && rft->range_min && rft->range_min->lt_op(*dft->range_min)->is_known_true()) {
        fail(std::format("field `{}` range exceeds declared (min {} < {})",
                         fld,
                         rft->range_min->to_pyrope(),
                         dft->range_min->to_pyrope()));
      }
    }
  }
}

void uPass_runner::emit_op_with_fold_at(const Lnast_nid& src) {
  // Save and re-position the read cursor so the in-progress traversal isn't
  // disturbed. emit_op_with_fold balances its own move_to_child / sibling /
  // parent operations, so the nid_stack returns to baseline; restoring here
  // just covers move_to_nid (which doesn't push onto the stack).
  auto saved = lm->save_cursor();
  lm->move_to_nid(src);
  emit_op_with_fold(/*fold_all=*/false);
  lm->restore_cursor(saved);
}

void uPass_runner::emit_ref_or_folded(std::string_view name) {
  if (!materialize_) {
    return;
  }
  auto folded = try_fold_ref(name);
  // Substitute only a genuine folded constant. A `nil` value is an
  // unset/poison marker (not is_invalid, so it slips past the check below, but
  // its to_pyrope() is the placeholder `0`); emitting it would replace a live
  // operand with `0` in the materialized tree. This is the tail of the 1i
  // comb-inliner bug: a runtime-valued output stays nil-seeded in the ST, so
  // the kept `store(out, ___ret)` would otherwise fold to `store(out, 0)` and
  // the output reads 0 instead of the real value. Keep the ref so the runtime
  // producer drives it. (Materialize-only: comptime folding never reaches here
  // — it reads the ST directly via current_prim_value.)
  if (folded && !folded->is_invalid() && !folded->is_nil()) {
    emit_leaf(Lnast_node::create_const(folded->to_pyrope()));
  } else {
    emit_leaf(lm->current_node());
  }
}

void uPass_runner::emit_op_with_fold(bool fold_all) {
  if (!materialize_) {
    return;  // pure emission (dispatch happened in the caller); cursor untouched
  }
  const auto op_ntype = lm->current_type();
  emit_push(op_ntype);  // carries the SourceId (general carry)

  // A `declare`/`type_spec` whose type slot (child 1) is a named-type
  // `ref` must NOT be folded: the ref names a TYPE, not a value, so folding it
  // through the symbol table (e.g. `const a = …; const x:a = …`) would replace
  // the type with `a`'s value. Keep child 1 verbatim for these op-nodes.
  const bool type_slot_at_1 = Lnast_ntype::is_declare(op_ntype) || Lnast_ntype::is_type_spec(op_ntype);

  if (lm->has_child()) {
    lm->move_to_child();
    int idx = 0;
    do {
      const bool is_lhs = ((idx == 0) && !fold_all) || (idx == 1 && type_slot_at_1);
      if (!is_lhs && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
        emit_ref_or_folded(lm->current_text());
      } else if (!is_lhs && Lnast_ntype::is_store(lm->get_raw_ntype())) {
        // A tuple-field entry `assign(key, val)` (inside tuple_add/concat/set):
        // the key is a structural label (kept raw by current_text), but the
        // VALUE must fold — otherwise a value tmp whose producer was dropped
        // (folded to a const) is emitted raw and dangles (lnastfmt rejects it).
        emit_push(lm->current_type());
        if (lm->move_to_child()) {
          emit_subtree_verbatim();  // key — current_text keeps the field-key raw
          while (lm->move_to_sibling()) {
            if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
              emit_ref_or_folded(lm->current_text());
            } else {
              emit_subtree_verbatim();
            }
          }
          lm->move_to_parent();
        }
        emit_pop();
      } else {
        // Either the LHS (don't fold — it's a dst) or a non-ref child (const,
        // or a nested subtree).
        emit_subtree_verbatim();
      }
      ++idx;
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }

  emit_pop();
}

// ── Pass dispatch ─────────────────────────────────────────────────────────────

void uPass_runner::dispatch_to_passes(Pass_method fn) {
  for (auto& entry : upasses) {
    // Snapshot the read cursor before dispatch. If the pass throws (e.g.
    // constprop's sub_op refuses a string-typed invalid Dlop) or is
    // otherwise unbalanced, restoring here keeps the runner-level emit
    // logic operating on the op-node the switch case selected.
    const auto saved = lm->save_cursor();
    try {
      (entry.pass.get()->*fn)();
    } catch (const std::runtime_error& ex) {
      std::print(stderr, "uPass [{}] error: {}\n", entry.name, ex.what());
    }
    lm->restore_cursor(saved);
  }
}

bool uPass_runner::any_pass_drops() const {
  for (auto* p : classify_capable_passes) {
    if (p->classify_statement().kind == upass::Emit_kind::drop_subtree) {
      return true;
    }
  }
  return false;
}

void uPass_runner::process_drop_candidate(Pass_method fn, bool fold_all) {
  // 1. Run per-node process_* so symbol tables see the current statement.
  dispatch_to_passes(fn);
  // 2. Region/verdict drops (verifier cassert discharge, func_extract
  //    virtualized bodies) still ride classify_statement; constprop's and
  //    the coalescer's per-op decisions arrived as push VOTES and never
  //    reach this legacy path — the runner derives constprop's dst-drop
  //    itself: a fully-known trivial-scalar dst (not reg/mut-declared)
  //    means every consumer folds the value, so the producer is dead.
  //    cassert has NO dst (child 0 is the condition) and always emits.
  bool drop = any_pass_drops();
  if (!drop && lm->get_raw_ntype() != Lnast_ntype::Lnast_ntype_cassert && lm->has_child()) {
    const auto here = lm->save_cursor();
    lm->move_to_child();
    if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
      const auto name = lm->current_text();
      const auto b    = symbol_table_.get_bundle(name);
      const auto mode = b ? b->get_mode() : upass::Mode::unknown;
      if (mode != upass::Mode::reg_kind && mode != upass::Mode::mut_kind) {
        drop = symbol_table_.known_const_scalar(name).has_value();
      }
    }
    lm->restore_cursor(here);
  }
  if (!drop) {
    emit_op_with_fold(fold_all);
  }
}

// ── Push-based dispatch ─────────────────────────────────────────────────────

bool uPass_runner::resolve_node_operands(Resolved_node& out) {
  if (!lm->has_child()) {
    return false;
  }
  const auto here = lm->save_cursor();
  lm->move_to_child();
  if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(here);
    return false;
  }
  out.dst_name = lm->current_text();

  // dst: the live bundle (COW-unshared for in-place mutation) when bound;
  // otherwise a throwaway the lazy install publishes on first write. Dotted
  // dsts (field-path stores) resolve the ROOT var — the selectors ride in
  // src per the store contract.
  const auto root = std::string(Bundle::get_first_level(out.dst_name));
  if (auto b = symbol_table_.get_bundle_for_write(root); b) {
    out.dst           = std::move(b);
    out.dst_was_bound = true;
  } else {
    out.dst           = std::make_shared<Bundle>(root);
    out.dst_was_bound = false;
  }

  while (lm->move_to_sibling()) {
    const auto t = lm->get_raw_ntype();
    if (Lnast_ntype::is_const(t)) {
      // Literal text → Kind (optable.md §Inference, ex-typecheck
      // seed_kind_from_const): nil is poison, true/false are boolean, the
      // single-unknown-bit `0sb?`/`0ub?` is TYPELESS (wildcard), a
      // double-quoted literal is a string (single-quoted chars parse as
      // integers), anything integer-parseable is an integer.
      const auto  txt = lm->current_text();
      upass::Kind k   = upass::Kind::unknown;
      if (txt == "nil") {
        k = upass::Kind::nil;
      } else if (txt == "true" || txt == "false") {
        k = upass::Kind::boolean;
      } else if (txt == "0sb?" || txt == "0ub?") {
        k = upass::Kind::unknown;
      } else if (!txt.empty() && txt.front() == '"') {
        k = upass::Kind::string;
      }
      Dlop v;
      try {
        v = *Dlop::from_pyrope(txt);
        if (k == upass::Kind::unknown && txt != "0sb?" && txt != "0ub?") {
          k = v.is_string() ? upass::Kind::string : (v.is_integer() ? upass::Kind::integer : upass::Kind::unknown);
        }
      } catch (...) {
        // Unparseable literal (selector words etc.): keep it as a string.
        v = *Dlop::from_string(txt);
        if (k == upass::Kind::unknown) {
          k = upass::Kind::string;
        }
      }
      const bool pattern = txt.size() >= 3 && txt[0] == '0' && (txt[1] == 's' || txt[1] == 'u') && txt[2] == 'b';
      out.src.push_back(upass::Operand{std::string_view{}, Bundle::make_const(v, k), pattern});
    } else if (Lnast_ntype::is_ref(t)) {
      const auto              name = lm->current_text();
      std::shared_ptr<Bundle> b    = symbol_table_.get_bundle(name);
      if (!b) {
        b = std::make_shared<Bundle>(name);  // unbound (runtime IO etc.): empty view
      }
      out.src.push_back(upass::Operand{name, std::move(b), false});
    } else {
      // Sub-tree operand (compound payload, e.g. a nested store marker):
      // an empty placeholder keeps src child-aligned for the hooks.
      out.src.push_back(upass::Operand{std::string_view{}, std::make_shared<Bundle>(""), false});
    }
  }
  lm->restore_cursor(here);
  return true;
}

bool uPass_runner::dispatch_push(upass::Push_method fn, Resolved_node& rn) {
  bool any_drop = false;
  for (auto& entry : upasses) {
    const auto here = lm->save_cursor();
    try {
      if ((entry.pass.get()->*fn)(rn.dst_name, *rn.dst, upass::Src_span{rn.src}) == upass::Vote::drop) {
        any_drop = true;
      }
    } catch (const std::runtime_error& e) {
      std::print(stderr, "upass pass error: {}\n", e.what());
    }
    lm->restore_cursor(here);
  }
  // Lazy install: a dst the passes populated becomes the name's live bundle
  // ("the first write installs the bundle"). VALUE-vs-FACT split: constprop
  // binds the VALUE directly in the table mid-dispatch (its fold is a table
  // side-effect) — its binding wins the value slice; the typed FACT fields
  // the other passes wrote into the resolved dst are merged onto it instead
  // of clobbering.
  // A dotted dst names a FIELD of the root bundle: the root's binding is what
  // resolve handed out (when bound); never install a fresh root from here.
  //
  // The re-fetch+merge below also covers BOUND dsts: a pass writing the
  // table mid-dispatch goes through the COW unshare,
  // which CLONES the slot (the resolved rn.dst still holds a reference) and
  // orphans rn.dst — facts a later pass wrote land on the
  // orphan and must be folded onto the new slot bundle.
  if (!rn.dst_name.empty() && rn.dst_name.find('.') == std::string_view::npos) {
    const auto root = std::string(Bundle::get_first_level(rn.dst_name));
    if (auto now = symbol_table_.get_bundle_for_write(root); now) {
      if (now.get() != rn.dst.get()) {
        merge_fact_fields(*now, *rn.dst);
      }
      // Bw soundness invariant: a comptime-known value must lie
      // WITHIN its derived range (the range is a conservative bound on the
      // CURRENT value; replace-on-stamp + value-write invalidation keep it
      // fresh). A violation is an internal compiler bug, not a user error.
      {
        const Bundle::Entry& e0 = now->get_entry("0");
        if (!e0.trivial.is_invalid() && e0.trivial.is_integer() && !e0.trivial.has_unknowns() && !e0.bw_max.is_invalid()
            && !e0.bw_min.is_invalid()) {
          I(!e0.trivial.gt_op(e0.bw_max)->is_known_true() && !e0.trivial.lt_op(e0.bw_min)->is_known_true());
        }
      }
    } else if (!rn.dst_was_bound
               && (!rn.dst->is_empty() || rn.dst->get_mode() != upass::Mode::unknown || !rn.dst->get_type_name().empty())) {
      // set() anchors ___ tmps at the function scope and records the
      // uncertain-arm modification.
      (void)symbol_table_.set(root, rn.dst);
    }
  }
  if (!symbol_table_.pending_decl_facts.empty()) {
    apply_pending_field_facts();
  }
  return any_drop;
}

// Drain the dotted-bake stash: any pending field whose root binding
// now holds a trivial at that path gets its declared facts written (and the
// pending entry erased). Called per dispatched node; the map is empty except
// in the few statements between an inliner type_spec prologue and the
// argument store, so the common-path cost is one empty() check.
void uPass_runner::apply_pending_field_facts() {
  auto& pending = symbol_table_.pending_decl_facts;
  for (auto it = pending.begin(); it != pending.end();) {
    const auto root  = Bundle::get_first_level(it->first);
    const auto fpath = Bundle::get_all_but_first_level(it->first);
    auto       rb    = symbol_table_.get_bundle_for_write(root);
    if (!rb || !rb->has_trivial(fpath)) {
      ++it;
      continue;
    }
    const auto&   pf = it->second;
    Bundle::Entry fe = rb->get_entry(fpath);
    fe.immutable     = false;
    if (pf.kind != upass::Kind::unknown) {
      fe.kind = pf.kind;
    }
    if (pf.mode != upass::Mode::unknown) {
      fe.mode = pf.mode;
    }
    if (!pf.decl_max.is_invalid()) {
      fe.decl_max = pf.decl_max;
    }
    if (!pf.decl_min.is_invalid()) {
      fe.decl_min = pf.decl_min;
    }
    fe.comptime = fe.comptime || pf.comptime;
    rb->set(fpath, std::move(fe));
    pending.erase(it++);
  }
}

// Fold the typed pass-fact fields written into the resolved dst onto the
// live slot bundle (constprop may have replaced/cloned it mid-dispatch).
// Facts only (kind / declared envelope / comptime / mode / type_name); the
// value slice (trivial) stays the binding's, and the DERIVED range REPLACES
// (it is this node's fresh derivation — a stale pair would no longer
// contain the new value).
void uPass_runner::merge_fact_fields(Bundle& bound, const Bundle& from) {
  if (bound.get_mode() == upass::Mode::unknown && from.get_mode() != upass::Mode::unknown) {
    bound.set_mode(from.get_mode());
  }
  if (bound.get_value_kind() == upass::Kind::unknown && from.get_value_kind() != upass::Kind::unknown) {
    bound.set_value_kind(from.get_value_kind());
  }
  if (bound.get_type_name().empty() && !from.get_type_name().empty()) {
    bound.set_type_name(from.get_type_name());
  }
  // A throwaway's "0" facts describe the BUNDLE; on a multi-shaped binding
  // the "0" key is field 0, so bundle-level facts must not land there.
  const bool bound_scalar = !bound.has_named_top() && bound.unnamed_top_count() <= 1;
  for (const auto& [key, fe] : from.non_attr_entries()) {
    if (!bound.has_trivial(key)) {
      continue;  // facts ride only onto entries the binding actually has
    }
    if (key == "0" && !bound_scalar) {
      continue;
    }
    Bundle::Entry e   = bound.get_entry(key);
    bool          dif = false;
    if (e.kind == upass::Kind::unknown && fe.kind != upass::Kind::unknown) {
      e.kind = fe.kind;
      dif    = true;
    }
    if (e.decl_max.is_invalid() && !fe.decl_max.is_invalid()) {
      e.decl_max = fe.decl_max;
      dif        = true;
    }
    if (e.decl_min.is_invalid() && !fe.decl_min.is_invalid()) {
      e.decl_min = fe.decl_min;
      dif        = true;
    }
    // bw is VALUE-derived: the throwaway carries THIS node's fresh
    // derivation — it replaces any pair the slot still holds (a stale pair
    // would no longer contain the new value).
    if (!fe.bw_max.is_invalid() && (e.bw_max.is_invalid() || !e.bw_max.is_known_eq(fe.bw_max))) {
      e.bw_max = fe.bw_max;
      dif      = true;
    }
    if (!fe.bw_min.is_invalid() && (e.bw_min.is_invalid() || !e.bw_min.is_known_eq(fe.bw_min))) {
      e.bw_min = fe.bw_min;
      dif      = true;
    }
    if (!e.comptime && fe.comptime) {
      e.comptime = true;
      dif        = true;
    }
    if (dif) {
      bound.set(key, std::move(e));
    }
  }
}

void uPass_runner::process_drop_candidate_push(upass::Push_method fn, bool fold_all) {
  Resolved_node rn;
  if (!resolve_node_operands(rn)) {
    // No leading dst ref: still push-dispatch with a throwaway dst (hooks
    // that need the payload walk the cursor themselves).
    rn.dst = std::make_shared<Bundle>("");
  }
  const bool vote_drop = dispatch_push(fn, rn);
  if (!vote_drop && !any_pass_drops()) {
    emit_op_with_fold(fold_all);
  }
}

void uPass_runner::process_drop_candidate_verbatim(Pass_method fn) {
  dispatch_to_passes(fn);
  if (!any_pass_drops()) {
    emit_subtree_verbatim();
  }
}

void uPass_runner::process_verbatim(Pass_method fn) {
  dispatch_to_passes(fn);
  // Emit the op-node with operand folding (skipping LHS at child 0). This
  // matches A_OP behavior except we never drop the statement — verbatim
  // ops (tuple_*, attr_*, range, io, delay_assign) carry side-effects or
  // shape information that downstream passes still need to see, but their
  // ref operands should fold so the staged tree doesn't carry references
  // to tmps whose producing op was dropped (lnastfmt's read-without-write
  // check would correctly flag the dangling refs otherwise).
  emit_op_with_fold(/*fold_all=*/false);
}

// ── comb-call inliner ──────────────────────────────────────────────────────

void uPass_runner::set_function_registry(const std::vector<std::shared_ptr<Lnast>>& lnasts) {
  function_registry.clear();
  for (const auto& ln : lnasts) {
    if (ln) {
      function_registry.emplace(std::string(ln->get_top_module_name()), ln);
    }
  }

  // Build the call graph and mark callees that can reach themselves (direct or
  // mutual recursion). The inliner bails those to the evaluator, which fully unrolls
  // comptime-bounded recursion; runner-side fuel-bounded recursion remains a follow-up.
  recursive_callees_.clear();
  absl::flat_hash_map<std::string, std::vector<std::string>> edges;
  for (const auto& [name, ln] : function_registry) {
    for (const auto& nid : ln->depth_preorder(ln->get_root())) {
      if (nid.is_invalid() || ln->get_type(nid) != Lnast_ntype::Lnast_ntype_func_call) {
        continue;
      }
      auto c0 = ln->get_first_child(nid);
      if (!c0.is_valid()) {
        continue;
      }
      auto c1 = ln->get_sibling_next(c0);  // callee-name child
      if (!c1.is_valid() || ln->get_type(c1) != Lnast_ntype::Lnast_ntype_ref) {
        continue;
      }
      if (auto tgt = lookup_callee(ln->get_name(c1))) {
        edges[name].emplace_back(tgt->get_top_module_name());
      }
    }
  }
  for (const auto& [name, _] : function_registry) {
    absl::flat_hash_set<std::string> seen;
    std::vector<std::string>         stack;
    if (auto it = edges.find(name); it != edges.end()) {
      stack = it->second;
    }
    while (!stack.empty()) {
      auto cur = std::move(stack.back());
      stack.pop_back();
      if (cur == name) {
        recursive_callees_.insert(name);
        break;
      }
      if (!seen.insert(cur).second) {
        continue;
      }
      if (auto it = edges.find(cur); it != edges.end()) {
        stack.insert(stack.end(), it->second.begin(), it->second.end());
      }
    }
  }

  // Mark which callees the runner can fully splice today (everything else is
  // routed to the evaluator). Phase C supports: a real signature, exactly one
  // output written by name in the body, non-recursive, and no positional
  // placeholder params (`_`, `_0`…) / implicit returns. Multi-output, method
  // dispatch, closures, and comptime-attr queries land in later phases.
  auto is_placeholder = [](std::string_view n) { return n == "_" || (n.size() >= 2 && n[0] == '_' && n[1] >= '0' && n[1] <= '9'); };
  inlinable_callees_.clear();
  placeholder_callees_.clear();
  for (const auto& [name, ln] : function_registry) {
    const auto& io        = ln->io_meta();
    // Recursion stays on the evaluator. Pure emit+fold recurses in the
    // runner's own C++ call stack (process_lnast → try_inline → …), so deep
    // comptime recursion (e.g. fib(10)) stack-overflows; the evaluator folds
    // to a constant without emitting/recursing the runner. Revisit only with
    // an iterative work-stack rewrite of process_lnast.
    // Recursion is allowed here (gated at the call site on constant args, which
    // is what lets the base case fold and terminate the unroll). Multi-output
    // is allowed too (epilogue splats a bundle); require at least one output.
    auto        reg_stmts = ln->get_first_child(ln->get_root());
    if (reg_stmts.is_valid() && ln->get_type(reg_stmts) == Lnast_ntype::Lnast_ntype_io) {
      reg_stmts = ln->get_sibling_next(reg_stmts);
    }
    if (!reg_stmts.is_valid() || !ln->get_first_child(reg_stmts).is_valid()) {
      continue;  // no body to inline
    }
    // A zero-output comb is only worth splicing when it has an observable side
    // effect: a cassert/cputs (`comb top() { cassert(…) } top()`) OR a `ref`
    // param it mutates (`comb set_pair(ref self, …) { self.x = … }`). Requiring
    // one avoids mis-inlining an *implicit-return* comb whose result io_meta
    // doesn't capture as a declared output (e.g. `comb top() { x = (…) }`
    // consumed by `const a = top()` — see named_tuple.prp), binding nothing back.
    if (io.outputs.empty()) {
      bool has_side_effect = false;
      for (const auto& e : io.inputs) {
        if (e.is_ref) {
          has_side_effect = true;
          break;
        }
      }
      for (const auto& nid : ln->depth_preorder(reg_stmts)) {
        if (has_side_effect) {
          break;
        }
        if (nid.is_invalid()) {
          continue;
        }
        const auto nt = ln->get_type(nid);
        if (nt == Lnast_ntype::Lnast_ntype_cassert) {
          has_side_effect = true;
          break;
        }
        if (nt == Lnast_ntype::Lnast_ntype_func_call) {
          auto c0 = ln->get_first_child(nid);
          auto c1 = c0.is_valid() ? ln->get_sibling_next(c0) : c0;
          if (c1.is_valid() && ln->get_type(c1) == Lnast_ntype::Lnast_ntype_ref && ln->get_name(c1) == "cputs") {
            has_side_effect = true;
            break;
          }
        }
      }
      if (!has_side_effect) {
        continue;  // pure / implicit-return zero-output comb — leave as a call
      }
    }
    absl::flat_hash_set<std::string> params;
    for (const auto& e : io.inputs) {
      params.insert(e.name);
    }
    // Scan the body STMTS only (skip the io node — its `-> (a,b)` output
    // declaration is itself a tuple node and would otherwise mark every
    // multi-output comb as touching a tuple).
    auto stmts_nid = ln->get_first_child(ln->get_root());
    if (stmts_nid.is_valid() && ln->get_type(stmts_nid) == Lnast_ntype::Lnast_ntype_io) {
      stmts_nid = ln->get_sibling_next(stmts_nid);
    }
    absl::flat_hash_set<std::string>                 defined;
    absl::flat_hash_set<std::string>                 tuple_dsts;   // dsts assigned a tuple/bundle value
    std::vector<std::pair<std::string, std::string>> ref_aliases;  // (lhs, rhs-ref) plain assigns
    bool                                             has_placeholder = false;
    bool                                             tuple_param     = false;
    if (stmts_nid.is_valid()) {
      for (const auto& nid : ln->depth_preorder(stmts_nid)) {
        if (nid.is_invalid()) {
          continue;
        }
        const auto nt = ln->get_type(nid);
        if (nt == Lnast_ntype::Lnast_ntype_ref && is_placeholder(ln->get_name(nid))) {
          has_placeholder = true;
        }
        auto fc = ln->get_first_child(nid);
        if (fc.is_valid() && ln->get_type(fc) == Lnast_ntype::Lnast_ntype_ref) {
          defined.insert(std::string(ln->get_name(fc)));
          // A tuple_add/concat dst is assigned a bundle value.
          if (nt == Lnast_ntype::Lnast_ntype_tuple_add || nt == Lnast_ntype::Lnast_ntype_tuple_concat) {
            tuple_dsts.insert(std::string(ln->get_name(fc)));
          }
          // Plain `lhs = rhs` (single ref rhs) — record for alias propagation
          // so `foo = ___t` inherits ___t's tuple-valued-ness. This is
          // a 0-level `store` (exactly ref+value children; `assign` was deleted).
          if (nt == Lnast_ntype::Lnast_ntype_store && ln->get_sibling_next(fc).is_valid()
              && !ln->get_sibling_next(ln->get_sibling_next(fc)).is_valid()) {
            auto rhs = ln->get_sibling_next(fc);
            if (rhs.is_valid() && ln->get_type(rhs) == Lnast_ntype::Lnast_ntype_ref) {
              ref_aliases.emplace_back(std::string(ln->get_name(fc)), std::string(ln->get_name(rhs)));
            }
          }
        }
        // A param read as a tuple_get source is a tuple-typed param — the runner
        // can't bind/propagate bundle actuals yet (Phase E), so leave those to
        // the evaluator. tuple_get layout: dst, src, field...
        if (nt == Lnast_ntype::Lnast_ntype_tuple_get && fc.is_valid()) {
          auto src = ln->get_sibling_next(fc);
          if (src.is_valid() && ln->get_type(src) == Lnast_ntype::Lnast_ntype_ref && params.contains(ln->get_name(src))) {
            tuple_param = true;
          }
        }
      }
    }
    // Propagate tuple-valued-ness across plain ref aliases to a fixpoint, so a
    // post-SSA `___t = (…); foo = ___t` marks `foo` as tuple-valued.
    bool grew = true;
    while (grew) {
      grew = false;
      for (const auto& [lhs, rhs] : ref_aliases) {
        if (tuple_dsts.contains(rhs) && tuple_dsts.insert(lhs).second) {
          grew = true;
        }
      }
    }
    bool all_outputs_written = true;
    for (const auto& o : io.outputs) {
      // A tuple-typed output is flattened to leaves `p.first`/`p.second`, but the
      // body writes the LOGICAL output `p` (as a tuple). Accept the leaf OR its
      // top-level (pre-dot) prefix — but only when the leaf IS dotted (a genuine
      // flattened tuple output), so a plain scalar output keeps the strict check.
      bool ok = defined.contains(o.name);
      if (!ok) {
        if (const auto dp = o.name.find('.'); dp != std::string::npos) {
          ok = defined.contains(o.name.substr(0, dp));
        }
      }
      if (!ok) {
        all_outputs_written = false;
        break;
      }
    }
    // A tuple-VALUED output (`foo = (bar=…)`, possibly through the SSA tmp
    // alias `___t=(…); foo=___t`) is now handled: the epilogue splats it as a
    // bundle-ref field and the field-key/value-fold emit rebuilds the nested
    // shape (fcall5b / fcall_rename_deep). tuple_param (a param read as a
    // bundle via tuple_get, e.g. tree_sum's `v[lo]`) is allowed too — a bundle
    // actual binds via the bundle-alias assign path.
    (void)tuple_param;
    (void)tuple_dsts;
    if (all_outputs_written) {
      inlinable_callees_.insert(name);
      // Positional-placeholder callees (body uses _0/_1/_) need the prologue to
      // bind those aliases too — record so try_inline only does that work when
      // needed (avoids emitting dead alias assigns for ordinary callees).
      if (has_placeholder) {
        placeholder_callees_.insert(name);
      }
    }
  }
}

std::shared_ptr<Lnast> uPass_runner::lookup_callee(std::string_view name) const {
  // Delegated to the resolver (name resolution is its job).
  return upass::call_resolver::lookup_callee(function_registry, name);
}

void uPass_runner::flush_deferred_emits() { dispatch_to_passes(&upass::uPass::flush_deferred); }

void uPass_runner::emit_inline_binding(const std::string& lhs, const Lnast_node& rhs) {
  // A synthesized `lhs = nil` seed is exempt from typecheck's
  // nil-does-not-infer-tuple-shape rule (the body/epilogue legally binds a
  // tuple over it).
  if (rhs.is_const() && rhs.get_name() == "nil") {
    symbol_table_.nil_seeded.insert(lhs);
  }
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-bind");
  auto s    = std::make_shared<Lnast>(body, "inl-bind");
  auto root = s->set_root(Lnast_ntype::create_store());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(lhs));
  s->add_child(root, rhs);
  flush_deferred_emits();     // flush outer-tree parked writes before the swap
  lm->push_source(s, "", 0);  // literal names; no rename for the binding
  process_lnast();            // cursor at assign root → A_OP(assign): dispatch + emit
  flush_deferred_emits();     // flush scratch-tree parked writes before swap-out
  lm->pop_source();
}

void uPass_runner::emit_inline_tuple(const std::string& dst, const std::vector<std::pair<std::string, Lnast_node>>& fields) {
  // Build `dst = (k0=v0, k1=v1, …)` as a tuple_add and run it through the walk
  // so constprop builds dst's bundle (multi-output return splat). Layout:
  //   tuple_add ref(dst) assign(ref(key), val)...
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-tup");
  auto s    = std::make_shared<Lnast>(body, "inl-tup");
  auto root = s->set_root(Lnast_ntype::create_tuple_add());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));
  for (const auto& [k, v] : fields) {
    auto a = s->add_child(root, Lnast_ntype::create_store());
    s->add_child(a, Lnast_node::create_ref(k));
    s->add_child(a, v);
  }
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at tuple_add root → A_OP(tuple_add): dispatch + emit
  flush_deferred_emits();
  lm->pop_source();
}

void uPass_runner::emit_inline_typespec(const std::string& name, int bits, bool is_signed) {
  if (bits <= 0) {
    return;  // unknown width — nothing to declare
  }
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-type");
  auto s    = std::make_shared<Lnast>(body, "inl-type");
  auto root = s->set_root(Lnast_ntype::create_type_spec());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(name));
  // Emit the canonical prim_type_int(max,min) from (bits, signed).
  auto pt = s->add_child(root, Lnast_ntype::create_prim_type_int());
  if (bits > 0 && is_signed) {
    s->add_child(pt, Lnast_node::create_const(std::string(Dlop::get_mask_value(static_cast<int>(bits) - 1)->to_pyrope())));
    s->add_child(pt, Lnast_node::create_const(std::string(Dlop::get_neg_mask_value(static_cast<int>(bits) - 1)->to_pyrope())));
  } else if (bits > 0) {
    s->add_child(pt, Lnast_node::create_const(std::string(Dlop::get_mask_value(static_cast<int>(bits))->to_pyrope())));
    s->add_child(pt, Lnast_node::create_const("0"));
  } else {
    s->add_child(pt, Lnast_node::create_const("nil"));
    s->add_child(pt, Lnast_node::create_const("nil"));
  }
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at type_spec root → C_OP(type_spec): dispatch + emit
  flush_deferred_emits();
  lm->pop_source();
}

void uPass_runner::emit_inline_typespec_range(const std::string& name, const std::optional<Dlop>& range_max,
                                              const std::optional<Dlop>& range_min) {
  if (!range_max && !range_min) {
    return;  // fully unbounded — nothing concrete to declare
  }
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-type");
  auto s    = std::make_shared<Lnast>(body, "inl-type");
  auto root = s->set_root(Lnast_ntype::create_type_spec());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(name));
  // Mirror read_scalar_type_at_cursor's prim_type_int(max,min) layout: an
  // unset bound is the "nil" (unbounded) child.
  auto pt = s->add_child(root, Lnast_ntype::create_prim_type_int());
  s->add_child(pt, Lnast_node::create_const(range_max ? std::string(range_max->to_pyrope()) : std::string("nil")));
  s->add_child(pt, Lnast_node::create_const(range_min ? std::string(range_min->to_pyrope()) : std::string("nil")));
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at type_spec root → C_OP(type_spec): dispatch + emit
  flush_deferred_emits();
  lm->pop_source();
}

void uPass_runner::emit_inline_typespec_bool(const std::string& name) {
  // A `bool`-bound generic param/output: emit a childless prim_type_bool
  // typespec (mirrors prp2lnast's `:bool` lowering). Without this, a bool bind
  // — which also carries max=1/min=0 — would be typed as int(1,0), turning a
  // `r == true` round-trip into an int==bool mismatch.
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-type");
  auto s    = std::make_shared<Lnast>(body, "inl-type");
  auto root = s->set_root(Lnast_ntype::create_type_spec());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(name));
  s->add_child(root, Lnast_ntype::create_prim_type_bool());  // leaf, no max/min children
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();
  flush_deferred_emits();
  lm->pop_source();
}

void uPass_runner::emit_inline_sext(const std::string& dst, const std::string& src, int sign_bit) {
  if (sign_bit < 0) {
    return;
  }
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-sext");
  auto s    = std::make_shared<Lnast>(body, "inl-sext");
  auto root = s->set_root(Lnast_ntype::create_sext());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));
  s->add_child(root, Lnast_node::create_ref(src));
  s->add_child(root, Lnast_node::create_const(std::to_string(sign_bit)));
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at sext root → dispatch + emit/fold
  flush_deferred_emits();
  lm->pop_source();
}

bool uPass_runner::try_inline_func_call() {
  // One-shot ctor marker (see splice_init_call): true only for the
  // synthesized constructor call itself, never for calls nested inside the
  // spliced init body.
  const bool is_ctor_call = ctor_call_pending_;
  ctor_call_pending_      = false;
  // Cursor at the func_call node. Layout: [dst(ref), callee(ref), actual...].
  if (!lm->has_child()) {
    return false;
  }

  // ── Gather the call shape without disturbing the outer cursor ─────────────
  const auto saved = lm->save_cursor();

  // Snapshot the call-site source span (cursor is on the func_call node) so the
  // argument-naming diagnostics below can point at the call line. The SourceId
  // resolves through the tree that owns the node.
  const auto         call_nid  = lm->get_current_nid();  // func_call node in the source tree
  livehd::diag::Span call_span = lm->get_lnast()->span_of(call_nid);

  lm->move_to_child();  // dst
  if (lm->get_raw_ntype() != Lnast_ntype::Lnast_ntype_ref) {
    lm->restore_cursor(saved);
    return false;
  }
  std::string dst_name(lm->current_text());  // dst lives in the caller/frame scope

  if (!lm->move_to_sibling() || lm->get_raw_ntype() != Lnast_ntype::Lnast_ntype_ref) {
    lm->restore_cursor(saved);
    return false;
  }
  std::string callee_name(lm->current_raw_text());  // function id — never renamed

  // Higher-order resolution: inside an inlined body, `f(x)` where `f` is a
  // function-valued param resolves to the function bound at the outer call
  // site (closure_capture / fcall6). func_param_bindings_ is keyed by the raw
  // param name as it appears in the body.
  if (auto fb = func_param_bindings_.find(callee_name); fb != func_param_bindings_.end()) {
    callee_name = fb->second;
  }
  // The callee identifier as written at the call site (method dispatch and
  // the import-namespace exemption below need it after callee_name rebinds).
  const std::string source_callee_name(callee_name);

  auto callee = lookup_callee(callee_name);

  // Method dispatch: `obj.method(args)` lowers to `func_call[dst, method,
  // store(__ufcs_arg, obj), args…]` — `method` is not itself a registry
  // function, but obj's type bundle carries it as a function-name field.
  // Resolve method → function via obj's typename (the receiver then naturally
  // binds to the function's first `self` param as the leading positional
  // actual). The receiver rides inside the UFCS marker store; unwrap
  // it to read the obj ref (a bare ref sibling is the legacy/direct shape).
  if (!callee) {
    const auto here     = lm->save_cursor();
    bool       have_obj = false;
    if (lm->move_to_sibling()) {
      if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
        have_obj = true;
      } else if (Lnast_ntype::is_store(lm->get_raw_ntype()) && lm->move_to_child() && lm->current_raw_text() == call_ufcs_arg_marker
                 && lm->move_to_sibling() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
        have_obj = true;  // cursor now on the receiver ref inside the marker store
      }
    }
    if (have_obj) {
      // Candidate bundles, most-specific first: the receiver's own value
      // bundle (an untyped tuple literal carries its methods directly —
      // tup_method), then its declared typename's bundle (the decorator
      // pattern: methods live on the comptime type tuple).
      const std::string recv(lm->current_text());
      const auto        tn = try_typename(recv);
      for (const auto& bundle_name : {recv, std::string(tn)}) {
        if (bundle_name.empty() || callee) {
          continue;
        }
        if (auto bf = try_bundle_fields(bundle_name)) {
          for (const auto& [fld, val] : *bf) {
            if (fld == callee_name && val.is_string()) {
              auto fn = val.to_pyrope();
              if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
                fn = fn.substr(1, fn.size() - 2);  // strip pyrope quotes
              }
              if (auto m = lookup_callee(fn)) {
                callee      = m;
                callee_name = fn;
              }
              break;
            }
          }
        }
      }
    }
    lm->restore_cursor(here);
  }

  // Call through a lambda-ref binding: `const f = b.add1` or
  // `const f = import("ln:u.g")` bind `f` to the callee's TREE NAME as a
  // string (the fcall-ref-const lambda-value form). Resolve it like the
  // bundle-field method path above.
  if (!callee) {
    if (auto fv = try_fold_ref(callee_name); fv && fv->is_string()) {
      auto fn = fv->to_pyrope();
      if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
        fn = fn.substr(1, fn.size() - 2);
      }
      if (fn.starts_with("ln:")) {
        fn = fn.substr(3);
      }
      if (auto m = lookup_callee(fn)) {
        callee      = m;
        callee_name = fn;
      }
    }
  }

  // Overload-gathering dispatch (2f-overload): `const add = [f1, f2]` folds to
  // a bundle of qualified lambda-name strings under numeric keys. When the
  // callee is such a set, rewrite it to the FIRST candidate whose signature
  // accepts the call (tuple-order priority, mirroring select_init_overload);
  // the rest of this function then inlines the chosen lambda as a normal single
  // callee. A one-entry set defers to the normal bind path so its precise
  // arg-shape diagnostics still fire; a multi-entry set with no match is a
  // fatal `fcall-no-overload`.
  if (!callee) {
    auto cands = overload_candidates_of(callee_name);
    if (!cands.empty()) {
      std::vector<Actual>      ov_actuals;
      std::vector<std::string> ov_generics;
      if (gather_actuals(/*drop_ufcs_receiver=*/false, ov_actuals, ov_generics)) {
        std::string chosen;
        if (cands.size() == 1) {
          chosen = cands.front();  // single → defer to the normal precise-error path
        } else {
          for (const auto& fn : cands) {
            auto c = lookup_callee(fn);
            if (c && signature_matches(c->io_meta(), ov_actuals, /*is_ctor_call=*/false)) {
              chosen = fn;
              break;
            }
          }
          if (chosen.empty()) {
            fcall_arg_fail(call_span,
                           "fcall-no-overload",
                           std::format("no overload of `{}` matches the call", source_callee_name),
                           std::format("none of the {} gathered lambdas accepts these arguments", cands.size()));
          }
        }
        callee_name = chosen;
        callee      = lookup_callee(chosen);
      }
    }
  }

  if (!callee) {
    lm->restore_cursor(saved);
    return false;  // not a known comb body → typecast / cell-op / marker path
  }

  // Call-form enforcement: the UFCS form `obj.f(...)` is only valid
  // when the callee declares `self` (as its first input). The direct form
  // `f(obj, ...)` carries no marker and stays subject to the normal argument
  // rules. Checked here — before the inlinable/fuel gates — so a marked call
  // can never silently fall through to the evaluator path.
  //
  // Namespace-access exemption through an import tuple:
  // `b.add1(args)` where `b = import("unit")` and b's field `add1` is a
  // lambda ref (a string naming this callee's tree). The receiver is a
  // namespace, not a method receiver: drop it from the actuals instead of
  // requiring `self`.
  bool drop_ufcs_receiver = false;
  {
    const auto  here        = lm->save_cursor();
    bool        ufcs_marked = false;
    std::string recv_name;
    if (lm->move_to_sibling() && Lnast_ntype::is_store(lm->get_raw_ntype()) && lm->move_to_child()) {
      ufcs_marked = lm->current_raw_text() == call_ufcs_arg_marker;
      if (ufcs_marked && lm->move_to_sibling() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
        recv_name = std::string(lm->current_text());
      }
    }
    lm->restore_cursor(here);
    if (ufcs_marked) {
      const auto& cio = callee->io_meta();
      if (cio.inputs.empty() || cio.inputs[0].name != "self") {
        bool namespace_access = false;
        if (!recv_name.empty()) {
          if (auto bf = try_bundle_fields(recv_name)) {
            for (const auto& [fld, val] : *bf) {
              if (fld != source_callee_name || !val.is_string()) {
                continue;
              }
              auto fn = val.to_pyrope();
              if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
                fn = fn.substr(1, fn.size() - 2);
              }
              if (fn == callee->get_top_module_name() || lookup_callee(fn) == callee) {
                namespace_access = true;
              }
              break;
            }
          }
        }
        if (!namespace_access) {
          fcall_arg_fail(call_span,
                         "fcall-ufcs-no-self",
                         std::format("`{}` does not declare `self`; it cannot be called as a method", callee_name),
                         "use the direct call form `f(args...)`, or declare `self` as the first parameter");
        }
        // Reached only on a namespace access (the no-self error above is
        // [[noreturn]]); set the flag explicitly under the same condition so
        // the receiver-drop never decouples from namespace detection if the
        // error path ever stops being fatal.
        drop_ufcs_receiver = namespace_access;
      }
    }
  }
  // A callee with no declared outputs (`-> ()`) produces no value, so its call
  // result must not be consumed (`const a = top()` — the named_tuple.prp bug:
  // the old implicit-return sugar made the body's locals leak out as a return
  // tuple). Detect consumption by scanning the call's following siblings in
  // the SOURCE tree for a read of the dst tmp. Checked here — before the
  // inlinable/fuel gates — so pure zero-output callees that are never spliced
  // error too. Only prp2lnast's `___N` call tmps are checked (hand-built .ln
  // trees may bind named dsts); the ctor splice synthesizes a never-read dst.
  // Read the outputs off the callee's io NODE, not io_meta — io_meta is only
  // populated by the SSA upass, so it is indistinguishably empty when SSA is
  // disabled (upass.ssa=false dev/test runs would mis-flag every callee).
  const auto callee_has_no_outputs = [&]() -> bool {
    const auto io_nid = callee->get_first_child(callee->get_root());
    if (io_nid.is_invalid() || !Lnast_ntype::is_io(callee->get_type(io_nid))) {
      return false;  // no io node (hand-built tree) — outputs unknown, don't flag
    }
    const auto in_tup = callee->get_first_child(io_nid);
    if (in_tup.is_invalid()) {
      return false;  // malformed io — don't flag
    }
    const auto out_tup = callee->get_sibling_next(in_tup);
    return out_tup.is_invalid() || callee->get_first_child(out_tup).is_invalid();
  };
  if (!is_ctor_call && callee_has_no_outputs()) {
    const auto& src_ln  = lm->get_lnast();
    const auto  dst_raw = src_ln->get_name(src_ln->get_first_child(call_nid));
    if (prp_is_tmp_name(dst_raw)) {
      bool consumed = false;
      for (auto sib = src_ln->get_sibling_next(call_nid); sib.is_valid() && !consumed; sib = src_ln->get_sibling_next(sib)) {
        for (const auto& nid : src_ln->depth_preorder(sib)) {
          if (nid.is_invalid()) {
            continue;
          }
          if (Lnast_ntype::is_ref(src_ln->get_type(nid)) && src_ln->get_name(nid) == dst_raw) {
            consumed = true;
            break;
          }
        }
      }
      if (consumed) {
        fcall_arg_fail(call_span,
                       "fcall-no-output",
                       std::format("`{}` declares no outputs (`-> ()`); its call returns nothing to bind", callee_name),
                       "drop the result binding, or declare outputs in the callee: `-> (res)`");
      }
    }
  }
  // Single gate: only splice callees the runner fully supports today
  // (precomputed in set_function_registry). Everything else — multi-output,
  // placeholders/implicit-return, no-signature — routes to the evaluator.
  if (!inlinable_callees_.contains(std::string(callee->get_top_module_name()))) {
    lm->restore_cursor(saved);
    return false;
  }
  // Phase D — recursion fuel. Comptime-bounded recursion terminates via
  // process_if dead-branch pruning; this is the backstop for unbounded or
  // explosive recursion. Per-callee depth cap (active frames of this callee)
  // plus a per-run total-inline budget. On exhaustion, bail (the call stays a
  // runtime func_call / routes to the evaluator while it still exists).
  std::size_t same_callee_depth = 0;
  for (const auto* c : active_inline_callees_) {
    if (c == callee.get()) {
      ++same_callee_depth;
    }
  }
  if (same_callee_depth >= kInlineMaxDepth || inline_budget_ == 0) {
    lm->restore_cursor(saved);
    return false;
  }
  --inline_budget_;

  std::vector<Actual>      actuals;
  std::vector<std::string> explicit_generics;  // `f<int,string>(…)` type-arg refs, in order
  if (!gather_actuals(drop_ufcs_receiver, actuals, explicit_generics)) {
    return false;
  }
  lm->restore_cursor(saved);  // back on the func_call node (gather left it on the callee ref)

  const auto& io = callee->io_meta();
  // io.empty() is allowed: a void comb (`comb top() { … }`) has no signature
  // but is a legitimate inline target (passed the inlinable_callees_ gate
  // above). Its empty inputs/outputs make the prologue/epilogue no-ops.

  // NOTE: the `pipe`/`mod` declines below run AFTER the argument-naming
  // validation loop (not here). 06-functions.md §"Argument naming" applies to
  // EVERY resolvable callee regardless of kind, so the same naming check must
  // fire for pipe/mod call sites before we bail out to the Sub-instance / pipe
  // path — otherwise an unnamed `diff(x, y)` would compile for a `mod` while
  // erroring for the identical `comb`. The callee + io are already resolved.

  // ── Match actuals → params (positional + named) ──────────────────────────
  const std::size_t       nparams    = io.inputs.size();
  // A trailing `...args` var-arg param (always the LAST input)
  // gathers every actual not consumed by a fixed leading param into one
  // synthesized tuple: positional leftovers become positional entries
  // (`args[i]`), named leftovers become named fields (`args.NAME`). Fixed
  // params bind by the normal rules below; only the leftovers go to the tuple.
  const bool              has_vararg = nparams > 0 && io.inputs[nparams - 1].is_varargs;
  const std::size_t       nbind      = has_vararg ? nparams - 1 : nparams;  // bindable fixed params (vararg excluded)
  std::vector<Lnast_node> vararg_pos;                                       // positional leftovers, in call order
  std::vector<std::pair<std::string, Lnast_node>> vararg_named;             // named leftovers
  std::vector<Lnast_node>                         param_val(nparams, Lnast_node::create_invalid());
  std::vector<bool>                               param_set(nparams, false);
  std::vector<std::string>                        param_func(nparams);  // non-empty: function-valued param
  auto                                            param_index = [&](std::string_view k) -> std::size_t {
    for (std::size_t i = 0; i < nbind; ++i) {  // never match the var-arg slot by name
      if (io.inputs[i].name == k) {
        return i;
      }
    }
    return nbind;
  };
  // 06-functions.md §"Argument naming": every input must be named at the call
  // site. Unnamed (positional) actuals are allowed only under narrow exceptions:
  //   (1) exactly one non-self parameter (nothing to disambiguate),
  //   (2) the actual is a bare variable whose name matches the parameter name,
  //   (3) the actual's type uniquely identifies one parameter by kind (e.g.
  //       `f(true, 7)` for `f(flag:bool, n:int)`). An untyped (kind=none) param
  //       does NOT participate — it must be named unless it is the only arg.
  // `self` (io.inputs[0] for a method) is always bound positionally by the UFCS
  // receiver and is never named; pass-by-ref actuals (`f(ref x)`) are positional
  // by design.
  const bool        has_self       = nbind > 0 && io.inputs[0].name == "self";
  const std::size_t n_named_params = nbind - (has_self ? 1 : 0);  // fixed, non-self (var-arg excluded)
  // Classify a positional actual's scalar kind for exception (3). Dlop literals
  // carry their kind verbatim; a `ref` reports its inferred/declared scalar kind
  // (bool/string/integer), falling back to integer for a range-carrying typed
  // var. Anything else is `none` (can't drive type-based disambiguation). MUST
  // stay identical to signature_matches's `classify` — the overload probe and
  // this real bind have to agree on kind, or a probed-accepted candidate is
  // committed and then rejected here (see signature_matches CONTRACT).
  auto              actual_kind    = [&](const Lnast_node& node) -> Io_kind {
    if (node.is_const()) {
      const auto t = node.get_name();
      if (t == "true" || t == "false") {
        return Io_kind::boolean;
      }
      if (!t.empty() && (t.front() == '\'' || t.front() == '"')) {
        return Io_kind::string;
      }
      if (t == "nil") {
        return Io_kind::none;
      }
      if (auto v = Dlop::from_pyrope(t); v && v->is_integer()) {
        return Io_kind::integer;
      }
      return Io_kind::none;
    }
    if (node.is_ref()) {
      if (auto k = try_scalar_kind(node.get_name()); k != Io_kind::none) {
        return k;  // inferred/declared scalar kind (bool/string/integer)
      }
      if (try_decl_type(node.get_name()).has_value()) {
        return Io_kind::integer;  // typed scalar var (range carrier)
      }
    }
    return Io_kind::none;
  };
  for (auto& a : actuals) {
    if (a.is_named) {
      if (a.key == "self") {
        fcall_arg_fail(call_span,
                       "fcall-self-named",
                       std::format("`self` cannot be passed as a named argument to `{}`", callee_name),
                       "self is bound positionally by the UFCS receiver (`value.method(...)`)");
      }
      const auto idx = param_index(a.key);
      if (idx >= nbind) {
        // A named actual that matches no fixed param is a named
        // leftover gathered into the var-arg tuple (`args.NAME`).
        if (has_vararg) {
          vararg_named.emplace_back(a.key, a.node);
          continue;
        }
        // A bundle-typed param is flattened in io_meta to dotted leaves
        // (`ar` → `ar.x`, `ar.y`). A bundle-literal actual `ar=(x=2,y=11)`
        // arrives under the un-flattened key `ar`; expand it into the leaf
        // params from the caller-side bundle fields (shared ST).
        bool expanded = false;
        if (a.node.is_ref()) {
          if (auto bf = try_bundle_fields(a.node.get_name()); bf && !bf->empty()) {
            expanded = true;
            for (const auto& [fld, val] : *bf) {
              const auto leaf = a.key + "." + fld;
              const auto lidx = param_index(leaf);
              if (lidx >= nbind || val.is_invalid()) {
                expanded = false;
                break;
              }
              param_val[lidx] = Lnast_node::create_const(val.to_pyrope());
              param_set[lidx] = true;
            }
          }
        }
        if (!expanded) {
          fcall_arg_fail(call_span,
                         "fcall-unknown-arg",
                         std::format("unknown argument `{}` in call to `{}`", a.key, callee_name),
                         "remove it or rename it to a declared parameter");
        }
        continue;
      }
      if (param_set[idx]) {
        fcall_arg_fail(call_span,
                       "fcall-duplicate-arg",
                       std::format("duplicate argument `{}` in call to `{}`", a.key, callee_name),
                       "each parameter may be bound only once");
      }
      param_val[idx]  = a.node;
      param_set[idx]  = true;
      param_func[idx] = a.func_name;
    } else {
      // Positional actual. Bind it, honoring the naming exceptions.
      auto bind = [&](std::size_t i) {
        param_val[i]  = a.node;
        param_set[i]  = true;
        param_func[i] = a.func_name;
      };
      const auto next_unset = [&](std::size_t from) -> std::size_t {
        for (std::size_t i = from; i < nbind; ++i) {
          if (!param_set[i]) {
            return i;
          }
        }
        return nbind;
      };
      // Pass-by-ref (`f(ref x)`) binds positionally by design — exempt from the
      // naming rules — and so does the UFCS receiver bound to `self` (the first
      // positional actual when the callee declares self).
      if (a.is_ref_pass) {
        const auto slot = next_unset(0);
        if (slot >= nbind) {
          if (has_vararg) {
            vararg_pos.push_back(a.node);
            continue;
          }
          fcall_arg_fail(call_span,
                         "fcall-too-many-args",
                         std::format("too many positional arguments in call to `{}`", callee_name),
                         "remove the extra argument(s) or pass them by name");
        }
        bind(slot);
        continue;
      }
      if (has_self && !param_set[0]) {
        bind(0);  // self ← UFCS receiver
        continue;
      }
      // With a var-arg param, fixed leading params bind positionally
      // in declaration order; every actual past them is a positional leftover
      // gathered into the var-arg tuple (`args[i]`). The naming-disambiguation
      // exceptions below are for fully-fixed signatures and are skipped here.
      if (has_vararg) {
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot < nbind) {
          bind(slot);
        } else {
          vararg_pos.push_back(a.node);
        }
        continue;
      }
      // Constructor call (synthesized — no user-written call site to hold to
      // the naming rules): construction args bind in tuple order, mirroring
      // `init(ref y, "hello", 44)` in 07-typesystem.md.
      if (is_ctor_call) {
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot >= nparams) {
          fcall_arg_fail(call_span,
                         "fcall-too-many-args",
                         std::format("too many constructor arguments for `{}`", callee_name),
                         "the construction value has more entries than the init overload's parameters");
        }
        bind(slot);
        continue;
      }
      // Positional TUPLE actual → a flattened tuple-param GROUP. A tuple-typed
      // param `p:(x,y)` is flattened to leaves `p.x`,`p.y`; a positional tuple
      // actual binds by EXPANDING its fields into those leaves (the positional
      // mirror of the named-arg expansion above). signature_matches has already
      // rejected candidates whose only params are scalars, so this only fires for
      // the genuine tuple-param candidate. (2f-arg_naming_tuple.)
      if (a.node.is_ref()) {
        auto bf              = try_bundle_fields(a.node.get_name());
        bool is_tuple_actual = false;
        if (bf && !bf->empty()) {  // genuine tuple (named field or >1 entry), not a 1-entry scalar
          is_tuple_actual = bf->size() > 1;
          for (const auto& [fld, fv] : *bf) {
            (void)fv;
            if (fld.find_first_not_of("0123456789") != std::string::npos) {
              is_tuple_actual = true;
              break;
            }
          }
        }
        if (is_tuple_actual) {
          absl::flat_hash_map<std::string, std::vector<std::size_t>> groups;
          for (std::size_t i = (has_self ? 1u : 0u); i < nbind; ++i) {
            if (param_set[i]) {
              continue;
            }
            const auto& pn = io.inputs[i].name;
            if (auto dp = pn.rfind('.'); dp != std::string::npos) {
              groups[pn.substr(0, dp)].push_back(i);
            }
          }
          bool matched = false;
          for (auto& [prefix, idxs] : groups) {
            if (idxs.size() != bf->size()) {
              continue;
            }
            auto leaf_idx = [&](std::string_view fld) -> std::size_t {
              for (std::size_t idx : idxs) {
                if (io.inputs[idx].name.substr(prefix.size() + 1) == fld) {
                  return idx;
                }
              }
              return nbind;
            };
            bool ok = true;
            for (const auto& [fld, val] : *bf) {
              if (val.is_invalid() || leaf_idx(fld) >= nbind) {
                ok = false;
                break;
              }
            }
            if (!ok) {
              continue;
            }
            for (const auto& [fld, val] : *bf) {
              const auto idx = leaf_idx(fld);
              param_val[idx] = Lnast_node::create_const(val.to_pyrope());
              param_set[idx] = true;
            }
            matched = true;
            break;
          }
          if (matched) {
            continue;
          }
        }
      }
      // Exception 2: a bare variable whose name matches an unset, non-self
      // parameter binds to THAT parameter — so reordered calls like `f(b, a)`
      // bind by name, not position.
      if (a.node.is_ref()) {
        const auto nidx = param_index(a.node.get_name());
        if (nidx < nparams && nidx >= (has_self ? 1u : 0u) && !param_set[nidx]) {
          bind(nidx);
          continue;
        }
      }
      // Exception 1: exactly one non-self parameter — a single positional value
      // (literal, expression, or non-matching variable) maps unambiguously.
      if (n_named_params == 1) {
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot < nparams) {
          bind(slot);
          continue;
        }
      }
      // Exception 3: the actual's kind uniquely identifies one unset, typed,
      // non-self parameter (`f(true, 7)` → bool→flag, int→n). Untyped params
      // (kind=none) never match, so an unnamed value beside an untyped param is
      // still ambiguous and must be named.
      if (const auto k = actual_kind(a.node); k != Io_kind::none) {
        std::size_t match = nparams;
        std::size_t count = 0;
        for (std::size_t i = (has_self ? 1u : 0u); i < nparams; ++i) {
          if (!param_set[i] && io.inputs[i].kind == k) {
            match = i;
            ++count;
          }
        }
        if (count == 1) {
          bind(match);
          continue;
        }
      }
      // Otherwise the positional argument is ambiguous and must be named.
      const auto  slot  = next_unset(has_self ? 1 : 0);
      std::string pname = slot < nparams ? io.inputs[slot].name : std::string{"argument"};
      if (slot >= nparams) {
        fcall_arg_fail(call_span,
                       "fcall-too-many-args",
                       std::format("too many positional arguments in call to `{}`", callee_name),
                       "remove the extra argument(s) or pass them by name");
      }
      fcall_arg_fail(call_span,
                     "fcall-unnamed-arg",
                     std::format("argument `{}` must be named (`{}=...`) in call to `{}`", pname, pname, callee_name),
                     "name the argument, or pass a variable whose name matches the parameter");
    }
  }

  // ── pipe/mod declines ─────────────────────────────────────────────────────
  // These run AFTER the argument-naming validation above (06-functions.md
  // §"Argument naming" applies to every resolvable callee regardless of kind),
  // and BEFORE the binding/splice — so a pipe/mod call site is held to the same
  // naming rule as a comb, then routed to its own lowering path.

  // A pipe/mod/fluid TEMPLATE (untyped boundary) is realized per call
  // site as a concrete specialized module. Runs BEFORE the pipe/mod declines
  // (which assume a concrete callee) but AFTER arg-naming validation (the same
  // rules apply to every callee kind). A `ref self` method is NOT a standalone
  // module — it keeps the splice path below. A `comb` template (untyped scalar
  // params / var-args) is excluded here and inlines as usual.
  // Generic `<T,…>` bindings (2f-generics): explicit `<…>` args win, else
  // inferred from the declared types of the actuals at `:T` positions.
  // Resolved once here — both the specialize path and the comb splice
  // substitute from the same map. Also validates `<…>` on a non-generic
  // callee and unification conflicts (fatal).
  const auto gbinds = resolve_generic_binds(callee, io, param_val, param_set, nbind, explicit_generics, callee_name, call_span);

  if (callee->is_template()) {
    const auto k         = callee->get_lambda_kind();
    const bool is_method = !io.inputs.empty() && io.inputs[0].name == "self";
    if ((k == "mod" || k == "pipe" || k == "fluid") && !is_method) {
      if (maybe_specialize_template_call(callee,
                                         io,
                                         param_val,
                                         param_set,
                                         nbind,
                                         has_vararg,
                                         vararg_pos,
                                         vararg_named,
                                         dst_name,
                                         callee_name,
                                         call_span,
                                         gbinds)) {
        return true;
      }
    }
  }

  // A `pipe` callee (any output carries a stages annotation) is
  // never comb-inlined: its outputs are flopped, and a call site must consume
  // it via `stage[N]` (later phase). Decline so the call surfaces unresolved
  // instead of silently dropping the latency.
  for (const auto& oe : io.outputs) {
    if (oe.stages_min > 0) {
      return false;
    }
  }

  // A plain `mod` callee is its own module: the call becomes a
  // Sub instance, never a comb splice — even when every declared
  // output cycle is 0, a mod may hold state. A `ref self` mod METHOD keeps
  // the splice path (mod-init constructors, `y.method(...)`): recognized by
  // its `self` io entry.
  if (callee->get_lambda_kind() == "mod") {
    bool has_self_input = false;
    for (const auto& ie : io.inputs) {
      if (ie.name == "self") {
        has_self_input = true;
        break;
      }
    }
    if (!has_self_input) {
      return false;
    }
  }

  // Every non-self FIXED parameter must be bound — io_meta carries no defaults,
  // so an unset input means the caller omitted a required argument. The var-arg
  // slot (index nbind, when present) is always satisfied — it gathers zero or
  // more leftovers — so it is excluded from this check.
  for (std::size_t i = (has_self ? 1 : 0); i < nbind; ++i) {
    if (!param_set[i]) {
      fcall_arg_fail(call_span,
                     "fcall-missing-arg",
                     std::format("missing required argument `{}` in call to `{}`", io.inputs[i].name, callee_name),
                     "provide the argument by name");
    }
  }

  // Typed `self:T`: the bound receiver must satisfy `receiver does
  // T` (structural; both call forms and the tuple-field method dispatch land
  // here). Untyped self skips the check entirely.
  if (has_self && param_set[0] && !io.inputs[0].type_name.empty()) {
    check_self_does(call_span, callee_name, io.inputs[0].type_name, param_val[0]);
  }

  // Ref-actual mutability: a `ref` param (incl. `ref self`, whose
  // actual is the receiver on either call form) writes back into the caller's
  // variable, so a `const` or `type` binding can never be the actual. Non-ref
  // `self` stays callable on a type binding (read-only over the defaults).
  for (std::size_t i = 0; i < nparams; ++i) {
    if (!io.inputs[i].is_ref || !param_set[i] || !param_val[i].is_ref()) {
      continue;
    }
    const bool is_self_param = has_self && i == 0;
    if (is_self_param && is_ctor_call) {
      continue;  // the constructor is the one legal `ref self` writer of a const receiver
    }
    const auto storage = try_decl_storage(param_val[i].get_name());
    if (storage != upass::uPass::Decl_storage::const_storage && storage != upass::uPass::Decl_storage::type_storage) {
      continue;
    }
    const std::string_view what = storage == upass::uPass::Decl_storage::type_storage ? "type binding" : "const";
    fcall_arg_fail(call_span,
                   "fcall-ref-const",
                   is_self_param
                       ? std::format("cannot call `ref self` method `{}` on {} `{}`", callee_name, what, param_val[i].get_name())
                       : std::format("cannot pass {} `{}` to `ref` parameter `{}` of `{}`",
                                     what,
                                     param_val[i].get_name(),
                                     io.inputs[i].name,
                                     callee_name),
                   "a `ref` parameter writes back into the caller's variable; use a `mut` value");
  }

  // Recursive callees may only be spliced when every actual is a comptime
  // constant — only then does the body's base-case condition fold so
  // process_if prunes the recursive arm and the unroll terminates. With a
  // non-const arg (e.g. the standalone function body, where the param is an
  // unbound input) the recursion would never bottom out and just runs to the
  // fuel cap; leave those as a runtime func_call (→ evaluator / real call).
  if (recursive_callees_.contains(std::string(callee->get_top_module_name()))) {
    for (std::size_t i = 0; i < nparams; ++i) {
      if (!param_set[i] || !param_func[i].empty()) {
        continue;  // unset or function-valued param — not a numeric base-case driver
      }
      const auto& pv         = param_val[i];
      bool        is_const_a = pv.is_const();
      if (pv.is_ref()) {
        auto fv    = try_fold_ref(pv.get_name());
        is_const_a = fv && !fv->is_invalid() && !fv->has_unknowns();
        // A bundle actual (e.g. `tree_sum(v = data4, …)`) folds via the shared
        // ST instead of scalar fold_ref: treat it as const when every field is
        // a concrete comptime value so the recursion's base case still folds.
        if (!is_const_a) {
          if (auto bf = try_bundle_fields(pv.get_name()); bf && !bf->empty()) {
            is_const_a = true;
            for (const auto& [k, v] : *bf) {
              if (v.is_invalid() || v.has_unknowns()) {
                is_const_a = false;
                break;
              }
            }
          }
        }
      }
      if (!is_const_a) {
        lm->restore_cursor(saved);
        return false;
      }
    }
  }

  // ── Splice ───────────────────────────────────────────────────────────────
  const uint32_t    salt = ++inline_seq_;
  const std::string tag  = std::format("inl{}_", salt);

  // Prologue: declare param + output widths (so `<tag>x.[bits]` folds), then
  // bind param values. Ref-param actuals are remembered for write-back.
  const bool uses_placeholders = placeholder_callees_.contains(std::string(callee->get_top_module_name()));
  std::vector<std::pair<std::string, Lnast_node>>                 writebacks;
  // Function-valued params: record the body-local name → bound function so the
  // body's `f(x)` resolves; restored after the body walk. No value is emitted.
  std::vector<std::pair<std::string, std::optional<std::string>>> saved_func_bindings;
  for (std::size_t i = 0; i < nparams; ++i) {
    const auto& e = io.inputs[i];
    // Register the var-arg leftovers for try_resolve_vararg_get.
    // Positional leftovers take the canonical decimal keys "0","1",… (so the
    // body's `args[i]` reads them — a tuple_get index canonicalizes to that
    // key); named leftovers keep their names (`args.NAME`). No tuple node is
    // emitted: each access is rewritten to a direct copy during the body walk,
    // so tolg never sees a runtime tuple_add/tuple_get it cannot lower.
    if (has_vararg && i == nbind) {
      const auto pname   = upass::Lnast_manager::make_inlined_name(tag, e.name);
      auto&      entries = vararg_bindings_[pname];
      entries.clear();
      entries.reserve(vararg_pos.size() + vararg_named.size());
      for (std::size_t p = 0; p < vararg_pos.size(); ++p) {
        entries.emplace_back(std::to_string(p), vararg_pos[p]);
      }
      for (auto& [k, v] : vararg_named) {
        entries.emplace_back(k, v);
      }
      continue;
    }
    if (!param_func[i].empty()) {
      auto it = func_param_bindings_.find(e.name);
      saved_func_bindings.emplace_back(e.name,
                                       it == func_param_bindings_.end() ? std::nullopt : std::optional<std::string>(it->second));
      func_param_bindings_[e.name] = param_func[i];
      continue;  // function value — no width/value binding
    }
    const auto pname = upass::Lnast_manager::make_inlined_name(tag, e.name);
    const auto gbit  = e.type_name.empty() ? gbinds.end() : gbinds.find(e.type_name);
    if (e.bits > 0) {
      emit_inline_typespec(pname, e.bits, e.is_signed);
    } else if (gbit != gbinds.end() && gbit->second.type_name.empty() && gbit->second.kind == Io_kind::boolean) {
      // `a:T` with T bound to `bool` — must precede the (max||min) branch since
      // a bool bind also carries max=1/min=0 (else it would be typed int(1,0)).
      emit_inline_typespec_bool(pname);
    } else if (gbit != gbinds.end() && gbit->second.type_name.empty() && (gbit->second.max || gbit->second.min)) {
      // `a:T` with T bound (explicit `<…>` or unified from the actuals):
      // substitute the concrete envelope — macro expansion, so the normal
      // range-fit rules then judge the actual against it.
      emit_inline_typespec_range(pname, gbit->second.max, gbit->second.min);
    } else if (param_set[i] && param_val[i].is_ref()) {
      // Untyped param (`comb reverse(x)`): the signature carries no width, so
      // adopt the actual argument's declared type at this call site — the
      // param's type is fixed by the caller variable (`reverse(x0:u6)` → x:u6,
      // `reverse(x2:s4)` → x:s4). Without this the inlined body's `.[bits]`/
      // `.[max]`/`.[min]` (declared-type-driven) fold to nil even though the
      // value is bound, so a loop like `for i in 0..<x.[bits]` never runs.
      // The declared range comes from the attributes pass (shared-ST); the
      // walk has already processed the actual's declaration by this point.
      if (auto dt = try_decl_type(param_val[i].get_name())) {
        emit_inline_typespec_range(pname, dt->range_max, dt->range_min);
      }
    }
    if (param_set[i]) {
      emit_inline_binding(pname, param_val[i]);
      if (e.is_ref && param_val[i].is_ref()) {
        writebacks.emplace_back(std::string(param_val[i].get_name()), Lnast_node::create_ref(pname));
      }
      // Positional-placeholder bodies read params as `_i` (and `_` for a single
      // param) instead of by name — bind those aliases so the body folds.
      if (uses_placeholders) {
        emit_inline_binding(upass::Lnast_manager::make_inlined_name(tag, "_" + std::to_string(i)), Lnast_node::create_ref(pname));
        if (nparams == 1) {
          emit_inline_binding(upass::Lnast_manager::make_inlined_name(tag, "_"), Lnast_node::create_ref(pname));
        }
      }
    }
  }
  // Bind each generic NAME inside the inline frame so body `:T` slots
  // (declare type refs, renamed to `inlN_T`) resolve through the normal
  // named-type machinery.
  for (const auto& [g, gb] : gbinds) {
    if (gb.type_name.empty() && gb.kind == Io_kind::boolean) {
      emit_inline_typespec_bool(upass::Lnast_manager::make_inlined_name(tag, g));
    } else if (gb.type_name.empty() && (gb.max || gb.min)) {
      emit_inline_typespec_range(upass::Lnast_manager::make_inlined_name(tag, g), gb.max, gb.min);
    }
  }
  for (const auto& o : io.outputs) {
    const auto oname = upass::Lnast_manager::make_inlined_name(tag, o.name);
    if (o.bits == 0 && !o.type_name.empty()) {
      if (auto it = gbinds.find(o.type_name); it != gbinds.end() && it->second.type_name.empty()) {
        if (it->second.kind == Io_kind::boolean) {
          // `-> (r:T)` with T bound to `bool` — precede the range branch.
          emit_inline_typespec_bool(oname);
          emit_inline_binding(oname, Lnast_node::create_const("nil"));
          continue;
        }
        if (it->second.max || it->second.min) {
          // `-> (r:T)` with T bound — substitute the concrete envelope.
          emit_inline_typespec_range(oname, it->second.max, it->second.min);
          emit_inline_binding(oname, Lnast_node::create_const("nil"));
          continue;
        }
      }
    }
    emit_inline_typespec(oname, o.bits, o.is_signed);
    // Declare the output in the inlined top scope (mirrors a real body's io
    // `assign res = nil : T`). Without this, an output assigned inside a
    // branch block (`if c { res = … }`) anchors to that block's scope and is
    // discarded on block-leave — before the epilogue can read it. A nil init
    // here is harmless: a taken branch overwrites it in this same top scope.
    emit_inline_binding(oname, Lnast_node::create_const("nil"));
  }

  // Body: walk the callee stmts in place (names rewritten by the frame tag)
  // straight into the caller's current staging stmts. pop_source resets the
  // cursor regardless of how the body walk left it.
  active_inline_callees_.push_back(callee.get());
  flush_deferred_emits();  // flush caller-tree parked writes before entering
  // Call-site id, re-minted into the root locator (the calling
  // tree may itself be a callee in nested inlining), anchors every node
  // spliced from this body via combine(callee_def, call_site) in carry_srcid.
  {
    const auto& call_ln      = lm->get_lnast();
    auto        call_site_id = call_ln->get_srcid(call_nid);
    if (call_site_id != hhds::SourceId_invalid && call_ln.get() != root_lnast_.get()) {
      call_site_id = root_lnast_->source_locator().import_from(call_ln->source_locator(), call_site_id);
    }
    inline_call_sites_.push_back(call_site_id);
  }
  lm->push_source(callee, tag, salt);
  if (lm->move_to_child()) {
    if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_io) {
      lm->move_to_sibling();
    }
    if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_stmts && lm->move_to_child()) {
      do {
        process_lnast();
      } while (lm->move_to_sibling());
    }
  }
  flush_deferred_emits();  // flush callee-tree parked writes before leaving
  lm->pop_source();
  inline_call_sites_.pop_back();
  active_inline_callees_.pop_back();
  // Drop this frame's var-arg gather (the tag is unique per call
  // site, so this only prunes stale state; nested frames used distinct tags).
  if (has_vararg) {
    vararg_bindings_.erase(upass::Lnast_manager::make_inlined_name(tag, io.inputs[nbind].name));
  }
  // Restore the func-param bindings shadowed by this frame.
  for (const auto& [key, old] : saved_func_bindings) {
    if (old) {
      func_param_bindings_[key] = *old;
    } else {
      func_param_bindings_.erase(key);
    }
  }

  // Epilogue: map the callee outputs back to the caller's dst, then apply
  // ref-param write-backs.
  //   - single output  → `dst = <tag>out` (scalar or whole-bundle copy)
  //   - multiple outputs → `dst = (out0=<tag>out0, …)` so the caller's
  //     destructuring tuple_gets (`dst.out0`) fold (see fcall5.prp).
  // A signed output is reinterpreted to its declared width first (mirrors the
  // deleted evaluator's adjust_for_type): a bit-slice like `res:s4 = b#[0..<4]`
  // yields the raw bits 0b1110 = 14, which as s4 must read back as -2.
  auto output_ref = [&](const auto& o) -> Lnast_node {
    const auto raw = upass::Lnast_manager::make_inlined_name(tag, o.name);
    if (o.is_signed && o.bits > 0) {
      const auto sx = upass::Lnast_manager::make_inlined_name(tag, o.name + "_sx");
      emit_inline_sext(sx, raw, o.bits - 1);
      return Lnast_node::create_ref(sx);
    }
    return Lnast_node::create_ref(raw);
  };
  // Regroup FLATTENED output leaves back into LOGICAL outputs. A tuple-typed
  // output `p:(first,second)` is flattened in io_meta to leaves `p.first`,
  // `p.second` (just like a tuple param); they must be regrouped by their
  // top-level (pre-dot) prefix so a SINGLE logical output binds correctly:
  //   - one scalar output       → dst = value          (name dropped)
  //   - one TUPLE output         → dst = (sub=val, …)    (name dropped; dst IS the tuple)
  //   - N logical outputs        → dst = (lname=…, …)    (splat, picked by destructure)
  // (2f-arg_naming_tuple: the symmetric of the call-arg tuple regroup.)
  std::vector<std::string>                                                 logical_order;
  absl::flat_hash_map<std::string, std::vector<std::pair<std::string, Lnast_node>>> logical;
  for (const auto& o : io.outputs) {
    const auto  dp    = o.name.find('.');
    std::string lname = dp == std::string::npos ? o.name : o.name.substr(0, dp);
    std::string sub   = dp == std::string::npos ? std::string{} : o.name.substr(dp + 1);
    if (!logical.contains(lname)) {
      logical_order.push_back(lname);
    }
    logical[lname].emplace_back(std::move(sub), output_ref(o));
  }
  if (logical_order.empty()) {
    // Void comb (e.g. `top()` called for its casserts) — nothing to bind back.
  } else if (logical_order.size() == 1) {
    auto& leaves = logical[logical_order[0]];
    if (leaves.size() == 1 && leaves[0].first.empty()) {
      emit_inline_binding(dst_name, leaves[0].second);  // single scalar output
    } else {
      emit_inline_tuple(dst_name, leaves);  // single TUPLE output → dst IS the tuple
    }
  } else {
    // >1 logical outputs: splat as a bundle for a destructure to field-pick, and
    // record the result tmp so a WHOLE bind to a single user var is rejected
    // (`const inner = two_output_f()` — see the store handler).
    multi_output_results_.insert(dst_name);
    std::vector<std::pair<std::string, Lnast_node>> fields;
    fields.reserve(logical_order.size());
    for (const auto& lname : logical_order) {
      auto& leaves = logical[lname];
      if (leaves.size() == 1 && leaves[0].first.empty()) {
        fields.emplace_back(lname, leaves[0].second);  // scalar logical output
      } else {
        // A tuple-typed output among several: keep its dotted leaves so the
        // destructure / `.lname.sub` read still resolves (rare; one level).
        for (auto& [sub, ref] : leaves) {
          fields.emplace_back(lname + "." + sub, ref);
        }
      }
    }
    emit_inline_tuple(dst_name, fields);
  }
  for (auto& [caller_var, src] : writebacks) {
    emit_inline_binding(caller_var, src);
  }

  return true;
}

bool uPass_runner::try_resolve_tuple_get() {
  // Cursor on a tuple_get node: [dst(ref), src(ref), field(const|ref)...].
  // A tuple pick with a COMPTIME-known index/name is a comptime STRUCTURAL
  // operation even when the picked VALUE is a runtime signal — so rewrite it to
  // a direct copy `dst = <picked ref>` (tolg cannot lower a surviving
  // tuple_get). The picked ref comes from either (1) a var-arg gathered at the
  // call site (vararg_bindings_), or (2) constprop's slot→ref map for any
  // tuple variable (try_tuple_slot_ref). Falls through (false) on nested access
  // (`t[i].f`), a dynamic index, or a slot with no known runtime ref — constprop
  // folds/diagnoses those on the normal path.
  if (!lm->has_child()) {
    return false;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();  // dst
  if (lm->get_raw_ntype() != Lnast_ntype::Lnast_ntype_ref) {
    lm->restore_cursor(saved);
    return false;
  }
  std::string dst(lm->current_text());
  if (!lm->move_to_sibling() || lm->get_raw_ntype() != Lnast_ntype::Lnast_ntype_ref) {
    lm->restore_cursor(saved);
    return false;
  }
  std::string src(lm->current_text());  // tuple name (frame-tagged inside an inline)
  // Exactly one field segment; nested access falls through.
  if (!lm->move_to_sibling()) {
    lm->restore_cursor(saved);
    return false;
  }
  std::string key;
  if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
    if (auto v = Dlop::from_pyrope(lm->current_text()); v && !v->is_invalid()) {
      key = v->to_field();
    }
  } else if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    // A comptime-known index (a folded loop var) resolves; anything else does not.
    if (auto fv = try_fold_ref(lm->current_text()); fv && fv->is_integer() && !fv->has_unknowns()) {
      key = fv->to_field();  // decimal at any width (to_just_i64 asserts past 62 bits)
    }
  }
  const bool single_segment = lm->is_last_child();
  lm->restore_cursor(saved);
  if (key.empty() || !single_segment) {
    return false;
  }
  // (1) var-arg gathered entries (keyed by the frame-tagged var-arg name).
  if (auto it = vararg_bindings_.find(src); it != vararg_bindings_.end()) {
    for (const auto& [k, node] : it->second) {
      if (k == key) {
        emit_inline_binding(dst, node);  // dst already frame-tagged; emit literal
        return true;
      }
    }
    // Out-of-range var-arg pick: let constprop fold to nil / diagnose.
    return false;
  }
  // (2) general tuple variable whose slot holds a runtime scalar ref.
  if (auto rname = try_tuple_slot_ref(src, key)) {
    emit_inline_binding(dst, Lnast_node::create_ref(*rname));
    return true;
  }
  return false;
}

// ── pipe/mod/fluid template specialization ──────────────────────────

void uPass_runner::copy_subtree_into(const std::shared_ptr<Lnast>& src, const Lnast_nid& src_nid, const std::shared_ptr<Lnast>& dst,
                                     const Lnast_nid& dst_parent,
                                     const absl::flat_hash_map<std::string, Generic_bind>* type_subst) {
  const auto type = src->get_type(src_nid);
  Lnast_nid  newn;
  if (Lnast_ntype::is_ref(type)) {
    // Generic substitution: a body `:T` slot is a `ref T` (SSA strips io
    // type refs; only body declare/type_spec slots carry them). Replace it
    // with the bound concrete type (macro expansion).
    if (type_subst != nullptr) {
      if (auto it = type_subst->find(std::string(src->get_name(src_nid))); it != type_subst->end()) {
        const auto& gb = it->second;
        if (!gb.type_name.empty()) {
          dst->add_child(dst_parent, Lnast_node::create_ref(gb.type_name));
          return;
        }
        if (gb.max || gb.min) {
          auto pt = dst->add_child(dst_parent, Lnast_ntype::create_prim_type_int());
          dst->add_child(pt, Lnast_node::create_const(gb.max ? std::string(gb.max->to_pyrope()) : std::string("nil")));
          dst->add_child(pt, Lnast_node::create_const(gb.min ? std::string(gb.min->to_pyrope()) : std::string("nil")));
          return;
        }
      }
    }
    newn = dst->add_child(dst_parent, Lnast_node::create_ref(src->get_name(src_nid)));
  } else if (Lnast_ntype::is_const(type)) {
    newn = dst->add_child(dst_parent, Lnast_node::create_const(src->get_name(src_nid)));
  } else if (Lnast_ntype::is_invalid(type)) {
    newn = dst->add_child(dst_parent, Lnast_node::create_invalid());
  } else {
    newn = dst->add_child(dst_parent, type);
  }
  // Cross-tree carry: re-mint the id into the clone's own locator so
  // a specialized template body stays attributable to the template's source.
  if (Lnast::srcid_carries(type)) {
    if (const auto id = src->get_srcid(src_nid); id != hhds::SourceId_invalid) {
      dst->set_srcid(newn, dst->source_locator().import_from(src->source_locator(), id));
    }
  }
  for (auto c : src->children(src_nid)) {
    copy_subtree_into(src, c, dst, newn, type_subst);
  }
}

std::shared_ptr<Lnast> uPass_runner::clone_template_specialized(const std::shared_ptr<Lnast>& tmpl, const std::string& mangled,
                                                                const std::vector<Spec_port>& inject,
                                                                const std::vector<Spec_port>& vports, const std::string& vname,
                                                                const std::vector<Spec_port>& out_inject,
                                                                const absl::flat_hash_map<std::string, Generic_bind>& type_subst) {
  auto clone    = std::make_shared<Lnast>(mangled);
  const auto* subst = type_subst.empty() ? nullptr : &type_subst;
  auto src_root = tmpl->get_root();
  auto dst_root = clone->set_root(tmpl->get_type(src_root));  // top
  // Module anchor: the clone keeps pointing at the template's
  // definition (set_root bypasses copy_subtree_into's carry).
  if (const auto id = tmpl->get_srcid(src_root); id != hhds::SourceId_invalid) {
    clone->set_srcid(dst_root, clone->source_locator().import_from(tmpl->source_locator(), id));
  }

  // Emit one concrete input-port store: store(ref(name), const(nil), <type>).
  auto add_port = [&](const Lnast_nid& in_dst, const Spec_port& p, const std::string& name) {
    auto st = clone->add_child(in_dst, Lnast_ntype::create_store());
    clone->add_child(st, Lnast_node::create_ref(name));
    clone->add_child(st, Lnast_node::create_const("nil"));
    if (!p.type_name.empty()) {
      clone->add_child(st, Lnast_node::create_ref(p.type_name));
    } else {
      auto pt = clone->add_child(st, Lnast_ntype::create_prim_type_int());
      clone->add_child(pt, Lnast_node::create_const(p.max ? std::string(p.max->to_pyrope()) : std::string("nil")));
      clone->add_child(pt, Lnast_node::create_const(p.min ? std::string(p.min->to_pyrope()) : std::string("nil")));
    }
  };

  if (vname.empty()) {
    for (auto c : tmpl->children(src_root)) {
      copy_subtree_into(tmpl, c, clone, dst_root, subst);
    }
  } else {
    // Var-arg expansion: rebuild the io (drop the `...vname` marker
    // port, append the N concrete vports) and the body (prefix a
    // `vname = (port…)` reconstruction so the body's args[i]/args.NAME/
    // for-a-in-args lower via the normal tuple/for machinery). Append-only trees
    // can't delete the marker port, hence the fresh rebuild.
    bool io_done = false, body_done = false;
    for (auto c : tmpl->children(src_root)) {
      const auto ct = tmpl->get_type(c);
      if (Lnast_ntype::is_io(ct) && !io_done) {
        io_done     = true;
        auto io_dst = clone->add_child(dst_root, ct);
        auto src_in = tmpl->get_first_child(c);
        if (src_in.is_invalid()) {
          continue;
        }
        auto in_dst = clone->add_child(io_dst, tmpl->get_type(src_in));
        for (auto entry : tmpl->children(src_in)) {
          auto nm = tmpl->get_first_child(entry);
          if (!nm.is_invalid() && tmpl->get_name(nm) == vname) {
            continue;  // drop the `...vname` marker port
          }
          copy_subtree_into(tmpl, entry, clone, in_dst, subst);
        }
        for (const auto& vp : vports) {
          add_port(in_dst, vp, vp.port_name);
        }
        for (auto out = tmpl->get_sibling_next(src_in); !out.is_invalid(); out = tmpl->get_sibling_next(out)) {
          copy_subtree_into(tmpl, out, clone, io_dst, subst);  // output tuple (+ any further io children) verbatim
        }
      } else if (Lnast_ntype::is_stmts(ct) && !body_done) {
        body_done     = true;
        auto body_dst = clone->add_child(dst_root, ct);
        auto recon    = clone->add_child(body_dst, Lnast_ntype::create_tuple_add());
        clone->add_child(recon, Lnast_node::create_ref(vname));
        for (const auto& vp : vports) {
          if (vp.is_named) {
            auto fst = clone->add_child(recon, Lnast_ntype::create_store());
            clone->add_child(fst, Lnast_node::create_ref(vp.field));
            clone->add_child(fst, Lnast_node::create_ref(vp.port_name));
          } else {
            clone->add_child(recon, Lnast_node::create_ref(vp.port_name));
          }
        }
        for (auto bc : tmpl->children(c)) {
          copy_subtree_into(tmpl, bc, clone, body_dst, subst);
        }
      } else {
        copy_subtree_into(tmpl, c, clone, dst_root, subst);
      }
    }
  }
  clone->set_lambda_kind(tmpl->get_lambda_kind());
  clone->set_template(false);
  clone->set_generics({});

  // Inject the concrete type child into each untyped fixed input port. Verbatim
  // copy preserves the io order, so port i lines up with inject[i].
  auto io_n = clone->get_first_child(clone->get_root());
  if (io_n.is_invalid() || !Lnast_ntype::is_io(clone->get_type(io_n))) {
    return clone;
  }
  auto in_tup = clone->get_first_child(io_n);
  if (in_tup.is_invalid()) {
    return clone;
  }
  std::size_t i = 0;
  for (auto entry : clone->children(in_tup)) {
    if (!Lnast_ntype::is_store(clone->get_type(entry))) {
      continue;
    }
    auto name_n = clone->get_first_child(entry);
    if (name_n.is_invalid() || clone->get_name(name_n) == "__empty_tuple") {
      continue;
    }
    if (i < inject.size() && inject[i].inject) {
      if (!inject[i].type_name.empty()) {
        clone->add_child(entry, Lnast_node::create_ref(inject[i].type_name));
      } else {
        auto pt = clone->add_child(entry, Lnast_ntype::create_prim_type_int());
        clone->add_child(pt,
                         Lnast_node::create_const(inject[i].max ? std::string(inject[i].max->to_pyrope()) : std::string("nil")));
        clone->add_child(pt,
                         Lnast_node::create_const(inject[i].min ? std::string(inject[i].min->to_pyrope()) : std::string("nil")));
      }
    }
    ++i;
  }

  // OUTPUT ports: `-> (r:T)` with T bound gets the concrete type too (the
  // outputs tuple is the io node's second tuple_add).
  if (auto out_tup = clone->get_sibling_next(in_tup); !out_tup.is_invalid()) {
    std::size_t oi = 0;
    for (auto entry : clone->children(out_tup)) {
      if (!Lnast_ntype::is_store(clone->get_type(entry))) {
        continue;
      }
      auto name_n = clone->get_first_child(entry);
      if (name_n.is_invalid() || clone->get_name(name_n) == "__empty_tuple") {
        continue;
      }
      if (oi < out_inject.size() && out_inject[oi].inject) {
        if (!out_inject[oi].type_name.empty()) {
          clone->add_child(entry, Lnast_node::create_ref(out_inject[oi].type_name));
        } else {
          auto pt = clone->add_child(entry, Lnast_ntype::create_prim_type_int());
          clone->add_child(
              pt,
              Lnast_node::create_const(out_inject[oi].max ? std::string(out_inject[oi].max->to_pyrope()) : std::string("nil")));
          clone->add_child(
              pt,
              Lnast_node::create_const(out_inject[oi].min ? std::string(out_inject[oi].min->to_pyrope()) : std::string("nil")));
        }
      }
      ++oi;
    }
  }
  return clone;
}

void uPass_runner::emit_specialized_call(const std::string& dst, const std::string& mangled,
                                         const std::vector<Lnast_node>& actuals) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-spec");
  auto s    = std::make_shared<Lnast>(body, "inl-spec");
  auto root = s->set_root(Lnast_ntype::create_func_call());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));
  s->add_child(root, Lnast_node::create_ref(mangled));  // mangled callee → tolg makes the Sub
  for (const auto& a : actuals) {
    s->add_child(root, a);
  }
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // mangled callee isn't in the registry yet → emitted verbatim for tolg
  flush_deferred_emits();
  lm->pop_source();
}

absl::flat_hash_map<std::string, uPass_runner::Generic_bind> uPass_runner::resolve_generic_binds(
    const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io, const std::vector<Lnast_node>& param_val,
    const std::vector<bool>& param_set, std::size_t nbind, const std::vector<std::string>& explicit_generics,
    const std::string& callee_name, const livehd::diag::Span& call_span) {
  absl::flat_hash_map<std::string, Generic_bind> binds;
  const auto&                                    gens = callee->get_generics();
  if (gens.empty()) {
    if (!explicit_generics.empty()) {
      fcall_arg_fail(call_span,
                     "fcall-generic-arity",
                     std::format("`{}` declares no generic parameters but the call binds {}", callee_name, explicit_generics.size()),
                     "drop the `<…>` binding list");
    }
    return binds;
  }

  // One-line type description for the mismatch diagnostic.
  const auto describe = [](const Generic_bind& gb) -> std::string {
    if (!gb.type_name.empty()) {
      return gb.type_name;
    }
    switch (gb.kind) {
      case Io_kind::boolean: return "bool";
      case Io_kind::string : return "string";
      case Io_kind::integer:
        if (gb.max || gb.min) {
          return std::format("int(max={}, min={})",
                             gb.max ? std::string(gb.max->to_pyrope()) : "nil",
                             gb.min ? std::string(gb.min->to_pyrope()) : "nil");
        }
        return "int";
      default: return "untyped";
    }
  };

  // Resolve a TYPE NAME (an explicit `<…>` argument: a `'type'` declare tmp
  // or a named type) into a binding.
  const auto bind_of_type_name = [&](const std::string& tn, std::string from) -> Generic_bind {
    Generic_bind gb;
    gb.from = std::move(from);
    if (const auto f = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), tn); f && f->has_type_spec) {
      switch (f->kind) {
        case upass::decl_facts::Num::unsigned_int:
        case upass::decl_facts::Num::signed_int:
          gb.kind = Io_kind::integer;
          gb.max  = f->range_max;
          gb.min  = f->range_min;
          return gb;
        case upass::decl_facts::Num::boolean:
          gb.kind = Io_kind::boolean;
          gb.max  = *Dlop::from_pyrope("1");
          gb.min  = *Dlop::from_pyrope("0");
          return gb;
        case upass::decl_facts::Num::string: gb.kind = Io_kind::string; return gb;
        case upass::decl_facts::Num::none  : break;
      }
    }
    gb.type_name = tn;  // named type — substituted verbatim (macro expansion)
    return gb;
  };

  // 1) Explicit `<…>` bindings: declaration order, all-or-nothing.
  if (!explicit_generics.empty()) {
    if (explicit_generics.size() != gens.size()) {
      fcall_arg_fail(
          call_span,
          "fcall-generic-arity",
          std::format("`{}` declares {} generic parameter(s) but the call binds {}", callee_name, gens.size(), explicit_generics.size()),
          "bind one type per generic name, in declaration order");
    }
    for (std::size_t i = 0; i < gens.size(); ++i) {
      binds[gens[i]] = bind_of_type_name(explicit_generics[i], std::format("the explicit `<…>` argument {}", i + 1));
    }
    return binds;
  }

  // 2) Inference from the actuals at `:T` positions. Declared types bind;
  // literals contribute their KIND only (an integer literal leaves T's range
  // open — `triadd(a=1,b=2,c=3)` is T = int).
  const auto& caller_io = lm->get_lnast()->io_meta();
  const auto  unify     = [&](const std::string& g, Generic_bind cand) {
    auto [it, inserted] = binds.emplace(g, cand);
    if (inserted) {
      return;
    }
    auto&      prev          = it->second;
    const auto kind_of       = [](const Generic_bind& b) { return b.type_name.empty() ? b.kind : Io_kind::none; };
    const bool same_named    = !prev.type_name.empty() && prev.type_name == cand.type_name;
    bool       compatible    = same_named;
    if (prev.type_name.empty() && cand.type_name.empty() && kind_of(prev) == kind_of(cand)) {
      // Same kind. Integer ranges must agree when BOTH are pinned; a
      // kind-only candidate (literal) folds into the pinned one.
      const bool prev_pinned = prev.max.has_value() || prev.min.has_value();
      const bool cand_pinned = cand.max.has_value() || cand.min.has_value();
      if (!prev_pinned && cand_pinned) {
        prev.max  = cand.max;
        prev.min  = cand.min;
        prev.from = cand.from;
        return;
      }
      if (!cand_pinned) {
        compatible = true;
      } else {
        const auto same_bound = [](const std::optional<Dlop>& a, const std::optional<Dlop>& b) {
          if (a.has_value() != b.has_value()) {
            return false;
          }
          return !a.has_value() || a->same_repr(*b);
        };
        compatible = same_bound(prev.max, cand.max) && same_bound(prev.min, cand.min);
      }
    }
    if (!compatible) {
      fcall_arg_fail(call_span,
                     "fcall-generic-mismatch",
                     std::format("generic `{}` of `{}` does not unify: {} binds `{}` but {} binds `{}`",
                                 g,
                                 callee_name,
                                 prev.from,
                                 describe(prev),
                                 cand.from,
                                 describe(cand)),
                     "every actual typed with the same generic must share one type; bind it explicitly with `f<type>(…)` if intended");
    }
  };
  for (std::size_t i = 0; i < nbind && i < io.inputs.size(); ++i) {
    const auto& e = io.inputs[i];
    if (e.type_name.empty() || !param_set[i]) {
      continue;
    }
    if (std::find(gens.begin(), gens.end(), e.type_name) == gens.end()) {
      continue;  // a real named type (x:Point), not a generic
    }
    const auto&  av   = param_val[i];
    const auto   from = std::format("argument `{}`", e.name);
    Generic_bind cand;
    cand.from = from;
    if (av.is_const()) {
      const auto t = av.get_name();
      if (t == "true" || t == "false") {
        cand.kind = Io_kind::boolean;
        cand.max  = *Dlop::from_pyrope("1");
        cand.min  = *Dlop::from_pyrope("0");
      } else if (!t.empty() && (t.front() == '\'' || t.front() == '"')) {
        cand.kind = Io_kind::string;
      } else if (t == "nil") {
        continue;  // nil carries no type
      } else {
        cand.kind = Io_kind::integer;  // kind only — the range stays open
      }
      unify(e.type_name, std::move(cand));
      continue;
    }
    if (!av.is_ref()) {
      continue;
    }
    const auto an = std::string(av.get_name());
    if (auto tn = try_typename(an); !tn.empty()) {
      cand.type_name = std::string(tn);
    } else if (const auto f = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), an);
               f && f->has_type_spec && (f->range_max || f->range_min)) {
      cand.kind = (f->kind == upass::decl_facts::Num::boolean) ? Io_kind::boolean : Io_kind::integer;
      cand.max  = f->range_max;
      cand.min  = f->range_min;
    } else {
      const Lnast_io_entry* ci = nullptr;
      for (const auto& ce : caller_io.inputs) {
        if (ce.name == an) {
          ci = &ce;
          break;
        }
      }
      if (ci != nullptr && ci->kind == Io_kind::boolean) {
        cand.kind = Io_kind::boolean;
        cand.max  = *Dlop::from_pyrope("1");
        cand.min  = *Dlop::from_pyrope("0");
      } else if (ci != nullptr && ci->bits > 0) {
        cand.kind = Io_kind::integer;
        if (ci->is_signed) {
          cand.max = *Dlop::get_mask_value(ci->bits - 1);
          cand.min = *Dlop::get_neg_mask_value(ci->bits - 1);
        } else {
          cand.max = *Dlop::get_mask_value(ci->bits);
          cand.min = *Dlop::from_pyrope("0");
        }
      } else if (try_scalar_kind(an) == Io_kind::boolean) {
        cand.kind = Io_kind::boolean;
        cand.max  = *Dlop::from_pyrope("1");
        cand.min  = *Dlop::from_pyrope("0");
      } else {
        continue;  // untyped ref — contributes nothing
      }
    }
    unify(e.type_name, std::move(cand));
  }
  return binds;
}

bool uPass_runner::maybe_specialize_template_call(const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io,
                                                  const std::vector<Lnast_node>& param_val, const std::vector<bool>& param_set,
                                                  std::size_t nbind, bool has_vararg, const std::vector<Lnast_node>& vararg_pos,
                                                  const std::vector<std::pair<std::string, Lnast_node>>& vararg_named,
                                                  const std::string& dst_name, const std::string& callee_name,
                                                  const livehd::diag::Span& call_span,
                                                  const absl::flat_hash_map<std::string, Generic_bind>& gbinds) {
  const auto kind = std::string(callee->get_lambda_kind());

  // Readable bits/sign suffix from a declared (max,min) range — mirrors
  // upass_ssa type_info_from so the name matches the lowered width.
  auto suffix_for = [](const std::optional<Dlop>& max, const std::optional<Dlop>& min) -> std::string {
    const bool is_signed = !(min && !min->is_negative());
    int        bits      = 0;
    if (max && max->is_integer()) {
      if (!is_signed) {
        bits = max->is_known_zero() ? 0 : static_cast<int>(max->get_bits() - 1);
      } else {
        const int mb = static_cast<int>(max->get_bits());
        const int nb = (min && min->is_integer()) ? static_cast<int>(min->get_bits()) : mb;
        bits         = std::max(mb, nb);
      }
    }
    return (is_signed ? "s" : "u") + std::to_string(bits);
  };

  // The actual's declared type can come from a body declaration
  // (try_decl_type / try_typename) OR — when the actual is itself an input of
  // the tree holding the call site — from that tree's io_meta. Build a (bits,
  // signed) → (max,min) prim_type_int range for the latter (mirrors SSA's
  // emit_section).
  const auto& caller_io    = lm->get_lnast()->io_meta();
  auto        caller_input = [&](const std::string& nm) -> const Lnast_io_entry* {
    for (const auto& ce : caller_io.inputs) {
      if (ce.name == nm) {
        return &ce;
      }
    }
    return nullptr;
  };

  std::vector<std::string> suffix;

  // Read one actual's DECLARED type into a Spec_port (inject=true) and push its
  // readable suffix; an untyped actual is a fatal call-site error. Shared by the
  // fixed params and the var-arg leftovers (decision 4). `label` names the actual
  // for the diagnostic.
  auto type_from_actual = [&](const Lnast_node& av, bool av_set, std::string_view label) -> Spec_port {
    const auto fail = [&]() {
      fcall_arg_fail(call_span,
                     "fcall-untyped-actual",
                     std::format("argument `{}` to template `{}` has no declared type — a `{}` boundary needs an explicit width",
                                 label,
                                 callee_name,
                                 kind),
                     "annotate the actual, e.g. `x:u8`");
    };
    if (!av_set || !av.is_ref()) {
      fail();
    }
    Spec_port  sp;
    const auto an = std::string(av.get_name());
    if (auto tn = try_typename(an); !tn.empty()) {
      sp = {true, std::nullopt, std::nullopt, std::string(tn)};
      suffix.push_back(std::string(tn));
    } else if (auto dt = try_decl_type(an); dt && (dt->range_max || dt->range_min)) {
      sp = {true, dt->range_max, dt->range_min, {}};
      suffix.push_back(suffix_for(dt->range_max, dt->range_min));
    } else if (const auto* ci = caller_input(an); ci != nullptr && (ci->bits > 0 || ci->kind == Io_kind::boolean)) {
      if (ci->kind == Io_kind::boolean) {
        sp = {true, *Dlop::from_pyrope("1"), *Dlop::from_pyrope("0"), {}};
        suffix.emplace_back("bool");
      } else if (ci->is_signed) {
        sp = {true, *Dlop::get_mask_value(ci->bits - 1), *Dlop::get_neg_mask_value(ci->bits - 1), {}};
        suffix.push_back("s" + std::to_string(ci->bits));
      } else {
        sp = {true, *Dlop::get_mask_value(ci->bits), *Dlop::from_pyrope("0"), {}};
        suffix.push_back("u" + std::to_string(ci->bits));
      }
    } else if (try_scalar_kind(an) == Io_kind::boolean) {
      sp = {true, *Dlop::from_pyrope("1"), *Dlop::from_pyrope("0"), {}};
      suffix.emplace_back("bool");
    } else {
      fail();
    }
    return sp;
  };

  // A port typed with a GENERIC name is not "already typed": the binding
  // substitutes the concrete type (macro expansion).
  const auto& gens            = callee->get_generics();
  auto        is_generic_name = [&](const std::string& tn) {
    return !tn.empty() && std::find(gens.begin(), gens.end(), tn) != gens.end();
  };
  auto spec_port_of_bind = [&](const Generic_bind& gb) -> std::optional<Spec_port> {
    if (!gb.type_name.empty()) {
      suffix.push_back(gb.type_name);
      return Spec_port{true, std::nullopt, std::nullopt, gb.type_name};
    }
    if (gb.kind == Io_kind::boolean) {
      suffix.emplace_back("bool");
      return Spec_port{true, *Dlop::from_pyrope("1"), *Dlop::from_pyrope("0"), {}};
    }
    if (gb.kind == Io_kind::integer && (gb.max || gb.min)) {
      suffix.push_back(suffix_for(gb.max, gb.min));
      return Spec_port{true, gb.max, gb.min, {}};
    }
    return std::nullopt;  // kind-only / string — no concrete port type
  };

  std::vector<Spec_port> inject(nbind);
  for (std::size_t i = 0; i < nbind; ++i) {
    const auto& e          = io.inputs[i];
    const bool  is_generic = is_generic_name(e.type_name);
    const bool  already_typed
        = !is_generic && (e.bits > 0 || !e.type_name.empty() || e.kind == Io_kind::boolean);
    if (already_typed) {
      inject[i].inject = false;
      if (!e.type_name.empty()) {
        suffix.push_back(e.type_name);
      } else if (e.kind == Io_kind::boolean) {
        suffix.emplace_back("bool");
      } else {
        suffix.push_back((e.is_signed ? "s" : "u") + std::to_string(e.bits));
      }
      continue;
    }
    if (is_generic) {
      if (auto it = gbinds.find(e.type_name); it != gbinds.end()) {
        if (auto sp = spec_port_of_bind(it->second); sp) {
          inject[i] = std::move(*sp);
          continue;
        }
      }
      // Unbound generic at a hardware boundary: the actual's declared type
      // decides, exactly like an untyped port (fatal when untyped).
    }
    inject[i] = type_from_actual(param_val[i], param_set[i], e.name);
  }

  // `-> (r:T)` outputs with T bound get the concrete type injected too (the
  // suffix tokens come from the inputs only, matching the untyped-param path).
  std::vector<Spec_port> out_inject(io.outputs.size());
  {
    const auto saved_suffix_n = suffix.size();
    for (std::size_t i = 0; i < io.outputs.size(); ++i) {
      const auto& o = io.outputs[i];
      if (!is_generic_name(o.type_name)) {
        continue;
      }
      if (auto it = gbinds.find(o.type_name); it != gbinds.end()) {
        if (auto sp = spec_port_of_bind(it->second); sp) {
          out_inject[i] = std::move(*sp);
        }
      }
    }
    suffix.resize(saved_suffix_n);  // output bindings never alter the mangled name
  }

  // Var-arg boundary (`...vname`): expand the N leftover actuals into
  // N concrete typed ports on the clone (positional → vname__0…, named →
  // vname__KEY). clone_template_specialized drops the `...vname` io port, adds
  // these, and prefixes the body with `vname = (port…)` so args[i]/args.NAME/
  // for-a-in-args lower via the normal tuple/for machinery.
  std::vector<Spec_port> vports;
  std::string            vname;
  if (has_vararg) {
    vname         = io.inputs[nbind].name;
    std::size_t k = 0;
    for (const auto& a : vararg_pos) {
      Spec_port vp = type_from_actual(a, true, std::format("{}[{}]", vname, k));
      vp.port_name = std::format("{}__{}", vname, k);
      vp.is_named  = false;
      vports.push_back(std::move(vp));
      ++k;
    }
    for (const auto& [key, a] : vararg_named) {
      Spec_port vp = type_from_actual(a, true, std::format("{}.{}", vname, key));
      vp.port_name = std::format("{}__{}", vname, key);
      vp.is_named  = true;
      vp.field     = key;
      vports.push_back(std::move(vp));
    }
  }

  // Mangled module name: readable + deterministic, so identical signatures map
  // to one module (natural dedup keyed by name). The owning-unit prefix is kept
  // so tolg exact-matches and cross-unit names never collide.
  //
  // Width tokens (`uN`/`sN`/`bool`) contain no `_`, so the `_`-joined readable
  // form is unambiguous for them. A NAMED-type component may contain `_` (or
  // even look like a width token), which could map two DISTINCT signatures of
  // the same template onto one name — and the name-keyed dedup would then
  // silently drop the second clone, mis-wiring that call site. So when any
  // component is not a plain width token, append a deterministic hash of the
  // exact component list (FNV-1a, NUL-free separator) to disambiguate; the
  // common all-primitive case keeps its clean `foo__u8` name.
  auto is_width_token = [](const std::string& s) -> bool {
    if (s == "bool") {
      return true;
    }
    if (s.size() < 2 || (s[0] != 'u' && s[0] != 's')) {
      return false;
    }
    for (std::size_t i = 1; i < s.size(); ++i) {
      if (s[i] < '0' || s[i] > '9') {
        return false;
      }
    }
    return true;
  };
  bool ambiguous = false;
  for (const auto& s : suffix) {
    if (!is_width_token(s)) {
      ambiguous = true;
      break;
    }
  }
  std::string mangled = std::string(callee->get_top_module_name()) + "__";
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    if (i) {
      mangled += "_";
    }
    mangled += suffix[i];
  }
  if (ambiguous) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
    for (const auto& s : suffix) {
      h ^= 0x01;  // component separator (never in an identifier/width token)
      h *= 1099511628211ull;
      for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
      }
    }
    mangled += std::format("_h{:08x}", static_cast<uint32_t>(h & 0xffffffffu));
  }

  if (!specialized_emitted_.contains(mangled)) {
    specialized_emitted_.insert(mangled);
    new_lnasts.push_back(clone_template_specialized(callee, mangled, inject, vports, vname, out_inject, gbinds));
  }

  // Actuals wired to the clone's ports, in io order: fixed params, then the
  // var-arg leftovers (positional then named) → the synthesized vname__* ports.
  std::vector<Lnast_node> actuals;
  actuals.reserve(nbind + vports.size());
  for (std::size_t i = 0; i < nbind; ++i) {
    actuals.push_back(param_val[i]);
  }
  for (const auto& a : vararg_pos) {
    actuals.push_back(a);
  }
  for (const auto& [key, a] : vararg_named) {
    actuals.push_back(a);
  }
  emit_specialized_call(dst_name, mangled, actuals);
  return true;
}

// ── overload-gathering call dispatch (2f-overload) ────────────────────────────

bool uPass_runner::gather_actuals(bool drop_ufcs_receiver, std::vector<Actual>& actuals,
                                  std::vector<std::string>& explicit_generics) {
  // Cursor MUST be on the callee ref (the func_call's 2nd child); the actuals
  // are its following siblings. The cursor is saved/restored here so this can
  // be called twice (overload probe + real bind) without disturbing the caller.
  const auto entry = lm->save_cursor();

  // A ref actual whose raw name is itself a registry function is a higher-order
  // / closure argument: capture the function name so the body's `f(x)` can
  // resolve to it (see func_param_bindings_).
  auto func_actual_name = [&]() -> std::string {
    if (Lnast_ntype::is_ref(lm->get_raw_ntype()) && lookup_callee(lm->current_raw_text()) != nullptr) {
      return std::string(lm->current_raw_text());
    }
    return {};
  };

  bool shape_ok = true;
  while (lm->move_to_sibling()) {
    const auto t = lm->get_raw_ntype();
    if (Lnast_ntype::is_ref(t) || Lnast_ntype::is_const(t)) {
      actuals.push_back(Actual{.is_named = false, .key = {}, .node = lm->current_node(), .func_name = func_actual_name()});
    } else if (Lnast_ntype::is_store(t)) {
      Actual a;
      a.is_named      = true;
      const auto here = lm->save_cursor();
      if (!lm->move_to_child()) {
        shape_ok = false;
        lm->restore_cursor(here);
        break;
      }
      a.key = std::string(lm->current_raw_text());  // param name — not renamed
      // `f(ref x)` lowers to `assign(__ref_arg, x)` — a POSITIONAL pass-by-ref
      // actual, not a named one. Strip the marker so it binds by position and
      // the `ref` param's write-back fires (see the io.is_ref check at bind).
      if (a.key == "__ref_arg") {
        a.is_named    = false;
        a.is_ref_pass = true;
        a.key.clear();
      }
      // The UFCS receiver marker is positional too (binds to `self`). A
      // namespace access drops the receiver entirely — it names the namespace.
      if (a.key == call_ufcs_arg_marker) {
        if (drop_ufcs_receiver) {
          lm->restore_cursor(here);
          continue;
        }
        a.is_named = false;
        a.key.clear();
      }
      // Explicit generic binding — consumed here, never an actual. The value
      // ref is frame-renamed text, so current_text(), not raw.
      if (a.key == call_generic_arg_marker) {
        if (!lm->move_to_sibling()) {
          shape_ok = false;
          lm->restore_cursor(here);
          break;
        }
        explicit_generics.emplace_back(lm->current_text());
        lm->restore_cursor(here);
        continue;
      }
      // Call-argument spread (`f(..., ...rest)`): expand the referenced bundle's
      // fields into named (non-numeric key) / positional (numeric key) actuals.
      // The bundle is fully folded by constprop before the call resolves, so the
      // field values are concrete consts (same shape as the named-bundle-leaf
      // expansion at the bind loop above). An unresolved bundle expands to
      // nothing (the call then fails arity/naming as before).
      if (a.key == call_spread_arg_marker) {
        if (!lm->move_to_sibling()) {
          shape_ok = false;
          lm->restore_cursor(here);
          break;
        }
        const auto bundle_name = std::string(lm->current_text());
        if (auto bf = try_bundle_fields(bundle_name)) {
          for (const auto& [fld, val] : *bf) {
            if (val.is_invalid()) {
              continue;
            }
            const bool numeric = !fld.empty() && fld.find_first_not_of("0123456789") == std::string::npos;
            Actual     sa;
            sa.is_named = !numeric;
            if (sa.is_named) {
              sa.key = fld;
            }
            sa.node = Lnast_node::create_const(val.to_pyrope());
            actuals.push_back(std::move(sa));
          }
        }
        lm->restore_cursor(here);
        continue;
      }
      if (!lm->move_to_sibling()) {
        shape_ok = false;
        lm->restore_cursor(here);
        break;
      }
      a.func_name = func_actual_name();
      a.node      = lm->current_node();
      lm->restore_cursor(here);
      actuals.push_back(std::move(a));
    } else {
      shape_ok = false;
      break;
    }
  }
  lm->restore_cursor(entry);
  return shape_ok;
}

std::vector<std::string> uPass_runner::overload_candidates_of(std::string_view name) {
  // A gathered set `const add = [f1, f2]` (array `[…]` or tuple `(…)` — same
  // lowering) constprop-records each positional entry under a numeric slot
  // "0","1",… in one of two shapes, depending on whether process_tuple_add
  // could qualify the lambda name in its current module:
  //   (a) string field — `<module>.f` stored as a string Dlop (try_bundle_fields),
  //       used when the gather sits in the same module the lambda registered in;
  //   (b) ref slot — the raw lambda name in the tuple_slot_ref map
  //       (try_tuple_shape + try_tuple_slot_ref), used when the gather is inside a
  //       function body lowered standalone (the qualified name then misses the
  //       registry, so the entry rode the runtime-slot path instead).
  // Collect candidates in tuple order, resolving each via lookup_callee. Strict:
  // EVERY slot must be positional and resolve to a registry lambda, otherwise
  // this is a plain data tuple / mixed bundle, not an overload set (return empty
  // so the caller falls through to the normal non-callee path).
  std::vector<std::string> out;
  if (name.empty()) {
    return out;
  }
  auto unquote = [](std::string s) {
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
      return s.substr(1, s.size() - 2);
    }
    return s;
  };
  // Shape (a): string fields keyed by slot.
  std::map<std::string, std::string> str_field;
  if (auto bf = try_bundle_fields(name)) {
    for (const auto& [key, val] : *bf) {
      if (val.is_string()) {
        str_field[key] = val.to_pyrope();
      }
    }
  }
  // Slot list: the tuple shape is authoritative (it carries positional-ness and
  // every slot, value-typed or runtime-ref). Fall back to the string-field keys
  // when no shape was recorded (a pure comptime string bundle).
  std::vector<std::pair<std::string, bool>> slots;
  if (auto shp = try_tuple_shape(name)) {
    slots = *shp;
  } else {
    for (const auto& [key, _] : str_field) {
      slots.emplace_back(key, true);
    }
  }
  if (slots.empty()) {
    return out;
  }
  std::map<int, std::string> ordered;
  for (const auto& [slot, is_pos] : slots) {
    if (!is_pos || slot.empty() || slot.find_first_not_of("0123456789") != std::string::npos) {
      return {};  // a named field → not a clean positional lambda set
    }
    std::string fn;
    if (auto it = str_field.find(slot); it != str_field.end()) {
      fn = unquote(it->second);  // shape (a)
    } else if (auto r = try_tuple_slot_ref(name, slot)) {
      fn = *r;  // shape (b): raw lambda name
    } else {
      return {};  // slot is neither a fn-string nor a ref → not a lambda set
    }
    if (fn.starts_with("ln:")) {
      fn = fn.substr(3);
    }
    if (!lookup_callee(fn)) {
      return {};  // not a registry lambda
    }
    ordered[std::stoi(slot)] = std::move(fn);
  }
  if (ordered.empty()) {
    return {};
  }
  for (auto& [i, fn] : ordered) {
    out.push_back(std::move(fn));
  }
  return out;
}

bool uPass_runner::signature_matches(const Lnast_tree_io& io, const std::vector<Actual>& actuals, bool is_ctor_call) {
  // CONTRACT: an overloaded and a non-overloaded call decide "can this lambda be
  // called with these actuals?" by the SAME rules — a gathered overload only
  // adds "try the candidates first-to-last, take the first that can be called,
  // else a compile error". So this is a faithful, NON-FATAL mirror of
  // try_inline_func_call's actual→param bind loop (06-functions.md §"Argument
  // naming": positional binds only by exception 1/2/3, named binds by name) plus
  // the missing-arg check and a per-arg scalar kind/range fit (the typed-param
  // check the runner otherwise defers downstream — re-derived here so dispatch
  // can tell candidates apart on type, not just arity). Returns true iff this
  // candidate would accept the call. Conservative on the few shapes it does not
  // model (bundle-leaf expansion of a named actual; typed-self `does`; ref-const)
  // → returns false / leaves them to the chosen candidate's full bind path, which
  // stays the authority for diagnostics. A too-strict skip thus surfaces as a
  // clean no-overload, never a silently-wrong dispatch.
  const std::size_t nparams    = io.inputs.size();
  const bool        has_vararg = nparams > 0 && io.inputs[nparams - 1].is_varargs;
  const std::size_t nbind      = has_vararg ? nparams - 1 : nparams;

  std::vector<Lnast_node> param_val(nparams, Lnast_node::create_invalid());
  std::vector<bool>       param_set(nparams, false);

  auto param_index = [&](std::string_view k) -> std::size_t {
    for (std::size_t i = 0; i < nbind; ++i) {
      if (io.inputs[i].name == k) {
        return i;
      }
    }
    return nbind;
  };
  const bool        has_self       = nbind > 0 && io.inputs[0].name == "self";
  const std::size_t n_named_params = nbind - (has_self ? 1 : 0);

  // Comptime scalar kind of an actual (mirrors the inliner's actual_kind, with
  // bool/string detection for typed refs added for the kind pre-filter).
  auto classify = [&](const Lnast_node& node) -> Io_kind {
    if (node.is_const()) {
      const auto t = node.get_name();
      if (t == "true" || t == "false") {
        return Io_kind::boolean;
      }
      if (!t.empty() && (t.front() == '\'' || t.front() == '"')) {
        return Io_kind::string;
      }
      if (t == "nil") {
        return Io_kind::none;
      }
      if (auto v = Dlop::from_pyrope(t); v && v->is_integer()) {
        return Io_kind::integer;
      }
      return Io_kind::none;
    }
    if (node.is_ref()) {
      if (auto k = try_scalar_kind(node.get_name()); k != Io_kind::none) {
        return k;
      }
      if (try_decl_type(node.get_name()).has_value()) {
        return Io_kind::integer;
      }
    }
    return Io_kind::none;
  };

  for (const auto& a : actuals) {
    if (a.is_named) {
      if (a.key == "self") {
        return false;  // self is never named
      }
      const auto idx = param_index(a.key);
      if (idx >= nbind) {
        if (has_vararg) {
          continue;  // named leftover → var-arg tuple
        }
        return false;  // unknown argument (bundle-leaf expansion not modeled here)
      }
      if (param_set[idx]) {
        return false;  // duplicate
      }
      param_val[idx] = a.node;
      param_set[idx] = true;
    } else {
      const auto next_unset = [&](std::size_t from) -> std::size_t {
        for (std::size_t i = from; i < nbind; ++i) {
          if (!param_set[i]) {
            return i;
          }
        }
        return nbind;
      };
      auto bind = [&](std::size_t i) {
        param_val[i] = a.node;
        param_set[i] = true;
      };
      if (a.is_ref_pass) {
        const auto slot = next_unset(0);
        if (slot >= nbind) {
          if (has_vararg) {
            continue;
          }
          return false;  // too many positional
        }
        bind(slot);
        continue;
      }
      if (has_self && !param_set[0]) {
        bind(0);  // self ← UFCS receiver
        continue;
      }
      if (has_vararg) {
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot < nbind) {
          bind(slot);
        }
        continue;  // fixed params filled; leftover → var-arg tuple
      }
      // Overloaded and non-overloaded calls share the SAME callability rules
      // (06-functions.md §"Argument naming"); a gathered overload only differs
      // in trying candidates first-to-last. So the positional-binding
      // disambiguation below mirrors the real bind path exactly: ctor-style
      // order binding is reserved for is_ctor_call (never reached here — the
      // probe is invoked with is_ctor_call=false), and otherwise a positional
      // actual binds only under exceptions 2/1/3.
      if (is_ctor_call) {
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot >= nparams) {
          return false;
        }
        bind(slot);
        continue;
      }
      // Tuple-shape discrimination (a `does`-style field check): a positional
      // TUPLE actual binds ONLY to a tuple-shaped param — a group of flattened
      // leaves sharing a dotted prefix (`p:(x,y)` → io entries `p.x`,`p.y`) whose
      // field names it structurally covers. It must NEVER fall through to
      // exception 1 and bind to a lone scalar param (`a:int`). So when the actual
      // is a readable bundle, match-or-reject here.
      if (a.node.is_ref()) {
        auto af              = try_bundle_fields(a.node.get_name());
        bool is_tuple_actual = false;
        if (af && !af->empty()) {  // a genuine tuple (named field or >1 entry), NOT a 1-entry scalar
          is_tuple_actual = af->size() > 1;
          for (const auto& [fld, fv] : *af) {
            (void)fv;
            if (fld.find_first_not_of("0123456789") != std::string::npos) {
              is_tuple_actual = true;
              break;
            }
          }
        }
        if (is_tuple_actual) {
          // group unset non-self params by their top-level (pre-dot) prefix
          absl::flat_hash_map<std::string, std::vector<std::size_t>> groups;
          for (std::size_t i = (has_self ? 1u : 0u); i < nbind; ++i) {
            if (param_set[i]) {
              continue;
            }
            const auto& pn = io.inputs[i].name;
            if (auto dp = pn.rfind('.'); dp != std::string::npos) {
              groups[pn.substr(0, dp)].push_back(i);
            }
          }
          bool matched = false;
          for (auto& [prefix, idxs] : groups) {
            if (idxs.size() != af->size()) {
              continue;
            }
            // every actual field name must be a leaf of this param group
            bool cover = true;
            for (const auto& [fld, _] : *af) {
              bool found = false;
              for (std::size_t idx : idxs) {
                if (io.inputs[idx].name.substr(prefix.size() + 1) == fld) {
                  found = true;
                  break;
                }
              }
              if (!found) {
                cover = false;
                break;
              }
            }
            if (cover) {
              for (std::size_t idx : idxs) {
                param_val[idx] = a.node;
                param_set[idx] = true;
              }
              matched = true;
              break;
            }
          }
          if (!matched) {
            return false;  // a tuple actual can't bind a scalar param / no matching group
          }
          continue;
        }
      }
      if (a.node.is_ref()) {  // exception 2: bare var name matches a param
        const auto nidx = param_index(a.node.get_name());
        if (nidx < nparams && nidx >= (has_self ? 1u : 0u) && !param_set[nidx]) {
          bind(nidx);
          continue;
        }
      }
      if (n_named_params == 1) {  // exception 1: single non-self param
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot < nparams) {
          bind(slot);
          continue;
        }
      }
      if (const auto k = classify(a.node); k != Io_kind::none) {  // exception 3: unique kind
        std::size_t match = nparams;
        std::size_t count = 0;
        for (std::size_t i = (has_self ? 1u : 0u); i < nparams; ++i) {
          if (!param_set[i] && io.inputs[i].kind == k) {
            match = i;
            ++count;
          }
        }
        if (count == 1) {
          bind(match);
          continue;
        }
      }
      return false;  // ambiguous / unbindable positional — must be named (same rule)
    }
  }

  // Every non-self FIXED parameter must be bound (io_meta carries no defaults).
  for (std::size_t i = (has_self ? 1 : 0); i < nbind; ++i) {
    if (!param_set[i]) {
      return false;  // missing required argument
    }
  }

  // Per-arg scalar kind/range fit — the typed-param check the runner otherwise
  // defers to downstream typecheck/bitwidth. Re-derived here so dispatch can
  // distinguish e.g. `f(a:bool)` from `f(a:u8)`, or `f(a:u8)` from `f(a:u16)`.
  // Skips self, the var-arg slot, and unset params; an untyped param or a
  // not-comptime-classifiable actual is permissive (the full path / downstream
  // remains authoritative). A typed-self structural `does` is NOT checked here
  // (method overloads fall through to check_self_does on the chosen candidate).
  for (std::size_t i = 0; i < nparams; ++i) {
    if ((has_vararg && i == nbind) || (has_self && i == 0) || !param_set[i]) {
      continue;
    }
    const auto& pe = io.inputs[i];
    const auto  ak = classify(param_val[i]);
    if (pe.kind != Io_kind::none && ak != Io_kind::none && pe.kind != ak) {
      return false;  // kind mismatch (bool vs int vs string)
    }
    if (pe.kind == Io_kind::integer && param_val[i].is_const()
        && (pe.has_range || (pe.bits > 0 && pe.bits < 62))) {
      if (auto v = Dlop::from_pyrope(param_val[i].get_name());
          v && v->is_integer() && !v->has_unknowns() && v->get_bits() <= 62) {
        const int64_t val = v->to_just_i64();
        // Prefer the EXACT declared `int(min,max)` range (so adjacent windows
        // like int(0,99) vs int(100,199) discriminate 100 correctly — a
        // `does`-style range containment); fall back to the bits window.
        const int64_t lo = pe.has_range ? pe.range_min : (pe.is_signed ? -(int64_t{1} << (pe.bits - 1)) : int64_t{0});
        const int64_t hi = pe.has_range ? pe.range_max
                                        : (pe.is_signed ? (int64_t{1} << (pe.bits - 1)) - 1 : (int64_t{1} << pe.bits) - 1);
        if (val < lo || val > hi) {
          return false;  // argument does not fit this overload's declared range
        }
      }
    }
  }

  return true;
}

// ── init constructor hook ─────────────────────────────────────────────────────

std::vector<std::string> uPass_runner::init_candidates_of(std::string_view tn) {
  // The type bundle records `init` either as one function-name string or as
  // an `init.N` overload list (`init = [init_empty, init_v]`). Bundle keys
  // are canonical dotted leaves, so the list arrives as init.0, init.1, …
  std::vector<std::string> out;
  if (tn.empty()) {
    return out;
  }
  auto bf = try_bundle_fields(tn);
  if (!bf) {
    return out;
  }
  auto unquote = [](std::string s) {
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
      return s.substr(1, s.size() - 2);
    }
    return s;
  };
  std::map<int, std::string> ordered;
  for (const auto& [key, val] : *bf) {
    if (!val.is_string()) {
      continue;
    }
    if (key == "init") {
      out.push_back(unquote(val.to_pyrope()));
    } else if (key.size() > 5 && key.compare(0, 5, "init.") == 0) {
      const auto idx = key.substr(5);
      if (!idx.empty() && idx.find_first_not_of("0123456789") == std::string::npos) {
        ordered[std::stoi(idx)] = unquote(val.to_pyrope());
      }
    }
  }
  for (auto& [i, fn] : ordered) {
    out.push_back(std::move(fn));
  }
  return out;
}

std::string uPass_runner::select_init_overload(const std::vector<std::string>& candidates, const std::vector<Ctor_arg>& args) {
  // Tuple-order priority (07b-structtype.md "Lambda overloading"): first
  // candidate whose non-self formals fit the args wins. Delegates to
  // signature_matches (is_ctor_call=true) so per-arg KIND/RANGE fit — not just
  // arity — decides, letting `init(99)` skip a `b:bool` overload instead of
  // binding the first arity-compatible candidate.
  for (const auto& fn : candidates) {
    auto callee = lookup_callee(fn);
    if (!callee) {
      continue;
    }
    const auto& cio = callee->io_meta();
    if (cio.inputs.empty() || cio.inputs[0].name != "self") {
      continue;  // init must be a method (ref self first)
    }
    // Build actuals for the probe: a self placeholder (binds slot 0
    // positionally — signature_matches skips it in the kind check, so any
    // non-invalid node serves) followed by the constructor args in tuple order.
    std::vector<Actual> actuals;
    actuals.reserve(args.size() + 1);
    actuals.push_back(Actual{.is_named = false, .is_ref_pass = false, .key = {}, .node = Lnast_node::create_const("0")});
    for (const auto& a : args) {
      actuals.push_back(Actual{.is_named = !a.key.empty(), .is_ref_pass = false, .key = a.key, .node = a.node});
    }
    if (signature_matches(cio, actuals, /*is_ctor_call=*/true)) {
      return fn;
    }
  }
  return {};
}

void uPass_runner::splice_init_call(const std::string& receiver, const std::string& tn, const std::string& init_fn,
                                    const std::vector<Ctor_arg>& args) {
  ++init_construction_depth_;
  constructing_vars_.insert(receiver);
  dispatch_to_passes(&upass::uPass::notify_init_construction_begin);

  // 1) Bind the receiver to the type's defaults (whole-bundle alias; the
  //    symbol table's COW keeps the type bundle itself immutable). The
  //    mod-init form has no type-side init and may come typename-less.
  if (!tn.empty()) {
    emit_inline_binding(receiver, Lnast_node::create_ref(tn));
  }

  // 2) Synthesize `init_fn(__ufcs_arg=receiver, args…)` on a scratch tree and
  //    run it through the normal walk — try_inline_func_call splices it with
  //    the receiver bound to `ref self` and the write-back restoring the
  //    constructed value into the receiver.
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("init-call");
  auto s    = std::make_shared<Lnast>(body, "init-call");
  auto root = s->set_root(Lnast_ntype::create_func_call());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(std::format("___ctor{}", ++inline_seq_)));
  s->add_child(root, Lnast_node::create_ref(init_fn));
  auto recv = s->add_child(root, Lnast_ntype::create_store());
  s->add_child(recv, Lnast_node::create_ref(call_ufcs_arg_marker));
  s->add_child(recv, Lnast_node::create_ref(receiver));
  for (const auto& a : args) {
    if (a.key.empty()) {
      s->add_child(root, a.node);
    } else {
      auto st = s->add_child(root, Lnast_ntype::create_store());
      s->add_child(st, Lnast_node::create_ref(a.key));
      s->add_child(st, a.node);
    }
  }
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  ctor_call_pending_ = true;  // the next try_inline entry is the ctor call
  process_lnast();
  ctor_call_pending_ = false;
  flush_deferred_emits();
  lm->pop_source();

  dispatch_to_passes(&upass::uPass::notify_init_construction_end);
  constructing_vars_.erase(receiver);
  --init_construction_depth_;
}

bool uPass_runner::try_init_construction() {
  // Cursor on a 2-child `store(x, V)`. Only the DECLARATION store may
  // construct (init runs once); pending_ctor_store_ was armed by the
  // preceding `declare`.
  const auto saved = lm->save_cursor();
  if (!lm->move_to_child() || !Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return false;
  }
  const std::string x(lm->current_text());
  if (!lm->move_to_sibling()) {
    lm->restore_cursor(saved);
    return false;
  }
  const bool        v_is_ref   = Lnast_ntype::is_ref(lm->get_raw_ntype());
  const bool        v_is_const = Lnast_ntype::is_const(lm->get_raw_ntype());
  const std::string v_text(lm->current_text());
  const std::string v_raw(v_is_ref ? std::string(lm->current_raw_text()) : std::string{});
  lm->restore_cursor(saved);

  if (constructing_vars_.contains(x)) {
    return false;  // the synthesized defaults-bind / write-back of this very construction
  }
  if (x.find('.') != std::string::npos || prp_is_tmp_name(x)) {
    return false;
  }
  // An inline anonymous tuple type (`mut x:(field:u32, comb init...) = 0`) lowers
  // to a `store(x, <type-bundle>)` — binding x to the type's default shape, which
  // carries the `init` field — that PRECEDES the real `= value` construction
  // store. This shape-binding store must bind structurally and must NOT consume
  // the pending-ctor flag: the construction runs on the NEXT store, where x now
  // holds the inline type's `init`. (2f-init_dispatch.)
  if (pending_ctor_store_.contains(x) && v_is_ref && !init_candidates_of(v_text).empty()) {
    return false;
  }
  if (!pending_ctor_store_.erase(x)) {
    return false;  // re-assignment, not the declaration initializer
  }
  if (!v_is_ref && !v_is_const) {
    return false;
  }

  const auto tn = try_typename(x);

  // A bare function reference (`mut y:Mix_tup = mix_tup_init` — an identifier
  // that names a registry comb/pipe/mod written WITHOUT a call `(...)`) may
  // only bind to an UNtyped variable (`const f = mix_tup_init`, later
  // `x.f()`). On a TYPED declaration it is a compile error: the function
  // reference does not match the declared type. The old "the named `ref self`
  // mod IS the constructor" sugar is gone — construct with a `T(...)` call or
  // an `init` method inside the type instead.
  if (v_is_ref && !v_raw.empty() && lookup_callee(v_raw)) {
    if (tn.empty()) {
      return false;  // untyped binding — a plain function-value alias, allowed
    }
    livehd::diag::Span span = lm->get_lnast()->span_of(lm->get_current_nid());
    const auto         msg  = std::format("cannot assign function reference `{}` to typed variable `{}`:{}", v_raw, x, tn);
    livehd::diag::sink().emit(
        livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                 .code     = "ctor-func-ref",
                                 .category = "type",
                                 .pass     = "upass.runner",
                                 .message  = msg,
                                 .span     = span,
                                 .hint     = "call it (`x = T(...)`) or give the type an `init` method; a bare function "
                                             "reference may only bind to an untyped variable"});
    throw std::runtime_error(msg);
  }

  auto candidates = init_candidates_of(tn);
  if (candidates.empty() && tn.empty()) {
    // Inline anonymous tuple type (`mut x:(field:u32, comb init...) = 0`): the
    // type bundle — including its `init` field — was already bound to x by the
    // type-spec store, but it carries no `typename` attr, so try_typename is
    // empty. Look for `init` directly on x's own bundle. (2f-init_dispatch.)
    candidates = init_candidates_of(x);
  }
  if (candidates.empty()) {
    return false;
  }

  // Explode V into constructor args. Everything must be comptime-resolvable
  // (construction is a comptime affair in this walk); otherwise fall back to
  // the structural store.
  std::vector<Ctor_arg> args;
  if (v_is_const) {
    if (v_text != "nil") {
      args.push_back(Ctor_arg{.key = {}, .node = Lnast_node::create_const(v_text)});
    }
  } else if (auto fv = try_fold_ref(v_text); fv && !fv->is_invalid()) {
    args.push_back(Ctor_arg{.key = {}, .node = Lnast_node::create_const(fv->to_pyrope())});
  } else if (auto vb = try_bundle_fields(v_text); vb && !vb->empty()) {
    // The construction value is a tuple. An init taking a SINGLE array/tuple
    // param (`init(ref self, data:[4]int)`) must receive the WHOLE tuple, not N
    // exploded positional args. Try the whole-tuple single-arg form first; fall
    // back to the exploded multi-arg form (the `(a="x", b=0)` multi-param ctor)
    // only when no overload accepts the single tuple.
    std::vector<Ctor_arg> whole{Ctor_arg{.key = {}, .node = Lnast_node::create_ref(v_text)}};
    if (const auto chosen_whole = select_init_overload(candidates, whole); !chosen_whole.empty()) {
      splice_init_call(x, tn, chosen_whole, whole);
      return true;
    }
    for (const auto& [key, val] : *vb) {
      if (key.find('.') != std::string::npos || val.is_invalid()) {
        return false;  // nested / non-comptime field — structural fallback
      }
      const bool positional = key.find_first_not_of("0123456789") == std::string::npos;
      args.push_back(Ctor_arg{.key = positional ? std::string{} : key, .node = Lnast_node::create_const(val.to_pyrope())});
    }
  } else {
    return false;  // unresolved value — structural fallback
  }

  const auto chosen = select_init_overload(candidates, args);
  if (chosen.empty()) {
    return false;  // no overload fits (e.g. nil decl without a 0-arg init) — structural
  }

  splice_init_call(x, tn, chosen, args);
  return true;
}

bool uPass_runner::try_construct_call() {
  // Cursor on `func_call(dst, NAME, args…)` that try_inline_func_call
  // declined. When NAME is a type bundle carrying `init`, this is the
  // explicit construction form `x = T(args…)` (07-typesystem.md): bind dst
  // to T's defaults and splice init with dst as the receiver.
  const auto saved = lm->save_cursor();
  if (!lm->move_to_child() || !Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return false;
  }
  const std::string dst(lm->current_text());
  if (!lm->move_to_sibling() || !Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return false;
  }
  const std::string tn(lm->current_raw_text());
  if (lookup_callee(tn)) {
    lm->restore_cursor(saved);
    return false;  // a real function — not a type construction
  }
  const auto candidates = init_candidates_of(tn);
  if (candidates.empty()) {
    lm->restore_cursor(saved);
    return false;
  }
  // Collect args verbatim (refs/consts positional, store(key,val) named).
  std::vector<Ctor_arg> args;
  bool                  shape_ok = true;
  while (lm->move_to_sibling()) {
    const auto t = lm->get_raw_ntype();
    if (Lnast_ntype::is_ref(t) || Lnast_ntype::is_const(t)) {
      args.push_back(Ctor_arg{.key = {}, .node = lm->current_node()});
    } else if (Lnast_ntype::is_store(t)) {
      const auto here = lm->save_cursor();
      if (!lm->move_to_child()) {
        shape_ok = false;
        break;
      }
      std::string key(lm->current_raw_text());
      if (!lm->move_to_sibling()) {
        shape_ok = false;
        lm->restore_cursor(here);
        break;
      }
      args.push_back(Ctor_arg{.key = std::move(key), .node = lm->current_node()});
      lm->restore_cursor(here);
    } else {
      shape_ok = false;
      break;
    }
  }
  lm->restore_cursor(saved);
  if (!shape_ok) {
    return false;
  }
  const auto chosen = select_init_overload(candidates, args);
  if (chosen.empty()) {
    return false;
  }
  splice_init_call(dst, tn, chosen, args);
  return true;
}

// ── Comptime loop unroll (range `for` + `while`/`loop`) ───────────────────────

void uPass_runner::emit_inline_tuple_pick(const std::string& dst, const std::string& src, const std::string& index_text) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-pick");
  auto s    = std::make_shared<Lnast>(body, "inl-pick");
  auto root = s->set_root(Lnast_ntype::create_tuple_get());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));
  s->add_child(root, Lnast_node::create_ref(src));
  s->add_child(root, Lnast_node::create_const(index_text));
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // tuple_get dispatch → try_resolve_tuple_get (runtime) or constprop fold (comptime)
  flush_deferred_emits();
  lm->pop_source();
}

void uPass_runner::emit_inline_tuple_store(const std::string& dst, const std::string& index_text, const std::string& value) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-tset");
  auto s    = std::make_shared<Lnast>(body, "inl-tset");
  auto root = s->set_root(Lnast_ntype::create_store());  // 3-child store == tuple_set
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));
  s->add_child(root, Lnast_node::create_const(index_text));
  s->add_child(root, Lnast_node::create_ref(value));
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // store dispatch → constprop tuple_set / write-back into dst's slot
  flush_deferred_emits();
  lm->pop_source();
}

void uPass_runner::emit_inline_declare_typed(const std::string& name, const std::optional<Dlop>& max,
                                             const std::optional<Dlop>& min) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-decl");
  auto s    = std::make_shared<Lnast>(body, "inl-decl");
  auto root = s->set_root(Lnast_ntype::create_declare());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(name));
  auto pt = s->add_child(root, Lnast_ntype::create_prim_type_int());
  s->add_child(pt, Lnast_node::create_const(max ? std::string(max->to_pyrope()) : std::string("nil")));
  s->add_child(pt, Lnast_node::create_const(min ? std::string(min->to_pyrope()) : std::string("nil")));
  s->add_child(root, Lnast_node::create_const("mut"));
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // declare dispatch → records the iter var's declared type
  flush_deferred_emits();
  lm->pop_source();
}

bool uPass_runner::walk_loop_iteration(const std::function<void()>& emit_binds, const std::function<void()>& emit_post) {
  // Precondition: the read cursor is on the loop body `stmts` node.
  if (inline_budget_ == 0 || loop_depth_ > static_cast<int>(kInlineMaxDepth)) {
    return false;  // fuel / depth guard — a non-terminating comptime loop bails (like recursion)
  }
  --inline_budget_;
  const uint32_t iter_salt = ++inline_seq_;

  flush_deferred_emits();                            // flush outer parked writes before the iteration frame
  lm->push_iteration(iter_salt);                     // fresh salt → fresh tmp namespace + block-scope id; cursor + tag kept
  enter_block_scope();                               // Runner-owned block scope (scope_uid uses iter_salt)
  dispatch_to_passes(&upass::uPass::process_stmts);  // passes seed per-block state
  emit_push(Lnast_ntype::create_stmts());            // staging block for this iteration

  // Bind the iteration variable(s) into this scope so the body's reads fold.
  emit_binds();

  loop_continue_hit_ = false;  // fresh per iteration (a `continue` skips only this one)
  if (lm->move_to_child()) {
    do {
      process_lnast();
      if (loop_break_hit_ || loop_continue_hit_) {
        break;  // a comptime `break`/`continue` fired — skip the rest of this iteration's body
      }
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }

  if (emit_post) {
    emit_post();  // `for i in ref d` write-back: still inside this iteration's scope
  }

  dispatch_to_passes(&upass::uPass::process_stmts_pre_pop);  // coalescer flushes this iteration's writes
  emit_pop();
  dispatch_to_passes(&upass::uPass::process_stmts_post);
  symbol_table_.leave_scope();  // Pop after stmts_post (see process_stmts)
  lm->pop_source();             // restore cursor (body stmts), outer salt/stack/tag
  return true;
}

void uPass_runner::unroll_for() {
  // Cursor on the `for` node. Layout (prp2lnast):
  //   for( value_ref, iterable_ref, stmts(body), const(mode) [, idx_ref [, key_ref]] )
  // iterable_ref is a `range` tmp whose (lo, hi_inclusive) bounds constprop
  // recorded (process_range), a tuple name/tmp resolved by try_tuple_shape, or a
  // var-arg. mode is "ref" (write each value back into the slot after the body)
  // or "val". idx_ref/key_ref are the optional position/key binds of
  // `for (value, idx, key) in t`.
  if (!lm->has_child()) {
    emit_subtree_verbatim();
    return;
  }
  lm->move_to_child();  // child0: value-var ref
  const std::string ivar = std::string(lm->current_text());
  if (!lm->move_to_sibling()) {
    lm->move_to_parent();
    emit_subtree_verbatim();
    return;
  }
  const std::string iterable = std::string(lm->current_text());  // child1: iterable ref
  if (!lm->move_to_sibling()) {                                  // child2: body stmts
    lm->move_to_parent();
    emit_subtree_verbatim();
    return;
  }
  // Trailing metadata: child3 mode ("ref"/"val"), child4 idx, child5 key.
  bool        is_ref = false;
  std::string idx_var;
  std::string key_var;
  if (lm->move_to_sibling()) {  // child3: mode
    is_ref = lm->current_text() == "ref";
    if (lm->move_to_sibling()) {  // child4: idx
      idx_var = std::string(lm->current_text());
      if (lm->move_to_sibling()) {  // child5: key
        key_var = std::string(lm->current_text());
      }
    }
  }
  lm->move_to_parent();  // back to for-node

  // `ivar` was read via current_text(), so it is ALREADY the frame-renamed name
  // the body's reads of the iteration variable resolve to (under the same tag).
  // Bind it directly each iteration — do NOT re-tag (that would double-prefix).
  // idx_var/key_var are likewise already frame-renamed.
  const std::string& tagged_i = ivar;
  const auto         for_bm   = lm->save_cursor();  // on the for-node
  // Re-position the cursor on the body stmts (child2) before each iteration.
  auto               to_body  = [&]() {
    lm->restore_cursor(for_bm);
    lm->move_to_child();    // child0 value
    lm->move_to_sibling();  // child1 iterable
    lm->move_to_sibling();  // child2 body
  };

  // (a) Range iterable: `for i in lo..hi [step n]` — bind a Dlop each iteration.
  if (auto rng = try_range(iterable); rng && std::get<0>(*rng).is_just_i64() && std::get<1>(*rng).is_just_i64()) {
    const int64_t lo          = std::get<0>(*rng).to_just_i64();
    const int64_t hi          = std::get<1>(*rng).to_just_i64();  // inclusive
    int64_t       step        = std::get<2>(*rng).is_just_i64() ? std::get<2>(*rng).to_just_i64() : 1;
    if (step < 1) {
      step = 1;  // non-positive step is a comptime error (process_func_call); avoid a hang here
    }
    const bool saved_break = loop_break_hit_;
    loop_break_hit_        = false;
    ++loop_depth_;
    for (int64_t v = lo; v <= hi; v += step) {
      to_body();
      if (!walk_loop_iteration([&]() { emit_inline_binding(tagged_i, Lnast_node::create_const(std::to_string(v))); })) {
        break;  // fuel exhausted
      }
      if (loop_break_hit_) {
        break;
      }
    }
    --loop_depth_;
    loop_break_hit_    = saved_break;
    loop_continue_hit_ = false;  // a `continue` never escapes the loop
    lm->restore_cursor(for_bm);
    return;
  }

  // (b) Tuple iterable (loop-migration): `for x in t` — unroll over the
  // comptime-known shape, binding the iter var to each entry via a tuple_get
  // pick (try_resolve_tuple_get rewrites a runtime entry to a copy; constprop
  // folds a comptime entry). This is the path that fixes the parse-time-unroll
  // accumulation drop, since walk_loop_iteration handles outer-`mut` writes.
  // The shape comes from constprop's tuple, or — for `for x in args` inside a
  // comb — from the gathered var-arg entries (which aren't a constprop tuple).
  std::optional<std::vector<std::pair<std::string, bool>>> shape = try_tuple_shape(iterable);
  if (!shape) {
    if (auto vit = vararg_bindings_.find(iterable); vit != vararg_bindings_.end()) {
      std::vector<std::pair<std::string, bool>> vshape;
      for (const auto& [k, _node] : vit->second) {
        const bool is_pos = !k.empty() && k.find_first_not_of("0123456789") == std::string::npos;
        vshape.emplace_back(k, is_pos);
      }
      shape = std::move(vshape);
    }
  }
  if (shape) {
    const bool saved_break = loop_break_hit_;
    loop_break_hit_        = false;
    ++loop_depth_;
    int64_t pos = 0;
    for (const auto& [key, is_pos] : *shape) {
      to_body();
      const std::string index_text = is_pos ? key : ("'" + key + "'");
      const std::string key_text   = is_pos ? std::string("''") : ("'" + key + "'");  // positional slots have no name
      const int64_t     cur_pos    = pos;
      // If the element is a typed runtime ref (a var-arg port, or any typed tuple
      // slot), give the iteration variable that declared type — so a nested
      // template specialization called with it (mod_varargs_csa: `for a in args`
      // then `blk_add(a)`) can read its width. Comptime-value slots have no slot
      // ref → no declare → unchanged.
      std::optional<upass::uPass::Decl_scalar_type> elem_dt;
      if (auto eref = try_tuple_slot_ref(iterable, key)) {
        elem_dt = try_decl_type(*eref);
        if (!elem_dt || (!elem_dt->range_max && !elem_dt->range_min)) {
          // A var-arg port / module input is not in decl_type — its width lives
          // in this tree's io_meta (mirrors the caller_input read in
          // maybe_specialize_template_call). Build a (max,min) range from it.
          const auto& cio = lm->get_lnast()->io_meta();
          for (const auto& ie : cio.inputs) {
            if (ie.name != *eref || ie.bits == 0) {
              continue;
            }
            upass::uPass::Decl_scalar_type dt;
            if (ie.is_signed) {
              dt.range_max = *Dlop::get_mask_value(ie.bits - 1);
              dt.range_min = *Dlop::get_neg_mask_value(ie.bits - 1);
            } else {
              dt.range_max = *Dlop::get_mask_value(ie.bits);
              dt.range_min = *Dlop::from_pyrope("0");
            }
            elem_dt = dt;
            break;
          }
        }
      }
      const bool ok = walk_loop_iteration(
          [&]() {
            if (elem_dt && (elem_dt->range_max || elem_dt->range_min)) {
              emit_inline_declare_typed(tagged_i, elem_dt->range_max, elem_dt->range_min);
            }
            emit_inline_tuple_pick(tagged_i, iterable, index_text);  // value = t[index]
            if (!idx_var.empty()) {
              emit_inline_binding(idx_var, Lnast_node::create_const(std::to_string(cur_pos)));  // idx = position
            }
            if (!key_var.empty()) {
              emit_inline_binding(key_var, Lnast_node::create_const(key_text));  // key = slot name string
            }
          },
          // `for i in ref d`: after the body, write the (possibly mutated) value
          // back into d's slot (d is enclosing — relies on the Phase-3 fix to
          // lower a runtime mutation across the iteration block).
          is_ref ? std::function<void()>([&]() { emit_inline_tuple_store(iterable, index_text, tagged_i); })
                 : std::function<void()>{});
      if (!ok) {
        break;
      }
      if (loop_break_hit_) {
        break;
      }
      ++pos;
    }
    --loop_depth_;
    loop_break_hit_    = saved_break;
    loop_continue_hit_ = false;  // a `continue` never escapes the loop
    lm->restore_cursor(for_bm);
    return;
  }

  // Neither a comptime range nor a known tuple: `for` is comptime-only; leave
  // the node verbatim rather than silently dropping the body (typecheck/tolg
  // surface the non-comptime iterable).
  lm->restore_cursor(for_bm);
  emit_subtree_verbatim();
}

void uPass_runner::unroll_while() {
  // Cursor on the `while` node: child0 = condition (ref/const), child1 = body.
  // `loop {}` lowers to `while(const "true")`. Comptime-unroll only the
  // statically-true infinite form (terminated by a comptime `break`); a
  // known-false condition drops the loop; anything else (data-dependent /
  // non-bool) is emitted verbatim so typecheck flags a non-bool condition and
  // codegen keeps a real runtime loop.
  if (!lm->has_child()) {
    emit_subtree_verbatim();
    return;
  }
  const auto while_bm = lm->save_cursor();
  lm->move_to_child();  // condition
  const auto          cond_type = lm->get_raw_ntype();
  const std::string   cond_text = std::string(lm->current_text());
  std::optional<Dlop> cval;
  if (Lnast_ntype::is_ref(cond_type)) {
    cval = try_fold_ref(cond_text);
  }
  const bool have_body = lm->move_to_sibling();  // body stmts
  lm->move_to_parent();                          // back to while

  const bool cond_is_true_literal  = (cond_type == Lnast_ntype::Lnast_ntype_const && cond_text == "true");
  const bool cond_is_false_literal = (cond_type == Lnast_ntype::Lnast_ntype_const && cond_text == "false");
  const bool cond_folds_false
      = cond_is_false_literal || (cval && !cval->is_invalid() && !cval->has_unknowns() && cval->is_known_false());
  // A data-dependent `while cond { … }` is lowered by prp2lnast to
  // `while cond { if cond { … } else { break } }`, so the body always carries
  // a per-iteration termination guard. The outer condition here is just the
  // entry gate: enter the comptime unroll whenever it folds to a known-true
  // value (the literal `true`/`loop {}` form, or a folding ref like `c != 0`
  // with c known at entry). The inner `if cond … else break` then ends the
  // unroll when the condition turns false. A non-folding (genuinely runtime)
  // condition stays verbatim — typecheck rejects it (loops must be comptime).
  const bool cond_folds_true
      = cond_is_true_literal || (cval && !cval->is_invalid() && !cval->has_unknowns() && cval->is_known_true());

  if (have_body && cond_folds_false) {
    return;  // `while false` — loop never runs; emit nothing
  }
  if (!have_body || !cond_folds_true) {
    emit_subtree_verbatim();  // runtime / non-comptime loop — leave to typecheck/codegen
    return;
  }

  // Progress guard. A Pyrope loop is comptime-only and must make progress toward
  // its exit; otherwise the unroll runs to the fuel cap (spamming output) and
  // then silently bails. Snapshot the loop's variable state at the start of each
  // iteration: if a state recurs, the loop is deterministic and would reproduce
  // that iteration forever (e.g. `while c != 0 { cputs(c) }` with no update to
  // `c`) — report it. This is the loop analogue of recursion's progress: "if the
  // inputs are already seen, it can never converge".
  absl::flat_hash_set<std::string> cond_var_set;
  collect_cond_vars(*lm->get_lnast(), lm->get_current_nid(), cond_text, Lnast_ntype::is_ref(cond_type), cond_var_set);
  // Broaden the state signature with the body's loop-carried (written) vars, so a
  // late-flipping exit flag is not mistaken for "no progress" (BUG C): a real
  // progress var advancing keeps the signature changing until the loop exits.
  {
    const auto& ln_  = *lm->get_lnast();
    const auto  cnid = ln_.get_first_child(lm->get_current_nid());           // condition
    const auto  bnid = cnid.is_invalid() ? cnid : ln_.get_sibling_next(cnid);  // body stmts
    collect_body_assigned_vars(ln_, bnid, cond_var_set);
  }
  const std::vector<std::string> loop_vars(cond_var_set.begin(), cond_var_set.end());
  absl::flat_hash_set<std::string> seen_states;

  // Span for any loop diagnostic below — the `while` node carries the SourceId.
  auto while_span = [&]() { return lm->get_lnast()->span_of(lm->get_current_nid()); };

  const bool saved_break = loop_break_hit_;
  loop_break_hit_        = false;
  ++loop_depth_;
  // Per-loop unroll cap. State-repeat (below) catches a frozen/cyclic condition
  // in O(1) iterations, but a DIVERGENT loop whose condition variable keeps
  // changing yet never reaches the exit (`while c != 10 { c -= 1 }` from below)
  // never repeats a state. This bounds such loops to a prompt error rather than
  // grinding to the shared fuel cap. 100k body-copies is already absurd for a
  // comptime unroll (it lowers to that many hardware copies), so no real loop
  // hits it.
  constexpr std::size_t kMaxLoopUnroll = 100000;
  std::size_t           loop_iters     = 0;
  while (true) {
    lm->restore_cursor(while_bm);  // cursor on the while node — the scope where the condition folds

    if (++loop_iters > kMaxLoopUnroll) {
      --loop_depth_;
      loop_break_hit_ = saved_break;
      loop_fail(while_span(),
                "loop-unbounded",
                "comptime loop did not terminate within the unroll limit (it may never converge)",
                "ensure the loop is comptime-bounded and its exit condition is eventually reached");
    }

    // State-repeat check: fold the condition's input variables at the iteration
    // boundary. Only conclude when they ALL fold to known constants (an unknown
    // means we can't prove non-progress → fall back to the fuel cap). When the
    // condition has no tracked vars (`loop {}` / `while true`), skip this and
    // rely on the body's `break` (and the fuel cap) instead.
    if (!loop_vars.empty()) {
      std::string sig;
      bool        all_known = true;
      for (const auto& v : loop_vars) {
        auto cv = try_fold_ref(v);
        if (!cv || cv->is_invalid() || cv->has_unknowns()) {
          all_known = false;
          break;
        }
        sig += v;
        sig.push_back('=');
        sig += cv->to_pyrope();
        sig.push_back(';');
      }
      if (all_known && !seen_states.insert(sig).second) {
        --loop_depth_;
        loop_break_hit_ = saved_break;
        lm->restore_cursor(while_bm);  // try_fold_ref above may have moved the cursor; reset for the span
        loop_fail(while_span(),
                  "loop-no-progress",
                  "comptime loop cannot terminate: its condition variables repeat with no progress toward the exit",
                  "update a condition variable each iteration so the loop can exit (e.g. add `c -= 1`)");
      }
    }

    lm->restore_cursor(while_bm);  // try_fold_ref above can move the cursor; reset before the body walk
    lm->move_to_child();           // condition
    lm->move_to_sibling();         // body stmts
    if (!walk_loop_iteration([]() {})) {
      // Fuel/depth cap hit without the state ever repeating — a diverging loop
      // (e.g. an unbounded counter that never satisfies the exit). Pyrope loops
      // must be comptime-bounded, so this is a build error rather than a silent
      // partial unroll.
      --loop_depth_;
      loop_break_hit_ = saved_break;
      lm->restore_cursor(while_bm);
      loop_fail(while_span(),
                "loop-unbounded",
                "comptime loop did not terminate within the unroll budget (it may never converge)",
                "ensure the loop is comptime-bounded and its exit condition is eventually reached");
    }
    if (loop_break_hit_) {
      break;
    }
  }
  --loop_depth_;
  loop_break_hit_    = saved_break;
  loop_continue_hit_ = false;  // a `continue` never escapes the loop
  lm->restore_cursor(while_bm);  // leave cursor on the while-node
}

// ── Top-level run loop ────────────────────────────────────────────────────────

void uPass_runner::run() {
  if (configuration_error) {
    std::print("uPass - invalid configuration: {}\n", configuration_error_msg);
    return;
  }

  if (upasses.empty()) {
    std::print("uPass - no passes configured\n");
    return;
  }
  symbol_table_.pending_decl_facts.clear();  // dotted-bake stash is per-run state
  symbol_table_.tget_origin.clear();
  symbol_table_.nil_seeded.clear();

  // Step H — allocate the dest (staging) body in a runner-owned Forest
  // (conceptually the "lgdb/optimized" forest the plan describes; today
  // it's in-memory only, no on-disk lgdb path resolution yet). pass_upass
  // still picks the tree out via take_staging() and replace_body()s the
  // input Lnast so any holder of the input picks up the new body.
  //
  // With single-walk dispatch (Step M) the lambda only runs once.
  if (!dest_forest_) {
    dest_forest_ = hhds::Forest::create();
  }
  auto fresh_staging = [&]() {
    auto body            = dest_forest_->create_tree_temp(std::format("optimized-{}", lm->get_top_module_name()));
    staging              = std::make_shared<Lnast>(body, lm->get_top_module_name());
    staging_parent       = staging->set_root(Lnast_ntype::create_top());
    staging_parent_stack = {};
    // Module anchor: replace_body swaps the WHOLE tree — re-stamp the
    // unit-declaration id (func_extract put it on the source root) onto the
    // fresh root or it dies with the old tree.
    if (root_lnast_) {
      if (const auto id = root_lnast_->get_srcid(root_lnast_->get_root()); id != hhds::SourceId_invalid) {
        staging->set_srcid(staging->get_root(), id);
      }
    }
  };
  fresh_staging();

  // The runner owns the symbol-table lifecycle: the per-tree function
  // scope is pushed here (it used to be constprop's constructor). run() is
  // called once per runner, but guard anyway so a re-run can't double-push.
  if (symbol_table_.stack.empty()) {
    symbol_table_.function_scope(lm->get_top_module_name());
  }

  // Single walk per invocation. begin_iteration() is the per-run setup hook
  // (e.g. bitwidth seeds its range map); there is no iteration loop.
  for (auto& entry : upasses) {
    entry.pass->begin_iteration();
  }
  process_lnast();
  // Print the completion marker the dependency / shared-pass tests
  // (upass_noop_first_iter_test.sh, upass_lnast_shared_scan_test.sh,
  // upass_lnast_shared_decide_test.sh) grep for.
  std::print("uPass - walk complete\n");

  // Post-walk DCE on staging — removes definition statements (assign,
  // tuple_add, attr_set, etc.) whose dst has no surviving downstream
  // reader. Constprop is conservative about multi-entry tuple bundles
  // (their dst can't fold via fold_ref's single-value return), so it
  // emits orphan tuple_add+assign+attr_set chains for fully-constant
  // tuples even when every consumer was already folded away. The DCE
  // cleans those up. Skipped (with the whole staging build) when nothing
  // consumes the rewritten LNAST — see set_materialize().
  if (materialize_) {
    dead_code_eliminate_staging();
  }

  // Step J — dest-walk finisher dispatch. Passes that want to inspect
  // the freshly-built staging tree (verifier/assert cassert counts in
  // the redesign) override walk_dest. Default no-op; today no pass
  // uses this, but the hook is available for migration.
  if (staging) {
    for (auto& entry : upasses) {
      entry.pass->walk_dest(staging);
    }
  }

  // Per-pass finalization. Runs after the walk finishes — passes use
  // this to emit summaries or enforce end-of-run invariants (see
  // uPass_verifier::end_run, which compares cassert tallies against
  // expected counts).
  for (auto& entry : upasses) {
    entry.pass->end_run();
    auto produced = entry.pass->take_new_lnasts();
    new_lnasts.insert(new_lnasts.end(), produced.begin(), produced.end());
  }
}

// ── Post-walk DCE ────────────────────────────────────────────────────────────

namespace {

// True iff `t` is a definition-producing op whose first child is a ref
// to the value being defined. These are the ops eligible for DCE when
// their dst is unused. attr_set is included because it "defines" the
// attribute side-channel on its first-child target; an attr_set on a
// dead name is itself dead.
bool dce_is_def_producing(Lnast_ntype::Lnast_ntype_int t) {
  using N = Lnast_ntype;
  switch (t) {
    case N::Lnast_ntype_store:  // store defines its first-child target (the unified write node)
    case N::Lnast_ntype_dp_assign:
    case N::Lnast_ntype_range:  // a comptime range whose for-loop unrolled away is dead scaffolding
    case N::Lnast_ntype_tuple_add:
    case N::Lnast_ntype_tuple_concat:
    case N::Lnast_ntype_tuple_get:
    case N::Lnast_ntype_plus:
    case N::Lnast_ntype_minus:
    case N::Lnast_ntype_mult:
    case N::Lnast_ntype_div:
    case N::Lnast_ntype_mod:
    case N::Lnast_ntype_shl:
    case N::Lnast_ntype_sra:
    case N::Lnast_ntype_sext:
    case N::Lnast_ntype_set_mask:
    case N::Lnast_ntype_get_mask:
    case N::Lnast_ntype_bit_and:
    case N::Lnast_ntype_bit_or:
    case N::Lnast_ntype_bit_xor:
    case N::Lnast_ntype_bit_not:
    case N::Lnast_ntype_red_and:
    case N::Lnast_ntype_red_or:
    case N::Lnast_ntype_red_xor:
    case N::Lnast_ntype_popcount:
    case N::Lnast_ntype_log_and:
    case N::Lnast_ntype_log_or:
    case N::Lnast_ntype_log_not:
    case N::Lnast_ntype_eq:
    case N::Lnast_ntype_ne:
    case N::Lnast_ntype_lt:
    case N::Lnast_ntype_le:
    case N::Lnast_ntype_gt:
    case N::Lnast_ntype_ge:
    case N::Lnast_ntype_is:
    case N::Lnast_ntype_func_call:
    case N::Lnast_ntype_func_does:
    case N::Lnast_ntype_func_equals:
    case N::Lnast_ntype_func_in:
    case N::Lnast_ntype_func_has:
    case N::Lnast_ntype_func_case:
    case N::Lnast_ntype_attr_set:
    case N::Lnast_ntype_attr_get    : return true;
    default                         : return false;
  }
}

// `attr_set TARGET 'type' 'mut'|'reg'` records a user-visible storage
// class declaration; even when the name has no surviving readers, we
// keep the declaration so downstream consumers (lnastfmt, bitwidth)
// still see it.
bool dce_is_keepalive_attr_set(const Lnast& staging, const Lnast_nid& node) {
  if (staging.get_type(node) != Lnast_ntype::Lnast_ntype_attr_set) {
    return false;
  }
  auto tgt = staging.get_first_child(node);
  if (!tgt.is_valid()) {
    return false;
  }
  auto key = staging.get_sibling_next(tgt);
  if (!key.is_valid() || staging.get_type(key) != Lnast_ntype::Lnast_ntype_const) {
    return false;
  }
  if (staging.get_name(key) != "type") {
    return false;
  }
  auto val = staging.get_sibling_next(key);
  if (!val.is_valid() || staging.get_type(val) != Lnast_ntype::Lnast_ntype_const) {
    return false;
  }
  auto v = staging.get_name(val);
  return v == "mut" || v == "reg";
}

}  // namespace

void uPass_runner::dead_code_eliminate_staging() {
  if (!staging) {
    return;
  }
  // Only run DCE when constprop is active. DCE relies on its fold to
  // settle if-bodies and tuple_get expansions; without it the runner's
  // branch-elimination hasn't pruned dead arms and the use-count would
  // be wildly off.
  bool has_constprop = false;
  for (auto& e : upasses) {
    if (e.name == "constprop") {
      has_constprop = true;
      break;
    }
  }
  if (!has_constprop) {
    return;
  }
  // Skip ONLY comb function bodies extracted for inlining: the inliner
  // virtual-splices them from the (pre-rewrite) registry copy and the
  // standalone tree is dropped, so sweeping it is wasted and could perturb
  // the copy a later caller inlines. mod/pipe/fluid units — concrete defs
  // AND specialized clones — ARE the final output, so they must be
  // swept (hybrid): without this they kept dead post-unroll scaffolding
  // (the var-arg reconstruction tuple_add, a folded `.[bits]` attr_get)
  // that tolg then warned on. Templates never materialize (guarded by
  // materialize_ at the call site), so they don't reach here. The io-root
  // named-var rule below relies on io_meta being populated (SSA ran).
  if (is_function_body_ && lm->get_lnast()->get_lambda_kind() == "comb") {
    return;
  }
  using N = Lnast_ntype;

  // Root set for user-named (non-temp) defs. The model: variables cannot
  // declare outputs, so function IO is the authoritative named-var root set;
  // state elements (mut/reg declarations) are the other roots. Any OTHER
  // user var whose in-tree readers all vanished is dead — e.g. the var-arg
  // reconstruction `args = (args__0, …)` once the for-loop unrolled into
  // direct port reads. (Outputs are written but never read in-tree, so
  // without the io_meta root they'd be mis-swept — this is why the rule is
  // root-based, not "keep all named vars".)
  //
  // GUARD: dropping a named var is only sound when the complete IO set is
  // KNOWN, i.e. SSA populated io_meta. That happens for func_extract'd
  // bodies (mod/pipe/fluid units + the specialized clones), not for the
  // file-level shell tree or a synthetic unit test that drives the runner
  // without SSA. When the IO set is unknown, fall back to the conservative
  // "keep every named var" behaviour (only temps are swept) so an output
  // we can't see is never deleted.
  const auto& io               = lm->get_lnast()->io_meta();
  const bool  io_known         = !io.inputs.empty() || !io.outputs.empty();
  const bool  allow_named_drop = is_function_body_ && io_known;

  absl::flat_hash_set<std::string> protected_names;
  {
    for (const auto& e : io.inputs) {
      protected_names.insert(e.name);
    }
    for (const auto& e : io.outputs) {
      protected_names.insert(e.name);
    }
    for (auto node : staging->depth_preorder(staging->get_root())) {
      if (node.is_invalid() || staging->get_type(node) != N::Lnast_ntype_declare) {
        continue;
      }
      auto nm = staging->get_first_child(node);  // child0: declared var
      if (!nm.is_valid() || staging->get_type(nm) != N::Lnast_ntype_ref) {
        continue;
      }
      auto ty = staging->get_sibling_next(nm);  // child1: type subtree
      if (!ty.is_valid()) {
        continue;
      }
      auto mode = staging->get_sibling_next(ty);  // child2: storage-class const
      if (!mode.is_valid() || staging->get_type(mode) != N::Lnast_ntype_const) {
        continue;
      }
      const auto m = staging->get_name(mode);
      if (m == "mut" || m == "reg") {
        protected_names.insert(std::string(staging->get_name(nm)));
      }
    }
  }

  // Compute the live-statement set via iterative liveness on the
  // staging tree, then rebuild a fresh tree containing only the live
  // statements. Avoiding in-place delete_subtree dodges HHDS's
  // pre-order iterator transiently yielding default-constructed
  // Node_class instances for deleted slots — downstream lnastfmt walks
  // would crash on the unchecked `get_type` that follows.

  // Set of statement nids in the staging tree that should be dropped.
  absl::flat_hash_set<int64_t> dead_stmts;

  bool changed = true;
  while (changed) {
    changed = false;

    // Pass 1: count uses (refs in non-LHS positions) for each name. Refs
    // that live inside an already-dead statement don't count.
    absl::flat_hash_map<std::string, int> use_count;
    for (auto node : staging->depth_preorder(staging->get_root())) {
      if (node.is_invalid()) {
        continue;
      }
      if (staging->get_type(node) != N::Lnast_ntype_ref) {
        continue;
      }
      auto parent = staging->get_parent(node);
      if (!parent.is_valid()) {
        continue;
      }
      bool inside_dead = false;
      for (auto a = parent; a.is_valid(); a = staging->get_parent(a)) {
        if (dead_stmts.contains(a.get_class_index().value)) {
          inside_dead = true;
          break;
        }
      }
      if (inside_dead) {
        continue;
      }
      const bool is_lhs = dce_is_def_producing(staging->get_type(parent)) && node.is_first_child();
      if (!is_lhs) {
        ++use_count[std::string(staging->get_name(node))];
      }
    }

    // Pass 2: extend the dead set with statement-level defs whose dst
    // is unused. A node is statement-level only when its direct parent
    // is a `stmts` block — nested `assign` nodes living inside a
    // tuple_add (field-label payload, not a real statement) must be
    // skipped. attr_set with `type`=mut|reg is a keepalive marker
    // (storage class declarations survive even when the name has no
    // surviving readers). Empty stmts subtrees are also dropped
    // (constprop's dead-branch elimination collapses if/else with
    // known conds to an empty stmts node so block scope survives;
    // once nothing in there had effects, the wrapper itself is noise).
    for (auto node : staging->depth_preorder(staging->get_root())) {
      if (node.is_invalid()) {
        continue;
      }
      const auto key = node.get_class_index().value;
      if (dead_stmts.contains(key)) {
        continue;
      }
      auto parent = staging->get_parent(node);
      if (!parent.is_valid()) {
        continue;
      }
      const bool parent_is_stmts = staging->get_type(parent) == N::Lnast_ntype_stmts;
      auto       t               = staging->get_type(node);

      // Empty stmts subtree (other than the body shell directly under top).
      if (t == N::Lnast_ntype_stmts) {
        bool has_live_child = false;
        for (auto c = node.first_child(); c.is_valid(); c = c.next_sibling()) {
          if (!dead_stmts.contains(c.get_class_index().value)) {
            has_live_child = true;
            break;
          }
        }
        if (!has_live_child && staging->get_type(parent) != N::Lnast_ntype_top) {
          dead_stmts.insert(key);
          changed = true;
        }
        continue;
      }

      if (!parent_is_stmts) {
        continue;  // payload node inside an op (e.g. nested assign in tuple_add)
      }
      if (!dce_is_def_producing(t)) {
        continue;
      }
      if (dce_is_keepalive_attr_set(*staging, node)) {
        continue;
      }
      auto fc = staging->get_first_child(node);
      if (!fc.is_valid() || staging->get_type(fc) != N::Lnast_ntype_ref) {
        continue;
      }
      // Temporary defs (`___N`) are always droppable when unread. A
      // user-named def is droppable only when it is NOT a root: not function
      // IO (io_meta) and not a mut/reg state element. This is the io-root
      // rule — `protected_names` holds exactly those roots. Outputs are the
      // motivating case for the io_meta half (written, never read in-tree, so
      // they'd be mis-swept without the root); the var-arg reconstruction
      // `args = (args__0, …)` is the motivating case for the drop (non-IO,
      // non-state, zero readers once the for-loop unrolled into port reads).
      const auto fc_name = staging->get_name(fc);
      if (!Lnast::is_tmp(fc_name) && (!allow_named_drop || protected_names.contains(std::string(fc_name)))) {
        continue;
      }
      auto it = use_count.find(std::string(fc_name));
      if (it == use_count.end() || it->second == 0) {
        dead_stmts.insert(key);
        changed = true;
      }
    }
  }

  if (dead_stmts.empty()) {
    return;
  }

  // Rebuild staging into a fresh forest body, copying every live node.
  // Once we descend into a non-structural op (anything that isn't
  // top/stmts/if/while/for), every descendant is statement payload and
  // is copied unconditionally — only structural-level statements get
  // dead-checked.
  auto fresh_body  = dest_forest_->create_tree_temp(std::format("optimized-{}", lm->get_top_module_name()));
  auto new_staging = std::make_shared<Lnast>(fresh_body, lm->get_top_module_name());

  auto src_root = staging->get_root();
  auto dst_root = new_staging->set_root(staging->get_type(src_root));
  // Module anchor survives the DCE re-copy too.
  if (const auto id = staging->get_srcid(src_root); id != hhds::SourceId_invalid) {
    new_staging->set_srcid(dst_root, id);
  }

  auto is_structural = [](Lnast_ntype::Lnast_ntype_int t) {
    return t == N::Lnast_ntype_top || t == N::Lnast_ntype_stmts || t == N::Lnast_ntype_if || t == N::Lnast_ntype_while
           || t == N::Lnast_ntype_for;
  };

  std::function<void(const Lnast_nid&, const Lnast_nid&, bool)> copy_subtree;
  copy_subtree = [&](const Lnast_nid& src, const Lnast_nid& dst, bool inside_payload) {
    auto fc = src.first_child();
    while (fc.is_valid()) {
      const bool is_dead_stmt = !inside_payload && dead_stmts.contains(fc.get_class_index().value);
      if (!is_dead_stmt) {
        auto      t = staging->get_type(fc);
        Lnast_nid new_child;
        if (t == N::Lnast_ntype_ref) {
          new_child = new_staging->add_child(dst, Lnast_node::create_ref(staging->get_name(fc)));
        } else if (t == N::Lnast_ntype_const) {
          new_child = new_staging->add_child(dst, Lnast_node::create_const(staging->get_name(fc)));
        } else {
          new_child = new_staging->add_child(dst, t);
          // Carry across the DCE sweep (same-locator: both bodies end
          // up owned by root_lnast_, so the integer copies verbatim).
          if (Lnast::srcid_carries(t)) {
            new_staging->set_srcid(new_child, staging->get_srcid(fc));
          }
          copy_subtree(fc, new_child, inside_payload || !is_structural(t));
        }
      }
      fc = fc.next_sibling();
    }
  };
  copy_subtree(src_root, dst_root, false);

  staging = new_staging;
}

// ── Node dispatch ─────────────────────────────────────────────────────────────

void uPass_runner::process_lnast() {
  using Ntype = Lnast_ntype;

  // clang-format off
  // Category A: drop-candidate op-nodes. First child is the LHS/dst (not
  // folded); subsequent ref children are fed to fold_ref.
// Value-producing ops go through the PUSH path: the runner resolves
// (dst, src) bundles and calls the push-form hook. Some passes keep
// same-named cursor methods, so the member pointer needs an explicit cast.
#define PUSH_FN(NAME) static_cast<upass::Push_method>(&upass::uPass::process_##NAME)
#define A_OP(NAME)                                                                              \
  case Ntype::Lnast_ntype_##NAME:                                                               \
    process_drop_candidate_push(PUSH_FN(NAME), /*fold_all=*/false);                             \
    break;

  // Category C: emit verbatim; still dispatch so passes can observe.
#define C_OP(NAME)                                                                              \
  case Ntype::Lnast_ntype_##NAME: process_verbatim(&upass::uPass::process_##NAME); break;

  switch (get_raw_ntype()) {
    // Structural — push the node into staging and recurse.
    case Ntype::Lnast_ntype_top:   process_top(); break;
    case Ntype::Lnast_ntype_stmts: process_stmts(); break;
    case Ntype::Lnast_ntype_if:    process_if(); break;
    // unique_if shares the if walk/fold; emit_push(lm->current_type()) in
    // process_if re-emits the unique_if type so the marker survives staging.
    case Ntype::Lnast_ntype_unique_if: process_if(); break;

    // Statement-scope leaves (e.g. an if condition's ref/const). Fold refs
    // through the symbol table so dropping the producing assign doesn't
    // leave a dangling name.
    case Ntype::Lnast_ntype_ref: emit_ref_or_folded(lm->current_text()); break;
    case Ntype::Lnast_ntype_const:
      emit_leaf(lm->current_node());
      break;

    // Assignment — `store` is the one write/bind node (`assign` was
    // deleted). Branch on arity: a 2-child store is a scalar/wire
    // write (route to process_assign, drop-candidate); a ≥3-child store is a
    // tuple-field write (route to process_tuple_set, verbatim — the bundle
    // mutation is the point, never drop). The pass methods walk children by
    // position, not by node type, so they handle a `store` node unchanged.
    // Both arities dispatch the push-form process_store (each pass routes
    // by src arity to its scalar-assign / tuple-set body). EMIT semantics
    // stay per-arity: the
    // scalar store is a drop candidate; the field-path store is verbatim
    // (the bundle mutation is the point — never dropped, classify not
    // consulted, matching the old process_verbatim path).
    case Ntype::Lnast_ntype_store:
      if (lm->current_num_children() <= 2) {
        // Direct self-store `store(c, c)` (`c = c`) is a user-facing
        // diagnostic: deliberately the EXACT lowered form only (no
        // path-equivalence analysis — `c.a = c.a` is out of scope). SSA
        // rewrites straight-line self-stores into copies, so this survives
        // only where the user wrote it in a branch body / on a reg name.
        if (lm->has_child()) {
          const auto here = lm->save_cursor();
          lm->move_to_child();
          std::string self_dst;
          if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
            self_dst = lm->current_text();
          }
          bool        is_self = false;
          std::string rhs_name;
          bool        rhs_is_ref = false;
          if (!self_dst.empty() && lm->move_to_sibling() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
            rhs_name   = lm->current_text();
            rhs_is_ref = true;
            is_self    = rhs_name == self_dst;
          }
          lm->restore_cursor(here);
          // Multiple outputs bound WHOLE to one user variable must be
          // destructured. The result tmp of a >1-output call is recorded by the
          // epilogue; a destructure picks fields (tuple_get) and never reaches
          // here, so only the single-variable bind trips this.
          if (rhs_is_ref && !Lnast::is_tmp(self_dst) && multi_output_results_.contains(rhs_name)) {
            livehd::diag::sink().emit(livehd::diag::Diagnostic{
                .severity = livehd::diag::Severity::error,
                .code     = "multi-output-one-var",
                .category = "type",
                .pass     = "upass.runner",
                .message  = std::format("a call returning multiple outputs cannot bind to the single variable `{}`", self_dst),
                .span     = lm->get_lnast()->span_of(lm->get_current_nid()),
                .hint     = "destructure into the output names, e.g. `const (o1, o2) = f(...)` (or remap `(x=f.o1, …)`)",
            });
          }
          if (is_self) {
            livehd::diag::sink().emit(livehd::diag::Diagnostic{
                .severity = livehd::diag::Severity::error,
                .code     = "irrelevant-assignment",
                .category = "syntax",
                .pass     = "upass",
                .message  = std::format("irrelevant assignment: `{}` is assigned to itself", self_dst),
                .hint     = "likely an error or delete this assignment",
            });
          }
        }
        // init constructor hook: the DECLARATION store of a typed var whose
        // type carries `init` (or whose value is a ref-self mod/comb) becomes
        // a defaults-bind + spliced constructor call instead of a structural
        // assign. Re-assignment stores never construct (init runs once).
        if (!try_init_construction()) {
          process_drop_candidate_push(&upass::uPass::process_store, /*fold_all=*/false);
        }
      } else {
        Resolved_node rn;
        if (!resolve_node_operands(rn)) {
          rn.dst = std::make_shared<Bundle>("");
        }
        (void)dispatch_push(&upass::uPass::process_store, rn);  // votes ignored: verbatim
        emit_op_with_fold(/*fold_all=*/false);
      }
      break;

    // declare carries type/mode metadata downstream passes still
    // need (like type_spec/attr_set); emit verbatim, never drop. child0 is the
    // declared var (LHS, not folded); child1 the type subtree; child2 the mode
    // const; optional child3 an init value (folded if a ref).
    case Ntype::Lnast_ntype_declare:
      // Remember that the NEXT store to this var is its declaration
      // initializer — the only store where the init constructor may run.
      if (lm->has_child()) {
        const auto here = lm->save_cursor();
        lm->move_to_child();
        if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
          pending_ctor_store_.insert(std::string(lm->current_text()));
        }
        lm->restore_cursor(here);
      }
      bake_decl_pre_step(/*is_declare=*/true);  // Bake type/mode into the bundle first
      process_verbatim(&upass::uPass::process_declare);
      break;

    // Bitwidth
    A_OP(bit_and)
    A_OP(bit_or)
    A_OP(bit_not)
    A_OP(bit_xor)

    // Bitwidth Insensitive Reduce
    A_OP(red_or)
    A_OP(red_and)
    A_OP(red_xor)
    A_OP(popcount)

    // Logical
    A_OP(log_and)
    A_OP(log_or)
    A_OP(log_not)

    // Arithmetic
    A_OP(plus)
    A_OP(minus)
    A_OP(mult)
    A_OP(div)
    A_OP(mod)

    // Shift
    A_OP(shl)
    A_OP(sra)

    // Bit Manipulation
    A_OP(sext)
    A_OP(set_mask)
    A_OP(get_mask)

    // Comparison
    A_OP(ne)
    A_OP(eq)
    A_OP(lt)
    A_OP(le)
    A_OP(gt)
    A_OP(ge)
    A_OP(is)

    // Function Call — 1i: if the callee is a comb body in the registry and
    // the call shape is supported, virtually splice it (prologue / push_source
    // body walk / epilogue). Otherwise fall back to the drop-candidate path so
    // constprop can fold built-in typecasts (int/uint/uNN/sNN) and cell-ops;
    // anything it declines stays un-folded and the statement is emitted.
    case Ntype::Lnast_ntype_func_call:
      if (!try_inline_func_call() && !try_construct_call()) {
        process_drop_candidate(&upass::uPass::process_func_call, /*fold_all=*/false);
      }
      break;
    // does/in/has/case fold to a known boolean (or nil) → drop-candidate.
    A_OP(func_does)
    A_OP(func_equals)
    A_OP(func_in)
    A_OP(func_has)
    A_OP(func_case)
    // break/continue/return. During a comptime loop unroll a `func_break`
    // reached on the taken path (process_if pruned to it) terminates the loop:
    // flag it and consume the node (don't emit) so no stray break lands in
    // staging. Outside a loop, emit verbatim as before.
    case Ntype::Lnast_ntype_func_break:
      dispatch_to_passes(&upass::uPass::process_func_break);
      if (loop_depth_ > 0) {
        loop_break_hit_ = true;  // checked by unroll_for/unroll_while after the iteration
      } else {
        emit_subtree_verbatim();
      }
      break;
    // `continue`: during a loop unroll, stop the rest of THIS iteration's body
    // (loop_continue_hit_, checked by the body-walk loops) but let the unroller
    // proceed to the next iteration. Outside a loop, emit verbatim.
    case Ntype::Lnast_ntype_func_continue:
      dispatch_to_passes(&upass::uPass::process_func_continue);
      if (loop_depth_ > 0) {
        loop_continue_hit_ = true;
      } else {
        emit_subtree_verbatim();
      }
      break;
    C_OP(func_return)
    case Ntype::Lnast_ntype_func_def:
      process_drop_candidate_verbatim(&upass::uPass::process_func_def);
      break;
    C_OP(io)

    // Tuple Operations — Slice 1 pass-through (Slice 6 flattens).
    //
    // tuple_add / tuple_get produce a fresh tmp bundle (or a folded scalar
    // extracted from a bundle); their dst is a tmp that's purely scaffolding.
    // Once the destination is resolved (is_known_const true on its first
    // entry), dropping the stmt is safe — consumers either fold the value
    // via fold_ref or read the bundle directly from the symbol table.
    // Without this, for-loop unrolls leave behind orphan
    // `tuple_add ___N = (i,)` stmts whose tuple_concat / assign-to-c
    // consumers got dropped, producing dead code with dangling tmp refs.
    // A `tuple_get` on a registered var-arg with a comptime-known
    // index/name is rewritten to a direct copy (so a runtime var-arg pick
    // lowers); anything else folds/emits normally.
    case Ntype::Lnast_ntype_tuple_get:
      if (!try_resolve_tuple_get()) {
        process_drop_candidate(&upass::uPass::process_tuple_get, /*fold_all=*/false);
      }
      break;
    A_OP(tuple_add)
    // the tuple_set node was deleted; field writes are now `store`
    // (≥3 children → process_tuple_set, handled in the store case above).
    // tuple_concat folds when every operand is a known scalar (string/int
    // concat via Lconst::concat_op); treat like arithmetic so classify can
    // drop the statement once the destination is resolved.
    A_OP(tuple_concat)
    // Range nodes carry start/end for slicing (`x[a..=b]` / `x[a..]`). They
    // must dispatch so constprop can stash bounds before the consuming
    // tuple_get folds. C_OP keeps the node visible for downstream passes.
    C_OP(range)

    // Attribute Statements — Slice 1 pass-through (Slice 5 lifts to side-map).
    C_OP(attr_set)
    C_OP(attr_get)

    // Type metadata — emit verbatim, but dispatch so the attribute pass
    // observes type_spec for max/min/bits derivation. (type_def was deleted —
    // `type Foo = …` now lowers to `declare(mode=="type")`, handled above.)
    // A standalone type_spec(tmp, TYPE) is a producer for tmp's
    // bundle: bake the type facts before the pass dispatch.
    case Ntype::Lnast_ntype_type_spec:
      bake_decl_pre_step(/*is_declare=*/false);
      process_verbatim(&upass::uPass::process_type_spec);
      break;

    // Cassert — emit with all operand refs folded (Slice 2 gives this to
    // verifier so known-true cassert gets dropped).
    case Ntype::Lnast_ntype_cassert:
      process_drop_candidate(&upass::uPass::process_cassert, /*fold_all=*/true);
      break;

    // Delay-assign carries timing; emit verbatim (see upass.md invariant 6).
    C_OP(delay_assign)

    // For — comptime range-loop unroll (tuple-iteration for-loops are still
    // unrolled by prp2lnast and never emit a `for` node). The body is re-walked
    // once per iteration with the iter var bound; see unroll_for.
    case Ntype::Lnast_ntype_for:
      unroll_for();
      break;

    // While / loop — comptime unroll the statically-true infinite form (until a
    // `break`), drop a known-false loop, and leave any data-dependent /
    // non-bool condition verbatim (typecheck flags it; codegen keeps a runtime
    // loop). Dispatch still happens inside unroll_while's verbatim path.
    case Ntype::Lnast_ntype_while:
      dispatch_to_passes(&upass::uPass::process_while);
      unroll_while();
      break;

    default:
      // Unknown / not-yet-handled node type: copy its subtree verbatim so
      // nothing silently disappears from the output tree. Add an explicit
      // A_OP/C_OP entry above when folding behavior is needed.
      emit_subtree_verbatim();
      break;
  }

#undef A_OP
#undef C_OP
  // clang-format on
}

// ── Structural handlers ───────────────────────────────────────────────────────

// ── Declaration pre-step (the "bake") ────────────────────────────────────
// Read the TYPE subtree of a `declare` / standalone `type_spec` ONCE and bake
// the persistent facts into the destination's symbol-table bundle as TYPED
// fields: Kind + declared max/min (+ comptime) on the "0" Entry,
// mode/type_name on the Bundle. No pass ever walks a
// type subtree again; until subtask E retires them, the passes' own walks
// coexist (their maps stay authoritative for their checks).
void uPass_runner::bake_decl_pre_step(bool is_declare) {
  // Cursor on the declare/type_spec node; restored before returning.
  if (!lm->has_child()) {
    return;
  }
  lm->move_to_child();
  if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->move_to_parent();
    return;
  }
  const std::string var{lm->current_text()};
  const bool        dotted = var.find('.') != std::string::npos;

  upass::Kind kind = upass::Kind::unknown;
  Dlop        decl_max;  // invalid = unbounded/unset
  Dlop        decl_min;
  Dlop        elem_max;  // array declares: the ELEMENT envelope ([4][8]u8 → u8)
  Dlop        elem_min;
  std::string type_name;
  upass::Mode mode     = upass::Mode::unknown;
  bool        comptime = false;

  if (lm->move_to_sibling()) {  // TYPE slot
    const auto t = lm->get_raw_ntype();
    if (Lnast_ntype::is_comp_type_array(t)) {
      // Array declare — bake the innermost element's envelope as INTERNAL
      // bundle attrs (__elem_max/__elem_min; bitwidth checks element stores
      // against them). Entry-0 decl_max/min stay untouched: stamping them
      // would make the array read as a scalar of the element type
      // (decl_facts/try_decl_type consumers).
      int depth = 0;
      while (Lnast_ntype::is_comp_type_array(lm->get_raw_ntype()) && lm->has_child()) {
        lm->move_to_child();  // child0 = element (possibly a nested array)
        ++depth;
      }
      if (Lnast_ntype::is_prim_type_int(lm->get_raw_ntype()) && lm->has_child()) {
        lm->move_to_child();
        ++depth;
        if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
          if (auto v = Dlop::from_pyrope(lm->current_text()); v->is_integer()) {
            elem_max = *v;
          }
        }
        if (lm->move_to_sibling() && Lnast_ntype::is_const(lm->get_raw_ntype())) {
          if (auto v = Dlop::from_pyrope(lm->current_text()); v->is_integer()) {
            elem_min = *v;
          }
        }
      }
      for (; depth > 0; --depth) {
        lm->move_to_parent();
      }
    } else if (Lnast_ntype::is_prim_type_int(t)) {
      kind = upass::Kind::integer;
      if (lm->move_to_child()) {  // up to two const bounds; a non-integer child = unbounded
        if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
          if (auto v = Dlop::from_pyrope(lm->current_text()); v->is_integer()) {
            decl_max = *v;
          }
        }
        if (lm->move_to_sibling() && Lnast_ntype::is_const(lm->get_raw_ntype())) {
          if (auto v = Dlop::from_pyrope(lm->current_text()); v->is_integer()) {
            decl_min = *v;
          }
        }
        lm->move_to_parent();
      }
    } else if (Lnast_ntype::is_prim_type_bool(t)) {
      kind = upass::Kind::boolean;
    } else if (Lnast_ntype::is_prim_type_string(t)) {
      kind = upass::Kind::string;
    } else if (Lnast_ntype::is_ref(t)) {
      type_name = lm->current_text();  // named type (`x:Point`)
    }
    // prim_type_none / comp_type_*: nothing scalar to bake here (per-field
    // types of a comp_type_tuple arrive as separate dotted type_specs).

    if (is_declare && lm->move_to_sibling() && Lnast_ntype::is_const(lm->get_raw_ntype())) {
      // mode: space-joined tokens, storage first, optional "comptime".
      const auto txt   = lm->current_text();
      size_t     start = 0;
      while (start <= txt.size()) {
        const size_t sp  = txt.find(' ', start);
        const auto   tok = txt.substr(start, sp == std::string_view::npos ? std::string_view::npos : sp - start);
        if (tok == "mut") {
          mode = upass::Mode::mut_kind;
        } else if (tok == "const") {
          mode = upass::Mode::const_kind;
        } else if (tok == "reg") {
          mode = upass::Mode::reg_kind;
        } else if (tok == "await") {
          mode = upass::Mode::await_kind;
        } else if (tok == "type") {
          mode = upass::Mode::type_kind;
        } else if (tok == "comptime") {
          comptime = true;
        }
        if (sp == std::string_view::npos) {
          break;
        }
        start = sp + 1;
      }
    }
  }
  lm->move_to_parent();

  // Dotted destination (`type_spec(inl1_ar.x, u3)` — the inliner's tuple
  // param prologue, or a per-field `t1.a:T`): write the facts onto the ROOT
  // binding's field entry when it already holds a value; otherwise stash
  // them as pending (applied by dispatch_push at the field's first write).
  if (dotted) {
    const bool any_fact = kind != upass::Kind::unknown || mode != upass::Mode::unknown || !decl_max.is_invalid()
                          || !decl_min.is_invalid() || comptime;
    if (!any_fact) {
      return;
    }
    const auto root  = Bundle::get_first_level(var);
    const auto fpath = Bundle::get_all_but_first_level(var);
    auto       rb    = symbol_table_.get_bundle_for_write(root);
    if (rb && rb->has_trivial(fpath)) {
      Bundle::Entry fe = rb->get_entry(fpath);
      fe.immutable     = false;
      if (kind != upass::Kind::unknown) {
        fe.kind = kind;
      }
      if (mode != upass::Mode::unknown) {
        fe.mode = mode;
      }
      if (!decl_max.is_invalid()) {
        fe.decl_max = decl_max;
      }
      if (!decl_min.is_invalid()) {
        fe.decl_min = decl_min;
      }
      fe.comptime = fe.comptime || comptime;
      rb->set(fpath, std::move(fe));
    } else {
      auto& pf    = symbol_table_.pending_decl_facts[var];
      pf.kind     = kind;
      pf.mode     = mode;
      pf.decl_max = decl_max;
      pf.decl_min = decl_min;
      pf.comptime = comptime;
    }
    return;
  }

  // Contract: a declaration creates+inserts the first bundle for a name.
  // declare → lexical (innermost) scope; a standalone type_spec dst may be a
  // compiler tmp, and set() anchors ___ tmps at the function scope.
  // (provide_bundle_fields guards against the resulting empty data bundles.)
  if (is_declare) {
    (void)symbol_table_.declare_bare(var);  // refuses redecl (persistent loop scopes) — binding kept
  } else if (!symbol_table_.has_known(var)) {
    (void)symbol_table_.set(var, std::make_shared<Bundle>(var));
  }
  auto bundle = symbol_table_.get_bundle_for_write(var);
  if (!bundle) {
    return;
  }
  if (mode != upass::Mode::unknown) {
    bundle->set_mode(mode);
  }
  if (!type_name.empty()) {
    bundle->set_type_name(type_name);
  }
  // The "0" Entry carries the scalar facts. Only touch it when the bundle has
  // a scalar slot (or is empty) — writing a "0" leaf next to named tuple
  // fields would corrupt the shape.
  const bool has_scalar_slot = bundle->is_empty() || bundle->has_trivial("0");
  const bool has_entry_facts = kind != upass::Kind::unknown || !decl_max.is_invalid() || !decl_min.is_invalid() || comptime;
  if (has_scalar_slot && has_entry_facts) {
    Bundle::Entry e = bundle->get_entry("0");
    e.immutable     = false;  // get_entry's missing-key sentinel is immutable; a decl entry is writable
    if (kind != upass::Kind::unknown) {
      e.kind = kind;
    }
    if (mode != upass::Mode::unknown) {
      e.mode = mode;  // rides entry copies into aggregates (per-field mut/const)
    }
    if (!decl_max.is_invalid()) {
      e.decl_max = decl_max;
    }
    if (!decl_min.is_invalid()) {
      e.decl_min = decl_min;
    }
    e.comptime = e.comptime || comptime;
    bundle->set("0", std::move(e));
  }
  if (!elem_max.is_invalid()) {
    bundle->set_attr("__elem_max", elem_max);
  }
  if (!elem_min.is_invalid()) {
    bundle->set_attr("__elem_min", elem_min);
  }

  // Back-flow: when this dst is a tuple_get extraction tmp (`___2 = ___1.a`
  // then `type_spec(___2, T)` / `declare(___2, …, mut)` — the typed-tuple-
  // literal lowering), copy the facts onto the SOURCE field entry too, so
  // they ride the aggregate into every alias (`t = ___1`) and back out
  // through extraction. Deliberately OUTSIDE the entry-facts gate: a
  // mode-only per-field declare must back-flow even though it bakes no
  // entry on the tmp itself.
  if (kind != upass::Kind::unknown || mode != upass::Mode::unknown || !decl_max.is_invalid() || !decl_min.is_invalid()
      || comptime) {
    if (const auto oit = symbol_table_.tget_origin.find(var); oit != symbol_table_.tget_origin.end()) {
      const std::string& path  = oit->second;
      const auto         root  = Bundle::get_first_level(path);
      const auto         fpath = Bundle::get_all_but_first_level(path);
      if (!fpath.empty()) {
        if (auto src_b = symbol_table_.get_bundle_for_write(root); src_b) {
          if (src_b->has_trivial(fpath)) {
            Bundle::Entry fe = src_b->get_entry(fpath);
            fe.immutable     = false;
            if (kind != upass::Kind::unknown) {
              fe.kind = kind;
            }
            if (mode != upass::Mode::unknown) {
              fe.mode = mode;
            }
            if (!decl_max.is_invalid()) {
              fe.decl_max = decl_max;
            }
            if (!decl_min.is_invalid()) {
              fe.decl_min = decl_min;
            }
            fe.comptime = fe.comptime || comptime;
            src_b->set(fpath, std::move(fe));
          } else if (src_b->has_top_named(fpath)) {
            // BUNDLE-valued field (`mut b = (…)` inside a literal): there is
            // no scalar entry to carry mode/comptime — use per-field attrs.
            if (mode != upass::Mode::unknown) {
              src_b->set_attr(fpath, "fmode", *Dlop::create_integer(static_cast<int>(mode)));
            }
            if (comptime) {
              src_b->set_attr(fpath, "fcomptime", *Dlop::create_integer(1));
            }
          }
        }
      }
    }
  }
}

void uPass_runner::process_top() {
  // staging_parent is the already-materialized root slot; overwrite its
  // data with the input top node (preserves the correct text/token).
  if (materialize_) {
    staging->set_data(staging_parent, lm->current_type());
  }

  if (lm->has_child()) {
    lm->move_to_child();
    do {
      process_lnast();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
}

void uPass_runner::process_stmts() {
  // The runner owns the scope transition: push the block scope (keyed
  // by the scope uid, which folds in the inline/loop iteration salt) BEFORE
  // any pass dispatch, and mark it uncertain when this stmts is the body of
  // an if-arm whose condition didn't fold (next_block_uncertain_ is set at
  // the notify_uncertain_arm_begin dispatch site).
  enter_block_scope();
  // Pre-dispatch lets passes seed per-block state before children are
  // walked; post-dispatch (after emit_pop) lets them tear it down. The
  // cursor is restored by dispatch_to_passes around each pass call, so
  // passes can move freely without disturbing the runner's traversal.
  dispatch_to_passes(&upass::uPass::process_stmts);
  emit_push(lm->current_type());
  if (lm->has_child()) {
    lm->move_to_child();
    do {
      process_lnast();
      if (loop_break_hit_ || loop_continue_hit_) {
        break;  // a comptime `break`/`continue` fired in a nested block — stop emitting the rest
      }
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
  // Pre-pop hook fires while the staging cursor is still inside the stmts
  // block, so a pass (e.g. coalescer) can flush deferred writes into the
  // closing block rather than the parent scope. process_stmts_post fires
  // after the pop and is for tear-down (e.g. constprop's scope leave).
  dispatch_to_passes(&upass::uPass::process_stmts_pre_pop);
  emit_pop();
  dispatch_to_passes(&upass::uPass::process_stmts_post);
  // Pop AFTER stmts_post so passes' tear-down hooks (e.g. constprop's pub
  // harvest, which reads the scope depth) still see the block scope active.
  symbol_table_.leave_scope();
}

void uPass_runner::process_if() {
  // Dispatch first so passes can update their symbol tables from the condition.
  dispatch_to_passes(&upass::uPass::process_if);

  // Slice 7 — dead-branch elimination.
  // When the first child is a comptime-known condition (ref or const), emit
  // only the taken branch's stmts spliced into the parent (no if node).
  // Cursor invariant: must be at the if-node on all exit paths.
  // See upass.md §Slice 7 and §5 (if cursor discipline).
  //
  // Two if shapes are recognized:
  //   * Scoped form: (cond, stmts, [cond, stmts]…, [stmts]) — normal
  //     if/elif/else. The body's `stmts` wrapper is a real scope.
  //   * Flat form (when/unless): (cond, stmt, [stmt]…) — gated stmts with
  //     no `stmts` wrapper. The body is executed in the parent scope when
  //     the cond is true. Emitted by prp2lnast for `s when c` / `s unless c`.
  //     when/unless conditions are required to be comptime-known; if the
  //     fold here fails, downstream verifier flags it as a build error.
  if (lm->has_child()) {
    lm->move_to_child();
    using Ntype    = Lnast_ntype;
    const auto raw = lm->get_raw_ntype();

    if (raw != Ntype::Lnast_ntype_stmts) {
      // First child is a condition (ref or const). Try to fold it.
      std::optional<Dlop> cval;
      if (raw == Ntype::Lnast_ntype_const) {
        cval = *Dlop::from_pyrope(lm->current_text());
      } else {
        cval = try_fold_ref(lm->current_text());
      }

      // Peek at the body shape: if the second child is not `stmts`, this
      // is a flat (when/unless) if. Flat ifs have no else/elif chain.
      bool is_flat = false;
      if (lm->move_to_sibling()) {
        is_flat = lm->get_raw_ntype() != Ntype::Lnast_ntype_stmts;
      }
      // The peek above moved cursor onto the second child (or invalid
      // if there isn't one). The original move_to_child pushed the
      // if-node onto the cursor stack, so a single move_to_parent
      // restores cursor to the if-node and unwinds that push. After
      // that we re-enter via move_to_child to leave the cursor at the
      // condition again — exactly mirroring the pre-peek state.
      lm->move_to_parent();
      lm->move_to_child();

      if (is_flat) {
        // Flat form: cond known-true → emit each body stmt in parent scope;
        // cond known-false → drop entirely; cond unknown → emit verbatim
        // (when/unless conditions must be comptime-known; the verifier
        // reports the build error downstream).
        if (cval && !cval->is_invalid() && !cval->has_unknowns() && !cval->is_nil()) {
          const bool taken = !cval->is_known_false();
          if (taken) {
            while (lm->move_to_sibling()) {
              process_lnast();
              if (loop_break_hit_ || loop_continue_hit_) {
                break;  // a comptime `break`/`continue` fired — stop emitting the rest of this flat body
              }
            }
          }
          lm->move_to_parent();
          return;  // pruned — no if node emitted
        }
        // Unknown cond: emit the if and its children verbatim, no
        // dispatch into the body (we don't want the body's effects to
        // mutate the symbol table when the gate didn't fire).
        lm->move_to_parent();  // matches the move_to_child above
        emit_subtree_verbatim();
        return;
      }

      if (cval && !cval->is_invalid() && !cval->has_unknowns() && !cval->is_nil()) {
        const bool taken = !cval->is_known_false();

        // Advance past the condition to the then-stmts.
        if (lm->move_to_sibling()) {
          if (taken) {
            // Emit the then-stmts block (preserving its scope) — the if
            // node itself is dropped, but the stmts wrapper stays so that
            // `mut x = ...` inside the body remains scoped to that block
            // instead of leaking into the parent.
            process_lnast();
            lm->move_to_parent();  // back to if-node
            return;                // pruned — no if node emitted
          }

          // Condition is false: skip the then-stmts, look for an else-stmts.
          if (lm->move_to_sibling()) {
            if (lm->get_raw_ntype() == Ntype::Lnast_ntype_stmts) {
              // Bare else-stmts: emit it (preserving its scope), drop the if.
              process_lnast();
              lm->move_to_parent();  // back to if-node
              return;                // pruned
            }
            // elif chain: fall through to full-if emit with per-arm dead
            // branch tracking (see loop below).
          } else {
            // No else-stmts: false condition → emit nothing.
            lm->move_to_parent();  // back to if-node
            return;                // pruned
          }
        }
      }
    }

    lm->move_to_parent();  // condition unknown or elif chain — fall through
  }

  // Unknown condition (or elif chain): emit the full if node unchanged.
  emit_push(lm->current_type());
  if (lm->has_child()) {
    lm->move_to_child();
    // Branch elimination for known conditions. The if's children alternate
    // (cond, stmts) pairs, with an optional trailing stmts (else). For
    // every cond/stmts pair we peek at the cond's folded value:
    //   - known-false: emit the stmts subtree verbatim (no constprop
    //     dispatch into the body) so dead-branch assigns can't update
    //     the symbol table. Without this, `if false { c = 2 }` would
    //     overwrite c=1 because constprop's process_assign runs
    //     unconditionally during traversal.
    //   - known-true (and we haven't seen a true arm yet): process the
    //     body normally; mark a "matched" flag so any later arms / else
    //     are emit-verbatim'd (only the first matching arm runs).
    //   - matched-already (previous arm was known-true): emit verbatim.
    //   - unknown / partially-known: process normally (conservative —
    //     the symbol table merges values across both branches today).
    bool last_cond_false     = false;
    bool last_cond_true      = false;
    bool last_was_cond       = false;
    bool already_matched     = false;  // a *previous* arm already fired
    bool any_prior_uncertain = false;  // some earlier cond folded to neither true nor false

    auto cond_value = [this]() -> std::optional<Dlop> {
      if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_const) {
        try {
          return *Dlop::from_pyrope(lm->current_text());
        } catch (...) {
          return std::nullopt;
        }
      }
      if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
        return try_fold_ref(lm->current_text());
      }
      return std::nullopt;
    };

    do {
      auto t = lm->get_raw_ntype();
      if (t == Lnast_ntype::Lnast_ntype_stmts) {
        // Body for the prior cond, or the trailing else (no prior cond
        // this round). Dead iff a prior arm has already fired, or the
        // immediate cond just folded to false.
        const bool dead         = already_matched || (last_was_cond && last_cond_false);
        // `just_matched` only fires when no earlier arm was uncertain — if
        // a prior cond was undecided at comptime (nil/unknown) the runtime
        // ordering may still pick *that* arm, so a later concrete-true
        // cond doesn't deterministically take over. Treat the body as
        // uncertain instead so its writes get invalidated on exit.
        // Without this gate, a `match` chain where `case (a=33,…)` folds
        // to nil and `case (a=2,…)` folds to true would still concretely
        // apply case-2's body, leaving the var "definitely 1052" even
        // though case-1 might actually fire at runtime.
        const bool just_matched = last_was_cond && last_cond_true && !any_prior_uncertain;
        // Uncertain := body executes but isn't *guaranteed* to. After a
        // cond: !just_matched (dead is handled separately, so the cond
        // wasn't known-false). Trailing else with no preceding cond: only
        // uncertain when some prior arm's cond didn't fold either way; if
        // every prior cond folded to known-false the else *is* guaranteed.
        const bool uncertain    = last_was_cond ? !just_matched : any_prior_uncertain;
        last_was_cond           = false;
        if (dead) {
          emit_subtree_verbatim();
          continue;
        }
        if (uncertain) {
          next_block_uncertain_ = true;  // The arm's stmts scope gets mark_current_uncertain
          dispatch_to_passes(&upass::uPass::notify_uncertain_arm_begin);
        }
        process_lnast();
        if (uncertain) {
          dispatch_to_passes(&upass::uPass::notify_uncertain_arm_end);
        }
        // Process the body first, THEN flip the matched flag — so the
        // current arm's body actually dispatches into constprop. Only
        // *subsequent* arms / else become dead.
        if (just_matched) {
          already_matched = true;
        }
        continue;
      }

      // Non-stmts child — must be a cond (ref/const).
      auto       val        = cond_value();
      // nil cond models "comptime can't decide" (e.g. a `case` whose values
      // didn't match but whose runtime predicate might still fire). Treat
      // it the same as has_unknowns(): not known-true and not known-false,
      // so the arm body is visited as uncertain rather than dead-pruned.
      const bool val_is_nil = val.has_value() && !val->is_invalid() && val->is_nil();
      last_cond_true        = val.has_value() && !val->is_invalid() && !val_is_nil && val->is_known_true();
      last_cond_false       = val.has_value() && !val->is_invalid() && !val_is_nil && val->is_known_false();
      last_was_cond         = true;
      if (!last_cond_true && !last_cond_false) {
        any_prior_uncertain = true;
      }
      process_lnast();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
  emit_pop();
}
