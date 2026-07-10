//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
//  pass.lean - emit per-design Lean theory scaffolding for graph-certificate
//  based translation proofs.  This pass intentionally mirrors the public knobs
//  of pass.isabelle so the existing DINO/CVA6 generation scripts can be ported
//  incrementally.

#include "pass_lean.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "perf_tracing.hpp"

static Pass_plugin pass_plugin_lean("pass_lean", Pass_lean::setup);

namespace {

LeanCertWFMode parse_cert_wf_mode(std::string_view mode) {
  if (mode == "eval") {
    return LeanCertWFMode::Eval;
  }
  if (mode == "sorry") {
    return LeanCertWFMode::Sorry;
  }
  if (mode == "chunked") {
    return LeanCertWFMode::Chunked;
  }
  return LeanCertWFMode::Skip;
}

LeanCertWFFallback parse_cert_wf_fallback(std::string_view mode) {
  if (mode == "sorry") {
    return LeanCertWFFallback::Sorry;
  }
  if (mode == "eval") {
    return LeanCertWFFallback::Eval;
  }
  return LeanCertWFFallback::Fail;
}

const std::unordered_set<std::string> kLeanReserved = {
    "abbrev", "axiom", "by",       "class", "def",     "deriving", "do",      "else",   "end",
    "example", "false", "for",      "fun",   "if",      "import",   "in",      "inductive",
    "instance", "let",  "match",    "mutual", "namespace", "open",  "opaque",  "partial",
    "private", "protected", "rec", "set_option", "structure", "theorem", "then", "true",
    "universe", "variable", "where", "with",
};

std::string sanitize_lean(std::string_view name) {
  std::string out;
  out.reserve(name.size() + 4);

  for (unsigned char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (ok) {
      out.push_back(static_cast<char>(c));
    } else {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "_x%02x_", c);
      out += buf;
    }
  }

  if (out.empty()) {
    out = "id";
  }
  if (out[0] >= '0' && out[0] <= '9') {
    out = "id_" + out;
  }
  if (kLeanReserved.count(out) > 0) {
    out = "id_" + out;
  }
  return out;
}

using Node     = hhds::Node_class;
using Node_pin = hhds::Pin_class;
using Edge     = hhds::Edge_class;

struct Emit_error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

uint32_t node_id(const Node& node) { return static_cast<uint32_t>(node.get_debug_nid()); }

Node pin_node(const Node_pin& pin) { return pin.get_master_node(); }

Ntype_op node_op(const Node& node) { return livehd::graph_util::type_op_of(node); }

bool node_is_flop(const Node& node) { return livehd::graph_util::is_type_flop(node); }

bool node_is_memory(const Node& node) { return node_op(node) == Ntype_op::Memory; }

bool pin_is_input(const Node_pin& pin) { return livehd::graph_util::is_graph_input_pin(pin); }

bool pin_is_const(const Node_pin& pin) { return livehd::graph_util::is_const_pin(pin); }

livehd::graph_util::Edge_vec inp_edges_ordered(const Node& node) {
  auto edges = node.inp_edges();
  std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
    const auto ap = a.sink.get_port_id();
    const auto bp = b.sink.get_port_id();
    if (ap != bp) {
      return ap < bp;
    }
    return a.driver.get_class_index().value < b.driver.get_class_index().value;
  });
  return edges;
}

std::string sink_pin_name(const Edge& edge) {
  const auto sink_node = pin_node(edge.sink);
  return std::string(Ntype::get_sink_name(node_op(sink_node), edge.sink.get_port_id()));
}

uint32_t raw_pin_width(const Node_pin& pin) { return static_cast<uint32_t>(livehd::graph_util::bits_of(pin)); }

uint32_t raw_node_width(const Node& node) { return raw_pin_width(node.create_driver_pin(0)); }

Dlop pin_const_value(const Node_pin& pin) { return livehd::graph_util::hydrate_const(pin); }

Dlop node_const_value(const Node& node) { return livehd::graph_util::hydrate_const(node); }

bool node_output_is_signed(const Node& node) {
  auto n    = node;
  auto dpin = n.create_driver_pin(0);
  return !dpin.is_invalid() && !livehd::graph_util::is_unsign(dpin);
}

std::string make_field_name(std::string_view role, std::string_view rtl_name, absl::flat_hash_set<std::string>& used) {
  std::string base = std::string(role) + sanitize_lean(rtl_name);
  std::string name = base;
  size_t      n    = 0;
  while (used.count(name) > 0 || kLeanReserved.count(name) > 0) {
    ++n;
    name = base + "_" + std::to_string(n);
  }
  used.insert(name);
  return name;
}

struct LeanCtx {
  hhds::Graph* g = nullptr;
  std::string  top_name;
  std::string  base_name;
  bool         strict = true;
  size_t       max_width = 1024;

  absl::flat_hash_set<std::string> used_fields;

  std::map<std::string, std::string> input_field;
  std::map<std::string, uint32_t>    input_width;
  std::map<std::string, uint32_t>    input_source_id;

  std::map<std::string, std::string> output_field;
  std::map<std::string, uint32_t>    output_width;

  std::map<uint32_t, std::string> flop_field;
  std::map<uint32_t, uint32_t>    flop_width;
};

[[noreturn]] void fatal(const LeanCtx& /*ctx*/, const std::string& msg) { throw Emit_error("[ERROR] pass.lean: " + msg); }

void check_width(const LeanCtx& ctx, const Node& node, uint32_t w, std::string_view what) {
  if (w == 0) {
    fatal(ctx,
          "node n_" + std::to_string(node_id(node)) + " (" + std::string(what)
              + ") has zero width; Lean BitVec generation requires positive widths.");
  }
  if (w > ctx.max_width) {
    fatal(ctx,
          "node n_" + std::to_string(node_id(node)) + " (" + std::string(what) + ") has width " + std::to_string(w)
              + " > max_width=" + std::to_string(ctx.max_width));
  }
}

uint32_t pin_width(const LeanCtx& ctx, const Node_pin& pin, const Node& owner) {
  auto w = raw_pin_width(pin);
  if (w == 0 || static_cast<size_t>(w) > ctx.max_width) {
    if (pin_is_const(pin)) {
      return 1;
    }
    if (ctx.strict) {
      check_width(ctx, owner, w, "pin");
    }
    return 1;
  }
  return static_cast<uint32_t>(w);
}

uint32_t node_width(const LeanCtx& ctx, const Node& node) {
  auto w = raw_node_width(node);
  check_width(ctx, node, w, "node");
  return static_cast<uint32_t>(w);
}

std::string lean_int_literal(std::string_view decimal) {
  if (!decimal.empty() && decimal.front() == '-') {
    return "(-Int.ofNat " + std::string(decimal.substr(1)) + ")";
  }
  return "(Int.ofNat " + std::string(decimal) + ")";
}

std::string lit_bv(uint32_t w, std::string_view v) { return "(BitVec.ofInt " + std::to_string(w) + " (" + std::string(v) + "))"; }

std::string lit_zero(uint32_t w) { return "(0#" + std::to_string(w) + ")"; }

std::string lit_one(uint32_t w) { return "(1#" + std::to_string(w) + ")"; }

