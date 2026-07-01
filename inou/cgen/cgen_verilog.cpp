// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_verilog.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/graph.hpp"
#include "iassert.hpp"
#include "node_util.hpp"  // //graph:graph — livehd::graph_util::* helpers
#include "perf_tracing.hpp"
#include "str_tools.hpp"
// pass.hpp pulls in the diag reporting surface (livehd::diag) and Pass::info.
#include "pass.hpp"
// hlop's Dlop is the Dlop representation; we deserialize node-level
// const-value strings via Dlop::unserialize.
#include "hlop/dlop.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::color_of;
using livehd::graph_util::const_value_of;
using livehd::graph_util::debug_name;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::has_color;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::is_type_const;
using livehd::graph_util::is_type_flop;
using livehd::graph_util::is_type_register;
using livehd::graph_util::is_type_sub;
using livehd::graph_util::is_unsign;
using livehd::graph_util::node_name_of;
using livehd::graph_util::pin_name_of;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {

using livehd::graph_util::hydrate_const;

// Emit a constant as Verilog. hlop's Dlop::to_verilog() formats a NEGATIVE
// value as its bare magnitude ("{n}'sh{mag}", dropping the sign), so it
// re-reads as +mag (e.g. -6 -> "4'sh6" == +6, a silent miscompile that broke
// signed mux arms / reset values). Re-emit a known negative as its
// two's-complement hex at the declared width so the bit pattern round-trips
// through a Verilog re-read. (Wide non-i64 negatives are rare and left to the
// upstream path.)
template <typename C>
std::string const_to_verilog(const C& c) {
  if (c.is_negative() && !c.has_unknowns() && c.is_just_i64()) {
    int nbits = c.get_bits();
    if (nbits < 1) {
      nbits = 1;
    }
    uint64_t tc = static_cast<uint64_t>(c.to_just_i64());
    if (nbits < 64) {
      tc &= (uint64_t{1} << nbits) - 1;
    }
    return absl::StrCat(nbits, "'sh", absl::Hex(tc));
  }
  return c.to_verilog();
}

// Sort edges by sink port_id (for mux iteration).
void sort_by_sink_pid(livehd::graph_util::Edge_vec& edges) {
  std::sort(edges.begin(), edges.end(), [](const hhds::Edge_class& a, const hhds::Edge_class& b) {
    return a.sink.get_port_id() < b.sink.get_port_id();
  });
}

}  // namespace

Cgen_verilog::Cgen_verilog(bool _verbose, std::string_view _odir, bool _srcmap)
    : verbose(_verbose), odir(_odir), srcmap(_srcmap), nrunning(0) {
  static std::once_flag init_once;
  std::call_once(init_once, [] {
    // Full Verilog-2005 (IEEE 1364) + SystemVerilog (IEEE 1800-2017, Annex B)
    // reserved word set. A Pyrope signal/port/instance name that collides with
    // any of these MUST be emitted as an escaped identifier (`\name `), or
    // yosys / iverilog -g2012 / verilator reject the netlist (e.g. a signal
    // named `packed`, `bit`, `type`). get_scaped_name() consults this set.
    reserved_keyword.insert({
        "accept_on",
        "alias",
        "always",
        "always_comb",
        "always_ff",
        "always_latch",
        "and",
        "assert",
        "assign",
        "assume",
        "automatic",
        "before",
        "begin",
        "bind",
        "bins",
        "binsof",
        "bit",
        "break",
        "buf",
        "bufif0",
        "bufif1",
        "byte",
        "case",
        "casex",
        "casez",
        "cell",
        "chandle",
        "checker",
        "class",
        "clocking",
        "cmos",
        "config",
        "const",
        "constraint",
        "context",
        "continue",
        "cover",
        "covergroup",
        "coverpoint",
        "cross",
        "deassign",
        "default",
        "defparam",
        "design",
        "disable",
        "dist",
        "do",
        "edge",
        "else",
        "end",
        "endcase",
        "endchecker",
        "endclass",
        "endclocking",
        "endconfig",
        "endfunction",
        "endgenerate",
        "endgroup",
        "endinterface",
        "endmodule",
        "endpackage",
        "endprimitive",
        "endprogram",
        "endproperty",
        "endspecify",
        "endsequence",
        "endtable",
        "endtask",
        "enum",
        "event",
        "eventually",
        "expect",
        "export",
        "extends",
        "extern",
        "final",
        "first_match",
        "for",
        "force",
        "foreach",
        "forever",
        "fork",
        "forkjoin",
        "function",
        "generate",
        "genvar",
        "global",
        "highz0",
        "highz1",
        "if",
        "iff",
        "ifnone",
        "ignore_bins",
        "illegal_bins",
        "implements",
        "implies",
        "import",
        "incdir",
        "include",
        "initial",
        "inout",
        "input",
        "inside",
        "instance",
        "int",
        "integer",
        "interconnect",
        "interface",
        "intersect",
        "join",
        "join_any",
        "join_none",
        "large",
        "let",
        "liblist",
        "library",
        "local",
        "localparam",
        "logic",
        "longint",
        "macromodule",
        "matches",
        "medium",
        "modport",
        "module",
        "nand",
        "negedge",
        "nettype",
        "new",
        "nexttime",
        "nmos",
        "nor",
        "noshowcancelled",
        "not",
        "notif0",
        "notif1",
        "null",
        "or",
        "output",
        "package",
        "packed",
        "parameter",
        "pmos",
        "posedge",
        "primitive",
        "priority",
        "program",
        "property",
        "protected",
        "pull0",
        "pull1",
        "pulldown",
        "pullup",
        "pulsestyle_ondetect",
        "pulsestyle_onevent",
        "pure",
        "rand",
        "randc",
        "randcase",
        "randsequence",
        "rcmos",
        "real",
        "realtime",
        "ref",
        "reg",
        "reject_on",
        "release",
        "repeat",
        "restrict",
        "return",
        "rnmos",
        "rpmos",
        "rtran",
        "rtranif0",
        "rtranif1",
        "s_always",
        "s_eventually",
        "s_nexttime",
        "s_until",
        "s_until_with",
        "scalared",
        "sequence",
        "shortint",
        "shortreal",
        "showcancelled",
        "signed",
        "small",
        "soft",
        "solve",
        "specify",
        "specparam",
        "static",
        "string",
        "strong",
        "strong0",
        "strong1",
        "struct",
        "super",
        "supply0",
        "supply1",
        "sync_accept_on",
        "sync_reject_on",
        "table",
        "tagged",
        "task",
        "this",
        "throughout",
        "time",
        "timeprecision",
        "timeunit",
        "tran",
        "tranif0",
        "tranif1",
        "tri",
        "tri0",
        "tri1",
        "triand",
        "trior",
        "trireg",
        "type",
        "typedef",
        "union",
        "unique",
        "unique0",
        "unsigned",
        "until",
        "until_with",
        "untyped",
        "use",
        "uwire",
        "var",
        "vectored",
        "virtual",
        "void",
        "wait",
        "wait_order",
        "wand",
        "weak",
        "weak0",
        "weak1",
        "while",
        "wildcard",
        "wire",
        "with",
        "within",
        "wor",
        "xnor",
        "xor",
    });
  });
}

std::string Cgen_verilog::pin_wire_name(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return {};
  }
  // Graph-IO pins resolve via HHDS's get_pin_name (it walks the GraphIO
  // declaration tables on INPUT_NODE / OUTPUT_NODE pins).
  if (is_graph_input_pin(pin) || is_graph_output_pin(pin)) {
    auto n = pin.get_pin_name();
    if (!n.empty()) {
      return std::string{n};
    }
  }
  return wire_name(pin);
}

hhds::Pin_class Cgen_verilog::get_driver(const hhds::Pin_class& sink) {
  if (sink.is_invalid()) {
    return {};
  }
  // get_driver_pins() reads the sink's fan-in directly (no Edge_class vector);
  // a sink's fan-in is 0-1 for the vast majority of pins.
  auto drivers = sink.get_driver_pins();
  if (drivers.empty()) {
    return {};
  }
  // Single-driver accessor: correct for max-1-driver sinks (e.g. SRA "a", flop
  // "din"), but a multi-driver port (Sum a/b, bit_or, memory) must read ALL
  // drivers (inp_drivers_of). Assert the caller is not silently dropping fan-in.
  I(drivers.size() == 1);
  return drivers.front();
}

hhds::Pin_class Cgen_verilog::find_sink_pin(const hhds::Node_class& node, std::string_view name) {
  if (node.is_invalid()) {
    return {};
  }
  // For Sub nodes the sink name comes from the sub-graph's GraphIO and HHDS
  // resolves it directly. For all other Ntype_op cells the sink name is a
  // LiveHD convention (e.g. "a", "din", "clock_pin") — translate it to a
  // port_id via Ntype before asking HHDS for the pin.
  //
  // HHDS asserts when get_sink_pin(port_id) is called for an unmaterialized
  // pin. cgen frequently asks for optional pins (e.g. `reset_pin`, `async`,
  // `negreset`, `initial` on a flop) that may not be connected at all. To
  // emulate LiveHD's invalid-on-miss behaviour we walk inp_edges and match
  // by port_id — slower than a direct fetch but safe.
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    auto pin = node.get_sink_pin(name);  // sub-graph path: HHDS knows the port_id
    return pin;
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

std::string Cgen_verilog::get_wire_or_const(const hhds::Pin_class& dpin) const {
  auto var_it = pin2var.find(dpin.get_class_index());
  if (var_it != pin2var.end()) {
    return var_it->second;
  }

  if (is_const_pin(dpin)) {
    return const_to_verilog(hydrate_const(dpin));
  }

  return get_scaped_name(pin_wire_name(dpin));
}

std::string Cgen_verilog::get_scaped_name(std::string_view name) {
  std::string res_name;
  if (name.empty()) {
    return res_name;
  }
  // LNAST backtick-quoted names (`a[0]`) carry the ORIGINAL verilog escaped
  // identifier inside the quotes; strip them before re-escaping.
  if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
    name.remove_prefix(1);
    name.remove_suffix(1);
  }
  if (reserved_keyword.contains(name)) {
    return absl::StrCat("\\", name, " ");
  } else {
    res_name = name;
  }

  for (auto i = 0u; i < res_name.size(); ++i) {
    auto ch = res_name[i];
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
      return absl::StrCat("\\", res_name, " ");
    }
  }

  return res_name;
}

bool Cgen_verilog::sra_operand_signed(const hhds::Pin_class& dpin) {
  if (dpin.is_invalid()) {
    return false;
  }
  if (!is_unsign(dpin)) {
    return true;
  }
  // The signed hint is dropped on every op output by tolg's bind_result, so a
  // chained right shift `(a>>b)>>b` reads as unsigned at the outer SRA even
  // though the inner SRA preserves the signed `a`. Walk through the SRA chain.
  auto node = dpin.get_master_node();
  if (!node.is_invalid() && type_op_of(node) == Ntype_op::SRA) {
    return sra_operand_signed(get_driver(find_sink_pin(node, "a")));
  }
  return false;
}

std::string Cgen_verilog::get_append_to_name(std::string_view name, std::string_view ext) {
  if (!name.empty() && name.front() == '\\') {
    return absl::StrCat("\\", ext, name.substr(1, name.size() - 1), " ");
  }

  return absl::StrCat(ext, name);
}

std::string Cgen_verilog::get_unique_decl_name(std::string_view name) {
  std::string base{name};
  auto [it, inserted] = declared_name_counts.insert({base, 1});
  if (inserted) {
    return base;
  }
  int n = it->second;  // copy out: a later insert() may rehash and invalidate `it`

  // Escaped Verilog id (`\name `): the `_cgen<N>` suffix goes before the
  // trailing space. Keep bumping N until the candidate is itself unused, then
  // RESERVE it — so a suffixed name can never alias a user signal, a port (the
  // io decls are pre-seeded in do_from_graph), or a previously-emitted name.
  const bool  escaped = !base.empty() && base.front() == '\\';
  std::string core    = base;
  if (escaped) {
    while (!core.empty() && core.back() == ' ') {
      core.pop_back();
    }
  }
  std::string result;
  do {
    result = escaped ? absl::StrCat(core, "_cgen", n++, " ") : absl::StrCat(core, "_cgen", n++);
  } while (!declared_name_counts.insert({result, 1}).second);
  declared_name_counts[base] = n;  // persist the advanced counter for the next call
  return result;
}

