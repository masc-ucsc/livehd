//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
//  pass.rocq - emit per-design Rocq (Coq) files for graph-certificate based
//  translation proofs.  This is the third prover target after pass.isabelle and
//  pass.lean; it mirrors their public knobs so the existing DINO/CVA6 generation
//  scripts port over, and adds one Rocq-specific knob (`eval_engine`) because
//  Rocq is the only one of the three where the speed/trusted-base trade on
//  computational proofs is a real, exposed choice.
//
//  Two files are emitted per graph:
//
//    <Top>_Lgraph.v       the fast executable model (records + comb/next/step)
//    <Top>_Lgraph_Cert.v  the graph certificate + cert model + bridge lemmas
//
//  The split follows pass.isabelle rather than pass.lean because Rocq caches a
//  compiled `.vo` per file, so iterating on the certificate does not re-elaborate
//  the (expensive) model.  Lean has no such compilation unit, which is why its
//  emitter produces one file.

#include "pass_rocq.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
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

static Pass_plugin pass_plugin_rocq("pass_rocq", Pass_rocq::setup);

namespace {

RocqCertWFMode parse_cert_wf_mode(std::string_view mode) {
  if (mode == "eval") {
    return RocqCertWFMode::Eval;
  }
  if (mode == "sorry") {
    return RocqCertWFMode::Sorry;
  }
  if (mode == "chunked") {
    return RocqCertWFMode::Chunked;
  }
  return RocqCertWFMode::Skip;
}

RocqCertWFFallback parse_cert_wf_fallback(std::string_view mode) {
  if (mode == "sorry") {
    return RocqCertWFFallback::Sorry;
  }
  if (mode == "eval") {
    return RocqCertWFFallback::Eval;
  }
  return RocqCertWFFallback::Fail;
}

RocqEvalEngine parse_eval_engine(std::string_view mode) {
  if (mode == "native") {
    return RocqEvalEngine::Native;
  }
  if (mode == "cbv") {
    return RocqEvalEngine::Cbv;
  }
  return RocqEvalEngine::Vm;
}

const char* eval_engine_tactic(RocqEvalEngine e) {
  switch (e) {
    case RocqEvalEngine::Native: return "native_compute";
    case RocqEvalEngine::Cbv: return "cbv";
    case RocqEvalEngine::Vm: break;
  }
  return "vm_compute";
}

// Parse the max_width knob. "0"/"unlimited"/"inf"/"none" (case-insensitive) mean
// no cap -> SIZE_MAX (every `w > max_width` guard is then always-false, so the
// upper bound is disabled while the separate `w == 0` unsized-node check stays).
size_t parse_max_width(std::string_view s, size_t dflt = 1024) {
  if (s.empty()) {
    return dflt;
  }
  std::string l(s);
  for (auto& c : l) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (l == "unlimited" || l == "inf" || l == "none" || l == "0") {
    return std::numeric_limits<size_t>::max();
  }
  try {
    size_t v = std::stoul(l);
    return v == 0 ? std::numeric_limits<size_t>::max() : v;
  } catch (...) {
    return dflt;
  }
}

// Rocq keywords and the vernacular openers that cannot start an identifier in a
// generated definition or record field.  Also covers the identifiers this pass's
// own support library exports, so a design signal named `mem_read` cannot shadow
// the primitive of the same name.
const std::unordered_set<std::string> kRocqReserved = {
    "as",         "at",       "cofix",     "else",       "end",       "exists",   "exists2",  "fix",
    "for",        "forall",   "fun",       "if",         "IF",        "in",       "let",      "match",
    "mod",        "Prop",     "return",    "Set",        "then",      "Type",     "using",    "where",
    "with",       "struct",   "measure",   "wf",         "by",        "andb",     "orb",      "negb",
    "Definition", "Theorem",  "Lemma",     "Record",     "Inductive", "Fixpoint", "Section",  "Module",
    "Import",     "Export",   "Require",   "Notation",   "Instance",  "Class",    "Axiom",    "Qed",
    "Defined",    "Admitted", "Proof",     "Arguments",  "Scheme",    "Variable", "Hypothesis",
    "Example",    "Goal",     "Ltac",      "Hint",       "Open",      "Local",    "Global",   "nat",
    "bool",       "list",     "option",    "true",       "false",     "cons",     "nil",      "None",
    "Some",       "pair",     "fst",       "snd",        "prod",      "unit",     "tt",       "Z",
    "N",          "BitVec",   "BV",
};

std::string sanitize_rocq(std::string_view name) {
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
  // A leading digit, or a leading underscore (Rocq treats `_foo` as usable but
  // `_` alone is a wildcard, and generated names read better prefixed).
  if ((out[0] >= '0' && out[0] <= '9') || out[0] == '_') {
    out = "id" + out;
  }
  if (kRocqReserved.count(out) > 0) {
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

bool node_is_op(const Node& node, Ntype_op op) { return node_op(node) == op; }

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
  std::string base = std::string(role) + sanitize_rocq(rtl_name);
  std::string name = base;
  size_t      n    = 0;
  while (used.count(name) > 0 || kRocqReserved.count(name) > 0) {
    ++n;
    name = base + "_" + std::to_string(n);
  }
  used.insert(name);
  return name;
}

struct Memory_port_info {
  size_t   port_id = 0;
  bool     rdport  = false;
  Node_pin addr;
  Node_pin din;
  Node_pin enable;
  Node_pin clock;
  uint32_t driver_pid = 0;  // read-output driver pin id, valid only for rdport
};

struct Memory_info {
  Node                          node;
  uint32_t                      nid        = 0;
  std::string                   field;
  std::string                   raw_name;
  uint32_t                      bits       = 0;
  uint32_t                      addr_width = 1;
  uint64_t                      size       = 0;
  uint32_t                      wensize    = 0;
  int64_t                       type       = 0;
  int64_t                       fwd        = 0;
  int64_t                       posclk     = 1;
  bool                          undef      = false;
  std::vector<Memory_port_info> ports;
  std::vector<size_t>           read_ports;
  std::vector<size_t>           write_ports;
  bool                          sync = false;
  std::map<size_t, std::string> read_reg_field;
};

struct RocqCtx {
  hhds::Graph* g = nullptr;
  std::string  top_name;
  std::string  base_name;
  bool         strict    = true;
  size_t       max_width = 1024;

  absl::flat_hash_set<std::string> used_fields;

  std::map<std::string, std::string> input_field;
  std::map<std::string, uint32_t>    input_width;
  std::map<std::string, uint32_t>    input_source_id;

  std::map<std::string, std::string> output_field;
  std::map<std::string, uint32_t>    output_width;

  std::map<uint32_t, std::string> flop_field;
  std::map<uint32_t, uint32_t>    flop_width;

  std::map<uint32_t, Memory_info> memory_info;
};

[[noreturn]] void fatal(const RocqCtx& /*ctx*/, const std::string& msg) { throw Emit_error("[ERROR] pass.rocq: " + msg); }

void check_width(const RocqCtx& ctx, const Node& node, uint32_t w, std::string_view what) {
  if (w == 0) {
    fatal(ctx,
          "node n_" + std::to_string(node_id(node)) + " (" + std::string(what)
              + ") has zero width; Rocq BitVec generation requires positive widths.");
  }
  if (w > ctx.max_width) {
    fatal(ctx,
          "node n_" + std::to_string(node_id(node)) + " (" + std::string(what) + ") has width " + std::to_string(w)
              + " > max_width=" + std::to_string(ctx.max_width));
  }
}

uint32_t pin_width(const RocqCtx& ctx, const Node_pin& pin, const Node& owner) {
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

uint32_t node_width(const RocqCtx& ctx, const Node& node) {
  auto w = raw_node_width(node);
  check_width(ctx, node, w, "node");
  return static_cast<uint32_t>(w);
}

uint32_t ceil_log2_u64(uint64_t v) {
  if (v <= 1) {
    return 1;
  }
  --v;
  uint32_t w = 0;
  while (v != 0) {
    ++w;
    v >>= 1;
  }
  return w == 0 ? 1 : w;
}

int64_t const_pin_int(const RocqCtx& ctx, const Node_pin& pin, const Node& owner, std::string_view field) {
  if (!pin_is_const(pin)) {
    fatal(ctx, "Memory node n_" + std::to_string(node_id(owner)) + " has non-constant " + std::string(field) + " policy pin.");
  }
  auto v = pin_const_value(pin);
  if (!v.is_just_i64()) {
    fatal(ctx, "Memory node n_" + std::to_string(node_id(owner)) + " has non-integer " + std::string(field) + " policy pin.");
  }
  return v.to_just_i64();
}

// Lenient reader for policy pins that are parsed but unused in the emitted
// mem_read/mem_write model (fwd forwarding, clock polarity): the front end can
// drive these with a non-constant signal, so fall back to a default instead of
// aborting.  Mirrors pass.lean.
int64_t const_pin_int_or(const Node_pin& pin, int64_t dflt) {
  if (!pin_is_const(pin)) {
    return dflt;
  }
  auto v = pin_const_value(pin);
  return v.is_just_i64() ? v.to_just_i64() : dflt;
}

std::string memory_policy_summary(const Memory_info& mi) {
  std::ostringstream oss;
  oss << "memory node n_" << mi.nid << " bits=" << mi.bits << " size=" << mi.size << " addr_width=" << mi.addr_width
      << " type=" << mi.type << " fwd=" << mi.fwd << " undef=" << (mi.undef ? 1 : 0) << " posclk=" << mi.posclk
      << " wensize=" << mi.wensize << " rdports=" << mi.read_ports.size() << " wrports=" << mi.write_ports.size();
  return oss.str();
}

Memory_info parse_memory_info(RocqCtx& ctx, const Node& node) {
  Memory_info mi;
  mi.node = node;
  mi.nid  = node_id(node);

  const auto stride = static_cast<size_t>(Ntype::Memory_port_stride);
  for (const auto& e : inp_edges_ordered(node)) {
    const auto raw_pid = static_cast<size_t>(e.sink.get_port_id());
    const auto pname   = std::string(Ntype::get_sink_name(Ntype_op::Memory, raw_pid % stride));
    const auto port_id = raw_pid / stride;
    if (mi.ports.size() <= port_id) {
      mi.ports.resize(port_id + 1);
    }
    mi.ports[port_id].port_id = port_id;

    if (pname == "bits") {
      const auto v = const_pin_int(ctx, e.driver, node, pname);
      if (v <= 0) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " has non-positive bits.");
      }
      mi.bits = static_cast<uint32_t>(v);
    } else if (pname == "size") {
      const auto v = const_pin_int(ctx, e.driver, node, pname);
      if (v <= 0) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " has non-positive size.");
      }
      mi.size = static_cast<uint64_t>(v);
    } else if (pname == "wensize") {
      const auto v = const_pin_int(ctx, e.driver, node, pname);
      if (v < 0) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " has negative wensize.");
      }
      mi.wensize = static_cast<uint32_t>(v);
    } else if (pname == "type") {
      mi.type = const_pin_int(ctx, e.driver, node, pname);
    } else if (pname == "fwd") {
      mi.fwd = const_pin_int_or(e.driver, 0);
    } else if (pname == "undef") {
      // ordering="none": the read-during-write window is UNDEFINED, but the
      // emitted policy tuple only knows the two DEFINED answers, so a
      // certificate about such a memory would assert a concrete value in a
      // window the RTL leaves x.  Refuse it below.
      mi.undef = true;
      if (pin_is_const(e.driver)) {
        mi.undef = !pin_const_value(e.driver).is_known_false();
      }
    } else if (pname == "posclk") {
      mi.posclk = const_pin_int_or(e.driver, 1);
    } else if (pname == "rdport") {
      mi.ports[port_id].rdport = const_pin_int(ctx, e.driver, node, pname) != 0;
    } else if (pname == "addr") {
      mi.ports[port_id].addr = e.driver;
    } else if (pname == "din") {
      mi.ports[port_id].din = e.driver;
    } else if (pname == "enable") {
      mi.ports[port_id].enable = e.driver;
    } else if (pname == "clock_pin") {
      mi.ports[port_id].clock = e.driver;
    }
  }

  if (mi.bits == 0 || mi.size == 0) {
    fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " is missing constant bits/size policy.");
  }
  check_width(ctx, node, mi.bits, "memory data");
  mi.addr_width = ceil_log2_u64(mi.size);
  if (mi.addr_width == 0 || mi.addr_width > ctx.max_width) {
    fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " has unsupported address width " + std::to_string(mi.addr_width));
  }
  if (mi.wensize == 0) {
    mi.wensize = 1;
  }
  if (mi.bits % mi.wensize != 0) {
    fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " has bits not divisible by wensize: bits=" + std::to_string(mi.bits)
                   + " wensize=" + std::to_string(mi.wensize));
  }

  for (size_t idx = 0; idx < mi.ports.size(); ++idx) {
    auto& p   = mi.ports[idx];
    p.port_id = idx;
    // A resize gap (a port_id that never appeared on any sink) is not a real port.
    if (p.addr.is_invalid() && p.din.is_invalid() && p.enable.is_invalid() && p.clock.is_invalid()) {
      continue;
    }
    if (p.rdport) {
      mi.read_ports.push_back(idx);
      if (p.addr.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " read port missing addr.");
      }
      if (p.enable.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " read port missing enable.");
      }
    } else {
      mi.write_ports.push_back(idx);
      if (p.addr.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " write port missing addr.");
      }
      if (p.enable.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " write port missing enable.");
      }
      if (p.din.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " write port missing din.");
      }
    }
  }

  // Read-data output pin id follows the cgen convention (cgen_verilog.cpp):
  // dout pid = (total write ports) + (read-port index in port order).
  for (size_t k = 0; k < mi.read_ports.size(); ++k) {
    mi.ports[mi.read_ports[k]].driver_pid = static_cast<uint32_t>(mi.write_ports.size() + k);
  }

  if (!(mi.type == 0 || mi.type == 1 || mi.type == 2)) {
    fatal(ctx, memory_policy_summary(mi) + ". pass.rocq memory supports async/array (type 0/2) and sync-read (type 1) only.");
  }
  if (mi.undef) {
    fatal(ctx,
          memory_policy_summary(mi)
              + ". pass.rocq cannot model ordering=\"none\" (the `undef` matrix): sram_1r1w_{read,write}_first only express "
                "the two DEFINED collision answers, so the certificate would assert a value in a window the emitted RTL "
                "leaves x. Use ordering=\"old\"/\"fwd\"/\"program\" to export this design.");
  }
  mi.sync = (mi.type == 1);

  return mi;
}

