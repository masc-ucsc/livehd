//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_tolg.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hlop/dlop.hpp"
#include "lnast_ntype.hpp"
#include "node_util.hpp"
#include "pass.hpp"

namespace {

using livehd::graph_util::create_const;
using livehd::graph_util::set_bits;
using livehd::graph_util::set_sign;
using livehd::graph_util::set_unsign;
using livehd::graph_util::setup_sink_by_name;

using Pin      = hhds::Pin_class;
using WriteMap = absl::flat_hash_map<std::string, Pin>;

// One lowered value: its driver pin + meaningful (unsigned) bit width `mw`.
// LGraph stores values signed; an unsigned N-bit value occupies N+1 pin bits
// (a leading 0 sign bit), which is what cgen's add_to_pin2var expects (it does
// `--bits` for unsigned dpins). We track `mw` (the N) and stamp `mw+1` bits.
struct Val {
  Pin     pin;
  int32_t mw{0};
};

// Bits to represent a non-negative value as unsigned (>=1).
[[nodiscard]] int32_t mw_of_val(int64_t v) {
  if (v <= 0) {
    return 1;
  }
  return static_cast<int32_t>(std::bit_width(static_cast<uint64_t>(v)));
}

// Resolve a func_call callee name against the lnast registry the
// same way the runner's lookup_callee does: exact top-module-name match, else
// a UNIQUE "<module>.<name>" suffix match.
[[nodiscard]] std::shared_ptr<Lnast> resolve_callee_lnast(std::string_view                           name,
                                                          const std::vector<std::shared_ptr<Lnast>>& registry) {
  std::shared_ptr<Lnast> exact;
  std::shared_ptr<Lnast> suffix_hit;
  int                    suffix_matches = 0;
  const std::string      suffix         = "." + std::string(name);
  for (const auto& ln : registry) {
    if (!ln) {
      continue;
    }
    auto n = ln->get_top_module_name();
    if (n == name) {
      exact = ln;
    } else if (n.size() > suffix.size() && n.substr(n.size() - suffix.size()) == suffix) {
      suffix_hit = ln;
      ++suffix_matches;
    }
  }
  if (exact) {
    return exact;
  }
  if (suffix_matches == 1) {
    return suffix_hit;
  }
  return nullptr;
}

// One pending time obligation: an asserted (min,max) landing
// interval on a value's driver pin (`is_sink=false`, from an undischarged
// `@[N]`) or on a GraphIO output sink (`is_sink=true`, the mod declared
// cycle / the pipe declared range). The combined checker verifies and REMOVES
// the paired pending_time attr; leftovers are compile errors.
struct Pending_rec {
  hhds::Pin_class pin;
  std::string     name;
  int64_t         min     = 0;
  int64_t         max     = 0;
  bool            is_sink = false;
};

// Shared phase-1 io+clock+reset GraphIO registration result. `clock_name` /
// `reset_name` are the graph inputs driving flop clock_pin / reset_pin (a
// declared input bound before minting, or the implicit "clock"/"reset"
// minted — the *_minted flags distinguish them so only minted pins get their
// width/sign stamped at first use). Empty names = the module needs none.
// `reset_neg` marks an active-low (…_n) module reset input.
struct Io_setup {
  std::string clock_name;
  bool        clock_minted = false;
  std::string reset_name;
  bool        reset_minted = false;
  bool        reset_neg    = false;
};

// Builds one hhds::Graph from one post-upass / post-SSA function-tree Lnast.
class Tolg {
public:
  // `registry`/`lib` resolve pipe/mod call sites to Sub instances.
  // `async_default` is the upass.reset_style=async elaboration flag;
  // a per-reg `:[sync=…]` attr beats it.
  Tolg(const std::shared_ptr<Lnast>& lnast, hhds::Graph* g, Io_setup io_setup, const uPass_tolg::Registry* registry,
       hhds::GraphLibrary* lib, bool async_default)
      : lnast_(lnast)
      , g_(g)
      , registry_(registry)
      , lib_(lib)
      , clock_name_(std::move(io_setup.clock_name))
      , clock_minted_(io_setup.clock_minted)
      , reset_name_(std::move(io_setup.reset_name))
      , reset_minted_(io_setup.reset_minted)
      , reset_neg_(io_setup.reset_neg)
      , reset_async_default_(async_default) {}

private:
  // Deferred stage-reg creation: a declare(reg)+stages does NOT
  // create the Flop immediately; the din store does, because only there the
  // effective depth is known (a Sub-fed stage reg realizes the DEFICIT
  // stage_N − callee_min; depth 0 = plain wire, no Flop at all). min/max ride
  // as raw const texts so "nil" stays distinguishable from 0.
  struct Pending_stage {
    std::string min_txt;
    std::string max_txt;
    Lnast_nid   decl_nid;  // for located diagnostics
  };

  // Per call-result name: the callee output's declared stages
  // interval + kind, recorded when the Sub is created and consumed by the
  // following stage-reg din store for the range check + deficit narrowing.
  struct Sub_out {
    int64_t          cmin    = 0;
    int64_t          cmax    = 0;  // pipe convention: 0 with cmin>=1 = unconstrained
    bool             is_pipe = false;
    hhds::Node_class node;  // to re-stamp time_range when stage[N] pins the pick
  };

public:
  void build() {
    // Module anchor: io-time cells (the to-positive port masks below)
    // are minted outside any statement — anchor them, and the graph io nodes
    // cgen reads for the module header, at the unit's `mod`/`comb` declaration
    // (stamped on the LNAST root by func_extract / the specialize clone).
    if (const auto id = lnast_->get_srcid(lnast_->get_root()); id != hhds::SourceId_invalid) {
      cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
    }

    // Inputs: from io_meta(). Unsigned inputs are wrapped in a to-positive
    // Get_mask so signed-declared ports read with their unsigned value (e.g.
    // a 3-bit `a` = 0b111 reads as 7, not -1) — mirrors lgyosys tposs.
    for (const auto& e : lnast_->io_meta().inputs) {
      auto raw = g_->get_input_pin(e.name);  // body driver pin for the port
      if (cur_srcid_ != hhds::SourceId_invalid && !raw.is_invalid()) {
        raw.get_master_node().attr(hhds::attrs::srcid).set(cur_srcid_);
      }
      int32_t mw = io_mw(e);
      if (e.kind == Io_kind::boolean) {
        set_bits(raw, 1);
        set_unsign(raw);
        record(e.name, raw, 1);
      } else if (mw <= 1) {
        set_bits(raw, 1);
        if (e.is_signed) {
          set_sign(raw);
          record(e.name, raw, 1);
        } else {
          // 1-bit unsigned: same to-positive contract as the wide branch
          // below, else `x + y` on 1-bit ports reads -1 (port decls are signed).
          set_sign(raw);
          record(e.name, to_positive(raw, 1), 1);
        }
      } else if (e.is_signed) {
        set_bits(raw, mw);
        set_sign(raw);
        record(e.name, raw, mw);
      } else {
        // Stamp width AND sign on the raw pin too: passes may reconnect
        // consumers straight to the port (cprop mask folds), and a bits-less
        // pin miscompiles in cgen (b[-1] sign-replicate). The raw port reads
        // SIGNED (cgen declares every port signed) — only the to-positive
        // wrapper provides the unsigned view, so mark raw signed to keep
        // sign-sensitive folds (cprop get_mask rule 4) away from it.
        set_bits(raw, mw);
        set_sign(raw);
        record(e.name, to_positive(raw, mw), mw);  // unsigned -> positive
      }
    }
    for (const auto& e : lnast_->io_meta().outputs) {
      out_names_.push_back(e.name);
      if (cur_srcid_ != hhds::SourceId_invalid) {
        if (auto sink = g_->get_output_pin(e.name); !sink.is_invalid()) {
          sink.get_master_node().attr(hhds::attrs::srcid).set(cur_srcid_);
        }
      }
    }

    // Body: lower the `stmts` child of `top`.
    auto top = lnast_->get_root();
    for (auto c = lnast_->get_first_child(top); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (Lnast_ntype::is_stmts(lnast_->get_type(c))) {
        lower_stmts(c);
      }
    }
    // Walk done: drop the statement anchor so finalize-time diagnostics are
    // unlocated rather than mislocated at whatever statement came last
    // (finalize_regs / create_stage_flop re-anchor per entity).
    cur_srcid_ = hhds::SourceId_invalid;

    // Wire every declared reg's din/enable/reset/initial now that
    // all stores and per-reg attr overrides have been seen.
    finalize_regs();
    // Sanity-check the per-memory port allocation.
    finalize_mems();
    // 2f-defer — bind any deferred field reads (forward references to a call
    // result lowered later, e.g. the defer feedback's `tmp.add`) now that every
    // call's Sub result exists; then wire each `.[defer]` buffer to its base's
    // final driver (incl. a reg's accumulated din).
    resolve_pending_tgets();
    resolve_defers();

    // Outputs: connect each output's bound driver to its graph output sink.
    for (const auto& name : out_names_) {
      auto sink = g_->get_output_pin(name);
      if (sink.is_invalid()) {
        continue;
      }
      auto it = pin_map_.find(name);
      if (it == pin_map_.end()) {
        warn_at(Lnast_nid{}, {"undriven-output", "type"}, "output '{}' not driven by body — wiring nil (0sb?)", name);
        sink.connect_driver(create_const(*g_, *Dlop::from_pyrope("0sb?")));
        continue;
      }
      sink.connect_driver(it->second);
    }

    // Lower the declared per-output intervals as pendings.
    stamp_output_pendings();

    // Guard — a stage declare whose din store never arrived would
    // silently drop the delay (and the value): hard error, never nil.
    if (!pending_stage_.empty()) {
      error_at(pending_stage_.begin()->second.decl_nid,
               "upass.tolg: stage reg '{}' in '{}' was declared but never stored — its delay would be silently lost",
               pending_stage_.begin()->first,
               lnast_->get_top_module_name());
    }
  }

private:
  // ── width / value helpers ───────────────────────────────────────────────────

  [[nodiscard]] static int32_t io_mw(const Lnast_io_entry& e) {
    if (e.kind == Io_kind::boolean) {
      return 1;
    }
    return e.bits > 0 ? static_cast<int32_t>(e.bits) : int32_t{1};
  }

  [[nodiscard]] Pin nil_pin() { return create_const(*g_, *Dlop::from_pyrope("0sb?")); }