std::string Cgen_verilog::get_expression(const hhds::Pin_class& dpin) {
  auto var_it = pin2var.find(dpin.get_class_index());
  if (var_it != pin2var.end()) {
    return var_it->second;
  }

  const auto expr_it = pin2expr.find(dpin.get_class_index());
  if (expr_it != pin2expr.end()) {
    if (expr_it->second.needs_parenthesis) {
      return absl::StrCat("(", expr_it->second.var, ")");
    }
    return expr_it->second.var;
  }

  // Graph-IO pins on OUTPUT_NODE/INPUT_NODE can be referenced via different
  // pid encodings (driver vs sink counterpart) than the one create_module_io
  // registered. HHDS's get_pin_name resolves both to the declared name; fall
  // back to that so the emitted Verilog references the right wire.
  if (is_const_pin(dpin)) {
    // Parenthesize like the needs_parenthesis sub-expressions above: callers
    // (e.g. Get_mask/Sext) may append a bit-select suffix directly to this
    // string (`a[hi:lo]`), and a bare sized literal can't take one — Verilog
    // rejects `193'sb0????...?[191:64]` ("expected ';'"). `(193'sb0...)[191:64]`
    // is valid and identical in every other context this return value is used.
    return absl::StrCat("(", const_to_verilog(hydrate_const(dpin)), ")");
  }

  // Single-use unnamed nodes are intentionally not declared in create_locals:
  // process_simple_node normally caches them in pin2expr before consumers ask
  // for them. Large imported graphs can still present a consumer before such a
  // producer in forward_class() order. Do not emit a bare, undeclared net in
  // that case; inline the same local expression the producer would have cached.
  if (!dpin.is_invalid()) {
    auto node = dpin.get_master_node();
    if (!node.is_invalid()) {
      switch (type_op_of(node)) {
        case Ntype_op::Sum:
        case Ntype_op::Ror:
        case Ntype_op::Div:
        case Ntype_op::Not:
        case Ntype_op::LT:
        case Ntype_op::GT:
        case Ntype_op::SHL:
        case Ntype_op::SRA:
        case Ntype_op::Mult:
        case Ntype_op::And:
        case Ntype_op::Or:
        case Ntype_op::Xor:
        case Ntype_op::EQ  : return absl::StrCat("(", build_simple_expr(nullptr, node), ")");
        default            : break;
      }
    }
  }

  auto wn = pin_wire_name(dpin);
  if (!wn.empty()) {
    return get_scaped_name(wn);
  }
  return "'hx /*cgen-miss*/";
}

std::string Cgen_verilog::add_expression(std::string_view txt_seq, std::string_view txt_op, const hhds::Pin_class& dpin) {
  auto expr = get_expression(dpin);

  if (txt_seq.empty()) {
    return expr;
  }

  return absl::StrCat(txt_seq, " ", txt_op, " ", expr);
}

// Record one source-map segment for the statement about to be
// emitted for `node`. The SourceId (stamped by tolg/yosys ingress) resolves
// through the Source_locator at write time; a combined id displays as its
// primary anchor (lossy by design — the full id rides x_hhds).
void Cgen_verilog::note_src(const std::shared_ptr<File_output>& fout, const hhds::Node_class& node) {
  if (!srcmap) {
    return;
  }
  auto ref = node.attr(hhds::attrs::srcid);
  if (!ref.has()) {
    return;
  }
  // prepend offset added at write time
  map_segments_.push_back({static_cast<uint32_t>(fout->append_line()), 0, ref.get()});
}

void Cgen_verilog::note_module(const std::shared_ptr<File_output>& fout) {
  if (!srcmap || module_anchor_.is_invalid()) {
    return;
  }
  note_src(fout, module_anchor_);
}

void Cgen_verilog::write_srcmap(const std::shared_ptr<File_output>& fout, const std::string& filename,
                                const hhds::Source_locator& sl) {
  if (!srcmap || map_segments_.empty()) {
    return;
  }
  // Prepends (the `include lines) land before every appended line: shift the
  // recorded append-relative lines to their final absolute positions.
  for (auto& seg : map_segments_) {
    seg.gen_line += static_cast<uint32_t>(fout->prepend_lines());
  }
  const auto  slash    = filename.find_last_of('/');
  std::string basename = slash == std::string::npos ? filename : filename.substr(slash + 1);
  fout->append("//# sourceMappingURL=", basename, ".map\n");
  std::ofstream ofs(filename + ".map");
  if (ofs.is_open()) {
    ofs << hhds::sourcemap::to_json(basename, sl, std::move(map_segments_));
  }
}

void Cgen_verilog::process_flop(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  note_src(fout, node);
  auto sink_d = find_sink_pin(node, "din");
  auto dpin_d = get_driver(sink_d);
  auto dpin_q = node.get_driver_pin(0);

  auto       pin_name  = get_wire_or_const(dpin_q);
  const auto name_next = get_append_to_name(pin_name, "___next_");

  if (dpin_d.is_invalid()) {
    fout->append("  ", name_next, " = 'hx; // disconnected flop\n");
  } else {
    fout->append("  ", name_next, " = ", get_expression(dpin_d), ";\n");
  }
}

// A level-sensitive latch (yosys $dlatch). Unlike a flop it has no `___next_`
// combinational half: it is emitted directly as `always @* if (en) q = d;`,
// which yosys re-reads as a $dlatch for LEC. The reader maps EN->enable,
// D->din, and connects a const-0 `posclk` only for active-low enable
// (EN_POLARITY==false), so a known-false posclk means the transparent level is
// `!enable` (e.g. prim_clk_gate's `if (!clk_i)`).
void Cgen_verilog::process_latch(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  auto dpin_q = node.get_driver_pin(0);
  auto name   = get_scaped_name(pin_wire_name(dpin_q));

  auto din_dpin = get_driver(find_sink_pin(node, "din"));
  auto en_dpin  = get_driver(find_sink_pin(node, "enable"));
  if (din_dpin.is_invalid() || en_dpin.is_invalid()) {
    return;  // malformed latch: leave the declared reg inert rather than emit garbage
  }
  auto din    = get_wire_or_const(din_dpin);
  auto enable = get_wire_or_const(en_dpin);

  bool neg_en      = false;
  auto posclk_dpin = get_driver(find_sink_pin(node, "posclk"));
  if (!posclk_dpin.is_invalid() && is_const_pin(posclk_dpin)) {
    neg_en = hydrate_const(posclk_dpin).is_known_false();
  }

  note_src(fout, node);
  fout->append("always @* begin\n");
  fout->append(absl::StrCat("  if (", neg_en ? "!" : "", enable, ") ", name, " = ", din, ";\n"));
  fout->append("end\n");
}

// Generate a cgen_memory_[multiclock_]<R>rd_<W>wr module mirroring the static
// ware/rtl wrapper templates, for (R,W,clock) shapes ware/rtl does not ship
// (e.g. a register file that reads out all entries -> many read ports, or a
// multi-clock RF). Semantics match the templates exactly: port-order write
// priority, per-write-port FWD forwarding, and LATENCY_0 (==1 flops the output
// once, ==0 async). Uses $clog2 instead of the `log2 macro to avoid depending
// on a macro that may not be in scope when emitted inline.
std::string Cgen_verilog::gen_mem_wrapper(const std::string& mod_name, int n_rd, int n_wr, bool single_clock) {
  std::string s;
  s          += absl::StrCat("module ", mod_name, "\n");
  s          += "  #(parameter BITS = 4, SIZE=128, FWD=1, LATENCY_0=1, WENSIZE=1,\n";
  s          += "    parameter INIT_EN=0, parameter [BITS*SIZE-1:0] INIT=0)\n  (\n";
  bool first  = true;
  auto port   = [&](const std::string& decl) {
    s     += (first ? "    " : "   ,") + decl + "\n";
    first  = false;
  };
  if (single_clock) {
    port("input clk");
  }
  for (int k = 0; k < n_rd; ++k) {
    port(absl::StrCat("input [$clog2(SIZE)-1:0] rd_addr_", k));
    port(absl::StrCat("input rd_enable_", k));
    if (!single_clock) {
      port(absl::StrCat("input rd_clock_", k));
    }
    port(absl::StrCat("output reg [BITS-1:0] rd_dout_", k));
  }
  for (int j = 0; j < n_wr; ++j) {
    port(absl::StrCat("input [$clog2(SIZE)-1:0] wr_addr_", j));
    port(absl::StrCat("input [WENSIZE-1:0] wr_enable_", j));
    if (!single_clock) {
      port(absl::StrCat("input wr_clock_", j));
    }
    port(absl::StrCat("input [BITS-1:0] wr_din_", j));
  }
  s            += "  );\n";
  s            += "localparam MASKSIZE = BITS/WENSIZE;\n";
  s            += "reg [BITS-1:0] data[SIZE-1:0];\n";
  s            += "generate if (INIT_EN) begin:BLOCK_INIT\n";
  s            += "  integer ii;\n  initial for(ii=0;ii<SIZE;ii=ii+1) data[ii] = INIT[ii*BITS +: BITS];\n";
  s            += "end endgenerate\n";
  // WRITE: single-clock = one always with all ports (port-order priority);
  // multiclock = one always per write clock.
  s            += "integer i;\n";
  auto wr_body  = [&](int j) {
    s += absl::StrCat("  for(i=0;i<WENSIZE;i=i+1) if(wr_enable_",
                      j,
                      "[i]) data[wr_addr_",
                      j,
                      "][i*MASKSIZE +: MASKSIZE] <= wr_din_",
                      j,
                      "[i*MASKSIZE +: MASKSIZE];\n");
  };
  if (single_clock) {
    s += "always @(posedge clk) begin\n";
    for (int j = 0; j < n_wr; ++j) {
      wr_body(j);
    }
    s += "end\n";
  } else {
    for (int j = 0; j < n_wr; ++j) {
      s += absl::StrCat("always @(posedge wr_clock_", j, ") begin\n");
      wr_body(j);
      s += "end\n";
    }
  }
  // READ ports
  for (int k = 0; k < n_rd; ++k) {
    s += absl::StrCat("reg [BITS-1:0] d", k, "_mem; reg [BITS-1:0] d", k, "_fwd;\n");
    s += absl::StrCat("always_comb d", k, "_mem = rd_enable_", k, " ? data[rd_addr_", k, "] : {BITS{1'bx}};\n");
    s += absl::StrCat("genvar fwd_j", k, ";\n");
    s += absl::StrCat("generate for(fwd_j",
                      k,
                      "=0;fwd_j",
                      k,
                      "<WENSIZE;fwd_j",
                      k,
                      "=fwd_j",
                      k,
                      "+1) begin:FWD_BLOCK_CALC_",
                      k,
                      "\n");
    s += absl::StrCat("  always_comb d", k, "_fwd[fwd_j", k, "*MASKSIZE +: MASKSIZE] =\n");
    for (int j = 0; j < n_wr; ++j) {
      s += absl::StrCat("    (((FWD >> ",
                        j,
                        ") & 1) != 0 && wr_enable_",
                        j,
                        "[fwd_j",
                        k,
                        "] && (wr_addr_",
                        j,
                        " == rd_addr_",
                        k,
                        ")) ? wr_din_",
                        j,
                        "[fwd_j",
                        k,
                        "*MASKSIZE +: MASKSIZE] :\n");
    }
    s += absl::StrCat("    d", k, "_mem[fwd_j", k, "*MASKSIZE +: MASKSIZE];\n");
    s += "end endgenerate\n";
    s += absl::StrCat("generate if (LATENCY_0==1) begin:BLOCK_RD_LAT_", k, "\n");
    s += absl::StrCat("  always @(posedge ",
                      single_clock ? std::string("clk") : absl::StrCat("rd_clock_", k),
                      ") rd_dout_",
                      k,
                      " <= d",
                      k,
                      "_fwd;\n");
    s += absl::StrCat("end else begin:BLOCK_RD_COMB_", k, "\n  assign rd_dout_", k, " = d", k, "_fwd;\nend endgenerate\n");
  }
  s += "endmodule\n";
  return s;
}