const Memory_info& memory_info_for(const RocqCtx& ctx, const Node& node) {
  auto it = ctx.memory_info.find(node_id(node));
  if (it == ctx.memory_info.end()) {
    throw Emit_error("internal: memory node n_" + std::to_string(node_id(node)) + " has no Memory_info.");
  }
  return it->second;
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

std::string int_of_const(const RocqCtx& ctx, const Node& node, const Dlop& v);

std::string lit_bv(uint32_t w, std::string_view v) {
  return "(bv_norm " + std::to_string(w) + " (" + std::string(v) + "))";
}

std::string lit_zero(uint32_t w) { return lit_bv(w, "0"); }

std::string lit_one(uint32_t w) { return lit_bv(w, "1"); }

std::string lit_const_at(const RocqCtx& ctx, const Node& node, const Dlop& v, uint32_t w) {
  if (w == 0 || static_cast<size_t>(w) > ctx.max_width) {
    check_width(ctx, node, w, "Const");
  }
  if (v.has_unknowns()) {
    if (ctx.strict) {
      fatal(ctx, "Const node n_" + std::to_string(node_id(node)) + " has X/Z bits; strict Rocq emission rejects four-valued logic.");
    }
    return lit_zero(w);
  }
  // ONE source of truth for constant spelling: the fast model and the
  // certificate leaf both go through `int_of_const`, so a constant is textually
  // identical on both sides by construction.  Divergent per-form spellings
  // (zero as `0#w`, -1 as `-1`, i64 as a bare decimal) cost pass.lean and
  // pass.isabelle a bridge bug each; see pass/lean/STEP5_BRIDGE_BUGS.md bug 4.
  return lit_bv(w, int_of_const(ctx, node, v));
}

// The Z literal for a constant.  Rocq's Z_scope parses arbitrary-precision
// decimal numerals directly, so unlike Lean there is no `Int.ofNat` wrapper to
// pick; the only shaping needed is parenthesising a negative.
std::string int_of_const(const RocqCtx& ctx, const Node& node, const Dlop& v) {
  if (v.has_unknowns()) {
    fatal(ctx, "Const node n_" + std::to_string(node_id(node)) + " has X/Z bits; strict Rocq certificate rejects four-valued logic.");
  }
  if (v.is_known_zero()) {
    return "0";
  }
  if (v.same_repr(*Dlop::create_integer(-1))) {
    return "(-1)";
  }
  const std::string decimal = v.is_just_i64() ? std::to_string(v.to_just_i64()) : v.to_decimal_string();
  if (!decimal.empty() && decimal.front() == '-') {
    return "(" + decimal + ")";
  }
  return decimal;
}

std::string input_name_for_pin(const RocqCtx& ctx, const Node_pin& pin) {
  auto pname = std::string(livehd::graph_util::pin_name_of(pin));
  if (!pname.empty() && ctx.input_field.contains(pname)) {
    return pname;
  }
  return {};
}

std::string driver_expr_at(const RocqCtx& ctx, const Node_pin& dpin, uint32_t expected_w);

std::string ucast_expr(const std::string& expr, uint32_t w) {
  return "(bv_zext " + std::to_string(w) + " " + expr + ")";
}

std::string ucast_pin_at(const RocqCtx& ctx, const Node_pin& dpin, uint32_t w) {
  return ucast_expr(driver_expr_at(ctx, dpin, w), w);
}

// ---------------------------------------------------------------------------
// Memories
// ---------------------------------------------------------------------------

// Folded write image: apply every write port to the current memory in port
// order, so a later (higher port_id) write WINS on a same-cycle same-address
// collision.  Each write is enable-gated; wensize>1 uses mem_write_be.
std::string memory_write_fold(const RocqCtx& ctx, const Memory_info& mi) {
  std::string acc = "(" + mi.field + " s)";
  for (auto widx : mi.write_ports) {
    const auto& p        = mi.ports.at(widx);
    const auto  addr     = ucast_pin_at(ctx, p.addr, mi.addr_width);
    const auto  data     = ucast_pin_at(ctx, p.din, mi.bits);
    const auto  enable_w = std::max<uint32_t>(1, mi.wensize);
    const auto  enable   = ucast_pin_at(ctx, p.enable, enable_w);
    const auto  we       = "(bitvec_nonzero " + enable + ")";
    if (mi.wensize <= 1) {
      acc = "(if " + we + " then mem_write " + acc + " " + addr + " " + data + " else " + acc + ")";
    } else {
      const auto byte_w = mi.bits / mi.wensize;
      acc = "(if " + we + " then mem_write_be " + acc + " " + addr + " " + data + " " + enable + " " + std::to_string(byte_w)
            + " else " + acc + ")";
    }
  }
  return acc;
}

std::string memory_read_enable_port(const RocqCtx& ctx, const Memory_info& mi, size_t port_idx) {
  const auto& rp = mi.ports.at(port_idx);
  return "(bitvec_nonzero " + ucast_pin_at(ctx, rp.enable, pin_width(ctx, rp.enable, mi.node)) + ")";
}

// Raw (ungated) read value.  Read-during-write policy is memory-wide: fwd=1
// (write-first / transparent) reads the post-write folded image; else the old
// memory.
std::string memory_raw_read_port(const RocqCtx& ctx, const Memory_info& mi, size_t port_idx) {
  const auto& rp    = mi.ports.at(port_idx);
  const auto  raddr = ucast_pin_at(ctx, rp.addr, mi.addr_width);
  const auto  base  = (mi.fwd == 1 && !mi.write_ports.empty()) ? memory_write_fold(ctx, mi) : ("(" + mi.field + " s)");
  return "(mem_read " + base + " " + raddr + ")";
}

std::string memory_read_port_expr(const RocqCtx& ctx, const Memory_info& mi, size_t port_idx) {
  if (mi.sync) {
    return "(" + mi.read_reg_field.at(port_idx) + " s)";
  }
  return "(if " + memory_read_enable_port(ctx, mi, port_idx) + " then " + memory_raw_read_port(ctx, mi, port_idx) + " else "
         + lit_zero(mi.bits) + ")";
}

std::string memory_sync_reg_next_port(const RocqCtx& ctx, const Memory_info& mi, size_t port_idx) {
  return "(sram_sync_read_reg_next " + memory_read_enable_port(ctx, mi, port_idx) + " "
         + memory_raw_read_port(ctx, mi, port_idx) + " (" + mi.read_reg_field.at(port_idx) + " s))";
}

std::string memory_next_expr(const RocqCtx& ctx, const Memory_info& mi) {
  if (mi.write_ports.empty()) {
    return "(" + mi.field + " s)";
  }
  return memory_write_fold(ctx, mi);
}

// ---------------------------------------------------------------------------
// Driver expressions
// ---------------------------------------------------------------------------

// Rocq record projections are ordinary functions, so an input field reads as
// `(in_a i)` where Lean would write `i.in_a`.  Field names are uniquified across
// all three records (in_/out_/st_) by make_field_name, so the projections are
// globally unique inside the generated file.
std::string driver_expr(const RocqCtx& ctx, const Node_pin& dpin) {
  auto driver_node = pin_node(dpin);
  if (pin_is_input(dpin)) {
    auto pname = input_name_for_pin(ctx, dpin);
    auto it    = ctx.input_field.find(pname);
    if (it == ctx.input_field.end()) {
      throw Emit_error("internal: graph input pin has no Rocq input field");
    }
    return "(" + it->second + " i)";
  }
  if (node_is_flop(driver_node)) {
    auto fit = ctx.flop_field.find(node_id(driver_node));
    if (fit == ctx.flop_field.end()) {
      throw Emit_error("internal: flop n_" + std::to_string(node_id(driver_node)) + " has no Rocq state field");
    }
    return "(" + fit->second + " s)";
  }
  if (pin_is_const(dpin)) {
    auto v = pin_const_value(dpin);
    auto w = raw_pin_width(dpin);
    if (w == 0) {
      w = std::max<uint32_t>(1, static_cast<uint32_t>(v.get_bits()));
    }
    return lit_const_at(ctx, driver_node, v, w);
  }
  if (node_is_memory(driver_node)) {
    // One read-data output pin per read port; the driver pin id selects which.
    return "n_" + std::to_string(node_id(driver_node)) + "_p" + std::to_string(dpin.get_port_id());
  }
  return "n_" + std::to_string(node_id(driver_node));
}

std::string driver_expr_at(const RocqCtx& ctx, const Node_pin& dpin, uint32_t expected_w) {
  auto driver_node = pin_node(dpin);
  if (pin_is_const(dpin)) {
    return lit_const_at(ctx, driver_node, pin_const_value(dpin), expected_w);
  }
  return driver_expr(ctx, dpin);
}

// Left-nested application fold: nary_call("bv_add", {a,b,c}) => (bv_add (bv_add a b) c)
std::string nary_call(const std::string& fn, const std::vector<std::string>& operands) {
  if (operands.empty()) {
    throw Emit_error("internal: empty operand list for " + fn);
  }
  std::string out = operands[0];
  for (size_t i = 1; i < operands.size(); ++i) {
    out = "(" + fn + " " + out + " " + operands[i] + ")";
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
  auto     uv   = static_cast<uint64_t>(iv);
  uint32_t bits = 0;
  while (uv != 0) {
    ++bits;
    uv >>= 1;
  }
  return std::max<uint32_t>(1, bits);
}

// ---------------------------------------------------------------------------
// THE shared width resolver.
//
// The single largest bug class in pass.isabelle and pass.lean is the fast-model
// emitter and the certificate emitter disagreeing on the width at which an
// operand is materialized (13 documented postmortems between BRIDGE_BUGS.md and
// STEP5_BRIDGE_BUGS.md).  Both of those passes compute the widths twice, once
// per emitter.  Here every width rule that is not simply "the node width" lives
// in one place and BOTH emitters call it.
// ---------------------------------------------------------------------------

// A constant shift/amount operand must be widened to hold its VALUE rather than
// truncated to a possibly-1-bit pin width: a const 32 on a 1-bit pin would
// otherwise become 0.
uint32_t shift_dep_width(const RocqCtx& ctx, const Node_pin& dpin, const Node& owner) {
  uint32_t w = pin_width(ctx, dpin, owner);
  if (pin_is_const(dpin)) {
    w = std::max<uint32_t>(w, minimal_unsigned_const_width(pin_const_value(dpin)));
  }
  return w;
}

// Common compare width for LT/GT/EQ: the max of the operand pin widths.
uint32_t compare_dep_width(const RocqCtx& ctx, const Node& node, const std::vector<Node_pin>& drivers, uint32_t dflt) {
  uint32_t cw = dflt;
  for (const auto& d : drivers) {
    cw = std::max(cw, pin_width(ctx, d, node));
  }
  return cw;
}

// Get_mask materialization widths: (source width, mask width).  LiveHD's
// canonical zero-extend idiom is Get_mask(a, -1) == zext(a) (cprop.cpp), where
// the -1 mask is an ALL-ONES sentinel, not a 1-bit 0b1.  cgen reads it at
// max(src_w, mask_w, out_w) (cgen_sim.cpp); emitting it at the mask pin's
// declared 1-bit width would make sem_get_mask select only bit 0.
std::pair<uint32_t, uint32_t> get_mask_dep_widths(const RocqCtx& ctx, const Node& node, const std::vector<Node_pin>& drivers,
                                                 uint32_t out_w) {
  const uint32_t src_w  = drivers.empty() ? out_w : pin_width(ctx, drivers[0], node);
  const uint32_t mask_w = std::max(src_w, out_w);
  return {src_w, mask_w};
}

// Per-operand materialization width for one node, index by input order.  This is
// the function both emitters agree through.
uint32_t operand_dep_width(const RocqCtx& ctx, const Node& node, const std::vector<Node_pin>& drivers, size_t idx,
                           uint32_t out_w) {
  switch (node_op(node)) {
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::EQ: return compare_dep_width(ctx, node, drivers, node_op(node) == Ntype_op::EQ ? 1 : out_w);

    // Port 0 (the shifted value) is materialized at the node width by the fast
    // model; port 1 is the amount, widened to hold a constant's value.
    case Ntype_op::SHL: return idx == 1 ? shift_dep_width(ctx, drivers[idx], node) : out_w;

    // SRA keeps port 0 at its own pin width (the fast model zero-extends the
    // result afterwards), so the cert must too.
    case Ntype_op::SRA:
    case Ntype_op::Sext: return idx == 1 ? shift_dep_width(ctx, drivers[idx], node) : pin_width(ctx, drivers[idx], node);

    case Ntype_op::Get_mask: {
      const auto ws = get_mask_dep_widths(ctx, node, drivers, out_w);
      return idx == 1 ? ws.second : ws.first;
    }

    case Ntype_op::Set_mask: return pin_width(ctx, drivers[idx], node);

    default: return out_w;
  }
}

std::string shift_amount_expr_at(const RocqCtx& ctx, const Node_pin& dpin, uint32_t expected_w) {
  auto driver_node = pin_node(dpin);
  if (pin_is_const(dpin)) {
    const auto v = pin_const_value(dpin);
    const auto w = std::max<uint32_t>(expected_w, minimal_unsigned_const_width(v));
    return lit_const_at(ctx, driver_node, v, w);
  }
  return driver_expr_at(ctx, dpin, expected_w);
}

std::vector<Node_pin> drivers_of(const Node& node) {
  std::vector<Node_pin> drivers;
  for (const auto& e : inp_edges_ordered(node)) {
    drivers.push_back(e.driver);
  }
  return drivers;
}

// ---------------------------------------------------------------------------
// Fast-model expression emitter
// ---------------------------------------------------------------------------

std::string emit_node_expr(const RocqCtx& ctx, const Node& node) {
  const auto op = node_op(node);
  // A Memory node has no single width-bearing driver pin 0; its read-output
  // width is mi.bits.  Other ops use the node's declared width.
  const auto w = node_is_memory(node) ? memory_info_for(ctx, node).bits : node_width(ctx, node);

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
        return nary_call("bv_add", a_terms);
      }
      if (a_terms.empty()) {
        return "(bv_sub " + lit_zero(w) + " " + nary_call("bv_add", b_terms) + ")";
      }
      return "(bv_sub " + nary_call("bv_add", a_terms) + " " + nary_call("bv_add", b_terms) + ")";
    }

    case Ntype_op::Mult: {
      std::vector<std::string> terms;
      for (const auto& e : inp_edges_ordered(node)) {
        terms.push_back(ucast_pin_at(ctx, e.driver, w));
      }
      if (terms.empty()) {
        fatal(ctx, "Mult node n_" + std::to_string(node_id(node)) + " has no inputs.");
      }
      return nary_call("bv_mul", terms);
    }

    case Ntype_op::Div: {
      auto drivers = drivers_of(node);
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
      const char* fn = op == Ntype_op::And ? "bv_and" : (op == Ntype_op::Or ? "bv_or" : "bv_xor");
      return nary_call(fn, terms);
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
        disj = "(orb " + disj + " " + bools[i] + ")";
      }
      return "(bool_to_bv1 " + disj + ")";
    }

    case Ntype_op::Not: {
      auto drivers = drivers_of(node);
      if (drivers.empty()) {
        fatal(ctx, "Not node n_" + std::to_string(node_id(node)) + " has no input.");
      }
      return "(bv_not " + ucast_pin_at(ctx, drivers[0], w) + ")";
    }

    case Ntype_op::LT:
    case Ntype_op::GT: {
      auto drivers = drivers_of(node);
      if (drivers.size() != 2) {
        fatal(ctx, "LT/GT node n_" + std::to_string(node_id(node)) + " is not binary.");
      }
      const uint32_t cmp_w = operand_dep_width(ctx, node, drivers, 0, w);
      const auto     a     = ucast_pin_at(ctx, drivers[0], cmp_w);
      const auto     b     = ucast_pin_at(ctx, drivers[1], cmp_w);
      const bool     sgn   = node_output_is_signed(node);
      const char*    fn    = op == Ntype_op::LT ? (sgn ? "bv_sltb" : "bv_ultb") : (sgn ? "bv_sgtb" : "bv_ugtb");
      return "(bool_to_bv1 (" + std::string(fn) + " " + a + " " + b + "))";
    }

    case Ntype_op::EQ: {
      auto drivers = drivers_of(node);
      if (drivers.size() <= 1) {
        return lit_one(1);
      }
      const uint32_t eq_w = operand_dep_width(ctx, node, drivers, 0, 1);
      const auto     head = ucast_pin_at(ctx, drivers[0], eq_w);
      std::string    conj;
      for (size_t i = 1; i < drivers.size(); ++i) {
        const auto t = "(bv_eqb " + ucast_pin_at(ctx, drivers[i], eq_w) + " " + head + ")";
        conj         = (i == 1) ? t : "(andb " + conj + " " + t + ")";
      }
      return "(bool_to_bv1 " + conj + ")";
    }

    case Ntype_op::SHL: {
      auto drivers = drivers_of(node);
      if (drivers.size() != 2) {
        fatal(ctx, "SHL node n_" + std::to_string(node_id(node)) + " is not binary.");
      }
      const auto bw = operand_dep_width(ctx, node, drivers, 1, w);
      return "(bv_shl " + ucast_pin_at(ctx, drivers[0], w) + " " + shift_amount_expr_at(ctx, drivers[1], bw) + ")";
    }

    case Ntype_op::SRA: {
      auto drivers = drivers_of(node);
      if (drivers.size() == 1) {
        return ucast_pin_at(ctx, drivers[0], w);
      }
      if (drivers.size() != 2) {
        fatal(ctx, "SRA node n_" + std::to_string(node_id(node)) + " is not binary.");
      }
      const auto vw = operand_dep_width(ctx, node, drivers, 0, w);
      const auto sw = operand_dep_width(ctx, node, drivers, 1, w);
      return "(bv_zext " + std::to_string(w) + " (sem_sra " + ucast_pin_at(ctx, drivers[0], vw) + " "
             + shift_amount_expr_at(ctx, drivers[1], sw) + "))";
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
      // n-way: chained index test, totalized with a trailing zero arm so the
      // expression is defined for every selector value.
      std::string out = "(";
      size_t      idx = 0;
      for (auto& kv : options) {
        out += "if Z.eqb (bv_uint " + sel_e + ") " + std::to_string(idx) + " then " + ucast_pin_at(ctx, kv.second, w)
               + " else ";
        ++idx;
      }
      out += lit_zero(w) + ")";
      return out;
    }

    case Ntype_op::Sext: {
      auto drivers = drivers_of(node);
      if (drivers.empty()) {
        fatal(ctx, "Sext node n_" + std::to_string(node_id(node)) + " has no input.");
      }
      return "(bv_sext " + std::to_string(w) + " " + driver_expr(ctx, drivers[0]) + ")";
    }

    case Ntype_op::Get_mask: {
      auto drivers = drivers_of(node);
      if (drivers.size() != 2) {
        fatal(ctx, "Get_mask node n_" + std::to_string(node_id(node)) + " is not binary.");
      }
      const auto mask_w = operand_dep_width(ctx, node, drivers, 1, w);
      return "(sem_get_mask " + std::to_string(w) + " " + driver_expr(ctx, drivers[0]) + " "
             + driver_expr_at(ctx, drivers[1], mask_w) + ")";
    }

    case Ntype_op::Set_mask: {
      auto drivers = drivers_of(node);
      if (drivers.size() != 3) {
        fatal(ctx, "Set_mask node n_" + std::to_string(node_id(node)) + " is not ternary.");
      }
      return "(sem_set_mask " + ucast_pin_at(ctx, drivers[0], w) + " " + driver_expr(ctx, drivers[1]) + " "
             + driver_expr(ctx, drivers[2]) + ")";
    }

    case Ntype_op::Memory:
      throw Emit_error("internal: Memory node n_" + std::to_string(node_id(node))
                       + " must be emitted via per-port read lets");

    case Ntype_op::Latch:
    case Ntype_op::Fflop:
    case Ntype_op::Sub:
    case Ntype_op::LUT:
    case Ntype_op::AttrSet:
    case Ntype_op::Hotmux:
      fatal(ctx, "unsupported op `" + std::string(Ntype::get_name(op)) + "` at node n_" + std::to_string(node_id(node)) + ".");

    default: throw Emit_error("internal: unhandled Ntype_op in pass.rocq emit_node_expr");
  }
}