  // To-positive-signed: Get_mask(x, -1) -> same bits, guaranteed non-negative,
  // marked unsigned with one extra (sign) bit. Lets unsigned values flow
  // through signed LGraph arithmetic correctly.
  [[nodiscard]] Pin to_positive(const Pin& src, int32_t mw) {
    auto node = make_node(Ntype_op::Get_mask);
    setup_sink_by_name(node, "a").connect_driver(src);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *Dlop::create_integer(-1)));
    auto drv = node.create_driver_pin(0);
    set_bits(drv, mw + 1);
    set_unsign(drv);
    return drv;
  }

  void record(std::string_view name, const Pin& pin, int32_t mw) {
    std::string key{name};
    pin_map_[key] = pin;
    mw_map_[key]  = mw;
    if (!branch_writes_.empty()) {
      branch_writes_.back()[key] = pin;
    }
    // 2f-defer — track the LOGICAL variable's most-recent driver (SSA versions
    // collapse to the root). Shadow keys (\x01din:/\x01en:) never name a defer
    // base, so skip them.
    if (!key.empty() && key.front() != '\x01') {
      const auto pos = key.find("___ssa_");
      logical_last_[pos == std::string::npos ? key : key.substr(0, pos)] = {pin, mw};
    }
  }

  [[nodiscard]] int32_t mw_lookup(std::string_view name) {
    auto it = mw_map_.find(std::string{name});
    return it != mw_map_.end() ? it->second : int32_t{1};
  }

  [[nodiscard]] Pin resolve(std::string_view name) {
    std::string key{name};
    auto        it = pin_map_.find(key);
    if (it != pin_map_.end()) {
      return it->second;
    }
    if (mem_map_.contains(key)) {
      // A memory name never binds a scalar pin; only indexed
      // accesses are supported. A warning (not an error): hardware regs may
      // legitimately be observed by other means (scan chain, future regref).
      warn_at(Lnast_nid{},
              {"memory-scalar-read", "unsupported"},
              "memory '{}' used as a scalar value — whole-array reads are unsupported, wiring nil (0sb?)",
              name);
      auto p        = nil_pin();
      pin_map_[key] = p;
      mw_map_[key]  = 1;
      return p;
    }
    warn_at(Lnast_nid{}, {"unresolved-ref", "name"}, "unresolved ref '{}' — wiring nil (0sb?)", name);
    auto p        = nil_pin();
    pin_map_[key] = p;
    mw_map_[key]  = 1;
    return p;
  }

  [[nodiscard]] Val leaf(const Lnast_nid& nid) {
    if (Lnast_ntype::is_const(lnast_->get_type(nid))) {
      auto    c  = Dlop::from_pyrope(lnast_->get_name(nid));
      int32_t mw = c->is_just_i64() ? mw_of_val(c->to_just_i64()) : std::max<int32_t>(1, static_cast<int32_t>(c->get_bits()));
      return {create_const(*g_, *c), mw};
    }
    auto name = lnast_->get_name(nid);
    return {resolve(name), mw_lookup(name)};
  }

  // Bind a computed result: stamp mw+1 unsigned bits and record name->(pin,mw).
  void bind_result(std::string_view name, const Pin& drv, int32_t mw) {
    int32_t m = mw > 0 ? mw : int32_t{1};
    set_bits(drv, m + 1);
    set_unsign(drv);
    record(name, drv, m);
  }

  // ── statement / node dispatch ───────────────────────────────────────────────

  void lower_stmts(const Lnast_nid& stmts) {
    for (auto c = lnast_->get_first_child(stmts); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      lower_node(c);
    }
  }

  // The current statement's SourceId, re-minted into the graph's
  // locator. Every cell make_node creates while lowering this statement is
  // stamped with it, so LGraph nodes resolve back to Pyrope source.
  hhds::SourceId cur_srcid_{0};

  // Anchor priority shared by error_at/warn_at: the given nid's SourceId,
  // falling back to the current statement's (re-minted into the graph).
  [[nodiscard]] livehd::diag::Diagnostic locate_record(const Lnast_nid& nid, livehd::diag::Severity sev, std::string_view code,
                                                       std::string_view category, std::string msg) const {
    livehd::diag::Span              span;
    std::vector<livehd::diag::Note> notes;
    if (!nid.is_invalid() && lnast_) {
      span  = lnast_->span_of(nid);
      notes = lnast_->notes_of(nid, "reached via this site");
    }
    if (span.is_null() && g_ != nullptr) {
      const auto rs = g_->source_locator().resolve_spans(cur_srcid_);
      span          = rs.primary;
      notes         = livehd::diag::notes_from(rs, "reached via this site");
    }
    return livehd::diag::Diagnostic{
        .severity = sev,
        .code     = std::string(code),
        .category = std::string(category),
        .pass     = "upass.tolg",
        .message  = std::move(msg),
        .span     = std::move(span),
        .notes    = std::move(notes),
    };
  }

  // Stage a located Diagnostic, then throw (Pass::error semantics — the
  // downstream flush seam emits the staged record exactly once, so the error
  // carries a resolved span instead of no location).
  template <typename... Args>
  [[noreturn]] void error_at(const Lnast_nid& nid, livehd::diag::Id id, std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::format(fmt, std::forward<Args>(args)...);
    livehd::diag::sink().stage(locate_record(nid, livehd::diag::Severity::error, id.code, id.category, msg));
    err_tracker::logger(msg);
    throw Eprp::parser_error(Pass::eprp, msg);
  }

  template <typename... Args>
  [[noreturn]] void error_at(const Lnast_nid& nid, std::format_string<Args...> fmt, Args&&... args) {
    error_at(nid, livehd::diag::Id{"tolg-error", "type"}, "{}", std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args>
  [[noreturn]] void error_here(std::format_string<Args...> fmt, Args&&... args) {
    error_at(Lnast_nid{}, "{}", std::format(fmt, std::forward<Args>(args)...));
  }

  // Non-fatal sibling: emit a located warning and continue lowering.
  template <typename... Args>
  void warn_at(const Lnast_nid& nid, livehd::diag::Id id, std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::format(fmt, std::forward<Args>(args)...);
    livehd::diag::sink().emit(locate_record(nid, livehd::diag::Severity::warning, id.code, id.category, std::move(msg)));
  }

  template <typename... Args>
  hhds::Node_class make_node(Args&&... args) {
    auto n = livehd::graph_util::create_typed_node(*g_, std::forward<Args>(args)...);
    if (cur_srcid_ != hhds::SourceId_invalid) {
      n.attr(hhds::attrs::srcid).set(cur_srcid_);
    }
    return n;
  }

  void lower_node(const Lnast_nid& nid) {
    const auto t           = lnast_->get_type(nid);
    // Anchor the statement: nested lower_* calls (and the cells they mint)
    // inherit it; statements without an id keep the enclosing one.
    const auto saved_srcid = cur_srcid_;
    if (const auto id = lnast_->get_srcid(nid); id != hhds::SourceId_invalid) {
      cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
    }
    lower_node_dispatch(nid, t);
    cur_srcid_ = saved_srcid;
  }

  void lower_node_dispatch(const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int t) {
    using N = Lnast_ntype;
    if (N::is_stmts(t)) {
      lower_stmts(nid);
    } else if (N::is_if(t)) {
      lower_if(nid);
    } else if (N::is_unique_if(t)) {
      lower_if(nid, /*unique=*/true);
    } else if (N::is_store(t)) {
      lower_store(nid);
    } else if (N::is_declare(t)) {
      lower_declare(nid);
    } else if (N::is_range(t)) {
      lower_range(nid);
    } else if (N::is_get_mask(t)) {
      lower_get_mask(nid);
    } else if (N::is_set_mask(t)) {
      lower_set_mask(nid);
    } else if (N::is_plus(t)) {
      lower_op(nid, Ntype_op::Sum, true, OpW::add);
    } else if (N::is_minus(t)) {
      lower_op(nid, Ntype_op::Sum, false, OpW::add);
    } else if (N::is_mult(t)) {
      lower_op(nid, Ntype_op::Mult, true, OpW::mul);
    } else if (N::is_bit_and(t) || N::is_log_and(t)) {
      lower_op(nid, Ntype_op::And, true, OpW::maxw);
    } else if (N::is_bit_or(t) || N::is_log_or(t)) {
      lower_op(nid, Ntype_op::Or, true, OpW::maxw);
    } else if (N::is_bit_xor(t)) {
      lower_op(nid, Ntype_op::Xor, true, OpW::maxw);
    } else if (N::is_eq(t)) {
      lower_op(nid, Ntype_op::EQ, true, OpW::boolw);
    } else if (N::is_lt(t)) {
      lower_op(nid, Ntype_op::LT, false, OpW::boolw);
    } else if (N::is_gt(t)) {
      lower_op(nid, Ntype_op::GT, false, OpW::boolw);
    } else if (N::is_shl(t)) {
      lower_op(nid, Ntype_op::SHL, false, OpW::shlw);
    } else if (N::is_sra(t)) {
      lower_op(nid, Ntype_op::SRA, false, OpW::firstw);
    } else if (N::is_sext(t)) {
      lower_sext(nid);
    } else if (N::is_div(t)) {
      lower_op(nid, Ntype_op::Div, false, OpW::firstw);
    } else if (N::is_bit_not(t) || N::is_log_not(t)) {
      lower_unary(nid, Ntype_op::Not);
    } else if (N::is_ne(t)) {
      lower_negated(nid, Ntype_op::EQ, true);
    } else if (N::is_le(t)) {
      lower_negated(nid, Ntype_op::GT, false);
    } else if (N::is_ge(t)) {
      lower_negated(nid, Ntype_op::LT, false);
    } else if (N::is_timecheck(t)) {
      // An `x@[N]` record the LNAST discharge could not decide
      // ("checked"-marked ones are already done): lower it to a PENDING
      // time-check attr on the named value's driver pin. The combined checker
      // verifies and removes it; a leftover pending is a compile error.
      lower_timecheck(nid);
    } else if (N::is_func_call(t)) {
      // Pipe/mod call sites lower to Ntype_op::Sub instances;
      // anything unresolvable (runtime wrap/sat, comb recursion) stays a
      // HARD error inside lower_func_call.
      lower_func_call(nid);
    } else if (N::is_attr_get(t)) {
      // `.[defer]` end-of-cycle read (2f-defer). Other attribute reads fold in
      // upass.attributes before tolg; an unhandled one keeps the warn inside.
      lower_attr_get(nid);
    } else if (N::is_attr_set(t)) {
      // Per-reg flop-attr overrides (reset_pin/sync/negreset/
      // initial); anything else keeps the unhandled warn below.
      lower_attr_set(nid);
    } else if (N::is_tuple_get(t)) {
      // An indexed read of a declared memory becomes a read port;
      // any other surviving tuple_get keeps the unhandled warn inside.
      lower_tuple_get(nid);
    } else if (N::is_tuple_add(t)) {
      // An all-const tuple literal is recorded as a potential array
      // initializer; anything else keeps the unhandled warn inside.
      lower_tuple_add(nid);
    } else if (N::is_tuple_concat(t)) {
      // `...` splice / `++` residue: comptime bookkeeping (the runner already
      // folded the spliced field wires upstream). Record the merged tuple.
      lower_tuple_concat(nid);
    } else if (N::is_for(t)) {
      // A `for` node reaching tolg means uPass_runner::unroll_for
      // could NOT unroll it: the iterable resolved to neither a comptime range
      // nor a known tuple shape. Pyrope `for` is comptime-only (must fully
      // unroll), so this is a user error (a runtime/unknown iterable), not a
      // silent miscompile. HARD error rather than the unhandled warn below.
      error_here(
          "upass.tolg: non-comptime `for` loop in '{}' — the iterable did not resolve to a comptime range "
          "or tuple, so the loop could not unroll (Pyrope for-loops are comptime-only and must fully unroll)",
          lnast_->get_top_module_name());
    } else {
      warn_at(Lnast_nid{}, {"unhandled-node", "unsupported"}, "unhandled node type '{}'", Lnast_ntype::to_sv(t));
    }
  }

  // attr_get(ref(dst), ref(base), const(attr)) — only `.[defer]` reaches tolg
  // (every other attribute read folds in upass.attributes). `x.[defer]` is the
  // end-of-cycle read: the value the base holds AFTER all in-cycle writes, as
  // same-cycle wiring (no flop). The base's final dpin is not known at the read
  // site (single walk), so emit a passthrough buffer whose OUTPUT consumers
  // read now and whose INPUT is connected to the base's last dpin in
  // resolve_defers() (after finalize_regs). A LHS `x.[defer] = …` write form is
  // rejected upstream in prp2lnast — only the RHS read reaches here. (2f-defer)
  void lower_attr_get(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto base = lnast_->get_sibling_next(dst);
    auto attr = base.is_invalid() ? base : lnast_->get_sibling_next(base);
    const std::string attr_name = attr.is_invalid() ? std::string{} : std::string(lnast_->get_name(attr));
    if (attr_name != "defer") {
      warn_at(nid, {"unhandled-node", "unsupported"}, "unhandled attribute read '.[{}]'", attr_name);
      return;
    }
    if (base.is_invalid() || !Lnast_ntype::is_ref(lnast_->get_type(base))) {
      error_here("upass.tolg: `.[defer]` must read a variable or field reference");
      return;
    }
    std::string base_name(lnast_->get_name(base));
    // Single-driver Or = a pure passthrough (cgen emits `out = a`). The input
    // sink stays open until resolve_defers() wires the base's final driver.
    auto buf  = make_node(Ntype_op::Or);
    auto out  = buf.create_driver_pin(0);
    set_bits(out, 1);  // provisional; restamped from the base width at resolve
    set_unsign(out);
    auto sink = setup_sink_by_name(buf, "a");
    record(lnast_->get_name(dst), out, 1);
    defer_cut_nids_.insert(buf.get_debug_nid());  // loop check cuts this node's in-edge
    defer_pends_.push_back(Defer_pend{.sink = sink, .out = out, .base = std::move(base_name), .src = nid});
  }

  // Re-resolve each deferred field read once the whole body (incl. later
  // calls) has lowered: by now the source name is a known Sub result / memory,
  // so lower_tuple_get binds the port driver. tget_final_ makes a genuinely
  // unresolvable one warn rather than defer again. (2f-defer)
  void resolve_pending_tgets() {
    tget_final_ = true;
    for (const auto& nid : pending_tgets_) {
      lower_tuple_get(nid);
    }
    pending_tgets_.clear();
  }

  // Wire every pending `.[defer]` buffer to its base's last dpin: a reg's
  // accumulated next-state (din) — never q — or a plain variable's final
  // combinational driver. A genuine end-of-cycle feedback that is actually a
  // combinational cycle (the base depends on the deferred value) surfaces as a
  // graph loop downstream, not here. (2f-defer)
  void resolve_defers() {
    for (auto& dp : defer_pends_) {
      Pin     drv;
      int32_t mw = 1;
      if (reg_map_.contains(dp.base)) {
        if (auto dit = pin_map_.find(din_key(dp.base)); dit != pin_map_.end()) {
          drv = dit->second;  // accumulated next-state (last in-cycle write)
          mw  = mw_lookup(din_key(dp.base));
        } else {
          drv = resolve(dp.base);  // never written this cycle → holds q
          mw  = mw_lookup(dp.base);
        }
      } else if (const auto pos = dp.base.find("___ssa_"); true) {
        // A plain variable's last dpin is its final SSA version's driver
        // (logical_last_), not the read-site version pin_map_ holds for `base`.
        const std::string logical = pos == std::string::npos ? dp.base : dp.base.substr(0, pos);
        if (auto lit = logical_last_.find(logical); lit != logical_last_.end()) {
          drv = lit->second.first;
          mw  = lit->second.second;
        } else {
          drv = resolve(dp.base);  // never written — resolve reports + wires nil
          mw  = mw_lookup(dp.base);
        }
      }
      dp.sink.connect_driver(drv);
      set_bits(dp.out, mw);
    }
  }

  // attr_set(ref(target), const(key), value) — record the per-reg
  // flop-attr overrides consumed by finalize_regs. `:[reset_pin=…, sync=…,
  // negreset, initial=N]` (04b-attributes.md); a per-reg `sync` beats the
  // upass.reset_style flag; `reset_pin=false` opts out of reset (only valid
  // with a nil init).
  void lower_attr_set(const Lnast_nid& nid) {
    auto tgt = lnast_->get_first_child(nid);
    if (tgt.is_invalid()) {
      return;
    }
    auto key_n = lnast_->get_sibling_next(tgt);
    if (key_n.is_invalid()) {
      return;
    }
    auto it = reg_info_.find(std::string(lnast_->get_name(tgt)));
    if (it == reg_info_.end()) {
      // Not a flop reg (yet): stash for a later array/memory declare (the
      // importer emits the attr_set before the declare it qualifies).
      auto  key_sv = lnast_->get_name(key_n);
      auto  val_n0 = lnast_->get_sibling_next(key_n);
      auto  val_sv = val_n0.is_invalid() ? std::string_view{"true"} : std::string_view(lnast_->get_name(val_n0));
      pending_attrs_[std::string(lnast_->get_name(tgt))][std::string(key_sv)] = std::string(val_sv);
      return;
    }
    auto& info  = it->second;
    auto  key   = lnast_->get_name(key_n);
    auto  val_n = lnast_->get_sibling_next(key_n);
    auto  val   = val_n.is_invalid() ? std::string_view{"true"} : std::string_view(lnast_->get_name(val_n));
    if (key == "reset_pin") {
      info.reset_pin_name = std::string(val);
    } else if (key == "clock_pin") {
      info.clock_pin_name = std::string(val);
    } else if (key == "posclk") {
      info.has_posclk = true;
      info.posclk_val = val != "false" && val != "0";
    } else if (key == "sync") {
      info.has_sync = true;
      info.sync_val = val != "false" && val != "0";
    } else if (key == "negreset") {
      info.negreset = val != "false" && val != "0";
    } else if (key == "initial") {
      info.initial_txt = std::string(val);
    } else if (key == "type" || key == "comptime") {
      // storage-class markers — already consumed by the declare
    } else {
      warn_at(tgt,
              {"reg-attr-not-lowered", "unsupported"},
              "reg '{}' attribute '{}' not lowered (attribute not in the lowered set)",
              lnast_->get_name(tgt),
              key);
    }
  }

  // Wire each declared reg's din / enable / reset_pin / initial /
  // async / negreset after the whole body has been lowered (stores and attr
  // overrides arrive in any order relative to the declare).
  void finalize_regs() {
    for (const auto& name : reg_order_) {
      auto& info = reg_info_.at(name);
      auto& flop = info.flop;

      // Runs after the walk: anchor this reg's diagnostics at its declaration
      // instead of whatever statement the walk ended on.
      cur_srcid_ = hhds::SourceId_invalid;
      if (const auto id = lnast_->get_srcid(info.decl_nid); id != hhds::SourceId_invalid) {
        cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
      }

      // din: the final shadow value (last-write-wins; branch writes arrive
      // pre-muxed). A never-written reg holds its value forever: din <- q.
      auto q = flop.create_driver_pin(0);
      Pin  din;
      if (auto dit = pin_map_.find(din_key(name)); dit != pin_map_.end()) {
        din = dit->second;
      } else {
        din = q;
      }
      setup_sink_by_name(flop, "din").connect_driver(din);

      if (info.is_latch) {
        // A latch (Ntype_op::Latch) has no clock/reset/posclk: wire only its
        // enable (the transparency condition; cgen process_latch emits
        // `always @* if(enable) q = din`). din already carries the if-merge
        // `cond ? d : q`. Skip the const-false (never-written) enable.
        if (info.decl_mw == 0) {
          auto    dit = mw_map_.find(din_key(name));
          int32_t mw  = dit != mw_map_.end() ? dit->second : int32_t{1};
          set_bits(q, mw + 1);
          set_unsign(q);
          mw_map_[name] = mw;
        }
        if (auto eit = pin_map_.find(en_key(name)); eit != pin_map_.end()) {
          const auto  en       = eit->second;
          const auto  en_nid   = en.get_master_node().get_debug_nid();
          const bool  is_false = en_false_valid_ && en_nid == en_false_pin_.get_master_node().get_debug_nid();
          if (!is_false) {
            setup_sink_by_name(flop, "enable").connect_driver(en);
          }
        }
        continue;
      }

      // clock: explicit clock_pin=NAME beats the implicit/shared clock input.
      // A named clock is usually a module input (clk_i), but can also be an
      // internal/derived wire — e.g. a gated clock (a clock-gate cell's
      // `clk & en_latch` output) feeding a flop. Check has_input FIRST (a clock
      // input is NOT in pin_map_; an unrelated same-named signal might be, which
      // is why pin_map_-first is wrong), then fall back to pin_map_ for the
      // internal-wire case. get_input_pin would assert on a non-input.
      if (!info.clock_pin_name.empty()) {
        if (g_->get_io()->has_input(info.clock_pin_name)) {
          setup_sink_by_name(flop, "clock_pin").connect_driver(g_->get_input_pin(info.clock_pin_name));
        } else if (auto cn = std::string(info.clock_pin_name); pin_map_.contains(cn)) {
          setup_sink_by_name(flop, "clock_pin").connect_driver(pin_map_.at(cn));
        } else {
          error_here("upass.tolg: reg '{}' names clock_pin '{}' but '{}' has no such input/wire",
                     name,
                     info.clock_pin_name,
                     lnast_->get_top_module_name());
          continue;
        }
      } else if (!clock_name_.empty()) {
        setup_sink_by_name(flop, "clock_pin").connect_driver(clock_pin());
      } else {
        warn_at(info.decl_nid, {"no-clock", "time"}, "reg '{}' has no clock input to bind", name);
      }
      if (info.has_posclk && !info.posclk_val) {
        setup_sink_by_name(flop, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
      }

      // q width: untyped regs take the final din width (mw+1 unsigned).
      if (info.decl_mw == 0) {
        auto    dit = mw_map_.find(din_key(name));
        int32_t mw  = dit != mw_map_.end() ? dit->second : int32_t{1};
        set_bits(q, mw + 1);
        set_unsign(q);
        mw_map_[name] = mw;
      }

      // enable: still the seeded false const => never written (no enable);
      // the true const => unconditionally written (no enable needed); any
      // other pin is the OR-of-conditions mux chain.
      if (auto eit = pin_map_.find(en_key(name)); eit != pin_map_.end()) {
        const auto en       = eit->second;
        const auto en_nid   = en.get_master_node().get_debug_nid();
        const bool is_true  = en_true_valid_ && en_nid == en_true_pin_.get_master_node().get_debug_nid();
        const bool is_false = en_false_valid_ && en_nid == en_false_pin_.get_master_node().get_debug_nid();
        if (!is_true && !is_false) {
          setup_sink_by_name(flop, "enable").connect_driver(en);
        }
      }

      // Reset wiring. Effective init: an explicit `initial=N` attr overrides
      // the declare's [value]; "nil" (or absent) = NO reset (confirmed
      // 2026-06-07 ruling).
      const std::string init     = !info.initial_txt.empty() ? info.initial_txt : info.init_txt;
      const bool        has_init = !init.empty() && init != "nil";
      const bool        rp_false = info.reset_pin_name == "false";
      if (rp_false && has_init) {
        error_here("upass.tolg: reg '{}' has a non-nil initializer but `reset_pin=false` — drop the init or the override", name);
        return;
      }
      const bool wants_reset = (has_init || (!info.reset_pin_name.empty() && !rp_false)) && !rp_false;
      if (!wants_reset) {
        continue;
      }

      Pin  rpin;
      bool neg = info.negreset;
      if (!info.reset_pin_name.empty()) {
        // Usually a graph input, but a reset synchronizer drives it from a
        // DERIVED module-level signal — wire from that signal's driver instead.
        // (get_input_pin ASSERTS on a non-input name; gate on has_input first.)
        if (g_->get_io()->has_input(info.reset_pin_name)) {
          rpin = g_->get_input_pin(info.reset_pin_name);
        } else {
          // Derived reset signal: its FINAL combinational driver lives in
          // logical_last_ (the last SSA version), NOT pin_map_[name] that
          // resolve() checks (which holds the read-site version, or nothing).
          std::string base = info.reset_pin_name;
          if (auto p = base.find("___ssa_"); p != std::string::npos) {
            base.resize(p);
          }
          if (auto lit = logical_last_.find(base); lit != logical_last_.end()) {
            rpin = lit->second.first;
          } else {
            rpin = resolve(info.reset_pin_name);
          }
        }
        if (info.reset_pin_name.ends_with("_n")) {
          neg = true;
        }
      } else if (!reset_name_.empty()) {
        rpin = reset_pin();
        if (reset_neg_) {
          neg = true;
        }
      } else {
        error_here("upass.tolg: reg '{}' has a reset value but '{}' has no reset input (setup_io bug)",
                   name,
                   lnast_->get_top_module_name());
        return;
      }
      setup_sink_by_name(flop, "reset_pin").connect_driver(rpin);
      if (has_init) {
        // The reset value must be a compile-time constant. A body-`reg`'s
        // non-literal init is caught at the declare (lower_declare errors on
        // a ref init); an output-reg `-> (reg q = expr)` stringifies its
        // initializer, so a malformed parse can still arrive here — reject it
        // rather than deref a null Dlop.
        auto iv = Dlop::from_pyrope(init);
        if (!iv) {
          error_here("upass.tolg: reg '{}' reset/initial value '{}' is not a compile-time constant", name, init);
          return;
        }
        setup_sink_by_name(flop, "initial").connect_driver(create_const(*g_, *iv));
      }
      if (neg) {
        setup_sink_by_name(flop, "negreset").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
      }
      // sync-vs-async: per-reg `sync` attr beats the elaboration flag.
      const bool async = info.has_sync ? !info.sync_val : reset_async_default_;
      if (async) {
        setup_sink_by_name(flop, "async").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
      }
    }
  }

  // store(ref(lhs), value) — scalar assignment / alias. A store whose lhs is
  // a declared reg connects the value to the Flop's din instead of rebinding
  // the name (reads keep seeing the q pin — Verilog `<=` semantics).
  void lower_store(const Lnast_nid& nid) {
    auto lhs = lnast_->get_first_child(nid);
    if (lhs.is_invalid()) {
      return;
    }
    auto rhs = lnast_->get_sibling_next(lhs);
    if (rhs.is_invalid()) {
      return;
    }
    // 1a-mem — an indexed store to a declared memory becomes a write port;
    // the 2-child whole-array form is the mut/const array initializer.
    if (auto mit = mem_map_.find(std::string(lnast_->get_name(lhs))); mit != mem_map_.end()) {
      if (lnast_->get_sibling_next(rhs).is_invalid()) {
        lower_mem_init_store(rhs, lnast_->get_name(lhs), mit->second);
      } else {
        lower_mem_store(lhs, lnast_->get_name(lhs), mit->second);
      }
      return;
    }
    if (!lnast_->get_sibling_next(rhs).is_invalid()) {
      warn_at(lhs,
              {"tuple-store-unsupported", "unsupported"},
              "tuple/field store not handled yet (lhs '{}')",
              lnast_->get_name(lhs));
      return;
    }
    auto lhs_name = lnast_->get_name(lhs);
    // Deferred stage-reg creation: the din store knows the
    // effective depth (deficit narrowing against a Sub callee; 0 = wire).
    if (auto pit = pending_stage_.find(lhs_name); pit != pending_stage_.end()) {
      auto pending = pit->second;
      pending_stage_.erase(pit);
      create_stage_flop(lhs_name, pending, rhs);
      return;
    }
    if (reg_map_.contains(lhs_name)) {
      // A store to a declared reg is a next-state write: rebind the
      // SHADOW din/enable keys (never the name — reads keep seeing q, Verilog
      // `<=` semantics). The branch-mux machinery merges conditional writes
      // into last-write-wins din + OR-of-conditions enable; finalize_regs()
      // wires the final pins.
      if (!reg_info_.contains(std::string(lhs_name))) {
        // A stage reg (created by its one din store) has no finalize record —
        // a second store would be silently lost.
        error_here("upass.tolg: stage reg '{}' stored more than once in '{}'", lhs_name, lnast_->get_top_module_name());
        return;
      }
      auto v = leaf(rhs);
      record(din_key(lhs_name), v.pin, v.mw);
      record(en_key(lhs_name), en_const(true), 1);
      return;
    }
    // 1a-mem — a plain `name = <tuple-literal-ref>` / `name = <__memory
    // result>` aliases the record instead of binding a scalar pin (the
    // literal/result has no pin; its consumers resolve through the record).
    if (Lnast_ntype::is_ref(lnast_->get_type(rhs))) {
      const std::string rhs_name(lnast_->get_name(rhs));
      // Copy BEFORE inserting: operator[] may rehash and invalidate the
      // found iterator (the type_info_map rehash-invalidation UAF all over again).
      if (auto tit = tuple_recs_.find(rhs_name); tit != tuple_recs_.end()) {
        auto rec_copy                      = tit->second;
        tuple_recs_[std::string(lhs_name)] = std::move(rec_copy);
        return;
      }
      if (auto mrt = mem_results_.find(rhs_name); mrt != mem_results_.end()) {
        auto rec_copy                       = mrt->second;
        mem_results_[std::string(lhs_name)] = rec_copy;
        return;
      }
      // A multi-output call result bound to a named var (`mut tmp = add_sub(…)`):
      // alias the Sub_result so `tmp.add`/`tmp.sub` resolve through the named
      // var, not just the call's result temp. (Copy before insert — operator[]
      // may rehash and invalidate the found iterator.)
      if (auto srt = sub_results_.find(rhs_name); srt != sub_results_.end()) {
        auto rec_copy                       = srt->second;
        sub_results_[std::string(lhs_name)] = std::move(rec_copy);
        return;
      }
    }
    auto v = leaf(rhs);
    record(lhs_name, v.pin, v.mw);
  }

  // declare(ref(name), type, const("reg")) [+ stages(min,max)]:
  // create the Flop cell (the first Flop on the Pyrope->LG path). The name
  // binds to the q pin so subsequent READS see q; the din store above wires
  // the input. Inserted pipeline flops carry the declared stages range on
  // the pipe_min/pipe_max comptime pins (LG pass1 narrows them by sigma
  // later). A pure-comb partition's flop is the no-reset shape — reset_pin/
  // initial/async/enable stay unconnected; posclk unset reads as posedge.
  void lower_declare(const Lnast_nid& nid) {
    auto name_nid = lnast_->get_first_child(nid);
    if (name_nid.is_invalid()) {
      return;
    }
    auto       type_nid = lnast_->get_sibling_next(name_nid);
    auto       mode_nid = type_nid.is_invalid() ? type_nid : lnast_->get_sibling_next(type_nid);
    auto       mode     = mode_nid.is_invalid() || !Lnast_ntype::is_const(lnast_->get_type(mode_nid))
                              ? std::string_view{}
                              : std::string_view(lnast_->get_name(mode_nid));
    // 1a-mem — an array-typed declare is a Memory cell: reg → clocked async
    // memory; a mut/const array that survived to tolg (runtime-indexed) → a
    // comb type=2 array / ROM. Never a Flop, never a plain binding.
    const bool is_reg   = mode == "reg" || mode.starts_with("reg ");
    const bool is_latch = mode == "latch";  // level-sensitive latch (din+enable, no clock)
    if (!type_nid.is_invalid() && Lnast_ntype::is_comp_type_array(lnast_->get_type(type_nid))
        && (is_reg || mode == "mut" || mode == "const" || mode.starts_with("mut ") || mode.starts_with("const "))) {
      lower_mem_declare(name_nid, type_nid, mode_nid, /*is_array=*/!is_reg);
      return;
    }

    if (!is_reg && !is_latch) {
      // mut/const/type declares carry no graph payload here (values arrive
      // via their stores); nothing to lower.
      return;
    }

    // stages(min,max) trailing child — DEFER the Flop creation to
    // the din store, which knows the effective depth (a Sub-fed stage reg
    // realizes the deficit stage_N − callee_min; depth 0 = wire, no Flop).
    // Safe because every emitted shape stores immediately after the declare
    // (prp2lnast enforces stage-needs-value; the pipe upass always emits the
    // din store) — no read can occur in between.
    for (auto c = lnast_->get_sibling_next(mode_nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (!Lnast_ntype::is_stages(lnast_->get_type(c))) {
        continue;
      }
      auto mn = lnast_->get_first_child(c);
      if (mn.is_invalid()) {
        break;
      }
      auto          mx = lnast_->get_sibling_next(mn);
      Pending_stage p;
      p.min_txt                                               = std::string(lnast_->get_name(mn));
      p.max_txt                                               = mx.is_invalid() ? p.min_txt : std::string(lnast_->get_name(mx));
      p.decl_nid                                              = nid;
      pending_stage_[std::string(lnast_->get_name(name_nid))] = std::move(p);
      return;
    }

    // Plain reg (no stages) — state/stage register: create the Flop
    // now; the name binds to q (reads see q), stores rebind the shadow
    // din/enable keys, finalize_regs() wires the pins. The declared type
    // gives q's width up front (a counter's `r + 1` read needs it before any
    // din store); untyped regs restamp from the final din width.
    auto flop = make_node(is_latch ? Ntype_op::Latch : Ntype_op::Flop);
    // clock wiring happens in finalize_regs (a clock_pin/posclk attr_set may
    // arrive after the declare). A latch has no clock/reset — finalize_regs
    // wires only its din + enable.
    auto name = lnast_->get_name(name_nid);
    auto q    = flop.create_driver_pin(0);
    // Keep the register's RTL name on q. The lnast path otherwise leaves the
    // flop unnamed (cgen then synthesizes `flop_<nid>`), losing the identity
    // that yosys-slang preserves — pass/lec needs it to put corresponding flops
    // of the two front-ends in 1:1 correspondence (and the emitted Verilog reads
    // better). Strip any SSA suffix so it matches the logical (declared) name.
    {
      std::string base{name};
      if (auto p = base.find("___ssa_"); p != std::string::npos) {
        base.resize(p);
      }
      if (!base.empty()) {
        livehd::graph_util::set_pin_name(q, base);
      }
    }

    Reg_info info;
    info.flop     = flop;
    info.is_latch = is_latch;
    info.decl_nid = nid;
    if (!type_nid.is_invalid()) {
      std::tie(info.decl_mw, info.is_signed) = declared_width(type_nid);
    }
    // The declare's optional trailing [value] child is the
    // power-on/reset value (a const after declare-folding; an unresolved ref
    // means a runtime initializer, which a reset value cannot be).
    for (auto c = lnast_->get_sibling_next(mode_nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      const auto ct = lnast_->get_type(c);
      if (Lnast_ntype::is_const(ct)) {
        info.init_txt = std::string(lnast_->get_name(c));
        break;
      }
      if (Lnast_ntype::is_ref(ct)) {
        error_here("upass.tolg: reg '{}' initializer is not a compile-time constant — a reset value must be comptime", name);
        return;
      }
    }

    if (info.decl_mw > 0) {
      if (info.is_signed) {
        set_bits(q, info.decl_mw);
        set_sign(q);
      } else {
        set_bits(q, info.decl_mw + 1);
        set_unsign(q);
      }
      record(name, q, info.decl_mw);
    } else {
      record(name, q, 1);  // provisional width; finalize_regs restamps from din
    }
    reg_map_.emplace(std::string(name), flop);
    reg_order_.emplace_back(name);
    reg_info_.emplace(std::string(name), std::move(info));
    plain_reg_flops_[flop.get_debug_nid()] = std::string(name);
    // Seed the enable shadow false: a store rebinds it true, the branch-mux
    // machinery turns conditional writes into the OR-of-conditions chain.
    record(en_key(name), en_const(false), 1);
  }

  // Per-reg lowering state recorded at the declare; consumed by
  // finalize_regs() after every store/attr_set has been seen.
  struct Reg_info {
    hhds::Node_class flop;
    Lnast_nid        decl_nid;
    std::string      init_txt;       // declare [value] child; "" = none, "nil" = explicit no-reset
    int32_t          decl_mw   = 0;  // declared type width; 0 = untyped
    bool             is_signed = false;
    // Per-reg flop-attr overrides (04b-attributes.md): a per-reg `sync` beats
    // the upass.reset_style flag; `reset_pin=false` opts out of reset.
    std::string      reset_pin_name;  // explicit reset_pin=NAME / "false"
    std::string      clock_pin_name;  // explicit clock_pin=NAME (beats implicit clock)
    bool             has_posclk = false;
    bool             posclk_val = true;  // false = negedge clock
    bool             has_sync = false;
    bool             sync_val = true;
    bool             negreset = false;
    std::string      initial_txt;  // explicit initial=N (overrides init_txt)
    bool             is_latch = false;  // mode "latch": Ntype_op::Latch, wire din+enable only
  };

  // Shadow pin_map_ keys for a reg's next-state value and write-enable. The
  // \x01 prefix cannot collide with user identifiers or `___N` temps.
  [[nodiscard]] static std::string din_key(std::string_view n) {
    return std::string(
               "\x01"
               "din:")
        .append(n);
  }
  [[nodiscard]] static std::string en_key(std::string_view n) {
    return std::string(
               "\x01"
               "en:")
        .append(n);
  }

  // Cached 1/0 const pins for the enable shadow (node identity doubles as the
  // "still unconditionally true/false" test in finalize_regs).
  [[nodiscard]] Pin en_const(bool v) {
    auto& pin   = v ? en_true_pin_ : en_false_pin_;
    auto& valid = v ? en_true_valid_ : en_false_valid_;
    if (!valid) {
      pin   = create_const(*g_, *Dlop::create_integer(v ? 1 : 0));
      valid = true;
    }
    return pin;
  }

  // ── 1a-mem: array-typed reg → Ntype_op::Memory ──────────────────────────────
  // One Memory cell per declared `reg name:[N]T`; one write port per store
  // site and one read port per tuple_get site (no port merging here — that is
  // a future LG pass). Per-port sink pids stride by 12 (graph/cell.cpp); the
  // r-th read port's data comes out on driver pid (n_wr_total + r), so the
  // write-site count is pre-scanned at the declare.
  struct Mem_info {
    hhds::Node_class             node;
    int64_t                      size        = 0;  // total entries (∏dims)
    std::vector<int64_t>         dims;             // outer dim first; size 1 for a flat array
    int32_t                      elem_mw     = 0;  // element max-value width
    bool                         elem_signed = false;
    bool                         is_array    = false;  // type=2: mut/const array (no clock, no persistence)
    bool                         is_pub      = false;  // pub reg: a remote regref may attach accesses — no diagnostics
    bool                         init_wired  = false;
    int                          n_wr_total  = 0;  // user sites + restore ports (fixes dout pids)
    int                          n_user_wr   = 0;  // pre-scanned program write sites
    int                          wr_next     = 0;
    int                          rd_next     = 0;
    // 1a-mem reset-restore — per-entry init values: when a concrete-init reg
    // array coexists with a bound reset, finalize_mems() adds one restore
    // write port per entry (addr=k, din=init[k], enable=reset) and gates the
    // user ports' enables with !reset. Restore ports stay OUT of the fwd
    // mask: a read during reset returns the committed (old) contents.
    std::vector<spool_ptr<Dlop>> restore_vals;
  };

  static constexpr int kMemPortStride = 12;  // == Memory sink count, graph/cell.cpp

  // Branch path conditions for memory write enables. Maintained by lower_if
  // only while a memory exists (mem_map_ non-empty); each entry is the full
  // path condition (already ANDed with the enclosing one). Empty stack =
  // unconditional.
  [[nodiscard]] Pin current_path_cond() const { return path_cond_.empty() ? Pin{} : path_cond_.back(); }

  // a AND b as a 1-bit unsigned pin; an invalid operand means "true".
  [[nodiscard]] Pin and2(const Pin& a, const Pin& b) {
    if (a.is_invalid()) {
      return b;
    }
    if (b.is_invalid()) {
      return a;
    }
    auto node = make_node(Ntype_op::And);
    node.create_sink_pin(0).connect_driver(a);
    node.create_sink_pin(0).connect_driver(b);
    auto d = node.create_driver_pin(0);
    set_bits(d, 1);
    set_unsign(d);
    return d;
  }

  // Bitwise NOT of a 1-bit condition (LSB carries the logical value).
  [[nodiscard]] Pin not1(const Pin& a) {
    auto node = make_node(Ntype_op::Not);
    setup_sink_by_name(node, "a").connect_driver(a);
    auto d = node.create_driver_pin(0);
    set_bits(d, 1);
    set_unsign(d);
    return d;
  }

  // A 1-bit condition shifted to one-hot position `amount` (unique-if
  // selector packing). The value reaches 1<<amount (amount+1 magnitude
  // bits); LiveHD `bits` includes the sign bit, hence amount+2.
  [[nodiscard]] Pin shl1_by(const Pin& a, int amount) {
    if (amount == 0) {
      return a;
    }
    auto node = make_node(Ntype_op::SHL);
    setup_sink_by_name(node, "a").connect_driver(a);
    setup_sink_by_name(node, "b").connect_driver(create_const(*g_, *Dlop::create_integer(amount)));
    auto d = node.create_driver_pin(0);
    set_bits(d, amount + 2);
    set_unsign(d);
    return d;
  }

  // Row-major flat address for a chained index list over `mi.dims`:
  // addr = ((i0*D1 + i1)*D2 + i2)… (Horner). Const indices fold at build
  // time; a runtime index materializes the accumulator and emits Mult/Sum
  // cells (widths mirror lower_op: mul = sum of operand mws, add = max+1).
  // Returns an invalid Pin after reporting (index-arity mismatch, non-integer
  // index, field access).
  [[nodiscard]] Pin flatten_mem_addr(const Mem_info& mi, const std::vector<Lnast_nid>& idxs, std::string_view name) {
    if (idxs.size() != mi.dims.size()) {
      error_here("upass.tolg: memory '{}' has {} dimension(s) but the access supplies {} index(es)",
                 name,
                 mi.dims.size(),
                 idxs.size());
      return {};
    }
    // A const index must be an integer — a string key would be a field
    // access, which memories don't have.
    auto const_index_of = [&](const Lnast_nid& nid, std::optional<int64_t>& out) -> bool {
      if (!Lnast_ntype::is_const(lnast_->get_type(nid))) {
        return true;  // runtime ref — resolved through leaf()
      }
      auto v = Dlop::from_pyrope(lnast_->get_name(nid));
      if (!v || !v->is_just_i64()) {
        error_here("upass.tolg: memory '{}' index '{}' is not an integer — field access on a memory is not supported",
                   name,
                   lnast_->get_name(nid));
        return false;
      }
      out = v->to_just_i64();
      return true;
    };

    std::optional<int64_t> acc_c;
    Pin                    acc_p{};
    int32_t                acc_mw = 0;
    if (!const_index_of(idxs[0], acc_c)) {
      return {};
    }
    if (!acc_c) {
      auto v = leaf(idxs[0]);
      acc_p  = v.pin;
      acc_mw = v.mw;
    }
    for (size_t k = 1; k < idxs.size(); ++k) {
      const int64_t          d = mi.dims[k];
      std::optional<int64_t> ic;
      if (!const_index_of(idxs[k], ic)) {
        return {};
      }
      if (acc_c && ic) {
        acc_c = *acc_c * d + *ic;
        continue;
      }
      if (acc_c) {  // runtime index joins a const accumulator
        acc_p  = create_const(*g_, *Dlop::create_integer(*acc_c));
        acc_mw = mw_of_val(*acc_c);
        acc_c.reset();
      }
      if (d != 1) {
        auto mul = make_node(Ntype_op::Mult);
        setup_sink_by_name(mul, "a").connect_driver(acc_p);
        setup_sink_by_name(mul, "a").connect_driver(create_const(*g_, *Dlop::create_integer(d)));
        auto md = mul.create_driver_pin(0);
        acc_mw += mw_of_val(d);
        set_bits(md, acc_mw + 1);
        set_unsign(md);
        acc_p = md;
      }
      Pin     ip{};
      int32_t imw = 0;
      if (ic) {
        if (*ic == 0) {
          continue;  // + 0 — skip the Sum
        }
        ip  = create_const(*g_, *Dlop::create_integer(*ic));
        imw = mw_of_val(*ic);
      } else {
        auto v = leaf(idxs[k]);
        ip  = v.pin;
        imw = v.mw;
      }
      auto add = make_node(Ntype_op::Sum);
      setup_sink_by_name(add, "a").connect_driver(acc_p);
      setup_sink_by_name(add, "a").connect_driver(ip);
      auto ad = add.create_driver_pin(0);
      acc_mw  = std::max(acc_mw, imw) + 1;
      set_bits(ad, acc_mw + 1);
      set_unsign(ad);
      acc_p = ad;
    }
    if (acc_c) {
      return create_const(*g_, *Dlop::create_integer(*acc_c));
    }
    return acc_p;
  }

  // Pre-scan: indexed stores (store(ref name, idx, val)) anywhere in the tree.
  [[nodiscard]] int count_mem_write_sites(std::string_view name) const {
    int                                   n    = 0;
    std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
      if (Lnast_ntype::is_store(lnast_->get_type(nid))) {
        auto c0 = lnast_->get_first_child(nid);
        if (!c0.is_invalid() && lnast_->get_name(c0) == name) {
          auto c1 = lnast_->get_sibling_next(c0);
          if (!c1.is_invalid() && !lnast_->get_sibling_next(c1).is_invalid()) {
            ++n;
          }
        }
      }
      for (auto c = lnast_->get_first_child(nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
        walk(c);
      }
    };
    walk(lnast_->get_root());
    return n;
  }

  // declare(ref name, comp_type_array(elem_type, const '[N]'), const mode
  // [, init]) — two flavors sharing one lowering:
  //  * reg  → async memory (type=0, fwd=1, 0-cycle read): writes commit at
  //    the cycle edge, same-cycle reads see them through forwarding. Only a
  //    nil/0sb? initializer is accepted in this slice (no reset hardware;
  //    the reset-sweep FSM is a later slice).
  //  * mut/const → comb array (type=2, no clock, no cross-cycle
  //    persistence): the per-cycle default is the init contents (the
  //    whole-array store wires the `init` pin); a const array with runtime
  //    reads is a ROM (init + read ports only).
  void lower_mem_declare(const Lnast_nid& name_nid, const Lnast_nid& type_nid, const Lnast_nid& mode_nid, bool is_array) {
    auto name     = lnast_->get_name(name_nid);
    auto elem_nid = lnast_->get_first_child(type_nid);
    auto len_nid  = elem_nid.is_invalid() ? elem_nid : lnast_->get_sibling_next(elem_nid);
    if (elem_nid.is_invalid() || len_nid.is_invalid()) {
      error_here("upass.tolg: memory '{}' array type is missing its element type or size", name);
      return;
    }
    // Collect the dimension chain — each comp_type_array level is
    // (elem | nested comp_type_array, const '[N]'), nested OUTER dim first
    // (`[4][8]u8` → top len is '[4]'). The flat entry layout is row-major
    // (matrix_partial.prp contract): index (i,j) over dims (D0,D1) lands at
    // flat address i*D1 + j.
    std::vector<int64_t> dims;
    while (true) {
      // The size const's text is the raw '[N]' annotation — strip the brackets.
      auto len_txt = std::string(lnast_->get_name(len_nid));
      if (len_txt.size() >= 2 && len_txt.front() == '[' && len_txt.back() == ']') {
        len_txt = len_txt.substr(1, len_txt.size() - 2);
      }
      int64_t d = 0;
      if (auto c = Dlop::from_pyrope(len_txt); c && c->is_just_i64()) {
        d = c->to_just_i64();
      }
      if (d <= 0) {
        error_here("upass.tolg: memory '{}' size '{}' is not a positive comptime constant", name, lnast_->get_name(len_nid));
        return;
      }
      dims.emplace_back(d);
      if (!Lnast_ntype::is_comp_type_array(lnast_->get_type(elem_nid))) {
        break;
      }
      auto inner_elem = lnast_->get_first_child(elem_nid);
      auto inner_len  = inner_elem.is_invalid() ? inner_elem : lnast_->get_sibling_next(inner_elem);
      if (inner_elem.is_invalid() || inner_len.is_invalid()) {
        error_here("upass.tolg: memory '{}' array type is missing its element type or size", name);
        return;
      }
      elem_nid = inner_elem;
      len_nid  = inner_len;
    }
    auto [elem_mw, elem_signed] = declared_width(elem_nid);
    if (elem_mw == 0) {
      error_here("upass.tolg: memory '{}' element type must be a sized integer or bool", name);
      return;
    }
    int64_t size = 1;
    for (auto d : dims) {
      size *= d;
    }
    // reg initializer — same treatment as a mut array (reg and not-reg
    // initialize alike): a concrete value becomes POWER-ON contents on the
    // `init` pin (a scalar broadcasts to every entry; a tuple literal packs
    // per entry). nil / 0sb? = uninitialized. When the module also carries a
    // bound reset, the per-entry values become restore write ports (a reset
    // re-loads the init in one cycle — finalize_mems()); with no reset the
    // init stays power-on-only. mut/const arrays get theirs via the
    // whole-array store instead.
    spool_ptr<Dlop>              reg_init;
    std::vector<spool_ptr<Dlop>> init_entries;
    if (!is_array) {
      for (auto c = lnast_->get_sibling_next(mode_nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
        const auto ct = lnast_->get_type(c);
        if (Lnast_ntype::is_stages(ct)) {
          error_here("upass.tolg: memory '{}' cannot carry a stage[] qualifier", name);
          return;
        }
        if (Lnast_ntype::is_const(ct)) {
          auto txt = lnast_->get_name(c);
          if (txt == "nil" || txt == "0sb?") {
            break;
          }
          auto v = Dlop::from_pyrope(txt);
          if (!v || !v->is_just_i64()) {
            error_here("upass.tolg: memory '{}' initializer '{}' is not an integer constant", name, txt);
            return;
          }
          // Scalar broadcast: every entry = value (masked to the element).
          auto mask  = Dlop::get_mask_value(elem_mw);
          auto entry = v->and_op(*mask);
          reg_init   = Dlop::create_integer(0);
          for (int64_t i = 0; i < size; ++i) {
            reg_init = reg_init->or_op(*entry->shl_op(*Dlop::create_integer(i * elem_mw)));
            init_entries.emplace_back(entry);
          }
          break;
        }
        if (Lnast_ntype::is_ref(ct)) {
          auto tit = tuple_recs_.find(std::string(lnast_->get_name(c)));
          if (tit == tuple_recs_.end() || !tit->second.named.empty()) {
            error_here("upass.tolg: memory '{}' initializer must be a comptime constant or tuple literal", name);
            return;
          }
          // Row-major flatten; nested literals must match the dims chain.
          if (!flatten_init_values(tit->second, dims, 0, name, elem_mw, init_entries)) {
            return;  // flatten_init_values reported
          }
          reg_init = pack_entries(init_entries, elem_mw);
          break;
        }
        error_here("upass.tolg: memory '{}' initializer must be a comptime constant or tuple literal", name);
        return;
      }
    }

    const int     user_sites    = count_mem_write_sites(name);
    const bool    wants_restore = !is_array && reg_init && !reset_name_.empty();
    // The fwd mask covers exactly the PROGRAM write ports (per-write-port
    // forwarding in the cgen wrappers); restore ports stay un-forwarded so a
    // read during reset returns the committed contents. A pre-declared
    // `fwd=0` attr (verilog nonblocking memory semantics: reads see the old
    // contents) clears the program-port forwarding.
    int64_t fwd_mask = is_array ? 1 : (int64_t{1} << user_sites) - 1;
    if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
      if (auto fit = pit->second.find("fwd"); fit != pit->second.end()) {
        if (auto fv = Dlop::from_pyrope(fit->second); fv && fv->is_just_i64()) {
          fwd_mask = fv->to_just_i64();
        }
      }
    }

    auto mem = make_node(Ntype_op::Memory);
    setup_sink_by_name(mem, "bits").connect_driver(create_const(*g_, *Dlop::create_integer(elem_mw)));
    setup_sink_by_name(mem, "size").connect_driver(create_const(*g_, *Dlop::create_integer(size)));
    setup_sink_by_name(mem, "type").connect_driver(create_const(*g_, *Dlop::create_integer(is_array ? 2 : 0)));
    setup_sink_by_name(mem, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(fwd_mask)));
    setup_sink_by_name(mem, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    if (reg_init) {
      setup_sink_by_name(mem, "init").connect_driver(create_const(*g_, *reg_init));
    }
    // Clock wiring (posclk + clock_pin) for a clocked (non-array) memory is
    // deferred to finalize_mems: the slang reader emits the clock_pin/posclk
    // attr_set AFTER this declare in lnast order, so pending_attrs_ is not yet
    // populated here (unlike fwd, which the reader emits before the declare).

    Mem_info info;
    info.node        = mem;
    info.size        = size;
    info.dims        = std::move(dims);
    info.elem_mw     = elem_mw;
    info.elem_signed = elem_signed;
    info.is_array    = is_array;
    // `pub` on a reg means a remote regref may attach reads/writes later —
    // suppress access diagnostics. Dormant today (prp2lnast restricts `pub`
    // to file scope), but the gate is mode-keyed so it activates with regref.
    info.is_pub      = std::string_view(lnast_->get_name(mode_nid)).find("pub") != std::string_view::npos;
    info.n_user_wr   = user_sites;
    info.n_wr_total  = user_sites + (wants_restore ? static_cast<int>(size) : 0);
    if (wants_restore) {
      info.restore_vals = std::move(init_entries);
    }
    mem_map_.emplace(std::string(name), info);
    mem_order_.emplace_back(name);
  }

  // A surviving tuple literal, recorded by node id so a memory consumer can
  // resolve its elements later: positional const/ref children land in
  // `elems`, named fields (store children) in `named`. Consumers: the
  // mut/const array initializer (all-const elems → `init` packing) and the
  // __memory(cfg) builtin (named fields + per-port positional lists).
  struct Tuple_rec {
    std::vector<Lnast_nid>                      elems;
    absl::flat_hash_map<std::string, Lnast_nid> named;
  };

  // tuple_add(ref dst, e0 | store(name, v), …) — record the literal. Any
  // other child shape keeps the unhandled warn (nothing can consume it).
  void lower_tuple_add(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    Tuple_rec rec;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      const auto ct = lnast_->get_type(c);
      if (Lnast_ntype::is_const(ct) || Lnast_ntype::is_ref(ct)) {
        rec.elems.emplace_back(c);
        continue;
      }
      if (Lnast_ntype::is_store(ct)) {
        auto k = lnast_->get_first_child(c);
        auto v = k.is_invalid() ? k : lnast_->get_sibling_next(k);
        if (!v.is_invalid() && lnast_->get_sibling_next(v).is_invalid()) {
          rec.named[std::string(lnast_->get_name(k))] = v;
          continue;
        }
      }
      warn_at(nid, {"unhandled-node", "unsupported"}, "unhandled node type 'tuple_add'");
      return;
    }
    tuple_recs_[std::string(lnast_->get_name(dst))] = std::move(rec);
  }

  // tuple_concat(ref dst, op…) — the `...` splice / `++` result. 2f-splice: a
  // tuple op is fully comptime, so by the time it reaches tolg its real data
  // has already folded into wire refs upstream (the runner propagates each
  // operand's runtime field slot-refs through the concat). The surviving node
  // is pure comptime bookkeeping, so RECORD the merged tuple — exactly like
  // lower_tuple_add — instead of warning + dropping the spliced field wires.
  // No hardware is created here (tuple ops never lower to a cell); a downstream
  // whole-tuple read resolves through the record like any other tuple literal.
  void lower_tuple_concat(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    Tuple_rec rec;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      const auto ct = lnast_->get_type(c);
      if (Lnast_ntype::is_ref(ct)) {
        // Splice an operand tuple: merge its recorded fields in order (positional
        // appended, named keyed). Constprop already reported any field overlap.
        if (auto it = tuple_recs_.find(std::string(lnast_->get_name(c))); it != tuple_recs_.end()) {
          for (auto e : it->second.elems) {
            rec.elems.emplace_back(e);
          }
          for (const auto& [k, v] : it->second.named) {
            rec.named[k] = v;
          }
          continue;
        }
        rec.elems.emplace_back(c);  // a bare ref operand — append as one positional field
        continue;
      }
      if (Lnast_ntype::is_const(ct)) {
        rec.elems.emplace_back(c);
        continue;
      }
      if (Lnast_ntype::is_store(ct)) {
        auto k = lnast_->get_first_child(c);
        auto v = k.is_invalid() ? k : lnast_->get_sibling_next(k);
        if (!v.is_invalid() && lnast_->get_sibling_next(v).is_invalid()) {
          rec.named[std::string(lnast_->get_name(k))] = v;
          continue;
        }
      }
      warn_at(nid, {"unhandled-node", "unsupported"}, "unhandled node type 'tuple_concat'");
      return;
    }
    tuple_recs_[std::string(lnast_->get_name(dst))] = std::move(rec);
  }

  // store(ref mem, rhs) — the whole-array form. For a mut/const array this
  // is its initializer: pack the recorded tuple consts into one wide const
  // (entry 0 in the low `bits`, row-major) on the `init` sink. `= nil` means
  // zero-filled (cgen's default). reg memories reject the shape outright.
  void lower_mem_init_store(const Lnast_nid& rhs, std::string_view name, Mem_info& mi) {
    if (!mi.is_array) {
      error_here("upass.tolg: whole-array assignment to memory '{}' is not supported — write one entry at a time", name);
      return;
    }
    if (mi.init_wired) {
      error_here("upass.tolg: array '{}' is re-initialized — only the declaration initializer is supported", name);
      return;
    }
    const auto rt = lnast_->get_type(rhs);
    if (Lnast_ntype::is_const(rt)) {
      auto txt = lnast_->get_name(rhs);
      if (txt == "nil" || txt == "0sb?") {
        mi.init_wired = true;  // zero-filled default
        return;
      }
      // Scalar broadcast: every entry = value (masked to the element) — the
      // same treatment the reg declare-initializer path applies.
      auto v = Dlop::from_pyrope(txt);
      if (!v || !v->is_just_i64()) {
        error_here("upass.tolg: array '{}' initializer '{}' is not supported — use an integer, a tuple literal or nil",
                   name,
                   txt);
        return;
      }
      auto entry = v->and_op(*Dlop::get_mask_value(mi.elem_mw));
      auto init  = Dlop::create_integer(0);
      for (int64_t i = 0; i < mi.size; ++i) {
        init = init->or_op(*entry->shl_op(*Dlop::create_integer(i * mi.elem_mw)));
      }
      setup_sink_by_name(mi.node, "init").connect_driver(create_const(*g_, *init));
      mi.init_wired = true;
      return;
    }
    auto tit = tuple_recs_.find(std::string(lnast_->get_name(rhs)));
    if (tit == tuple_recs_.end() || !tit->second.named.empty()) {
      error_here("upass.tolg: array '{}' initializer must be a comptime tuple literal", name);
      return;
    }
    std::vector<spool_ptr<Dlop>> entries;
    if (!flatten_init_values(tit->second, mi.dims, 0, name, mi.elem_mw, entries)) {
      return;  // flatten_init_values reported
    }
    setup_sink_by_name(mi.node, "init").connect_driver(create_const(*g_, *pack_entries(entries, mi.elem_mw)));
    mi.init_wired = true;
  }

  // Flatten an init tuple literal to per-entry masked constants, ROW-MAJOR,
  // validating each level's entry count against the dims chain. A nested
  // dimension's literal arrives as a `ref` to its own recorded tuple_add
  // (`((1,2),(3,4))` → outer elems are refs into tuple_recs_). Returns false
  // after reporting.
  [[nodiscard]] bool flatten_init_values(const Tuple_rec& rec, const std::vector<int64_t>& dims, size_t level,
                                         std::string_view name, int32_t bits, std::vector<spool_ptr<Dlop>>& out) {
    if (static_cast<int64_t>(rec.elems.size()) != dims[level]) {
      error_here("upass.tolg: '{}' initializer has {} entries where dimension {} holds {}",
                 name,
                 rec.elems.size(),
                 level,
                 dims[level]);
      return false;
    }
    auto mask = Dlop::get_mask_value(bits);
    for (size_t i = 0; i < rec.elems.size(); ++i) {
      const auto& e = rec.elems[i];
      if (level + 1 < dims.size()) {
        if (!Lnast_ntype::is_ref(lnast_->get_type(e))) {
          error_here("upass.tolg: '{}' initializer entry {} must be a nested tuple literal (the memory has {} dimensions)",
                     name,
                     i,
                     dims.size());
          return false;
        }
        auto tit = tuple_recs_.find(std::string(lnast_->get_name(e)));
        if (tit == tuple_recs_.end() || !tit->second.named.empty()) {
          error_here("upass.tolg: '{}' initializer entry {} must be a comptime tuple literal", name, i);
          return false;
        }
        if (!flatten_init_values(tit->second, dims, level + 1, name, bits, out)) {
          return false;
        }
        continue;
      }
      if (!Lnast_ntype::is_const(lnast_->get_type(e))) {
        error_here("upass.tolg: '{}' initializer entry {} is not a compile-time constant", name, i);
        return false;
      }
      auto v = Dlop::from_pyrope(lnast_->get_name(e));
      if (!v || !v->is_just_i64()) {
        error_here("upass.tolg: '{}' initializer entry {} is not an integer constant", name, i);
        return false;
      }
      out.emplace_back(v->and_op(*mask));
    }
    return true;
  }

  // Pack flat per-entry constants into the wide `init` value: entry 0 in the
  // low `bits`.
  [[nodiscard]] static spool_ptr<Dlop> pack_entries(const std::vector<spool_ptr<Dlop>>& entries, int32_t bits) {
    auto init = Dlop::create_integer(0);
    for (size_t i = 0; i < entries.size(); ++i) {
      init = init->or_op(*entries[i]->shl_op(*Dlop::create_integer(static_cast<int64_t>(i) * bits)));
    }
    return init;
  }

  // 1a-mem — the bound result of a `__memory(cfg)` call: `res[N]` reads the
  // N-th READ port's data (driver pid n_wr + N, port order).
  struct Mem_result {
    hhds::Node_class node;
    int              n_wr = 0;
    int              n_rd = 0;
    int32_t          bits = 0;
  };

  // fcall(ref dst, ref __memory, ref cfg) — direct Memory-cell instantiation
  // (08-memories.md RTL form). The cfg vocabulary is the cell pins VERBATIM
  // (decision 2026-06-09): addr/bits/clock_pin/din/enable/fwd/posclk/type/
  // wensize/size/rdport + init — no `latency`, type picks 0 async / 1 sync /
  // 2 array, rdport entries are strictly 0/1, dout comes back as a tuple
  // indexed by read-port order. Returns false when the call is not __memory.
  bool try_lower_memory_builtin(const Lnast_nid& nid, std::string_view callee_name) {
    if (callee_name != "__memory") {
      return false;
    }
    auto dst      = lnast_->get_first_child(nid);
    auto callee_n = lnast_->get_sibling_next(dst);
    auto arg      = lnast_->get_sibling_next(callee_n);
    if (arg.is_invalid() || !lnast_->get_sibling_next(arg).is_invalid()) {
      error_here("upass.tolg: __memory takes exactly one config tuple in '{}'", lnast_->get_top_module_name());
      return true;
    }
    auto rit = tuple_recs_.find(std::string(lnast_->get_name(arg)));
    if (rit == tuple_recs_.end()) {
      error_here(
          "upass.tolg: __memory config '{}' must be a single tuple literal (build it as `mut cfg = (addr=…, "
          "bits=…, …)`)",
          lnast_->get_name(arg));
      return true;
    }
    const auto& cfg = rit->second;

    // Guardrail: cell pins verbatim — diagnose the old doc vocabulary.
    static constexpr std::string_view known[]
        = {"addr", "bits", "clock_pin", "din", "enable", "fwd", "posclk", "type", "wensize", "size", "rdport", "init"};
    for (const auto& [k, v] : cfg.named) {
      if (std::find(std::begin(known), std::end(known), k) == std::end(known)) {
        error_here(
            "upass.tolg: unknown __memory config field '{}' — the vocabulary is the Memory cell pins verbatim "
            "(addr/bits/clock_pin/din/enable/fwd/posclk/type/wensize/size/rdport/init; no `latency`, no `clock`)",
            k);
        return true;
      }
    }

    auto cfg_const = [&](std::string_view key, int64_t def, bool required, int64_t& out) -> bool {
      auto it = cfg.named.find(std::string(key));
      if (it == cfg.named.end()) {
        if (required) {
          error_here("upass.tolg: __memory config is missing the required '{}' field", key);
          return false;
        }
        out = def;
        return true;
      }
      if (!Lnast_ntype::is_const(lnast_->get_type(it->second))) {
        error_here("upass.tolg: __memory config field '{}' must be a compile-time constant", key);
        return false;
      }
      auto v = Dlop::from_pyrope(lnast_->get_name(it->second));
      if (!v || !v->is_just_i64()) {
        // bool consts ("false"/"true") are integers in from_pyrope; anything
        // else is a config error.
        error_here("upass.tolg: __memory config field '{}' is not an integer constant", key);
        return false;
      }
      out = v->to_just_i64();
      return true;
    };

    int64_t bits = 0, size = 0, type = 0, fwd = 0, wensize = 1, posclk = 1;
    if (!cfg_const("bits", 0, true, bits) || !cfg_const("size", 0, true, size) || !cfg_const("type", 0, false, type)
        || !cfg_const("fwd", 0, false, fwd) || !cfg_const("wensize", 1, false, wensize) || !cfg_const("posclk", 1, false, posclk)) {
      return true;
    }
    if (bits <= 0 || size <= 0) {
      error_here("upass.tolg: __memory needs positive bits/size (got bits={}, size={})", bits, size);
      return true;
    }
    if (type < 0 || type > 2) {
      error_here("upass.tolg: __memory type must be 0 (async), 1 (sync) or 2 (array) — got {}", type);
      return true;
    }

    // Per-port lists: a field is a positional tuple ref or a single scalar.
    auto cfg_list = [&](std::string_view key, std::vector<Lnast_nid>& out) -> bool {
      auto it = cfg.named.find(std::string(key));
      if (it == cfg.named.end()) {
        return true;  // empty
      }
      const auto vt = lnast_->get_type(it->second);
      if (Lnast_ntype::is_ref(vt)) {
        if (auto lit = tuple_recs_.find(std::string(lnast_->get_name(it->second))); lit != tuple_recs_.end()) {
          if (!lit->second.named.empty()) {
            error_here("upass.tolg: __memory config field '{}' must be a positional tuple", key);
            return false;
          }
          out = lit->second.elems;
          return true;
        }
      }
      out = {it->second};  // single scalar = one port
      return true;
    };

    std::vector<Lnast_nid> addrs, dins, ens, rdports;
    if (!cfg_list("addr", addrs) || !cfg_list("din", dins) || !cfg_list("enable", ens) || !cfg_list("rdport", rdports)) {
      return true;
    }
    if (addrs.empty()) {
      error_here("upass.tolg: __memory config needs at least one 'addr' entry");
      return true;
    }
    const int n_ports = static_cast<int>(addrs.size());
    if (static_cast<int>(rdports.size()) != n_ports) {
      error_here("upass.tolg: __memory 'rdport' has {} entries but 'addr' has {}", rdports.size(), n_ports);
      return true;
    }

    int n_wr_cfg = 0;
    for (const auto& rp : rdports) {
      if (!Lnast_ntype::is_const(lnast_->get_type(rp))) {
        continue;  // diagnosed in the port loop below
      }
      auto v = Dlop::from_pyrope(lnast_->get_name(rp));
      if (!v || v->is_known_false()) {
        ++n_wr_cfg;
      }
    }
    // `fwd=true` means every write port forwards (the cgen wrappers take a
    // per-write-port mask); a value > 1 passes through as an explicit mask.
    const int64_t fwd_mask = fwd == 0 ? 0 : (fwd == 1 ? (int64_t{1} << n_wr_cfg) - 1 : fwd);

    auto mem = make_node(Ntype_op::Memory);
    setup_sink_by_name(mem, "bits").connect_driver(create_const(*g_, *Dlop::create_integer(bits)));
    setup_sink_by_name(mem, "size").connect_driver(create_const(*g_, *Dlop::create_integer(size)));
    setup_sink_by_name(mem, "type").connect_driver(create_const(*g_, *Dlop::create_integer(type)));
    setup_sink_by_name(mem, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(fwd_mask)));
    setup_sink_by_name(mem, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(wensize)));
    if (type != 2) {
      setup_sink_by_name(mem, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(posclk)));
      if (auto it = cfg.named.find("clock_pin"); it != cfg.named.end()) {
        setup_sink_by_name(mem, "clock_pin").connect_driver(leaf(it->second).pin);
      } else if (!clock_name_.empty()) {
        setup_sink_by_name(mem, "clock_pin").connect_driver(clock_pin());
      } else {
        warn_at(Lnast_nid{}, {"no-clock", "time"}, "__memory has no clock to bind in '{}'", lnast_->get_top_module_name());
      }
    }
    if (auto it = cfg.named.find("init"); it != cfg.named.end()) {
      spool_ptr<Dlop> init;
      if (Lnast_ntype::is_const(lnast_->get_type(it->second))) {
        init = Dlop::from_pyrope(lnast_->get_name(it->second));
      } else if (auto lit = tuple_recs_.find(std::string(lnast_->get_name(it->second))); lit != tuple_recs_.end()) {
        const std::vector<int64_t>   flat_dims{size};  // __memory is always flat
        std::vector<spool_ptr<Dlop>> entries;
        if (flatten_init_values(lit->second, flat_dims, 0, "__memory init", static_cast<int32_t>(bits), entries)) {
          init = pack_entries(entries, static_cast<int32_t>(bits));
        }
      }
      if (!init) {
        error_here("upass.tolg: __memory 'init' must be a comptime constant or tuple literal");
        return true;
      }
      setup_sink_by_name(mem, "init").connect_driver(create_const(*g_, *init));
    }

    int n_wr = 0;
    for (int i = 0; i < n_ports; ++i) {
      if (!Lnast_ntype::is_const(lnast_->get_type(rdports[i]))) {
        error_here("upass.tolg: __memory 'rdport' entry {} must be a comptime 0/1 constant", i);
        return true;
      }
      auto       v     = Dlop::from_pyrope(lnast_->get_name(rdports[i]));
      const bool is_rd = v && !v->is_known_false();
      if (!is_rd) {
        ++n_wr;
      }
    }

    for (int i = 0; i < n_ports; ++i) {
      const auto base  = i * kMemPortStride;
      auto       rdv   = Dlop::from_pyrope(lnast_->get_name(rdports[i]));
      const bool is_rd = rdv && !rdv->is_known_false();
      mem.create_sink_pin(static_cast<hhds::Port_id>(base + 0)).connect_driver(leaf(addrs[i]).pin);
      mem.create_sink_pin(static_cast<hhds::Port_id>(base + 10))
          .connect_driver(create_const(*g_, *Dlop::create_integer(is_rd ? 1 : 0)));
      Pin en = i < static_cast<int>(ens.size()) ? leaf(ens[i]).pin : en_const(true);
      mem.create_sink_pin(static_cast<hhds::Port_id>(base + 4)).connect_driver(en);
      if (!is_rd) {
        if (i >= static_cast<int>(dins.size())) {
          error_here("upass.tolg: __memory write port {} has no 'din' entry", i);
          return true;
        }
        mem.create_sink_pin(static_cast<hhds::Port_id>(base + 3)).connect_driver(leaf(dins[i]).pin);
      }
    }

    mem_results_[std::string(lnast_->get_name(dst))] = Mem_result{mem, n_wr, n_ports - n_wr, static_cast<int32_t>(bits)};
    return true;
  }

  // store(ref mem, idx, val) — one write port per site. The enable is the
  // site's full branch-path condition (true when unconditional); same-cycle
  // conflicts between ports are defined by the memory config (fwd), not here.
  void lower_mem_store(const Lnast_nid& lhs, std::string_view lhs_name, Mem_info& mi) {
    // Gather the index chain; the LAST sibling is the stored value
    // (store(mem, i, j, …, val) is FLAT — one node, N index operands).
    std::vector<Lnast_nid> idxs;
    for (auto c = lnast_->get_sibling_next(lhs); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      idxs.emplace_back(c);
    }
    // Chunked masked write: store(mem, <idx…>, din, chunk_k) has dims+2
    // children — the trailing const is the per-chunk write-enable index, so the
    // enable becomes `path_cond << k` (the wensize byte/chunk-enable model; the
    // memory's wensize is set from the reader's pending attr in finalize_mems).
    int chunk = -1;
    if (idxs.size() == mi.dims.size() + 2) {
      if (auto cv = Dlop::from_pyrope(lnast_->get_name(idxs.back())); cv && cv->is_just_i64()) {
        chunk = static_cast<int>(cv->to_just_i64());
        idxs.pop_back();
      }
    }
    if (idxs.size() < 2) {
      error_here("upass.tolg: whole-array assignment to memory '{}' is not supported — write one entry at a time", lhs_name);
      return;
    }
    auto val = idxs.back();
    idxs.pop_back();
    auto addr = flatten_mem_addr(mi, idxs, lhs_name);
    if (addr.is_invalid()) {
      return;  // flatten_mem_addr reported
    }
    if (mi.wr_next >= mi.n_user_wr) {
      error_here("upass.tolg: internal — memory '{}' write-site pre-scan undercounted", lhs_name);
      return;
    }
    if (mi.is_array && mi.rd_next > 0) {
      // A type=2 array is lowered writes-before-reads (forwarding), so a
      // source-order read placed BEFORE this write would wrongly see it.
      // reg memories are exempt: fwd semantics are order-free by contract.
      error_here(
          "upass.tolg: array '{}' is written after being read — same-cycle order is not preserved for "
          "mut/const arrays; reorder the accesses or use a `reg` memory",
          lhs_name);
      return;
    }
    const auto base = mi.wr_next * kMemPortStride;
    ++mi.wr_next;
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 0)).connect_driver(addr);           // addr
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 3)).connect_driver(leaf(val).pin);  // din
    auto en = current_path_cond();
    if (en.is_invalid()) {
      en = en_const(true);
    }
    if (chunk >= 0) {
      en = shl1_by(en, chunk);  // per-chunk write enable: bit `chunk` = path_cond
    }
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 4)).connect_driver(en);  // enable
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 10))
        .connect_driver(create_const(*g_, *Dlop::create_integer(0)));  // rdport = 0 (write)
  }

  // tuple_get(ref dst, ref mem, idx) — one read port per site, always
  // enabled; dst binds to the port's dout driver (pid n_wr_total + r).
  void lower_tuple_get(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    auto src = dst.is_invalid() ? dst : lnast_->get_sibling_next(dst);
    auto idx = src.is_invalid() ? src : lnast_->get_sibling_next(src);
    if (idx.is_invalid()) {
      warn_at(nid, {"unhandled-node", "unsupported"}, "unhandled node type 'tuple_get'");
      return;
    }
    auto it = mem_map_.find(std::string(lnast_->get_name(src)));
    if (it == mem_map_.end()) {
      // Multi-output Sub result: tuple_get(dst, result, 'port') binds that
      // output port's driver pin with the io-entry width/sign contract.
      if (auto srt = sub_results_.find(std::string(lnast_->get_name(src))); srt != sub_results_.end()) {
        if (!Lnast_ntype::is_const(lnast_->get_type(idx)) || !lnast_->get_sibling_next(idx).is_invalid()) {
          error_here("upass.tolg: a multi-output instance result is read by a single output-port name");
          return;
        }
        const auto&           pname = lnast_->get_name(idx);
        const Lnast_io_entry* oe    = nullptr;
        for (const auto& e : srt->second.outputs) {
          if (e.name == pname) {
            oe = &e;
            break;
          }
        }
        if (oe == nullptr) {
          error_here("upass.tolg: instance result has no output named '{}'", pname);
          return;
        }
        auto    out_dpin = srt->second.sub.create_driver_pin(oe->name);
        int32_t mw       = io_mw(*oe);
        if (oe->kind == Io_kind::boolean || mw <= 1) {
          set_bits(out_dpin, 1);
          if (oe->is_signed) {
            set_sign(out_dpin);
          } else {
            set_unsign(out_dpin);
          }
          record(lnast_->get_name(dst), out_dpin, 1);
        } else if (oe->is_signed) {
          set_bits(out_dpin, mw);
          set_sign(out_dpin);
          record(lnast_->get_name(dst), out_dpin, mw);
        } else {
          set_bits(out_dpin, mw);
          set_sign(out_dpin);
          record(lnast_->get_name(dst), to_positive(out_dpin, mw), mw);
        }
        return;
      }
      // 1a-mem — res[N] on a __memory result: bind the N-th read port's dout.
      if (auto mrt = mem_results_.find(std::string(lnast_->get_name(src))); mrt != mem_results_.end()) {
        const auto& mr = mrt->second;
        if (!Lnast_ntype::is_const(lnast_->get_type(idx)) || !lnast_->get_sibling_next(idx).is_invalid()) {
          error_here("upass.tolg: a __memory result is indexed by a single comptime read-port number");
          return;
        }
        auto          v = Dlop::from_pyrope(lnast_->get_name(idx));
        const int64_t k = (v && v->is_just_i64()) ? v->to_just_i64() : -1;
        if (k < 0 || k >= mr.n_rd) {
          error_here("upass.tolg: __memory result index {} out of range — the config has {} read port(s)",
                     lnast_->get_name(idx),
                     mr.n_rd);
          return;
        }
        auto dout = mr.node.create_driver_pin(static_cast<hhds::Port_id>(mr.n_wr + k));
        set_bits(dout, mr.bits);
        set_unsign(dout);  // __memory data is raw bits — unsigned
        record(lnast_->get_name(dst), to_positive(dout, mr.bits), mr.bits);
        return;
      }
      // 2f-defer — a single-field read (`src.field`) of a name that is not yet
      // a known memory / Sub result. It may be a forward reference to a call
      // result lowered later in the body (the defer feedback `tmp.add.[defer]`
      // reads tmp.add before `tmp = add_sub(…)` runs). Defer the bind to
      // end-of-pass; re-resolved with tget_final_, a still-unresolved one warns.
      if (!tget_final_ && Lnast_ntype::is_const(lnast_->get_type(idx)) && lnast_->get_sibling_next(idx).is_invalid()) {
        pending_tgets_.emplace_back(nid);
        return;
      }
      warn_at(nid, {"unhandled-node", "unsupported"}, "unhandled node type 'tuple_get'");
      return;
    }
    auto& mi = it->second;
    // Gather the full index chain (tuple_get(dst, mem, i, j, …) is FLAT).
    std::vector<Lnast_nid> idxs;
    for (auto c = idx; !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      idxs.emplace_back(c);
    }
    auto addr = flatten_mem_addr(mi, idxs, lnast_->get_name(src));
    if (addr.is_invalid()) {
      return;  // flatten_mem_addr reported
    }
    const int  slot = mi.n_wr_total + mi.rd_next;
    const auto base = slot * kMemPortStride;
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 0)).connect_driver(addr);  // addr
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 4)).connect_driver(en_const(true));
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 10))
        .connect_driver(create_const(*g_, *Dlop::create_integer(1)));  // rdport = 1 (read)
    auto dout = mi.node.create_driver_pin(static_cast<hhds::Port_id>(mi.n_wr_total + mi.rd_next));
    ++mi.rd_next;
    auto dst_name = lnast_->get_name(dst);
    if (mi.elem_signed) {
      set_bits(dout, mi.elem_mw);
      set_sign(dout);
      record(dst_name, dout, mi.elem_mw);
    } else {
      set_bits(dout, mi.elem_mw);
      set_unsign(dout);
      record(dst_name, to_positive(dout, mi.elem_mw), mi.elem_mw);  // unsigned -> positive
    }
  }

  void finalize_mems() {
    for (const auto& name : mem_order_) {
      auto it = mem_map_.find(name);
      if (it == mem_map_.end()) {
        continue;
      }
      const auto& mi = it->second;
      if (mi.wr_next != mi.n_user_wr) {
        error_here("upass.tolg: internal — memory '{}' lowered {} write sites but the pre-scan counted {}",
                   name,
                   mi.wr_next,
                   mi.n_user_wr);
      }
      // 1a-mem reset-restore — a concrete-init reg array with a bound reset
      // restores its init on reset: one write port per entry (addr=k,
      // din=init[k], enable=reset) on the slots after the user sites, and
      // every USER port's enable gated with !reset (during reset the program
      // writes are suppressed, exactly like a scalar reg's din). The restore
      // ports are excluded from the fwd mask, so a same-cycle read during
      // reset still returns the committed (old) contents.
      if (!mi.restore_vals.empty()) {
        Pin rst = reset_pin();
        if (reset_neg_) {
          rst = not1(rst);
        }
        const auto en_pid_off = 4;
        Pin        not_rst    = not1(rst);
        for (int u = 0; u < mi.n_user_wr; ++u) {
          const auto pid = static_cast<uint64_t>(u * kMemPortStride + en_pid_off);
          for (const auto& e : mi.node.inp_edges()) {
            if (!e.sink.is_invalid() && static_cast<uint64_t>(e.sink.get_port_id()) == pid) {
              auto old_en = e.driver;
              e.del_edge();
              mi.node.create_sink_pin(static_cast<hhds::Port_id>(pid)).connect_driver(and2(old_en, not_rst));
              break;
            }
          }
        }
        for (int64_t k = 0; k < static_cast<int64_t>(mi.restore_vals.size()); ++k) {
          const auto base = (mi.n_user_wr + k) * kMemPortStride;
          mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 0)).connect_driver(create_const(*g_, *Dlop::create_integer(k)));
          mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 3))
              .connect_driver(create_const(*g_, *mi.restore_vals[static_cast<size_t>(k)]));
          mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 4)).connect_driver(rst);
          mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 10))
              .connect_driver(create_const(*g_, *Dlop::create_integer(0)));  // rdport = 0 (write)
        }
      }
      // Chunked masked writes (mem[addr][chunk]<=data) set a wensize > 1 via a
      // pending attr from the reader; the declare provisionally drove wensize=1,
      // so re-drive it here (after every write port is in place). wensize is the
      // single config pin at port_id 8 (see graph/cell.cpp Memory pin names).
      if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
        if (auto wit = pit->second.find("wensize"); wit != pit->second.end()) {
          if (auto wv = Dlop::from_pyrope(wit->second); wv && wv->is_just_i64() && wv->to_just_i64() > 1) {
            for (const auto& e : mi.node.inp_edges()) {
              if (!e.sink.is_invalid() && static_cast<int>(e.sink.get_port_id()) == 8) {
                e.del_edge();
                break;
              }
            }
            setup_sink_by_name(mi.node, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(wv->to_just_i64())));
          }
        }
      }

      // Clocked (non-array) memory clock wiring, deferred from lower_mem_declare
      // (the clock_pin/posclk attr_set arrives after the declare). Mirrors the
      // per-reg wiring in finalize_regs: an explicit clock_pin=<input> (the
      // slang reader emits it for a non-`clk`/`clock` write clock) beats the
      // implicit shared clock; posclk=false marks a negedge write clock.
      if (!mi.is_array) {
        bool        posclk_val = true;
        std::string clock_pin_name;
        if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
          if (auto cit = pit->second.find("clock_pin"); cit != pit->second.end()) {
            clock_pin_name = cit->second;
          }
          if (auto pcit = pit->second.find("posclk"); pcit != pit->second.end()) {
            posclk_val = pcit->second != "false" && pcit->second != "0";
          }
        }
        setup_sink_by_name(mi.node, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(posclk_val ? 1 : 0)));
        if (!clock_pin_name.empty()) {
          auto cpin = g_->get_input_pin(clock_pin_name);
          if (cpin.is_invalid()) {
            error_here("upass.tolg: memory '{}' names clock_pin '{}' but '{}' has no such input",
                       name,
                       clock_pin_name,
                       lnast_->get_top_module_name());
          } else {
            setup_sink_by_name(mi.node, "clock_pin").connect_driver(cpin);
          }
        } else if (!clock_name_.empty()) {
          setup_sink_by_name(mi.node, "clock_pin").connect_driver(clock_pin());
        } else {
          warn_at(Lnast_nid{}, {"no-clock", "time"}, "memory '{}' has no clock input to bind", name);
        }
      }

      // A read-less (or access-less) memory is a WARNING at most — its state
      // can be observed by a scan chain, and a future remote regref may
      // attach reads/writes. `pub` (regref potential) silences it entirely.
      if (!mi.is_pub && mi.rd_next == 0) {
        warn_at(Lnast_nid{},
                {"memory-never-read", "type"},
                "memory '{}' is never read — contents are only observable via scan/regref",
                name);
      }
    }
  }

  // Declared (mw, is_signed) from a declare's type child. prim_type_int(max,
  // min): unsigned iff min ≥ 0, mw mirrors the ssa io harvest (get_bits()-1
  // drops the sign bit when unsigned). prim_type_bool → 1. Unknown → (0,_).
  [[nodiscard]] std::pair<int32_t, bool> declared_width(const Lnast_nid& type_nid) {
    using N      = Lnast_ntype;
    const auto t = lnast_->get_type(type_nid);
    if (N::is_prim_type_bool(t)) {
      return {1, false};
    }
    if (!N::is_prim_type_int(t)) {
      return {0, false};
    }
    auto mx = lnast_->get_first_child(type_nid);
    if (mx.is_invalid()) {
      return {0, false};
    }
    auto mn    = lnast_->get_sibling_next(mx);
    auto max_v = Dlop::from_pyrope(lnast_->get_name(mx));
    if (!max_v || !max_v->is_integer()) {
      return {0, false};
    }
    bool    min_known = false;
    bool    min_neg   = false;
    int32_t min_bits  = 0;
    if (!mn.is_invalid()) {
      if (auto mn_v = Dlop::from_pyrope(lnast_->get_name(mn)); mn_v && mn_v->is_integer()) {
        min_known = true;
        min_neg   = mn_v->is_negative();
        min_bits  = static_cast<int32_t>(mn_v->get_bits());
      }
    }
    const bool is_signed = !(min_known && !min_neg);
    if (!is_signed) {
      auto bits = max_v->is_known_zero() ? int32_t{1} : static_cast<int32_t>(max_v->get_bits() - 1);
      return {bits, false};
    }
    // Signed: the WIDER of the two bounds' signed widths (mirrors the ssa io
    // harvest + io_mw — a min like -100 needs more bits than a max of 3).
    auto bits = static_cast<int32_t>(max_v->get_bits());
    if (min_known) {
      bits = std::max(bits, min_bits);
    }
    return {bits, true};
  }

  // Materialize a deferred stage reg at its din store. Effective
  // depth: plain RHS keeps the declared (min,max); a Sub call result narrows
  // to the DEFICIT against the callee (Phase-1 realization = callee at its
  // declared min, so deficit = stage_N − callee_min; for a mod callee the
  // output cycle is fixed and stage_N must match it exactly → deficit 0).
  // Depth (0,0) is a plain wire — no Flop is created at all.
  void create_stage_flop(std::string_view name, const Pending_stage& p, const Lnast_nid& rhs) {
    // Runs from finalize (no statement walk active): anchor the flop at the
    // stage declaration.
    cur_srcid_ = hhds::SourceId_invalid;
    if (const auto id = lnast_->get_srcid(p.decl_nid); id != hhds::SourceId_invalid) {
      cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
    }
    const bool min_nil = p.min_txt == "nil";
    const bool max_nil = p.max_txt == "nil";
    int64_t    smin    = 0;
    int64_t    smax    = 0;
    if (!min_nil) {
      auto c = Dlop::from_pyrope(p.min_txt);
      smin   = (c && c->is_just_i64()) ? c->to_just_i64() : 0;
    }
    if (!max_nil) {
      auto c = Dlop::from_pyrope(p.max_txt);
      smax   = (c && c->is_just_i64()) ? c->to_just_i64() : 0;
    }

    int64_t emin = smin;
    int64_t emax = smax;

    std::string rhs_name;
    if (Lnast_ntype::is_ref(lnast_->get_type(rhs))) {
      rhs_name = std::string(lnast_->get_name(rhs));
    }
    if (auto sit = sub_out_stages_.find(rhs_name); sit != sub_out_stages_.end()) {
      const auto& so = sit->second;
      if (min_nil || max_nil || smin != smax) {
        error_here(
            "upass.tolg: `stage[]` / ranged stage counts on a pipe/mod call are not supported yet — "
            "write a fixed `stage[N]` for '{}'",
            name);
        return;
      }
      const int64_t n = smin;
      if (so.is_pipe) {
        // pipe convention: cmax < cmin (e.g. bare pipe (1,0)) = no upper bound.
        if (n < so.cmin || (so.cmax >= so.cmin && n > so.cmax)) {
          if (so.cmax >= so.cmin) {
            error_here("upass.tolg: stage[{}] on '{}' is outside the callee's declared latency range [{}, {}]",
                       n,
                       name,
                       so.cmin,
                       so.cmax);
          } else {
            error_here("upass.tolg: stage[{}] on '{}' is below the callee's declared minimum latency {}", n, name, so.cmin);
          }
          return;
        }
        emin = emax = n - so.cmin;  // callee realized at its declared min
      } else {
        // mod callee: the output's landing cycle is fixed by its interface.
        if (so.cmin != so.cmax || n != so.cmin) {
          error_here(
              "upass.tolg: mod call result '{}' lands at its declared cycle {} — `stage[{}]` must match it "
              "(add a separate `stage[N] x = value` for extra delay)",
              name,
              so.cmin,
              n);
          return;
        }
        emin = emax = 0;
      }
      // The stage pick pins the REALIZED split: the callee
      // instance contributes its declared min (Phase-1 realization), the
      // caller-side deficit flop the remaining n − cmin. Stamping the full
      // pick on the instance would double-count the deficit.
      {
        const int64_t realized = so.is_pipe ? so.cmin : n;
        so.node.attr(livehd::attrs::time_range).set({realized, realized});
        sub_time_[so.node.get_debug_nid()] = {realized, realized};
      }
    } else if (min_nil || max_nil) {
      error_here(
          "upass.tolg: `stage[]` on '{}' has no chosen count at realization — write `stage[N]` (the toolchain-picked "
          "default lands in a later phase)",
          name);
      return;
    }

    auto v = leaf(rhs);
    if (emin == 0 && emax == 0) {
      record(name, v.pin, v.mw);  // zero-depth stage = wire
      return;
    }

    auto flop                         = make_node(Ntype_op::Flop);
    flop_depth_[flop.get_debug_nid()] = {emin, emax};
    // The LN-inserted pipe output flop (vs a user `stage[N]` reg) is
    // the narrowing target: LG pass1 rewrites its depth to (min−σ, max−σ).
    if (name.starts_with("___pipe_")) {
      inserted_flops_.insert(flop.get_debug_nid());
    }
    setup_sink_by_name(flop, "pipe_min").connect_driver(create_const(*g_, *Dlop::create_integer(emin)));
    setup_sink_by_name(flop, "pipe_max").connect_driver(create_const(*g_, *Dlop::create_integer(emax)));
    if (!clock_name_.empty()) {
      setup_sink_by_name(flop, "clock_pin").connect_driver(clock_pin());
    } else {
      warn_at(Lnast_nid{}, {"no-clock", "time"}, "reg '{}' has no clock input to bind", name);
    }
    setup_sink_by_name(flop, "din").connect_driver(v.pin);
    auto q = flop.create_driver_pin(0);
    set_bits(q, v.mw + 1);
    set_unsign(q);
    reg_map_.emplace(std::string(name), flop);
    record(name, q, v.mw);
  }

  // func_call(dst_tmp, callee_name, args...) → an Ntype_op::Sub
  // instance of the callee's graph. Args are positional refs/consts (mapped
  // to the callee's io_meta input order) or named store(argname, value)
  // children. The single output binds the dst name with the same
  // bits/sign/to-positive treatment a graph INPUT gets (the value enters
  // this graph from outside). The callee output's declared stages interval
  // is recorded for the following stage-reg store (deficit narrowing).
  void lower_func_call(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto callee_n = lnast_->get_sibling_next(dst);
    if (callee_n.is_invalid()) {
      return;
    }
    std::string callee_name(lnast_->get_name(callee_n));

    // 1a-mem — direct Memory-cell instantiation builtin.
    if (try_lower_memory_builtin(nid, callee_name)) {
      return;
    }

    // A resolved `import` call is comptime scaffolding: constprop
    // bound its namespace bundle / lambda ref and every consumer folded (an
    // UNRESOLVED live import never reaches tolg — pass.upass errors or the
    // kernel defers). Nothing lowers to hardware here.
    if (Lnast_ntype::is_const(lnast_->get_type(callee_n)) && callee_name == "import") {
      return;
    }

    std::string                    callee_full;
    std::shared_ptr<hhds::GraphIO> gio;
    const Lnast_tree_io*           cio_ptr = nullptr;
    Lnast_tree_io                  cio_lg;  // synthesized for an lg: black box
    std::string_view               kind;    // callee lambda kind ("" for an lg: black box)
    std::shared_ptr<Lnast>         callee;  // kept alive: cio_ptr may point into its io_meta()

    // An `import("lg:foo")` binding folds the callee to the string
    // 'lg:foo'. Instantiate the foreign graph as a BLACK BOX: its GraphIO (the
    // kernel load_merge'd the lg: inputs into lib_) supplies the IO to wire by
    // name; cgen emits the instance by name and the body rides along in the
    // assembled library. There is no ln: lambda — synthesize the io_meta the
    // shared wiring below expects from the GraphIO's declared pins.
    std::string lg_name;
    {
      std::string s = callee_name;
      if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        s = s.substr(1, s.size() - 2);
      }
      if (s.rfind("lg:", 0) == 0) {
        lg_name = s.substr(3);
      }
    }
    if (!lg_name.empty()) {
      gio = lib_ != nullptr ? lib_->find_io(lg_name) : nullptr;
      if (!gio) {
        error_here(
            "upass.tolg: imported lg: graph '{}' not found in any input library — pass it as an `lg:` "
            "input (or it failed to load)",
            lg_name);
        return;
      }
      auto kind_of_bits = [](uint32_t b) { return b == 1 ? Io_kind::boolean : Io_kind::integer; };
      for (const auto& d : gio->get_input_pin_decls()) {
        if (d.name == "clock" || d.name == "reset") {
          continue;  // implicit; wired from the parent below, not an argument
        }
        cio_lg.inputs.push_back(Lnast_io_entry{.name      = d.name,
                                               .bits      = static_cast<int32_t>(d.bits),
                                               .is_signed = !d.unsign,
                                               .kind      = kind_of_bits(d.bits)});
      }
      for (const auto& d : gio->get_output_pin_decls()) {
        cio_lg.outputs.push_back(Lnast_io_entry{.name      = d.name,
                                                .bits      = static_cast<int32_t>(d.bits),
                                                .is_signed = !d.unsign,
                                                .kind      = kind_of_bits(d.bits)});
      }
      cio_ptr     = &cio_lg;
      callee_full = lg_name;
      callee_name = lg_name;  // Sub instance name + diagnostics
    } else {
      if (registry_ != nullptr) {
        callee = resolve_callee_lnast(callee_name, *registry_);
      }
      kind = callee ? callee->get_lambda_kind() : std::string_view{};
      if (!callee || (kind != "pipe" && kind != "mod")) {
        // Unresolvable (runtime wrap/sat builtin, recursion-kept comb, …):
        // HARD error — the pre-1r behavior silently wired the call's outputs
        // to nil and emitted a broken netlist.
        error_here(
            "upass.tolg: call to '{}' has no hardware lowering yet — only pipe/mod calls become instances "
            "(note `comb` may not call a `pipe`/`mod`), and runtime `wrap`/`sat` lowering is pending",
            callee_name);
        return;
      }

      // Only a `mod` may instantiate pipe/mod callees (06-functions.md: `comb`
      // may not call a `pipe`/`mod`; pipe bodies use stage inference, not
      // instantiation). Without this gate a comb would silently grow a
      // latency-carrying instance.
      if (lnast_->get_lambda_kind() != "mod") {
        error_here("upass.tolg: '{}' (a {}) calls the {} '{}' — only `mod` bodies may instantiate pipe/mod",
                   lnast_->get_top_module_name(),
                   lnast_->get_lambda_kind().empty() ? std::string_view{"comb"} : lnast_->get_lambda_kind(),
                   kind,
                   callee_name);
        return;
      }

      // 2f-lg: the callee's GraphIO is keyed by its effective graph name (lg
      // override or mangled name) — the same key register_io/setup_io_impl
      // used. resolve_callee_lnast above still matched by top_module_name (the
      // import/call identity), so the rename never affects call resolution.
      callee_full = std::string(callee->get_graph_name());
      gio         = lib_ != nullptr ? lib_->find_io(callee_full) : nullptr;
      if (!gio) {
        error_here("upass.tolg: callee '{}' has no registered GraphIO — register_io() phase missing", callee_full);
        return;
      }
      cio_ptr = &callee->io_meta();
    }
    const auto& cio = *cio_ptr;
    if (cio.outputs.empty()) {
      error_here("upass.tolg: call to '{}' has no outputs — nothing to bind", callee_full);
      return;
    }

    // NOTE: set_subnode RE-STAMPS the raw hhds type to its own 2/3 loop-hint
    // encoding — type_op_of() recognizes Subs by the subnode LINK, never by
    // the stored type (see node_util.hpp).
    auto sub = make_node(Ntype_op::Sub);
    sub.set_subnode(gio);
    {
      std::string dst_txt(lnast_->get_name(dst));
      std::string suffix = dst_txt.size() > 3 && dst_txt.substr(0, 3) == "___" ? dst_txt.substr(3) : dst_txt;
      sub.attr(hhds::attrs::name).set("u_" + callee_name + "_" + suffix);
    }

    // Actuals → callee input sink pins.
    std::size_t pos      = 0;
    std::size_t provided = 0;
    for (auto a = lnast_->get_sibling_next(callee_n); !a.is_invalid(); a = lnast_->get_sibling_next(a)) {
      std::string pname;
      Lnast_nid   val;
      if (Lnast_ntype::is_store(lnast_->get_type(a))) {
        auto an = lnast_->get_first_child(a);
        if (an.is_invalid()) {
          continue;
        }
        pname = std::string(lnast_->get_name(an));
        val   = lnast_->get_sibling_next(an);
        if (val.is_invalid()) {
          continue;
        }
        // Namespace receiver marker: `lib.scale(args)` through an
        // import tuple carries the receiver in a `__ufcs_arg` store, but the
        // receiver names the NAMESPACE — it is not an argument of a no-self
        // callee. (A true `ref self` mod method splices in the runner and
        // never reaches the Sub path.)
        if (pname == "__ufcs_arg" && (cio.inputs.empty() || cio.inputs[0].name != "self")) {
          continue;
        }
      } else {
        if (pos >= cio.inputs.size()) {
          error_here("upass.tolg: call to '{}' passes more arguments than its {} declared inputs", callee_full, cio.inputs.size());
          return;
        }
        pname = cio.inputs[pos].name;
        ++pos;
        val = a;
      }
      auto v = leaf(val);
      // 2f-lgimport — validate the port name BEFORE create_sink_pin: an unknown
      // port (e.g. a typo, or a call shaped for a different module) otherwise
      // asserts inside resolve_sink_port (graph.cpp). The compiler must never
      // abort on user input — emit a clean port-mismatch diagnostic instead.
      if (!gio->has_input(pname)) {
        error_here("upass.tolg: call to '{}' names input '{}' which the imported module does not have",
                   callee_full,
                   pname);
        return;
      }
      auto spin = sub.create_sink_pin(pname);
      if (spin.is_invalid()) {
        error_here("upass.tolg: callee '{}' has no input named '{}'", callee_full, pname);
        return;
      }
      spin.connect_driver(v.pin);
      ++provided;
    }
    if (provided != cio.inputs.size()) {
      error_here("upass.tolg: call to '{}' provides {} of its {} declared inputs", callee_full, provided, cio.inputs.size());
      return;
    }

    // Minted-clock wiring: the callee's implicit "clock" input exists on its
    // GraphIO (register_io pre-declared it) but not in its io_meta — wire it
    // to this graph's clock (needs_clock made sure we have one).
    bool callee_declares_clock = false;
    for (const auto& e : cio.inputs) {
      if (e.name == "clock" || e.name == "clk") {
        callee_declares_clock = true;
        break;
      }
    }
    if (!callee_declares_clock && gio->has_input("clock")) {
      if (clock_name_.empty()) {
        error_here("upass.tolg: instance of clocked '{}' but '{}' has no clock to forward (needs_clock bug)",
                   callee_full,
                   lnast_->get_top_module_name());
        return;
      }
      sub.create_sink_pin("clock").connect_driver(clock_pin());
    }

    // Minted-reset forwarding, same pattern: the callee's implicit
    // "reset" input (active-high by construction) exists on its GraphIO but
    // not in its io_meta. An active-low caller reset is inverted on the way
    // in so the callee's polarity contract holds.
    bool callee_declares_reset = false;
    for (const auto& e : cio.inputs) {
      if (e.name == "reset" || e.name == "rst" || e.name == "reset_n" || e.name == "rst_n") {
        callee_declares_reset = true;
        break;
      }
    }
    if (!callee_declares_reset && gio->has_input("reset")) {
      if (reset_name_.empty()) {
        error_here("upass.tolg: instance of reset-carrying '{}' but '{}' has no reset to forward (needs_reset bug)",
                   callee_full,
                   lnast_->get_top_module_name());
        return;
      }
      Pin r = reset_pin();
      if (reset_neg_) {
        auto inv = make_node(Ntype_op::Not);
        setup_sink_by_name(inv, "a").connect_driver(r);
        r = inv.create_driver_pin(0);
        set_bits(r, 1);
        set_unsign(r);
      }
      sub.create_sink_pin("reset").connect_driver(r);
    }

    std::string dst_name(lnast_->get_name(dst));

    if (cio.outputs.size() > 1) {
      // Multi-output callee: the fcall result is a tuple; each
      // tuple_get(dst2, result, 'port') binds that port's driver pin
      // (lower_tuple_get below). Nothing binds the bare result name.
      // Create EVERY output pin now: downstream passes/cgen walk the
      // callee GraphIO and expect the pins to exist even when a port is
      // left unread (`.e()` unconnected-output style).
      for (const auto& oe2 : cio.outputs) {
        (void)sub.create_driver_pin(oe2.name);
      }
      sub_results_[dst_name] = Sub_result{sub, {cio.outputs.begin(), cio.outputs.end()}};
      return;
    }

    // Single output: bind dst like a graph input (external value entering).
    const auto& oe       = cio.outputs.front();
    auto        out_dpin = sub.create_driver_pin(oe.name);
    int32_t     mw       = io_mw(oe);
    if (oe.kind == Io_kind::boolean || mw <= 1) {
      set_bits(out_dpin, 1);
      if (oe.is_signed) {
        set_sign(out_dpin);
      } else {
        set_unsign(out_dpin);
      }
      record(dst_name, out_dpin, 1);
    } else if (oe.is_signed) {
      set_bits(out_dpin, mw);
      set_sign(out_dpin);
      record(dst_name, out_dpin, mw);
    } else {
      set_bits(out_dpin, mw);
      set_sign(out_dpin);
      record(dst_name, to_positive(out_dpin, mw), mw);
    }
    // The instance is a timed crossing: stamp its declared
    // latency interval (a following stage[N] re-stamps the pinned pick).
    // Bare-pipe unconstrained max (cmax<cmin) propagates as min (the
    // Phase-1 realization) — the io stages remain the caller-facing truth.
    // A callee output declared `@[]` (stages -1) carries no interval: the
    // instance propagates like a comb crossing and stage[] picks over it
    // fall back to the plain-RHS path.
    if (oe.stages_min >= 0) {
      const int64_t cmin = oe.stages_min;
      const int64_t cmax = oe.stages_max < oe.stages_min ? oe.stages_min : oe.stages_max;
      sub.attr(livehd::attrs::time_range).set({cmin, cmax});
      sub_time_[sub.get_debug_nid()] = {cmin, cmax};
      sub_out_stages_[dst_name]      = {oe.stages_min, oe.stages_max, kind == "pipe", sub};
    }
  }

  // Lower an undischarged timecheck statement to a pending
  // attr + record for the checker.
  void lower_timecheck(const Lnast_nid& nid) {
    auto ref = lnast_->get_first_child(nid);
    if (ref.is_invalid()) {
      return;
    }
    auto mn = lnast_->get_sibling_next(ref);
    if (mn.is_invalid()) {
      return;
    }
    auto mx = mn.is_invalid() ? mn : lnast_->get_sibling_next(mn);
    if (!mx.is_invalid()) {
      auto extra = lnast_->get_sibling_next(mx);
      if (!extra.is_invalid() && Lnast_ntype::is_const(lnast_->get_type(extra)) && lnast_->get_name(extra) == "checked") {
        return;  // discharged at LNAST
      }
    }
    const std::string name(lnast_->get_name(ref));
    auto              it = pin_map_.find(name);
    if (it == pin_map_.end()) {
      error_here("upass.tolg: `@[N]` check on '{}' — the value never materialized in the graph", name);
      return;
    }
    const int64_t a_min = const_val(mn);
    const int64_t a_max = mx.is_invalid() ? a_min : const_val(mx);
    it->second.attr(livehd::attrs::pending_time).set({a_min, a_max});
    pending_checks_.push_back({it->second, name, a_min, a_max});
  }

  // The clock graph-input pin. A minted implicit clock gets stamped 1-bit
  // unsigned on first use; a reused declared clk/clock input keeps the
  // width/sign the io loop already stamped.
  [[nodiscard]] Pin clock_pin() {
    if (!clock_pin_valid_) {
      auto p = g_->get_input_pin(clock_name_);
      if (clock_minted_) {
        set_bits(p, 1);
        set_unsign(p);
      }
      clock_pin_       = p;
      clock_pin_valid_ = true;
    }
    return clock_pin_;
  }

  // The module reset graph-input pin (same lazy stamping contract
  // as clock_pin).
  [[nodiscard]] Pin reset_pin() {
    if (!reset_pin_valid_) {
      auto p = g_->get_input_pin(reset_name_);
      if (reset_minted_) {
        set_bits(p, 1);
        set_unsign(p);
      }
      reset_pin_       = p;
      reset_pin_valid_ = true;
    }
    return reset_pin_;
  }

  // range(ref(dst), lo, hi) — record [lo,hi] for a later get_mask; no node.
  void lower_range(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto lo = lnast_->get_sibling_next(dst);
    if (lo.is_invalid()) {
      return;
    }
    auto hi = lnast_->get_sibling_next(lo);
    if (hi.is_invalid()) {
      return;
    }
    range_map_[std::string{lnast_->get_name(dst)}] = {const_val(lo), const_val(hi)};
  }

  [[nodiscard]] int64_t const_val(const Lnast_nid& nid) {
    auto c = Dlop::from_pyrope(lnast_->get_name(nid));
    return c->is_just_i64() ? c->to_just_i64() : 0;
  }

  // get_mask(ref(dst), value, mask) — mask is a const bitmask or a range ref.
  void lower_get_mask(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto val = lnast_->get_sibling_next(dst);
    if (val.is_invalid()) {
      return;
    }
    auto mask_op = lnast_->get_sibling_next(val);
    if (mask_op.is_invalid()) {
      return;
    }
    auto mask = mask_from_operand(mask_op);

    auto node = make_node(Ntype_op::Get_mask);
    setup_sink_by_name(node, "a").connect_driver(leaf(val).pin);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *mask));
    auto    drv = node.create_driver_pin(0);
    int32_t mw  = mask_popcount(*mask);
    bind_result(lnast_->get_name(dst), drv, mw);
  }

  // The mask of a get_mask/set_mask is a full Dlop, never an int64: a 64-bit
  // (or wider) mask like 2^64-1 (`0x0ffffffffffffffff`, a full-width truncate)
  // overflows int64 and would silently collapse to 0 — the value `from_pyrope`
  // parses correctly is kept as-is.
  [[nodiscard]] spool_ptr<Dlop> mask_from_operand(const Lnast_nid& mask_op) {
    if (Lnast_ntype::is_const(lnast_->get_type(mask_op))) {
      return Dlop::from_pyrope(lnast_->get_name(mask_op));
    }
    auto it = range_map_.find(std::string{lnast_->get_name(mask_op)});
    if (it != range_map_.end()) {
      auto [lo, hi] = it->second;
      if (hi < lo) {
        std::swap(lo, hi);
      }
      if (lo < 0 || hi < lo) {
        return Dlop::create_integer(0);
      }
      return Dlop::get_mask_value(static_cast<int>(hi), static_cast<int>(lo));  // multi-word capable, no 63-bit cap
    }
    warn_at(mask_op,
            {"mask-not-const", "unsupported"},
            "get_mask mask operand '{}' not const/range — using 0",
            lnast_->get_name(mask_op));
    return Dlop::create_integer(0);
  }

  // Number of set bits in a (non-negative) mask = the get_mask result width.
  static int32_t mask_popcount(const Dlop& m) {
    auto pc = m.popcount_op();
    return pc->is_just_i64() ? static_cast<int32_t>(pc->to_just_i64()) : 0;
  }

  // Highest set bit + 1 of a (non-negative) mask = the set_mask reach.
  static int32_t mask_high_bit(const Dlop& m) {
    int gb = m.is_positive() ? m.get_bits() : 0;  // get_bits() counts the sign bit too
    return gb > 0 ? static_cast<int32_t>(gb - 1) : int32_t{0};
  }

  // set_mask(ref(dst), value, mask, ins).
  void lower_set_mask(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto val = lnast_->get_sibling_next(dst);
    if (val.is_invalid()) {
      return;
    }
    auto mask_op = lnast_->get_sibling_next(val);
    if (mask_op.is_invalid()) {
      return;
    }
    auto ins = lnast_->get_sibling_next(mask_op);
    if (ins.is_invalid()) {
      return;
    }
    auto mask = mask_from_operand(mask_op);
    auto vv   = leaf(val);

    auto node = make_node(Ntype_op::Set_mask);
    setup_sink_by_name(node, "a").connect_driver(vv.pin);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *mask));
    setup_sink_by_name(node, "value").connect_driver(leaf(ins).pin);
    int32_t mask_mw = mask_high_bit(*mask);
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), std::max(vv.mw, mask_mw));
  }

  enum class OpW { add, mul, maxw, firstw, boolw, shlw };

  // n-ary op: child0 = dst, children 1..N = operands. Commutative ops feed all
  // operands into sink "a"; positional binary ops use "a" then "b".
  void lower_op(const Lnast_nid& nid, Ntype_op op, bool commutative, OpW wmode) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto    node      = make_node(op);
    int32_t max_mw    = 0;
    int32_t sum_mw    = 0;
    int32_t first_mw  = 0;
    int32_t second_mw = 0;       // shift-amount magnitude width (shlw)
    int64_t shl_amt   = -1;      // shift-amount value when constant (shlw); <0 = dynamic
    bool    first     = true;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      auto v  = leaf(c);
      max_mw  = std::max(max_mw, v.mw);
      sum_mw += v.mw;
      if (first) {
        first_mw = v.mw;
      } else if (second_mw == 0) {
        second_mw = v.mw;
        if (Lnast_ntype::is_const(lnast_->get_type(c))) {
          auto cv = Dlop::from_pyrope(lnast_->get_name(c));
          if (cv->is_just_i64()) {
            shl_amt = cv->to_just_i64();
          }
        }
      }
      setup_sink_by_name(node, (commutative || first) ? "a" : "b").connect_driver(v.pin);
      first = false;
    }
    int32_t mw = 1;
    switch (wmode) {
      case OpW::add   : mw = static_cast<int32_t>(max_mw + 1); break;
      case OpW::mul   : mw = sum_mw > 0 ? sum_mw : int32_t{1}; break;
      case OpW::maxw  : mw = max_mw; break;
      case OpW::firstw: mw = first_mw; break;
      case OpW::boolw : mw = 1; break;
      case OpW::shlw  : {
        // A left shift GROWS: out = a_width + shift_amount. A constant amount is
        // exact; a dynamic amount uses the 2^amount_width-1 upper bound (capped
        // to avoid pathological blow-up). Without this the result kept the input
        // width (old OpW::maxw) and `b<<N` silently truncated in intermediates.
        int64_t grow = shl_amt >= 0 ? shl_amt : (second_mw >= 12 ? int64_t{4096} : ((int64_t{1} << second_mw) - 1));
        mw           = static_cast<int32_t>(first_mw + grow);
        break;
      }
    }
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), mw);
  }

  // LNAST sext(dst, a, b): reinterpret bit POSITION b of `a` as the sign
  // (the Dlop::sext_op convention constprop folds with). The LGraph Sext
  // cell's b operand is the kept bit COUNT instead (cgen slices [b-1:0],
  // bitwidth ranges sbits=b, lgyosys Pick passes the width) - convert here.
  // The result is SIGNED with meaningful width b+1.
  void lower_sext(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto b = lnast_->get_sibling_next(a);
    if (b.is_invalid()) {
      return;
    }
    if (!Lnast_ntype::is_const(lnast_->get_type(b))) {
      warn_at(b, {"sext-runtime-pos", "unsupported"}, "sext with a runtime sign position has no lowering — wiring nil");
      bind_result(lnast_->get_name(dst), nil_pin(), 1);
      return;
    }
    const auto pos  = const_val(b);
    auto       av   = leaf(a);
    auto       node = make_node(Ntype_op::Sext);
    setup_sink_by_name(node, "a").connect_driver(av.pin);
    setup_sink_by_name(node, "b").connect_driver(create_const(*g_, *Dlop::create_integer(pos + 1)));
    const auto mw  = static_cast<int32_t>(pos) + 1;
    auto       drv = node.create_driver_pin(0);
    set_bits(drv, mw > 0 ? mw : 1);
    set_sign(drv);
    record(lnast_->get_name(dst), drv, mw > 0 ? mw : 1);
  }

  void lower_unary(const Lnast_nid& nid, Ntype_op op) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto v    = leaf(a);
    auto node = make_node(op);
    setup_sink_by_name(node, "a").connect_driver(v.pin);
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), v.mw);
  }

  // ne/le/ge = Not(eq/gt/lt(...)). Result is 1-bit boolean.
  void lower_negated(const Lnast_nid& nid, Ntype_op inner_op, bool commutative) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto inner = make_node(inner_op);
    bool first = true;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      setup_sink_by_name(inner, (commutative || first) ? "a" : "b").connect_driver(leaf(c).pin);
      first = false;
    }
    auto not_node = make_node(Ntype_op::Not);
    setup_sink_by_name(not_node, "a").connect_driver(inner.create_driver_pin(0));
    bind_result(lnast_->get_name(dst), not_node.create_driver_pin(0), 1);
  }

  // Lower an if-branch body into a fresh write scope; rolls pin_map_ back so
  // branch-local writes don't leak. Returns the names the branch bound.
  WriteMap lower_branch(const Lnast_nid& stmts) {
    branch_writes_.emplace_back();
    auto snapshot = pin_map_;
    if (Lnast_ntype::is_stmts(lnast_->get_type(stmts))) {
      lower_stmts(stmts);
    } else {
      lower_node(stmts);
    }
    auto writes = std::move(branch_writes_.back());
    branch_writes_.pop_back();
    for (const auto& [name, _] : writes) {
      auto it = snapshot.find(name);
      if (it == snapshot.end()) {
        pin_map_.erase(name);
      } else {
        pin_map_[name] = it->second;
      }
    }
    return writes;
  }

  // if(cond, then-stmts, [cond, stmts]*, [else-stmts]) -> per-variable binary
  // Mux chains. Mux pins: 0 = selector, 1 = false/else, 2 = true/then.
  //
  // unique_if (the `unique if` / `match` chain) declares the conditions
  // mutually exclusive, so the per-variable merge is ONE Hotmux instead: a
  // shared one-hot selector packs bit i = cond_i plus a final
  // none-of-the-conds bit (the else / fall-through slot), and values ride
  // p1..pN. The selector is one-hot by construction exactly when the
  // uniqueness assume holds; a violation makes it multi-hot, which the
  // Hotmux contract flags at runtime (cgen's case default).
  struct Branch {
    bool     is_else{false};
    Pin      cond;
    WriteMap writes;
  };

  void lower_if(const Lnast_nid& nid, bool unique = false) {
    std::vector<Branch> branches;

    auto child = lnast_->get_first_child(nid);
    if (child.is_invalid()) {
      return;
    }
    // 1a-mem — memory write enables need each branch's full path condition;
    // only built while a memory exists (zero overhead otherwise). `guard`
    // accumulates enclosing ∧ ¬(prior conds); a branch's path is guard ∧ cond.
    const bool track_path             = !mem_map_.empty();
    Pin        guard                  = track_path ? current_path_cond() : Pin{};
    auto       lower_branch_with_path = [&](const Lnast_nid& stmts, const Pin& path) {
      if (!track_path) {
        return lower_branch(stmts);
      }
      path_cond_.push_back(path);
      auto w = lower_branch(stmts);
      path_cond_.pop_back();
      return w;
    };

    Pin first_cond = leaf(child).pin;  // child0 = condition
    child          = lnast_->get_sibling_next(child);
    if (child.is_invalid()) {
      return;
    }
    branches.push_back({false, first_cond, lower_branch_with_path(child, track_path ? and2(guard, first_cond) : Pin{})});
    if (track_path) {
      guard = and2(guard, not1(first_cond));
    }

    child = lnast_->get_sibling_next(child);
    while (!child.is_invalid()) {
      bool last = lnast_->is_last_child(child);
      if (last && Lnast_ntype::is_stmts(lnast_->get_type(child))) {
        branches.push_back({true, Pin{}, lower_branch_with_path(child, guard)});  // bare else
        break;
      }
      Pin elif_cond = leaf(child).pin;
      child         = lnast_->get_sibling_next(child);
      if (child.is_invalid()) {
        break;
      }
      branches.push_back({false, elif_cond, lower_branch_with_path(child, track_path ? and2(guard, elif_cond) : Pin{})});
      if (track_path) {
        guard = and2(guard, not1(elif_cond));
      }
      child = lnast_->get_sibling_next(child);
    }

    absl::flat_hash_set<std::string> all_vars;
    for (auto& br : branches) {
      for (auto& [name, _] : br.writes) {
        all_vars.insert(name);
      }
    }

    const bool      has_else    = !branches.empty() && branches.back().is_else;
    const WriteMap& else_writes = has_else ? branches.back().writes : empty_writes_;

    if (unique && !all_vars.empty()) {
      lower_unique_merge(branches, all_vars, has_else, else_writes);
      return;
    }

    for (const auto& var : all_vars) {
      // `pre` is the var's value ENTERING this if (it already encodes every
      // prior statement's writes — e.g. an earlier separate `if(inr) ov<=0`).
      // A branch that does not write `var` must fall back to `pre`, NOT to the
      // else value: the else only applies on the all-conds-false path. Seeding
      // the chain's false arm (`cur`) with the else value while still using
      // `cur` as the fallback for non-writing branches (the old code) leaked
      // the else value up into the then/elif arms, clobbering `pre`. For a
      // reg's enable shadow this collapsed a conditional enable into constant
      // true (write-every-cycle); for its din it forced the else value.
      auto base      = pin_map_.find(var);
      const Pin pre  = (base != pin_map_.end()) ? base->second : nil_pin();
      auto      ew   = else_writes.find(var);
      Pin       cur  = (ew != else_writes.end()) ? ew->second : pre;

      // The merged value's width is the widest among the branch sources;
      // mw_lookup alone holds whatever the LAST write recorded (or 1 for a
      // never-bound io output), which under-sizes the mux and truncates the
      // wider arms. Take the max over every contributing pin's stamped bits
      // (bits >= mw by construction, so this only ever widens).
      auto pin_mw = [](const Pin& p) -> int32_t {
        if (auto bb = livehd::graph_util::bits_of(p); bb > 0) {
          return bb;
        }
        if (livehd::graph_util::is_const_pin(p)) {  // const pins carry no bits stamp
          auto v = livehd::graph_util::hydrate_const(p);
          if (v.is_just_i64()) {
            return mw_of_val(v.to_just_i64());
          }
          return std::max<int32_t>(1, static_cast<int32_t>(v.get_bits()));
        }
        return 0;
      };
      int32_t mw = std::max({mw_lookup(var), pin_mw(cur), pin_mw(pre)});
      int n         = static_cast<int>(branches.size());
      int last_cond = has_else ? n - 2 : n - 1;
      for (int i = last_cond; i >= 0; --i) {
        auto& br       = branches[i];
        auto  wr       = br.writes.find(var);
        Pin   true_val = (wr != br.writes.end()) ? wr->second : pre;
        if (wr != br.writes.end()) {
          mw = std::max(mw, pin_mw(wr->second));
        }

        auto mux = make_node(Ntype_op::Mux);
        mux.create_sink_pin(0).connect_driver(br.cond);   // selector
        mux.create_sink_pin(1).connect_driver(cur);       // false / else
        mux.create_sink_pin(2).connect_driver(true_val);  // true / then
        cur = mux.create_driver_pin(0);
      }
      bind_result(var, cur, mw);
    }
  }

  // unique_if merge: one Hotmux per variable over a shared one-hot selector.
  // Selector bit i (i < n_conds) is branches[i].cond; the top bit is
  // "none of the conds" — the else / fall-through slot. Hotmux pins:
  // 0 = one-hot selector, p(i+1) = arm i's value, p(n_conds+1) = else value
  // (the variable's pre-if value when the arm / else doesn't write it).
  void lower_unique_merge(const std::vector<Branch>& branches, const absl::flat_hash_set<std::string>& all_vars, bool has_else,
                          const WriteMap& else_writes) {
    const int n_conds = static_cast<int>(branches.size()) - (has_else ? 1 : 0);
    I(n_conds >= 1);

    // none = (OR of all conds) == 0: exactly one of {cond_0..cond_k, none}
    // is set when the uniqueness assume holds. EQ (not Not) on purpose: a
    // bitwise Not of a 1-bit bool carries infinite high bits (LSB-only by
    // convention, safe under And but NOT under the SHL/Or packing below).
    Pin or_all = branches[0].cond;
    if (n_conds > 1) {
      auto or_node = make_node(Ntype_op::Or);
      for (int i = 0; i < n_conds; ++i) {
        or_node.create_sink_pin(0).connect_driver(branches[i].cond);
      }
      or_all = or_node.create_driver_pin(0);
      set_bits(or_all, 1);
      set_unsign(or_all);
    }
    auto none_node = make_node(Ntype_op::EQ);
    none_node.create_sink_pin(0).connect_driver(or_all);
    none_node.create_sink_pin(0).connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    const Pin none = none_node.create_driver_pin(0);
    set_bits(none, 1);
    set_unsign(none);

    auto sel_node = make_node(Ntype_op::Or);
    for (int i = 0; i < n_conds; ++i) {
      sel_node.create_sink_pin(0).connect_driver(shl1_by(branches[i].cond, i));
    }
    sel_node.create_sink_pin(0).connect_driver(shl1_by(none, n_conds));
    auto sel = sel_node.create_driver_pin(0);
    // n_conds+1 one-hot positions -> magnitude n_conds+1 bits, +1 sign bit
    // (LiveHD `bits` is the signed width; cgen declares unsigned as bits-1).
    set_bits(sel, n_conds + 2);
    set_unsign(sel);

    for (const auto& var : all_vars) {
      auto base     = pin_map_.find(var);
      Pin  pre      = (base != pin_map_.end()) ? base->second : nil_pin();
      auto ew       = else_writes.find(var);
      Pin  else_val = (ew != else_writes.end()) ? ew->second : pre;

      auto hot = make_node(Ntype_op::Hotmux);
      hot.create_sink_pin(0).connect_driver(sel);
      for (int i = 0; i < n_conds; ++i) {
        auto wr = branches[i].writes.find(var);
        hot.create_sink_pin(static_cast<hhds::Port_id>(i + 1)).connect_driver(wr != branches[i].writes.end() ? wr->second : pre);
      }
      hot.create_sink_pin(static_cast<hhds::Port_id>(n_conds + 1)).connect_driver(else_val);
      bind_result(var, hot.create_driver_pin(0), mw_lookup(var));
    }
  }

  std::shared_ptr<Lnast>      lnast_;
  hhds::Graph*                g_;
  const uPass_tolg::Registry* registry_ = nullptr;
  hhds::GraphLibrary*         lib_      = nullptr;

  absl::flat_hash_map<std::string, Pin>                         pin_map_;
  absl::flat_hash_map<std::string, int32_t>                     mw_map_;
  // 2f-defer — the last driver written to each LOGICAL variable (SSA versions
  // x / x___ssa_1 / … collapsed to "x"): `x.[defer]` reads the value after ALL
  // in-cycle writes, i.e. the last SSA version's driver, not the read-site one.
  absl::flat_hash_map<std::string, std::pair<Pin, int32_t>>     logical_last_;
  // 2f-defer — a field read whose source is a Sub result created by a call
  // lowered LATER in the body (`c = tmp.add.[defer]` reads tmp.add before
  // `tmp = add_sub(…)` runs). Deferred to end-of-pass, then re-resolved with
  // tget_final_ so a still-unresolved one warns instead of looping.
  std::vector<Lnast_nid>                                        pending_tgets_;
  bool                                                          tget_final_ = false;
  absl::flat_hash_map<std::string, std::pair<int64_t, int64_t>> range_map_;
  std::vector<WriteMap>                                         branch_writes_;
  std::vector<std::string>                                      out_names_;
  WriteMap                                                      empty_writes_;

  // 2f-defer — `x.[defer]` is an end-of-cycle read: the edge from the base's
  // FINAL driver is added after every statement is processed (the runner's
  // single walk doesn't know the final dpin at the read site). Each entry is a
  // passthrough buffer's input sink waiting for its base's last dpin.
  struct Defer_pend {
    Pin         sink;  // the buffer's "A" input sink (consumers already read its output)
    Pin         out;   // the buffer's output driver (restamped from the base width)
    std::string base;  // the base variable / dotted field path
    Lnast_nid   src;   // for diagnostics (the `.[defer]` site)
  };
  std::vector<Defer_pend>                                       defer_pends_;

  // Reg lowering state. reg_map_ holds each declared reg's Flop
  // node (reads resolve to its q via pin_map_; stores rebind the shadow
  // din/enable keys). clock_*/reset_* lazily bind the clock/reset graph
  // inputs. reg_info_/reg_order_ carry the finalize metadata for
  // PLAIN regs (stage regs live only in reg_map_/flop_depth_).
  absl::flat_hash_map<std::string, hhds::Node_class> reg_map_;
  absl::flat_hash_map<std::string, Reg_info>         reg_info_;
  std::vector<std::string>                           reg_order_;
  // Declared memories (array-typed regs + mut/const arrays), the
  // branch-path stack lower_if maintains for their write enables, the
  // recorded tuple literals (array initializers / __memory configs), and the
  // bound __memory results.
  absl::flat_hash_map<std::string, Mem_info>         mem_map_;
  std::vector<std::string>                           mem_order_;
  std::vector<Pin>                                   path_cond_;
  absl::flat_hash_map<std::string, Tuple_rec>        tuple_recs_;
  absl::flat_hash_map<std::string, Mem_result>       mem_results_;
  // attr_set seen before its target's declare (memory fwd overrides etc).
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, std::string>> pending_attrs_;
  std::string                                        clock_name_;
  bool                                               clock_minted_ = false;
  Pin                                                clock_pin_;
  bool                                               clock_pin_valid_ = false;
  std::string                                        reset_name_;
  bool                                               reset_minted_        = false;
  bool                                               reset_neg_           = false;
  bool                                               reset_async_default_ = false;
  Pin                                                reset_pin_;
  bool                                               reset_pin_valid_ = false;
  Pin                                                en_true_pin_;
  Pin                                                en_false_pin_;
  bool                                               en_true_valid_  = false;
  bool                                               en_false_valid_ = false;

  absl::flat_hash_map<std::string, Pending_stage> pending_stage_;
  absl::flat_hash_map<std::string, Sub_out>       sub_out_stages_;
  // Multi-output instance results: fcall dst name -> (Sub node, callee
  // outputs); consumed by tuple_get field reads.
  struct Sub_result {
    hhds::Node_class            sub;
    std::vector<Lnast_io_entry> outputs;
  };
  absl::flat_hash_map<std::string, Sub_result> sub_results_;

  // Checker inputs gathered while building: pending records,
  // per-Flop effective crossing depth, per-Sub pinned latency interval.
  // Also adds plain_reg_flops_ (state/stage classification candidates) and
  // inserted_flops_ (the LN-inserted pipe output flops — narrowing targets).
  std::vector<Pending_rec>                                   pending_checks_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> flop_depth_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> sub_time_;
  absl::flat_hash_map<uint64_t, std::string>                 plain_reg_flops_;
  absl::flat_hash_set<uint64_t>                              inserted_flops_;
  // 2f-defer — the `.[defer]` passthrough buffers. A defer edge may legally
  // close a same-cycle cycle (Verilog allows comb loops; a later lgraph pass
  // detects/handles real ones), so the time-checker cuts these nodes' in-edges
  // instead of flagging the loop.
  absl::flat_hash_set<uint64_t>                              defer_cut_nids_;