std::string lit_const_at(const LeanCtx& ctx, const Node& node, const Dlop& v, uint32_t w) {
  if (w == 0 || static_cast<size_t>(w) > ctx.max_width) {
    check_width(ctx, node, w, "Const");
  }
  if (v.has_unknowns()) {
    if (ctx.strict) {
      fatal(ctx, "Const node n_" + std::to_string(node_id(node)) + " has X/Z bits; strict Lean emission rejects four-valued logic.");
    }
    return lit_zero(w);
  }
  if (v.is_known_zero()) {
    return lit_zero(w);
  }
  if (v.same_repr(*Dlop::create_integer(-1))) {
    return lit_bv(w, "-1");
  }
  if (v.is_just_i64()) {
    return lit_bv(w, std::to_string(v.to_just_i64()));
  }
  auto [mask_lo, mask_hi] = v.get_mask_range();
  if (mask_lo == 0 && static_cast<uint32_t>(mask_hi) <= w) {
    return "((1#" + std::to_string(w) + " <<< " + std::to_string(w) + ") - 1#" + std::to_string(w) + ")";
  }
  return lit_bv(w, lean_int_literal(v.to_decimal_string()));
}

std::string int_of_const(const LeanCtx& ctx, const Node& node, const Dlop& v) {
  if (v.has_unknowns()) {
    fatal(ctx, "Const node n_" + std::to_string(node_id(node)) + " has X/Z bits; strict Lean certificate rejects four-valued logic.");
  }
  if (v.is_known_zero()) {
    return "0";
  }
  if (v.same_repr(*Dlop::create_integer(-1))) {
    return "(-Int.ofNat 1)";
  }
  if (v.is_just_i64()) {
    return lean_int_literal(std::to_string(v.to_just_i64()));
  }
  return lean_int_literal(v.to_decimal_string());
}

std::string input_name_for_pin(const LeanCtx& ctx, const Node_pin& pin) {
  // pin_name_of resolves a graph-input pin's declared port name directly (via
  // the graph's IO maps); no need to identity-match against get_input_pin.
  auto pname = std::string(livehd::graph_util::pin_name_of(pin));
  if (!pname.empty() && ctx.input_field.contains(pname)) {
    return pname;
  }
  return {};
}

std::string driver_expr_at(const LeanCtx& ctx, const Node_pin& dpin, uint32_t expected_w);

std::string ucast_expr(const std::string& expr, uint32_t w) {
  return "((bv_zext " + expr + ") : BitVec " + std::to_string(w) + ")";
}

std::string ucast_pin_at(const LeanCtx& ctx, const Node_pin& dpin, uint32_t w) {
  return ucast_expr(driver_expr_at(ctx, dpin, w), w);
}

std::string driver_expr(const LeanCtx& ctx, const Node_pin& dpin) {
  auto driver_node = pin_node(dpin);
  if (pin_is_input(dpin)) {
    auto pname = input_name_for_pin(ctx, dpin);
    auto it    = ctx.input_field.find(pname);
    if (it == ctx.input_field.end()) {
      throw Emit_error("internal: graph input pin has no Lean input field");
    }
    return "i." + it->second;
  }
  if (node_is_flop(driver_node)) {
    auto fit = ctx.flop_field.find(node_id(driver_node));
    if (fit == ctx.flop_field.end()) {
      throw Emit_error("internal: flop n_" + std::to_string(node_id(driver_node)) + " has no Lean state field");
    }
    return "s." + fit->second;
  }
  if (pin_is_const(dpin)) {
    auto v = pin_const_value(dpin);
    auto w = raw_pin_width(dpin);
    if (w == 0) {
      w = std::max<uint32_t>(1, static_cast<uint32_t>(v.get_bits()));
    }
    return lit_const_at(ctx, driver_node, v, w);
  }
  return "n_" + std::to_string(node_id(driver_node));
}

std::string driver_expr_at(const LeanCtx& ctx, const Node_pin& dpin, uint32_t expected_w) {
  auto driver_node = pin_node(dpin);
  if (pin_is_const(dpin)) {
    return lit_const_at(ctx, driver_node, pin_const_value(dpin), expected_w);
  }
  return driver_expr(ctx, dpin);
}

std::string nary_op_inline(const std::string& op, const std::vector<std::string>& operands) {
  if (operands.empty()) {
    throw Emit_error("internal: empty operand list for " + op);
  }
  std::string out = operands[0];
  for (size_t i = 1; i < operands.size(); ++i) {
    out = "(" + out + " " + op + " " + operands[i] + ")";
  }
  return out;
}

uint32_t minimal_unsigned_const_width(const Dlop& v) {
  if (!v.is_just_i64()) {
    return std::max<uint32_t>(1, static_cast<uint32_t>(v.get_bits()));
  }
  const int64_t iv = v.to_just_i64();
  if (iv <= 0) {
    return 1;
  }
  auto uv = static_cast<uint64_t>(iv);
  uint32_t bits = 0;
  while (uv != 0) {
    ++bits;
    uv >>= 1;
  }
  return std::max<uint32_t>(1, bits);
}

std::string shift_amount_expr_at(const LeanCtx& ctx, const Node_pin& dpin, uint32_t expected_w) {
  auto driver_node = pin_node(dpin);
  if (pin_is_const(dpin)) {
    const auto v = pin_const_value(dpin);
    const auto w = std::max<uint32_t>(expected_w, minimal_unsigned_const_width(v));
    return lit_const_at(ctx, driver_node, v, w);
  }
  return driver_expr_at(ctx, dpin, expected_w);
}

