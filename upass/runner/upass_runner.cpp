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
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_join.h"
#include "array_dim.hpp"
#include "call_resolver.hpp"
#include "decl_facts.hpp"
#include "default_prologue.hpp"
#include "diag.hpp"
#include "hash_util.hpp"
#include "io_port_rules.hpp"
#include "lnast_range.hpp"
#include "lsp_index.hpp"
#include "op_kind_rules.hpp"
#include "range_bits.hpp"
#include "ssa_demote.hpp"

namespace {

// Emit a function-call argument diagnostic (06-functions.md §"Argument naming")
// and abort the walk. The record is flushed to the JSONL sink crash-safe before
// the throw unwinds up to main's top-level catch, so the error-test harness sees
// it even though the throw aborts the remaining pipeline stages.
[[noreturn]] void fcall_arg_fail(const livehd::diag::Span& span, std::string_view code, const std::string& msg,
                                 std::string_view hint, std::string_view category = "name") {
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = std::string{code},
                                                     .category = std::string{category},
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
[[noreturn]] void in_op_fail(const livehd::diag::Span& span, std::string_view code, const std::string& msg, std::string_view hint) {
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
    case In_type::K::string : return "a string";
    case In_type::K::enumv  : return "an enum";
    case In_type::K::tuple  : return "a tuple";
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
    case upass::Kind::string : t.k = In_type::K::string; return t;
    case upass::Kind::integer: t.k = In_type::K::integer; return t;
    case upass::Kind::enumv  : t.k = In_type::K::enumv; return t;  // identity normally caught above
    default                  : break;
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
constexpr std::string_view call_spread_arg_marker  = "__spread_arg";
// prp2lnast emits a call-site instance name (`alu::[name=X](…)`) as a reserved
// `store(__inst_name, const "X")` actual; gather_actuals consumes it (never an
// argument) and try_inline uses it as the hierarchical-prefix level.
constexpr std::string_view call_inst_name_marker   = "__inst_name";
// Loop-iteration tag the unroller stamps on every call it emits from inside an
// unrolled body (`store(__inst_suffix, const "__li3")`, one `__li<ordinal>` per
// enclosing loop). tolg appends it to whatever instance name it derives, so the
// N body copies of one source call site are `<name>__li0`..`<name>__liN-1`
// instead of N instances all spelling the source name. (pass/lec's
// canon_flop_name folds a ROLLED loop's `u_loop_<n>__li<k>.` wrapper level onto
// this exact spelling, so the two lowerings pair state by name.)
constexpr std::string_view call_inst_suffix_marker = "__inst_suffix";

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
// walk — mirrors fcall_arg_fail's crash-safe flush-before-throw. The category is
// what the run's error class reports (lhd map_diag_category): `type` for a loop
// the source got wrong (it can never exit), `unsupported` for a valid loop
// LiveHD cannot lower yet — never the `internal` of an unmapped category.
[[noreturn]] void loop_fail(const livehd::diag::Span& span, std::string_view category, std::string_view code,
                            const std::string& msg, std::string_view hint) {
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = std::string{code},
                                                     .category = std::string{category},
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

  // The kernel and standalone runners share the same default: preserve loops.
  // Source expansion is explicitly requested only for benchmarking.
  if (auto it = options.find("unroll"); it != options.end()) {
    const auto& v     = it->second;
    unroll_requested_ = v == "true" || v == "1" || v == "on";
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
  // A scratch LNAST is consumed synchronously while `src_ln` remains on the
  // Lnast_manager source stack, so borrow its immutable provenance table
  // instead of importing it.  import_from() makes a locator self-contained;
  // for a fresh scratch locator that also re-derives the full source file's
  // content hash and line-offset table.  Cast lowering creates multiple
  // scratch LNASTs per operation, turning that O(source-bytes) metadata work
  // into a generated-code-scale quadratic cost.
  //
  // carry_srcid() resolves this id through the scratch locator while the base
  // is alive and imports it into the durable root only when the root does not
  // already own it (the ordinary same-source case is an O(1) has(id) hit).
  scratch->source_locator().set_base(&src_ln->source_locator());
  scratch->set_srcid(root, id);
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

void uPass_runner::emit_current_leaf() {
  if (!materialize_) {
    return;
  }
  const auto type = lm->current_type();
  if (Lnast_ntype::is_ref(type)) {
    const auto raw     = lm->current_raw_text();
    const auto renamed = lm->current_text();
    if (renamed != raw) {
      // push_source() virtually walks an extracted function body in place.
      // Its refs must be materialized with the per-call-site name computed by
      // Lnast_manager; copying the source name id here would pair an `inlN_a`
      // prologue with a raw `a` body read and leave the inlined operation
      // undriven. Keep the fast name-id copy for the overwhelmingly common
      // non-renamed path below.
      staging->add_child(staging_parent, Lnast_node::create_ref(renamed));
      return;
    }
  }
  auto nid = staging->add_child(staging_parent, type);
  // The source and staging Lnasts share active_name_pool(), so copying the
  // interned id avoids resolve-to-text + hashing the same name back into the
  // same pool for every verbatim ref/const leaf. Generated-code-scale units
  // have millions of these leaves.
  staging->set_name_id(nid, lm->get_lnast()->get_name_id(lm->get_current_nid()));
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
      emit_current_leaf();
    } else {
      emit_leaf(type);
    }
  }
}

std::optional<Dlop> uPass_runner::fold_frame_ref(std::string_view name) const {
  if (auto v = symbol_table_.known_const_scalar(name)) {
    return v;
  }
  // Inside an inlined body a user name is bound under the frame's tag (a
  // generic `<N>`, a constant comb argument): `[N]` / `[0..<N]` read it there.
  if (lm->in_inline_frame() && !Lnast::is_tmp(name)) {
    return symbol_table_.known_const_scalar(lm->frame_variable(name));
  }
  return std::nullopt;
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
  const auto slots   = symbol_table_.tuple_slot_ref.find(std::string(name));
  if (auto b = symbol_table_.get_bundle(name); b) {
    // A runtime tuple split into per-field leaves (split_runtime_tuple_store)
    // has exactly those fields: every slot reads a split leaf, its own or the
    // one of the value it copies (`const d = if s { … } else { … }`). A
    // runtime slot "0" beside them is the scalar carrier of a whole write
    // (`d = %t`), which an `if` arm leaves runtime-divergent
    // (Symbol_table::leave_scope): counting it as a field made `h(t=d)` look
    // for a `t.0` leaf and `for f in d` iterate a value that does not exist.
    auto reads_split_leaf = [&](const std::string& slot, const std::string& ref) {
      if (!ref.ends_with(absl::StrCat(".", slot))) {
        return false;
      }
      const auto split = split_tuple_leaves_.find(ref.substr(0, ref.size() - slot.size() - 1));
      return split != split_tuple_leaves_.end() && split->second.contains(slot);
    };
    const bool scalar_marker
        = slots != symbol_table_.tuple_slot_ref.end() && !slots->second.empty() && !slots->second.contains("0")
          && b->get_trivial(bundle_path::of_string("0")).is_invalid()
          && std::ranges::all_of(slots->second, [&](const auto& s) { return reads_split_leaf(s.first, s.second); });
    for (const auto& tl : b->top_levels()) {
      std::string key = tl.pos >= 0 ? std::to_string(tl.pos) : std::string(tl.name);
      if (scalar_marker && key == "0") {
        continue;
      }
      if (!key.empty() && seen.insert(key).second) {
        out.emplace_back(key, is_positional(key));
      }
    }
  }
  if (slots != symbol_table_.tuple_slot_ref.end()) {
    for (const auto& [slot, _] : slots->second) {
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

void uPass_runner::record_runtime_tuple_slot_refs() {
  // Cursor on tuple_add: [dst(ref), entry...] — entry is const | ref | store(key, val).
  // See the .hpp comment: backfill slot→ref carriers constprop dropped for
  // runtime-scalar REF fields that carry an ST bundle (locals/temps).
  if (!lm->has_child()) {
    return;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  if (lm->get_raw_ntype() != Lnast_ntype::Lnast_ntype_ref) {
    lm->restore_cursor(saved);
    return;
  }
  const std::string dvar(lm->current_text());
  auto              consider = [&](const std::string& slot, std::string_view txt) {
    if (auto it = symbol_table_.tuple_slot_ref.find(dvar); it != symbol_table_.tuple_slot_ref.end() && it->second.count(slot)) {
      return;  // constprop already recorded this carrier
    }
    auto b = symbol_table_.get_bundle(txt);
    if (!b) {
      return;  // no bundle → constprop's own no-bundle case already handled it
    }
    if (!b->is_trivial_scalar()) {
      return;  // genuine tuple: its runtime leaves were re-homed (dotted) by constprop
    }
    if (symbol_table_.known_const_scalar(txt)) {
      return;  // comptime scalar: the field's bundle trivial is the authority
    }
    symbol_table_.tuple_slot_ref[dvar][slot] = std::string(txt);
  };
  int unnamed_pos = 0;
  while (lm->move_to_sibling()) {
    const auto t = lm->get_raw_ntype();
    if (Lnast_ntype::is_const(t)) {
      ++unnamed_pos;
    } else if (Lnast_ntype::is_ref(t)) {
      const auto slot = std::to_string(unnamed_pos);
      consider(slot, lm->current_text());
      ++unnamed_pos;
    } else if (Lnast_ntype::is_store(t)) {
      // Named field: store(ref(key), const/ref(val)) — named slots don't
      // advance unnamed_pos (mirrors constprop's process_tuple_add).
      const auto entry = lm->save_cursor();
      if (lm->move_to_child()) {
        const std::string key(lm->current_text());
        if (lm->move_to_sibling() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
          consider(key, lm->current_text());
        }
      }
      lm->restore_cursor(entry);
    }
  }
  lm->restore_cursor(saved);

  // 2f-stream_ssa — pin each carrier to the version live AT CAPTURE.
  //
  // Under STREAMING SSA (selected for a repeatedly-assigned scalar in an
  // `is_verilog_origin()` unit, i.e. one the slang reader produced) the
  // LNAST keeps the BASE name on every read and only the runner versions
  // it, at emit time (emit_ref_or_folded -> stream_ssa_ref_name). But the
  // slot->carrier map records the RAW source name (constprop's
  // `tuple_slot_ref[dvar][slot] = current_text()`, and `consider` above), so a
  // later `try_lower_dynamic_tuple_index` emitted the Hotmux arm against the
  // variable's VERSION-0 net:
  //   mut m:u8 = x ; m = y ; const t = (10, 20, m, 30) ; z = t[sel]
  // put `x` in the sel==2 arm instead of `y` — silently, exit 0, and DCE then
  // deleted the whole (now unread) producing cone. 04-variables.md: "The last
  // write in program order wins".
  //
  // Resolve HERE, where the tuple literal is built, not at the indexed read:
  // the tuple captures the value the variable has at this statement, so a
  // read-time resolve would merely break the mirror case (a carrier captured
  // BEFORE a later write) in the other direction.
  //
  // stream_ssa_ref_name is the identity when streaming SSA is off or the name
  // is unversioned, and is idempotent on an already-versioned `x___ssa_N`
  // (that spelling is never a key of stream_ssa_current_), so this is a strict
  // no-op on the ordinary SSA-rebuild path.
  if (stream_ssa_enabled_) {
    if (auto it = symbol_table_.tuple_slot_ref.find(dvar); it != symbol_table_.tuple_slot_ref.end()) {
      for (auto& [slot, carrier] : it->second) {
        auto versioned = stream_ssa_ref_name(carrier);
        if (versioned != carrier) {
          carrier = std::move(versioned);
        }
      }
    }
  }
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
  if (const auto f = upass::decl_facts::lookup(symbol_table_, lm ? lm->get_lnast().get() : nullptr, name); f && f->has_type_spec) {
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
        case Io_kind::boolean: return "Bool";
        case Io_kind::string : return "String";
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
  auto folded
      = !preserved_param_names_.empty() && preserved_param_names_.contains(name) ? std::optional<Dlop>{} : try_fold_ref(name);
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
    // Keep a named package constant SYMBOLIC when the flow asked for it (see
    // set_preserve_param_provenance): `0x78` loses which constant it was, and
    // the re-emitted Pyrope should read `vpu_defs_pkg.VPU_TRANS_SIN_P2`.
    if (preserve_param_provenance_) {
      if (auto sym = pkg_origin_of(name); !sym.empty()) {
        emit_leaf(Lnast_node::create_ref(sym));
        return;
      }
    }
    emit_leaf(Lnast_node::create_const(folded->to_pyrope()));
  } else if (!stream_ssa_enabled_) {
    // Fast path: every emitted ref pays this branch; with stream-SSA off the
    // rename can never differ, so skip the per-ref string materialization.
    emit_current_leaf();
  } else {
    auto renamed = stream_ssa_ref_name(name);
    if (renamed == name) {
      emit_current_leaf();
    } else {
      emit_leaf(Lnast_node::create_ref(renamed));
    }
  }
}

std::string uPass_runner::pkg_origin_of(std::string_view name) const {
  const auto it = symbol_table_.tget_origin.find(std::string(name));
  if (it == symbol_table_.tget_origin.end()) {
    return {};
  }
  const std::string& key = it->second;  // "<base>.<field>[.<field>…]"
  const auto         dot = key.find('.');
  if (dot == std::string::npos) {
    return {};
  }
  const std::string base = key.substr(0, dot);
  // Only an IMPORT NAMESPACE qualifies — call_resolver stamps `pub_unit` on the
  // bundle it builds for `const pkg = import("pkg")`. An ordinary struct read
  // (`sigs.ldst`) has no such marker and keeps folding to its value.
  const auto        bun  = symbol_table_.get_bundle(base);
  if (!bun || !bun->has_attr(battr::pub_unit)) {
    return {};
  }
  if (lm && lm->get_lnast()) {
    lm->get_lnast()->add_imported_package(base);  // the pyrope emit needs the file-scope import
  }
  return key;
}

bool uPass_runner::imported_alias_range(std::string_view type_name, Dlop& max_out, Dlop& min_out) const {
  // An IMPORTED scalar alias (`x:pkg.PType`): a lambda unit carries no import
  // statement, so the namespace bundle is not in ITS symbol table — resolve the
  // dotted name straight off the exporting unit's pub list (the same registry
  // the value dot-selector uses) and take the "MAX|MIN" face the exporter's
  // harvest (or the slang reader) stamped into pub_values.
  if (registry_ == nullptr) {
    return false;
  }
  const auto dot = type_name.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  const std::string unit(type_name.substr(0, dot));
  const std::string member(type_name.substr(dot + 1));
  auto              uit = reg().function_registry.find(unit);
  if (uit == reg().function_registry.end() || !uit->second->get_lambda_kind().empty()) {
    return false;
  }
  std::string max_txt;
  std::string min_txt;
  if (!uit->second->pub_type_face(member, max_txt, min_txt)) {
    return false;
  }
  auto mx = Dlop::from_pyrope(max_txt);
  auto mn = Dlop::from_pyrope(min_txt);
  if (mx && mn) {
    max_out = *mx;
    min_out = *mn;
    return true;
  }
  return false;
}

void uPass_runner::emit_io_with_type_slots() {
  // io → tuple_add* → store(ref, init, [type], [stages]): a REF type slot (an
  // imported scalar alias `cmd:pkg.P_T`) concretizes to prim_type_int exactly
  // like a declare's — tolg reads port widths from the LNAST slot, and a raw
  // ref there breaks the port lowering.
  emit_push(lm->current_type());
  if (lm->has_child()) {
    lm->move_to_child();
    do {
      if (lm->get_raw_ntype() != Lnast_ntype::Lnast_ntype_tuple_add || !lm->has_child()) {
        emit_subtree_verbatim();
        continue;
      }
      emit_push(lm->current_type());
      lm->move_to_child();
      do {
        if (!Lnast_ntype::is_store(lm->get_raw_ntype()) || !lm->has_child()) {
          emit_subtree_verbatim();
          continue;
        }
        emit_push(lm->current_type());
        lm->move_to_child();
        int         cidx = 0;
        std::string port_name;
        do {
          if (cidx == 0 && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
            port_name = lm->current_text();
          }
          if (cidx >= 2 && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref
              && emit_scalar_named_type_slot(lm->current_text(), port_name)) {
            // concretized imported alias — nothing else to emit for this child
          } else {
            emit_subtree_verbatim();
          }
          ++cidx;
        } while (lm->move_to_sibling());
        lm->move_to_parent();
        emit_pop();
      } while (lm->move_to_sibling());
      lm->move_to_parent();
      emit_pop();
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }
  emit_pop();
}

std::pair<std::shared_ptr<Lnast>, Lnast_nid> uPass_runner::lookup_file_type(std::string_view type_name) {
  if (registry_ == nullptr || !lm || !lm->get_lnast()) {
    return {};
  }
  std::string unit{lm->get_lnast()->get_graph_name()};
  if (const auto dot = unit.rfind('.'); dot != std::string::npos) {
    unit.resize(dot);
  }
  const auto fit = reg().function_registry.find(unit);
  if (fit == reg().function_registry.end() || !fit->second->get_lambda_kind().empty()) {
    return {};
  }
  const auto& shell = *fit->second;
  for (auto top : shell.children(shell.get_root())) {
    if (!Lnast_ntype::is_stmts(shell.get_type(top))) {
      continue;
    }
    for (auto stmt : shell.children(top)) {
      if (!Lnast_ntype::is_declare(shell.get_type(stmt))) {
        continue;
      }
      const auto name_n = shell.get_first_child(stmt);
      const auto type_n = name_n.is_invalid() ? name_n : shell.get_sibling_next(name_n);
      const auto mode_n = type_n.is_invalid() ? type_n : shell.get_sibling_next(type_n);
      if (!name_n.is_invalid() && !type_n.is_invalid() && !mode_n.is_invalid() && Lnast_ntype::is_ref(shell.get_type(name_n))
          && shell.get_name(name_n) == type_name && Lnast_ntype::is_const(shell.get_type(mode_n))
          && shell.get_name(mode_n) == "type") {
        return {fit->second, type_n};
      }
    }
  }
  return {};
}

bool uPass_runner::emit_scalar_named_type_slot(std::string_view type_name, std::string_view port_name) {
  if (!materialize_ || type_name.empty()) {
    return false;
  }
  // Record the alias the source spelled (`rm_in:vpu_defs_pkg.TXFMA_RM_SZ_T`)
  // before it concretizes to `u3`, so the pyrope re-emission can print the name
  // back. The LNAST slot still becomes prim_type_int, so every other consumer
  // (tolg port widths, bitwidth) is unchanged — this is a pure side-channel.
  if (preserve_param_provenance_ && !port_name.empty() && type_name.find('.') != std::string_view::npos && lm && lm->get_lnast()) {
    const auto base = type_name.substr(0, type_name.find('.'));
    if (const auto bun = symbol_table_.get_bundle(base); bun && bun->has_attr(battr::pub_unit)) {
      lm->get_lnast()->add_io_type_name(port_name, type_name);
      lm->get_lnast()->add_imported_package(base);
    }
  }
  // An integer-encoded enum declaration type (`reg st:Color`) concretizes to
  // its hidden encoding alias (user ruling 2026-09-28 (29)), declared next to
  // the enum: in this unit (an `enum` statement is replayed into every lambda
  // that reads it) or in the file shell (a file-scope `const Color =
  // enum(…)`). A port keeps the enum's name: upass.ssa already sized it.
  const auto enc = Lnast::enum_encoding_type(type_name);
  if (port_name.empty() && (symbol_table_.has_bundle(enc) || lookup_file_type(enc).first)) {
    type_name = enc;
  }
  auto tb = symbol_table_.get_bundle(type_name);
  if (!tb) {
    Dlop imax, imin;
    if (imported_alias_range(type_name, imax, imin)) {
      emit_push(Lnast_ntype::create_prim_type_int());
      emit_leaf(Lnast_node::create_const(std::string(imax.to_pyrope())));
      emit_leaf(Lnast_node::create_const(std::string(imin.to_pyrope())));
      emit_pop();
      return true;
    }

    // A file-scope LOCAL alias lives in the source-unit shell, not in the
    // extracted module's symbol table. Recover its primitive declaration from
    // that shell. This is also what lets an alias nested below
    // comp_type_array be concretized before tolg asks for the element width.
    if (const auto [owner, type_n] = lookup_file_type(type_name); owner) {
      const auto& shell = *owner;
      if (Lnast_ntype::is_prim_type_int(shell.get_type(type_n))) {
        const auto max_n = shell.get_first_child(type_n);
        const auto min_n = max_n.is_invalid() ? max_n : shell.get_sibling_next(max_n);
        if (max_n.is_invalid() || min_n.is_invalid()) {
          return false;
        }
        emit_push(Lnast_ntype::create_prim_type_int());
        emit_leaf(Lnast_node::create_const(std::string(shell.get_name(max_n))));
        emit_leaf(Lnast_node::create_const(std::string(shell.get_name(min_n))));
        emit_pop();
        return true;
      }
      if (Lnast_ntype::is_prim_type_bool(shell.get_type(type_n))) {
        emit_leaf(Lnast_ntype::create_prim_type_bool());
        return true;
      }
      if (Lnast_ntype::is_prim_type_clock_or_reset(shell.get_type(type_n))) {
        emit_leaf(shell.get_type(type_n));  // `type C = Clock`: keep the class
        return true;
      }
      if (Lnast_ntype::is_prim_type_string(shell.get_type(type_n))) {
        emit_leaf(Lnast_ntype::create_prim_type_string());
        return true;
      }
      return false;
    }
  }
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

bool uPass_runner::emit_concrete_type_slot() {
  if (!materialize_) {
    return false;
  }
  if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    return emit_scalar_named_type_slot(lm->current_text());
  }
  if (!Lnast_ntype::is_comp_type_array(lm->get_raw_ntype())) {
    return false;
  }

  // comp_type_array(element_type, dimension): only the first child is a type
  // slot. Recursing through it supports `[N][M]Alias`; copying every later
  // child verbatim keeps a named/constant dimension on the value-fold path.
  emit_push(lm->current_type());
  if (lm->has_child()) {
    lm->move_to_child();
    if (!emit_concrete_type_slot()) {
      emit_subtree_verbatim();
    }
    while (lm->move_to_sibling()) {
      emit_subtree_verbatim();
    }
    lm->move_to_parent();
  }
  emit_pop();
  return true;
}

// The DECLARED bit width of ONE element of a positional array, or 0 when the
// bundle carries no element envelope. The envelope rides the bundle as the
// internal `__elem_max`/`__elem_min` attrs the array declare bakes.
static uint32_t array_elem_declared_bits(const Bundle& b) {
  const auto& elem_max = b.get_attr("__elem_max");
  const auto& elem_min = b.get_attr("__elem_min");
  if (!elem_max.is_integer() || !elem_min.is_integer()) {
    return 0;
  }
  if (elem_min.is_negative()) {
    return static_cast<uint32_t>(std::max<int64_t>(elem_max.get_signed_bits(), elem_min.get_signed_bits()));
  }
  return static_cast<uint32_t>(elem_max.get_payload_bits());
}

// The total width of a DECLARED positional array spliced as ONE concat lane:
// every entry of the array, each a window of the element type. Returns 0 when
// the bundle is not a declared array or its extent is unresolved -- fail
// closed, never a guess, because a wrong extent relocates every lane above it.
//
// Reads the FLAT entry count (`__array_flat_size`), not the outer
// `__array_size`: `[4][8]u8` splices 32 windows, not 4.
static uint64_t concat_array_lane_bits(const Bundle& b) {
  const auto& flat = b.get_attr("__array_flat_size");
  if (!flat.is_integer() || !flat.is_just_i64()) {
    return 0;
  }
  const int64_t  n  = flat.to_just_i64();
  const uint32_t eb = array_elem_declared_bits(b);
  if (n <= 0 || eb == 0) {
    return 0;
  }
  return static_cast<uint64_t>(n) * eb;
}

// A named bundle has field IDENTITY but deliberately has no field ORDER:
// Bundle's canonical traversal sorts its keys so `(hi=3, lo=2)` and
// `(lo=2, hi=3)` are the same value. Packing such a value would silently turn
// that implementation order into a bit-layout contract. A one-field named
// bundle is unambiguous; positional tuples and arrays retain their explicit
// order and remain legal.
//
// Try both the exact ref and its source-level SSA base. Compiler temps are
// useful here too: `((hi=3, lo=2), c)#[..]` names the literal through one.
bool uPass_runner::named_bundle_without_bit_order(std::string_view name) const {
  if (name.empty()) {
    return false;
  }
  std::shared_ptr<Bundle> b = symbol_table_.get_bundle(name);
  if (!b) {
    const auto logical = concat_logical_name(name);
    if (logical.empty()) {
      return false;
    }
    b = symbol_table_.get_bundle(logical);
    if (!b) {
      return false;
    }
  }
  return b->has_named_top() && (b->named_top_count() + b->unnamed_top_count()) > 1;
}

// `x#[..]` where `x` is a multi-field NAMED bundle. Same rule as a concat
// lane (see named_bundle_without_bit_order), reached by the other spelling:
// `#[..]` is the full bit vector of its operand.
//
// Without this the operand simply never resolves and tolg reports `unresolved
// reference 'x'`, which names neither the rule nor the fix. A ONE-field named
// bundle is unambiguous and stays legal, as do positional tuples and arrays.
void uPass_runner::check_bitsel_named_bundle() {
  if (!lm->has_child()) {
    return;
  }
  // PYROPE ONLY, like every other member of this rule family (check_concat_dest,
  // upass.tolg's twin). "a packing needs a written bit order" is a rule about
  // Pyrope SOURCE; slang resolves its own layouts and emits get_mask nodes
  // (fit_wrap truncation) over whatever it likes, so applying this to imported
  // RTL would reject legal Verilog.
  if (lm->get_lnast() && lm->get_lnast()->is_verilog_origin()) {
    return;
  }
  const auto nid = lm->get_current_nid();
  if (!bitsel_checked_.insert(nid).second) {
    return;  // the runner may revisit a node across iterations; report once
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();  // child 0 = dst ref
  if (lm->move_to_sibling()) {
    const std::string src_name(lm->current_text());
    if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref && named_bundle_without_bit_order(src_name)) {
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "concat-named-bundle-lane",
          .category = "type",
          .pass     = "upass.runner",
          .message  = std::format("`{}` is a named bundle with multiple fields, which has no bit order", src_name),
          .span     = lm->current_span(),
          .hint     = "select the fields explicitly in the order you want (for example, `(x.lo, x.hi)#[..]`, entry 0 "
                      "at bit 0); positional tuples and arrays already have an order",
      });
    }
  }
  lm->restore_cursor(saved);
}

// Report every `concat` lane that declares no width.
//
// Deliberately NOT folded into resolve_concat_widths, which runs on the EMIT
// path: emission is materialize-only, and the comptime / expected-error tiers
// (`toln:0`) never materialize -- so a diagnostic raised there would simply not
// exist for anyone compiling for comptime. This runs from the dispatch path,
// which every tier takes. upass.tolg keeps its own copy of the check as the
// fail-closed backstop for whatever reaches lowering.
void uPass_runner::check_concat_lanes() {
  if (!lm->has_child()) {
    return;
  }
  const auto nid = lm->get_current_nid();
  if (!concat_checked_.insert(nid).second) {
    return;  // the runner may revisit a node across iterations; report once
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();  // child 0 = dst ref
  const std::string dst(lm->current_text());
  uint64_t          total = 0;
  bool              bad   = false;
  while (lm->move_to_sibling()) {
    const std::string  lane_name(lm->current_text());
    const bool         lane_is_ref = lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref;
    livehd::diag::Span span        = lm->current_span();
    if (!lm->move_to_sibling()) {
      break;  // trailing lane with no width operand: malformed shape, tolg reports it
    }
    const auto bound_txt = std::string(lm->current_text());
    if (!bound_txt.empty() && bound_txt != "nil") {
      auto bv = Dlop::from_pyrope(bound_txt);  // a frontend already bound this window
      if (bv && bv->is_integer() && bv->is_just_i64() && bv->to_just_i64() > 0) {
        total += static_cast<uint64_t>(bv->to_just_i64());
      } else {
        bad = true;
      }
      continue;
    }
    // A multi-field named bundle has no bit order -- see
    // named_bundle_without_bit_order, which the `x#[..]` spelling shares.
    std::shared_ptr<Bundle> lane_bundle = lane_is_ref ? symbol_table_.get_bundle(lane_name) : nullptr;
    if (!lane_bundle && lane_is_ref) {
      const auto logical = concat_logical_name(lane_name);
      if (!logical.empty()) {
        lane_bundle = symbol_table_.get_bundle(logical);
      }
    }
    const size_t lane_field_count
        = lane_bundle ? lane_bundle->named_top_count() + lane_bundle->unnamed_top_count() : static_cast<size_t>(0);
    if (lane_bundle && lane_bundle->has_named_top() && lane_field_count > 1) {
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "concat-named-bundle-lane",
          .category = "type",
          .pass     = "upass.runner",
          .message  = std::format("concat lane `{}` is a named bundle with multiple fields, which has no bit order", lane_name),
          .span     = std::move(span),
          .hint     = "select the fields explicitly in the order you want (for example, `(x.lo, x.hi)#[..]`, entry 0 "
                      "at bit 0); positional tuples and arrays already have an order",
      });
      bad = true;
      continue;
    }
    // An ordered positional tuple/array (or the unambiguous one-field named
    // case) is a sequence of windows, not one scalar window. Account for its
    // field declarations here so the dispatch path does not reject it before
    // constprop can splice and fold the fields. Runtime tuple packing still
    // stays structural unless a later pass can materialize it; no width is
    // guessed from field values.
    if (lane_bundle && (lane_field_count > 1 || lane_bundle->has_named_top())) {
      const auto     logical         = concat_logical_name(lane_name);
      const auto     root            = logical.empty() ? std::string_view(lane_name) : logical;
      const uint32_t array_elem_bits = array_elem_declared_bits(*lane_bundle);
      uint64_t       tuple_total     = 0;
      bool           complete        = true;
      for (const auto& field : lane_bundle->top_levels()) {
        if (field.has_leafs || (field.pos < 0 && field.name.empty())) {
          complete = false;
          break;
        }
        const std::string key = field.pos >= 0 ? std::to_string(field.pos) : std::string(field.name);
        const auto        facts
            = upass::decl_facts::lookup(symbol_table_, lm ? lm->get_lnast().get() : nullptr, absl::StrCat(root, ".", key));
        const uint32_t field_bits = (facts && facts->has_type_spec) ? facts->bits : array_elem_bits;
        if (field_bits == 0) {
          complete = false;
          break;
        }
        tuple_total += field_bits;
      }
      if (complete && tuple_total > 0) {
        total += tuple_total;
        continue;
      }
    }
    // A DECLARED array whose entries are not bundle FIELDS. A `reg
    // stages:[4]u8` is one persistent memory, so nothing ever materializes
    // `stages.0`..`stages.3` in the table and the top_levels walk above finds
    // nothing to sum -- the lane read as undeclared even though `[4]u8` states
    // its 32 bits exactly. The declared extent + element envelope ride the
    // bundle as attrs, which is the one source that survives for every array
    // storage class.
    if (lane_bundle) {
      if (const auto ab = concat_array_lane_bits(*lane_bundle); ab != 0) {
        total += ab;
        continue;
      }
    }
    if (const auto db = lane_is_ref ? concat_lane_declared_bits(lane_name) : 0; db != 0) {
      total += db;
      continue;
    }
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "concat-untyped-lane",
        .category = "type",
        .pass     = "upass.runner",
        .message  = lane_is_ref ? std::format("concat lane `{}` has no declared bit width", lane_name)
                                : std::format("concat lane `{}` is a literal, which has no declared bit width", lane_name),
        .span     = std::move(span),
        .hint     = "a concat window is sized by the lane's DECLARED type -- never by its value, an inferred range, "
                    "or a literal's spelling -- because narrowing one lane would shift every lane above it; bind it to "
                    "a typed name first (`const w:U4 = <expr>` then `(..., w, ...)#[..]`)",
    });
    bad = true;
  }
  lm->restore_cursor(saved);
  // Record the result temp's own width even in a non-materializing tier: the
  // destination check below reads it, and so does a NESTING concat's lane
  // lookup (an inner concat's temp is never user-declared, but its width is
  // the lane sum by construction).
  if (!bad && total > 0 && !dst.empty()) {
    concat_result_bits_[dst] = static_cast<uint32_t>(total);
  }
}

// `c:u12 = concat(a:u4, b:u8)` -- the destination's declared width must equal
// the lane sum EXACTLY. Not `>=`: a concat states a bit LAYOUT, and a
// destination that quietly zero-extends it is a layout the source never wrote.
// Signedness is free (`u12` and `s12` are both 12-bit fields), so only the
// width is compared.
//
// Runs where a concat's TEMP is bound to a named destination, because the
// concat node's own dst is always a compiler temp.
void uPass_runner::check_concat_dest(std::string_view dest_name, std::string_view value_name) {
  const auto cit = concat_result_bits_.find(std::string(value_name));
  if (cit == concat_result_bits_.end()) {
    return;
  }
  // PYROPE ONLY. "the destination must declare the lane sum" is a rule about
  // Pyrope SOURCE: it exists so a concat's bit layout is written down at both
  // ends. Verilog states its widths its own way -- `logic [14:0] x; assign x =
  // {a,b,c};` is a complete declaration, and Verilog's assignment rules make a
  // wider or narrower target legal (pad / truncate) rather than an error. slang
  // has already resolved all of that and binds every lane width itself, so
  // applying the Pyrope rule to an imported design rejects ordinary RTL --
  // minion's vpu_top.sv alone produced 269 of these. Same split tolg draws for
  // an undriven read (hard error for Pyrope, warn for Verilog).
  if (lm && lm->get_lnast() && lm->get_lnast()->is_verilog_origin()) {
    return;
  }
  // Only a SOURCE-level destination is checked. A store into a compiler temp
  // (an SSA staging name a frontend minted, a `___N`) is an internal move, not
  // a declaration the user wrote, and demanding a type of it would reject a
  // frontend's own staging name.
  dest_name = concat_logical_name(dest_name);
  if (dest_name.empty()) {
    return;
  }
  const auto nid = lm->get_current_nid();
  if (!concat_dest_checked_.insert(nid).second) {
    return;
  }
  const uint32_t     declared = concat_lane_declared_bits(dest_name);
  livehd::diag::Span span     = lm->current_span();
  if (declared == 0) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "concat-untyped-dest",
        .category = "type",
        .pass     = "upass.runner",
        .message  = std::format("`{}` is assigned a concat but has no declared type", dest_name),
        .span     = std::move(span),
        .hint     = std::format("a concat's destination must declare the {}-bit width its lanes add up to "
                                "(e.g. `{}:U{}` or `{}:S{}`)",
                            cit->second,
                            dest_name,
                            cit->second,
                            dest_name,
                            cit->second),
    });
    return;
  }
  if (declared != cit->second) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "concat-width-mismatch",
        .category = "type",
        .pass     = "upass.runner",
        .message
        = std::format("`{}` is declared {} bits but the concat assigned to it is {} bits", dest_name, declared, cit->second),
        .span = std::move(span),
        .hint = "a concat's destination must match the lane sum EXACTLY, so the bit layout the source states is "
                "the layout the destination has -- widen or narrow the lanes, not the destination",
    });
  }
}

// The DECLARED bit width of one `concat` lane, or 0 when the lane declares
// none. This is the only width a concat window may be sized by.
//
// Four sources, in order:
//   * a previous concat's result -- its width is the lane sum, by construction,
//     which is what makes `concat(concat(a,b), c)` legal without the inner temp
//     ever being declared by the user;
//   * a declared tuple field reached through its tuple_get/compiler-copy
//     provenance -- this is what makes explicit `concat(p.hi, p.lo)` legal;
//   * decl_facts, the single source of truth for "what was `name` declared as".
//     Its `bits` is already the literal FIELD width, so `a:u4` and `a:s4` both answer 4 -- which is
//     right: a window is a bit count, and signedness only decides how the bits
//     are read back;
//   * nothing. A literal operand lands here (a literal has no declared type,
//     however it is spelled) and so does an unannotated expression. Both keep
//     the `nil` and are reported by upass.tolg, which has the span.
uint32_t uPass_runner::concat_lane_declared_bits(std::string_view lane_name) const {
  if (lane_name.empty()) {
    return 0;
  }
  if (const auto it = concat_result_bits_.find(std::string(lane_name)); it != concat_result_bits_.end()) {
    return it->second;
  }
  // A BOOLEAN lane is 1 bit BY DEFINITION -- it has no declared `:uN`, but its
  // width is not an inferred range either, so the "sized by the DECLARED type"
  // rule is satisfied, not bent. This is what makes the sanctioned bool->int
  // cast `unsigned(b)`/`signed(b)` expressible: it lowers through a get_mask
  // whose bit-select handler wraps the value in a one-lane concat, and without
  // this the lane was reported as untyped and the cast could not be written at
  // all (its own diagnostic recommended it). Matches the 1-bit mask the cast
  // emits for a bool operand in try_lower_typecast.
  if (const auto b = symbol_table_.get_bundle(lane_name); b && b->get_value_kind() == upass::Kind::boolean) {
    return 1;
  }
  // A dotted source read lowers through a compiler temp (`%t = tuple_get(p,
  // hi)`). The temp itself is intentionally untyped, but tuple_get records its
  // declared source field in tget_origin. Follow that provenance so spelling
  // the desired named-bundle order explicitly as `concat(p.hi, p.lo)` works.
  // Ordinary untyped expression temps still have no origin and remain errors.
  auto origin = symbol_table_.tget_origin.find(std::string(lane_name));
  if (origin != symbol_table_.tget_origin.end()) {
    const auto f = upass::decl_facts::lookup(symbol_table_, lm ? lm->get_lnast().get() : nullptr, origin->second);
    if (f && f->has_type_spec && f->bits != 0) {
      return f->bits;
    }
  }
  // A function body may be checked directly from its stored fdef before the
  // tuple_get statements have gone through the normal dispatch path, so the
  // transient origin map above is not necessarily populated yet. Recover the
  // same provenance structurally from a preceding sibling
  // `tuple_get(lane_name, base, field...)`. prp2lnast emits dotted argument
  // reads immediately before their consuming concat; limiting the search to
  // the enclosing statement list also avoids cross-function temp collisions.
  const bool compiler_temp = lane_name.front() == '%' || lane_name.starts_with("___");
  if (compiler_temp && lm && lm->get_lnast()) {
    const auto& ln         = *lm->get_lnast();
    auto        concat_nid = lm->get_current_nid();
    while (!concat_nid.is_invalid() && !Lnast_ntype::is_concat(ln.get_type(concat_nid))) {
      concat_nid = ln.get_parent(concat_nid);
    }
    const auto parent = concat_nid.is_invalid() ? concat_nid : ln.get_parent(concat_nid);
    if (!parent.is_invalid()) {
      for (auto stmt = ln.get_first_child(parent); !stmt.is_invalid() && stmt != concat_nid; stmt = ln.get_sibling_next(stmt)) {
        // The runner may already have resolved tuple_get to a direct copy in
        // staging. Follow that compiler-temp copy when its RHS is itself a
        // declared ref (normally the flattened `p.hi` input leaf).
        if (Lnast_ntype::is_store(ln.get_type(stmt))) {
          const auto dst = ln.get_first_child(stmt);
          const auto rhs = dst.is_invalid() ? dst : ln.get_sibling_next(dst);
          if (!dst.is_invalid() && !rhs.is_invalid() && ln.get_name(dst) == lane_name && Lnast_ntype::is_ref(ln.get_type(rhs))) {
            const auto f = upass::decl_facts::lookup(symbol_table_, &ln, ln.get_name(rhs));
            if (f && f->has_type_spec && f->bits != 0) {
              return f->bits;
            }
          }
        }
        if (!Lnast_ntype::is_tuple_get(ln.get_type(stmt))) {
          continue;
        }
        const auto dst = ln.get_first_child(stmt);
        const auto src = dst.is_invalid() ? dst : ln.get_sibling_next(dst);
        if (dst.is_invalid() || src.is_invalid() || ln.get_name(dst) != lane_name || !Lnast_ntype::is_ref(ln.get_type(src))) {
          continue;
        }
        std::string source_field(ln.get_name(src));
        for (auto field = ln.get_sibling_next(src); !field.is_invalid(); field = ln.get_sibling_next(field)) {
          if (!Lnast_ntype::is_const(ln.get_type(field))) {
            source_field.clear();
            break;
          }
          absl::StrAppend(&source_field, ".", ln.get_name(field));
        }
        if (!source_field.empty()) {
          const auto f = upass::decl_facts::lookup(symbol_table_, &ln, source_field);
          if (f && f->has_type_spec && f->bits != 0) {
            return f->bits;
          }
        }
      }
    }
  }
  // A read is an SSA VERSION (`a___ssa_2`); the type was declared once on the
  // base name. Without this strip every lane in an SSA'd body -- which is every
  // lane a frontend produces -- looks undeclared.
  const auto base = concat_logical_name(lane_name);
  if (const auto it = concat_result_bits_.find(std::string(base)); it != concat_result_bits_.end()) {
    return it->second;
  }
  if (!base.empty() && base != lane_name) {
    origin = symbol_table_.tget_origin.find(std::string(base));
    if (origin != symbol_table_.tget_origin.end()) {
      const auto f = upass::decl_facts::lookup(symbol_table_, lm ? lm->get_lnast().get() : nullptr, origin->second);
      if (f && f->has_type_spec && f->bits != 0) {
        return f->bits;
      }
    }
  }
  if (const auto f = upass::decl_facts::lookup(symbol_table_, lm ? lm->get_lnast().get() : nullptr, base);
      f && f->has_type_spec && f->bits != 0) {
    return f->bits;
  }
  // Last: the lane's OWN name, even when it is a compiler temp. `base` is empty
  // for a temp (concat_logical_name: a temp has no USER identity), but a temp
  // can still carry a typespec the compiler itself stamped -- and a bit select
  // is exactly that case: the bit-select handler stamps the selected window
  // with `emit_inline_typespec(dst, selected, /*signed=*/false)` -- b-a+1 bits.
  // (Signedness itself is NOT decided here: upass derives it from the computed
  // max/min, and a min < 0 makes the destination have to be signed or the
  // assignment is a compile error. Only the WIDTH is read back.) That width comes
  // from the SLICE BOUNDS, never from the value, so it satisfies the "sized by
  // the DECLARED type" rule -- an array element that cannot fold (an all-`0sb?`
  // array) must not lose its width just because its VALUE is unknown.
  // Only a stamped type_spec counts here; an inferred range still does not.
  const auto tf = upass::decl_facts::lookup(symbol_table_, lm ? lm->get_lnast().get() : nullptr, lane_name);
  if (!tf || !tf->has_type_spec || tf->bits == 0) {
    return 0;
  }
  return tf->bits;
}

// The user-visible variable behind an LNAST name: SSA suffix stripped. Returns
// empty for a pure COMPILER TEMP (`___3`, `%4`), which has no user identity at
// all -- the "a concat's destination must be declared" rule is a statement
// about SOURCE, so it cannot be asked of a temp the compiler minted itself.
std::string_view uPass_runner::concat_logical_name(std::string_view name) {
  if (const auto p = name.find("___ssa_"); p != std::string_view::npos) {
    name = name.substr(0, p);
  }
  if (name.empty() || name.starts_with("___") || name.front() == '%') {
    return {};
  }
  return name;
}

// Walk the concat node under the cursor and return the width text to emit for
// each lane ("" = leave the `nil` in place). Also records the result temp's own
// width when EVERY lane resolved, so a nesting concat can size this one.
std::vector<std::string> uPass_runner::resolve_concat_widths(std::string& dst_name) {
  std::vector<std::string> widths;
  if (!lm->has_child()) {
    return widths;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  dst_name = std::string(lm->current_text());  // child 0 is the dst ref

  uint64_t total     = 0;
  bool     all_bound = true;
  while (lm->move_to_sibling()) {
    const std::string lane_name(lm->current_text());
    const bool        lane_is_ref = lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref;
    if (!lm->move_to_sibling()) {
      all_bound = false;  // trailing lane with no width operand: malformed, tolg reports it
      break;
    }
    // An ALREADY-BOUND width wins: a frontend that knew the window (slang reads
    // it off the operand type) has better information than anything derivable
    // from a name, and re-deriving it could disagree.
    const auto bound_txt = std::string(lm->current_text());
    const bool is_nil    = bound_txt.empty() || bound_txt == "nil";
    if (!is_nil) {
      widths.emplace_back();  // keep whatever is there
      auto v = Dlop::from_pyrope(bound_txt);
      if (v && v->is_integer() && v->is_just_i64() && v->to_just_i64() > 0) {
        total += static_cast<uint64_t>(v->to_just_i64());
      } else {
        all_bound = false;
      }
      continue;
    }
    const uint32_t bits = lane_is_ref ? concat_lane_declared_bits(lane_name) : 0;
    if (bits == 0) {
      widths.emplace_back();  // unresolved: leave the nil; check_concat_lanes reported it
      all_bound = false;
      continue;
    }
    widths.emplace_back(std::to_string(bits));
    total += bits;
  }
  lm->restore_cursor(saved);

  if (all_bound && total > 0 && total <= std::numeric_limits<uint32_t>::max() && !dst_name.empty()) {
    concat_result_bits_[dst_name] = static_cast<uint32_t>(total);
  }
  return widths;
}

// The BASE variable a private SSA version name belongs to, or "" when `name` is
// not a version. Both spellings are recognized: `x___ssa_<N>` as upass.ssa mints
// it, and `x__w<N>` as upass.ssa DEMOTES it when a body is re-SSA'd (see the
// "Demote stale SSA versions" block in upass/ssa/upass_ssa.cpp, and
// lnast_prp_writer's identical `base + "__w" + version` spelling on emit).
static std::string_view ssa_version_base(std::string_view name) {
  for (const std::string_view sep : {std::string_view{"___ssa_"}, std::string_view{"__w"}}) {
    const auto pos = name.rfind(sep);
    if (pos == std::string_view::npos || pos == 0) {
      continue;
    }
    const auto digits = pos + sep.size();
    if (digits >= name.size()) {
      continue;
    }
    if (name.find_first_not_of("0123456789", digits) != std::string_view::npos) {
      continue;
    }
    return name.substr(0, pos);
  }
  return {};
}

// The variable a COMPILER-MINTED SSA version in `ln` belongs to, or "" when
// `name` is not one: upass.ssa's private `x___ssa_<N>`, and a `x__w<N>` that
// demote_stale_ssa recorded on `ln` (both may stack). Unlike ssa_version_base
// this never strips a source-authored `x__w2`, which is a distinct variable.
static std::string minted_ssa_base(const Lnast& ln, std::string_view name) {
  std::string_view base     = name;
  bool             stripped = false;
  for (;;) {
    if (const auto suffix = upass::stale_ssa_suffix(base); suffix && suffix->first > 0) {
      base = base.substr(0, suffix->first);
    } else if (const auto demoted = ln.ssa_demoted_base(base); !demoted.empty()) {
      base = demoted;
    } else {
      break;
    }
    stripped = true;
  }
  return stripped ? std::string(base) : std::string{};
}

// `x:u48 = 0sb?` — an UNKNOWN sign extension is the WIDTH-TAKING wildcard: the
// `?` replicates into the destination's DECLARED width and stops there, so the
// store is exactly `x = 0ub` + 48 `?` (and `v:u8 = 0sb?1` is `0ub??????_?1`).
// Returns that literal's text, or "" to leave the store as written.
//
// Resolved HERE, at emission, for the same reason the concat widths above are:
// everything downstream (tolg, the Pyrope writer, sim, LEC) reads the LITERAL,
// and a sign-unknown literal carries no width to read — Dlop can only bound one
// conservatively (get_signed_bits() -> 65 for ANY sign-unknown value). Left alone, every
// net driven by an x-poison came out 65 bits wide, and a destination wider than
// that read a known 0 above bit 64 instead of `?`.
//
// A destination with no declared width (`mut z = 0sb?`) has no envelope to fill
// and keeps the 1-bit signed unknown it is written as. A SIGNED destination is
// also left alone: masking would make the value non-negative, and Dlop has no
// bounded-width signed all-unknown to narrow it to (the sign bit is the unknown).
std::string uPass_runner::resolve_x_fill() {
  if (!lm->has_child()) {
    return {};
  }
  const auto  saved = lm->save_cursor();
  std::string out;
  lm->move_to_child();
  const std::string dst(lm->current_text());  // child 0 is the dst ref
  // Exactly two children: a plain scalar store. An indexed / multi-level write
  // targets part of the destination, which is not the whole declared envelope.
  if (!dst.empty() && lm->move_to_sibling() && Lnast_ntype::is_const(lm->get_raw_ntype())) {
    const auto txt      = std::string(lm->current_text());
    const bool last_kid = !lm->move_to_sibling();  // a third child = an indexed write, not a whole store
    // An SSA version holds a value OF the declared variable, so it answers to
    // the base name's envelope (the poison store lands on `x___ssa_1`).
    auto       f        = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), dst);
    // An SSA version answers to the base name's envelope, and it reaches here
    // under EITHER of the two spellings a version can carry:
    //
    //   `x___ssa_N` — minted by the SSA run that produced this body;
    //   `x__wN`     — the SAME version after upass_ssa's "demote stale SSA
    //                 versions" step (upass/ssa/upass_ssa.cpp, `___ssa_<N>` ->
    //                 `__w<N>`), which fires whenever a body is sent through
    //                 SSA a SECOND time.
    //
    // The second pass is not exotic: lhd_kernel_compile.cpp re-runs pass.upass
    // with `default_top` set so a GENERIC top can be specialized
    // (specialize_top_defaults), and that round re-SSAs the clone. Matching only
    // `___ssa_` therefore lost the envelope for every x-poison in a unit with
    // generic parameters — the store stayed the UNBOUNDED sign-unknown `0sb?`,
    // tolg sized it from Dlop::get_signed_bits() (65), and every bit above the
    // declared width reached the netlist free. A whole-value read of such a net
    // (`|vec` -> `vec != 0`) is then unconstrained: minion's
    // `minion_dcache_tensor_load{,_p1}` emitted `65'sb1?…?` for nine 4-bit
    // generate-loop vectors and LEC refuted on `l2_req_valid`/`bus_err_o`.
    if (!f || !f->range_max) {
      if (const auto base = ssa_version_base(dst); !base.empty()) {
        f = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), base);
      }
    }
    // The declared envelope, as a MASK. Two sources, in precedence order:
    //
    //   1. the exact `int(min,max)` range — the precise bound, so `int(0,5)`
    //      fills 3 bits and not 8;
    //   2. `bits` alone, which is ALL AN IO PORT CARRIES once its width passes
    //      61. upass.ssa records `has_range` only when BOTH bounds are
    //      i64-representable (`Dlop::is_just_i64()` == `get_signed_bits() <= 62`), and
    //      a u62's max needs 63 — so every wider port reaches here with a
    //      width and no range.
    //
    // Without source 2 a wide port's poison stayed the 1-bit `0sb?`, tolg sized
    // it from `Dlop::get_signed_bits()` (65 for ANY sign-unknown value), and the
    // destination read a KNOWN 1 above bit 64 — the silent 0/1 this fill exists
    // to prevent, just at a different width. Reproduced with a 128-bit undriven
    // comb output round-tripped through `--emit-dir pyrope:`: `129'sb0?…?`
    // direct, `65'sb1?…?` after the round trip. Only `has_type_spec` facts
    // qualify (an io port or an explicit `[bits=N]`), never an inferred width.
    std::optional<Dlop> envelope;
    if (f && f->range_max && f->range_min && f->range_max->is_integer() && !f->range_max->is_negative()
        && !f->range_min->is_negative() && !f->range_max->has_unknowns()) {
      envelope = *f->range_max;
    } else if (f && f->has_type_spec && f->kind == upass::decl_facts::Num::unsigned_int && f->bits > 0) {
      envelope = *Dlop::get_mask_value(static_cast<int>(f->bits));
    }
    if (last_kid && envelope && !envelope->is_invalid()) {
      const int w = envelope->get_payload_bits();  // uN's max is 2^N-1
      if (w > 0) {
        try {
          const Dlop& v = Dlop::from_pyrope_cached(txt);
          if (v.is_integer() && v.unknown_bit_test(w)) {
            if (auto filled = v.and_op(*envelope); filled && !filled->is_invalid()) {
              out = filled->to_pyrope();  // canonical + round-tripping
            }
          }
        } catch (...) {  // NOLINT(bugprone-empty-catch) — an unparseable literal is left as written
        }
      }
    }
  }
  lm->restore_cursor(saved);
  return out;
}

void uPass_runner::emit_op_with_fold(bool fold_all) {
  if (!materialize_) {
    return;  // pure emission (dispatch happened in the caller); cursor untouched
  }
  if (Lnast_ntype::is_io(lm->get_raw_ntype())) {
    emit_io_with_type_slots();  // port type slots need the named-alias concretization
    return;
  }
  const auto op_ntype         = lm->current_type();
  const bool is_call          = Lnast_ntype::is_func_call(op_ntype);
  const auto saved_active_def = stream_ssa_active_def_;
  if (stream_ssa_enabled_ && lm->get_lnast().get() == root_lnast_.get()) {
    const uint64_t node_id = lm->get_current_nid().get_class_index().value;
    if (const auto it = stream_ssa_defs_.find(node_id); it != stream_ssa_defs_.end()) {
      stream_ssa_active_def_ = it->second;
    }
  }

  // ── concat: bind the `nil` width operands, HERE ──────────────────────────
  //
  // `concat(dst, v0, w0, v1, w1, …)` reaches the runner with every `w` still
  // the `nil` sentinel when the frontend had no types (prp2lnast). Each one has
  // to become the lane's DECLARED width, and it has to happen at THIS moment:
  // the loop below folds a comptime lane ref to a literal, and a literal's
  // magnitude is not its window (`0ub0010` and `0ub10` are the same value at
  // different widths). Resolve first, fold second, and the width survives as a
  // sibling operand.
  //
  // Deliberately NOT sized from the lane's value or its inferred range: a
  // window that shrank because a value happened to be small would shift every
  // lane ABOVE it -- a silent miscompile, not a lost bound. An unresolvable
  // lane keeps its `nil` and upass.tolg reports it with a span.
  std::vector<std::string> concat_widths;  // one per lane, "" = leave the nil
  std::string              concat_dst;
  if (Lnast_ntype::is_concat(op_ntype)) {
    concat_widths = resolve_concat_widths(concat_dst);
  }
  // ── `0sb?`: the width-taking wildcard, bound to the destination, HERE ─────
  const std::string x_fill = Lnast_ntype::is_store(op_ntype) ? resolve_x_fill() : std::string{};

  emit_push(op_ntype);      // carries the SourceId (general carry)
  std::string call_callee;  // child 1 of a func_call — read while walking it below

  // A `declare`/`type_spec` whose type slot (child 1) is a named-type
  // `ref` must NOT be folded: the ref names a TYPE, not a value, so folding it
  // through the symbol table (e.g. `const a = …; const x:a = …`) would replace
  // the type with `a`'s value. Keep child 1 verbatim for these op-nodes.
  const bool type_slot_at_1  = Lnast_ntype::is_declare(op_ntype) || Lnast_ntype::is_type_spec(op_ntype);
  // A tuple/array element store (`store(w, 0, v)`) mutates the aggregate's
  // CURRENT stream-SSA version in place, as the established SSA transformer
  // does (`store(w___ssa_1, 0, v)`). The raw name would update a version that
  // every later read has already moved past (`w = 0sb?; w[0] = a; o = w[0]`
  // read all-X).
  const bool stream_mutation = stream_ssa_enabled_ && Lnast_ntype::is_store(op_ntype) && lm->current_num_children() > 2;

  if (lm->has_child()) {
    lm->move_to_child();
    int idx = 0;
    do {
      const bool is_type_slot = (idx == 1 && type_slot_at_1);
      const bool is_lhs       = ((idx == 0) && !fold_all) || is_type_slot;
      if (is_call && idx == 1) {
        call_callee = std::string(lm->current_raw_text());  // callee id — never frame-renamed
      }
      if (is_type_slot && emit_concrete_type_slot()) {
        // scalar named-type refs, including array element aliases, concretized
        // to primitive types — nothing else to emit
      } else if (idx == 0 && is_lhs && stream_ssa_active_def_.has_value() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
        emit_leaf(Lnast_node::create_ref(stream_ssa_active_def_->output));
      } else if (idx == 0 && stream_mutation && Lnast_ntype::is_ref(lm->get_raw_ntype())
                 && stream_ssa_ref_name(lm->current_text()) != lm->current_text()) {
        emit_leaf(Lnast_node::create_ref(stream_ssa_ref_name(lm->current_text())));
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
      } else if (!x_fill.empty() && idx == 1) {
        emit_leaf(Lnast_node::create_const(x_fill));  // `0sb?` filled to the dst's declared width
      } else if (!concat_widths.empty() && idx >= 2 && (idx % 2) == 0 && !concat_widths[idx / 2 - 1].empty()) {
        // A concat WIDTH operand (children 2, 4, 6, … pair with lanes 1, 3, 5, …)
        // that we just resolved: emit the bound width in place of the `nil`.
        emit_leaf(Lnast_node::create_const(concat_widths[idx / 2 - 1]));
      } else {
        // Either the LHS (don't fold — it's a dst) or a non-ref child (const,
        // or a nested subtree).
        emit_subtree_verbatim();
      }
      ++idx;
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }

  // A call that reaches the materialized tree becomes a Sub INSTANCE (an
  // inlined one never gets here — the splice consumed it). Inside an unrolled
  // body each copy of that one source call site is a SEPARATE instance, so tag
  // it with the iteration ordinal while we still know it; tolg appends the tag
  // to whatever instance name it derives.
  if (is_call && !loop_iter_ordinals_.empty()) {
    stamp_loop_inst_suffix(call_callee);
  }

  emit_pop();
  stream_ssa_active_def_ = saved_active_def;
}

void uPass_runner::stamp_loop_inst_suffix(std::string_view callee) {
  // Only a call tolg lowers to a Sub is tagged. A builtin (`__memory`) reads a
  // closed argument vocabulary and an unresolved callee binds positionally, so
  // an extra marker store there is an unknown-argument error, not a name.
  std::string name(callee);
  if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'') {
    name = name.substr(1, name.size() - 2);  // an import-bound callee folds to a quoted string
  }
  bool becomes_instance = name.starts_with("lg:");
  if (!becomes_instance) {
    auto       c     = lookup_callee(name);
    const auto k     = c ? c->get_lambda_kind() : std::string_view{};
    becomes_instance = (k == "mod" || k == "pipe" || k == "fluid" || k == "comb");
  }
  if (!becomes_instance) {
    return;
  }
  // Already tagged: this is a re-emit of a call the unroller stamped on an
  // earlier run (emit_named_instance_call re-walks its own output).
  for (auto c : staging->children(staging_parent)) {
    if (!Lnast_ntype::is_store(staging->get_type(c))) {
      continue;
    }
    auto k = staging->get_first_child(c);
    if (!k.is_invalid() && staging->get_name(k) == call_inst_suffix_marker) {
      return;
    }
  }
  auto st = staging->add_child(staging_parent, Lnast_ntype::create_store());
  staging->add_child(st, Lnast_node::create_ref(std::string(call_inst_suffix_marker)));
  staging->add_child(st, Lnast_node::create_const(loop_inst_suffix()));
}

// ── Pass dispatch ─────────────────────────────────────────────────────────────

namespace {

// A pass that threw is REPORTED, not printed. stderr is not a channel the CLI
// reads: it honours neither -q nor --diag-fmt json, never reaches an
// `--emit diagnostics:` consumer, and never enters the result envelope -- so a
// pass whose exception the dispatcher swallows used to leave the run reporting
// success with the reason on a scrolled-past line.
//
// A throw out of `diag::…​.fatal()` has ALREADY reported: Sink::fatal emits the
// record and then throws a runtime_error carrying that same message. Match on
// it so the specific diagnostic stands alone instead of being restated (and
// double-counted) as a generic one.
void report_pass_exception(std::string_view where, const std::runtime_error& ex) {
  const std::string_view what{ex.what()};
  for (const auto& r : livehd::diag::sink().records()) {
    if (r.severity == livehd::diag::Severity::error && r.message == what) {
      return;
    }
  }
  livehd::diag::err("pass.upass", "pass-exception", "internal").msg("{}: {}", where, what).emit();
}

}  // namespace

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
        report_pass_exception(entry.name, ex);
      }
      entry.stat_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
      ++entry.stat_calls;
      lm->restore_cursor(saved);
      continue;
    }
    try {
      (entry.pass.get()->*fn)();
    } catch (const std::runtime_error& ex) {
      report_pass_exception(entry.name, ex);
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

bool uPass_runner::track_param_provenance() {
  // Keep the dataflow derived from module parameters symbolic in emitted
  // source. Evaluation still sees the bound constants in the symbol table;
  // only producer removal and materialized operands are affected.
  if (preserved_param_names_.empty() || lm->get_lnast().get() != root_lnast_.get() || !lm->has_child()) {
    return false;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return false;
  }
  const std::string dst(lm->current_text());
  bool              derived = preserved_param_names_.contains(dst);
  auto              scan    = [&](auto&& self) -> void {
    if (Lnast_ntype::is_ref(lm->get_raw_ntype()) && preserved_param_names_.contains(std::string(lm->current_text()))) {
      derived = true;
    }
    if (!lm->has_child()) {
      return;  // a leaf: move_to_child() would INVALIDATE the cursor and kill the caller's sibling loop
    }
    lm->move_to_child();
    do {
      self(self);
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  };
  while (lm->move_to_sibling()) {
    scan(scan);
  }
  lm->restore_cursor(saved);
  if (derived) {
    preserved_param_names_.insert(dst);
  }
  return derived;
}

// 1i-inline — `name` is an INLINED callee's output-port local whose store is a
// real hardware driver and must survive constprop's "every consumer folds it"
// drop vote. Gated on a HARDWARE scalar: a `comb` returning a comptime
// string/type/tuple must still fold away completely. Shared by both drop paths
// so the two can never disagree.
bool uPass_runner::is_inline_output_driver(std::string_view name) const {
  if (!inline_output_names_.contains(name)) {
    return false;
  }
  const auto v = symbol_table_.known_const_scalar(name);
  return v && !v->is_invalid() && !v->is_nil() && !v->is_string() && (v->is_integer() || v->is_bool());
}

void uPass_runner::process_drop_candidate(Pass_method fn, bool fold_all) {
  const bool parameter_expr = !fold_all && track_param_provenance();
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
  if (!drop && !parameter_expr && lm->get_raw_ntype() != Lnast_ntype::Lnast_ntype_cassert && lm->has_child()) {
    const auto here = lm->save_cursor();
    lm->move_to_child();
    if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
      const auto name = lm->current_text();
      const auto b    = symbol_table_.get_bundle(name);
      const auto mode = b ? b->get_mode() : upass::Mode::unknown;
      if (mode != upass::Mode::reg_kind && mode != upass::Mode::mut_kind && mode != upass::Mode::wire_kind) {
        drop = symbol_table_.known_const_scalar(name).has_value() && !is_inline_output_driver(name);
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
      auto       cit = const_parse_cache_.find(txt);
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
        cit                = const_parse_cache_.emplace(std::string(txt), Parsed_const{std::move(v), k, pattern}).first;
      }
      const auto& pc = cit->second;
      out.src.push_back(upass::Operand{std::string_view{}, Bundle::make_const(pc.value, pc.kind), pc.pattern});
    } else if (Lnast_ntype::is_ref(t)) {
      const auto name = lm->current_text();
      if (!bundle_key::is_single_level(name)) {  // backtick-aware: `` `a.b` `` is one name
        // A dotted ref operand is an explicit field READ. Detupled wire/reg
        // leaves are read through plain refs (never tuple_get), so this is
        // the only place those reads are visible — constprop's
        // unset-unused-field warning consults the set.
        symbol_table_.field_touched.insert(Symbol_table::field_touch_key(lm->unit_lnast()->get_top_module_name(), name));
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
    // Push hooks receive fully-resolved operands and are required to leave the
    // read cursor where they found it. Restoring the bookmark unconditionally
    // for every pass on every operation dominated generated RTL (tens of
    // millions of successful dispatches), so the restore now runs only on the
    // exceptional path; the balance is asserted in debug builds. The bookmark
    // itself stays complete (nid + parent-stack depth): a handler that throws
    // mid-descent has also PUSHED parents, and leaving those on the stack makes
    // every later move_to_parent() return the wrong node.
    const auto here = lm->save_cursor();
    const auto t0   = dispatch_stats_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    try {
      if ((entry.pass.get()->*fn)(rn.dst_name, *rn.dst, upass::Src_span{rn.src}) == upass::Vote::drop) {
        any_drop = true;
      }
    } catch (const std::runtime_error& e) {
      report_pass_exception(entry.name, e);
      lm->restore_cursor(here);
    }
    I(lm->get_current_nid() == here.current, "push handler left the LNAST cursor unbalanced");
    if (dispatch_stats_) {
      entry.stat_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
      ++entry.stat_calls;
    }
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
  if (!rn.dst_name.empty() && bundle_key::is_single_level(rn.dst_name)) {
    const auto root = std::string(Bundle::get_first_level(rn.dst_name));
    // resolve_node_operands already unshared this dst before dispatch. A
    // second writable lookup would see rn.dst's reference and clone it again,
    // forcing a whole-array fact merge even when no pass replaced the slot.
    auto       now  = symbol_table_.peek_writable_bundle(root) == rn.dst.get() ? rn.dst : symbol_table_.get_bundle_for_write(root);
    if (now) {
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
        if (!Lnast::is_tmp(rn.dst_name) && !e0.trivial.is_invalid() && e0.trivial.is_integer() && !e0.trivial.has_unknowns()
            && !e0.bw_max.is_invalid() && !e0.bw_min.is_invalid()) {
          const bool over  = e0.trivial.gt_op(e0.bw_max)->is_known_true();
          const bool under = e0.trivial.lt_op(e0.bw_min)->is_known_true();
          if ((over || under) && !livehd::diag::sink().has_errors()) {
            auto user = upass::Lnast_manager::user_name(Bundle::get_first_level(rn.dst_name));
            user      = user.substr(0, user.find("___ssa_"));  // the variable, not its SSA version
            livehd::diag::sink().emit(livehd::diag::Diagnostic{
                .severity = livehd::diag::Severity::error,
                .code     = "bitwidth-overflow",
                .category = "bitwidth",
                .pass     = "upass.runner",
                .message  = std::format("`{}` (value {}) does not fit its declared range [{}, {}]",
                                       user,
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
  if (!symbol_table_.pending_decl_facts.empty() && !rn.dst_name.empty()) {
    apply_pending_field_facts(Bundle::get_first_level(rn.dst_name));
  }
  return any_drop;
}

// Drain one root's dotted-bake stash: any pending field whose root binding
// now holds a trivial at that path gets its declared facts written (and the
// pending entry erased). Only the just-written destination root can have
// materialized a field, so use the reverse index instead of rescanning facts
// belonging to every other live root after every dispatched node.
void uPass_runner::apply_pending_field_facts(std::string_view root) {
  auto root_it = symbol_table_.pending_keys_by_root.find(root);
  if (root_it == symbol_table_.pending_keys_by_root.end()) {
    return;
  }

  auto& pending = symbol_table_.pending_decl_facts;
  auto& keys    = root_it->second;
  auto  out     = keys.begin();
  for (const auto& key : keys) {
    auto it = pending.find(key);
    if (it == pending.end()) {
      continue;
    }
    const auto    fpath = Bundle::get_all_but_first_level(key);
    // READ-ONLY probe first: never clone just to poll. This stash is drained
    // once per dispatched node, and the COW unshare inside get_bundle_for_write
    // (the old probe) cloned the whole root bundle on every poll — O(N^2) on a
    // large module. A null root binding means the field has no live write-scope
    // (its declaring scope was left): skip; the entry is dropped at the root's
    // scope exit (Symbol_table::leave_scope) so the stash stays bounded.
    const Bundle* rb_ro = symbol_table_.peek_writable_bundle(root);
    if (rb_ro == nullptr || !rb_ro->has_trivial(bundle_path::of_string(fpath))) {
      *out++ = key;
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
    pending.erase(it);
  }
  keys.erase(out, keys.end());
  if (keys.empty()) {
    symbol_table_.pending_keys_by_root.erase(root_it);
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
  const bool    parameter_expr = !fold_all && track_param_provenance();
  Resolved_node rn;
  if (!resolve_node_operands(rn)) {
    // No leading dst ref: still push-dispatch with a throwaway dst (hooks
    // that need the payload walk the cursor themselves).
    rn.dst = std::make_shared<Bundle>("");
  }
  if (Lnast_ntype::is_concat(lm->get_raw_ntype())) {
    check_concat_lanes();  // dispatch path: runs in EVERY tier, unlike emission
  } else if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_get_mask) {
    check_bitsel_named_bundle();  // same rule, reached through `x#[..]`
  }
  const bool vote_drop = dispatch_push(fn, rn);
  // Record the just-defined variable into the LSP semantic
  // index, now that this op's bitwidth/kind facts are written. Gated on the
  // LSP-only global flag, so the normal CLI never enters record_lsp_def.
  if (livehd::lsp_index::index().enabled()) {
    record_lsp_def(rn.dst_name);
  }
  // A known-constant output is still a hardware driver. The legacy SSA tree
  // seeded flattened outputs in the symbol table, which made constprop keep
  // these stores implicitly; the streaming source retains a composite io
  // node, so make the output-root rule explicit (e.g. `o.id = 0`).
  //
  // 1i-inline — the SAME rule for an INLINED callee's output port. constprop
  // votes DROP for every store whose dst folds to a known constant, on the
  // theory that "every consumer folds the value, so the producer is dead".
  // That theory is FALSE once a later write under an UNCERTAIN scope
  // invalidates the constant: the post-`if` read is a runtime mux, not a fold,
  // and with the producing store gone tolg has nothing driving the else arm and
  // synthesizes the universal unknown `0sb1????…`. A constant DEFAULT followed
  // by a conditional override — 04-variables.md: "The last write in program
  // order wins, so an unconditional write is the default for the cycle and a
  // later conditional write overrides it" — was therefore silently miscompiled
  // whenever the `comb` holding it was inlined, with no error and no warning:
  //   comb Fwd(rs:u5) -> (sel:u3) { sel = 0 ; if rs != 0 { sel = 2 } }
  // emitted `else sel = 0` as its own module and `else sel = 65'sb1????…` when
  // spliced into a caller. (equiv+sim fixtures: inline_const_default.)
  //
  // Gated on a HARDWARE scalar: a `comb` returning a comptime string/type/tuple
  // must still fold away completely (the comptime suite depends on it), and a
  // constant PARAMETER binding is not an output so it keeps folding either way
  // — that is what generic `<N=SIZE>` specialization rides on.
  //
  // A defaulted input's default-value local (todo 3g E) is the same kind of
  // root: nothing in this unit reads it (its own module drives the port from
  // the input), but a later inliner of this materialized body binds the param
  // from it at every call that omits the arg, so a constant default must
  // survive as a store instead of folding away with its (absent) readers.
  const bool output_driver        = io_output_names_.contains(rn.dst_name) || default_value_names_.contains(rn.dst_name);
  const bool inline_output_driver = !output_driver && vote_drop && is_inline_output_driver(rn.dst_name);
  if ((!vote_drop || output_driver || inline_output_driver || parameter_expr) && !any_pass_drops()) {
    emit_op_with_fold(fold_all);
  }
}

namespace {

// The scalar type of one bundle leaf for the LSP hover, from the
// Entry's own facts alone: kind (bool/string/enum), else integer with bits
// recomputed from the declared envelope and the bw_min/bw_max (or comptime
// value) range labeled the same way a plain variable renders. Used for tuple
// FIELDS, where the io_meta/bw_meta side-channels don't apply (write_bw skips
// dotted names — the per-field range lives on the bundle Entry itself).
std::string lsp_render_leaf_type(const Bundle::Entry& fe) {
  switch (fe.kind) {
    case upass::Kind::boolean: return "Bool";
    case upass::Kind::string : return "String";
    case upass::Kind::enumv  : return "enum";
    case upass::Kind::tuple  : return "tuple";
    default                  : break;
  }
  if (!fe.trivial.is_invalid() && fe.trivial.is_string()) {
    return "String";
  }
  if (!fe.trivial.is_invalid() && fe.trivial.is_bool()) {
    return "Bool";
  }
  const auto to_i64 = [](const Dlop& v) -> std::optional<int64_t> {
    if (v.is_invalid() || !v.is_integer() || v.has_unknowns() || v.get_signed_bits() > 62) {
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

  // Declared WIDTH from the (max, min) type Consts. get_signed_bits() is the signed
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
    const auto bw = [](const Dlop& v) -> int { return (v.is_invalid() || !v.is_integer()) ? 0 : v.get_signed_bits(); };
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
    // no declared width and no derived/point range: a width-less `Unsigned`
    // (min=0, max=nil) keeps its declared sign
    const bool unsigned_decl = !fe.decl_min.is_invalid() && fe.decl_min.is_integer() && !fe.decl_min.is_negative();
    return unsigned_decl ? "Unsigned" : "Signed";
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
  std::string r(1, sgn ? 'S' : 'U');
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

// A whole tuple for the LSP hover: each leaf with its scalar render,
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

// See the header. Statement-granularity span (operand refs
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
    if (v.is_invalid() || !v.is_integer() || v.has_unknowns() || v.get_signed_bits() > 62) {
      return std::nullopt;
    }
    return v.to_just_i64();
  };
  // Minimal bit width to hold a range, matching upass_bitwidth's storage rule.
  const auto ubits = [](int64_t hi) -> int { return hi <= 0 ? 0 : static_cast<int>(std::bit_width(static_cast<uint64_t>(hi))); };
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
  std::optional<int>     dbits;                // declared bit width (prefix)
  bool                   dsigned     = false;  // declared sign
  bool                   dsign_known = false;  // dsigned comes from a declared integer type
  std::optional<int64_t> dlo;                  // declared value envelope
  std::optional<int64_t> dhi;
  Io_kind                io_kind    = Io_kind::none;  // declared bool/string on an IO leaf
  // Derive the declared width + i64 envelope from a type's (max, min) Consts.
  // get_signed_bits() handles >62-bit types (u64/u128) whose bounds don't fit int64,
  // so dbits is set even when to_i64 (dlo/dhi) cannot represent the bound.
  const auto             apply_decl = [&](const Dlop& dmax, const Dlop& dmin) {
    if ((dmax.is_invalid() || !dmax.is_integer()) && (dmin.is_invalid() || !dmin.is_integer())) {
      return;
    }
    dsigned       = !dmin.is_invalid() && dmin.is_integer() && dmin.is_negative();
    dsign_known   = !dmin.is_invalid() && dmin.is_integer();
    const auto bw = [](const Dlop& v) -> int { return (v.is_invalid() || !v.is_integer()) ? 0 : v.get_signed_bits(); };
    const int  b  = dsigned ? std::max(bw(dmax), bw(dmin)) : (dmax.is_known_zero() ? 0 : bw(dmax) - 1);
    if (b > 0) {
      dbits = b;
    }
    dlo = to_i64(dmin);
    dhi = to_i64(dmax);
  };
  {
    const auto&           io    = lm->get_lnast()->io_meta();
    const Lnast_io_entry* found = nullptr;
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
      dsigned     = found->is_signed;
      dsign_known = found->kind == Io_kind::integer;
      io_kind     = found->kind;
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
      // An empty sub-bundle is only a structural placeholder.  Its scalar
      // declaration type lives in the pending entry on the parent bundle.
      if (!leaves.empty()) {
        if (leaves.size() == 1 && leaves.begin()->first == "0") {
          // a lone positional leaf is the field's scalar, not a nested tuple
          render += lsp_render_leaf_type(lsp_overlay_pending(leaves.begin()->second, symbol_table_, base_s));
        } else {
          render += lsp_render_tuple(*sb, symbol_table_, base_s);
        }
        rendered = true;
      }
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
    render += "Bool";
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
      callee_shape
          = callee_shape && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.');
    }
    if (const auto fn = callee_shape ? lookup_callee(txt) : nullptr; fn) {
      const auto lk  = fn->get_lambda_kind();
      render        += lk.empty() ? std::string_view("fun") : lk;
      render        += ' ';
      render        += txt;
    } else {
      render += "String";
    }
  } else {
    const bool have_decl = dlo && dhi;  // declared envelope fits int64
    const bool have_inf  = ilo && ihi;  // inferred range known
    if (!have_decl && !have_inf && !dbits) {
      // no declared width and no derived range: a width-less `x:Unsigned` keeps its sign
      render += (dsign_known && !dsigned) ? "Unsigned" : "Signed";
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
      render += sgn ? 'S' : 'U';
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
      f.inlinable        = true;
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
  // Delegated to the resolver (name resolution is its job). The caller's own
  // unit name gives lexical priority: a nested helper resolves to THIS scope's
  // definition before any same-named sibling-scope one. Inside an inline frame
  // the scope is the INLINED callee's own unit: a bare `helper(...)` in an
  // imported comb's body is that file's helper, never a same-named one the
  // caller's file defines (inlining the caller's helper is a silent miscompile).
  return upass::call_resolver::lookup_callee(reg().function_registry, name, lexical_unit());
}

std::string_view uPass_runner::lexical_unit() const {
  if (const auto* frame = lm ? lm->inline_frame_lnast() : nullptr) {
    return frame->get_top_module_name();
  }
  return root_lnast_->get_top_module_name();
}

std::string uPass_runner::frame_portable_func_name(const std::string& name, std::string_view callee_unit) const {
  // A function-valued actual is captured by its raw spelling in the CALLER's
  // scope, but the body that calls it resolves in the callee's scope (see
  // lookup_callee). When the two scopes pick different bodies, pin the binding
  // to the caller's choice by its full registry name; otherwise keep the raw
  // spelling (unchanged naming for the common case).
  const auto here = lookup_callee(name);
  if (!here) {
    return name;
  }
  const auto there = upass::call_resolver::lookup_callee(reg().function_registry, name, callee_unit);
  return there == here ? name : std::string(here->get_top_module_name());
}

std::string uPass_runner::frame_portable_callee_name(const std::string& name, const std::shared_ptr<Lnast>& callee) {
  // Inside a spliced body the callee resolved in the INLINED unit's scope
  // (lookup_callee), but the re-emitted call is re-walked with no frame and
  // tolg resolves it against the root unit: a bare `helper` in an imported
  // generic's body would bind the CALLER file's same-named helper there (a
  // silent wrong Sub instance). Pin the full registry name when the two scopes
  // disagree; keep the spelling otherwise (unchanged instance naming).
  if (!callee || !lm || !lm->in_inline_frame()) {
    return name;
  }
  const auto root_unit = root_lnast_->get_top_module_name();
  const auto pinned    = std::string(callee->get_top_module_name());
  if (upass::call_resolver::lookup_callee(reg().function_registry, name, root_unit) != callee) {
    return pinned;
  }
  // The registry agrees, but the re-walk also honors a VALUE binding of the
  // bare spelling in the caller's scope (`const helper = import("lib2.hh")`,
  // see the callee_var fold in try_inline_call), which shadows the by-name hit.
  // The frame tags its own variables (`inl<N>_x`), so an untagged binding here
  // is the caller's. Any such binding that does not name `callee` itself would
  // hijack the Sub instance: pin.
  if (auto fv = try_fold_ref(name)) {
    auto fn = fv->is_string() ? fv->to_pyrope() : std::string{};
    if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
      fn = fn.substr(1, fn.size() - 2);
    }
    if (fn.starts_with("ln:")) {
      fn = fn.substr(3);
    }
    if (fn.empty() || upass::call_resolver::lookup_callee(reg().function_registry, fn, root_unit) != callee) {
      return pinned;
    }
  }
  return name;
}

std::string uPass_runner::value_bound_func_name(std::string_view var, std::string_view raw) {
  const auto by_name = lookup_callee(raw);
  if (!by_name) {
    return {};  // no by-name hit to shadow: the value path binds the alias
  }
  auto fv = try_fold_ref(var);
  if (!fv || !fv->is_string()) {
    return {};
  }
  auto fn = fv->to_pyrope();
  if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
    fn = fn.substr(1, fn.size() - 2);
  }
  if (fn.starts_with("ln:")) {
    fn = fn.substr(3);
  }
  if (fn.empty()) {
    return {};
  }
  const auto target = lookup_callee(fn);
  if (!target || target == by_name) {
    return {};
  }
  return std::string(target->get_top_module_name());
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
  if (symbol_table_.in_uncertain_scope()) {
    return;  // one if-arm's cast/bit-select force must not become the target's DECLARED envelope
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

void uPass_runner::process_bit_selection() {
  // Stamp a compiler temp's selected width as its declared ENVELOPE ONLY --
  // decl_max/decl_min on its scalar entry -- never through a scratch
  // `type_spec` node. decl_facts::lookup derives range, bits and (from
  // min >= 0) unsignedness from it; the temp's kind is left alone.
  const auto stamp_envelope = [&](const std::string& dst, uint32_t w) {
    if (!symbol_table_.has_known(dst)) {
      (void)symbol_table_.set(dst, std::make_shared<Bundle>(dst));
    }
    if (const auto b = symbol_table_.get_bundle_for_write(dst);
        b && (b->is_empty() || b->has_trivial(bundle_path::of_string("0")))) {
      Bundle::Entry e = b->get_entry(bundle_path::of_string("0"));
      e.immutable     = false;
      if (e.decl_max.is_invalid()) {
        e.decl_max = upass::max_from_bits(w, false);
      }
      if (e.decl_min.is_invalid()) {
        e.decl_min = upass::min_from_bits(w, false);
      }
      b->set(bundle_path::of_string("0"), e);
      declared_typed_.insert(dst);
    }
  };
  const auto              saved = lm->save_cursor();
  std::vector<Lnast_node> kids;
  if (lm->move_to_child()) {
    do {
      kids.push_back(Lnast_ntype::is_const(lm->get_raw_ntype()) ? Lnast_node::create_const(std::string(lm->current_text()))
                                                                : Lnast_node::create_ref(std::string(lm->current_text())));
    } while (lm->move_to_sibling());
  }
  lm->restore_cursor(saved);
  if (kids.size() < 3 || kids.size() > 4) {
    return;
  }
  const std::string dst(kids[0].get_name());
  auto              value = kids[1];
  const auto        fold  = [&](const Lnast_node& n) -> std::optional<Dlop> {
    return n.is_const() ? std::optional<Dlop>(*Dlop::from_pyrope(n.get_name())) : try_fold_ref(n.get_name());
  };
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  const auto bundle    = value.is_const() ? nullptr : symbol_table_.get_bundle(value.get_name());
  const bool aggregate = bundle && !lm->get_lnast()->is_verilog_origin() && bundle->get_attr("rng_s").is_invalid()
                         && bundle->get_attr("enumentry").is_invalid() && bundle->get_attr("enumval").is_invalid()
                         && (bundle->has_named_top() || bundle->unnamed_top_count() > 1
                             || (!bundle->is_scalar() && concat_array_lane_bits(*bundle) != 0));
  if (aggregate) {
    auto packed = std::make_shared<Lnast>(scratch_forest_->create_tree_temp("bitsel-pack"), "bitsel-pack");
    auto root   = packed->set_root(Lnast_ntype::create_concat());
    stamp_scratch_srcid(packed, root);
    const std::string name = dst + "_packed";
    packed->add_child(root, Lnast_node::create_ref(name));
    packed->add_child(root, value);
    packed->add_child(root, Lnast_node::create_const("nil"));
    flush_deferred_emits();
    lm->push_source(packed, "", 0);
    concat_checked_.erase(lm->get_current_nid());
    process_lnast();
    flush_deferred_emits();
    lm->pop_source();
    kids[1] = value = Lnast_node::create_ref(name);
  }
  bool       rewritten = aggregate;
  const auto lo        = fold(kids[2]);
  auto       hi        = kids.size() == 4 ? fold(kids[3]) : std::optional<Dlop>{};
  if (kids.size() == 4 && hi && hi->is_nil()) {
    uint32_t bits = value.is_const() ? 0 : concat_lane_declared_bits(value.get_name());
    if (bits == 0) {
      if (const auto v = fold(value); v && v->is_integer()) {
        bits = v->get_signed_bits();
      }
    }
    if (bits > 0) {
      rewritten = true;
      kids[3]   = Lnast_node::create_const(static_cast<int64_t>(bits));
      hi        = *Dlop::create_integer(bits);
    }
  }
  if (rewritten) {
    auto selected = std::make_shared<Lnast>(scratch_forest_->create_tree_temp("bitsel"), "bitsel");
    auto root     = selected->set_root(Lnast_ntype::create_get_mask());
    stamp_scratch_srcid(selected, root);
    for (const auto& k : kids) {
      selected->add_child(root, k);
    }
    flush_deferred_emits();
    lm->push_source(selected, "", 0);
    process_drop_candidate_push(static_cast<upass::Push_method>(&upass::uPass::process_get_mask), false);
    flush_deferred_emits();
    lm->pop_source();
  } else {
    process_drop_candidate_push(static_cast<upass::Push_method>(&upass::uPass::process_get_mask), false);
  }
  int64_t width = runtime_range_width(saved.current).value_or(0);
  if (lo && hi && lo->is_just_i64() && hi->is_just_i64() && !lo->has_unknowns() && !hi->has_unknowns()) {
    width = hi->to_just_i64() - lo->to_just_i64();
  }
  if (width > 0 && width <= std::numeric_limits<int>::max()) {
    if (const auto b = symbol_table_.get_bundle_for_write(dst);
        b && !b->is_empty() && !b->has_trivial(bundle_path::of_string("0")) && !b->has_named_top() && b->unnamed_top_count() == 0
        && std::all_of(b->get_attrs().begin(), b->get_attrs().end(), [](const auto& a) {
             return a.first.starts_with("__array_") || a.first.starts_with("__elem_");
           })) {
      std::vector<std::string> attrs;
      for (const auto& a : b->get_attrs()) {
        attrs.emplace_back(a.first);
      }
      for (const auto& a : attrs) {
        b->clear_attr(a);
      }
    }
    if (lm->get_lnast()->is_verilog_origin() || (Lnast::is_tmp(dst) && symbol_table_.in_uncertain_scope())) {
      stamp_envelope(dst, static_cast<uint32_t>(width));
    } else {
      emit_inline_typespec(dst, static_cast<int>(width), false);
    }
  }
}

void uPass_runner::process_bit_update() {
  process_drop_candidate_push(static_cast<upass::Push_method>(&upass::uPass::process_set_mask), false);
}

// The integer type of a TYPED value (user rulings 26, 38, 44), or nullopt.
// Typed means the value states its type: a name declared `uN`/`sN` /
// `unsigned(bits=N)` (a variable, a port, a tuple field or instance output read
// through its temp), an untyped `const` alias of one (ruling 38), a compiler
// temp whose producer stamped a `uN` envelope (a bit slice `x#[..]`, a `uN(...)`
// cast), or the result of a `~` of a typed value, of a bitwise and/or/xor over
// typed operands, or of a cast that folded at comptime (typed_expr_types_). A
// bool, a literal and an untyped value (whose runtime `.[bits]` is nil) are not.
std::optional<uPass_runner::Int_type> uPass_runner::typed_int_of(std::string_view name) const {
  if (name.empty()) {
    return std::nullopt;
  }
  if (const auto it = typed_expr_types_.find(name); it != typed_expr_types_.end()) {
    return it->second;
  }
  const auto* ln       = lm ? lm->get_lnast().get() : nullptr;
  const auto  declared = [&](std::string_view n) -> std::optional<Int_type> {
    using Num    = upass::decl_facts::Num;
    const auto f = upass::decl_facts::lookup(symbol_table_, ln, n);
    if (!f || !f->has_type_spec) {
      return std::nullopt;
    }
    if (f->kind == Num::none && f->bits == 0 && !f->range_max && !f->range_min) {
      return Int_type{.is_signed = true};  // `x:signed`: an unbounded signed integer
    }
    // An unsigned type with no width (`x:unsigned`) states no bits to flip.
    if ((f->kind != Num::unsigned_int || f->bits == 0) && f->kind != Num::signed_int) {
      return std::nullopt;
    }
    Int_type t{.is_signed = f->kind == Num::signed_int, .bits = f->bits};
    if (f->range_max && f->range_min) {
      t.max = *f->range_max;
      t.min = *f->range_min;
    }
    return t;
  };
  // Typed by its own declaration or type_spec; an SSA version reads its base's.
  if (declared_typed_.contains(name)) {
    return declared(name);
  }
  std::string_view base = name;
  if (const auto p = base.find("___ssa_"); p != std::string_view::npos) {
    base = base.substr(0, p);
  }
  if (declared_typed_.contains(base)) {
    return declared(base);
  }
  // A port of the unit: its signature declares it (an unbounded `a:signed`
  // port pins no bits, so decl_facts has nothing for it).
  if (const auto* pe = ln != nullptr ? ln->io_meta().find(base) : nullptr) {
    if (pe->kind == Io_kind::integer && pe->is_signed && pe->bits == 0 && !pe->has_range && !pe->wide_range_min) {
      return Int_type{.is_signed = true};
    }
    return declared(base);
  }
  // A read temp typed by what it reads: a declared tuple field or an instance
  // output (`t.f`, `inst.o`).
  if (Lnast::is_tmp(name)) {
    if (const auto o = symbol_table_.tget_origin.find(name); o != symbol_table_.tget_origin.end()) {
      return declared(o->second);
    }
  }
  // A single-output instance handle, through its temp or the name it was
  // bound to (`stage[1] m = st(d=x)`, `const c = child(a=x)`): its output's type.
  for (const auto n : {name, base}) {
    if (const auto so = symbol_table_.single_output_port.find(n); so != symbol_table_.single_output_port.end()) {
      return declared(absl::StrCat(n, ".", so->second));
    }
  }
  return std::nullopt;
}

void uPass_runner::inherit_alias_type(const std::string& dst, const std::string& src) {
  // An arithmetic result stays untyped (`const y = a + 1` has a nil `.[bits]`,
  // ruling 6): only a plain copy of a typed name reaches here. A `mut` is a
  // variable, not an alias, and a Verilog-read unit keeps Verilog widths. The
  // temp of an `if`/`match` expression is written once per arm: one typed arm
  // does not type it (`if c { a } else { a + 1 }` is untyped).
  if (dst == src || declared_typed_.contains(dst) || (lm->get_lnast() && lm->get_lnast()->is_verilog_origin())
      || symbol_table_.is_conditional_write(dst)) {
    return;
  }
  if (bundle_key::find_top_dot(dst) != std::string_view::npos) {
    return;  // a field store (`c.addr` of a detupled `const c:(..) = call()`), not an alias of a bare var
  }
  const auto* ln = lm->get_lnast().get();
  if (ln != nullptr && ln->io_meta().find(dst) != nullptr) {
    return;  // a port: its signature types it (an untyped output's range is derived, ruling 28)
  }
  const auto df = upass::decl_facts::lookup(symbol_table_, ln, dst);
  if (!df || df->mode != upass::Mode::const_kind) {
    return;
  }
  const auto ty = typed_int_of(src);
  if (!ty) {
    return;
  }
  const auto b = symbol_table_.get_bundle_for_write(dst);
  if (!b || b->has_named_top() || b->unnamed_top_count() > 1) {
    return;  // a tuple alias carries its fields' own types
  }
  Bundle::Entry e = b->get_entry(bundle_path::of_string("0"));
  e.immutable     = false;
  if (ty->bits == 0 && ty->max.is_invalid()) {
    e.kind = upass::Kind::integer;  // an unbounded `signed`: typed, with no range (`.[max]` is nil)
  } else {
    e.decl_max = ty->max.is_invalid() ? upass::max_from_bits(ty->bits, ty->is_signed) : ty->max;
    e.decl_min = ty->min.is_invalid() ? upass::min_from_bits(ty->bits, ty->is_signed) : ty->min;
  }
  b->set(bundle_path::of_string("0"), e);
  declared_typed_.insert(dst);
}

// An element of a declared `[N]uW` array is a uW (ruling 26 types `~arr[i]`
// like `~x` of a `uW` x), whether the index is a constant or runtime. Only a
// ONE-dimensional array read with one index: a multi-dimensional read may
// stop at a packed row.
std::pair<std::string, uint32_t> uPass_runner::array_elem_read_bits() const {
  if (!lm->has_child() || (lm->get_lnast() && lm->get_lnast()->is_verilog_origin())) {
    return {};
  }
  const auto  saved = lm->save_cursor();
  std::string dst;
  std::string src;
  int         n_idx = 0;
  lm->move_to_child();
  if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    dst = std::string(lm->current_text());
    if (lm->move_to_sibling() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
      src = std::string(lm->current_text());
      while (lm->move_to_sibling()) {
        ++n_idx;
      }
    }
  }
  lm->restore_cursor(saved);
  if (dst.empty() || src.empty() || n_idx != 1) {
    return {dst, 0};
  }
  const auto base = std::string_view(src).substr(0, std::string_view(src).find("___ssa_"));
  if (const auto b = symbol_table_.get_bundle(base); b && !b->get_attr("__array_size").is_invalid()) {
    const auto& size    = b->get_attr("__array_size");
    const auto& flat    = b->get_attr("__array_flat_size");
    const auto& kind    = b->get_attr("__elem_kind");
    const auto& emin    = b->get_attr("__elem_min");
    const bool  one_dim = flat.is_invalid() || flat.same_repr(size);
    const bool  integer
        = kind.is_invalid() || (kind.is_just_i64() && kind.to_just_i64() == static_cast<int64_t>(upass::Kind::integer));
    if (!one_dim || !integer || !emin.is_integer() || emin.is_negative()) {
      return {dst, 0};
    }
    return {dst, array_elem_declared_bits(*b)};
  }
  const auto* ln = lm->get_lnast().get();
  if (const auto* pe = ln != nullptr ? ln->io_meta().find(base) : nullptr;
      pe != nullptr && pe->array_size > 0 && pe->inner_dims.empty() && !pe->elem_signed && pe->elem_bits > 0) {
    return {dst, static_cast<uint32_t>(pe->elem_bits)};
  }
  return {dst, 0};
}

// `~x` (user rulings 26 and 44): on an UNSIGNED-typed operand of known width N
// it flips the N bits, `(2^N - 1) - x`, a uN result; on a signed-typed operand,
// or a compile-time integer (`~5`, `const k = 5; ~k`), it is `-x - 1`; on an
// UNTYPED runtime value (`~(a + 1)`) it is a compile error -- a bitwise not is
// ambiguous unless the type is known. This is the ONE place the choice is
// made. A Pyrope `bit_not`
// whose operand is unsigned-typed is re-issued in its TYPED form
// `bit_not(dst, x, N)`, so every consumer reads the width off the node instead
// of re-deriving it: constprop/bitwidth/typecheck at this dispatch, the loop
// planner's range eval, tolg (the existing `Not` cell plus a `Get_mask` of N
// bits -- no new LGraph cell), and the Pyrope writer. An untyped node is left
// exactly as written (a bare `Not`).
//
// A Verilog-read unit never gets here typed by the runner: its reader already
// states Verilog's context-width `~` explicitly (the typed form for an
// unsigned result, the bare `-x - 1` for a signed one), so re-typing an
// operand's declared width there would change the Verilog meaning.
void uPass_runner::dispatch_bit_not() {
  const auto  saved = lm->save_cursor();
  std::string dst;
  Lnast_node  operand;
  std::string operand_src;  // the operand as the source spelled it (no inline tag)
  uint32_t    stated = 0;   // the width of an already-typed node
  bool        simple = false;
  if (lm->move_to_child()) {
    dst = std::string(lm->current_text());
    if (lm->move_to_sibling()) {
      const auto t = lm->get_raw_ntype();
      simple       = Lnast_ntype::is_ref(t) || Lnast_ntype::is_const(t);
      operand      = Lnast_ntype::is_ref(t) ? Lnast_node::create_ref(std::string(lm->current_text()))
                                            : Lnast_node::create_const(std::string(lm->current_text()));
      operand_src  = std::string(lm->current_raw_text());
      if (lm->move_to_sibling()) {
        simple = false;
        if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
          const auto v = Dlop::from_pyrope(lm->current_text());
          if (v && v->is_just_i64() && v->to_just_i64() > 0 && v->to_just_i64() <= std::numeric_limits<uint32_t>::max()) {
            stated = static_cast<uint32_t>(v->to_just_i64());
          }
        }
      }
    }
  }
  lm->restore_cursor(saved);

  const bool     verilog = lm->get_lnast() && lm->get_lnast()->is_verilog_origin();
  const auto     type    = (simple && !verilog && operand.is_ref()) ? typed_int_of(operand.get_name()) : std::nullopt;
  const uint32_t bits    = type && !type->is_signed ? type->bits : 0;

  // A `bool` operand is typed, just not an integer (the typecheck rule of
  // `~`, reported here for a template input too: an inlined body reads the
  // caller's bool through the unit's io, which typecheck does not see).
  const std::string_view src_name = std::string_view(operand_src).substr(0, operand_src.find("___ssa_"));
  const auto             is_bool  = [&](std::string_view name) {
    if (const auto b = symbol_table_.get_bundle(name); b && upass::decl_facts::bundle_kind(*b) == upass::Kind::boolean) {
      return true;
    }
    return upass::decl_facts::operand_kind(symbol_table_, lm->get_lnast().get(), name) == upass::Kind::boolean
           || upass::decl_facts::operand_kind(symbol_table_, root_lnast_.get(), name) == upass::Kind::boolean;
  };
  if (!verilog && operand.is_ref() && is_bool(operand.get_name())) {
    fcall_arg_fail(lm->current_span(),
                   "type-mismatch-arith",
                   std::format("operator `~` requires integer operands ({}:Bool)", Lnast::is_tmp(src_name) ? "<value>" : src_name),
                   std::format("`!{}` is the logical not of a bool; no implicit conversion — cast explicitly: {}",
                               Lnast::is_tmp(src_name) ? std::string_view{"b"} : src_name,
                               upass::kBoolIntCastHint),
                   "type");
  }
  // A template's own walk has no types for its untyped inputs yet: each
  // specialization (and each inlined call) decides with the bound types.
  if (!verilog && stated == 0 && !type && operand.is_ref() && !root_lnast_->is_template()) {
    const auto folded = try_fold_ref(operand.get_name());
    if (!folded || folded->is_invalid()) {
      fcall_arg_fail(lm->current_span(),
                     "bitnot-untyped",
                     Lnast::is_tmp(src_name)
                         ? std::string{"`~` needs an operand of known type: this expression is an untyped runtime value"}
                         : std::format("`~` needs an operand of known type: `{}` is an untyped runtime value", src_name),
                     "give it a type (`const t:U8 = …`) or slice it (`(x)#[0..<N]`): a bitwise not flips the bits of a declared "
                     "width, and an arithmetic result has none",
                     "type");
    }
  }
  if (bits == 0) {
    process_drop_candidate_push(static_cast<upass::Push_method>(&upass::uPass::process_bit_not), /*fold_all=*/false);
  } else {
    if (!scratch_forest_) {
      scratch_forest_ = hhds::Forest::create();
    }
    auto s    = std::make_shared<Lnast>(scratch_forest_->create_tree_temp("bitnot"), "bitnot");
    auto root = s->set_root(Lnast_ntype::create_bit_not());
    stamp_scratch_srcid(s, root);
    s->add_child(root, Lnast_node::create_ref(dst));
    s->add_child(root, operand);
    s->add_child(root, Lnast_node::create_const(std::to_string(bits)));
    flush_deferred_emits();
    lm->push_source(s, "", 0);
    process_drop_candidate_push(static_cast<upass::Push_method>(&upass::uPass::process_bit_not), /*fold_all=*/false);
    flush_deferred_emits();
    lm->pop_source();
  }
  if (verilog) {
    return;
  }
  // The result of a `~` has its operand's type (`~~x`, `~x & y`): a uN for a
  // typed flip, the signed type for `-x - 1` of a signed value.
  std::optional<Int_type> result;
  if (bits != 0 || stated != 0) {
    result = Int_type{.bits = bits != 0 ? bits : stated};
  } else if (type) {
    result = Int_type{.is_signed = true, .bits = type->bits};
  }
  if (result && !dst.empty()) {
    typed_expr_types_[dst] = *result;
  } else {
    typed_expr_types_.erase(dst);
  }
}

// The type a typecast gives its result (rulings 26, 44): `uN(x)`/`sN(x)` is a
// uN/sN, and `unsigned(x)`/`signed(x)` reinterprets a typed x at its declared
// width. Recorded whatever path lowers the call: a runtime cast stamps its
// temp, but a comptime one folds straight to a value that would otherwise read
// as untyped (`~u8(k)` of a `const k:u3`). nullopt for a non-cast call, or a
// cast whose width is not known.
std::optional<std::pair<std::string, uPass_runner::Int_type>> uPass_runner::typed_cast_result() const {
  if (!lm->has_child() || (lm->get_lnast() && lm->get_lnast()->is_verilog_origin())) {
    return std::nullopt;
  }
  const auto  saved = lm->save_cursor();
  std::string dst;
  std::string callee;
  std::string callee_var;  // frame-tagged spelling: generic_cast_binds_ key
  std::string arg;
  bool        simple = false;
  lm->move_to_child();
  if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    dst = std::string(lm->current_text());
    if (lm->move_to_sibling()) {
      callee     = std::string(lm->current_raw_text());  // an inlined body tag-prefixes it; the builtin name is raw
      callee_var = std::string(lm->current_text());
      if (lm->move_to_sibling() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
        arg    = std::string(lm->current_text());
        simple = !lm->move_to_sibling();
      }
    }
  }
  lm->restore_cursor(saved);
  if (!simple || dst.empty()) {
    return std::nullopt;
  }
  auto tc = upass::classify_typecast(callee);
  if (!tc) {
    if (const auto gb = generic_cast_binds_.find(callee_var); gb != generic_cast_binds_.end()) {
      tc = upass::classify_typecast(gb->second);
    }
  }
  if (!tc) {
    return std::nullopt;
  }
  if (tc->kind == upass::Typecast_kind::to_sized && tc->sized_bits > 0) {
    return std::pair{
        dst,
        Int_type{.is_signed = tc->sized_signed, .bits = static_cast<uint32_t>(tc->sized_bits)}
    };
  }
  if (tc->kind != upass::Typecast_kind::to_uint && tc->kind != upass::Typecast_kind::to_signed) {
    return std::nullopt;
  }
  uint32_t bits = 0;
  if (const auto t = typed_int_of(arg)) {
    bits = t->bits;
  } else if (const auto f = upass::decl_facts::lookup_operand(symbol_table_, lm->get_lnast().get(), arg);
             f && f->has_type_spec && f->kind == upass::decl_facts::Num::signed_int) {
    bits = f->bits;
  }
  if (bits == 0) {
    return std::nullopt;
  }
  return std::pair{
      dst,
      Int_type{.is_signed = tc->kind == upass::Typecast_kind::to_signed, .bits = bits}
  };
}

// A bitwise and/or/xor: its result is typed when EVERY operand is (a literal
// or an untyped operand makes it untyped): unsigned at the widest operand's
// width when all operands are, else signed wide enough for every operand. So
// a negated form -- nand
// `~(a & b)`, nor, xnor -- follows the same rule as a plain `~x` (rulings 26,
// 44).
void uPass_runner::dispatch_bitwise(upass::Push_method fn) {
  std::string             dst;
  std::optional<Int_type> type;
  std::vector<Int_type>   operands;
  if (!lm->get_lnast() || !lm->get_lnast()->is_verilog_origin()) {
    const auto saved = lm->save_cursor();
    if (lm->move_to_child()) {
      dst = std::string(lm->current_text());
      while (lm->move_to_sibling()) {
        const auto t = Lnast_ntype::is_ref(lm->get_raw_ntype()) ? typed_int_of(lm->current_text()) : std::nullopt;
        if (!t) {
          operands.clear();
          break;
        }
        operands.push_back(*t);
      }
    }
    lm->restore_cursor(saved);
  }
  // Mixed signs make a signed result, where an unsigned N-bit operand needs
  // N+1 bits (`u8 & s4` spans [0, 255]); an unbounded signed operand leaves
  // it unbounded.
  if (!operands.empty()) {
    const bool is_signed = std::any_of(operands.begin(), operands.end(), [](const Int_type& t) { return t.is_signed; });
    uint32_t   bits      = 0;
    for (const auto& t : operands) {
      if (t.is_signed && t.bits == 0) {
        bits = 0;
        break;
      }
      bits = std::max(bits, t.bits + (is_signed && !t.is_signed ? 1 : 0));
    }
    type = Int_type{.is_signed = is_signed, .bits = bits};
  }
  process_drop_candidate_push(fn, /*fold_all=*/false);
  if (dst.empty()) {
    return;
  }
  if (type) {
    typed_expr_types_[dst] = *type;
  } else {
    typed_expr_types_.erase(dst);
  }
}

std::optional<int64_t> uPass_runner::runtime_range_width(const Lnast_nid& stmt) {
  const auto& ln = lm->get_lnast();

  // Compiler temps have ONE definition, emitted by the front end just before
  // the statement that consumes them; a short backward window finds it.
  constexpr int kDefWindow = 16;
  const auto    def_of     = [&](std::string_view name) -> Lnast_nid {
    if (!Lnast::is_tmp(name)) {
      return Lnast_nid{};
    }
    int budget = kDefWindow;
    for (auto s = ln->get_sibling_prev(stmt); !s.is_invalid() && budget-- > 0; s = ln->get_sibling_prev(s)) {
      const auto d = ln->get_first_child(s);
      if (!d.is_invalid() && Lnast_ntype::is_ref(ln->get_type(d)) && ln->get_name(d) == name) {
        return s;
      }
    }
    return Lnast_nid{};
  };
  // A comptime operand: a literal, or a ref (a generic, a comptime const) that
  // folds under its current frame name.
  const auto comptime_of = [&](const Lnast_nid& n) -> std::optional<int64_t> {
    std::optional<Dlop> v;
    if (Lnast_ntype::is_const(ln->get_type(n))) {
      if (auto c = Dlop::from_pyrope(ln->get_name(n)); c) {
        v = *c;
      }
    } else if (Lnast_ntype::is_ref(ln->get_type(n))) {
      const auto here = lm->save_cursor();
      lm->restore_cursor({n, here.depth});
      const std::string name(lm->current_text());
      lm->restore_cursor(here);
      v = try_fold_ref(name);
    }
    constexpr int64_t kMaxStep = int64_t{1} << 40;  // keeps the offset sums far from int64 overflow
    if (!v || !v->is_integer() || v->has_unknowns() || !v->is_just_i64() || v->to_just_i64() >= kMaxStep
        || v->to_just_i64() <= -kMaxStep) {
      return std::nullopt;
    }
    return v->to_just_i64();
  };

  const auto src = ln->get_sibling_next(ln->get_first_child(stmt));
  const auto lo  = ln->get_sibling_next(src);
  const auto hi  = ln->get_sibling_next(lo);
  if (hi.is_invalid()) {
    return 1;
  }
  if (!Lnast_ntype::is_ref(ln->get_type(lo))) {
    return std::nullopt;
  }
  // The front end lowers each spelling of a bound on its own: in
  // `a#[(b*4)..=((b*4)+3)]` hi reads a second `b*4` temp. Two temps with the
  // same operation over the same operands hold the same value.
  std::function<bool(const Lnast_nid&, const Lnast_nid&, int)> same_value
      = [&](const Lnast_nid& x, const Lnast_nid& y, int depth) -> bool {
    const auto tx = ln->get_type(x);
    if (tx != ln->get_type(y)) {
      return false;
    }
    if (Lnast_ntype::is_const(tx)) {
      return ln->get_name(x) == ln->get_name(y);
    }
    if (!Lnast_ntype::is_ref(tx)) {
      return false;
    }
    if (ln->get_name(x) == ln->get_name(y)) {
      return true;
    }
    const auto dx = depth < 4 ? def_of(ln->get_name(x)) : Lnast_nid{};
    const auto dy = depth < 4 ? def_of(ln->get_name(y)) : Lnast_nid{};
    if (dx.is_invalid() || dy.is_invalid() || ln->get_type(dx) != ln->get_type(dy)
        || !(Lnast_ntype::is_plus(ln->get_type(dx)) || Lnast_ntype::is_minus(ln->get_type(dx))
             || Lnast_ntype::is_mult(ln->get_type(dx)) || Lnast_ntype::is_shl(ln->get_type(dx)))) {
      return false;
    }
    auto a = ln->get_sibling_next(ln->get_first_child(dx));
    auto b = ln->get_sibling_next(ln->get_first_child(dy));
    for (; !a.is_invalid() && !b.is_invalid(); a = ln->get_sibling_next(a), b = ln->get_sibling_next(b)) {
      if (!same_value(a, b, depth + 1)) {
        return false;
      }
    }
    return a.is_invalid() && b.is_invalid();
  };

  // hi == lo + offset, through plus/minus by comptime amounts.
  std::function<std::optional<int64_t>(const Lnast_nid&, int)> offset
      = [&](const Lnast_nid& n, int depth) -> std::optional<int64_t> {
    if (!Lnast_ntype::is_ref(ln->get_type(n))) {
      return std::nullopt;
    }
    if (same_value(n, lo, 0)) {
      return 0;
    }
    const auto def = depth < 4 ? def_of(ln->get_name(n)) : Lnast_nid{};
    if (def.is_invalid()) {
      return std::nullopt;
    }
    const auto t = ln->get_type(def);
    const auto a = ln->get_sibling_next(ln->get_first_child(def));
    const auto b = a.is_invalid() ? a : ln->get_sibling_next(a);
    if (b.is_invalid() || !ln->get_sibling_next(b).is_invalid()) {
      return std::nullopt;
    }
    if (Lnast_ntype::is_plus(t)) {
      if (const auto c = comptime_of(b); c) {
        if (const auto o = offset(a, depth + 1); o) {
          return *o + *c;
        }
      }
      if (const auto c = comptime_of(a); c) {
        if (const auto o = offset(b, depth + 1); o) {
          return *o + *c;
        }
      }
    } else if (Lnast_ntype::is_minus(t)) {
      if (const auto c = comptime_of(b); c) {
        if (const auto o = offset(a, depth + 1); o) {
          return *o - *c;
        }
      }
    }
    return std::nullopt;
  };
  const auto span = offset(hi, 0);
  if (!span || *span < 0) {
    return std::nullopt;
  }
  return *span;
}

void uPass_runner::emit_inline_get_mask(const std::string& dst, const Lnast_node& value, int lo, int hi) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-getmask");
  auto s    = std::make_shared<Lnast>(body, "inl-getmask");
  auto root = s->set_root(Lnast_ntype::create_get_mask());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(dst));  // dst
  s->add_child(root, value);                        // value
  s->add_child(root, Lnast_node::create_const(lo));
  s->add_child(root, Lnast_node::create_const(hi));
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
  emit_leaf(Lnast_node::create_ref(cond));  // cond
  emit_push(Lnast_ntype::create_stmts());   // then arm
  emit_push(Lnast_ntype::create_store());   // dst = value
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

std::string uPass_runner::materialize_array_literal(const std::vector<int64_t>& dims, size_t level, const std::vector<Dlop>& flat,
                                                    size_t start) {
  // Outer dim first (dims[0]); inner tuples are emitted BEFORE the outer so
  // they are recorded (tuple_recs_) and staged ahead of the reference to them.
  const std::string name = "%marrayinit_" + std::to_string(inline_seq_) + "_" + std::to_string(level) + "_" + std::to_string(start);
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
  const auto type_ntype    = lm->get_raw_ntype();
  const bool type_is_array = Lnast_ntype::is_comp_type_array(type_ntype);
  const bool type_is_none  = Lnast_ntype::is_prim_type_none(type_ntype);
  const auto type_nid      = lm->get_current_nid();
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
  // `= nil`, including a comptime `if` whose TAKEN arm is `nil`: prp2lnast hoists
  // the arms into a `%<var>_0` temp, so the nil arrives here as a REF rather than
  // as the `const 'nil'` a literal `= nil` gives. Nil means NO initializer -- no
  // power-on contents and no reset -- so it must NOT be
  // materialized: Dlop::to_pyrope() has no Type::Nil case and falls through to
  // the integer branch returning "0", which upass_tolg's scalar-broadcast branch
  // then reads as a real reset value. That silently zero-initializes the memory
  // AND mints a reset write port, so `if ZERO {0} else {nil}` with ZERO=false
  // stopped matching an explicit `= nil`.
  //
  // Gated on type_is_array so the INFERRED form (`reg t = iv`, prim_type_none)
  // keeps today's behavior; an all-nil typeless bundle is a scalar reg and
  // already declines below. A MIXED bundle (`(1,nil,3)`) also keeps today's
  // behavior: a don't-care entry in an otherwise-initialized memory.
  const bool init_is_nil
      = type_is_array && std::all_of(km.begin(), km.end(), [](const auto& kv) { return kv.second.trivial.is_nil(); });
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

  std::string outer;
  if (!init_is_nil) {
    ++inline_seq_;  // fresh namespace for this declare's materialized temps
    outer = materialize_array_literal(dims, 0, flat, 0);
  }

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
  // `const 'nil'` is the exact text upass_tolg keys on to mean "no initializer".
  s->add_child(root, init_is_nil ? Lnast_node::create_const("nil") : Lnast_node::create_ref(outer));
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  process_lnast();  // cursor at declare root → declare case (guard skips re-entry)
  flush_deferred_emits();
  lm->pop_source();
  return true;
}

bool uPass_runner::try_lower_wrap_sat() {
  using N          = Lnast_ntype;
  // Cursor is on the func_call. Walk children read-only; restore on every exit.
  const auto saved = lm->save_cursor();
  auto       bail  = [&]() {
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
  const bool is_sat  = callee == "sat";
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
  // A destination that holds no bits has nothing to narrow into: a bit range
  // whose width only folds here (`wrap d#[1..+k]`, `k` a generic or a comptime
  // const computed by a call, that folds to 0 -- prp2lnast rejects the forms
  // it can fold itself), or an integer type of width 0. Checked for comptime
  // values too, which otherwise fold silently below.
  if (const auto target = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), type_src);
      target && target->range_max && target->range_min && target->range_max->is_known_zero()
      && target->range_min->is_known_zero()) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "overflow-policy-zero-width",
        .category = "type",
        .pass     = "upass.runner",
        .message  = std::format("`{}` into a destination that holds no bits (its width folds to 0)", is_wrap ? "wrap" : "sat"),
        .span     = lm->current_span(),
        .hint     = "a bit range must select at least one bit (`x#[lo..+n]` with n >= 1), and an integer type needs bits >= 1",
    });
    emit_inline_binding(dst, Lnast_node::create_const("0"));
    return true;
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
  const auto ub        = facts->bits;
  const bool is_signed = facts->kind == upass::decl_facts::Num::signed_int || (facts->range_min && facts->range_min->is_negative());
  const Dlop tmax      = facts->range_max ? *facts->range_max : upass::max_from_bits(ub, is_signed);
  const Dlop tmin      = facts->range_min ? *facts->range_min : upass::min_from_bits(ub, is_signed);

  // Value range: the bitwidth-stamped binding (tightest), falling back to the
  // value's DECLARED envelope (e.g. a module input never gets a per-write
  // bw_*). Both are sound over-approximations — needed both to gate the clamps
  // and to prove a no-op for the unnecessary-wrap/sat warning.
  const auto          vr      = value_range_of(Lnast_node::create_ref(value_name));
  std::optional<Dlop> vmax    = vr.max;
  std::optional<Dlop> vmin    = vr.min;
  // Need the upper clamp/mask unless the value provably stays ≤ max; the lower
  // unless it provably stays ≥ min. (For an unsigned target min==0, so this is
  // the "value may be negative → clamp to 0" case of saturate_unsigned.)
  const bool          need_hi = !(vmax && !vmax->gt_op(tmax)->is_known_true());
  const bool          need_lo = !(vmin && !vmin->lt_op(tmin)->is_known_true());

  // Preserve the per-pass func_call side effects (bitwidth's wrap_sat_exempt_
  // handshake in particular), mirroring process_drop_candidate's step 1, so
  // the trailing store(lhs,dst) keeps skipping the does-not-fit check. The
  // func_call itself is NOT emitted (we return true).
  dispatch_to_passes(&upass::uPass::process_func_call);

  if (!need_hi && !need_lo) {
    // The value provably fits the target type — the wrap/sat narrows nothing.
    // Warn (the keyword is dead) and emit a plain alias. Only for the
    // definition itself: inside an inlined comb the range is one call site's,
    // and the body's `wrap` is still needed for the others (and its own module).
    if (!lm->in_inline_frame()) {
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
    }
    emit_inline_binding(dst, *value);  // alias
    return true;
  }

  if (is_wrap) {
    // C/C++ truncation: keep the low N bits; sign-reinterpret per the type.
    if (is_signed) {
      const std::string masked = dst + "_wm";
      emit_inline_get_mask(masked, *value, 0, static_cast<int>(ub));
      emit_inline_sext(dst, masked, static_cast<int>(ub) - 1);
    } else {
      emit_inline_get_mask(dst, *value, 0, static_cast<int>(ub));
    }
    return true;
  }

  // sat: seed then bw-gated clamps, built straight into staging (the `if` can't
  // go through process_lnast — see emit_staging_op). The clamp result lies in
  // [min,max], so its low N bits are the value: a signed target re-signs them
  // through a final sext, an unsigned one keeps them through a final get_mask.
  // Neither changes a clamped value, but both give the result the target's
  // range without a guard-aware analysis -- a re-read of the lowered form (the
  // Pyrope writer's `if v > 0xff { 0xff } else { v }`) must still fit.
  const std::string clamp = dst + "_sc";
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
    emit_staging_op(N::create_sext(), dst, {Lnast_node::create_ref(clamp), Lnast_node::create_const(static_cast<int64_t>(ub) - 1)});
  } else {
    emit_staging_op(
        N::create_get_mask(),
        dst,
        {Lnast_node::create_ref(clamp), Lnast_node::create_const("0"), Lnast_node::create_const(static_cast<int64_t>(ub))});
  }
  return true;
}

bool uPass_runner::try_lower_tuple_spread() {
  if (std::none_of(upasses.begin(), upasses.end(), [](const auto& p) { return p.name == "constprop"; })) {
    return false;
  }
  const auto saved = lm->save_cursor();
  auto       bail  = [&]() {
    lm->restore_cursor(saved);
    return false;
  };
  if (!lm->move_to_child()) {
    return bail();
  }
  const std::string dst(lm->current_text());
  if (!lm->move_to_sibling() || lm->current_raw_text() != "__fkind__tuple_spread" || !lm->move_to_sibling()) {
    return bail();
  }
  const bool        literal = Lnast_ntype::is_const(lm->get_raw_ntype());
  const std::string name(lm->current_text());
  const auto        value  = literal ? std::optional<Dlop>(*Dlop::from_pyrope(name)) : symbol_table_.comptime_scalar(name);
  const auto        source = literal ? Lnast_node::create_const(name) : Lnast_node::create_ref(name);
  lm->restore_cursor(saved);
  if (value && value->is_string()) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "string-spread",
        .category = "type",
        .pass     = "upass.runner",
        .message  = "strings are opaque and cannot be spread into tuple entries",
        .span     = lm->current_span(),
        .hint     = "use string concatenation to join strings, or explicitly construct a tuple",
    });
  } else {
    emit_inline_binding(dst, source);
  }
  return true;
}

bool uPass_runner::try_lower_typecast() {
  // Front-end-only walks do not establish values or runtime ranges. Leave
  // casts intact there instead of diagnosing every unresolved string cast.
  if (std::none_of(upasses.begin(), upasses.end(), [](const auto& pass) { return pass.name == "constprop"; })) {
    return false;
  }
  using N          = Lnast_ntype;
  // Cursor on the func_call. Walk children read-only; restore on every decline.
  const auto saved = lm->save_cursor();
  auto       bail  = [&]() {
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
    // `u8`): reclassify against the bound concrete token (todo 3g A). Keyed by
    // the frame-tagged spelling (see generic_cast_binds_).
    if (auto gb = generic_cast_binds_.find(lm->current_text()); gb != generic_cast_binds_.end()) {
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

  if (callee == "Signed" || callee == "Unsigned") {
    const auto value
        = arg_name.empty() ? std::optional<Dlop>(*Dlop::from_pyrope(arg_node.get_name())) : symbol_table_.comptime_scalar(arg_name);
    if (value && value->is_string()) {
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "string-reinterpret",
          .category = "type",
          .pass     = "upass.runner",
          .message  = "strings are opaque and cannot be reinterpreted as integers",
          .span     = lm->current_span(),
          .hint     = "signed/unsigned require a numeric bit vector; strings have no character-bit layout",
      });
      return true;
    }
  }

  // A comptime operand folds in constprop (and the drop path retires the call);
  // only a runtime value needs hardware here.
  if (arg_name.empty()) {
    return false;  // const operand → comptime
  }
  // docs 07-typesystem "Clock and Reset": a Clock is not data, so `U1(clk)` /
  // `Bool(clk)` is a compile error (only its debug `String(...)` text form is
  // left to the string path).
  if (tc->kind != upass::Typecast_kind::to_string && !lm->get_lnast()->is_verilog_origin()) {
    if (const auto* pe = lm->get_lnast()->io_meta().find(arg_name); pe != nullptr && pe->is_clock()) {
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "clock-as-data",
          .category = "type",
          .pass     = "upass.runner",
          .message  = std::format("`{}({})`: `{}` is a `Clock`, and a Clock is not data", callee, arg_name, arg_name),
          .span     = lm->current_span(),
          .hint     = "a Clock only drives register clock pins (`clock_pin=clk`) or a child's `Clock` input; use an "
                      "enable for clock-dependent logic",
      });
      return true;
    }
  }
  if (symbol_table_.known_const_scalar(arg_name).has_value()) {
    return false;  // folds to a known scalar → comptime
  }
  // `string(...)` is comptime-ONLY: the switch below has no hardware lowering
  // for it, just the runtime-string-cast error. So an x-carrying comptime value
  // must not reach it — `0sb?` IS a bound Dlop (a compile-time value), and
  // known_const_scalar refuses it only because INLINING unknown bits into
  // hardware is LEC-breaking. Rendering it as text is not hardware, so hand the
  // cast to constprop, which spells the unknown bits `?`.
  if (tc->kind == upass::Typecast_kind::to_string && symbol_table_.comptime_scalar(arg_name).has_value()) {
    return false;
  }

  // Operand kind + range: the bitwidth-stamped binding (tightest), falling back
  // to the declared envelope (a module input never gets a per-write bw_*).
  // Operand kind + range. bool-ness lives on the BUNDLE-level value kind
  // (typecheck's set_value_kind — a runtime `a<b` stamps value_kind=boolean
  // there, NOT on entry "0", whose range reads as a signed int(-1,0)); mirror
  // uPass_typecheck::kind_of_bundle. Enums carry the `enumentry` attr.
  bool                operand_is_bool       = false;
  bool                operand_is_enum       = false;
  bool                operand_decl_unsigned = false;  // declared `:uN` => provably >= 0
  upass::Kind         operand_kind          = upass::Kind::unknown;
  std::optional<Dlop> vmax;
  std::optional<Dlop> vmin;
  if (auto b = symbol_table_.get_bundle(arg_name); b) {
    const auto  vk  = b->get_value_kind();
    const auto& e   = b->get_entry(bundle_path::of_string("0"));
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
    // `unsigned(child.flag)` reaches here with arg_name = the tuple_get TEMP, which
    // has no declared type of its own. Follow tget_origin back to the source field
    // ("child.flag") the way concat_lane_declared_bits already does, so a declared
    // port of a Sub-INSTANCE handle is visible: the instance's own field facts come
    // from stash_sub_instance_port_facts at the decline. Without the hop a `bool`
    // output port of an instantiated `mod` had no known width and the cast failed
    // with `cast-not-typed` even though the callee declares it.
    if (auto vf = operand_decl_facts(arg_name)) {
      if (vf->kind == upass::decl_facts::Num::boolean) {
        operand_is_bool = true;
        operand_kind    = upass::Kind::boolean;
      } else if (operand_kind == upass::Kind::unknown
                 && (vf->kind == upass::decl_facts::Num::unsigned_int || vf->kind == upass::decl_facts::Num::signed_int)) {
        operand_kind = upass::Kind::integer;
      }
      // A declared type bounds the value unless a write into it could not be
      // judged (Symbol_table::unchecked_typed).
      const bool bounded_by_type = !symbol_table_.unchecked_typed.contains(arg_name.substr(0, arg_name.find("___ssa_")));
      if (vf->kind == upass::decl_facts::Num::unsigned_int && bounded_by_type) {
        operand_decl_unsigned = true;
      }
      if (!vmax && vf->range_max && vf->range_max->is_integer() && bounded_by_type) {
        vmax = vf->range_max;
      }
      if (!vmin && vf->range_min && vf->range_min->is_integer() && bounded_by_type) {
        vmin = vf->range_min;
      }
      reinterpret_W = vf->bits;
    }
  }
  // The reinterpret width is the operand's DECLARED (structural) width: a typed
  // name, or a temp whose width its producer fixed (a bit-select `x#[0..=31]`,
  // an inner cast). An arithmetic result (`a / 17 * 17`) has only the range
  // the bitwidth analysis derived, and that depends on how tight the analysis
  // is: identical math could reinterpret at different widths (8 bits give
  // -1 for 255, 9 give 255), so it never decides a sign (see the arms below).

  // Only a genuine runtime hardware scalar (int/bool) is lowered here. Enums
  // (`string(E.x)`/`int(E.x)`), comptime strings, tuples, ranges, nil — all
  // constprop's domain — are declined so its richer fold runs (this hook fires
  // BEFORE constprop on the func_call, so over-eager handling would corrupt
  // them). `unknown` is kept: a runtime arithmetic result (`int(a+b)`) is often
  // unstamped but is a real integer.
  if (operand_is_enum || (!operand_is_bool && operand_kind != upass::Kind::integer && operand_kind != upass::Kind::unknown)) {
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

  switch (tc->kind) {
    case upass::Typecast_kind::to_string:
      // A runtime value has no string form (strings are comptime-only).
      cast_error("runtime-string-cast",
                 "`String(...)` needs a compile-time value — a runtime signal has no string form",
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
                     "give the operand a sized type (`:U<N>`/`:S<N>`, e.g. a typed local or a bit-select `x#[0..<N]`) "
                     "before reinterpreting its sign");
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
        emit_inline_get_mask(dst, arg_node, 0, 1);  // unsigned bit: true = 1, false = 0
        emit_inline_typespec(dst, 1, false);
        return true;
      }
      // A provably NON-NEGATIVE operand needs NO width, because `unsigned(x)`
      // only RE-TAGS the sign and x is already unsigned -- the cast is the
      // IDENTITY. The width matters solely for a value that can be NEGATIVE,
      // whose bit pattern depends on it (-1 is 0b111 at u3 but 0b11111 at u5);
      // that, and only that, is what cannot be reinterpreted without a width.
      // Demanding a width unconditionally rejected the very common (and
      // redundant) `unsigned(<already unsigned>)` that front ends emit, e.g.
      // `unsigned(arr[0]#[0..=4])` over a declared `[2]u5`.
      if ((vmin && !vmin->is_negative()) || operand_decl_unsigned) {
        emit_inline_binding(dst, arg_node);
        return true;
      }
      if (reinterpret_W == 0) {
        cast_error("cast-not-typed",
                   std::format("`{}(...)` reinterpret needs a fully-typed input (a known bit width)", callee),
                   "give the operand a sized type (`:U<N>`/`:S<N>`) before reinterpreting its sign, "
                   "or drop the cast if it is already unsigned");
        return true;
      }
      // Mask to the low W bits: same bits, now an unsigned value/range.
      emit_inline_get_mask(dst, arg_node, 0, static_cast<int>(reinterpret_W));
      emit_inline_typespec(dst, static_cast<int>(reinterpret_W), false);
      return true;

    case upass::Typecast_kind::to_sized: {
      const auto ub  = static_cast<uint32_t>(tc->sized_bits);
      const bool sgn = tc->sized_signed;
      if (operand_is_bool) {
        if (sgn) {
          emit_inline_sext(dst, arg_name, 0);  // true = -1
        } else {
          emit_inline_get_mask(dst, arg_node, 0, 1);  // true = 1
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
                   "a sized cast (`U<N>`/`S<N>`) is checked, not truncating; use a `wrap` or `sat` prefix to drop bits");
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
  using N          = Lnast_ntype;
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
  if (a_type.k == In_type::K::tuple) {
    in_op_fail(span,
               "in-lhs-tuple",
               "the left operand of `in` must be a scalar, not a tuple",
               "test each scalar separately; `in` is membership, not a tuple subset test");
  }

  // A bare const rhs (`a in 5`) is a one-element membership: `dst = (a == 5)`.
  if (b_is_const) {
    if (!in_types_compatible(a_type, classify_in_const(b_text))) {
      in_op_fail(span,
                 "in-type-mismatch",
                 std::format("`in` compares values of different types: the left operand is {}, but the right is {}",
                             in_kind_name(a_type.k),
                             in_kind_name(classify_in_const(b_text).k)),
                 "the right operand of `in` must have the same type as the left operand");
    }
    emit_inline_op(N::create_eq(), dst, {a_node, b_node});
    return true;
  }

  // RHS shape must be known at compile time; names only select its values.
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
    const std::string key    = tl.pos >= 0 ? std::to_string(tl.pos) : std::string(tl.name);
    const In_type     e_type = classify_in_bundle(b_bundle->get_bundle(bundle_path::of_string(key)));
    if (!in_types_compatible(a_type, e_type)) {
      in_op_fail(span,
                 "in-type-mismatch",
                 std::format("`in` compares values of different types: the left operand is {}, but element {} is {}",
                             in_kind_name(a_type.k),
                             idx,
                             in_kind_name(e_type.k)),
                 "every element on the right of `in` must have the same type as the left operand (integer "
                 "width/signedness may differ; bool, integer, enum and tuple may not be mixed)");
    }

    const std::string pick     = std::format("{}_in{}", dst, idx);
    const std::string selector = tl.pos >= 0 ? key : std::string(Dlop::from_string(key)->to_pyrope());
    emit_inline_tuple_pick(pick, b_name, selector);  // b[key] → runtime copy or const fold
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
  auto s    = std::make_shared<Lnast>(body, std::string(root_lnast_->get_top_module_name()));
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

bool uPass_runner::bind_call_actuals(const Lnast_tree_io& io, const std::vector<Actual>& actuals, bool commit,
                                     std::string_view callee_name, const livehd::diag::Span& call_span,
                                     std::vector<Lnast_node>& param_val, std::vector<bool>& param_set,
                                     std::vector<std::string>& param_func, std::vector<Lnast_node>& vararg_pos,
                                     std::vector<std::pair<std::string, Lnast_node>>& vararg_named, bool* out_tuple_expanded) {
  const std::size_t nparams    = io.inputs.size();
  // A trailing `...args` var-arg param (always the LAST input) gathers every
  // actual not consumed by a fixed leading param into one synthesized tuple:
  // only named leftovers become fields (`args.NAME`). Positional values must
  // bind an ordinary parameter under the normal naming exceptions.
  const bool        has_vararg = nparams > 0 && io.inputs[nparams - 1].is_varargs;
  const std::size_t nbind      = has_vararg ? nparams - 1 : nparams;  // bindable fixed params (vararg excluded)

  param_val.assign(nparams, Lnast_node::create_invalid());
  param_set.assign(nparams, false);
  param_func.assign(nparams, std::string{});
  vararg_pos.clear();
  vararg_named.clear();
  if (out_tuple_expanded != nullptr) {
    *out_tuple_expanded = false;
  }

  // Slang represents a Verilog escaped scalar port such as `\\req.a ` as the
  // quoted LNAST identifier `` `req.a` `` so bundle processing does not split
  // it at the dot.  gather_actuals canonicalizes the matching named actual to
  // the bare external port name; compare the formal through the same view.
  // Keep quotes on names containing whitespace because those identifiers
  // genuinely need their quoted spelling downstream (the same rule tolg uses
  // in canon_io_name).
  const auto canonical_port_name = [](std::string_view name) {
    if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
      const auto inner  = name.substr(1, name.size() - 2);
      const bool has_ws = std::any_of(inner.begin(), inner.end(), [](unsigned char c) { return std::isspace(c) != 0; });
      if (!has_ws) {
        return inner;
      }
    }
    return name;
  };
  auto param_index = [&](std::string_view k) -> std::size_t {
    const auto canonical_key = canonical_port_name(k);
    for (std::size_t i = 0; i < nbind; ++i) {  // never match the var-arg slot by name
      if (canonical_port_name(io.inputs[i].name) == canonical_key) {
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
      // Declared input bundles already have scalar ports in io_meta; they
      // need not have a runtime tuple constructor/slot map. Forward their
      // known leaves as one named argument, with the same all-or-nothing
      // validation as a constructed tuple below.
      std::vector<std::pair<std::size_t, std::string>> inputs;
      const std::string                                input_prefix = std::string(tup) + ".";
      for (const auto& input : lm->get_lnast()->io_meta().inputs) {
        if (!input.name.starts_with(input_prefix)) {
          continue;
        }
        const auto idx = param_index(std::string(prefix) + "." + input.name.substr(input_prefix.size()));
        if (idx >= nbind || param_set[idx]) {
          return false;
        }
        inputs.emplace_back(idx, input.name);
      }
      if (inputs.empty()) {
        return false;
      }
      for (const auto& [idx, name] : inputs) {
        param_val[idx] = Lnast_node::create_ref(name);
        param_set[idx] = true;
      }
      if (out_tuple_expanded != nullptr) {
        *out_tuple_expanded = true;
      }
      return true;
    }
    const auto& shape   = *shape_opt;
    // A genuine tuple has >1 field or a NAMED (non-positional) field; a 1-entry
    // positional bundle is a scalar carrier, not a tuple worth expanding.
    bool        genuine = shape.size() > 1;
    for (const auto& [fld, is_pos] : shape) {
      if (!is_pos) {
        genuine = true;
        break;
      }
    }
    if (!genuine) {
      return false;
    }
    const auto                                              comptime = try_bundle_fields(tup);
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
    if (out_tuple_expanded != nullptr) {
      *out_tuple_expanded = true;  // a Sub-bound callee must re-emit dotted NAMED actuals
    }
    return true;
  };
  // The var-arg parameter `...x` is ONE parameter holding a tuple and, like every
  // argument, is bound by NAME (qa.md "Var-args": `add1(x=(1, 2, 3))`). Expand the
  // named actual's fields into the gathered entries: unnamed fields take the
  // canonical positional keys (`x[0]`, `x[1]`...), named fields keep their names
  // (`x.NAME`). A scalar actual is a one-element var-arg. False when a field of a
  // tuple actual cannot be resolved to a value.
  auto expand_vararg_actual = [&](const Actual& a) -> bool {
    if (!a.node.is_ref()) {
      vararg_pos.push_back(a.node);
      return true;
    }
    const auto tup       = std::string(a.node.get_name());
    const auto shape_opt = try_tuple_shape(tup);
    if (!shape_opt || shape_opt->empty()) {
      vararg_pos.push_back(a.node);  // a runtime scalar variable
      return true;
    }
    const auto                                      comptime = try_bundle_fields(tup);
    std::vector<std::pair<std::string, Lnast_node>> fields;  // (key, value), declaration order
    for (const auto& [fld, is_pos] : *shape_opt) {
      bool resolved = false;
      if (comptime) {
        for (const auto& [k, v] : *comptime) {
          if (k == fld && !v.is_invalid()) {
            fields.emplace_back(is_pos ? std::string{} : fld, Lnast_node::create_const(v.to_pyrope()));
            resolved = true;
            break;
          }
        }
      }
      if (!resolved) {
        if (auto rn = try_tuple_slot_ref(tup, fld)) {
          fields.emplace_back(is_pos ? std::string{} : fld, Lnast_node::create_ref(*rn));
          resolved = true;
        }
      }
      if (!resolved) {
        return false;
      }
    }
    for (auto& [k, v] : fields) {
      if (k.empty()) {
        vararg_pos.push_back(std::move(v));
      } else {
        vararg_named.emplace_back(k, std::move(v));
      }
    }
    return true;
  };
  const bool        has_self       = nbind > 0 && io.inputs[0].name == "self";
  const std::size_t first_param    = has_self ? 1 : 0;     // first non-self fixed param
  const std::size_t n_named_params = nbind - first_param;  // fixed, non-self (var-arg excluded)
  auto              actual_kind    = [&](const Lnast_node& node) { return actual_node_kind(node); };

  // Exception 2: a positional bare variable whose name matches a parameter
  // binds to it. Match the SOURCE spelling (Actual::src_name), exact name
  // first, then the variable a compiler-minted SSA version belongs to. Returns
  // the unset, non-self fixed param it spells, or nbind.
  auto pun_index = [&](const Actual& a) -> std::size_t {
    for (const std::string* nm : {&a.src_name, &a.src_base}) {
      if (nm->empty()) {
        continue;
      }
      if (const auto idx = param_index(*nm); idx >= first_param && idx < nbind && !param_set[idx]) {
        return idx;
      }
    }
    return nbind;
  };
  // The same for a flattened tuple-param GROUP (`p` for the leaves `p.x`,
  // `p.y`) with an unset leaf, or "".
  auto pun_group = [&](const Actual& a) -> std::string {
    for (const std::string* nm : {&a.src_name, &a.src_base}) {
      if (nm->empty()) {
        continue;
      }
      const std::string prefix = *nm + ".";
      for (std::size_t i = first_param; i < nbind; ++i) {
        if (!param_set[i] && canonical_port_name(io.inputs[i].name).starts_with(prefix)) {
          return *nm;
        }
      }
    }
    return {};
  };
  bool vararg_bound = false;  // the var-arg param was bound by its own name
  for (std::size_t ai = 0; ai < actuals.size(); ++ai) {
    const auto& a = actuals[ai];
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
      if (has_vararg && canonical_port_name(a.key) == canonical_port_name(io.inputs[nbind].name)) {
        if (vararg_bound) {
          if (!commit) {
            return false;  // duplicate
          }
          fcall_arg_fail(call_span,
                         "fcall-duplicate-arg",
                         std::format("duplicate argument `{}` in call to `{}`", a.key, callee_name),
                         "each parameter may be bound only once");
        }
        vararg_bound = true;
        if (!expand_vararg_actual(a)) {
          if (!commit) {
            return false;
          }
          fcall_arg_fail(call_span,
                         "fcall-vararg-unresolved",
                         std::format("var-arg `{}` of `{}` is bound to a tuple with a field that has no value", a.key, callee_name),
                         "every field of the var-arg tuple must be a known value");
        }
        continue;
      }
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
        if (!expanded && bind_minted_ok_ && (a.key == "clock" || a.key == "reset")) {
          // The child's MINTED clock/reset (it declares none): bindable by name.
          const auto want = a.key == "clock" ? Io_sig::clock : Io_sig::reset;
          if (std::none_of(io.inputs.begin(), io.inputs.end(), [&](const Lnast_io_entry& e) { return e.sig == want; })) {
            bind_minted_actuals_.emplace_back(a.key, a.node);
            continue;
          }
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
      // A pass-by-ref actual (`f(ref x)`) follows the ordinary naming rules
      // (qa.md "Positional binding"); only the UFCS receiver bound to `self`
      // is exempt.
      if (has_self && !param_set[0]) {
        bind(0);  // self ← UFCS receiver
        continue;
      }
      // Constructor arguments (explicit `T(...)` and `mut x:T = (...)`) bind like
      // any call (qa.md Q31): by name, single parameter, name match or unique
      // type -- never by tuple order.
      // A positional TUPLE actual (more than one field, or a named one) binds a
      // flattened tuple-param GROUP: a tuple-typed param `p:(x,y)` is flattened
      // to the leaves `p.x`,`p.y`, and the actual's fields EXPAND into them.
      std::optional<std::vector<std::pair<std::string, bool>>> shape_opt;
      bool                                                     is_tuple_actual = false;
      if (a.node.is_ref()) {
        shape_opt = try_tuple_shape(a.node.get_name());
        if (shape_opt) {
          is_tuple_actual = shape_opt->size() > 1;
          for (const auto& [fld, is_pos] : *shape_opt) {
            if (!is_pos) {
              is_tuple_actual = true;
              break;
            }
          }
        }
      }
      // Exception 2 first: a bare variable whose name matches an unset, non-self
      // parameter binds to THAT parameter (so `f(b, a)` binds by name), a
      // tuple variable to the tuple-param group it names (`dx(to, from)`). The
      // match is on the SOURCE spelling: inside an inlined body (a recursive
      // call passing the caller's own param) `node` is the frame-renamed
      // `inl<N>_v`. In the overload probe a tuple actual never binds a lone
      // scalar param (the tuple-vs-scalar discrimination).
      if (const auto pidx = pun_index(a); pidx < nbind && (commit || !is_tuple_actual)) {
        bind(pidx);
        continue;
      }
      if (a.node.is_ref()) {
        if (const auto group = pun_group(a); !group.empty() && expand_tuple_actual(a.node.get_name(), group)) {
          continue;
        }
      }
      if (is_tuple_actual) {
        // Otherwise the ONE unset group whose leaves are exactly the actual's
        // fields takes it (the argument's shape makes the mapping unambiguous).
        // Two such groups (`dx(from:Pt, to:Pt)`) are ambiguous: the argument
        // must be named. Groups are walked in declaration order.
        std::vector<std::pair<std::string, std::size_t>> groups;  // (prefix, unset leaf count)
        for (std::size_t i = first_param; i < nbind; ++i) {
          const auto& pn = io.inputs[i].name;
          const auto  dp = pn.rfind('.');
          if (param_set[i] || dp == std::string::npos) {
            continue;
          }
          const auto prefix = pn.substr(0, dp);
          auto       it     = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == prefix; });
          if (it == groups.end()) {
            groups.emplace_back(prefix, 1);
          } else {
            ++it->second;
          }
        }
        const std::string* match  = nullptr;
        std::size_t        nmatch = 0;
        const auto&        fields = *shape_opt;
        for (const auto& [prefix, cnt] : groups) {
          const bool fits = cnt == fields.size() && std::all_of(fields.begin(), fields.end(), [&](const auto& f) {
                              const auto lidx = param_index(prefix + "." + f.first);
                              return lidx < nbind && !param_set[lidx];
                            });
          if (fits) {
            match = &prefix;
            ++nmatch;
          }
        }
        if (nmatch == 1 && expand_tuple_actual(a.node.get_name(), *match)) {
          continue;
        }
        // A tuple actual that matched no group must NOT bind a lone scalar
        // param in the overload probe; the real bind falls through to
        // exception 1 (whole tuple into the only param) or the naming error.
        if (!commit
            && !(n_named_params == 1 && (io.inputs[first_param].kind == Io_kind::none || io.inputs[first_param].array_size > 0))) {
          return false;
        }
      }
      // Exception 1: exactly one non-self parameter — a single positional value
      // maps unambiguously (also the whole-tuple→scalar path under commit).
      if (n_named_params == 1) {
        const auto slot = next_unset(has_self ? 1 : 0);
        if (slot < nbind) {
          bind(slot);
          continue;
        }
      }
      // Exception 3: the actual's kind uniquely identifies one typed, non-self
      // parameter of the WHOLE signature (06-functions.md: "if two parameters
      // have the same type ... the call must name the argument"). Counting only
      // the still-unset params made the last same-kind slot a positional
      // fallback: `f1(a, b)` into `f1(a:u4, c:u4)` bound b→c once `a` punned,
      // which also let an overload set dispatch to the wrong lambda. Untyped
      // params (kind=none) never match.
      if (const auto k = actual_kind(a.node); k != Io_kind::none) {
        std::size_t match   = nparams;
        std::size_t count   = 0;
        bool        untyped = false;  // an untyped param could accept the value too: not unique
        for (std::size_t i = (has_self ? 1u : 0u); i < nbind; ++i) {
          const auto& e = io.inputs[i];
          if (e.kind == k) {
            match = i;
            ++count;
          } else if (e.kind == Io_kind::none && e.type_name.empty() && e.bits == 0 && !e.has_range && e.array_size == 0
                     && !e.wide_range_min && !e.has_deferred_bound()) {
            untyped = true;
          }
        }
        if (count == 1 && !untyped && !param_set[match]) {
          bind(match);
          continue;
        }
      }
      // A `ref` actual (`f(ref m, by=2)`) is identified by its MODE the way
      // Exception 3 identifies a value by its type: it may only bind a `ref`
      // parameter, so exactly one `ref` parameter in the whole signature makes
      // the mapping unambiguous. Two `ref` parameters (`bump(ref x, ref y)`)
      // must be named (`bump(p=ref x, q=ref y)`).
      if (a.is_ref_pass) {
        std::size_t match = nparams;
        std::size_t count = 0;
        for (std::size_t i = first_param; i < nbind; ++i) {
          if (io.inputs[i].is_ref) {
            match = i;
            ++count;
          }
        }
        if (count == 1 && !param_set[match]) {
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
      // A flattened tuple-param leaf (`p.x`) is named as its parameter (`p=`).
      if (const auto dp = pname.find('.'); dp != std::string::npos && !pname.starts_with('`')) {
        pname.resize(dp);
      }
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

namespace {
// The declared comp_type_array of port `name` when its shape does not fold in
// `tmpl` (`v:[N]u4`, `v:[N]unsigned(bits=N)` on a generic lambda): upass.ssa
// keeps that type in the io tree (the io entry has no width), and the
// specialization folds it under the binds (clone_template_specialized). The
// declaration types the port, never the actual. Invalid otherwise.
Lnast_nid unsized_array_port(const Lnast& tmpl, const Lnast_io_entry& e, bool output) {
  if (e.bits > 0 || e.array_size > 0 || e.has_range || e.kind != Io_kind::none) {
    return {};  // a sized port (a `[4]u8` array is too)
  }
  const auto name = std::string_view(e.name);
  const auto io   = tmpl.get_first_child(tmpl.get_root());
  if (io.is_invalid() || !Lnast_ntype::is_io(tmpl.get_type(io))) {
    return {};
  }
  auto tup = tmpl.get_first_child(io);
  if (output && !tup.is_invalid()) {
    tup = tmpl.get_sibling_next(tup);
  }
  if (tup.is_invalid()) {
    return {};
  }
  for (auto st : tmpl.children(tup)) {
    const auto nm = tmpl.get_first_child(st);
    if (nm.is_invalid() || tmpl.get_name(nm) != name) {
      continue;
    }
    for (auto c : tmpl.children(st)) {
      if (Lnast_ntype::is_comp_type_array(tmpl.get_type(c))) {
        return c;
      }
    }
    return {};
  }
  return {};
}

// An ARRAY port: a sized one (`v:[4]u8`) or an unsized one (see above).
bool is_array_port(const Lnast& tmpl, const Lnast_io_entry& e, bool output) {
  return e.array_size > 0 || !unsized_array_port(tmpl, e, output).is_invalid();
}
}  // namespace

// A call that DECLINES the comb splice lowers to a Sub INSTANCE, and the handle's
// FIELDS are the callee's declared output ports. tolg re-derives them when it
// builds the Sub, but the runner's own declared-type consumers run FIRST -- so
// without this `unsigned(child.flag)` saw no width for a `bool` output port and
// failed with `cast-not-typed`, even though the callee declares `flag:bool`.
//
// Stashed in Symbol_table::pending_decl_facts, which is exactly the "declared
// facts for a dotted path whose field entry does not exist yet" side slot that
// upass::decl_facts::lookup already consults -- so every existing consumer picks
// them up with no new plumbing.
void uPass_runner::stash_sub_instance_port_facts(std::string_view handle, const std::shared_ptr<Lnast>& callee,
                                                 const std::vector<Spec_port>* out_inject) {
  if (handle.empty() || !callee) {
    return;
  }
  if (const auto& outs = callee->io_meta().outputs; outs.size() == 1 && !outs.front().name.empty()) {
    symbol_table_.single_output_port.insert_or_assign(std::string(handle), outs.front().name);
    if (Lnast::is_tmp(handle)) {
      const auto cn = callee->get_top_module_name();
      symbol_table_.call_result_label.insert_or_assign(
          std::string(handle),
          absl::StrCat(cn.substr(cn.rfind('.') == std::string_view::npos ? 0 : cn.rfind('.') + 1), "(…).", outs.front().name));
    }
  }
  if (Lnast::is_tmp(handle)) {  // an absent-field read names the call (upass.constprop)
    const auto cn = callee->get_top_module_name();
    symbol_table_.call_result_callee.insert_or_assign(std::string(handle),
                                                      cn.substr(cn.rfind('.') == std::string_view::npos ? 0 : cn.rfind('.') + 1));
  }
  const auto& outs  = callee->io_meta().outputs;
  auto&       names = symbol_table_.sub_output_names[std::string(handle)];
  names.clear();
  for (const auto& oe : outs) {
    // Unquoted, as a field read spells it (a Verilog port rides `` `rsp.data` ``).
    const bool quoted = oe.name.size() >= 2 && oe.name.front() == '`' && oe.name.back() == '`';
    names.push_back(quoted ? oe.name.substr(1, oe.name.size() - 2) : oe.name);
  }
  for (std::size_t oi = 0; oi < outs.size(); ++oi) {
    auto oe = outs[oi];
    if (oe.name.empty()) {
      continue;
    }
    // A generic-width output (`o:unsigned(bits=N)`) of a specialized template:
    // its width is this call's folded bound, not the template's unbounded port.
    // An array output's folded bound is its ELEMENT's, never the port's.
    if (out_inject != nullptr && oi < out_inject->size() && (*out_inject)[oi].inject
        && unsized_array_port(*callee, oe, true).is_invalid()) {
      const auto& sp = (*out_inject)[oi];
      if (sp.kind == Io_kind::boolean) {
        oe.kind = Io_kind::boolean;
      } else if (sp.type_name.empty() && sp.array_size == 0 && sp.max && sp.min && sp.max->is_just_i64() && sp.min->is_just_i64()) {
        oe.kind      = Io_kind::integer;
        oe.has_range = true;
        oe.range_max = sp.max->to_just_i64();
        oe.range_min = sp.min->to_just_i64();
      } else if (sp.type_name.empty() && sp.array_size == 0 && sp.max && sp.min) {
        oe.kind           = Io_kind::integer;
        oe.wide_range_max = *sp.max;
        oe.wide_range_min = *sp.min;
      }
    }
    const auto key = absl::StrCat(handle, ".", oe.name);
    // Ruling 21 names a `mod`/`pipe` instance's untyped output: one still
    // untyped here has no range to promise (a template's specialization, or an
    // output its body left unbounded) -- a fully typed callee's derived range
    // was already exported into its io_meta (ruling 28, pass.upass). A `comb`
    // is inlined unless `compile.upass.inline=false` keeps it a Sub, and the
    // inlined body derives the output's range: marking the Sub's output
    // unknown would make the knob decide whether the program compiles.
    if (callee->get_lambda_kind() != "comb" && (oe.kind == Io_kind::none || oe.kind == Io_kind::integer) && oe.array_size == 0
        && oe.bits == 0 && !oe.has_range && !oe.wide_range_min && !oe.has_deferred_bound() && oe.type_name.empty()
        && unsized_array_port(*callee, oe, true).is_invalid()) {
      symbol_table_.opaque_sub_outputs[handle].insert(oe.name);
    }
    if (oe.kind == Io_kind::integer && oe.array_size == 0) {
      auto& ports = symbol_table_.sub_output_ranges[handle];
      if (oe.has_range) {
        ports.insert_or_assign(oe.name, std::pair{*Dlop::create_integer(oe.range_min), *Dlop::create_integer(oe.range_max)});
      } else if (oe.wide_range_min && oe.wide_range_max) {
        ports.insert_or_assign(oe.name, std::pair{*oe.wide_range_min, *oe.wide_range_max});
      } else if (oe.bits > 0) {
        const auto w = static_cast<uint32_t>(oe.bits);
        ports.insert_or_assign(oe.name, std::pair{upass::min_from_bits(w, oe.is_signed), upass::max_from_bits(w, oe.is_signed)});
      }
    }
    Symbol_table::Pending_decl pd;
    if (oe.kind == Io_kind::boolean) {
      pd.kind     = upass::Kind::boolean;
      pd.decl_max = *Dlop::create_integer(1);
      pd.decl_min = *Dlop::create_integer(0);
    } else if (oe.has_range && oe.range_min >= 0) {
      pd.kind     = upass::Kind::integer;
      pd.decl_max = *Dlop::create_integer(oe.range_max);
      pd.decl_min = *Dlop::create_integer(oe.range_min);
    } else if (oe.bits > 0 && !oe.is_signed) {
      pd.kind     = upass::Kind::integer;
      pd.decl_max = upass::max_from_bits(static_cast<uint32_t>(oe.bits), false);
      pd.decl_min = upass::min_from_bits(static_cast<uint32_t>(oe.bits), false);
    } else {
      // A SIGNED output is deliberately left unclaimed. Claiming its width would
      // make `unsigned(inst.sval)` type-check, but the reinterpret is not
      // materialized on an instance read -- the emitted Verilog assigns the signed
      // wire straight through, so `unsigned(s8(-3))` into a u9 sign-extends to
      // 9'h1FD instead of 9'h0FD. Declining keeps the loud `cast-not-typed` instead
      // of a silent wrong value. For an UNSIGNED or boolean output `unsigned()` is
      // an identity (see the unsigned-cast rule: a width is needed only when the
      // operand can be negative), which is why those are safe to claim.
      continue;
    }
    symbol_table_.pending_decl_facts.insert_or_assign(key, pd);
    symbol_table_.pending_keys_by_root[std::string(handle)].push_back(key);
  }
}

void uPass_runner::note_opaque_output_read() {
  if (symbol_table_.opaque_sub_outputs.empty() || !lm->has_child()) {
    return;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  const std::string dst(lm->current_text());
  lm->restore_cursor(saved);
  // `c.o`: the tuple_get's origin "handle.port", recorded by the dispatch. A
  // call to a ONE-output callee is its output's value itself (`y = m(a=x)`).
  std::string_view handle;
  std::string_view port;
  if (const auto origin = symbol_table_.tget_origin.find(dst); origin != symbol_table_.tget_origin.end()) {
    const auto dot = origin->second.find('.');
    if (dot == std::string::npos) {
      return;
    }
    handle = std::string_view(origin->second).substr(0, dot);
    port   = std::string_view(origin->second).substr(dot + 1);
  } else if (const auto so = symbol_table_.single_output_port.find(dst); so != symbol_table_.single_output_port.end()) {
    handle = dst;
    port   = so->second;
  } else {
    return;
  }
  const auto oo = symbol_table_.opaque_sub_outputs.find(handle);
  if (oo != symbol_table_.opaque_sub_outputs.end() && oo->second.contains(port)) {
    symbol_table_.wide_values.insert(dst);  // unknown range: judged like a too-wide value (ruling 21)
  }
}

std::optional<upass::decl_facts::Facts> uPass_runner::operand_decl_facts(std::string_view name) const {
  return upass::decl_facts::lookup_operand(symbol_table_, lm ? lm->get_lnast().get() : nullptr, name);
}

uPass_runner::Value_range uPass_runner::value_range_of(const Lnast_node& v) const {
  Value_range r;
  if (v.is_const()) {
    // A 0sb/0ub BIT-PATTERN literal carries bits, not a value: binding it is a
    // force/reinterpret, never an overflow (the same rule upass.bitwidth applies).
    const auto txt = v.get_name();
    if (txt.size() >= 3 && txt[0] == '0' && (txt[1] == 's' || txt[1] == 'u') && txt[2] == 'b') {
      return r;
    }
    const auto& c = Dlop::from_pyrope_cached(txt);
    if (c.is_invalid() || !c.is_integer() || c.has_unknowns()) {
      return r;
    }
    r.integer = true;
    r.min     = c;
    r.max     = c;
    return r;
  }
  if (!v.is_ref()) {
    return r;
  }
  // A ref reads the RANGE upass.bitwidth derived, never the folded value, so a
  // pattern-derived constant keeps its force semantics here too.
  const auto name = v.get_name();
  const auto b    = symbol_table_.get_bundle(name);
  if (b) {
    const auto& e = b->get_entry(bundle_path::of_string("0"));
    if (!e.bw_max.is_invalid() && e.bw_max.is_integer() && !e.bw_min.is_invalid() && e.bw_min.is_integer()) {
      r.integer = true;
      r.min     = e.bw_min;
      r.max     = e.bw_max;
      return r;
    }
    // An enum entry (`Dir.W`) is its integer encoding (user ruling 2026-09-28
    // (29)); nothing derives a range for its carrier, and a narrower port must
    // not truncate it silently.
    if (bundle_enum_type(b)) {
      if (const auto ev = b->scalar(); ev && ev->is_integer() && !ev->has_unknowns()) {
        r.integer = true;
        r.min     = *ev;
        r.max     = *ev;
        return r;
      }
    }
  }
  // A range published before this walk (e.g. a rolled loop's index, whose
  // port type is only a signed storage window around its elaborated domain).
  const auto& ranges = lm->get_lnast()->bw_meta().ranges;
  if (auto it = ranges.find(std::string(name)); it != ranges.end() && !it->second.unbounded) {
    r.integer = true;
    r.min     = *Dlop::create_integer(it->second.min);
    r.max     = *Dlop::create_integer(it->second.max);
    return r;
  }
  // A read of a Sub instance's output (`child.o`) is a tuple_get temp with no
  // type of its own: the instance port bounds it (stash_sub_instance_port_facts).
  // (Also a single-output instance read through its handle, `f(a=s1)`.)
  const Symbol_table::Port_range* port = nullptr;
  if (const auto origin = symbol_table_.tget_origin.find(std::string(name)); origin != symbol_table_.tget_origin.end()) {
    port = symbol_table_.sub_output_range(origin->second);
  } else if (const auto h = symbol_table_.sub_output_ranges.find(name);
             h != symbol_table_.sub_output_ranges.end() && h->second.size() == 1) {
    port = &h->second.begin()->second;
  }
  if (port != nullptr) {
    r.integer = true;
    r.min     = port->first;
    r.max     = port->second;
    return r;
  }
  // Its declared type, unless a write into it was one upass.bitwidth could not
  // judge (then the type does not bound what it holds).
  if (const auto vf = operand_decl_facts(name)) {
    if (vf->kind == upass::decl_facts::Num::boolean || vf->kind == upass::decl_facts::Num::string) {
      return r;
    }
    const auto base = name.substr(0, name.find("___ssa_"));
    if (vf->range_max && vf->range_max->is_integer() && vf->range_min && vf->range_min->is_integer()
        && !symbol_table_.unchecked_typed.contains(base)) {
      r.integer = true;
      r.min     = vf->range_min;
      r.max     = vf->range_max;
      return r;
    }
  }
  // A value too WIDE for a derived range (`x << n`, a >62-bit product, as
  // upass.bitwidth classified it). A value nothing derived a range for is left
  // alone.
  r.integer = symbol_table_.wide_values.contains(name);
  return r;
}

namespace {
// Does the result `dst` of the call at `call_nid` reach a `comptime`
// declaration through the straight-line copies after it? `comptime const T =
// f(...)` lowers to the call into a temp, T's declare, then `store(T, %tmp)`.
// Such a call is evaluated at compile time, so its loops cannot roll. The walk
// follows the value to its FIRST consumer only (and never past a compound
// statement): it runs for every inlined call, so it must not rescan the rest
// of the scope.
bool feeds_comptime_decl(const Lnast& ln, const Lnast_nid& call_nid, std::string_view dst) {
  std::string_view                      flow = dst;
  absl::flat_hash_set<std::string_view> comptime_decls;
  for (auto s = ln.get_sibling_next(call_nid); !s.is_invalid(); s = ln.get_sibling_next(s)) {
    const auto t = ln.get_type(s);
    const auto a = ln.get_first_child(s);
    const auto b = a.is_invalid() ? a : ln.get_sibling_next(a);
    if (Lnast_ntype::is_if_like(t) || Lnast_ntype::is_for(t) || Lnast_ntype::is_while(t) || Lnast_ntype::is_stmts(t)) {
      return false;
    }
    if (a.is_invalid() || b.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(a))) {
      continue;
    }
    if (Lnast_ntype::is_declare(t)) {
      const auto mode = ln.get_sibling_next(b);
      if (!mode.is_invalid() && Lnast_ntype::is_const(ln.get_type(mode))
          && ln.get_name(mode).find("comptime") != std::string_view::npos) {
        if (ln.get_name(a) == flow) {
          return true;
        }
        comptime_decls.insert(ln.get_name(a));
      }
      continue;
    }
    if (Lnast_ntype::is_store(t) && Lnast_ntype::is_ref(ln.get_type(b)) && ln.get_sibling_next(b).is_invalid()
        && ln.get_name(b) == flow) {
      if (comptime_decls.contains(ln.get_name(a))) {
        return true;
      }
      if (!Lnast::is_tmp(ln.get_name(a))) {
        return false;  // a runtime variable holds the value
      }
      flow = ln.get_name(a);  // a compiler copy: follow it
      continue;
    }
    for (auto n : ln.depth_preorder(s)) {
      if (Lnast_ntype::is_ref(ln.get_type(n)) && ln.get_name(n) == flow) {
        return false;  // consumed by anything else: a runtime use
      }
    }
  }
  return false;
}

}  // namespace

std::optional<uPass_runner::Array_port_shape> uPass_runner::array_port_shape(
    const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e, bool output,
    const absl::flat_hash_map<std::string, Generic_bind>& gbinds, const Lnast_node* actual) {
  if (e.array_size > 0 && e.elem_bits > 0) {
    // A multi-dimensional port's elem_bits is one packed row: the leaf is that
    // row split over the inner dims.
    int64_t row = 1;
    for (const auto d : e.inner_dims) {
      row *= d;
    }
    if (row <= 0 || e.elem_bits % row != 0) {
      return std::nullopt;
    }
    const auto w = static_cast<uint32_t>(e.elem_bits / row);
    return Array_port_shape{.lanes      = e.array_size,
                            .elem_min   = upass::min_from_bits(w, e.elem_signed),
                            .elem_max   = upass::max_from_bits(w, e.elem_signed),
                            .elem_bool  = e.elem_bool,
                            .inner_dims = e.inner_dims};
  }
  if (!callee || e.bits > 0 || e.array_size > 0) {
    return std::nullopt;
  }
  const auto at = unsized_array_port(*callee, e, output);
  if (at.is_invalid()) {
    return std::nullopt;
  }
  // `comp_type_array(elem, dim)`, outer dim first: a multi-dimensional port's
  // elem is itself the array of the next dimension (`[N][M]T`).
  std::vector<int64_t> dims;
  auto                 elem = callee->get_first_child(at);
  for (;;) {
    const auto dim     = elem.is_invalid() ? elem : callee->get_sibling_next(elem);
    // Missing/empty dimensions are filled from the actual below; retain
    // the declared element envelope independently of the extent.
    // The dim rides as `const '[N]'` (the written name) or `ref %t` (`[N+1]`).
    auto       dim_txt = dim.is_invalid() ? std::string_view{} : callee->get_name(dim);
    if (dim_txt.size() >= 2 && dim_txt.front() == '[' && dim_txt.back() == ']') {
      dim_txt = dim_txt.substr(1, dim_txt.size() - 2);
    }
    if (dim_txt.empty()) {
      dims.push_back(0);
    } else {
      const auto lanes = fold_template_bound(callee, dim_txt, gbinds);
      if (!lanes || !lanes->is_just_i64() || lanes->to_just_i64() <= 0) {
        return std::nullopt;
      }
      dims.push_back(lanes->to_just_i64());
    }
    if (!Lnast_ntype::is_comp_type_array(callee->get_type(elem))) {
      break;
    }
    elem = callee->get_first_child(elem);
  }
  Array_port_shape shape{.lanes       = dims.front(),
                         .infer_lanes = dims.front() == 0,
                         .inner_dims  = std::vector<int64_t>(dims.begin() + 1, dims.end())};
  if (shape.infer_lanes && output) {
    return std::nullopt;  // This inference rule applies to input arguments.
  }
  if (shape.infer_lanes && actual) {
    if (actual->is_const()) {
      shape.lanes = 1;
    } else if (actual->is_ref()) {
      const auto name = actual->get_name();
      const auto b    = symbol_table_.get_bundle(name);
      if (b && !b->get_attr("__array_size").is_invalid() && b->get_attr("__array_size").is_just_i64()) {
        shape.lanes = b->get_attr("__array_size").to_just_i64();
      } else if (const auto* port = lm->get_lnast()->io_meta().find(name); port && port->array_size > 0) {
        shape.lanes = port->array_size;
      } else if (auto fields = try_tuple_shape(name)) {
        shape.lanes = fields->size();
      } else if (actual_node_kind(*actual) != Io_kind::none) {
        shape.lanes = 1;
      }
    }
  }
  if (Lnast_ntype::is_prim_type_bool(callee->get_type(elem))) {
    shape.elem_bool = true;
    shape.elem_min  = *Dlop::create_integer(0);
    shape.elem_max  = *Dlop::create_integer(1);
    return shape;
  }
  const auto mx   = Lnast_ntype::is_prim_type_int(callee->get_type(elem)) ? callee->get_first_child(elem) : Lnast_nid{};
  const auto mn   = mx.is_invalid() ? mx : callee->get_sibling_next(mx);
  const auto vmax = mn.is_invalid() ? std::nullopt : fold_template_bound(callee, callee->get_name(mx), gbinds);
  const auto vmin = mn.is_invalid() ? std::nullopt : fold_template_bound(callee, callee->get_name(mn), gbinds);
  if (!vmax || !vmin) {
    return std::nullopt;
  }
  shape.elem_max = *vmax;
  shape.elem_min = *vmin;
  return shape;
}

std::optional<std::pair<Dlop, Dlop>> uPass_runner::declared_param_range(
    const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e, const absl::flat_hash_map<std::string, Generic_bind>& gbinds) {
  if (e.array_size > 0 || (callee && is_array_port(*callee, e, false))) {
    return std::nullopt;  // an array port: its shape is checked lane by lane (check_call_args_fit)
  }
  const auto gbi = e.type_name.empty() ? gbinds.end() : gbinds.find(e.type_name);
  const bool gen = gbi != gbinds.end() && gbi->second.type_name.empty() && gbi->second.const_text.empty();
  if (e.kind == Io_kind::boolean || (gen && gbi->second.kind == Io_kind::boolean)) {
    return std::pair{*Dlop::create_integer(0), *Dlop::create_integer(1)};
  }
  if (e.kind == Io_kind::integer && e.has_range) {
    return std::pair{*Dlop::create_integer(e.range_min), *Dlop::create_integer(e.range_max)};
  }
  if (e.kind == Io_kind::integer && e.wide_range_min && e.wide_range_max) {
    return std::pair{*e.wide_range_min, *e.wide_range_max};
  }
  if (e.kind == Io_kind::integer && e.bits > 0 && e.array_size == 0) {
    const auto w = static_cast<uint32_t>(e.bits);
    return std::pair{upass::min_from_bits(w, e.is_signed), upass::max_from_bits(w, e.is_signed)};
  }
  if (e.has_deferred_bound()) {
    // Generic-width port (`a:unsigned(bits=N)`): the bound folded under this
    // call's binds. A bound that does not fold here is diagnosed by the
    // specialization / splice path, not by the argument check.
    if (!callee) {
      return std::nullopt;
    }
    std::optional<Dlop> dmax;
    std::optional<Dlop> dmin;
    if (!e.bound_max_text.empty() && e.bound_max_text != "nil") {
      dmax = fold_template_bound(callee, e.bound_max_text, gbinds, 0, nullptr);
    }
    if (!e.bound_min_text.empty() && e.bound_min_text != "nil") {
      dmin = fold_template_bound(callee, e.bound_min_text, gbinds, 0, nullptr);
    }
    if (dmax && dmin) {
      return std::pair{*dmin, *dmax};
    }
    return std::nullopt;
  }
  if (gen && gbi->second.kind == Io_kind::integer && gbi->second.max && gbi->second.min) {
    return std::pair{*gbi->second.min, *gbi->second.max};  // `a:T` with T bound to an integer type
  }
  return std::nullopt;
}

upass::Kind uPass_runner::value_kind_of(const Lnast_node& v) const {
  if (v.is_const()) {
    const auto txt = v.get_name();
    if (txt == "true" || txt == "false") {
      return upass::Kind::boolean;
    }
    if (!txt.empty() && txt.front() == '"') {
      return upass::Kind::string;
    }
    if (txt == "nil" || txt == "0sb?" || txt == "0ub?") {
      return upass::Kind::unknown;  // nil, or the typeless single unknown bit
    }
    // A string literal rides as `'text'` too (a one-character `'a'` is a
    // character, an integer).
    const auto& c = Dlop::from_pyrope_cached(txt);
    if (c.is_invalid()) {
      return upass::Kind::unknown;
    }
    return c.is_integer() ? upass::Kind::integer : c.is_string() ? upass::Kind::string : upass::Kind::unknown;
  }
  if (!v.is_ref()) {
    return upass::Kind::unknown;
  }
  const auto name = v.get_name();
  if (const auto b = symbol_table_.get_bundle(name)) {
    if (const auto k = upass::decl_facts::bundle_kind(*b); k != upass::Kind::unknown) {
      return k;
    }
  }
  return upass::decl_facts::operand_kind(symbol_table_, lm ? lm->get_lnast().get() : nullptr, name);
}

void uPass_runner::check_call_arg_kind(const Lnast_io_entry& e, const absl::flat_hash_map<std::string, Generic_bind>& gbinds,
                                       const Lnast_node& actual, std::string_view callee,
                                       const livehd::diag::Span& call_span) const {
  if (e.array_size != 0) {
    return;
  }
  // A `Clock`/`Reset` input (or actual) is a 1-bit signal that binds by type
  // (docs 07-typesystem "Clock and Reset"): a Bool expression binds to a Reset
  // without a cast, and a clock crosses into a Verilog child's 1-bit `clk`.
  // Only a string is never one.
  bool clock_reset = e.sig != Io_sig::none;
  if (actual.is_ref() && lm) {
    if (const auto* ae = lm->get_lnast()->io_meta().find(actual.get_name()); ae != nullptr && ae->sig != Io_sig::none) {
      clock_reset = true;
    }
  }
  if (clock_reset) {
    if (value_kind_of(actual) == upass::Kind::string) {
      check_bind_kind(e, Io_kind::boolean, upass::Kind::string, callee, call_span);
    }
    return;
  }
  auto want = e.kind;
  if (const auto gbi = e.type_name.empty() ? gbinds.end() : gbinds.find(e.type_name);
      gbi != gbinds.end() && gbi->second.type_name.empty() && gbi->second.const_text.empty()) {
    want = gbi->second.kind;  // `a:T` with T bound to a scalar type
  }
  check_bind_kind(e, want, value_kind_of(actual), callee, call_span);
}

void uPass_runner::check_bind_kind(const Lnast_io_entry& e, Io_kind want, upass::Kind got, std::string_view callee,
                                   const livehd::diag::Span& call_span) {
  if (want == Io_kind::boolean && got == upass::Kind::integer) {
    fcall_arg_fail(call_span,
                   "fcall-arg-kind",
                   std::format("cannot bind integer value to input `{}` of `{}` (it is boolean)", e.name, callee),
                   "an integer never turns into a `Bool` implicitly: pass `Bool(x)` (true when non-zero, i.e. `x != 0`), "
                   "pick one bit with `Bool(x#[i])`, or write `true`/`false` for a constant",
                   "type");
  }
  if (want == Io_kind::integer && got == upass::Kind::boolean) {
    fcall_arg_fail(call_span,
                   "fcall-arg-kind",
                   std::format("cannot bind boolean value to input `{}` of `{}` (it is integer)", e.name, callee),
                   "a `Bool` never turns into an integer implicitly: pass `U1(x)` (true == 1)",
                   "type");
  }
  if ((want == Io_kind::integer || want == Io_kind::boolean) && got == upass::Kind::string) {
    fcall_arg_fail(call_span,
                   "fcall-arg-kind",
                   std::format("cannot bind string value to input `{}` of `{}` (it is {})",
                               e.name,
                               callee,
                               want == Io_kind::boolean ? "boolean" : "integer"),
                   "a string never turns into an integer or a `Bool`",
                   "type");
  }
}

bool uPass_runner::sub_input_may_be_omitted(const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e) const {
  if (upass::io_port::declares_input_default(callee.get(), e)) {
    return true;
  }
  return (upass::io_port::is_clock_candidate(e) || upass::io_port::is_reset_candidate(e)) && !lm->get_lnast()->is_verilog_origin();
}

void uPass_runner::warn_auto_wire_to_minted(const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e, std::string_view bare,
                                            const livehd::diag::Span& call_span) const {
  const bool is_clock = upass::io_port::is_clock_candidate(e);
  if (!callee || (!is_clock && !upass::io_port::is_reset_candidate(e)) || upass::io_port::declares_input_default(callee.get(), e)) {
    return;
  }
  const auto caller = lm->get_lnast();
  if (!caller || caller->is_verilog_origin() || lm->in_inline_frame()) {
    return;
  }
  const auto  want = is_clock ? Io_sig::clock : Io_sig::reset;
  const auto& ins  = caller->io_meta().inputs;
  if (std::any_of(ins.begin(), ins.end(), [&](const Lnast_io_entry& i) { return i.sig == want; })) {
    return;  // the caller's own declared Clock/Reset is what auto-wires: no mint
  }
  static const absl::flat_hash_set<std::string_view> clock_names{"clk", "clock", "clk_i", "clk_in"};
  static const absl::flat_hash_set<std::string_view>
      reset_names{"rst", "reset", "rst_n", "reset_n", "rstn", "resetn", "rst_i", "rst_ni", "arst", "arst_n", "srst", "srst_n"};
  const auto&       conventional = is_clock ? clock_names : reset_names;
  const std::string minted       = is_clock ? "clock" : "reset";
  // A data input spelled exactly `clock`/`reset` collides with the mint itself:
  // that is its own compile error, not this warning.
  const auto        data         = std::find_if(ins.begin(), ins.end(), [&](const Lnast_io_entry& i) {
    return i.sig == Io_sig::none && i.name != minted && (i.name == e.name || conventional.contains(i.name));
  });
  if (data == ins.end()) {
    return;
  }
  const auto caller_name = caller->get_top_module_name();
  const auto caller_bare = caller_name.substr(caller_name.rfind('.') + 1);
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::warning,
      .code     = is_clock ? "clock-auto-wire-minted" : "reset-auto-wire-minted",
      .category = "time",
      .pass     = "upass.runner",
      .message  = std::format("the `{}` input `{}` of `{}` is auto-wired to the minted `{}` of `{}`, not to its data input `{}`; "
                              "nothing drives `{}` unless the caller of `{}` sets it",
                             is_clock ? "Clock" : "Reset",
                             e.name,
                             bare,
                             minted,
                             caller_bare,
                             data->name,
                             minted,
                             caller_bare),
      .span     = call_span,
      .hint     = is_clock
                      ? std::format("bind it explicitly, or declare `{}:Clock` in `{}` so it is the clock", data->name, caller_bare)
                      : std::format("bind it explicitly (`{}=Bool({})` for an integer `{}`), or declare `{}:Reset` in `{}`",
                                e.name,
                                data->name,
                                data->name,
                                data->name,
                                caller_bare),
  });
}

void uPass_runner::check_call_args_fit(const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io,
                                       const std::vector<Lnast_node>& param_val, const std::vector<bool>& param_set,
                                       std::size_t nbind, const absl::flat_hash_map<std::string, Generic_bind>& gbinds,
                                       std::string_view callee_name, const livehd::diag::Span& call_span) {
  // Diagnostics name the callee as the user spells it, never import-qualified.
  const auto bare = callee_name.substr(callee_name.rfind('.') == std::string_view::npos ? 0 : callee_name.rfind('.') + 1);
  for (std::size_t i = 0; i < nbind && i < io.inputs.size() && i < param_val.size(); ++i) {
    if (!param_set[i]) {
      check_omitted_default_fit(callee, io.inputs[i], gbinds, bare, call_span);
      warn_auto_wire_to_minted(callee, io.inputs[i], bare, call_span);
      continue;
    }
    if (param_val[i].is_invalid()) {
      continue;
    }
    const auto& e = io.inputs[i];
    if (e.array_size > 0 || (callee && is_array_port(*callee, e, false))) {
      check_call_array_arg(callee, e, gbinds, param_val[i], bare, call_span);
      continue;
    }
    check_call_arg_kind(e, gbinds, param_val[i], bare, call_span);
    const auto declared = declared_param_range(callee, e, gbinds);
    if (!declared) {
      continue;  // untyped: the parameter takes the actual's type
    }
    const auto& [dmin, dmax] = *declared;
    const auto actual        = value_range_of(param_val[i]);
    if (!actual.may_exceed(dmin, dmax)) {
      continue;  // not an integer (a kind mismatch is typecheck's), or every value it may take fits
    }
    const auto gbi = e.type_name.empty() ? gbinds.end() : gbinds.find(e.type_name);
    if (e.kind == Io_kind::boolean
        || (gbi != gbinds.end() && gbi->second.type_name.empty() && gbi->second.const_text.empty()
            && gbi->second.kind == Io_kind::boolean)) {
      fcall_arg_fail(call_span,
                     "fcall-arg-overflow",
                     std::format("cannot bind integer value to input `{}` of `{}` (it is boolean)", e.name, bare),
                     "an integer never narrows into a `Bool` implicitly: pass `Bool(x)` (true when non-zero, i.e. "
                     "`x != 0`), or pick one bit with `Bool(x#[i])`",
                     "type");
    }
    std::string what     = "unbounded range";
    bool        disjoint = false;
    if (actual.bounded()) {
      const auto& amin = *actual.min;
      const auto& amax = *actual.max;
      disjoint         = amin.gt_op(dmax)->is_known_true() || amax.lt_op(dmin)->is_known_true();
      what             = amin.same_repr(amax) ? std::format("value {}", amin.to_decimal_string())
                                              : std::format("range [{}, {}]", amin.to_decimal_string(), amax.to_decimal_string());
    }
    std::string example;
    if (!dmin.is_negative() && dmax.add_op(*Dlop::create_integer(1))->is_power2()) {
      example = std::format(" (e.g. `{}=x#[0..<{}]`)", e.name, dmax.get_payload_bits());
    }
    fcall_arg_fail(call_span,
                   "fcall-arg-overflow",
                   std::format("argument `{}` ({}) in call to `{}` {} not fit its declared range [{}, {}]",
                               e.name,
                               what,
                               bare,
                               disjoint ? "does" : "may",
                               dmin.to_decimal_string(),
                               dmax.to_decimal_string()),
                   std::format("an argument never narrows implicitly: slice it at the call site{} or narrow it into a "
                               "typed local with a `wrap`/`sat` assignment first",
                               example),
                   "type");
  }
}

void uPass_runner::check_omitted_default_fit(const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e,
                                             const absl::flat_hash_map<std::string, Generic_bind>& gbinds, std::string_view bare,
                                             const livehd::diag::Span& call_span) {
  // A `mod`/`pipe` default drives the Sub port from this call (lnast.tolg), so
  // it binds like an argument: the same kind and overflow rules. (A comb's
  // default is a body-prologue local, checked where the inlined body assigns it.)
  auto dv = upass::io_port::input_default_const(callee.get(), e);
  if (!dv && e.has_default && callee->is_template() && callee->get_lambda_kind() != "comb") {
    // A default over a generic (`c:u8 = N * 2`, ruling 34) folds with this
    // call's binds: the value the specialization's slot will hold. One this
    // bound fold cannot evaluate (a comb call, an if-expression) is folded by
    // the specialization's own walk, which pass.upass checks the same way.
    dv = fold_template_bound(callee, Lnast_io_entry::default_value_name(e.name), gbinds, 0, nullptr);
  }
  if (!dv || !dv->is_integer() || e.array_size != 0) {
    return;
  }
  check_bind_kind(e, e.kind, upass::Kind::integer, bare, call_span);
  const auto declared = declared_param_range(callee, e, gbinds);
  if (!declared || !(dv->lt_op(declared->first)->is_known_true() || dv->gt_op(declared->second)->is_known_true())) {
    return;
  }
  fcall_arg_fail(call_span,
                 "fcall-arg-overflow",
                 std::format("the default of input `{}` of `{}` (value {}) does not fit its declared range [{}, {}]",
                             e.name,
                             bare,
                             dv->to_decimal_string(),
                             declared->first.to_decimal_string(),
                             declared->second.to_decimal_string()),
                 "a default binds like an argument and never narrows implicitly: change the default, or widen the input's type",
                 "type");
}

std::optional<uPass_runner::Array_prefill> uPass_runner::array_init_prefill() const {
  const auto& ln = lm->get_lnast();
  if (lm->current_num_children() != 2 || !lm->has_child() || (ln->is_template() && !lm->in_inline_frame())
      || ln->is_verilog_origin()) {
    return std::nullopt;
  }
  const auto stmt = lm->get_current_nid();
  const auto dst  = ln->get_first_child(stmt);
  const auto val  = dst.is_invalid() ? dst : ln->get_sibling_next(dst);
  if (val.is_invalid() || !Lnast_ntype::is_ref(ln->get_type(dst)) || !Lnast_ntype::is_const(ln->get_type(val))) {
    return std::nullopt;
  }
  // A known value only (the front end's pre-fill skips `nil` the same way).
  if (const auto& v = Dlop::from_pyrope_cached(ln->get_name(val));
      v.is_invalid() || !(v.is_integer() || v.is_bool()) || v.has_unknowns()) {
    return std::nullopt;
  }
  // The initializer right after its array declare, `declare(m, comp_type_array
  // (…, [n]), mut); store(m, 0)`, and not already followed by the front end's
  // own per-lane pre-fill (an extent it could fold, `[4]`): that one starts at
  // lane `0…0` with the initializer's value, a user's first element write
  // (`m[1][101] = 3` into a `[2][100..<102]` it could not size) does not.
  const auto var  = ln->get_name(dst);
  const auto decl = ln->get_sibling_prev(stmt);
  if (decl.is_invalid() || !Lnast_ntype::is_declare(ln->get_type(decl))) {
    return std::nullopt;
  }
  const auto dvar = ln->get_first_child(decl);
  auto       ty   = dvar.is_invalid() ? dvar : ln->get_sibling_next(dvar);
  if (ty.is_invalid() || ln->get_name(dvar) != var || !Lnast_ntype::is_comp_type_array(ln->get_type(ty))) {
    return std::nullopt;
  }
  if (const auto next = ln->get_sibling_next(stmt); !next.is_invalid() && Lnast_ntype::is_store(ln->get_type(next))) {
    const auto ndst    = ln->get_first_child(next);
    bool       prefill = !ndst.is_invalid() && ln->get_name(ndst) == var;
    size_t     nsel    = 0;
    auto       k       = ndst.is_invalid() ? ndst : ln->get_sibling_next(ndst);
    for (; prefill && !k.is_invalid() && !ln->get_sibling_next(k).is_invalid(); k = ln->get_sibling_next(k), ++nsel) {
      prefill = Lnast_ntype::is_const(ln->get_type(k)) && ln->get_name(k) == "0";
    }
    if (prefill && nsel > 0 && !k.is_invalid() && Lnast_ntype::is_const(ln->get_type(k)) && ln->get_name(k) == ln->get_name(val)) {
      return std::nullopt;
    }
  }
  Array_prefill fill{.var = std::string(var), .value = std::string(ln->get_name(val)), .dims = {}};

  // An index-range / enum dimension has the extent its declaration was
  // lowered to (Index_dims_scope recorded it on the binding).
  std::shared_ptr<const Bundle> binding;
  {
    const auto here = lm->save_cursor();
    lm->move_to_nid(dst);
    binding = symbol_table_.get_bundle(lm->current_text());
    lm->restore_cursor(here);
  }
  int64_t lanes = 1;
  for (size_t k = 0; !ty.is_invalid() && Lnast_ntype::is_comp_type_array(ln->get_type(ty)); ty = ln->get_first_child(ty), ++k) {
    const auto             dim = upass::array_level_dim(*ln, ty);
    std::optional<int64_t> n;
    if (!dim.is_invalid() && Lnast_ntype::is_const(ln->get_type(dim))) {
      n = upass::array_dim_lanes(ln->get_name(dim));
    }
    if (const auto nk = std::format("__array_n{}", k); !n && binding && binding->has_attr(nk)) {
      n = binding->get_attr(nk).to_just_i64();
    }
    constexpr int64_t kMaxPrefillLanes = int64_t{1} << 16;
    if (!n || *n <= 0 || lanes > kMaxPrefillLanes / *n) {
      return std::nullopt;
    }
    lanes *= *n;
    fill.dims.push_back(*n);
  }
  return fill;
}

void uPass_runner::emit_array_prefill(const Array_prefill& fill) {
  // Every flat slot, outermost index first -- the order prp2lnast pre-fills a
  // `[4][2]u8 = v` it can size, so a later element read or a runtime-index
  // read-modify-write (`m[i]#[j] = b`) finds each lane. The lanes are
  // physical: an index-range array is not rebased again.
  std::vector<int64_t> idx(fill.dims.size(), 0);
  const bool           outer_physical = std::exchange(physical_indices_, true);
  while (true) {
    std::vector<Lnast_node> ops;
    ops.reserve(idx.size() + 1);
    for (const auto i : idx) {
      ops.push_back(Lnast_node::create_const(std::to_string(i)));
    }
    ops.push_back(Lnast_node::create_const(fill.value));
    emit_inline_op(Lnast_ntype::create_store(), fill.var, ops);
    auto k = static_cast<int>(idx.size()) - 1;
    for (; k >= 0; --k) {
      if (++idx[k] < fill.dims[k]) {
        break;
      }
      idx[k] = 0;
    }
    if (k < 0) {
      physical_indices_ = outer_physical;
      return;
    }
  }
}

void uPass_runner::track_open_fill_array() {
  // Cursor at a whole store `store(a, v)`: it replaces any contents a tracked
  // `a` had. It is the fill of an inferred-extent array only as the
  // initializer right after `declare(a, comp_type_array([]…), mut)`.
  const auto& ln  = lm->get_lnast();
  const auto  dst = ln->get_first_child(lm->get_current_nid());
  const auto  val = dst.is_invalid() ? dst : ln->get_sibling_next(dst);
  if (val.is_invalid() || !Lnast_ntype::is_ref(ln->get_type(dst))) {
    return;
  }
  std::string var;
  {
    const auto here = lm->save_cursor();
    lm->move_to_nid(dst);
    var = std::string(lm->current_text());
    lm->restore_cursor(here);
  }
  open_fill_arrays_.erase(var);
  const auto decl = ln->get_sibling_prev(lm->get_current_nid());
  if (!Lnast_ntype::is_const(ln->get_type(val)) || decl.is_invalid() || !Lnast_ntype::is_declare(ln->get_type(decl))
      || (ln->is_template() && !lm->in_inline_frame())) {
    return;
  }
  const auto dvar = ln->get_first_child(decl);
  const auto type = dvar.is_invalid() ? dvar : ln->get_sibling_next(dvar);
  const auto mode = type.is_invalid() ? type : ln->get_sibling_next(type);
  if (mode.is_invalid() || ln->get_name(dvar) != ln->get_name(dst) || ln->get_name(mode) != "mut"
      || !Lnast_ntype::is_comp_type_array(ln->get_type(type))) {
    return;
  }
  for (auto level = type; Lnast_ntype::is_comp_type_array(ln->get_type(level)); level = ln->get_first_child(level)) {
    const auto dim = upass::array_level_dim(*ln, level);
    if (dim.is_invalid() || ln->get_name(dim) != "[]") {
      return;  // a sized dimension: array_init_prefill fills every lane up front
    }
  }
  if (const auto& v = Dlop::from_pyrope_cached(ln->get_name(val));
      v.is_invalid() || !(v.is_integer() || v.is_bool()) || v.has_unknowns()) {
    return;  // `nil` / `0sb?`: no known fill
  }
  open_fill_arrays_.insert_or_assign(std::move(var), std::string(ln->get_name(val)));
}

bool uPass_runner::try_open_fill_access() {
  if (open_fill_arrays_.empty() || !lm->has_child()) {
    return false;
  }
  // store(a, i…, v) / tuple_get(d, a, i…), every operand a leaf.
  const bool              is_store = Lnast_ntype::is_store(lm->get_raw_ntype());
  std::vector<Lnast_node> kids;
  {
    const auto here = lm->save_cursor();
    for (bool more = lm->move_to_child(); more; more = lm->move_to_sibling()) {
      if (!Lnast_ntype::is_ref(lm->get_raw_ntype()) && !Lnast_ntype::is_const(lm->get_raw_ntype())) {
        kids.clear();
        break;
      }
      kids.push_back(lm->current_node());
    }
    lm->restore_cursor(here);
  }
  const size_t base = is_store ? 0 : 1;
  if (kids.size() < 3 || !kids[base].is_ref()) {
    return false;
  }
  const auto it = open_fill_arrays_.find(kids[base].get_name());
  if (it == open_fill_arrays_.end()) {
    return false;
  }
  const size_t last = is_store ? kids.size() - 1 : kids.size();  // one past the last index
  std::string  path;
  for (size_t k = base + 1; k < last; ++k) {
    std::optional<int64_t> v;
    if (kids[k].is_const()) {
      v = upass::int_literal(kids[k].get_name());
    } else if (const auto fv = symbol_table_.known_const_scalar(kids[k].get_name()); fv && fv->is_just_i64()) {
      v = fv->to_just_i64();
    }
    if (!v || *v < 0) {
      // A runtime index: the runner no longer knows which entries a write
      // reached (and a read of one decides nothing here).
      if (is_store) {
        open_fill_arrays_.erase(it);
      }
      return false;
    }
    absl::StrAppend(&path, path.empty() ? "" : ".", *v);
  }
  if (is_store) {
    return false;  // a constant-index write defines its entry as usual
  }
  const auto b = symbol_table_.get_bundle(it->first);
  if (!b || b->has_trivial(bundle_path::of_string(path)) || b->has_bundle(bundle_path::of_string(path))) {
    return false;
  }
  // No write reached this entry: it holds the initializer's fill.
  emit_inline_op(Lnast_ntype::create_store(), std::string(kids[0].get_name()), {Lnast_node::create_const(it->second)});
  return true;
}

std::optional<uPass_runner::Array_index_dim> uPass_runner::array_index_dim(std::string_view dim_txt, std::string* why) const {
  if (dim_txt.size() >= 2 && dim_txt.front() == '[' && dim_txt.back() == ']') {
    dim_txt = dim_txt.substr(1, dim_txt.size() - 2);
  }
  const auto fold = [&](std::string_view bound) -> std::optional<int64_t> {
    if (const auto v = upass::int_literal(bound)) {
      return v;
    }
    if (const auto v = fold_frame_ref(bound); v && v->is_just_i64()) {
      return v->to_just_i64();
    }
    return std::nullopt;
  };
  if (const auto r = upass::array_dim_range(dim_txt, fold)) {
    return Array_index_dim{.lanes = r->lanes, .lo = r->lo, .enum_type = {}};
  }
  // `[X]` over an enum TYPE: its bundle holds one named entry per enum entry,
  // each tagged `<entry>.enumentry`.
  auto b = symbol_table_.get_bundle(dim_txt);
  if (!b && lm->in_inline_frame()) {
    b = symbol_table_.get_bundle(lm->frame_variable(dim_txt));
  }
  if (!b || !b->has_named_top() || b->has_unnamed_top()) {
    return std::nullopt;
  }
  const auto& attrs = b->get_attrs();
  if (std::none_of(attrs.begin(), attrs.end(), [](const auto& kv) {
        return Bundle::get_last_level(kv.first) == battr::enumentry;
      })) {
    return std::nullopt;
  }
  std::vector<int64_t> values;
  for (const auto& tl : b->top_levels()) {
    if (tl.has_leafs || !tl.scalar.is_just_i64()) {
      *why = "has hierarchical or payload entries";
      return std::nullopt;
    }
    values.push_back(tl.scalar.to_just_i64());
  }
  std::sort(values.begin(), values.end());
  const Array_index_dim map{.lanes = static_cast<int64_t>(values.size()), .lo = values.front(), .enum_type = std::string(dim_txt)};
  // One entry per enum entry, at the entry's value: the values must be
  // consecutive (a default one-hot enum numbers 1, 2, 4, …).
  if (std::adjacent_find(values.begin(), values.end()) != values.end() || values.back() - values.front() + 1 != map.lanes) {
    *why = "does not number its entries consecutively";
  }
  return map;
}

std::optional<uPass_runner::Array_index_dim> uPass_runner::range_value_dim(std::string_view name) const {
  // A comptime `range` value carries its (inclusive) bounds as attrs.
  const auto b = symbol_table_.get_bundle(name);
  if (!b || !b->has_attr("rng_s") || !b->has_attr("rng_e")) {
    return std::nullopt;
  }
  const auto& start = b->get_attr("rng_s");
  const auto& end   = b->get_attr("rng_e");
  const auto& step  = b->get_attr("rng_step");
  if (!start.is_just_i64() || !end.is_just_i64() || (b->has_attr("rng_step") && !(step.is_just_i64() && step.to_just_i64() == 1))
      || end.to_just_i64() < start.to_just_i64()) {
    return std::nullopt;
  }
  return Array_index_dim{.lanes = end.to_just_i64() - start.to_just_i64() + 1, .lo = start.to_just_i64(), .enum_type = {}};
}

uPass_runner::Index_dims_scope::Index_dims_scope(uPass_runner& r) : r_(r) {
  const auto& lm = r_.lm;
  if (!lm->has_child()) {
    return;
  }
  const auto& ln   = lm->get_lnast();
  const auto  name = ln->get_first_child(lm->get_current_nid());
  const auto  type = name.is_invalid() ? name : ln->get_sibling_next(name);
  if (type.is_invalid() || !Lnast_ntype::is_ref(ln->get_type(name)) || !Lnast_ntype::is_comp_type_array(ln->get_type(type))) {
    return;
  }
  for (auto level = type; !level.is_invalid() && Lnast_ntype::is_comp_type_array(ln->get_type(level));
       level      = ln->get_first_child(level)) {
    Level lv{.dim = upass::array_level_dim(*ln, level), .text = {}, .map = {}, .is_ref = false};
    if (!lv.dim.is_invalid() && Lnast_ntype::is_const(ln->get_type(lv.dim))) {
      lv.text = std::string(ln->get_name(lv.dim));
      std::string why;
      if (lv.text != "[]" && !upass::array_dim_lanes(lv.text)) {
        if (const auto m = r_.array_index_dim(lv.text, &why)) {
          lv.map  = *m;
          mapped_ = true;
        }
      }
      if (!why.empty()) {
        r_.index_dim_reported_ = true;  // the bake does not report the dim again
        livehd::diag::sink().emit(livehd::diag::Diagnostic{
            .severity = livehd::diag::Severity::error,
            .code     = "array-dim-enum",
            .category = "type",
            .pass     = "upass.runner",
            .message  = std::format("array dimension `{}` is an enum that {}, so it has no dense index", lv.text, why),
            .span     = lm->current_span(),
            .hint     = "an enum dimension needs a flat enum numbered consecutively: give its first entry a value "
                        "(`enum X = (a = 0, b, c)`)",
        });
      }
    } else if (!lv.dim.is_invalid() && Lnast_ntype::is_ref(ln->get_type(lv.dim))) {
      // An index range whose bounds prp2lnast could not fold (`[N..<(N+2)]`
      // over a generic) is a `range` value, known once the generic is bound.
      const auto here = lm->save_cursor();
      lm->move_to_nid(lv.dim);
      const std::string value(lm->current_text());
      lm->restore_cursor(here);
      if (const auto m = r_.range_value_dim(value)) {
        lv.text   = std::string(ln->get_name(lv.dim));
        lv.map    = *m;
        lv.is_ref = true;
        mapped_   = true;
      }
    }
    levels_.push_back(std::move(lv));
  }
  ln_             = ln;
  const auto here = lm->save_cursor();
  (void)lm->move_to_child();
  var_ = std::string(lm->current_text());
  lm->restore_cursor(here);
  for (const auto& lv : levels_) {
    if (lv.map.lanes > 0) {
      if (lv.is_ref) {
        ln_->set_type(lv.dim, Lnast_ntype::create_const());
      }
      ln_->set_name(lv.dim, std::format("[{}]", lv.map.lanes));
    }
  }
}

uPass_runner::Index_dims_scope::~Index_dims_scope() {
  if (levels_.empty()) {
    return;
  }
  r_.index_dim_reported_ = false;
  for (const auto& lv : levels_) {
    if (lv.map.lanes > 0) {
      if (lv.is_ref) {
        ln_->set_type(lv.dim, Lnast_ntype::create_ref());
      }
      ln_->set_name(lv.dim, lv.text);
    }
  }
  // The index map rides the array's binding (and so every copy of it, a comb
  // argument included). A plain array declaration drops any map an earlier
  // binding of the same name left behind.
  const auto rb    = r_.symbol_table_.get_bundle(var_);
  const auto stale = [&] {
    for (size_t k = 0; k < levels_.size(); ++k) {
      if (rb->has_attr(std::format("__array_n{}", k))) {
        return true;
      }
    }
    return false;
  };
  if (!rb || (!mapped_ && !stale())) {
    return;
  }
  const auto b = r_.symbol_table_.get_bundle_for_write(var_);
  for (size_t k = 0; k < levels_.size(); ++k) {
    const auto& m  = levels_[k].map;
    const auto  nk = std::format("__array_n{}", k);
    const auto  lk = std::format("__array_lo{}", k);
    const auto  ek = std::format("__array_enum{}", k);
    if (m.lanes <= 0) {
      b->clear_attr(nk);
      b->clear_attr(lk);
      b->clear_attr(ek);
      continue;
    }
    b->set_attr(nk, *Dlop::create_integer(m.lanes));
    b->set_attr(lk, *Dlop::create_integer(m.lo));
    if (m.enum_type.empty()) {
      b->clear_attr(ek);
    } else {
      b->set_attr(ek, *Dlop::from_string(m.enum_type));
    }
  }
  r_.any_index_mapped_ = r_.any_index_mapped_ || mapped_;
}

bool uPass_runner::try_lower_array_index() {
  if (physical_indices_ || !any_index_mapped_ || !lm->has_child()) {
    return false;
  }
  const bool               is_store = Lnast_ntype::is_store(lm->get_raw_ntype());
  std::vector<Lnast_node>  kids;
  std::vector<std::string> raw;  // each operand as written in the tree being walked
  {
    const auto here = lm->save_cursor();
    for (bool more = lm->move_to_child(); more; more = lm->move_to_sibling()) {
      if (!Lnast_ntype::is_ref(lm->get_raw_ntype()) && !Lnast_ntype::is_const(lm->get_raw_ntype())) {
        kids.clear();
        break;
      }
      kids.push_back(lm->current_node());
      raw.emplace_back(lm->current_raw_text());
    }
    lm->restore_cursor(here);
  }
  // store(a, i…, v) / tuple_get(d, a, i…)
  const size_t base = is_store ? 0 : 1;
  if (kids.size() < 3 || !kids[base].is_ref()) {
    return false;
  }
  const size_t last         = is_store ? kids.size() - 1 : kids.size();  // one past the last index
  // The operator that computed temp `tmp` a few statements up (`ia + 0`), if
  // any: an arithmetic result is an integer, never an enum entry.
  const auto   integer_expr = [&](std::string_view tmp) {
    const auto& ln = lm->get_lnast();
    auto        at = ln->get_sibling_prev(lm->get_current_nid());
    for (int n = 0; n < 32 && !at.is_invalid(); ++n, at = ln->get_sibling_prev(at)) {
      const auto def = ln->get_first_child(at);
      if (!def.is_invalid() && Lnast_ntype::is_ref(ln->get_type(def)) && ln->get_name(def) == tmp) {
        const auto rule = upass::op_kind::rule_of(ln->get_type(at));
        return (rule && rule->result == upass::Kind::integer) || ln->get_type(at) == Lnast_ntype::Lnast_ntype_shl;
      }
    }
    return false;
  };
  // The index map rides the binding (a copy or a comb argument keeps it).
  const std::string array(kids[base].get_name());
  const auto        b = symbol_table_.get_bundle(array);
  if (!b || std::none_of(b->get_attrs().begin(), b->get_attrs().end(), [](const auto& kv) {
        return std::string_view(kv.first).starts_with("__array_n");
      })) {
    return false;
  }
  const auto shown_array = upass::Lnast_manager::user_name(array);
  const auto fail        = [&](std::string_view code, std::string msg, std::string hint) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
               .severity = livehd::diag::Severity::error,
               .code     = std::string(code),
               .category = "type",
               .pass     = "upass.runner",
               .message  = std::move(msg),
               .span     = lm->current_span(),
               .hint     = std::move(hint),
    });
    return false;
  };
  bool rebased = false;
  for (size_t k = 0; base + 1 + k < last; ++k) {
    const auto nk = std::format("__array_n{}", k);
    if (!b->has_attr(nk)) {
      continue;  // a plain zero-based dimension
    }
    const int64_t              n       = b->get_attr(nk).to_just_i64();
    const int64_t              lo      = b->get_attr(std::format("__array_lo{}", k)).to_just_i64();
    const auto&                en_attr = b->get_attr(std::format("__array_enum{}", k));
    const auto                 en      = en_attr.is_string() ? en_attr.to_string() : std::string{};
    auto&                      idx     = kids[base + 1 + k];
    std::optional<int64_t>     v;
    std::optional<std::string> idx_type;  // the index's enum/named type, "" = an integer, nullopt = unknown
    if (idx.is_const()) {
      v = upass::int_literal(idx.get_name());
      if (!v) {
        continue;  // not a position (a named-field key): judged elsewhere
      }
      idx_type = std::string{};
    } else {
      const auto  ib   = symbol_table_.get_bundle(idx.get_name());
      const auto& name = raw[base + 1 + k];
      if (ib && (ib->has_attr("rng_s") || ib->has_named_top() || ib->unnamed_top_count() > 1)) {
        return fail("array-index-not-scalar",
                    std::format("array `{}` is declared over an index range or enum: index one entry at a time", shown_array),
                    "a range or tuple index (a slice) is not supported on an array with an index map");
      }
      // An input of the unit or of the inlined comb being walked: its declared
      // type (an untyped comb input takes whatever the caller passes).
      const auto* port = lm->get_lnast()->io_meta().find(name);
      idx_type         = bundle_enum_type(ib);  // an enum entry (`X.t1`) carries its enum
      if (const auto fv = symbol_table_.known_const_scalar(idx.get_name()); fv && fv->is_just_i64()) {
        v = fv->to_just_i64();
        if (!idx_type) {
          idx_type = std::string{};
        }
      } else if (!idx_type && ib && !ib->get_type_name().empty()) {
        idx_type = std::string(ib->get_type_name());  // a typed local (`mut s:X`)
      } else if (!idx_type && port != nullptr) {
        if (!port->type_name.empty()) {
          idx_type = port->type_name;  // `s:X`
        } else if (port->kind != Io_kind::none) {
          idx_type = std::string{};  // `u:U2`
        }
      } else if (!idx_type && ib && declared_typed_.contains(std::string(idx.get_name()))) {
        idx_type = std::string{};  // `mut j:U2`
      } else if (!idx_type && Lnast::is_tmp(name) && integer_expr(name)) {
        idx_type = std::string{};  // an integer expression (`ia + 0`)
      }
    }
    // An enum dimension takes the entries of its enum only (08-memories.md
    // "Array index": `x2[0]` is an error). A runtime value of unknown type
    // cannot be judged here.
    if (!en.empty() && idx_type && *idx_type != en && *idx_type != Lnast::enum_encoding_type(en)) {
      // A compiler temp (`ia + 0`, `Z.z2`) has no source spelling to show.
      const bool named = idx.is_const() || !Lnast::is_tmp(idx.get_name());
      const auto kind  = idx_type->empty() ? std::string("an integer") : std::format("a `{}`", *idx_type);
      const auto what  = named ? std::format("`{}` ({})", upass::Lnast_manager::user_name(idx.get_name()), kind) : kind;
      return fail("array-index-not-enum",
                  std::format("array `{}` is indexed by the entries of enum `{}`, not by {}", shown_array, en, what),
                  std::format("index it with an entry of `{}` (`{}[{}.<entry>]`) or a value typed `{}`", en, shown_array, en, en));
    }
    if (v) {
      if (*v < lo || *v - lo >= n) {
        (void)fail("array-index-out-of-range",
                   std::format("index {} is outside the index range [{}, {}) of array `{}`", *v, lo, lo + n, shown_array),
                   "use an index within the array's declared index range");
        v = lo;  // the compile already failed: the first entry keeps every later check quiet
      }
      rebased = rebased || lo != 0 || idx.is_ref();
      idx     = Lnast_node::create_const(*v - lo);
    } else if (lo != 0) {
      auto tmp = std::format("{}{}_{}", upass::kRebasedIndexPrefix, ++inline_seq_, k);
      emit_inline_op(Lnast_ntype::create_minus(), tmp, {idx, Lnast_node::create_const(lo)});
      if (!en.empty()) {
        // An enum value's range is its encoding type's (`enum Y = (a = 4, b,
        // c)` is a U3), not its entries': keep the lane bits only, so the
        // rebased index is non-negative (an entry is always a lane).
        const auto bits = std::max<int64_t>(1, std::bit_width(static_cast<uint64_t>(n - 1)));
        auto       mask = std::format("{}m{}_{}", upass::kRebasedIndexPrefix, inline_seq_, k);
        emit_inline_op(Lnast_ntype::create_bit_and(),
                       mask,
                       {Lnast_node::create_ref(tmp), Lnast_node::create_const((int64_t{1} << bits) - 1)});
        tmp = std::move(mask);
      }
      idx     = Lnast_node::create_ref(tmp);
      rebased = true;
    }
  }
  if (!rebased) {
    return false;
  }
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-aidx");
  auto s    = std::make_shared<Lnast>(body, std::string(root_lnast_->get_top_module_name()));
  auto root = s->set_root(is_store ? Lnast_ntype::create_store() : Lnast_ntype::create_tuple_get());
  stamp_scratch_srcid(s, root);
  for (const auto& kid : kids) {
    s->add_child(root, kid);
  }
  flush_deferred_emits();
  const bool outer_physical = std::exchange(physical_indices_, true);
  lm->push_source(s, "", 0);
  process_lnast();
  flush_deferred_emits();
  lm->pop_source();
  physical_indices_ = outer_physical;
  return true;
}

bool uPass_runner::has_in_place_type_folds(const Lnast& tmpl) {
  const auto [it, fresh] = in_place_fold_cache_.try_emplace(&tmpl, false);
  if (!fresh) {
    return it->second;
  }
  for (auto n : tmpl.depth_preorder(tmpl.get_root())) {
    const auto t = tmpl.get_type(n);
    if (Lnast_ntype::is_prim_type_int(t)) {
      for (auto c : tmpl.children(n)) {
        if (Lnast_ntype::is_ref(tmpl.get_type(c))) {
          it->second = true;  // `unsigned(bits=x.[bits])`
          return true;
        }
      }
    } else if (Lnast_ntype::is_comp_type_array(t)) {
      const auto elem = tmpl.get_first_child(n);
      const auto dim  = elem.is_invalid() ? elem : tmpl.get_sibling_next(elem);
      if (!dim.is_invalid() && !(Lnast_ntype::is_const(tmpl.get_type(dim)) && upass::array_dim_lanes(tmpl.get_name(dim)))) {
        it->second = true;  // `[N]T`, `[N+1]T`
        return true;
      }
    }
  }
  return false;
}

void uPass_runner::check_array_port_dims(const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io,
                                         const absl::flat_hash_map<std::string, Generic_bind>& gbinds, std::string_view callee_name,
                                         const livehd::diag::Span& call_span) const {
  if (!callee || !callee->is_template()) {
    return;  // a concrete callee's ports were sized by upass.ssa
  }
  const auto bare  = callee_name.substr(callee_name.rfind('.') == std::string_view::npos ? 0 : callee_name.rfind('.') + 1);
  const auto check = [&](const Lnast_io_entry& e, bool output) {
    for (auto lvl = unsized_array_port(*callee, e, output);
         !lvl.is_invalid() && Lnast_ntype::is_comp_type_array(callee->get_type(lvl));
         lvl = callee->get_first_child(lvl)) {
      const auto elem = callee->get_first_child(lvl);
      const auto dim  = elem.is_invalid() ? elem : callee->get_sibling_next(elem);
      if (dim.is_invalid()) {
        return;  // an open `[]`
      }
      auto txt = callee->get_name(dim);
      if (txt.size() >= 2 && txt.front() == '[' && txt.back() == ']') {
        txt = txt.substr(1, txt.size() - 2);
      }
      if (txt.empty() && !output) {
        continue;  // [] is inferred from this call's actual, not a generic expression.
      }
      std::string unbound;
      if (const auto n = fold_template_bound(callee, txt, gbinds, 0, &unbound); !n || !n->is_just_i64() || n->to_just_i64() <= 0) {
        if (!unbound.empty()) {
          fcall_arg_fail(call_span,
                         "fcall-generic-arity",
                         std::format("generic `{}` of `{}` is unbound and has no default (it sets the shape of port `{}`)",
                                     unbound,
                                     bare,
                                     e.name),
                         std::format("bind it in the `<…>` list or declare a default `<{}=…>`", unbound));
        }
        // The specialization would harvest a width-less port (one bit).
        fcall_arg_fail(call_span,
                       "array-port-dim-not-comptime",
                       std::format("the dimension of array port `{}` of `{}` is not a compile-time constant once its "
                                   "generics are bound",
                                   e.name,
                                   bare),
                       "size a port with a literal, a `comptime const`, or an expression over the lambda's generic "
                       "parameters (`[N+1]`)",
                       "type");
      }
    }
  };
  for (const auto& e : io.inputs) {
    check(e, false);
  }
  for (const auto& e : io.outputs) {
    check(e, true);
  }
}

bool uPass_runner::check_call_array_arg(const std::shared_ptr<Lnast>& callee, const Lnast_io_entry& e,
                                        const absl::flat_hash_map<std::string, Generic_bind>& gbinds, const Lnast_node& actual,
                                        std::string_view callee_name, const livehd::diag::Span& call_span, bool probe) {
  if (lm->get_lnast()->is_verilog_origin()) {
    return true;  // the Verilog reader binds by the language's own (packed) rules
  }
  const auto shape = array_port_shape(callee, e, false, gbinds);
  if (!shape || !shape->inner_dims.empty()) {
    return true;  // multi-dimensional, or a shape the specialization diagnoses
  }
  if (shape->infer_lanes) {
    const auto fail = [&](std::string_view code, std::string_view why) {
      if (!probe) {
        fcall_arg_fail(call_span,
                       code,
                       std::format("argument `{}` in call to `{}` {}", e.name, callee_name, why),
                       "every element must fit the declared array element type",
                       "type");
      }
      return false;
    };
    const auto fits = [&](const Lnast_node& value) {
      const auto kind = actual_node_kind(value);
      if (kind == Io_kind::none && value.is_ref()) {
        if (const auto fields = try_tuple_shape(value.get_name()); fields && fields->size() > 1) {
          return fail("fcall-arg-kind", "has a tuple-valued element where a scalar is required");
        }
      }
      const auto want = shape->elem_bool ? Io_kind::boolean : Io_kind::integer;
      if (kind != Io_kind::none && kind != want) {
        return fail("fcall-arg-kind", "has an element of the wrong kind");
      }
      if (!shape->elem_bool && value_range_of(value).may_exceed(shape->elem_min, shape->elem_max)) {
        return fail("fcall-arg-overflow", "has an element outside its declared range");
      }
      return true;
    };
    if (actual.is_const()) {
      return fits(actual);  // a scalar is a one-entry tuple
    }
    const auto name = actual.get_name();
    const auto b    = symbol_table_.get_bundle(name);
    if (b && !b->get_attr("__array_size").is_invalid() && b->get_attr("__array_size").is_just_i64()) {
      const auto& flat = b->get_attr("__array_flat_size");
      if (!flat.is_invalid() && !flat.same_repr(b->get_attr("__array_size"))) {
        return fail("fcall-arg-shape", "has array-valued elements where scalar elements are required");
      }
      const auto& kind = b->get_attr("__elem_kind");
      const auto  want = shape->elem_bool ? upass::Kind::boolean : upass::Kind::integer;
      if (!kind.is_invalid() && kind.is_just_i64() && kind.to_just_i64() != static_cast<int64_t>(want)) {
        return fail("fcall-arg-kind", "has elements of the wrong kind");
      }
      const auto& mx = b->get_attr("__elem_max");
      const auto& mn = b->get_attr("__elem_min");
      if (mx.is_integer() && mn.is_integer() && !shape->elem_bool
          && (mx.gt_op(shape->elem_max)->is_known_true() || mn.lt_op(shape->elem_min)->is_known_true())) {
        return fail("fcall-arg-overflow", "has elements outside its declared range");
      }
      return true;
    }
    if (const auto* port = lm->get_lnast()->io_meta().find(name); port && port->array_size > 0) {
      if (!port->inner_dims.empty()) {
        return fail("fcall-arg-shape", "has array-valued elements where scalar elements are required");
      }
      if (port->elem_bool != shape->elem_bool) {
        return fail("fcall-arg-kind", "has elements of the wrong kind");
      }
      const auto mx = upass::max_from_bits(port->elem_bits, port->elem_signed);
      const auto mn = upass::min_from_bits(port->elem_bits, port->elem_signed);
      if (!shape->elem_bool && (mx.gt_op(shape->elem_max)->is_known_true() || mn.lt_op(shape->elem_min)->is_known_true())) {
        return fail("fcall-arg-overflow", "has elements outside its declared range");
      }
      return true;
    }
    const auto fields = try_tuple_shape(name);
    if (!fields) {
      if (b && b->get_value_kind() == upass::Kind::tuple && b->non_attr_entries().empty()) {
        return true;
      }
      return fits(actual);
    }
    const auto values = try_bundle_fields(name);
    for (const auto& [key, positional] : *fields) {
      if (!positional) {
        return fail("fcall-arg-shape", "is a named tuple rather than an array");
      }
      bool checked = false;
      if (values) {
        for (const auto& [k, v] : *values) {
          if (k.starts_with(key + ".")) {
            return fail("fcall-arg-kind", "has a tuple-valued element where a scalar is required");
          }
          if (k == key && !v.is_invalid()) {
            if (!fits(Lnast_node::create_const(v.to_pyrope()))) {
              return false;
            }
            checked = true;
            break;
          }
        }
      }
      if (!checked) {
        if (auto ref = try_tuple_slot_ref(name, key)) {
          if (!fits(Lnast_node::create_ref(*ref))) {
            return false;
          }
        }
      }
    }
    return true;
  }
  const auto declared   = std::format("[{}] with elements in [{}, {}]",
                                    shape->lanes,
                                    shape->elem_min.to_decimal_string(),
                                    shape->elem_max.to_decimal_string());
  const auto shape_fail = [&](std::string_view what) {
    if (probe) {
      return;
    }
    fcall_arg_fail(call_span,
                   "fcall-arg-shape",
                   std::format("argument `{}` in call to `{}` {}: the port is declared {}", e.name, callee_name, what, declared),
                   "pass an array of the declared length; build it lane by lane (`mut t:[N]T = 0; t[i] = …`) when the "
                   "value has another shape",
                   "type");
  };
  // What the actual is known to be: a declared array (a body `[N]T`, or an
  // array input port of the caller) or a scalar integer.
  int64_t             lanes = 0;
  std::optional<Dlop> amin;
  std::optional<Dlop> amax;
  bool                scalar = false;
  if (actual.is_const()) {
    const auto& c = Dlop::from_pyrope_cached(actual.get_name());
    scalar        = !c.is_invalid() && (c.is_integer() || c.is_bool());
  } else if (actual.is_ref()) {
    const auto name = actual.get_name();
    const auto base = name.substr(0, name.find("___ssa_"));
    const auto b    = symbol_table_.get_bundle(base);
    if (b && !b->get_attr("__array_size").is_invalid() && b->get_attr("__array_size").is_just_i64()) {
      const auto& flat = b->get_attr("__array_flat_size");
      if (!flat.is_invalid() && !flat.same_repr(b->get_attr("__array_size"))) {
        return true;  // a multi-dimensional actual binds packed rows
      }
      lanes = b->get_attr("__array_size").to_just_i64();
      if (const auto &mx = b->get_attr("__elem_max"), &mn = b->get_attr("__elem_min"); mx.is_integer() && mn.is_integer()) {
        amax = mx;
        amin = mn;
      }
    } else if (const auto* ce = lm->get_lnast()->io_meta().find(name); ce != nullptr && ce->array_size > 0) {
      if (!ce->inner_dims.empty()) {
        return true;  // a multi-dimensional actual binds packed rows (elem_signed is the leaf's sign)
      }
      lanes        = ce->array_size;
      const auto w = static_cast<uint32_t>(ce->elem_bits);
      amin         = upass::min_from_bits(w, ce->elem_signed);
      amax         = upass::max_from_bits(w, ce->elem_signed);
    } else if (const auto f = operand_decl_facts(name);
               f && f->has_type_spec
               && (f->kind == upass::decl_facts::Num::unsigned_int || f->kind == upass::decl_facts::Num::signed_int)
               && !(b && (b->has_named_top() || b->unnamed_top_count() > 1))) {
      scalar = true;
    }
  }
  if (scalar) {
    shape_fail("is not an array");
    return false;
  }
  if (lanes > 0 && lanes != shape->lanes) {
    shape_fail(std::format("has {} lane{}", lanes, lanes == 1 ? "" : "s"));
    return false;
  }
  if (amin && amax && !shape->elem_bool
      && (amax->gt_op(shape->elem_max)->is_known_true() || amin->lt_op(shape->elem_min)->is_known_true())) {
    if (probe) {
      return false;
    }
    fcall_arg_fail(call_span,
                   "fcall-arg-overflow",
                   std::format("argument `{}` (elements in [{}, {}]) in call to `{}` may not fit its declared element range "
                               "[{}, {}]",
                               e.name,
                               amin->to_decimal_string(),
                               amax->to_decimal_string(),
                               callee_name,
                               shape->elem_min.to_decimal_string(),
                               shape->elem_max.to_decimal_string()),
                   "an argument never narrows implicitly: copy it lane by lane into an array of the declared element type "
                   "first, narrowing each lane with a bit-select or a `wrap`/`sat` variable",
                   "type");
  }
  return true;
}

bool uPass_runner::try_inline_func_call() {
  // A generic body is also visited before any concrete caller exists.
  // Its local comptime expressions may still depend on unbound parameters;
  // specializing/inlining a nested call now mistakes those values for type
  // names. Preserve the call until the enclosing template is instantiated.
  if (lm->get_lnast()->is_template() && !lm->in_inline_frame()) {
    return false;
  }
  // One-shot ctor marker (see splice_init_call): true only for the
  // synthesized constructor call itself, never for calls nested inside the
  // spliced init body.
  const bool is_ctor_call = ctor_call_pending_;
  ctor_call_pending_      = false;
  const auto ctor_args    = std::move(ctor_call_args_);
  ctor_call_args_.clear();
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
  // User ruling 2026-09-28 (39): a call in this `comb` unit's own default-only
  // prologue (`b:u8 = g(a=a)`) stays a call. The comb's module never uses the
  // default (lnast.tolg skips that prologue), a caller that omits `b` inlines
  // the call from this body, and the Pyrope writer re-emits it as the
  // default's expression.
  if (!lm->in_inline_frame() && lm->get_lnast().get() == root_lnast_.get()
      && default_prologue_calls_.contains(call_nid.get_class_index().value)) {
    return false;
  }

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
  std::string       callee_name(lm->current_raw_text());  // function id — never renamed
  // The same identifier as a VARIABLE in the current scope: inside an inline
  // frame it carries the frame tag (`inl<N>_lf`). A callee spelled through a
  // value binding — a lambda-ref/import alias (`const lf = import("leaf.leaf")`,
  // re-bound at the top of every streamed body) or a gathered overload set —
  // lives under THIS name, so those folds must read it, never the raw name: the
  // raw name misses the frame-local binding (the call survives unresolved and a
  // generic callee reaches tolg unspecialized) or, worse, hits a same-named
  // binding in the CALLER's scope and inlines the wrong function. Outside a
  // frame it equals the raw name.
  const std::string callee_var(lm->current_text());

  // Higher-order resolution: inside an inlined body, `f(x)` where `f` is a
  // function-valued param resolves to the function bound at the outer call
  // site (closure_capture / fcall6). func_param_bindings_ is keyed by the
  // FRAME-TAGGED name (`inl<N>_f`), so it is read through callee_var: only the
  // frame that bound the param sees it. Keyed by the raw name, a callee
  // inlined from inside that body (an imported comb calling ITS own `f`
  // alias, or a file-local `comb f`) was hijacked by the outer param.
  bool via_param_binding = false;
  if (auto fb = func_param_bindings_.find(callee_var); fb != func_param_bindings_.end()) {
    callee_name       = fb->second;
    via_param_binding = true;
  }
  // The callee identifier as written at the call site (method dispatch and
  // the import-namespace exemption below need it after callee_name rebinds).
  const std::string source_callee_name(callee_name);

  auto callee = lookup_callee(callee_name);
  // A value binding of the callee identifier in the CURRENT scope (an import /
  // lambda-ref alias, `const lf = import("leaf.leaf")`) shadows a same-named
  // registry function found by name: inside an inline frame the bare-name
  // lookup can land on a function some OTHER file defines under the alias's
  // spelling. Only a DIFFERENT body overrides the by-name hit, so the common
  // `const bpur_vec = import("bpur_vec.bpur_vec")` keeps its raw callee name.
  if (callee && !via_param_binding) {
    if (auto fv = try_fold_ref(callee_var); fv && fv->is_string()) {
      auto fn = fv->to_pyrope();
      if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
        fn = fn.substr(1, fn.size() - 2);
      }
      if (fn.starts_with("ln:")) {
        fn = fn.substr(3);
      }
      if (auto m = lookup_callee(fn); m && m != callee) {
        callee      = m;
        callee_name = fn;
      }
    }
  }
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
    // 2f-nested_type — this used to ALSO force a Sub instance whenever any
    // output leaf had two or more dots, because "the splice ABI currently
    // materializes one level of output fields" and splicing a deeper one "would
    // leave dotted placeholder variables undriven". That limitation is gone:
    // the prologue already mints `inl<N>_<fully.dotted.leaf>` type_specs (which
    // make the detupler register a split with JOINED field names), the body's
    // per-level field writes/reads are joined back to those leaves, and the
    // epilogue regroups on the FIRST dot so a lone tuple output keeps its full
    // dotted sub-paths. A two-dot output is therefore spliced and bound exactly
    // like a one-dot one.
    //
    // The gate cost more than hierarchy: a declined splice left the result as a
    // Sub handle with no bundle, no tuple value and no split, so
    //   ctl = helper(bits=bits)   -> "whole-tuple operation cannot be scalarized"
    //   y   = t.ex.aa             -> "instance result has no output named 'ex.aa'"
    // for a callee whose shape matched the destination exactly. The cliff sat
    // precisely at the SECOND dot: one-dot tuple outputs already worked.
    return !inlining_enabled_ && c && !via_param_binding
           && reg().sub_convertible_combs.contains(std::string(c->get_top_module_name()));
  };
  bool consider_sub_instance = sub_instance_eligible(callee);

  // Call through a lambda-ref binding: `const f = b.add1` or
  // `const f = import("ln:u.g")` bind `f` to the callee's TREE NAME as a
  // string (the fcall-ref-const lambda-value form). Resolve it like the
  // bundle-field method path below.
  if (!callee) {
    // A function-valued param already rebound callee_name to the OUTER call
    // site's function (a registry name, folded as-is); otherwise the binding
    // lives in the current scope under its (frame-renamed) variable name.
    if (auto fv = try_fold_ref(via_param_binding ? callee_name : callee_var); fv && fv->is_string()) {
      auto fn = fv->to_pyrope();
      if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
        fn = fn.substr(1, fn.size() - 2);
      }
      if (fn.starts_with("ln:")) {
        fn = fn.substr(3);
      }
      if (auto m = lookup_callee(fn)) {
        callee                = m;
        callee_name           = fn;
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

  // Dotted calls have two independent candidates: a callable member, and an
  // external function declaring `self`. Generic bindings and other metadata
  // may precede the receiver, so locate its marker by name, not child index.
  const auto declares_self = [](const std::shared_ptr<Lnast>& fn) {
    return fn && !fn->io_meta().inputs.empty() && fn->io_meta().inputs.front().name == "self";
  };
  bool        explicit_receiver = false;
  bool        member_access     = false;
  std::string recv_name;
  {
    const auto here = lm->save_cursor();
    while (lm->move_to_sibling()) {
      const auto arg = lm->save_cursor();
      if (Lnast_ntype::is_store(lm->get_raw_ntype()) && lm->move_to_child() && lm->current_raw_text() == call_ufcs_arg_marker
          && lm->move_to_sibling()) {
        explicit_receiver = true;
        if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
          recv_name = std::string(lm->current_text());
        }
        break;
      }
      lm->restore_cursor(arg);
    }
    lm->restore_cursor(here);

    // Retain the legacy unmarked receiver shape for hand-built LNAST with an
    // unresolved callee; a resolved direct call never performs member lookup.
    if (!explicit_receiver && !callee && lm->move_to_sibling() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
      recv_name = std::string(lm->current_text());
    }
    lm->restore_cursor(here);
  }
  if (!recv_name.empty()) {
    const auto             external = callee;
    std::shared_ptr<Lnast> member;
    std::string            member_name;
    // Value fields take precedence over their declared type's field defaults.
    const auto             tn = try_typename(recv_name);
    for (const auto& bundle_name : {recv_name, std::string(tn)}) {
      if (bundle_name.empty() || member) {
        continue;
      }
      if (auto bf = try_bundle_fields(bundle_name)) {
        for (const auto& [fld, val] : *bf) {
          if (fld != source_callee_name || !val.is_string()) {
            continue;
          }
          auto fn = val.to_pyrope();
          if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
            fn = fn.substr(1, fn.size() - 2);
          }
          if (auto m = lookup_callee(fn)) {
            member      = m;
            member_name = fn;
          }
          break;
        }
      }
    }
    if (member) {
      if (explicit_receiver && declares_self(external)) {
        fcall_arg_fail(
            call_span,
            "fcall-ambiguous-member-ufcs",
            std::format("ambiguous dotted call `{}`: both a callable member and an external function declaring `self` exist",
                        source_callee_name),
            "rename the member or external function, or call the intended function explicitly");
      }
      callee                = member;
      callee_name           = member_name;
      member_access         = true;
      consider_sub_instance = false;
    } else {
      // An unbound template receiver has no member table yet. Defer until it
      // is instantiated rather than treating an external name as its member.
      const auto receiver = symbol_table_.get_bundle(recv_name);
      if (explicit_receiver
          && ((recv_name == "self" && lm->get_lnast()->io_meta().find("self") != nullptr)
              || (lm->get_lnast()->is_template() && (!receiver || receiver->non_attr_entries().empty())))) {
        lm->restore_cursor(saved);
        return false;
      }
    }
  }

  // A sole ordinary parameter can receive the whole positional argument
  // tuple: f(1,2,3) == f((1,2,3)) == f(x=(1,2,3)). A capture is excluded.
  // Materialize outside the signature probe, once per receiver shape, so the
  // probe and the selected call see the same real tuple (including runtime
  // values) without mutating state inside bind_call_actuals(commit=false).
  std::map<std::size_t, Actual> packed_call_tuples;
  const auto                    pack_single_tuple = [&](const Lnast_tree_io& io, std::vector<Actual>& args) {
    if (io.inputs.empty() || io.inputs.back().is_varargs || is_ctor_call) {
      return;
    }
    const std::size_t first           = io.inputs.front().name == "self" ? 1 : 0;
    // The sole parameter is ONE input, or a positional tuple port
    // `v:(_:U4, _:U8)` flattened to the leaves `v.0`, `v.1`, ... (digit-only
    // suffixes of one prefix; a named-tuple group keeps its named-argument rule).
    const auto        sole_anon_group = [&]() {
      if (io.inputs.size() < first + 2) {
        return false;
      }
      std::string prefix;
      for (std::size_t i = first; i < io.inputs.size(); ++i) {
        const auto& nm  = io.inputs[i].name;
        const auto  dot = nm.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= nm.size() || io.inputs[i].is_ref || io.inputs[i].is_varargs
            || !std::all_of(nm.begin() + static_cast<std::ptrdiff_t>(dot) + 1, nm.end(), [](char c) {
                 return c >= '0' && c <= '9';
               })) {
          return false;
        }
        const auto pre = nm.substr(0, dot);
        if (i == first) {
          prefix = pre;
        } else if (pre != prefix) {
          return false;
        }
      }
      return true;
    };
    const bool single_entry = io.inputs.size() == first + 1 && !io.inputs[first].is_ref
                              && !(io.inputs[first].kind != Io_kind::none && io.inputs[first].array_size == 0);
    if (!(single_entry || sole_anon_group()) || args.size() <= first + 1
        || std::any_of(args.begin(), args.end(), [](const Actual& a) { return a.is_named || a.is_ref_pass; })) {
      return;
    }
    auto it = packed_call_tuples.find(first);
    if (it == packed_call_tuples.end()) {
      std::vector<Lnast_node> values;
      for (std::size_t i = first; i < args.size(); ++i) {
        values.push_back(args[i].node);
      }
      const auto name = std::format("%calltuple{}", ++inline_seq_);
      emit_inline_positional_tuple(name, values);
      Actual tuple;
      tuple.node = Lnast_node::create_ref(name);
      it         = packed_call_tuples.emplace(first, std::move(tuple)).first;
    }
    args.resize(first);
    args.push_back(it->second);
  };

  // Overload-gathering dispatch (2f-overload): `const add = [f1, f2]` folds to
  // a bundle of qualified lambda-name strings under numeric keys. When the
  // callee is such a set, rewrite it to the FIRST candidate whose signature
  // accepts the call (tuple-order priority, mirroring select_init_overload);
  // the rest of this function then inlines the chosen lambda as a normal single
  // callee. A one-entry set defers to the normal bind path so its precise
  // arg-shape diagnostics still fire; a multi-entry set with no match is a
  // fatal `fcall-no-overload`.
  if (!callee) {
    auto cands = overload_candidates_of(via_param_binding ? callee_name : callee_var);
    if (!cands.empty()) {
      std::vector<Actual>         ov_actuals;
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
          bool                             whole_used         = false;
          bool                             scalar_destination = false;
          collect_return_consumption(saved, dst_name, req_fields, whole_used, &scalar_destination);
          for (const auto& fn : cands) {
            auto c                 = lookup_callee(fn);
            auto candidate_actuals = ov_actuals;
            if (c) {
              pack_single_tuple(c->io_meta(), candidate_actuals);
            }
            if (c && signature_matches(c->io_meta(), candidate_actuals, c)
                && return_matches(c->io_meta(), req_fields, whole_used, scalar_destination)) {
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

  // A tuple field is a namespace lookup, independent of external names. A
  // self-less field call drops the namespace; a self method binds the receiver.
  // Without a field, only an external function declaring self supports UFCS.
  bool drop_ufcs_receiver = false;
  if (explicit_receiver && !declares_self(callee)) {
    if (!member_access) {
      fcall_arg_fail(call_span,
                     "fcall-ufcs-no-self",
                     std::format("`{}` does not declare `self`; it cannot be called as a method", callee_name),
                     "use the direct call form `f(args...)`, or declare `self` as the first parameter");
    }
    drop_ufcs_receiver = true;
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
    if (out_tup.is_invalid()) {
      return true;
    }
    const auto first = callee->get_first_child(out_tup);
    // The parser/extractor uses a structural sentinel to distinguish an
    // explicitly empty tuple from a missing signature.  The SSA fast path
    // deliberately retains the source io tree, so recognize that sentinel
    // here just as the rebuilding path recognizes an empty harvested vector.
    return first.is_invalid()
           || (Lnast_ntype::is_ref(callee->get_type(first)) && callee->get_name(first) == "__empty_tuple"
               && callee->get_sibling_next(first).is_invalid());
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
  // A concrete `mod`/`pipe` reused as-is -- PRE-ELABORATED from an `ln:` dir,
  // or restored from the compile cache with its graph -- has no body to splice
  // either, but it is always a Sub instance with a restored io_meta: it goes on
  // to bind its actuals, so the call-site checks (argument fit, the typed /
  // derived output range and bool kind its instance handle carries) run
  // exactly as for a source callee, then it declines below.
  const auto reused_instance = [&]() {
    const auto  kind = callee->get_lambda_kind();
    const auto& cio  = callee->io_meta();
    return (callee->is_pre_elaborated() || callee->is_graph_restored()) && !callee->is_template()
           && (kind == "mod" || kind == "pipe") && (cio.inputs.empty() || cio.inputs[0].name != "self");
  };
  if (!reg().inlinable_callees.contains(std::string(callee->get_top_module_name())) && !reused_instance()) {
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

  std::vector<Actual>         actuals;
  std::vector<Generic_actual> explicit_generics;  // `f<int,string>(…)` binds, in order (named or positional)
  if (!gather_actuals(drop_ufcs_receiver, actuals, explicit_generics)) {
    return false;
  }
  pack_single_tuple(callee->io_meta(), actuals);
  // The synthesized ctor call's refs carry the caller's renamed text: take
  // each argument's source spelling from its Ctor_arg (actuals[0] is the
  // receiver). A construction VALUE (`mut x:T = (..)`) carries none.
  if (is_ctor_call) {
    for (std::size_t i = 1; i < actuals.size(); ++i) {
      const bool known    = i - 1 < ctor_args.size();
      actuals[i].src_name = known ? ctor_args[i - 1].src_name : std::string{};
      actuals[i].src_base = known ? ctor_args[i - 1].src_base : std::string{};
    }
  }
  lm->restore_cursor(saved);  // back on the func_call node (gather left it on the callee ref)
  const std::string call_synth     = gathered_synth_call_;
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
  // tuple_actual_expanded: this COMMIT bind expanded a tuple actual into
  // flattened `<prefix>.<leaf>` params (the probe's expansion in
  // signature_matches is a separate, discarded bind). A Sub-bound callee must
  // then re-emit the call with the dotted NAMED binding below.
  bool                                            tuple_actual_expanded = false;
  bind_minted_ok_ = callee->get_lambda_kind() == "mod" || callee->get_lambda_kind() == "pipe";
  bind_minted_actuals_.clear();
  bind_call_actuals(io,
                    actuals,
                    /*commit=*/true,
                    callee_name,
                    call_span,
                    param_val,
                    param_set,
                    param_func,
                    vararg_pos,
                    vararg_named,
                    &tuple_actual_expanded);
  bind_minted_ok_           = false;
  // A named actual of the child's minted clock/reset: carried into a re-emitted
  // Sub call below (an all-named call reaches tolg verbatim with it).
  const auto minted_actuals = std::move(bind_minted_actuals_);
  bind_minted_actuals_.clear();
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
  check_call_args_fit(callee, io, param_val, param_set, nbind, gbinds, callee_name, call_span);
  check_array_port_dims(callee, io, gbinds, callee_name, call_span);

  // `in_identity_respecialize_`: this IS the call an identity specialization
  // just emitted, coming back through emit_named_instance_call's re-walk. The
  // clone it names carries the template's own name, so re-entering here would
  // specialize it again, forever. Decline instead and take the ordinary
  // mod/pipe Sub-instance path below — which is what the clone now is.
  if (callee->is_template() && !in_identity_respecialize_) {
    const auto k         = callee->get_lambda_kind();
    const bool is_method = !io.inputs.empty() && io.inputs[0].name == "self";
    if ((k == "mod" || k == "pipe" || k == "fluid") && !is_method) {
      // Template mod/pipe/fluid specialization returns true unconditionally, so
      // the general missing-arg check below (3527) is unreachable for these
      // callees. Run the same check here so an omitted required arg errors
      // instead of being silently pushed as an invalid actual (is_method==false
      // here, so there is no self slot to skip).
      for (std::size_t i = 0; i < nbind; ++i) {
        if (!param_set[i] && !sub_input_may_be_omitted(callee, io.inputs[i])) {
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
    const bool is_pipe
        = std::any_of(io.outputs.begin(), io.outputs.end(), [](const Lnast_io_entry& oe) { return oe.stages_min > 0; });
    const bool becomes_sub = is_pipe || (callee->get_lambda_kind() == "mod" && !has_self);
    const bool any_unnamed = std::any_of(actuals.begin(), actuals.end(), [](const Actual& a) { return !a.is_named; });
    // A tuple actual (even a NAMED one, `req=t`) was expanded field-by-field
    // into the flattened `req.a`/`req.b` leaf params — a binding the source
    // spelling cannot express for tolg (the store names `req`, which is not a
    // port). Materialize it as dotted named actuals unconditionally.
    if (becomes_sub && (any_unnamed || tuple_actual_expanded)) {
      std::vector<std::pair<std::string, Lnast_node>> named;
      named.reserve(nbind);
      for (std::size_t i = 0; i < nbind; ++i) {  // becomes_sub ⇒ no self slot
        if (param_set[i]) {
          named.emplace_back(io.inputs[i].name, param_val[i]);
        }
      }
      named.insert(named.end(), minted_actuals.begin(), minted_actuals.end());
      if (!call_synth.empty()) {
        named.emplace_back("__synth_call", Lnast_node::create_const(call_synth));
      }
      emit_named_instance_call(dst_name, frame_portable_callee_name(callee_name, callee), call_inst_name, named);
      return true;
    }
  }

  if (!call_synth.empty()) {
    stash_sub_instance_port_facts(dst_name, callee);
    return false;
  }

  // A `pipe` callee (any output carries a stages annotation) is
  // never comb-inlined: its outputs are flopped, and a call site must consume
  // it via `stage[N]` (later phase). Decline so the call surfaces unresolved
  // instead of silently dropping the latency.
  for (const auto& oe : io.outputs) {
    if (oe.stages_min > 0) {
      stash_sub_instance_port_facts(dst_name, callee);  // becomes a Sub instance
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
      stash_sub_instance_port_facts(dst_name, callee);  // becomes a Sub instance
      return false;
    }
  }

  // Every non-self FIXED parameter must be bound — an unset input means the
  // caller omitted a required argument. EXCEPT a param with a declared default
  // (`comb f(in1:u4, in2=3)`, todo 3g E): an omitted default takes the
  // body-prologue value (bound below). And a `comb` input the body never reads
  // (user ruling 2026-09-28 (30)): nothing is wired into it, whatever its name
  // -- a comb holds no state, so its `clk`/`rst` are plain data and never
  // auto-wired. The var-arg slot (index nbind, when present) is always
  // satisfied — it gathers zero or more leftovers.
  for (std::size_t i = (has_self ? 1 : 0); i < nbind; ++i) {
    if (!param_set[i] && !io.inputs[i].has_default) {
      if (upass::io_port::comb_port_is_dead(*callee, io.inputs[i])) {
        continue;
      }
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
    // Same re-emit as the mod/pipe block above, deferred to HERE because a comb
    // only becomes a Sub once the runtime-actual check settles it (an
    // all-const call keeps the splice path so it folds to a comptime value —
    // that path consumes the tuple expansion natively). A tuple actual was
    // expanded into the flattened leaf params; the source spelling (a store
    // naming the tuple, or a positional ref to a detupled variable) is not
    // wireable by tolg, so materialize the resolved binding as dotted NAMED
    // actuals. `!has_self`: a method comb is not a standalone Sub — keep the
    // plain decline for it (pre-existing behavior).
    // Also re-emit when the source used any unnamed actual (a same-name
    // shorthand `f(b)` or a type-distinguished one): the runner resolved it by
    // NAME/KIND, but tolg binds an unnamed actual to the next declared port, so
    // `f(a=x, b)` bound `b` to `a` ("binds input more than once") and `f(b, a)`
    // silently swapped the ports. Calls never bind by position
    // (06-functions.md §"Argument naming"). A `ref` actual keeps the decline.
    const bool any_unnamed = std::any_of(actuals.begin(), actuals.end(), [](const Actual& a) { return !a.is_named; });
    const bool any_ref     = std::any_of(actuals.begin(), actuals.end(), [](const Actual& a) { return a.is_ref_pass; });
    // Also re-emit inside an inline frame (a call in a spliced generic/closure
    // body): the plain decline copies the func_call through the frame rename,
    // which tags the CALLEE ref too (`inc` -> `inl1_inc`, or a generic lambda
    // `F` -> `inl1_F`), naming a function tolg cannot find. The re-emit spells
    // the resolved callee (frame_portable_callee_name).
    if ((tuple_actual_expanded || (any_unnamed && !any_ref) || (lm->in_inline_frame() && !any_ref)) && !has_self) {
      std::vector<std::pair<std::string, Lnast_node>> named;
      named.reserve(nbind);
      for (std::size_t i = 0; i < nbind; ++i) {
        if (param_set[i]) {
          named.emplace_back(io.inputs[i].name, param_val[i]);
        }
      }
      named.insert(named.end(), minted_actuals.begin(), minted_actuals.end());
      if (!call_synth.empty()) {
        named.emplace_back("__synth_call", Lnast_node::create_const(call_synth));
      }
      emit_named_instance_call(dst_name, frame_portable_callee_name(callee_name, callee), call_inst_name, named);
      return true;
    }
    lm->restore_cursor(saved);
    stash_sub_instance_port_facts(dst_name, callee);  // becomes a Sub instance
    return false;
  }

  // ── Splice ───────────────────────────────────────────────────────────────
  const uint32_t salt = ++inline_seq_;
  if (const auto* unit = lm->unit_lnast().get(); unit != inline_tags_unit_) {
    inline_tags_unit_ = unit;
    inline_tags_taken_.clear();
    for (const auto& node : unit->depth_preorder(unit->get_root())) {
      const auto n = Lnast_nid(node);
      if (!Lnast_ntype::is_ref(unit->get_type(n))) {
        continue;
      }
      const auto name = unit->get_name(n);
      uint32_t   k    = 0;
      if (name.size() > 4 && name.starts_with("inl")) {
        const auto [p, ec] = std::from_chars(name.data() + 3, name.data() + name.size(), k);
        if (ec == std::errc{} && p != name.data() + 3 && p < name.data() + name.size() && *p == '_') {
          inline_tags_taken_.insert(k);
        }
      }
    }
  }
  uint32_t tag_n = ++inline_tag_seq_;  // names hardware: see inline_tag_seq_
  while (inline_tags_taken_.contains(tag_n)) {
    tag_n = ++inline_tag_seq_;
  }
  const std::string tag = std::format("inl{}_", tag_n);

  // Generic-WIDTH ports (`a:unsigned(bits=N * 4)`): the width is the bound
  // folded under this call's binds, never the actual's declared type — the same
  // rule maybe_specialize_template_call applies to a mod. Folded once here: the
  // prologue types the inlined param/output from it, and the private inline
  // body below gets the same concrete io leaves, so a later scan of that tree
  // (the loop roller's declaration lookup) never meets the template's
  // `prim_type_int(ref %tmp, …)`.
  std::vector<Spec_port> inject(nparams);
  std::vector<Spec_port> out_inject(io.outputs.size());
  for (std::size_t i = 0; i < nparams; ++i) {
    if (io.inputs[i].has_deferred_bound()) {
      inject[i] = deferred_port_type(callee, io.inputs[i], gbinds, callee_name, call_span);
    }
  }
  for (std::size_t i = 0; i < io.outputs.size(); ++i) {
    if (io.outputs[i].has_deferred_bound()) {
      out_inject[i] = deferred_port_type(callee, io.outputs[i], gbinds, callee_name, call_span);
    }
  }

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
      // Keyed by the frame-tagged name (see try_inline_func_call): a nested
      // frame tags its own `f` differently, so the binding never leaks there.
      const auto fname = upass::Lnast_manager::make_inlined_name(tag, e.name);
      auto       it    = func_param_bindings_.find(fname);
      saved_func_bindings.emplace_back(fname,
                                       it == func_param_bindings_.end() ? std::nullopt : std::optional<std::string>(it->second));
      func_param_bindings_[fname] = frame_portable_func_name(param_func[i], callee->get_top_module_name());
      continue;  // function value — no width/value binding
    }
    const auto pname = upass::Lnast_manager::make_inlined_name(tag, e.name);
    const auto gbit  = e.type_name.empty() ? gbinds.end() : gbinds.find(e.type_name);
    if (is_array_port(*callee, e, false)) {
      // `v:[4]u8`, `v:[N]unsigned(bits=N)`: the declared array, never its
      // packed width nor its element's bound (a multi-dimensional unsized one
      // takes the actual's shape).
      if (const auto shape = array_port_shape(callee, e, false, gbinds, param_set[i] ? &param_val[i] : nullptr);
          shape && shape->lanes > 0) {
        emit_inline_declare_array(pname, *shape);
      }
    } else if (e.kind == Io_kind::boolean) {
      emit_inline_typespec_bool(pname);
    } else if (e.bits > 0) {
      emit_inline_typespec(pname, e.bits, e.is_signed);
    } else if (inject[i].inject) {
      emit_inline_typespec_range(pname, inject[i].max, inject[i].min);  // folded generic-width bound
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
  //   * a LAMBDA  (`f<inc>`): the body calls it (`F(v)`) — register the
  //     frame-tagged name in func_param_bindings_ so the call dispatches (same
  //     seam, and same frame scoping, as a function-valued param).
  //   * a TYPE    (`f<u8>`): the body uses it as a `:T` type slot (declare refs
  //     renamed to `inlN_T`, bound via the named-type machinery) AND/OR as a
  //     constructor cast (`T(a)` → generic_cast_binds_ so try_lower_typecast
  //     reclassifies it against the concrete token).
  for (const auto& [g, gb] : gbinds) {
    if (!gb.func_name.empty()) {
      const auto gname = upass::Lnast_manager::make_inlined_name(tag, g);
      saved_func_bindings.emplace_back(
          gname,
          func_param_bindings_.count(gname) != 0u ? std::optional<std::string>(func_param_bindings_[gname]) : std::nullopt);
      func_param_bindings_[gname] = frame_portable_func_name(gb.func_name, callee->get_top_module_name());
      continue;
    }
    if (!gb.const_text.empty()) {
      const auto gname = upass::Lnast_manager::make_inlined_name(tag, g);
      if (gb.decl_typed) {
        emit_inline_typespec_range(gname, gb.decl_max, gb.decl_min);  // the bound constant's declared type
      }
      emit_inline_binding(gname, Lnast_node::create_const(gb.const_text));
      continue;
    }
    if (gb.type_name.empty() && gb.kind == Io_kind::boolean) {
      emit_inline_typespec_bool(upass::Lnast_manager::make_inlined_name(tag, g));
    } else if (gb.type_name.empty() && (gb.max || gb.min)) {
      emit_inline_typespec_range(upass::Lnast_manager::make_inlined_name(tag, g), gb.max, gb.min);
    }
    // Constructor-cast token for a body `T(a)`. A named type is spelled
    // verbatim; an integer/bool envelope maps back to its scalar token.
    const std::string cast_token = generic_cast_token(gb);
    if (!cast_token.empty()) {
      // Frame-tagged key, like func_param_bindings_: a nested frame's own `T`
      // is tagged differently and never reads this frame's bind.
      const auto gname = upass::Lnast_manager::make_inlined_name(tag, g);
      saved_cast_binds.emplace_back(
          gname,
          generic_cast_binds_.count(gname) != 0u ? std::optional<std::string>(generic_cast_binds_[gname]) : std::nullopt);
      generic_cast_binds_[gname] = cast_token;
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
  for (std::size_t oi = 0; oi < io.outputs.size(); ++oi) {
    const auto& o = io.outputs[oi];
    if (input_names.contains(o.name)) {
      continue;  // shared input/output name: input binding stands (see above)
    }
    const auto oname = upass::Lnast_manager::make_inlined_name(tag, o.name);
    inline_output_names_.emplace(oname);  // 1i-inline: this binding is a hardware driver (see the set's decl)
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
    const bool array_out = is_array_port(*callee, o, true);
    if (o.kind == Io_kind::boolean) {
      emit_inline_typespec_bool(oname);
    } else if (array_out) {
      // `-> (r:[2]u8)`, `-> (r:[N]unsigned(bits=N))`: an array, not its packed
      // width nor its element's bound.
      if (const auto shape = array_port_shape(callee, o, true, gbinds)) {
        emit_inline_declare_array(oname, *shape);
      }
    } else if (out_inject[oi].inject) {
      emit_inline_typespec_range(oname, out_inject[oi].max, out_inject[oi].min);  // folded generic-width bound
    } else {
      emit_inline_typespec(oname, o.bits, o.is_signed);
    }
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
    inline_level      = (!is_tmp && !d.empty()) ? d : ("u_" + callee_name + "_" + std::to_string(tag_n));
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
  // A defaulted input (todo 3g E): the body opens with a prologue store of the
  // default into Lnast_io_entry::default_value_name(), never into the port. A
  // PROVIDED arg skips that store (the actual bound in the param loop wins); an
  // OMITTED one walks it and then binds the param from it, before any body read.
  absl::flat_hash_map<std::string, std::size_t> default_stores;  // default-value local -> input index
  for (std::size_t i = 0; i < nbind && i < io.inputs.size(); ++i) {
    if (io.inputs[i].has_default) {
      default_stores.emplace(Lnast_io_entry::default_value_name(io.inputs[i].name), i);
    }
  }
  // The callee's TUPLE ports live in its io_meta as flattened leaves
  // (`inst_ctrl.legal`), but its body still talks to the whole port: a field
  // `store(inst_ctrl,'legal',v)` and a `tuple_get(t,inst_ctrl,'legal')`. When
  // the callee runs as its own unit the streaming ABI rewrite
  // (try_stream_tuple_port_{store,alias_store} / try_resolve_tuple_get) turns
  // those into the dotted leaf; upass_ssa therefore leaves them alone
  // (`streamable`). Splicing the body here must do the same — otherwise tolg is
  // handed a multi-element store it cannot lower ("tuple/field store to
  // 'inl1_inst_ctrl' has no hardware lowering"), which is what happens whenever
  // the registry hands back a callee body that has not already been rewritten
  // (an IMPORTED comb: it is staged by a different pass.upass invocation).
  // Register the frame-tagged leaves for this body walk only.
  std::vector<std::string> frame_port_leaves;    // leaf names this frame added
  std::vector<std::string> frame_port_prefixes;  // their proper prefixes
  {
    const auto add_leaf = [&](const std::string& raw_name, bool is_input) {
      if (raw_name.find('.') == std::string::npos) {
        return;  // scalar port — no field store/get to rewrite
      }
      const auto tagged = upass::Lnast_manager::make_inlined_name(tag, raw_name);
      auto&      leaves = is_input ? stream_port_in_leaf_ : stream_port_out_leaf_;
      if (leaves.insert(tagged).second) {
        frame_port_leaves.push_back(tagged);
      }
      for (auto pos = tagged.find('.'); pos != std::string::npos; pos = tagged.find('.', pos + 1)) {
        auto prefix = tagged.substr(0, pos);
        if (stream_port_prefix_.insert(prefix).second) {
          frame_port_prefixes.push_back(std::move(prefix));
        }
      }
    };
    for (const auto& e : io.inputs) {
      add_leaf(e.name, /*is_input=*/true);
    }
    for (const auto& e : io.outputs) {
      add_leaf(e.name, /*is_input=*/false);
    }
  }
  // Baking dependent declaration bounds rewrites their nodes in place.
  // A generic comb can be inlined with several widths, and so can an untyped
  // one (`mut t:unsigned(bits=x.[bits])` sized by each call's actual):
  // specialize a private body so one call cannot freeze the shared template's
  // type/array bounds.
  const auto inline_source
      = gbinds.empty() && !has_in_place_type_folds(*callee)
            ? callee
            : clone_template_specialized(callee, std::string(callee->get_top_module_name()), inject, {}, {}, out_inject, gbinds);
  // A call evaluated at compile time (its result feeds a `comptime`
  // declaration) cannot roll its loops: a rolled body is runtime hardware, so
  // `comptime const INIT = rows(size=4)` would lose its value.
  const bool saved_unroll = unroll_requested_;
  if (feeds_comptime_decl(*lm->get_lnast(), call_nid, dst_raw_name)) {
    unroll_requested_ = true;
  }
  lm->push_source(inline_source, tag, salt);
  if (lm->move_to_child()) {
    if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_io) {
      lm->move_to_sibling();
    }
    // A PROVIDED arg's default is not taken: the statements that only compute
    // it (its side effects included, e.g. an assert of an inlined checker in
    // the default expression) are skipped with its store.
    absl::flat_hash_set<int64_t> unused_default;
    if (!default_stores.empty() && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_stmts) {
      absl::flat_hash_set<std::string> provided;
      absl::flat_hash_set<std::string> omitted;
      for (const auto& [local, idx] : default_stores) {
        (param_set[idx] ? provided : omitted).insert(local);
      }
      unused_default = upass::unused_default_prologue(*inline_source, lm->get_current_nid(), provided, omitted);
    }
    if (lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_stmts && lm->move_to_child()) {
      do {
        if (!unused_default.empty() && unused_default.contains(lm->get_current_nid().get_class_index().value)) {
          continue;
        }
        if (!default_stores.empty() && lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_store) {
          const auto                 sc = lm->save_cursor();
          std::optional<std::size_t> dflt;
          if (lm->move_to_child() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
            if (auto it = default_stores.find(lm->current_raw_text()); it != default_stores.end()) {
              dflt = it->second;
              default_stores.erase(it);
            }
          }
          lm->restore_cursor(sc);
          if (dflt) {
            if (param_set[*dflt]) {
              continue;  // provided arg wins over its default prologue store
            }
            process_lnast();
            const auto& pname = io.inputs[*dflt].name;
            emit_inline_binding(
                upass::Lnast_manager::make_inlined_name(tag, pname),
                Lnast_node::create_ref(upass::Lnast_manager::make_inlined_name(tag, Lnast_io_entry::default_value_name(pname))));
            continue;
          }
        }
        process_lnast();
      } while (lm->move_to_sibling());
    }
  }
  flush_deferred_emits();  // flush callee-tree parked writes before leaving
  lm->pop_source();
  unroll_requested_ = saved_unroll;
  inline_call_sites_.pop_back();
  active_inline_callees_.pop_back();
  hier_prefix_stack_.pop_back();
  // Un-register this frame's callee-port ABI (only the names it actually added,
  // so a nested frame that shares a prefix keeps its own).
  for (const auto& leaf : frame_port_leaves) {
    stream_port_in_leaf_.erase(leaf);
    stream_port_out_leaf_.erase(leaf);
  }
  for (const auto& prefix : frame_port_prefixes) {
    stream_port_prefix_.erase(prefix);
  }
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

  // A destructure slot that matches no output reads an absent field of this
  // temp (upass.constprop `unknown-field`): let that diagnostic name the call.
  if (Lnast::is_tmp(dst_name)) {
    symbol_table_.call_result_callee.insert_or_assign(dst_name, source_callee_name);
  }

  // Epilogue: map the callee outputs back to the caller's dst, then apply
  // ref-param write-backs.
  //   - single output  → `dst = <tag>out` (scalar or whole-bundle copy)
  //   - multiple outputs → `dst = (out0=<tag>out0, …)` so the caller's
  //     destructuring tuple_gets (`dst.out0`) fold (see fcall5.prp).
  // A signed output is reinterpreted to its declared width first (mirrors the
  // deleted evaluator's adjust_for_type): a bit-slice like `res:s4 = b#[0..<4]`
  // yields the raw bits 0b1110 = 14, which as s4 must read back as -2.
  // A generic-width output's signed width comes from its folded bound.
  auto output_ref = [&](std::size_t oi) -> Lnast_node {
    const auto& o         = io.outputs[oi];
    const auto& sp        = out_inject[oi];
    int         sign_bits = o.is_signed ? o.bits : 0;
    if (sp.inject && sp.max && sp.min && sp.min->is_negative()) {
      sign_bits = static_cast<int>(std::max(sp.max->get_signed_bits(), sp.min->get_signed_bits()));
    }
    const auto raw = upass::Lnast_manager::make_inlined_name(tag, o.name);
    if (sign_bits > 0) {
      const auto sx = upass::Lnast_manager::make_inlined_name(tag, o.name + "_sx");
      emit_inline_sext(sx, raw, sign_bits - 1);
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
  std::vector<std::string>                                                          logical_order;
  absl::flat_hash_map<std::string, std::vector<std::pair<std::string, Lnast_node>>> logical;
  for (std::size_t oi = 0; oi < io.outputs.size(); ++oi) {
    const auto& o     = io.outputs[oi];
    const auto  dp    = o.name.find('.');
    std::string lname = dp == std::string::npos ? o.name : o.name.substr(0, dp);
    std::string sub   = dp == std::string::npos ? std::string{} : o.name.substr(dp + 1);
    if (!logical.contains(lname)) {
      logical_order.push_back(lname);
    }
    logical[lname].emplace_back(std::move(sub), output_ref(oi));
  }
  bool named_result = false;
  if (logical_order.size() == 1) {
    absl::flat_hash_set<std::string> consumed_fields;
    bool                             whole_result_used = false;
    named_result = collect_return_consumption(saved, dst_name, consumed_fields, whole_result_used, nullptr, logical_order.front());
  }
  if (logical_order.empty()) {
    // Void comb (e.g. `top()` called for its casserts) — nothing to bind back.
  } else if (logical_order.size() == 1 && !named_result) {
    auto& leaves = logical[logical_order[0]];
    if (leaves.size() == 1 && leaves[0].first.empty()) {
      emit_inline_binding(dst_name, leaves[0].second);  // single scalar output
    } else {
      emit_inline_tuple(dst_name, leaves);  // single TUPLE output → dst IS the tuple
    }
  } else {
    // Multiple logical outputs form a named bundle, available both to a
    // whole-result binding and to a destructuring field pick.
    std::vector<std::pair<std::string, Lnast_node>>  fields;
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

void uPass_runner::initialize_stream_port_abi() {
  stream_port_in_leaf_.clear();
  stream_port_out_leaf_.clear();
  io_output_names_.clear();
  stream_port_prefix_.clear();
  stream_port_alias_.clear();

  auto register_leaf = [&](std::string_view name, bool is_input) {
    if (name.find('.') == std::string_view::npos) {
      return;
    }
    (is_input ? stream_port_in_leaf_ : stream_port_out_leaf_).emplace(name);
    for (auto pos = name.find('.'); pos != std::string_view::npos; pos = name.find('.', pos + 1)) {
      stream_port_prefix_.emplace(name.substr(0, pos));
    }
  };
  const auto& io = root_lnast_->io_meta();
  for (const auto& input : io.inputs) {
    register_leaf(input.name, true);
  }
  for (const auto& output : io.outputs) {
    io_output_names_.emplace(output.name);
    register_leaf(output.name, false);
  }
  default_value_names_.clear();
  for (const auto& input : io.inputs) {
    if (input.has_default) {
      default_value_names_.emplace(Lnast_io_entry::default_value_name(input.name));
    }
  }
  default_prologue_calls_.clear();
  if (root_lnast_->get_lambda_kind() == "comb" && !default_value_names_.empty()) {
    for (auto top : root_lnast_->children(root_lnast_->get_root())) {
      if (!Lnast_ntype::is_stmts(root_lnast_->get_type(top))) {
        continue;
      }
      const auto prologue = upass::unused_default_prologue(*root_lnast_, top, default_value_names_, {});
      for (auto stmt : root_lnast_->children(top)) {
        const auto idx = stmt.get_class_index().value;
        if (prologue.contains(idx) && Lnast_ntype::is_func_call(root_lnast_->get_type(stmt))) {
          default_prologue_calls_.insert(idx);
        }
      }
      break;
    }
  }
}

void uPass_runner::note_stream_ssa_definition() {
  if (!stream_ssa_enabled_ || lm->get_lnast().get() != root_lnast_.get() || !lm->has_child()) {
    return;
  }
  if (Lnast_ntype::is_store(lm->get_raw_ntype()) && lm->current_num_children() > 2) {
    return;  // tuple/array mutation keeps one aggregate identity
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return;
  }
  std::string source(lm->current_text());
  lm->restore_cursor(saved);
  if (Lnast::is_tmp(source) || stream_ssa_state_names_.contains(source) || !root_lnast_->stream_ssa_names().contains(source)) {
    return;
  }

  const uint64_t node_id = lm->get_current_nid().get_class_index().value;
  if (stream_ssa_defs_.contains(node_id)) {
    return;  // deferred re-emission of the same source statement
  }
  auto [current_it, first] = stream_ssa_current_.try_emplace(source, source);
  Stream_ssa_def def;
  def.source   = source;
  def.previous = current_it->second;
  if (first) {
    def.output = source;
    stream_ssa_count_.try_emplace(source, 0);
  } else {
    int next           = ++stream_ssa_count_[source];
    def.output         = std::format("{}___ssa_{}", source, next);
    current_it->second = def.output;
  }
  stream_ssa_defs_.emplace(node_id, std::move(def));
}

std::string uPass_runner::stream_ssa_ref_name(std::string_view name) const {
  if (!stream_ssa_enabled_) {
    return std::string(name);
  }
  if (stream_ssa_active_def_.has_value() && stream_ssa_active_def_->source == name) {
    return stream_ssa_active_def_->previous;  // `x = f(x)` reads the prior version
  }
  if (const auto it = stream_ssa_current_.find(name); it != stream_ssa_current_.end()) {
    return it->second;
  }
  return std::string(name);
}

std::optional<std::string> uPass_runner::resolve_stream_port_path(std::string_view name) const {
  if (stream_port_prefix_.contains(name)) {
    return std::string(name);
  }
  if (const auto it = stream_port_alias_.find(name); it != stream_port_alias_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool uPass_runner::try_stream_tuple_port_alias_store() {
  // A branch-local copy is a real mux arm, not a disposable carrier alias.
  // Recording `result_tmp -> ar.x` in the runner-global alias map would drop
  // the store itself; a later arm would overwrite that map and the unique-if
  // would lose every simple field-valued arm.  Keep the store under runtime
  // control so lower_branch can merge the same destination across paths.
  if (symbol_table_.in_uncertain_scope()) {
    return false;
  }
  if (lm->current_num_children() != 2 || !lm->has_child()) {
    return false;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return false;
  }
  std::string dst(lm->current_text());
  if (!Lnast::is_tmp(dst) || !lm->move_to_sibling() || !Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return false;
  }
  auto source = resolve_stream_port_path(lm->current_text());
  lm->restore_cursor(saved);
  // Only an INTERIOR port path is a disposable carrier (a later field pick
  // resolves through the alias). A copy of a LEAF (`%c = %t` over `%t =
  // req.addr`, e.g. a cast's value-preserving bind) is a scalar value that
  // plain reads use: keep the store.
  if (!source || stream_port_in_leaf_.contains(*source) || stream_port_out_leaf_.contains(*source)) {
    return false;
  }
  stream_port_alias_.insert_or_assign(std::move(dst), std::move(*source));
  return true;
}

// Under control flow, the comptime fields a whole-tuple store's destination
// holds before the store (nullopt: no tuple value yet, e.g. an if-expression's
// result temp). split_runtime_tuple_store merges against them.
std::optional<std::vector<std::string>> uPass_runner::runtime_tuple_prior_fields() const {
  if (lm->current_num_children() != 2 || !lm->has_child() || !symbol_table_.in_uncertain_scope()) {
    return std::nullopt;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  const bool        is_ref = Lnast_ntype::is_ref(lm->get_raw_ntype());
  const std::string lhs(lm->current_text());
  lm->restore_cursor(saved);
  const auto b = is_ref ? symbol_table_.get_bundle(lhs) : nullptr;
  if (!b || !(b->has_named_top() || b->unnamed_top_count() > 1)) {
    return std::nullopt;
  }
  std::vector<std::string> fields;
  for (const auto& [key, e] : b->non_attr_entries()) {
    if (e.trivial.is_numeric()) {
      fields.push_back(key);
    }
  }
  return fields;
}

// A runtime tuple VALUE (fixed shape, runtime fields: a tuple literal over
// runtime values, a multi-output call result) lives only as per-slot refs
// (Symbol_table::tuple_slot_ref) that name whichever temps produced the fields.
// A whole write under an `if` (`if s { d = (a=…, b=…) }`, or an if-expression's
// arms writing the same temp) re-pointed those refs at ONE arm's temps, and the
// field reads after the `if` forwarded that arm's value on every path (the
// other path read an undriven X). Give such a variable one scalar LEAF per
// field (`d.a = %t`, and `d.b = 5` for a comptime field) and point its slot
// refs at the leaves: every write of the value then writes the same leaf
// names, which tolg merges into a mux per field exactly like hand-written
// per-field writes. Needed for a write under control flow, and for a `mut` (a
// later arm may rewrite it, and the arm merge needs the value before the `if`
// on the same leaf names -- a comptime initializer included). Outside control
// flow a comptime field keeps folding from the binding (its declared type and
// attributes ride there); only its leaf store is emitted, as the pre-`if`
// value of a later conditional write. A POSITIONAL comptime field gets no leaf
// (`t.0` also names the binding's own scalar slot, so a leaf store there would
// retype a comptime tuple: `mut acc = (0, 1); acc ++= …`), and neither does a
// field that is no scalar (a nested tuple, a string, a function). A write
// under control flow that would have to merge such a field is refused rather
// than left to read an undriven X on its other path.
void uPass_runner::split_runtime_tuple_store(const std::optional<std::vector<std::string>>& prior) {
  if (lm->current_num_children() != 2 || !lm->has_child()) {
    return;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  const bool        is_ref = Lnast_ntype::is_ref(lm->get_raw_ntype());
  const std::string lhs(lm->current_text());
  lm->restore_cursor(saved);
  if (!is_ref || lhs.find('.') != std::string::npos) {
    return;
  }
  const bool uncertain = symbol_table_.in_uncertain_scope();
  if (!uncertain) {
    const auto f = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), lhs);
    if (!f || f->mode != upass::Mode::mut_kind) {
      return;
    }
  }
  // Snapshot: each emitted store re-enters constprop, which edits the map.
  std::map<std::string, std::string> slots;
  if (const auto it = symbol_table_.tuple_slot_ref.find(lhs); it != symbol_table_.tuple_slot_ref.end()) {
    slots = it->second;
  }
  std::map<std::string, Lnast_node> consts;
  std::string                       unsplit;  // a field that gets no leaf
  // A tuple by SHAPE: a one-field `(a=1)` is one too (is_scalar() says
  // otherwise for any single-entry bundle).
  if (const auto b = symbol_table_.get_bundle(lhs); b && (b->has_named_top() || b->unnamed_top_count() > 1)
                                                    && b->get_attr("__array_size").is_invalid()
                                                    && b->get_attr("__elem_max").is_invalid()) {
    for (const auto& [key, e] : b->non_attr_entries()) {
      if (slots.contains(key)) {
        continue;
      }
      const bool named = !key.empty() && (key.front() < '0' || key.front() > '9');
      if (named && e.trivial.is_numeric()) {
        consts.emplace(key, Lnast_node::create_const(e.trivial.to_pyrope()));
      } else if (unsplit.empty()) {
        unsplit = key;
      }
    }
    if (!unsplit.empty()) {
      consts.clear();
    }
  }
  // Leaves are named after the SSA BASE: every version of the value (a second
  // whole write outside control flow is `d___ssa_1`) writes the same leaves,
  // so an arm merge reads the latest one as its pre-`if` value.
  const std::string base(std::string_view(lhs).substr(0, lhs.find("___ssa_")));
  if (prior) {
    // By value: the emits below re-enter the passes, which may touch the map.
    absl::flat_hash_set<std::string> have;
    if (const auto it = split_tuple_leaves_.find(base); it != split_tuple_leaves_.end()) {
      have = it->second;
    }
    std::string missing = unsplit;
    for (const auto& key : *prior) {
      if (missing.empty() && !have.contains(key)) {
        missing = key;
      }
    }
    if (!missing.empty()) {
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "tuple-cond-write-unsplit",
          .category = "unsupported",
          .pass     = "upass",
          .message  = std::format("conditional write of tuple `{}`: field `{}` cannot be merged across the `if` (a "
                                  "compile-time POSITIONAL field, or a field that is not a scalar)",
                                 base,
                                 missing),
          .span     = lm->current_span(),
          .hint     = std::format("name the fields (`mut {} = (a=…, b=…)`) or declare the tuple's type", base),
      });
      return;
    }
  }
  if (slots.empty() && consts.empty()) {
    return;
  }
  std::map<std::string, std::string> leaves;
  for (const auto& [slot, ref] : slots) {
    auto leaf = absl::StrCat(base, ".", slot);
    if (ref != leaf) {
      emit_inline_binding(leaf, Lnast_node::create_ref(ref));
    }
    leaves.emplace(slot, std::move(leaf));
  }
  for (const auto& [slot, value] : consts) {
    auto leaf = absl::StrCat(base, ".", slot);
    emit_inline_binding(leaf, value);
    if (uncertain) {
      leaves.emplace(slot, std::move(leaf));
    }
  }
  auto& split = split_tuple_leaves_[base];
  for (const auto& [slot, ref] : slots) {
    (void)ref;
    split.insert(slot);
  }
  for (const auto& [slot, value] : consts) {
    (void)value;
    split.insert(slot);
  }
  if (leaves.empty()) {
    return;
  }
  symbol_table_.tuple_slot_ref.insert_or_assign(lhs, std::move(leaves));
}

bool uPass_runner::try_stream_tuple_port_store() {
  // Cursor on store(dst, field..., value). The parser guarantees that value is
  // a ref/const leaf; keep the check explicit because declining is always safe
  // and leaves the established tuple-set implementation in charge.
  if (lm->current_num_children() < 3 || !lm->has_child()) {
    return false;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return false;
  }
  auto base = resolve_stream_port_path(lm->current_text());
  if (!base) {
    lm->restore_cursor(saved);
    return false;
  }

  std::string path = std::move(*base);
  Lnast_node  value;
  bool        have_value = false;
  while (lm->move_to_sibling()) {
    if (lm->is_last_child()) {
      if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
        value = Lnast_node::create_ref(lm->current_text());
      } else if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
        value = Lnast_node::create_const(lm->current_text());
      } else {
        lm->restore_cursor(saved);
        return false;
      }
      have_value = true;
      break;
    }

    std::string field;
    if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
      if (auto v = Dlop::from_pyrope(lm->current_text()); v && !v->is_invalid()) {
        field = v->to_field();
      }
    } else if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
      if (auto v = try_fold_ref(lm->current_text()); v && !v->is_invalid() && !v->has_unknowns()) {
        field = v->to_field();
      }
    }
    if (field.empty()) {
      lm->restore_cursor(saved);
      return false;  // dynamic field/index: this is not a static ABI rewrite
    }
    path.push_back('.');
    path.append(field);
  }
  lm->restore_cursor(saved);
  if (!have_value || !stream_port_out_leaf_.contains(path)) {
    return false;
  }

  // The synthesized binding below runs through a scratch LNAST whose
  // top-module name is `inl-bind`.  Field-use diagnostics are keyed by the
  // owning unit, so record the source write here under the real unit (the
  // ordinary tuple-store path does this in constprop::record_field_write).
  // A nil seed is a declaration placeholder, not evidence that the field was
  // set, matching the ordinary path.
  if (!(value.is_const() && value.get_name() == "nil")) {
    symbol_table_.field_touched.insert(Symbol_table::field_touch_key(root_lnast_->get_top_module_name(), path));
  }
  emit_inline_binding(path, value);
  return true;
}

// A field write `store(d, 'a', v)` into a tuple value whose fields
// split_runtime_tuple_store gave scalar leaves writes that leaf instead, and
// the field reads through it from then on (tuple_slot_ref[d][a] == "d.a"; a
// comptime value still folds there). The aggregate write alone left a
// re-pointed field stale, and tolg has no lowering for it at all; the leaf
// write merges under control flow like any scalar write. A dynamic field
// keeps the ordinary path.
bool uPass_runner::try_split_leaf_field_store() {
  if (lm->current_num_children() < 3 || !lm->has_child() || split_tuple_leaves_.empty()) {
    return false;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
    lm->restore_cursor(saved);
    return false;
  }
  const std::string var(lm->current_text());
  const auto        base = std::string_view(var).substr(0, var.find("___ssa_"));
  if (!split_tuple_leaves_.contains(base)) {
    lm->restore_cursor(saved);
    return false;
  }
  std::string path;
  Lnast_node  value;
  bool        have_value = false;
  while (lm->move_to_sibling()) {
    if (lm->is_last_child()) {
      if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
        value      = Lnast_node::create_ref(lm->current_text());
        have_value = true;
      } else if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
        value      = Lnast_node::create_const(lm->current_text());
        have_value = true;
      }
      break;
    }
    std::string field;
    if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
      if (auto v = Dlop::from_pyrope(lm->current_text()); v && !v->is_invalid()) {
        field = v->to_field();
      }
    } else if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
      if (auto v = try_fold_ref(lm->current_text()); v && !v->is_invalid() && !v->has_unknowns()) {
        field = v->to_field();
      }
    }
    if (field.empty()) {
      break;
    }
    if (!path.empty()) {
      path.push_back('.');
    }
    path.append(field);
  }
  lm->restore_cursor(saved);
  if (!have_value || path.empty()) {
    return false;
  }
  const auto leaf   = absl::StrCat(base, ".", path);
  const auto slots  = symbol_table_.tuple_slot_ref.find(var);
  const bool routed = slots != symbol_table_.tuple_slot_ref.end() && slots->second.contains(path) && slots->second.at(path) == leaf;
  if (!routed && !split_tuple_leaves_.at(base).contains(path)) {
    return false;  // a field the split gave no leaf (a positional comptime entry)
  }
  if (!(value.is_const() && value.get_name() == "nil")) {
    symbol_table_.field_touched.insert(Symbol_table::field_touch_key(root_lnast_->get_top_module_name(), leaf));
  }
  // Copied out first: the binding below re-enters constprop, which edits the map.
  std::map<std::string, std::string> updated;
  if (!routed && slots != symbol_table_.tuple_slot_ref.end()) {
    updated = slots->second;
  }
  emit_inline_binding(leaf, value);
  if (!routed) {
    updated[path] = leaf;
    symbol_table_.tuple_slot_ref.insert_or_assign(var, std::move(updated));
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
  // A flattened tuple-port read is structural in exactly the same way as a
  // gathered var-arg pick. An interior pick creates only an alias; the first
  // leaf pick becomes a scalar binding. This consumes the tuple while the
  // shared pass runner streams the source and avoids SSA's former full-tree
  // compatibility copy.
  if (auto base = resolve_stream_port_path(src)) {
    std::string path = std::move(*base);
    path.push_back('.');
    path.append(key);
    if (stream_port_in_leaf_.contains(path) || stream_port_out_leaf_.contains(path)) {
      // This direct binding bypasses constprop's tuple_get hook, which is
      // where the legacy path records an explicit field read.
      symbol_table_.field_touched.insert(Symbol_table::field_touch_key(root_lnast_->get_top_module_name(), path));
      stream_port_alias_.insert_or_assign(dst, path);
      emit_inline_binding(dst, Lnast_node::create_ref(path));
      return true;
    }
    if (stream_port_prefix_.contains(path)) {
      stream_port_alias_.insert_or_assign(std::move(dst), std::move(path));
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
  // An instance's composite output has ABI leaf facts but no runtime tuple
  // object. Materialize its selected bundle as ordinary leaf reads, so whole
  // assignments and subsequent nested calls use the same tuple-value path.
  if (const auto pending = symbol_table_.pending_keys_by_root.find(src); pending != symbol_table_.pending_keys_by_root.end()) {
    const std::string        prefix = src + "." + key + ".";
    std::vector<std::string> leaves;
    for (const auto& candidate : pending->second) {
      if (candidate.starts_with(prefix) && symbol_table_.pending_decl_facts.contains(candidate)) {
        leaves.push_back(candidate.substr(prefix.size()));
      }
    }
    std::sort(leaves.begin(), leaves.end());
    leaves.erase(std::unique(leaves.begin(), leaves.end()), leaves.end());
    if (!leaves.empty()) {
      std::vector<std::pair<std::string, Lnast_node>> fields;
      for (std::size_t i = 0; i < leaves.size(); ++i) {
        const auto value = dst + "__leaf" + std::to_string(i);
        emit_inline_tuple_pick(value, src, key + "." + leaves[i]);
        fields.emplace_back(leaves[i], Lnast_node::create_ref(value));
      }
      emit_inline_tuple(dst, fields);
      return true;
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
  using N    = Lnast_ntype;
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
      // tuple_slot_ref can retain the SSA carrier of a leaf whose producer
      // constprop folded away. Emitting that carrier here creates a dangling
      // Hotmux arm (`%tmp` with no staged store). Materialize its folded value
      // as a literal; only a genuinely runtime carrier keeps the ref.
      if (auto folded = try_fold_ref(*rname)) {
        elems.push_back(Lnast_node::create_const(folded->to_pyrope()));
      } else {
        elems.push_back(Lnast_node::create_ref(*rname));  // runtime wire
        any_runtime = true;
      }
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
    // A frontend-created `%tmp = tuple_add(...)` has no declaration facts but
    // is itself the plain tuple value; all-constant SROA reads commonly take
    // this shape. Named values still require an explicit non-reg mode so a
    // registered/typed array cannot be mistaken for a constant ROM tuple.
    const bool muxable_mode   = (!facts && std::string_view(src).starts_with("%"))
                              || (facts && (facts->mode == upass::Mode::const_kind || facts->mode == upass::Mode::mut_kind));
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

// The scalar token a type-valued generic spells when the body uses it as a
// CONSTRUCTOR / cast (`T(a)`): a named type verbatim, a Bool bind `Bool`, an
// integer envelope its `U<N>`/`S<N>` spelling (upass::classify_typecast reads
// it back). Empty when the bind is not a type (constant / lambda) — those
// never stand in a callee slot.
std::string uPass_runner::generic_cast_token(const Generic_bind& gb) {
  if (!gb.type_name.empty()) {
    return gb.type_name;
  }
  if (gb.kind == Io_kind::boolean) {
    return "Bool";
  }
  if (!gb.max && !gb.min) {
    return {};
  }
  const bool is_signed = !(gb.min && !gb.min->is_negative());
  int        bits      = 0;
  if (gb.max && gb.max->is_integer()) {
    if (!is_signed) {
      bits = static_cast<int>(gb.max->get_payload_bits());
    } else {
      const int mb = static_cast<int>(gb.max->get_signed_bits());
      const int nb = (gb.min && gb.min->is_integer()) ? static_cast<int>(gb.min->get_signed_bits()) : mb;
      bits         = std::max(mb, nb);
    }
  }
  return (is_signed ? "S" : "U") + std::to_string(bits);
}

// ── pipe/mod/fluid template specialization ──────────────────────────

void uPass_runner::copy_subtree_into(const std::shared_ptr<Lnast>& src, const Lnast_nid& src_nid, const std::shared_ptr<Lnast>& dst,
                                     const Lnast_nid&                                      dst_parent,
                                     const absl::flat_hash_map<std::string, Generic_bind>* type_subst) {
  const auto type = src->get_type(src_nid);
  Lnast_nid  newn;
  // `N.[bits]`/`.[max]`/`.[min]` of a value generic bound to a TYPED constant
  // reads that constant's declared type (Generic_bind::decl_typed): the literal
  // the base `ref N` becomes below would report its value width instead.
  if (type_subst != nullptr && Lnast_ntype::is_attr_get(type)) {
    const auto res  = src->get_first_child(src_nid);
    const auto base = res.is_invalid() ? res : src->get_sibling_next(res);
    const auto attr = base.is_invalid() ? base : src->get_sibling_next(base);
    const auto it   = attr.is_invalid() || !Lnast_ntype::is_ref(src->get_type(base)) || !Lnast_ntype::is_const(src->get_type(attr))
                          ? type_subst->end()
                          : type_subst->find(std::string(src->get_name(base)));
    if (it != type_subst->end() && it->second.decl_typed && src->get_sibling_next(attr).is_invalid()) {
      const auto&         gb = it->second;
      const auto          an = src->get_name(attr);
      std::optional<Dlop> v;
      if (an == "max") {
        v = gb.decl_max;
      } else if (an == "min") {
        v = gb.decl_min;
      } else if (an == "bits" && gb.decl_max && gb.decl_min) {
        v = *Dlop::create_integer(upass::range_bits(*gb.decl_max, *gb.decl_min));
      }
      if (an == "max" || an == "min" || an == "bits") {
        auto st = dst->add_child(dst_parent, Lnast_ntype::create_store());
        if (const auto id = src->get_srcid(src_nid); id != hhds::SourceId_invalid) {
          dst->set_srcid(st, dst->source_locator().import_from(src->source_locator(), id));
        }
        dst->add_child(st, Lnast_node::create_ref(src->get_name(res)));
        dst->add_child(st, Lnast_node::create_const(v ? std::string(v->to_pyrope()) : std::string("nil")));
        return;
      }
    }
  }
  if (Lnast_ntype::is_ref(type)) {
    // Generic substitution: a body `:T` slot is a `ref T` (SSA strips io
    // type refs; only body declare/type_spec slots carry them). Replace it
    // with the bound concrete type (macro expansion).
    //
    // A KEY ref is a label, never a read: the child-0 ref of a `store` directly
    // under a func_call (named actual `f(a=a)`) or a tuple_add/tuple_concat
    // (field `(a=a)`). Same rule as makes_store_keys / Lnast_manager's
    // is_call_arg_key. Substituting it (a rolled loop's caller-namespace
    // rename, a constant generic `f(N=N)`) names an argument/field that does
    // not exist (`add2(inl1_a = inl1_a)`), so copy it verbatim.
    const auto is_key_ref = [&]() {
      const auto parent = src->get_parent(src_nid);
      if (parent.is_invalid() || !Lnast_ntype::is_store(src->get_type(parent)) || src->get_first_child(parent) != src_nid) {
        return false;
      }
      const auto gp = src->get_parent(parent);
      if (gp.is_invalid()) {
        return false;
      }
      const auto gt = src->get_type(gp);
      return Lnast_ntype::is_func_call(gt) || Lnast_ntype::is_tuple_add(gt) || Lnast_ntype::is_tuple_concat(gt);
    };
    if (type_subst != nullptr && !is_key_ref()) {
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
        // The CALLEE slot of a `func_call` (`T(a)`, the constructor cast of a
        // type-valued generic) needs the type's NAME, not a type node: tolg
        // reads the callee by its text, and a `prim_type_int` there has none
        // (`call to undefined function ''`). Rebuild the scalar token the same
        // way the inline path's generic_cast_binds_ does, so a specialized
        // clone (a generic comb inlined with several widths, or a mod
        // template) casts exactly like the inline frame.
        if (Lnast_ntype::is_func_call(dst->get_type(dst_parent))) {
          const auto first = dst->get_first_child(dst_parent);
          if (!first.is_invalid() && dst->get_sibling_next(first).is_invalid()) {
            if (const auto token = generic_cast_token(gb); !token.empty()) {
              dst->add_child(dst_parent, Lnast_node::create_ref(token));
              return;
            }
          }
        }
        if (!gb.tuple_fields.empty() && Lnast_ntype::is_store(dst->get_type(dst_parent))) {
          auto tuple = dst->add_child(dst_parent, Lnast_ntype::create_tuple_add());
          for (const auto& field : gb.tuple_fields) {
            auto entry = dst->add_child(tuple, Lnast_ntype::create_store());
            dst->add_child(entry, Lnast_node::create_ref(field.name));
            dst->add_child(entry, Lnast_node::create_const("nil"));
            if (field.type.kind == upass::Kind::boolean) {
              dst->add_child(entry, Lnast_ntype::create_prim_type_bool());
            } else {
              auto field_type = dst->add_child(entry, Lnast_ntype::create_prim_type_int());
              dst->add_child(field_type, Lnast_node::create_const(field.type.max.to_pyrope()));
              dst->add_child(field_type, Lnast_node::create_const(field.type.min.to_pyrope()));
            }
          }
          return;
        }
        if (!gb.type_name.empty()) {
          dst->add_child(dst_parent, Lnast_node::create_ref(gb.type_name));
          return;
        }
        // BOOLEAN before the envelope arm: a bool bind carries a 1/0 envelope too,
        // and Pyrope keeps bool and int distinct -- typing a `:T` port as
        // int(max=1,min=0) made `if a` fail with `cond-not-bool`. This is the
        // shape prp2lnast emits for a hand-written `:bool` port (a childless
        // prim_type_bool), and it mirrors the inline path's own arm ordering.
        if (gb.kind == Io_kind::boolean) {
          dst->add_child(dst_parent, Lnast_ntype::create_prim_type_bool());
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
  if (Lnast_ntype::is_comp_type_array(type)) {
    // `mut v:[N]T`: prp2lnast keeps the dimension as the expression it was
    // written as (`ref N`). Every downstream reader takes the dim node's TEXT,
    // and `Dlop::from_pyrope("N")` is the character code of 'N' — 78 lanes for
    // any `[N]`. Fold a comptime-named dim to its digits while the declared
    // type is copied; tolg now refuses a dim that is not a digit string.
    bool first = true;
    for (auto c : src->children(src_nid)) {
      const bool is_dim = !first;
      first             = false;
      if (is_dim && (Lnast_ntype::is_ref(src->get_type(c)) || Lnast_ntype::is_const(src->get_type(c)))) {
        // The dim rides as `ref N` or as `const '[N]'` (prp2lnast keeps the
        // written text); a digit string is already folded.
        std::string txt{src->get_name(c)};
        if (txt.size() >= 2 && txt.front() == '[' && txt.back() == ']') {
          txt = txt.substr(1, txt.size() - 2);
        }
        int64_t    n      = 0;
        const bool digits = !txt.empty() && std::from_chars(txt.data(), txt.data() + txt.size(), n).ptr == txt.data() + txt.size();
        if (!digits) {
          // A GENERIC binding WINS over any same-named caller-scope constant.
          // try_fold_ref reads the CALLER's symbol table, so consulting it for
          // a name that IS a generic parameter sizes `buf<4>`'s `[N]` array by
          // the caller's shadowing `const N = 16` -- silently, and with the
          // packed bus four times too wide.
          const Generic_bind* gb = nullptr;
          if (type_subst != nullptr) {
            if (auto git = type_subst->find(txt); git != type_subst->end()) {
              gb = &git->second;
            }
          }
          if (gb != nullptr) {
            int64_t     gn = 0;
            const auto& ct = gb->const_text;
            if (!ct.empty() && std::from_chars(ct.data(), ct.data() + ct.size(), gn).ptr == ct.data() + ct.size() && gn > 0) {
              dst->add_child(newn, Lnast_node::create_const("[" + std::to_string(gn) + "]"));
              continue;
            }
            // A type/lambda/non-integer bind is not a dim: fall through to the
            // recursion so type_subst still governs (and the bad dim is
            // diagnosed downstream), never to the caller's constant.
          } else if (Lnast::is_tmp(txt)) {
            // `[N+1]T` lowered to template statements. A PORT's is folded under
            // the binds here (no later pass folds a port's shape); a body
            // declare's is computed by the clone's own prologue, where
            // bake_decl_pre_step folds it. A caller-scope temp of the same name
            // is unrelated either way.
            if (type_subst != nullptr) {
              if (auto fv = fold_template_bound(src, txt, *type_subst, 0, nullptr);
                  fv && fv->is_just_i64() && fv->to_just_i64() > 0) {
                dst->add_child(newn, Lnast_node::create_const("[" + std::to_string(fv->to_just_i64()) + "]"));
                continue;
              }
            }
          } else if (auto fv = try_fold_ref(txt); fv && fv->is_integer() && fv->is_just_i64() && fv->to_just_i64() > 0) {
            dst->add_child(newn, Lnast_node::create_const("[" + std::to_string(fv->to_just_i64()) + "]"));
            continue;
          }
        }
      }
      copy_subtree_into(src, c, dst, newn, type_subst);
    }
    return;
  }
  for (auto c : src->children(src_nid)) {
    copy_subtree_into(src, c, dst, newn, type_subst);
  }
}

// 2f-generic_port_width — evaluate a port bound's text under the generic binds.
// The bound is either a literal, a generic name (bound to a constant), or a
// `%tmp` that prp2lnast's flush_deferred_port_bounds defined by a straight-line
// arithmetic statement in the template's body prologue (`mult %t0, N, 4; shl
// %t1, 1, %t0; minus %t2, %t1, 1`). Only the FIRST stmts block is scanned, and
// only the operator set the bound desugar can produce; anything else (a call, a
// mux, a runtime read) is not a compile-time width. Two more prologue shapes
// fold: a captured `comptime const` (`store(W, 4)`, emitted ahead of the bound
// desugar, possibly after its declaration) and an integer
// `.[bits]`/`.[max]`/`.[min]` read of a bound type generic (`T.[bits]`) or of
// an input's declared type (`a.[bits]`).
std::optional<Dlop> uPass_runner::fold_template_bound(const std::shared_ptr<Lnast>& tmpl, std::string_view text,
                                                      const absl::flat_hash_map<std::string, Generic_bind>& binds, int depth,
                                                      std::string* unbound) {
  if (text.empty() || text == "nil" || depth > 64) {
    return std::nullopt;
  }
  const char c0 = text.front();
  if (std::isdigit(static_cast<unsigned char>(c0)) != 0 || c0 == '-') {
    // An index-range dimension (`100..<132`) starts with a digit too, but it
    // is no literal: from_pyrope throws on it.
    if (text.find("..") != std::string_view::npos) {
      return std::nullopt;
    }
    auto v = Dlop::from_pyrope(text);
    return (v && v->is_integer() && !v->has_unknowns()) ? std::optional<Dlop>(*v) : std::nullopt;
  }
  if (auto it = binds.find(std::string(text)); it != binds.end()) {
    if (it->second.const_text.empty()) {
      return std::nullopt;  // a type / lambda bind is not a width
    }
    return fold_template_bound(tmpl, it->second.const_text, binds, depth + 1, unbound);
  }
  if (const auto& gens = tmpl->get_generics(); std::find(gens.begin(), gens.end(), text) != gens.end()) {
    if (unbound != nullptr && unbound->empty()) {
      *unbound = std::string(text);  // no explicit bind, no default, not inferred
    }
    return std::nullopt;
  }
  const auto& tio = tmpl->io_meta();
  if (tio.find(text) != nullptr) {
    return std::nullopt;  // a runtime port value (its only prologue store is a `__default`) is never a width
  }
  // The declared integer envelope (max, min) of a bound type generic or of an
  // input port, for an attribute read.
  const auto envelope = [&](std::string_view base) -> std::optional<std::pair<Dlop, Dlop>> {
    if (auto it = binds.find(std::string(base)); it != binds.end()) {
      const auto& gb = it->second;
      if (gb.const_text.empty() && gb.type_name.empty() && gb.kind == Io_kind::integer && gb.max && gb.min) {
        return std::pair{*gb.max, *gb.min};
      }
      if (gb.decl_typed && gb.decl_max && gb.decl_min) {
        return std::pair{*gb.decl_max, *gb.decl_min};  // a constant bound from a typed one reads its declared type
      }
      return std::nullopt;
    }
    const auto* e = tio.find(base);
    if (e == nullptr || e->kind == Io_kind::boolean || e->array_size > 0 || !unsized_array_port(*tmpl, *e, false).is_invalid()) {
      return std::nullopt;  // an array port's deferred bound is its element's
    }
    if (e->has_deferred_bound()) {
      auto mx = fold_template_bound(tmpl, e->bound_max_text, binds, depth + 1, unbound);
      auto mn = fold_template_bound(tmpl, e->bound_min_text, binds, depth + 1, unbound);
      return mx && mn ? std::optional<std::pair<Dlop, Dlop>>(std::pair{*mx, *mn}) : std::nullopt;
    }
    if (e->has_range) {
      return std::pair{*Dlop::create_integer(e->range_max), *Dlop::create_integer(e->range_min)};
    }
    if (e->bits > 0) {
      const auto b = static_cast<uint32_t>(e->bits);
      return std::pair{upass::max_from_bits(b, e->is_signed), upass::min_from_bits(b, e->is_signed)};
    }
    return std::nullopt;
  };
  for (auto top : tmpl->children(tmpl->get_root())) {
    if (!Lnast_ntype::is_stmts(tmpl->get_type(top))) {
      continue;
    }
    // Only a name with ONE writer folds: a captured `comptime const` computed
    // by a `mut` updated under a `for`/`if` (`mut m = 0; for … { m += 1 }`)
    // has its value in the later writes, not in the top-level initial store.
    // A `%tmp` is single-assignment by construction.
    int writers = 1;
    if (!Lnast::is_tmp(text)) {
      writers = 0;
      for (auto n : tmpl->depth_preorder(top)) {
        using N      = Lnast_ntype;
        const auto t = tmpl->get_type(n);
        if (N::is_stmts(t) || !N::is_stmts(tmpl->get_type(tmpl->get_parent(n)))) {
          continue;  // only statements write
        }
        if (N::is_for(t)) {
          int pos = 0;
          for (auto c : tmpl->children(n)) {
            if (pos++ != 1 && N::is_ref(tmpl->get_type(c)) && tmpl->get_name(c) == text) {
              ++writers;  // a loop bind (child 1 is the iterable)
            }
          }
          continue;
        }
        if (N::is_declare(t) || N::is_attr_set(t) || N::is_type_spec(t) || N::is_if_like(t) || N::is_while(t) || N::is_cassert(t)) {
          continue;  // no destination
        }
        if (const auto dst = tmpl->get_first_child(n);
            !dst.is_invalid() && N::is_ref(tmpl->get_type(dst)) && tmpl->get_name(dst) == text) {
          ++writers;
        }
      }
    }
    if (writers != 1) {
      return std::nullopt;
    }
    for (auto stmt : tmpl->children(top)) {
      const auto dst = tmpl->get_first_child(stmt);
      if (dst.is_invalid() || !Lnast_ntype::is_ref(tmpl->get_type(dst)) || tmpl->get_name(dst) != text) {
        continue;
      }
      const auto t = tmpl->get_type(stmt);
      using N      = Lnast_ntype;
      if (N::is_declare(t) || N::is_attr_set(t) || N::is_type_spec(t)) {
        continue;  // a captured `comptime const`'s declaration; its value is the store
      }
      if (N::is_attr_get(t)) {
        const auto base = tmpl->get_sibling_next(dst);
        const auto attr = base.is_invalid() ? base : tmpl->get_sibling_next(base);
        if (attr.is_invalid() || !N::is_ref(tmpl->get_type(base)) || !N::is_const(tmpl->get_type(attr))) {
          return std::nullopt;
        }
        // An untyped constant generic is as wide as its value (user ruling 6).
        if (const auto it = binds.find(std::string(tmpl->get_name(base)));
            it != binds.end() && !it->second.const_text.empty() && !it->second.decl_typed && tmpl->get_name(attr) == "bits") {
          const auto v = fold_template_bound(tmpl, it->second.const_text, binds, depth + 1, unbound);
          return v ? std::optional<Dlop>(*Dlop::create_integer(upass::value_bits(*v))) : std::nullopt;
        }
        const auto env = envelope(tmpl->get_name(base));
        if (!env) {
          return std::nullopt;
        }
        const auto& [mx, mn] = *env;
        const auto an        = tmpl->get_name(attr);
        if (an == "max") {
          return mx;
        }
        if (an == "min") {
          return mn;
        }
        if (an == "bits") {
          return *Dlop::create_integer(upass::range_bits(mx, mn));
        }
        return std::nullopt;
      }
      if (N::is_func_call(t)) {
        // `std.clog2(N)`, the one call a comptime width may use (docs 13-stdlib).
        const auto callee = tmpl->get_sibling_next(dst);
        const auto arg    = callee.is_invalid() ? callee : tmpl->get_sibling_next(callee);
        if (arg.is_invalid() || !tmpl->get_sibling_next(arg).is_invalid() || !N::is_ref(tmpl->get_type(callee))
            || tmpl->get_name(callee) != upass::std_clog2_callee || N::is_store(tmpl->get_type(arg))) {
          return std::nullopt;
        }
        const auto v = fold_template_bound(tmpl, tmpl->get_name(arg), binds, depth + 1, unbound);
        return v ? upass::std_clog2(*v) : std::nullopt;
      }
      std::vector<Dlop> ops;
      for (auto c = tmpl->get_sibling_next(dst); !c.is_invalid(); c = tmpl->get_sibling_next(c)) {
        auto v = fold_template_bound(tmpl, tmpl->get_name(c), binds, depth + 1, unbound);
        if (!v) {
          return std::nullopt;
        }
        ops.push_back(*v);
      }
      if (ops.size() == 1 && N::is_store(t)) {
        return ops[0];  // a captured `comptime const W = 4`
      }
      if (ops.size() < 2) {
        return std::nullopt;
      }
      auto fold_chain = [&](auto op) {
        Dlop r = ops[0];
        for (std::size_t i = 1; i < ops.size(); ++i) {
          r = *op(r, ops[i]);
        }
        return std::optional<Dlop>(r);
      };
      if (N::is_plus(t)) {
        return fold_chain([](const Dlop& a, const Dlop& b) { return a.add_op(b); });
      }
      if (N::is_mult(t)) {
        return fold_chain([](const Dlop& a, const Dlop& b) { return a.mult_op(b); });
      }
      if (N::is_bit_and(t)) {
        return fold_chain([](const Dlop& a, const Dlop& b) { return a.and_op(b); });
      }
      if (N::is_bit_or(t)) {
        return fold_chain([](const Dlop& a, const Dlop& b) { return a.or_op(b); });
      }
      if (N::is_bit_xor(t)) {
        return fold_chain([](const Dlop& a, const Dlop& b) { return a.xor_op(b); });
      }
      if (ops.size() != 2) {
        return std::nullopt;
      }
      const Dlop& a = ops[0];
      const Dlop& b = ops[1];
      if (N::is_minus(t)) {
        return *a.sub_op(b);
      }
      if (N::is_div(t)) {
        return b.is_known_zero() ? std::nullopt : std::optional<Dlop>(*a.div_op(b));
      }
      if (N::is_mod(t)) {
        return b.is_known_zero() ? std::nullopt : std::optional<Dlop>(*a.rem_op(b));
      }
      if (N::is_shl(t)) {
        return b.is_negative() ? std::nullopt : std::optional<Dlop>(*a.shl_op(b));
      }
      if (N::is_sra(t)) {
        return b.is_negative() ? std::nullopt : std::optional<Dlop>(*a.sra_op(b));
      }
      return std::nullopt;  // a call, mux, if-tmp… is not a comptime width
    }
    break;  // the body prologue is the first stmts block
  }
  return std::nullopt;
}

uPass_runner::Spec_port uPass_runner::deferred_port_type(const std::shared_ptr<Lnast>& tmpl, const Lnast_io_entry& e,
                                                         const absl::flat_hash_map<std::string, Generic_bind>& binds,
                                                         const std::string& callee_name, const livehd::diag::Span& span) {
  Spec_port sp;
  sp.inject       = true;
  const auto side = [&](const std::string& text, std::string_view which) -> std::optional<Dlop> {
    if (text.empty() || text == "nil") {
      return std::nullopt;  // an unbounded side stays unbounded
    }
    std::string unbound;
    if (auto v = fold_template_bound(tmpl, text, binds, 0, &unbound); v) {
      return v;
    }
    if (!unbound.empty()) {
      // Same wording as resolve_generic_binds' explicit-list miss: nothing
      // bound, defaulted or inferred the generic this port's width needs.
      fcall_arg_fail(span,
                     "fcall-generic-arity",
                     std::format("generic `{}` of `{}` is unbound and has no default (it sets the width of port `{}`)",
                                 unbound,
                                 callee_name,
                                 e.name),
                     std::format("bind it in the `<…>` list or declare a default `<{}=…>`", unbound));
    }
    const std::string msg
        = std::format("integer type bound `{}` of port `{}` of `{}` is not a compile-time value once its generics are bound",
                      which,
                      e.name,
                      callee_name);
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "type-bound-not-comptime",
        .category = "type",
        .pass     = "upass.runner",
        .message  = msg,
        .span     = span,
        .hint     = "a port bound may use literals, the lambda's own generic parameters, and `comptime const`s computed from "
                    "them with `+ - * / % << >> & | ^` and `std.clog2` only (not by a loop, an `if` or another call)",
    });
    throw std::runtime_error(msg);
  };
  sp.max = side(e.bound_max_text, "max");
  sp.min = side(e.bound_min_text, "min");
  return sp;
}

std::shared_ptr<Lnast> uPass_runner::clone_template_specialized(const std::shared_ptr<Lnast>& tmpl, const std::string& mangled,
                                                                const std::vector<Spec_port>& inject,
                                                                const std::vector<Spec_port>& vports, const std::string& vname,
                                                                const std::vector<Spec_port>&                         out_inject,
                                                                const absl::flat_hash_map<std::string, Generic_bind>& type_subst) {
  for (const auto& [generic, binding] : type_subst) {
    (void)generic;
    if (!binding.tuple_fields.empty()) {
      detuple_registry_->named_types.insert_or_assign(mangled.substr(0, mangled.find('.')) + "\n" + binding.type_name,
                                                      binding.tuple_fields);
    }
  }
  // A LAMBDA-valued generic (`apply<inc>`) is captured by its raw spelling
  // in the CALLER's scope, but the clone writes it into the callee body as a
  // bare `inc(...)`, which then resolves lexically in the callee's own unit
  // (lookup_callee): a same-named helper nested in (or defined by the file
  // of) the generic would silently win over the caller's `inc`. Pin each such
  // binding to the caller's choice (full registry name) whenever the
  // template's scope or the clone's scope would pick a different body. This
  // runs before the clone is pushed as a source, so lookup_callee still
  // resolves in the caller's scope.
  absl::flat_hash_map<std::string, Generic_bind> portable_subst;
  const auto*                                    subst_src = &type_subst;
  for (const auto& [generic, binding] : type_subst) {
    if (binding.func_name.empty()) {
      continue;
    }
    auto pinned = frame_portable_func_name(binding.func_name, tmpl->get_top_module_name());
    if (pinned == binding.func_name) {
      pinned = frame_portable_func_name(binding.func_name, mangled);
    }
    if (pinned == binding.func_name) {
      continue;
    }
    if (subst_src != &portable_subst) {
      portable_subst = type_subst;
      subst_src      = &portable_subst;
    }
    portable_subst[generic].func_name = std::move(pinned);
  }
  auto        clone    = std::make_shared<Lnast>(mangled);
  const auto* subst    = subst_src->empty() ? nullptr : subst_src;
  auto        src_root = tmpl->get_root();
  auto        dst_root = clone->set_root(tmpl->get_type(src_root));  // top
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
    } else if (p.kind == Io_kind::boolean) {
      clone->add_child(st, Lnast_ntype::create_prim_type_bool());
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
  clone->set_simulation_init(tmpl->get_simulation_init());
  clone->set_verilog_origin(tmpl->is_verilog_origin());
  clone->set_skip_timecheck(tmpl->get_skip_timecheck());
  // The body keeps the template's demoted SSA versions (`w__w1`, a template
  // restored from `ln:` is re-SSA'd before it is cloned): keep what they are
  // versions of.
  for (const auto& [demoted, base] : tmpl->get_ssa_demoted()) {
    clone->note_ssa_demoted(demoted, base);
  }
  clone->set_template(false);
  clone->set_generics({});

  // 2f-generic_port_width — a deferred generic-width port copied from the
  // template still carries `prim_type_int(ref %t, …)`. Rewrite those leaves IN
  // PLACE: the tree is append-only and SSA's flatten_assign takes the FIRST
  // type child, so an appended second prim_type_int would leave the ref one in
  // charge. Returns false when the entry has no ref-bearing type child (the
  // ordinary append path then applies).
  auto patch_ref_bounds = [&](const Lnast_nid& entry, const Spec_port& p) -> bool {
    if (!p.type_name.empty()) {
      return false;
    }
    for (auto c : clone->children(entry)) {
      // An array port's deferred bound is its ELEMENT's (`v:[N]unsigned(bits=N)`).
      while (Lnast_ntype::is_comp_type_array(clone->get_type(c)) && !clone->get_first_child(c).is_invalid()) {
        c = clone->get_first_child(c);
      }
      if (!Lnast_ntype::is_prim_type_int(clone->get_type(c))) {
        continue;
      }
      auto mx = clone->get_first_child(c);
      auto mn = mx.is_invalid() ? mx : clone->get_sibling_next(mx);
      if (mx.is_invalid() || mn.is_invalid()) {
        return false;
      }
      if (!Lnast_ntype::is_ref(clone->get_type(mx)) && !Lnast_ntype::is_ref(clone->get_type(mn))) {
        return false;
      }
      clone->set_type(mx, Lnast_ntype::create_const());
      clone->set_name(mx, p.max ? std::string(p.max->to_pyrope()) : std::string("nil"));
      clone->set_type(mn, Lnast_ntype::create_const());
      clone->set_name(mn, p.min ? std::string(p.min->to_pyrope()) : std::string("nil"));
      return true;
    }
    return false;
  };

  // Inject the concrete type child into each untyped fixed port. `inject` and
  // `out_inject` are indexed like the template's io_meta, where an inline tuple
  // port is FLATTENED into its dotted leaves (`t.a`, `t.b`) while the tree keeps
  // it as ONE store: match each tree port to its io_meta slot by NAME, never by
  // position, or every port after a tuple port takes its neighbour's type. A
  // tuple port itself has no slot of its own and is never injected.
  auto inject_section = [&](const Lnast_nid& tup, const std::vector<Lnast_io_entry>& entries, const std::vector<Spec_port>& specs) {
    absl::flat_hash_map<std::string, std::size_t> slot;
    for (std::size_t k = 0; k < entries.size() && k < specs.size(); ++k) {
      slot.try_emplace(entries[k].name, k);
    }
    // A field of a tuple port keeps its generic-width bound on the nested
    // store (`req:(addr:unsigned(bits=N), …)`): visit it under its dotted
    // io_meta name so the in-place patch reaches it too.
    std::vector<std::pair<Lnast_nid, std::string>> entries_todo;
    for (auto entry : clone->children(tup)) {
      entries_todo.emplace_back(entry, std::string{});
    }
    for (std::size_t w = 0; w < entries_todo.size(); ++w) {
      const auto [entry, prefix] = entries_todo[w];
      if (!Lnast_ntype::is_store(clone->get_type(entry))) {
        continue;
      }
      auto name_n = clone->get_first_child(entry);
      if (name_n.is_invalid()) {
        continue;
      }
      const auto full = prefix + std::string(clone->get_name(name_n));
      for (auto c : clone->children(entry)) {
        if (Lnast_ntype::is_tuple_add(clone->get_type(c))) {
          for (auto field : clone->children(c)) {
            entries_todo.emplace_back(field, full + ".");
          }
        }
      }
      const auto it = slot.find(full);
      if (it == slot.end()) {
        continue;
      }
      const auto& sp = specs[it->second];
      if (sp.array_size > 0) {
        for (auto type : clone->children(entry)) {
          if (!Lnast_ntype::is_comp_type_array(clone->get_type(type))) {
            continue;
          }
          const auto elem = clone->get_first_child(type);
          const auto dim  = elem.is_invalid() ? elem : clone->get_sibling_next(elem);
          if (dim.is_invalid()) {
            clone->add_child(type, Lnast_node::create_const(std::format("[{}]", sp.array_size)));
          } else if (clone->get_name(dim) == "[]") {
            clone->set_type(dim, Lnast_ntype::create_const());
            clone->set_name(dim, std::format("[{}]", sp.array_size));
          }
          break;
        }
      }
      if (!sp.inject || std::any_of(type_subst.begin(), type_subst.end(), [&](const auto& item) {
            return !item.second.tuple_fields.empty() && item.second.type_name == sp.type_name;
          })) {
        continue;
      }
      if (sp.kind == Io_kind::boolean) {
        clone->add_child(entry, Lnast_ntype::create_prim_type_bool());
        continue;
      }
      if (patch_ref_bounds(entry, sp)) {
        continue;  // rewritten in place
      }
      if (!sp.type_name.empty()) {
        clone->add_child(entry, Lnast_node::create_ref(sp.type_name));
        continue;
      }
      auto pt = clone->add_child(entry, Lnast_ntype::create_prim_type_int());
      clone->add_child(pt, Lnast_node::create_const(sp.max ? std::string(sp.max->to_pyrope()) : std::string("nil")));
      clone->add_child(pt, Lnast_node::create_const(sp.min ? std::string(sp.min->to_pyrope()) : std::string("nil")));
    }
  };
  auto io_n = clone->get_first_child(clone->get_root());
  if (io_n.is_invalid() || !Lnast_ntype::is_io(clone->get_type(io_n))) {
    return clone;
  }
  auto in_tup = clone->get_first_child(io_n);
  if (in_tup.is_invalid()) {
    return clone;
  }
  const auto& tio = tmpl->io_meta();
  inject_section(in_tup, tio.inputs, inject);
  // OUTPUT ports: `-> (r:T)` with T bound gets the concrete type too (the
  // outputs tuple is the io node's second tuple_add).
  if (auto out_tup = clone->get_sibling_next(in_tup); !out_tup.is_invalid()) {
    inject_section(out_tup, tio.outputs, out_inject);
  }
  // User ruling 2026-09-28 (34): a `mod`/`pipe` input default over a generic
  // (`c:u8 = N * 2`) rode the template's body prologue behind the `__default`
  // io sentinel; this specialization's binds fold it into the io slot, the
  // constant a caller drives an omitted port with (io_port::input_default_const;
  // check_omitted_default_fit already rejected one that does not fold).
  if (tmpl->get_lambda_kind() != "comb") {
    for (auto entry : clone->children(in_tup)) {
      const auto nm = clone->get_first_child(entry);
      const auto dv = nm.is_invalid() ? nm : clone->get_sibling_next(nm);
      if (dv.is_invalid() || !Lnast_ntype::is_const(clone->get_type(dv)) || clone->get_name(dv) != "__default") {
        continue;
      }
      if (const auto v
          = fold_template_bound(tmpl, Lnast_io_entry::default_value_name(clone->get_name(nm)), type_subst, 0, nullptr)) {
        clone->set_name(dv, std::string(v->to_pyrope()));
      }
    }
  }
  return clone;
}

// ── loop rolling (todo_loop_cond_sub.md M4) ─────────────────────────

namespace {

// A `store` whose child 0 names something STRUCTURAL rather than a variable.
// Two shapes lower to the same `store(ref(key), value)` a plain assignment
// does, and both are labels, not writes:
//   * a NAMED ACTUAL — `f(x=d)` and the reserved `f::[name=inst]` sit directly
//     under a func_call, so reading them as writes turns `__inst_name` into a
//     loop carry;
//   * a TUPLE-LITERAL FIELD — `(x=i, y=1)` lowers to
//     `tuple_add(%tmp, store(ref("x"), …), store(ref("y"), …))`
//     (prp2lnast.cpp), so its FIELD NAMES would be read as written variables.
//     A field named after an enclosing io port then becomes a "carry" whose
//     lifted `__carry_out` is assigned back onto a module port.
// Same rule Lnast_manager::is_tuple_field_key/is_call_arg_key apply when they
// decline to inline-rename these refs.
bool makes_store_keys(const Lnast& ln, const Lnast_nid& nid) {
  const auto t = ln.get_type(nid);
  return Lnast_ntype::is_func_call(t) || Lnast_ntype::is_tuple_add(t) || Lnast_ntype::is_tuple_concat(t);
}

// Free-variable collection over a loop body. One walk classifies every NON-tmp
// name as read / written / declared-local. Key stores (see above) are neither.
struct Body_vars {
  absl::flat_hash_set<std::string> read;
  absl::flat_hash_set<std::string> written;
  absl::flat_hash_set<std::string> declared;
};

void collect_body_vars(const Lnast& ln, const Lnast_nid& nid, bool parent_makes_keys, Body_vars& out) {
  if (nid.is_invalid()) {
    return;
  }
  const auto t = ln.get_type(nid);
  if (Lnast_ntype::is_func_def(t)) {
    return;  // a nested lambda has its own scope
  }
  const bool is_call    = Lnast_ntype::is_func_call(t);
  const bool is_store   = Lnast_ntype::is_store(t);
  const bool is_declare = Lnast_ntype::is_declare(t);
  const bool is_for     = Lnast_ntype::is_for(t);
  // Child 0 of a defining statement names its target, not a read. Every
  // value-producing primitive op defines its child 0 too (`set_mask(x, x, r,
  // v)` WRITES x: the slang bit-write lowering updates a function-local vector
  // in place).
  const bool value_op   = Lnast_ntype::is_primitive_op(t) && !Lnast_ntype::is_attr_set(t) && !is_store && !is_declare
                        && !Lnast_ntype::is_stages(t) && !Lnast_ntype::is_timecheck(t) && !Lnast_ntype::is_ref(t)
                        && !Lnast_ntype::is_const(t);
  const bool defines = is_store || is_declare || is_call || is_for || Lnast_ntype::is_tuple_add(t) || Lnast_ntype::is_attr_set(t)
                       || Lnast_ntype::is_tuple_get(t) || value_op;

  const auto target = ln.get_first_child(nid);
  const auto target_name
      = (!target.is_invalid() && Lnast_ntype::is_ref(ln.get_type(target))) ? ln.get_name(target) : std::string_view{};
  if (!target_name.empty() && !prp_is_tmp_name(target_name)) {
    if (is_declare || is_for) {
      out.declared.emplace(target_name);
    } else if ((is_store || value_op) && !parent_makes_keys) {
      out.written.emplace(target_name);  // a store under a call/tuple is a key
    }
  }

  const bool child_keys = makes_store_keys(ln, nid);
  int        idx        = 0;
  for (auto c : ln.children(nid)) {
    const bool skip = (defines && idx == 0) || (is_call && idx == 1);  // target, callee
    if (!skip && Lnast_ntype::is_ref(ln.get_type(c))) {
      const auto nm = ln.get_name(c);
      if (!nm.empty() && !prp_is_tmp_name(nm)) {
        out.read.emplace(nm);
      }
    }
    collect_body_vars(ln, c, child_keys, out);
    ++idx;
  }
}

// True when some `store` OUTSIDE `skip` (the loop body) writes `name`. A carry
// needs a value entering the loop; a variable written ONLY inside the body has
// none, and the emitted initial actual would reference an undriven name.
//
// A KEY store is not a write (see makes_store_keys). That exemption is what
// makes this guard mean anything for a module port: an io declaration is
// `io -> tuple_add -> store(ref(name), const(nil), <type>)`, the same shape,
// so without it EVERY port name answered true and the refusal never fired for
// `-> (z:u12)` written only inside the loop.
bool written_outside(const Lnast& ln, const Lnast_nid& nid, const Lnast_nid& skip, std::string_view name,
                     bool parent_makes_keys = false) {
  if (nid.is_invalid() || nid == skip) {
    return false;
  }
  if (!parent_makes_keys && Lnast_ntype::is_store(ln.get_type(nid))) {
    auto tgt = ln.get_first_child(nid);
    if (!tgt.is_invalid() && Lnast_ntype::is_ref(ln.get_type(tgt)) && ln.get_name(tgt) == name) {
      return true;
    }
  }
  const bool child_keys = makes_store_keys(ln, nid);
  for (auto c : ln.children(nid)) {
    if (Lnast_ntype::is_func_def(ln.get_type(c))) {
      continue;  // a nested lambda has its own scope: its writes are not this scope's
    }
    if (written_outside(ln, c, skip, name, child_keys)) {
      return true;
    }
  }
  return false;
}

// True when a write to `name` REACHES the loop, i.e. some `store` that precedes
// `loop` in program order writes it. Order matters: a store that only happens
// AFTER the loop is not a value entering ordinal 0, and accepting it emits a
// `<name>__carry_in = ref(<name>)` actual against a name with no driver yet --
// exactly what the "written only inside the loop" refusal exists to prevent.
// Everything under `loop` (including the body) is excluded by construction.
bool written_before(const Lnast& ln, const Lnast_nid& loop, std::string_view name) {
  for (auto node = loop; !node.is_invalid(); node = ln.get_parent(node)) {
    const auto parent = ln.get_parent(node);
    if (parent.is_invalid()) {
      break;
    }
    if (Lnast_ntype::is_func_def(ln.get_type(parent))) {
      break;  // a lambda boundary: an enclosing scope's writes are not this one's
    }
    const bool parent_keys = makes_store_keys(ln, parent);
    for (auto sib : ln.children(parent)) {
      if (sib == node) {
        break;  // only the siblings BEFORE this level's ancestor have run
      }
      if (Lnast_ntype::is_func_def(ln.get_type(sib))) {
        continue;
      }
      if (written_outside(ln, sib, Lnast_nid{}, name, parent_keys)) {
        return true;
      }
    }
  }
  return false;
}

// Conservative must-write proof for the final-only class. A straight-line
// store establishes the value. An if/match does so only when it has an else
// arm and every arm establishes it. Anything more involved (nested loops,
// calls returning into the name, partial tuple stores) remains a carry or
// falls back to source unrolling; guessing here would expose an undriven last
// occurrence on one control path.
bool body_must_write(const Lnast& ln, const Lnast_nid& nid, std::string_view name) {
  if (nid.is_invalid()) {
    return false;
  }
  const auto type = ln.get_type(nid);
  if (Lnast_ntype::is_store(type)) {
    auto target = ln.get_first_child(nid);
    if (target.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(target)) || ln.get_name(target) != name) {
      return false;
    }
    const auto value = ln.get_sibling_next(target);
    return !value.is_invalid() && ln.get_sibling_next(value).is_invalid();  // whole-value store, not tuple/bit field
  }
  if (Lnast_ntype::is_stmts(type)) {
    for (auto c : ln.children(nid)) {
      if (body_must_write(ln, c, name)) {
        return true;
      }
    }
    return false;
  }
  if (Lnast_ntype::is_if(type) || Lnast_ntype::is_unique_if(type)) {
    std::vector<Lnast_nid> arms;
    int                    children      = 0;
    bool                   last_is_stmts = false;
    for (auto c : ln.children(nid)) {
      ++children;
      last_is_stmts = Lnast_ntype::is_stmts(ln.get_type(c));
      if (last_is_stmts) {
        arms.emplace_back(c);
      }
    }
    // Layout is cond,stmts[,cond,stmts]...,stmts(else). Therefore an odd
    // child count ending in stmts is the exhaustive form.
    if (children == 0 || (children % 2) == 0 || !last_is_stmts || arms.empty()) {
      return false;
    }
    return std::ranges::all_of(arms, [&](const Lnast_nid& arm) { return body_must_write(ln, arm, name); });
  }
  return false;
}

// True when the subtree holds a `break` or `continue`. The roller uses this to
// select the per-occurrence `next_active` ABI and predicate the lifted body;
// nested loops own their control independently later in the classification.
bool subtree_has_loop_control(const Lnast& ln, const Lnast_nid& nid, bool is_root = true) {
  if (nid.is_invalid()) {
    return false;
  }
  const auto t = ln.get_type(nid);
  if (Lnast_ntype::is_func_def(t)) {
    return false;
  }
  // `break`/`continue` bind to the INNERMOST enclosing loop, so a nested
  // `for`/`while` owns everything under it — it lowers inside the lifted
  // definition and mints its own activation there. Counting it here would
  // predicate THIS body under a `__next_active` that copy_controlled_stmt never
  // writes (a constant copy of `__valid`), and would also block the `finals`
  // class for names this loop must-writes.
  if (!is_root && (Lnast_ntype::is_for(t) || Lnast_ntype::is_while(t))) {
    return false;
  }
  if (Lnast_ntype::is_func_break(t) || Lnast_ntype::is_func_continue(t)) {
    return true;
  }
  for (auto c : ln.children(nid)) {
    if (subtree_has_loop_control(ln, c, /*is_root=*/false)) {
      return true;
    }
  }
  return false;
}

bool subtree_has_call(const Lnast& ln, const Lnast_nid& nid) {
  if (nid.is_invalid()) {
    return false;
  }
  const auto type = ln.get_type(nid);
  if (Lnast_ntype::is_func_def(type)) {
    return false;
  }
  if (Lnast_ntype::is_func_call(type)) {
    return true;
  }
  for (auto c : ln.children(nid)) {
    if (subtree_has_call(ln, c)) {
      return true;
    }
  }
  return false;
}

// The default source-unroll path already handles a break/continue whose guard
// becomes comptime after binding the iteration variable. Route only genuinely
// runtime control through the activation representation when general rolling
// is disabled. LNAST conditions are three-address refs, so follow temporary
// producers back to their operands: `i == 6` depends only on the loop index,
// while `stop#[i]` reaches the unresolved input `stop` and is runtime.
bool subtree_has_runtime_loop_control(const Lnast& ln, const Lnast_nid& body_stmts, std::string_view ivar) {
  absl::flat_hash_map<std::string, Lnast_nid> temp_defs;
  std::function<void(const Lnast_nid&)>       collect_defs = [&](const Lnast_nid& nid) {
    if (nid.is_invalid() || Lnast_ntype::is_func_def(ln.get_type(nid))) {
      return;
    }
    auto first = ln.get_first_child(nid);
    if (!first.is_invalid() && Lnast_ntype::is_ref(ln.get_type(first))) {
      const auto name = ln.get_name(first);
      if (prp_is_tmp_name(name)) {
        // SSA temporaries have one real producer. Control nodes (`if`, `for`,
        // `break`, ...) may also happen to have a ref as child zero, but that
        // ref is an operand/binding rather than a destination. Since the
        // producer precedes every use in the body, keep the first statement
        // that claims the name instead of letting a later control use replace
        // it (notably `if %cond { break }`).
        temp_defs.try_emplace(std::string(name), nid);
      }
    }
    for (auto c : ln.children(nid)) {
      collect_defs(c);
    }
  };
  collect_defs(body_stmts);

  absl::flat_hash_set<std::string>      visiting;
  std::function<bool(const Lnast_nid&)> is_iteration_static = [&](const Lnast_nid& nid) -> bool {
    if (nid.is_invalid()) {
      return true;
    }
    const auto type = ln.get_type(nid);
    if (Lnast_ntype::is_const(type)) {
      return true;
    }
    if (Lnast_ntype::is_ref(type)) {
      const auto name = ln.get_name(nid);
      if (name == ivar) {
        return true;
      }
      auto it = temp_defs.find(std::string(name));
      if (it == temp_defs.end() || !visiting.insert(std::string(name)).second) {
        return false;
      }
      const bool result = is_iteration_static(it->second);
      visiting.erase(std::string(name));
      return result;
    }
    int  child_index = 0;
    bool result      = true;
    for (auto c : ln.children(nid)) {
      // A three-address statement's first child is its destination, not an
      // operand. The condition itself may be a leaf, handled above.
      if (child_index++ == 0 && Lnast_ntype::is_ref(ln.get_type(c))) {
        continue;
      }
      result = result && is_iteration_static(c);
    }
    return result;
  };

  std::function<bool(const Lnast_nid&, bool)> scan = [&](const Lnast_nid& nid, bool runtime_guard) -> bool {
    if (nid.is_invalid() || Lnast_ntype::is_func_def(ln.get_type(nid))) {
      return false;
    }
    const auto type = ln.get_type(nid);
    if (Lnast_ntype::is_func_break(type) || Lnast_ntype::is_func_continue(type)) {
      return runtime_guard;
    }
    // A nested range loop owns its control statements, but an enclosing loop
    // that source-unrolls first would put the inner body in a salted frame;
    // lifted definitions deliberately refuse to copy such frame-renamed names.
    // Detect the inner loop with ITS iteration variable so the outer loop is
    // lifted first, then the inner loop can lower normally in that generated
    // definition. This is what makes nested runtime break compositional.
    if (nid != body_stmts && Lnast_ntype::is_for(type)) {
      Lnast_nid nested_ivar;
      Lnast_nid nested_body;
      int       child_index = 0;
      for (auto c : ln.children(nid)) {
        if (child_index == 0) {
          nested_ivar = c;
        } else if (child_index == 2) {
          nested_body = c;
          break;
        }
        ++child_index;
      }
      if (!nested_ivar.is_invalid() && !nested_body.is_invalid()) {
        return subtree_has_runtime_loop_control(ln, nested_body, ln.get_name(nested_ivar));
      }
      return false;
    }
    if (nid != body_stmts && Lnast_ntype::is_while(type)) {
      return false;
    }
    if (Lnast_ntype::is_if(type) || Lnast_ntype::is_unique_if(type)) {
      bool any_runtime_cond = false;
      for (auto c : ln.children(nid)) {
        if (Lnast_ntype::is_stmts(ln.get_type(c))) {
          if (scan(c, runtime_guard || any_runtime_cond)) {
            return true;
          }
        } else {
          any_runtime_cond = any_runtime_cond || !is_iteration_static(c);
        }
      }
      return false;
    }
    for (auto c : ln.children(nid)) {
      if (scan(c, runtime_guard)) {
        return true;
      }
    }
    return false;
  };
  return scan(body_stmts, false);
}

// A loop NESTED in the body whose domain reads the index or a carry cannot
// survive rolling: inside the lifted definition those names are runtime ports,
// and a `for` over a runtime range is a hard tolg error ("non-comptime `for`
// loop"). The matched-filter tree is the canonical shape — `for lvl { for j in
// 0..<(N >> (lvl+1)) … }` — where the OUTER loop must unroll (its body's inner
// domain reads `lvl`) while each inner copy, left with a constant domain, rolls.
// A generic bind or an integer type bound that reads them needs the unroll too.
bool nested_loop_domain_reads_any(const Lnast& ln, const Lnast_nid& body_stmts, const std::vector<std::string>& names) {
  if (names.empty()) {
    return false;
  }
  // Statement-order taint: a name is runtime inside the lifted body if it IS
  // the index / a carry, or is computed from one (`%t = N >> (lvl + 1)` makes
  // `%t` runtime, and `range(%d, 0, %t)` is then a runtime domain). prp2lnast
  // emits the domain as a `range` statement ahead of the `for`, possibly through
  // several tmps, so the walk must follow dataflow to the `for`'s own iterable
  // ref -- a `range` node by itself is NOT evidence of anything (a bit-select
  // with non-literal endpoints is spelled with one too).
  absl::flat_hash_set<std::string> tainted(names.begin(), names.end());
  const auto                       any_read_tainted = [&](const Lnast_nid& stmt, bool skip_first) {
    bool first = true;
    for (auto c : ln.children(stmt)) {
      if (first && skip_first) {
        first = false;
        continue;
      }
      first = false;
      if (Lnast_ntype::is_ref(ln.get_type(c)) && tainted.contains(std::string(ln.get_name(c)))) {
        return true;
      }
    }
    return false;
  };
  std::function<bool(const Lnast_nid&)> walk = [&](const Lnast_nid& stmts) -> bool {
    for (auto stmt : ln.children(stmts)) {
      const auto t = ln.get_type(stmt);
      if (Lnast_ntype::is_func_def(t)) {
        continue;
      }
      if (Lnast_ntype::is_stmts(t)) {
        if (walk(stmt)) {
          return true;
        }
        continue;
      }
      // A nested `for` is the whole point of this walk: its ITERABLE (child 1;
      // child 0 is the index it binds) is the domain, and a tainted domain is
      // the shape that cannot survive rolling. Test the iterable itself rather
      // than every `range` statement in the body -- prp2lnast also emits
      // `range` for a non-literal bit-select (`x#[(i*8)..=(i*8+7)]`) and for a
      // tuple slice, and matching those refused the roll for a loop that merely
      // slices with its index, which then falls through to the source unroller
      // and hard-errors on a runtime `break`. The domain still reaches the
      // iterable through the generic taint below, however many tmps it takes.
      if (Lnast_ntype::is_for(t) && any_read_tainted(stmt, /*skip_first=*/true)) {
        return true;
      }
      // Same rule for an explicit generic bind (`add_n<N=(SIZE >> (lvl+1))>`):
      // a generic is a COMPTIME parameter, so its actual must be a constant
      // where the call sits. Inside the lifted body the index / a carry / an
      // invariant is a runtime port, so a tainted bind cannot fold -- the callee
      // then reaches tolg with an undriven `inl<n>_<G>`. prp2lnast rides each
      // bind in a `store(__generic_arg, value)` child of the func_call, one
      // level below the statement, so `any_read_tainted` (direct children only)
      // does not see it.
      if (Lnast_ntype::is_func_call(t)) {
        // `std.clog2(x)` is comptime only (docs 04b-attributes): inside the
        // lifted body a tainted `x` is a runtime port and the call would fail
        // "not a compile-time value" on source that unrolls fine
        // (`for k in 1..=8 { acc += std.clog2(k) }`).
        if (const auto callee = ln.get_sibling_next(ln.get_first_child(stmt));
            !callee.is_invalid() && Lnast_ntype::is_ref(ln.get_type(callee)) && ln.get_name(callee) == upass::std_clog2_callee
            && any_read_tainted(stmt, /*skip_first=*/true)) {
          return true;
        }
        for (auto arg : ln.children(stmt)) {
          if (!Lnast_ntype::is_store(ln.get_type(arg))) {
            continue;
          }
          auto key = ln.get_first_child(arg);
          if (key.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(key)) || ln.get_name(key) != call_generic_arg_marker) {
            continue;
          }
          auto val = ln.get_sibling_next(key);
          if (!val.is_invalid() && Lnast_ntype::is_ref(ln.get_type(val)) && tainted.contains(std::string(ln.get_name(val)))) {
            return true;
          }
        }
      }
      // Same rule for an integer type BOUND (`const t:unsigned(bits=i)`, the
      // lane of `wrap a#[0..+i] = ..`): the runner bakes it where the
      // declaration sits, so it must fold there. It hangs below the type child
      // (child 1: `prim_type_int(max, min)`, an array's dims), out of
      // any_read_tainted's reach.
      if (Lnast_ntype::is_declare(t) || Lnast_ntype::is_type_spec(t)) {
        const auto                            first = ln.get_first_child(stmt);
        std::function<bool(const Lnast_nid&)> reads = [&](const Lnast_nid& nid) {
          if (Lnast_ntype::is_ref(ln.get_type(nid)) && tainted.contains(std::string(ln.get_name(nid)))) {
            return true;
          }
          for (auto c : ln.children(nid)) {
            if (reads(c)) {
              return true;
            }
          }
          return false;
        };
        if (const auto type_nid = first.is_invalid() ? first : ln.get_sibling_next(first);
            !type_nid.is_invalid() && reads(type_nid)) {
          return true;
        }
      }
      // Compound statements (if / for / while …): their nested stmts blocks
      // are walked with the same taint set; their own condition refs are
      // ordinary reads.
      bool compound = false;
      for (auto c : ln.children(stmt)) {
        if (Lnast_ntype::is_stmts(ln.get_type(c))) {
          compound = true;
          if (walk(c)) {
            return true;
          }
        }
      }
      if (compound) {
        continue;
      }
      // Three-address statement: dst = first ref child; taint it when any
      // operand is tainted.
      auto dst = ln.get_first_child(stmt);
      if (!dst.is_invalid() && Lnast_ntype::is_ref(ln.get_type(dst)) && any_read_tainted(stmt, /*skip_first=*/true)) {
        tainted.insert(std::string(ln.get_name(dst)));
      }
    }
    return false;
  };
  return walk(body_stmts);
}

bool later_loop_domain_reads_any(const Lnast& ln, const Lnast_nid& body_stmts, const std::vector<std::string>& names) {
  if (names.empty()) {
    return false;
  }
  absl::flat_hash_set<std::string_view> wanted;
  for (const auto& name : names) {
    wanted.insert(name);
  }
  const auto reads_wanted = [&](const Lnast_nid& root) {
    std::function<bool(const Lnast_nid&)> scan = [&](const Lnast_nid& nid) {
      if (nid.is_invalid()) {
        return false;
      }
      if (Lnast_ntype::is_ref(ln.get_type(nid)) && wanted.contains(ln.get_name(nid))) {
        return true;
      }
      for (auto child : ln.children(nid)) {
        if (scan(child)) {
          return true;
        }
      }
      return false;
    };
    return scan(root);
  };

  const auto loop = ln.get_parent(body_stmts);
  if (loop.is_invalid()) {
    return false;
  }
  std::function<bool(const Lnast_nid&)> contains_domain_read = [&](const Lnast_nid& nid) {
    if (nid.is_invalid() || Lnast_ntype::is_func_def(ln.get_type(nid))) {
      return false;
    }
    // A `range` statement IS a loop domain in flight: prp2lnast lowers
    // `for j in lo..<n` to `range(%t, lo, n)` emitted BEFORE the for, leaving
    // only `ref(%t)` in the for's own children (see process_for_statement,
    // "emits range/tuple stmts before the for; returns the tmp ref"). Checking
    // just the for children therefore never sees `n` and the refusal cannot
    // fire for the ONLY domain spelling that can be non-comptime. Matching the
    // range node itself also covers a domain built through several tmps.
    // Non-domain ranges (a tuple slice `t[a..<b]`) are matched too; that costs
    // at most a declined roll, never a wrong compile.
    if (Lnast_ntype::is_range(ln.get_type(nid))) {
      return reads_wanted(nid);
    }
    if (Lnast_ntype::is_for(ln.get_type(nid))) {
      int child_index = 0;
      for (auto child : ln.children(nid)) {
        if (child_index++ < 2 && reads_wanted(child)) {
          return true;
        }
        if (contains_domain_read(child)) {
          return true;  // a nested loop in this loop's body
        }
      }
      return false;
    }
    for (auto child : ln.children(nid)) {
      if (contains_domain_read(child)) {
        return true;
      }
    }
    return false;
  };
  // Walk the ENCLOSING chain, not just this loop's own sibling list: a loop
  // nested in an `if` (or any other block) has a sibling chain that ends inside
  // that block, so a later comptime loop in the outer scope would never be
  // reached and the refusal could not fire. Sibling arms of an enclosing `if`
  // are scanned too — over-approximating costs at most a declined roll.
  for (auto node = loop; !node.is_invalid(); node = ln.get_parent(node)) {
    const auto parent = ln.get_parent(node);
    if (parent.is_invalid() || Lnast_ntype::is_func_def(ln.get_type(parent))) {
      break;  // a lambda boundary: the outer scope's statements are not "later" here
    }
    for (auto stmt = ln.get_sibling_next(node); !stmt.is_invalid(); stmt = ln.get_sibling_next(stmt)) {
      if (contains_domain_read(stmt)) {
        return true;
      }
    }
  }
  return false;
}

// name -> every name its straight-line writes read (uPass_runner::Name_deps).
using Def_use = absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>;

// The definition `nid` belongs to: its nearest enclosing lambda, or the root.
Lnast_nid enclosing_definition(const Lnast& ln, const Lnast_nid& nid) {
  for (auto p = ln.get_parent(nid); !p.is_invalid(); p = ln.get_parent(p)) {
    if (Lnast_ntype::is_func_def(ln.get_type(p))) {
      return p;
    }
  }
  return ln.get_root();
}

// The declarations under `definition` (not inside a nested lambda, which has
// its own scope): every name declared as a `wire`, as a `reg`, and with an
// array type (`reg r:[4]u8`, also through a `type M = [4]u8` alias, itself a
// `type` declare). Shape: declare( ref(name), <type>, const(<class>) [, init] ).
void gather_loop_decls(const Lnast& ln, const Lnast_nid& definition, absl::flat_hash_set<std::string>& wires,
                       absl::flat_hash_set<std::string>& regs, absl::flat_hash_set<std::string>& arrays,
                       absl::flat_hash_set<std::string>& comptimes) {
  absl::flat_hash_map<std::string, std::vector<std::string>> typed_as;  // type alias -> names declared with it
  std::function<void(const Lnast_nid&)>                      walk = [&](const Lnast_nid& nid) {
    for (auto c : ln.children(nid)) {
      if (Lnast_ntype::is_func_def(ln.get_type(c))) {
        continue;
      }
      if (Lnast_ntype::is_declare(ln.get_type(c))) {
        const auto target = ln.get_first_child(c);
        if (!target.is_invalid() && Lnast_ntype::is_ref(ln.get_type(target))) {
          const std::string name(ln.get_name(target));
          const auto        type = ln.get_sibling_next(target);
          for (auto k = type; !k.is_invalid(); k = ln.get_sibling_next(k)) {
            if (Lnast_ntype::is_const(ln.get_type(k))) {
              const auto cls = ln.get_name(k);
              if (cls == "reg") {
                regs.emplace(name);
              } else if (cls == "wire" || cls.starts_with("wire ")) {
                wires.emplace(name);
              }
              if (cls.find("comptime") != std::string_view::npos) {
                comptimes.emplace(name);
              }
              break;
            }
          }
          if (!type.is_invalid() && Lnast_ntype::is_comp_type_array(ln.get_type(type))) {
            arrays.emplace(name);
          } else if (!type.is_invalid() && Lnast_ntype::is_ref(ln.get_type(type))) {
            typed_as[ln.get_name(type)].emplace_back(name);
          }
        }
      }
      walk(c);
    }
  };
  walk(definition);
  std::vector<std::string> work(arrays.begin(), arrays.end());
  while (!work.empty()) {
    const auto alias = std::move(work.back());
    work.pop_back();
    if (const auto it = typed_as.find(alias); it != typed_as.end()) {
      for (const auto& name : it->second) {
        if (arrays.emplace(name).second) {
          work.emplace_back(name);
        }
      }
    }
  }
}

// Whether any ref under `nid` (not inside a nested lambda) names one of `names`.
bool subtree_reads_any(const Lnast& ln, const Lnast_nid& nid, const absl::flat_hash_set<std::string>& names) {
  if (Lnast_ntype::is_func_def(ln.get_type(nid))) {
    return false;
  }
  if (Lnast_ntype::is_ref(ln.get_type(nid)) && names.contains(ln.get_name(nid))) {
    return true;
  }
  for (auto c : ln.children(nid)) {
    if (subtree_reads_any(ln, c, names)) {
      return true;
    }
  }
  return false;
}

// The straight-line def-use of `scope`'s statements into `deps`: for each
// statement's target, every name the statement reads (not its target, a
// call's callee or a named actual / tuple field label: names, not reads).
// if/match arms are straight-line; a loop body is not, since that loop may
// roll into an instance of its own -- except a loop that always unrolls (a
// `while`, or a body that reads a wire), whose body is straight-line again.
// The bodies of the loops that enclose the one being planned are unrolled
// too; they are gathered as scopes of their own.
void gather_straight_line_deps(const Lnast& ln, const Lnast_nid& scope, const absl::flat_hash_set<std::string>& wires,
                               Def_use& deps) {
  std::function<void(const Lnast_nid&, bool, bool, absl::flat_hash_set<std::string>&)> reads_of
      = [&](const Lnast_nid& nid, bool is_stmt, bool parent_keys, absl::flat_hash_set<std::string>& out) {
          const auto t     = ln.get_type(nid);
          const bool keys  = makes_store_keys(ln, nid);
          int        index = 0;
          for (auto c : ln.children(nid)) {
            const bool label = (index == 0 && (is_stmt || (parent_keys && Lnast_ntype::is_store(t))))
                               || (index == 1 && Lnast_ntype::is_func_call(t));
            if (!label && Lnast_ntype::is_ref(ln.get_type(c))) {
              out.emplace(ln.get_name(c));
            }
            reads_of(c, false, keys, out);
            ++index;
          }
        };
  std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
    for (auto stmt : ln.children(nid)) {
      const auto t = ln.get_type(stmt);
      if (Lnast_ntype::is_func_def(t)) {
        continue;
      }
      const bool is_loop  = Lnast_ntype::is_for(t) || Lnast_ntype::is_while(t) || Lnast_ntype::is_rolled_for(t);
      const bool unrolled = Lnast_ntype::is_while(t) || (Lnast_ntype::is_for(t) && subtree_reads_any(ln, stmt, wires));
      if (Lnast_ntype::is_stmts(t) || Lnast_ntype::is_if_like(t) || unrolled) {
        walk(stmt);  // conditions are refs, not statements: only the nested blocks count
        continue;
      }
      const auto target = ln.get_first_child(stmt);
      if (!is_loop && !target.is_invalid() && Lnast_ntype::is_ref(ln.get_type(target))) {
        reads_of(stmt, true, false, deps[std::string(ln.get_name(target))]);
      }
    }
  };
  walk(scope);
}

// A combinational RING around a rolled loop instance: a value the loop
// PRODUCES flows back into the value one of its inputs (`seeds`: an invariant,
// or a carry's ordinal-0 value) takes, e.g.
//   wire w:u16 = nil
//   const wc = w
//   for i in 0..<4 { ... wc ... ; pv#[(i*4)..+4] = ... }
//   w = pv
// Only a `wire` lets a value flow backwards in program order, so a ring path
// crosses one. Bit-level the ring may be acyclic, and the instance can carry
// it (graph_util::closes_loop_self_edge keeps it an ordinary external edge),
// but the netlist then holds a word-level loop through the instance that the
// unrolled form does not, so such a loop unrolls. `deps` are the straight-line
// def-use maps of the definition and of the loops enclosing this one;
// flow-insensitive, so over-reporting only costs an unrolled loop. A ring the
// maps cannot see (through a later loop that declines to roll, or through the
// caller of an inlined comb) still compiles rolled.
bool loop_output_rings_back(const std::vector<const Def_use*>& deps, const absl::flat_hash_set<std::string>& wires,
                            const std::vector<std::string>& seeds, const std::vector<std::string>& produced) {
  const absl::flat_hash_set<std::string_view>     out(produced.begin(), produced.end());
  std::array<absl::flat_hash_set<std::string>, 2> seen;  // [crossed a wire]
  std::vector<std::pair<std::string, bool>>       work;
  for (const auto& n : seeds) {
    work.emplace_back(n, false);
  }
  while (!work.empty()) {
    auto [name, crossed] = std::move(work.back());
    work.pop_back();
    crossed = crossed || wires.contains(name);
    if (crossed && out.contains(name)) {
      return true;
    }
    if (!seen[crossed].insert(name).second) {
      continue;
    }
    for (const auto* d : deps) {
      if (const auto it = d->find(name); it != d->end()) {
        for (const auto& src : it->second) {
          work.emplace_back(src, crossed);
        }
      }
    }
  }
  return false;
}

// Whether a carry the body reads reaches a whole-value write into a typed
// integer (`typed`: the typed carries/finals and the body's typed locals)
// that is not an explicit wrap/sat, by statement-order taint. Without derived
// carry ranges (eval_carry_ranges gave up), such a write would be judged in
// the lifted body with the carry-in at its declared type -- stricter than any
// unrolled iteration (`cnt = cnt + x#[i]` eight times into a `u4` fits every
// iteration, yet `u4 + 1` does not), so the loop stays unrolled.
bool carry_feeds_range_check(const Lnast& ln, const Lnast_nid& body_stmts, const std::vector<std::string>& carries_read,
                             absl::flat_hash_set<std::string> typed) {
  if (carries_read.empty()) {
    return false;
  }
  absl::flat_hash_set<std::string> tainted(carries_read.begin(), carries_read.end());
  absl::flat_hash_set<std::string> narrowed;  // wrap/sat results: their store is the explicit narrowing
  const auto                       ref_name = [&](const Lnast_nid& n) -> std::string_view {
    return !n.is_invalid() && Lnast_ntype::is_ref(ln.get_type(n)) ? ln.get_name(n) : std::string_view{};
  };
  std::function<bool(const Lnast_nid&)> walk = [&](const Lnast_nid& stmts) -> bool {
    for (auto stmt : ln.children(stmts)) {
      const auto t = ln.get_type(stmt);
      if (Lnast_ntype::is_func_def(t)) {
        continue;
      }
      bool compound = Lnast_ntype::is_stmts(t);
      if (compound) {
        if (walk(stmt)) {
          return true;
        }
        continue;
      }
      for (auto c : ln.children(stmt)) {
        if (Lnast_ntype::is_stmts(ln.get_type(c))) {
          compound = true;
          if (walk(c)) {
            return true;
          }
        }
      }
      if (compound) {
        continue;
      }
      const auto dst_nid = ln.get_first_child(stmt);
      const auto dst     = ref_name(dst_nid);
      if (Lnast_ntype::is_declare(t)) {
        const auto ty = dst_nid.is_invalid() ? dst_nid : ln.get_sibling_next(dst_nid);
        if (!dst.empty() && !ty.is_invalid()
            && (Lnast_ntype::is_prim_type_int(ln.get_type(ty)) || Lnast_ntype::is_ref(ln.get_type(ty)))) {
          typed.emplace(dst);
        }
        continue;
      }
      if (Lnast_ntype::is_func_call(t)) {
        // A call's result carries the taint of its arguments; a wrap/sat
        // result is the explicit narrowing of its store.
        const auto callee = dst_nid.is_invalid() ? dst_nid : ln.get_sibling_next(dst_nid);
        const auto name   = ref_name(callee);
        bool       reads  = false;
        for (auto arg = callee.is_invalid() ? callee : ln.get_sibling_next(callee); !arg.is_invalid();
             arg      = ln.get_sibling_next(arg)) {
          const auto key = Lnast_ntype::is_store(ln.get_type(arg)) ? ln.get_first_child(arg) : Lnast_nid{};
          const auto val = ref_name(key.is_invalid() ? arg : ln.get_sibling_next(key));
          reads          = reads || (!val.empty() && tainted.contains(val));
        }
        if (!dst.empty()) {
          if (reads) {
            tainted.emplace(dst);
          }
          if (name == "wrap" || name == "sat") {
            narrowed.emplace(dst);
          }
        }
        continue;
      }
      if (dst.empty()) {
        continue;
      }
      bool reads = false;
      for (auto c = ln.get_sibling_next(dst_nid); !c.is_invalid(); c = ln.get_sibling_next(c)) {
        const auto r = ref_name(c);
        reads        = reads || (!r.empty() && tainted.contains(r));
      }
      if (!reads) {
        continue;
      }
      tainted.emplace(dst);
      // A whole-value store (`store(dst, v)`) or an op writing its result
      // straight into the name; a field / element store is not a scalar write.
      const bool scalar_store = !Lnast_ntype::is_store(t) || ln.get_sibling_next(ln.get_sibling_next(dst_nid)).is_invalid();
      const auto value        = Lnast_ntype::is_store(t) ? ref_name(ln.get_sibling_next(dst_nid)) : std::string_view{};
      if (scalar_store && typed.contains(dst) && !narrowed.contains(value)) {
        return true;
      }
    }
    return false;
  };
  return walk(body_stmts);
}

// ── Plan-time carry ranges ──────────────────────────────────────────────────
// Rolling compiles the body ONCE, with each carry entering as an input port;
// unrolling lets upass.bitwidth see the range every iteration actually has.
// This interval evaluation of the raw body (the same Lnast_range lattice and
// op rules as upass.bitwidth, over a small straight-line subset) stands in for
// the unrolled analysis at plan time: it yields, per carry, the union of the
// values that ENTER an iteration (the lifted body seeds its carry-in port's
// range with it, as it does for the index), and tells whether one walk over
// that union would judge some typed write stricter than every single iteration
// does. A nested `for` over a comptime range is iterated in place; a runtime
// trip count (`while`) gives up; any other value it does not model (a tuple
// op, an opaque call) is unbounded, and a carry whose union ends up unbounded
// is simply not seeded.
class Loop_range_eval {
public:
  using Env        = absl::flat_hash_map<std::string, Lnast_range>;
  using Callee_out = std::function<std::optional<Lnast_range>(std::string_view)>;

  // `elem_typed`: the ELEMENT type of each typed array (an element store is
  // judged against it, as upass.bitwidth's check_array_elem_fit does).
  Loop_range_eval(const Lnast& ln, Env typed, Env elem_typed, Callee_out callee_out)
      : ln_(ln), typed_(std::move(typed)), elem_typed_(std::move(elem_typed)), callee_out_(std::move(callee_out)) {}

  // Typed writes whose value left (overflow) or could not be bounded against
  // (unknown) their declared range, over every evaluation so far.
  absl::flat_hash_set<Lnast_nid> overflow;
  absl::flat_hash_set<Lnast_nid> unknown;

  // One pass over `stmts`: `env` in, the values at its end out; every early
  // exit (break/continue) point is joined in. False when the budget ran out.
  bool run(const Lnast_nid& stmts, Env& env) {
    exits_.clear();
    if (!eval_block(stmts, env)) {
      return false;
    }
    for (const auto& x : exits_) {
      join_into(env, x, &env);
    }
    return true;
  }

  static Lnast_range join(const Lnast_range& a, const Lnast_range& b) {
    return a.is_unbounded() || b.is_unbounded() ? Lnast_range::make_unbounded() : a.join(b);
  }

private:
  const Lnast&                                                          ln_;
  Env                                                                   typed_;
  Env                                                                   elem_typed_;
  absl::flat_hash_set<std::string>                                      multi_dim_;  // body arrays of 2+ dimensions
  Callee_out                                                            callee_out_;
  absl::flat_hash_set<std::string>                                      narrowed_;  // wrap/sat results
  absl::flat_hash_map<std::string, std::pair<Lnast_range, Lnast_range>> ranges_;    // `range` temps: lo, hi
  absl::flat_hash_map<std::string, Lnast_nid>                           bound_defs_;
  std::vector<Env>                                                      exits_;
  int64_t                                                               budget_ = 1 << 19;  // statement evaluations

  // `pre` holds the value a path that skipped the arm keeps.
  static void join_into(Env& acc, const Env& other, const Env* pre) {
    for (auto& [name, r] : acc) {
      if (const auto it = other.find(name); it != other.end()) {
        r = join(r, it->second);
      } else if (pre != nullptr && !pre->contains(name)) {
        r = Lnast_range::make_unbounded();  // arm-local on one side only
      }
    }
    for (const auto& [name, r] : other) {
      if (!acc.contains(name)) {
        const auto it = pre != nullptr ? pre->find(name) : other.end();
        acc.emplace(name, pre != nullptr && it != pre->end() ? join(r, it->second) : Lnast_range::make_unbounded());
      }
    }
  }

  std::string_view ref_name(const Lnast_nid& n) const {
    return !n.is_invalid() && Lnast_ntype::is_ref(ln_.get_type(n)) ? ln_.get_name(n) : std::string_view{};
  }

  Lnast_range val(const Lnast_nid& n, const Env& env) const {
    if (n.is_invalid()) {
      return Lnast_range::make_unbounded();
    }
    if (Lnast_ntype::is_const(ln_.get_type(n))) {
      const auto txt = ln_.get_name(n);
      if (txt == "true" || txt == "false") {
        return Lnast_range::boolean();
      }
      if (txt.size() >= 3 && txt[0] == '0' && (txt[1] == 's' || txt[1] == 'u') && txt[2] == 'b') {
        return Lnast_range::make_unbounded();  // a bit pattern is a force, not a value
      }
      const auto& v = Dlop::from_pyrope_cached(txt);
      if (!v.is_invalid() && v.is_integer() && !v.has_unknowns() && v.get_signed_bits() <= 62) {
        return Lnast_range::constant(v.to_just_i64());
      }
      return Lnast_range::make_unbounded();
    }
    const auto it = env.find(ref_name(n));
    return it == env.end() ? Lnast_range::make_unbounded() : it->second;
  }

  std::optional<std::pair<int64_t, int64_t>> window(const Lnast_nid& lo_node, const Lnast_nid& hi_node, const Env& env) const {
    const auto lo = val(lo_node, env);
    const auto hi = val(hi_node, env);
    if (lo.is_constant() && hi.is_constant() && lo.min >= 0 && hi.min > lo.min) {
      return std::pair{lo.min, hi.min - lo.min};
    }
    return std::nullopt;
  }

  // Recognize hi = lo + W even when the moving endpoint is not constant.
  // Only compiler temporaries enter bound_defs_: mutable source variables
  // cannot be substituted by a definition from an earlier statement.
  std::optional<int64_t> bound_offset(const Lnast_nid& hi, const Lnast_nid& lo, const Env& env, int depth = 4) const {
    if (!ref_name(hi).empty() && ref_name(hi) == ref_name(lo)) {
      return 0;
    }
    if (depth == 0) {
      return std::nullopt;
    }
    const auto it = bound_defs_.find(ref_name(hi));
    if (it == bound_defs_.end()) {
      return std::nullopt;
    }
    const auto stmt  = it->second;
    const auto left  = ln_.get_sibling_next(ln_.get_first_child(stmt));
    const auto right = ln_.get_sibling_next(left);
    if (right.is_invalid() || !ln_.get_sibling_next(right).is_invalid()) {
      return std::nullopt;
    }
    const auto rv = val(right, env);
    const auto lv = val(left, env);
    if (rv.is_constant() && rv.min >= -4096 && rv.min <= 4096) {
      if (auto offset = bound_offset(left, lo, env, depth - 1)) {
        return *offset + (Lnast_ntype::is_minus(ln_.get_type(stmt)) ? -rv.min : rv.min);
      }
    } else if (lv.is_constant() && lv.min >= -4096 && lv.min <= 4096 && Lnast_ntype::is_plus(ln_.get_type(stmt))) {
      if (auto offset = bound_offset(right, lo, env, depth - 1)) {
        return *offset + lv.min;
      }
    }
    return std::nullopt;
  }

  Lnast_range eval_op(Lnast_ntype::Lnast_ntype_int t, const std::vector<Lnast_nid>& ops, const Env& env) const {
    using N         = Lnast_ntype;
    const auto fold = [&](auto&& f) {
      if (ops.empty()) {
        return Lnast_range::make_unbounded();
      }
      auto r = val(ops[0], env);
      for (std::size_t i = 1; i < ops.size(); ++i) {
        r = f(r, val(ops[i], env));
      }
      return r;
    };
    if (N::is_plus(t)) {
      return fold([](const Lnast_range& a, const Lnast_range& b) { return a.add(b); });
    }
    if (N::is_mult(t)) {
      return fold([](const Lnast_range& a, const Lnast_range& b) { return a.mul(b); });
    }
    if (N::is_bit_and(t)) {
      return fold([](const Lnast_range& a, const Lnast_range& b) { return a.band(b); });
    }
    if (N::is_bit_or(t)) {
      return fold([](const Lnast_range& a, const Lnast_range& b) { return a.bor(b); });
    }
    if (N::is_bit_xor(t)) {
      return fold([](const Lnast_range& a, const Lnast_range& b) { return a.bxor(b); });
    }
    if (N::is_bit_not(t)) {
      if (ops.empty()) {
        return Lnast_range::make_unbounded();
      }
      if (ops.size() > 1) {  // the typed `~` (ruling 26): flips the stated N bits only
        const auto n = val(ops[1], env);
        return n.is_constant() ? val(ops[0], env).bnot_bits(n.min) : Lnast_range::make_unbounded();
      }
      return val(ops[0], env).bnot();
    }
    if (N::is_log_and(t) || N::is_log_or(t) || N::is_log_not(t) || N::is_red_or(t) || N::is_red_and(t) || N::is_red_xor(t)
        || N::is_eq(t) || N::is_ne(t) || N::is_lt(t) || N::is_le(t) || N::is_gt(t) || N::is_ge(t)) {
      return Lnast_range::boolean();
    }
    if (ops.size() < 2) {
      return Lnast_range::make_unbounded();
    }
    const auto a = val(ops[0], env);
    const auto b = val(ops[1], env);
    if (N::is_minus(t)) {
      return a.sub(b);
    }
    if (N::is_div(t)) {
      return a.div(b);
    }
    if (N::is_mod(t)) {
      return a.mod(b);
    }
    if (N::is_shl(t)) {
      return a.shl(b);
    }
    if (N::is_sra(t)) {
      return a.sra(b);
    }
    if (N::is_sext(t)) {
      return b.is_constant() ? Lnast_range::sext_to(b.min) : Lnast_range::make_unbounded();
    }
    if (N::is_get_mask(t)) {
      const auto w = ops.size() >= 3 ? window(ops[1], ops[2], env) : std::nullopt;
      if (!w && ops.size() >= 3) {
        if (const auto width = bound_offset(ops[2], ops[1], env); width && *width > 0 && *width < 62) {
          return Lnast_range::constant(0).join(Lnast_range::constant((int64_t{1} << *width) - 1));
        }
      }
      if (!w || w->second >= 62) {
        return Lnast_range::make_unbounded();
      }
      auto r = Lnast_range::constant(0).join(Lnast_range::constant((int64_t{1} << w->second) - 1));
      if (!a.is_unbounded() && a.min >= 0) {
        r.max = std::min(r.max, a.max >> w->first);
        if (w->first == 0 && a.max <= r.max) {
          r.min = a.min;
        }
      }
      return r;
    }
    if (N::is_set_mask(t)) {
      if (const auto w = ops.size() >= 4 ? window(ops[2], ops[3], env) : std::nullopt;
          w && !a.is_unbounded() && a.min >= 0 && w->first + w->second < 62) {
        const int64_t m = ((int64_t{1} << w->second) - 1) << w->first;
        return Lnast_range::constant(0).join(Lnast_range::constant(Lnast_range::ones_cover(std::max(a.max, m))));
      }
      if (const auto it = typed_.find(ref_name(ops[0])); it != typed_.end()) {
        return it->second;
      }
    }
    return Lnast_range::make_unbounded();
  }

  // A whole-value write of a typed name (an element write of a typed array):
  // judge it the way upass.bitwidth will.
  void judge(const Lnast_nid& stmt, std::string_view dst, const Lnast_range& r, std::string_view value, bool element = false) {
    const auto& types = element ? elem_typed_ : typed_;
    const auto  it    = types.find(dst);
    if (it == types.end() || narrowed_.contains(value)) {
      return;
    }
    if (r.is_unbounded()) {
      unknown.insert(stmt);
    } else if (!it->second.contains(r)) {
      overflow.insert(stmt);
    }
  }

  bool eval_block(const Lnast_nid& stmts, Env& env) {
    for (auto stmt : ln_.children(stmts)) {
      if (--budget_ < 0) {
        return false;
      }
      const auto t = ln_.get_type(stmt);
      if (Lnast_ntype::is_func_def(t) || Lnast_ntype::is_cassert(t) || Lnast_ntype::is_attr_set(t)
          || Lnast_ntype::is_type_spec(t)) {
        continue;
      }
      if (Lnast_ntype::is_stmts(t)) {
        if (!eval_block(stmt, env)) {
          return false;
        }
        continue;
      }
      if (Lnast_ntype::is_func_break(t) || Lnast_ntype::is_func_continue(t)) {
        exits_.push_back(env);
        continue;
      }
      if (Lnast_ntype::is_for(t)) {
        // for(value, iterable, stmts, mode, …) over a comptime `range`: its body
        // once per index value. Any other domain (a tuple, a runtime bound, a
        // `ref` slot walk) is not modeled.
        const auto var  = ln_.get_first_child(stmt);
        const auto iter = var.is_invalid() ? var : ln_.get_sibling_next(var);
        const auto body = iter.is_invalid() ? iter : ln_.get_sibling_next(iter);
        const auto mode = body.is_invalid() ? body : ln_.get_sibling_next(body);
        const auto rit  = ranges_.find(ref_name(iter));
        if (ref_name(var).empty() || rit == ranges_.end() || body.is_invalid() || !Lnast_ntype::is_stmts(ln_.get_type(body))
            || (!mode.is_invalid() && ln_.get_name(mode) == "ref")) {
          return false;
        }
        const auto& [lo, hi] = rit->second;
        if (!lo.is_constant() || !hi.is_constant()) {
          return false;
        }
        const std::string index(ref_name(var));
        for (int64_t x = lo.min; x <= hi.min; ++x) {
          if (--budget_ < 0) {
            return false;
          }
          env.insert_or_assign(index, Lnast_range::constant(x));
          const auto mark = exits_.size();
          if (!eval_block(body, env)) {
            return false;
          }
          // Exits taken in THIS nested iteration belong to the nested loop: a
          // continue reaches its next iteration, a break the statement after
          // it. Joining both into the running env over-approximates both
          // (sound); only exits outside any nested `for` stay for run().
          for (auto k = mark; k < exits_.size(); ++k) {
            join_into(env, exits_[k], &env);
          }
          exits_.resize(mark);
        }
        continue;
      }
      if (Lnast_ntype::is_while(t) || Lnast_ntype::is_rolled_for(t)) {
        return false;  // a runtime trip count: its iterations are not modeled
      }
      if (Lnast_ntype::is_if(t) || Lnast_ntype::is_unique_if(t)) {
        // (cond, stmts, cond, stmts, …, [stmts]): every arm may run; without a
        // trailing else the pre-if values survive too.
        int  conds = 0;
        int  arms  = 0;
        Env  merged;
        bool first = true;
        for (auto c : ln_.children(stmt)) {
          if (!Lnast_ntype::is_stmts(ln_.get_type(c))) {
            ++conds;
            continue;
          }
          ++arms;
          Env arm = env;
          if (!eval_block(c, arm)) {
            return false;
          }
          if (first) {
            merged = std::move(arm);
            first  = false;
          } else {
            join_into(merged, arm, &env);
          }
        }
        if (arms <= conds) {
          join_into(merged, env, &env);
        }
        if (!first) {
          env = std::move(merged);
        }
        continue;
      }
      const auto dst_nid = ln_.get_first_child(stmt);
      const auto dst     = std::string(ref_name(dst_nid));
      if (dst.empty()) {
        continue;
      }
      std::vector<Lnast_nid> ops;
      for (auto c = ln_.get_sibling_next(dst_nid); !c.is_invalid(); c = ln_.get_sibling_next(c)) {
        ops.push_back(c);
      }
      if (Lnast_ntype::is_declare(t) || Lnast_ntype::is_type_spec(t)) {
        // declare(var, prim_type_int(max, min), mode[, value]): the type a
        // later write is judged against.
        // An array declare, `comp_type_array(…(prim_type_int(max, min)), dim)`,
        // types its elements.
        auto       ty    = ops.empty() ? Lnast_nid{} : ops[0];
        const bool array = !ty.is_invalid() && Lnast_ntype::is_comp_type_array(ln_.get_type(ty));
        while (!ty.is_invalid() && Lnast_ntype::is_comp_type_array(ln_.get_type(ty))) {
          ty = ln_.get_first_child(ty);
        }
        if (array && Lnast_ntype::is_comp_type_array(ln_.get_type(ln_.get_first_child(ops[0])))) {
          multi_dim_.insert(dst);
        }
        if (!ty.is_invalid() && Lnast_ntype::is_prim_type_int(ln_.get_type(ty))) {
          const auto mx = val(ln_.get_first_child(ty), env);
          const auto mn = ln_.get_first_child(ty).is_invalid() ? Lnast_range::make_unbounded()
                                                               : val(ln_.get_sibling_next(ln_.get_first_child(ty)), env);
          if (mx.is_constant() && mn.is_constant() && mn.min <= mx.min) {
            const auto declared = Lnast_range::constant(mn.min).join(Lnast_range::constant(mx.min));
            (array ? elem_typed_ : typed_).insert_or_assign(dst, declared);
            if (Lnast_ntype::is_type_spec(t) && !array) {
              // A selection's type annotation refines its envelope; it must
              // not overwrite the value just inferred for this temporary.
              const auto current = env.find(dst);
              env.insert_or_assign(
                  dst,
                  current == env.end() || current->second.is_unbounded() ? declared : current->second.meet(declared));
            }
          }
        }
        continue;
      }
      if (Lnast_ntype::is_store(t)) {
        if (ops.size() == 1) {
          const auto r = val(ops[0], env);
          judge(stmt, dst, r, ref_name(ops[0]));
          env.insert_or_assign(dst, r);
        } else {
          const std::string base(Bundle::get_first_level(dst));
          judge(stmt, base, val(ops.back(), env), ref_name(ops.back()), /*element=*/true);
          env.insert_or_assign(base, Lnast_range::make_unbounded());
        }
        continue;
      }
      if (Lnast_ntype::is_range(t)) {
        // range(dst, lo, hi_inclusive); a stepped one is not modeled.
        if (ops.size() == 2) {
          ranges_.insert_or_assign(dst, std::pair{val(ops[0], env), val(ops[1], env)});
        }
        env.insert_or_assign(dst, Lnast_range::make_unbounded());
        continue;
      }
      if (Lnast_ntype::is_func_call(t)) {
        const auto callee = ops.empty() ? std::string_view{} : ref_name(ops[0]);
        auto       r      = Lnast_range::make_unbounded();
        if (callee == "wrap" || callee == "sat") {
          // wrap/sat(v=…, type=<target>): the target's declared range.
          for (std::size_t i = 1; i < ops.size(); ++i) {
            const auto key = Lnast_ntype::is_store(ln_.get_type(ops[i])) ? ln_.get_first_child(ops[i]) : Lnast_nid{};
            if (!key.is_invalid() && ln_.get_name(key) == "type") {
              if (const auto it = typed_.find(ref_name(ln_.get_sibling_next(key))); it != typed_.end()) {
                r = it->second;
              }
            }
          }
          narrowed_.insert(dst);
        } else if (const auto out = callee.empty() ? std::nullopt : callee_out_(callee)) {
          r = *out;
        }
        env.insert_or_assign(dst, r);
        continue;
      }
      if (Lnast_ntype::is_tuple_get(t) && ops.size() == 2) {
        // An element of a typed one-dimensional array: its element type bounds
        // it, as upass.bitwidth's process_tuple_get does (every element store
        // is held to that type).
        const std::string arr(Bundle::get_first_level(ref_name(ops[0])));
        if (const auto it = elem_typed_.find(arr); it != elem_typed_.end() && !multi_dim_.contains(arr)) {
          env.insert_or_assign(dst, it->second);
          continue;
        }
      }
      if (Lnast::is_tmp(dst) && (Lnast_ntype::is_plus(t) || Lnast_ntype::is_minus(t))) {
        bound_defs_.insert_or_assign(dst, stmt);
      }
      env.insert_or_assign(dst, eval_op(t, ops, env));
    }
    return true;
  }
};

struct Carry_ranges {
  bool                                          ok = false;  // every carry evaluated (possibly to unbounded)
  absl::flat_hash_map<std::string, Lnast_range> carry_in;    // the union of the values entering an iteration
  bool rolled_stricter = false;  // one walk over carry_in rejects a typed write no single iteration does
};

// Iterate the body over its carries: exactly, one index value per iteration,
// for a short loop; for a long one, the whole index domain at once until the
// carry unions stop growing (or give up).
Carry_ranges eval_carry_ranges(const Lnast& ln, const Lnast_nid& body, const std::string& ivar, int64_t first, int64_t step,
                               uint64_t count, const Loop_range_eval::Env& base, const Loop_range_eval::Env& carry_init,
                               const Loop_range_eval::Env& typed, const Loop_range_eval::Env& elem_typed,
                               const Loop_range_eval::Callee_out& callee_out) {
  Carry_ranges       out;
  Loop_range_eval    per_iter(ln, typed, elem_typed, callee_out);
  const int64_t      last    = static_cast<int64_t>(static_cast<__int128>(first) + static_cast<__int128>(count - 1) * step);
  const auto         domain  = Lnast_range::constant(std::min(first, last)).join(Lnast_range::constant(std::max(first, last)));
  auto               current = carry_init;  // what enters the next iteration
  auto&              u       = out.carry_in;
  constexpr uint64_t kExactIterations = 4096;
  const auto         join_in          = [&](const Loop_range_eval::Env& in) {
    bool grew = false;
    for (auto& [c, r] : u) {
      const auto joined = Loop_range_eval::join(r, in.at(c));
      grew              = grew || joined.is_unbounded() != r.is_unbounded() || joined.min != r.min || joined.max != r.max;
      r                 = joined;
    }
    return grew;
  };
  const auto carry_out = [&](const Loop_range_eval::Env& end) {
    for (auto& [c, r] : current) {
      if (const auto it = end.find(c); it != end.end()) {
        r = it->second;
      }
    }
  };
  u = carry_init;
  if (count <= kExactIterations) {
    for (uint64_t k = 0; k < count; ++k) {
      join_in(current);
      auto env = base;
      env.insert_or_assign(
          ivar,
          Lnast_range::constant(static_cast<int64_t>(static_cast<__int128>(first) + static_cast<__int128>(k) * step)));
      for (const auto& [c, r] : current) {
        env.insert_or_assign(c, r);
      }
      if (!per_iter.run(body, env)) {
        return out;
      }
      carry_out(env);
    }
  } else {
    // Every iteration's carry-in is some earlier iteration's carry-out.
    bool converged = false;
    for (int round = 0; round < 64 && !converged; ++round) {
      auto env = base;
      env.insert_or_assign(ivar, domain);
      for (const auto& [c, r] : u) {
        env.insert_or_assign(c, r);
      }
      if (!per_iter.run(body, env)) {
        return out;
      }
      carry_out(env);
      converged = !join_in(current);
    }
    if (!converged) {
      return out;
    }
  }
  // One walk over the unions, as the lifted body will judge it.
  Loop_range_eval rolled(ln, typed, elem_typed, callee_out);
  auto            env = base;
  env.insert_or_assign(ivar, domain);
  for (const auto& [c, r] : u) {
    const auto t = typed.find(c);
    env.insert_or_assign(c, t == typed.end() || r.is_unbounded() ? r : r.meet(t->second));
  }
  if (!rolled.run(body, env)) {
    return out;
  }
  out.rolled_stricter = std::ranges::any_of(rolled.overflow, [&](const Lnast_nid& n) { return !per_iter.overflow.contains(n); });
  out.ok              = true;
  return out;
}

}  // namespace

bool uPass_runner::subtree_writes_through_ref(const Lnast& ln, const Lnast_nid& nid) const {
  if (nid.is_invalid() || Lnast_ntype::is_func_def(ln.get_type(nid))) {
    return false;
  }
  if (Lnast_ntype::is_func_call(ln.get_type(nid))) {
    const auto dst = ln.get_first_child(nid);
    const auto fn  = dst.is_invalid() ? dst : ln.get_sibling_next(dst);
    for (auto a = fn.is_invalid() ? fn : ln.get_sibling_next(fn); !a.is_invalid(); a = ln.get_sibling_next(a)) {
      const auto key = Lnast_ntype::is_store(ln.get_type(a)) ? ln.get_first_child(a) : Lnast_nid{};
      if (key.is_invalid()) {
        continue;
      }
      if (ln.get_name(key) == "__ref_arg") {
        return true;
      }
      if (ln.get_name(key) == call_ufcs_arg_marker) {
        const auto callee = Lnast_ntype::is_ref(ln.get_type(fn)) ? lookup_callee(ln.get_name(fn)) : nullptr;
        if (!callee || callee->io_meta().inputs.empty() || callee->io_meta().inputs.front().is_ref) {
          return true;
        }
      }
    }
  }
  for (auto c : ln.children(nid)) {
    if (subtree_writes_through_ref(ln, c)) {
      return true;
    }
  }
  return false;
}

uPass_runner::Loop_scope_facts& uPass_runner::loop_scope_facts(const Lnast_nid& definition) {
  const auto& tree     = lm->get_lnast();
  auto&       per_tree = loop_tree_facts_[tree.get()];
  if (per_tree.tree != tree) {
    per_tree = Loop_tree_facts{.tree = tree, .definitions = {}};
  }
  auto [it, fresh] = per_tree.definitions.try_emplace(definition);
  if (fresh) {
    gather_loop_decls(*tree, definition, it->second.wires, it->second.regs, it->second.arrays, it->second.comptimes);
  }
  return it->second;
}

bool uPass_runner::plan_loop_roll(const Lnast_nid& body_stmts, const std::string& ivar, int64_t lo, int64_t hi, int64_t step,
                                  Loop_roll_plan& out) {
  const auto refuse = [&](std::string_view why) {
    std::print("uPass - roll declined for `for {}`: {}\n", ivar, why);
    return false;
  };
  if (step <= 0) {
    return refuse("non-positive step");
  }
  if (hi < lo) {
    return refuse("empty range");  // zero-count rolling is legal but pointless
  }
  // Widen before subtracting: an otherwise valid domain may span from a
  // negative bound to a positive one and `hi - lo` is not representable in
  // int64_t even though both endpoints are. Check descriptor representability
  // before narrowing; trip count is not an unrolling policy.
  using i128            = __int128;
  const i128 span       = static_cast<i128>(hi) - static_cast<i128>(lo);
  const i128 count_wide = span / static_cast<i128>(step) + 1;
  if (count_wide > static_cast<i128>(std::numeric_limits<uint64_t>::max())) {
    return refuse("trip count exceeds the loop descriptor range");
  }
  const auto count = static_cast<uint64_t>(count_wide);

  const auto& ln = *lm->get_lnast();
  if (subtree_writes_through_ref(ln, body_stmts)) {
    return refuse("a call writes a variable back through `ref`");
  }
  out.has_loop_control = subtree_has_loop_control(ln, body_stmts);
  const auto loop_nid  = ln.get_parent(body_stmts);  // the `for` node: the order anchor for written_before

  Body_vars vars;
  collect_body_vars(ln, body_stmts, /*parent_is_call=*/false, vars);
  const auto& written  = vars.written;
  const auto& read     = vars.read;
  const auto& declared = vars.declared;

  out.first = lo;
  out.step  = step;
  out.count = count;
  out.ivar  = ivar;
  for (const auto& name : read) {
    out.actual_names[name] = lm->frame_variable(name);
  }
  for (const auto& name : written) {
    out.actual_names[name] = lm->frame_variable(name);
  }

  // Declared-in-body names are local; the iteration variable is the index.
  const auto is_local   = [&](const std::string& n) { return declared.contains(n) || n == ivar; };
  const auto definition = enclosing_definition(ln, loop_nid);
  auto&      facts      = loop_scope_facts(definition);

  for (const auto& n : written) {
    if (is_local(n)) {
      continue;
    }
    // A register always has an incoming value (its pending D, or Q when the
    // cycle has not written it), so it is a carry even when every path writes
    // it: a final would come back as a plain value bound to the register name,
    // leaving the flop's D at Q.
    const bool has_incoming = written_before(ln, loop_nid, n) || facts.regs.contains(n);
    if (!read.contains(n) && !has_incoming && !out.has_loop_control && body_must_write(ln, body_stmts, n)) {
      out.finals.emplace_back(n);
    } else {
      out.carries.emplace_back(n);
    }
  }
  for (const auto& n : read) {
    if (is_local(n) || written.contains(n)) {
      continue;
    }
    if (auto value = symbol_table_.known_const_scalar(out.actual_names.at(n))) {
      out.constants.emplace_back(n, *value);
    } else {
      out.invariants.emplace_back(n);
    }
  }
  std::ranges::sort(out.constants, {}, &std::pair<std::string, Dlop>::first);
  // Deterministic port order: the lifted definition's interface must not depend
  // on hash iteration order, or two compiles of the same source disagree.
  std::ranges::sort(out.carries);
  std::ranges::sort(out.finals);
  std::ranges::sort(out.invariants);

  // A value the loop PRODUCES and a later range domain reads must remain
  // comptime so that loop can unroll. This is a forward use check, not a blanket
  // ban on loops whose current values happen to be constant. `finals` count for
  // exactly the same reason as `carries`: both become Sub outputs, i.e. runtime
  // wires, after emit_rolled_loop_call.
  std::vector<std::string> produced;
  produced.reserve(out.carries.size() + out.finals.size());
  produced.insert(produced.end(), out.carries.begin(), out.carries.end());
  produced.insert(produced.end(), out.finals.begin(), out.finals.end());
  if (later_loop_domain_reads_any(ln, body_stmts, produced)) {
    return refuse("a value the loop produces is used by a later comptime loop domain");
  }
  // Loops that stay UNROLLED whatever compile.unroll says (docs
  // 05b-statements.md "Loops and `wire`"), so the rolled and unrolled
  // lowerings never disagree about what compiles:
  //  * a body that writes a memory (a `reg` array): unrolled, each indexed
  //    write stays a write port of the enclosing Memory for memory inference,
  //    where a rolled body could only carry the whole array through every
  //    ordinal and write it back;
  //  * a body that reads a `wire` (prp2lnast rejects a drive): the wire is a
  //    module-wide net whose driver may follow the loop and depend on it;
  //  * a loop whose own output rings back into its inputs through a wire (see
  //    loop_output_rings_back).
  // A body with a runtime `break`/`continue` has no unrolled form (unroll_for
  // routes it here whatever the knob says), so it stays rolled through a wire
  // read or a ring -- a ring stays an ordinary external edge of the instance,
  // see graph_util::closes_loop_self_edge -- and a memory write is an error.
  const bool must_roll = out.has_loop_control && subtree_has_runtime_loop_control(ln, body_stmts, ivar);
  for (const auto& n : produced) {
    // A `comptime` variable is computed at compile time; a rolled body is
    // runtime hardware (`comptime mut init:[N]u4 = nil` filled by a loop).
    if (facts.comptimes.contains(n)) {
      return refuse(std::format("the body writes comptime `{}`", n));
    }
  }
  for (const auto& n : produced) {
    if (!facts.regs.contains(n)) {
      continue;
    }
    const auto bundle = symbol_table_.get_bundle(out.actual_names.at(n));
    if ((bundle && bundle->get_attr("__array_size").is_integer()) || facts.arrays.contains(n)) {
      if (must_roll) {
        loop_fail(ln.span_of_nearest(loop_nid),
                  "unsupported",
                  "loop-runtime-break-memory",
                  std::format("a loop with a runtime `break`/`continue` may not write memory `{}` (a `reg` array)", n),
                  "such a loop stays rolled, and a rolled body cannot write a memory; write it after the loop, or make the "
                  "exit condition comptime so the loop unrolls");
      }
      return refuse(std::format("the body writes memory `{}` (a reg array)", n));
    }
  }
  if (!must_roll && !facts.wires.empty()) {
    for (const auto& n : read) {
      if (!is_local(n) && facts.wires.contains(n)) {
        return refuse(std::format("the body reads wire `{}`", n));
      }
    }
    std::vector<Lnast_nid> scopes{definition};  // the definition, then every loop enclosing this one
    for (auto p = ln.get_parent(loop_nid); !p.is_invalid() && p != definition; p = ln.get_parent(p)) {
      const auto t = ln.get_type(p);
      if (Lnast_ntype::is_for(t) || Lnast_ntype::is_while(t) || Lnast_ntype::is_rolled_for(t)) {
        scopes.emplace_back(p);
      }
    }
    for (const auto& scope : scopes) {
      if (auto [it, fresh] = facts.deps.try_emplace(scope); fresh) {
        gather_straight_line_deps(ln, scope, facts.wires, it->second);
      }
    }
    std::vector<const Name_deps*> deps;  // after every insertion: the map may rehash
    for (const auto& scope : scopes) {
      deps.emplace_back(&facts.deps.at(scope));
    }
    auto seeds = out.invariants;
    seeds.insert(seeds.end(), out.carries.begin(), out.carries.end());
    if (loop_output_rings_back(deps, facts.wires, seeds, produced)) {
      return refuse("a value the loop produces reaches its own input through a wire");
    }
  }
  {
    // Everything that becomes an INPUT PORT of the lifted definition is runtime
    // inside it, and lift_loop_body promotes the invariants too -- not just the
    // index and the carry-ins. Omitting them let `for j in 0..<n` (n a loop
    // invariant) be accepted for rolling and then abort in tolg with
    // "non-comptime 'for' loop", on source that unrolls fine.
    std::vector<std::string> runtime_in_body = produced;
    runtime_in_body.emplace_back(ivar);
    runtime_in_body.insert(runtime_in_body.end(), out.invariants.begin(), out.invariants.end());
    if (nested_loop_domain_reads_any(ln, body_stmts, runtime_in_body)) {
      return refuse(
          "a nested loop domain, a generic `<...>` bind or an integer type bound in the body reads the index, a carry or "
          "a loop invariant (all runtime inside the lifted body, and each needs a comptime value)");
    }
  }

  // Repeated constant arithmetic over a bounded declared carry is also kept
  // source-unrolled: upass.bitwidth observes successive values and emits the
  // range/overflow diagnostics that compiling the lifted body once cannot see.
  // Calls make the boundary runtime even with constant-looking actuals.
  const auto known_before_loop
      = [&](const std::string& name) { return symbol_table_.known_const_scalar(out.actual_names.at(name)).has_value(); };
  const bool constant_arithmetic = std::ranges::all_of(out.invariants, known_before_loop)
                                   && std::ranges::all_of(out.carries, known_before_loop) && !subtree_has_call(ln, body_stmts);

  // Boundary types. These io declarations establish the loop's width contract
  // before graph bitwidth inference (set_subgraph_boundary_bw re-reads outputs),
  // so a name with no declared type cannot roll.
  const auto& encl_io    = lm->get_lnast()->io_meta();
  const auto  encl_input = [&](const std::string& nm) -> const Lnast_io_entry* {
    for (const auto& ce : encl_io.inputs) {
      if (ce.name == nm) {
        return &ce;
      }
    }
    for (const auto& ce : encl_io.outputs) {
      if (ce.name == nm) {
        return &ce;
      }
    }
    return nullptr;
  };
  const auto type_of = [&](const std::string& n, Spec_port& sp) {
    const auto actual     = lm->frame_variable(n);
    const auto from_facts = [&](const std::optional<upass::decl_facts::Facts>& f) {
      if (!f) {
        return false;
      }
      if (f->kind == upass::decl_facts::Num::boolean) {
        sp = Spec_port{.inject = true, .kind = Io_kind::boolean};
        return true;
      }
      if (f->range_max || f->range_min) {
        sp = Spec_port{.inject = true, .max = f->range_max, .min = f->range_min};
        return true;
      }
      return false;
    };
    if (from_facts(upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), actual))) {
      return true;
    }
    // The variable's declaration facts retain the nominal alias name even
    // when its concrete range has not yet flowed onto the value entry. Resolve
    // the alias bundle here, before outlining into a definition that cannot see
    // the caller's type namespace.
    if (const auto alias = try_typename(actual); !alias.empty()) {
      if (from_facts(upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), alias))) {
        return true;
      }
      Dlop max;
      Dlop min;
      if (imported_alias_range(alias, max, min)) {
        sp = Spec_port{.inject = true, .max = max, .min = min};
        return true;
      }
    }
    // Keep a tree-level fallback for scalar aliases. The runner may have
    // invalidated a runtime value's bundle before loop planning, but the
    // declaration syntax is still authoritative and stable:
    //   declare(acc, ref(Word), mut)
    //   declare(Word, prim_type_int(4095,0), type)
    // Resolve that chain to a concrete boundary instead of copying `Word`
    // into the separately compiled lifted module.
    const auto resolve_decl_in = [&](const Lnast& tree, std::string_view first_name) {
      absl::flat_hash_set<std::string>      resolving;
      std::function<bool(std::string_view)> resolve_decl = [&](std::string_view wanted) {
        if (!resolving.emplace(wanted).second) {
          return false;
        }
        std::function<bool(const Lnast_nid&)> scan = [&](const Lnast_nid& nid) {
          if (nid.is_invalid()) {
            return false;
          }
          const auto parent      = tree.get_parent(nid);
          const auto grandparent = parent.is_invalid() ? parent : tree.get_parent(parent);
          const bool io_port     = Lnast_ntype::is_store(tree.get_type(nid)) && !parent.is_invalid()
                               && Lnast_ntype::is_tuple_add(tree.get_type(parent)) && !grandparent.is_invalid()
                               && Lnast_ntype::is_io(tree.get_type(grandparent));
          if (Lnast_ntype::is_declare(tree.get_type(nid)) || io_port) {
            auto target = tree.get_first_child(nid);
            auto type_n = target.is_invalid() ? target : tree.get_sibling_next(target);
            if (io_port && !type_n.is_invalid()) {
              type_n = tree.get_sibling_next(type_n);  // skip the port default
            }
            if (!target.is_invalid() && !type_n.is_invalid() && Lnast_ntype::is_ref(tree.get_type(target))
                && tree.get_name(target) == wanted) {
              const auto tt = tree.get_type(type_n);
              if (Lnast_ntype::is_prim_type_bool(tt)) {
                sp = Spec_port{.inject = true, .kind = Io_kind::boolean};
                return true;
              }
              // An ARRAY port after upass.ssa's full rebuild (forced by any
              // repeated definition, e.g. `mut t:U12 = 0 ; t = 0`): the rebuild
              // re-emits the io slot as the flat packed prim_type_int and only
              // io_meta keeps the `[N]T` view. Read as a plain N*T-bit integer,
              // the lifted body's `b[i]` no longer indexes an array and
              // lnast.tolg cannot resolve it. Rebuild the same one-dimension
              // integer-lane port the comp_type_array branch below produces.
              if (io_port && Lnast_ntype::is_prim_type_int(tt)) {
                const auto* ci = encl_input(std::string(wanted));
                if (ci != nullptr && ci->array_size > 0 && ci->elem_bits > 0 && ci->inner_dims.empty() && !ci->elem_bool
                    && ci->kind != Io_kind::boolean) {
                  const auto elem_max
                      = ci->elem_signed ? upass::signed_max_from_bits(ci->elem_bits) : upass::unsigned_max_from_bits(ci->elem_bits);
                  const auto elem_min = ci->elem_signed ? upass::signed_min_from_bits(ci->elem_bits) : *Dlop::from_pyrope("0");
                  sp                  = Spec_port{.inject = true, .max = elem_max, .min = elem_min, .array_size = ci->array_size};
                  return true;
                }
              }
              if (Lnast_ntype::is_prim_type_int(tt)) {
                auto max_n = tree.get_first_child(type_n);
                auto min_n = max_n.is_invalid() ? max_n : tree.get_sibling_next(max_n);
                // A REF bound leaf is a generic-width port bound only the call's
                // binds can fold (a template io `prim_type_int(ref %t, 0)`): it
                // is not this variable's type, so let the fallbacks below decide.
                if ((!max_n.is_invalid() && Lnast_ntype::is_ref(tree.get_type(max_n)))
                    || (!min_n.is_invalid() && Lnast_ntype::is_ref(tree.get_type(min_n)))) {
                  return false;
                }
                std::optional<Dlop> max;
                std::optional<Dlop> min;
                if (!max_n.is_invalid() && Lnast_ntype::is_const(tree.get_type(max_n))) {
                  if (auto v = Dlop::from_pyrope(tree.get_name(max_n)); v->is_integer()) {
                    max = *v;
                  }
                }
                if (!min_n.is_invalid() && Lnast_ntype::is_const(tree.get_type(min_n))) {
                  if (auto v = Dlop::from_pyrope(tree.get_name(min_n)); v->is_integer()) {
                    min = *v;
                  }
                }
                if (max || min) {
                  sp = Spec_port{.inject = true, .max = max, .min = min};
                  return true;
                }
              }
              if (Lnast_ntype::is_comp_type_array(tt)) {
                // `mut v:[N]T`: carry the whole array across the boundary as an
                // `[N]T` port (element range + size). A nested element type
                // (2-D) is not a packed lane array and does not roll.
                auto elem_n = tree.get_first_child(type_n);
                auto len_n  = elem_n.is_invalid() ? elem_n : tree.get_sibling_next(elem_n);
                if (elem_n.is_invalid() || len_n.is_invalid() || !Lnast_ntype::is_prim_type_int(tree.get_type(elem_n))
                    || !Lnast_ntype::is_const(tree.get_type(len_n))) {
                  return false;
                }
                const auto lanes = upass::array_dim_lanes(tree.get_name(len_n));
                if (!lanes) {
                  return false;  // a dim that did not fold to a literal (see array_dim.hpp)
                }
                const int64_t       count_i = *lanes;
                auto                max_n   = tree.get_first_child(elem_n);
                auto                min_n   = max_n.is_invalid() ? max_n : tree.get_sibling_next(max_n);
                std::optional<Dlop> max;
                std::optional<Dlop> min;
                if (!max_n.is_invalid() && Lnast_ntype::is_const(tree.get_type(max_n))) {
                  if (auto v = Dlop::from_pyrope(tree.get_name(max_n)); v->is_integer()) {
                    max = *v;
                  }
                }
                if (!min_n.is_invalid() && Lnast_ntype::is_const(tree.get_type(min_n))) {
                  if (auto v = Dlop::from_pyrope(tree.get_name(min_n)); v->is_integer()) {
                    min = *v;
                  }
                }
                if (!(max || min)) {
                  return false;
                }
                sp = Spec_port{.inject = true, .max = max, .min = min, .array_size = count_i};
                return true;
              }
              if (Lnast_ntype::is_ref(tt)) {
                return resolve_decl(tree.get_name(type_n));
              }
            }
          }
          for (auto c : tree.children(nid)) {
            if (!Lnast_ntype::is_func_def(tree.get_type(c)) && scan(c)) {
              return true;
            }
          }
          return false;
        };
        return scan(tree.get_root());
      };
      return resolve_decl(first_name);
    };
    if (resolve_decl_in(ln, n)) {
      return true;
    }
    // A file-level scalar alias lives in the source-unit shell LNAST, while
    // this runner is walking the extracted module LNAST. Recover the nominal
    // type from the module declaration, then resolve it in that shared shell.
    std::string                           nominal;
    std::function<void(const Lnast_nid&)> find_nominal = [&](const Lnast_nid& nid) {
      if (!nominal.empty() || nid.is_invalid()) {
        return;
      }
      if (Lnast_ntype::is_declare(ln.get_type(nid))) {
        auto target = ln.get_first_child(nid);
        auto type_n = target.is_invalid() ? target : ln.get_sibling_next(target);
        if (!target.is_invalid() && !type_n.is_invalid() && Lnast_ntype::is_ref(ln.get_type(target)) && ln.get_name(target) == n
            && Lnast_ntype::is_ref(ln.get_type(type_n))) {
          nominal = ln.get_name(type_n);
          return;
        }
      }
      for (auto child : ln.children(nid)) {
        if (!Lnast_ntype::is_func_def(ln.get_type(child))) {
          find_nominal(child);
        }
      }
    };
    find_nominal(ln.get_root());
    if (!nominal.empty() && registry_ != nullptr) {
      std::string unit{ln.get_graph_name()};
      if (const auto dot = unit.rfind('.'); dot != std::string::npos) {
        unit.resize(dot);
      }
      if (auto it = reg().function_registry.find(unit);
          it != reg().function_registry.end() && it->second->get_lambda_kind().empty() && resolve_decl_in(*it->second, nominal)) {
        return true;
      }
    }
    // A NAMED type (`type W = u12; mut acc:W`) must resolve to a RANGE here: the
    // lifted definition is a separate module that cannot see the caller's type
    // alias, so emitting `ref(W)` as the port type yields a 1-bit port and
    // silently truncates the carry. Prefer the declared range; refuse if the
    // name only has an alias.
    if (auto dt = try_decl_type(actual); dt && (dt->range_max || dt->range_min)) {
      sp = Spec_port{.inject = true, .max = dt->range_max, .min = dt->range_min};
      return true;
    }
    // A free variable that is an INPUT of the enclosing definition carries its
    // width on the io declaration rather than in a body `declare`.
    if (const auto* ci = encl_input(n); ci != nullptr && (ci->bits > 0 || ci->kind == Io_kind::boolean)) {
      if (ci->kind == Io_kind::boolean) {
        sp = Spec_port{.inject = true, .max = *Dlop::from_pyrope("1"), .min = *Dlop::from_pyrope("0")};
      } else if (ci->is_signed) {
        sp = Spec_port{.inject = true, .max = upass::signed_max_from_bits(ci->bits), .min = upass::signed_min_from_bits(ci->bits)};
      } else {
        sp = Spec_port{.inject = true, .max = upass::unsigned_max_from_bits(ci->bits), .min = *Dlop::from_pyrope("0")};
      }
      return true;
    }
    // Inferring from the VALUE is also unsound in general for integers: a
    // `mut acc = 0` accumulator widens as the loop runs, so typing the
    // boundary from its seed would silently truncate (design rule 11).
    return false;
  };
  // The lifted definition is compiled on its own, so an integer boundary port
  // needs BOTH bounds: with one side open SSA derives no width for the port and
  // it lowers as a single bit (a bare `unsigned` declaration, or a generic
  // bound nothing folded).
  const auto half_open
      = [](const Spec_port& sp) { return sp.kind != Io_kind::boolean && sp.type_name.empty() && (!sp.max || !sp.min); };
  for (const auto& n : out.carries) {
    // The flop stays in the enclosing definition. Q is an invariant input;
    // only the pending D value participates in the ordinal carry chain.
    if (facts.regs.contains(n)) {
      out.registers.insert(n);
    }
    // A non-final variable still needs an ordinal-0 value. Conditional writes,
    // reads-before-write and breakable loops all land here deliberately.
    if (!out.registers.contains(n) && !written_before(ln, loop_nid, n)) {
      return refuse(std::format("`{}` is written only inside the loop (no value enters ordinal 0)", n));
    }
    Spec_port sp;
    if (!type_of(n, sp)) {
      return refuse(std::format("carried variable `{}` has no declared type", n));
    }
    if (half_open(sp)) {
      return refuse(std::format("carried variable `{}` has no bounded type", n));
    }
    out.types[n] = sp;
  }
  for (const auto& n : out.finals) {
    Spec_port sp;
    if (!type_of(n, sp)) {
      return refuse(std::format("final-only variable `{}` has no declared type", n));
    }
    if (half_open(sp)) {
      return refuse(std::format("final-only variable `{}` has no bounded type", n));
    }
    out.types[n] = sp;
  }
  for (const auto& n : out.invariants) {
    Spec_port sp;
    if (!type_of(n, sp) || half_open(sp)) {
      // Unlike a carry, an invariant cannot widen while this loop executes.
      // Its inferred value envelope is therefore a valid boundary contract.
      sp                 = Spec_port{};
      const auto& actual = out.actual_names.at(n);
      if (auto bundle = symbol_table_.get_bundle(actual)) {
        const auto& value = bundle->get_entry(bundle_path::of_string("0"));
        if (value.bw_max.is_integer() && value.bw_min.is_integer()) {
          sp = Spec_port{.inject = true, .max = value.bw_max, .min = value.bw_min};
        }
      }
      if (!sp.inject) {
        const auto& ranges = ln.bw_meta().ranges;
        if (auto it = ranges.find(actual); it != ranges.end() && !it->second.unbounded) {
          sp = Spec_port{.inject = true,
                         .max    = *Dlop::create_integer(it->second.max),
                         .min    = *Dlop::create_integer(it->second.min)};
        }
      }
      if (!sp.inject) {
        return refuse(std::format("loop-invariant `{}` has no inferred type", n));
      }
    }
    out.types[n] = sp;
  }
  if (constant_arithmetic
      && std::ranges::any_of(out.carries, [&](const std::string& n) { return out.types.at(n).kind != Io_kind::boolean; })) {
    return refuse("rolling would hide per-iteration range/overflow diagnostics on a bounded carry");
  }
  {
    // The overflow rule must judge the rolled loop as the unrolled one would
    // (compile.unroll must not change what compiles): seed the lifted body with
    // each carry's range over all iterations, and keep the loop unrolled when
    // that one walk would still be stricter than every single iteration, or
    // when the ranges cannot be derived and a carry reaches a typed write. A
    // body with break/continue cannot unroll (a runtime exit needs the rolled
    // activation chain), so it rolls in every mode.
    std::vector<std::string> carries_read;
    for (const auto& n : out.carries) {
      if (read.contains(n)) {
        carries_read.emplace_back(n);
      }
    }
    const auto spec_range = [](const Spec_port& sp) -> std::optional<Lnast_range> {
      if (sp.kind == Io_kind::boolean) {
        return Lnast_range::boolean();
      }
      if (sp.array_size != 0 || !sp.max || !sp.min || !sp.max->is_integer() || !sp.min->is_integer()
          || sp.max->get_signed_bits() > 62 || sp.min->get_signed_bits() > 62) {
        return std::nullopt;
      }
      return Lnast_range::constant(sp.min->to_just_i64()).join(Lnast_range::constant(sp.max->to_just_i64()));
    };
    // What a name holds where the loop starts: a comptime value, else the
    // range derived for it, else its declared type.
    const auto entry_range = [&](const std::string& n) {
      const auto& actual = out.actual_names.at(n);
      if (!out.registers.contains(n)) {
        if (const auto v = symbol_table_.known_const_scalar(actual); v && v->is_integer() && v->get_signed_bits() <= 62) {
          return Lnast_range::constant(v->to_just_i64());
        }
        if (const auto r = value_range_of(Lnast_node::create_ref(actual));
            r.bounded() && r.min->get_signed_bits() <= 62 && r.max->get_signed_bits() <= 62) {
          return Lnast_range::constant(r.min->to_just_i64()).join(Lnast_range::constant(r.max->to_just_i64()));
        }
      }
      const auto t = out.types.find(n);
      const auto r = t == out.types.end() ? std::nullopt : spec_range(t->second);
      return r ? *r : Lnast_range::make_unbounded();
    };
    Loop_range_eval::Env             typed;
    Loop_range_eval::Env             elem_typed;  // an array carry's element type
    absl::flat_hash_set<std::string> typed_names;
    for (const auto& [n, sp] : out.types) {
      if (const auto r = spec_range(sp); r && sp.kind != Io_kind::boolean) {
        typed.emplace(n, *r);
        typed_names.emplace(n);
      } else if (sp.array_size != 0 && sp.kind != Io_kind::boolean && sp.max && sp.min && sp.max->is_integer()
                 && sp.min->is_integer() && sp.max->get_signed_bits() <= 62 && sp.min->get_signed_bits() <= 62) {
        elem_typed.emplace(n, Lnast_range::constant(sp.min->to_just_i64()).join(Lnast_range::constant(sp.max->to_just_i64())));
      }
    }
    Loop_range_eval::Env base;
    for (const auto& [n, v] : out.constants) {
      if (v.is_integer() && !v.has_unknowns() && v.get_signed_bits() <= 62) {
        base.emplace(n, Lnast_range::constant(v.to_just_i64()));
      }
    }
    for (const auto& n : out.invariants) {
      const auto r = entry_range(n);
      base.emplace(n, r);
      if (!r.is_unbounded()) {
        out.invariant_ranges.emplace(n, std::pair{r.min, r.max});
      }
    }
    Loop_range_eval::Env carry_init;
    for (const auto& n : out.carries) {
      carry_init.emplace(n, entry_range(n));
    }
    const auto callee_out = [&](std::string_view callee) -> std::optional<Lnast_range> {
      const auto c = lookup_callee(callee);
      if (!c || c->io_meta().outputs.size() != 1) {
        return std::nullopt;
      }
      const auto& o = c->io_meta().outputs.front();
      if (o.kind != Io_kind::integer || o.array_size != 0) {
        return std::nullopt;
      }
      if (o.has_range) {
        return Lnast_range::constant(o.range_min).join(Lnast_range::constant(o.range_max));
      }
      if (o.bits > 0 && o.bits <= 62) {
        return o.is_signed ? Lnast_range::sext_to(o.bits - 1)
                           : Lnast_range::constant(0).join(Lnast_range::constant((int64_t{1} << o.bits) - 1));
      }
      return std::nullopt;
    };
    const auto cr = eval_carry_ranges(ln, body_stmts, ivar, lo, step, count, base, carry_init, typed, elem_typed, callee_out);
    if (!out.has_loop_control) {
      if (cr.ok && cr.rolled_stricter) {
        return refuse("one walk of the lifted body would judge a typed write stricter than every iteration does");
      }
      if (!cr.ok && carry_feeds_range_check(ln, body_stmts, carries_read, std::move(typed_names))) {
        return refuse("a carried value reaches a typed write without wrap/sat, and its per-iteration range is unknown");
      }
    }
    if (cr.ok) {
      for (const auto& [c, r] : cr.carry_in) {
        const auto t = typed.find(c);
        const auto s = t == typed.end() ? r : r.meet(t->second);
        if (!s.is_unbounded()) {
          out.carry_in_ranges.emplace(c, std::pair{s.min, s.max});
        }
      }
    }
  }

  // The index port is signed and wide enough for every generated value (the
  // realized Verilog localparam is signed, so a minimal unsigned width would
  // wrap the top ordinal).
  // The realized boundary is SIGNED (cgen prints `input signed [W-1:0]`), so
  // the declared range must be a signed one wide enough for the extreme index.
  // Declaring `[0, 7]` yields a 3-bit port that cannot hold 7 once it is read
  // as signed — design rule 7 (a 0..=15 domain needs 5 signed bits, not 4).
  const int64_t last = static_cast<int64_t>(static_cast<i128>(lo) + static_cast<i128>(count - 1) * step);
  {
    // Two's-complement width, sign bit included, of the extreme generated index.
    const auto sbits = [](int64_t v) {
      if (v == 0) {
        return 1;
      }
      if (v > 0) {
        int b = 1;
        for (; v != 0; v >>= 1) {
          ++b;
        }
        return b;
      }
      int b = 2;
      while (b < 64 && v < -(static_cast<int64_t>(1) << (b - 1))) {
        ++b;
      }
      return b;
    };
    const int w     = std::max(sbits(lo), sbits(last));
    out.types[ivar] = Spec_port{.inject = true, .max = upass::signed_max_from_bits(w), .min = upass::signed_min_from_bits(w)};
  }

  // A compiler-owned port name must not collide with a source variable. EVERY
  // source name lift_loop_body turns into a boundary port has to be checked, not
  // just the carried ones: the index (`add_port(ins, plan.ivar, ...)`) and the
  // loop invariants become input ports the same way, so `for __valid in 0..<4`
  // would emit two `__valid` inputs, and tolg's lower_rolled_for would then
  // resolve index_input and activation_input to the same pid and make
  // hhds::Node_class::set_subnode throw ("role inputs must be distinct") instead
  // of declining the roll here.
  std::vector<std::string> boundary_names = out.carries;
  boundary_names.insert(boundary_names.end(), out.finals.begin(), out.finals.end());
  boundary_names.insert(boundary_names.end(), out.invariants.begin(), out.invariants.end());
  boundary_names.emplace_back(out.ivar);
  for (const auto& n : boundary_names) {
    if (n.ends_with(kCarryInSuffix) || n.ends_with(kCarryOutSuffix) || n == kLoopValid || n == kLoopExec || n == kLoopNextActive) {
      return refuse(std::format("variable `{}` collides with a reserved loop port name", n));
    }
  }
  return true;
}

std::shared_ptr<Lnast> uPass_runner::lift_loop_body(const Lnast_nid& body_stmts, Loop_roll_plan& plan) {
  const auto& src  = lm->get_lnast();
  auto        body = std::make_shared<Lnast>(plan.mangled);
  auto        root = body->set_root(Lnast_ntype::create_top());
  if (const auto id = src->get_srcid(body_stmts); id != hhds::SourceId_invalid) {
    body->set_srcid(root, body->source_locator().import_from(src->source_locator(), id));
  }
  // A loop lifted out of a `comb` must itself be a `comb`. tolg lets ANY body
  // instantiate a comb callee (it is latency-0 and stateless), but only a `mod`
  // may instantiate a mod -- so lifting unconditionally as a mod made every
  // rolled loop inside a comb a hard error ("'x' (a comb) calls the mod
  // 'x.__loop0' -- only `mod` bodies may instantiate pipe/mod"), i.e. every
  // typed-carry `for` in a comb failed under the default compile.unroll=false.
  // The slice is stateless by construction: the same tolg gate already forbids
  // a reg or a mod instance anywhere in the enclosing comb's body.
  body->set_lambda_kind(src->get_lambda_kind() == "comb" ? "comb" : "mod");
  body->set_template(false);
  // The body's statements keep their unit's semantics: a Verilog-read unit's
  // bare `~` is Verilog's (user ruling 2026-09-29 (44) types only Pyrope's),
  // and `::[timecheck=false]` covers the lifted slice too.
  body->set_verilog_origin(src->is_verilog_origin());
  body->set_skip_timecheck(src->get_skip_timecheck());
  // The storage type has a sign bit, but the index only takes values in this
  // elaborated domain. Preserve that tighter fact for shift/range analysis.
  const auto last_index
      = static_cast<int64_t>(static_cast<__int128>(plan.first) + static_cast<__int128>(plan.count - 1) * plan.step);
  body->bw_meta().ranges[plan.ivar]
      = BitwidthEntry{.min = std::min(plan.first, last_index), .max = std::max(plan.first, last_index), .unbounded = false};
  // Likewise each carry-in holds only what enters some iteration
  // (plan_loop_roll's eval_carry_ranges), not all of its storage type.
  for (const auto& [n, r] : plan.carry_in_ranges) {
    body->bw_meta().ranges[n + std::string(kCarryInSuffix)] = BitwidthEntry{.min = r.first, .max = r.second, .unbounded = false};
  }
  // And each invariant only what it holds on loop entry: a NESTED loop reading
  // the outer index would otherwise see that index port's signed storage
  // window (`[-4, 3]`) and report a negative array index or shift amount.
  for (const auto& [n, r] : plan.invariant_ranges) {
    body->bw_meta().ranges[n] = BitwidthEntry{.min = r.first, .max = r.second, .unbounded = false};
  }

  // One io port declaration: store(ref(name), const(nil), <type>) [+ stages].
  const auto add_port = [&](const Lnast_nid& parent, const std::string& name, const Spec_port& p, bool is_output) {
    auto st = body->add_child(parent, Lnast_ntype::create_store());
    body->add_child(st, Lnast_node::create_ref(name));
    body->add_child(st, Lnast_node::create_const("nil"));
    // A captured `Clock`/`Reset` input of the owner keeps its type: it is the
    // lifted slice's implicit clock/reset too (a stateful callee in the body
    // binds to it), not a data port a minted `reset` would then clash with.
    const auto* owner_port = is_output ? nullptr : src->io_meta().find(name);
    if (owner_port != nullptr && owner_port->sig == Io_sig::clock) {
      body->add_child(st, Lnast_ntype::create_prim_type_clock());
    } else if (owner_port != nullptr && owner_port->sig == Io_sig::reset) {
      body->add_child(st, Lnast_ntype::create_prim_type_reset());
    } else if (name == kLoopValid || name == kLoopNextActive || p.kind == Io_kind::boolean) {
      body->add_child(st, Lnast_ntype::create_prim_type_bool());
    } else if (!p.type_name.empty()) {
      body->add_child(st, Lnast_node::create_ref(p.type_name));
    } else if (p.array_size > 0) {
      auto at = body->add_child(st, Lnast_ntype::create_comp_type_array());
      auto pt = body->add_child(at, Lnast_ntype::create_prim_type_int());
      body->add_child(pt, Lnast_node::create_const(p.max ? std::string(p.max->to_pyrope()) : std::string("nil")));
      body->add_child(pt, Lnast_node::create_const(p.min ? std::string(p.min->to_pyrope()) : std::string("nil")));
      body->add_child(at, Lnast_node::create_const("[" + std::to_string(p.array_size) + "]"));
    } else {
      auto pt = body->add_child(st, Lnast_ntype::create_prim_type_int());
      body->add_child(pt, Lnast_node::create_const(p.max ? std::string(p.max->to_pyrope()) : std::string("nil")));
      body->add_child(pt, Lnast_node::create_const(p.min ? std::string(p.min->to_pyrope()) : std::string("nil")));
    }
    if (is_output) {
      auto stg = body->add_child(st, Lnast_ntype::create_stages());
      body->add_child(stg, Lnast_node::create_const("0"));
      body->add_child(stg, Lnast_node::create_const("0"));
    }
  };

  auto io_n = body->add_child(root, Lnast_ntype::create_io());
  auto ins  = body->add_child(io_n, Lnast_ntype::create_tuple_add());
  add_port(ins, plan.ivar, plan.types.at(plan.ivar), false);
  const Spec_port bool_port{.inject = true, .max = *Dlop::from_pyrope("1"), .min = *Dlop::from_pyrope("0")};
  // Every compiler-generated loop definition has the local activation ABI.
  // Ordinary calls bind true; a conditional caller conjoins its path predicate
  // in tolg. Bodies without control/effects need not consume the value, but the
  // uniform append-only port keeps generated definitions composable.
  add_port(ins, std::string(kLoopValid), bool_port, false);
  for (const auto& n : plan.invariants) {
    add_port(ins, n, plan.types.at(n), false);
  }
  for (const auto& n : plan.carries) {
    add_port(ins, n + std::string(kCarryInSuffix), plan.types.at(n), false);
    if (plan.registers.contains(n)) {
      add_port(ins, n, plan.types.at(n), false);
    }
  }
  auto outs = body->add_child(io_n, Lnast_ntype::create_tuple_add());
  if (plan.has_loop_control) {
    add_port(outs, std::string(kLoopNextActive), bool_port, true);
  }
  for (const auto& n : plan.finals) {
    add_port(outs, n, plan.types.at(n), true);
  }
  for (const auto& n : plan.carries) {
    add_port(outs, n + std::string(kCarryOutSuffix), plan.types.at(n), true);
  }

  auto stmts = body->add_child(root, Lnast_ntype::create_stmts());

  // Capture elaborated constants by value. Making these ordinary inputs would
  // erase their compile-time meaning in nested domains, type widths and generic
  // bindings, besides needlessly adding hardware ports.
  for (const auto& [name, value] : plan.constants) {
    auto bind = body->add_child(stmts, Lnast_ntype::create_store());
    body->add_child(bind, Lnast_node::create_ref(name));
    body->add_child(bind, Lnast_node::create_const(value.to_pyrope()));
  }

  if (plan.has_loop_control) {
    auto dcl = body->add_child(stmts, Lnast_ntype::create_declare());
    body->add_child(dcl, Lnast_node::create_ref(std::string(kLoopExec)));
    body->add_child(dcl, Lnast_ntype::create_prim_type_bool());
    body->add_child(dcl, Lnast_node::create_const("mut"));

    auto seed_exec = body->add_child(stmts, Lnast_ntype::create_store());
    body->add_child(seed_exec, Lnast_node::create_ref(std::string(kLoopExec)));
    body->add_child(seed_exec, Lnast_node::create_ref(std::string(kLoopValid)));

    auto seed_next = body->add_child(stmts, Lnast_ntype::create_store());
    body->add_child(seed_next, Lnast_node::create_ref(std::string(kLoopNextActive)));
    body->add_child(seed_next, Lnast_node::create_ref(std::string(kLoopValid)));
  }

  // Prologue: seed each carry as an ordinary `mut` local from its input port,
  // so the copied body's reads and writes of that name need no substitution.
  for (const auto& n : plan.carries) {
    const auto& p   = plan.types.at(n);
    auto        dcl = body->add_child(stmts, Lnast_ntype::create_declare());
    body->add_child(dcl, Lnast_node::create_ref(plan.registers.contains(n) ? n + std::string(kCarryNextSuffix) : n));
    if (p.kind == Io_kind::boolean) {
      body->add_child(dcl, Lnast_ntype::create_prim_type_bool());
    } else if (!p.type_name.empty()) {
      body->add_child(dcl, Lnast_node::create_ref(p.type_name));
    } else if (p.array_size > 0) {
      auto at = body->add_child(dcl, Lnast_ntype::create_comp_type_array());
      auto pt = body->add_child(at, Lnast_ntype::create_prim_type_int());
      body->add_child(pt, Lnast_node::create_const(p.max ? std::string(p.max->to_pyrope()) : std::string("nil")));
      body->add_child(pt, Lnast_node::create_const(p.min ? std::string(p.min->to_pyrope()) : std::string("nil")));
      body->add_child(at, Lnast_node::create_const("[" + std::to_string(p.array_size) + "]"));
    } else {
      auto pt = body->add_child(dcl, Lnast_ntype::create_prim_type_int());
      body->add_child(pt, Lnast_node::create_const(p.max ? std::string(p.max->to_pyrope()) : std::string("nil")));
      body->add_child(pt, Lnast_node::create_const(p.min ? std::string(p.min->to_pyrope()) : std::string("nil")));
    }
    body->add_child(dcl, Lnast_node::create_const("mut"));

    auto seed = body->add_child(stmts, Lnast_ntype::create_store());
    body->add_child(seed, Lnast_node::create_ref(plan.registers.contains(n) ? n + std::string(kCarryNextSuffix) : n));
    body->add_child(seed, Lnast_node::create_ref(n + std::string(kCarryInSuffix)));
  }

  // The ordinary case stays verbatim. A body with loop control is predicated
  // statement-by-statement. `break` clears both the remainder-of-this-
  // occurrence predicate and the value chained into later occurrences;
  // `continue` clears only the former. Nested if/unique-if arms share the same
  // mutable predicate, so a taken control statement suppresses the rest of its
  // arm and every statement after the conditional while the untaken arms keep
  // the pre-branch value through the normal tolg mux merge.
  std::function<void(const Lnast_nid&, const Lnast_nid&)> copy_controlled_stmt;
  std::function<void(const Lnast_nid&, const Lnast_nid&)> copy_controlled_stmts;

  const auto copy_srcid = [&](const Lnast_nid& from, const Lnast_nid& to) {
    if (const auto id = src->get_srcid(from); id != hhds::SourceId_invalid) {
      body->set_srcid(to, body->source_locator().import_from(src->source_locator(), id));
    }
  };
  const auto guarded_parent = [&](const Lnast_nid& parent, const Lnast_nid& anchor) {
    auto guard = body->add_child(parent, Lnast_ntype::create_if());
    copy_srcid(anchor, guard);
    body->add_child(guard, Lnast_node::create_ref(std::string(kLoopExec)));
    return body->add_child(guard, Lnast_ntype::create_stmts());
  };

  copy_controlled_stmts = [&](const Lnast_nid& source_stmts, const Lnast_nid& target_stmts) {
    for (auto c : src->children(source_stmts)) {
      copy_controlled_stmt(c, target_stmts);
    }
  };
  copy_controlled_stmt = [&](const Lnast_nid& source_stmt, const Lnast_nid& target_stmts) {
    const auto type        = src->get_type(source_stmt);
    // Pure three-address computations and declarations are safe to evaluate
    // while inactive and must remain outside the runtime predicate. Besides
    // avoiding useless muxes, this preserves COMPTIME structure: a nested
    // range's `4-1` and `range(0,3)` producers cannot sit under `if __valid`, or
    // the inner `for` sees an uncertain iterable and is rejected as runtime.
    // Observable writes/calls/properties and control nodes stay guarded.
    const bool needs_guard = Lnast_ntype::is_store(type) || Lnast_ntype::is_func_call(type) || Lnast_ntype::is_cassert(type)
                             || Lnast_ntype::is_if(type) || Lnast_ntype::is_unique_if(type) || Lnast_ntype::is_for(type)
                             || Lnast_ntype::is_while(type) || Lnast_ntype::is_func_break(type)
                             || Lnast_ntype::is_func_continue(type) || Lnast_ntype::is_func_return(type);
    if (!needs_guard) {
      copy_subtree_into(src, source_stmt, body, target_stmts, nullptr);
      return;
    }
    auto dst = guarded_parent(target_stmts, source_stmt);
    if (Lnast_ntype::is_func_break(type) || Lnast_ntype::is_func_continue(type)) {
      auto stop = body->add_child(dst, Lnast_ntype::create_store());
      copy_srcid(source_stmt, stop);
      body->add_child(stop, Lnast_node::create_ref(std::string(kLoopExec)));
      body->add_child(stop, Lnast_node::create_const("false"));
      if (Lnast_ntype::is_func_break(type)) {
        auto next = body->add_child(dst, Lnast_ntype::create_store());
        copy_srcid(source_stmt, next);
        body->add_child(next, Lnast_node::create_ref(std::string(kLoopNextActive)));
        body->add_child(next, Lnast_node::create_const("false"));
      }
      return;
    }
    if (Lnast_ntype::is_if(type) || Lnast_ntype::is_unique_if(type)) {
      auto copied_if = body->add_child(dst, type);
      copy_srcid(source_stmt, copied_if);
      for (auto c : src->children(source_stmt)) {
        if (Lnast_ntype::is_stmts(src->get_type(c))) {
          auto copied_arm = body->add_child(copied_if, Lnast_ntype::create_stmts());
          copy_srcid(c, copied_arm);
          copy_controlled_stmts(c, copied_arm);
        } else {
          copy_subtree_into(src, c, body, copied_if, nullptr);
        }
      }
      return;
    }
    // A nested loop owns its own break/continue. Treat the whole node as one
    // guarded statement; its runner invocation will lower its control at the
    // inner loop boundary rather than stealing it for this loop.
    copy_subtree_into(src, source_stmt, body, dst, nullptr);
  };

  if (plan.has_loop_control) {
    copy_controlled_stmts(body_stmts, stmts);
  } else {
    // `for` is an SSA scope barrier, so its names are raw source names and the
    // port names above were chosen to match them.
    for (auto c : src->children(body_stmts)) {
      copy_subtree_into(src, c, body, stmts, nullptr);
    }
  }

  // Preserve nonblocking register semantics while the flop remains outside
  // the loop. Explicit reads see Q; stores and partial-write bases update D.
  if (!plan.registers.empty()) {
    absl::flat_hash_map<std::string, std::string> register_writes;
    std::function<void(const Lnast_nid&)>         collect_register_writes = [&](const Lnast_nid& nid) {
      if (Lnast_ntype::is_store(body->get_type(nid))) {
        auto dst   = body->get_first_child(nid);
        auto value = dst.is_invalid() ? dst : body->get_sibling_next(dst);
        if (!value.is_invalid() && plan.registers.contains(std::string(body->get_name(dst)))
            && Lnast_ntype::is_ref(body->get_type(value))) {
          register_writes[std::string(body->get_name(value))] = body->get_name(dst);
        }
      }
      for (auto child : body->children(nid)) {
        collect_register_writes(child);
      }
    };
    collect_register_writes(stmts);
    std::function<void(const Lnast_nid&, bool)> rewrite_registers = [&](const Lnast_nid& nid, bool keys) {
      const auto type   = body->get_type(nid);
      auto       target = body->get_first_child(nid);
      if (Lnast_ntype::is_store(type) && !keys && !target.is_invalid()) {
        const std::string name{body->get_name(target)};
        if (plan.registers.contains(name)) {
          body->set_name(target, name + std::string(kCarryNextSuffix));
        }
      }
      if (Lnast_ntype::is_set_mask(type) && !target.is_invalid()) {
        auto base = body->get_sibling_next(target);
        if (!base.is_invalid()) {
          const std::string name{body->get_name(base)};
          if (auto it = register_writes.find(std::string(body->get_name(target))); it != register_writes.end()) {
            // SSA may name the previous partial-write result as the base.
            // That temporary belongs to the enclosing definition; this
            // boundary transports the accumulated pending value instead.
            body->set_name(base, it->second + std::string(kCarryNextSuffix));
          } else if (plan.registers.contains(name)) {
            body->set_name(base, name + std::string(kCarryNextSuffix));
          }
        }
      }
      const bool child_keys = makes_store_keys(*body, nid);
      for (auto child : body->children(nid)) {
        rewrite_registers(child, child_keys);
      }
    };
    rewrite_registers(stmts, false);
  }

  // A callee named through an IMPORT ALIAS (`const tap = import("tap.tap")`,
  // then `tap(...)`) is a const bound to the callee's tree name in THIS unit's
  // symbol table; the lifted definition is compiled on its own and cannot see
  // that binding ("call to undefined function 'tap'"). Spell the resolved tree
  // name into the copied call instead, exactly what the call path itself does
  // through try_fold_ref.
  {
    std::function<void(const Lnast_nid&)> resolve_callees = [&](const Lnast_nid& nid) {
      if (nid.is_invalid()) {
        return;
      }
      if (Lnast_ntype::is_func_call(body->get_type(nid))) {
        auto dst = body->get_first_child(nid);
        auto fn  = dst.is_invalid() ? dst : body->get_sibling_next(dst);
        if (!fn.is_invalid() && Lnast_ntype::is_ref(body->get_type(fn))) {
          const std::string callee{body->get_name(fn)};
          if (auto fv = try_fold_ref(callee); fv && fv->is_string()) {
            auto name = fv->to_pyrope();
            if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'') {
              name = name.substr(1, name.size() - 2);
            }
            if (name.starts_with("ln:")) {
              name = name.substr(3);
            }
            if (!name.empty() && name != callee) {
              body->set_name(fn, name);
            }
          }
        }
      }
      for (auto c : body->children(nid)) {
        resolve_callees(c);
      }
    };
    resolve_callees(stmts);
  }

  // Epilogue: publish each carry on its output port.
  for (const auto& n : plan.carries) {
    auto wb = body->add_child(stmts, Lnast_ntype::create_store());
    body->add_child(wb, Lnast_node::create_ref(n + std::string(kCarryOutSuffix)));
    body->add_child(wb, Lnast_node::create_ref(plan.registers.contains(n) ? n + std::string(kCarryNextSuffix) : n));
  }

  // The body was copied from the enclosing definition's SSA'd tree, so it can
  // hold private `<base>___ssa_<N>` versions -- a boundary port too, when an
  // invariant or final is one (`mut v:u8 = 0; v = a` before the loop). The
  // lifted definition is SSA'd again on its own, and that run demotes every
  // private version it meets (`___ssa_<N>` -> `__w<N>`), so a port renamed
  // there no longer matches the name the rolled call binds. Demote here, by
  // the same rule, and have the call use the recorded port names.
  const auto demoted = upass::demote_stale_ssa(*body, root);
  auto&      ranges  = body->bw_meta().ranges;
  for (const auto& [from, to] : demoted) {
    if (plan.actual_names.contains(from)) {
      plan.port_names.emplace(from, to);
    }
    if (auto it = ranges.find(from); it != ranges.end()) {
      const auto r = it->second;
      ranges.erase(it);
      ranges.insert_or_assign(to, r);
    }
  }
  return body;
}

void uPass_runner::emit_rolled_loop_call(const Loop_roll_plan& plan, const Lnast_nid& source_body) {
  // The compact loop survives as one explicit LNAST node. Its first fields are
  // the source-level domain/body that prp_writer round-trips; its final stmts
  // child is a hidden ordinary call + result-binding payload that tolg lowers.
  // Replication metadata is NOT smuggled through reserved call actuals.
  // No-emit walk (the func_extract pre-loop): there is no netlist to append to,
  // but the loop still turns its results into runtime values, so invalidate the
  // comptime seeds exactly as the emitting path below does. Returning without
  // that would let the pre-loop keep folding the PRE-loop value of every carry.
  if (!staging) {
    const auto invalidate = [&](const std::string& n) {
      const auto& actual = plan.actual_names.at(n);
      if (const auto tit = plan.types.find(n); tit != plan.types.end() && tit->second.array_size > 0) {
        const std::string unit{lm->unit_lnast()->get_top_module_name()};
        for (int64_t e = 0; e < tit->second.array_size; ++e) {
          const std::string lane = actual + "." + std::to_string(e);
          (void)symbol_table_.set(lane, Bundle::invalid_lconst);
          symbol_table_.field_touched.insert(Symbol_table::field_touch_key(unit, lane));
        }
      } else {
        (void)symbol_table_.set(actual, Bundle::invalid_lconst);
      }
    };
    for (const auto& n : plan.finals) {
      invalidate(n);
    }
    for (const auto& n : plan.carries) {
      invalidate(n);
    }
    return;
  }
  // Park no write across the call. This node is appended to staging DIRECTLY,
  // unlike the emit_named_instance_call/emit_inline_tuple_pick pair it replaced,
  // and both of those bracketed their work with flush_deferred_emits(). Without
  // it a write the coalescer parked before the loop (`acc = seed` ahead of `for
  // .. { acc = acc + i }`) is flushed AFTER the call, so the loop's carry-in
  // actual reads the pre-seed value while the unrolled build reads the seed.
  flush_deferred_emits();
  const auto& src    = lm->get_lnast();
  auto        rolled = staging->add_child(staging_parent, Lnast_ntype::create_rolled_for());
  if (const auto id = src->get_srcid(source_body); id != hhds::SourceId_invalid) {
    staging->set_srcid(rolled, staging->source_locator().import_from(src->source_locator(), id));
  }
  staging->add_child(rolled, Lnast_node::create_ref(plan.ivar));
  staging->add_child(rolled, Lnast_node::create_const(std::to_string(plan.first)));
  staging->add_child(rolled, Lnast_node::create_const(std::to_string(plan.step)));
  staging->add_child(rolled, Lnast_node::create_const(std::to_string(plan.count)));
  staging->add_child(rolled, Lnast_node::create_const(plan.has_loop_control ? std::string(kLoopValid) : std::string{}));
  staging->add_child(rolled, Lnast_node::create_const(plan.has_loop_control ? std::string(kLoopNextActive) : std::string{}));
  auto carry_map = staging->add_child(rolled, Lnast_ntype::create_tuple_add());
  for (const auto& n : plan.carries) {
    auto map = staging->add_child(carry_map, Lnast_ntype::create_store());
    staging->add_child(map, Lnast_node::create_ref(n + std::string(kCarryInSuffix)));
    staging->add_child(map, Lnast_node::create_const(n + std::string(kCarryOutSuffix)));
    if (plan.registers.contains(n)) {
      staging->add_child(map, Lnast_node::create_ref(plan.actual_names.at(n)));
      staging->add_child(map, Lnast_node::create_ref("%" + plan.inst + "_" + n + "_seed"));
    }
  }
  // The retained source is replayed when a processed comb is inlined. Its
  // enclosing constant declarations may already have been folded away, so
  // retain their values here as well as in the lifted implementation.
  absl::flat_hash_map<std::string, Generic_bind> captured;
  for (const auto& [name, value] : plan.constants) {
    captured[name].const_text = value.to_pyrope();
  }
  // A re-rolled inline body lives in the caller's namespace. The hardware
  // payload already binds actual_names; retain the same names in the source
  // body used by the Pyrope writer and by any subsequent inline replay.
  //
  // Under STREAMING SSA (a `::[timecheck=false]` unit that reassigns a plain
  // scalar in straight-line code) the LNAST keeps the BASE name on every read
  // and only the runner versions it, at emit time (emit_ref_or_folded ->
  // stream_ssa_ref_name). A loop INVARIANT is such a read: `mut x = 0 ; x = a ;
  // for .. { .. x .. }` must see `x___ssa_1`, not the version-0 seed `x` --
  // binding the raw name made the loop read the declaration's 0 and DCE then
  // deleted the now-unread `x___ssa_1 = a` (lhdtrack br_ram_data_rd_pipe's
  // Pyrope re-emission: an inlined comb's `mut inl_in = 0 ; inl_in = row_data_q`
  // read by its rolled mux loop). The retained source body must read the same
  // version, or the Pyrope writer re-prints the stale seed. Only read-only
  // names: a name the body writes is renamed on both sides of its stores, and
  // upass_ssa forces the full SSA rebuild for a stream name written under a
  // `for` (so a carry is never versioned here).
  const auto bound_actual = [&](const std::string& n) -> std::string {
    const auto& actual = plan.actual_names.at(n);
    if (std::ranges::find(plan.invariants, n) == plan.invariants.end()) {
      return actual;
    }
    return stream_ssa_ref_name(actual);
  };
  for (const auto& [name, actual] : plan.actual_names) {
    if (name == plan.ivar || captured.contains(name)) {
      continue;
    }
    auto bound = bound_actual(name);
    if (name != bound) {
      captured[name].func_name = std::move(bound);
    }
  }
  // Compiler temporaries are local to this retained loop, too. Multiple
  // inline calls can retain identically named parser temps in sibling loops.
  for (const auto& node : src->depth_preorder(source_body)) {
    const auto n    = Lnast_nid(node);
    const auto name = src->get_name(n);
    if (Lnast_ntype::is_ref(src->get_type(n)) && Lnast::is_tmp(name)) {
      captured[std::string(name)].func_name = "%" + plan.inst + "_" + std::string(name.substr(1));
    }
  }
  copy_subtree_into(src, source_body, staging, rolled, &captured);

  // The lifted definition's port for a boundary name (see lift_loop_body).
  const auto port_of = [&](const std::string& n) -> const std::string& {
    const auto it = plan.port_names.find(n);
    return it == plan.port_names.end() ? n : it->second;
  };
  auto                                            lowered = staging->add_child(rolled, Lnast_ntype::create_stmts());
  std::vector<std::pair<std::string, Lnast_node>> actuals;
  for (const auto& n : plan.invariants) {
    actuals.emplace_back(port_of(n), Lnast_node::create_ref(bound_actual(n)));
  }
  for (const auto& n : plan.carries) {
    // See bound_actual: upass_ssa keeps a stream-SSA name written in a loop
    // body off the streaming path, so a carry's raw name IS its live version
    // (the result binding below writes the raw name back).
    I(stream_ssa_ref_name(plan.actual_names.at(n)) == plan.actual_names.at(n), "a rolled-loop carry is a streaming-SSA name");
    if (plan.registers.contains(n)) {
      actuals.emplace_back(port_of(n), Lnast_node::create_ref(plan.actual_names.at(n)));
      actuals.emplace_back(n + std::string(kCarryInSuffix), Lnast_node::create_ref("%" + plan.inst + "_" + n + "_seed"));
    } else {
      actuals.emplace_back(n + std::string(kCarryInSuffix), Lnast_node::create_ref(plan.actual_names.at(n)));
    }
  }
  actuals.emplace_back(std::string(kLoopValid), Lnast_node::create_const("true"));

  const auto dst  = std::string("%") + plan.inst + "_r";
  auto       call = staging->add_child(lowered, Lnast_ntype::create_func_call());
  staging->add_child(call, Lnast_node::create_ref(dst));
  staging->add_child(call, Lnast_node::create_ref(plan.mangled));
  auto inst = staging->add_child(call, Lnast_ntype::create_store());
  staging->add_child(inst, Lnast_node::create_ref("__inst_name"));
  staging->add_child(inst, Lnast_node::create_const(plan.inst));
  for (const auto& [name, value] : actuals) {
    auto arg = staging->add_child(call, Lnast_ntype::create_store());
    staging->add_child(arg, Lnast_node::create_ref(name));
    staging->add_child(arg, value);
  }

  std::vector<std::pair<std::string, std::string>> results;
  results.reserve(plan.finals.size() + plan.carries.size());
  for (const auto& n : plan.finals) {
    results.emplace_back(n, port_of(n));
  }
  for (const auto& n : plan.carries) {
    results.emplace_back(n, n + std::string(kCarryOutSuffix));
  }
  const size_t callee_outputs = results.size() + (plan.has_loop_control ? 1u : 0u);
  for (const auto& [raw_name, port] : results) {
    const auto& name = plan.actual_names.at(raw_name);
    if (callee_outputs == 1) {
      auto bind = staging->add_child(lowered, Lnast_ntype::create_store());
      staging->add_child(bind, Lnast_node::create_ref(name));
      staging->add_child(bind, Lnast_node::create_ref(dst));
    } else if (plan.registers.contains(raw_name)) {
      // A register carry returns the flop's NEXT value, so it must reach the
      // register through a STORE (tolg's next-state write). A tuple_get
      // straight into the register name rebinds the name instead: the flop
      // keeps its Q forever and a later read sees D.
      const auto next = "%" + plan.inst + "_" + raw_name + "_next";
      auto       pick = staging->add_child(lowered, Lnast_ntype::create_tuple_get());
      staging->add_child(pick, Lnast_node::create_ref(next));
      staging->add_child(pick, Lnast_node::create_ref(dst));
      staging->add_child(pick, Lnast_node::create_const(port));
      auto bind = staging->add_child(lowered, Lnast_ntype::create_store());
      staging->add_child(bind, Lnast_node::create_ref(name));
      staging->add_child(bind, Lnast_node::create_ref(next));
    } else {
      auto bind = staging->add_child(lowered, Lnast_ntype::create_tuple_get());
      staging->add_child(bind, Lnast_node::create_ref(name));
      staging->add_child(bind, Lnast_node::create_ref(dst));
      staging->add_child(bind, Lnast_node::create_const(port));
    }
    // The source value is a runtime result now. Invalidate the old comptime
    // seed just as an uncertain scope does, so later statements cannot fold it
    // back to the pre-loop value.
    if (const auto tit = plan.types.find(raw_name); tit != plan.types.end() && tit->second.array_size > 0) {
      // An ARRAY carry comes back as one packed runtime value, but the symbol
      // table still holds its pre-loop LANES ("0".."N-1"): invalidating the bare
      // name only clears slot 0, and a later `v[3]` then constant-folds to the
      // seed (MEASURED: `y = v[0] + v[3]` after a rolled write loop lowered to
      // `tuple_get(v,0) + 0`). Every lane is runtime after the loop.
      const std::string unit{lm->unit_lnast()->get_top_module_name()};
      for (int64_t e = 0; e < tit->second.array_size; ++e) {
        const std::string lane = name + "." + std::to_string(e);
        (void)symbol_table_.set(lane, Bundle::invalid_lconst);
        // The loop wrote the lane (through the carry): say so, or constprop's
        // unset-unused-field sweep reports every lane "declared but never set".
        symbol_table_.field_touched.insert(Symbol_table::field_touch_key(unit, lane));
      }
    } else {
      (void)symbol_table_.set(name, Bundle::invalid_lconst);
    }
  }
}

void uPass_runner::emit_named_instance_call(const std::string& dst, const std::string& callee_ref, const std::string& inst_name,
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
  // The rewritten callee may be a specialization minted by THIS runner and
  // therefore not present in the shared registry until the queue folds in
  // new_lnasts. stamp_loop_inst_suffix cannot classify that name yet. Carry
  // the loop identity explicitly so cold generation and a mixed run that has
  // the same specialization restored name the Sub identically.
  if (!loop_iter_ordinals_.empty()) {
    auto is = s->add_child(root, Lnast_ntype::create_store());
    s->add_child(is, Lnast_node::create_ref(std::string(call_inst_suffix_marker)));
    s->add_child(is, Lnast_node::create_const(loop_inst_suffix()));
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
      fcall_arg_fail(
          call_span,
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
      case Io_kind::boolean: return "Bool";
      case Io_kind::string : return "String";
      case Io_kind::integer:
        if (gb.max || gb.min) {
          return std::format("Signed(max={}, min={})",
                             gb.max ? std::string(gb.max->to_pyrope()) : "nil",
                             gb.min ? std::string(gb.min->to_pyrope()) : "nil");
        }
        return "Signed";
      default: return "untyped";
    }
  };

  // Resolve a TYPE NAME (an explicit `<…>` argument: a `'type'` declare tmp
  // or a named type) into a binding.
  const auto bind_of_type_name = [&](const std::string& tn, std::string from) -> Generic_bind {
    Generic_bind gb;
    gb.from = std::move(from);
    // Declaration defaults carry source type tokens rather than the typed
    // temporary emitted for an explicit `<U8>` actual (the built-in type
    // words, docs 07-typesystem; the old lowercase spellings are banned words
    // the front end already rejected).
    if (tn == "Bool") {
      gb.kind = Io_kind::boolean;
      gb.max  = *Dlop::create_integer(1);
      gb.min  = *Dlop::create_integer(0);
      return gb;
    }
    if (tn == "String") {
      gb.kind = Io_kind::string;
      return gb;
    }
    if (tn == "Unsigned") {
      gb.kind = Io_kind::integer;
      gb.min  = *Dlop::create_integer(0);
      return gb;
    }
    if (tn == "Signed") {
      gb.kind = Io_kind::integer;
      return gb;
    }
    // Width sugar `U<N>`/`S<N>` -- the same spelling set uPass_constprop's
    // does_operand decodes. int64_t (not int) so an out-of-int spelling like
    // `U9999999999` still lands on the diagnostic below instead of falling
    // through and being substituted verbatim as a named type.
    //
    // KNOWN GAP: unlike uPass_constprop::does_operand (which decodes a type token
    // only when `!is_known_var`, "a real variable wins over a type-token
    // spelling"), this decode runs BEFORE any value lookup. On the
    // specialize_top_defaults path the symbol table is empty, so a top generic
    // whose DEFAULT names a comptime constant spelled like a width token
    // (`comptime const i2 = 3` used as `<N=i2>`) binds as a TYPE and the clone
    // gets a prim_type node in a value position -- reported as
    // `unresolved reference ''`. Fixing it needs the file-unit constant scope
    // seeded here (a call site is unaffected: bind_of_explicit_arg folds the ref
    // first). Compile-time error, never wrong hardware.
    if (tn.size() > 1 && (tn.front() == 'U' || tn.front() == 'S')) {
      int64_t    width  = 0;
      const auto parsed = std::from_chars(tn.data() + 1, tn.data() + tn.size(), width);
      if ((parsed.ec == std::errc{} || parsed.ec == std::errc::result_out_of_range) && parsed.ptr == tn.data() + tn.size()
          && (width > 0 || parsed.ec == std::errc::result_out_of_range)) {
        // Bound BEFORE materializing: max_from_bits builds a 2^N-1 Dlop and the
        // LNAST const it is stringified into is what exhausts memory. Both
        // explicit spellings (`(a:uN)`, `f<uN>(…)`) already refuse above this.
        if (parsed.ec == std::errc::result_out_of_range || width > upass::kMaxIntTypeWidth) {
          fcall_arg_fail(
              call_span,
              "width-too-large",
              std::format("integer type width '{}' in {} is out of range (0..{} bits)", tn, gb.from, upass::kMaxIntTypeWidth),
              "use a smaller bit width");
        }
        gb.kind = Io_kind::integer;
        gb.max  = upass::max_from_bits(static_cast<uint32_t>(width), tn.front() == 'S');
        gb.min  = upass::min_from_bits(static_cast<uint32_t>(width), tn.front() == 'S');
        return gb;
      }
    }
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
    // File-level scalar aliases are outside the extracted caller's symbol
    // table. Resolve their range before cloning a template in another file:
    // copying the bare alias there loses the declaration used by wrap/sat.
    if (const auto [owner, type_n] = lookup_file_type(tn); owner) {
      const auto type = owner->get_type(type_n);
      if (Lnast_ntype::is_prim_type_int(type)) {
        const auto max_n = owner->get_first_child(type_n);
        const auto min_n = max_n.is_invalid() ? max_n : owner->get_sibling_next(max_n);
        if (!max_n.is_invalid() && !min_n.is_invalid()) {
          gb.kind = Io_kind::integer;
          // An unbounded scalar type carries nil bounds. Preserve those as
          // absent bounds instead of dereferencing a failed numeric parse.
          if (auto bound = Dlop::from_pyrope(owner->get_name(max_n)); bound && bound->is_integer()) {
            gb.max = *bound;
          }
          if (auto bound = Dlop::from_pyrope(owner->get_name(min_n)); bound && bound->is_integer()) {
            gb.min = *bound;
          }
          return gb;
        }
      } else if (Lnast_ntype::is_prim_type_bool(type)) {
        gb.kind = Io_kind::boolean;
        return gb;
      } else if (Lnast_ntype::is_prim_type_string(type)) {
        gb.kind = Io_kind::string;
        return gb;
      }
    }
    if (detuple_registry_ != nullptr) {
      if (const auto it = detuple_registry_->named_types.find(detuple_registry_key(tn));
          it != detuple_registry_->named_types.end()) {
        gb.tuple_fields       = it->second;
        std::string signature = detuple_registry_key(tn);
        for (const auto& field : gb.tuple_fields) {
          signature += std::format("\n{}:{}:{}:{}",
                                   field.name,
                                   static_cast<int>(field.type.kind),
                                   field.type.max.to_pyrope(),
                                   field.type.min.to_pyrope());
        }
        gb.type_name = tn + "__" + std::to_string(livehd::hash_util::fnv1a64(signature));
        return gb;
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
  // `raw` is the arg's SOURCE spelling (Generic_actual::src_name; empty for a
  // const / tmp / declaration default): `s` is frame-renamed inside an inlined
  // body, `raw` never is.
  const auto bind_of_explicit_arg = [&](const std::string& s, std::string from, const std::string& raw = {}) -> Generic_bind {
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
    if (std::isdigit(static_cast<unsigned char>(c0)) != 0
        || (s.size() > 1 && (c0 == '-' || c0 == '+') && std::isdigit(static_cast<unsigned char>(s[1])) != 0)) {
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
    // A ref to THIS frame's own function-valued param (`apply_g<F=f>` inside
    // `outer(f)`, `s` is the frame-tagged `inlN_f`) forwards the function it is
    // bound to, exactly as func_actual_name does for a value-param `g(f=f)`.
    // It is registered only in func_param_bindings_ (never as a value), so
    // without this it fell through to a TYPE bind and the body's `F(v)` became
    // a call to an undefined `inlM_inlN_f`.
    if (const auto fb = func_param_bindings_.find(s); fb != func_param_bindings_.end()) {
      Generic_bind gb;
      gb.from      = std::move(from);
      gb.func_name = fb->second;
      return gb;
    }
    if (lookup_callee(s) != nullptr) {
      // An import alias spelled like a registry function (`const inc =
      // import("lib2.hh")` while the template's file has its own `inc`) binds
      // the alias TARGET, pinned by its full registry name: the raw spelling
      // resolves by name to the same wrong body from both scopes, so
      // frame_portable_func_name / the clone pin could never tell them apart.
      auto         aliased = value_bound_func_name(s, s);
      Generic_bind gb;
      gb.from      = std::move(from);
      gb.func_name = aliased.empty() ? s : std::move(aliased);
      return gb;
    }
    // Inside an inline frame (e.g. a generic's clone spliced in place, `e<inc>`
    // in `g<N>`) the ref arrives frame-tagged (`inl1_inc`), which no registry
    // function is named. Resolve its SOURCE spelling exactly as
    // func_actual_name does for a value-param `f(g=inc)`; without this it fell
    // through to a TYPE bind and the body's `F(v)` became a call to an
    // undefined `inl2_inl1_inc`. A frame-local value of that spelling (a
    // comptime constant, a variable) shadows the function and keeps the paths
    // below; an import-alias string binds its target (value_bound_func_name).
    if (!raw.empty() && raw != s && lm->in_inline_frame() && lookup_callee(raw) != nullptr) {
      auto aliased = value_bound_func_name(s, raw);
      if (!aliased.empty()) {
        Generic_bind gb;
        gb.from      = std::move(from);
        gb.func_name = std::move(aliased);
        return gb;
      }
      const auto fv = try_fold_ref(s);
      const bool shadowed
          = (fv && !fv->is_invalid()) || upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), s).has_value();
      if (!shadowed) {
        Generic_bind gb;
        gb.from      = std::move(from);
        gb.func_name = raw;
        return gb;
      }
    }
    // A generic bound to a comptime VALUE rather than a literal: a named
    // constant (`f<N=SIZE>`), a loop index bound by the unroller
    // (`f<N=lvl>`), or the tmp prp2lnast lowered a parenthesized expression
    // into (`f<N=(SIZE >> 1)>`). All three arrive as a ref, and without this
    // they fell through to bind_of_type_name and became a named TYPE bind:
    // nothing then emitted a value binding for the generic, so the callee's
    // body read of it reached tolg as an undriven `inl<n>_<G>`. A type name
    // never folds (only a trivial known scalar does), so a real `f<u8>` /
    // `f<Byte>` still takes the type path below.
    if (auto fv = try_fold_ref(s);
        fv && !fv->is_invalid() && !fv->has_unknowns() && (fv->is_string() || fv->is_bool() || fv->is_integer())) {
      Generic_bind gb;
      gb.from       = std::move(from);
      gb.const_text = std::string(fv->to_pyrope());
      if (fv->is_string()) {
        gb.kind = Io_kind::string;
        return gb;
      }
      if (fv->is_bool()) {
        // Mirror the literal `f<true>` branch above: a bool binds the boolean
        // kind with a 0..1 envelope, NOT an integer pinned to its own value --
        // `Io_kind::integer` here would make a `:B` slot a 1-valued int type.
        gb.kind = Io_kind::boolean;
        gb.max  = *Dlop::from_pyrope("1");
        gb.min  = *Dlop::from_pyrope("0");
        return gb;
      }
      gb.kind = Io_kind::integer;
      gb.max  = *fv;  // a constant pins its own value as the type envelope (D)
      gb.min  = *fv;
      if (!Lnast::is_tmp(s)) {
        if (const auto df = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), s); df && df->has_type_spec) {
          gb.decl_typed = true;
          gb.decl_max   = df->range_max;
          gb.decl_min   = df->range_min;
        }
      }
      return gb;
    }
    // A variable or an input/output of the caller that does not fold is a
    // runtime value: bound as a type name it would read as nil in the callee.
    const auto  df    = upass::decl_facts::lookup(symbol_table_, lm->get_lnast().get(), s);
    const auto& cio   = lm->get_lnast()->io_meta();
    const auto  is_io = [&](const std::vector<Lnast_io_entry>& es) {
      return std::any_of(es.begin(), es.end(), [&](const Lnast_io_entry& e) { return e.name == s; });
    };
    const bool runtime = (df
                          && (df->mode == upass::Mode::mut_kind || df->mode == upass::Mode::const_kind
                              || df->mode == upass::Mode::reg_kind || df->mode == upass::Mode::wire_kind))
                         || is_io(cio.inputs) || is_io(cio.outputs);
    if (runtime && !Lnast::is_tmp(s)) {
      fcall_arg_fail(call_span,
                     "fcall-generic-runtime",
                     std::format("{} of `{}` is `{}`, a runtime value: a generic is bound at compile time", from, callee_name, s),
                     "bind a constant, a type or a lambda; pass a runtime value as a call argument");
    }
    return bind_of_type_name(s, std::move(from));
  };

  // A generic left unbound by the call takes its DECLARATION default (`<T,
  // N=1>`, todo 3g B): the default text is re-classified exactly like an
  // explicit `<…>` argument (type / constant / lambda).
  const auto& gdefaults      = callee->get_generic_defaults();
  const auto  apply_defaults = [&]() {
    for (std::size_t i = 0; i < gens.size(); ++i) {
      if (binds.count(gens[i]) != 0u) {
        continue;
      }
      const std::string def = (i < gdefaults.size()) ? gdefaults[i] : std::string{};
      if (!def.empty()) {
        auto gb         = bind_of_explicit_arg(def, std::format("the default for generic `{}`", gens[i]));
        gb.from_default = true;  // identity-specialization test (see maybe_specialize_template_call)
        binds[gens[i]]  = std::move(gb);
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
                        g,
                        callee_name,
                        what,
                        g,
                        what),
            "bind a type there (e.g. `f<U8>`), or use the generic only as a value/lambda in the body");
      }
    }
  };

  // A generic that types a param/output (`:G`), vs one used as a value.
  const auto is_type_generic = [&](const std::string& g) {
    const auto in_slot = [&](const std::vector<Lnast_io_entry>& es) {
      return std::any_of(es.begin(), es.end(), [&](const Lnast_io_entry& e) { return e.type_name == g; });
    };
    return in_slot(io.inputs) || in_slot(io.outputs);
  };

  // 1) Explicit `<…>` bindings — named and/or positional, following the same
  // argument-naming rules as call arguments (todo 3g C). NAMED binds (`f<T=u8>`)
  // bind by name; POSITIONAL binds fill the remaining generics in declaration
  // order. A PARTIAL list is legal when the still-unbound generics carry
  // declaration defaults (todo 3g B). Binding MORE than declared is an error.
  if (!explicit_generics.empty()) {
    if (explicit_generics.size() > gens.size()) {
      fcall_arg_fail(call_span,
                     "fcall-generic-arity",
                     std::format("`{}` declares {} generic parameter(s) but the call binds {}",
                                 callee_name,
                                 gens.size(),
                                 explicit_generics.size()),
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
      binds[ga.name] = bind_of_explicit_arg(ga.value, std::format("the named bind `{}=…`", ga.name), ga.src_name);
    }
    // Positional binds follow the SAME naming exceptions as call arguments
    // (06-functions.md §"Argument naming"), NOT declaration-order fill: a bare
    // name that matches a generic (exc 2), a single free generic (exc 1), or a
    // role (type vs value) that uniquely picks one free generic (exc 3). An
    // ambiguous positional bind (e.g. two free type generics `two<signed,string>`)
    // must be named `<Name=…>`. Role-matching also fixes the case where the
    // declaration order does not match the actuals' roles (`<N,T>` bound `<u4,3>`).
    for (const auto& ga : explicit_generics) {
      if (!ga.name.empty()) {
        continue;  // named binds already applied above
      }
      const Generic_bind cand
          = bind_of_explicit_arg(ga.value, std::format("the explicit `<…>` argument `{}`", ga.value), ga.src_name);
      const bool  cand_is_type = cand.const_text.empty() && cand.func_name.empty();  // type bind vs value/lambda
      std::size_t target       = gens.size();
      // Exception 2: a bare identifier whose SOURCE name matches an unbound
      // generic name (`ga.value` is frame-renamed inside an inlined body),
      // the exact name first (see Actual::src_name).
      for (const std::string* nm : {&ga.src_name, &ga.src_base}) {
        const auto it = std::find(gens.begin(), gens.end(), *nm);
        if (!nm->empty() && it != gens.end() && binds.count(*it) == 0u) {
          target = static_cast<std::size_t>(it - gens.begin());
          break;
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
                                   cand_is_type ? "type" : "value",
                                   callee_name),
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
    auto&      prev       = it->second;
    const auto kind_of    = [](const Generic_bind& b) { return b.type_name.empty() ? b.kind : Io_kind::none; };
    const bool same_named = !prev.type_name.empty() && prev.type_name == cand.type_name;
    bool       compatible = same_named;
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
      fcall_arg_fail(
          call_span,
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
  // A VALUE generic nothing bound (no `<…>` bind, no default; only a `:G`
  // type is inferred) would read as nil in the body: a direct error at the
  // call, as with an explicit list, naming the port whose width it sets (the
  // deferred_port_type wording). A type generic an untyped actual left open
  // stays open.
  for (const auto& g : gens) {
    if (binds.count(g) != 0u || is_type_generic(g)) {
      continue;
    }
    std::string port;
    for (const auto* es : {&io.inputs, &io.outputs}) {
      for (const auto& e : *es) {
        for (const auto* text : {&e.bound_max_text, &e.bound_min_text}) {
          std::string unbound;
          if (port.empty() && !text->empty() && *text != "nil" && !fold_template_bound(callee, *text, binds, 0, &unbound)
              && unbound == g) {
            port = e.name;
          }
        }
      }
    }
    fcall_arg_fail(call_span,
                   "fcall-generic-arity",
                   port.empty() ? std::format("generic `{}` of `{}` is unbound and has no default", g, callee_name)
                                : std::format("generic `{}` of `{}` is unbound and has no default (it sets the width of port `{}`)",
                                              g,
                                              callee_name,
                                              port),
                   std::format("bind it in the `<…>` list or declare a default `<{}=…>`", g));
  }
  validate_kinds();
  return binds;
}

std::shared_ptr<Lnast> uPass_runner::specialize_top_defaults() {
  const auto  callee   = lm->get_lnast();
  const auto  name     = std::string(callee->get_top_module_name());
  const auto& gens     = callee->get_generics();
  const auto& defaults = callee->get_generic_defaults();
  for (std::size_t i = 0; i < gens.size(); ++i) {
    if (i >= defaults.size() || defaults[i].empty()) {
      fcall_arg_fail(lm->current_span(),
                     "top-generic-default",
                     std::format("generic `{}` of top `{}` has no default", gens[i], name),
                     "declare a default or select a concrete caller as the top");
    }
  }
  const auto binds = resolve_generic_binds(callee, callee->io_meta(), {}, {}, 0, {}, name, lm->current_span());
  // Every INPUT must end up concretely typed. There is no call site here to
  // infer a width from (the rule `type_from_actual` enforces as
  // `fcall-untyped-actual`), so an untyped input would silently lower as a 1-bit
  // SIGNED wire -- `pub mod u2<N=3>(a) -> (y:u9@[0])` emitted
  // `input signed a` and reported "pass", while the same module WITHOUT the
  // generic is refused outright. Outputs are legitimately inferred from the body.
  for (const auto& e : callee->io_meta().inputs) {
    if (e.bits > 0 || e.has_range || e.kind != Io_kind::none || e.array_size > 0 || e.is_varargs
        || !unsized_array_port(*callee, e, false).is_invalid()) {
      continue;  // already concrete (an array folds under the defaults while cloning)
    }
    bool resolved = false;
    if (!e.type_name.empty()) {
      if (const auto it = binds.find(e.type_name); it != binds.end()) {
        // A width token the parse REJECTED (`u0`, `u99999999999`) leaves neither
        // an envelope nor a boolean kind, so it must not pass as "typed".
        resolved = it->second.max.has_value() || it->second.min.has_value() || it->second.kind == Io_kind::boolean;
      } else {
        resolved = true;  // a plain named type -- substituted verbatim, checked downstream
      }
    }
    if (!resolved) {
      fcall_arg_fail(lm->current_span(),
                     "top-untyped-port",
                     std::format("input `{}` of top `{}` has no declared type — a `{}` boundary needs an explicit width",
                                 e.name,
                                 name,
                                 callee->get_lambda_kind()),
                     "annotate the port (e.g. `a:U8`), or give the generic it is typed by a resolvable default");
    }
  }
  // Generic-WIDTH ports (`a12:unsigned(bits=LANES * 4)`) are folded under the
  // defaults exactly like a call-site specialization does.
  const auto&            io = callee->io_meta();
  std::vector<Spec_port> inject(io.inputs.size());
  std::vector<Spec_port> out_inject(io.outputs.size());
  for (std::size_t i = 0; i < io.inputs.size(); ++i) {
    if (io.inputs[i].has_deferred_bound()) {
      inject[i] = deferred_port_type(callee, io.inputs[i], binds, name, lm->current_span());
    }
  }
  for (std::size_t i = 0; i < io.outputs.size(); ++i) {
    if (io.outputs[i].has_deferred_bound()) {
      out_inject[i] = deferred_port_type(callee, io.outputs[i], binds, name, lm->current_span());
    }
  }
  return clone_template_specialized(callee, name, inject, {}, {}, out_inject, binds);
}

bool uPass_runner::maybe_specialize_template_call(const std::shared_ptr<Lnast>& callee, const Lnast_tree_io& io,
                                                  const std::vector<Lnast_node>& param_val, const std::vector<bool>& param_set,
                                                  std::size_t nbind, bool has_vararg, const std::vector<Lnast_node>& vararg_pos,
                                                  const std::vector<std::pair<std::string, Lnast_node>>& vararg_named,
                                                  const std::string& dst_name, const std::string& callee_name,
                                                  const livehd::diag::Span&                             call_span,
                                                  const absl::flat_hash_map<std::string, Generic_bind>& gbinds) {
  const auto kind = std::string(callee->get_lambda_kind());

  // Readable bits/sign suffix from a declared (max,min) range — mirrors
  // upass_ssa type_info_from so the name matches the lowered width.
  auto suffix_for = [](const std::optional<Dlop>& max, const std::optional<Dlop>& min) -> std::string {
    const bool is_signed = !(min && !min->is_negative());
    int        bits      = 0;
    if (max && max->is_integer()) {
      if (!is_signed) {
        bits = static_cast<int>(max->get_payload_bits());
      } else {
        const int mb = static_cast<int>(max->get_signed_bits());
        const int nb = (min && min->is_integer()) ? static_cast<int>(min->get_signed_bits()) : mb;
        bits         = std::max(mb, nb);
      }
    }
    return (is_signed ? "S" : "U") + std::to_string(bits);
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
                     "annotate the actual, e.g. `x:U8`");
    };
    if (!av_set || !av.is_ref()) {
      fail();
    }
    Spec_port   sp;
    const auto  an = std::string(av.get_name());
    const auto* ci = caller_input(an);
    if (auto tn = try_typename(an); !tn.empty()) {
      sp = {true, std::nullopt, std::nullopt, std::string(tn)};
      suffix.push_back(std::string(tn));
    } else if ((ci != nullptr && ci->kind == Io_kind::boolean) || try_scalar_kind(an) == Io_kind::boolean) {
      // A bool actual types the port `bool`, never int [0,1]: Pyrope keeps
      // the two apart, so `if en`/`en and b` in the body stay legal.
      sp = bool_spec_port();
      suffix.emplace_back("Bool");
    } else if (auto dt = try_decl_type(an); dt && (dt->range_max || dt->range_min)) {
      sp = {true, dt->range_max, dt->range_min, {}};
      suffix.push_back(suffix_for(dt->range_max, dt->range_min));
    } else if (ci != nullptr && ci->bits > 0) {
      if (ci->is_signed) {
        sp = {true, upass::signed_max_from_bits(ci->bits), upass::signed_min_from_bits(ci->bits), {}};
        suffix.push_back("S" + std::to_string(ci->bits));
      } else {
        sp = {true, upass::unsigned_max_from_bits(ci->bits), *Dlop::from_pyrope("0"), {}};
        suffix.push_back("U" + std::to_string(ci->bits));
      }
    } else {
      fail();
    }
    return sp;
  };

  // A port typed with a GENERIC name is not "already typed": the binding
  // substitutes the concrete type (macro expansion).
  const auto& gens = callee->get_generics();
  auto        is_generic_name
      = [&](const std::string& tn) { return !tn.empty() && std::find(gens.begin(), gens.end(), tn) != gens.end(); };
  auto spec_port_of_bind = [&](const Generic_bind& gb) -> std::optional<Spec_port> {
    if (!gb.type_name.empty()) {
      suffix.push_back(gb.type_name);
      return Spec_port{true, std::nullopt, std::nullopt, gb.type_name};
    }
    if (gb.kind == Io_kind::boolean) {
      suffix.emplace_back("Bool");
      return bool_spec_port();
    }
    if (gb.kind == Io_kind::integer && (gb.max || gb.min)) {
      suffix.push_back(suffix_for(gb.max, gb.min));
      return Spec_port{true, gb.max, gb.min, {}};
    }
    return std::nullopt;  // kind-only / string — no concrete port type
  };

  std::vector<Spec_port> inject(nbind);
  for (std::size_t i = 0; i < nbind; ++i) {
    const auto& e = io.inputs[i];
    if (e.has_deferred_bound()) {
      // A generic-WIDTH port (`a:unsigned(bits=LANES * 4)`): the width is the
      // bound folded under the binds, never the actual's declared type.
      inject[i] = deferred_port_type(callee, e, gbinds, callee_name, call_span);
      suffix.push_back(suffix_for(inject[i].max, inject[i].min));
      continue;
    }
    const bool is_generic = is_generic_name(e.type_name);
    // A generic-shape ARRAY port (`v:[N]u4`) is typed by its declaration: the
    // clone folds `[N]` under the binds, so the actual is never asked.
    const auto array_type = unsized_array_port(*callee, e, false);
    // A half-open `int(max=99)` param carries a range but bits==0; treat it as
    // already-typed too so the actual's type isn't injected over it (cat 1).
    const bool already_typed
        = !is_generic
          && (e.bits > 0 || e.has_range || !e.type_name.empty() || e.kind == Io_kind::boolean || !array_type.is_invalid());
    if (already_typed) {
      inject[i].inject = false;
      if (!e.type_name.empty()) {
        suffix.push_back(e.type_name);
      } else if (e.kind == Io_kind::boolean) {
        suffix.emplace_back("Bool");
      } else if (!array_type.is_invalid()) {
        if (const auto shape = array_port_shape(callee, e, false, gbinds, param_set[i] ? &param_val[i] : nullptr);
            shape && shape->infer_lanes) {
          inject[i].array_size = shape->lanes;
          suffix.push_back("a" + std::to_string(shape->lanes));
        }
        // The element's width; fixed/generic dims keep their existing tokens.
        auto elem = array_type;
        while (Lnast_ntype::is_comp_type_array(callee->get_type(elem)) && !callee->get_first_child(elem).is_invalid()) {
          elem = callee->get_first_child(elem);
        }
        std::optional<Dlop> emax;
        std::optional<Dlop> emin;
        if (const auto mx = callee->get_first_child(elem);
            Lnast_ntype::is_prim_type_int(callee->get_type(elem)) && !mx.is_invalid()) {
          if (auto v = Dlop::from_pyrope(callee->get_name(mx)); v->is_integer()) {
            emax = *v;
          }
          if (const auto mn = callee->get_sibling_next(mx); !mn.is_invalid()) {
            if (auto v = Dlop::from_pyrope(callee->get_name(mn)); v->is_integer()) {
              emin = *v;
            }
          }
        }
        suffix.push_back(suffix_for(emax, emin));
      } else {
        suffix.push_back((e.is_signed ? "S" : "U") + std::to_string(e.bits));
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
      if (o.has_deferred_bound()) {
        out_inject[i] = deferred_port_type(callee, o, gbinds, callee_name, call_span);
        continue;
      }
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
    // A lambda bind names the CALLER's function: two call sites binding
    // `m<inc>` to different `inc` bodies (each file's own) must not share one
    // clone, so key it by the same caller-portable spelling the clone writes
    // into the body (clone_template_specialized).
    const std::string tok = !it->second.const_text.empty() ? it->second.const_text
                            : it->second.func_name.empty()
                                ? std::string{}
                                : frame_portable_func_name(it->second.func_name, callee->get_top_module_name());
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
  // Width tokens (`U<N>`/`S<N>`/`Bool`, the canonical type spelling) contain no `_`, so the `_`-joined readable
  // form is unambiguous for them. A NAMED-type component may contain `_` (or
  // even look like a width token), which could map two DISTINCT signatures of
  // the same template onto one name — and the name-keyed dedup would then
  // silently drop the second clone, mis-wiring that call site. So when any
  // component is not a plain width token, append a deterministic hash of the
  // exact component list (FNV-1a, NUL-free separator) to disambiguate; the
  // common all-primitive case keeps its clean `foo__U8` name.
  auto is_width_token = [](const std::string& s) -> bool {
    if (s == "Bool") {
      return true;
    }
    if (s.size() < 2 || (s[0] != 'U' && s[0] != 'S')) {
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
  // IDENTITY specialization — the clone IS the template. Three conditions, all
  // computed above: no port type is injected (every declared port was already
  // concrete, so no type or array extent is injected), there is no
  // var-arg expansion, and every declared generic took its DECLARATION DEFAULT
  // (`from_default`, set in resolve_generic_binds::apply_defaults) rather than
  // an explicit `<…>` bind or an inferred one. The clone then differs from the
  // template only by having the generics substituted at their defaults and the
  // template flag cleared — exactly the tree specialize_top_defaults builds for
  // a template TOP, which already keeps the template's own name. So name it
  // after the template too: the mangled name buys nothing here and costs the
  // module name that every downstream by-name consumer needs (lec pairing,
  // netlist review, the emitted Verilog module name).
  //
  // A call that binds anything else keeps the mangled name, so the two can
  // never merge: the base name has no `__` suffix and every mangled one does.
  // The template itself mints no GraphIO (upass_tolg::register_io skips it),
  // and the runner's own callee registry keeps the FIRST body under a name
  // (uPass_function_registry::ensure), so the template — not this clone — stays
  // the resolution target for any LATER call site that binds non-defaults.
  const bool identity_spec
      = !has_vararg && vports.empty()
        && std::none_of(inject.begin(), inject.end(), [](const Spec_port& p) { return p.inject || p.array_size > 0; })
        && std::none_of(out_inject.begin(), out_inject.end(), [](const Spec_port& p) { return p.inject; })
        && std::all_of(gens.begin(), gens.end(), [&](const std::string& g) {
             const auto it = gbinds.find(g);
             return it != gbinds.end() && it->second.from_default;
           });

  std::string mangled = std::string(callee->get_top_module_name());
  if (!identity_spec) {
    mangled += "__";
    for (std::size_t i = 0; i < suffix.size(); ++i) {
      if (i) {
        mangled += "_";
      }
      mangled += suffix[i];
    }
    if (ambiguous) {
      namespace hu = livehd::hash_util;
      uint64_t h   = hu::kFnv1a64_offset;
      for (const auto& s : suffix) {
        h ^= 0x01;  // component separator (never in an identifier/width token)
        h *= hu::kFnv1a64_prime;
        h  = hu::fnv1a64(s, h);
      }
      mangled += std::format("_h{:08x}", static_cast<uint32_t>(h & 0xffffffffu));
    }
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
    if (!param_set[i]) {
      continue;  // omitted: tolg drives its declared default or auto-wires it (sub_input_may_be_omitted)
    }
    const auto binding = gbinds.find(io.inputs[i].type_name);
    if (binding == gbinds.end() || binding->second.tuple_fields.empty()) {
      actuals.emplace_back(io.inputs[i].name, param_val[i]);
      continue;
    }
    const std::string                               source{param_val[i].get_name()};
    std::vector<std::pair<std::string, Lnast_node>> values;
    detuple_flatten_tuple_value(source, "", values);
    for (const auto& field : binding->second.tuple_fields) {
      const auto value = std::find_if(values.begin(), values.end(), [&](const auto& item) { return item.first == field.name; });
      if (value != values.end()) {
        actuals.emplace_back(io.inputs[i].name + "." + field.name, value->second);
      } else if (auto ref = try_tuple_slot_ref(source, field.name)) {
        actuals.emplace_back(io.inputs[i].name + "." + field.name, Lnast_node::create_ref(*ref));
      } else if (auto constant = try_fold_ref(source + "." + field.name); constant && !constant->is_invalid()) {
        actuals.emplace_back(io.inputs[i].name + "." + field.name, Lnast_node::create_const(constant->to_pyrope()));
      } else if (const auto split = detuple_splits_.find(source);
                 split != detuple_splits_.end()
                 && std::any_of(split->second.fields.begin(), split->second.fields.end(), [&](const auto& leaf) {
                      return leaf.name == field.name;
                    })) {
        actuals.emplace_back(io.inputs[i].name + "." + field.name, Lnast_node::create_ref(source + "." + field.name));
      } else {
        fcall_arg_fail(call_span,
                       "fcall-tuple-field",
                       std::format("tuple argument `{}` has no field `{}`", source, field.name),
                       "provide every field of the bound tuple type");
      }
    }
  }
  std::size_t vk = 0;
  for (const auto& a : vararg_pos) {
    actuals.emplace_back(std::format("{}__{}", vname, vk++), a);
  }
  for (const auto& [key, a] : vararg_named) {
    actuals.emplace_back(std::format("{}__{}", vname, key), a);
  }
  // The specialized clone is instantiated as a Sub, so the handle's fields are
  // its declared output ports. Same reason as the declines in try_inline_func_call:
  // the runner's declared-type consumers run before tolg rebuilds the Sub.
  stash_sub_instance_port_facts(dst_name, callee, &out_inject);
  for (const auto& output : io.outputs) {
    const auto binding = gbinds.find(output.type_name);
    if (binding == gbinds.end()) {
      continue;
    }
    for (const auto& field : binding->second.tuple_fields) {
      Symbol_table::Pending_decl pd;
      pd.kind        = field.type.kind;
      pd.decl_max    = field.type.max;
      pd.decl_min    = field.type.min;
      const auto key = dst_name + "." + output.name + "." + field.name;
      symbol_table_.pending_decl_facts.insert_or_assign(key, pd);
      symbol_table_.pending_keys_by_root[dst_name].push_back(key);
    }
  }
  {
    // See in_identity_respecialize_: only the identity clone reuses the
    // template's name, so only its re-walk needs the guard.
    const bool saved          = in_identity_respecialize_;
    in_identity_respecialize_ = identity_spec;
    emit_named_instance_call(dst_name, mangled, /*inst_name=*/"", actuals);
    in_identity_respecialize_ = saved;
  }
  return true;
}

// ── overload-gathering call dispatch (2f-overload) ────────────────────────────

std::pair<std::string, std::string> uPass_runner::source_var_at_cursor() const {
  // The raw name, never the inline frame's `inl<N>_x`: an inlined callee body
  // is read in place, so its raw text is the callee's own spelling.
  const auto raw = lm->current_raw_text();
  if (!Lnast_ntype::is_ref(lm->get_raw_ntype()) || Lnast::is_tmp(raw)) {
    return {};
  }
  return {std::string(raw), minted_ssa_base(*lm->get_lnast(), raw)};
}

bool uPass_runner::gather_actuals(bool drop_ufcs_receiver, std::vector<Actual>& actuals,
                                  std::vector<Generic_actual>& explicit_generics) {
  // Cursor MUST be on the callee ref (the func_call's 2nd child); the actuals
  // are its following siblings. The cursor is saved/restored here so this can
  // be called twice (overload probe + real bind) without disturbing the caller.
  const auto entry = lm->save_cursor();
  gathered_synth_call_.clear();
  gathered_inst_name_.clear();  // reset; set below if `__inst_name` is present

  // A ref actual whose raw name is itself a registry function is a higher-order
  // / closure argument: capture the function name so the body's `f(x)` can
  // resolve to it (see func_param_bindings_). A ref to THIS frame's own
  // function-valued param (`apply(f=f)` inside `outer(f)`) forwards the
  // function it is bound to; it shadows a same-named registry function.
  auto func_actual_name = [&]() -> std::string {
    if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
      return {};
    }
    if (const auto fb = func_param_bindings_.find(lm->current_text()); fb != func_param_bindings_.end()) {
      return fb->second;
    }
    if (lookup_callee(lm->current_raw_text()) != nullptr) {
      // A value binding (import alias) of the spelling shadows the by-name hit:
      // capture the alias target (see bind_of_explicit_arg).
      if (auto aliased = value_bound_func_name(lm->current_text(), lm->current_raw_text()); !aliased.empty()) {
        return aliased;
      }
      return std::string(lm->current_raw_text());
    }
    return {};
  };
  bool shape_ok = true;
  while (lm->move_to_sibling()) {
    const auto t = lm->get_raw_ntype();
    if (Lnast_ntype::is_ref(t) || Lnast_ntype::is_const(t)) {
      auto [src_name, src_base] = source_var_at_cursor();
      actuals.push_back(Actual{.is_named  = false,
                               .key       = {},
                               .node      = lm->current_node(),
                               .func_name = func_actual_name(),
                               .src_name  = std::move(src_name),
                               .src_base  = std::move(src_base)});
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
      // A backtick-escaped key (`` `port.leaf` `` — prp2lnast keeps the escape
      // when the identifier carries a dot, e.g. a flattened tuple-port LEAF the
      // prp_writer had to quote at a re-emitted call site) must match the
      // callee's BARE dotted io name. Strip the escape unless the content
      // genuinely needs it (whitespace) — the same rule as tolg's
      // canon_io_name, so the runner and tolg agree on the port name.
      if (a.key.size() >= 2 && a.key.front() == '`' && a.key.back() == '`') {
        const std::string_view inner = std::string_view(a.key).substr(1, a.key.size() - 2);
        const bool has_ws = std::any_of(inner.begin(), inner.end(), [](unsigned char c) { return std::isspace(c) != 0; });
        if (!has_ws) {
          a.key = std::string(inner);
        }
      }
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
      // Loop-iteration tag stamped by a previous unroll (emit_op_with_fold).
      // Consumed like `__inst_name` — it is a naming marker, never an actual —
      // and re-added by the emitter, so it need not be carried here.
      if (a.key == "__synth_call") {
        if (lm->move_to_sibling()) {
          gathered_synth_call_ = std::string(lm->current_raw_text());
        }
        lm->restore_cursor(here);
        continue;
      }
      if (a.key == call_inst_suffix_marker) {
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
        ga.value                           = std::string(lm->current_text());
        std::tie(ga.src_name, ga.src_base) = source_var_at_cursor();
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
      if (a.is_ref_pass) {
        // `f(ref x)`: the variable's source spelling drives name-match binding.
        std::tie(a.src_name, a.src_base) = source_var_at_cursor();
      }
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

bool uPass_runner::signature_matches(const Lnast_tree_io& io, const std::vector<Actual>& actuals,
                                     const std::shared_ptr<Lnast>& callee) {
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
  if (!bind_call_actuals(io,
                         actuals,
                         /*commit=*/false,
                         /*callee_name=*/{},
                         livehd::diag::Span{},
                         param_val,
                         param_set,
                         param_func,
                         vararg_pos,
                         vararg_named)) {
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

  // Per-arg scalar kind/range fit. Re-derived here so dispatch can
  // distinguish e.g. `f(a:bool)` from `f(a:u8)`, or `f(a:u8)` from `f(a:u16)`:
  // a candidate whose declared parameter range the actual MAY exceed cannot
  // take the call (the same containment rule check_call_args_fit enforces on
  // the chosen callee), so a runtime u16 skips `f(a:u8)` for `f(a:u16)`.
  // Skips self, the var-arg slot, and unset params; an untyped param (or a
  // generic-width one, whose bound needs the call's binds) is permissive, as
  // is an actual that is not an integer (the kind check above decides it). A
  // typed-self structural `does` is NOT checked here (method overloads fall
  // through to check_self_does on the chosen candidate).
  for (std::size_t i = 0; i < nparams; ++i) {
    if ((has_vararg && i == nbind) || (has_self && i == 0) || !param_set[i]) {
      continue;
    }
    const auto& pe = io.inputs[i];
    if (callee && is_array_port(*callee, pe, false)) {
      if (!check_call_array_arg(callee, pe, {}, param_val[i], {}, {}, true)) {
        return false;
      }
      continue;
    }
    const auto ak = classify(param_val[i]);
    if (pe.kind != Io_kind::none && ak != Io_kind::none && pe.kind != ak) {
      return false;  // kind mismatch (bool vs int vs string)
    }
    if (pe.kind != Io_kind::integer) {
      continue;
    }
    if (const auto declared = declared_param_range(nullptr, pe, {});
        declared && value_range_of(param_val[i]).may_exceed(declared->first, declared->second)) {
      return false;  // the argument may not fit this overload's declared range
    }
  }

  return true;
}

bool uPass_runner::collect_return_consumption(const upass::Lnast_manager::Cursor_state& fcall_cursor, std::string_view dst_name,
                                              absl::flat_hash_set<std::string>& req_fields, bool& whole_used,
                                              bool* scalar_destination, std::string_view stop_field) {
  // The destructure `(p1,p2) = f()` lowers to `fcall(dst, …)` followed by
  // sibling `tuple_get(tmp, dst, 'p1')` / `tuple_get(tmp, dst, 'p2')` picks
  // (see the parse dump); a whole bind `c = f()` lowers to `store(c, dst)` and
  // an operand use `c = f()+1` to `plus(c, dst, 1)`. So learn the result shape
  // by walking the fcall's following statements once: a tuple_get whose SRC
  // (2nd child) is our dst contributes a required field; any other node that
  // names dst as a child marks a whole-value use.
  //
  // The walk must DESCEND into nested statement blocks (if/match arms, loop and
  // tick bodies), not just the fcall's straight-line siblings: a call hoisted to
  // the top of a module whose result is only read inside a branch
  // (`u = f(); if c { r = u.out.f }` — the shape every Verilog-imported instance
  // has) would otherwise look unconsumed, and the epilogue would drop the lone
  // TUPLE output's NAME (`u = (f=…)` instead of `u = (out=(f=…))`), making the
  // branch read fail with `unknown field out`.
  // This query is on the immutable source tree. Walking it through Lnast_manager
  // used to save/restore the cursor several times per visited node. Large
  // generated modules have hundreds of calls, so scanning each call's suffix
  // that way made this pass both quadratic and needlessly expensive inside the
  // quadratic term. Use the tree's node API directly; all references in this
  // source frame receive the same inline renaming, so raw-name equality is the
  // same relation as current_text() equality.
  const auto&                      ln        = *lm->get_lnast();
  const auto                       fcall_dst = ln.get_first_child(fcall_cursor.current);
  absl::flat_hash_set<std::string> result_aliases{fcall_dst.is_invalid() ? std::string(dst_name)
                                                                         : std::string(ln.get_name(fcall_dst))};
  bool                             target_found = false;

  // One statement. `self` recurses into the statement's nested blocks.
  const auto scan_stmt = [&](const auto& self, Lnast_nid node) -> bool {
    const auto raw     = ln.get_type(node);
    const bool is_tget = Lnast_ntype::is_tuple_get(raw);
    const auto first   = ln.get_first_child(node);
    if (first.is_invalid()) {
      return false;
    }
    const auto        second      = ln.get_sibling_next(first);
    const bool        is_alias    = Lnast_ntype::is_store(raw) && !second.is_invalid() && ln.get_sibling_next(second).is_invalid();
    // children in order: [dst, src/operand, field/operand, …]
    std::size_t       idx         = 0;
    bool              names_dst   = false;  // dst_name appears as a non-leading child
    std::size_t       dst_at_idx  = std::string::npos;
    // Only an alias store can extend the set, so only it needs the dst COPIED
    // (the walk has moved on by the time it is inserted below).
    const std::string destination = is_alias ? std::string(ln.get_name(first)) : std::string{};
    for (auto child = first; !child.is_invalid(); child = ln.get_sibling_next(child)) {
      // Heterogeneous lookup: no std::string per child, per sibling. Raw names
      // are intentional; every ref in this source frame has the same tag/salt.
      if (idx > 0 && Lnast_ntype::is_ref(ln.get_type(child)) && result_aliases.contains(ln.get_name(child))) {
        names_dst  = true;
        dst_at_idx = idx;
      }
      ++idx;
    }
    if (is_tget && dst_at_idx == 1) {
      // tuple_get(tmp, dst, field): read the field const (3rd child).
      const auto field = second.is_invalid() ? second : ln.get_sibling_next(second);
      if (!field.is_invalid() && Lnast_ntype::is_const(ln.get_type(field))) {
        if (auto v = Dlop::from_pyrope(ln.get_name(field)); v && !v->is_invalid()) {
          auto picked = v->to_field();
          req_fields.insert(picked);
          if (!stop_field.empty() && (picked == stop_field || picked.starts_with(std::string(stop_field) + "."))) {
            target_found = true;
            return true;
          }
        }
      }
    } else if (names_dst) {
      whole_used = true;
      if (is_alias) {
        result_aliases.insert(destination);
        if (scalar_destination != nullptr) {
          // Declaration facts are keyed by the frame-renamed destination. User
          // names take the regular tag here; compiler temps are not meaningful
          // scalar destinations for overload selection.
          const auto fact_name = Lnast::is_tmp(destination) ? std::string{} : lm->frame_variable(destination);
          if (!fact_name.empty()) {
            if (const auto facts = upass::decl_facts::lookup(symbol_table_, &ln, fact_name);
                facts && facts->kind != upass::decl_facts::Num::none) {
              *scalar_destination = true;
            }
          }
        }
      }
    }

    // A func_def body is a separate name scope (its `dst_name` is a different
    // variable) — never descend into one.
    if (!Lnast_ntype::is_func_def(raw) && !Lnast_ntype::is_comp_type_lambda(raw)) {
      for (auto child = first; !child.is_invalid(); child = ln.get_sibling_next(child)) {
        const auto craw = ln.get_type(child);
        if (Lnast_ntype::is_stmts(craw)) {
          // Scoped block: if/match arm, for/while/tick body, rolled_for payload.
          for (auto stmt = ln.get_first_child(child); !stmt.is_invalid(); stmt = ln.get_sibling_next(stmt)) {
            if (self(self, stmt)) {
              return true;
            }
          }
        } else if (Lnast_ntype::is_if_like(raw) && !Lnast_ntype::is_ref(craw) && !Lnast_ntype::is_const(craw)
                   && !Lnast_ntype::is_type(craw)) {
          if (self(self, child)) {  // flat when/unless: body statements are direct children
            return true;
          }
        }
      }
    }
    return false;
  };

  for (auto stmt = ln.get_sibling_next(fcall_cursor.current); !stmt.is_invalid(); stmt = ln.get_sibling_next(stmt)) {
    if (scan_stmt(scan_stmt, stmt)) {
      break;
    }
  }
  return target_found;
}

bool uPass_runner::return_matches(const Lnast_tree_io& io, const absl::flat_hash_set<std::string>& req_fields, bool whole_used,
                                  bool scalar_destination) {
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
    // A lone output drops its name; multiple outputs bind as a named tuple.
    // An explicitly scalar destination still requires a scalar result.
    if (scalar_destination) {
      return n_logical == 1 && logical.begin()->second.empty();
    }
    return n_logical > 0;
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

std::string uPass_runner::select_init_overload(const std::vector<std::string>& candidates, const std::vector<Ctor_arg>& args,
                                               const livehd::diag::Span* span, std::string_view label) {
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
    actuals.push_back(
        Actual{.is_named = false, .is_ref_pass = false, .key = {}, .node = Lnast_node::create_const("0"), .func_name = {}});
    for (const auto& a : args) {
      actuals.push_back(Actual{.is_named    = !a.key.empty(),
                               .is_ref_pass = false,
                               .key         = a.key,
                               .node        = a.node,
                               .func_name   = {},
                               .src_name    = a.src_name,
                               .src_base    = a.src_base});
    }
    if (signature_matches(cio, actuals)) {
      return fn;
    }
  }
  // No overload accepts the arguments. With ONE `init` the call is held to the
  // ordinary naming rules (qa.md Q31: `Typ2("hello", 44)` into `init(ref self,
  // a, b)` must name its arguments): a committed bind reports the exact error,
  // and a bind that succeeds (the probe rejected on kind/range) keeps the
  // structural fallback.
  if (span != nullptr && candidates.size() == 1) {
    if (auto callee = lookup_callee(candidates.front());
        callee && !callee->io_meta().inputs.empty() && callee->io_meta().inputs[0].name == "self") {
      std::vector<Actual> actuals;
      actuals.push_back(
          Actual{.is_named = false, .is_ref_pass = false, .key = {}, .node = Lnast_node::create_const("0"), .func_name = {}});
      for (const auto& a : args) {
        actuals.push_back(Actual{.is_named    = !a.key.empty(),
                                 .is_ref_pass = false,
                                 .key         = a.key,
                                 .node        = a.node,
                                 .func_name   = {},
                                 .src_name    = a.src_name,
                                 .src_base    = a.src_base});
      }
      std::vector<Lnast_node>                         param_val;
      std::vector<bool>                               param_set;
      std::vector<std::string>                        param_func;
      std::vector<Lnast_node>                         vararg_pos;
      std::vector<std::pair<std::string, Lnast_node>> vararg_named;
      bind_call_actuals(callee->io_meta(),
                        actuals,
                        /*commit=*/true,
                        label,
                        *span,
                        param_val,
                        param_set,
                        param_func,
                        vararg_pos,
                        vararg_named);
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
  ctor_call_args_    = args;
  process_lnast();
  ctor_call_pending_ = false;
  ctor_call_args_.clear();
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
  const auto saved     = lm->save_cursor();
  const auto ctor_span = lm->current_span();
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
  if (!bundle_key::is_single_level(x) || prp_is_tmp_name(x)) {
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
    std::vector<Ctor_arg> whole{
        Ctor_arg{.key = {}, .node = Lnast_node::create_ref(v_text)}
    };
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

  const auto chosen = select_init_overload(candidates, args, &ctor_span, tn);
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
  const auto saved     = lm->save_cursor();
  const auto ctor_span = lm->current_span();
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
  // Collect args verbatim (refs/consts positional, store(key,val) named). A
  // positional bare variable keeps its source spelling: an explicit `T(...)`
  // call follows naming exception 2, so `T(c, b, a)` binds init's `a`/`b`/`c`
  // by name.
  std::vector<Ctor_arg> args;
  bool                  shape_ok = true;
  while (lm->move_to_sibling()) {
    const auto t = lm->get_raw_ntype();
    if (Lnast_ntype::is_ref(t) || Lnast_ntype::is_const(t)) {
      auto [src_name, src_base] = source_var_at_cursor();
      args.push_back(
          Ctor_arg{.key = {}, .node = lm->current_node(), .src_name = std::move(src_name), .src_base = std::move(src_base)});
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
  const auto chosen = select_init_overload(candidates, args, &ctor_span, tn);
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
  // A position the compiler picked (a loop's `t[k]`, an `in` operand), never a
  // user index: an index-range array is not rebased.
  const bool outer_physical = std::exchange(physical_indices_, true);
  lm->push_source(s, "", 0);
  process_lnast();  // tuple_get dispatch → try_resolve_tuple_get (runtime) or constprop fold (comptime)
  flush_deferred_emits();
  lm->pop_source();
  physical_indices_ = outer_physical;
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
  const bool outer_physical = std::exchange(physical_indices_, true);  // see emit_inline_tuple_pick
  lm->push_source(s, "", 0);
  process_lnast();  // store dispatch → constprop tuple_set / write-back into dst's slot
  flush_deferred_emits();
  lm->pop_source();
  physical_indices_ = outer_physical;
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

void uPass_runner::emit_inline_declare_array(const std::string& name, const Array_port_shape& shape) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("inl-decl");
  auto s    = std::make_shared<Lnast>(body, "inl-decl");
  auto root = s->set_root(Lnast_ntype::create_declare());
  stamp_scratch_srcid(s, root);
  s->add_child(root, Lnast_node::create_ref(name));
  // comp_type_array(... elem ..., [dim]) nests outer dim first, like
  // prp2lnast's `[2][4]u8`: the outer node's first child is the inner array.
  std::vector<int64_t> dims{shape.lanes};
  dims.insert(dims.end(), shape.inner_dims.begin(), shape.inner_dims.end());
  std::vector<Lnast_nid> levels;
  auto                   parent = root;
  for (size_t l = 0; l < dims.size(); ++l) {
    parent = s->add_child(parent, Lnast_ntype::create_comp_type_array());
    levels.push_back(parent);
  }
  if (shape.elem_bool) {
    s->add_child(parent, Lnast_ntype::create_prim_type_bool());
  } else {
    auto pt = s->add_child(parent, Lnast_ntype::create_prim_type_int());
    s->add_child(pt, Lnast_node::create_const(std::string(shape.elem_max.to_pyrope())));
    s->add_child(pt, Lnast_node::create_const(std::string(shape.elem_min.to_pyrope())));
  }
  for (size_t l = dims.size(); l-- > 0;) {
    s->add_child(levels[l], Lnast_node::create_const(std::format("[{}]", dims[l])));
  }
  s->add_child(root, Lnast_node::create_const("mut"));
  flush_deferred_emits();
  lm->push_source(s, "", 0);
  // A scalar-element array, never a tuple to split: the detupler must not park
  // this declare for a replay (the scratch tree is gone by then).
  detuple_synthetic_ = true;
  process_lnast();  // declare dispatch → bakes the array shape (bake_decl_pre_step) and emits it for tolg
  detuple_synthetic_ = false;
  flush_deferred_emits();
  lm->pop_source();
}

std::string uPass_runner::loop_inst_suffix() const {
  std::string s;
  for (const auto ordinal : loop_iter_ordinals_) {
    s += std::format("__li{}", ordinal);
  }
  return s;
}

bool uPass_runner::walk_loop_iteration(const std::function<void()>& emit_binds, const std::function<void()>& emit_post) {
  // Precondition: the read cursor is on the loop body `stmts` node.
  if (inline_budget_ == 0 || loop_depth_ > static_cast<int>(kInlineMaxDepth)) {
    return false;  // fuel / depth guard — a non-terminating comptime loop bails (like recursion)
  }
  --inline_budget_;
  const uint32_t iter_salt  = ++inline_seq_;
  const auto     baked_mark = loop_baked_refs_.size();

  flush_deferred_emits();                            // flush outer parked writes before the iteration frame
  lm->push_iteration(iter_salt);                     // fresh salt → fresh tmp namespace + block-scope id; cursor + tag kept
  enter_block_scope();                               // Runner-owned block scope (scope_uid uses iter_salt)
  dispatch_to_passes(&upass::uPass::process_stmts);  // passes seed per-block state
  emit_push(Lnast_ntype::create_stmts());            // staging block for this iteration

  // Bind the iteration variable(s) into this scope so the body's reads fold.
  emit_binds();

  loop_continue_hit_              = false;  // fresh per iteration (a `continue` skips only this one)
  // Uncertainty the BODY introduces is what makes a `break` data dependent;
  // uncertainty the loop is merely nested inside does not. Snapshot the base so
  // the break check below compares against scopes entered since this point.
  const auto saved_uncertain_base = loop_uncertain_base_;
  loop_uncertain_base_            = symbol_table_.uncertain_scope_count();
  if (lm->move_to_child()) {
    do {
      process_lnast();
      if (loop_break_hit_ || loop_continue_hit_) {
        break;  // a comptime `break`/`continue` fired — skip the rest of this iteration's body
      }
    } while (lm->move_to_sibling());
    lm->move_to_parent();
  }

  loop_uncertain_base_ = saved_uncertain_base;

  if (emit_post) {
    emit_post();  // `for i in ref d` write-back: still inside this iteration's scope
  }

  dispatch_to_passes(&upass::uPass::process_stmts_pre_pop);  // coalescer flushes this iteration's writes
  emit_pop();
  dispatch_to_passes(&upass::uPass::process_stmts_post);
  symbol_table_.leave_scope();  // Pop after stmts_post (see process_stmts)
  lm->pop_source();             // restore cursor (body stmts), outer salt/stack/tag
  // The iteration is emitted: un-fold the bounds it baked into the shared body
  // so the next iteration bakes its own (see loop_baked_refs_).
  while (loop_baked_refs_.size() > baked_mark) {
    const auto& b = loop_baked_refs_.back();
    b.ln->set_name(b.nid, b.ref);
    b.ln->set_type(b.nid, Lnast_ntype::create_ref());
    loop_baked_refs_.pop_back();
  }
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
    const int64_t lo   = std::get<0>(*rng).to_just_i64();
    const int64_t hi   = std::get<1>(*rng).to_just_i64();  // inclusive
    int64_t       step = std::get<2>(*rng).is_just_i64() ? std::get<2>(*rng).to_just_i64() : 1;
    if (step < 1) {
      step = 1;  // non-positive step is a comptime error (process_func_call); avoid a hang here
    }
    // ROLL: keep the loop as one replicated instance instead of emitting one
    // body copy per iteration (compile.unroll). Declines back to unrolling
    // whenever the body is not eligible, so this is purely additive.
    // Runtime loop control cannot be represented by the ordinary source
    // unroller: later iterations need the activation recurrence. Route any
    // control-bearing body through the rolled form even while general rolling
    // remains opt-in. Comptime-only breaks are folded inside the lifted body;
    // a later eligibility refinement may keep those on the source-unroll path
    // as a compactness choice, but both representations are semantic.
    to_body();
    const auto body_nid = lm->get_current_nid();
    lm->restore_cursor(for_bm);
    if (!unroll_requested_ || subtree_has_runtime_loop_control(*lm->get_lnast(), body_nid, tagged_i)) {
      Loop_roll_plan plan;
      lm->move_to_nid(lm->get_lnast()->get_first_child(lm->get_lnast()->get_parent(body_nid)));
      const std::string body_index(lm->current_raw_text());
      lm->restore_cursor(for_bm);
      if (plan_loop_roll(body_nid, body_index, lo, hi, step, plan)) {
        plan.inst    = std::format("u_loop_{}", roll_seq_);
        plan.mangled = std::format("{}.__loop{}", lm->outlining_owner(), roll_seq_);
        ++roll_seq_;
        if (specialized_emitted_.insert(plan.mangled).second) {
          new_lnasts.push_back(lift_loop_body(body_nid, plan));
        }
        emit_rolled_loop_call(plan, body_nid);
        const size_t depth = loop_iter_ordinals_.size();
        if (next_loop_ordinal_bases_.size() <= depth) {
          next_loop_ordinal_bases_.resize(depth + 1, 0);
        }
        auto& base = next_loop_ordinal_bases_[depth];
        base = plan.count > std::numeric_limits<uint64_t>::max() - base ? std::numeric_limits<uint64_t>::max() : base + plan.count;
        lm->restore_cursor(for_bm);
        return;
      }
    }

    const bool saved_break = loop_break_hit_;
    loop_break_hit_        = false;
    Unroll_scope unroll(*this);  // ++loop_depth_ + iteration-ordinal level, both undone on return
    for (int64_t v = lo; v <= hi; v += step) {
      to_body();
      if (!walk_loop_iteration([&]() { emit_inline_binding(tagged_i, Lnast_node::create_const(std::to_string(v))); })) {
        break;  // fuel exhausted
      }
      unroll.complete_iteration();
      if (loop_break_hit_) {
        break;
      }
      unroll.next_iteration();
    }
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
  std::optional<std::vector<std::pair<std::string, bool>>> shape      = try_tuple_shape(iterable);
  // A specialized array input has a concrete extent in io_meta, but no
  // constprop tuple: its elements are slices of the packed runtime port.
  const auto*                                              array_port = lm->get_lnast()->io_meta().find(iterable);
  if (!shape && array_port && array_port->array_size > 0) {
    shape.emplace();
    for (int64_t i = 0; i < array_port->array_size; ++i) {
      shape->emplace_back(std::to_string(i), true);
    }
  }
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
    Unroll_scope unroll(*this);  // ++loop_depth_ + iteration-ordinal level, both undone on return
    int64_t      pos = 0;
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
      if (!elem_dt && array_port && array_port->array_size > 0 && !array_port->elem_bool) {
        upass::uPass::Decl_scalar_type dt;
        dt.range_max = upass::max_from_bits(array_port->elem_bits, array_port->elem_signed);
        dt.range_min = upass::min_from_bits(array_port->elem_bits, array_port->elem_signed);
        elem_dt      = dt;
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
      unroll.complete_iteration();
      if (loop_break_hit_) {
        break;
      }
      unroll.next_iteration();
      ++pos;
    }
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
    const auto  cnid = ln_.get_first_child(lm->get_current_nid());             // condition
    const auto  bnid = cnid.is_invalid() ? cnid : ln_.get_sibling_next(cnid);  // body stmts
    collect_body_assigned_vars(ln_, bnid, cond_var_set);
  }
  const std::vector<std::string>   loop_vars(cond_var_set.begin(), cond_var_set.end());
  absl::flat_hash_set<std::string> seen_states;

  // Span for any loop diagnostic below — the `while` node carries the SourceId.
  auto while_span = [&]() { return lm->current_span(); };

  const bool saved_break = loop_break_hit_;
  loop_break_hit_        = false;
  Unroll_scope          unroll(*this);  // ++loop_depth_ + iteration-ordinal level, undone on return AND on loop_fail's throw
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
      loop_break_hit_ = saved_break;
      loop_fail(while_span(),
                "type",
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
        loop_break_hit_ = saved_break;
        lm->restore_cursor(while_bm);  // try_fold_ref above may have moved the cursor; reset for the span
        loop_fail(while_span(),
                  "type",
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
      loop_break_hit_ = saved_break;
      lm->restore_cursor(while_bm);
      loop_fail(while_span(),
                "type",
                "loop-unbounded",
                "comptime loop did not terminate within the unroll budget (it may never converge)",
                "ensure the loop is comptime-bounded and its exit condition is eventually reached");
    }
    unroll.complete_iteration();
    if (loop_break_hit_) {
      break;
    }
    unroll.next_iteration();
  }
  loop_break_hit_    = saved_break;
  loop_continue_hit_ = false;    // a `continue` never escapes the loop
  lm->restore_cursor(while_bm);  // leave cursor on the while-node
}

// ── Top-level run loop ────────────────────────────────────────────────────────

void uPass_runner::run() {
  if (configuration_error) {
    livehd::diag::err("pass.upass", "bad-configuration", "io")
        .msg("uPass invalid configuration: {}", configuration_error_msg)
        .emit();
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
  in_place_fold_cache_.clear();
  any_index_mapped_   = false;
  index_dim_reported_ = false;
  physical_indices_   = false;
  open_fill_arrays_.clear();
  split_tuple_leaves_.clear();
  symbol_table_.tget_origin.clear();
  symbol_table_.origin_poisoned.clear();
  symbol_table_.typed_fields.clear();
  symbol_table_.sub_output_ranges.clear();
  symbol_table_.single_output_port.clear();
  symbol_table_.call_result_label.clear();
  symbol_table_.call_result_callee.clear();
  symbol_table_.unchecked_typed.clear();
  symbol_table_.wide_values.clear();
  symbol_table_.instance_handles.clear();
  symbol_table_.opaque_sub_outputs.clear();
  symbol_table_.sub_output_names.clear();
  symbol_table_.nil_seeded.clear();
  symbol_table_.uninitialized.clear();
  symbol_table_.field_touched.clear();
  initialize_stream_port_abi();
  stream_ssa_enabled_ = root_lnast_->needs_stream_ssa();
  stream_ssa_active_def_.reset();
  stream_ssa_state_names_.clear();
  stream_ssa_current_.clear();
  stream_ssa_count_.clear();
  stream_ssa_defs_.clear();
  detuple_pending_decl_.reset();
  detuple_tuple_values_.clear();
  detuple_shape_fields_.clear();
  detuple_named_layout_.clear();
  detuple_field_alias_.clear();
  detuple_predecl_fields_.clear();
  detuple_splits_.clear();
  detuple_index_aliases_.clear();
  detuple_replay_    = false;
  detuple_synthetic_ = false;
  inline_tags_unit_  = nullptr;  // rescan: a new tree may reuse a freed address
  if (stream_ssa_enabled_) {
    for (const auto& nid : root_lnast_->depth_preorder(root_lnast_->get_root())) {
      if (nid.is_invalid() || !Lnast_ntype::is_declare(root_lnast_->get_type(nid))) {
        continue;
      }
      const auto name = root_lnast_->get_first_child(nid);
      const auto type = name.is_invalid() ? name : root_lnast_->get_sibling_next(name);
      const auto mode = type.is_invalid() ? type : root_lnast_->get_sibling_next(type);
      if (name.is_invalid() || mode.is_invalid() || !Lnast_ntype::is_ref(root_lnast_->get_type(name))
          || !Lnast_ntype::is_const(root_lnast_->get_type(mode))) {
        continue;
      }
      const auto mode_text = root_lnast_->get_name(mode);
      if (mode_text == "reg" || mode_text.starts_with("reg ") || mode_text == "wire" || mode_text.starts_with("wire ")) {
        stream_ssa_state_names_.emplace(root_lnast_->get_name(name));
      }
    }
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
  detuple_flush_pending_decl();
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
    std::print(stderr,
               "uPass stats [{}]: walk {:.1f}s, dispatched {:.1f}s ({:.0f}%), runner-core {:.1f}s\n",
               lm->get_top_module_name(),
               walk_s,
               dispatched_s,
               walk_s > 0 ? 100.0 * dispatched_s / walk_s : 0.0,
               walk_s - dispatched_s);
    std::vector<const Pass_entry*> sorted;
    sorted.reserve(upasses.size());
    for (const auto& entry : upasses) {
      sorted.push_back(&entry);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Pass_entry* a, const Pass_entry* b) { return a->stat_ns > b->stat_ns; });
    for (const auto* entry : sorted) {
      std::print(stderr,
                 "uPass stats [{}]:   {:<12} {:9.1f}s  ({:4.1f}% of walk, {} dispatches)\n",
                 lm->get_top_module_name(),
                 entry->name,
                 static_cast<double>(entry->stat_ns) / 1e9,
                 walk_s > 0 ? 100.0 * (static_cast<double>(entry->stat_ns) / 1e9) / walk_s : 0.0,
                 entry->stat_calls);
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
      std::print(stderr,
                 "uPass stats [{}]:   staging-DCE {:9.1f}s\n",
                 lm->get_top_module_name(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dce_t0).count() / 1e3);
    }
  }

  // Step J — dest-walk finisher dispatch. Passes that inspect or finish the
  // freshly-built staging tree override walk_dest (default no-op): bitwidth
  // writes the shape of each array its uses inferred (`mut a:[] = 0`) into
  // the staged declaration.
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
    case N::Lnast_ntype_concat:
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
  if (staging.get_name(key) == "__synth_scope" || staging.get_name(key).starts_with("synth.") || staging.get_name(key) == "__region"
      || staging.get_name(key) == "__region_ware" || staging.get_name(key) == "__region_delay") {
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
  // Every materialized body, including a fully typed comb, can reach tolg.
  // Remove dead comptime scaffolding from its staged copy: enum metadata and
  // range-value calls have no runtime representation after their uses fold.
  // The registry retains the original tree for later inlining; DCE here only
  // changes staging. Unmaterialized templates never reach this call.
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
  // Pyrope provenance emission is a source-preserving view.  In particular,
  // a module-local parameter must survive even when its only dataflow user is
  // otherwise dead; the writer needs that named definition to reproduce the
  // symbolic `const LOCAL = pkg.PARAM + ...` declaration.  This mode is never
  // used by the graph-producing compile path, so retaining its named source
  // definitions has no synthesis cost.
  const bool  allow_named_drop = is_function_body_ && io_known && !preserve_param_provenance_;

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
  for (const auto& name : default_value_names_) {
    protected_names.insert(name);  // read only by a later inliner (todo 3g E)
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
    // `wire` declares on the spine, and every def-producing write of each name
    // (spine or payload). A wire is a combinational net, not a state root: once
    // its readers are gone its driver is swept like any other dead def, and the
    // declaration must go WITH it — a surviving `declare x … wire` with no
    // driver reads as an UNDRIVEN wire to lnast.tolg ("wire 'x' is never
    // driven"), e.g. a wire whose only reader was an argument the inlined
    // callee ignores.
    absl::flat_hash_map<std::string_view, std::vector<int64_t>> wire_decls;
    absl::flat_hash_map<std::string_view, int>                  write_count;

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
          // A declaration names its target; it does not consume the value.
          const bool is_lhs
              = (dce_is_def_producing(nt) || nt == N::Lnast_ntype_declare || nt == N::Lnast_ntype_type_spec) && c.is_first_child();
          if (!is_lhs) {
            const auto nm = staging->get_name(c);
            ++use_count[nm];
            if (active != 0) {
              stmt_reads[active].push_back(nm);
            }
          } else if (dce_is_def_producing(nt)) {
            ++write_count[staging->get_name(c)];
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
                if (auto mode = staging->get_sibling_next(ty); mode.is_valid() && staging->get_type(mode) == N::Lnast_ntype_const) {
                  const auto m = staging->get_name(mode);
                  if (m == "mut" || m == "reg") {
                    protected_names.insert(staging->get_name(nm));
                  } else if (on_spine && (m == "wire" || m.starts_with("wire "))) {
                    wire_decls[staging->get_name(nm)].push_back(c.get_class_index().value);
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
                                      || ct == N::Lnast_ntype_while || ct == N::Lnast_ntype_for || ct == N::Lnast_ntype_tick;
        scan(c, next_active, on_spine && child_structural);
      }
    };
    scan(staging->get_root(), 0, true);

    const auto droppable
        = [&](std::string_view name) { return Lnast::is_tmp(name) || (allow_named_drop && !protected_names.contains(name)); };
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
    // A dead wire's declaration dies with its driver(s): only when nothing
    // reads it and EVERY write of the name was swept (a write kept as payload,
    // e.g. under a unique_if arm, keeps the declaration too).
    for (const auto& [name, ids] : wire_decls) {
      if (!droppable(name)) {
        continue;
      }
      if (const auto uit = use_count.find(name); uit != use_count.end() && uit->second != 0) {
        continue;
      }
      int dead_writes = 0;
      if (const auto dit = defs_of.find(name); dit != defs_of.end()) {
        for (const auto id : dit->second) {
          dead_writes += dead_stmts.contains(id) ? 1 : 0;
        }
      }
      // A wire that was NEVER written stays: its declaration is what lets
      // lnast.tolg report the undriven wire.
      const auto wit = write_count.find(name);
      if (dead_writes > 0 && wit != write_count.end() && dead_writes == wit->second) {
        dead_stmts.insert(ids.begin(), ids.end());
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
             || t == N::Lnast_ntype_for || t == N::Lnast_ntype_tick;
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
      std::print(stderr,
                 "uPass stats [{}]:   DCE mark {} dead stmts in {:.2f}s (no rebuild)\n",
                 lm->get_top_module_name(),
                 dead_stmts.size(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(dce_t_marked - dce_t_scan).count() / 1e3);
    }
    staging->set_dce_dead_stmts(std::move(dead_stmts));
    return;
  }

  // HHDS subtree deletion currently leaves a corrupt sibling chain for some
  // mixtures of nested and adjacent dead roots (Rob.Rob reliably exposes it
  // with thousands of roots). Build the clean final body until Tree provides a
  // stable bulk-prune operation. lg-only flows already avoid this copy through
  // dce:mark above, which is the common compile path.
  auto fresh_body  = dest_forest_->create_tree_temp(std::format("optimized-{}", lm->get_top_module_name()));
  auto new_staging = std::make_shared<Lnast>(fresh_body, lm->get_top_module_name());

  auto src_root = staging->get_root();
  auto dst_root = new_staging->set_root(staging->get_type(src_root));
  if (const auto id = staging->get_srcid(src_root); id != hhds::SourceId_invalid) {
    new_staging->set_srcid(dst_root, id);
  }

  auto is_structural = [](Lnast_ntype::Lnast_ntype_int t) {
    return t == N::Lnast_ntype_top || t == N::Lnast_ntype_stmts || t == N::Lnast_ntype_if || t == N::Lnast_ntype_while
           || t == N::Lnast_ntype_for || t == N::Lnast_ntype_tick;
  };
  std::function<void(const Lnast_nid&, const Lnast_nid&, bool)> copy_live;
  copy_live = [&](const Lnast_nid& src, const Lnast_nid& dst, bool inside_payload) {
    for (auto child = src.first_child(); child.is_valid(); child = child.next_sibling()) {
      if (!inside_payload && dead_stmts.contains(child.get_class_index().value)) {
        continue;
      }
      const auto type = staging->get_type(child);
      Lnast_nid  copied;
      if (Lnast_ntype::is_ref(type)) {
        copied = new_staging->add_child(dst, Lnast_node::create_ref(staging->get_name(child)));
      } else if (Lnast_ntype::is_const(type)) {
        copied = new_staging->add_child(dst, Lnast_node::create_const(staging->get_name(child)));
      } else {
        copied = new_staging->add_child(dst, type);
        if (Lnast::srcid_carries(type)) {
          new_staging->set_srcid(copied, staging->get_srcid(child));
        }
        copy_live(child, copied, inside_payload || !is_structural(type));
      }
    }
  };
  copy_live(src_root, dst_root, false);

  if (dispatch_stats_) {
    std::print(
        stderr,
        "uPass stats [{}]:   DCE mark {} dead stmts in {:.2f}s, rebuild {:.2f}s\n",
        lm->get_top_module_name(),
        dead_stmts.size(),
        std::chrono::duration_cast<std::chrono::milliseconds>(dce_t_marked - dce_t_scan).count() / 1e3,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dce_t_marked).count() / 1e3);
  }
  staging = new_staging;
}

// ── Streaming detuple front end ───────────────────────────────────────────────

std::string uPass_runner::detuple_text(const Lnast_nid& nid) const {
  // The source tree is shared across call sites. Resolve refs through the
  // active inline/iteration frame, just like the normal pass dispatch does;
  // raw names would mix a callee's tuple layout with the caller's layout.
  const auto saved = lm->save_cursor();
  lm->move_to_nid(nid);
  std::string text(lm->current_text());
  lm->restore_cursor(saved);
  return text;
}

// The detupled storage of an aggregate is its per-field leaf set, keyed by the
// BASE name, while SSA re-versions the aggregate on a second whole-tuple write
// (a typed `mut p:(..) = (a, w)` is declare-seed + init: `p` then
// `p___ssa_1`). Every read or write of a versioned aggregate name resolves to
// the base split; a name that is not (a version of) a split comes back as is.
std::string uPass_runner::detuple_split_name(std::string_view var) const {
  if (!detuple_splits_.contains(var)) {
    if (const auto pos = var.find("___ssa_"); pos != std::string_view::npos) {
      if (const auto base = var.substr(0, pos); detuple_splits_.contains(base)) {
        return std::string(base);
      }
    }
  }
  return std::string(var);
}

std::optional<uPass_detuple_registry::Scalar_type> uPass_runner::detuple_scalar_type(std::string_view name) const {
  const auto bundle = symbol_table_.get_bundle(name);
  if (!bundle) {
    return std::nullopt;
  }
  const auto entries = bundle->non_attr_entries();
  if (entries.size() != 1) {
    return std::nullopt;
  }
  const auto&                         entry = entries.begin()->second;
  uPass_detuple_registry::Scalar_type type{.kind = entry.kind, .max = entry.decl_max, .min = entry.decl_min};
  if (!type.valid()) {
    return std::nullopt;
  }
  return type;
}

void uPass_runner::detuple_error(std::string code, std::string message, std::string hint) {
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = std::move(code),
                                                     .category = "type",
                                                     .pass     = "upass.detuple",
                                                     .message  = std::move(message),
                                                     .span     = lm->current_span(),
                                                     .hint     = std::move(hint)});
}

void uPass_runner::detuple_emit_declare(std::string_view name, const uPass_detuple_registry::Scalar_type& type,
                                        std::string_view mode, const Lnast_node* init, const Lnast_node* dimension) {
  if (!scratch_forest_) {
    scratch_forest_ = hhds::Forest::create();
  }
  auto body = scratch_forest_->create_tree_temp("detuple-decl");
  auto ln   = std::make_shared<Lnast>(body, std::string(root_lnast_->get_top_module_name()));
  auto root = ln->set_root(Lnast_ntype::create_declare());
  stamp_scratch_srcid(ln, root);
  ln->add_child(root, Lnast_node::create_ref(name));
  auto emit_scalar_type = [&](const Lnast_nid& parent) {
    if (type.kind == upass::Kind::boolean) {
      ln->add_child(parent, Lnast_ntype::create_prim_type_bool());
      return;
    }
    if (type.kind == upass::Kind::unknown) {
      // Untyped struct-reg leaf (`reg V = (x=20, y=40)`): like an untyped
      // scalar `reg foo = 20`, the bitwidth pass infers the width from the
      // reset value and the writes.
      ln->add_child(parent, Lnast_ntype::create_prim_type_none());
      return;
    }
    auto prim = ln->add_child(parent, Lnast_ntype::create_prim_type_int());
    ln->add_child(prim, Lnast_node::create_const(type.max.is_invalid() ? "nil" : std::string(type.max.to_pyrope())));
    ln->add_child(prim, Lnast_node::create_const(type.min.is_invalid() ? "nil" : std::string(type.min.to_pyrope())));
  };
  if (dimension != nullptr && !dimension->is_invalid()) {
    auto array = ln->add_child(root, Lnast_ntype::create_comp_type_array());
    emit_scalar_type(array);
    ln->add_child(array, *dimension);
  } else {
    emit_scalar_type(root);
  }
  ln->add_child(root, Lnast_node::create_const(mode));
  if (init != nullptr && !init->is_invalid()) {
    ln->add_child(root, *init);
  }
  flush_deferred_emits();
  lm->push_source(ln, "", 0);
  detuple_synthetic_ = true;
  process_lnast();
  detuple_synthetic_ = false;
  flush_deferred_emits();
  lm->pop_source();
}

void uPass_runner::detuple_emit_store(std::string_view name, const std::vector<Lnast_node>& operands) {
  detuple_synthetic_ = true;
  emit_inline_op(Lnast_ntype::create_store(), std::string(name), operands);
  detuple_synthetic_ = false;
}

bool uPass_runner::detuple_validate_scalar_store(std::string_view name, const uPass_detuple_registry::Scalar_type& type,
                                                 const Lnast_node& value) {
  if (!value.is_const() || value.get_name() == "nil" || value.get_name() == "0sb?" || value.get_name() == "0ub?") {
    return true;
  }
  const std::string_view text    = value.get_name();
  const bool             is_bool = text == "true" || text == "false";
  spool_ptr<Dlop>        parsed;
  try {
    parsed = Dlop::from_pyrope(text);
  } catch (...) {
    // Leave malformed literals to the ordinary parser/typecheck diagnostic.
  }
  const bool is_string = parsed && !parsed->is_invalid() && parsed->is_string();
  if ((type.kind == upass::Kind::integer && (is_string || is_bool)) || (type.kind == upass::Kind::boolean && !is_bool)) {
    const auto expected = type.kind == upass::Kind::boolean ? "boolean" : "integer";
    const auto actual   = is_string ? "string" : (is_bool ? "boolean" : "integer");
    detuple_error("field-kind-mismatch",
                  std::format("`{}` is {} but the assigned value is {}; a variable's type cannot change", name, expected, actual));
    return false;
  }
  if (type.kind != upass::Kind::integer) {
    return true;
  }
  if (!parsed || parsed->is_invalid() || !parsed->is_integer() || parsed->has_unknowns()) {
    return true;
  }
  const bool over  = !type.max.is_invalid() && parsed->gt_op(type.max)->is_known_true();
  const bool under = !type.min.is_invalid() && parsed->lt_op(type.min)->is_known_true();
  if (over || under) {
    detuple_error("field-overflow",
                  std::format("`{}` value {} does not fit its declared range [{}, {}]",
                              name,
                              parsed->to_decimal_string(),
                              type.min.is_invalid() ? "-inf" : type.min.to_decimal_string(),
                              type.max.is_invalid() ? "+inf" : type.max.to_decimal_string()));
    return false;
  }
  return true;
}

void uPass_runner::detuple_flush_pending_decl() {
  if (!detuple_pending_decl_) {
    return;
  }
  const auto pending = std::move(*detuple_pending_decl_);
  detuple_pending_decl_.reset();
  const auto saved = lm->save_cursor();
  lm->move_to_nid(pending.nid);
  detuple_replay_ = true;
  process_lnast();
  detuple_replay_ = false;
  lm->restore_cursor(saved);
}

bool uPass_runner::detuple_finalize_pending_decl() {
  if (!detuple_pending_decl_ || detuple_pending_decl_->fields.empty()) {
    return false;
  }
  auto pending = std::move(*detuple_pending_decl_);
  detuple_pending_decl_.reset();
  // Shared with the shape-bind path so a declaration finalized WITHOUT a
  // shape store (Slang predecl facts, or an untyped `reg V = (x=…, y=…)`
  // whose fields came from the init bundle) still projects its per-field
  // init/reset values.
  detuple_commit_pending_split(pending);
  return true;
}

void uPass_runner::detuple_flush_pending_before_current() {
  if (!detuple_pending_decl_ || detuple_replay_ || detuple_synthetic_) {
    return;
  }
  if (lm->get_current_nid() == detuple_pending_decl_->nid) {
    return;
  }
  const auto type = lm->get_raw_ntype();
  if (Lnast_ntype::is_type_spec(type) || Lnast_ntype::is_tuple_add(type)) {
    return;  // declaration-shape cluster
  }
  if (Lnast_ntype::is_store(type) && lm->has_child()) {
    const auto saved = lm->save_cursor();
    lm->move_to_child();
    const bool owns = Lnast_ntype::is_ref(lm->get_raw_ntype()) && lm->current_text() == detuple_pending_decl_->name;
    lm->restore_cursor(saved);
    if (owns) {
      // Slang declares an aggregate, streams its dotted type_spec facts, and
      // then assigns the aggregate. Once those facts are known there is no
      // shape bind to wait for: publish scalar declarations before processing
      // this first store, which can then be expanded through detuple_splits_.
      if (!detuple_pending_decl_->shape_tmp) {
        (void)detuple_finalize_pending_decl();
      }
      return;
    }
  }
  if (!detuple_pending_decl_->shape_tmp && detuple_finalize_pending_decl()) {
    return;
  }
  detuple_flush_pending_decl();
}

bool uPass_runner::try_detuple_declare() {
  if (detuple_registry_ == nullptr || detuple_replay_ || detuple_synthetic_ || !lm->has_child()) {
    return false;
  }
  const auto& ln   = lm->get_lnast();
  const auto  node = lm->get_current_nid();
  auto        name = ln->get_first_child(node);
  auto        type = name.is_invalid() ? name : ln->get_sibling_next(name);
  auto        mode = type.is_invalid() ? type : ln->get_sibling_next(type);
  auto        init = mode.is_invalid() ? mode : ln->get_sibling_next(mode);
  if (name.is_invalid() || type.is_invalid() || mode.is_invalid() || !Lnast_ntype::is_ref(ln->get_type(name))
      || !Lnast_ntype::is_const(ln->get_type(mode))) {
    return false;
  }
  const std::string var{detuple_text(name)};
  const std::string mode_text{detuple_text(mode)};

  // Named tuple memory: the owning file-level runner has already published the
  // type layout. Scalarize immediately, before any other pass sees the
  // aggregate declaration.
  //
  // The type slot is EITHER comp_type_array (a named tuple MEMORY,
  // `reg mem:[N]Decode`) OR a bare ref (a named tuple SCALAR, `reg instr:Decode`).
  // Only the array form used to be handled, so a scalar named tuple stayed an
  // aggregate and its field writes reached tolg as multi-element stores
  // ("tuple/field store to 'instr' has no hardware lowering") -- even though the
  // ANONYMOUS spelling of the same shape, `reg instr:(add:bool, …)`, splits fine.
  // The named_types hit is the whole guard: a scalar type ALIAS (`type Byte = u8`)
  // publishes no layout, and an unknown/cross-file type misses the unit-qualified
  // key, so both keep their existing fall-through.
  if (detuple_registry_ != nullptr) {
    Lnast_nid  elem{};
    Lnast_nid  dim{};
    const bool is_array = Lnast_ntype::is_comp_type_array(ln->get_type(type));
    if (is_array) {
      elem = ln->get_first_child(type);
      dim  = elem.is_invalid() ? elem : ln->get_sibling_next(elem);
      if (elem.is_invalid() || dim.is_invalid() || ln->get_sibling_next(dim).is_valid()) {
        elem = Lnast_nid{};
      }
    } else if (Lnast_ntype::is_ref(ln->get_type(type))) {
      elem = type;  // `reg instr:Decode` — the named type IS the type slot
    }
    if (!elem.is_invalid() && Lnast_ntype::is_ref(ln->get_type(elem))) {
      if (const auto it = detuple_registry_->named_types.find(detuple_registry_key(ln->get_name(elem)));
          it != detuple_registry_->named_types.end()) {
        // Resolve the initializer BEFORE committing the split. A ref init is a
        // whole-ELEMENT tuple value: fanning the un-projected aggregate into
        // every leaf silently gave each field-memory the whole tuple temp as
        // its reset. Project per field; a ref that is not a recorded tuple
        // literal cannot be projected here, so decline the split and let the
        // ordinary aggregate machinery own (and loudly reject) it.
        std::vector<std::optional<Lnast_node>> field_inits(it->second.size());
        std::optional<Lnast_node>              broadcast_init;
        if (!init.is_invalid()) {
          if (Lnast_ntype::is_ref(ln->get_type(init))) {
            const auto vit = detuple_tuple_values_.find(std::string(detuple_text(init)));
            if (vit == detuple_tuple_values_.end()) {
              return false;
            }
            if (!detuple_project_init(var, vit->second, it->second, field_inits, ln->get_name(elem))) {
              return true;  // reported
            }
          } else if (Lnast_ntype::is_const(ln->get_type(init))) {
            broadcast_init = Lnast_node::create_const(detuple_text(init));  // nil / scalar fill
          }
        }
        // A register's reset value must fit each leaf's declared range, like
        // a scalar `reg r:u4 = 20` (the reset was silently truncated).
        if (mode_text == "reg" || mode_text.starts_with("reg ")) {
          for (std::size_t i = 0; i < it->second.size(); ++i) {
            const auto& leaf_type = it->second[i].type;
            // A scalar fill (`= 0`) resets every leaf; only its range matters.
            const auto& value     = broadcast_init && leaf_type.kind == upass::Kind::integer ? broadcast_init : field_inits[i];
            if (value && !detuple_validate_scalar_store(var + "." + it->second[i].name, leaf_type, *value)) {
              return true;  // reported
            }
          }
        }
        // A scalar named tuple has no dimension: `memory` must stay false, or
        // try_detuple_tuple_get/try_detuple_store route it through the
        // memory index-alias path.
        Lnast_node dim_node;
        if (is_array) {
          dim_node = Lnast_ntype::is_ref(ln->get_type(dim)) ? Lnast_node::create_ref(detuple_text(dim))
                                                            : Lnast_node::create_const(detuple_text(dim));
        }
        // Keep the aggregate's storage identity alive before publishing dotted
        // leaves. Otherwise their reg/wire facts have no root to attach to.
        bake_decl_pre_step(/*is_declare=*/true);
        Detuple_split split{.fields = it->second, .mode = mode_text, .memory = is_array, .dimension = dim_node};
        split.whole_bound = !init.is_invalid();  // the declaration's own initializer is the construction
        detuple_splits_.insert_or_assign(var, split);
        for (std::size_t i = 0; i < split.fields.size(); ++i) {
          const auto&               field      = split.fields[i];
          const auto                leaf       = var + "." + field.name;
          std::optional<Lnast_node> field_init = broadcast_init ? broadcast_init : field_inits[i];
          detuple_emit_declare(leaf,
                               field.type,
                               mode_text,
                               field_init ? &*field_init : nullptr,
                               is_array ? &split.dimension : nullptr);
        }
        return true;
      }
    }
  }

  // Parser tuple declarations are emitted as an untyped/array-looking
  // declaration followed immediately by type_spec + shape nodes. Delay only
  // these ambiguous hardware declarations; a scalar/ordinary array is replayed
  // unchanged as soon as the next non-shape statement arrives. A TYPED `const`
  // tuple (its declaration is followed by the per-field `type_spec`s) binds its
  // possibly runtime initializer once, like a `mut`, and is split the same way:
  // otherwise a positional or call-result initializer never reaches the named
  // fields. An untyped `const` keeps its ordinary binding.
  const auto typed_tuple_follows = [&]() {
    const auto next = ln->get_sibling_next(node);
    if (next.is_invalid() || !Lnast_ntype::is_type_spec(ln->get_type(next))) {
      return false;
    }
    const auto target = ln->get_first_child(next);
    return !target.is_invalid() && Lnast_ntype::is_ref(ln->get_type(target)) && detuple_text(target).starts_with(var + ".");
  };
  const bool hardware_mode = mode_text == "wire" || mode_text.starts_with("wire ") || mode_text == "reg"
                             || mode_text.starts_with("reg ") || mode_text == "mut"
                             || (mode_text == "const" && typed_tuple_follows());
  const bool maybe_tuple
      = Lnast_ntype::is_prim_type_none(ln->get_type(type)) || Lnast_ntype::is_comp_type_array(ln->get_type(type));
  if (!hardware_mode || !maybe_tuple) {
    return false;
  }

  // Slang's structural pseudo-variables carry their field type_specs BEFORE
  // this bare declaration. That is already sufficient to split a declaration
  // with no value initializer; no future node is needed and no source tree is
  // revisited. (A ref initializer still takes the pending shape-bind path
  // below because its per-field values must be paired with the layout.)
  if (init.is_invalid() || (Lnast_ntype::is_const(ln->get_type(init)) && detuple_text(init) == "nil")) {
    if (auto fields = detuple_predecl_fields_.find(var); fields != detuple_predecl_fields_.end() && !fields->second.empty()) {
      Detuple_split split{.fields = std::move(fields->second), .mode = mode_text, .memory = false};
      detuple_predecl_fields_.erase(fields);
      detuple_splits_.insert_or_assign(var, split);
      for (const auto& field : split.fields) {
        detuple_emit_declare(var + "." + field.name, field.type, mode_text);
      }
      return true;
    }
  }
  detuple_flush_pending_decl();
  Detuple_pending_decl pending;
  pending.nid  = node;
  pending.name = var;
  pending.mode = mode_text;
  if (!init.is_invalid() && Lnast_ntype::is_ref(ln->get_type(init))) {
    pending.init_ref = std::string(detuple_text(init));
  }
  // Untyped struct reg `reg V = (x=20, y=40)`: the named all-const init
  // literal was already recorded (it precedes the declare). Seed one UNTYPED
  // leaf per entry — like an untyped scalar `reg foo = 20`, bitwidth infers
  // each width from reset + writes — so the declaration can finalize into
  // per-field flops instead of replaying into a tolg hard error. A TYPED reg
  // refines/replaces these through its dotted type_specs and shape bind.
  if (pending.init_ref && (mode_text == "reg" || mode_text.starts_with("reg "))) {
    if (const auto vit = detuple_tuple_values_.find(*pending.init_ref); vit != detuple_tuple_values_.end()) {
      const auto& recorded = vit->second;
      if (recorded.named && recorded.positional.empty()
          && std::all_of(recorded.fields.begin(), recorded.fields.end(), [](const auto& entry) {
               return entry.second.is_const();
             })) {
        for (const auto& [field_name, value] : recorded.fields) {
          (void)value;
          pending.fields.push_back({field_name, uPass_detuple_registry::Scalar_type{}});
        }
      }
    }
  }
  detuple_pending_decl_ = std::move(pending);
  // The detupler owns this first semantic update. It lets following field
  // type_specs resolve through the real scoped symbol table while the original
  // aggregate declaration remains invisible to the other passes.
  bake_decl_pre_step(/*is_declare=*/true);
  return true;
}

bool uPass_runner::try_detuple_tuple_add() {
  if (detuple_registry_ == nullptr || detuple_synthetic_ || !lm->has_child()) {
    return false;
  }
  const auto& ln   = lm->get_lnast();
  const auto  node = lm->get_current_nid();
  auto        dst  = ln->get_first_child(node);
  if (dst.is_invalid() || !Lnast_ntype::is_ref(ln->get_type(dst))) {
    return false;
  }
  const std::string              tmp{detuple_text(dst)};
  Detuple_tuple_value            tuple;
  bool                           all_typed_refs = true;
  uPass_detuple_registry::Layout shape;
  uPass_detuple_registry::Layout named_layout;              // 2f-defaulted_tuple, see detuple_named_layout_
  bool                           saw_named_field  = false;  // at least one DEFAULTED (store) child
  // Every child must contribute exactly one named_layout entry, or the layout is
  // SHORT and `reg r:T` would split into too few flops (the field that could not
  // be described silently disappears). Any child shape that cannot be named +
  // typed here (a positional const, a ref whose type does not resolve -- e.g.
  // `payload:Inner` -- or a malformed store) disqualifies the whole layout, which
  // falls back to the pre-2f-defaulted_tuple behavior instead of mis-splitting.
  bool                           layout_complete  = true;
  // 2f-nested_type — every child is a BARE ref that resolved NO scalar type:
  // the shape seed of a NESTED tuple type (`tuple_add %ctl_0, ref 'ex', ref 'wb'`).
  bool                           all_untyped_refs = true;
  int                            child_count      = 0;
  for (auto child = ln->get_sibling_next(dst); child.is_valid(); child = ln->get_sibling_next(child)) {
    const auto ct = ln->get_type(child);
    ++child_count;
    if (!Lnast_ntype::is_ref(ct)) {
      all_untyped_refs = false;
    }
    if (Lnast_ntype::is_ref(ct)) {
      const std::string name{detuple_text(child)};
      tuple.positional.emplace_back(Lnast_node::create_ref(name));
      tuple.positional_src.emplace_back(ln->get_name(child));
      if (const auto type = detuple_scalar_type(name)) {
        all_untyped_refs = false;
        // A typed tuple entry's field label stays source-spelled, while the
        // variable carrying its type is scoped to this inline call.
        shape.push_back({std::string(ln->get_name(child)), *type});
        // 2f-defaulted_tuple: a type may MIX defaulted and plain fields
        // (`(mut flag:bool = false, mut state:u2)`). Both child shapes must
        // land in the named layout, in source order -- publishing only the
        // defaulted ones would split a register into too few flops.
        named_layout.push_back({std::string(ln->get_name(child)), *type});
      } else {
        all_typed_refs  = false;
        layout_complete = false;
      }
    } else if (Lnast_ntype::is_const(ct)) {
      tuple.positional.emplace_back(Lnast_node::create_const(detuple_text(child)));
      tuple.positional_src.emplace_back();
      all_typed_refs  = false;
      layout_complete = false;
    } else if (Lnast_ntype::is_store(ct)) {
      auto key = ln->get_first_child(child);
      auto val = key.is_invalid() ? key : ln->get_sibling_next(key);
      if (key.is_invalid() || val.is_invalid() || ln->get_sibling_next(val).is_valid() || !Lnast_ntype::is_ref(ln->get_type(key))
          || (!Lnast_ntype::is_ref(ln->get_type(val)) && !Lnast_ntype::is_const(ln->get_type(val)))) {
        all_typed_refs  = false;
        layout_complete = false;
        continue;
      }
      tuple.named = true;
      tuple.fields.emplace_back(std::string(detuple_text(key)),
                                Lnast_ntype::is_ref(ln->get_type(val)) ? Lnast_node::create_ref(detuple_text(val))
                                                                       : Lnast_node::create_const(detuple_text(val)));
      // 2f-defaulted_tuple: remember the field and its SOURCE ORDER. The type
      // is unknown here and arrives later, through detuple_field_alias_.
      named_layout.push_back({std::string(detuple_text(key)), uPass_detuple_registry::Scalar_type{}});
      saw_named_field = true;
      all_typed_refs  = false;
    } else {
      all_typed_refs  = false;
      layout_complete = false;
    }
  }
  detuple_tuple_values_.insert_or_assign(tmp, std::move(tuple));
  if (all_typed_refs && !shape.empty()) {
    detuple_shape_fields_.insert_or_assign(tmp, shape);
  } else if (saw_named_field && layout_complete && !named_layout.empty()) {
    detuple_named_layout_.insert_or_assign(tmp, std::move(named_layout));
  }

  if (!detuple_pending_decl_) {
    return false;  // ordinary tuple: the normal passes still own it
  }
  // Untyped struct reg `reg V = (x=20, y=40)`: the declaration went pending
  // with init_ref = this literal's temp and no dotted type_specs. The deleted
  // whole-tree pass derived one UNTYPED leaf per named all-const init entry
  // (width inferred from reset + writes); reproduce that so the declaration
  // finalizes with per-field resets instead of replaying the aggregate into a
  // tolg hard error. Positional untyped `(20,40)` stays array-like, untouched.
  if (detuple_pending_decl_->fields.empty() && !detuple_pending_decl_->shape_tmp && detuple_pending_decl_->init_ref
      && *detuple_pending_decl_->init_ref == tmp
      && (detuple_pending_decl_->mode == "reg" || detuple_pending_decl_->mode.starts_with("reg "))) {
    const auto& recorded = detuple_tuple_values_.at(tmp);
    if (recorded.named && recorded.positional.empty()
        && std::all_of(recorded.fields.begin(), recorded.fields.end(), [](const auto& entry) { return entry.second.is_const(); })) {
      for (const auto& [field_name, value] : recorded.fields) {
        (void)value;
        detuple_pending_decl_->fields.push_back({field_name, uPass_detuple_registry::Scalar_type{}});
      }
      return false;  // the literal itself still lowers normally (DCE'd later)
    }
  }
  // A declaration's dotted type_specs already describe its shape. Match
  // those labels directly, without consulting same-named local variables.
  // This also covers a mix of scalar and nested fields in one declaration.
  const auto& declared    = detuple_pending_decl_->fields;
  const auto& recorded    = detuple_tuple_values_.at(tmp);
  auto        names_field = [](const Lnast_node& label, const auto& field) {
    return label.is_ref() && (field.name == label.get_name() || field.name.starts_with(std::string(label.get_name()) + "."));
  };
  if (!declared.empty() && !recorded.named && recorded.positional.size() == static_cast<std::size_t>(child_count)
      && std::all_of(recorded.positional.begin(),
                     recorded.positional.end(),
                     [&](const auto& label) {
                       return std::any_of(declared.begin(), declared.end(), [&](const auto& field) {
                         return names_field(label, field);
                       });
                     })
      && std::all_of(declared.begin(), declared.end(), [&](const auto& field) {
           return std::any_of(recorded.positional.begin(), recorded.positional.end(), [&](const auto& label) {
             return names_field(label, field);
           });
         })) {
    detuple_pending_decl_->shape_tmp = tmp;
    return true;
  }
  if (!all_typed_refs || shape.empty()) {
    // 2f-nested_type — a NESTED tuple type's shape seed carries BARE refs for
    // the nested fields (`tuple_add %ctl_0, ref 'ex', ref 'wb'`), which have no
    // scalar type of their own, so `shape` is empty. The real layout is the
    // DOTTED LEAF type_specs prp2lnast emits just above (`ctl.ex.aa : u1`),
    // which have already landed in this pending declaration's fields. Consume
    // the seed and keep the declaration pending so the trailing
    // `store(ctl, %ctl_0)` commits the split. Flushing here replayed the
    // declare verbatim and left the seed temp with no driver, which is why a
    // nested tuple type could not be storage at all ("unresolved reference
    // '%ctl_0'").
    if (all_untyped_refs && child_count > 0 && !detuple_pending_decl_->shape_tmp && !detuple_pending_decl_->fields.empty()) {
      detuple_pending_decl_->shape_tmp = tmp;
      return true;  // virtual shape: do not dispatch or materialize it
    }
    detuple_flush_pending_decl();
    return false;
  }
  // A pending declaration binds only ITS OWN shape. When dotted type_specs
  // already collected field facts, an unrelated all-typed-refs literal must
  // not hijack the bind (it used to clear those facts wholesale) — require
  // the same field-name set, else fall back to the loud verbatim replay.
  if (!detuple_pending_decl_->fields.empty()) {
    const auto& collected = detuple_pending_decl_->fields;
    const bool same_set = collected.size() == shape.size() && std::all_of(shape.begin(), shape.end(), [&](const auto& shape_field) {
                            return std::any_of(collected.begin(), collected.end(), [&](const auto& field) {
                              return field.name == shape_field.name;
                            });
                          });
    if (!same_set) {
      detuple_flush_pending_decl();
      return false;
    }
  }
  detuple_pending_decl_->shape_tmp = tmp;
  detuple_pending_decl_->fields    = std::move(shape);
  return true;  // virtual shape: do not dispatch or materialize it
}

void uPass_runner::detuple_flatten_tuple_value(const std::string& rhs, const std::string& prefix,
                                               std::vector<std::pair<std::string, Lnast_node>>& out, int depth) {
  if (depth > 8) {
    return;  // pathological nesting: bail rather than recurse without bound
  }
  const auto it = detuple_tuple_values_.find(rhs);
  if (it == detuple_tuple_values_.end()) {
    return;
  }
  for (const auto& [key, value] : it->second.fields) {
    const std::string path = prefix.empty() ? key : prefix + "." + key;
    if (value.is_ref() && detuple_tuple_values_.contains(std::string(value.get_name()))) {
      detuple_flatten_tuple_value(std::string(value.get_name()), path, out, depth + 1);
    } else {
      out.emplace_back(path, value);
    }
  }
}

bool uPass_runner::detuple_layout_is_flat(const uPass_detuple_registry::Layout& fields) {
  return !fields.empty()
         && std::all_of(fields.begin(), fields.end(), [](const auto& f) { return f.name.find('.') == std::string::npos; });
}

// A typed tuple constructed with UNNAMED values binds them like the arguments of
// a call (06-functions.md "Argument naming", qa.md Q35): a bare variable whose
// name is a field, the lone field, or a value whose type is unique among ALL the
// fields; anything else must be named. `out` holds the fields already bound by
// name; the unnamed values fill the rest. False when reported.
bool uPass_runner::detuple_bind_unnamed_values(std::string_view var, std::string_view type_name, const Detuple_tuple_value& value,
                                               const uPass_detuple_registry::Layout&   fields,
                                               std::vector<std::optional<Lnast_node>>& out) {
  if (value.positional_src.size() != value.positional.size()) {
    return true;
  }
  std::vector<bool> taken(fields.size(), false);
  for (std::size_t i = 0; i < fields.size(); ++i) {
    taken[i] = out[i].has_value();  // bound by name
  }
  const auto kind_of = [&](const Lnast_node& entry) -> upass::Kind {
    // The same kind the call binder uses for a positional actual.
    switch (actual_node_kind(entry)) {
      case Io_kind::integer: return upass::Kind::integer;
      case Io_kind::boolean: return upass::Kind::boolean;
      default              : return upass::Kind::unknown;
    }
  };
  for (std::size_t k = 0; k < value.positional.size(); ++k) {
    std::optional<std::size_t> slot;
    if (const auto& src = value.positional_src[k]; !src.empty()) {
      for (std::size_t i = 0; i < fields.size(); ++i) {
        if (!taken[i] && fields[i].name == src) {
          slot = i;  // exception 2: a bare variable spelling a field
          break;
        }
      }
    }
    if (!slot && fields.size() == 1 && !taken[0]) {
      slot = 0;  // exception 1: a single field
    }
    if (!slot) {
      if (const auto want = kind_of(value.positional[k]); want != upass::Kind::unknown) {
        std::size_t count = 0;  // exception 3: the value's type is unique among ALL the fields
        std::size_t match = 0;
        for (std::size_t i = 0; i < fields.size(); ++i) {
          if (fields[i].type.kind == want) {
            match = i;
            ++count;
          }
        }
        if (count == 1 && !taken[match]) {
          slot = match;
        }
      }
    }
    if (!slot) {
      detuple_error("tuple-unnamed-field",
                    std::format("an unnamed value is ambiguous in the construction of `{}`: the value must be named "
                                "(`field=value`)",
                                type_name.empty() ? std::string(var) : std::string(type_name)),
                    "name the field, pass a variable whose name matches it, or give a value whose type is unique among all "
                    "the fields");
      return false;
    }
    taken[*slot] = true;
    out[*slot]   = value.positional[k];
  }
  return true;
}

bool uPass_runner::detuple_project_init(std::string_view var, const Detuple_tuple_value& init,
                                        const uPass_detuple_registry::Layout& fields, std::vector<std::optional<Lnast_node>>& out,
                                        std::string_view type_name) {
  out.assign(fields.size(), std::nullopt);
  // Does `path` name a leaf, or a nested field with leaves below it?
  auto names_field = [&](const std::string& path) {
    return std::any_of(fields.begin(), fields.end(), [&](const auto& field) {
      return field.name == path
             || (field.name.starts_with(path) && field.name.size() > path.size() && field.name[path.size()] == '.');
    });
  };
  // One tuple level: `prefix` is the dotted path of the (sub)tuple `value`
  // initializes, its children are the distinct next path components of the
  // leaves below it, in layout order.
  std::function<bool(const Detuple_tuple_value&, const std::string&, int)> project;
  project = [&](const Detuple_tuple_value& value, const std::string& prefix, int depth) -> bool {
    std::vector<std::string> children;
    std::vector<std::size_t> leaves;  // the layout leaves below `prefix`, in order
    for (std::size_t i = 0; i < fields.size(); ++i) {
      if (!fields[i].name.starts_with(prefix)) {
        continue;
      }
      leaves.push_back(i);
      const auto rest  = std::string_view(fields[i].name).substr(prefix.size());
      auto       child = std::string(rest.substr(0, rest.find('.')));
      if (std::find(children.begin(), children.end(), child) == children.end()) {
        children.push_back(std::move(child));
      }
    }
    auto bind = [&](const std::string& path, const Lnast_node& entry) -> bool {
      for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == path) {
          out[i] = entry;
          return true;
        }
      }
      // A nested field: its leaves come from a nested tuple literal, or from
      // the leaves of an already split tuple variable. `nil` leaves them
      // without a value, like a whole `= nil`.
      if (entry.is_const() && (entry.get_name() == "nil" || entry.get_name() == "0sb?")) {
        return true;
      }
      if (entry.is_ref()) {
        const auto src = detuple_split_name(entry.get_name());
        if (const auto sit = detuple_splits_.find(src); sit != detuple_splits_.end() && !sit->second.memory) {
          // The variable must have exactly the nested field's leaves: a
          // missing one would leave a leaf silently without a value.
          const auto  sub_prefix = path + ".";
          std::size_t bound      = 0;
          bool        all_found  = true;
          for (std::size_t i = 0; i < fields.size(); ++i) {
            if (!fields[i].name.starts_with(sub_prefix)) {
              continue;
            }
            const auto sub = fields[i].name.substr(sub_prefix.size());
            if (std::none_of(sit->second.fields.begin(), sit->second.fields.end(), [&](const auto& f) { return f.name == sub; })) {
              all_found = false;
              continue;
            }
            out[i] = Lnast_node::create_ref(src + "." + sub);
            ++bound;
          }
          if (all_found && bound == sit->second.fields.size()) {
            return true;
          }
          detuple_error(
              "tuple-assignment-shape",
              std::format("`{}` initializer gives nested tuple field `{}` the tuple `{}` of another shape", var, path, src));
          return false;
        }
      }
      const auto nested = entry.is_ref() ? detuple_tuple_values_.find(std::string(entry.get_name())) : detuple_tuple_values_.end();
      if (nested == detuple_tuple_values_.end() || depth > 8) {
        detuple_error("tuple-assignment-shape",
                      std::format("`{}` initializer gives nested tuple field `{}` a value that is not a tuple literal", var, path));
        return false;
      }
      return project(nested->second, path + ".", depth + 1);
    };
    if (value.named) {
      // A key names a field of this level, or, dotted (`n.x = 5`, docs
      // 03-bundle "Dotted Field Expansion"), a field below it.
      for (const auto& [key, entry] : value.fields) {
        if (!names_field(prefix + key)) {
          detuple_error("tuple-assignment-shape", std::format("`{}` initializer names unknown field `{}{}`", var, prefix, key));
          return false;
        }
        if (!bind(prefix + key, entry)) {
          return false;
        }
      }
      if (value.positional.empty()) {
        return true;
      }
    }
    if (value.positional.empty()) {
      return true;
    }
    // A FLAT typed tuple constructed with unnamed values binds them like the
    // arguments of a call (see detuple_bind_unnamed_values).
    if (prefix.empty() && depth == 0 && detuple_layout_is_flat(fields)) {
      return detuple_bind_unnamed_values(var, type_name, value, fields, out);
    }
    // Positional: flat, one scalar per leaf below this level, or one entry per
    // field of this level (a nested field takes a nested tuple). The two
    // readings meet only when every nested field has one leaf; scalars there
    // read flat, as they did before nested initializers existed.
    const bool all_scalar = std::none_of(value.positional.begin(), value.positional.end(), [&](const Lnast_node& entry) {
      return entry.is_ref()
             && (detuple_tuple_values_.contains(std::string(entry.get_name()))
                 || detuple_splits_.contains(detuple_split_name(entry.get_name())));
    });
    if (all_scalar && value.positional.size() == leaves.size()) {
      for (std::size_t i = 0; i < leaves.size(); ++i) {
        out[leaves[i]] = value.positional[i];
      }
      return true;
    }
    if (value.positional.size() == children.size()) {
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (!bind(prefix + children[i], value.positional[i])) {
          return false;
        }
      }
      return true;
    }
    std::string what;
    if (!prefix.empty()) {
      what = std::format("`{}`", prefix.substr(0, prefix.size() - 1));
    } else if (!type_name.empty()) {
      what = std::format("type `{}`", type_name);
    } else {
      what = "the tuple";
    }
    detuple_error(
        "tuple-assignment-shape",
        std::format("`{}` initializer has {} entries but {} has {} fields", var, value.positional.size(), what, children.size()));
    return false;
  };
  return project(init, "", 0);
}

void uPass_runner::detuple_publish_named_type(std::string_view name, std::string_view rhs) {
  if (detuple_registry_ == nullptr) {
    return;
  }
  const auto binding = symbol_table_.get_bundle(name);
  if (!binding || binding->get_mode() != upass::Mode::type_kind) {
    return;
  }
  // A nested named type's dotted leaf facts are emitted on the type name.
  // Publish only a complete shape: every top-level field must have a leaf.
  if (const auto layout = detuple_predecl_fields_.find(std::string(name)); layout != detuple_predecl_fields_.end()) {
    const auto shape = detuple_tuple_values_.find(std::string(rhs));
    if (shape != detuple_tuple_values_.end() && !shape->second.named && !shape->second.positional.empty()
        && std::all_of(layout->second.begin(), layout->second.end(), [](const auto& field) { return field.type.valid(); })
        && std::all_of(shape->second.positional.begin(), shape->second.positional.end(), [&](const auto& entry) {
             if (!entry.is_ref()) {
               return false;
             }
             const std::string field{entry.get_name()};
             return std::any_of(layout->second.begin(), layout->second.end(), [&](const auto& leaf) {
               return leaf.name == field || leaf.name.starts_with(field + ".");
             });
           })) {
      detuple_registry_->named_types.insert_or_assign(detuple_registry_key(name), layout->second);
      return;
    }
  }
  if (const auto sit = detuple_shape_fields_.find(std::string(rhs)); sit != detuple_shape_fields_.end()) {
    detuple_registry_->named_types.insert_or_assign(detuple_registry_key(name), sit->second);
    return;
  }
  // 2f-defaulted_tuple — a type whose fields carry DEFAULTS never reaches
  // detuple_shape_fields_ (its children are named stores, not typed refs).
  // Publish the named layout instead, but only once EVERY field's type has
  // arrived: a partially-typed layout would size a register's flops wrong.
  const auto nit = detuple_named_layout_.find(std::string(rhs));
  if (nit == detuple_named_layout_.end() || nit->second.empty()) {
    return;
  }
  if (!std::all_of(nit->second.begin(), nit->second.end(), [](const auto& field) { return field.type.valid(); })) {
    return;
  }
  detuple_registry_->named_types.insert_or_assign(detuple_registry_key(name), nit->second);
}

std::string uPass_runner::detuple_registry_key(std::string_view type_name) const {
  // The registry is shared by every runner of one pass.upass invocation, and
  // ALL file wrappers run before any extracted/streamed function body. Keyed
  // by the bare type name, a later file's same-named `type T = (…)` silently
  // overwrote an earlier file's layout before that file's functions consumed
  // it (reproduced: an 8-bit field truncated to another file's 2-bit layout).
  // Types are file-scoped, so qualify by the owning source unit: a function
  // body "file.entity[...]" shares its file wrapper's prefix.
  const auto  unit = lm->get_lnast()->get_top_module_name();
  const auto  dot  = unit.find('.');
  std::string key(unit.substr(0, dot));
  key.push_back('\n');  // '\n' cannot appear in a type identifier
  key.append(type_name);
  return key;
}

void uPass_runner::detuple_commit_pending_split(const Detuple_pending_decl& pending) {
  Detuple_split split{.fields = pending.fields, .mode = pending.mode, .memory = false};
  split.whole_bound = pending.init_ref.has_value();  // the declaration's own initializer is the construction
  detuple_splits_.insert_or_assign(pending.name, split);

  // The initializer's SHAPE must match the declared layout. Extra entries used
  // to be silently discarded (a positional `(20, 3, 7)` reset for a two-field
  // reg dropped the 7 from the netlist); the deleted whole-tree pass refused
  // the split on any arity mismatch, keeping the aggregate error loud. A
  // nested field takes its leaves from the nested literal.
  std::vector<std::optional<Lnast_node>> inits(split.fields.size());
  if (pending.init_ref) {
    if (const auto it = detuple_tuple_values_.find(*pending.init_ref); it != detuple_tuple_values_.end()) {
      if (!detuple_project_init(pending.name, it->second, split.fields, inits)) {
        return;  // reported
      }
    }
  }
  const bool is_reg = pending.mode == "reg" || pending.mode.starts_with("reg ");
  if (is_reg) {
    // A register's reset value must fit each leaf's declared range, like a
    // scalar `reg r:u4 = 20` (the reset was silently truncated).
    for (std::size_t i = 0; i < split.fields.size(); ++i) {
      if (inits[i] && !detuple_validate_scalar_store(pending.name + "." + split.fields[i].name, split.fields[i].type, *inits[i])) {
        return;  // reported
      }
    }
  }
  for (std::size_t i = 0; i < split.fields.size(); ++i) {
    const auto&                      field = split.fields[i];
    const std::optional<Lnast_node>& init  = inits[i];
    const auto                       leaf  = pending.name + "." + field.name;
    detuple_emit_declare(leaf, field.type, pending.mode, is_reg && init ? &*init : nullptr);
    // A mut tuple's initializer is an ordinary scalar assignment, not a
    // register reset. Running it through the shared pass dispatch creates
    // the field entry and attaches the declared type/range facts before
    // later writes are checked. Wire nil seeds remain placeholders and are
    // intentionally not emitted; their real field drivers follow.
    if (!is_reg && init && !(pending.mode == "wire" || pending.mode.starts_with("wire "))) {
      detuple_emit_store(leaf, {*init});
    }
  }
}

bool uPass_runner::try_detuple_store() {
  if (detuple_registry_ == nullptr || detuple_synthetic_ || !lm->has_child()) {
    return false;
  }
  if (!detuple_pending_decl_ && detuple_splits_.empty()) {
    return false;  // nothing this store could bind to — skip the operand walk
  }
  const auto& ln   = lm->get_lnast();
  const auto  node = lm->get_current_nid();
  auto        lhs  = ln->get_first_child(node);
  if (lhs.is_invalid() || !Lnast_ntype::is_ref(ln->get_type(lhs))) {
    return false;
  }
  // SSA re-versions the AGGREGATE name on a second whole-tuple write
  // (`control` -> `control___ssa_1`). Without mapping it back to the base split
  // the second whole-tuple assignment missed detuple_splits_ entirely and
  // returned false, so the SHAPE CHECK below never ran: `control = (op=…,
  // valid=…, extra=…)` silently accepted the unknown field -- and equally
  // silently accepted a MISSING one -- while the byte-identical program at FILE
  // scope, where nothing re-versions, reported both correctly.
  std::string             var = detuple_split_name(detuple_text(lhs));
  std::vector<Lnast_node> rest;
  for (auto child = ln->get_sibling_next(lhs); child.is_valid(); child = ln->get_sibling_next(child)) {
    if (Lnast_ntype::is_ref(ln->get_type(child))) {
      rest.emplace_back(Lnast_node::create_ref(detuple_text(child)));
    } else if (Lnast_ntype::is_const(ln->get_type(child))) {
      rest.emplace_back(Lnast_node::create_const(detuple_text(child)));
    } else {
      return false;
    }
  }

  if (detuple_pending_decl_ && var == detuple_pending_decl_->name) {
    // A store of the SHAPE temp (typed form) or of the INIT bundle temp
    // (untyped `reg V = (x=…, y=…)`, whose fields were seeded from the named
    // const literal) commits the split; either way the store is the bind, not
    // a body write.
    const bool binds_shape = detuple_pending_decl_->shape_tmp && rest.size() == 1 && rest[0].is_ref()
                             && rest[0].get_name() == *detuple_pending_decl_->shape_tmp;
    const bool binds_init = !detuple_pending_decl_->shape_tmp && detuple_pending_decl_->init_ref && rest.size() == 1
                            && rest[0].is_ref() && rest[0].get_name() == *detuple_pending_decl_->init_ref;
    if ((binds_shape || binds_init) && !detuple_pending_decl_->fields.empty()) {
      auto pending = std::move(*detuple_pending_decl_);
      detuple_pending_decl_.reset();
      detuple_commit_pending_split(pending);
      return true;  // shape bind is consumed by the detupler
    }
    detuple_flush_pending_decl();
  }

  const auto split_it = detuple_splits_.find(var);
  if (split_it == detuple_splits_.end()) {
    return false;
  }
  const auto& split        = split_it->second;
  auto        field_exists = [&](std::string_view name) {
    return std::any_of(split.fields.begin(), split.fields.end(), [&](const auto& field) { return field.name == name; });
  };

  if (rest.size() == 1 && rest[0].is_const()
      && (rest[0].get_name() == "nil" || rest[0].get_name() == "0sb?" || rest[0].get_name() == "0ub?")) {
    if (!(split.mode == "wire" || split.mode.starts_with("wire "))) {
      for (const auto& field : split.fields) {
        detuple_emit_store(var + "." + field.name, {rest[0]});
      }
    }
    return true;
  }

  // 2f-nested_type — a write to a NESTED field arrives with one const key PER
  // LEVEL (`ctl.ex.aa = a` -> `store(ctl, 'ex', 'aa', a)`), while the split's
  // field names are the JOINED dotted leaf paths (`ex.aa`) that the dotted leaf
  // type_specs established. Join the leading const keys and try the FULL path
  // first: the single-key scan below matches on any ONE key, so a split holding
  // both a top-level `aa` and a nested `ex.aa` (`reg ctl:(aa:u1, ex:(aa:u1))`)
  // let `ctl.ex.aa = x` match the flat `aa` and silently write the wrong leaf.
  // The longest path is the only unambiguous reading, so it has to win.
  if (rest.size() >= 3) {
    std::string joined;
    bool        all_const = true;
    for (int i = 0; i + 1 < static_cast<int>(rest.size()); ++i) {
      if (!rest[i].is_const()) {
        all_const = false;
        break;
      }
      if (!joined.empty()) {
        joined += ".";
      }
      joined += std::string(rest[i].get_name());
    }
    if (all_const && !joined.empty() && field_exists(joined)) {
      const auto leaf_name = var + "." + joined;
      const auto field_it = std::find_if(split.fields.begin(), split.fields.end(), [&](const auto& f) { return f.name == joined; });
      if (field_it != split.fields.end()) {
        (void)detuple_validate_scalar_store(leaf_name, field_it->type, rest.back());
      }
      detuple_emit_store(leaf_name, {rest.back()});
      return true;
    }
  }

  int field_pos = -1;
  for (int i = 0; i + 1 < static_cast<int>(rest.size()); ++i) {
    if (rest[i].is_const() && field_exists(rest[i].get_name())) {
      field_pos = i;
      break;
    }
  }
  if (field_pos >= 0 && field_pos == static_cast<int>(rest.size()) - 2) {
    const auto field_it  = std::find_if(split.fields.begin(), split.fields.end(), [&](const auto& field) {
      return field.name == rest[field_pos].get_name();
    });
    const auto leaf_name = var + "." + std::string(rest[field_pos].get_name());
    if (field_it != split.fields.end()) {
      (void)detuple_validate_scalar_store(leaf_name, field_it->type, rest.back());
    }
    std::vector<Lnast_node> scalar_operands;
    scalar_operands.reserve(rest.size() - 1);
    scalar_operands.insert(scalar_operands.end(), rest.begin(), rest.begin() + field_pos);
    scalar_operands.push_back(rest.back());
    detuple_emit_store(leaf_name, scalar_operands);
    return true;
  }

  if (rest.size() == 1 && rest[0].is_ref()) {
    const std::string rhs = detuple_split_name(rest[0].get_name());
    if (const auto values = detuple_tuple_values_.find(rhs); values != detuple_tuple_values_.end()) {
      // Reject a SHAPE mismatch before emitting anything: extra positional
      // entries were silently discarded (netlist-proven truncation), and an
      // unknown named field was silently ignored.
      if (!values->second.named && values->second.positional.size() != split.fields.size()) {
        detuple_error("tuple-assignment-shape",
                      std::format("tuple assignment to `{}` has {} entries but the tuple has {} fields",
                                  var,
                                  values->second.positional.size(),
                                  split.fields.size()));
        return true;
      }
      // 2f-nested_type — the destination's field names are dotted LEAF paths
      // (`ex.aa`), so a NESTED right-hand side has to be flattened the same way
      // before it can be matched. A flat RHS flattens to itself, so the
      // ordinary case is unchanged.
      std::vector<std::pair<std::string, Lnast_node>> rhs_fields;
      detuple_flatten_tuple_value(rhs, "", rhs_fields);
      // A single-output lambda's result auto-unwraps in scalar context
      // (06-functions.md): `helper(...)` whose one output `io_data` IS the
      // tuple binds `io_data`'s CONTENTS, not a field called `io_data`. Peel
      // that wrapper when the destination has no such field.
      // ONE level only: `detuple_flatten_tuple_value` already descends, so a
      // second pass would re-derive from the same `values->second.fields`
      // front and produce the identical list. (The `for (peel < 8)` this
      // replaces always hit its trailing `break`, so behavior is unchanged.)
      if (values->second.fields.size() == 1 && !rhs_fields.empty() && !field_exists(rhs_fields.front().first)) {
        const auto& only = values->second.fields.front();
        if (only.second.is_ref() && detuple_tuple_values_.contains(std::string(only.second.get_name()))) {
          std::vector<std::pair<std::string, Lnast_node>> peeled;
          detuple_flatten_tuple_value(std::string(only.second.get_name()), "", peeled);
          if (!peeled.empty()) {
            rhs_fields = std::move(peeled);
          }
        }
      }
      for (const auto& [key, node_value] : rhs_fields) {
        (void)node_value;
        if (!field_exists(key)) {
          detuple_error("tuple-assignment-shape", std::format("tuple assignment to `{}` names unknown field `{}`", var, key));
          return true;
        }
      }
      // The first whole-tuple store of a flat typed tuple is its CONSTRUCTION:
      // the unnamed values bind like call arguments (qa.md Q35).
      const bool constructing      = !split_it->second.whole_bound;
      split_it->second.whole_bound = true;
      if (constructing && !values->second.positional.empty() && detuple_layout_is_flat(split.fields)
          && !(values->second.named && values->second.positional.size() + values->second.fields.size() > split.fields.size())) {
        std::vector<std::optional<Lnast_node>> bound(split.fields.size());
        for (const auto& [key, node_value] : rhs_fields) {
          for (std::size_t i = 0; i < split.fields.size(); ++i) {
            if (split.fields[i].name == key) {
              bound[i] = node_value;
            }
          }
        }
        if (!detuple_bind_unnamed_values(var, {}, values->second, split.fields, bound)) {
          return true;  // reported
        }
        for (std::size_t i = 0; i < split.fields.size(); ++i) {
          if (!bound[i]) {
            detuple_error("tuple-assignment-shape",
                          std::format("tuple assignment to `{}` does not provide field `{}`", var, split.fields[i].name));
            return true;
          }
          const auto leaf_name = var + "." + split.fields[i].name;
          (void)detuple_validate_scalar_store(leaf_name, split.fields[i].type, *bound[i]);
          detuple_emit_store(leaf_name, {*bound[i]});
        }
        return true;
      }
      for (std::size_t i = 0; i < split.fields.size(); ++i) {
        const auto&               field = split.fields[i];
        std::optional<Lnast_node> value;
        if (values->second.named) {
          for (const auto& [key, node_value] : rhs_fields) {
            if (key == field.name) {
              value = node_value;
              break;
            }
          }
        } else if (i < values->second.positional.size()) {
          value = values->second.positional[i];
        }
        if (!value) {
          detuple_error("tuple-assignment-shape",
                        std::format("tuple assignment to `{}` does not provide field `{}`", var, field.name));
          return true;
        }
        const auto leaf_name = var + "." + field.name;
        (void)detuple_validate_scalar_store(leaf_name, field.type, *value);
        detuple_emit_store(leaf_name, {*value});
      }
      return true;
    }
    if (const auto rhs_split = detuple_splits_.find(rhs); rhs_split != detuple_splits_.end()) {
      if (rhs == var && rest[0].get_name() != detuple_text(lhs)) {
        // An SSA carry between two versions of the same aggregate
        // (`p = p___ssa_1`): both name the same leaves, which the versioned
        // write already wrote. Not a user self-assignment (`p = p` still is).
        return true;
      }
      for (const auto& field : split.fields) {
        if (!std::any_of(rhs_split->second.fields.begin(), rhs_split->second.fields.end(), [&](const auto& rhs_field) {
              return rhs_field.name == field.name;
            })) {
          detuple_error("tuple-assignment-shape",
                        std::format("tuple `{}` has no field `{}` required by `{}`", rhs, field.name, var));
          return true;
        }
        detuple_emit_store(var + "." + field.name, {Lnast_node::create_ref(rhs + "." + field.name)});
      }
      return true;
    }
    // A composite input port is represented only by ABI leaves. Copy those
    // leaves directly when the whole port is assigned to typed tuple storage.
    if (std::all_of(split.fields.begin(), split.fields.end(), [&](const auto& field) {
          return stream_port_in_leaf_.contains(rhs + "." + field.name);
        })) {
      for (const auto& field : split.fields) {
        detuple_emit_store(var + "." + field.name, {Lnast_node::create_ref(rhs + "." + field.name)});
      }
      return true;
    }
  }

  // A SCALAR assigned to a typed tuple with ONE field (`type Tone = (mut
  // val:Signed = 0)`, `mut t:Tone = 5`) names no field: the single-field
  // exception of the call naming rules (06-functions.md "Argument naming")
  // binds it to that field. A function reference stays an error (below).
  if (rest.size() == 1 && split.fields.size() == 1 && split.fields.front().name.find('.') == std::string::npos
      && (rest[0].is_const() || (rest[0].is_ref() && !lookup_callee(rest[0].get_name())))) {
    const auto& only      = split.fields.front();
    const auto  leaf_name = var + "." + only.name;
    (void)detuple_validate_scalar_store(leaf_name, only.type, rest[0]);
    detuple_emit_store(leaf_name, {rest[0]});
    return true;
  }

  // A bare FUNCTION REFERENCE assigned to a typed tuple (`mut y:T = my_init`)
  // is not a tuple operation at all, so do not bury it under a generic
  // scalarization failure. The runner's own `ctor-func-ref` check cannot reach
  // it any more: once the type is split, `try_typename` finds no tuple and the
  // check reads the store as an untyped (legal) function-value alias. Report
  // the same thing here. (errors/mix_tup.prp)
  if (rest.size() == 1 && rest[0].is_ref() && lookup_callee(rest[0].get_name())) {
    detuple_error("ctor-func-ref",
                  std::format("cannot assign function reference `{}` to typed variable `{}`", rest[0].get_name(), var),
                  "call it (`x = T(...)`) or give the type an `init` method; a bare function reference may only bind to an "
                  "untyped variable");
    return true;
  }
  detuple_error("unsupported-whole-tuple",
                std::format("whole-tuple operation on `{}` cannot be scalarized", var),
                "select or assign scalar fields explicitly");
  return true;
}

bool uPass_runner::try_detuple_tuple_get() {
  if (detuple_registry_ == nullptr || detuple_synthetic_ || !lm->has_child()) {
    return false;
  }
  const auto& ln   = lm->get_lnast();
  const auto  node = lm->get_current_nid();
  auto        dst  = ln->get_first_child(node);
  auto        src  = dst.is_invalid() ? dst : ln->get_sibling_next(dst);
  auto        key  = src.is_invalid() ? src : ln->get_sibling_next(src);
  if (dst.is_invalid() || src.is_invalid() || key.is_invalid() || !Lnast_ntype::is_ref(ln->get_type(dst))
      || !Lnast_ntype::is_ref(ln->get_type(src))
      || (!Lnast_ntype::is_ref(ln->get_type(key)) && !Lnast_ntype::is_const(ln->get_type(key)))) {
    return false;
  }
  // 2f-nested_type — a read of a NESTED field arrives with one const key PER
  // LEVEL (`ctl.ex.aa` -> `tuple_get(%t, ctl, 'ex', 'aa')`), while the split's
  // field names are the JOINED dotted leaf paths. Join the extra const keys so
  // the leaf is found; anything else with a trailing key (a runtime index, a
  // mixed ref/const chain) still declines, exactly as before.
  std::string joined_key;
  if (const auto extra = ln->get_sibling_next(key); extra.is_valid()) {
    if (!Lnast_ntype::is_const(ln->get_type(key))) {
      return false;
    }
    joined_key = detuple_text(key);
    for (auto k = extra; k.is_valid(); k = ln->get_sibling_next(k)) {
      if (!Lnast_ntype::is_const(ln->get_type(k))) {
        return false;
      }
      joined_key += ".";
      joined_key += detuple_text(k);
    }
  }
  const std::string dst_name{detuple_text(dst)};
  const std::string src_name = detuple_split_name(detuple_text(src));
  const Lnast_node  key_node = !joined_key.empty()                      ? Lnast_node::create_const(joined_key)
                               : Lnast_ntype::is_ref(ln->get_type(key)) ? Lnast_node::create_ref(detuple_text(key))
                                                                        : Lnast_node::create_const(detuple_text(key));

  // 2f-defaulted_tuple — `tuple_get %t = %T.'a'` off a DEFAULTED tuple type
  // literal is prp2lnast's handle for stamping that field's type. Remember
  // which (tuple, field) `%t` projects so the `type_spec %t : <T>` that follows
  // can fill the layout in. Record only; the node still lowers normally.
  if (key_node.is_const() && detuple_named_layout_.contains(src_name)) {
    detuple_field_alias_.insert_or_assign(dst_name, Detuple_field_alias{src_name, std::string(key_node.get_name())});
  }

  if (const auto split_it = detuple_splits_.find(src_name); split_it != detuple_splits_.end()) {
    const auto& split = split_it->second;
    if (key_node.is_const()) {
      const auto field = std::string(key_node.get_name());
      if (std::any_of(split.fields.begin(), split.fields.end(), [&](const auto& f) { return f.name == field; })) {
        // For a split MEMORY this aliases dst to the whole per-field ARRAY
        // (`mem.field`); a later element read must go through the ordinary
        // memory machinery. Same emission either way today — kept as one
        // statement (the two arms used to be byte-identical copies).
        detuple_emit_store(dst_name, {Lnast_node::create_ref(src_name + "." + field)});
        return true;
      }
    }
    if (!split.memory && key_node.is_const()) {
      const std::string                               prefix = std::string(key_node.get_name()) + ".";
      std::vector<std::pair<std::string, Lnast_node>> fields;
      for (const auto& field : split.fields) {
        if (field.name.starts_with(prefix)) {
          fields.emplace_back(field.name.substr(prefix.size()), Lnast_node::create_ref(src_name + "." + field.name));
        }
      }
      if (!fields.empty()) {
        emit_inline_tuple(dst_name, fields);
        return true;
      }
    }
    if (split.memory) {
      detuple_index_aliases_.insert_or_assign(dst_name, Detuple_index_alias{src_name, key_node});
      return true;  // virtual mem[index] aggregate; the field pick consumes it
    }
    detuple_error("tuple-field", std::format("`{}` has no tuple field `{}`", src_name, key_node.get_name()));
    return true;
  }

  if (const auto alias_it = detuple_index_aliases_.find(src_name); alias_it != detuple_index_aliases_.end()) {
    if (!key_node.is_const()) {
      detuple_error("dynamic-tuple-field",
                    std::format("tuple-memory field selection on `{}` must be static", alias_it->second.memory));
      return true;
    }
    const auto split_it = detuple_splits_.find(alias_it->second.memory);
    if (split_it == detuple_splits_.end()
        || !std::any_of(split_it->second.fields.begin(), split_it->second.fields.end(), [&](const auto& f) {
             return f.name == key_node.get_name();
           })) {
      detuple_error("tuple-field",
                    std::format("tuple memory `{}` has no field `{}`", alias_it->second.memory, key_node.get_name()));
      return true;
    }
    detuple_synthetic_ = true;
    emit_inline_op(
        Lnast_ntype::create_tuple_get(),
        dst_name,
        {Lnast_node::create_ref(alias_it->second.memory + "." + std::string(key_node.get_name())), alias_it->second.index});
    detuple_synthetic_ = false;
    detuple_index_aliases_.erase(alias_it);
    return true;
  }
  return false;
}

bool uPass_runner::try_detuple_typespec() {
  if (detuple_registry_ == nullptr || detuple_synthetic_ || !lm->has_child()) {
    return false;
  }
  const auto& ln   = lm->get_lnast();
  const auto  name = ln->get_first_child(lm->get_current_nid());
  if (name.is_invalid() || !Lnast_ntype::is_ref(ln->get_type(name))) {
    return false;
  }
  const auto text          = detuple_text(name);
  const auto dot           = text.find('.');
  // 2f-defaulted_tuple — a DOTLESS target is normally not a field, but it is
  // exactly the shape prp2lnast uses to type a defaulted tuple field: the
  // `tuple_get` above aliased this temp to (tuple literal, field), so decode
  // the type and fill the pending layout in. Everything else still bails.
  const bool aliased_field = dot == std::string_view::npos && detuple_field_alias_.contains(text);
  if (dot == std::string_view::npos && !aliased_field) {
    return false;
  }
  const std::string root(aliased_field ? std::string{} : std::string(text.substr(0, dot)));
  if (!aliased_field && detuple_splits_.contains(root)) {
    return true;  // folded into the synthesized scalar leaf declaration
  }

  auto type = ln->get_sibling_next(name);
  if (type.is_invalid()) {
    return false;
  }
  uPass_detuple_registry::Scalar_type scalar;
  if (Lnast_ntype::is_prim_type_bool(ln->get_type(type))) {
    scalar.kind = upass::Kind::boolean;
  } else if (Lnast_ntype::is_prim_type_int(ln->get_type(type))) {
    scalar.kind      = upass::Kind::integer;
    auto max         = ln->get_first_child(type);
    auto min         = max.is_invalid() ? max : ln->get_sibling_next(max);
    auto parse_bound = [&](Lnast_nid bound, Dlop& out) {
      if (bound.is_invalid() || !Lnast_ntype::is_const(ln->get_type(bound)) || detuple_text(bound) == "nil") {
        return;
      }
      if (auto value = Dlop::from_pyrope(detuple_text(bound)); value && value->is_integer()) {
        out = *value;
      }
    };
    parse_bound(max, scalar.max);
    parse_bound(min, scalar.min);
  }
  if (!scalar.valid()) {
    const auto binding = aliased_field ? nullptr : symbol_table_.get_bundle(root);
    if (binding && binding->get_mode() == upass::Mode::type_kind) {
      // A field typed by another NAMED tuple type (`type Outer = (inner:Inner,
      // tag:u2)`): once `Inner` published its layout, the field contributes
      // Inner's leaves under its own name (`inner.d`, `inner.v`).
      const auto field = std::string(text.substr(dot + 1));
      if (Lnast_ntype::is_ref(ln->get_type(type))) {
        if (const auto nested = detuple_registry_->named_types.find(detuple_registry_key(detuple_text(type)));
            nested != detuple_registry_->named_types.end()) {
          for (const auto& leaf : nested->second) {
            detuple_predecl_fields_[root].push_back({field + "." + leaf.name, leaf.type});
          }
          return false;
        }
      }
      // Keep unsupported leaves in a named type's shape so a sibling scalar
      // cannot make a partially decoded nested field look complete.
      detuple_predecl_fields_[root].push_back({field, scalar});
    }
    return false;
  }
  if (aliased_field) {
    const auto alias = detuple_field_alias_.at(text);
    if (auto lit = detuple_named_layout_.find(alias.tuple_tmp); lit != detuple_named_layout_.end()) {
      for (auto& field : lit->second) {
        if (field.name == alias.field) {
          field.type = std::move(scalar);
          break;
        }
      }
    }
    return false;  // record only: the type_spec itself still lowers normally
  }
  const auto field_name = std::string(text.substr(dot + 1));
  auto*      fields_ptr = &detuple_predecl_fields_[root];
  if (detuple_pending_decl_ && detuple_pending_decl_->name == root) {
    fields_ptr = &detuple_pending_decl_->fields;
  }
  auto&      fields   = *fields_ptr;
  const auto existing = std::find_if(fields.begin(), fields.end(), [&](const auto& field) { return field.name == field_name; });
  if (existing == fields.end()) {
    fields.push_back({field_name, std::move(scalar)});
  } else {
    existing->type = std::move(scalar);
  }
  // A pending aggregate declaration owns these facts. They will be reproduced
  // on the scalar leaf declarations, so no later pass should see the aggregate
  // type_spec. With no pending declaration, retain the fact for either an
  // upcoming Slang declaration or the ordinary type machinery.
  return detuple_pending_decl_ && detuple_pending_decl_->name == root;
}

// ── Node dispatch ─────────────────────────────────────────────────────────────

void uPass_runner::process_lnast() {
  using Ntype = Lnast_ntype;

  // An ambiguous hardware declaration is held only across its immediately
  // following parser-generated field-shape cluster. Encountering any other
  // statement proves it was an ordinary scalar/array declaration, so replay it
  // now before processing the current statement.
  detuple_flush_pending_before_current();

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
      emit_current_leaf();
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
      if (lm->current_num_children() > 2 && (try_lower_array_index() || try_open_fill_access())) {
        break;  // an element write, re-issued with zero-based indices
      }
      if (try_instance_handle_store()) {
        break;
      }
      if (try_detuple_store()) {
        break;
      }
      if (try_stream_tuple_port_alias_store()) {
        break;
      }
      // `c = concat(...)`: the destination's declared width must equal the lane
      // sum exactly. Checked at the bind, not at the concat, because the concat
      // node's own dst is always a compiler temp.
      if (lm->current_num_children() == 2 && lm->has_child()) {
        const auto sv = lm->save_cursor();
        lm->move_to_child();
        const std::string dname(lm->current_text());
        if (lm->move_to_sibling()) {
          const std::string vname(lm->current_text());
          lm->restore_cursor(sv);
          check_concat_dest(dname, vname);
        } else {
          lm->restore_cursor(sv);
        }
      }
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
          if (!self_dst.empty() && lm->move_to_sibling() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
            rhs_name   = lm->current_text();
            is_self    = rhs_name == self_dst;
          }
          lm->restore_cursor(here);
          // Function values may be bound just like a method/import lambda
          // reference. Materialize the registry identity before constprop
          // copies it to an alias (`const g = f`).
          const auto unit_name = lm->get_lnast()->get_graph_name();
          const auto entity_name = unit_name.substr(unit_name.rfind('.') + 1);
          const bool simulation_body = !entity_name.empty() && entity_name.front() == '%';
          // A Sub-instance handle is bound through a tmp (`const child = %child_0`).
          // Carry its stashed declared port facts over to the user-visible name,
          // or `child.flag` still has no declared width.
          //
          // Both maps are MUTATED while their own entries are read, so nothing may
          // hold an iterator/reference across an insert: absl rehashes on growth,
          // and `insert_or_assign(nkey, pit->second)` would then copy from a
          // destroyed slot. Snapshot the key list, copy each fact by VALUE, and
          // skip the `x = x` self-bind (diagnosed a few lines below) that would
          // otherwise push into the very vector being walked.
          if (!rhs_name.empty() && !self_dst.empty() && rhs_name != self_dst) {
            std::vector<std::string> pending_keys;
            if (const auto rit = symbol_table_.pending_keys_by_root.find(rhs_name);
                rit != symbol_table_.pending_keys_by_root.end()) {
              pending_keys = rit->second;
            }
            for (const auto& key : pending_keys) {
              const auto pit = symbol_table_.pending_decl_facts.find(key);
              if (pit == symbol_table_.pending_decl_facts.end()) {
                continue;
              }
              const Symbol_table::Pending_decl pd = pit->second;  // by value: the insert below can rehash
              const auto                       field = key.substr(rhs_name.size());  // ".<port>"
              auto                             nkey  = absl::StrCat(self_dst, field);
              symbol_table_.pending_decl_facts.insert_or_assign(nkey, pd);
              symbol_table_.pending_keys_by_root[self_dst].push_back(std::move(nkey));
            }
            if (const auto sit = symbol_table_.sub_output_ranges.find(rhs_name); sit != symbol_table_.sub_output_ranges.end()) {
              auto ports = sit->second;  // by value: the insert below can rehash
              symbol_table_.sub_output_ranges.insert_or_assign(self_dst, std::move(ports));
            }
            if (const auto so = symbol_table_.single_output_port.find(rhs_name); so != symbol_table_.single_output_port.end()) {
              auto port = so->second;  // by value: the insert below can rehash
              symbol_table_.single_output_port.insert_or_assign(self_dst, std::move(port));
            }
            if (const auto oo = symbol_table_.opaque_sub_outputs.find(rhs_name); oo != symbol_table_.opaque_sub_outputs.end()) {
              auto ports = oo->second;  // by value: the insert below can rehash
              symbol_table_.opaque_sub_outputs.insert_or_assign(self_dst, std::move(ports));
            }
            if (const auto on = symbol_table_.sub_output_names.find(rhs_name); on != symbol_table_.sub_output_names.end()) {
              auto names = on->second;  // by value: the insert below can rehash
              symbol_table_.sub_output_names.insert_or_assign(self_dst, std::move(names));
            }
          }
          // An io port is a value, never a function, even when some comb
          // elsewhere shares its name (`comb m(sel:U8)` beside a nested `sel`).
          if (!simulation_body && !rhs_name.empty() && !symbol_table_.has_bundle(rhs_name)
              && lm->unit_lnast()->io_meta().find(rhs_name) == nullptr) {
            if (const auto function = lookup_callee(rhs_name); function && function->get_lambda_kind() == "comb") {
              symbol_table_.set(rhs_name, *Dlop::from_string(function->get_top_module_name()));
            }
          }
          if (simulation_body && !rhs_name.empty() && !self_dst.empty() && rhs_name != self_dst) {
            bind_instance_handle(self_dst, rhs_name);
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
        track_open_fill_array();
        // init constructor hook: the DECLARATION store of a typed var whose
        // type carries `init` (or whose value is a ref-self mod/comb) becomes
        // a defaults-bind + spliced constructor call instead of a structural
        // assign. Re-assignment stores never construct (init runs once).
        if (!try_init_construction()) {
          // Registered only on the path that emits through emit_op_with_fold:
          // a construction-consumed store never emits its versioned LHS, so
          // noting it first would advance stream_ssa_current_ to a phantom
          // definition every later read would dangle on.
          note_stream_ssa_definition();
          const auto prior = runtime_tuple_prior_fields();
          const auto fill  = array_init_prefill();
          process_drop_candidate_push(&upass::uPass::process_store, /*fold_all=*/false);
          split_runtime_tuple_store(prior);
          if (fill) {
            emit_array_prefill(*fill);
          }
        }
        // A file-level `type T=(a:A,b:B)` has now populated T's real
        // symbol-table bundle. Publish only its compact scalar layout for the
        // extracted functions that follow; no tree is revisited.
        if (lm->current_num_children() == 2 && lm->has_child()) {
          const auto saved = lm->save_cursor();
          lm->move_to_child();
          std::string type_name;
          std::string rhs_name;
          if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
            type_name = std::string(lm->current_text());
          }
          if (lm->move_to_sibling() && Lnast_ntype::is_ref(lm->get_raw_ntype())) {
            rhs_name = std::string(lm->current_text());
          }
          lm->restore_cursor(saved);
          if (!type_name.empty() && !rhs_name.empty()) {
            detuple_publish_named_type(type_name, rhs_name);
            inherit_alias_type(type_name, rhs_name);  // `const x = a` (ruling 38)
          }
        }
      } else if (!try_split_leaf_field_store() && !try_stream_tuple_port_store()) {
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
    case Ntype::Lnast_ntype_declare: {
      // An index-range / enum array dimension declares its plain extent.
      const Index_dims_scope index_dims(*this);
      if (!open_fill_arrays_.empty() && lm->has_child()) {
        const auto here = lm->save_cursor();
        lm->move_to_child();
        open_fill_arrays_.erase(std::string(lm->current_text()));  // a new binding of the name
        lm->restore_cursor(here);
      }
      if (try_detuple_declare()) {
        break;
      }
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
      // A reg/wire declare's stores are never symbolically bound, so
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
    }

    // Bitwidth. The bitwise ops also track which results are unsigned-typed,
    // and `~` picks its typed form from that (user ruling 26).
    case Ntype::Lnast_ntype_bit_and: dispatch_bitwise(PUSH_FN(bit_and)); break;
    case Ntype::Lnast_ntype_bit_or : dispatch_bitwise(PUSH_FN(bit_or)); break;
    case Ntype::Lnast_ntype_bit_xor: dispatch_bitwise(PUSH_FN(bit_xor)); break;
    case Ntype::Lnast_ntype_bit_not: dispatch_bit_not(); break;

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
    case Ntype::Lnast_ntype_set_mask: process_bit_update(); break;
    case Ntype::Lnast_ntype_get_mask: process_bit_selection(); break;
    A_OP(concat)

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
    case Ntype::Lnast_ntype_func_call: {
      const auto cast = typed_cast_result();  // read before the call lowers or folds
      if (!try_lower_tuple_spread() && !try_lower_wrap_sat() && !try_lower_typecast() && !try_inline_func_call() && !try_construct_call()) {
        process_drop_candidate(&upass::uPass::process_func_call, /*fold_all=*/false);
        note_opaque_output_read();  // a Sub call: its one output may be opaque
      }
      if (cast) {
        typed_expr_types_[cast->first] = cast->second;
      }
      break;
    }
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
        // A `break` under a RUNTIME condition cannot be resolved by unrolling:
        // process_lnast walks an unknown-condition arm like any other, so
        // reaching the break here would set loop_break_hit_ unconditionally and
        // abandon the rest of THIS iteration plus every later one — silently
        // compiling `for i in 0..<8 { if runtime {break}; acc += 1 }` to
        // `acc == 0`. Refuse instead of miscompiling. The rolled form lowers
        // this to the per-replica activation chain (`next_active`, see
        // todo_loop_cond_sub.md rule 13); until then it is unsupported.
        if (symbol_table_.uncertain_scope_count() > loop_uncertain_base_) {
          loop_fail(lm->get_lnast()->span_of(lm->get_current_nid()),
                    "unsupported",
                    "loop-runtime-break",
                    "`break` under a runtime condition inside a comptime loop is not supported",
                    "make the break condition comptime, or restructure the loop so the exit is not data dependent");
        }
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
        // Same miscompile as the runtime `break` above, and worse: the flag is
        // re-armed every iteration, so `for i in 0..<8 { if runtime {continue};
        // acc += 1 }` abandons the rest of EVERY iteration and compiles to
        // `acc == 0` — including the inputs where no `continue` is taken.
        // Refuse until the rolled form's activation chain (`next_active`, see
        // todo_loop_cond_sub.md rule 13) can express it.
        if (symbol_table_.uncertain_scope_count() > loop_uncertain_base_) {
          loop_fail(lm->get_lnast()->span_of(lm->get_current_nid()),
                    "unsupported",
                    "loop-runtime-continue",
                    "`continue` under a runtime condition inside a comptime loop is not supported",
                    "make the continue condition comptime, or restructure the loop so the skip is not data dependent");
        }
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
    case Ntype::Lnast_ntype_tuple_get: {
      if (try_lower_array_index() || try_open_fill_access()) {
        break;  // re-issued with zero-based indices, or an entry of an inferred array no write reached
      }
      const auto [elem_dst, elem_bits] = array_elem_read_bits();  // before the node folds away
      if (!try_detuple_tuple_get() && !try_resolve_tuple_get()) {
        process_drop_candidate(&upass::uPass::process_tuple_get, /*fold_all=*/false);
        note_opaque_output_read();
      }
      if (elem_bits != 0) {
        typed_expr_types_[elem_dst] = Int_type{.bits = elem_bits};
      } else if (!elem_dst.empty()) {
        typed_expr_types_.erase(elem_dst);
      }
      break;
    }
    // tuple_add is A_OP-shaped plus a runner post-step: after the dispatch
    // (constprop rebuilt dst's bundle + slot→ref map) backfill the runtime
    // slot carriers constprop drops for local/temp scalar field values — see
    // record_runtime_tuple_slot_refs. Must run AFTER the dispatch (constprop
    // erases + rebuilds tuple_slot_ref[dst] wholesale).
    case Ntype::Lnast_ntype_tuple_add:
      if (!try_detuple_tuple_add()) {
        process_drop_candidate_push(PUSH_FN(tuple_add), /*fold_all=*/false);
        record_runtime_tuple_slot_refs();
      }
      break;
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
    case Ntype::Lnast_ntype_attr_set: {
      bool split_attribute = false;
      if (!detuple_synthetic_ && lm->has_child()) {
        const auto& ln = lm->get_lnast();
        const auto target = ln->get_first_child(lm->get_current_nid());
        const auto key = ln->get_sibling_next(target);
        const auto value = key.is_invalid() ? key : ln->get_sibling_next(key);
        if (ln->get_type(target) == Ntype::Lnast_ntype_ref && key.is_valid() && value.is_valid()
            && Lnast_ntype::is_const(ln->get_type(key))) {
          if (const auto it = detuple_splits_.find(std::string(detuple_text(target))); it != detuple_splits_.end()) {
            const auto fields = it->second.fields;
            const std::string root{detuple_text(target)};
            const std::string attr{detuple_text(key)};
            const auto val = Lnast_ntype::is_ref(ln->get_type(value)) ? Lnast_node::create_ref(detuple_text(value))
                                                                   : Lnast_node::create_const(detuple_text(value));
            // The aggregate's typename describes its shape, not a leaf type.
            if (attr != "typename") {
              detuple_synthetic_ = true;
              for (const auto& field : fields) {
                emit_inline_op(Lnast_ntype::create_attr_set(), root + "." + field.name,
                               {Lnast_node::create_const(attr), val});
              }
              detuple_synthetic_ = false;
            }
            split_attribute = true;
          }
        }
      }
      if (!split_attribute) {
        process_verbatim(&upass::uPass::process_attr_set);
      }
      break;
    }
    case Ntype::Lnast_ntype_attr_get: {
      // Function IO is registry metadata, available without evaluating a body.
      const auto saved = lm->save_cursor();
      bool folded = false;
      if (lm->move_to_child()) {
        const std::string dst(lm->current_text());
        if (lm->move_to_sibling()) {
          const std::string base(lm->current_raw_text());
          auto callee = lookup_callee(base);
          if (!callee) {
            if (auto value = symbol_table_.comptime_scalar(lm->current_text()); value && value->is_string()) {
              callee = lookup_callee(value->to_string());
            }
          }
          if (callee && lm->move_to_sibling()) {
            const auto attr = lm->current_raw_text();
            if (attr == "inp" || attr == "out") {
              auto names = std::make_shared<Bundle>(dst);
              names->set_value_kind(upass::Kind::tuple);
              absl::flat_hash_set<std::string> seen;
              const auto& ports = attr == "inp" ? callee->io_meta().inputs : callee->io_meta().outputs;
              int pos = 0;
              for (const auto& port : ports) {
                const std::string name(Bundle::get_first_level(port.name));
                if (seen.insert(name).second) {
                  names->set(bundle_path::of_string(std::to_string(pos++)), *Dlop::from_string(name));
                }
              }
              symbol_table_.set(dst, names);
              folded = true;
            }
          }
        }
      }
      lm->restore_cursor(saved);
      if (folded) {
        emit_subtree_verbatim();
      } else {
        process_verbatim(&upass::uPass::process_attr_get);
      }
      break;
    }

    // Type metadata — emit verbatim, but dispatch so the attribute pass
    // observes type_spec for max/min/bits derivation. (type_def was deleted —
    // `type Foo = …` now lowers to `declare(mode=="type")`, handled above.)
    // A standalone type_spec(tmp, TYPE) is a producer for tmp's
    // bundle: bake the type facts before the pass dispatch.
    case Ntype::Lnast_ntype_type_spec:
      if (!try_detuple_typespec()) {
        bake_decl_pre_step(/*is_declare=*/false);
        process_verbatim(&upass::uPass::process_type_spec);
      }
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

    case Ntype::Lnast_ntype_rolled_for: {
      const auto source = lm->get_lnast();
      const auto here   = lm->save_cursor();
      std::vector<Lnast_nid> kids;
      for (auto kid : source->children(lm->get_current_nid())) {
        kids.push_back(kid);
      }
      if (!lm->in_inline_frame() || kids.size() != lnast_rolled_for::arity
          || !source->get_name(kids[lnast_rolled_for::activation]).empty()) {
        emit_subtree_verbatim();
        break;
      }
      // A previously processed comb may already contain a compact loop.
      // Inlining its opaque call payload renames the lifted function/ports
      // and leaves the carry's comptime seed live. Replay the retained source
      // body in the inline frame, just as unroll_for does when it declines
      // rolling inside that frame. All normal carry/type/constprop hooks run.
      lm->move_to_nid(kids[lnast_rolled_for::index]);
      const std::string index(lm->current_text());
      const auto first = Dlop::from_pyrope(source->get_name(kids[lnast_rolled_for::first]))->to_just_i64();
      const auto step  = Dlop::from_pyrope(source->get_name(kids[lnast_rolled_for::step]))->to_just_i64();
      const auto count = Dlop::from_pyrope(source->get_name(kids[lnast_rolled_for::count]))->to_just_i64();
      if (!unroll_requested_ && count > 0) {
        Loop_roll_plan plan;
        const auto raw_index = std::string(source->get_name(kids[lnast_rolled_for::index]));
        const auto last = static_cast<int64_t>(static_cast<__int128>(first) + static_cast<__int128>(count - 1) * step);
        if (plan_loop_roll(kids[lnast_rolled_for::source_body], raw_index, first, last, step, plan)) {
          plan.inst = std::format("u_loop_{}", roll_seq_);
          plan.mangled = std::format("{}.__loop{}", lm->outlining_owner(), roll_seq_++);
          if (specialized_emitted_.insert(plan.mangled).second) {
            new_lnasts.push_back(lift_loop_body(kids[lnast_rolled_for::source_body], plan));
          }
          emit_rolled_loop_call(plan, kids[lnast_rolled_for::source_body]);
          lm->restore_cursor(here);
          break;
        }
      }
      const bool saved_break = loop_break_hit_;
      loop_break_hit_ = false;
      Unroll_scope unroll(*this);
      for (int64_t ordinal = 0; ordinal < count; ++ordinal) {
        const auto value = static_cast<int64_t>(static_cast<__int128>(first) + static_cast<__int128>(ordinal) * step);
        lm->restore_cursor(here);
        lm->move_to_nid(kids[lnast_rolled_for::source_body]);
        if (!walk_loop_iteration([&]() { emit_inline_binding(index, Lnast_node::create_const(std::to_string(value))); })) {
          break;
        }
        unroll.complete_iteration();
        if (loop_break_hit_) {
          break;
        }
        unroll.next_iteration();
      }
      loop_break_hit_ = saved_break;
      loop_continue_hit_ = false;
      lm->restore_cursor(here);
      break;
    }

    // While / loop — comptime unroll the statically-true infinite form (until a
    // `break`), drop a known-false loop, and leave any data-dependent /
    // non-bool condition verbatim (typecheck flags it; codegen keeps a runtime
    // loop). Dispatch still happens inside unroll_while's verbatim path.
    case Ntype::Lnast_ntype_while:
      dispatch_to_passes(&upass::uPass::process_while);
      unroll_while();
      break;

    // Tick — the simulation cycle loop of a `test` block. Deliberately NOT
    // unrolled, unlike `for`/`while`: its iteration count is assumed UNKNOWN
    // even when written as a literal, because a tick count is routinely
    // overridden by a runtime `--arg` (`tick cycles` with `cycles:u20=4` is the
    // common shape, and `--arg cycles=0` must not be a miscompile).
    //
    // Consequence, and the whole reason this case exists: the body may run zero
    // times, so a variable written inside is `prior-value`-or-`written-value`
    // afterwards — not knowable. Walking the body as an UNCERTAIN scope makes
    // Symbol_table::leave_scope invalidate every such variable on exit, which is
    // exactly the required rule and needs no tick-specific logic in constprop.
    // A variable the tick never writes is untouched and keeps its comptime value.
    //
    // Spec + regression gates: inou/prp/tests/sim/tick_comptime_{survives,opaque}.prp
    case Ntype::Lnast_ntype_tick:
      tick_uncertain_body();
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
  // TOP-level dot only (bundle_key is backtick-aware): a quoted identifier
  // such as `` `bht_d.valid` `` -- inou/slang's flattened struct-field array,
  // re-read from emitted Pyrope -- is ONE name. A raw find('.') baked it as a
  // field of a root that does not exist, so its `mut` mode never landed on the
  // binding and the runner's dst-drop rule below then deleted its comptime
  // whole-array seed store (tolg: "written before it has an initializer").
  const bool        dotted = !bundle_key::is_single_level(var);

  upass::Kind kind = upass::Kind::unknown;
  Dlop        decl_max;  // invalid = unbounded/unset
  Dlop        decl_min;
  Dlop        elem_max;  // array declares: the ELEMENT envelope ([4][8]u8 → u8)
  Dlop        elem_min;
  Dlop        array_size;                        // outer declared extent (for static bounds checks)
  bool        array_dim_pending = false;         // outer dim written but not folded yet (`[N]` before N is bound)
  Dlop        array_flat_size;                   // ALL dims multiplied ([4][8]u8 -> 32); invalid when any dim is unresolved
  upass::Kind elem_kind = upass::Kind::unknown;  // array declares: the element KIND (integer vs boolean)
  std::string type_name;
  upass::Mode mode       = upass::Mode::unknown;
  bool        comptime   = false;
  // A `()` COMPOSITE-TUPLE type slot — POSITIVE evidence that the name holds a
  // tuple, stamped onto the bundle below as value_kind.  An empty `()` bakes no
  // scalar "0" leaf, so it is shape-indistinguishable from an untyped runtime
  // scalar (`mut c = ack`, whose prim_type_none slot likewise bakes no leaf);
  // constprop's empty-tuple compare fold needs this flag to tell them apart.
  // NOT arrays: value_kind is also typecheck's assignment kind, and an array's
  // ELEMENTS are scalars (`mut buf:[4]u8 = 0`; `buf[i] = 3` would be rejected as
  // int-into-tuple).  An array only needs the flag when EMPTY, which is handled by
  // the tuple-literal stamp in typecheck (`mut a:[] = nil` carries no entries).
  bool        tuple_type = false;

  // 2f-type_bound — a bound may be a REF instead of a const when prp2lnast
  // could not fold it (`:unsigned(bits=N)` with a GENERIC N, a scalar or an
  // array element: the value only exists after specialization). Fold it here
  // and rewrite the node IN PLACE, exactly as an `[N]` array dim is and for the
  // same reason: the declare is copied VERBATIM into staging, so every
  // downstream reader (tolg, prp_writer, decl_facts) takes the node's text
  // and `Dlop::from_pyrope("N")` would silently read a character code.
  auto bake_bound = [&](Dlop& out) {
    if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
      if (auto v = Dlop::from_pyrope(lm->current_text()); v->is_integer()) {
        out = *v;
      }
      return;
    }
    if (!Lnast_ntype::is_ref(lm->get_raw_ntype())) {
      return;  // unbounded
    }
    const std::string bound_ref{lm->current_text()};
    if (auto fv = try_fold_ref(bound_ref); fv && fv->is_integer() && !fv->has_unknowns()) {
      out = *fv;
      if (loop_depth_ > 0) {
        loop_baked_refs_.push_back(
            {lm->get_lnast(), lm->get_current_nid(), std::string(lm->get_lnast()->get_name(lm->get_current_nid()))});
      }
      lm->get_lnast()->set_name(lm->get_current_nid(), std::string(fv->to_pyrope()));
      lm->get_lnast()->set_type(lm->get_current_nid(), Lnast_ntype::create_const());
    } else if (!(lm->get_lnast() && lm->get_lnast()->is_template() && !lm->in_inline_frame())) {
      // Still a ref with the generics bound: the bound is genuinely not a
      // compile-time value (`mut r:unsigned(bits=if runtime_s {5} else {4})`).
      // Say so here. Without this the declare keeps an unresolved bound,
      // decl_facts reports bits==0, and the user sees whatever downstream
      // pass trips over it first -- for a narrowing write that was
      // "call to undefined function 'wrap'", three passes away.
      //
      // Compiler temps name nothing the user wrote: a `bits=E` bound lowers to
      // a `(1 << E) - 1` temp, and a temp DECLARATION with a deferred bound is
      // the lane of a `wrap`/`sat` into a bit range (prp2lnast's only one).
      const bool  lane = Lnast::is_tmp(var);
      std::string msg;
      if (lane) {
        msg = "the lane width of this `wrap`/`sat` bit-range write is not a compile-time value";
      } else if (Lnast::is_tmp(bound_ref)) {
        msg = std::format("integer type bound of `{}` is not a compile-time value", var);
      } else {
        msg = std::format("integer type bound `{}` of `{}` is not a compile-time value", bound_ref, var);
      }
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "type-bound-not-comptime",
          .category = "type",
          .pass     = "upass.runner",
          .message  = std::move(msg),
          .span     = lm->current_span(),
          .hint     = lane ? "the bounds that set the width must fold at compile time: literals, `comptime const`s, generic "
                             "parameters, attribute reads or the index of a loop that unrolls"
                           : "an integer type bound must be a literal, a `comptime const`, or a generic parameter",
      });
    }
  };
  // A SCALAR named-type alias (`type PType = u10`; local OR imported
  // `pkg.PType`): borrow the alias's declared range so `:PType` constrains
  // width exactly like a literal `:u10`. The alias's own declare
  // (`declare(PType, prim_type_int(max,min), 'type')`) already baked its
  // "0"-entry envelope into PType's bundle. A TUPLE/struct named type carries
  // fields (not a scalar "0" range) and is materialized by constprop's
  // named-type default path instead -- its "0" entry has no decl range, so
  // this leaves the outputs unset.
  auto borrow_alias = [&](const std::string& alias_type, Dlop& out_max, Dlop& out_min, upass::Kind& out_kind) {
    // An integer-encoded enum (`reg st:Color`) borrows the range of its hidden
    // encoding alias (user ruling 2026-09-28 (29)): in this unit, or in the
    // file shell for a file-scope `const Color = enum(…)`.
    const auto  enc   = Lnast::enum_encoding_type(alias_type);
    const auto& alias = symbol_table_.has_bundle(enc) ? enc : alias_type;
    if (alias != enc) {
      if (const auto [owner, type_n] = lookup_file_type(enc); owner && Lnast_ntype::is_prim_type_int(owner->get_type(type_n))) {
        const auto max_n = owner->get_first_child(type_n);
        const auto min_n = max_n.is_invalid() ? max_n : owner->get_sibling_next(max_n);
        if (!min_n.is_invalid()) {
          out_max  = Dlop::from_pyrope_cached(owner->get_name(max_n));
          out_min  = Dlop::from_pyrope_cached(owner->get_name(min_n));
          out_kind = upass::Kind::integer;
          return;
        }
      }
    }
    if (!symbol_table_.has_bundle(alias)) {
      // A FILE-SCOPE scalar alias (`type Addr = U5` next to the mod): the unit
      // has no binding for it, only the file shell's declare. Without these
      // facts a `reg r:Addr` had no envelope, so `wrap r += 1` could not lower
      // ("call to undefined function 'wrap'", suggestions6 1.7).
      if (const auto [owner, type_n] = lookup_file_type(alias); owner) {
        const auto tt = owner->get_type(type_n);
        if (Lnast_ntype::is_prim_type_int(tt)) {
          const auto max_n = owner->get_first_child(type_n);
          const auto min_n = max_n.is_invalid() ? max_n : owner->get_sibling_next(max_n);
          if (!min_n.is_invalid()) {
            out_max  = Dlop::from_pyrope_cached(owner->get_name(max_n));
            out_min  = Dlop::from_pyrope_cached(owner->get_name(min_n));
            out_kind = upass::Kind::integer;
            return;
          }
        } else if (Lnast_ntype::is_prim_type_bool(tt)) {
          out_kind = upass::Kind::boolean;
          return;
        } else if (Lnast_ntype::is_prim_type_string(tt)) {
          out_kind = upass::Kind::string;
          return;
        }
      }
      // IMPORTED alias (`x:pkg.PType`) -- resolve off the exporter's pub
      // list/values instead (the lambda unit has no import statement).
      Dlop imax, imin;
      if (imported_alias_range(alias, imax, imin)) {
        out_max  = imax;
        out_min  = imin;
        out_kind = upass::Kind::integer;
        return;
      }
    }
    if (auto tb = symbol_table_.get_bundle(alias);
        tb && !tb->has_named_top() && tb->unnamed_top_count() <= 1 && tb->get_value_kind() != upass::Kind::tuple) {
      // A genuinely SCALAR named type (not a tuple/struct — those carry
      // fields and are materialized by constprop's named-type default path;
      // borrowing their leaked "0"-entry kind would mis-type the var as a
      // bare scalar, breaking `mut x:Complex = (…)`).
      const auto& te = tb->get_entry(bundle_path::of_string("0"));
      if (!te.decl_max.is_invalid() || !te.decl_min.is_invalid()) {
        out_max  = te.decl_max;
        out_min  = te.decl_min;
        out_kind = te.kind == upass::Kind::unknown ? upass::Kind::integer : te.kind;
      } else if (te.kind == upass::Kind::boolean || te.kind == upass::Kind::string) {
        out_kind = te.kind;  // `type B = bool` / `type S = string`
      }
    }
  };

  if (lm->move_to_sibling()) {  // TYPE slot
    const auto t = lm->get_raw_ntype();
    tuple_type   = Lnast_ntype::is_comp_type_tuple(t);
    if (Lnast_ntype::is_comp_type_array(t)) {
      // Array declare — bake the innermost element's envelope as INTERNAL
      // bundle attrs (__elem_max/__elem_min; bitwidth checks element stores
      // against them). Entry-0 decl_max/min stay untouched: stamping them
      // would make the array read as a scalar of the element type
      // (decl_facts/try_decl_type consumers).
      int     depth   = 0;
      int64_t flat    = 1;     // running product of EVERY dim, for __array_flat_size
      bool    flat_ok = true;  // one unresolved dim poisons the whole product
      while (Lnast_ntype::is_comp_type_array(lm->get_raw_ntype()) && lm->has_child()) {
        const auto arr_nid = lm->get_current_nid();
        const auto dim_n   = upass::array_level_dim(*lm->get_lnast(), arr_nid);
        int64_t    n       = -1;
        if (!dim_n.is_invalid()
            && (Lnast_ntype::is_const(lm->get_lnast()->get_type(dim_n)) || Lnast_ntype::is_ref(lm->get_lnast()->get_type(dim_n)))) {
          std::string_view dim = lm->get_lnast()->get_name(dim_n);
          if (dim.size() >= 2 && dim.front() == '[' && dim.back() == ']') {
            dim.remove_prefix(1);
            dim.remove_suffix(1);
          }
          int64_t parsed = 0;
          if (auto [ptr, ec] = std::from_chars(dim.data(), dim.data() + dim.size(), parsed);
              ec == std::errc{} && ptr == dim.end() && parsed >= 0) {
            n = parsed;
          } else if (auto fv = fold_frame_ref(dim); fv && fv->is_integer() && fv->is_just_i64() && fv->to_just_i64() > 0) {
            // `mut v:[N]T` / `[N][N]T` with a comptime-named dim (`const
            // '[N]'`), or `[N+1]T` whose expression prp2lnast lowered to
            // statements (`ref %t`): the declare is copied VERBATIM into
            // staging, and every downstream reader takes the dim node's text —
            // `Dlop::from_pyrope("N")` is the character code of 'N', which
            // silently sized every `[N]` array at 78 lanes. Fold it to digits
            // IN PLACE, at every depth, before the verbatim copy.
            n = fv->to_just_i64();
            lm->get_lnast()->set_name(dim_n, "[" + std::to_string(n) + "]");
            lm->get_lnast()->set_type(dim_n, Lnast_ntype::create_const());
          } else if (Lnast::is_tmp(dim) && !(lm->get_lnast()->is_template() && !lm->in_inline_frame())) {
            // `[a+1]T` over a runtime value, with any generics bound: say so
            // here, at the declaration, instead of letting tolg name the
            // compiler temp the expression was lowered to.
            livehd::diag::sink().emit(livehd::diag::Diagnostic{
                .severity = livehd::diag::Severity::error,
                .code     = "array-dim-not-comptime",
                .category = "type",
                .pass     = "upass.runner",
                .message  = std::format("the dimension of array `{}` is not a compile-time value", var),
                .span     = lm->current_span(),
                .hint     = "size an array with a literal, a `comptime const`, or an expression over generic parameters",
            });
          } else if (const auto lit = upass::array_dim_lanes(dim)) {
            n = *lit;  // `[0x100]`, `[1_024]`
          } else if (is_declare && !dim.empty() && !index_dim_reported_
                     && !(lm->get_lnast()->is_template() && !lm->in_inline_frame())) {
            // Neither a compile-time integer, an index range nor an enum type
            // (Index_dims_scope lowered those to their extent): the array would
            // silently have no size at all.
            livehd::diag::sink().emit(livehd::diag::Diagnostic{
                .severity = livehd::diag::Severity::error,
                .code     = "array-dim-not-comptime",
                .category = "type",
                .pass     = "upass.runner",
                .message  = std::format("the dimension `[{}]` of array `{}` is not a compile-time integer, an index range or "
                                        "an enum type",
                                       dim,
                                       upass::Lnast_manager::user_name(var)),
                .span     = lm->current_span(),
                .hint     = "size an array with a literal or a `comptime const` (`[16]`), an index range (`[100..<132]`) or "
                            "an enum type (`[State]`)",
            });
          }
          if (depth == 0 && n >= 0) {
            array_size = *Dlop::create_integer(n);
          } else if (depth == 0 && !dim.empty()) {
            array_dim_pending = true;
          }
        }
        // Fail closed on an overflowing product too: `__array_flat_size` sizes
        // `concat(a)`, and a wrapped extent would relocate every lane above it.
        if (n > 0 && flat <= std::numeric_limits<int64_t>::max() / n) {
          flat *= n;
        } else {
          flat_ok = false;
        }
        lm->move_to_child();  // child0 = element (possibly a nested array)
        ++depth;
      }
      if (flat_ok && flat > 0) {
        array_flat_size = *Dlop::create_integer(flat);
      }
      if (Lnast_ntype::is_prim_type_int(lm->get_raw_ntype()) && lm->has_child()) {
        elem_kind = upass::Kind::integer;
        lm->move_to_child();
        ++depth;
        bake_bound(elem_max);
        if (lm->move_to_sibling()) {
          bake_bound(elem_min);
        }
      } else if (Lnast_ntype::is_ref(lm->get_raw_ntype())) {
        // `[N]Row` over a scalar alias (`type Row = u4`, or `unsigned(bits=N)`
        // folded at the alias's own declare): the element takes the alias's
        // envelope, so the array is as declared as `[N]u4` is.
        borrow_alias(std::string(lm->current_text()), elem_max, elem_min, elem_kind);
        if (elem_kind == upass::Kind::boolean) {
          elem_max = *Dlop::create_integer(1);
          elem_min = *Dlop::create_integer(0);
        } else if (elem_kind != upass::Kind::integer) {
          elem_kind = upass::Kind::unknown;
          elem_max  = Dlop{};
          elem_min  = Dlop{};
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
      if (lm->move_to_child()) {  // up to two bounds; anything unresolved = unbounded
        bake_bound(decl_max);
        if (lm->move_to_sibling()) {
          bake_bound(decl_min);
        }
        lm->move_to_parent();
      }
    } else if (Lnast_ntype::is_prim_type_bool(t)) {
      kind = upass::Kind::boolean;
    } else if (Lnast_ntype::is_prim_type_string(t)) {
      kind = upass::Kind::string;
    } else if (Lnast_ntype::is_ref(t)) {
      type_name = lm->current_text();  // named type (`x:Point`)
      borrow_alias(type_name, decl_max, decl_min, kind);
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
      // `mut x = 0ub????` (an X-pattern init — the Verilog-import poison of a
      // net with no explicit type): the pattern's WIDTH is the declared
      // envelope. Without it the var has no decl range, and a later
      // per-version bit-select force (`x = v#[0..=2]` in one if-arm) would
      // merge ITS narrower stamp in as the declared envelope — failing a
      // sibling arm's legal wider write against a range the source never
      // declared.
      if (decl_max.is_invalid() && decl_min.is_invalid() && (mode == upass::Mode::mut_kind || mode == upass::Mode::wire_kind)
          && lm->move_to_sibling() && Lnast_ntype::is_const(lm->get_raw_ntype())) {
        const auto vt = lm->current_text();
        if (vt.find('?') != std::string_view::npos) {
          if (auto v = Dlop::from_pyrope(vt); v && v->has_unknowns() && v->get_signed_bits() > 1) {
            const bool sgn  = vt.size() > 1 && vt[1] == 's';
            const auto bits = static_cast<uint32_t>(sgn ? v->get_signed_bits() : v->get_payload_bits());
            if (bits > 0) {
              decl_max = upass::max_from_bits(bits, sgn);
              decl_min = upass::min_from_bits(bits, sgn);
              if (kind == upass::Kind::unknown) {
                kind = upass::Kind::integer;
              }
            }
          }
        }
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
    if (!decl_max.is_invalid() || !decl_min.is_invalid()) {
      symbol_table_.typed_fields[root].emplace(fpath);  // a field declared with a type (`mut p:Pkt` splits into these)
    }
    auto rb = symbol_table_.get_bundle_for_write(root);
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
  // A `wire`/`reg` binding never carries a comptime value: constprop does not
  // bind its stores (process_store/process_assign return early), so a trivial
  // already on the name at its declare can never be overwritten. The one
  // producer is the comb inliner, which seeds every output `inlN_out = nil` in
  // the call prologue BEFORE the body's own `wire out:T` re-declaration
  // (inou.slang emits it for an output net read before its driver). The stale
  // nil then rode the epilogue tuple into the caller: `alu.adder_out#[0..=47]`
  // folded to nil (get_mask dropped, consumer left dangling -> tolg "unresolved
  // ref ... wiring nil", an all-X dcache address in lhdsuite's minion) and
  // `if alu.adder_out#[3] != 0` folded to a constant with NO diagnostic.
  if (is_declare && (mode == upass::Mode::wire_kind || mode == upass::Mode::reg_kind)) {
    const auto p0 = bundle_path::of_string("0");
    if (bundle->has_trivial(p0) && !bundle->get_trivial(p0).is_invalid()) {
      bundle->set(p0, Bundle::invalid_lconst);  // value only: the entry keeps kind/decl range/mode
    }
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
  // A name typed by its own declaration / type_spec (a declared variable, a
  // stamped slice or cast temp), unlike one whose envelope only rode in on a
  // value: the typed `~` (ruling 26) reads the former only. An untyped
  // declaration starts a new untyped binding (`const t = x` in the sibling arm
  // of an `if` whose other arm declared `const t:u3`).
  if (!decl_max.is_invalid() || !decl_min.is_invalid() || kind == upass::Kind::integer) {
    declared_typed_.insert(var);  // `kind` alone: an unbounded `x:signed`
  } else if (is_declare) {
    declared_typed_.erase(var);
  }
  if (is_declare) {
    symbol_table_.typed_fields.erase(var);  // a new binding of the name: its field declarations follow it
  }
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
  if (!array_size.is_invalid()) {
    bundle->set_attr("__array_size", array_size);
  }
  if (array_dim_pending) {
    bundle->set_attr("__array_dim_pending", *Dlop::create_integer(1));
  }

  // The FLAT entry count (every dim multiplied). `__array_size` is the OUTER
  // extent -- what an `a[i]` bounds check compares against -- so it is the
  // wrong number for anything that wants the whole array's storage, such as
  // sizing `concat(a)`.
  if (!array_flat_size.is_invalid()) {
    bundle->set_attr("__array_flat_size", array_flat_size);
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
            if (!decl_max.is_invalid() || !decl_min.is_invalid()) {
              symbol_table_.typed_fields[root].emplace(fpath);  // `(mut a:u3 = 0)`: a declared field type
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
  // A declaration at the end of a block (or an ordinary array whose shape
  // never materialized) must be replayed while this block's symbol scope and
  // staging insertion point are still active.
  detuple_flush_pending_decl();
  // Pre-pop hook fires while the staging cursor is still inside the stmts
  // block, so a pass (e.g. coalescer) can flush deferred writes into the
  // closing block rather than the parent scope. process_stmts_post fires
  // after the pop and is for tear-down (e.g. constprop's scope leave).
  dispatch_to_passes(&upass::uPass::process_stmts_pre_pop);
  emit_pop();
  dispatch_to_passes(&upass::uPass::process_stmts_post);
  if (symbol_table_.stack.size() == 2 && lm->get_lnast().get() == root_lnast_.get()) {
    snapshot_unit_body();  // the unit's own body block, about to close
  }
  // Pop AFTER stmts_post so passes' tear-down hooks (e.g. constprop's pub
  // harvest, which reads the scope depth) still see the block scope active.
  symbol_table_.leave_scope();
}

void uPass_runner::snapshot_unit_body() {
  const auto keep = [&](std::string_view name) {
    const auto b = symbol_table_.get_bundle(name);
    if (!b) {
      return;
    }
    Body_value bv{.kind = upass::decl_facts::bundle_kind(*b)};
    if (const auto t = b->scalar(); t && t->is_integer() && !t->has_unknowns()) {
      bv.value = *t;
    }
    unit_body_values_.insert_or_assign(std::string(name), std::move(bv));
  };
  for (const auto& name : io_output_names_) {
    keep(name);
  }
  for (const auto& name : default_value_names_) {
    keep(name);
  }
}

// Walk a `tick` node: (count, stmts-body). The count is evaluated ONCE, before
// the loop, so it is walked in the enclosing (certain) scope; the BODY is walked
// as an uncertain scope.
//
// The tick node itself is always emitted — a tick is never unrolled and never
// dropped, however its count folds. Folding on the count is precisely what must
// not happen: it is routinely overridden by a runtime `--arg`, so even a literal
// count carries no guarantee the body runs at all. Marking the body uncertain
// makes Symbol_table::leave_scope invalidate every variable written inside, so a
// value that would only be correct after >= 1 iteration can never escape.
//
// `notify_uncertain_arm_begin/end` are dispatched around the body for the same
// reason the if-arm walk does it: the coalescer must flush deferred writes at
// the boundary, and bitwidth must stop treating in-body stores as the only range.
void uPass_runner::tick_uncertain_body() {
  if (!lm->has_child()) {
    emit_subtree_verbatim();  // malformed tick (no count, no body) — never drop it silently
    return;
  }

  emit_push(lm->current_type());
  lm->move_to_child();

  // child0 — the iteration count, in the ENCLOSING scope.
  process_lnast();

  // child1 (and any future trailing children) — the body.
  //
  // The body is EMITTED VERBATIM, not folded. Two independent reasons:
  //
  //   1. Nothing inside a tick may be folded anyway (R0/R1/R2), so folding the
  //      body could only produce values that must then be thrown away.
  //   2. A tick body pokes and peeks DUT INSTANCE PORTS (`acc.en = true`,
  //      `got = acc.v`). upass has no model of an instance — `mut acc = counter`
  //      binds no io — so walking those reads raises a spurious
  //      "unknown field `v` on tuple `acc`". Modelling instances properly is a
  //      separate piece of work; until then, not folding is both sufficient and
  //      honest.
  //
  // What we DO need is the body's WRITES, so a variable captured in the loop
  // (`got = acc.v`) loses its stale pre-loop binding instead of keeping the
  // initializer — that stale binding is exactly what made a testbench `assert`
  // fold to false (lhdsuite fixme issue 2). So: open an uncertain scope,
  // register every name the body stores to, emit, and close — leave_scope then
  // invalidates each one in its declaring scope.
  while (lm->move_to_sibling()) {
    symbol_table_.block_scope(lm->current_scope_uid());
    symbol_table_.mark_current_uncertain();
    dispatch_to_passes(&upass::uPass::notify_uncertain_arm_begin);
    register_tick_body_writes();
    check_tick_body_kinds();
    emit_subtree_verbatim();
    dispatch_to_passes(&upass::uPass::notify_uncertain_arm_end);
    symbol_table_.leave_scope();
  }

  lm->move_to_parent();
  emit_pop();
}

// Walk the subtree under the cursor and register every stored-to NAME as an
// uncertain write. Cursor-neutral (saved/restored), and it folds nothing — it
// only reads node types and the first child of each store.
void uPass_runner::register_tick_body_writes() {
  const auto saved = lm->save_cursor();

  std::function<void()> walk = [&]() {
    const auto t = lm->current_type();
    if (Lnast_ntype::is_store(t) && lm->has_child()) {
      lm->move_to_child();
      if (Lnast_ntype::is_ref(lm->current_type())) {
        // A field write (`acc.en = …`) stores to the dotted path; the ROOT name
        // is what has to be invalidated, so cut at the first dot.
        std::string_view nm = Bundle::get_first_level(lm->current_text());  // backtick-aware root
        if (!nm.empty()) {
          symbol_table_.note_uncertain_write(nm);
        }
      }
      lm->move_to_parent();
    }
    if (lm->has_child()) {
      lm->move_to_child();
      do {
        walk();
      } while (lm->move_to_sibling());
      lm->move_to_parent();
    }
  };
  walk();

  lm->restore_cursor(saved);
}

void uPass_runner::bind_instance_handle(std::string_view dst, std::string_view rhs) {
  // The bound name is a unit (`mut d = dut`), or an import binding of one:
  // `import("unit.lam")` / `import("ln:…")` bind the tree name, `import("lg:…")`
  // the url of a compiled library whose ports upass cannot see (a handle with
  // no unit: nothing to check, reads stay unknown).
  std::shared_ptr<Lnast> unit;
  if (!symbol_table_.has_bundle(rhs)) {
    unit = lookup_callee(rhs);
    if (!unit || unit->io_meta().empty()) {
      return;
    }
  } else if (const auto fv = try_fold_ref(rhs); fv && fv->is_string()) {
    auto fn = fv->to_pyrope();
    if (fn.size() >= 2 && fn.front() == '\'' && fn.back() == '\'') {
      fn = fn.substr(1, fn.size() - 2);
    }
    if (!fn.starts_with("lg:")) {
      unit = lookup_callee(fn.starts_with("ln:") ? std::string_view(fn).substr(3) : std::string_view(fn));
      if (!unit || unit->io_meta().empty()) {
        return;
      }
    }
  } else {
    return;
  }
  symbol_table_.instance_handles.insert_or_assign(std::string(dst), unit);
  if (unit) {
    stash_sub_instance_port_facts(dst, unit);  // `d.o` reads carry the port's declared kind/range
  }
}

bool uPass_runner::try_instance_handle_store() {
  if (lm->current_num_children() < 3 || symbol_table_.instance_handles.empty()) {
    return false;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  const auto hit = Lnast_ntype::is_ref(lm->current_type()) ? symbol_table_.instance_handles.find(lm->current_text())
                                                           : symbol_table_.instance_handles.end();
  if (hit == symbol_table_.instance_handles.end()) {
    lm->restore_cursor(saved);
    return false;
  }
  const auto [port, got] = instance_poke_operands([this] { return value_kind_of(lm->current_node()); });
  const auto value       = lm->current_node();
  lm->restore_cursor(saved);
  check_instance_clock_poke(hit->second, port, got, value, /*value_is_clock=*/false, lm->current_span());
  check_instance_poke(hit->second, port, got, value, lm->current_span());
  emit_subtree_verbatim();  // a poke drives the port; the handle holds no field
  return true;
}

std::pair<std::string, upass::Kind> uPass_runner::instance_poke_operands(const std::function<upass::Kind()>& value_kind) {
  // The fields between the handle and the value name the poked input: `d.en`,
  // or a tuple port's flattened leaf `d.cmd.op` ("cmd.op"). A computed field
  // names no leaf ("").
  std::string port;
  bool        named = true;
  lm->move_to_sibling();
  while (true) {
    const auto saved = lm->save_cursor();
    if (!lm->move_to_sibling()) {
      lm->restore_cursor(saved);
      break;  // the cursor is on the value
    }
    lm->restore_cursor(saved);
    named = named && Lnast_ntype::is_const(lm->current_type());
    absl::StrAppend(&port, port.empty() ? "" : ".", lm->current_text());
    lm->move_to_sibling();
  }
  return {named ? port : std::string{}, value_kind()};
}

void uPass_runner::check_instance_poke(const std::shared_ptr<Lnast>& unit, std::string_view port, upass::Kind got,
                                       const Lnast_node& value, const livehd::diag::Span& span) const {
  if (!unit || port.empty()) {
    return;
  }
  const auto name = unit->get_top_module_name();
  const auto bare = name.substr(name.rfind('.') + 1);
  for (const auto& e : unit->io_meta().inputs) {
    if (e.name != port || e.array_size != 0) {
      continue;
    }
    if (e.sig == Io_sig::clock) {
      continue;  // a Clock is not data: check_instance_clock_poke owns its rules
    }
    check_bind_kind(e, e.kind, got, bare, span);
    // A poke binds like a call argument (ruling 1): a value whose derived
    // range may not fit the input never narrows implicitly.
    const auto declared = (e.kind == Io_kind::boolean || value.is_invalid()) ? std::nullopt : declared_param_range(unit, e, {});
    const auto actual   = declared ? value_range_of(value) : Value_range{};
    if (!actual.bounded() || !actual.may_exceed(declared->first, declared->second)) {
      return;
    }
    const auto& amin     = *actual.min;
    const auto& amax     = *actual.max;
    const bool  disjoint = amin.gt_op(declared->second)->is_known_true() || amax.lt_op(declared->first)->is_known_true();
    fcall_arg_fail(
        span,
        "fcall-arg-overflow",
        std::format("poke of input `{}` of `{}` ({}) {} not fit its declared range [{}, {}]",
                    e.name,
                    bare,
                    amin.same_repr(amax) ? std::format("value {}", amin.to_decimal_string())
                                         : std::format("range [{}, {}]", amin.to_decimal_string(), amax.to_decimal_string()),
                    disjoint ? "does" : "may",
                    declared->first.to_decimal_string(),
                    declared->second.to_decimal_string()),
        "a poke never narrows implicitly: slice the value (`x#[0..<N]`) or `wrap` it into a typed local first",
        "type");
  }
}

void uPass_runner::check_instance_clock_poke(const std::shared_ptr<Lnast>& unit, std::string_view port, upass::Kind got,
                                             const Lnast_node& value, bool value_is_clock, const livehd::diag::Span& span) const {
  if (!unit || port.empty()) {
    return;
  }
  const auto name = unit->get_top_module_name();
  const auto bare = name.substr(name.rfind('.') + 1);

  const Lnast_io_entry* e = nullptr;
  for (const auto& in : unit->io_meta().inputs) {
    if (in.name == port && in.array_size == 0) {
      e = &in;
      break;
    }
  }
  // The MINTED clock/reset of a mod/pipe that declares none of that class: a
  // Clock / Reset input like a declared one (qa.md section 6).
  Io_sig sig = e != nullptr ? e->sig : Io_sig::none;
  if (e == nullptr && (port == "clock" || port == "reset") && !unit->is_verilog_origin()
      && (unit->get_lambda_kind() == "mod" || unit->get_lambda_kind() == "pipe")) {
    const auto  want = port == "clock" ? Io_sig::clock : Io_sig::reset;
    const auto& ins  = unit->io_meta().inputs;
    if (std::none_of(ins.begin(), ins.end(), [&](const Lnast_io_entry& i) { return i.sig == want; })) {
      sig = want;
    }
  }
  const bool value_const = value.is_const();
  if (sig == Io_sig::clock) {
    if (value_const) {
      fcall_arg_fail(span,
                     "clock-bound-to-constant",
                     std::format("the `Clock` input `{}` of `{}` can not be bound to a constant", port, bare),
                     "a Clock is a real signal, never `1`/`true`: pass the tick's `clock` (or another Clock), or leave the input "
                     "unbound to auto-wire it",
                     "type");
    }
    if (got == upass::Kind::boolean && !value_is_clock) {
      fcall_arg_fail(
          span,
          "clock-bound-to-bool",
          std::format("the `Clock` input `{}` of `{}` can not be driven by a Bool expression (a Bool is not a Clock)", port, bare),
          "a test passes a real Clock such as the tick's `clock`; a derived clock is not allowed (use an enable)",
          "type");
    }
    return;
  }
  if (sig == Io_sig::reset) {
    if (e == nullptr && got == upass::Kind::integer) {  // a declared Reset is kind-checked as a Bool input
      fcall_arg_fail(span,
                     "fcall-arg-kind",
                     std::format("cannot bind integer value to the `Reset` input `{}` of `{}` (a Reset is Bool-like)", port, bare),
                     "an integer never turns into a `Bool` implicitly: write `true`/`false`, `Bool(x)` or `x != 0`",
                     "type");
    }
    if (value_is_clock) {
      fcall_arg_fail(span,
                     "reset-bound-to-clock",
                     std::format("cannot bind a Clock to the `Reset` input `{}` of `{}`", port, bare),
                     "a Reset takes a Bool expression such as `clock < 2`",
                     "type");
    }
    return;
  }
  if (value_is_clock && e != nullptr && e->kind == Io_kind::boolean) {
    fcall_arg_fail(span,
                   "fcall-arg-kind",
                   std::format("cannot bind a Clock to the Bool input `{}` of `{}` (a Clock is not a Bool)", port, bare),
                   "write a Bool expression such as `clock > 0`",
                   "type");
  }
}

void uPass_runner::check_tick_body_kinds() {
  const auto saved = lm->save_cursor();
  using K          = upass::Kind;
  // Kinds of the names the body defines so far (statement order), and how a
  // diagnostic names a handle read (`%t` -> `d.f`).
  absl::flat_hash_map<std::string, K>           kinds;
  absl::flat_hash_map<std::string, std::string> labels;
  // Names that hold a Clock (the tick's minted `clock`, and plain copies of it):
  // its numeric view is a debug cycle count (kind unknown, so `clock < 2` and
  // `0x10 + clock` stay legal) but it is never a Bool or an integer condition.
  absl::flat_hash_set<std::string>              clock_names;
  // Declared scalar kind of a typed test local (`const n:U8 = ...`).
  absl::flat_hash_map<std::string, K>           declared;
  {
    // The tick body's first statement declares its minted loop variable.
    const auto s0 = lm->save_cursor();
    if (lm->has_child()) {
      lm->move_to_child();
      if (Lnast_ntype::is_declare(lm->current_type()) && lm->has_child()) {
        lm->move_to_child();
        clock_names.emplace(lm->current_text());
      }
    }
    lm->restore_cursor(s0);
  }

  using upass::op_kind::kind_name;
  // Kind + label of the operand under the cursor.
  const auto operand = [&]() -> std::pair<K, std::string> {
    if (Lnast_ntype::is_const(lm->current_type())) {
      return {value_kind_of(lm->current_node()), "<const>"};
    }
    const std::string nm(lm->current_text());
    const auto        lb = labels.find(nm);
    const std::string label(lb != labels.end() ? std::string_view(lb->second) : upass::Lnast_manager::user_name(nm));
    if (const auto it = kinds.find(nm); it != kinds.end()) {
      return {it->second, label};
    }
    return {value_kind_of(lm->current_node()), label};
  };
  const auto operand_is_clock
      = [&]() { return lm->current_node().is_ref() && clock_names.contains(std::string(lm->current_text())); };
  const auto type_error = [&](std::string_view code, const std::string& msg, const std::string& hint) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                       .code     = std::string{code},
                                                       .category = "type",
                                                       .pass     = "upass.runner",
                                                       .message  = msg,
                                                       .span     = lm->current_span(),
                                                       .hint     = hint});
  };
  // An operator node, by typecheck's own rule table: every operand must be
  // the required kind (unknown unifies), or for `==`/`!=` share one eq_class.
  const auto check_op = [&](const upass::op_kind::Rule& rule) {
    lm->move_to_child();
    const std::string        dst(lm->current_text());
    std::vector<std::string> ops;
    bool                     bad  = false;
    int                      seen = -1;
    while (lm->move_to_sibling()) {
      const auto [k, label] = operand();
      ops.push_back(std::format("{}:{}", label, upass::op_kind::kind_annot(k)));
      if (k == K::unknown || k == K::nil) {
        continue;
      }
      if (rule.required != K::unknown) {
        bad |= k != rule.required;
      } else if (const int c = upass::op_kind::eq_class(k); c >= 0 && seen >= 0 && seen != c) {
        bad = true;
      } else if (seen < 0) {
        seen = c;
      }
    }
    lm->move_to_parent();
    kinds.insert_or_assign(dst, rule.result);
    if (!bad) {
      return;
    }
    const auto joined = absl::StrJoin(ops, rule.required == K::unknown ? " vs " : ", ");
    if (rule.required == K::unknown) {
      type_error(rule.code,
                 std::format("`{}` requires both operands to be the same type ({})", rule.sym, joined),
                 std::format("no implicit conversion — cast explicitly: {}", upass::kBoolIntCastHint));
    } else {
      type_error(rule.code,
                 std::format("operator `{}` requires {} operands ({})", rule.sym, kind_name(rule.required), joined),
                 rule.required == K::boolean
                     ? std::string(upass::op_kind::kLogicalOperandHint)
                     : std::format("no implicit conversion — cast explicitly: {}", upass::kBoolIntCastHint));
    }
  };

  using N                     = Lnast_ntype;
  std::function<void()> visit = [&]() {
    const auto t = lm->current_type();
    if (N::is_stmts(t) || N::is_if_like(t)) {
      if (!lm->has_child()) {
        return;
      }
      lm->move_to_child();
      do {
        if (N::is_if_like(t) && !N::is_stmts(lm->current_type())) {
          // An if/elif condition must be boolean, as typecheck's process_if.
          if (operand_is_clock()) {
            type_error("cond-not-bool",
                       "condition must be boolean, got a Clock (a Clock is not a Bool)",
                       "a Clock's numeric view is a cycle count: compare it, e.g. `if clock > 3`");
            continue;
          }
          if (const auto [k, label] = operand(); k != K::unknown && k != K::nil && k != K::boolean) {
            type_error("cond-not-bool",
                       std::format("condition must be boolean, got {}", kind_name(k)),
                       k == K::integer ? "an integer is a value, not a condition — did you mean `!= 0`?"
                                       : "compare explicitly, e.g. `if x != 0`");
          }
          continue;
        }
        visit();
      } while (lm->move_to_sibling());
      lm->move_to_parent();
      return;
    }
    if (!lm->has_child()) {
      return;
    }
    if (const auto rule = upass::op_kind::rule_of(t)) {
      check_op(*rule);
      return;
    }
    const auto nkids = lm->current_num_children();
    lm->move_to_child();
    const std::string dst(lm->current_text());
    if (N::is_store(t) && nkids >= 3) {
      // A poke `store(d, field..., v)` (the check try_instance_handle_store makes).
      if (const auto hit = symbol_table_.instance_handles.find(dst); hit != symbol_table_.instance_handles.end()) {
        bool value_is_clock    = false;
        const auto [port, got] = instance_poke_operands([&] {
          value_is_clock = operand_is_clock();
          return operand().first;
        });
        check_instance_clock_poke(hit->second,
                                  port,
                                  got,
                                  Lnast_ntype::is_const(lm->current_type()) ? lm->current_node() : Lnast_node{},
                                  value_is_clock,
                                  lm->current_span());
        // A tick body's own names carry no derived range: only a literal's is known.
        check_instance_poke(hit->second,
                            port,
                            got,
                            Lnast_ntype::is_const(lm->current_type()) ? lm->current_node() : Lnast_node{},
                            lm->current_span());
      }
    } else if (N::is_store(t) && nkids == 2 && lm->move_to_sibling()) {
      if (operand_is_clock()) {
        clock_names.insert(dst);
      }
      auto k = operand().first;
      if (const auto d = declared.find(dst); d != declared.end()) {
        // A typed local binds like a call argument: a Bool never turns into an integer or the reverse.
        if ((d->second == K::integer && k == K::boolean) || (d->second == K::boolean && k == K::integer)) {
          type_error("type-mismatch-assign",
                     std::format("cannot assign {} value to `{}` (it is {})",
                                 kind_name(k),
                                 upass::Lnast_manager::user_name(dst),
                                 kind_name(d->second)),
                     d->second == K::integer ? "a `Bool` never turns into an integer implicitly: write `U1(x)` (true == 1)"
                                             : "an integer never turns into a `Bool` implicitly: write `x != 0` or `Bool(x)`");
        }
        k = d->second;
      }
      kinds.insert_or_assign(dst, k);
    } else if (N::is_tuple_get(t) && nkids >= 3 && lm->move_to_sibling()) {
      // `d.f` (or a tuple port's leaf `d.res.f`) on an instance handle: the
      // port's declared kind.
      const auto hit = symbol_table_.instance_handles.find(lm->current_text());
      if (hit != symbol_table_.instance_handles.end()) {
        std::string field;
        while (lm->move_to_sibling()) {
          absl::StrAppend(&field, field.empty() ? "" : ".", lm->current_text());
        }
        labels.insert_or_assign(dst, absl::StrCat(hit->first, ".", field));
        if (const auto* e = hit->second ? hit->second->io_meta().find(field) : nullptr; e != nullptr && e->array_size == 0) {
          kinds.insert_or_assign(dst,
                                 e->sig == Io_sig::clock       ? K::unknown  // a Clock reads as a cycle count, never a Bool
                                 : e->kind == Io_kind::boolean ? K::boolean
                                 : e->kind == Io_kind::integer ? K::integer
                                                               : K::unknown);
        }
      }
    } else if (N::is_declare(t) && lm->move_to_sibling()) {
      if (lm->current_type() == Lnast_ntype::Lnast_ntype_prim_type_int) {
        declared.insert_or_assign(dst, K::integer);
      } else if (lm->current_type() == Lnast_ntype::Lnast_ntype_prim_type_bool) {
        declared.insert_or_assign(dst, K::boolean);
      }
    } else if (N::is_cassert(t)) {
      // An assert condition in a test is a Bool (docs 09-verification): an integer is a value, not a condition.
      if (operand_is_clock()) {
        type_error("cond-not-bool", "assert condition must be boolean, got a Clock", "compare the Clock, e.g. `clock > 3`");
      } else if (const auto [k, label] = operand(); k == K::integer) {
        type_error("cond-not-bool",
                   std::format("assert condition must be boolean, got {} (`{}`)", kind_name(k), label),
                   "an integer is a value, not a condition — did you mean `!= 0`?");
      }
    } else if (N::is_func_call(t) && lm->move_to_sibling()) {
      if (const auto cast = upass::classify_typecast(lm->current_text())) {
        kinds.insert_or_assign(dst,
                               cast->kind == upass::Typecast_kind::to_bool     ? K::boolean
                               : cast->kind == upass::Typecast_kind::to_string ? K::string
                                                                               : K::integer);
      }
    } else if (N::is_shl(t) || N::is_get_mask(t) || N::is_set_mask(t) || N::is_sext(t) || N::is_concat(t)) {
      kinds.insert_or_assign(dst, K::integer);
    }
    lm->move_to_parent();
  };
  visit();
  lm->restore_cursor(saved);
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
            // elif chain: walk it here while every condition folds. A known-false
            // arm is skipped WHOLE (cond + body); the first known-true arm's stmts
            // is emitted; a bare trailing stmts reached with every cond false is
            // emitted; exhausting the children emits nothing. Only a condition that
            // does NOT fold falls through to the full-if walk below.
            //
            // This is required now that the full-if walk drops a dead arm's cond and
            // body instead of copying them: with every cond decided it would emit a
            // DEGENERATE if node (no cond, just the else stmts) that tolg cannot
            // read -- `if false { } elif false { } else { … }` failed with
            // "unresolved reference '' — it has no driver".
            //
            // Cursor: the enclosing move_to_child() pushed the if node, so every
            // return below does one move_to_parent() first, exactly like the
            // decided cases above. try_fold_ref can move the cursor, hence the
            // save/restore around it.
            while (true) {
              const auto arm_t = lm->get_raw_ntype();
              if (arm_t == Ntype::Lnast_ntype_stmts) {
                process_lnast();  // trailing else: every condition was false
                lm->move_to_parent();
                return;
              }
              std::optional<Dlop> aval;
              const auto          arm_here = lm->save_cursor();
              if (arm_t == Ntype::Lnast_ntype_const) {
                aval = *Dlop::from_pyrope(lm->current_text());  // same deref as the first cond above
              } else if (Lnast_ntype::is_ref(arm_t)) {
                aval = try_fold_ref(lm->current_text());
              }
              lm->restore_cursor(arm_here);
              if (!aval || aval->is_invalid() || aval->has_unknowns() || aval->is_nil()) {
                break;  // undecidable: let the full-if walk own the whole chain
              }
              const bool arm_taken = !aval->is_known_false();
              if (!lm->move_to_sibling()) {
                break;  // cond with no body: malformed, let the full walk report it
              }
              if (arm_taken) {
                process_lnast();  // this arm fires; the rest can never
                lm->move_to_parent();
                return;
              }
              if (!lm->move_to_sibling()) {
                lm->move_to_parent();
                return;  // the dead arm was last and there is no else: emit nothing
              }
            }
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
    bool                     last_cond_false     = false;
    bool                     last_cond_true      = false;
    bool                     last_was_cond       = false;
    bool                     already_matched     = false;  // a *previous* arm already fired
    bool                     any_prior_uncertain = false;  // some earlier cond folded to neither true nor false
    // The ref of every condition seen so far (the last one is the current
    // arm's): what holds on an arm's path, for notify_arm_guard.
    std::vector<std::string> cond_refs;

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
        const bool dead             = already_matched || (last_was_cond && last_cond_false);
        // `just_matched` only fires when no earlier arm was uncertain — if
        // a prior cond was undecided at comptime (nil/unknown) the runtime
        // ordering may still pick *that* arm, so a later concrete-true
        // cond doesn't deterministically take over. Treat the body as
        // uncertain instead so its writes get invalidated on exit.
        // Without this gate, a `match` chain where `case (a=33,…)` folds
        // to nil and `case (a=2,…)` folds to true would still concretely
        // apply case-2's body, leaving the var "definitely 1052" even
        // though case-1 might actually fire at runtime.
        const bool just_matched     = last_was_cond && last_cond_true && !any_prior_uncertain;
        // Uncertain := body executes but isn't *guaranteed* to. After a
        // cond: !just_matched (dead is handled separately, so the cond
        // wasn't known-false). Trailing else with no preceding cond: only
        // uncertain when some prior arm's cond didn't fold either way; if
        // every prior cond folded to known-false the else *is* guaranteed.
        const bool uncertain        = last_was_cond ? !just_matched : any_prior_uncertain;
        // A trailing else (a body-stmts with no cond of its own this round)
        // is the only stmts reached with last_was_cond == false.
        const bool is_trailing_else = !last_was_cond;
        last_was_cond               = false;
        if (dead) {
          all_arms_uncertain = false;  // a comptime-decided/dead path exists — not a clean mux
          // Emit NOTHING. emit_subtree_verbatim() used to copy the body in without
          // pass dispatch (so its assigns could not pollute the symbol table), but
          // KEEPING an unreachable body is wrong on its own: a call inside it
          // reaches tolg, which has no notion of reachability and reports it as
          // "call to undefined function". A `mod` guarded by a false generic
          // (`if ENABLED { leaf(a) }`) therefore failed to compile even though
          // nothing instantiates it. The paired cond is skipped below, so the
          // (cond, stmts) pairing stays intact.
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
          // An arm's own condition holds on its path (a trailing else has
          // none); every earlier condition of the chain is false there.
          const std::size_t own = is_trailing_else ? cond_refs.size() : cond_refs.size() - 1;
          for (std::size_t i = 0; i < cond_refs.size(); ++i) {
            if (!cond_refs[i].empty()) {
              for (auto& e : upasses) {
                e.pass->notify_arm_guard(cond_refs[i], /*negated=*/i != own);
              }
            }
          }
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
      cond_refs.emplace_back(lm->get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref ? std::string(lm->current_text()) : std::string{});
      if (!last_cond_true && !last_cond_false) {
        any_prior_uncertain = true;
      }
      if (already_matched || last_cond_false) {
        continue;  // the arm this cond guards is dead: drop cond + body as a UNIT
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