public:
  // Lower the partition's declared per-output intervals as
  // pending checks on the GraphIO output sinks (mod: the @[N] landing cycle;
  // pipe: the declared range — comb bodies must land at exactly that range,
  // sigma>0 narrowing lights up here). `@[]`-opted-out outputs (nil) are
  // skipped. Called at the end of build().
  void stamp_output_pendings() {
    const auto kind = lnast_->get_lambda_kind();
    if (kind != "pipe" && kind != "mod") {
      return;
    }
    auto      root = lnast_->get_root();
    Lnast_nid io_nid;
    for (auto c = lnast_->get_first_child(root); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (Lnast_ntype::is_io(lnast_->get_type(c))) {
        io_nid = c;
        break;
      }
    }
    if (io_nid.is_invalid()) {
      return;
    }
    auto in_tup = lnast_->get_first_child(io_nid);
    if (in_tup.is_invalid()) {
      return;
    }
    auto out_tup = lnast_->get_sibling_next(in_tup);
    if (out_tup.is_invalid()) {
      return;
    }
    for (auto st = lnast_->get_first_child(out_tup); !st.is_invalid(); st = lnast_->get_sibling_next(st)) {
      if (!Lnast_ntype::is_store(lnast_->get_type(st))) {
        continue;
      }
      auto name_nid = lnast_->get_first_child(st);
      if (name_nid.is_invalid()) {
        continue;
      }
      Lnast_nid stages_nid;
      for (auto c = lnast_->get_sibling_next(name_nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
        if (Lnast_ntype::is_stages(lnast_->get_type(c))) {
          stages_nid = c;
          break;
        }
      }
      if (stages_nid.is_invalid()) {
        continue;
      }
      auto mn = lnast_->get_first_child(stages_nid);
      if (mn.is_invalid()) {
        continue;
      }
      auto mx = lnast_->get_sibling_next(mn);
      if (lnast_->get_name(mn) == "nil" || (!mx.is_invalid() && lnast_->get_name(mx) == "nil")) {
        continue;  // @[] opt-out — unconstrained
      }
      int64_t a_min = const_val(mn);
      int64_t a_max = mx.is_invalid() ? a_min : const_val(mx);
      if (a_max < a_min) {
        a_max = a_min;  // bare-pipe (1,0) sentinel realizes at min
      }
      const std::string name(lnast_->get_name(name_nid));
      auto              sink = g_->get_output_pin(name);
      if (sink.is_invalid()) {
        continue;
      }
      sink.attr(livehd::attrs::pending_time).set({a_min, a_max});
      pending_checks_.push_back({sink, name, a_min, a_max, /*is_sink=*/true});
    }
  }

  [[nodiscard]] std::vector<Pending_rec>&& take_pending_checks() { return std::move(pending_checks_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& take_flop_depths() { return std::move(flop_depth_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& take_sub_times() { return std::move(sub_time_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::string>&& take_plain_reg_flops() { return std::move(plain_reg_flops_); }
  [[nodiscard]] absl::flat_hash_set<uint64_t>&&              take_inserted_flops() { return std::move(inserted_flops_); }
  [[nodiscard]] absl::flat_hash_set<uint64_t>&&              take_defer_cuts() { return std::move(defer_cut_nids_); }
};

// The combined pipe/mod LG time checker (written once for both kinds).
// Runs at the tolg seam on the just-built
// graph:
//   1. Tarjan SCC over the node digraph (flop din->q edges included, i.e.
//      node-level cycles). A non-trivial SCC must contain a STATE-eligible
//      flop (a plain reg); an SCC with only stage/inserted flops is
//      a cross-stage register feedback error, one with no flop at all is the
//      classic combinational-loop error. A plain reg is also STATE when its
//      `enable` is driven (conditional write = enable-encoded hold feedback,
//      invisible to SCC); every other flop is a STAGE crossing.
//   2. Forward (min,max) interval propagation, twice: graph inputs (0,0);
//      constants unify with anything; comb cells take the equal-meet of
//      their operands (a mismatch is the 06c misalignment error); a stage
//      Flop adds its effective crossing depth; a Sub adds its pinned
//      instance interval (clock sinks excluded). Pass 1 treats every state
//      flop's q as unconstrained; then sigma(q) := sigma(din) (state regs
//      pin to their HOME stage — no crossing) and pass 2 re-checks every
//      meet with the pinned values.
//   3. Narrow each LN-inserted pipe output flop to the body deficit
//      (min−sigma, max−sigma); (0,0) realizes as a wire (the flop is
//      bypassed and deleted). sigma > min is the latency-exceeded error.
//   4. Discharge every pending record: computed == asserted -> REMOVE the
//      pending_time attr; mismatch (or an undischargeable record) -> error.
//      A declared-reg output (`-> (reg q@[N])`) discharges against home+1:
//      state home stage must equal N−1 (and the reg must be state — a
//      feedforward stage reg in the output list is an error).
class Time_checker {
public:
  struct TR {
    int64_t min = 0;
    int64_t max = 0;
    bool    any = false;  // constants — unify with any cycle
  };

  Time_checker(hhds::Graph* g, const std::shared_ptr<Lnast>& ln, std::vector<Pending_rec>&& pendings,
               absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& flop_depth,
               absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& sub_time,
               absl::flat_hash_map<uint64_t, std::string>&& plain_regs, absl::flat_hash_set<uint64_t>&& inserted,
               absl::flat_hash_set<uint64_t>&& defer_cuts)
      : g_(g)
      , ln_(ln)
      , pendings_(std::move(pendings))
      , flop_depth_(std::move(flop_depth))
      , sub_time_(std::move(sub_time))
      , plain_regs_(std::move(plain_regs))
      , inserted_(std::move(inserted))
      , defer_cuts_(std::move(defer_cuts)) {
    for (const auto& [nid, name] : plain_regs_) {
      reg_flop_by_name_.emplace(name, nid);
    }
  }

  // Stage a Diagnostic located via the graph node's srcid (stamped
  // by the lowering walk, resolved through the graph's locator) before the
  // Pass::error-style throw. An id-less or invalid node degrades to an
  // unlocated record.
  template <typename... Args>
  [[noreturn]] void error_at_node(const hhds::Node_class& node, livehd::diag::Id id, std::format_string<Args...> fmt,
                                  Args&&... args) {
    auto                            msg = std::format(fmt, std::forward<Args>(args)...);
    livehd::diag::Span              span;
    std::vector<livehd::diag::Note> notes;
    if (g_ != nullptr && !node.is_invalid()) {
      if (auto ref = node.attr(hhds::attrs::srcid); ref.has()) {
        const auto rs = g_->source_locator().resolve_spans(ref.get());
        span          = rs.primary;
        notes         = livehd::diag::notes_from(rs, "reached via this site");
      }
    }
    livehd::diag::sink().stage(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = std::string(id.code),
        .category = std::string(id.category),
        .pass     = "upass.tolg",
        .message  = msg,
        .span     = std::move(span),
        .notes    = std::move(notes),
    });
    err_tracker::logger(msg);
    throw Eprp::parser_error(Pass::eprp, msg);
  }

  template <typename... Args>
  [[noreturn]] void error_at_node(const hhds::Node_class& node, std::format_string<Args...> fmt, Args&&... args) {
    error_at_node(node, livehd::diag::Id{"tolg-time-error", "time"}, "{}", std::format(fmt, std::forward<Args>(args)...));
  }

  // The pending record's value driver, as the diag anchor (invalid when
  // undriven -- error_at_node degrades to an unlocated record).
  [[nodiscard]] static hhds::Node_class pending_anchor(const auto& rec) {
    if (rec.is_sink) {
      auto edges = rec.pin.inp_edges();
      return edges.empty() ? hhds::Node_class{} : edges.front().driver.get_master_node();
    }
    return rec.pin.get_master_node();
  }

  void run() {
    using livehd::graph_util::is_graph_input_pin;
    using livehd::graph_util::is_type_const;
    using livehd::graph_util::is_type_flop;
    using livehd::graph_util::type_op_of;

    // 1. Collect nodes + node-level digraph (consts/graph-inputs excluded).
    std::vector<hhds::Node_class>         nodes;
    absl::flat_hash_map<uint64_t, size_t> idx;
    for (auto n : g_->fast_class()) {
      if (is_type_const(n)) {
        continue;
      }
      idx.emplace(n.get_debug_nid(), nodes.size());
      nodes.push_back(n);
    }
    const size_t                  nn = nodes.size();
    std::vector<std::vector<int>> succ(nn);
    std::vector<std::vector<int>> pred(nn);
    auto                          node_idx_of_pin = [&](const hhds::Pin_class& dpin) -> int {
      if (dpin.is_invalid() || is_graph_input_pin(dpin)) {
        return -1;
      }
      auto mn = dpin.get_master_node();
      if (mn.is_invalid() || is_type_const(mn) || type_op_of(mn) == Ntype_op::Nconst) {
        return -1;
      }
      auto it = idx.find(mn.get_debug_nid());
      return it == idx.end() ? -1 : static_cast<int>(it->second);
    };
    for (size_t i = 0; i < nn; ++i) {
      // 2f-defer — a `.[defer]` buffer is an end-of-cycle wire: cut its in-edge
      // for loop detection (like a state flop's q). A defer feedback may legally
      // form a same-cycle cycle; a later lgraph pass handles real comb loops.
      if (defer_cuts_.contains(nodes[i].get_debug_nid())) {
        continue;
      }
      for (const auto& e : nodes[i].inp_edges()) {
        const int p = node_idx_of_pin(e.driver);
        if (p >= 0) {
          pred[i].push_back(p);
          succ[static_cast<size_t>(p)].push_back(static_cast<int>(i));
        }
      }
    }

    // 2. Tarjan SCC (iterative). Non-trivial SCCs classify their flops.
    std::vector<int> scc_id(nn, -1);
    {
      std::vector<int>    low(nn, -1), num(nn, -1);
      std::vector<bool>   on_stack(nn, false);
      std::vector<int>    stk;
      int                 counter  = 0;
      int                 next_scc = 0;
      std::vector<size_t> scc_size;
      for (size_t root = 0; root < nn; ++root) {
        if (num[root] >= 0) {
          continue;
        }
        // explicit DFS: (node, next-successor-cursor)
        std::vector<std::pair<int, size_t>> dfs;
        dfs.emplace_back(static_cast<int>(root), 0);
        num[root] = low[root] = counter++;
        stk.push_back(static_cast<int>(root));
        on_stack[root] = true;
        while (!dfs.empty()) {
          auto& [v, cur] = dfs.back();
          if (cur < succ[static_cast<size_t>(v)].size()) {
            const int w = succ[static_cast<size_t>(v)][cur++];
            if (num[w] < 0) {
              num[w] = low[w] = counter++;
              stk.push_back(w);
              on_stack[w] = true;
              dfs.emplace_back(w, 0);
            } else if (on_stack[w]) {
              low[v] = std::min(low[v], num[w]);
            }
            continue;
          }
          if (low[v] == num[v]) {
            size_t members = 0;
            int    w;
            do {
              w = stk.back();
              stk.pop_back();
              on_stack[w] = false;
              scc_id[w]   = next_scc;
              ++members;
            } while (w != v);
            scc_size.push_back(members);
            ++next_scc;
          }
          const int done = v;
          dfs.pop_back();
          if (!dfs.empty()) {
            low[dfs.back().first] = std::min(low[dfs.back().first], low[done]);
          }
        }
      }
      // self-loops count as non-trivial too
      std::vector<bool> nontrivial(static_cast<size_t>(next_scc), false);
      for (size_t i = 0; i < nn; ++i) {
        if (scc_size[static_cast<size_t>(scc_id[i])] > 1) {
          nontrivial[static_cast<size_t>(scc_id[i])] = true;
        }
        for (int s : succ[i]) {
          if (s == static_cast<int>(i)) {
            nontrivial[static_cast<size_t>(scc_id[i])] = true;
          }
        }
      }
      // Classify: every non-trivial SCC needs a state-eligible flop.
      const auto en_pid = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, "enable"));
      for (size_t i = 0; i < nn; ++i) {
        if (!is_type_flop(nodes[i])) {
          continue;
        }
        const auto nid      = nodes[i].get_debug_nid();
        const bool eligible = plain_regs_.contains(nid);
        if (eligible) {
          bool en_driven = false;
          for (const auto& e : nodes[i].inp_edges()) {
            if (!e.sink.is_invalid() && static_cast<uint64_t>(e.sink.get_port_id()) == en_pid && !e.driver.is_invalid()) {
              en_driven = true;
              break;
            }
          }
          if (en_driven || nontrivial[static_cast<size_t>(scc_id[i])]) {
            state_.insert(nid);
          }
        }
      }
      for (int s = 0; s < next_scc; ++s) {
        if (!nontrivial[static_cast<size_t>(s)]) {
          continue;
        }
        bool             has_state = false;
        bool             has_flop  = false;
        hhds::Node_class rep;  // a member of the offending SCC, for the diag anchor
        for (size_t i = 0; i < nn; ++i) {
          if (scc_id[i] != s) {
            continue;
          }
          if (rep.is_invalid()) {
            rep = nodes[i];
          }
          if (is_type_flop(nodes[i])) {
            has_flop = true;
            if (state_.contains(nodes[i].get_debug_nid())) {
              has_state = true;
            }
          }
        }
        if (!has_state) {
          if (has_flop) {
            error_at_node(rep,
                          "upass.tolg: '{}' has register feedback through stage registers — make the looping register a plain "
                          "`reg` (state)",
                          ln_->get_top_module_name());
          } else {
            error_at_node(rep, "upass.tolg: combinational loop in '{}'", ln_->get_top_module_name());
          }
        }
      }
    }

    // 3. Kahn topo with state-flop in-edges cut (their q is pinned later,
    // not derived from din during the forward pass). Leftover cycle =
    // a comb loop that the state cut did not break.
    std::vector<int> indeg(nn, 0);
    for (size_t i = 0; i < nn; ++i) {
      if (state_.contains(nodes[i].get_debug_nid())) {
        continue;  // source: q decoupled from din
      }
      indeg[i] = static_cast<int>(pred[i].size());
    }
    std::vector<size_t> queue;
    std::vector<size_t> order;
    order.reserve(nn);
    for (size_t i = 0; i < nn; ++i) {
      if (indeg[i] == 0) {
        queue.push_back(i);
      }
    }
    while (!queue.empty()) {
      const size_t i = queue.back();
      queue.pop_back();
      order.push_back(i);
      for (int s : succ[i]) {
        if (state_.contains(nodes[static_cast<size_t>(s)].get_debug_nid())) {
          continue;
        }
        if (--indeg[static_cast<size_t>(s)] == 0) {
          queue.push_back(static_cast<size_t>(s));
        }
      }
    }
    if (order.size() < nn) {
      // state flops never enter the order (indeg cut makes them sources —
      // they ARE in the order); a shortfall is a residual comb cycle.
      hhds::Node_class rep;
      for (size_t i = 0; i < nn; ++i) {
        if (indeg[i] > 0) {
          rep = nodes[i];  // a node still on the cycle — the diag anchor
          break;
        }
      }
      error_at_node(rep, "upass.tolg: combinational loop in '{}'", ln_->get_top_module_name());
    }

    // 4. Forward σ with state q pinned to σ(din). Pass 1 leaves state q
    // unconstrained, then we pin σ(q):=σ(din) and re-propagate. A state
    // flop's din may transit through ANOTHER state flop's q, so iterate to a
    // fixpoint (bounded by the state count) — otherwise a chained state reg
    // would home at `any` and silently pass its `@[N]` check.
    const auto din_pid    = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, "din"));
    auto       din_driver = [&](const hhds::Node_class& flop) -> hhds::Pin_class {
      for (const auto& e : flop.inp_edges()) {
        if (!e.sink.is_invalid() && static_cast<uint64_t>(e.sink.get_port_id()) == din_pid) {
          return e.driver;
        }
      }
      return {};
    };
    for (const size_t i : order) {
      eval_node(nodes[i]);
    }
    const size_t state_count = state_.size();
    for (size_t pass = 0; pass <= state_count; ++pass) {
      bool changed = false;
      for (size_t i = 0; i < nn; ++i) {
        const auto nid = nodes[i].get_debug_nid();
        if (!state_.contains(nid)) {
          continue;
        }
        const TR pinned = pin_tr(din_driver(nodes[i]));  // σ(q) = σ(din)
        auto     it     = tr_.find(nid);
        if (it == tr_.end() || it->second.any != pinned.any || it->second.min != pinned.min || it->second.max != pinned.max) {
          tr_[nid] = pinned;
          changed  = true;
        }
      }
      for (const size_t i : order) {
        if (state_.contains(nodes[i].get_debug_nid())) {
          continue;  // pinned above
        }
        eval_node(nodes[i]);
      }
      if (!changed) {
        break;
      }
    }

    // 5. Narrow LN-inserted pipe output flops to the body deficit.
    for (const auto& rec : pendings_) {
      if (!rec.is_sink) {
        continue;
      }
      auto edges = rec.pin.inp_edges();
      if (edges.empty()) {
        continue;
      }
      auto mn = edges.front().driver.get_master_node();
      if (mn.is_invalid() || !inserted_.contains(mn.get_debug_nid())) {
        continue;
      }
      const TR sb = pin_tr(din_driver(mn));
      if (sb.any || sb.min != sb.max) {
        continue;  // const-driven or ranged body sigma — declared depth stands
      }
      const int64_t sigma = sb.min;
      if (sigma > rec.min) {
        error_at_node(mn,
                      "upass.tolg: output '{}' of '{}' lands at stage {}, pipe declares {}",
                      rec.name,
                      ln_->get_top_module_name(),
                      sigma,
                      rec.min);
      }
      const int64_t nmin = rec.min - sigma;
      const int64_t nmax = rec.max - sigma;
      replace_const_sink(mn, "pipe_min", nmin);
      replace_const_sink(mn, "pipe_max", nmax);
      flop_depth_[mn.get_debug_nid()] = {nmin, nmax};
      if (nmin == 0 && nmax == 0) {
        // (0,0) realizes as a wire: bypass and delete the flop.
        auto din = din_driver(mn);
        edges.front().del_edge();
        rec.pin.connect_driver(din);
        mn.del_node();
      } else {
        tr_[mn.get_debug_nid()] = {sigma + nmin, sigma + nmax, false};
      }
    }

    // 6. Discharge pendings.
    for (const auto& rec : pendings_) {
      // Declared-reg output (`-> (reg q@[N])`). A STATE reg's flop crossing
      // IS the interface cycle: home stage sigma(din) must be N−1. A
      // feedforward (stage) reg as output is rejected for a PIPE (06c — the
      // output is already registered by the contract); for a MOD it is just
      // a registered output and sigma(q)=sigma(din)+1 discharges through
      // the normal path below.
      if (rec.is_sink) {
        if (auto rit = reg_flop_by_name_.find(rec.name); rit != reg_flop_by_name_.end()) {
          const auto fnid     = rit->second;
          const bool is_state = state_.contains(fnid);
          if (!is_state && ln_->get_lambda_kind() == "pipe") {
            error_at_node(pending_anchor(rec),
                          {"pipe-output-reg", "time"},
                          "feedforward register '{}' in the output list of '{}' — the output is already "
                          "registered by the pipe contract",
                          rec.name,
                          ln_->get_top_module_name());
          }
          if (is_state) {
            if (rec.min != rec.max) {
              error_at_node(pending_anchor(rec),
                            {"reg-output-cycle", "time"},
                            "register output '{}' of '{}' needs a fixed declared cycle (got [{}, {}])",
                            rec.name,
                            ln_->get_top_module_name(),
                            rec.min,
                            rec.max);
            }
            const TR home = tr_.contains(fnid) ? tr_.at(fnid) : TR{0, 0, true};
            if (!home.any && home.min + 1 != rec.min) {
              error_at_node(pending_anchor(rec),
                            {"reg-output-cycle", "time"},
                            "state register '{}' of '{}' homes at stage {} but its declared landing cycle {} "
                            "requires home {}",
                            rec.name,
                            ln_->get_top_module_name(),
                            home.min,
                            rec.min,
                            rec.min - 1);
            }
            rec.pin.attr(livehd::attrs::pending_time).del();
            continue;
          }
        }
      }
      TR               cur;
      hhds::Node_class anchor_node;  // the value's driver cell, for the diag span
      if (rec.is_sink) {
        auto edges = rec.pin.inp_edges();
        if (edges.empty()) {
          continue;  // undriven output already warned/nil-wired
        }
        cur         = pin_tr(edges.front().driver);
        anchor_node = edges.front().driver.get_master_node();
      } else {
        cur         = pin_tr(rec.pin);
        anchor_node = rec.pin.get_master_node();
      }
      if (cur.any || (cur.min == rec.min && cur.max == rec.max)) {
        rec.pin.attr(livehd::attrs::pending_time).del();  // removed once checked
        continue;
      }
      error_at_node(anchor_node,
                    "upass.tolg: '{}' in '{}' lands at cycle(s) ({},{}) but ({},{}) is {}",
                    rec.name,
                    ln_->get_top_module_name(),
                    cur.min,
                    cur.max,
                    rec.min,
                    rec.max,
                    rec.is_sink ? "declared at the interface" : "asserted by `@[N]`");
    }
  }

private:
  [[nodiscard]] TR pin_tr(const hhds::Pin_class& dpin) {
    using livehd::graph_util::is_graph_input_pin;
    using livehd::graph_util::is_type_const;
    using livehd::graph_util::type_op_of;
    if (dpin.is_invalid()) {
      return {0, 0, true};
    }
    if (is_graph_input_pin(dpin)) {
      return {0, 0, false};
    }
    auto mn = dpin.get_master_node();
    if (mn.is_invalid() || is_type_const(mn) || type_op_of(mn) == Ntype_op::Nconst) {
      return {0, 0, true};
    }
    auto it = tr_.find(mn.get_debug_nid());
    if (it == tr_.end()) {
      return {0, 0, true};
    }
    return it->second;
  }

  // Replace a comptime const sink (pipe_min/pipe_max) with a new value.
  void replace_const_sink(const hhds::Node_class& node, std::string_view pin_name, int64_t value) {
    const auto pid = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, pin_name));
    for (const auto& e : node.inp_edges()) {
      if (!e.sink.is_invalid() && static_cast<uint64_t>(e.sink.get_port_id()) == pid) {
        e.del_edge();
        break;
      }
    }
    setup_sink_by_name(const_cast<hhds::Node_class&>(node), pin_name)
        .connect_driver(create_const(*g_, *Dlop::create_integer(value)));
  }

  void eval_node(const hhds::Node_class& node) {
    using livehd::graph_util::is_type_flop;
    using livehd::graph_util::is_type_sub;
    using livehd::graph_util::type_op_of;

    const auto nid = node.get_debug_nid();

    // A state flop's q is pinned (sigma(q)=sigma(din)) outside this
    // function; pass 1 leaves it unconstrained.
    if (state_.contains(nid)) {
      if (!tr_.contains(nid)) {
        tr_[nid] = {0, 0, true};
      }
      return;
    }

    TR meet{0, 0, true};

    // Operand selection per kind: a Flop reads only din; a Sub skips its
    // clock/reset sinks; a Memory skips its clock pins; a Mux/Hotmux skips
    // its SELECT and unions the arm intervals (two paths of different depth
    // = a cycle RANGE, the declaration covers it with `@[a..=b]` or `@[]`).
    // When EVERY arm unifies (constants — e.g. the OR-of-conditions enable
    // chain muxes true/false), the select's σ times the value instead.
    // Everything else meets all inputs (consts unify).
    absl::flat_hash_set<uint64_t> skip_pids;
    bool                          din_only    = false;
    const bool                    is_mem      = type_op_of(node) == Ntype_op::Memory;
    bool                          mem_clocked = false;
    const bool                    is_mux      = type_op_of(node) == Ntype_op::Mux || type_op_of(node) == Ntype_op::Hotmux;
    TR                            mux_sel{0, 0, true};
    if (is_mux) {
      skip_pids.insert(0);  // pid 0 = "s" — the select never adds path depth
      for (const auto& e : node.inp_edges()) {
        if (!e.sink.is_invalid() && e.sink.get_port_id() == 0) {
          mux_sel = pin_tr(e.driver);
          break;
        }
      }
    }
    if (is_type_flop(node)) {
      din_only = true;
    } else if (is_mem) {
      // 2f-mem — a memory is treated like a register (08-memories.md): an
      // ASYNC read (type 0/2) returns committed state with no added stage,
      // exactly like a scalar reg read — @[0] from the read address. Only a
      // SYNC read (type=1, registered dout) charges the +1 crossing. The
      // clock sinks are excluded from the meet either way.
      constexpr int kStride = 12;  // == uPass_tolg kMemPortStride (Memory sink count)
      for (const auto& e : node.inp_edges()) {
        if (e.sink.is_invalid()) {
          continue;
        }
        const auto raw_pid   = static_cast<int>(e.sink.get_port_id());
        const auto sink_name = Ntype::get_sink_name(Ntype_op::Memory, raw_pid % kStride);
        if (sink_name == "clock_pin" && raw_pid < kStride) {
          skip_pids.insert(static_cast<uint64_t>(raw_pid));
        } else if (sink_name == "type" && raw_pid < kStride) {
          skip_pids.insert(static_cast<uint64_t>(raw_pid));
          if (auto v = livehd::graph_util::hydrate_const(e.driver); v.is_just_i64() && v.to_just_i64() == 1) {
            mem_clocked = true;  // sync read: dout is registered
          }
        }
      }
    } else if (is_type_sub(node)) {
      auto gio = node.get_subnode_io();
      if (gio) {
        for (const auto& d : gio->get_input_pin_decls()) {
          if (d.name == "clock" || d.name == "clk" || d.name == "reset" || d.name == "rst" || d.name == "reset_n"
              || d.name == "rst_n") {
            skip_pids.insert(static_cast<uint64_t>(d.port_id));
          }
        }
      }
    }
    const auto din_pid = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, "din"));

    for (const auto& e : node.inp_edges()) {
      if (e.sink.is_invalid()) {
        continue;
      }
      const auto spid = static_cast<uint64_t>(e.sink.get_port_id());
      if (din_only && spid != din_pid) {
        continue;
      }
      if (!din_only && !skip_pids.empty() && skip_pids.contains(spid)) {
        continue;
      }
      TR t = pin_tr(e.driver);
      if (t.any) {
        continue;
      }
      if (meet.any) {
        meet = t;
        continue;
      }
      if (meet.min != t.min || meet.max != t.max) {
        if (is_mux) {
          meet = {std::min(meet.min, t.min), std::max(meet.max, t.max), false};
          continue;
        }
        error_at_node(node,
                      "upass.tolg: '{}' mixes values at different cycles (({},{}) vs ({},{})) at a {} cell (sink pid {}) "
                      "— align them with `stage[N]` first",
                      ln_->get_top_module_name(),
                      meet.min,
                      meet.max,
                      t.min,
                      t.max,
                      Ntype::get_name(type_op_of(node)),
                      spid);
      }
    }

    if (is_mux && meet.any) {
      meet = mux_sel;  // const-armed mux: the select times the value
    }

    TR out = meet.any ? TR{0, 0, true} : meet;
    if (is_mem) {
      if (mem_clocked) {
        if (out.any) {
          out = {0, 0, false};
        }
        out = {out.min + 1, out.max + 1, false};
      }
    } else if (is_type_flop(node)) {
      auto    it   = flop_depth_.find(nid);
      int64_t dmin = 1;
      int64_t dmax = 1;
      if (it != flop_depth_.end()) {
        dmin = it->second.first;
        dmax = it->second.second < it->second.first ? it->second.first : it->second.second;
      }
      if (out.any) {
        out = {0, 0, false};
      }
      out = {out.min + dmin, out.max + dmax, false};
    } else if (is_type_sub(node)) {
      auto    it   = sub_time_.find(nid);
      int64_t dmin = it != sub_time_.end() ? it->second.first : 0;
      int64_t dmax = it != sub_time_.end() ? it->second.second : 0;
      if (out.any) {
        out = {0, 0, false};
      }
      out = {out.min + dmin, out.max + dmax, false};
    }
    tr_[nid] = out;
  }

  hhds::Graph*                                               g_;
  std::shared_ptr<Lnast>                                     ln_;
  std::vector<Pending_rec>                                   pendings_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> flop_depth_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> sub_time_;
  absl::flat_hash_map<uint64_t, std::string>                 plain_regs_;
  absl::flat_hash_set<uint64_t>                              inserted_;
  absl::flat_hash_set<uint64_t>                              defer_cuts_;  // 2f-defer: cut these in-edges
  absl::flat_hash_map<std::string, uint64_t>                 reg_flop_by_name_;
  absl::flat_hash_set<uint64_t>                              state_;
  absl::flat_hash_map<uint64_t, TR>                          tr_;
};