// ---------------------------------------------------------------------------
// Graph walk
// ---------------------------------------------------------------------------

std::vector<Node> reachable_topo_order(const std::vector<Node_pin>& roots, const absl::flat_hash_set<uint32_t>& flop_nids) {
  absl::flat_hash_set<uint32_t>      reached;
  std::vector<Node>                  order;
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

// ---------------------------------------------------------------------------
// normalize: repair pre-export IR width artifacts
//
// cprop can leave a Get_mask driver pin with a zero Bits attribute even though
// every consumer reads it at a definite width.  Zero-width is a hard error for
// the emitter, so infer the width from the consumers and write it back.  Ported
// from pass.isabelle (where it is implemented; in pass.lean the knob is parsed
// but never read).
// ---------------------------------------------------------------------------

uint32_t consumer_expected_width(const Edge& e) {
  auto sink = e.sink;
  auto sw   = raw_pin_width(sink);
  if (sw > 0) {
    return static_cast<uint32_t>(sw);
  }

  auto sn = pin_node(sink);
  if (node_is_flop(sn)) {
    auto pname = sink_pin_name(e);
    if (pname == "din") {
      return static_cast<uint32_t>(raw_node_width(sn));
    }
    return 1;
  }
  auto nw = raw_node_width(sn);
  if (nw > 0) {
    return static_cast<uint32_t>(nw);
  }
  return 0;
}

void normalize_zero_width_get_masks(const RocqCtx& ctx, const std::vector<Node>& topo) {
  for (auto node : topo) {
    if (!node_is_op(node, Ntype_op::Get_mask) || raw_node_width(node) != 0) {
      continue;
    }

    uint32_t    expected = 0;
    std::string consumers;
    for (const auto& e : node.out_edges()) {
      const auto ew = consumer_expected_width(e);
      consumers += " n_" + std::to_string(node_id(pin_node(e.sink))) + ":" + std::to_string(ew);
      if (ew == 0) {
        continue;
      }
      if (expected == 0) {
        expected = ew;
      } else if (expected != ew) {
        fatal(ctx, "Get_mask node n_" + std::to_string(node_id(node)) + " has conflicting consumer widths:" + consumers);
      }
    }

    if (expected == 0) {
      fatal(ctx,
            "Get_mask node n_" + std::to_string(node_id(node))
                + " has zero width and no inferable consumer width; consumers:" + consumers);
    }
    if (static_cast<size_t>(expected) > ctx.max_width) {
      fatal(ctx,
            "Get_mask node n_" + std::to_string(node_id(node)) + " inferred width " + std::to_string(expected)
                + " > max_width=" + std::to_string(ctx.max_width));
    }

    auto dpin = node.create_driver_pin(0);
    livehd::graph_util::set_bits(dpin, expected);
  }
}

// ---------------------------------------------------------------------------
// Certificate
// ---------------------------------------------------------------------------

// Certificate ids are `N` (binary naturals), NOT `nat`.  Rocq's `nat` is
// genuinely unary, so a LiveHD node id like 2000000000 would elaborate through
// Nat.of_num_uint into a term with two billion successors the moment anything
// reduced it -- and the evaluator compares ids on every step.  Lean's Nat is a
// GMP bignum, which is why pass.lean can use it directly; this is the sharpest
// place the Rocq port must diverge from its sibling.  See LGraphModel.v.
//
// The %N delimiter is REQUIRED: generated files open Z_scope, and Rocq does not
// push a record field's element scope into a list literal, so a bare [1; 2]
// elaborates as `list Z` and the file does not compile.
std::string id_literal(uint32_t x) { return std::to_string(x) + "%N"; }

std::string id_list(const std::vector<uint32_t>& xs) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < xs.size(); ++i) {
    if (i != 0) {
      oss << "; ";
    }
    oss << xs[i];
  }
  oss << "]%N";
  return oss.str();
}