std::string emit_node_expr(const LeanCtx& ctx, const Node& node) {
  const auto op = node_op(node);
  const auto w  = node_width(ctx, node);

  switch (op) {
    case Ntype_op::Nconst: return lit_const_at(ctx, node, node_const_value(node), w);

    case Ntype_op::IO: throw Emit_error("internal: emit_node_expr called on IO node");

    case Ntype_op::Sum: {
      std::vector<std::string> a_terms, b_terms;
      for (const auto& e : inp_edges_ordered(node)) {
        auto expr = ucast_pin_at(ctx, e.driver, w);
        if (e.sink.get_port_id() == 0) {
          a_terms.push_back(expr);
        } else if (e.sink.get_port_id() == 1) {
          b_terms.push_back(expr);
        }
      }
      if (a_terms.empty() && b_terms.empty()) {
        fatal(ctx, "Sum node n_" + std::to_string(node_id(node)) + " has no inputs.");
      }
      if (b_terms.empty()) {
        return nary_op_inline("+", a_terms);
      }
      if (a_terms.empty()) {
        return "(" + lit_zero(w) + " - " + nary_op_inline("+", b_terms) + ")";
      }
      return "((" + nary_op_inline("+", a_terms) + ") - (" + nary_op_inline("+", b_terms) + "))";
    }

    case Ntype_op::Mult: {
      std::vector<std::string> terms;
      for (const auto& e : inp_edges_ordered(node)) {
        terms.push_back(ucast_pin_at(ctx, e.driver, w));
      }
      if (terms.empty()) {
        fatal(ctx, "Mult node n_" + std::to_string(node_id(node)) + " has no inputs.");
      }
      return nary_op_inline("*", terms);
    }

    case Ntype_op::Div: {
      std::vector<Node_pin> drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        drivers.push_back(e.driver);
      }
      if (drivers.size() != 2) {
        fatal(ctx, "Div node n_" + std::to_string(node_id(node)) + " is not binary.");
      }
      return "(sem_udiv " + ucast_pin_at(ctx, drivers[0], w) + " " + ucast_pin_at(ctx, drivers[1], w) + ")";
    }

    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor: {
      std::vector<std::string> terms;
      for (const auto& e : inp_edges_ordered(node)) {
        terms.push_back(ucast_pin_at(ctx, e.driver, w));
      }
      if (terms.empty()) {
        return lit_zero(w);
      }
      const char* bop = op == Ntype_op::And ? "&&&" : (op == Ntype_op::Or ? "|||" : "^^^");
      return nary_op_inline(bop, terms);
    }

    case Ntype_op::Ror: {
      std::vector<std::string> bools;
      for (const auto& e : inp_edges_ordered(node)) {
        const auto ew = pin_width(ctx, e.driver, node);
        bools.push_back("(bitvec_nonzero " + driver_expr_at(ctx, e.driver, ew) + ")");
      }
      if (bools.empty()) {
        return lit_zero(1);
      }
      std::string disj = bools[0];
      for (size_t i = 1; i < bools.size(); ++i) {
        disj = "(" + disj + " || " + bools[i] + ")";
      }
      return "(bool_to_bv1 " + disj + ")";
    }

    case Ntype_op::Not: {
      std::optional<Node_pin> a;
      for (const auto& e : inp_edges_ordered(node)) {
        a = e.driver;
        break;
      }
      if (!a) {
        fatal(ctx, "Not node n_" + std::to_string(node_id(node)) + " has no input.");
      }
      return "(~~~ " + ucast_pin_at(ctx, *a, w) + ")";
    }

    case Ntype_op::LT:
    case Ntype_op::GT: {
      std::vector<Node_pin> drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        drivers.push_back(e.driver);
      }
      if (drivers.size() != 2) {
        fatal(ctx, "LT/GT node n_" + std::to_string(node_id(node)) + " is not binary.");
      }
      const uint32_t cmp_w = std::max(pin_width(ctx, drivers[0], node), pin_width(ctx, drivers[1], node));
      const auto a = ucast_pin_at(ctx, drivers[0], cmp_w);
      const auto b = ucast_pin_at(ctx, drivers[1], cmp_w);
      const char* cmp = op == Ntype_op::LT ? "<" : ">";
      if (node_output_is_signed(node)) {
        return "(bool_to_bv1 (BitVec.toInt " + a + " " + cmp + " BitVec.toInt " + b + "))";
      }
      return "(bool_to_bv1 (" + a + " " + cmp + " " + b + "))";
    }

    case Ntype_op::EQ: {
      std::vector<Node_pin> drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        drivers.push_back(e.driver);
      }
      if (drivers.size() <= 1) {
        return lit_one(1);
      }
      uint32_t eq_w = 1;
      for (auto& dp : drivers) {
        eq_w = std::max(eq_w, pin_width(ctx, dp, node));
      }
      const auto head = ucast_pin_at(ctx, drivers[0], eq_w);
      std::string conj;
      for (size_t i = 1; i < drivers.size(); ++i) {
        const auto t = "(" + ucast_pin_at(ctx, drivers[i], eq_w) + " = " + head + ")";
        conj = (i == 1) ? t : "(" + conj + " && " + t + ")";
      }
      return "(bool_to_bv1 " + conj + ")";
    }

    case Ntype_op::SHL: {
      std::vector<Node_pin> drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        drivers.push_back(e.driver);
      }
      if (drivers.size() != 2) {
        fatal(ctx, "SHL node n_" + std::to_string(node_id(node)) + " is not binary.");
      }
      const auto bw = pin_width(ctx, drivers[1], node);
      return "(" + ucast_pin_at(ctx, drivers[0], w) + " <<< (BitVec.toNat " + shift_amount_expr_at(ctx, drivers[1], bw) + "))";
    }

    case Ntype_op::SRA: {
      std::vector<Node_pin> drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        drivers.push_back(e.driver);
      }
      if (drivers.size() == 1) {
        return ucast_pin_at(ctx, drivers[0], w);
      }
      if (drivers.size() != 2) {
        fatal(ctx, "SRA node n_" + std::to_string(node_id(node)) + " is not binary.");
      }
      const auto vw = pin_width(ctx, drivers[0], node);
      auto sw = pin_width(ctx, drivers[1], node);
      if (pin_is_const(drivers[1])) {
        sw = std::max<uint32_t>(sw, minimal_unsigned_const_width(pin_const_value(drivers[1])));
      }
      return "((bv_zext (sem_sra " + ucast_pin_at(ctx, drivers[0], vw) + " "
             + shift_amount_expr_at(ctx, drivers[1], sw) + ")) : BitVec " + std::to_string(w) + ")";
    }

    case Ntype_op::Mux: {
      Node_pin                sel;
      bool                    have_sel = false;
      std::map<int, Node_pin> options;
      uint32_t                sel_w = 1;
      for (const auto& e : inp_edges_ordered(node)) {
        const int pid = e.sink.get_port_id();
        if (pid == 0) {
          sel      = e.driver;
          have_sel = true;
          sel_w    = pin_width(ctx, e.driver, node);
        } else {
          options[pid] = e.driver;
        }
      }
      if (!have_sel || options.size() < 2) {
        fatal(ctx, "Mux node n_" + std::to_string(node_id(node)) + " is malformed.");
      }
      const auto sel_e = driver_expr_at(ctx, sel, sel_w);
      if (options.size() == 2 && sel_w == 1) {
        return "(if bitvec_nonzero " + sel_e + " then " + ucast_pin_at(ctx, options.at(2), w) + " else "
               + ucast_pin_at(ctx, options.at(1), w) + ")";
      }
      std::string out = "(";
      size_t idx = 0;
      for (auto& kv : options) {
        out += "if BitVec.toNat " + sel_e + " = " + std::to_string(idx) + " then " + ucast_pin_at(ctx, kv.second, w) + " else ";
        ++idx;
      }
      out += lit_zero(w) + ")";
      return out;
    }

    case Ntype_op::Sext: {
      std::vector<Node_pin> drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        drivers.push_back(e.driver);
      }
      if (drivers.empty()) {
        fatal(ctx, "Sext node n_" + std::to_string(node_id(node)) + " has no input.");
      }
      return "((bv_sext " + driver_expr(ctx, drivers[0]) + ") : BitVec " + std::to_string(w) + ")";
    }

    case Ntype_op::Get_mask: {
      std::vector<Node_pin> drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        drivers.push_back(e.driver);
      }
      if (drivers.size() != 2) {
        fatal(ctx, "Get_mask node n_" + std::to_string(node_id(node)) + " is not binary.");
      }
      // The mask must be materialized wide enough to address every source bit.
      // LiveHD's canonical zext idiom is Get_mask(a, -1) == zext(a) (cprop.cpp:852):
      // the -1 mask is an all-ones sentinel, NOT a 1-bit 0b1. cgen reads it at a
      // common width cw = max(src_w, mask_w, out_w) (cgen_sim.cpp:201); we mirror
      // that here. Emitting it at the mask pin's declared 1-bit width would make
      // sem_get_mask select only bit 0. For -1 the widened value is all-ones over
      // the source; for a concrete positive pattern the extra high bits are 0.
      const uint32_t mask_src_w = pin_width(ctx, drivers[0], node);
      const uint32_t mask_w     = std::max(mask_src_w, w);
      return "((sem_get_mask " + driver_expr(ctx, drivers[0]) + " " + driver_expr_at(ctx, drivers[1], mask_w)
             + ") : BitVec " + std::to_string(w) + ")";
    }

    case Ntype_op::Set_mask: {
      std::vector<Node_pin> drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        drivers.push_back(e.driver);
      }
      if (drivers.size() != 3) {
        fatal(ctx, "Set_mask node n_" + std::to_string(node_id(node)) + " is not ternary.");
      }
      return "(sem_set_mask " + ucast_pin_at(ctx, drivers[0], w) + " " + driver_expr(ctx, drivers[1]) + " "
             + driver_expr(ctx, drivers[2]) + ")";
    }

    case Ntype_op::Memory:
      fatal(ctx, "Memory node n_" + std::to_string(node_id(node))
                     + " reached pass.lean fast model. Lean memory primitives exist, but Ntype_op::Memory port-policy emission is not ported yet.");

    case Ntype_op::Latch:
    case Ntype_op::Fflop:
    case Ntype_op::Sub:
    case Ntype_op::LUT:
    case Ntype_op::AttrSet:
    case Ntype_op::Hotmux:
      fatal(ctx, "unsupported op `" + std::string(Ntype::get_name(op)) + "` at node n_" + std::to_string(node_id(node)) + ".");

    default: throw Emit_error("internal: unhandled Ntype_op in pass.lean emit_node_expr");
  }
}

