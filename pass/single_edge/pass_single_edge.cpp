// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_single_edge.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "attr_carry.hpp"
#include "cell.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;
namespace lc = livehd::latch_contract;

namespace livehd::single_edge {

namespace {

constexpr std::string_view kPass = "pass.single_edge";

// The synthesized phase register's name lives in node_util.hpp because
// pass/lec/query.cpp needs the SAME spelling for its keep-init exception (a
// free phase lets the solver pick the wrong sub-step parity). It must survive
// canon_flop_name IDENTICALLY on both sides of a miter, or it lands in the
// per-side fallback instead of shared_inputs and the two sides start from
// independent phases — the failure mode `clk_prev_` had to be fixed for.
constexpr std::string_view kPhaseName = gu::single_edge_phase_name;

// ---------------------------------------------------------------------------
// Slot assignment
//
// The commit-class -> slot table is exactly M3's landed classifier plus a
// choice of reference clock. It is derived STRUCTURALLY (from the (net, edge)
// pair), never from node or traversal order: the pass runs independently on
// both sides of a miter and a slot disagreement is a false REFUTED.
//
//   ref clock RISE  -> slot 0
//   ref clock FALL  -> slot 1
//   data-gated latch -> the slot of the state its ENABLE cone depends on
//                       (slot 0 when the enable is input-driven)
//
// Coincident (net, edge) => the SAME slot, by construction: the slot is a pure
// function of the class key. That is the soundness precondition condition 5
// names; what it does NOT give is the L1 case, handled by rule 4 below.
struct Element {
  hhds::Node_class            node;
  lc::Commit_class            cc;
  bool                        is_latch = false;
  int                         slot     = 0;
  // A decoded `<clock> & <enables>` clock cone. Present => this element's
  // gated clock is rewritten into its ENABLE (see apply_icg): it commits on the
  // reference clock's edge iff the enables hold, which is exactly what the
  // encoder models natively and strictly cheaper than a per-step commit term.
  std::optional<lc::Icg_cone> icg;
  // This element samples on the edge the enable latch OPENS at -- a FALLING
  // consumer of a low-transparent ICG. The arm bypass is not exact there, so
  // the GATE GUARD (and only the gate guard) is sampled in the rising slot and
  // held; see apply_icg's hold path.
  bool                        icg_hold        = false;
  // Enable latches this GATED LATCH reads by their held Q (the window rule in
  // build_plan): the gate is steady over its whole open phase, so no bypass.
  int                         icg_window_held = 0;
  // Commits on a NON-reference root (Options::multi_clock): left the plain
  // posedge flop it is -- no slot gate, no sync-reset move, clock_pin rebound to
  // its OWN root; only its gated clock (if any) is folded.
  bool                        off_ref         = false;
};

// Is an instance's definition purely combinational (no state anywhere below
// it)? A body-less def is unknown and counted as combinational: the walk below
// is a REFUSAL test, so the conservative answer is the one that keeps walking.
bool def_is_comb(const std::shared_ptr<hhds::Graph>& def, absl::flat_hash_map<const hhds::Graph*, bool>& memo, int depth) {
  if (!def) {
    return true;
  }
  if (auto it = memo.find(def.get()); it != memo.end()) {
    return it->second;
  }
  memo[def.get()] = false;  // a def reached again while open is a cycle: not provably combinational
  bool comb       = depth < 16;
  for (auto dn : def->body().nodes()) {
    if (!comb) {
      break;
    }
    const auto op = gu::type_op_of(dn);
    if (gu::is_type_register(dn)) {
      comb = false;
    } else if (op == Ntype_op::Sub) {
      comb = def_is_comb(dn.get_subnode_graph(), memo, depth + 1);
    }
  }
  memo[def.get()] = comb;
  return comb;
}

// The clock input a DATA-gated latch's window still reads, or invalid. Such a
// latch keeps its enable through the rewrite and evaluates it on every
// sub-step, so a clock in that cone is read AS DATA after normalization: the
// window follows the free sub-step clock, not the source phase. Every shape the
// pass understands (a clock-phase window, a gate cone) is classified as
// Clock-role before it gets here; what is left reads the clock through logic
// the classifier could not resolve -- a gate inside an instance (a mapped
// netlist's AND2 cell under a wrapper module), a reduction it does not
// model. Walks the combinational cone INTO combinational instances (an
// instance output is taken to depend on every input) and through Clock_cells,
// stopping at state.
hhds::Pin_class window_clock_read(const hhds::Pin_class& start, const lc::Design_clocks& clocks) {
  absl::flat_hash_set<hhds::Class_index>        seen;
  absl::flat_hash_map<const hhds::Graph*, bool> comb_def;
  std::vector<hhds::Pin_class>                  work{start};
  while (!work.empty()) {
    auto p = work.back();
    work.pop_back();
    if (p.is_invalid() || p.is_const()) {
      continue;
    }
    if (gu::is_graph_input_pin(p)) {
      if (clocks.is_clock(p)) {
        return p;
      }
      continue;
    }
    if (!seen.insert(p.get_class_index()).second) {
      continue;
    }
    auto n = p.get_master_node();
    if (gu::is_type_register(n)) {
      continue;  // state: a value the window may read
    }
    if (gu::type_op_of(n) == Ntype_op::Sub && !def_is_comb(n.get_subnode_graph(), comb_def, 0)) {
      continue;  // a stateful instance: its outputs are state, not a path from its clock input
    }
    for (auto sink : n.inp_sorted_pins()) {  // read-only pin walk
      for (const auto& drv : sink.get_driver_pins()) {
        work.push_back(drv);
      }
    }
  }
  return {};
}

// Backward combinational reach to STATE elements (stops at any register), so a
// data-gated latch can inherit the slot of whatever drives its enable.
void comb_state_reach(const hhds::Pin_class& start, absl::flat_hash_set<hhds::Class_index>& hit) {
  if (start.is_invalid()) {
    return;
  }
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Pin_class>           work{start};
  while (!work.empty()) {
    auto p = work.back();
    work.pop_back();
    if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p)) {
      continue;
    }
    if (!seen.insert(p.get_class_index()).second) {
      continue;
    }
    auto n = p.get_master_node();
    if (gu::is_type_register(n)) {
      hit.insert(n.get_class_index());
      continue;
    }
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      continue;  // opaque instance
    }
    for (auto sink : n.inp_sorted_pins()) {  // read-only pin walk
      // PLURAL: a compact loop's carry-in sink holds two drivers
      // (pass/legalize/legalize.cpp:301).
      for (const auto& drv : sink.get_driver_pins()) {
        work.push_back(drv);
      }
    }
  }
}

hhds::Pin_class sink_driver(const hhds::Node_class& n, std::string_view pin) { return gu::get_driver_of_sink_name(n, pin); }

// The SOURCE of a zero-extend wrapper, or an invalid pin.
//
// An ICG's enable often reaches the gate through a width adapter rather than
// directly: `Get_mask(en, 0, W)` with W >= bits(en) is a zero extension, and
// the enable latch sits UNDER it. The bypass below tests the driver's own node
// type, so a wrapped enable looks like "not a latch", the bypass does not fire,
// and the enable stays a full cycle late -- the defect is invisible because
// nothing refuses, the netlist is simply wrong.
//
// Only a widening window anchored at 0 counts. `lo != 0` is a field read, not
// an extension, and a result narrower than its source TRUNCATES: neither
// preserves the value, so neither may be stepped through.
hhds::Pin_class zext_source(const hhds::Pin_class& p) {
  if (p.is_invalid()) {
    return {};
  }
  auto n = p.get_master_node();
  if (gu::type_op_of(n) != Ntype_op::Get_mask) {
    return {};
  }
  auto a  = gu::get_driver_of_sink_name(n, "a");
  auto lo = gu::get_driver_of_sink_name(n, "lo");
  auto hi = gu::get_driver_of_sink_name(n, "hi");
  if (a.is_invalid() || lo.is_invalid() || hi.is_invalid() || !lo.is_const() || !hi.is_const()) {
    return {};
  }
  const auto& lc_ = gu::const_of(lo);
  const auto& hc_ = gu::const_of(hi);
  if (lc_.has_unknowns() || hc_.has_unknowns() || !lc_.is_just_i64() || !hc_.is_just_i64()) {
    return {};
  }
  if (lc_.to_just_i64() != 0) {
    return {};  // a field read, not an extension
  }
  const auto src_b = gu::bits_of(a);
  const auto res_b = gu::bits_of(p);
  if (src_b == 0 || res_b == 0 || res_b < src_b || hc_.to_just_i64() < static_cast<int64_t>(src_b)) {
    return {};  // unknown width, or a TRUNCATING window: not a zero extension
  }
  return a;
}

// Mapped clock logic can spell the held enable as (q == 1), or two
// comparisons with zero. Peel only identities of an unsigned one-bit value;
// an inverted enable or a comparison of a wider value must stay intact.
hhds::Pin_class identity_enable_source(const hhds::Pin_class& p) {
  auto value    = p;
  bool inverted = false;
  for (int depth = 0; depth < 16; ++depth) {
    if (value.is_invalid() || value.is_const() || gu::is_graph_input_pin(value)) {
      break;
    }
    if (const auto next = zext_source(value); !next.is_invalid()) {
      value = next;
      continue;
    }
    const auto node = value.get_master_node();
    if (gu::type_op_of(node) != Ntype_op::EQ) {
      break;
    }
    const auto inputs = gu::inp_sink_drivers(node);
    if (inputs.size() != 2 || inputs[0].driver.is_const() == inputs[1].driver.is_const()) {
      break;
    }
    const auto  constant = inputs[0].driver.is_const() ? inputs[0].driver : inputs[1].driver;
    const auto  next     = inputs[0].driver.is_const() ? inputs[1].driver : inputs[0].driver;
    const auto& literal  = gu::const_of(constant);
    if (gu::bits_of(next) != 1 || !gu::is_unsign(next) || !literal.is_just_i64()
        || (literal.to_just_i64() != 0 && literal.to_just_i64() != 1)) {
      break;
    }
    inverted ^= literal.to_just_i64() == 0;
    value     = next;
  }
  return !inverted && value != p ? value : hhds::Pin_class{};
}

// ---------------------------------------------------------------------------
// Memory ports under a phase divider.
//
// A `Memory` cell's pins are PER PORT: raw_pid = base + port * stride, which is
// how `parse_memory_info` recovers the port id.
//
// PORT DISCOVERY FOLLOWS THE NATIVE CONTRACT.  A port exists when ANY of its
// fields is driven -- `addr`, `din`, `enable`, `clock_pin` -- and `rdport`
// DEFAULTS TO FALSE, i.e. a write port (`Memory_port_info::rdport = false` in
// pass/lean/Exporter/Frontend/graph_access.hpp, which cgen and the Lean scanner both rely on).
// Discovering ports from `rdport` pins alone would make a port that carries
// `addr`/`din`/`enable` but no explicit `rdport` INVISIBLE, and a writable cell
// would then be mistaken for a ROM and left ungated.  That is a correctness
// hole, not a cosmetic one.
struct Mem_port {
  uint32_t        port      = 0;
  bool            rdport    = false;  // native default: a WRITE port
  // This port's OWN read timing (Ntype::Memory_rdport_sync). The cell-global
  // `type` cannot express a memory whose read ports differ, so for a MIXED
  // cell this is the only thing that answers "does this port commit".
  bool            rd_sync   = false;
  bool            has_rdpin = false;
  hhds::Pin_class enable;
  hhds::Pin_class clock;
  bool            has_addr = false;
  bool            has_din  = false;
};

struct Mem_shape {
  bool                  parsed      = true;   // every consulted policy pin was constant
  bool                  sync_read   = false;  // a REGISTERED read commits here
  bool                  mixed_read  = false;  // `type == Ntype::Memory_type_mixed`
  bool                  rising      = true;   // `posclk == 1`
  bool                  mixed_edge  = false;  // `posclk == Ntype::Memory_posclk_mixed`
  bool                  whole_array = false;  // `update`/`update_enable`/`reset` driven
  uint32_t              wensize     = 0;
  std::vector<Mem_port> ports;
};

Mem_shape mem_shape_of(const hhds::Node_class& n) {
  Mem_shape                               ms;
  const auto                              stride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  absl::flat_hash_map<uint32_t, Mem_port> byport;
  auto                                    at = [&](uint32_t port) -> Mem_port& {
    auto& p = byport[port];
    p.port  = port;
    return p;
  };
  for (const auto& e : gu::inp_sink_drivers(n)) {
    const auto raw   = static_cast<uint32_t>(e.get_port_id());
    const auto pname = std::string(Ntype::get_sink_name(Ntype_op::Memory, raw % stride));
    const auto port  = raw / stride;
    if (pname == "addr") {
      at(port).has_addr = true;
    } else if (pname == "din") {
      at(port).has_din = true;
    } else if (pname == "enable") {
      at(port).enable = e.driver;
    } else if (pname == "clock_pin") {
      at(port).clock = e.driver;
    } else if (pname == "rdport") {
      if (!e.driver.is_const() || !gu::const_of(e.driver).is_just_i64()) {
        ms.parsed = false;
        continue;
      }
      auto&      p  = at(port);
      const auto rd = gu::const_of(e.driver).to_just_i64();
      p.rdport      = rd != 0;
      // Only a 2 states SYNC by itself. A 1 means "this is a read port" and
      // leaves the timing to the cell-global `type` -- the encoding every graph
      // written before `rdport` was widened uses.
      p.rd_sync     = rd == Ntype::Memory_rdport_sync;
      p.has_rdpin   = true;
    } else if (pname == "update" || pname == "update_enable" || pname == "reset") {
      // A WHOLE-ARRAY cell: one bulk next-state bus instead of per-entry write
      // ports, and `reset` restores `init` at RUNTIME.  cgen_verilog and
      // pass/lean/Exporter/Frontend/graph_access.cpp classify these pins independently of the
      // per-entry write ports, so their presence must be recorded on its own
      // and NOT inferred from whether a write port exists.
      ms.whole_array = true;
    } else if (pname == "type" || pname == "wensize" || pname == "posclk") {
      if (!e.driver.is_const() || !gu::const_of(e.driver).is_just_i64()) {
        ms.parsed = false;
        continue;
      }
      const auto v = gu::const_of(e.driver).to_just_i64();
      if (pname == "type") {
        // Conservative on purpose, and UNCHANGED for the scalar types: both
        // consumers of `sync_read` only make the pass less willing to call a
        // cell an immutable ROM and more willing to refuse, so reading an array
        // (2) as "registered" errs safely. A MIXED cell (3) has no scalar
        // answer at all, so the ports are asked below instead.
        ms.sync_read  = v != 0;
        ms.mixed_read = v == Ntype::Memory_type_mixed;
      } else if (pname == "wensize") {
        ms.wensize = v > 0 ? static_cast<uint32_t>(v) : 0;
      } else {
        // `posclk` is TRISTATE: 1 = rising, 0 = falling, and
        // `Ntype::Memory_posclk_mixed` = the ports do not share an edge.
        // Reading "nonzero == rising" would silently call a MIXED cell rising.
        ms.mixed_edge = v == Ntype::Memory_posclk_mixed;
        ms.rising     = v == 1;
      }
    }
  }
  for (auto& [port, p] : byport) {
    // A block carrying only scalar policy pins is not a port.
    if (p.has_addr || p.has_din || !p.enable.is_invalid() || !p.clock.is_invalid() || p.has_rdpin) {
      ms.ports.push_back(p);
    }
  }
  std::sort(ms.ports.begin(), ms.ports.end(), [](const Mem_port& a, const Mem_port& b) { return a.port < b.port; });
  // A MIXED cell's read timing is PER PORT, so the cell-global answer above is
  // meaningless for it and the ports decide: the cell has a registered read iff
  // some read port says so. Done after the sort rather than inside the pin walk
  // so it cannot depend on the order pins are visited.
  //
  // The scalar types are left exactly as they were. `type == 1` already implies
  // every read port is registered, and `type == 2` keeps its deliberately
  // conservative reading.
  if (ms.mixed_read) {
    ms.sync_read = false;
    for (const auto& p : ms.ports) {
      if (p.rdport && p.rd_sync) {
        ms.sync_read = true;
        break;
      }
    }
  }
  return ms;
}

// ONE VALIDATED WRITE PORT, FROZEN BEFORE ANY MUTATION.
//
// Every fact the gate needs is decided and checked during the refusal pass and
// then CONSUMED from here: nothing is recomputed at mutation time, and there is
// no slot fallback.  A port that could not be classified never reaches this
// vector -- the pass has already returned a refusal.
struct Mem_gate_plan {
  hhds::Node_class             node;
  uint32_t                     port = 0;
  hhds::Pin_class              enable;  // INVALID means "absent" -> all lanes write
  uint32_t                     wensize = 0;
  int                          slot    = 0;
  // A GATED write clock, decided during the refusal scan and FROZEN here as the
  // gate's already-latch-bypassed operand PINS. They are pins that already
  // exist; nothing is allocated during planning, because planning must leave
  // the graph untouched (`opts.dry_run` returns before any rewrite and promises
  // exactly that). The AND cone over them is built in the rewrite.
  std::vector<hhds::Pin_class> gate_ops;
  // The clock_pin to rebind to once the gate is folded: invalid means the
  // reference pin; a port on a SECOND clock domain (multi_clock) keeps its own
  // resolved root, because it is exported, not lowered.
  hhds::Pin_class              own_root;
};

// Does a write port of `n` sit behind a clock GATE? Such a port owes the fold
// below even without a phase divider: the exporter refuses a gate left on
// clock_pin, since dropping it would commit unconditionally.
bool mem_has_gated_write_port(const hhds::Node_class& n, const lc::Design_clocks& clocks) {
  const auto ms = mem_shape_of(n);
  if (!ms.parsed) {
    return false;  // judged by name where the scan runs; not a reason to start one
  }
  for (const auto& port : ms.ports) {
    if (!port.rdport && !port.clock.is_invalid() && lc::resolve_icg(port.clock, clocks)) {
      return true;
    }
  }
  return false;
}

// A cell with no committing port: an immutable asynchronous ROM.  `update`,
// `update_enable` and whole-array reset cells all carry a write port and are
// NOT this.
bool mem_is_immutable_rom(const Mem_shape& ms) {
  if (ms.sync_read || ms.whole_array) {
    // A registered read commits whatever the write side is, and a whole-array
    // cell commits through its bulk bus even with no per-entry write port --
    // so neither is an immutable ROM, and `update`/`update_enable`/`reset`
    // must be judged BEFORE any ROM classification.
    return false;
  }
  for (const auto& p : ms.ports) {
    if (!p.rdport) {
      return false;
    }
  }
  return true;
}