// Transitive clock need. A module needs a clock when its own
// tree declares a reg (stage decls synthesize reg declares) or when any
// pipe/mod CALLEE transitively needs one (its instance's minted clock pin
// must be forwarded). Memoized over the registry; an instantiation cycle
// (illegal mutual hierarchy) breaks false so we never hang — the real
// recursion diagnostic belongs to a later phase.
[[nodiscard]] bool tree_declares_reg(const std::shared_ptr<Lnast>& lnast) {
  // A reg whose `clock_pin=NAME` attr names its clock explicitly does not
  // need the implicit `clock` input (the slang reader stamps these for
  // non-clk/clock Verilog clock names); collect the covered names first.
  absl::flat_hash_set<std::string>      clocked_elsewhere;
  std::function<void(const Lnast_nid&)> scan_attrs = [&](const Lnast_nid& nid) {
    if (Lnast_ntype::is_attr_set(lnast->get_type(nid))) {
      auto tgt = lnast->get_first_child(nid);
      auto key = tgt.is_invalid() ? tgt : lnast->get_sibling_next(tgt);
      if (!key.is_invalid() && lnast->get_name(key) == "clock_pin") {
        clocked_elsewhere.emplace(lnast->get_name(tgt));
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      scan_attrs(c);
    }
  };
  scan_attrs(lnast->get_root());

  std::function<bool(const Lnast_nid&)> has_reg = [&](const Lnast_nid& nid) -> bool {
    // 1a-mem — a __memory(cfg) instantiation needs the clock too (a type=2
    // array config leaves the minted input unused; acceptable, documented).
    if (Lnast_ntype::is_func_call(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      auto c1 = c0.is_invalid() ? c0 : lnast->get_sibling_next(c0);
      if (!c1.is_invalid() && lnast->get_name(c1) == "__memory") {
        return true;
      }
    }
    if (Lnast_ntype::is_declare(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        if (!c1.is_invalid()) {
          auto c2 = lnast->get_sibling_next(c1);
          if (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2))) {
            auto mode = lnast->get_name(c2);
            if ((mode == "reg" || mode.starts_with("reg ")) && !clocked_elsewhere.contains(lnast->get_name(c0))) {
              return true;
            }
          }
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (has_reg(c)) {
        return true;
      }
    }
    return false;
  };
  return has_reg(lnast->get_root());
}

void collect_callee_names(const std::shared_ptr<Lnast>& lnast, std::vector<std::string>& out) {
  std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
    if (Lnast_ntype::is_func_call(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        if (!c1.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c1))) {
          out.emplace_back(lnast->get_name(c1));
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      walk(c);
    }
  };
  walk(lnast->get_root());
}

[[nodiscard]] bool needs_clock_rec(const std::shared_ptr<Lnast>& lnast, const uPass_tolg::Registry& registry,
                                   absl::flat_hash_map<std::string, bool>& memo, absl::flat_hash_set<std::string>& visiting) {
  const std::string key(lnast->get_top_module_name());
  if (auto it = memo.find(key); it != memo.end()) {
    return it->second;
  }
  if (!visiting.insert(key).second) {
    return false;  // cycle guard
  }
  bool needs = tree_declares_reg(lnast);
  if (!needs) {
    std::vector<std::string> callees;
    collect_callee_names(lnast, callees);
    for (const auto& cn : callees) {
      auto callee = resolve_callee_lnast(cn, registry);
      if (!callee) {
        continue;
      }
      auto kind = callee->get_lambda_kind();
      if (kind != "pipe" && kind != "mod") {
        continue;
      }
      if (needs_clock_rec(callee, registry, memo, visiting)) {
        needs = true;
        break;
      }
    }
  }
  visiting.erase(key);
  memo[key] = needs;
  return needs;
}

// True when any plain-reg declare carries a non-nil initializer (the
// declare's trailing const [value] child) without an explicit per-reg
// `reset_pin` attr override (those bind their own reset input). A ref init is
// counted (tolg later requires it const; the reset NEED is already real).
[[nodiscard]] bool tree_declares_reset_reg(const std::shared_ptr<Lnast>& lnast) {
  absl::flat_hash_set<std::string>      explicit_rp;
  std::function<void(const Lnast_nid&)> collect_rp = [&](const Lnast_nid& nid) {
    if (Lnast_ntype::is_attr_set(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        if (!c1.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c1)) && lnast->get_name(c1) == "reset_pin") {
          explicit_rp.emplace(lnast->get_name(c0));
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      collect_rp(c);
    }
  };
  collect_rp(lnast->get_root());

  std::function<bool(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) -> bool {
    if (Lnast_ntype::is_declare(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        auto c2 = c1.is_invalid() ? c1 : lnast->get_sibling_next(c1);
        if (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2))) {
          auto       mode     = lnast->get_name(c2);
          // 1a-mem — array regs are memories: no reset hardware in this
          // slice (only nil/0sb? init is accepted), so they never need the
          // implicit reset input.
          const bool is_array = !c1.is_invalid() && Lnast_ntype::is_comp_type_array(lnast->get_type(c1));
          if (!is_array && (mode == "reg" || mode.starts_with("reg "))) {
            for (auto c = lnast->get_sibling_next(c2); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
              const auto ct = lnast->get_type(c);
              if (Lnast_ntype::is_stages(ct)) {
                break;  // stage reg — no init slot
              }
              if (Lnast_ntype::is_const(ct) || Lnast_ntype::is_ref(ct)) {
                const bool nil_init = Lnast_ntype::is_const(ct) && lnast->get_name(c) == "nil";
                if (!nil_init && !explicit_rp.contains(std::string(lnast->get_name(c0)))) {
                  return true;
                }
                break;
              }
            }
          }
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (walk(c)) {
        return true;
      }
    }
    return false;
  };
  return walk(lnast->get_root());
}

// 1a-mem reset-restore — true when any ARRAY-typed reg declare carries a
// concrete (non-nil) initializer. Such an array never mints an implicit
// reset, but if the module already has a reset-candidate input it binds it:
// the memory lowering adds per-entry restore write ports (reset re-loads the
// init contents in one cycle).
[[nodiscard]] bool tree_declares_init_reg_array(const std::shared_ptr<Lnast>& lnast) {
  std::function<bool(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) -> bool {
    if (Lnast_ntype::is_declare(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        auto c2 = c1.is_invalid() ? c1 : lnast->get_sibling_next(c1);
        if (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2))) {
          auto       mode     = lnast->get_name(c2);
          const bool is_array = !c1.is_invalid() && Lnast_ntype::is_comp_type_array(lnast->get_type(c1));
          if (is_array && (mode == "reg" || mode.starts_with("reg "))) {
            for (auto c = lnast->get_sibling_next(c2); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
              const auto ct = lnast->get_type(c);
              if (Lnast_ntype::is_const(ct)) {
                auto txt = lnast->get_name(c);
                if (txt != "nil" && txt != "0sb?") {
                  return true;
                }
                break;
              }
              if (Lnast_ntype::is_ref(ct)) {
                return true;  // tuple-literal init
              }
            }
          }
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (walk(c)) {
        return true;
      }
    }
    return false;
  };
  return walk(lnast->get_root());
}

[[nodiscard]] bool needs_reset_rec(const std::shared_ptr<Lnast>& lnast, const uPass_tolg::Registry& registry,
                                   absl::flat_hash_map<std::string, bool>& memo, absl::flat_hash_set<std::string>& visiting) {
  const std::string key(lnast->get_top_module_name());
  if (auto it = memo.find(key); it != memo.end()) {
    return it->second;
  }
  if (!visiting.insert(key).second) {
    return false;  // cycle guard
  }
  bool needs = tree_declares_reset_reg(lnast);
  if (!needs) {
    std::vector<std::string> callees;
    collect_callee_names(lnast, callees);
    for (const auto& cn : callees) {
      auto callee = resolve_callee_lnast(cn, registry);
      if (!callee) {
        continue;
      }
      auto kind = callee->get_lambda_kind();
      if (kind != "pipe" && kind != "mod") {
        continue;
      }
      if (needs_reset_rec(callee, registry, memo, visiting)) {
        needs = true;
        break;
      }
    }
  }
  visiting.erase(key);
  memo[key] = needs;
  return needs;
}

// Shared phase-1 io+clock+reset GraphIO registration. Idempotent (the GraphIO
// add calls are has_-guarded). Returns the clock/reset binding for the body
// build; empty names = the module needs none.
[[nodiscard]] Io_setup setup_io_impl(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path,
                                     const uPass_tolg::Registry& registry) {
  auto& lib      = livehd::Hhds_graph_library::instance(lib_path);
  // 2f-lg: the GraphIO/library key (hence the emitted module name and the
  // `import("lg:<name>")` key) is the effective graph name — the `lg="…"`
  // override when present, else the mangled top_module_name. The
  // `import("file.entity")` key (function_registry, resolve_callee_lnast,
  // needs_*_rec memo) stays on top_module_name and is untouched here.
  auto  mod_name = std::string(lnast->get_graph_name());

  auto gio = lib.find_io(mod_name);
  if (!gio) {
    gio = lib.create_io(mod_name);
  }

  // Declare I/O on the GraphIO: positional pin ids + meaningful bits + sign.
  // Port widths cgen emits come from these decls (meaningful width, not the +1
  // internal convention).
  hhds::Port_id pid     = 1;
  auto          declare = [&](const Lnast_io_entry& e, bool is_input) {
    uint32_t bits = e.kind == Io_kind::boolean ? 1u : (e.bits > 0 ? static_cast<uint32_t>(e.bits) : 1u);
    if (is_input) {
      if (!gio->has_input(e.name) && !gio->has_output(e.name)) {
        gio->add_input(e.name, pid);
      }
    } else {
      if (!gio->has_output(e.name) && !gio->has_input(e.name)) {
        gio->add_output(e.name, pid);
      }
    }
    gio->set_bits(e.name, bits);
    gio->set_unsign(e.name, !e.is_signed);
    ++pid;
  };
  for (const auto& e : lnast->io_meta().inputs) {
    declare(e, /*is_input=*/true);
  }
  for (const auto& e : lnast->io_meta().outputs) {
    declare(e, /*is_input=*/false);
  }

  // Implicit clock: when the tree holds state (own regs OR,
  // transitively, any pipe/mod callee instance — its minted clock must be
  // forwarded) and the partition has no clk/clock input, mint a 1-bit
  // unsigned "clock" graph input for the flops'/instances' clock_pin.
  std::string clock_name;
  bool        clock_minted = false;
  {
    absl::flat_hash_map<std::string, bool> memo;
    absl::flat_hash_set<std::string>       visiting;
    if (needs_clock_rec(lnast, registry, memo, visiting)) {
      // Reuse a declared clk/clock input only when it can actually be a
      // clock (bool or <=1-bit; untyped bits==0 included) — a multi-bit
      // DATA port that happens to be named clk/clock must not be hijacked
      // as the flop clock.
      for (const auto& e : lnast->io_meta().inputs) {
        if ((e.name == "clk" || e.name == "clock") && (e.kind == Io_kind::boolean || e.bits <= 1)) {
          clock_name = e.name;
          break;
        }
      }
      if (clock_name.empty()) {
        // Minting the implicit "clock" input collides with any existing
        // multi-bit clock-named port — diagnose instead of double-driving.
        for (const auto& e : lnast->io_meta().inputs) {
          if (e.name == "clock") {
            livehd::diag::err("upass.tolg", "clock-collision", "time")
                .msg(
                    "input 'clock' of '{}' is not usable as the pipeline clock (multi-bit data port) "
                    "and collides with the implicit clock — rename it or declare it 1-bit",
                    mod_name)
                .fatal();
          }
        }
        for (const auto& e : lnast->io_meta().outputs) {
          if (e.name == "clock") {
            livehd::diag::err("upass.tolg", "clock-collision", "time")
                .msg("output 'clock' of '{}' collides with the implicit pipeline clock — rename it", mod_name)
                .fatal();
          }
        }
        clock_name = "clock";
        if (!gio->has_input(clock_name) && !gio->has_output(clock_name)) {
          gio->add_input(clock_name, pid);
          ++pid;
        }
        gio->set_bits(clock_name, 1);
        gio->set_unsign(clock_name, true);
        clock_minted = true;
      }
    }
  }

  // Implicit reset: when the tree (or, transitively, any pipe/mod
  // callee instance) holds a reg with a non-nil initializer, bind an existing
  // reset/rst/reset_n/rst_n input (exactly one candidate; the `_n` variants
  // are active-low) or mint a 1-bit unsigned "reset" graph input — the same
  // bind-before-mint pattern as the implicit clock.
  std::string reset_name;
  bool        reset_minted = false;
  bool        reset_neg    = false;
  {
    auto bind_reset_candidate = [&]() {
      int candidates = 0;
      for (const auto& e : lnast->io_meta().inputs) {
        const bool is_cand = (e.name == "reset" || e.name == "rst" || e.name == "reset_n" || e.name == "rst_n")
                             && (e.kind == Io_kind::boolean || e.bits <= 1);
        if (!is_cand) {
          continue;
        }
        ++candidates;
        if (reset_name.empty()) {
          reset_name = e.name;
          reset_neg  = e.name.size() > 2 && e.name.substr(e.name.size() - 2) == "_n";
        }
      }
      if (candidates > 1) {
        livehd::diag::err("upass.tolg", "reset-ambiguous", "time")
            .msg("'{}' has multiple reset-candidate inputs — give each reg an explicit `:[reset_pin=…]`", mod_name)
            .hint("name exactly one of reset/rst/reset_n/rst_n, or bind per-reg with `:[reset_pin=…]`")
            .fatal();
      }
    };
    absl::flat_hash_map<std::string, bool> memo;
    absl::flat_hash_set<std::string>       visiting;
    if (needs_reset_rec(lnast, registry, memo, visiting)) {
      bind_reset_candidate();
      if (reset_name.empty()) {
        for (const auto& e : lnast->io_meta().inputs) {
          if (e.name == "reset") {
            livehd::diag::err("upass.tolg", "reset-collision", "time")
                .msg(
                    "input 'reset' of '{}' is not usable as the register reset (multi-bit data port) "
                    "and collides with the implicit reset — rename it or declare it 1-bit",
                    mod_name)
                .fatal();
          }
        }
        for (const auto& e : lnast->io_meta().outputs) {
          if (e.name == "reset") {
            livehd::diag::err("upass.tolg", "reset-collision", "time")
                .msg("output 'reset' of '{}' collides with the implicit register reset — rename it", mod_name)
                .fatal();
          }
        }
        reset_name = "reset";
        if (!gio->has_input(reset_name) && !gio->has_output(reset_name)) {
          gio->add_input(reset_name, pid);
          ++pid;
        }
        gio->set_bits(reset_name, 1);
        gio->set_unsign(reset_name, true);
        reset_minted = true;
      }
    } else if (tree_declares_init_reg_array(lnast)) {
      // 1a-mem reset-restore — a reg ARRAY with a concrete initializer never
      // MINTS a reset (memories stay power-on-only by default), but when the
      // module already carries a reset-candidate input, bind it: tolg then
      // adds per-entry restore write ports so a reset re-loads the init.
      bind_reset_candidate();
    }
  }

  return {clock_name, clock_minted, reset_name, reset_minted, reset_neg};
}

}  // namespace

