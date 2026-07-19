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
#include <bit>
#include <chrono>
#include <cstdlib>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "call_resolver.hpp"
#include "decl_facts.hpp"
#include "range_bits.hpp"
#include "diag.hpp"
#include "lsp_index.hpp"

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

// Emit a fatal `in`-operator diagnostic (category typecheck) and abort the walk,
// mirroring fcall_arg_fail: the record is flushed crash-safe before the throw so
// the error-test harness sees it. `a in b` is fully expanded here, so an
// unfixable shape/type problem must be a hard error — never a silent nil.
[[noreturn]] void in_op_fail(const livehd::diag::Span& span, std::string_view code, const std::string& msg,
                             std::string_view hint) {
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = std::string{code},
                                                     .category = "type",
                                                     .pass     = "upass.runner",
                                                     .message  = msg,
                                                     .span     = span,
                                                     .hint     = std::string{hint}});
  throw std::runtime_error(msg);
}

// Coarse type of an `in` operand, used to reject comparisons between values that
// can never be equal (06-operators: `a in b` is `a==b[0] or …`, and `==` between
// e.g. an int and a bool / an int and an enum / a scalar and a tuple is a type
// error). Integer width/signedness is intentionally NOT part of the identity
// (`u4 in (s8, …)` is fine); two enums match only when the SAME enum type.
struct In_type {
  enum class K { unknown, integer, boolean, string, enumv, tuple } k = K::unknown;
  std::string enum_type;  // for enumv: the enum TYPE name ("Color" of "Color.Red")
};

const char* in_kind_name(In_type::K k) {
  switch (k) {
    case In_type::K::integer: return "an integer";
    case In_type::K::boolean: return "a boolean";
    case In_type::K::string:  return "a string";
    case In_type::K::enumv:   return "an enum";
    case In_type::K::tuple:   return "a tuple";
    case In_type::K::unknown: return "an unknown type";
  }
  return "an unknown type";
}

// A bundle's OWN enum identity ("Color.Red") → its enum TYPE name ("Color").
// The parse-time `__enumentry` tag lands as the bare "enumentry" attr; a
// scalar-carrier round-trip may intern it under positional layers
// ("0.enumentry") — accept those; an enum-TYPE bundle's per-entry tag
// ("Red.enumentry") has a NAMED prefix and is not the bundle's own.
std::optional<std::string> bundle_enum_type(const std::shared_ptr<const Bundle>& b) {
  if (!b) {
    return std::nullopt;
  }
  for (const auto& [k, ep] : b->get_attrs()) {
    if (Bundle::get_last_level(k) != battr::enumentry) {
      continue;
    }
    std::string_view rest = Bundle::get_all_but_last_level(k);
    bool             own  = true;
    while (!rest.empty() && own) {
      own  = (Bundle::get_first_level(rest) == "0");
      rest = Bundle::get_all_but_first_level(rest);
    }
    if (!own || ep.trivial.is_invalid()) {
      continue;
    }
    std::string id(ep.trivial.to_pyrope());  // e.g. 'Color.Red' / 'Animal.mammal.rat' (may be quoted)
    if (id.size() >= 2 && (id.front() == '\'' || id.front() == '"')) {
      id = id.substr(1, id.size() - 2);
    }
    // The enum TYPE is the TOP-LEVEL name (first segment): flat `Color.Red`→
    // `Color`; hierarchical `Animal.mammal.rat`→`Animal`, so all values of one
    // hierarchical enum share a type (and compare), while `Color` vs `Dir` differ.
    const auto dot = id.find('.');
    return dot == std::string::npos ? id : id.substr(0, dot);
  }
  return std::nullopt;
}

// The own bit-encoding of an enum VALUE: the `enumval` attr for a hierarchical
// PARENT carrier (`Animal.bird`, whose bits ride an attr, not the scalar), else
// the lone scalar of a leaf carrier (`Animal.mammal.rat`). nullopt when `b`
// carries no extractable encoding. Mirrors constprop's enum_scalar_of so the
// `in`-over-union fold agrees with `==`/string() on the same value.
std::optional<Dlop> enum_encoding_of(const std::shared_ptr<const Bundle>& b) {
  if (!b) {
    return std::nullopt;
  }
  if (b->has_attr("enumval") && !b->get_attr("enumval").is_invalid()) {
    return b->get_attr("enumval");
  }
  if (auto s = b->scalar(); s.has_value() && !s->is_invalid()) {
    return s;
  }
  return std::nullopt;
}

// Classify a resolved `in` operand bundle (the `a` ref's bundle, or a per-element
// `b[i]` pick). Enum identity first, then tuple shape (named top / >1 positional
// / a single sub-bundle element), then the scalar Entry kind, falling back to the
// scalar Dlop type. Returns unknown when nothing is decidable (caller skips the
// type check rather than false-flag a mismatch).
In_type classify_in_bundle(const std::shared_ptr<const Bundle>& b) {
  In_type t;
  if (!b) {
    return t;
  }
  if (auto et = bundle_enum_type(b)) {
    t.k         = In_type::K::enumv;
    t.enum_type = std::move(*et);
    return t;
  }
  if (b->has_named_top() || b->unnamed_top_count() > 1) {
    t.k = In_type::K::tuple;
    return t;
  }
  for (const auto& tl : b->top_levels()) {
    if (tl.has_leafs) {
      t.k = In_type::K::tuple;  // single element that is itself a sub-bundle
      return t;
    }
  }
  const auto& e = b->get_entry(bundle_path::of_string("0"));
  switch (e.kind) {
    case upass::Kind::boolean: t.k = In_type::K::boolean; return t;
    case upass::Kind::string:  t.k = In_type::K::string; return t;
    case upass::Kind::integer: t.k = In_type::K::integer; return t;
    case upass::Kind::enumv:   t.k = In_type::K::enumv; return t;  // identity normally caught above
    default: break;
  }
  // Declared kind unset (e.g. a freshly picked const element): derive from the
  // scalar value — the Dlop tracks bool / string distinctly from a 1-bit int,
  // so `2 in (true,false)` is still caught as int-vs-bool.
  if (auto sc = b->scalar(); sc && !sc->is_invalid()) {
    if (sc->is_string()) {
      t.k = In_type::K::string;
    } else if (sc->is_bool()) {
      t.k = In_type::K::boolean;
    } else {
      t.k = In_type::K::integer;
    }
  }
  return t;
}

// A bare const operand on the left of `in` (`5 in (…)`, `'x' in (…)`). Enums are
// always refs (Color.Red resolves to a bundle), so only int/bool/string land here.
In_type classify_in_const(std::string_view text) {
  In_type t;
  if (text == "true" || text == "false") {
    t.k = In_type::K::boolean;
  } else if (!text.empty() && (text.front() == '\'' || text.front() == '"')) {
    t.k = In_type::K::string;
  } else {
    t.k = In_type::K::integer;
  }
  return t;
}

bool in_types_compatible(const In_type& a, const In_type& b) {
  if (a.k == In_type::K::unknown || b.k == In_type::K::unknown) {
    return true;  // undecidable — defer to the emitted `==` rather than false-flag
  }
  if (a.k != b.k) {
    return false;
  }
  if (a.k == In_type::K::enumv) {
    return a.enum_type == b.enum_type;  // cross-enum membership is a type error
  }
  return true;  // both integer (any width) / boolean / string / tuple
}

bool prp_is_tmp_name(std::string_view n) { return Lnast::is_tmp(n); }

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
// prp2lnast emits a call-site instance name (`alu::[name=X](…)`) as a reserved
// `store(__inst_name, const "X")` actual; gather_actuals consumes it (never an
// argument) and try_inline uses it as the hierarchical-prefix level.
constexpr std::string_view call_inst_name_marker = "__inst_name";

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

  dispatch_stats_ = std::getenv("LIVEHD_UPASS_STATS") != nullptr;

  // dce:mark — lg-only flows skip the post-DCE staging rebuild (see
  // dead_code_eliminate_staging); tolg skips the marked statements instead.
  if (auto it = options.find("dce"); it != options.end()) {
    dce_mark_only_ = it->second == "mark";
  }

  // compile.upass.inline=false: keep fully-defined combs as Sub instances
  // (set_function_registry consults this when populating inlinable_callees_).
  if (auto it = options.find("inline"); it != options.end()) {
    const auto& v     = it->second;
    inlining_enabled_ = !(v == "false" || v == "0" || v == "no" || v == "off");
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
  ft.kind      = upass::decl_facts::io_kind_from_num(f->kind, f->range_max || f->range_min);
  ft.range_max = f->range_max;
  ft.range_min = f->range_min;
  return ft;
}

Io_kind uPass_runner::try_scalar_kind(std::string_view name) {
  // The inferred kind lattice off the binding (ex typecheck's
  // provide_scalar_kind): multi-shape → none (a tuple), else the producer-
  // stamped value kind, else the "0" Entry's declared kind.
  if (const auto b = symbol_table_.get_bundle(name); b && !(b->has_named_top() || b->unnamed_top_count() > 1)) {
    upass::Kind k = b->get_value_kind();
    if (k == upass::Kind::unknown) {
      k = b->get_entry(bundle_path::of_string("0")).kind;
    }
    switch (k) {
      case upass::Kind::integer: return Io_kind::integer;
      case upass::Kind::boolean: return Io_kind::boolean;
      case upass::Kind::string : return Io_kind::string;
      default                  : break;
    }
  }
  // Bundle had no concrete kind: fall back to the declared type_spec, same as
  // constprop's scalar_type_query_of — a `:bool`/`:string`/typed var that has
  // not been written yet still has a known scalar kind (review cat 4 #5).
  if (const auto f = upass::decl_facts::lookup(symbol_table_, lm ? lm->get_lnast().get() : nullptr, name);
      f && f->has_type_spec) {
    return upass::decl_facts::io_kind_from_num(f->kind, f->range_max || f->range_min);
  }
  return Io_kind::none;
}