// The per-port sink pin, by name, at a given port.
hhds::Pin_class mem_port_driver(const hhds::Node_class& n, std::string_view pin, uint32_t port) {
  const auto stride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  const auto want   = Ntype::get_sink_pid(Ntype_op::Memory, pin) + port * stride;
  for (const auto& e : gu::inp_sink_drivers(n)) {
    if (static_cast<uint32_t>(e.get_port_id()) == want) {
      return e.driver;
    }
  }
  return hhds::Pin_class{};
}

void mem_drop_port_sink(const hhds::Node_class& n, std::string_view pin, uint32_t port) {
  const auto                              stride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  const auto                              want   = Ntype::get_sink_pid(Ntype_op::Memory, pin) + port * stride;
  absl::InlinedVector<hhds::Pin_class, 4> doomed;
  for (auto sink : n.inp_sorted_pins()) {
    if (static_cast<uint32_t>(sink.get_port_id()) == want) {
      doomed.push_back(sink);
    }
  }
  for (const auto& sink : doomed) {
    sink.del_sink();
  }
}

void drop_sink(const hhds::Node_class& n, std::string_view pin) {
  const auto pid = Ntype::get_sink_pid(gu::type_op_of(n), pin);
  if (pid == hhds::Port_invalid) {
    return;
  }
  // Snapshot the pins, then delete: inp_sorted_pins() is a view over live
  // storage, so the del_sink() calls must land after the walk (the hhds rule
  // the edge form obeyed by collecting `doomed` first).
  absl::InlinedVector<hhds::Pin_class, 4> doomed;
  for (auto sink : n.inp_sorted_pins()) {
    if (sink.get_port_id() == pid) {
      doomed.push_back(sink);
    }
  }
  for (const auto& sink : doomed) {
    sink.del_sink();  // one driver per sink pin
  }
}

std::string label_of(const hhds::Node_class& n) { return gu::debug_name(n); }

void refuse(bool quiet, std::string_view code, const std::string& msg, std::string_view hint) {
  if (quiet) {
    return;
  }
  livehd::diag::err(kPass, code, "unsupported").msg("{}", msg).hint(hint).emit();
}

// Why a Clock_cell GATE CHAIN cannot be folded into ONE endpoint enable, or
// `None`. One walk, but the reason is reported back so each refusal keeps the
// diagnostic code and wording it already had.
enum class Chain_refusal { None, Invert, Div, Unbounded };

// The WHOLE chain has to be walked, not just the outermost cell, because
// `clock_op_of` absorbs every cell's enable into ONE endpoint enable:
//
//   invert — an active-low cell buried in the chain contributes an enable the
//     hardware samples before the FALL, which the fold would then evaluate at
//     the reference RISE: half a period out, and silent.
//   div — worse than invisible. `absorb_chain` does `outer.div = inner.div`, so
//     an OUTER cell with div=2 over an inner div=1 folds to a cone reading
//     div == 1 and the `e.icg->div != 1` backstop below never fires; the
//     divider then gets retimed into an endpoint enable, which is exactly the
//     clock-blindness class the div refusal exists to stop.
//
// single_edge is deliberately STRICTLY MORE CONSERVATIVE than the same walk in
// pass/lec/phase_sched.cpp (`resolve_chain`): that one sets `guard_before_fall`
// only for a CONSTANT `invert` and only refuses a CONSTANT `div`, while a
// NON-CONSTANT one refuses here — its sample phase / divisor is not decidable,
// and single_edge's only move is to decline, never to mis-schedule. Do NOT
// "fix" the divergence by relaxing this side; the two are allowed to disagree
// in the refusing direction, not in the accepting one.
//
// Scan ONE MORE cell than the fold can absorb: `clock_op_depth` recurses while
// `depth < kMaxGateChain` starting at depth 0, so it folds up to
// kMaxGateChain+1 == 17 cells (graph/latch_contract.cpp). The extra hop is what
// lets a legal 17-cell chain run off its end (a non-Clock_cell root) instead of
// tripping the bound. kMaxGateChain lives in that file's anonymous namespace,
// so the value is duplicated here — keep the two in step.
constexpr int kMaxChainScan = 18;

Chain_refusal chain_refusal(hhds::Pin_class cell_out) {
  for (int hops = 0; hops < kMaxChainScan; ++hops) {
    if (cell_out.is_invalid()) {
      return Chain_refusal::None;  // an unconnected clk_ref: the `!e.icg` backstop below catches it
    }
    const auto cr = lc::control_root(cell_out, /*stop_at_clock_cell=*/true);
    if (cr.net.is_invalid() || gu::is_graph_input_pin(cr.net) || cr.net.is_const()) {
      return Chain_refusal::None;  // the chain ended on a real net
    }
    const auto cell = cr.net.get_master_node();
    if (gu::type_op_of(cell) != Ntype_op::Clock_cell) {
      return Chain_refusal::None;
    }
    // A NON-CONSTANT `invert` counts too: the sample phase is then not even
    // decidable here, so fail closed rather than assume the positive flavour.
    if (const auto inv = sink_driver(cell, "invert"); !inv.is_invalid() && (!inv.is_known_false())) {
      return Chain_refusal::Invert;
    }
    // Same fail-closed rule for `div`: `clock_cell_cone` only reads the pin when
    // it is CONST, so a non-constant divisor leaves `Icg_cone::div` at its 1
    // default and reads downstream as "no division at all".
    if (const auto d = sink_driver(cell, "div"); !d.is_invalid()) {
      if (!d.is_const()) {
        return Chain_refusal::Div;
      }
      const auto& dv = gu::const_of(d);
      if (!dv.is_just_i64() || dv.to_just_i64() != 1) {
        return Chain_refusal::Div;
      }
    }
    cell_out = sink_driver(cell, "clk_ref");
  }
  // The bound is exhausted — a chain deeper than the fold can absorb, or a
  // cyclic clock cone (nothing type-checks one). FAIL CLOSED: `clock_op_of`
  // absorbed those cells into one endpoint enable regardless, so returning
  // "nothing wrong" here would ship precisely the silent mis-timing this walk
  // exists to catch.
  return Chain_refusal::Unbounded;
}

