//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_tolg.hpp"

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
#include "const.hpp"
#include "graph_library_singleton.hpp"
#include "lnast_ntype.hpp"
#include "node_util.hpp"
#include "pass.hpp"

namespace {

using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
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
  Pin    pin;
  Bits_t mw{0};
};

// Bits to represent a non-negative value as unsigned (>=1).
[[nodiscard]] Bits_t mw_of_val(int64_t v) {
  if (v <= 0) {
    return 1;
  }
  return static_cast<Bits_t>(std::bit_width(static_cast<uint64_t>(v)));
}

// Task 1u-A — resolve a func_call callee name against the lnast registry the
// same way the runner's lookup_callee does: exact top-module-name match, else
// a UNIQUE "<module>.<name>" suffix match.
[[nodiscard]] std::shared_ptr<Lnast> resolve_callee_lnast(std::string_view name,
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

// Task 1u-C — one pending time obligation: an asserted (min,max) landing
// interval on a value's driver pin (`is_sink=false`, from an undischarged
// `@[N]`) or on a GraphIO output sink (`is_sink=true`, the mod declared
// cycle / the pipe declared range). The 1u-D checker verifies and REMOVES
// the paired pending_time attr; leftovers are compile errors.
struct Pending_rec {
  hhds::Pin_class pin;
  std::string     name;
  int64_t         min     = 0;
  int64_t         max     = 0;
  bool            is_sink = false;
};

// Builds one hhds::Graph from one post-upass / post-SSA function-tree Lnast.
class Tolg {
public:
  // `clock_name` is the graph input driving every flop's clock_pin (the
  // declared clk/clock input, or the implicit "clock" run() minted —
  // `clock_minted` distinguishes them so only the minted pin gets its
  // width/sign stamped here; a reused declared input was already stamped by
  // the io loop). Empty when the tree has no regs (own or in any callee).
  // `registry`/`lib` resolve pipe/mod call sites to Sub instances (1u-A).
  Tolg(const std::shared_ptr<Lnast>& lnast, hhds::Graph* g, std::string clock_name, bool clock_minted,
       const uPass_tolg::Registry* registry, hhds::GraphLibrary* lib)
      : lnast_(lnast), g_(g), registry_(registry), lib_(lib), clock_name_(std::move(clock_name)), clock_minted_(clock_minted) {}

private:
  // Task 1u-A — deferred stage-reg creation: a declare(reg)+stages does NOT
  // create the Flop immediately; the din store does, because only there the
  // effective depth is known (a Sub-fed stage reg realizes the DEFICIT
  // stage_N − callee_min; depth 0 = plain wire, no Flop at all). min/max ride
  // as raw const texts so "nil" stays distinguishable from 0.
  struct Pending_stage {
    std::string min_txt;
    std::string max_txt;
    Lnast_nid   decl_nid;  // for located diagnostics
  };

  // Task 1u-A — per call-result name: the callee output's declared stages
  // interval + kind, recorded when the Sub is created and consumed by the
  // following stage-reg din store for the range check + deficit narrowing.
  struct Sub_out {
    int64_t           cmin    = 0;
    int64_t           cmax    = 0;  // pipe convention: 0 with cmin>=1 = unconstrained
    bool              is_pipe = false;
    hhds::Node_class  node;  // to re-stamp time_range when stage[N] pins the pick
  };

public:

  void build() {
    // Inputs: from io_meta(). Unsigned inputs are wrapped in a to-positive
    // Get_mask so signed-declared ports read with their unsigned value (e.g.
    // a 3-bit `a` = 0b111 reads as 7, not -1) — mirrors lgyosys tposs.
    for (const auto& e : lnast_->io_meta().inputs) {
      auto   raw = g_->get_input_pin(e.name);  // body driver pin for the port
      Bits_t mw  = io_mw(e);
      if (e.kind == Io_kind::boolean || mw <= 1) {
        set_bits(raw, 1);
        if (e.is_signed) {
          set_sign(raw);
        } else {
          set_unsign(raw);
        }
        record(e.name, raw, 1);
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
    }

    // Body: lower the `stmts` child of `top`.
    auto top = lnast_->get_root();
    for (auto c = lnast_->get_first_child(top); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (Lnast_ntype::is_stmts(lnast_->get_type(c))) {
        lower_stmts(c);
      }
    }

    // Outputs: connect each output's bound driver to its graph output sink.
    for (const auto& name : out_names_) {
      auto sink = g_->get_output_pin(name);
      if (sink.is_invalid()) {
        continue;
      }
      auto it = pin_map_.find(name);
      if (it == pin_map_.end()) {
        Pass::warn("upass.tolg: output '{}' not driven by body — wiring nil (0sb?)", name);
        sink.connect_driver(create_const(*g_, *Const::from_pyrope("0sb?")));
        continue;
      }
      sink.connect_driver(it->second);
    }

    // Task 1u-C — lower the declared per-output intervals as pendings.
    stamp_output_pendings();

    // 2d-reg guard — a stage declare whose din store never arrived would
    // silently drop the delay (and the value): hard error, never nil.
    if (!pending_stage_.empty()) {
      Pass::error("upass.tolg: stage reg '{}' in '{}' was declared but never stored — its delay would be silently lost",
                  pending_stage_.begin()->first,
                  lnast_->get_top_module_name());
    }
  }

private:
  // ── width / value helpers ───────────────────────────────────────────────────

  [[nodiscard]] static Bits_t io_mw(const Lnast_io_entry& e) {
    if (e.kind == Io_kind::boolean) {
      return 1;
    }
    return e.bits > 0 ? static_cast<Bits_t>(e.bits) : Bits_t{1};
  }

  [[nodiscard]] Pin nil_pin() { return create_const(*g_, *Const::from_pyrope("0sb?")); }

  // To-positive-signed: Get_mask(x, -1) -> same bits, guaranteed non-negative,
  // marked unsigned with one extra (sign) bit. Lets unsigned values flow
  // through signed LGraph arithmetic correctly.
  [[nodiscard]] Pin to_positive(const Pin& src, Bits_t mw) {
    auto node = create_typed_node(*g_, Ntype_op::Get_mask);
    setup_sink_by_name(node, "a").connect_driver(src);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *Const::create_integer(-1)));
    auto drv = node.create_driver_pin(0);
    set_bits(drv, mw + 1);
    set_unsign(drv);
    return drv;
  }

  void record(std::string_view name, const Pin& pin, Bits_t mw) {
    std::string key{name};
    pin_map_[key] = pin;
    mw_map_[key]  = mw;
    if (!branch_writes_.empty()) {
      branch_writes_.back()[key] = pin;
    }
  }

  [[nodiscard]] Bits_t mw_lookup(std::string_view name) {
    auto it = mw_map_.find(std::string{name});
    return it != mw_map_.end() ? it->second : Bits_t{1};
  }

  [[nodiscard]] Pin resolve(std::string_view name) {
    std::string key{name};
    auto        it = pin_map_.find(key);
    if (it != pin_map_.end()) {
      return it->second;
    }
    Pass::warn("upass.tolg: unresolved ref '{}' — wiring nil (0sb?)", name);
    auto p        = nil_pin();
    pin_map_[key] = p;
    mw_map_[key]  = 1;
    return p;
  }

  [[nodiscard]] Val leaf(const Lnast_nid& nid) {
    if (Lnast_ntype::is_const(lnast_->get_type(nid))) {
      auto   c  = Const::from_pyrope(lnast_->get_name(nid));
      Bits_t mw = c->is_i() ? mw_of_val(c->to_i()) : Bits_t{1};
      return {create_const(*g_, *c), mw};
    }
    auto name = lnast_->get_name(nid);
    return {resolve(name), mw_lookup(name)};
  }

  // Bind a computed result: stamp mw+1 unsigned bits and record name->(pin,mw).
  void bind_result(std::string_view name, const Pin& drv, Bits_t mw) {
    Bits_t m = mw > 0 ? mw : Bits_t{1};
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

  void lower_node(const Lnast_nid& nid) {
    using N      = Lnast_ntype;
    const auto t = lnast_->get_type(nid);
    if (N::is_stmts(t)) {
      lower_stmts(nid);
    } else if (N::is_if(t)) {
      lower_if(nid);
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
      lower_op(nid, Ntype_op::SHL, false, OpW::maxw);
    } else if (N::is_sra(t)) {
      lower_op(nid, Ntype_op::SRA, false, OpW::firstw);
    } else if (N::is_bit_not(t) || N::is_log_not(t)) {
      lower_unary(nid, Ntype_op::Not);
    } else if (N::is_ne(t)) {
      lower_negated(nid, Ntype_op::EQ, true);
    } else if (N::is_le(t)) {
      lower_negated(nid, Ntype_op::GT, false);
    } else if (N::is_ge(t)) {
      lower_negated(nid, Ntype_op::LT, false);
    } else if (N::is_timecheck(t)) {
      // Task 1u-C — an `x@[N]` record the LNAST discharge could not decide
      // ("checked"-marked ones are already done): lower it to a PENDING
      // time-check attr on the named value's driver pin. The 1u-D checker
      // verifies and removes it; a leftover pending is a compile error.
      lower_timecheck(nid);
    } else if (N::is_func_call(t)) {
      // Task 1u-A — pipe/mod call sites lower to Ntype_op::Sub instances;
      // anything unresolvable (runtime wrap/sat, comb recursion) stays a
      // HARD error inside lower_func_call.
      lower_func_call(nid);
    } else {
      Pass::warn("upass.tolg: unhandled node type '{}'", Lnast_ntype::to_sv(t));
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
    if (!lnast_->get_sibling_next(rhs).is_invalid()) {
      Pass::warn("upass.tolg: tuple/field store not handled yet (lhs '{}')", lnast_->get_name(lhs));
      return;
    }
    auto lhs_name = lnast_->get_name(lhs);
    // Task 1u-A — deferred stage-reg creation: the din store knows the
    // effective depth (deficit narrowing against a Sub callee; 0 = wire).
    if (auto pit = pending_stage_.find(lhs_name); pit != pending_stage_.end()) {
      auto pending = pit->second;
      pending_stage_.erase(pit);
      create_stage_flop(lhs_name, pending, rhs);
      return;
    }
    if (auto rit = reg_map_.find(lhs_name); rit != reg_map_.end()) {
      if (!reg_din_connected_.insert(std::string(lhs_name)).second) {
        // Conditional / multiple reg writes are the 2d milestone (they need
        // the enable/mux lowering); the pipe upass emits exactly one.
        Pass::warn("upass.tolg: multiple writes to reg '{}' not handled yet — keeping the first", lhs_name);
        return;
      }
      auto v = leaf(rhs);
      setup_sink_by_name(rit->second, "din").connect_driver(v.pin);
      // The q pin mirrors din's width/sign convention (mw+1 unsigned bits).
      auto q = rit->second.create_driver_pin(0);
      set_bits(q, v.mw + 1);
      set_unsign(q);
      mw_map_[std::string(lhs_name)] = v.mw;
      return;
    }
    auto v = leaf(rhs);
    record(lhs_name, v.pin, v.mw);
  }

  // Task 1q — declare(ref(name), type, const("reg")) [+ stages(min,max)]:
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
    auto type_nid = lnast_->get_sibling_next(name_nid);
    auto mode_nid = type_nid.is_invalid() ? type_nid : lnast_->get_sibling_next(type_nid);
    auto mode     = mode_nid.is_invalid() || !Lnast_ntype::is_const(lnast_->get_type(mode_nid))
                        ? std::string_view{}
                        : std::string_view(lnast_->get_name(mode_nid));
    if (mode != "reg" && !mode.starts_with("reg ")) {
      // mut/const/type declares carry no graph payload here (values arrive
      // via their stores); nothing to lower.
      return;
    }

    // stages(min,max) trailing child — Task 1u-A: DEFER the Flop creation to
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
      auto mx = lnast_->get_sibling_next(mn);
      Pending_stage p;
      p.min_txt  = std::string(lnast_->get_name(mn));
      p.max_txt  = mx.is_invalid() ? p.min_txt : std::string(lnast_->get_name(mx));
      p.decl_nid = nid;
      pending_stage_[std::string(lnast_->get_name(name_nid))] = std::move(p);
      return;
    }

    // Plain reg (no stages) — the 2d milestone shape: create the Flop now.
    auto flop = create_typed_node(*g_, Ntype_op::Flop);
    if (!clock_name_.empty()) {
      setup_sink_by_name(flop, "clock_pin").connect_driver(clock_pin());
    } else {
      Pass::warn("upass.tolg: reg '{}' has no clock input to bind", lnast_->get_name(name_nid));
    }
    auto name = lnast_->get_name(name_nid);
    auto q    = flop.create_driver_pin(0);
    reg_map_.emplace(std::string(name), flop);
    record(name, q, 1);  // provisional width; the din store restamps it
  }

  // Task 1u-A — materialize a deferred stage reg at its din store. Effective
  // depth: plain RHS keeps the declared (min,max); a Sub call result narrows
  // to the DEFICIT against the callee (Phase-1 realization = callee at its
  // declared min, so deficit = stage_N − callee_min; for a mod callee the
  // output cycle is fixed and stage_N must match it exactly → deficit 0).
  // Depth (0,0) is a plain wire — no Flop is created at all.
  void create_stage_flop(std::string_view name, const Pending_stage& p, const Lnast_nid& rhs) {
    const bool min_nil = p.min_txt == "nil";
    const bool max_nil = p.max_txt == "nil";
    int64_t    smin    = 0;
    int64_t    smax    = 0;
    if (!min_nil) {
      auto c = Const::from_pyrope(p.min_txt);
      smin   = (c && c->is_i()) ? c->to_i() : 0;
    }
    if (!max_nil) {
      auto c = Const::from_pyrope(p.max_txt);
      smax   = (c && c->is_i()) ? c->to_i() : 0;
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
        Pass::error(
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
            Pass::error("upass.tolg: stage[{}] on '{}' is outside the callee's declared latency range [{}, {}]",
                        n,
                        name,
                        so.cmin,
                        so.cmax);
          } else {
            Pass::error("upass.tolg: stage[{}] on '{}' is below the callee's declared minimum latency {}",
                        n,
                        name,
                        so.cmin);
          }
          return;
        }
        emin = emax = n - so.cmin;  // callee realized at its declared min
      } else {
        // mod callee: the output's landing cycle is fixed by its interface.
        if (so.cmin != so.cmax || n != so.cmin) {
          Pass::error("upass.tolg: mod call result '{}' lands at its declared cycle {} — `stage[{}]` must match it "
                      "(add a separate `stage[N] x = value` for extra delay)",
                      name,
                      so.cmin,
                      n);
          return;
        }
        emin = emax = 0;
      }
      // Task 1u-C — the stage pick pins the REALIZED split: the callee
      // instance contributes its declared min (Phase-1 realization), the
      // caller-side deficit flop the remaining n − cmin. Stamping the full
      // pick on the instance would double-count the deficit.
      {
        const int64_t realized = so.is_pipe ? so.cmin : n;
        so.node.attr(livehd::attrs::time_range).set({realized, realized});
        sub_time_[so.node.get_debug_nid()] = {realized, realized};
      }
    } else if (min_nil || max_nil) {
      Pass::error(
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

    auto flop = create_typed_node(*g_, Ntype_op::Flop);
    flop_depth_[flop.get_debug_nid()] = {emin, emax};
    setup_sink_by_name(flop, "pipe_min").connect_driver(create_const(*g_, *Const::create_integer(emin)));
    setup_sink_by_name(flop, "pipe_max").connect_driver(create_const(*g_, *Const::create_integer(emax)));
    if (!clock_name_.empty()) {
      setup_sink_by_name(flop, "clock_pin").connect_driver(clock_pin());
    } else {
      Pass::warn("upass.tolg: reg '{}' has no clock input to bind", name);
    }
    setup_sink_by_name(flop, "din").connect_driver(v.pin);
    auto q = flop.create_driver_pin(0);
    set_bits(q, v.mw + 1);
    set_unsign(q);
    reg_map_.emplace(std::string(name), flop);
    reg_din_connected_.insert(std::string(name));
    record(name, q, v.mw);
  }

  // Task 1u-A — func_call(dst_tmp, callee_name, args...) → an Ntype_op::Sub
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

    // Task 1m — a resolved `import` call is comptime scaffolding: constprop
    // bound its namespace bundle / lambda ref and every consumer folded (an
    // UNRESOLVED live import never reaches tolg — pass.upass errors or the
    // kernel defers). Nothing lowers to hardware here.
    if (Lnast_ntype::is_const(lnast_->get_type(callee_n)) && callee_name == "import") {
      return;
    }

    std::shared_ptr<Lnast> callee;
    if (registry_ != nullptr) {
      callee = resolve_callee_lnast(callee_name, *registry_);
    }
    const auto kind = callee ? callee->get_lambda_kind() : std::string_view{};
    if (!callee || (kind != "pipe" && kind != "mod")) {
      // Unresolvable (runtime wrap/sat builtin, recursion-kept comb, …):
      // HARD error — the pre-1r behavior silently wired the call's outputs
      // to nil and emitted a broken netlist.
      Pass::error(
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
      Pass::error("upass.tolg: '{}' (a {}) calls the {} '{}' — only `mod` bodies may instantiate pipe/mod",
                  lnast_->get_top_module_name(),
                  lnast_->get_lambda_kind().empty() ? std::string_view{"comb"} : lnast_->get_lambda_kind(),
                  kind,
                  callee_name);
      return;
    }

    const auto  callee_full = std::string(callee->get_top_module_name());
    auto        gio         = lib_ != nullptr ? lib_->find_io(callee_full) : nullptr;
    if (!gio) {
      Pass::error("upass.tolg: callee '{}' has no registered GraphIO — register_io() phase missing", callee_full);
      return;
    }
    const auto& cio = callee->io_meta();
    if (cio.outputs.size() != 1) {
      Pass::error("upass.tolg: call to '{}' has {} outputs — multi-output call sites land in a later phase",
                  callee_full,
                  cio.outputs.size());
      return;
    }

    // NOTE: set_subnode RE-STAMPS the raw hhds type to its own 2/3 loop-hint
    // encoding — type_op_of() recognizes Subs by the subnode LINK, never by
    // the stored type (see node_util.hpp).
    auto sub = create_typed_node(*g_, Ntype_op::Sub);
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
        // Task 1m — namespace receiver marker: `lib.scale(args)` through an
        // import tuple carries the receiver in a `__ufcs_arg` store, but the
        // receiver names the NAMESPACE — it is not an argument of a no-self
        // callee. (A true `ref self` mod method splices in the runner and
        // never reaches the Sub path.)
        if (pname == "__ufcs_arg" && (cio.inputs.empty() || cio.inputs[0].name != "self")) {
          continue;
        }
      } else {
        if (pos >= cio.inputs.size()) {
          Pass::error("upass.tolg: call to '{}' passes more arguments than its {} declared inputs",
                      callee_full,
                      cio.inputs.size());
          return;
        }
        pname = cio.inputs[pos].name;
        ++pos;
        val = a;
      }
      auto v    = leaf(val);
      auto spin = sub.create_sink_pin(pname);
      if (spin.is_invalid()) {
        Pass::error("upass.tolg: callee '{}' has no input named '{}'", callee_full, pname);
        return;
      }
      spin.connect_driver(v.pin);
      ++provided;
    }
    if (provided != cio.inputs.size()) {
      Pass::error("upass.tolg: call to '{}' provides {} of its {} declared inputs",
                  callee_full,
                  provided,
                  cio.inputs.size());
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
        Pass::error("upass.tolg: instance of clocked '{}' but '{}' has no clock to forward (needs_clock bug)",
                    callee_full,
                    lnast_->get_top_module_name());
        return;
      }
      sub.create_sink_pin("clock").connect_driver(clock_pin());
    }

    // Single output: bind dst like a graph input (external value entering).
    const auto& oe       = cio.outputs.front();
    auto        out_dpin = sub.create_driver_pin(oe.name);
    Bits_t      mw       = io_mw(oe);
    std::string dst_name(lnast_->get_name(dst));
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
    // Task 1u-C — the instance is a timed crossing: stamp its declared
    // latency interval (a following stage[N] re-stamps the pinned pick).
    // Bare-pipe unconstrained max (cmax<cmin) propagates as min (the
    // Phase-1 realization) — the io stages remain the caller-facing truth.
    {
      const int64_t cmin = oe.stages_min;
      const int64_t cmax = oe.stages_max < oe.stages_min ? oe.stages_min : oe.stages_max;
      sub.attr(livehd::attrs::time_range).set({cmin, cmax});
      sub_time_[sub.get_debug_nid()] = {cmin, cmax};
    }
    sub_out_stages_[dst_name] = {oe.stages_min, oe.stages_max, kind == "pipe", sub};
  }

  // Task 1u-C — lower an undischarged timecheck statement to a pending
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
      Pass::error("upass.tolg: `@[N]` check on '{}' — the value never materialized in the graph", name);
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
    auto c = Const::from_pyrope(lnast_->get_name(nid));
    return c->is_i() ? c->to_i() : 0;
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
    int64_t mask = mask_from_operand(mask_op);

    auto node = create_typed_node(*g_, Ntype_op::Get_mask);
    setup_sink_by_name(node, "a").connect_driver(leaf(val).pin);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *Const::create_integer(mask)));
    auto   drv = node.create_driver_pin(0);
    Bits_t mw  = static_cast<Bits_t>(std::popcount(static_cast<uint64_t>(mask)));
    bind_result(lnast_->get_name(dst), drv, mw);
  }

  [[nodiscard]] int64_t mask_from_operand(const Lnast_nid& mask_op) {
    if (Lnast_ntype::is_const(lnast_->get_type(mask_op))) {
      return const_val(mask_op);
    }
    auto it = range_map_.find(std::string{lnast_->get_name(mask_op)});
    if (it != range_map_.end()) {
      auto [lo, hi] = it->second;
      if (hi < lo) {
        std::swap(lo, hi);
      }
      int width = static_cast<int>(hi - lo + 1);
      if (width <= 0 || width > 63) {
        return 0;
      }
      return ((static_cast<int64_t>(1) << width) - 1) << lo;
    }
    Pass::warn("upass.tolg: get_mask mask operand '{}' not const/range — using 0", lnast_->get_name(mask_op));
    return 0;
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
    int64_t mask = mask_from_operand(mask_op);
    auto    vv   = leaf(val);

    auto node = create_typed_node(*g_, Ntype_op::Set_mask);
    setup_sink_by_name(node, "a").connect_driver(vv.pin);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *Const::create_integer(mask)));
    setup_sink_by_name(node, "value").connect_driver(leaf(ins).pin);
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), vv.mw);
  }

  enum class OpW { add, mul, maxw, firstw, boolw };

  // n-ary op: child0 = dst, children 1..N = operands. Commutative ops feed all
  // operands into sink "a"; positional binary ops use "a" then "b".
  void lower_op(const Lnast_nid& nid, Ntype_op op, bool commutative, OpW wmode) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto   node    = create_typed_node(*g_, op);
    Bits_t max_mw  = 0;
    Bits_t sum_mw  = 0;
    Bits_t first_mw = 0;
    bool   first   = true;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      auto v      = leaf(c);
      max_mw      = std::max(max_mw, v.mw);
      sum_mw     += v.mw;
      if (first) {
        first_mw = v.mw;
      }
      setup_sink_by_name(node, (commutative || first) ? "a" : "b").connect_driver(v.pin);
      first = false;
    }
    Bits_t mw = 1;
    switch (wmode) {
      case OpW::add: mw = static_cast<Bits_t>(max_mw + 1); break;
      case OpW::mul: mw = sum_mw > 0 ? sum_mw : Bits_t{1}; break;
      case OpW::maxw: mw = max_mw; break;
      case OpW::firstw: mw = first_mw; break;
      case OpW::boolw: mw = 1; break;
    }
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), mw);
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
    auto node = create_typed_node(*g_, op);
    setup_sink_by_name(node, "a").connect_driver(v.pin);
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), v.mw);
  }

  // ne/le/ge = Not(eq/gt/lt(...)). Result is 1-bit boolean.
  void lower_negated(const Lnast_nid& nid, Ntype_op inner_op, bool commutative) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto inner = create_typed_node(*g_, inner_op);
    bool first = true;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      setup_sink_by_name(inner, (commutative || first) ? "a" : "b").connect_driver(leaf(c).pin);
      first = false;
    }
    auto not_node = create_typed_node(*g_, Ntype_op::Not);
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
  void lower_if(const Lnast_nid& nid) {
    struct Branch {
      bool     is_else{false};
      Pin      cond;
      WriteMap writes;
    };
    std::vector<Branch> branches;

    auto child = lnast_->get_first_child(nid);
    if (child.is_invalid()) {
      return;
    }
    Pin first_cond = leaf(child).pin;  // child0 = condition
    child          = lnast_->get_sibling_next(child);
    if (child.is_invalid()) {
      return;
    }
    branches.push_back({false, first_cond, lower_branch(child)});  // then-stmts

    child = lnast_->get_sibling_next(child);
    while (!child.is_invalid()) {
      bool last = lnast_->is_last_child(child);
      if (last && Lnast_ntype::is_stmts(lnast_->get_type(child))) {
        branches.push_back({true, Pin{}, lower_branch(child)});  // bare else
        break;
      }
      Pin elif_cond = leaf(child).pin;
      child         = lnast_->get_sibling_next(child);
      if (child.is_invalid()) {
        break;
      }
      branches.push_back({false, elif_cond, lower_branch(child)});
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

    for (const auto& var : all_vars) {
      Pin  cur;
      auto base = pin_map_.find(var);
      cur       = (base != pin_map_.end()) ? base->second : nil_pin();
      auto ew   = else_writes.find(var);
      if (ew != else_writes.end()) {
        cur = ew->second;
      }

      int n         = static_cast<int>(branches.size());
      int last_cond = has_else ? n - 2 : n - 1;
      for (int i = last_cond; i >= 0; --i) {
        auto& br       = branches[i];
        auto  wr       = br.writes.find(var);
        Pin   true_val = (wr != br.writes.end()) ? wr->second : cur;

        auto mux = create_typed_node(*g_, Ntype_op::Mux);
        mux.create_sink_pin(0).connect_driver(br.cond);   // selector
        mux.create_sink_pin(1).connect_driver(cur);       // false / else
        mux.create_sink_pin(2).connect_driver(true_val);  // true / then
        cur = mux.create_driver_pin(0);
      }
      // The merged value's width is the widest among the branch sources.
      bind_result(var, cur, mw_lookup(var));
    }
  }

  std::shared_ptr<Lnast>      lnast_;
  hhds::Graph*                g_;
  const uPass_tolg::Registry* registry_ = nullptr;
  hhds::GraphLibrary*         lib_      = nullptr;

  absl::flat_hash_map<std::string, Pin>                         pin_map_;
  absl::flat_hash_map<std::string, Bits_t>                      mw_map_;
  absl::flat_hash_map<std::string, std::pair<int64_t, int64_t>> range_map_;
  std::vector<WriteMap>                                         branch_writes_;
  std::vector<std::string>                                      out_names_;
  WriteMap                                                      empty_writes_;

  // Task 1q — reg lowering state. reg_map_ holds each declared reg's Flop
  // node (reads resolve to its q via pin_map_; the din store connects through
  // the node handle here). clock_* lazily binds the clock graph input.
  absl::flat_hash_map<std::string, hhds::Node_class> reg_map_;
  absl::flat_hash_set<std::string>                   reg_din_connected_;
  std::string                                        clock_name_;
  bool                                               clock_minted_ = false;
  Pin                                                clock_pin_;
  bool                                               clock_pin_valid_ = false;

  absl::flat_hash_map<std::string, Pending_stage> pending_stage_;
  absl::flat_hash_map<std::string, Sub_out>       sub_out_stages_;

  // Task 1u-C/D — checker inputs gathered while building: pending records,
  // per-Flop effective crossing depth, per-Sub pinned latency interval.
  std::vector<Pending_rec>                                       pending_checks_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>     flop_depth_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>     sub_time_;

public:
  // Task 1u-C — lower the partition's declared per-output intervals as
  // pending checks on the GraphIO output sinks (mod: the @[N] landing cycle;
  // pipe: the declared range — comb bodies must land at exactly that range,
  // sigma>0 narrowing lights up with 2d). `@[]`-opted-out outputs (nil) are
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

  [[nodiscard]] std::vector<Pending_rec>&&                                   take_pending_checks() { return std::move(pending_checks_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& take_flop_depths() { return std::move(flop_depth_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& take_sub_times() { return std::move(sub_time_); }
};

// Task 1u-D — the combined pipe/mod LG time checker (ex-1q-D, written once
// for both kinds). Runs at the tolg seam on the just-built graph:
//   1. Kahn topological pass over the node graph; leftover nodes form a
//      cycle: a cycle containing a Flop is a STATE register group (lands
//      with task 2d reg lowering — clean error until then), a comb-only
//      cycle is the classic combinational-loop error. This is the
//      pre-2d-trivial form of the Tarjan classification: with no state
//      support every Flop must be a pure feedforward STAGE crossing.
//   2. Forward (min,max) interval propagation: graph inputs (0,0);
//      constants unify with anything; comb cells take the equal-meet of
//      their operands (a mismatch is the 06c misalignment error); a Flop
//      adds its effective crossing depth; a Sub adds its pinned instance
//      interval (clock sinks excluded from meets).
//   3. Discharge every pending record: computed == asserted -> REMOVE the
//      pending_time attr; mismatch (or an undischargeable record) -> error.
class Time_checker {
public:
  struct TR {
    int64_t min = 0;
    int64_t max = 0;
    bool    any = false;  // constants — unify with any cycle
  };

  Time_checker(hhds::Graph* g, const std::shared_ptr<Lnast>& ln, std::vector<Pending_rec>&& pendings,
               absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& flop_depth,
               absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& sub_time)
      : g_(g), ln_(ln), pendings_(std::move(pendings)), flop_depth_(std::move(flop_depth)), sub_time_(std::move(sub_time)) {}

  void run() {
    using livehd::graph_util::is_graph_input_pin;
    using livehd::graph_util::is_type_const;
    using livehd::graph_util::type_op_of;

    // 1. Collect nodes + Kahn topo.
    std::vector<hhds::Node_class>        nodes;
    absl::flat_hash_map<uint64_t, size_t> idx;
    for (auto n : g_->fast_class()) {
      if (is_type_const(n)) {
        continue;
      }
      idx.emplace(n.get_debug_nid(), nodes.size());
      nodes.push_back(n);
    }
    std::vector<int> indeg(nodes.size(), 0);
    auto pred_of = [&](const hhds::Edge_class& e) -> int {
      if (e.driver.is_invalid() || is_graph_input_pin(e.driver)) {
        return -1;
      }
      auto mn = e.driver.get_master_node();
      if (mn.is_invalid() || is_type_const(mn) || type_op_of(mn) == Ntype_op::Nconst) {
        return -1;
      }
      auto it = idx.find(mn.get_debug_nid());
      return it == idx.end() ? -1 : static_cast<int>(it->second);
    };
    for (size_t i = 0; i < nodes.size(); ++i) {
      for (const auto& e : nodes[i].inp_edges()) {
        if (pred_of(e) >= 0) {
          ++indeg[i];
        }
      }
    }
    std::vector<size_t> queue;
    for (size_t i = 0; i < nodes.size(); ++i) {
      if (indeg[i] == 0) {
        queue.push_back(i);
      }
    }
    size_t processed = 0;
    while (!queue.empty()) {
      const size_t i = queue.back();
      queue.pop_back();
      ++processed;
      eval_node(nodes[i]);
      for (const auto& e : nodes[i].out_edges()) {
        if (e.sink.is_invalid()) {
          continue;
        }
        auto sn = e.sink.get_master_node();
        if (sn.is_invalid()) {
          continue;
        }
        auto it = idx.find(sn.get_debug_nid());
        if (it == idx.end()) {
          continue;
        }
        if (--indeg[it->second] == 0) {
          queue.push_back(it->second);
        }
      }
    }
    if (processed < nodes.size()) {
      bool has_flop = false;
      for (size_t i = 0; i < nodes.size(); ++i) {
        if (indeg[i] > 0 && livehd::graph_util::is_type_flop(nodes[i])) {
          has_flop = true;
          break;
        }
      }
      if (has_flop) {
        Pass::error("upass.tolg: '{}' has register feedback (a STATE register) — state in pipe/mod bodies lands with "
                    "task 2d reg lowering",
                    ln_->get_top_module_name());
      } else {
        Pass::error("upass.tolg: combinational loop in '{}'", ln_->get_top_module_name());
      }
      return;
    }

    // 3. Discharge pendings.
    for (const auto& rec : pendings_) {
      TR cur;
      if (rec.is_sink) {
        auto edges = rec.pin.inp_edges();
        if (edges.empty()) {
          continue;  // undriven output already warned/nil-wired
        }
        cur = pin_tr(edges.front().driver);
      } else {
        cur = pin_tr(rec.pin);
      }
      if (cur.any || (cur.min == rec.min && cur.max == rec.max)) {
        rec.pin.attr(livehd::attrs::pending_time).del();  // removed once checked
        continue;
      }
      Pass::error("upass.tolg: '{}' in '{}' lands at cycle(s) ({},{}) but ({},{}) is {}",
                  rec.name,
                  ln_->get_top_module_name(),
                  cur.min,
                  cur.max,
                  rec.min,
                  rec.max,
                  rec.is_sink ? "declared at the interface" : "asserted by `@[N]`");
      return;
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

  void eval_node(const hhds::Node_class& node) {
    using livehd::graph_util::is_type_flop;
    using livehd::graph_util::is_type_sub;
    using livehd::graph_util::type_op_of;

    const auto nid = node.get_debug_nid();
    TR         meet{0, 0, true};

    // Operand selection per kind: a Flop reads only din; a Sub skips its
    // clock sinks; everything else meets all inputs (consts unify).
    absl::flat_hash_set<uint64_t> skip_pids;
    bool                          din_only = false;
    if (is_type_flop(node)) {
      din_only = true;
    } else if (is_type_sub(node)) {
      auto gio = node.get_subnode_io();
      if (gio) {
        for (const auto& d : gio->get_input_pin_decls()) {
          if (d.name == "clock" || d.name == "clk") {
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
        Pass::error("upass.tolg: '{}' mixes values at different cycles (({},{}) vs ({},{})) at a {} cell (sink pid {}) "
                    "— align them with `stage[N]` first",
                    ln_->get_top_module_name(),
                    meet.min,
                    meet.max,
                    t.min,
                    t.max,
                    Ntype::get_name(type_op_of(node)),
                    spid);
        return;
      }
    }

    TR out = meet.any ? TR{0, 0, true} : meet;
    if (is_type_flop(node)) {
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
  absl::flat_hash_map<uint64_t, TR>                          tr_;
};

// Task 1u-A — transitive clock need. A module needs a clock when its own
// tree declares a reg (stage decls synthesize reg declares) or when any
// pipe/mod CALLEE transitively needs one (its instance's minted clock pin
// must be forwarded). Memoized over the registry; an instantiation cycle
// (illegal mutual hierarchy) breaks false so we never hang — the real
// recursion diagnostic belongs to a later phase.
[[nodiscard]] bool tree_declares_reg(const std::shared_ptr<Lnast>& lnast) {
  std::function<bool(const Lnast_nid&)> has_reg = [&](const Lnast_nid& nid) -> bool {
    if (Lnast_ntype::is_declare(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        if (!c1.is_invalid()) {
          auto c2 = lnast->get_sibling_next(c1);
          if (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2))) {
            auto mode = lnast->get_name(c2);
            if (mode == "reg" || mode.starts_with("reg ")) {
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

// Shared phase-1 io+clock GraphIO registration. Idempotent (the GraphIO add
// calls are has_-guarded). Returns {clock_name, clock_minted} for the body
// build; empty clock_name = the module needs no clock.
[[nodiscard]] std::pair<std::string, bool> setup_io_impl(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path,
                                                         const uPass_tolg::Registry& registry) {
  auto& lib      = livehd::Hhds_graph_library::instance(lib_path);
  auto  mod_name = std::string(lnast->get_top_module_name());

  auto gio = lib.find_io(mod_name);
  if (!gio) {
    gio = lib.create_io(mod_name);
  }

  // Declare I/O on the GraphIO: positional pin ids + meaningful bits + sign.
  // Port widths cgen emits come from these decls (meaningful width, not the +1
  // internal convention).
  hhds::Port_id pid = 1;
  auto declare = [&](const Lnast_io_entry& e, bool is_input) {
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

  // Task 1q/1u — implicit clock: when the tree holds state (own regs OR,
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
            Pass::error(
                "upass.tolg: input 'clock' of '{}' is not usable as the pipeline clock (multi-bit data port) "
                "and collides with the implicit clock — rename it or declare it 1-bit",
                mod_name);
          }
        }
        for (const auto& e : lnast->io_meta().outputs) {
          if (e.name == "clock") {
            Pass::error("upass.tolg: output 'clock' of '{}' collides with the implicit pipeline clock — rename it", mod_name);
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

  return {clock_name, clock_minted};
}

}  // namespace

void uPass_tolg::register_io(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path, const Registry& registry) {
  if (!lnast || lnast->io_meta().empty()) {
    return;  // not a lowerable module (e.g. the empty file-root tree)
  }
  (void)setup_io_impl(lnast, lib_path, registry);
}

std::shared_ptr<hhds::Graph> uPass_tolg::run(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path,
                                             const Registry& registry) {
  if (!lnast || lnast->io_meta().empty()) {
    return nullptr;  // not a lowerable module (e.g. the empty file-root tree)
  }

  auto [clock_name, clock_minted] = setup_io_impl(lnast, lib_path, registry);

  auto& lib      = livehd::Hhds_graph_library::instance(lib_path);
  auto  gio      = lib.find_io(std::string(lnast->get_top_module_name()));
  auto  g_shared = gio->has_graph() ? gio->get_graph() : gio->create_graph();

  Tolg builder(lnast, g_shared.get(), clock_name, clock_minted, &registry, &lib);
  builder.build();

  // Task 1u-D — the combined pipe/mod time checker at the tolg seam.
  {
    const auto kind = lnast->get_lambda_kind();
    if (kind == "pipe" || kind == "mod") {
      Time_checker checker(g_shared.get(),
                           lnast,
                           builder.take_pending_checks(),
                           builder.take_flop_depths(),
                           builder.take_sub_times());
      checker.run();
    }
  }

  g_shared->commit();
  return g_shared;
}