Io_kind uPass_runner::actual_node_kind(const Lnast_node& node) {
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
    case upass::Mode::wire_kind : return upass::uPass::Decl_storage::wire_storage;
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

bool uPass_runner::emit_scalar_named_type_slot(std::string_view type_name) {
  if (!materialize_ || type_name.empty()) {
    return false;
  }
  auto tb = symbol_table_.get_bundle(type_name);
  if (!tb || tb->has_named_top() || tb->unnamed_top_count() > 1 || tb->get_value_kind() == upass::Kind::tuple) {
    return false;  // unresolved / a tuple-or-struct named type — keep the ref verbatim
  }
  const auto& te = tb->get_entry(bundle_path::of_string("0"));
  if (!te.decl_max.is_invalid() || !te.decl_min.is_invalid()) {
    emit_push(Lnast_ntype::create_prim_type_int());
    emit_leaf(Lnast_node::create_const(te.decl_max.is_invalid() ? "nil" : std::string(te.decl_max.to_pyrope())));
    emit_leaf(Lnast_node::create_const(te.decl_min.is_invalid() ? "nil" : std::string(te.decl_min.to_pyrope())));
    emit_pop();
    return true;
  }
  if (te.kind == upass::Kind::boolean) {
    emit_leaf(Lnast_ntype::create_prim_type_bool());
    return true;
  }
  if (te.kind == upass::Kind::string) {
    emit_leaf(Lnast_ntype::create_prim_type_string());
    return true;
  }
  return false;  // a tuple/struct named type (or a scalar with no baked range) — verbatim
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
      const bool is_type_slot = (idx == 1 && type_slot_at_1);
      const bool is_lhs       = ((idx == 0) && !fold_all) || is_type_slot;
      if (is_type_slot && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref
          && emit_scalar_named_type_slot(lm->current_text())) {
        // scalar named-type ref concretized to a prim_type — nothing else to emit
      } else if (!is_lhs && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
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
    if (dispatch_stats_) {
      const auto t0 = std::chrono::steady_clock::now();
      try {
        (entry.pass.get()->*fn)();
      } catch (const std::runtime_error& ex) {
        std::print(stderr, "uPass [{}] error: {}\n", entry.name, ex.what());
      }
      entry.stat_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
      ++entry.stat_calls;
      lm->restore_cursor(saved);
      continue;
    }
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
      if (mode != upass::Mode::reg_kind && mode != upass::Mode::mut_kind && mode != upass::Mode::wire_kind) {
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
      const auto txt = lm->current_text();
      // Per-run cache of the const's {value, Kind, pattern}. The shln-heavy parse
      // itself is owned by the Lnast (get_const_value, memoized there); this layer
      // additionally caches the upass Kind/pattern derivation and the
      // unparseable-literal fallback so neither is recomputed per dispatched node.
      auto cit = const_parse_cache_.find(txt);
      if (cit == const_parse_cache_.end()) {
        upass::Kind k = upass::Kind::unknown;
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
          v = lm->get_lnast()->get_const_value(txt);  // Lnast-memoized from_pyrope
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
        cit = const_parse_cache_.emplace(std::string(txt), Parsed_const{std::move(v), k, pattern}).first;
      }
      const auto& pc = cit->second;
      out.src.push_back(upass::Operand{std::string_view{}, Bundle::make_const(pc.value, pc.kind), pc.pattern});
    } else if (Lnast_ntype::is_ref(t)) {
      const auto name = lm->current_text();
      if (name.find('.') != std::string_view::npos) {
        // A dotted ref operand is an explicit field READ. Detupled wire/reg
        // leaves are read through plain refs (never tuple_get), so this is
        // the only place those reads are visible — constprop's
        // unset-unused-field warning consults the set.
        symbol_table_.field_touched.insert(Symbol_table::field_touch_key(lm->get_top_module_name(), name));
      }
      std::shared_ptr<Bundle> b = symbol_table_.get_bundle(name);
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
    const auto t0   = dispatch_stats_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    try {
      if ((entry.pass.get()->*fn)(rn.dst_name, *rn.dst, upass::Src_span{rn.src}) == upass::Vote::drop) {
        any_drop = true;
      }
    } catch (const std::runtime_error& e) {
      std::print(stderr, "upass pass error: {}\n", e.what());
    }
    if (dispatch_stats_) {
      entry.stat_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
      ++entry.stat_calls;
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
      // Bw soundness check: a comptime-known value must lie WITHIN its derived
      // range. When it provably does not (e.g. a negative literal stamped onto
      // an unsigned-typed slot, `o:u4 = -1`), that is a USER bitwidth error, not
      // a compiler bug — it is reachable straight from source. Report it once
      // (skip if an upstream pass already flagged the design) and let the run
      // fail cleanly instead of tripping a hard invariant abort.
      {
        const Bundle::Entry& e0 = now->get_entry(bundle_path::of_string("0"));
        if (!e0.trivial.is_invalid() && e0.trivial.is_integer() && !e0.trivial.has_unknowns() && !e0.bw_max.is_invalid()
            && !e0.bw_min.is_invalid()) {
          const bool over  = e0.trivial.gt_op(e0.bw_max)->is_known_true();
          const bool under = e0.trivial.lt_op(e0.bw_min)->is_known_true();
          if ((over || under) && !livehd::diag::sink().has_errors()) {
            livehd::diag::sink().emit(livehd::diag::Diagnostic{
                .severity = livehd::diag::Severity::error,
                .code     = "bitwidth-overflow",
                .category = "bitwidth",
                .pass     = "upass.runner",
                .message  = std::format("`{}` (value {}) does not fit its declared range [{}, {}]",
                                        rn.dst_name,
                                        e0.trivial.to_decimal_string(),
                                        e0.bw_min.to_decimal_string(),
                                        e0.bw_max.to_decimal_string()),
                .span     = lm->current_span(),
                .hint     = "widen the declared type, force fewer bits with a bit-select, or adjust the value",
            });
          }
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
    // READ-ONLY probe first: never clone just to poll. This stash is drained
    // once per dispatched node, and the COW unshare inside get_bundle_for_write
    // (the old probe) cloned the whole root bundle on every poll — O(N^2) on a
    // large module. A null root binding means the field has no live write-scope
    // (its declaring scope was left): skip; the entry is dropped at the root's
    // scope exit (Symbol_table::leave_scope) so the stash stays bounded.
    const Bundle* rb_ro = symbol_table_.peek_writable_bundle(root);
    if (rb_ro == nullptr || !rb_ro->has_trivial(bundle_path::of_string(fpath))) {
      ++it;
      continue;
    }
    // Field materialized → apply. Unshare for the write only now (rare path).
    auto          rb = symbol_table_.get_bundle_for_write(root);
    const auto&   pf = it->second;
    Bundle::Entry fe = rb->get_entry(bundle_path::of_string(fpath));
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
    rb->set(bundle_path::of_string(fpath), std::move(fe));
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
    if (!bound.has_trivial(bundle_path::of_string(key))) {
      continue;  // facts ride only onto entries the binding actually has
    }
    if (key == "0" && !bound_scalar) {
      continue;
    }
    Bundle::Entry e   = bound.get_entry(bundle_path::of_string(key));
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
      bound.set(bundle_path::of_string(key), std::move(e));
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
  // task 2n Phase B: record the just-defined variable into the LSP semantic
  // index, now that this op's bitwidth/kind facts are written. Gated on the
  // LSP-only global flag, so the normal CLI never enters record_lsp_def.
  if (livehd::lsp_index::index().enabled()) {
    record_lsp_def(rn.dst_name);
  }
  if (!vote_drop && !any_pass_drops()) {
    emit_op_with_fold(fold_all);
  }
}

namespace {

// task 2n — the scalar type of ONE bundle leaf for the LSP hover, from the
// Entry's own facts alone: kind (bool/string/enum), else integer with bits
// recomputed from the declared envelope and the bw_min/bw_max (or comptime
// value) range labeled the same way a plain variable renders. Used for tuple
// FIELDS, where the io_meta/bw_meta side-channels don't apply (write_bw skips
// dotted names — the per-field range lives on the bundle Entry itself).
std::string lsp_render_leaf_type(const Bundle::Entry& fe) {
  switch (fe.kind) {
    case upass::Kind::boolean: return "bool";
    case upass::Kind::string : return "string";
    case upass::Kind::enumv  : return "enum";
    case upass::Kind::tuple  : return "tuple";
    default                  : break;
  }
  if (!fe.trivial.is_invalid() && fe.trivial.is_string()) {
    return "string";
  }
  if (!fe.trivial.is_invalid() && fe.trivial.is_bool()) {
    return "bool";
  }
  const auto to_i64 = [](const Dlop& v) -> std::optional<int64_t> {
    if (v.is_invalid() || !v.is_integer() || v.has_unknowns() || v.get_bits() > 62) {
      return std::nullopt;
    }
    return v.to_just_i64();
  };
  const auto ubits = [](int64_t hi) -> int { return hi <= 0 ? 0 : static_cast<int>(std::bit_width(static_cast<uint64_t>(hi))); };
  const auto sbits = [](int64_t lo, int64_t hi) -> int {
    const auto sb = [](int64_t v) {
      return v >= 0 ? static_cast<int>(std::bit_width(static_cast<uint64_t>(v))) + 1
                    : static_cast<int>(std::bit_width(static_cast<uint64_t>(-v - 1))) + 1;
    };
    return std::max(sb(lo), sb(hi));
  };

  // Declared WIDTH from the (max, min) type Consts. get_bits() is the signed
  // width; an unsigned envelope (min >= 0) drops the sign bit. Unlike to_i64
  // this handles >62-bit types (u64/u128) whose bounds don't fit int64, so the
  // width still renders instead of collapsing to `int`.
  bool       dsgn  = false;
  const auto dbits = [&]() -> std::optional<int> {
    const auto& dmax = fe.decl_max;
    const auto& dmin = fe.decl_min;
    if ((dmax.is_invalid() || !dmax.is_integer()) && (dmin.is_invalid() || !dmin.is_integer())) {
      return std::nullopt;
    }
    dsgn          = !dmin.is_invalid() && dmin.is_integer() && dmin.is_negative();
    const auto bw = [](const Dlop& v) -> int { return (v.is_invalid() || !v.is_integer()) ? 0 : v.get_bits(); };
    const int  b  = dsgn ? std::max(bw(dmax), bw(dmin)) : (dmax.is_known_zero() ? 0 : bw(dmax) - 1);
    return b > 0 ? std::optional<int>(b) : std::nullopt;
  }();
  const auto dlo = to_i64(fe.decl_min);
  const auto dhi = to_i64(fe.decl_max);
  auto       lo  = to_i64(fe.bw_min);
  auto       hi  = to_i64(fe.bw_max);
  if (!lo || !hi) {  // no derived range: a comptime value is its own point range
    if (const auto v = to_i64(fe.trivial); v) {
      lo = v;
      hi = v;
    }
  }
  const bool have_decl_i64 = dlo && dhi;
  if (!dbits && (!lo || !hi) && !have_decl_i64) {
    return "int";  // no declared width and no derived/point range
  }
  bool sgn;
  int  bits;
  if (dbits) {
    sgn  = dsgn;
    bits = *dbits;
  } else if (have_decl_i64) {
    sgn  = *dlo < 0;
    bits = sgn ? sbits(*dlo, *dhi) : ubits(*dhi);
  } else {
    sgn  = *lo < 0;
    bits = sgn ? sbits(*lo, *hi) : ubits(*hi);
  }
  std::string r(1, sgn ? 's' : 'u');
  r += std::to_string(bits);
  // Show bw_min/bw_max ONLY when the value is strictly NARROWER than the
  // declared full range — a full-range value adds no info, and wide-type
  // bounds don't fit int64 anyway. With no declared envelope the derived
  // range IS the only bound, so show it.
  if (lo && hi && (!have_decl_i64 || *lo > *dlo || *hi < *dhi)) {
    r += "(bw_min=";
    r += std::to_string(*lo);
    r += ", bw_max=";
    r += std::to_string(*hi);
    r += ')';
  }
  return r;
}

// LSP-side stash of declared type/mode read straight off `declare` nodes
// during the walk (filled only when the lsp index is enabled). A reg field's
// declare (`bank.x` from a struct reg) reaches bake_decl_pre_step before its
// root binding exists, so the compiler path DROPS the facts (regs are runtime;
// tolg re-reads the type subtree itself) — this keeps a render-only copy so
// hover still shows u32/s8 + the reg mode. Keyed by dotted dst name, cleared
// per runner run; never fed back into the symbol table (no semantic effect).
absl::flat_hash_map<std::string, Symbol_table::Pending_decl>& lsp_decl_hints() {
  static absl::flat_hash_map<std::string, Symbol_table::Pending_decl> m;
  return m;
}

// Overlay for declared facts that never landed on the bundle entry: first the
// compiler's own pending stash (a field declared before its first write), then
// the LSP-side declare hints above. Applied at render time only.
Bundle::Entry lsp_overlay_pending(Bundle::Entry fe, const Symbol_table& st, const std::string& key) {
  const auto apply = [&fe](const Symbol_table::Pending_decl& pf) {
    if (fe.kind == upass::Kind::unknown) {
      fe.kind = pf.kind;
    }
    if (fe.decl_max.is_invalid()) {
      fe.decl_max = pf.decl_max;
    }
    if (fe.decl_min.is_invalid()) {
      fe.decl_min = pf.decl_min;
    }
  };
  if (const auto it = st.pending_decl_facts.find(key); it != st.pending_decl_facts.end()) {
    apply(it->second);
  }
  if (const auto it = lsp_decl_hints().find(key); it != lsp_decl_hints().end()) {
    apply(it->second);
  }
  return fe;
}

// task 2n — a whole tuple for the LSP hover: each leaf with its scalar render,
// `tuple(x: u4(bw_min=1, bw_max=1), y: string, …)`. non_attr_entries flattens
// nested sub-bundles into dotted keys in canonical order; long tuples truncate
// so the hover stays one-glance readable. `root` prefixes each leaf key for
// the pending-facts overlay lookup.
std::string lsp_render_tuple(const Bundle& b, const Symbol_table& st, std::string_view root) {
  constexpr size_t kMaxFields = 8;
  const auto       leaves     = b.non_attr_entries();
  std::string      r          = "tuple(";
  size_t           shown      = 0;
  for (const auto& [key, fe] : leaves) {
    if (shown == kMaxFields) {
      r += ", …+";
      r += std::to_string(leaves.size() - kMaxFields);
      break;
    }
    if (shown != 0) {
      r += ", ";
    }
    std::string pkey(root);
    pkey += '.';
    pkey += key;
    r    += key;
    r    += ": ";
    r    += lsp_render_leaf_type(lsp_overlay_pending(fe, st, pkey));
    ++shown;
  }
  r += ')';
  return r;
}

}  // namespace

// task 2n Phase B — see the header. Statement-granularity span (operand refs
// carry no SourceId; the enclosing op does), so selectionRange == range.
void uPass_runner::record_lsp_def(std::string_view dst_name) {
  if (dst_name.empty() || Lnast::is_tmp(dst_name)) {
    return;  // throwaway dst or a compiler temporary: no user-visible name
  }
  const auto nid = lm->get_current_nid();
  const auto sp  = lm->get_lnast()->span_of(nid);
  if (!sp.start_line) {
    return;  // synthesized / inlined node with no resolvable source location
  }

  // Source-level display name: strip the SSA suffix (x___ssa_2 -> x).
  std::string_view base = dst_name;
  if (const auto pos = base.find("___ssa_"); pos != std::string_view::npos) {
    base = base.substr(0, pos);
  }
  if (base.empty() || Lnast::is_tmp(base)) {
    return;
  }

  // Dlop const -> int64 (mirrors uPass_bitwidth::const_to_i64).
  const auto to_i64 = [](const Dlop& v) -> std::optional<int64_t> {
    if (v.is_invalid() || !v.is_integer() || v.has_unknowns() || v.get_bits() > 62) {
      return std::nullopt;
    }
    return v.to_just_i64();
  };
  // Minimal bit width to hold a range, matching upass_bitwidth's storage rule.
  const auto ubits = [](int64_t hi) -> int {
    return hi <= 0 ? 0 : static_cast<int>(std::bit_width(static_cast<uint64_t>(hi)));
  };
  const auto sbits = [](int64_t lo, int64_t hi) -> int {
    const auto sb = [](int64_t v) {
      return v >= 0 ? static_cast<int>(std::bit_width(static_cast<uint64_t>(v))) + 1
                    : static_cast<int>(std::bit_width(static_cast<uint64_t>(-v - 1))) + 1;
    };
    return std::max(sb(lo), sb(hi));
  };

  // The live bundle drives BOTH the kind shown (bool/string/enum/tuple/
  // import/function) and the declared envelope of a non-IO integer. May be
  // null (e.g. an IO store with no symbol-table binding yet).
  const auto bun = symbol_table_.get_bundle(Bundle::get_first_level(dst_name));

  // Declared type/envelope. IO leaves carry it on the Lnast's io_meta
  // side-channel (harvested by the SSA upass): the authoritative declared bits +
  // sign + value range. For an internal (non-IO) declared var, fall back to the
  // live bundle's declared envelope (decl_min/decl_max).
  std::optional<int>     dbits;            // declared bit width (prefix)
  bool                   dsigned = false;  // declared sign
  std::optional<int64_t> dlo;              // declared value envelope
  std::optional<int64_t> dhi;
  Io_kind                io_kind = Io_kind::none;  // declared bool/string on an IO leaf
  // Derive the declared width + i64 envelope from a type's (max, min) Consts.
  // get_bits() handles >62-bit types (u64/u128) whose bounds don't fit int64,
  // so dbits is set even when to_i64 (dlo/dhi) cannot represent the bound.
  const auto apply_decl = [&](const Dlop& dmax, const Dlop& dmin) {
    if ((dmax.is_invalid() || !dmax.is_integer()) && (dmin.is_invalid() || !dmin.is_integer())) {
      return;
    }
    dsigned       = !dmin.is_invalid() && dmin.is_integer() && dmin.is_negative();
    const auto bw = [](const Dlop& v) -> int { return (v.is_invalid() || !v.is_integer()) ? 0 : v.get_bits(); };
    const int  b  = dsigned ? std::max(bw(dmax), bw(dmin)) : (dmax.is_known_zero() ? 0 : bw(dmax) - 1);
    if (b > 0) {
      dbits = b;
    }
    dlo = to_i64(dmin);
    dhi = to_i64(dmax);
  };
  {
    const auto&                  io    = lm->get_lnast()->io_meta();
    const Lnast_io_entry*        found = nullptr;
    for (const auto& e : io.outputs) {
      if (std::string_view(e.name) == base) {
        found = &e;
        break;
      }
    }
    if (found == nullptr) {
      for (const auto& e : io.inputs) {
        if (std::string_view(e.name) == base) {
          found = &e;
          break;
        }
      }
    }
    if (found != nullptr) {
      dsigned = found->is_signed;
      io_kind = found->kind;
      if (found->bits > 0) {
        dbits = found->bits;
      }
      if (found->has_range) {
        dlo = found->range_min;
        dhi = found->range_max;
      } else if (found->bits > 0 && found->bits < 63) {
        if (dsigned) {
          dhi = (int64_t{1} << (found->bits - 1)) - 1;
          dlo = -(int64_t{1} << (found->bits - 1));
        } else {
          dhi = (int64_t{1} << found->bits) - 1;
          dlo = 0;
        }
      }
    } else if (bun) {
      const auto& e = bun->get_entry(bundle_path::of_string("0"));
      apply_decl(e.decl_max, e.decl_min);
    }
  }

  // A DOTTED field whose root container is NOT a live tuple binding (a wire/reg
  // struct field: the field TYPE was stashed in lsp_decl_hints / pending, but
  // no `io` bundle ever bound). Without this the field falls through to a bare
  // `int` — recover its declared width from the stash. Integer width via
  // apply_decl; a bool/string field carries its kind through io_kind below.
  if (!dbits && !dlo && !dhi && !Bundle::get_all_but_first_level(base).empty()) {
    const auto pull = [&](const Symbol_table::Pending_decl& pf) {
      apply_decl(pf.decl_max, pf.decl_min);
      if (io_kind == Io_kind::none && pf.kind == upass::Kind::boolean) {
        io_kind = Io_kind::boolean;
      } else if (io_kind == Io_kind::none && pf.kind == upass::Kind::string) {
        io_kind = Io_kind::string;
      }
    };
    const std::string base_s(base);
    if (const auto it = symbol_table_.pending_decl_facts.find(base_s); it != symbol_table_.pending_decl_facts.end()) {
      pull(it->second);
    }
    if (!dbits && !dlo && !dhi) {
      if (const auto it = lsp_decl_hints().find(base_s); it != lsp_decl_hints().end()) {
        pull(it->second);
      }
    }
  }

  // Inferred range live at this definition (per SSA version), from the bitwidth
  // side-channel keyed by the post-SSA name. Unbounded for an unconstrained
  // value (e.g. a raw input), in which case the declared envelope is shown.
  std::optional<int64_t> ilo;
  std::optional<int64_t> ihi;
  const auto&            ranges = lm->get_lnast()->bw_meta().ranges;
  if (const auto it = ranges.find(std::string(dst_name)); it != ranges.end() && !it->second.unbounded) {
    ilo = it->second.min;
    ihi = it->second.max;
  }

  // Scalar kind: typecheck's stamps (bundle value_kind, then the "0" Entry
  // kind), then the comptime Dlop, then the declared IO kind. Kind is carried
  // explicitly — a runtime bool (`a<b`) has no comptime Dlop and `true` reads
  // as 1, so the Dlop alone cannot classify (see upass/core/kind.hpp).
  upass::Kind kind = upass::Kind::unknown;
  if (bun) {
    kind = bun->get_value_kind();
    if (kind == upass::Kind::unknown) {
      kind = bun->get_entry(bundle_path::of_string("0")).kind;
    }
    if (kind == upass::Kind::unknown) {
      if (const auto sc = bun->scalar(); sc && !sc->is_invalid()) {
        if (sc->is_string()) {
          kind = upass::Kind::string;
        } else if (sc->is_bool()) {
          kind = upass::Kind::boolean;
        }
      }
    }
  }
  if (kind == upass::Kind::unknown && io_kind == Io_kind::boolean) {
    kind = upass::Kind::boolean;
  }
  if (kind == upass::Kind::unknown && io_kind == Io_kind::string) {
    kind = upass::Kind::string;
  }

  // Tuple shape: named top / >1 positional / a single element that is itself a
  // sub-bundle (mirrors classify_in_bundle / uPass_typecheck::kind_of_bundle).
  bool is_tuple = false;
  if (bun && (bun->has_named_top() || bun->unnamed_top_count() > 1)) {
    is_tuple = true;
  } else if (bun) {
    for (const auto& tl : bun->top_levels()) {
      if (tl.has_leafs) {
        is_tuple = true;
        break;
      }
    }
  }

  // An enum TYPE bundle (`const Color = enum(...)`) carries per-member
  // `<Member>.enumentry` tags — a NAMED prefix, so it is not the bundle's own
  // identity and bundle_enum_type below skips it (that helper answers only for
  // enum VALUES).
  bool is_enum_type = false;
  if (bun) {
    for (const auto& [k, ep] : bun->get_attrs()) {
      if (Bundle::get_last_level(k) == battr::enumentry) {
        is_enum_type = true;
        break;
      }
    }
  }

  const auto unquote = [](std::string s) {
    if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') && s.back() == s.front()) {
      s = s.substr(1, s.size() - 2);
    }
    return s;
  };

  const auto emit = [&](std::string_view nm, std::string rend) {
    livehd::lsp_index::Entry e;
    e.start_line = sp.start_line ? *sp.start_line : 0;
    e.start_col  = sp.start_col ? *sp.start_col : 0;
    e.end_line   = sp.end_line ? *sp.end_line : e.start_line;
    e.end_col    = sp.end_col ? *sp.end_col : e.start_col;
    e.file       = sp.file;
    e.name       = std::string(nm);
    e.render     = std::move(rend);
    livehd::lsp_index::index().record(std::move(e));
  };

  std::string reg_pfx;
  if (bun && bun->get_mode() == upass::Mode::reg_kind) {
    reg_pfx = "reg ";
  }

  // A DOTTED dst (`bank.x = …`, or the per-field declares detuple splits a
  // struct reg into) defines one FIELD of a container bundle: render that
  // field's own type, and refresh the container's whole-tuple entry so the
  // container name stays hoverable (nothing ever defines bare `bank`).
  if (const auto sub = Bundle::get_all_but_first_level(base); bun && !sub.empty()) {
    const std::string base_s(base);
    if (reg_pfx.empty()) {  // a reg field's mode only exists in the declare hints
      if (const auto it = lsp_decl_hints().find(base_s); it != lsp_decl_hints().end() && it->second.mode == upass::Mode::reg_kind) {
        reg_pfx = "reg ";
      }
    }
    std::string render(base);
    render        += " : ";
    render        += reg_pfx;
    bool rendered  = false;
    if (const auto sb = bun->get_bundle(bundle_path::of_string(sub)); sb) {
      const auto leaves = sb->non_attr_entries();
      if (leaves.size() == 1 && leaves.begin()->first == "0") {
        // a lone positional leaf is the field's scalar, not a nested tuple
        render += lsp_render_leaf_type(lsp_overlay_pending(leaves.begin()->second, symbol_table_, base_s));
      } else {
        render += lsp_render_tuple(*sb, symbol_table_, base_s);
      }
      rendered = true;
    }
    if (!rendered) {
      render += lsp_render_leaf_type(lsp_overlay_pending(bun->get_entry(bundle_path::of_string(sub)), symbol_table_, base_s));
    }
    emit(base, std::move(render));
    const auto  first = Bundle::get_first_level(base);
    std::string crend(first);
    crend += " : ";
    crend += reg_pfx;
    crend += lsp_render_tuple(*bun, symbol_table_, first);
    emit(first, std::move(crend));
    return;
  }

  std::string render(base);
  render += " : ";
  render += reg_pfx;
  if (bun && bun->has_attr("pub_unit")) {
    // `X = import("unit")` whole-namespace binding (call_resolver marker attr).
    render += "import(\"";
    render += unquote(std::string(bun->get_attr("pub_unit").to_pyrope()));
    render += "\")";
  } else if (const auto et = bundle_enum_type(bun); et) {
    render += "enum ";
    render += *et;
  } else if (is_enum_type) {
    render += "enum";
  } else if (is_tuple) {
    render += lsp_render_tuple(*bun, symbol_table_, base);
  } else if (kind == upass::Kind::boolean) {
    render += "bool";
  } else if (kind == upass::Kind::string) {
    // A callee-name string ('unit.entity', how constprop folds an imported or
    // aliased lambda) is a function value, not text — show it as one. Gate the
    // registry lookup on the qualified-identifier shape: lookup_callee's miss
    // path is a linear registry scan, and paying it for every text-string def
    // would make this (LSP-only) walk super-linear in the import closure.
    std::string txt;
    if (const auto sc = bun ? bun->scalar() : std::optional<Dlop>{}; sc && !sc->is_invalid() && sc->is_string()) {
      txt = unquote(std::string(sc->to_pyrope()));
    }
    bool callee_shape = txt.find('.') != std::string::npos;
    for (const char c : txt) {
      callee_shape = callee_shape
                     && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.');
    }
    if (const auto fn = callee_shape ? lookup_callee(txt) : nullptr; fn) {
      const auto lk  = fn->get_lambda_kind();
      render        += lk.empty() ? std::string_view("fun") : lk;
      render        += ' ';
      render        += txt;
    } else {
      render += "string";
    }
  } else {
    const bool have_decl = dlo && dhi;  // declared envelope fits int64
    const bool have_inf  = ilo && ihi;  // inferred range known
    if (!have_decl && !have_inf && !dbits) {
      render += "int";  // no declared width and no derived range
    } else {
      // Width prefix = the DECLARED width when known (dbits — handles u64/u128
      // whose bounds don't fit int64), else derived from the i64 envelope/range.
      const bool sgn = dbits ? dsigned : (have_decl ? dsigned : (*ilo < 0));
      int        bits;
      if (dbits) {
        bits = *dbits;
      } else if (have_decl) {
        bits = dsigned ? sbits(*dlo, *dhi) : ubits(*dhi);
      } else {
        bits = (*ilo < 0) ? sbits(*ilo, *ihi) : ubits(*ihi);
      }
      render += sgn ? 's' : 'u';
      render += std::to_string(bits);
      // Show bw_min/bw_max ONLY when the inferred range is strictly NARROWER
      // than the declared full range (`z:u8` narrowed to 0..15 →
      // u8(bw_min=0, bw_max=15); a full-range `z:u8` → just u8). With no
      // declared envelope the inferred range IS the only bound, so show it.
      if (have_inf && (!have_decl || *ilo > *dlo || *ihi < *dhi)) {
        render += "(bw_min=";
        render += std::to_string(*ilo);
        render += ", bw_max=";
        render += std::to_string(*ihi);
        render += ')';
      }
    }
  }

  emit(base, std::move(render));
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

const uPass_function_registry& uPass_runner::reg() const {
  // A runner that never had a registry pointed at it (e.g. the bitwidth-only
  // runner) sees an immortal empty registry — no inlining, no crashes.
  static const uPass_function_registry empty;
  return registry_ != nullptr ? *registry_ : empty;
}

std::shared_ptr<Lnast> uPass_function_registry::lookup_callee(std::string_view name) const {
  return upass::call_resolver::lookup_callee(function_registry, name);
}

void uPass_function_registry::ensure(const std::vector<std::shared_ptr<Lnast>>& lnasts) {
  if (lnasts.size() == built_count) {
    return;  // nothing new since the last fold-in — O(1) fast path
  }

  // ── Phase 1: fold in each NEW lnast, walking its body EXACTLY ONCE. Every
  // fact computed here is purely local to the one body, so it is cached and the
  // body is never re-walked — even as runner-spawned specializations grow
  // var.lnasts. (The recursion-dependent decision is made globally in phase 2.)
  for (std::size_t i = built_count; i < lnasts.size(); ++i) {
    const auto& ln = lnasts[i];
    if (!ln) {
      continue;
    }
    std::string name(ln->get_top_module_name());
    if (!function_registry.emplace(name, ln).second) {
      continue;  // duplicate name — the first body wins (as the old rebuild did)
    }

    // A pre-elaborated import is a black box: registered above so call sites bind
    // to it (via its restored io_meta), but never inlined and its internal call
    // graph is irrelevant to this compile. Skip the full body walk — it is the
    // dominant per-import cost on a large design (the loaded bodies are big).
    if (ln->is_pre_elaborated()) {
      facts.emplace(std::move(name), Lnast_facts{});
      continue;
    }

    Lnast_facts f;

    // (a) Call-graph out-edges — the raw callee names referenced by func_calls.
    // Resolution to a registered module is deferred to phase 2 so a forward
    // reference (callee folded in by a later ensure) still resolves, matching
    // the old full-registry rebuild.
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
      f.callee_names.emplace_back(ln->get_name(c1));
    }

    // (b) Inlinability classification. Phase C supports: a real signature, every
    // declared output written by name, and no positional-placeholder params /
    // implicit returns. Multi-output is allowed (epilogue splats a bundle);
    // recursion is allowed (gated at the call site on constant args). The dead
    // tuple_dsts/tuple_param/ref_aliases bookkeeping the old code computed then
    // `(void)`'d is dropped here — it also shed a pile of get_name calls.
    const auto& io        = ln->io_meta();
    auto        reg_stmts = ln->get_first_child(ln->get_root());
    if (reg_stmts.is_valid() && ln->get_type(reg_stmts) == Lnast_ntype::Lnast_ntype_io) {
      reg_stmts = ln->get_sibling_next(reg_stmts);
    }
    if (!reg_stmts.is_valid() || !ln->get_first_child(reg_stmts).is_valid()) {
      facts.emplace(std::move(name), std::move(f));
      continue;  // no body to inline
    }
    // A zero-output comb is only worth splicing with an observable side effect:
    // a cassert/cputs OR a mutated `ref` param. Requiring one avoids mis-inlining
    // an implicit-return comb whose result io_meta doesn't capture as a declared
    // output (named_tuple.prp), binding nothing back.
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
        facts.emplace(std::move(name), std::move(f));
        continue;  // pure / implicit-return zero-output comb — leave as a call
      }
    }
    // Scan the body STMTS only (skip the io node — its `-> (a,b)` output decl is
    // itself a tuple node). We need just `defined` (written dsts) + whether a
    // positional placeholder appears.
    auto stmts_nid = ln->get_first_child(ln->get_root());
    if (stmts_nid.is_valid() && ln->get_type(stmts_nid) == Lnast_ntype::Lnast_ntype_io) {
      stmts_nid = ln->get_sibling_next(stmts_nid);
    }
    absl::flat_hash_set<std::string> defined;
    if (stmts_nid.is_valid()) {
      for (const auto& nid : ln->depth_preorder(stmts_nid)) {
        if (nid.is_invalid()) {
          continue;
        }
        auto fc = ln->get_first_child(nid);
        if (fc.is_valid() && ln->get_type(fc) == Lnast_ntype::Lnast_ntype_ref) {
          defined.insert(std::string(ln->get_name(fc)));
        }
      }
    }
    bool all_outputs_written = true;
    for (const auto& o : io.outputs) {
      // A tuple-typed output is flattened to leaves `p.first`/`p.second`, but the
      // body writes the LOGICAL output `p`. Accept the leaf OR its pre-dot prefix,
      // but only when the leaf IS dotted (a genuine flattened tuple output).
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
    if (all_outputs_written) {
      f.inlinable = true;
      // sub-convertible candidate (subset of inlinable): a fully-typed,
      // pure-dataflow comb with its own standalone GraphIO. Excludes templates,
      // var-arg / `ref` params, zero-output side-effect combs. The recursion
      // exclusion is applied in phase 2.
      const auto lk      = ln->get_lambda_kind();
      const bool is_comb = lk.empty() || lk == "comb";
      bool       special = false;
      for (const auto& e : io.inputs) {
        // A defaulted input (todo 3g E) has no standalone-module form — its
        // body-prologue default binding is only correct on the inline path (the
        // provided-arg skip runs there), and an expression default like
        // `b=a+5` cannot be a static port default at all. Force inline.
        if (e.is_ref || e.is_varargs || e.has_default) {
          special = true;
          break;
        }
      }
      f.sub_candidate = is_comb && !ln->is_template() && !io.outputs.empty() && !special;
    }
    facts.emplace(std::move(name), std::move(f));
  }
  built_count = lnasts.size();

  // ── Phase 2: tree-walk-free. Resolve the cached out-edges against the FULL
  // registry, run the recursion closure, then derive the global sets. The old
  // inliner bailed self-reaching callees to the evaluator (it fully unrolls
  // comptime-bounded recursion). Cheap enough to redo wholesale on each growth.
  recursive_callees.clear();
  absl::flat_hash_map<std::string, std::vector<std::string>> edges;
  for (const auto& [name, f] : facts) {
    for (const auto& cn : f.callee_names) {
      if (auto tgt = lookup_callee(cn)) {
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
        recursive_callees.insert(name);
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

  inlinable_callees.clear();
  sub_convertible_combs.clear();
  for (const auto& [name, f] : facts) {
    if (f.inlinable) {
      inlinable_callees.insert(name);
    }
    if (f.sub_candidate && !recursive_callees.contains(name)) {
      sub_convertible_combs.insert(name);
    }
  }
}

std::shared_ptr<Lnast> uPass_runner::lookup_callee(std::string_view name) const {
  // Delegated to the resolver (name resolution is its job).
  return upass::call_resolver::lookup_callee(reg().function_registry, name);
}

void uPass_runner::flush_deferred_emits() { dispatch_to_passes(&upass::uPass::flush_deferred); }

void uPass_runner::emit_inline_binding(const std::string& lhs, const Lnast_node& rhs) {
  // A synthesized `lhs = nil` seed is exempt from typecheck's
  // nil-does-not-infer-tuple-shape rule (the body/epilogue legally binds a
  // tuple over it). The mark also propagates along the epilogue alias chain
  // (`hs = inl1_r` where inl1_r is a runtime-valued output that stayed nil-
  // seeded): the LHS is then equally a runtime-placeholder nil, so constprop's
  // nil-operand check skips it (it is not a genuine illegal nil).
  if ((rhs.is_const() && rhs.get_name() == "nil")
      || (rhs.is_ref() && symbol_table_.nil_seeded.contains(std::string(rhs.get_name())))) {
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

void uPass_runner::emit_inline_attr(const std::string& target, std::string_view key, const std::string& value) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-attr");
  auto s    = std::make_shared<Lnast>(body, "inl-attr");
  auto root = s->set_root(Lnast_ntype::create_attr_set());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(target));  // literal — already frame-renamed
  s->add_child(root, Lnast_node::create_const(key));
  s->add_child(root, Lnast_node::create_const(value));
  flush_deferred_emits();
  lm->push_source(s, "", 0);  // literal names; no rename
  process_lnast();            // cursor at attr_set root → C_OP(attr_set): dispatch + emit
  flush_deferred_emits();
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
  // Emit the canonical prim_type_int(max,min) from (bits, signed). bits>0 here
  // (early return above), so just split on signedness via the shared helpers.
  auto       pt = s->add_child(root, Lnast_ntype::create_prim_type_int());
  const auto ub = static_cast<uint32_t>(bits);
  s->add_child(pt, Lnast_node::create_const(std::string(upass::max_from_bits(ub, is_signed).to_pyrope())));
  s->add_child(pt, Lnast_node::create_const(std::string(upass::min_from_bits(ub, is_signed).to_pyrope())));
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

void uPass_runner::emit_inline_get_mask(const std::string& dst, const Lnast_node& value, const std::string& mask_text) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-getmask");
  auto s    = std::make_shared<Lnast>(body, "inl-getmask");
  auto root = s->set_root(Lnast_ntype::create_get_mask());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));     // dst
  s->add_child(root, value);                           // value
  s->add_child(root, Lnast_node::create_const(mask_text));  // const bitmask
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at get_mask root → push path: dispatch + emit
  flush_deferred_emits();
  lm->pop_source();
}

void uPass_runner::emit_inline_to_bool(const std::string& dst, const Lnast_node& value) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-tobool");
  auto s    = std::make_shared<Lnast>(body, "inl-tobool");
  auto root = s->set_root(Lnast_ntype::create_ne());  // dst = (value != 0)
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));
  s->add_child(root, value);
  s->add_child(root, Lnast_node::create_const("0"));
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at ne root → dispatch + emit (typecheck stamps bool)
  flush_deferred_emits();
  lm->pop_source();
}

void uPass_runner::emit_staging_op(Lnast_ntype::Lnast_ntype_int op, const std::string& dst,
                                   const std::vector<Lnast_node>& operands) {
  emit_push(op);
  emit_leaf(Lnast_node::create_ref(dst));  // dst
  for (const auto& o : operands) {
    emit_leaf(o);
  }
  emit_pop();
}

void uPass_runner::emit_staging_guarded_store(const std::string& cond, const std::string& dst, const Lnast_node& value) {
  // if(cond) { dst = value } — built straight into staging.
  emit_push(Lnast_ntype::create_if());
  emit_leaf(Lnast_node::create_ref(cond));        // cond
  emit_push(Lnast_ntype::create_stmts());         // then arm
  emit_push(Lnast_ntype::create_store());         // dst = value
  emit_leaf(Lnast_node::create_ref(dst));
  emit_leaf(value);
  emit_pop();  // store
  emit_pop();  // stmts
  emit_pop();  // if
}

void uPass_runner::emit_inline_positional_tuple(const std::string& dst, const std::vector<Lnast_node>& children) {
  // `dst = (c0, c1, …)` — positional tuple_add (no store-wrapped field keys),
  // so tolg's memory-init path (which requires `named` empty) can consume it.
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-ptup");
  auto s    = std::make_shared<Lnast>(body, "inl-ptup");
  auto root = s->set_root(Lnast_ntype::create_tuple_add());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));
  for (const auto& c : children) {
    s->add_child(root, c);
  }
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at tuple_add root → dispatch + emit
  flush_deferred_emits();
  lm->pop_source();
}