// WHICH BIT of a clock root a control cone reads. `control_root` follows every
// Get_mask to the WHOLE input (tolg relies on that), so `clks[0]` and `clks[1]`
// share one commit class keyed `clks`. This walks the same hops while tracking
// the bit the consumer's edge is detected on (bit 0 of the driver), the way
// pass/lec/encode.cpp's Clk_root does, and returns it once the walk lands on
// `root`. nullopt = the bit is not decidable here (a non-constant select, a
// boolean of a multi-bit value, an ambiguous gate): every caller fails closed.
//
// A two-operand And is a clock GATE (`clk & en`, the shape `resolve_icg`
// folds): bit-wise, so the clock bit is the bit of whichever operand reaches
// `root`. Both operands reaching it (`clks[0] & clks[1]`) is ambiguous.
std::optional<int> clock_bit_on(hhds::Pin_class p, const hhds::Pin_class& root, int bit, int depth) {
  for (int hops = 0; hops < 64 && depth < 8; ++hops) {
    if (p.is_invalid() || p.is_const()) {
      return std::nullopt;
    }
    if (gu::is_graph_input_pin(p)) {
      if (p.get_class_index() != root.get_class_index()) {
        return std::nullopt;
      }
      if (const int w = gu::bits_of(p); w > 0 && bit >= w) {
        return std::nullopt;  // a bit past the input: its extension, not a clock bit
      }
      return bit;
    }
    const auto n           = p.get_master_node();
    const auto op          = gu::type_op_of(n);
    // A BOOLEAN of the value (`c ? 1 : 0`, `c == 0/1`): only bit 0 of the
    // result carries it, and it is a single bit of `c` only when `c` is 1 bit.
    auto       follow_bool = [&](const hhds::Pin_class& c) -> bool {
      if (bit != 0 || c.is_invalid() || gu::bits_of(c) != 1) {
        return false;
      }
      p = c;
      return true;
    };
    if (op == Ntype_op::Mux) {
      hhds::Pin_class sel, arm0, arm1;
      for (const auto& in : gu::inp_sink_drivers(n)) {
        const auto pid                            = in.get_port_id();
        (pid == 0 ? sel : pid == 1 ? arm0 : arm1) = in.driver;
      }
      const bool a0 = arm0.is_const() && gu::const_of(arm0).is_just_i64() && gu::const_of(arm0).to_just_i64() == 0;
      const bool a1 = arm1.is_const() && gu::const_of(arm1).is_just_i64() && gu::const_of(arm1).to_just_i64() == 0;
      if (arm0.is_invalid() || arm1.is_invalid() || a0 == a1 || !follow_bool(sel)) {
        return std::nullopt;
      }
      continue;
    }
    if (op == Ntype_op::EQ || op == Ntype_op::Xor) {
      hhds::Pin_class val;
      int             live = 0;
      for (const auto& in : gu::inp_sink_drivers(n)) {
        if (!in.driver.is_const()) {
          val = in.driver;
          ++live;
        }
      }
      if (live != 1 || !follow_bool(val)) {
        return std::nullopt;
      }
      continue;
    }
    if (op == Ntype_op::Not) {
      p = gu::first_value_driver(n);  // bit-wise: the bit stays put
      continue;
    }
    if (op == Ntype_op::And || op == Ntype_op::Or) {
      std::vector<hhds::Pin_class> live;
      for (const auto& in : gu::inp_sink_drivers(n)) {
        if (in.driver.is_const()) {
          // A constant must leave the tracked bit alone (set for And, clear for
          // Or); otherwise the bit is a constant, not a clock.
          const auto& c = gu::const_of(in.driver);
          if (!c.is_just_i64() || bit >= 62) {
            return std::nullopt;
          }
          const bool set = ((c.to_just_i64() >> bit) & 1) != 0;
          if (set != (op == Ntype_op::And)) {
            return std::nullopt;
          }
          continue;
        }
        live.push_back(in.driver);
      }
      if (live.size() == 1) {
        p = live.front();
        continue;
      }
      if (op != Ntype_op::And || live.empty()) {
        return std::nullopt;
      }
      std::optional<int> found;
      for (const auto& l : live) {
        if (auto b = clock_bit_on(l, root, bit, depth + 1)) {
          if (found && *found != *b) {
            return std::nullopt;
          }
          found = b;
        }
      }
      return found;
    }
    if (op == Ntype_op::Get_mask) {
      const auto range = gu::bit_range(n);
      if (!range || range->first + bit >= range->second) {
        return std::nullopt;
      }
      bit += range->first;
      p    = gu::first_value_driver(n);
      continue;
    }
    if (op == Ntype_op::Sext) {
      if (bit != 0) {
        return std::nullopt;  // only bit 0 is position-independent of the sign point
      }
      p = gu::first_value_driver(n);
      continue;
    }
    if (op == Ntype_op::Clock_cell) {
      if (bit != 0) {
        return std::nullopt;
      }
      p = gu::get_driver_of_sink_name(n, "clk_ref");
      continue;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------

struct Plan {
  std::vector<Element>                  elems;
  absl::flat_hash_map<std::string, int> slot_of_key;
  int                                   slots = 1;
  bool                                  ok    = false;
  std::string                           ref_net;      // class key of the reference clock ("" = none)
  std::string                           why;          // failure reason when !ok
  std::string                           code;         // diagnostic code when !ok
  hhds::Pin_class                       ref_clk_pin;  // the reference clock net (may be invalid = implicit)
  // The BIT of `ref_clk_pin` the reference edge is on, when that net is a
  // multi-bit input (-1 = the net itself). The rewrite rebinds every clock_pin
  // to the reference, and a bare multi-bit input reads as its bit 0, so a bit
  // k>0 gets its own select there.
  int                                   ref_clk_bit = -1;
  // The reference clock's name for the cross-side agreement check: `ref_net`,
  // plus `[k]` for a bit k>0 of a bus (encode.cpp's Clk_root spelling), so a
  // side on clks[0] and one on clks[1] are two different time bases.
  std::string                           ref_clock_name;
  int                                   domains = 1;  // clock roots state commits on (multi_clock lets >1 through)
  std::string                           domain_desc;  // per-root detail, for the reason text
  // Decision 3A: (latch, reader) pairs on one commit edge that rule 4 would
  // refuse, admitted by GUARDED READER FORWARDING (apply_reader_forwards).
  std::vector<std::pair<hhds::Node_class, hhds::Node_class>> forwards;
};

Plan build_plan(hhds::Graph* g, const lc::Design_clocks& clocks, bool multi_clock) {
  Plan plan;

  // 1. Collect every state element with its commit class.
  absl::flat_hash_map<std::string, int>                   per_net;  // net_key -> #elements on it (clock role only)
  // One state element per clock net, for the multi-clock refusal: "3 clock
  // nets" alone does not say WHICH nets, and a derived net the walk could not
  // trace to its root reads exactly like a genuine second domain.
  absl::flat_hash_map<std::string, std::string>           example_on_net;
  // Multi-bit clock inputs: net_key -> the distinct bits used ("?<elem>" = not
  // decidable), and the bit itself when decided.
  absl::flat_hash_map<std::string, std::set<std::string>> bus_bits;
  absl::flat_hash_map<std::string, int>                   bus_bit_of;
  for (auto n : g->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Memory) {
      // A memory rides the reference clock. Leaving it untouched is correct at
      // P = 1 and WRONG at P > 1 (it would commit every sub-step), so it is
      // allowed here and rejected below once P is known.
      continue;
    }
    if (op != Ntype_op::Flop && op != Ntype_op::Fflop && op != Ntype_op::Latch) {
      continue;
    }
    const auto control            = sink_driver(n, op == Ntype_op::Latch ? "enable" : "clock_pin");
    const auto cr                 = lc::control_root(control, /*stop_at_clock_cell=*/true);
    const bool through_clock_cell = !cr.net.is_invalid() && !gu::is_graph_input_pin(cr.net) && !cr.net.is_const()
                                    && gu::type_op_of(cr.net.get_master_node()) == Ntype_op::Clock_cell;
    if (through_clock_cell) {
      // The positive gate flavour (`clk & en_latched`) can be retimed exactly
      // into the endpoint enable at the same reference edge. The active-low
      // flavour samples before the falling edge and needs a sub-period the slot
      // model does not have, so it remains a named refusal — ANYWHERE in the
      // chain, because the fold below absorbs every cell's enable into one.
      // Same reach for `div`: the per-cell scan is the ONLY place it is visible,
      // since the fold overwrites the outer cell's divisor with the inner one's
      // (the `e.icg->div != 1` test below stays as a backstop for the shapes
      // that never reach a Clock_cell chain at all).
      const auto refusal = chain_refusal(cr.net);
      if (refusal == Chain_refusal::Invert) {
        plan.code = "clock-cell-sample-phase";
        plan.why  = "state element `" + label_of(n)
                   + "` is clocked through an active-low Clock_cell whose enable sample phase cannot be represented";
        return plan;
      }
      if (refusal == Chain_refusal::Div) {
        plan.code = "clock-cell-unsupported";
        plan.why  = "state element `" + label_of(n)
                    + "` is clocked through a Clock_cell chain that divides the clock, which cannot be retimed to one "
                      "endpoint enable";
        return plan;
      }
      if (refusal == Chain_refusal::Unbounded) {
        plan.code = "clock-cell-unsupported";
        plan.why  = "state element `" + label_of(n)
                    + "` is clocked through a Clock_cell chain deeper than the fold can absorb, so it cannot be retimed "
                      "to one endpoint enable";
        return plan;
      }
    }
    auto cc = lc::commit_class_of(n, &clocks);
    if (!cc) {
      plan.code = "unresolved-commit-class";
      plan.why  = "state element `" + label_of(n) + "` has a control cone that does not resolve to a root net";
      return plan;
    }
    Element e;
    e.node     = n;
    e.cc       = *cc;
    e.is_latch = op == Ntype_op::Latch;
    {
      // A flop's gate lives on `clock_pin`; a LATCH's lives on `enable` (its
      // gate IS its enable). Same cone, same fold: the clock half becomes the
      // commit class, the data half becomes the flop enable.
      e.icg = through_clock_cell ? lc::clock_op_of(control, clocks) : lc::resolve_icg(control, clocks);
      if (through_clock_cell && (!e.icg || e.icg->div != 1)) {
        plan.code = "clock-cell-unsupported";
        plan.why  = "state element `" + label_of(n) + "` has a Clock_cell that cannot be retimed to one endpoint enable";
        return plan;
      }
      if (e.icg) {
        // Resolve the ENABLE-LATCH BYPASS here, during ANALYSIS, because the
        // rewrite loop retypes latches into flops as it goes -- so by the time
        // it reached this flop the enable latch might already be a Flop and the
        // bypass would silently not fire, leaving the enable a full cycle late.
        // (Measured exactly that way: the emitted netlist came out `if (enl)`,
        // the latch's held Q, instead of the value it was passing through.)
        //
        // A real ICG latches its enable on the opposite phase purely to
        // suppress glitches: the latch closes exactly when the flop samples, so
        // the flop sees what the latch was passing THROUGH. Reading its Q
        // instead is the classic L1 error.
        // The gate's enables AS WRITTEN, before any arm bypass below rewrites
        // one: the window rule conditions on the OTHER enables of this cone.
        const std::vector<hhds::Pin_class> gate_enables = e.icg->enables;
        for (auto& en : e.icg->enables) {
          if (en.is_invalid() || en.is_const() || gu::is_graph_input_pin(en)) {
            continue;
          }
          // STEP THROUGH ZERO-EXTEND WRAPPERS before asking what drives the
          // enable. An ICG's enable commonly reaches the gate through a width
          // adapter, and testing the wrapper's node type answers "not a latch",
          // so the bypass silently does not fire and the enable stays a cycle
          // late. The depth bound is a safety rail, not a shape: adapters nest
          // at most a couple deep, and an unbounded walk here would follow a
          // cycle. `en` itself is only rebound once a Latch is actually found,
          // so a wrapper over something else leaves the enable untouched.
          auto dn = en.get_master_node();
          if (gu::type_op_of(dn) != Ntype_op::Latch) {
            auto under = en;
            for (int depth = 0; depth < 4; ++depth) {
              under = identity_enable_source(under);
              if (under.is_invalid()) {
                break;
              }
              auto un = under.get_master_node();
              if (gu::type_op_of(un) == Ntype_op::Latch) {
                en = under;  // the latch IS the enable; the adapter only widened it
                dn = un;
                break;
              }
            }
          }
          if (gu::type_op_of(dn) != Ntype_op::Latch) {
            continue;
          }
          // The bypass is exact ONLY for a latch on the OPPOSITE phase: open for
          // the whole phase before the gated edge, shut for the whole phase
          // after it. A latch open on the GATED phase (`if (clk) en_l = en;
          // clk & en_l`) passes a change of `en` straight through while the
          // gated clock is high -- an extra, glitch edge the real gate was built
          // to suppress -- so reading its arm PROVED that twin equal to a real
          // clock gate. The same phase rule pass/lec/phase_sched.cpp applies to
          // its own bypass; here it fails closed rather than mis-time.
          const bool closed_after = e.cc.rising;  // the reference level after the gated edge
          const bool opposite_here
              = lc::gated_latch_closed_at(dn, e.icg->clock, closed_after) && lc::latch_open_at(dn, e.icg->clock, !closed_after);
          // THE OTHER EDGE OF THE SAME GATE. One ICG legitimately drives both a
          // rising and a falling consumer: CORE-ET's half-cycle register file
          // (tech_generic/prim_rf_1r1w_preview -- `always_ff @(negedge
          // rf_clk_i)` for the data delay against `always_ff @(posedge
          // rf_clk_i)` for the array) sits behind minion_dcache_top's
          // `u_clk_gate`. For the FALLING consumer of a low-transparent gate
          // the latch OPENS at the sampling edge, so Q != D there and the arm
          // bypass is not exact -- but `clk & en_latch` still produces that
          // falling edge iff the value held across the high phase was 1. A
          // lowering question, not a refusal.
          //
          // FLOPS ONLY. A latch behind a gate has its own window and its own
          // commit rules, and a sampled enable is the wrong instrument for it
          // (its rule is `held_for_window`, below).
          const bool opposite_for_other_edge = !e.cc.rising && !e.is_latch
                                               && lc::gated_latch_closed_at(dn, e.icg->clock, /*closed_level=*/true)
                                               && lc::latch_open_at(dn, e.icg->clock, /*level=*/false);
          // A GATED LATCH has its own rule, and it is NOT the flop rule above.
          // A flop samples at one edge, so its gate must be steady ACROSS that
          // edge. A latch is open for a whole PHASE and commits when it closes,
          // so its gate must be steady for the WHOLE WINDOW: the enable latch
          // shut while the gated latch is open, open while it is shut. That is
          // the textbook glitch-free gate in front of a latch -- CORE-ET's
          // tech_generic/prim_write_commit_en:
          //
          //     always_latch if (!clk)          en_1p <= en_i;   // shut while clk is high
          //     always_latch if (clk && en_1p)  q_o   <= d_i;    // open while clk is high
          //
          // The flop rule asks the wrong question here (closed after the
          // FALLING commit edge = open while clk is high, which would be the
          // glitchy gate) and refused it as `clock-gate-latch-phase`.
          //
          // NO BYPASS on this path. The gated latch closes half a period after
          // the enable latch did, so what gates its window -- and so what the
          // commit-at-closing-edge model must read -- is the enable latch's
          // HELD Q, captured when it shut. Its transparent arm is the value
          // passing through at the OTHER edge: reading it would let `en_i`
          // change mid-window and decide a commit the hardware had already
          // decided. (The enable latch is itself retyped to a flop committing
          // at its own closing edge, so its Q stays a valid read.)
          const bool                   window_level = !e.cc.rising;  // a latch committing at FALL is open while the clock is HIGH
          //
          // NESTED GATES. After resolve_icg flattens a gate of a gated clock,
          // the inner enable latch is `en_1p` with window `!(clk & en_q)`: shut
          // at clk=1 only while the OUTER enable `en_q` is on. That is the only
          // time this latch is open (its window needs every enable of the cone),
          // so "shut whenever this latch is open" holds pointwise -- the check
          // conditions on the cone's other enables being 1.
          std::vector<hhds::Pin_class> others;
          for (const auto& o : gate_enables) {
            if (!o.is_invalid() && o != en) {
              others.push_back(o);
            }
          }
          // ...and "open while this latch is shut" conditions the same way: an
          // enable latch ON the gated clock (`if (clock) en_2p = ...`, B08's
          // prim_mul_div with clock = clk & en_q) is open at clk=1 only while
          // the outer gate is on -- the only time this latch can commit.
          const bool held_for_window
              = e.is_latch
                && (lc::gated_latch_closed_at(dn, e.icg->clock, /*closed_level=*/window_level)
                    || lc::gated_latch_closed_assuming(dn, e.icg->clock, /*closed_level=*/window_level, others))
                && (lc::latch_open_at(dn, e.icg->clock, /*level=*/!window_level)
                    || lc::latch_open_assuming(dn, e.icg->clock, /*level=*/!window_level, others));
          if (opposite_for_other_edge && !opposite_here) {
            e.icg_hold = true;
          }
          if (!opposite_here && !opposite_for_other_edge && !held_for_window) {
            plan.code = "clock-gate-latch-phase";
            plan.why  = "state element `" + label_of(n) + "` is clocked through a gate whose enable latch `" + label_of(dn)
                       + "` is not transparent exactly on the phase opposite the gated edge";
            return plan;
          }
          if (held_for_window && !opposite_here) {
            ++e.icg_window_held;
            continue;  // the held Q, deliberately: see above
          }
          if (auto arm = lc::latch_transparent_arm(dn); !arm.is_invalid()) {
            en = arm;
          }
        }
      }
    }
    // A GATED LATCH is lowered to "commit at the closing edge iff the gate's
    // enable held", i.e. a gated-OFF clock HOLDS it. That is only the latch's
    // behavior when the gated-off clock keeps its window SHUT. A window open
    // while the gated clock is LOW (`if (!(clk & en_l)) p = d`, minion's
    // register-file preview latch) is transparent for the WHOLE period when
    // gated off, which no slot can express -- lowering it anyway PROVED it
    // equal to a latch that really holds. Decline -- unless every reader of the
    // latch is masked by that same gated clock (an ICG's own enable latch on a
    // gated clock), which makes the gated-off window unobservable.
    if (e.is_latch && e.icg && e.cc.role == lc::Net_role::Clock
        && !lc::gated_latch_closed_at(n, e.icg->clock, /*closed_level=*/e.cc.rising) && !lc::gated_latch_masked_off(n)) {
      plan.code = "gated-latch-open-when-off";
      plan.why  = "latch `" + label_of(n)
                  + "` has a gated clock window that stays transparent while the gate is off, which one commit edge "
                    "cannot represent";
      return plan;
    }
    plan.elems.push_back(e);
    if (cc->role == lc::Net_role::Clock) {
      ++per_net[cc->net_key()];
      example_on_net.try_emplace(cc->net_key(), label_of(n));
      // A clock that is ONE BIT of a multi-bit input: record WHICH bit. The
      // commit class is keyed by the whole input (control_root follows every
      // Get_mask to it), so clks[0] and clks[1] would otherwise share one slot
      // and one reference edge -- a flop moved between the bits normalized to
      // the identical graph. Only a DATA-role latch is exempt: its enable is
      // not a clock, it keeps that enable through the rewrite, and its slot
      // comes from the state the enable reads, never from this key.
      if (!cc->implicit_clock && !cc->net.is_invalid() && gu::is_graph_input_pin(cc->net) && gu::bits_of(cc->net) > 1) {
        const auto bit = clock_bit_on(control, cc->net, 0, 0);
        bus_bits[cc->net_key()].insert(bit ? std::to_string(*bit) : "?" + label_of(n));
        if (bit) {
          bus_bit_of[cc->net_key()] = *bit;
        }
      }
    }
  }
  // Decline only when the bits are AMBIGUOUS: two different bits of one input
  // (or a bit this walk cannot decide) clock the design. One bit, used alone,
  // is a single clock and normalizes like any other (the reference rebind below
  // selects that bit). Sorted for a deterministic diagnostic.
  {
    std::vector<std::string> keys;
    for (const auto& [k, bits] : bus_bits) {
      if (bits.size() > 1 || !bus_bit_of.contains(k)) {
        keys.push_back(k);
      }
    }
    std::sort(keys.begin(), keys.end());
    if (!keys.empty()) {
      std::string list;
      for (const auto& b : bus_bits[keys.front()]) {
        list += (list.empty() ? "" : ", ") + (b.front() == '?' ? "an undecidable bit (`" + b.substr(1) + "`)" : "bit " + b);
      }
      plan.code = "clock-bus-bit";
      plan.why  = "the multi-bit input `" + keys.front() + "` clocks state on " + list
                 + "; edge normalization keys a clock by its input net and cannot tell the bits of one bus apart";
      return plan;
    }
  }
  if (plan.elems.empty()) {
    plan.ok    = true;
    plan.slots = 1;
    return plan;
  }

  // 2. Reference clock = the clock net carrying the most state. Ties break on
  //    the KEY (a string), never on traversal order, so both miter sides agree.
  //    Under multi_clock a net whose state commits on the FALL needs the phase
  //    divider, and only the reference gets one, so such a net is preferred:
  //    counting cells alone made an 8-bit latch (8 cells) on a second clock
  //    outweigh the clock that carried the negedge state.
  absl::flat_hash_set<std::string> needs_divider;
  for (const auto& e : plan.elems) {
    if (e.cc.role == lc::Net_role::Clock && !e.cc.implicit_clock && !e.cc.rising) {
      needs_divider.insert(e.cc.net_key());
    }
  }
  std::string ref_net;
  int         best     = -1;
  bool        best_div = false;
  for (const auto& [k, cnt] : per_net) {
    const bool div = needs_divider.contains(k);
    if (std::tuple{div, cnt} > std::tuple{best_div, best} || (div == best_div && cnt == best && k < ref_net)) {
      best     = cnt;
      best_div = div;
      ref_net  = k;
    }
  }
  if (per_net.size() >= 2) {
    // A second clock DOMAIN. Its ratio to the reference is unknown here (a
    // free, testbench-driven clock genuinely has none), and slotting it anyway
    // would silently change what the design means. Decline: pass/lec's
    // detected-edge branch and sim's M6 model both still handle it honestly.
    std::vector<std::string> keys;
    for (const auto& [k, cnt] : per_net) {
      keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());  // deterministic: keys are structural
    std::string detail;
    for (size_t i = 0; i < keys.size(); ++i) {
      detail += std::format("{} `{}` ({} element(s), e.g. `{}`)",
                            i == 0 ? ": " : "; ",
                            keys[i],
                            per_net[keys[i]],
                            example_on_net[keys[i]]);
    }
    if (!multi_clock) {
      plan.code = "multi-clock-no-ratio";
      plan.why  = std::format("{} clock nets and no known integer ratio between them", per_net.size()) + detail;
      return plan;
    }
    // multi_clock: the heaviest root is the reference and is normalized exactly
    // as before. Every element on ANOTHER root is left the plain posedge flop it
    // is -- step 3 refuses anything else -- and the consumer reads its domain
    // from the certificate's clock table. Nothing here relates the domains: a
    // step of the Lean semantics says which clocks fire, and the reference
    // divider counts only its own clock's edges.
    plan.domains     = static_cast<int>(per_net.size());
    plan.domain_desc = detail;
  }
  // The reference clock is the RESOLVED ROOT of the class, not the raw
  // clock_pin driver. They differ whenever the cone carries an inversion or a
  // width mask (`always @(posedge ~clk)` arrives as `And(Not(Get_mask(clk)),1)`)
  // -- and the root is what every normalized element gets rebound to below, so
  // taking the raw driver would propagate the derived cone into the output.
  plan.ref_net        = ref_net;
  plan.ref_clock_name = ref_net;
  plan.ref_clk_pin    = {};
  if (auto it = bus_bit_of.find(ref_net); it != bus_bit_of.end()) {
    plan.ref_clk_bit = it->second;
    if (it->second != 0) {
      plan.ref_clock_name += "[" + std::to_string(it->second) + "]";
    }
  }
  for (const auto& e : plan.elems) {
    // A LATCH counts as a candidate too, as long as its role is Clock -- then its
    // `net` IS the clock root (a data-gated latch is Net_role::Data and is
    // excluded by the same test). Skipping latches here left a design whose only
    // state is clock-gated latches with NO reference clock at all, so the retyped
    // flops came out clockless and cgen emitted `always @(posedge 'hx)`.
    if (e.cc.role == lc::Net_role::Clock && !e.cc.implicit_clock && e.cc.net_key() == ref_net) {
      plan.ref_clk_pin = e.cc.net;
      break;
    }
  }

  // 3. Slot per class key. Clock-role classes on the reference net take their
  //    slot from the EDGE; a data-gated latch inherits the slot of the state
  //    its enable depends on.
  absl::flat_hash_map<hhds::Class_index, int> slot_of_node;   // for the data pass below
  absl::flat_hash_set<hhds::Class_index>      off_ref_nodes;  // multi_clock: state left on a non-reference root
  for (auto& e : plan.elems) {
    if (e.cc.role != lc::Net_role::Clock) {
      continue;
    }
    if (e.cc.net_key() != ref_net) {
      if (!multi_clock) {
        plan.code = "off-reference-clock";
        plan.why  = "state element `" + label_of(e.node) + "` commits on a net that is not the reference clock";
        return plan;
      }
      // A second domain is EXPORTED, not divided: everything on it must commit
      // on its clock's RISE. A posedge flop is left as it is. A latch that
      // CLOSES on the rise (open while its clock is low -- a gate's enable
      // latch, CORE-ET B05's `u_noc_clk_gate.en_latch`) is lowered exactly as
      // the reference domain lowers one at P=1: retyped to a posedge flop on
      // its own root (commit at the closing edge), with rule 4 and the
      // cross-domain read rule below as its preconditions. A negedge element or
      // a latch closing on the FALL would need a phase divider of its own --
      // the reference divider counts the wrong clock -- so it is refused, never
      // left as a partial lowering.
      if (!e.cc.rising) {
        plan.code = "off-reference-clock";
        plan.why  = std::format(
            "{} `{}` commits on `{}` ({}), a clock domain other than the reference `{}`; off the "
             "reference clock only state committing on the rise is supported (a second domain "
             "would need its own phase divider)",
            e.is_latch ? "latch" : "negedge flop",
            label_of(e.node),
            e.cc.net_key(),
            e.cc.rising ? "rise" : "fall",
            ref_net);
        return plan;
      }
      e.off_ref = true;
      e.slot    = 0;  // unused: it takes no slot gate
      off_ref_nodes.insert(e.node.get_class_index());
      continue;
    }
    e.slot                                 = e.cc.rising ? 0 : 1;
    plan.slot_of_key[e.cc.key()]           = e.slot;
    slot_of_node[e.node.get_class_index()] = e.slot;
  }
  for (auto& e : plan.elems) {
    if (e.cc.role == lc::Net_role::Clock) {
      continue;
    }
    // A data-gated window that still reads a CLOCK is not data: lowering it
    // keeps the clock as data in the enable, a silent full-cycle error.
    // Decline instead (B08's rule: refuse the shape whenever its conditions
    // cannot be checked). Measured: `lhd_abc_latch_test_usyn`'s mapped
    // `abc_latch_gated`, whose gate is an AND2 cell inside a wrapper module,
    // normalized this way and REFUTED against its correctly normalized RTL.
    if (const auto ck = window_clock_read(sink_driver(e.node, "enable"), clocks); !ck.is_invalid()) {
      plan.code = "latch-window-reads-clock";
      plan.why  = "latch `" + label_of(e.node) + "` has a window that reads clock `" + std::string{gu::pin_name_of(ck)}
                 + "` through logic this pass cannot resolve to a clock phase or a gate, so its lowered enable would "
                   "read the clock as data";
      return plan;
    }
    // Data-gated: the window closes when the enable deasserts, and the enable
    // is a function of state, so it closes on THAT state's commit edge.
    absl::flat_hash_set<hhds::Class_index> hit;
    comb_state_reach(sink_driver(e.node, "enable"), hit);
    absl::flat_hash_set<int> slots;
    for (const auto& ix : hit) {
      // Its closing edge would be ANOTHER domain's edge, which the reference
      // divider cannot express.
      if (off_ref_nodes.contains(ix)) {
        plan.code = "off-reference-clock";
        plan.why  = "latch `" + label_of(e.node)
                   + "` has an enable driven by state in a clock domain other than the reference, so its closing "
                     "edge is not a slot of the reference clock";
        return plan;
      }
      if (auto it = slot_of_node.find(ix); it != slot_of_node.end()) {
        slots.insert(it->second);
      }
    }
    if (slots.size() > 1) {
      plan.code = "ambiguous-data-gate";
      plan.why  = "latch `" + label_of(e.node)
                 + "` has an enable driven by state in more than one commit class, so its closing edge is ambiguous";
      return plan;
    }
    e.slot                       = slots.empty() ? 0 : *slots.begin();
    plan.slot_of_key[e.cc.key()] = e.slot;
    // NOT written back into slot_of_node. That map holds CLOCK-role slots only,
    // so a data-gated latch's slot is a function of clock-role state alone --
    // order-independent by construction. Writing data-gated results back made
    // the slot of a latch whose enable depends on ANOTHER data-gated latch
    // depend on body().nodes() iteration order, and the task page's condition 2
    // is binding: slots are derived structurally, never from traversal order,
    // because the two miter sides run this independently and a disagreement is
    // a false REFUTED.
  }

  // CROSS-DOMAIN LATCH READS. A second-domain element samples on ITS clock,
  // whose edges are unrelated to the reference: one can fall while a
  // reference-domain latch is transparent, and then the hardware reads the
  // latch's D while the commit-at-closing-edge model gives the held Q. Rule 4
  // catches this within one domain (same commit class); across domains there is
  // no shared edge to compare, so the only sound answer is to refuse the read.
  if (!off_ref_nodes.empty()) {
    absl::flat_hash_set<hhds::Class_index> ref_latches;
    for (const auto& e : plan.elems) {
      if (e.is_latch && !e.off_ref) {
        ref_latches.insert(e.node.get_class_index());
      }
    }
    if (!ref_latches.empty()) {
      const auto reads_ref_latch = [&](const hhds::Pin_class& p) -> std::optional<hhds::Class_index> {
        absl::flat_hash_set<hhds::Class_index> hit;
        comb_state_reach(p, hit);
        for (const auto& ix : hit) {
          if (ref_latches.contains(ix)) {
            return ix;
          }
        }
        return std::nullopt;
      };
      for (const auto& e : plan.elems) {
        if (!e.off_ref) {
          continue;
        }
        std::vector<hhds::Pin_class> cones{sink_driver(e.node, "din"), sink_driver(e.node, "enable")};
        if (e.icg) {
          cones.insert(cones.end(), e.icg->enables.begin(), e.icg->enables.end());
        }
        for (const auto& c : cones) {
          if (c.is_invalid()) {
            continue;
          }
          if (auto ix = reads_ref_latch(c)) {
            std::string latch_name = "?";
            for (const auto& l : plan.elems) {
              if (l.node.get_class_index() == *ix) {
                latch_name = label_of(l.node);
              }
            }
            plan.code = "cross-domain-latch-read";
            plan.why  = std::format(
                "state element `{}` on clock `{}` reads latch `{}` of the reference domain `{}` "
                 "combinationally; an edge of `{}` can fall inside that latch's window, where the "
                 "hardware reads its D and the commit-at-closing-edge model its held Q",
                label_of(e.node),
                e.cc.net_key(),
                latch_name,
                ref_net,
                e.cc.net_key());
            return plan;
          }
        }
      }
      // ...and every memory write port clocked off the reference.
      if (g != nullptr) {
        for (auto n : g->body().nodes()) {
          if (gu::type_op_of(n) != Ntype_op::Memory) {
            continue;
          }
          const auto ms = mem_shape_of(n);
          if (!ms.parsed) {
            continue;  // normalize() refuses an unparsed memory by name before any use
          }
          for (const auto& mp : ms.ports) {
            if (mp.rdport || mp.clock.is_invalid()) {
              continue;
            }
            const auto mcc = lc::commit_class_of_clock(mp.clock, ms.rising, &clocks);
            if (!mcc || mcc->implicit_clock || mcc->net_key() == ref_net) {
              continue;
            }
            for (const char* field : {"addr", "din", "enable"}) {
              if (auto ix = reads_ref_latch(mem_port_driver(n, field, mp.port))) {
                plan.code = "cross-domain-latch-read";
                plan.why  = std::format(
                    "memory `{}` write port {} on clock `{}` reads a latch of the reference "
                     "domain `{}` combinationally (through `{}`)",
                    label_of(n),
                    mp.port,
                    mcc->net_key(),
                    ref_net,
                    field);
                return plan;
              }
            }
          }
        }
      }
    }
  }

  // ...and the REVERSE: a latch left on a second domain, read combinationally by
  // state of ANOTHER domain. An edge of that domain can fall inside the latch's
  // window, where the hardware reads its D and the model its held Q -- the same
  // hazard, from the other side. Within its own domain rule 4 decides.
  {
    absl::flat_hash_map<hhds::Class_index, std::string> off_latch_root;
    for (const auto& e : plan.elems) {
      if (e.is_latch && e.off_ref) {
        off_latch_root.emplace(e.node.get_class_index(), e.cc.net_key());
      }
    }
    if (!off_latch_root.empty()) {
      const auto reads_other = [&](const hhds::Pin_class& p, const std::string& my_root) -> const hhds::Node_class* {
        absl::flat_hash_set<hhds::Class_index> hit;
        comb_state_reach(p, hit);
        for (const auto& l : plan.elems) {
          if (hit.contains(l.node.get_class_index())) {
            if (auto it = off_latch_root.find(l.node.get_class_index()); it != off_latch_root.end() && it->second != my_root) {
              return &l.node;
            }
          }
        }
        return nullptr;
      };
      for (const auto& e : plan.elems) {
        const std::string            my_root = e.off_ref ? e.cc.net_key() : ref_net;
        std::vector<hhds::Pin_class> cones{sink_driver(e.node, "din"), sink_driver(e.node, "enable")};
        if (e.icg) {
          cones.insert(cones.end(), e.icg->enables.begin(), e.icg->enables.end());
        }
        for (const auto& c : cones) {
          if (c.is_invalid()) {
            continue;
          }
          if (const auto* l = reads_other(c, my_root)) {
            plan.code = "cross-domain-latch-read";
            plan.why  = std::format(
                "state element `{}` on clock `{}` reads latch `{}` of the domain `{}` "
                 "combinationally; an edge of `{}` can fall inside that latch's window, where the "
                 "hardware reads its D and the commit-at-closing-edge model its held Q",
                label_of(e.node),
                my_root,
                label_of(*l),
                off_latch_root.at(l->get_class_index()),
                my_root);
            return plan;
          }
        }
      }
      if (g != nullptr) {
        for (auto n : g->body().nodes()) {
          if (gu::type_op_of(n) != Ntype_op::Memory) {
            continue;
          }
          const auto ms = mem_shape_of(n);
          if (!ms.parsed) {
            continue;
          }
          for (const auto& mp : ms.ports) {
            if (mp.rdport) {
              continue;
            }
            const auto        mcc = mp.clock.is_invalid() ? std::nullopt : lc::commit_class_of_clock(mp.clock, ms.rising, &clocks);
            const std::string my_root = (!mcc || mcc->implicit_clock) ? ref_net : mcc->net_key();
            for (const char* field : {"addr", "din", "enable"}) {
              if (const auto* l = reads_other(mem_port_driver(n, field, mp.port), my_root)) {
                plan.code = "cross-domain-latch-read";
                plan.why  = std::format(
                    "memory `{}` write port {} on clock `{}` reads latch `{}` of the domain `{}` "
                     "combinationally (through `{}`)",
                    label_of(n),
                    mp.port,
                    my_root,
                    label_of(*l),
                    off_latch_root.at(l->get_class_index()),
                    field);
                return plan;
              }
            }
          }
        }
      }
    }
  }

  int max_slot = 0;
  for (const auto& e : plan.elems) {
    max_slot = std::max(max_slot, e.slot);
  }
  plan.slots = max_slot + 1;
  plan.ok    = true;
  return plan;
}

// Rule 4 (the US7624363 L1 case), as a PRECONDITION of the lowering rather
// than an optimization. A latch is transparent right up to its closing edge, so
// anything that commits at that SAME instant reads the latch's D, not its held
// Q. The flop-with-enable model gives it the held Q instead: a persistent
// FULL-CYCLE error, silent in Verilog and invisible to a symmetric gate.
//
// Slotting does not fix this — it REPRODUCES it, because coincident classes
// share a slot by construction. So refuse (M3 rule 4's "explicit rejection"
// arm); rewriting the latch to a buffer is the other legal answer and is left
// for when a real design needs it.
// Decision 3A's guard: which same-edge (latch, reader) pairs are FORWARDED
// instead of refused. The latch is lowered by the slot model (clock-role, on
// the reference); the reader is a FLOP there too, reading the latch through a
// combinational path (comb_state_reach does not enter instances or memories,
// so a hazard it reports has a path the forwarding can clone). A latch reader,
// a memory port, or a second-domain element keeps the refusal.
bool rule4_forwardable(const Element& l, const Element& s) {
  return l.is_latch && l.cc.role == lc::Net_role::Clock && !l.off_ref && !s.is_latch && !s.off_ref
         && gu::type_op_of(s.node) == Ntype_op::Flop;
}

bool check_rule4(Plan& plan, hhds::Graph* g, const lc::Design_clocks& clocks, bool quiet, bool forward) {
  bool ok = true;
  // MEMORY WRITE PORTS ARE READERS TOO.
  //
  // `plan.elems` holds flops and latches, so when memories were refused
  // outright they could not be on either side of this rule.  Now that a write
  // port is admitted and slotted it reads `addr`, `din` and `enable`
  // combinationally, and if one of those cones reaches a latch that closes on
  // the SAME edge the port commits on, it is the identical L1 hazard: the real
  // hardware samples the latch's D, the lowering gives it the held Q.  Leaving
  // Memory out of the candidate list would bypass rule 4 exactly where the new
  // support created the exposure.
  struct Mem_reader {
    hhds::Node_class             node;
    lc::Commit_class             cc;
    uint32_t                     port = 0;
    // A GATED write clock is folded into this port's write enable, so the
    // gate's own enable cone becomes a COMBINATIONAL input to the commit --
    // exactly like `addr`/`din`/`enable`, and exposed to the same L1 hazard.
    // Rule 4 runs before the memory plan exists, so the cone is resolved here
    // rather than read off the plan.
    std::vector<hhds::Pin_class> gate_cones;
  };
  std::vector<Mem_reader> mem_readers;
  if (g != nullptr) {
    for (auto n : g->body().nodes()) {
      if (gu::type_op_of(n) != Ntype_op::Memory) {
        continue;
      }
      const auto ms = mem_shape_of(n);
      if (!ms.parsed || mem_is_immutable_rom(ms)) {
        continue;
      }
      for (const auto& mp : ms.ports) {
        if (mp.rdport) {
          continue;
        }
        const auto clk_pin = mp.clock.is_invalid() ? plan.ref_clk_pin : mp.clock;
        auto       cc      = lc::commit_class_of_clock(clk_pin, ms.rising, &clocks);
        if (cc && cc->role == lc::Net_role::Clock) {
          std::vector<hhds::Pin_class> cones;
          if (!clk_pin.is_invalid()) {
            if (const auto icg = lc::resolve_icg(clk_pin, clocks)) {
              for (auto en : icg->enables) {
                // The same arm the fold will use: the bypass is what the port
                // actually reads, so that is the cone to walk.
                if (!en.is_invalid() && !en.is_const() && !gu::is_graph_input_pin(en)) {
                  if (auto dn = en.get_master_node(); gu::type_op_of(dn) == Ntype_op::Latch) {
                    if (auto arm = lc::latch_transparent_arm(dn); !arm.is_invalid()) {
                      en = arm;
                    }
                  }
                }
                if (!en.is_invalid()) {
                  cones.push_back(en);
                }
              }
            }
          }
          mem_readers.push_back({n, *cc, mp.port, std::move(cones)});
        }
      }
    }
  }
  absl::flat_hash_map<hhds::Class_index, absl::flat_hash_set<hhds::Class_index>> reach_of;
  for (const auto& l : plan.elems) {
    if (!l.is_latch) {
      continue;
    }
    auto q = l.node.get_driver_pin(0);
    if (q.is_invalid()) {
      continue;
    }
    for (const auto& s : plan.elems) {
      // Keyed on the COMMIT CLASS (net AND edge), not on the slot. Two elements
      // can share a slot without sharing an edge — a data-gated latch defaults
      // to slot 0 alongside the posedge flops, and that pairing is the ordinary
      // flop-with-enable abstraction the whole task is scoped to (it is what
      // `lhd sim` and cgen already ship). The L1 hazard is narrower: the latch
      // closes on the SAME edge of the SAME net that the reader samples on.
      if (s.node.get_class_index() == l.node.get_class_index() || s.cc.key() != l.cc.key()) {
        continue;
      }
      // ONE reach per reader, not one per (latch, reader) pair: B08 has 1587
      // latches against ~2400 elements, and recomputing the reader's cone for
      // every latch was most of a 270 s run.
      auto reach = reach_of.find(s.node.get_class_index());
      if (reach == reach_of.end()) {
        absl::flat_hash_set<hhds::Class_index> hit;
        // A CLOCK-gated latch reader samples its TRANSPARENT arm: the rewrite
        // peels its hold mux, whose selector is the gate (`Ror(clk & en_l) ? 0 :
        // d`), so walking the raw din would charge it with reading the gate's
        // own enable latch -- a read the lowering does not make. Same arm the
        // fold uses, as for the memory gate cones above.
        auto                                   s_din = sink_driver(s.node, "din");
        if (s.is_latch && s.cc.role == lc::Net_role::Clock) {
          if (const auto hp = lc::latch_hold_path(s.node)) {
            s_din = hp->value;  // a bit-blasted wide latch: its bit of the open arm
          } else if (auto arm = lc::latch_transparent_arm(s.node); !arm.is_invalid()) {
            s_din = arm;
          }
        }
        comb_state_reach(s_din, hit);
        reach = reach_of.emplace(s.node.get_class_index(), std::move(hit)).first;
      }
      if (!reach->second.contains(l.node.get_class_index())) {
        continue;
      }
      if (forward && rule4_forwardable(l, s)) {
        plan.forwards.emplace_back(l.node, s.node);
        continue;  // 3A: forwarded, see apply_reader_forwards
      }
      ok = false;
      if (quiet) {
        // A PROBE (dry run) must stay silent: under `formal.phase_sched` a
        // decline is not a failure -- the caller hands the design to the formal
        // phase schedule instead -- and an error-severity diagnostic here would
        // fail an otherwise clean, PROVEN run through the CLI's error count.
        return false;
      }
      livehd::diag::err(kPass, "coincident-commit-edge", "unsupported")
          .msg("latch `{}` and state element `{}` commit at the SAME edge, and `{}` reads the latch combinationally",
               label_of(l.node),
               label_of(s.node),
               label_of(s.node))
          .hint(
              "the latch is still transparent at that edge, so the real hardware samples its D while the "
              "commit-at-closing-edge model samples its held Q — a persistent full-cycle error. Move the two onto "
              "opposite phases (a master/slave pair), or replace the latch with a buffer if it is meant to be one")
          .emit();
      break;
    }
    // ...and the same test against every admitted memory write port.
    for (const auto& mr : mem_readers) {
      if (mr.cc.key() != l.cc.key()) {
        continue;
      }
      absl::flat_hash_set<hhds::Class_index> hit;
      for (const char* field : {"addr", "din", "enable"}) {
        comb_state_reach(mem_port_driver(mr.node, field, mr.port), hit);
      }
      for (const auto& gc : mr.gate_cones) {
        comb_state_reach(gc, hit);  // the folded gate is read combinationally too
      }
      if (!hit.contains(l.node.get_class_index())) {
        continue;
      }
      ok = false;
      if (quiet) {
        return false;
      }
      livehd::diag::err(kPass, "coincident-commit-edge", "unsupported")
          .msg(
              "latch `{}` and memory `{}` write port {} commit at the SAME edge, and the port reads the latch "
              "combinationally",
              label_of(l.node),
              label_of(mr.node),
              mr.port)
          .hint(
              "the latch is still transparent at that edge, so the real hardware writes its D while the "
              "commit-at-closing-edge model writes its held Q. Move the latch and the memory onto opposite phases, "
              "or replace the latch with a buffer if it is meant to be one")
          .emit();
      break;
    }
  }
  return ok;
}

// `posclk` known-false on a LATCH means the enable is active LOW: transparent
// while enable == 0. A `Flop` has no polarity pin with that meaning — pid 6 on
// a Flop is the CLOCK EDGE — so carrying the pin across the retype would read
// back as "negedge flop" and silently invert the design's timing. That is the
// double-negation miscompile this whole task exists to prevent.
//
// The sound lowering is to move the polarity into the enable CONE, where the
// Pyrope/slang shapes already carry it: enable := !enable, then drop the pin.
// This is NOT the refused "bare polarity-pin flip": that flip left `din`'s hold
// mux keyed on the UN-inverted condition, so the latch wrote itself forever.
// Here the enable and the din keep the SAME condition — only the test is
// inverted — which is exactly what cgen_verilog emits for this cell
// (`neg_en` -> `if (!en)`), so the two agree by construction.
bool latch_is_active_low(const hhds::Node_class& latch) {
  auto pc = sink_driver(latch, "posclk");
  return pc.is_known_false();
}

// True when `din` carries tolg's hold mux (`cond ? d : q`), i.e. a Mux on din
// with this latch's own Q as an arm. An active-LOW latch that ALSO has one is a
// shape no emitter produces: the hold would be keyed on the un-inverted
// condition while the enable test is inverted, so the two disagree. Refuse it.
bool has_hold_mux(const hhds::Node_class& latch) {
  auto q   = latch.get_driver_pin(0);
  auto din = sink_driver(latch, "din");
  if (q.is_invalid() || din.is_invalid() || din.is_const() || gu::is_graph_input_pin(din)) {
    return false;
  }
  auto n = din.get_master_node();
  if (gu::type_op_of(n) != Ntype_op::Mux) {
    return false;
  }
  for (auto sink : n.inp_sorted_pins()) {
    const auto drv = sink.get_driver_pin();
    if (!drv.is_invalid() && drv.get_class_index() == q.get_class_index()) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool needed(hhds::Graph* g, const std::vector<hhds::Graph*>& defs) {
  if (g != nullptr && lc::needs_single_edge(g).needed) {
    return true;
  }
  for (auto* d : defs) {
    if (d != nullptr && lc::needs_single_edge(d).needed) {
      return true;
    }
  }
  return false;
}

// DECISION 3A: GUARDED READER FORWARDING. Rule 4's hazard -- a flop committing
// on the very edge a latch it reads closes on -- is that the hardware flop
// samples the latch while it is still TRANSPARENT (its D), and the
// commit-at-closing-edge model gives it the held Q. CORE-ET B08's register file
// write-data delay flop (`always_ff @(negedge rf_clk_i)`) reads the write-back
// mux of CSR and multiplier latches that close on that same fall.
//
// The forwarded value is the latch's OUTPUT AT THE EDGE, built once per latch
// from its lowered controls: `reset ? RV : (E ? D : Q)` -- E its final commit
// enable (gate enables and slot predicate), D its open value, Q the held value.
// It replaces the latch's Q in the hazardous READER'S cone only: the
// combinational nodes between those latches and that reader's din/enable are
// cloned (attributes carried), so every other reader -- including every reader
// on another edge, for which the held Q is right -- keeps reading Q. Not a
// latch-to-wire replacement.
void copy_width_sign(const hhds::Pin_class& from, const hhds::Pin_class& to) {
  gu::set_bits(to, gu::bits_of(from));
  if (gu::is_unsign(from)) {
    gu::set_unsign(to);
  } else {
    gu::set_sign(to);
  }
}

int apply_reader_forwards(hhds::Graph& g, const std::vector<std::pair<hhds::Node_class, hhds::Node_class>>& fwd) {
  absl::flat_hash_map<hhds::Class_index, hhds::Pin_class> forwarded;  // latch node -> its value at the edge
  const auto                                              value_at_edge = [&](const hhds::Node_class& l) -> hhds::Pin_class {
    if (auto it = forwarded.find(l.get_class_index()); it != forwarded.end()) {
      return it->second;
    }
    const auto q = l.get_driver_pin(0);
    const auto mux = [&](const hhds::Pin_class& sel, const hhds::Pin_class& if0, const hhds::Pin_class& if1) {
      auto m = gu::create_typed_node(g, Ntype_op::Mux);
      sel.connect_sink(livehd::graph_util::setup_sink_pid(m, 0));
      if0.connect_sink(livehd::graph_util::setup_sink_pid(m, 1));
      if1.connect_sink(livehd::graph_util::setup_sink_pid(m, 2));
      auto o = m.create_driver_pin(0);
      copy_width_sign(q, o);  // the latch's width and sign, NOT its name
      return o;
    };
    hhds::Pin_class v = sink_driver(l, "din");
    if (const auto en = sink_driver(l, "enable"); !en.is_invalid()) {
      v = mux(en, q, v);
    }
    if (const auto rst = sink_driver(l, "reset_pin"); !rst.is_invalid()) {
      auto rv = sink_driver(l, "initial");
      if (rv.is_invalid()) {
        rv = gu::create_const(g, *Dlop::create_integer(0));
      }
      const auto neg = sink_driver(l, "negreset");
      v = (neg.is_const() && !neg.is_known_false()) ? mux(rst, rv, v) : mux(rst, v, rv);
    }
    forwarded.emplace(l.get_class_index(), v);
    return v;
  };

  absl::flat_hash_map<hhds::Class_index, std::vector<hhds::Node_class>> latches_of;
  std::vector<hhds::Node_class>                                         readers;
  for (const auto& [l, s] : fwd) {
    auto& v = latches_of[s.get_class_index()];
    if (v.empty()) {
      readers.push_back(s);
    }
    v.push_back(l);
  }
  int rewired = 0;
  for (const auto& s : readers) {
    absl::flat_hash_map<hhds::Class_index, hhds::Node_class> latch_of_q;  // Q pin -> latch
    for (const auto& l : latches_of.at(s.get_class_index())) {
      latch_of_q.emplace(l.get_driver_pin(0).get_class_index(), l);
    }
    absl::flat_hash_map<hhds::Class_index, bool> dep;  // node -> reaches a forwarded Q combinationally
    std::function<bool(const hhds::Pin_class&)>  depends = [&](const hhds::Pin_class& p) -> bool {
      if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p)) {
        return false;
      }
      if (latch_of_q.contains(p.get_class_index())) {
        return true;
      }
      const auto n = p.get_master_node();
      if (gu::is_type_register(n) || gu::type_op_of(n) == Ntype_op::Sub) {
        return false;
      }
      if (auto it = dep.find(n.get_class_index()); it != dep.end()) {
        return it->second;
      }
      dep[n.get_class_index()] = false;  // a DAG; a cycle here would be a comb loop
      bool d                   = false;
      for (auto sink : n.inp_sorted_pins()) {
        for (const auto& drv : sink.get_driver_pins()) {
          d = depends(drv) || d;
        }
      }
      dep[n.get_class_index()] = d;
      return d;
    };
    absl::flat_hash_map<hhds::Class_index, hhds::Node_class> clone_of;
    std::function<hhds::Pin_class(const hhds::Pin_class&)>   rebuild = [&](const hhds::Pin_class& p) -> hhds::Pin_class {
      if (auto it = latch_of_q.find(p.get_class_index()); it != latch_of_q.end()) {
        return value_at_edge(it->second);
      }
      if (!depends(p)) {
        return p;
      }
      const auto       n = p.get_master_node();
      // BY VALUE: the recursion below inserts into clone_of, which invalidates
      // any iterator into it (measured: a dangling one returned the forwarded
      // mux in place of the clone).
      hhds::Node_class neo;
      if (auto c = clone_of.find(n.get_class_index()); c != clone_of.end()) {
        neo = c->second;
      } else {
        neo = gu::create_typed_node(g, gu::type_op_of(n));
        // Semantics only: inputs (constants included) and each output's width
        // and sign. NO names -- a second driver named like the original makes
        // name-keyed resolution conflate the two (measured: the clone's output
        // was replaced by the forwarded latch value on save). A LUT's table is
        // the one semantic node attribute.
        livehd::graph_util::carry_attr<livehd::attrs::lut_t>(n, neo);
        clone_of.emplace(n.get_class_index(), neo);
        // Recursion and wiring the clone mutate the original's graph body.
        for (auto sink : n.inp_pins_snapshot()) {
          const auto pid = static_cast<int>(sink.get_port_id());
          for (const auto& drv : sink.get_driver_pins()) {
            rebuild(drv).connect_sink(livehd::graph_util::setup_sink_pid(neo, pid));
          }
        }
        for (auto dp : n.out_pins_snapshot()) {
          copy_width_sign(dp, neo.create_driver_pin(dp.get_port_id()));
        }
      }
      return neo.create_driver_pin(p.get_port_id());
    };
    for (const char* pin : {"din", "enable"}) {
      const auto drv = sink_driver(s, pin);
      if (drv.is_invalid() || (!latch_of_q.contains(drv.get_class_index()) && !depends(drv))) {
        continue;
      }
      const auto neo = rebuild(drv);
      drop_sink(s, pin);
      neo.connect_sink(gu::setup_sink_by_name(s, pin));
      ++rewired;
    }
  }
  return rewired;
}

// ---------------------------------------------------------------------------
// DATA-gated latches: the open value
//
// A data-gated latch -- PicoRV32's `always @*` case on mem_wordsize, which
// leaves mem_rdata_word unassigned on an unreachable arm -- is retyped into a
// flop with the latch's enable: that flop is the value the latch HOLDS. But the
// latch is level-sensitive: while its enable holds it passes D straight
// through, so a reader in the same cycle (the posedge flop that latches a load,
// an output, combinational logic) sees D, not the held Q. Leaving the readers on
// Q is the full-cycle error rule 4 names for a same-edge pair, and rule 4 never
// sees it here: a data-gated latch and the posedge flops reading it are in
// different commit classes. Measured on PicoRV32 P0: every load returned the
// previous load's data.
//
// So every reader moves to the open value `reset ? RV : (E ? D : Q)`, exactly
// apply_reader_forwards' value at the edge, built from the latch's own enable
// (after the active-low lowering, before any slot gate: transparency does not
// depend on the phase). Exempt are the latch itself and the nodes in its own
// din/enable/reset cone: that is where the hold arm reads Q, and moving such a
// read onto the open value would close a combinational loop.
absl::flat_hash_set<hhds::Class_index> latch_own_cone(const hhds::Node_class& l) {
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Pin_class>           work;
  for (const char* pin : {"din", "enable", "reset_pin", "negreset", "initial"}) {
    if (auto d = sink_driver(l, pin); !d.is_invalid()) {
      work.push_back(d);
    }
  }
  while (!work.empty()) {
    auto p = work.back();
    work.pop_back();
    if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p)) {
      continue;
    }
    const auto n = p.get_master_node();
    if (gu::is_type_register(n) || gu::type_op_of(n) == Ntype_op::Sub) {
      continue;
    }
    if (!seen.insert(n.get_class_index()).second) {
      continue;
    }
    for (auto sink : n.inp_sorted_pins()) {
      for (const auto& drv : sink.get_driver_pins()) {
        work.push_back(drv);
      }
    }
  }
  return seen;
}