struct CertBuild {
  std::set<uint32_t>              source_ids;
  std::map<uint32_t, std::string> source_exprs;
  std::map<uint32_t, uint32_t>    source_width;
  uint32_t                        next_synth_id = 1000000000;
};

struct CertNodeInfo {
  uint32_t              nid   = 0;
  std::string           op_expr;
  uint32_t              width = 0;
  std::vector<uint32_t> deps;
};

uint32_t cert_dep_id(const RocqCtx& ctx, CertBuild& build, const Node_pin& pin, uint32_t expected_w) {
  auto n = pin_node(pin);
  if (pin_is_const(pin)) {
    const uint32_t sid = build.next_synth_id++;
    build.source_ids.insert(sid);
    build.source_exprs[sid] = "mk_bv " + std::to_string(expected_w) + " (" + int_of_const(ctx, n, pin_const_value(pin)) + ")";
    build.source_width[sid] = expected_w;
    return sid;
  }
  if (pin_is_input(pin)) {
    auto pname  = input_name_for_pin(ctx, pin);
    auto sid_it = ctx.input_source_id.find(pname);
    if (sid_it == ctx.input_source_id.end()) {
      fatal(ctx, "internal: input source id missing for certificate dependency");
    }
    const auto sid = sid_it->second;
    build.source_ids.insert(sid);
    build.source_exprs[sid]
        = "mk_bv " + std::to_string(ctx.input_width.at(pname)) + " (bv_uint (" + ctx.input_field.at(pname) + " i))";
    build.source_width[sid] = ctx.input_width.at(pname);
    return sid;
  }
  if (node_is_flop(n)) {
    const auto sid = node_id(n);
    build.source_ids.insert(sid);
    build.source_exprs[sid]
        = "mk_bv " + std::to_string(ctx.flop_width.at(sid)) + " (bv_uint (" + ctx.flop_field.at(sid) + " s))";
    build.source_width[sid] = ctx.flop_width.at(sid);
    return sid;
  }
  return node_id(n);
}

