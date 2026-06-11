//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
//  pass.isabelle - emit a per-design Isabelle theory <Top>_Lgraph.thy
//  See plan 20 (lovely-popping-canyon.md).
//
//  Scope (v1, strict mode default):
//    Combinational: Dlop, Sum, Mult, Div, And, Or, Xor, Ror, Not, LT, GT, EQ,
//                   SHL, SRA, Mux, Sext, Get_mask, Set_mask, IO
//    Stateful:      Flop only.
//    Rejected:      Latch, Fflop, Memory, Sub, LUT, AttrGet/AttrSet, Tposs.

#include "pass_isabelle.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
#include "hlop/dlop.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "perf_tracing.hpp"

static Pass_plugin pass_plugin_isabelle("pass_isabelle", Pass_isabelle::setup);

namespace {

using Node     = hhds::Node_class;
using Node_pin = hhds::Pin_class;
using Edge     = hhds::Edge_class;

uint32_t node_id(const Node &node) {
  return static_cast<uint32_t>(node.get_debug_nid());
}

Node pin_node(const Node_pin &pin) {
  return pin.get_master_node();
}

Ntype_op node_op(const Node &node) {
  return livehd::graph_util::type_op_of(node);
}

bool node_is_op(const Node &node, Ntype_op op) {
  return node_op(node) == op;
}

bool node_is_const(const Node &node) {
  return livehd::graph_util::is_type_const(node) || node_op(node) == Ntype_op::Nconst;
}

bool node_is_flop(const Node &node) {
  return livehd::graph_util::is_type_flop(node);
}

bool pin_is_input(const Node_pin &pin) {
  return livehd::graph_util::is_graph_input_pin(pin);
}

bool pin_is_const(const Node_pin &pin) {
  return livehd::graph_util::is_const_pin(pin);
}

bool same_pin(const Node_pin &a, const Node_pin &b) {
  return !a.is_invalid() && !b.is_invalid() && a.get_class_index() == b.get_class_index();
}

std::vector<Edge> inp_edges_ordered(const Node &node) {
  auto edges = node.inp_edges();
  std::sort(edges.begin(), edges.end(), [](const Edge &a, const Edge &b) {
    const auto ap = a.sink.get_port_id();
    const auto bp = b.sink.get_port_id();
    if (ap != bp) return ap < bp;
    return a.driver.get_class_index().value < b.driver.get_class_index().value;
  });
  return edges;
}

std::string sink_pin_name(const Edge &edge) {
  const auto sink_node = pin_node(edge.sink);
  return std::string(Ntype::get_sink_name(node_op(sink_node), edge.sink.get_port_id()));
}

uint32_t raw_pin_width(const Node_pin &pin) {
  return static_cast<uint32_t>(livehd::graph_util::bits_of(pin));
}

uint32_t raw_node_width(const Node &node) {
  return raw_pin_width(node.create_driver_pin(0));
}

Dlop pin_const_value(const Node_pin &pin) {
  return livehd::graph_util::hydrate_const(pin);
}

Dlop node_const_value(const Node &node) {
  return livehd::graph_util::hydrate_const(node);
}

CertWFMode parse_cert_wf_mode(std::string_view mode) {
  if (mode == "eval") {
    return CertWFMode::Eval;
  }
  if (mode == "sorry") {
    return CertWFMode::Sorry;
  }
  if (mode == "chunked") {
    return CertWFMode::Chunked;
  }
  return CertWFMode::Skip;
}

CertWFFallback parse_cert_wf_fallback(std::string_view mode) {
  if (mode == "sorry") {
    return CertWFFallback::Sorry;
  }
  if (mode == "eval") {
    return CertWFFallback::Eval;
  }
  return CertWFFallback::Fail;
}

}  // namespace

Pass_isabelle::Pass_isabelle(const Eprp_var &var) : Pass("pass.isabelle", var) {
  // strict defaults to true (regression mode)
  auto s = var.get("strict");
  strict = (s == "false") ? false : true;
  auto n = var.get("normalize");
  normalize = (n == "false") ? false : true;
  top    = std::string(var.get("top"));
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

void Pass_isabelle::setup() {
  Eprp_method m1("pass.isabelle",
                 "Emit per-design Isabelle theories (combinational + Flop) for `by eval` regression.",
                 &Pass_isabelle::work);
  m1.add_label_optional("path", "Output directory for emitted *_Lgraph.thy");
  m1.add_label_optional("top", "Top module name (informational only)");
  m1.add_label_optional("strict", "true|false (default true). Abort on unsupported ops.");
  m1.add_label_optional("normalize", "true|false (default true). Normalize pre-export width artifacts.");
  m1.add_label_optional("max_width", "Hard cap on node Bits width (default 1024).");
  m1.add_label_optional("cert_wf", "skip|eval|sorry|chunked (default skip). Certificate well-formedness proof mode.");
  m1.add_label_optional("cert_wf_fallback", "fail|sorry|eval for unsupported cert_wf:chunked chunk shapes (default fail).");
  m1.add_label_optional("cert_chunk_size", "Number of node certificates per chunk for cert_wf:chunked (default 25).");
  m1.add_label_optional("cert_chunk_limit", "Emit only the first N certificate chunks for proof-shape testing (default 0 = all).");
  register_pass(m1);
}

void Pass_isabelle::work(Eprp_var &var) {
  Pass_isabelle pass(var);
  for (const auto &g : var.graphs) {
    pass.emit_for_graph(g);
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Reserved Isabelle keywords we must not use as field/variable names.
const std::unordered_set<std::string> kIsabelleReserved = {
    "type",  "record",     "let",     "if",     "then",   "else",  "case",
    "end",   "fun",        "definition", "primrec", "where",  "imports",
    "begin", "datatype",   "lemma",   "theorem", "by",     "assume",
    "show",  "obtain",     "fix",     "do",      "in",     "of",
    "and",   "or",         "not",     "True",    "False",  "for",
};

// Sanitize an RTL identifier so it is a valid Isabelle identifier:
//   1. Replace any non-[A-Za-z0-9_] character with `_chrXX_` (XX = hex).
//   2. If the first char is a digit, prefix `id_`.
//   3. If the result clashes with a reserved keyword, prefix `id_`.
std::string sanitize(std::string_view name) {
  std::string out;
  out.reserve(name.size() + 4);
  for (unsigned char c : name) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (ok) {
      out.push_back(static_cast<char>(c));
    } else {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "_chr%x_", c);
      out += buf;
    }
  }
  if (out.empty()) {
    return "id_";
  }
  if (out[0] >= '0' && out[0] <= '9') {
    out = "id_" + out;
  }
  if (kIsabelleReserved.count(out) > 0) {
    out = "id_" + out;
  }
  return out;
}

// Compose a role-prefixed, globally-unique selector name for a record field.
std::string make_field_name(std::string_view role, std::string_view rtl_name,
                            absl::flat_hash_set<std::string> &used) {
  std::string base = std::string(role) + sanitize(rtl_name);
  std::string name = base;
  size_t      n    = 0;
  while (used.count(name) > 0 || kIsabelleReserved.count(name) > 0) {
    ++n;
    name = base + "_" + std::to_string(n);
  }
  used.insert(name);
  return name;
}

// Render a width as a literal numeric Isabelle word annotation: ":: <w> word".
std::string ty_word(uint32_t w) { return ":: " + std::to_string(w) + " word"; }

}  // namespace

// ---------------------------------------------------------------------------
// Per-graph emission
// ---------------------------------------------------------------------------

namespace {

struct Emit_error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Format an explicit 1-bit / fallback constant of width `w`.
std::string lit_zero(uint32_t w) { return "(0 :: " + std::to_string(w) + " word)"; }
std::string lit_one (uint32_t w) { return "(1 :: " + std::to_string(w) + " word)"; }

// Format a width-annotated `ucast` of an inner expression.
std::string ucast_at(const std::string &expr, uint32_t w) {
  return "((ucast (" + expr + ") :: " + std::to_string(w) + " word))";
}

// Format a numeric literal of width w.
std::string lit_const_pos(int64_t v, uint32_t w) {
  return "(" + std::to_string(v) + " :: " + std::to_string(w) + " word)";
}
std::string lit_const_neg(int64_t v, uint32_t w) {
  return "((word_of_int (" + std::to_string(v) + ")) :: " + std::to_string(w) + " word)";
}

std::string cert_op_tag_from_expr(const std::string &expr) {
  const std::string marker = "op = ";
  auto pos = expr.find(marker);
  if (pos == std::string::npos) {
    return "unknown";
  }
  pos += marker.size();
  auto end = expr.find_first_of(" ,", pos);
  if (end == std::string::npos) {
    return expr.substr(pos);
  }
  return expr.substr(pos, end - pos);
}

std::string chunk_op_summary(const std::vector<std::string> &tags, size_t begin, size_t end) {
  std::map<std::string, size_t> counts;
  for (size_t i = begin; i < end && i < tags.size(); ++i) {
    counts[tags[i]]++;
  }
  std::ostringstream oss;
  bool first = true;
  for (const auto &kv : counts) {
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << kv.first << "=" << kv.second;
  }
  return oss.str();
}

bool is_simple_cert_op_tag(const std::string &tag) {
  static const std::unordered_set<std::string> simple_ops = {
    "Op_Const", "Op_Sum", "Op_And", "Op_Or", "Op_Xor", "Op_Ror", "Op_Not", "Op_EQ",
    "Op_ULT", "Op_UGT", "Op_SLT", "Op_SGT", "Op_GetMask", "Op_MuxBool",
    "Op_MuxN", "Op_SHL", "Op_SRA", "Op_Sext"
  };
  return simple_ops.count(tag) != 0;
}

bool is_simple_cert_chunk(const std::vector<std::string> &tags, size_t begin, size_t end) {
  for (size_t i = begin; i < end && i < tags.size(); ++i) {
    if (!is_simple_cert_op_tag(tags[i])) {
      return false;
    }
  }
  return begin < end;
}

std::vector<uint32_t> chunk_dep_ids(const std::vector<std::string> &exprs, size_t begin, size_t end) {
  std::set<uint32_t> deps;
  const std::string marker = "deps = [";
  for (size_t i = begin; i < end && i < exprs.size(); ++i) {
    const auto &expr = exprs[i];
    auto pos = expr.find(marker);
    if (pos == std::string::npos) {
      continue;
    }
    pos += marker.size();
    auto close = expr.find("]", pos);
    if (close == std::string::npos || close <= pos) {
      continue;
    }
    std::string deps_text = expr.substr(pos, close - pos);
    std::stringstream ss(deps_text);
    std::string item;
    while (std::getline(ss, item, ',')) {
      auto first = item.find_first_not_of(" \t\n\r");
      auto last = item.find_last_not_of(" \t\n\r");
      if (first == std::string::npos || last == std::string::npos) {
        continue;
      }
      auto trimmed = item.substr(first, last - first + 1);
      try {
        deps.insert(static_cast<uint32_t>(std::stoul(trimmed)));
      } catch (...) {
      }
    }
  }
  return std::vector<uint32_t>(deps.begin(), deps.end());
}

std::vector<size_t> needed_id_chunks(const std::vector<uint32_t> &deps,
                                     const std::vector<uint32_t> &all_ids,
                                     size_t chunk_size,
                                     size_t chunk_count) {
  std::set<size_t> chunks;
  if (chunk_size == 0) {
    return {};
  }

  std::map<uint32_t, size_t> id_to_chunk;
  for (size_t i = 0; i < all_ids.size(); ++i) {
    const size_t chunk = i / chunk_size;
    if (chunk < chunk_count) {
      id_to_chunk.emplace(all_ids[i], chunk);
    }
  }

  for (auto dep : deps) {
    auto it = id_to_chunk.find(dep);
    if (it != id_to_chunk.end()) {
      chunks.insert(it->second);
    }
  }

  return std::vector<size_t>(chunks.begin(), chunks.end());
}

std::string undefined_at(uint32_t w, const std::string &reason) {
  return "(undefined :: " + std::to_string(w) + " word) (* TODO pass.isabelle: " + reason + " *)";
}

// Per-graph context.
struct Ctx {
  hhds::Graph *g;
  std::string top_name;        // sanitized top module name
  std::string base_name;       // raw top module name (for messages)
  bool        strict;
  bool        normalize;
  size_t      max_width;
  CertWFMode  cert_wf;
  CertWFFallback cert_wf_fallback;
  size_t      cert_chunk_size;
  size_t      cert_chunk_limit;
  std::string output_dir;