// Returns the number of reader inputs moved onto the open value.
int make_data_latch_transparent(hhds::Graph& g, const hhds::Node_class& l) {
  const auto q = l.get_driver_pin(0);
  if (q.is_invalid()) {
    return 0;
  }
  const auto cone = latch_own_cone(l);
  absl::flat_hash_set<hhds::Class_index> made;  // the muxes built here read Q
  const auto mux = [&](const hhds::Pin_class& sel, const hhds::Pin_class& if0, const hhds::Pin_class& if1) {
    auto m = gu::create_typed_node(g, Ntype_op::Mux);
    made.insert(m.get_class_index());
    sel.connect_sink(livehd::graph_util::setup_sink_pid(m, 0));
    if0.connect_sink(livehd::graph_util::setup_sink_pid(m, 1));
    if1.connect_sink(livehd::graph_util::setup_sink_pid(m, 2));
    auto o = m.create_driver_pin(0);
    copy_width_sign(q, o);  // the latch's width and sign, NOT its name
    return o;
  };
  const auto din = sink_driver(l, "din");
  if (din.is_invalid()) {
    return 0;  // no data: nothing passes through
  }
  hhds::Pin_class v = din;
  if (const auto en = sink_driver(l, "enable"); !en.is_invalid()) {
    v = mux(en, q, v);
  }
  if (const auto rst = sink_driver(l, "reset_pin"); !rst.is_invalid()) {
    auto rv = sink_driver(l, "initial");
    if (rv.is_invalid()) {
      rv = gu::create_const(g, *Dlop::create_integer(0));
    }
    const auto neg = sink_driver(l, "negreset");
    v = (neg.is_const() && !neg.is_known_false()) ? mux(rst, rv, v) : mux(rst, v, rv);
  }
  if (v.get_class_index() == q.get_class_index()) {
    return 0;
  }
  // Snapshot first: out_edges() is a view over live storage.
  std::vector<hhds::Pin_class> readers;
  for (const auto& e : q.out_edges()) {
    const auto rn = e.sink.get_master_node();
    if (rn.get_class_index() == l.get_class_index() || made.contains(rn.get_class_index())
        || cone.contains(rn.get_class_index())) {
      continue;
    }
    readers.push_back(e.sink);
  }
  // Delete the EDGE, keep the sink pin (graph/clock_gate.cpp's redirect_readers): deleting
  // the pin and reconnecting renumbers a banked operand, which broke a Concat's lane table.
  for (const auto& sink : readers) {
    sink.del_sink(q);
    v.connect_sink(sink);
  }
  return static_cast<int>(readers.size());
}

