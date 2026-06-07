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
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_set.h"
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

// Task 1k — prp2lnast wraps the UFCS receiver of `obj.method(...)` in a
// `store(__ufcs_arg, obj)` marker (positional, like __ref_arg) so the runner
// can reject the UFCS form when the callee declares no `self`.
constexpr std::string_view call_ufcs_arg_marker = "__ufcs_arg";

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
  auto fold_fn    = [this](std::string_view name) { return try_fold_ref(name); };
  auto emit_at_fn = [this](const Lnast_nid& src) { emit_op_with_fold_at(src); };
  for (auto& entry : upasses) {
    entry.pass->set_runner_fold_fn(fold_fn);
    entry.pass->set_runner_emit_at_fn(emit_at_fn);
    entry.pass->set_runner_symbol_table(&runner_symbol_table);
    entry.pass->set_options(options);
  }

  // Pre-cache pass subsets so the hot try_fold_ref / any_pass_drops loops
  // only visit passes that actually override the relevant virtual. Order is
  // preserved (try_fold_ref takes the first non-nullopt; attributes' wrap/sat
  // narrowing must beat constprop's raw value, etc.).
  fold_capable_passes.reserve(upasses.size());
  classify_capable_passes.reserve(upasses.size());
  for (auto& entry : upasses) {
    if (entry.pass->overrides_fold_ref()) {
      fold_capable_passes.push_back(entry.pass.get());
    }
    if (entry.pass->overrides_classify_statement()) {
      classify_capable_passes.push_back(entry.pass.get());
    }
    if (entry.pass->overrides_shared_st()) {
      shared_st_passes_.push_back(entry.pass.get());
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

  // Carry an if/while source span across the staging rebuild so upass/typecheck's
  // cond-not-bool can point at the condition; same for store/declare (bitwidth's
  // "does not fit" points at the write/declaration) and range (constprop's
  // descending-range error points at the `a..=b`). prp2lnast records it
  // (attach_loc) but a func_extract / constprop rebuild re-creates the node via
  // emit_push, which doesn't otherwise copy loc. cassert / func_call are carried
  // at their own emit sites (emit_op_with_fold, try_inline_func_call). Gate on
  // the current source node actually being this node kind so a synthetic block
  // (e.g. a for-unroll `create_stmts`) emitted with a literal type doesn't grab
  // an unrelated loc.
  if ((type == Lnast_ntype::Lnast_ntype_if || type == Lnast_ntype::Lnast_ntype_while || type == Lnast_ntype::Lnast_ntype_store
       || type == Lnast_ntype::Lnast_ntype_declare || type == Lnast_ntype::Lnast_ntype_range
       || type == Lnast_ntype::Lnast_ntype_attr_set)
      && lm->current_type() == type) {
    const auto& src_ln  = lm->get_lnast();
    const auto  src_nid = lm->get_current_nid();
    const auto  loc     = src_ln->get_loc(src_nid);
    if (loc.pos1 != 0 || loc.pos2 != 0 || loc.line != 0 || loc.tok != 0) {
      staging->set_loc(nid, loc);
      if (auto fn = src_ln->get_fname(src_nid); !fn.empty()) {
        staging->set_fname(nid, fn);
      }
    }
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
  staging->add_child(staging_parent, type);
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

std::optional<Const> uPass_runner::try_fold_ref(std::string_view name) {
  for (auto* p : fold_capable_passes) {
    auto folded = p->fold_ref(name);
    if (folded) {
      return folded;
    }
  }
  return std::nullopt;
}

std::optional<std::vector<std::pair<std::string, Const>>> uPass_runner::try_bundle_fields(std::string_view name) {
  for (auto* p : shared_st_passes_) {
    if (auto b = p->provide_bundle_fields(name)) {
      return b;
    }
  }
  return std::nullopt;
}

std::string uPass_runner::try_typename(std::string_view name) {
  for (auto* p : shared_st_passes_) {
    if (auto tn = p->provide_typename(name); !tn.empty()) {
      return tn;
    }
  }
  return {};
}

std::optional<upass::uPass::Decl_scalar_type> uPass_runner::try_decl_type(std::string_view name) {
  for (auto* p : shared_st_passes_) {
    if (auto dt = p->provide_decl_type(name)) {
      return dt;
    }
  }
  return std::nullopt;
}

std::optional<std::pair<Const, Const>> uPass_runner::try_range(std::string_view name) {
  for (auto* p : shared_st_passes_) {
    if (auto r = p->provide_range(name)) {
      return r;
    }
  }
  return std::nullopt;
}

std::optional<upass::uPass::Field_decl_type> uPass_runner::try_field_type(std::string_view name) {
  for (auto* p : shared_st_passes_) {
    if (auto ft = p->provide_field_type(name)) {
      return ft;
    }
  }
  return std::nullopt;
}

upass::uPass::Decl_storage uPass_runner::try_decl_storage(std::string_view name) {
  for (auto* p : shared_st_passes_) {
    if (auto s = p->provide_decl_storage(name); s != upass::uPass::Decl_storage::unknown) {
      return s;
    }
  }
  return upass::uPass::Decl_storage::unknown;
}

void uPass_runner::check_self_does(const livehd::diag::Span& span, std::string_view callee_name, std::string_view decl_tn,
                                   const Lnast_node& receiver) {
  // Structural `does`-check (task 1k, 07b-structtype.md): every field of the
  // declared self type must exist on the receiver with a matching scalar kind;
  // integer fields additionally need receiver-range ⊆ declared-range. Checks
  // run per flat dotted leaf (bundle keys are canonical dotted paths), which
  // makes the recursion over tuple fields implicit. NEVER a typename
  // comparison — a superset receiver passes.
  auto decl_fields = try_bundle_fields(decl_tn);
  if (!decl_fields || decl_fields->empty()) {
    return;  // unknown / scalar named type — nothing structural to check
  }

  const std::string                                         recv_name(receiver.is_ref() ? receiver.get_name() : std::string_view{});
  const std::string                                         recv_tn = recv_name.empty() ? std::string{} : try_typename(recv_name);
  std::optional<std::vector<std::pair<std::string, Const>>> recv_fields;
  if (!recv_name.empty()) {
    recv_fields = try_bundle_fields(recv_name);
  }
  // The receiver's own TYPE bundle (when it has a declared typename) is the
  // reliable field directory: its defaults are always comptime, while a
  // receiver field holding a non-comptime value is skipped by
  // provide_bundle_fields and would otherwise read as "missing".
  std::optional<std::vector<std::pair<std::string, Const>>> recv_type_fields;
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
      = [](const std::optional<std::vector<std::pair<std::string, Const>>>& fields, std::string_view key) -> const Const* {
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
    const Const* rv = find_field(recv_fields, fld);
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
  emit_push(op_ntype);

  // Carry a cassert's / func_call's source span across the staging rebuild.
  // prp2lnast records it (attach_loc) so the verifier can point at a
  // comptime-false assertion and the inliner can point at an argument-naming
  // error, but a func_extract / constprop rebuild re-creates the node via
  // emit_push, which doesn't copy loc. Scoped to these two op kinds to avoid
  // perturbing other nodes' dumps; the general per-node carry is the sourcemap
  // work (task 1f).
  if (op_ntype == Lnast_ntype::Lnast_ntype_cassert || op_ntype == Lnast_ntype::Lnast_ntype_func_call) {
    const auto& src_ln  = lm->get_lnast();
    const auto  src_nid = lm->get_current_nid();
    const auto  loc     = src_ln->get_loc(src_nid);
    if (loc.pos1 != 0 || loc.pos2 != 0 || loc.line != 0 || loc.tok != 0) {
      staging->set_loc(staging_parent, loc);
    }
    const auto fn = src_ln->get_fname(src_nid);
    if (!fn.empty()) {
      staging->set_fname(staging_parent, fn);
    }
  }

  // Task 1t — a `declare`/`type_spec` whose type slot (child 1) is a named-type
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
    // constprop's sub_op refuses a string-typed invalid Const) or is
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
  // 2. Ask every pass whether to keep this statement; first drop wins.
  // 3. Emit (with operand folding) unless dropped.
  if (!any_pass_drops()) {
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

// ── 1i comb-call inliner ──────────────────────────────────────────────────────

void uPass_runner::set_function_registry(const std::vector<std::shared_ptr<Lnast>>& lnasts) {
  function_registry.clear();
  for (const auto& ln : lnasts) {
    if (ln) {
      function_registry.emplace(std::string(ln->get_top_module_name()), ln);
    }
  }

  // Build the call graph and mark callees that can reach themselves (direct or
  // mutual recursion). 1i bails those to the evaluator, which fully unrolls
  // comptime-bounded recursion; runner-side fuel-bounded recursion is Phase D.
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
    // an iterative work-stack rewrite of process_lnast (see 1i Phase D note).
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
          // so `foo = ___t` inherits ___t's tuple-valued-ness. Task 1t — this is
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
      if (!defined.contains(o.name)) {
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
  // Gather the exact-name module plus any unique "<module>.<name>" body.
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
  // Prefer a candidate carrying a real comb signature (non-empty io). When a
  // file's top module shares the comb's name (fcall1.prp's `comb fcall1`), the
  // registry holds an empty-io top module `fcall1` AND the actual extracted body
  // `fcall1.fcall1`; the body must win or try_inline bails on the empty-io top.
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

void uPass_runner::flush_deferred_emits() { dispatch_to_passes(&upass::uPass::flush_deferred); }

void uPass_runner::emit_inline_binding(const std::string& lhs, const Lnast_node& rhs) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-bind");
  auto s    = std::make_shared<Lnast>(body, "inl-bind");
  auto root = s->set_root(Lnast_ntype::create_store());
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
  s->add_child(root, Lnast_node::create_ref(name));
  // Task 1t — emit the canonical prim_type_int(max,min) from (bits, signed).
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

void uPass_runner::emit_inline_typespec_range(const std::string& name, const std::optional<Const>& range_max,
                                              const std::optional<Const>& range_min) {
  if (!range_max && !range_min) {
    return;  // fully unbounded — nothing concrete to declare
  }
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-type");
  auto s    = std::make_shared<Lnast>(body, "inl-type");
  auto root = s->set_root(Lnast_ntype::create_type_spec());
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
  // argument-naming diagnostics below can point at the call line. Read from the
  // source tree (prp2lnast's attach_loc survives the lnastfmt round-trip).
  livehd::diag::Span call_span;
  {
    const auto& src_ln = lm->get_lnast();
    const auto  loc    = src_ln->get_loc(lm->get_current_nid());
    const auto  fn     = src_ln->get_fname(lm->get_current_nid());
    if (!fn.empty()) {
      call_span.file = std::string(fn);
    }
    if (loc.line != 0) {
      call_span.start_line = loc.line;
    }
    if (loc.pos1 != 0 || loc.pos2 != 0) {
      call_span.start_byte = loc.pos1;
      call_span.end_byte   = loc.pos2;
    }
  }

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
  // actual). The receiver rides inside the task-1k UFCS marker store; unwrap
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

  // Task 1m — call through a lambda-ref binding: `const f = b.add1` or
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

  if (!callee) {
    lm->restore_cursor(saved);
    return false;  // not a known comb body → typecast / cell-op / marker path
  }

  // Task 1k — call-form enforcement: the UFCS form `obj.f(...)` is only valid
  // when the callee declares `self` (as its first input). The direct form
  // `f(obj, ...)` carries no marker and stays subject to the normal argument
  // rules. Checked here — before the inlinable/fuel gates — so a marked call
  // can never silently fall through to the evaluator path.
  //
  // Task 1m exemption — namespace access through an import tuple:
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

  struct Actual {
    bool        is_named{false};
    bool        is_ref_pass{false};  // `f(ref x)` — positional by design, exempt from naming rules
    std::string key;
    Lnast_node  node{Lnast_node::create_invalid()};
    std::string func_name;  // non-empty: this actual is a function value (closure)
  };
  // A ref actual whose raw name is itself a registry function is a higher-order
  // / closure argument: capture the function name so the body's `f(x)` can
  // resolve to it (see func_param_bindings_).
  auto func_actual_name = [&]() -> std::string {
    if (Lnast_ntype::is_ref(lm->get_raw_ntype()) && lookup_callee(lm->current_raw_text()) != nullptr) {
      return std::string(lm->current_raw_text());
    }
    return {};
  };

  std::vector<Actual> actuals;
  bool                shape_ok = true;
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
      // the `ref` param's write-back fires (see the io.is_ref check below).
      if (a.key == "__ref_arg") {
        a.is_named    = false;
        a.is_ref_pass = true;
        a.key.clear();
      }
      // Task 1k — the UFCS receiver marker is positional too; the receiver
      // binds to `self` via the has_self branch below (the no-self UFCS error
      // already fired right after callee resolution). Task 1m — a namespace
      // access (`b.add1(...)` through an import tuple) drops the receiver
      // entirely: it names the namespace, it is not an argument.
      if (a.key == call_ufcs_arg_marker) {
        if (drop_ufcs_receiver) {
          lm->restore_cursor(here);
          continue;
        }
        a.is_named = false;
        a.key.clear();
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
  lm->restore_cursor(saved);  // back on the func_call node
  if (!shape_ok) {
    return false;
  }

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
  const std::size_t        nparams = io.inputs.size();
  std::vector<Lnast_node>  param_val(nparams, Lnast_node::create_invalid());
  std::vector<bool>        param_set(nparams, false);
  std::vector<std::string> param_func(nparams);  // non-empty: function-valued param
  auto                     param_index = [&](std::string_view k) -> std::size_t {
    for (std::size_t i = 0; i < nparams; ++i) {
      if (io.inputs[i].name == k) {
        return i;
      }
    }
    return nparams;
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
  const bool        has_self       = nparams > 0 && io.inputs[0].name == "self";
  const std::size_t n_named_params = nparams - (has_self ? 1 : 0);
  // Classify a positional actual's scalar kind for exception (3). Const literals
  // carry their kind verbatim; a typed `ref` whose declared range is known is an
  // integer. Anything else is `none` (can't drive type-based disambiguation).
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
    if (node.is_ref() && try_decl_type(node.get_name()).has_value()) {
      return Io_kind::integer;  // typed scalar var (range carrier)
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
      if (idx >= nparams) {
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
              if (lidx >= nparams || val.is_invalid()) {
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
        for (std::size_t i = from; i < nparams; ++i) {
          if (!param_set[i]) {
            return i;
          }
        }
        return nparams;
      };
      // Pass-by-ref (`f(ref x)`) binds positionally by design — exempt from the
      // naming rules — and so does the UFCS receiver bound to `self` (the first
      // positional actual when the callee declares self).
      if (a.is_ref_pass) {
        const auto slot = next_unset(0);
        if (slot >= nparams) {
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

  // Task 1q — a `pipe` callee (any output carries a stages annotation) is
  // never comb-inlined: its outputs are flopped, and a call site must consume
  // it via `stage[N]` (later phase). Decline so the call surfaces unresolved
  // instead of silently dropping the latency.
  for (const auto& oe : io.outputs) {
    if (oe.stages_min > 0) {
      return false;
    }
  }

  // Task 1r — a plain `mod` callee is its own module: the call becomes a
  // Sub instance (1r-D), never a comb splice — even when every declared
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

  // Every non-self parameter must be bound — io_meta carries no defaults, so an
  // unset input means the caller omitted a required argument.
  for (std::size_t i = (has_self ? 1 : 0); i < nparams; ++i) {
    if (!param_set[i]) {
      fcall_arg_fail(call_span,
                     "fcall-missing-arg",
                     std::format("missing required argument `{}` in call to `{}`", io.inputs[i].name, callee_name),
                     "provide the argument by name");
    }
  }

  // Task 1k — typed `self:T`: the bound receiver must satisfy `receiver does
  // T` (structural; both call forms and the tuple-field method dispatch land
  // here). Untyped self skips the check entirely.
  if (has_self && param_set[0] && !io.inputs[0].type_name.empty()) {
    check_self_does(call_span, callee_name, io.inputs[0].type_name, param_val[0]);
  }

  // Task 1k — ref-actual mutability: a `ref` param (incl. `ref self`, whose
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
    if (!param_func[i].empty()) {
      auto it = func_param_bindings_.find(e.name);
      saved_func_bindings.emplace_back(e.name,
                                       it == func_param_bindings_.end() ? std::nullopt : std::optional<std::string>(it->second));
      func_param_bindings_[e.name] = param_func[i];
      continue;  // function value — no width/value binding
    }
    const auto pname = upass::Lnast_manager::make_inlined_name(tag, e.name);
    if (e.bits > 0) {
      emit_inline_typespec(pname, e.bits, e.is_signed);
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
  for (const auto& o : io.outputs) {
    const auto oname = upass::Lnast_manager::make_inlined_name(tag, o.name);
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
  active_inline_callees_.pop_back();
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
  if (io.outputs.empty()) {
    // Void comb (e.g. `top()` called for its casserts) — nothing to bind back.
  } else if (io.outputs.size() == 1) {
    emit_inline_binding(dst_name, output_ref(io.outputs[0]));
  } else {
    std::vector<std::pair<std::string, Lnast_node>> fields;
    fields.reserve(io.outputs.size());
    for (const auto& o : io.outputs) {
      fields.emplace_back(o.name, output_ref(o));
    }
    emit_inline_tuple(dst_name, fields);
  }
  for (auto& [caller_var, src] : writebacks) {
    emit_inline_binding(caller_var, src);
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
  // candidate whose non-self formals fit the args wins. Arity for
  // positional; full key-coverage for named.
  for (const auto& fn : candidates) {
    auto callee = lookup_callee(fn);
    if (!callee) {
      continue;
    }
    const auto& cio = callee->io_meta();
    if (cio.inputs.empty() || cio.inputs[0].name != "self") {
      continue;  // init must be a method (ref self first)
    }
    const std::size_t formal = cio.inputs.size() - 1;
    if (args.size() != formal) {
      continue;
    }
    bool names_ok = true;
    for (const auto& a : args) {
      if (a.key.empty()) {
        continue;
      }
      bool found = false;
      for (std::size_t i = 1; i < cio.inputs.size(); ++i) {
        if (cio.inputs[i].name == a.key) {
          found = true;
          break;
        }
      }
      if (!found) {
        names_ok = false;
        break;
      }
    }
    if (names_ok) {
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
  if (!pending_ctor_store_.erase(x)) {
    return false;  // re-assignment, not the declaration initializer
  }
  if (!v_is_ref && !v_is_const) {
    return false;
  }

  const auto tn = try_typename(x);

  // Mod-init form: `mut y:Mix_tup = mix_tup_init` — the value names a
  // registry function whose first input is `ref self`. The function IS the
  // constructor; T needs no `init` field. Requires the typename so a plain
  // function-value alias (`mut f = somefunc`) stays an alias.
  if (v_is_ref && !v_raw.empty()) {
    if (auto fc = lookup_callee(v_raw)) {
      const auto& cio = fc->io_meta();
      if (!tn.empty() && !cio.inputs.empty() && cio.inputs[0].name == "self" && cio.inputs[0].is_ref) {
        splice_init_call(x, tn, v_raw, {});
        return true;
      }
      return false;  // function value, but not a ctor shape — plain alias
    }
  }

  const auto candidates = init_candidates_of(tn);
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

bool uPass_runner::walk_loop_iteration(const std::vector<std::pair<std::string, Const>>& binds) {
  // Precondition: the read cursor is on the loop body `stmts` node.
  if (inline_budget_ == 0 || loop_depth_ > static_cast<int>(kInlineMaxDepth)) {
    return false;  // fuel / depth guard — a non-terminating comptime loop bails (like recursion)
  }
  --inline_budget_;
  const uint32_t iter_salt = ++inline_seq_;

  flush_deferred_emits();                            // flush outer parked writes before the iteration frame
  lm->push_iteration(iter_salt);                     // fresh salt → fresh tmp namespace + block-scope id; cursor + tag kept
  dispatch_to_passes(&upass::uPass::process_stmts);  // passes open a block scope (scope_uid uses iter_salt)
  emit_push(Lnast_ntype::create_stmts());            // staging block for this iteration

  // Bind the iteration variable(s) into this scope so the body's reads fold.
  for (const auto& [name, val] : binds) {
    emit_inline_binding(name, Lnast_node::create_const(std::string(val.to_pyrope())));
  }

  if (lm->move_to_child()) {
    do {
      process_lnast();
      if (loop_break_hit_) {
        break;  // a comptime `break` fired — skip the rest of this iteration's body
      }
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }

  dispatch_to_passes(&upass::uPass::process_stmts_pre_pop);  // coalescer flushes this iteration's writes
  emit_pop();
  dispatch_to_passes(&upass::uPass::process_stmts_post);
  lm->pop_source();  // restore cursor (body stmts), outer salt/stack/tag
  return true;
}

void uPass_runner::unroll_for() {
  // Cursor on the `for` node. Range-for layout (prp2lnast):
  //   for( ref(i), <iterable-ref>, stmts(body) )
  // The iterable is a `range` tmp whose (lo, hi_inclusive) bounds constprop
  // recorded (process_range) when the preceding sibling range statement walked.
  // Tuple-iteration for-loops are unrolled by prp2lnast and never reach here.
  if (!lm->has_child()) {
    emit_subtree_verbatim();
    return;
  }
  lm->move_to_child();  // child0: iter-var ref
  const std::string ivar = std::string(lm->current_text());
  if (!lm->move_to_sibling()) {
    lm->move_to_parent();
    emit_subtree_verbatim();
    return;
  }
  const std::string iterable  = std::string(lm->current_text());  // child1: iterable ref
  const bool        have_body = lm->move_to_sibling();            // child2: body stmts
  lm->move_to_parent();                                           // back to for-node
  if (!have_body) {
    emit_subtree_verbatim();
    return;
  }

  auto rng = try_range(iterable);
  if (!rng || !rng->first.is_i() || !rng->second.is_i()) {
    // `for` is comptime-only; a non-foldable bound is a build error reported
    // elsewhere. Leave the node verbatim rather than silently dropping the body.
    emit_subtree_verbatim();
    return;
  }
  const int64_t lo = rng->first.to_i();
  const int64_t hi = rng->second.to_i();  // inclusive

  // `ivar` was read via current_text(), so it is ALREADY the frame-renamed name
  // the body's reads of the iteration variable resolve to (under the same tag).
  // Bind it directly each iteration — do NOT re-tag (that would double-prefix).
  const std::string& tagged_i = ivar;

  const auto for_bm      = lm->save_cursor();  // on the for-node
  const bool saved_break = loop_break_hit_;
  loop_break_hit_        = false;
  ++loop_depth_;
  for (int64_t v = lo; v <= hi; ++v) {
    lm->restore_cursor(for_bm);  // back to for-node
    lm->move_to_child();         // child0
    lm->move_to_sibling();       // child1
    lm->move_to_sibling();       // child2 (body stmts)
    std::vector<std::pair<std::string, Const>> binds;
    binds.emplace_back(tagged_i, *Dlop::create_integer(v));
    if (!walk_loop_iteration(binds)) {
      break;  // fuel exhausted
    }
    if (loop_break_hit_) {
      break;
    }
  }
  --loop_depth_;
  loop_break_hit_ = saved_break;
  lm->restore_cursor(for_bm);  // leave cursor on the for-node (no node emitted for it)
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
  const auto           cond_type = lm->get_raw_ntype();
  const std::string    cond_text = std::string(lm->current_text());
  std::optional<Const> cval;
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
  const std::vector<std::string>   loop_vars(cond_var_set.begin(), cond_var_set.end());
  absl::flat_hash_set<std::string> seen_states;

  // Span for any loop diagnostic below — the `while` node carries the source loc.
  auto while_span = [&]() {
    livehd::diag::Span span;
    const auto&        s   = lm->get_lnast();
    const auto         nid = lm->get_current_nid();
    const auto         loc = s->get_loc(nid);
    if (const auto fn = s->get_fname(nid); !fn.empty()) {
      span.file = std::string(fn);
    }
    if (loc.line != 0) {
      span.start_line = loc.line;
    }
    if (loc.pos1 != 0 || loc.pos2 != 0) {
      span.start_byte = loc.pos1;
      span.end_byte   = loc.pos2;
    }
    return span;
  };

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
    if (!walk_loop_iteration({})) {
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
  loop_break_hit_ = saved_break;
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
  };
  fresh_staging();

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
    case N::Lnast_ntype_store:  // task 1t — store defines its first-child target (the unified write node)
    case N::Lnast_ntype_dp_assign:
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
  // be wildly off. Skip on func_extract-spawned function bodies: their
  // outputs are consumed externally and would look unreferenced from
  // inside-the-body alone.
  bool has_constprop = false;
  for (auto& e : upasses) {
    if (e.name == "constprop") {
      has_constprop = true;
      break;
    }
  }
  if (!has_constprop || is_function_body_) {
    return;
  }
  using N = Lnast_ntype;

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
      // Only DCE temporary defs (`___N`). User-named variables (out, tmp,
      // alt …) may be observable outputs or have meaning to downstream
      // consumers that DCE can't see (IO ports lose their `%`/`$` prefix
      // by the time they hit staging). Dropping a user-named def with no
      // in-tree readers would silently delete writes the user intended.
      const auto fc_name = staging->get_name(fc);
      if (!Lnast::is_tmp(fc_name)) {
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
#define A_OP(NAME)                                                                              \
  case Ntype::Lnast_ntype_##NAME:                                                               \
    process_drop_candidate(&upass::uPass::process_##NAME, /*fold_all=*/false);                  \
    break;

  // Category C: emit verbatim; still dispatch so passes can observe.
#define C_OP(NAME)                                                                              \
  case Ntype::Lnast_ntype_##NAME: process_verbatim(&upass::uPass::process_##NAME); break;

  switch (get_raw_ntype()) {
    // Structural — push the node into staging and recurse.
    case Ntype::Lnast_ntype_top:   process_top(); break;
    case Ntype::Lnast_ntype_stmts: process_stmts(); break;
    case Ntype::Lnast_ntype_if:    process_if(); break;

    // Statement-scope leaves (e.g. an if condition's ref/const). Fold refs
    // through the symbol table so dropping the producing assign doesn't
    // leave a dangling name.
    case Ntype::Lnast_ntype_ref: emit_ref_or_folded(lm->current_text()); break;
    case Ntype::Lnast_ntype_const:
      emit_leaf(lm->current_node());
      break;

    // Assignment — Task 1t — `store` is the one write/bind node (`assign` was
    // deleted). Branch on arity: a 2-child store is a scalar/wire
    // write (route to process_assign, drop-candidate); a ≥3-child store is a
    // tuple-field write (route to process_tuple_set, verbatim — the bundle
    // mutation is the point, never drop). The pass methods walk children by
    // position, not by node type, so they handle a `store` node unchanged.
    case Ntype::Lnast_ntype_store:
      if (lm->current_num_children() <= 2) {
        // init constructor hook: the DECLARATION store of a typed var whose
        // type carries `init` (or whose value is a ref-self mod/comb) becomes
        // a defaults-bind + spliced constructor call instead of a structural
        // assign. Re-assignment stores never construct (init runs once).
        if (!try_init_construction()) {
          process_drop_candidate(&upass::uPass::process_assign, /*fold_all=*/false);
        }
      } else {
        process_verbatim(&upass::uPass::process_tuple_set);
      }
      break;

    // Task 1t — declare carries type/mode metadata downstream passes still
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
    C_OP(func_continue)
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
    A_OP(tuple_get)
    A_OP(tuple_add)
    // Task 1t — the tuple_set node was deleted; field writes are now `store`
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
    C_OP(type_spec)

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
  // Pre-dispatch lets passes push a block scope before children are
  // walked; post-dispatch (after emit_pop) lets them pop it. The cursor
  // is restored by dispatch_to_passes around each pass call, so passes
  // can move freely without disturbing the runner's traversal.
  //
  // Step D — the runner-owned symbol-table push/pop will land here once
  // Step F's bundle pre-pass populates it. For now the runner-owned
  // table sits idle (a pass that needs it can push/pop via the pointer
  // returned by get_runner_symbol_table()).
  dispatch_to_passes(&upass::uPass::process_stmts);
  emit_push(lm->current_type());
  if (lm->has_child()) {
    lm->move_to_child();
    do {
      process_lnast();
      if (loop_break_hit_) {
        break;  // a comptime `break` fired in a nested block — stop emitting the rest
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
      std::optional<Const> cval;
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
              if (loop_break_hit_) {
                break;  // a comptime `break` fired — stop emitting the rest of this flat body
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

    auto cond_value = [this]() -> std::optional<Const> {
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
