// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_sim.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <print>
#include <string>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "cell.hpp"        // Ntype / Ntype_op
#include "diag.hpp"        // livehd::diag::err — Stage 0 comb-loop safety net
#include "node_util.hpp"  // //graph:graph — livehd::graph_util::* helpers
#include "split_selfref.hpp"  // //graph — word-level false-loop splitter (also run by pass/cprop)
#include "str_tools.hpp"  // str_tools::ends_with

using livehd::graph_util::bits_of;
using livehd::graph_util::debug_name;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::hydrate_const;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_type_flop;
using livehd::graph_util::is_type_register;
using livehd::graph_util::is_type_sub;
using livehd::graph_util::is_unsign;
using livehd::graph_util::pin_name_of;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {
// Width of a pin, floored at 1 (Slop<N> requires N >= 1).
int wbits_of(const hhds::Pin_class& pin) {
  int b = pin.is_invalid() ? 1 : bits_of(pin);
  return b <= 0 ? 1 : b;
}

// Edges of a node sorted by sink port_id (selector/operand order).
auto sorted_inp(const hhds::Node_class& node) {
  auto edges = node.inp_edges();
  std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.sink.get_port_id() < b.sink.get_port_id(); });
  return edges;
}

const char* op_name(Ntype_op op) {
  switch (op) {
    case Ntype_op::Sum: return "Sum";
    case Ntype_op::Mult: return "Mult";
    case Ntype_op::Div: return "Div";
    case Ntype_op::And: return "And";
    case Ntype_op::Or: return "Or";
    case Ntype_op::Xor: return "Xor";
    case Ntype_op::Not: return "Not";
    case Ntype_op::LT: return "LT";
    case Ntype_op::GT: return "GT";
    case Ntype_op::EQ: return "EQ";
    case Ntype_op::SHL: return "SHL";
    case Ntype_op::SRA: return "SRA";
    case Ntype_op::Mux: return "Mux";
    case Ntype_op::Hotmux: return "Hotmux";
    case Ntype_op::Get_mask: return "Get_mask";
    case Ntype_op::Set_mask: return "Set_mask";
    case Ntype_op::Sext: return "Sext";
    case Ntype_op::Nconst: return "Nconst";
    default: return "op?";
  }
}
}  // namespace

std::string Cgen_sim::cpp_id(std::string_view name) {
  std::string r;
  r.reserve(name.size() + 1);
  // strip LNAST backtick quotes (`a[0]`)
  if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
    name.remove_prefix(1);
    name.remove_suffix(1);
  }
  for (char c : name) {
    r.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
  }
  if (r.empty() || std::isdigit(static_cast<unsigned char>(r.front()))) {
    r.insert(r.begin(), '_');
  }
  return r;
}

// A graph name (`file.entity`, or `../dir/file.entity` for a path-qualified
// import) is used verbatim as the emitted .hpp/.cpp basename and in the sibling
// `#include`s. A '.' is legal in a filename, but a '/' (or the '/' inside a
// `..`) from a relative-path import is a directory separator that would write
// into a non-existent subdir. Sanitize ONLY the separators to '_', keeping the
// dotted `file.entity` form so ordinary (dot-only) filenames are unchanged.
// Applied identically to the module's own name and to a child's name in the
// `#include`, so the reference always resolves to the emitted file.
static std::string sim_file_stem(std::string_view name) {
  std::string s(name);
  for (auto& c : s) {
    if (c == '/' || c == '\\') {
      c = '_';
    }
  }
  return s;
}

hhds::Pin_class Cgen_sim::get_driver(const hhds::Pin_class& sink) {
  if (sink.is_invalid()) {
    return {};
  }
  auto edges = sink.inp_edges();
  if (edges.empty()) {
    return {};
  }
  return edges.front().driver;
}

hhds::Pin_class Cgen_sim::find_sink_pin(const hhds::Node_class& node, std::string_view name) {
  if (node.is_invalid()) {
    return {};
  }
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    // Same invalid-on-miss contract for sub instances: resolve the name via the
    // sub-graph's GraphIO decls and walk inp_edges — a declared input that was
    // never connected has no materialized pin, and hhds get_sink_pin asserts.
    auto sub_io = node.get_subnode_io();
    if (!sub_io || !sub_io->has_input(name)) {
      return {};
    }
    auto pid = sub_io->get_input_port_id(name);
    for (const auto& e : node.inp_edges()) {
      if (e.sink.get_port_id() == pid) {
        return e.sink;
      }
    }
    return {};
  }
  auto pid = Ntype::get_sink_pid(op, name);
  if (pid == livehd::Port_invalid) {
    return {};
  }
  for (const auto& e : node.inp_edges()) {
    if (e.sink.get_port_id() == pid) {
      return e.sink;
    }
  }
  return {};
}

hhds::Pin_class Cgen_sim::find_driver_pin(const hhds::Node_class& node, std::string_view name) {
  // Invalid-on-miss probe for a Sub instance's declared output: a declared
  // output with no consumer has no materialized pin (hhds get_driver_pin
  // asserts on it), and an edge-less pin is unnamed to the emitted code
  // anyway — walk out_edges and match the resolved port_id.
  if (node.is_invalid()) {
    return {};
  }
  auto sub_io = node.get_subnode_io();
  if (!sub_io || !sub_io->has_output(name)) {
    return {};
  }
  auto pid = sub_io->get_output_port_id(name);
  for (const auto& e : node.out_edges()) {
    if (e.driver.get_port_id() == pid) {
      return e.driver;
    }
  }
  return {};
}

std::string Cgen_sim::operand(const hhds::Pin_class& dpin, int target_bits, int sign_mode) {
  const std::string tw = std::to_string(target_bits);
  if (dpin.is_invalid()) {
    return absl::StrCat("Slop<", tw, ">::create_integer(0)");
  }
  if (is_const_pin(dpin)) {
    // Exact via the shared pyrope codec (handles wide constants too).
    return absl::StrCat("Slop<", tw, ">::from_pyrope(\"", hydrate_const(dpin).to_pyrope(), "\")");
  }
  auto it = pin2var.find(dpin.get_class_index());
  if (it == pin2var.end()) {
    // A VALID, NON-CONST driver with no pin2var binding can only be a combinational
    // cycle back-edge: its producer node sorts AFTER this use in forward_class (a
    // real comb loop, or a FALSE loop through an atomic Sub call). There is no valid
    // schedule; record it so do_from_graph() fails loudly instead of silently
    // simulating 0. (A genuinely undriven pin already returned at is_invalid above.)
    cycle_unresolved_ = true;
    if (cycle_first_label_.empty()) {
      cycle_first_label_ = absl::StrCat("`", debug_name(dpin.get_master_node()), "` (a combinational-cycle back-edge)");
    }
    return absl::StrCat("Slop<", tw, ">::create_integer(0) /*UNRESOLVED-CYCLE*/");  // already a Slop<tw> expr
  }
  const std::string& base = it->second;
  // A Sum with a subtrahend (a `-` operand, sink pid != 0) can go negative; its
  // value wraps at the node width, so WIDENING it must sign-extend to propagate
  // the borrow -- matching Verilog re-evaluating `a - b` at the consumer width.
  // Safe: a non-negative sum has top bit 0, so sext == zext there.
  bool sum_with_sub = false;
  if (sign_mode == 0 && is_unsign(dpin)) {
    auto mn = dpin.get_master_node();
    if (!mn.is_invalid() && type_op_of(mn) == Ntype_op::Sum) {
      for (const auto& ie : mn.inp_edges()) {
        if (ie.sink.get_port_id() != 0) {
          sum_with_sub = true;
          break;
        }
      }
    }
  }
  const bool unsigned_ = !sum_with_sub && ((sign_mode == -1) || (sign_mode == 0 && is_unsign(dpin)));
  if (unsigned_) {
    return absl::StrCat(base, ".zext_to<", tw, ">()");  // zero-extend / mask
  }
  return absl::StrCat("Slop<", tw, ">{", base, "}");  // signed sext via the hlop cross-width ctor
}

std::string Cgen_sim::node_expr(const hhds::Node_class& node, int wbits) {
  const auto op = type_op_of(node);
  const auto tw = std::to_string(wbits);
  auto       e  = sorted_inp(node);

  auto fold = [&](const char* method) -> std::string {
    if (e.empty()) {
      return absl::StrCat("Slop<", tw, ">::create_integer(0)");
    }
    std::string s = operand(e[0].driver, wbits);
    for (size_t i = 1; i < e.size(); ++i) {
      s = absl::StrCat(s, ".", method, "(", operand(e[i].driver, wbits), ")");
    }
    return s;
  };

  switch (op) {
    case Ntype_op::Sum: {
      std::string adds, subs;
      for (const auto& ed : e) {
        auto& tgt = (ed.sink.get_port_id() == 0) ? adds : subs;
        if (!tgt.empty()) {
          tgt += ", ";
        }
        tgt += operand(ed.driver, wbits);
      }
      return absl::StrCat("Slop<", tw, ">::sum_op({", adds, "}, {", subs, "})");
    }
    case Ntype_op::And: return fold("and_op");
    case Ntype_op::Or: return fold("or_op");
    case Ntype_op::Xor: return fold("xor_op");
    case Ntype_op::Mult: return fold("mult_op");
    case Ntype_op::Div: {
      // Binary, order-sensitive, and sign-aware (slop div_op dispatches on the
      // operand sign). Without this case Div fell through to the default and
      // simulated as the bare dividend. Read each operand at its own width with
      // one headroom bit when either side is signed, so a signed operand actually
      // sign-extends before the divide (same reasoning as the LT/GT/SRA cases);
      // the quotient then fits to the node width on assignment.
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits);
      }
      int cw = std::max({wbits_of(e[0].driver), wbits_of(e[1].driver), wbits, 1});
      if (!is_unsign(e[0].driver) || !is_unsign(e[1].driver)) {
        cw += 1;
      }
      return absl::StrCat(operand(e[0].driver, cw), ".div_op(", operand(e[1].driver, cw), ")");
    }
    case Ntype_op::Ror: {
      // OR-reduction: 1 iff ANY bit of ANY operand is set. Each operand must be
      // read at its OWN full width (not the 1-bit node width), then reduced --
      // `a.ror_op()` is the unary reduce, `a.ror_op(b)` reduces both. Matches the
      // Verilog cgen `|a` / `|{a|b|...}`.
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      auto ow = [&](size_t i) { return operand(e[i].driver, std::max(wbits_of(e[i].driver), 1)); };
      std::string s = (e.size() == 1) ? absl::StrCat(ow(0), ".ror_op()") : ow(0);
      for (size_t i = 1; i < e.size(); ++i) {
        s = absl::StrCat(s, ".ror_op(", ow(i), ")");
      }
      return absl::StrCat("(", s, ").zext_to<", tw, ">()");
    }
    case Ntype_op::Not:
      return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : absl::StrCat(operand(e[0].driver, wbits), ".not_op()");
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::EQ: {
      if (e.size() < 2) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      int cw = std::max({wbits_of(e[0].driver), wbits_of(e[1].driver), 1});
      // An ORDERED compare (LT/GT) is sign-aware: a signed operand read at its OWN
      // width is not sign-extended, so its stored value stays a positive magnitude
      // (0xF8 == 248, not -8) and the compare goes wrong. Give one extra bit of
      // headroom when EITHER side is signed so `operand()`'s signed read actually
      // sign-extends. EQ is a bit-pattern compare -- no headroom (would break the
      // signed-vs-unsigned same-bits case).
      if (op != Ntype_op::EQ && (!is_unsign(e[0].driver) || !is_unsign(e[1].driver))) {
        cw += 1;
      }
      const char* m = (op == Ntype_op::LT) ? "lt_op" : (op == Ntype_op::GT) ? "gt_op" : "eq_op";
      // Slop's compare ops return a Bool whose true value is all-ones (-1); a bare
      // zext_to<tw> would leave `tw` all-ones (e.g. 3 for a 2-bit consumer) instead
      // of 1. Clamp to a single bit first so the boolean is 0/1 in any width.
      return absl::StrCat(operand(e[0].driver, cw), ".", m, "(", operand(e[1].driver, cw), ").zext_to<1>().zext_to<", tw,
                          ">()");
    }
    case Ntype_op::SHL:
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits);
      }
      return absl::StrCat(operand(e[0].driver, wbits), ".shl_op(", operand(e[1].driver, wbits), ")");
    case Ntype_op::SRA:
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits, /*signed=*/1);
      }
      // arithmetic shift: the shifted operand must be read as signed
      return absl::StrCat(operand(e[0].driver, wbits, /*signed=*/1), ".sra_op(", operand(e[1].driver, wbits), ")");
    case Ntype_op::Get_mask: {
      // value (e[0]) + optional mask (e[1]). The unary form is the common tolg
      // width-adjust; lower it to a plain zext.
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      if (e.size() == 1) {
        return operand(e[0].driver, wbits, /*unsigned=*/-1);
      }
      int cw = std::max({wbits_of(e[0].driver), wbits_of(e[1].driver), wbits, 1});
      // A constant mask can select bits ABOVE the value's declared width (e.g.
      // `b#[12..=15]` on a 9-bit `b`, reaching into the sign region). Widen the
      // compare width to the mask's true span so the value operand undergoes a
      // genuine SIGNED cross-width widen (sign-extend) rather than a same-width
      // no-op copy that would read the out-of-range bits as 0.
      if (is_const_pin(e[1].driver)) {
        cw = std::max(cw, static_cast<int>(hydrate_const(e[1].driver).get_bits()));
      }
      // The value is normally read UNSIGNED (get_mask yields a non-negative
      // magnitude). Two corrections, both keyed off a CONSTANT mask:
      //  * A finite mask that reaches past the value width extracts the value's
      //    SIGN bits (e.g. `(x#sext[..])#[0..=63]`), so read the value per its
      //    declared sign -- EXCEPT the mask==-1 "to positive" idiom stays unsigned.
      //  * A single selected bit makes get_mask_op return the SIGNED 1-bit value
      //    (-1 when set), so clamp the packed result to one bit -> magnitude 0/1.
      int  val_sign   = -1;
      bool single_bit = false;
      if (is_const_pin(e[1].driver)) {
        auto mv  = hydrate_const(e[1].driver);
        val_sign = (mv.is_just_i64() && mv.to_just_i64() == -1) ? -1 : 0;
        single_bit = !mv.is_negative() && mv.popcount() == 1;
      }
      std::string gm = absl::StrCat("(", operand(e[0].driver, cw, val_sign), ".get_mask_op(",
                                    operand(e[1].driver, cw, -1), "))");
      if (single_bit) {
        gm = absl::StrCat(gm, ".zext_to<1>()");
      }
      return absl::StrCat(gm, ".zext_to<", tw, ">()");
    }
    case Ntype_op::Set_mask: {
      // value.set_mask_op(mask, newbits) — best effort at the node width. The
      // inserted bits (e[2]) are read per their declared sign so a SIGNED source
      // sign-fills a slot wider than its width (e.g. `s#[5..=12] = i#sext[28..=31]`);
      // an unsigned source is unchanged (is_unsign -> zero-extend).
      if (e.size() < 3) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits, -1);
      }
      return absl::StrCat(operand(e[0].driver, wbits, -1), ".set_mask_op(", operand(e[1].driver, wbits, -1), ", ",
                          operand(e[2].driver, wbits), ")");
    }
    case Ntype_op::Sext: {
      // value sign-extended from a bit position (2nd input, normally constant).
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      int frombit = wbits - 1;
      if (e.size() > 1 && is_const_pin(e[1].driver)) {
        // LGraph Sext(a, b) keeps b bits [b-1:0] (cgen_verilog emits `a[b-1:0]`),
        // i.e. the sign bit is at b-1. Slop::sext_op(fb) takes fb as the sign-bit
        // position, so pass b-1 (NOT b) or the value stays unsigned/off-by-one.
        frombit = static_cast<int>(hydrate_const(e[1].driver).to_just_i64()) - 1;
      }
      // read the source wide enough to preserve the sign bit before extending
      int sw = std::max({wbits, frombit + 1, wbits_of(e[0].driver)});
      return absl::StrCat("Slop<", tw, ">{", operand(e[0].driver, sw, /*signed=*/1), ".sext_op(",
                          std::to_string(frombit), ")}");
    }
    case Ntype_op::Mux:
    case Ntype_op::Hotmux: {
      if (e.size() < 3) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      std::string vals;
      for (size_t i = 1; i < e.size(); ++i) {
        if (!vals.empty()) {
          vals += ", ";
        }
        vals += operand(e[i].driver, wbits);
      }
      const char* m   = (op == Ntype_op::Mux) ? "mux_op" : "hotmux_op";
      std::string sel = operand(e[0].driver, wbits);  // Slop<tw> selector
      if (op == Ntype_op::Mux) {
        // A binary Mux selector is an INDEX (0..n-1). Slop encodes a boolean
        // `true` as all-ones, which as a wide selector value falls outside
        // [0,n) -> mux_op returns invalid and the mux never picks. Keep only
        // the ceil(log2(n)) low index bits (then re-widen to tw): all-ones -> 1.
        size_t n_vals = e.size() - 1;
        int    sel_w  = 1;
        while ((static_cast<size_t>(1) << sel_w) < n_vals) {
          ++sel_w;
        }
        if (sel_w < wbits) {
          sel = absl::StrCat("(", sel, ").zext_to<", sel_w, ">().zext_to<", wbits, ">()");
        }
      }
      return absl::StrCat("Slop<", tw, ">::", m, "(", sel, ", {", vals, "})");
    }
    default:
      // Compiling fallback: pass the first input through at the node width (the
      // per-operand width conversion already enforces wbits). Covers width-trim
      // Get_mask and not-yet-modeled ops; the iverilog differential test flags
      // any that need exact lowering.
      return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits);
  }
}