Result normalize(hhds::Graph* g, const std::vector<hhds::Graph*>& defs, const Options& opts) {
  Result     r;
  const bool quiet = opts.quiet || opts.dry_run;
  if (g == nullptr) {
    return r;
  }

  const lc::Design_clocks           clocks(g);
  const auto                        need = lc::needs_single_edge(g, &clocks);
  // A def only forces our hand when it holds something the ENGINES GET WRONG --
  // a Latch (the encoder refuses the cell) or a negedge flop (it is blind to
  // the edge). Keying this on the full trigger instead swept in ">= 2 clock
  // nets", so any hierarchical design with a second clock net ANYWHERE in its
  // library -- or any `--lib` cell model that happens to have one -- refused
  // every run, including designs that instantiate none of it.
  // Only defs this design actually INSTANTIATES matter. Scanning every graph the
  // caller handed us refused on library modules the design never touches -- and
  // it also went stale the moment a clock-gate cell was inlined, because
  // inline_sub deliberately leaves the def in the library for its other
  // instantiation sites. Walk `g`'s own instance tree instead.
  absl::flat_hash_set<hhds::Graph*> reachable;
  {
    std::vector<hhds::Graph*> work{g};
    while (!work.empty()) {
      auto* cur = work.back();
      work.pop_back();
      for (auto n : cur->body().nodes()) {
        if (gu::type_op_of(n) != Ntype_op::Sub) {
          continue;
        }
        if (auto sub = n.get_subnode_graph(); sub && reachable.insert(sub.get()).second) {
          work.push_back(sub.get());
        }
      }
    }
  }
  // `defs` is the caller's ALLOW-LIST: it already excludes defs the user
  // COLLAPSED or TRUSTED, which are blackboxes whose contents are assumed equal
  // and never encoded -- `formal.lec.trust` is the documented escape hatch for
  // exactly "the encoder cannot model this leaf", and refusing on its contents
  // would pre-empt the mechanism that makes such a design provable at all.
  // So consider a def only when it is BOTH instantiated and allowed.
  absl::flat_hash_set<hhds::Graph*> allowed(defs.begin(), defs.end());
  bool                              def_needs = false;
  std::string                       def_why;  // which def, and WHAT is in it (the diagnostic must say)
  for (auto* d : reachable) {
    if (d == nullptr || !allowed.contains(d)) {
      continue;
    }
    const auto dn = lc::needs_single_edge(d);
    if (dn.n_latches > 0 || dn.n_negedge_flops > 0) {
      def_needs = true;
      def_why   = std::format("def `{}` holds {}", d->get_name(), dn.why);
      break;
    }
  }
  // What the pass OWES a lowering for: a Latch (the encoder refuses the cell)
  // and a negedge flop (the encoder is blind to the edge). A design that merely
  // has two clock nets is already handled honestly downstream — pass/lec's
  // detected-edge branch REFUTES two-clock-vs-one-clock today — so declining it
  // here would turn a correct verdict into an unsupported exit.
  // A gated-clock flop is on the list because encoding it as a gated CLOCK is
  // what the encoder cannot do soundly OR cheaply: it has no clock-identity
  // model (so gated and ungated look identical unless the ICG name heuristic
  // happens to match) and a gate costs a per-step commit term. Rewritten into
  // the flop's ENABLE it is just an ordinary flop.
  //
  // ...but ONLY in a single-clock design. Folding a gate into an enable
  // expresses the flop's timing relative to THE reference clock, so a gate on a
  // SECOND domain (`gclk = clk_b & gate` while other flops run on `clock`) has
  // no such reference and the fold would be meaningless. There we owe nothing:
  // leave it skipped for pass/lec's multi-clock machinery, exactly as before.
  // With multi_clock the fold is relative to each element's OWN root, which
  // the certificate then names, so a gate in a second domain is meaningful too.
  const bool icg_foldable = need.n_icg_flops > 0 && (need.n_clock_nets <= 1 || opts.multi_clock);
  const bool must_fix     = need.n_latches > 0 || need.n_negedge_flops > 0 || icg_foldable || def_needs;
  if (!must_fix && opts.force_slots <= 1) {
    r.reason = need.needed ? std::format("skipped: {} — no latch or negedge state to lower", need.why)
                           : "skipped: no latch, no negedge state, one clock net";
    return r;
  }
  if (def_needs) {
    // A Latch inside a resolution-library def is NOT covered by fixing the top
    // graph: pass/lec/encode.cpp screens defs separately and sends a stateful
    // one to the blackbox path, so the design would silently keep an opaque
    // per-side transition — the free-per-side-constant CEX, with no diagnostic.
    r.error  = true;
    r.reason = def_why + ", and normalizing across a module boundary is not supported yet";
    refuse(quiet,
           "hier-unsupported",
           std::format("{}: {}", g->get_name(), r.reason),
           "flatten the design (--set formal.flatten=true), blackbox the def (--set formal.lec.trust=<def>), or "
           "normalize the def separately");
    return r;
  }

  auto plan = build_plan(g, clocks, opts.multi_clock);
  if (!plan.ok) {
    if (!must_fix) {
      r.reason = std::format("skipped: {}", plan.why);  // nothing owed here; downstream handles it
      return r;
    }
    r.error  = true;
    r.reason = plan.why;
    refuse(quiet,
           plan.code,
           std::format("{}: {}", g->get_name(), plan.why),
           "edge normalization declines rather than half-transform: a partial lowering is a silent full-cycle error");
    return r;
  }
  plan.slots = std::max(plan.slots, opts.force_slots);
  if (!check_rule4(plan, g, clocks, quiet, opts.forward_same_edge)) {
    r.error  = true;
    r.reason = "coincident commit edges between a latch and another state element";
    return r;
  }
  if (plan.slots > 2) {
    r.error  = true;
    r.reason = std::format("{} slots needed; only P<=2 is implemented", plan.slots);
    refuse(quiet, "too-many-slots", std::format("{}: {}", g->get_name(), r.reason), "");
    return r;
  }

  // Does this def REWRITE a clock? A latch retype moves when its enable
  // commits, and an ICG fold moves the gate into a flop enable; a def with only
  // plain posedge flops rewrites nothing and cannot mis-time a child.
  bool rewrites_a_clock = false;
  for (const auto& e : plan.elems) {
    if (e.is_latch || e.icg) {
      rewrites_a_clock = true;
      break;
    }
  }

  // Fail closed on shapes a partial lowering would silently mis-time.
  //
  // Every memory write port that survives this scan is recorded in
  // `mem_gate_plans` with the facts the gate will use.  The gate below consumes
  // them and recomputes nothing.
  std::vector<Mem_gate_plan> mem_gate_plans;
  for (auto n : g->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Fflop) {
      r.error  = true;
      r.reason = "fluid flop `" + label_of(n) + "` has no commit-class model";
      refuse(quiet, "fflop-unsupported", std::format("{}: {}", g->get_name(), r.reason), "");
      return r;
    }
    // WITHOUT a divider a memory owes this pass something only when a write
    // port is GATED: the gate must still fold into the write enable (ported
    // from d2, 90583be13 -- the Phase-A fold rode inside P>1, so a gated memory
    // in a design that needed no divider kept its gated clock and the exporter
    // refused it: B05's async-FIFO `u_ar_fifo.mem_1`).
    const bool mem_fold_only = op == Ntype_op::Memory && plan.slots <= 1 && mem_has_gated_write_port(n, clocks);
    if (op == Ntype_op::Memory && (plan.slots > 1 || mem_fold_only)) {
      // The blanket refusal this replaces fired on EVERY Memory at P>1.  What
      // actually mis-times under a phase divider is a port that COMMITS on a
      // sub-step it does not own, so the shape and the CLOCK decide.
      //
      // Every refusal here happens BEFORE any mutation: the plan is built and
      // validated first, and a design that cannot be lowered is returned
      // untouched rather than half-rewritten.
      const auto ms = mem_shape_of(n);
      if (!ms.parsed) {
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` has a non-constant type/wensize/posclk/rdport pin";
        refuse(quiet,
               "memory-unsupported",
               std::format("{}: {}", g->get_name(), r.reason),
               "the port shape must be constant before a slot gate can be placed");
        return r;
      }
      // JUDGED BEFORE ANY ROM CLASSIFICATION.
      if (ms.whole_array) {
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` is a WHOLE-ARRAY cell (`update`/`update_enable`/`reset` driven)";
        refuse(quiet,
               "memory-unsupported",
               std::format("{}: {}", g->get_name(), r.reason),
               "one bulk next-state bus instead of per-entry write ports; this pass gates per-entry write "
               "enables and has no model for the bulk commit");
        return r;
      }
      if (ms.mixed_edge) {
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` declares MIXED port polarity (posclk == Memory_posclk_mixed)";
        refuse(quiet,
               "memory-unsupported",
               std::format("{}: {}", g->get_name(), r.reason),
               "its ports do not share a committing edge, so one slot cannot describe the cell");
        return r;
      }
      if (mem_is_immutable_rom(ms)) {
        continue;  // nothing commits: an immutable asynchronous ROM
      }
      if (ms.sync_read && plan.slots > 1) {  // a divider-only hazard: at P=1 there are no sub-steps
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` has a synchronous read port; its read register would commit on every sub-step";
        refuse(quiet,
               "memory-unsupported",
               std::format("{}: {}", g->get_name(), r.reason),
               "synchronous read-port timing is not preserved by the slot gate yet");
        return r;
      }
      if (ms.wensize == 0) {
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` has a committing port but no positive wensize";
        refuse(quiet,
               "memory-unsupported",
               std::format("{}: {}", g->get_name(), r.reason),
               "the write-enable width must be known to gate the whole vector");
        return r;
      }
      // THE CLOCK, per committing port, through the SAME classifier a flop
      // uses.  `commit_class_of` returns nullopt for a Memory by contract, so
      // calling it here would have silently yielded slot 0 for every port
      // whatever its clock or `posclk` was.
      for (const auto& port : ms.ports) {
        if (port.rdport) {
          continue;  // an async read is combinational: re-evaluated every sub-step
        }
        // AN ABSENT ENABLE MEANS ALL LANES WRITE.
        //
        // That is the native contract, not a default invented here: both the
        // pinned old and the master simulator's `emit_wen` spell an invalid
        // enable pin as `Slop<wensize>::create_integer(-1)`
        // (inou/cgen/cgen_sim.cpp), and the LLVM path agrees.  An earlier
        // comment here claimed the contract was silent; it is not, and that
        // claim is withdrawn.  The gate below needs no wide constant for it:
        // the slot mask IS the gated enable when every lane is enabled.
        const auto clk_pin = port.clock.is_invalid() ? plan.ref_clk_pin : port.clock;
        // A GATED clock is refused.  `commit_class_of_clock` resolves an ICG to
        // its ROOT net and drops the gate's enable conditions -- which the flop
        // path then re-introduces by folding them into the element's `enable`.
        // This pass does no such folding for a Memory, so accepting a gated
        // memory clock would silently discard the gate.
        auto       cc      = lc::commit_class_of_clock(clk_pin, ms.rising, &clocks);
        if (!cc || cc->role != lc::Net_role::Clock) {
          r.error  = true;
          r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                     + " has a clock cone that does not resolve to a root net";
          refuse(quiet,
                 "memory-unsupported",
                 std::format("{}: {}", g->get_name(), r.reason),
                 "the committing edge must be classified before the port can be slotted");
          return r;
        }
        // A SECOND CLOCK DOMAIN (multi_clock): a POSEDGE port on another root
        // is exported as its own domain -- no slot gate, its gate (if any)
        // folds, its clock rebinds to ITS root. Anything else off the reference
        // is refused as before.
        bool port_off_ref = false;
        if (!cc->implicit_clock && cc->net_key() != plan.ref_net) {
          if (opts.multi_clock && cc->rising) {
            port_off_ref = true;
          } else {
            r.error  = true;
            r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                       + " commits on a net that is not the reference clock";
            refuse(quiet,
                   "memory-unsupported",
                   std::format("{}: {}", g->get_name(), r.reason),
                   opts.multi_clock ? "only a posedge memory may live off the reference clock"
                                    : "an off-reference memory clock is not rebound by this pass (see multi_clock)");
            return r;
          }
        }
        // TWO BITS OF ONE CLOCK BUS ARE DIFFERENT CLOCKS.  `build_plan` already
        // tracks this with `clock_bit_on`/`ref_clk_bit`; comparing `net_key()`
        // alone would accept a memory clocked by a DIFFERENT bit of the same
        // multi-bit input and then gate it against the reference bit's phase.
        if (!port_off_ref && !cc->implicit_clock && !cc->net.is_invalid() && gu::is_graph_input_pin(cc->net)
            && gu::bits_of(cc->net) > 1) {
          const auto bit = clock_bit_on(clk_pin, cc->net, 0, 0);
          if (!bit || (plan.ref_clk_bit >= 0 && *bit != plan.ref_clk_bit)) {
            r.error  = true;
            r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                       + " commits on a different bit of the clock bus than the reference";
            refuse(quiet,
                   "memory-unsupported",
                   std::format("{}: {}", g->get_name(), r.reason),
                   "two bits of one clock bus are two clocks; this pass rebinds only the reference bit");
            return r;
          }
        }
        // A FALLING committing edge is REFUSED, not slotted.
        //
        // A negedge flop is lowered by RETYPING it to posedge and gating it to
        // the fall slot; the two together are what make the result
        // single-edge.  A Memory has no equivalent retype here yet -- this pass
        // does not touch `clock_pin` or `posclk` on a Memory -- so gating alone
        // would leave the cell still committing on the fall while ALSO being
        // slot-gated, i.e. a half-lowering whose emission still carries an
        // inverted memory clock.  Refuse it by name until the rebinding is
        // implemented and validated, rather than emit that.
        //
        // This is also what makes the per-port classification load-bearing in
        // this increment: its job here is to DECIDE ADMISSIBILITY.  Before it,
        // `commit_class_of` returned nullopt for every Memory and a falling
        // memory was silently gated to slot 0.
        if (!cc->rising) {
          r.error  = true;
          r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port) + " commits on the FALLING edge";
          refuse(quiet,
                 "memory-unsupported",
                 std::format("{}: {}", g->get_name(), r.reason),
                 "a falling-edge memory would need its clock rebound and its polarity retyped, which this pass "
                 "does not yet do for a Memory; gating alone would leave an inverted memory clock in a "
                 "single-edge design");
          return r;
        }
        // A GATED write clock is FOLDED INTO THE WRITE ENABLE, the same move the
        // flop path makes: `commit_class_of_clock` resolves an ICG to its ROOT
        // and drops the gate's conditions, so they have to be re-introduced or
        // the gate is silently discarded.
        //
        // RESOLVED HERE, AFTER `cc`, deliberately. `cc` is the EFFECTIVE commit
        // class -- it already folds `icg->clock_inverted` in -- and the checks
        // above have established that this port is on the reference net, on the
        // reference BIT, and rising. Doing the gate work earlier would reason
        // about `ms.rising` against a root whose polarity had not been settled.
        //
        // NOTHING IS ALLOCATED. Only existing pins are frozen into the plan;
        // the AND cone over them is built in the rewrite, because `dry_run`
        // returns before the rewrite and promises an untouched graph.
        std::vector<hhds::Pin_class> port_gate_ops;
        if (!clk_pin.is_invalid()) {
          if (const auto icg = lc::resolve_icg(clk_pin, clocks)) {
            if (icg->div != 1) {
              r.error  = true;
              r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                         + " has a gated clock with div=" + std::to_string(icg->div);
              refuse(quiet,
                     "memory-unsupported",
                     std::format("{}: {}", g->get_name(), r.reason),
                     "a divided memory clock is a second commit class, not a gate");
              return r;
            }
            // AN INVERTED GATE IS REFUSED, not rebound. `~root & en` gates the
            // FALLING root edge, and `commit_class_of_clock` folds that in --
            // so a negedge Memory on such a gate can present as rising here and
            // pass the check above. Rebinding it to the root while leaving
            // `posclk` alone would move it to the root's FALLING edge, which
            // `cgen_verilog`'s clock_expr still inverts: a silent half-cycle
            // change. This pass does not retype a Memory's polarity (see the
            // falling-edge refusal above), so the combination is declined by
            // name instead of half-lowered.
            if (icg->clock_inverted) {
              r.error  = true;
              r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                         + " has an INVERTED gated clock (`~clk & en`)";
              refuse(quiet,
                     "memory-unsupported",
                     std::format("{}: {}", g->get_name(), r.reason),
                     "rebinding to the root would change the committing edge, and this pass does not retype a "
                     "memory's polarity");
              return r;
            }
            for (auto en : icg->enables) {
              if (!en.is_invalid() && !en.is_const() && !gu::is_graph_input_pin(en)) {
                if (auto dn = en.get_master_node(); gu::type_op_of(dn) == Ntype_op::Latch) {
                  // The enable-latch bypass, against the EFFECTIVE edge.
                  const bool closed_after = cc->rising;
                  if (!lc::gated_latch_closed_at(dn, icg->clock, closed_after)
                      || !lc::latch_open_at(dn, icg->clock, !closed_after)) {
                    r.error  = true;
                    r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                               + " is clocked through a gate whose enable latch `" + label_of(dn)
                               + "` is not transparent exactly on the phase opposite the gated edge";
                    refuse(quiet,
                           "memory-unsupported",
                           std::format("{}: {}", g->get_name(), r.reason),
                           "edge normalization declines rather than half-transform: a partial lowering is a "
                           "silent full-cycle error");
                    return r;
                  }
                  if (auto arm = lc::latch_transparent_arm(dn); !arm.is_invalid()) {
                    en = arm;
                  }
                }
              }
              if (!en.is_invalid()) {
                port_gate_ops.push_back(en);
              }
            }
            if (port_gate_ops.empty()) {
              r.error  = true;
              r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                         + " has a gated clock whose gate carries no usable enable";
              refuse(quiet,
                     "memory-unsupported",
                     std::format("{}: {}", g->get_name(), r.reason),
                     "folding a gate with no condition would drop it");
              return r;
            }
          }
        }
        // THE SLOT, decided here and FROZEN.  There is no fallback: the class
        // resolved, is on the reference net and bit, and is rising, so either
        // the key is in `slot_of_key` or the rise/fall map gives it.
        // -1 = NO slot gate: no divider, or a port on a second domain.
        int slot = -1;
        if (plan.slots > 1 && !port_off_ref) {
          if (auto it = plan.slot_of_key.find(cc->key()); it != plan.slot_of_key.end()) {
            slot = it->second;
          } else {
            slot = cc->rising ? 0 : 1;  // the same map build_plan uses
          }
        }
        if (slot < 0 && port_gate_ops.empty()) {
          continue;  // neither a slot nor a gate: this port is left exactly as it is
        }
        mem_gate_plans.push_back(
            {n, port.port, port.enable, ms.wensize, slot, port_gate_ops, port_off_ref ? cc->net : hhds::Pin_class{}});
      }
      // everything else is handled by the slot gate below
    }
    if (op != Ntype_op::Sub) {
      continue;
    }
    auto sub = n.get_subnode_graph();
    if (!sub) {
      continue;  // blackbox / property cell: no state of ours to re-time
    }
    // ...and only under a DIVIDER, exactly like the Memory case above. What
    // this refuses is re-timing a child we cannot reach: at P>1 the parent's
    // state moves onto slots driven by a phase counter that is not threaded
    // through the child's ports, so the child would keep counting in the old
    // time base. At P=1 there IS no phase counter and no slot -- a latch
    // retype and an ICG folded into a flop enable both leave every clock and
    // every instance boundary exactly where they were -- so a stateful child
    // is simply not this pass's business.
    //
    // Scoping this is what lets a def with an inlined clock gate fold at all:
    // a real one instantiates FIFOs and register files (minion's
    // `minion_dcache_reduce` holds `u_ba_alloc_fifo`), so the unscoped guard
    // refused every def that mattered and the gated flops stayed unencodable.
    bool child_has_state = false;
    for (auto sn : sub->body().nodes()) {
      if (gu::is_type_register(sn)) {
        child_has_state = true;
        break;
      }
    }
    if (child_has_state && plan.slots > 1) {
      r.error  = true;
      r.reason = "instance `" + label_of(n) + "` holds state; the phase divider is not port-threaded";
      refuse(quiet,
             "hier-unsupported",
             std::format("{}: {}", g->get_name(), r.reason),
             "flatten the design before normalizing (all three M8 gate fixtures are flat)");
      return r;
    }
    // P=1 is NOT automatically safe for a stateful child. The claim above --
    // "a latch retype and an ICG folded into a flop enable leave every clock and
    // every instance boundary exactly where they were" -- holds only while the
    // rewritten clock stays INSIDE this def. When an ICG's gated clock also
    // crosses into a stateful child's clock port, the fold retypes the enable
    // latch to a flop for the LOCAL flops while the child keeps consuming the
    // gate, so the two halves of one clock gate disagree by a full cycle and the
    // pass silently ships a design whose submodule gating is off by one -- where
    // it used to refuse honestly.
    //
    // The discriminator is cheap and exact: a child clocked by a PLAIN clock
    // input is untouched by anything this pass does (that is the minion shape
    // the narrowing exists for), while a child clocked by IN-GRAPH LOGIC is
    // clocked by something we may be about to rewrite. Refuse only the latter,
    // and only when this def actually rewrites a clock (a latch or a folded
    // ICG); a def with plain posedge state normalizes nothing and stays allowed.
    if (child_has_state && rewrites_a_clock) {
      auto csio = n.get_subnode_io();
      for (auto sink : n.inp_sorted_pins()) {
        const auto       drv = sink.get_driver_pin();
        // Clock ports only, by the SHARED spelling notion (Design_clocks::
        // name_looks_like_clock) rather than a private list, so this and the
        // rest of M8 agree on what a clock port is.
        std::string_view pname;
        for (const auto& d : csio->get_input_pin_decls()) {
          if (csio->get_input_port_id(d.name) == sink.get_port_id()) {
            pname = d.name;
            break;
          }
        }
        if (pname.empty() || !lc::Design_clocks::name_looks_like_clock(pname)) {
          continue;
        }
        // A PLAIN clock input (possibly through the width mask the readers add)
        // is untouched by this pass; anything else is in-graph logic.
        if (gu::is_graph_input_pin(drv) || (gu::type_op_of(drv.get_master_node()) == Ntype_op::Get_mask && [&] {
              for (auto gsink : drv.get_master_node().inp_sorted_pins()) {
                if (gu::is_graph_input_pin(gsink.get_driver_pin())) {
                  return true;
                }
              }
              return false;
            }())) {
          continue;
        }
        {
          r.error  = true;
          r.reason = "instance `" + label_of(n) + "` holds state and is clocked by in-graph logic this pass rewrites";
          refuse(quiet,
                 "hier-unsupported",
                 std::format("{}: {}", g->get_name(), r.reason),
                 "the gate would be re-timed for this def's own flops but not for the child's; flatten the design, or "
                 "trust the child def, before normalizing");
          return r;
        }
      }
    }
  }

  // An active-LOW latch is lowered by inverting its ENABLE. Two shapes make
  // that impossible, and both are refused rather than approximated.
  for (const auto& e : plan.elems) {
    if (!e.is_latch || !latch_is_active_low(e.node)) {
      continue;
    }
    if (sink_driver(e.node, "enable").is_invalid()) {
      r.error  = true;
      r.reason = "latch `" + label_of(e.node) + "` is active-low with no enable driver (permanently transparent)";
      refuse(quiet,
             "raw-latch-unsupported",
             std::format("{}: {}", g->get_name(), r.reason),
             "a permanently transparent latch is a buffer, not a state element; drop the cell");
      return r;
    }
    if (has_hold_mux(e.node)) {
      r.error  = true;
      r.reason = "latch `" + label_of(e.node) + "` has an active-low polarity pin AND a hold mux on din";
      refuse(quiet,
             "raw-latch-unsupported",
             std::format("{}: {}", g->get_name(), r.reason),
             "the hold mux is keyed on the un-inverted condition while the enable test is inverted, so the two "
             "disagree — no emitter produces this shape and lowering it would pick one arbitrarily");
      return r;
    }
  }

  if (opts.dry_run) {
    // Everything above is analysis; nothing has been mutated. Report what the
    // rewrite WOULD do so a miter can pick one shared time base first.
    r.applied   = plan.slots > 1 || r.latches_retyped > 0 || need.n_latches > 0 || need.n_negedge_flops > 0;
    r.slots     = plan.slots;
    r.ref_clock = plan.ref_clock_name;
    r.reason    = std::format("plan: P={} slots ({})", plan.slots, need.why.empty() ? "forced" : need.why);
    return r;
  }

  // ---- the rewrite -------------------------------------------------------
  if (plan.ref_clk_bit > 0 && !plan.ref_clk_pin.is_invalid()) {
    // The reference edge is on bit k>0 of a bus input. Every rebound clock_pin
    // reads its edge on bit 0 of its driver, so binding the bare bus would move
    // the whole design onto clks[0]. Select the bit once and rebind to that.
    auto sel = gu::create_get_mask(*g, plan.ref_clk_pin, plan.ref_clk_bit).create_driver_pin(0);
    gu::set_bits(sel, 1);
    gu::set_unsign(sel);
    plan.ref_clk_pin = sel;
  }
  hhds::Pin_class                                   slot_pred[2];
  // One sampled gate guard per GATE, not per consumer. The key is a PLANNED
  // identity -- the gate's clock root, its reference edge, and the guard cone's
  // own pins -- built BEFORE any node is created, which is what
  // `pass/lec/phase_sched.cpp` does (`root_key` + polarity + `cone_digest` per
  // guard, then `guard_cones`). Keying on a freshly built AND node instead
  // would give every COMPOUND guard a fresh id and so a separate held state,
  // sharing only the single-enable case by accident.
  absl::flat_hash_map<std::string, hhds::Pin_class> held_guards;
  if (plan.slots > 1) {
    // Phase divider off the design's OWN clock: one free-running clock and a
    // CONCRETELY INITIALIZED counter, so at unroll step j the solver knows
    // `phase == j mod P` statically and every slot predicate const-folds.
    auto ph = gu::create_typed_node(*g, Ntype_op::Flop);
    ph.attr(hhds::attrs::name).set(std::string{kPhaseName});
    auto phq = ph.create_driver_pin(0);
    gu::set_bits(phq, 1);
    gu::set_unsign(phq);
    phq.attr(livehd::attrs::pin_name).set(std::string{kPhaseName});
    gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(ph, "initial"));
    if (!plan.ref_clk_pin.is_invalid()) {
      plan.ref_clk_pin.connect_sink(gu::setup_sink_by_name(ph, "clock_pin"));
    }
    // This divider describes the time base, independently of the design's
    // resets. Copying one state element's reset here changes the phase when
    // that reset is folded into mapped latch logic on the other side of LEC.
    // The encoder preserves this synthetic key's concrete initial value
    // (graph_util::is_single_edge_phase_key), including during after_reset,
    // so both designs advance through the same slots throughout the prologue.
    // P == 2: next = !phase (a 1-bit counter). P > 2 is refused above.
    auto nxt = gu::create_typed_node(*g, Ntype_op::Not);
    phq.connect_sink(livehd::graph_util::setup_sink_pid(nxt, 0));
    auto nxtq = nxt.create_driver_pin(0);
    gu::set_bits(nxtq, 1);
    gu::set_unsign(nxtq);
    nxtq.connect_sink(gu::setup_sink_by_name(ph, "din"));

    for (int k = 0; k < plan.slots; ++k) {
      auto eq = gu::create_typed_node(*g, Ntype_op::EQ);
      phq.connect_sink(livehd::graph_util::setup_sink_pid(eq, 0));
      gu::create_const(*g, *Dlop::create_integer(k)).connect_sink(livehd::graph_util::setup_sink_pid(eq, 0));
      auto eqq = eq.create_driver_pin(0);
      gu::set_bits(eqq, 1);
      gu::set_unsign(eqq);
      slot_pred[k] = eqq;
    }
  }

  // ---- the memory slot gate ------------------------------------------------
  // A write port must commit ONLY on the sub-step its memory owns.  The flop
  // form above is `enable &= (phase == slot)` on a ONE-BIT enable; a write
  // enable is a VECTOR -- one bit per lane, `wensize` wide, and on the retained
  // B03/B11 graphs that is 16, 27, 34, 69, 70 or 8224 bits, never 1.  ANDing a
  // 1-bit predicate into it would keep lane 0 and silently drop every higher
  // lane, which is a different design.
  //
  // NO WIDE CONSTANT IS MINTED.  `gu::create_const` returns a CANONICAL SHARED
  // pin (`create_constant` deduplicates by value representation;
  // graph/node_util.hpp:326 and pinned HHDS hhds/graph.hpp:1815) and
  // `gu::set_bits` (node_util.hpp:838) writes that pin's own attribute, so a
  // `wensize` zero arm stamped with `set_bits` restamps the ONE zero every
  // other reader shares -- measured at b6b0a5cb6 as B11's shared zero going raw
  // width 0 -> 69 and B03's 0 -> 27, against Concat lanes of 9 and 1.
  //
  // The mask is therefore built from the predicate with the repository's own
  // sizing idiom (pass/abc/abc_satopt_test's `a AND Sext(c, 1)`): a SIGNED
  // sign-extension of the 1-bit predicate is all-ones when it holds and
  // all-zeros when it does not, at whatever width its own driver declares.
  // Every pin stamped below is this gate's OWN driver pin.
  //
  //     enable' = enable AND sext(phase == slot, 1)      (enable present)
  //     enable' =           sext(phase == slot, 1)       (enable ABSENT)
  //
  // The second line is the NATIVE contract for an absent enable -- all lanes
  // write, `Slop<wensize>::create_integer(-1)` in both the pinned old and the
  // master `emit_wen` (inou/cgen/cgen_sim.cpp) -- expressed without a wide
  // constant, because the mask already IS an all-ones vector of the right
  // width whenever the slot is active.
  //
  // Active: the ORIGINAL vector bit for bit, so every high lane survives.
  // Quiet: a width-correct all-zero vector, so no lane commits.  Geometry,
  // initial/default contents, `fwd`, `posclk`, read latency and the clock pins
  // are untouched.
  //
  // EVERY FACT HERE IS FROZEN.  `mem_gate_plans` was filled during the refusal
  // scan; nothing is reclassified here and there is no slot fallback.
  for (const auto& mp : mem_gate_plans) {
    // THE MASK MUST SURVIVE BITWIDTH, NOT JUST LEAVE THIS PASS WIDE.
    //
    // `Sext(pred, 1)` is a ONE-BIT SIGNED value (0 or -1).  Stamping 69 bits on
    // its driver here does NOT stick: `Bitwidth::process_sext` recomputes the
    // range from the POSITION operand -- `bw.set_sbits_range(sign_max)` with
    // `sign_max = 1` -- so the emitted node came out `Op_Sext width 1`, and the
    // certificate's `Op_And` does not sign-extend its operands.  The emitted
    // gate was therefore `And(enable_69, mask_1)`, which can only ever enable
    // bit 0: lanes 1..68 could never be written.  Measured in the retained v4
    // certificate at origin 6792.
    //
    // The widening idiom this design ALREADY uses is `Get_mask` against an
    // all-ones constant -- see origin 5268 in the same certificate,
    // `Op_GetMask w69 [Op_Sext w69, const70 2^69-1]`.  `mask_window_const`
    // builds that constant at any width, so the mask becomes a genuine
    // `wensize`-wide all-ones (predicate high) or all-zeros (predicate low),
    // and `Get_mask`'s own result width is what carries it through bitwidth.
    //
    // No BV or Sext semantics are changed and no unsigned operand is widened:
    // the only new value is this gate's own mask constant.
    // THE GATE JOINS THE PREDICATE, not the vector. Both are one bit here, so
    // the AND is a 1-bit AND and the Sext + Get_mask below widens the PAIR to
    // `wensize` -- the gate therefore reaches every lane, and no wide constant
    // is minted for it either.
    // No slot (P=1, or a second domain): the gate ALONE is the predicate.
    hhds::Pin_class pred = mp.slot >= 0 ? slot_pred[mp.slot] : hhds::Pin_class{};
    for (const auto& gop : mp.gate_ops) {
      if (gop.is_invalid()) {
        continue;
      }
      if (pred.is_invalid()) {
        pred = gop;
        continue;
      }
      auto gandn = gu::create_typed_node(*g, Ntype_op::And);
      pred.connect_sink(livehd::graph_util::setup_sink_pid(gandn, 0));
      gop.connect_sink(livehd::graph_util::setup_sink_pid(gandn, 0));
      pred = gandn.create_driver_pin(0);
      gu::set_bits(pred, 1);
      gu::set_unsign(pred);
    }
    if (pred.is_invalid()) {
      continue;  // unreachable: a port with neither slot nor gate is never planned
    }
    auto sx = gu::create_typed_node(*g, Ntype_op::Sext);
    pred.connect_sink(sx.create_sink_pin(0));
    gu::create_const(*g, *Dlop::create_integer(1)).connect_sink(sx.create_sink_pin(1));
    auto sxq = sx.create_driver_pin(0);
    gu::set_sbits(sxq, mp.wensize);

    auto gm = gu::create_typed_node(*g, Ntype_op::Get_mask);
    // `connect_mask_operands` installs the value AND the `lo`/`hi` endpoint
    // constants the readers require: `graph_util::bit_range` reads those pins,
    // and `pass.lean` refuses a Get_mask without them.  The window is
    // [0, wensize): every lane of the sign-extended predicate.
    livehd::graph_util::connect_mask_operands(gm, sxq, 0, static_cast<int>(mp.wensize));
    auto mask = gm.create_driver_pin(0);
    gu::set_bits(mask, mp.wensize);
    gu::set_unsign(mask);

    hhds::Pin_class gated;
    if (mp.enable.is_invalid()) {
      gated = mask;  // the native all-lanes default, at full width
    } else {
      auto andn = gu::create_typed_node(*g, Ntype_op::And);
      mp.enable.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
      mask.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
      gated = andn.create_driver_pin(0);
      gu::set_bits(gated, mp.wensize);
      gu::set_unsign(gated);
    }
    mem_drop_port_sink(mp.node, "enable", mp.port);
    const auto stride = static_cast<uint32_t>(Ntype::Memory_port_stride);
    gated.connect_sink(mp.node.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, "enable") + mp.port * stride));
    // THE GATED CLOCK MUST GO. Folding the gate into the enable while leaving
    // the gated net on `clock_pin` would apply the gate twice and, worse, leave
    // a DERIVED clock in a design this pass exists to make single-edge -- the
    // encoder refuses one, so the cell would come back UNKNOWN after a
    // successful lowering. Rebind to the root the gate resolved to.
    const auto rebind_to = mp.own_root.is_invalid() ? plan.ref_clk_pin : mp.own_root;
    if (!mp.gate_ops.empty() && !rebind_to.is_invalid()) {
      // REBIND TO THE VALIDATED REFERENCE PIN, not to the gate's bare root.
      // When the reference edge is on bit k>0 of a bus, `plan.ref_clk_pin` has
      // already been replaced by the SELECTED bit above -- every rebound
      // clock_pin reads its edge on bit 0 of its driver, so binding the bare
      // bus would move this memory onto bit 0 while the rest of the design sits
      // on bit k. The scan has already refused a port on a DIFFERENT bit, so
      // this port is on the reference bit by construction and the selected pin
      // is the right one.
      mem_drop_port_sink(mp.node, "clock_pin", mp.port);
      rebind_to.connect_sink(mp.node.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin") + mp.port * stride));
      ++r.mem_ports_ungated;
    }
    if (mp.slot >= 0) {
      ++r.mem_ports_slotted;
    } else {
      ++r.icg_folded;  // a gate folded with no slot (P=1, or a second domain)
    }
  }
  {
    absl::flat_hash_set<hhds::Class_index> seen;
    for (const auto& mp : mem_gate_plans) {
      if (mp.slot >= 0 && seen.insert(mp.node.get_class_index()).second) {
        ++r.mems_slotted;
      }
    }
  }

  std::vector<hhds::Pin_class> orphaned_clocks;  // old clock_pin drivers the rebind below disconnects
  for (const auto& e : plan.elems) {
    if (e.is_latch) {
      // The value a CLOCK-gated latch passes through, read BEFORE anything below
      // rewires its enable: latch_transparent_arm proves a hold arm against the
      // window as written (enable + polarity pin), and the active-low lowering
      // next inverts the enable while the pin is still on the cell.
      const auto through   = e.cc.role == lc::Net_role::Clock ? lc::latch_transparent_arm(e.node) : hhds::Pin_class{};
      const auto hold_path = e.cc.role == lc::Net_role::Clock ? lc::latch_hold_path(e.node) : std::optional<lc::Latch_hold_path>{};
      // `posclk` on a Latch is the ENABLE POLARITY, not an edge. Carrying it
      // onto a Flop would silently re-read it as "negedge" — the exact
      // double-negation this task exists to prevent. Move the polarity into the
      // enable cone (where every Pyrope/slang latch already carries it), then
      // drop the pin.
      if (latch_is_active_low(e.node)) {
        auto en  = sink_driver(e.node, "enable");
        auto inv = gu::create_typed_node(*g, Ntype_op::Not);
        en.connect_sink(livehd::graph_util::setup_sink_pid(inv, 0));
        auto iq = inv.create_driver_pin(0);
        gu::set_bits(iq, 1);
        gu::set_unsign(iq);
        drop_sink(e.node, "enable");
        iq.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
      }
      // ---- a CLOCK-gated latch: the gate is ABSORBED by the slot ----------
      // Not AND-ed with it. A latch whose gate IS a clock commits exactly once
      // per period, at one edge, and the slot already says which — so the gate
      // has no residual meaning. Leaving it in place would keep the normalized
      // design READING THE CLOCK AS DATA, and after normalization one clock
      // tick is one SUB-step, so that cone would be evaluated P times per
      // source period: the enable becomes `clk & (phase==slot)`, which commits
      // only when the free clock input happens to be high at the slot step.
      // Measured with iverilog: the latch outputs stay X.
      //
      // So the enable becomes the slot predicate alone, and `din` becomes the
      // TRANSPARENT arm of tolg's hold mux (`gate ? d : q` -> `d`): the value
      // the window was passing through when it closed, which is exactly what
      // commit-at-closing-edge commits. A DATA gate is left completely alone —
      // it is real logic, and collapsing the two cases is what would blind the
      // transparent-low polarity canary.
      //
      // The hold arm comes in THREE spellings and all of them are peeled: tolg's
      // Q feedback (`gate ? d : q`), the reader's don't-care (`gate ? d : 'x`),
      // and -- under `yosys.setundef=zero`, which every CORE-ET and CVA6 row is
      // compiled with -- a KNOWN constant (`Ror(clk) ? 0 : en`). Peeling only the
      // Q spelling left the other two in din, so the normalized design kept
      // reading the clock as data on exactly the latches the gate removed:
      // CORE-ET B01's `if (!clk_i) en_1p <= en_i` enable latches, live state in
      // the certificate. latch_transparent_arm proves the constant/X arm is the
      // SHUT phase's before it peels; anything it cannot prove comes back as din
      // itself, which is the value mux it really is.
      if (e.cc.role == lc::Net_role::Clock) {
        // A bit-blasted WIDE latch: din is a bit select of the hold mux, which
        // latch_transparent_arm returns unchanged; its open value is the same
        // bit select of the open arm (CORE-ET B08's mpcnd_1p[k]).
        const auto open_value = hold_path ? lc::rebuild_bit_select(*g, hold_path->chain, hold_path->value) : through;
        if (open_value.is_invalid()) {
          r.error  = true;
          r.reason = "clock-gated latch `" + label_of(e.node) + "` has a hold mux with no transparent arm";
          refuse(quiet, "latch-shape-unsupported", std::format("{}: {}", g->get_name(), r.reason), "");
          return r;
        }
        if (auto din = sink_driver(e.node, "din"); din.is_invalid() || din.get_class_index() != open_value.get_class_index()) {
          drop_sink(e.node, "din");
          open_value.connect_sink(gu::setup_sink_by_name(e.node, "din"));
        }
        // The yosys raw-D shape has no hold arm at all: `din` is already the
        // transparent value, so only the gate has to go.
        drop_sink(e.node, "enable");
      }
      drop_sink(e.node, "posclk");
      gu::set_type_op(e.node, Ntype_op::Flop);
      ++r.latches_retyped;  // clock_pin is bound by the shared rebind below
      if (e.cc.role != lc::Net_role::Clock && opts.transparent_data_latch) {
        r.data_latch_reads += make_data_latch_transparent(*g, e.node);
        ++r.data_latches_transparent;
      }
    } else if (!e.cc.rising) {
      // A negedge flop becomes a posedge flop in slot 1.
      drop_sink(e.node, "posclk");
    }
    // ---- CLOCK GATING -> FLOP ENABLE --------------------------------------
    // An ICG is the commonest latch structure in hardware, and modelling it as
    // a gated CLOCK is both unsound here (the encoder has no clock-identity
    // model, so gated and ungated look identical) and wasteful (a per-step
    // commit term on every flop). A gated clock IS a flop enable: the flop
    // commits on the reference edge iff the enables hold there. So fold the
    // enables into `enable` and let the ordinary Flop encoding do the work.
    //
    // The ENABLE-LATCH BYPASS is what makes this exact. A real ICG latches its
    // enable on the opposite phase purely to suppress glitches: the latch closes
    // exactly when the flop samples, so the flop sees the value the latch was
    // passing THROUGH, not its previously-held one. Taking the latch's Q instead
    // would be a full cycle late -- and would also make the latch and the flop a
    // coincident-edge pair, which rule 4 then (correctly, for that shape)
    // rejects. Taking its transparent arm is the US7624363 L1->buffer rewrite,
    // applied exactly where it belongs.
    if (e.icg) {
      const auto and1 = [&](const hhds::Pin_class& a, const hhds::Pin_class& b) {
        if (a.is_invalid()) {
          return b;
        }
        if (b.is_invalid()) {
          return a;
        }
        auto andn = gu::create_typed_node(*g, Ntype_op::And);
        a.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
        b.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
        auto aq = andn.create_driver_pin(0);
        gu::set_bits(aq, 1);
        gu::set_unsign(aq);
        return aq;
      };
      // THE PLANNED GUARD IDENTITY, from pins that already exist: the gate's
      // clock root, the reference edge it is sampled against, and every pin of
      // the guard cone, sorted so operand order cannot split one gate in two.
      // Nothing here allocates, so two consumers of the SAME gate agree on it
      // however many enables the gate has.
      std::string guard_key;
      {
        std::vector<uint64_t> cone;
        cone.reserve(e.icg->enables.size());
        for (const auto& en : e.icg->enables) {
          cone.push_back(en.is_invalid() ? 0 : static_cast<uint64_t>(en.get_class_index().value));
        }
        std::sort(cone.begin(), cone.end());
        guard_key = std::format("ck:{}:{}",
                                e.icg->clock.is_invalid() ? 0 : static_cast<uint64_t>(e.icg->clock.get_class_index().value),
                                e.cc.rising ? '+' : '-');
        for (const auto c : cone) {
          guard_key += std::format("&{}", c);
        }
      }

      // THE GATE GUARD, on its own. Only the gate's own enables go in here --
      // the element's LOCAL enable is deliberately left out and combined after
      // any sampling below.
      hhds::Pin_class gate_acc;
      for (const auto& en : e.icg->enables) {  // already latch-bypassed in build_plan
        gate_acc = and1(gate_acc, en);
      }

      // THE HOLD PATH, for a falling consumer of a low-transparent gate. The
      // gate reproduces its falling edge iff the value captured before the rise
      // and held across the high phase was 1, so that value -- not the live arm
      // -- is this consumer's gate condition. It is SAMPLED in the rising slot.
      //
      // This is the parity pass/lec/phase_sched.cpp already uses: a guard
      // sampled on the inactive phase immediately before the CELL's own active
      // edge, "parity of the CELL, not of the consumer" (guard_before_fall /
      // guard_sample), and keyed so one gate contributes ONE guard
      // (guard_key / guard_cones) rather than one per consumer. `icg_held`
      // counts the consumers served; `held_guards` is the sharing.
      //
      // WHY ONLY THE GATE GUARD. The consumer's own enable is evaluated at ITS
      // commit, not at the rise. Sampling the conjunction instead would freeze
      // the local enable half a period early: with the gate on and the local
      // enable 0 at the rise and 1 at the fall it would suppress a commit the
      // design makes, and 1-at-rise / 0-at-fall would invent one. B01's
      // `wr_data_en_i` is exactly such a local enable.
      //
      // EPOCH. The divider initializes to 0 and slot 0 is therefore the FIRST
      // normalized step, so the sampling commit always precedes the first
      // slot-1 read and the held flop's power-on value is never observed. It is
      // still given a concrete 0 so the encoder has a definite initial state
      // rather than a free one.
      if (e.icg_hold && !gate_acc.is_invalid() && plan.slots > 1 && !slot_pred[0].is_invalid()) {
        auto it = held_guards.find(guard_key);
        if (it == held_guards.end()) {
          auto hold = gu::create_typed_node(*g, Ntype_op::Flop);
          gate_acc.connect_sink(gu::setup_sink_by_name(hold, "din"));
          slot_pred[0].connect_sink(gu::setup_sink_by_name(hold, "enable"));
          if (!plan.ref_clk_pin.is_invalid()) {
            plan.ref_clk_pin.connect_sink(gu::setup_sink_by_name(hold, "clock_pin"));
          }
          gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(hold, "initial"));
          auto hq = hold.create_driver_pin(0);
          gu::set_bits(hq, 1);
          gu::set_unsign(hq);
          it = held_guards.emplace(guard_key, hq).first;
        }
        gate_acc = it->second;
        ++r.icg_held;
      }

      // The LOCAL enable joins here, unsampled, so it is read at this
      // element's own commit. The slot predicate is ANDed in further below.
      hhds::Pin_class acc = gate_acc;
      if (auto old_en = sink_driver(e.node, "enable"); !old_en.is_invalid()) {
        acc = and1(acc, old_en);
      }
      drop_sink(e.node, "enable");
      if (!acc.is_invalid()) {
        acc.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
      }
      ++r.icg_folded;
      r.icg_window_held += e.icg_window_held > 0 ? 1 : 0;
    }

    // ---- a SECOND DOMAIN (multi_clock): exported, not lowered ------------
    // Its timing is ITS clock's, not a slot of the reference divider, so it
    // gets no slot gate and no sync-reset move; only the clock cone is rebound
    // to its resolved root (an ICG fold above has already moved the gate into
    // the enable), so the consumer sees a clean root and not a derived cone.
    if (e.off_ref) {
      const auto own = e.icg ? e.icg->clock : e.cc.net;
      if (!own.is_invalid()) {
        auto cur = sink_driver(e.node, "clock_pin");
        if (cur.is_invalid() || cur.get_class_index() != own.get_class_index()) {
          if (!cur.is_invalid()) {
            orphaned_clocks.push_back(cur);
          }
          drop_sink(e.node, "clock_pin");
          own.connect_sink(gu::setup_sink_by_name(e.node, "clock_pin"));
        }
      }
      continue;
    }

    // ---- every element now commits on the REFERENCE clock ----------------
    // After normalization the slot predicate carries ALL of the timing, so any
    // derived clock cone left on clock_pin is not just redundant, it is
    // actively harmful: the encoder still sees a derived clock and (correctly,
    // since M8) REFUSES to model it, so a design the pass had just lowered
    // successfully came back UNKNOWN. Rebinding to the resolved root also
    // removes the inversion of a `posedge ~clk` flop, whose edge is now
    // expressed by its slot instead.
    if (!plan.ref_clk_pin.is_invalid()) {
      auto cur = sink_driver(e.node, "clock_pin");
      if (cur.is_invalid() || cur.get_class_index() != plan.ref_clk_pin.get_class_index()) {
        if (!cur.is_invalid()) {
          orphaned_clocks.push_back(cur);
        }
        drop_sink(e.node, "clock_pin");
        plan.ref_clk_pin.connect_sink(gu::setup_sink_by_name(e.node, "clock_pin"));
      }
    }

    if (plan.slots > 1) {
      // ---- SYNC reset moves INSIDE the slot gate --------------------------
      // The encoder nests reset OUTSIDE the commit (`ITE(rst, init, ITE(en,
      // din, self))`), i.e. ASYNC for every flop. Harmless while one step is
      // one commit; WRONG the moment a flop only owns some steps, because its
      // reset then also lands in slots it does not own. Measured on
      // flop_reset_matrix: the ref spells a sync reset with `reset_pin` (reset
      // outside the gate) and its own round-trip spells the identical reset as
      // an ordinary `if reset { … }` in the DATA path (inside the gate), so at
      // P=2 the two disagreed at the first checked step — a FALSE REFUTED on a
      // design that is its own round-trip.
      //
      // Normalize toward the data-path form, which is what "synchronous" means:
      //     din    := rst ? initial : din
      //     enable := enable | rst          (reset WINS over the enable)
      // then drop the pin. An ASYNC reset is left on `reset_pin` — outside the
      // gate is exactly right for it, and it is also the form M7 ruled a
      // lowered latch's reset must keep.
      if (auto rstp = sink_driver(e.node, "reset_pin"); !rstp.is_invalid()) {
        auto       asyncp   = sink_driver(e.node, "async");
        // A LATCH's reset is inherently ASYNCHRONOUS -- there is no clock edge
        // for it to synchronize to, which is M7's landed ruling and why cgen
        // emits it as the FIRST branch ahead of the transparency test. So a
        // retyped latch's reset must stay OUTSIDE the slot gate even when it
        // carries no `async` attribute. Folding it in would gate the reset on
        // the slot predicate, and an async reset pulse that falls between two
        // normalized clock edges would be silently lost.
        const bool is_async = e.is_latch || (asyncp.is_const() && !asyncp.is_known_false());
        if (!is_async) {
          auto negp = sink_driver(e.node, "negreset");
          auto test = rstp;
          if (negp.is_const() && !negp.is_known_false()) {
            auto inv = gu::create_typed_node(*g, Ntype_op::Not);
            rstp.connect_sink(livehd::graph_util::setup_sink_pid(inv, 0));
            test = inv.create_driver_pin(0);
            gu::set_bits(test, 1);
            gu::set_unsign(test);
          }
          auto      din  = sink_driver(e.node, "din");
          auto      init = sink_driver(e.node, "initial");
          const int qw   = std::max(1, static_cast<int>(gu::bits_of(e.node.get_driver_pin(0))));
          if (init.is_invalid()) {
            init = gu::create_const(*g, *Dlop::create_integer(0));
          }
          if (!din.is_invalid()) {
            // Mux pin ids: 0 = selector, 1 = arm taken when the selector is 0,
            // 2 = arm taken when it is 1. Reset value on the 1 arm.
            auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
            test.connect_sink(livehd::graph_util::setup_sink_pid(mux, 0));
            din.connect_sink(livehd::graph_util::setup_sink_pid(mux, 1));
            init.connect_sink(livehd::graph_util::setup_sink_pid(mux, 2));
            auto mq = mux.create_driver_pin(0);
            gu::set_bits(mq, qw);
            gu::set_unsign(mq);
            drop_sink(e.node, "din");
            mq.connect_sink(gu::setup_sink_by_name(e.node, "din"));
          }
          if (auto en = sink_driver(e.node, "enable"); !en.is_invalid()) {
            auto orn = gu::create_typed_node(*g, Ntype_op::Or);
            en.connect_sink(livehd::graph_util::setup_sink_pid(orn, 0));
            test.connect_sink(livehd::graph_util::setup_sink_pid(orn, 0));
            auto oq = orn.create_driver_pin(0);
            gu::set_bits(oq, 1);
            gu::set_unsign(oq);
            drop_sink(e.node, "enable");
            oq.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
          }
          drop_sink(e.node, "reset_pin");
          drop_sink(e.node, "negreset");
          // `initial` goes with it. It is BOTH the reset value and the flop's
          // power-on value, and the reset value now lives in the mux above — so
          // keeping the pin leaves a concrete power-on value that the same
          // design's own round-trip (which spells the sync reset in the data
          // path from the start) does not have. Measured: that asymmetry is
          // what the state pairing reports as "kind/init mismatch", and an
          // unpaired reset-less flop gets a FREE per-side constant, i.e. the
          // q(ref=254 impl=255) step-1 counterexample.
          drop_sink(e.node, "initial");
        }
      }

      // enable &= (phase == slot)
      auto pred = slot_pred[e.slot];
      auto old  = sink_driver(e.node, "enable");
      if (old.is_invalid()) {
        pred.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
      } else {
        auto andn = gu::create_typed_node(*g, Ntype_op::And);
        old.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
        pred.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
        auto aq = andn.create_driver_pin(0);
        gu::set_bits(aq, 1);
        gu::set_unsign(aq);
        drop_sink(e.node, "enable");
        aq.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
      }
      ++r.flops_slotted;
    }
  }

  // ---- decision 3A: same-edge readers get the latch's value AT the edge ----
  // After every element is lowered, so each forwarded value is built from the
  // latch's FINAL enable, din and reset.
  if (!plan.forwards.empty()) {
    r.reader_forwards = apply_reader_forwards(*g, plan.forwards);
  }

  // ---- the derived clock cones the rebind left behind -------------------
  // A cone that only ever fed a clock_pin is dead once every element commits
  // on the reference clock, but it is not GONE: the encoder skips a node with
  // no fanout, not a node whose fanout is itself dead, so the head of such a
  // cone is still encoded -- as DATA. For a negedge register behind an ICG
  // (`INV(Clock_cell)` in a mapped netlist) that head reads the timing-only
  // Clock_cell output and the whole def came back UNKNOWN. Remove what became
  // unreachable, stopping at state, instances, IO and anything still read.
  {
    std::vector<hhds::Pin_class>           work = std::move(orphaned_clocks);
    absl::flat_hash_set<hhds::Class_index> gone;  // a DAG reaches a node twice; never touch a deleted one
    while (!work.empty()) {
      auto p = work.back();
      work.pop_back();
      if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p)) {
        continue;
      }
      auto n = p.get_master_node();
      if (gone.contains(n.get_class_index())) {
        continue;
      }
      const auto op = gu::type_op_of(n);
      if (n.is_invalid() || n.has_out_edges() || gu::is_type_register(n) || op == Ntype_op::Sub || op == Ntype_op::Memory) {
        continue;
      }
      for (auto sink : n.inp_sorted_pins()) {
        for (const auto& drv : sink.get_driver_pins()) {
          work.push_back(drv);
        }
      }
      gone.insert(n.get_class_index());
      n.del_node();
    }
  }

  // ---- period-boundary guard on the design's obligations -----------------
  // Keeps ALL period knowledge inside this pass instead of smearing P through
  // Lec_options, both fork-race codecs and the verify cache key. Without it a
  // phase-1 step has one half of the design committed and the other not, so a
  // relational assert (`bq == aq`) is outright FALSE mid-period and plain BMC
  // refutes it well inside the bound.
  if (plan.slots > 1) {
    // The boundary is `phase == 0`, NOT `phase == P-1`. A step READS state and
    // COMMITS at its end, so the state visible at the phase-k step is what
    // committed through slot k-1. The whole period is therefore settled — every
    // slot committed, exactly the end-of-cycle observation model the fixtures
    // pin — at the phase-0 step that STARTS the next period. Traced by hand on
    // flop_verify_posneg: at a phase-1 step `a` has committed and `b` has not,
    // so `bq == aq` is outright false there and plain BMC refutes it.
    const auto boundary = slot_pred[0];
    for (auto n : g->body().nodes()) {
      if (gu::type_op_of(n) != Ntype_op::Sub) {
        continue;
      }
      auto sio = n.get_subnode_io();
      if (sio == nullptr || !gu::is_marker_module_name(sio->get_name())) {
        continue;
      }
      // ASSERTS ONLY. An `fproperty` carries "<kind>\x1f<loc>\x1f<msg>" in its
      // name attr, and the kinds are assert | assert_always | assume. Guarding
      // an ASSUME to the period boundary is not a weakening, it is a semantic
      // change: an environment assume must constrain EVERY step, and
      // `boundary implies cond` leaves the mid-period steps unconstrained. The
      // engine then finds a mid-period input assignment the assume was there to
      // exclude, the assume gets reclassified and refutes, and it takes the
      // correct assert down with it -- a FALSE REFUTED at P>1 on a design whose
      // only sin was having an input assume.
      {
        const auto nm   = std::string(gu::node_name_of(n));
        const auto sep  = nm.find('\x1f');
        const auto kind = sep == std::string::npos ? nm : nm.substr(0, sep);
        if (!kind.empty() && kind.rfind("assert", 0) != 0) {
          continue;  // assume (or any future non-assert kind): leave it alone
        }
      }
      const auto      cond_pid = sio->get_input_port_id("cond");
      hhds::Pin_class cond;
      for (auto sink : n.inp_sorted_pins()) {
        if (sink.get_port_id() == cond_pid) {
          cond = sink.get_driver_pin();
          break;
        }
      }
      if (cond.is_invalid()) {
        continue;
      }
      // boundary implies cond  ==  !boundary | cond
      auto notb = gu::create_typed_node(*g, Ntype_op::Not);
      boundary.connect_sink(livehd::graph_util::setup_sink_pid(notb, 0));
      auto nbq = notb.create_driver_pin(0);
      gu::set_bits(nbq, 1);
      gu::set_unsign(nbq);
      auto orn = gu::create_typed_node(*g, Ntype_op::Or);
      nbq.connect_sink(livehd::graph_util::setup_sink_pid(orn, 0));
      cond.connect_sink(livehd::graph_util::setup_sink_pid(orn, 0));
      auto oq = orn.create_driver_pin(0);
      gu::set_bits(oq, 1);
      gu::set_unsign(oq);
      // Snapshot first: the pin walk is a view over live storage.
      absl::InlinedVector<hhds::Pin_class, 4> doomed;
      for (auto sink : n.inp_sorted_pins()) {
        if (sink.get_port_id() == cond_pid) {
          doomed.push_back(sink);
        }
      }
      for (const auto& sink : doomed) {
        sink.del_sink();
      }
      oq.connect_sink(n.create_sink_pin("cond"));
    }
  }

  r.applied       = true;
  r.slots         = plan.slots;
  r.ref_clock     = plan.ref_clock_name;
  r.clock_domains = plan.domains;
  r.reason        = std::format(
      "P={} slots, {} latch(es) retyped, {} gated clock(s) folded into an enable ({} sampled-held, {} "
             "window-held), {} element(s) slotted, {} memory(ies) slotted over {} write port(s), {} clock domain(s){}{} ({})",
      r.slots,
      r.latches_retyped,
      r.icg_folded,
      r.icg_held,
      r.icg_window_held,
      r.flops_slotted,
      r.mems_slotted,
      r.mem_ports_slotted,
      r.clock_domains,
      plan.domains > 1 ? " exported" + plan.domain_desc : "",
      (r.reader_forwards > 0
                 ? std::format(", {} same-edge reader input(s) forwarded from {} latch read(s)", r.reader_forwards, plan.forwards.size())
                 : std::string{})
          + (r.data_latches_transparent > 0
                 ? std::format(", {} data-gated latch(es) transparent over {} reader input(s)",
                               r.data_latches_transparent,
                               r.data_latch_reads)
                 : std::string{}),
      need.why.empty() ? "def-driven" : need.why);
  livehd::diag::info(kPass, "single-edge-applied", "progress")
      .msg("edge normalization on `{}`: {}", g->get_name(), r.reason)
      .emit();
  return r;
}

}  // namespace livehd::single_edge