void Cgen_verilog::process_memory(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  note_src(fout, node);
  auto iname = get_scaped_name(default_instance_name(node));

  int n_rd_ports = 0;
  int n_wr_ports = 0;

  struct Port_field {
    bool            rdport = false;
    hhds::Pin_class enable;
    hhds::Pin_class addr;
    hhds::Pin_class clock;
    hhds::Pin_class din;  // only for write port
  };
  std::vector<Port_field> port_vector;

  int mem_size    = 0;
  int mem_bits    = 0;
  int mem_fwd     = 0;
  int mem_type    = 2;  // array by default
  int mem_wensize = 0;

  hhds::Pin_class mem_init_dpin;  // comptime contents OR (whole-array) runtime reset-value bus (entry 0 in the low bits)
  // Whole-array pins (driven => this cell is a whole-array memory: one `update`
  // bus instead of N per-entry write ports; an async `read_all` output).
  hhds::Pin_class mem_update_dpin;         // whole-array next-state bus (size*bits, entry 0 low)
  hhds::Pin_class mem_update_enable_dpin;  // optional bulk-update enable (absent => always-on)
  hhds::Pin_class mem_reset_dpin;          // 1-bit reset condition (registered whole-array)

  for (auto e : node.inp_edges()) {
    // HHDS does not store LiveHD's per-sink-name convention; derive the
    // name from the port_id via Ntype::get_sink_name. For memory the names
    // wrap with `pid % Memory_port_stride` (see Ntype::get_sink_name).
    auto   raw_pid  = static_cast<int>(e.sink.get_port_id());
    auto   pin_name = Ntype::get_sink_name(Ntype_op::Memory, raw_pid);
    size_t port_id  = static_cast<size_t>(raw_pid) / Ntype::Memory_port_stride;

    if (port_vector.size() <= port_id) {
      port_vector.resize(1 + port_id);
    }

    if (pin_name == "bits") {
      if (!is_const_pin(e.driver)) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("memory {} should have a constant for bits not {}", debug_name(node), debug_name(e.driver.get_master_node()))
            .fatal();
        return;
      }
      mem_bits = hydrate_const(e.driver).to_just_i64();
    } else if (pin_name == "size") {
      if (!is_const_pin(e.driver)) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("memory {} should have a constant for size not {}", debug_name(node), debug_name(e.driver.get_master_node()))
            .fatal();
        return;
      }
      mem_size = hydrate_const(e.driver).to_just_i64();
    } else if (pin_name == "type") {
      if (!is_const_pin(e.driver)) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("memory {} should have a constant type not {}", debug_name(node), debug_name(e.driver.get_master_node()))
            .fatal();
        return;
      }
      mem_type = hydrate_const(e.driver).to_just_i64();
    } else if (pin_name == "wensize") {
      if (!is_const_pin(e.driver)) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("memory {} should have a constant for wensize not {}", debug_name(node), debug_name(e.driver.get_master_node()))
            .fatal();
        return;
      }
      mem_wensize = hydrate_const(e.driver).to_just_i64();
    } else if (pin_name == "fwd") {
      if (!is_const_pin(e.driver)) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("memory {} should have a constant for fwd not {}", debug_name(node), debug_name(e.driver.get_master_node()))
            .fatal();
        return;
      }
      mem_fwd = hydrate_const(e.driver).to_just_i64();
    } else if (pin_name == "init") {
      // For a plain memory `init` is the comptime power-on contents; for a
      // whole-array cell (the `update` pin is driven) it is the RUNTIME reset
      // value bus, so do not force a constant here — the const-consuming paths
      // (wrapper INIT param, type-2 default fill) only run when there is no update.
      mem_init_dpin = e.driver;
    } else if (pin_name == "update") {
      mem_update_dpin = e.driver;
    } else if (pin_name == "update_enable") {  // MUST precede the ends_with("enable") per-port branch below
      mem_update_enable_dpin = e.driver;
    } else if (pin_name == "reset") {
      mem_reset_dpin = e.driver;
    } else if (str_tools::ends_with(pin_name, "clock_pin")) {
      port_vector[port_id].clock = e.driver;
    } else if (str_tools::ends_with(pin_name, "addr")) {
      port_vector[port_id].addr = e.driver;
    } else if (str_tools::ends_with(pin_name, "enable")) {
      port_vector[port_id].enable = e.driver;
    } else if (str_tools::ends_with(pin_name, "din")) {
      port_vector[port_id].din = e.driver;
    } else if (str_tools::ends_with(pin_name, "rdport")) {
      if (!is_const_pin(e.driver)) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("memory {} should have a constant rdport not {}", debug_name(node), debug_name(e.driver.get_master_node()))
            .fatal();
        return;
      }
      auto v                      = hydrate_const(e.driver);
      bool rdport                 = !v.is_known_false();
      port_vector[port_id].rdport = rdport;
      if (rdport) {
        ++n_rd_ports;
      } else {
        ++n_wr_ports;
      }
    }
  }

  // ── Whole-array memory (the `update` bus is driven) ─────────────────────────
  // The entire array is (re)written from one `update` bus instead of N per-entry
  // write ports: registered (clocked, reset to the runtime `init` bus) when a
  // clock is present, else combinational. Per-port writes still apply and OVERRIDE
  // the bulk update (emitted after it => last-write wins). An async `read_all`
  // driver (reserved pid) exposes the whole array. Emitted inline as a reg-array;
  // the cgen_memory_* wrappers cannot carry a runtime reset bus / update / read_all.
  if (!mem_update_dpin.is_invalid()) {
    const auto aname      = absl::StrCat(iname, "_data");
    const auto clock_dpin = port_vector.empty() ? hhds::Pin_class{} : port_vector[0].clock;
    const bool registered = !clock_dpin.is_invalid();
    const int  busw       = mem_size * mem_bits;

    fout->append(absl::StrCat("reg [", mem_bits - 1, ":0] ", aname, "[", mem_size - 1, ":0];\n"));
    // Bind the buses to nets so per-entry part-selects are always legal.
    const auto updbus = absl::StrCat(aname, "_upd");
    fout->append(absl::StrCat("wire [", busw - 1, ":0] ", updbus, " = ", get_wire_or_const(mem_update_dpin), ";\n"));
    std::string initbus;
    if (!mem_init_dpin.is_invalid()) {
      initbus = absl::StrCat(aname, "_rst");
      fout->append(absl::StrCat("wire [", busw - 1, ":0] ", initbus, " = ", get_wire_or_const(mem_init_dpin), ";\n"));
    }
    auto entry_sel = [&](const std::string& bus, int i) {
      return absl::StrCat(bus, "[", (i + 1) * mem_bits - 1, ":", i * mem_bits, "]");
    };

    if (registered) {
      fout->append(absl::StrCat("always @(posedge ", get_wire_or_const(clock_dpin), ") begin\n"));
      std::string ind = "  ";
      if (!mem_reset_dpin.is_invalid()) {  // sync reset to the runtime init/reset bus (highest priority)
        fout->append(absl::StrCat("  if (", get_wire_or_const(mem_reset_dpin), ") begin\n"));
        for (int i = 0; i < mem_size; ++i) {
          fout->append(
              absl::StrCat("    ", aname, "[", i, "] <= ", initbus.empty() ? std::string("'b0") : entry_sel(initbus, i), ";\n"));
        }
        fout->append("  end else begin\n");
        ind = "    ";
      }
      const bool gated = !mem_update_enable_dpin.is_invalid();
      if (gated) {
        fout->append(absl::StrCat(ind, "if (", get_wire_or_const(mem_update_enable_dpin), ") begin\n"));
      }
      for (int i = 0; i < mem_size; ++i) {  // bulk update (default); per-port writes below override
        fout->append(absl::StrCat(ind, gated ? "  " : "", aname, "[", i, "] <= ", entry_sel(updbus, i), ";\n"));
      }
      if (gated) {
        fout->append(absl::StrCat(ind, "end\n"));
      }
      for (auto& p : port_vector) {  // per-port writes OVERRIDE the bulk update (later <= wins)
        if (p.rdport || p.addr.is_invalid() || p.din.is_invalid()) {
          continue;
        }
        auto w = absl::StrCat(aname, "[", get_wire_or_const(p.addr), "] <= ", get_wire_or_const(p.din), ";\n");
        fout->append(p.enable.is_invalid() ? absl::StrCat(ind, w) : absl::StrCat(ind, "if (", get_wire_or_const(p.enable), ") ", w));
      }
      if (!mem_reset_dpin.is_invalid()) {
        fout->append("  end\n");
      }
      fout->append("end\n");
    } else {  // combinational whole-array (no clock); update_enable n/a (no hold state)
      fout->append("always_comb begin\n");
      for (int i = 0; i < mem_size; ++i) {
        fout->append(absl::StrCat("  ", aname, "[", i, "] = ", entry_sel(updbus, i), ";\n"));
      }
      for (auto& p : port_vector) {
        if (p.rdport || p.addr.is_invalid() || p.din.is_invalid()) {
          continue;
        }
        auto w = absl::StrCat(aname, "[", get_wire_or_const(p.addr), "] = ", get_wire_or_const(p.din), ";\n");
        fout->append(p.enable.is_invalid() ? absl::StrCat("  ", w) : absl::StrCat("  if (", get_wire_or_const(p.enable), ") ", w));
      }
      fout->append("end\n");
    }

    // Async reads: per-entry douts (read port N => pid n_wr_ports+N) + read_all.
    // Registered douts are `wire` (create_locals, type!=2) -> continuous assign;
    // combinational douts are `reg` -> drive inside an always_comb.
    const bool reads_in_comb = !registered;
    if (reads_in_comb) {
      fout->append("always_comb begin\n");
    }
    auto drive = [&](const std::string& dest, const std::string& rhs) {
      fout->append(reads_in_comb ? absl::StrCat("  ", dest, " = ", rhs, ";\n") : absl::StrCat("assign ", dest, " = ", rhs, ";\n"));
    };
    int n_rd_pos = 0;
    for (auto& p : port_vector) {
      if (!p.rdport) {
        continue;
      }
      if (p.addr.is_invalid()) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("array {} read port is not correctly configured", debug_name(node))
            .fatal();
      }
      auto dout_dpin = node.create_driver_pin(static_cast<hhds::Port_id>(n_wr_ports + n_rd_pos));
      drive(get_wire_or_const(dout_dpin), absl::StrCat(aname, "[", get_wire_or_const(p.addr), "]"));
      ++n_rd_pos;
    }
    bool has_read_all = false;
    for (const auto& e2 : node.out_edges()) {
      if (static_cast<hhds::Port_id>(e2.driver.get_port_id()) == Ntype::Memory_readall_pid) {
        has_read_all = true;
        break;
      }
    }
    if (has_read_all) {  // {data[size-1], ..., data[0]} (entry 0 in the low bits)
      std::string cat = "{";
      for (int i = mem_size - 1; i >= 0; --i) {
        cat += absl::StrCat(aname, "[", std::to_string(i), "]", i ? "," : "");
      }
      cat += "}";
      auto ra = node.create_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
      drive(get_wire_or_const(ra), cat);
    }
    if (reads_in_comb) {
      fout->append("end\n");
    }
    return;
  }

  if (mem_type == 0 || mem_type == 1) {  // sync or async memory
    bool            single_clock    = true;
    hhds::Pin_class base_clock_dpin = port_vector.empty() ? hhds::Pin_class{} : port_vector[0].clock;
    for (auto& p : port_vector) {
      auto& dpin = p.clock;
      if (dpin.is_invalid()) {
        dpin = base_clock_dpin;
        continue;
      }
      if (dpin != base_clock_dpin) {
        single_clock = false;
      }
    }

    if (base_clock_dpin.is_invalid()) {
      livehd::diag::err("inou.cgen", "mem-malformed", "internal")
          .msg("memory {} should have a clock pin", debug_name(node))
          .fatal();
      return;
    }

    // The wrapper variants start at 1rd_1wr: a read-less memory (scan/regref
    // observed) or a write-less one (scan/regref loaded) still instantiates
    // the smallest variant with the dummy port tied off below.
    const int eff_rd = n_rd_ports > 0 ? n_rd_ports : 1;
    const int eff_wr = n_wr_ports > 0 ? n_wr_ports : 1;

    // ware/rtl carries a fixed wrapper family; anything beyond it (e.g. a
    // big reset-restored reg array minting one restore port per entry) needs
    // a new cgen_memory_<R>rd_<W>wr.v variant.
    const bool  have_wrapper = single_clock ? ((eff_rd >= 1 && eff_rd <= 4 && eff_wr >= 1 && eff_wr <= 2)
                                               || (eff_rd == 1 && (eff_wr == 3 || eff_wr == 4)))
                                            : (eff_rd == 1 && eff_wr == 1);
    std::string name;
    name = absl::StrCat(name, "cgen_memory_", single_clock ? "" : "multiclock_");
    name = absl::StrCat(name, eff_rd, "rd_");
    name = absl::StrCat(name, eff_wr, "wr");

    // ware/rtl ships a fixed wrapper family; for any other (R,W,clock) shape
    // (e.g. a register file reading out all entries, or a multi-clock RF)
    // generate the wrapper module inline instead of `include`ing a missing file.
    // Dedup per file so two same-shape memories do not re-define the module.
    if (mem_wrappers_emitted_.insert(name).second) {
      if (have_wrapper) {
        fout->prepend(absl::StrCat("`include \"", name, ".v\" \n"));
      } else {
        fout->prepend(gen_mem_wrapper(name, eff_rd, eff_wr, single_clock));
      }
    }
    fout->append(absl::StrCat(name));

    std::string parameters;
    bool        first_entry = true;

    parameters  = absl::StrCat(parameters, first_entry ? " " : " ,", ".LATENCY_0(", mem_type, ")");
    first_entry = false;
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".BITS(", mem_bits, ")");
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".SIZE(", mem_size, ")");
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".WENSIZE", "(", mem_wensize, ")");
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".FWD", "(", mem_fwd, ")");
    if (!mem_init_dpin.is_invalid()) {
      // Power-on contents ride the wrapper's INIT parameter (packed, entry 0
      // in the low BITS); only the single-clock wrappers carry it.
      if (!single_clock) {
        livehd::diag::err("inou.cgen", "mem-multiclock-init", "unsupported")
            .msg("memory {} init contents are not supported on multiclock memories yet", debug_name(node))
            .fatal();
        return;
      }
      parameters = absl::StrCat(parameters, " ,.INIT_EN(1) ,.INIT(", const_to_verilog(hydrate_const(mem_init_dpin)), ")");
    }
    fout->append(" #(", parameters, ") ");

    fout->append(iname, "(\n");

    first_entry = true;
    if (single_clock) {
      fout->append(absl::StrCat(".clk(", get_wire_or_const(base_clock_dpin), ")\n"));
      first_entry = false;
    }

    auto n_rd_pos = 0;
    auto n_wr_pos = 0;
    for (auto& p : port_vector) {
      if (p.addr.is_invalid() && p.din.is_invalid() && p.enable.is_invalid()) {
        // A phantom slot holding only the shared clock_pin (pid 2 lands in
        // port 0's clock field even when port 0 was never minted).
        continue;
      }
      if (p.rdport) {
        if (p.addr.is_invalid() || p.enable.is_invalid() || p.clock.is_invalid()) {
          livehd::diag::err("inou.cgen", "mem-malformed", "internal")
              .msg("memory {} read port is not correctly configured", debug_name(node))
              .fatal();
        }
        fout->append(absl::StrCat(first_entry ? "  .rd_addr_" : "  ,.rd_addr_", n_rd_pos, "(", get_wire_or_const(p.addr), ")\n"));
        first_entry = false;

        fout->append("  ,.rd_enable_", std::to_string(n_rd_pos), "(", get_wire_or_const(p.enable), ")\n");
        if (!single_clock) {
          fout->append("  ,.rd_clock_", std::to_string(n_rd_pos), "(", get_wire_or_const(p.clock), ")\n");
        }
        // The dout driver pin for read port N is pid (n_wr_ports + N) — the
        // convention resolve_memory uses in lgyosys_tolg (`wrports + rdport`).
        // Enumerating all out pins here would wire every dout to every port.
        auto dout_dpin = node.create_driver_pin(static_cast<hhds::Port_id>(n_wr_ports + n_rd_pos));  // find-or-create
        if (!dout_dpin.out_edges().empty()) {
          fout->append("  ,.rd_dout_", std::to_string(n_rd_pos), "(", get_wire_or_const(dout_dpin), ")\n");
        }
        ++n_rd_pos;
      } else {
        if (p.addr.is_invalid() || p.enable.is_invalid() || p.clock.is_invalid() || p.din.is_invalid()) {
          livehd::diag::err("inou.cgen", "mem-malformed", "internal")
              .msg("memory {} write port is not correctly configured", debug_name(node))
              .fatal();
        }
        fout->append(absl::StrCat(first_entry ? "  .wr_addr_" : "  ,.wr_addr_",
                                  std::to_string(n_wr_pos),
                                  "(",
                                  get_wire_or_const(p.addr),
                                  ")\n"));
        first_entry = false;

        fout->append("  ,.wr_enable_", std::to_string(n_wr_pos), "(", get_wire_or_const(p.enable), ")\n");
        if (!single_clock) {
          fout->append("  ,.wr_clock_", std::to_string(n_wr_pos), "(", get_wire_or_const(p.clock), ")\n");
        }
        fout->append("  ,.wr_din_", std::to_string(n_wr_pos), "(", get_wire_or_const(p.din), ")\n");
        ++n_wr_pos;
      }
    }
    I(n_rd_pos == n_rd_ports);
    I(n_wr_pos == n_wr_ports);

    // Tie off the dummy port of a read-less / write-less memory (dout of the
    // dummy read port is simply left unconnected).
    if (n_rd_ports == 0) {
      fout->append(first_entry ? "  .rd_addr_0(1'b0)\n" : "  ,.rd_addr_0(1'b0)\n");
      first_entry = false;
      fout->append("  ,.rd_enable_0(1'b0)\n");
    }
    if (n_wr_ports == 0) {
      fout->append(first_entry ? "  .wr_addr_0(1'b0)\n" : "  ,.wr_addr_0(1'b0)\n");
      first_entry = false;
      fout->append("  ,.wr_enable_0(1'b0)\n");
      fout->append("  ,.wr_din_0(1'b0)\n");
    }

    fout->append(");\n");
  } else {  // array
    // Distinct storage name: a zero-write-port array (ROM) puts its dout on
    // driver pid 0, whose wire is named after the node — `iname` itself.
    const auto aname = absl::StrCat(iname, "_data");
    fout->append(absl::StrCat("reg [", mem_bits - 1, ":0] ", aname, "[", mem_size - 1, ":0];\n"));

    if (first_array_block) {
      fout->append("integer mem_loop_i;\n");
      first_array_block = false;
    }

    fout->append("always_comb begin\n");
    if (!mem_init_dpin.is_invalid()) {
      // Per-cycle default = the init contents (entry 0 in the low bits,
      // row-major); writes below override (forwarding semantics).
      const auto init_val = hydrate_const(mem_init_dpin);
      const auto mask     = Dlop::get_mask_value(mem_bits);
      for (int i = 0; i < mem_size; ++i) {
        auto entry = init_val.sra_op(*Dlop::create_integer(static_cast<int64_t>(i) * mem_bits))->and_op(*mask);
        fout->append(aname, "[", std::to_string(i), "] = ", const_to_verilog(*entry), ";\n");
      }
    } else {
      fout->append("for (mem_loop_i=0;mem_loop_i < ", std::to_string(mem_size), ";mem_loop_i = mem_loop_i + 1) begin\n");
      fout->append(aname, "[mem_loop_i] = 'b0;\n");
      fout->append("end\n");
    }

    // Writes first (array has forwarding semantics)
    for (auto& p : port_vector) {
      if (p.rdport) {
        continue;
      }
      if (p.addr.is_invalid() || p.din.is_invalid()) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("memory {} write port is not correctly configured", debug_name(node))
            .fatal();
      }
      auto din_name   = get_wire_or_const(p.din);
      auto write_stmt = absl::StrCat(aname, "[", get_wire_or_const(p.addr), "] = ", din_name, ";\n");
      if (p.enable.is_invalid()) {
        fout->append("  ", write_stmt);
      } else {
        fout->append("  if (", get_wire_or_const(p.enable), ") begin \n");
        fout->append("    ", write_stmt);
        fout->append("end\n");
      }
    }

    auto n_rd_pos = 0;
    for (auto& p : port_vector) {
      if (!p.rdport) {
        continue;
      }
      if (p.addr.is_invalid()) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("array {} read port is not correctly configured", debug_name(node))
            .fatal();
      }
      // Same dout convention as type 0/1: read port N drives pid (n_wr_ports + N).
      auto dout_dpin = node.create_driver_pin(static_cast<hhds::Port_id>(n_wr_ports + n_rd_pos));  // find-or-create
      auto dest_name = get_wire_or_const(dout_dpin);

      auto read_stmt = absl::StrCat(dest_name, " = ", aname, "[", get_wire_or_const(p.addr), "];\n");
      if (p.enable.is_invalid()) {
        fout->append("  ", read_stmt);
      } else {
        fout->append("  if (", get_wire_or_const(p.enable), ") begin \n");
        fout->append("    ", read_stmt);
        fout->append("end\n");
      }
      ++n_rd_pos;
    }

    fout->append("end\n");
  }
}