  // Field name registries.
  absl::flat_hash_set<std::string> used_fields;

  // Map: graph-input pin name -> field selector name
  std::map<std::string, std::string> input_field;
  std::map<std::string, uint32_t>    input_width;
  std::map<std::string, uint32_t>    input_source_id;
  // Map: graph-output pin name -> field selector name
  std::map<std::string, std::string> output_field;
  std::map<std::string, uint32_t>    output_width;
  // Map: flop nid -> field selector name
  std::map<uint32_t, std::string>    flop_field;
  std::map<uint32_t, uint32_t>       flop_width;

  // Reverse map: rtl name -> isabelle field (for name-map header).
  std::vector<std::tuple<std::string, std::string, std::string>> name_map_rows;
};

[[noreturn]] void fatal(const Ctx & /*ctx*/, const std::string &msg) {
  throw Emit_error("[ERROR] pass.isabelle: " + msg);
}

std::string input_name_for_pin(const Ctx &ctx, const Node_pin &pin) {
  if (ctx.g == nullptr) {
    return {};
  }
  for (const auto &kv : ctx.input_field) {
    auto ipin = ctx.g->get_input_pin(kv.first);
    if (same_pin(ipin, pin)) {
      return kv.first;
    }
  }
  auto pname = std::string(livehd::graph_util::pin_name_of(pin));
  if (!pname.empty() && ctx.input_field.contains(pname)) {
    return pname;
  }
  return {};
}

void check_width(const Ctx &ctx, const Node &node, uint32_t w, std::string_view what) {
  if (w == 0) {
    fatal(ctx, "node n_" + std::to_string(node_id(node)) + " (" + std::string(what)
                   + ") has zero-width Bits attribute; Isabelle word types require LENGTH('a) >= 1.");
  }
  if (w > ctx.max_width) {
    fatal(ctx, "node n_" + std::to_string(node_id(node)) + " (" + std::string(what)
                   + ") has width " + std::to_string(w) + " > max_width=" + std::to_string(ctx.max_width));
  }
}

uint32_t pin_width(const Ctx &ctx, const Node_pin &pin, const Node &owner) {
  auto w = raw_pin_width(pin);
  if (w == 0 || static_cast<size_t>(w) > ctx.max_width) {
    // Yosys/LiveHD can represent untyped constants such as 0 as zero-width
    // constants. Their real width is determined by the consumer, so return a
    // minimal width here and let driver_expr_at/ucast_pin_at specialize them.
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

uint32_t node_width(const Ctx &ctx, const Node &node) {
  auto w = raw_node_width(node);
  check_width(ctx, node, w, "node");
  return static_cast<uint32_t>(w);
}

std::string lit_const_at(const Ctx &ctx, const Node &node, const Dlop &v, uint32_t w) {
  if (w == 0 || static_cast<size_t>(w) > ctx.max_width) {
    if (ctx.strict) check_width(ctx, node, w, "Const");
    return undefined_at(1, "Const node n_" + std::to_string(node_id(node))
                              + " has invalid width " + std::to_string(w));
  }
  if (v.has_unknowns()) {
    if (ctx.strict) {
      fatal(ctx, "Const node n_" + std::to_string(node_id(node))
                     + " has X/Z bits; v1 strict mode rejects four-valued logic.");
    }
    return undefined_at(w, "Const node n_" + std::to_string(node_id(node)) + " has X/Z bits");
  }
  if (v.is_known_zero()) return lit_zero(w);
  if (v.same_repr(*Dlop::create_integer(-1))) return "((word_of_int (-1)) :: " + std::to_string(w) + " word)";
  if (v.is_just_i64()) {
    const int64_t iv = v.to_just_i64();
    if (iv < 0) return lit_const_neg(iv, w);
    return lit_const_pos(iv, w);
  }
  auto [mask_lo, mask_hi] = v.get_mask_range();
  if (mask_lo == 0 && static_cast<uint32_t>(mask_hi) <= w) {
    return "((2 ^ " + std::to_string(w) + " - 1) :: " + std::to_string(w) + " word)";
  }
  if (ctx.strict) {
    fatal(ctx, "Const node n_" + std::to_string(node_id(node))
                   + " is a non-mask arbitrary-width constant; pass.isabelle needs a decimal emitter.");
  }
  return undefined_at(w, "Const node n_" + std::to_string(node_id(node))
                         + " is a non-mask arbitrary-width constant");
}

// Driver expression: how the consumer references the value produced by `dpin`.
// - graph input    -> "in_<field> i"
// - flop output    -> "st_<field> s"
// - inlined Dlop  -> direct numeral (positive) or word_of_int (negative)
// - any other node -> "n_<nid>"  (the let-binding name)
std::string driver_expr(const Ctx &ctx, const Node_pin &dpin) {
  auto driver_node = pin_node(dpin);
  if (pin_is_input(dpin)) {
    auto pname = input_name_for_pin(ctx, dpin);
    auto it    = ctx.input_field.find(pname);
    if (it == ctx.input_field.end()) {
      throw Emit_error("internal: graph-input pin '" + pname + "' has no input field");
    }
    return it->second + " i";
  }
  if (node_is_flop(driver_node)) {
    auto fit = ctx.flop_field.find(node_id(driver_node));
    if (fit == ctx.flop_field.end()) {
      throw Emit_error("internal: flop n_" + std::to_string(node_id(driver_node)) + " has no state field");
    }
    return fit->second + " s";
  }
  if (pin_is_const(dpin)) {
    auto v       = pin_const_value(dpin);
    auto w       = raw_pin_width(dpin);
    if (w == 0) {
      w = std::max<uint32_t>(1, static_cast<uint32_t>(v.get_bits()));
    }
    return lit_const_at(ctx, driver_node, v, w);
  }
  return "n_" + std::to_string(node_id(driver_node));
}

std::string driver_expr_at(const Ctx &ctx, const Node_pin &dpin, uint32_t expected_w) {
  auto driver_node = pin_node(dpin);
  if (pin_is_const(dpin)) {
    return lit_const_at(ctx, driver_node, pin_const_value(dpin), expected_w);
  }
  return driver_expr(ctx, dpin);
}

std::string ucast_pin_at(const Ctx &ctx, const Node_pin &dpin, uint32_t w) {
  return ucast_at(driver_expr_at(ctx, dpin, w), w);
}

// Format an n-ary infix operator using lowercase function-name form
//   to avoid Isabelle's `OR`/`AND`/`XOR` infix-vs-token parser ambiguity in
//   nested let-chains. Result is parenthesized.
std::string nary_op_func(const std::string &fname, const std::vector<std::string> &operands) {
  if (operands.empty()) {
    throw Emit_error("internal: empty operand list for " + fname);
  }
  std::string out = operands[0];
  for (size_t i = 1; i < operands.size(); ++i) {
    out = "(" + fname + " " + out + " " + operands[i] + ")";
  }
  return out;
}

// Format an n-ary infix operator using inline `+`/`-`/`*` (which DO work fine
// inline because they are not bit_operations typeclass methods that conflict
// with the parser's notion of word arithmetic).
std::string nary_op_inline(const std::string &op, const std::vector<std::string> &operands) {
  if (operands.empty()) {
    throw Emit_error("internal: empty operand list for " + op);
  }
  std::string out = operands[0];
  for (size_t i = 1; i < operands.size(); ++i) {
    out = "(" + out + " " + op + " " + operands[i] + ")";
  }
  return out;
}

std::string nat_list(const std::vector<uint32_t> &xs) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < xs.size(); ++i) {
    if (i != 0) oss << ", ";
    oss << xs[i];
  }
  oss << "]";
  return oss.str();
}

std::string int_of_const(const Ctx &ctx, const Node &node, const Dlop &v, uint32_t w) {
  if (v.has_unknowns()) {
    fatal(ctx, "Const node n_" + std::to_string(node_id(node))
                   + " has X/Z bits; certificate strict mode rejects four-valued logic.");
  }
  if (v.is_known_zero()) return "0";
  if (v.same_repr(*Dlop::create_integer(-1))) return "(- 1)";
  if (v.is_just_i64()) {
    const auto i = v.to_just_i64();
    if (i < 0) return "(- " + std::to_string(-i) + ")";
    return std::to_string(i);
  }
  auto [mask_lo, mask_hi] = v.get_mask_range();
  if (mask_lo == 0 && static_cast<uint32_t>(mask_hi) <= w) return "((2::int) ^ " + std::to_string(w) + " - 1)";
  fatal(ctx, "Const node n_" + std::to_string(node_id(node))
                 + " is a non-mask arbitrary-width constant; certificate needs a decimal emitter.");
  return "0";
}

struct Cert_build {
  std::set<uint32_t> source_ids;
  std::map<uint32_t, std::string> source_exprs;
  std::map<uint32_t, uint32_t> source_widths;
  std::map<std::pair<uint64_t, uint32_t>, Node_pin> const_pins;
  std::map<std::pair<uint64_t, uint32_t>, uint32_t> const_ids;
  uint32_t next_synth_id = 1000000000;
};

std::string bv_of_word_expr(const std::string &expr, uint32_t w) {
  return "(BV " + std::to_string(w) + " (uint (" + expr + ")))";
}

std::string word_of_bv_expr(const std::string &rho, uint32_t id, uint32_t w) {
  return "((word_of_int (bv_uint (" + rho + " " + std::to_string(id)
         + "))) :: " + std::to_string(w) + " word)";
}

uint32_t cert_dep_id(const Ctx &ctx, Cert_build &build, const Node_pin &pin, uint32_t expected_w) {
  auto n = pin_node(pin);
  auto id = node_id(n);
  if (pin_is_const(pin)) {
    if (expected_w == 0) {
      fatal(ctx, "Const node n_" + std::to_string(id)
                     + " reached by certificate with zero expected width.");
    }
    const auto key = std::make_pair(static_cast<uint64_t>(pin.get_class_index().value), expected_w);
    build.const_pins.emplace(key, pin);
    const auto it = build.const_ids.find(key);
    if (it == build.const_ids.end()) {
      const uint32_t synth_id = build.next_synth_id++;
      build.const_ids.emplace(key, synth_id);
      return synth_id;
    }
    return it->second;
  } else if (pin_is_input(pin) || node_is_flop(n)) {
    if (pin_is_input(pin)) {
      auto pname = input_name_for_pin(ctx, pin);
      auto it = ctx.input_field.find(pname);
      if (it == ctx.input_field.end()) {
        fatal(ctx, "internal: graph-input pin '" + pname + "' has no input field for certificate source.");
      }
      auto sid_it = ctx.input_source_id.find(pname);
      if (sid_it == ctx.input_source_id.end()) {
        fatal(ctx, "internal: graph-input pin '" + pname + "' has no synthetic certificate source id.");
      }
      id = sid_it->second;
      auto w = ctx.input_width.at(pname);
      build.source_exprs[id] = bv_of_word_expr(it->second + " i", w);
      build.source_widths[id] = w;
    } else {
      id = node_id(n);
      auto fit = ctx.flop_field.find(id);
      if (fit == ctx.flop_field.end()) {
        fatal(ctx, "internal: flop n_" + std::to_string(id) + " has no state field for certificate source.");
      }
      auto w = ctx.flop_width.at(id);
      build.source_exprs[id] = bv_of_word_expr(fit->second + " s", w);
      build.source_widths[id] = w;
    }
    build.source_ids.insert(id);
  }
  return id;
}

std::string cert_const_pin_expr(const Ctx &ctx, const Node_pin &pin, uint32_t synth_id, uint32_t w) {
  auto node = pin_node(pin);
  check_width(ctx, node, w, "certificate const width");
  std::ostringstream oss;
  oss << "\\<lparr>nid = " << synth_id
      << ", op = Op_Const " << int_of_const(ctx, node, pin_const_value(pin), w)
      << ", width = " << w
      << ", deps = []\\<rparr>";
  return oss.str();
}

std::string cert_node_expr(const Ctx &ctx,
                           Cert_build &build,
                           const Node &node,
                           const std::string &forced_op = "",
                           const std::vector<uint32_t> &forced_deps = {}) {
  uint32_t w = 0;
  if (node_is_const(node)) {
    w = raw_node_width(node);
    check_width(ctx, node, w, "certificate const width");
  } else {
    w = node_width(ctx, node);
  }
  std::string op_expr = forced_op;
  std::vector<uint32_t> deps = forced_deps;

  if (op_expr.empty()) {
    switch (node_op(node)) {
      case Ntype_op::Nconst:
        op_expr = "Op_Const " + int_of_const(ctx, node, node_const_value(node), w);
        break;

      case Ntype_op::Sum: {
        std::vector<uint32_t> a_terms, b_terms;
        for (const auto &e : inp_edges_ordered(node)) {
          if (e.sink.get_port_id() == 0) a_terms.push_back(cert_dep_id(ctx, build, e.driver, w));
          else if (e.sink.get_port_id() == 1) b_terms.push_back(cert_dep_id(ctx, build, e.driver, w));
        }
        deps = a_terms;
        deps.insert(deps.end(), b_terms.begin(), b_terms.end());
        op_expr = "Op_Sum " + std::to_string(a_terms.size());
        break;
      }

      case Ntype_op::Mult:
        op_expr = "Op_Mult";
        for (const auto &e : inp_edges_ordered(node)) deps.push_back(cert_dep_id(ctx, build, e.driver, w));
        break;

      case Ntype_op::Div: {
        Node_pin a, b;
        bool have_a = false, have_b = false;
        for (const auto &e : inp_edges_ordered(node)) {
          auto pname = sink_pin_name(e);
          if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
          else if (pname == "b" || e.sink.get_port_id() == 1) { b = e.driver; have_b = true; }
        }
        if (!have_a || !have_b) fatal(ctx, "Div node n_" + std::to_string(node_id(node)) + " missing a/b.");
        op_expr = "Op_UDiv";
        deps = {cert_dep_id(ctx, build, a, w), cert_dep_id(ctx, build, b, w)};
        break;
      }

      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor:
      case Ntype_op::Ror:
      case Ntype_op::EQ: {
        if (node_op(node) == Ntype_op::And) op_expr = "Op_And";
        else if (node_op(node) == Ntype_op::Or) op_expr = "Op_Or";
        else if (node_op(node) == Ntype_op::Xor) op_expr = "Op_Xor";
        else if (node_op(node) == Ntype_op::Ror) op_expr = "Op_Ror";
        else op_expr = "Op_EQ";
        uint32_t dep_w = w;
        if (node_op(node) == Ntype_op::EQ) {
          dep_w = 1;
          for (const auto &e : inp_edges_ordered(node)) dep_w = std::max(dep_w, pin_width(ctx, e.driver, node));
        }
        for (const auto &e : inp_edges_ordered(node)) deps.push_back(cert_dep_id(ctx, build, e.driver, dep_w));
        break;
      }

      case Ntype_op::Not: {
        Node_pin a;
        bool have = false;
        for (const auto &e : inp_edges_ordered(node)) { a = e.driver; have = true; break; }
        if (!have) fatal(ctx, "Not node n_" + std::to_string(node_id(node)) + " has no input.");
        op_expr = "Op_Not";
        deps = {cert_dep_id(ctx, build, a, w)};
        break;
      }

      case Ntype_op::LT:
      case Ntype_op::GT: {
        op_expr = (node_op(node) == Ntype_op::LT) ? "Op_ULT" : "Op_UGT";
        std::vector<Node_pin> drivers;
        for (const auto &e : inp_edges_ordered(node)) drivers.push_back(e.driver);
        if (drivers.size() != 2) fatal(ctx, "LT/GT node n_" + std::to_string(node_id(node)) + " is not binary.");
        const uint32_t cmp_w = std::max(pin_width(ctx, drivers[0], node), pin_width(ctx, drivers[1], node));
        deps = {cert_dep_id(ctx, build, drivers[0], cmp_w), cert_dep_id(ctx, build, drivers[1], cmp_w)};
        break;
      }

      case Ntype_op::SHL: {
        Node_pin a;
        std::vector<Node_pin> bs;
        bool have_a = false;
        for (const auto &e : inp_edges_ordered(node)) {
          auto pname = sink_pin_name(e);
          if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
          else if (pname == "B" || pname == "b" || e.sink.get_port_id() == 1) { bs.push_back(e.driver); }
        }
        if (!have_a || bs.empty()) fatal(ctx, "SHL node n_" + std::to_string(node_id(node)) + " is malformed.");
        op_expr = "Op_SHL";
        deps.push_back(cert_dep_id(ctx, build, a, w));
        for (auto &b : bs) deps.push_back(cert_dep_id(ctx, build, b, pin_width(ctx, b, node)));
        break;
      }

      case Ntype_op::SRA: {
        Node_pin a, b;
        bool have_a = false, have_b = false;
        std::vector<Node_pin> ordered;
        for (const auto &e : inp_edges_ordered(node)) {
          ordered.push_back(e.driver);
          auto pname = sink_pin_name(e);
          if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
          else if (pname == "B" || pname == "b" || e.sink.get_port_id() == 1) { b = e.driver; have_b = true; }
        }
        if ((!have_a || !have_b) && ordered.size() == 2) {
          a = ordered[0];
          b = ordered[1];
          have_a = true;
          have_b = true;
        }
        if (!have_a && have_b) {
          op_expr = "Op_Const 0";
        } else if (have_a && !have_b) {
          op_expr = "Op_Sum 1";
          deps = {cert_dep_id(ctx, build, a, w)};
        } else if (have_a && have_b) {
          op_expr = "Op_SRA";
          deps = {cert_dep_id(ctx, build, a, w), cert_dep_id(ctx, build, b, pin_width(ctx, b, node))};
        } else {
          fatal(ctx, "SRA node n_" + std::to_string(node_id(node)) + " missing a/b.");
        }
        break;
      }

      case Ntype_op::Mux: {
        Node_pin sel;
        bool have_sel = false;
        std::map<int, Node_pin> options;
        uint32_t sel_w = 0;
        for (const auto &e : inp_edges_ordered(node)) {
          int pid = e.sink.get_port_id();
          if (pid == 0) {
            sel = e.driver;
            have_sel = true;
            sel_w = pin_width(ctx, e.driver, node);
          } else {
            options[pid] = e.driver;
          }
        }
        if (!have_sel || options.size() < 2) fatal(ctx, "Mux node n_" + std::to_string(node_id(node)) + " is malformed.");
        deps.push_back(cert_dep_id(ctx, build, sel, sel_w));
        if (options.size() == 2 && sel_w == 1) {
          op_expr = "Op_MuxBool";
        } else {
          op_expr = "Op_MuxN";
        }
        for (const auto &kv : options) deps.push_back(cert_dep_id(ctx, build, kv.second, w));
        break;
      }

      case Ntype_op::Sext: {
        Node_pin a, b;
        bool have_a = false, have_b = false;
        for (const auto &e : inp_edges_ordered(node)) {
          auto pname = sink_pin_name(e);
          if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
          else if (pname == "b" || e.sink.get_port_id() == 1) { b = e.driver; have_b = true; }
        }
        if (!have_a || !have_b) fatal(ctx, "Sext node n_" + std::to_string(node_id(node)) + " missing a/b.");
        op_expr = "Op_Sext";
        deps = {cert_dep_id(ctx, build, a, pin_width(ctx, a, node)), cert_dep_id(ctx, build, b, pin_width(ctx, b, node))};
        break;
      }

      case Ntype_op::Get_mask: {
        Node_pin a, mask;
        bool have_a = false, have_m = false;
        std::vector<Node_pin> ordered;
        for (const auto &e : inp_edges_ordered(node)) {
          ordered.push_back(e.driver);
          auto pname = sink_pin_name(e);
          if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
          else if (pname == "mask" || e.sink.get_port_id() == 1) { mask = e.driver; have_m = true; }
        }
        if ((!have_a || !have_m) && ordered.size() == 2) {
          a = ordered[0];
          mask = ordered[1];
          have_a = true;
          have_m = true;
        }
        if (!have_a || !have_m) fatal(ctx, "Get_mask node n_" + std::to_string(node_id(node)) + " missing operands.");
        op_expr = "Op_GetMask";
        deps = {cert_dep_id(ctx, build, a, pin_width(ctx, a, node)), cert_dep_id(ctx, build, mask, pin_width(ctx, mask, node))};
        break;
      }

      case Ntype_op::Set_mask: {
        Node_pin a, mask, value;
        bool have_a = false, have_m = false, have_v = false;
        for (const auto &e : inp_edges_ordered(node)) {
          auto pname = sink_pin_name(e);
          if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
          else if (pname == "mask" || e.sink.get_port_id() == 1) { mask = e.driver; have_m = true; }
          else if (pname == "value" || e.sink.get_port_id() == 2) { value = e.driver; have_v = true; }
        }
        if (!have_a || !have_m || !have_v) fatal(ctx, "Set_mask node n_" + std::to_string(node_id(node)) + " missing operands.");
        op_expr = "Op_SetMask";
        deps = {cert_dep_id(ctx, build, a, w), cert_dep_id(ctx, build, mask, pin_width(ctx, mask, node)),
                cert_dep_id(ctx, build, value, pin_width(ctx, value, node))};
        break;
      }

      default:
        fatal(ctx, "Unsupported op in certificate for node n_" + std::to_string(node_id(node)));
    }
  }

  std::ostringstream oss;
  oss << "\\<lparr>nid = " << node_id(node)
      << ", op = " << op_expr
      << ", width = " << w
      << ", deps = " << nat_list(deps)
      << "\\<rparr>";
  return oss.str();
}

void emit_cert_theory(const Ctx &ctx,
                      const std::vector<Node> &topo,
                      const std::vector<Node> &flop_nodes,
                      const absl::flat_hash_set<uint32_t> &flop_nids,
                      const std::map<std::string, Node_pin> &out_drivers,
                      const std::map<uint32_t, Node_pin> &flop_din,
                      const std::map<uint32_t, Node_pin> &flop_reset,
                      const std::map<uint32_t, Node_pin> &flop_enable) {
  (void)flop_nids;
  const std::string model_theory = ctx.top_name + "_Lgraph";
  const std::string cert_theory  = ctx.top_name + "_Lgraph_Cert";
  const std::string path         = ctx.output_dir + "/" + cert_theory + ".thy";
  std::ofstream ofs(path);
  if (!ofs) {
    std::cerr << "pass.isabelle could not write " << path << "\n";
    return;
  }

  const std::string node_count_name = ctx.top_name + "_node_count";
  const std::string flop_count_name = ctx.top_name + "_flop_count";
  const std::string certs_name      = ctx.top_name + "_node_certs";
  const std::string cert_chunks_name = ctx.top_name + "_node_cert_chunks";
  const std::string id_chunks_name   = ctx.top_name + "_cert_id_chunks";
  const std::string all_ids_name     = ctx.top_name + "_cert_all_ids";
  const std::string topo_name       = ctx.top_name + "_cert_topo";
  const std::string sources_name    = ctx.top_name + "_cert_sources";
  const std::string nodes_name      = ctx.top_name + "_cert_nodes";
  const std::string graph_name      = ctx.top_name + "_graph_cert";
  const std::string wf_bool_name    = ctx.top_name + "_graph_cert_wf_bool";
  const std::string source_env_name = ctx.top_name + "_source_env";
  const std::string outputs_name    = ctx.top_name + "_outputs_from_cert";
  const std::string next_name       = ctx.top_name + "_next_state_from_cert";
  const std::string cert_step_name  = ctx.top_name + "_cert_step";

  Cert_build build;
  std::vector<std::string> internal_exprs;
  std::vector<std::string> internal_op_tags;
  std::vector<uint32_t> internal_ids;
  internal_exprs.reserve(topo.size());
  internal_op_tags.reserve(topo.size());
  internal_ids.reserve(topo.size());
  for (const auto &node : topo) {
    internal_exprs.push_back(cert_node_expr(ctx, build, node));
    internal_op_tags.push_back(cert_op_tag_from_expr(internal_exprs.back()));
    internal_ids.push_back(node_id(node));
  }

  std::map<std::string, uint32_t> out_driver_ids;
  for (const auto &kv : ctx.output_field) {
    const auto &out_name = kv.first;
    auto drv = out_drivers.find(out_name);
    if (drv != out_drivers.end()) {
      out_driver_ids[out_name] = cert_dep_id(ctx, build, drv->second, ctx.output_width.at(out_name));
    }
  }

  std::map<uint32_t, uint32_t> flop_din_ids, flop_reset_ids, flop_enable_ids;
  for (const auto &fn : flop_nodes) {
    auto fid = node_id(fn);
    auto fw = ctx.flop_width.at(fid);
    if (auto it = flop_din.find(fid); it != flop_din.end()) {
      flop_din_ids[fid] = cert_dep_id(ctx, build, it->second, fw);
    }
    if (auto it = flop_reset.find(fid); it != flop_reset.end()) {
      flop_reset_ids[fid] = cert_dep_id(ctx, build, it->second, 1);
    }
    if (auto it = flop_enable.find(fid); it != flop_enable.end()) {
      flop_enable_ids[fid] = cert_dep_id(ctx, build, it->second, 1);
    }
    build.source_ids.insert(fid);
    build.source_exprs[fid] = bv_of_word_expr(ctx.flop_field.at(fid) + " s", fw);
    build.source_widths[fid] = fw;
  }

  std::vector<std::string> const_exprs;
  std::vector<std::string> const_op_tags;
  std::vector<uint32_t> const_ids;
  const_exprs.reserve(build.const_pins.size());
  const_op_tags.reserve(build.const_pins.size());
  const_ids.reserve(build.const_pins.size());
  for (const auto &kv : build.const_pins) {
    const auto id_it = build.const_ids.find(kv.first);
    if (id_it == build.const_ids.end()) {
      fatal(ctx, "internal: missing synthetic certificate id for const node n_"
                     + std::to_string(static_cast<uint64_t>(kv.first.first)));
    }
    const_exprs.push_back(cert_const_pin_expr(ctx, kv.second, id_it->second, kv.first.second));
    const_op_tags.push_back("Op_Const");
    const_ids.push_back(id_it->second);
  }

  std::vector<std::string> all_exprs;
  std::vector<std::string> all_op_tags;
  std::vector<uint32_t> all_ids;
  all_exprs.reserve(const_exprs.size() + internal_exprs.size());
  all_op_tags.reserve(const_op_tags.size() + internal_op_tags.size());
  all_ids.reserve(const_ids.size() + internal_ids.size());
  all_exprs.insert(all_exprs.end(), const_exprs.begin(), const_exprs.end());
  all_exprs.insert(all_exprs.end(), internal_exprs.begin(), internal_exprs.end());
  all_op_tags.insert(all_op_tags.end(), const_op_tags.begin(), const_op_tags.end());
  all_op_tags.insert(all_op_tags.end(), internal_op_tags.begin(), internal_op_tags.end());
  all_ids.insert(all_ids.end(), const_ids.begin(), const_ids.end());
  all_ids.insert(all_ids.end(), internal_ids.begin(), internal_ids.end());

  std::vector<uint32_t> source_ids(build.source_ids.begin(), build.source_ids.end());
  const size_t chunk_size = ctx.cert_chunk_size == 0 ? 25 : ctx.cert_chunk_size;
  const size_t full_chunk_count = (all_exprs.size() + chunk_size - 1) / chunk_size;
  const bool chunk_limited = ctx.cert_chunk_limit != 0 && ctx.cert_chunk_limit < full_chunk_count;
  const size_t chunk_count = chunk_limited ? ctx.cert_chunk_limit : full_chunk_count;
  std::cerr << "pass.isabelle cert chunks for " << ctx.top_name
            << ": total_nodes=" << all_exprs.size()
            << " chunk_size=" << chunk_size
            << " emitted_chunks=" << chunk_count
            << " full_chunks=" << full_chunk_count << "\n";
  for (size_t chunk = 0; chunk < chunk_count; ++chunk) {
    const size_t begin = chunk * chunk_size;
    const size_t end = std::min(begin + chunk_size, all_exprs.size());
    const bool const_only_chunk = end <= const_exprs.size();
    const bool simple_chunk = !const_only_chunk && is_simple_cert_chunk(all_op_tags, begin, end);
    std::cerr << "  chunk " << chunk << ": nodes=" << (end - begin)
              << " class="
              << (const_only_chunk ? "const-only" : (simple_chunk ? "simple-mixed" : "unsupported-mixed"))
              << " ops={" << chunk_op_summary(all_op_tags, begin, end) << "}\n";
  }

  ofs << "(* Generated by pass.isabelle. Do not edit by hand. *)\n";
  ofs << "theory " << cert_theory << "\n";
  ofs << "  imports " << model_theory
      << " \"LGraph-Translation-Correctness.Translation_Certificate_Evaluator\"\n"
      << "          \"LGraph-Translation-Correctness.Translation_Step\"\n";
  ofs << "begin\n\n";

  ofs << "definition " << node_count_name << " :: nat where\n";
  ofs << "  \"" << node_count_name << " = " << topo.size() << "\"\n\n";

  ofs << "definition " << flop_count_name << " :: nat where\n";
  ofs << "  \"" << flop_count_name << " = " << flop_nodes.size() << "\"\n\n";

  ofs << "lemma " << ctx.top_name << "_certificate_counts:\n";
  ofs << "  \"" << node_count_name << " = " << topo.size()
      << " \\<and> " << flop_count_name << " = " << flop_nodes.size() << "\"\n";
  ofs << "  by (simp add: " << node_count_name << "_def "
      << flop_count_name << "_def)\n\n";

  for (size_t chunk = 0; chunk < chunk_count; ++chunk) {
    const size_t begin = chunk * chunk_size;
    const size_t end = std::min(begin + chunk_size, all_exprs.size());
    ofs << "definition " << certs_name << "_" << chunk << " :: \"node_cert list\" where\n";
    ofs << "  \"" << certs_name << "_" << chunk << " = [\n";
    for (size_t i = begin; i < end; ++i) {
      ofs << "    " << all_exprs[i];
      if (i + 1 != end) ofs << ",";
      ofs << "\n";
    }
    ofs << "  ]\"\n\n";

    ofs << "definition " << certs_name << "_ids_" << chunk << " :: \"nat list\" where\n";
    ofs << "  \"" << certs_name << "_ids_" << chunk << " = map nid "
        << certs_name << "_" << chunk << "\"\n\n";
  }

  ofs << "definition " << cert_chunks_name << " :: \"node_cert list list\" where\n";
  ofs << "  \"" << cert_chunks_name << " = [\n";
  for (size_t chunk = 0; chunk < chunk_count; ++chunk) {
    ofs << "    " << certs_name << "_" << chunk;
    if (chunk + 1 != chunk_count) ofs << ",";
    ofs << "\n";
  }
  ofs << "  ]\"\n\n";

  ofs << "definition " << id_chunks_name << " :: \"nat list list\" where\n";
  ofs << "  \"" << id_chunks_name << " = [\n";
  for (size_t chunk = 0; chunk < chunk_count; ++chunk) {
    ofs << "    " << certs_name << "_ids_" << chunk;
    if (chunk + 1 != chunk_count) ofs << ",";
    ofs << "\n";
  }
  ofs << "  ]\"\n\n";

  ofs << "definition " << all_ids_name << " :: \"nat list\" where\n";
  ofs << "  \"" << all_ids_name << " = concat " << id_chunks_name << "\"\n\n";

  ofs << "definition " << certs_name << " :: \"node_cert list\" where\n";
  ofs << "  \"" << certs_name << " = concat " << cert_chunks_name << "\"\n\n";

  ofs << "definition " << sources_name << " :: \"nat list\" where\n";
  ofs << "  \"" << sources_name << " = " << nat_list(source_ids) << "\"\n\n";

  ofs << "definition " << topo_name << " :: \"nat list\" where\n";
  ofs << "  \"" << topo_name << " = " << all_ids_name << "\"\n\n";

  ofs << "definition " << nodes_name << " :: \"nat \\<Rightarrow> node_cert option\" where\n";
  ofs << "  \"" << nodes_name << " = nodes_of_list " << certs_name << "\"\n\n";

  ofs << "definition " << graph_name << " :: graph_cert where\n";
  ofs << "  \"" << graph_name << " =\n";
  ofs << "     \\<lparr>topo = " << topo_name << ",\n";
  ofs << "      sources = " << sources_name << ",\n";
  ofs << "      nodes = " << nodes_name << "\\<rparr>\"\n\n";

  const bool sequential = !flop_nodes.empty();
  ofs << "definition " << source_env_name << " :: \""
      << ctx.top_name << "_in \\<Rightarrow> ";
  if (sequential) {
    ofs << ctx.top_name << "_state \\<Rightarrow> ";
  }
  ofs << "nat \\<Rightarrow> bv\" where\n";
  ofs << "  \"" << source_env_name << " i";
  if (sequential) ofs << " s";
  ofs << " n =\n";
  bool first_src = true;
  for (const auto id : source_ids) {
    auto expr_it = build.source_exprs.find(id);
    if (expr_it == build.source_exprs.end()) continue;
    ofs << "     " << (first_src ? "(" : "else ") << "if n = " << id << " then "
        << expr_it->second << "\n";
    first_src = false;
  }
  ofs << "      else BV 1 0";
  if (!first_src) ofs << ")";
  ofs << "\"\n\n";

  ofs << "definition " << outputs_name << " :: \"(nat \\<Rightarrow> bv) \\<Rightarrow> "
      << ctx.top_name << "_out\" where\n";
  ofs << "  \"" << outputs_name << " rho = \\<lparr>";
  bool first_out = true;
  for (const auto &kv : ctx.output_field) {
    if (!first_out) ofs << ", ";
    first_out = false;
    const auto &out_name = kv.first;
    auto id_it = out_driver_ids.find(out_name);
    if (id_it == out_driver_ids.end()) {
      ofs << kv.second << " = " << lit_zero(ctx.output_width.at(out_name));
    } else {
      ofs << kv.second << " = "
          << word_of_bv_expr("rho", id_it->second, ctx.output_width.at(out_name));
    }
  }
  if (ctx.output_field.empty()) {
    ofs << "out_dummy = (0 :: 1 word)";
  }
  ofs << "\\<rparr>\"\n\n";

  if (sequential) {
    ofs << "definition " << next_name << " :: \"(nat \\<Rightarrow> bv) \\<Rightarrow> "
        << ctx.top_name << "_state\" where\n";
    ofs << "  \"" << next_name << " rho = \\<lparr>";
    bool first_flop = true;
    for (const auto &fn : flop_nodes) {
      auto fid = node_id(fn);
      auto fld = ctx.flop_field.at(fid);
      auto fw = ctx.flop_width.at(fid);
      if (!first_flop) ofs << ", ";
      first_flop = false;
      const auto current = word_of_bv_expr("rho", fid, fw);
      const auto din = flop_din_ids.count(fid) ? word_of_bv_expr("rho", flop_din_ids.at(fid), fw)
                                               : lit_zero(fw);
      const auto reset = flop_reset_ids.count(fid)
        ? "(bv_nonzero (rho " + std::to_string(flop_reset_ids.at(fid)) + "))"
        : "False";
      const auto enable = flop_enable_ids.count(fid)
        ? "(bv_nonzero (rho " + std::to_string(flop_enable_ids.at(fid)) + "))"
        : "True";
      ofs << fld << " = flop_next " << reset << " (0 :: " << fw << " word) "
          << enable << " " << din << " " << current;
    }
    ofs << "\\<rparr>\"\n\n";

    ofs << "definition " << cert_step_name << " :: \""
        << ctx.top_name << "_in \\<Rightarrow> " << ctx.top_name
        << "_state \\<Rightarrow> " << ctx.top_name << "_state \\<times> "
        << ctx.top_name << "_out\" where\n";
    ofs << "  \"" << cert_step_name << " i s =\n";
    ofs << "     (let rho = eval_graph (topo " << graph_name << ") "
        << graph_name << " (" << source_env_name << " i s)\n";
    ofs << "      in (" << next_name << " rho, " << outputs_name << " rho))\"\n\n";
  }

  ofs << "definition " << wf_bool_name << " :: bool where\n";
  ofs << "  \"" << wf_bool_name << " = graph_cert_wf_bool "
      << certs_name << " " << sources_name << "\"\n\n";

  if (ctx.cert_wf == CertWFMode::Chunked) {
    for (size_t chunk = 0; chunk < chunk_count; ++chunk) {
      const size_t begin = chunk * chunk_size;
      const size_t end = std::min(begin + chunk_size, all_exprs.size());
      const bool const_only_chunk = end <= const_exprs.size();
      const bool simple_chunk = !const_only_chunk && is_simple_cert_chunk(all_op_tags, begin, end);

      ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ids:\n";
      ofs << "  \"map nid " << certs_name << "_" << chunk << " = "
          << certs_name << "_ids_" << chunk << "\"\n";
      ofs << "  by (simp add: " << certs_name << "_ids_" << chunk << "_def)\n\n";

      if (const_only_chunk) {
        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_const:\n";
        ofs << "  \"list_all const_node_cert_wf_bool " << certs_name << "_" << chunk << "\"\n";
        ofs << "  by (simp add: " << certs_name << "_" << chunk
            << "_def const_node_cert_wf_bool_def)\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ids_distinct:\n";
        ofs << "  \"distinct (map nid " << certs_name << "_" << chunk << ")\"\n";
        ofs << "  by (simp add: " << certs_name << "_" << chunk << "_def)\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ids_subset:\n";
        ofs << "  \"set (map nid " << certs_name << "_" << chunk
            << ") \\<subseteq> set " << all_ids_name << "\"\n";
        ofs << "  by (auto simp add: " << all_ids_name << "_def " << id_chunks_name
            << "_def " << certs_name << "_ids_" << chunk << "_def)\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ids_disjoint:\n";
        ofs << "  \"set (map nid " << certs_name << "_" << chunk
            << ") \\<inter> set " << sources_name << " = {}\"\n";
        ofs << "  by (simp add: " << certs_name << "_" << chunk << "_def "
            << sources_name << "_def)\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ok:\n";
        ofs << "  \"node_cert_chunk_wf_bool " << all_ids_name << " "
            << sources_name << " " << certs_name << "_" << chunk << "\"\n";
        ofs << "  using " << ctx.top_name << "_cert_chunk_" << chunk << "_const\n";
        ofs << "        " << ctx.top_name << "_cert_chunk_" << chunk << "_ids_distinct\n";
        ofs << "        " << ctx.top_name << "_cert_chunk_" << chunk << "_ids_subset\n";
        ofs << "        " << ctx.top_name << "_cert_chunk_" << chunk << "_ids_disjoint\n";
        ofs << "  by (rule const_node_cert_chunk_wf_bool_sound)\n\n";
      } else if (simple_chunk) {
        const auto deps = chunk_dep_ids(all_exprs, begin, end);
        const auto proof_chunks = needed_id_chunks(deps, all_ids, chunk_size, chunk_count);
        const std::string deps_name = ctx.top_name + "_cert_chunk_" + std::to_string(chunk) + "_deps";

        ofs << "definition " << deps_name << " :: \"nat list\" where\n";
        ofs << "  \"" << deps_name << " = " << nat_list(deps) << "\"\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_shape:\n";
        ofs << "  \"list_all simple_node_cert_shape_wf_bool "
            << certs_name << "_" << chunk << "\"\n";
        ofs << "  by (simp add: " << certs_name << "_" << chunk << "_def "
            << "simple_node_cert_shape_wf_bool_def simple_op_cert_wf_bool_def)\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_deps:\n";
        ofs << "  \"set (node_cert_deps " << certs_name << "_" << chunk << ") \\<subseteq> set "
            << deps_name << "\"\n";
        ofs << "  by (simp add: " << certs_name << "_" << chunk << "_def "
            << deps_name << "_def node_cert_deps_def)\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ids_distinct:\n";
        ofs << "  \"distinct (map nid " << certs_name << "_" << chunk << ")\"\n";
        ofs << "  by (simp add: " << certs_name << "_" << chunk << "_def)\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ids_subset:\n";
        ofs << "  \"set (map nid " << certs_name << "_" << chunk
            << ") \\<subseteq> set " << all_ids_name << "\"\n";
        ofs << "  by (auto simp add: " << all_ids_name << "_def " << id_chunks_name
            << "_def " << certs_name << "_ids_" << chunk << "_def)\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ids_disjoint:\n";
        ofs << "  \"set (map nid " << certs_name << "_" << chunk
            << ") \\<inter> set " << sources_name << " = {}\"\n";
        ofs << "  by (simp add: " << certs_name << "_" << chunk << "_def "
            << sources_name << "_def)\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_deps_subset:\n";
        ofs << "  \"set (node_cert_deps " << certs_name << "_" << chunk
            << ") \\<subseteq> set " << all_ids_name << " \\<union> set "
            << sources_name << "\"\n";
        ofs << "  using " << ctx.top_name << "_cert_chunk_" << chunk << "_deps\n";
        for (auto proof_chunk : proof_chunks) {
          ofs << "        " << ctx.top_name << "_cert_chunk_" << proof_chunk << "_ids_subset\n";
        }
        ofs << "  by (auto simp add: " << deps_name << "_def " << sources_name << "_def";
        for (auto proof_chunk : proof_chunks) {
          ofs << " " << certs_name << "_ids_" << proof_chunk << "_def"
              << " " << certs_name << "_" << proof_chunk << "_def";
        }
        ofs << ")\n\n";

        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ok:\n";
        ofs << "  \"node_cert_chunk_wf_bool " << all_ids_name << " "
            << sources_name << " " << certs_name << "_" << chunk << "\"\n";
        ofs << "  by (rule simple_node_cert_chunk_wf_bool_sound'[OF "
            << ctx.top_name << "_cert_chunk_" << chunk << "_shape "
            << ctx.top_name << "_cert_chunk_" << chunk << "_deps_subset "
            << ctx.top_name << "_cert_chunk_" << chunk << "_ids_distinct "
            << ctx.top_name << "_cert_chunk_" << chunk << "_ids_subset "
            << ctx.top_name << "_cert_chunk_" << chunk << "_ids_disjoint])\n\n";
      } else {
        if (ctx.cert_wf_fallback == CertWFFallback::Fail) {
          fatal(ctx, "unsupported mixed certificate chunk " + std::to_string(chunk)
                         + " for " + ctx.top_name + " under cert_wf_fallback:fail; ops={"
                         + chunk_op_summary(all_op_tags, begin, end) + "}");
        }
        ofs << "lemma " << ctx.top_name << "_cert_chunk_" << chunk << "_ok:\n";
        ofs << "  \"node_cert_chunk_wf_bool " << all_ids_name << " "
            << sources_name << " " << certs_name << "_" << chunk << "\"\n";
        if (ctx.cert_wf_fallback == CertWFFallback::Sorry) {
          ofs << "  sorry (* placeholder: unsupported mixed certificate chunk; ops={"
              << chunk_op_summary(all_op_tags, begin, end) << "} *)\n\n";
        } else {
          ofs << "  by eval\n\n";
        }
      }
    }

    if (chunk_count != 0) {
      ofs << "lemmas " << ctx.top_name << "_cert_chunk_ids =\n";
      for (size_t chunk = 0; chunk < chunk_count; ++chunk) {
        ofs << "  " << ctx.top_name << "_cert_chunk_" << chunk << "_ids\n";
      }
      ofs << "\n";

      ofs << "lemmas " << ctx.top_name << "_cert_chunk_checks =\n";
      for (size_t chunk = 0; chunk < chunk_count; ++chunk) {
        ofs << "  " << ctx.top_name << "_cert_chunk_" << chunk << "_ok\n";
      }
      ofs << "\n";
    }

    ofs << "lemma " << ctx.top_name << "_cert_chunks_ids:\n";
    ofs << "  \"map (map nid) " << cert_chunks_name << " = " << id_chunks_name << "\"\n";
    ofs << "  by (simp add: " << cert_chunks_name << "_def " << id_chunks_name << "_def";
    if (chunk_count != 0) {
      ofs << " " << ctx.top_name << "_cert_chunk_ids";
    }
    ofs << ")\n\n";

    ofs << "lemma " << ctx.top_name << "_cert_chunks_ok:\n";
    ofs << "  \"list_all (node_cert_chunk_wf_bool " << all_ids_name << " "
        << sources_name << ") " << cert_chunks_name << "\"\n";
    ofs << "  by (simp add: " << cert_chunks_name << "_def";
    if (chunk_count != 0) {
      ofs << " " << ctx.top_name << "_cert_chunk_checks";
    }
    ofs << ")\n\n";

    if (chunk_limited) {
      ofs << "text \\<open>\n";
      ofs << "  cert_chunk_limit emitted only " << chunk_count << " of "
          << full_chunk_count << " chunks.  This theory is for proof-shape\n";
      ofs << "  benchmarking and intentionally does not state graph_cert_wf for\n";
      ofs << "  the full design certificate.\n";
      ofs << "\\<close>\n\n";
    } else {
      ofs << "lemma " << ctx.top_name << "_cert_all_ids_distinct:\n";
      ofs << "  \"distinct " << all_ids_name << "\"\n";
      ofs << "  sorry (* placeholder: replace with chunked uniqueness checker; "
             "global distinct by eval is not scalable. *)\n\n";

      ofs << "lemma " << ctx.top_name << "_cert_sources_distinct:\n";
      ofs << "  \"distinct " << sources_name << "\"\n";
      ofs << "  by eval\n\n";

      ofs << "theorem " << ctx.top_name << "_graph_cert_wf:\n";
      ofs << "  \"graph_cert_wf " << graph_name << "\"\n";
      ofs << "  unfolding " << graph_name << "_def " << topo_name << "_def "
          << nodes_name << "_def " << certs_name << "_def\n";
      ofs << "  by (rule graph_cert_wf_from_chunk_checks[where chunks="
          << cert_chunks_name << " and id_chunks=" << id_chunks_name << "],\n";
      ofs << "      simp_all add: " << ctx.top_name << "_cert_chunks_ids "
          << ctx.top_name << "_cert_all_ids_distinct "
          << ctx.top_name << "_cert_sources_distinct "
          << ctx.top_name << "_cert_chunks_ok " << all_ids_name << "_def)\n\n";
    }
  } else if (ctx.cert_wf == CertWFMode::Eval) {
    ofs << "lemma " << ctx.top_name << "_graph_cert_wf_bool:\n";
    ofs << "  \"" << wf_bool_name << "\"\n";
    ofs << "  by eval\n\n";

    ofs << "lemma " << ctx.top_name << "_cert_all_ids_eq_map_nid:\n";
    ofs << "  \"" << all_ids_name << " = map nid " << certs_name << "\"\n";
    ofs << "  by eval\n\n";

    ofs << "theorem " << ctx.top_name << "_graph_cert_wf:\n";
    ofs << "  \"graph_cert_wf " << graph_name << "\"\n";
    ofs << "  using " << ctx.top_name << "_graph_cert_wf_bool "
        << ctx.top_name << "_cert_all_ids_eq_map_nid\n";
    ofs << "  by (simp add: " << wf_bool_name << "_def " << graph_name << "_def "
        << topo_name << "_def " << sources_name << "_def " << nodes_name
        << "_def graph_cert_wf_bool_sound)\n\n";
  } else if (ctx.cert_wf == CertWFMode::Sorry) {
    ofs << "lemma " << ctx.top_name << "_graph_cert_wf_bool:\n";
    ofs << "  \"" << wf_bool_name << "\"\n";
    ofs << "  sorry\n\n";

    ofs << "theorem " << ctx.top_name << "_graph_cert_wf:\n";
    ofs << "  \"graph_cert_wf " << graph_name << "\"\n";
    ofs << "  sorry\n\n";
  } else {
    ofs << "text \\<open>\n";
    ofs << "  Certificate well-formedness proof skipped by pass.isabelle cert_wf:skip.\n";
    ofs << "  The certificate object is still typechecked.  Re-run with cert_wf:eval\n";
    ofs << "  only for small designs or after replacing by eval with a scalable\n";
    ofs << "  chunked checker.\n";
    ofs << "\\<close>\n\n";
  }

  ofs << "theorem " << ctx.top_name << "_eval_graph_correct:\n";
  ofs << "  \"env_correct_on (set (topo " << graph_name << "))\n";
  ofs << "     (eval_graph (topo " << graph_name << ") " << graph_name << " source_env)\n";
  ofs << "     (graph_denotation (topo " << graph_name << ") " << graph_name << " source_env)\"\n";
  ofs << "  by (rule eval_graph_correct_for_cert)\n\n";

  if (sequential) {
    ofs << "theorem " << ctx.top_name << "_cert_step_refines_lgraph_certificate:\n";
    ofs << "  \"" << cert_step_name << " i s =\n";
    ofs << "     (let rho = eval_graph (topo " << graph_name << ") "
        << graph_name << " (" << source_env_name << " i s)\n";
    ofs << "      in (" << next_name << " rho, " << outputs_name << " rho))\"\n";
    ofs << "  by (simp add: " << cert_step_name << "_def)\n\n";
  }

  ofs << "theorem " << ctx.top_name << "_translation_correct_conditional:\n";
  ofs << "  assumes next_correct: \"gen_next i s = spec_next i s\"\n";
  ofs << "  assumes comb_correct: \"gen_comb i s = spec_comb i s\"\n";
  ofs << "  shows \"generated_step gen_next gen_comb i s =\n";
  ofs << "         lgraph_step spec_next spec_comb i s\"\n";
  ofs << "  using next_correct comb_correct\n";
  ofs << "  by (rule generated_step_equals_lgraph_step)\n\n";

  ofs << "text \\<open>\n";
  ofs << "  This certificate materializes the reachable LGraph DAG as compact data:\n";
  ofs << "  constants and internal nodes are node_cert entries, while primary inputs\n";
  ofs << "  and flop outputs are source nodes supplied by the source environment.\n";
  ofs << "  The pass constructs the certificate from reachable_topo_order and rejects\n";
  ofs << "  zero-width or unsupported reachable nodes before writing this file.\n";
  if (ctx.cert_wf == CertWFMode::Skip) {
    ofs << "  This file was emitted with cert_wf:skip, so it typechecks the certificate\n";
    ofs << "  data but intentionally does not prove graph_cert_wf for the DINO-scale\n";
    ofs << "  graph.  Use this mode for normal model-build gates; use a chunked checker\n";
    ofs << "  before re-enabling a full certificate well-formedness theorem.\n";
  } else if (ctx.cert_wf == CertWFMode::Chunked) {
    ofs << "  This file was emitted with cert_wf:chunked, so graph_cert_wf is proved\n";
    ofs << "  by bounded per-chunk by-eval facts and a generic non-evaluating assembly\n";
    ofs << "  theorem, instead of one whole-graph evaluation.\n";
  } else {
    ofs << "  The " << ctx.top_name << "_graph_cert_wf theorem discharges the concrete\n";
    ofs << "  certificate well-formedness obligation via graph_cert_wf_bool.\n";
  }
  ofs << "  The theorem " << ctx.top_name << "_eval_graph_correct instantiates the\n";
  ofs << "  generic graph-certificate evaluator for this concrete design. A separate\n";
  ofs << "  generated-fast-model-vs-certificate theorem can be added later without\n";
  ofs << "  reintroducing per-node local lemmas.\n";
  ofs << "\\<close>\n\n";

  ofs << "end\n";

  std::cout << "pass.isabelle: " << ctx.base_name << " -> " << path
            << " (" << topo.size() << " internal nodes, "
            << build.const_pins.size() << " const nodes, "
            << build.source_ids.size() << " source nodes, "
            << flop_nodes.size() << " flops)\n";
}

// Emit the inline Isabelle expression for a node (no surrounding `let n_<id>`).
std::string emit_node_expr(const Ctx &ctx, const Node &node) {
  const auto op = node_op(node);
  const auto w  = node_width(ctx, node);

  // Gather sink edges grouped by pin.
  // For multi-driver pin groups (Sum's A/B, EQ's variadic A, And's variadic A,
  // SHL's variadic B), use `inp_edges_ordered()` to get stable ordering.

  switch (op) {
    case Ntype_op::Nconst: {
      // Dlop expression is inlined at use site by driver_expr().
      // But emit_node_expr is called when the node is in the let-chain
      // (which we suppress for Dlop). If somehow reached, format directly.
      return lit_const_at(ctx, node, node_const_value(node), w);
    }

    case Ntype_op::IO:
      throw Emit_error("internal: emit_node_expr called on IO node");

    case Ntype_op::Sum: {
      // pid 0 = A (additive, n-ary), pid 1 = B (subtractive, n-ary)
      std::vector<std::string> a_terms, b_terms;
      for (const auto &e : inp_edges_ordered(node)) {
        auto pid = e.sink.get_port_id();
        auto expr = ucast_pin_at(ctx, e.driver, w);
        if (pid == 0) a_terms.push_back(expr);
        else if (pid == 1) b_terms.push_back(expr);
      }
      if (a_terms.empty() && b_terms.empty()) {
        fatal(ctx, "Sum node n_" + std::to_string(node_id(node))
                       + " has 0 inputs - pass.cprop should have folded it.");
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
      for (const auto &e : inp_edges_ordered(node)) {
        terms.push_back(ucast_pin_at(ctx, e.driver, w));
      }
      if (terms.empty()) fatal(ctx, "Mult node n_" + std::to_string(node_id(node)) + " is empty.");
      return nary_op_inline("*", terms);
    }

    case Ntype_op::Div: {
      // v1: emit sem_udiv unconditionally.
      Node_pin a, b;
      bool have_a = false, have_b = false;
      for (const auto &e : inp_edges_ordered(node)) {
        auto pname = sink_pin_name(e);
        if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
        else if (pname == "b" || e.sink.get_port_id() == 1) { b = e.driver; have_b = true; }
      }
      if (!have_a || !have_b) {
        const auto reason = "Div node n_" + std::to_string(node_id(node)) + " missing a or b";
        if (ctx.strict) fatal(ctx, reason + ".");
        return undefined_at(w, reason);
      }
      return "(sem_udiv " + ucast_pin_at(ctx, a, w) + " "
             + ucast_pin_at(ctx, b, w) + ")";
    }

    case Ntype_op::And: {
      std::vector<std::string> terms;
      for (const auto &e : inp_edges_ordered(node)) {
        terms.push_back(ucast_pin_at(ctx, e.driver, w));
      }
      if (terms.empty()) fatal(ctx, "And node n_" + std::to_string(node_id(node)) + " is empty.");
      return nary_op_func("Bit_Operations.and", terms);
    }

    case Ntype_op::Or: {
      std::vector<std::string> terms;
      for (const auto &e : inp_edges_ordered(node)) {
        terms.push_back(ucast_pin_at(ctx, e.driver, w));
      }
      if (terms.empty()) return lit_zero(w);
      return nary_op_func("Bit_Operations.or", terms);
    }

    case Ntype_op::Xor: {
      std::vector<std::string> terms;
      for (const auto &e : inp_edges_ordered(node)) {
        terms.push_back(ucast_pin_at(ctx, e.driver, w));
      }
      if (terms.empty()) return lit_zero(w);
      return nary_op_func("Bit_Operations.xor", terms);
    }

    case Ntype_op::Ror: {
      // Output is 1 word. Mixed input widths handled by `(expr) \<noteq> 0` per operand.
      std::vector<std::string> bools;
      for (const auto &e : inp_edges_ordered(node)) {
        auto ew = pin_width(ctx, e.driver, node);
        bools.push_back("(" + driver_expr_at(ctx, e.driver, ew) + ") \\<noteq> 0");
      }
      if (bools.empty()) return lit_zero(1);
      // Inline disjunction (small k) for `by eval` performance.
      std::string disj = bools[0];
      for (size_t i = 1; i < bools.size(); ++i) disj = disj + " \\<or> " + bools[i];
      return "(if " + disj + " then " + lit_one(1) + " else " + lit_zero(1) + ")";
    }

    case Ntype_op::Not: {
      Node_pin a;
      bool have = false;
      for (const auto &e : inp_edges_ordered(node)) { a = e.driver; have = true; break; }
      if (!have) fatal(ctx, "Not node n_" + std::to_string(node_id(node)) + " has no input.");
      return "(Bit_Operations.not " + ucast_pin_at(ctx, a, w) + ")";
    }

    case Ntype_op::LT:
    case Ntype_op::GT: {
      // Result type is 1 word; operands compare at cmp_w = max width of inputs.
      std::vector<std::pair<int, Node_pin>> ordered;
      for (const auto &e : inp_edges_ordered(node)) {
        ordered.emplace_back(e.sink.get_port_id(), e.driver);
      }
      // pid 0 = A (n-ary, signed-flag uses pid 1 in lgraph; for v1 we treat
      //              standard LT/GT as 2-input unsigned).
      // Conservative: collect all driver-side pins and require exactly two.
      std::vector<Node_pin> drivers;
      for (auto &p : ordered) drivers.push_back(p.second);
      if (drivers.size() != 2) {
        fatal(ctx, "LT/GT node n_" + std::to_string(node_id(node))
                       + " has " + std::to_string(drivers.size()) + " inputs; v1 expects 2.");
      }
      uint32_t wa = pin_width(ctx, drivers[0], node);
      uint32_t wb = pin_width(ctx, drivers[1], node);
      uint32_t cmp_w = std::max(wa, wb);
      // v1 default: emit unsigned comparison. Signed variant requires LGraph
      // signedness attribute - not threaded in v1 (warn-and-emit-unsigned).
      const char *cmp = (op == Ntype_op::LT) ? "<" : ">";
      return "(if " + ucast_pin_at(ctx, drivers[0], cmp_w) + " " + cmp + " "
             + ucast_pin_at(ctx, drivers[1], cmp_w)
             + " then " + lit_one(1) + " else " + lit_zero(1) + ")";
    }

    case Ntype_op::EQ: {
      std::vector<Node_pin> drivers;
      for (const auto &e : inp_edges_ordered(node)) drivers.push_back(e.driver);
      if (drivers.empty()) fatal(ctx, "EQ node n_" + std::to_string(node_id(node)) + " is empty.");
      if (drivers.size() == 1) return lit_one(1);
      uint32_t eq_w = 0;
      for (auto &dp : drivers) eq_w = std::max(eq_w, pin_width(ctx, dp, node));
      const std::string head = ucast_pin_at(ctx, drivers[0], eq_w);
      std::string conj;
      for (size_t i = 1; i < drivers.size(); ++i) {
        const auto t = ucast_pin_at(ctx, drivers[i], eq_w) + " = " + head;
        conj = (i == 1) ? t : (conj + " \\<and> " + t);
      }
      return "(if " + conj + " then " + lit_one(1) + " else " + lit_zero(1) + ")";
    }

    case Ntype_op::SHL: {
      Node_pin a;
      std::vector<Node_pin> bs;
      bool have_a = false;
      for (const auto &e : inp_edges_ordered(node)) {
        auto pname = sink_pin_name(e);
        if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
        else if (pname == "B" || pname == "b" || e.sink.get_port_id() == 1) { bs.push_back(e.driver); }
      }
      if (!have_a || bs.empty()) {
        const auto reason = "SHL node n_" + std::to_string(node_id(node)) + " is malformed";
        if (ctx.strict) fatal(ctx, reason + ".");
        return undefined_at(w, reason);
      }
      const std::string a_cast = ucast_pin_at(ctx, a, w);
      if (bs.size() == 1) {
        auto bw = pin_width(ctx, bs[0], node);
        return "(push_bit (unat (" + driver_expr_at(ctx, bs[0], bw) + ")) " + a_cast + ")";
      }
      // Multi-shift-OR: inline OR-chain
      std::vector<std::string> shifts;
      for (auto &bd : bs) {
        auto bw = pin_width(ctx, bd, node);
        shifts.push_back("(push_bit (unat (" + driver_expr_at(ctx, bd, bw) + ")) " + a_cast + ")");
      }
      return nary_op_func("Bit_Operations.or", shifts);
    }

    case Ntype_op::SRA: {
      Node_pin a, b;
      bool have_a = false, have_b = false;
      std::vector<Node_pin> ordered;
      std::vector<std::string> pins;
      for (const auto &e : inp_edges_ordered(node)) {
        ordered.push_back(e.driver);
        auto pname = sink_pin_name(e);
        pins.push_back(pname + "#" + std::to_string(e.sink.get_port_id()));
        if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
        else if (pname == "B" || pname == "b" || e.sink.get_port_id() == 1) { b = e.driver; have_b = true; }
      }
      if ((!have_a || !have_b) && ordered.size() == 2) {
        a = ordered[0];
        b = ordered[1];
        have_a = true;
        have_b = true;
      }
      if (!have_a || !have_b) {
        std::string detail;
        for (const auto &p : pins) detail += " " + p;
        const auto reason = "SRA node n_" + std::to_string(node_id(node)) + " missing a/b; pins:" + detail;
        // pass.cprop can leave degenerate SRA nodes after folding one side of
        // the shift. A missing data operand represents the folded zero value;
        // a missing shift operand represents shift-by-zero.
        if (!have_a && have_b) {
          return lit_zero(w);
        }
        if (have_a && !have_b) {
          return ucast_pin_at(ctx, a, w);
        }
        if (ctx.strict) fatal(ctx, reason);
        return undefined_at(w, reason);
      }
      uint32_t shift_w = pin_width(ctx, b, node);
      return "(sem_sra " + ucast_pin_at(ctx, a, w) + " "
             + ucast_pin_at(ctx, b, shift_w) + ")";
    }

    case Ntype_op::Mux: {
      // pin 0 = selector; pin 1..k = data options
      Node_pin sel;
      bool have_sel = false;
      // Collect drivers indexed by pid (1, 2, 3, ...)
      std::map<int, Node_pin> options;
      uint32_t sel_w = 0;
      for (const auto &e : inp_edges_ordered(node)) {
        int pid = e.sink.get_port_id();
        if (pid == 0) {
          sel = e.driver;
          have_sel = true;
          sel_w = pin_width(ctx, e.driver, node);
        } else {
          options[pid] = e.driver;
        }
      }
      if (!have_sel || options.size() < 2) {
        fatal(ctx, "Mux node n_" + std::to_string(node_id(node))
                       + " is malformed (need selector + >=2 options).");
      }
      const std::string sel_e = driver_expr_at(ctx, sel, sel_w);
      // Form A (Boolean): exactly 2 options AND sel_w == 1
      if (options.size() == 2 && sel_w == 1) {
        // pid 1 = false / sel == 0; pid 2 = true / sel == 1
        const auto &false_drv = options.at(1);
        const auto &true_drv  = options.at(2);
        return "(if (" + sel_e + ") \\<noteq> 0 then "
               + ucast_pin_at(ctx, true_drv, w) + " else "
               + ucast_pin_at(ctx, false_drv, w) + ")";
      }
      // Form B (n-way numeric, totalized)
      std::string out = "(";
      size_t idx = 0;
      for (auto &kv : options) {
        out += "if unat (" + sel_e + ") = " + std::to_string(idx) + " then "
               + ucast_pin_at(ctx, kv.second, w) + " else ";
        ++idx;
      }
      out += lit_zero(w) + ")";
      return out;
    }

    case Ntype_op::Sext: {
      Node_pin a, b;
      bool have_a = false, have_b = false;
      for (const auto &e : inp_edges_ordered(node)) {
        auto pname = sink_pin_name(e);
        if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
        else if (pname == "b" || e.sink.get_port_id() == 1) { b = e.driver; have_b = true; }
      }
      if (!have_a || !have_b) {
        const auto reason = "Sext node n_" + std::to_string(node_id(node)) + " missing a/b";
        if (ctx.strict) fatal(ctx, reason + ".");
        return undefined_at(w, reason);
      }
      uint32_t src_w = pin_width(ctx, a, node);
      uint32_t amount_w = pin_width(ctx, b, node);
      return "((word_of_int (signed_take_bit (unat (" + driver_expr_at(ctx, b, amount_w)
             + ")) (uint " + ucast_pin_at(ctx, a, src_w) + "))) "
             + ty_word(w) + ")";
    }

    case Ntype_op::Get_mask: {
      Node_pin a, mask;
      bool have_a = false, have_m = false;
      std::vector<Node_pin> ordered;
      std::vector<std::string> pins;
      for (const auto &e : inp_edges_ordered(node)) {
        ordered.push_back(e.driver);
        auto pname = sink_pin_name(e);
        pins.push_back(pname + "#" + std::to_string(e.sink.get_port_id()));
        if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
        else if (pname == "mask" || e.sink.get_port_id() == 1) { mask = e.driver; have_m = true; }
      }
      if ((!have_a || !have_m) && ordered.size() == 2) {
        a = ordered[0];
        mask = ordered[1];
        have_a = true;
        have_m = true;
      }
      if (!have_a || !have_m) {
        std::string detail;
        for (const auto &p : pins) detail += " " + p;
        const auto reason = "Get_mask node n_" + std::to_string(node_id(node)) + " missing a/mask; pins:" + detail;
        if (ctx.strict) fatal(ctx, reason);
        return undefined_at(w, reason);
      }
      uint32_t src_w  = pin_width(ctx, a, node);
      uint32_t mask_w = pin_width(ctx, mask, node);
      return "((sem_get_mask " + ucast_pin_at(ctx, a, src_w) + " "
             + ucast_pin_at(ctx, mask, mask_w) + ") " + ty_word(w) + ")";
    }

    case Ntype_op::Set_mask: {
      Node_pin a, mask, value;
      bool have_a = false, have_m = false, have_v = false;
      for (const auto &e : inp_edges_ordered(node)) {
        auto pname = sink_pin_name(e);
        if (pname == "a" || e.sink.get_port_id() == 0) { a = e.driver; have_a = true; }
        else if (pname == "mask" || e.sink.get_port_id() == 1) { mask = e.driver; have_m = true; }
        else if (pname == "value" || e.sink.get_port_id() == 2) { value = e.driver; have_v = true; }
      }
      if (!have_a || !have_m || !have_v) {
        const auto reason = "Set_mask node n_" + std::to_string(node_id(node)) + " missing operands";
        if (ctx.strict) fatal(ctx, reason + ".");
        return undefined_at(w, reason);
      }
      auto mask_w = pin_width(ctx, mask, node);
      auto value_w = pin_width(ctx, value, node);
      return "(sem_set_mask " + ucast_pin_at(ctx, a, w) + " "
             + driver_expr_at(ctx, mask, mask_w) + " " + driver_expr_at(ctx, value, value_w) + ")";
    }

    // Unsupported in v1.
    case Ntype_op::Memory:
    case Ntype_op::Latch:
    case Ntype_op::Fflop:
    case Ntype_op::Sub:
    case Ntype_op::LUT:
    case Ntype_op::AttrSet:
    case Ntype_op::Hotmux: {
      const std::string opn(Ntype::get_name(op));
      if (ctx.strict) {
        fatal(ctx, "unsupported op `" + opn + "` at node n_" + std::to_string(node_id(node))
                       + " (output width " + std::to_string(w) + "). v1 strict mode rejects it; "
                         "rerun with strict:false to emit `undefined` stubs (proofs will fail).");
      }
      return "(undefined :: " + std::to_string(w) + " word) (* TODO: " + opn + " *)";
    }

    default:
      throw Emit_error("internal: unhandled Ntype_op in emit_node_expr");
  }
}

// Reachability: starting from the union of root-set drivers, walk reverse-fanin
// stopping at primary inputs, flop outputs, and Dlop nodes. Emit a topological
// order of all reached internal combinational nodes.
std::vector<Node> reachable_topo_order(Ctx & /*ctx*/, const std::vector<Node_pin> &roots,
                                       const absl::flat_hash_set<uint32_t> &flop_nids) {
  absl::flat_hash_set<uint32_t> reached;
  std::vector<Node>             order;

  // Iterative DFS post-order to produce dependency-respecting order.
  std::vector<std::pair<Node, bool>> stack;
  for (const auto &dpin : roots) {
    auto n = pin_node(dpin);
    if (pin_is_input(dpin) || pin_is_const(dpin) || flop_nids.count(node_id(n)) > 0) continue;
    if (reached.count(node_id(n)) > 0) continue;
    stack.emplace_back(n, false);
    while (!stack.empty()) {
      auto &[cur, visited] = stack.back();
      if (visited) {
        if (reached.insert(node_id(cur)).second) {
          order.push_back(cur);
        }
        stack.pop_back();
        continue;
      }
      visited = true;
      // Push children (driver pins of input edges).
      for (const auto &e : inp_edges_ordered(cur)) {
        auto child = pin_node(e.driver);
        if (pin_is_input(e.driver) || pin_is_const(e.driver)) continue;
        if (flop_nids.count(node_id(child)) > 0) continue;  // cut at flop output
        if (reached.count(node_id(child)) > 0) continue;
        stack.emplace_back(child, false);
      }
    }
  }
  return order;
}

template <typename Edge>
uint32_t consumer_expected_width(const Edge &e) {
  auto sink = e.sink;
  auto sw = raw_pin_width(sink);
  if (sw > 0) return static_cast<uint32_t>(sw);

  auto sn = pin_node(sink);
  if (node_is_flop(sn)) {
    auto pname = sink_pin_name(e);
    if (pname == "din") return static_cast<uint32_t>(raw_node_width(sn));
    return 1;
  }
  auto nw = raw_node_width(sn);
  if (nw > 0) return static_cast<uint32_t>(nw);
  return 0;
}

void normalize_zero_width_get_masks(const Ctx &ctx, const std::vector<Node> &topo) {
  for (auto node : topo) {
    if (!node_is_op(node, Ntype_op::Get_mask) || raw_node_width(node) != 0) continue;

    uint32_t expected = 0;
    std::string consumers;
    for (const auto &e : node.out_edges()) {
      const auto ew = consumer_expected_width(e);
      consumers += " n_" + std::to_string(node_id(pin_node(e.sink))) + ":" + std::to_string(ew);
      if (ew == 0) continue;
      if (expected == 0) {
        expected = ew;
      } else if (expected != ew) {
        fatal(ctx, "Get_mask node n_" + std::to_string(node_id(node))
                       + " has conflicting consumer widths:" + consumers);
      }
    }

    if (expected == 0) {
      fatal(ctx, "Get_mask node n_" + std::to_string(node_id(node))
                     + " has zero width and no inferable consumer width; consumers:" + consumers);
    }
    if (static_cast<size_t>(expected) > ctx.max_width) {
      fatal(ctx, "Get_mask node n_" + std::to_string(node_id(node))
                     + " inferred width " + std::to_string(expected)
                     + " > max_width=" + std::to_string(ctx.max_width));
    }

    auto dpin = node.create_driver_pin(0);
    livehd::graph_util::set_bits(dpin, expected);
  }
}

}  // namespace

void Pass_isabelle::emit_for_graph(const std::shared_ptr<hhds::Graph>& graph) const {
  TRACE_EVENT("pass", "ISABELLE_emit_for_graph");

  if (!graph) {
    warn("pass.isabelle received a null Graph instance");
    return;
  }
  auto *g = graph.get();

  Ctx ctx;
  ctx.g          = g;
  ctx.base_name  = std::string(g->get_name());
  ctx.top_name   = sanitize(ctx.base_name);
  ctx.strict     = strict;
  ctx.normalize  = normalize;
  ctx.max_width  = max_width;
  ctx.cert_wf    = cert_wf;
  ctx.cert_wf_fallback = cert_wf_fallback;
  ctx.cert_chunk_size = cert_chunk_size;
  ctx.cert_chunk_limit = cert_chunk_limit;
  ctx.output_dir = (path == "/INVALID") ? std::string(".") : path;

  // ---- Collect inputs / outputs / flops --------------------------------
  auto gio = g->get_io();
  uint32_t next_input_source_id = 2000000000;
  for (const auto &decl : gio->get_input_pin_decls()) {
    auto raw = std::string(decl.name);
    auto pin = g->get_input_pin(decl.name);
    auto fld = make_field_name("in_", raw, ctx.used_fields);
    ctx.input_field[raw]  = fld;
    auto w = static_cast<uint32_t>(livehd::graph_util::bits_of(pin, *gio, decl.name));
    if (w == 0 || w > ctx.max_width) {
      check_width(ctx, pin_node(pin), w, "input port");
    }
    ctx.input_width[raw] = w;
    ctx.input_source_id[raw] = next_input_source_id++;
    ctx.name_map_rows.emplace_back("input", raw, fld);
  }
  for (const auto &decl : gio->get_output_pin_decls()) {
    auto raw = std::string(decl.name);
    auto pin = g->get_output_pin(decl.name);
    auto fld = make_field_name("out_", raw, ctx.used_fields);
    ctx.output_field[raw]  = fld;
    auto w = static_cast<uint32_t>(livehd::graph_util::bits_of(pin, *gio, decl.name));
    if (w == 0 || w > ctx.max_width) {
      check_width(ctx, pin_node(pin), w, "output port");
    }
    ctx.output_width[raw] = w;
    ctx.name_map_rows.emplace_back("output", raw, fld);
  }

  // Flops
  std::vector<Node> flop_nodes;
  absl::flat_hash_set<uint32_t> flop_nids;
  for (auto node : g->fast_class()) {
    if (node_is_flop(node)) {
      flop_nodes.emplace_back(node);
      flop_nids.insert(node_id(node));
      // Use first user-visible out-edge driver name, else fall back to nid.
      std::string flop_raw;
      for (const auto &e : node.out_edges()) {
        auto wn = livehd::graph_util::wire_name(e.driver);
        if (!wn.empty() && wn[0] != '_') { flop_raw = std::string(wn); break; }
      }
      if (flop_raw.empty()) flop_raw = "flop_" + std::to_string(node_id(node));
      auto fld = make_field_name("st_", flop_raw, ctx.used_fields);
      ctx.flop_field[node_id(node)]  = fld;
      auto w = raw_node_width(node);
      check_width(ctx, node, w, "flop");
      ctx.flop_width[node_id(node)] = w;
      ctx.name_map_rows.emplace_back("state", flop_raw, fld);
    }
  }
  bool sequential = !flop_nodes.empty();

  // ---- Compute root set for reachability -------------------------------
  std::vector<Node_pin> roots;
  // (a) drivers of every graph output
  std::map<std::string, Node_pin> out_drivers;
  for (const auto &decl : gio->get_output_pin_decls()) {
    auto out_sink = g->get_output_pin(decl.name);
    auto edges    = out_sink.inp_edges();
    if (!edges.empty()) {
      out_drivers[std::string(decl.name)] = edges.front().driver;
    }
  }
  for (auto &kv : out_drivers) roots.push_back(kv.second);

  // (b) drivers of every flop's D/reset/enable input
  std::map<uint32_t, Node_pin> flop_din, flop_reset, flop_enable;
  for (auto &fn : flop_nodes) {
    for (const auto &e : inp_edges_ordered(fn)) {
      auto pname = sink_pin_name(e);
      if (pname == "din")        flop_din[node_id(fn)]    = e.driver;
      else if (pname == "reset_pin") flop_reset[node_id(fn)]  = e.driver;
      else if (pname == "enable")    flop_enable[node_id(fn)] = e.driver;
      else if (pname == "negreset")  flop_reset[node_id(fn)]  = e.driver;
    }
  }
  for (auto &kv : flop_din)    roots.push_back(kv.second);
  for (auto &kv : flop_reset)  roots.push_back(kv.second);
  for (auto &kv : flop_enable) roots.push_back(kv.second);

  // ---- Topo-order all reachable internal combinational nodes -----------
  auto topo = reachable_topo_order(ctx, roots, flop_nids);
  if (ctx.normalize) {
    normalize_zero_width_get_masks(ctx, topo);
  }

  // ---- Emit theory file ------------------------------------------------
  std::string theory_name = ctx.top_name + "_Lgraph";
  std::string theory_path = ctx.output_dir + "/" + theory_name + ".thy";
  std::ofstream ofs(theory_path);
  if (!ofs) {
    warn("pass.isabelle could not write " + theory_path);
    return;
  }

  ofs << "(* Generated by pass.isabelle. Do not edit by hand. *)\n";
  ofs << "theory " << theory_name << "\n";
  ofs << "  imports \"DINO_Semantic_Primitives_Test.SemanticPrimitives\"\n";
  ofs << "begin\n\n";

  if (sequential) {
    ofs << "(* ---------------------------------------------------------------------- *)\n";
    ofs << "(*  v1 sequential abstraction notes (generated by pass.isabelle).         *)\n";
    ofs << "(*                                                                        *)\n";
    ofs << "(*  - One call to <top>_step models exactly ONE positive clock edge.      *)\n";
    ofs << "(*  - Async reset is sampled ONLY at the step boundary.                   *)\n";
    ofs << "(*  - Reset polarity (negreset) is folded into reset_active at gen time.  *)\n";
    ofs << "(*  - Enable defaults to True when the LGraph port is unconnected.        *)\n";
    ofs << "(*  See plan 20.4.1 for the full v1 step rule.                            *)\n";
    ofs << "(* ---------------------------------------------------------------------- *)\n\n";
  }

  // Name-map header.
  ofs << "(* RTL-to-Isabelle name map:\n";
  for (auto &row : ctx.name_map_rows) {
    ofs << "     " << std::get<0>(row) << " " << std::get<1>(row) << " -> " << std::get<2>(row) << "\n";
  }
  ofs << "*)\n\n";

  try {
    // Records.
    ofs << "record " << ctx.top_name << "_in =\n";
    for (auto &kv : ctx.input_field) {
      ofs << "  " << kv.second << " :: \"" << ctx.input_width[kv.first] << " word\"\n";
    }
    if (ctx.input_field.empty()) {
      ofs << "  in_dummy :: \"1 word\"\n";  // record cannot be empty
    }
    ofs << "\n";

    ofs << "record " << ctx.top_name << "_out =\n";
    for (auto &kv : ctx.output_field) {
      ofs << "  " << kv.second << " :: \"" << ctx.output_width[kv.first] << " word\"\n";
    }
    if (ctx.output_field.empty()) {
      ofs << "  out_dummy :: \"1 word\"\n";
    }
    ofs << "\n";

    if (sequential) {
      ofs << "record " << ctx.top_name << "_state =\n";
      for (auto &kv : ctx.flop_field) {
        ofs << "  " << kv.second << " :: \"" << ctx.flop_width[kv.first] << " word\"\n";
      }
      ofs << "\n";
    }

    // <top>_comb body: shared let-chain over the topo-ordered emit set.
    auto emit_let_chain = [&]() -> std::string {
      std::ostringstream oss;
      oss << "    (let\n";
      bool first = true;
      for (const auto &n : topo) {
        if (!first) oss << ";\n";
        first = false;
        auto w   = node_width(ctx, n);
        auto rhs = emit_node_expr(ctx, n);
        oss << "       n_" << node_id(n) << " = (" << rhs << " " << ty_word(w) << ")";
      }
      if (first) {
        // No bindings - use trivial `let dummy = 0 in ...`
        oss << "       _dummy = (0 :: 1 word)";
      }
      oss << "\n     in \\<lparr>";
      // Output record constructor.
      bool firstf = true;
      for (auto &kv : ctx.output_field) {
        if (!firstf) oss << ", ";
        firstf = false;
        const auto &out_name = kv.first;
        auto drv             = out_drivers.find(out_name);
        if (drv == out_drivers.end()) {
          oss << kv.second << " = " << lit_zero(ctx.output_width[out_name]);
        } else {
          oss << kv.second << " = " << ucast_pin_at(ctx, drv->second, ctx.output_width[out_name]);
        }
      }
      if (ctx.output_field.empty()) {
        oss << "out_dummy = (0 :: 1 word)";
      }
      oss << "\\<rparr>)";
      return oss.str();
    };

    // <top>_comb signature.
    if (sequential) {
      ofs << "definition " << ctx.top_name << "_comb :: \""
          << ctx.top_name << "_in \\<Rightarrow> "
          << ctx.top_name << "_state \\<Rightarrow> "
          << ctx.top_name << "_out\" where\n";
      ofs << "  \"" << ctx.top_name << "_comb i s =\n" << emit_let_chain() << "\"\n\n";
    } else {
      ofs << "definition " << ctx.top_name << "_comb :: \""
          << ctx.top_name << "_in \\<Rightarrow> "
          << ctx.top_name << "_out\" where\n";
      ofs << "  \"" << ctx.top_name << "_comb i =\n" << emit_let_chain() << "\"\n\n";
    }

    // Sequential-only: emit <top>_next and <top>_step.
    if (sequential) {
      ofs << "definition " << ctx.top_name << "_next :: \""
          << ctx.top_name << "_in \\<Rightarrow> "
          << ctx.top_name << "_state \\<Rightarrow> "
          << ctx.top_name << "_state\" where\n";
      ofs << "  \"" << ctx.top_name << "_next i s =\n";
      ofs << "    (let\n";
      // Emit the same combinational let-chain (it covers reset/enable/din cones).
      bool first = true;
      for (const auto &n : topo) {
        if (!first) ofs << ";\n";
        first = false;
        auto w   = node_width(ctx, n);
        auto rhs = emit_node_expr(ctx, n);
        ofs << "       n_" << node_id(n) << " = (" << rhs << " " << ty_word(w) << ")";
      }
      // Per-flop new value via flop_next.
      for (auto &fn : flop_nodes) {
        auto fid       = node_id(fn);
        auto fld       = ctx.flop_field[fid];
        auto fw        = ctx.flop_width[fid];
        std::string din_e = "(0 :: " + std::to_string(fw) + " word)";
        if (auto it = flop_din.find(fid); it != flop_din.end()) {
          din_e = ucast_pin_at(ctx, it->second, fw);
        }
        std::string reset_e = "False";
        if (auto it = flop_reset.find(fid); it != flop_reset.end()) {
          reset_e = "((" + driver_expr_at(ctx, it->second, 1) + ") \\<noteq> 0)";
        }
        std::string en_e = "True";
        if (auto it = flop_enable.find(fid); it != flop_enable.end()) {
          en_e = "((" + driver_expr_at(ctx, it->second, 1) + ") \\<noteq> 0)";
        }
        if (!first) ofs << ";\n";
        first = false;
        ofs << "       new_" << fld << " = flop_next " << reset_e << " "
            << "(0 :: " << fw << " word) " << en_e << " " << din_e
            << " (" << fld << " s)";
      }
      ofs << "\n     in s\\<lparr>";
      bool fc = true;
      for (auto &fn : flop_nodes) {
        if (!fc) ofs << ", ";
        fc = false;
        auto fld = ctx.flop_field[node_id(fn)];
        ofs << fld << " := new_" << fld;
      }
      ofs << "\\<rparr>)\"\n\n";

      ofs << "definition " << ctx.top_name << "_step :: \""
          << ctx.top_name << "_in \\<Rightarrow> "
          << ctx.top_name << "_state \\<Rightarrow> "
          << ctx.top_name << "_state \\<times> "
          << ctx.top_name << "_out\" where\n";
      ofs << "  \"" << ctx.top_name << "_step i s = ("
          << ctx.top_name << "_next i s, " << ctx.top_name << "_comb i s)\"\n\n";
    }

    ofs << "end\n";
  } catch (const Emit_error &err) {
    ofs.close();
    std::cerr << err.what() << "\n";
    if (ctx.strict) {
      // Remove half-written file
      std::remove(theory_path.c_str());
      Pass::error(err.what());
    }
    return;
  }

  emit_cert_theory(ctx, topo, flop_nodes, flop_nids,
                   out_drivers, flop_din, flop_reset, flop_enable);

  std::cout << "pass.isabelle: " << ctx.base_name << " -> " << theory_path
            << " (" << topo.size() << " nodes, " << flop_nodes.size() << " flops, sequential="
            << (sequential ? "yes" : "no") << ")\n";
}