std::vector<Node> reachable_topo_order(const std::vector<Node_pin>& roots, const absl::flat_hash_set<uint32_t>& flop_nids) {
  absl::flat_hash_set<uint32_t> reached;
  std::vector<Node>             order;
  std::vector<std::pair<Node, bool>> stack;

  for (const auto& dpin : roots) {
    auto n = pin_node(dpin);
    if (pin_is_input(dpin) || pin_is_const(dpin) || flop_nids.count(node_id(n)) > 0) {
      continue;
    }
    if (reached.count(node_id(n)) > 0) {
      continue;
    }
    stack.emplace_back(n, false);
    while (!stack.empty()) {
      auto& [cur, visited] = stack.back();
      if (visited) {
        if (reached.insert(node_id(cur)).second) {
          order.push_back(cur);
        }
        stack.pop_back();
        continue;
      }
      visited = true;
      for (const auto& e : inp_edges_ordered(cur)) {
        auto child = pin_node(e.driver);
        if (pin_is_input(e.driver) || pin_is_const(e.driver) || flop_nids.count(node_id(child)) > 0) {
          continue;
        }
        if (reached.count(node_id(child)) > 0) {
          continue;
        }
        stack.emplace_back(child, false);
      }
    }
  }
  return order;
}

std::string nat_list(const std::vector<uint32_t>& xs) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < xs.size(); ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << xs[i];
  }
  oss << "]";
  return oss.str();
}

struct CertBuild {
  std::set<uint32_t> source_ids;
  std::map<uint32_t, std::string> source_exprs;
  uint32_t next_synth_id = 1000000000;
};

uint32_t cert_dep_id(const LeanCtx& ctx, CertBuild& build, const Node_pin& pin, uint32_t expected_w) {
  auto n = pin_node(pin);
  if (pin_is_const(pin)) {
    const uint32_t sid = build.next_synth_id++;
    build.source_ids.insert(sid);
    build.source_exprs[sid] = "mk_bv " + std::to_string(expected_w) + " (" + int_of_const(ctx, n, pin_const_value(pin)) + ")";
    return sid;
  }
  if (pin_is_input(pin)) {
    auto pname = input_name_for_pin(ctx, pin);
    auto sid_it = ctx.input_source_id.find(pname);
    if (sid_it == ctx.input_source_id.end()) {
      fatal(ctx, "internal: input source id missing for certificate dependency");
    }
    const auto sid = sid_it->second;
    build.source_ids.insert(sid);
    build.source_exprs[sid] = "mk_bv " + std::to_string(ctx.input_width.at(pname)) + " (Int.ofNat (BitVec.toNat i."
                             + ctx.input_field.at(pname) + "))";
    return sid;
  }
  if (node_is_flop(n)) {
    const auto sid = node_id(n);
    build.source_ids.insert(sid);
    build.source_exprs[sid] = "mk_bv " + std::to_string(ctx.flop_width.at(sid)) + " (Int.ofNat (BitVec.toNat s."
                             + ctx.flop_field.at(sid) + "))";
    return sid;
  }
  return node_id(n);
}