void Cgen_verilog::process_mux(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  note_src(fout, node);
  auto ordered_inp = node.inp_edges();
  sort_by_sink_pid(ordered_inp);
  I(ordered_inp.size() > 2);  // at least 0 + 1 + 2

  auto sel_expr    = get_expression(ordered_inp[0].driver);
  auto dpin_dest   = node.get_driver_pin(0);
  auto dest_var_it = pin2var.find(dpin_dest.get_class_index());
  I(dest_var_it != pin2var.end());
  auto dest_var = dest_var_it->second;

  auto mux2vec_it = mux2vector.find(node.get_class_index());
  if (mux2vec_it == mux2vector.end()) {
    if (ordered_inp.size() == 3) {  // if-else
      fout->append("   if (", sel_expr, ") begin\n");
      fout->append("     ", dest_var, " = ", get_expression(ordered_inp[2].driver), ";\n");
      fout->append("   end else begin\n");
      fout->append("     ", dest_var, " = ", get_expression(ordered_inp[1].driver), ";\n");
      fout->append("   end\n");
    } else {
      fout->append("   case (", sel_expr, ")\n");
      auto sel_bits = bits_of(ordered_inp[0].driver);
      for (auto i = 1u; i < ordered_inp.size(); ++i) {
        fout->append("     ", std::to_string(sel_bits), "'d", std::to_string(i - 1));
        fout->append(" : ", dest_var, " = ", get_expression(ordered_inp[i].driver), ";\n");
      }
      size_t num_cases = size_t{1} << sel_bits;
      if (num_cases > ordered_inp.size() - 1) {
        // The import path runs Yosys `setundef -zero` for deterministic RTL
        // semantics. Reintroducing X on sparse mux defaults makes regenerated
        // Verilog strictly less defined than the reference netlist and breaks
        // LEC for byte-enable update chains.
        fout->append("       default: ", dest_var, " = '0;\n");
      }
      fout->append("   endcase\n");
    }
  }
}

// Hotmux: one-hot selector (sink 0), values on p1..pN. Emitted as a case
// over the one-hot constants (arm i matches sel == 1<<i); a zero/multi-hot
// selector violates the unique-if assume and falls to the 'hx default.
void Cgen_verilog::process_hotmux(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  note_src(fout, node);
  auto ordered_inp = node.inp_edges();
  sort_by_sink_pid(ordered_inp);
  I(ordered_inp.size() > 2);  // selector + at least 2 values

  auto sel_expr    = get_expression(ordered_inp[0].driver);
  auto dpin_dest   = node.get_driver_pin(0);
  auto dest_var_it = pin2var.find(dpin_dest.get_class_index());
  I(dest_var_it != pin2var.end());
  auto dest_var = dest_var_it->second;

  const auto n_values = ordered_inp.size() - 1;
  auto       sel_bits = bits_of(ordered_inp[0].driver);
  if (sel_bits < static_cast<int32_t>(n_values)) {
    sel_bits = static_cast<int32_t>(n_values);  // missing/short bw: widen the labels to cover every arm
  }
  fout->append("   case (", sel_expr, ")\n");
  for (auto i = 1u; i < ordered_inp.size(); ++i) {
    // One-hot label as a binary literal ("1" then i-1 zeros) — no 64-arm cap.
    fout->append("     ", std::to_string(sel_bits), "'b1", std::string(i - 1, '0'));
    fout->append(" : ", dest_var, " = ", get_expression(ordered_inp[i].driver), ";\n");
  }
  fout->append("       default: ", dest_var, " = 'hx;\n");
  fout->append("   endcase\n");
}

