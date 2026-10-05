//  This file is distributed under the BSD 3-Clause License. See LICENSE for
//  details.

#include "upass_tolg.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "array_dim.hpp"
#include "cell.hpp"
#include "default_prologue.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hlop/dlop.hpp"
#include "io_port_rules.hpp"
#include "json_util.hpp"
#include "latch_contract.hpp"
#include "lnast_ntype.hpp"
#include "mask_eval.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "perf_tracing.hpp"  // TRACE_EVENT — no-op unless built with --define profiling=1
#include "port_reach.hpp"
#include "range_bits.hpp"
#include "split_selfref.hpp"

namespace {

using livehd::graph_util::create_const;
using livehd::graph_util::is_unsign;
using livehd::graph_util::set_bits;
using livehd::graph_util::set_sbits;
using livehd::graph_util::set_sign;
using livehd::graph_util::set_ubits;
using livehd::graph_util::set_unsign;
using livehd::graph_util::setup_sink_by_name;

using Pin      = hhds::Pin_class;
using WriteMap = absl::flat_hash_map<std::string, Pin>;

// The DRIVEN sink pin of `node` at port `pid`, or an invalid pin when that port
// carries no driver.
//
// ONE DRIVER PER SINK PIN, so "the edge into pid" and "the pin at pid" are the
// same thing and the answer is a pin, not an edge. inp_sorted_pins() is a
// read-only view over live storage, so every mutating caller below re-drives
// AFTER the walk has ended (the old shape -- del_edge() then break -- relied on
// inp_edges() materializing a snapshot).
[[nodiscard]] inline Pin driven_sink_at(const hhds::Node_class& node, uint64_t pid) {
  for (auto sink : node.inp_sorted_pins()) {
    if (static_cast<uint64_t>(sink.get_port_id()) == pid) {
      return sink;
    }
  }
  return Pin{};
}

// TRUE when `a` and `b` compute the same value: one driver, equal constants,
// the same combinational cell over pairwise-equal operands, or two reads of one
// memory at equal addresses. A re-evaluated expression is two cells, not one
// pin: prp2lnast lowers the index of `mem[a + 1]#[..] = v` once for the element
// read and again for the store, with no write in between (which is what makes
// two reads of one memory equal here).
[[nodiscard]] bool same_value(const Pin& a, const Pin& b, int depth = 8) {
  if (a == b) {
    return true;
  }
  if (a.is_invalid() || b.is_invalid() || depth == 0) {
    return false;
  }
  if (a.is_const() || b.is_const()) {
    return a.is_const() && b.is_const() && livehd::graph_util::const_of(a).same_repr(livehd::graph_util::const_of(b));
  }
  const auto na = a.get_master_node();
  const auto nb = b.get_master_node();
  const auto op = livehd::graph_util::type_op_of(na);
  if (op == Ntype_op::Memory && na == nb) {
    // Read port k drives dout pid k and takes its address at pid k * stride.
    const auto stride = static_cast<uint64_t>(Ntype::Memory_port_stride);
    const auto sa     = driven_sink_at(na, static_cast<uint64_t>(a.get_port_id()) * stride);
    const auto sb     = driven_sink_at(nb, static_cast<uint64_t>(b.get_port_id()) * stride);
    return !sa.is_invalid() && !sb.is_invalid() && same_value(sa.get_driver_pin(), sb.get_driver_pin(), depth - 1);
  }
  if (!Ntype::is_comb(op) || op != livehd::graph_util::type_op_of(nb) || a.get_port_id() != b.get_port_id()
      || livehd::graph_util::bits_of(a) != livehd::graph_util::bits_of(b)) {
    return false;
  }
  const auto sa = na.inp_pins_snapshot();
  const auto sb = nb.inp_pins_snapshot();
  if (sa.size() != sb.size()) {
    return false;
  }
  for (size_t p = 0; p < sa.size(); ++p) {
    const auto da = sa[p].get_driver_pins();
    const auto db = sb[p].get_driver_pins();
    if (sa[p].get_port_id() != sb[p].get_port_id() || da.size() != db.size()) {
      return false;
    }
    for (size_t i = 0; i < da.size(); ++i) {
      if (!same_value(da[i], db[i], depth - 1)) {
        return false;
      }
    }
  }
  return true;
}

using upass::io_port::comb_port_is_dead;
using upass::io_port::declares_input_default;
using upass::io_port::input_default_const;
using upass::io_port::is_clock_candidate;
using upass::io_port::is_clock_input;
using upass::io_port::is_reset_candidate;
using upass::io_port::is_reset_input;
using upass::io_port::reset_input_active_low;
using upass::io_port::reset_name_active_low;
using upass::io_port::verilog_clock_name;
using upass::io_port::verilog_reset_name;

// One lowered value: its driver pin plus the literal container width `mw`.
// Despite the historical name, this is now the same unit as attrs::bits for
// both signed and unsigned values.
struct Val {
  Pin     pin;
  int32_t mw{0};
};

// Bits to represent a non-negative value as unsigned (>=1).
// Minimal literal width needed by a constant used in an inferred expression.
//
// A NEGATIVE value used to collapse to 1 here, whatever its magnitude, so every
// width computed from it was too small: `(-3) << ua` sized its result from
// mw(-3)==1 instead of 2 and produced a 9-bit intermediate for a shift that
// needs 10, silently wrapping `-3 << 7` from -384 to +128. Size a negative by
// its magnitude, exactly like a positive.
[[nodiscard]] int32_t mw_of_val(int64_t v) {
  if (v == 0) {
    return 1;
  }
  if (v < 0) {
    // Two's-complement signed width: -1 needs one bit, -3 needs three.
    return static_cast<int32_t>(std::bit_width(static_cast<uint64_t>(~v)) + 1U);
  }
  return std::max<int32_t>(1, static_cast<int32_t>(std::bit_width(static_cast<uint64_t>(v))));
}

// Literal width of a driver pin: its stamped bits, or — for a const pin
// (which carries no bits stamp) — the bits needed for its value. Used to size
// a merged mux/hotmux to the WIDEST arm so a narrow (e.g. const) arm does not
// truncate the wider ones. Returns 0 for an unstamped non-const pin.
[[nodiscard]] int32_t pin_mw_of(const Pin& p) {
  if (auto bb = livehd::graph_util::bits_of(p); bb > 0) {
    return bb;
  }
  if (livehd::graph_util::is_graph_input_pin(p) && p.get_graph() != nullptr) {
    if (const auto gio = p.get_graph()->get_io(); gio) {
      if (const auto bb = livehd::graph_util::bits_of(p, *gio, p.get_pin_name()); bb > 0) {
        return bb;
      }
    }
  }
  if (p.is_const()) {
    const auto& v = livehd::graph_util::const_of(p);
    if (v.is_just_i64()) {
      return mw_of_val(v.to_just_i64());
    }
    // The literal PAYLOAD width, not Dlop's signed carrier: a non-negative
    // constant (an unsigned unknown such as `0ub?` included) carries one
    // leading zero beyond its payload, and that headroom must not widen a
    // Mux/Hotmux arm or an enclosing Concat lane.
    return std::max<int32_t>(1, v.get_payload_bits());
  }
  return 0;
}

// Can the value on this pin be NEGATIVE?
//
// An UNSIGNED hint proves non-negativity; a SIGNED pin may be negative. A
// CONSTANT pin carries no signed hint at all -- the same
// trap Cgen_verilog::operand_reads_signed documents -- so ask its VALUE instead
// of its stamp, or a literal `-2` arm reads as non-negative.
[[nodiscard]] bool pin_can_be_negative(const Pin& p) {
  if (p.is_invalid()) {
    return false;
  }
  if (p.is_const()) {
    const auto& v = livehd::graph_util::const_of(p);
    return !v.has_unknowns() && v.is_negative();
  }
  return !is_unsign(p);
}

// `hi - lo` when `hi` is `lo` plus a constant in [0, 4096) -- `lo` itself, or
// a chain of Sums that each add/subtract constants to one running value (the
// `x#[lo..+W]` shape is `(lo + W) - 1`, a Verilog `+:`/`-:` window `lo + W-1`),
// every Sum wide enough not to wrap -- else nullopt.
[[nodiscard]] std::optional<int64_t> const_window_offset(const Pin& hi, const Pin& lo, int depth = 4) {
  if (same_value(hi, lo)) {
    return 0;
  }
  if (depth == 0 || hi.is_invalid() || hi.is_const() || hi.get_port_id() != 0) {
    return std::nullopt;
  }
  const auto node = hi.get_master_node();
  if (livehd::graph_util::type_op_of(node) != Ntype_op::Sum) {
    return std::nullopt;
  }
  int64_t c = 0;
  Pin     run;  // the one non-constant operand
  int32_t in_mw = 0;
  for (const auto* bank : {"as", "bs"}) {
    for (const auto& d : livehd::graph_util::inp_drivers_of(node, bank)) {
      in_mw = std::max(in_mw, pin_mw_of(d));
      if (!d.is_const()) {
        if (!run.is_invalid() || *bank == 'b') {
          return std::nullopt;
        }
        run = d;
        continue;
      }
      const auto& v = livehd::graph_util::const_of(d);
      if (!v.is_just_i64() || std::abs(v.to_just_i64()) >= 4096) {
        return std::nullopt;
      }
      c += *bank == 'a' ? v.to_just_i64() : -v.to_just_i64();
    }
  }
  if (run.is_invalid() || pin_mw_of(hi) <= in_mw) {
    return std::nullopt;
  }
  const auto inner = const_window_offset(run, lo, depth - 1);
  if (!inner || *inner + c < 0 || *inner + c >= 4096) {
    return std::nullopt;
  }
  return *inner + c;
}

// Resolve a func_call callee name against the lnast registry the
// same way the runner's lookup_callee does: exact top-module-name match, then
// a lexical match on the CALLER's own unit-name prefix chain (streamed nested
// helpers register as "<file>.<outer>.<name>", and two same-named helpers in
// sibling scopes otherwise make the suffix scan ambiguous), else a UNIQUE
// "<module>.<name>" suffix match.
[[nodiscard]] std::shared_ptr<Lnast> resolve_callee_lnast(std::string_view                           name,
                                                          const std::vector<std::shared_ptr<Lnast>>& registry,
                                                          std::string_view caller_unit = {}, bool want_template = false) {
  // Escaped Verilog module references retain LNAST backticks until tolg;
  // registry keys carry the literal module name.
  if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
    name = name.substr(1, name.size() - 2);
  }
  // A TEMPLATE is never the answer to a lowering lookup: it mints no GraphIO
  // (register_io) and lowers to nothing (run), so any call that survived to
  // tolg is served by a specialization. It also SHARES its name with an
  // IDENTITY specialization (maybe_specialize_template_call), which would
  // otherwise make that name look ambiguous to the suffix scan below and
  // resolve to nothing at all. `want_template` inverts the filter, ONLY for
  // the diagnostic of a call that reached tolg still naming a generic callee
  // (the runner never specialized it) — the same scan, so the two answers
  // cannot drift apart.
  const auto candidate = [want_template](const std::shared_ptr<Lnast>& ln) { return ln && ln->is_template() == want_template; };
  std::shared_ptr<Lnast> exact;
  std::shared_ptr<Lnast> suffix_hit;
  int                    suffix_matches = 0;
  const std::string      suffix         = "." + std::string(name);
  for (const auto& ln : registry) {
    if (!candidate(ln)) {
      continue;
    }
    auto n = ln->get_top_module_name();
    if ((n == name)) {
      exact = ln;
    } else if (n.size() > suffix.size() && str_tools::ends_with(n, suffix)) {
      suffix_hit = ln;
      ++suffix_matches;
    }
  }
  if (exact) {
    return exact;
  }
  if (!caller_unit.empty()) {
    std::string scoped;
    for (std::string_view unit = caller_unit;;) {
      scoped.assign(unit);
      scoped.push_back('.');
      scoped.append(name);
      for (const auto& ln : registry) {
        if (candidate(ln) && ln->get_top_module_name() == scoped) {
          return ln;
        }
      }
      const auto dot = unit.rfind('.');
      if (dot == std::string_view::npos) {
        break;
      }
      unit = unit.substr(0, dot);
    }
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

// A pipe/mod instance's declared landing interval, PER OUTPUT: (Sub nid,
// output port id) -> (min, max) cycles after the instance's input cycle. Each
// output keeps its own interval, so a multi-output child whose outputs land at
// different cycles (`vld@[1]`, `q@[0]`) is seen at each one.
using Sub_out_times = absl::flat_hash_map<std::pair<uint64_t, uint64_t>, std::pair<int64_t, int64_t>>;

// The lowering pass in flight (reset by uPass_tolg::detect_lg_collisions): the
// graph names of the units still waiting to be lowered. A callee body under
// one of those names is absent or left over from an earlier build, so nothing
// may read through it yet.
struct Lowering_pass {
  absl::flat_hash_set<std::string>               pending;
  // graph name -> the unit that lowers to it (a Sub's callee, for its port
  // types); a miss is memoized as nullptr (an lg: black box).
  absl::flat_hash_map<std::string, const Lnast*> units;
};

Lowering_pass& lowering_pass() {
  static thread_local Lowering_pass state;
  return state;
}

// The combinational reach the loop checks splice for a callee: a crossbar
// while the callee waits to be lowered in this pass, else the summary tolg
// recorded when it finished the body (port_reach::stamp in uPass_tolg::run).
// That record survives the compile cache, so a restored callee, whose body is
// already optimized, reads exactly as it did when it was lowered. A body tolg
// did not lower (an absorbed lg: library) carries none and is walked.
std::optional<livehd::port_reach::Def_reach> callee_reach(const std::shared_ptr<hhds::Graph>& g) {
  if (lowering_pass().pending.contains(g->get_name())) {
    return livehd::port_reach::crossbar(*g);
  }
  return livehd::port_reach::stamped(*g);
}

// User ruling 2026-09-28 (27): a register whose clock resolves to a constant
// is a compile error. These read it off LOWERED bodies: a callee is lowered
// before its callers (lowering_order), and a body the compile cache restored
// reads the same, so the answer never depends on the cache state. Every copy,
// alias and typed read of a clock is just an edge (or a Get_mask) here.

// The register a constant clock reaches: the one a constant-clock error names.
struct Clocked_state {
  std::string        name{};          // "" for an anonymous (compiler-inserted) register
  std::string        owner{};         // the graph (module) declaring it
  livehd::diag::Span span{};          // its declaration, when its graph records one
  bool               memory = false;  // a memory port's clock, not a flop's
  int                port   = -1;     // that memory port, when its ports have their own clocks
  std::string        enable_input{};  // the body input enabling that port ("" local): bound to 0, it is idle
};

// What the memory clock sink `clock_sink` clocks: the ports it drives (every
// port when the memory has one clock) that can commit -- a write, or a read of
// a sync memory -- as the body input enabling each ("" when the enable is local
// logic). A port whose enable is held at 0 never commits (LiveHD's own ROM
// emission ties an idle write port's clock off) and an async read needs no
// clock: empty when nothing on this clock commits.
[[nodiscard]] std::vector<std::string> mem_clock_users(const hhds::Node_class& mem, hhds::Port_id clock_sink) {
  namespace gu                = livehd::graph_util;
  constexpr int       stride  = static_cast<int>(Ntype::Memory_port_stride);
  const auto          sink_of = [](std::string_view name) { return static_cast<int>(Ntype::get_sink_pid(Ntype_op::Memory, name)); };
  std::map<int, Pin>  en;  // port (every port has an address) -> its enable driver, if any
  std::map<int, bool> rd;  // port -> a read port
  int                 n_clocks = 0;
  bool                sync_rd  = false;
  bool                bulk     = false;      // a whole-array `update` commits on the clock too
  for (auto sink : mem.inp_sorted_pins()) {  // read-only walk
    const auto pid = static_cast<int>(sink.get_port_id());
    if (pid == sink_of("update")) {
      bulk = true;
    } else if (pid % stride == sink_of("addr")) {
      en.try_emplace(pid / stride);
    } else if (pid % stride == sink_of("clock_pin")) {
      ++n_clocks;
    } else if (pid % stride == sink_of("enable")) {
      en[pid / stride] = sink.get_driver_pin();
    } else if (pid % stride == sink_of("rdport")) {
      rd[pid / stride] = sink.get_driver_pin().is_const() && gu::const_of(sink.get_driver_pin()).is_known_true();
    } else if (pid == sink_of("type")) {
      sync_rd = sink.get_driver_pin().is_const() && gu::const_of(sink.get_driver_pin()).is_just_i64()
                && gu::const_of(sink.get_driver_pin()).to_just_i64() == 1;
    }
  }
  std::vector<std::string> users;
  if (bulk) {
    users.emplace_back();
  }
  for (const auto& [port, drv] : en) {
    if ((n_clocks > 1 && port != static_cast<int>(clock_sink) / stride) || (rd.contains(port) && rd.at(port) && !sync_rd)) {
      continue;
    }
    const auto root = drv.is_invalid() ? livehd::latch_contract::Control_root{} : livehd::latch_contract::control_root(drv);
    if (root.net.is_const()) {
      if (!(root.inverted ? root.net.is_known_true() : root.net.is_known_false())) {
        users.emplace_back();
      }
    } else if (!root.net.is_invalid() && gu::is_graph_input_pin(root.net) && !root.inverted) {
      users.emplace_back(root.net.get_pin_name());
    } else {
      users.emplace_back();
    }
  }
  return users;
}

// A memory's clock sink `pid` as the port a diagnostic names: -1 when the
// memory has a single clock (it clocks every port).
[[nodiscard]] int mem_clock_port(const hhds::Node_class& mem, hhds::Port_id pid) {
  constexpr auto stride    = static_cast<int>(Ntype::Memory_port_stride);
  const auto     clock_pid = static_cast<int>(Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin"));
  int            n_clocks  = 0;
  for (auto sink : mem.inp_sorted_pins()) {  // read-only walk
    if (static_cast<int>(sink.get_port_id()) % stride == clock_pid) {
      ++n_clocks;
    }
  }
  return n_clocks > 1 ? static_cast<int>(pid) / stride : -1;
}

// "memory `m`", "memory `m` (port 2)" or "register `r`" for a diagnostic.
[[nodiscard]] std::string clocked_state_label(std::string_view kind, std::string_view name, int port) {
  auto what = name.empty() ? std::format("a pipeline {}", kind) : std::format("{} `{}`", kind, name);
  return port < 0 ? what : std::format("{} (port {})", what, port);
}

// What a constant on one input of a body reaches through clock-path cells: a
// register it clocks (it wins: nothing idles a register), every live memory
// port it clocks (each idle only when its own enable is held at 0), and the
// outputs it holds constant (a clock buffer or gate cell that its
// instantiator clocks state with).
struct Const_clock_reach {
  std::optional<Clocked_state>              state;
  std::vector<Clocked_state>                memory_ports;
  std::vector<std::pair<std::string, Dlop>> outputs;
};

// const_clock_reach answers per (body, input, constant). uPass_tolg::run
// clears it before lowering each unit, so a relowered body is never read stale.
absl::node_hash_map<std::string, Const_clock_reach>& const_clock_memo() {
  static thread_local absl::node_hash_map<std::string, Const_clock_reach> memo;
  return memo;
}

[[nodiscard]] const Const_clock_reach& const_clock_reach(const std::shared_ptr<hhds::Graph>& g, std::string_view input,
                                                         const Dlop& value);

// A forward walk's constant: the value on every edge out of `driver`.
struct Clock_given {
  hhds::Pin_class driver;
  const Dlop*     value = nullptr;
};

constexpr int kClockHops = 8;  // bounds a backward clock-path fold

[[nodiscard]] std::optional<Dlop> fold_clock_cell(const hhds::Pin_class& out, const Clock_given& given, int hops);

// The constant the clock-path value `d` settles to, or nullopt while it can
// still toggle.
[[nodiscard]] std::optional<Dlop> clock_const_of(const hhds::Pin_class& d, const Clock_given& given, int hops) {
  if (d.is_invalid() || hops <= 0) {
    return std::nullopt;
  }
  if (given.value != nullptr && d == given.driver) {
    return *given.value;
  }
  if (d.is_const()) {
    return livehd::graph_util::const_of(d);
  }
  if (livehd::graph_util::is_graph_input_pin(d)) {
    return std::nullopt;
  }
  auto v = fold_clock_cell(d, given, hops);
  return v && v->is_numeric() ? v : std::nullopt;
}

// Bit 0 of a clock-path value: all a clock pin samples.
[[nodiscard]] Dlop clock_bit(const Dlop& v) { return livehd::eval_get_mask(v, 0); }

// The constant the cell driving `out` settles to, its operands folded by
// clock_const_of. Only the cells a clock or a gate enable legally crosses
// fold: Not, Get_mask and Sext (a typed read), EQ (a bool inversion), And (a
// gate: one known-0 operand holds it at 0), Or/Xor (a gate enable built from
// constants; a known-1 operand holds an Or at 1) and a Mux with a constant
// select, a Clock_cell (a constant reference, or an enable held at 0, stops
// it), and an instance output its body holds constant. An unconnected clock
// (`x`, rulings 41/80) never ticks through a gate either: an unknown operand
// of an And/Or/Xor is the gate's value unless a known one dominates it.
[[nodiscard]] std::optional<Dlop> fold_clock_cell(const hhds::Pin_class& out, const Clock_given& given, int hops) {
  namespace gu        = livehd::graph_util;
  const auto n        = out.get_master_node();
  auto       operand  = [&](const hhds::Pin_class& drv) { return clock_const_of(drv, given, hops - 1); };
  auto       operands = [&](std::string_view a, std::string_view b) {
    return std::pair{operand(gu::get_driver_of_sink_name(n, a)), operand(gu::get_driver_of_sink_name(n, b))};
  };
  // An And/Or/Xor gate's operands, folded. An operand `dominant` accepts (an
  // And's known 0, an Or's known-1 clock bit) holds the gate whatever the
  // others do; failing that, an unknown clock bit does.
  auto gate = [&](auto&& dominant, auto&& combine) -> std::optional<Dlop> {
    std::vector<std::optional<Dlop>> vals;
    for (const auto& op : gu::inp_sink_drivers(n)) {
      vals.push_back(operand(op.driver));
    }
    for (const auto& v : vals) {
      if (v && dominant(*v)) {
        return v;
      }
    }
    for (const auto& v : vals) {
      if (v && clock_bit(*v).has_unknowns()) {
        return clock_bit(*v);
      }
    }
    std::optional<Dlop> acc;
    for (const auto& v : vals) {
      if (!v) {
        return std::nullopt;
      }
      acc = acc ? combine(*acc, *v) : *v;
    }
    return acc;
  };
  const auto op_kind = gu::type_op_of(n);
  switch (op_kind) {
    case Ntype_op::Not: {
      const auto a = operand(gu::first_value_driver(n));
      return a ? std::optional<Dlop>(*a->not_op()) : std::nullopt;
    }
    case Ntype_op::Get_mask: {
      const auto a     = operand(gu::get_driver_of_sink_name(n, "a"));
      const auto range = gu::bit_range(n);
      if (!a || !range) {
        return std::nullopt;
      }
      return livehd::eval_get_mask(*a, range->first, range->second);
    }

    case Ntype_op::Sext: {
      const auto [a, b] = operands("a", "b");
      return a && b ? std::optional<Dlop>(*a->sext_op(*b)) : std::nullopt;
    }
    case Ntype_op::And:
      return gate([](const Dlop& v) { return v.is_known_zero(); }, [](const Dlop& a, const Dlop& b) { return *a.and_op(b); });
    case Ntype_op::Or:  // `clk | 1` never ticks
      return gate([](const Dlop& v) { return v.is_known_true() && clock_bit(v).is_known_true(); },
                  [](const Dlop& a, const Dlop& b) { return *a.or_op(b); });
    case Ntype_op::Xor: return gate([](const Dlop&) { return false; }, [](const Dlop& a, const Dlop& b) { return *a.xor_op(b); });
    case Ntype_op::Mux: {  // a constant select (sink 0) picks one arm (sink sel+1)
      const auto ops = gu::inp_sink_drivers(n);
      const auto sel = ops.empty() || ops.front().get_port_id() != 0 ? std::nullopt : operand(ops.front().driver);
      if (!sel || !sel->is_just_i64() || sel->to_just_i64() < 0) {
        return std::nullopt;
      }
      const auto arm = static_cast<hhds::Port_id>(sel->to_just_i64() + 1);
      for (const auto& op : ops) {
        if (op.get_port_id() == arm) {
          return operand(op.driver);
        }
      }
      return std::nullopt;
    }
    case Ntype_op::EQ: {  // a bool `not clk` lowers to `clk == 0`
      std::optional<Dlop> first;
      std::optional<Dlop> all_eq;
      for (const auto& op : gu::inp_sink_drivers(n)) {
        auto v = operand(op.driver);
        if (!v) {
          return std::nullopt;
        }
        if (!first) {
          first = std::move(v);
          continue;
        }
        const auto eq = first->eq_op(*v);
        all_eq        = all_eq ? *all_eq->and_op(*eq) : *eq;
      }
      return all_eq;
    }
    case Ntype_op::Clock_cell: {
      const auto [ref, en] = operands("clk_ref", "en");
      if (ref || (en && en->is_known_zero())) {
        return *Dlop::create_integer(0);  // stopped: it never takes an edge
      }
      return std::nullopt;
    }
    case Ntype_op::Sub: {
      const auto child = n.get_subnode_graph();
      if (!child || n.is_loop_subnode() || lowering_pass().pending.contains(child->get_name())) {
        return std::nullopt;
      }
      const auto oname = out.get_pin_name();
      for (const auto& op : gu::inp_sink_drivers(n)) {
        if (const auto v = operand(op.driver)) {
          for (const auto& [o, ov] : const_clock_reach(child, op.sink.get_pin_name(), *v).outputs) {
            if (o == oname) {
              return ov;
            }
          }
        }
      }
      return std::nullopt;
    }
    default: return std::nullopt;
  }
}

// Walk constant `value` forward from input `input` of body `g`: through the
// clock-path cells fold_clock_cell folds and into instances (whose bodies are
// lowered first), stopping at the first flop clock it reaches.
const Const_clock_reach& const_clock_reach(const std::shared_ptr<hhds::Graph>& g, std::string_view input, const Dlop& value) {
  namespace gu = livehd::graph_util;
  auto& memo   = const_clock_memo();
  auto  key    = std::format("{}\n{}\n{}", g->get_name(), input, value.to_string());
  if (const auto it = memo.find(key); it != memo.end()) {
    return it->second;  // an instantiation cycle reads the entry still being filled
  }
  auto& out = memo[std::move(key)];
  if (!g->get_io()->has_input(input)) {
    return out;
  }
  auto clock_pid = [](Ntype_op op) { return Ntype::get_sink_pid(op, "clock_pin"); };
  auto state_of  = [&](const hhds::Node_class& n, bool memory) {
    Clocked_state st{.name = std::string(gu::node_name_of(n)), .memory = memory};
    if (st.name.starts_with('%')) {
      st.name.clear();  // a compiler temp (`past`'s `%q_0`) is no source name
    } else if (st.name.size() >= 2 && st.name.front() == '`' && st.name.back() == '`') {
      st.name = st.name.substr(1, st.name.size() - 2);  // a quoted id (slang's `q[0]` bit)
    }
    st.owner = std::string(g->get_name());
    if (const auto id = n.attr(hhds::attrs::srcid); id.has()) {
      st.span = g->source_locator().resolve_span(id.get());
    }
    return st;
  };
  std::vector<std::pair<hhds::Pin_class, Dlop>> work;
  absl::flat_hash_set<hhds::Pin_class>          seen;
  work.emplace_back(g->get_input_pin(input), value);
  while (!work.empty()) {
    const auto [d, v] = std::move(work.back());
    work.pop_back();
    if (!seen.insert(d).second) {
      continue;
    }
    for (const auto& e : d.out_edges()) {
      if (gu::is_graph_output_pin(e.sink)) {
        out.outputs.emplace_back(std::string(e.sink.get_pin_name()), v);
        continue;
      }
      const auto n   = e.sink.get_master_node();
      const auto op  = gu::type_op_of(n);
      const auto pid = e.sink.get_port_id();
      if ((op == Ntype_op::Flop || op == Ntype_op::Fflop) && pid == clock_pid(op)) {
        out.state = state_of(n, false);
        return out;
      }
      if (op == Ntype_op::Memory) {
        if (pid % Ntype::Memory_port_stride == clock_pid(op)) {
          for (auto& en : mem_clock_users(n, pid)) {
            auto& port        = out.memory_ports.emplace_back(state_of(n, true));
            port.port         = mem_clock_port(n, pid);
            port.enable_input = std::move(en);
          }
        }
        continue;
      }
      if (op == Ntype_op::Sub) {
        const auto child = n.get_subnode_graph();
        if (!child || lowering_pass().pending.contains(child->get_name())) {
          continue;
        }
        const auto& r = const_clock_reach(child, e.sink.get_pin_name(), v);
        if (r.state) {
          out.state = r.state;
          return out;
        }
        for (const auto& m : r.memory_ports) {
          out.memory_ports.push_back(m);  // `m.owner` is the child: this body's call cannot idle it
        }
        if (n.is_loop_subnode()) {
          continue;  // a rolled loop's outputs are its carries, not a clock
        }
        for (const auto& [o, ov] : r.outputs) {
          for (auto dp : n.out_sorted_pins()) {
            if (dp.get_pin_name() == o) {
              work.emplace_back(dp, ov);
            }
          }
        }
        continue;
      }
      for (auto dp : n.out_sorted_pins()) {
        if (auto folded = fold_clock_cell(dp, Clock_given{d, &v}, kClockHops)) {
          work.emplace_back(dp, std::move(*folded));
        }
      }
    }
  }
  return out;
}

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
  std::string valid_name;
  bool        valid_minted = false;
  bool        valid_active = false;
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
      , reset_async_default_(async_default)
      , valid_name_(std::move(io_setup.valid_name))
      , valid_minted_(io_setup.valid_minted)
      , valid_active_(io_setup.valid_active) {}

private:
  // Deferred stage-reg creation: a declare(reg)+stages does NOT
  // create the Flop immediately; the din store does, because only there the
  // effective depth is known (a Sub-fed stage reg realizes the DEFICIT
  // stage_N − callee_min; depth 0 = plain wire, no Flop at all). min/max ride
  // as raw const texts so "nil" stays distinguishable from 0.
  struct Pending_stage {
    std::string min_txt;
    std::string max_txt;
    Lnast_nid   decl_nid;        // for located diagnostics
    int32_t     decl_color = 0;  // block region at the declare (2opt-freq B)
  };

  // Per call-result name: the callee output's declared stages
  // interval + kind, recorded when the Sub is created and consumed by the
  // following stage-reg din store for the range check + deficit narrowing.
  struct Sub_out {
    int64_t          cmin    = 0;
    int64_t          cmax    = 0;  // pipe convention: 0 with cmin>=1 = unconstrained
    bool             is_pipe = false;
    hhds::Node_class node;         // to re-stamp time_range when stage[N] pins the pick
    uint64_t         out_pid = 0;  // the result's output port on `node`
  };

public:
  void build() {
    if (const auto program = lnast_->get_simulation_init(); !program.empty()) {
      g_->get_input_node().attr(livehd::attrs::simulation_init).set(program);
    }
    index_mem_write_sites();
    // Module anchor: graph io nodes
    // cgen reads for the module header, at the unit's `mod`/`comb` declaration
    // (stamped on the LNAST root by func_extract / the specialize clone).
    if (const auto id = lnast_->get_srcid(lnast_->get_root()); id != hhds::SourceId_invalid) {
      cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
    }

    // Inputs: from io_meta(). LGraph values are signed, unbounded integers;
    // `unsign` records the non-negative range of an unsigned source port. The
    // physical W-bit -> non-negative boundary conversion belongs in each
    // backend, not in the graph as a Get_mask operation.
    for (const auto& e : lnast_->io_meta().inputs) {
      const std::string ename{canon_io_name(e.name)};    // strip slang's `` `ar.x` `` marker
      auto              raw = g_->get_input_pin(ename);  // body driver pin for the port
      if (cur_srcid_ != hhds::SourceId_invalid && !raw.is_invalid()) {
        raw.get_master_node().attr(hhds::attrs::srcid).set(cur_srcid_);
      }
      int32_t mw = io_mw(e);
      // A port's declared width is a real declared type (io_meta carries it
      // straight from the signature), so it can size a Concat lane. An
      // UNBOUNDED `int`/`unsigned` port has io_mw 0 and records nothing, which
      // is what makes `concat(unbounded_port, b)` the intended hard error.
      record_decl_type(e.name, e.kind == Io_kind::boolean ? int32_t{1} : mw, e.kind == Io_kind::boolean ? false : e.is_signed);
      if (e.kind == Io_kind::boolean) {
        set_ubits(raw, 1);
        record(e.name, raw, 1);
      } else if (mw <= 1) {
        set_bits(raw, 1);
        if (e.is_signed) {
          set_sign(raw);
          record(e.name, raw, 1);
        } else {
          set_unsign(raw);
          record(e.name, raw, 1);
        }
      } else if (e.is_signed) {
        set_bits(raw, mw);
        set_sign(raw);
        record(e.name, raw, mw);
      } else {
        // Stamp width and the non-negative range directly on the input pin.
        // Backends still know the GraphIO declaration is physically W bits.
        set_ubits(raw, mw);
        record(e.name, raw, mw);
      }
      if (e.array_size > 0) {
        // `a:[N]T` port: the packed bus above, plus the lane view that makes
        // `a[i]` lower exactly like a body `mut` array (a rolled loop's array
        // carry crosses the lifted boundary this way).
        array_scalar_views_[e.name] = array_view_of(e);
      }
    }
    for (const auto& e : lnast_->io_meta().outputs) {
      if (e.array_size > 0 && !array_scalar_views_.contains(e.name)) {
        array_scalar_views_[e.name] = array_view_of(e);
      }
      // An OUTPUT port's declared width is a declared type just like an
      // input's, and it is the common destination of a concat (`z:u6 = ...`).
      // Recording it here rather than only at a `declare` is what lets
      // check_concat_dest_width see a signature-declared port at all.
      record_decl_type(e.name,
                       e.kind == Io_kind::boolean ? int32_t{1} : io_mw(e),
                       e.kind == Io_kind::boolean ? false : e.is_signed);
      // An output starts as nil, exactly like `mut v:uN = nil`: a first
      // `out#[lo..=hi] = x` (no whole write before it) builds on a zero base
      // at the port's declared width (set_mask_base).
      if (e.array_size == 0) {
        scalar_decl_.insert(std::string(canon_io_name(e.name)));
      }
      if (cur_srcid_ != hhds::SourceId_invalid) {
        if (auto sink = g_->get_output_pin(canon_io_name(e.name)); !sink.is_invalid()) {
          sink.get_master_node().attr(hhds::attrs::srcid).set(cur_srcid_);
        }
      }
    }

    // Body: lower the `stmts` child of `top`.
    auto top = lnast_->get_root();
    decl_reset_pin_.clear();  // one unit per walk; never carry a name across modules
    collect_decl_reset_pins(top);
    for (auto c = lnast_->get_first_child(top); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (Lnast_ntype::is_stmts(lnast_->get_type(c))) {
        mark_default_only_prologue(c);
        lower_stmts(c);
      }
    }
    // Walk done: drop the statement anchor so finalize-time diagnostics are
    // unlocated rather than mislocated at whatever statement came last
    // (finalize_regs / create_stage_flop re-anchor per entity).
    cur_srcid_ = hhds::SourceId_invalid;
    cur_color_ = 0;

    // Wire every declared reg's din/enable/reset/initial now that
    // all stores and per-reg attr overrides have been seen.
    finalize_regs();
    cur_color_ = 0;  // the last reg's region must not leak into mem/output glue
    // Sanity-check the per-memory port allocation.
    {
      const auto saved_srcid = cur_srcid_;  // finalize_mems anchors at each memory's declare
      finalize_mems();
      cur_srcid_ = saved_srcid;
    }
    // Bind any deferred field reads (forward references to a call result
    // lowered later) now that every call's Sub result exists.
    resolve_pending_tgets();
    // 2c-wire — wire each `wire` net's buffer input to its single accumulated
    // driver (position-independent reads already bind to the buffer output),
    // and enforce the single-driver / undriven rules. Runs after finalize_regs
    // so a `reset_pin = <wire>` resolves the wire's buffer pin, and after
    // resolve_pending_tgets so a driver that reads a forward call result is
    // bound first.
    finalize_wires();
    cur_color_ = 0;  // the last wire's region must not leak into output glue
    check_nil_array_outputs();

    // Outputs: connect each output's bound driver to its graph output sink. The
    // GraphIO carries the port widths cgen emits; fetch it so an unbounded
    // output can be sized from its (now-lowered) driver below.
    auto out_gio = lib_ != nullptr ? lib_->find_io(std::string(lnast_->get_graph_name())) : nullptr;
    for (const auto& e : lnast_->io_meta().outputs) {
      const std::string ename{canon_io_name(e.name)};  // strip slang's `` `p.q` `` marker
      auto              sink = g_->get_output_pin(ename);
      if (sink.is_invalid()) {
        continue;
      }
      auto it = pin_map_.find(ename);
      if (it == pin_map_.end()) {
        // Every declared output must be assigned. A Verilog-origin module's
        // legally-undriven output (defaults to X) is already poison-inited to
        // `0sb?` at body top by inou.slang, so it never reaches here — reaching
        // here now genuinely means a Pyrope output the body forgot to drive.
        error_at(Lnast_nid{},
                 {"undriven-output", "type"},
                 "output '{}' is never driven by the body of '{}' — every "
                 "declared output must be assigned",
                 e.name,
                 lnast_->get_top_module_name());
        continue;
      }
      sink.connect_driver(it->second);
      // An UNBOUNDED output (`int`/`unsigned`, no declared width — io_meta bits
      // == 0) takes its driver's width+sign: 07-typesystem says an
      // unconstrained signal "uses whatever current value is found". Without
      // this the GraphIO port kept the setup_io_impl default of 1 bit (io_meta
      // e.bits==0) and the value was TRUNCATED to a single bit (e.g. `out:int =
      // int(c)` for a wider `c`, or `out:int = a + 1`). The port width cgen
      // emits lives on the GraphIO (set in setup_io_impl, before the body — and
      // the driver width is only known now), so restamp it there. A declared
      // width (uN/sN, or a bounded `int(max=…)`) keeps e.bits>0 and is left
      // untouched — the constraint is the contract.
      if (e.kind != Io_kind::boolean && e.bits == 0 && out_gio != nullptr) {
        const bool uns   = livehd::graph_util::is_unsign(it->second);
        int32_t    dbits = livehd::graph_util::bits_of(it->second);
        if (dbits <= 0) {
          // A const driver carries no `bits` attr; size from the constant's own
          // width (the width cgen emits for the literal) so `out:int = 300` is
          // not squeezed into a single bit.
          if (it->second.is_const()) {
            dbits = livehd::graph_util::const_of(it->second).get_signed_bits();
          } else {
            dbits = mw_lookup(e.name);
          }
        }
        if (dbits > 0) {
          // The output port's width+sign live on the GraphIO (authoritative;
          // the bits_of(pin, gio, name) overload falls back to it).
          // `bits`/`signed` are DRIVER-pin properties, so do NOT stamp them on
          // this output-port SINK — its width is its driver's, read through the
          // driver (see node_util.hpp set_bits, which now asserts driver-only).
          out_gio->set_bits(ename, static_cast<uint32_t>(dbits));
          out_gio->set_unsign(ename, uns);
        }
      }
    }

    // Every consumer of a memory read is wired now: replay the same-cycle
    // writes its ordering lets it see.
    replay_mem_reads();

    // Clock/Reset are types, not data (docs 07-typesystem): checked once the
    // body is fully wired.
    check_clock_reset_flow();
    check_builtin_memory_clocks();
    check_verilog_clock_idioms();

    // Lower the declared per-output intervals as pendings.
    stamp_output_pendings();

    // Guard — a stage declare whose din store never arrived would
    // silently drop the delay (and the value): hard error, never nil.
    if (!pending_stage_.empty()) {
      error_at(pending_stage_.begin()->second.decl_nid,
               "upass.tolg: stage reg '{}' in '{}' was declared but never "
               "stored — its delay would be silently lost",
               pending_stage_.begin()->first,
               lnast_->get_top_module_name());
    }

    // Persist the block-attribute regions (2opt-freq B): the coloring_info
    // JSON's "region_opts" member is what pass.abc reads for per-region ABC
    // options; the node colors themselves were stamped by make_node.
    write_region_info();
  }

private:
  // ── width / value helpers
  // ───────────────────────────────────────────────────

  [[nodiscard]] static int32_t io_mw(const Lnast_io_entry& e) {
    if (e.kind == Io_kind::boolean) {
      return 1;
    }
    return e.bits > 0 ? static_cast<int32_t>(e.bits) : int32_t{1};
  }

  [[nodiscard]] Pin nil_pin() { return create_const(*g_, *Dlop::from_pyrope("0sb?")); }

  // Backtick is LiveHD's general quoted-string IDENTIFIER syntax: `` `id` `` is
  // a literal id whose content is any character (a literal backtick rides as
  // `\`). It is the LNAST analogue of a Verilog escaped id `\id ` — slang emits
  // it for
  // `\ar.x ` so `.x` is not read as a tuple field access through upass. By tolg
  // names are flat strings (no field-access parsing), so the quoting is no
  // longer needed: recover the bare content as the canonical lg signal/IO name.
  // The yosys-verilog and Pyrope readers already emit the bare `ar.x`, so
  // unquoting makes the name identical across readers — without it a
  // cross-reader LEC sees
  // `` `ar.x` `` vs `ar.x` as two unrelated free inputs and falsely refutes.
  // cgen re-escapes by content (the `.`), so the marker is redundant
  // downstream. EXCEPTION: a content with WHITESPACE cannot be a bare lg name
  // (library.txt is whitespace-delimited), so keep it quoted — those rare ids
  // stay `` `a b` ``.
  [[nodiscard]] static std::string_view canon_io_name(std::string_view name) {
    if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
      auto inner = name.substr(1, name.size() - 2);
      for (const char c : inner) {
        if (std::isspace(static_cast<unsigned char>(c))) {
          return name;  // genuinely needs quoting (whitespace) — leave as-is
        }
      }
      return inner;
    }
    return name;
  }

  // An undriven (all-`?`) UNSIGNED value exactly `mw` bits wide. Unlike
  // nil_pin() (a sign-extending 64-bit `0sb?`), a read of a value built on it
  // keeps the declared width instead of seeing X sign-fill above it.
  [[nodiscard]] Pin undriven_pin(int32_t mw) {
    return create_const(*g_, *Dlop::from_pyrope(absl::StrCat("0ub", std::string(static_cast<size_t>(std::max(mw, 1)), '?'))));
  }

  void record(std::string_view name_in, const Pin& pin, int32_t mw) {
    std::string_view name = canon_io_name(name_in);
    std::string      key{name};
    if (!branch_writes_.empty()) {
      // First write to `key` in this branch: remember how to undo it on branch
      // exit -- restore the pre-branch value, or erase if the name was absent.
      // Capturing lazily here is O(writes); lower_branch used to snapshot the
      // whole pin_map_ per branch, which is O(pin_map_) and turns quadratic on
      // if/elif-heavy designs (e.g. firtool mux chains) -- a deep hang.
      if (auto [it, inserted] = branch_writes_.back().try_emplace(key, pin); inserted) {
        auto pit = pin_map_.find(key);
        auto mit = mw_map_.find(key);
        branch_restore_.back().emplace(key,
                                       Branch_restore{pit != pin_map_.end() ? std::optional<Pin>{pit->second} : std::nullopt,
                                                      mit != mw_map_.end() ? std::optional<int32_t>{mit->second} : std::nullopt});
      } else {
        it->second = pin;  // keep the branch's latest value for the merge
      }
    }
    pin_map_[key] = pin;
    mw_map_[key]  = mw;
    // Track the LOGICAL variable's most-recent driver (SSA versions collapse to
    // the root) for derived reset_pin/clock_pin resolution and the wire buffer.
    // Shadow keys (\x01din:/\x01en:) are not user names, so skip them.
    if (!key.empty() && key.front() != '\x01') {
      const auto pos                                                     = key.find("___ssa_");
      logical_last_[pos == std::string::npos ? key : key.substr(0, pos)] = {pin, mw};
    }
  }

  [[nodiscard]] int32_t mw_lookup(std::string_view name) {
    auto it = mw_map_.find(std::string{canon_io_name(name)});
    return it != mw_map_.end() ? it->second : int32_t{1};
  }

  // The LOGICAL variable behind a (possibly SSA-versioned, possibly
  // backtick-marked) LNAST name -- the key decl_type_ uses, because a type is
  // declared once on the base name while every read is a fresh SSA version.
  // A version SSA'd a second time (a generic specialization re-SSAs its clone
  // of the already-SSA'd template) is spelled `x__w<N>` (upass::
  // demote_stale_ssa), which this tree records; a source-authored `x__w2` is
  // not recorded and stays its own variable.
  [[nodiscard]] std::string logical_key(std::string_view name) const {
    std::string k{canon_io_name(name)};
    for (;;) {
      if (auto p = k.find("___ssa_"); p != std::string::npos) {
        k.resize(p);
      }
      const auto demoted = lnast_->ssa_demoted_base(k);
      if (demoted.empty()) {
        return k;
      }
      k = std::string(demoted);
    }
  }

  // Declared width + signedness of one name (see decl_type_ below). Defined
  // here, not with the member, because a nested type must be declared before
  // the member functions whose SIGNATURE names it.
  struct Decl_type {
    int32_t mw{0};
    bool    is_signed{false};
  };

  void record_decl_type(std::string_view name, int32_t mw, bool is_signed) {
    if (mw <= 0) {
      return;  // untyped / unbounded: nothing declared to remember
    }
    decl_type_[logical_key(name)] = Decl_type{mw, is_signed};
  }

  [[nodiscard]] std::optional<Decl_type> decl_type_lookup(std::string_view name) const {
    auto it = decl_type_.find(logical_key(name));
    if (it == decl_type_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  [[nodiscard]] Pin resolve(std::string_view name_in) {
    std::string_view name = canon_io_name(name_in);
    std::string      key{name};
    auto             it = pin_map_.find(key);
    if (it != pin_map_.end()) {
      return it->second;
    }
    // Whole-array read (`x = mem`): materialize the cell's async read_all
    // output. Publish its packed width under the memory name as well: leaf()
    // resolves the pin first and then asks mw_lookup(name), so omitting this
    // left every whole-array read with the default width 1 even though the pin
    // itself carries size*elem_mw bits. A following partial write then sized
    // its first set_mask only to the mask reach and discarded the untouched
    // high lanes.
    auto mit = mem_map_.find(key);
    if (mit == mem_map_.end() && name != name_in) {
      // Array declarations retain escaped flat names (e.g. `btb_q.valid`).
      // Scalar pin lookup canonicalizes those names; whole-memory reads must
      // still find the same declaration as indexed reads and writes.
      mit = mem_map_.find(std::string(name_in));
    }
    if (mit != mem_map_.end()) {
      auto& mi  = mit->second;
      auto  pin = get_or_make_read_all(mi);
      if (!mi.is_array) {
        pin = make_read_all_site(mi, pin);  // forward_read_all replays this cycle's writes over it
      }
      mw_map_[key] = static_cast<int32_t>(mi.size * mi.elem_mw);
      return pin;
    }
    // A name that resolves to neither a driver nor a memory would be wired to
    // nil (0sb?). For Pyrope that drops whatever the reference carried — a
    // silent miscompile, so it is a hard error. A Verilog-origin module may
    // legally read an undriven wire (it defaults to X), so keep the warn + nil.
    const bool strict = !lnast_->is_verilog_origin();
    if (strict) {
      const auto alias = nil_seed_alias_.find(key);
      error_here(
          "upass.tolg: unresolved reference '{}' — it has no driver "
          "(often an unassigned variable or a value that "
          "resolved to nil)",
          alias != nil_seed_alias_.end() ? std::string_view(alias->second) : name);
    } else {
      warn_at(Lnast_nid{}, {"unresolved-ref", "name"}, "unresolved ref '{}' — wiring nil (0sb?)", name);
    }
    auto p        = nil_pin();
    pin_map_[key] = p;
    mw_map_[key]  = 1;
    return p;
  }

  [[nodiscard]] Val leaf(const Lnast_nid& nid) {
    if (Lnast_ntype::is_const(lnast_->get_type(nid))) {
      auto c = Dlop::from_pyrope(lnast_->get_name(nid));
      if (c->is_invalid()) {
        error_at(nid, "upass.tolg: malformed constant literal '{}'", lnast_->get_name(nid));
      }
      if (c->is_nil()) {
        // A bare `nil` literal that reaches a graph leaf has always lowered to
        // the integer 0 (the structural nil paths are handled by their own
        // rules); say so here -- the constant pool refuses Nil as a value.
        c = Dlop::create_integer(0);
      }
      int32_t mw
          = c->is_just_i64() ? mw_of_val(c->to_just_i64()) : std::max<int32_t>(1, static_cast<int32_t>(c->get_signed_bits()));
      return {create_const(*g_, *c), mw};
    }
    auto name = lnast_->get_name(nid);
    return {resolve(name), mw_lookup(name)};
  }

  // Bind a computed result using the literal unsigned-width contract.
  void bind_result(std::string_view name, const Pin& drv, int32_t mw) {
    int32_t m = mw > 0 ? mw : int32_t{1};
    set_ubits(drv, m);
    record(name, drv, m);
  }

  // ── statement / node dispatch
  // ───────────────────────────────────────────────

  void lower_stmts(const Lnast_nid& stmts) {
    // A `__region` marker inside this stmts sets cur_color_ for the REST of
    // the block; restoring here bounds the region to its block (nested blocks
    // override and restore, if/match arms inherit the enclosing color).
    const auto saved_color = cur_color_;
    for (auto c = lnast_->get_first_child(stmts); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (lnast_->is_dce_dead(c)) {
        continue;  // dce:mark (lg-only flows): a dead statement is skipped here
                   // instead of the runner rebuilding the whole staging tree
      }
      if (!default_only_stmts_.empty() && default_only_stmts_.contains(c.get_class_index().value)) {
        continue;  // a defaulted input's default expression (mark_default_only_prologue)
      }
      lower_node(c);
    }
    cur_color_ = saved_color;
  }

  // The current statement's SourceId, re-minted into the graph's
  // locator. Every cell make_node creates while lowering this statement is
  // stamped with it, so LGraph nodes resolve back to Pyrope source.
  hhds::SourceId cur_srcid_{0};

  // Block-scoped synthesis region (2opt-freq B): the active `{ ::[abc=…,
  // color=…] }` region id, set by the block's `__region` marker and restored
  // at the enclosing stmts' exit; every node make_node mints while it is
  // non-zero gets livehd::attrs::color, which pass.partition/pass.abc turn
  // into a per-region mapping unit. region_abc_ collects the per-color ABC
  // flow payloads for the coloring_info "region_opts" member.
  int32_t                                               cur_color_ = 0;
  std::map<int32_t, std::string>                        region_abc_;
  std::map<int32_t, std::map<std::string, std::string>> region_options_;
  absl::flat_hash_set<int32_t>                          region_colors_marked_;
  absl::flat_hash_set<int32_t>                          region_colors_stamped_;

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
    throw Eprp::parser_error(Pass::eprp, msg);
  }

  template <typename... Args>
  [[noreturn]] void error_at(const Lnast_nid& nid, std::format_string<Args...> fmt, Args&&... args) {
    error_at(nid, livehd::diag::Id{"tolg-error", "type"}, "{}", std::format(fmt, std::forward<Args>(args)...));
  }

  // error_at with a `help:` line (and optional notes at other sites).
  [[noreturn]] void error_hint_at(const Lnast_nid& nid, livehd::diag::Id id, std::string msg, std::string hint,
                                  std::vector<livehd::diag::Note> notes = {}) {
    auto d = locate_record(nid, livehd::diag::Severity::error, id.code, id.category, msg);
    d.hint = std::move(hint);
    std::ranges::move(notes, std::back_inserter(d.notes));
    livehd::diag::sink().stage(std::move(d));
    throw Eprp::parser_error(Pass::eprp, msg);
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
    if (cur_color_ != 0) {
      livehd::graph_util::set_color(n, cur_color_);
      region_colors_stamped_.insert(cur_color_);
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
      // Range values are consumed by upass; selection nodes carry their endpoints directly.
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
      lower_op(nid, Ntype_op::And, true, OpW::andw);
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
    } else if (N::is_concat(t)) {
      lower_concat(nid);
    } else if (N::is_sext(t)) {
      lower_sext(nid);
    } else if (N::is_red_or(t)) {
      lower_red_or(nid);
    } else if (N::is_red_and(t)) {
      lower_red_and(nid);
    } else if (N::is_red_xor(t)) {
      lower_red_xor(nid);
    } else if (N::is_popcount(t)) {
      lower_popcount(nid);
    } else if (N::is_div(t)) {
      lower_op(nid, Ntype_op::Div, false, OpW::firstw);
    } else if (N::is_mod(t)) {
      // `a % b` has no general hardware lowering; lower_mod handles only the
      // easy/unambiguous cases (power-of-two, range-fit, `% 3`) to shift/mask
      // and HARD-errors the rest.
      lower_mod(nid);
    } else if (N::is_bit_not(t)) {
      lower_unary(nid, Ntype_op::Not);
    } else if (N::is_log_not(t)) {
      lower_log_not(nid);
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
    } else if (N::is_rolled_for(t)) {
      lower_rolled_for(nid);
    } else if (N::is_attr_get(t)) {
      // Every attribute read folds in upass.attributes before tolg; one that
      // survives has no hardware lowering — a hard error inside.
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
          "upass.tolg: non-comptime `for` loop in '{}' — the iterable "
          "did not resolve to a comptime range "
          "or tuple, so the loop could not unroll (Pyrope for-loops are "
          "comptime-only and must fully unroll)",
          lnast_->get_top_module_name());
    } else if (N::is_type_spec(t)) {
      // Pure annotation, no datapath (the comb inliner's emit_inline_typespec,
      // or a folded type check). No hardware, but a typed name remembers its
      // declared width (see note_type_spec).
      note_type_spec(nid);
    } else if (N::is_cassert(t)) {
      // A cassert upass.verifier could NOT discharge at comptime (unknown cond)
      // survives here. Materialize it as an `fproperty` Sub so pass.formal can
      // try to prove it and cgen can emit a runtime check for whatever is left.
      lower_cassert(nid);
    } else {
      // Any other node type reaching tolg has no LGraph lowering and would be
      // silently dropped (→ undriven wires / nil). That is a miscompile, so it
      // is a hard error, never a warning on an otherwise-"passing" run.
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: node type '{}' has no hardware lowering — it "
               "survived elaboration but cannot be turned "
               "into a netlist (this is usually an unresolved value or an "
               "unsupported runtime construct)",
               Lnast_ntype::to_sv(t));
    }
  }

  // Every attribute read folds during elaboration (upass.attributes); one that
  // survives to tolg has no hardware lowering, so it is a hard error. A range
  // attribute (`.[bits]`, `.[max]`, `.[min]`, `.[sign]`) of a value with no
  // declared width folds to nil (user ruling 2026-09-27 (6): an untyped
  // runtime value has no width, nor does an unbounded `x:signed`), and nil
  // drives no hardware: that is the user's error, not an internal one.
  void lower_attr_get(const Lnast_nid& nid) {
    auto              dst       = lnast_->get_first_child(nid);
    auto              base      = dst.is_invalid() ? dst : lnast_->get_sibling_next(dst);
    auto              attr      = base.is_invalid() ? base : lnast_->get_sibling_next(base);
    const std::string attr_name = attr.is_invalid() ? std::string{} : std::string(lnast_->get_name(attr));
    if (attr_name == "bits" || attr_name == "max" || attr_name == "min" || attr_name == "sign") {
      std::string_view base_name = base.is_invalid() ? std::string_view{} : lnast_->get_name(base);
      base_name                  = base_name.substr(0, base_name.find("___ssa_"));
      const std::string what
          = base_name.empty() || Lnast::is_tmp(base_name) ? std::string{"the value"} : std::format("`{}`", base_name);
      error_hint_at(nid,
                    {"attr-read-nil", "type"},
                    std::format("`.[{}]` of {} is nil: it has no declared width, so the read has no value to drive hardware",
                                attr_name,
                                what),
                    "declare the value with a sized type (`mut x:U8 = …`, or read it from a typed port or tuple field), or only "
                    "compare the attribute read with `nil`");
    }
    error_at(nid,
             {"unhandled-node", "unsupported"},
             "upass.tolg: attribute read '.[{}]' has no hardware lowering — it "
             "should have folded during elaboration",
             attr_name);
  }

  // Re-resolve each deferred field read once the whole body (incl. later
  // calls) has lowered: by now the source name is a known Sub result / memory,
  // so lower_tuple_get binds the port driver. tget_final_ makes a genuinely
  // unresolvable one warn rather than defer again.
  void resolve_pending_tgets() {
    tget_final_ = true;
    for (const auto& nid : pending_tgets_) {
      lower_tuple_get(nid);
    }
    pending_tgets_.clear();
  }

  // ── 2c-wire — single-driver combinational nets
  // ────────────────────────────── A `wire` declares a passthrough buffer (Or)
  // whose OUTPUT every read binds to (record() at declare →
  // position-independent: a read before the driver appears textually still sees
  // the buffer), and whose INPUT is connected as soon as the single accumulated
  // driver is complete (the din shadow the branch-mux machinery merges, exactly
  // like a reg's din — but with no flop). The buffer is a transparent net, so
  // the time-checker's SCC sees through it: a real comb loop is an error; a
  // ring is legal only when a register breaks it.

  // The single-driver rule is enforced in the FRONTEND (prp2lnast
  // check_wire_drivers) on the pre-elaborate tree, before lnastfmt drops a dead
  // first write (which would hide a double-drive). COVERAGE is NOT a rule: one
  // driver may be conditional, and the merges below fill the unwritten paths
  // with the written value (see is_wire_din). tolg only wires the net and lets
  // the time-checker flag a real comb loop.

  // Wire every declared wire's buffer input to its single accumulated driver
  // (the din shadow the branch-mux machinery merged). The single-driver rule is
  // enforced in the frontend; here a missing driver only survives for a Verilog
  // net (legal X) or a loop-built Pyrope wire the frontend skipped — wire it to
  // nil rather than miscompile.
  void finalize_wires() {
    // An undriven wire reads X under `::[timecheck=false]` (and in a Verilog
    // net) instead of being a compile error -- a TIMING relaxation (docs
    // 04b-attributes, timecheck).
    const bool undriven_is_x = lnast_->is_timecheck_off();
    for (const auto& name : wire_order_) {
      auto& info = wire_info_.at(name);

      // Anchor this wire's diagnostics at its declaration.
      cur_srcid_ = hhds::SourceId_invalid;
      if (const auto id = lnast_->get_srcid(info.decl_nid); id != hhds::SourceId_invalid) {
        cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
      }
      cur_color_ = info.decl_color;  // finalize glue lands in the wire's region

      // Wire the buffer input to the accumulated single driver, restamping the
      // buffer output width from the driver when the wire was untyped.
      Pin     din;
      int32_t mw     = info.decl_mw;
      bool    driven = false;
      if (auto dit = pin_map_.find(din_key(name)); dit != pin_map_.end()) {
        din    = dit->second;
        driven = true;
        if (mw <= 0) {
          mw = mw_lookup(din_key(name));
        }
      } else {
        if (!undriven_is_x) {
          error_here(
              "upass.tolg: wire '{}' is never driven in '{}' — a `wire` "
              "must have exactly one driver",
              name,
              lnast_->get_top_module_name());
        }
        din = nil_pin();  // Verilog net defaults to X; a loop-built undriven
                          // wire falls here too
        if (mw <= 0) {
          mw = 1;
        }
      }
      // An UNDRIVEN net still has to be wired and sized: leaving the passthrough
      // Or with no `as` input ships a dangling cell (a Verilog net legally
      // defaults to X, so `undriven_is_x` only suppresses the diagnostic, not
      // the lowering). There is no driver to truncate in that case, so skip the
      // typed Get_mask -- `nil` is already the declared width's don't-care.
      if (!info.bound) {
        bind_wire_driver(name, din, mw, /*narrow_typed=*/driven);
      }
      resolve_wire_selfref(name);
    }
  }

  // Resolve a packed self-reference once the wire's driver is COMPLETE. Every
  // write has landed by now (finalize_wires runs after finalize_regs/_mems and
  // resolve_pending_tgets), so the splitter sees the whole accumulator instead
  // of a partial one, and a residual dependency is a genuine loop.
  void resolve_wire_selfref(std::string_view name) {
    auto it = wire_info_.find(std::string{name});
    if (it == wire_info_.end()) {
      return;
    }
    auto& info = it->second;
    if (info.bound_din.is_invalid() || info.early_readers.empty()) {
      return;
    }
    livehd::graph_util::split_packed_selfref_wire(g_, info.buf, info.bound_din, info.early_readers, &comb_dependencies_);
    if (!lnast_->is_timecheck_off() && livehd::graph_util::comb_pin_depends_on(info.bound_din, info.buf, &comb_dependencies_)) {
      // Lead with the established `combinational loop` vocabulary -- the same
      // words the time-checker's SCC uses at the end of this file. This IS
      // one; it is simply caught earlier and with a better source anchor.
      // //inou/prp:prp-wire_comb_loop matches the diagnostic on that phrase,
      // and the wire-only wording silently stopped matching it.
      error_here(
          "upass.tolg: combinational loop through wire '{}' in '{}' — its driver depends on itself and no disjoint "
          "packed-slice split can break it",
          name,
          lnast_->get_top_module_name());
    }
  }

  // Attach one wire's effective driver as soon as it is known. This must not
  // wait for end-of-module finalization: a later wire write may read this net,
  // and that later edge is the one that closes a multi-wire dependency.
  //
  // REBINDS. A wire assembled from several partial writes (`w#[3:0] = a;
  // w#[7:4] = b`) reaches here once per write, and lower_set_mask chains each
  // write onto the din accumulator -- so the LATEST driver is the complete one
  // and every earlier bind must be undone. Keeping the first bind (an early
  // `bound` return) silently dropped every write after the first.
  void bind_wire_driver(std::string_view name, Pin din, int32_t mw, bool narrow_typed = true) {
    auto it = wire_info_.find(std::string{name});
    if (it == wire_info_.end()) {
      return;
    }
    auto& info = it->second;

    if (info.bound) {
      // Drop the previous buffer input first: a second connect_driver on the
      // same `as` sink would make the passthrough Or a two-input OR of the
      // partial and the complete value.
      auto as_sink = livehd::graph_util::find_sink_pin(info.buf, "as");
      if (!as_sink.is_invalid()) {
        as_sink.del_sink();
      }
      if (!info.narrow.is_invalid() && !info.narrow.has_out_edges()) {
        wire_cells_.erase(info.narrow.get_debug_nid());
        info.narrow.del_node();  // the superseded typed-wire truncation cell
      }
      info.narrow = hhds::Node_class{};
    }

    // The consumers that exist NOW are the ones that can participate in this
    // net's definition: a genuine position-independent read, or a read taken
    // inside the branch bodies whose merge produced `din`. Captured here rather
    // than at each store so EVERY bind path (plain store, set_mask chain,
    // if/match merge, finalize) is covered by the self-reference resolution
    // below -- a merge-bound wire used to skip it entirely.
    capture_wire_readers(name);

    // A TYPED wire narrows its driver with a real precision-changing cell.
    if (narrow_typed && info.decl_mw > 0) {
      // Get_mask always returns an unsigned pattern. A signed wire instead
      // uses Sext, whose bit-count operand explicitly selects the sign bit.
      auto narrow = make_node(info.is_signed ? Ntype_op::Sext : Ntype_op::Get_mask);
      auto out    = narrow.create_driver_pin(0);
      if (info.is_signed) {
        setup_sink_by_name(narrow, "a").connect_driver(din);
        setup_sink_by_name(narrow, "b").connect_driver(create_const(*g_, *Dlop::create_integer(info.decl_mw)));
        set_sbits(out, info.decl_mw);
      } else {
        livehd::graph_util::connect_mask_operands(narrow, din, 0, info.decl_mw);
        set_ubits(out, info.decl_mw);
      }
      din                                 = out;
      info.narrow                         = narrow;
      wire_cells_[narrow.get_debug_nid()] = info.data_typed;
    }

    setup_sink_by_name(info.buf, "as").connect_driver(din);
    // Record the driver; do NOT split here. A wire assembled from several
    // partial writes rebinds once per write, and splitting against a PARTIAL
    // accumulator resolves an early slice read onto the `0sb?` seed instead of
    // the lane's real driver -- permanently, because the split rewires the
    // reader off the buffer and the next bind then sees no early reader at all
    // (`w#[0..=3] = a ^ hi` before `w#[4..=7] = b` froze `hi` at don't-care,
    // while the same two writes in the opposite order were correct -- a `wire`
    // is a net, so write order must not change its value). resolve_wire_selfref
    // runs the split once, from finalize_wires, against the COMPLETE driver.
    info.bound_din = din;

    if (info.decl_mw <= 0) {
      const int32_t dbits = livehd::graph_util::bits_of(din);
      if (dbits > 0) {
        set_bits(info.out, dbits);
        if (livehd::graph_util::is_unsign(din)) {
          set_unsign(info.out);
        } else {
          set_sign(info.out);
        }
        mw = dbits;
      } else {
        set_ubits(info.out, mw);
      }
      mw_map_[std::string{name}] = mw;
    }
    info.bound = true;
  }

  void maybe_bind_wire_shadow(std::string_view shadow, const Pin& driver, int32_t mw) {
    if (!branch_writes_.empty() || !is_wire_din(shadow)) {
      return;  // inside a branch the merge is not complete yet
    }
    bind_wire_driver(shadow.substr(kDinPrefix.size()), driver, mw);
  }

  // Accumulate the consumers that exist at this wire's binds. There is no
  // reason to track reads taken after the LAST write: they are downstream of
  // the completed net and cannot participate in its definition. Called from
  // bind_wire_driver ONLY, right before the buffer input is attached, and it
  // UNIONS rather than rebuilds -- a partial-write rebind must not forget a
  // reader that the earlier write already saw.
  void capture_wire_readers(std::string_view name) {
    auto it = wire_info_.find(std::string{name});
    if (it == wire_info_.end()) {
      return;
    }
    auto& readers = it->second.early_readers;
    for (const auto& e : it->second.out.out_edges()) {
      auto reader = e.sink.get_master_node();
      if (reader.is_invalid() || reader == it->second.buf) {
        continue;
      }
      if (std::ranges::find(readers, reader) == readers.end()) {
        readers.push_back(reader);
      }
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
    if (lnast_->get_name(key_n) == "__region" || lnast_->get_name(key_n) == "__region_ware"
        || lnast_->get_name(key_n) == "__region_delay") {
      // Synthesis-region marker (2opt-freq B): attr_set(%__region_<id>,
      // "__region", <abc-string | true>) — first statement of an annotated
      // `{ ::[…] … }` block. The region id rides the target name; a quoted
      // value is the per-region ABC flow payload. lower_stmts restores
      // cur_color_ at the block's exit.
      constexpr std::string_view kPrefix = "%__region_";
      auto                       tname   = lnast_->get_name(tgt);
      int32_t                    id      = 0;
      if (tname.size() > kPrefix.size() && tname.substr(0, kPrefix.size()) == kPrefix) {
        auto ds = tname.substr(kPrefix.size());
        std::from_chars(ds.data(), ds.data() + ds.size(), id);
      }
      if (id <= 0) {
        warn_at(tgt, {"region-marker-malformed", "internal"}, "malformed __region marker target '{}' (compiler bug?)", tname);
        return;
      }
      cur_color_ = id;
      region_colors_marked_.insert(id);
      if (auto val_n = lnast_->get_sibling_next(key_n); !val_n.is_invalid()) {
        std::string_view val = lnast_->get_name(val_n);
        const auto       key = lnast_->get_name(key_n);
        if (key != "__region") {
          const std::string opt{key.substr(std::string_view{"__region_"}.size())};
          const auto [it, inserted] = region_options_[id].try_emplace(opt, val);
          if (!inserted && it->second != val) {
            error_at(tgt, {"region-option-conflict", "syntax"}, "region color {} carries conflicting {}= options", id, opt);
          }
          return;
        }
        if (val.size() >= 2 && ((val.front() == '\'' && val.back() == '\'') || (val.front() == '"' && val.back() == '"'))) {
          val                  = val.substr(1, val.size() - 2);
          auto [ait, inserted] = region_abc_.try_emplace(id, std::string(val));
          if (!inserted && ait->second != val) {
            warn_at(tgt,
                    {"region-abc-conflict", "unsupported"},
                    "region color {} carries conflicting abc= options; keeping "
                    "the first",
                    id);
          }
        }
      }
      return;
    }
    auto it = reg_info_.find(std::string(lnast_->get_name(tgt)));
    if (it == reg_info_.end()) {
      // Not a flop reg (yet): stash for a later array/memory declare (the
      // importer emits the attr_set before the declare it qualifies).
      auto key_sv = lnast_->get_name(key_n);
      auto val_n0 = lnast_->get_sibling_next(key_n);
      auto val_sv = val_n0.is_invalid() ? std::string_view{"true"} : std::string_view(lnast_->get_name(val_n0));
      pending_attrs_[std::string(lnast_->get_name(tgt))][std::string(key_sv)] = std::string(val_sv);
      return;
    }
    auto& info  = it->second;
    auto  key   = lnast_->get_name(key_n);
    auto  val_n = lnast_->get_sibling_next(key_n);
    auto  val   = val_n.is_invalid() ? std::string_view{"true"} : std::string_view(lnast_->get_name(val_n));
    if ((key == "reset_pin")) {
      info.reset_pin_name = std::string(val);
    } else if ((key == "clock_pin")) {
      info.clock_pin_name     = std::string(val);
      info.clock_pin_is_const = val_n.is_invalid() || Lnast_ntype::is_const(lnast_->get_type(val_n));
    } else if ((key == "enable")) {
      // Explicit write-enable (`reg q:[enable=(wen!=0)]`): the state element
      // updates (a latch: is transparent) only while it holds. It is ANDed onto
      // the OR-of-write-conditions the branch machinery already derives, so an
      // `if`-guarded write inside an `enable=`-qualified reg keeps BOTH guards.
      // The value is a plain ref (prp2lnast hoists the expression into a temp
      // ahead of the declare), so it is resolved in finalize_regs, once every
      // producer has been walked.
      info.enable_name     = std::string(val);
      // …UNLESS the value is a CONSTANT: the flag-only spelling `:[enable]`
      // (val defaults to "true" above), `enable=true/false`, `enable=1/0`. A
      // const names no signal, so resolve_attr_signal would fail it with a
      // nonsense "has no such input/wire". Remember which it is here — the
      // node type is only visible at the attr_set.
      info.enable_is_const = val_n.is_invalid() || Lnast_ntype::is_const(lnast_->get_type(val_n));
    } else if ((key == "posclk") || (key == "enable_high")) {
      // `enable_high` is the LATCH-facing spelling of the same pin (2f-latch
      // M2): on a latch, pid 6 is the ENABLE POLARITY, not a clock edge, so
      // `posclk` reads as a lie there. Both map to the same slot — the IR pin
      // keeps the Flop-shared name because find_sink_pin() resolves an unknown
      // name to invalid SILENTLY, and a rename that missed a consumer would
      // drop the polarity without a word.
      info.has_posclk = true;
      info.posclk_val = val != "false" && val != "0";
    } else if ((key == "sync")) {
      info.has_sync = true;
      info.sync_val = val != "false" && val != "0";
    } else if ((key == "async")) {
      // Canonical Pyrope-source spelling (04b-attributes.md known_attrs): the
      // inverse of the importer's `sync`.  `async=true` => async reset.
      info.has_sync = true;
      info.sync_val = val == "false" || val == "0";
    } else if ((key == "negreset")) {
      info.negreset = val != "false" && val != "0";  // explicit: beats the `_n` name default
    } else if ((key == "initial")) {
      // `initial` is the ONE spelling: the LGraph Flop/Latch/Memory reset-value
      // pin (graph/cell.cpp) and the Pyrope declaration attribute are the same
      // name.  (`init` was the old Pyrope-only spelling; prp2lnast now reports
      // it with a "use `initial`" hint.)  Overrides the declare's reset value.
      info.initial_txt    = std::string(val);
      info.initial_is_ref = !val_n.is_invalid() && Lnast_ntype::is_ref(lnast_->get_type(val_n));
    } else if ((key == "name")) {
      // Explicit local flop name (`reg x:[name="reg_x"]`) — overrides the
      // declared variable name; finalize_regs combines it with any hier prefix.
      // The value is a Pyrope string literal, so strip the surrounding quotes.
      std::string_view nm = val;
      if (nm.size() >= 2 && ((nm.front() == '\'' && nm.back() == '\'') || (nm.front() == '"' && nm.back() == '"'))) {
        nm = nm.substr(1, nm.size() - 2);
      }
      info.name_override = std::string(nm);
    } else if (key == "__hier") {
      // Runner-stamped instance-path prefix for a reg inside an inlined comb
      // (`pipeB_ex_mem`); finalize_regs prepends it (dotted) to the local name.
      info.hier_prefix = std::string(val);
    } else if ((key == "type") || (key == "comptime") || (key == "typename")) {
      // storage-class markers and the declared named type (`reg st:Color`) —
      // already consumed by the declare
    } else if ((key == "__store_clock_pin") || (key == "__store_posclk")) {
      // Memory-only markers: inou.slang emits them for the STORES of an
      // unpacked array, and only lower_mem_update_store / lower_mem_store read
      // them. Landing on a Flop means the array was lowered as a scalar
      // register instead, and the generic warning below would let the clock
      // evaporate -- finalize_regs then binds the flop to the lazily-minted
      // implicit `clock` input, on the WRONG edge. That is the vpu_trans
      // `id_trans_scoreboard_o` / intpipe_csr_msgs failure; a dropped clock is
      // not a warning.
      error_at(tgt,
               {"store-attr-on-flop", "unsupported"},
               "reg '{}' carries the memory-only marker '{}': its clock would "
               "be silently dropped",
               lnast_->get_name(tgt),
               key);
    } else {
      warn_at(tgt,
              {"reg-attr-not-lowered", "unsupported"},
              "reg '{}' attribute '{}' not lowered (attribute not in the "
              "lowered set)",
              lnast_->get_name(tgt),
              key);
    }
  }

  // Persist the block-attribute regions (2opt-freq B) as the graph-level
  // coloring_info JSON: algorithm "block-attr" marks the colors as
  // SOURCE-SEEDED (pass.color preserves seeded colors and allocates its own
  // ids above them), and "region_opts" carries each region's abc= flow string
  // for pass.abc. Emitted only when at least one annotated block exists, so
  // ordinary compiles keep no coloring_info.
  void write_region_info() {
    if (region_colors_marked_.empty()) {
      return;
    }
    // Absorb uncolored fan-in glue into its consumers' region. Helper-minted
    // conditioning nodes (e.g. node_util's to-positive Get_mask wrappers)
    // bypass the make_node funnel and would otherwise shatter into tiny
    // color-0 boundary regions between the block and its inputs — each a
    // separate mapping unit whose region cut costs delay. A color-0 node
    // whose every sink lives in ONE region belongs to that region; iterate to
    // a fixpoint so glue chains absorb too.
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto n : g_->body().nodes(hhds::Node_order::forward)) {
        if (n.is_invalid() || livehd::graph_util::is_builtin_node(n)) {
          continue;
        }
        auto op = livehd::graph_util::type_op_of(n);
        if (op == Ntype_op::IO) {
          continue;
        }
        if (livehd::graph_util::node_color_of(n) != 0) {
          continue;
        }
        int32_t c    = 0;
        bool    ok   = false;
        bool    stop = false;
        // Pin-centric walk: out_sorted_pins() yields this node's CONNECTED
        // driver pins; a driver's fanout is a SET, so the sinks still come from
        // the pin's own out_edges(). Read-only, so the lazy views are safe.
        for (auto dpin : n.out_sorted_pins()) {
          for (const auto& e : dpin.out_edges()) {
            auto sn = e.sink.get_master_node();
            if (sn.is_invalid() || livehd::graph_util::is_builtin_node(sn)) {
              ok   = false;  // drives an output/builtin: boundary glue, keep it out
              stop = true;
              break;
            }
            auto sc = livehd::graph_util::node_color_of(sn);
            if (sc == 0 || (c != 0 && sc != c)) {
              ok   = false;  // uncolored or multi-region fanout: stays background
              stop = true;
              break;
            }
            c  = sc;
            ok = true;
          }
          if (stop) {
            break;
          }
        }
        if (ok && c != 0) {
          livehd::graph_util::set_color(n, c);
          changed = true;
        }
      }
    }
    for (auto c : region_colors_marked_) {
      if (!region_colors_stamped_.contains(c)) {
        warn_at(Lnast_nid{},
                {"block-attr-region-empty", "unsupported"},
                "block region color {} in '{}' produced no hardware (its "
                "statements folded away?) — the annotation has no effect",
                c,
                lnast_->get_top_module_name());
      }
    }
    std::string j  = "{\"schema_version\":1,";
    j             += std::format("\"top\":\"{}\",", livehd::json_util::escape(lnast_->get_graph_name()));
    j             += "\"algorithm\":\"block-attr\",\"params\":{},\"colors\":{},"
                     "\"region_opts\":{";
    // Use ordered maps for stable metadata and cache recipes.
    auto options   = region_options_;
    for (const auto& [color, flow] : region_abc_) {
      options[color]["flow"] = flow;
    }
    bool first = true;
    for (const auto& [color, values] : options) {
      if (!first) {
        j += ",";
      }
      first              = false;
      j                 += std::format("\"{}\":{{", color);
      bool first_option  = true;
      for (const auto& [key, value] : values) {
        if (!first_option) {
          j += ",";
        }
        first_option  = false;
        j            += std::format("\"{}\":", key);
        if (key == "ware") {
          j += value == "true" ? "true" : "false";
        } else {
          // Region_opts uses strings for delay; zero clears inherited timing.
          j += std::format("\"{}\"", livehd::json_util::escape(key == "delay" && value == "0" ? "" : value));
        }
      }
      j += "}";
    }
    j += "}}";
    g_->get_input_node().attr(livehd::attrs::coloring_info).set(j);
  }

  // Remove hold arms that branch lowering bakes into a latch's D before
  // cprop. This narrow structural proof matters for hierarchical LEC, where a
  // child definition can be inspected before a graph-pass sweep reaches it.
  // At every peeled layer, D's Q arm must correspond to a known-false arm of
  // the enable mux under the exact same selector.
  [[nodiscard]] Pin canonical_latch_din(Pin din, const Pin& q, Pin en) {
    const auto driver_at = [](const hhds::Node_class& n, hhds::Port_id pid) -> Pin {
      // Read-only pin walk: inp_sorted_pins() yields CONNECTED sink pins in
      // ascending port order, and each carries exactly one driver.
      for (auto sink : n.inp_sorted_pins()) {
        if (sink.get_port_id() == pid) {
          return sink.get_driver_pin();
        }
      }
      return Pin{};
    };
    const auto same = [](const Pin& a, const Pin& b) {
      return !a.is_invalid() && !b.is_invalid() && a.get_class_index() == b.get_class_index();
    };
    for (int depth = 0; depth < 64 && !din.is_invalid() && !en.is_invalid(); ++depth) {
      auto dm = din.get_master_node();
      auto em = en.get_master_node();
      if (livehd::graph_util::type_op_of(dm) != Ntype_op::Mux || livehd::graph_util::type_op_of(em) != Ntype_op::Mux) {
        break;
      }
      auto ds = driver_at(dm, 0);
      auto es = driver_at(em, 0);
      if (!same(ds, es)) {
        break;
      }
      auto d0    = driver_at(dm, 1);
      auto d1    = driver_at(dm, 2);
      int  q_arm = same(d0, q) ? 0 : (same(d1, q) ? 1 : -1);
      if (q_arm < 0) {
        break;
      }
      auto e_hold = driver_at(em, static_cast<hhds::Port_id>(q_arm + 1));
      if (e_hold.is_invalid() || !e_hold.is_const() || !livehd::graph_util::const_of(e_hold).is_known_false()) {
        break;
      }
      din = q_arm == 0 ? d1 : d0;
      en  = driver_at(em, static_cast<hhds::Port_id>((1 - q_arm) + 1));
    }
    return din;
  }

  // Resolve a reg-attribute signal NAME (`clock_pin=`, `enable=`) to its
  // driver pin. IO inputs FIRST (a port is not in pin_map_, while an unrelated
  // same-named value might be, which is why a pin_map_-first lookup is wrong);
  // then a 2c-wire's DRIVER rather than its passthrough buffer (cgen drops a
  // buffer whose only consumer is a flop control pin); then any ordinary
  // value/temp. Invalid = the module has no such input/wire/value.
  [[nodiscard]] Pin resolve_attr_signal(const std::string& nm) {
    if (auto p = resolve_attr_signal_as(nm); !p.is_invalid()) {
      return p;
    }
    // A port named by a backticked type word (`clock_pin=`S1``): the GraphIO
    // spells it bare. Tried second, so an escaped net (`` `foo.bar` ``, a wire
    // keyed by its quoted name) keeps resolving as before.
    if (const auto canon = canon_io_name(nm); canon != nm) {
      return resolve_attr_signal_as(std::string(canon));
    }
    return Pin{};
  }
  [[nodiscard]] Pin resolve_attr_signal_as(const std::string& nm) {
    if (g_->get_io()->has_input(nm)) {
      return g_->get_input_pin(nm);
    }
    if (wire_names_.contains(nm)) {
      if (auto dit = pin_map_.find(din_key(nm)); dit != pin_map_.end()) {
        return dit->second;
      }
    }
    if (auto it = pin_map_.find(nm); it != pin_map_.end()) {
      return it->second;
    }
    return Pin{};
  }

  // Wire each declared reg's din / enable / reset_pin / initial /
  // async / negreset after the whole body has been lowered (stores and attr
  // overrides arrive in any order relative to the declare).
  void finalize_regs() {
    for (const auto& name : reg_order_) {
      auto& info = reg_info_.at(name);
      auto& flop = info.flop;

      // Hierarchical / overridden flop name. `name_override` (`reg
      // x::[name=…]`) replaces the local name; `hier_prefix` (runner `__hier`,
      // the instance path of the inlined comb) is prepended dotted — so an
      // inlined reg reads `pipeB_ex_mem.reg_x`, the same get_hier_name() a
      // non-inlined Sub gives.
      if (!info.hier_prefix.empty() || !info.name_override.empty()) {
        // Recover the clean source-local name from the connectivity name by
        // stripping the inliner's frame tag (`inl<salt>_`) and any SSA suffix.
        auto strip_inl_tag = [](std::string_view s) -> std::string_view {
          if (s.size() > 4 && s.compare(0, 3, "inl") == 0) {
            std::size_t i = 3;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
              ++i;
            }
            if (i > 3 && i < s.size() && s[i] == '_') {
              return s.substr(i + 1);
            }
          }
          return s;
        };
        std::string local;
        if (!info.name_override.empty()) {
          local = info.name_override;
        } else {
          local = std::string(strip_inl_tag(name));
          if (auto p = local.find("___ssa_"); p != std::string::npos) {
            local.resize(p);
          }
        }
        const std::string final_name = info.hier_prefix.empty() ? local : (info.hier_prefix + "." + local);
        if (!final_name.empty()) {
          auto qn = flop.create_driver_pin(0);
          livehd::graph_util::set_pin_name(qn, final_name);
          flop.set_name(final_name);
        }
      }

      // Runs after the walk: anchor this reg's diagnostics at its declaration
      // instead of whatever statement the walk ended on.
      cur_srcid_ = hhds::SourceId_invalid;
      if (const auto id = lnast_->get_srcid(info.decl_nid); id != hhds::SourceId_invalid) {
        cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
      }
      cur_color_ = info.decl_color;  // finalize glue lands in the reg's region

      // din: the final shadow value (last-write-wins; branch writes arrive
      // pre-muxed). A never-written reg holds its value forever: din <- q.
      auto q = flop.create_driver_pin(0);
      Pin  din;
      if (auto dit = pin_map_.find(din_key(name)); dit != pin_map_.end()) {
        din = dit->second;
      } else {
        din = q;
      }
      if (info.is_latch) {
        if (auto eit = pin_map_.find(en_key(name)); eit != pin_map_.end()) {
          din = canonical_latch_din(din, q, eit->second);
        }
      }
      setup_sink_by_name(flop, "din").connect_driver(din);

      if (info.is_latch) {
        // FAIL CLOSED on an attr this branch cannot honor (2f-latch M0), now
        // narrowed to the ones M2 did NOT wire. Before M0 every one of these
        // was silently DISCARDED: a
        // `reg l:u8:[latch=true, clock_pin=ck2, posclk=false, initial=3]`
        // compiled exit 0, zero warnings, and emitted Verilog byte-identical to
        // a plain transparent-high latch. Authoring an attr that vanishes is
        // worse than not having it.
        //
        // Still refused: the reset family (M7 wires it; the pins are reserved
        // on the cell already) and `clock_pin`. clock_pin stays refused BY
        // DESIGN, not as a stub: a latch's gate IS its enable, so a second
        // clock/gate identity would recreate exactly the two-sources-of-truth
        // disagreement the enable-polarity ruling collapses. It is only
        // reserved on the cell so a future ICG model has a slot if one is ever
        // ruled in.
        {
          std::string_view dropped;
          std::string_view why = "todo/livehd/2f-latch M7 wires the reset family";
          if (!info.clock_pin_name.empty()) {
            dropped = "clock_pin";
            why
                = "a latch's gate IS its `enable` signal — write the "
                  "transparency condition in the `if`, not as a clock";
          } else if (info.has_posclk && !info.posclk_val && (info.enable_name.empty() || info.enable_is_const)) {
            // A conditional write already builds a hold mux and an active-high
            // enable from the same condition. Polarity may invert only a
            // separate explicit enable, never that combined write condition.
            dropped = "enable_high=false (active-low enable)";
            why
                = "an active-low latch requires a nonconstant explicit enable; "
                  "use enable=g with enable_high=false, or write if !g { ... }";
          }
          // The RESET FAMILY (reset_pin / sync / async / negreset / init) is no
          // longer refused: M7 wires it through the SHARED flop path below, so
          // a latch gets the same reset semantics a flop does and cgen emits it.
          if (!dropped.empty()) {
            error_here(
                "upass.tolg: latch '{}' carries '{}', which the Latch "
                "cell cannot honor — the attribute would be SILENTLY "
                "DROPPED. {}",
                name,
                dropped,
                why);
            continue;
          }
        }
        // A latch now FALLS THROUGH to the shared q-width / enable / reset
        // wiring below (2f-latch M7) instead of duplicating the first two and
        // refusing the third. Only the clock/posclk block is skipped: a latch's
        // gate IS its enable, so it has no clock identity to bind.
      }

      // clock: explicit clock_pin=NAME beats the implicit/shared clock input.
      // A named clock is usually a module input (clk_i), but can also be an
      // internal/derived wire — e.g. a gated clock (a clock-gate cell's
      // `clk & en_latch` output) feeding a flop. Check has_input FIRST (a clock
      // input is NOT in pin_map_; an unrelated same-named signal might be,
      // which is why pin_map_-first is wrong), then fall back to pin_map_ for
      // the internal-wire case. get_input_pin would assert on a non-input.
      if (info.is_latch) {
        // no clock identity: the gate IS the enable
      } else if (!info.clock_pin_name.empty()) {
        // 2c-wire — a wire clock signal (a gated/derived clock): use its DRIVER
        // (din) directly, not the passthrough buffer (cgen drops a buffer whose
        // only consumer is a flop control pin).
        const auto cp = info.clock_pin_is_const ? Pin{} : resolve_attr_signal(info.clock_pin_name);
        // User rulings 27/41/80: a register clocked by a constant never ticks
        // in the emitted Verilog while `lhd sim` steps it, whatever the front
        // end: `const k = 0` then `clock_pin=k`, a Verilog `posedge` on a
        // tied-off or unconnected (`0ub?`) wire, or on a gate a constant holds
        // (a `Clock_cell` whose enable is 0, a buffer instance fed a constant).
        // A constant bound to a callee's clock input is caught at the call
        // (check_const_clock_bind).
        if (info.clock_pin_is_const || clock_const_of(cp, {}, kClockHops)) {
          const auto top = lnast_->get_top_module_name();
          error_hint_at(info.decl_nid,
                        {"clock-const", "time"},
                        std::format("register `{}` of `{}` is clocked by a constant", name, top.substr(top.rfind('.') + 1)),
                        lnast_->is_verilog_origin() ? "a constant never ticks: clock it by a clock signal"
                                                    : "a constant never ticks: clock it by a clock signal (`clock_pin=clk`), "
                                                      "or drop `clock_pin` for the implicit clock");
        }
        if (cp.is_invalid()) {
          if (!info.clock_pin_is_const) {
            error_here(
                "upass.tolg: reg '{}' names clock_pin '{}' but '{}' has "
                "no such input/wire",
                name,
                info.clock_pin_name,
                lnast_->get_top_module_name());
          }
          continue;
        }
        check_clock_pin_class(info.decl_nid, std::format("register `{}`", name), info.clock_pin_name, cp);
        setup_sink_by_name(flop, "clock_pin").connect_driver(cp);
      } else if (!clock_name_.empty()) {
        setup_sink_by_name(flop, "clock_pin").connect_driver(clock_pin());
      } else {
        warn_at(info.decl_nid, {"no-clock", "time"}, "reg '{}' has no clock input to bind", name);
      }
      if (!info.is_latch && info.has_posclk && !info.posclk_val) {
        setup_sink_by_name(flop, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
      }

      // q width: untyped regs take the final din width (mw+1 unsigned).
      if (info.decl_mw == 0) {
        auto    dit = mw_map_.find(din_key(name));
        int32_t mw  = dit != mw_map_.end() ? dit->second : int32_t{1};
        set_ubits(q, mw);
        mw_map_[name] = mw;
      }

      // Explicit `:[enable=<ref>]`. Resolved HERE, not at the attr_set: prp2lnast
      // hoists the attribute ahead of the declare, so the temp holding
      // `(wen_i != 0)` is still undefined when lower_attr_set runs.
      Pin attr_en;
      Pin latch_gate;
      if (!info.enable_name.empty()) {
        if (info.enable_is_const) {
          // A CONSTANT enable names no signal: `enable=true` is the
          // constant-true CONDITION — "always update", no extra gate at all
          // (an invalid attr_en), which is already the Flop default. Not a
          // curiosity: a generic-parameterised `enable=(EN!=0)` folds to
          // exactly this. Without this arm it fell into the resolve arm below
          // and died with "names enable 'true' but … has no such input/wire".
          // (The value-less `:[enable]`, which the attribute grammar also
          // defaults to the text "true", is refused at the SOURCE by
          // prp2lnast's attr-needs-value rule — a pin attribute names a
          // signal, so a flag-only spelling says nothing. This arm still
          // covers it for any other LNAST producer.)
          const auto cv = Dlop::from_pyrope(info.enable_name);
          if (!cv || cv->is_invalid()) {
            error_here(
                "upass.tolg: reg '{}' enable value '{}' is neither a signal "
                "nor a compile-time constant",
                name,
                info.enable_name);
            continue;
          }
          if (cv->is_known_false()) {
            // A constant-0 enable CANNOT be emitted: cgen skips a const enable
            // pin outright (cgen_verilog `!enable_dpin.is_const()`), so wiring
            // one would compile to a register that updates every edge — the
            // same silent-drop failure the `enable_high=false` arm refuses.
            error_here(
                "upass.tolg: reg '{}' has `enable=false`, a register that can "
                "never update — drop the reg (use a const) or the attribute",
                name);
            continue;
          }
        } else {
          attr_en = resolve_attr_signal(info.enable_name);
          if (attr_en.is_invalid()) {
            error_here(
                "upass.tolg: reg '{}' names enable '{}' but '{}' has "
                "no such input/wire",
                name,
                info.enable_name,
                lnast_->get_top_module_name());
            continue;
          }
          if (info.is_latch) {
            latch_gate = attr_en;
          }
          attr_en = nonzero1(attr_en);
          if (info.is_latch && info.has_posclk && !info.posclk_val) {
            attr_en = not1(attr_en);
          }
        }
      }

      // enable: still the seeded false const => never written. For a Flop, the
      // true const needs no pin (unconditional edge update is the default)
      // unless `enable=` adds one. For a Latch it MUST remain explicit:
      // enable=true means always transparent, which lets cprop recognize that
      // the cell stores nothing and replace it with its combinational din. Any
      // other pin is the OR-of-conditions mux chain.
      if (auto eit = pin_map_.find(en_key(name)); eit != pin_map_.end()) {
        const auto en       = eit->second;
        // Identity by VALUE, not by node. Every const pin shares ONE pooled
        // master node (graph const pool), so comparing get_debug_nid() against
        // the memoized en_const() pins answered TRUE for both questions at
        // once: an unconditionally-written reg read as `is_false` (never
        // written) as well. On a FLOP both answers happened to land on the same
        // "wire no enable pin" outcome, which is why it hid; on a LATCH they do
        // not — an always-written latch lost the explicit enable=true that
        // cprop needs to collapse the always-open cell (latch_always_transparent),
        // and an explicit `:[enable=…]` needs them apart on either cell.
        const bool is_const = en.is_const();
        const bool is_true  = is_const && livehd::graph_util::const_of(en).is_known_true();
        const bool is_false = is_const && livehd::graph_util::const_of(en).is_known_false();
        if (!is_false) {
          if (info.is_latch) {
            // A latch has no clock to gate, so the transported instance
            // activation participates directly in its transparency enable.
            // Reset remains a separate, higher-priority control in cgen.
            const auto active = !valid_active_ ? Pin{} : valid_pin();
            // Dynamic enables come from lower_if's merged branch selectors,
            // which already include activation so din's hold mux and enable
            // remain structurally identical (the latch-contract proof relies
            // on that identity). Only an unconditional true needs gating here.
            Pin        lat_en = is_true ? active : (valid_minted_ ? en : and2(en, active));
            // The explicit enable (with its selected polarity) narrows the
            // write condition. Whenever the latch is transparent, that write
            // condition holds and the hold mux therefore supplies D.
            lat_en            = and2(lat_en, attr_en);
            if (lat_en.is_invalid()) {
              lat_en = en;  // unconditionally transparent: keep the explicit true
            }
            auto enable_sink = setup_sink_by_name(flop, "enable");
            enable_sink.connect_driver(lat_en);
            // Only the explicit latch pin may read a Clock's physical level.
            // Record its exact sink edges, including polarity/AND gates built
            // here. Unrelated logic using the same clock remains illegal.
            if (!latch_gate.is_invalid()) {
              absl::flat_hash_set<Pin> seen;
              std::function<void(Pin)> note = [&](Pin sink) {
                const auto driver = sink.get_driver_pin();
                if (driver == latch_gate) {
                  latch_gate_sinks_.insert(sink);
                  return;
                }
                if (driver.is_invalid() || driver.is_const() || livehd::graph_util::is_graph_input_pin(driver)
                    || !seen.insert(driver).second) {
                  return;
                }
                const auto node = driver.get_master_node();
                const auto op   = livehd::graph_util::type_op_of(node);
                if (op != Ntype_op::And && op != Ntype_op::EQ) {
                  return;
                }
                for (auto input : node.inp_sorted_pins()) {
                  note(input);
                }
              };
              note(enable_sink);
            }
          } else if (const Pin fin = is_true ? attr_en : and2(en, attr_en); !fin.is_invalid()) {
            // `enable=` ANDs onto the OR-of-write-conditions shadow, so a reg
            // that carries BOTH an attribute enable and a conditional write
            // keeps both guards. An unconditionally-written reg with no
            // attribute leaves the pin unwired (the Flop default already is
            // "update every edge").
            setup_sink_by_name(flop, "enable").connect_driver(fin);
          }
        }
      }

      // Reset wiring. Effective init: an explicit `initial=N` attr overrides
      // the declare's [value]; "nil" (or absent) = NO reset.
      const std::string init     = !info.initial_txt.empty() ? info.initial_txt : info.init_txt;
      const bool        has_init = !init.empty() && init != "nil";
      const bool        rp_false = info.reset_pin_name == "false";
      if (rp_false && has_init) {
        error_here(
            "upass.tolg: reg '{}' has a non-nil initializer but "
            "`reset_pin=false` — drop the init or the override",
            name);
        return;
      }
      const bool wants_reset = (has_init || (!info.reset_pin_name.empty() && !rp_false)) && !rp_false;
      if (!wants_reset) {
        continue;
      }

      Pin  rpin;
      bool neg = false;
      if (!info.reset_pin_name.empty()) {
        // Usually a graph input, but a reset synchronizer drives it from a
        // DERIVED module-level signal — wire from that signal's driver instead.
        // THE ladder lives in resolve_reset_signal so finalize_mems resolves a
        // register array's `reset_pin=<wire>` identically; the two used to
        // differ and an array silently lost its reset.
        rpin = resolve_reset_signal(info.reset_pin_name);
        neg  = reset_active_low(info.negreset, info.reset_pin_name);
      } else if (!reset_name_.empty()) {
        rpin = reset_pin();
        neg  = info.negreset.value_or(reset_neg_);
      } else {
        error_here(
            "upass.tolg: reg '{}' has a reset value but '{}' has no "
            "reset input (setup_io bug)",
            name,
            lnast_->get_top_module_name());
        return;
      }
      setup_sink_by_name(flop, "reset_pin").connect_driver(rpin);
      if (has_init && info.initial_is_ref) {
        auto initial_pin = resolve_attr_signal(init);
        if (initial_pin.is_invalid()) {
          error_here("upass.tolg: reg '{}' names unknown asynchronous load value '{}'", name, init);
          return;
        }
        setup_sink_by_name(flop, "initial").connect_driver(initial_pin);
      } else if (has_init) {
        // The reset value must be a compile-time constant. A body-`reg`'s
        // non-literal init is caught at the declare (lower_declare errors on
        // a ref init); an output-reg `-> (reg q = expr)` stringifies its
        // initializer, so a malformed parse can still arrive here — reject it
        // rather than deref a null Dlop.
        auto iv = Dlop::from_pyrope(init);
        if (iv->is_invalid()) {
          error_here(
              "upass.tolg: reg '{}' reset/initial value '{}' is not a "
              "compile-time constant",
              name,
              init);
          return;
        }
        if (iv->is_nil()) {
          iv = Dlop::create_integer(0);  // as always: a nil initial value is 0
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

  [[nodiscard]] bool is_output_port(std::string_view name) const {
    const auto& outs = lnast_->io_meta().outputs;
    return std::ranges::any_of(outs, [&](const Lnast_io_entry& e) { return canon_io_name(e.name) == name; });
  }

  // Per array output, which lanes are driven (see nil_array_output_lanes_).
  using Lane_coverage = absl::flat_hash_map<std::string, std::vector<bool>>;

  // Ruling 15 / 04-variables.md: an array output built lane by lane follows
  // the coverage rule of `mut v:[N]T = nil` -- every lane must be driven.
  // Reported on the output's declaration.
  void check_nil_array_outputs() {
    const auto io_n = lnast_->get_first_child(lnast_->get_root());
    const auto in_n
        = io_n.is_invalid() || !Lnast_ntype::is_io(lnast_->get_type(io_n)) ? Lnast_nid{} : lnast_->get_first_child(io_n);
    const auto out_n = in_n.is_invalid() ? in_n : lnast_->get_sibling_next(in_n);
    for (const auto& e : lnast_->io_meta().outputs) {
      const std::string name{canon_io_name(e.name)};
      const auto        it = nil_array_output_lanes_.find(name);
      if (it == nil_array_output_lanes_.end()) {
        continue;
      }
      const auto miss = std::ranges::find(it->second, false);
      if (miss == it->second.end()) {
        continue;
      }
      Lnast_nid decl = lnast_->get_root();
      for (auto st = out_n.is_invalid() ? out_n : lnast_->get_first_child(out_n); !st.is_invalid();
           st      = lnast_->get_sibling_next(st)) {
        if (const auto nm = lnast_->get_first_child(st); !nm.is_invalid() && canon_io_name(lnast_->get_name(nm)) == name) {
          decl = st;
          break;
        }
      }
      const auto lane = output_lane_text(name, std::distance(it->second.begin(), miss));
      auto       d    = locate_record(decl,
                                      livehd::diag::Severity::error,
                                      "undriven-output",
                                      "type",
                                      std::format("lane {} of output `{}` is never driven", lane.what, name));
      d.hint = std::format("every bit of a value built from pieces must be driven: write `{}{}`, or give `{}` a whole value first",
                           name,
                           lane.index,
                           name);
      livehd::diag::sink().stage(std::move(d));
      throw Eprp::parser_error(Pass::eprp, std::format("lane {} of output `{}` is never driven", lane.what, name));
    }
  }

  // The lanes driven after an `if`: those every arm drives, and, without an
  // `else`, the pre-`if` lanes too (an output no path had started building
  // yet has none).
  void merge_arm_coverage(const Lane_coverage& pre, const std::vector<Lane_coverage>& arms, bool has_else) {
    if (arms.empty() || (pre.empty() && std::ranges::all_of(arms, [](const Lane_coverage& a) { return a.empty(); }))) {
      return;
    }
    Lane_coverage out;
    const auto    merge_from = [&](const Lane_coverage& m) {
      for (const auto& [name, lanes] : m) {
        out.try_emplace(name, lanes.size(), true);
      }
    };
    merge_from(pre);
    for (const auto& a : arms) {
      merge_from(a);
    }
    const auto and_in = [&](const Lane_coverage& m) {
      for (auto& [name, lanes] : out) {
        const auto it = m.find(name);
        for (size_t i = 0; i < lanes.size(); ++i) {
          lanes[i] = lanes[i] && it != m.end() && i < it->second.size() && it->second[i];
        }
      }
    };
    for (const auto& a : arms) {
      and_in(a);
    }
    if (!has_else) {
      and_in(pre);
    }
    nil_array_output_lanes_ = std::move(out);
  }

  // A read of a lane of an array output built lane by lane (ruling 15: it
  // starts as nil) before that lane is driven reads nil, exactly as a read of
  // a never-written scalar output does: an error, never a silent 0. A runtime
  // index needs every lane driven.
  void check_nil_array_output_read(const Lnast_nid& nid, std::string_view name, const Lnast_nid& idx) {
    std::optional<int64_t> lane;
    if (Lnast_ntype::is_const(lnast_->get_type(idx))) {
      const auto ci = Dlop::from_pyrope(lnast_->get_name(idx));
      if (!ci || !ci->is_just_i64()) {
        return;  // diagnosed by the lane read
      }
      lane = ci->to_just_i64();
    }
    check_nil_array_output_lane(nid, name, lane);
  }
  // The same check for an already-flattened lane (nullopt: a runtime lane).
  void check_nil_array_output_lane(const Lnast_nid& nid, std::string_view name, std::optional<int64_t> lane) {
    const auto it = nil_array_output_lanes_.find(canon_io_name(name));
    if (it == nil_array_output_lanes_.end()) {
      return;
    }
    size_t miss = 0;
    if (lane) {
      if (*lane < 0 || static_cast<size_t>(*lane) >= it->second.size() || it->second[static_cast<size_t>(*lane)]) {
        return;  // driven, or out of range (diagnosed by the lane read)
      }
      miss = static_cast<size_t>(*lane);
    } else {
      const auto m = std::ranges::find(it->second, false);
      if (m == it->second.end()) {
        return;
      }
      miss = static_cast<size_t>(std::distance(it->second.begin(), m));
    }
    const auto canon = canon_io_name(name);
    error_at(nid,
             {"undriven-output", "type"},
             "lane {} of output `{}` is read before it is driven",
             output_lane_text(canon, static_cast<int64_t>(miss)).what,
             canon);
  }

  // How a diagnostic names a flat (row-major) lane of an array output: its
  // number, or for a multi-dimensional `r:[N][M]T` its per-dimension indices
  // (`r[5]` is no valid access there, `r[1][1]` is).
  struct Output_lane_text {
    std::string what;   // `5`, or `[1][1]`
    std::string index;  // `[5]`, or `[1][1]`
  };
  [[nodiscard]] Output_lane_text output_lane_text(std::string_view name, int64_t lane) const {
    std::vector<int64_t> dims;
    for (const auto& e : lnast_->io_meta().outputs) {
      if (canon_io_name(e.name) == name && !e.inner_dims.empty()) {
        dims.push_back(e.array_size);
        dims.insert(dims.end(), e.inner_dims.begin(), e.inner_dims.end());
        break;
      }
    }
    if (dims.size() < 2) {
      return {std::to_string(lane), std::format("[{}]", lane)};
    }
    std::string index;
    for (auto d = dims.size(); d-- > 0;) {
      index.insert(0, std::format("[{}]", lane % dims[d]));
      lane /= dims[d];
    }
    return {index, index};
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
    const std::string lhs_text{lnast_->get_name(lhs)};

    // Comptime arrays have already been evaluated and every runtime use is
    // materialized by the runner (for example as a tuple-literal reg init).
    // Their original initializer stores remain in the marked LNAST for source
    // fidelity, but they must not mint hardware.
    if (comptime_array_names_.contains(lhs_text)) {
      return;
    }
    // Docs (04b "comptime"): a `comptime` variable must be compile-time
    // known or a compile error is generated.
    if (comptime_scalar_names_.contains(lhs_text) && Lnast_ntype::is_ref(lnast_->get_type(rhs))
        && lnast_->get_sibling_next(rhs).is_invalid()) {
      error_hint_at(nid,
                    livehd::diag::Id{"comptime-not-constant", "type"},
                    std::format("`{}` is declared comptime but its value is not known at compile time", lhs_text),
                    "drop `comptime`, or compute the value only from constants and comptime values");
    }
    // The compile-time identity/encoding attrs of an enum entry
    // (prp2lnast's lower_enum_def: `store(carrier, '__enumentry', 'E.x')`,
    // `store(carrier, '__enumval', v)`). A port typed with the enum keeps the
    // enum's definition live, but these are never hardware.
    if (const auto val = lnast_->get_sibling_next(rhs);  // rhs is the attr key here
        Lnast_ntype::is_const(lnast_->get_type(rhs)) && !val.is_invalid() && lnast_->get_sibling_next(val).is_invalid()
        && (lnast_->get_name(rhs) == "__enumentry" || lnast_->get_name(rhs) == "__enumval")) {
      return;
    }

    // Combinational typed positional array. A two-child store is a whole-value
    // initializer/replacement; additional children are indices followed by the
    // element value. The live representation is a packed bus with element 0 in
    // the least-significant lane.
    if (auto ait = array_scalar_views_.find(lhs_text); ait != array_scalar_views_.end()) {
      auto& view = ait->second;
      auto  next = lnast_->get_sibling_next(rhs);
      if (next.is_invalid()) {
        const auto value = whole_array_value(rhs, view, lhs_text);
        if (value.pin.is_invalid()) {
          return;  // reported
        }
        record(lhs_text, value.pin, static_cast<int32_t>(view.size * view.elem_mw));
        if (const std::string key{canon_io_name(lhs_text)}; nil_array_output_lanes_.contains(key) || is_output_port(key)) {
          // A whole value drives every lane (also on a path of an `if` whose
          // other path builds the output lane by lane).
          nil_array_output_lanes_.insert_or_assign(key, std::vector<bool>(static_cast<size_t>(view.size), true));
        }
        return;
      }

      // store(lhs, idx..., value): one index per dimension (a multi-dimensional
      // port picks its lane row-major, pick_array_lane).
      std::vector<Lnast_nid> idxs{rhs};
      auto                   value_nid = next;
      for (auto after = lnast_->get_sibling_next(value_nid); !after.is_invalid(); after = lnast_->get_sibling_next(value_nid)) {
        idxs.push_back(value_nid);
        value_nid = after;
      }
      // CANONICAL key: record() strips the backtick quoting, so a flattened
      // struct-field array (`` `bht_d.valid` `` out of inou/slang) is keyed
      // `bht_d.valid` -- the raw spelling missed here and every such array
      // read "written before it has an initializer" at its first element store.
      const std::string base_key{canon_io_name(lhs_text)};
      const auto        packed_mw = static_cast<int32_t>(view.size * view.elem_mw);
      Val               base;
      if (auto base_it = pin_map_.find(base_key); base_it != pin_map_.end()) {
        base = Val{base_it->second, packed_mw};
      } else if (is_output_port(base_key)) {
        // An output starts as nil, like `mut v:[N]T = nil` (ruling 15), so an
        // array output may be built lane by lane. The unwritten lanes of that
        // base read 0 (set_mask_base's nil scalar), and a lane the body never
        // writes is an error once the body is lowered (check_nil_array_outputs).
        base = Val{create_const(*g_, *Dlop::create_integer(0)), packed_mw};
        nil_array_output_lanes_.try_emplace(base_key, static_cast<size_t>(view.size), false);
      } else {
        error_here("upass.tolg: array '{}' is written before it has an initializer", lhs_text);
        return;
      }
      auto lanes_it = nil_array_output_lanes_.find(base_key);
      auto iv       = leaf(value_nid);

      if (view.dims.size() > 1 || idxs.size() > 1) {
        const auto shape = view;  // leaf() inside the pick may grow array_scalar_views_
        const auto pick  = pick_array_lane(nid, idxs, shape, lhs_text);
        if (!pick.ok) {
          return;
        }
        write_array_lane(lhs_text, shape, base, iv, pick.lane, pick.dyn);
        if (pick.lane && lanes_it != nil_array_output_lanes_.end()) {
          lanes_it->second[static_cast<size_t>(*pick.lane)] = true;
        }
        return;
      }
      if (Lnast_ntype::is_const(lnast_->get_type(rhs))) {
        auto ci = Dlop::from_pyrope(lnast_->get_name(rhs));
        if (!ci || !ci->is_just_i64() || ci->to_just_i64() < 0 || ci->to_just_i64() >= view.size) {
          error_at(nid,
                   {"array-index-out-of-range", "type"},
                   "Pyrope array index {} is outside [0, {}) for '{}'",
                   lnast_->get_name(rhs),
                   view.size,
                   lhs_text);
          return;
        }
        write_array_lane(lhs_text, view, base, iv, ci->to_just_i64(), {});
        if (lanes_it != nil_array_output_lanes_.end()) {
          lanes_it->second[static_cast<size_t>(ci->to_just_i64())] = true;
        }
        return;
      }
      // A runtime index drives one lane per cycle: it proves no lane is
      // always driven, so it does not count toward coverage.
      auto index = leaf(rhs);
      write_array_lane(lhs_text, view, base, iv, std::nullopt, index);
      lower_array_index_assert(index, view.size, nid);
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
    // A bit-view or whole-value update of a typed array is
    // SSA-versioned (`r___ssa_N = packed_bus`). Keep that version as a scalar
    // packed alias with the original array layout. Subsequent bit reads use the
    // bus directly and element reads extract one declared-width lane.
    if (lnast_->get_sibling_next(rhs).is_invalid()) {
      const std::string base = logical_key(lhs_text);
      if (base != lhs_text) {
        std::optional<Array_scalar_view> view;
        if (auto ait = array_scalar_views_.find(base); ait != array_scalar_views_.end()) {
          view = ait->second;
        } else if (auto mit = mem_map_.find(base); mit != mem_map_.end()) {
          view = Array_scalar_view{
              .size        = mit->second.size,
              .dims        = mit->second.dims,
              .elem_mw     = mit->second.elem_mw,
              .elem_signed = mit->second.elem_signed,
          };
        }
        if (view) {
          // A constant keeps its own value here (`w = 0sb?` stays unknown); a
          // tuple literal (`w = (1, 3)`) has no pin and packs like an
          // initializer.
          const auto v = Lnast_ntype::is_const(lnast_->get_type(rhs)) ? leaf(rhs) : whole_array_value(rhs, *view, lhs_text);
          if (v.pin.is_invalid()) {
            return;  // reported
          }
          record(lhs_text, v.pin, v.mw);
          array_scalar_views_[lhs_text] = *std::move(view);
          return;
        }
      }
    }
    if (!lnast_->get_sibling_next(rhs).is_invalid()) {
      error_at(lhs,
               {"tuple-store-unsupported", "unsupported"},
               "upass.tolg: tuple/field store to '{}' has no hardware lowering "
               "— the elaboration left a multi-element "
               "store that cannot be turned into wires",
               lnast_->get_name(lhs));
    }
    auto lhs_name = lnast_->get_name(lhs);
    // `c = concat(...)` — the destination's declared width must equal the lane
    // sum exactly. Checked at the STORE (and at the declare below) because the
    // concat node's own dst is always a compiler temp, so this is the first
    // point where a user-declared name and a concat result meet.
    check_concat_dest_width(nid, lhs_name, rhs);
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
    if (wire_names_.contains(std::string(lhs_name))) {
      // 2c-wire — a store to a wire is (part of) its single combinational
      // driver, recorded on the SHADOW din key (reads keep seeing the buffer
      // output, so they stay position-independent). The branch-mux machinery
      // merges conditional writes before the buffer input is bound. A
      // `= nil` forward-declare is not a driver — skip it.
      if (Lnast_ntype::is_const(lnast_->get_type(rhs)) && lnast_->get_name(rhs) == "nil") {
        return;
      }
      auto v = leaf(rhs);
      record(din_key(lhs_name), v.pin, v.mw);
      maybe_bind_wire_shadow(din_key(lhs_name), v.pin, v.mw);
      return;
    }
    // 1a-mem — a plain `name = <tuple-literal-ref>` / `name = <__memory
    // result>` aliases the record instead of binding a scalar pin (the
    // literal/result has no pin; its consumers resolve through the record).
    if (Lnast_ntype::is_ref(lnast_->get_type(rhs))) {
      const std::string rhs_name(lnast_->get_name(rhs));
      // A field read of a still-nil output leaf (`r.a#[0..<2] = x` reads
      // `%t = r.a` as the write's base): remember the alias instead of
      // resolving the undriven leaf, so set_mask_base seeds it exactly like
      // the leaf itself. Any other read of the temp still errors.
      if (Lnast::is_tmp(lhs_name) && !pin_map_.contains(canon_io_name(rhs_name)) && scalar_decl_.contains(canon_io_name(rhs_name))
          && !reg_map_.contains(rhs_name) && !wire_names_.contains(rhs_name)) {
        nil_seed_alias_.insert_or_assign(std::string(canon_io_name(lhs_name)), std::string(canon_io_name(rhs_name)));
        return;
      }
      // prp_writer names a call result before feeding it to a stage:
      //   const t = pipe(...); stage[N] x = t
      // Preserve the callee's latency rider across that scalar alias so
      // create_stage_flop narrows N by the pipe's realized minimum instead of
      // charging N fresh flops on top of the callee.
      if (auto sit = sub_out_stages_.find(rhs_name); sit != sub_out_stages_.end()) {
        // Copy before inserting: operator[] may rehash the flat_hash_map and
        // invalidate sit before the RHS is read.
        const auto sub_out_copy                = sit->second;
        sub_out_stages_[std::string(lhs_name)] = sub_out_copy;
      }
      // Copy BEFORE inserting: operator[] may rehash and invalidate the
      // found iterator (the type_info_map rehash-invalidation UAF all over
      // again).
      if (auto tit = tuple_recs_.find(rhs_name); tit != tuple_recs_.end()) {
        auto rec_copy                      = tit->second;
        tuple_recs_[std::string(lhs_name)] = std::move(rec_copy);
        return;
      }
      if (auto ait = array_scalar_views_.find(rhs_name); ait != array_scalar_views_.end()) {
        auto view_copy                             = ait->second;
        array_scalar_views_[std::string(lhs_name)] = std::move(view_copy);
      } else if (auto mit = mem_map_.find(rhs_name); mit != mem_map_.end()) {
        // A whole array read is a packed snapshot, including a register
        // array's read_all bus. Retain its layout when the snapshot is copied
        // or updated through a bit view, so later element reads select lanes.
        array_scalar_views_[std::string(lhs_name)] = Array_scalar_view{
            .size        = mit->second.size,
            .dims        = mit->second.dims,
            .elem_mw     = mit->second.elem_mw,
            .elem_signed = mit->second.elem_signed,
        };
      }
      if (auto mrt = mem_results_.find(rhs_name); mrt != mem_results_.end()) {
        auto rec_copy                       = mrt->second;
        mem_results_[std::string(lhs_name)] = rec_copy;
        return;
      }
      // A call result bound to a named var (`mut tmp = add_sub(…)`): alias the
      // Sub_result so `tmp.add`/`tmp.sub` resolve through the named var, not
      // just the call's result temp. (Copy before insert — operator[] may
      // rehash and invalidate the found iterator.) A MULTI-output result has no
      // scalar pin, so the alias is the whole binding (return). A SINGLE-output
      // result IS scalar-bindable (the result temp has a direct pin record), so
      // alias it AND fall through to bind the scalar — otherwise a plain read
      // of the named var (`s1 + s2`, no `.field`) would find no driver.
      if (auto sft = staged_fields_.find(rhs_name); sft != staged_fields_.end()) {
        auto fields_copy                      = sft->second;
        staged_fields_[std::string(lhs_name)] = std::move(fields_copy);
        if (!sub_results_.contains(rhs_name)) {
          return;  // a staged tuple has no scalar pin
        }
      }
      if (auto srt = sub_results_.find(rhs_name); srt != sub_results_.end()) {
        const bool multi                    = srt->second.outputs.size() > 1;
        auto       rec_copy                 = srt->second;
        sub_results_[std::string(lhs_name)] = std::move(rec_copy);
        if (multi) {
          return;
        }
        // single-output: fall through to the scalar record below
      }
    }
    auto v = leaf(rhs);
    record(lhs_name, v.pin, v.mw);
  }

  // declare(ref(name), type, const("wire")): a single-driver combinational net
  // (2c-wire). Create the passthrough buffer (Or) and bind the name to its
  // OUTPUT so every read — including one before the driver appears textually —
  // resolves to the net (position-independent). Stores record the din shadow;
  // the completed write binds the buffer input; finalize_wires() only enforces
  // the undriven rule (the single-driver rule is a frontend
  // check). No flop.
  void lower_wire_declare(const Lnast_nid& name_nid, const Lnast_nid& type_nid, const Lnast_nid& decl_nid) {
    auto name = lnast_->get_name(name_nid);
    if (!type_nid.is_invalid() && Lnast_ntype::is_comp_type_array(lnast_->get_type(type_nid))) {
      error_here(
          "upass.tolg: array `wire` is not supported — declare an array "
          "as `mut`/`reg`; a `wire` is a scalar net");
      return;
    }
    Wire_info info;
    info.buf      = make_node(Ntype_op::Or);  // single-input Or = pure passthrough (cgen `out = a`)
    info.out      = info.buf.create_driver_pin(0);
    info.decl_nid = decl_nid;
    if (!type_nid.is_invalid()) {
      std::tie(info.decl_mw, info.is_signed) = declared_width(type_nid);
      const auto t                           = lnast_->get_type(type_nid);
      info.data_typed = t != Lnast_ntype::Lnast_ntype_prim_type_none && t != Lnast_ntype::Lnast_ntype_prim_type_clock;
    }
    wire_cells_[info.buf.get_debug_nid()] = info.data_typed;
    if (info.decl_mw > 0) {
      if (info.is_signed) {
        set_sbits(info.out, info.decl_mw);
      } else {
        set_ubits(info.out, info.decl_mw);
      }
      record(name, info.out, info.decl_mw);
    } else {
      set_bits(info.out,
               1);  // provisional; finalize_wires restamps from the driver
      set_unsign(info.out);
      record(name, info.out, 1);
    }
    // Keep the net's RTL name on the buffer output (cgen / pass-lec
    // readability).
    {
      std::string base{name};
      if (auto p = base.find("___ssa_"); p != std::string::npos) {
        base.resize(p);
      }
      if (!base.empty()) {
        livehd::graph_util::set_pin_name(info.out, base);
      }
    }
    wire_names_.insert(std::string(name));
    wire_order_.emplace_back(name);
    info.decl_color = cur_color_;
    wire_info_.emplace(std::string(name), std::move(info));
    // Under `::[timecheck=false]` (and in a Verilog net) a comb-cycle net may
    // close a same-cycle ring (e.g. a ready/valid handshake whose dataflow
    // loops through a submodule instance but is not a real comb loop). Cut its
    // buffer in-edge for the loop check, preserving the pre-2c-wire leniency.
    // Other Pyrope wires are NEVER cut, so a real comb loop through a wire
    // surfaces as a hard error.
    if (lnast_->is_timecheck_off()) {
      wire_cut_nids_.insert(wire_info_.at(name).buf.get_debug_nid());
    }
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
    auto type_nid = lnast_->get_sibling_next(name_nid);
    auto mode_nid = type_nid.is_invalid() ? type_nid : lnast_->get_sibling_next(type_nid);
    auto mode     = mode_nid.is_invalid() || !Lnast_ntype::is_const(lnast_->get_type(mode_nid))
                        ? std::string_view{}
                        : std::string_view(lnast_->get_name(mode_nid));
    // Remember the DECLARED width for every flavour of declare (mut/const/wire/
    // reg/latch alike) before the per-mode branches return. Concat lanes read
    // this; nothing else does, so an unrecognised/absent type simply records
    // nothing and a lane on that name errors instead of silently mis-sizing.
    if (!type_nid.is_invalid()) {
      const auto [dmw, dsigned] = declared_width(type_nid);
      record_decl_type(lnast_->get_name(name_nid), dmw, dsigned);
    }
    // A scalar `comptime const|mut`: constprop deletes every store whose value
    // folded, so a store that survives to lower_store carries a runtime value.
    if (mode.find("comptime") != std::string_view::npos
        && (type_nid.is_invalid() || !Lnast_ntype::is_comp_type_array(lnast_->get_type(type_nid)))) {
      comptime_scalar_names_.insert(std::string(lnast_->get_name(name_nid)));
    }
    // A declare's optional trailing [value] child carries the initializer, so
    // `const c:u12 = concat(a,b)` is checked here rather than at a store.
    for (auto c = mode_nid.is_invalid() ? mode_nid : lnast_->get_sibling_next(mode_nid); !c.is_invalid();
         c      = lnast_->get_sibling_next(c)) {
      if (Lnast_ntype::is_ref(lnast_->get_type(c))) {
        check_concat_dest_width(nid, lnast_->get_name(name_nid), c);
        break;
      }
    }
    // 2c-wire — a single-driver combinational net: declare its passthrough
    // buffer now so position-independent reads (a read before the driver) bind
    // to it; the completed write wires the buffer input to the single driver.
    if (mode == "wire" || mode.starts_with("wire ")) {
      lower_wire_declare(name_nid, type_nid, nid);
      return;
    }
    // Storage intent precedes representation: a `reg` array is persistent and
    // remains a Memory cell; a mut/const array is a combinational aggregate and
    // receives a packed scalar view that indexed operations can scalar-replace.
    const bool is_reg   = mode == "reg" || mode.starts_with("reg ");
    const bool is_latch = mode == "latch";  // level-sensitive latch (din+enable, no clock)
    if (!type_nid.is_invalid() && Lnast_ntype::is_comp_type_array(lnast_->get_type(type_nid))
        && (is_reg || mode == "mut" || mode == "const" || mode.starts_with("mut ") || mode.starts_with("const "))) {
      if (mode.find("comptime") != std::string_view::npos) {
        comptime_array_names_.insert(std::string(lnast_->get_name(name_nid)));
        return;
      }
      if (is_reg) {
        lower_mem_declare(name_nid, type_nid, mode_nid, /*is_array=*/false);
      } else {
        const auto elem_nid         = lnast_->get_first_child(type_nid);
        const bool multidimensional = !elem_nid.is_invalid() && Lnast_ntype::is_comp_type_array(lnast_->get_type(elem_nid));
        const bool has_inline_init  = !lnast_->get_sibling_next(mode_nid).is_invalid();
        if (multidimensional || has_inline_init) {
          // Slang ROM/array initializers are children of the declaration, and
          // nested arrays retain row-major address semantics. Keep those as a
          // Memory cell; the scalar view is for one-dimensional, store-driven
          // combinational arrays only.
          lower_mem_declare(name_nid, type_nid, mode_nid, /*is_array=*/true);
        } else {
          lower_comb_array_declare(name_nid, type_nid);
        }
      }
      return;
    }

    if (!is_reg && !is_latch) {
      // mut/const/type declares carry no graph payload here (values arrive
      // via their stores); nothing to lower. Remember the scalar name so a
      // later `b#[lo..=hi] = …` whose base is a still-undriven `mut b = nil`
      // can use a zero base instead of erroring (see set_mask_base).
      if (mode == "mut" || mode == "const" || mode.starts_with("mut ") || mode.starts_with("const ")) {
        // CANONICAL key: set_mask_base tests it against pin_map_, which
        // record()/resolve() key on the backtick-stripped name.
        scalar_decl_.insert(std::string(canon_io_name(lnast_->get_name(name_nid))));
      }
      if (mode == "const" || mode.starts_with("const ")) {
        const_decl_.insert(std::string(canon_io_name(lnast_->get_name(name_nid))));
      }
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
      p.decl_color                                            = cur_color_;
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
    // of the two front-ends in 1:1 correspondence (and the emitted Verilog
    // reads better). Strip any SSA suffix so it matches the logical (declared)
    // name.
    {
      std::string base{name};
      if (auto p = base.find("___ssa_"); p != std::string::npos) {
        base.resize(p);
      }
      if (!base.empty()) {
        livehd::graph_util::set_pin_name(q, base);
        // Also stamp the flop NODE name so hhds get_hier_name() (which reads
        // the node `name` attr, not the LiveHD pin attr) reports the register's
        // hierarchical name `inst.reg` instead of the `n<id>` fallback.
        flop.set_name(base);
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
        error_here(
            "upass.tolg: reg '{}' initializer is not a compile-time "
            "constant — a reset value must be comptime",
            name);
        return;
      }
    }

    if (info.decl_mw > 0) {
      if (info.is_signed) {
        set_sbits(q, info.decl_mw);
      } else {
        set_ubits(q, info.decl_mw);
      }
      record(name, q, info.decl_mw);
    } else {
      record(name, q, 1);  // provisional width; finalize_regs restamps from din
    }
    reg_map_.emplace(std::string(name), flop);
    reg_order_.emplace_back(name);
    info.decl_color = cur_color_;
    reg_info_.emplace(std::string(name), std::move(info));
    plain_reg_flops_[flop.get_debug_nid()] = std::string(name);
    // Seed the enable shadow false: a store rebinds it true, the branch-mux
    // machinery turns conditional writes into the OR-of-conditions chain.
    record(en_key(name), en_const(false), 1);
  }

  // Per-reg lowering state recorded at the declare; consumed by
  // finalize_regs() after every store/attr_set has been seen.
  struct Reg_info {
    hhds::Node_class    flop;
    Lnast_nid           decl_nid;
    int32_t             decl_color = 0;  // block region at the declare (finalize glue inherits it)
    std::string         init_txt;        // declare [value] child; "" = none, "nil" = explicit no-reset
    int32_t             decl_mw   = 0;   // declared type width; 0 = untyped
    bool                is_signed = false;
    // Per-reg flop-attr overrides (04b-attributes.md): a per-reg `sync` beats
    // the upass.reset_style flag; `reset_pin=false` opts out of reset.
    std::string         reset_pin_name;              // explicit reset_pin=NAME / "false"
    std::string         clock_pin_name;              // explicit clock_pin=NAME (beats implicit clock)
    bool                clock_pin_is_const = false;  // …that constprop folded to a constant (`const k = 0`)
    std::string         enable_name;                 // explicit enable=REF (ANDed onto the write-condition shadow)
    bool                enable_is_const = false;     // …and that REF is a const (`:[enable]`, `enable=false`), not a signal
    bool                has_posclk      = false;
    bool                posclk_val      = true;  // false = negedge clock
    bool                has_sync        = false;
    bool                sync_val        = true;
    std::optional<bool> negreset;  // explicit negreset=; unset = the reset's `_n` name picks it
    bool                initial_is_ref = false;
    std::string         initial_txt;       // explicit initial=N (overrides init_txt)
    bool                is_latch = false;  // mode "latch": Ntype_op::Latch, wire din+enable only
    // Hierarchical naming (call-site `name=` on an inlined comb / `reg
    // x::[name=]`):
    std::string         name_override;  // explicit `name=` — replaces the local flop name
    std::string         hier_prefix;    // runner-stamped `__hier` instance path (e.g.
                                        // "pipeB_ex_mem")
  };

  // Shadow pin_map_ keys for a reg's next-state value and write-enable. The
  // \x01 prefix cannot collide with user identifiers or `___N` temps.
  static constexpr std::string_view kDinPrefix{
      "\x01"
      "din:"};
  [[nodiscard]] static std::string din_key(std::string_view n) { return std::string(kDinPrefix).append(n); }
  [[nodiscard]] static std::string en_key(std::string_view n) {
    return std::string(
               "\x01"
               "en:")
        .append(n);
  }

  // The "hold" value for a reg's din shadow on an unwritten conditional path:
  // the reg's q (current value), so a conditional write auto-holds (Verilog
  // non-blocking `<=` semantics) even when no branch reads the reg (a pure
  // write). Without it the unwritten path falls back to a don't-care, which is
  // only masked by the enable shadow — fragile, and it balloons the merge width
  // (nil_pin() is 64 bits). Returns nullopt for a NON-reg merge var (e.g. a
  // combinational match-expression result), which legitimately keeps its
  // don't-care none-of slot. `var` is a din shadow key iff it carries the
  // din_key() `\x01din:` prefix.
  [[nodiscard]] std::optional<Pin> reg_hold_pin(std::string_view var) {
    constexpr std::string_view din_prefix{
        "\x01"
        "din:"};
    if (!var.starts_with(din_prefix)) {
      return std::nullopt;
    }
    std::string name(var.substr(din_prefix.size()));
    if (auto it = reg_map_.find(name); it != reg_map_.end()) {
      return it->second.create_driver_pin(0);  // flop q = current registered value
    }
    return std::nullopt;
  }

  // 2c-wire — is `var` the din shadow key of a declared `wire`? Unlike a reg, a
  // wire has NO hold value on a branch path that does not write it: the net is
  // defined by its ONE driver, so an unwritten path is a DON'T-CARE, not an X.
  // A conditionally written wire therefore carries the driver's value on EVERY
  // path — exactly as if the assignment had been written unconditionally (the
  // frontend allows the conditional form for that reason). The branch merges
  // below fill the unwritten paths with a WRITTEN value instead of nil, so a
  // single writing arm needs no mux at all.
  [[nodiscard]] bool is_wire_din(std::string_view var) const {
    // Heterogeneous lookup: this runs per merge variable, so do not mint a
    // std::string just to probe the set.
    return var.starts_with(kDinPrefix) && wire_names_.contains(var.substr(kDinPrefix.size()));
  }

  // The same don't-care rule for a `const` still carrying no value at this
  // point. `const` is SINGLE-ASSIGNMENT: the one bind defines it, so — exactly
  // like a `wire` — a branch path that does not write it is a don't-care, and
  // `const x:T = nil; if c { x = v }` means `x == v`, not `c ? v : x`.
  // `mut` is deliberately NOT included: it is last-write-wins, so an unwritten
  // path legitimately keeps the pre-if value, and slang's poison-init
  // accumulators DEPEND on that value staying the `0sb?`/nil seed.
  // Only reached with no pre-if value in pin_map_, i.e. genuinely unbound.
  [[nodiscard]] bool is_unbound_const(std::string_view var) const {
    return !const_decl_.empty() && !var.empty() && var.front() != '\x01' && const_decl_.contains(logical_key(var));
  }

  // Either single-driver net shape: a `wire` din shadow, or a still-unbound
  // `const`. Both fill an unwritten branch path with a WRITTEN value instead
  // of nil, so a single writing arm needs no mux at all.
  [[nodiscard]] bool is_single_bind_net(std::string_view var) const { return is_wire_din(var) || is_unbound_const(var); }

  // Cached 1/0 const pins for the enable shadow. The cache is a minting
  // shortcut ONLY: every constant pin in the graph shares the CONST_NODE
  // master, so node identity says nothing about a value — finalize_regs tests
  // `pin.is_const()` + `const_of(pin)` instead.
  [[nodiscard]] Pin en_const(bool v) {
    auto& pin   = v ? en_true_pin_ : en_false_pin_;
    auto& valid = v ? en_true_valid_ : en_false_valid_;
    if (!valid) {
      pin   = create_const(*g_, *Dlop::create_integer(v ? 1 : 0));
      valid = true;
    }
    return pin;
  }

  // ── 1a-mem: array-typed reg → Ntype_op::Memory
  // ────────────────────────────── One Memory cell per declared `reg
  // name:[N]T`; one write port per store site and one read port per tuple_get
  // site (no port merging here — that is a future LG pass). Per-port sink pids
  // stride by 12 (graph/cell.cpp); the r-th read port's data comes out on
  // driver pid (n_user_wr + r), so the write-site count is pre-scanned at the
  // declare.
  struct Mem_info {
    hhds::Node_class     node;
    Lnast_nid            decl_nid;                // the declare: finalize_mems anchors its diagnostics here
    int64_t              size = 0;                // total entries (∏dims)
    std::vector<int64_t> dims;                    // outer dim first; size 1 for a flat array
    int32_t              elem_mw            = 0;  // element max-value width
    bool                 elem_signed        = false;
    bool                 is_array           = false;  // type=2: mut/const array (no clock, no persistence)
    bool                 is_pub             = false;  // pub reg: a remote regref may attach accesses — no diagnostics
    bool                 init_wired         = false;
    int                  n_user_wr          = 0;  // pre-scanned program write sites
    int                  wr_next            = 0;
    bool                 has_store_clock    = false;
    bool                 first_store_posclk = true;
    bool                 mixed_store_edges  = false;
    Pin                  update_clock;
    bool                 update_posclk = true;
    int                  rd_next       = 0;
    // Write-port ordinals whose store carried NO chunk index. Their enable is
    // one bit, which a wensize > 1 memory reads as "chunk 0 only" — so on a
    // memory that ALSO takes chunked writes, finalize_mems has to replicate
    // that bit across every chunk or the write silently loses all but its
    // bottom chunk.
    std::vector<int>     plain_wr_ports;
    // Same-cycle ordering (Pyrope `ordering` attr): "program" (default) needs
    // each read port's POSITION in program order, so record `wr_next` as each
    // read port is minted — the number of program writes that textually
    // precede it. finalize_mems() turns this into the per-(read,write) `fwd`
    // matrix. "fwd" forwards every write to every read (position-blind),
    // "old" forwards nothing and every read is the DEFINED committed value,
    // "none" forwards nothing and a colliding read is UNDEFINED (the `undef`
    // matrix, graph/cell.cpp pid 15). "old" vs "none" is the distinction a
    // single `fwd` bit cannot make; the Verilog readers need "old" because a
    // nonblocking write is never visible to a same-timestep read.
    enum class Mem_order { program, fwd, old, none };
    int64_t             legacy_fwd_mask = 0;  // set when the deprecated `fwd=` attr is used
    bool                has_legacy_fwd  = false;
    std::vector<int>    rd_wr_before;  // per read port: writes minted before it
    // Per read port: the old-value read of a PARTIAL write (`mem[a]#[..] = v`
    // desugars to tuple_get + set_mask + store). `ordering` defines what a
    // same-cycle USER read observes; this read instead sees the entry as the
    // writes before it in this cycle left it, so two partial writes to one
    // entry merge in every ordering. Its matrix row stays committed and
    // forward_mem_read replays those writes in logic (wr_sites, bulk_sites).
    std::vector<bool>   rd_is_rmw;
    std::vector<Pin>    rd_addr;  // per read port: its flat address
    // Declared reset value (`reg m:[N]T = <const|tuple>`), packed entry 0 in
    // the low elem_mw bits — the SAME value the `initial` pin carries. Non-null
    // => finalize_mems wires the cell's whole-array `reset` (pin 14): the reset
    // restores every entry in ONE cycle, exactly like a scalar reg. nil => none.
    // (A value, not a spool_ptr: Mem_info is copied into mem_map_, and a null
    // spool_ptr cannot be copied -- its copy bumps the pointee's refcount.)
    std::optional<Dlop> reset_init;
    // Whole-array support: a runtime `mem = <bus>` store drives the cell's
    // `update` sink (size*elem_mw bus) instead of minting per-entry write
    // ports. A whole `x = mem` read materializes the async `read_all` driver
    // pin (cached so repeated reads share one output). For a registered array
    // the reset value bus rides the (now runtime-capable) `initial` sink + the
    // `reset` cond pin.
    bool                has_update = false;  // an update bus is wired (whole-array memory)
    Pin                 read_all_pin{};      // cached async read_all driver pin
    // Accumulator for MULTIPLE conditional whole-array stores (e.g. a reset arm
    // and a flush arm). Each later store folds into one
    // `update`/`update_enable` pair via a priority mux: `update_val = en ? this
    // : update_val` (later store wins where its path-cond holds) and `update_en
    // = update_en | en`. The if/else-if path conditions already encode source
    // priority (later arms negate earlier conditions), so "later wins" matches
    // Verilog nonblocking semantics.
    Pin                 update_val{};  // current accumulated update bus value
    Pin                 update_en{};   // current accumulated update enable (invalid => always-on)

    // Per write port, in program order: what forward_mem_read replays.
    struct Wr_site {
      Pin                 addr;
      Pin                 din;
      Pin                 en;          // the path condition before the chunk shift; invalid => always
      int                 chunk = -1;  // the wensize lane a chunked write stores, -1 = the whole entry
      // A write-MASKED partial write (Masked_pw_val): `bitmask` is the
      // entry-wide mask of the bits it writes (`cmask` when constant); invalid
      // => not one. finalize_mems turns it into the port's lane enable.
      Pin                 bitmask;
      std::optional<Dlop> cmask;
    };
    std::vector<Wr_site> wr_sites;
    bool                 has_masked_wr = false;  // some write port is a write-masked partial write
    // Per whole-array store, in program order. A bulk store and a per-entry
    // write of the same cycle resolve in PROGRAM order like any two writes
    // (the later one wins), while the cell's ladder lets every per-entry write
    // override the update bus. finalize_mems reconciles the two by gating each
    // write port with the enables of the bulk stores that follow it, and the
    // same-cycle reads replay these stores in place (forward_mem_read).
    struct Bulk_site {
      Pin val;            // the store's packed bus (entry 0 in the low bits)
      Pin en;             // its path condition; invalid => always
      int wr_before = 0;  // write ports minted before it
    };
    std::vector<Bulk_site>           bulk_sites;
    std::vector<int>                 rd_bulk_before;  // per read port: bulk stores before it
    Pin                              not_rst{};       // !reset once finalize_mems wires the reset; invalid => none
    // Per read port, decided by finalize_mems: the write ports and bulk stores
    // (a program-order prefix of each) that replay_mem_reads replays over it;
    // {0, 0} => none. The replay runs once every consumer of the dout is wired.
    std::vector<std::pair<int, int>> rd_replay;
    int64_t                          wensize = 1;  // the chunk count forward_mem_read splits a chunked write by
    // Per WHOLE-array read (`x = mem`, and every rolled-loop body that reads the
    // array), in program order. The cell's read_all output is the COMMITTED
    // contents -- it has no collision-matrix row -- so each read takes it
    // through its own passthrough Or (`pin`) and forward_read_all replays the
    // writes its `ordering` lets it see: "program" the ones before it, "fwd"
    // all of them, "old"/"none" none (the passthrough is then dropped). Without
    // it `reg r:[2]u4; if en { r[0] = d }; for i in 0..<2 { o[i] = r[i] }` read
    // the stale lane 0 while `q = r[0]` (a per-element port) read `d`.
    struct Read_all_site {
      Pin pin;
      int wr_before   = 0;
      int bulk_before = 0;
    };
    std::vector<Read_all_site>       read_all_sites;
    std::vector<std::pair<int, int>> ra_replay;  // per read_all site, like rd_replay
  };

  // One compiler temp on the old-value chain of a memory PARTIAL write: the
  // element read (tuple_get into a temp), a set_mask over it that MERGES the
  // written lane, and a get_mask that only narrows the merged value (the slang
  // reader's width cut). A store of a merged temp back to the entry it read
  // marks that read port as a read-modify-write (Mem_info::rd_is_rmw). User
  // names never join the chain: `x = mem[a]` then `mem[a] = x` is a user read
  // and keeps the memory's ordering, and so is an element read stored back
  // with no merge.
  struct Mem_rmw_read {
    std::string mem;
    int         rd_port = 0;
    bool        merged  = false;
  };

  // A memory partial write lowered WITHOUT its old-value read: the merged temp
  // holds only the written bits (placed at their position, the rest 0), and
  // `mask` says which bits of the entry it writes (entry-wide, `cmask` when it
  // is a constant). lower_mem_store turns it into a write-masked port and
  // finalize_mems sizes the memory's `wensize` lanes from every such mask.
  struct Masked_pw_base {
    std::string mem;
    Pin         addr;             // the entry the old-value read would have read
    int         wr_before   = 0;  // its program position, should it be minted after all
    int         bulk_before = 0;
  };
  struct Masked_pw_val {
    Masked_pw_base      base;
    Pin                 mask;
    std::optional<Dlop> cmask;
  };

  // Packed scalar SSA version of a combinational typed positional array.
  // The value itself lives in pin_map_; this side record preserves the array
  // extent and lane type so tuple_get can recover element semantics.
  struct Array_scalar_view {
    int64_t              size = 0;
    std::vector<int64_t> dims;
    int32_t              elem_mw     = 0;
    bool                 elem_signed = false;
  };

  // A whole-array constant (a broadcast store or initializer) as the integer
  // every entry takes: a bool is its u1 value (`reg v:[4]bool = false`, `v =
  // false`), anything else parses as written.
  [[nodiscard]] static spool_ptr<Dlop> whole_array_const(std::string_view txt) {
    auto v = Dlop::from_pyrope(txt);
    if (v && v->is_bool()) {
      return Dlop::create_integer(v->is_known_true() ? 1 : 0);
    }
    return v;
  }

  // The packed bus (element 0 in the low lane) a whole-array store `what = rhs`
  // gives a combinational array of shape `view`: a constant broadcast to every
  // element (`nil`/`0sb?` is zero-filled), a comptime tuple literal packed
  // element by element, or a runtime bus as is. Invalid after reporting.
  [[nodiscard]] Val whole_array_value(const Lnast_nid& rhs, const Array_scalar_view& view, std::string_view what) {
    const auto packed_mw = static_cast<int32_t>(view.size * view.elem_mw);
    if (Lnast_ntype::is_const(lnast_->get_type(rhs))) {
      auto txt = lnast_->get_name(rhs);
      auto v   = (txt == "nil" || txt == "0sb?") ? Dlop::create_integer(0) : whole_array_const(txt);
      if (!v || !v->is_integer()) {
        error_here("upass.tolg: whole-array value '{}' for '{}' is not an integer", txt, what);
        return {};
      }
      auto lane   = v->and_op(*Dlop::get_mask_value(view.elem_mw));
      auto packed = Dlop::create_integer(0);
      for (int64_t i = 0; i < view.size; ++i) {
        packed = packed->or_op(*lane->shl_op(*Dlop::create_integer(i * view.elem_mw)));
      }
      return {create_const(*g_, *packed), packed_mw};
    }
    if (Lnast_ntype::is_ref(lnast_->get_type(rhs))) {
      if (auto tit = tuple_recs_.find(std::string(lnast_->get_name(rhs)));
          tit != tuple_recs_.end() && tit->second.named.empty() && static_cast<int64_t>(tit->second.elems.size()) == view.size) {
        if (std::any_of(tit->second.elems.begin(), tit->second.elems.end(), [&](const auto& e) {
              return !Lnast_ntype::is_const(lnast_->get_type(e));
            })) {
          // A tuple of runtime arguments uses the same lane layout as a
          // constant tuple: element zero occupies the low element bits.
          auto packed = create_const(*g_, *Dlop::create_integer(0));
          for (int64_t i = 0; i < view.size; ++i) {
            const auto value = leaf(tit->second.elems[static_cast<size_t>(i)]);
            if (value.pin.is_invalid()) {
              return {};
            }
            const auto offset = i * view.elem_mw;
            auto       mask   = Dlop::get_mask_value(static_cast<int>(offset + view.elem_mw - 1), static_cast<int>(offset));
            auto       put    = make_node(Ntype_op::Set_mask);
            livehd::graph_util::connect_mask_operands(put,
                                                      packed,
                                                      livehd::graph_util::mask_window(*mask).first,
                                                      livehd::graph_util::mask_window(*mask).second,
                                                      value.pin);
            packed = put.create_driver_pin(0);
            set_ubits(packed, packed_mw);
          }
          return {packed, packed_mw};
        }
        auto packed = Dlop::create_integer(0);
        for (int64_t i = 0; i < view.size; ++i) {
          const auto e = tit->second.elems[static_cast<size_t>(i)];
          if (!Lnast_ntype::is_const(lnast_->get_type(e))) {
            error_here("upass.tolg: runtime tuple whole-array value for '{}' is not supported", what);
            return {};
          }
          auto ev = Dlop::from_pyrope(lnast_->get_name(e));
          if (!ev || !ev->is_integer()) {
            error_here("upass.tolg: array '{}' initializer element is not an integer", what);
            return {};
          }
          auto lane = ev->and_op(*Dlop::get_mask_value(view.elem_mw));
          packed    = packed->or_op(*lane->shl_op(*Dlop::create_integer(i * view.elem_mw)));
        }
        return {create_const(*g_, *packed), packed_mw};
      }
    }
    return leaf(rhs);
  }

  static constexpr int kMemPortStride = static_cast<int>(Ntype::Memory_port_stride);

  // Get-or-create the cell's async `read_all` driver pin (the whole-array read,
  // size*elem_mw bits wide, entry 0 in the low elem_mw). Cached on the Mem_info
  // so repeated whole reads share one output. Sits at the reserved driver pid
  // Memory_readall_pid (never collides with the sequential read douts).
  [[nodiscard]] Pin get_or_make_read_all(Mem_info& mi) {
    if (!mi.read_all_pin.is_invalid()) {
      return mi.read_all_pin;
    }
    auto d = mi.node.create_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
    set_ubits(d, static_cast<int>(mi.size * mi.elem_mw));
    mi.read_all_pin = d;
    return d;
  }  // Memory per-port sink stride, graph/cell.hpp

  // One whole-array read at this program position (Mem_info::Read_all_site):
  // a passthrough Or over the shared read_all pin that forward_read_all later
  // replaces by the committed contents with the cycle's visible writes applied.
  [[nodiscard]] Pin make_read_all_site(Mem_info& mi, const Pin& read_all) {
    auto buf = make_node(Ntype_op::Or);  // single-input Or = pure passthrough
    setup_sink_by_name(buf, "as").connect_driver(read_all);
    auto out = buf.create_driver_pin(0);
    set_ubits(out, static_cast<int>(mi.size * mi.elem_mw));
    mi.read_all_sites.push_back({.pin = out, .wr_before = mi.wr_next, .bulk_before = static_cast<int>(mi.bulk_sites.size())});
    return out;
  }

  // Branch path condition for memory write enables AND for property guards.
  //
  // The stack holds UNMATERIALIZED terms — the branch condition pins, which
  // exist anyway as mux selectors — and the and2/not1/nonzero1 chain is built
  // only when a consumer actually asks. That is why tracking can now be
  // UNCONDITIONAL: an `if` in a body with no memory and no property mints no
  // cells at all, so there is nothing to dead-strip and no node-id churn (the
  // measured symptom of always materializing was every emitted signal in every
  // design with an `if` getting renumbered).
  //
  // Unconditional tracking is what makes the path condition correct rather than
  // discovery-ordered. The old gate was `!mem_map_.empty()`, evaluated once when
  // lower_if was ENTERED, so a memory DECLARED INSIDE a branch was invisible:
  // `if c1 { reg m:[4]u8 = …; if c2 { m[a] = d } }` emitted `wr_enable = c2`,
  // dropping c1 entirely and writing the memory on a cycle the source does not.
  // Arming it from a property pre-scan instead only moved the seam — the same
  // body then lowered differently depending on whether an unrelated assert
  // existed elsewhere in it.
  //
  // Folds are memoized per prefix, so N consumers under one branch share cells.
  [[nodiscard]] Pin current_path_cond() {
    if (path_terms_.empty()) {
      return Pin{};
    }
    size_t i = 0;
    while (i < path_folded_.size() && !path_folded_[i].is_invalid()) {
      ++i;
    }
    Pin acc = i == 0 ? Pin{} : path_folded_[i - 1];
    for (; i < path_terms_.size(); ++i) {
      // nonzero1 FIRST: and2/not1 stamp bits=1, so a multi-bit branch condition
      // fed in raw would contribute only its LSB and silently narrow the path.
      Pin one = nonzero1(path_terms_[i].cond);
      if (path_terms_[i].negated) {
        one = not1(one);
      }
      acc             = and2(acc, one);
      path_folded_[i] = acc;
    }
    return acc;
  }

  // Full execution context for state, calls and source-visible effects. A
  // definition's transported activation composes with its local branch path;
  // an invalid term denotes constant true and therefore mints no glue.
  [[nodiscard]] Pin effect_path_cond() {
    const auto local = current_path_cond();
    return !valid_active_ ? local : and2(valid_pin(), local);
  }

  // Push one term (a branch condition, or its negation for a later arm/else).
  void push_path_term(const Pin& cond, bool negated) {
    path_terms_.push_back({cond, negated});
    path_folded_.emplace_back();  // lazily materialized by current_path_cond()
  }
  void truncate_path_terms(size_t depth) {
    path_terms_.resize(depth);
    path_folded_.resize(depth);
  }

  // a AND b as a 1-bit unsigned pin; an invalid operand means "true".
  [[nodiscard]] Pin and2(const Pin& a, const Pin& b) {
    if (a.is_invalid()) {
      return b;
    }
    if (b.is_invalid()) {
      return a;
    }
    auto node = make_node(Ntype_op::And);
    livehd::graph_util::setup_sink_pid(node, 0).connect_driver(a);
    livehd::graph_util::setup_sink_pid(node, 0).connect_driver(b);
    auto d = node.create_driver_pin(0);
    set_ubits(d, 1);
    return d;
  }

  // a OR b as an unsigned Boolean value; an invalid operand is the identity (returns
  // the other), so an unguarded caller mints no cell at all.
  [[nodiscard]] Pin or2(const Pin& a, const Pin& b) {
    if (a.is_invalid()) {
      return b;
    }
    if (b.is_invalid()) {
      return a;
    }
    const auto lhs  = nonzero1(a);
    const auto rhs  = nonzero1(b);
    auto       node = make_node(Ntype_op::Or);
    livehd::graph_util::setup_sink_pid(node, 0).connect_driver(lhs);
    livehd::graph_util::setup_sink_pid(node, 0).connect_driver(rhs);
    auto d = node.create_driver_pin(0);
    set_ubits(d, 1);
    return d;
  }

  // A glitch-free clock gate. `en` is sampled by the backend on the inactive
  // clock phase; div=1 and the `invert` flavour are explicit so every consumer
  // sees the same v1 contract rather than relying on implicit pin defaults.
  // `invert` is the ACTIVE-LOW flavour (`clk | ~en_latch`: the output idles high,
  // the enable latch is transparent while the clock is HIGH, and the gated event
  // is the fall).
  [[nodiscard]] Pin clock_gate(const Pin& clk, const Pin& en, bool invert = false) {
    if (clk.is_invalid() || en.is_invalid()) {
      return clk;
    }
    auto cell = make_node(Ntype_op::Clock_cell);
    setup_sink_by_name(cell, "clk_ref").connect_driver(clk);
    setup_sink_by_name(cell, "div").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    setup_sink_by_name(cell, "en").connect_driver(nonzero1(en));
    setup_sink_by_name(cell, "invert").connect_driver(create_const(*g_, *Dlop::create_integer(invert ? 1 : 0)));
    auto out = cell.create_driver_pin(0);
    set_ubits(out, 1);
    return out;
  }

  // "a != 0" as a 1-bit unsigned pin: an OR-reduction over every bit, which is
  // exactly the nonzero test regardless of width or signedness (a two's
  // complement value is nonzero iff some bit is set).
  //
  // Needed because and2/or2/not1 all stamp their driver `bits=1, unsigned`.
  // Feeding a multi-bit value straight into one of them therefore keeps
  // only its LSB. That is harmless for a comparison result (already 1 bit) and a
  // silent miscompile for anything wider, so a wide operand must be reduced
  // BEFORE it reaches them. A 1-bit input makes this a no-op the folder removes.
  [[nodiscard]] Pin nonzero1(const Pin& a) {
    if (a.is_invalid()) {
      return a;
    }
    // A 1-bit operand already IS its own nonzero test, so return it untouched.
    // This is not just an optimization: the path condition feeds SYNTHESIZABLE
    // logic (a memory write enable), and every cell minted here has to survive
    // pass.abc. Practically every branch condition is a comparison or a bool, so
    // the common path must add nothing at all — and does not.
    if (pin_mw_of(a) <= 1) {
      return a;
    }
    // Wider: `a != 0` as EQ-to-zero plus a NOT. Deliberately NOT Ntype_op::Ror,
    // which is the obvious spelling and has no combinational bit-blast in
    // pass.abc — a Ror on a memory write-enable cone made `lhd pass abc` fail
    // with "cell 'ror' ... has no combinational bit-blast yet". eq and not are
    // both in abc's supported set.
    auto eq = make_node(Ntype_op::EQ);
    livehd::graph_util::setup_sink_pid(eq, 0).connect_driver(a);
    livehd::graph_util::setup_sink_pid(eq, 0).connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    auto z = eq.create_driver_pin(0);
    set_ubits(z, 1);
    return not1(z);
  }

  // Truth-value negation. Keep this robust at transported GraphIO/Sub
  // boundaries where the base pin may not carry the declaration's width attr:
  // EQ-to-zero is exact for both an honest u1 and any wider condition.
  [[nodiscard]] Pin not1(const Pin& a) {
    auto node = make_node(Ntype_op::EQ);
    livehd::graph_util::setup_sink_pid(node, 0).connect_driver(a);
    livehd::graph_util::setup_sink_pid(node, 0).connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    auto d = node.create_driver_pin(0);
    set_ubits(d, 1);
    return d;
  }

  // A 1-bit condition shifted to one-hot position `amount` (unique-if
  // selector packing). The value reaches 1<<amount (amount+1 magnitude
  // bits), hence an unsigned literal width of amount+1.
  [[nodiscard]] Pin shl1_by(const Pin& a, int amount) {
    if (amount == 0) {
      return a;
    }
    auto node = make_node(Ntype_op::SHL);
    setup_sink_by_name(node, "a").connect_driver(a);
    setup_sink_by_name(node, "b").connect_driver(create_const(*g_, *Dlop::create_integer(amount)));
    auto d = node.create_driver_pin(0);
    set_ubits(d, amount + 1);
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
      error_here(
          "upass.tolg: memory '{}' has {} dimension(s) but the access "
          "supplies {} index(es)",
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
        error_here(
            "upass.tolg: memory '{}' index '{}' is not an integer — "
            "field access on a memory is not supported",
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
        setup_sink_by_name(mul, "as").connect_driver(acc_p);
        setup_sink_by_name(mul, "as").connect_driver(create_const(*g_, *Dlop::create_integer(d)));
        auto md  = mul.create_driver_pin(0);
        acc_mw  += mw_of_val(d);
        set_ubits(md, acc_mw);
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
        ip     = v.pin;
        imw    = v.mw;
      }
      auto add = make_node(Ntype_op::Sum);
      setup_sink_by_name(add, "as").connect_driver(acc_p);
      setup_sink_by_name(add, "as").connect_driver(ip);
      auto ad = add.create_driver_pin(0);
      acc_mw  = std::max(acc_mw, imw) + 1;
      set_ubits(ad, acc_mw);
      acc_p = ad;
    }
    if (acc_c) {
      return create_const(*g_, *Dlop::create_integer(*acc_c));
    }
    return acc_p;
  }

  // One immutable-tree pre-scan for indexed stores.  The former implementation
  // repeated this full recursive walk once per memory declaration.
  void index_mem_write_sites() {
    mem_write_site_counts_.clear();
    masked_pw_gets_.clear();
    masked_pw_bases_.clear();
    masked_pw_vals_.clear();
    // Memory partial-write candidates (masked_pw_candidate) and the uses of
    // every compiler temp, so a candidate whose temps escape is dropped.
    std::vector<std::pair<Lnast_nid, std::array<std::string, 2>>> pw_cands;
    absl::flat_hash_map<std::string, int>                         tmp_uses;
    int                                                           rolled_depth = 0;
    std::function<void(const Lnast_nid&)>                         walk         = [&](const Lnast_nid& nid) {
      if (lnast_->is_dce_dead(nid)) {
        return;  // dce:mark — the lowering skips dead stores; counting them
                 // here would desync the pre-scan exactly like the decl-store
      }
      if (Lnast_ntype::is_ref(lnast_->get_type(nid))) {
        if (Lnast::is_tmp(lnast_->get_name(nid))) {
          ++tmp_uses[std::string(lnast_->get_name(nid))];
        }
      }
      if (rolled_depth == 0) {
        if (auto temps = masked_pw_candidate(nid)) {
          pw_cands.emplace_back(nid, *temps);
        }
      }
      if (Lnast_ntype::is_store(lnast_->get_type(nid))) {
        auto c0 = lnast_->get_first_child(nid);
        if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast_->get_type(c0))) {
          auto c1 = lnast_->get_sibling_next(c0);
          if (!c1.is_invalid()) {
            auto c2 = lnast_->get_sibling_next(c1);
            // A real memory write is store(mem, idx, val); a typed declaration
            // — store(name, init, TYPE) — also has 3 children but its last is a
            // type node (an unpacked-array OUTPUT port emits both a flat packed
            // `= nil : int` decl-store AND its comp_type_array memory declare).
            // Counting the decl-store as a write desyncs the pre-scan from the
            // lowering (which ignores it) — skip type-tailed stores.
            if (!c2.is_invalid() && !Lnast_ntype::is_type(lnast_->get_type(c2))) {
              ++mem_write_site_counts_[lnast_->get_name_id(c0)];
            }
          }
        }
      }
      // A rolled_for keeps its source body for reference only; lower_rolled_for
      // lowers just the payload, so a store left in the source body is no site.
      const bool rolled  = Lnast_ntype::is_rolled_for(lnast_->get_type(nid));
      size_t     pos     = 0;
      rolled_depth      += rolled ? 1 : 0;
      for (auto c = lnast_->get_first_child(nid); !c.is_invalid(); c = lnast_->get_sibling_next(c), ++pos) {
        if (!rolled || pos != lnast_rolled_for::source_body) {
          walk(c);
        }
      }
      rolled_depth -= rolled ? 1 : 0;
    };
    walk(lnast_->get_root());
    // Each temp of the triple is defined once and read once (by the next node
    // of the triple): anything else reads the old value or the merged one, and
    // needs the real read port.
    for (const auto& [get_nid, temps] : pw_cands) {
      if (tmp_uses[temps[0]] == 2 && tmp_uses[temps[1]] == 2) {
        masked_pw_gets_.insert(get_nid);
      }
    }
  }

  // A memory PARTIAL write `mem[i…]#[mask] = v` as the three statements the
  // front end desugars it to:
  //   tuple_get(%t0, mem, i…)   set_mask(%t1, %t0, v, lo, hi)   store(mem, i…, %t1)
  // with only temp arithmetic between them (the mask of a runtime position, a
  // store index spelled again), the store naming the same memory with as many
  // index operands. Returns the
  // names of %t0 and %t1 when `nid` is the tuple_get of such a triple. Whether
  // the memory can take a write mask is decided later (lower_tuple_get).
  [[nodiscard]] std::optional<std::array<std::string, 2>> masked_pw_candidate(const Lnast_nid& nid) const {
    using N = Lnast_ntype;
    if (!N::is_tuple_get(lnast_->get_type(nid))) {
      return std::nullopt;
    }
    // A runtime position computes its mask between the read and the set_mask
    // (`shl %m, 1, k`; `plus`/`minus`/`range` for `#[j..+4]`), and a `wrap`
    // value is typed and cut there: pure temp arithmetic, which a masked write
    // keeps in place.
    auto pure_temp_op = [&](const Lnast_nid& n) {
      const auto t = lnast_->get_type(n);
      if (!(N::is_plus(t) || N::is_minus(t) || N::is_mult(t) || N::is_shl(t) || N::is_sra(t) || N::is_range(t) || N::is_bit_and(t)
            || N::is_bit_or(t) || N::is_bit_xor(t) || N::is_bit_not(t) || N::is_get_mask(t) || N::is_sext(t)
            || N::is_type_spec(t))) {
        return false;
      }
      const auto d = lnast_->get_first_child(n);
      return !d.is_invalid() && N::is_ref(lnast_->get_type(d)) && Lnast::is_tmp(lnast_->get_name(d));
    };
    // dce:mark leaves dead statements in place; the lowering skips them.
    auto sm = lnast_->get_sibling_next(nid);
    while (!sm.is_invalid() && (lnast_->is_dce_dead(sm) || pure_temp_op(sm))) {
      sm = lnast_->get_sibling_next(sm);
    }
    // A computed index is lowered again for the store (prp2lnast spells it
    // twice), between the set_mask and the store; lower_mem_store checks the
    // two addresses agree.
    auto index_temp_op = [&](const Lnast_nid& n) {
      if (pure_temp_op(n)) {
        return true;
      }
      const auto d = lnast_->get_first_child(n);
      return N::is_tuple_get(lnast_->get_type(n)) && !d.is_invalid() && N::is_ref(lnast_->get_type(d))
             && Lnast::is_tmp(lnast_->get_name(d));
    };
    auto st = sm.is_invalid() ? sm : lnast_->get_sibling_next(sm);
    while (!st.is_invalid() && (lnast_->is_dce_dead(st) || index_temp_op(st))) {
      st = lnast_->get_sibling_next(st);
    }
    if (st.is_invalid() || !N::is_set_mask(lnast_->get_type(sm)) || !N::is_store(lnast_->get_type(st))) {
      return std::nullopt;
    }
    auto kids = [&](const Lnast_nid& n) {
      std::vector<Lnast_nid> v;
      for (auto c = lnast_->get_first_child(n); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
        v.push_back(c);
      }
      return v;
    };
    const auto g = kids(nid);  // %t0, mem, i…
    const auto m = kids(sm);   // %t1, %t0, v, lo, [hi]
    const auto s = kids(st);   // mem, i…, %t1
    if (g.size() < 3 || (m.size() != 4 && m.size() != 5) || s.size() != g.size()) {
      return std::nullopt;
    }
    auto is_tmp_ref = [&](const Lnast_nid& n) { return N::is_ref(lnast_->get_type(n)) && Lnast::is_tmp(lnast_->get_name(n)); };
    if (!is_tmp_ref(g[0]) || !is_tmp_ref(m[0]) || !N::is_ref(lnast_->get_type(g[1])) || !N::is_ref(lnast_->get_type(s[0]))
        || !N::is_ref(lnast_->get_type(m[1])) || !N::is_ref(lnast_->get_type(s.back()))) {
      return std::nullopt;
    }
    const std::string t0{lnast_->get_name(g[0])};
    const std::string t1{lnast_->get_name(m[0])};
    if (t0 == t1 || lnast_->get_name(m[1]) != t0 || lnast_->get_name(s.back()) != t1
        || lnast_->get_name(g[1]) != lnast_->get_name(s[0])) {
      return std::nullopt;
    }
    return std::array<std::string, 2>{t0, t1};
  }

  [[nodiscard]] int count_mem_write_sites(const Lnast_nid& name_nid) const {
    if (auto it = mem_write_site_counts_.find(lnast_->get_name_id(name_nid)); it != mem_write_site_counts_.end()) {
      return it->second;
    }
    return 0;
  }

  // Why an array declared `[]` or without an element type (08-memories.md:
  // "arrays ... can be inferred automatically") did not lower: upass.bitwidth
  // sizes and types it only from uses that settle it.
  static constexpr std::string_view kInferHint
      = "every index needs a bounded range, every stored value a bounded range (a value read back from the array "
        "itself has none), and a tuple initializer must list every indexed entry; otherwise declare it `[N]T`";

  // A mut/const positional array is combinational aggregate storage, not a
  // persistent Memory. Preserve its declared shape while representing the live
  // value as one packed scalar bus; indexed reads/writes below recover lanes.
  // This keeps the logical LNAST array intact and leaves any physical
  // per-lane expansion to downstream transformations.
  //
  // ONE-DIMENSIONAL by construction: lower_declare sends a nested
  // `comp_type_array` element (and any inline initializer) to lower_mem_declare
  // instead, so the element type reaching here is always a scalar. A nested
  // element that ever did reach here would take the "sized scalar element type"
  // error below (declared_width of an array type is 0), never a silent
  // mis-lowering — so there is no multi-dimension walk to maintain here.
  void lower_comb_array_declare(const Lnast_nid& name_nid, const Lnast_nid& type_nid) {
    auto    elem_nid = lnast_->get_first_child(type_nid);
    auto    len_nid  = elem_nid.is_invalid() ? elem_nid : lnast_->get_sibling_next(elem_nid);
    int64_t size     = 0;
    if (!elem_nid.is_invalid() && !len_nid.is_invalid()) {
      std::string len_txt{lnast_->get_name(len_nid)};
      const auto  lanes = Lnast_ntype::is_const(lnast_->get_type(len_nid)) ? upass::array_dim_lanes(len_txt) : std::nullopt;
      if (!lanes && len_txt == "[]") {
        error_here("upass.tolg: array '{}' size could not be inferred from its uses: {}", lnast_->get_name(name_nid), kInferHint);
        return;
      }
      if (!lanes) {
        error_here(
            "upass.tolg: array '{}' size '{}' is not a positive comptime constant (a named constant must fold before lowering)",
            lnast_->get_name(name_nid),
            lnast_->get_name(len_nid));
        return;
      }
      size = *lanes;
    }
    const auto [elem_mw, elem_signed] = declared_width(elem_nid);
    if (size <= 0 || elem_mw <= 0) {
      if (len_nid.is_invalid()) {  // `mut a:[13] = …` / `mut a:[] = …`: no element type
        error_here("upass.tolg: array '{}' element type could not be inferred from its uses: {}",
                   lnast_->get_name(name_nid),
                   kInferHint);
      } else {
        error_here("upass.tolg: array '{}' requires a sized scalar element type", lnast_->get_name(name_nid));
      }
      return;
    }
    array_scalar_views_[std::string(lnast_->get_name(name_nid))] = Array_scalar_view{
        .size        = size,
        .dims        = {size},
        .elem_mw     = elem_mw,
        .elem_signed = elem_signed,
    };
  }

  // declare(ref name, comp_type_array(elem_type, const '[N]'), const mode
  // [, init]) — two flavors sharing one lowering:
  //  * reg  → async memory (type=0, fwd=1, 0-cycle read): writes commit at
  //    the cycle edge, same-cycle reads see them through forwarding. Only a
  //    nil/0sb? initializer is accepted in this slice (no reset hardware;
  //    the reset-sweep FSM is a later slice).
  //  * mut/const → comb array (type=2, no clock, no cross-cycle
  //    persistence): the per-cycle default is the init contents (the
  //    whole-array store wires the `initial` pin); a const array with runtime
  //    reads is a ROM (init + read ports only).
  void lower_mem_declare(const Lnast_nid& name_nid, const Lnast_nid& type_nid, const Lnast_nid& mode_nid, bool is_array) {
    auto name     = lnast_->get_name(name_nid);
    auto elem_nid = lnast_->get_first_child(type_nid);
    auto len_nid  = elem_nid.is_invalid() ? elem_nid : lnast_->get_sibling_next(elem_nid);
    if (elem_nid.is_invalid() || len_nid.is_invalid()) {
      // upass.bitwidth already sized/typed every array its uses settle.
      error_here("upass.tolg: memory '{}' element type or size could not be inferred from its uses: {}", name, kInferHint);
      return;
    }
    // Collect the dimension chain — each comp_type_array level is
    // (elem | nested comp_type_array, const '[N]'), nested OUTER dim first
    // (`[4][8]u8` → top len is '[4]'). The flat entry layout is row-major
    // (matrix_partial.prp contract): index (i,j) over dims (D0,D1) lands at
    // flat address i*D1 + j.
    std::vector<int64_t> dims;
    while (true) {
      // The size const's text is the raw '[N]' annotation (array_dim_lanes
      // strips the brackets). Going through the shared reader is what keeps a
      // still-unfolded `[N]` from lowering as 78 entries here, the way it used
      // to when this site called Dlop::from_pyrope directly.
      const auto len_txt = std::string(lnast_->get_name(len_nid));
      const auto d       = upass::array_dim_lanes(len_txt).value_or(0);
      if (d <= 0 && len_txt == "[]") {
        error_here("upass.tolg: memory '{}' size could not be inferred from its uses: {}", name, kInferHint);
        return;
      }
      if (d <= 0) {
        error_here(
            "upass.tolg: memory '{}' size '{}' is not a positive "
            "comptime constant",
            name,
            lnast_->get_name(len_nid));
        return;
      }
      dims.emplace_back(d);
      if (!Lnast_ntype::is_comp_type_array(lnast_->get_type(elem_nid))) {
        break;
      }
      auto inner_elem = lnast_->get_first_child(elem_nid);
      auto inner_len  = inner_elem.is_invalid() ? inner_elem : lnast_->get_sibling_next(inner_elem);
      if (inner_elem.is_invalid() || inner_len.is_invalid()) {
        error_here(
            "upass.tolg: memory '{}' array type is missing its element "
            "type or size",
            name);
        return;
      }
      elem_nid = inner_elem;
      len_nid  = inner_len;
    }
    auto [elem_mw, elem_signed] = declared_width(elem_nid);
    if (elem_mw == 0) {
      error_here(
          "upass.tolg: memory '{}' element type must be a sized integer "
          "or bool",
          name);
      return;
    }
    int64_t size = 1;
    for (auto d : dims) {
      size *= d;
    }
    // reg initializer — same treatment as a mut array (reg and not-reg
    // initialize alike): a concrete value becomes POWER-ON contents on the
    // `initial` pin (a scalar broadcasts to every entry; a tuple literal packs
    // per entry). nil / 0sb? = uninitialized. It is ALSO the RESET value of
    // every entry (the same statement `= <const>` makes on a scalar reg), so
    // the module has a reset by construction and finalize_mems() wires the
    // cell's whole-array `reset` pin, which reloads the `initial` contents in
    // ONE cycle. mut/const arrays get theirs via the whole-array store instead.
    spool_ptr<Dlop>                       reg_init;
    std::vector<spool_ptr<Dlop>>          init_entries;
    // Read an INLINE init child on the declare. The Pyrope frontend gives
    // mut/const arrays their init via a separate whole-array store (so an array
    // declare has no child after `mode`, and this loop is a no-op for it); the
    // slang reader instead emits the `initial` contents INLINE as a scalar
    // const or a tuple_add literal on the declare — for BOTH regs and arrays.
    // Reading it here for arrays too lands the contents on the type==2 `init`
    // pin with NO reset (reset_init is gated on !is_array below), i.e. pure
    // power-on init: a `mut`/`const` array has no clock, so there is nothing
    // for a reset to re-load. A reg array DOES restore its init through the
    // cell's whole-array `reset` pin (finalize_mems). Flatten an inline tuple
    // literal's constant leaves row-major into entries.
    std::function<bool(const Lnast_nid&)> flatten_lit = [&](const Lnast_nid& tnid) -> bool {
      for (auto ch = lnast_->get_first_child(tnid); !ch.is_invalid(); ch = lnast_->get_sibling_next(ch)) {
        const auto cht = lnast_->get_type(ch);
        if (Lnast_ntype::is_tuple_add(cht)) {
          if (!flatten_lit(ch)) {
            return false;
          }
        } else if (Lnast_ntype::is_const(cht)) {
          auto v = Dlop::from_pyrope(lnast_->get_name(ch));
          if (!v || !v->is_integer()) {
            error_here(
                "upass.tolg: memory '{}' initializer '{}' is not an "
                "integer constant",
                name,
                lnast_->get_name(ch));
            return false;
          }
          init_entries.emplace_back(v->and_op(*Dlop::get_mask_value(elem_mw)));
        } else if (!Lnast_ntype::is_ref(cht)) {  // a leading self-ref (tuple target) is skipped
          error_here(
              "upass.tolg: memory '{}' initializer must be a comptime "
              "constant or tuple literal",
              name);
          return false;
        }
      }
      return true;
    };
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
        auto v = whole_array_const(txt);
        if (v && v->is_string() && hlop::memory_image(v->to_string())) {
          if (is_array) {
            error_here("memory '{}': readmem initializer requires reg storage", name);
            return;
          }
          reg_init = v;  // startup command, no reset value or packed image
          break;
        }
        if (!v || !v->is_integer()) {
          error_here(
              "upass.tolg: memory '{}' initializer '{}' is not an "
              "integer constant",
              name,
              txt);
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
          error_here(
              "upass.tolg: memory '{}' initializer must be a comptime "
              "constant or tuple literal",
              name);
          return;
        }
        // Row-major flatten; nested literals must match the dims chain.
        if (!flatten_init_values(tit->second, dims, 0, name, elem_mw, init_entries)) {
          return;  // flatten_init_values reported
        }
        reg_init = pack_entries(init_entries, elem_mw);
        break;
      }
      if (Lnast_ntype::is_tuple_add(ct)) {  // slang's INLINE per-entry init literal
        if (!flatten_lit(c)) {
          return;  // flatten_lit reported
        }
        reg_init = pack_entries(init_entries, elem_mw);
        break;
      }
      error_here(
          "upass.tolg: memory '{}' initializer must be a comptime "
          "constant or tuple literal",
          name);
      return;
    }

    const int user_sites      = count_mem_write_sites(name_nid);
    // Same-cycle ordering: the `fwd` sink is a per-(read,write) MATRIX that
    // finalize_mems() builds once every port is minted and each read port's
    // program position is known (`rd_wr_before`). The `ordering` attr is read
    // there too, not here — a hand-written Pyrope declaration emits its
    // attr_set AFTER the declare, so pending_attrs_ is not populated yet (the
    // same reason the clock wiring is deferred). The value driven below is
    // provisional, and is the final one only for the two cases finalize_mems
    // leaves alone: a `mut`/`const` array and a legacy `fwd=` escape hatch.
    int64_t   legacy_fwd_mask = 0;
    bool      has_legacy_fwd  = false;
    if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
      // Deprecated numeric `fwd=`: an explicit matrix, taken verbatim (a
      // per-WRITE-port mask still reads correctly on a 1-read memory, which is
      // every historical user).
      if (auto fit = pit->second.find("fwd"); fit != pit->second.end()) {
        if (auto fv = Dlop::from_pyrope(fit->second); fv && fv->is_just_i64()) {
          legacy_fwd_mask = fv->to_just_i64();
          has_legacy_fwd  = true;
        }
      }
    }
    // A `mut`/`const` array (type=2) has no clock: tolg lowers it
    // writes-before-reads (a read after a write is a hard error), so every
    // write is visible to every read and the matrix is irrelevant — the comb
    // encoders read the post-write array unconditionally.
    int64_t fwd_mask = is_array ? 1 : (int64_t{1} << user_sites) - 1;
    if (has_legacy_fwd) {
      fwd_mask = legacy_fwd_mask;
    }

    auto mem = make_node(Ntype_op::Memory);
    // Stamp the declared RTL name on the Memory NODE (SSA suffix stripped),
    // mirroring the flop path (~L1634): hhds get_hier_name() otherwise falls
    // back to the positional `n<id>`, which forces pass/lec to pair memories
    // ANONYMOUSLY by shape + occurrence ordinal — two front-ends that
    // enumerate reads/memories in a different order then tie DIFFERENT
    // logical arrays (or read ports) to one shared symbol and falsely refute.
    // The reader's detupled per-field regs give unique names in both flows
    // (e.g. `msg_port_conf.umode`).
    {
      std::string mem_base{name};
      if (auto p = mem_base.find("___ssa_"); p != std::string::npos) {
        mem_base.resize(p);
      }
      if (!mem_base.empty()) {
        mem.set_name(std::string(canon_io_name(mem_base)));
      }
    }
    setup_sink_by_name(mem, "bits").connect_driver(create_const(*g_, *Dlop::create_integer(elem_mw)));
    setup_sink_by_name(mem, "size").connect_driver(create_const(*g_, *Dlop::create_integer(size)));
    setup_sink_by_name(mem, "type").connect_driver(create_const(*g_, *Dlop::create_integer(is_array ? 2 : 0)));
    setup_sink_by_name(mem, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(fwd_mask)));
    setup_sink_by_name(mem, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    if (reg_init) {
      setup_sink_by_name(mem, "initial").connect_driver(create_const(*g_, *reg_init));
    }
    // Clock wiring (posclk + clock_pin) for a clocked (non-array) memory is
    // deferred to finalize_mems: the slang reader emits the clock_pin/posclk
    // attr_set AFTER this declare in lnast order, so pending_attrs_ is not yet
    // populated here (unlike fwd, which the reader emits before the declare).

    Mem_info info;
    info.node            = mem;
    info.decl_nid        = lnast_->get_parent(name_nid);
    info.size            = size;
    info.dims            = std::move(dims);
    info.elem_mw         = elem_mw;
    info.elem_signed     = elem_signed;
    info.is_array        = is_array;
    // `pub` on a reg means a remote regref may attach reads/writes later —
    // suppress access diagnostics. Dormant today (prp2lnast restricts `pub`
    // to file scope), but the gate is mode-keyed so it activates with regref.
    info.is_pub          = std::string_view(lnast_->get_name(mode_nid)).find("pub") != std::string_view::npos;
    info.n_user_wr       = user_sites;
    info.legacy_fwd_mask = legacy_fwd_mask;
    info.has_legacy_fwd  = has_legacy_fwd;
    // `!init_entries.empty()` is not redundant with `reg_init`: a zero-entry
    // array leaves `reg_init` set (the broadcast loop simply never runs) with
    // nothing to reset. Whether a reset SIGNAL exists is decided in
    // finalize_mems (mem_reset_source): `reg arr:[N]u8:[reset_pin=rst] = 0`
    // names its own, and tree_declares_reset_reg deliberately does NOT mint the
    // implicit `reset` then.
    if (!is_array && reg_init && !init_entries.empty()) {
      info.reset_init = *reg_init;
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
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: tuple '{}' has an element that did not fold to a "
               "constant or wire — it has no hardware "
               "lowering (tuples must be fully resolved at compile time)",
               lnast_->get_name(dst));
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
        // Splice an operand tuple: merge its recorded fields in order
        // (positional appended, named keyed). Constprop already reported any
        // field overlap.
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
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: concatenated tuple '{}' has an operand that did "
               "not fold to a constant or wire — it has no "
               "hardware lowering (tuple `++`/`...` must be fully resolved at "
               "compile time)",
               lnast_->get_name(dst));
    }
    tuple_recs_[std::string(lnast_->get_name(dst))] = std::move(rec);
  }

  // The size*elem_mw-bit value bus a whole-array store contributes to `update`.
  // A runtime ref is the bus itself (leaf). A const scalar broadcasts to every
  // entry (each masked to the element, row-major); a comptime tuple literal
  // packs row-major; `nil`/`0sb?` is zero-filled. Returns an invalid Pin after
  // reporting (an unsupported value shape).
  [[nodiscard]] Pin mem_whole_value_pin(const Lnast_nid& rhs, std::string_view name, Mem_info& mi) {
    const auto rt           = lnast_->get_type(rhs);
    const bool is_tuple_lit = Lnast_ntype::is_ref(rt) && tuple_recs_.find(std::string(lnast_->get_name(rhs))) != tuple_recs_.end();
    if (!Lnast_ntype::is_const(rt) && !is_tuple_lit) {
      return leaf(rhs).pin;  // runtime whole-array bus
    }
    if (Lnast_ntype::is_const(rt)) {
      auto txt = lnast_->get_name(rhs);
      if (txt == "nil" || txt == "0sb?") {
        return create_const(*g_, *Dlop::create_integer(0));  // zero-filled
      }
      auto v = whole_array_const(txt);
      if (!v || !v->is_integer()) {
        error_here(
            "upass.tolg: whole-array value '{}' for memory '{}' is not "
            "supported — use an integer, a tuple literal or nil",
            txt,
            name);
        return {};
      }
      auto entry  = v->and_op(*Dlop::get_mask_value(mi.elem_mw));
      auto packed = Dlop::create_integer(0);
      for (int64_t i = 0; i < mi.size; ++i) {
        packed = packed->or_op(*entry->shl_op(*Dlop::create_integer(i * mi.elem_mw)));
      }
      return create_const(*g_, *packed);
    }
    auto tit = tuple_recs_.find(std::string(lnast_->get_name(rhs)));
    if (tit == tuple_recs_.end() || !tit->second.named.empty()) {
      error_here(
          "upass.tolg: whole-array value for memory '{}' must be a "
          "comptime tuple literal",
          name);
      return {};
    }
    std::vector<spool_ptr<Dlop>> entries;
    if (!flatten_init_values(tit->second, mi.dims, 0, name, mi.elem_mw, entries)) {
      return {};  // flatten_init_values reported
    }
    return create_const(*g_, *pack_entries(entries, mi.elem_mw));
  }

  // Delete the single existing edge to the memory cell's sink `pid` (if any),
  // then drive it with `d` when `d` is valid (invalid => leave it unconnected).
  void redrive_mem_sink(Mem_info& mi, int pid, const Pin& d) {
    if (auto sink = driven_sink_at(mi.node, static_cast<uint64_t>(pid)); !sink.is_invalid()) {
      sink.del_sink();  // mutates only after driven_sink_at's walk has ended
    }
    if (!d.is_invalid()) {
      mi.node.create_sink_pin(static_cast<hhds::Port_id>(pid)).connect_driver(d);
    }
  }

  // store(ref mem, rhs) — the whole-array form. For a mut/const array this
  // is its initializer: pack the recorded tuple consts into one wide const
  // (entry 0 in the low `bits`, row-major) on the `initial` sink. `= nil` means
  // zero-filled (cgen's default).
  // store(mem, <value>) — the whole-array `update` write (runtime bus, const
  // broadcast, or comptime tuple literal). The size*elem_mw bus (entry 0 in the
  // low `elem_mw`, row-major) drives the cell's `update` sink; a conditional
  // whole-write (`if(c) mem=<value>`) carries the branch path-condition into
  // `update_enable` (absent => always-on). MULTIPLE conditional whole-array
  // stores (a reset arm + a flush arm) accumulate into one
  // `update`/`update_enable` pair: the later-lowered store wins where its
  // enable holds (priority mux), and `update_enable` is the OR of every enable;
  // the if/else-if path conditions already encode source priority. The ladder
  // reset > per-port write > (update_enable? update : hold) is realized by
  // cgen/cgen_sim/lec. Against the per-entry writes the store keeps its
  // PROGRAM position (Mem_info::bulk_sites): finalize_mems turns the ladder
  // into program order, the last write of the cycle winning.
  void lower_mem_update_value(const Pin& v, std::string_view name, Mem_info& mi) {
    // Bulk writes have no ordinary write-port site. Capture their process
    // clock here too, before a later process changes the ordered attributes.
    if (!mi.is_array) {
      if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
        if (auto cit = pit->second.find("__store_clock_pin"); cit != pit->second.end()) {
          const auto cp       = resolve_attr_signal(cit->second);
          const auto pos      = pit->second.find("__store_posclk");
          const bool positive = pos == pit->second.end() || (pos->second != "false" && pos->second != "0");
          if (cp.is_invalid()) {
            error_here("upass.tolg: memory '{}' names unknown bulk-write clock '{}'", name, cit->second);
            return;
          }
          if (!mi.update_clock.is_invalid() && (mi.update_clock != cp || mi.update_posclk != positive)) {
            error_here("upass.tolg: memory '{}' bulk writes require one shared clock and edge", name);
            return;
          }
          mi.update_clock  = cp;
          mi.update_posclk = positive;
        }
      }
    }
    auto en = current_path_cond();  // invalid => unconditional
    mi.bulk_sites.push_back({.val = v, .en = en, .wr_before = mi.wr_next});
    if (!mi.has_update) {
      setup_sink_by_name(mi.node, "update").connect_driver(v);
      if (!en.is_invalid()) {
        setup_sink_by_name(mi.node, "update_enable").connect_driver(en);
      }
      mi.has_update = true;
      mi.update_val = v;
      mi.update_en  = en;
      // The cell itself forwards nothing on a whole-array memory: the clocked
      // bulk update is a next-state, and cgen emits `assign dout = data[addr]`
      // for every read port. Force fwd=0 so the cvc5 encoder reads a_cur,
      // matching cgen. The same-cycle writes a read must see under its
      // `ordering` are replayed in logic instead (finalize_mems ->
      // forward_mem_read).
      if (auto fwd_sink = driven_sink_at(mi.node, 5); !fwd_sink.is_invalid()) {  // fwd (pid 5)
        fwd_sink.del_sink();
      }
      setup_sink_by_name(mi.node, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
      return;
    }
    // A subsequent conditional whole-array write: later store wins where `en`,
    // else the previously-accumulated value. An unconditional later store
    // (`en` invalid) fully replaces the prior value and makes the bus
    // always-on.
    Pin merged_val;
    if (en.is_invalid()) {
      merged_val = v;
    } else {
      auto mux = make_node(Ntype_op::Mux);
      livehd::graph_util::setup_sink_pid(mux, 0).connect_driver(en);             // selector
      livehd::graph_util::setup_sink_pid(mux, 1).connect_driver(mi.update_val);  // false / else = previous value
      livehd::graph_util::setup_sink_pid(mux, 2).connect_driver(v);              // true / then = this store
      merged_val = mux.create_driver_pin(0);
      // The Memory sink is the declared-width storage boundary; the Mux is an
      // ordinary unbounded operation and must first preserve the widest arm.
      // Stamp the widest literal width, exactly as bind_result does. Constants
      // have no pin-width attribute, so sizing
      // from bits_of alone collapsed an 80-bit fill value to a one-bit Mux.
      set_ubits(merged_val, std::max({1, pin_mw_of(mi.update_val), pin_mw_of(v)}));
    }
    // Combined enable: always-on (invalid) if either contributor is always-on.
    Pin merged_en = (mi.update_en.is_invalid() || en.is_invalid()) ? Pin{} : or2(mi.update_en, en);
    redrive_mem_sink(mi, 12, merged_val);  // update (pid 12)
    redrive_mem_sink(mi, 13, merged_en);   // update_enable (pid 13); invalid =>
                                           // leave unconnected (always-on)
    mi.update_val = merged_val;
    mi.update_en  = merged_en;
  }

  void lower_mem_update_store(const Lnast_nid& rhs, std::string_view name, Mem_info& mi) {
    const auto value = mem_whole_value_pin(rhs, name, mi);
    if (!value.is_invalid()) {
      lower_mem_update_value(value, name, mi);
    }
  }

  void lower_mem_init_store(const Lnast_nid& rhs, std::string_view name, Mem_info& mi) {
    const auto rt = lnast_->get_type(rhs);
    // A registered (`reg`) memory has NO declaration initializer through this
    // path — its declared init rides lower_mem_declare. Every whole-array store
    // to it is therefore a CONDITIONAL bulk write (a reset arm, a flush arm, a
    // runtime refill, …): route it to the `update` bus regardless of whether
    // the value is a runtime bus, a const broadcast, or a comptime tuple
    // literal. Multiple such stores accumulate (priority mux in
    // lower_mem_update_store).
    if (!mi.is_array) {
      lower_mem_update_store(rhs, name, mi);
      return;
    }
    // A RUNTIME whole-array value (`arr = <bus>` where the rhs is a wire/leaf,
    // not a constant and not a recorded comptime tuple literal) drives the
    // cell's `update` bus: the whole array is (re)written each cycle
    // combinationally
    // (`mut`/`const` array), instead of minting per-entry ports.
    const bool is_tuple_lit = Lnast_ntype::is_ref(rt) && tuple_recs_.find(std::string(lnast_->get_name(rhs))) != tuple_recs_.end();
    if (!Lnast_ntype::is_const(rt) && !is_tuple_lit) {
      lower_mem_update_store(rhs, name, mi);
      return;
    }
    // ---- declaration initializer (comptime const / tuple literal) ----
    if (mi.init_wired) {
      error_here(
          "upass.tolg: array '{}' is re-initialized — only the "
          "declaration initializer is supported",
          name);
      return;
    }
    if (Lnast_ntype::is_const(rt)) {
      auto txt = lnast_->get_name(rhs);
      if (txt == "nil" || txt == "0sb?") {
        mi.init_wired = true;  // zero-filled default
        return;
      }
      // Scalar broadcast: every entry = value (masked to the element) — the
      // same treatment the reg declare-initializer path applies.
      auto v = whole_array_const(txt);
      if (!v || !v->is_integer()) {
        error_here(
            "upass.tolg: array '{}' initializer '{}' is not supported — "
            "use an integer, a tuple literal or nil",
            name,
            txt);
        return;
      }
      auto entry = v->and_op(*Dlop::get_mask_value(mi.elem_mw));
      auto init  = Dlop::create_integer(0);
      for (int64_t i = 0; i < mi.size; ++i) {
        init = init->or_op(*entry->shl_op(*Dlop::create_integer(i * mi.elem_mw)));
      }
      setup_sink_by_name(mi.node, "initial").connect_driver(create_const(*g_, *init));
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
    setup_sink_by_name(mi.node, "initial").connect_driver(create_const(*g_, *pack_entries(entries, mi.elem_mw)));
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
      error_here(
          "upass.tolg: '{}' initializer has {} entries where dimension "
          "{} holds {}",
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
          error_here(
              "upass.tolg: '{}' initializer entry {} must be a nested "
              "tuple literal (the memory has {} dimensions)",
              name,
              i,
              dims.size());
          return false;
        }
        auto tit = tuple_recs_.find(std::string(lnast_->get_name(e)));
        if (tit == tuple_recs_.end() || !tit->second.named.empty()) {
          error_here(
              "upass.tolg: '{}' initializer entry {} must be a comptime "
              "tuple literal",
              name,
              i);
          return false;
        }
        if (!flatten_init_values(tit->second, dims, level + 1, name, bits, out)) {
          return false;
        }
        continue;
      }
      if (!Lnast_ntype::is_const(lnast_->get_type(e))) {
        error_here(
            "upass.tolg: '{}' initializer entry {} is not a "
            "compile-time constant",
            name,
            i);
        return false;
      }
      auto v = Dlop::from_pyrope(lnast_->get_name(e));
      if (!v || !v->is_integer()) {
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
  // (08-memories.md RTL form). The cfg vocabulary is the cell pins VERBATIM:
  // addr/bits/clock_pin/din/enable/fwd/posclk/type/
  // wensize/size/rdport + init — no `latency`, type picks 0 async / 1 sync /
  // 2 array, rdport entries are strictly 0/1, dout comes back as a tuple
  // indexed by read-port order. Returns false when the call is not __memory.
  bool try_lower_memory_builtin(const Lnast_nid& nid, std::string_view callee_name) {
    if (callee_name != "__memory") {
      return false;
    }
    auto      dst      = lnast_->get_first_child(nid);
    auto      callee_n = lnast_->get_sibling_next(dst);
    auto      arg      = lnast_->get_sibling_next(callee_n);
    // Two spellings (08-memories.md RTL instantiation): the named-argument call
    // `__memory(addr=…, bits=…, …)` (every `__` cell call binds by name: the
    // fcall carries one `store(name, value)` child per argument) and the older
    // single config tuple `__memory(cfg)`.
    Tuple_rec named_cfg;
    bool      by_name = false;
    if (!arg.is_invalid() && Lnast_ntype::is_store(lnast_->get_type(arg))) {
      by_name = true;
      for (auto a = arg; !a.is_invalid(); a = lnast_->get_sibling_next(a)) {
        auto k = Lnast_ntype::is_store(lnast_->get_type(a)) ? lnast_->get_first_child(a) : Lnast_nid{};
        auto v = k.is_invalid() ? k : lnast_->get_sibling_next(k);
        if (v.is_invalid() || !lnast_->get_sibling_next(v).is_invalid()) {
          error_here("upass.tolg: every argument of `__memory` must be named with its pin name (`addr`, `bits`, ...)");
          return true;
        }
        named_cfg.named[std::string(lnast_->get_name(k))] = v;
      }
    } else if (arg.is_invalid() || !lnast_->get_sibling_next(arg).is_invalid()) {
      error_here("upass.tolg: __memory takes exactly one config tuple in '{}'", lnast_->get_top_module_name());
      return true;
    }
    auto rit = by_name ? tuple_recs_.end() : tuple_recs_.find(std::string(lnast_->get_name(arg)));
    if (!by_name && rit == tuple_recs_.end()) {
      error_here(
          "upass.tolg: __memory config '{}' must be a single tuple "
          "literal (build it as `mut cfg = (addr=…, "
          "bits=…, …)`)",
          lnast_->get_name(arg));
      return true;
    }
    const auto& cfg = by_name ? named_cfg : rit->second;

    // Guardrail: cell pins verbatim — diagnose the old doc vocabulary.
    static constexpr std::string_view known[]
        = {"addr", "bits", "clock_pin", "din", "enable", "fwd", "undef", "posclk", "type", "wensize", "size", "rdport", "initial"};
    for (const auto& [k, v] : cfg.named) {
      if (std::find(std::begin(known), std::end(known), k) == std::end(known)) {
        error_here(
            "upass.tolg: unknown __memory config field '{}' — the "
            "vocabulary is the Memory cell pins verbatim "
            "(addr/bits/clock_pin/din/enable/fwd/undef/posclk/type/"
            "wensize/size/rdport/initial; no `latency`, no `clock`)",
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
        error_here(
            "upass.tolg: __memory config field '{}' must be a "
            "compile-time constant",
            key);
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

    int64_t bits = 0, size = 0, type = 0, fwd = 0, undef = 0, wensize = 1, posclk = 1;
    if (!cfg_const("bits", 0, true, bits) || !cfg_const("size", 0, true, size) || !cfg_const("type", 0, false, type)
        || !cfg_const("fwd", 0, false, fwd) || !cfg_const("undef", 0, false, undef) || !cfg_const("wensize", 1, false, wensize)
        || !cfg_const("posclk", 1, false, posclk)) {
      return true;
    }
    if (bits <= 0 || size <= 0) {
      error_here(
          "upass.tolg: __memory needs positive bits/size (got bits={}, "
          "size={})",
          bits,
          size);
      return true;
    }
    if (type < 0 || type > 2) {
      error_here(
          "upass.tolg: __memory type must be 0 (async), 1 (sync) or 2 "
          "(array) — got {}",
          type);
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
            error_here(
                "upass.tolg: __memory config field '{}' must be a "
                "positional tuple",
                key);
            return false;
          }
          out = lit->second.elems;
          return true;
        }
      }
      out = {it->second};  // single scalar = one port
      return true;
    };

    std::vector<Lnast_nid> addrs, clocks, dins, ens, rdports;
    if (!cfg_list("addr", addrs) || !cfg_list("din", dins) || !cfg_list("clock_pin", clocks) || !cfg_list("enable", ens)
        || !cfg_list("rdport", rdports)) {
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
    if (clocks.size() > 1 && static_cast<int>(clocks.size()) != n_ports) {
      error_here("upass.tolg: __memory 'clock_pin' has {} entries but 'addr' has {} — pass one shared clock or one clock per port",
                 clocks.size(),
                 n_ports);
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
    // `fwd=true` means every write port forwards to every read port: the sink
    // is a per-(read,write) MATRIX (graph/cell.cpp), so the all-ones value
    // spans n_rd*n_wr bits, not n_wr. A value > 1 passes through as an explicit
    // matrix (the RTL escape hatch: __memory's vocabulary is the cell verbatim).
    const int n_rd_cfg            = n_ports - n_wr_cfg;
    const int fwd_bits            = n_rd_cfg * n_wr_cfg;
    // The all-ones expansion is keyed on the BOOLEAN literal, not on the value
    // 1. from_pyrope collapses `true` and `1` to the same integer, so testing
    // the value made the one-bit matrix `undef=1` (= read 0 / write 0 only)
    // unreachable: it silently became all-ones, and the lec X plane then masked
    // away EVERY read port's collision window — a genuinely wrong read port
    // proved equivalent. A numeric value is always an explicit matrix now.
    auto      cfg_is_true_literal = [&](std::string_view key) {
      auto it = cfg.named.find(std::string(key));
      return it != cfg.named.end() && lnast_->get_name(it->second) == "true";
    };
    const bool fwd_all   = cfg_is_true_literal("fwd");
    const bool undef_all = cfg_is_true_literal("undef");
    if ((fwd_all || undef_all) && fwd_bits > 62) {
      error_here(
          "upass.tolg: __memory has {} read x {} write ports — "
          "fwd=true/undef=true exceeds the 62-bit matrix this path "
          "builds; pass an explicit matrix instead",
          n_rd_cfg,
          n_wr_cfg);
      return true;
    }
    const int64_t fwd_mask   = fwd_all ? (int64_t{1} << fwd_bits) - 1 : fwd;
    // `undef` is the same shape (graph/cell.cpp pid 15) and takes the same
    // 0/true/explicit-matrix spellings. It is mutually exclusive with `fwd`
    // per (read,write) pair: forwarded data is defined by construction.
    const int64_t undef_mask = undef_all ? (int64_t{1} << fwd_bits) - 1 : undef;
    if ((fwd_mask & undef_mask) != 0) {
      error_here(
          "upass.tolg: __memory has fwd and undef both set for the same "
          "(read,write) pair (fwd={:#x}, undef={:#x}) — a forwarded "
          "read returns the new data, so it cannot also be undefined",
          fwd_mask,
          undef_mask);
      return true;
    }

    auto mem = make_node(Ntype_op::Memory);
    // Stamp the USER BINDING on the Memory node (same rationale as the
    // array-declare site: a null name degrades pass/lec memory pairing to
    // anonymous shape+ordinal). Calls lower through a compiler temporary:
    //
    //   fcall(%res_0, __memory, cfg)
    //   store(res, %res_0)
    //
    // so naming the cell directly from `dst` leaks `%res_0` into the state
    // correspondence key. Recover the adjacent source binding exactly as the
    // ordinary Sub path does; fall back to dst only when the result is consumed
    // directly by an expression.
    {
      std::string mem_base{lnast_->get_name(dst)};
      if (auto bound = lhs_var_of_temp_dst(nid, mem_base); !bound.empty()) {
        mem_base = std::move(bound);
      }
      if (auto p = mem_base.find("___ssa_"); p != std::string::npos) {
        mem_base.resize(p);
      }
      if (!mem_base.empty()) {
        mem.set_name(std::string(canon_io_name(mem_base)));
      }
    }
    setup_sink_by_name(mem, "bits").connect_driver(create_const(*g_, *Dlop::create_integer(bits)));
    setup_sink_by_name(mem, "size").connect_driver(create_const(*g_, *Dlop::create_integer(size)));
    setup_sink_by_name(mem, "type").connect_driver(create_const(*g_, *Dlop::create_integer(type)));
    setup_sink_by_name(mem, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(fwd_mask)));
    if (undef_mask != 0) {
      setup_sink_by_name(mem, "undef").connect_driver(create_const(*g_, *Dlop::create_integer(undef_mask)));
    }
    setup_sink_by_name(mem, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(wensize)));
    if (type != 2) {
      setup_sink_by_name(mem, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(posclk)));
      if (clocks.size() == 1) {
        setup_sink_by_name(mem, "clock_pin").connect_driver(leaf(clocks.front()).pin);
      } else if (clocks.empty() && !clock_name_.empty()) {
        setup_sink_by_name(mem, "clock_pin").connect_driver(clock_pin());
      } else if (clocks.empty()) {
        warn_at(Lnast_nid{}, {"no-clock", "time"}, "__memory has no clock to bind in '{}'", lnast_->get_top_module_name());
      }
      // A per-port list is connected below at base+2 once the port ordering is
      // known. Materializing every sink (rather than one shared pid 2) lets
      // cgen select the multiclock wrapper and retain each read/write clock.
    }
    if (auto it = cfg.named.find("initial"); it != cfg.named.end()) {
      spool_ptr<Dlop> init;
      if (Lnast_ntype::is_const(lnast_->get_type(it->second))) {
        init = Dlop::from_pyrope(lnast_->get_name(it->second));
      } else if (auto lit = tuple_recs_.find(std::string(lnast_->get_name(it->second))); lit != tuple_recs_.end()) {
        const std::vector<int64_t>   flat_dims{size};  // __memory is always flat
        std::vector<spool_ptr<Dlop>> entries;
        if (flatten_init_values(lit->second, flat_dims, 0, "__memory initial", static_cast<int32_t>(bits), entries)) {
          init = pack_entries(entries, static_cast<int32_t>(bits));
        }
      }
      if (!init) {
        error_here(
            "upass.tolg: __memory 'initial' must be a comptime constant or "
            "tuple literal");
        return true;
      }
      setup_sink_by_name(mem, "initial").connect_driver(create_const(*g_, *init));
    }

    int n_wr = 0;
    for (int i = 0; i < n_ports; ++i) {
      if (!Lnast_ntype::is_const(lnast_->get_type(rdports[i]))) {
        error_here(
            "upass.tolg: __memory 'rdport' entry {} must be a comptime "
            "0/1 constant",
            i);
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
      livehd::graph_util::setup_sink_pid(mem, static_cast<hhds::Port_id>(base + 0)).connect_driver(leaf(addrs[i]).pin);
      livehd::graph_util::setup_sink_pid(mem, static_cast<hhds::Port_id>(base + 10))
          .connect_driver(create_const(*g_, *Dlop::create_integer(is_rd ? 1 : 0)));
      if (type != 2 && clocks.size() > 1) {
        livehd::graph_util::setup_sink_pid(mem, static_cast<hhds::Port_id>(base + 2))
            .connect_driver(leaf(clocks[static_cast<size_t>(i)]).pin);
      }
      Pin en = i < static_cast<int>(ens.size()) ? leaf(ens[i]).pin : en_const(true);
      livehd::graph_util::setup_sink_pid(mem, static_cast<hhds::Port_id>(base + 4)).connect_driver(en);
      if (!is_rd) {
        if (i >= static_cast<int>(dins.size())) {
          error_here("upass.tolg: __memory write port {} has no 'din' entry", i);
          return true;
        }
        livehd::graph_util::setup_sink_pid(mem, static_cast<hhds::Port_id>(base + 3)).connect_driver(leaf(dins[i]).pin);
      }
    }

    mem_results_[std::string(lnast_->get_name(dst))] = Mem_result{mem, n_wr, n_ports - n_wr, static_cast<int32_t>(bits)};
    return true;
  }

  // fcall(ref dst, ref __mux, store(s, c), store(p1, a), store(p2, b), ...) — a
  // basic gate is an ordinary call whose arguments are named with the cell's
  // LGraph pin names (06b-instantiation.md "Basic gates"); comptime operands
  // fold in constprop, runtime ones lower here to the cell. Only the
  // multiplexer is lowered: every other gate on runtime values is still spelled
  // with the operator it stands for. Returns false when the call is not `__mux`.
  bool try_lower_gate_builtin(const Lnast_nid& nid, std::string_view callee_name) {
    if (callee_name != "__mux") {
      return false;
    }
    auto    dst      = lnast_->get_first_child(nid);
    auto    callee_n = lnast_->get_sibling_next(dst);
    auto    mux      = make_node(Ntype_op::Mux);
    int32_t mw       = 1;
    bool    sel      = false;
    for (auto arg = lnast_->get_sibling_next(callee_n); !arg.is_invalid(); arg = lnast_->get_sibling_next(arg)) {
      if (!Lnast_ntype::is_store(lnast_->get_type(arg))) {
        error_here("upass.tolg: every argument of `__mux` must be named with its pin name (`s`, `p1`, `p2`, ...)");
        return true;
      }
      auto pin_n = lnast_->get_first_child(arg);
      auto val_n = lnast_->get_sibling_next(pin_n);
      if (pin_n.is_invalid() || val_n.is_invalid()) {
        error_here("upass.tolg: malformed `__mux` argument");
        return true;
      }
      const auto pin_name = lnast_->get_name(pin_n);
      if (!Ntype::is_sink_name(Ntype_op::Mux, pin_name)) {
        error_here("upass.tolg: unknown argument `{}` in call to `__mux`: not a pin of the cell", pin_name);
        return true;
      }
      const auto v = leaf(val_n);
      setup_sink_by_name(mux, pin_name).connect_driver(v.pin);
      if (pin_name == "s") {
        sel = true;
      } else {
        mw = std::max(mw, v.mw);
      }
    }
    if (!sel) {
      error_here("upass.tolg: `__mux` needs its selector pin `s`");
      return true;
    }
    bind_result(lnast_->get_name(dst), mux.create_driver_pin(0), mw);
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
      error_here(
          "upass.tolg: whole-array assignment to memory '{}' is not "
          "supported — write one entry at a time",
          lhs_name);
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
          "upass.tolg: array '{}' is written after being read — "
          "same-cycle order is not preserved for "
          "mut/const arrays; reorder the accesses or use a `reg` memory",
          lhs_name);
      return;
    }
    const auto           base   = mi.wr_next * kMemPortStride;
    // A write-masked partial write (lower_set_mask over a masked old-value
    // temp of this memory): its din carries only the written bits.
    const Masked_pw_val* masked = nullptr;
    Pin                  masked_bits;  // the entry-wide mask of the bits it writes
    Pin                  rmw_din;      // set when the partial write falls back to a read-modify-write
    if (chunk < 0 && Lnast_ntype::is_ref(lnast_->get_type(val))) {
      if (auto mit = masked_pw_vals_.find(lnast_->get_name(val));
          mit != masked_pw_vals_.end() && mit->second.base.mem == lhs_name) {
        masked = &mit->second;
      }
    }
    if (masked != nullptr) {
      if (masked->cmask) {
        masked_bits = create_const(*g_, *masked->cmask->and_op(*Dlop::get_mask_value(mi.elem_mw)));
      } else {
        auto gm = make_node(Ntype_op::Get_mask);
        livehd::graph_util::connect_mask_operands(gm, masked->mask, 0, mi.elem_mw);
        masked_bits = gm.create_driver_pin(0);
        set_ubits(masked_bits, mi.elem_mw);
      }
      // The store's index is lowered apart from the read's (a computed index
      // is spelled twice); a different entry is a copy between entries, not a
      // partial write: mint the old-value read the masked form skipped, at its
      // own program position, and write the merged entry whole.
      if (!same_value(addr, masked->base.addr)) {
        const auto old = mint_mem_read(mi, masked->base.addr, masked->base.wr_before, masked->base.bulk_before);
        auto       gm  = make_node(Ntype_op::Get_mask);
        livehd::graph_util::connect_mask_operands(gm, old, 0, mi.elem_mw);
        auto old_u = gm.create_driver_pin(0);
        set_ubits(old_u, mi.elem_mw);
        rmw_din = lower_dynamic_mask_rmw(Val{old_u, mi.elem_mw}, masked_bits, leaf(val).pin);
        masked  = nullptr;
      }
    }
    if (chunk < 0 && masked == nullptr) {
      mi.plain_wr_ports.emplace_back(mi.wr_next);
    }
    if (!mi.is_array) {
      if (auto pit = pending_attrs_.find(std::string(lhs_name)); pit != pending_attrs_.end()) {
        if (auto cit = pit->second.find("__store_clock_pin"); cit != pit->second.end()) {
          const auto cp = resolve_attr_signal(cit->second);
          if (cp.is_invalid()) {
            error_here("upass.tolg: memory '{}' names unknown store clock '{}'", lhs_name, cit->second);
            return;
          }
          livehd::graph_util::setup_sink_pid(mi.node, static_cast<hhds::Port_id>(base + 2)).connect_driver(cp);
          const auto pos      = pit->second.find("__store_posclk");
          const bool positive = pos == pit->second.end() || (pos->second != "false" && pos->second != "0");
          if (mi.has_store_clock && positive != mi.first_store_posclk && !mi.mixed_store_edges) {
            mi.mixed_store_edges = true;
            warn_at(lhs,
                    {"mixed-memory-clock-edges", "unsupported"},
                    "memory '{}' mixes clock edges: write port {} differs from write port 0; formal checking requires per-port "
                    "edge support",
                    lhs_name,
                    mi.wr_next);
          }
          if (!mi.has_store_clock) {
            mi.first_store_posclk = positive;
          }
          mi.has_store_clock = true;
        }
      }
    }
    // A merged old-value chain stored back to the entry it read (the same
    // address) is a partial write; into any other entry it is a user copy.
    if (Lnast_ntype::is_ref(lnast_->get_type(val))) {
      if (auto rit = mem_rmw_reads_.find(lnast_->get_name(val));
          rit != mem_rmw_reads_.end() && rit->second.merged && rit->second.mem == lhs_name
          && same_value(addr, mi.rd_addr[static_cast<size_t>(rit->second.rd_port)])) {
        mi.rd_is_rmw[static_cast<size_t>(rit->second.rd_port)] = true;
      }
    }
    ++mi.wr_next;
    const auto din     = rmw_din.is_invalid() ? leaf(val).pin : rmw_din;
    const auto path_en = current_path_cond();
    mi.wr_sites.push_back({.addr = addr, .din = din, .en = path_en, .chunk = chunk, .bitmask = {}, .cmask = std::nullopt});
    if (masked != nullptr) {
      // The written bits of the entry: a constant mask stays a constant (it
      // sizes the lanes), a runtime one is cut to the entry width.
      auto& site   = mi.wr_sites.back();
      site.bitmask = masked_bits;
      if (masked->cmask) {
        site.cmask = *masked->cmask->and_op(*Dlop::get_mask_value(mi.elem_mw));
      }
      mi.has_masked_wr = true;
    }
    livehd::graph_util::setup_sink_pid(mi.node, static_cast<hhds::Port_id>(base + 0)).connect_driver(addr);  // addr
    livehd::graph_util::setup_sink_pid(mi.node, static_cast<hhds::Port_id>(base + 3)).connect_driver(din);   // din
    auto en = path_en.is_invalid() ? en_const(true) : path_en;
    if (chunk >= 0) {
      en = shl1_by(en, chunk);  // per-chunk write enable: bit `chunk` = path_cond
    }
    livehd::graph_util::setup_sink_pid(mi.node, static_cast<hhds::Port_id>(base + 4)).connect_driver(en);  // enable
    livehd::graph_util::setup_sink_pid(mi.node, static_cast<hhds::Port_id>(base + 10))
        .connect_driver(create_const(*g_, *Dlop::create_integer(0)));  // rdport = 0 (write)
  }

  // One lane of an array held as a packed bus `packed` (the `view` shape): a
  // const index is range-checked here, a runtime one gets the bounds assert.
  // `what` names the array in a diagnostic.
  // The lane view of an `a:[N]T` io entry (a port or a Sub instance output).
  // A multi-dimensional `a:[N][M]T` port is N*M lanes of T, row-major (its
  // io entry's elem_bits is one packed row of M).
  static Array_scalar_view array_view_of(const Lnast_io_entry& e) {
    Array_scalar_view view{
        .size        = e.array_size,
        .dims        = {e.array_size},
        .elem_mw     = e.elem_bits,
        .elem_signed = e.elem_signed,
    };
    for (const auto d : e.inner_dims) {
      view.size    *= d;
      view.elem_mw /= static_cast<int32_t>(d);
      view.dims.push_back(d);
    }
    return view;
  }

  void lower_array_lane_read(const Lnast_nid& nid, std::string_view dst_name, const Val& packed, const Lnast_nid& idx,
                             const Array_scalar_view& view, std::string_view what) {
    auto iv = leaf(idx);
    if (Lnast_ntype::is_const(lnast_->get_type(idx))) {
      auto ci = Dlop::from_pyrope(lnast_->get_name(idx));
      if (!ci || !ci->is_just_i64() || ci->to_just_i64() < 0 || ci->to_just_i64() >= view.size) {
        error_at(nid,
                 {"array-index-out-of-range", "type"},
                 "Pyrope array index {} is outside [0, {}) for '{}'",
                 lnast_->get_name(idx),
                 view.size,
                 what);
        return;
      }
      read_array_lane(dst_name, packed, ci->to_just_i64(), iv, view);
      return;
    }
    read_array_lane(dst_name, packed, std::nullopt, iv, view);
    lower_array_index_assert(iv, view.size, nid);
  }

  // The lane of a MULTI-dimensional view (`a[i][j]` of `a:[N][M]T`) an index
  // chain picks, row-major: folded when every index is a constant, else a
  // runtime Val. Each index is checked against its OWN dimension -- a constant
  // here, a runtime one by an index assert -- so a too-large inner index never
  // aliases the next row. Every dimension must be indexed (one lane, never a
  // packed row), like a multi-dimensional memory.
  struct Lane_pick {
    bool                   ok = false;
    std::optional<int64_t> lane;
    Val                    dyn;
  };
  Lane_pick pick_array_lane(const Lnast_nid& nid, const std::vector<Lnast_nid>& idxs, const Array_scalar_view& view,
                            std::string_view what) {
    if (idxs.size() != view.dims.size()) {
      error_here("upass.tolg: array '{}' has {} dimension(s) but the access supplies {} index(es)",
                 what,
                 view.dims.size(),
                 idxs.size());
      return {};
    }
    Lane_pick pick{.ok = true, .lane = int64_t{0}, .dyn = {}};
    for (size_t k = 0; k < idxs.size(); ++k) {
      const int64_t d = view.dims[k];
      Val           iv;
      if (Lnast_ntype::is_const(lnast_->get_type(idxs[k]))) {
        auto ci = Dlop::from_pyrope(lnast_->get_name(idxs[k]));
        if (!ci || !ci->is_just_i64() || ci->to_just_i64() < 0 || ci->to_just_i64() >= d) {
          error_at(nid,
                   {"array-index-out-of-range", "type"},
                   "Pyrope array index {} is outside [0, {}) for '{}'",
                   lnast_->get_name(idxs[k]),
                   d,
                   what);
          return {};
        }
        if (pick.lane) {
          pick.lane = *pick.lane * d + ci->to_just_i64();
          continue;
        }
        iv = Val{create_const(*g_, *Dlop::create_integer(ci->to_just_i64())), mw_of_val(ci->to_just_i64())};
      } else {
        iv = leaf(idxs[k]);
        lower_array_index_assert(iv, d, nid);
      }
      Val acc = pick.lane ? Val{create_const(*g_, *Dlop::create_integer(*pick.lane)), mw_of_val(*pick.lane)} : pick.dyn;
      pick.lane.reset();
      if (d != 1) {
        auto mul = make_node(Ntype_op::Mult);
        setup_sink_by_name(mul, "as").connect_driver(acc.pin);
        setup_sink_by_name(mul, "as").connect_driver(create_const(*g_, *Dlop::create_integer(d)));
        acc = Val{mul.create_driver_pin(0), acc.mw + mw_of_val(d)};
        set_ubits(acc.pin, acc.mw);
      }
      auto add = make_node(Ntype_op::Sum);
      setup_sink_by_name(add, "as").connect_driver(acc.pin);
      setup_sink_by_name(add, "as").connect_driver(iv.pin);
      pick.dyn = Val{add.create_driver_pin(0), std::max(acc.mw, iv.mw) + 1};
      set_ubits(pick.dyn.pin, pick.dyn.mw);
    }
    return pick;
  }

  // Write `value` into lane `lane` (or the runtime lane `dyn`) of the packed
  // bus `base`, rebinding `name` to the result.
  void write_array_lane(std::string_view name, const Array_scalar_view& view, const Val& base, const Val& value,
                        std::optional<int64_t> lane, const Val& dyn) {
    if (lane) {
      const int64_t off  = *lane * view.elem_mw;
      auto          mask = Dlop::get_mask_value(static_cast<int>(off + view.elem_mw - 1), static_cast<int>(off));
      auto          sm   = make_node(Ntype_op::Set_mask);
      livehd::graph_util::connect_mask_operands(sm,
                                                base.pin,
                                                livehd::graph_util::mask_window(*mask).first,
                                                livehd::graph_util::mask_window(*mask).second,
                                                value.pin);
      auto out = sm.create_driver_pin(0);
      set_ubits(out, base.mw);
      record(name, out, base.mw);
      return;
    }
    auto mult = make_node(Ntype_op::Mult);
    setup_sink_by_name(mult, "as").connect_driver(dyn.pin);
    setup_sink_by_name(mult, "as").connect_driver(create_const(*g_, *Dlop::create_integer(view.elem_mw)));
    auto offset = mult.create_driver_pin(0);
    set_ubits(offset, std::max<int32_t>(dyn.mw + std::bit_width(static_cast<uint32_t>(view.elem_mw)), 1));

    auto maskn = make_node(Ntype_op::SHL);
    setup_sink_by_name(maskn, "a").connect_driver(create_const(*g_, *Dlop::get_mask_value(view.elem_mw)));
    setup_sink_by_name(maskn, "b").connect_driver(offset);
    auto mask = maskn.create_driver_pin(0);
    set_ubits(mask, base.mw);

    // The value must enter the lane as its elem_mw-bit TWO'S-COMPLEMENT
    // pattern. Shifting the raw (narrower, signed) value zero-extended it:
    // MEASURED on the rolled matched filter, every negative s8 product
    // written into an s12 lane through a runtime index came out +256 (the
    // comptime-index store goes through Set_mask, which extends correctly).
    // Sext to the lane width (wrap semantics for a wider value, exactly what
    // `wrap` would do), then take the lane-wide unsigned pattern.
    Pin lane_value = value.pin;
    if (view.elem_signed) {
      auto sx = make_node(Ntype_op::Sext);
      setup_sink_by_name(sx, "a").connect_driver(value.pin);
      setup_sink_by_name(sx, "b").connect_driver(create_const(*g_, *Dlop::create_integer(view.elem_mw)));
      auto sout = sx.create_driver_pin(0);
      set_bits(sout, view.elem_mw);
      set_sign(sout);
      auto gm = make_node(Ntype_op::Get_mask);
      livehd::graph_util::connect_mask_operands(gm, sout, 0, view.elem_mw);
      lane_value = gm.create_driver_pin(0);
      set_ubits(lane_value, view.elem_mw);
    }
    auto shifted = make_node(Ntype_op::SHL);
    setup_sink_by_name(shifted, "a").connect_driver(lane_value);
    setup_sink_by_name(shifted, "b").connect_driver(offset);
    auto placed = shifted.create_driver_pin(0);
    set_ubits(placed, base.mw);

    auto out = lower_dynamic_mask_rmw(base, mask, placed);
    record(name, out, base.mw);
  }

  // Read lane `lane` (or the runtime lane `dyn`) of the packed bus `packed`.
  void read_array_lane(std::string_view dst_name, const Val& packed, std::optional<int64_t> lane, const Val& dyn,
                       const Array_scalar_view& view) {
    Pin offset;
    if (lane) {
      offset = create_const(*g_, *Dlop::create_integer(*lane * view.elem_mw));
    } else {
      auto mult = make_node(Ntype_op::Mult);
      setup_sink_by_name(mult, "as").connect_driver(dyn.pin);
      setup_sink_by_name(mult, "as").connect_driver(create_const(*g_, *Dlop::create_integer(view.elem_mw)));
      offset = mult.create_driver_pin(0);
      set_ubits(offset, std::max<int32_t>(dyn.mw + std::bit_width(static_cast<uint32_t>(view.elem_mw)), 1));
    }

    auto sra = make_node(Ntype_op::SRA);
    setup_sink_by_name(sra, "a").connect_driver(packed.pin);
    setup_sink_by_name(sra, "b").connect_driver(offset);
    auto shifted = sra.create_driver_pin(0);
    set_ubits(shifted, packed.mw);

    auto gm = make_node(Ntype_op::Get_mask);
    livehd::graph_util::connect_mask_operands(gm, shifted, 0, view.elem_mw);
    auto out = gm.create_driver_pin(0);
    if (view.elem_signed) {
      // A Get_mask is UNSIGNED by construction: stamping the sign on its
      // driver does not survive cprop's constant-slice fold (the read of a
      // comptime index folds straight onto the packed lane and came back
      // zero-extended — MEASURED `mut a:[2]s8; a[0] = -3; y:s12 = a[0]` read
      // 253, while a runtime index, a `reg` array and a same-width consumer
      // were all fine). Ride a same-width Sext, the abc read-back idiom: its
      // `b` is the kept bit COUNT, the result is a signed elem_mw-bit value.
      set_ubits(out, view.elem_mw);  // the lane itself: an unsigned elem_mw-bit pattern (an unsized pin emits as ONE bit)
      auto sx = make_node(Ntype_op::Sext);
      setup_sink_by_name(sx, "a").connect_driver(out);
      setup_sink_by_name(sx, "b").connect_driver(create_const(*g_, *Dlop::create_integer(view.elem_mw)));
      auto sout = sx.create_driver_pin(0);
      set_bits(sout, view.elem_mw);
      set_sign(sout);
      record(dst_name, sout, view.elem_mw);
    } else {
      bind_result(dst_name, out, view.elem_mw);
    }
  }

  // tuple_get(ref dst, ref mem, idx) — one read port per site, always
  // enabled; dst binds to the port's dout driver (pid n_user_wr + r).
  void lower_tuple_get(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    auto src = dst.is_invalid() ? dst : lnast_->get_sibling_next(dst);
    auto idx = src.is_invalid() ? src : lnast_->get_sibling_next(src);
    if (idx.is_invalid()) {
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: tuple/field read of '{}' has no index — it cannot "
               "be lowered to a netlist",
               src.is_invalid() ? std::string_view{"?"} : lnast_->get_name(src));
    }
    const std::string src_name{lnast_->get_name(src)};
    if (auto ait = array_scalar_views_.find(src_name); ait != array_scalar_views_.end()) {
      if (ait->second.dims.size() > 1 || !lnast_->get_sibling_next(idx).is_invalid()) {
        std::vector<Lnast_nid> idxs;
        for (auto ix = idx; !ix.is_invalid(); ix = lnast_->get_sibling_next(ix)) {
          idxs.push_back(ix);
        }
        const auto view = ait->second;  // leaf() below may grow array_scalar_views_
        const auto pick = pick_array_lane(nid, idxs, view, src_name);
        if (!pick.ok) {
          return;
        }
        check_nil_array_output_lane(nid, src_name, pick.lane);
        read_array_lane(lnast_->get_name(dst), leaf(src), pick.lane, pick.dyn, view);
        return;
      }
      check_nil_array_output_read(nid, src_name, idx);
      lower_array_lane_read(nid, lnast_->get_name(dst), leaf(src), idx, ait->second, src_name);
      return;
    }
    auto it = mem_map_.find(src_name);
    if (it == mem_map_.end()) {
      // A field of a `stage[N]` over several values: its own stage flops.
      if (auto sft = staged_fields_.find(src_name); sft != staged_fields_.end() && Lnast_ntype::is_const(lnast_->get_type(idx))
                                                    && lnast_->get_sibling_next(idx).is_invalid()) {
        std::string_view fld = lnast_->get_name(idx);
        if (fld.size() >= 2 && (fld.front() == '\'' || fld.front() == '"') && fld.back() == fld.front()) {
          fld = fld.substr(1, fld.size() - 2);
        }
        if (auto fit = sft->second.find(canon_io_name(fld)); fit != sft->second.end()) {
          const auto v = fit->second;  // record may grow the maps
          record(lnast_->get_name(dst), v.pin, v.mw);
          return;
        }
      }
      // Multi-output Sub result: tuple_get(dst, result, 'port') binds that
      // output port's driver pin with the io-entry width/sign contract.
      if (auto srt = sub_results_.find(std::string(lnast_->get_name(src))); srt != sub_results_.end()) {
        // `x[k]` on a SINGLE array output: the call binds `x` to that output
        // (06-functions.md "Binding return values"), so the index reads one
        // lane of its packed bus -- unless it names the output (`x.v`).
        // A const index must be a NUMBER to pick a lane: `x.w` names an
        // output (and is diagnosed as one below).
        const auto idx_txt = lnast_->get_name(idx);
        if (const auto& outs = srt->second.outputs;
            outs.size() == 1 && outs.front().array_size > 0
            && (!Lnast_ntype::is_const(lnast_->get_type(idx))
                || (!idx_txt.empty() && std::isdigit(static_cast<unsigned char>(idx_txt.front())) != 0))) {
          const auto&            ae   = outs.front();
          const auto             view = array_view_of(ae);
          std::vector<Lnast_nid> idxs;
          for (auto ix = idx; !ix.is_invalid(); ix = lnast_->get_sibling_next(ix)) {
            idxs.push_back(ix);
          }
          if (view.dims.size() > 1 || idxs.size() > 1) {
            const auto pick = pick_array_lane(nid, idxs, view, src_name);
            if (pick.ok) {
              auto pv = srt->second.sub.create_driver_pin(std::string(canon_io_name(ae.name)));
              set_ubits(pv, io_mw(ae));
              read_array_lane(lnast_->get_name(dst), Val{pv, io_mw(ae)}, pick.lane, pick.dyn, view);
            }
            return;
          }
          auto pv = srt->second.sub.create_driver_pin(std::string(canon_io_name(ae.name)));
          set_ubits(pv, io_mw(ae));
          lower_array_lane_read(nid, lnast_->get_name(dst), Val{pv, io_mw(ae)}, idx, view, src_name);
          return;
        }
        // The read may carry MULTIPLE indices: a tuple-typed output port
        // flattens to a dotted leaf name (`rsp.sum`), and the dot-form read
        // `inst.rsp.sum` arrives as a flat all-const index chain
        // (tuple_get(dst, inst, 'rsp', 'sum')). Join the chain with '.' and
        // match the FULL dotted output name (no suffix/partial matching —
        // `inst.sum` stays a no-output error when the port is `rsp.sum`).
        // Each index is a string CONST: a bracket read `inst["port"]` reaches
        // here with the surrounding quotes still on the name (`'rdata'`),
        // while the dot form (`inst.port`) is bare — unquote each component
        // before matching (same trap as the quoted mod-import callee).
        std::string joined;
        for (auto ix = idx; !ix.is_invalid(); ix = lnast_->get_sibling_next(ix)) {
          if (!Lnast_ntype::is_const(lnast_->get_type(ix))) {
            error_here(
                "upass.tolg: a multi-output instance result is read by "
                "a single output-port name");
            return;
          }
          std::string_view comp = lnast_->get_name(ix);
          if (comp.size() >= 2 && ((comp.front() == '\'' && comp.back() == '\'') || (comp.front() == '"' && comp.back() == '"'))) {
            comp = comp.substr(1, comp.size() - 2);
          }
          if (!joined.empty()) {
            joined += '.';
          }
          joined += comp;
        }
        // BOTH sides go through canon_io_name: the io entry may carry slang's
        // `` `p.q` `` marker, and so may the READ (`inst.`p.q``, which arrives
        // as a const index with the backticks still on). Canonicalizing only
        // the declaration turned a legal quoted-port read into a hard
        // "instance result has no output named" error.
        std::string_view      pname = canon_io_name(joined);
        const Lnast_io_entry* oe    = nullptr;
        for (const auto& e : srt->second.outputs) {
          if (canon_io_name(e.name) == pname) {
            oe = &e;
            break;
          }
        }
        if (oe == nullptr) {
          error_here("upass.tolg: instance result has no output named '{}'", pname);
          return;
        }
        if (oe->array_size > 0) {
          // `m.v` of an array output: the packed bus, with the lane view so a
          // following `[k]` reads one lane.
          array_scalar_views_[std::string(lnast_->get_name(dst))] = array_view_of(*oe);
        }
        const auto v = sub_output_val(srt->second.sub, *oe);
        record(lnast_->get_name(dst), v.pin, v.mw);
        return;
      }
      // 1a-mem — res[N] on a __memory result: bind the N-th read port's dout.
      if (auto mrt = mem_results_.find(std::string(lnast_->get_name(src))); mrt != mem_results_.end()) {
        const auto& mr = mrt->second;
        if (!Lnast_ntype::is_const(lnast_->get_type(idx)) || !lnast_->get_sibling_next(idx).is_invalid()) {
          error_here(
              "upass.tolg: a __memory result is indexed by a single "
              "comptime read-port number");
          return;
        }
        auto          v = Dlop::from_pyrope(lnast_->get_name(idx));
        const int64_t k = (v && v->is_just_i64()) ? v->to_just_i64() : -1;
        if (k < 0 || k >= mr.n_rd) {
          error_here(
              "upass.tolg: __memory result index {} out of range — the "
              "config has {} read port(s)",
              lnast_->get_name(idx),
              mr.n_rd);
          return;
        }
        auto dout = mr.node.create_driver_pin(static_cast<hhds::Port_id>(mr.n_wr + k));
        set_ubits(dout, mr.bits);  // __memory data is raw bits — unsigned
        record(lnast_->get_name(dst), dout, mr.bits);
        return;
      }
      // A single-field read (`src.field`) of a name that is not yet a known
      // memory / Sub result. It may be a forward reference to a call result
      // lowered later in the body (`c = tmp.add` reads tmp.add before
      // `tmp = add_sub(…)` runs). Defer the bind to end-of-pass; re-resolved
      // with tget_final_, a still-unresolved one warns.
      if (!tget_final_ && Lnast_ntype::is_const(lnast_->get_type(idx)) && lnast_->get_sibling_next(idx).is_invalid()) {
        pending_tgets_.emplace_back(nid);
        return;
      }
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: field/index read of '{}' could not be resolved — "
               "'{}' is not a memory, a multi-output "
               "instance result, or a resolved value (often an unassigned "
               "value/nil, or an unsupported runtime tuple "
               "index)",
               lnast_->get_name(src),
               lnast_->get_name(src));
    }
    auto&                  mi = it->second;
    // Gather the full index chain (tuple_get(dst, mem, i, j, …) is FLAT).
    std::vector<Lnast_nid> idxs;
    for (auto c = idx; !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      idxs.emplace_back(c);
    }
    auto addr = flatten_mem_addr(mi, idxs, lnast_->get_name(src));
    if (addr.is_invalid()) {
      return;  // flatten_mem_addr reported
    }
    // The old-value read of a partial write the memory takes as a write-MASKED
    // port (index_mem_write_sites): no read port. The temp is the all-zero
    // entry, so the set_mask over it builds just the written bits, and the
    // store that follows turns it into the masked write (or, should its
    // address not be this one, mints this read after all: lower_mem_store). A
    // `mut` array, a legacy `fwd=` matrix, or a memory the Verilog reader
    // already splits into its own chunk lanes keeps the read-modify-write.
    if (masked_pw_gets_.contains(nid) && !mi.is_array && !mi.has_legacy_fwd && mem_wensize_attr(src_name) <= 1) {
      const std::string dst_name{lnast_->get_name(dst)};
      masked_pw_bases_.insert_or_assign(dst_name,
                                        Masked_pw_base{.mem         = src_name,
                                                       .addr        = addr,
                                                       .wr_before   = mi.wr_next,
                                                       .bulk_before = static_cast<int>(mi.bulk_sites.size())});
      auto zero = create_const(*g_, *Dlop::create_integer(0));
      record(dst_name, zero, mi.elem_mw);
      return;
    }
    if (Lnast::is_tmp(lnast_->get_name(dst))) {
      mem_rmw_reads_.insert_or_assign(std::string(lnast_->get_name(dst)),
                                      Mem_rmw_read{.mem = src_name, .rd_port = mi.rd_next, .merged = false});
    }
    // Program-order position: the writes minted so far are exactly those that
    // textually precede this read, i.e. the ones it may forward from.
    const auto dout = mint_mem_read(mi, addr, mi.wr_next, static_cast<int>(mi.bulk_sites.size()));
    record(lnast_->get_name(dst), dout, mi.elem_mw);
  }

  // One per-element read port of `mi` at `addr`, placed in program order after
  // `wr_before` write ports and `bulk_before` whole-array stores. Returns its
  // dout (the element, signed when the element is).
  Pin mint_mem_read(Mem_info& mi, const Pin& addr, int wr_before, int bulk_before) {
    const int  slot = mi.n_user_wr + mi.rd_next;
    const auto base = slot * kMemPortStride;
    mi.rd_wr_before.emplace_back(wr_before);
    mi.rd_bulk_before.emplace_back(bulk_before);
    mi.rd_is_rmw.emplace_back(false);
    mi.rd_addr.emplace_back(addr);
    livehd::graph_util::setup_sink_pid(mi.node, static_cast<hhds::Port_id>(base + 0)).connect_driver(addr);  // addr
    livehd::graph_util::setup_sink_pid(mi.node, static_cast<hhds::Port_id>(base + 4)).connect_driver(en_const(true));
    livehd::graph_util::setup_sink_pid(mi.node, static_cast<hhds::Port_id>(base + 10))
        .connect_driver(create_const(*g_, *Dlop::create_integer(1)));  // rdport = 1 (read)
    auto dout = mi.node.create_driver_pin(static_cast<hhds::Port_id>(mi.n_user_wr + mi.rd_next));
    ++mi.rd_next;
    if (mi.elem_signed) {
      set_bits(dout, mi.elem_mw);
      set_sign(dout);
    } else {
      set_ubits(dout, mi.elem_mw);
    }
    return dout;
  }

  // The reader's per-chunk write-enable count for memory `name` (its `wensize`
  // pending attr), 1 when it has none.
  [[nodiscard]] int64_t mem_wensize_attr(std::string_view name) const {
    if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
      if (auto wit = pit->second.find("wensize"); wit != pit->second.end()) {
        if (auto wv = Dlop::from_pyrope(wit->second); wv && wv->is_just_i64() && wv->to_just_i64() > 1) {
          return wv->to_just_i64();
        }
      }
    }
    return 1;
  }

  // One read's same-cycle replay (forward_mem_read, forward_read_all). Every
  // replay is BUILT before any is wired: a recorded write or bulk-store pin
  // (Mem_info::wr_sites, bulk_sites) may itself be another replayed read, and
  // moving that read's consumers only once every chain exists carries those
  // chains along with the rest, whatever order the reads were built in.
  struct Mem_read_replay {
    Pin              from;  // the committed read value
    Pin              to;    // the replayed value
    hhds::Node_class own;   // the replay's own tap on `from`, which stays on it (invalid => none)
    hhds::Node_class drop;  // a read_all site's passthrough, deleted once empty (invalid => none)
  };

  // Entry `bits` of bulk store `bs` when its bus is a constant that fills every
  // entry alike (a clear), whatever the address; nullopt otherwise. Checked in
  // O(log size) by doubling the entry up to the bus width.
  [[nodiscard]] std::optional<Dlop> uniform_bulk_entry(const Mem_info& mi, const Mem_info::Bulk_site& bs) const {
    if (!bs.val.is_const()) {
      return std::nullopt;
    }
    const int   bits   = mi.elem_mw;
    const auto  packed = static_cast<int>(mi.size * bits);
    const auto& all    = livehd::graph_util::const_of(bs.val);
    auto        entry  = all.and_op(*Dlop::get_mask_value(bits));
    auto        fill   = entry;
    for (int k = bits; k < packed; k *= 2) {
      fill = fill->or_op(*fill->shl_op(*Dlop::create_integer(k)));
    }
    const auto packed_mask = Dlop::get_mask_value(packed);
    if (!fill->and_op(*packed_mask)->eq_op(*all.and_op(*packed_mask))->is_known_true()) {
      return std::nullopt;
    }
    return *entry;
  }

  // Build the replay of the first write ports and whole-array stores of the
  // cycle (Mem_info::rd_replay), in program order, over read port `r`'s
  // committed dout: a bulk store takes its own entry at the read address, a
  // write port hitting the address takes its din (per lane for a chunked
  // write), and the last event wins. Two kinds of read need it:
  //   * a partial write's old-value read (Mem_info::rd_is_rmw), in every
  //     ordering: it sees the entry as the writes before it left it, so the
  //     partial writes of one entry merge (its own write comes after it, so the
  //     replay has no cycle);
  //   * a user read of a memory with a whole-array store, under ordering
  //     "program" (the events before it) or "fwd" (every event of the cycle):
  //     the cell forwards nothing once an `update` bus is wired.
  // A write during reset never lands, so `not_rst` gates every event and a
  // read during reset sees the committed contents. `uniform` caches
  // uniform_bulk_entry per bulk store as a pin.
  void forward_mem_read(const Mem_info& mi, int r, std::vector<std::optional<Pin>>& uniform, std::vector<Mem_read_replay>& out) {
    const auto [wr_upto, bulk_upto] = mi.rd_replay[static_cast<size_t>(r)];
    if (wr_upto == 0 && bulk_upto == 0) {
      return;
    }
    const auto dout   = mi.node.create_driver_pin(static_cast<hhds::Port_id>(mi.n_user_wr + r));  // minted by the read
    const auto fanout = dout.out_edges();
    if (fanout.begin() == fanout.end()) {
      return;
    }
    const int32_t bits  = mi.elem_mw;
    const auto    raddr = mi.rd_addr[static_cast<size_t>(r)];
    // Bits [lo, hi) of `v` as an unsigned value.
    auto          field = [&](const Pin& v, int lo, int hi) {
      auto gm = make_node(Ntype_op::Get_mask);
      livehd::graph_util::connect_mask_operands(gm, v, lo, hi);
      auto res = gm.create_driver_pin(0);
      set_ubits(res, hi - lo);
      return res;
    };
    // Every value in the replay is the entry's unsigned `bits`-wide pattern.
    auto word = [&](const Pin& v) { return field(v, 0, bits); };
    auto pick = [&](const Pin& sel, const Pin& keep, const Pin& take) {
      if (sel.is_invalid()) {
        return take;  // always
      }
      auto mux = make_node(Ntype_op::Mux);
      livehd::graph_util::setup_sink_pid(mux, 0).connect_driver(sel);
      livehd::graph_util::setup_sink_pid(mux, 1).connect_driver(keep);
      livehd::graph_util::setup_sink_pid(mux, 2).connect_driver(take);
      auto res = mux.create_driver_pin(0);
      set_ubits(res, bits);
      return res;
    };
    // Entry `raddr` of bulk store `b`'s bus, which holds entry i at bits
    // [i*bits, (i+1)*bits).
    auto bulk_entry = [&](int b) -> Pin {
      const auto& bs = mi.bulk_sites[static_cast<size_t>(b)];
      auto&       u  = uniform[static_cast<size_t>(b)];
      if (!u.has_value()) {
        u = Pin{};
        if (auto entry = uniform_bulk_entry(mi, bs)) {
          // No set_ubits: constants are interned (one pin per value), so
          // stamping this one would re-size every other use of the value --
          // e.g. the zero base of a write-masked partial write, which cprop
          // then packed as a 16-bit lane into an 8-bit Concat window.
          u = create_const(*g_, *entry);
        }
      }
      if (!u->is_invalid()) {
        return *u;
      }
      const auto packed_bits = static_cast<int>(mi.size * bits);
      auto       mult        = make_node(Ntype_op::Mult);
      setup_sink_by_name(mult, "as").connect_driver(raddr);
      setup_sink_by_name(mult, "as").connect_driver(create_const(*g_, *Dlop::create_integer(bits)));
      auto offset = mult.create_driver_pin(0);
      set_ubits(offset, std::max<int32_t>(pin_mw_of(raddr) + std::bit_width(static_cast<uint32_t>(bits)), 1));
      auto sra = make_node(Ntype_op::SRA);
      setup_sink_by_name(sra, "a").connect_driver(field(bs.val, 0, packed_bits));
      setup_sink_by_name(sra, "b").connect_driver(offset);
      auto shifted = sra.create_driver_pin(0);
      set_ubits(shifted, packed_bits);
      return word(shifted);
    };
    const Pin     tap              = word(dout);
    Pin           v                = tap;
    const int64_t lane_bits        = bits % mi.wensize == 0 ? bits / mi.wensize : bits;
    int           b                = 0;
    auto          replay_bulk_upto = [&](int wr_pos) {
      for (; b < bulk_upto && mi.bulk_sites[static_cast<size_t>(b)].wr_before <= wr_pos; ++b) {
        v = pick(and2(mi.bulk_sites[static_cast<size_t>(b)].en, mi.not_rst), v, bulk_entry(b));
      }
    };
    for (int w = 0; w < wr_upto; ++w) {
      replay_bulk_upto(w);
      const auto& s  = mi.wr_sites[static_cast<size_t>(w)];
      auto        eq = make_node(Ntype_op::EQ);
      livehd::graph_util::setup_sink_pid(eq, 0).connect_driver(s.addr);
      livehd::graph_util::setup_sink_pid(eq, 0).connect_driver(raddr);
      auto same = eq.create_driver_pin(0);
      set_ubits(same, 1);
      Pin take;
      if (!s.bitmask.is_invalid()) {
        // A write-masked partial write stores only its mask's bits.
        take = lower_dynamic_mask_rmw(Val{v, bits}, word(s.bitmask), word(s.din));
      } else if (s.chunk < 0 || lane_bits == bits) {
        take = word(s.din);
      } else {
        // A chunked write stores lane `chunk` of its din and keeps the rest.
        const int lo = static_cast<int>(s.chunk * lane_bits);
        const int hi = static_cast<int>(lo + lane_bits);
        auto      sm = make_node(Ntype_op::Set_mask);
        livehd::graph_util::connect_mask_operands(sm, v, lo, hi, field(s.din, lo, hi));
        take = sm.create_driver_pin(0);
        set_ubits(take, bits);
      }
      v = pick(and2(and2(s.en, mi.not_rst), same), v, take);
    }
    replay_bulk_upto(wr_upto);
    if (mi.elem_signed) {
      auto sx = make_node(Ntype_op::Sext);
      setup_sink_by_name(sx, "a").connect_driver(v);
      setup_sink_by_name(sx, "b").connect_driver(create_const(*g_, *Dlop::create_integer(bits)));
      v = sx.create_driver_pin(0);
      set_bits(v, bits);
      set_sign(v);
    }
    out.push_back({.from = dout, .to = v, .own = tap.get_master_node(), .drop = {}});
  }

  // The whole-array counterpart of forward_mem_read, for read_all site `s`
  // (Mem_info::Read_all_site): replay the first write ports and bulk stores of
  // the cycle (Mem_info::ra_replay), in program order, over the packed
  // committed contents -- a bulk store replaces the bus, a write port splices
  // its din (one lane of it for a chunked write) into entry `addr` -- and move
  // the site's consumers onto the result. With nothing to replay they move to
  // the shared read_all pin. Either way the site's passthrough goes.
  void forward_read_all(const Mem_info& mi, int s, std::vector<Mem_read_replay>& out) {
    const auto& site                = mi.read_all_sites[static_cast<size_t>(s)];
    const auto [wr_upto, bulk_upto] = mi.ra_replay[static_cast<size_t>(s)];
    const auto fanout               = site.pin.out_edges();
    if (fanout.begin() == fanout.end() || (wr_upto == 0 && bulk_upto == 0)) {
      out.push_back({.from = site.pin, .to = mi.read_all_pin, .own = {}, .drop = site.pin.get_master_node()});
      return;
    }
    const int32_t bits   = mi.elem_mw;
    const int32_t packed = static_cast<int32_t>(mi.size * bits);
    auto          field  = [&](const Pin& v, int lo, int hi) {
      auto gm = make_node(Ntype_op::Get_mask);
      livehd::graph_util::connect_mask_operands(gm, v, lo, hi);
      auto res = gm.create_driver_pin(0);
      set_ubits(res, hi - lo);
      return res;
    };
    auto pick = [&](const Pin& sel, const Pin& keep, const Pin& take) {
      if (sel.is_invalid()) {
        return take;  // always
      }
      auto mux = make_node(Ntype_op::Mux);
      livehd::graph_util::setup_sink_pid(mux, 0).connect_driver(sel);
      livehd::graph_util::setup_sink_pid(mux, 1).connect_driver(keep);
      livehd::graph_util::setup_sink_pid(mux, 2).connect_driver(take);
      auto res = mux.create_driver_pin(0);
      set_ubits(res, packed);
      return res;
    };
    auto shl = [&](const Pin& v, const Pin& amt) {
      auto sh = make_node(Ntype_op::SHL);
      setup_sink_by_name(sh, "a").connect_driver(v);
      setup_sink_by_name(sh, "b").connect_driver(amt);
      auto res = sh.create_driver_pin(0);
      set_ubits(res, packed);
      return field(res, 0, packed);
    };
    Pin           v                = mi.read_all_pin;  // the committed contents
    const int64_t lane_bits        = bits % mi.wensize == 0 ? bits / mi.wensize : bits;
    int           b                = 0;
    auto          replay_bulk_upto = [&](int wr_pos) {
      for (; b < bulk_upto && mi.bulk_sites[static_cast<size_t>(b)].wr_before <= wr_pos; ++b) {
        const auto& bs = mi.bulk_sites[static_cast<size_t>(b)];
        v              = pick(and2(bs.en, mi.not_rst), v, field(bs.val, 0, packed));
      }
    };
    for (int w = 0; w < wr_upto; ++w) {
      replay_bulk_upto(w);
      const auto& ws = mi.wr_sites[static_cast<size_t>(w)];
      int         lo = 0;
      int         hi = bits;
      if (ws.chunk >= 0 && lane_bits != bits) {
        lo = static_cast<int>(ws.chunk * lane_bits);
        hi = static_cast<int>(lo + lane_bits);
      }
      // entry `addr` starts at bit addr*bits of the packed bus
      auto mult = make_node(Ntype_op::Mult);
      setup_sink_by_name(mult, "as").connect_driver(ws.addr);
      setup_sink_by_name(mult, "as").connect_driver(create_const(*g_, *Dlop::create_integer(bits)));
      auto offset = mult.create_driver_pin(0);
      set_ubits(offset, std::max<int32_t>(pin_mw_of(ws.addr) + std::bit_width(static_cast<uint32_t>(bits)), 1));
      auto mask = shl(
          ws.bitmask.is_invalid() ? create_const(*g_, livehd::graph_util::mask_window_const(lo, hi)) : field(ws.bitmask, 0, bits),
          offset);
      auto value = shl(field(ws.din, 0, bits), offset);
      auto take  = lower_dynamic_mask_rmw(Val{v, packed}, mask, value);
      v          = pick(and2(ws.en, mi.not_rst), v, take);
    }
    replay_bulk_upto(wr_upto);
    out.push_back({.from = site.pin, .to = v, .own = {}, .drop = site.pin.get_master_node()});
  }

  // Replays every read finalize_mems marked (forward_mem_read,
  // forward_read_all), once the outputs, wires and deferred reads that may
  // consume a read are wired: first every chain is built, then each read's
  // consumers -- other chains' taps included -- move onto its replayed value.
  // Program order keeps that acyclic: a chain replays only events before its
  // read, and those events read only values before them. ("fwd" replays the
  // whole cycle, so a read that feeds a write of its own memory is a loop,
  // exactly what the cell's forwarding would make of it.)
  void replay_mem_reads() {
    std::vector<Mem_read_replay> replays;
    for (const auto& name : mem_order_) {
      auto it = mem_map_.find(name);
      if (it == mem_map_.end()) {
        continue;
      }
      const auto&                     mi = it->second;
      std::vector<std::optional<Pin>> uniform(mi.bulk_sites.size());
      for (int r = 0; r < static_cast<int>(mi.rd_replay.size()); ++r) {
        forward_mem_read(mi, r, uniform, replays);
      }
      for (int s = 0; s < static_cast<int>(mi.ra_replay.size()); ++s) {
        forward_read_all(mi, s, replays);
      }
    }
    for (const auto& rp : replays) {
      const auto                         fanout = rp.from.out_edges();
      const livehd::graph_util::Edge_vec readers(fanout.begin(), fanout.end());
      for (const auto& e : readers) {
        if (!rp.own.is_invalid() && e.sink.get_master_node() == rp.own) {
          continue;
        }
        e.sink.del_sink(rp.from);
        e.sink.connect_driver(rp.to);
      }
      if (!rp.drop.is_invalid()) {
        rp.drop.del_node();
      }
    }
  }

  // `cond ? v : 0` for a multi-bit `v` (a lane-enable vector). and2 is a
  // 1-bit AND, which would keep only lane 0.
  [[nodiscard]] Pin gate_lanes(const Pin& cond, const Pin& v, int32_t bits) {
    if (cond.is_invalid()) {
      return v;
    }
    auto mux = make_node(Ntype_op::Mux);
    livehd::graph_util::setup_sink_pid(mux, 0).connect_driver(cond);
    livehd::graph_util::setup_sink_pid(mux, 1).connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    livehd::graph_util::setup_sink_pid(mux, 2).connect_driver(v);
    auto out = mux.create_driver_pin(0);
    set_ubits(out, std::max(1, bits));
    return out;
  }

  // Write-MASKED partial writes (Wr_site::bitmask): the memory's lane width is
  // the largest one every constant mask is made of (a runtime mask needs
  // single-bit lanes), `wensize` is the lane count, and every write port's
  // enable becomes a lane vector: a masked write enables its mask's lanes, a
  // whole-entry write all of them, each gated by its path condition. The ports
  // keep program order, so the cell's last-port-wins lanes merge several
  // partial writes of one entry exactly like the read-modify-write they
  // replace (docs: a partial write updates the entry as the earlier writes of
  // the cycle left it), with no old-value read port.
  void lane_mask_mem_writes(Mem_info& mi) {
    if (!mi.has_masked_wr || mi.elem_mw <= 0) {
      return;
    }
    const int32_t bits   = mi.elem_mw;
    int32_t       lane_w = bits;
    for (const auto& s : mi.wr_sites) {
      if (s.bitmask.is_invalid()) {
        continue;
      }
      if (!s.cmask) {
        lane_w = 1;
        break;
      }
      for (int32_t i = 1; i < bits && lane_w > 1; ++i) {
        if (s.cmask->bit_test(i) != s.cmask->bit_test(i - 1)) {
          lane_w = std::gcd(lane_w, i);
        }
      }
    }
    const int32_t wensize = bits / lane_w;
    mi.wensize            = wensize;
    if (auto ws_sink = driven_sink_at(mi.node, 8); !ws_sink.is_invalid()) {
      ws_sink.del_sink();
    }
    setup_sink_by_name(mi.node, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(wensize)));
    const auto all_lanes = create_const(*g_, *Dlop::get_mask_value(wensize));
    for (int w = 0; w < mi.n_user_wr && w < static_cast<int>(mi.wr_sites.size()); ++w) {
      const auto& s = mi.wr_sites[static_cast<size_t>(w)];
      Pin         lanes;
      if (s.bitmask.is_invalid()) {
        lanes = all_lanes;
      } else if (!s.cmask) {
        lanes = s.bitmask;  // single-bit lanes: the mask IS the lane vector
      } else {
        std::string lane_bits(static_cast<size_t>(wensize), '0');  // MSB first
        for (int32_t l = 0; l < wensize; ++l) {
          if (s.cmask->bit_test(l * lane_w)) {
            lane_bits[static_cast<size_t>(wensize - 1 - l)] = '1';
          }
        }
        lanes = lane_bits.find('1') == std::string::npos ? create_const(*g_, *Dlop::create_integer(0))
                                                         : create_const(*g_, *Dlop::from_binary(lane_bits, true));
      }
      const auto pid = static_cast<uint64_t>(w * kMemPortStride + 4);
      if (auto en_sink = driven_sink_at(mi.node, pid); !en_sink.is_invalid()) {
        en_sink.del_sink();
      }
      mi.node.create_sink_pin(static_cast<hhds::Port_id>(pid)).connect_driver(gate_lanes(s.en, lanes, wensize));
    }
  }

  // Ruling 45: a whole-array store and a per-entry write of one cycle resolve
  // in program order, the later one winning. The cell's ladder lets every
  // per-entry write override the `update` bus, so a write port followed by a
  // bulk store must not land where that store's enable holds: gate it with
  // the negated enables of every later bulk store (an unconditional one kills
  // it). A bulk store BEFORE the port needs nothing -- the ladder already
  // lets the port win.
  void gate_mem_writes_by_later_bulk(Mem_info& mi) {
    Pin  kill;
    bool always_killed = false;
    int  b             = static_cast<int>(mi.bulk_sites.size()) - 1;
    for (int w = mi.n_user_wr - 1; w >= 0; --w) {
      for (; b >= 0 && mi.bulk_sites[static_cast<size_t>(b)].wr_before > w; --b) {
        const auto& bs = mi.bulk_sites[static_cast<size_t>(b)];
        if (bs.en.is_invalid()) {
          always_killed = true;
        } else {
          kill = or2(kill, bs.en);
        }
      }
      if (!always_killed && kill.is_invalid()) {
        continue;  // no bulk store after this port
      }
      const auto pid     = static_cast<uint64_t>(w * kMemPortStride + 4);
      auto       en_sink = driven_sink_at(mi.node, pid);
      if (en_sink.is_invalid()) {
        continue;
      }
      const auto cur = en_sink.get_driver_pin();
      en_sink.del_sink();
      // The enable may be a multi-bit chunk mask, so select it whole rather
      // than AND it with a 1-bit condition.
      auto off = create_const(*g_, *Dlop::create_integer(0));
      Pin  gated;
      if (always_killed) {
        gated = off;
      } else {
        auto mux = make_node(Ntype_op::Mux);
        livehd::graph_util::setup_sink_pid(mux, 0).connect_driver(kill);
        livehd::graph_util::setup_sink_pid(mux, 1).connect_driver(cur);
        livehd::graph_util::setup_sink_pid(mux, 2).connect_driver(off);
        gated = mux.create_driver_pin(0);
        set_ubits(gated, std::max(1, pin_mw_of(cur)));
      }
      mi.node.create_sink_pin(static_cast<hhds::Port_id>(pid)).connect_driver(gated);
    }
  }

  void finalize_mems() {
    for (const auto& name : mem_order_) {
      auto it = mem_map_.find(name);
      if (it == mem_map_.end()) {
        continue;
      }
      auto& mi   = it->second;
      // Runs after the walk: anchor this memory's diagnostics (a clock-ambiguous
      // implicit clock, a missing clock_pin input) at its declaration.
      cur_srcid_ = hhds::SourceId_invalid;
      if (const auto id = lnast_->get_srcid(mi.decl_nid); id != hhds::SourceId_invalid) {
        cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
      }
      if (mi.wr_next != mi.n_user_wr) {
        error_here(
            "upass.tolg: internal — memory '{}' lowered {} write sites "
            "but the pre-scan counted {}",
            name,
            mi.wr_next,
            mi.n_user_wr);
      }
      // Same-cycle ordering: build the per-(read,write) `fwd` matrix now that
      // every port is minted. Bit (r*n_wr + w) => read port r forwards write
      // port w. A write suppressed by reset (finalize_mems gates every user
      // enable with !reset) never lands, so a read during reset sees the
      // committed contents.
      //   "program" (default): row r = the writes that textually precede read r
      //                        (a PREFIX, recorded in rd_wr_before)
      //   "fwd":               every read forwards every user write
      //   "old":               nothing forwards; a colliding read is the
      //                        DEFINED committed value (what the Verilog
      //                        readers need — a nonblocking write is invisible
      //                        to a same-timestep read)
      //   "none":              nothing forwards and a colliding read is
      //                        UNDEFINED — the parallel `undef` matrix below,
      //                        which is the only thing that distinguishes it
      //                        from "old" (a zero `fwd` row cannot)
      // A type=2 array keeps its legacy single-bit value: it has no clock and
      // is lowered writes-before-reads, so every encoder reads the post-write
      // array unconditionally and the matrix is unused.
      // (A whole-array cell has already forced fwd=0 and leaves the matrix
      // alone: its same-cycle reads are replayed in logic, forward_mem_read.)
      auto ordering = Mem_info::Mem_order::program;  // Pyrope default
      if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
        if (auto oit = pit->second.find("ordering"); oit != pit->second.end()) {
          std::string_view ov{oit->second};
          // Attr values arrive as Pyrope source text: a string literal keeps
          // its quotes.
          while (ov.size() >= 2 && (ov.front() == '"' || ov.front() == '\'') && ov.back() == ov.front()) {
            ov = ov.substr(1, ov.size() - 2);
          }
          if (ov == "program") {
            ordering = Mem_info::Mem_order::program;
          } else if (ov == "fwd") {
            ordering = Mem_info::Mem_order::fwd;
          } else if (ov == "old") {
            ordering = Mem_info::Mem_order::old;
          } else if (ov == "none") {
            ordering = Mem_info::Mem_order::none;
          } else {
            error_here(
                "upass.tolg: memory '{}' has ordering=\"{}\" — the legal "
                "values are \"program\" (default), \"fwd\", \"old\" and "
                "\"none\"",
                name,
                ov);
          }
        }
      }
      // Per-ELEMENT read ports only. A read_all is not one of them (it has no
      // port block and forwards nothing), so a memory read ONLY as a whole has
      // n_rd == 0 -- and that case must STILL reach the redrive below, or the
      // cell keeps the declare-time PROVISIONAL all-ones `fwd`
      // (`(1<<user_sites)-1`), which is a same-cycle collision matrix nobody
      // asked for. cgen then refuses the design outright ("read WHOLE
      // (read_all) and also carries a non-zero same-cycle collision matrix"),
      // which is what stopped the lgyosys LEC backend on lhdtrack's
      // br_tracker_linked_list_ctrl. With no rows the packed matrix is exactly
      // zero, which is also what the inline reg-array emission implements.
      const int n_rd = static_cast<int>(mi.rd_wr_before.size());
      lane_mask_mem_writes(mi);
      if (!mi.bulk_sites.empty() && mi.n_user_wr > 0) {
        gate_mem_writes_by_later_bulk(mi);
      }
      if (!mi.is_array && !mi.has_legacy_fwd && !mi.has_update && mi.n_user_wr > 0) {
        // Row-major bit string, MSB first: bit (r*n_wr + w) sits at index
        // n_bits-1-(r*n_wr+w). Built as TEXT so a wide matrix stays exact — a
        // whole-array expansion easily reaches 9rd x 8wr = 72 bits, and every
        // consumer reads it with Dlop::bit_test (arbitrary precision).
        const int   n_bits = n_rd * mi.n_user_wr;
        std::string bits(static_cast<size_t>(n_bits), '0');
        // ordering="none": the SAME layout, but the bits mean "undefined on a
        // collision" rather than "forward". A zero `fwd` row alone cannot say
        // whether the read is defined-OLD or undefined, so "none" needs its own
        // matrix (graph/cell.cpp pid 15).
        std::string ubits(static_cast<size_t>(n_bits), '0');
        for (int r = 0; r < n_rd; ++r) {
          int fwd_upto   = 0;
          int undef_upto = 0;
          // A partial write's old-value read reads the committed entry: the
          // writes it must see are replayed by forward_mem_read.
          switch (mi.rd_is_rmw[static_cast<size_t>(r)] ? Mem_info::Mem_order::old : ordering) {
            case Mem_info::Mem_order::program: fwd_upto = mi.rd_wr_before[static_cast<size_t>(r)]; break;
            case Mem_info::Mem_order::fwd    : fwd_upto = mi.n_user_wr; break;
            case Mem_info::Mem_order::old    : fwd_upto = 0; break;
            case Mem_info::Mem_order::none   : undef_upto = mi.n_user_wr; break;
          }
          for (int w = 0; w < fwd_upto; ++w) {
            bits[static_cast<size_t>(n_bits - 1 - (r * mi.n_user_wr + w))] = '1';
          }
          for (int w = 0; w < undef_upto; ++w) {
            ubits[static_cast<size_t>(n_bits - 1 - (r * mi.n_user_wr + w))] = '1';
          }
        }
        // Same encoding for both: compact int64 while it fits (so the emitted
        // Verilog stays a plain decimal), exact `0ub…` text beyond that.
        auto pack = [&](const std::string& b) -> spool_ptr<Dlop> {
          // Width is carried independently by the memory's read/write port
          // counts; a zero mask therefore needs no leading-zero payload.  In
          // particular, ordering="old" on a large restored memory can make
          // this matrix several million zero bits wide.  Keeping those zeros
          // in a Dlop is unnecessary and exceeds Dlop's current word-count
          // representation even though the value itself is simply zero.
          if (b.find('1') == std::string::npos) {
            return Dlop::create_integer(0);
          }
          // A Dlop counts its 64-bit words in an int16_t. Past that the matrix
          // cannot be built at all, and emitting the memory without it would
          // silently drop the forwarding.
          if (1 + (static_cast<int64_t>(n_bits) / 64) > std::numeric_limits<int16_t>::max()) {
            error_here(
                "upass.tolg: memory '{}' needs a {}x{} same-cycle collision matrix ({} bits), wider than a constant can "
                "hold; use ordering=\"old\" (which needs none) or fewer read/write ports",
                name,
                n_rd,
                mi.n_user_wr,
                n_bits);
            return {};
          }
          if (n_bits <= 62) {
            int64_t v = 0;
            for (int i = 0; i < n_bits; ++i) {
              if (b[static_cast<size_t>(n_bits - 1 - i)] == '1') {
                v |= int64_t{1} << i;
              }
            }
            return Dlop::create_integer(v);
          }
          // `b` is already the payload of an unsigned binary literal.  Going
          // through from_pyrope("0ub" + b) makes Dlop provision storage once
          // for the generic parser and then again in init_from_binary().  For
          // very large memories (XiangShan has forwarding matrices above
          // 512K bits), that provisional word count overflows Dlop's int16_t
          // size field before the binary parser releases it, corrupting the
          // pool free.  Parse the known binary payload directly: this is both
          // the exact intended representation and avoids the redundant wide
          // allocation altogether.
          return Dlop::from_binary(b, true);
        };
        auto redrive = [&](int pid, std::string_view pin_name, const spool_ptr<Dlop>& matrix) {
          if (!matrix) {
            return;
          }
          if (auto sink = driven_sink_at(mi.node, static_cast<uint64_t>(pid)); !sink.is_invalid()) {
            sink.del_sink();
          }
          setup_sink_by_name(mi.node, pin_name).connect_driver(create_const(*g_, *matrix));
        };
        redrive(5, "fwd", pack(bits));  // fwd (pid 5)
        if (ubits.find('1') != std::string::npos) {
          redrive(15, "undef", pack(ubits));  // undef (pid 15)
        }
      }
      // Chunked masked writes (mem[addr][chunk]<=data) set a wensize > 1 via a
      // pending attr from the reader; the declare provisionally drove
      // wensize=1, so re-drive it here (after every write port is in place).
      // wensize is the single config pin at port_id 8 (see graph/cell.cpp
      // Memory pin names).
      if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
        if (auto wit = pit->second.find("wensize"); wit != pit->second.end()) {
          if (auto wv = Dlop::from_pyrope(wit->second); wv && wv->is_just_i64() && wv->to_just_i64() > 1) {
            const int64_t wensize = wv->to_just_i64();
            if (auto ws_sink = driven_sink_at(mi.node, 8); !ws_sink.is_invalid()) {
              ws_sink.del_sink();
            }
            setup_sink_by_name(mi.node, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(wensize)));
            // Every WHOLE-word write port on this memory (`mem[i] <= v`, or a
            // reader read-modify-write for a slice the chunk model could not
            // express) still drives a ONE-BIT enable, and the wensize wrapper
            // reads bit k as "write chunk k" — so chunks 1..wensize-1 of that
            // write were dropped. Replicate the bit across every chunk: a 0/1
            // path condition times the all-ones mask is all-ones or zero.
            for (const int port : mi.plain_wr_ports) {
              const auto pid = static_cast<hhds::Port_id>(port * kMemPortStride + 4);
              Pin        cur;
              if (auto en_sink = driven_sink_at(mi.node, static_cast<uint64_t>(pid)); !en_sink.is_invalid()) {
                cur = en_sink.get_driver_pin();
                en_sink.del_sink();
              }
              if (cur.is_invalid()) {
                continue;
              }
              auto rep = make_node(Ntype_op::Mult);
              setup_sink_by_name(rep, "as").connect_driver(cur);
              setup_sink_by_name(rep, "as").connect_driver(create_const(*g_, *Dlop::get_mask_value(static_cast<int>(wensize))));
              auto out = rep.create_driver_pin(0);
              set_ubits(out, static_cast<int32_t>(wensize));
              mi.node.create_sink_pin(pid).connect_driver(out);
            }
          }
        }
        // Re-drive the forwarding mask (fwd, port 5).  lower_mem_declare reads
        // `fwd` from pending_attrs_ at declare time, which only works when the
        // attr_set precedes the declare (the slang reader's order).  When the
        // Pyrope source folds the attr onto the declaration (`reg
        // t:[N]T:[fwd=0]`) prp2lnast emits the attr_set AFTER the declare, so
        // it lands here.
        // A whole-array cell keeps the fwd=0 that lower_mem_update_store
        // forced: a clocked bulk update is a next-state no same-cycle read
        // observes, and cgen emits `dout = data[addr]` for it regardless.
        if (auto fit = pit->second.find("fwd"); fit != pit->second.end() && !mi.has_update) {
          if (auto fv = Dlop::from_pyrope(fit->second); fv && fv->is_just_i64()) {
            if (auto fwd_sink = driven_sink_at(mi.node, 5); !fwd_sink.is_invalid()) {
              fwd_sink.del_sink();
            }
            setup_sink_by_name(mi.node, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(fv->to_just_i64())));
          }
        }
      }

      // FAIL CLOSED on `:[enable=…]`, which finalize_regs lowers on a plain
      // flop/latch but which NOTHING wires on an array/memory: the write
      // enables come from the per-store conditions, so the attribute vanished
      // without a word and the array was written on every cycle. Same rule as
      // the latch refusal in finalize_regs — an attribute that silently
      // evaporates is worse than not having it.
      if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
        if (pit->second.contains("enable")) {
          error_here(
              "upass.tolg: array/memory '{}' carries an `enable` attribute, "
              "which the Memory cell path does not lower — the attribute would "
              "be SILENTLY DROPPED. Guard the store with an `if` instead: a "
              "memory write enable comes from the condition of the write, not "
              "from an attribute",
              name);
        }
      }

      // Clocked (non-array) memory clock wiring, deferred from
      // lower_mem_declare (the clock_pin/posclk attr_set arrives after the
      // declare). Mirrors the per-reg wiring in finalize_regs: an explicit
      // clock_pin=<input> (the slang reader emits it for a non-`clk`/`clock`
      // write clock) beats the implicit shared clock; posclk=false marks a
      // negedge write clock.
      if (!mi.update_clock.is_invalid()) {
        // The bulk-update bus has one clock. Combining it with entry writes
        // on another clock/edge cannot be represented by this memory cell.
        bool clock_wired = false;
        for (auto sink : mi.node.inp_sorted_pins()) {  // read-only walk
          if (static_cast<int>(sink.get_port_id()) % kMemPortStride != 2) {
            continue;
          }
          if (sink.get_driver_pin() != mi.update_clock) {
            error_here("upass.tolg: memory '{}' bulk and entry writes require one shared clock", name);
          }
          clock_wired = true;
        }
        if (mi.has_store_clock && (mi.mixed_store_edges || mi.first_store_posclk != mi.update_posclk)) {
          error_here("upass.tolg: memory '{}' bulk and entry writes require one shared clock edge", name);
        }
        if (!clock_wired) {
          setup_sink_by_name(mi.node, "clock_pin").connect_driver(mi.update_clock);
        }
        setup_sink_by_name(mi.node, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(mi.update_posclk ? 1 : 0)));
      } else if (mi.has_store_clock) {
        const int polarity = mi.mixed_store_edges ? Ntype::Memory_posclk_mixed : (mi.first_store_posclk ? 1 : 0);
        setup_sink_by_name(mi.node, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(polarity)));
      } else if (!mi.is_array) {
        // A file preload initializes persistent contents, but an unwritten
        // array has only asynchronous reads. It needs no clock connection.
        const auto  init_sink         = driven_sink_at(mi.node, Ntype::get_sink_pid(Ntype_op::Memory, "initial"));
        const auto  init              = init_sink.is_invalid() ? Pin{} : init_sink.get_driver_pin();
        const bool  read_only_preload = mi.n_user_wr == 0 && !mi.has_update && !mi.reset_init && !mi.is_pub && init.is_const()
                                        && livehd::graph_util::const_of(init).is_string()
                                        && hlop::memory_image(livehd::graph_util::const_of(init).to_string()).has_value();
        bool        posclk_val        = true;
        std::string clock_pin_name;
        if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
          if (auto cit = pit->second.find("clock_pin"); cit != pit->second.end()) {
            clock_pin_name = cit->second;
          } else if (auto scit = pit->second.find("__store_clock_pin"); scit != pit->second.end()) {
            // An unwritten SROA field still belongs to its declaring process.
            clock_pin_name = scit->second;
          }
          if (auto pcit = pit->second.find("posclk"); pcit != pit->second.end()) {
            posclk_val = pcit->second != "false" && pcit->second != "0";
          } else if (auto spit = pit->second.find("__store_posclk"); spit != pit->second.end()) {
            posclk_val = spit->second != "false" && spit->second != "0";
          }
        }
        setup_sink_by_name(mi.node, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(posclk_val ? 1 : 0)));
        if (!clock_pin_name.empty()) {
          // Same resolution as the per-reg wiring: a module input first, then an
          // internal/derived wire (a gated clock — clock-gate cell output —
          // clocking a reg array; use its DRIVER, not the passthrough buffer),
          // then any plain named pin. A folded constant (`clock_pin=true`,
          // `const k = true`) names no signal: check_mem_const_clock reports it.
          if (const auto cp = resolve_attr_signal(clock_pin_name); !cp.is_invalid()) {
            if (check_clock_pin_class(mi.decl_nid, std::format("memory `{}`", name), clock_pin_name, cp)) {
              setup_sink_by_name(mi.node, "clock_pin").connect_driver(cp);
            }
          } else if (const auto cv = Dlop::from_pyrope(clock_pin_name); cv && cv->is_numeric()) {
            setup_sink_by_name(mi.node, "clock_pin").connect_driver(create_const(*g_, *cv));
          } else if (tuple_recs_.contains(clock_pin_name)) {
            error_hint_at(mi.decl_nid,
                          {"clock-bind-not-clock", "type"},
                          std::format("memory `{}` has one clock: its clock_pin names a single `Clock`, not a tuple", name),
                          "per-port clocks need a `__memory` cell (a `clock_pin` tuple, one entry per port)");
          } else if (cv && cv->is_string()) {
            error_hint_at(mi.decl_nid,
                          {"clock-bind-not-clock", "type"},
                          std::format("memory `{}` names the string {} as its clock_pin, not a `Clock`", name, clock_pin_name),
                          "name the `Clock` signal itself: `clock_pin=clk`");
          } else {
            error_here(
                "upass.tolg: memory '{}' names clock_pin '{}' but '{}' "
                "has no such input/wire",
                name,
                clock_pin_name,
                lnast_->get_top_module_name());
          }
        } else if (read_only_preload) {
          // Keep an explicitly bound clock above, but do not introduce an
          // implicit dependency merely because the contents are a reg array.
        } else if (!clock_name_.empty()) {
          setup_sink_by_name(mi.node, "clock_pin").connect_driver(clock_pin());
        } else {
          warn_at(Lnast_nid{}, {"no-clock", "time"}, "memory '{}' has no clock input to bind", name);
        }
      }
      if (!mi.is_array) {
        check_mem_const_clock(mi.node, mi.decl_nid, name);
      }

      // ── Memory reset: ONE cycle, every entry, through the cell's whole-array
      // `reset` pin (14) + the `initial` bus — the same contract a scalar reg
      // has. Two sources, ONE wiring:
      //   (a) the declaration's `= <const|tuple>` (mi.reset_init, already on the
      //       `initial` pin); its reset signal is the declaration's reset_pin=
      //       or the module's implicit reset (mem_reset_source);
      //   (b) the importer's `initial=<packed bus> reset_pin=<sig>` attr pair on
      //       a whole-array (`update`) cell — the slang reader's form.
      // `initial=` (04b-attributes.md: the PACKED contents, entry 0 in the low
      // bits) next to a declared reset value must AGREE with it: both ride the
      // one `initial` pin (power-on contents == reset value), so two different
      // values is a contradiction, not an override. Next to `= nil` it is
      // power-on-only contents and NO reset is wired.
      if (!mi.is_array) {
        const absl::flat_hash_map<std::string, std::string>* attrs = nullptr;
        if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
          attrs = &pit->second;
        }
        auto attr_of = [&](std::string_view k) -> std::string_view {
          if (attrs == nullptr) {
            return {};
          }
          auto ait = attrs->find(std::string(k));
          return ait == attrs->end() ? std::string_view{} : std::string_view(ait->second);
        };
        spool_ptr<Dlop> attr_init;
        if (auto itxt = attr_of("initial"); !itxt.empty() && itxt != "false") {
          attr_init = Dlop::from_pyrope(itxt);
          if (!attr_init || attr_init->is_invalid()) {
            error_here(
                "upass.tolg: memory '{}' `initial={}` is not a comptime constant "
                "(the packed contents, entry 0 in the low bits)",
                name,
                itxt);
            continue;
          }
          if (attr_init->is_nil()) {
            attr_init = Dlop::create_integer(0);  // as always: a nil initial is 0
          }
        }
        if (attr_init && mi.reset_init) {
          if (!attr_init->eq_op(*mi.reset_init)->is_known_true()) {
            error_here(
                "upass.tolg: memory '{}' declares the reset value {} (`= …`) but also carries `initial={}`: a "
                "register array's initializer IS both its power-on contents and its reset value (one `initial` "
                "pin), so the two must agree — drop one of them, or spell `= nil` to keep `initial=` as "
                "power-on-only contents with no reset",
                name,
                mi.reset_init->to_pyrope(),
                attr_of("initial"));
            continue;
          }
        } else if (attr_init) {
          // Power-on contents (`= nil` + `initial=`), or the importer's
          // reset-value bus on a whole-array cell: replaces any declare-time
          // `initial` (pid 11).
          if (auto init_sink = driven_sink_at(mi.node, 11); !init_sink.is_invalid()) {
            init_sink.del_sink();
          }
          setup_sink_by_name(mi.node, "initial").connect_driver(create_const(*g_, *attr_init));
        }
        const auto rpn         = attr_of("reset_pin");
        const bool wants_reset = mi.reset_init ? !mem_reset_source(name).empty()
                                               : (mi.has_update && !rpn.empty() && rpn != "false" && static_cast<bool>(attr_init));
        // A DECLARED RESET VALUE WITH NOTHING TO APPLY IT IS AN ERROR, not a
        // silent drop. `wants_reset` above goes false when mem_reset_source() is
        // empty, which happens two ways: the module has no reset input at all,
        // or this declare says `reset_pin=false`. Either way the `= <value>`
        // the source asked for would never be applied and the array would come
        // up holding whatever the power-on fill gives it -- the memory analogue
        // of the reg case finalize_regs already diagnoses ("reg '{}' has a reset
        // value but '{}' has no reset input"). Say so, and name the spelling
        // that DOES mean "power-on contents, no reset": `= nil` plus `initial=`.
        // Power-on-only contents are legitimate and are NOT caught here --
        // mi.reset_init is unset for them.
        if (wants_reset) {
          const auto rst_name = mem_reset_source(name);
          // A memory's reset signal resolves exactly like a scalar reg's (see
          // finalize_regs): usually a graph input, but a reset synchronizer
          // drives it from a DERIVED module-level signal. This used to be a
          // bare `has_input(rst_name) ? get_input_pin(...) : reset_pin()`, so a
          // `reset_pin=<internal wire>` on an array reg silently fell back
          // to the module's implicit reset — and in a module that has none,
          // to an INVALID pin, which then reached not1()/and2() and left the
          // array's write-enable ANDed with a dangling node that cgen folds to
          // constant 0. The array could never be written (lhdsuite minion:
          // `prv`, `reg_fcc_counter`, `id_ctrl_stall_trans_cnt`) and the
          // emitted Verilog carried a nameless `reg signed ;` for the dangling
          // node. Without a declared `reset_pin=` the memory takes the IMPLICIT
          // reset, and only reset_pin() rejects a module with two or more
          // `Reset` inputs -- resolving mem_reset_source's fallback name here
          // silently bound the first one.
          Pin        rst      = decl_reset_pin_.contains(std::string(name)) ? resolve_reset_signal(rst_name) : reset_pin();
          if (rst.is_invalid()) {
            error_here("upass.tolg: memory '{}' names unknown reset signal '{}'", name, rst_name);
            continue;
          }
          std::optional<bool> negreset;
          if (auto nv = attr_of("negreset"); !nv.empty()) {
            negreset = nv != "false" && nv != "0";
          }
          const bool neg
              = rst_name.empty() || rst_name == reset_name_ ? negreset.value_or(reset_neg_) : reset_active_low(negreset, rst_name);
          if (neg) {
            rst = not1(rst);
          }
          // Program writes are suppressed while reset is high, exactly like a
          // scalar reg's din. The reset arm already has PRIORITY in every
          // consumer (cgen inline, cgen_sim, pass/lec encode, pass/abc
          // mem_lower); the gate is what keeps a same-cycle read from
          // FORWARDING a write that never lands (a read during reset returns
          // the committed contents), and it keeps a consumer that commits
          // per-port writes after the whole-array apply correct regardless.
          const Pin     not_rst = not1(rst);
          const int32_t lane_en = static_cast<int32_t>(mi.has_masked_wr ? mi.wensize : mem_wensize_attr(name));
          mi.not_rst            = not_rst;
          for (int u = 0; u < mi.n_user_wr; ++u) {
            const auto pid = static_cast<uint64_t>(u * kMemPortStride + 4);
            if (auto en_sink = driven_sink_at(mi.node, pid); !en_sink.is_invalid()) {
              auto old_en = en_sink.get_driver_pin();
              en_sink.del_sink();
              // A lane-vector enable (write-masked partial writes) is gated
              // whole: a 1-bit AND would keep only lane 0.
              mi.node.create_sink_pin(static_cast<hhds::Port_id>(pid))
                  .connect_driver(lane_en > 1 ? gate_lanes(not_rst, old_en, lane_en) : and2(old_en, not_rst));
            }
          }
          if (mi.has_update) {
            mi.update_en = and2(mi.update_en, not_rst);
            redrive_mem_sink(mi, 13, mi.update_en);
          }
          setup_sink_by_name(mi.node, "reset").connect_driver(rst);
          // Preserve reset EDGE semantics as well as its value. The
          // whole-array Memory pin block is full, so cgen consumes this
          // node marker when it builds the event control.
          bool async = reset_async_default_;
          if (auto av = attr_of("async"); !av.empty()) {
            async = av != "false" && av != "0";
          } else if (auto sv = attr_of("sync"); !sv.empty()) {
            async = sv == "false" || sv == "0";
          }
          if (async) {
            mi.node.attr(livehd::attrs::memory_async_reset).set(1);
          }
        }
      }

      // Same-cycle reads that must see this cycle's writes in logic (see
      // forward_mem_read): every partial write's old-value read, and on a
      // memory with a whole-array store the user reads its `ordering` lets see
      // writes -- "program" the ones before it, "fwd" all of them; "old" and
      // "none" read the committed entry (a legal refinement of "none"'s
      // undefined collision).
      const bool replays = !mi.is_array && !mi.has_legacy_fwd;
      if (replays && !mi.has_masked_wr) {  // lane_mask_mem_writes already sized a masked memory
        mi.wensize = mem_wensize_attr(name);
      }
      mi.rd_replay.assign(static_cast<size_t>(n_rd), {0, 0});
      if (replays) {
        for (int r = 0; r < n_rd; ++r) {
          const auto ri = static_cast<size_t>(r);
          if (mi.rd_is_rmw[ri] || (mi.has_update && ordering == Mem_info::Mem_order::program)) {
            mi.rd_replay[ri] = {mi.rd_wr_before[ri], mi.rd_bulk_before[ri]};
          } else if (mi.has_update && ordering == Mem_info::Mem_order::fwd) {
            mi.rd_replay[ri] = {mi.n_user_wr, static_cast<int>(mi.bulk_sites.size())};
          }
        }
      }
      // Whole-array reads (forward_read_all): the read_all output has no
      // collision-matrix row, so whatever the cell would forward to a
      // per-element read port at the same position is replayed in logic.
      mi.ra_replay.assign(mi.read_all_sites.size(), {0, 0});
      if (replays) {
        for (size_t s = 0; s < mi.read_all_sites.size(); ++s) {
          if (ordering == Mem_info::Mem_order::program) {
            mi.ra_replay[s] = {mi.read_all_sites[s].wr_before, mi.read_all_sites[s].bulk_before};
          } else if (ordering == Mem_info::Mem_order::fwd) {
            mi.ra_replay[s] = {mi.n_user_wr, static_cast<int>(mi.bulk_sites.size())};
          }
        }
      }

      // A read-less (or access-less) memory is a WARNING at most — its state
      // can be observed by a scan chain, and a future remote regref may
      // attach reads/writes. `pub` (regref potential) silences it entirely.
      if (!mi.is_pub && mi.rd_next == 0 && mi.read_all_pin.is_invalid()) {
        warn_at(Lnast_nid{},
                {"memory-never-read", "type"},
                "memory '{}' is never read — contents are only observable via "
                "scan/regref",
                name);
      }
    }
  }

  // A defaulted comb input's default expression (`b:u8 = chk(a=a)`) is lowered
  // as the body PROLOGUE, ending at its `__default_b` store. The comb's OWN
  // module drives `b` from its input port -- the default binds only where an
  // inlined call omits the argument -- so that prologue is dead here, side
  // effects included: an `assert` inside the default expression must not
  // become a check of the comb's own module. lower_stmts skips the statements
  // upass::unused_default_prologue marks.
  void mark_default_only_prologue(const Lnast_nid& stmts) {
    absl::flat_hash_set<std::string> locals;
    for (const auto& e : lnast_->io_meta().inputs) {
      if (e.has_default) {
        locals.insert(Lnast_io_entry::default_value_name(e.name));
      }
    }
    default_only_stmts_ = upass::unused_default_prologue(*lnast_, stmts, locals, {});
  }

  // type_spec(ref(name), type) on a SOURCE name is a typed declaration that
  // carries no value -- the comb inliner types each inlined output this way
  // before the body runs. Like `mut v:uN = nil`, such a name starts as nil, so
  // a first partial write `y#[lo..=hi] = x` into an inlined comb output seeds
  // its uncovered bits at the declared width (set_mask_base). A compiler
  // temp's type_spec only stamps the width of the value it already holds.
  void note_type_spec(const Lnast_nid& nid) {
    const auto name_nid = lnast_->get_first_child(nid);
    if (name_nid.is_invalid() || !Lnast_ntype::is_ref(lnast_->get_type(name_nid))) {
      return;
    }
    const auto type_nid = lnast_->get_sibling_next(name_nid);
    if (type_nid.is_invalid()) {
      return;
    }
    const std::string_view name = lnast_->get_name(name_nid);
    if (Lnast::is_tmp(name) || logical_key(name).starts_with("___")) {
      return;
    }
    const auto [dmw, dsigned] = declared_width(type_nid);
    if (dmw <= 0) {
      return;
    }
    record_decl_type(name, dmw, dsigned);
    scalar_decl_.insert(std::string(canon_io_name(name)));
  }

  // Declared (mw, is_signed) from a declare's type child. prim_type_int(max,
  // min): unsigned iff min ≥ 0, mw mirrors the ssa io harvest (get_signed_bits()-1
  // drops the sign bit when unsigned). prim_type_bool → 1. Unknown → (0,_).
  [[nodiscard]] std::pair<int32_t, bool> declared_width(const Lnast_nid& type_nid) {
    using N      = Lnast_ntype;
    const auto t = lnast_->get_type(type_nid);
    if (N::is_prim_type_bool(t) || N::is_prim_type_clock_or_reset(t)) {
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
        min_bits  = static_cast<int32_t>(mn_v->get_signed_bits());
      }
    }
    const bool is_signed = !(min_known && !min_neg);
    if (!is_signed) {
      auto bits = std::max<int32_t>(1, static_cast<int32_t>(max_v->get_payload_bits()));
      return {bits, false};
    }
    // Signed: the WIDER of the two bounds' signed widths (mirrors the ssa io
    // harvest + io_mw — a min like -100 needs more bits than a max of 3).
    auto bits = static_cast<int32_t>(max_v->get_signed_bits());
    if (min_known) {
      bits = std::max(bits, min_bits);
    }
    return {bits, true};
  }

  // `stage[N] t = f(...)` where f has SEVERAL outputs: the result is the
  // instance's port tuple (sub_results_), not one pin. Each output follows the
  // single-output rule -- a mod output lands at its declared cycle and N must
  // match it (unless `::[timecheck=false]`, user ruling 2026-09-28 (22)), a
  // pipe is realized at its declared minimum, a comb output at cycle 0 -- and
  // the caller adds the cycles an output still misses as that output's own
  // stage flops (staged_fields_, named `t_q`: a dotted name would read as the
  // instance `t`'s own state), so every `t.q` reads at cycle N. `n` < 0 is a
  // `stage[]` or ranged pick; `rhs_name` names the call result.
  void stage_multi_output_call(std::string_view name, int64_t n, const std::string& rhs_name, const Lnast_nid& decl_nid) {
    const auto res = sub_results_.at(rhs_name);  // copy: the inserts below may rehash
    if (n < 0) {
      error_at(decl_nid,
               "upass.tolg: `stage[]` / ranged stage counts on a pipe/mod call are not supported yet — write a fixed "
               "`stage[N]` for '{}'",
               name);
      return;
    }
    const bool           mismatch_ok = lnast_->is_timecheck_off();
    std::string          landing;  // every output's cycle, for the diagnostic
    bool                 mod_mismatch = false;
    std::vector<int64_t> deficit(res.outputs.size(), n);  // an untimed (comb) output lands at 0
    for (size_t i = 0; i < res.outputs.size(); ++i) {
      landing += std::format("{}{}", landing.empty() ? "" : ", ", canon_io_name(res.outputs[i].name));
      if (i >= res.out_stages.size() || !res.out_stages[i]) {
        landing += "@[0]";
        continue;
      }
      const auto& so = *res.out_stages[i];
      landing += so.is_pipe || so.cmax == so.cmin ? std::format("@[{}]", so.cmin) : std::format("@[{}..={}]", so.cmin, so.cmax);
      if (so.is_pipe) {
        if (!mismatch_ok && (n < so.cmin || (so.cmax >= so.cmin && n > so.cmax))) {
          error_at(decl_nid,
                   "upass.tolg: stage[{}] on '{}' is outside the callee's declared latency range [{}, {}]",
                   n,
                   name,
                   so.cmin,
                   so.cmax >= so.cmin ? so.cmax : so.cmin);
          return;
        }
      } else if (!mismatch_ok && (so.cmin != so.cmax || n != so.cmin)) {
        mod_mismatch = true;
      }
      deficit[i] = std::max<int64_t>(0, n - so.cmin);
    }
    if (mod_mismatch) {
      error_hint_at(decl_nid,
                    {"stage-multi-output", "time"},
                    std::format("upass.tolg: `stage[{}] {} = ...` over a call with several outputs needs every output to "
                                "land at cycle {}, but they land at: {}",
                                n,
                                name,
                                n,
                                landing),
                    std::format("bind the call where its outputs land (a mod: `const {} = f(...)`, each output read at its "
                                "own cycle) and add a separate `stage[K] x = {}.out` for an output that needs extra delay",
                                name,
                                name));
      return;
    }
    // The realized split, as for one output: the instance contributes each
    // output's declared minimum and the caller's flops the rest.
    for (const auto& so : res.out_stages) {
      if (so) {
        so->node.attr(livehd::attrs::time_range).set({so->cmin, so->cmin});
        sub_time_[{so->node.get_debug_nid(), so->out_pid}] = {so->cmin, so->cmin};
      }
    }
    absl::flat_hash_map<std::string, Val> fields;
    for (size_t i = 0; i < res.outputs.size(); ++i) {
      if (deficit[i] > 0) {
        const std::string oname{canon_io_name(res.outputs[i].name)};
        fields[oname]
            = make_stage_flop(absl::StrCat(name, "_", oname), sub_output_val(res.sub, res.outputs[i]), deficit[i], deficit[i]);
      }
    }
    sub_results_[std::string(name)] = res;
    if (!fields.empty()) {
      staged_fields_[std::string(name)] = std::move(fields);
    }
  }

  // `stage[N] t = (q1=a, q2=b)`: a tuple value -- the result of an inlined
  // comb with several outputs -- gets one stage flop set per field, read back
  // through staged_fields_ (`t.q1`). Positional fields are keyed by index.
  void stage_tuple_value(std::string_view name, int64_t n, const Tuple_rec& rec, const Lnast_nid& decl_nid) {
    if (n < 0) {
      error_at(decl_nid,
               "upass.tolg: `stage[]` / ranged stage counts on a tuple value are not supported yet — write a fixed "
               "`stage[N]` for '{}'",
               name);
      return;
    }
    absl::flat_hash_map<std::string, Val> fields;
    const auto                            stage_field = [&](const std::string& key, const Lnast_nid& value) {
      const auto v = leaf(value);
      fields[key]  = n == 0 ? v : make_stage_flop(absl::StrCat(name, "_", key), v, n, n);
    };
    for (size_t i = 0; i < rec.elems.size(); ++i) {
      stage_field(std::to_string(i), rec.elems[i]);
    }
    for (const auto& [key, value] : rec.named) {
      stage_field(std::string(canon_io_name(key)), value);
    }
    staged_fields_[std::string(name)] = std::move(fields);
  }

  // The value of output `oe` of the instance `sub`, bound with the io-entry
  // width/sign contract (a bool, and any one-bit output, is one bit).
  Val sub_output_val(hhds::Node_class sub, const Lnast_io_entry& oe) {
    auto          out_dpin = sub.create_driver_pin(std::string(canon_io_name(oe.name)));
    const int32_t mw       = io_mw(oe);
    if (oe.kind == Io_kind::boolean) {
      set_ubits(out_dpin, 1);
      return {out_dpin, 1};
    }
    if (mw <= 1) {
      set_bits(out_dpin, 1);
      if (oe.is_signed) {
        set_sign(out_dpin);
      } else {
        set_unsign(out_dpin);
      }
      return {out_dpin, 1};
    }
    if (oe.is_signed) {
      set_bits(out_dpin, mw);
      set_sign(out_dpin);
    } else {
      set_ubits(out_dpin, mw);
    }
    return {out_dpin, mw};
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
    cur_color_         = p.decl_color;  // stage flop lands in its declare's region
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
    if (auto srt = sub_results_.find(rhs_name); srt != sub_results_.end() && srt->second.outputs.size() > 1) {
      stage_multi_output_call(name, min_nil || max_nil || smin != smax ? -1 : smin, rhs_name, p.decl_nid);
      return;
    }
    if (auto trt = tuple_recs_.find(rhs_name); trt != tuple_recs_.end()) {
      const auto rec = trt->second;  // copy: leaf() may grow the maps
      stage_tuple_value(name, min_nil || max_nil || smin != smax ? -1 : smin, rec, p.decl_nid);
      return;
    }
    if (auto sit = sub_out_stages_.find(rhs_name); sit != sub_out_stages_.end()) {
      const auto& so = sit->second;
      if (min_nil || max_nil || smin != smax) {
        error_here(
            "upass.tolg: `stage[]` / ranged stage counts on a pipe/mod "
            "call are not supported yet — "
            "write a fixed `stage[N]` for '{}'",
            name);
        return;
      }
      const int64_t n           = smin;
      // `::[timecheck=false]` turns the latency match off too (user ruling
      // 2026-09-28 (22)): the callee still lands at its declared minimum and
      // the caller adds the missing cycles, if any, as stage flops.
      const bool    mismatch_ok = lnast_->is_timecheck_off();
      if (so.is_pipe) {
        // pipe convention: cmax < cmin (e.g. bare pipe (1,0)) = no upper bound.
        if (!mismatch_ok && (n < so.cmin || (so.cmax >= so.cmin && n > so.cmax))) {
          if (so.cmax >= so.cmin) {
            error_here(
                "upass.tolg: stage[{}] on '{}' is outside the callee's "
                "declared latency range [{}, {}]",
                n,
                name,
                so.cmin,
                so.cmax);
          } else {
            error_here(
                "upass.tolg: stage[{}] on '{}' is below the callee's "
                "declared minimum latency {}",
                n,
                name,
                so.cmin);
          }
          return;
        }
      } else {
        // mod callee: the output's landing cycle is fixed by its interface.
        if (!mismatch_ok && (so.cmin != so.cmax || n != so.cmin)) {
          error_here(
              "upass.tolg: mod call result '{}' lands at its declared "
              "cycle {} — `stage[{}]` must match it "
              "(add a separate `stage[N] x = value` for extra delay)",
              name,
              so.cmin,
              n);
          return;
        }
      }
      // The stage pick pins the REALIZED split: the callee
      // instance contributes its declared min (Phase-1 realization), the
      // caller-side deficit flop the remaining n − cmin. Stamping the full
      // pick on the instance would double-count the deficit.
      emin = emax = std::max<int64_t>(0, n - so.cmin);
      {
        const int64_t realized = so.cmin;
        so.node.attr(livehd::attrs::time_range).set({realized, realized});
        sub_time_[{so.node.get_debug_nid(), so.out_pid}] = {realized, realized};
      }
    } else if (min_nil || max_nil) {
      error_here(
          "upass.tolg: `stage[]` on '{}' has no chosen count at "
          "realization — write `stage[N]` (the toolchain-picked "
          "default lands in a later phase)",
          name);
      return;
    }

    auto v = leaf(rhs);
    if (emin == 0 && emax == 0) {
      record(name, v.pin, v.mw);  // zero-depth stage = wire
      return;
    }
    const auto q = make_stage_flop(name, v, emin, emax);
    record(name, q.pin, q.mw);
  }

  // The stage flop of depth (emin, emax) over `v`, named after the stage
  // variable `name`; returns its q.
  Val make_stage_flop(std::string_view name, const Val& v, int64_t emin, int64_t emax) {
    auto flop                         = make_node(Ntype_op::Flop);
    flop_depth_[flop.get_debug_nid()] = {emin, emax};
    // The LN-inserted pipe output flop (vs a user `stage[N]` reg) is
    // the narrowing target: LG pass1 rewrites its depth to (min−σ, max−σ).
    if (name.starts_with("%pipe_")) {
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
    set_ubits(q, v.mw);
    // Keep the stage register's RTL name on q, exactly like finalize_regs does
    // for a `reg`. Without it cgen synthesizes `flop_<nid>`, and pass/lec's
    // tier-1 state pairing (which is BY NAME) then matches nothing: every flop
    // of the def falls through to the speculative tier-2 signature pass, whose
    // uncertain pairs suppress a bounded-bmc PASS. A `stage[N]` design would
    // report UNKNOWN even though it is provably equivalent.
    //
    // `%pipe_<output>` is an LN-inserted pipe-output flop. The prefix is
    // lowering-only, but the suffix is the stable source/output state name and
    // is exactly the tier-1 correspondence anchor used by another front end.
    // Keeping the whole temp anonymous turns a fixed-latency pipe into an
    // unpairable `f:<nid>` frontier and cuts every downstream semdiff region.
    {
      std::string base{name};
      if (base.starts_with("%pipe_")) {
        base.erase(0, std::string_view{"%pipe_"}.size());
      }
      if (auto ssa = base.find("___ssa_"); ssa != std::string::npos) {
        base.resize(ssa);
      }
      if (!base.empty() && base.front() != '%') {
        livehd::graph_util::set_pin_name(q, base);
        // Also stamp the flop NODE name so get_hier_name() reports the register
        // name instead of the `n<id>` fallback (see finalize_regs).
        flop.set_name(base);
      }
    }
    reg_map_.emplace(std::string(name), flop);
    return {q, v.mw};
  }

  // True when `nid`'s subtree names `ref`. Used to bound the LHS search below:
  // once the call's result temp has been consumed by anything else, a later
  // store of that temp is no longer "the variable this call binds to".
  bool subtree_reads_ref(const Lnast_nid& nid, std::string_view ref) const {
    if (nid.is_invalid()) {
      return false;
    }
    if (Lnast_ntype::is_ref(lnast_->get_type(nid)) && lnast_->get_name(nid) == ref) {
      return true;
    }
    for (auto c : lnast_->children(nid)) {
      if (subtree_reads_ref(c, ref)) {
        return true;
      }
    }
    return false;
  }

  // The source variable a temp-dst call result is copied into, or "" when the
  // result is consumed by an expression instead (a multi-output instance read
  // through `tuple_get`, an inline `f(g(x))`, ...). A declared binding
  // (`const lane_q = lane(…)`, `var q = Mod(…)`) lowers to a temp dst plus a
  // following `store(lane_q, %t)`, so without this the instance loses the name
  // the source gave it and falls back to `u_<callee>_<temp>` — while the very
  // same call written `lane_q = lane(…)` keeps it. Scanning stops at the first
  // statement that reads the temp for any other purpose.
  std::string lhs_var_of_temp_dst(const Lnast_nid& call, std::string_view dst_txt) const {
    // The copy-out is emitted right behind the call (at most a `declare` in
    // between), so a handful of statements is all this ever needs to look at.
    // The bound also keeps a call whose result is UNUSED — nothing ever reads
    // the temp, so the scan has no natural stop — from walking the rest of the
    // module once per such call.
    int budget = 8;
    for (auto s = lnast_->get_sibling_next(call); !s.is_invalid() && budget-- > 0; s = lnast_->get_sibling_next(s)) {
      if (Lnast_ntype::is_store(lnast_->get_type(s))) {
        auto tgt = lnast_->get_first_child(s);
        auto src = tgt.is_invalid() ? Lnast_nid{} : lnast_->get_sibling_next(tgt);
        if (!src.is_invalid() && Lnast_ntype::is_ref(lnast_->get_type(src)) && lnast_->get_name(src) == dst_txt
            && lnast_->get_sibling_next(src).is_invalid()) {
          std::string v(lnast_->get_name(tgt));
          if (auto p = v.find("___ssa_"); p != std::string::npos) {
            v.resize(p);
          }
          // A dotted store (`t.f = %tmp`) names a tuple field, not an instance.
          if (v.empty() || v.front() == '%' || v.find('.') != std::string::npos) {
            return {};
          }
          // A PORT of the enclosing module is not an instance binding. `y =
          // add1(…)` says where the result goes, not what to call the box, and
          // taking it would give the instance the port's own spelling — which
          // both backends must then rename anyway (Verilog: an instance beside
          // `output reg y`; sim: a struct member beside the `Out` field). Fall
          // through to the synthesized name.
          for (const auto& e : lnast_->io_meta().inputs) {
            if (e.name == v) {
              return {};
            }
          }
          for (const auto& e : lnast_->io_meta().outputs) {
            if (e.name == v) {
              return {};
            }
          }
          return v;
        }
      }
      if (subtree_reads_ref(s, dst_txt)) {
        return {};
      }
    }
    return {};
  }

  // rolled_for owns an ordinary call payload but transports replication in an
  // explicit node, never in reserved actuals. Lower the hidden payload first,
  // then attach the native HHDS descriptor and literal carry self-edges to the
  // Sub that payload created.
  void lower_rolled_for(const Lnast_nid& nid) {
    std::vector<Lnast_nid> kids;
    for (auto c : lnast_->children(nid)) {
      kids.emplace_back(c);
    }
    if (kids.size() != lnast_rolled_for::arity || !Lnast_ntype::is_ref(lnast_->get_type(kids[lnast_rolled_for::index]))
        || !Lnast_ntype::is_tuple_add(lnast_->get_type(kids[lnast_rolled_for::carries]))
        || !Lnast_ntype::is_stmts(lnast_->get_type(kids[lnast_rolled_for::lowering_payload]))) {
      error_here("upass.tolg: malformed rolled_for transport in '{}'", lnast_->get_top_module_name());
      return;
    }

    int64_t  domain_first = 0;
    int64_t  domain_step  = 0;
    uint64_t domain_count = 0;
    if (!absl::SimpleAtoi(lnast_->get_name(kids[lnast_rolled_for::first]), &domain_first)
        || !absl::SimpleAtoi(lnast_->get_name(kids[lnast_rolled_for::step]), &domain_step)
        || !absl::SimpleAtoi(lnast_->get_name(kids[lnast_rolled_for::count]), &domain_count) || domain_step == 0) {
      error_here("upass.tolg: malformed rolled_for domain in '{}'", lnast_->get_top_module_name());
      return;
    }

    // A register carry has two additional fields: the enclosing register and
    // a temporary bound to its pending D before the call. Q remains available
    // under the ordinary register name throughout payload lowering.
    for (auto map : lnast_->children(kids[lnast_rolled_for::carries])) {
      auto in  = lnast_->get_first_child(map);
      auto out = in.is_invalid() ? in : lnast_->get_sibling_next(in);
      auto reg = out.is_invalid() ? out : lnast_->get_sibling_next(out);
      if (!reg.is_invalid()) {
        auto seed = lnast_->get_sibling_next(reg);
        if (seed.is_invalid()) {
          error_here("upass.tolg: malformed rolled_for register carry");
          return;
        }
        auto value = set_mask_base(reg);
        record(lnast_->get_name(seed), value.pin, value.mw);
      }
    }

    const std::string saved_index = std::exchange(rolled_index_port_, std::string(lnast_->get_name(kids[lnast_rolled_for::index])));
    last_lowered_sub_             = {};
    lower_stmts(kids[lnast_rolled_for::lowering_payload]);
    rolled_index_port_ = saved_index;
    auto sub           = last_lowered_sub_;
    last_lowered_sub_  = {};
    if (sub.is_invalid()) {
      error_here("upass.tolg: rolled_for payload did not create an instance in '{}'", lnast_->get_top_module_name());
      return;
    }
    auto gio = sub.get_subnode_io();
    if (!gio) {
      error_here("upass.tolg: rolled_for instance has no callee interface in '{}'", lnast_->get_top_module_name());
      return;
    }
    const auto input_pid = [&](std::string_view name) -> std::optional<hhds::Port_id> {
      for (const auto& d : gio->get_input_pin_decls()) {
        if (d.name == name) {
          return d.port_id;
        }
      }
      return std::nullopt;
    };
    const auto output_pid = [&](std::string_view name) -> std::optional<hhds::Port_id> {
      for (const auto& d : gio->get_output_pin_decls()) {
        if (d.name == name) {
          return d.port_id;
        }
      }
      return std::nullopt;
    };

    hhds::Subnode_loop desc;
    desc.first       = domain_first;
    desc.step        = domain_step;
    desc.count       = domain_count;
    desc.index_input = input_pid(lnast_->get_name(kids[lnast_rolled_for::index]));
    if (!desc.index_input) {
      error_here("upass.tolg: rolled_for index port '{}' is not a callee input", lnast_->get_name(kids[lnast_rolled_for::index]));
      return;
    }
    const auto activation_name = lnast_->get_name(kids[lnast_rolled_for::activation]);
    const auto next_name       = lnast_->get_name(kids[lnast_rolled_for::next_active]);
    if (!activation_name.empty()) {
      desc.activation_input = input_pid(activation_name);
      if (!desc.activation_input) {
        error_here("upass.tolg: rolled_for activation port '{}' is not a callee input", activation_name);
        return;
      }
    }
    if (!next_name.empty()) {
      desc.next_active_output = output_pid(next_name);
      if (!desc.next_active_output || !desc.activation_input) {
        error_here("upass.tolg: rolled_for next-active port '{}' is invalid", next_name);
        return;
      }
    }
    // hhds enforces distinct role inputs by THROWING (std::invalid_argument,
    // "set_subnode(loop): role inputs must be distinct"). plan_loop_roll declines
    // any loop whose source names collide with the reserved port names, so this
    // is unreachable from source — but reaching hhds with two roles on one pid
    // would abort the compiler instead of pointing at the offending loop.
    if (desc.activation_input && desc.index_input && *desc.activation_input == *desc.index_input) {
      error_here("upass.tolg: rolled_for index and activation resolve to the same callee input port '{}'", activation_name);
      return;
    }
    sub.set_subnode(gio, desc);
    for (auto map : lnast_->children(kids[lnast_rolled_for::carries])) {
      if (!Lnast_ntype::is_store(lnast_->get_type(map))) {
        error_here("upass.tolg: malformed rolled_for carry entry");
        return;
      }
      auto in_n  = lnast_->get_first_child(map);
      auto out_n = in_n.is_invalid() ? in_n : lnast_->get_sibling_next(in_n);
      auto ip    = in_n.is_invalid() ? std::optional<hhds::Port_id>{} : input_pid(lnast_->get_name(in_n));
      auto op    = out_n.is_invalid() ? std::optional<hhds::Port_id>{} : output_pid(lnast_->get_name(out_n));
      if (!ip || !op) {
        error_here("upass.tolg: rolled_for carry ports do not exist on the callee");
        return;
      }
      sub.create_driver_pin(*op).connect_sink(sub.create_sink_pin(*ip));
    }
  }

  // How a diagnostic names the module `graph_name` (`file.entity`): a template
  // specialization (`ileaf__u1_u4_W_4_h…`) by its template, as the source does.
  [[nodiscard]] std::string_view source_module_name(std::string_view graph_name) const {
    const auto entity = graph_name.rfind('.') + 1;  // npos + 1 == 0: no file prefix
    const auto sep    = graph_name.find("__", entity);
    if (sep != std::string_view::npos && registry_ != nullptr && std::ranges::any_of(*registry_, [&](const auto& ln) {
          return ln && ln->is_template() && ln->get_top_module_name() == graph_name.substr(0, sep);
        })) {
      return graph_name.substr(entity, sep - entity);
    }
    return graph_name.substr(entity);
  }

  // The unit that lowers to graph `g` (a Sub's callee), or nullptr (an lg:
  // black box).
  [[nodiscard]] const Lnast* unit_of(const hhds::Graph& g) const {
    auto&             units = lowering_pass().units;
    const std::string name{g.get_name()};
    if (const auto it = units.find(name); it != units.end()) {
      return it->second;
    }
    const Lnast* found = nullptr;  // a unit the pass did not index up front
    if (registry_ != nullptr) {
      for (const auto& ln : *registry_) {
        if (ln && !ln->is_template() && ln->get_graph_name() == name) {
          found = ln.get();
          break;
        }
      }
    }
    units.emplace(name, found);
    return found;
  }

  // docs 07-typesystem "Clock and Reset": `Clock` and `Reset` are distinct
  // types, and a Clock is not data. The class of a driver pin: a `Clock` or
  // `Reset` input of this unit, a clock gate's output (a gated Clock), a
  // Pyrope child's output by its declared type, an untyped or `Clock` wire by
  // its driver, or none (data -- a Bool/U1 value, a computed expression, a
  // wire of another type). An output of a child with no Pyrope types
  // (imported Verilog, an lg: black box) and a wire not yet driven are left
  // unclassified (nullopt).
  [[nodiscard]] std::optional<Io_sig> pin_signal_class(const Pin& d, int depth = 0) const {
    namespace gu = livehd::graph_util;
    if (d.is_invalid() || d.is_const() || depth > kClockHops) {
      return std::nullopt;
    }
    if (gu::is_graph_input_pin(d)) {
      const auto name = d.get_pin_name();
      // Implicit ports are added to GraphIO, not the source IO metadata.
      if (clock_minted_ && name == clock_name_) {
        return Io_sig::clock;
      }
      if (reset_minted_ && name == reset_name_) {
        return Io_sig::reset;
      }
      const auto* e = lnast_->io_meta().find(name);
      return e == nullptr ? Io_sig::none : e->sig;
    }
    const auto n = d.get_master_node();
    if (const auto it = wire_cells_.find(n.get_debug_nid()); it != wire_cells_.end()) {
      return it->second ? std::optional<Io_sig>(Io_sig::none) : pin_signal_class(gu::first_value_driver(n), depth + 1);
    }
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Clock_cell) {
      return Io_sig::clock;
    }
    if (op == Ntype_op::Sub) {
      const auto  child = n.get_subnode_graph();
      const auto* unit  = child ? unit_of(*child) : nullptr;
      if (unit == nullptr || unit->is_verilog_origin()) {
        return std::nullopt;
      }
      const auto* e = unit->io_meta().find(d.get_pin_name());
      return e == nullptr ? std::nullopt : std::optional<Io_sig>(e->sig);
    }
    return Io_sig::none;
  }

  // docs 04b "Implicit clock and reset" (ruling 55): a clock pin names a
  // Clock -- a `Clock` input, a gated `Clock(clock_pin=, enable=)` (a
  // Clock_cell) or a child's `Clock` output -- never a `Reset` or data: there
  // are no derived clocks. Imported Verilog has no Clock type. `what` names
  // the state element ("register `r`"); false once reported.
  bool check_clock_pin_class(const Lnast_nid& at, std::string_view what, std::string_view pin_txt, const Pin& cp) {
    if (lnast_->is_verilog_origin()) {
      return true;
    }
    const auto cls = pin_signal_class(cp);
    if (!cls || *cls == Io_sig::clock) {
      return true;
    }
    const std::string_view is
        = *cls == Io_sig::reset ? "a `Reset`, not a Clock" : "data, not a Clock (there are no derived clocks)";
    error_hint_at(at,
                  {"clock-bind-not-clock", "type"},
                  pin_txt.empty() || Lnast::is_tmp(pin_txt)
                      ? std::format("the clock_pin of {} is {}", what, is)
                      : std::format("{} names `{}` as its clock_pin, but it is {}", what, pin_txt, is),
                  "name a `Clock` input in `clock_pin=`, or gate one with `Clock(clock_pin=clk, enable=en)`");
    return false;
  }

  // Rulings 42/80: a memory port clocked by a constant (a folded `clock_pin`,
  // a Verilog `posedge` on a tied-off or unconnected wire, a gate a constant
  // holds off) never takes an edge in the emitted Verilog while `lhd sim`
  // steps it. A constant reaching the clock through an instance input is
  // caught at the call (check_const_clock_bind). `at` locates the report.
  void check_mem_const_clock(const hhds::Node_class& mem, const Lnast_nid& at, std::string_view name) {
    const auto clock_pid = static_cast<int>(Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin"));
    for (auto sink : mem.inp_sorted_pins()) {  // read-only walk
      if (static_cast<int>(sink.get_port_id()) % kMemPortStride == clock_pid
          && clock_const_of(sink.get_driver_pin(), {}, kClockHops) && !mem_clock_users(mem, sink.get_port_id()).empty()) {
        error_hint_at(at,
                      {"clock-const", "time"},
                      std::format("{} is clocked by a constant",
                                  clocked_state_label("memory", name, mem_clock_port(mem, sink.get_port_id()))),
                      lnast_->is_verilog_origin() ? "a constant never ticks: clock it by a clock signal"
                                                  : "a constant never ticks: name a `Clock` in `clock_pin=`, or drop "
                                                    "`clock_pin` for the implicit clock");
      }
    }
  }

  // The clocks of a memory no `reg` declares (a `__memory(cfg)` cell): the
  // same rules as a `reg` memory's `clock_pin` -- never a constant, a `Reset`
  // or data -- checked once the body is wired.
  void check_builtin_memory_clocks() {
    namespace gu = livehd::graph_util;
    absl::flat_hash_set<uint64_t> declared;
    for (const auto& [name, mi] : mem_map_) {
      declared.insert(mi.node.get_debug_nid());
    }
    const auto clock_pid = static_cast<int>(Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin"));
    for (auto n : g_->body().nodes()) {
      if (gu::type_op_of(n) != Ntype_op::Memory || declared.contains(n.get_debug_nid())) {
        continue;
      }
      if (auto ref = n.attr(hhds::attrs::srcid); ref.has()) {
        cur_srcid_ = ref.get();
      }
      const auto name = gu::node_name_of(n);
      check_mem_const_clock(n, Lnast_nid{}, name);
      for (auto sink : n.inp_sorted_pins()) {  // read-only walk
        if (static_cast<int>(sink.get_port_id()) % kMemPortStride == clock_pid) {
          const auto cp = sink.get_driver_pin();
          check_clock_pin_class(Lnast_nid{},
                                clocked_state_label("memory", name, mem_clock_port(n, sink.get_port_id())),
                                gu::is_graph_input_pin(cp) ? cp.get_pin_name() : std::string_view{},
                                cp);
        }
      }
    }
  }

  // A call binding respects the Clock/Reset types (a Pyrope caller and a
  // Pyrope callee; Verilog has no such types): a child `Clock` input takes a
  // Clock (never data: there are no derived clocks, gate with a Clock_cell or
  // an enable), a Clock binds to nothing but a `Clock` input, and a `Reset`
  // never feeds a `Clock` input. A Bool binds to a `Reset` input (Bool-like).
  void check_clock_reset_bind(const Lnast_nid& nid, const Lnast_tree_io& cio, std::string_view pname, const Pin& actual,
                              const Lnast* callee, std::string_view callee_bare) {
    if (lnast_->is_verilog_origin() || callee == nullptr || callee->is_verilog_origin()) {
      return;
    }
    const auto*    ce = cio.find(pname);
    Lnast_io_entry minted;  // the child's minted `clock`/`reset`, bound by name
    if (ce == nullptr && (pname == "clock" || pname == "reset")) {
      minted.name = std::string(pname);
      minted.sig  = pname == "clock" ? Io_sig::clock : Io_sig::reset;
      ce          = &minted;
    }
    if (ce == nullptr) {
      return;
    }
    const auto cls = pin_signal_class(actual);
    if (!cls) {
      return;
    }
    const auto what = [&]() -> std::string {
      if (livehd::graph_util::is_graph_input_pin(actual)) {
        return std::format("`{}`", actual.get_pin_name());
      }
      return "the value";
    };
    if (ce->sig == Io_sig::clock && *cls != Io_sig::clock) {
      error_hint_at(
          nid,
          {"clock-bind-not-clock", "type"},
          std::format("`Clock` input `{}` of `{}` is bound to {}, which is {}",
                      pname,
                      callee_bare,
                      what(),
                      *cls == Io_sig::reset ? "a `Reset`, not a Clock" : "data, not a Clock (there are no derived clocks)"),
          "bind a `Clock` (a clock input of this module); gate a clock with an enable instead of logic");
    }
    // A data input the callee never reads as data uses nothing as data (a
    // blackbox's unused `clk`, a Verilog cell's `ca` that only names a
    // `clock_pin`): only a data READ makes a Clock data.
    if (ce->sig != Io_sig::clock && *cls == Io_sig::clock && upass::io_port::body_reads_input_as_data(*callee, ce->name)) {
      error_hint_at(
          nid,
          {"clock-as-data", "type"},
          std::format("{} is a `Clock` bound to input `{}` of `{}`, which is {}",
                      what(),
                      pname,
                      callee_bare,
                      ce->sig == Io_sig::reset ? "a `Reset`: a Clock is never a reset" : "not a `Clock`: a Clock is not data"),
          "a Clock only drives register clock pins (`clock_pin=clk`) or a child's `Clock` input");
    }
  }

  // docs 07-typesystem "Clock and Reset" over the lowered body of a Pyrope
  // unit. Every edge out of a `Clock` input must reach a clock sink: a flop or
  // memory clock pin, a clock gate's reference, an instance input (typed at
  // the call by check_clock_reset_bind) or a `Clock` output. Any other reader
  // uses the clock as DATA -- legal only when its whole cone ends in
  // properties (`assert(clk < 1000)`, the debug cycle-count view). A `Clock`
  // output takes a Clock (never a constant, never data), and a register's
  // clock pin never names a `Reset`.
  void check_clock_reset_flow() {
    namespace gu = livehd::graph_util;
    if (lnast_->is_verilog_origin()) {
      return;
    }
    const auto& io        = lnast_->io_meta();
    const auto  clock_pid = [](Ntype_op op) { return static_cast<hhds::Port_id>(Ntype::get_sink_pid(op, "clock_pin")); };
    const auto  fail_at   = [&](const hhds::Node_class& n, livehd::diag::Id id, std::string msg, std::string hint) {
      if (!n.is_invalid()) {
        if (auto ref = n.attr(hhds::attrs::srcid); ref.has()) {
          cur_srcid_ = ref.get();
        }
      }
      error_hint_at(Lnast_nid{}, id, std::move(msg), std::move(hint));
    };
    // The declaration of output `name` (its io store), to locate an output
    // diagnostic.
    const auto output_decl = [&](std::string_view name) -> Lnast_nid {
      const auto io_n = lnast_->get_first_child(lnast_->get_root());
      if (io_n.is_invalid() || !Lnast_ntype::is_io(lnast_->get_type(io_n))) {
        return Lnast_nid{};
      }
      const auto ins  = lnast_->get_first_child(io_n);
      const auto outs = ins.is_invalid() ? ins : lnast_->get_sibling_next(ins);
      for (auto st = outs.is_invalid() ? outs : lnast_->get_first_child(outs); !st.is_invalid();
           st      = lnast_->get_sibling_next(st)) {
        if (const auto nm = lnast_->get_first_child(st); !nm.is_invalid() && canon_io_name(lnast_->get_name(nm)) == name) {
          return st;
        }
      }
      return Lnast_nid{};
    };
    // Whether every value leaving comb node `n` ends in a property marker.
    absl::flat_hash_map<uint64_t, bool>               debug_memo;
    std::function<bool(const hhds::Node_class&, int)> debug_cone = [&](const hhds::Node_class& n, int depth) -> bool {
      if (gu::is_property_marker(n)) {
        return true;
      }
      const auto op = gu::type_op_of(n);
      if (depth > 32 || op == Ntype_op::Sub || op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch
          || op == Ntype_op::Memory || op == Ntype_op::Clock_cell) {
        return false;
      }
      if (const auto it = debug_memo.find(n.get_debug_nid()); it != debug_memo.end()) {
        return it->second;
      }
      debug_memo[n.get_debug_nid()] = false;  // a cycle is not a debug cone
      bool any                      = false;
      for (auto dp : n.out_sorted_pins()) {
        for (const auto& e : dp.out_edges()) {
          any = true;
          if (gu::is_graph_output_pin(e.sink) || !debug_cone(e.sink.get_master_node(), depth + 1)) {
            return false;
          }
        }
      }
      debug_memo[n.get_debug_nid()] = any;
      return any;
    };
    // The readers of one Clock: a `Clock` input, a gated `Clock(...)` (a
    // Clock_cell) or a child's `Clock` output. An untyped or `Clock` wire alias
    // (its passthrough buffer and typed read) carries the Clock on; a wire of
    // another type stores it as data.
    const auto check_readers = [&](const Pin& clock, const std::string& who) {
      std::vector<std::pair<hhds::Pin_class, bool>> clock_sinks;  // (sink, reached through a wire)
      std::vector<std::pair<hhds::Pin_class, bool>> clock_nets{
          {clock, false}
      };
      absl::flat_hash_set<hhds::Pin_class> seen_nets;
      while (!clock_nets.empty()) {
        const auto [net, via_wire] = clock_nets.back();
        clock_nets.pop_back();
        if (!seen_nets.insert(net).second) {
          continue;
        }
        for (const auto& oe : net.out_edges()) {
          const auto cell
              = gu::is_graph_output_pin(oe.sink) ? wire_cells_.end() : wire_cells_.find(oe.sink.get_master_node().get_debug_nid());
          if (cell != wire_cells_.end() && !cell->second) {
            clock_nets.emplace_back(oe.sink.get_master_node().get_driver_pin(0), true);
          } else {
            clock_sinks.emplace_back(oe.sink, via_wire);
          }
        }
      }
      for (const auto& [sink, via_wire] : clock_sinks) {
        if (gu::is_graph_output_pin(sink)) {
          const auto* out = io.find(sink.get_pin_name());
          if (out == nullptr || out->sig != Io_sig::clock) {
            error_hint_at(output_decl(sink.get_pin_name()),
                          {"clock-as-data", "type"},
                          std::format("{} is a `Clock` driving output `{}`, which is not a `Clock`: a Clock is not data",
                                      who,
                                      sink.get_pin_name()),
                          "declare the output `:Clock` to pass the clock through");
          }
          continue;
        }
        if (latch_gate_sinks_.contains(sink)) {
          continue;
        }
        const auto n   = sink.get_master_node();
        const auto op  = gu::type_op_of(n);
        const auto pid = sink.get_port_id();
        if ((op == Ntype_op::Flop || op == Ntype_op::Fflop) && pid == clock_pid(op)) {
          continue;
        }
        if (op == Ntype_op::Memory && pid % Ntype::Memory_port_stride == clock_pid(op)) {
          continue;
        }
        if (op == Ntype_op::Clock_cell && pid == Ntype::get_sink_pid(op, "clk_ref")) {
          continue;
        }
        if (op == Ntype_op::Sub) {
          // An instance input is typed at the call (check_clock_reset_bind),
          // which sees a wire's buffer before the wire is driven: check a Clock
          // reaching one through a wire here.
          const auto  child = n.get_subnode_graph();
          const auto* unit  = via_wire && child ? unit_of(*child) : nullptr;
          const auto* port  = unit == nullptr || unit->is_verilog_origin() ? nullptr : unit->io_meta().find(sink.get_pin_name());
          if (port != nullptr && port->sig != Io_sig::clock && upass::io_port::body_reads_input_as_data(*unit, port->name)) {
            fail_at(n,
                    {"clock-as-data", "type"},
                    std::format(
                        "{} is a `Clock` bound to input `{}` of `{}`, which is {}",
                        who,
                        sink.get_pin_name(),
                        source_module_name(child->get_name()),
                        port->sig == Io_sig::reset ? "a `Reset`: a Clock is never a reset" : "not a `Clock`: a Clock is not data"),
                    "a Clock only drives register clock pins (`clock_pin=clk`) or a child's `Clock` input");
          }
          continue;
        }
        if (debug_cone(n, 0)) {
          continue;  // a debug read
        }
        const bool as_reset = (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch)
                              && pid == Ntype::get_sink_pid(op, "reset_pin");
        fail_at(n,
                {"clock-as-data", "type"},
                as_reset ? std::format("{} is a `Clock` used as a register reset: a Clock is never a reset", who)
                         : std::format("{} is a `Clock`, and a Clock is not data (it feeds logic)", who),
                "a Clock only drives register clock pins (`clock_pin=clk`) or a child's `Clock` input; use an enable for "
                "clock-dependent logic (the cycle count is readable only in `assert`/`puts`/test blocks)");
      }
    };
    for (const auto& e : io.inputs) {
      if (e.sig != Io_sig::clock) {
        continue;
      }
      const std::string cname{canon_io_name(e.name)};
      if (g_->get_io()->has_input(cname)) {
        check_readers(g_->get_input_pin(cname), std::format("`{}`", cname));
      }
    }
    for (auto n : g_->body().nodes()) {
      const auto op = gu::type_op_of(n);
      if (op == Ntype_op::Clock_cell) {
        // A gate's clock is a Clock (checked again here: a wire bound after the
        // `Clock(...)` call was not classified there).
        const auto ref = gu::get_driver_of_sink_name(n, "clk_ref");
        if (const auto cls = pin_signal_class(ref); cls && *cls != Io_sig::clock) {
          fail_at(n,
                  {"clock-bind-not-clock", "type"},
                  std::format("the clock_pin of the clock gate `Clock(...)` is {}",
                              *cls == Io_sig::reset ? "a `Reset`, not a Clock" : "data, not a Clock (there are no derived clocks)"),
                  "name a `Clock` input in `clock_pin=`, or gate one with `Clock(clock_pin=clk, enable=en)`");
        }
        const auto who = gu::is_graph_input_pin(ref) ? std::format("the gated clock `Clock(clock_pin={}, ...)`", ref.get_pin_name())
                                                     : std::string("the gated clock `Clock(...)`");
        check_readers(n.get_driver_pin(0), who);
      } else if (op == Ntype_op::Sub) {
        for (auto dp : n.out_sorted_pins()) {
          if (pin_signal_class(dp) == Io_sig::clock) {
            const auto child = n.get_subnode_graph();
            check_readers(dp,
                          std::format("`Clock` output `{}` of `{}`",
                                      dp.get_pin_name(),
                                      child ? source_module_name(child->get_name()) : std::string_view{"?"}));
          }
        }
      }
    }
    // A `Clock` output passes a Clock through: never a constant, never data.
    for (const auto& e : io.outputs) {
      if (e.sig != Io_sig::clock) {
        continue;
      }
      const std::string oname{canon_io_name(e.name)};
      const auto        sink = g_->get_output_pin(oname);
      const auto        drv  = sink.is_invalid() ? Pin{} : sink.get_driver_pin();
      if (drv.is_invalid()) {
        continue;
      }
      const auto cls = pin_signal_class(drv);
      if (drv.is_const() || (cls && *cls != Io_sig::clock)) {
        error_hint_at(output_decl(oname),
                      {drv.is_const() ? "clock-const-bind" : "clock-bind-not-clock", "type"},
                      drv.is_const()
                          ? std::format("`Clock` output `{}` is driven by a constant: a Clock is never a constant",
                                        std::string(canon_io_name(e.name)))
                          : std::format("`Clock` output `{}` is driven by {}, not by a Clock (there are no derived clocks)",
                                        std::string(canon_io_name(e.name)),
                                        *cls == Io_sig::reset ? "a `Reset`" : "data"),
                      "drive a `Clock` output from a `Clock` input");
      }
    }
  }

  // Ruling 81 (qa Q26): Verilog clock idioms map what fits and error the
  // rest. Imported Verilog has no Clock type, so its state is clocked by any
  // net; what LiveHD maps is a clock input (through the typed-read and
  // boolean-shaping wrappers, control_root), a gate on one (the ICG `clk & en`
  // with exactly one clock operand, clock_op_of; the active-low ICG
  // `clk | ~en_latch`), a Clock_cell and an instance output (a gate cell or
  // clock buffer, checked in its own body). An inverted clock (`posedge ~clk`;
  // write `negedge clk`), a register or latch output (a divided clock), a clock
  // mux and any other logic on a clock -- data merged into it, two clocks
  // combined -- are derived clocks: not supported. So is a derived clock passed
  // to an instance's clock input (a port that clocks state in its body), but
  // for a plain inversion there: a gate-level negedge flop.
  void check_verilog_clock_idioms() {
    namespace gu = livehd::graph_util;
    namespace lc = livehd::latch_contract;
    if (!lnast_->is_verilog_origin()) {
      return;
    }
    constexpr std::string_view       kInverted = "an inverted clock";
    std::optional<lc::Design_clocks> clocks;  // built on the first gate
    const auto                       io_clock = [&](const Pin& p) {
      const auto* e = lnast_->io_meta().find(p.get_pin_name());
      return e != nullptr && e->sig == Io_sig::clock;
    };
    // A gate operand that is a clock: an input clocking state (Design_clocks,
    // by use or by its conventional name), a Clock_cell or an instance output.
    const auto clock_operand = [&](const Pin& drv) {
      const auto root = lc::control_root(drv, /*stop_at_clock_cell=*/true);
      if (root.inverted || root.net.is_invalid() || root.net.is_const()) {
        return false;
      }
      if (gu::is_graph_input_pin(root.net)) {
        if (!clocks) {
          clocks.emplace(g_);
        }
        return clocks->is_clock(root.net) || io_clock(root.net);
      }
      const auto op = gu::type_op_of(root.net.get_master_node());
      return op == Ntype_op::Clock_cell || op == Ntype_op::Sub;
    };
    std::function<std::string(const Pin&, int)> derived = [&](const Pin& drv, int depth) -> std::string {
      if (drv.is_invalid() || drv.is_const() || depth > kClockHops || clock_const_of(drv, {}, kClockHops)) {
        return {};  // unclocked, or a constant clock (reported as clock-const)
      }
      const auto root = lc::control_root(drv, /*stop_at_clock_cell=*/true);
      if (root.net.is_invalid() || root.net.is_const()) {
        return {};
      }
      const auto op = gu::is_graph_input_pin(root.net) ? Ntype_op::Invalid : gu::type_op_of(root.net.get_master_node());
      if ((op == Ntype_op::Or || op == Ntype_op::Xor) && !root.inverted) {
        const auto ins = gu::inp_sink_drivers(root.net.get_master_node());
        // A zero pad (`$signed(2'b0) | $signed(clk)`, cgen's width spelling) is
        // the identity: continue through its one live operand.
        Pin        live;
        bool       pad = true;
        for (const auto& in : ins) {
          if (in.driver.is_const()) {
            pad = pad && gu::const_of(in.driver).is_known_zero();
          } else {
            pad  = pad && live.is_invalid();
            live = in.driver;
          }
        }
        if (pad && !live.is_invalid()) {
          return derived(live, depth + 1);
        }
        // The active-low ICG flavour `clk | ~en_latch` (the gated event is the
        // fall; a Clock_cell with `invert`): one clock operand, the other the
        // inverted output of the enable latch.
        const auto inverted_latch = [](const Pin& p) {
          const auto r = lc::control_root(p, /*stop_at_clock_cell=*/true);
          return r.inverted && !r.net.is_invalid() && !r.net.is_const() && !gu::is_graph_input_pin(r.net)
                 && gu::type_op_of(r.net.get_master_node()) == Ntype_op::Latch;
        };
        if (op == Ntype_op::Or && ins.size() == 2) {
          for (size_t i = 0; i < 2; ++i) {
            if (inverted_latch(ins[1 - i].driver) && clock_operand(ins[i].driver) && derived(ins[i].driver, depth + 1).empty()) {
              return {};
            }
          }
        }
      }
      if (op == Ntype_op::Invalid || op == Ntype_op::Clock_cell || op == Ntype_op::Sub) {
        return root.inverted ? std::string(kInverted) : "";
      }
      if (op == Ntype_op::And) {
        if (!clocks) {
          clocks.emplace(g_);
        }
        if (const auto cone = lc::clock_op_of(drv, *clocks)) {
          return cone->clock_inverted ? "an inverted gated clock" : "";
        }
        if (root.inverted) {
          return std::string(kInverted);
        }
        // A gate clock_op_of cannot place (the design's clocks are named by
        // use): an ICG only with exactly one clock operand.
        const auto ins     = gu::inp_sink_drivers(root.net.get_master_node());
        const auto n_clock = std::ranges::count_if(ins, [&](const auto& in) { return clock_operand(in.driver); });
        return n_clock == 1 ? "" : n_clock == 0 ? "an `and` of data (no clock operand)" : "an `and` of two clocks";
      }
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
        return std::format("a {} output (a divided clock)", op == Ntype_op::Latch ? "latch" : "register");
      }
      if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
        return "a clock mux";
      }
      const auto name = Ntype::get_name(op);
      return std::format("{} `{}` of a clock", std::string_view{"aeiou"}.contains(name.front()) ? "an" : "a", name);
    };
    const auto report = [&](const hhds::Node_class& n, std::string what, std::string_view how) {
      if (auto ref = n.attr(hhds::attrs::srcid); ref.has()) {
        cur_srcid_ = ref.get();
      }
      error_hint_at(Lnast_nid{},
                    {"derived-clock-unsupported", "time"},
                    std::format("{} is clocked by {}: derived, inverted and muxed clocks are not supported", what, how),
                    "clock it by a clock input (`negedge clk` for the falling edge), gate it with an ICG (`clk & en`), or "
                    "use an enable");
    };
    const auto clock_pid = static_cast<int>(Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin"));
    for (auto n : g_->body().nodes()) {
      const auto op = gu::type_op_of(n);
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
        if (const auto how = derived(gu::get_driver_of_sink_name(n, "clock_pin"), 0); !how.empty()) {
          report(n, clocked_state_label("register", gu::node_name_of(n), -1), how);
        }
      } else if (op == Ntype_op::Memory) {
        for (auto sink : n.inp_sorted_pins()) {  // read-only walk
          if (static_cast<int>(sink.get_port_id()) % kMemPortStride != clock_pid) {
            continue;
          }
          if (const auto how = derived(sink.get_driver_pin(), 0); !how.empty() && !mem_clock_users(n, sink.get_port_id()).empty()) {
            report(n, clocked_state_label("memory", gu::node_name_of(n), mem_clock_port(n, sink.get_port_id())), how);
          }
        }
      } else if (op == Ntype_op::Sub) {
        const auto  child = n.get_subnode_graph();
        const auto* unit  = child ? unit_of(*child) : nullptr;
        if (unit == nullptr) {
          continue;
        }
        for (auto sink : n.inp_sorted_pins()) {  // read-only walk
          if (!is_clock_input(unit->io_meta(), sink.get_pin_name())) {
            continue;
          }
          // A gate-level netlist builds a negedge flop from an inverter and a
          // posedge cell (`dff u(.CLK(~clk))`): the one derived clock accepted
          // at a cell's clock port.
          if (const auto how = derived(sink.get_driver_pin(), 0); !how.empty() && how != kInverted) {
            report(n, std::format("clock input `{}` of instance `{}`", sink.get_pin_name(), gu::node_name_of(n)), how);
          }
        }
      }
    }
  }

  // User ruling 2026-09-28 (27): a constant bound to callee input `pname` is a
  // compile error only when it leaves a register clocked by a constant -- in
  // the callee, or in an instance below it (the implicit-clock auto-wire
  // included): the emitted Verilog never ticks it (what LEC and Verilator see)
  // while `lhd sim` steps every clock port. A clk/clock-named input that clocks
  // nothing is plain data, and a body-less lg: black box proves nothing either
  // way. `from_default`: the call omits `pname`, which takes its declared
  // default. `bound` maps every input of the call to its resolved driver
  // (actual, positional or named, or declared default).
  void check_const_clock_bind(const Lnast_nid& nid, hhds::GraphIO& gio, std::string_view pname, const Pin& value,
                              const Lnast* callee_ln, const absl::flat_hash_map<std::string, Pin>& bound,
                              bool from_default = false) {
    // docs 04b "Implicit clock and reset": binding a constant to a `Clock`
    // input is ALWAYS a compile error, whether or not a register reads it (a
    // `Bool` input is data whatever its name). A Verilog-origin callee's
    // clock is recognized by name, so it keeps the register-reach rule below.
    if (callee_ln != nullptr && !callee_ln->is_verilog_origin() && is_clock_input(callee_ln->io_meta(), pname)) {
      const auto callee_nm = callee_ln->get_top_module_name();
      error_hint_at(nid,
                    {"clock-const-bind", "time"},
                    std::format("`Clock` input `{}` of `{}` is {} a constant: a Clock is never bound to a constant",
                                pname,
                                callee_nm.substr(callee_nm.rfind('.') + 1),
                                from_default ? "omitted and defaults to" : "bound to"),
                    from_default ? std::format("drop the constant default of `{}` so an omitted `{}` is wired to the caller's "
                                               "clock, or pass a clock signal",
                                               pname,
                                               pname)
                                 : std::format("omit `{}` to wire the caller's implicit clock, or pass a `Clock` signal", pname));
      return;
    }
    const auto child = gio.has_graph() ? gio.get_graph() : nullptr;
    if (!child || lowering_pass().pending.contains(child->get_name())) {
      return;
    }
    // Rulings 41/42/80: an unconnected clock (`.clk()` reads `0ub?`) is a
    // constant too, and a memory port's clock is checked like a register's.
    const auto& cval     = livehd::graph_util::const_of(value);
    const auto& reach    = const_clock_reach(child, pname, cval);
    // A port of the callee's own memory whose enable this call holds at 0
    // never writes: its clock is irrelevant (an idle ROM write port). Every
    // other port on that clock must be idle too.
    const auto  held_off = [&](std::string_view enable_input) {
      const auto it = bound.find(enable_input);
      return it != bound.end() && it->second.is_const() && livehd::graph_util::const_of(it->second).is_known_zero();
    };
    const Clocked_state* state = reach.state ? &*reach.state : nullptr;
    for (const auto& port : reach.memory_ports) {
      if (state != nullptr) {
        break;
      }
      if (port.enable_input.empty() || port.owner != child->get_name() || !held_off(port.enable_input)) {
        state = &port;
      }
    }
    if (state == nullptr) {
      return;
    }
    const std::string               what  = clocked_state_label(state->memory ? "memory" : "register", state->name, state->port);
    const auto                      owner = source_module_name(state->owner);
    std::vector<livehd::diag::Note> notes;
    if (!state->span.is_null()) {
      notes.push_back({std::format("{} of `{}` is declared here", what, owner), state->span});
    }
    // A clock input (by type in Pyrope, by use in Verilog; an lg: black box
    // has no types) clocks the state; any other input reaches it through a
    // gate enable the constant holds off.
    const bool  is_clock    = callee_ln == nullptr || is_clock_input(callee_ln->io_meta(), pname);
    const bool  auto_wired  = callee_ln != nullptr && is_clock && !lnast_->is_verilog_origin();
    const bool  unconnected = !from_default && cval.has_unknowns();  // `.clk()`, an omitted Verilog port, an `x`
    const auto  how         = from_default  ? "omitted and defaults to a constant"
                              : unconnected ? "unconnected (or `x`)"
                                            : "bound to a constant";
    std::string hint;
    if (!is_clock) {
      hint = std::format("pass a non-constant enable to `{}`: a clock gate held off never ticks", pname);
    } else if (from_default && auto_wired) {
      hint = std::format("pass a clock signal to `{}`, or drop its constant default so an omitted `{}` is wired", pname, pname);
    } else if (auto_wired) {
      hint = std::format("omit `{}` to wire the caller's implicit clock, or pass a clock signal", pname);
    } else if (unconnected) {
      hint = std::format("connect a clock signal to `{}`: an unconnected clock never ticks", pname);
    } else {
      hint = std::format("pass a clock signal to `{}`: a constant never ticks", pname);
    }
    error_hint_at(nid,
                  {"clock-const-bind", "time"},
                  is_clock ? std::format("clock input `{}` of `{}` is {}, but it clocks {} of `{}`",
                                         pname,
                                         source_module_name(child->get_name()),
                                         how,
                                         what,
                                         owner)
                           : std::format("input `{}` of `{}` is {}, which holds the clock gate of {} of `{}` off",
                                         pname,
                                         source_module_name(child->get_name()),
                                         how,
                                         what,
                                         owner),
                  std::move(hint),
                  std::move(notes));
  }

  // Ruling 56 (docs 07-typesystem "Clock and Reset"): `Clock(clock_pin=clk,
  // enable=en)` gates a clock -- the only derived Clock -- as a Clock_cell (an
  // ICG). prp2lnast validated the two named arguments. The clock is a Clock
  // (never a constant, never data) and the result is a Clock (a Clock_cell
  // output, pin_signal_class).
  void lower_clock_gate(const Lnast_nid& nid, const Lnast_nid& callee_n) {
    const auto       dst = lnast_->get_first_child(nid);
    Pin              clk;
    Pin              en;
    int32_t          en_mw = 0;
    std::string_view clk_txt;
    bool             invert = false;
    for (auto a = lnast_->get_sibling_next(callee_n); !a.is_invalid(); a = lnast_->get_sibling_next(a)) {
      const auto key = Lnast_ntype::is_store(lnast_->get_type(a)) ? lnast_->get_first_child(a) : Lnast_nid{};
      const auto val = key.is_invalid() ? key : lnast_->get_sibling_next(key);
      if (val.is_invalid()) {
        continue;
      }
      if (lnast_->get_name(key) == "clock_pin") {
        clk     = leaf(val).pin;
        clk_txt = lnast_->get_name(val);
      } else if (lnast_->get_name(key) == "enable") {
        const auto v = leaf(val);
        en           = v.pin;
        en_mw        = v.mw;
      } else if (lnast_->get_name(key) == "invert") {
        invert = lnast_->get_name(val) == "true";  // prp2lnast: a literal true/false
      }
    }
    if (clk.is_invalid() || en.is_invalid()) {
      error_here("upass.tolg: `Clock(...)` needs a `clock_pin` and an `enable`");
      return;
    }
    if (en_mw > 1) {  // a condition is a Bool (no integer truthiness)
      error_hint_at(nid,
                    {"clock-gate-args", "type"},
                    std::format("the `enable` of `Clock(...)` is a {}-bit value, not a `Bool`", en_mw),
                    "pass a `Bool` enable, e.g. `enable=(en != 0)`");
    }
    if (clock_const_of(clk, {}, kClockHops)) {
      error_hint_at(nid,
                    {"clock-const", "time"},
                    "the clock gate `Clock(clock_pin=..., enable=...)` is clocked by a constant: a Clock is never a constant",
                    "name a `Clock` input in `clock_pin=`");
    } else if (!check_clock_pin_class(nid, "the clock gate `Clock(...)`", clk_txt, clk)) {
      return;
    }
    bind_result(lnast_->get_name(dst), clock_gate(clk, en, invert), 1);
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

    if (try_lower_gate_builtin(nid, callee_name)) {
      return;
    }

    if (callee_name == "Clock" && Lnast_ntype::is_ref(lnast_->get_type(callee_n))) {
      lower_clock_gate(nid, callee_n);
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

    // An import-bound pipe/mod callee arrives as a string Dlop, so constprop
    // renders it QUOTED (`'unit.entity'` / `'lg:foo'`) when it folds the call's
    // callee ref to its value — unlike a by-name (same-file) callee, which
    // stays an unquoted ref (`file.entity`). A `comb` is unquoted-ref-resolved
    // and inlined by the runner before tolg, but pipe/mod calls reach here as
    // the folded const, so strip the surrounding quotes once so the registry
    // lookup, lg: detection, Sub instance name, and diagnostics all see the
    // bare callee name (mirrors the comb inliner's lambda-ref unquoting).
    if (callee_name.size() >= 2 && callee_name.front() == '\'' && callee_name.back() == '\'') {
      callee_name = callee_name.substr(1, callee_name.size() - 2);
    }

    // An `import("lg:foo")` binding folds the callee to the string
    // 'lg:foo'. Instantiate the foreign graph as a BLACK BOX: its GraphIO (the
    // kernel load_merge'd the lg: inputs into lib_) supplies the IO to wire by
    // name; cgen emits the instance by name and the body rides along in the
    // assembled library. There is no ln: lambda — synthesize the io_meta the
    // shared wiring below expects from the GraphIO's declared pins.
    std::string lg_name;
    if (callee_name.rfind("lg:", 0) == 0) {
      lg_name = callee_name.substr(3);
    }
    if (!lg_name.empty()) {
      gio = lib_ != nullptr ? lib_->find_io(lg_name) : nullptr;
      if (!gio) {
        error_here(
            "upass.tolg: imported lg: graph '{}' not found in any input "
            "library — pass it as an `lg:` "
            "input (or it failed to load)",
            lg_name);
        return;
      }
      auto kind_of_bits = [](uint32_t b) { return b == 1 ? Io_kind::boolean : Io_kind::integer; };
      // A body-less black box has no Clock/Reset types: class its 1-bit
      // clk/rst inputs by the Verilog reader's convention, as upass.ssa stamps
      // a Verilog-origin unit, so a conditional call still gates its clock.
      auto sig_of       = [](const auto& d) {
        if (d.bits != 1) {
          return Io_sig::none;
        }
        return verilog_clock_name(d.name) ? Io_sig::clock : (verilog_reset_name(d.name) ? Io_sig::reset : Io_sig::none);
      };
      for (const auto& d : gio->get_input_pin_decls()) {
        if ((d.name == "clock") || (d.name == "reset")) {
          continue;  // implicit; wired from the parent below, not an argument
        }
        cio_lg.inputs.push_back(Lnast_io_entry{.name      = d.name,
                                               .bits      = static_cast<int32_t>(d.bits),
                                               .is_signed = !d.unsign,
                                               .kind      = kind_of_bits(d.bits),
                                               .sig       = sig_of(d)});
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
        callee = resolve_callee_lnast(callee_name, *registry_, lnast_->get_top_module_name());
      }
      kind = callee ? callee->get_lambda_kind() : std::string_view{};
      // A `comb` callee normally inlines in the runner, but with
      // compile.upass.inline=false a fully-defined comb survives as a func_call
      // and lowers here to a Sub instance of its standalone module (same Sub
      // machinery as pipe/mod; a comb carries no clock/reset, so the minted-
      // clock/reset wiring below stays inert). An empty kind ("") is the lg:
      // black-box path handled above, never reached here.
      if (!callee || (kind != "pipe" && kind != "mod" && kind != "comb")) {
        // An unresolved call is ALWAYS a hard error: it is neither a defined
        // pipe/mod/comb, a built-in scalar cast (`signed`/`unsigned`/`uN`/`sN`/
        // `bool`/`string`), nor a `__cellop`. (`comb` may not call a
        // `pipe`/`mod`.)
        const std::string_view cn = callee_name;
        if (cn == "int" || cn == "uint" || cn == "integer") {
          // Tailored guidance for the removed `int`/`uint` cast.
          error_here(
              "the `{}(...)` cast was removed — use "
              "`Signed(x)`/`Unsigned(x)` to reinterpret a value's sign, "
              "or a sized cast `U<N>(x)`/`S<N>(x)`",
              callee_name);
        } else if (upass::classify_typecast(cn)) {
          // The runner lowers a built-in cast wherever its operand has ONE
          // declared scalar type; a cast still here had none to convert.
          error_here(
              "built-in cast `{}(...)` has no scalar operand to convert — the operand is a multi-output "
              "instance, a tuple value, or a signed instance output; cast one field or output (`x.o`) instead",
              callee_name);
        } else if (const auto tmpl
                   = (!callee && registry_ != nullptr)
                         ? resolve_callee_lnast(callee_name, *registry_, lnast_->get_top_module_name(), /*want_template=*/true)
                         : nullptr) {
          // The name DOES match a definition, but only its generic template:
          // the call reached lowering unspecialized. Saying "undefined" here
          // sent users hunting for a missing import that is present.
          error_here(
              "call to generic {} '{}' reached lowering unspecialized — upass never resolved this call site to "
              "a specialization (e.g. the callee is spelled through an import alias that did not resolve inside "
              "an inlined body)",
              tmpl->get_lambda_kind().empty() ? std::string_view{"function"} : tmpl->get_lambda_kind(),
              callee_name);
        } else {
          error_here(
              "call to undefined function '{}' — no such pipe/mod/comb "
              "or built-in cast",
              callee_name);
        }
        return;
      }

      // Only a `mod` may instantiate a pipe/mod callee (06-functions.md: `comb`
      // may not call a `pipe`/`mod`; pipe bodies use stage inference, not
      // instantiation). Without this gate a comb would silently grow a
      // latency-carrying instance. A `comb` callee is exempt: it is
      // combinational (latency-0, stateless), so any body — comb, mod, or a top
      // — may instantiate it (compile.upass.inline=false path).
      if (kind != "comb" && lnast_->get_lambda_kind() != "mod") {
        error_here(
            "upass.tolg: '{}' (a {}) calls the {} '{}' — only `mod` "
            "bodies may instantiate pipe/mod",
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
        error_here(
            "upass.tolg: callee '{}' has no registered GraphIO — "
            "register_io() phase missing",
            callee_full);
        return;
      }
      cio_ptr = &callee->io_meta();
    }
    const auto& cio = *cio_ptr;
    // A zero-output callee is a legitimate SINK instance (e.g. a verification /
    // DPI observer module — XiangShan `DiffExt*` / `DummyDPICWrapper` — which
    // under -DSYNTHESIS carries inputs but no outputs). It binds its inputs and
    // produces no result; handled below after the input/clock/reset wiring.

    // NOTE: set_subnode RE-STAMPS the raw hhds type to its own 2/3 loop-hint
    // encoding — type_op_of() recognizes Subs by the subnode LINK, never by
    // the stored type (see node_util.hpp).
    // Transport the complete caller execution context. Conditional callees
    // expose __valid in their GraphIO, so latch enables, properties, and any
    // descendants see the same guard that clocked state already receives.
    const Pin call_guard = effect_path_cond();
    auto      sub        = make_node(Ntype_op::Sub);
    sub.set_subnode(gio);
    last_lowered_sub_ = sub;
    {
      // Name the Sub by its RTL INSTANCE name so hhds get_hier_name() yields
      // the Verilog-style hierarchy (foo.bar.xx). The name is the LHS VARIABLE
      // the call result binds to (Pyrope `id_ex = Mod(...)`; slang passes
      // inst.name as the dst) — either the dst itself, or, when the dst is a
      // compiler temp, the variable the very next statement copies it into
      // (`const lane_q = lane(…)` lowers to `fcall(%t, lane, …)` +
      // `store(lane_q, %t)`). Strip an SSA suffix. Only a call whose result
      // never lands in a source variable falls back to the synthesized unique
      // `u_<module>_<id>` name. A call-site `name=` (reserved `__inst_name`
      // actual) takes precedence over both — it IS the explicit instance name,
      // so spelling `Mod::[name=x]` on `x = Mod(…)` is redundant, not required.
      std::string callsite_inst;
      std::string callsite_suffix;
      for (auto a = lnast_->get_sibling_next(callee_n); !a.is_invalid(); a = lnast_->get_sibling_next(a)) {
        if (!Lnast_ntype::is_store(lnast_->get_type(a))) {
          continue;
        }
        auto an = lnast_->get_first_child(a);
        if (an.is_invalid()) {
          continue;
        }
        const auto key = lnast_->get_name(an);
        if (key != "__inst_name" && key != "__inst_suffix") {
          continue;
        }
        if (auto v = lnast_->get_sibling_next(an); !v.is_invalid()) {
          (key == "__inst_name" ? callsite_inst : callsite_suffix) = std::string(lnast_->get_name(v));
        }
      }
      std::string dst_txt(lnast_->get_name(dst));
      // A `%`-prefixed compiler temp (1-char prefix); strip the `%` when
      // building the fallback instance name. The `___ssa_` infix below is the
      // unrelated user-var SSA convention — keep it.
      const bool  is_tmp    = !dst_txt.empty() && dst_txt[0] == '%';
      std::string inst_name = dst_txt;
      if (auto p = inst_name.find("___ssa_"); p != std::string::npos) {
        inst_name = inst_name.substr(0, p);
      }
      if (is_tmp) {
        inst_name = lhs_var_of_temp_dst(nid, dst_txt);
      }
      if (!callsite_inst.empty()) {
        sub.set_name(callsite_inst + callsite_suffix);
      } else if (!inst_name.empty()) {
        sub.set_name(inst_name + callsite_suffix);
      } else {
        std::string suffix = is_tmp ? dst_txt.substr(1) : dst_txt;
        sub.set_name("u_" + callee_name + "_" + suffix + callsite_suffix);
      }
    }

    // An explicit rolled_for supplies its index per occurrence, so that one
    // input is intentionally absent from the hidden ordinary call.
    const std::string supplied_index_port = rolled_index_port_;

    // Actuals → callee input sink pins. Named actuals (`port=value`, a `store`)
    // bind by port name; a bare positional actual binds the next declared input
    // in order. Track bound ports per-port so a duplicate bind or an omitted
    // input is caught individually — a bare count of provided-vs-declared could
    // net out equal when one port was bound twice and another left undriven.
    const std::string_view                          callee_bare = std::string_view(callee_full).substr(callee_full.rfind('.') + 1);
    std::size_t                                     pos         = 0;
    absl::flat_hash_set<std::string>                bound_ports;
    std::vector<std::pair<Pin, Pin>>                deferred_clocks;  // (Sub sink, ungated parent clock)
    std::vector<Pin>                                active_resets;    // normalized active-high callee resets
    // A callee reset as bound (wire, callee port): its polarity costs a walk of
    // the callee body, so it is resolved only when a clock gate reads it.
    std::vector<std::pair<Pin, std::string>>        pending_resets;
    // Every bound input's resolved driver, and the constant-clock checks that
    // read them (an idle memory port's enable may be bound after its clock,
    // positionally, or by a default).
    absl::flat_hash_map<std::string, Pin>           bound_pins;
    std::vector<std::tuple<std::string, Pin, bool>> const_clock_checks;  // (port, value, from_default)
    // A body-less lg: black box has no Clock/Reset types and its implicit
    // `clock`/`reset` are not in cio_lg: name them as a Verilog unit does.
    const bool                                      lg_box = !lg_name.empty();
    for (auto a = lnast_->get_sibling_next(callee_n); !a.is_invalid(); a = lnast_->get_sibling_next(a)) {
      std::string pname;
      Lnast_nid   val;
      if (Lnast_ntype::is_store(lnast_->get_type(a))) {
        auto an = lnast_->get_first_child(a);
        if (an.is_invalid()) {
          continue;
        }
        // A named actual's key may ride backtick-escaped (`` `port.leaf` `` —
        // a dotted flattened tuple-port leaf the prp_writer quoted); the
        // GraphIO port names are BARE — canonicalize like every other io name.
        pname = std::string(canon_io_name(lnast_->get_name(an)));
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
        // The UFCS receiver of a non-inlined `self` comb (`a.twice()` with
        // compile.upass.inline=false) drives the callee's `self` input.
        if (pname == "__ufcs_arg") {
          pname = "self";
        }
        // Reserved call-site instance name / loop-iteration suffix — already
        // consumed for sub.set_name above; never a callee port (don't bind,
        // don't count toward arity).
        if (pname == "__inst_name" || pname == "__inst_suffix") {
          continue;
        }
        // An explicit generic binding (`f<W=8>(…)`) is consumed by the runner
        // when it specializes the call. One still here means the call was
        // lowered against an unspecialized (identity/default) definition —
        // the binding would be silently dropped, so never treat it as a port.
        if (pname == "__generic_arg") {
          error_here(
              "upass.tolg: the explicit generic binding on the call to '{}' was not consumed — upass never "
              "specialized this call site (e.g. the callee is spelled through an import alias that did not "
              "resolve inside an inlined body)",
              callee_full);
          return;
        }
      } else {
        if (pos >= cio.inputs.size()) {
          error_here(
              "upass.tolg: call to '{}' passes more arguments than its "
              "{} declared inputs",
              callee_full,
              cio.inputs.size());
          return;
        }
        pname = std::string(canon_io_name(cio.inputs[pos].name));
        ++pos;
        val = a;
      }
      const auto input
          = std::find_if(cio.inputs.begin(), cio.inputs.end(), [&](const auto& e) { return canon_io_name(e.name) == pname; });
      const bool tuple_array = input != cio.inputs.end() && input->array_size > 0 && Lnast_ntype::is_ref(lnast_->get_type(val))
                               && tuple_recs_.contains(std::string(lnast_->get_name(val)));
      auto       v           = tuple_array ? whole_array_value(val, array_view_of(*input), pname) : leaf(val);
      // Generated activation-capable definitions expose `__valid` for
      // source-visible side effects. An unconditional call passes true; a call
      // under if/match conjoins the caller path so nested activation composes.
      if (pname == "__valid" && !call_guard.is_invalid()) {
        v.pin = and2(nonzero1(v.pin), call_guard);
        v.mw  = 1;
      }
      // 2f-lgimport — validate the port name BEFORE create_sink_pin: an unknown
      // port (e.g. a typo, or a call shaped for a different module) otherwise
      // asserts inside resolve_sink_port (graph.cpp). The compiler must never
      // abort on user input — emit a clean port-mismatch diagnostic instead.
      if (!gio->has_input(pname)) {
        error_here(
            "upass.tolg: call to '{}' names input '{}' which the "
            "imported module does not have",
            callee_full,
            pname);
        return;
      }
      if (!bound_ports.insert(pname).second) {
        error_here("upass.tolg: call to '{}' binds input '{}' more than once", callee_full, pname);
        return;
      }
      auto spin = sub.create_sink_pin(pname);
      if (spin.is_invalid()) {
        error_here("upass.tolg: callee '{}' has no input named '{}'", callee_full, pname);
        return;
      }
      bound_pins[pname] = v.pin;
      if (v.pin.is_const()) {
        const_clock_checks.emplace_back(pname, v.pin, false);
      } else {
        check_clock_reset_bind(nid, cio, pname, v.pin, callee.get(), callee_bare);
      }
      if ((is_clock_input(cio, pname) || (lg_box && pname == "clock")) && !call_guard.is_invalid()) {
        // Reset is not known until all actuals have been visited. Defer clock
        // wiring so the gate can use `guard | reset_asserted` and a synchronous
        // reset still reaches state while the source call is inactive.
        deferred_clocks.emplace_back(spin, v.pin);
      } else {
        spin.connect_driver(v.pin);
      }
      if (is_reset_input(cio, pname) || (lg_box && pname == "reset")) {
        pending_resets.emplace_back(v.pin, pname);
      }
    }
    // A compiler-minted activation port is deliberately absent from io_meta,
    // so source arity does not change. Missing explicit generated __valid is
    // also safe to fill here: unconditional context means true; otherwise the
    // complete caller guard is forwarded.
    if (gio->has_input("__valid") && !bound_ports.contains("__valid")) {
      auto active = call_guard.is_invalid() ? create_const(*g_, *Dlop::create_integer(1)) : call_guard;
      sub.create_sink_pin("__valid").connect_driver(active);
      bound_ports.insert("__valid");
    }
    // Every declared input must be driven — checked per-port so an omitted input
    // is caught even when another was bound twice (a bare provided==declared
    // count would miss that).
    for (const auto& ie : cio.inputs) {
      const std::string pname{canon_io_name(ie.name)};
      if (bound_ports.count(pname) == 0) {
        // A replicated instance's index input carries a different value per
        // ordinal, so realization (not the parent graph) drives it. That is the
        // ONLY input a call may leave unconnected.
        if (!supplied_index_port.empty() && pname == supplied_index_port) {
          continue;
        }
        // User ruling 2026-09-28 (30): a `comb` input its body never reads
        // (comb_port_is_dead) may be omitted, whatever its name; nothing drives
        // it, so tie it off. The runner already rejected an omitted input the
        // comb reads (a comb's `clk`/`rst` are data, never auto-wired).
        if (callee && !declares_input_default(callee.get(), ie) && comb_port_is_dead(*callee, ie)) {
          sub.create_sink_pin(pname).connect_driver(create_const(*g_, *Dlop::create_integer(0)));
          bound_ports.insert(pname);
          continue;
        }
        // User ruling 2026-09-27 (12): an OMITTED clock / implicit-reset input
        // of a `mod`/`pipe` is wired from this caller's implicit clock / reset
        // -- what already happens when the child has no such port and LiveHD
        // mints one. prepare_registry_abi gave this caller the clock/reset to
        // wire. (A Pyrope rule: an unconnected Verilog port is not a request
        // for one.) A port with its own declared default (`rst:u1 = 0`) is not
        // omitted for this rule: it keeps the ordinary defaulted-input handling.
        // An lg: black box (no callee Lnast) auto-wires nothing here: its
        // implicit clock/reset are forwarded below.
        const bool auto_wire = callee && !lnast_->is_verilog_origin() && callee->get_lambda_kind() != "comb"
                               && !declares_input_default(callee.get(), ie);
        if (auto_wire && is_clock_candidate(ie)) {
          if (clock_name_.empty()) {
            error_here("upass.tolg: call to '{}' omits clock input '{}' but '{}' has no clock to wire (needs_clock bug)",
                       callee_full,
                       pname,
                       lnast_->get_top_module_name());
          }
          auto sink = sub.create_sink_pin(pname);
          if (call_guard.is_invalid()) {
            sink.connect_driver(clock_pin());
          } else {
            deferred_clocks.emplace_back(sink, clock_pin());
          }
          bound_ports.insert(pname);
          continue;
        }
        if (auto_wire && is_reset_candidate(ie)) {
          if (reset_name_.empty()) {
            error_here("upass.tolg: call to '{}' omits reset input '{}' but '{}' has no reset to wire (needs_reset bug)",
                       callee_full,
                       pname,
                       lnast_->get_top_module_name());
          }
          // docs 04b: an unbound child `Reset` is WIRED to the caller's
          // single Reset, exactly like an explicit binding. A reset is a raw
          // wire; its polarity is a register property (`negreset=`) on each
          // side, never a property of the signal or of its name.
          sub.create_sink_pin(pname).connect_driver(reset_pin());
          if (!call_guard.is_invalid()) {
            // A conditional call's clock gate stays open while the child's
            // regs see this reset asserted, so they reset while it is absent.
            const auto child_neg = reset_input_active_low(callee.get(), pname);
            if (!child_neg) {
              error_hint_at(nid,
                            {"reset-auto-wire-polarity", "time"},
                            std::format("cannot gate the conditional call to `{}`: the regs on its reset input `{}` read it "
                                        "with both polarities (`negreset=`)",
                                        callee_bare,
                                        pname),
                            std::format("give the regs on `{}` one polarity, or make the call unconditional", pname));
            }
            active_resets.push_back(*child_neg ? not1(reset_pin()) : nonzero1(reset_pin()));
          }
          bound_ports.insert(pname);
          continue;
        }
        // 06-functions.md: an omitted input with a declared default takes it
        // (the callee stays a normal unit; its port is driven from here).
        if (const auto dv = input_default_const(callee.get(), ie)) {
          const auto dpin = create_const(*g_, *dv);
          const_clock_checks.emplace_back(pname, dpin, /*from_default=*/true);
          bound_pins[pname] = dpin;
          sub.create_sink_pin(pname).connect_driver(dpin);
          bound_ports.insert(pname);
          continue;
        }
        error_here("upass.tolg: call to '{}' does not bind declared input '{}'", callee_full, pname);
        return;
      }
    }
    for (const auto& [pname, value, from_default] : const_clock_checks) {
      check_const_clock_bind(nid, *gio, pname, value, callee.get(), bound_pins, from_default);
    }

    // Minted-clock wiring: the callee's implicit "clock" input exists on its
    // GraphIO (register_io pre-declared it) but not in its io_meta — wire it
    // to this graph's clock (needs_clock made sure we have one).
    // A callee with a declared `Clock` input (any name) has no minted one, and
    // a DECLARED input spelled `clock` (a data port) is bound by the actuals
    // above, never forwarded.
    bool callee_declares_clock = false;
    for (const auto& e : cio.inputs) {
      if (is_clock_candidate(e) || e.name == "clock") {
        callee_declares_clock = true;
        break;
      }
    }
    if (!callee_declares_clock && gio->has_input("clock") && !bound_ports.contains("clock")) {
      if (clock_name_.empty()) {
        error_here(
            "upass.tolg: instance of clocked '{}' but '{}' has no clock "
            "to forward (needs_clock bug)",
            callee_full,
            lnast_->get_top_module_name());
        return;
      }
      auto sink = sub.create_sink_pin("clock");
      if (call_guard.is_invalid()) {
        sink.connect_driver(clock_pin());
      } else {
        deferred_clocks.emplace_back(sink, clock_pin());
      }
    }

    // Minted-reset forwarding, same pattern: the callee's implicit
    // "reset" input (active-high by construction) exists on its GraphIO but
    // not in its io_meta. An active-low caller reset is inverted on the way
    // in so the callee's polarity contract holds.
    bool callee_declares_reset = false;
    for (const auto& e : cio.inputs) {
      if (is_reset_candidate(e) || e.name == "reset") {
        callee_declares_reset = true;
        break;
      }
    }
    if (!callee_declares_reset && gio->has_input("reset") && !bound_ports.contains("reset")) {
      if (reset_name_.empty()) {
        error_here(
            "upass.tolg: instance of reset-carrying '{}' but '{}' has "
            "no reset to forward (needs_reset bug)",
            callee_full,
            lnast_->get_top_module_name());
        return;
      }
      Pin r = reset_pin();
      if (reset_neg_) {
        // NOT the bitwise `Not` cell: an LGraph Not is unlimited precision
        // (`~x == -x-1`), so `Not(u1)` holds {-1,-2} and stamping its driver u1
        // is a lie -- and cprop's is_bool01 now trusts the u1 hint alone, so a
        // consumer that widens this pin would zero-fill -1. not1() is the same
        // EQ-against-0 spelling every other truth-value negation here uses, and
        // it is exact for any width.
        r = not1(r);
      }
      sub.create_sink_pin("reset").connect_driver(r);
      // The level at which the callee's regs see their minted reset asserted
      // (they are active-high unless they set `negreset=true`).
      pending_resets.emplace_back(r, "reset");
    }

    // Conditional state activation: each clock domain gets its own glitch-free
    // gate. Pyrope's generated defs have one canonical reset; accepting several
    // reset ports would require a per-state clock/reset-domain map, and OR-ing
    // unrelated resets could advance non-reset state while the call is absent.
    // Fail closed rather than guess that mapping.
    // Generated activation-capable callees are gated structurally after every
    // body has been built. A port name is neither necessary (`clk_i`) nor
    // sufficient (a minted but unused `clock`) evidence that it clocks state.
    // Keep the legacy spelling path only for imported callees without the
    // generated __valid ABI, where no post-lowering guard is available.
    if (gio->has_input("__valid")) {
      for (const auto& [sink, raw_clock] : deferred_clocks) {
        sink.connect_driver(raw_clock);
      }
      deferred_clocks.clear();
    }
    if (!call_guard.is_invalid() && !deferred_clocks.empty()) {
      if (active_resets.size() + pending_resets.size() > 1) {
        error_here("upass.tolg: conditional call to '{}' has multiple reset inputs; clock/reset domain mapping is ambiguous",
                   callee_full);
        return;
      }
      for (const auto& [r, port] : pending_resets) {
        const bool neg = lg_box ? reset_name_active_low(port) : reset_input_active_low(callee.get(), port).value_or(false);
        active_resets.push_back(neg ? not1(r) : nonzero1(r));
      }
      Pin gate_en = call_guard;
      if (!active_resets.empty()) {
        gate_en = or2(gate_en, active_resets.front());
      }
      for (const auto& [sink, raw_clock] : deferred_clocks) {
        sink.connect_driver(clock_gate(raw_clock, gate_en));
      }
    }

    std::string dst_name(lnast_->get_name(dst));

    if (cio.outputs.empty()) {
      // Sink instance (no outputs): inputs/clock/reset are wired above; there
      // is no result pin to create and nothing for the caller to bind.
      return;
    }

    if (cio.outputs.size() > 1) {
      // Multi-output callee: the fcall result is a tuple; each
      // tuple_get(dst2, result, 'port') binds that port's driver pin
      // (lower_tuple_get below). Nothing binds the bare result name.
      // Create EVERY output pin now: downstream passes/cgen walk the
      // callee GraphIO and expect the pins to exist even when a port is
      // left unread (`.e()` unconnected-output style).
      std::vector<std::optional<Sub_out>> out_stages;
      for (const auto& oe2 : cio.outputs) {
        const std::string output_name{canon_io_name(oe2.name)};
        if (!gio->has_output(output_name)) {
          error_here("upass.tolg: callee '{}' has no output named '{}'", callee_full, output_name);
          return;
        }
        auto out_dpin = sub.create_driver_pin(output_name);
        if (const auto t = stamp_sub_out_time(sub, out_dpin, oe2, kind)) {
          out_stages.emplace_back(Sub_out{oe2.stages_min, oe2.stages_max, kind == "pipe", sub, out_dpin.get_port_id()});
        } else {
          out_stages.emplace_back(std::nullopt);
        }
      }
      sub_results_[dst_name] = Sub_result{
          sub,
          {cio.outputs.begin(), cio.outputs.end()},
          std::move(out_stages),
      };
      return;
    }

    // Single output: bind dst like a graph input (external value entering).
    const auto&       oe          = cio.outputs.front();
    const std::string output_name = std::string(canon_io_name(oe.name));
    if (!gio->has_output(output_name)) {
      error_here("upass.tolg: callee '{}' has no output named '{}'", callee_full, output_name);
      return;
    }
    auto    out_dpin = sub.create_driver_pin(output_name);
    int32_t mw       = io_mw(oe);
    if (oe.kind == Io_kind::boolean) {
      set_ubits(out_dpin, 1);
      record(dst_name, out_dpin, 1);
    } else if (mw <= 1) {
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
      set_ubits(out_dpin, mw);
      record(dst_name, out_dpin, mw);
    }
    // Also expose the single output by name so an explicit field read of the
    // result (`f(...).out`) resolves through lower_tuple_get, exactly like the
    // multi-output case — without it a single-output instance whose result is
    // read via `.out` left the field unbound (the port wire was dropped and the
    // consumer wired to nil). The bare-result form (`r = f(...)`) keeps using
    // the direct record above (resolve() reads pin_map_).
    sub_results_[dst_name] = Sub_result{sub, {oe}, {}};
    // A following stage[N] re-stamps the pinned pick on the instance.
    if (const auto t = stamp_sub_out_time(sub, out_dpin, oe, kind)) {
      sub.attr(livehd::attrs::time_range).set({t->first, t->second});
      sub_out_stages_[dst_name] = {oe.stages_min, oe.stages_max, kind == "pipe", sub, out_dpin.get_port_id()};
    }
  }

  // The instance is a timed crossing: stamp the declared latency interval of
  // one of its outputs (every output of a multi-output callee has its own).
  // Bare-pipe unconstrained max (cmax<cmin) propagates as min (the Phase-1
  // realization) — the io stages remain the caller-facing truth. A callee
  // output declared `@[]` (stages -1) carries no interval: it propagates like
  // a comb crossing and stage[] picks over it fall back to the plain-RHS path.
  // A `comb` callee (compile.upass.inline=false) is purely combinational —
  // latency 0, stateless — so it carries NO interval either: `stage[N] x =
  // comb(...)` adds its flops over the instance exactly as it would over the
  // inlined logic (its stages_min is the unannotated default 0, which must not
  // pin the result to cycle 0).
  std::optional<std::pair<int64_t, int64_t>> stamp_sub_out_time(const hhds::Node_class& sub, const Pin& out_dpin,
                                                                const Lnast_io_entry& oe, std::string_view kind) {
    if (kind == "comb" || oe.stages_min < 0) {
      return std::nullopt;
    }
    const int64_t cmin = oe.stages_min;
    const int64_t cmax = oe.stages_max < oe.stages_min ? oe.stages_min : oe.stages_max;

    sub_time_[{sub.get_debug_nid(), out_dpin.get_port_id()}] = {cmin, cmax};
    return std::pair{cmin, cmax};
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
    if (lnast_->is_timecheck_off()) {
      return;  // `::[timecheck=false]` turns off every `@[N]` check
    }
    const std::string name(canon_io_name(lnast_->get_name(ref)));  // pin_map_ is keyed canonically
    auto              it = pin_map_.find(name);
    if (it == pin_map_.end()) {
      error_here(
          "upass.tolg: `@[N]` check on '{}' — the value never "
          "materialized in the graph",
          name);
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
      // docs 04b "Implicit clock and reset": the implicit clock is the ONE
      // `Clock` input. With two or more, whatever relies on it (a reg or
      // memory with no `clock_pin`, a stage, a child's omitted Clock) is a
      // compile error -- the twin of reset_pin()'s. Anchored at the current
      // statement: the first such reg/stage/call.
      std::vector<std::string_view> candidates;
      for (const auto& e : lnast_->io_meta().inputs) {
        if (is_clock_candidate(e)) {
          candidates.push_back(e.name);
        }
      }
      // Imported Verilog names every clock but its conventional one
      // (clock_name_), so a second Clock-typed port (typed by its use) is no
      // ambiguity there.
      if (candidates.size() > 1 && !lnast_->is_verilog_origin()) {
        const auto top = lnast_->get_top_module_name();
        error_hint_at(Lnast_nid{},
                      {"clock-ambiguous", "time"},
                      std::format("'{}' has {} `Clock` inputs (`{}`), so it has no implicit clock — every register, "
                                  "memory and stage needs `clock_pin=…`, and every child `Clock` input must be bound",
                                  top.substr(top.rfind('.') + 1),
                                  candidates.size(),
                                  absl::StrJoin(candidates, "`, `")),
                      std::format("bind this one with `:[clock_pin={}]` (a reg, stage or memory), or pass the clock by "
                                  "name at a call",
                                  candidates.front()));
      }
      auto p = g_->get_input_pin(clock_name_);
      if (clock_minted_) {
        set_ubits(p, 1);
      }
      clock_pin_       = p;
      clock_pin_valid_ = true;
    }
    return clock_pin_;
  }

  // Pre-scan: record every `attr_set(<var>, "reset_pin", <val>)` in the tree.
  // See decl_reset_pin_ for why a memory declare cannot wait for
  // pending_attrs_.
  void collect_decl_reset_pins(const Lnast_nid& nid) {
    if (Lnast_ntype::is_attr_set(lnast_->get_type(nid))) {
      auto tgt = lnast_->get_first_child(nid);
      if (!tgt.is_invalid()) {
        auto key = lnast_->get_sibling_next(tgt);
        if (!key.is_invalid() && Lnast_ntype::is_const(lnast_->get_type(key)) && lnast_->get_name(key) == "reset_pin") {
          auto val = lnast_->get_sibling_next(key);
          decl_reset_pin_[std::string(lnast_->get_name(tgt))]
              = val.is_invalid() ? std::string{"true"} : std::string(lnast_->get_name(val));
        }
      }
    }
    for (auto c = lnast_->get_first_child(nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      collect_decl_reset_pins(c);
    }
  }

  // The reset SIGNAL a memory's whole-array `reset` pin is driven from: a
  // source-spelled `reset_pin` when the declaration carries one, otherwise the
  // module's implicit reset. Empty means "no reset at all" (`reset_pin=false`,
  // or no implicit reset either).
  // docs 04b: a reset is active-high unless `negreset=true`; a Verilog-origin
  // unit keeps the reader's `_n` naming convention as its default.
  [[nodiscard]] bool reset_active_low(std::optional<bool> negreset, std::string_view reset_name) const {
    // docs 04b: a Pyrope reset name carries no polarity (`rst_n` is
    // active-high without `negreset=true`); the Verilog reader's `_n`
    // convention stays for a Verilog-origin unit.
    return negreset.value_or(lnast_->is_verilog_origin() && upass::io_port::reset_name_active_low(reset_name));
  }

  [[nodiscard]] std::string mem_reset_source(std::string_view name) const {
    if (auto it = decl_reset_pin_.find(std::string(name)); it != decl_reset_pin_.end()) {
      return it->second == "false" ? std::string{} : it->second;
    }
    return reset_name_;
  }

  // THE shared resolution for a source-spelled reset signal name, used by both
  // finalize_regs (scalar flops) and finalize_mems (register arrays). Usually
  // a graph input, but a reset synchronizer drives it from a DERIVED
  // module-level signal, so fall through the same ladder in both places: a
  // `wire`'s own din (the passthrough buffer is dropped by cgen), then the
  // last SSA version in logical_last_, then a plain resolve(). An invalid Pin
  // back means "nothing in this module drives that name" and the caller must
  // diagnose it -- the two paths drifted once and a memory silently lost its
  // reset.
  [[nodiscard]] Pin resolve_reset_signal(std::string_view rst_name) {
    if (rst_name.empty()) {
      return Pin{};
    }
    // A Reset port named by a backticked type word (`reset_pin=`U4``): the
    // GraphIO spells it bare (checked first: a quoted wire keeps its path below).
    if (const auto canon = canon_io_name(rst_name); canon != rst_name && g_->get_io()->has_input(canon)) {
      return g_->get_input_pin(canon);
    }
    if (g_->get_io()->has_input(rst_name)) {
      return g_->get_input_pin(rst_name);
    }
    std::string base(rst_name);
    if (auto p = base.find("___ssa_"); p != std::string::npos) {
      base.resize(p);
    }
    if (auto dit = wire_names_.contains(base) ? pin_map_.find(din_key(base)) : pin_map_.end(); dit != pin_map_.end()) {
      return dit->second;
    }
    if (auto lit = logical_last_.find(base); lit != logical_last_.end()) {
      return lit->second.first;
    }
    return resolve(rst_name);
  }

  // The module reset graph-input pin (same lazy stamping contract
  // as clock_pin).
  [[nodiscard]] Pin reset_pin() {
    if (!reset_pin_valid_) {
      // The implicit reset is the ONE `Reset` input (docs 04b); with two or
      // more, a register relying on it is a compile error (lazy, like
      // clock_pin()'s ambiguity).
      std::vector<std::string_view> candidates;
      for (const auto& e : lnast_->io_meta().inputs) {
        if (is_reset_candidate(e)) {
          candidates.push_back(e.name);
        }
      }
      if (candidates.size() > 1) {
        const auto top = lnast_->get_top_module_name();
        error_hint_at(Lnast_nid{},
                      {"reset-ambiguous", "time"},
                      std::format("'{}' has {} `Reset` inputs (`{}`), so it has no implicit reset — every initialized "
                                  "register needs `reset_pin=…`, and every child `Reset` input must be bound",
                                  top.substr(top.rfind('.') + 1),
                                  candidates.size(),
                                  absl::StrJoin(candidates, "`, `")),
                      std::format("bind this one with `:[reset_pin={}]`, or pass the reset by name at a call", candidates.front()));
      }
      auto p = g_->get_input_pin(reset_name_);
      if (reset_minted_) {
        set_ubits(p, 1);
      }
      reset_pin_       = p;
      reset_pin_valid_ = true;
    }
    return reset_pin_;
  }

  // The activation graph-input pin. Minted pins are stamped lazily like the
  // implicit clock/reset; an explicit generated __valid keeps its IO stamp.
  [[nodiscard]] Pin valid_pin() {
    if (!valid_pin_valid_) {
      auto p = g_->get_input_pin(valid_name_);
      if (valid_minted_) {
        set_ubits(p, 1);
      }
      valid_pin_       = p;
      valid_pin_valid_ = true;
    }
    return valid_pin_;
  }

  [[nodiscard]] int64_t const_val(const Lnast_nid& nid) {
    auto c = Dlop::from_pyrope(lnast_->get_name(nid));
    return c->is_just_i64() ? c->to_just_i64() : 0;
  }

  // get_mask(dst, value, lo, [hi]): half-open range or one bit.
  // Runtime endpoints lower to explicit shifts and bitwise operations.
  void lower_get_mask(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    auto val = lnast_->get_sibling_next(dst);
    auto lo  = lnast_->get_sibling_next(val);
    auto hi  = lnast_->get_sibling_next(lo);
    I(!lo.is_invalid());
    extend_mem_rmw_read(dst, val, false);
    auto a = leaf(val);
    if (Lnast_ntype::is_const(lnast_->get_type(lo)) && (hi.is_invalid() || Lnast_ntype::is_const(lnast_->get_type(hi)))) {
      auto l = Dlop::from_pyrope(lnast_->get_name(lo));
      auto h = hi.is_invalid()                 ? l->add_op(*Dlop::create_integer(1))
               : lnast_->get_name(hi) == "nil" ? Dlop::create_integer(a.mw)
                                               : Dlop::from_pyrope(lnast_->get_name(hi));
      if (!l->is_just_i64() || !h->is_just_i64() || l->to_just_i64() < 0 || h->to_just_i64() > std::numeric_limits<int>::max()) {
        error_at(nid, {"bit-range-invalid", "type"}, "invalid get_mask endpoints");
      }
      int low = l->to_just_i64(), high = h->to_just_i64();
      if (high <= low) {
        bind_result(lnast_->get_name(dst), create_const(*g_, *Dlop::create_integer(0)), 1);
        return;
      }
      auto out = make_node(Ntype_op::Get_mask);
      livehd::graph_util::connect_mask_operands(out, a.pin, low, high);
      bind_result(lnast_->get_name(dst), out.create_driver_pin(0), high - low);
      return;
    }
    lower_dynamic_range_select(dst, val, lo, hi, nid);
  }

  void lower_dynamic_range_select(const Lnast_nid& dst, const Lnast_nid& val, const Lnast_nid& lo, const Lnast_nid& hi,
                                  const Lnast_nid& loc_nid) {
    auto a_val = leaf(val);
    auto n     = leaf(lo);

    // shifted = a >> n   (arithmetic right shift; the only right shift cell —
    // for an unsigned `a` cgen's `>>>` fills zeros, matching the workaround).
    auto sra = make_node(Ntype_op::SRA);
    setup_sink_by_name(sra, "a").connect_driver(a_val.pin);
    setup_sink_by_name(sra, "b").connect_driver(n.pin);
    auto sra_dp = sra.create_driver_pin(0);
    if (pin_can_be_negative(a_val.pin)) {
      set_sbits(sra_dp, a_val.mw);
    } else {
      set_ubits(sra_dp, a_val.mw);
    }

    if (!hi.is_invalid() && Lnast_ntype::is_const(lnast_->get_type(hi)) && lnast_->get_name(hi) == "nil") {
      // Open range `a#[n..]`: bits n..msb are exactly `a>>n`; no mask, no
      // m>=n precondition (there is no `m`).
      bind_result(lnast_->get_name(dst), sra_dp, a_val.mw);
      return;
    }

    auto m = range_upper(n, hi);
    if (const auto width = const_window_offset(m.pin, n.pin); width && *width > 0) {
      auto out = make_node(Ntype_op::Get_mask);
      livehd::graph_util::connect_mask_operands(out, sra_dp, 0, static_cast<int>(*width));
      bind_result(lnast_->get_name(dst), out.create_driver_pin(0), static_cast<int>(*width));
      return;
    }
    auto rw = lower_range_width(n, m);

    // pow = 1 << width. One headroom bit represents 2^a_width before -1.
    auto pow = make_node(Ntype_op::SHL);
    setup_sink_by_name(pow, "a").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    setup_sink_by_name(pow, "b").connect_driver(rw.clamped);
    const int32_t pow_mw = a_val.mw + 1;
    auto          pow_dp = pow.create_driver_pin(0);
    set_ubits(pow_dp, pow_mw);

    // mask = pow - 1   (the low (m-n) bits set).
    auto maskn = make_node(Ntype_op::Sum);
    setup_sink_by_name(maskn, "as").connect_driver(pow_dp);
    setup_sink_by_name(maskn, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    auto mask_dp = maskn.create_driver_pin(0);
    set_ubits(mask_dp, pow_mw);

    // result = shifted & mask
    auto andn = make_node(Ntype_op::And);  // commutative: both operands feed sink "a"
    setup_sink_by_name(andn, "as").connect_driver(sra_dp);
    setup_sink_by_name(andn, "as").connect_driver(mask_dp);
    bind_result(lnast_->get_name(dst), andn.create_driver_pin(0), a_val.mw);

    lower_range_assert(rw.reversed, loc_nid);
  }

  // `hi - lo`, plus the one-bit "this range is REVERSED" flag that both the
  // data path and the runtime assert need. Shared by the range READ and the
  // range WRITE: the two halves of one bit view have to agree on the geometry.
  //
  // The difference is genuinely SIGNED. Nothing orders two runtime endpoints
  // (`lo`/`hi` are plain lowered values, and the `hi >= lo` obligation is a
  // runtime lgassert that no width/range inference consumes), which is the same
  // rule lower_op applies to every other subtraction -- "a subtraction can go
  // negative regardless of operand signs". Stamping it unsigned was a lie the
  // shifts below then read as a huge count: `1 << width` wrapped to 0, the low
  // mask to all-ones, and a WRITE clobbered every bit of its destination --
  // outside the requested range, and decided before the assert ever fires.
  //
  // The flag tests the SIGN OF THE WIDTH, not `hi < lo`. An LT node carries the
  // structural u1 hint on its output pin and cgen.verilog derives the
  // comparison's signedness from exactly that pin, so `hi < lo` emits a bare
  // `hi < lo` -- and Verilog makes a relational UNSIGNED as soon as one operand
  // is unsigned, so a negative `hi` (`a#[j..=(i-1)]`, i == 0) read as a huge
  // value and the guard silently never fired. cgen.sim instead takes the
  // comparison's signedness from the OPERAND pins, so the same node also
  // disagreed between the two backends. Both operands of `width < 0` are
  // signed, so every backend agrees, and it is the exact condition wanted:
  // `width == 0` (the empty `hi == lo`) already yields a zero mask.
  struct Range_width {
    Pin     clamped;   // unsigned: the width, or 0 when the range is reversed
    Pin     reversed;  // u1: 1 when hi < lo
    int32_t mw;
  };

  Val range_upper(const Val& lo, const Lnast_nid& hi) {
    if (!hi.is_invalid()) {
      return leaf(hi);
    }
    auto sum = make_node(Ntype_op::Sum);
    setup_sink_by_name(sum, "as").connect_driver(lo.pin);
    setup_sink_by_name(sum, "as").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    auto out = sum.create_driver_pin(0);
    set_ubits(out, lo.mw + 1);
    return Val{out, lo.mw + 1};
  }

  Range_width lower_range_width(const Val& lo, const Val& hi) {
    // One bit WIDER than the endpoints' own carrier: the widest legal width,
    // `hi_max + 1`, needs the full unsigned `w_mw`, so a SIGNED carrier of the
    // same size would wrap it.
    const int32_t w_mw  = std::max(lo.mw, hi.mw) + 1;
    auto          width = make_node(Ntype_op::Sum);
    setup_sink_by_name(width, "as").connect_driver(hi.pin);
    setup_sink_by_name(width, "bs").connect_driver(lo.pin);
    auto width_dp = width.create_driver_pin(0);
    set_sbits(width_dp, w_mw + 1);

    auto rev = make_node(Ntype_op::LT);  // positional: width < 0
    setup_sink_by_name(rev, "as").connect_driver(width_dp);
    setup_sink_by_name(rev, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    auto rev_dp = rev.create_driver_pin(0);
    set_ubits(rev_dp, 1);

    // A reversed range selects NO bits, so clamp its width to 0: the mask comes
    // out 0, so a read is 0 and a write leaves its destination untouched -- the
    // only sane data path for an empty range (the lgassert still reports it).
    auto sel = make_node(Ntype_op::Mux);
    livehd::graph_util::setup_sink_pid(sel, 0).connect_driver(rev_dp);                                       // selector
    livehd::graph_util::setup_sink_pid(sel, 1).connect_driver(width_dp);                                     // false: hi >= lo
    livehd::graph_util::setup_sink_pid(sel, 2).connect_driver(create_const(*g_, *Dlop::create_integer(0)));  // true: reversed
    auto clamped = sel.create_driver_pin(0);
    // Unsigned (the clamp proves it) and never NARROWER than the widest arm:
    // cgen.sim rejects a Mux whose result carrier truncates an arm
    // ("mux-width-loss").
    set_ubits(clamped, w_mw + 1);

    return Range_width{.clamped = clamped, .reversed = rev_dp, .mw = w_mw + 1};
  }

  // Emit a runtime `lgassert(hi >= lo)` guarding a dynamic range select against
  // a descending range (the select precondition). The check is an `lgassert`
  // Sub instance: a recognized primitive cgen lowers to an inline SystemVerilog
  // immediate assertion (no data-path output, so LEC is unaffected). `loc_nid`
  // carries the `a#[lo..=hi]` source span for the assert message. Skipped when
  // there is no GraphLibrary to register the primitive in (the data-path
  // lowering is already complete and correct without the guard).
  // `reversed` is the flag lower_range_width already built for the data path.
  // Recomputing it here as `LT(hi, lo)` is what the guard used to do, and that
  // spelling could not fire for a signed `hi` -- see lower_range_width.
  void lower_range_assert(const Pin& reversed, const Lnast_nid& loc_nid) {
    if (lib_ == nullptr) {
      return;
    }
    // cond = (hi >= lo) = reversed XOR 1.
    auto notn = make_node(Ntype_op::Xor);
    setup_sink_by_name(notn, "as").connect_driver(reversed);
    setup_sink_by_name(notn, "as").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    auto cond = notn.create_driver_pin(0);
    set_ubits(cond, 1);

    // A dynamic range check is a source-visible effect just like an assert:
    // while this definition/branch is inactive, the obligation is vacuous.
    const auto guard = effect_path_cond();
    if (!guard.is_invalid()) {
      cond = or2(not1(nonzero1(guard)), nonzero1(cond));
    }

    auto gio = lib_->find_io(livehd::graph_util::lgassert_module_name);
    if (!gio) {
      gio = lib_->create_io(livehd::graph_util::lgassert_module_name);
      gio->add_input("cond", 1);
      gio->set_bits("cond", 1);
      gio->set_unsign("cond", true);
    }
    auto sub = make_node(Ntype_op::Sub);
    sub.set_subnode(gio);
    sub.create_sink_pin("cond").connect_driver(cond);
    // Carry the "line of code info" (file:line of the `a#[lo..=hi]`) on the
    // instance-name attr so cgen can fold it into the assertion message.
    const auto  sp  = lnast_->span_of(loc_nid);
    std::string loc = sp.file.empty() ? std::string{"?"} : sp.file;
    if (sp.start_line) {
      loc += ":" + std::to_string(*sp.start_line);
    }
    sub.attr(hhds::attrs::name).set(loc);
  }

  // Pyrope requires a dynamic out-of-range array access to fail at runtime.
  // Materialize `0 <= index < size` as the same lgassert primitive used for
  // dynamic range preconditions. Verilog-origin accesses are handled by slang
  // and retain SystemVerilog's X/ignored-write behavior instead.
  void lower_array_index_assert(const Val& index, int64_t size, const Lnast_nid& loc_nid) {
    if (lib_ == nullptr || lnast_->is_verilog_origin()) {
      return;
    }

    auto lt_size = make_node(Ntype_op::LT);
    setup_sink_by_name(lt_size, "as").connect_driver(index.pin);
    setup_sink_by_name(lt_size, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(size)));
    auto cond = lt_size.create_driver_pin(0);
    set_ubits(cond, 1);

    if (pin_can_be_negative(index.pin)) {
      auto lt_zero = make_node(Ntype_op::LT);
      setup_sink_by_name(lt_zero, "as").connect_driver(index.pin);
      setup_sink_by_name(lt_zero, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
      auto neg = lt_zero.create_driver_pin(0);
      set_ubits(neg, 1);
      cond = and2(cond, not1(neg));
    }

    const auto guard = effect_path_cond();
    if (!guard.is_invalid()) {
      cond = or2(not1(nonzero1(guard)), nonzero1(cond));
    }

    auto gio = lib_->find_io(livehd::graph_util::lgassert_module_name);
    if (!gio) {
      gio = lib_->create_io(livehd::graph_util::lgassert_module_name);
      gio->add_input("cond", 1);
      gio->set_bits("cond", 1);
      gio->set_unsign("cond", true);
    }
    auto sub = make_node(Ntype_op::Sub);
    sub.set_subnode(gio);
    sub.create_sink_pin("cond").connect_driver(cond);
    const auto  sp  = lnast_->span_of(loc_nid);
    std::string loc = "array index out of range";
    if (!sp.file.empty()) {
      loc += " at " + sp.file;
      if (sp.start_line) {
        loc += ":" + std::to_string(*sp.start_line);
      }
    }
    sub.attr(hhds::attrs::name).set(loc);
  }

  // Materialize a verifier-unknown cassert (assert / assert_always / assume) as
  // an `fproperty` Sub: a recognized primitive carrying the 1-bit cond, with
  // "<kind>\x1f<loc>\x1f<msg>" packed in the instance-name attr. pass.formal
  // proves/defers it; cgen emits a runtime check for what it could not prove.
  void lower_cassert(const Lnast_nid& nid) {
    if (lib_ == nullptr) {
      return;
    }
    auto cond_nid = lnast_->get_first_child(nid);
    if (cond_nid.is_invalid()) {
      return;
    }
    Val cond = leaf(cond_nid);
    if (cond.pin.is_invalid()) {
      return;
    }
    // Children after cond: an optional kind sentinel (assume / assert_always)
    // followed by an optional user message (both are const children that
    // survive upass re-emission, unlike the cassert node name).
    std::string kind = "assert";
    std::string msg;
    auto        nxt = lnast_->get_sibling_next(cond_nid);
    if (!nxt.is_invalid() && Lnast_ntype::is_const(lnast_->get_type(nxt))) {
      std::string s{lnast_->get_name(nxt)};
      // EXACT match, never a substring search: when the user wrote a plain
      // `assert` there is no sentinel and this child is the user's MESSAGE.
      // An unanchored find() there let `assert(x, "… __fkind__assume …")`
      // retype itself into an assume that the solver then USED as a
      // hypothesis — a silent false-PROVEN. The sentinel is emitted unquoted
      // so it cannot collide with any string message.
      if (s == "__fkind__assert_always") {
        kind = "assert_always";
        nxt  = lnast_->get_sibling_next(nxt);
      } else if (s == "__fkind__assume") {
        kind = "assume";
        nxt  = lnast_->get_sibling_next(nxt);
      } else if (s == "__fkind__assume_nocheck") {
        kind = "assume_nocheck";
        nxt  = lnast_->get_sibling_next(nxt);
      } else if (s == "__fkind__cassert") {
        kind = "cassert";
        nxt  = lnast_->get_sibling_next(nxt);
      }
    }
    // `cassert` is an ELABORATION check: the upass must fold it here, or it
    // fails. It never becomes an fproperty, so it
    // never reaches pass.formal and never survives into the netlist as a
    // runtime check — that is exactly what distinguishes it from `assert`.
    if (kind == "cassert") {
      if (!cond.pin.is_const()) {
        error_at(nid,
                 {"cassert-not-comptime", "unsupported"},
                 "upass.tolg: cassert condition did not fold to a compile-time "
                 "value — cassert is an elaboration check; use `assert` for a "
                 "condition that must hold of the hardware");
        return;
      }
      // Discharge only on a known-TRUE fold. `!is_known_false()` is not the
      // same predicate: an X/unknown constant pin is const and not known-false,
      // so it would slip through as "proven" and emit a full netlist. A cassert
      // the compiler cannot decide is exactly the case that must fail.
      if (!livehd::graph_util::const_of(cond.pin).is_known_true()) {
        error_at(nid, {"cassert-false", "unsupported"}, "upass.tolg: cassert condition is not true at compile time");
      }
      return;  // folded true: discharged here, nothing to materialize
    }
    if (!nxt.is_invalid() && Lnast_ntype::is_const(lnast_->get_type(nxt))) {
      msg = std::string{lnast_->get_name(nxt)};
    }
    auto gio = lib_->find_io(livehd::graph_util::fproperty_module_name);
    if (!gio) {
      gio = lib_->create_io(livehd::graph_util::fproperty_module_name);
      gio->add_input("cond", 1);
      gio->set_bits("cond", 1);
      gio->set_unsign("cond", true);
    }
    // R1 Phase 2 — the RAW guard, as a second port that is DIAGNOSTIC ONLY.
    // `cond` above already carries the full obligation (`!guard || cond`), so a
    // consumer that never reads this port is still CORRECT — it merely loses the
    // antecedent-vacuity diagnostic. That asymmetry is the whole reason the
    // implication is folded into one pin instead of being carried here: a
    // correctness-bearing second port reproduces the R1 bug once per consumer
    // that forgets it, while a diagnostic one can only cost a warning.
    // Guarded by has_input so an fproperty GraphIO loaded from an older `lg:`
    // artifact (cond only) degrades to "no vacuity check" rather than asserting.
    if (!gio->has_input("guard")) {
      gio->add_input("guard", 2);
      gio->set_bits("guard", 1);
      gio->set_unsign("guard", true);
    }
    // R1 — an `assert`/`assume` inside an `if`/`match` arm is guarded by that
    // arm's path condition, exactly as SystemVerilog does it: a procedural
    // assertion is only evaluated when control flow reaches it, which the LRM
    // models as an implication (`guard |-> cond`). Dropping the guard makes the
    // obligation STRICTLY STRONGER, which is silently wrong in both directions
    // — an assert fires on paths the user never claimed anything about, and an
    // over-constrained assume prunes traces (a false PROVEN). So fold the guard
    // into `cond` rather than carrying it as a second port: every consumer of
    // this Sub (pass.formal, the verify monitor encode, cgen_verilog,
    // cgen_sim) then honors it by construction, where a separate port would
    // reproduce this bug once per consumer that forgot to read it.
    //
    // IMPLICATION, not conjunction — `!guard || cond`. The other consumer of
    // current_path_cond() (memory write enables) wants `and2(guard, ...)`; the
    // same pin with the wrong combinator here yields an assert that fires
    // precisely when the guard is FALSE.
    //
    // `cassert` never reaches this point (it returned above): it is an
    // elaboration check that must fold to a comptime constant, and `!guard ||
    // cond` under a runtime guard never folds, so guarding it would turn
    // working code into `cassert-not-comptime`.
    //
    // or2 treats an invalid operand as the identity, so an unguarded property
    // (empty path stack) mints no cells at all — and, crucially, keeps its cond
    // pin EXACTLY as before, at full width, so the encoder's own nonzero test
    // still spans every bit.
    //
    // A guarded one must reduce first: or2/not1 stamp `bits=1`, which the
    // encoder fits to [0:0], so handing them a multi-bit condition would keep
    // only its LSB. `if c { assert(flags | 0x2) }` is always true (bit 1 is
    // always set) yet refuted on flags[0]==0 before nonzero1 was applied. Both
    // operands go through it: `cond` because the user may assert any integer,
    // and `guard` because it is only 1-bit by convention (prp2lnast gives an
    // if-condition a synthetic `:bool`), not by construction.
    const auto guard    = effect_path_cond();
    const auto eff_cond = guard.is_invalid() ? cond.pin : or2(not1(nonzero1(guard)), nonzero1(cond.pin));
    auto       sub      = make_node(Ntype_op::Sub);
    sub.set_subnode(gio);
    sub.create_sink_pin("cond").connect_driver(eff_cond);
    if (!guard.is_invalid() && gio->has_input("guard")) {
      sub.create_sink_pin("guard").connect_driver(guard);
    }
    const auto  sp  = lnast_->span_of(nid);
    std::string loc = sp.file.empty() ? std::string{} : sp.file;
    if (sp.start_line) {
      loc += ":" + std::to_string(*sp.start_line);
    }
    sub.attr(hhds::attrs::name).set(kind + "\x1f" + loc + "\x1f" + msg);
  }

  // `r.f#[..] = v` on a REGISTER field. After the register's detuple,
  // prp2lnast's read-modify-write reads the field into a temp, splices the lane
  // in and stores the result back: `store(%t, r.f)`, `set_mask(%n, %t, ..)`,
  // `store(r.f, %n)`. That read is the partial write's own base, so it must
  // resolve like `r#[..] = v`'s: to the pending din, or a second write of the
  // cycle rebuilds the field from Q and drops the first. Returns the register
  // operand `val` stands for, or `val` itself when the set_mask is not such a
  // write-back (a temp copy of a register read for any other use sees Q).
  [[nodiscard]] Lnast_nid reg_field_rmw_base(const Lnast_nid& set_mask_nid, const Lnast_nid& val) const {
    const auto dst = lnast_->get_first_child(set_mask_nid);
    if (dst.is_invalid() || !Lnast_ntype::is_ref(lnast_->get_type(val)) || !Lnast::is_tmp(lnast_->get_name(val))) {
      return val;
    }
    const auto plain_store = [&](const Lnast_nid& n, std::string_view target) -> Lnast_nid {
      if (!Lnast_ntype::is_store(lnast_->get_type(n))) {
        return {};
      }
      const auto lhs = lnast_->get_first_child(n);
      const auto rhs = lhs.is_invalid() ? lhs : lnast_->get_sibling_next(lhs);
      if (rhs.is_invalid() || !lnast_->get_sibling_next(rhs).is_invalid() || lnast_->get_name(lhs) != target
          || !Lnast_ntype::is_ref(lnast_->get_type(rhs))) {
        return {};
      }
      return rhs;
    };
    // The temp's definition, a few statements up (the lane mask and a wrap/sat
    // cast sit in between): a copy of a register, with no write of that
    // register between it and the set_mask. Any other definition ends the
    // search.
    Lnast_nid reg;
    int       budget = 64;
    for (auto p = lnast_->get_sibling_prev(set_mask_nid); !p.is_invalid() && --budget > 0; p = lnast_->get_sibling_prev(p)) {
      const auto lhs = lnast_->get_first_child(p);
      if (lhs.is_invalid() || lnast_->get_name(lhs) != lnast_->get_name(val) || Lnast_ntype::is_type_spec(lnast_->get_type(p))) {
        continue;
      }
      reg = plain_store(p, lnast_->get_name(val));
      break;
    }
    if (reg.is_invalid() || !reg_map_.contains(std::string(lnast_->get_name(reg)))) {
      return val;
    }
    const auto def = lnast_->get_parent(reg);
    for (auto p = lnast_->get_sibling_prev(set_mask_nid); !p.is_invalid() && p != def; p = lnast_->get_sibling_prev(p)) {
      if (Lnast_ntype::is_store(lnast_->get_type(p)) && lnast_->get_name(lnast_->get_first_child(p)) == lnast_->get_name(reg)) {
        return val;
      }
    }
    // ... and the result stored straight back to it.
    for (auto n = lnast_->get_sibling_next(set_mask_nid); !n.is_invalid(); n = lnast_->get_sibling_next(n)) {
      if (Lnast_ntype::is_type_spec(lnast_->get_type(n))) {
        continue;
      }
      const auto rhs = plain_store(n, lnast_->get_name(reg));
      return !rhs.is_invalid() && lnast_->get_name(rhs) == lnast_->get_name(dst) ? reg : val;
    }
    return val;
  }

  // The base (`value`) operand of a set_mask. Normally `leaf(val)`, but a
  // `mut b:uN = nil` emits no init store, so the first `b#[lo..=hi] = …`
  // reads `b` with no driver. That is NOT an error: the bit-assignments
  // overwrite the covered bits, and whatever is left uncovered reads 0, the
  // same value upass leaves when it folds the chain. Only a name that was
  // DECLARED as a scalar mut/const (or is an output port, which starts as nil
  // the same way) gets this zero base, sized at its DECLARED width — a genuine
  // undriven reference (typo, dropped value) still errors through
  // leaf()/resolve().
  //
  // Both lookups MUST go through canon_io_name. `pin_map_` is keyed on the
  // canonical (backtick-stripped) name because record()/resolve() canonicalize,
  // so probing it with the RAW name misses on every backtick-escaped
  // identifier -- `` `req_written_rearm.addr` ``, i.e. every struct leaf the
  // Pyrope writer emits. The miss then read as "declared but never driven" and
  // substituted a 0sb? base, DISCARDING the value the variable was carrying:
  // `x = a; if c { x#[hi..=lo] = v }` silently lost `a`'s uncovered bits under
  // the branch. It refuted `minion_dcache_replay_queue`; the shape is a
  // conditional partial write, which is why the unconditional form and a plain
  // identifier both looked fine.
  //
  // 2c-wire — a `wire` needs a seed too, but the test above can never
  // fire for one: lower_wire_declare records the passthrough-Or OUTPUT under
  // the wire's name (so reads are position independent), so a wire is ALWAYS
  // in pin_map_. Taking leaf(val) there seeded the chain with the wire's own
  // buffer output while the completed write connects the chain's result back to
  // that buffer's INPUT — a manufactured combinational RING (`upass.tolg:
  // combinational loop`), the one class of self-referential set_mask chain the
  // frontends still produced. The uncovered bits of a wire assembled from
  // bit-range writes are undriven (a Verilog net reads X there), so seed an
  // unsigned all-`?` value at the wire's DECLARED width and let the covered
  // lanes overwrite it. Only the
  // FIRST partial write reaches here: lower_set_mask prefers the din
  // accumulator once one exists, so a chain still accumulates, and a wire with
  // a whole-value driver already recorded keeps that value as its base.
  [[nodiscard]] Val set_mask_base(const Lnast_nid& operand, const Lnast_nid& set_mask_nid = {}) {
    const auto val = set_mask_nid.is_invalid() ? operand : reg_field_rmw_base(set_mask_nid, operand);
    if (Lnast_ntype::is_ref(lnast_->get_type(val))) {
      const std::string raw{lnast_->get_name(val)};
      const std::string name{canon_io_name(raw)};
      // Every partial write accumulates on the pending input, including
      // runtime ranges. Ordinary expression reads still resolve to committed Q.
      if (reg_map_.contains(raw) || wire_names_.contains(raw)) {
        if (auto dit = pin_map_.find(din_key(raw)); dit != pin_map_.end()) {
          return {dit->second, mw_lookup(din_key(raw))};
        }
      }
      // A packed bit-view write after a whole-memory assignment must splice
      // into the pending bulk value, just as a scalar register uses its din.
      // Ordinary reads still use the memory's committed read_all value.
      auto mit = mem_map_.find(raw);
      if (mit == mem_map_.end()) {
        mit = mem_map_.find(name);
      }
      if (mit != mem_map_.end() && !mit->second.is_array && mit->second.has_update) {
        auto&      mi    = mit->second;
        const auto width = static_cast<int32_t>(mi.size * mi.elem_mw);
        if (mi.update_en.is_invalid()) {
          return {mi.update_val, width};
        }
        auto mux = make_node(Ntype_op::Mux);
        livehd::graph_util::setup_sink_pid(mux, 0).connect_driver(mi.update_en);
        livehd::graph_util::setup_sink_pid(mux, 1).connect_driver(get_or_make_read_all(mi));
        livehd::graph_util::setup_sink_pid(mux, 2).connect_driver(mi.update_val);
        auto value = mux.create_driver_pin(0);
        set_ubits(value, width);
        return {value, width};
      }
      if (const auto ait = nil_seed_alias_.find(name); ait != nil_seed_alias_.end()) {
        const auto dt = decl_type_lookup(ait->second);
        return {create_const(*g_, *Dlop::create_integer(0)), dt ? dt->mw : int32_t{1}};
      }
      if (!pin_map_.contains(name) && scalar_decl_.contains(name)) {
        // The upass fold of `mut v:uN = nil; v#[..] = x` leaves the uncovered
        // bits 0; an output or a runtime local lowered here must agree with it.
        const auto dt = decl_type_lookup(name);
        return {create_const(*g_, *Dlop::create_integer(0)), dt ? dt->mw : int32_t{1}};
      }
      if (auto wit = wire_info_.find(raw); wit != wire_info_.end() && !pin_map_.contains(din_key(raw))) {
        const int32_t mw = wit->second.decl_mw > 0 ? wit->second.decl_mw : 1;
        return {undriven_pin(mw), mw};
      }
    }
    return leaf(val);
  }

  void record_set_mask_result(std::string_view dst_name, const Pin& drv, int32_t mw) {
    if (auto mit = mem_map_.find(std::string(dst_name)); mit != mem_map_.end()) {
      // In-place bit writes from Slang have no following store. Commit their
      // packed value through the memory update port, preserving the Q binding.
      lower_mem_update_value(drv, dst_name, mit->second);
      return;
    }
    const bool is_reg  = reg_map_.contains(std::string(dst_name)) && reg_info_.contains(std::string(dst_name));
    const bool is_wire = !is_reg && wire_names_.contains(std::string(dst_name));
    if (is_reg) {
      record(din_key(dst_name), drv, mw);
      record(en_key(dst_name), en_const(true), 1);
    } else if (is_wire) {
      record(din_key(dst_name), drv, mw);
      maybe_bind_wire_shadow(din_key(dst_name), drv, mw);
    } else {
      record(dst_name, drv, mw);
    }
  }

  // Build `(base & ~mask) | (shifted_value & mask)` using an explicitly
  // width-bounded inverse mask. This is the common full-value RMW used by
  // runtime bit and range writes; no dynamic Set_mask cell is required.
  // `inside_mask`: `shifted_value` has no bit outside `mask` already, so it
  // needs no masking of its own.
  [[nodiscard]] Pin lower_dynamic_mask_rmw(const Val& base, const Pin& mask, const Pin& shifted_value, bool inside_mask = false) {
    const int32_t out_mw = std::max<int32_t>(base.mw, 1);
    last_rmw_mask_       = mask;  // a write-masked memory partial write reuses it (lower_set_mask)

    auto inv = make_node(Ntype_op::Xor);
    setup_sink_by_name(inv, "as").connect_driver(mask);
    setup_sink_by_name(inv, "as").connect_driver(create_const(*g_, *Dlop::get_mask_value(out_mw)));
    auto inv_dp = inv.create_driver_pin(0);
    set_ubits(inv_dp, out_mw);

    auto kept = make_node(Ntype_op::And);
    setup_sink_by_name(kept, "as").connect_driver(base.pin);
    setup_sink_by_name(kept, "as").connect_driver(inv_dp);
    auto kept_dp = kept.create_driver_pin(0);
    set_ubits(kept_dp, out_mw);

    auto inserted_dp = shifted_value;
    if (!inside_mask) {
      auto inserted = make_node(Ntype_op::And);
      setup_sink_by_name(inserted, "as").connect_driver(shifted_value);
      setup_sink_by_name(inserted, "as").connect_driver(mask);
      inserted_dp = inserted.create_driver_pin(0);
      set_ubits(inserted_dp, out_mw);
    }

    auto merged = make_node(Ntype_op::Or);
    setup_sink_by_name(merged, "as").connect_driver(kept_dp);
    setup_sink_by_name(merged, "as").connect_driver(inserted_dp);
    auto merged_dp = merged.create_driver_pin(0);
    set_ubits(merged_dp, out_mw);
    return merged_dp;
  }

  // The declared type a bit write lands in: the base variable's, the
  // set_mask destination's, and that of the name the result is stored to next
  // (`store(q___ssa_1, <dst>)`; a base constprop folded to a literal names no
  // variable). The widest width; signed when any of them is.
  [[nodiscard]] std::optional<Decl_type> bit_write_decl(const Lnast_nid& set_mask_nid, const Lnast_nid& dst,
                                                        const Lnast_nid& val) const {
    std::optional<Decl_type> out;
    auto                     widen = [&](std::string_view name) {
      if (const auto dt = decl_type_lookup(name)) {
        out = Decl_type{std::max(out ? out->mw : 0, dt->mw), (out && out->is_signed) || dt->is_signed};
      }
    };
    if (Lnast_ntype::is_ref(lnast_->get_type(val))) {
      widen(lnast_->get_name(val));
    }
    widen(lnast_->get_name(dst));
    if (const auto next = lnast_->get_sibling_next(set_mask_nid);
        !next.is_invalid() && Lnast_ntype::is_store(lnast_->get_type(next))) {
      const auto sdst = lnast_->get_first_child(next);
      const auto ssrc = sdst.is_invalid() ? sdst : lnast_->get_sibling_next(sdst);
      if (!ssrc.is_invalid() && lnast_->get_sibling_next(ssrc).is_invalid() && Lnast_ntype::is_ref(lnast_->get_type(sdst))
          && Lnast_ntype::is_ref(lnast_->get_type(ssrc)) && lnast_->get_name(ssrc) == lnast_->get_name(dst)) {
        widen(lnast_->get_name(sdst));
      }
    }
    return out;
  }

  // The base of a RUNTIME-index/range write (and of a constant-position write
  // into a signed scalar), widened to the variable's DECLARED width
  // (bit_write_decl). The RMW keeps only `out_mw` bits (its inverse mask is
  // `out_mw` ones), so a constant base narrower than its variable
  // (`mut q:u8 = 0` is a 1-bit literal) would size the first write's result
  // to that literal, and the second write's clear mask to its low bit only:
  // `q#[i] = e; q#[j] = f` then kept bit j of the first write set.
  // `is_signed` reports a signed declared type: the RMW itself is unsigned
  // bit logic. `reach` is the write's highest bit + 1 (0 = unknown). An
  // UNDECLARED base (`mut q = 0`, whose literal is one bit) is an unbounded
  // integer, so it widens to the reach instead -- sized to the literal,
  // `q#[i] = e; q#[j] = f` cleared bit j with a one-bit inverse mask and kept
  // the first write's bit set. One that can be negative gets a bit more and
  // reads back signed: its bits above the reach are its sign.
  [[nodiscard]] Val dynamic_update_base(const Lnast_nid& set_mask_nid, const Lnast_nid& dst, const Lnast_nid& val, bool& is_signed,
                                        int32_t reach = 0) {
    auto          base   = set_mask_base(val, set_mask_nid);
    const int32_t own_mw = base.mw;
    const auto    decl   = bit_write_decl(set_mask_nid, dst, val);
    is_signed            = decl && decl->is_signed;
    if (decl) {
      base.mw = std::max(base.mw, decl->mw);
    } else if (reach > 0) {
      is_signed = pin_can_be_negative(base.pin);
      base.mw   = std::max(base.mw, is_signed ? reach + 1 : reach);
    }
    // A narrower signed base enters the RMW as its bit pattern at the full
    // width: the RMW is unsigned bit logic, where it would zero-extend. A
    // negative literal folds to that pattern; a signed value (`mut q:s8 = x`
    // with `x:s4`) is sign-extended, then read unsigned.
    if (Lnast_ntype::is_const(lnast_->get_type(val))) {
      if (auto v = Dlop::from_pyrope(lnast_->get_name(val)); v && v->is_integer() && v->is_negative()) {
        base.pin = create_const(*g_, *v->and_op(*Dlop::get_mask_value(base.mw)));
      }
    } else if (own_mw < base.mw && pin_can_be_negative(base.pin)) {
      auto sx = make_node(Ntype_op::Sext);
      setup_sink_by_name(sx, "a").connect_driver(base.pin);
      setup_sink_by_name(sx, "b").connect_driver(create_const(*g_, *Dlop::create_integer(base.mw)));
      auto wide = sx.create_driver_pin(0);
      set_bits(wide, base.mw);
      set_sign(wide);
      auto gm = make_node(Ntype_op::Get_mask);
      livehd::graph_util::connect_mask_operands(gm, wide, 0, base.mw);
      base.pin = gm.create_driver_pin(0);
      set_ubits(base.pin, base.mw);
    }
    return base;
  }

  // A runtime-index/range RMW result, re-tagged signed at `mw` bits for a
  // signed destination (`mut q:s8 = …; q#[i] = e; q < 0`).
  [[nodiscard]] Pin dynamic_update_result(const Pin& merged, int32_t mw, bool is_signed) {
    if (!is_signed) {
      return merged;
    }
    auto sx = make_node(Ntype_op::Sext);
    setup_sink_by_name(sx, "a").connect_driver(merged);
    setup_sink_by_name(sx, "b").connect_driver(create_const(*g_, *Dlop::create_integer(mw)));
    auto out = sx.create_driver_pin(0);
    set_bits(out, mw);
    set_sign(out);
    return out;
  }

  // `dst#[lo..=hi] = value` with runtime endpoints. The language operation
  // stays a range write through LNAST; tolg materializes its hardware as one
  // packed RMW. Later SROA can distribute the resulting value over leaves.
  void lower_dynamic_range_update(const Lnast_nid& dst, const Lnast_nid& val, const Lnast_nid& ins, const Lnast_nid& lo,
                                  const Lnast_nid& hi, const Lnast_nid& loc_nid) {
    auto          n         = leaf(lo);
    auto          m         = range_upper(n, hi);
    auto          iv        = leaf(ins);
    // The write reaches bit hi_max: `hi` fits its `mw` bits (one fewer when it
    // can be negative). Past 2^12 bits the reach is capped the way a runtime
    // SHL's growth is (OpW::shlw), which also sizes a bit write's one-hot mask
    // (lower_dynamic_bit_update): an undeclared base must never keep its own
    // (literal) width, or a second write's inverse mask is too narrow.
    const int32_t hi_bits   = std::max(pin_can_be_negative(m.pin) ? m.mw - 1 : m.mw, 0);
    bool          is_signed = false;
    auto          base      = dynamic_update_base(loc_nid, dst, val, is_signed, hi_bits <= 12 ? int32_t{1} << hi_bits : 4097);
    const int32_t mw        = std::max<int32_t>(base.mw, 1);

    auto shifted = make_node(Ntype_op::SHL);
    setup_sink_by_name(shifted, "a").connect_driver(iv.pin);
    setup_sink_by_name(shifted, "b").connect_driver(n.pin);
    auto shifted_dp = shifted.create_driver_pin(0);
    set_ubits(shifted_dp, mw);

    // A window of constant width W (`hi` is `lo + W`): its mask is a
    // constant run shifted into place, it is never reversed (no runtime
    // check), and a value of at most W non-negative bits already stays inside
    // it once shifted.
    if (const auto fixed = const_window_offset(m.pin, n.pin)) {
      const int64_t w     = *fixed;
      auto          maskn = make_node(Ntype_op::SHL);
      setup_sink_by_name(maskn, "a").connect_driver(create_const(*g_, *Dlop::get_mask_value(static_cast<int>(w))));
      setup_sink_by_name(maskn, "b").connect_driver(n.pin);
      auto mask_dp = maskn.create_driver_pin(0);
      set_ubits(mask_dp, mw);
      const bool inside = !pin_can_be_negative(iv.pin) && iv.mw > 0 && iv.mw <= w;
      auto       merged = dynamic_update_result(lower_dynamic_mask_rmw(base, mask_dp, shifted_dp, inside), mw, is_signed);
      record_set_mask_result(lnast_->get_name(dst), merged, mw);
      return;
    }

    // width = hi - lo, clamped to 0 on a reversed range so the mask comes
    // out 0 and the RMW leaves `dst` untouched. The clamped value is
    // non-negative by construction, so the shift amounts below never see a
    // negative-as-unsigned count.
    auto rw = lower_range_width(n, m);

    auto pow = make_node(Ntype_op::SHL);
    setup_sink_by_name(pow, "a").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    setup_sink_by_name(pow, "b").connect_driver(rw.clamped);
    auto pow_dp = pow.create_driver_pin(0);
    set_ubits(pow_dp, std::max<int32_t>(base.mw + 1, 2));

    auto low_mask = make_node(Ntype_op::Sum);
    setup_sink_by_name(low_mask, "as").connect_driver(pow_dp);
    setup_sink_by_name(low_mask, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    auto low_mask_dp = low_mask.create_driver_pin(0);
    set_ubits(low_mask_dp, std::max<int32_t>(base.mw + 1, 2));

    auto maskn = make_node(Ntype_op::SHL);
    setup_sink_by_name(maskn, "a").connect_driver(low_mask_dp);
    setup_sink_by_name(maskn, "b").connect_driver(n.pin);
    auto mask_dp = maskn.create_driver_pin(0);
    set_ubits(mask_dp, mw);

    auto merged = dynamic_update_result(lower_dynamic_mask_rmw(base, mask_dp, shifted_dp), mw, is_signed);
    record_set_mask_result(lnast_->get_name(dst), merged, mw);
    lower_range_assert(rw.reversed, loc_nid);
  }

  // set_mask(ref(dst), value, mask, ins).
  // Carries the old-value chain of a memory partial write (mem_rmw_reads_)
  // from `val` to the temp `dst`: a set_mask over the chain merges the written
  // lane (`merges`), a get_mask only narrows an already merged value.
  void extend_mem_rmw_read(const Lnast_nid& dst, const Lnast_nid& val, bool merges) {
    if (!Lnast::is_tmp(lnast_->get_name(dst)) || !Lnast_ntype::is_ref(lnast_->get_type(val))) {
      return;
    }
    auto rit = mem_rmw_reads_.find(lnast_->get_name(val));
    if (rit == mem_rmw_reads_.end() || (!merges && !rit->second.merged)) {
      return;
    }
    auto rmw   = rit->second;  // insert_or_assign may rehash
    rmw.merged = true;
    mem_rmw_reads_.insert_or_assign(std::string(lnast_->get_name(dst)), std::move(rmw));
  }

  void lower_set_mask(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto val = lnast_->get_sibling_next(dst);
    if (val.is_invalid()) {
      return;
    }
    auto ins = lnast_->get_sibling_next(val);
    auto lo  = lnast_->get_sibling_next(ins);
    auto hi  = lnast_->get_sibling_next(lo);
    I(!lo.is_invalid());
    extend_mem_rmw_read(dst, val, /*merges=*/true);
    // Over the zero base of a write-masked memory partial write (lower_tuple_get)
    // the result is just the written bits; record which bits those are.
    std::optional<Masked_pw_base> pw_base;
    if (Lnast_ntype::is_ref(lnast_->get_type(val))) {
      if (auto pit = masked_pw_bases_.find(lnast_->get_name(val)); pit != masked_pw_bases_.end()) {
        pw_base = pit->second;
      }
    }
    last_rmw_mask_     = Pin{};
    auto note_pw_write = [&](const Pin& written, std::optional<Dlop> cmask) {
      if (!pw_base || (written.is_invalid() && !cmask)) {
        return;
      }
      masked_pw_vals_.insert_or_assign(std::string(lnast_->get_name(dst)),
                                       Masked_pw_val{.base = *pw_base, .mask = written, .cmask = std::move(cmask)});
    };
    if (!hi.is_invalid() && lnast_->get_name(hi) == "nil") {
      error_at(nid,
               {"open-range-write", "unsupported"},
               "open-ended bit-range write is not supported — give the upper bound explicitly");
    }
    if (!Lnast_ntype::is_const(lnast_->get_type(lo)) || (!hi.is_invalid() && !Lnast_ntype::is_const(lnast_->get_type(hi)))) {
      lower_dynamic_range_update(dst, val, ins, lo, hi, nid);
      note_pw_write(last_rmw_mask_, std::nullopt);
      return;
    }
    auto l = Dlop::from_pyrope(lnast_->get_name(lo));
    auto h = hi.is_invalid() ? l->add_op(*Dlop::create_integer(1)) : Dlop::from_pyrope(lnast_->get_name(hi));
    if (!l->is_just_i64() || !h->is_just_i64() || l->to_just_i64() < 0 || h->to_just_i64() <= l->to_just_i64()
        || h->to_just_i64() > std::numeric_limits<int>::max()) {
      error_at(nid, {"bit-range-invalid", "type"}, "invalid set_mask endpoints");
    }
    const int low = l->to_just_i64(), high = h->to_just_i64();
    if (pw_base) {
      note_pw_write(Pin{}, *Dlop::get_mask_value(high - 1, low));
    }

    // A partial bit-range WRITE of a declared reg/wire is a next-state
    // read-modify-write: the destination NAME stays bound to q (a reg) / the
    // buffer output (a wire) so OTHER reads keep Verilog `<=` semantics, while
    // the masked result accumulates on the SHADOW din key. Without this the old
    // `bind_result(dst)` rebound the name to the combinational set_mask result:
    // the reg/wire din was never set (finalize_regs then wired din=q, an
    // identity `___next = q`), so EVERY partial write (`r.field <= …`,
    // `r[hi:lo] <= …`) was silently dropped. The din shadow also lets a chain
    // of partial writes ACCUMULATE: the 2nd `r[..]<=` must read the 1st write's
    // result, not q — so when the base operand is the destination itself, read
    // the current din accumulator (if any) instead of resolving the name to q.
    const std::string dst_name{lnast_->get_name(dst)};
    // RMW base = current din accumulator if a prior partial write set it, else
    // the committed value (q / buffer output) via the name. This must key off
    // the BASE OPERAND's name, not just dst: prp2lnast lowers `r#[lo..=hi] = x`
    // to a copy-temp shape (`set_mask %t, r, mask, x` + `r = %t`), so dst is a
    // TEMP while the read of `r` still needs the accumulated value — resolving
    // it to q made the SECOND conditional partial write rebuild its whole-word
    // image from stale q and silently drop the first write when both enables
    // fired (the DataModule__64entry per-entry register file: entry 63's write
    // vanished under a same-cycle entry-62 write).
    //
    // A write into a SIGNED scalar (`mut v:s8`, an `s8` output) changes bits
    // of the variable's value: the base enters at the declared width (a
    // narrower or negative base sign-extends) and the result reads back
    // signed, exactly like the runtime-position RMW. Without it `o#[4..<8] =
    // b; o < 0` compared the unsigned bit pattern and was never true.
    bool              is_signed = false;
    auto vv = signed_scalar_bit_write(nid, dst, val) ? dynamic_update_base(nid, dst, val, is_signed) : set_mask_base(val, nid);

    auto node = make_node(Ntype_op::Set_mask);
    livehd::graph_util::connect_mask_operands(node, vv.pin, low, high, leaf(ins).pin);
    int32_t mask_mw = high;
    auto    drv     = node.create_driver_pin(0);
    int32_t res_mw  = std::max(vv.mw, mask_mw);
    set_ubits(drv, res_mw);
    record_set_mask_result(dst_name, dynamic_update_result(drv, res_mw, is_signed), res_mw);
  }

  // Whether a bit write lands in a plain SIGNED scalar (a local, an output, a
  // tuple leaf). A reg, wire or array keeps its own declared view of the bits
  // it accumulates.
  [[nodiscard]] bool signed_scalar_bit_write(const Lnast_nid& set_mask_nid, const Lnast_nid& dst, const Lnast_nid& val) const {
    if (Lnast_ntype::is_ref(lnast_->get_type(val))) {
      const std::string_view raw = lnast_->get_name(val);
      for (const std::string& key : {std::string(raw), std::string(canon_io_name(raw)), logical_key(raw)}) {
        if (reg_map_.contains(key) || wire_names_.contains(key) || mem_map_.contains(key) || array_scalar_views_.contains(key)) {
          return false;
        }
      }
    }
    const auto decl = bit_write_decl(set_mask_nid, dst, val);
    return decl && decl->is_signed;
  }

  // ── concat ───────────────────────────────────────────────────────────────
  //
  // `concat( dst, v_msb, w_msb, …, v_lsb, w_lsb )` lowers 1:1 to one
  // Ntype_op::Concat: the LNAST node ALREADY carries the interleaved
  // (value, width) shape, and the cell's sinks are the same pairs at pids
  // 2i / 2i+1, MSB-first.
  //
  // A width operand may still be the `nil` sentinel here, meaning no upass pass
  // could bind it from the lane's declared type. That is a HARD ERROR, never a
  // guess: mw_lookup() would happily hand back the live value's width (or its
  // default of 1), and the value's width is precisely what a concat may not be
  // sized by -- narrowing one lane shifts every lane above it.
  //
  // This is also the LAST place the destination's declared width is checked.
  // The concat's own dst is a compiler temp, so the user-facing `c:u12 = …`
  // check rides where that temp is BOUND to a declared name; see
  // check_concat_dest_width, called from the declare/store paths.

  // The bound width of one lane's width operand, or nullopt when it is `nil`
  // (or otherwise not a positive comptime integer).
  [[nodiscard]] std::optional<int32_t> concat_bound_width(const Lnast_nid& nid) const {
    if (nid.is_invalid() || !Lnast_ntype::is_const(lnast_->get_type(nid))) {
      return std::nullopt;
    }
    const auto txt = lnast_->get_name(nid);
    if (txt.empty() || txt == "nil") {
      return std::nullopt;
    }
    auto v = Dlop::from_pyrope(txt);
    if (!v || !v->is_integer() || !v->is_just_i64()) {
      return std::nullopt;
    }
    const auto w = v->to_just_i64();
    if (w <= 0 || w > std::numeric_limits<int32_t>::max()) {
      return std::nullopt;
    }
    return static_cast<int32_t>(w);
  }

  // A concat lane that names a DECLARED ARRAY: its (entry count, element
  // width), or nullopt when the lane is not one. Both array storage classes
  // answer here — a `mut`/`const` comb array is an Array_scalar_view and a
  // `reg` array is a Memory — because both carry the declared extent and a
  // single scalar element width.
  //
  // The entry count is the FLAT one (`[4][8]u8` splices 32 windows), which is
  // what both records already hold.
  [[nodiscard]] std::optional<std::pair<int64_t, int32_t>> concat_array_extent(const Lnast_nid& v) const {
    if (v.is_invalid() || !Lnast_ntype::is_ref(lnast_->get_type(v))) {
      return std::nullopt;
    }
    // The packed bus is size*elem_mw bits and every downstream width here is
    // int32, so an extent whose product does not fit is declined rather than
    // truncated -- a wrapped packed_mw would silently mis-slice every entry.
    auto extent = [](int64_t size, int32_t elem_mw) -> std::optional<std::pair<int64_t, int32_t>> {
      if (size <= 0 || elem_mw <= 0 || size > std::numeric_limits<int32_t>::max() / elem_mw) {
        return std::nullopt;
      }
      return std::pair<int64_t, int32_t>{size, elem_mw};
    };
    const std::string name{lnast_->get_name(v)};
    if (auto it = array_scalar_views_.find(name); it != array_scalar_views_.end()) {
      if (auto e = extent(it->second.size, it->second.elem_mw)) {
        return e;
      }
    }
    if (auto it = mem_map_.find(name); it != mem_map_.end()) {
      if (auto e = extent(it->second.size, it->second.elem_mw)) {
        return e;
      }
    }
    return std::nullopt;
  }

  // Entry `index` of a packed array bus: the elem_mw-bit window at
  // index*elem_mw. The raw UNSIGNED bit pattern is what a concat lane wants —
  // a signed element still occupies exactly its own elem_mw bits, the same rule
  // that makes a Verilog concat operand self-determined — so no sign extension
  // rides along (unlike the element READ in lower_tuple_get, whose result feeds
  // an arithmetic consumer).
  [[nodiscard]] Pin array_entry_slice(const Pin& packed, int32_t packed_mw, int64_t index, int32_t elem_mw) {
    Pin src = packed;
    if (index > 0) {
      auto sra = make_node(Ntype_op::SRA);
      setup_sink_by_name(sra, "a").connect_driver(packed);
      setup_sink_by_name(sra, "b").connect_driver(create_const(*g_, *Dlop::create_integer(index * elem_mw)));
      src = sra.create_driver_pin(0);
      set_ubits(src, packed_mw);
    }
    auto gm = make_node(Ntype_op::Get_mask);
    livehd::graph_util::connect_mask_operands(gm, src, 0, elem_mw);
    auto out = gm.create_driver_pin(0);
    set_ubits(out, elem_mw);  // an unsized pin emits as ONE bit
    return out;
  }

  void lower_concat(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }

    // Resolve every lane FIRST: a bad lane must abort before any cell is
    // minted, so a rejected concat leaves no half-wired node behind.
    struct Lane {
      Lnast_nid nid;
      int32_t   width;    // one window
      int64_t   entries;  // 0 = a scalar lane; >0 = SPLICE that many windows of an array
    };
    std::vector<Lane> lanes;
    size_t            n_windows = 0;
    for (auto v = lnast_->get_sibling_next(dst); !v.is_invalid();) {
      auto w = lnast_->get_sibling_next(v);
      if (w.is_invalid()) {
        error_at(nid,
                 {"concat-malformed", "internal"},
                 "upass.tolg: concat lane '{}' has no width operand — every lane is a (value, width) PAIR",
                 lnast_->get_name(v));
      }
      if (const auto bound = concat_bound_width(w)) {
        lanes.push_back(Lane{v, *bound, 0});
        n_windows += 1;
      } else if (const auto ext = concat_array_extent(v)) {
        // An ARRAY lane splices its entries, entry 0 LEAST significant — the
        // positional-tuple rule (docs/pyrope/10-internals.md, "Bit selection
        // and packing"). It occupies no SINGLE window, which is why its own
        // width operand is still the `nil` sentinel and why this arm sits
        // ahead of the untyped-lane error rather than after it.
        //
        // Only a Pyrope array reaches here: concat_array_extent matches a
        // DECLARED array ref, and an unpacked array is not a legal Verilog
        // concatenation operand — so flipping this order cannot disturb a
        // Verilog-origin concat.
        lanes.push_back(Lane{v, ext->second, ext->first});
        n_windows += static_cast<size_t>(ext->first);
      } else {
        error_at(nid,
                 {"concat-untyped-lane", "type"},
                 "upass.tolg: concat lane '{}' has no declared bit width — a concat window is sized by the lane's "
                 "DECLARED type, never by its value or an inferred range, because narrowing one lane would shift "
                 "every lane above it (bind it to a typed name first: `const w:U4 = <expr>`)",
                 lnast_->get_name(v));
      }
      v = lnast_->get_sibling_next(w);
    }
    if (lanes.empty() || n_windows == 0) {
      error_at(nid, {"concat-empty", "type"}, "upass.tolg: concat needs at least one lane");
    }

    auto          node   = make_node(Ntype_op::Concat);
    int32_t       sum_mw = 0;
    hhds::Port_id pid    = 0;
    for (const auto& l : lanes) {
      if (l.entries == 0) {
        auto v = leaf(l.nid);
        node.create_sink_pin(pid++).connect_driver(v.pin);
        node.create_sink_pin(pid++).connect_driver(create_const(*g_, *Dlop::create_integer(l.width)));
        sum_mw += l.width;
        continue;
      }
      // The packed bus already holds entry 0 in the LOW element window
      // (graph/cell.cpp pid 12, and the layout Array_scalar_view's indexed read
      // assumes), and a spliced array keeps exactly that order, so walking the
      // entries DOWN the bus emits them MSB-first into this MSB-first lane
      // list. The splice is a COPY of the packed word, not a per-entry
      // reversal of it.
      auto          packed    = leaf(l.nid);
      const int32_t packed_mw = static_cast<int32_t>(l.entries * l.width);
      for (int64_t e = l.entries - 1; e >= 0; --e) {
        node.create_sink_pin(pid++).connect_driver(array_entry_slice(packed.pin, packed_mw, e, l.width));
        node.create_sink_pin(pid++).connect_driver(create_const(*g_, *Dlop::create_integer(l.width)));
        sum_mw += l.width;
      }
    }

    auto out = node.create_driver_pin(0);
    // The assembled value is always NON-NEGATIVE, so bind_result stamps the
    // exact literal sum(w_i) width as unsigned.
    bind_result(lnast_->get_name(dst), out, sum_mw);
    // The result has a declared width BY CONSTRUCTION, which is what makes
    // `concat(concat(a,b), c)` legal and what the destination check compares
    // a declared `c:u12` against.
    record_decl_type(lnast_->get_name(dst), sum_mw, /*is_signed=*/false);
    concat_result_mw_[logical_key(lnast_->get_name(dst))] = sum_mw;
  }

  // `const c:u12 = concat(a:u4, b:u8)` — the destination's declared width must
  // equal the lane sum EXACTLY. Not `>=`: a concat states a bit layout, and a
  // destination that quietly zero-extends it is a layout the source does not
  // say. Signedness is free (`u12` and `s12` are both 12-bit fields), so only
  // the width is compared.
  //
  // Checked where the concat's TEMP is bound to a declared name, because the
  // concat node's own dst is always a compiler temp.
  void check_concat_dest_width(const Lnast_nid& anchor, std::string_view dest_name, const Lnast_nid& value_nid) {
    // Pyrope only -- see the twin guard in upass.runner. Verilog declares its
    // widths its own way and its assignment rules pad/truncate rather than
    // reject, so the Pyrope "destination states the layout" rule would refuse
    // ordinary imported RTL.
    if (lnast_->is_verilog_origin()) {
      return;
    }
    if (value_nid.is_invalid() || !Lnast_ntype::is_ref(lnast_->get_type(value_nid))) {
      return;
    }
    auto cit = concat_result_mw_.find(logical_key(lnast_->get_name(value_nid)));
    if (cit == concat_result_mw_.end()) {
      return;  // not a concat result
    }
    // Only a SOURCE-level destination is checked -- the same exemption
    // upass.runner's twin (check_concat_dest) documents. A store into a
    // COMPILER TEMP (`___N`, an SSA staging name a frontend minted) is an
    // internal move, not a declaration the user wrote: demanding a declared
    // type of it would turn a frontend's own staging store into a hard
    // `concat-untyped-dest` error on legal source.
    if (const auto dkey = logical_key(dest_name); dkey.empty() || dkey.starts_with("___")) {
      return;
    }
    auto dt = decl_type_lookup(dest_name);
    if (!dt) {
      error_at(anchor,
               {"concat-untyped-dest", "type"},
               "upass.tolg: '{}' is assigned a concat but has no declared type — a concat's destination must declare "
               "the {}-bit width the lanes add up to (e.g. `{}:U{}` or `{}:S{}`)",
               dest_name,
               cit->second,
               dest_name,
               cit->second,
               dest_name,
               cit->second);
    }
    if (dt->mw != cit->second) {
      error_at(anchor,
               {"concat-width-mismatch", "type"},
               "upass.tolg: '{}' is declared {} bits but the concat assigned to it is {} bits — a concat's "
               "destination must match the lane sum EXACTLY, so that the bit layout the source states is the layout "
               "the destination has",
               dest_name,
               dt->mw,
               cit->second);
    }
  }

  enum class OpW { add, mul, maxw, andw, firstw, boolw, shlw };

  // n-ary op: child0 = dst, children 1..N = operands. Commutative ops feed all
  // operands into sink "a"; positional binary ops use "a" then "b".
  void lower_op(const Lnast_nid& nid, Ntype_op op, bool commutative, OpW wmode) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto    node           = make_node(op);
    int32_t max_mw         = 0;
    int32_t sum_mw         = 0;
    int32_t min_nonneg_mw  = 0;      // andw: narrowest NON-NEGATIVE operand...
    bool    any_nonneg     = false;  // ...which is only meaningful once this is set (mw 0 is legal)
    bool    any_negative   = false;  // at least one operand may carry a negative value
    int32_t signed_mw      = 0;      // width needed if the result must use a signed carrier
    bool    first_negative = false;  // shifts take their result sign from the value operand
    int32_t first_mw       = 0;
    int32_t second_mw      = 0;   // shift-amount magnitude width (shlw)
    int64_t shl_amt        = -1;  // shift-amount value when constant (shlw); <0 = dynamic
    bool    first          = true;
    int     opnd_idx       = 0;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      auto v        = leaf(c);
      max_mw        = std::max(max_mw, v.mw);
      sum_mw       += v.mw;
      any_negative  = any_negative || pin_can_be_negative(v.pin);
      // A literal uW operand needs W+1 bits when represented in a signed
      // carrier; an sW operand already includes its sign bit.  Bitwise ops
      // become signed when any operand may be negative, so remember the
      // widest lossless signed representation while walking the inputs.
      signed_mw     = std::max(signed_mw, v.mw + (pin_can_be_negative(v.pin) ? 0 : 1));
      if (wmode == OpW::andw && v.pin.is_const()) {
        // A bitwise AND is bounded by its NARROWEST NON-NEGATIVE operand: `x & m`
        // can only keep bits that `m` has set, so the result never exceeds m --
        // whatever x is, and however wide.
        //
        // Only a NON-NEGATIVE operand bounds it. A negative one is an infinite
        // run of leading ones (`x & -2` keeps every bit of x but bit 0), so its
        // magnitude width bounds nothing and `min` there would truncate.
        //
        // CONSTANTS ONLY, deliberately. A non-const operand's `is_unsign` stamp
        // is NOT a proof of non-negativity: bind_result stamps every computed op
        // unsigned, and the OPEN note at the end of this function records that a
        // BITWISE op over signed operands is stamped unsigned while its value is
        // negative (`sa ^ sb` can be -1). Narrowing an AND to such an operand's
        // magnitude width truncates the OTHER operand -- `(-1) & w` must be w,
        // but a 5-bit stamp keeps only w's low 5 bits, a silent miscompile that
        // OpW::maxw did not have. Trusting the stamp measured just 1.1 points
        // better on minion (-16.2% vs -15.1% of op words); the win is dominated
        // by the constant masks, which are exact.
        //
        // Test the lowered PIN rather than the LNAST leaf kind: cprop can prove
        // a reference temporary constant before tolg, and that constant is just
        // as valid a mask. The value comes from that pin, so there is no second
        // Dlop parse.
        const auto& cv = livehd::graph_util::const_of(v.pin);
        if (cv.is_numeric() && !cv.is_negative() && (!any_nonneg || v.mw < min_nonneg_mw)) {
          min_nonneg_mw = v.mw;
          any_nonneg    = true;
        }
      }
      if (first) {
        first_mw       = v.mw;
        first_negative = pin_can_be_negative(v.pin);
      } else if (second_mw == 0) {
        second_mw = v.mw;
        if (Lnast_ntype::is_const(lnast_->get_type(c))) {
          // Read back the const pin leaf() built, rather than re-parsing the
          // same literal text a second time.
          const auto& cv = livehd::graph_util::const_of(v.pin);
          if (cv.is_just_i64()) {
            shl_amt = cv.to_just_i64();
          }
        }
      }
      // SHL b is single-driver: the runtime one-hot `a << (b0, b1, …)` form was
      // removed (comptime cases fold in upass.constprop before reaching tolg).
      // A 3rd+ operand would build a multi-driver b, so reject it cleanly.
      if (op == Ntype_op::SHL && opnd_idx >= 2) {
        livehd::diag::err("upass.tolg", "shl-onehot-removed", "unsupported")
            .msg(
                "runtime one-hot shift 'a << (b0, b1, ...)' is no longer "
                "supported; shift by a single amount")
            .emit();
        break;
      }
      // op varies (Sum/Mult/And/.../Div/SHL/SRA): address by pid so the right
      // sink name resolves per op (pid 0 = a/as, pid 1 = b) without hardcoding.
      livehd::graph_util::setup_sink_pid(node, (commutative || first) ? 0 : 1).connect_driver(v.pin);
      first = false;
      ++opnd_idx;
    }
    int32_t mw = 1;
    switch (wmode) {
      case OpW::add   : mw = static_cast<int32_t>(max_mw + 1); break;
      case OpW::mul   : mw = sum_mw > 0 ? sum_mw : int32_t{1}; break;
      case OpW::maxw  : mw = max_mw; break;
      // AND narrows: bounded by the narrowest non-negative operand when there is
      // one, else (no operand proven non-negative) it keeps the widest.
      case OpW::andw  : mw = any_nonneg ? min_nonneg_mw : max_mw; break;
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
    auto out = node.create_driver_pin(0);
    bind_result(lnast_->get_name(dst), out, mw);
    // A subtraction can go negative regardless of operand signs. An addition
    // can go negative whenever at least one operand can. In either case the
    // arithmetic carrier is signed; only an explicit mask/cast may turn its
    // finite low-bit projection into an unsigned value.
    if (op == Ntype_op::Sum && ((!commutative && opnd_idx >= 2) || any_negative)) {
      set_sbits(out, std::max<int32_t>(1, mw));
    }
    if ((op == Ntype_op::Mult || op == Ntype_op::Div) && any_negative) {
      set_sbits(out, std::max<int32_t>(1, mw));
    }
    if ((op == Ntype_op::SHL || op == Ntype_op::SRA) && first_negative) {
      set_sbits(out, std::max<int32_t>(1, mw));
    }
    // Or/Xor preserve the infinite leading ones of a negative operand. And does
    // too only when every operand may be negative: one proven non-negative
    // operand is a finite mask and bounds the result to its own unsigned width.
    // Keep the unbounded cases signed at the widest lossless signed width.
    if ((op == Ntype_op::Or || op == Ntype_op::Xor || (op == Ntype_op::And && !any_nonneg)) && any_negative) {
      set_sbits(out, std::max<int32_t>(1, signed_mw));
    }
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
      error_at(b,
               {"sext-runtime-pos", "unsupported"},
               "upass.tolg: sign-extend with a runtime sign position has no "
               "lowering — the sign-bit position must be a "
               "compile-time constant");
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

  // `~x` — bitwise NOT (the only unary lowered through here).
  //
  // An unsigned `mw`-bit operand ranges through 2^mw-1. Flipping its unlimited
  // leading zeros makes the result negative — range
  // [-(2^mw), -1] — and has to be stamped SIGNED across all mw+1 bits, the same
  // shape lower_sext() uses for its signed result.
  //
  // Binding it through bind_result() stamped it UNSIGNED instead. Consumers
  // then disagreed about the required sign extension: the LEC read one bit too few, and abc
  // zero-filled a widening that had to sign-extend — `(~ec)#[0..=9]` mapped to
  // 511 where the RTL says 1023, i.e. a genuinely wrong netlist. (cgen emits an
  // explicitly signed net and so stayed correct, which is what made this look
  // like an abc-only bug.)
  //
  // The TYPED form `bit_not(dst, x, N)` -- the runner's (or a Verilog reader's)
  // `~` of an UNSIGNED N-bit value, user ruling 26 -- flips only those N bits.
  // There is no separate LGraph cell for it: the same `Not`, then a `Get_mask`
  // of the low N bits, the unsigned uN result. A plain (signed/untyped) `~x`
  // stays a bare `Not`.
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
    const int32_t m     = v.mw > 0 ? v.mw : 1;
    const int32_t out_m = pin_can_be_negative(v.pin) ? m : m + 1;
    auto          drv   = node.create_driver_pin(0);
    set_sbits(drv, out_m);
    const auto width = lnast_->get_sibling_next(a);
    if (op != Ntype_op::Not || width.is_invalid()) {
      record(lnast_->get_name(dst), drv, out_m);
      return;
    }
    int64_t bits = 0;
    if (Lnast_ntype::is_const(lnast_->get_type(width))) {
      if (const auto n = Dlop::from_pyrope(lnast_->get_name(width)); n && n->is_just_i64()) {
        bits = n->to_just_i64();
      }
    }
    if (bits <= 0 || bits > std::numeric_limits<int>::max()) {
      error_at(nid,
               {"bad-bitnot-width", "internal"},
               "upass.tolg: a typed `~` needs a positive constant bit count, got '{}'",
               lnast_->get_name(width));
    }
    auto gm = make_node(Ntype_op::Get_mask);
    livehd::graph_util::connect_mask_operands(gm, drv, 0, static_cast<int>(bits));
    bind_result(lnast_->get_name(dst), gm.create_driver_pin(0), static_cast<int32_t>(bits));
  }

  // `!x` is a truth-value negation, not the signed bitwise `~x` operation.
  // Pyrope conditions may carry a wider integer, so compare against zero;
  // XOR-with-one is equivalent only after a separate u1 proof.
  void lower_log_not(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto v   = leaf(a);
    auto neg = make_node(Ntype_op::EQ);  // commutative: both operands feed sink "a"
    setup_sink_by_name(neg, "as").connect_driver(v.pin);
    setup_sink_by_name(neg, "as").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    bind_result(lnast_->get_name(dst), neg.create_driver_pin(0), 1);
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
      // inner_op is EQ/GT/LT; address by pid (0 = a/as, 1 = bs) so the right
      // multi-driver sink name resolves per op without hardcoding.
      livehd::graph_util::setup_sink_pid(inner, (commutative || first) ? 0 : 1).connect_driver(leaf(c).pin);
      first = false;
    }
    // The inner comparator (EQ/GT/LT) is a literal one-bit unsigned boolean.
    // Without this the inner
    // driver pin is left at bits==0 and leaks an unbounded cell past tolg into
    // cprop/cgen (e.g. slang `!=` lowering to ~(a==b)).
    auto inner_dp = inner.create_driver_pin(0);
    set_ubits(inner_dp, 1);
    // Logical negation of a u1 is XOR with one.
    auto neg = make_node(Ntype_op::Xor);  // commutative: both operands feed sink "a"
    setup_sink_by_name(neg, "as").connect_driver(inner_dp);
    setup_sink_by_name(neg, "as").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    bind_result(lnast_->get_name(dst), neg.create_driver_pin(0), 1);
  }

  // ── Bit-insensitive reductions (`foo#|/&/^/+[range]`)
  // ────────────────────────
  //
  // prp2lnast lowers every `foo#OP[range]` to a `get_mask` (which packs the
  // selected bits LSB-first into an unsigned slice `rr` of width
  // popcount(mask)) followed by the reduction node over `rr`. A comptime `foo`
  // folds in constprop; only a RUNTIME `foo` reaches tolg here, so the
  // reduction's single operand is always the already-lowered get_mask result,
  // whose magnitude width
  // (`av.mw`) is the selected-bit count `k`. An open `#[..]` masks every bit,
  // so `k` is then `foo`'s full width — the "use the type's number of bits"
  // rule.

  // `foo#|[range]`: OR-reduce the selected bits → int 0/1. The graph Ror cell
  // reduces every bit of its operand (cgen emits `|expr`).
  void lower_red_or(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto av   = leaf(a);
    auto node = make_node(Ntype_op::Ror);
    setup_sink_by_name(node, "as").connect_driver(av.pin);
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), 1);
  }

  // `foo#&[range]`: AND-reduce → int 0/1. Sign-extend the packed slice from its
  // top bit so an all-ones slice reads as the signed -1, then compare `== -1`
  // (a width-independent all-ones test; 1 iff every selected bit is set).
  void lower_red_and(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto          av = leaf(a);
    const int32_t k  = av.mw > 0 ? av.mw : 1;  // selected-bit count
    // Sext cell `b` operand is the kept bit COUNT (cgen slices [b-1:0]); the
    // result is signed with width k, == -1 exactly when bits 0..k-1 are all
    // set.
    auto          sx = make_node(Ntype_op::Sext);
    setup_sink_by_name(sx, "a").connect_driver(av.pin);
    setup_sink_by_name(sx, "b").connect_driver(create_const(*g_, *Dlop::create_integer(k)));
    auto srr = sx.create_driver_pin(0);
    set_bits(srr, k);
    set_sign(srr);
    auto eq = make_node(Ntype_op::EQ);  // commutative: both operands feed sink "a"
    setup_sink_by_name(eq, "as").connect_driver(srr);
    setup_sink_by_name(eq, "as").connect_driver(create_const(*g_, *Dlop::create_integer(-1)));
    bind_result(lnast_->get_name(dst), eq.create_driver_pin(0), 1);
  }

  void lower_counted_reduction(const Lnast_nid& nid, Ntype_op op) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    const auto av    = leaf(a);
    const int  count = std::max<int32_t>(av.mw, 1);
    auto       node  = make_node(op);
    setup_sink_by_name(node, "a").connect_driver(av.pin);
    setup_sink_by_name(node, "b").connect_driver(create_const(*g_, *Dlop::create_integer(count)));
    const int width = op == Ntype_op::Rxor ? 1 : std::bit_width(static_cast<unsigned>(count));
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), width);
  }

  void lower_red_xor(const Lnast_nid& nid) { lower_counted_reduction(nid, Ntype_op::Rxor); }
  void lower_popcount(const Lnast_nid& nid) { lower_counted_reduction(nid, Ntype_op::Popcount); }

  // ── `a % b` (modulo) — the easy, unambiguous cases only
  // ───────────────────── Pyrope has no general hardware modulo: the signed
  // semantics differ across languages and a full divider is expensive (docs
  // pyrope/02-basics). LiveHD uses TRUNCATED semantics (the remainder's sign
  // follows the dividend, matching Dlop::rem_op and Verilog `%`). We lower the
  // cases that collapse to shift/mask and HARD-error the rest:
  //   (2) |a| < |b| over their whole ranges          → a            (no
  //   remainder) (1) b a comptime power-of-two, a non-negative   → a & (|b|-1)
  //   (low bits) (3) |b| == 3 (comptime),       a non-negative   → base-4
  //   digit-sum reduce
  // Build the general `a % b` cell. |a % b| <= |a| under truncated semantics, so
  // the dividend's width is always a sound result width. The result sign follows
  // the dividend, which is also why there is exactly ONE remainder op.
  void emit_rem(const std::string& dst_name, const Val& av, const Lnast_nid& b) {
    auto remn = make_node(Ntype_op::Rem);
    setup_sink_by_name(remn, "a").connect_driver(av.pin);
    setup_sink_by_name(remn, "b").connect_driver(leaf(b).pin);
    const int32_t bits = av.mw > 0 ? av.mw : 1;
    auto          out  = remn.create_driver_pin(0);
    bind_result(dst_name, out, bits);
    if (pin_can_be_negative(av.pin)) {
      set_sbits(out, bits);
    }
  }

  void lower_mod(const Lnast_nid& nid) {
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
      error_here("upass.tolg: modulo in '{}' is missing its divisor operand", lnast_->get_top_module_name());
      return;
    }

    const std::string dst_name{lnast_->get_name(dst)};
    auto              av      = leaf(a);
    const auto        a_range = range_of_operand(a);
    const auto        b_range = range_of_operand(b);

    // `a` is non-negative when its pin is unsigned (the common case — every
    // typed unsigned port and every computed result reads unsigned) or its
    // published range proves min >= 0.
    const bool a_nonneg = is_unsign(av.pin) || (a_range && a_range->first >= 0);

    // (2) Range fit. If |a| < |b| for every (a,b) pair then `a % b == a` under
    // truncated semantics (the quotient truncates to 0), regardless of sign.
    if (a_range && b_range && a_range->first != std::numeric_limits<int64_t>::min()
        && a_range->second != std::numeric_limits<int64_t>::min()) {
      const int64_t a_absmax = std::max(iabs64(a_range->first), iabs64(a_range->second));
      // Smallest |b| over b's range. 0 when the range straddles 0 (a possible
      // divisor of 0/±1 is not a guaranteed no-op), which fails the test below.
      int64_t       b_absmin = 0;
      if (b_range->first >= 1) {
        b_absmin = b_range->first;
      } else if (b_range->second <= -1 && b_range->second != std::numeric_limits<int64_t>::min()) {
        b_absmin = -b_range->second;
      }
      if (b_absmin >= 2 && a_absmax < b_absmin) {
        record(dst_name, av.pin, av.mw);  // `a % b == a` — alias the dividend
        return;
      }
    }

    // A RUNTIME divisor is a first-class remainder cell. It used to be a hard
    // error here, which put the "can this be synthesized?" question in the
    // wrong pass: bitwidth, constprop, LEC and sim can all reason about `%`
    // perfectly well, and only the netlist mapper cannot. pass.abc raises the
    // diagnostic now, so the rest of LiveHD handles remainder normally.
    if (!Lnast_ntype::is_const(lnast_->get_type(b))) {
      emit_rem(dst_name, av, b);
      return;
    }
    const int64_t bval = const_val(b);
    if (bval == 0) {  // constprop already errors comptime mod-by-zero; guard anyway.
      error_at(b, {"mod-by-zero", "type"}, "upass.tolg: modulo by zero is an illegal operation");
      return;
    }
    const int64_t babs = iabs64(bval);
    if (babs == 1) {  // `a % ±1 == 0` for any sign of `a`.
      record(dst_name, create_const(*g_, *Dlop::create_integer(0)), 1);
      return;
    }

    // A possibly-NEGATIVE dividend is no longer ambiguous: the op is defined as
    // TRUNCATED remainder (sign follows the dividend, like Verilog `%` and
    // Dlop::rem_op), so it needs no constraint on `a` -- only the mask/digit-sum
    // shortcuts below do, and those are optimizations, not the semantics.
    if (!a_nonneg) {
      emit_rem(dst_name, av, b);
      return;
    }

    // (1) Power-of-two divisor → mask off the low log2(|b|) bits.
    if ((babs & (babs - 1)) == 0) {
      int32_t k = 0;  // |b| == 2^k
      while ((int64_t{1} << k) < babs) {
        ++k;
      }
      auto andn = make_node(Ntype_op::And);  // commutative: both operands feed sink "a"
      setup_sink_by_name(andn, "as").connect_driver(av.pin);
      setup_sink_by_name(andn, "as").connect_driver(create_const(*g_, *Dlop::create_integer(babs - 1)));
      bind_result(dst_name, andn.create_driver_pin(0), k > 0 ? k : 1);
      return;
    }

    // (3) Modulo 3 → base-4 digit-sum reduction. Needs a SOUND int64 upper
    // bound on `a` (the digit-sum loop's termination + width bound is int64
    // math): a non-negative published range, else the stored width when it fits
    // in 62 bits. A wider untyped dividend has no reliable int64 bound — hard
    // error.
    if (babs == 3) {
      int64_t init_max = 0;
      if (a_range && a_range->first >= 0) {
        init_max = a_range->second;  // exact, sound
      } else if (av.mw > 0 && av.mw <= 62) {
        init_max = (int64_t{1} << av.mw) - 1;  // sound for ≤ 62 bits
      } else {
        // No sound int64 bound for the digit-sum loop -- fall back to the cell.
        emit_rem(dst_name, av, b);
        return;
      }
      bind_result(dst_name, lower_mod3(av, init_max), 2);  // result in [0, 2]
      return;
    }

    // Any other divisor: the general cell.
    emit_rem(dst_name, av, b);
  }

  // `a % 3` for a non-negative `a` whose max value is `init_max`. Because
  // 4 ≡ 1 (mod 3), `a ≡ Σ(base-4 digits of a)  (mod 3)`. Each round splits the
  // value into 2-bit base-4 digits and sums them with a balanced adder tree;
  // the running max strictly shrinks while it is ≥ 4 (max_base4_digit_sum(M) <
  // M for M ≥ 4), so the generation loop terminates. Once the value is in [0,
  // 3] a single correction maps the one non-reduced point 3 → 0.
  [[nodiscard]] Pin lower_mod3(const Val& av, int64_t init_max) {
    Pin     cur     = av.pin;
    int64_t cur_max = init_max < 0 ? 0 : init_max;
    int32_t cur_mw  = mw_of_val(cur_max);
    if (cur_mw > av.mw && av.mw > 0) {
      cur_mw = av.mw;  // never read past the dividend's stored bits
    }
    while (cur_max >= 4) {
      std::vector<Val> digits;
      digits.reserve(static_cast<size_t>((cur_mw + 1) / 2));
      for (int32_t lo = 0; lo < cur_mw; lo += 2) {
        const int32_t hi = std::min(lo + 1, cur_mw - 1);
        auto          gm = make_node(Ntype_op::Get_mask);
        livehd::graph_util::connect_mask_operands(gm, cur, lo, (hi) + 1);
        auto          d   = gm.create_driver_pin(0);
        const int32_t dmw = hi - lo + 1;  // 1 or 2 bits per base-4 digit
        set_ubits(d, dmw);
        digits.push_back({d, dmw});
      }
      cur     = adder_tree(std::move(digits)).pin;
      cur_max = max_base4_digit_sum(cur_max);
      cur_mw  = mw_of_val(cur_max);
    }
    // `cur` ∈ [0, 3]: result = (cur == 3) ? 0 : cur  ==  cur - 3*(cur == 3).
    auto eq = make_node(Ntype_op::EQ);  // commutative: both operands feed sink "a"
    setup_sink_by_name(eq, "as").connect_driver(cur);
    setup_sink_by_name(eq, "as").connect_driver(create_const(*g_, *Dlop::create_integer(3)));
    auto eqp = eq.create_driver_pin(0);
    set_ubits(eqp, 1);
    auto mul = make_node(Ntype_op::Mult);  // 3 * (cur == 3) ∈ {0, 3}
    setup_sink_by_name(mul, "as").connect_driver(eqp);
    setup_sink_by_name(mul, "as").connect_driver(create_const(*g_, *Dlop::create_integer(3)));
    auto mulp = mul.create_driver_pin(0);
    set_ubits(mulp, 2);
    auto sub = make_node(Ntype_op::Sum);  // cur - 3*(cur == 3)
    setup_sink_by_name(sub, "as").connect_driver(cur);
    setup_sink_by_name(sub, "bs").connect_driver(mulp);
    auto subp = sub.create_driver_pin(0);
    set_ubits(subp, 2);
    return subp;
  }

  // Balanced binary adder tree over `terms` (each a non-negative value).
  // Mirrors the popcount tree: every Sum grows the width by one bit; an odd
  // leaf rides up a level untouched. Requires at least one term.
  [[nodiscard]] Val adder_tree(std::vector<Val> terms) {
    while (terms.size() > 1) {
      std::vector<Val> next;
      next.reserve((terms.size() + 1) / 2);
      for (size_t i = 0; i + 1 < terms.size(); i += 2) {
        auto s = make_node(Ntype_op::Sum);  // both operands ADD on sink "a"
        setup_sink_by_name(s, "as").connect_driver(terms[i].pin);
        setup_sink_by_name(s, "as").connect_driver(terms[i + 1].pin);
        const int32_t mw = std::max(terms[i].mw, terms[i + 1].mw) + 1;
        auto          d  = s.create_driver_pin(0);
        set_ubits(d, mw);
        next.push_back({d, mw});
      }
      if (terms.size() & 1) {
        next.push_back(terms.back());  // odd leaf rides to the next level
      }
      terms = std::move(next);
    }
    return terms.front();
  }

  // Maximum base-4 digit sum over [0, M] (the standard "max digit sum of
  // numbers ≤ N" DP). Strictly less than M for M ≥ 4, which is what makes
  // lower_mod3's loop terminate.
  [[nodiscard]] static int64_t max_base4_digit_sum(int64_t M) {
    if (M < 0) {
      return 0;
    }
    int top = 0;  // highest base-4 digit position
    while (top < 31 && (int64_t{1} << (2 * (top + 1))) <= M) {
      ++top;
    }
    int64_t best = 0, prefix = 0;
    for (int pos = top; pos >= 0; --pos) {
      const int64_t d = (M >> (2 * pos)) & 3;
      if (d > 0) {  // drop this digit to d-1 and fill the rest with 3s
        best = std::max(best, prefix + (d - 1) + int64_t{3} * pos);
      }
      prefix += d;  // keep this digit tight
    }
    return std::max(best, prefix);  // ...or M itself
  }

  // |v| with INT64_MIN guarded (callers exclude it before reaching here).
  [[nodiscard]] static int64_t iabs64(int64_t v) {
    if (v == std::numeric_limits<int64_t>::min()) {
      return std::numeric_limits<int64_t>::max();
    }
    return v < 0 ? -v : v;
  }

  // Published bounded range by LNAST name. nullopt = unbounded / unavailable.
  [[nodiscard]] std::optional<std::pair<int64_t, int64_t>> range_of_name(std::string_view name) const {
    const std::string nm{name};
    const auto&       meta = lnast_->bw_meta();
    auto              it   = meta.ranges.find(nm);
    if (it == meta.ranges.end()) {
      it = meta.ranges.find(std::string{canon_io_name(nm)});  // unquoted slang `` `x` `` form
    }
    if (it != meta.ranges.end() && !it->second.unbounded) {
      return std::make_pair(it->second.min, it->second.max);
    }
    // A read-only input param has no derived bw_meta entry — fall back to its
    // declared envelope (io_meta), mirroring
    // uPass_bitwidth::envelope_of_operand.
    std::string_view base = canon_io_name(name);
    if (const auto pos = base.find("___ssa_"); pos != std::string_view::npos) {
      base = base.substr(0, pos);
    }
    for (const auto& in : lnast_->io_meta().inputs) {
      if (in.name != base || in.kind != Io_kind::integer) {
        continue;
      }
      if (in.has_range) {
        return std::make_pair(in.range_min,
                              in.range_max);  // exact `int(min,max)`
      }
      if (in.bits > 0 && in.bits <= 62) {
        if (in.is_signed) {
          return std::make_pair(-(int64_t{1} << (in.bits - 1)), (int64_t{1} << (in.bits - 1)) - 1);
        }
        return std::make_pair(int64_t{0}, (int64_t{1} << in.bits) - 1);
      }
      break;
    }
    return std::nullopt;
  }

  // Published value range of an operand: exact for a comptime const, else the
  // bitwidth pass' derived [min, max] (bw_meta), else — for a never-written
  // input port — its declared envelope from io_meta. nullopt = unbounded.
  [[nodiscard]] std::optional<std::pair<int64_t, int64_t>> range_of_operand(const Lnast_nid& nid) const {
    if (Lnast_ntype::is_const(lnast_->get_type(nid))) {
      auto c = Dlop::from_pyrope(lnast_->get_name(nid));
      if (c->is_just_i64()) {
        const int64_t v = c->to_just_i64();
        return std::make_pair(v, v);
      }
      return std::nullopt;
    }
    return range_of_name(lnast_->get_name(nid));
  }

  // Lower an if-branch body into a fresh write scope; rolls pin_map_ back so
  // branch-local writes don't leak. Returns the names the branch bound. The
  // rollback restores only the names this branch wrote (recorded lazily in
  // record()), so it is O(writes) -- no per-branch copy of the whole pin_map_.
  WriteMap lower_branch(const Lnast_nid& stmts) {
    branch_writes_.emplace_back();
    branch_restore_.emplace_back();
    if (Lnast_ntype::is_stmts(lnast_->get_type(stmts))) {
      lower_stmts(stmts);
    } else {
      lower_node(stmts);
    }
    auto writes = std::move(branch_writes_.back());
    branch_writes_.pop_back();
    auto restore = std::move(branch_restore_.back());
    branch_restore_.pop_back();
    for (const auto& [name, old] : restore) {
      if (old.pin.has_value()) {
        pin_map_[name] = *old.pin;
      } else {
        pin_map_.erase(name);
      }
      if (old.mw.has_value()) {
        mw_map_[name] = *old.mw;
      } else {
        mw_map_.erase(name);
      }
    }
    return writes;
  }

  // if(cond, then-stmts, [cond, stmts]*, [else-stmts]) -> per-variable binary
  // Mux chains. Mux pins: 0 = selector, 1 = false/else, 2 = true/then.
  //
  // unique_if (the `unique if` / `match` chain) declares the conditions
  // mutually exclusive, so the per-variable merge is ONE Hotmux instead: a
  // sequence of control/value pairs plus a default carrying the else or
  // pre-if value. pass.formal checks exclusivity of the controls; simulation
  // checks deferred obligations at runtime.
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
    // R1 — so do assert/assume guards (lower_cassert turns the path condition
    // into an IMPLICATION, where a memory enable uses it as a CONJUNCTION).
    // Tracking is UNCONDITIONAL: the stack holds the raw condition pins (which
    // exist anyway as mux selectors) and current_path_cond() materializes the
    // and2/not1/nonzero1 chain only when a consumer asks, so a body with neither
    // consumer mints nothing. The old `!mem_map_.empty()` gate was evaluated on
    // ENTRY, which lost the guard of any `if` enclosing a memory's declaration.
    // The MUX selector (`branches[].cond`) keeps the RAW pin — that is datapath,
    // with its own established semantics; only the path copy is reduced.
    // `match` rides this too: it lowers to a unique_if whose arms are
    // branch-lowered here before lower_unique_merge runs.
    const size_t               path_base = path_terms_.size();
    // Arm k is taken when every earlier condition is false and c_k is true, so
    // its terms are ¬c_0 … ¬c_{k-1}, c_k; the bare else drops the final c_k.
    std::vector<Pin>           prior_conds;
    auto                       merge_cond = [&](const Pin& raw) { return !valid_minted_ ? raw : and2(valid_pin(), nonzero1(raw)); };
    // Ruling 15 coverage of lane-built array outputs: each arm starts from the
    // pre-`if` lanes, and after the `if` a lane counts as driven only when
    // every path drives it (merge_arm_coverage).
    const auto                 pre_cov    = nil_array_output_lanes_;
    std::vector<Lane_coverage> arm_cov;
    auto                       lower_arm = [&](const Lnast_nid& stmts, const Pin& cond, bool is_else) {
      for (const auto& pc : prior_conds) {
        push_path_term(pc, /*negated=*/true);
      }
      if (!is_else) {
        push_path_term(cond, /*negated=*/false);
      }
      nil_array_output_lanes_ = pre_cov;
      auto w                  = lower_branch(stmts);
      truncate_path_terms(path_base);
      arm_cov.push_back(std::exchange(nil_array_output_lanes_, pre_cov));
      return w;
    };

    Pin first_cond = leaf(child).pin;  // child0 = condition
    child          = lnast_->get_sibling_next(child);
    if (child.is_invalid()) {
      return;
    }
    branches.push_back({false, merge_cond(first_cond), lower_arm(child, first_cond, /*is_else=*/false)});
    prior_conds.push_back(first_cond);

    child = lnast_->get_sibling_next(child);
    while (!child.is_invalid()) {
      bool last = lnast_->is_last_child(child);
      if (last && Lnast_ntype::is_stmts(lnast_->get_type(child))) {
        if (!valid_minted_) {
          branches.push_back({true, Pin{}, lower_arm(child, Pin{}, /*is_else=*/true)});
        } else {
          // Inactive means NO arm, including else. Spell the else as an
          // explicit `active & !c0 & ...` arm so its writes fall through to
          // the pre-if value while inactive and unique-if stays one-hot.
          Pin else_cond = valid_pin();
          for (const auto& pc : prior_conds) {
            else_cond = and2(else_cond, not1(nonzero1(pc)));
          }
          branches.push_back({false, else_cond, lower_arm(child, Pin{}, /*is_else=*/true)});
        }
        break;
      }
      Pin elif_cond = leaf(child).pin;
      child         = lnast_->get_sibling_next(child);
      if (child.is_invalid()) {
        break;
      }
      branches.push_back({false, merge_cond(elif_cond), lower_arm(child, elif_cond, /*is_else=*/false)});
      prior_conds.push_back(elif_cond);
      child = lnast_->get_sibling_next(child);
    }

    merge_arm_coverage(pre_cov, arm_cov, !branches.empty() && branches.back().is_else);

    absl::flat_hash_set<std::string> all_vars_set;
    for (auto& br : branches) {
      for (auto& [name, _] : br.writes) {
        all_vars_set.insert(name);
      }
    }
    // Deterministic merge order: flat_hash_set iteration is randomized per
    // PROCESS, and each var below creates mux nodes -- a random order makes
    // the produced graph (node ids, emission order, every file generated from
    // it) differ run to run, defeating downstream content caches (bazel on
    // the sim C++, run_id, ...). Sort once; both merge paths share it.
    std::vector<std::string> all_vars(all_vars_set.begin(), all_vars_set.end());
    std::sort(all_vars.begin(), all_vars.end());

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
      auto      base      = pin_map_.find(var);
      auto      ew        = else_writes.find(var);
      const int n         = static_cast<int>(branches.size());
      const int last_cond = has_else ? n - 2 : n - 1;
      // 2c-wire — a wire's din shadow has no pre-if value to hold and no X to
      // fall back to (see is_wire_din): the unwritten paths are don't-cares, so
      // fill them with a value the wire IS written with. `wire_seed` is the
      // lowest-priority writing arm, which becomes the chain's base — with a
      // single writing arm that leaves the driver bare, no mux at all.
      Pin       wire_fill;
      int       wire_seed = -1;
      if (base == pin_map_.end() && is_single_bind_net(var)) {
        if (ew != else_writes.end()) {
          wire_fill = ew->second;
        } else {
          for (int i = last_cond; i >= 0; --i) {
            if (auto wr = branches[i].writes.find(var); wr != branches[i].writes.end()) {
              wire_fill = wr->second;
              wire_seed = i;
              break;
            }
          }
        }
      }
      // A non-writing branch falls back to `pre`. For a reg's din shadow with
      // no recorded pre-value (a pure conditional write, no prior write and no
      // read), `pre` is the reg's q (hold) — NOT a don't-care.
      Pin pre;
      if (base != pin_map_.end()) {
        pre = base->second;
      } else if (auto hold = reg_hold_pin(var)) {
        pre = *hold;
      } else if (!wire_fill.is_invalid()) {
        pre = wire_fill;
      } else {
        pre = nil_pin();
      }
      // A wire arm that writes nothing selects nothing: skip it (and the seed
      // arm, already the chain's base) instead of muxing in a don't-care.
      auto skip_arm = [&](int i, bool writes) { return !wire_fill.is_invalid() && (i == wire_seed || !writes); };
      Pin  cur      = (ew != else_writes.end()) ? ew->second : pre;

      // The merged value's width is the widest among the branch sources;
      // mw_lookup alone holds whatever the LAST write recorded (or 1 for a
      // never-bound io output), which under-sizes the mux and truncates the
      // wider arms. Take the max over every contributing pin's stamped bits
      // (bits >= mw by construction, so this only ever widens). pin_mw_of
      // shares this with the Hotmux path (lower_unique_merge).
      // Size from VALUES, not the destination variable's declared envelope.
      // In unbounded LNAST/LGraph semantics a declaration is a boundary
      // contract, not a request to inflate every internal mux. The eventual
      // store/GraphIO/register landing performs a lossless widen; a real wide
      // pre-value or arm is already represented by its pin below.
      int32_t signed_mw   = 0;
      int32_t unsigned_mw = 0;
      // Tracked SEPARATELY from `signed_mw`: an arm that can be negative but
      // carries no width stamp leaves pin_mw_of() at 0, and deriving
      // "any signed" from `signed_mw > 0` would then stamp the merge UNSIGNED
      // -- the exact zero-fill miscompile described below.
      bool    any_signed  = false;
      auto    note_arm    = [&](const Pin& arm) {
        if (pin_can_be_negative(arm)) {
          any_signed = true;
          signed_mw  = std::max(signed_mw, pin_mw_of(arm));
        } else {
          unsigned_mw = std::max(unsigned_mw, pin_mw_of(arm));
        }
      };
      note_arm(cur);
      // A merge is only as unsigned as its ARMS. bind_result stamps UNSIGNED
      // unconditionally, which is a lie the moment one arm can go negative, so
      // every consumer that widens the merge zero-fills a value that had to
      // sign-extend. `c ? -2 : s7` came back as an unsigned net, cgen declared
      // it `reg [65:0]`, and that turned the whole enclosing Verilog expression
      // unsigned: `(-s1) + (c ? -2 : s7)` evaluated to 2 where the golden says
      // 6 (vloghammer wideexpr_00093, confirmed against iverilog). Collect the
      // sign over the same sources the width is collected over.
      // Finalize the merged width BEFORE building the chain so every mux in it
      // (not only the outermost one bind_result stamps) carries it. An if/elif
      // with >=2 conditions builds a chain of muxes; leaving the inner muxes at
      // bits==0 leaks an unbounded cell past tolg (e.g. an else-less `if/elif`
      // whose fall-through arm is a nil `pre` value --
      // assert_ifelse2.pick_max).
      for (int i = last_cond; i >= 0; --i) {
        auto wr = branches[i].writes.find(var);
        if (skip_arm(i, wr != branches[i].writes.end())) {
          continue;
        }
        if (wr != branches[i].writes.end()) {
          note_arm(wr->second);
        } else {
          // Only a NON-WRITING conditional arm can select the pre-if value.
          // When every condition and the explicit else write `var`, `pre` is
          // not connected to this mux at all and must not inflate its carrier
          // (a u64 declaration around `c ? 1 : 0` used to make Slop<66>).
          note_arm(pre);
        }
      }
      const auto mw = std::max<int32_t>(1, any_signed ? std::max(signed_mw, unsigned_mw > 0 ? unsigned_mw + 1 : 0) : unsigned_mw);
      bool       minted = false;
      for (int i = last_cond; i >= 0; --i) {
        auto& br = branches[i];
        auto  wr = br.writes.find(var);
        if (skip_arm(i, wr != br.writes.end())) {
          continue;
        }
        Pin true_val = (wr != br.writes.end()) ? wr->second : pre;

        auto mux = make_node(Ntype_op::Mux);
        livehd::graph_util::setup_sink_pid(mux, 0).connect_driver(br.cond);   // selector
        livehd::graph_util::setup_sink_pid(mux, 1).connect_driver(cur);       // false / else
        livehd::graph_util::setup_sink_pid(mux, 2).connect_driver(true_val);  // true / then
        cur    = mux.create_driver_pin(0);
        minted = true;
        if (i != 0) {  // inner mux; bind_result stamps the outermost (i==0) below
          if (any_signed) {
            set_sbits(cur, mw);
          } else {
            set_ubits(cur, mw);
          }
        }
      }
      if (!minted) {
        // 2c-wire with a single writing arm: `cur` IS that arm's driver pin,
        // owned by the node that produced it. record() it (bind_result would
        // re-stamp a pin this merge did not mint).
        record(var, cur, mw);
        maybe_bind_wire_shadow(var, cur, mw);
        continue;
      }
      bind_result(var, cur, mw);
      if (any_signed) {
        set_sbits(cur, mw);
      }
      // AFTER the final stamp: bind_result stamps `cur` UNSIGNED, and a wire
      // bind copies the driver's width/sign onto the passthrough output, so
      // binding first left a signed driver behind an unsigned buffer.
      maybe_bind_wire_shadow(var, cur, mw);
    }
  }

  // Prove coverage from the SOURCE input declaration, never from inferred pin
  // widths. In particular, an exhaustive match with an empty else must not
  // retain a register's Q on an unreachable none-of path. That artificial hold
  // otherwise leaves an always-transparent latch looking like real state.
  bool covers_input_domain(const std::vector<Branch>& branches, int n_conds) const {
    Pin                          selector;
    absl::flat_hash_set<int64_t> values;
    for (int i = 0; i < n_conds; ++i) {
      const auto cond = branches[i].cond;
      if (cond.is_invalid() || cond.is_const()) {
        return false;
      }
      const auto node = cond.get_master_node();
      if (livehd::graph_util::type_op_of(node) != Ntype_op::EQ) {
        return false;
      }
      // Read-only, but INDEXED: snapshot for the [] access, not for mutation.
      // EQ is a single-bank op, so its two operands are two consecutive sink
      // pins (pid 0 and 1), one driver each.
      const auto sinks = node.inp_pins_snapshot();
      if (sinks.size() != 2) {
        return false;
      }
      auto input = sinks[0].get_driver_pin();
      auto value = sinks[1].get_driver_pin();
      if (input.is_const()) {
        std::swap(input, value);
      }
      if (!livehd::graph_util::is_graph_input_pin(input) || !value.is_const() || (!selector.is_invalid() && selector != input)) {
        return false;
      }
      const auto& literal = livehd::graph_util::const_of(value);
      if (literal.has_unknowns() || !literal.is_just_i64() || !values.insert(literal.to_just_i64()).second) {
        return false;
      }
      selector = input;
    }
    for (const auto& input : lnast_->io_meta().inputs) {
      if (g_->get_input_pin(canon_io_name(input.name)) != selector) {
        continue;
      }
      const auto bits = input.kind == Io_kind::boolean ? 1 : input.bits;
      if (bits <= 0 || bits >= 63 || (uint64_t{1} << bits) != values.size()) {
        return false;
      }
      const int64_t low  = input.is_signed ? -(int64_t{1} << (bits - 1)) : 0;
      const int64_t high = input.is_signed ? (int64_t{1} << (bits - 1)) - 1 : (int64_t{1} << bits) - 1;
      return std::all_of(values.begin(), values.end(), [&](int64_t v) { return low <= v && v <= high; });
    }
    return false;
  }

  // unique_if merge: direct control/value pairs plus a fall-through value.
  void lower_unique_merge(const std::vector<Branch>& branches, const std::vector<std::string>& all_vars, bool has_else,
                          const WriteMap& else_writes) {
    const int n_conds = static_cast<int>(branches.size()) - (has_else ? 1 : 0);
    I(n_conds >= 1);
    const bool exhaustive = covers_input_domain(branches, n_conds);

    for (const auto& var : all_vars) {
      auto       base    = pin_map_.find(var);
      bool       has_pre = base != pin_map_.end();
      // A reg's din shadow with no recorded pre-value still HOLDS on an
      // unwritten / none-of arm: fall back to the reg's q (current value), not
      // a don't-care. Treat that q as a real pre-value so the none-of slot
      // below drives the hold instead of `Dlop::unknown`. A non-reg var
      // (combinational match-expression result) keeps has_pre=false →
      // don't-care none-of slot.
      auto       ew      = else_writes.find(var);
      const bool has_ev  = ew != else_writes.end();
      // 2c-wire — a wire's din shadow has no hold and no X fallback (see
      // is_wire_din): every unwritten arm and the none-of slot are don't-cares,
      // so fill them with a value the wire IS written with instead of an
      // unknown. When that is the ONLY value written, the wire's driver needs no
      // Hotmux at all.
      Pin        wire_fill;
      if (!has_pre && is_single_bind_net(var)) {
        if (has_ev) {
          wire_fill = ew->second;
        } else {
          for (int i = n_conds - 1; i >= 0; --i) {
            if (auto wr = branches[i].writes.find(var); wr != branches[i].writes.end()) {
              wire_fill = wr->second;
              break;
            }
          }
        }
        if (!wire_fill.is_invalid()) {
          bool uniform = true;
          for (const auto& br : branches) {
            if (auto wr = br.writes.find(var); wr != br.writes.end() && !(wr->second == wire_fill)) {
              uniform = false;
              break;
            }
          }
          if (uniform) {  // one distinct driver over all arms: that IS the net
            record(var, wire_fill, std::max<int32_t>(1, pin_mw_of(wire_fill)));
            continue;
          }
        }
      }
      Pin pre;
      if (has_pre) {
        pre = base->second;
      } else if (auto hold = reg_hold_pin(var)) {
        pre     = *hold;
        has_pre = true;
      } else if (!wire_fill.is_invalid()) {
        pre     = wire_fill;
        has_pre = true;
      } else {
        pre = nil_pin();
      }
      Pin else_val = has_ev ? ew->second : pre;

      // The Hotmux result width is the WIDEST among its REAL arm sources,
      // exactly as the Mux chain above sizes itself. mw_lookup alone holds
      // whatever the LAST recorded write left (or 1 for a const arm like a
      // `match … else {0}` slot), which under-sizes the result and truncates
      // the wider arms (the match-expression `o = match s {…else{0}}`
      // 1-bit-output miscompile).
      //
      // A SYNTHETIC nil fallback must never count toward the width. When a
      // `match` omits its `else` and the result has no pre-match value (a fresh
      // match-expression result tmp), the none-of slot is a pure don't-care;
      // but nil_pin() is 64 bits wide, so counting it would balloon the whole
      // Hotmux to 65 bits and truncate the real arms back down. Size from real
      // sources only, then drive the none-of slot with a width-correct
      // don't-care.
      // As in the ordinary Mux path, derive the Hotmux carrier from its real
      // values. A wide declaration around narrow arms is not an arithmetic
      // operand and must not widen the internal selection tree.
      int32_t signed_mw   = 0;
      int32_t unsigned_mw = 0;
      // Tracked SEPARATELY from `signed_mw`: an arm that can be negative but
      // carries no width stamp leaves pin_mw_of() at 0, and deriving
      // "any signed" from `signed_mw > 0` would then stamp the merge UNSIGNED
      // -- the exact zero-fill miscompile described below.
      bool    any_signed  = false;
      auto    note_arm    = [&](const Pin& arm) {
        if (pin_can_be_negative(arm)) {
          any_signed = true;
          signed_mw  = std::max(signed_mw, pin_mw_of(arm));
        } else {
          unsigned_mw = std::max(unsigned_mw, pin_mw_of(arm));
        }
      };
      if (has_ev) {
        note_arm(else_val);
      } else if (has_pre) {
        // No explicit else: none-of selects the pre-if value.
        note_arm(pre);
      }

      // Collect first, connect second: a non-writing arm of a fresh match
      // result has no pre-value.  Its placeholder is a don't-care and must be
      // minted only AFTER the real arms establish `mw`; using nil_pin() here
      // leaked the typeless signed unknown into cgen, where deterministic-X=0
      // materialized it as a 65-bit negative sentinel in an otherwise 1-bit
      // Hotmux.
      std::vector<Pin> arm_values;
      arm_values.reserve(n_conds);
      for (int i = 0; i < n_conds; ++i) {
        auto wr  = branches[i].writes.find(var);
        // A non-writing arm keeps the pre-match value; only real writes size.
        Pin  val = wr != branches[i].writes.end() ? wr->second : (has_pre ? pre : Pin{});
        if (wr != branches[i].writes.end()) {
          note_arm(val);
        } else if (has_pre) {
          // This condition does not write `var`, so its real value arm is the
          // pre-if value even when a separate explicit else exists.
          note_arm(pre);
        }
        arm_values.push_back(val);
      }

      const auto mw = std::max<int32_t>(1, any_signed ? std::max(signed_mw, unsigned_mw > 0 ? unsigned_mw + 1 : 0) : unsigned_mw);

      auto hot = make_node(Ntype_op::Hotmux);
      for (int i = 0; i < n_conds; ++i) {
        const Pin val = arm_values[i].is_invalid() ? create_const(*g_, *Dlop::unknown(mw)) : arm_values[i];
        livehd::graph_util::setup_sink_pid(hot, static_cast<hhds::Port_id>(2 * i)).connect_driver(branches[i].cond);
        livehd::graph_util::setup_sink_pid(hot, static_cast<hhds::Port_id>(2 * i + 1)).connect_driver(val);
      }
      // none-of slot: explicit else / pre value when present; otherwise an
      // exhaustive else-less match — drive the unreachable slot with a
      // width-matched don't-care (`mw`-bit 0sb?) so it adds no width pressure.
      const Pin none_val = exhaustive && !arm_values.front().is_invalid() ? arm_values.front()
                           : (has_ev || has_pre)                          ? else_val
                                                                          : create_const(*g_, *Dlop::unknown(mw));
      livehd::graph_util::setup_sink_pid(hot, static_cast<hhds::Port_id>(2 * n_conds)).connect_driver(none_val);
      auto hot_out = hot.create_driver_pin(0);
      bind_result(var, hot_out, mw);
      if (any_signed) {
        set_sbits(hot_out, mw);
      }
      maybe_bind_wire_shadow(var, hot_out, mw);  // AFTER the final stamp (see lower_if_merge)
    }
  }

  std::shared_ptr<Lnast>      lnast_;
  hhds::Graph*                g_;
  const uPass_tolg::Registry* registry_ = nullptr;
  hhds::GraphLibrary*         lib_      = nullptr;

  absl::flat_hash_map<std::string, Pin>                     pin_map_;
  absl::flat_hash_map<std::string, int32_t>                 mw_map_;
  // The last driver written to each LOGICAL variable (SSA versions x /
  // x___ssa_1 / … collapsed to "x"): the value after ALL in-cycle writes, used
  // for a derived `reset_pin = <signal>` / `clock_pin = <signal>` resolution
  // and for a `wire`'s buffer pin.
  absl::flat_hash_map<std::string, std::pair<Pin, int32_t>> logical_last_;
  // A field read whose source is a Sub result created by a call lowered LATER
  // in the body. Deferred to end-of-pass, then re-resolved with tget_final_ so
  // a still-unresolved one warns instead of looping.
  std::vector<Lnast_nid>                                    pending_tgets_;
  bool                                                      tget_final_ = false;
  std::vector<WriteMap>                                     branch_writes_;
  struct Branch_restore {
    std::optional<Pin>     pin;
    std::optional<int32_t> mw;
  };
  // Parallel to branch_writes_: per active branch, the pre-branch value of each
  // name it wrote (nullopt = absent before the branch). Both the driver and its
  // width are transactional: restoring only pin_map_ lets a narrow then-arm
  // poison the width used while lowering the else-arm. lower_branch replays
  // this to roll both maps back, avoiding full per-branch copies.
  std::vector<absl::flat_hash_map<std::string, Branch_restore>> branch_restore_;
  WriteMap                                                      empty_writes_;

  // 2c-wire — per-wire lowering state recorded at the declare. Binding connects
  // the buffer input to the accumulated driver and restamps untyped outputs;
  // finalize_wires() handles only still-unbound/undriven declarations.
  struct Wire_info {
    hhds::Node_class              buf;             // the passthrough Or (cgen `out = a`)
    hhds::Node_class              narrow;          // typed-wire Get_mask/Sext of the CURRENT bind (dropped on rebind)
    Pin                           out;             // the buffer output (what reads bind to)
    Lnast_nid                     decl_nid;        // diag anchor (the `wire x` site)
    int32_t                       decl_color = 0;  // block region at the declare (2opt-freq B)
    int32_t                       decl_mw    = 0;  // declared width; 0 = untyped (restamp from driver)
    bool                          is_signed  = false;
    bool                          bound      = false;
    bool                          data_typed = false;  // declared with a type other than `Clock`
    Pin                           bound_din;           // driver of the LATEST bind (what finalize splits against)
    std::vector<hhds::Node_class> early_readers;       // consumers present at any bind
  };
  absl::flat_hash_set<std::string>            wire_names_;  // gates lower_store
  std::vector<std::string>                    wire_order_;  // declaration order
  absl::flat_hash_map<std::string, Wire_info> wire_info_;
  livehd::graph_util::Comb_dependency_cache   comb_dependencies_;
  // A wire's buffer and typed-read cells (debug nid) -> its data_typed: an
  // untyped or `Clock` wire is an alias of its driver's Clock/Reset class.
  absl::flat_hash_set<Pin>                    latch_gate_sinks_;
  absl::flat_hash_map<uint64_t, bool>         wire_cells_;

  // Reg lowering state. reg_map_ holds each declared reg's Flop
  // node (reads resolve to its q via pin_map_; stores rebind the shadow
  // din/enable keys). clock_*/reset_* lazily bind the clock/reset graph
  // inputs. reg_info_/reg_order_ carry the finalize metadata for
  // PLAIN regs (stage regs live only in reg_map_/flop_depth_).
  absl::flat_hash_map<std::string, hhds::Node_class>  reg_map_;
  absl::flat_hash_map<std::string, Reg_info>          reg_info_;
  std::vector<std::string>                            reg_order_;
  // Scalar `mut`/`const` declares (NOT reg/latch/array). A `mut b:uN = nil`
  // emits no init store, so its name never gets a driver — but using it as a
  // `b#[lo..=hi] = …` bit-assembly base is legal (the covered bits are
  // overwritten). set_mask_base substitutes a zero base for such a name; the
  // set guards that only a DECLARED scalar gets the treatment (a genuine typo
  // still errors). `= 0` never hits this — its base already folds to const 0.
  absl::flat_hash_set<std::string>                    scalar_decl_;
  // Compiler temps copied from a still-undriven scalar_decl_ name (a field
  // read of a nil output leaf feeding a bit write): temp -> that name.
  absl::flat_hash_map<std::string, std::string>       nil_seed_alias_;
  // Top-level body statements (class indices) that only compute a defaulted
  // input's default value (see mark_default_only_prologue).
  absl::flat_hash_set<int64_t>                        default_only_stmts_;
  // Scalar names declared `const` (see is_unbound_const).
  absl::flat_hash_set<std::string>                    const_decl_;
  // Declared TYPE width per LOGICAL name (canonical, SSA suffix stripped), for
  // the one op whose semantics depend on the DECLARED width rather than on
  // whatever value currently drives the name: Concat.
  //
  // Deliberately NOT mw_map_. That map tracks the live value's magnitude width,
  // so `var a:u4 = 3` leaves 2 there — and a `concat(a, b)` lane must still be
  // 4 bits wide, because narrowing it would shift every lane above it. Filled
  // from io_meta and from every `declare` that carries a type child; a nested
  // `concat`'s own result registers here too (its width is the lane sum, by
  // construction).
  absl::flat_hash_map<std::string, Decl_type>         decl_type_;
  // Names whose value is a `concat` result, with the lane sum. Read only by
  // check_concat_dest_width: the concat node's own dst is a compiler temp, so
  // the user-facing `c:u12 = concat(...)` width check has to happen where that
  // temp is bound to a declared name.
  absl::flat_hash_map<std::string, int32_t>           concat_result_mw_;
  // Declared memories (array-typed regs + mut/const arrays), the
  // branch-path stack lower_if maintains for their write enables, the
  // recorded tuple literals (array initializers / __memory configs), and the
  // bound __memory results.
  absl::flat_hash_map<std::string, Mem_info>          mem_map_;
  // Compiler temps on the old-value chain of a memory PARTIAL write, keyed by
  // temp name (Mem_rmw_read).
  absl::flat_hash_map<std::string, Mem_rmw_read>      mem_rmw_reads_;
  // Memory PARTIAL writes lowered as a write-MASKED port: the old-value
  // tuple_get nids index_mem_write_sites found eligible, the temp such a read
  // binds (to its memory name) once lower_tuple_get takes the masked form, and
  // the merged temp's written-bit mask (Masked_pw_val).
  absl::flat_hash_set<Lnast_nid>                      masked_pw_gets_;
  absl::flat_hash_map<std::string, Masked_pw_base>    masked_pw_bases_;
  absl::flat_hash_map<std::string, Masked_pw_val>     masked_pw_vals_;
  Pin                                                 last_rmw_mask_;  // the mask of the last lower_dynamic_mask_rmw
  absl::flat_hash_map<std::string, Array_scalar_view> array_scalar_views_;
  absl::flat_hash_set<std::string>                    comptime_array_names_;
  absl::flat_hash_set<std::string>                    comptime_scalar_names_;
  absl::flat_hash_map<int32_t, int>                   mem_write_site_counts_;
  std::vector<std::string>                            mem_order_;
  // Path-condition stack: one entry per enclosing branch arm, UNMATERIALIZED
  // (see current_path_cond). `path_folded_[i]` caches the fold of terms[0..i]
  // once some consumer asks for it; an invalid entry is "not built yet".
  struct Path_term {
    Pin  cond;
    bool negated = false;
  };
  std::vector<Path_term>                                                          path_terms_;
  std::vector<Pin>                                                                path_folded_;
  absl::flat_hash_map<std::string, Tuple_rec>                                     tuple_recs_;
  absl::flat_hash_map<std::string, Mem_result>                                    mem_results_;
  // attr_set seen before its target's declare (memory fwd overrides etc).
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, std::string>> pending_attrs_;
  // Every `attr_set(<var>, "reset_pin", <val>)` in the tree, collected BEFORE
  // the body walk. A source-spelled reset_pin normally reaches pending_attrs_
  // only AFTER the declare it belongs to (both the hand-written Pyrope form and
  // the slang reader emit it later); mem_reset_source reads this table so a
  // memory's reset signal resolves the same way at every point of the walk.
  absl::flat_hash_map<std::string, std::string>                                   decl_reset_pin_;
  std::string                                                                     clock_name_;
  bool                                                                            clock_minted_ = false;
  Pin                                                                             clock_pin_;
  bool                                                                            clock_pin_valid_ = false;
  std::string                                                                     reset_name_;
  bool                                                                            reset_minted_        = false;
  bool                                                                            reset_neg_           = false;
  bool                                                                            reset_async_default_ = false;
  Pin                                                                             reset_pin_;
  bool                                                                            reset_pin_valid_ = false;
  std::string                                                                     valid_name_;
  bool                                                                            valid_minted_ = false;
  bool                                                                            valid_active_ = false;
  Pin                                                                             valid_pin_;
  bool                                                                            valid_pin_valid_ = false;
  Pin                                                                             en_true_pin_;
  Pin                                                                             en_false_pin_;
  bool                                                                            en_true_valid_  = false;
  bool                                                                            en_false_valid_ = false;

  absl::flat_hash_map<std::string, Pending_stage> pending_stage_;
  absl::flat_hash_map<std::string, Sub_out>       sub_out_stages_;
  // Multi-output instance results: fcall dst name -> (Sub node, callee
  // outputs); consumed by tuple_get field reads.
  struct Sub_result {
    hhds::Node_class                    sub;
    std::vector<Lnast_io_entry>         outputs;
    // A MULTI-output call: each output's declared latency (parallel to
    // `outputs`; nullopt = untimed), for a `stage[N]` over the whole result.
    std::vector<std::optional<Sub_out>> out_stages;
  };
  absl::flat_hash_map<std::string, Sub_result>                            sub_results_;
  // `stage[N] t = f(...)` over SEVERAL values (a multi-output call, an inlined
  // comb's result tuple): stage name -> field -> the field's value after its
  // own stage flops. tuple_get reads a field here before any Sub_result.
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, Val>> staged_fields_;
  // Array OUTPUTS built lane by lane (ruling 15: they start as nil), with the
  // lanes driven so far on the current path (check_nil_array_outputs). A whole
  // write drives every lane.
  Lane_coverage                                                           nil_array_output_lanes_;
  // Explicit rolled_for lowering hand-off. The index port is allowed to be
  // absent from the hidden ordinary call; lower_rolled_for then attaches the
  // descriptor to exactly the Sub created by that payload.
  std::string                                                             rolled_index_port_;
  hhds::Node_class                                                        last_lowered_sub_;

  // Checker inputs gathered while building: pending records,
  // per-Flop effective crossing depth, per-Sub pinned latency interval.
  // Also adds plain_reg_flops_ (state/stage classification candidates) and
  // inserted_flops_ (the LN-inserted pipe output flops — narrowing targets).
  std::vector<Pending_rec>                                   pending_checks_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> flop_depth_;
  Sub_out_times                                              sub_time_;
  absl::flat_hash_map<uint64_t, std::string>                 plain_reg_flops_;
  absl::flat_hash_set<uint64_t>                              inserted_flops_;
  // 2c-wire — comb-cycle wire buffers of a `::[timecheck=false]` (or
  // Verilog-read) unit: a net that may close a same-cycle ring through a
  // submodule instance; a later lgraph pass detects/handles real ones. The
  // time-checker cuts these nodes' in-edges instead of flagging the loop,
  // preserving the pre-2c-wire leniency. Other wire buffers are NEVER added
  // here, so a real comb loop through a Pyrope wire is flagged as an error.
  absl::flat_hash_set<uint64_t>                              wire_cut_nids_;

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
    if (lnast_->is_timecheck_off()) {
      return;  // `::[timecheck=false]`: no landing-cycle checks
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
      const auto        io = g_->get_io();
      if (!io || !io->has_output(name)) {
        // A tuple-typed output store names the aggregate here, while the graph
        // boundary contains only its flattened dotted leaves. There is no
        // aggregate pin to stamp; each leaf carries the stage annotation from
        // the normal tuple flattening path.
        continue;
      }
      auto sink = g_->get_output_pin(name);
      sink.attr(livehd::attrs::pending_time).set({a_min, a_max});
      pending_checks_.push_back({sink, name, a_min, a_max, /*is_sink=*/true});
    }
  }

  [[nodiscard]] std::vector<Pending_rec>&& take_pending_checks() { return std::move(pending_checks_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& take_flop_depths() { return std::move(flop_depth_); }
  [[nodiscard]] Sub_out_times&&                                              take_sub_times() { return std::move(sub_time_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::string>&& take_plain_reg_flops() { return std::move(plain_reg_flops_); }
  [[nodiscard]] absl::flat_hash_set<uint64_t>&&              take_inserted_flops() { return std::move(inserted_flops_); }
  [[nodiscard]] absl::flat_hash_set<uint64_t>&&              take_wire_cuts() { return std::move(wire_cut_nids_); }
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
//      invisible to SCC); every other flop is a STAGE crossing. A Sub on a
//      cycle is split per OUTPUT for the loop error: each output only depends
//      on the inputs in its own combinational cone inside the child (a ring
//      through a child's register is sequential, not a loop).
//   2. Forward (min,max) interval propagation, twice: graph inputs (0,0);
//      constants unify with anything; comb cells take the equal-meet of
//      their operands (a mismatch is the 06c misalignment error); a stage
//      Flop adds its effective crossing depth; a Sub output adds its own
//      declared interval to the instance's input cycle (clock sinks
//      excluded). Pass 1 treats every state flop's q, and every output of a
//      Sub split on a ring, as unconstrained; then sigma(q) := sigma(din)
//      (state regs pin to their HOME stage — no crossing), a split output
//      pins to its instance's input cycle plus its declared interval, and
//      pass 2 re-checks every meet with the pinned values.
//   3. Narrow each LN-inserted pipe output flop to the body deficit
//      (min−sigma, max−sigma); (0,0) realizes as a wire (the flop is
//      bypassed and deleted). sigma > min is the latency-exceeded error.
//   4. Discharge every pending record: computed == asserted -> REMOVE the
//      pending_time attr; mismatch (or an undischargeable record) -> error.
//      A declared state output (`-> (reg q@[N])`) pins q and din to the same
//      home stage, so its interface cycle discharges directly against home.
// A `comb`, and a `::[timecheck=false]` (Verilog-origin) lambda, runs the
// acyclicity check of step 1 only: the attribute turns off every timing check
// in its lambda, including the cycle mix against a child's declared latency.
class Time_checker {
public:
  struct TR {
    int64_t min = 0;
    int64_t max = 0;
    bool    any = false;  // constants — unify with any cycle
  };

  Time_checker(hhds::Graph* g, const std::shared_ptr<Lnast>& ln, std::vector<Pending_rec>&& pendings,
               absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& flop_depth, Sub_out_times&& sub_time,
               absl::flat_hash_map<uint64_t, std::string>&& plain_regs, absl::flat_hash_set<uint64_t>&& inserted,
               absl::flat_hash_set<uint64_t>&& wire_cuts)
      : g_(g)
      , ln_(ln)
      , pendings_(std::move(pendings))
      , flop_depth_(std::move(flop_depth))
      , sub_time_(std::move(sub_time))
      , plain_regs_(std::move(plain_regs))
      , inserted_(std::move(inserted))
      , wire_cuts_(std::move(wire_cuts)) {
    for (const auto& [nid, name] : plain_regs_) {
      reg_flop_by_name_.emplace(name, nid);
    }
    // Regs with an explicit declared cycle (`reg q@[N]` output, or an `@[N]`
    // assertion) are LEGITIMATELY feedforward — their landing cycle is part of
    // the contract. They must NOT be force-classified as cycle-0 state (the mod
    // default below), or a `q@[1]` delay reg would collapse to a pass-through.
    // Collect them.
    for (const auto& rec : pendings_) {
      // Map the declared-cycle record to its flop. A `@[N]` assertion pins a
      // value pin (its master node); a `reg q@[N]` output pin is DRIVEN by the
      // reg flop (the output port name need not equal the internal reg name, so
      // match by the driver node, not the name). Mark the flop iff it is a
      // plain reg.
      // A bit select or sign extension of the reg (`q = r#[0..<4]`) reads it
      // as directly as `q = r` (ruling 83: a body reg lands one cycle after
      // the inputs that drive it, however it is sliced).
      auto consider = [&](hhds::Node_class mn) {
        for (int hops = 0; hops < 8 && !mn.is_invalid(); ++hops) {
          if (plain_regs_.contains(mn.get_debug_nid())) {
            decl_cycle_regs_.insert(mn.get_debug_nid());
            return;
          }
          const auto op = livehd::graph_util::type_op_of(mn);
          if (op != Ntype_op::Get_mask && op != Ntype_op::Sext) {
            return;
          }
          const auto a = livehd::graph_util::get_driver_of_sink_name(mn, "a");
          mn           = a.is_invalid() || livehd::graph_util::is_graph_input_pin(a) ? hhds::Node_class{} : a.get_master_node();
        }
      };
      if (rec.is_sink) {
        // ONE DRIVER PER SINK PIN: the old "first in-edge" is the driver.
        // Invalid when the sink is undriven, which get_master_node() tolerates.
        if (auto drv = rec.pin.get_driver_pin(); !drv.is_invalid()) {
          consider(drv.get_master_node());
        }
      } else if (!rec.pin.is_invalid()) {
        consider(rec.pin.get_master_node());
      }
      if (auto it = reg_flop_by_name_.find(rec.name); it != reg_flop_by_name_.end()) {
        decl_cycle_regs_.insert(it->second);  // name match too (defensive)
      }
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
      auto drv = rec.pin.get_driver_pin();  // one driver per sink pin
      return drv.is_invalid() ? hhds::Node_class{} : drv.get_master_node();
    }
    return rec.pin.get_master_node();
  }

  void run() {
    using livehd::graph_util::is_graph_input_pin;
    using livehd::graph_util::is_type_const;
    using livehd::graph_util::is_type_flop;
    using livehd::graph_util::is_type_sub;
    using livehd::graph_util::type_op_of;

    // 1. Collect nodes + node-level digraph (consts/graph-inputs excluded).
    // A Sub is ONE vertex here: every input feeds every output (a crossbar).
    // That is the grain that classifies this body's flops (a ring through a
    // child's register is still register feedback), but it is too coarse for
    // the loop error, which 2b refines per child output.
    std::vector<hhds::Node_class>         nodes;
    absl::flat_hash_map<uint64_t, size_t> idx;
    for (auto n : g_->body().nodes()) {
      if (is_type_const(n)) {
        continue;
      }
      idx.emplace(n.get_debug_nid(), nodes.size());
      nodes.push_back(n);
    }
    const size_t nn = nodes.size();
    // Ruling 83: a partial write (`r#[0..<4] = x`) keeps the bits it does not
    // write, so its Set_mask reads the reg's own q as its base -- the untouched
    // bits, not a hold path. That edge neither makes the reg state (a reg
    // written only through slices lands one cycle after its inputs, like
    // `r = x`) nor times the write.
    for (const auto& n : nodes) {
      if (!is_type_flop(n) || !plain_regs_.contains(n.get_debug_nid())) {
        continue;
      }
      const auto q   = n.get_driver_pin(0);
      auto       din = livehd::graph_util::get_driver_of_sink_name(n, "din");
      for (int hops = 0; hops < 64 && !din.is_invalid() && !is_graph_input_pin(din); ++hops) {
        const auto sm = din.get_master_node();
        if (type_op_of(sm) != Ntype_op::Set_mask) {
          break;
        }
        din = livehd::graph_util::get_driver_of_sink_name(sm, "a");
        if (din == q) {
          partial_keep_.insert(sm.get_debug_nid());
          break;
        }
      }
    }
    auto node_idx_of_pin = [&](const hhds::Pin_class& dpin) -> int {
      if (dpin.is_invalid() || is_graph_input_pin(dpin)) {
        return -1;
      }
      auto mn = dpin.get_master_node();
      if (mn.is_invalid() || is_type_const(mn)) {
        return -1;
      }
      auto it = idx.find(mn.get_debug_nid());
      return it == idx.end() ? -1 : static_cast<int>(it->second);
    };
    // Every driver pin feeding node i, i.e. its in-edges.
    auto for_each_in_driver = [&](size_t i, const auto& fn) {
      // 2c-wire — a Verilog-origin comb-cycle wire buffer: cut its in-edge for
      // loop detection (like a state flop's q). Such a net may legally close a
      // same-cycle ring through a submodule; a later lgraph pass handles real
      // comb loops. Pyrope wire buffers are never in this set, so a real comb
      // loop through a Pyrope wire is flagged below.
      if (wire_cuts_.contains(nodes[i].get_debug_nid())) {
        return;
      }
      const bool keeps_own_bits = partial_keep_.contains(nodes[i].get_debug_nid());
      for (auto sink : nodes[i].inp_sorted_pins()) {  // read-only walk
        if (keeps_own_bits && sink.get_port_id() == 0) {
          continue;  // a partial write's untouched bits (see partial_keep_)
        }
        // get_driver_pinS, PLURAL. A compact loop's carry-in sink is the one
        // sanctioned multi-driver pin (pass/legalize/legalize.cpp:301 -- the
        // SEED plus a self edge meaning "the previous ordinal"). Reading it
        // with get_driver_pin() keeps one of the two and silently drops the
        // other, which is a dropped dependency in this classifier.
        for (const auto& drv : sink.get_driver_pins()) {
          // A compact loop carry is a literal Sub self-edge for edge/binding
          // visibility, but HHDS orders the group as if unrolled. Mirror that
          // dependency rule in this domain-specific SCC classifier.
          if (nodes[i].is_loop_subnode() && drv.get_master_node() == nodes[i]) {
            continue;
          }
          fn(drv);
        }
      }
    };
    Digraph gx(nn);
    for (size_t i = 0; i < nn; ++i) {
      for_each_in_driver(i, [&](const hhds::Pin_class& drv) {
        if (const int p = node_idx_of_pin(drv); p >= 0) {
          gx.add_edge(static_cast<size_t>(p), i);
        }
      });
    }

    // 2. Tarjan SCC. Non-trivial SCCs classify their flops.
    const Sccs xs            = sccs_of(gx);
    // Classify: every non-trivial SCC needs a state-eligible flop. A plain
    // reg with neither an enable nor a feedback SCC is otherwise a pyrope
    // FEEDFORWARD (`@[stage]`) flop: σ(q)=σ(din)+1. But a VERILOG `always_ff`
    // reg is always a 1-cycle STATE element (q reads at the current cycle,
    // σ=0) — never a feedforward pipeline stage. Treating it as feedforward
    // gave a spurious stage-1 that tripped the "mixes values at different
    // cycles" check when the reg's output was combined with a stage-0 value
    // (e.g. a concat field). Under `::[timecheck=false]` (and for a module
    // read from Verilog), all plain regs are state.
    //
    // The SAME holds for a pyrope `mod`: a `mod` is Mealy/Moore, so its plain
    // `reg`s are cycle-0 STATE and its only structural latency is from
    // explicit `stage[N]` decls (NOT plain_regs_, untouched here). Only a
    // `pipe` infers a feedforward stage from a plain reg written purely from
    // inputs/earlier regs. Without this, an unconditionally-written `mod`
    // pipeline register (a flush-or-capture stage reg with no hold path) was
    // mis-classified as a +1-cycle stage, forcing a spurious runtime enable
    // that diverged from the Verilog it mirrors (issues.txt A3).
    const bool timecheck_off = ln_->is_timecheck_off();
    const bool mod_default   = ln_->get_lambda_kind() == "mod";
    const auto en_pid        = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, "enable"));
    for (size_t i = 0; i < nn; ++i) {
      if (!is_type_flop(nodes[i])) {
        continue;
      }
      const auto nid      = nodes[i].get_debug_nid();
      const bool eligible = plain_regs_.contains(nid);
      if (eligible) {
        // inp_sorted_pins() yields CONNECTED sink pins only, so reaching the
        // `en` pid at all IS the "driven" answer.
        const bool en_driven     = !driven_sink_at(nodes[i], en_pid).is_invalid();
        // A mod's plain regs default to cycle-0 state — UNLESS the reg
        // carries an explicit @[N]/interface cycle (then it is a declared
        // feedforward stage).
        const bool state_default = timecheck_off || (mod_default && !decl_cycle_regs_.contains(nid));
        if (state_default || en_driven || xs.nontrivial[static_cast<size_t>(xs.id[i])]) {
          state_.insert(nid);
        }
      }
    }
    // Memory nodes are sequential state as well: a read dout reflects STORED
    // state (decoupled from this cycle's write din for the acyclicity check),
    // so a reg-array pipeline like `pfOp[1] <= pfOp[0]` — whose write din is
    // a read of the same memory node — is NOT a combinational loop.  Cut them
    // like flops so the node-level dout->din self-edge does not
    // false-positive.  A Latch holds stored state too: its din hold arm
    // (`din = cond ? d : q`) reads its own q by construction — the standard
    // inferred-latch idiom (prim_clk_gate's ICG), not a loop.
    for (size_t i = 0; i < nn; ++i) {
      const auto op = type_op_of(nodes[i]);
      if (op == Ntype_op::Memory || op == Ntype_op::Latch) {
        state_.insert(nodes[i].get_debug_nid());
      }
    }

    // 2b. The loop graph. A Sub on a non-trivial SCC is SPLIT: each output the
    // body reads becomes its own vertex, fed only by the drivers of the inputs
    // in that output's combinational cone inside the child
    // (graph/port_reach.hpp: flops cut, memories join, nested Subs spliced,
    // each through callee_reach). `q = r` of a child register then depends on
    // no input, so a caller ring through it is sequential, while a ring
    // through a Mealy output (`nx = d + 1`) stays a loop. A loop subnode and a
    // child whose body is absent, or not lowered yet in this pass, stay
    // crossbars.
    std::vector<size_t> split_subs;  // vertex ids of the split Subs
    for (int s = 0; s < xs.count; ++s) {
      if (!xs.nontrivial[static_cast<size_t>(s)]) {
        continue;
      }
      for (const int mi : xs.members[static_cast<size_t>(s)]) {
        const auto& n  = nodes[static_cast<size_t>(mi)];
        const auto  cg = is_type_sub(n) && !n.is_loop_subnode() ? n.get_subnode_graph() : nullptr;
        if (cg && !lowering_pass().pending.contains(cg->get_name())) {
          split_.insert(n.get_debug_nid());
          split_subs.push_back(static_cast<size_t>(mi));
        }
      }
    }
    Digraph                                                  gl_split;
    std::vector<std::pair<size_t, uint64_t>>                 out_vertex;  // loop vertex nn+k -> (Sub vertex, output pid)
    absl::flat_hash_map<std::pair<size_t, uint64_t>, size_t> out_vertex_idx;
    if (!split_.empty()) {
      gl_split         = Digraph(nn);
      // The loop vertex of a driver pin: a split Sub's output has its own.
      auto loop_vertex = [&](const hhds::Pin_class& drv) -> int {
        const int p = node_idx_of_pin(drv);
        if (p < 0 || !split_.contains(nodes[static_cast<size_t>(p)].get_debug_nid())) {
          return p;
        }
        const auto key         = std::pair{static_cast<size_t>(p), static_cast<uint64_t>(drv.get_port_id())};
        const auto [it, fresh] = out_vertex_idx.try_emplace(key, nn + out_vertex.size());
        if (fresh) {
          out_vertex.push_back(key);
          gl_split.add_vertex();
        }
        return static_cast<int>(it->second);
      };
      for (size_t i = 0; i < nn; ++i) {
        for_each_in_driver(i, [&](const hhds::Pin_class& drv) {
          if (const int p = loop_vertex(drv); p >= 0) {
            gl_split.add_edge(static_cast<size_t>(p), i);
          }
        });
      }
      // out_vertex grows while nested split outputs are reached: index it.
      for (size_t k = 0; k < out_vertex.size(); ++k) {
        const auto [s, pid] = out_vertex[k];
        const auto& cone    = reach_.callee_of(nodes[s].get_subnode_graph()).out2ins;
        const auto  it      = cone.find(static_cast<uint32_t>(pid));
        if (it == cone.end()) {
          continue;  // no input reaches this output combinationally
        }
        for (auto sink : nodes[s].inp_sorted_pins()) {  // read-only walk
          if (!it->second.contains(static_cast<uint32_t>(sink.get_port_id()))) {
            continue;
          }
          for (const auto& drv : sink.get_driver_pins()) {
            if (const int p = loop_vertex(drv); p >= 0) {
              gl_split.add_edge(static_cast<size_t>(p), nn + k);
            }
          }
        }
      }
    }
    const Digraph& gl        = split_.empty() ? gx : gl_split;
    const size_t   nv        = gl.succ.size();
    auto           vertex_at = [&](size_t v) -> const hhds::Node_class& {
      return v < nn ? nodes[v] : nodes[out_vertex[v - nn].first];  // a split output reports its instance
    };

    {
      const Sccs  ls_split = split_.empty() ? Sccs{} : sccs_of(gl);
      const Sccs& ls       = split_.empty() ? xs : ls_split;
      for (int s = 0; s < ls.count; ++s) {
        if (!ls.nontrivial[static_cast<size_t>(s)]) {
          continue;
        }
        bool             has_state = false;
        bool             has_flop  = false;
        hhds::Node_class rep;  // a member of the offending SCC, for the diag anchor
        for (const int mi : ls.members[static_cast<size_t>(s)]) {
          const auto& n = vertex_at(static_cast<size_t>(mi));
          if (rep.is_invalid()) {
            rep = n;
          }
          if (is_type_flop(n)) {
            has_flop = true;
            if (state_.contains(n.get_debug_nid())) {
              has_state = true;
            }
          } else if (type_op_of(n) == Ntype_op::Memory || type_op_of(n) == Ntype_op::Latch) {
            // A memory's stored state breaks the cycle (its read dout reflects
            // the flopped contents, decoupled from this cycle's write din) — a
            // reg-array pipeline `pfOp[1] <= pfOp[0]` is sequential, not a
            // loop. A latch's q likewise holds state; its hold-arm q read is
            // the inferred-latch idiom, not a loop.
            has_state = true;
          }
        }
        if (!has_state) {
          if (has_flop) {
            error_at_node(rep,
                          "upass.tolg: '{}' has register feedback through "
                          "stage registers — make the looping register a plain "
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
    auto is_state_vertex = [&](size_t v) { return v < nn && state_.contains(nodes[v].get_debug_nid()); };
    auto order           = topo_order(gl, is_state_vertex);
    if (order.size() < nv) {
      // state flops never enter the order (indeg cut makes them sources —
      // they ARE in the order); a shortfall is a residual comb cycle.
      std::vector<bool> placed(nv, false);
      for (const size_t v : order) {
        placed[v] = true;
      }
      const auto first = static_cast<size_t>(std::find(placed.begin(), placed.end(), false) - placed.begin());
      error_at_node(vertex_at(first), "upass.tolg: combinational loop in '{}'", ln_->get_top_module_name());
    }

    // 2c-wire — a `comb` runs steps 1-3 ONLY (acyclicity), to catch a comb loop
    // through a self-feeding wire in a standalone-compiled comb top. It has no
    // flops and no `@[N]` landing cycles, so the σ-timing phase below is
    // pipe/mod-specific and skipped. A `::[timecheck=false]` (or Verilog-read)
    // lambda opts out of every timing check, so it stops here too.
    if (ln_->get_lambda_kind() == "comb" || timecheck_off) {
      return;
    }
    // A split output is evaluated like a state q: cut from its instance (it
    // may close a ring through it) and pinned below.
    if (!split_.empty()) {
      order = topo_order(gl, [&](size_t v) { return v >= nn || is_state_vertex(v); });
    }

    // 4. Forward σ with state q pinned to σ(din). Pass 1 leaves state q
    // unconstrained, then we pin σ(q):=σ(din) and re-propagate. A state
    // flop's din may transit through ANOTHER state flop's q, so iterate to a
    // fixpoint (bounded by the pinned count) — otherwise a chained state reg
    // would home at `any` and silently pass its `@[N]` check. The outputs of a
    // split Sub pin the same way, to the instance's input cycle plus each
    // output's declared interval (all outputs of one instance settle together).
    const auto din_pid    = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, "din"));
    auto       din_driver = [&](const hhds::Node_class& flop) -> hhds::Pin_class {
      auto sink = driven_sink_at(flop, din_pid);
      return sink.is_invalid() ? hhds::Pin_class{} : sink.get_driver_pin();
    };
    for (const size_t i : order) {
      if (i < nn) {
        eval_node(nodes[i]);
      }
    }
    // Fast exit for modules with no declared timing. The σ-fixpoint below and
    // phases 5/6 produce/consume tr_ ONLY through pendings_ (declared `@[N]` /
    // interface cycles) and inserted_ (LN-minted `%pipe_` flops), so with both
    // empty nothing reads the fixpoint's result. The fixpoint is also the only
    // thing that pins a STATE flop's σ (or a split Sub output's) from `any` to
    // a concrete cycle (re-walking every node's in-pins once per pass,
    // O(pinned_count * nn)); skipping it can only hide a "mixes values at
    // different cycles" error if some node actually carries a non-zero σ.
    // Non-zero σ requires a feedforward path: an explicit stage depth
    // (flop_depth_ non-empty), a `pipe`'s plain regs (which default to +1), or
    // an instance output with a declared latency. When there is none — a
    // non-pipe lambda (a `mod` reg is cycle-0 state) — every σ is 0, so the
    // single eval pass above already ran an exhaustive mix check and the
    // fixpoint is pure dead work.
    const bool sub_latency = std::any_of(sub_time_.begin(), sub_time_.end(), [](const auto& kv) {
      return kv.second.first != 0 || kv.second.second != 0;
    });
    if (pendings_.empty() && inserted_.empty() && flop_depth_.empty() && !sub_latency && ln_->get_lambda_kind() != "pipe") {
      return;
    }
    // Every pin settles within pinned_count passes unless a ring through them
    // gains cycles on each trip (a child's @[N>=1] output or a stage flop fed
    // back into its own input). Such a ring has no consistent cycle: the last
    // pass names a pin still moving, and that is a cycle-mix error.
    const size_t     pinned_count = state_.size() + split_subs.size();
    bool             converged    = false;
    hhds::Node_class drift_flop;  // a state flop still moving on the last pass
    hhds::Node_class drift_sub;   // a split instance still moving on the last pass
    uint64_t         drift_pid = 0;
    for (size_t pass = 0; pass <= pinned_count && !converged; ++pass) {
      bool changed = false;
      drift_flop   = {};
      drift_sub    = {};
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
          if (drift_flop.is_invalid()) {
            drift_flop = nodes[i];
          }
        }
      }
      for (const size_t s : split_subs) {
        const auto nid = nodes[s].get_debug_nid();
        for (auto dp : nodes[s].out_sorted_pins()) {  // read-only walk
          const auto key    = std::pair{nid, static_cast<uint64_t>(dp.get_port_id())};
          const TR   pinned = instance_out_tr(key);  // σ(instance inputs) + declared
          auto       it     = out_tr_.find(key);
          if (it == out_tr_.end() || it->second.any != pinned.any || it->second.min != pinned.min || it->second.max != pinned.max) {
            out_tr_[key] = pinned;
            changed      = true;
            if (drift_sub.is_invalid()) {
              drift_sub = nodes[s];
              drift_pid = key.second;
            }
          }
        }
      }
      for (const size_t i : order) {
        if (i >= nn || state_.contains(nodes[i].get_debug_nid())) {
          continue;  // pinned above
        }
        eval_node(nodes[i]);
      }
      converged = !changed;
    }
    if (!converged) {
      if (!drift_sub.is_invalid()) {
        error_at_node(drift_sub,
                      "upass.tolg: '{}' mixes values at different cycles: output '{}' of instance '{}' feeds back into "
                      "its own inputs, so every trip around the ring lands later — close the ring through a state "
                      "output declared at `@[0]`, or use `::[timecheck=false]`",
                      ln_->get_top_module_name(),
                      output_name_of(drift_sub, drift_pid),
                      drift_sub.get_name());
      }
      error_at_node(drift_flop,
                    "upass.tolg: '{}' mixes values at different cycles: register '{}' feeds back into itself through "
                    "a delayed path, so every trip around the ring lands later — use `::[timecheck=false]`",
                    ln_->get_top_module_name(),
                    drift_flop.is_invalid() ? std::string_view{} : drift_flop.get_name());
    }

    // 5. Narrow LN-inserted pipe output flops to the body deficit.
    for (const auto& rec : pendings_) {
      if (!rec.is_sink) {
        continue;
      }
      auto drv = rec.pin.get_driver_pin();  // one driver per sink pin
      if (drv.is_invalid()) {
        continue;
      }
      auto mn = drv.get_master_node();
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
                      "upass.tolg: output '{}' of '{}' lands at stage {}, pipe "
                      "declares {}",
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
        rec.pin.del_sink();  // drop the single in-edge (the flop's q)
        rec.pin.connect_driver(din);
        mn.del_node();
      } else {
        tr_[mn.get_debug_nid()] = {sigma + nmin, sigma + nmax, false};
      }
    }

    // 6. Discharge pendings.
    for (const auto& rec : pendings_) {
      // Declared-reg output (`-> (reg q@[N])`). A STATE reg's crossing is
      // already represented by the state cell: sigma(q)=sigma(din), so its
      // home equals the declared landing cycle. A feedforward (stage) reg as
      // output is rejected for a PIPE; for a MOD it discharges through the
      // normal path.
      if (rec.is_sink) {
        if (auto rit = reg_flop_by_name_.find(rec.name); rit != reg_flop_by_name_.end()) {
          const auto fnid     = rit->second;
          const bool is_state = state_.contains(fnid);
          if (!is_state && ln_->get_lambda_kind() == "pipe") {
            error_at_node(pending_anchor(rec),
                          {"pipe-output-reg", "time"},
                          "feedforward register '{}' in the output list of "
                          "'{}' — the output is already "
                          "registered by the pipe contract",
                          rec.name,
                          ln_->get_top_module_name());
          }
          if (is_state) {
            if (rec.min != rec.max) {
              error_at_node(pending_anchor(rec),
                            {"reg-output-cycle", "time"},
                            "register output '{}' of '{}' needs a fixed "
                            "declared cycle (got [{}, {}])",
                            rec.name,
                            ln_->get_top_module_name(),
                            rec.min,
                            rec.max);
            }
            const TR      home          = tr_.contains(fnid) ? tr_.at(fnid) : TR{0, 0, true};
            const int64_t required_home = rec.min;
            if (!home.any && home.min != required_home) {
              error_at_node(pending_anchor(rec),
                            {"reg-output-cycle", "time"},
                            "state register '{}' of '{}' homes at stage {} but "
                            "its declared landing cycle {} "
                            "requires home {}",
                            rec.name,
                            ln_->get_top_module_name(),
                            home.min,
                            rec.min,
                            required_home);
            }
            rec.pin.attr(livehd::attrs::pending_time).del();
            continue;
          }
        }
      }
      TR               cur;
      hhds::Node_class anchor_node;  // the value's driver cell, for the diag span
      if (rec.is_sink) {
        auto drv = rec.pin.get_driver_pin();  // one driver per sink pin
        if (drv.is_invalid()) {
          continue;  // undriven output already warned/nil-wired
        }
        cur         = pin_tr(drv);
        anchor_node = drv.get_master_node();
      } else {
        cur         = pin_tr(rec.pin);
        anchor_node = rec.pin.get_master_node();
      }
      if (cur.any || (cur.min == rec.min && cur.max == rec.max)) {
        rec.pin.attr(livehd::attrs::pending_time).del();  // removed once checked
        continue;
      }
      error_at_node(anchor_node,
                    "upass.tolg: '{}' in '{}' lands at cycle(s) ({},{}) but "
                    "({},{}) is {}",
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
  // A node-level digraph (succ/pred adjacency by vertex id).
  struct Digraph {
    std::vector<std::vector<int>> succ;
    std::vector<std::vector<int>> pred;

    Digraph() = default;
    explicit Digraph(size_t n) : succ(n), pred(n) {}
    void add_vertex() {
      succ.emplace_back();
      pred.emplace_back();
    }
    void add_edge(size_t from, size_t to) {
      pred[to].push_back(static_cast<int>(from));
      succ[from].push_back(static_cast<int>(to));
    }
  };

  // Strongly connected components: per-vertex SCC id, per-SCC members
  // (ascending vertex id, so members[s][0] is the lowest-index member == the
  // diag-anchor rep) and whether the SCC is a cycle (2+ members or a
  // self-loop).
  struct Sccs {
    std::vector<int>              id;
    int                           count = 0;
    std::vector<bool>             nontrivial;
    std::vector<std::vector<int>> members;
  };

  // Tarjan SCC (iterative).
  [[nodiscard]] static Sccs sccs_of(const Digraph& g) {
    const size_t        nn = g.succ.size();
    Sccs                r;
    std::vector<int>    low(nn, -1), num(nn, -1);
    std::vector<bool>   on_stack(nn, false);
    std::vector<int>    stk;
    std::vector<size_t> scc_size;
    int                 counter = 0;
    r.id.assign(nn, -1);
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
        if (cur < g.succ[static_cast<size_t>(v)].size()) {
          const int w = g.succ[static_cast<size_t>(v)][cur++];
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
            r.id[w]     = r.count;
            ++members;
          } while (w != v);
          scc_size.push_back(members);
          ++r.count;
        }
        const int done = v;
        dfs.pop_back();
        if (!dfs.empty()) {
          low[dfs.back().first] = std::min(low[dfs.back().first], low[done]);
        }
      }
    }
    // self-loops count as non-trivial too. Bucket each vertex by its SCC id in
    // this same O(nn) pass, so an offending-SCC scan iterates only the members
    // of each nontrivial SCC instead of re-scanning all nn vertices per SCC
    // (was O(nontrivial_scc * nn); XSCore has many register-feedback rings).
    r.nontrivial.assign(static_cast<size_t>(r.count), false);
    r.members.resize(static_cast<size_t>(r.count));
    for (size_t i = 0; i < nn; ++i) {
      const auto s = static_cast<size_t>(r.id[i]);
      r.members[s].push_back(static_cast<int>(i));
      if (scc_size[s] > 1) {
        r.nontrivial[s] = true;
      }
      for (int w : g.succ[i]) {
        if (w == static_cast<int>(i)) {
          r.nontrivial[s] = true;
        }
      }
    }
    return r;
  }

  // Kahn topological order with the in-edges of every `cut` vertex removed
  // (such a vertex is a source). A vertex left on a cycle is missing from the
  // returned order.
  template <typename Cut>
  [[nodiscard]] static std::vector<size_t> topo_order(const Digraph& g, const Cut& cut) {
    const size_t     nn = g.succ.size();
    std::vector<int> indeg(nn, 0);
    for (size_t i = 0; i < nn; ++i) {
      if (!cut(i)) {
        indeg[i] = static_cast<int>(g.pred[i].size());
      }
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
      for (int s : g.succ[i]) {
        if (cut(static_cast<size_t>(s))) {
          continue;
        }
        if (--indeg[static_cast<size_t>(s)] == 0) {
          queue.push_back(static_cast<size_t>(s));
        }
      }
    }
    return order;
  }

  // The declared name of an instance's output port, for a diagnostic.
  [[nodiscard]] static std::string output_name_of(const hhds::Node_class& sub, uint64_t pid) {
    if (auto gio = sub.get_subnode_io()) {
      for (const auto& d : gio->get_output_pin_decls()) {
        if (static_cast<uint64_t>(d.port_id) == pid) {
          return std::string(d.name);
        }
      }
    }
    return {};
  }

  // σ of one instance output: the instance's input cycle (the equal-meet of
  // its data inputs, kept on the Sub node) plus that output's declared
  // interval. Unconstrained until the Sub itself is evaluated; an instance fed
  // only by constants lands at the declared interval.
  [[nodiscard]] TR instance_out_tr(const std::pair<uint64_t, uint64_t>& key) const {
    const auto it = tr_.find(key.first);
    if (it == tr_.end()) {
      return {0, 0, true};
    }
    TR out = it->second.any ? TR{0, 0, false} : it->second;
    if (const auto dit = sub_time_.find(key); dit != sub_time_.end()) {
      out.min += dit->second.first;
      out.max += dit->second.second;
    }
    return out;
  }

  [[nodiscard]] TR pin_tr(const hhds::Pin_class& dpin) {
    using livehd::graph_util::is_graph_input_pin;
    using livehd::graph_util::is_type_const;
    using livehd::graph_util::is_type_sub;
    if (dpin.is_invalid()) {
      return {0, 0, true};
    }
    if (is_graph_input_pin(dpin)) {
      return {0, 0, false};
    }
    auto mn = dpin.get_master_node();
    if (mn.is_invalid() || is_type_const(mn)) {
      return {0, 0, true};
    }
    if (is_type_sub(mn)) {
      // Each instance output lands at its own declared interval; a split
      // instance's output is pinned by the fixpoint (unconstrained before).
      const auto key = std::pair{mn.get_debug_nid(), static_cast<uint64_t>(dpin.get_port_id())};
      if (split_.contains(key.first)) {
        const auto it = out_tr_.find(key);
        return it == out_tr_.end() ? TR{0, 0, true} : it->second;
      }
      return instance_out_tr(key);
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
    if (auto sink = driven_sink_at(node, pid); !sink.is_invalid()) {
      sink.del_sink();
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
      const auto control_end = livehd::graph_util::hotmux_control_end(node);
      for (auto sink : node.inp_sorted_pins()) {  // read-only walk
        if (type_op_of(node) == Ntype_op::Mux ? sink.get_port_id() == 0
                                              : livehd::graph_util::is_hotmux_control(sink.get_port_id(), control_end)) {
          skip_pids.insert(sink.get_port_id());
          const auto t = pin_tr(sink.get_driver_pin());
          if (!t.any) {
            mux_sel = mux_sel.any ? t : TR{std::min(mux_sel.min, t.min), std::max(mux_sel.max, t.max), false};
          }
        }
      }
    }
    if (is_type_flop(node)) {
      din_only = true;
    } else if (partial_keep_.contains(nid)) {
      skip_pids.insert(0);  // a partial write's untouched bits carry no cycle of their own
    } else if (type_op_of(node) == Ntype_op::Clock_cell) {
      // Ruling 83: a Clock carries no cycle; the gate is timed by its enable.
      skip_pids.insert(static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Clock_cell, "clk_ref")));
    } else if (is_mem) {
      // 2f-mem — a memory is treated like a register (08-memories.md): an
      // ASYNC read (type 0/2) returns committed state with no added stage,
      // exactly like a scalar reg read — @[0] from the read address. Only a
      // SYNC read (type=1, registered dout) charges the +1 crossing. The
      // clock sinks are excluded from the meet either way.
      constexpr int kStride = static_cast<int>(Ntype::Memory_port_stride);  // Memory per-port sink
                                                                            // stride, graph/cell.hpp
      for (auto sink : node.inp_sorted_pins()) {                            // read-only walk
        const auto raw_pid   = static_cast<int>(sink.get_port_id());
        const auto sink_name = Ntype::get_sink_name(Ntype_op::Memory, raw_pid % kStride);
        if (sink_name == "clock_pin") {
          skip_pids.insert(static_cast<uint64_t>(raw_pid));
        } else if (sink_name == "type" && raw_pid < kStride) {
          skip_pids.insert(static_cast<uint64_t>(raw_pid));
          if (const auto& v = livehd::graph_util::const_of(sink.get_driver_pin()); v.is_just_i64() && v.to_just_i64() == 1) {
            mem_clocked = true;  // sync read: dout is registered
          }
        }
      }
    } else if (is_type_sub(node)) {
      auto gio = node.get_subnode_io();
      if (gio) {
        for (const auto& d : gio->get_input_pin_decls()) {
          // The minted `clock`/`reset` and the Verilog-convention names. (A
          // Pyrope `Clock`/`Reset` port with another name carries no marker on
          // the GraphIO; its driver is a graph input, time 0, like any clock.)
          if (upass::io_port::verilog_clock_name(d.name) || upass::io_port::verilog_reset_name(d.name)) {
            skip_pids.insert(static_cast<uint64_t>(d.port_id));
          }
        }
      }
    }
    const auto din_pid = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, "din"));

    for (auto sink : node.inp_sorted_pins()) {  // read-only walk
      const auto spid = static_cast<uint64_t>(sink.get_port_id());
      if (din_only && spid != din_pid) {
        continue;
      }
      if (!din_only && !skip_pids.empty() && skip_pids.contains(spid)) {
        continue;
      }
      // PLURAL: a compact loop's carry-in sink legitimately holds two drivers
      // (see the SCC classifier above); the meet must see both.
      for (const auto& drv : sink.get_driver_pins()) {
        TR t = pin_tr(drv);
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
                        "upass.tolg: '{}' mixes values at different cycles "
                        "(({},{}) vs ({},{})) at a {} cell (sink pid {}) "
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
    }
    // A Sub keeps the meet of its data inputs (the instance's input cycle);
    // each output adds its own declared interval when read (pin_tr).
    tr_[nid] = out;
  }

  hhds::Graph*                                               g_;
  std::shared_ptr<Lnast>                                     ln_;
  std::vector<Pending_rec>                                   pendings_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> flop_depth_;
  Sub_out_times                                              sub_time_;
  absl::flat_hash_map<uint64_t, std::string>                 plain_regs_;
  absl::flat_hash_set<uint64_t>                              inserted_;
  absl::flat_hash_set<uint64_t>                              wire_cuts_;  // 2c-wire: cut Verilog comb-cycle wire in-edges
  absl::flat_hash_map<std::string, uint64_t>                 reg_flop_by_name_;
  absl::flat_hash_set<uint64_t>                              decl_cycle_regs_;  // regs with an explicit @[N]/interface cycle
                                                                                // (feedforward)
  absl::flat_hash_set<uint64_t>                              state_;
  absl::flat_hash_set<uint64_t>                              partial_keep_;  // Set_masks whose base is their own reg's q
  absl::flat_hash_map<uint64_t, TR>                          tr_;
  absl::flat_hash_set<uint64_t>                              split_;   // Subs split per output (on a ring)
  absl::flat_hash_map<std::pair<uint64_t, uint64_t>, TR>     out_tr_;  // their pinned (nid, output pid) cycles

  // Per-callee output -> input comb cones, spliced through callee_reach.
  livehd::port_reach::Cache reach_{callee_reach, /*slices=*/false};
};

// Transitive clock need. A module needs a clock when its own
// tree declares a reg (stage decls synthesize reg declares) or when any
// pipe/mod CALLEE transitively needs one (its instance's minted clock pin
// must be forwarded). Memoized over the registry; an instantiation cycle
// (illegal mutual hierarchy) breaks false so we never hang — the real
// recursion diagnostic belongs to a later phase.
[[nodiscard]] bool tree_declares_reg(const std::shared_ptr<Lnast>& lnast) {
  auto& cache_slot = lnast->tolg_scan_cache().declares_reg;
  if (cache_slot.has_value()) {
    return *cache_slot;
  }
  // A reg whose `clock_pin=NAME` attr names its clock explicitly does not
  // need the implicit `clock` input (the slang reader stamps these for
  // non-clk/clock Verilog clock names); collect the covered names first.
  absl::flat_hash_set<std::string>      clocked_elsewhere;
  absl::flat_hash_set<std::string>      written;
  std::function<void(const Lnast_nid&)> scan_attrs = [&](const Lnast_nid& nid) {
    if (lnast->is_dce_dead(nid)) {
      return;
    }
    // Reuse the existing scan. Count both indexed and whole-array stores, and
    // an in-place set_mask (its result is the variable itself: no store
    // follows); a file initializer itself is a child of declare, not a store.
    if (const auto nt = lnast->get_type(nid); Lnast_ntype::is_store(nt) || nt == Lnast_ntype::Lnast_ntype_set_mask) {
      const auto dst = lnast->get_first_child(nid);
      if (!dst.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(dst))) {
        const auto name = lnast->get_name(dst);
        written.emplace(name.substr(0, name.find("___ssa_")));
      }
    }
    if (Lnast_ntype::is_attr_set(lnast->get_type(nid))) {
      auto tgt = lnast->get_first_child(nid);
      auto key = tgt.is_invalid() ? tgt : lnast->get_sibling_next(tgt);
      if (!key.is_invalid() && (lnast->get_name(key) == "clock_pin" || lnast->get_name(key) == "__store_clock_pin")) {
        clocked_elsewhere.emplace(lnast->get_name(tgt));
      }
      if (!key.is_invalid() && (lnast->get_name(key) == "initial" || lnast->get_name(key) == "reset_pin")) {
        written.emplace(lnast->get_name(tgt));  // conservatively retain explicit initialization/reset overrides
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      scan_attrs(c);
    }
  };
  scan_attrs(lnast->get_root());

  std::function<bool(const Lnast_nid&)> has_reg = [&](const Lnast_nid& nid) -> bool {
    if (lnast->is_dce_dead(nid)) {
      return false;
    }
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
              const auto init = lnast->get_sibling_next(c2);
              if (mode == "reg" && Lnast_ntype::is_comp_type_array(lnast->get_type(c1)) && !written.contains(lnast->get_name(c0))
                  && !init.is_invalid() && Lnast_ntype::is_const(lnast->get_type(init))) {
                const auto value = Dlop::from_pyrope(lnast->get_name(init));
                if (value && value->is_string() && hlop::memory_image(value->to_string())) {
                  return false;  // an unwritten file-preloaded array has no clocked behavior
                }
              }
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
  const bool result = has_reg(lnast->get_root());
  cache_slot        = result;
  return result;
}

void collect_callee_names_impl(const std::shared_ptr<Lnast>& lnast, std::vector<std::string>& out) {
  std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
    if (lnast->is_dce_dead(nid)) {
      return;  // dce:mark — a dead instance call must not pull clock/reset
               // onto the parent (the rebuilt-tree path would have dropped it)
    }
    if (Lnast_ntype::is_func_call(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        // A by-name (same-file) callee is an unquoted ref; an import-bound
        // pipe/mod callee folds to a QUOTED const string (`'unit.entity'`).
        // Accept both and unquote, so the transitive clock/reset walk reaches
        // imported stateful children too (else the parent skips registering the
        // clock/reset it must forward — the "needs_reset bug" at
        // instantiation).
        if (!c1.is_invalid() && (Lnast_ntype::is_ref(lnast->get_type(c1)) || Lnast_ntype::is_const(lnast->get_type(c1)))) {
          std::string nm(lnast->get_name(c1));
          if (nm.size() >= 2 && nm.front() == '\'' && nm.back() == '\'') {
            nm = nm.substr(1, nm.size() - 2);
          }
          out.emplace_back(std::move(nm));
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      walk(c);
    }
  };
  walk(lnast->get_root());
}

// Cached callee-name list. The unquoted by-name/import callees are a pure
// function of this immutable-during-tolg tree, queried by needs_clock_rec /
// needs_reset_rec once per ancestor and phase — compute once.
const std::vector<std::string>& collect_callee_names(const std::shared_ptr<Lnast>& lnast) {
  auto& cache_slot = lnast->tolg_scan_cache().callee_names;
  if (!cache_slot.has_value()) {
    std::vector<std::string> names;
    collect_callee_names_impl(lnast, names);
    cache_slot = std::move(names);
  }
  return *cache_slot;
}

// The argument shape of one call: enough to tell which callee inputs it binds.
struct Call_shape {
  absl::flat_hash_set<std::string> named;  // `port=value` keys
  std::size_t                      positional = 0;
};

// Every live call in `lnast`, keyed by the unquoted callee name (as
// collect_callee_names spells it).
absl::flat_hash_map<std::string, std::vector<Call_shape>> collect_call_shapes(const std::shared_ptr<Lnast>& lnast) {
  absl::flat_hash_map<std::string, std::vector<Call_shape>> out;
  std::function<void(const Lnast_nid&)>                     walk = [&](const Lnast_nid& nid) {
    if (lnast->is_dce_dead(nid)) {
      return;
    }
    if (Lnast_ntype::is_func_call(lnast->get_type(nid))) {
      const auto c0 = lnast->get_first_child(nid);
      const auto c1 = c0.is_invalid() ? c0 : lnast->get_sibling_next(c0);
      if (!c1.is_invalid() && (Lnast_ntype::is_ref(lnast->get_type(c1)) || Lnast_ntype::is_const(lnast->get_type(c1)))) {
        std::string nm(lnast->get_name(c1));
        if (nm.size() >= 2 && nm.front() == '\'' && nm.back() == '\'') {
          nm = nm.substr(1, nm.size() - 2);
        }
        Call_shape shape;
        for (auto a = lnast->get_sibling_next(c1); !a.is_invalid(); a = lnast->get_sibling_next(a)) {
          if (!Lnast_ntype::is_store(lnast->get_type(a))) {
            ++shape.positional;
          } else if (const auto key = lnast->get_first_child(a); !key.is_invalid()) {
            shape.named.emplace(lnast->get_name(key));
          }
        }
        out[nm].push_back(std::move(shape));
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      walk(c);
    }
  };
  walk(lnast->get_root());
  return out;
}

// User ruling 2026-09-27 (12): a call that OMITS its `mod`/`pipe` callee's
// declared clock (or implicit reset) input is wired from the CALLER's implicit
// clock (reset) in lower_func_call, so that caller needs one even when the
// callee's own port keeps the clock off the transitive ABI. `candidate` picks
// the ports.
[[nodiscard]] bool some_call_omits(const std::vector<Call_shape>& calls, const Lnast& callee,
                                   bool (*candidate)(const Lnast_io_entry&)) {
  const auto& cio = callee.io_meta();
  for (const auto& call : calls) {
    for (std::size_t k = call.positional; k < cio.inputs.size(); ++k) {
      const auto& in = cio.inputs[k];
      if (candidate(in) && !call.named.contains(in.name) && !declares_input_default(&callee, in)) {
        return true;
      }
    }
  }
  return false;
}

// Registry-wide ABI facts are immutable during one tolg invocation. Keep the
// linear call-graph analysis outside Lnast: the cache owns no units and is
// replaced when a different registry is presented, so it cannot extend Lnast
// lifetime or alter the object's layout/destructor state.
struct Registry_abi_cache {
  const uPass_tolg::Registry*             registry = nullptr;
  absl::flat_hash_map<const Lnast*, bool> needs_clock;
  absl::flat_hash_map<const Lnast*, bool> needs_reset;
  absl::flat_hash_map<const Lnast*, bool> activation_capable;
};

Registry_abi_cache& registry_abi_cache() {
  static thread_local Registry_abi_cache cache;
  return cache;
}

void prepare_registry_abi(const uPass_tolg::Registry& registry);

// Memoized, cycle-guarded transitive "does this module (or any pipe/mod callee)
// satisfy `declares`" walk. `declares` is the per-tree leaf predicate — a plain
// reg declare (needs a clock) or a reset-carrying reg declare (needs a reset).
[[nodiscard]] bool needs_transitive(const std::shared_ptr<Lnast>& lnast, const uPass_tolg::Registry& registry,
                                    absl::flat_hash_map<std::string, bool>& memo, absl::flat_hash_set<std::string>& visiting,
                                    bool (*declares)(const std::shared_ptr<Lnast>&)) {
  const std::string key(lnast->get_top_module_name());
  if (auto it = memo.find(key); it != memo.end()) {
    return it->second;
  }
  if (!visiting.insert(key).second) {
    return false;  // cycle guard
  }
  bool needs = declares(lnast);
  if (!needs) {
    std::optional<absl::flat_hash_map<std::string, std::vector<Call_shape>>> shapes;  // built on the first public port
    for (const auto& cn : collect_callee_names(lnast)) {
      auto callee = resolve_callee_lnast(cn, registry, lnast->get_top_module_name());
      if (!callee) {
        continue;
      }
      const auto kind = callee->get_lambda_kind();
      if (kind != "pipe" && kind != "mod") {
        continue;  // a comb holds no state and auto-wires nothing (ruling 30)
      }
      // A clocked callee whose public ABI already carries `clk`/`clock` is
      // wired by the ordinary named actual at the call site.  It does NOT need
      // the caller's compiler-minted implicit clock forwarded as a second,
      // hidden input.  Propagating the raw "contains state" fact through such
      // a boundary gave parents like prim_rf_1r1w_preview an unused `clock`
      // GraphIO pin in addition to their explicit `rf_clk_i`; hierarchical LEC
      // then quite correctly reported an implementation-only box input.
      //
      // Keep the leaf result true: setup_io_impl still needs it to select the
      // callee's own declared clk/clock as clock_name.  Suppress only the
      // TRANSITIVE request seen by its caller.
      const bool  clock_walk = declares == &tree_declares_reg;
      const auto  candidate  = clock_walk ? &is_clock_candidate : &is_reset_candidate;
      const auto& cins       = callee->io_meta().inputs;
      if (!lnast->is_verilog_origin() && std::any_of(cins.begin(), cins.end(), candidate)) {
        if (!shapes) {
          shapes = collect_call_shapes(lnast);
        }
        if (const auto it = shapes->find(cn); it != shapes->end() && some_call_omits(it->second, *callee, candidate)) {
          needs = true;  // the omitted clock/reset is auto-wired from this caller's own
          break;
        }
      }
      const bool callee_has_public_clock = clock_walk && std::any_of(cins.begin(), cins.end(), &is_clock_candidate);
      if (!callee_has_public_clock && needs_transitive(callee, registry, memo, visiting, declares)) {
        needs = true;
        break;
      }
    }
  }
  visiting.erase(key);
  memo[key] = needs;
  return needs;
}

[[nodiscard]] bool needs_clock_rec(const std::shared_ptr<Lnast>& lnast, const uPass_tolg::Registry& registry,
                                   absl::flat_hash_map<std::string, bool>& memo, absl::flat_hash_set<std::string>& visiting) {
  prepare_registry_abi(registry);
  if (const auto it = registry_abi_cache().needs_clock.find(lnast.get()); it != registry_abi_cache().needs_clock.end()) {
    return it->second;
  }
  return needs_transitive(lnast, registry, memo, visiting, &tree_declares_reg);
}

// True when any plain-reg declare carries a non-nil initializer (the
// declare's trailing const [value] child) without an explicit per-reg
// `reset_pin` attr override (those bind their own reset input). A ref init is
// counted (tolg later requires it const; the reset NEED is already real).
[[nodiscard]] bool tree_declares_reset_reg_impl(const std::shared_ptr<Lnast>& lnast) {
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
          // 1a-mem — an array reg is a memory, and `reg arr:[N]T = <const>`
          // means exactly what it means on a scalar: that const is the RESET
          // value of every entry, so an init'd array
          // needs the implicit reset input just like a flop does (finalize_mems
          // wires the cell's whole-array `reset` pin, which reloads every entry
          // in one cycle). `0sb?` is the array spelling of "no reset
          // value" (lower_mem_declare stops on it exactly like `nil`).
          const bool is_array = !c1.is_invalid() && Lnast_ntype::is_comp_type_array(lnast->get_type(c1));
          // "latch" counts too (2f-latch M7): a latch with a reset value needs
          // the module's reset input created just as a flop does. Keying this
          // on "reg" alone is why `reg l:u8:[latch=true] = 3` used to die with
          // "has a reset value but <mod> has no reset input (setup_io bug)" —
          // the reg was real, the PORT was never made.
          if (mode == "reg" || mode.starts_with("reg ") || (!is_array && mode == "latch")) {
            for (auto c = lnast->get_sibling_next(c2); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
              const auto ct = lnast->get_type(c);
              if (Lnast_ntype::is_stages(ct)) {
                break;  // stage reg — no init slot
              }
              if (Lnast_ntype::is_const(ct) || Lnast_ntype::is_ref(ct)) {
                const auto txt      = lnast->get_name(c);
                const bool nil_init = Lnast_ntype::is_const(ct) && (txt == "nil" || (is_array && txt == "0sb?"));
                if (is_array && Lnast_ntype::is_const(ct)) {
                  auto value = Dlop::from_pyrope(txt);
                  if (value && value->is_string() && hlop::memory_image(value->to_string())) {
                    break;
                  }
                }
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

// True when any reg (or latch) declare carries a reset value — including an
// ARRAY-typed one, whose concrete (non-nil, non-`0sb?`) initializer is the
// reset value of every entry. The module binds a reset-candidate input, or
// mints the implicit `reset`; the memory lowering then builds a
// one-entry-per-cycle restore sweep.
[[nodiscard]] bool tree_declares_reset_reg(const std::shared_ptr<Lnast>& lnast) {
  auto& slot = lnast->tolg_scan_cache().declares_reset_reg;
  if (!slot.has_value()) {
    slot = tree_declares_reset_reg_impl(lnast);
  }
  return *slot;
}

[[nodiscard]] bool needs_reset_rec(const std::shared_ptr<Lnast>& lnast, const uPass_tolg::Registry& registry,
                                   absl::flat_hash_map<std::string, bool>& memo, absl::flat_hash_set<std::string>& visiting) {
  prepare_registry_abi(registry);
  if (const auto it = registry_abi_cache().needs_reset.find(lnast.get()); it != registry_abi_cache().needs_reset.end()) {
    return it->second;
  }
  return needs_transitive(lnast, registry, memo, visiting, &tree_declares_reset_reg);
}

// Activation is an ABI property of the CALLEE, but it is discovered at call
// sites: a definition reached below a runtime if/match arm must be able to hold
// every kind of state and suppress every property/side effect while that arm is
// inactive. Include transitive descendants because an activated A may call B
// unconditionally; B still runs in A's activation context.
[[nodiscard]] std::vector<std::string> collect_guarded_callee_names(const std::shared_ptr<Lnast>& lnast) {
  std::vector<std::string>                    out;
  std::function<void(const Lnast_nid&, bool)> walk = [&](const Lnast_nid& nid, bool guarded) {
    if (lnast->is_dce_dead(nid)) {
      return;
    }
    const auto type = lnast->get_type(nid);
    if (guarded && Lnast_ntype::is_func_call(type)) {
      auto dst = lnast->get_first_child(nid);
      auto cal = dst.is_invalid() ? dst : lnast->get_sibling_next(dst);
      if (!cal.is_invalid() && (Lnast_ntype::is_ref(lnast->get_type(cal)) || Lnast_ntype::is_const(lnast->get_type(cal)))) {
        std::string name(lnast->get_name(cal));
        if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'') {
          name = name.substr(1, name.size() - 2);
        }
        out.emplace_back(std::move(name));
      }
    }
    const bool branches = Lnast_ntype::is_if(type) || Lnast_ntype::is_unique_if(type);
    size_t     ordinal  = 0;
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c), ++ordinal) {
      // child 0 is the first condition, evaluated in the surrounding context.
      // Every later child is an arm or a later condition, hence conditionally
      // reached. The runner has already removed compile-time-dead arms.
      walk(c, guarded || (branches && ordinal != 0));
    }
  };
  walk(lnast->get_root(), false);
  return out;
}

void prepare_registry_abi(const uPass_tolg::Registry& registry) {
  auto& cache = registry_abi_cache();
  if (cache.registry == &registry) {
    return;
  }
  cache.registry = nullptr;
  cache.needs_clock.clear();
  cache.needs_reset.clear();
  cache.activation_capable.clear();

  std::vector<std::shared_ptr<Lnast>> units;
  units.reserve(registry.size());
  absl::flat_hash_map<const Lnast*, size_t> by_ptr;
  absl::flat_hash_map<std::string, size_t>  by_exact_name;
  for (const auto& ln : registry) {
    if (!ln) {
      continue;
    }
    const size_t idx = units.size();
    units.push_back(ln);
    by_ptr.emplace(ln.get(), idx);
    by_exact_name[std::string(ln->get_top_module_name())] = idx;
  }

  auto resolve_index = [&](std::string_view name, std::string_view caller_unit) -> std::optional<size_t> {
    if (const auto it = by_exact_name.find(name); it != by_exact_name.end()) {
      return it->second;
    }
    auto ln = resolve_callee_lnast(name, registry, caller_unit);
    if (!ln) {
      return std::nullopt;
    }
    const auto it = by_ptr.find(ln.get());
    return it == by_ptr.end() ? std::nullopt : std::optional<size_t>{it->second};
  };

  std::vector<std::vector<size_t>> edges(units.size());
  std::vector<std::vector<size_t>> clock_reverse(units.size());
  std::vector<std::vector<size_t>> reset_reverse(units.size());
  std::vector<uint8_t>             activation(units.size(), 0);
  std::vector<uint8_t>             clock(units.size(), 0);
  std::vector<uint8_t>             reset(units.size(), 0);

  // A caller that omits a child's declared clock/reset input auto-wires its own
  // (lower_func_call), so it is a clock/reset root (some_call_omits).
  std::vector<std::tuple<size_t, std::string, size_t>> public_clock_reset_calls;  // (caller, callee name, child)

  std::vector<uint8_t> control_root(units.size(), 0);
  for (size_t i = 0; i < units.size(); ++i) {
    clock[i]         = units[i]->tolg_needs_clock().value_or(tree_declares_reg(units[i]));
    reset[i]         = units[i]->tolg_needs_reset().value_or(tree_declares_reset_reg(units[i]));
    control_root[i]  = std::any_of(units[i]->io_meta().outputs.begin(), units[i]->io_meta().outputs.end(), [](const auto& e) {
      return e.name == "__next_active";
    });
    // Only a NON-template unit is a caller the activation flood may start
    // from (the pre-index scan skipped templates on the caller side); a
    // template's own `__next_active` output still makes it capable, but that
    // is added after the flood so it never spreads to its callees.
    // OR, never assign: an EARLIER unit's guarded-callee loop below may already
    // have marked this one, and overwriting that mark loses the whole reason it
    // is activation capable (the caller precedes the callee in registry order
    // whenever the callee is imported, which is the common case).
    activation[i]   |= units[i]->tolg_activation_capable().value_or(false);
    if (!units[i]->is_template()) {
      activation[i] |= control_root[i];
    }

    for (const auto& name : collect_callee_names(units[i])) {
      const auto child = resolve_index(name, units[i]->get_top_module_name());
      if (!child.has_value()) {
        continue;
      }
      edges[i].push_back(*child);
      const auto kind = units[*child]->get_lambda_kind();
      if (kind == "pipe" || kind == "mod") {
        // A child's declared clk/clock is an ordinary actual at this call site;
        // only a compiler-MINTED clock must be transported through the caller's
        // hidden ABI. Keep reset's existing propagation independent -- clock
        // and reset need not share the same public boundary.
        const auto& cins                   = units[*child]->io_meta().inputs;
        const bool  child_has_public_clock = std::any_of(cins.begin(), cins.end(), &is_clock_candidate);
        if (!child_has_public_clock) {
          clock_reverse[*child].push_back(i);
        }
        reset_reverse[*child].push_back(i);
        if (!units[i]->is_template() && !units[i]->is_verilog_origin()
            && (child_has_public_clock || std::any_of(cins.begin(), cins.end(), &is_reset_candidate))) {
          public_clock_reset_calls.emplace_back(i, name, *child);
        }
      }
    }
    if (units[i]->is_template()) {
      continue;
    }
    for (const auto& name : collect_guarded_callee_names(units[i])) {
      if (const auto child = resolve_index(name, units[i]->get_top_module_name()); child.has_value()) {
        activation[*child] = 1;
      }
    }
  }

  auto flood = [](std::vector<uint8_t>& marked, const std::vector<std::vector<size_t>>& adjacency) {
    std::vector<size_t> queue;
    queue.reserve(marked.size());
    for (size_t i = 0; i < marked.size(); ++i) {
      if (marked[i]) {
        queue.push_back(i);
      }
    }
    for (size_t head = 0; head < queue.size(); ++head) {
      for (const auto next : adjacency[queue[head]]) {
        if (!marked[next]) {
          marked[next] = 1;
          queue.push_back(next);
        }
      }
    }
  };
  {
    absl::flat_hash_map<size_t, absl::flat_hash_map<std::string, std::vector<Call_shape>>> shapes;
    for (const auto& [caller, name, child] : public_clock_reset_calls) {
      auto [it, fresh] = shapes.try_emplace(caller);
      if (fresh) {
        it->second = collect_call_shapes(units[caller]);
      }
      // collect_call_shapes keys a call by the same spelling collect_callee_names uses.
      const auto calls = it->second.find(name);
      if (calls == it->second.end()) {
        continue;
      }
      clock[caller] |= some_call_omits(calls->second, *units[child], &is_clock_candidate);
      reset[caller] |= some_call_omits(calls->second, *units[child], &is_reset_candidate);
    }
  }
  flood(activation, edges);
  flood(clock, clock_reverse);
  flood(reset, reset_reverse);
  // A unit that itself publishes `__next_active` is activation capable no
  // matter who reaches it — including a template, which is never a flood root.
  for (size_t i = 0; i < units.size(); ++i) {
    activation[i] |= control_root[i];
  }

  cache.needs_clock.reserve(units.size());
  cache.needs_reset.reserve(units.size());
  cache.activation_capable.reserve(units.size());
  for (size_t i = 0; i < units.size(); ++i) {
    cache.needs_clock.emplace(units[i].get(), clock[i] != 0);
    cache.needs_reset.emplace(units[i].get(), reset[i] != 0);
    cache.activation_capable.emplace(units[i].get(), activation[i] != 0);
    units[i]->set_tolg_needs_clock(clock[i] != 0);
    units[i]->set_tolg_needs_reset(reset[i] != 0);
    units[i]->set_tolg_activation_capable(activation[i] != 0);
  }
  cache.registry = &registry;
}

void reset_registry_abi(const uPass_tolg::Registry& registry) {
  auto& cache    = registry_abi_cache();
  cache.registry = nullptr;
  cache.needs_clock.clear();
  cache.needs_reset.clear();
  cache.activation_capable.clear();
  prepare_registry_abi(registry);
}

[[nodiscard]] bool activation_reaches(const std::shared_ptr<Lnast>& from, const std::shared_ptr<Lnast>& target,
                                      const uPass_tolg::Registry& registry, absl::flat_hash_set<std::string>& visiting) {
  if (from == target || from->get_top_module_name() == target->get_top_module_name()) {
    return true;
  }
  const std::string key(from->get_top_module_name());
  if (!visiting.insert(key).second) {
    return false;
  }
  for (const auto& name : collect_callee_names(from)) {
    auto child = resolve_callee_lnast(name, registry, from->get_top_module_name());
    if (child && activation_reaches(child, target, registry, visiting)) {
      visiting.erase(key);
      return true;
    }
  }
  visiting.erase(key);
  return false;
}

[[nodiscard]] bool is_activation_capable(const std::shared_ptr<Lnast>& target, const uPass_tolg::Registry& registry) {
  prepare_registry_abi(registry);
  if (const auto it = registry_abi_cache().activation_capable.find(target.get());
      it != registry_abi_cache().activation_capable.end()) {
    return it->second;
  }
  // Runtime-control loops can deactivate later occurrences through
  // __next_active even when their compact call is not inside a source if.
  for (const auto& e : target->io_meta().outputs) {
    if (e.name == "__next_active") {
      return true;
    }
  }
  for (const auto& caller : registry) {
    if (!caller || caller->is_template()) {
      continue;
    }
    const bool runtime_control_root = std::any_of(caller->io_meta().outputs.begin(),
                                                  caller->io_meta().outputs.end(),
                                                  [](const auto& e) { return e.name == "__next_active"; });
    if (runtime_control_root) {
      absl::flat_hash_set<std::string> visiting;
      if (activation_reaches(caller, target, registry, visiting)) {
        return true;
      }
    }
    for (const auto& name : collect_guarded_callee_names(caller)) {
      auto root = resolve_callee_lnast(name, registry, caller->get_top_module_name());
      if (!root) {
        continue;
      }
      absl::flat_hash_set<std::string> visiting;
      if (activation_reaches(root, target, registry, visiting)) {
        return true;
      }
    }
  }
  return false;
}

// The declarations one module wants, collected before any GraphIO mutation.
struct Io_plan {
  struct Port {
    std::string   name;
    hhds::Port_id pid = 0;
  };
  struct Stamp {
    std::string name;
    uint32_t    bits   = 1;
    bool        unsign = true;
  };
  std::vector<Port>                inputs;   // declaration order
  std::vector<Port>                outputs;  // declaration order
  std::vector<Stamp>               stamps;   // set_bits/set_unsign, in order
  absl::flat_hash_set<std::string> names;    // both directions

  // Declare `name` once, in either direction (the first direction wins, as
  // the has_input/has_output guard always did). False when already declared.
  bool want(std::string_view name, bool is_input, hhds::Port_id pid) {
    if (!names.emplace(name).second) {
      return false;
    }
    (is_input ? inputs : outputs).push_back({std::string(name), pid});
    return true;
  }
  void stamp(std::string_view name, uint32_t bits, bool unsign) { stamps.push_back({std::string(name), bits, unsign}); }
};

// Make `gio` declare exactly `plan`: the ports a fresh library would get
// (same names, directions, positional port ids, same order).
//
// The GraphIO may already hold declarations from outside this lowering: an
// `--emit-dir lg:` library an earlier compile saved (instance() loads it from
// disk), an absorbed `lg:` input the source now redefines, or an earlier
// lowering in the same process. The source is the truth. The old add-only
// merge kept every port the source no longer declares: renaming input `a` to
// `a2` left BOTH on port id 2, cgen emitted a phantom `input a`, and
// pass.partition read an unnamed pin (SIGSEGV in opt).
//
// The longest per-direction prefix that already matches (same name + port id,
// in order) is kept, so an unchanged interface is not touched at all (the warm
// recompile, and run() seeing its own phase-1 declaration again) and a changed
// one keeps its gid and leading ports. The mismatched tail is dropped and
// re-declared, which leaves the declaration vectors in fresh-compile order. A
// body is deleted first: hhds refuses to drop a declared pin a body still
// wires, and run() rebuilds the body from the LNAST anyway.
//
// Returns the declarations it replaced when it changed an interface that
// already declared ports (nullopt for a fresh or unchanged GraphIO).
struct Io_decls {
  std::vector<hhds::GraphIO::DeclaredIoPin> inputs;
  std::vector<hhds::GraphIO::DeclaredIoPin> outputs;
};
[[nodiscard]] std::optional<Io_decls> apply_io_plan(hhds::GraphLibrary& lib, hhds::GraphIO& gio, const Io_plan& plan) {
  auto matching_prefix = [](const std::vector<hhds::GraphIO::DeclaredIoPin>& have, const std::vector<Io_plan::Port>& want) {
    size_t k = 0;
    while (k < have.size() && k < want.size() && have[k].name == want[k].name && have[k].port_id == want[k].pid
           && !have[k].loop_break) {
      ++k;
    }
    return k;
  };
  const size_t            keep_in  = matching_prefix(gio.get_input_pin_decls(), plan.inputs);
  const size_t            keep_out = matching_prefix(gio.get_output_pin_decls(), plan.outputs);
  std::optional<Io_decls> replaced;
  if (keep_in != gio.get_input_pin_decls().size() || keep_out != gio.get_output_pin_decls().size()) {
    replaced = Io_decls{gio.get_input_pin_decls(), gio.get_output_pin_decls()};
    if (gio.has_graph()) {
      lib.delete_graph(gio.get_gid());
    }
    std::vector<std::string> drop_in;
    std::vector<std::string> drop_out;
    for (size_t i = keep_in; i < gio.get_input_pin_decls().size(); ++i) {
      drop_in.push_back(gio.get_input_pin_decls()[i].name);
    }
    for (size_t i = keep_out; i < gio.get_output_pin_decls().size(); ++i) {
      drop_out.push_back(gio.get_output_pin_decls()[i].name);
    }
    // Back to front: each delete then reindexes nothing.
    for (auto it = drop_in.rbegin(); it != drop_in.rend(); ++it) {
      gio.delete_input(*it);
    }
    for (auto it = drop_out.rbegin(); it != drop_out.rend(); ++it) {
      gio.delete_output(*it);
    }
  }
  // Every kept name is a wanted name of the same direction and the wanted
  // names are unique across directions, so the tail adds cannot collide.
  for (size_t i = keep_in; i < plan.inputs.size(); ++i) {
    gio.add_input(plan.inputs[i].name, plan.inputs[i].pid);
  }
  for (size_t i = keep_out; i < plan.outputs.size(); ++i) {
    gio.add_output(plan.outputs[i].name, plan.outputs[i].pid);
  }
  for (const auto& s : plan.stamps) {
    gio.set_bits(s.name, s.bits);
    gio.set_unsign(s.name, s.unsign);
  }
  return replaced;  // set only when a declared port was dropped: never for a fresh GraphIO
}

// Callee gid -> the library graphs that instantiate it and that the current
// lowering does not produce: no unit tree lowers to it, and the compile cache
// did not restore it (a restore rebuilds every importer of a changed
// interface). The modules of an absorbed `lg:` input, of other compiles sharing
// an `--emit-dir lg:`, or ones an earlier compile left behind.
//
// `leftover` marks those a unit of this compile or of this scope's PREVIOUS
// generation owns (`unit` or `unit.<x>`, the compile cache's ownership rule;
// uPass_tolg::set_prior_units): the modules of a unit the edit dropped from the
// import closure, or a module a unit no longer defines. The kernel prunes those
// right after lowering unless something still instantiates them
// (compile_cache_prune_graphs), so their stale instances wait for
// check_leftover_instances instead of being refused at once.
//
// Built on the first interface change of a lowering pass (detect_lg_collisions
// opens a pass) and only then, since it walks every foreign body.
struct Foreign_instance_index {
  const uPass_tolg::Registry*                            registry = nullptr;
  const hhds::GraphLibrary*                              lib      = nullptr;
  absl::flat_hash_map<hhds::Gid, std::vector<hhds::Gid>> by_callee;
  absl::flat_hash_set<hhds::Gid>                         leftover;
};

// One instance whose child's connected port id now names another port.
struct Stale_instance {
  std::string   callee;
  std::string   caller;
  hhds::Port_id pid = 0;
  std::string   was{};  // empty: the id was not declared
  std::string   now;
  std::string   hint{};  // how to fix it, naming where `caller` came from (stale_instance_hint)
};

// Where the library's modules came from (uPass_tolg::set_library_origins).
struct Library_origins {
  bool                                          known = false;  // the caller said; else the hint names both options
  std::string                                   working;        // the working library as the user names it, or ""
  std::vector<std::string>                      inputs;         // the absorbed lg: INPUT dirs
  absl::flat_hash_map<std::string, std::string> absorbed;       // module -> the lg: INPUT dir its body came from
};

// The guard state of one lowering pass.
struct Io_guard_state {
  Foreign_instance_index                                      index;
  std::vector<std::string>                                    prior_units;
  Library_origins                                             origins;
  std::vector<std::pair<hhds::GraphLibrary*, Stale_instance>> deferred;  // held by a leftover graph
};

Io_guard_state& io_guard_state() {
  static thread_local Io_guard_state state;
  return state;
}

void reset_io_guard() {
  auto& state = io_guard_state();
  state.index = {};
  state.prior_units.clear();
  state.origins = {};
  state.deferred.clear();
}

const Foreign_instance_index& foreign_instances(hhds::GraphLibrary& lib, const uPass_tolg::Registry& registry) {
  auto& state = io_guard_state();
  auto& index = state.index;
  if (index.registry == &registry && index.lib == &lib) {
    return index;
  }
  index          = {};
  index.registry = &registry;
  index.lib      = &lib;
  // A graph this lowering produces (or the compile cache restored) is current.
  // Anything else under a name a unit of this compile or of the previous
  // generation owns is a leftover: a module a unit no longer defines (a renamed
  // lambda, a unit that left the closure) is not rebuilt either, and the prune
  // keeps it when something else in the library still instantiates it.
  absl::flat_hash_set<std::string_view> current;
  absl::flat_hash_set<std::string_view> units(state.prior_units.begin(), state.prior_units.end());
  for (const auto& ln : registry) {
    if (!ln) {
      continue;
    }
    if (!ln->is_template()) {  // a template lowers to no graph of its own
      current.insert(ln->get_graph_name());
    }
    units.insert(ln->get_top_module_name());
  }
  const auto owned = [](const absl::flat_hash_set<std::string_view>& owners, std::string_view name) {
    for (;;) {
      if (owners.contains(name)) {
        return true;
      }
      const auto dot = name.rfind('.');
      if (dot == std::string_view::npos) {
        return false;
      }
      name = name.substr(0, dot);
    }
  };
  for (const auto gid : lib.all_gids()) {
    auto graph = lib.get_graph(gid);
    if (!graph || current.contains(graph->get_name())) {
      continue;
    }
    if (owned(units, graph->get_name())) {
      index.leftover.insert(gid);
    }
    absl::flat_hash_set<hhds::Gid> callees;
    for (auto node : graph->body().nodes()) {
      if (!livehd::graph_util::is_type_sub(node)) {
        continue;
      }
      if (auto child = node.get_subnode_io(); child && callees.insert(child->get_gid()).second) {
        index.by_callee[child->get_gid()].push_back(gid);
      }
    }
  }
  return index;
}

// The fix for a stale `caller`, which lives where it came from: an absorbed
// lg: INPUT supplied its body (rebuild that input), or the working library
// already held it (an earlier compile left it in the emit dir: rebuild that
// one -- and any input it may have been copied from). Without
// set_library_origins the origin is unknown, so both kinds of dir are named.
std::string stale_instance_hint(std::string_view caller) {
  const auto& origins = io_guard_state().origins;
  if (origins.known) {
    if (const auto it = origins.absorbed.find(caller); it != origins.absorbed.end()) {
      return std::format(
          "'{}' comes from the lg: input {}, and this compile does not define it: rebuild that input from its current source, "
          "or compile the source that defines '{}' in the same run instead",
          caller,
          it->second,
          caller);
    }
    if (!origins.working.empty()) {
      std::string inputs;
      for (const auto& dir : origins.inputs) {
        inputs += inputs.empty() ? dir : ", " + dir;
      }
      return std::format(
          "the lg: library {} still holds '{}', a module this compile does not define (an earlier compile left it there): "
          "rebuild that directory fresh (or emit into a new one){}, or compile the source that defines '{}' in the same run "
          "when it still exists",
          origins.working,
          caller,
          inputs.empty() ? std::string{} : std::format(" and any lg: input that also holds it ({})", inputs),
          caller);
    }
  }
  return std::format(
      "the lg: library still holds '{}', a module this compile does not define: rebuild the lg: input or emit dir that "
      "holds '{}', or compile the source that defines '{}' in the same run",
      caller,
      caller,
      caller);
}

[[noreturn]] void refuse_stale_instance(const Stale_instance& s) {
  livehd::diag::err("upass.tolg", "stale-instance", "io")
      .msg(
          "this compile changes the ports of '{}', but '{}' (already in the lg: library, not rebuilt by this compile) "
          "instantiates it with the old ports: its port id {} was '{}' and is now '{}'",
          s.callee,
          s.caller,
          s.pid,
          s.was.empty() ? std::string_view{"<none>"} : std::string_view{s.was},
          s.now.empty() ? std::string_view{"<none>"} : std::string_view{s.now})
      .hint(s.hint)
      .fatal();
}

// `gio`'s interface just changed from `before`. A foreign instance (see
// Foreign_instance_index) binds the child's ports by PORT ID, so once a
// connected id names a different port -- or none -- cgen silently rewires the
// instance: a caller's `reset` landed on the child's new `clock`. Refuse that,
// or defer it when a leftover graph holds the instance. An instance whose
// connected ids all keep their names (an appended port, a width change) is
// left alone, as before.
void check_foreign_instances(hhds::GraphLibrary& lib, const hhds::GraphIO& gio, const Io_decls& before,
                             const uPass_tolg::Registry& registry) {
  const auto& index = foreign_instances(lib, registry);
  const auto  it    = index.by_callee.find(gio.get_gid());
  if (it == index.by_callee.end()) {
    return;
  }
  const auto name_at = [](const std::vector<hhds::GraphIO::DeclaredIoPin>& decls, hhds::Port_id pid) -> std::string_view {
    for (const auto& d : decls) {
      if (d.port_id == pid) {
        return d.name;
      }
    }
    return {};
  };
  for (const auto caller_gid : it->second) {
    auto caller = lib.get_graph(caller_gid);
    if (!caller) {
      continue;
    }
    std::optional<Stale_instance> stale;  // the first connected id that changed name
    const auto                    probe = [&](hhds::Port_id pid, bool input) {
      const auto was = name_at(input ? before.inputs : before.outputs, pid);
      const auto now = name_at(input ? gio.get_input_pin_decls() : gio.get_output_pin_decls(), pid);
      if (!stale && was != now) {
        stale = Stale_instance{.callee = std::string(gio.get_name()),
                               .caller = std::string(caller->get_name()),
                               .pid    = pid,
                               .was    = std::string(was),
                               .now    = std::string(now)};
      }
    };
    for (auto node : caller->body().nodes()) {
      if (stale) {
        break;
      }
      if (!livehd::graph_util::is_type_sub(node)) {
        continue;
      }
      auto child = node.get_subnode_io();
      if (!child || child->get_gid() != gio.get_gid()) {
        continue;
      }
      for (const auto& sink : node.inp_sorted_pins()) {
        probe(sink.get_port_id(), /*input=*/true);
      }
      for (const auto& drv : node.out_sorted_pins()) {
        probe(drv.get_port_id(), /*input=*/false);
      }
    }
    if (!stale) {
      continue;
    }
    stale->hint = stale_instance_hint(stale->caller);
    if (index.leftover.contains(caller_gid)) {
      io_guard_state().deferred.emplace_back(&lib, std::move(*stale));
      continue;
    }
    refuse_stale_instance(*stale);
  }
}

// Shared phase-1 io+clock+reset/activation GraphIO registration. Idempotent:
// the GraphIO ends up declaring exactly this module's ports (apply_io_plan).
// Returns the clock/reset binding for the body build; empty names = the module
// needs none.
[[nodiscard]] Io_setup setup_io_impl(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path,
                                     const uPass_tolg::Registry& registry) {
  auto& lib      = livehd::Hhds_graph_library::instance(lib_path);
  auto  mod_name = std::string(lnast->get_graph_name());

  auto gio = lib.find_io(mod_name);
  if (!gio) {
    gio = lib.create_io(mod_name);
  }

  // Declare I/O on the GraphIO: positional pin ids + literal bits + sign.
  // Boolean ports are always the unsigned one-bit realization, independently
  // of the legacy io_meta signed flag used by the distinct Pyrope bool kind.
  Io_plan       plan;
  hhds::Port_id pid     = 1;
  auto          declare = [&](const Lnast_io_entry& e, bool is_input) {
    // Canonical external port name: unquote a slang-read escaped id's backtick
    // form (`` `ar.x` `` -> `ar.x`) so the GraphIO port the LEC matches on is
    // identical to the yosys / Pyrope readers' name. (Mirrors canon_io_name in
    // the lowering class; this declare lambda is a free function so it
    // inlines.) Keep a whitespace content quoted — it cannot be a bare lg name.
    std::string_view nm = e.name;
    if (nm.size() >= 2 && nm.front() == '`' && nm.back() == '`') {
      auto inner = nm.substr(1, nm.size() - 2);
      if (inner.find_first_of(" \t\n\r\f\v") == std::string_view::npos) {
        nm = inner;
      }
    }
    uint32_t bits = e.kind == Io_kind::boolean ? 1u : (e.bits > 0 ? static_cast<uint32_t>(e.bits) : 1u);
    (void)plan.want(nm, is_input, pid);
    plan.stamp(nm, bits, e.kind == Io_kind::boolean || !e.is_signed);
    ++pid;  // positional: a repeated name still consumes its slot
  };
  for (const auto& e : lnast->io_meta().inputs) {
    declare(e, /*is_input=*/true);
  }
  for (const auto& e : lnast->io_meta().outputs) {
    declare(e, /*is_input=*/false);
  }

  // Append-only hidden activation ABI. Do not perturb ordinary public tops:
  // only a definition reached from a runtime-conditional call receives the
  // compiler-minted port. Lifted loop bodies already declare __valid, but only
  // runtime-control or conditionally called bodies consume it as execution
  // context; ordinary always-active loops keep their pre-activation netlist.
  std::string valid_name;
  bool        valid_minted   = false;
  bool        valid_active   = is_activation_capable(lnast, registry);
  const bool  explicit_valid = std::any_of(lnast->io_meta().inputs.begin(), lnast->io_meta().inputs.end(), [](const auto& e) {
    return e.name == "__valid";
  });
  if (valid_active || explicit_valid) {
    valid_name                 = "__valid";
    // `valid_minted` keeps its historical meaning: this call declared the
    // port. A GraphIO that already carried `__valid` (the phase-1 declaration
    // seen again by run(), or a reloaded library) reports false.
    const bool declared_before = gio->has_input(valid_name) || gio->has_output(valid_name);
    if (plan.want(valid_name, /*is_input=*/true, pid)) {
      ++pid;
      valid_minted = !declared_before;
    }
    plan.stamp(valid_name, 1, true);
  }

  // Implicit clock and reset (docs 04b "Implicit clock and reset"): they bind
  // BY TYPE. The implicit clock is the unit's single `Clock` input (any name);
  // the implicit reset its single `Reset` input. With two or more there is no
  // implicit one: clock_pin()/reset_pin() report the ambiguity lazily, at the
  // first reg/memory/stage/call that relies on it. With none, and state that
  // needs one (own regs, or a pipe/mod callee whose clock/reset must be
  // forwarded or auto-wired), a `clock` (`reset`) input is MINTED -- a compile
  // error when a non-Clock (non-Reset) port already has that name. (A
  // Verilog-origin unit's clk/rst inputs were stamped Clock/Reset by
  // upass.ssa from the reader's naming convention.)
  // The GraphIO spelling (canon_io_name's unquoting, as `declare` above): a
  // Clock named by a backticked type word (`` `U1`:Clock ``) is the port `U1`.
  const auto first_of = [&](Io_sig sig) -> std::string {
    for (const auto& e : lnast->io_meta().inputs) {
      if (e.sig == sig) {
        std::string_view nm = e.name;
        if (nm.size() >= 2 && nm.front() == '`' && nm.back() == '`') {
          if (auto inner = nm.substr(1, nm.size() - 2); inner.find_first_of(" \t\n\r\f\v") == std::string_view::npos) {
            nm = inner;
          }
        }
        return std::string(nm);
      }
    }
    return {};
  };
  // The io store declaring port `name` (inputs, or outputs), to locate a
  // diagnostic at the offending port.
  const auto port_decl_span = [&](std::string_view name, bool is_input) -> livehd::diag::Span {
    const auto io_n = lnast->get_first_child(lnast->get_root());
    if (io_n.is_invalid() || !Lnast_ntype::is_io(lnast->get_type(io_n))) {
      return {};
    }
    auto list = lnast->get_first_child(io_n);
    if (!is_input && !list.is_invalid()) {
      list = lnast->get_sibling_next(list);
    }
    for (auto st = list.is_invalid() ? list : lnast->get_first_child(list); !st.is_invalid(); st = lnast->get_sibling_next(st)) {
      if (const auto nm = lnast->get_first_child(st); !nm.is_invalid() && lnast->get_name(nm) == name) {
        return lnast->span_of(st);
      }
    }
    return {};
  };
  const auto mint_collision = [&](std::string_view minted, std::string_view type) {
    for (const auto* v : {&lnast->io_meta().inputs, &lnast->io_meta().outputs}) {
      for (const auto& e : *v) {
        if (e.name != minted) {
          continue;
        }
        const bool is_input = v == &lnast->io_meta().inputs;
        livehd::diag::err("upass.tolg", std::format("{}-collision", minted), "time")
            .at(port_decl_span(e.name, is_input))
            .msg("'{}' needs an implicit {} but has no `{}` input, and minting `{}:{}` clashes with its {} `{}`",
                 mod_name.substr(mod_name.rfind('.') + 1),  // the lambda, not the dotted `file.lambda` unit
                 minted,
                 type,
                 minted,
                 type,
                 is_input ? "non-" + std::string(type) + " input" : std::string("output"),
                 minted)
            .hint(is_input ? std::format("declare it `{}:{}` to make it the {}, or rename it", minted, type, minted)
                           : std::format("rename the output `{}`", minted))
            .fatal();
      }
    }
  };

  std::string clock_name;
  bool        clock_minted = false;
  {
    absl::flat_hash_map<std::string, bool> memo;
    absl::flat_hash_set<std::string>       visiting;
    if (needs_clock_rec(lnast, registry, memo, visiting)) {
      clock_name = first_of(Io_sig::clock);
      if (clock_name.empty()) {
        mint_collision("clock", "Clock");
        clock_name = "clock";
        if (plan.want(clock_name, /*is_input=*/true, pid)) {
          ++pid;
        }
        plan.stamp(clock_name, 1, true);
        clock_minted = true;
      }
    }
  }

  std::string reset_name;
  bool        reset_minted = false;
  bool        reset_neg    = false;  // a minted reset is active-high
  {
    absl::flat_hash_map<std::string, bool> memo;
    absl::flat_hash_set<std::string>       visiting;
    if (needs_reset_rec(lnast, registry, memo, visiting)) {
      reset_name = first_of(Io_sig::reset);
      if (!reset_name.empty()) {
        // A Pyrope reset name carries no polarity (docs 04b); the Verilog
        // reader's `_n` convention stands for a Verilog-origin unit.
        reset_neg = lnast->is_verilog_origin() && upass::io_port::reset_name_active_low(reset_name);
      } else {
        mint_collision("reset", "Reset");
        reset_name = "reset";
        if (plan.want(reset_name, /*is_input=*/true, pid)) {
          ++pid;
        }
        plan.stamp(reset_name, 1, true);
        reset_minted = true;
      }
    }
  }

  if (const auto before = apply_io_plan(lib, *gio, plan)) {
    check_foreign_instances(lib, *gio, *before, registry);
  }

  return {clock_name, clock_minted, reset_name, reset_minted, reset_neg, valid_name, valid_minted, valid_active};
}

}  // namespace

// A compiler-minted (`%`-named entity) unit — the comb a `test` block lowers
// to — is simulation-only: its body holds
// `tick`/`step`/`assert` that the `lhd sim` driver (prp_sim) runs, never
// synthesizable hardware. The front-end even drops the `tick` body (an
// unhandled statement), so a value written only inside the loop stays nil and
// its `assert` reads a driverless temp. `inou.cgen.sim` already skips these on
// the same `%`-entity signal; tolg must too. The io_meta().empty() guards below
// only catch a *parameterless* testbench — once a `test` declares a parameter
// (`test t(cycles:u20=20)`) it gains io_meta, defeating that guard, and the
// sim-only body would otherwise be lowered and fail with a dangling `cassert`
// reference.
static bool is_sim_only_unit(const std::shared_ptr<Lnast>& lnast) {
  const auto name   = lnast->get_graph_name();
  const auto entity = name.substr(name.rfind('.') + 1);
  return !entity.empty() && entity.front() == '%';
}

void uPass_tolg::detect_lg_collisions(const Registry& registry) {
  std::vector<std::pair<std::string, std::string>> seen;  // (graph name, owning unit)
  for (const auto& ln : registry) {
    if (!ln || ln->is_template() || ln->io_meta().empty()) {
      continue;  // same filter as register_io/run: only units that mint a
                 // GraphIO
    }
    std::string gname(ln->get_graph_name());
    std::string unit(ln->get_top_module_name());
    for (const auto& [g, u] : seen) {
      if (g == gname && u != unit) {
        livehd::diag::err("upass.tolg", "lg-name-collision", "type")
            .msg(
                "two units map to the same lgraph/module name '{}' (units "
                "'{}' and '{}')",
                gname,
                u,
                unit)
            .hint("give each `pub` definition a distinct name and/or filename")
            .fatal();
      }
    }
    seen.emplace_back(std::move(gname), std::move(unit));
  }

  // A Registry is normally stack-owned, so a later invocation may reuse the
  // same address with different Lnast objects. Refresh the non-owning ABI
  // analysis once per lowering pass rather than trusting pointer identity.
  reset_registry_abi(registry);
  reset_io_guard();

  // Every unit run() will lower in this pass (a restored body is final).
  auto& pending = lowering_pass().pending;
  pending.clear();
  lowering_pass().units.clear();
  for (const auto& ln : registry) {
    if (ln && !ln->is_template() && !ln->io_meta().empty() && !is_sim_only_unit(ln) && !ln->is_graph_restored()) {
      pending.emplace(ln->get_graph_name());
    }
    if (ln && !ln->is_template()) {
      lowering_pass().units.try_emplace(ln->get_graph_name(), ln.get());
    }
  }
}

namespace {
// The import bindings of `ln`'s top-level statements, binding -> unit text: an
// `import` call's destination (`func_call(Cnt, 'import', 'lib.ent')`) and a
// copy of one (`store(lib, %t)`). Before upass folds them, a call names its
// callee through such a binding (`Cnt(...)`, `lib.ent(...)`).
absl::flat_hash_map<std::string, std::string> top_level_import_bindings(const Lnast& ln) {
  absl::flat_hash_map<std::string, std::string> out;
  for (auto top : ln.children(ln.get_root())) {
    if (!Lnast_ntype::is_stmts(ln.get_type(top))) {
      continue;
    }
    for (auto stmt : ln.children(top)) {
      const auto dst = ln.get_first_child(stmt);
      const auto arg = dst.is_invalid() ? dst : ln.get_sibling_next(dst);
      if (arg.is_invalid() || !Lnast_ntype::is_ref(ln.get_type(dst))) {
        continue;
      }
      if (Lnast_ntype::is_func_call(ln.get_type(stmt))) {
        const auto txt = ln.get_sibling_next(arg);
        if (Lnast_ntype::is_const(ln.get_type(arg)) && ln.get_name(arg) == "import" && !txt.is_invalid()) {
          std::string_view t = ln.get_name(txt);
          if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"') && t.back() == t.front()) {
            t = t.substr(1, t.size() - 2);
          }
          out.insert_or_assign(std::string(ln.get_name(dst)), std::string(t));
        }
      } else if (Lnast_ntype::is_store(ln.get_type(stmt)) && Lnast_ntype::is_ref(ln.get_type(arg))
                 && ln.get_sibling_next(arg).is_invalid()) {
        if (const auto it = out.find(ln.get_name(arg)); it != out.end()) {
          auto text = it->second;
          out.insert_or_assign(std::string(ln.get_name(dst)), std::move(text));
        }
      }
    }
  }
  return out;
}
}  // namespace

uPass_tolg::Registry uPass_tolg::lowering_order(const Registry& registry, const std::function<bool(const Lnast&)>& follow) {
  // Units sharing a graph name are lowered back to back in registry order, so
  // the LAST one still wins as it did in plain registry order.
  absl::flat_hash_map<std::string_view, std::vector<size_t>> by_name;
  for (size_t i = 0; i < registry.size(); ++i) {
    if (registry[i]) {
      by_name[registry[i]->get_graph_name()].push_back(i);
    }
  }
  Registry                                   order;
  std::vector<bool>                          seen(registry.size(), false);  // visiting or emitted
  std::function<void(std::string_view name)> visit = [&](std::string_view name) {
    const auto& group = by_name.at(name);
    if (seen[group.front()]) {
      return;  // emitted, or an instantiation cycle (recursion is diagnosed elsewhere)
    }
    for (const size_t i : group) {
      seen[i] = true;
    }
    for (const size_t i : group) {
      const auto&                                                  ln = registry[i];
      std::optional<absl::flat_hash_map<std::string, std::string>> imports;  // built on the first unresolved name
      for (const auto& callee_name : collect_callee_names(ln)) {
        if (const auto it = by_name.find(callee_name); it != by_name.end()) {
          if (!follow || follow(*registry[it->second.front()])) {
            visit(callee_name);
          }
          continue;
        }
        auto callee = resolve_callee_lnast(callee_name, registry, ln->get_top_module_name());
        if (!callee) {
          // A callee named through an import binding (the pre-upass spelling).
          if (!imports) {
            imports = top_level_import_bindings(*ln);
          }
          const auto dot = callee_name.find('.');
          if (const auto it = imports->find(callee_name.substr(0, dot)); it != imports->end()) {
            const auto target = dot == std::string::npos ? it->second : it->second + callee_name.substr(dot);
            callee            = resolve_callee_lnast(target, registry, ln->get_top_module_name());
          }
        }
        if (callee && (!follow || follow(*callee))) {
          visit(callee->get_graph_name());
        }
      }
    }
    for (const size_t i : group) {
      order.push_back(registry[i]);
    }
  };
  for (const auto& ln : registry) {
    if (ln) {
      visit(ln->get_graph_name());
    }
  }
  return order;
}

void uPass_tolg::set_prior_units(const std::vector<std::string>& units) {
  auto& state       = io_guard_state();
  state.prior_units = units;
  state.index       = {};  // an index built before would miss the leftovers
}

void uPass_tolg::set_library_origins(std::string working_lib, std::vector<std::string> input_dirs,
                                     std::vector<std::pair<std::string, std::string>> absorbed) {
  auto& origins   = io_guard_state().origins;
  origins.known   = true;
  origins.working = std::move(working_lib);
  origins.inputs  = std::move(input_dirs);
  origins.absorbed.clear();
  for (auto& [module, dir] : absorbed) {
    origins.absorbed.insert_or_assign(std::move(module), std::move(dir));
  }
}

void uPass_tolg::check_leftover_instances() {
  auto deferred = std::move(io_guard_state().deferred);
  io_guard_state().deferred.clear();
  for (const auto& [lib, stale] : deferred) {
    if (lib->find_io(stale.caller)) {
      refuse_stale_instance(stale);  // the prune kept it: something live still instantiates it
    }
  }
}

// Decide ONE surviving `cassert` node on the post-upass tree, with exactly the
// predicate lower_cassert applies to the folded driver pin.
static void decide_unlowered_cassert(const std::shared_ptr<Lnast>& lnast, const Lnast_nid& nid) {
  auto cond = lnast->get_first_child(nid);
  if (cond.is_invalid()) {
    return;
  }
  // `assert` / `assert_always` / `assume` share this node type and are DESIGN
  // obligations: they may legally stay unresolved (a testbench `assert` is the
  // runtime check prp_sim emits, an `assert` in a body becomes an fproperty).
  // Only `cassert` is an elaboration check. The kind rides an EXACT-match
  // sentinel const child ahead of the optional user message — never a substring
  // search, or a message merely CONTAINING the text would retype the
  // obligation (the false-PROVEN bug lower_cassert documents).
  auto kind_nid = lnast->get_sibling_next(cond);
  if (kind_nid.is_invalid() || !Lnast_ntype::is_const(lnast->get_type(kind_nid))
      || lnast->get_name(kind_nid) != "__fkind__cassert") {
    return;
  }
  std::string msg;
  if (auto m = lnast->get_sibling_next(kind_nid); !m.is_invalid() && Lnast_ntype::is_const(lnast->get_type(m))) {
    msg = std::string{lnast->get_name(m)};
    if (msg.size() >= 2 && msg.front() == '\'' && msg.back() == '\'') {
      msg = msg.substr(1, msg.size() - 2);  // strip Lconst::to_pyrope quoting
    }
  }
  const bool const_cond = Lnast_ntype::is_const(lnast->get_type(cond));
  if (const_cond) {
    auto v = Dlop::from_pyrope(lnast->get_name(cond));
    // Discharge ONLY on a known-TRUE fold, or on a comptime nil (an unset
    // attribute reads as nil; upass.verifier discharges that same way per
    // attributes_spec §Phase 2). `!is_known_false()` is NOT this predicate: an
    // X/unknown constant is const and not known-false, so it would slip through
    // as "proven" — and a cassert the compiler cannot decide is exactly the
    // case that must fail.
    if (v && !v->is_invalid() && (v->is_nil() || v->is_known_true())) {
      return;
    }
  }
  std::string text = const_cond ? "upass.tolg: cassert condition is not true at compile time"
                                : "upass.tolg: cassert condition did not fold to a "
                                  "compile-time value";
  if (!msg.empty()) {
    text += ": " + msg;
  }
  livehd::diag::err("upass.tolg", const_cond ? "cassert-false" : "cassert-not-comptime", "unsupported")
      .at(lnast->span_of(nid))
      .msg("{}", text)
      .hint(
          "cassert is an elaboration check: it must fold to true at compile "
          "time; use `assert` for a condition that must hold of the hardware")
      .fatal();
}

// `cassert` is an ELABORATION check: the compiler folds it to true, or it
// is a diagnostic error. It never becomes an LGraph
// node, a netlist check, a simulation check or a formal obligation — that is
// exactly what separates it from `assert`. `lower_cassert` enforces that for
// every lambda BODY the builder walks, but two trees are handed to tolg and
// returned UNLOWERED, so no lower_cassert ever runs on them:
//
//   * the FILE-SCOPE statement tree (empty io_meta — the top-level
//     `mut`/`const`/`cassert` statements of the source file), and
//   * a `%`-named SIM-ONLY unit (the comb a `test` block lowers to),
//     whose casserts inou.prp (prp_sim) would otherwise code-generate as
//     RUNTIME checks that fail during `lhd sim` instead of at build time. The
//     verifier is stripped for those minted units, so even a comptime-FALSE
//     one survives here undiagnosed.
//
// Both are CLOSED scopes: no call site is left that could bind a value and fold
// the condition later, so an undischarged cassert there is final. A TEMPLATE
// body is the opposite case — it is realized (and folded) per call site — and a
// pre-elaborated import / `.__pub` index is not re-walked this run; all three
// are skipped.
//
// This runs at the tolg seam on purpose: reaching tolg is what says "this
// compilation is producing hardware". An LNAST-only flow (lnast-dump, the LSP,
// the `comptime`/`upass` test tiers, which all pass upass.tolg=false) may still
// carry an unresolved cassert, exactly as the ruling allows.
static void check_unlowered_casserts(const std::shared_ptr<Lnast>& lnast) {
  if (!lnast || lnast->is_template() || lnast->is_pre_elaborated() || lnast->get_top_module_name().ends_with(".__pub")) {
    return;
  }
  // Whole-subtree walk: a cassert can sit inside a `tick` body, an `if`/`uif`
  // arm or a nested `stmts`. (A DEAD `comptime if` / const-`if` arm is already
  // gone — the runner prunes the untaken arm and its casserts with it — so this
  // never sees one. A `match` lowers to a `uif` that KEEPS its untaken arms;
  // deciding the casserts in them is the same behavior lower_cassert already
  // has for lambda bodies.)
  std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (Lnast_ntype::is_cassert(lnast->get_type(c))) {
        decide_unlowered_cassert(lnast, c);
      }
      walk(c);
    }
  };
  walk(lnast->get_root());
}

void uPass_tolg::gate_activation_clocks(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  // Calls may be lowered before their callee body. In that case HHDS can only
  // classify the Sub from its boundary declarations, so a stateful callee with
  // no loop-break port is provisionally stamped combinational. Refresh every
  // instance bottom-up now that all bodies exist; otherwise a legal feedback
  // path through a child flop remains a local combinational cycle and cgen's
  // cycle tail emits blocking assignments in storage order (a consumer can
  // appear before its producer).
  absl::flat_hash_set<hhds::Graph*> refreshed;
  absl::flat_hash_set<hhds::Graph*> refreshing;
  std::function<void(hhds::Graph*)> refresh_subs = [&](hhds::Graph* graph) {
    if (graph == nullptr || refreshed.contains(graph) || !refreshing.insert(graph).second) {
      return;
    }
    for (auto node : graph->body().nodes()) {
      if (!livehd::graph_util::is_type_sub(node)) {
        continue;
      }
      auto gio = node.get_subnode_io();
      if (gio == nullptr) {
        continue;
      }
      if (gio->has_graph()) {
        refresh_subs(gio->get_graph().get());
      }
      if (auto loop = node.subnode_loop()) {
        node.set_subnode(gio, *loop);
      } else {
        node.set_subnode(gio);
      }
    }
    refreshing.erase(graph);
    refreshed.insert(graph);
  };
  for (const auto& graph : graphs) {
    refresh_subs(graph.get());
  }

  livehd::latch_contract::Clock_port_cache cache;
  for (const auto& graph : graphs) {
    if (graph) {
      (void)livehd::latch_contract::gate_activation_clocks(graph.get(), "upass.tolg", cache);
    }
  }
}

void uPass_tolg::register_io(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path, const Registry& registry) {
  if (!lnast || lnast->io_meta().empty()) {
    return;  // not a lowerable module (e.g. the empty file-root tree)
  }
  if (is_sim_only_unit(lnast)) {
    return;  // testbench comb — never reserve a GraphIO for it
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
    // Not a lowerable module (the file-root tree, or a parameterless `test`
    // block). No lower_cassert will run on it, so discharge-or-fail its
    // casserts here before dropping it.
    check_unlowered_casserts(lnast);
    return nullptr;
  }
  // One perfetto slice per lowered unit (profiling builds only) — tolg is
  // invoked directly by the kernel (not via run_step), so without this the
  // whole LNAST->LGraph phase was a blank stretch in the trace.
  TRACE_EVENT("pass", "lnast.tolg", "unit", std::string(lnast->get_top_module_name()));
  if (is_sim_only_unit(lnast)) {
    // Testbench comb — checked by `lhd sim`, not lowered to hardware.
    // Its `assert`s stay for prp_sim; its `cassert`s are elaboration checks
    // that must be discharged here (nothing downstream can).
    check_unlowered_casserts(lnast);
    return nullptr;
  }
  // A deferred template produces no LGraph (see register_io). A
  // template selected as a synthesis top simply yields no module; it is never
  // a hard error at definition time (contract decision 3).
  if (lnast->is_template()) {
    return nullptr;
  }

  auto io_setup = setup_io_impl(lnast, lib_path, registry);
  const_clock_memo().clear();  // a body lowered since the last unit may have replaced one it read

  auto& lib = livehd::Hhds_graph_library::instance(lib_path);
  auto  gio = lib.find_io(std::string(lnast->get_graph_name()));  // 2f-lg: lg override or mangled name
  // Re-emitting into a --emit-dir that already holds a prior build: instance()
  // deserializes the persisted bodies from disk (GraphLibrary::load
  // materializes every graph_<gid>/body.bin), so gio->has_graph() is true here
  // even though we are about to rebuild this module from its LNAST — the source
  // of truth. Reusing that stale body would make builder.build() append a
  // SECOND copy of the whole module on top of it, fabricating register-feedback
  // cycles the Time_checker then mis-reports as "register feedback through
  // stage registers" (so a clean design crashes on the 2nd `compile` into the
  // same lg: dir). Drop the loaded body but keep the gid + IO decls (set up
  // just above) so the rebuilt graph reuses the same stable IDs across runs.
  // (An interface change already dropped the body there: apply_io_plan.)
  if (gio->has_graph()) {
    lib.delete_graph(gio->get_gid());
  }
  auto g_shared = gio->create_graph();

  Tolg builder(lnast, g_shared.get(), std::move(io_setup), &registry, &lib, reset_style == "async");
  builder.build();

  // The combined pipe/mod time checker at the tolg seam. A `comb` runs it too
  // (2c-wire) — but only its acyclicity phase — to catch a combinational loop
  // through a self-feeding wire in a standalone-compiled comb top.
  {
    const auto kind = lnast->get_lambda_kind();
    if (kind == "pipe" || kind == "mod" || kind == "comb") {
      Time_checker checker(g_shared.get(),
                           lnast,
                           builder.take_pending_checks(),
                           builder.take_flop_depths(),
                           builder.take_sub_times(),
                           builder.take_plain_reg_flops(),
                           builder.take_inserted_flops(),
                           builder.take_wire_cuts());
      checker.run();
    }
  }
  lowering_pass().pending.erase(lnast->get_graph_name());  // its body is final now
  // Record the finished body's comb reach for its callers' loop checks.
  livehd::port_reach::stamp(*g_shared, livehd::port_reach::Cache(callee_reach, /*slices=*/false).of(g_shared));

  g_shared->commit();

#ifndef NDEBUG
  // tolg output invariant (-c dbg): every value-producing cell must be sized.
  // Self-check here (not only at cprop entry) so the guarantee holds for every
  // direct caller of tolg as well as the standard compile pipeline.
  livehd::graph_util::debug_assert_cells_sized(*g_shared, "upass.tolg");
#endif

  return g_shared;
}