// ---------------------------------------------------------------------------
// Eprp surface

static Pass_plugin single_edge_plugin("pass_single_edge", Pass_single_edge::setup);

Pass_single_edge::Pass_single_edge(const Eprp_var& var) : Pass("pass.single_edge", var) {}

void Pass_single_edge::setup() {
  Eprp_method m("pass.single_edge",
                "Edge normalization: remove latches and negedge state so every element is a posedge flop (verification "
                "and simulation only; never on the synthesis path)",
                &Pass_single_edge::work);
  m.add_label_optional("out", "output graph_library directory (the --emit-dir lg: slot)", "");
  m.add_label_optional("forward_same_edge",
                       "true|false. Admit a flop committing on the edge a latch it reads closes on (rule 4) by giving "
                       "that reader alone the latch's value at the edge, instead of refusing the pair",
                       "false");
  m.add_label_optional("transparent_data_latch",
                       "true|false. Give every reader of a DATA-gated latch its open value "
                       "`reset ? RV : (E ? D : Q)` (the latch is level-sensitive); false keeps the "
                       "flop-with-enable lowering, a full cycle late on same-cycle reads",
                       "true");
  m.add_label_optional("multi_clock",
                       "true|false. Export unrelated clock domains (plain posedge state off the reference clock is "
                       "left on its own clock, for a consumer that carries clock ordinals) instead of refusing them",
                       "false");
  register_pass(m);
}

