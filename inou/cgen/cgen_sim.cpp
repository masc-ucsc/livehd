// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_sim.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <print>
#include <string>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "cell.hpp"           // Ntype / Ntype_op
#include "diag.hpp"           // livehd::diag::err — Stage 0 comb-loop safety net
#include "inline_sub.hpp"     // //graph — sim.flatten structural inline of a small sub-instance
#include "latch_contract.hpp"  // //graph — inline_clock_gate_cells (ICG gate -> local AND cone)
#include "node_util.hpp"      // //graph:graph — livehd::graph_util::* helpers
#include "split_selfref.hpp"  // //graph — word-level false-loop splitter (also run by pass/cprop)
#include "str_tools.hpp"      // str_tools::ends_with

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
    case Ntype_op::Sum     : return "Sum";
    case Ntype_op::Mult    : return "Mult";
    case Ntype_op::Div     : return "Div";
    case Ntype_op::And     : return "And";
    case Ntype_op::Or      : return "Or";
    case Ntype_op::Xor     : return "Xor";
    case Ntype_op::Not     : return "Not";
    case Ntype_op::LT      : return "LT";
    case Ntype_op::GT      : return "GT";
    case Ntype_op::EQ      : return "EQ";
    case Ntype_op::SHL     : return "SHL";
    case Ntype_op::SRA     : return "SRA";
    case Ntype_op::Mux     : return "Mux";
    case Ntype_op::Hotmux  : return "Hotmux";
    case Ntype_op::Get_mask: return "Get_mask";
    case Ntype_op::Set_mask: return "Set_mask";
    case Ntype_op::Sext    : return "Sext";
    case Ntype_op::Nconst  : return "Nconst";
    default                : return "op?";
  }
}
}  // namespace

std::string Cgen_sim::cpp_port_path(std::string_view name) {
  const auto dot = name.find('.');
  if (dot == std::string_view::npos) {
    return cpp_id(name);
  }
  return absl::StrCat(cpp_id(name.substr(0, dot)), ".", cpp_id(name.substr(dot + 1)));
}

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

namespace {
// Pyrope text for a constant, with any UNKNOWN bit forced to 0.
//
// Slop is a value type with no runtime unknowns, so the simulator has no way to
// represent an `x`: a `?` bit is simulated as 0. Emitting the raw `0sb1????...`
// form made the generated C++ carry a literal that from_pyrope cannot
// constant-evaluate, which forced every wide constant through a runtime parse
// (27.8% of simulation time on the dino CPU). Substituting the `?` up front
// makes every emitted literal foldable at compile time.
std::string sim_const_text(const Dlop& c) {
  auto txt = c.to_pyrope();
  if (c.has_unknowns()) {
    std::replace(txt.begin(), txt.end(), '?', '0');
  }
  return txt;
}
}  // namespace