// Ops for which LGraphModel.simpleOpCertWfBool has a real shape rule.  A chunk
// made only of these can carry the strong per-node shape lemma; anything else
// falls back per cert_wf_fallback.  Mirrors pass.isabelle's is_simple_cert_op_tag.
bool is_simple_cert_op(std::string_view op_expr) {
  static const std::unordered_set<std::string> kSimple = {
      "Op_Const", "Op_Sum", "Op_And", "Op_Or",   "Op_Xor",     "Op_Ror",  "Op_Not",
      "Op_EQ",    "Op_ULT", "Op_UGT", "Op_SLT",  "Op_SGT",     "Op_SHL",  "Op_SRA",
      "Op_Sext",  "Op_GetMask", "Op_MuxBool", "Op_MuxN",
  };
  // op_expr is either a bare tag or "Op_Const (...)" / "Op_Sum 2".
  const auto sp  = op_expr.find(' ');
  const auto tag = std::string(sp == std::string_view::npos ? op_expr : op_expr.substr(0, sp));
  return kSimple.count(tag) > 0;
}

std::string cert_node_expr(const RocqCtx& ctx, CertBuild& build, const Node& node, CertNodeInfo* info = nullptr) {
  const auto            op = node_op(node);
  const auto            w  = node_width(ctx, node);
  std::string           op_expr;
  std::vector<uint32_t> deps;
  auto                  drivers = drivers_of(node);

  switch (op) {
    case Ntype_op::Nconst: op_expr = "Op_Const (" + int_of_const(ctx, node, node_const_value(node)) + ")"; break;

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
      op_expr = "(Op_Sum " + std::to_string(adds.size()) + ")";
      break;
    }

    case Ntype_op::Mult:
      op_expr = "Op_Mult";
      for (size_t i = 0; i < drivers.size(); ++i) {
        deps.push_back(cert_dep_id(ctx, build, drivers[i], operand_dep_width(ctx, node, drivers, i, w)));
      }
      break;

    case Ntype_op::Div:
      op_expr = "Op_UDiv";
      for (size_t i = 0; i < drivers.size(); ++i) {
        deps.push_back(cert_dep_id(ctx, build, drivers[i], operand_dep_width(ctx, node, drivers, i, w)));
      }
      break;

    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor:
    case Ntype_op::Ror:
    case Ntype_op::EQ: {
      if (op == Ntype_op::And) {
        op_expr = "Op_And";
      } else if (op == Ntype_op::Or) {
        op_expr = "Op_Or";
      } else if (op == Ntype_op::Xor) {
        op_expr = "Op_Xor";
      } else if (op == Ntype_op::Ror) {
        op_expr = "Op_Ror";
      } else {
        op_expr = "Op_EQ";
      }
      for (size_t i = 0; i < drivers.size(); ++i) {
        deps.push_back(cert_dep_id(ctx, build, drivers[i], operand_dep_width(ctx, node, drivers, i, w)));
      }
      break;
    }

    case Ntype_op::Not:
      op_expr = "Op_Not";
      for (size_t i = 0; i < drivers.size(); ++i) {
        deps.push_back(cert_dep_id(ctx, build, drivers[i], operand_dep_width(ctx, node, drivers, i, w)));
      }
      break;

    case Ntype_op::LT:
    case Ntype_op::GT: {
      const bool is_signed = node_output_is_signed(node);
      if (op == Ntype_op::LT) {
        op_expr = is_signed ? "Op_SLT" : "Op_ULT";
      } else {
        op_expr = is_signed ? "Op_SGT" : "Op_UGT";
      }
      for (size_t i = 0; i < drivers.size(); ++i) {
        deps.push_back(cert_dep_id(ctx, build, drivers[i], operand_dep_width(ctx, node, drivers, i, w)));
      }
      break;
    }

    case Ntype_op::SHL:
    case Ntype_op::SRA:
    case Ntype_op::Sext:
    case Ntype_op::Set_mask:
    case Ntype_op::Get_mask: {
      if (op == Ntype_op::SHL) {
        op_expr = "Op_SHL";
      } else if (op == Ntype_op::SRA) {
        op_expr = "Op_SRA";
      } else if (op == Ntype_op::Sext) {
        op_expr = "Op_Sext";
      } else if (op == Ntype_op::Set_mask) {
        op_expr = "Op_SetMask";
      } else {
        op_expr = "Op_GetMask";
      }
      for (size_t i = 0; i < drivers.size(); ++i) {
        deps.push_back(cert_dep_id(ctx, build, drivers[i], operand_dep_width(ctx, node, drivers, i, w)));
      }
      break;
    }

    case Ntype_op::Mux: {
      uint32_t sel_w      = 1;
      size_t   data_count = 0;
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
      op_expr = (data_count == 2 && sel_w == 1) ? "Op_MuxBool" : "Op_MuxN";
      break;
    }

    default:
      fatal(ctx,
            "unsupported certificate op `" + std::string(Ntype::get_name(op)) + "` at node n_" + std::to_string(node_id(node))
                + ".");
  }

  if (info != nullptr) {
    info->nid     = node_id(node);
    info->op_expr = op_expr;
    info->width   = w;
    info->deps    = deps;
  }

  std::ostringstream oss;
  oss << "{| nc_nid := " << id_literal(node_id(node)) << "; nc_op := " << op_expr << "; nc_width := " << w
      << "; nc_deps := " << id_list(deps) << " |}";
  return oss.str();
}

}  // namespace

// ---------------------------------------------------------------------------
// Pass plumbing
// ---------------------------------------------------------------------------

Pass_rocq::Pass_rocq(const Eprp_var& var) : Pass("pass.rocq", var) {
  auto s = var.get("strict");
  strict = (s == "false") ? false : true;

  auto n   = var.get("normalize");
  normalize = (n == "false") ? false : true;

  auto ec   = var.get("emit_cert");
  emit_cert = (ec == "false") ? false : true;

  top              = std::string(var.get("top"));
  cert_wf          = parse_cert_wf_mode(var.get("cert_wf"));
  cert_wf_fallback = parse_cert_wf_fallback(var.get("cert_wf_fallback"));
  eval_engine      = parse_eval_engine(var.get("eval_engine"));

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

  max_width = parse_max_width(var.get("max_width"));
}

void Pass_rocq::setup() {
  Eprp_method m1("pass.rocq", "Emit per-design Rocq files for graph-certificate translation proofs.", &Pass_rocq::work);
  m1.add_label_optional("path", "Output directory for emitted Rocq files.");
  m1.add_label_optional("top", "Top module name override.");
  m1.add_label_optional("strict", "true|false. Abort on unsupported ops (formal.strict applies too; formal.rocq.strict wins)",
                        "true");
  m1.add_label_optional("normalize", "true|false. Normalize pre-export width artifacts (formal.normalize applies too)", "true");
  m1.add_label_optional("emit_cert", "true|false. Emit graph certificate and cert-model definitions.", "true");
  m1.add_label_optional("max_width", "Hard cap on node Bits width; 0 or 'unlimited' = no cap (default 1024).", "1024");
  m1.add_label_optional("cert_wf", "skip|eval|sorry|chunked. Certificate well-formedness proof mode.", "skip");
  m1.add_label_optional("cert_wf_fallback", "fail|sorry|eval for cert_wf:chunked chunks with a non-simple op shape.", "fail");
  m1.add_label_optional("cert_chunk_size", "Number of node certificates per chunk for cert_wf:chunked.", "25");
  m1.add_label_optional("cert_chunk_limit", "Emit only first N certificate chunks for proof-shape testing (0 = all).", "0");
  m1.add_label_optional("eval_engine",
                        "vm|native|cbv. Reduction engine for computational certificate proofs. vm=vm_compute (default, "
                        "kernel+VM), native=native_compute (fastest, adds the OCaml compiler to the TCB), cbv=kernel only.",
                        "vm");
  register_pass(m1);
}