std::string Cgen_verilog::build_simple_expr(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  auto dpin = node.get_driver_pin(0);
  auto op   = type_op_of(node);
  I(!Ntype::has_multiple_driver_pins(op));

  std::string final_expr;

  if (op == Ntype_op::Sum) {
    std::string add_seq;
    std::string sub_seq;
    for (auto e : node.inp_edges()) {
      if (e.sink.get_port_id() == 0) {
        add_seq = add_expression(add_seq, "+", e.driver);
      } else {
        sub_seq = add_expression(sub_seq, "+", e.driver);
      }
    }
    if (sub_seq.empty()) {
      final_expr = add_seq;
    } else if (add_seq.empty()) {
      final_expr = absl::StrCat(" -(", sub_seq, ")");
    } else {
      final_expr = absl::StrCat(add_seq, " - (", sub_seq, ")");
    }
  } else if (op == Ntype_op::Ror) {
    auto inp_edges = node.inp_edges();
    if (inp_edges.size() == 1) {
      auto expr  = get_expression(inp_edges[0].driver);
      final_expr = absl::StrCat("|", expr);
    } else {
      auto expr  = get_expression(inp_edges[0].driver);
      final_expr = absl::StrCat("|{", expr);
      for (auto i = 1u; i < inp_edges.size(); ++i) {
        final_expr = absl::StrCat(final_expr, " | ", get_expression(inp_edges[i].driver));
      }
      final_expr = absl::StrCat(final_expr, "}");
    }
  } else if (op == Ntype_op::Div) {
    auto lhs   = get_expression(get_driver(find_sink_pin(node, "a")));
    auto rhs   = get_expression(get_driver(find_sink_pin(node, "b")));
    final_expr = absl::StrCat(lhs, "/", rhs);
  } else if (op == Ntype_op::Not) {
    auto lhs_dpin = get_driver(find_sink_pin(node, "a"));
    auto lhs      = get_expression(lhs_dpin);
    auto var_pre  = pin2var.find(dpin.get_class_index());
    if (fout && var_pre != pin2var.end() && var_pre->second != lhs) {
      // Bitwise NOT is evaluated at the node output width. Assign through the
      // destination-width temporary first, so a narrow expression like addr[3:0]
      // becomes 5'b0_addr before ~ is applied.
      fout->append("  ", var_pre->second, " = ", lhs, ";\n");
      lhs = var_pre->second;
    }
    final_expr = absl::StrCat("~", lhs);
  } else if (op == Ntype_op::Set_mask) {
    auto a_dpin = get_driver(find_sink_pin(node, "a"));
    auto a      = get_expression(a_dpin);

    auto mask_dpin = get_driver(find_sink_pin(node, "mask"));
    I(is_const_pin(mask_dpin));
    auto mask_v = hydrate_const(mask_dpin);
    I(!mask_v.has_unknowns());

    if (mask_v.is_known_zero()) {
      final_expr = a;
    } else {
      auto [range_begin, range_end] = mask_v.get_mask_range();
      if (range_end > static_cast<int>(bits_of(dpin))) {
        range_end = bits_of(dpin) + range_begin;
      }

      auto a_bits = bits_of(a_dpin);

      auto value_dpin = get_driver(find_sink_pin(node, "value"));
      auto value      = get_expression(value_dpin);

      if (range_begin >= static_cast<int>(bits_of(dpin))) {
        // The write starts past the END of the result — nothing of it lands.
        // (Must be gated on the RESULT width, not the base `a` width: a write
        // above a NARROW base but still inside the result, e.g. `b#[0..=4]=…`
        // then `b#[12]=…`, leaves a zero gap and the insert in the high bits —
        // it must NOT be dropped. Comparing against `a_bits` silently dropped
        // every non-contiguous set.)
        final_expr = a;
      } else if (range_begin < 0 || range_end < 0) {
        std::string sel;
        for (auto i = 0; i < a_bits; ++i) {
          if (mask_v.and_op(*Dlop::create_integer(int64_t{1} << i))->is_known_false()) {
            if (sel.empty()) {
              sel = absl::StrCat(a, "[", i, "]");
            } else {
              sel = absl::StrCat(sel, ",", a, "[", i, "]");
            }
          } else {
            if (sel.empty()) {
              sel = absl::StrCat(value, "[", i, "]");
            } else {
              sel = absl::StrCat(sel, ",", value, "[", i, "]");
            }
          }
        }
        final_expr = absl::StrCat("{", sel, "}");
      } else {
        std::string a_replaced;
        int32_t     value_bits_to_use = static_cast<int32_t>(range_end - range_begin);
        if (value_bits_to_use >= bits_of(value_dpin)) {
          a_replaced = value;
        } else if (value_bits_to_use == 1) {
          a_replaced = absl::StrCat(value, "[0]");
        } else {
          a_replaced = absl::StrCat(value, "[", value_bits_to_use - 1, ":0]");
        }

        auto var_it = pin2var.find(dpin.get_class_index());
        assert(var_it != pin2var.end());
        if (value_bits_to_use < bits_of(dpin)) {
          if (fout && var_it->second != a) {
            note_src(fout, node);
            fout->append("  ", var_it->second, " = ", a, ";\n");
          }
        }
        std::string replace;
        if (value_bits_to_use == 1) {
          replace = absl::StrCat("[", range_begin, "] = ");
        } else {
          replace = absl::StrCat("[", range_end - 1, ":", range_begin, "] = ");
        }
        if (fout) {
          note_src(fout, node);
          fout->append("  ", var_it->second, replace, value, ";\n");
          return {};
        }
        return absl::StrCat("(", a, ")");  // Set_mask inlining is intentionally conservative.
      }
    }
  } else if (op == Ntype_op::Get_mask) {
    auto mask_dpin = get_driver(find_sink_pin(node, "mask"));
    I(is_const_pin(mask_dpin));
    auto mask_v = hydrate_const(mask_dpin);
    I(!mask_v.has_unknowns());

    auto a_dpin = get_driver(find_sink_pin(node, "a"));
    auto a_bits = bits_of(a_dpin);
    auto a      = get_expression(a_dpin);
    if (is_const_pin(a_dpin)) {
      // A bit-select of a CONSTANT operand: get_expression returns a parenthesized
      // literal `(N'sb1?...)`, and appending `[hi:lo]` produces invalid Verilog — a
      // part-select of a parenthesized constant, often out of range (slang rejects
      // it: "select expression is not allowed here"; the verification.html
      // InvalidSelectExpression / "lhd lec ERROR" category). The select is fully
      // determined at generation time, so apply the mask to the constant directly
      // and emit the resulting literal (the value cprop would have folded to).
      final_expr = const_to_verilog(*hydrate_const(a_dpin).get_mask_op(mask_v));
    } else if (mask_v.is_just_i64() && mask_v.to_just_i64() == -1) {
      if (a_bits > 0 && !is_unsign(a_dpin)) {
        // To-positive of a signed driver: a plain copy sign-extends when the
        // unsigned LHS is wider (e.g. 1-bit signed ~(|b) into a 2-bit reg).
        // AND with an unsigned mask of the driver's width so the expression
        // turns unsigned and zero-extends — get_mask(a,-1) == zext(a).
        std::string m;
        if (auto rem = a_bits % 4; rem != 0) {
          m += absl::StrCat((1 << rem) - 1);
        }
        m.append(a_bits / 4, 'f');
        final_expr = absl::StrCat("(", a, " & ", a_bits, "'h", m, ")");
      } else if (bits_of(dpin) > a_bits && a_bits > 0) {
        final_expr = absl::StrCat("{{", bits_of(dpin) - a_bits, "{1'b0}},", a, "}");
      } else if (bits_of(dpin) > 0 && a_bits > bits_of(dpin)) {
        final_expr = absl::StrCat(a, "[", bits_of(dpin) - 1, ":0]");
      } else {
        final_expr = a;
      }
    } else {
      auto [range_begin, range_end] = mask_v.get_mask_range();
      int32_t a_bits_to_use         = static_cast<int32_t>(range_end - range_begin);
      if (a_bits_to_use > bits_of(dpin)) {
        range_end = bits_of(dpin) + range_begin;
      }

      int out_bits = bits_of(dpin);
      if (is_unsign(dpin)) {
        --out_bits;
      }

      if (range_begin < 0 || range_end < 0) {
        std::string sel;
        auto        max_bits = std::max(mask_v.get_bits(), a_bits);
        for (auto i = 0; i < max_bits; ++i) {
          if (mask_v.and_op(*Dlop::create_integer(int64_t{1} << i))->is_known_false()) {
            continue;
          }
          if (sel.empty()) {
            sel = absl::StrCat(a, "[", i, "]");
          } else {
            sel = absl::StrCat(sel, ",", a, "[", i, "]");
          }
        }
        final_expr = absl::StrCat("{", sel, "}");
        // a_bits == 0 means the driver width is unknown (no bits attr): the
        // sign-replicate / extend forms below would fabricate a[-1]. Fall
        // through to the width-agnostic part-select forms instead.
      } else if (a_bits > 0 && range_begin >= static_cast<int>(a_bits)) {
        final_expr = absl::StrCat("{", range_end - range_begin, "{", a, "[", a_bits - 1, "]}}");
      } else if (a_bits > 0 && range_end > static_cast<int>(a_bits) && range_begin == 0) {
        // Pure widening: let the assignment context extend the bare net per its
        // declared signedness (sign-extend a signed net, zero-extend unsigned).
        // Indexing a[a_bits-1] here would overflow a net declared narrower than
        // bits_of — an unsigned driver drops its always-0 sign bit at declare
        // (reg [bits-2:0]), so a[bits-1] reads as undef (X) in yosys.
        final_expr = a;
      } else if (a_bits > 0 && range_end > static_cast<int>(a_bits)) {
        auto top   = absl::StrCat("{{", range_end - a_bits, "{", a, "[", a_bits - 1, "]}}");
        final_expr = absl::StrCat(top, ",", a, "[", a_bits - 1, ":", range_begin, "]}");
      } else if (range_begin == 0 && range_end >= out_bits) {
        final_expr = a;
      } else if (a_bits_to_use == 1) {
        final_expr = absl::StrCat(a, "[", range_begin, "]");
      } else {
        final_expr = absl::StrCat(a, "[", range_end - 1, ":", range_begin, "]");
      }
    }
  } else if (op == Ntype_op::Sext) {
    auto lhs      = get_expression(get_driver(find_sink_pin(node, "a")));
    auto pos_dpin = get_driver(find_sink_pin(node, "b"));
    auto pos_node = pos_dpin.is_invalid() ? hhds::Node_class{} : pos_dpin.get_master_node();
    if (!pos_node.is_invalid() && is_type_const(pos_node)) {
      auto lpos = hydrate_const(pos_dpin);
      if (lpos.is_just_i64()) {
        final_expr = absl::StrCat(lhs, "[", lpos.to_just_i64() - 1, ":0]");
      }
    }
    if (final_expr.empty()) {
      auto bits     = bits_of(pos_dpin);
      auto pos_expr = get_expression(pos_dpin);
      final_expr    = absl::StrCat(lhs, "& ((1'sh", bits, " << ", pos_expr, ")-1)");
    }
  } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
    std::vector<std::string> lhs;
    std::vector<std::string> rhs;
    bool                     signed_compare = !is_unsign(dpin);
    auto                     cmp_expr       = [&](hhds::Pin_class cmp_dpin) {
      if (signed_compare && !cmp_dpin.is_invalid() && !is_const_pin(cmp_dpin)) {
        auto cmp_node = cmp_dpin.get_master_node();
        if (type_op_of(cmp_node) == Ntype_op::Get_mask) {
          auto a_dpin    = get_driver(find_sink_pin(cmp_node, "a"));
          auto mask_dpin = get_driver(find_sink_pin(cmp_node, "mask"));
          if (!a_dpin.is_invalid() && !mask_dpin.is_invalid() && is_const_pin(mask_dpin) && !is_unsign(a_dpin)) {
            auto mask_v  = hydrate_const(mask_dpin);
            auto out_w   = bits_of(cmp_dpin);
            auto a_w     = bits_of(a_dpin);
            bool all_one = mask_v.is_just_i64() && mask_v.to_just_i64() == -1;
            if (!all_one && mask_v.is_just_i64() && out_w > 0 && out_w <= 62) {
              all_one = mask_v.to_just_i64() == ((int64_t{1} << out_w) - 1);
            }
            if (!all_one && mask_v.is_just_i64() && a_w > 0 && a_w <= 62) {
              all_one = mask_v.to_just_i64() == ((int64_t{1} << a_w) - 1);
            }

            // get_unsigned_dpin() can leave a Get_mask(a,-1) wrapper around a
            // signed value. For signed comparisons, that wrapper would
            // zero-extend the value and break guards such as signed(~addr) < 0.
            // Real zero-extensions over unsigned RTLIL wires are protected by
            // the imported pin signedness: !is_unsign(a_dpin) is false.
            if (all_one && a_w > 0 && out_w > 0 && out_w >= a_w) {
              cmp_dpin = a_dpin;
            }
          }
        }
      }

      auto expr = get_expression(cmp_dpin);
      if (signed_compare) {
        return absl::StrCat("$signed(", expr, ")");
      }
      return expr;
    };
    for (const auto& e : node.inp_edges()) {
      if (Ntype::get_sink_name(op, e.sink.get_port_id()) == "as") {
        lhs.emplace_back(cmp_expr(e.driver));
      } else {
        rhs.emplace_back(cmp_expr(e.driver));
      }
    }
    std::string cmp = (op == Ntype_op::GT) ? " > " : " < ";
    for (const auto& l : lhs) {
      for (const auto& r : rhs) {
        if (final_expr.empty()) {
          final_expr = absl::StrCat(l, cmp, r);
        } else {
          final_expr = absl::StrCat(final_expr, " && ", l, cmp, r);
        }
      }
    }
  } else if (op == Ntype_op::SHL) {
    auto val_dpin = get_driver(find_sink_pin(node, "a"));
    auto val_expr = get_expression(val_dpin);

    // Verilog `a << b` is self-determined by `a`'s width: a narrow left operand
    // (e.g. a 1-bit const `1`, or a 1-bit signal) shifts WITHIN that narrow
    // width and loses the high bits before the surrounding context (`~`, `&`)
    // can widen it. That silently corrupts a dynamic bit-write RMW mask
    // (`~(1<<idx)` for `data[idx]=…` zero-extends to all-but-low-bits instead
    // of all-but-bit-idx). Pad the left operand to this node's full inferred
    // width (and force unsigned) so the shift happens at the correct width.
    //
    // Widen via this CONTEXT-determined OR, NOT a concatenation `{{N{1'b0}},val}`.
    // A concat makes `val` self-determined, so an inlined arithmetic operand (a
    // single-fanout `a+b` folded inline) would be evaluated at its own narrow
    // operand width and WRAP before the zero-pad — e.g. `(in1+in2+7) << 1` would
    // truncate the sum to 8 bits. The OR's context width propagates into the add.
    auto        obits    = bits_of(node.get_driver_pin(0));
    std::string wide_val = absl::StrCat("({", std::to_string(obits), "{1'b0}} | ", val_expr, ")");

    // SHL b is single-driver (the one-hot multi-shift `(n<<b0)|(n<<b1)` form
    // was removed).
    auto amt_expr = get_expression(get_driver(find_sink_pin(node, "b")));
    final_expr    = absl::StrCat("(", wide_val, " << ", amt_expr, ")");
  } else if (op == Ntype_op::SRA) {
    auto a_dpin   = get_driver(find_sink_pin(node, "a"));
    auto val_expr = get_expression(a_dpin);
    auto amt_expr = get_expression(get_driver(find_sink_pin(node, "b")));
    // `>>>` is an *arithmetic* (sign-filling) shift only when its left operand
    // is signed IN THE EVALUATION CONTEXT. Verilog makes the whole enclosing
    // expression unsigned if ANY operand is unsigned (e.g. the deliberate SHL
    // zero-extend idiom `({N{1'b0}} | a)`), and that unsigned context
    // propagates DOWN into a context-determined `a >>> amt`, silently turning
    // it into a logical (zero-fill) shift — wrong for negative `a`. A bare
    // `$signed(a) >>> amt` does NOT survive: the shift's left operand is still
    // context-determined, so the enclosing unsigned context wins (verified with
    // iverilog). The fix is to isolate the whole shift in its own SELF-
    // determined signed context — the argument of `$signed(...)` is self-
    // determined — so the sign fill happens at the operand's natural width
    // regardless of how the result is later used. The inner `$signed(val)`
    // forces the left operand signed even when `val`'s text would otherwise read
    // unsigned. Only do this for a genuinely signed operand: `$signed`-wrapping
    // an unsigned value would sign-extend a value that should zero-fill.
    if (!sra_operand_signed(a_dpin)) {
      final_expr = absl::StrCat(val_expr, " >>> ", amt_expr);
    } else {
      // A nested SRA's operand also takes this branch (its inner shift already
      // emitted self-contained signed text), so the outer `>>>` is isolated too
      // and the enclosing unsigned context cannot demote it to a logical shift.
      final_expr = absl::StrCat("$signed($signed(", val_expr, ") >>> ", amt_expr, ")");
    }
  } else if (op == Ntype_op::Nconst) {
    return {};  // emitted as expr at create_locals time
  } else if (op == Ntype_op::AttrSet) {
    return {};  // drop
  } else {
    std::string txt_op;
    if (op == Ntype_op::Mult) {
      txt_op = "*";
    } else if (op == Ntype_op::And) {
      txt_op = "&";
    } else if (op == Ntype_op::Or) {
      txt_op = "|";
    } else if (op == Ntype_op::Xor) {
      txt_op = "^";
    } else if (op == Ntype_op::EQ) {
      txt_op = "==";
    }
    I(!txt_op.empty());

    for (auto e : node.inp_edges()) {
      final_expr = add_expression(final_expr, txt_op, e.driver);
    }
  }

  if (final_expr.empty()) {
    if (op == Ntype_op::Sum || op == Ntype_op::Or || op == Ntype_op::Xor) {
      // Empty variadic nodes can be left behind by cprop/bitwidth on paths
      // where all operands folded away. The Isabelle exporter interprets
      // empty Sum/Or/Xor as the neutral zero value, so Cgen must do the same
      // for RTL/LGraph LEC.
      final_expr = "'0";
    } else {
      Pass::info("likely issue in node:{} that has no compute value", debug_name(node));
      final_expr = "'hx";
    }
  }

  if (has_color(node)) {
    absl::StrAppend(&final_expr, " /* color:", std::to_string(color_of(node)), "*/");
  }

  return final_expr;
}

