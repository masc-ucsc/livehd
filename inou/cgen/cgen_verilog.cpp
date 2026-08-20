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

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/graph.hpp"
#include "iassert.hpp"
#include "node_util.hpp"  // //graph:graph — livehd::graph_util::* helpers
#include "perf_tracing.hpp"
#include "split_selfref.hpp"  // //graph — pure-comb hierarchy false-loop repair
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

// Emit an integer constant in the receiving sink's literal width and sign.
// This avoids preserving Dlop's own signed carrier width at a typed boundary
// (for example a boolean clock/reset constant becoming 2 bits).
template <typename C>
std::string const_to_verilog(const C& c, int width, bool unsign) {
  if (width <= 0 || !c.is_integer()) {
    return const_to_verilog(c);
  }
  // Keep the common known-integer spelling compact. Besides producing cleaner
  // RTL, hex materially reduces generated source size for wide constants while
  // the explicit width/sign still provides the exact sink context.
  //
  // ONLY up to 64 bits: a sized Verilog literal is ZERO-extended to its declared
  // width, so spelling a 65-bit sink as `65'sh<64-bit two's complement>` turns
  // -1 into +2^64-1 (and a whole-array memory `update`/`init` bus is routinely
  // size*bits wide). Past 64 bits fall through to the bit spelling, which
  // extends explicitly.
  if (!c.has_unknowns() && c.is_just_i64() && width <= 64) {
    uint64_t value = static_cast<uint64_t>(c.to_just_i64());
    if (width < 64) {
      value &= (uint64_t{1} << width) - 1;
    }
    return absl::StrCat(width, unsign ? "'h" : "'sh", absl::Hex(value));
  }
  std::string bits;
  bits.reserve(static_cast<size_t>(width));
  const int  source_bits = std::max(1, static_cast<int>(c.get_bits()));
  // Above the value's own two's-complement width the pattern is its SIGN, for
  // an unsigned sink too: assigning a negative constant to a wider bus is a
  // two's-complement truncation, which is exactly what the hex path above emits
  // (`-1` at width 3 is `3'h7`). Zero-filling here instead made the two
  // spellings of the same constant disagree.
  const bool negative    = c.is_negative();
  for (int i = width - 1; i >= 0; --i) {
    if (c.unknown_bit_test(i)) {
      bits.push_back('?');
    } else if (i >= source_bits) {
      bits.push_back(negative ? '1' : '0');
    } else {
      bits.push_back(c.bit_test(i) ? '1' : '0');
    }
  }
  return absl::StrCat(width, unsign ? "'b" : "'sb", bits);
}

// Sort edges by sink port_id (for mux iteration).
void sort_by_sink_pid(livehd::graph_util::Edge_vec& edges) {
  std::sort(edges.begin(), edges.end(), [](const hhds::Edge_class& a, const hhds::Edge_class& b) {
    return a.sink.get_port_id() < b.sink.get_port_id();
  });
}

}  // namespace