void Pass_single_edge::work(Eprp_var& var) {
  const auto top = std::string{var.get("top", "")};
  const auto out = std::string{var.get("out", "")};

  std::vector<hhds::Graph*> all;
  hhds::Graph*              g = nullptr;
  for (const auto& sp : var.graphs) {
    if (!sp) {
      continue;
    }
    all.push_back(sp.get());
    if (!top.empty() && sp->get_name() == top) {
      g = sp.get();
    }
  }
  if (g == nullptr) {
    if (all.size() == 1) {
      g = all.front();
    } else {
      livehd::diag::err("pass.single_edge", "no-top", "unsupported")
          .msg("pass.single_edge: top module '{}' not found in the input library", top)
          .fatal();
      return;
    }
  }
  std::vector<hhds::Graph*> defs;
  for (auto* d : all) {
    if (d != g) {
      defs.push_back(d);
    }
  }

  // Same pre-step the formal commands run: bring an instantiated clock-gate
  // cell's gate into the body so the fold below can see it.
  if (const int nc = lc::inline_clock_gate_cells(g, "pass.single_edge"); nc > 0) {
    livehd::diag::info("pass.single_edge", "clock-gate-inlined", "progress")
        .msg("pass.single_edge: inlined {} clock-gate cell(s) into `{}`", nc, g->get_name())
        .emit();
  }
  // ...and lift a LEVEL-RESET latch's reset out of its window (see
  // lift_latch_level_resets): `rst | gate` matches no gate shape, `gate` does.
  if (const int nl = lc::lift_latch_level_resets(g); nl > 0) {
    livehd::diag::info("pass.single_edge", "latch-reset-lifted", "progress")
        .msg("pass.single_edge: lifted the level reset of {} latch(es) in `{}` onto reset_pin", nl, g->get_name())
        .emit();
  }
  livehd::single_edge::Options opts;
  opts.multi_clock       = std::string{var.get("multi_clock", "false")} == "true";
  opts.forward_same_edge = std::string{var.get("forward_same_edge", "false")} == "true";
  opts.transparent_data_latch = std::string{var.get("transparent_data_latch", "true")} != "false";
  const auto res         = livehd::single_edge::normalize(g, defs, opts);
  if (res.error) {
    livehd::diag::err("pass.single_edge", "normalize-refused", "unsupported")
        .msg("pass.single_edge refused '{}': {}", g->get_name(), res.reason)
        .fatal();
    return;
  }
  if (!res.applied) {
    livehd::diag::info("pass.single_edge", "single-edge-skipped", "progress")
        .msg("pass.single_edge on `{}`: {}", g->get_name(), res.reason)
        .emit();
  }
  if (!out.empty()) {
    // The rewrite happened IN PLACE in the input library, so emitting is a
    // straight cross-library copy of every reachable module (the caller already
    // refused out == in).
    auto* srclib = g->get_io() != nullptr ? g->get_io()->get_library() : nullptr;
    if (srclib == nullptr) {
      livehd::diag::err("pass.single_edge", "no-library", "internal")
          .msg("pass.single_edge: the input graphs have no owning library to copy from")
          .fatal();
      return;
    }
    auto& outlib = livehd::Hhds_graph_library::instance(out);
    for (const auto& sp : var.graphs) {
      if (sp && !livehd::copy_with_callees(outlib, *srclib, sp->get_name())) {
        livehd::diag::err("pass.single_edge", "copy-failed", "internal")
            .msg("pass.single_edge: could not copy module '{}' into {}", sp->get_name(), out)
            .fatal();
        return;
      }
    }
  }
}