void Cgen_verilog::process_simple_node(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  auto dpin       = node.get_driver_pin(0);
  auto final_expr = build_simple_expr(fout, node);
  if (final_expr.empty()) {
    return;
  }

  auto var_it = pin2var.find(dpin.get_class_index());
  if (var_it == pin2var.end()) {
    pin2expr.emplace(dpin.get_class_index(), Expr(final_expr, true));
  } else if (var_it->second != final_expr) {
    note_src(fout, node);
    fout->append("  ", var_it->second, " = ", final_expr, ";\n");
  }
}

void Cgen_verilog::reserve_instance_names(hhds::Graph* graph) {
  for (auto node : graph->fast_class()) {
    auto op = type_op_of(node);
    if (op != Ntype_op::Sub && op != Ntype_op::Memory) {
      continue;
    }
    auto iname = get_scaped_name(default_instance_name(node));
    declared_name_counts.insert({iname, 1});
  }
}

void Cgen_verilog::create_module_io(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  auto gio = graph->get_io();
  I(gio);

  // Combine input + output decls and sort by port_id for a deterministic
  // module-header declaration order. Ports are emitted by name, so the order is
  // purely textual (stable diffs); correctness does not depend on it.
  struct IoEntry {
    std::string name;
    uint32_t    bits;
    bool        is_input;
    uint32_t    port_id;
  };
  std::vector<IoEntry> entries;
  for (const auto& d : gio->get_input_pin_decls()) {
    entries.push_back({d.name, d.bits, true, static_cast<uint32_t>(d.port_id)});
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    entries.push_back({d.name, d.bits, false, static_cast<uint32_t>(d.port_id)});
  }
  std::sort(entries.begin(), entries.end(), [](const IoEntry& a, const IoEntry& b) { return a.port_id < b.port_id; });

  bool first_arg = true;
  for (const auto& e : entries) {
    note_module(fout);
    if (!first_arg) {
      fout->append("  ,");
    } else {
      fout->append("   ");
    }
    first_arg = false;

    if (e.is_input) {
      fout->append("input signed ");
    } else {
      fout->append("output reg signed ");
    }

    const auto name = get_scaped_name(e.name);

    // Prefer the concrete HHDS pin width when present. Some imported GraphIO
    // declarations can retain stale placeholder widths, while the graph pin
    // has already been fixed by bitwidth propagation.
    hhds::Pin_class pin  = e.is_input ? graph->get_input_pin(e.name) : graph->get_output_pin(e.name);
    const auto      bits = pin.is_invalid() ? e.bits : livehd::graph_util::bits_of(pin, *gio, e.name);

    if (bits > 1) {
      fout->append("[", std::to_string(bits - 1), ":0] ", name, "\n");
    } else {
      fout->append(name, "\n");
    }

    // Map the corresponding HHDS pin (driver for inputs, sink for outputs) into pin2var.
    if (!pin.is_invalid()) {
      pin2var.emplace(pin.get_class_index(), name);
    }
  }

  note_module(fout);
  fout->append(");\n");
}

void Cgen_verilog::create_memories(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->fast_class()) {
    if (type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    process_memory(fout, node);
  }
}

void Cgen_verilog::create_subs(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->fast_class()) {
    if (!is_type_sub(node)) {
      continue;
    }

    auto sub_io = node.get_subnode_io();
    if (!sub_io) {
      continue;
    }

    // Runtime range-select guard (`a#[lo..=hi]`): an `lgassert` Sub is a
    // recognized primitive, NOT a real sub-graph. Lower it to an inline
    // SystemVerilog immediate assertion on its `cond` input. It drives no data
    // output, so the equivalence check (which compares module outputs) is
    // unaffected; the instance-name attr carries the file:line for the message.
    // Wrapped in `synthesis translate_off`/`on` so synthesis + the yosys LEC
    // reader skip the simulation-only `assert … else $error` action block
    // (yosys cannot parse the `else`), while RTL simulators keep it live.
    if (sub_io->get_name() == livehd::graph_util::lgassert_module_name) {
      auto cond = get_driver(node.get_sink_pin("cond"));
      if (cond.is_invalid()) {
        continue;
      }
      std::string loc;
      if (auto nm = node.attr(hhds::attrs::name); nm.has()) {
        loc = std::string{nm.get()};
      }
      note_src(fout, node);
      fout->append("// synthesis translate_off\n");
      fout->append("always_comb begin\n");
      fout->append("  assert (",
                   get_wire_or_const(cond),
                   ") else $error(\"lgassert: descending bit-range select (hi < lo)",
                   loc.empty() ? std::string{} : absl::StrCat(" at ", loc),
                   "\");\n");
      fout->append("end\n");
      fout->append("// synthesis translate_on\n");
      continue;
    }

    // User property materialized by pass.formal/tolg (`fproperty`): a recognized
    // primitive carrying a 1-bit cond and a packed "<kind>\x1f<loc>\x1f<msg>" name
    // attr. Skip emission when pass.formal proved it (no runtime check needed);
    // otherwise emit an immediate assert/assume in synthesis-off (LEC-invisible).
    if (sub_io->get_name() == livehd::graph_util::fproperty_module_name) {
      if (livehd::graph_util::proven_of(node) != 0) {
        continue;  // pass.formal discharged it -> elide the runtime check
      }
      auto cond = get_driver(node.get_sink_pin("cond"));
      if (cond.is_invalid()) {
        continue;
      }
      std::string kind = "assert";
      std::string loc;
      std::string msg;
      if (auto nm = node.attr(hhds::attrs::name); nm.has()) {
        std::string packed{nm.get()};
        if (auto p1 = packed.find('\x1f'); p1 != std::string::npos) {
          kind    = packed.substr(0, p1);
          auto p2 = packed.find('\x1f', p1 + 1);
          if (p2 == std::string::npos) {
            loc = packed.substr(p1 + 1);
          } else {
            loc = packed.substr(p1 + 1, p2 - p1 - 1);
            msg = packed.substr(p2 + 1);
          }
        }
      }
      // Sanitize the user message for a Verilog string literal.
      for (auto& ch : msg) {
        if (ch == '"' || ch == '\\' || ch == '\n' || ch == '\r') {
          ch = ' ';
        }
      }
      std::string detail = kind;
      if (!loc.empty()) {
        detail += absl::StrCat(" at ", loc);
      }
      if (!msg.empty()) {
        detail += absl::StrCat(": ", msg);
      }
      note_src(fout, node);
      fout->append("// synthesis translate_off\n");
      fout->append("always_comb begin\n");
      if (kind == "assume") {
        fout->append("  assume (", get_wire_or_const(cond), ");\n");
      } else {
        fout->append("  assert (", get_wire_or_const(cond), ") else $error(\"", detail, "\");\n");
      }
      fout->append("end\n");
      fout->append("// synthesis translate_on\n");
      continue;
    }

    auto iname = get_scaped_name(default_instance_name(node));

    note_src(fout, node);
    fout->append(get_scaped_name(sub_io->get_name()), " ", iname, "(\n");

    bool first_entry = true;

    // Order pins by port_id for a deterministic instance-connection order. The
    // connections are named (.name(sig)), so this only fixes the textual order.
    struct SortedPin {
      const hhds::GraphIO::DeclaredIoPin* decl;
      bool                                is_input;
    };
    std::vector<SortedPin> ordered;
    for (const auto& d : sub_io->get_input_pin_decls()) {
      ordered.push_back({&d, true});
    }
    for (const auto& d : sub_io->get_output_pin_decls()) {
      ordered.push_back({&d, false});
    }
    std::sort(ordered.begin(), ordered.end(), [](const SortedPin& a, const SortedPin& b) {
      return a.decl->port_id < b.decl->port_id;
    });

    for (const auto& io : ordered) {
      hhds::Pin_class dpin;
      if (io.is_input) {
        // node's sink pin named io.decl->name → driver via inp_edges
        auto spin = node.get_sink_pin(io.decl->name);
        dpin      = get_driver(spin);
      } else {
        // node's driver pin named io.decl->name → emit only if has consumers
        auto candidate = node.get_driver_pin(io.decl->name);
        if (!candidate.is_invalid() && !candidate.out_edges().empty()) {
          dpin = candidate;
        }
      }
      if (!dpin.is_invalid()) {
        note_src(fout, node);
        fout->append(absl::StrCat(first_entry ? "" : ",", ".", io.decl->name, "(", get_wire_or_const(dpin), ")\n"));
        first_entry = false;
      }
    }

    note_src(fout, node);
    fout->append(");\n");
  }
}