std::string uPass_runner::materialize_array_literal(const std::vector<int64_t>& dims, size_t level,
                                                    const std::vector<Dlop>& flat, size_t start) {
  // Outer dim first (dims[0]); inner tuples are emitted BEFORE the outer so
  // they are recorded (tuple_recs_) and staged ahead of the reference to them.
  const std::string name = "%marrayinit_" + std::to_string(inline_seq_) + "_" + std::to_string(level) + "_"
                           + std::to_string(start);
  std::vector<Lnast_node> children;
  if (level + 1 == dims.size()) {
    for (int64_t k = 0; k < dims[level]; ++k) {
      children.emplace_back(Lnast_node::create_const(std::string(flat[start + static_cast<size_t>(k)].to_pyrope())));
    }
  } else {
    int64_t stride = 1;
    for (size_t l = level + 1; l < dims.size(); ++l) {
      stride *= dims[l];
    }
    for (int64_t k = 0; k < dims[level]; ++k) {
      auto child = materialize_array_literal(dims, level + 1, flat, start + static_cast<size_t>(k * stride));
      children.emplace_back(Lnast_node::create_ref(child));
    }
  }
  emit_inline_positional_tuple(name, children);
  return name;
}

bool uPass_runner::try_materialize_array_init() {
  if (!materialize_ || !lm->has_child()) {
    return false;
  }
  // Read the declare shape without disturbing the outer cursor.
  // Layout: declare(ref name, <type>, const mode, [init]).
  const auto saved = lm->save_cursor();
  lm->move_to_child();  // child0 = name
  if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return false;
  }
  const std::string name{lm->current_text()};
  if (!lm->move_to_sibling()) {  // child1 = type
    lm->restore_cursor(saved);
    return false;
  }
  const auto type_ntype   = lm->get_raw_ntype();
  const bool type_is_array = Lnast_ntype::is_comp_type_array(type_ntype);
  const bool type_is_none  = Lnast_ntype::is_prim_type_none(type_ntype);
  const auto type_nid     = lm->get_current_nid();
  if (!lm->move_to_sibling() || !Lnast_ntype::is_const(lm->get_raw_ntype())) {  // child2 = mode
    lm->restore_cursor(saved);
    return false;
  }
  const std::string mode{lm->current_text()};
  if (!lm->move_to_sibling() || !Lnast_ntype::is_ref(lm->get_raw_ntype())) {  // child3 = init (ref)
    lm->restore_cursor(saved);
    return false;
  }
  const std::string init_ref{lm->current_text()};
  lm->restore_cursor(saved);

  // Scope: reg memories only (a mut/const array inits via a separate
  // whole-array store, not the declare init child). Skip our own materialized
  // literals (re-entry guard) and anything that is not an array declare.
  if (mode.find("reg") == std::string::npos) {
    return false;
  }
  if (init_ref.starts_with("%marrayinit_")) {
    return false;
  }
  if (!type_is_array && !type_is_none) {
    return false;
  }

  // The initializer must be a fully-comptime, dense, rectangular array bundle.
  auto b = symbol_table_.get_bundle(init_ref);
  if (!b) {
    return false;
  }
  const auto& km = b->non_attr_entries();
  if (km.empty()) {
    return false;
  }
  std::vector<int64_t> dims;
  std::vector<Dlop>    flat;
  for (const auto& [k, e] : km) {
    if (e.trivial.is_invalid()) {
      return false;  // a runtime field — not a comptime literal
    }
    flat.emplace_back(e.trivial);
    // Each dotted segment must be a non-negative decimal index (positional
    // array), else this is a named tuple/struct, not an array.
    size_t seg = 0, pos = 0;
    while (true) {
      const size_t dot  = k.find('.', pos);
      const auto   part = k.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
      if (part.empty() || part.find_first_not_of("0123456789") != std::string::npos) {
        return false;
      }
      const int64_t idx = std::stoll(part);
      if (dims.size() <= seg) {
        dims.resize(seg + 1, 0);
      }
      dims[seg] = std::max(dims[seg], idx + 1);
      if (dot == std::string::npos) {
        break;
      }
      pos = dot + 1;
      ++seg;
    }
  }
  int64_t total = 1;
  for (auto d : dims) {
    total *= d;
  }
  if (dims.empty() || total != static_cast<int64_t>(flat.size())) {
    return false;  // ragged / not dense — let the existing diagnostic fire
  }
  if (type_is_none && total == 1) {
    return false;  // a 1-entry inferred bundle is a scalar reg, not a memory
  }

  // Element envelope for the inferred (typeless) form — synthesize a
  // prim_type_int from the bundle's baked element range, falling back to the
  // values' magnitude when the attr is absent.
  Dlop emax = b->get_attr("__elem_max");
  Dlop emin = b->get_attr("__elem_min");
  if (type_is_none && emax.is_invalid()) {
    int64_t mx = 0;
    for (const auto& v : flat) {
      if (!v.is_just_i64()) {
        return false;
      }
      mx = std::max(mx, v.to_just_i64());
    }
    emax = *Dlop::create_integer(mx);
    emin = *Dlop::create_integer(0);
  }

  ++inline_seq_;  // fresh namespace for this declare's materialized temps
  const std::string outer = materialize_array_literal(dims, 0, flat, 0);

  // Re-emit the declare with its init pointed at the materialized literal.
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-memdecl");
  auto s    = std::make_shared<Lnast>(body, "inl-memdecl");
  auto root = s->set_root(Lnast_ntype::create_declare());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(name));
  if (type_is_array) {
    copy_subtree_into(lm->get_lnast(), type_nid, s, root, nullptr);  // keep the declared type
  } else {
    // Synthesize comp_type_array(... prim_type_int(emax,emin) ...), outer dim
    // first (matches prp2lnast's array_type lowering).
    std::function<void(const Lnast_nid&, size_t)> build_arr = [&](const Lnast_nid& parent, size_t l) {
      auto arr = s->add_child(parent, Lnast_ntype::create_comp_type_array());
      if (l + 1 < dims.size()) {
        build_arr(arr, l + 1);
      } else {
        auto pt = s->add_child(arr, Lnast_ntype::create_prim_type_int());
        s->add_child(pt, Lnast_node::create_const(std::string(emax.to_pyrope())));
        s->add_child(pt, Lnast_node::create_const(std::string(emin.to_pyrope())));
      }
      s->add_child(arr, Lnast_node::create_const("[" + std::to_string(dims[l]) + "]"));
    };
    build_arr(root, 0);
  }
  s->add_child(root, Lnast_node::create_const(mode));
  s->add_child(root, Lnast_node::create_ref(outer));
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at declare root → declare case (guard skips re-entry)
  flush_deferred_emits();
  lm->pop_source();
  return true;
}

bool uPass_runner::try_lower_wrap_sat() {
  using N = Lnast_ntype;
  // Cursor is on the func_call. Walk children read-only; restore on every exit.
  const auto saved = lm->save_cursor();
  auto       bail   = [&]() {
    lm->restore_cursor(saved);
    return false;
  };

  if (!lm->has_child()) {
    return bail();
  }
  lm->move_to_child();  // dst
  if (!N::is_ref(lm->get_raw_ntype())) {
    return bail();
  }
  const std::string dst(lm->current_text());

  if (!lm->move_to_sibling()) {  // callee
    return bail();
  }
  // Read RAW: an inlined body tag-prefixes the fname (`wrap` → `inlN_wrap`).
  const auto callee  = lm->current_raw_text();
  const bool is_wrap = callee == "wrap";
  const bool is_sat  = callee == "sat" || callee == "saturate";
  if (!is_wrap && !is_sat) {
    return bail();
  }

  // Gather the `v=` value operand and the `type=` lhs ref.
  std::optional<Lnast_node> value;
  std::string               value_name;  // ref name (empty when the value is a const)
  std::string               type_src;    // the lhs whose declared type we narrow to
  while (lm->move_to_sibling()) {
    if (lm->get_raw_ntype() != N::Lnast_ntype_store || !lm->has_child()) {
      continue;
    }
    const auto inner = lm->save_cursor();
    lm->move_to_child();
    const std::string key(lm->current_text());
    if (lm->move_to_sibling()) {
      const auto vt = lm->get_raw_ntype();
      if (key == "v") {
        if (N::is_const(vt)) {
          value_name.clear();
          value = lm->current_node();
        } else if (N::is_ref(vt)) {
          value_name = std::string(lm->current_text());
          value      = Lnast_node::create_ref(value_name);
        }
      } else if (key == "type" && N::is_ref(vt)) {
        type_src = std::string(lm->current_text());
      }
    }
    lm->restore_cursor(inner);
  }
  lm->restore_cursor(saved);  // back on the func_call

  if (!value || type_src.empty()) {
    return false;  // malformed — let the normal path emit/diagnose
  }
  // The `type=` ref names the lvalue whose DECLARED envelope we narrow into.
  // When that lvalue was re-assigned before the wrap (`mut d:u32=0; d=0;
  // wrap d = …`) SSA reads it as the version `d___ssa_N`, whose per-version
  // binding carries NO declared envelope — only the BASE name `d` does. Strip
  // the suffix so decl_facts resolves the type; otherwise the lookup fails,
  // the runner declines, and the call leaks to tolg ("call to 'wrap' has no
  // hardware lowering yet") instead of clamping.
  if (const auto pos = type_src.find("___ssa_"); pos != std::string::npos) {
    type_src.resize(pos);
  }
  // Comptime values are folded by the attributes pass (and the drop path
  // retires the call); only RUNTIME values need hardware here.
  if (value_name.empty()) {
    return false;  // const value → comptime
  }
  if (symbol_table_.known_const_scalar(value_name).has_value()) {
    return false;  // folds to a known scalar → comptime
  }

  // Target type envelope: bits, signedness, and the declared [min,max].
  const auto facts = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), type_src);
  if (!facts || facts->bits == 0) {
    return false;  // unknown width → cannot lower (decline; old behavior diagnoses)
  }
  const auto     ub        = facts->bits;
  const bool     is_signed = facts->kind == upass::decl_facts::Num::signed_int
                         || (facts->range_min && facts->range_min->is_negative());
  const Dlop tmax = facts->range_max ? *facts->range_max : upass::max_from_bits(ub, is_signed);
  const Dlop tmin = facts->range_min ? *facts->range_min : upass::min_from_bits(ub, is_signed);

  // Value range: the bitwidth-stamped binding (tightest), falling back to the
  // value's DECLARED envelope (e.g. a module input never gets a per-write
  // bw_*). Both are sound over-approximations — needed both to gate the clamps
  // and to prove a no-op for the unnecessary-wrap/sat warning.
  std::optional<Dlop> vmax;
  std::optional<Dlop> vmin;
  if (auto b = symbol_table_.get_bundle(value_name); b) {
    const auto& e = b->get_entry(bundle_path::of_string("0"));
    if (!e.bw_max.is_invalid() && e.bw_max.is_integer()) {
      vmax = e.bw_max;
    }
    if (!e.bw_min.is_invalid() && e.bw_min.is_integer()) {
      vmin = e.bw_min;
    }
  }
  if (!vmax || !vmin) {
    if (auto vf = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), value_name)) {
      if (!vmax && vf->range_max && vf->range_max->is_integer()) {
        vmax = vf->range_max;
      }
      if (!vmin && vf->range_min && vf->range_min->is_integer()) {
        vmin = vf->range_min;
      }
    }
  }
  // Need the upper clamp/mask unless the value provably stays ≤ max; the lower
  // unless it provably stays ≥ min. (For an unsigned target min==0, so this is
  // the "value may be negative → clamp to 0" case of saturate_unsigned.)
  const bool need_hi = !(vmax && !vmax->gt_op(tmax)->is_known_true());
  const bool need_lo = !(vmin && !vmin->lt_op(tmin)->is_known_true());

  // Preserve the per-pass func_call side effects (bitwidth's wrap_sat_exempt_
  // handshake in particular), mirroring process_drop_candidate's step 1, so
  // the trailing store(lhs,dst) keeps skipping the does-not-fit check. The
  // func_call itself is NOT emitted (we return true).
  dispatch_to_passes(&upass::uPass::process_func_call);

  if (!need_hi && !need_lo) {
    // The value provably fits the target type — the wrap/sat narrows nothing.
    // Warn (the keyword is dead) and emit a plain alias.
    livehd::diag::Span span = lm->current_span();
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::warning,
        .code     = is_wrap ? "unnecessary-wrap" : "unnecessary-sat",
        .category = "bitwidth",
        .pass     = "upass.runner",
        .message  = std::format("unnecessary `{}`: the value already fits the target type", is_wrap ? "wrap" : "sat"),
        .span     = std::move(span),
        .hint     = "the value's range is within the target type, so the wrap/saturate has no effect",
    });
    emit_inline_binding(dst, *value);  // alias
    return true;
  }

  if (is_wrap) {
    // C/C++ truncation: keep the low N bits; sign-reinterpret per the type.
    const std::string mask_text(Dlop::get_mask_value(static_cast<int>(ub))->to_pyrope());
    if (is_signed) {
      const std::string masked = dst + "_wm";
      emit_inline_get_mask(masked, *value, mask_text);
      emit_inline_sext(dst, masked, static_cast<int>(ub) - 1);
    } else {
      emit_inline_get_mask(dst, *value, mask_text);
    }
    return true;
  }

  // sat: seed then bw-gated clamps, built straight into staging (the `if` can't
  // go through process_lnast — see emit_staging_op). A signed target re-signs
  // the clamped low-N bits through a final sext (the clamp result lies in
  // [min,max], so its low N bits are the correct two's-complement value); an
  // unsigned target writes the mux result directly (values in [0,max], read
  // unsigned).
  const std::string clamp = is_signed ? dst + "_sc" : dst;
  emit_staging_op(N::create_store(), clamp, {*value});  // seed: clamp = value
  if (need_hi) {
    const std::string cond = dst + "_sgt";
    const Lnast_node  hi   = Lnast_node::create_const(std::string(tmax.to_pyrope()));
    emit_staging_op(N::create_gt(), cond, {*value, hi});  // cond = value > max
    emit_staging_guarded_store(cond, clamp, hi);          // if (cond) clamp = max
  }
  if (need_lo) {
    const std::string cond = dst + "_slt";
    const Lnast_node  lo   = Lnast_node::create_const(std::string(tmin.to_pyrope()));
    emit_staging_op(N::create_lt(), cond, {*value, lo});  // cond = value < min
    emit_staging_guarded_store(cond, clamp, lo);          // if (cond) clamp = min
  }
  if (is_signed) {
    emit_staging_op(N::create_sext(), dst, {Lnast_node::create_ref(clamp),
                                            Lnast_node::create_const(static_cast<int64_t>(ub) - 1)});
  }
  return true;
}