namespace {
// Inline a PURE-COMB Sub instance that sits on a FALSE combinational loop -- its
// output feeds back into one of its OWN inputs through parent logic -- into `g`.
// inou.cgen.sim schedules a Sub atomically (all inputs -> all outputs via one
// child.cycle()), so such an instance is an uncuttable node-level cycle even
// though the callee's output cones are independent; flattening it lets
// forward_class order the now-flat DAG. Only STATELESS (pure-comb) callees are
// flattened, so flop/memory/VCD/checkpoint semantics are unchanged, and only a
// Sub genuinely ON a false loop is touched (normal hierarchical sims are
// identical). Runs on the live sim graph, before emission. Returns #flattened.
int flatten_false_loop_subs(hhds::Graph* g) {
  namespace gu = livehd::graph_util;

  // The whole CLOSURE must be state-free: nested comb Subs are fine (the clone
  // below re-instantiates them in `g` as ordinary pure-comb leaf instances --
  // the ExeUnitImp_4/Alu/AluDataModule shape), but any Flop/Latch/Fflop/Memory
  // anywhere makes inlining change state identity, so those are never touched.
  auto callee_closure_is_comb = [](auto&& self, const std::shared_ptr<hhds::Graph>& cg) -> bool {
    if (!cg) {
      return false;
    }
    for (auto n : cg->fast_class()) {
      auto op = gu::type_op_of(n);
      if (op == Ntype_op::Memory || op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Fflop) {
        return false;
      }
      if (op == Ntype_op::Sub && !self(self, n.get_subnode_graph())) {
        return false;
      }
    }
    return true;
  };

  auto driver_of = [](const hhds::Pin_class& sink) -> hhds::Pin_class {
    if (sink.is_invalid()) {
      return {};
    }
    for (auto e : sink.get_master_node().inp_edges()) {
      if (e.sink.get_port_id() == sink.get_port_id()) {
        return e.driver;
      }
    }
    return {};
  };

  // A Sub S is on a false loop iff a backward COMB walk from one of its input
  // drivers reaches S's own output (stopping at any other state/loop_last).
  auto on_false_loop = [&](const hhds::Node_class& s) {
    absl::flat_hash_set<hhds::Node_class> seen;
    std::vector<hhds::Pin_class>          stk;
    for (auto e : s.inp_edges()) {
      stk.push_back(e.driver);
    }
    while (!stk.empty()) {
      auto d = stk.back();
      stk.pop_back();
      if (d.is_invalid() || gu::is_const_pin(d)) {
        continue;
      }
      auto m = d.get_master_node();
      if (m == s) {
        return true;
      }
      auto op = gu::type_op_of(m);
      if (op == Ntype_op::Sub || op == Ntype_op::Memory || gu::is_type_register(m) || op == Ntype_op::IO) {
        continue;  // a real state boundary -- the loop does not thread through it
      }
      if (!seen.insert(m).second) {
        continue;
      }
      for (auto e : m.inp_edges()) {
        stk.push_back(e.driver);
      }
    }
    return false;
  };

  // Fixpoint rounds: inlining one level can MOVE the false loop onto a nested
  // instance that just became a direct child (Alu -> AluDataModule), so rescan
  // until no on-false-loop comb-closure Sub remains (bounded).
  int flattened = 0;
  for (int round = 0; round < 8; ++round) {
  std::vector<hhds::Node_class> targets;
  for (auto node : g->fast_class()) {
    if (gu::type_op_of(node) != Ntype_op::Sub) {
      continue;
    }
    if (!callee_closure_is_comb(callee_closure_is_comb, node.get_subnode_graph()) || !on_false_loop(node)) {
      continue;
    }
    targets.push_back(node);
  }
  if (targets.empty()) {
    break;
  }
  flattened += static_cast<int>(targets.size());

  for (auto& sub : targets) {
    auto cg  = sub.get_subnode_graph();
    auto sio = sub.get_subnode_io();
    if (!cg || !sio) {
      continue;
    }
    // The Sub's driver for each input port (by port id) -- feeds a callee input.
    absl::flat_hash_map<uint32_t, hhds::Pin_class> sub_in_drv;
    for (auto e : sub.inp_edges()) {
      sub_in_drv[static_cast<uint32_t>(e.sink.get_port_id())] = e.driver;
    }

    // (a) copy every callee comb node into g (consts/IO ports handled on demand).
    absl::flat_hash_map<hhds::Node_class, hhds::Node_class> nmap;
    for (auto cn : cg->fast_class()) {
      auto op = gu::type_op_of(cn);
      if (op == Ntype_op::IO || op == Ntype_op::Nconst) {
        continue;
      }
      auto neo = gu::create_typed_node(*g, op);
      if (op == Ntype_op::Sub) {
        // a nested comb Sub is re-instantiated in g as an ordinary child
        // instance (the closure check above guarantees it is state-free)
        neo.set_subnode(cn.get_subnode_io());
      }
      if (gu::has_name(cn)) {
        neo.attr(hhds::attrs::name).set(std::string{gu::node_name_of(cn)});
      }
      nmap[cn] = neo;
    }

    // A callee driver pin -> the equivalent driver pin in g.
    auto map_driver = [&](const hhds::Pin_class& cdrv) -> hhds::Pin_class {
      if (gu::is_graph_input_pin(cdrv)) {
        // a callee INPUT port -> the Sub's driver for that port (same port id)
        auto it = sub_in_drv.find(static_cast<uint32_t>(cdrv.get_port_id()));
        return it == sub_in_drv.end() ? hhds::Pin_class{} : it->second;
      }
      if (gu::is_const_pin(cdrv)) {
        return gu::create_const(*g, gu::hydrate_const(cdrv));  // recreate the const in g
      }
      auto mit = nmap.find(cdrv.get_master_node());
      if (mit == nmap.end()) {
        return {};  // an uncopied node (should not happen for a pure-comb callee)
      }
      auto neo = mit->second;
      auto np  = neo.create_driver_pin(cdrv.get_port_id());
      if (auto b = gu::bits_of(cdrv); b != 0) {
        gu::set_bits(np, b);
      }
      if (!gu::is_unsign(cdrv)) {
        gu::set_sign(np);
      }
      if (auto pn = gu::pin_name_of(cdrv); !pn.empty()) {
        gu::set_pin_name(np, pn);
      }
      return np;
    };

    // (b) rewire the callee's internal edges onto the copies.
    for (auto cn : cg->fast_class()) {
      auto it = nmap.find(cn);
      if (it == nmap.end()) {
        continue;
      }
      for (auto e : cn.inp_edges()) {
        auto gdrv = map_driver(e.driver);
        if (gdrv.is_invalid()) {
          continue;
        }
        gdrv.connect_sink(it->second.create_sink_pin(e.sink.get_port_id()));
      }
    }

    // (c) resolve each callee OUTPUT port to its g-driver and the Sub-output's
    // consumer sinks (computed BEFORE deleting the Sub, which still owns them).
    // Walk out_edges instead of probing get_driver_pin per decl: a declared
    // output with no consumer has no materialized pin and hhds asserts.
    absl::flat_hash_map<uint32_t, std::vector<hhds::Pin_class>> out_sinks;
    for (const auto& oe : sub.out_edges()) {
      out_sinks[static_cast<uint32_t>(oe.driver.get_port_id())].push_back(oe.sink);
    }
    std::vector<std::pair<hhds::Pin_class, std::vector<hhds::Pin_class>>> reconnect;
    for (const auto& od : sio->get_output_pin_decls()) {
      auto sit = out_sinks.find(static_cast<uint32_t>(od.port_id));
      if (sit == out_sinks.end()) {
        continue;
      }
      auto internal = driver_of(cg->get_output_pin(od.name));  // driver inside the callee
      if (internal.is_invalid()) {
        continue;
      }
      auto gdrv = map_driver(internal);
      if (gdrv.is_invalid()) {
        continue;
      }
      reconnect.emplace_back(gdrv, std::move(sit->second));
    }

    sub.del_node();  // drops the Sub + all its boundary edges

    // (d) drive the former Sub-output consumers from the inlined logic.
    for (auto& [gdrv, sinks] : reconnect) {
      for (auto& sk : sinks) {
        gdrv.connect_sink(sk);
      }
    }
  }
  }  // fixpoint rounds
  return flattened;
}

// A Sub S sits on a FALSE combinational loop when a backward COMB walk from one
// of its input drivers reaches S itself (stopping at other state/Sub/IO
// boundaries). Returns the S output PORT-IDS the loop threads through (the
// fed-back outputs); empty means S is not on a false loop. The Moore-sub
// deferral only needs non-emptiness; the per-output-cone (Stage-2) deferral
// checks each fed-back port is a pure state read.
// `sub_out_is_state_only(node, pid)` classifies another Sub's OUTPUT reached
// mid-walk: a pure current-state read is a REAL boundary (its value exists
// before any call), but a comb-dependent output is an atomic pass-through --
// the value needs the call, the call needs all its inputs, so the walk
// continues through the callee's input drivers. Traversing pass-through Subs
// is what lets a MULTI-INSTANCE false loop (e.g. if_id -> hazard -> if_id,
// the dino dual-issue shape) reach `s` at all; the old all-Subs-opaque walk
// only caught direct self-feedback through parent comb logic.
template <typename F>
absl::flat_hash_set<uint32_t> sub_false_loop_output_pids(const hhds::Node_class& s, F&& sub_out_is_state_only) {
  namespace gu = livehd::graph_util;
  absl::flat_hash_set<uint32_t>         pids;
  absl::flat_hash_set<hhds::Node_class> seen;
  std::vector<hhds::Pin_class>          stk;
  for (auto e : s.inp_edges()) {
    stk.push_back(e.driver);
  }
  while (!stk.empty()) {
    auto d = stk.back();
    stk.pop_back();
    if (d.is_invalid() || gu::is_const_pin(d)) {
      continue;
    }
    auto m = d.get_master_node();
    if (m == s) {
      pids.insert(static_cast<uint32_t>(d.get_port_id()));
      continue;  // stop AT s; keep walking the rest for every fed-back port
    }
    auto op = gu::type_op_of(m);
    if (op == Ntype_op::Memory || gu::is_type_register(m) || op == Ntype_op::IO) {
      continue;  // a real state boundary -- the loop does not thread through it
    }
    if (op == Ntype_op::Sub) {
      if (sub_out_is_state_only(m, static_cast<uint32_t>(d.get_port_id()))) {
        continue;  // Moore/state-read output: available pre-call, boundary
      }
      if (!seen.insert(m).second) {
        continue;
      }
      for (auto e : m.inp_edges()) {  // comb pass-through: the call's inputs
        stk.push_back(e.driver);
      }
      continue;
    }
    if (!seen.insert(m).second) {
      continue;
    }
    for (auto e : m.inp_edges()) {
      stk.push_back(e.driver);
    }
  }
  return pids;
}

// TRUE when NO output of the callee depends COMBINATIONALLY on any of its
// inputs -- a Moore machine: every output is a pure function of state/consts
// (the DivUnit/SRT16DividerDataModule handshake shape, where `ready` is a
// register read and the fed-back `kill` only affects NEXT state). Conservative:
// anything unbounded (a nested Sub on an output cone, a reached input) -> false.
template <typename SIO>
bool callee_is_moore(const std::shared_ptr<hhds::Graph>& cg, const SIO& sio) {
  namespace gu = livehd::graph_util;
  if (!cg || !sio) {
    return false;
  }
  auto driver_of = [](const hhds::Pin_class& sink) -> hhds::Pin_class {
    if (sink.is_invalid()) {
      return {};
    }
    for (auto e : sink.get_master_node().inp_edges()) {
      if (e.sink.get_port_id() == sink.get_port_id()) {
        return e.driver;
      }
    }
    return {};
  };
  absl::flat_hash_set<hhds::Node_class> seen;
  std::vector<hhds::Pin_class>          stk;
  for (const auto& od : sio->get_output_pin_decls()) {
    auto drv = driver_of(cg->get_output_pin(od.name));
    if (!drv.is_invalid()) {
      stk.push_back(drv);
    }
  }
  while (!stk.empty()) {
    auto d = stk.back();
    stk.pop_back();
    if (d.is_invalid() || gu::is_const_pin(d)) {
      continue;
    }
    if (gu::is_graph_input_pin(d)) {
      return false;  // a combinational in->out path (Mealy)
    }
    auto m  = d.get_master_node();
    auto op = gu::type_op_of(m);
    if (op == Ntype_op::Memory || gu::is_type_register(m)) {
      continue;  // state boundary: reads of current state are input-independent
    }
    if (op == Ntype_op::Sub || op == Ntype_op::IO) {
      return false;  // conservative: a nested sub may be comb-through
    }
    if (!seen.insert(m).second) {
      continue;
    }
    for (auto e : m.inp_edges()) {
      stk.push_back(e.driver);
    }
  }
  return true;
}

// The PER-OUTPUT slice of the Moore check: port-ids of the callee outputs whose
// cone has NO combinational dependence on any callee input (pure functions of
// state/consts). A MEALY callee (rejected whole by callee_is_moore) still has
// state-only outputs -- the CSR/NewCSR::MipModule / DivUnit-SRT16 shape, where
// the fed-back `ready` is a register read but a sibling `echo` output is a comb
// in->out path. Conservative per output: anything unbounded (a nested Sub on
// the cone, a reached IO) disqualifies that output only; an undriven output is
// never included.
template <typename SIO>
absl::flat_hash_set<uint32_t> callee_state_only_outputs(const std::shared_ptr<hhds::Graph>& cg, const SIO& sio) {
  namespace gu = livehd::graph_util;
  absl::flat_hash_set<uint32_t> res;
  if (!cg || !sio) {
    return res;
  }
  auto driver_of = [](const hhds::Pin_class& sink) -> hhds::Pin_class {
    if (sink.is_invalid()) {
      return {};
    }
    for (auto e : sink.get_master_node().inp_edges()) {
      if (e.sink.get_port_id() == sink.get_port_id()) {
        return e.driver;
      }
    }
    return {};
  };
  for (const auto& od : sio->get_output_pin_decls()) {
    auto drv = driver_of(cg->get_output_pin(od.name));
    if (drv.is_invalid()) {
      continue;
    }
    bool                                  state_only = true;
    absl::flat_hash_set<hhds::Node_class> seen;
    std::vector<hhds::Pin_class>          stk{drv};
    while (!stk.empty() && state_only) {
      auto d = stk.back();
      stk.pop_back();
      if (d.is_invalid() || gu::is_const_pin(d)) {
        continue;
      }
      if (gu::is_graph_input_pin(d)) {
        state_only = false;  // a combinational in->out path
        break;
      }
      auto m  = d.get_master_node();
      auto op = gu::type_op_of(m);
      if (op == Ntype_op::Memory || gu::is_type_register(m)) {
        continue;  // state boundary: reads of current state are input-independent
      }
      if (op == Ntype_op::Sub || op == Ntype_op::IO) {
        state_only = false;  // conservative: a nested sub may be comb-through
        break;
      }
      if (!seen.insert(m).second) {
        continue;
      }
      for (auto e : m.inp_edges()) {
        stk.push_back(e.driver);
      }
    }
    if (state_only) {
      res.insert(static_cast<uint32_t>(od.port_id));
    }
  }
  return res;
}

}  // namespace