std::string Cgen_sim::operand(const hhds::Pin_class& dpin, int target_bits, int sign_mode) {
  const std::string tw = std::to_string(target_bits);
  if (dpin.is_invalid()) {
    return absl::StrCat("Slop<", tw, ">::create_integer(0)");
  }
  if (is_const_pin(dpin)) {
    const auto c = hydrate_const(dpin);
    // Fast path -- the overwhelmingly common case. A plain Integer that fits an
    // int64 emits as create_integer(N): a constexpr array fill the compiler
    // always folds, versus from_pyrope's digit-by-digit parse (constexpr, but
    // only actually evaluated at compile time when the compiler chooses to).
    // Same bits by construction: both leave the value UNMASKED in base_[0] and
    // sign-extend into the upper words, and is_just_i64() (<= 62 bits, no
    // unknowns) guarantees the round-trip through int64 is exact.
    if (c.is_integer() && c.is_just_i64()) {
      return absl::StrCat("Slop<", tw, ">::create_integer(", c.to_just_i64(), ")");
    }
    // Exact via the shared pyrope codec: wide constants, unknown (`?`) bits, and
    // the non-Integer types (Boolean/String/Nil) that create_integer would flatten.
    // Folded at COMPILE time via a constexpr local. As a plain sub-expression in
    // a hot loop the compiler is free to keep from_pyrope's digit-by-digit parse
    // at runtime -- and it did: 27.8% of simulation time on the dino CPU. This is
    // only legal because sim_const_text() has already forced unknown bits to 0;
    // a literal still carrying `?` is not constant-evaluable.
    return absl::StrCat("([]{ constexpr auto _k = Slop<", tw, ">::from_pyrope(\"", sim_const_text(c), "\"); return _k; }())");
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
  const std::string& base         = it->second;
  // A Sum with a subtrahend (a `-` operand, sink pid != 0) can go negative; its
  // value wraps at the node width, so WIDENING it must sign-extend to propagate
  // the borrow -- matching Verilog re-evaluating `a - b` at the consumer width.
  // Safe: a non-negative sum has top bit 0, so sext == zext there.
  bool               sum_with_sub = false;
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

std::string Cgen_sim::raw_operand(const hhds::Pin_class& dpin, int fallback_bits) {
  const std::string fw = std::to_string(fallback_bits);
  if (dpin.is_invalid()) {
    return absl::StrCat("Slop<", fw, ">::create_integer(0)");
  }
  if (is_const_pin(dpin)) {
    const auto c = hydrate_const(dpin);
    if (c.is_integer() && c.is_just_i64()) {
      return absl::StrCat("Slop<", fw, ">::create_integer(", c.to_just_i64(), ")");
    }
    return absl::StrCat("([]{ constexpr auto _k = Slop<", fw, ">::from_pyrope(\"", sim_const_text(c), "\"); return _k; }())");
  }
  auto it = pin2var.find(dpin.get_class_index());
  if (it == pin2var.end()) {
    // Same unschedulable-comb-cycle guard as operand(): record it so
    // do_from_graph() fails loudly instead of silently simulating 0.
    cycle_unresolved_ = true;
    if (cycle_first_label_.empty()) {
      cycle_first_label_ = absl::StrCat("`", debug_name(dpin.get_master_node()), "` (a combinational-cycle back-edge)");
    }
    return absl::StrCat("Slop<", fw, ">::create_integer(0) /*UNRESOLVED-CYCLE*/");
  }
  if (!canonical_.contains(dpin.get_class_index())) {
    // A boundary value (module input, memory read, sub output) may hold a
    // non-canonical word for its declared width, so it still needs the
    // declared-width re-interpretation.
    return operand(dpin, fallback_bits);
  }
  return it->second;  // BARE value -- the op deduces its width
}

std::string Cgen_sim::node_expr(const hhds::Node_class& node, int wbits) {
  const auto op = type_op_of(node);
  const auto tw = std::to_string(wbits);
  auto       e  = sorted_inp(node);

  // 1-to-1 fold: `Slop<W>::op(a, b, ...)` over operands read at their OWN widths.
  // Left-associated to match the previous member-chain result exactly.
  auto fold = [&](const char* method) -> std::string {
    if (e.empty()) {
      return absl::StrCat("Slop<", tw, ">::create_integer(0)");
    }
    std::string s = raw_operand(e[0].driver, wbits);
    for (size_t i = 1; i < e.size(); ++i) {
      s = absl::StrCat("Slop<", tw, ">::", method, "(", s, ", ", raw_operand(e[i].driver, wbits), ")");
    }
    if (e.size() == 1) {
      // A single operand still has to land at the node width.
      return absl::StrCat("Slop<", tw, ">{", s, "}");
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
    case Ntype_op::And : return fold("and_op");
    case Ntype_op::Or  : return fold("or_op");
    case Ntype_op::Xor : return fold("xor_op");
    case Ntype_op::Mult: return fold("mult_op");
    case Ntype_op::Div : {
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
    case Ntype_op::Rem: {
      // Same shape and the SAME reason as Div above: this switch has no
      // fail-closed default, so a missing arm here would silently simulate
      // `a % b` as the bare dividend. Truncated remainder, sign following the
      // dividend -- Dlop::rem_op is exactly that, and there is only one flavour
      // because every value is signed.
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits);
      }
      int cw = std::max({wbits_of(e[0].driver), wbits_of(e[1].driver), wbits, 1});
      if (!is_unsign(e[0].driver) || !is_unsign(e[1].driver)) {
        cw += 1;
      }
      return absl::StrCat(operand(e[0].driver, cw), ".rem_op(", operand(e[1].driver, cw), ")");
    }
    case Ntype_op::Ror: {
      // OR-reduction: 1 iff ANY bit of ANY operand is set. Each operand must be
      // read at its OWN full width (not the 1-bit node width), then reduced --
      // `a.ror_op()` is the unary reduce, `a.ror_op(b)` reduces both. Matches the
      // Verilog cgen `|a` / `|{a|b|...}`.
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      auto        ow = [&](size_t i) { return operand(e[i].driver, std::max(wbits_of(e[i].driver), 1)); };
      std::string s  = (e.size() == 1) ? absl::StrCat(ow(0), ".ror_op()") : ow(0);
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
      // 1-to-1: the mixed-width compare reads both operands at their own widths
      // (sign-extending each into the compare, so a narrow signed operand still
      // compares signed) and materializes a 0/1 MAGNITUDE at the node width.
      // That replaces three emitted conversions -- two operand reads plus the
      // `.zext_to<1>().zext_to<tw>()` clamp that existed only because the member
      // form returns create_bool's all-ones (-1). The `cw += 1` headroom above is
      // likewise unnecessary here: it existed only to force cw != operand width so
      // the cross-width ctor would fire instead of the copy ctor.
      return absl::StrCat("Slop<", tw, ">::", m, "(", raw_operand(e[0].driver, cw), ", ", raw_operand(e[1].driver, cw), ")");
    }
    case Ntype_op::SHL:
    case Ntype_op::SRA: {
      const bool is_shl = op == Ntype_op::SHL;
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)")
                         : operand(e[0].driver, wbits, is_shl ? 0 : /*signed=*/1);
      }
      // A shift AMOUNT is a count, not a value in the datapath. When it is
      // constant, hand the int64 overload the number directly instead of
      // materializing a whole Slop<W> constant just to pass it (which, at
      // W > 64, was a multi-word object built per shift).
      if (is_const_pin(e[1].driver)) {
        const auto amt = hydrate_const(e[1].driver);
        if (amt.is_integer() && amt.is_just_i64() && amt.to_just_i64() >= 0) {
          return absl::StrCat("Slop<", tw, ">::", is_shl ? "shl_op" : "sra_op", "(", raw_operand(e[0].driver, wbits), ", ",
                              amt.to_just_i64(), ")");
        }
      }
      // Runtime amount: keep the member form (it reads amount.base_[0]).
      // The shifted operand of an ARITHMETIC shift must be read as signed.
      return absl::StrCat(operand(e[0].driver, wbits, is_shl ? 0 : /*signed=*/1), ".", is_shl ? "shl_op" : "sra_op", "(",
                          operand(e[1].driver, wbits), ")");
    }
    case Ntype_op::Get_mask: {
      // value (e[0]) + optional mask (e[1]). The unary form is the common tolg
      // width-adjust; lower it to a plain zext.
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      if (e.size() == 1) {
        return operand(e[0].driver, wbits, /*unsigned=*/-1);
      }
      // FAST PATHS for the two mask shapes that dominate real designs. Both
      // replace a call to Slop::get_mask_op -- which is a PER-BIT LOOP
      // (bit_test + set for every selected bit, plus every bit up to
      // get_bits() for a negative mask) -- with a single mask instruction.
      // A profile of the dino CPU put 68.9% of simulation time inside
      // get_mask_op and another 27.8% inside from_pyrope parsing the wide mask
      // CONSTANTS it is called with, against 2.2% in the actual module bodies.
      if (is_const_pin(e[1].driver)) {
        const auto mv = hydrate_const(e[1].driver);
        if (!mv.has_unknowns()) {
          // (a) mask == -1 is the to-positive idiom. The value is read UNSIGNED
          // (zext_to), which already yields a non-negative value, so making it
          // positive is the identity -- as is the trailing trim. The whole
          // `x.zext_to<W>().get_mask_op(-1).zext_to<W>()` sequence collapses to
          // `x.zext_to<W>()`.
          if (mv.is_just_i64() && mv.to_just_i64() == -1) {
            return operand(e[0].driver, wbits, /*unsigned=*/-1);
          }
          // (b) a LOW-CONTIGUOUS mask 2^n-1 keeps the low n bits and zeroes the
          // rest, packed LSB-first in place -- which is exactly what zext_to<n>
          // does, in one masking step instead of n loop iterations. It also
          // deletes the mask constant itself, so a >64-bit mask no longer costs
          // a from_pyrope string parse per cycle.
          //
          // n == 1 needs no special case: the member form returns the signed -1
          // for a lone selected bit and cgen clamps it with .zext_to<1>();
          // zext_to<1> produces the same 0/1 directly.
          //
          // GUARDED to masks that stay INSIDE the value's declared width. A mask
          // reaching past it (e.g. `(x#sext[..])#[0..=63]` on a narrower x)
          // selects the value's SIGN bits, which the slow path below gets by
          // reading the value per its declared sign; zext_to would read those
          // positions as 0 instead. Dropping this guard breaks bitset_imm and
          // bitset_nil.
          if (!mv.is_negative()) {
            auto [mb, me] = mv.get_mask_range();  // half-open; {-1,-1} = noncontiguous
            // ...and a mask that DOES reach past the width is still fine when the
            // value is UNSIGNED: the positions above it select sign bits, which
            // are all zero there, and a zero-extended read reproduces exactly
            // that. Only a signed value needs the slow path's sign replication.
            //
            // This is not a corner case. dino's ImmediateGenerator and top level
            // mask 4- and 15-bit intermediates with the 64-bit all-ones constant
            // (a plain "keep the low 64 bits" produced by the Slop capacity
            // invariant, bits = magnitude+1). Every one of those failed
            // `me <= wbits` -- 64 <= 4 -- and fell into a 64-iteration bit loop
            // that measured 47% of total simulation time, before AND after
            // sim.flatten (flattening moves the call, it does not remove it).
            const bool in_width = me <= wbits_of(e[0].driver);
            if (mb >= 0 && me > mb && (in_width || is_unsign(e[0].driver))) {
              // get_mask packs the selected bits LSB-FIRST, so a contiguous run
              // [mb,me) is exactly (x >> mb) & mask(me-mb) -- two instructions
              // instead of an (me-mb)-iteration loop. mb == 0 is plain
              // truncation; mb > 0 is a bit-slice `x[hi:lo]`, which every
              // constant mask still reaching get_mask_op on the dino CPU is.
              //
              // The read is zero-extended to `me` first, so the value is
              // non-negative and sra_op degenerates to a logical shift -- no
              // sign bits can leak down into the extracted field.
              auto expr = operand(e[0].driver, me, /*unsigned=*/-1);
              if (mb > 0) {
                expr = absl::StrCat(expr, ".sra_op(", mb, ")");
              }
              return absl::StrCat(expr, ".zext_to<", tw, ">()");
            }
          }
        }
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
      //
      // NOT converted to the mixed-width Slop<W>::get_mask_op. That call reads
      // each operand at its own width and sign-extends internally, but the
      // `operand(..., cw, -1)` below is a ZERO-extend: it masks the value to its
      // declared width and clears everything above. The two differ for a negative
      // value under the to-positive idiom, which broke 131 simeq goldens when
      // tried. Converting this arm needs the mixed-width op to take the source's
      // declared width into account, not just its storage sign.
      int  val_sign   = -1;
      bool single_bit = false;
      if (is_const_pin(e[1].driver)) {
        auto mv    = hydrate_const(e[1].driver);
        val_sign   = (mv.is_just_i64() && mv.to_just_i64() == -1) ? -1 : 0;
        single_bit = !mv.is_negative() && mv.popcount() == 1;
      }
      std::string gm = absl::StrCat("(", operand(e[0].driver, cw, val_sign), ".get_mask_op(", operand(e[1].driver, cw, -1), "))");
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
      return absl::StrCat(operand(e[0].driver, wbits, -1),
                          ".set_mask_op(",
                          operand(e[1].driver, wbits, -1),
                          ", ",
                          operand(e[2].driver, wbits),
                          ")");
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
      return absl::StrCat("Slop<", tw, ">{", operand(e[0].driver, sw, /*signed=*/1), ".sext_op(", std::to_string(frombit), ")}");
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
        size_t n_vals = e.size() - 1;
        if (n_vals == 2) {
          // A 2-arm Mux selector is a CONDITION, not an index: ANY nonzero value
          // selects arm 1. That matches cgen_verilog (`sel ? arm1 : arm0`, whose
          // netlist `lhd lec` proves) and the LGraph model, where an `if` cone
          // hands the Mux whatever the condition computed -- `a & 0x80` arrives
          // as 128, not as 1.
          //
          // This used to truncate the selector to its low bit, to turn Slop's
          // all-ones boolean (`create_bool(true)` == -1) into 1. That is correct
          // ONLY for a value that is already 0/1: for any nonzero-but-EVEN
          // condition the low bit is 0, so the mux silently took arm 0.
          // `if a & 0x80 { y = 1 } else { y = 2 }` returned 2 for EVERY input.
          // A nonzero test maps both forms right: -1 -> 1 and 128 -> 1.
          sel = absl::StrCat("Slop<", tw, ">::create_integer((", sel, ").is_known_true() ? 1 : 0)");
        } else {
          // 3+ arms: the selector IS an index (0..n-1). Keep only the
          // ceil(log2(n)) low index bits, then re-widen to tw.
          int sel_w = 1;
          while ((static_cast<size_t>(1) << sel_w) < n_vals) {
            ++sel_w;
          }
          if (sel_w < wbits) {
            sel = absl::StrCat("(", sel, ").zext_to<", sel_w, ">().zext_to<", wbits, ">()");
          }
        }
      }
      return absl::StrCat("Slop<", tw, ">::", m, "(", sel, ", {", vals, "})");
    }
    case Ntype_op::Clock_cell:
      // FAIL CLOSED, never fall into the pass-through below. A Clock_cell's
      // first input is `clk_ref`, so the generic fallback would emit the
      // UNGATED reference clock and silently drop the gate -- the flop would
      // then commit every tick with the enable as dead code. That is the same
      // silent miscompile `gated-clock-unsupported` refuses one level up, and a
      // clock gate is precisely the shape where it is invisible to a
      // before/after comparison. The M9 lowering resolves the cell to a commit
      // guard instead; until it lands for this shape, refuse by name.
      livehd::diag::err("inou.cgen.sim", "clock-cell-unsupported", "unsupported")
          .msg("cell `{}` is a Clock_cell reaching expression emission", debug_name(node))
          .hint(
              "a Clock_cell must be lowered to a flop/memory COMMIT GUARD, never evaluated as a data expression -- "
              "emitting it as a value would hand the simulator an ungated clock with the enable as dead code")
          .fatal();
      return absl::StrCat("Slop<", tw, ">::create_integer(0)");
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
// 3: per-module <stem>.iface.json manifest + observable outputs/memories (2f-sim B0/B)
// simgen-5: next-state and the commit enable became persistent members (_din,
// _cen) so the commit can be its own method. simgen-4: cycle() gained its
// trailing __settle() and peek() was removed. The
// bump is not cosmetic -- this digest gates a per-module early return that skips
// emission entirely, so a warm workdir would otherwise keep stale settle-only
// .cpp files whose parent calls a peek() that no longer exists (a compile error
// at best; at worst a stale child that commits at the other end of the period).
static constexpr std::string_view kSimGenVersion = "simgen-5";

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
  namespace gu                                       = livehd::graph_util;
  uint64_t                                         h = 0xcbf29ce484222325ULL;
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
std::vector<hhds::Pin_class> Cgen_sim::icg_guards(const hhds::Node_class& flop, std::string_view clock_port,
                                                 const livehd::latch_contract::Design_clocks* clocks) {
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
    // A GENERATED clock -- a clock_cell output, or the reference clock aliased
    // through an internal net (`clock = clk_i`, which minion's csr file does) --
    // is not a graph input, so the name test below cannot see it. Ask the shared
    // clock-role analysis first; it answers from the resolved clock cones.
    //
    // Measured on minion_top: 20 errors -> 19, `gated-clock-unsupported` 11 -> 9
    // (one extra comb loop appears, hence the net of 1). Standalone
    // intpipe_csr_file goes 2 errors -> 1, its gated-clock refusal gone.
    //
    // This is a PARTIAL answer, not the real step 1. is_clock() says yes for any
    // clock root, including a generated one, so it cannot distinguish "the
    // reference clock of the domain this flop commits in" from "some clock".
    // The full fix is enable/divider on Commit_class -- see todo_sim_pipeline.md.
    if (clocks != nullptr && clocks->is_clock(p)) {
      saw_clock = true;
      continue;
    }
    if (!p.is_invalid() && livehd::graph_util::is_graph_input_pin(p)) {
      const auto pn = pin_name_of(p);
      // The reference clock, consumed by the tick itself. When the module has a
      // RESOLVED clock port, require an exact match. When it does not — which is
      // the normal case for an ICG design, because clock_input_of() only finds a
      // clock by looking for a flop wired STRAIGHT to an input, and here the only
      // flop is wired to the gate — fall back to the conventional spelling. A
      // miss is fail-CLOSED (the caller refuses with a diagnostic), never a
      // silent commit-every-tick.
      //
      // Use the SHARED token-wise matcher, which is public precisely so every
      // consumer has one notion of what a clock is named. Hardcoding
      // `clk`/`clock` here refused minion outright: its ports are `clk_i`, so an
      // ordinary `prim_clk_gate` ICG failed this test and was reported as "some
      // other derived clock" — a naming gap masquerading as an unsupported
      // shape, on 7 distinct flops.
      if (!clock_port.empty() ? (pn == clock_port) : livehd::latch_contract::Design_clocks::name_looks_like_clock(pn)) {
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

// Node count of one def's body. Memoized: a def is reached once per
// instantiation site, and counting is a full traversal.
int Cgen_sim::graph_node_count(hhds::Graph* g) {
  if (g == nullptr) {
    return 0;
  }
  if (auto it = node_count_memo_.find(g); it != node_count_memo_.end()) {
    return it->second;
  }
  int n = 0;
  for ([[maybe_unused]] auto node : g->fast_class()) {
    ++n;
  }
  node_count_memo_.emplace(g, n);
  return n;
}

// sim.flatten=N: structurally inline every sub-instance whose callee body is
// small enough, BOTTOM-UP.
//
// Children first, which is exactly what inline_sub_instance's single-level
// contract asks for: by the time a def is inlined anywhere, its own small
// children are already part of its body, so one pass absorbs a whole chain of
// small leaves with no instantiation-context bookkeeping.
//
// The size test is on the callee body AFTER its own children were absorbed, so
// the count that decides is the code actually about to be spliced in rather than
// the pre-inline stub. That is what makes the budget mean "how much C++ am I
// willing to duplicate per instantiation site".
//
// COST MODEL, because this knob is easy to misread: inlining removes a call, the
// In/Out structs copied across it, and the port-boundary width adjusts — all
// per-CYCLE savings — but it emits the body once per instantiation SITE, so
// generated code and host C++ time grow with the instance COUNT. That is why the
// default is 0, and why a blanket "flatten everything" is the wrong trade on a
// large design even though it is the fastest per cycle.
int Cgen_sim::flatten_small_subs(hhds::Graph* g) {
  if (g == nullptr || flatten_budget <= 0 || !flatten_walked_.insert(g).second) {
    return 0;
  }
  int inlined = 0;
  // Recurse first. Held as shared_ptrs so a child def stays alive while we walk
  // it, independent of what happens to the instance node in this body.
  std::vector<std::shared_ptr<hhds::Graph>> children;
  for (auto n : g->fast_class()) {
    if (type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    if (auto cg = n.get_subnode_graph()) {
      children.push_back(cg);
    }
  }
  for (const auto& cg : children) {
    inlined += flatten_small_subs(cg.get());
  }

  // Snapshot the victims: inline_sub_instance deletes nodes and fast_class() is
  // a live view over the node table.
  std::vector<hhds::Node_class> victims;
  for (auto n : g->fast_class()) {
    if (type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    auto cg = n.get_subnode_graph();
    if (!cg) {
      continue;  // body-less black box (liberty cell / external IP): nothing to inline
    }
    if (graph_node_count(cg.get()) <= flatten_budget) {
      victims.emplace_back(n);
    }
  }
  for (const auto& v : victims) {
    if (!livehd::graph_util::inline_sub_instance(g, v, "inou.cgen.sim")) {
      // A partially inlined body is fatal, exactly as flatten treats it; the
      // located diagnostic already came from inline_sub_instance.
      return inlined;
    }
    node_count_memo_.erase(g);  // this body just grew
    ++inlined;
  }
  return inlined;
}

void Cgen_sim::do_from_graph(const std::shared_ptr<hhds::Graph>& graph) {
  pin2var.clear();
  tmp_cnt           = 0;
  cycle_unresolved_ = false;
  cycle_reported_   = false;
  cycle_first_label_.clear();

  hhds::Graph* g     = graph.get();
  auto         gio   = g->get_io();
  const auto   gname = std::string{graph->get_name()};
  const auto   mod   = cpp_id(gname);

  // VCD trace (sim.vcd): only the --top module emits it (so a
  // hierarchy writes one file). top empty -> single-module run, emit anyway.
  const auto entity = gname.substr(gname.rfind('.') + 1);
  // A `test` block lowers to a compiler-minted (`%`-named) comb — a testbench,
  // not synthesizable hardware. Its asserts are checked by running the `lhd sim`
  // driver (prp_sim), never by emitting a Slop unit (which would pull in the
  // formal-property header it does not need). Skip it; the kernel's sim_into()
  // drops it from the build's source list too so the two stay consistent.
  if (!entity.empty() && entity.front() == '%') {
    return;
  }
  // Bring an instantiated clock-gate cell's gate INTO this body — the same
  // pre-step pass.single_edge and the formal commands run.
  //
  // An ICG arrives as a Sub (`prim_clk_gate`: a low-transparent latch capturing
  // `en_i`, then `clk_o = clk_i & en_latch`), and while it stays a Sub it breaks
  // sim codegen twice over. Its output is a clock this module's flops carry as
  // `clock_pin`, which icg_guards() cannot fold because that matcher walks an
  // AND cone in the LOCAL body and the gate is behind a call — so every such
  // flop is refused as "a derived clock inou.cgen.sim cannot fold into a commit
  // guard". And because a Sub is simulated atomically (all inputs -> all
  // outputs), the perfectly ordinary feedback `clk_o -> flops -> en_i -> clk_o`
  // reads as a combinational loop THROUGH the instance. Inlining the gate turns
  // both into the local AND cone icg_guards already folds into a flop enable,
  // which is the same treatment lec gives it.
  if (const int ncg = livehd::latch_contract::inline_clock_gate_cells(g, "inou.cgen.sim"); ncg > 0) {
    livehd::diag::info("inou.cgen.sim", "clock-gate-inlined", "progress")
        .msg("`{}`: inlined {} clock-gate cell(s) so their gate folds into a flop enable", gname, ncg)
        .emit();
  }
  // sim.flatten=N: absorb small sub-instances into this body first, so
  // everything below — the digest, the schedule, the emission — sees the
  // flattened graph. A no-op at the default N=0.
  if (const int nin = flatten_small_subs(g); nin > 0) {
    livehd::diag::info("inou.cgen.sim", "sim-flatten", "progress")
        .msg("`{}`: inlined {} sub-instance(s) with <= {} nodes (sim.flatten)", gname, nin, flatten_budget)
        .emit();
  }
  // Break a false combinational loop through an ATOMIC pure-comb sub-instance by
  // inlining the offending instance into this graph before scheduling. A no-op
  // unless a stateless Sub's output feeds back into one of its own inputs.
  livehd::graph_util::flatten_false_loop_subs(g);
  // ...and the STATEFUL remainder, which the pass above declines by design ("any
  // Flop/Latch/Fflop/Memory anywhere makes inlining change state identity").
  //
  // A Sub is simulated atomically -- one call produces every output from every
  // input -- so an output that feeds back, through parent logic, into one of the
  // instance's own inputs has no valid single-pass order, even when the callee
  // has no internal path between that pair. Two shapes already dissolve without
  // inlining: a MOORE callee (every output a pure state read, so the whole call
  // defers) and a MEALY callee whose FED-BACK outputs are all state reads (those
  // pre-bind and the call orders normally). What is left needs the callee
  // evaluated in pieces, and inlining is how to get that -- exactly what the
  // refusal's own hint has always advised ("flatten this instance for sim").
  //
  // The state-identity cost is real but bounded: inline_sub_instance prefixes
  // cloned names with the instance (`u_dcache.foo`), so checkpoint and
  // --list-signals names stay hierarchical. Paying it beats refusing the design.
  {
    auto state_only_of = [&](const hhds::Node_class& m, uint32_t pid) -> bool {
      auto cg = m.get_subnode_graph();
      auto sio = m.get_subnode_io();
      if (!cg || !sio) {
        return false;
      }
      if (callee_is_moore(cg, sio)) {
        return true;
      }
      return callee_state_only_outputs(cg, sio).contains(pid);
    };
    int broken = 0;
    // Inlining one offender can expose another (a loop threading through two
    // instances), so iterate to a fixpoint; the bound is the instance count.
    for (int round = 0; round < 64; ++round) {
      std::vector<hhds::Node_class> victims;
      for (auto n : g->fast_class()) {
        if (type_op_of(n) != Ntype_op::Sub) {
          continue;
        }
        auto cg  = n.get_subnode_graph();
        auto sio = n.get_subnode_io();
        if (!cg || !sio) {
          continue;  // body-less black box: nothing to inline
        }
        auto fed_back = sub_false_loop_output_pids(n, state_only_of);
        if (fed_back.empty() || callee_is_moore(cg, sio)) {
          continue;  // not on a false loop, or the whole call defers instead
        }
        const auto state_only = callee_state_only_outputs(cg, sio);
        bool       all_state  = true;
        for (auto pid : fed_back) {
          if (!state_only.contains(pid)) {
            all_state = false;
            break;
          }
        }
        if (!all_state) {
          victims.push_back(n);
        }
      }
      if (victims.empty()) {
        break;
      }
      for (const auto& v : victims) {
        if (!livehd::graph_util::inline_sub_instance(g, v, "inou.cgen.sim")) {
          break;
        }
        ++broken;
      }
    }
    if (broken > 0) {
      livehd::diag::info("inou.cgen.sim", "false-loop-inlined", "progress")
          .msg("`{}`: inlined {} sub-instance(s) to break a false combinational loop through them", gname, broken)
          .emit();
    }
  }
  // The shared clock-role analysis -- the same `Design_clocks` that pass/lec's
  // phase schedule and pass.single_edge build. Built AFTER the inlining
  // pre-steps above, which change the graph.
  const livehd::latch_contract::Design_clocks design_clocks(g, /*hier=*/false);

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
  const bool is_top = top.empty() || entity == top || gname == top;

  // Liveness: only logic something REAL consumes gets emitted — backward BFS
  // from the sinks (IO nodes cover the graph outputs; state elements, Memory
  // and Sub calls gather their input cones at emission).
  live_.clear();
  {
    std::vector<hhds::Node_class> lstk;
    for (auto n : g->fast_class()) {
      auto nop = livehd::graph_util::type_op_of(n);
      if (nop == Ntype_op::Sub || nop == Ntype_op::Memory || nop == Ntype_op::IO || livehd::graph_util::is_type_register(n)) {
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
  const bool vcd_on = !vcd_file.empty();

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
        && std::filesystem::exists(base + ".cpp", ec) && std::filesystem::exists(base + ".iface.json", ec)) {
      return;
    }
    gen_digests_[gname] = hex;  // persisted below, after a clean emission
  }
  auto hout = std::make_shared<File_output>(absl::StrCat(base, ".hpp"));  // interface
  auto fout = std::make_shared<File_output>(absl::StrCat(base, ".cpp"));  // definitions ("the slop")

  // Header (<name>.hpp): data members + In/Out + method DECLARATIONS only. A
  // module that instantiates this one #includes this small header (by-value
  // member), so it recompiles when this interface changes, not when the body
  // (in the .cpp) does. The cycle()/reset_cycle() bodies live in the .cpp and
  // are compiled exactly once.
  hout->append("// Generated by inou.cgen.sim (LiveHD, TODO 3d). Do not edit.\n");
  hout->append(
      "#pragma once\n#include <array>\n#include <cstdint>\n#include <map>\n#include <string>\n#include <vector>\n"
      "#include \"slop.hpp\"\n#include \"memory.hpp\"\n");
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
  // C++ access path for a port. A tuple leaf keeps its dot — it is a member of
  // the nested struct emitted for the tuple below — so the RTL name and the C++
  // path stay the same text; only the individual segments are mangled.
  std::vector<Io> ios;
  if (gio) {
    for (const auto& d : gio->get_input_pin_decls()) {
      ios.push_back({cpp_port_path(d.name), std::string{d.name}, 0, true, static_cast<uint32_t>(d.port_id)});
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      ios.push_back({cpp_port_path(d.name), std::string{d.name}, 0, false, static_cast<uint32_t>(d.port_id)});
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
    const std::string clock_port          = clock_input_of(g);
    // See through tolg's identity port wrappers (unary Get_mask width-adjust,
    // and the Get_mask(v,-1) to-positive idiom), same as clock_input_of().
    auto              resolve_passthrough = [](hhds::Pin_class p) {
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
      if (!icg_guards(node, clock_port, &design_clocks).empty()) {
        clock_nets.insert("\x01implicit");  // folded: commits on the reference clock, qualified
        continue;
      }
      livehd::diag::err("inou.cgen.sim", "gated-clock-unsupported", "unsupported")
          .msg("module `{}`: flop `{}` has a derived clock inou.cgen.sim cannot fold into a commit guard", gname, debug_name(node))
          .hint(
              "only the ICG shape `<clock> & <enable>` is folded (commit when the enable is high at the reference "
              "edge); any other derived clock would be simulated as if it ticked every step, with the gate as dead "
              "code — a silent miscompile. Model the gate as the flop's `enable` instead, or simulate the emitted "
              "Verilog with an event-driven simulator")
          .emit();
      return;
    }
    // MULTI-CLOCK is supported as of M6 — the M0 refusal here is LIFTED. State
    // on a net other than the reference clock commits on a detected EDGE of
    // that net (see Flop::sec_clock), so a second clock is simply a data input
    // the testbench toggles. What is NOT lifted is a derived clock we cannot
    // fold (handled above): that still fails closed, because there is no net to
    // detect an edge on without inventing one.
    (void)clock_nets;
  }

  // ---- flops (Flop cells; Latch/Memory -> later phase) ----
  struct Flop {
    hhds::Node_class             node;
    std::string                  member;
    int                          bits;
    int                          depth;              // pipe_min shift-register depth (>=1)
    std::vector<std::string>     stages;             // depth-1 intermediate stage members (q is the last)
    bool                         posedge    = true;  // false = negedge flop (posclk known-false)
    bool                         is_latch   = false;
    // 2f-latch M8 step 0d. On a LATCH, `posclk` is the ENABLE POLARITY, not an
    // edge (graph/cell.cpp): known-false means transparent while enable == 0,
    // so the write test is INVERTED. Reading the pin with the Flop meaning did
    // two wrong things at once — it put the latch in the negedge sub-tick and
    // left the enable un-inverted — for the one shape that produces it, a
    // yosys-imported active-low $dlatch. cgen_verilog gets this right
    // (`neg_en` -> `if (!en)`), so sim silently disagreed with its own Verilog.
    bool                         neg_enable = false;
    // ICG fold (2f-latch M5): non-clock operands of a `<clock> & <enable>` clock
    // cone. Non-empty => this flop commits only in ticks where every guard is
    // true. Empty => an ungated clock, i.e. commit every tick.
    std::vector<hhds::Pin_class> clock_guards;
    // SECONDARY CLOCK (2f-latch M6). Invalid => this flop rides the module's
    // REFERENCE clock, one edge per tick, exactly as before. Valid => the flop
    // hangs on a DIFFERENT clock net (a second clock port the testbench drives
    // as data), so it commits only on a detected edge of that net, tracked by
    // `prev_member`.
    hhds::Pin_class              sec_clock;
    std::string                  prev_member;
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
    // which IS transparency sampled at the end of the tick. The scheduler below
    // evaluates a low window and then a high window, carrying the settled low
    // value into the latter. That supplies both master/slave ordering and the
    // coincident-edge read-through needed by the L1 fixture (verified against
    // the Icarus schedules in tests/sim/latch_sim_{master_slave,l1_buffer}.prp).
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
    Flop f{node, cpp_id(wire_name(qpin)), wbits_of(qpin), depth, {}, true, type_op_of(node) == Ntype_op::Latch, false, {}, {}, {}};
    const std::string ref_clock = clock_input_of(g);
    f.clock_guards              = icg_guards(node, ref_clock, &design_clocks);
    // SECONDARY CLOCK (2f-latch M6): a clock_pin wired to a graph input that is
    // NOT the module's reference clock. One tick is one period of the REFERENCE
    // clock, so a second clock cannot also tick once per call — it is a signal
    // the testbench drives, and its flops commit on a detected edge of it.
    if (f.clock_guards.empty()) {
      auto cd = get_driver(find_sink_pin(node, "clock_pin"));
      for (int hops = 0; hops < 8 && !cd.is_invalid(); ++hops) {
        auto cn = cd.get_master_node();
        if (type_op_of(cn) != Ntype_op::Get_mask) {
          break;
        }
        auto ce = sorted_inp(cn);
        if (ce.empty()) {
          break;
        }
        if (ce.size() >= 2 && !is_const_pin(ce[1].driver)) {
          break;
        }
        cd = ce[0].driver;
      }
      if (!cd.is_invalid() && livehd::graph_util::is_graph_input_pin(cd) && std::string{pin_name_of(cd)} != ref_clock) {
        f.sec_clock   = get_driver(find_sink_pin(node, "clock_pin"));
        f.prev_member = absl::StrCat("__clkprev_", cpp_id(std::string{pin_name_of(cd)}));
      }
    }
    // posedge (default) vs negedge clock: the comptime `posclk` pin, known-false
    // means negedge -- and, since M5, it also picks the sub-tick the element
    // commits in. On a LATCH the SAME pin means enable polarity instead, so it
    // must never reach f.posedge: a latch always commits in the primary
    // sub-tick and carries its polarity in `neg_enable` (M8 step 0d).
    if (auto pc = get_driver(find_sink_pin(node, "posclk")); !pc.is_invalid() && is_const_pin(pc)) {
      const bool pos = !hydrate_const(pc).is_known_false();
      if (type_op_of(node) == Ntype_op::Latch) {
        f.neg_enable = !pos;
      } else {
        f.posedge = pos;
      }
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
    hhds::Node_class node;
    std::string      member;
    int              bits = 1, size = 0, type = 2;
    // Per-(read,write) forwarding matrix (graph/cell.cpp): bit (rdidx*n_wr +
    // wridx). Held as the const itself — Dlop::bit_test is arbitrary precision,
    // and n_rd*n_wr overflows an int on wide shapes.
    spool_ptr<Dlop>  fwd;
    // Same layout: bit (rdidx*n_wr + wridx) set => that collision is UNDEFINED
    // (Pyrope ordering="none"). Slop has no x, so the ruling's "sim randomizes"
    // applies — a latent collision then fails loudly instead of silently
    // reading the committed value.
    spool_ptr<Dlop>  undef;
    int              n_wr    = 0;
    int              n_rd    = 0;
    int              wensize = 1;  // sub-word write-enable lanes (1 = whole entry)
    // The two matrices classified into one of the four hlop::Memory_* types.
    // Every `ordering` mode makes each row a PREFIX over the user write ports
    // (upass_tolg.cpp ~3585), so a row is a count, never a bit vector.
    enum class Order { old, fwd, program, none };
    Order                order     = Order::old;
    int                  n_user_wr = 0;  // trailing ports are reset/restore: never forward
    std::vector<int>     fwd_upto;       // per read port; Order::program only
    std::vector<MemPort> ports;          // real ports, in port order (phantoms dropped)
    // Whole-array support (the `update` bus is driven): one update/read_all bus
    // instead of N per-entry ports. registered when a clock is present.
    hhds::Pin_class      update, update_enable, init, reset, clock;
    bool                 has_read_all = false;
    bool                 is_whole() const { return !update.is_invalid(); }
    bool                 registered() const { return !clock.is_invalid(); }
  };
  std::vector<Mem> mems;
  for (auto node : g->fast_class()) {
    if (type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    Mem m;
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
        m.fwd = Dlop::clone(hydrate_const(e.driver));
      } else if (pn == "undef") {
        m.undef = Dlop::clone(hydrate_const(e.driver));
      } else if (pn == "update") {
        m.update = e.driver;
      } else if (pn == "update_enable") {  // MUST precede ends_with("enable") below
        m.update_enable = e.driver;
      } else if (pn == "reset") {
        m.reset = e.driver;
      } else if (pn == "init") {
        m.init = e.driver;  // whole-array reset-value bus (runtime); plain mem: comptime, still assumed 0
      } else if (pn == "wensize") {
        m.wensize = static_cast<int>(hydrate_const(e.driver).to_just_i64());
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
    // Row stride of the `fwd` matrix: the write-port count `wridx` ranks over
    // (tolg mints every write port with addr+din+enable, so this matches the
    // n_wr the producer laid the matrix out with).
    m.n_wr   = wr;
    m.n_rd   = rd;
    m.member = cpp_id(default_instance_name(node));
    if (m.wensize < 1 || m.bits % m.wensize != 0) {
      livehd::diag::err("inou.cgen.sim", "mem-wensize-not-a-divisor", "unsupported")
          .msg("memory `{}` has wensize={}, which does not split bits={} into equal lanes", m.member, m.wensize, m.bits)
          .hint("the write enable is a per-lane vector; a lane width of bits/wensize must be an integer")
          .fatal();
      m.wensize = 1;
    }

    // ---- classify the `fwd`/`undef` matrices into one of the four modes ----
    // Bit (r*n_wr + w). Both are prefix rows by construction, so a row reduces
    // to a count; a NON-prefix row cannot come from an `ordering` attribute
    // (only the deprecated numeric `fwd=` escape hatch could produce one), and
    // guessing a mode for it would silently simulate a different memory.
    auto row_prefix = [&](const spool_ptr<Dlop>& mat, int r) -> int {
      if (!mat) {
        return 0;
      }
      int pre = 0;
      while (pre < m.n_wr && mat->bit_test(r * m.n_wr + pre)) {
        ++pre;
      }
      for (int w = pre; w < m.n_wr; ++w) {
        if (mat->bit_test(r * m.n_wr + w)) {
          return -1;  // a hole: not a prefix
        }
      }
      return pre;
    };
    if (!m.registered()) {
      // A `mut`/`const` array (type=2) has no clock: tolg lowers it
      // writes-before-reads, so every write is visible to every read and the
      // matrix is unused. That is exactly ordering="fwd".
      m.order     = Mem::Order::fwd;
      m.n_user_wr = m.n_wr;
    } else {
      std::vector<int> fwd_upto(static_cast<size_t>(m.n_rd), 0);
      int              max_fwd = 0, max_undef = 0;
      bool             fwd_uniform = true;
      bool             bad_row     = false;
      for (int r = 0; r < m.n_rd; ++r) {
        const int f = row_prefix(m.fwd, r);
        const int u = row_prefix(m.undef, r);
        if (f < 0 || u < 0) {
          bad_row = true;
          break;
        }
        fwd_upto[static_cast<size_t>(r)] = f;
        max_fwd                          = std::max(max_fwd, f);
        max_undef                        = std::max(max_undef, u);
        if (r > 0 && f != fwd_upto[0]) {
          fwd_uniform = false;
        }
      }
      if (bad_row) {
        livehd::diag::err("inou.cgen.sim", "mem-ordering-matrix-not-a-prefix", "unsupported")
            .msg("memory `{}` has a `fwd`/`undef` row that is not a prefix over its {} write ports", m.member, m.n_wr)
            .hint(
                "every `ordering` value (none/old/program/fwd) produces a prefix row; a hole means the matrix came "
                "from the deprecated numeric `fwd=` attribute, which cgen.sim cannot map to a hlop::Memory_* type -- "
                "use `ordering=` instead")
            .fatal();
      }
      // `fwd` and `undef` are MUTUALLY EXCLUSIVE per (read,write) pair, and
      // every `ordering` value drives exactly one of the two matrices. Both
      // non-empty means the matrices were not built from an `ordering` attr,
      // and one of the two meanings would be silently dropped below.
      if (max_fwd > 0 && max_undef > 0) {
        livehd::diag::err("inou.cgen.sim", "mem-fwd-and-undef-both-set", "unsupported")
            .msg("memory `{}` drives BOTH the `fwd` and the `undef` matrix", m.member)
            .hint(
                "the two are mutually exclusive per (read,write) pair -- each `ordering` value sets one of them, so "
                "a cell with both did not come from an `ordering` attribute and has no hlop::Memory_* equivalent")
            .fatal();
      }
      // The reset/restore write ports are the tail no row ever names, so the
      // widest prefix IS the user write-port count.
      if (max_undef > 0) {
        m.order     = Mem::Order::none;
        m.n_user_wr = max_undef;
      } else if (max_fwd == 0) {
        m.order     = Mem::Order::old;
        m.n_user_wr = m.n_wr;
      } else if (fwd_uniform) {
        m.order     = Mem::Order::fwd;
        m.n_user_wr = max_fwd;
      } else {
        m.order     = Mem::Order::program;
        m.n_user_wr = max_fwd;
        m.fwd_upto  = std::move(fwd_upto);
      }
    }
    mems.push_back(std::move(m));
  }

  // ---- sub-module instances (Ntype_op::Sub -> nested struct member) ----
  struct Sub {
    hhds::Node_class node;
    std::string      inst;           // member name
    std::string      callee_struct;  // cpp_id of the callee module name
    bool             negedge_only;   // state advances in the fall half, after the parent's rise
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
    if (cname.empty() || cname == livehd::graph_util::lgassert_module_name || cname == livehd::graph_util::fproperty_module_name) {
      // Recognized primitives, not real sub-graphs: instantiating one emitted
      // an #include for a module with no body and broke the sim host-compile
      // (any design with an undischarged assert). Skipped like cgen_verilog's
      // special-case; EXECUTING the runtime check in sim is the pending
      // runtime-fallback item (2f-formal / 4b slop emission).
      continue;
    }
    bool child_posedge = false;
    bool child_negedge = false;
    if (auto sg = node.get_subnode_graph()) {
      for (auto sn : sg->fast_class()) {
        if (!is_type_flop(sn)) {
          continue;
        }
        bool pos = true;
        if (auto pc = get_driver(find_sink_pin(sn, "posclk")); !pc.is_invalid() && is_const_pin(pc)) {
          pos = !hydrate_const(pc).is_known_false();
        }
        child_posedge |= pos;
        child_negedge |= !pos;
      }
    }
    subs.push_back({node, cpp_id(default_instance_name(node)), cpp_id(cname), child_negedge && !child_posedge});
    auto hdr = absl::StrCat(sim_file_stem(cname), ".hpp");
    if (std::find(sub_includes.begin(), sub_includes.end(), hdr) == sub_includes.end()) {
      sub_includes.push_back(hdr);
    }
  }

  for (const auto& h : sub_includes) {
    hout->append(absl::StrCat("#include \"", h, "\"\n"));
  }
  // The hlop::Memory_* type a memory lowers to. ordering="program" additionally
  // needs its per-read-port forwarding prefix as a non-type template argument,
  // emitted as a namespace-scope constexpr array just above the struct.
  auto mem_prefix_name = [](const Mem& m) { return absl::StrCat("__", m.member, "_fwd_upto"); };
  auto mem_type        = [&](const Mem& m) -> std::string {
    const std::string common = absl::
        StrCat("Slop<", m.bits, ">, ", m.bits, ", ", m.size, ", ", m.n_rd, ", ", m.n_wr, ", ", m.n_user_wr, ", ", m.wensize);
    switch (m.order) {
      case Mem::Order::fwd    : return absl::StrCat("hlop::Memory_fwd<", common, ">");
      case Mem::Order::none   : return absl::StrCat("hlop::Memory_none<", common, ">");
      case Mem::Order::program: return absl::StrCat("hlop::Memory_program<", common, ", ", mem_prefix_name(m), ">");
      case Mem::Order::old    : break;
    }
    return absl::StrCat("hlop::Memory_old<", common, ">");
  };
  // The write enable a port stages with. The Memory cell's `enable` (pid 4) IS
  // the lane mask: one bit at wensize==1, a wensize-bit vector otherwise. An
  // absent enable pin means every lane writes unconditionally.
  auto emit_wen = [&](const Mem& m, const MemPort& wp) -> std::string {
    if (wp.enable.is_invalid()) {
      return absl::StrCat("Slop<", m.wensize, ">::create_integer(-1)");
    }
    return operand(wp.enable, m.wensize);
  };
  for (const auto& m : mems) {
    if (m.order != Mem::Order::program) {
      continue;
    }
    hout->append(absl::StrCat("inline constexpr std::array<uint16_t, ", m.n_rd, "> ", mem_prefix_name(m), "{"));
    for (int r = 0; r < m.n_rd; ++r) {
      hout->append(absl::StrCat(r ? ", " : "", m.fwd_upto[static_cast<size_t>(r)]));
    }
    hout->append("};  // ordering=\"program\": writes preceding each read port\n");
  }

  // Does this module have a FALL half at all? Only then is `eval_negedge`
  // emitted and called — dino has none, so it keeps exactly one pass per tick.
  bool has_fall = false;
  for (const auto& f : flops) {
    if (!f.is_latch && !f.posedge) {
      has_fall = true;
    }
  }
  for (const auto& s : subs) {
    if (s.negedge_only) {
      has_fall = true;
    }
  }

  hout->append("struct ", mod, " {\n");
  // Each state element carries its Q and, alongside it, the D it will take at
  // the next edge (`_din`) plus the boolean saying whether that edge fires for
  // it (`_cen`). Both are DERIVED: the settle recomputes them from Q and the
  // inputs every period, so they are deliberately absent from
  // dump_state/load_state/design_hash (reset_cycle()'s trailing settle
  // re-derives them after a checkpoint load).
  //
  // They are members rather than settle-locals because the settle and the
  // commit are separate methods: the commit runs with NO combinational value in
  // scope, so anything it needs — the next value, the ICG enable, a secondary
  // clock's edge test — has to have been computed and parked by the settle.
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      hout->append("  Slop<", std::to_string(f.bits), "> ", s, "{};  // pipe stage\n");
      hout->append("  Slop<", std::to_string(f.bits), "> ", s, "_din{};  // ...its next value\n");
    }
    hout->append("  Slop<", std::to_string(f.bits), "> ", f.member, "{};  // flop\n");
    hout->append("  Slop<", std::to_string(f.bits), "> ", f.member, "_din{};  // ...its next value\n");
    if (!f.clock_guards.empty() || !f.sec_clock.is_invalid()) {
      hout->append("  bool ", f.member, "_cen = true;  // ...and whether its edge fires this period\n");
    }
  }
  // Previous-value bit per SECONDARY clock net (2f-latch M6), deduped: every
  // flop on the same net shares one, which is also what makes them commit
  // together on that net's edge.
  {
    absl::flat_hash_set<std::string> emitted;
    for (const auto& f : flops) {
      if (f.prev_member.empty() || !emitted.insert(f.prev_member).second) {
        continue;
      }
      hout->append("  bool ", f.prev_member, "{false};  // previous level of a secondary clock net\n");
    }
  }
  for (const auto& m : mems) {
    hout->append(absl::StrCat("  ",
                              mem_type(m),
                              " ",
                              m.member,
                              "{};  // memory (ordering=",
                              m.order == Mem::Order::fwd       ? "fwd"
                              : m.order == Mem::Order::none    ? "none"
                              : m.order == Mem::Order::program ? "program"
                                                               : "old",
                              ")\n"));
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
  // A tuple/struct-packed port flattens to dotted leaves (`io_in.pc`), which is
  // not a legal C++ member name. Rather than mangle the dot away and make every
  // consumer re-derive the grouping, MIRROR the tuple as a nested struct:
  //
  //     struct In {
  //       struct { Slop<32> instruction{}; Slop<1> isValid{}; } io_in{};
  //     };
  //
  // so the RTL path and the C++ path are the same text — `in.io_in.instruction`
  // — and a testbench addressing `acc.io_in.instruction` needs no translation
  // (inou/prp/prp_sim.cpp). Leaves keep declaration order, which is packed
  // order (first member = most significant), so the aggregate can also be
  // read/written as one value. A port with no dot emits exactly as before.
  auto emit_io_block = [&](bool want_input) {
    std::string open_group;  // tuple prefix currently open, "" = none
    for (const auto& io : ios) {
      if (io.is_input != want_input) {
        continue;
      }
      const auto  dot   = io.raw.find('.');
      std::string group = dot == std::string::npos ? std::string{} : io.raw.substr(0, dot);
      if (group != open_group) {
        if (!open_group.empty()) {
          hout->append("    } ", open_group, "{};\n");
        }
        if (!group.empty()) {
          hout->append("    struct {\n");
        }
        open_group = group;
      }
      if (group.empty()) {
        hout->append("    Slop<", std::to_string(io.bits), "> ", io.field, "{};\n");
      } else {
        // nested leaf: the segment after the group prefix (io.field is
        // "<group>.<leaf>", so take what follows its dot)
        hout->append("      Slop<", std::to_string(io.bits), "> ", io.field.substr(io.field.find('.') + 1), "{};\n");
      }
    }
    if (!open_group.empty()) {
      hout->append("    } ", open_group, "{};\n");
    }
  };
  hout->append("  struct In {\n");
  emit_io_block(true);
  hout->append("  };\n  struct Out {\n");
  emit_io_block(false);
  hout->append("  };\n");

  // Persistent input latch. The testbench writes inputs through a ref
  // (`acc.x = v` -> __in.x) and advances with `step()`; internal registers are
  // plain members (`acc.total`) and outputs live in __out below. Keeping __in in
  // the instance (not the driver) means a state copy / checkpoint captures the
  // driven inputs too.
  hout->append("  In __in{};\n");
  // 2f-sim B: the outputs this instance last computed DURING the period --
  // recorded by cycle() before it commits, so it is the value the output drove
  // while the period ran, computed from the state ENTERING the cycle. That is
  // the contract the query engine publishes (09b-simquery "Outputs and state are
  // one commit apart", `"sampled":"during_period"`), so it must stay pre-commit;
  // __out below is the post-commit twin and the two are deliberately different.
  // DERIVED state: deliberately absent from dump_state/load_state and from
  // design_hash — it is recomputed by the next cycle() and must not change the
  // checkpoint layout or its compatibility hash.
  hout->append("  Out __last_out{};\n");
  // The SETTLED outputs of the CURRENT committed state: what the design drives
  // once this period's edge has been taken. __settle() recomputes it from the
  // committed members, and cycle() calls __settle() on its way out, so a
  // testbench read after `step` observes the post-edge value -- exactly what the
  // old peek(__in) returned, but as a plain member a `sigref` can bind ONCE and
  // hold for the whole run instead of an O(total design state) snapshot/restore
  // per read. Also DERIVED: out of dump_state/load_state/design_hash for the
  // same reason as __last_out (reset_cycle() re-settles it after a load).
  hout->append("  Out __out{};\n");

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
      vsig.push_back({absl::StrCat("__vv", k),
                      (b > 1 ? absl::StrCat(nm, "[", b - 1, ":0]") : nm),
                      acc,
                      absl::StrCat("__vs", k),
                      absl::StrCat("__vp", k),
                      b,
                      pe,
                      is_in});
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
  // The two halves of a tick, each callable on its own so a PARENT can order a
  // child's phases (a negedge consumer in the parent needs the child's fall to
  // have happened, but not its rise a second time). `eval_negedge` is emitted
  // only for a module that has negedge state -- dino has none, so it runs one
  // pass per tick exactly as before.
  hout->append("  void eval_posedge(In in);  // rise: comb from pre-edge state, then commit posedge state\n");
  if (has_fall) {
    hout->append("  void eval_negedge(In in);  // fall: the negedge cones, re-read post-rise, then commit them\n");
  }
  // One clock edge is SETTLE -> COMMIT -> SETTLE, and cycle() is the whole of
  // it: its body settles the comb cone from the pre-edge state (that is what
  // computes both `o`/__last_out and every next-state), commits, and then calls
  // __settle(in) to refresh __out from the just-committed state.
  //
  // The trailing settle is what makes a `sigref` possible, and it must NOT be
  // hoisted to the front of the next cycle(): committing a next-state that was
  // settled against the PREVIOUS period's `in` would make every driven input
  // land one cycle late (`acc.reset = clock < 2` would reset cycles 1-2, not
  // 0-1). Both settles are load-bearing and they settle against different state.
  hout->append("  Out cycle(In in) {  // one clock period\n");
  hout->append("    eval_posedge(in);\n");
  if (has_fall) {
    hout->append("    eval_negedge(in);\n");
  }
  hout->append("    __settle(in);  // refresh __out so a bound sigref reads the post-edge value\n");
  hout->append("    return __last_out;  // the during-period outputs the rise recorded\n");
  hout->append("  }\n");
  hout->append("  void step() { cycle(__in); }  // drive __in, then advance one clock\n");
  hout->append(
      "  void __settle(In in);  // recompute __out from the CURRENT committed state; no commit, no VCD, no state change\n");
  // Editable, name-keyed checkpoint (sim_checkpoint_debug_plan): flops/regs ->
  // the `_r` map keyed by the hierarchical `_p`+member name (pyrope literal),
  // memories -> one `<_dir>/<_p><member>.hex` file each, sub-instances recursed.
  // design_hash folds member names+widths (cross-version warn, never reject).
  hout->append(
      "  void dump_state(const std::string& _p, std::map<std::string, std::string>& _r, const std::string& _dir) const;\n");
  hout->append(
      "  void load_state(const std::string& _p, const std::map<std::string, std::string>& _r, const std::string& _dir);\n");
  hout->append("  std::uint64_t design_hash() const;\n");
  // Observability (lhd sim --list-signals / --probe / --break-when): every scalar
  // signal (flop / pipe stage / sync-read reg / input) by hierarchical name.
  hout->append("  void describe_signals(const std::string& _p, std::vector<hlop::ckpt::Signal>& _v) const;\n");
  hout->append("  void probe_signals(const std::string& _p, std::map<std::string, long>& _m) const;\n");
  // 2f-sim B: the LOSSLESS observation surface the query engine reads. Same walk
  // as probe_signals, but (a) values are full-width canonical hex instead of
  // to_i64_low()'s silent low-64 truncation, and (b) module OUTPUTS are included,
  // served from the last computed Out (recorded at the end of cycle(), so an
  // output read costs nothing — never the O(total state) snapshot/restore of a
  // peek()). `observe_mem` reads ONE committed memory word by member name.
  hout->append("  void observe_signals(const std::string& _p, std::map<std::string, std::string>& _m) const;\n");
  hout->append("  bool observe_mem(const std::string& _n, long _i, std::string& _o) const;\n");
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
    const bool  has_clock = !clk_field.empty() || !flops.empty();
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
          fout->append(
              absl::StrCat("      if (!__root && !", v.snap, ".same_repr(", v.prev, ")) __w->change(", v.var, ", \"", x, "\");\n"));
        } else {
          fout->append(absl::StrCat("      if (!", v.snap, ".same_repr(", v.prev, ")) __w->change(", v.var, ", \"", x, "\");\n"));
        }
      }
      fout->append("    } else {\n");
      for (const auto& v : vsig) {
        if (v.posedge) {
          continue;
        }
        const char* x = v.bits > 1 ? "bx" : "x";
        fout->append(absl::StrCat("      if (!", v.snap, ".same_repr(", v.prev, ")) __w->change(", v.var, ", \"", x, "\");\n"));
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
        fout->append(absl::StrCat("      if (!__root) { __w->change(",
                                  v.var,
                                  ", vcd::to_vcd_bits(",
                                  v.snap,
                                  ", ",
                                  v.bits,
                                  "));",
                                  upd,
                                  " }\n"));
      } else {
        fout->append(absl::StrCat("      __w->change(", v.var, ", vcd::to_vcd_bits(", v.snap, ", ", v.bits, "));", upd, "\n"));
      }
    }
    fout->append("    } else {\n");
    for (const auto& v : vsig) {
      if (v.posedge) {
        continue;
      }
      std::string upd = vcd_fakedelay ? absl::StrCat(" ", v.prev, " = ", v.snap, ";") : std::string{};
      fout->append(absl::StrCat("      __w->change(", v.var, ", vcd::to_vcd_bits(", v.snap, ", ", v.bits, "));", upd, "\n"));
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
  {
    absl::flat_hash_set<std::string> emitted;
    for (const auto& f : flops) {
      if (!f.prev_member.empty() && emitted.insert(f.prev_member).second) {
        fout->append("    ", f.prev_member, " = false;\n");
      }
    }
  }
  for (const auto& m : mems) {
    // Power-on contents: apply the comptime `init` bus (ROM / `const`/`mut` array
    // reset value) instead of zero-filling, so first-cycle reads see the real
    // contents. A runtime/absent init keeps the zero-fill.
    if (!m.init.is_invalid() && is_const_pin(m.init)) {
      fout->append(absl::StrCat("    ", m.member, ".apply_update(", operand(m.init, m.bits * m.size), ");\n"));
    } else {
      fout->append(absl::StrCat("    ", m.member, ".fill(Slop<", m.bits, ">::create_integer(0));\n"));
    }
    fout->append(absl::StrCat("    ", m.member, ".clear_pending();\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("    ", m.member, "_q", p.rdidx, " = Slop<", m.bits, ">::create_integer(0);\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append("    ", s.inst, ".reset_cycle();\n");
  }
  // Leave the design SETTLED at t=0. Every read a testbench makes before its
  // first `step` -- and the docs use that shape (05-assert.md's `assert(x == 0)`
  // ahead of any step) -- goes through __out, which would otherwise be
  // default-zero rather than the outputs the reset state actually drives. Also
  // covers load_state(): __out is derived, absent from the checkpoint, and this
  // is what re-derives it after a restore.
  fout->append("    __settle(__in);\n");
  fout->append("}\n");

  // ---- cycle() and __settle(): ONE emitter, run twice ----
  // A clock period is settle -> commit -> settle. The body below IS the first
  // settle plus the commit (it binds flop Q to the pre-edge member, walks the
  // comb cone, then commits); running the SAME emitter with `settle` set drops
  // every state-mutating statement and retargets the outputs at the `__out`
  // member, which is the trailing settle. cycle() calls it as its last act.
  //
  // Emitting one body twice (rather than a hand-written second walker) is what
  // keeps the two passes from drifting: the ordering rules here -- the false-loop
  // pre-binds, the memory read/stage interleave, the on-demand operand cones --
  // are subtle enough that a parallel implementation would rot. The settle pass
  // is also restricted to the OUTPUT cone (`settle_cone` below), so it emits far
  // less than the full body: next-state logic is exactly what it does not need.
  //
  // Reset is an ordinary input the testbench drives into `in` (via step()/__in);
  // no special override here. The flop next-state logic reads it through the
  // normal reset_pin path below.
  //
  // Nodes the settle pass must emit: everything backward-reachable from an
  // output driver, stopping at state elements (a flop Q is already a member).
  // Memory and Sub nodes stay IN the set -- a memory read and a sub-instance
  // output are emitted by their own node, not by ensure_ready(), so dropping
  // them would leave the output cone unbound and operand() would quietly
  // substitute a constant 0.
  //
  // Set while emitting __settle(): a sub-instance must be SETTLED (comb only),
  // never cycled, or the trailing settle of every period would advance the whole
  // sub-tree a second time.
  bool                                   settle_mode = false;
  // The three emissions of one clock period. Rise and Fall are the two halves of
  // the tick; Settle is the trailing comb refresh that keeps `__out` (and any
  // `sigref` bound to it) current. A module with no negedge state emits no Fall
  // at all, so dino stays exactly one pass per tick.
  enum class Pass { Rise, Fall, Settle };
  absl::flat_hash_set<hhds::Class_index> settle_cone;
  {
    std::vector<hhds::Pin_class> work;
    for (const auto& io : ios) {
      if (io.is_input) {
        continue;
      }
      auto spin = g->get_output_pin(io.raw);
      if (!spin.is_invalid()) {
        work.push_back(get_driver(spin));
      }
    }
    absl::flat_hash_set<pin_key_t> seen;
    while (!work.empty()) {
      auto p = work.back();
      work.pop_back();
      if (p.is_invalid() || is_const_pin(p) || !seen.insert(p.get_class_index()).second) {
        continue;
      }
      auto n = p.get_master_node();
      settle_cone.insert(n.get_class_index());
      if (is_type_register(n)) {
        continue;  // Q is a committed member; its din cone is next-state, not needed
      }
      for (const auto& e : n.inp_edges()) {
        work.push_back(e.driver);
      }
    }
  }

  // Every operand port a state element's next-value/guard emission reads.
  static constexpr std::array<const char*, 4> flop_operand_ports{"din", "enable", "reset_pin", "initial"};

  // Nodes the FALL pass must emit: everything backward-reachable from a negedge
  // element's operands, stopping at state (whose Q is the just-committed
  // member). Same shape as `settle_cone` — it is what keeps the fall from
  // re-emitting the whole body when it only needs the negedge cones.
  absl::flat_hash_set<hhds::Class_index> fall_cone;
  if (has_fall) {
    std::vector<hhds::Pin_class> work;
    for (const auto& f : flops) {
      if (f.posedge) {
        continue;
      }
      for (const auto* port : flop_operand_ports) {
        work.push_back(get_driver(find_sink_pin(f.node, port)));
      }
    }
    absl::flat_hash_set<pin_key_t> seen;
    while (!work.empty()) {
      auto pp = work.back();
      work.pop_back();
      if (pp.is_invalid() || is_const_pin(pp) || !seen.insert(pp.get_class_index()).second) {
        continue;
      }
      auto n = pp.get_master_node();
      fall_cone.insert(n.get_class_index());
      if (is_type_register(n)) {
        continue;
      }
      for (const auto& e : n.inp_edges()) {
        work.push_back(e.driver);
      }
    }
    for (const auto& s : subs) {
      if (s.negedge_only) {
        fall_cone.insert(s.node.get_class_index());
      }
    }
  }

  auto emit_period_body = [&](Pass pass_) -> bool {
  const bool settle = pass_ == Pass::Settle;
  const bool fall   = pass_ == Pass::Fall;
  // `settle_mode` means "do NOT advance a child" -- true for the fall as well as
  // the settle. A sub-instance already ran its whole `cycle()` (both halves plus
  // its own trailing settle) during this module's RISE, so all the fall needs
  // from it is its outputs re-evaluated against the inputs the rise just
  // committed: a comb re-settle, not a second advance. That is precisely what
  // the old `negedge-through-instance` refusal existed to prevent, and why it
  // can now be lifted -- before the settle/commit split there was no way to ask
  // a child for its outputs without also stepping it.
  settle_mode       = settle || fall;
  if (pass_ != Pass::Rise) {
    // Fresh binding environment. The settle reads the COMMITTED members rather
    // than the temporaries the rise left behind; the fall likewise starts from
    // the state the rise just committed, which is what makes its half-cycle
    // handover fall out of an ordinary walk instead of the invalidate/re-ready
    // dance a single monolithic body needed.
    pin2var.clear();
    canonical_.clear();
    fout->append("void ", mod, "::", settle ? "__settle" : "eval_negedge", "(In in) {\n");
  } else {
    fout->append("void ", mod, "::eval_posedge(In in) {\n");
  }
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
  absl::flat_hash_set<hhds::Class_index>                                 moore_deferred;
  std::vector<const Sub*>                                                deferred_moore;
  absl::flat_hash_set<hhds::Class_index>                                 fall_deferred;
  std::vector<const Sub*>                                                deferred_fall;
  // Per-OUTPUT-cone deferral (Stage-2): a MEALY callee (some comb in->out path,
  // so callee_is_moore declines the whole-callee treatment) whose FED-BACK
  // outputs are all pure state reads. Pre-binding just those outputs via peek()
  // un-cycles the schedule; the atomic cycle(in) call then orders normally (its
  // input cone reads only pre-bound values, emitted on demand below). The
  // CSR/NewCSR::MipModule and DivUnit/SRT16 residual class.
  absl::flat_hash_set<hhds::Class_index>                                 mealy_prebound;
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
    if (fed_back.empty() && !s.negedge_only) {
      continue;
    }
    const bool                    moore = s.negedge_only || callee_is_moore(s.node.get_subnode_graph(), s.node.get_subnode_io());
    absl::flat_hash_set<uint32_t> state_only;
    if (s.negedge_only) {
      fall_deferred.insert(s.node.get_class_index());
    } else if (moore) {
      moore_deferred.insert(s.node.get_class_index());
    } else {
      state_only          = callee_state_only_outputs(s.node.get_subnode_graph(), s.node.get_subnode_io());
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
    // The child's outputs for its CURRENT committed state are exactly what its
    // own trailing settle left in `__out` -- which is what the old
    // `peek({})` recomputed, at the price of snapshotting and restoring the
    // whole child subtree. In the CYCLE pass this must be COPIED: the deferred
    // `<inst>.cycle(...)` emitted after the walk refreshes `__out`, and these
    // bindings have to keep reading the pre-call values. In the SETTLE pass
    // nothing advances the child, so bind straight at the member; the only
    // writer there is a Mealy sub's own `__settle`, and every pin bound here is
    // a pure state read, which a settle does not change.
    const std::string pre_base = settle ? absl::StrCat(s.inst, ".__out.") : absl::StrCat(s.inst, "__pre.");
    if (!settle) {
      fout->append(absl::StrCat("    auto ",
                                s.inst,
                                "__pre = ",
                                s.inst,
                                s.negedge_only ? ".__out;  // negedge-only sub: advance after the parent's rise\n"
                                : moore        ? ".__out;  // Moore sub: outputs from current state (call deferred)\n"
                                               : ".__out;  // Mealy sub: state-only outputs pre-bound (call ordered normally)\n"));
    }
    // Bind only pins that EXIST (enumerated via the instance's out-edges): a
    // declared-but-unread output has no created pin, and hhds' name lookup
    // asserts on it (find_pin "requested pin was not created" -- the DataPath
    // family crash). The lazy out_edges view is iterated read-only.
    auto                                       sio = s.node.get_subnode_io();
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
        // cpp_port_path, never cpp_id: a tuple leaf is `io_data.instruction` in
        // the callee's Out struct (a NESTED struct, see emit_io_block), so
        // mangling the dot to `_` here named a member that does not exist —
        // `no member named 'io_data_instruction' in 'StageReg_StageReg::Out'`.
        pin2var[opin.get_class_index()] = absl::StrCat(pre_base, cpp_port_path(it->second));
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
  auto                                   emit_sub_call = [&](auto&& ensure_fn, const hhds::Node_class& node) -> void {
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
                  s.inst,
                  gname,
                  s.callee_struct,
                  d.name)
              .hint(
                  "a sub-instance is simulated atomically (all inputs -> all outputs), so an output that feeds back "
                  "into one of its inputs forms a cycle the single-pass schedule cannot break; restructure so the "
                  "cone feeding this input is computed before the call, split the sub by output cone, or flatten "
                  "this instance for sim")
              .emit();
          cycle_reported_ = true;
        }
        fout->append(absl::StrCat("    ", s.inst, "__i.", cpp_port_path(d.name), " = ", operand(drv, wb), ";\n"));
      }
      // In settle mode the child is re-settled against the inputs just rebuilt
      // from the parent's COMMITTED state, and its outputs are read from its
      // own __out member. cycle() would advance it a second time in the period.
      if (settle_mode) {
        fout->append(absl::StrCat("    ", s.inst, ".__settle(", s.inst, "__i);\n"));
      } else {
        fout->append(absl::StrCat("    auto ", s.inst, "__o = ", s.inst, ".cycle(", s.inst, "__i);\n"));
      }
      const std::string sub_out = settle_mode ? absl::StrCat(s.inst, ".__out.") : absl::StrCat(s.inst, "__o.");
      for (const auto& d : sio->get_output_pin_decls()) {
        auto opin = find_driver_pin(node, d.name);
        if (!opin.is_invalid()) {
          pin2var[opin.get_class_index()] = absl::StrCat(sub_out, cpp_port_path(d.name));
        }
      }
      break;
    }
  };

  absl::flat_hash_set<pin_key_t> prefetch_seen;
  auto                           ensure_ready_impl = [&](auto&& self, const hhds::Pin_class& drv) -> void {
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
    fout->append(
        absl::StrCat("    Slop<", wb, "> ", var, " = ", node_expr(n, wb), ";  // ", op_name(nop), " (mem-operand prefetch)\n"));
    pin2var[dp.get_class_index()] = var;
    canonical_.insert(dp.get_class_index());   // node_expr result: canonical at its declared width
  };
  auto ensure_ready = [&](const hhds::Pin_class& drv) { ensure_ready_impl(ensure_ready_impl, drv); };

  // combinational SSA bindings, in dependency order
  for (auto node : g->forward_class()) {
    auto op = type_op_of(node);
    // The settle pass only needs the OUTPUT cone. Everything else in this walk
    // is next-state logic -- on a real design the large majority of it -- so
    // this filter is what keeps the second emission from doubling the generated
    // C++ (and with it the host clang++ time, which already dominates `lhd sim`).
    if (settle && !settle_cone.contains(node.get_class_index())) {
      continue;
    }
    // Same idea for the fall half: it needs only the cones feeding negedge
    // state, re-read from the members the rise just committed.
    if (fall && !fall_cone.contains(node.get_class_index())) {
      continue;
    }
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
          fout->append(absl::StrCat("    ", m.member, ".apply_update(", operand(m.update, m.bits * m.size), ");\n"));
        } else if (!m.registered() && !m.init.is_invalid() && is_const_pin(m.init)) {
          // Combinational per-port array (`mut t:[..] = <const>`): re-seed the
          // whole array to its comptime init at the START of every cycle. Writes
          // are forwarded to the same-cycle read below and also committed to
          // `member` at the edge, but a comb array has no state, so without this
          // re-seed a per-port write would leak into later cycles' reads.
          fout->append(absl::StrCat("    ", m.member, ".apply_update(", operand(m.init, m.bits * m.size), ");\n"));
        }
        // Stage the write ports a read may OBSERVE, INTERLEAVED with the reads
        // in program order: before read port r, stage exactly the write ports r
        // resolves against (hlop::Memory_*::read consults the staged slots per
        // its ordering mode). Staging a write any earlier would make the emitted
        // C++ depend on its data, and under ordering="program" the canonical
        // shape `o1 = mem[a]; mem[b] = f(o1); o4 = mem[c]` -- a legal
        // read-modify-write -- would then look like a combinational cycle.
        int  staged        = 0;  // write ports staged so far, by ordinal
        auto stage_through = [&](int upto) {
          // A settle runs AFTER the edge, so a REGISTERED memory's writes for
          // this period are already in the array (mem.tick() committed them);
          // re-staging them would duplicate the write-forwarding and leave
          // pending slots behind. Post-commit, reading the committed array
          // already reflects the write -- also the documented remote-reader rule
          // (08-memories.md: "remote readers always see the last committed
          // state").
          //
          // A COMBINATIONAL array is the opposite: it has no clock and no
          // committed state, its contents ARE the writes made during the pass
          // (which is why the walk re-seeds it to its init above), so the settle
          // must stage them exactly as the cycle pass does. Skipping them left
          // every read returning the re-seed value -- caught by
          // prp-simeq-packed_assign, whose `mut g:[4]u4` is filled by four
          // per-port writes and then runtime-indexed.
          if (settle && m.registered()) {
            staged = upto;
            return;
          }
          if (upto <= staged) {
            return;
          }
          for (const auto& wp : m.ports) {
            if (wp.rd || wp.addr.is_invalid() || wp.din.is_invalid() || wp.wridx < staged || wp.wridx >= upto) {
              continue;
            }
            if (!wp.enable.is_invalid() && is_const_pin(wp.enable) && hydrate_const(wp.enable).is_known_false()) {
              continue;  // never fires
            }
            ensure_ready(wp.addr);
            if (!wp.enable.is_invalid()) {
              ensure_ready(wp.enable);
            }
            // ordering="none" never reads the staged DATA -- the collision value
            // is undefined -- so only the address and enable are needed to
            // detect it. Skipping the din keeps a read-modify-write on a "none"
            // memory from becoming a false cycle too.
            std::string din = absl::StrCat("Slop<", m.bits, ">::create_integer(0)");
            if (m.order != Mem::Order::none) {
              ensure_ready(wp.din);
              din = operand(wp.din, m.bits);
            }
            fout->append(absl::StrCat("    ",
                                      m.member,
                                      ".stage_write<",
                                      wp.wridx,
                                      ">(",
                                      emit_wen(m, wp),
                                      ", ",
                                      operand(wp.addr, std::max(1, bits_of(wp.addr))),
                                      ", ",
                                      din,
                                      ");\n"));
          }
          staged = upto;
        };
        // How many write ports read port r resolves against. A sync (latency-1)
        // read resolves at the edge instead, so it stages nothing here.
        auto read_prefix = [&](int rdidx) -> int {
          if (m.type == 1) {
            return 0;
          }
          switch (m.order) {
            case Mem::Order::old    : return 0;
            case Mem::Order::fwd    :
            case Mem::Order::none   : return m.n_user_wr;
            case Mem::Order::program: return m.fwd_upto[static_cast<size_t>(rdidx)];
          }
          return 0;
        };
        for (const auto& p : m.ports) {
          if (!p.rd || p.addr.is_invalid()) {
            continue;
          }
          auto dout = node.create_driver_pin(static_cast<hhds::Port_id>(p.dout_pid));
          if (m.type == 1) {
            pin2var[dout.get_class_index()] = absl::StrCat(m.member, "_q", std::to_string(p.rdidx));
          } else {
            // Latency-0 read: the memory resolves its own ordering mode against
            // the writes staged above, exactly as the cgen_memory wrapper's
            // UNDEF-then-FWD-then-stored chain does (and at latency 1 the same
            // resolved value is what the read register latches, at the edge).
            stage_through(read_prefix(p.rdidx));  // program-order staging, see above
            ensure_ready(p.addr);                 // computed read address emitted before this early (loop_last) memory node
            int  ab  = std::max(1, bits_of(p.addr));
            auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
            fout->append(absl::StrCat("    Slop<",
                                      m.bits,
                                      "> ",
                                      var,
                                      " = ",
                                      m.member,
                                      ".read<",
                                      p.rdidx,
                                      ">(",
                                      operand(p.addr, ab),
                                      ");  // mem read\n"));
            pin2var[dout.get_class_index()] = var;
          }
        }
        // Async whole-array read: pack the current `member` into one bus.
        if (m.has_read_all) {
          auto ra  = node.create_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
          auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
          fout->append(absl::StrCat("    auto ", var, " = ", m.member, ".read_all();  // read_all\n"));
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
      if (fall_deferred.contains(node.get_class_index())) {
        for (const auto& s : subs) {
          if (s.node.get_class_index() == node.get_class_index()) {
            deferred_fall.push_back(&s);
            break;
          }
        }
        continue;
      }
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
    int  wb  = wbits_of(dpin);
    auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
    fout->append(absl::StrCat("    Slop<", wb, "> ", var, " = ", node_expr(node, wb), ";  // ", op_name(op), "\n"));
    pin2var[dpin.get_class_index()] = var;
    canonical_.insert(dpin.get_class_index());  // node_expr result: canonical at its declared width
  }

  // Deferred Moore-sub calls: every comb value is bound now; the child call
  // only advances the child's state (its outputs were pre-bound from __out).
  // Inputs are bound from the instance's EXISTING sink edges (an unconnected
  // declared input has no created pin -- hhds asserts on a name lookup -- and
  // the In{} zero-init already models it as 0).
  //
  // Nothing to do in the settle pass: it never advances a child, and the
  // pre-binding above already pointed these outputs straight at `<inst>.__out`.
  for (const auto* sp : (pass_ == Pass::Rise) ? deferred_moore : std::vector<const Sub*>{}) {
    const auto& s    = *sp;
    auto        dsio = s.node.get_subnode_io();
    if (!dsio) {
      continue;
    }
    absl::flat_hash_map<uint32_t, const void*>                 bound_pids;  // pid -> decl (dedupe)
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
      // cpp_port_path for the same reason as the __pre read above: In mirrors a
      // tuple port as a nested struct, so the leaf is `io_data.instruction`.
      fout->append(absl::StrCat("    ",
                                s.inst,
                                "__i.",
                                cpp_port_path(it->second.first),
                                " = ",
                                operand(e.driver, it->second.second),
                                ";\n"));
    }
    fout->append(absl::StrCat("    ", s.inst, ".cycle(", s.inst, "__i);  // deferred Moore-sub state advance\n"));
  }

  // Stage 0: if a combinational cycle survivor was hit anywhere above but the
  // precise Sub-input site did not already name it (a pure-cell comb loop with no
  // Sub on it), fail loudly with the best label we captured rather than emit a
  // silently-wrong sim that substitutes 0.
  if (cycle_unresolved_ && !cycle_reported_) {
    livehd::diag::err("inou.cgen.sim", "combinational-loop", "unsupported")
        .msg(
            "module `{}` has a combinational loop the single-pass sim schedule cannot order: {} was read before it "
            "could be computed",
            gname,
            cycle_first_label_.empty() ? std::string{"a value"} : cycle_first_label_)
        .hint(
            "inou.cgen.sim emits one sequential `cycle()` per module; a combinational cycle (a real loop, or a false "
            "loop through an atomic Sub instance) has no valid emission order")
        .emit();
    cycle_reported_ = true;
  }

  // flop next-state. Each stage/q: reset ? rstval : (enable ? value_in : hold),
  // mirroring cgen's always block. value_in = din for stage0, the previous
  // stage otherwise.
  //
  // TWO-PHASE TICK (todo/livehd/2f-latch M5). One tick is one clock PERIOD, and
  // a period contains a RISE then a FALL. Posedge state is evaluated here (from
  // pre-tick state) and committed below; NEGEDGE state is evaluated afterwards,
  // in the second phase, so it observes the POST-RISE value of anything the
  // posedge half just committed. That is the half-cycle transfer: a posedge
  // stage feeding a negedge stage on the same clock hands over WITHIN one
  // period, which the old single-phase commit turned into a full-cycle lag.
  //
  // A design with no negedge flop skips the fall re-evaluation; latches still
  // use the low/high window pair and memories remain in the rise phase.
  // Park the commit enable, for the same reason `_din` is parked: the ICG guard
  // operands and a secondary clock's level are combinational, and the commit
  // method cannot read them.
  //
  // The edge test compares against the PRE-TICK level, so EVERY flop's `_cen` --
  // negedge ones included -- is emitted in the RISE pass, before `prev_member`
  // is advanced. That is what preserves the rule both phases depend on: updating
  // the prev bit between the phases made every secondary negedge test compare
  // `cur` against itself and miss the edge permanently.
  auto emit_commit_enable = [&](const Flop& f) {
    if (f.clock_guards.empty() && f.sec_clock.is_invalid()) {
      return;  // ungated, on the reference clock: commits every tick
    }
    std::string cen;
    for (const auto& gp : f.clock_guards) {
      absl::StrAppend(&cen, cen.empty() ? "" : " && ", "(", operand(gp, 1), ").is_known_true()");
    }
    if (!f.sec_clock.is_invalid()) {
      const std::string cur = absl::StrCat("(", operand(f.sec_clock, 1), ").is_known_true()");
      absl::StrAppend(&cen,
                      cen.empty() ? "" : " && ",
                      f.posedge ? absl::StrCat("(", cur, " && !", f.prev_member, ")")
                                : absl::StrCat("(!", cur, " && ", f.prev_member, ")"));
    }
    fout->append("    ", f.member, "_cen = ", cen, ";\n");
  };

  // `with_cen == false` emits ONLY the next value and leaves `_cen` alone. The
  // fall pass needs exactly that: its `_din` is computed from the POST-rise
  // state, but its commit enable was already computed in the rise pass.
  auto emit_flop_next = [&](const Flop& f, bool latch_low = false, bool latch_high = false, bool with_cen = true) {
    auto din      = get_driver(find_sink_pin(f.node, "din"));
    auto rstp     = get_driver(find_sink_pin(f.node, "reset_pin"));
    auto initp    = get_driver(find_sink_pin(f.node, "initial"));
    auto enp      = get_driver(find_sink_pin(f.node, "enable"));
    bool negreset = false;
    if (auto np = get_driver(find_sink_pin(f.node, "negreset")); !np.is_invalid() && is_const_pin(np)) {
      negreset = !hydrate_const(np).is_known_false();
    }
    const std::string rstval = initp.is_invalid() ? absl::StrCat("Slop<", f.bits, ">::create_integer(0)") : operand(initp, f.bits);
    std::string       rtest;  // C++ bool: reset asserted (empty = no reset)
    bool              reset_always = false;
    if (!rstp.is_invalid()) {
      if (is_const_pin(rstp)) {
        reset_always = !hydrate_const(rstp).is_known_false();
      } else {
        rtest = absl::StrCat(operand(rstp, 1), negreset ? ".is_known_false()" : ".is_known_true()");
      }
    }
    std::string etest;  // C++ bool: write enabled (empty = always)
    if (!enp.is_invalid() && !is_const_pin(enp)) {
      // `neg_enable` = an active-LOW latch gate: transparent while enable == 0.
      // is_known_false() rather than !is_known_true(), so an UNKNOWN enable
      // fails closed (holds) on both polarities instead of writing on X.
      etest = absl::StrCat(operand(enp, 1), f.neg_enable ? ".is_known_false()" : ".is_known_true()");
    } else if (!enp.is_invalid() && f.neg_enable && is_const_pin(enp)) {
      // A CONSTANT active-low gate folds here: const 0 => permanently
      // transparent (write every tick), anything else => never writes.
      if (!hydrate_const(enp).is_known_false()) {
        etest = "false";
      }
    }
    auto next_of = [&](const std::string& value_in, const std::string& hold) -> std::string {
      if (reset_always) {
        return rstval;
      }
      std::string core = etest.empty() ? value_in : absl::StrCat("(", etest, " ? ", value_in, " : ", hold, ")");
      return rtest.empty() ? core : absl::StrCat("(", rtest, " ? ", rstval, " : ", core, ")");
    };
    // `_din` is a MEMBER (the commit runs in a separate method and has no
    // combinational value in scope, so the next value has to be parked here);
    // `_low`, the latch's low-window staging, stays a settle-local because it is
    // produced and consumed entirely inside this pass.
    auto              next_name = [&](const std::string& base) { return absl::StrCat(base, latch_low ? "_low" : "_din"); };
    auto              hold_name = [&](const std::string& base) { return latch_high ? absl::StrCat(base, "_low") : base; };
    const char*       decl      = latch_low ? "auto " : "";
    const std::string din_expr  = din.is_invalid() ? f.member : operand(din, f.bits);
    if (f.depth <= 1) {
      fout->append("    ", decl, next_name(f.member), " = ", next_of(din_expr, hold_name(f.member)), ";\n");
    } else {
      fout->append("    ", decl, next_name(f.stages[0]), " = ", next_of(din_expr, hold_name(f.stages[0])), ";\n");
      for (size_t i = 1; i < f.stages.size(); ++i) {
        fout->append("    ", decl, next_name(f.stages[i]), " = ", next_of(f.stages[i - 1], hold_name(f.stages[i])), ";\n");
      }
      fout->append("    ", decl, next_name(f.member), " = ", next_of(f.stages.back(), hold_name(f.member)), ";\n");
    }
    if (with_cen && !latch_low) {
      emit_commit_enable(f);
    }
  };

  // Every driver pin emit_flop_next() will dereference. PHASE 2 drops the
  // bindings of everything reachable from a posedge Q, so each of these cones
  // has to be re-readied there — readying only `din` left a latch-qualified
  // enable unbound and operand() substituted a silent constant 0 into the
  // commit guard. (`negreset` is absent: it is only ever read as a constant.)

  // Commit one state element's next value under its ICG gate and/or its
  // secondary clock's edge. BOTH phases use this: a gated or secondary-clocked
  // flop needs the same guard whichever edge it commits on.
  // Pure member moves: everything combinational this needs was parked by the
  // settle (see emit_flop_next). That is what lets the commit be its own method.
  //
  // ICG fold (2f-latch M5): a gated flop commits only in ticks where its gate's
  // enable terms are true; without it the gate is DEAD CODE and the flop loads
  // every tick regardless — a silently wrong waveform. SECONDARY clock (M6): a
  // flop on a different net commits only on a detected EDGE of it, which is why
  // the shared prev bit is updated once per tick rather than per phase — two
  // clocks whose edges land in the same tick then both sample pre-edge values,
  // which is what IEEE 1800 requires for coincident edges. Both are folded into
  // `_cen`.
  auto commit_state = [&](const auto& f) {
    const bool        guarded = !f.clock_guards.empty() || !f.sec_clock.is_invalid();
    const std::string ind     = guarded ? "      " : "    ";
    if (guarded) {
      fout->append("    if (", f.member, "_cen) {  // gated/secondary clock: commit only when its edge fires\n");
    }
    for (const auto& s : f.stages) {
      fout->append(ind, s, " = ", s, "_din;\n");
    }
    fout->append(ind, f.member, " = ", f.member, f.posedge ? "_din;\n" : "_din;  // negedge: commits at the FALL\n");
    if (guarded) {
      fout->append("    }\n");
    }
  };

  auto invalidate_upstream = [&](auto&& self, const hhds::Pin_class& p) -> void {
    if (p.is_invalid() || is_const_pin(p) || livehd::graph_util::is_graph_input_pin(p)) {
      return;
    }
    auto n = p.get_master_node();
    if (is_type_register(n) || type_op_of(n) == Ntype_op::Memory || type_op_of(n) == Ntype_op::Sub) {
      return;
    }
    auto dp = n.get_driver_pin(0);
    if (!dp.is_invalid()) {
      pin2var.erase(dp.get_class_index());
      prefetch_seen.erase(dp.get_class_index());
    }
    for (const auto& e : n.inp_edges()) {
      self(self, e.driver);
    }
  };
  auto invalidate_downstream = [&](const hhds::Pin_class& root) {
    std::vector<hhds::Pin_class>           work{root};
    absl::flat_hash_set<hhds::Class_index> seen;
    while (!work.empty()) {
      auto p = work.back();
      work.pop_back();
      if (p.is_invalid() || !seen.insert(p.get_class_index()).second) {
        continue;
      }
      for (const auto& e : p.out_edges()) {
        auto n = e.sink.get_master_node();
        if (is_type_register(n) || type_op_of(n) == Ntype_op::Memory || type_op_of(n) == Ntype_op::Sub
            || Ntype::has_multiple_driver_pins(type_op_of(n))) {
          continue;
        }
        auto dp = n.get_driver_pin(0);
        if (!dp.is_invalid()) {
          pin2var.erase(dp.get_class_index());
          prefetch_seen.erase(dp.get_class_index());
          work.push_back(dp);
        }
      }
    }
  };

  // PHASE 0/1 (low settle, then the rise).  An active-low latch is still
  // transparent immediately before the rising edge, so a posedge consumer at
  // that same edge samples the latch's just-settled value, not its previous
  // committed member.  Stage every latch first, then temporarily bind the Q of
  // active-low latches to its staged value while building posedge next-state.
  // This is the L1/coincident-edge rule and is equivalent to Verilog's latch
  // transparency before the active event without requiring an event queue.
  //
  // The SETTLE pass skips this whole block. It computes next-state, and a settle
  // has no next-state to compute; more importantly the windows rebind the
  // reference clock pin to constants and invalidate operand cones around each
  // one, which would corrupt the committed-state bindings the settle depends on.
  // Skipping it also leaves every latch Q bound to its plain member (the
  // end-of-period committed value), which is the right post-edge reading -- the
  // `<q>_low` transparency binding is a within-period concern.
  bool              any_negedge    = false;
  const std::string ref_clock_name = clock_input_of(g);
  const auto        ref_clock_pin  = ref_clock_name.empty() ? hhds::Pin_class{} : g->get_input_pin(ref_clock_name);
  // Low window: force the synthetic reference clock to zero and stage every
  // latch into `<q>_low`.  This works for both explicit active-low cells and a
  // source-level `if !clk` gate (whose cell polarity remains active-high).
  for (const auto& f : flops) {
    if (pass_ != Pass::Rise || !f.is_latch) {
      continue;
    }
    for (const auto* port : flop_operand_ports) {
      invalidate_upstream(invalidate_upstream, get_driver(find_sink_pin(f.node, port)));
    }
    if (!ref_clock_pin.is_invalid()) {
      pin2var[ref_clock_pin.get_class_index()] = "Slop<1>::create_integer(0)";
    }
    for (const auto* port : flop_operand_ports) {
      ensure_ready(get_driver(find_sink_pin(f.node, port)));
    }
    emit_flop_next(f, true, false);
  }
  // High window: rebuild with the reference clock at one.  The low-window
  // result is the hold value, so a latch open in either half retains the last
  // transparent value and closes with the correct end-of-period state.
  for (const auto& f : flops) {
    if (pass_ != Pass::Rise || !f.is_latch) {
      continue;
    }
    for (const auto* port : flop_operand_ports) {
      invalidate_upstream(invalidate_upstream, get_driver(find_sink_pin(f.node, port)));
    }
    if (!ref_clock_pin.is_invalid()) {
      pin2var[ref_clock_pin.get_class_index()] = "Slop<1>::create_integer(1)";
    }
    for (const auto* port : flop_operand_ports) {
      ensure_ready(get_driver(find_sink_pin(f.node, port)));
    }
    emit_flop_next(f, false, true);
  }
  if (pass_ == Pass::Rise && !ref_clock_pin.is_invalid()) {
    pin2var[ref_clock_pin.get_class_index()] = absl::StrCat("in.", cpp_port_path(ref_clock_name));
  }
  for (const auto& f : flops) {
    if (pass_ == Pass::Rise && f.is_latch) {
      auto qpin = f.node.get_driver_pin(0);
      if (!qpin.is_invalid()) {
        invalidate_downstream(qpin);
        pin2var[qpin.get_class_index()] = absl::StrCat(f.member, "_low");
      }
    }
  }
  for (const auto& f : flops) {
    if (pass_ != Pass::Rise) {
      break;  // the rise owns posedge next-state; the fall emits its own below
    }
    if (!f.is_latch && f.posedge) {
      for (const auto* port : flop_operand_ports) {
        ensure_ready(get_driver(find_sink_pin(f.node, port)));
      }
      emit_flop_next(f);
    } else if (!f.is_latch && !f.posedge) {
      any_negedge = true;
      // Its `_din` belongs to the fall pass (post-rise state), but its `_cen`
      // belongs HERE — see emit_commit_enable: every phase's edge test must read
      // the same pre-tick secondary-clock level, and the prev bit advances at
      // the end of this pass.
      for (const auto& gp : f.clock_guards) {
        ensure_ready(gp);
      }
      ensure_ready(f.sec_clock);
      emit_commit_enable(f);
    }
  }

  // Outputs from the state this pass reads. In the CYCLE pass that is the
  // PRE-edge state, and the result is the returned `o`/`__last_out` (what the
  // output drove during the period). In the SETTLE pass it is the committed
  // state, and the result lands directly in the `__out` member a `sigref` binds.
  // (The FALL pass drives no outputs: `__last_out` is the during-period value
  // the rise recorded, and `__out` is refreshed by the trailing settle.)
  const std::string out_dst = settle ? "    __out." : "    o.";
  if (!fall) {
    if (!settle) {
      fout->append("    Out o;\n");
    }
    for (const auto& io : ios) {
      if (io.is_input) {
        continue;
      }
      auto spin = g->get_output_pin(io.raw);
      auto drv  = spin.is_invalid() ? hhds::Pin_class{} : get_driver(spin);
      if (drv.is_invalid()) {
        fout->append("    // output ", io.field, " is undriven\n");
      } else {
        fout->append(out_dst, io.field, " = ", operand(drv, io.bits), ";\n");
      }
    }
  }
  // The settle pass ends here: everything below is state advance (VCD sample,
  // memory commit, flop commit, the fall phase) and must happen exactly once
  // per period.
  if (settle) {
    fout->append("}\n");
    return true;
  }

  // Everything from here to the posedge commit is the RISE half: it advances
  // state and must happen exactly once per period.
  if (!fall) {
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

  // Memories are posedge state.  Stage and commit them while every operand is
  // still the PRE-RISE binding, alongside the posedge flops below.  The old
  // placement after phase 2 was both semantically late and unsafe: phase 2 had
  // erased cones reachable from a posedge Q, so a write enable/data/address in
  // one of those cones degraded to `0 /*UNRESOLVED-CYCLE*/` and the write was
  // silently dropped.
  for (const auto& m : mems) {
    if (m.is_whole() && !m.registered()) {
      continue;  // combinational whole-array: contents applied in the combinational section
    }
    for (const auto& p : m.ports) {
      if (p.rd || p.addr.is_invalid() || p.din.is_invalid()) {
        continue;
      }
      if (!p.enable.is_invalid() && is_const_pin(p.enable) && hydrate_const(p.enable).is_known_false()) {
        continue;
      }
      fout->append(absl::StrCat("    ",
                                m.member,
                                ".stage_write<",
                                p.wridx,
                                ">(",
                                emit_wen(m, p),
                                ", ",
                                operand(p.addr, std::max(1, bits_of(p.addr))),
                                ", ",
                                operand(p.din, m.bits),
                                ");\n"));
    }
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1 && !p.addr.is_invalid()) {
        const int ab = std::max(1, bits_of(p.addr));
        fout->append(absl::StrCat("    ",
                                  m.member,
                                  "_q",
                                  p.rdidx,
                                  " = ",
                                  m.member,
                                  ".read<",
                                  p.rdidx,
                                  ">(",
                                  operand(p.addr, ab),
                                  ");  // sync read: the RESOLVED value\n"));
      }
    }
    std::string whole_close;
    if (m.is_whole()) {
      const int W = m.bits * m.size;
      if (!m.reset.is_invalid()) {
        const std::string initbus = m.init.is_invalid() ? absl::StrCat("Slop<", W, ">::create_integer(0)") : operand(m.init, W);
        fout->append(absl::StrCat("    if ((",
                                  operand(m.reset, 1),
                                  ").is_known_true()) { ",
                                  m.member,
                                  ".apply_update(",
                                  initbus,
                                  "); ",
                                  m.member,
                                  ".clear_pending(); } else {\n"));
        whole_close = "    }\n";
      }
      const std::string ue
          = m.update_enable.is_invalid() ? "" : absl::StrCat("if ((", operand(m.update_enable, 1), ").is_known_true()) ");
      fout->append(absl::StrCat("    ", ue, m.member, ".apply_update(", operand(m.update, W), ");\n"));
    }
    fout->append(whole_close);
    fout->append(absl::StrCat("    ", m.member, ".tick();\n"));
  }

  // commit flops (+ pipe stages) at the clock edge (PHASE 1: the rise)
  for (const auto& f : flops) {
    if (!f.posedge) {
      continue;  // negedge state commits in phase 2, below
    }
    commit_state(f);
  }
  }  // end of the rise half

  // THE FALL half: negedge state, evaluated AFTER the rise committed.
  //
  // This is now its own method with a fresh binding environment, so the old
  // invalidate/re-ready dance is gone: every flop Q was bound in the prologue to
  // its just-committed member, and the walk above re-emitted the fall cone from
  // there. Data crossing a SUB-INSTANCE is no longer refused either — the child
  // ran its whole cycle() during the rise, so `settle_mode` has the walk ask it
  // for a comb re-settle (`sub.__settle`) rather than a second advance, which is
  // exactly what the `negedge-through-instance` refusal existed to prevent.
  if (fall) {
    (void)any_negedge;
    // A negedge-only child samples after the parent's rise.  Its current-state
    // outputs were pre-bound before the walk, but its state-advancing call is
    // delayed until now so inputs driven by parent posedge Qs are rebuilt from
    // the just-committed members.  A local negedge consumer must see the child's
    // POST-fall state in this same period, and it does so for free: the child's
    // own cycle() ends with its trailing settle, so `<inst>.__out` already holds
    // the outputs of the state it just committed. This used to need a second
    // full peek() (a whole-subtree snapshot/restore) after the call.
    // Bind the member directly -- unlike the pre-bind above, nothing runs after
    // this point that would advance the child again, so no copy is needed.
    for (const auto* sp : deferred_fall) {
      const auto& s   = *sp;
      auto        sio = s.node.get_subnode_io();
      if (!sio) {
        continue;
      }
      fout->append(absl::StrCat("    ", s.callee_struct, "::In ", s.inst, "__fall_i;\n"));
      for (const auto& d : sio->get_input_pin_decls()) {
        auto drv = get_driver(find_sink_pin(s.node, d.name));
        int  wb  = d.bits > 0 ? static_cast<int>(d.bits) : 1;
        ensure_ready(drv);
        fout->append(absl::StrCat("    ", s.inst, "__fall_i.", cpp_port_path(d.name), " = ", operand(drv, wb), ";\n"));
      }
      fout->append(absl::StrCat("    ", s.inst, ".cycle(", s.inst, "__fall_i);\n"));
      for (const auto& d : sio->get_output_pin_decls()) {
        auto opin = find_driver_pin(s.node, d.name);
        if (!opin.is_invalid()) {
          pin2var[opin.get_class_index()] = absl::StrCat(s.inst, ".__out.", cpp_port_path(d.name));
        }
      }
    }
    // Re-ready EVERY cone the negedge emission below will read: the four
    // operand ports of emit_flop_next(), plus the ICG guards and secondary
    // clock that commit_state() reads. Readying only `din` is not enough — a
    // reg-file whose write strobe is a transparent latch AND-ed with posedge
    // state leaves `enable` in the erased set, and operand() then substitutes a
    // constant 0 into the commit guard, so the flop silently never loads.
    // ensure_ready() no-ops on invalid and const pins, so absent ports are free.
    for (const auto& f : flops) {
      if (f.posedge) {
        continue;
      }
      for (const auto* port : flop_operand_ports) {
        ensure_ready(get_driver(find_sink_pin(f.node, port)));
      }
      for (const auto& gp : f.clock_guards) {
        ensure_ready(gp);
      }
      ensure_ready(f.sec_clock);
    }
    for (const auto& f : flops) {
      if (f.posedge) {
        continue;
      }
      // `_din` only: `_cen` was computed in the rise pass, against the pre-tick
      // secondary-clock level both phases must observe.
      emit_flop_next(f, /*latch_low=*/false, /*latch_high=*/false, /*with_cen=*/false);
    }
    for (const auto& f : flops) {
      if (f.posedge) {
        continue;
      }
      commit_state(f);
    }
  }

  // Secondary-clock levels for the NEXT tick's edge detection. Both phases'
  // guards must observe the same PRE-TICK level -- updating this between them
  // made every secondary negedge test compare `cur` against itself and miss the
  // edge permanently. That still holds now the phases are separate methods: the
  // fall reads no level at all (emit_commit_enable parked every `_cen`, negedge
  // ones included, back in the rise), so advancing the bit at the end of the
  // RISE is safe and, unlike the fall, always runs.
  if (!fall) {
    absl::flat_hash_set<std::string> done;
    for (const auto& f : flops) {
      if (f.prev_member.empty() || !done.insert(f.prev_member).second) {
        continue;
      }
      fout->append("    ", f.prev_member, " = (", operand(f.sec_clock, 1), ").is_known_true();\n");
    }
  }

  // Fail closed on a PHASE-2 substitution. Stage 0 above already cleared this
  // graph of real combinational cycles, so an operand that is STILL unbound
  // here is not a loop: it is a negedge cone that phase 2 dropped and did not
  // re-ready, and operand() has quietly put a 0 in its place. Emitting that
  // would be a silently wrong sim (the failure mode the whole fail-closed pass
  // exists to prevent), so refuse instead.
  if (cycle_unresolved_ && !cycle_reported_) {
    livehd::diag::err("inou.cgen.sim", "negedge-operand-unresolved", "internal")
        .msg("module `{}` has a NEGEDGE state element whose operand {} could not be re-resolved after the rise",
             gname,
             cycle_first_label_.empty() ? std::string{"(unnamed)"} : cycle_first_label_)
        .hint(
            "the two-phase tick drops the pin2var bindings reachable from a posedge Q and re-emits them via "
            "ensure_ready(); every pin the negedge emission reads (din, enable, reset_pin, initial, the ICG "
            "clock_guards and any secondary clock) must be re-readied — tracked as todo/livehd/2f-latch M5")
        .emit();
    cycle_reported_ = true;
    return false;
  }

  if (!fall) {
    // The during-period outputs, recorded before any commit -- the value the
    // query engine publishes as `"sampled":"during_period"`.
    fout->append("    __last_out = o;  // 2f-sim B: free output observation for the query engine\n");
  }
  fout->append("}\n");
  return true;
  };
  if (!emit_period_body(Pass::Rise)) {
    return;
  }
  if (has_fall && !emit_period_body(Pass::Fall)) {
    return;
  }
  if (!emit_period_body(Pass::Settle)) {
    return;
  }

  // ---- dump_state: flops/regs -> the `_r` map (by hierarchical name, pyrope
  // literal); memories -> one editable `<_dir>/<_p><member>.hex` each; recurse
  // into sub-instances. Mirrors the reset_cycle/peek member walk. ----
  fout->append("void ",
               mod,
               "::dump_state(const std::string& _p, std::map<std::string, std::string>& _r, const std::string& _dir) const {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _r[_p + \"", s, "\"] = ", s, ".to_pyrope();\n"));
    }
    fout->append(absl::StrCat("  _r[_p + \"", f.member, "\"] = ", f.member, ".to_pyrope();\n"));
  }
  {
    absl::flat_hash_set<std::string> emitted;
    for (const auto& f : flops) {
      if (!f.prev_member.empty() && emitted.insert(f.prev_member).second) {
        fout->append(absl::StrCat("  _r[_p + \"", f.prev_member, "\"] = ", f.prev_member, " ? \"true\" : \"false\";\n"));
      }
    }
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
  fout->append("void ",
               mod,
               "::load_state(const std::string& _p, const std::map<std::string, std::string>& _r, const std::string& _dir) {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"",
                                s,
                                "\"); _it != _r.end()) ",
                                s,
                                " = Slop<",
                                f.bits,
                                ">::from_pyrope(_it->second);\n"));
    }
    fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"",
                              f.member,
                              "\"); _it != _r.end()) ",
                              f.member,
                              " = Slop<",
                              f.bits,
                              ">::from_pyrope(_it->second);\n"));
  }
  {
    absl::flat_hash_set<std::string> emitted;
    for (const auto& f : flops) {
      if (!f.prev_member.empty() && emitted.insert(f.prev_member).second) {
        fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"",
                                  f.prev_member,
                                  "\"); _it != _r.end()) ",
                                  f.prev_member,
                                  " = Slop<1>::from_pyrope(_it->second).is_known_true();\n"));
      }
    }
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("  hlop::ckpt::read_mem_hex(_dir + \"/\" + _p + \"", m.member, ".hex\", ", m.member, ");\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"",
                                  m.member,
                                  "_q",
                                  p.rdidx,
                                  "\"); _it != _r.end()) ",
                                  m.member,
                                  "_q",
                                  p.rdidx,
                                  " = Slop<",
                                  m.bits,
                                  ">::from_pyrope(_it->second);\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".load_state(_p + \"", s.inst, ".\", _r, _dir);\n"));
  }
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"__in.",
                                io.field,
                                "\"); _it != _r.end()) __in.",
                                io.field,
                                " = Slop<",
                                io.bits,
                                ">::from_pyrope(_it->second);\n"));
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
  {
    absl::flat_hash_set<std::string> emitted;
    for (const auto& f : flops) {
      if (!f.prev_member.empty() && emitted.insert(f.prev_member).second) {
        fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", f.prev_member, "\"), 1);\n"));
      }
    }
  }
  for (const auto& m : mems) {
    // Ordering + wensize are part of the memory's identity: a checkpoint taken
    // under one same-cycle mode restores into a different machine under another,
    // and without this the hash would not budge.
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"",
                              m.member,
                              "\"), ",
                              m.bits,
                              "), ",
                              m.size,
                              ");\n"));
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(_h, ",
                              static_cast<int>(m.order),
                              "), ",
                              m.wensize,
                              ");\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"",
                                  m.member,
                                  "_q",
                                  p.rdidx,
                                  "\"), ",
                                  m.bits,
                                  ");\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a(_h, \"",
                              s.inst,
                              "\"); _h = hlop::ckpt::fnv1a_u64(_h, ",
                              s.inst,
                              ".design_hash());\n"));
  }
  // Interface (every I/O port name+width+direction) so a port change warns.
  for (const auto& io : ios) {
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"",
                              io.field,
                              "\"), ",
                              io.bits,
                              "), ",
                              io.is_input ? 1 : 2,
                              ");\n"));
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
        fout->append(absl::StrCat("  _m[_p + \"", m.member, "_q", p.rdidx, "\"] = ", m.member, "_q", p.rdidx, ".to_i64_low();\n"));
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

  // ---- observe_signals / observe_mem: the LOSSLESS query surface (2f-sim B) ----
  // Same hierarchical walk as probe_signals, with the two things the query engine
  // needs that the legacy probe map cannot express:
  //   * FULL-WIDTH values. `to_i64_low()` returns the raw low limb, so every
  //     signal wider than 64 bits was silently truncated in --probe rows and in
  //     --break-when comparisons. `to_hex(digits)` renders the whole vector, and
  //     the digit count is pinned to ceil(bits/4) so the text is fixed-width and
  //     a reader can recover the value without knowing the sign convention.
  //   * OUTPUTS, served from __last_out (recorded by cycle()). Inputs keep their
  //     historical `__in.` spelling here; the catalog publishes the clean port
  //     name and carries this one as an alias.
  fout->append("void ", mod, "::observe_signals(const std::string& _p, std::map<std::string, std::string>& _m) const {\n");
  const auto hexdigits = [](int bits) { return std::to_string((std::max(1, bits) + 3) / 4); };
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _m[_p + \"", s, "\"] = ", s, ".to_hex(", hexdigits(f.bits), ");\n"));
    }
    fout->append(absl::StrCat("  _m[_p + \"", f.member, "\"] = ", f.member, ".to_hex(", hexdigits(f.bits), ");\n"));
  }
  for (const auto& m : mems) {
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _m[_p + \"", m.member, "_q", p.rdidx, "\"] = ", m.member, "_q", p.rdidx, ".to_hex(",
                                  hexdigits(m.bits), ");\n"));
      }
    }
  }
  for (const auto& io : ios) {
    if (io.is_input) {
      // Keyed by the CLEAN port name, which is what the catalog publishes and
      // therefore what a query asks for. probe_signals keeps the historical
      // `__in.<field>` spelling (pinned by lhd_sim_observe_test.sh) and the
      // catalog carries it as an alias, but observe_signals is new and only the
      // query engine reads it, so it has no legacy shape to preserve. Keying it
      // the old way made every INPUT unqueryable: the catalog offered `acc.din`
      // while the stream only held `acc.__in.din`, so a perfectly valid name
      // came back "not observable".
      fout->append(absl::StrCat("  _m[_p + \"", io.field, "\"] = __in.", io.field, ".to_hex(", hexdigits(io.bits), ");\n"));
    } else {
      fout->append(absl::StrCat("  _m[_p + \"", io.field, "\"] = __last_out.", io.field, ".to_hex(", hexdigits(io.bits),
                                ");\n"));
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".observe_signals(_p + \"", s.inst, ".\", _m);\n"));
  }
  fout->append("}\n");

  // One COMMITTED memory word by member name. `_n` is relative to this instance
  // ("mem" here, "sub.mem" one level down), so the walk mirrors the catalog's
  // dotted names. Staged same-cycle writes are transient between ticks by
  // construction, so what is read here is exactly the entry array.
  fout->append("bool ", mod, "::observe_mem(const std::string& _n, long _i, std::string& _o) const {\n");
  for (const auto& m : mems) {
    fout->append(absl::StrCat("  if (_n == \"", m.member, "\") {\n"));
    fout->append(absl::StrCat("    if (_i < 0 || _i >= ", m.size, ") { return false; }\n"));
    fout->append(absl::StrCat("    _o = ", m.member, "[static_cast<size_t>(_i)].to_hex(", hexdigits(m.bits), ");\n"));
    fout->append("    return true;\n  }\n");
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  if (_n.compare(0, ", s.inst.size() + 1, ", \"", s.inst, ".\") == 0) {\n"));
    fout->append(absl::StrCat("    return ", s.inst, ".observe_mem(_n.substr(", s.inst.size() + 1, "), _i, _o);\n  }\n"));
  }
  fout->append("  return false;\n}\n");

  // ---- <stem>.iface.json — the machine-readable module manifest (2f-sim B0) ----
  // Replaces the historical TEXT SCRAPE of this module's generated .hpp
  // (prp_sim.cpp's parse_hpp), which silently bit-rotted the day memories stopped
  // being `std::array<Slop<...>>` members and became `hlop::Memory_*<...>`. One
  // JSON object per module holds exactly what a testbench generator and the
  // sim-query catalog need: IO with tuple leaves already flattened to their dotted
  // C++/RTL path, state elements, memories (with the shape a word read needs), and
  // sub-instances. Two widths are published per entry because they differ and both
  // matter: `bits` is the INTERNAL Slop width that --list-signals has always
  // reported, `declared_bits` is the source-declared width an agent sees in the
  // Pyrope/Verilog. They diverge only for INTERNAL nets, which carry a sign slot
  // above the magnitude — a `reg count:u8` is a Slop<9> declared 8. A PORT is
  // declared at its nominal width (`value:u8` IS a Slop<8>) and a memory's data
  // width comes straight from the cell, so for those the two widths are equal and
  // the sign-slot adjustment must NOT be applied.
  // Names here are cpp_id()/cpp_port_path() output (identifier chars plus '.' for
  // tuple leaves), so no JSON string escaping is required.
  {
    auto jout = std::make_shared<File_output>(absl::StrCat(base, ".iface.json"));
    // Internal-net width -> source-declared width (unsigned drops the sign slot).
    const auto  decl_reg = [](int b, bool uns) { return uns ? std::max(1, b - 1) : b; };
    std::string j;
    absl::StrAppend(&j, "{\"schema_version\":1,\"kind\":\"sim_iface\",\"gen\":\"", kSimGenVersion, "\",\"module\":\"", mod,
                    "\",\n");

    // IO, in port_id order (the order the In/Out structs are emitted in).
    absl::StrAppend(&j, " \"io\":[");
    bool first = true;
    for (const auto& io : ios) {
      // Port signedness, following cgen_verilog's rule because the LGraph's own
      // answer differs by direction. An INPUT pin is marked signed by
      // CONVENTION ("cgen declares every port signed", upass_tolg's io_meta
      // inputs loop, to compensate a to-positive Get_mask), so its attr says
      // nothing about the source-declared sign — reading it made every `u8`
      // input come back signed, which would render `dec` negative for any value
      // above half range. The declared sign lives in LNAST io_meta, which this
      // LGraph-level emitter cannot see, so inputs are published unsigned and
      // the catalog does not claim otherwise. An OUTPUT has no such
      // compensation: follow its DRIVER's sign, exactly as cgen_verilog does.
      bool uns = true;
      if (!io.is_input) {
        auto pin = g->get_output_pin(io.raw);
        if (!pin.is_invalid()) {
          auto drv = get_driver(pin);
          uns      = drv.is_invalid() ? is_unsign(pin) : is_unsign(drv);
        }
      }
      absl::StrAppend(&j, first ? "" : ",\n      ", "{\"name\":\"", io.field, "\",\"dir\":\"", io.is_input ? "input" : "output",
                      "\",\"bits\":", io.bits, ",\"declared_bits\":", io.bits, ",\"signed\":", uns ? "false" : "true", "}");
      first = false;
    }
    absl::StrAppend(&j, "],\n");

    // State: pipe stages precede their flop, matching describe_signals() order.
    absl::StrAppend(&j, " \"regs\":[");
    first = true;
    for (const auto& f : flops) {
      const bool uns = is_unsign(f.node.get_driver_pin(0));
      for (const auto& s : f.stages) {
        absl::StrAppend(&j, first ? "" : ",\n        ", "{\"name\":\"", s, "\",\"kind\":\"pipe\",\"bits\":", f.bits,
                        ",\"declared_bits\":", decl_reg(f.bits, uns), ",\"signed\":", uns ? "false" : "true", "}");
        first = false;
      }
      absl::StrAppend(&j, first ? "" : ",\n        ", "{\"name\":\"", f.member,
                      "\",\"kind\":\"flop\",\"bits\":", f.bits, ",\"declared_bits\":", decl_reg(f.bits, uns),
                      ",\"signed\":", uns ? "false" : "true", ",\"latch\":", f.is_latch ? "true" : "false", "}");
      first = false;
    }
    absl::StrAppend(&j, "],\n");

    // Memories: `rd_regs` are the sync-read output registers already carried by
    // the catalog as kind "memrd"; `size`/`bits` are what a word read needs.
    absl::StrAppend(&j, " \"mems\":[");
    first = true;
    for (const auto& m : mems) {
      bool uns = true;
      for (const auto& p : m.ports) {
        if (p.rd && p.dout_pid >= 0) {
          uns = is_unsign(m.node.get_driver_pin(static_cast<hhds::Port_id>(p.dout_pid)));
          break;
        }
      }
      std::string rds;
      for (const auto& p : m.ports) {
        if (p.rd && m.type == 1) {
          absl::StrAppend(&rds, rds.empty() ? "" : ",", "\"", m.member, "_q", p.rdidx, "\"");
        }
      }
      absl::StrAppend(&j, first ? "" : ",\n        ", "{\"name\":\"", m.member, "\",\"bits\":", m.bits,
                      ",\"declared_bits\":", m.bits, ",\"signed\":", uns ? "false" : "true",
                      ",\"size\":", m.size, ",\"ordering\":\"",
                      m.order == Mem::Order::fwd       ? "fwd"
                      : m.order == Mem::Order::none    ? "none"
                      : m.order == Mem::Order::program ? "program"
                                                       : "old",
                      "\",\"wensize\":", m.wensize, ",\"n_rd\":", m.n_rd, ",\"n_wr\":", m.n_wr,
                      ",\"sync_read\":", m.type == 1 ? "true" : "false", ",\"rd_regs\":[", rds, "]}");
      first = false;
    }
    absl::StrAppend(&j, "],\n");

    absl::StrAppend(&j, " \"subs\":[");
    first = true;
    for (const auto& s : subs) {
      absl::StrAppend(&j, first ? "" : ",\n        ", "{\"inst\":\"", s.inst, "\",\"module\":\"", s.callee_struct, "\"}");
      first = false;
    }
    absl::StrAppend(&j, "]\n}\n");
    jout->append(j);
  }

  // Persist the (updated) generation digests only after a CLEAN emission — a
  // Stage-0 comb-loop failure must not record a digest that would make the
  // next run skip over the same broken files.
  if (!odir.empty() && !cycle_reported_ && !cycle_unresolved_) {
    save_gen_digests();
  }
}