void uPass_tolg::detect_lg_collisions(const Registry& registry) {
  // 2f-lg: a `pub comb f::[lg="name"]` pins the artifact (GraphIO/module)
  // name. Two units resolving to the SAME effective graph name would silently
  // share one GraphIO and emit a broken, double-driven module. Diagnose before
  // any GraphIO is created. The import key (top_module_name) is unique by
  // mangling and unaffected. Linear scan: a compile carries few units.
  std::vector<std::pair<std::string, std::string>> seen;  // (graph name, owning unit)
  for (const auto& ln : registry) {
    if (!ln || ln->is_template() || ln->io_meta().empty()) {
      continue;  // same filter as register_io/run: only units that mint a GraphIO
    }
    std::string gname(ln->get_graph_name());
    std::string unit(ln->get_top_module_name());
    for (const auto& [g, u] : seen) {
      if (g == gname && u != unit) {
        livehd::diag::err("upass.tolg", "lg-name-collision", "type")
            .msg("two units map to the same lgraph/module name '{}' (units '{}' and '{}')", gname, u, unit)
            .hint("give each `pub` definition a distinct `lg=\"…\"` name (or drop `lg` to use the default `file.entity`)")
            .fatal();
      }
    }
    seen.emplace_back(std::move(gname), std::move(unit));
  }
}