// Name (raw port name) of the INPUT port that clocks module `g`: the port
// feeding a flop's clock_pin, else -- recursively -- the port wired straight
// through to a sub-instance's own clock port (a flopless wrapper still has a
// clock). Empty when no input-driven clock exists (purely combinational, or
// an internally derived clock). Memoized per Cgen_sim (one graph emission);
// the in-progress "" entry terminates a recursive instantiation.
std::string Cgen_sim::clock_input_of(hhds::Graph* g) {
  const auto key = std::string{g->get_name()};
  if (auto it = clk_memo_.find(key); it != clk_memo_.end()) {
    return it->second;
  }
  clk_memo_.emplace(key, "");
  auto gio = g->get_io();
  if (!gio) {
    return "";
  }
  // See through the identity wrappers tolg puts on a typed port read -- a unary
  // Get_mask (width adjust) or Get_mask(v, -1) (to-positive) -- so `clock:u1`
  // wired straight into a sub's clock port still resolves to the input pin.
  auto resolve_passthrough = [](hhds::Pin_class p) {
    for (int hops = 0; hops < 8 && !p.is_invalid(); ++hops) {
      auto n = p.get_master_node();
      if (type_op_of(n) != Ntype_op::Get_mask) {
        break;
      }
      auto e = sorted_inp(n);  // e[0]=value, e[1]=optional mask (node_expr's convention)
      if (e.empty()) {
        break;
      }
      if (e.size() >= 2) {  // binary form: identity only for the mask==-1 idiom
        if (!is_const_pin(e[1].driver)) {
          break;
        }
        auto mv = hydrate_const(e[1].driver);
        if (!mv.is_just_i64() || mv.to_just_i64() != -1) {
          break;
        }
      }
      p = e[0].driver;
    }
    return p;
  };
  std::string found;
  bool        has_flops = false;
  for (auto node : g->fast_class()) {
    if (!livehd::graph_util::is_type_flop(node)) {
      continue;
    }
    has_flops = true;
    auto d    = resolve_passthrough(get_driver(find_sink_pin(node, "clock_pin")));
    if (d.is_invalid()) {
      continue;
    }
    if (livehd::graph_util::is_graph_input_pin(d)) {
      found = std::string{pin_name_of(d)};
      break;
    }
  }
  // name fallback, mirroring the top-level one: flops whose clock_pin is left
  // implicit still make an input literally named `clock` THE clock port (so a
  // parent wiring its own clock into it resolves through this module too).
  if (found.empty() && has_flops) {
    for (const auto& d : gio->get_input_pin_decls()) {
      if (d.name == "clock") {
        found = "clock";
        break;
      }
    }
  }
  if (found.empty()) {
    // Collect EVERY parent input wired into some sub-instance's clock port, and
    // accept only an unambiguous result: a candidate literally named "clock"
    // wins, else a single distinct candidate. A gated-clock idiom (two nets
    // clocking different subs) stays undetected -- picking whichever instance
    // the traversal visits first would mislabel a poked DATA input as the
    // free-running clock waveform and silently drop its real trace.
    absl::flat_hash_set<std::string> candidates;
    for (auto node : g->fast_class()) {
      if (!livehd::graph_util::is_type_sub(node)) {
        continue;
      }
      auto sio = node.get_subnode_io();
      auto sg  = node.get_subnode_graph();
      if (!sio || !sg) {
        continue;
      }
      const auto callee_clk = clock_input_of(sg.get());
      if (callee_clk.empty()) {
        continue;
      }
      uint32_t clk_pid  = 0;
      bool     have_pid = false;
      for (const auto& d : sio->get_input_pin_decls()) {
        if (d.name == callee_clk) {
          clk_pid  = static_cast<uint32_t>(d.port_id);
          have_pid = true;
          break;
        }
      }
      if (!have_pid) {
        continue;
      }
      for (auto e : node.inp_edges()) {
        if (static_cast<uint32_t>(e.sink.get_port_id()) != clk_pid) {
          continue;
        }
        auto d = resolve_passthrough(e.driver);
        if (!d.is_invalid() && livehd::graph_util::is_graph_input_pin(d)) {
          candidates.insert(std::string{pin_name_of(d)});
        }
        break;
      }
    }
    if (candidates.contains("clock")) {
      found = "clock";
    } else if (candidates.size() == 1) {
      found = *candidates.begin();
    }
  }
  clk_memo_[key] = found;
  return found;
}

// ---- incremental generation digests ----------------------------------------
// The digest covers what the generated C++ depends on for ONE module: IO
// decls, node ops/names/constants, edge topology (via traversal-order node
// indices — stable now that graph construction is deterministic), per-pin
// width/sign, plus the generation-affecting options and a generator version
// (BUMP kSimGenVersion whenever the emitted C++ shape changes).
static constexpr std::string_view kSimGenVersion = "simgen-2";  // 2: liveness-gated comb emission

static inline uint64_t fnv1a(uint64_t h, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    h ^= (v >> (i * 8)) & 0xffu;
    h *= 0x100000001b3ULL;
  }
  return h;
}
static inline uint64_t fnv1a_str(uint64_t h, std::string_view s) {
  for (unsigned char c : s) {
    h ^= c;
    h *= 0x100000001b3ULL;
  }
  return fnv1a(h, s.size());
}

uint64_t Cgen_sim::sim_graph_digest(hhds::Graph* g) {
  namespace gu = livehd::graph_util;
  uint64_t h = 0xcbf29ce484222325ULL;
  absl::flat_hash_map<hhds::Class_index, uint32_t> seq;
  uint32_t                                         ni = 0;
  for (auto n : g->fast_class()) {
    seq[n.get_class_index()] = ni++;
  }
  auto gio = g->get_io();
  for (const auto& d : gio->get_input_pin_decls()) {
    h = fnv1a_str(h, d.name);
    h = fnv1a(h, static_cast<uint64_t>(d.port_id));
    h = fnv1a(h, static_cast<uint64_t>(d.bits) * 2 + 1);
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    h = fnv1a_str(h, d.name);
    h = fnv1a(h, static_cast<uint64_t>(d.port_id));
    h = fnv1a(h, static_cast<uint64_t>(d.bits) * 2);
  }
  for (auto n : g->fast_class()) {
    auto op = gu::type_op_of(n);
    h       = fnv1a(h, static_cast<uint64_t>(op));
    if (gu::has_name(n)) {
      h = fnv1a_str(h, gu::node_name_of(n));
    }
    if (op == Ntype_op::Sub) {
      auto cg = n.get_subnode_graph();
      h       = fnv1a_str(h, cg ? cg->get_name() : std::string_view{});
    }
    if (op == Ntype_op::Nconst) {
      h = fnv1a_str(h, hydrate_const(n.get_driver_pin(0)).to_pyrope());
    }
    for (auto e : n.inp_edges()) {
      h = fnv1a(h, static_cast<uint64_t>(e.sink.get_port_id()));
      h = fnv1a(h, seq[e.driver.get_master_node().get_class_index()]);
      h = fnv1a(h, static_cast<uint64_t>(e.driver.get_port_id()));
    }
    for (auto e : n.out_edges()) {  // lazy view: iterate only, never snapshot
      h = fnv1a(h, static_cast<uint64_t>(e.driver.get_port_id()));
      h = fnv1a(h, static_cast<uint64_t>(wbits_of(e.driver)) * 2 + (is_unsign(e.driver) ? 1 : 0));
    }
  }
  return h;
}

void Cgen_sim::load_gen_digests() {
  gen_digests_loaded_ = true;
  std::ifstream ifs(absl::StrCat(std::string(odir), "/gen_digests.json"));
  if (!ifs) {
    return;
  }
  std::stringstream ss;
  ss << ifs.rdbuf();
  const std::string t = ss.str();
  if (t.find(absl::StrCat("\"gen\":\"", kSimGenVersion, "\"")) == std::string::npos) {
    return;  // other generator version -> cold
  }
  // {"gen":"simgen-1","modules":{"file.entity":"0123456789abcdef",...}}
  size_t p = t.find("\"modules\"");
  if (p == std::string::npos) {
    return;
  }
  p = t.find('{', p);
  if (p == std::string::npos) {
    return;
  }
  ++p;
  while (true) {
    size_t k0 = t.find('"', p);
    if (k0 == std::string::npos) {
      break;
    }
    size_t k1 = t.find('"', k0 + 1);
    size_t v0 = k1 == std::string::npos ? std::string::npos : t.find('"', k1 + 1);
    size_t v1 = v0 == std::string::npos ? std::string::npos : t.find('"', v0 + 1);
    if (v1 == std::string::npos) {
      break;
    }
    gen_digests_[t.substr(k0 + 1, k1 - k0 - 1)] = t.substr(v0 + 1, v1 - v0 - 1);
    p                                           = v1 + 1;
  }
}

void Cgen_sim::save_gen_digests() {
  std::vector<std::string> keys;
  keys.reserve(gen_digests_.size());
  for (const auto& [k, _] : gen_digests_) {
    keys.push_back(k);
  }
  std::sort(keys.begin(), keys.end());  // stable file bytes
  std::ofstream ofs(absl::StrCat(std::string(odir), "/gen_digests.json"));
  ofs << "{\"gen\":\"" << kSimGenVersion << "\",\"modules\":{";
  bool first = true;
  for (const auto& k : keys) {
    ofs << (first ? "" : ",") << "\"" << k << "\":\"" << gen_digests_.at(k) << "\"";
    first = false;
  }
  ofs << "}}\n";
}

// ICG FOLD (todo/livehd/2f-latch M5). A clock-gating cell's output is
// `<clock> & <enable>`: it has a rising edge exactly on the reference clock's
// rising edges where the enable is high. Since one sim tick IS one reference
// period, that folds to "commit this tick iff every non-clock operand of the
// gate is true" — no edge detection, no clock net in the scheduler. This is the
// same abstraction LEC uses (CIRCT's arc.state has a native `enable` operand
// for exactly this); nobody proves ICGs in industry practice, they normalize
// them away.
//
// Returns the guard operand pins, or EMPTY when the cone is not a foldable ICG —
// in which case the caller must REFUSE rather than silently commit every tick.
// Empty is therefore "cannot fold", never "no guard needed"; a plain
// (ungated) clock never reaches here.
std::vector<hhds::Pin_class> Cgen_sim::icg_guards(const hhds::Node_class& flop, std::string_view clock_port) {
  std::vector<hhds::Pin_class> guards;
  auto                         clk_d = get_driver(find_sink_pin(flop, "clock_pin"));
  if (clk_d.is_invalid()) {
    return {};
  }
  auto n = clk_d.get_master_node();
  if (type_op_of(n) != Ntype_op::And) {
    return {};  // only the AND shape folds; anything else is refused
  }
  bool saw_clock = false;
  for (const auto& e : n.inp_edges()) {
    // See through tolg's identity port wrappers, exactly as the clock scan does.
    auto p = e.driver;
    for (int hops = 0; hops < 8 && !p.is_invalid(); ++hops) {
      auto pn = p.get_master_node();
      if (type_op_of(pn) != Ntype_op::Get_mask) {
        break;
      }
      auto ge = sorted_inp(pn);
      if (ge.empty()) {
        break;
      }
      if (ge.size() >= 2) {
        if (!is_const_pin(ge[1].driver)) {
          break;
        }
        auto mv = hydrate_const(ge[1].driver);
        if (!mv.is_just_i64() || mv.to_just_i64() != -1) {
          break;
        }
      }
      p = ge[0].driver;
    }
    if (!p.is_invalid() && livehd::graph_util::is_graph_input_pin(p)) {
      const auto pn = pin_name_of(p);
      // The reference clock, consumed by the tick itself. When the module has a
      // RESOLVED clock port, require an exact match. When it does not — which is
      // the normal case for an ICG design, because clock_input_of() only finds a
      // clock by looking for a flop wired STRAIGHT to an input, and here the only
      // flop is wired to the gate — fall back to the conventional names, the same
      // fallback clock_input_of() itself uses. A miss is fail-CLOSED (the caller
      // refuses with a diagnostic), never a silent commit-every-tick.
      if (!clock_port.empty() ? (pn == clock_port) : (pn == "clk" || pn == "clock")) {
        saw_clock = true;
        continue;
      }
    }
    guards.push_back(e.driver);  // an enable term: becomes part of the commit guard
  }
  // Both halves are required: without the reference clock this is not an ICG at
  // all (it is some other derived clock), and without an enable there is
  // nothing to gate on.
  if (!saw_clock || guards.empty()) {
    return {};
  }
  return guards;
}