std::string cert_node_expr(const LeanCtx& ctx, CertBuild& build, const Node& node) {
  const auto op = node_op(node);
  const auto w = node_width(ctx, node);
  std::string op_expr;
  std::vector<uint32_t> deps;

  switch (op) {
    case Ntype_op::Nconst:
      op_expr = "LGraphOp.Op_Const (" + int_of_const(ctx, node, node_const_value(node)) + ")";
      break;
    case Ntype_op::Sum: {
      std::vector<uint32_t> adds;
      std::vector<uint32_t> subs;
      for (const auto& e : inp_edges_ordered(node)) {
        if (e.sink.get_port_id() == 0) {
          adds.push_back(cert_dep_id(ctx, build, e.driver, w));
        } else if (e.sink.get_port_id() == 1) {
          subs.push_back(cert_dep_id(ctx, build, e.driver, w));
        }
      }
      deps = adds;
      deps.insert(deps.end(), subs.begin(), subs.end());
      op_expr = "LGraphOp.Op_Sum " + std::to_string(adds.size());
      break;
    }
    case Ntype_op::Mult:
      op_expr = "LGraphOp.Op_Mult";
      for (const auto& e : inp_edges_ordered(node)) {
        deps.push_back(cert_dep_id(ctx, build, e.driver, w));
      }
      break;
    case Ntype_op::Div:
      op_expr = "LGraphOp.Op_UDiv";
      for (const auto& e : inp_edges_ordered(node)) {
        deps.push_back(cert_dep_id(ctx, build, e.driver, w));
      }
      break;
    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor:
    case Ntype_op::Ror:
    case Ntype_op::EQ: {
      if (op == Ntype_op::And) {
        op_expr = "LGraphOp.Op_And";
      } else if (op == Ntype_op::Or) {
        op_expr = "LGraphOp.Op_Or";
      } else if (op == Ntype_op::Xor) {
        op_expr = "LGraphOp.Op_Xor";
      } else if (op == Ntype_op::Ror) {
        op_expr = "LGraphOp.Op_Ror";
      } else {
        op_expr = "LGraphOp.Op_EQ";
      }
      uint32_t dep_w = w;
      if (op == Ntype_op::EQ) {
        dep_w = 1;
        for (const auto& e : inp_edges_ordered(node)) {
          dep_w = std::max(dep_w, pin_width(ctx, e.driver, node));
        }
      }
      for (const auto& e : inp_edges_ordered(node)) {
        deps.push_back(cert_dep_id(ctx, build, e.driver, dep_w));
      }
      break;
    }
    case Ntype_op::Not:
      op_expr = "LGraphOp.Op_Not";
      for (const auto& e : inp_edges_ordered(node)) {
        deps.push_back(cert_dep_id(ctx, build, e.driver, w));
      }
      break;
    case Ntype_op::LT:
    case Ntype_op::GT: {
      const bool is_signed = node_output_is_signed(node);
      if (op == Ntype_op::LT) {
        op_expr = is_signed ? "LGraphOp.Op_SLT" : "LGraphOp.Op_ULT";
      } else {
        op_expr = is_signed ? "LGraphOp.Op_SGT" : "LGraphOp.Op_UGT";
      }
      std::vector<Node_pin> drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        drivers.push_back(e.driver);
      }
      const uint32_t cmp_w = drivers.size() == 2 ? std::max(pin_width(ctx, drivers[0], node), pin_width(ctx, drivers[1], node)) : w;
      for (auto& d : drivers) {
        deps.push_back(cert_dep_id(ctx, build, d, cmp_w));
      }
      break;
    }
    case Ntype_op::SHL:
      op_expr = "LGraphOp.Op_SHL";
      for (const auto& e : inp_edges_ordered(node)) {
        deps.push_back(cert_dep_id(ctx, build, e.driver, pin_width(ctx, e.driver, node)));
      }
      break;
    case Ntype_op::SRA:
      op_expr = "LGraphOp.Op_SRA";
      for (const auto& e : inp_edges_ordered(node)) {
        deps.push_back(cert_dep_id(ctx, build, e.driver, pin_width(ctx, e.driver, node)));
      }
      break;
    case Ntype_op::Mux: {
      uint32_t sel_w = 1;
      size_t data_count = 0;
      for (const auto& e : inp_edges_ordered(node)) {
        if (e.sink.get_port_id() == 0) {
          sel_w = pin_width(ctx, e.driver, node);
          deps.push_back(cert_dep_id(ctx, build, e.driver, sel_w));
        }
      }
      for (const auto& e : inp_edges_ordered(node)) {
        if (e.sink.get_port_id() != 0) {
          deps.push_back(cert_dep_id(ctx, build, e.driver, w));
          ++data_count;
        }
      }
      op_expr = (data_count == 2 && sel_w == 1) ? "LGraphOp.Op_MuxBool" : "LGraphOp.Op_MuxN";
      break;
    }
    case Ntype_op::Sext:
      op_expr = "LGraphOp.Op_Sext";
      for (const auto& e : inp_edges_ordered(node)) {
        deps.push_back(cert_dep_id(ctx, build, e.driver, pin_width(ctx, e.driver, node)));
      }
      break;
    case Ntype_op::Get_mask: {
      op_expr = "LGraphOp.Op_GetMask";
      // Mirror the fast-model widening (see Ntype_op::Get_mask in emit_node_expr
      // and cgen_sim.cpp:201): the mask (port 1) is materialized at
      // max(src_w, out_w) so an all-ones -1 mask selects every source bit. The
      // source operand (port 0) keeps its own width. Keeping this in lockstep
      // with the fast model preserves the model = certificate bridge theorem.
      std::vector<Node_pin> gm_drivers;
      for (const auto& e : inp_edges_ordered(node)) {
        gm_drivers.push_back(e.driver);
      }
      const uint32_t gm_src_w  = gm_drivers.empty() ? w : pin_width(ctx, gm_drivers[0], node);
      const uint32_t gm_mask_w = std::max(gm_src_w, w);
      for (size_t gi = 0; gi < gm_drivers.size(); ++gi) {
        const uint32_t dep_w = (gi == 1) ? gm_mask_w : pin_width(ctx, gm_drivers[gi], node);
        deps.push_back(cert_dep_id(ctx, build, gm_drivers[gi], dep_w));
      }
      break;
    }
    case Ntype_op::Set_mask:
      op_expr = "LGraphOp.Op_SetMask";
      for (const auto& e : inp_edges_ordered(node)) {
        deps.push_back(cert_dep_id(ctx, build, e.driver, pin_width(ctx, e.driver, node)));
      }
      break;
    default:
      fatal(ctx, "unsupported certificate op `" + std::string(Ntype::get_name(op)) + "` at node n_" + std::to_string(node_id(node)) + ".");
  }

  std::ostringstream oss;
  oss << "{ nid := " << node_id(node) << ", op := " << op_expr << ", width := " << w << ", deps := " << nat_list(deps) << " }";
  return oss.str();
}

}  // namespace

Pass_lean::Pass_lean(const Eprp_var& var) : Pass("pass.lean", var) {
  auto s = var.get("strict");
  strict = (s == "false") ? false : true;

  auto n = var.get("normalize");
  normalize = (n == "false") ? false : true;

  auto ec = var.get("emit_cert");
  emit_cert = (ec == "false") ? false : true;

  top = std::string(var.get("top"));
  cert_wf = parse_cert_wf_mode(var.get("cert_wf"));
  cert_wf_fallback = parse_cert_wf_fallback(var.get("cert_wf_fallback"));

  auto ccs = var.get("cert_chunk_size");
  if (!ccs.empty()) {
    try {
      cert_chunk_size = std::stoul(std::string(ccs));
    } catch (...) {
      cert_chunk_size = 25;
    }
  } else {
    cert_chunk_size = 25;
  }
  if (cert_chunk_size == 0) {
    cert_chunk_size = 25;
  }

  auto ccl = var.get("cert_chunk_limit");
  if (!ccl.empty()) {
    try {
      cert_chunk_limit = std::stoul(std::string(ccl));
    } catch (...) {
      cert_chunk_limit = 0;
    }
  } else {
    cert_chunk_limit = 0;
  }

  auto mw = var.get("max_width");
  if (!mw.empty()) {
    try {
      max_width = std::stoul(std::string(mw));
    } catch (...) {
      max_width = 1024;
    }
  } else {
    max_width = 1024;
  }
}

void Pass_lean::setup() {
  Eprp_method m1("pass.lean",
                 "Emit per-design Lean theories for graph-certificate translation proofs.",
                 &Pass_lean::work);
  m1.add_label_optional("path", "Output directory for emitted Lean files.");
  m1.add_label_optional("top", "Top module name override.");
  m1.add_label_optional("strict", "true|false (default true). Abort on unsupported ops.");
  m1.add_label_optional("normalize", "true|false (default true). Normalize pre-export width artifacts.");
  m1.add_label_optional("emit_cert", "true|false (default true). Emit graph certificate and cert-model definitions.");
  m1.add_label_optional("max_width", "Hard cap on node Bits width (default 1024).");
  m1.add_label_optional("cert_wf", "skip|eval|sorry|chunked (default skip). Certificate well-formedness proof mode.");
  m1.add_label_optional("cert_wf_fallback", "fail|sorry|eval for unsupported cert_wf:chunked chunk shapes (default fail).");
  m1.add_label_optional("cert_chunk_size", "Number of node certificates per chunk for cert_wf:chunked (default 25).");
  m1.add_label_optional("cert_chunk_limit", "Emit only first N certificate chunks for proof-shape testing (default 0 = all).");
  register_pass(m1);
}