void uPass_tolg::register_io(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path, const Registry& registry) {
  if (!lnast || lnast->io_meta().empty()) {
    return;  // not a lowerable module (e.g. the empty file-root tree)
  }
  // A deferred template (untyped/var-args/generic signature) emits no
  // LGraph: it is realized per call site (comb inlines, pipe/mod/fluid
  // specialize into a concrete clone). Never reserve a GraphIO for it, or a
  // call site could mis-bind to a port-less interface.
  if (lnast->is_template()) {
    return;
  }
  (void)setup_io_impl(lnast, lib_path, registry);
}

std::shared_ptr<hhds::Graph> uPass_tolg::run(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path,
                                             const Registry& registry, std::string_view reset_style) {
  if (!lnast || lnast->io_meta().empty()) {
    return nullptr;  // not a lowerable module (e.g. the empty file-root tree)
  }
  // A deferred template produces no LGraph (see register_io). A
  // template selected as a synthesis top simply yields no module; it is never
  // a hard error at definition time (contract decision 3).
  if (lnast->is_template()) {
    return nullptr;
  }

  auto io_setup = setup_io_impl(lnast, lib_path, registry);

  auto& lib      = livehd::Hhds_graph_library::instance(lib_path);
  auto  gio      = lib.find_io(std::string(lnast->get_graph_name()));  // 2f-lg: lg override or mangled name
  auto  g_shared = gio->has_graph() ? gio->get_graph() : gio->create_graph();

  Tolg builder(lnast, g_shared.get(), std::move(io_setup), &registry, &lib, reset_style == "async");
  builder.build();

  // The combined pipe/mod time checker at the tolg seam.
  {
    const auto kind = lnast->get_lambda_kind();
    if (kind == "pipe" || kind == "mod") {
      Time_checker checker(g_shared.get(),
                           lnast,
                           builder.take_pending_checks(),
                           builder.take_flop_depths(),
                           builder.take_sub_times(),
                           builder.take_plain_reg_flops(),
                           builder.take_inserted_flops(),
                           builder.take_defer_cuts());
      checker.run();
    }
  }

  g_shared->commit();
  return g_shared;
}