void Cgen_verilog::create_combinational(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  note_module(fout);
  fout->append("always_comb begin\n");

  for (auto node : graph->forward_class()) {
    auto op = type_op_of(node);
    if (Ntype::has_multiple_driver_pins(op)) {
      continue;
    }
    // is_type_register excludes Flop/Fflop/Latch/Memory from combinational
    // expression emission (Memory is already handled by has_multiple_driver_pins above);
    // a Latch is emitted as a level-sensitive block in create_registers.
    if (!node.has_out_edges() || is_type_register(node)) {
      continue;
    }
    if (bits_of(node.get_driver_pin(0)) == 0) {
      if (op != Ntype_op::Nconst && op != Ntype_op::AttrSet && op != Ntype_op::Mux && op != Ntype_op::Hotmux) {
        // missing bits; was a hard error in the original — skip silent.
      }
    }
    if (op == Ntype_op::Mux) {
      process_mux(fout, node);
    } else if (op == Ntype_op::Hotmux) {
      process_hotmux(fout, node);
    } else {
      process_simple_node(fout, node);
    }
  }

  note_module(fout);
  fout->append("end\n");
}

void Cgen_verilog::create_outputs(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  note_module(fout);
  fout->append("always_comb begin\n");
  auto gio = graph->get_io();
  I(gio);
  for (const auto& d : gio->get_output_pin_decls()) {
    auto spin = graph->get_output_pin(d.name);
    if (spin.is_invalid()) {
      continue;
    }
    auto out_dpin = get_driver(spin);
    if (out_dpin.is_invalid()) {
      continue;
    }
    auto name = get_scaped_name(d.name);
    auto expr = get_expression(out_dpin);
    if (name != expr) {
      // An inlined expression's statement line lands here — anchor
      // the output assignment at its driver cell's source.
      note_src(fout, out_dpin.get_master_node());
      fout->append("  ", name, " = ", expr, ";\n");
    }
  }
  for (auto node : graph->fast_class()) {
    if (is_type_flop(node)) {
      process_flop(fout, node);
    }
  }
  note_module(fout);
  fout->append("end\n");
}

void Cgen_verilog::create_registers(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->fast_class()) {
    if (type_op_of(node) == Ntype_op::Latch) {
      process_latch(fout, node);
      continue;
    }
    if (!is_type_flop(node)) {
      continue;
    }

    auto        dpin      = node.get_driver_pin(0);
    std::string name      = get_wire_or_const(dpin);
    const auto  name_next = get_append_to_name(name, "___next_");

    std::string edge        = "posedge";
    auto        posclk_sink = find_sink_pin(node, "posclk");
    auto        posclk_dpin = get_driver(posclk_sink);
    if (!posclk_dpin.is_invalid()) {
      auto v = !hydrate_const(posclk_dpin).is_known_false();
      if (!v) {
        edge = "negedge";
      }
    }
    auto        clock_sink = find_sink_pin(node, "clock_pin");
    // Use get_expression (not pin_wire_name directly): an internal/derived clock
    // (a gated/buffered clock feeding the flop's clock_pin) may be either a
    // DECLARED net — e.g. a `Get_mask` masking `clk & en` to 1 bit, whose wire
    // name carries cgen's `_u` suffix recorded in pin2var (pin_wire_name returns
    // the bare name, so `always @(posedge get_mask_16)` would miss the real
    // `get_mask_16_u` net) — or a single-fanout node INLINED as an expression
    // (a boolean `clk_b & gate` with no other consumer is never declared as a
    // wire, so its bare name `and_28` is undeclared). get_expression resolves
    // both (pin2var net name, else the pin2expr inline expression), giving
    // `always @(posedge (clk_b & gate))`; a module-input clock falls through to
    // its input name as before.
    std::string clock      = get_expression(get_driver(clock_sink));

    std::string reset_async;
    std::string reset;
    bool        negreset = false;

    auto reset_sink = find_sink_pin(node, "reset_pin");
    auto reset_dpin = get_driver(reset_sink);
    if (!reset_dpin.is_invalid()) {
      if (is_const_pin(reset_dpin)) {
        auto reset_const = hydrate_const(reset_dpin);
        if (!reset_const.is_known_false() && !reset_const.same_repr(*Dlop::from_string("false"))) {
          reset = const_to_verilog(reset_const);
        }
      } else {
        reset = get_wire_or_const(reset_dpin);

        // A reset is a 1-bit boolean: its level test and (especially) any async
        // edge event must be on a single bit. A DERIVED reset driver (e.g. a
        // scan-bypass mux `scanmode ? scan_reset_n : rst_ni`) can carry an
        // inferred-wider width whose upper bits are always 0; yosys rejects a
        // posedge/negedge event on a multi-bit net. Narrow to bit 0 (exact for
        // a boolean reset, and consistent between the edge and the level test).
        if (bits_of(reset_dpin) > 1) {
          reset = absl::StrCat(reset, "[0]");
        }

        auto negreset_dpin = get_driver(find_sink_pin(node, "negreset"));
        if (!negreset_dpin.is_invalid()) {
          negreset = !hydrate_const(negreset_dpin).is_known_false();
        }
        auto async_dpin = get_driver(find_sink_pin(node, "async"));
        if (!async_dpin.is_invalid()) {
          auto v = !hydrate_const(async_dpin).is_known_false();
          if (v) {
            reset_async = absl::StrCat(negreset ? " or negedge " : " or posedge ", reset);
          }
        }
      }
    }

    std::string reset_initial = "'h0";
    auto        initial_dpin  = get_driver(find_sink_pin(node, "initial"));
    if (!initial_dpin.is_invalid()) {
      // Value context: use get_expression (not get_wire_or_const) so a computed,
      // single-fanout reset/initial driver that was inlined into pin2expr is
      // emitted inline. get_wire_or_const ignores pin2expr and would return a
      // bare, undriven wire name (X). Matches how the flop's din is referenced.
      reset_initial = get_expression(initial_dpin);
    }

    // Pipeline depth: the pipe_min/pipe_max comptime pins make one
    // Flop cell model a whole depth-d shift register. Unset pins => depth 1
    // (today's single flop, bit-for-bit). For a ranged depth (min<max) the
    // realization default at Verilog emission is the declared MINIMUM (the
    // LG pass2 knob picks differently later); pipe_max is for the checker
    // and the slop simulation, never read here.
    int64_t depth = 1;
    {
      auto pm_dpin = get_driver(find_sink_pin(node, "pipe_min"));
      if (!pm_dpin.is_invalid() && is_const_pin(pm_dpin)) {
        depth = hydrate_const(pm_dpin).to_just_i64();
      }
    }

    // Write-enable: a conditionally-written state register holds
    // its value when the OR-of-write-conditions is false (no din=q feedback
    // mux is ever inserted — the enable IS the hold).
    std::string enable;
    {
      auto enable_dpin = get_driver(find_sink_pin(node, "enable"));
      if (!enable_dpin.is_invalid() && !is_const_pin(enable_dpin)) {
        enable = get_wire_or_const(enable_dpin);
      }
    }

    // Anchor the whole always block at the reg's source site.
    note_src(fout, node);

    if (depth <= 1) {
      // Depth 1 (or unset): today's single-flop emission, plus the optional
      // enable gate.
      fout->append("always @(", edge, " ", clock, reset_async, " ) begin\n");

      const std::string update = enable.empty() ? absl::StrCat(name, " <= ", name_next, ";\n")
                                                : absl::StrCat("if (", enable, ") begin\n", name, " <= ", name_next, ";\nend\n");
      if (reset.empty()) {
        fout->append(update);
      } else {
        if (negreset) {
          fout->append("if (!", reset, ") begin\n");
        } else {
          fout->append("if (", reset, ") begin\n");
        }
        fout->append(name, " <= ", reset_initial, ";\n");
        fout->append("end else begin\n");
        fout->append(update);
        fout->append("end\n");
      }

      fout->append("end\n");
      continue;
    }

    // depth >= 2: declare the d-1 intermediate stage regs (q itself is the
    // last stage) and emit one clocked block chaining them. Every stage
    // replicates the SAME clock/reset configuration — the inserted-flop
    // contract forbids inventing a different reset style per stage.
    int  bits    = bits_of(dpin);
    bool out_uns = is_unsign(dpin);
    if (out_uns && type_op_of(dpin.get_master_node()) == Ntype_op::Get_mask) {
      --bits;
    }
    std::vector<std::string> stage_names;
    stage_names.reserve(static_cast<size_t>(depth) - 1);
    for (int64_t i = 0; i < depth - 1; ++i) {
      auto sname = get_append_to_name(name, absl::StrCat("___pipe", i, "_"));
      if (bits <= 1) {
        fout->append(out_uns ? "reg " : "reg signed ", sname, ";\n");
      } else {
        fout->append(out_uns ? "reg " : "reg signed ", "[", std::to_string(bits - 1), ":0] ", sname, ";\n");
      }
      stage_names.emplace_back(std::move(sname));
    }

    fout->append("always @(", edge, " ", clock, reset_async, " ) begin\n");

    auto emit_chain = [&]() {
      fout->append(stage_names.front(), " <= ", name_next, ";\n");
      for (size_t i = 1; i < stage_names.size(); ++i) {
        fout->append(stage_names[i], " <= ", stage_names[i - 1], ";\n");
      }
      fout->append(name, " <= ", stage_names.back(), ";\n");
    };
    auto emit_enabled = [&]() {
      if (enable.empty()) {
        emit_chain();
      } else {
        fout->append("if (", enable, ") begin\n");
        emit_chain();
        fout->append("end\n");
      }
    };

    if (reset.empty()) {
      emit_enabled();
    } else {
      if (negreset) {
        fout->append("if (!", reset, ") begin\n");
      } else {
        fout->append("if (", reset, ") begin\n");
      }
      for (const auto& sname : stage_names) {
        fout->append(sname, " <= ", reset_initial, ";\n");
      }
      fout->append(name, " <= ", reset_initial, ";\n");
      fout->append("end else begin\n");
      emit_enabled();
      fout->append("end\n");
    }

    fout->append("end\n");
  }
}