bool uPass_runner::try_lower_typecast() {
  using N = Lnast_ntype;
  // Cursor on the func_call. Walk children read-only; restore on every decline.
  const auto saved = lm->save_cursor();
  auto       bail   = [&]() {
    lm->restore_cursor(saved);
    return false;
  };

  // Layout: ref(dst), ref(callee), <single positional operand>.
  if (!lm->has_child()) {
    return bail();
  }
  lm->move_to_child();  // dst
  if (!N::is_ref(lm->get_raw_ntype())) {
    return bail();
  }
  const std::string dst(lm->current_text());

  if (!lm->move_to_sibling()) {  // callee
    return bail();
  }
  // RAW name: an inlined body tag-prefixes the callee (`int` → `inlN_int`); the
  // cast classifier keys on the un-renamed builtin name (mirrors constprop).
  const std::string callee(lm->current_raw_text());
  auto              tc = upass::classify_typecast(callee);
  if (!tc) {
    // A type-valued generic used as a constructor/cast (`T(a)` with T bound to
    // `u8`): reclassify against the bound concrete token (todo 3g A).
    if (auto gb = generic_cast_binds_.find(callee); gb != generic_cast_binds_.end()) {
      tc = upass::classify_typecast(gb->second);
    }
    if (!tc) {
      return bail();  // not a built-in scalar cast (bool/boolean, user fn, cell-op…)
    }
  }

  // Exactly one positional operand (a ref or const). A `store(...)` named actual
  // or extra args means this is not a plain scalar cast — let the normal path run.
  if (!lm->move_to_sibling()) {
    return bail();
  }
  std::string arg_name;  // empty ⇒ const operand
  Lnast_node  arg_node;
  if (N::is_ref(lm->get_raw_ntype())) {
    arg_name = std::string(lm->current_text());
    arg_node = Lnast_node::create_ref(arg_name);
  } else if (N::is_const(lm->get_raw_ntype())) {
    arg_node = lm->current_node();
  } else {
    return bail();
  }
  if (lm->move_to_sibling()) {
    return bail();  // arity > 1 — not a scalar cast
  }
  lm->restore_cursor(saved);  // back on the func_call

  // A comptime operand folds in constprop (and the drop path retires the call);
  // only a runtime value needs hardware here.
  if (arg_name.empty()) {
    return false;  // const operand → comptime
  }
  if (symbol_table_.known_const_scalar(arg_name).has_value()) {
    return false;  // folds to a known scalar → comptime
  }

  // Operand kind + range: the bitwidth-stamped binding (tightest), falling back
  // to the declared envelope (a module input never gets a per-write bw_*).
  // Operand kind + range. bool-ness lives on the BUNDLE-level value kind
  // (typecheck's set_value_kind — a runtime `a<b` stamps value_kind=boolean
  // there, NOT on entry "0", whose range reads as a signed int(-1,0)); mirror
  // uPass_typecheck::kind_of_bundle. Enums carry the `enumentry` attr.
  bool                operand_is_bool = false;
  bool                operand_is_enum = false;
  upass::Kind         operand_kind    = upass::Kind::unknown;
  std::optional<Dlop> vmax;
  std::optional<Dlop> vmin;
  if (auto b = symbol_table_.get_bundle(arg_name); b) {
    const auto  vk = b->get_value_kind();
    const auto& e  = b->get_entry(bundle_path::of_string("0"));
    operand_kind    = (vk != upass::Kind::unknown) ? vk : e.kind;
    operand_is_bool = (operand_kind == upass::Kind::boolean);
    for (const auto& attr : b->get_attrs()) {
      if (Bundle::get_last_level(attr.first) == battr::enumentry) {
        operand_is_enum = true;
        break;
      }
    }
    if (!e.bw_max.is_invalid() && e.bw_max.is_integer()) {
      vmax = e.bw_max;
    }
    if (!e.bw_min.is_invalid() && e.bw_min.is_integer()) {
      vmin = e.bw_min;
    }
  }
  uint32_t reinterpret_W = 0;  // signed()/unsigned() reinterpret: the input's declared width
  if (!operand_is_bool && !operand_is_enum) {
    if (auto vf = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), arg_name)) {
      if (vf->kind == upass::decl_facts::Num::boolean) {
        operand_is_bool = true;
        operand_kind    = upass::Kind::boolean;
      } else if (operand_kind == upass::Kind::unknown
                 && (vf->kind == upass::decl_facts::Num::unsigned_int || vf->kind == upass::decl_facts::Num::signed_int)) {
        operand_kind = upass::Kind::integer;
      }
      if (!vmax && vf->range_max && vf->range_max->is_integer()) {
        vmax = vf->range_max;
      }
      if (!vmin && vf->range_min && vf->range_min->is_integer()) {
        vmin = vf->range_min;
      }
      reinterpret_W = vf->bits;
    }
  }
  // Fallback: a slice (`x#[0..=31]`) or arithmetic result has no DECLARED width,
  // but the bitwidth pass stamped a range — derive the reinterpret width from it
  // so `signed(x#[0..=31])` / `signed(a + b)` work, not just declared variables.
  if (!operand_is_bool && reinterpret_W == 0 && vmax && vmin) {
    if (!vmin->is_negative()) {
      reinterpret_W = vmax->is_known_zero() ? 0u : static_cast<uint32_t>(vmax->get_bits() - 1);
    } else {
      reinterpret_W = static_cast<uint32_t>(std::max<int64_t>(vmax->get_bits(), vmin->get_bits()));
    }
  }

  // Only a genuine runtime hardware scalar (int/bool) is lowered here. Enums
  // (`string(E.x)`/`int(E.x)`), comptime strings, tuples, ranges, nil — all
  // constprop's domain — are declined so its richer fold runs (this hook fires
  // BEFORE constprop on the func_call, so over-eager handling would corrupt
  // them). `unknown` is kept: a runtime arithmetic result (`int(a+b)`) is often
  // unstamped but is a real integer.
  if (operand_is_enum
      || (!operand_is_bool && operand_kind != upass::Kind::integer && operand_kind != upass::Kind::unknown)) {
    return false;
  }

  // A CHECKED cast (uN/sN) of an integer operand can only be proven safe with a
  // range; a bool operand always fits. Decline (let the normal path run) when
  // the range is missing. The signed()/unsigned() REINTERPRETS instead need the
  // input's declared width (reinterpret_W) — that is enforced in their switch
  // arms below.
  if (!operand_is_bool) {
    if (tc->kind == upass::Typecast_kind::to_sized && (!vmax || !vmin)) {
      return false;
    }
  }

  // Committed: run the per-pass func_call hooks first (mirrors
  // process_drop_candidate step 1 / try_lower_wrap_sat) so any handshake the
  // call carried still fires; the func_call node itself is NOT emitted.
  dispatch_to_passes(&upass::uPass::process_func_call);

  auto cast_error = [&](std::string_view code, std::string msg, std::string_view hint) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = std::string(code),
        .category = "type",
        .pass     = "upass.runner",
        .message  = std::move(msg),
        .span     = lm->current_span(),
        .hint     = std::string(hint),
    });
  };

  // 1-bit unsigned mask, used to read a bool's bit as an unsigned 0/1.
  const auto mask1 = std::string(Dlop::get_mask_value(1)->to_pyrope());

  switch (tc->kind) {
    case upass::Typecast_kind::to_string:
      // A runtime value has no string form (strings are comptime-only).
      cast_error("runtime-string-cast",
                 "`string(...)` needs a compile-time value — a runtime signal has no string form",
                 "build the string from comptime data, or print it via a debug statement");
      return true;

    case upass::Typecast_kind::to_signed:
      // REINTERPRET as signed (Verilog $signed): keep the input's bits, re-tag
      // the sign. A `uN` value's bit N-1 becomes the sign (no gate — just a wire
      // alias + a signed typespec).
      if (operand_is_bool) {
        emit_inline_sext(dst, arg_name, 0);  // 1-bit signed: true = -1, false = 0
        emit_inline_typespec(dst, 1, true);
      } else {
        if (reinterpret_W == 0) {
          cast_error("cast-not-typed",
                     std::format("`{}(...)` reinterpret needs a fully-typed input (a known bit width)", callee),
                     "give the operand a sized type (`:uN`/`:sN`) before reinterpreting its sign");
          return true;
        }
        // Read bit W-1 as the sign: same W bits, now a signed value/range.
        emit_inline_sext(dst, arg_name, static_cast<int>(reinterpret_W) - 1);
        emit_inline_typespec(dst, static_cast<int>(reinterpret_W), true);
      }
      return true;

    case upass::Typecast_kind::to_bool:
      // `bool(int)` == (x != 0); `bool(bool)` is identity. (Undefined/nil only
      // exist at comptime — constprop errors there; a runtime signal has no `?`.)
      if (operand_is_bool) {
        emit_inline_binding(dst, arg_node);
      } else {
        emit_inline_to_bool(dst, arg_node);
      }
      return true;

    case upass::Typecast_kind::to_uint:
      // REINTERPRET as unsigned (Verilog $unsigned): keep the input's bits,
      // re-tag the sign. A negative `sN` value reads as its 2^N magnitude.
      if (operand_is_bool) {
        emit_inline_get_mask(dst, arg_node, mask1);  // unsigned bit: true = 1, false = 0
        emit_inline_typespec(dst, 1, false);
        return true;
      }
      if (reinterpret_W == 0) {
        cast_error("cast-not-typed",
                   std::format("`{}(...)` reinterpret needs a fully-typed input (a known bit width)", callee),
                   "give the operand a sized type (`:uN`/`:sN`) before reinterpreting its sign");
        return true;
      }
      // Mask to the low W bits: same bits, now an unsigned value/range.
      emit_inline_get_mask(dst, arg_node, std::string(Dlop::get_mask_value(reinterpret_W)->to_pyrope()));
      emit_inline_typespec(dst, static_cast<int>(reinterpret_W), false);
      return true;

    case upass::Typecast_kind::to_sized: {
      const auto ub  = static_cast<uint32_t>(tc->sized_bits);
      const bool sgn = tc->sized_signed;
      if (operand_is_bool) {
        if (sgn) {
          emit_inline_sext(dst, arg_name, 0);          // true = -1
        } else {
          emit_inline_get_mask(dst, arg_node, mask1);  // true = 1
        }
        emit_inline_typespec(dst, static_cast<int>(ub), sgn);
        return true;
      }
      const Dlop tmax = upass::max_from_bits(ub, sgn);
      const Dlop tmin = upass::min_from_bits(ub, sgn);
      if (vmax->gt_op(tmax)->is_known_true() || vmin->lt_op(tmin)->is_known_true()) {
        cast_error("cast-overflow",
                   std::format("value range [{}, {}] does not fit the `{}` cast range [{}, {}]",
                               vmin->to_decimal_string(),
                               vmax->to_decimal_string(),
                               callee,
                               tmin.to_decimal_string(),
                               tmax.to_decimal_string()),
                   "a sized cast (`uN`/`sN`) is checked, not truncating; use a `wrap` or `sat` prefix to drop bits");
        return true;
      }
      emit_inline_binding(dst, arg_node);  // fits → value-preserving
      emit_inline_typespec(dst, static_cast<int>(ub), sgn);
      return true;
    }
  }
  return true;  // all Typecast_kind cases handled above
}

bool uPass_runner::lower_in() {
  using N = Lnast_ntype;
  // func_in(dst, a, b): dst (child 0), a (subject, child 1), b (rhs, child 2).
  // Read children read-only; we never move the main cursor off the func_in (the
  // inline emits below ride scratch sources), matching try_lower_wrap_sat.
  const auto saved = lm->save_cursor();
  auto       bail  = [&]() {
    lm->restore_cursor(saved);
    return false;
  };

  livehd::diag::Span span = lm->current_span();

  if (!lm->has_child()) {
    return bail();  // malformed — caller falls back
  }
  lm->move_to_child();  // dst
  if (!N::is_ref(lm->get_raw_ntype())) {
    return bail();
  }
  const std::string dst(lm->current_text());

  if (!lm->move_to_sibling()) {  // a (subject)
    return bail();
  }
  const bool a_is_ref   = N::is_ref(lm->get_raw_ntype());
  const bool a_is_const = N::is_const(lm->get_raw_ntype());
  if (!a_is_ref && !a_is_const) {
    return bail();
  }
  const Lnast_node  a_node = lm->current_node();
  const std::string a_name = a_is_ref ? std::string(lm->current_text()) : std::string{};
  const std::string a_text = a_is_const ? std::string(lm->current_text()) : std::string{};

  if (!lm->move_to_sibling()) {  // b (rhs)
    return bail();
  }
  const bool        b_is_ref   = N::is_ref(lm->get_raw_ntype());
  const bool        b_is_const = N::is_const(lm->get_raw_ntype());
  const Lnast_node  b_node     = lm->current_node();
  const std::string b_name     = b_is_ref ? std::string(lm->current_text()) : std::string{};
  const std::string b_text     = b_is_const ? std::string(lm->current_text()) : std::string{};
  if (!b_is_ref && !b_is_const) {
    return bail();
  }
  lm->restore_cursor(saved);  // back on the func_in

  const In_type a_type = a_is_const ? classify_in_const(a_text) : classify_in_bundle(symbol_table_.get_bundle(a_name));

  // A bare const rhs (`a in 5`) is a one-element membership: `dst = (a == 5)`.
  if (b_is_const) {
    if (!in_types_compatible(a_type, classify_in_const(b_text))) {
      in_op_fail(span, "in-type-mismatch",
                 std::format("`in` compares values of different types: the left operand is {}, but the right is {}",
                             in_kind_name(a_type.k), in_kind_name(classify_in_const(b_text).k)),
                 "the right operand of `in` must have the same type as the left operand");
    }
    emit_inline_op(N::create_eq(), dst, {a_node, b_node});
    return true;
  }

  // RHS must be a known, UNNAMED tuple/array (tuple shapes are always comptime).
  auto b_bundle = symbol_table_.get_bundle(b_name);
  if (!b_bundle) {
    // No bundle for the RHS: this happens only in front-end-only modes (e.g.
    // `upass.order=noop`, the parsing/lnast checks) where constprop never ran,
    // so there is nothing to expand against. Decline — the caller emits the
    // func_in verbatim; those modes never lower to tolg. In a REAL evaluation
    // pipeline constprop has resolved the tuple shape, so b_bundle is non-null
    // and the expansion below always runs.
    return bail();
  }
  if (b_bundle->has_named_top()) {
    in_op_fail(span, "in-rhs-named", "the right operand of `in` must be an unnamed tuple/array, but it has named fields",
               "use a positional tuple like `(x, y, z)`; named-field membership is not supported");
  }

  // Enum membership over a bitwise UNION (03-bundle.md): `a in u`, where `a` is
  // an enum value and `u` is a SINGLE union scalar, is bit-containment
  // `(a & u) == a`. An array of enum literals carries a per-member `enumentry`
  // tag on each element ("0.enumentry", …) — that falls through to the eq
  // expansion below; a union scalar carries none, so it routes here.
  if (a_type.k == In_type::K::enumv && b_bundle->scalar().has_value()) {
    bool b_has_member_tags = false;
    for (const auto& [k, ep] : b_bundle->get_attrs()) {
      (void)ep;
      if (Bundle::get_last_level(k) == battr::enumentry && Bundle::get_first_level(k) != k) {
        b_has_member_tags = true;  // a member's own tag (keyed under "0."/name) ⇒ array of enum values
        break;
      }
    }
    if (!b_has_member_tags) {
      // Comptime fold via the extracted encodings — needed for a hierarchical
      // PARENT value (`Animal.bird`), whose bits live in the `enumval` attr, not
      // the scalar, so a raw `a & b` LNAST op could not see them.
      const auto ea = enum_encoding_of(symbol_table_.get_bundle(a_name));
      const auto eb = b_bundle->scalar();
      if (ea && ea->is_integer() && !ea->has_unknowns() && eb && eb->is_integer() && !eb->has_unknowns()) {
        const bool contained = ea->and_op(*eb)->same_repr(*ea);  // a ⊆ b
        emit_inline_op(N::create_store(), dst, {Lnast_node::create_const(contained ? "true" : "false")});
        return true;
      }
      // Runtime fallback: a leaf/runtime value IS its encoding, so `(a & b) == a`.
      const std::string andt = std::format("{}_iand", dst);
      emit_inline_op(N::create_bit_and(), andt, {a_node, b_node});
      emit_inline_op(N::create_eq(), dst, {Lnast_node::create_ref(andt), a_node});
      return true;
    }
  }

  // Expand `dst = (a==b[0]) or (a==b[1]) or … or (a==b[N-1])`. Each element is
  // picked into its own ref (tuple_get → runtime copy or const fold), classified
  // against `a`, then compared; the comparisons OR together. All emits go through
  // the inline path so constprop folds the constant cases.
  std::vector<Lnast_node> or_terms;
  size_t                  idx = 0;
  for (const auto& tl : b_bundle->top_levels()) {
    // Classify the element from b's STRUCTURE (the sub-bundle), not from the
    // picked value: comptime folding strips a value's enum-identity attr, so a
    // picked enum element would misread as a bare integer. The sub-bundle keeps
    // the `enumentry` tag, so hierarchical/flat enums classify correctly.
    const In_type e_type = classify_in_bundle(b_bundle->get_bundle(bundle_path::of_string(std::to_string(tl.pos))));
    if (!in_types_compatible(a_type, e_type)) {
      in_op_fail(span, "in-type-mismatch",
                 std::format("`in` compares values of different types: the left operand is {}, but element {} is {}",
                             in_kind_name(a_type.k), idx, in_kind_name(e_type.k)),
                 "every element on the right of `in` must have the same type as the left operand (integer "
                 "width/signedness may differ; bool, integer, enum and tuple may not be mixed)");
    }

    const std::string pick = std::format("{}_in{}", dst, idx);
    emit_inline_tuple_pick(pick, b_name, std::to_string(tl.pos));  // b[i] → runtime copy or const fold
    const std::string cmp = std::format("{}_ic{}", dst, idx);
    emit_inline_op(N::create_eq(), cmp, {a_node, Lnast_node::create_ref(pick)});
    or_terms.emplace_back(Lnast_node::create_ref(cmp));
    ++idx;
  }

  if (or_terms.empty()) {
    emit_inline_op(N::create_store(), dst, {Lnast_node::create_const("false")});  // `a in ()` ⇒ false
  } else if (or_terms.size() == 1) {
    emit_inline_op(N::create_store(), dst, {or_terms.front()});  // `dst = (a == b[0])`
  } else {
    // Left-fold the comparisons into a chain of TWO-input `or`s rather than one
    // n-ary `log_or`: downstream passes (the verifier in particular) assume a
    // binary `log_or`, and tolg lowers the chain to the same OR-reduction.
    for (size_t i = 1; i < or_terms.size(); ++i) {
      const std::string out = (i + 1 == or_terms.size()) ? dst : std::format("{}_or{}", dst, i);
      const Lnast_node  lhs = (i == 1) ? or_terms[0] : Lnast_node::create_ref(std::format("{}_or{}", dst, i - 1));
      emit_inline_op(N::create_log_or(), out, {lhs, or_terms[i]});
    }
  }
  return true;
}

void uPass_runner::emit_inline_op(Lnast_ntype::Lnast_ntype_int op, const std::string& dst,
                                  const std::vector<Lnast_node>& operands) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-op");
  auto s    = std::make_shared<Lnast>(body, "inl-op");
  auto root = s->set_root(op);
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));
  for (const auto& o : operands) {
    s->add_child(root, o);
  }
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at op root → dispatch + emit/fold
  flush_deferred_emits();
  lm->pop_source();
}