void Pass_rocq::work(Eprp_var& var) {
  Pass_rocq pass(var);
  for (const auto& g : var.graphs) {
    pass.emit_for_graph(g);
  }
}

void Pass_rocq::emit_for_graph(const std::shared_ptr<hhds::Graph>& graph) const {
  TRACE_EVENT("pass", "ROCQ_emit_for_graph");

  if (!graph) {
    livehd::diag::warn("pass.rocq", "no-input", "io").msg("received a null Graph instance").emit();
    return;
  }
  auto* g = graph.get();

  const std::string output_dir = (path == "/INVALID" || path.empty()) ? std::string(".") : path;

  const std::string raw_name   = top.empty() ? std::string(g->get_name()) : top;
  const std::string base_name  = sanitize_rocq(raw_name);
  const std::string model_path = output_dir + "/" + base_name + "_Lgraph.v";
  const std::string cert_path  = output_dir + "/" + base_name + "_Lgraph_Cert.v";
  const std::string proj_path  = output_dir + "/_CoqProject";

  RocqCtx ctx;
  ctx.g         = g;
  ctx.base_name = raw_name;
  ctx.top_name  = base_name;
  ctx.strict    = strict;
  ctx.max_width = max_width;

  auto     gio                   = g->get_io();
  uint32_t next_input_source_id = 2000000000;

  const char* const eval_tac = eval_engine_tactic(eval_engine);

  try {
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
  } catch (const Emit_error& err) {
    std::cerr << err.what() << "\n";
    if (strict) {
      livehd::diag::err("pass.rocq", "rocq-error", "internal").msg("{}", err.what()).fatal();
    }
    return;
  }

  std::vector<Node>             flop_nodes;
  absl::flat_hash_set<uint32_t> flop_nids;
  std::vector<Node>             memory_nodes;
  std::vector<Node>             topo;
  std::map<std::string, Node_pin> out_drivers;
  std::map<uint32_t, Node_pin>    flop_din, flop_reset, flop_enable;

  try {
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
      } else if (node_is_memory(node)) {
        memory_nodes.emplace_back(node);
      }
    }

    for (auto& mn : memory_nodes) {
      auto        mi = parse_memory_info(ctx, mn);
      std::string mem_raw;
      for (const auto& e : mn.out_edges()) {
        auto wn = livehd::graph_util::wire_name(e.driver);
        if (!wn.empty() && wn[0] != '_') {
          mem_raw = std::string(wn);
          break;
        }
      }
      if (mem_raw.empty()) {
        mem_raw = "mem_" + std::to_string(node_id(mn));
      }
      mi.raw_name = mem_raw;
      mi.field    = make_field_name("st_", mem_raw, ctx.used_fields);
      if (mi.sync) {
        for (auto pidx : mi.read_ports) {
          mi.read_reg_field[pidx] = make_field_name("st_", mem_raw + "_rdata_" + std::to_string(pidx), ctx.used_fields);
        }
      }
      ctx.memory_info[node_id(mn)] = mi;
    }

    std::vector<Node_pin> roots;
    for (const auto& decl : gio->get_output_pin_decls()) {
      auto out_sink = g->get_output_pin(decl.name);
      auto edges    = out_sink.inp_edges();
      if (!edges.empty()) {
        out_drivers[std::string(decl.name)] = edges.front().driver;
        roots.push_back(edges.front().driver);
      }
    }

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

    for (auto& mn : memory_nodes) {
      const auto& mi = ctx.memory_info.at(node_id(mn));
      for (const auto& p : mi.ports) {
        if (!p.addr.is_invalid()) {
          roots.push_back(p.addr);
        }
        if (!p.din.is_invalid()) {
          roots.push_back(p.din);
        }
        if (!p.enable.is_invalid()) {
          roots.push_back(p.enable);
        }
      }
    }

    topo = reachable_topo_order(roots, flop_nids);

    if (normalize) {
      normalize_zero_width_get_masks(ctx, topo);
    }
  } catch (const Emit_error& err) {
    std::cerr << err.what() << "\n";
    if (strict) {
      livehd::diag::err("pass.rocq", "rocq-error", "internal").msg("{}", err.what()).fatal();
    }
    return;
  }

  const bool sequential = !flop_nodes.empty() || !memory_nodes.empty();

  // -------------------------------------------------------------------------
  // File 1: the fast executable model.
  // -------------------------------------------------------------------------
  std::ofstream ofs(model_path);
  if (!ofs) {
    livehd::diag::warn("pass.rocq", "write-failed", "io").msg("could not write {}", model_path).emit();
    return;
  }

  auto abort_model = [&](const Emit_error& err) {
    ofs.close();
    std::cerr << err.what() << "\n";
    if (strict) {
      std::remove(model_path.c_str());
      livehd::diag::err("pass.rocq", "rocq-error", "internal").msg("{}", err.what()).fatal();
    }
  };

  ofs << "(* Generated by LiveHD pass.rocq. Do not edit by hand.\n";
  ofs << "   Fast executable model for design `" << raw_name << "`.\n";
  ofs << "   The graph certificate and the model=certificate bridge live in\n";
  ofs << "   " << base_name << "_Lgraph_Cert.v (a separate compilation unit, so a\n";
  ofs << "   certificate iteration does not re-elaborate this file). *)\n\n";
  ofs << "From Stdlib Require Import ZArith List Bool.\n";
  ofs << "From RocqSemanticPrimitives Require Import SemanticPrimitives.\n\n";
  ofs << "Import ListNotations.\n";
  ofs << "Local Open Scope Z_scope.\n\n";
  ofs << "Set Warnings \"-notation-overridden\".\n\n";

  try {
    ofs << "Record " << base_name << "_in := mk_" << base_name << "_in {\n";
    if (ctx.input_field.empty()) {
      ofs << "  in_dummy : BitVec 1\n";
    } else {
      bool first = true;
      for (const auto& kv : ctx.input_field) {
        ofs << (first ? "  " : ";  ") << kv.second << " : BitVec " << ctx.input_width.at(kv.first) << "\n";
        first = false;
      }
    }
    ofs << "}.\n\n";

    ofs << "Record " << base_name << "_out := mk_" << base_name << "_out {\n";
    if (ctx.output_field.empty()) {
      ofs << "  out_dummy : BitVec 1\n";
    } else {
      bool first = true;
      for (const auto& kv : ctx.output_field) {
        ofs << (first ? "  " : ";  ") << kv.second << " : BitVec " << ctx.output_width.at(kv.first) << "\n";
        first = false;
      }
    }
    ofs << "}.\n\n";

    if (sequential) {
      ofs << "Record " << base_name << "_state := mk_" << base_name << "_state {\n";
      bool first = true;
      for (const auto& kv : ctx.flop_field) {
        ofs << (first ? "  " : ";  ") << kv.second << " : BitVec " << ctx.flop_width.at(kv.first) << "\n";
        first = false;
      }
      // Function-valued memory state, plus one registered read-data field per
      // read port for sync-read (type 1) memories.
      for (const auto& mn : memory_nodes) {
        const auto& mi = ctx.memory_info.at(node_id(mn));
        ofs << (first ? "  " : ";  ") << mi.field << " : BitVec " << mi.addr_width << " -> BitVec " << mi.bits << "\n";
        first = false;
        for (const auto& kv : mi.read_reg_field) {
          ofs << ";  " << kv.second << " : BitVec " << mi.bits << "\n";
        }
      }
      ofs << "}.\n\n";
    }

    const std::string comb_params
        = sequential ? ("(i : " + base_name + "_in) (s : " + base_name + "_state)") : ("(i : " + base_name + "_in)");

    // Emit the `let n_<id> : BitVec w := ... in` binding(s) for one node.  A
    // Memory node binds one value per read port (n_<id>_p<dout_pid>).
    auto emit_node_lets = [&](std::ostream& os, const Node& n) {
      if (node_is_memory(n)) {
        const auto& mi = memory_info_for(ctx, n);
        for (auto pidx : mi.read_ports) {
          os << "  let n_" << node_id(n) << "_p" << mi.ports.at(pidx).driver_pid << " : BitVec " << mi.bits
             << " := " << memory_read_port_expr(ctx, mi, pidx) << " in\n";
        }
        return;
      }
      os << "  let n_" << node_id(n) << " : BitVec " << node_width(ctx, n) << " := " << emit_node_expr(ctx, n) << " in\n";
    };

    ofs << "Definition " << base_name << "_comb " << comb_params << " : " << base_name << "_out :=\n";
    for (const auto& n : topo) {
      emit_node_lets(ofs, n);
    }
    ofs << "  {| ";
    bool first_out = true;
    if (ctx.output_field.empty()) {
      ofs << "out_dummy := " << lit_zero(1);
    } else {
      for (const auto& kv : ctx.output_field) {
        if (!first_out) {
          ofs << "; ";
        }
        first_out            = false;
        const auto& out_name = kv.first;
        auto        drv      = out_drivers.find(out_name);
        ofs << kv.second << " := ";
        if (drv == out_drivers.end()) {
          ofs << lit_zero(ctx.output_width.at(out_name));
        } else {
          ofs << ucast_pin_at(ctx, drv->second, ctx.output_width.at(out_name));
        }
      }
    }
    ofs << " |}.\n\n";

    if (sequential) {
      ofs << "Definition " << base_name << "_next " << comb_params << " : " << base_name << "_state :=\n";
      for (const auto& n : topo) {
        emit_node_lets(ofs, n);
      }
      for (auto& fn : flop_nodes) {
        const auto  fid   = node_id(fn);
        const auto  fld   = ctx.flop_field.at(fid);
        const auto  fw    = ctx.flop_width.at(fid);
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
        ofs << "  let new_" << fld << " : BitVec " << fw << " := flop_next " << reset_e << " " << lit_zero(fw) << " " << en_e
            << " " << din_e << " (" << fld << " s) in\n";
      }
      for (auto& mn : memory_nodes) {
        const auto& mi = ctx.memory_info.at(node_id(mn));
        ofs << "  let new_" << mi.field << " : BitVec " << mi.addr_width << " -> BitVec " << mi.bits
            << " := " << memory_next_expr(ctx, mi) << " in\n";
        for (const auto& kv : mi.read_reg_field) {
          ofs << "  let new_" << kv.second << " : BitVec " << mi.bits << " := " << memory_sync_reg_next_port(ctx, mi, kv.first)
              << " in\n";
        }
      }
      ofs << "  {| ";
      bool first_field = true;
      for (auto& fn : flop_nodes) {
        const auto fld = ctx.flop_field.at(node_id(fn));
        if (!first_field) {
          ofs << "; ";
        }
        first_field = false;
        ofs << fld << " := new_" << fld;
      }
      for (auto& mn : memory_nodes) {
        const auto& mi = ctx.memory_info.at(node_id(mn));
        if (!first_field) {
          ofs << "; ";
        }
        first_field = false;
        ofs << mi.field << " := new_" << mi.field;
        for (const auto& kv : mi.read_reg_field) {
          ofs << "; " << kv.second << " := new_" << kv.second;
        }
      }
      ofs << " |}.\n\n";

      ofs << "Definition " << base_name << "_step " << comb_params << " : prod " << base_name << "_state " << base_name
          << "_out :=\n";
      ofs << "  (" << base_name << "_next i s, " << base_name << "_comb i s).\n";
    }
  } catch (const Emit_error& err) {
    abort_model(err);
    return;
  }

  ofs.close();

  // -------------------------------------------------------------------------
  // File 2: the graph certificate.
  // -------------------------------------------------------------------------
  bool cert_written = false;

  if (emit_cert) {
    std::ofstream cfs(cert_path);
    if (!cfs) {
      livehd::diag::warn("pass.rocq", "write-failed", "io").msg("could not write {}", cert_path).emit();
    } else {
      auto abort_cert = [&](const Emit_error& err) {
        cfs.close();
        std::cerr << err.what() << "\n";
        if (strict) {
          std::remove(cert_path.c_str());
          livehd::diag::err("pass.rocq", "rocq-error", "internal").msg("{}", err.what()).fatal();
        }
      };

      cfs << "(* Generated by LiveHD pass.rocq. Do not edit by hand.\n";
      cfs << "   Graph certificate for design `" << raw_name << "`. *)\n\n";
      cfs << "From Stdlib Require Import ZArith NArith List Bool.\n";
      cfs << "From RocqSemanticPrimitives Require Import SemanticPrimitives.\n";
      cfs << "From RocqSemanticPrimitives.Translation Require Import LGraphModel.\n";
      // The emitted _CoqProject binds this directory with `-Q . LiveHD`, and -Q
      // (unlike -R) does not expose short names, so the model must be required
      // through its full logical path.
      cfs << "From LiveHD Require Import " << base_name << "_Lgraph.\n\n";
      cfs << "Import ListNotations.\n";
      cfs << "Local Open Scope Z_scope.\n\n";

      try {
        // Memory certificate stub (parity with pass.isabelle / pass.lean): the BV
        // bignum evaluator is bit-vector-only and does not interpret
        // function-valued memory, so memory-bearing designs get counts + a note.
        if (!memory_nodes.empty()) {
          cfs << "(* Certificate: STUB. This design contains " << memory_nodes.size()
              << " LGraph Memory node(s), modeled as function-valued\n";
          cfs << "   state (BitVec addr -> BitVec data) via SemanticPrimitives mem_read/mem_write/mem_write_be.\n";
          cfs << "   The graph-certificate evaluator (BV bignum) is bit-vector-only and does not yet\n";
          cfs << "   interpret function-valued memory, so no GraphCert / evalGraph bridge is emitted here\n";
          cfs << "   (parity with the pass.isabelle and pass.lean memory certificate stubs). *)\n\n";
          cfs << "Definition " << base_name << "_node_count : nat := " << topo.size() << ".\n";
          cfs << "Definition " << base_name << "_flop_count : nat := " << flop_nodes.size() << ".\n";
          cfs << "Definition " << base_name << "_memory_count : nat := " << memory_nodes.size() << ".\n\n";
          cfs << "Theorem " << base_name << "_certificate_counts :\n";
          cfs << "  " << base_name << "_node_count = " << topo.size() << "%nat /\\ " << base_name << "_flop_count = "
              << flop_nodes.size() << "%nat /\\ " << base_name << "_memory_count = " << memory_nodes.size() << "%nat.\n";
          cfs << "Proof. repeat split. Qed.\n";
          cfs.close();
          cert_written = true;
          std::cout << "pass.rocq: " << raw_name << " -> " << model_path << ", " << cert_path
                    << " (memory certificate stub: " << topo.size() << " nodes, " << flop_nodes.size() << " flops, "
                    << memory_nodes.size() << " memories)\n";
        } else {
          CertBuild                 cert_build;
          std::vector<std::string>  cert_nodes;
          std::vector<uint32_t>     topo_ids;
          std::vector<CertNodeInfo> cert_infos;
          for (const auto& n : topo) {
            topo_ids.push_back(node_id(n));
            CertNodeInfo info;
            cert_nodes.push_back(cert_node_expr(ctx, cert_build, n, &info));
            cert_infos.push_back(info);
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

          const std::string senv_params
              = sequential ? ("(i : " + base_name + "_in) (s : " + base_name + "_state)") : ("(i : " + base_name + "_in)");
          const std::string senv_args = sequential ? "i s" : "i";

          cfs << "Definition " << base_name << "_nodeCerts : list NodeCert := [\n";
          for (size_t i = 0; i < cert_nodes.size(); ++i) {
            cfs << "  " << cert_nodes[i];
            if (i + 1 != cert_nodes.size()) {
              cfs << ";";
            }
            cfs << "\n";
          }
          cfs << "].\n\n";

          cfs << "Definition " << base_name << "_sourceEnv " << senv_params << " : N -> BV := fun n =>\n";
          for (const auto& sid : source_ids) {
            cfs << "  if N.eqb n " << id_literal(sid) << " then " << cert_build.source_exprs.at(sid) << " else\n";
          }
          cfs << "  mk_bv 0 0.\n\n";

          cfs << "Definition " << base_name << "_graphCert : GraphCert :=\n";
          cfs << "  {| gc_topo := " << id_list(topo_ids) << "; gc_sources := " << id_list(source_ids)
              << "; gc_nodes := nodes_of_list " << base_name << "_nodeCerts |}.\n\n";

          cfs << "Definition " << base_name << "_outputsFromCert (rho : N -> BV) : " << base_name << "_out :=\n";
          cfs << "  {| ";
          bool first_cert_out = true;
          if (ctx.output_field.empty()) {
            cfs << "out_dummy := " << lit_zero(1);
          } else {
            for (const auto& kv : ctx.output_field) {
              const auto& out_name = kv.first;
              if (!first_cert_out) {
                cfs << "; ";
              }
              first_cert_out = false;
              cfs << kv.second << " := ";
              auto oid = output_cert_ids.find(out_name);
              if (oid == output_cert_ids.end()) {
                cfs << lit_zero(ctx.output_width.at(out_name));
              } else {
                cfs << "bv_to_bitvec " << ctx.output_width.at(out_name) << " (rho " << id_literal(oid->second) << ")";
              }
            }
          }
          cfs << " |}.\n\n";

          const std::string eval_expr = "evalGraph (gc_topo " + base_name + "_graphCert) " + base_name + "_graphCert ("
                                        + base_name + "_sourceEnv " + senv_args + ")";

          if (sequential) {
            cfs << "Definition " << base_name << "_nextStateFromCert (rho : N -> BV) (s : " << base_name << "_state) : "
                << base_name << "_state :=\n";
            for (auto& fn : flop_nodes) {
              const auto  fid   = node_id(fn);
              const auto  fld   = ctx.flop_field.at(fid);
              const auto  fw    = ctx.flop_width.at(fid);
              const auto  dit   = flop_din_cert_ids.find(fid);
              const auto  rit   = flop_reset_cert_ids.find(fid);
              const auto  eit   = flop_enable_cert_ids.find(fid);
              std::string din_e = lit_zero(fw);
              if (dit != flop_din_cert_ids.end()) {
                din_e = "(bv_to_bitvec " + std::to_string(fw) + " (rho " + id_literal(dit->second) + "))";
              }
              std::string reset_e = "false";
              if (rit != flop_reset_cert_ids.end()) {
                reset_e = "(bvc_nonzero (rho " + id_literal(rit->second) + "))";
              }
              std::string en_e = "true";
              if (eit != flop_enable_cert_ids.end()) {
                en_e = "(bvc_nonzero (rho " + id_literal(eit->second) + "))";
              }
              cfs << "  let new_" << fld << " : BitVec " << fw << " := flop_next " << reset_e << " " << lit_zero(fw) << " "
                  << en_e << " " << din_e << " (" << fld << " s) in\n";
            }
            cfs << "  {| ";
            bool first_field = true;
            for (auto& fn : flop_nodes) {
              const auto fld = ctx.flop_field.at(node_id(fn));
              if (!first_field) {
                cfs << "; ";
              }
              first_field = false;
              cfs << fld << " := new_" << fld;
            }
            cfs << " |}.\n\n";

            cfs << "Definition " << base_name << "_comb_cert " << senv_params << " : " << base_name << "_out :=\n";
            cfs << "  " << base_name << "_outputsFromCert (" << eval_expr << ").\n\n";

            cfs << "Definition " << base_name << "_next_cert " << senv_params << " : " << base_name << "_state :=\n";
            cfs << "  " << base_name << "_nextStateFromCert (" << eval_expr << ") s.\n\n";

            cfs << "Definition " << base_name << "_step_cert " << senv_params << " : prod " << base_name << "_state "
                << base_name << "_out :=\n";
            cfs << "  (" << base_name << "_next_cert i s, " << base_name << "_comb_cert i s).\n\n";

            cfs << "Theorem " << base_name << "_comb_cert_refines_cert " << senv_params << " :\n";
            cfs << "  " << base_name << "_comb_cert " << senv_args << " = " << base_name << "_outputsFromCert (" << eval_expr
                << ").\n";
            cfs << "Proof. reflexivity. Qed.\n\n";

            cfs << "Theorem " << base_name << "_next_cert_refines_cert " << senv_params << " :\n";
            cfs << "  " << base_name << "_next_cert " << senv_args << " = " << base_name << "_nextStateFromCert ("
                << eval_expr << ") s.\n";
            cfs << "Proof. reflexivity. Qed.\n\n";

            cfs << "Theorem " << base_name << "_step_cert_refines_lgraph_certificate " << senv_params << " :\n";
            cfs << "  " << base_name << "_step_cert " << senv_args << " = (" << base_name << "_nextStateFromCert ("
                << eval_expr << ") s, " << base_name << "_outputsFromCert (" << eval_expr << ")).\n";
            cfs << "Proof. reflexivity. Qed.\n\n";
          } else {
            cfs << "Definition " << base_name << "_comb_cert " << senv_params << " : " << base_name << "_out :=\n";
            cfs << "  " << base_name << "_outputsFromCert (" << eval_expr << ").\n\n";

            cfs << "Theorem " << base_name << "_comb_cert_refines_cert " << senv_params << " :\n";
            cfs << "  " << base_name << "_comb_cert " << senv_args << " = " << base_name << "_outputsFromCert (" << eval_expr
                << ").\n";
            cfs << "Proof. reflexivity. Qed.\n\n";
          }

          // The keystone, instantiated.  Nothing design-specific is proved here:
          // evalGraphCorrectForCert is closed once in LGraphModel.v.
          cfs << "Theorem " << base_name << "_evalGraph_correct " << senv_params << " :\n";
          cfs << "  envCorrectOn (gc_topo " << base_name << "_graphCert)\n";
          cfs << "    (" << eval_expr << ")\n";
          cfs << "    (graphDenotation (gc_topo " << base_name << "_graphCert) " << base_name << "_graphCert (" << base_name
              << "_sourceEnv " << senv_args << ")).\n";
          cfs << "Proof. apply evalGraphCorrectForCert. Qed.\n\n";

          // --- certificate well-formedness -------------------------------------
          switch (cert_wf) {
            case RocqCertWFMode::Skip:
              cfs << "(* Certificate well-formedness proof skipped (cert_wf=skip).\n";
              cfs << "   Re-run with --set formal.rocq.cert_wf=eval for a single computational\n";
              cfs << "   proof, or =chunked for per-chunk lemmas (cheaper Qed on large designs). *)\n";
              break;

            case RocqCertWFMode::Sorry:
              cfs << "(* cert_wf=sorry: the well-formedness obligation is ADMITTED, not proved. *)\n";
              cfs << "Theorem " << base_name << "_graphCert_wf : graphCertWf " << base_name << "_graphCert.\n";
              cfs << "Admitted.\n";
              break;

            case RocqCertWFMode::Eval:
              cfs << "(* cert_wf=eval: one computational proof over the whole certificate, via "
                  << eval_tac << ". *)\n";
              cfs << "Theorem " << base_name << "_graphCert_wf_bool :\n";
              cfs << "  graphCertWfBool " << base_name << "_nodeCerts (gc_sources " << base_name << "_graphCert) = true.\n";
              cfs << "Proof. " << eval_tac << ". reflexivity. Qed.\n";
              break;

            case RocqCertWFMode::Chunked: {
              // Split the node certificates into chunks and prove each chunk's
              // well-formedness separately.  Rocq type-checks the completed proof
              // term at every Qed, so many small obligations beat one huge one.
              cfs << "(* cert_wf=chunked: per-chunk well-formedness, " << cert_chunk_size << " node certificates per\n";
              cfs << "   chunk, each discharged by " << eval_tac << ".  Rocq re-checks the proof term at\n";
              cfs << "   every Qed, so many small obligations are cheaper than one whole-graph proof. *)\n\n";
              cfs << "Definition " << base_name << "_cert_all_ids : list N := " << id_list(topo_ids) << ".\n\n";

              const size_t total_chunks = (cert_nodes.size() + cert_chunk_size - 1) / cert_chunk_size;
              const size_t emit_chunks  = (cert_chunk_limit == 0) ? total_chunks : std::min(cert_chunk_limit, total_chunks);
              for (size_t c = 0; c < emit_chunks; ++c) {
                const size_t lo = c * cert_chunk_size;
                const size_t hi = std::min(lo + cert_chunk_size, cert_nodes.size());

                bool all_simple = true;
                for (size_t k = lo; k < hi; ++k) {
                  if (!is_simple_cert_op(cert_infos[k].op_expr)) {
                    all_simple = false;
                    break;
                  }
                }

                cfs << "Definition " << base_name << "_cert_chunk_" << c << " : list NodeCert := [\n";
                for (size_t k = lo; k < hi; ++k) {
                  cfs << "  " << cert_nodes[k] << (k + 1 != hi ? ";" : "") << "\n";
                }
                cfs << "].\n\n";

                cfs << "Theorem " << base_name << "_cert_chunk_" << c << "_ok :\n";
                cfs << "  nodeCertChunkWfBool " << base_name << "_cert_all_ids (gc_sources " << base_name << "_graphCert) "
                    << base_name << "_cert_chunk_" << c << " = true.\n";
                cfs << "Proof. " << eval_tac << ". reflexivity. Qed.\n\n";

                if (all_simple) {
                  cfs << "Theorem " << base_name << "_cert_chunk_" << c << "_shape :\n";
                  cfs << "  forallb simpleNodeCertShapeWfBool " << base_name << "_cert_chunk_" << c << " = true.\n";
                  cfs << "Proof. " << eval_tac << ". reflexivity. Qed.\n\n";
                } else {
                  switch (cert_wf_fallback) {
                    case RocqCertWFFallback::Fail:
                      fatal(ctx,
                            "cert_wf=chunked: chunk " + std::to_string(c)
                                + " contains an op with no simpleOpCertWfBool shape rule (Sub/Mult/Div/SDiv/LT/GT/SetMask). "
                                  "Re-run with --set formal.rocq.cert_wf_fallback=eval to emit the chunk well-formedness "
                                  "lemma only, or =sorry to admit the shape lemma.");
                    case RocqCertWFFallback::Sorry:
                      cfs << "(* cert_wf_fallback=sorry: chunk " << c
                          << " has an op outside simpleOpCertWfBool's shape rules. *)\n";
                      cfs << "Theorem " << base_name << "_cert_chunk_" << c << "_shape :\n";
                      cfs << "  forallb simpleNodeCertShapeWfBool " << base_name << "_cert_chunk_" << c << " = true.\n";
                      cfs << "Admitted.\n\n";
                      break;
                    case RocqCertWFFallback::Eval:
                      cfs << "(* cert_wf_fallback=eval: chunk " << c
                          << " has an op outside simpleOpCertWfBool's shape rules, so only the\n";
                      cfs << "   chunk well-formedness lemma above is emitted for it. *)\n\n";
                      break;
                  }
                }
              }
              if (emit_chunks != total_chunks) {
                cfs << "(* cert_chunk_limit=" << cert_chunk_limit << ": emitted " << emit_chunks << " of " << total_chunks
                    << " chunks; " << (total_chunks - emit_chunks) << " chunk(s) DROPPED for proof-shape testing. *)\n";
                std::cout << "pass.rocq: " << raw_name << " cert_chunk_limit dropped " << (total_chunks - emit_chunks)
                          << " of " << total_chunks << " certificate chunks\n";
              }
              break;
            }
          }

          cfs.close();
          cert_written = true;
          std::cout << "pass.rocq: " << raw_name << " -> " << model_path << ", " << cert_path << " (" << topo.size()
                    << " nodes, " << flop_nodes.size() << " flops, " << source_ids.size()
                    << " cert sources, sequential=" << (sequential ? "yes" : "no") << ")\n";
        }
      } catch (const Emit_error& err) {
        abort_cert(err);
        return;
      }
    }
  }

  if (!cert_written) {
    std::cout << "pass.rocq: " << raw_name << " -> " << model_path << " (" << topo.size() << " nodes, " << flop_nodes.size()
              << " flops, sequential=" << (sequential ? "yes" : "no") << ", certificate disabled)\n";
  }

  // -------------------------------------------------------------------------
  // A _CoqProject for the emitted directory.  The RocqSemanticPrimitives root
  // is NOT written here because the pass does not know where the repository
  // lives; scripts/run_dino_lgraph_rocq.sh prepends it, mirroring how the
  // Isabelle flow generates its ROOT files at run time.
  // -------------------------------------------------------------------------
  {
    std::ofstream pfs(proj_path, std::ios::app);
    if (pfs) {
      static bool banner = false;
      if (!banner) {
        // Only the first design in a directory writes the header.
        std::ifstream probe(proj_path);
        std::string   first;
        std::getline(probe, first);
        if (first.empty()) {
          pfs << "# Generated by LiveHD pass.rocq.\n";
          pfs << "# Prepend the support-library root before building, e.g.\n";
          pfs << "#   -R <livehd>/formal/rocq/theories RocqSemanticPrimitives\n";
          pfs << "-Q . LiveHD\n\n";
        }
        banner = true;
      }
      pfs << base_name << "_Lgraph.v\n";
      if (cert_written) {
        pfs << base_name << "_Lgraph_Cert.v\n";
      }
    }
  }
}