void Cgen_verilog::add_to_pin2var(std::shared_ptr<File_output> fout, const hhds::Pin_class& dpin, std::string_view name,
                                  bool out_unsigned) {
  if (is_const_pin(dpin)) {
    return;
  }

  if (pin2var.contains(dpin.get_class_index())) {
    return;
  }
  auto unique_name = get_unique_decl_name(name);
  pin2var.insert({dpin.get_class_index(), unique_name});
  name = unique_name;

  // Anchor the wire declaration line at its defining cell.
  note_src(fout, dpin.get_master_node());

  int bits = bits_of(dpin);

  std::string reg_str;
  if (out_unsigned) {
    reg_str = "reg ";
    if (type_op_of(dpin.get_master_node()) == Ntype_op::Get_mask) {
      --bits;
    }
  } else {
    reg_str = "reg signed ";
  }

  // A combinational driver that feeds a module output directly is named after
  // that output port, which create_module_io already declared as `output reg
  // <name>`. Emitting a body `reg <name>` here re-declares it (a Verilog
  // compile error). Keep the pin2var mapping (process_mux / consumers resolve
  // the name; an `output reg` is itself readable) but skip the duplicate
  // declaration. (The Sub/Memory output path instead renames to a dedicated
  // net; a simple node keeps the port name and assigns it in place.)
  bool redeclares_output = false;
  if (!dpin.is_invalid()) {
    for (const auto& e : dpin.out_edges()) {
      if (is_graph_output_pin(e.sink) && get_scaped_name(pin_wire_name(e.sink)) == name) {
        redeclares_output = true;
        break;
      }
    }
  }

  if (!redeclares_output) {
    if (bits <= 1) {
      fout->append(reg_str, name, ";\n");
    } else {
      fout->append(reg_str, "[", std::to_string(bits - 1), ":0] ", name, ";\n");
    }
  }

  if (!dpin.is_invalid() && is_type_flop(dpin.get_master_node())) {
    auto name_next = get_append_to_name(name, "___next_");
    note_src(fout, dpin.get_master_node());
    if (bits <= 1) {
      fout->append(reg_str, name_next, ";\n");
    } else {
      fout->append(reg_str, "[", std::to_string(bits - 1), ":0] ", name_next, ";\n");
    }
  }
}

void Cgen_verilog::create_locals(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->fast_class()) {
    auto op = type_op_of(node);

    if (Ntype::has_multiple_driver_pins(op)) {
      if (op == Ntype_op::Sub || op == Ntype_op::Memory) {
        for (auto& e : node.inp_edges()) {
          auto name2 = get_scaped_name(pin_wire_name(e.driver));
          add_to_pin2var(fout, e.driver, name2, is_unsign(e.driver));
        }
        if (op == Ntype_op::Memory) {
          // Instance outputs must land on a dedicated net: the dout pin is
          // usually named after the module output it drives (e.g. "q0"), so
          // reusing that name re-declares the port (and an instance output
          // cannot legally drive an `output reg` anyway). create_outputs
          // then emits `q0 = <iname>_dout_<pid>;` like any other driver.
          //
          // Iterate out_edges (not out_pins): out_pins misses driver pid 0
          // (a zero-write-port ROM's dout) and its handles encode pins
          // WITHOUT the driver bit, so their class_index never matches
          // edge.driver / create_driver_pin handles. Re-fetch the canonical
          // driver handle for keying; pin2var insert dedups repeat pids.
          //
          // type==2 (array) douts are procedurally assigned in process_memory's
          // always_comb, so they must be `reg`; type 0/1 douts connect to the
          // cgen_memory_* instance ports and must stay nets.
          bool is_array_mem = false;
          for (auto& e2 : node.inp_edges()) {
            if (e2.sink.get_port_id() == 7 && is_const_pin(e2.driver)) {  // pid 7 = "type" (comptime x 1)
              is_array_mem = hydrate_const(e2.driver).to_just_i64() == 2;
              break;
            }
          }
          for (const auto& e2 : node.out_edges()) {
            auto dout            = node.create_driver_pin(e2.driver.get_port_id());
            // Escape the FULL derived name as one unit: a memory instance name
            // can carry verilog-special chars (e.g. the '.' of a flattened
            // hierarchical name), so escaping iname first and then appending
            // "_dout_N" would drop the suffix past the escaped id's terminating
            // space (`\u_fifo.mem _dout_1`), which yosys cannot parse.
            auto name2           = get_scaped_name(absl::StrCat(default_instance_name(node), "_dout_", e2.driver.get_port_id()));
            auto [it2, inserted] = pin2var.insert({dout.get_class_index(), name2});
            if (inserted) {
              int bits2 = bits_of(dout);
              if (bits2 <= 1) {
                fout->append(is_array_mem ? "reg signed " : "wire signed ", name2, ";\n");
              } else {
                fout->append(is_array_mem ? "reg signed [" : "wire signed [", std::to_string(bits2 - 1), ":0] ", name2, ";\n");
              }
            }
          }
          continue;
        }
        for (auto& dpin2 : node.out_pins()) {
          if (dpin2.out_edges().empty()) {
            continue;
          }
          // Re-fetch the canonical driver handle (driver bit set) so this keys
          // pin2var identically to the edge.driver a consumer's inp_edges loop
          // uses — otherwise the same instance-output net is declared twice
          // (once here, once by the consumer) and lookups miss this entry.
          auto cdpin           = node.create_driver_pin(dpin2.get_port_id());
          // Use a DEDICATED net name (like the Memory dout above), never the wire
          // name: a Sub output that drives a module output directly is otherwise
          // named after that port, and declaring it here re-declares the port
          // (illegal — `Incompatible re-declaration of wire`; also an instance
          // output cannot legally drive an `output reg`). create_outputs then
          // emits `<port> = <iname>_o<pid>;` like any other driver.
          auto name2           = get_scaped_name(absl::StrCat(default_instance_name(node), "_o", dpin2.get_port_id()));
          auto [it2, inserted] = pin2var.insert({cdpin.get_class_index(), name2});
          if (inserted) {
            int bits2 = bits_of(cdpin);
            if (bits2 <= 1) {
              fout->append("wire signed ", name2, ";\n");
            } else {
              fout->append("wire signed [", std::to_string(bits2 - 1), ":0] ", name2, ";\n");
            }
          }
        }
      }
      continue;
    }
    I(op != Ntype_op::Sub && op != Ntype_op::Memory);

    if (!node.has_out_edges() && !is_type_flop(node)) {
      continue;
    }
    // A flop whose Q has no readers is still emitted by create_registers (it always
    // assigns `q <= ___next_q`), so it MUST get its `reg` declaration here too —
    // otherwise the netlist references an undeclared name (e.g. a_exmem_valid).

    auto        dpin         = node.get_driver_pin(0);
    std::string name         = get_scaped_name(pin_wire_name(dpin));
    bool        out_unsigned = is_unsign(dpin);

    if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
      // (large-mux vector path disabled in the original; preserve.)
      // Both always get a declared dest var — process_mux/process_hotmux
      // assign it from inside the always_comb.
    } else if (op == Ntype_op::Sext) {
      auto b_dpin = get_driver(find_sink_pin(node, "b"));
      if (!b_dpin.is_invalid() && is_const_pin(b_dpin)) {
        auto dpin2 = get_driver(find_sink_pin(node, "a"));
        if (!dpin2.is_invalid()) {
          std::string name2         = get_scaped_name(pin_wire_name(dpin2));
          bool        out_unsigned2 = (!dpin2.is_invalid() && type_op_of(dpin2.get_master_node()) == Ntype_op::Get_mask);
          add_to_pin2var(fout, dpin2, name2, out_unsigned2);
        }
      }
      auto nname = node_name_of(node);
      if (!nname.empty() && nname.front() != '_') {
        continue;
      }
    } else if (op == Ntype_op::Set_mask) {
      add_to_pin2var(fout, dpin, name, false);
    } else if (op == Ntype_op::Nconst || is_const_pin(dpin)) {
      auto final_expr = const_to_verilog(hydrate_const(dpin));
      pin2expr.emplace(dpin.get_class_index(), Expr(final_expr, false));
    } else if (op == Ntype_op::Get_mask) {
      auto a_spin  = find_sink_pin(node, "a");
      name         = get_scaped_name(absl::StrCat(pin_wire_name(dpin), "_u"));
      out_unsigned = true;
      auto a_dpin  = get_driver(a_spin);
      if (!a_dpin.is_invalid() && !pin2var.contains(a_dpin.get_class_index())) {
        auto name2 = get_scaped_name(pin_wire_name(a_dpin));
        add_to_pin2var(fout, a_dpin, name2, false);
      }
    } else if (op == Ntype_op::AttrSet) {
      auto dpin_key = get_driver(find_sink_pin(node, "field"));
      I(!dpin_key.is_invalid() && is_type_const(dpin_key.get_master_node()));
      auto key = hydrate_const(dpin_key).to_field();

      bool dp_assign = str_tools::ends_with(key, "__dp_assign");

      hhds::Pin_class attr_dpin;
      if (dp_assign) {
        attr_dpin = get_driver(find_sink_pin(node, "value"));
      } else {
        attr_dpin = get_driver(find_sink_pin(node, "parent"));
      }
      std::string attr_name;
      if (attr_dpin.is_invalid()) {
        attr_name = "0";
      } else {
        attr_name = get_wire_or_const(attr_dpin);
        add_to_pin2var(fout, attr_dpin, attr_name, out_unsigned);
      }

      pin2expr.insert({dpin.get_class_index(), Expr(attr_name, false)});
      continue;
    } else if (!is_type_register(node)) {  // Flop/Fflop/Latch all declare their q as a reg
      auto nname = node_name_of(node);
      if (!nname.empty() && nname.front() != '_') {
        continue;
      }
      // Declare a named wire only for a fanout of >=2 (single-use nets inline).
      // Cap the walk at 2: never iterate a high-fanout driver's full out-edge
      // set just to learn it has "more than one" reader.
      int fanout = 0;
      for (const auto& e : node.out_edges()) {
        (void)e;
        if (++fanout >= 2) {
          break;
        }
      }
      if (fanout < 2) {
        continue;
      }
    }

    add_to_pin2var(fout, dpin, name, out_unsigned);
  }
}

void Cgen_verilog::do_from_graph(const std::shared_ptr<hhds::Graph>& graph) {
  TRACE_EVENT("pass", nullptr, [&graph](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(absl::StrCat(converted_str, graph->get_name()));
  });

  assert(nrunning == 0);
  ++nrunning;

  (void)verbose;

  pin2var.clear();
  pin2expr.clear();
  mux2vector.clear();
  declared_name_counts.clear();
  first_array_block = true;
  map_segments_.clear();
  mem_wrappers_emitted_.clear();

  std::string filename;
  if (odir.empty()) {
    filename = absl::StrCat(graph->get_name(), ".v");
  } else {
    filename = absl::StrCat(odir, "/", graph->get_name(), ".v");
  }

  auto fout = std::make_shared<File_output>(filename);

  // Module anchor: any io node tolg stamped with the declaration's SourceId.
  module_anchor_ = hhds::Node_class();
  if (auto gio0 = graph->get_io(); gio0 && srcmap) {
    auto pick = [&](const auto& decls, bool is_input) {
      for (const auto& d : decls) {
        auto pin = is_input ? graph->get_input_pin(d.name) : graph->get_output_pin(d.name);
        if (!pin.is_invalid() && pin.get_master_node().attr(hhds::attrs::srcid).has()) {
          module_anchor_ = pin.get_master_node();
          return true;
        }
      }
      return false;
    };
    if (!pick(gio0->get_input_pin_decls(), true)) {
      pick(gio0->get_output_pin_decls(), false);
    }
  }

  note_module(fout);
  fout->append("/* verilator lint_off WIDTH */\n");
  note_module(fout);
  fout->append("module ", get_scaped_name(graph->get_name()), "(\n");

  hhds::Graph* g = graph.get();
  create_module_io(fout, g);

  reserve_instance_names(g);
  create_locals(fout, g);
  create_memories(fout, g);
  create_subs(fout, g);

  create_combinational(fout, g);
  create_outputs(fout, g);
  create_registers(fout, g);

  note_module(fout);
  fout->append("endmodule\n");

  write_srcmap(fout, filename, graph->source_locator());

  --nrunning;
}