bool uPass_runner::bind_call_actuals(const Lnast_tree_io& io, const std::vector<Actual>& actuals, bool is_ctor_call,
                                     bool commit, std::string_view callee_name, const livehd::diag::Span& call_span,
                                     std::vector<Lnast_node>& param_val, std::vector<bool>& param_set,
                                     std::vector<std::string>& param_func, std::vector<Lnast_node>& vararg_pos,
                                     std::vector<std::pair<std::string, Lnast_node>>& vararg_named) {
  const std::size_t nparams    = io.inputs.size();
  // A trailing `...args` var-arg param (always the LAST input) gathers every
  // actual not consumed by a fixed leading param into one synthesized tuple:
  // positional leftovers become positional entries (`args[i]`), named leftovers
  // become named fields (`args.NAME`).
  const bool        has_vararg = nparams > 0 && io.inputs[nparams - 1].is_varargs;
  const std::size_t nbind      = has_vararg ? nparams - 1 : nparams;  // bindable fixed params (vararg excluded)

  param_val.assign(nparams, Lnast_node::create_invalid());
  param_set.assign(nparams, false);
  param_func.assign(nparams, std::string{});
  vararg_pos.clear();
  vararg_named.clear();

  auto param_index = [&](std::string_view k) -> std::size_t {
    for (std::size_t i = 0; i < nbind; ++i) {  // never match the var-arg slot by name
      if (io.inputs[i].name == k) {
        return i;
      }
    }
    return nbind;
  };
  // Expand a (possibly RUNTIME) tuple ACTUAL `tup` into the flattened leaf params
  // `<prefix>.<field>` — a comptime field as a const, a runtime field as a ref to
  // its tuple_slot_ref wire. All-or-nothing: binds nothing and returns false
  // unless EVERY field maps onto an unset leaf param. Flat (one-level) tuples only.
  auto expand_tuple_actual = [&](std::string_view tup, std::string_view prefix) -> bool {
    const auto shape_opt = try_tuple_shape(tup);
    if (!shape_opt || shape_opt->empty()) {
      return false;
    }
    const auto& shape = *shape_opt;
    // A genuine tuple has >1 field or a NAMED (non-positional) field; a 1-entry
    // positional bundle is a scalar carrier, not a tuple worth expanding.
    bool genuine = shape.size() > 1;
    for (const auto& [fld, is_pos] : shape) {
      if (!is_pos) {
        genuine = true;
        break;
      }
    }
    if (!genuine) {
      return false;
    }
    const auto comptime = try_bundle_fields(tup);
    // Pre-resolve every field before committing (no partial bind on mismatch).
    std::vector<std::tuple<std::size_t, bool, std::string>> binds;  // (leaf idx, is_const, payload)
    binds.reserve(shape.size());
    for (const auto& [fld, is_pos] : shape) {
      const auto lidx = param_index(std::string(prefix) + "." + fld);
      if (lidx >= nbind || param_set[lidx]) {
        return false;
      }
      bool resolved = false;
      if (comptime) {
        for (const auto& [k, v] : *comptime) {
          if (k == fld && !v.is_invalid()) {
            binds.emplace_back(lidx, true, v.to_pyrope());
            resolved = true;
            break;
          }
        }
      }
      if (!resolved) {
        if (auto rn = try_tuple_slot_ref(tup, fld)) {
          binds.emplace_back(lidx, false, *rn);
          resolved = true;
        }
      }
      if (!resolved) {
        return false;
      }
    }
    for (const auto& [lidx, is_const, payload] : binds) {
      param_val[lidx] = is_const ? Lnast_node::create_const(payload) : Lnast_node::create_ref(payload);
      param_set[lidx] = true;
    }
    return true;
  };
  const bool        has_self       = nbind > 0 && io.inputs[0].name == "self";
  const std::size_t n_named_params = nbind - (has_self ? 1 : 0);  // fixed, non-self (var-arg excluded)
  auto              actual_kind    = [&](const Lnast_node& node) { return actual_node_kind(node); };
  for (const auto& a : actuals) {
    if (a.is_named) {
      if (a.key == "self") {
        if (!commit) {
          return false;  // self is never named
        }
        fcall_arg_fail(call_span,
                       "fcall-self-named",
                       std::format("`self` cannot be passed as a named argument to `{}`", callee_name),
                       "self is bound positionally by the UFCS receiver (`value.method(...)`)");
      }
      const auto idx = param_index(a.key);
      if (idx >= nbind) {
        // A named actual that matches no fixed param is a named leftover gathered
        // into the var-arg tuple (`args.NAME`); otherwise a bundle-typed actual
        // `ar=(x=2,y=11)` (or a runtime tuple `ar=a`) expands field-by-field.
        if (has_vararg) {
          vararg_named.emplace_back(a.key, a.node);
          continue;
        }
        bool expanded = false;
        if (a.node.is_ref()) {
          expanded = expand_tuple_actual(a.node.get_name(), a.key);
        }
        if (!expanded) {
          if (!commit) {
            return false;  // unknown argument
          }
          fcall_arg_fail(call_span,
                         "fcall-unknown-arg",
                         std::format("unknown argument `{}` in call to `{}`", a.key, callee_name),
                         "remove it or rename it to a declared parameter");
        }
        continue;
      }
      if (param_set[idx]) {
        if (!commit) {
          return false;  // duplicate
        }
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
      // naming rules — and so does the UFCS receiver bound to `self`.
      if (a.is_ref_pass) {
        const auto slot = next_unset(0);
        if (slot >= nbind) {
          if (has_vararg) {
            vararg_pos.push_back(a.node);
            continue;
          }
          if (!commit) {
            return false;  // too many positional
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
      // With a var-arg param, fixed leading params bind positionally in
      // declaration order; every actual past them is a positional leftover
      // gathered into the var-arg tuple. The naming exceptions below are skipped.
      if (has_vararg) {
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot < nbind) {
          bind(slot);
        } else {
          vararg_pos.push_back(a.node);
        }
        continue;
      }
      // Constructor call (synthesized): construction args bind in tuple order.
      if (is_ctor_call) {
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot >= nparams) {
          if (!commit) {
            return false;
          }
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
      // actual binds by EXPANDING its fields into those leaves.
      if (a.node.is_ref()) {
        const auto shape_opt       = try_tuple_shape(a.node.get_name());
        bool       is_tuple_actual = false;
        if (shape_opt) {
          is_tuple_actual = shape_opt->size() > 1;
          for (const auto& [fld, is_pos] : *shape_opt) {
            if (!is_pos) {
              is_tuple_actual = true;
              break;
            }
          }
        }
        if (is_tuple_actual) {
          // Find the unset flattened tuple-param GROUP whose leaf count matches
          // the actual's field count, then expand the fields into it.
          absl::flat_hash_map<std::string, std::size_t> group_count;
          for (std::size_t i = (has_self ? 1u : 0u); i < nbind; ++i) {
            if (param_set[i]) {
              continue;
            }
            const auto& pn = io.inputs[i].name;
            if (auto dp = pn.rfind('.'); dp != std::string::npos) {
              ++group_count[pn.substr(0, dp)];
            }
          }
          bool matched = false;
          for (const auto& [prefix, cnt] : group_count) {
            if (cnt == shape_opt->size() && expand_tuple_actual(a.node.get_name(), prefix)) {
              matched = true;
              break;
            }
          }
          if (matched) {
            continue;
          }
          // A tuple actual that matched no group must NOT bind a lone scalar
          // param in the overload probe (that is the tuple-vs-scalar
          // discrimination); the real bind falls through to exception 1.
          if (!commit) {
            return false;
          }
        }
      }
      // Exception 2: a bare variable whose name matches an unset, non-self
      // parameter binds to THAT parameter (so `f(b, a)` binds by name).
      if (a.node.is_ref()) {
        const auto nidx = param_index(a.node.get_name());
        if (nidx < nparams && nidx >= (has_self ? 1u : 0u) && !param_set[nidx]) {
          bind(nidx);
          continue;
        }
      }
      // Exception 1: exactly one non-self parameter — a single positional value
      // maps unambiguously (also the whole-tuple→scalar path under commit).
      if (n_named_params == 1) {
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot < nparams) {
          bind(slot);
          continue;
        }
      }
      // Exception 3: the actual's kind uniquely identifies one unset, typed,
      // non-self parameter. Untyped params (kind=none) never match.
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
      if (!commit) {
        return false;  // ambiguous / unbindable positional — must be named
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
  return true;
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
  std::string dst_name(lm->current_text());          // dst lives in the caller/frame scope
  std::string dst_raw_name(lm->current_raw_text());  // un-renamed source dst — the hier level

  if (!lm->move_to_sibling() || lm->get_raw_ntype() != Lnast_ntype::Lnast_ntype_ref) {
    lm->restore_cursor(saved);
    return false;
  }
  std::string callee_name(lm->current_raw_text());  // function id — never renamed

  // Higher-order resolution: inside an inlined body, `f(x)` where `f` is a
  // function-valued param resolves to the function bound at the outer call
  // site (closure_capture / fcall6). func_param_bindings_ is keyed by the raw
  // param name as it appears in the body.
  bool via_param_binding = false;
  if (auto fb = func_param_bindings_.find(callee_name); fb != func_param_bindings_.end()) {
    callee_name        = fb->second;
    via_param_binding  = true;
  }
  // The callee identifier as written at the call site (method dispatch and
  // the import-namespace exemption below need it after callee_name rebinds).
  const std::string source_callee_name(callee_name);

  auto callee = lookup_callee(callee_name);

  // compile.upass.inline=false: a DIRECTLY-resolved, Sub-convertible `comb`
  // whose call produces RUNTIME hardware is left as a func_call so tolg lowers
  // it to a module instance instead of inlining (preserving the comb boundary
  // for debug/optimization). A callee reached through a function-valued param
  // (closure), or through the method/overload/lambda-array fallbacks below, has
  // no standalone Sub form and must always inline. The lambda-ref const binding
  // is the ONE exception (it names a real registered module) and re-runs this
  // gate once it resolves — see sub_instance_eligible below. The actual
  // runtime-vs-comptime decision is deferred to the post-gather check below: an
  // all-constant call still inlines so it folds to a comptime value (casserts /
  // comptime evaluation keep working — there is no runtime instance to keep).
  auto sub_instance_eligible = [&](const std::shared_ptr<Lnast>& c) {
    return !inlining_enabled_ && c && !via_param_binding
           && reg().sub_convertible_combs.contains(std::string(c->get_top_module_name()));
  };
  bool consider_sub_instance = sub_instance_eligible(callee);

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
            if ((fld == callee_name) && val.is_string()) {
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
        // A lambda-ref const binding (`const F = import("file.F")`) names a REAL
        // registered module — unlike a closure/method/overload fallback, it has a
        // standalone Sub form, so it is still eligible for inline=false. Generated
        // code imports under an alias (`const F_t = import("file.F")`) whenever the
        // plain name is taken by the instance variable, and the initial by-name
        // lookup above necessarily missed it.
        consider_sub_instance = sub_instance_eligible(callee);
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
      std::vector<Generic_actual> ov_generics;
      if (gather_actuals(/*drop_ufcs_receiver=*/false, ov_actuals, ov_generics)) {
        std::string chosen;
        if (cands.size() == 1) {
          chosen = cands.front();  // single → defer to the normal precise-error path
        } else {
          // "Can handle" is the WHOLE `dst = f(args)`: the candidate must accept
          // the call (signature_matches, the input side) AND its return must
          // bind to how the result is consumed (return_matches, the output
          // side). Without the return check an input-compatible candidate whose
          // outputs do not fit the destructure is silently picked.
          absl::flat_hash_set<std::string> req_fields;
          bool       whole_used = false;
          collect_return_consumption(saved, dst_name, req_fields, whole_used);
          for (const auto& fn : cands) {
            auto c = lookup_callee(fn);
            if (c && signature_matches(c->io_meta(), ov_actuals, /*is_ctor_call=*/false)
                && return_matches(c->io_meta(), req_fields, whole_used)) {
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
              if (!(fld == source_callee_name) || !val.is_string()) {
                continue;
              }
              auto fn = val.to_pyrope();
              if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
                fn = fn.substr(1, fn.size() - 2);
              }
              if ((fn == callee->get_top_module_name()) || lookup_callee(fn) == callee) {
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
  // An HDL-origin (verilog→pyrope) module is INSTANTIATED, not value-called:
  // `mut inst = Mod(...)` binds an instance handle, and a zero-output sink
  // (e.g. a XiangShan `DiffExt*` DPI observer under -DSYNTHESIS) legitimately
  // has no fields to read.  The leak this gate guards against is the removed
  // native-pyrope implicit-return sugar, so only native (`!verilog_origin`)
  // callees are checked; an HDL sink lowers to a Sub instance in tolg.
  if (!is_ctor_call && !callee->is_verilog_origin() && callee_has_no_outputs()) {
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
  if (!reg().inlinable_callees.contains(std::string(callee->get_top_module_name()))) {
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
  std::vector<Generic_actual> explicit_generics;  // `f<int,string>(…)` binds, in order (named or positional)
  if (!gather_actuals(drop_ufcs_receiver, actuals, explicit_generics)) {
    return false;
  }
  lm->restore_cursor(saved);  // back on the func_call node (gather left it on the callee ref)
  const std::string call_inst_name = gathered_inst_name_;  // call-site name= (if any); stable past later gathers

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
  // The full binding ladder (06-functions.md §"Argument naming") lives in the
  // shared bind_call_actuals so the overload probe (signature_matches) cannot
  // drift from the real bind. commit=true → fatal diagnostics + whole-tuple→scalar.
  std::vector<Lnast_node>                         vararg_pos;
  std::vector<std::pair<std::string, Lnast_node>> vararg_named;
  std::vector<Lnast_node>                         param_val;
  std::vector<bool>                               param_set;
  std::vector<std::string>                        param_func;
  bind_call_actuals(io, actuals, is_ctor_call, /*commit=*/true, callee_name, call_span, param_val, param_set, param_func,
                    vararg_pos, vararg_named);
  const std::size_t nparams    = io.inputs.size();
  const bool        has_vararg = nparams > 0 && io.inputs[nparams - 1].is_varargs;
  const std::size_t nbind      = has_vararg ? nparams - 1 : nparams;
  const bool        has_self   = nbind > 0 && io.inputs[0].name == "self";

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
      // Template mod/pipe/fluid specialization returns true unconditionally, so
      // the general missing-arg check below (3527) is unreachable for these
      // callees. Run the same check here so an omitted required arg errors
      // instead of being silently pushed as an invalid actual (is_method==false
      // here, so there is no self slot to skip).
      for (std::size_t i = 0; i < nbind; ++i) {
        if (!param_set[i] && !io.inputs[i].has_default) {
          fcall_arg_fail(call_span,
                         "fcall-missing-arg",
                         std::format("missing required argument `{}` in call to `{}`", io.inputs[i].name, callee_name),
                         "provide the argument by name");
        }
      }
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

  // A pipe or a non-method mod lowers to a Sub INSTANCE, whose ports tolg wires
  // by declaration order for any unnamed actual. That mis-binds a
  // type-distinguished unnamed actual — the runner resolved it by KIND (e.g.
  // `pick(x, true)` binds x→the u8 param and true→the bool param regardless of
  // order), which position cannot reproduce. So if the source used any unnamed
  // actual, re-emit the call with the already-resolved binding as NAMED
  // `port=value` actuals and let tolg wire by name. The re-emitted call is
  // all-named, so on its re-walk this block is skipped and it declines straight
  // through — no re-canonicalization, no recursion.
  {
    const bool is_pipe = std::any_of(io.outputs.begin(), io.outputs.end(), [](const Lnast_io_entry& oe) {
      return oe.stages_min > 0;
    });
    const bool becomes_sub = is_pipe || (callee->get_lambda_kind() == "mod" && !has_self);
    const bool any_unnamed  = std::any_of(actuals.begin(), actuals.end(), [](const Actual& a) { return !a.is_named; });
    if (becomes_sub && any_unnamed) {
      std::vector<std::pair<std::string, Lnast_node>> named;
      named.reserve(nbind);
      for (std::size_t i = 0; i < nbind; ++i) {  // becomes_sub ⇒ no self slot
        if (param_set[i]) {
          named.emplace_back(io.inputs[i].name, param_val[i]);
        }
      }
      emit_named_instance_call(dst_name, callee_name, call_inst_name, named);
      return true;
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

  // Every non-self FIXED parameter must be bound — an unset input means the
  // caller omitted a required argument. EXCEPT a param with a declared default
  // (`comb f(in1:u4, in2=3)`, todo 3g E): an omitted default takes the
  // body-prologue value (bound below). The var-arg slot (index nbind, when
  // present) is always satisfied — it gathers zero or more leftovers.
  for (std::size_t i = (has_self ? 1 : 0); i < nbind; ++i) {
    if (!param_set[i] && !io.inputs[i].has_default) {
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

  // Whether a numeric actual is a comptime constant. Drives two gates: the
  // recursion base-case (a recursive callee only unrolls when every actual
  // folds) and the inline=false Sub decision (a call with a runtime actual is
  // the one that produces hardware worth keeping as an instance).
  auto actual_is_const = [&](std::size_t i) -> bool {
    if (!param_set[i] || !param_func[i].empty()) {
      return true;  // unset or function-valued param — not a numeric driver
    }
    const auto& pv = param_val[i];
    if (pv.is_const()) {
      return true;
    }
    if (pv.is_ref()) {
      auto fv = try_fold_ref(pv.get_name());
      if (fv && !fv->is_invalid() && !fv->has_unknowns()) {
        return true;
      }
      // A bundle actual (e.g. `tree_sum(v = data4, …)`) folds via the shared
      // ST instead of scalar fold_ref: const when every field is a concrete
      // comptime value.
      if (auto bf = try_bundle_fields(pv.get_name()); bf && !bf->empty()) {
        for (const auto& [k, v] : *bf) {
          if (v.is_invalid() || v.has_unknowns()) {
            return false;
          }
        }
        return true;
      }
    }
    return false;
  };
  const bool is_recursive_callee = reg().recursive_callees.contains(std::string(callee->get_top_module_name()));
  bool       all_actuals_const   = true;
  if (is_recursive_callee || consider_sub_instance) {
    for (std::size_t i = 0; i < nparams; ++i) {
      if (!actual_is_const(i)) {
        all_actuals_const = false;
        break;
      }
    }
  }

  // Recursive callees may only be spliced when every actual is a comptime
  // constant — only then does the body's base-case condition fold so
  // process_if prunes the recursive arm and the unroll terminates. With a
  // non-const arg (e.g. the standalone function body, where the param is an
  // unbound input) the recursion would never bottom out and just runs to the
  // fuel cap; leave those as a runtime func_call (→ evaluator / real call).
  if (is_recursive_callee && !all_actuals_const) {
    lm->restore_cursor(saved);
    return false;
  }

  // compile.upass.inline=false (see consider_sub_instance above): decline the
  // splice for a runtime-argument call so tolg emits a Sub module instance. An
  // all-constant call falls through and inlines, folding to a comptime value.
  if (consider_sub_instance && !all_actuals_const) {
    lm->restore_cursor(saved);
    return false;
  }

  // ── Splice ───────────────────────────────────────────────────────────────
  const uint32_t    salt = ++inline_seq_;
  const std::string tag  = std::format("inl{}_", salt);

  // Prologue: declare param + output widths (so `<tag>x.[bits]` folds), then
  // bind param values. Ref-param actuals are remembered for write-back.
  std::vector<std::pair<std::string, Lnast_node>>                 writebacks;
  // Function-valued params: record the body-local name → bound function so the
  // body's `f(x)` resolves; restored after the body walk. No value is emitted.
  std::vector<std::pair<std::string, std::optional<std::string>>> saved_func_bindings;
  // Type-generic constructor-cast tokens (`T(a)` → generic_cast_binds_), same
  // save/restore discipline as saved_func_bindings.
  std::vector<std::pair<std::string, std::optional<std::string>>> saved_cast_binds;
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
    }
  }
  // Bind each generic NAME inside the inline frame so body references resolve
  // (todo 3g A). A generic binds one of three comptime entities:
  //   * a CONSTANT (`f<3>`): the body reads it as a value (`a + N`) — emit the
  //     literal binding for the tagged name (`inlN_N = 3`).
  //   * a LAMBDA  (`f<inc>`): the body calls it (`F(v)`) — register the raw
  //     name in func_param_bindings_ so the call dispatches (same seam as a
  //     function-valued param).
  //   * a TYPE    (`f<u8>`): the body uses it as a `:T` type slot (declare refs
  //     renamed to `inlN_T`, bound via the named-type machinery) AND/OR as a
  //     constructor cast (`T(a)` → generic_cast_binds_ so try_lower_typecast
  //     reclassifies it against the concrete token).
  for (const auto& [g, gb] : gbinds) {
    if (!gb.func_name.empty()) {
      saved_func_bindings.emplace_back(
          g, func_param_bindings_.count(g) != 0u ? std::optional<std::string>(func_param_bindings_[g]) : std::nullopt);
      func_param_bindings_[g] = gb.func_name;
      continue;
    }
    if (!gb.const_text.empty()) {
      emit_inline_binding(upass::Lnast_manager::make_inlined_name(tag, g), Lnast_node::create_const(gb.const_text));
      continue;
    }
    if (gb.type_name.empty() && gb.kind == Io_kind::boolean) {
      emit_inline_typespec_bool(upass::Lnast_manager::make_inlined_name(tag, g));
    } else if (gb.type_name.empty() && (gb.max || gb.min)) {
      emit_inline_typespec_range(upass::Lnast_manager::make_inlined_name(tag, g), gb.max, gb.min);
    }
    // Constructor-cast token for a body `T(a)`. A named type is spelled
    // verbatim; an integer/bool envelope maps back to its scalar token.
    std::string cast_token;
    if (!gb.type_name.empty()) {
      cast_token = gb.type_name;
    } else if (gb.kind == Io_kind::boolean) {
      cast_token = "bool";
    } else if (gb.max || gb.min) {
      const bool is_signed = !(gb.min && !gb.min->is_negative());
      int        bits      = 0;
      if (gb.max && gb.max->is_integer()) {
        if (!is_signed) {
          bits = gb.max->is_known_zero() ? 0 : static_cast<int>(gb.max->get_bits() - 1);
        } else {
          const int mb = static_cast<int>(gb.max->get_bits());
          const int nb = (gb.min && gb.min->is_integer()) ? static_cast<int>(gb.min->get_bits()) : mb;
          bits         = std::max(mb, nb);
        }
      }
      cast_token = (is_signed ? "s" : "u") + std::to_string(bits);
    }
    if (!cast_token.empty()) {
      saved_cast_binds.emplace_back(
          g, generic_cast_binds_.count(g) != 0u ? std::optional<std::string>(generic_cast_binds_[g]) : std::nullopt);
      generic_cast_binds_[g] = cast_token;
    }
  }
  // An output may share the Pyrope name of an input (`comb f(x) -> (x)`):
  // 06-functions.md says reads before the output assignment use the INPUT
  // value, and the assignment binds the output. The param binding above already
  // established that value, so DO NOT re-seed such an output to nil — the seed
  // would clobber the input and a read-before-write (`c = a + b + x`) would
  // wrongly fold over nil.
  absl::flat_hash_set<std::string_view> input_names;
  for (const auto& ie : io.inputs) {
    input_names.insert(ie.name);
  }
  for (const auto& o : io.outputs) {
    if (input_names.contains(o.name)) {
      continue;  // shared input/output name: input binding stands (see above)
    }
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
    // emit_inline_binding marks `oname` as a nil RUNTIME placeholder so that an
    // op over an unwritten-but-runtime-DRIVEN output (`r = a + b`, kept
    // structural for tolg) stays structural instead of erroring. That mark must
    // stay. But a SCALAR integer/bool output that is read in an arithmetic op
    // BEFORE its first write (`res:unsigned` then `res <<= n` with no `res = 0`)
    // is a genuine read-before-write: the result of an op with that raw nil is a
    // compile error. Flag the raw seed as `uninitialized`; the first real store
    // (runtime or comptime) clears it (process_store). Reads while still flagged
    // are rejected by report_nil_operand even though the name is also nil_seeded.
    // Tuple / named-type / string outputs are not arithmetic accumulators — skip.
    if (o.type_name.empty() && (o.kind == Io_kind::integer || o.kind == Io_kind::boolean)) {
      symbol_table_.uninitialized.insert(oname);
    }
  }

  // Hierarchical instance level for this inline: the call-site `name=` if given,
  // else the source dst variable name (mirrors tolg's Sub-instance naming, so a
  // reg's hierarchical name is identical whether inlined or kept as a Sub). An
  // anonymous/temp dst falls back to a synthesized unique name, like tolg does.
  std::string inline_level;
  if (!call_inst_name.empty()) {
    inline_level = call_inst_name;
  } else {
    std::string d = dst_raw_name;
    if (auto p = d.find("___ssa_"); p != std::string::npos) {
      d.resize(p);
    }
    const bool is_tmp = Lnast::is_tmp(d);
    inline_level      = (!is_tmp && !d.empty()) ? d : ("u_" + callee_name + "_" + std::to_string(salt));
  }

  // Body: walk the callee stmts in place (names rewritten by the frame tag)
  // straight into the caller's current staging stmts. pop_source resets the
  // cursor regardless of how the body walk left it.
  hier_prefix_stack_.push_back(inline_level);
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
  // A PROVIDED arg that also has a default: the body opens with a prologue
  // `store(name, default)` (todo 3g E) — skip that one store so the actual bound
  // in the param loop wins. Only the FIRST prologue store per name is dropped
  // (a later body write to the same name is real). An OMITTED default is NOT in
  // this set, so its prologue store runs and binds the default value.
  absl::flat_hash_set<std::string> skip_default_stores;
  for (std::size_t i = 0; i < nbind && i < io.inputs.size(); ++i) {
    if (param_set[i] && io.inputs[i].has_default) {
      skip_default_stores.insert(io.inputs[i].name);
    }
  }
  lm->push_source(callee, tag, salt);
  if (lm->move_to_child()) {
    if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_io) {
      lm->move_to_sibling();
    }
    if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_stmts && lm->move_to_child()) {
      do {
        if (!skip_default_stores.empty() && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_store) {
          const auto  sc      = lm->save_cursor();
          bool        skipped = false;
          if (lm->move_to_child() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
            if (auto it = skip_default_stores.find(std::string(lm->current_raw_text())); it != skip_default_stores.end()) {
              skip_default_stores.erase(it);
              skipped = true;
            }
          }
          lm->restore_cursor(sc);
          if (skipped) {
            continue;  // provided arg wins over its default prologue store
          }
        }
        process_lnast();
      } while (lm->move_to_sibling());
    }
  }
  flush_deferred_emits();  // flush callee-tree parked writes before leaving
  lm->pop_source();
  inline_call_sites_.pop_back();
  active_inline_callees_.pop_back();
  hier_prefix_stack_.pop_back();
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
  // Restore the type-generic cast tokens shadowed by this frame.
  for (const auto& [key, old] : saved_cast_binds) {
    if (old) {
      generic_cast_binds_[key] = *old;
    } else {
      generic_cast_binds_.erase(key);
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
    std::vector<std::pair<std::string, std::string>> scalar_slot_refs;  // recorded AFTER emit
    fields.reserve(logical_order.size());
    for (const auto& lname : logical_order) {
      auto& leaves = logical[lname];
      if (leaves.size() == 1 && leaves[0].first.empty()) {
        fields.emplace_back(lname, leaves[0].second);  // scalar logical output
        if (leaves[0].second.is_ref()) {
          scalar_slot_refs.emplace_back(lname, std::string(leaves[0].second.get_name()));
        }
      } else {
        // A tuple-typed output among several: keep its dotted leaves so the
        // destructure / `.lname.sub` read still resolves (rare; one level).
        for (auto& [sub, ref] : leaves) {
          fields.emplace_back(lname + "." + sub, ref);
        }
      }
    }
    emit_inline_tuple(dst_name, fields);
    // A RUNTIME scalar output is held in the ST only as a nil-seeded scalar
    // bundle (output prologue, ~L2603); process_tuple_add (run by emit_inline_tuple
    // above, which ERASES + rebuilds the slot map) re-reads its bogus comptime
    // trivial (0) and the live wire `inl1_<out> = x+1` is lost — the destructure
    // picks fold to 0/nil and the call's logic is orphaned (verilog: `os = 65'sb1???`).
    // Record a runtime slot_ref so try_resolve_tuple_get rewrites `___2 = ___1.lname`
    // into a direct copy `___2 = inl1_<out>`, the same path that makes a
    // single-output comb work. Comptime outputs still fold (the destructure's
    // tuple_get finds the trivial first; the slot_ref is the runtime fallback).
    for (auto& [lname, src] : scalar_slot_refs) {
      symbol_table_.tuple_slot_ref[dst_name][lname] = src;
    }
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
  if (!lm->move_to_sibling()) {
    lm->restore_cursor(saved);
    return false;
  }
  // Gather the FULL field path. An all-comptime path (const / folded-ref
  // segments) builds a dotted key (`lo.hi`) for a possibly-NESTED slot-ref
  // lookup — `p = (lo=(hi=a, …), …)` stores `a`'s carrier under `p.lo.hi`
  // (constprop's propagate_sub_slot_refs). A genuinely runtime segment is only
  // handled for the single-segment dynamic-index (Hotmux) case below.
  std::string key;      // dotted comptime path
  std::string idx_ref;  // runtime index name (single-segment dynamic only)
  size_t      n_seg        = 0;
  bool        all_comptime = true;
  do {
    ++n_seg;
    if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
      if (auto v = Dlop::from_pyrope(lm->current_text()); v && !v->is_invalid()) {
        absl::StrAppend(&key, key.empty() ? "" : ".", v->to_field());
      } else {
        all_comptime = false;
      }
    } else if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
      // A comptime-known index (a folded loop var) resolves; anything else does not.
      if (auto fv = try_fold_ref(lm->current_text()); fv && fv->is_integer() && !fv->has_unknowns()) {
        absl::StrAppend(&key, key.empty() ? "" : ".", fv->to_field());  // decimal at any width
      } else {
        idx_ref      = std::string(lm->current_text());  // genuinely runtime — candidate for a dynamic mux
        all_comptime = false;
      }
    } else {
      all_comptime = false;
    }
  } while (!lm->is_last_child() && lm->move_to_sibling());
  const bool single_segment = (n_seg == 1);
  lm->restore_cursor(saved);
  if (!all_comptime || key.empty()) {
    // A runtime index into a comptime fixed-size tuple of scalar wires lowers
    // to a balanced Hotmux (the only datapath select tolg otherwise rejects).
    if (single_segment && key.empty() && !idx_ref.empty() && try_lower_dynamic_tuple_index(dst, src, idx_ref)) {
      return true;
    }
    return false;
  }
  // (1) var-arg gathered entries (keyed by the frame-tagged var-arg name).
  // Var-arg gather is single-level.
  if (single_segment) {
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
  }
  // (2) general tuple variable whose slot holds a runtime scalar ref — the key
  // may be a NESTED dotted path (`lo.hi`) re-homed by propagate_sub_slot_refs.
  if (auto rname = try_tuple_slot_ref(src, key)) {
    emit_inline_binding(dst, Lnast_node::create_ref(*rname));
    return true;
  }
  return false;
}

bool uPass_runner::try_lower_dynamic_tuple_index(const std::string& dst, const std::string& src, const std::string& idx_ref) {
  using N = Lnast_ntype;
  // The source must be a comptime fixed-size tuple of scalar elements at
  // contiguous positional slots 0..n-1. Each element resolves to either a
  // runtime-wire ref (tuple_slot_ref) or a comptime constant (bundle trivial);
  // a named field, a hole, or a nested sub-tuple slot declines.
  auto shape = try_tuple_shape(src);
  if (!shape || shape->size() < 2) {
    return false;
  }
  const size_t            n      = shape->size();
  const auto              bundle = symbol_table_.get_bundle(src);
  std::vector<Lnast_node> elems;
  elems.reserve(n);
  bool any_runtime = false;
  for (size_t k = 0; k < n; ++k) {
    const auto& [slot, is_pos] = (*shape)[k];
    if (!is_pos || slot != std::to_string(k)) {
      return false;  // named or non-contiguous slot — not a plain indexable array
    }
    if (auto rname = try_tuple_slot_ref(src, slot)) {
      elems.push_back(Lnast_node::create_ref(*rname));  // runtime wire
      any_runtime = true;
    } else if (bundle) {
      const Dlop& t = bundle->get_trivial(bundle_path::of_string(slot));
      if (t.is_invalid()) {
        return false;  // runtime-unknown / nested sub-tuple slot with no recorded ref
      }
      elems.push_back(Lnast_node::create_const(std::string(t.to_pyrope())));  // comptime constant
    } else {
      return false;
    }
  }
  // GATE: a runtime index into a comptime fixed-size tuple lowers to a Hotmux.
  // A tuple holding at least one genuine runtime wire (any_runtime) is always a
  // plain tuple-of-wires literal — mux it. An ALL-constant tuple is also
  // muxable, but ONLY when `src` is a plain (non-memory) tuple value:
  //   * NOT a comp_type_array — those are Memory/ROM cells lowered by tolg;
  //     muxing one would (a) drop a runtime-indexed write to a `mut`/`reg`
  //     array (its constant init never reflects the write) and (b) blow a large
  //     const ROM into a giant mux tree.
  //   * a `const`/`mut` binding, never a `reg` — a reg carries cross-cycle
  //     state, so its constant init is not the muxable value.
  // (A runtime-indexed write to a non-array tuple has no hardware lowering and
  // errors at the store, so an all-const tuple reaching here cannot be hiding a
  // dropped write; const-index const-value writes already folded into the slot
  // trivials, so the mux arms are exact.)
  if (!any_runtime) {
    // A comp_type_array declare bakes an element envelope (__elem_max/__elem_min,
    // present for every valid array — int and bool are the only legal element
    // types); its absence is what tells a plain tuple value from a Memory/ROM.
    const bool is_array_typed = bundle && !bundle->get_attr("__elem_max").is_invalid();
    const auto facts          = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), src);
    const bool muxable_mode   = facts && (facts->mode == upass::Mode::const_kind || facts->mode == upass::Mode::mut_kind);
    if (is_array_typed || !muxable_mode) {
      return false;
    }
  }

  // Materialize any parked producer of the index (or of an element) before the
  // comparisons reference it — a computed index `t[i+1]` defers `i+1`, which
  // would otherwise emit AFTER these `eq` nodes and dangle as a forward ref.
  flush_deferred_emits();

  // Emit `cmp_k = (idx == k)` for k in 0..n-2, then a unique_if whose arms bind
  // `dst` to each element with the last element as the mandatory else — the
  // exact match-chain shape tolg lowers to a single per-variable Hotmux.
  const uint32_t           seq = ++inline_seq_;
  std::vector<std::string> conds;
  conds.reserve(n - 1);
  for (size_t k = 0; k + 1 < n; ++k) {
    std::string cmp = std::format("%dsel{}_{}", seq, k);
    emit_staging_op(N::create_eq(), cmp, {Lnast_node::create_ref(idx_ref), Lnast_node::create_const(std::to_string(k))});
    conds.push_back(std::move(cmp));
  }
  emit_push(N::create_unique_if());
  for (size_t k = 0; k + 1 < n; ++k) {
    emit_leaf(Lnast_node::create_ref(conds[k]));  // arm condition
    emit_push(N::create_stmts());                 // arm body: dst = elems[k]
    emit_push(N::create_store());
    emit_leaf(Lnast_node::create_ref(dst));
    emit_leaf(elems[k]);
    emit_pop();  // store
    emit_pop();  // stmts
  }
  emit_push(N::create_stmts());  // else arm: dst = elems[n-1]
  emit_push(N::create_store());
  emit_leaf(Lnast_node::create_ref(dst));
  emit_leaf(elems[n - 1]);
  emit_pop();  // store
  emit_pop();  // stmts (else)
  emit_pop();  // unique_if
  return true;
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
        // A CONSTANT-valued generic (`m<3>`): a body reference is a VALUE
        // (`r = a + N`) — substitute the literal. D already rejected a constant
        // in a type slot, so a body `ref N` is always a value here (todo 3g F).
        if (!gb.const_text.empty()) {
          dst->add_child(dst_parent, Lnast_node::create_const(gb.const_text));
          return;
        }
        // A LAMBDA-valued generic (`m<inc>`): a body reference is a CALLEE
        // (`F(v)`) — rename it to the bound function so the clone dispatches.
        if (!gb.func_name.empty()) {
          dst->add_child(dst_parent, Lnast_node::create_ref(gb.func_name));
          return;
        }
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
  clone->set_verilog_origin(tmpl->is_verilog_origin());
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

void uPass_runner::emit_named_instance_call(const std::string& dst, const std::string& callee_ref,
                                            const std::string& inst_name,
                                            const std::vector<std::pair<std::string, Lnast_node>>& actuals) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-spec");
  auto s    = std::make_shared<Lnast>(body, "inl-spec");
  auto root = s->set_root(Lnast_ntype::create_func_call());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));
  s->add_child(root, Lnast_node::create_ref(callee_ref));  // callee → tolg makes the Sub
  // Re-emit the explicit instance name (`name=`) so the Sub keeps its hierarchy.
  if (!inst_name.empty()) {
    auto in = s->add_child(root, Lnast_ntype::create_store());
    s->add_child(in, Lnast_node::create_ref("__inst_name"));
    s->add_child(in, Lnast_node::create_const(inst_name));
  }
  // Named actuals: `store(port, value)` per binding, so tolg wires by name (never
  // by declaration order — a type-distinguished unnamed actual is resolved by
  // KIND, which position cannot reproduce).
  for (const auto& [port, val] : actuals) {
    auto st = s->add_child(root, Lnast_ntype::create_store());
    s->add_child(st, Lnast_node::create_ref(port));
    s->add_child(st, val);
  }
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // re-walked: an all-named call declines straight through to tolg (no re-canonicalize)
  flush_deferred_emits();
  lm->pop_source();
}

absl::flat_hash_map<std::string, uPass_runner::Generic_bind> uPass_runner::resolve_generic_binds(
    const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io, const std::vector<Lnast_node>& param_val,
    const std::vector<bool>& param_set, std::size_t nbind, const std::vector<Generic_actual>& explicit_generics,
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

  // Resolve ONE explicit `<…>` argument. Unlike inference (types only), an
  // explicit bind may be a type, a compile-time CONSTANT (`f<3>`), or a LAMBDA
  // name (`f<inc>`) — todo 3g A. A leading digit / quote or a bool keyword can
  // never be a type or lambda identifier, so classify on the first character;
  // otherwise a registry function name is a lambda bind, and anything else is a
  // type (named user type or a `'type'` tmp carrying an envelope).
  const auto bind_of_explicit_arg = [&](const std::string& s, std::string from) -> Generic_bind {
    const char c0 = s.empty() ? '\0' : s.front();
    if (c0 == '\'' || c0 == '"') {
      Generic_bind gb;
      gb.from       = std::move(from);
      gb.kind       = Io_kind::string;
      gb.const_text = s;
      return gb;
    }
    if (s == "true" || s == "false") {
      Generic_bind gb;
      gb.from       = std::move(from);
      gb.kind       = Io_kind::boolean;
      gb.max        = *Dlop::from_pyrope("1");
      gb.min        = *Dlop::from_pyrope("0");
      gb.const_text = s;
      return gb;
    }
    if (std::isdigit(static_cast<unsigned char>(c0)) != 0) {
      Generic_bind gb;
      gb.from = std::move(from);
      gb.kind = Io_kind::integer;
      if (auto d = Dlop::from_pyrope(s)) {
        gb.max = *d;  // a constant pins its own value as the type envelope (D)
        gb.min = *d;
      }
      gb.const_text = s;
      return gb;
    }
    if (lookup_callee(s) != nullptr) {
      Generic_bind gb;
      gb.from      = std::move(from);
      gb.func_name = s;
      return gb;
    }
    return bind_of_type_name(s, std::move(from));
  };

  // A generic left unbound by the call takes its DECLARATION default (`<T,
  // N=1>`, todo 3g B): the default text is re-classified exactly like an
  // explicit `<…>` argument (type / constant / lambda).
  const auto& gdefaults    = callee->get_generic_defaults();
  const auto  apply_defaults = [&]() {
    for (std::size_t i = 0; i < gens.size(); ++i) {
      if (binds.count(gens[i]) != 0u) {
        continue;
      }
      const std::string def = (i < gdefaults.size()) ? gdefaults[i] : std::string{};
      if (!def.empty()) {
        binds[gens[i]] = bind_of_explicit_arg(def, std::format("the default for generic `{}`", gens[i]));
      }
    }
  };

  // D — kind validation: a generic bound to a CONSTANT (`f<3>`) or a LAMBDA
  // (`f<inc>`) may NOT stand where a type is required — a param/output `:G`
  // slot. A constant/lambda is not a type; silently ignoring the annotation
  // (compiling `f<3>(a:N)` to a plain passthrough) is the one wrong answer (todo
  // 3g D). Value/lambda/default uses (`a + N`, `F(v)`, `in2=N`) stay legal.
  const auto validate_kinds = [&]() {
    for (const auto& [g, gb] : binds) {
      if (gb.const_text.empty() && gb.func_name.empty()) {
        continue;  // a type bind is fine in a type slot
      }
      const auto in_type_slot = [&](const std::vector<Lnast_io_entry>& es) {
        return std::any_of(es.begin(), es.end(), [&](const Lnast_io_entry& e) { return e.type_name == g; });
      };
      if (in_type_slot(io.inputs) || in_type_slot(io.outputs)) {
        const std::string what = gb.func_name.empty() ? "constant" : "lambda";
        fcall_arg_fail(
            call_span,
            "fcall-generic-kind",
            std::format("generic `{}` of `{}` is bound to a {} but is used as a type (a `:{}` param/output) — a {} is not a type",
                        g, callee_name, what, g, what),
            "bind a type there (e.g. `f<u8>`), or use the generic only as a value/lambda in the body");
      }
    }
  };

  // 1) Explicit `<…>` bindings — named and/or positional, following the same
  // argument-naming rules as call arguments (todo 3g C). NAMED binds (`f<T=u8>`)
  // bind by name; POSITIONAL binds fill the remaining generics in declaration
  // order. A PARTIAL list is legal when the still-unbound generics carry
  // declaration defaults (todo 3g B). Binding MORE than declared is an error.
  if (!explicit_generics.empty()) {
    if (explicit_generics.size() > gens.size()) {
      fcall_arg_fail(
          call_span,
          "fcall-generic-arity",
          std::format("`{}` declares {} generic parameter(s) but the call binds {}", callee_name, gens.size(), explicit_generics.size()),
          "bind at most one type per generic name");
    }
    // Named binds first: validate the name is a generic and not already bound.
    for (const auto& ga : explicit_generics) {
      if (ga.name.empty()) {
        continue;
      }
      if (std::find(gens.begin(), gens.end(), ga.name) == gens.end()) {
        fcall_arg_fail(call_span,
                       "fcall-generic-name",
                       std::format("`{}` has no generic parameter `{}`", callee_name, ga.name),
                       "name one of the declared generics, or bind positionally");
      }
      if (binds.count(ga.name) != 0u) {
        fcall_arg_fail(call_span,
                       "fcall-generic-name",
                       std::format("generic `{}` of `{}` is bound more than once", ga.name, callee_name),
                       "bind each generic at most once");
      }
      binds[ga.name] = bind_of_explicit_arg(ga.value, std::format("the named bind `{}=…`", ga.name));
    }
    // Positional binds follow the SAME naming exceptions as call arguments
    // (06-functions.md §"Argument naming"), NOT declaration-order fill: a bare
    // name that matches a generic (exc 2), a single free generic (exc 1), or a
    // role (type vs value) that uniquely picks one free generic (exc 3). An
    // ambiguous positional bind (e.g. two free type generics `two<signed,string>`)
    // must be named `<Name=…>`. Role-matching also fixes the case where the
    // declaration order does not match the actuals' roles (`<N,T>` bound `<u4,3>`).
    const auto is_type_generic = [&](const std::string& g) {
      const auto in_slot = [&](const std::vector<Lnast_io_entry>& es) {
        return std::any_of(es.begin(), es.end(), [&](const Lnast_io_entry& e) { return e.type_name == g; });
      };
      return in_slot(io.inputs) || in_slot(io.outputs);
    };
    for (const auto& ga : explicit_generics) {
      if (!ga.name.empty()) {
        continue;  // named binds already applied above
      }
      const Generic_bind cand         = bind_of_explicit_arg(ga.value, std::format("the explicit `<…>` argument `{}`", ga.value));
      const bool         cand_is_type = cand.const_text.empty() && cand.func_name.empty();  // type bind vs value/lambda
      std::size_t        target       = gens.size();
      // Exception 2: a bare identifier whose text matches an unbound generic name.
      if (const auto it = std::find(gens.begin(), gens.end(), ga.value); it != gens.end()) {
        const auto gi = static_cast<std::size_t>(it - gens.begin());
        if (binds.count(gens[gi]) == 0u) {
          target = gi;
        }
      }
      if (target >= gens.size()) {
        std::vector<std::size_t> free;
        for (std::size_t i = 0; i < gens.size(); ++i) {
          if (binds.count(gens[i]) == 0u) {
            free.push_back(i);
          }
        }
        if (free.size() == 1) {
          target = free[0];  // Exception 1: the only free generic.
        } else {
          // Exception 3: the actual's role (type vs value) selects exactly one.
          std::size_t match = gens.size();
          std::size_t count = 0;
          for (const auto i : free) {
            if (is_type_generic(gens[i]) == cand_is_type) {
              match = i;
              ++count;
            }
          }
          if (count == 1) {
            target = match;
          }
        }
      }
      if (target >= gens.size()) {
        // ga.value is a lowered temp/const by now, not the source text, so name
        // the ambiguity by role rather than by the (unhelpful) internal ref.
        fcall_arg_fail(call_span,
                       "fcall-generic-unnamed",
                       std::format("a positional {} generic argument to `{}` is ambiguous and must be named",
                                   cand_is_type ? "type" : "value", callee_name),
                       "name it (`<GenericName=…>`) — a positional generic bind resolves only when a single generic, a "
                       "matching name, or a unique role (type vs value) selects the target");
      }
      binds[gens[target]] = cand;
    }
    apply_defaults();
    // With an explicit list present, an unbound generic with no default is a
    // hard miss (we do not fall back to actual-type inference here).
    for (const auto& g : gens) {
      if (binds.count(g) == 0u) {
        fcall_arg_fail(call_span,
                       "fcall-generic-arity",
                       std::format("generic `{}` of `{}` is unbound and has no default", g, callee_name),
                       std::format("bind it in the `<…>` list or declare a default `<{}=…>`", g));
      }
    }
    validate_kinds();
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
        cand.max  = upass::max_from_bits(ci->bits, ci->is_signed);
        cand.min  = upass::min_from_bits(ci->bits, ci->is_signed);
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
  // A generic that inference did not reach falls to its declaration default
  // (`comb addn<T, N=1>(a:T)` called `addn(a=x)` — T inferred, N defaulted).
  apply_defaults();
  validate_kinds();
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
        sp = {true, upass::signed_max_from_bits(ci->bits), upass::signed_min_from_bits(ci->bits), {}};
        suffix.push_back("s" + std::to_string(ci->bits));
      } else {
        sp = {true, upass::unsigned_max_from_bits(ci->bits), *Dlop::from_pyrope("0"), {}};
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
    // A half-open `int(max=99)` param carries a range but bits==0; treat it as
    // already-typed too so the actual's type isn't injected over it (cat 1).
    const bool  already_typed
        = !is_generic && (e.bits > 0 || e.has_range || !e.type_name.empty() || e.kind == Io_kind::boolean);
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

  // Non-type binds — a CONSTANT (`m<3>`) or LAMBDA (`m<inc>`) generic used as a
  // value/callee in the body (todo 3g F) rides no typed port, so it never
  // reached `suffix` above. Append a stable `G_<token>` per such bind, in
  // declaration order, so two distinct binds (`m<3>` vs `m<5>`, `m<inc>` vs
  // `m<dec>`) produce distinct module names — otherwise the name-keyed dedup
  // silently merges them onto one clone and mis-wires the second call site.
  for (const auto& g : gens) {
    auto it = gbinds.find(g);
    if (it == gbinds.end()) {
      continue;
    }
    const std::string tok = !it->second.const_text.empty() ? it->second.const_text : it->second.func_name;
    if (tok.empty()) {
      continue;  // a type bind already contributed its width/name token
    }
    std::string clean;
    clean.reserve(tok.size());
    for (char ch : tok) {
      clean.push_back((std::isalnum(static_cast<unsigned char>(ch)) != 0) ? ch : '_');
    }
    suffix.push_back(std::format("{}_{}", g, clean));
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

  // Actuals wired to the clone's ports BY NAME, in io order: fixed params, then
  // the var-arg leftovers (positional then named) → the synthesized vname__*
  // ports. Emitting named `store(port, value)` (rather than positional) keeps
  // the specialized call consistent with the source's own naming — tolg binds
  // every instance actual by name, and its positional path is left only for a
  // genuine user single-arg call.
  std::vector<std::pair<std::string, Lnast_node>> actuals;
  actuals.reserve(nbind + vports.size());
  for (std::size_t i = 0; i < nbind; ++i) {
    actuals.emplace_back(io.inputs[i].name, param_val[i]);
  }
  std::size_t vk = 0;
  for (const auto& a : vararg_pos) {
    actuals.emplace_back(std::format("{}__{}", vname, vk++), a);
  }
  for (const auto& [key, a] : vararg_named) {
    actuals.emplace_back(std::format("{}__{}", vname, key), a);
  }
  emit_named_instance_call(dst_name, mangled, /*inst_name=*/"", actuals);
  return true;
}

// ── overload-gathering call dispatch (2f-overload) ────────────────────────────

bool uPass_runner::gather_actuals(bool drop_ufcs_receiver, std::vector<Actual>& actuals,
                                  std::vector<Generic_actual>& explicit_generics) {
  // Cursor MUST be on the callee ref (the func_call's 2nd child); the actuals
  // are its following siblings. The cursor is saved/restored here so this can
  // be called twice (overload probe + real bind) without disturbing the caller.
  const auto entry = lm->save_cursor();
  gathered_inst_name_.clear();  // reset; set below if `__inst_name` is present

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
      // Call-site instance name (`alu::[name=X](…)`) — consumed here, never an
      // actual. The value is a const string literal; remember it as this call's
      // hierarchical-prefix level (try_inline reads gathered_inst_name_).
      if (a.key == call_inst_name_marker) {
        if (!lm->move_to_sibling()) {
          shape_ok = false;
          lm->restore_cursor(here);
          break;
        }
        gathered_inst_name_ = std::string(lm->current_raw_text());
        lm->restore_cursor(here);
        continue;
      }
      // Explicit generic binding — consumed here, never an actual. The value
      // ref is frame-renamed text, so current_text(), not raw. A NAMED bind
      // (`f<T=u8>`, todo 3g C) carries the target generic name in a trailing
      // const child; read it RAW (a source name, never frame-renamed).
      if (a.key == call_generic_arg_marker) {
        if (!lm->move_to_sibling()) {
          shape_ok = false;
          lm->restore_cursor(here);
          break;
        }
        Generic_actual ga;
        ga.value = std::string(lm->current_text());
        if (lm->move_to_sibling()) {
          ga.name = std::string(lm->current_raw_text());
        }
        explicit_generics.push_back(std::move(ga));
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
  const bool        has_self   = nbind > 0 && io.inputs[0].name == "self";

  // Run the SHARED binding ladder in probe mode: commit=false returns false on
  // any rejection (no fatal diag) and forbids a tuple actual from binding a lone
  // scalar param. Same code as the real bind, so the two cannot drift;
  // callee_name/call_span are unused on the non-fatal path.
  std::vector<Lnast_node>                         param_val;
  std::vector<bool>                               param_set;
  std::vector<std::string>                        param_func;
  std::vector<Lnast_node>                         vararg_pos;
  std::vector<std::pair<std::string, Lnast_node>> vararg_named;
  if (!bind_call_actuals(io, actuals, is_ctor_call, /*commit=*/false, /*callee_name=*/{}, livehd::diag::Span{}, param_val,
                         param_set, param_func, vararg_pos, vararg_named)) {
    return false;
  }

  // Comptime scalar kind of an actual — the same actual_node_kind() the real
  // bind uses, so probe and commit agree on kind.
  auto classify = [&](const Lnast_node& node) { return actual_node_kind(node); };

  // Every non-self FIXED parameter must be bound, EXCEPT a param with a declared
  // default (io_meta.has_default): an omitted default takes the body-prologue
  // value, mirroring the real bind (try_inline_func_call, ~L3527). io_meta DOES
  // carry defaults, so a defaulted-param candidate must not be rejected here.
  for (std::size_t i = (has_self ? 1 : 0); i < nbind; ++i) {
    if (!param_set[i] && !io.inputs[i].has_default) {
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

void uPass_runner::collect_return_consumption(const upass::Lnast_manager::Cursor_state& fcall_cursor, std::string_view dst_name,
                                              absl::flat_hash_set<std::string>& req_fields, bool& whole_used) {
  // The destructure `(p1,p2) = f()` lowers to `fcall(dst, …)` followed by
  // sibling `tuple_get(tmp, dst, 'p1')` / `tuple_get(tmp, dst, 'p2')` picks
  // (see the parse dump); a whole bind `c = f()` lowers to `store(c, dst)` and
  // an operand use `c = f()+1` to `plus(c, dst, 1)`. So learn the result shape
  // by walking the fcall's following siblings once: a tuple_get whose SRC (2nd
  // child) is our dst contributes a required field; any other node that names
  // dst as a child marks a whole-value use.
  const auto here = lm->save_cursor();
  lm->restore_cursor(fcall_cursor);  // cursor on the func_call node
  while (lm->move_to_sibling()) {
    const bool is_tget = Lnast_ntype::is_tuple_get(lm->get_raw_ntype());
    const auto node    = lm->save_cursor();
    if (!lm->move_to_child()) {
      lm->restore_cursor(node);
      continue;
    }
    // children in order: [dst, src/operand, field/operand, …]
    std::size_t idx        = 0;
    bool        names_dst  = false;  // dst_name appears as a non-leading child
    std::size_t dst_at_idx = std::string::npos;
    do {
      if (idx > 0 && Lnast_ntype::is_ref(lm->get_raw_ntype()) && lm->current_text() == dst_name) {
        names_dst  = true;
        dst_at_idx = idx;
      }
      ++idx;
    } while (lm->move_to_sibling());
    if (is_tget && dst_at_idx == 1) {
      // tuple_get(tmp, dst, field): read the field const (3rd child).
      lm->restore_cursor(node);
      lm->move_to_child();      // tmp
      lm->move_to_sibling();    // dst
      if (lm->move_to_sibling() && Lnast_ntype::is_const(lm->get_raw_ntype())) {
        if (auto v = Dlop::from_pyrope(lm->current_text()); v && !v->is_invalid()) {
          req_fields.insert(v->to_field());
        }
      }
    } else if (names_dst) {
      whole_used = true;
    }
    lm->restore_cursor(node);
  }
  lm->restore_cursor(here);
}

bool uPass_runner::return_matches(const Lnast_tree_io& io, const absl::flat_hash_set<std::string>& req_fields, bool whole_used) {
  // Regroup the FLATTENED output leaves into LOGICAL outputs exactly as the real
  // epilogue does (see the output-binding code: a tuple output `p:(first,
  // second)` is io.outputs leaves `p.first`,`p.second`, one logical output `p`).
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> logical;  // lname → top-level sub-field names (case-sensitive)
  for (const auto& o : io.outputs) {
    const auto  dp    = o.name.find('.');
    std::string lname = dp == std::string::npos ? o.name : o.name.substr(0, dp);
    auto&       subs  = logical[lname];
    if (dp != std::string::npos) {
      const auto rest = o.name.substr(dp + 1);
      subs.insert(rest.substr(0, rest.find('.')));  // first sub-segment (one level)
    }
  }
  const std::size_t n_logical = logical.size();

  if (!req_fields.empty()) {
    // Destructure: every picked field must be bindable. Two shapes resolve:
    //   (a) ONE tuple output → dst IS that tuple, picks are its sub-fields;
    //   (b) N logical outputs → splat, picks must be among the output names.
    if (n_logical == 1) {
      const auto& subs = logical.begin()->second;  // sub-fields of the lone output
      for (const auto& f : req_fields) {
        if (!subs.contains(f)) {
          return false;  // a scalar (no sub-fields) or wrong-named tuple output
        }
      }
      return true;
    }
    for (const auto& f : req_fields) {
      if (!logical.contains(f)) {
        return false;  // no output supplies this destructure name (no by-order)
      }
    }
    return true;
  }

  if (whole_used) {
    // Whole bind to one destination: a lone logical output (scalar or tuple)
    // binds (name dropped); >1 outputs is the `multi-output-one-var` error, so
    // such a candidate cannot handle this call — skip it (try the next).
    return n_logical == 1;
  }
  // Result dropped (no consumer) — nothing to bind; stay permissive.
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
    actuals.push_back(Actual{.is_named = false, .is_ref_pass = false, .key = {}, .node = Lnast_node::create_const("0"), .func_name = {}});
    for (const auto& a : args) {
      actuals.push_back(Actual{.is_named = !a.key.empty(), .is_ref_pass = false, .key = a.key, .node = a.node, .func_name = {}});
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
  s->add_child(root, Lnast_node::create_ref(std::format("%ctor{}", ++inline_seq_)));
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
    livehd::diag::Span span = lm->current_span();
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
            dt.range_max = upass::max_from_bits(ie.bits, ie.is_signed);
            dt.range_min = upass::min_from_bits(ie.bits, ie.is_signed);
            elem_dt      = dt;
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

  // 2f-nil_diag — a condition that folds to nil is an illegal use of nil; a
  // genuinely runtime condition folds to nullopt (not nil) and stays verbatim.
  // An uncertainty-pinned poison nil (conditionally-driven var) is treated as
  // UNKNOWN, not an error (see the if-condition note above); a `const c = nil`
  // clears the uncertain mark on store, so it still errors.
  if (cval && !cval->is_invalid() && cval->is_nil()) {
    report_cond_nil("while");
    return;
  }

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
  auto while_span = [&]() { return lm->current_span(); };

  const bool saved_break = loop_break_hit_;
  loop_break_hit_        = false;
  ++loop_depth_;
  // Per-loop unroll cap. State-repeat (below) catches a frozen/cyclic condition
  // in O(1) iterations, but a DIVERGENT loop whose condition variable keeps
  // changing yet never reaches the exit (`while c != 10 { c -= 1 }` from below)
  // never repeats a state. This bounds such loops to a prompt error rather than
  // grinding to the shared fuel cap. 32k body-copies is already absurd for a
  // comptime unroll (it lowers to that many hardware copies), so no real loop
  // hits it — and a tiny runaway source now errors in seconds instead of taking
  // >60s to fold 100k trivial iterations first (a DoS-y footgun).
  constexpr std::size_t kMaxLoopUnroll = 32768;
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
  symbol_table_.pending_keys_by_root.clear();
  if (livehd::lsp_index::index().enabled()) {
    lsp_decl_hints().clear();  // LSP-side declare hints are per-run state too
  }
  const_parse_cache_.clear();
  symbol_table_.tget_origin.clear();
  symbol_table_.nil_seeded.clear();
  symbol_table_.uninitialized.clear();
  symbol_table_.field_touched.clear();

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
  const auto walk_t0 = std::chrono::steady_clock::now();
  process_lnast();
  // Print the completion marker the dependency / shared-pass tests
  // (upass_noop_first_iter_test.sh, upass_lnast_shared_scan_test.sh,
  // upass_lnast_shared_decide_test.sh) grep for.
  std::print("uPass - walk complete\n");
  if (dispatch_stats_) {
    // LIVEHD_UPASS_STATS breakdown. stderr, not stdout: run_step redirects
    // stdout into the per-step log; stats should land on the terminal.
    const auto walk_s
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - walk_t0).count() / 1e3;
    double dispatched_s = 0;
    for (const auto& entry : upasses) {
      dispatched_s += static_cast<double>(entry.stat_ns) / 1e9;
    }
    std::print(stderr, "uPass stats [{}]: walk {:.1f}s, dispatched {:.1f}s ({:.0f}%), runner-core {:.1f}s\n",
               lm->get_top_module_name(), walk_s, dispatched_s, walk_s > 0 ? 100.0 * dispatched_s / walk_s : 0.0,
               walk_s - dispatched_s);
    std::vector<const Pass_entry*> sorted;
    sorted.reserve(upasses.size());
    for (const auto& entry : upasses) {
      sorted.push_back(&entry);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Pass_entry* a, const Pass_entry* b) { return a->stat_ns > b->stat_ns; });
    for (const auto* entry : sorted) {
      std::print(stderr, "uPass stats [{}]:   {:<12} {:9.1f}s  ({:4.1f}% of walk, {} dispatches)\n",
                 lm->get_top_module_name(), entry->name, static_cast<double>(entry->stat_ns) / 1e9,
                 walk_s > 0 ? 100.0 * (static_cast<double>(entry->stat_ns) / 1e9) / walk_s : 0.0, entry->stat_calls);
    }
  }

  // Post-walk DCE on staging — removes definition statements (assign,
  // tuple_add, attr_set, etc.) whose dst has no surviving downstream
  // reader. Constprop is conservative about multi-entry tuple bundles
  // (their dst can't fold via fold_ref's single-value return), so it
  // emits orphan tuple_add+assign+attr_set chains for fully-constant
  // tuples even when every consumer was already folded away. The DCE
  // cleans those up. Skipped (with the whole staging build) when nothing
  // consumes the rewritten LNAST — see set_materialize().
  if (materialize_) {
    const auto dce_t0 = std::chrono::steady_clock::now();
    dead_code_eliminate_staging();
    if (dispatch_stats_) {
      std::print(stderr, "uPass stats [{}]:   staging-DCE {:9.1f}s\n", lm->get_top_module_name(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dce_t0).count() / 1e3);
    }
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
  // Synthesis-region marker (2opt-freq B): `attr_set %__region_N '__region' …`
  // opens a block-scoped partition region for tolg. Its %-target never has
  // readers by construction, so without this exemption DCE would silently
  // delete the user's block annotation.
  if (staging.get_name(key) == "__region") {
    return true;
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

  // string_view keys throughout: ref names resolve into the staging name pool
  // and io_meta entry strings, both stable for the whole DCE — no per-ref
  // std::string allocation. The mut/reg declare roots are collected during the
  // main scan below (declares are statement-level nodes the scan visits
  // anyway); droppability is therefore checked at SEED/KILL time, not at scan
  // time, which folds the old declare pre-walk into the single scan.
  absl::flat_hash_set<std::string_view> protected_names;
  for (const auto& e : io.inputs) {
    protected_names.insert(e.name);
  }
  for (const auto& e : io.outputs) {
    protected_names.insert(e.name);
  }

  // Compute the live-statement set via worklist liveness on the
  // staging tree, then rebuild a fresh tree containing only the live
  // statements. Avoiding in-place delete_subtree dodges HHDS's
  // pre-order iterator transiently yielding default-constructed
  // Node_class instances for deleted slots — downstream lnastfmt walks
  // would crash on the unchecked `get_type` that follows.
  //
  // Worklist, not fixed point: the previous version re-walked the WHOLE tree
  // (use recount + candidate rescan) once per dead "layer", so a K-deep dead
  // def-use chain cost K full-tree walks — Rob.Rob (240k ifs, ~5M staging
  // nodes) spent ~6 minutes here, dominating the whole compile. One scan
  // builds the use counts, the droppable-def index, and each droppable
  // statement's read list; killing a statement then decrements only ITS OWN
  // reads and enqueues the defs of names that hit zero — O(tree) total, same
  // fixed point (kill order does not change the final zero-use set).

  // Set of statement nids in the staging tree that should be dropped.
  absl::flat_hash_set<int64_t> dead_stmts;
  const auto                   dce_t_scan = std::chrono::steady_clock::now();
  {
    absl::flat_hash_map<std::string_view, int>                  use_count;   // refs in non-LHS positions, per name
    absl::flat_hash_map<std::string_view, std::vector<int64_t>> defs_of;     // name -> candidate stmt-level def nids
    absl::flat_hash_map<int64_t, std::vector<std::string_view>> stmt_reads;  // candidate def nid -> subtree read names

    // One recursive scan. A statement-level def is a def-producing node whose
    // direct parent is a `stmts` block — nested `assign` nodes living inside a
    // tuple_add (field-label payload, not a real statement) are payload, and
    // attr_set with `type`=mut|reg is a keepalive marker (storage class
    // declarations survive even when the name has no surviving readers).
    // Temporary defs (`___N`) are always droppable when unread. A user-named
    // def is droppable only when it is NOT a root: not function IO (io_meta)
    // and not a mut/reg state element. This is the io-root rule —
    // `protected_names` holds exactly those roots (mut/reg declares collected
    // right here in the scan; the droppable test runs at seed/kill time, when
    // the set is complete). Outputs are the motivating case for the io_meta
    // half (written, never read in-tree, so they'd be mis-swept without the
    // root); the var-arg reconstruction `args = (args__0, …)` is the
    // motivating case for the drop (non-IO, non-state, zero readers once the
    // for-loop unrolled into port reads).
    //
    // `active` = the enclosing candidate def stmt's nid (0 outside one): every
    // non-LHS ref in its subtree lands in its read list, so its death can
    // un-count exactly the reads it contributed. A ref that is the first child
    // of a def-producing node is that node's dst, at ANY depth (a nested
    // assign-in-tuple_add's child0 too) — never a read.
    //
    // `on_spine` mirrors the rebuild's is_structural descent (top/stmts/if/
    // while/for — NOT unique_if): the rebuild only dead-checks statements on
    // that spine and copies everything under a unique_if as payload, so a def
    // under a unique_if arm must NOT be a kill candidate. Killing it anyway
    // (as the old fixed point did) decremented its reads while the copy kept
    // the statement — a spine def whose only reader was that kept payload
    // could then be dropped out from under it.
    std::function<void(const Lnast_nid&, int64_t, bool)> scan = [&](const Lnast_nid& n, int64_t active, bool on_spine) {
      const auto nt       = staging->get_type(n);
      const bool is_stmts = nt == N::Lnast_ntype_stmts;
      for (auto c = n.first_child(); c.is_valid(); c = c.next_sibling()) {
        const auto ct = staging->get_type(c);
        if (ct == N::Lnast_ntype_ref) {
          const bool is_lhs = dce_is_def_producing(nt) && c.is_first_child();
          if (!is_lhs) {
            const auto nm = staging->get_name(c);
            ++use_count[nm];
            if (active != 0) {
              stmt_reads[active].push_back(nm);
            }
          }
          continue;
        }
        int64_t next_active = active;
        if (active == 0 && is_stmts) {
          if (ct == N::Lnast_ntype_declare) {
            // mut/reg storage-class declares are the named-var roots
            // (collected everywhere, spine or not — roots are name-level).
            auto nm = staging->get_first_child(c);
            if (nm.is_valid() && staging->get_type(nm) == N::Lnast_ntype_ref) {
              if (auto ty = staging->get_sibling_next(nm); ty.is_valid()) {
                if (auto mode = staging->get_sibling_next(ty);
                    mode.is_valid() && staging->get_type(mode) == N::Lnast_ntype_const) {
                  const auto m = staging->get_name(mode);
                  if (m == "mut" || m == "reg") {
                    protected_names.insert(staging->get_name(nm));
                  }
                }
              }
            }
          } else if (on_spine && dce_is_def_producing(ct) && !dce_is_keepalive_attr_set(*staging, c)) {
            if (auto fc = staging->get_first_child(c); fc.is_valid() && staging->get_type(fc) == N::Lnast_ntype_ref) {
              const auto id = c.get_class_index().value;
              defs_of[staging->get_name(fc)].push_back(id);
              next_active = id;
            }
          }
        }
        const bool child_structural = ct == N::Lnast_ntype_top || ct == N::Lnast_ntype_stmts || ct == N::Lnast_ntype_if
                                      || ct == N::Lnast_ntype_while || ct == N::Lnast_ntype_for;
        scan(c, next_active, on_spine && child_structural);
      }
    };
    scan(staging->get_root(), 0, true);

    const auto droppable = [&](std::string_view name) {
      return Lnast::is_tmp(name) || (allow_named_drop && !protected_names.contains(name));
    };
    std::vector<int64_t> work;
    for (const auto& [name, ids] : defs_of) {
      if (!droppable(name)) {
        continue;
      }
      const auto it = use_count.find(name);
      if (it == use_count.end() || it->second == 0) {
        work.insert(work.end(), ids.begin(), ids.end());
      }
    }
    while (!work.empty()) {
      const auto id = work.back();
      work.pop_back();
      if (!dead_stmts.insert(id).second) {
        continue;
      }
      const auto rit = stmt_reads.find(id);
      if (rit == stmt_reads.end()) {
        continue;
      }
      for (const auto nm : rit->second) {
        if (auto uit = use_count.find(nm); uit != use_count.end() && --uit->second == 0 && droppable(nm)) {
          if (auto dit = defs_of.find(nm); dit != defs_of.end()) {
            work.insert(work.end(), dit->second.begin(), dit->second.end());
          }
        }
      }
    }
  }

  // Empty stmts wrappers. Droppable ONLY when the wrapper is a block-scope
  // stmts sitting directly inside another `stmts` block — that is the noise
  // constprop's dead-branch elimination leaves behind. An empty stmts that
  // is a POSITIONAL child of an `if`/`for`/`while` (an arm body / loop
  // body) must be preserved: an if node's children are (cond, stmts) pairs
  // plus an optional trailing else-stmts, so deleting an empty arm body
  // SHIFTS every following sibling — e.g. `if a {…} elif b {} else {…}`
  // would lose the empty `elif b` body and slide the `else` into the `elif`
  // slot. For a conditional reg write that turns the intended hold into a
  // garbage write (the `else` value lands on the elif path), which is
  // exactly the no-auto-hold trap. Post-order, so a wrapper whose children
  // were themselves just-emptied wrappers resolves inner-first (a wrapper's
  // death frees no reads — everything under it is already dead — so this
  // cannot re-enable the def worklist above).
  {
    // stmts wrappers only live on the structural spine (top/stmts/if/while/
    // for arms — payload ops never nest a stmts), so the sweep skips payload
    // subtrees entirely instead of re-walking the whole tree. Same spine as
    // the rebuild's is_structural (unique_if is payload there, and nothing
    // under it is a kill candidate anyway).
    const auto structural = [](Lnast_ntype::Lnast_ntype_int t) {
      return t == N::Lnast_ntype_top || t == N::Lnast_ntype_stmts || t == N::Lnast_ntype_if || t == N::Lnast_ntype_while
             || t == N::Lnast_ntype_for;
    };
    std::function<void(const Lnast_nid&)> sweep_wrappers = [&](const Lnast_nid& n) {
      const bool n_is_stmts = staging->get_type(n) == N::Lnast_ntype_stmts;
      for (auto c = n.first_child(); c.is_valid(); c = c.next_sibling()) {
        const auto ct = staging->get_type(c);
        if (!structural(ct)) {
          continue;
        }
        sweep_wrappers(c);
        if (!n_is_stmts || ct != N::Lnast_ntype_stmts) {
          continue;
        }
        bool has_live_child = false;
        for (auto gc = c.first_child(); gc.is_valid(); gc = gc.next_sibling()) {
          if (!dead_stmts.contains(gc.get_class_index().value)) {
            has_live_child = true;
            break;
          }
        }
        if (!has_live_child) {
          dead_stmts.insert(c.get_class_index().value);
        }
      }
    };
    sweep_wrappers(staging->get_root());
  }
  const auto dce_t_marked = std::chrono::steady_clock::now();

  if (dead_stmts.empty()) {
    return;
  }

  // lg-only flows (dce:mark — nothing downstream keeps the LNAST): record the
  // dead set on the staging Lnast for lnast.tolg's lower_stmts to skip, and
  // pay NO tree rebuild at all. LNAST-emitting flows fall through to the
  // rebuild so prp_writer / ln: / dumps see a clean tree.
  if (dce_mark_only_) {
    if (dispatch_stats_) {
      std::print(stderr, "uPass stats [{}]:   DCE mark {} dead stmts in {:.2f}s (no rebuild)\n", lm->get_top_module_name(),
                 dead_stmts.size(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(dce_t_marked - dce_t_scan).count() / 1e3);
    }
    staging->set_dce_dead_stmts(std::move(dead_stmts));
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

  if (dispatch_stats_) {
    std::print(stderr, "uPass stats [{}]:   DCE mark {} dead stmts in {:.2f}s, rebuild {:.2f}s\n", lm->get_top_module_name(),
               dead_stmts.size(),
               std::chrono::duration_cast<std::chrono::milliseconds>(dce_t_marked - dce_t_scan).count() / 1e3,
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dce_t_marked).count()
                   / 1e3);
  }
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
                .span     = lm->current_span(),
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
                .span     = lm->current_span(),
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
      // 2f-mem_comptime_init — a reg-array declare whose init is a ref to a
      // fully-comptime bundle: materialize it into nested tuple_add literals
      // and re-emit the declare (tolg only resolves literal tuple_adds).
      if (try_materialize_array_init()) {
        break;
      }
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
      // task 2n: a reg/wire declare's stores are never symbolically bound, so
      // the push path records no def entry for them — record the declaration
      // site itself (now that the type/mode facts are baked). Non-state
      // declares record here too; the init store re-records with bw facts.
      // The declared type/mode is also read straight off the node into
      // lsp_decl_hints: a reg FIELD's facts never reach the symbol table (its
      // root binding doesn't exist when bake runs, so bake drops them).
      if (livehd::lsp_index::index().enabled() && lm->has_child()) {
        const auto  here = lm->save_cursor();
        std::string nm;
        lm->move_to_child();
        if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
          nm = std::string(lm->current_text());
          Symbol_table::Pending_decl hint;
          if (lm->move_to_sibling()) {  // TYPE slot (mirrors bake_decl_pre_step)
            const auto t = lm->get_raw_ntype();
            if (Lnast_ntype::is_prim_type_int(t)) {
              hint.kind = upass::Kind::integer;
              if (lm->move_to_child()) {
                if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
                  if (auto v = Dlop::from_pyrope(lm->current_text()); v->is_integer()) {
                    hint.decl_max = *v;
                  }
                }
                if (lm->move_to_sibling() && Lnast_ntype::is_const(lm->get_raw_ntype())) {
                  if (auto v = Dlop::from_pyrope(lm->current_text()); v->is_integer()) {
                    hint.decl_min = *v;
                  }
                }
                lm->move_to_parent();
              }
            } else if (Lnast_ntype::is_prim_type_bool(t)) {
              hint.kind = upass::Kind::boolean;
            } else if (Lnast_ntype::is_prim_type_string(t)) {
              hint.kind = upass::Kind::string;
            }
            if (lm->move_to_sibling() && Lnast_ntype::is_const(lm->get_raw_ntype())) {  // MODE slot
              const auto txt = lm->current_text();
              if (txt == "reg" || txt.substr(0, 4) == "reg ") {
                hint.mode = upass::Mode::reg_kind;
              }
            }
          }
          if (hint.kind != upass::Kind::unknown || hint.mode != upass::Mode::unknown || !hint.decl_max.is_invalid()) {
            lsp_decl_hints()[nm] = std::move(hint);
          }
        }
        lm->restore_cursor(here);
        if (!nm.empty()) {
          record_lsp_def(nm);
        }
      }
      // Inside an inlined comb body, stamp the hierarchical instance-path prefix
      // on each reg/latch/memory declare so tolg names the flop/mem
      // hierarchically (`pipeB_ex_mem.reg_x`) — matching what a non-inlined Sub
      // instance would report via get_hier_name() (inline/not-inline parity).
      {
        std::string hier_target;
        if (!hier_prefix_stack_.empty() && lm->has_child()) {
          const auto here = lm->save_cursor();
          lm->move_to_child();                       // name (ref)
          std::string nm(lm->current_text());        // renamed (frame tag applied)
          bool        is_state = false;
          if (lm->move_to_sibling() && lm->move_to_sibling()) {  // skip type → mode const
            const auto mode = lm->current_raw_text();
            // Only state declares (reg/latch) get a hierarchical name. Combs
            // (the only thing inlined) cannot declare reg/latch today, so this
            // is dormant for combs; it fires for any future inlined body (e.g.
            // a `ref self` mod method) that carries state.
            is_state = mode == "reg" || mode == "latch" || mode.starts_with("reg ");
          }
          if (is_state && !nm.empty()) {
            hier_target = std::move(nm);
          }
          lm->restore_cursor(here);
        }
        process_verbatim(&upass::uPass::process_declare);
        if (!hier_target.empty()) {
          std::string prefix;
          for (const auto& lvl : hier_prefix_stack_) {
            if (!prefix.empty()) {
              prefix += '.';
            }
            prefix += lvl;
          }
          emit_inline_attr(hier_target, "__hier", prefix);
        }
      }
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

    // Function Call — 1i: if the callee is a comb body in the registry and
    // the call shape is supported, virtually splice it (prologue / push_source
    // body walk / epilogue). Otherwise fall back to the drop-candidate path so
    // constprop can fold built-in typecasts (int/uint/uNN/sNN) and cell-ops;
    // anything it declines stays un-folded and the statement is emitted.
    case Ntype::Lnast_ntype_func_call:
      if (!try_lower_wrap_sat() && !try_lower_typecast() && !try_inline_func_call() && !try_construct_call()) {
        process_drop_candidate(&upass::uPass::process_func_call, /*fold_all=*/false);
      }
      break;
    // does/has/case fold to a known boolean (or nil) → drop-candidate.
    A_OP(func_does)
    A_OP(func_equals)
    A_OP(func_has)
    A_OP(func_case)
    // `in` is always fully expanded here into `a==b[0] or … or a==b[N-1]` (tuple
    // shapes are comptime, so the element count is known); constprop folds the
    // constant comparisons. tolg never sees a func_in. A malformed node (no
    // operands) falls back to verbatim so it surfaces downstream rather than
    // vanishing.
    case Ntype::Lnast_ntype_func_in:
      if (!lower_in()) {
        emit_subtree_verbatim();
      }
      break;
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
  upass::Kind elem_kind = upass::Kind::unknown;  // array declares: the element KIND (integer vs boolean)
  std::string type_name;
  upass::Mode mode     = upass::Mode::unknown;
  bool        comptime = false;
  // A `()` COMPOSITE-TUPLE type slot — POSITIVE evidence that the name holds a
  // tuple, stamped onto the bundle below as value_kind.  An empty `()` bakes no
  // scalar "0" leaf, so it is shape-indistinguishable from an untyped runtime
  // scalar (`mut c = ack`, whose prim_type_none slot likewise bakes no leaf);
  // constprop's empty-tuple compare fold needs this flag to tell them apart.
  // NOT arrays: value_kind is also typecheck's assignment kind, and an array's
  // ELEMENTS are scalars (`mut buf:[4]u8 = 0`; `buf[i] = 3` would be rejected as
  // int-into-tuple).  An array only needs the flag when EMPTY, which is handled by
  // the tuple-literal stamp in typecheck (`mut a:[] = nil` carries no entries).
  bool tuple_type = false;

  if (lm->move_to_sibling()) {  // TYPE slot
    const auto t = lm->get_raw_ntype();
    tuple_type   = Lnast_ntype::is_comp_type_tuple(t);
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
        elem_kind = upass::Kind::integer;
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
      } else if (Lnast_ntype::is_prim_type_bool(lm->get_raw_ntype())) {
        // A bool array element has a fixed [0,1] envelope (the type node carries
        // no const bounds). Baking it keeps __elem_max/__elem_min a COMPLETE
        // is-array marker — sized integer and bool are the only valid array
        // element types (tolg lower_mem_declare) — which the dynamic-index
        // lowering relies on, and lets bitwidth check 1-bit element stores.
        // The KIND distinguishes `[]bool` from `[]u1` (both envelope [0,1]) so
        // an element store can reject a bool↔int kind mismatch (review cat 3).
        elem_kind = upass::Kind::boolean;
        elem_max  = *Dlop::create_integer(1);
        elem_min  = *Dlop::create_integer(0);
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
      // SCALAR named-type alias (`type PType = u10`; local OR imported
      // `pkg.PType`): borrow the alias's declared range so `:PType` constrains
      // width exactly like a literal `:u10`. The alias's own declare
      // (`declare(PType, prim_type_int(max,min), 'type')`) already baked its
      // "0"-entry envelope into PType's bundle; copy it here. A TUPLE/struct
      // named type carries fields (not a scalar "0" range) and is materialized
      // by constprop's named-type default path instead — its "0" entry has no
      // decl range, so this leaves decl_max/min unset and falls through.
      if (kind == upass::Kind::unknown && decl_max.is_invalid() && decl_min.is_invalid()) {
        if (auto tb = symbol_table_.get_bundle(type_name);
            tb && !tb->has_named_top() && tb->unnamed_top_count() <= 1 && tb->get_value_kind() != upass::Kind::tuple) {
          // A genuinely SCALAR named type (not a tuple/struct — those carry
          // fields and are materialized by constprop's named-type default path;
          // borrowing their leaked "0"-entry kind would mis-type the var as a
          // bare scalar, breaking `mut x:Complex = (…)`).
          const auto& te = tb->get_entry(bundle_path::of_string("0"));
          if (!te.decl_max.is_invalid() || !te.decl_min.is_invalid()) {
            decl_max = te.decl_max;
            decl_min = te.decl_min;
            kind     = te.kind == upass::Kind::unknown ? upass::Kind::integer : te.kind;
          } else if (te.kind == upass::Kind::boolean || te.kind == upass::Kind::string) {
            kind = te.kind;  // `type B = bool` / `type S = string`
          }
        }
      }
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
        } else if (tok == "wire") {
          mode = upass::Mode::wire_kind;  // 2c-wire — single-driver combinational net
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
    if (rb && rb->has_trivial(bundle_path::of_string(fpath))) {
      Bundle::Entry fe = rb->get_entry(bundle_path::of_string(fpath));
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
      rb->set(bundle_path::of_string(fpath), std::move(fe));
    } else if (rb != nullptr) {
      // Root binding is live but the field has no value yet: stash the fact and
      // apply it at the field's first write (or drop it when the root's
      // write-scope exits, via leave_scope). Indexed under the root so that
      // drop is O(scope vars), keeping apply_pending_field_facts O(N) total.
      auto& pf    = symbol_table_.pending_decl_facts[var];
      pf.kind     = kind;
      pf.mode     = mode;
      pf.decl_max = decl_max;
      pf.decl_min = decl_min;
      pf.comptime = comptime;
      symbol_table_.pending_keys_by_root[std::string(root)].emplace_back(var);
    }
    // else (rb == nullptr): the root has no live write-scope binding (an
    // undeclared / dangling SSA temp). The fact can never apply — there is no
    // bundle to write it onto — so do NOT stash it. The old code stashed these
    // and re-scanned them on every dispatched node (they never drained), which
    // made apply_pending_field_facts O(N^2) on large modules.
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
  if (tuple_type && bundle->get_value_kind() == upass::Kind::unknown) {
    bundle->set_value_kind(upass::Kind::tuple);  // `()` declare — a real aggregate, not a bare scalar
  }
  if (!type_name.empty()) {
    bundle->set_type_name(type_name);
  }
  // The "0" Entry carries the scalar facts. Only touch it when the bundle has
  // a scalar slot (or is empty) — writing a "0" leaf next to named tuple
  // fields would corrupt the shape.
  const bool has_scalar_slot = bundle->is_empty() || bundle->has_trivial(bundle_path::of_string("0"));
  const bool has_entry_facts = kind != upass::Kind::unknown || !decl_max.is_invalid() || !decl_min.is_invalid() || comptime;
  if (has_scalar_slot && has_entry_facts) {
    Bundle::Entry e = bundle->get_entry(bundle_path::of_string("0"));
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
    bundle->set(bundle_path::of_string("0"), std::move(e));
  }
  if (!elem_max.is_invalid()) {
    bundle->set_attr("__elem_max", elem_max);
  }
  if (!elem_min.is_invalid()) {
    bundle->set_attr("__elem_min", elem_min);
  }
  if (elem_kind != upass::Kind::unknown) {
    bundle->set_attr("__elem_kind", *Dlop::create_integer(static_cast<int64_t>(elem_kind)));
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
          if (src_b->has_trivial(bundle_path::of_string(fpath))) {
            Bundle::Entry fe = src_b->get_entry(bundle_path::of_string(fpath));
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
            src_b->set(bundle_path::of_string(fpath), std::move(fe));
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

void uPass_runner::report_cond_nil(std::string_view which) {
  // Exempt unrealized template bodies: unbound params fold nil placeholders;
  // the real error resurfaces when the body is realized at a call site.
  if (lm->get_lnast() && lm->get_lnast()->is_template()) {
    return;
  }
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::error,
      .code     = "nil-condition",
      .category = "type",
      .pass     = "upass.runner",
      .message  = std::format("a nil value is used as the `{}` condition", which),
      .span     = lm->current_span(),
      .hint     = "the condition folds to nil (a nil/uninitialized operand or an illegal operation)",
  });
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
      // 2f-nil_diag — a nil condition is an illegal use of nil in a conditional.
      // nil is NOT unknown: a genuinely runtime condition folds to nullopt (not
      // nil) and stays verbatim. A nil here means the condition came from a nil
      // operand / illegal op, which can never be a valid gate → compile error.
      if (cval && !cval->is_invalid() && cval->is_nil()) {
        report_cond_nil("if");
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
  // Bracket the arm walk so a pass (bitwidth) can range-union a variable's
  // value across the arms — each arm's store REPLACES the range, so without
  // this a var written in every arm would keep only the textually-last arm's
  // (too-narrow) range. saw_else && all_arms_uncertain == "the uncertain arms
  // partition every runtime path", which lets a var written in ALL arms drop
  // its pre-if value from the union (see notify_if_merge_end).
  for (auto& e : upasses) {
    e.pass->notify_if_merge_begin();
  }
  bool saw_else           = false;
  bool all_arms_uncertain = true;
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
        // A trailing else (a body-stmts with no cond of its own this round)
        // is the only stmts reached with last_was_cond == false.
        const bool is_trailing_else = !last_was_cond;
        last_was_cond           = false;
        if (dead) {
          all_arms_uncertain = false;  // a comptime-decided/dead path exists — not a clean mux
          emit_subtree_verbatim();
          continue;
        }
        if (is_trailing_else) {
          saw_else = true;
        }
        if (!uncertain) {
          all_arms_uncertain = false;  // a just_matched (comptime-true) arm — not a clean mux
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
  for (auto& e : upasses) {
    e.pass->notify_if_merge_end(saw_else && all_arms_uncertain);
  }
  emit_pop();
}