void Cgen_sim::do_from_graph(const std::shared_ptr<hhds::Graph>& graph) {
  pin2var.clear();
  tmp_cnt           = 0;
  cycle_unresolved_ = false;
  cycle_reported_   = false;
  cycle_first_label_.clear();

  hhds::Graph* g   = graph.get();
  auto         gio = g->get_io();
  const auto   gname = std::string{graph->get_name()};
  const auto   mod   = cpp_id(gname);

  // VCD trace (sim.vcd): only the --top module emits it (so a
  // hierarchy writes one file). top empty -> single-module run, emit anyway.
  const auto entity  = gname.substr(gname.rfind('.') + 1);
  // A `test` block lowers to a compiler-minted (`%`-named) comb — a testbench,
  // not synthesizable hardware. Its asserts are checked by running the `lhd sim`
  // driver (prp_sim), never by emitting a Slop unit (which would pull in the
  // formal-property header it does not need). Skip it; the kernel's sim_into()
  // drops it from the build's source list too so the two stay consistent.
  if (!entity.empty() && entity.front() == '%') {
    return;
  }
  // Break a false combinational loop through an ATOMIC pure-comb sub-instance by
  // inlining the offending instance into this graph before scheduling. A no-op
  // unless a stateless Sub's output feeds back into one of its own inputs.
  flatten_false_loop_subs(g);
  // Break a false WORD-level combinational loop through a packed wire: redirect
  // each constant Get_mask slice-read of an `Or`-of-disjoint-ranges net to the one
  // operand that drives the read range. A no-op unless a genuine word-level cycle
  // exists; a real bit-level loop is never split (still fails loudly below).
  livehd::graph_util::split_packed_selfref_wires(g);
  // `is_top` = the testbench-driven module (vs an instantiated sub-module); it
  // only gates which module BAKES the VCD file path (avoids two modules opening
  // the same baked file). The VCD machinery itself (vars, snapshots, the phased
  // hierarchical dump methods) is emitted for EVERY module when tracing is on:
  // the root instance's writer is shared down the hierarchy by __vcd_hier(), so
  // one VCD carries the whole design tree, not just the top's io.
  // `top` may be the bare entity or the full internal `file.entity` name — the
  // full form is the only spelling that disambiguates two same-entity modules.
  const bool is_top  = top.empty() || entity == top || gname == top;

  // Liveness: only logic something REAL consumes gets emitted — backward BFS
  // from the sinks (IO nodes cover the graph outputs; state elements, Memory
  // and Sub calls gather their input cones at emission).
  live_.clear();
  {
    std::vector<hhds::Node_class> lstk;
    for (auto n : g->fast_class()) {
      auto nop = livehd::graph_util::type_op_of(n);
      if (nop == Ntype_op::Sub || nop == Ntype_op::Memory || nop == Ntype_op::IO
          || livehd::graph_util::is_type_register(n)) {
        if (live_.insert(n.get_class_index()).second) {
          lstk.push_back(n);
        }
      }
    }
    // Graph outputs seed from the same accessor the output emission uses —
    // the IO node's edges are not reliably enumerable via fast_class.
    for (const auto& d : gio->get_output_pin_decls()) {
      auto opin = g->get_output_pin(d.name);
      auto drv  = opin.is_invalid() ? hhds::Pin_class{} : get_driver(opin);
      if (!drv.is_invalid() && !livehd::graph_util::is_const_pin(drv)) {
        auto m = drv.get_master_node();
        if (!m.is_invalid() && live_.insert(m.get_class_index()).second) {
          lstk.push_back(m);
        }
      }
    }
    while (!lstk.empty()) {
      auto n = lstk.back();
      lstk.pop_back();
      for (auto e : n.inp_edges()) {
        auto m = e.driver.get_master_node();
        if (!m.is_invalid() && live_.insert(m.get_class_index()).second) {
          lstk.push_back(m);
        }
      }
    }
  }
  const bool vcd_on  = !vcd_file.empty();

  const std::string fstem = sim_file_stem(gname);
  const std::string base  = odir.empty() ? fstem : absl::StrCat(odir, "/", fstem);

  // Incremental generation: matching structural digest + existing outputs ->
  // this module's C++ is already up to date, skip the emission (and the file
  // rewrite) entirely. MUST happen before File_output creation (truncation).
  if (!odir.empty()) {
    if (!gen_digests_loaded_) {
      load_gen_digests();
    }
    uint64_t gd = sim_graph_digest(g);
    gd          = fnv1a_str(gd, kSimGenVersion);
    gd          = fnv1a_str(gd, vcd_file);
    gd          = fnv1a_str(gd, top);
    gd          = fnv1a(gd, (is_top ? 2u : 0u) | (vcd_fakedelay ? 1u : 0u));
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(gd));
    auto            it = gen_digests_.find(gname);
    std::error_code ec;
    if (it != gen_digests_.end() && it->second == hex && std::filesystem::exists(base + ".hpp", ec)
        && std::filesystem::exists(base + ".cpp", ec)) {
      return;
    }
    gen_digests_[gname] = hex;  // persisted below, after a clean emission
  }
  auto              hout = std::make_shared<File_output>(absl::StrCat(base, ".hpp"));  // interface
  auto              fout = std::make_shared<File_output>(absl::StrCat(base, ".cpp"));  // definitions ("the slop")

  // Header (<name>.hpp): data members + In/Out + method DECLARATIONS only. A
  // module that instantiates this one #includes this small header (by-value
  // member), so it recompiles when this interface changes, not when the body
  // (in the .cpp) does. The cycle()/reset_cycle() bodies live in the .cpp and
  // are compiled exactly once.
  hout->append("// Generated by inou.cgen.sim (LiveHD, TODO 3d). Do not edit.\n");
  hout->append("#pragma once\n#include <array>\n#include <cstdint>\n#include <map>\n#include <string>\n#include <vector>\n"
               "#include \"slop.hpp\"\n");
  // Forward-declare the signal record (full definition in checkpoint.hpp, included
  // by the .cpp); the header only needs it for the describe_signals() signature.
  hout->append("namespace hlop::ckpt { struct Signal; }\n");
  if (vcd_on) {
    hout->append("#include <memory>\n#include \"vcd_writer.hpp\"\n");
  }
  hout->append("\n");

  // Source (<name>.cpp): includes its own header (which transitively pulls the
  // child interface headers) and holds every method body.
  fout->append("// Generated by inou.cgen.sim (LiveHD, TODO 3d). Do not edit.\n");
  fout->append(absl::StrCat("#include \"", fstem, ".hpp\"\n"));
  fout->append("#include \"checkpoint.hpp\"  // name-keyed dump_state/load_state helpers\n\n");

  // ---- IO decls (sorted by port_id) ----
  struct Io {
    std::string field;
    std::string raw;
    int         bits;
    bool        is_input;
    uint32_t    port_id;
  };
  std::vector<Io> ios;
  if (gio) {
    for (const auto& d : gio->get_input_pin_decls()) {
      ios.push_back({cpp_id(d.name), std::string{d.name}, 0, true, static_cast<uint32_t>(d.port_id)});
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      ios.push_back({cpp_id(d.name), std::string{d.name}, 0, false, static_cast<uint32_t>(d.port_id)});
    }
  }
  for (auto& io : ios) {
    auto pin = io.is_input ? g->get_input_pin(io.raw) : g->get_output_pin(io.raw);
    io.bits  = pin.is_invalid() ? 1 : bits_of(pin, *gio, io.raw);
    if (io.bits <= 0) {
      io.bits = 1;
    }
  }
  std::sort(ios.begin(), ios.end(), [](const Io& a, const Io& b) { return a.port_id < b.port_id; });

  // ---- fail closed on a clock this scheduler cannot honor (2f-latch M0) ----
  // One step() == one full clock period and EVERY flop commits in ONE unified
  // loop, regardless of its `clock_pin`. Two shapes are therefore simulated
  // wrong today while reporting success:
  //   * a GATED / derived clock — the gate is dead code, so the flop loads every
  //     tick even when the gate says hold (lifted by M5, which honors clock_pin);
  //   * TWO OR MORE distinct clock nets — every clock is advanced as if it were
  //     the one clock, and clock_input_of() keeps only the first flop's net for
  //     the VCD while the rest degrade to poked data (lifted by M6).
  // Both used to exit 0 with a plausible-looking VCD, which is the worst
  // possible outcome: a silently wrong waveform reads as a passing test.
  {
    // The module's reference clock port: one tick IS one period of it, so it is
    // the net an ICG fold consumes (see icg_guards).
    const std::string clock_port = clock_input_of(g);
    // See through tolg's identity port wrappers (unary Get_mask width-adjust,
    // and the Get_mask(v,-1) to-positive idiom), same as clock_input_of().
    auto resolve_passthrough = [](hhds::Pin_class p) {
      for (int hops = 0; hops < 8 && !p.is_invalid(); ++hops) {
        auto n = p.get_master_node();
        if (type_op_of(n) != Ntype_op::Get_mask) {
          break;
        }
        auto e = sorted_inp(n);
        if (e.empty()) {
          break;
        }
        if (e.size() >= 2) {
          if (!is_const_pin(e[1].driver)) {
            break;
          }
          auto mv = hydrate_const(e[1].driver);
          if (!mv.is_just_i64() || mv.to_just_i64() != -1) {
            break;
          }
        }
        p = e[0].driver;
      }
      return p;
    };
    absl::flat_hash_set<std::string> clock_nets;  // distinct clock nets driving state
    for (auto node : g->fast_class()) {
      if (!is_type_flop(node)) {
        continue;
      }
      auto d = resolve_passthrough(get_driver(find_sink_pin(node, "clock_pin")));
      if (d.is_invalid()) {
        clock_nets.insert("\x01implicit");  // no clock_pin => the module's implicit clock
        continue;
      }
      if (livehd::graph_util::is_graph_input_pin(d)) {
        clock_nets.insert(std::string{pin_name_of(d)});
        continue;
      }
      // A clock_pin driven by LOGIC. The ICG shape `clk & en` is FOLDED into a
      // commit guard (2f-latch M5) — see icg_guards() — so only a derived clock
      // we cannot fold is refused. Refusing matters: sim would otherwise commit
      // the flop every tick with the gate as dead code, which is a silent
      // miscompile, not a slowdown.
      if (!icg_guards(node, clock_port).empty()) {
        clock_nets.insert("\x01implicit");  // folded: commits on the reference clock, qualified
        continue;
      }
      livehd::diag::err("inou.cgen.sim", "gated-clock-unsupported", "unsupported")
          .msg("module `{}`: flop `{}` has a derived clock inou.cgen.sim cannot fold into a commit guard", gname,
               debug_name(node))
          .hint(
              "only the ICG shape `<clock> & <enable>` is folded (commit when the enable is high at the reference "
              "edge); any other derived clock would be simulated as if it ticked every step, with the gate as dead "
              "code — a silent miscompile. Model the gate as the flop's `enable` instead, or simulate the emitted "
              "Verilog with an event-driven simulator")
          .emit();
      return;
    }
    if (clock_nets.size() >= 2) {
      std::vector<std::string> names(clock_nets.begin(), clock_nets.end());
      std::sort(names.begin(), names.end());  // hash-set order would make the message run-varying
      std::string net_list;
      for (const auto& n : names) {
        absl::StrAppend(&net_list, net_list.empty() ? "" : ", ", n == "\x01implicit" ? "<implicit clock>" : n);
      }
      livehd::diag::err("inou.cgen.sim", "multi-clock-unsupported", "unsupported")
          .msg("module `{}` has state on {} distinct clock nets ({}); inou.cgen.sim is single-clock", gname,
               names.size(), net_list)
          .hint(
              "one step() advances ALL state as if it shared one clock, and only the first net reaches the VCD (the "
              "others degrade to poked data), so a multi-clock design is silently mis-simulated; tracked as "
              "todo/livehd/2f-latch M6")
          .emit();
      return;
    }
  }

  // ---- flops (Flop cells; Latch/Memory -> later phase) ----
  struct Flop {
    hhds::Node_class         node;
    std::string              member;
    int                      bits;
    int                      depth;          // pipe_min shift-register depth (>=1)
    std::vector<std::string> stages;         // depth-1 intermediate stage members (q is the last)
    bool                     posedge = true; // false = negedge flop (posclk known-false)
    // ICG fold (2f-latch M5): non-clock operands of a `<clock> & <enable>` clock
    // cone. Non-empty => this flop commits only in ticks where every guard is
    // true. Empty => an ungated clock, i.e. commit every tick.
    std::vector<hhds::Pin_class> clock_guards;
  };
  std::vector<Flop> flops;
  for (auto node : g->fast_class()) {
    // A LATCH rides the flop path (2f-latch M5). Under the no-time-borrowing
    // scope ruling a latch is a flop-with-enable that commits at its window's
    // closing edge, and for END-OF-TICK observation — the shared observation
    // model for sim / BMC / Icarus — that collapses to exactly the flop update
    // this emitter already performs. tolg has already baked the hold mux into
    // din (`din = en ? d : q`) and wired `enable = en`, so
    //     q_next = en ? din : q = en ? d : q
    // which IS transparency sampled at the end of the tick. A master/slave pair
    // needs no extra machinery either: the two latches sit on opposite phases,
    // so only one of them updates in any given tick and the simultaneous
    // commit-from-pre-tick-state below reproduces the one-cycle latency
    // (verified against the iverilog-validated schedule in
    // tests/sim/latch_sim_master_slave.prp).
    //
    // The Latch cell shares Flop's pin ids (M2), so every named lookup below —
    // din, enable, and the absent reset/initial/pipe_min — resolves unchanged.
    if (!is_type_flop(node) && type_op_of(node) != Ntype_op::Latch) {
      continue;
    }
    auto qpin  = node.get_driver_pin(0);
    int  depth = 1;
    if (auto pm = get_driver(find_sink_pin(node, "pipe_min")); !pm.is_invalid() && is_const_pin(pm)) {
      depth = std::max<int>(1, static_cast<int>(hydrate_const(pm).to_just_i64()));
    }
    Flop f{node, cpp_id(wire_name(qpin)), wbits_of(qpin), depth, {}, true, {}};
    f.clock_guards = icg_guards(node, clock_input_of(g));
    // posedge (default) vs negedge clock: the comptime `posclk` pin, known-false
    // means negedge -- only matters for which sub-tick slot dumps its VCD data.
    if (auto pc = get_driver(find_sink_pin(node, "posclk")); !pc.is_invalid() && is_const_pin(pc)) {
      f.posedge = !hydrate_const(pc).is_known_false();
    }
    for (int i = 0; i < depth - 1; ++i) {
      f.stages.push_back(absl::StrCat(f.member, "_p", i));
    }
    flops.push_back(std::move(f));
  }

  // Identify the top-level clock INPUT port -- the net feeding the flops'
  // clock_pin. In a cycle-based sim it is otherwise just an input forced to 0
  // (which is why the clock never toggled), so we trace it as a dedicated clock
  // waveform driven by `step` rather than as an ordinary io signal. Reset is NOT
  // special: it is an ordinary input the testbench pokes (`acc.reset = ...`) and
  // is traced like any other input.
  std::string clk_field;
  {
    // clock_input_of covers both this module's own flops AND the pass-through
    // case (a flopless wrapper whose `clock` input is wired straight into its
    // sub-instances' clock ports -- e.g. the lecfail dut pair): without the
    // recursive walk that input traced as a flat ordinary signal and the
    // synthetic waveform label collided into `clock_vcd0`.
    const auto raw_clk = clock_input_of(g);
    if (!raw_clk.empty()) {
      for (const auto& io : ios) {
        if (io.is_input && io.raw == raw_clk) {
          clk_field = io.field;
          break;
        }
      }
    }
    // name fallback for the common `clock` port -- UNCONDITIONAL (user ruling
    // 2026-07-18): an input named `clock` is always THE clock waveform, never
    // an ordinary traced signal. Generated RTL (CIRCT) stamps a clock port on
    // every module including pure-comb ones and modules whose only state is a
    // Memory array (no flops); tracing those as data made the synthetic clock
    // label uniquify into `clock_vcd0` and desynced registration from
    // __vcd_clk (the hier-VCD 'clock_vcd0 do not registered' abort).
    if (clk_field.empty()) {
      for (const auto& io : ios) {
        if (io.is_input && io.field == "clock") {
          clk_field = "clock";
          break;
        }
      }
    }
  }
  // The VCD clock waveform label defaults to the real port name; a `tick
  // clocks=(name=ratio)` clause can override the label at run time.
  const std::string clk_label = clk_field.empty() ? "clock" : clk_field;

  // ---- memories (Ntype_op::Memory -> std::array<Slop<bits>,size> member) ----
  struct MemPort {
    bool            rd = false;
    hhds::Pin_class addr, enable, din;
    int             dout_pid = -1;  // read port: driver pin id = n_wr + rd_index
    int             rdidx    = -1;
    int             wridx    = -1;  // write port index (for the FWD bit)
  };
  struct Mem {
    hhds::Node_class     node;
    std::string          member;
    int                  bits = 1, size = 0, type = 2, fwd = 0;
    std::vector<MemPort> ports;  // real ports, in port order (phantoms dropped)
    // Whole-array support (the `update` bus is driven): one update/read_all bus
    // instead of N per-entry ports. registered when a clock is present.
    hhds::Pin_class update, update_enable, init, reset, clock;
    bool             has_read_all = false;
    bool is_whole() const { return !update.is_invalid(); }
    bool registered() const { return !clock.is_invalid(); }
  };
  std::vector<Mem> mems;
  for (auto node : g->fast_class()) {
    if (type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    Mem                  m;
    m.node = node;
    std::vector<MemPort> pv;  // indexed by port_id (raw_pid/12)
    for (auto e : node.inp_edges()) {
      int  raw = static_cast<int>(e.sink.get_port_id());
      auto pn  = Ntype::get_sink_name(Ntype_op::Memory, raw);
      auto pid = static_cast<size_t>(raw) / Ntype::Memory_port_stride;
      if (pn == "bits") {
        m.bits = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "size") {
        m.size = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "type") {
        m.type = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "fwd") {
        m.fwd = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "update") {
        m.update = e.driver;
      } else if (pn == "update_enable") {  // MUST precede ends_with("enable") below
        m.update_enable = e.driver;
      } else if (pn == "reset") {
        m.reset = e.driver;
      } else if (pn == "init") {
        m.init = e.driver;  // whole-array reset-value bus (runtime); plain mem: comptime, still assumed 0
      } else if (pn == "wensize") {
        // wensize (sub-word write-enable) not modeled yet
      } else {
        if (pv.size() <= pid) {
          pv.resize(pid + 1);
        }
        if (str_tools::ends_with(pn, "clock_pin")) {
          m.clock = e.driver;  // presence marks a registered whole-array (timing only)
        } else if (str_tools::ends_with(pn, "addr")) {
          pv[pid].addr = e.driver;
        } else if (str_tools::ends_with(pn, "enable")) {
          pv[pid].enable = e.driver;
        } else if (str_tools::ends_with(pn, "din")) {
          pv[pid].din = e.driver;
        } else if (str_tools::ends_with(pn, "rdport")) {
          pv[pid].rd = !hydrate_const(e.driver).is_known_false();
        }
      }
    }
    for (const auto& e2 : node.out_edges()) {  // read_all is a DRIVER pin (not in inp_edges)
      if (static_cast<hhds::Port_id>(e2.driver.get_port_id()) == Ntype::Memory_readall_pid) {
        m.has_read_all = true;
        break;
      }
    }
    if (m.bits <= 0) {
      m.bits = 1;
    }
    int n_wr = 0;
    for (auto& p : pv) {
      if (!p.addr.is_invalid() && !p.rd) {
        ++n_wr;
      }
    }
    int rd = 0, wr = 0;
    for (auto& p : pv) {
      if (p.addr.is_invalid() && p.din.is_invalid() && p.enable.is_invalid()) {
        continue;  // phantom slot (shared clock landed here)
      }
      if (p.rd) {
        p.dout_pid = n_wr + rd;
        p.rdidx    = rd++;
      } else {
        p.wridx = wr++;
      }
      m.ports.push_back(p);
    }
    m.member = cpp_id(default_instance_name(node));
    mems.push_back(std::move(m));
  }

  // ---- sub-module instances (Ntype_op::Sub -> nested struct member) ----
  struct Sub {
    hhds::Node_class node;
    std::string      inst;           // member name
    std::string      callee_struct;  // cpp_id of the callee module name
  };
  std::vector<Sub>         subs;
  std::vector<std::string> sub_includes;  // distinct callee headers
  for (auto node : g->fast_class()) {
    if (!is_type_sub(node)) {
      continue;
    }
    auto sio = node.get_subnode_io();
    if (!sio) {
      continue;
    }
    std::string cname{sio->get_name()};
    if (cname.empty() || cname == livehd::graph_util::lgassert_module_name
        || cname == livehd::graph_util::fproperty_module_name) {
      // Recognized primitives, not real sub-graphs: instantiating one emitted
      // an #include for a module with no body and broke the sim host-compile
      // (any design with an undischarged assert). Skipped like cgen_verilog's
      // special-case; EXECUTING the runtime check in sim is the pending
      // runtime-fallback item (2f-formal / 4b slop emission).
      continue;
    }
    subs.push_back({node, cpp_id(default_instance_name(node)), cpp_id(cname)});
    auto hdr = absl::StrCat(sim_file_stem(cname), ".hpp");
    if (std::find(sub_includes.begin(), sub_includes.end(), hdr) == sub_includes.end()) {
      sub_includes.push_back(hdr);
    }
  }

  for (const auto& h : sub_includes) {
    hout->append(absl::StrCat("#include \"", h, "\"\n"));
  }
  hout->append("struct ", mod, " {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      hout->append("  Slop<", std::to_string(f.bits), "> ", s, "{};  // pipe stage\n");
    }
    hout->append("  Slop<", std::to_string(f.bits), "> ", f.member, "{};  // flop\n");
  }
  for (const auto& m : mems) {
    hout->append(absl::StrCat("  std::array<Slop<", m.bits, ">, ", m.size, "> ", m.member, "{};  // memory\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {  // sync read: registered dout
        hout->append(absl::StrCat("  Slop<", m.bits, "> ", m.member, "_q", p.rdidx, "{};  // sync read reg\n"));
      }
    }
  }
  for (const auto& s : subs) {
    hout->append(absl::StrCat("  ", s.callee_struct, " ", s.inst, ";  // sub instance\n"));
  }

  // ---- In / Out ----
  hout->append("  struct In {\n");
  for (const auto& io : ios) {
    if (io.is_input) {
      hout->append("    Slop<", std::to_string(io.bits), "> ", io.field, "{};\n");
    }
  }
  hout->append("  };\n  struct Out {\n");
  for (const auto& io : ios) {
    if (!io.is_input) {
      hout->append("    Slop<", std::to_string(io.bits), "> ", io.field, "{};\n");
    }
  }
  hout->append("  };\n");

  // Persistent input latch. The testbench pokes inputs (`acc.x = v` -> __in.x)
  // and advances with `step()`. Outputs are read by recomputing from the current
  // committed state (`acc.y` -> peek(__in).y -- correct before the first step
  // too, unlike a cached snapshot); internal registers are read as plain members
  // (`acc.total`). Keeping __in in the instance (not the driver) means a future
  // state copy / checkpoint captures the pokes too.
  hout->append("  In __in{};\n");

  // ---- VCD trace state (compile.sim.vcd): traces In, Out, and flop state of
  // EVERY module -- the root instance (the one the driver hands a __vcd_path)
  // lazily opens the writer on its first cycle() and __vcd_hier() shares it down
  // the sub-instance tree, so one VCD carries the whole hierarchy under nested
  // scopes. The clock is the one signal NOT traced as an ordinary io port -- it
  // gets the `__vv_clk` waveform driven by `step`; reset and every other input
  // trace normally (reset is just a poked input now).
  //
  // Each traced instance SNAPSHOTS its values (__vs*) inside its own cycle(),
  // pre-commit -- a sub's cycle() runs (and commits its flops) mid-way through
  // the parent's comb walk, so by the parent's dump point the sub's live members
  // are already one edge ahead; the snapshot preserves this-period semantics.
  // The root then walks the hierarchy in timestamp-ordered phases (the writer
  // hard-rejects a past timestamp): clock edge -> [X window] -> settled data. ----
  struct VcdSig {
    std::string var, vname, accessor, snap, prev;
    int         bits;
    bool        posedge;   // dumped at the posedge or negedge slot of the period
    bool        is_input;  // root: poked at the edge; sub: an ordinary comb net
  };
  std::vector<VcdSig> vsig;
  if (vcd_on) {
    int  k   = 0;
    auto add = [&](const std::string& nm, int b, const std::string& acc, bool pe = true, bool is_in = false) {
      vsig.push_back({absl::StrCat("__vv", k), (b > 1 ? absl::StrCat(nm, "[", b - 1, ":0]") : nm), acc,
                      absl::StrCat("__vs", k), absl::StrCat("__vp", k), b, pe, is_in});
      ++k;
    };
    for (const auto& io : ios) {
      if (io.is_input && io.field != clk_field) {
        add(io.field, io.bits, absl::StrCat("in.", io.field), true, true);  // clock gets the dedicated waveform
      }
    }
    for (const auto& io : ios) {
      if (!io.is_input) {
        add(io.field, io.bits, absl::StrCat("o.", io.field));
      }
    }
    for (const auto& f : flops) {
      add(f.member, f.bits, f.member, f.posedge);
      for (const auto& s : f.stages) {
        add(s, f.bits, s, f.posedge);
      }
    }
    // shared_ptr (not unique_ptr) so a DUT struct stays COPYABLE: a hierarchical
    // parent's peek() snapshots each sub-instance by value (`auto _pk = sub;`),
    // which a non-copyable member would break. Sub-instances never get a VCD path
    // (only the root instance does), so their __vcd stays null and the shared
    // copy is harmless; the root instance's own peek move-saves __vcd + clears the
    // path so no spurious trace is written during the state-preserving recompute.
    hout->append("  std::shared_ptr<vcd::VCDWriter> __vcd;\n");
    hout->append("  bool __vcd_trace = false;  // registered in a trace (as root or by a parent's __vcd_hier)\n");
    hout->append("  vcd::VarPtr __vv_clk;\n");
    for (const auto& v : vsig) {
      hout->append(absl::StrCat("  vcd::VarPtr ", v.var, ";\n"));
      hout->append(absl::StrCat("  Slop<", v.bits, "> ", v.snap, "{};  // this-period sample (pre-commit)\n"));
      if (vcd_fakedelay) {
        hout->append(absl::StrCat("  Slop<", v.bits, "> ", v.prev, "{};  // last dumped (X-window change detection)\n"));
      }
    }
  }

  // Uniquify the clock waveform LABEL against the traced signal names, so a user
  // signal literally named e.g. "clock" can't duplicate the synthetic clock var
  // at VCD registration (vsig is empty unless tracing, so this is a no-op without
  // VCD).
  auto uniquify_label = [&](const std::string& label) {
    auto taken = [&](const std::string& s) {
      for (const auto& v : vsig) {
        if (v.vname == s) {
          return true;
        }
      }
      return false;
    };
    if (!taken(label)) {
      return label;
    }
    for (int i = 0;; ++i) {
      auto cand = absl::StrCat(label, "_vcd", i);
      if (!taken(cand)) {
        return cand;
      }
    }
  };
  const std::string clk_name_baked = uniquify_label(clk_label);

  // Clock waveform config + the VCD path / period counter. Plain scalars on every
  // module; the driver sets the path + clock ratio/name on each instance.
  {
    // A baked FILE path lands only on the --top module: with the machinery now on
    // every module, baking it everywhere would have each sub-instance lazily open
    // the same file as its own root writer.
    std::string vcd_baked = (vcd_file == "1" || vcd_file == "true" || !is_top) ? std::string{} : vcd_file;
    hout->append(absl::StrCat("  std::string __vcd_path = \"", vcd_baked, "\";\n"));
    hout->append("  unsigned __vcd_tick = 0;       // clock periods elapsed (10 VCD time-units each)\n");
    hout->append(absl::StrCat("  std::string __clk_name = \"", clk_name_baked, "\";\n"));
    hout->append("  unsigned __clk_ratio = 1;      // VCD ticks per clock period\n");
  }

  // ---- method declarations, then close the struct (bodies follow in the .cpp) ----
  if (vcd_on) {
    hout->append("  void __vcd_init();\n");
    hout->append("  void __vcd_hier(vcd::VCDWriter* __w, const std::string& __s);\n");
    hout->append("  void __vcd_clk(vcd::VCDWriter* __w, bool __rise);\n");
    hout->append("  void __vcd_dump_in(vcd::VCDWriter* __w);\n");
    if (vcd_fakedelay) {
      hout->append("  void __vcd_dump_x(vcd::VCDWriter* __w, bool __pos, bool __root);\n");
    }
    hout->append("  void __vcd_dump_data(vcd::VCDWriter* __w, bool __pos, bool __root);\n");
  }
  hout->append("  void reset_cycle();\n");
  hout->append("  Out cycle(In in);\n");
  // one clock edge: cycle() commits next-state (and dumps the pre-edge VCD
  // sample), then peek() recomputes the outputs from the just-committed state so
  // `acc.<out>` reads the POST-edge value. (peek saves/restores flops+mems and
  // suppresses VCD, so it has no net effect besides refreshing __out.)
  hout->append("  void step() { cycle(__in); }  // poke __in, then advance one clock\n");
  hout->append("  Out peek(In in);  // outputs of the current committed state; restores state, no VCD\n");
  // Editable, name-keyed checkpoint (sim_checkpoint_debug_plan): flops/regs ->
  // the `_r` map keyed by the hierarchical `_p`+member name (pyrope literal),
  // memories -> one `<_dir>/<_p><member>.hex` file each, sub-instances recursed.
  // design_hash folds member names+widths (cross-version warn, never reject).
  hout->append("  void dump_state(const std::string& _p, std::map<std::string, std::string>& _r, const std::string& _dir) const;\n");
  hout->append("  void load_state(const std::string& _p, const std::map<std::string, std::string>& _r, const std::string& _dir);\n");
  hout->append("  std::uint64_t design_hash() const;\n");
  // Observability (lhd sim --list-signals / --probe / --break-when): every scalar
  // signal (flop / pipe stage / sync-read reg / input) by hierarchical name.
  hout->append("  void describe_signals(const std::string& _p, std::vector<hlop::ckpt::Signal>& _v) const;\n");
  hout->append("  void probe_signals(const std::string& _p, std::map<std::string, long>& _m) const;\n");
  hout->append("};\n");

  // ---- VCD method definitions (source): __vcd_init (root-only entry) plus the
  // recursive registration/dump walkers every module carries. The root's writer
  // is passed down as a plain pointer -- only the root instance OWNS it (shared
  // __vcd stays null on subs, keeping window-off teardown a root-local reset). ----
  if (vcd_on) {
    // `has_clock`: this module has clocked state (or a pass-through clock port),
    // so its scope shows the clock waveform. A clockless module still gets one
    // when it is the ROOT (the testbench tick is its clock) -- registered in
    // __vcd_init, not __vcd_hier, so a clockless SUB scope stays data-only.
    const bool has_clock = !clk_field.empty() || !flops.empty();
    // The BAKED clock label was uniquified against the traced names at codegen,
    // but a `tick clocks=(name=…)` clause overrides __clk_name at RUN time -- a
    // label matching a traced 1-bit signal would make registration throw. Guard
    // the runtime value against this module's (baked) 1-bit signal names.
    std::string clk_guard;  // C++ bool expr over __cn, "" = no 1-bit names to collide with
    for (const auto& v : vsig) {
      if (v.bits == 1) {
        absl::StrAppend(&clk_guard, clk_guard.empty() ? "" : " || ", "__cn == \"", v.vname, "\"");
      }
    }
    auto emit_clk_reg = [&](const char* writer_expr, const std::string& scope_expr) {
      fout->append(absl::StrCat("  std::string __cn = __clk_name;\n"));
      if (!clk_guard.empty()) {
        fout->append(absl::StrCat("  if (", clk_guard, ") { __cn += \"_vcd0\"; }  // runtime tick-clock label collided\n"));
      }
      fout->append(
          absl::StrCat("  __vv_clk = ", writer_expr, "->register_var(", scope_expr, ", __cn, vcd::VariableType::wire, 1);\n"));
    };
    fout->append("void ", mod, "::__vcd_init() {\n");
    fout->append("  __vcd = std::make_shared<vcd::VCDWriter>(__vcd_path, vcd::makeVCDHeader());\n");
    if (!has_clock) {
      emit_clk_reg("__vcd", absl::StrCat("\"", mod, "\""));
    }
    fout->append(absl::StrCat("  __vcd_hier(__vcd.get(), \"", mod, "\");\n"));
    fout->append("}\n");

    // Register this instance's vars under scope `__s`, then the sub-instances
    // under `__s.<inst>` (nested $scope blocks -- the writer splits on '.').
    fout->append("void ", mod, "::__vcd_hier(vcd::VCDWriter* __w, const std::string& __s) {\n");
    fout->append("  (void)__w; (void)__s;\n");
    fout->append("  __vcd_trace = true;\n");
    if (has_clock) {
      emit_clk_reg("__w", "__s");
    }
    for (const auto& v : vsig) {
      fout->append(
          absl::StrCat("  ", v.var, " = __w->register_var(__s, \"", v.vname, "\", vcd::VariableType::wire, ", v.bits, ");\n"));
    }
    for (const auto& s : subs) {
      // A registered sub is never its own trace root: drop any writer it
      // self-rooted (a baked FILE path without --top) and clear the path so
      // its lazy __vcd_init can't fire again.
      fout->append(absl::StrCat("  ", s.inst, ".__vcd.reset();\n"));
      fout->append(absl::StrCat("  ", s.inst, ".__vcd_path.clear();\n"));
      fout->append(absl::StrCat("  ", s.inst, ".__vcd_hier(__w, __s + \".", s.inst, "\");\n"));
    }
    fout->append("}\n");

    // The clock edge, written into every clocked scope of the subtree.
    fout->append("void ", mod, "::__vcd_clk(vcd::VCDWriter* __w, bool __rise) {\n");
    fout->append("  (void)__w; (void)__rise;\n");
    fout->append("  if (__vv_clk) {\n");
    fout->append("    if (__rise) { __w->change(__vv_clk, \"1\"); } else { __w->change(__vv_clk, \"0\"); }\n");
    fout->append("  }\n");
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  ", s.inst, ".__vcd_clk(__w, __rise);\n"));
    }
    fout->append("}\n");

    // Root-only, at the edge timestamp: the testbench pokes inputs BEFORE the
    // edge, so they change exactly at it (no settle window, no X). Not recursive:
    // a sub's inputs are ordinary comb nets, dumped with the data phase below.
    fout->append("void ", mod, "::__vcd_dump_in(vcd::VCDWriter* __w) {\n");
    fout->append("  (void)__w;\n");
    fout->append("  if (__vcd_trace) {\n");
    for (const auto& v : vsig) {
      if (v.is_input) {
        fout->append(absl::StrCat("    __w->change(", v.var, ", vcd::to_vcd_bits(", v.snap, ", ", v.bits, "));\n"));
      }
    }
    fout->append("  }\n}\n");

    if (vcd_fakedelay) {
      // At the edge timestamp: any signal whose settled value will differ goes X
      // for the settle window ("computing"); an unchanged signal stays clean.
      fout->append("void ", mod, "::__vcd_dump_x(vcd::VCDWriter* __w, bool __pos, bool __root) {\n");
      fout->append("  (void)__w; (void)__pos; (void)__root;\n");
      fout->append("  if (__vcd_trace) {\n");
      fout->append("    if (__pos) {\n");
      for (const auto& v : vsig) {
        if (!v.posedge) {
          continue;
        }
        const char* x = v.bits > 1 ? "bx" : "x";
        if (v.is_input) {
          fout->append(absl::StrCat("      if (!__root && !", v.snap, ".same_repr(", v.prev, ")) __w->change(", v.var,
                                    ", \"", x, "\");\n"));
        } else {
          fout->append(
              absl::StrCat("      if (!", v.snap, ".same_repr(", v.prev, ")) __w->change(", v.var, ", \"", x, "\");\n"));
        }
      }
      fout->append("    } else {\n");
      for (const auto& v : vsig) {
        if (v.posedge) {
          continue;
        }
        const char* x = v.bits > 1 ? "bx" : "x";
        fout->append(
            absl::StrCat("      if (!", v.snap, ".same_repr(", v.prev, ")) __w->change(", v.var, ", \"", x, "\");\n"));
      }
      fout->append("    }\n  }\n");
      for (const auto& s : subs) {
        fout->append(absl::StrCat("  ", s.inst, ".__vcd_dump_x(__w, __pos, false);\n"));
      }
      fout->append("}\n");
    }

    // The settled data of this period's sample (posedge slot, then the negedge
    // slot at the period midpoint). Root inputs were already written at the edge
    // by __vcd_dump_in; as a SUB they are comb nets and dump here instead.
    fout->append("void ", mod, "::__vcd_dump_data(vcd::VCDWriter* __w, bool __pos, bool __root) {\n");
    fout->append("  (void)__w; (void)__pos; (void)__root;\n");
    fout->append("  if (__vcd_trace) {\n");
    fout->append("    if (__pos) {\n");
    for (const auto& v : vsig) {
      if (!v.posedge) {
        continue;
      }
      std::string upd = vcd_fakedelay ? absl::StrCat(" ", v.prev, " = ", v.snap, ";") : std::string{};
      if (v.is_input) {
        fout->append(absl::StrCat("      if (!__root) { __w->change(", v.var, ", vcd::to_vcd_bits(", v.snap, ", ",
                                  v.bits, "));", upd, " }\n"));
      } else {
        fout->append(
            absl::StrCat("      __w->change(", v.var, ", vcd::to_vcd_bits(", v.snap, ", ", v.bits, "));", upd, "\n"));
      }
    }
    fout->append("    } else {\n");
    for (const auto& v : vsig) {
      if (v.posedge) {
        continue;
      }
      std::string upd = vcd_fakedelay ? absl::StrCat(" ", v.prev, " = ", v.snap, ";") : std::string{};
      fout->append(
          absl::StrCat("      __w->change(", v.var, ", vcd::to_vcd_bits(", v.snap, ", ", v.bits, "));", upd, "\n"));
    }
    fout->append("    }\n  }\n");
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  ", s.inst, ".__vcd_dump_data(__w, __pos, false);\n"));
    }
    fout->append("}\n");
  }

  // ---- reset_cycle: each flop (+ its pipe stages) to its reset value (the
  // `initial` pin, normally a constant; default 0). ----
  fout->append("void ", mod, "::reset_cycle() {\n");
  for (const auto& f : flops) {
    auto        init = get_driver(find_sink_pin(f.node, "initial"));
    std::string rv   = init.is_invalid() ? absl::StrCat("Slop<", f.bits, ">::create_integer(0)") : operand(init, f.bits);
    for (const auto& s : f.stages) {
      fout->append("    ", s, " = ", rv, ";\n");
    }
    fout->append("    ", f.member, " = ", rv, ";\n");
  }
  for (const auto& m : mems) {
    // Power-on contents: apply the comptime `init` bus (ROM / `const`/`mut` array
    // reset value) instead of zero-filling, so first-cycle reads see the real
    // contents. A runtime/absent init keeps the zero-fill.
    if (!m.init.is_invalid() && is_const_pin(m.init)) {
      fout->append(absl::StrCat("    slop_apply_update(", m.member, ", ", operand(m.init, m.bits * m.size), ");\n"));
    } else {
      fout->append("    for (auto& __e : ", m.member, ") __e = Slop<", std::to_string(m.bits), ">::create_integer(0);\n");
    }
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("    ", m.member, "_q", p.rdidx, " = Slop<", m.bits, ">::create_integer(0);\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append("    ", s.inst, ".reset_cycle();\n");
  }
  fout->append("}\n");

  // ---- cycle ----
  // Reset is an ordinary input the testbench pokes into `in` (via step()/__in);
  // no special override here. The flop next-state logic reads it through the
  // normal reset_pin path below.
  fout->append(mod, "::Out ", mod, "::cycle(In in) {\n");
  // map input ports and flop q outputs into pin2var
  for (const auto& io : ios) {
    if (!io.is_input) {
      continue;
    }
    auto pin = g->get_input_pin(io.raw);
    if (!pin.is_invalid()) {
      pin2var[pin.get_class_index()] = absl::StrCat("in.", io.field);
    }
  }
  for (const auto& f : flops) {
    auto qpin = f.node.get_driver_pin(0);
    if (!qpin.is_invalid()) {
      pin2var[qpin.get_class_index()] = f.member;
    }
  }

  // ---- Moore-sub deferral: a Sub on a FALSE loop whose callee has NO
  // combinational input->output path needs no ordering between its inputs and
  // outputs -- its outputs are a pure function of the child's CURRENT state.
  // Pre-bind them via peek() (state-preserving output recompute) before the
  // comb walk; the real cycle(in) call, which only advances child state, is
  // DEFERRED until every comb value is bound (emitted after the walk below).
  // This resolves the DivUnit/ExeUnitImp_2 comb-loop-through-instance class
  // without flattening the stateful callee (state identity/checkpoint names
  // are untouched). ----
  absl::flat_hash_set<hhds::Class_index> moore_deferred;
  std::vector<const Sub*>                deferred_moore;
  // Per-OUTPUT-cone deferral (Stage-2): a MEALY callee (some comb in->out path,
  // so callee_is_moore declines the whole-callee treatment) whose FED-BACK
  // outputs are all pure state reads. Pre-binding just those outputs via peek()
  // un-cycles the schedule; the atomic cycle(in) call then orders normally (its
  // input cone reads only pre-bound values, emitted on demand below). The
  // CSR/NewCSR::MipModule and DivUnit/SRT16 residual class.
  absl::flat_hash_set<hhds::Class_index> mealy_prebound;
  // Memoized per callee def: the set of output pids that are pure current-state
  // reads (a Moore callee: ALL of them). Used by the fed-back walk to decide
  // whether another Sub's output is a boundary or an atomic pass-through.
  absl::flat_hash_map<const hhds::Graph*, absl::flat_hash_set<uint32_t>> state_out_memo;
  auto sub_out_is_state_only = [&](const hhds::Node_class& m, uint32_t pid) -> bool {
    auto cg = m.get_subnode_graph();
    if (!cg) {
      return false;
    }
    auto it = state_out_memo.find(cg.get());
    if (it == state_out_memo.end()) {
      auto                          sio = m.get_subnode_io();
      absl::flat_hash_set<uint32_t> so;
      if (sio) {
        if (callee_is_moore(cg, sio)) {
          for (const auto& od : sio->get_output_pin_decls()) {
            so.insert(static_cast<uint32_t>(od.port_id));
          }
        } else {
          so = callee_state_only_outputs(cg, sio);
        }
      }
      it = state_out_memo.emplace(cg.get(), std::move(so)).first;
    }
    return it->second.contains(pid);
  };
  for (const auto& s : subs) {
    auto fed_back = sub_false_loop_output_pids(s.node, sub_out_is_state_only);
    if (fed_back.empty()) {
      continue;
    }
    const bool                    moore = callee_is_moore(s.node.get_subnode_graph(), s.node.get_subnode_io());
    absl::flat_hash_set<uint32_t> state_only;
    if (moore) {
      moore_deferred.insert(s.node.get_class_index());
    } else {
      state_only = callee_state_only_outputs(s.node.get_subnode_graph(), s.node.get_subnode_io());
      bool all_state_only = true;
      for (auto pid : fed_back) {
        if (!state_only.contains(pid)) {
          all_state_only = false;
          break;
        }
      }
      if (!all_state_only) {
        continue;  // genuine comb feedback -> the loud Stage-0 diagnostic below
      }
      mealy_prebound.insert(s.node.get_class_index());
    }
    fout->append(absl::StrCat("    auto ", s.inst, "__pre = ", s.inst,
                              moore ? ".peek({});  // Moore sub: outputs from current state (call deferred)\n"
                                    : ".peek({});  // Mealy sub: state-only outputs pre-bound (call ordered normally)\n"));
    // Bind only pins that EXIST (enumerated via the instance's out-edges): a
    // declared-but-unread output has no created pin, and hhds' name lookup
    // asserts on it (find_pin "requested pin was not created" -- the DataPath
    // family crash). The lazy out_edges view is iterated read-only.
    auto                                    sio = s.node.get_subnode_io();
    absl::flat_hash_map<uint32_t, std::string> pid2name;
    for (const auto& d : sio->get_output_pin_decls()) {
      pid2name[static_cast<uint32_t>(d.port_id)] = d.name;
    }
    for (const auto& e : s.node.out_edges()) {
      auto opin = e.driver;
      if (opin.is_invalid() || pin2var.contains(opin.get_class_index())) {
        continue;
      }
      auto pid = static_cast<uint32_t>(opin.get_port_id());
      if (!moore && !state_only.contains(pid)) {
        continue;  // a comb in->out output binds at the (normally ordered) call
      }
      auto it = pid2name.find(pid);
      if (it != pid2name.end()) {
        pin2var[opin.get_class_index()] = absl::StrCat(s.inst, "__pre.", cpp_id(it->second));
      }
    }
  }

  // On-demand emission of a combinational input cone. A Memory cell is
  // `loop_last` (a toposort source), so forward_class emits it EARLY -- before a
  // COMPUTED read address / write-forward operand (e.g. `i*2+j`) that a normal
  // comb node would produce later in the order. Binding such an operand at the
  // (early) memory position would read it unresolved. This walks the operand's
  // driver cone and emits any not-yet-bound plain comb node first (its own
  // inputs recursively), so `operand()` resolves. Stops at state elements /
  // atomic Subs / multi-out cells / consts (bound elsewhere, or a genuine cycle
  // the detector below still catches). Direct-input / const addresses (the
  // common case) are already bound, so this is a no-op for them.
  // One atomic Sub call emission, shared by the scheduled walk below and the
  // on-demand path inside ensure_ready (a call can be SCHEDULED after its
  // reader once a false instance-level cycle was dissolved by the
  // Moore-deferral / pre-binding above). The guard is load-bearing: a second
  // emission would call child.cycle() twice and double-advance its state.
  absl::flat_hash_set<hhds::Class_index> emitted_subs;
  auto emit_sub_call = [&](auto&& ensure_fn, const hhds::Node_class& node) -> void {
    if (!emitted_subs.insert(node.get_class_index()).second) {
      return;
    }
    for (const auto& s : subs) {
      if (s.node.get_class_index() != node.get_class_index()) {
        continue;
      }
      auto sio = node.get_subnode_io();
      if (!sio) {
        break;
      }
      fout->append(absl::StrCat("    ", s.callee_struct, "::In ", s.inst, "__i;\n"));
      for (const auto& d : sio->get_input_pin_decls()) {
        auto drv = get_driver(find_sink_pin(node, d.name));
        int  wb  = d.bits > 0 ? static_cast<int>(d.bits) : 1;
        // Emit any pending operand cone on demand (conservative: stops at
        // state elements / consts; another atomic Sub recurses through this
        // same helper). A genuinely cyclic cone stays unbound and falls
        // through to the loud Stage-0 diagnostic below.
        ensure_fn(drv);
        // Stage 0: a valid, non-const driver feeding this instance input that is
        // not yet bound is a combinational cycle threading THROUGH this atomic Sub
        // call (the false-loop-through-instance case). Report it precisely.
        if (!drv.is_invalid() && !is_const_pin(drv) && !pin2var.contains(drv.get_class_index()) && !cycle_reported_) {
          livehd::diag::err("inou.cgen.sim", "comb-loop-through-instance", "unsupported")
              .msg(
                  "combinational loop through instance `{}` ({}::{}): input `{}` is fed by logic that depends on "
                  "this instance's own output",
                  s.inst, gname, s.callee_struct, d.name)
              .hint(
                  "a sub-instance is simulated atomically (all inputs -> all outputs), so an output that feeds back "
                  "into one of its inputs forms a cycle the single-pass schedule cannot break; restructure so the "
                  "cone feeding this input is computed before the call, split the sub by output cone, or flatten "
                  "this instance for sim")
              .emit();
          cycle_reported_ = true;
        }
        fout->append(absl::StrCat("    ", s.inst, "__i.", cpp_id(d.name), " = ", operand(drv, wb), ";\n"));
      }
      fout->append(absl::StrCat("    auto ", s.inst, "__o = ", s.inst, ".cycle(", s.inst, "__i);\n"));
      for (const auto& d : sio->get_output_pin_decls()) {
        auto opin = find_driver_pin(node, d.name);
        if (!opin.is_invalid()) {
          pin2var[opin.get_class_index()] = absl::StrCat(s.inst, "__o.", cpp_id(d.name));
        }
      }
      break;
    }
  };

  absl::flat_hash_set<pin_key_t> prefetch_seen;
  auto ensure_ready_impl = [&](auto&& self, const hhds::Pin_class& drv) -> void {
    if (drv.is_invalid() || is_const_pin(drv)) {
      return;
    }
    if (pin2var.contains(drv.get_class_index())) {
      return;
    }
    if (!prefetch_seen.insert(drv.get_class_index()).second) {
      // Already walked this pin (or a combinational-cycle back-edge re-entered
      // it): stop instead of recursing forever -- an unbound cycle member is
      // left for operand() to report as the loud combinational-loop diagnostic
      // (unbounded self-recursion here stack-overflowed on BusyTable_1/Dispatch,
      // any comb cycle reaching a Memory operand; repro sim_loop_mem_prefetch).
      return;
    }
    auto n   = drv.get_master_node();
    auto nop = type_op_of(n);
    if (nop == Ntype_op::Sub) {
      // A deferred-Moore instance's outputs are pre-bound (peek) -- nothing to
      // emit. Any other atomic call is emitted on demand, its own input cones
      // first (prefetch_seen above already broke re-entry on a real cycle).
      if (!moore_deferred.contains(n.get_class_index())) {
        emit_sub_call([&](const hhds::Pin_class& p) { self(self, p); }, n);
      }
      return;
    }
    if (nop == Ntype_op::Memory || is_type_register(n)) {
      return;  // bound at its own emission; if still unbound it is a real cycle
    }
    if (Ntype::has_multiple_driver_pins(nop) || !n.has_out_edges()) {
      return;
    }
    for (auto e : n.inp_edges()) {
      self(self, e.driver);
    }
    auto dp = n.get_driver_pin(0);
    if (pin2var.contains(dp.get_class_index())) {
      return;
    }
    int  wb  = wbits_of(dp);
    auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
    fout->append(absl::StrCat("    Slop<", wb, "> ", var, " = ", node_expr(n, wb), ";  // ", op_name(nop), " (mem-operand prefetch)\n"));
    pin2var[dp.get_class_index()] = var;
  };
  auto ensure_ready = [&](const hhds::Pin_class& drv) { ensure_ready_impl(ensure_ready_impl, drv); };

  // combinational SSA bindings, in dependency order
  for (auto node : g->forward_class()) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Memory) {
      // Emit the read data (read-first: the current array) and register each
      // read port's dout driver pin so downstream nodes resolve. Writes commit
      // at the edge (below). Sync read (type 0) consumers see the dout register.
      for (const auto& m : mems) {
        if (m.node.get_class_index() != node.get_class_index()) {
          continue;
        }
        // Combinational whole-array: the `update` bus IS the contents this cycle
        // (no clock edge), so scatter it into `member` BEFORE the reads so reads
        // and read_all observe the post-update value. (Registered whole-arrays
        // apply update at the edge below; reads here see committed state.)
        if (m.is_whole() && !m.registered()) {
          ensure_ready(m.update);
          fout->append(absl::StrCat("    slop_apply_update(", m.member, ", ", operand(m.update, m.bits * m.size), ");\n"));
        } else if (!m.registered() && !m.init.is_invalid() && is_const_pin(m.init)) {
          // Combinational per-port array (`mut t:[..] = <const>`): re-seed the
          // whole array to its comptime init at the START of every cycle. Writes
          // are forwarded to the same-cycle read below and also committed to
          // `member` at the edge, but a comb array has no state, so without this
          // re-seed a per-port write would leak into later cycles' reads.
          fout->append(absl::StrCat("    slop_apply_update(", m.member, ", ", operand(m.init, m.bits * m.size), ");\n"));
        }
        for (const auto& p : m.ports) {
          if (!p.rd || p.addr.is_invalid()) {
            continue;
          }
          auto dout = node.create_driver_pin(static_cast<hhds::Port_id>(p.dout_pid));
          if (m.type == 1) {
            pin2var[dout.get_class_index()] = absl::StrCat(m.member, "_q", std::to_string(p.rdidx));
          } else {
            // async read of the current array, then same-cycle write forwarding
            // (per-write-port FWD bit), matching the cgen_memory wrapper.
            ensure_ready(p.addr);  // computed read address emitted before this early (loop_last) memory node
            int  ab  = std::max(1, bits_of(p.addr));
            auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
            std::string body
                = absl::StrCat("    Slop<", m.bits, "> ", var, " = [&]{ size_t __r = static_cast<size_t>((", operand(p.addr, ab),
                               ").to_just_i64()); Slop<", m.bits, "> __v = __r < ", m.size, " ? ", m.member,
                               "[__r] : Slop<", m.bits, ">::create_integer(0);");
            // A combinational (non-registered) array has no clock edge: every
            // write is same-cycle, so the read must forward ALL write ports (the
            // per-port FWD bit only governs registered write-forwarding). Paired
            // with the comptime-init re-seed above, this makes a `mut` array read
            // its post-write value directly (not via the peek-committed member).
            const bool forward_all = !m.registered();
            for (const auto& wp : m.ports) {
              if (wp.rd || wp.addr.is_invalid() || (!forward_all && !((m.fwd >> wp.wridx) & 1))) {
                continue;
              }
              std::string we = "true";
              if (!wp.enable.is_invalid()) {
                if (is_const_pin(wp.enable)) {
                  if (hydrate_const(wp.enable).is_known_false()) {
                    continue;
                  }
                } else {
                  ensure_ready(wp.enable);
                  we = absl::StrCat("(", operand(wp.enable, 1), ").is_known_true()");
                }
              }
              ensure_ready(wp.addr);
              ensure_ready(wp.din);
              body += absl::StrCat(" { size_t __w = static_cast<size_t>((", operand(wp.addr, std::max(1, bits_of(wp.addr))),
                                   ").to_just_i64()); if (", we, " && __w == __r) __v = ", operand(wp.din, m.bits), "; }");
            }
            body += " return __v; }();  // mem read\n";
            fout->append(body);
            pin2var[dout.get_class_index()] = var;
          }
        }
        // Async whole-array read: pack the current `member` into one bus.
        if (m.has_read_all) {
          auto ra  = node.create_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
          auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
          fout->append(absl::StrCat("    auto ", var, " = slop_read_all(", m.member, ");  // read_all\n"));
          pin2var[ra.get_class_index()] = var;
        }
        break;
      }
      continue;
    }
    if (op == Ntype_op::Sub) {
      // Instantiate the callee directly: build its In from this Sub's input
      // drivers, call child.cycle(), and register each output driver pin to the
      // returned struct field (no intermediate wires).
      if (moore_deferred.contains(node.get_class_index())) {
        // outputs were pre-bound from peek(); the state-advancing cycle(in)
        // call is emitted after the walk, when its input operands are bound
        for (const auto& s : subs) {
          if (s.node.get_class_index() == node.get_class_index()) {
            deferred_moore.push_back(&s);
            break;
          }
        }
        continue;
      }
      emit_sub_call(ensure_ready, node);
      continue;
    }
    if (Ntype::has_multiple_driver_pins(op)) {
      continue;
    }
    if (!node.has_out_edges() || is_type_register(node)) {
      continue;
    }
    if (!live_.contains(node.get_class_index())) {
      // Nothing real consumes this value: split_packed_selfref_wires
      // redirects packed-wire readers and routinely strands whole Or-trees
      // (ImmediateGenerator: 400 of 935 emitted values were dead). A skipped
      // dead ROOT (no out edges) would otherwise leave its entire operand
      // tree emitted-but-unused (-Wunused-variable noise, wasted compiles).
      continue;
    }
    auto dpin = node.get_driver_pin(0);
    if (pin2var.contains(dpin.get_class_index())) {
      continue;  // already emitted via a memory-operand prefetch
    }
    // forward_class built its order on the RAW graph; once a false
    // instance-level cycle has been dissolved (Moore-deferral / state-only
    // pre-binding above), a comb node can still be SCHEDULED before one of
    // its operands. Emit pending operand cones on demand; a genuinely cyclic
    // operand stays unbound (ensure_ready stops on re-entry) and operand()
    // below reports it as the loud Stage-0 diagnostic.
    for (auto e : node.inp_edges()) {
      ensure_ready(e.driver);
    }
    int  wb   = wbits_of(dpin);
    auto var  = absl::StrCat("cg_", std::to_string(tmp_cnt++));
    fout->append(absl::StrCat("    Slop<", wb, "> ", var, " = ", node_expr(node, wb), ";  // ", op_name(op), "\n"));
    pin2var[dpin.get_class_index()] = var;
  }

  // Deferred Moore-sub calls: every comb value is bound now; the child call
  // only advances the child's state (its outputs were pre-bound via peek()).
  // Inputs are bound from the instance's EXISTING sink edges (an unconnected
  // declared input has no created pin -- hhds asserts on a name lookup -- and
  // the In{} zero-init already models it as 0).
  for (const auto* sp : deferred_moore) {
    const auto& s    = *sp;
    auto        dsio = s.node.get_subnode_io();
    if (!dsio) {
      continue;
    }
    absl::flat_hash_map<uint32_t, const void*> bound_pids;  // pid -> decl (dedupe)
    absl::flat_hash_map<uint32_t, std::pair<std::string, int>> pid2in;
    for (const auto& d : dsio->get_input_pin_decls()) {
      pid2in[static_cast<uint32_t>(d.port_id)] = {d.name, d.bits > 0 ? static_cast<int>(d.bits) : 1};
    }
    fout->append(absl::StrCat("    ", s.callee_struct, "::In ", s.inst, "__i;\n"));
    for (auto e : s.node.inp_edges()) {
      auto pid = static_cast<uint32_t>(e.sink.get_port_id());
      auto it  = pid2in.find(pid);
      if (it == pid2in.end() || !bound_pids.emplace(pid, nullptr).second) {
        continue;
      }
      fout->append(absl::StrCat("    ", s.inst, "__i.", cpp_id(it->second.first), " = ",
                                operand(e.driver, it->second.second), ";\n"));
    }
    fout->append(absl::StrCat("    ", s.inst, ".cycle(", s.inst, "__i);  // deferred Moore-sub state advance\n"));
  }

  // Stage 0: if a combinational cycle survivor was hit anywhere above but the
  // precise Sub-input site did not already name it (a pure-cell comb loop with no
  // Sub on it), fail loudly with the best label we captured rather than emit a
  // silently-wrong sim that substitutes 0.
  if (cycle_unresolved_ && !cycle_reported_) {
    livehd::diag::err("inou.cgen.sim", "combinational-loop", "unsupported")
        .msg("module `{}` has a combinational loop the single-pass sim schedule cannot order: {} was read before it "
             "could be computed",
             gname, cycle_first_label_.empty() ? std::string{"a value"} : cycle_first_label_)
        .hint(
            "inou.cgen.sim emits one sequential `cycle()` per module; a combinational cycle (a real loop, or a false "
            "loop through an atomic Sub instance) has no valid emission order")
        .emit();
    cycle_reported_ = true;
  }

  // flop next-state (all computed from current state before any commit). Each
  // stage/q: reset ? rstval : (enable ? value_in : hold), mirroring cgen's
  // always block. value_in = din for stage0, the previous stage otherwise.
  for (const auto& f : flops) {
    auto din  = get_driver(find_sink_pin(f.node, "din"));
    auto rstp = get_driver(find_sink_pin(f.node, "reset_pin"));
    auto initp = get_driver(find_sink_pin(f.node, "initial"));
    auto enp  = get_driver(find_sink_pin(f.node, "enable"));
    bool negreset = false;
    if (auto np = get_driver(find_sink_pin(f.node, "negreset")); !np.is_invalid() && is_const_pin(np)) {
      negreset = !hydrate_const(np).is_known_false();
    }
    const std::string rstval = initp.is_invalid() ? absl::StrCat("Slop<", f.bits, ">::create_integer(0)") : operand(initp, f.bits);
    std::string       rtest;          // C++ bool: reset asserted (empty = no reset)
    bool              reset_always = false;
    if (!rstp.is_invalid()) {
      if (is_const_pin(rstp)) {
        reset_always = !hydrate_const(rstp).is_known_false();
      } else {
        rtest = absl::StrCat(operand(rstp, 1), negreset ? ".is_known_false()" : ".is_known_true()");
      }
    }
    std::string etest;                // C++ bool: write enabled (empty = always)
    if (!enp.is_invalid() && !is_const_pin(enp)) {
      etest = absl::StrCat(operand(enp, 1), ".is_known_true()");
    }
    auto next_of = [&](const std::string& value_in, const std::string& hold) -> std::string {
      if (reset_always) {
        return rstval;
      }
      std::string core = etest.empty() ? value_in : absl::StrCat("(", etest, " ? ", value_in, " : ", hold, ")");
      return rtest.empty() ? core : absl::StrCat("(", rtest, " ? ", rstval, " : ", core, ")");
    };
    const std::string din_expr = din.is_invalid() ? f.member : operand(din, f.bits);
    if (f.depth <= 1) {
      fout->append("    auto ", f.member, "_next = ", next_of(din_expr, f.member), ";\n");
    } else {
      fout->append("    auto ", f.stages[0], "_next = ", next_of(din_expr, f.stages[0]), ";\n");
      for (size_t i = 1; i < f.stages.size(); ++i) {
        fout->append("    auto ", f.stages[i], "_next = ", next_of(f.stages[i - 1], f.stages[i]), ";\n");
      }
      fout->append("    auto ", f.member, "_next = ", next_of(f.stages.back(), f.member), ";\n");
    }
  }

  // outputs (from current state)
  fout->append("    Out o;\n");
  for (const auto& io : ios) {
    if (io.is_input) {
      continue;
    }
    auto spin = g->get_output_pin(io.raw);
    auto drv  = spin.is_invalid() ? hhds::Pin_class{} : get_driver(spin);
    if (drv.is_invalid()) {
      fout->append("    // output ", io.field, " is undriven\n");
    } else {
      fout->append("    o.", io.field, " = ", operand(drv, io.bits), ";\n");
    }
  }

  // VCD sample + dump (pre-commit, current-state values at this cycle's
  // timestamp). EVERY traced instance snapshots its values here -- a sub's
  // cycle() runs mid-way through its parent's comb walk, so by the parent's
  // dump point the sub's live flops are already one edge ahead; the snapshot
  // preserves this-period semantics. Only the ROOT (the instance holding the
  // writer; peek() clears path+writer to suppress dumping) then walks the
  // hierarchy in timestamp-ORDERED phases (the writer hard-rejects any past
  // timestamp, so per-instance inline dumping cannot interleave correctly).
  //
  // One cycle() call advances one clock period of `__clk_ratio` ticks (10 VCD
  // time-units each). With the settle window (compile.sim.vcdfakedelay, the
  // default), edges and data are spread out so a waveform viewer shows
  // clock-edge -> data causality:
  //   base          : clock rises 0->1; root inputs take this period's pokes;
  //                   any about-to-change data goes X ("computing")
  //   base + 3      : posedge-sourced data settles (just after the rising edge)
  //   base + half   : clock falls 1->0   (half = ratio*5, the period midpoint)
  //   base + half+3 : negedge-sourced data settles (just after the falling edge)
  // Without it (vcdfakedelay=false) data lands exactly ON its clock edge: no X,
  // no +3 offset (the traditional, smaller trace). change() only writes when a
  // value differs from the previous timestamp.
  if (vcd_on) {
    // UNCONDITIONAL (not gated on __vcd_trace): on the very first traced cycle
    // the root's lazy __vcd_init below has not run yet -- and every sub already
    // cycled earlier in this comb walk -- so a gated snapshot would dump
    // default-zero values for the whole first period (the reset cycle of a lec
    // witness, or the opening cycle of a --vcd-from window).
    for (const auto& v : vsig) {
      fout->append(absl::StrCat("    ", v.snap, " = ", v.accessor, ";\n"));
    }
    fout->append("    if (!__vcd && !__vcd_path.empty()) __vcd_init();\n");
    fout->append("    if (__vcd) {\n");
    fout->append("      const unsigned __b    = __vcd_tick * 10;\n");
    fout->append("      const unsigned __half = (__clk_ratio > 0 ? __clk_ratio : 1) * 5;\n");
    // rising clock edge, in every clocked scope; root inputs change AT the edge
    fout->append("      vcd::global_timestamp = __b;\n");
    fout->append("      __vcd_clk(__vcd.get(), true);\n");
    fout->append("      __vcd_dump_in(__vcd.get());\n");
    if (vcd_fakedelay) {
      fout->append("      __vcd_dump_x(__vcd.get(), true, true);\n");
      fout->append("      vcd::global_timestamp = __b + 3;\n");
    }
    fout->append("      __vcd_dump_data(__vcd.get(), true, true);\n");
    // falling clock edge at the period midpoint, then the negedge-sourced data
    // (the calls recurse the whole subtree; scopes with none write nothing)
    fout->append("      vcd::global_timestamp = __b + __half;\n");
    fout->append("      __vcd_clk(__vcd.get(), false);\n");
    if (vcd_fakedelay) {
      fout->append("      __vcd_dump_x(__vcd.get(), false, true);\n");
      fout->append("      vcd::global_timestamp = __b + __half + 3;\n");
    }
    fout->append("      __vcd_dump_data(__vcd.get(), false, true);\n");
    fout->append("    }\n");
  }
  // Advance the period counter every cycle -- independent of VCD and of is_top,
  // so the reset window (read at the top of cycle()) tracks identically whether
  // or not a trace is dumped. The member exists on every module.
  fout->append("    __vcd_tick += (__clk_ratio > 0 ? __clk_ratio : 1);\n");

  // commit flops (+ pipe stages) at the clock edge
  for (const auto& f : flops) {
    // ICG fold: a gated flop commits only in ticks where its gate's enable
    // terms are true. Without this the gate is DEAD CODE and the flop loads
    // every tick regardless — a silently wrong waveform (2f-latch M5).
    std::string guard;
    for (const auto& gp : f.clock_guards) {
      absl::StrAppend(&guard, guard.empty() ? "" : " && ", "(", operand(gp, 1), ").is_known_true()");
    }
    const std::string ind = guard.empty() ? "    " : "      ";
    if (!guard.empty()) {
      fout->append("    if (", guard, ") {  // gated clock: commit only when the ICG enable is high\n");
    }
    for (const auto& s : f.stages) {
      fout->append(ind, s, " = ", s, "_next;\n");
    }
    fout->append(ind, f.member, " = ", f.member, "_next;\n");
    if (!guard.empty()) {
      fout->append("    }\n");
    }
  }

  // memory edge: sync-read registers sample the CURRENT array (before writes),
  // then writes apply (read-before-write, matching a reg-array sync memory).
  for (const auto& m : mems) {
    if (m.is_whole() && !m.registered()) {
      continue;  // combinational whole-array: contents applied in the combinational section
    }
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1 && !p.addr.is_invalid()) {
        int ab = std::max(1, bits_of(p.addr));
        fout->append(absl::StrCat("    ", m.member, "_q", std::to_string(p.rdidx), " = [&]{ size_t __a = static_cast<size_t>((",
                                  operand(p.addr, ab), ").to_just_i64()); return __a < ", m.size, " ? ", m.member,
                                  "[__a] : Slop<", m.bits, ">::create_integer(0); }();\n"));
      }
    }
    // Registered whole-array next-state: reset (highest) > per-port write > update
    // (lowest). Apply the bulk update FIRST so the per-port writes below override
    // the entries they touch (later assignment wins).
    std::string whole_close;
    if (m.is_whole()) {
      const int W = m.bits * m.size;
      if (!m.reset.is_invalid()) {
        std::string initbus
            = m.init.is_invalid() ? absl::StrCat("Slop<", W, ">::create_integer(0)") : operand(m.init, W);
        fout->append(absl::StrCat("    if ((", operand(m.reset, 1), ").is_known_true()) { slop_apply_update(", m.member, ", ",
                                  initbus, "); } else {\n"));
        whole_close = "    }\n";
      }
      std::string ue = m.update_enable.is_invalid() ? "" : absl::StrCat("if ((", operand(m.update_enable, 1), ").is_known_true()) ");
      fout->append(absl::StrCat("    ", ue, "slop_apply_update(", m.member, ", ", operand(m.update, W), ");\n"));
    }
    for (const auto& p : m.ports) {
      if (p.rd || p.addr.is_invalid() || p.din.is_invalid()) {
        continue;
      }
      // write enable: invalid -> always; const-false -> never; else runtime test
      std::string guard = "true";
      if (!p.enable.is_invalid()) {
        if (is_const_pin(p.enable)) {
          if (hydrate_const(p.enable).is_known_false()) {
            continue;
          }
        } else {
          guard = absl::StrCat("(", operand(p.enable, 1), ").is_known_true()");
        }
      }
      int ab = std::max(1, bits_of(p.addr));
      fout->append(absl::StrCat("    { size_t __a = static_cast<size_t>((", operand(p.addr, ab), ").to_just_i64()); if (", guard,
                                " && __a < ", m.size, ") ", m.member, "[__a] = ", operand(p.din, m.bits), "; }\n"));
    }
    fout->append(whole_close);  // close the reset `else` block, if any
  }
  fout->append("    return o;\n}\n");

  // peek(): the outputs implied by the CURRENT (post-last-edge) state, with NO
  // net effect. It runs cycle() (which computes the outputs then advances) but
  // snapshots and restores ALL state first -- flops, pipe stages, memories, AND
  // sub-instances (a child's cycle() commits its own state, so children must be
  // saved too or they double-advance) -- and suppresses VCD, so step() / an
  // output peek observes the post-edge value without perturbing state or the
  // waveform. (A non-copying save/restore is used because the VCD writer member
  // is move-only; sub-instances are plain copyable structs.)
  fout->append(mod, "::Out ", mod, "::peek(In in) {\n");
  for (const auto& f : flops) {
    fout->append(absl::StrCat("    auto _pk_", f.member, " = ", f.member, ";\n"));
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("    auto _pk_", s, " = ", s, ";\n"));
    }
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("    auto _pk_", m.member, " = ", m.member, ";\n"));
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("    auto _pk_", s.inst, " = ", s.inst, ";  // sub-instance snapshot\n"));
  }
  fout->append("    auto _pk_tick = __vcd_tick;\n");  // peek must not perturb the period counter
  if (vcd_on) {
    fout->append("    auto _pk_vcd = std::move(__vcd); auto _pk_vp = __vcd_path; __vcd_path.clear();\n");
  }
  fout->append("    Out o = cycle(in);\n");
  for (const auto& f : flops) {
    fout->append(absl::StrCat("    ", f.member, " = _pk_", f.member, ";\n"));
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("    ", s, " = _pk_", s, ";\n"));
    }
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("    ", m.member, " = _pk_", m.member, ";\n"));
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("    ", s.inst, " = _pk_", s.inst, ";\n"));
  }
  fout->append("    __vcd_tick = _pk_tick;\n");
  if (vcd_on) {
    fout->append("    __vcd = std::move(_pk_vcd); __vcd_path = _pk_vp;\n");
  }
  fout->append("    return o;\n}\n");

  // ---- dump_state: flops/regs -> the `_r` map (by hierarchical name, pyrope
  // literal); memories -> one editable `<_dir>/<_p><member>.hex` each; recurse
  // into sub-instances. Mirrors the reset_cycle/peek member walk. ----
  fout->append("void ", mod,
               "::dump_state(const std::string& _p, std::map<std::string, std::string>& _r, const std::string& _dir) const {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _r[_p + \"", s, "\"] = ", s, ".to_pyrope();\n"));
    }
    fout->append(absl::StrCat("  _r[_p + \"", f.member, "\"] = ", f.member, ".to_pyrope();\n"));
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("  hlop::ckpt::write_mem_hex(_dir + \"/\" + _p + \"", m.member, ".hex\", ", m.member, ");\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _r[_p + \"", m.member, "_q", p.rdidx, "\"] = ", m.member, "_q", p.rdidx, ".to_pyrope();\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".dump_state(_p + \"", s.inst, ".\", _r, _dir);\n"));
  }
  // The persistent input latch __in is state too: a poke-once-and-hold input must
  // survive a restart (the testbench may not re-poke it after the checkpoint cycle).
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append(absl::StrCat("  _r[_p + \"__in.", io.field, "\"] = __in.", io.field, ".to_pyrope();\n"));
    }
  }
  fout->append("}\n");

  // ---- load_state: set each flop/reg from the map IF PRESENT (a missing key
  // keeps the reset value -> cross-version reload tolerance); memories from the
  // hex file if present; recurse into sub-instances. ----
  fout->append("void ", mod,
               "::load_state(const std::string& _p, const std::map<std::string, std::string>& _r, const std::string& _dir) {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"", s, "\"); _it != _r.end()) ", s, " = Slop<", f.bits,
                                ">::from_pyrope(_it->second);\n"));
    }
    fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"", f.member, "\"); _it != _r.end()) ", f.member, " = Slop<",
                              f.bits, ">::from_pyrope(_it->second);\n"));
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("  hlop::ckpt::read_mem_hex(_dir + \"/\" + _p + \"", m.member, ".hex\", ", m.member, ");\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"", m.member, "_q", p.rdidx, "\"); _it != _r.end()) ",
                                  m.member, "_q", p.rdidx, " = Slop<", m.bits, ">::from_pyrope(_it->second);\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".load_state(_p + \"", s.inst, ".\", _r, _dir);\n"));
  }
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"__in.", io.field, "\"); _it != _r.end()) __in.", io.field,
                                " = Slop<", io.bits, ">::from_pyrope(_it->second);\n"));
    }
  }
  fout->append("}\n");

  // ---- design_hash: FNV-fold of every member name+width (+ mem size + sub
  // callee + recursion). Stamped into meta.json; a mismatch on load is a WARNING
  // (editable checkpoints are cross-version on purpose), never a hard reject. ----
  fout->append("std::uint64_t ", mod, "::design_hash() const {\n");
  fout->append("  std::uint64_t _h = hlop::ckpt::kFnvOffset;\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", s, "\"), ", f.bits, ");\n"));
    }
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", f.member, "\"), ", f.bits, ");\n"));
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", m.member,
                              "\"), ", m.bits, "), ", m.size, ");\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", m.member, "_q", p.rdidx, "\"), ",
                                  m.bits, ");\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a(_h, \"", s.inst, "\"); _h = hlop::ckpt::fnv1a_u64(_h, ", s.inst,
                              ".design_hash());\n"));
  }
  // Interface (every I/O port name+width+direction) so a port change warns.
  for (const auto& io : ios) {
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", io.field,
                              "\"), ", io.bits, "), ", io.is_input ? 1 : 2, ");\n"));
  }
  // A coarse logic fingerprint: the cell count (computed at codegen). It is not a
  // full structural hash, but it makes a logic-only change (added/removed cells —
  // same state layout) flip the hash, so a stale checkpoint still warns.
  {
    size_t _ncells = 0;
    for ([[maybe_unused]] auto node : g->fast_class()) {
      ++_ncells;
    }
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(_h, ", _ncells, ");\n"));
  }
  fout->append("  return _h;\n}\n");

  // ---- describe_signals / probe_signals: the observable scalar state by
  // hierarchical name (flops, pipe stages, sync-read regs, inputs; whole memories
  // and combinational outputs are excluded). describe_* lists name+bits+kind for
  // --list-signals; probe_* reads the current values for --probe / --break-when. ----
  fout->append("void ", mod, "::describe_signals(const std::string& _p, std::vector<hlop::ckpt::Signal>& _v) const {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _v.push_back({_p + \"", s, "\", ", f.bits, ", \"pipe\"});\n"));
    }
    fout->append(absl::StrCat("  _v.push_back({_p + \"", f.member, "\", ", f.bits, ", \"flop\"});\n"));
  }
  for (const auto& m : mems) {
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _v.push_back({_p + \"", m.member, "_q", p.rdidx, "\", ", m.bits, ", \"memrd\"});\n"));
      }
    }
  }
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append(absl::StrCat("  _v.push_back({_p + \"__in.", io.field, "\", ", io.bits, ", \"input\"});\n"));
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".describe_signals(_p + \"", s.inst, ".\", _v);\n"));
  }
  fout->append("}\n");

  fout->append("void ", mod, "::probe_signals(const std::string& _p, std::map<std::string, long>& _m) const {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _m[_p + \"", s, "\"] = ", s, ".to_i64_low();\n"));
    }
    fout->append(absl::StrCat("  _m[_p + \"", f.member, "\"] = ", f.member, ".to_i64_low();\n"));
  }
  for (const auto& m : mems) {
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _m[_p + \"", m.member, "_q", p.rdidx, "\"] = ", m.member, "_q", p.rdidx,
                                  ".to_i64_low();\n"));
      }
    }
  }
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append(absl::StrCat("  _m[_p + \"__in.", io.field, "\"] = __in.", io.field, ".to_i64_low();\n"));
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".probe_signals(_p + \"", s.inst, ".\", _m);\n"));
  }
  fout->append("}\n");

  // Persist the (updated) generation digests only after a CLEAN emission — a
  // Stage-0 comb-loop failure must not record a digest that would make the
  // next run skip over the same broken files.
  if (!odir.empty() && !cycle_reported_ && !cycle_unresolved_) {
    save_gen_digests();
  }
}