void Pass_lean::work(Eprp_var& var) {
  Pass_lean pass(var);
  for (const auto& g : var.graphs) {
    pass.emit_for_graph(g);
  }
}

void Pass_lean::emit_for_graph(const std::shared_ptr<hhds::Graph>& graph) const {
  TRACE_EVENT("pass", "LEAN_emit_for_graph");

  if (!graph) {
    livehd::diag::warn("pass.lean", "no-input", "io").msg("received a null Graph instance").emit();
    return;
  }
  auto* g = graph.get();

  const std::string output_dir = (path == "/INVALID" || path.empty()) ? std::string(".") : path;

  const std::string raw_name = top.empty() ? std::string(g->get_name()) : top;
  const std::string base_name = sanitize_lean(raw_name);
  const std::string lean_path = output_dir + "/" + base_name + "_Lgraph.lean";

  LeanCtx ctx;
  ctx.g         = g;
  ctx.base_name = raw_name;
  ctx.top_name  = base_name;
  ctx.strict    = strict;
  ctx.max_width = max_width;

  auto gio = g->get_io();
  uint32_t next_input_source_id = 2000000000;

  for (const auto& decl : gio->get_input_pin_decls()) {
    auto raw = std::string(decl.name);
    auto pin = g->get_input_pin(decl.name);
    auto fld = make_field_name("in_", raw, ctx.used_fields);
    auto w   = static_cast<uint32_t>(livehd::graph_util::bits_of(pin, *gio, decl.name));
    if (w == 0 || w > ctx.max_width) {
      check_width(ctx, pin_node(pin), w, "input port");
    }
    ctx.input_field[raw]     = fld;
    ctx.input_width[raw]     = w;
    ctx.input_source_id[raw] = next_input_source_id++;
  }

  for (const auto& decl : gio->get_output_pin_decls()) {
    auto raw = std::string(decl.name);
    auto pin = g->get_output_pin(decl.name);
    auto fld = make_field_name("out_", raw, ctx.used_fields);
    auto w   = static_cast<uint32_t>(livehd::graph_util::bits_of(pin, *gio, decl.name));
    if (w == 0 || w > ctx.max_width) {
      check_width(ctx, pin_node(pin), w, "output port");
    }
    ctx.output_field[raw] = fld;
    ctx.output_width[raw] = w;
  }

  std::vector<Node>             flop_nodes;
  absl::flat_hash_set<uint32_t> flop_nids;
  bool                          has_memory = false;
  for (auto node : g->fast_class()) {
    if (node_is_flop(node)) {
      flop_nodes.emplace_back(node);
      flop_nids.insert(node_id(node));
      std::string flop_raw;
      for (const auto& e : node.out_edges()) {
        auto wn = livehd::graph_util::wire_name(e.driver);
        if (!wn.empty() && wn[0] != '_') {
          flop_raw = std::string(wn);
          break;
        }
      }
      if (flop_raw.empty()) {
        flop_raw = "flop_" + std::to_string(node_id(node));
      }
      auto fld = make_field_name("st_", flop_raw, ctx.used_fields);
      auto w   = raw_node_width(node);
      check_width(ctx, node, w, "flop");
      ctx.flop_field[node_id(node)] = fld;
      ctx.flop_width[node_id(node)] = w;
    }
    has_memory |= node_is_memory(node);
  }

  if (has_memory && strict) {
    livehd::diag::err("pass.lean", "memory-unsupported", "unsupported")
        .msg("pass.lean found a Memory node in {}; Lean memory primitives exist, but Memory node emission/certificates are not ported yet",
             raw_name)
        .fatal();
  }

  const bool sequential = !flop_nodes.empty();

  std::vector<Node_pin> roots;
  std::map<std::string, Node_pin> out_drivers;
  for (const auto& decl : gio->get_output_pin_decls()) {
    auto out_sink = g->get_output_pin(decl.name);
    auto edges    = out_sink.inp_edges();
    if (!edges.empty()) {
      out_drivers[std::string(decl.name)] = edges.front().driver;
      roots.push_back(edges.front().driver);
    }
  }

  std::map<uint32_t, Node_pin> flop_din, flop_reset, flop_enable;
  for (auto& fn : flop_nodes) {
    for (const auto& e : inp_edges_ordered(fn)) {
      auto pname = sink_pin_name(e);
      if (pname == "din") {
        flop_din[node_id(fn)] = e.driver;
      } else if (pname == "reset_pin" || pname == "negreset") {
        flop_reset[node_id(fn)] = e.driver;
      } else if (pname == "enable") {
        flop_enable[node_id(fn)] = e.driver;
      }
    }
  }
  for (auto& kv : flop_din) {
    roots.push_back(kv.second);
  }
  for (auto& kv : flop_reset) {
    roots.push_back(kv.second);
  }
  for (auto& kv : flop_enable) {
    roots.push_back(kv.second);
  }

  auto topo = reachable_topo_order(roots, flop_nids);

  std::ofstream ofs(lean_path);
  if (!ofs) {
    livehd::diag::warn("pass.lean", "write-failed", "io").msg("could not write {}", lean_path).emit();
    return;
  }

  ofs << "/-\n";
  ofs << "  Generated by LiveHD pass.lean.\n";
  ofs << "  Fast executable model: emitted.\n";
  ofs << "  Certificate model: emitted for the supported non-memory graph subset.\n";
  ofs << "-/\n\n";
  ofs << "import LeanSemanticPrimitives\n\n";
  ofs << "set_option linter.unusedVariables false\n\n";
  ofs << "set_option maxRecDepth 1000000\n";
  ofs << "set_option maxHeartbeats 0\n\n";
  ofs << "namespace " << base_name << "_Lgraph\n\n";

  try {
    ofs << "structure " << base_name << "_in where\n";
    if (ctx.input_field.empty()) {
      ofs << "  in_dummy : BitVec 1\n";
    } else {
      for (const auto& kv : ctx.input_field) {
        ofs << "  " << kv.second << " : BitVec " << ctx.input_width.at(kv.first) << "\n";
      }
    }
    ofs << "deriving Repr, Inhabited\n\n";

    ofs << "structure " << base_name << "_out where\n";
    if (ctx.output_field.empty()) {
      ofs << "  out_dummy : BitVec 1\n";
    } else {
      for (const auto& kv : ctx.output_field) {
        ofs << "  " << kv.second << " : BitVec " << ctx.output_width.at(kv.first) << "\n";
      }
    }
    ofs << "deriving Repr, Inhabited\n\n";

    if (sequential) {
      ofs << "structure " << base_name << "_state where\n";
      for (const auto& kv : ctx.flop_field) {
        ofs << "  " << kv.second << " : BitVec " << ctx.flop_width.at(kv.first) << "\n";
      }
      ofs << "deriving Repr, Inhabited\n\n";
    }

    auto emit_let_chain = [&](std::ostream& os, const std::string& result_expr) {
      for (const auto& n : topo) {
        auto w   = node_width(ctx, n);
        auto rhs = emit_node_expr(ctx, n);
        os << "  let n_" << node_id(n) << " : BitVec " << w << " := " << rhs << "\n";
      }
      os << result_expr;
    };

    if (sequential) {
      ofs << "def " << base_name << "_comb (i : " << base_name << "_in) (s : " << base_name << "_state) : "
          << base_name << "_out :=\n";
    } else {
      ofs << "def " << base_name << "_comb (i : " << base_name << "_in) : " << base_name << "_out :=\n";
    }
    std::ostringstream comb_result;
    comb_result << "  { ";
    bool first_out = true;
    if (ctx.output_field.empty()) {
      comb_result << "out_dummy := " << lit_zero(1);
    } else {
      for (const auto& kv : ctx.output_field) {
        if (!first_out) {
          comb_result << ", ";
        }
        first_out = false;
        const auto& out_name = kv.first;
        auto drv = out_drivers.find(out_name);
        comb_result << kv.second << " := ";
        if (drv == out_drivers.end()) {
          comb_result << lit_zero(ctx.output_width.at(out_name));
        } else {
          comb_result << ucast_pin_at(ctx, drv->second, ctx.output_width.at(out_name));
        }
      }
    }
    comb_result << " }\n\n";
    emit_let_chain(ofs, comb_result.str());

    if (sequential) {
      ofs << "def " << base_name << "_next (i : " << base_name << "_in) (s : " << base_name << "_state) : "
          << base_name << "_state :=\n";
      for (const auto& n : topo) {
        auto w   = node_width(ctx, n);
        auto rhs = emit_node_expr(ctx, n);
        ofs << "  let n_" << node_id(n) << " : BitVec " << w << " := " << rhs << "\n";
      }
      for (auto& fn : flop_nodes) {
        const auto fid = node_id(fn);
        const auto fld = ctx.flop_field.at(fid);
        const auto fw  = ctx.flop_width.at(fid);
        std::string din_e = lit_zero(fw);
        if (auto it = flop_din.find(fid); it != flop_din.end()) {
          din_e = ucast_pin_at(ctx, it->second, fw);
        }
        std::string reset_e = "false";
        if (auto it = flop_reset.find(fid); it != flop_reset.end()) {
          reset_e = "(bitvec_nonzero " + driver_expr_at(ctx, it->second, 1) + ")";
        }
        std::string en_e = "true";
        if (auto it = flop_enable.find(fid); it != flop_enable.end()) {
          en_e = "(bitvec_nonzero " + driver_expr_at(ctx, it->second, 1) + ")";
        }
        ofs << "  let new_" << fld << " : BitVec " << fw << " := flop_next " << reset_e << " "
            << lit_zero(fw) << " " << en_e << " " << din_e << " s." << fld << "\n";
      }
      ofs << "  { ";
      bool first_field = true;
      for (auto& fn : flop_nodes) {
        const auto fld = ctx.flop_field.at(node_id(fn));
        if (!first_field) {
          ofs << ", ";
        }
        first_field = false;
        ofs << fld << " := new_" << fld;
      }
      ofs << " }\n\n";

      ofs << "def " << base_name << "_step (i : " << base_name << "_in) (s : " << base_name << "_state) : "
          << base_name << "_state × " << base_name << "_out :=\n";
      ofs << "  (" << base_name << "_next i s, " << base_name << "_comb i s)\n\n";
    }
  } catch (const Emit_error& err) {
    ofs.close();
    std::cerr << err.what() << "\n";
    if (strict) {
      std::remove(lean_path.c_str());
      livehd::diag::err("pass.lean", "lean-error", "internal").msg("{}", err.what()).fatal();
    }
    return;
  }

  if (!emit_cert) {
    ofs << "-- Certificate emission disabled by lean.emit_cert=false.\n";
    ofs << "end " << base_name << "_Lgraph\n";
    ofs.close();

    std::cout << "pass.lean: " << raw_name << " -> " << lean_path << " (" << topo.size() << " nodes, "
              << flop_nodes.size() << " flops, sequential=" << (sequential ? "yes" : "no")
              << ", certificate disabled)\n";
    return;
  }

  CertBuild cert_build;
  std::vector<std::string> cert_nodes;
  std::vector<uint32_t> topo_ids;
  for (const auto& n : topo) {
    topo_ids.push_back(node_id(n));
    cert_nodes.push_back(cert_node_expr(ctx, cert_build, n));
  }

  std::map<std::string, uint32_t> output_cert_ids;
  for (const auto& kv : ctx.output_field) {
    const auto& out_name = kv.first;
    auto        drv      = out_drivers.find(out_name);
    if (drv != out_drivers.end()) {
      output_cert_ids[out_name] = cert_dep_id(ctx, cert_build, drv->second, ctx.output_width.at(out_name));
    }
  }

  std::map<uint32_t, uint32_t> flop_din_cert_ids;
  std::map<uint32_t, uint32_t> flop_reset_cert_ids;
  std::map<uint32_t, uint32_t> flop_enable_cert_ids;
  for (auto& fn : flop_nodes) {
    const auto fid = node_id(fn);
    const auto fw  = ctx.flop_width.at(fid);
    if (auto it = flop_din.find(fid); it != flop_din.end()) {
      flop_din_cert_ids[fid] = cert_dep_id(ctx, cert_build, it->second, fw);
    }
    if (auto it = flop_reset.find(fid); it != flop_reset.end()) {
      flop_reset_cert_ids[fid] = cert_dep_id(ctx, cert_build, it->second, 1);
    }
    if (auto it = flop_enable.find(fid); it != flop_enable.end()) {
      flop_enable_cert_ids[fid] = cert_dep_id(ctx, cert_build, it->second, 1);
    }
  }

  std::vector<uint32_t> source_ids(cert_build.source_ids.begin(), cert_build.source_ids.end());

  ofs << "def " << base_name << "_nodeCerts : List NodeCert := [\n";
  for (size_t i = 0; i < cert_nodes.size(); ++i) {
    ofs << "  " << cert_nodes[i];
    if (i + 1 != cert_nodes.size()) {
      ofs << ",";
    }
    ofs << "\n";
  }
  ofs << "]\n\n";

  if (sequential) {
    ofs << "def " << base_name << "_sourceEnv (i : " << base_name << "_in) (s : " << base_name << "_state) : Nat -> BV := fun n =>\n";
  } else {
    ofs << "def " << base_name << "_sourceEnv (i : " << base_name << "_in) : Nat -> BV := fun n =>\n";
  }
  for (const auto& sid : source_ids) {
    ofs << "  if n = " << sid << " then " << cert_build.source_exprs.at(sid) << " else\n";
  }
  ofs << "  mk_bv 0 0\n\n";

  ofs << "def " << base_name << "_graphCert : GraphCert :=\n";
  ofs << "  { topo := " << nat_list(topo_ids) << ", sources := " << nat_list(source_ids)
      << ", nodes := nodes_of_list " << base_name << "_nodeCerts }\n\n";

  ofs << "def " << base_name << "_outputsFromCert (rho : Nat -> BV) : " << base_name << "_out :=\n";
  ofs << "  { ";
  bool first_cert_out = true;
  if (ctx.output_field.empty()) {
    ofs << "out_dummy := " << lit_zero(1);
  } else {
    for (const auto& kv : ctx.output_field) {
      const auto& out_name = kv.first;
      if (!first_cert_out) {
        ofs << ", ";
      }
      first_cert_out = false;
      ofs << kv.second << " := ";
      auto oid = output_cert_ids.find(out_name);
      if (oid == output_cert_ids.end()) {
        ofs << lit_zero(ctx.output_width.at(out_name));
      } else {
        ofs << "(bv_to_bitvec " << ctx.output_width.at(out_name) << " (rho " << oid->second << "))";
      }
    }
  }
  ofs << " }\n\n";

  auto eval_expr = [&]() {
    if (sequential) {
      return "evalGraph " + base_name + "_graphCert.topo " + base_name + "_graphCert (" + base_name + "_sourceEnv i s)";
    }
    return "evalGraph " + base_name + "_graphCert.topo " + base_name + "_graphCert (" + base_name + "_sourceEnv i)";
  };

  if (sequential) {
    ofs << "def " << base_name << "_nextStateFromCert (rho : Nat -> BV) (s : " << base_name << "_state) : "
        << base_name << "_state :=\n";
    for (auto& fn : flop_nodes) {
      const auto fid = node_id(fn);
      const auto fld = ctx.flop_field.at(fid);
      const auto fw  = ctx.flop_width.at(fid);
      const auto dit = flop_din_cert_ids.find(fid);
      const auto rit = flop_reset_cert_ids.find(fid);
      const auto eit = flop_enable_cert_ids.find(fid);
      std::string din_e = lit_zero(fw);
      if (dit != flop_din_cert_ids.end()) {
        din_e = "(bv_to_bitvec " + std::to_string(fw) + " (rho " + std::to_string(dit->second) + "))";
      }
      std::string reset_e = "false";
      if (rit != flop_reset_cert_ids.end()) {
        reset_e = "(bv_nonzero (rho " + std::to_string(rit->second) + "))";
      }
      std::string en_e = "true";
      if (eit != flop_enable_cert_ids.end()) {
        en_e = "(bv_nonzero (rho " + std::to_string(eit->second) + "))";
      }
      ofs << "  let new_" << fld << " : BitVec " << fw << " := flop_next " << reset_e << " "
          << lit_zero(fw) << " " << en_e << " " << din_e << " s." << fld << "\n";
    }
    ofs << "  { ";
    bool first_field = true;
    for (auto& fn : flop_nodes) {
      const auto fld = ctx.flop_field.at(node_id(fn));
      if (!first_field) {
        ofs << ", ";
      }
      first_field = false;
      ofs << fld << " := new_" << fld;
    }
    ofs << " }\n\n";

    ofs << "def " << base_name << "_comb_cert (i : " << base_name << "_in) (s : " << base_name << "_state) : "
        << base_name << "_out :=\n";
    ofs << "  " << base_name << "_outputsFromCert (" << eval_expr() << ")\n\n";

    ofs << "def " << base_name << "_next_cert (i : " << base_name << "_in) (s : " << base_name << "_state) : "
        << base_name << "_state :=\n";
    ofs << "  " << base_name << "_nextStateFromCert (" << eval_expr() << ") s\n\n";

    ofs << "def " << base_name << "_step_cert (i : " << base_name << "_in) (s : " << base_name << "_state) : "
        << base_name << "_state × " << base_name << "_out :=\n";
    ofs << "  (" << base_name << "_next_cert i s, " << base_name << "_comb_cert i s)\n\n";

    ofs << "theorem " << base_name << "_comb_cert_refines_cert (i : " << base_name << "_in) (s : " << base_name << "_state) :\n";
    ofs << "    " << base_name << "_comb_cert i s = " << base_name << "_outputsFromCert (" << eval_expr() << ") := by\n";
    ofs << "  rfl\n\n";

    ofs << "theorem " << base_name << "_next_cert_refines_cert (i : " << base_name << "_in) (s : " << base_name << "_state) :\n";
    ofs << "    " << base_name << "_next_cert i s = " << base_name << "_nextStateFromCert (" << eval_expr() << ") s := by\n";
    ofs << "  rfl\n\n";

    ofs << "theorem " << base_name << "_step_cert_refines_lgraph_certificate (i : " << base_name << "_in) (s : " << base_name << "_state) :\n";
    ofs << "    " << base_name << "_step_cert i s = (" << base_name << "_nextStateFromCert (" << eval_expr() << ") s, "
        << base_name << "_outputsFromCert (" << eval_expr() << ")) := by\n";
    ofs << "  rfl\n\n";
  } else {
    ofs << "def " << base_name << "_comb_cert (i : " << base_name << "_in) : " << base_name << "_out :=\n";
    ofs << "  " << base_name << "_outputsFromCert (" << eval_expr() << ")\n\n";

    ofs << "theorem " << base_name << "_comb_cert_refines_cert (i : " << base_name << "_in) :\n";
    ofs << "    " << base_name << "_comb_cert i = " << base_name << "_outputsFromCert (" << eval_expr() << ") := by\n";
    ofs << "  rfl\n\n";
  }

  if (sequential) {
    ofs << "theorem " << base_name << "_evalGraph_correct (i : " << base_name << "_in) (s : " << base_name << "_state) :\n";
    ofs << "    envCorrectOn " << base_name << "_graphCert.topo\n";
    ofs << "      (evalGraph " << base_name << "_graphCert.topo " << base_name << "_graphCert (" << base_name << "_sourceEnv i s))\n";
    ofs << "      (graphDenotation " << base_name << "_graphCert.topo " << base_name << "_graphCert (" << base_name << "_sourceEnv i s)) := by\n";
    ofs << "  exact evalGraphCorrectForCert " << base_name << "_graphCert (" << base_name << "_sourceEnv i s)\n\n";
  } else {
    ofs << "theorem " << base_name << "_evalGraph_correct (i : " << base_name << "_in) :\n";
    ofs << "    envCorrectOn " << base_name << "_graphCert.topo\n";
    ofs << "      (evalGraph " << base_name << "_graphCert.topo " << base_name << "_graphCert (" << base_name << "_sourceEnv i))\n";
    ofs << "      (graphDenotation " << base_name << "_graphCert.topo " << base_name << "_graphCert (" << base_name << "_sourceEnv i)) := by\n";
    ofs << "  exact evalGraphCorrectForCert " << base_name << "_graphCert (" << base_name << "_sourceEnv i)\n\n";
  }

  if (cert_wf == LeanCertWFMode::Sorry) {
    ofs << "theorem " << base_name << "_graphCert_wf : graphCertWf " << base_name << "_graphCert := by\n";
    ofs << "  simp [" << base_name << "_graphCert, graphCertWf]\n\n";
  } else {
    ofs << "-- graphCertWf proof emission is pending full graph certificate output.\n";
  }

  ofs << "end " << base_name << "_Lgraph\n";
  ofs.close();

  std::cout << "pass.lean: " << raw_name << " -> " << lean_path << " (" << topo.size() << " nodes, "
            << flop_nodes.size() << " flops, sequential=" << (sequential ? "yes" : "no")
            << ", certificate shell)\n";
}