Cgen_verilog::Cgen_verilog(bool _verbose, std::string_view _odir, bool _srcmap,
                           const absl::flat_hash_map<std::string, std::string>* _flat_names)
    : verbose(_verbose), odir(_odir), srcmap(_srcmap), nrunning(0), flat_names_(_flat_names) {
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
  // wire_name resolves graph-IO pins to their declared port name (via
  // pin_name_of) and internal pins to their attr / synthetic name.
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

std::string Cgen_verilog::get_wire_or_const(const hhds::Pin_class& dpin, int width, bool unsign) const {
  if (is_const_pin(dpin)) {
    return const_to_verilog(hydrate_const(dpin), width, unsign);
  }
  return get_wire_or_const(dpin);
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

std::string Cgen_verilog::flat_module_name(std::string_view full) const {
  // Verilog module names are flat: turn the internal hierarchical `file.entity`
  // into a legal, collision-free bare name. The driver's map has the authoritative
  // decision for every co-emitted graph (bare entity when unique, sanitized
  // `file_entity` when two graphs share the entity). A name not in the map (a
  // single-graph emit, or a reference to an external/black-box graph) falls back
  // to the bare entity — the last '.'/'/'-separated component.
  if (flat_names_ != nullptr) {
    if (auto it = flat_names_->find(std::string(full)); it != flat_names_->end()) {
      return it->second;
    }
  }
  auto pos = full.find_last_of("./");
  return std::string(pos == std::string_view::npos ? full : full.substr(pos + 1));
}

int32_t Cgen_verilog::decl_bits_of(const hhds::Pin_class& dpin) {
  // The width the net was declared with. Width hints are literal for both
  // signed and unsigned nets, so this is uniformly bits_of(). A 1-bit net is
  // declared as a scalar `reg` with no range at all, and a
  //     scalar cannot be indexed ("scalar type cannot be indexed") -- callers
  //     use a 1 here to mean "index-free, emit the bare name".
  // 0 means "no declared net" (a constant or an invalid pin): the caller must
  // fold instead of appending a select to the literal.
  if (dpin.is_invalid() || is_const_pin(dpin)) {
    return 0;
  }
  return bits_of(dpin);
}

bool Cgen_verilog::operand_reads_signed(const hhds::Pin_class& dpin) {
  if (dpin.is_invalid()) {
    return false;
  }
  // A constant pin carries no signed hint, but const_to_verilog emits a NEGATIVE
  // value as a signed literal (`N'sh<two's complement>`) — so it reads signed in
  // the emitted text and has to take the sign-extending path, or the surrounding
  // unsigned context zero-extends it back to a positive number
  // (`3'sb101 << 0` came out 16'h0005 instead of 16'hfffd). A non-negative
  // constant zero- and sign-extend alike, so it needs no special handling.
  if (is_const_pin(dpin)) {
    auto c = hydrate_const(dpin);
    return !c.has_unknowns() && c.is_negative();
  }
  if (!is_unsign(dpin)) {
    return true;
  }
  // The signed hint is dropped on every op output by tolg's bind_result, so a
  // chained right shift `(a>>b)>>b` reads as unsigned at the outer SRA even
  // though the inner SRA preserves the signed `a`. Walk through the SRA chain.
  auto node = dpin.get_master_node();
  if (node.is_invalid()) {
    return false;
  }
  if (type_op_of(node) == Ntype_op::SRA) {
    return operand_reads_signed(get_driver(find_sink_pin(node, "a")));
  }
  // Same dropped hint, one op further out: a BITWISE combination is a
  // width-preserving pass-through of its operands' values, so a signed value
  // flowing into one still reads signed at the output. Walking SRA only left a
  // hole that a Verilog ROUND TRIP walks straight into, because the widening
  // pad this very function guards is ITSELF an Or: reading back
  // `(11'sb0 | sa) << ua` gives SHL(a = Or(const 0, sa_signed)), the Or read
  // unsigned, the caller took the `{N{1'b0}} |` branch, and the unsigned OR
  // zero-extended a negative operand -- `sa = 3'sb100` (-4) emitted as 16'h0004
  // instead of 16'hfffc. MEASURED end to end: our own emitted module returned 4
  // where iverilog says 65532, on all of tests/equiv/signed_shift_widen.
  //
  // ANY operand, not ALL, matching the SRA arm above (which propagates from `a`
  // alone): the question here is "does a signed value reach this pin", because
  // the answer decides whether the WIDENING is a sign or a zero extension. A
  // non-negative constant operand answers false and is transparent either way
  // -- it zero- and sign-extends alike.
  // Restricted to a pure WIDENING PAD -- an Or whose every other operand is a
  // ZERO constant. That is exactly the shape this function's own caller emits
  // (`$signed(N'sb0) | $signed(val)`), and a zero operand cannot change the
  // value, so calling the result signed is just naming the sign it already has.
  //
  // Deliberately NOT every bitwise op, and not any Or: `And(val, MASK)` is the
  // width mask cgen puts on every net read, and a masked value is non-negative
  // by construction. Marking THAT signed made a later widening sign-extend it,
  // which broke six tests (mem_comptime_init, bitrange_dyn_narrow, ...) --
  // measured, then narrowed to this.
  if (type_op_of(node) == Ntype_op::Or) {
    bool saw_signed = false;
    for (const auto& e : node.inp_edges()) {
      if (is_const_pin(e.driver)) {
        const auto c = hydrate_const(e.driver);
        if (c.has_unknowns() || !c.is_known_false()) {
          return false;  // a NON-zero constant operand: not a pad
        }
        continue;
      }
      if (!operand_reads_signed(e.driver)) {
        return false;  // an unsigned data operand makes the whole Or unsigned
      }
      saw_signed = true;
    }
    return saw_signed;
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
  // producer in body().nodes(hhds::Node_order::forward) order. Do not emit a bare, undeclared net in
  // that case; inline the same local expression the producer would have cached.
  if (!dpin.is_invalid()) {
    auto node = dpin.get_master_node();
    if (!node.is_invalid()) {
      switch (type_op_of(node)) {
        case Ntype_op::Sum:
        case Ntype_op::Ror:
        case Ntype_op::Div:
        case Ntype_op::Rem:
        case Ntype_op::Not:
        case Ntype_op::LT:
        case Ntype_op::GT:
        case Ntype_op::SHL:
        case Ntype_op::SRA:
        case Ntype_op::Mult:
        case Ntype_op::And:
        case Ntype_op::Or:
        case Ntype_op::Xor:
        // A concatenation is SELF-DELIMITING (`{a,b,c}` carries its own braces
        // and its own width), so it inlines as safely as the operators above --
        // and its lanes are already width-adjusted by build_simple_expr.
        case Ntype_op::Concat:
        case Ntype_op::EQ    : return absl::StrCat("(", build_simple_expr(nullptr, node), ")");
        default              : break;
      }
    }
  }

  auto wn = pin_wire_name(dpin);
  if (!wn.empty()) {
    return get_scaped_name(wn);
  }
  return "'hx /*cgen-miss*/";
}

bool Cgen_verilog::declared_unsigned_net(const hhds::Pin_class& dpin) const {
  if (dpin.is_invalid() || is_const_pin(dpin)) {
    return false;
  }
  return pin2var.contains(dpin.get_class_index()) && pin2var_unsigned_.contains(dpin.get_class_index());
}

std::string Cgen_verilog::signed_operand(const hhds::Pin_class& dpin, std::string_view expr) const {
  if (!declared_unsigned_net(dpin)) {
    return std::string{expr};
  }
  // `{1'b0, x}` is self-determined at x's declared width + 1 (safe: x IS a
  // declared net here), and $signed of that is the same non-negative value in a
  // signed expression. `$signed(x)` alone would read 8'hff as -1.
  return absl::StrCat("$signed({1'b0,", expr, "})");
}

bool Cgen_verilog::mixes_operand_signs(const hhds::Node_class& node) const {
  bool saw_signed   = false;
  bool saw_unsigned = false;
  for (const auto& e : node.inp_edges()) {
    if (operand_reads_signed(e.driver)) {
      saw_signed = true;
    } else if (declared_unsigned_net(e.driver)) {
      saw_unsigned = true;
    }
  }
  return saw_signed && saw_unsigned;
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
// combinational half: it is emitted directly as
// `always_latch if (en) q <= d;`, which yosys re-reads as a $dlatch and our own
// slang reader classifies into latch_syms_. The reader maps EN->enable,
// D->din, and connects a const-0 `posclk` only for active-low enable
// (EN_POLARITY==false), so a known-false posclk means the transparent level is
// `!enable` (e.g. prim_clk_gate's `if (!clk_i)`).
void Cgen_verilog::process_latch(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  auto dpin_q = node.get_driver_pin(0);
  // create_locals may have de-collided Q from a directly connected module
  // output (for example `q_cgen1`).  Write the declared storage variable,
  // then let create_outputs publish it, exactly as the flop path does.
  auto name   = get_wire_or_const(dpin_q);

  auto din_dpin = get_driver(find_sink_pin(node, "din"));
  auto en_dpin  = get_driver(find_sink_pin(node, "enable"));
  if (din_dpin.is_invalid()) {
    return;  // malformed latch: leave the declared reg inert rather than emit garbage
  }
  // A MISSING enable means ALWAYS TRANSPARENT, not malformed. upass.tolg only
  // wires the pin when the enable is a real condition -- "the true const =>
  // unconditionally written (no enable needed)" (see its finalize_regs). That
  // contract is fine for a Flop, but this function used to bail on the absent
  // pin and emit NOTHING, leaving the latch's Q (often a module output, since a
  // `reg` written from an `always @(*)` lands here) with no driver at all. The
  // netlist then read X and lgcheck REFUTED the round trip -- e.g. a full
  // `case` with a `default`, which is transparent on every path.
  // VALUE context: get_expression, not get_wire_or_const (2f-latch M1). A
  // computed, single-fanout din/enable driver is INLINED into pin2expr and
  // never gets a wire of its own; get_wire_or_const ignores pin2expr and would
  // emit its bare, undeclared name. iverilog rejects that outright, while yosys
  // silently invents an implicit wire (reading X) and lgcheck then REFUTES the
  // round-trip. The flop path already uses get_expression for exactly this
  // reason (see process_flop and the `initial` pin's rationale comment).
  auto din    = get_expression(din_dpin);
  auto enable = en_dpin.is_invalid() ? std::string{} : get_expression(en_dpin);

  bool neg_en      = false;
  auto posclk_dpin = get_driver(find_sink_pin(node, "posclk"));
  if (!posclk_dpin.is_invalid() && is_const_pin(posclk_dpin)) {
    neg_en = hydrate_const(posclk_dpin).is_known_false();
  }

  // A CONSTANT-TRUE enable is the always-transparent case spelled differently:
  // the direct slang -> lg path wires an explicit `1` where the Pyrope path
  // omits the pin entirely. Normalize to "no enable" so both spellings take the
  // combinational emission below. A constant-FALSE enable is left alone -- `if
  // (0)` is an honest rendering of a latch that never opens.
  if (!enable.empty() && is_const_pin(en_dpin)) {
    if (const auto c = hydrate_const(en_dpin); !c.has_unknowns()) {
      if (neg_en ? c.is_known_zero() : !c.is_known_zero()) {
        enable.clear();
      }
    }
  }

  // RESET (2f-latch M7). A latch's reset is inherently ASYNCHRONOUS — there is
  // no clock edge to synchronize to — so it is emitted as the FIRST branch,
  // ahead of the transparency test:
  //     always_latch begin
  //       if (rst) q <= <initial>;
  //       else if (en) q <= d;
  //     end
  // which is the $adlatch shape. `negreset` inverts the test, `initial` gives
  // the value (default 0). Absent reset_pin => the plain two-line form below,
  // byte-identical to what M1 emitted.
  std::string rst_test;
  std::string rst_val = "'h0";
  if (auto rst_dpin = get_driver(find_sink_pin(node, "reset_pin")); !rst_dpin.is_invalid()) {
    bool negreset = false;
    if (auto np = get_driver(find_sink_pin(node, "negreset")); !np.is_invalid() && is_const_pin(np)) {
      negreset = !hydrate_const(np).is_known_false();
    }
    rst_test = absl::StrCat(negreset ? "!" : "", get_expression(rst_dpin));
    if (auto init_dpin = get_driver(find_sink_pin(node, "initial")); !init_dpin.is_invalid()) {
      rst_val = get_expression(init_dpin);
    }
  }

  note_src(fout, node);
  // `always_latch` + NONBLOCKING `<=`, not `always @*` + blocking `=`
  // (2f-latch M1). yosys re-infers a latch from either, but our own slang
  // reader classifies a non-edge `always` with BLOCKING writes as plain
  // combinational logic — so the old emission could not round-trip through our
  // front end (Verilog -> slang lost the Latch cell entirely). `always_latch`
  // also states the intent to every downstream tool instead of relying on
  // inference.
  // ALWAYS TRANSPARENT (no enable pin): the cell stores nothing — every path
  // writes it — so it is plain combinational logic and must be emitted as
  // `always_comb` with a BLOCKING write. `always_latch` would be a lie yosys
  // refuses outright ("No latch inferred for signal ... from always_latch
  // process"), which lgcheck reports as a failed miter, i.e. still no round
  // trip. This is the shape of a full `case` with a `default` inside an
  // `always @(*)`.
  if (enable.empty()) {
    fout->append("always_comb begin\n");
    if (rst_test.empty()) {
      fout->append(absl::StrCat("  ", name, " = ", din, ";\n"));
    } else {
      fout->append(absl::StrCat("  if (", rst_test, ") ", name, " = ", rst_val, ";\n"));
      fout->append(absl::StrCat("  else ", name, " = ", din, ";\n"));
    }
    fout->append("end\n");
    return;
  }
  fout->append("always_latch begin\n");
  if (rst_test.empty()) {
    fout->append(absl::StrCat("  if (", neg_en ? "!" : "", enable, ") ", name, " <= ", din, ";\n"));
  } else {
    fout->append(absl::StrCat("  if (", rst_test, ") ", name, " <= ", rst_val, ";\n"));
    fout->append(absl::StrCat("  else if (", neg_en ? "!" : "", enable, ") ", name, " <= ", din, ";\n"));
  }
  fout->append("end\n");
}

// Generate a cgen_memory_[multiclock_]<R>rd_<W>wr module mirroring the static
// ware/rtl wrapper templates, for (R,W,clock) shapes ware/rtl does not ship
// (e.g. a register file that reads out all entries -> many read ports, or a
// multi-clock RF). Semantics match the templates exactly: port-order write
// priority (the highest enabled write port wins a same-address collision, i.e.
// the LAST program write), a per-(READ,WRITE) FWD matrix (bit k*n_wr+j forwards
// write port j to read port k), the parallel UNDEF matrix (same layout; the
// collision reads x instead — Pyrope `ordering="none"`), and LATENCY_0 (==1
// flops the output once, ==0 async). Uses $clog2 instead of the `log2 macro to
// avoid depending on a macro that may not be in scope when emitted inline.
std::string Cgen_verilog::gen_mem_wrapper(const std::string& mod_name, int n_rd, int n_wr, bool single_clock,
                                          bool no_collision_bypass) {
  std::string s;
  const auto  guard  = absl::StrCat("LIVEHD_", absl::AsciiStrToUpper(mod_name), "_DEFINED");
  s                 += absl::StrCat("`ifndef ", guard, "\n`define ", guard, "\nmodule ", mod_name, "\n");
  // FWD is the per-(read,write) matrix (bit k*n_wr+j) and can exceed a plain
  // integer parameter's 32 bits, so it is explicitly sized to THIS shape's
  // n_rd*n_wr (floored at 256 to match the shipped ware/rtl templates). A
  // reset-restore expansion mints one write port per entry, so the matrix
  // easily runs past 256 bits and a fixed width would silently drop the high
  // read-port rows.
  // ... but the direct-read specialization reads NEITHER matrix, and its whole
  // reason to exist is the shape (thousands of ports) whose n_rd*n_wr product
  // runs into the millions. Declaring two multi-megabit parameters no logic
  // touches re-creates the gigabyte-Verilog problem the specialization avoids,
  // so keep them at the shipped-template floor there (every caller passes 0).
  const int fwd_w    = no_collision_bypass ? 256 : std::max(256, n_rd * n_wr);
  s += absl::StrCat("  #(parameter BITS = 4, SIZE=128, parameter [", fwd_w - 1, ":0] FWD=1, parameter LATENCY_0=1, WENSIZE=1,\n");
  // UNDEF is the same shape as FWD and goes LAST so no existing positional
  // parameter moves; defaulting to 0 keeps every caller that omits it identical.
  s += absl::StrCat("    parameter INIT_EN=0, parameter [BITS*SIZE-1:0] INIT=0, parameter [", fwd_w - 1, ":0] UNDEF=0)\n  (\n");
  bool first = true;
  auto port  = [&](const std::string& decl) {
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
    s += absl::StrCat("reg [BITS-1:0] d", k, "_mem;\n");
    s += absl::StrCat("always_comb d", k, "_mem = rd_enable_", k, " ? data[rd_addr_", k, "] : {BITS{1'bx}};\n");
    std::string read_value = absl::StrCat("d", k, "_mem");
    if (!no_collision_bypass) {
      read_value  = absl::StrCat("d", k, "_fwd");
      s          += absl::StrCat("reg [BITS-1:0] ", read_value, ";\n");
      s          += absl::StrCat("genvar fwd_j", k, ";\n");
      s          += absl::StrCat("generate for(fwd_j",
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
      s          += absl::StrCat("  always_comb ", read_value, "[fwd_j", k, "*MASKSIZE +: MASKSIZE] =\n");
      // FWD is a per-(read,write) matrix: bit (k*n_wr + j) forwards write port j
      // to read port k. Later write ports override earlier ones so a same-address
      // multi-write forwards the LAST writer, matching the storage priority in
      // the always block above (which is why this chain runs j high -> low).
      // UNDEF is the same matrix for "the collision is undefined": its rung goes
      // FIRST within a write port (the two bits are mutually exclusive, so the
      // order only matters if a caller sets both) but stays INSIDE the per-port
      // step, so a low port's UNDEF can never beat a high port's FWD.
      for (int j = n_wr - 1; j >= 0; --j) {
        s += absl::StrCat("    (((UNDEF >> ",
                          k * n_wr + j,
                          ") & 1) != 0 && wr_enable_",
                          j,
                          "[fwd_j",
                          k,
                          "] && (wr_addr_",
                          j,
                          " == rd_addr_",
                          k,
                          ")) ? {MASKSIZE{1'bx}} :\n");
        s += absl::StrCat("    (((FWD >> ",
                          k * n_wr + j,
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
    }
    s += absl::StrCat("generate if (LATENCY_0==1) begin:BLOCK_RD_LAT_", k, "\n");
    s += absl::StrCat("  always @(posedge ",
                      single_clock ? std::string("clk") : absl::StrCat("rd_clock_", k),
                      ") rd_dout_",
                      k,
                      " <= ",
                      read_value,
                      ";\n");
    s += absl::StrCat("end else begin:BLOCK_RD_COMB_", k, "\n  assign rd_dout_", k, " = ", read_value, ";\nend endgenerate\n");
  }
  s += absl::StrCat("endmodule\n`endif // ", guard, "\n");
  return s;
}

void Cgen_verilog::process_memory(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  note_src(fout, node);
  // Derived net names must be composed BEFORE escaping: an escaped identifier
  // ends at its terminating space, so escape-then-append yields `\mem _data`
  // (two tokens). Memory nodes now carry their RTL name (tolg set_name), which
  // can be a Verilog keyword or a dotted bundle-field path.
  const auto iraw  = std::string(default_instance_name(node));
  auto       iname = get_scaped_name(iraw);

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

  int             mem_size = 0;
  int             mem_bits = 0;
  hhds::Pin_class mem_fwd_dpin;     // per-(read,write) forwarding matrix (may exceed 64 bits)
  hhds::Pin_class mem_undef_dpin;   // per-(read,write) UNDEFINED-on-collision matrix (same layout)
  int             mem_type    = 2;  // array by default
  int             mem_wensize = 0;

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
      mem_fwd_dpin = e.driver;
    } else if (pin_name == "undef") {
      if (!is_const_pin(e.driver)) {
        livehd::diag::err("inou.cgen", "mem-malformed", "internal")
            .msg("memory {} should have a constant for undef not {}", debug_name(node), debug_name(e.driver.get_master_node()))
            .fatal();
        return;
      }
      mem_undef_dpin = e.driver;
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

  int mem_addr_bits = 1;
  for (uint64_t n = mem_size > 1 ? static_cast<uint64_t>(mem_size - 1) : 0; n > 1; n >>= 1) {
    ++mem_addr_bits;
  }

  // Does anything read the WHOLE array (the reserved read_all driver pid)?
  // Computed here because it selects the emission style below, not just what
  // that style emits.
  bool wants_read_all = false;
  for (const auto& e2 : node.out_edges()) {
    if (static_cast<hhds::Port_id>(e2.driver.get_port_id()) == Ntype::Memory_readall_pid) {
      wants_read_all = true;
      break;
    }
  }

  // ── Inline reg-array memory ────────────────────────────────────────────────
  // Taken when the `update` bus is driven (the entire array is (re)written from
  // one bus instead of N per-entry write ports: registered when a clock is
  // present, else combinational; per-port writes still apply and OVERRIDE the
  // bulk update, emitted after it so last-write wins) -- OR when anything reads
  // the array WHOLE.
  //
  // read_all is the second trigger because the cgen_memory_* wrappers below
  // expose only their per-port douts: they have no whole-array output and no
  // way to add one, so a memory with a read_all used to emit its `_dout_1048576`
  // wire and then leave it UNDRIVEN -- valid-looking Verilog whose whole-array
  // reader silently reads nothing (measured on a packed reg array that is both
  // element-indexed and assigned whole; it LEC-refuted). The wrappers also
  // cannot carry a runtime reset bus or an update bus, which is the original
  // reason this path exists.
  if (!mem_update_dpin.is_invalid() || wants_read_all) {
    const bool has_update = !mem_update_dpin.is_invalid();
    // The inline form has NO collision model: a registered read is a continuous
    // `assign` off the array reg, so it always returns the COMMITTED value, and
    // a combinational one is emitted after the writes, so it always forwards.
    // The cgen_memory_* wrappers below carry the per-(read,write) FWD/UNDEF
    // matrices instead. Diverting a `type` 0/1 memory here purely because
    // something reads it WHOLE therefore silently downgrades `ordering="fwd"` /
    // `ordering="none"` to `"old"` -- so refuse loudly rather than emit Verilog
    // that disagrees with the graph. (A whole-array `update` cell never gets a
    // real matrix: tolg leaves `fwd` at its provisional declare value there.)
    if (!has_update && (mem_type == 0 || mem_type == 1)) {
      const bool collides = (!mem_fwd_dpin.is_invalid() && !hydrate_const(mem_fwd_dpin).is_known_zero())
                            || (!mem_undef_dpin.is_invalid() && !hydrate_const(mem_undef_dpin).is_known_zero());
      if (collides) {
        livehd::diag::err("inou.cgen", "mem-readall-collision", "unsupported")
            .msg(
                "memory {} is read WHOLE (read_all) and also carries a non-zero same-cycle collision matrix; the inline "
                "reg-array emission the whole read forces has no forwarding model",
                debug_name(node))
            .hint("spell `ordering=\"old\"` on the array, or drop the whole-array read so the cgen_memory_* wrapper (which "
                  "carries FWD/UNDEF) is used")
            .fatal();
        return;
      }
    }
    const auto aname      = get_scaped_name(absl::StrCat(iraw, "_data"));
    // The FIRST clock any port carries, not port zero's. tolg always wires the
    // cell clock into port 0's block, but a graph from another front end (or a
    // reloaded `lg:`) may put it on a later port -- and now that `read_all`
    // alone diverts a cell here, guessing "no clock" would emit a clocked
    // memory as a pure `always_comb` array, silently dropping all of its state.
    hhds::Pin_class clock_dpin;
    for (const auto& p : port_vector) {
      if (!p.clock.is_invalid()) {
        clock_dpin = p.clock;
        break;
      }
    }
    const bool registered = !clock_dpin.is_invalid();
    const int  busw       = mem_size * mem_bits;

    fout->append(absl::StrCat("reg [", mem_bits - 1, ":0] ", aname, "[", mem_size - 1, ":0];\n"));
    // Bind the buses to nets so per-entry part-selects are always legal.
    const auto updbus = absl::StrCat(aname, "_upd");
    if (has_update) {
      fout->append(absl::StrCat("wire [", busw - 1, ":0] ", updbus, " = ", get_wire_or_const(mem_update_dpin, busw, true), ";\n"));
    }
    // COMPTIME power-on contents: a constant `init` with NO runtime `reset`
    // condition (with one, the same pin is the reset-value BUS instead — see
    // below). The cgen_memory_* wrappers spell this as their INIT_EN/INIT
    // parameters; inline it is either an `initial` block (registered) or the
    // per-cycle default of the combinational always_comb. Sliced once here
    // because both consumers want the same per-entry constants.
    std::vector<std::string> init_entries;
    if (!mem_init_dpin.is_invalid() && mem_reset_dpin.is_invalid() && is_const_pin(mem_init_dpin)) {
      const auto init_value = hydrate_const(mem_init_dpin);
      init_entries.reserve(static_cast<size_t>(mem_size));
      for (int i = 0; i < mem_size; ++i) {
        const auto lane = init_value.get_mask_op(*Dlop::get_mask_value((i + 1) * mem_bits - 1, i * mem_bits));
        init_entries.emplace_back(const_to_verilog(*lane, mem_bits, true));
      }
    }
    std::string initbus;
    if (!mem_init_dpin.is_invalid() && !mem_reset_dpin.is_invalid()) {
      // Runtime reset-value bus (whole-array cells only). With no `reset`
      // condition the same pin is instead the COMPTIME power-on contents.
      initbus = absl::StrCat(aname, "_rst");
      fout->append(absl::StrCat("wire [", busw - 1, ":0] ", initbus, " = ", get_wire_or_const(mem_init_dpin, busw, true), ";\n"));
    } else if (registered && !init_entries.empty()) {
      // Without this a memory diverted here by read_all would silently lose its
      // power-on state. ONLY for the registered form: the combinational form
      // drives the array from an always_comb, and IEEE 1800 forbids a variable
      // written by always_comb being written by any other process — the array
      // takes its power-on contents as that block's per-cycle default instead.
      fout->append("initial begin\n");
      for (int i = 0; i < mem_size; ++i) {
        fout->append(absl::StrCat("  ", aname, "[", i, "] = ", init_entries[i], ";\n"));
      }
      fout->append("end\n");
    }
    auto entry_sel
        = [&](const std::string& bus, int i) { return absl::StrCat(bus, "[", (i + 1) * mem_bits - 1, ":", i * mem_bits, "]"); };

    if (registered) {
      fout->append(absl::StrCat("always @(posedge ", get_wire_or_const(clock_dpin, 1, true), ") begin\n"));
      std::string ind = "  ";
      if (!mem_reset_dpin.is_invalid()) {  // sync reset to the runtime init/reset bus (highest priority)
        fout->append(absl::StrCat("  if (", get_wire_or_const(mem_reset_dpin, 1, true), ") begin\n"));
        for (int i = 0; i < mem_size; ++i) {
          fout->append(
              absl::StrCat("    ", aname, "[", i, "] <= ", initbus.empty() ? std::string("'b0") : entry_sel(initbus, i), ";\n"));
        }
        fout->append("  end else begin\n");
        ind = "    ";
      }
      const bool gated = has_update && !mem_update_enable_dpin.is_invalid();
      if (gated) {
        fout->append(absl::StrCat(ind, "if (", get_wire_or_const(mem_update_enable_dpin, 1, true), ") begin\n"));
      }
      if (has_update) {
        for (int i = 0; i < mem_size; ++i) {  // bulk update (default); per-port writes below override
          fout->append(absl::StrCat(ind, gated ? "  " : "", aname, "[", i, "] <= ", entry_sel(updbus, i), ";\n"));
        }
      }
      if (gated) {
        fout->append(absl::StrCat(ind, "end\n"));
      }
      for (auto& p : port_vector) {  // per-port writes OVERRIDE the bulk update (later <= wins)
        if (p.rdport || p.addr.is_invalid() || p.din.is_invalid()) {
          continue;
        }
        auto w = absl::StrCat(aname,
                              "[",
                              get_wire_or_const(p.addr, mem_addr_bits, true),
                              "] <= ",
                              get_wire_or_const(p.din, mem_bits, true),
                              ";\n");
        fout->append(p.enable.is_invalid() ? absl::StrCat(ind, w)
                                           : absl::StrCat(ind, "if (", get_wire_or_const(p.enable, 1, true), ") ", w));
      }
      if (!mem_reset_dpin.is_invalid()) {
        fout->append("  end\n");
      }
      fout->append("end\n");
    } else {  // combinational whole-array (no clock); update_enable n/a (no hold state)
      fout->append("always_comb begin\n");
      if (has_update) {
        for (int i = 0; i < mem_size; ++i) {
          fout->append(absl::StrCat("  ", aname, "[", i, "] = ", entry_sel(updbus, i), ";\n"));
        }
      } else {
        // A combinational array holds NO state, so every entry has to be
        // assigned on every evaluation or the always_comb infers a latch and
        // stale entries survive. Default = the comptime power-on contents (zero
        // when there are none); the per-port writes below override it — the
        // same forwarding semantics the non-inline `type==2` array path emits.
        // Reachable since read_all (not just `update`) diverts a cell here.
        for (int i = 0; i < mem_size; ++i) {
          fout->append(absl::StrCat("  ",
                                    aname,
                                    "[",
                                    i,
                                    "] = ",
                                    init_entries.empty() ? absl::StrCat(mem_bits, "'b0") : init_entries[i],
                                    ";\n"));
        }
      }
      for (auto& p : port_vector) {
        if (p.rdport || p.addr.is_invalid() || p.din.is_invalid()) {
          continue;
        }
        auto w = absl::StrCat(aname,
                              "[",
                              get_wire_or_const(p.addr, mem_addr_bits, true),
                              "] = ",
                              get_wire_or_const(p.din, mem_bits, true),
                              ";\n");
        fout->append(p.enable.is_invalid() ? absl::StrCat("  ", w)
                                           : absl::StrCat("  if (", get_wire_or_const(p.enable, 1, true), ") ", w));
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
      drive(get_wire_or_const(dout_dpin), absl::StrCat(aname, "[", get_wire_or_const(p.addr, mem_addr_bits, true), "]"));
      ++n_rd_pos;
    }
    if (wants_read_all) {  // {data[size-1], ..., data[0]} (entry 0 in the low bits)
      std::string cat = "{";
      for (int i = mem_size - 1; i >= 0; --i) {
        cat += absl::StrCat(aname, "[", std::to_string(i), "]", i ? "," : "");
      }
      cat     += "}";
      auto ra  = node.create_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
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
    // many-ported register file) needs a new cgen_memory_<R>rd_<W>wr.v
    // variant.
    const bool have_wrapper        = single_clock ? ((eff_rd >= 1 && eff_rd <= 4 && eff_wr >= 1 && eff_wr <= 2)
                                              || (eff_rd == 1 && (eff_wr == 3 || eff_wr == 4)))
                                                  : (eff_rd == 1 && eff_wr == 1);
    const bool no_collision_bypass = (mem_fwd_dpin.is_invalid() || hydrate_const(mem_fwd_dpin).is_known_zero())
                                     && (mem_undef_dpin.is_invalid() || hydrate_const(mem_undef_dpin).is_known_zero());

    std::string name;
    name = absl::StrCat(name, "cgen_memory_", single_clock ? "" : "multiclock_");
    name = absl::StrCat(name, eff_rd, "rd_");
    name = absl::StrCat(name, eff_wr, "wr");
    // A large restored array can have thousands of read and write ports. When
    // both collision matrices are known zero (`ordering="old"`), emitting the
    // generic O(reads*writes) forwarding ladder is dead code and can grow into
    // gigabytes of Verilog. Give the direct-read specialization its own module
    // name so it can coexist with a forwarding instance of the same shape.
    if (!have_wrapper && no_collision_bypass) {
      name += "_nofwd";
    }

    // ware/rtl ships a fixed wrapper family; for any other (R,W,clock) shape
    // (e.g. a register file reading out all entries, or a multi-clock RF)
    // generate the wrapper module inline instead of `include`ing a missing file.
    // Dedup per file so two same-shape memories do not re-define the module.
    if (mem_wrappers_emitted_.insert(name).second) {
      if (have_wrapper) {
        fout->prepend(absl::StrCat("`include \"", name, ".v\" \n"));
      } else {
        fout->prepend(gen_mem_wrapper(name, eff_rd, eff_wr, single_clock, no_collision_bypass));
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
    {
      // The wrapper's FWD parameter is the per-(read,write) matrix. A matrix
      // wider than an int64 (a whole-array expansion reaches 9rd x 8wr = 72
      // bits) must go out as a sized literal, exactly like INIT.
      std::string fwd_txt = "0";
      if (!mem_fwd_dpin.is_invalid()) {
        auto fv = hydrate_const(mem_fwd_dpin);
        fwd_txt = fv.is_just_i64() ? std::to_string(fv.to_just_i64()) : const_to_verilog(fv);
      }
      parameters = absl::StrCat(parameters, first_entry ? "" : " ,", ".FWD", "(", fwd_txt, ")");
    }
    if (!mem_undef_dpin.is_invalid()) {
      // ordering="none": the same matrix shape saying "this collision reads x".
      // Emitted only when non-zero (tolg drives the pin only then), so every
      // netlist that predates the mode is byte-identical.
      auto        uv        = hydrate_const(mem_undef_dpin);
      std::string undef_txt = uv.is_just_i64() ? std::to_string(uv.to_just_i64()) : const_to_verilog(uv);
      parameters            = absl::StrCat(parameters, first_entry ? "" : " ,", ".UNDEF", "(", undef_txt, ")");
    }
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
      fout->append(absl::StrCat(".clk(", get_wire_or_const(base_clock_dpin, 1, true), ")\n"));
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
        fout->append(absl::StrCat(first_entry ? "  .rd_addr_" : "  ,.rd_addr_",
                                  n_rd_pos,
                                  "(",
                                  get_wire_or_const(p.addr, mem_addr_bits, true),
                                  ")\n"));
        first_entry = false;

        fout->append("  ,.rd_enable_", std::to_string(n_rd_pos), "(", get_wire_or_const(p.enable, 1, true), ")\n");
        if (!single_clock) {
          fout->append("  ,.rd_clock_", std::to_string(n_rd_pos), "(", get_wire_or_const(p.clock, 1, true), ")\n");
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
                                  get_wire_or_const(p.addr, mem_addr_bits, true),
                                  ")\n"));
        first_entry = false;

        // A memory write enable is one bit PER lane. A constant full-word
        // enable therefore has to land at WENSIZE (e.g. 2'b11), not at the
        // scalar control width used by read-enable. Narrowing it to 1 silently
        // disabled every lane above lane zero.
        fout->append("  ,.wr_enable_",
                     std::to_string(n_wr_pos),
                     "(",
                     get_wire_or_const(p.enable, std::max(mem_wensize, 1), true),
                     ")\n");
        if (!single_clock) {
          fout->append("  ,.wr_clock_", std::to_string(n_wr_pos), "(", get_wire_or_const(p.clock, 1, true), ")\n");
        }
        fout->append("  ,.wr_din_", std::to_string(n_wr_pos), "(", get_wire_or_const(p.din, mem_bits, true), ")\n");
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
    const auto aname = get_scaped_name(absl::StrCat(iraw, "_data"));
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
      auto din_name   = get_wire_or_const(p.din, mem_bits, true);
      auto write_stmt = absl::StrCat(aname, "[", get_wire_or_const(p.addr, mem_addr_bits, true), "] = ", din_name, ";\n");
      if (p.enable.is_invalid()) {
        fout->append("  ", write_stmt);
      } else {
        fout->append("  if (", get_wire_or_const(p.enable, 1, true), ") begin \n");
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

      auto read_stmt = absl::StrCat(dest_name, " = ", aname, "[", get_wire_or_const(p.addr, mem_addr_bits, true), "];\n");
      if (p.enable.is_invalid()) {
        fout->append("  ", read_stmt);
      } else {
        fout->append("  if (", get_wire_or_const(p.enable, 1, true), ") begin \n");
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
    // One unsigned operand makes the WHOLE Verilog expression unsigned, so a
    // signed sibling zero-extends: `a + b` with `input [7:0] a; input signed
    // [7:0] b` returned 255 for a=0, b=-1 where the LGraph value is -1. Ports
    // now carry their declared sign (they used to be a blanket `input signed`
    // compensated by a to_positive Get_mask), so read the unsigned ones as the
    // non-negative signed values they are.
    const bool  mixed_signs          = mixes_operand_signs(node);
    const int   result_bits          = bits_of(dpin);
    const bool  result_uns           = is_unsign(dpin);
    bool        saw_context_constant = false;
    auto        sum_expr             = [&](const hhds::Pin_class& operand_pin) {
      if (!is_const_pin(operand_pin) || result_bits <= 0) {
        return get_expression(operand_pin);
      }
      saw_context_constant = true;
      const auto c         = hydrate_const(operand_pin);
      // A Sum is context-determined by its realized result width. Materialize
      // constants in that context instead of retaining Dlop's signed-magnitude
      // carrier width (1 became 2'sh1 and confused a later Verilog->LGraph
      // round trip beside a u1 predicate). A negative operand stays signed so
      // its two's-complement extension remains arithmetic; a non-negative
      // operand follows the result's declared signedness.
      return absl::StrCat("(", const_to_verilog(c, result_bits, result_uns && !c.is_negative()), ")");
    };
    for (auto e : node.inp_edges()) {
      const auto raw     = sum_expr(e.driver);
      const auto operand = mixed_signs ? signed_operand(e.driver, raw) : std::string{};
      if (e.sink.get_port_id() == 0) {
        const auto& term = operand.empty() ? raw : operand;
        add_seq          = add_seq.empty() ? term : absl::StrCat(add_seq, " + ", term);
      } else {
        const auto& term = operand.empty() ? raw : operand;
        sub_seq          = sub_seq.empty() ? term : absl::StrCat(sub_seq, " + ", term);
      }
    }
    if (sub_seq.empty()) {
      final_expr = add_seq;
    } else if (add_seq.empty()) {
      final_expr = absl::StrCat(" -(", sub_seq, ")");
    } else {
      final_expr = absl::StrCat(add_seq, " - (", sub_seq, ")");
    }
    if (result_uns && saw_context_constant) {
      // The constant above gives the inner expression result_bits of context,
      // so this cast cannot narrow the arithmetic. It does make the proven
      // unsigned landing explicit to a Verilog reader: without it, the direct
      // Slang round trip re-tagged `u1_predicate + 1` signed and sign-extended
      // the 2-bit value 2 into a wider unsigned output as ...1110.
      final_expr = absl::StrCat("$unsigned(", final_expr, ")");
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
  } else if (op == Ntype_op::Rem) {
    auto lhs   = get_expression(get_driver(find_sink_pin(node, "a")));
    auto rhs   = get_expression(get_driver(find_sink_pin(node, "b")));
    final_expr = absl::StrCat(lhs, "%", rhs);
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
        if (range_begin == 0 && range_end >= static_cast<int>(bits_of(dpin))) {
          // Full overwrite of the declared width: no part-select. Load-bearing
          // for a 1-bit destination, which is DECLARED as a scalar reg — a
          // `var[0] = …` select on a scalar is invalid Verilog (abc's per-bit
          // Set_mask reassembly chain starts with exactly this shape).
          replace = " = ";
        } else if (value_bits_to_use == 1) {
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

      if (range_begin < 0 || range_end < 0) {
        std::string sel;
        auto        max_bits = std::max(mask_v.get_bits(), a_bits);
        for (auto i = 0; i < max_bits; ++i) {
          if (mask_v.and_op(*Dlop::create_integer(int64_t{1} << i))->is_known_false()) {
            continue;
          }
          // Past-the-net bits zero-extend unsigned values and sign-extend
          // signed values.
          std::string bit;
          if (a_bits > 0 && i >= a_bits) {
            bit = is_unsign(a_dpin) ? "1'b0" : absl::StrCat(a, "[", a_bits - 1, "]");
          } else {
            bit = absl::StrCat(a, "[", i, "]");
          }
          if (sel.empty()) {
            sel = bit;
          } else {
            sel = absl::StrCat(sel, ",", bit);
          }
        }
        final_expr = absl::StrCat("{", sel, "}");
        // a_bits == 0 means the driver width is unknown (no bits attr): the
        // sign-replicate / extend forms below would fabricate a[-1]. Fall
        // through to the width-agnostic part-select forms instead.
      } else if (a_bits > 0 && range_begin >= static_cast<int>(a_bits)) {
        // Entirely above the driver: zero-extend unsigned and sign-extend signed.
        if (is_unsign(a_dpin)) {
          final_expr = absl::StrCat("{", range_end - range_begin, "{1'b0}}");
        } else {
          final_expr = absl::StrCat("{", range_end - range_begin, "{", a, "[", a_bits - 1, "]}}");
        }
      } else if (a_bits > 0 && range_end > static_cast<int>(a_bits) && range_begin == 0) {
        // Pure widening: let the assignment context extend the bare net per its
        // declared signedness (sign-extend a signed net, zero-extend unsigned).
        // Letting the assignment perform the extension avoids manufacturing a
        // part-select and preserves the net's declared signedness.
        final_expr = a;
      } else if (a_bits > 0 && range_end > static_cast<int>(a_bits)) {
        // Straddling slice: high part = sign replication, low part = a
        // part-select. Unsigned values zero-fill; signed values replicate the
        // declared msb.
        if (is_unsign(a_dpin)) {
          auto decl_msb = a_bits - 1;
          if (decl_msb >= range_begin) {
            final_expr = absl::StrCat("{{", range_end - a_bits, "{1'b0}},", a, "[", decl_msb, ":", range_begin, "]}");
          } else {
            final_expr = absl::StrCat("{", range_end - range_begin, "{1'b0}}");
          }
        } else {
          auto top   = absl::StrCat("{{", range_end - a_bits, "{", a, "[", a_bits - 1, "]}}");
          final_expr = absl::StrCat(top, ",", a, "[", a_bits - 1, ":", range_begin, "]}");
        }
      } else if (range_begin == 0 && range_end >= out_bits) {
        final_expr = a;
      } else if (a_bits_to_use == 1) {
        if (a_bits > 0 && range_begin >= a_bits) {
          final_expr = is_unsign(a_dpin) ? "1'b0" : absl::StrCat(a, "[", a_bits - 1, "]");
        } else {
          final_expr = absl::StrCat(a, "[", range_begin, "]");
        }
      } else {
        final_expr = absl::StrCat(a, "[", range_end - 1, ":", range_begin, "]");
      }
    }
  } else if (op == Ntype_op::Sext) {
    auto a_dpin   = get_driver(find_sink_pin(node, "a"));
    auto lhs      = get_expression(a_dpin);
    auto pos_dpin = get_driver(find_sink_pin(node, "b"));
    auto pos_node = pos_dpin.is_invalid() ? hhds::Node_class{} : pos_dpin.get_master_node();
    if (!pos_node.is_invalid() && is_type_const(pos_node)) {
      auto lpos = hydrate_const(pos_dpin);
      if (lpos.is_just_i64()) {
        // Keep bits [pos-1:0] and let the assignment context sign-extend. The
        // select has to respect how the OPERAND was declared, not bits_of:
        //   * a constant operand renders as a parenthesized literal, and
        //     `(4'sh3)[0:0]` is not a legal Verilog select -- fold instead;
        //   * a 1-bit operand is a SCALAR reg, and `x[0:0]` on it is rejected by
        //     slang as "scalar type cannot be indexed" (this was live on the
        //     vloghammer expression_00064 round trip: yosys read the regen .v
        //     fine, so lgcheck passed while `lhd lec` could not even load it);
        // (A second bullet here described an unsigned Get_mask net declared a
        // bit NARROWER than bits_of. That convention is gone: decl_bits_of is
        // now `bits_of(dpin)` and add_to_pin2var no longer subtracts, because
        // `bits` is the literal container width for signed and unsigned alike.)
        // A select that covers the whole declared width is a no-op either way.
        const auto keep = lpos.to_just_i64();
        const auto decl = decl_bits_of(a_dpin);
        if (is_const_pin(a_dpin)) {
          // The Sext CELL's `b` is the kept bit COUNT, while Dlop::sext_op takes
          // the sign-bit POSITION (see upass_tolg's lower_sext) -- hence keep-1.
          // The int overload is hlop-internal; the public one takes the position
          // as a Dlop (same idiom as upass/bitwidth/wrap_sat.hpp).
          final_expr = const_to_verilog(*hydrate_const(a_dpin).sext_op(*Dlop::create_integer(static_cast<int>(keep) - 1)));
        } else if (keep <= 0) {
          final_expr = lhs;
        } else if (decl > 0 && keep > decl) {
          // Widen before changing signedness.  A bare unsigned RHS is
          // zero-extended by the signed destination's assignment context; a
          // signed RHS is sign-extended.  Casting the narrow value first would
          // instead reinterpret its current msb as a sign and then extend it
          // (u3 3'b111 -> -1 rather than the required widened +7).
          final_expr = lhs;
        } else if (decl <= 1 || keep == decl) {
          // Sext is also the explicit finite-vector signed reinterpretation
          // used by the Yosys reader. An unsigned one-bit/full-width source is
          // not a no-op here: $signed(1'b1) is -1 in unlimited LGraph terms.
          final_expr = is_unsign(a_dpin) ? absl::StrCat("$signed(", lhs, ")") : lhs;
        } else {
          // A Verilog part-select is unsigned regardless of its base. Cast the
          // kept finite vector before the surrounding context widens it.
          final_expr = absl::StrCat("$signed(", lhs, "[", keep - 1, ":0])");
        }
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
        // A declared-UNSIGNED net needs the zero-bit pad: bare `$signed(x)`
        // reinterprets its msb as a sign, so an unsigned 8'hff compares as -1.
        return declared_unsigned_net(cmp_dpin) ? signed_operand(cmp_dpin, expr) : absl::StrCat("$signed(", expr, ")");
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
    // ...but a ZERO pad is only right for an UNSIGNED operand. Padding a signed
    // value with `{N{1'b0}}` also turns the OR unsigned, so the operand
    // zero-extends and a negative left operand comes out POSITIVE: `sa << 0`
    // with sa = 3'sb100 (-4) emitted 16'h0004 instead of 16'hfffc. Widen with a
    // SIGNED zero and an explicitly signed operand instead — the OR is still
    // context-determined (so an inlined `a+b` operand still evaluates at the
    // full width, which is why the OR form was chosen over a concat), but now
    // the extension is a sign extension. Same operand-signedness question the
    // SRA branch below answers, hence the shared operand_reads_signed.
    auto       obits                     = bits_of(node.get_driver_pin(0));
    // The signed form below wraps the operand in `$signed(...)`, whose argument is
    // SELF-determined. That is exact for text that already carries the operand's
    // full width -- a declared variable (declared at bits_of) or a constant
    // literal -- but WRONG for an inlined multi-operand expression, which
    // self-determines at its own narrower natural width: `(10'sh12c + in1 - in2)`
    // inlines as 10 bits, so 555 re-read as signed-10 is -469. Only take the
    // signed path when the operand is self-contained; an inlined expression keeps
    // the context-determined unsigned pad it has always had.
    bool       operand_is_self_contained = is_const_pin(val_dpin) || pin2var.contains(val_dpin.get_class_index());
    const bool dest_declared_signed
        = pin2var.contains(dpin.get_class_index()) && !pin2var_unsigned_.contains(dpin.get_class_index());
    if (!operand_is_self_contained && operand_reads_signed(val_dpin) && dest_declared_signed) {
      // An INLINED signed operand is the hard case: it is not self-contained, so
      // `$signed()` would re-read it at its own narrow width, but the unsigned
      // pad zero-extends a negative value (`(sb - sa) << ub` shifted +251 where
      // the RTL says -1). Land it in this node's own destination first — that
      // variable is declared at `obits` AND signed, so the assignment does the
      // sign extension by declaration instead of by text width. Same trick the
      // Not branch below uses to evaluate at the destination width.
      if (auto var_pre = pin2var.find(dpin.get_class_index()); fout && var_pre != pin2var.end()) {
        fout->append("  ", var_pre->second, " = ", val_expr, ";\n");
        val_expr                  = var_pre->second;
        operand_is_self_contained = true;
      }
    }
    std::string wide_val;
    if (operand_is_self_contained && operand_reads_signed(val_dpin)) {
      wide_val = absl::StrCat("($signed(", std::to_string(obits), "'sb0) | $signed(", val_expr, "))");
    } else {
      wide_val = absl::StrCat("({", std::to_string(obits), "{1'b0}} | ", val_expr, ")");
    }

    // SHL b is single-driver (the one-hot multi-shift `(n<<b0)|(n<<b1)` form
    // was removed).
    // The amount rides a declared variable or a literal, never an inlined
    // expression — create_locals guarantees it (Verilog SELF-determines this
    // position; see the shift-amount pass there).
    auto amt_expr = get_expression(get_driver(find_sink_pin(node, "b")));
    final_expr    = absl::StrCat("(", wide_val, " << ", amt_expr, ")");
  } else if (op == Ntype_op::SRA) {
    auto a_dpin   = get_driver(find_sink_pin(node, "a"));
    auto val_expr = get_expression(a_dpin);
    auto amt_expr = get_expression(get_driver(find_sink_pin(node, "b")));  // declared: see create_locals
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
    if (!operand_reads_signed(a_dpin)) {
      final_expr = absl::StrCat(val_expr, " >>> ", amt_expr);
    } else {
      // A nested SRA's operand also takes this branch (its inner shift already
      // emitted self-contained signed text), so the outer `>>>` is isolated too
      // and the enclosing unsigned context cannot demote it to a logical shift.
      final_expr = absl::StrCat("$signed($signed(", val_expr, ") >>> ", amt_expr, ")");
    }
  } else if (op == Ntype_op::Concat) {
    // MSB-first `{lane0, lane1, ...}`. The lane table comes from concat_lanes(),
    // NEVER from inp_edges: a lane's window width rides an explicit const
    // operand (the odd sink pids) precisely because it is not recoverable from
    // the driver -- bits_of is an upper bound bitwidth/cprop are free to narrow,
    // and the value's significant bits are narrower still. Re-deriving a width
    // here would shift every lane ABOVE the one guessed wrong: a silent
    // miscompile, not a build error.
    const auto lanes = livehd::graph_util::concat_lanes(node);
    if (lanes.empty()) {
      // FAIL CLOSED, same argument as the terminal `else` below. Empty means
      // MALFORMED (missing/odd pin, a non-const or non-positive width), never
      // "zero lanes" -- and the empty-final_expr fallbacks at the end of this
      // function would quietly turn it into `'hx`.
      livehd::diag::err("inou.cgen", "concat-malformed", "internal")
          .msg("cell `{}` is a Concat whose lane table could not be decoded", debug_name(node))
          .hint(
              "a Concat's sinks are interleaved (value, width) pairs on p0,p1,p2,...; every odd pid must carry a "
              "positive integer constant")
          .fatal();
      return {};
    }

    // Every lane must land at EXACTLY its window width. Verilog SELF-determines
    // each concatenation operand -- it contributes its OWN width, not the
    // context's -- so a lane one bit too wide or too narrow silently shifts
    // every lane above it. That is why each one is width-adjusted here instead
    // of being pasted in raw.
    auto lane_at_width = [&](const hhds::Pin_class& v, int32_t w) -> std::string {
      if (is_const_pin(v)) {
        // Fold to a literal at the window width: a sized literal cannot take a
        // part-select and an unsized one self-determines at its own width, so
        // neither of the adjust forms below is available for a constant.
        // to_binary() is MSB-first over get_bits() and spells an unknown bit
        // '?' (the same spelling const_to_verilog already emits), so the low w
        // characters ARE the window, and a shorter value replicates its msb --
        // which is exactly `value mod 2^w` for a negative lane (-1 at w=3 is
        // 0b111).
        auto bin = hydrate_const(v).to_binary();
        if (bin.empty()) {
          bin = "0";
        }
        if (static_cast<int32_t>(bin.size()) >= w) {
          bin.erase(0, bin.size() - static_cast<size_t>(w));
        } else {
          const char msb = bin.front();  // copy: the insert below reallocates
          bin.insert(bin.begin(), static_cast<size_t>(w) - bin.size(), msb);
        }
        return absl::StrCat(w, "'b", bin);
      }

      auto    expr = get_expression(v);
      int32_t dw   = decl_bits_of(v);
      if (dw <= 0) {
        dw = 1;  // no width stamp: add_to_pin2var declares a SCALAR reg
      }
      if (dw == w) {
        return expr;
      }
      if (dw > w) {
        // Truncate. This is NOT a sign question: the window is defined as the
        // LOW w bits, so a negative lane keeps its two's-complement pattern and
        // must not be sign-extended back. dw > w >= 1 implies dw >= 2, so the
        // net is a vector and the part-select is legal (a 1-bit net is declared
        // as a scalar, which cannot be indexed).
        return absl::StrCat(expr, "[", w - 1, ":0]");
      }
      // Widen to w. `value mod 2^w` of a NEGATIVE value IS its two's-complement
      // window, i.e. a sign extension. The pad is chosen by the VALUE's sign
      // (is_unsign), never by how the net happens to be declared -- the two can
      // disagree, and the value is what the window has to reproduce. (The old
      // rationale here appealed to a "narrow unsigned reg" for a signed-pin
      // Get_mask; that narrower declaration no longer exists under the literal
      // width contract. The emitted code is unchanged and still correct: the
      // `_u` net is full width, so its top bit really is the value's sign.
      // Do not "simplify" this to read the declaration -- that shifts every
      // lane above it.)
      const int32_t pad = w - dw;
      if (is_unsign(v)) {
        return absl::StrCat("{", pad, "'b0,", expr, "}");
      }
      const auto sign_bit = (dw == 1) ? expr : absl::StrCat(expr, "[", dw - 1, "]");
      return absl::StrCat("{{", pad, "{", sign_bit, "}},", expr, "}");
    };

    // The emitted `{...}` is ALWAYS sum(w) bits wide. The driver pin's stamped
    // bits is not consulted: the interleaved const sinks carry the INTENDED bit
    // spacing, and LGraph passes are free to narrow a lane driver's real width
    // without that changing where any lane sits. This used to clip to
    // `min(bits_of(dpin), total)` and DROP whole MSB lanes, which silently
    // changed the value of every design where anything narrowed the pin.
    const auto lane_bad = livehd::graph_util::concat_lane_violation(lanes);
    I(lane_bad.empty(), lane_bad.c_str());

    std::string body;
    for (const auto& l : lanes) {
      if (!body.empty()) {
        absl::StrAppend(&body, ",");
      }
      // Exactly its declared window: a narrower driver sign-extends up to it
      // (lane_at_width), a wider one cannot reach here.
      absl::StrAppend(&body, lane_at_width(l.value, l.width));
    }
    final_expr = absl::StrCat("{", body, "}");
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
    // FAIL CLOSED on an op with no lowering here. `I()` alone is NOT enough:
    // it compiles out under NDEBUG (the default `-c opt` build), and the loop
    // below then joins the operands with an EMPTY operator -- a one-input cell
    // emits its first operand VERBATIM, i.e. a Clock_cell would emit its bare
    // `clk_ref` with the gate silently DROPPED, and a two-input one emits
    // syntactically broken text. A dropped clock gate is exactly the silent
    // miscompile class 2f-latch exists to remove, so name it instead.
    if (txt_op.empty()) {
      livehd::diag::err("inou.cgen", "unsupported-cell", "unsupported")
          .msg("cell `{}` ({}) has no Verilog lowering", debug_name(node), Ntype::get_name(op))
          .hint(
              "a Clock_cell reaches here only if a recognizer ran on the compile/emission path -- recognition is "
              "scoped to the formal and sim pipelines (2f-latch M9). Otherwise this op needs a lowering arm")
          .fatal();
      return {};
    }

    // Mult and EQ are VALUE ops: a mixed-sign expression must stay signed (see
    // the Sum arm). And/Or/Xor are bitwise and width-preserving, so padding
    // their operands would only widen the result.
    const bool mixed_signs = (op == Ntype_op::Mult || op == Ntype_op::EQ) && mixes_operand_signs(node);
    for (auto e : node.inp_edges()) {
      if (mixed_signs) {
        auto operand = signed_operand(e.driver, get_expression(e.driver));
        final_expr   = final_expr.empty() ? operand : absl::StrCat(final_expr, " ", txt_op, " ", operand);
        continue;
      }
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

std::string Cgen_verilog::sub_instance_name(const hhds::Node_class& node) {
  if (auto it = sub_instance_names_.find(node.get_class_index()); it != sub_instance_names_.end()) {
    return it->second;
  }
  std::string base;
  if (livehd::graph_util::has_name(node)) {
    base = default_instance_name(node);  // explicit RTL instance name
  } else if (auto io = node.get_subnode_io()) {
    // Anonymous instance (partition leaves region wrappers unnamed so the leaf
    // hier names stay transparent): derive a stable, readable name from the
    // module -- not `sub_<nid>`, which churns every recompile.
    base = "u_" + std::string{io->get_name()};
  } else {
    base = default_instance_name(node);
  }
  // De-collide against wires and other instances (two anonymous instances of one
  // module -- e.g. a deduplicated region mapped once and instantiated twice --
  // would otherwise both be `u_<module>`).
  auto name = get_unique_decl_name(get_scaped_name(base));
  sub_instance_names_.emplace(node.get_class_index(), name);
  return name;
}

std::string Cgen_verilog::loop_instance_name(const hhds::Node_class& node, const hhds::Subnode_occurrence& occurrence) {
  const Loop_occurrence_key key{node.get_class_index(), occurrence.ordinal()};
  if (auto it = loop_instance_names_.find(key); it != loop_instance_names_.end()) {
    return it->second;
  }
  auto* lib = occurrence.group().target_io().get_library();
  I(lib != nullptr);
  auto base = hhds::format_occurrence_path(*lib, occurrence.path());
  if (base.empty()) {
    base = absl::StrCat("u_", occurrence.group().target_io().get_name(), "__li", occurrence.ordinal());
  }
  auto name = get_unique_decl_name(get_scaped_name(base));
  loop_instance_names_.emplace(key, name);
  return name;
}

void Cgen_verilog::reserve_instance_names(hhds::Graph* graph) {
  for (auto node : graph->body().nodes()) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Sub) {
      const auto group = node.subnode_group();
      if (group.is_loop()) {
        for (const auto occurrence : group.occurrences()) {
          loop_instance_name(node, occurrence);
        }
      } else {
        sub_instance_name(node);  // choose + reserve the (possibly anonymous) name
      }
    } else if (op == Ntype_op::Memory) {
      declared_name_counts.insert({get_scaped_name(default_instance_name(node)), 1});
    }
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
    bool        unsign;
    bool        is_input;
    uint32_t    port_id;
  };
  std::vector<IoEntry> entries;
  for (const auto& d : gio->get_input_pin_decls()) {
    entries.push_back({d.name, d.bits, d.unsign, true, static_cast<uint32_t>(d.port_id)});
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    entries.push_back({d.name, d.bits, d.unsign, false, static_cast<uint32_t>(d.port_id)});
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

    const auto name = get_scaped_name(e.name);
    // A port OWNS its spelling: reserve it before anything else can be handed
    // the same one. Instance names in particular are source-derived now (the
    // LHS variable of the call), and `out = add(…)` on a module whose output is
    // `out` would otherwise emit `add out(…)` beside `output reg out` — an
    // illegal redefinition rather than a de-collided `out_cgen1`.
    declared_name_counts.insert({name, 1});

    // Prefer the concrete HHDS pin width when present. Some imported GraphIO
    // declarations can retain stale placeholder widths, while the graph pin
    // has already been fixed by bitwidth propagation.
    hhds::Pin_class pin  = e.is_input ? graph->get_input_pin(e.name) : graph->get_output_pin(e.name);
    const auto      bits = pin.is_invalid() ? e.bits : livehd::graph_util::bits_of(pin, *gio, e.name);

    // GraphIO is the source-language port contract. LGraph's internal values
    // are signed unbounded integers and an unsigned port is a non-negative
    // range, so preserve the declared Verilog sign at this physical boundary
    // instead of inserting a Get_mask node into the graph.
    if (e.is_input) {
      fout->append(e.unsign ? "input " : "input signed ");
    } else {
      fout->append(e.unsign ? "output reg " : "output reg signed ");
    }

    if (bits > 1) {
      fout->append("[", std::to_string(bits - 1), ":0] ", name, "\n");
    } else {
      fout->append(name, "\n");
    }

    // Map the corresponding HHDS pin (driver for inputs, sink for outputs) into pin2var.
    if (!pin.is_invalid()) {
      pin2var.emplace(pin.get_class_index(), name);
      if (e.unsign) {
        pin2var_unsigned_.insert(pin.get_class_index());
      }
    }
  }

  note_module(fout);
  fout->append(");\n");
}

void Cgen_verilog::create_memories(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->body().nodes()) {
    if (type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    process_memory(fout, node);
  }
}

void Cgen_verilog::create_subs(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->body().nodes()) {
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
      auto cond = get_driver(find_sink_pin(node, "cond"));
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
      // A DEFERRED obligation outranks a `proven` stamp. pass.formal marks a
      // selected-top IO assume BOTH proven (the only channel that keeps it an
      // active hypothesis for verify/LEC) and runtime_check (its verdict was
      // never Proven, so the netlist must still police the environment). Only a
      // genuinely discharged obligation — proven with NO deferred check — is
      // elided here.
      if (!livehd::graph_util::has_runtime_check(node) && livehd::graph_util::proven_of(node) != 0) {
        continue;  // pass.formal discharged it -> elide the runtime check
      }
      auto cond = get_driver(find_sink_pin(node, "cond"));
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
      if (kind == "assume" || kind == "assume_nocheck") {
        fout->append("  assume (", get_wire_or_const(cond), ");\n");
      } else {
        fout->append("  assert (", get_wire_or_const(cond), ") else $error(\"", detail, "\");\n");
      }
      fout->append("end\n");
      fout->append("// synthesis translate_on\n");
      continue;
    }

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

    if (node.is_loop_subnode()) {
      const auto group = node.subnode_group();
      group.validate();

      const auto signed_literal = [](uint32_t bits, int64_t value) {
        bits = std::max<uint32_t>(bits, 1);
        if (value >= 0) {
          return absl::StrCat(bits, "'sd", static_cast<uint64_t>(value));
        }
        const uint64_t magnitude = static_cast<uint64_t>(-(value + 1)) + 1;
        return absl::StrCat("-", bits, "'sd", magnitude);
      };

      for (const auto occurrence : group.occurrences()) {
        const auto bindings = occurrence.input_bindings();

        // Resolve and cache every input before emitting the call. Recurrences
        // refer only to ordinal-1, so a streaming ordinal walk is sufficient.
        for (const auto& input : sub_io->get_input_pin_decls()) {
          const Loop_pin_key       input_key{node.get_class_index(), occurrence.ordinal(), input.port_id};
          std::string              direct;
          std::string              previous_output;
          std::string              previous_input;
          bool                     has_inactive_bypass = false;
          std::vector<std::string> activation_terms;

          for (const auto& binding : bindings) {
            if (binding.input_port() != input.port_id) {
              continue;
            }
            using Kind = hhds::Input_binding_kind;
            switch (binding.kind()) {
              case Kind::invariant_external:
              case Kind::carry_initial:
              case Kind::external_activation:
                if (!binding.stored_edges().empty()) {
                  direct = get_wire_or_const(binding.stored_edges().front().driver,
                                             static_cast<int>(input.bits),
                                             sub_io->is_unsign(input.name));
                } else if (binding.kind() == Kind::external_activation) {
                  direct = "1'b1";
                }
                break;
              case Kind::domain_index:
                if (binding.index_value()) {
                  direct = signed_literal(input.bits, *binding.index_value());
                }
                break;
              case Kind::previous_occurrence_output: {
                I(binding.source_port() && binding.source_ordinal());
                const Loop_pin_key source{node.get_class_index(), *binding.source_ordinal(), *binding.source_port()};
                auto               it = loop_output_vars_.find(source);
                I(it != loop_output_vars_.end());
                previous_output = it->second;
                break;
              }
              case Kind::previous_occurrence_activation: {
                I(binding.source_port() && binding.source_ordinal());
                const Loop_pin_key source{node.get_class_index(), *binding.source_ordinal(), *binding.source_port()};
                auto               it = loop_input_exprs_.find(source);
                I(it != loop_input_exprs_.end());
                activation_terms.push_back(it->second);
                break;
              }
              case Kind::previous_occurrence_next_active: {
                I(binding.source_port() && binding.source_ordinal());
                const Loop_pin_key source{node.get_class_index(), *binding.source_ordinal(), *binding.source_port()};
                auto               it = loop_output_vars_.find(source);
                I(it != loop_output_vars_.end());
                activation_terms.push_back(it->second);
                break;
              }
              case Kind::inactive_carry_bypass: {
                I(binding.source_port() && binding.source_ordinal());
                const Loop_pin_key source{node.get_class_index(), *binding.source_ordinal(), *binding.source_port()};
                auto               it = loop_input_exprs_.find(source);
                I(it != loop_input_exprs_.end());
                previous_input      = it->second;
                has_inactive_bypass = true;
                break;
              }
            }
          }

          std::string expression = direct;
          bool        composed   = false;  // a NEW composite over other cached entries
          if (has_inactive_bypass) {
            const auto desc = group.loop();
            I(desc && desc->activation_input && occurrence.ordinal() > 0);
            const Loop_pin_key active_key{node.get_class_index(), occurrence.ordinal() - 1, *desc->activation_input};
            auto               active_it = loop_input_exprs_.find(active_key);
            I(active_it != loop_input_exprs_.end() && !previous_output.empty() && !previous_input.empty());
            expression = absl::StrCat("(", active_it->second, " ? ", previous_output, " : ", previous_input, ")");
            composed   = true;
          } else if (!activation_terms.empty()) {
            expression = activation_terms.front();
            for (size_t i = 1; i < activation_terms.size(); ++i) {
              expression = absl::StrCat("(", expression, " && ", activation_terms[i], ")");
              composed   = true;
            }
          } else if (!previous_output.empty()) {
            expression = previous_output;
          }
          // A composite is built from the PREVIOUS ordinal's cached entry, so
          // caching its TEXT nests one level per occurrence: activation and
          // inactive-carry expressions grow O(ordinal) each and are re-pasted at
          // every instantiation, which turns a 256-iteration loop into a
          // multi-hundred-megabyte .v. Bind it to a real net instead and cache
          // the NAME, exactly as loop_output_vars_ already does for the output
          // half. Declared here, immediately ahead of the instance that reads it.
          if (composed) {
            auto* lib = group.target_io().get_library();
            I(lib != nullptr);
            auto base = hhds::format_occurrence_path(*lib, occurrence.path());
            if (base.empty()) {
              base = absl::StrCat("u_", sub_io->get_name(), "__li", occurrence.ordinal());
            }
            auto           wire_name = get_unique_decl_name(get_scaped_name(absl::StrCat(base, "_i", input.port_id)));
            const uint32_t bits      = std::max<uint32_t>(input.bits, 1);
            if (bits <= 1) {
              fout->append("wire signed ", wire_name, ";\n");
            } else {
              fout->append("wire signed [", std::to_string(bits - 1), ":0] ", wire_name, ";\n");
            }
            fout->append("assign ", wire_name, " = ", expression, ";\n");
            expression = std::move(wire_name);
          }
          if (!expression.empty()) {
            loop_input_exprs_.insert_or_assign(input_key, std::move(expression));
          }
        }

        note_src(fout, node);
        fout->append(get_scaped_name(flat_module_name(sub_io->get_name())), " ", loop_instance_name(node, occurrence), "(\n");
        bool first_entry = true;
        for (const auto& io : ordered) {
          std::string        signal;
          const Loop_pin_key key{node.get_class_index(), occurrence.ordinal(), io.decl->port_id};
          if (io.is_input) {
            if (auto it = loop_input_exprs_.find(key); it != loop_input_exprs_.end()) {
              signal = it->second;
            }
          } else if (auto it = loop_output_vars_.find(key); it != loop_output_vars_.end()) {
            signal = it->second;
          }
          if (signal.empty()) {
            continue;
          }
          fout->append(absl::StrCat(first_entry ? "" : ",", ".", get_scaped_name(io.decl->name), "(", signal, ")\n"));
          first_entry = false;
        }
        note_src(fout, node);
        fout->append(");\n");
      }
      continue;
    }

    auto iname = sub_instance_name(node);

    note_src(fout, node);
    fout->append(get_scaped_name(flat_module_name(sub_io->get_name())), " ", iname, "(\n");

    bool first_entry = true;

    // Connections come from the instance's existing edges: a declared port with
    // no materialized pin (unused output, unconnected input) has no edge, and
    // probing it via get_driver_pin/get_sink_pin asserts inside hhds find_pin.
    absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> in_conn;   // sink port_id -> its driver
    absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> out_conn;  // driver port_id -> consumed driver pin
    for (const auto& e : node.inp_edges()) {
      in_conn.emplace(e.sink.get_port_id(), e.driver);
    }
    for (const auto& e : node.out_edges()) {
      out_conn.emplace(e.driver.get_port_id(), e.driver);
    }

    for (const auto& io : ordered) {
      hhds::Pin_class dpin;
      const auto&     conn = io.is_input ? in_conn : out_conn;
      if (auto it = conn.find(io.decl->port_id); it != conn.end()) {
        dpin = it->second;
      }
      if (!dpin.is_invalid()) {
        note_src(fout, node);
        // The port name must be escaped like the callee's own port DECLARATION
        // (get_scaped_name): a tuple-typed port flattens to a dotted leaf name
        // (`req.a`), which is only legal Verilog as `.\req.a (...)`.
        fout->append(absl::StrCat(first_entry ? "" : ",",
                                  ".",
                                  get_scaped_name(io.decl->name),
                                  "(",
                                  io.is_input
                                      ? get_wire_or_const(dpin, static_cast<int>(io.decl->bits), sub_io->is_unsign(io.decl->name))
                                      : get_wire_or_const(dpin),
                                  ")\n"));
        first_entry = false;
      }
    }

    note_src(fout, node);
    fout->append(");\n");
  }
}

void Cgen_verilog::create_clock_cells(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->body().nodes()) {
    if (type_op_of(node) != Ntype_op::Clock_cell || !node.has_out_edges()) {
      continue;
    }
    auto clk = get_driver(find_sink_pin(node, "clk_ref"));
    if (clk.is_invalid()) {
      livehd::diag::err("inou.cgen", "clock-cell-missing-clock", "internal")
          .msg("Clock_cell `{}` has no clk_ref", debug_name(node))
          .fatal();
      return;
    }

    int64_t div = 1;
    if (auto d = get_driver(find_sink_pin(node, "div")); !d.is_invalid()) {
      if (!is_const_pin(d) || !hydrate_const(d).is_just_i64()) {
        livehd::diag::err("inou.cgen", "clock-cell-div", "unsupported")
            .msg("Clock_cell `{}` has a non-constant divider", debug_name(node))
            .fatal();
        return;
      }
      div = hydrate_const(d).to_just_i64();
    }
    if (div != 1) {
      livehd::diag::err("inou.cgen", "clock-cell-div", "unsupported")
          .msg("Clock_cell `{}` requests div={}, but Verilog lowering supports only div=1", debug_name(node), div)
          .fatal();
      return;
    }

    bool invert = false;
    if (auto d = get_driver(find_sink_pin(node, "invert")); !d.is_invalid()) {
      if (!is_const_pin(d)) {
        livehd::diag::err("inou.cgen", "clock-cell-invert", "unsupported")
            .msg("Clock_cell `{}` has a non-constant invert flavour", debug_name(node))
            .fatal();
        return;
      }
      invert = !hydrate_const(d).is_known_false();
    }

    const auto dpin = node.create_driver_pin(0);
    auto       oit  = pin2var.find(dpin.get_class_index());
    auto       lit  = clock_latch_vars_.find(node.get_class_index());
    I(oit != pin2var.end() && lit != clock_latch_vars_.end());
    const auto clk_expr = get_expression(clk);
    const auto en_pin   = get_driver(find_sink_pin(node, "en"));
    const auto en_expr  = en_pin.is_invalid() ? std::string{"1'b1"} : get_expression(en_pin);

    note_src(fout, node);
    fout->append("always_latch begin\n");
    fout->append(absl::StrCat("  if (", invert ? "" : "!", clk_expr, ") ", lit->second, " <= ", en_expr, ";\n"));
    fout->append("end\n");
    if (invert) {
      fout->append(absl::StrCat("assign ", oit->second, " = ", clk_expr, " | ~", lit->second, ";\n"));
    } else {
      fout->append(absl::StrCat("assign ", oit->second, " = ", clk_expr, " & ", lit->second, ";\n"));
    }
  }
}

void Cgen_verilog::create_combinational(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  note_module(fout);
  fout->append("always_comb begin\n");

  for (auto node : graph->body().nodes(hhds::Node_order::forward)) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Clock_cell) {
      continue;  // emitted as a latch + continuous assignment below
    }
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
  for (auto node : graph->body().nodes()) {
    if (is_type_flop(node)) {
      process_flop(fout, node);
    }
  }
  note_module(fout);
  fout->append("end\n");
}

void Cgen_verilog::create_registers(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->body().nodes()) {
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
    // `get_mask_16_u` net) — or a plain module-input clock, which falls through
    // to its input name.
    //
    // What it must NEVER return here is an INLINE EXPRESSION. It used to: a
    // single-fanout derived clock (`clk_b & gate` read only by this clock_pin)
    // was left undeclared and emitted as `always @(posedge (clk_b & gate))`,
    // which is legal Verilog that our own reader rejects (`unsupported-clock`,
    // "the clock must be a plain signal") — cgen output LiveHD could not read
    // back, and tests/equiv/mclk_derived failed on exactly that. create_locals'
    // THIRD pass now force-declares every clock_pin driver, so the pin is in
    // pin2var by the time we get here and get_expression yields a net name.
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
        // VALUE context: get_expression, not get_wire_or_const — the same fix
        // M1 had to make for the latch's din/enable and this block's clock_pin.
        // A COMPUTED, single-fanout enable driver is inlined into pin2expr and
        // never declared as a wire, so get_wire_or_const returns a bare
        // undeclared name and the emitted `if (and_80)` does not elaborate
        // (iverilog: "Unable to bind wire/reg/memory"; yosys silently invents an
        // implicit wire reading X, which is worse). Previously unreachable
        // because every emitter gave a flop enable fanout >= 2; pass.single_edge
        // synthesizes `enable & (phase == slot)` with exactly one consumer.
        enable = get_expression(enable_dpin);
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
    int                      bits    = bits_of(dpin);
    bool                     out_uns = is_unsign(dpin);
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

  // A simple driver may intentionally use the spelling already owned by the
  // output port it drives.  Detect that before asking for a unique name: the
  // port was pre-reserved, so uniquifying first would turn `q` into `q_cgen1`
  // and defeat the direct-output case below.
  bool redeclares_output = false;
  if (!dpin.is_invalid()) {
    for (const auto& e : dpin.out_edges()) {
      if (is_graph_output_pin(e.sink) && get_scaped_name(pin_wire_name(e.sink)) == name) {
        redeclares_output = true;
        break;
      }
    }
  }
  std::string declared_name = redeclares_output ? std::string{name} : get_unique_decl_name(name);
  pin2var.insert({dpin.get_class_index(), declared_name});
  name = declared_name;

  // Anchor the wire declaration line at its defining cell.
  note_src(fout, dpin.get_master_node());

  int bits = bits_of(dpin);

  std::string reg_str;
  if (out_unsigned) {
    pin2var_unsigned_.insert(dpin.get_class_index());
    reg_str = "reg ";
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
  // Clock nets must be claimed before the ordinary traversal: a downstream
  // state/control consumer can otherwise name the Clock_cell output first and
  // add_to_pin2var declares it as a procedural reg, leaving no private enable
  // latch record for the dedicated lowering.
  for (auto node : graph->body().nodes()) {
    if (type_op_of(node) != Ntype_op::Clock_cell || !node.has_out_edges()) {
      continue;
    }
    auto       dpin     = node.create_driver_pin(0);
    const auto out_name = get_unique_decl_name(get_scaped_name(pin_wire_name(dpin)));
    const auto lat_name = get_unique_decl_name(get_scaped_name(absl::StrCat(out_name, "__en_latched")));
    pin2var.insert_or_assign(dpin.get_class_index(), out_name);
    pin2var_unsigned_.insert(dpin.get_class_index());
    clock_latch_vars_.insert_or_assign(node.get_class_index(), lat_name);
    note_src(fout, node);
    fout->append("wire ", out_name, ";\n");
    fout->append("reg ", lat_name, ";\n");
  }

  for (auto node : graph->body().nodes()) {
    auto op = type_op_of(node);

    if (Ntype::has_multiple_driver_pins(op)) {
      if (op == Ntype_op::Sub || op == Ntype_op::Memory) {
        if (op == Ntype_op::Sub && node.is_loop_subnode()) {
          const auto group = node.subnode_group();
          group.validate();

          // Only parent-body drivers need ordinary local declarations. The
          // descriptor's output->input self-edges are virtualized below and
          // must never create one shared class-level output net.
          for (const auto& e : node.inp_edges()) {
            if (e.driver.get_master_node() == node) {
              continue;
            }
            add_to_pin2var(fout, e.driver, get_scaped_name(pin_wire_name(e.driver)), is_unsign(e.driver));
          }

          // Materialize one private output net per logical call. Declare all
          // interface outputs: a net that has no parent reader can still feed a
          // carry or the next-active recurrence.
          for (const auto occurrence : group.occurrences()) {
            auto* lib = group.target_io().get_library();
            I(lib != nullptr);
            const auto occurrence_name = hhds::format_occurrence_path(*lib, occurrence.path());
            for (const auto& output : group.target_io().get_output_pin_decls()) {
              const Loop_pin_key key{node.get_class_index(), occurrence.ordinal(), output.port_id};
              if (loop_output_vars_.contains(key)) {
                continue;
              }
              auto name = get_unique_decl_name(get_scaped_name(absl::StrCat(occurrence_name, "_o", output.port_id)));
              loop_output_vars_.emplace(key, name);
              // Honor the callee's declared sign, exactly as the ordinary Sub
              // output path below does. A blanket `wire signed` makes every
              // wider read of an unsigned loop output SIGN-extend: a u8 lane
              // holding 8'hC8 widens to 16'hFFC8 while cgen's own expression
              // logic, which reads is_unsign() off the class pin, assumed
              // 16'h00C8.
              const bool out_unsigned = output.unsign;
              if (out_unsigned) {
                loop_output_unsigned_.insert(key);
              }
              if (output.bits <= 1) {
                fout->append(out_unsigned ? "wire " : "wire signed ", name, ";\n");
              } else {
                fout->append(out_unsigned ? "wire [" : "wire signed [", std::to_string(output.bits - 1), ":0] ", name, ";\n");
              }
            }
          }

          // Downstream class edges represent the loop result. Point that class
          // pin at the last logical occurrence without changing graph storage.
          if (group.size() != 0) {
            for (const auto& e : node.out_edges()) {
              if (e.sink.get_master_node() == node) {
                continue;
              }
              const Loop_pin_key key{node.get_class_index(), group.size() - 1, e.driver.get_port_id()};
              if (auto it = loop_output_vars_.find(key); it != loop_output_vars_.end()) {
                pin2var.insert_or_assign(e.driver.get_class_index(), it->second);
                // Without this, declared_unsigned_net() is false for the net we
                // just declared `wire` (not `wire signed`), and signed_operand()
                // emits it bare -- the reader would re-sign what the decl
                // deliberately left unsigned.
                if (loop_output_unsigned_.contains(key)) {
                  pin2var_unsigned_.insert(e.driver.get_class_index());
                }
              }
            }
          } else {
            // An empty loop publishes carried initial values directly. Bind
            // the stored class output to the corresponding external input
            // expression; non-carried outputs have no defined reader binding.
            for (const auto& binding : group.zero_count_output_bindings()) {
              if (!binding.source_input_port()) {
                continue;
              }
              hhds::Pin_class initial;
              hhds::Pin_class output;
              for (const auto& e : node.inp_edges()) {
                if (e.driver.get_master_node() != node && e.sink.get_port_id() == *binding.source_input_port()) {
                  initial = e.driver;
                  break;
                }
              }
              for (const auto& e : node.out_edges()) {
                if (e.sink.get_master_node() != node && e.driver.get_port_id() == binding.output_port()) {
                  output = e.driver;
                  break;
                }
              }
              if (!initial.is_invalid() && !output.is_invalid()) {
                pin2expr.insert_or_assign(output.get_class_index(), Expr(get_wire_or_const(initial), false));
              }
            }
          }
          continue;
        }

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
            auto dout = node.create_driver_pin(e2.driver.get_port_id());
            // Claim the slot FIRST (like the Sub branch below): get_unique_decl_name
            // permanently reserves the name it returns, so computing it for a pin
            // already bound would burn a `_cgenN` counter on a name never declared.
            if (pin2var.contains(dout.get_class_index())) {
              continue;
            }
            // Escape the FULL derived name as one unit: a memory instance name
            // can carry verilog-special chars (e.g. the '.' of a flattened
            // hierarchical name), so escaping iname first and then appending
            // "_dout_N" would drop the suffix past the escaped id's terminating
            // space (`\u_fifo.mem _dout_1`), which yosys cannot parse.
            // De-collide: two same-named Memory instances in one body (flattening,
            // or the occurrences a replica expansion produces) derive the SAME
            // `<iname>_dout_<pid>`, and declaring it twice is an illegal
            // re-declaration. reserve_instance_names only pre-seeds the bare
            // instance name, not these derivatives.
            auto name2 = get_unique_decl_name(
                get_scaped_name(absl::StrCat(default_instance_name(node), "_dout_", e2.driver.get_port_id())));
            pin2var.insert({dout.get_class_index(), name2});
            {
              int bits2 = bits_of(dout);
              // The whole-array read output (Ntype::Memory_readall_pid) carries the
              // ENTIRE array -- size*bits, entry 0 in the low bits -- not one entry.
              // bits_of() on that pin is the ELEMENT width, so declaring from it
              // truncated the {data[size-1],...,data[0]} concat that process_memory
              // drives down to entry 0, and every consumer of the bus (a whole-array
              // `d = q` copy, then the per-entry update) then read garbage. LEC
              // refuted comb_array_const_index_read on exactly this.
              if (static_cast<hhds::Port_id>(e2.driver.get_port_id()) == Ntype::Memory_readall_pid) {
                int64_t mb = 0;
                int64_t ms = 0;
                for (const auto& e3 : node.inp_edges()) {
                  if (!is_const_pin(e3.driver)) {
                    continue;
                  }
                  auto nm = Ntype::get_sink_name(Ntype_op::Memory, e3.sink.get_port_id());
                  if (nm == "bits") {
                    mb = hydrate_const(e3.driver).to_just_i64();
                  } else if (nm == "size") {
                    ms = hydrate_const(e3.driver).to_just_i64();
                  }
                }
                if (mb > 0 && ms > 0) {
                  bits2 = static_cast<int>(mb * ms);
                }
              }
              // Follow the dout pin's sign, exactly like the Sub-output net
              // below. upass.tolg stamps every memory read `set_unsign` unless
              // the element type is signed, and it no longer wraps the read in
              // a to_positive Get_mask, so a `signed` net here would make
              // Verilog sign-extend unsigned memory data (`mem[i] + 1` with
              // 8'hff came out 0 instead of 256).
              const bool dout_unsigned = is_unsign(dout);
              if (dout_unsigned) {
                pin2var_unsigned_.insert(dout.get_class_index());
              }
              const char* dout_decl
                  = is_array_mem ? (dout_unsigned ? "reg " : "reg signed ") : (dout_unsigned ? "wire " : "wire signed ");
              if (bits2 <= 1) {
                fout->append(dout_decl, name2, ";\n");
              } else {
                fout->append(dout_decl, "[", std::to_string(bits2 - 1), ":0] ", name2, ";\n");
              }
            }
          }
          continue;
        }
        absl::flat_hash_set<hhds::Port_id> declared_sub_outputs;
        for (const auto& output_edge : node.out_edges()) {
          const auto pid = output_edge.driver.get_port_id();
          if (!declared_sub_outputs.insert(pid).second) {
            continue;
          }
          // Re-fetch the canonical driver handle (driver bit set) so this keys
          // pin2var identically to the edge.driver a consumer's inp_edges loop
          // uses. Iterate out_edges rather than out_pins: HHDS out_pins can omit
          // a multi-driver node's pid-0 driver, leaving the instance connection
          // to create an implicit one-bit Verilog wire and silently truncate the
          // whole Sub output.
          auto cdpin = node.create_driver_pin(pid);
          // Use a DEDICATED net name (like the Memory dout above), never the wire
          // name: a Sub output that drives a module output directly is otherwise
          // named after that port, and declaring it here re-declares the port
          // (illegal — `Incompatible re-declaration of wire`; also an instance
          // output cannot legally drive an `output reg`). create_outputs then
          // emits `<port> = <iname>_o<pid>;` like any other driver.
          // De-collide: `default_instance_name` is the instance's NAME when it
          // has one, so two same-named instances (a generate loop, or the
          // occurrences an `upass.roll` expansion produces) would otherwise
          // declare and drive one shared net per port instead of one each.
          // Claim the slot FIRST: get_unique_decl_name permanently reserves the
          // name it hands back, so computing it for a pin another site already
          // bound would burn a `_cgenN` counter on a name never declared.
          if (!pin2var.contains(cdpin.get_class_index())) {
            auto name2 = get_unique_decl_name(get_scaped_name(absl::StrCat(default_instance_name(node), "_o", pid)));
            pin2var.insert({cdpin.get_class_index(), name2});
            const bool out_unsigned = is_unsign(cdpin);
            if (out_unsigned) {
              pin2var_unsigned_.insert(cdpin.get_class_index());
            }
            int bits2 = bits_of(cdpin);
            if (bits2 <= 1) {
              fout->append(out_unsigned ? "wire " : "wire signed ", name2, ";\n");
            } else {
              fout->append(out_unsigned ? "wire [" : "wire signed [", std::to_string(bits2 - 1), ":0] ", name2, ";\n");
            }
          }
        }
      }
      continue;
    }
    I(op != Ntype_op::Sub && op != Ntype_op::Memory);

    if (op == Ntype_op::Clock_cell) {
      auto dpin = node.create_driver_pin(0);
      if (!pin2var.contains(dpin.get_class_index())) {
        const auto out_name = get_unique_decl_name(get_scaped_name(pin_wire_name(dpin)));
        const auto lat_name = get_unique_decl_name(get_scaped_name(absl::StrCat(out_name, "__en_latched")));
        pin2var.emplace(dpin.get_class_index(), out_name);
        pin2var_unsigned_.insert(dpin.get_class_index());
        clock_latch_vars_.emplace(node.get_class_index(), lat_name);
        note_src(fout, node);
        fout->append("wire ", out_name, ";\n");
        fout->append("reg ", lat_name, ";\n");
      }
      continue;
    }

    if (!node.has_out_edges() && !is_type_register(node)) {
      continue;
    }
    // A flop whose Q has no readers is still emitted by create_registers (it always
    // assigns `q <= ___next_q`), so it MUST get its `reg` declaration here too —
    // otherwise the netlist references an undeclared name (e.g. a_exmem_valid).
    // Same for a LATCH (2f-latch M1): create_registers calls process_latch
    // unconditionally, so a reader-less latch Q used to have its assignment
    // emitted with no declaration to go with it. is_type_register covers both;
    // Memory cannot reach this line (asserted just above).

    auto        dpin         = node.get_driver_pin(0);
    std::string name         = get_scaped_name(pin_wire_name(dpin));
    bool        out_unsigned = is_unsign(dpin);
    if (op == Ntype_op::Concat) {
      // A Concat is the one cell whose result is non-negative BY CONSTRUCTION:
      // every lane masks into its own window, so no lane's sign can escape, and
      // the value is the sum-of-windows in [0, 2^sum(w)). The pin is stamped
      // bits = sum(w) and `unsign` at creation. Force it here rather than
      // trusting stale metadata because a signed destination would reinterpret
      // a top-lane all-ones value as negative.
      out_unsigned = true;
    }

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
          // Sext changes how the finite input is interpreted only after the
          // input has reached the requested width.  Preserve the producer's
          // literal signedness here: declaring an unsigned u1 predicate as a
          // signed scalar turns 1 into -1 before a wider Sext assignment and
          // incorrectly fills every high bit with ones.
          bool        out_unsigned2 = is_unsign(dpin2);
          add_to_pin2var(fout, dpin2, name2, out_unsigned2);
        }
      }
      auto nname = node_name_of(node);
      if (!nname.empty() && nname.front() != '_') {
        continue;
      }
    } else if (op == Ntype_op::Set_mask) {
      // Set_mask preserves the realization hint computed from its base/value.
      // Forcing every partial-write accumulator signed made a narrowed u13
      // carrier sign-extend into a u16 output when bit 12 was set.
      add_to_pin2var(fout, dpin, name, out_unsigned);
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
        // A Get_mask changes the signedness of its RESULT, not its source.
        // Preserve the source hint: forcing an unsigned u1 comparator into a
        // signed scalar made true read as -1 before a later zero-extension.
        add_to_pin2var(fout, a_dpin, name2, is_unsign(a_dpin));
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

  // Second pass — a shift's AMOUNT operand must never be an inlined expression.
  //
  // The RHS of `<<` / `>>` / `>>>` is the one binary operand Verilog
  // SELF-determines: it does NOT inherit the expression's context width. A
  // declared variable (declared at bits_of, `signed` unless the pin is unsigned)
  // or a constant literal carries its full width and signedness there, but an
  // expression folded inline is evaluated at its own natural Verilog width —
  // max(operand widths) — and WRAPS.
  //
  // That silently truncated the length of a runtime bit-range select. For
  // `a#[base ..= base+len]` (and the `(1 << (len+1)) - 1` mask a Verilog round
  // trip produces) the amount is a Sum whose LiveHD pin is 5 bits (0..8) while
  // its inlined text `(get_mask_32_u + (2'sh1))` was only max(3,2) = 3 bits, so
  // len==7 shifted by 0 and the mask came out 0 — z read 0 where the golden kept
  // bits (witness a=0x100 base=7 len=7: gold 2, gate 0).
  //
  // Landing the amount in its own variable — rather than padding the text in
  // place — is what makes this correct for BOTH signednesses: a Verilog
  // assignment evaluates its RHS at the destination's width, and the declaration
  // decides whether the extension is zero or sign. An in-place `({N{1'b0}} | …)`
  // pad (the idiom the SHL *left* operand uses) forces the amount unsigned, which
  // zero-extends a negative index: `a[$signed(b) +: 4]` lowers to
  // `(a<<4) >>> (sext(b)+4)` and b = 3'b111 must shift by 3, not by 11.
  //
  // Runs as its own pass so it sees the FINAL pin2var: a Get_mask amount, a named
  // amount, or a fanout>=2 amount is already declared above and is left alone.
  for (auto node : graph->body().nodes()) {
    auto op = type_op_of(node);
    if (op != Ntype_op::SHL && op != Ntype_op::SRA) {
      continue;
    }
    if (!node.has_out_edges()) {
      continue;  // dead shift: no statement is emitted for it
    }
    auto amt_dpin = get_driver(find_sink_pin(node, "b"));
    if (amt_dpin.is_invalid() || is_const_pin(amt_dpin) || pin2var.contains(amt_dpin.get_class_index())) {
      continue;
    }
    // A pin the first pass parked in pin2expr (AttrSet, an inlined const) has NO
    // assignment emitter: build_simple_expr returns {} for it, so declaring a
    // variable here would mint a wire nothing ever drives — and get_expression
    // prefers pin2var over pin2expr, so the shift would read that dangling net
    // (x in sim, an unconstrained amount after synthesis) instead of the alias
    // text. Leave those alone; the pre-existing inlined text is still correct
    // for them, just not width-declared.
    if (pin2expr.contains(amt_dpin.get_class_index())) {
      continue;
    }
    add_to_pin2var(fout, amt_dpin, get_scaped_name(pin_wire_name(amt_dpin)), is_unsign(amt_dpin));
  }

  // Third pass — a CLOCK (and a flop RESET) must never be an inlined
  // expression either.
  //
  // A clock lands in an EDGE EVENT CONTROL (`always @(posedge <x>)`) and in the
  // cgen_memory_* wrapper's `.clk()` port. Verilog accepts an expression there,
  // but OUR OWN front end does not: inou.slang's lower_ff_process takes only a
  // plain NamedValue and hard-errors `unsupported-clock` ("the clock must be a
  // plain signal") on anything else. So a DERIVED clock used once -- the shape
  // `gclk = clk_b and gate` in tests/equiv/mclk_derived, whose only reader is
  // the flop's clock_pin, hence fanout 1 -- was left undeclared by the first
  // pass, parked in pin2expr, and emitted as `always @(posedge (clk_b & gate))`:
  // Verilog that LiveHD could not read back. The memory paths are worse, since
  // get_wire_or_const ignores pin2expr entirely and emits a BARE UNDECLARED
  // name. Give the clock its own net; a hand-written golden spells it that way
  // too (`wire gclk; assign gclk = clk_b & gate;`).
  //
  // A flop RESET has the identical hazard, and the same two consumers: an
  // async reset lands in the edge event (`or posedge <x>`), and the sync level
  // test reads it through get_wire_or_const — which ignores pin2expr and emits
  // a bare undeclared name. A DERIVED reset used once is now an ordinary shape:
  // a reg array's reset-restore sweep counter parks on `!reset` (upass.tolg
  // build_restore_sweep), and in a memory-only design that inverter's single
  // reader is this pin. It emitted `if (eq_32)` against no such net.
  // (A Memory's own whole-array `reset` needs nothing here: every Sub/Memory
  // input driver is force-declared above.)
  //
  // Runs as its own pass so it sees the FINAL pin2var: a module-input clock or
  // reset, a flop-Q clock divider or a fanout>=2 driver is already declared
  // above and is left alone.
  for (auto node : graph->body().nodes()) {
    const auto op = type_op_of(node);
    if (!is_type_register(node) && op != Ntype_op::Memory) {
      continue;
    }
    for (const auto& e : node.inp_edges()) {
      const auto pin_name = Ntype::get_sink_name(op, e.sink.get_port_id());
      if (!str_tools::ends_with(pin_name, "clock_pin") && pin_name != "reset_pin") {
        continue;
      }
      auto ctl_dpin = e.driver;
      if (ctl_dpin.is_invalid() || is_const_pin(ctl_dpin) || pin2var.contains(ctl_dpin.get_class_index())) {
        continue;  // tied off, or already a declared net / module input
      }
      // Same hazard the second pass argues above: a pin parked in pin2expr has
      // NO assignment emitter, so declaring it would mint a net nothing drives
      // -- and get_expression prefers pin2var, so the flop would then clock off
      // a dangling `x`. Leave those with their inline text.
      if (pin2expr.contains(ctl_dpin.get_class_index())) {
        continue;
      }
      // A Clock_cell has no Verilog lowering at all and create_registers raises
      // a LOUD fatal for it. Declaring one here would replace that fatal with a
      // silent undriven net -- the dropped-clock-gate miscompile class.
      if (type_op_of(ctl_dpin.get_master_node()) == Ntype_op::Clock_cell) {
        continue;
      }
      add_to_pin2var(fout, ctl_dpin, get_scaped_name(pin_wire_name(ctl_dpin)), is_unsign(ctl_dpin));
    }
  }

  // Fourth pass — a Concat LANE VALUE must never be an inlined expression.
  //
  // Verilog SELF-DETERMINES every concatenation operand, so build_simple_expr
  // has to place each lane at EXACTLY its declared window width `w` (a
  // part-select when the operand is wider, a sized pad when it is narrower) or
  // every lane above it shifts. That adjustment needs the operand's width to be
  // KNOWN, and only two spellings carry one: a constant literal (folded in
  // place) and a DECLARED net (declared at bits_of -- see add_to_pin2var, which
  // decl_bits_of tracks). An expression folded inline instead self-determines at
  // its own natural Verilog width -- max over its operands -- which is neither
  // bits_of nor `w`, and nothing in the emitted text would reveal the mismatch.
  //
  // Runs as its own pass, and by construction BEFORE process_simple_node fills
  // pin2expr: that is what makes a Concat feeding another Concat's lane land in
  // a net of its own rather than as an inlined `{...}` of unknown width.
  for (auto node : graph->body().nodes()) {
    if (type_op_of(node) != Ntype_op::Concat) {
      continue;
    }
    if (!node.has_out_edges()) {
      continue;  // dead concat: no statement is emitted for it
    }
    for (const auto& lane : livehd::graph_util::concat_lanes(node)) {
      const auto& v = lane.value;
      if (v.is_invalid() || is_const_pin(v) || pin2var.contains(v.get_class_index())) {
        continue;  // folded to a literal, or already a declared net / module input
      }
      // Same hazard the second and third passes argue above: a pin parked in
      // pin2expr has NO assignment emitter, so declaring it would mint a net
      // nothing drives -- and get_expression prefers pin2var over pin2expr, so
      // the lane would read that dangling net instead of the alias text.
      if (pin2expr.contains(v.get_class_index())) {
        continue;
      }
      add_to_pin2var(fout, v, get_scaped_name(pin_wire_name(v)), is_unsign(v));
    }
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

  // Break a false comb loop through a pure-comb sub-instance. Packed-wire
  // self-references are already resolved locally by lnast.tolg when the wire's
  // defining edge is attached; writers must not mutate that graph afterward.
  // Native loop groups remain compact in the graph; create_subs realizes their
  // logical calls exclusively in private emission maps.
  livehd::graph_util::flatten_false_loop_subs(graph.get());

  pin2var.clear();
  pin2var_unsigned_.clear();
  pin2expr.clear();
  mux2vector.clear();
  declared_name_counts.clear();
  sub_instance_names_.clear();
  loop_instance_names_.clear();
  loop_output_vars_.clear();
  loop_output_unsigned_.clear();  // written and read as a pair with loop_output_vars_
  loop_input_exprs_.clear();
  clock_latch_vars_.clear();
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
  fout->append("module ", get_scaped_name(flat_module_name(graph->get_name())), "(\n");

  hhds::Graph* g = graph.get();
  create_module_io(fout, g);

  reserve_instance_names(g);
  create_locals(fout, g);
  create_memories(fout, g);
  create_subs(fout, g);
  create_clock_cells(fout, g);

  create_combinational(fout, g);
  create_outputs(fout, g);
  create_registers(fout, g);

  note_module(fout);
  fout->append("endmodule\n");

  write_srcmap(fout, filename, graph->source_locator());

  --nrunning;
}
