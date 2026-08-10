// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_sim.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <print>
#include <string>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "cell.hpp"            // Ntype / Ntype_op
#include "diag.hpp"            // livehd::diag::err — Stage 0 comb-loop safety net
#include "inline_sub.hpp"      // //graph — sim.flatten structural inline of a small sub-instance
#include "latch_contract.hpp"  // //graph — inline_clock_gate_cells (ICG gate -> local AND cone)
#include "node_util.hpp"
#include "occurrence_materialize.hpp"  // //graph — realize native loop groups in the private simulator library
#include "split_selfref.hpp"           // //graph — word-level false-loop splitter (also run by pass/cprop)
#include "str_tools.hpp"               // str_tools::ends_with

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

// EVERY port's clock driver. A Memory carries `<n>clock_pin` per port, so a
// consumer that keeps ONE answer per array (the sim's array-wide write guard)
// has to look at all of them before it can trust the first.
std::vector<hhds::Pin_class> memory_clock_drivers_of(const hhds::Node_class& node) {
  std::vector<hhds::Pin_class> out;
  for (const auto& e : node.inp_edges()) {
    const auto pn = Ntype::get_sink_name(Ntype_op::Memory, static_cast<int>(e.sink.get_port_id()));
    if (str_tools::ends_with(pn, "clock_pin")) {
      out.push_back(e.driver);
    }
  }
  return out;
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

// ---- Dead-temporary sweep over ONE finished method body ----
//
// Binding is demand-driven and cone-based, so a temporary can be emitted for a
// value the method never reads: the settle pass walks a Moore-deferred
// instance's whole input cone (build_settle_cone traverses the Sub) but only
// the RISE emits that instance's `cycle(in)` call, and a group-scheduled slice
// read binds a pin whose consumer turns out to live in a different pass. The
// host compiler drops them at -O, but they still cost compile time and raise
// -Wunused-variable on every one.
//
// A dead line's operands can die with it (dropping the last reader of a group
// snapshot strands the snapshot too), so iterate to a fixpoint. A PURE
// initializer is deleted outright; an IMPURE one keeps its call as a discarded
// statement -- a memory read resolves through Slop::unknown when the address is
// not a plain integer or an `ordering="none"` write collides, and that is a
// draw from the seeded process PRNG.
//
// Every name matched here is a block-scope local of this method, so "used
// nowhere else in the body" is exactly "dead".

// The name a line DECLARES, or empty when the line is not a declaration this
// sweep owns. Covers the three forms the emitter mints: `Slop<W> v = e;` (also
// the `Slop<W> v{e};` width-expression spelling) and `auto v = e;`.
std::string_view declared_temp(std::string_view line) {
  auto rest = line.substr(std::min(line.find_first_not_of(" \t"), line.size()));
  if (rest.starts_with("Slop<")) {
    const auto gt = rest.find('>');  // the width is an integer literal, never nested
    if (gt == std::string_view::npos) {
      return {};
    }
    rest = rest.substr(gt + 1);
  } else if (rest.starts_with("auto ")) {
    rest = rest.substr(5);
  } else {
    return {};
  }
  rest = rest.substr(std::min(rest.find_first_not_of(" \t"), rest.size()));
  if (rest.empty() || (std::isalpha(static_cast<unsigned char>(rest.front())) == 0 && rest.front() != '_')) {
    return {};
  }
  size_t n = 1;
  while (n < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[n])) != 0 || rest[n] == '_')) {
    ++n;
  }
  // Must be the whole declarator: an initializer follows immediately.
  auto tail = rest.substr(n);
  tail      = tail.substr(std::min(tail.find_first_not_of(" \t"), tail.size()));
  if (!tail.starts_with('=') && !tail.starts_with('{')) {
    return {};
  }
  return rest.substr(0, n);
}

// A single-line declaration whose initializer has no side effect. Child calls
// (`cycle`/`__settle`) and memory reads are the exceptions; staged writes are
// statements, never initializers, but reject them too so a future emission
// shape cannot silently become removable.
bool pure_temp_decl(std::string_view line) {
  const auto code = line.substr(0, line.find("//"));
  if (code.find(';') == std::string_view::npos) {
    return false;  // not a complete statement on this line
  }
  for (auto bad : {".read<", ".read(", ".read_all(", ".cycle(", "__settle", "slop_update", "apply_update"}) {
    if (line.find(bad) != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

// A dead call cannot be deleted (see above), but it can stop DECLARING the
// value nobody reads: `Slop<W> v = mem.read<r>(a);` becomes the plain statement
// `mem.read<r>(a);`, which keeps the side effect and lets the host compiler
// drop the rest. Empty when the initializer is not a plain `= <call>;` (the
// `{}` spelling is only ever a pure Slop expression, deleted outright).
std::string discard_dead_call(std::string_view line, std::string_view name) {
  const auto eq = line.find(" = ", line.find(name));
  if (eq == std::string_view::npos) {
    return {};
  }
  auto rhs = line.substr(eq + 3);
  while (!rhs.empty() && (rhs.back() == '\n' || rhs.back() == '\r')) {
    rhs.remove_suffix(1);
  }
  const auto tag = rhs.find("//") == std::string_view::npos ? "  // value unused" : " (value unused)";
  return absl::StrCat("    ", rhs, tag, "\n");
}

void strip_dead_temps(std::string& body) {
  std::vector<std::string> lines;
  for (size_t pos = 0; pos < body.size();) {
    const auto nl  = body.find('\n', pos);
    const auto end = nl == std::string::npos ? body.size() : nl + 1;
    lines.emplace_back(body, pos, end - pos);
    pos = end;
  }

  std::vector<bool> alive(lines.size(), true);
  bool              changed = true;
  bool              any     = false;
  while (changed) {
    changed = false;
    absl::flat_hash_map<std::string_view, int> uses;  // identifier -> occurrences in the LIVE body
    for (size_t i = 0; i < lines.size(); ++i) {
      if (!alive[i]) {
        continue;
      }
      const std::string_view l = lines[i];
      for (size_t p = 0; p < l.size();) {
        if (std::isalpha(static_cast<unsigned char>(l[p])) == 0 && l[p] != '_') {
          ++p;
          continue;
        }
        size_t n = p + 1;
        while (n < l.size() && (std::isalnum(static_cast<unsigned char>(l[n])) != 0 || l[n] == '_')) {
          ++n;
        }
        ++uses[l.substr(p, n - p)];
        p = n;
      }
    }
    for (size_t i = 0; i < lines.size(); ++i) {
      if (!alive[i]) {
        continue;
      }
      const auto name = declared_temp(lines[i]);
      if (name.empty() || uses[name] != 1) {
        continue;
      }
      if (pure_temp_decl(lines[i])) {
        alive[i] = false;
        changed  = true;
        any      = true;
        continue;
      }
      if (auto discarded = discard_dead_call(lines[i], name); !discarded.empty()) {
        lines[i] = std::move(discarded);
        changed  = true;
        any      = true;
        break;  // `uses` keys point INTO the line just replaced: recount before reusing them
      }
    }
  }
  if (!any) {
    return;
  }
  std::string out;
  out.reserve(body.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    if (alive[i]) {
      out.append(lines[i]);
    }
  }
  body = std::move(out);
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

// Can a Get_mask WIDTH-ADJUST of `drv` to `wbits` be dropped and the source
// passed through RAW? Two conditions, both necessary:
//
//   UNSIGNED — the value is already non-negative, so making it positive and
//   the trailing trim are both the identity. A signed source keeps the
//   unsigned read: to-positive of a negative value must mask.
//
//   NOT WIDER than the node — raw_operand's contract is that the string it
//   hands out is exact AT ITS DECLARED WIDTH, and the ops it feeds are
//   width-templated, so they take the operand at the source's own width. A
//   source wider than `wbits` still holds live bits above the node's declared
//   width, and dropping the adjust would let them into a downstream eq_op /
//   and_op / fold that the `.zext_to<wbits>()` read had masked them out of.
//   Narrower is fine: the value is exact and non-negative, so every later
//   landing (zext read, or the sign-extending cross-width ctor) reproduces it.
bool Cgen_sim::raw_width_adjust_ok(const hhds::Pin_class& drv, int wbits) {
  if (!is_unsign(drv)) {
    return false;
  }
  const int sw = wbits_of(drv);
  if (sw > wbits) {
    return false;
  }
  if (sw < wbits) {
    sub_width_expr_ = true;  // the caller's temp declaration must brace-init
  }
  return true;
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
          return absl::StrCat("Slop<",
                              tw,
                              ">::",
                              is_shl ? "shl_op" : "sra_op",
                              "(",
                              raw_operand(e[0].driver, wbits),
                              ", ",
                              amt.to_just_i64(),
                              ")");
        }
      }
      // Runtime amount: keep the member form (it reads amount.base_[0]).
      // The shifted operand of an ARITHMETIC shift must be read as signed.
      return absl::StrCat(operand(e[0].driver, wbits, is_shl ? 0 : /*signed=*/1),
                          ".",
                          is_shl ? "shl_op" : "sra_op",
                          "(",
                          operand(e[1].driver, wbits),
                          ")");
    }
    case Ntype_op::Get_mask: {
      // value (e[0]) + optional mask (e[1]). The unary form is the common tolg
      // width-adjust; lower it to a plain zext.
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      if (e.size() == 1) {
        if (raw_width_adjust_ok(e[0].driver, wbits)) {
          return raw_operand(e[0].driver, wbits);
        }
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
            if (raw_width_adjust_ok(e[0].driver, wbits)) {  // same raw pass-through as the unary arm above
              return raw_operand(e[0].driver, wbits);
            }
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
            auto [mb, me]       = mv.get_mask_range();  // half-open; {-1,-1} = noncontiguous
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
              // The trailing widen is replaceable by the cross-width ctor
              // (`Slop<tw>{expr}` — one landing instead of truncate-then-
              // widen) ONLY when the expression's top STORED bit is provably
              // clear: the ctor SIGN-extends from the source's top bit where
              // zext_to zero-extends, so a plain truncation (mb == 0) whose
              // value uses bit me-1 would flip negative (broke 9 simeq
              // goldens, bit_sel_sext first). After `.sra_op(mb)` with mb > 0
              // the top mb bits are zero, so bit me-1 is clear and the two
              // agree; me-mb < tw keeps the landed value inside tw's positive
              // range. mb == 0 keeps the explicit zext pair.
              if (mb > 0 && me - mb < static_cast<int64_t>(wbits)) {
                return absl::StrCat("Slop<", tw, ">{", expr, "}");
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

      // A positive contiguous CONSTANT mask is the packed-field-write shape.
      // Spell the splice as fixed-width bitwise operations
      // instead of calling set_mask_op(): the generic method has to discover
      // the range (get_bits + ctz/clz + a contiguity scan) on every execution,
      // even though cgen already knows it here. Minion emits thousands of these
      // tiny Set_mask cells (many Slop<2>); their range discovery was a large
      // fraction of whole-design simulation time.
      //
      // Keep the optimization deliberately narrow and proof-friendly:
      //   * is_just_i64() means the positive mask is exact in create_integer;
      //   * me <= wbits means no selected bit is clipped by the node width;
      // Multi-word results use the same algebra; clang unrolls the fixed-size
      // Slop operations and cgen still avoids rediscovering the mask range.
      if (is_const_pin(e[1].driver)) {
        const auto mv = hydrate_const(e[1].driver);
        if (!mv.has_unknowns() && !mv.is_negative() && mv.is_just_i64()) {
          const auto [mb, me] = mv.get_mask_range();  // half-open; {-1,-1} = noncontiguous
          if (mb >= 0 && me > mb && me <= wbits) {
            const auto base = operand(e[0].driver, wbits, -1);
            const auto mask = operand(e[1].driver, wbits, -1);
            const auto val  = operand(e[2].driver, wbits);
            if (mb == 0 && me == wbits) {
              return val;  // every result bit is replaced
            }
            const auto inserted = mb == 0 ? val : absl::StrCat("Slop<", tw, ">::shl_op(", val, ", ", mb, ")");
            return absl::StrCat("Slop<",
                                tw,
                                ">::or_op(Slop<",
                                tw,
                                ">::and_op(",
                                base,
                                ", Slop<",
                                tw,
                                ">::not_op(",
                                mask,
                                ")), Slop<",
                                tw,
                                ">::and_op(",
                                inserted,
                                ", ",
                                mask,
                                "))");
          }
        }
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
      const size_t n_vals = e.size() - 1;
      if (op == Ntype_op::Mux && n_vals == 2) {
        // A 2-arm Mux selector is a CONDITION, not an index: ANY nonzero value
        // selects arm 1. That matches cgen_verilog (`sel ? arm1 : arm0`, whose
        // netlist `lhd lec` proves) and the LGraph model, where an `if` cone
        // hands the Mux whatever the condition computed -- `a & 0x80` arrives
        // as 128, not as 1 (a low-bit truncation here silently took arm 0 for
        // any nonzero-but-even condition; the nonzero test maps both the
        // all-ones boolean and 128 to 1).
        //
        // Emitted as a C++ TERNARY with the selector read RAW at its own
        // width. Two costs die at once: the old form materialized the
        // selector at the RESULT width plus a second result-width 0/1 Slop
        // (two wide temporaries of pure ceremony — the largest zext
        // population in the dino emission), and ANY call-based mux
        // evaluates BOTH arms eagerly — with single-use forestation the
        // arms are whole inlined expression trees, so lazy arm evaluation
        // is the point, exactly like cgen_verilog's `sel ? a : b`. Both
        // arm strings come from operand(..., wbits), which always lands
        // them at Slop<tw>, so the ternary's types agree.
        //
        // Returned BEFORE the arm list / selector below are built: with
        // forestation each arm is a whole inlined tree, and the call form's
        // `vals` + result-width `sel` would be built and thrown away for every
        // one of the 2-arm muxes that dominate a real design.
        return absl::StrCat("((",
                            raw_operand(e[0].driver, std::max(wbits_of(e[0].driver), 1)),
                            ").is_known_true() ? ",
                            operand(e[2].driver, wbits),
                            " : ",
                            operand(e[1].driver, wbits),
                            ")");
      }

      if (op == Ntype_op::Hotmux) {
        // A Hotmux selector is a one-hot BIT VECTOR, not a value at the
        // result width.  A wide decode table can therefore select a 1-bit
        // result (for example, `id_vpu_insn`) with bit 100+.  Reading the
        // selector as Slop<tw> discarded every arm above tw and silently
        // selected invalid/default.  The emitted mixed-width helper preserves
        // the selector's declared width without widening every data arm.
        const int   sel_w = std::max(wbits_of(e[0].driver), 1);
        std::string vals;
        for (size_t i = 1; i < e.size(); ++i) {
          if (!vals.empty()) {
            vals += ", ";
          }
          vals += operand(e[i].driver, wbits);
        }
        const auto sel = operand(e[0].driver, sel_w, /*unsigned=*/-1);
        return absl::StrCat("__lhd_hotmux<", tw, ">(", sel, ", {", vals, "})");
      }

      std::string vals;
      for (size_t i = 1; i < e.size(); ++i) {
        if (!vals.empty()) {
          vals += ", ";
        }
        vals += operand(e[i].driver, wbits);
      }
      std::string sel   = operand(e[0].driver, wbits);  // Slop<tw> selector
      // 3+ arms: the selector IS an index (0..n-1). Keep only the
      // ceil(log2(n)) low index bits, then re-widen to tw.
      int         sel_w = 1;
      while ((static_cast<size_t>(1) << sel_w) < n_vals) {
        ++sel_w;
      }
      if (sel_w < wbits) {
        sel = absl::StrCat("(", sel, ").zext_to<", sel_w, ">().zext_to<", wbits, ">()");
      }
      return absl::StrCat("Slop<", tw, ">::mux_op(", sel, ", {", vals, "})");
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

// Same boundary question for a compact loop, excluding its descriptor carry
// self-edges. Any remaining path from an external input driver back to this
// node is a parent-level false ring; the native eager wrapper deliberately does
// not solve those and the caller uses occurrence expansion as its backstop.
bool compact_loop_has_external_ring(const hhds::Node_class& s) {
  namespace gu = livehd::graph_util;
  absl::flat_hash_set<hhds::Node_class> seen;
  std::vector<hhds::Pin_class>          stk;
  for (const auto& e : s.inp_edges()) {
    if (e.driver.get_master_node() != s) {
      stk.push_back(e.driver);
    }
  }
  while (!stk.empty()) {
    auto d = stk.back();
    stk.pop_back();
    if (d.is_invalid() || gu::is_const_pin(d)) {
      continue;
    }
    auto n = d.get_master_node();
    if (n == s) {
      return true;
    }
    const auto op = gu::type_op_of(n);
    if (gu::is_type_register(n) || op == Ntype_op::Memory || op == Ntype_op::IO) {
      continue;
    }
    if (!seen.insert(n).second) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      stk.push_back(e.driver);
    }
  }
  return false;
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
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
      continue;  // true state boundary: q is last period's value
    }
    if (op == Ntype_op::Memory) {
      // NOT a blanket boundary: an ASYNC read's dout is COMB in the read
      // ADDRESS (and, under write-forwarding orderings, in the write cones).
      // `rf_q[raddr]` is exactly how dino's register file reaches its read
      // data, and classifying the cell as state called that callee Moore —
      // its outputs were pre-bound from LAST period's addresses, a silent
      // one-period-stale miscompile (bench dino_prog read x2=0 forever).
      // Walk THROUGH the cell: every sink driver joins the cone. That is
      // conservative for a sync read or a read-first ordering (their douts
      // are input-independent), which only under-defers, never miscompiles.
      if (!seen.insert(m).second) {
        continue;
      }
      for (auto e : m.inp_edges()) {
        stk.push_back(e.driver);
      }
      continue;
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
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
        continue;  // true state boundary: q is last period's value
      }
      if (op == Ntype_op::Memory) {
        // Same rule as callee_is_moore above: an async dout is comb in the
        // read address, so the memory's own input cone joins this output's.
        if (!seen.insert(m).second) {
          continue;
        }
        for (auto e : m.inp_edges()) {
          stk.push_back(e.driver);
        }
        continue;
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

// Ports of `def` that carry a TICK GUARD — the recursive form of the question
// `clock_in_fields` used to answer locally. An input port is a guard port when
// state anywhere in the def's SUBTREE commits on a clock derived from it:
//   (a) a local state element's clock_pin resolves to the port (directly,
//       through width wrappers, or as the ROOT of a local gate cone), or
//   (b) the port forwards — again possibly through a gate — into a CHILD's
//       guard port.
// (b) is what a stateless pass-through wrapper needs: minion's txfma chain
// crosses THREE such boundaries between the gate (vpu_lane's cgate_txfma) and
// the flops it withholds (txfmafrac's stages), and before this went recursive
// the wrapper declared no tick field, the parent's gate had nowhere to write,
// and the leaf committed every tick — silently (tests/sim/gate_through_wrapper
// is the anchor). The shared analysis also drives conditional-activation gate
// insertion and gate-cell recognition, so all three consumers agree.
//
// The cache is a PARAMETER, never a static: latch_contract makes it
// caller-owned because graph preparation inlines gate cells and materializes
// occurrences, so an answer computed before a rewrite must not outlive it. A
// process-lifetime memo keyed by bare `const hhds::Graph*` would also survive
// the scratch library it was built from, and a later emission allocating a
// graph at that freed address would read back the previous design's ports.
const absl::flat_hash_set<uint32_t>& clock_guard_ports(const std::shared_ptr<hhds::Graph>&       def,
                                                       livehd::latch_contract::Clock_port_cache& cache) {
  return livehd::latch_contract::clock_input_ports(def, cache);
}

const livehd::latch_contract::Reset_input_ports& reset_guard_ports(const std::shared_ptr<hhds::Graph>&       def,
                                                                   livehd::latch_contract::Clock_port_cache& cache) {
  return livehd::latch_contract::reset_input_ports(def, cache);
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
  for (auto node : g->body().nodes()) {
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
    for (auto node : g->body().nodes()) {
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
// simgen-6: unique struct-member names for repeated instance/memory names,
// state-only prebind for any Sub on the schedule cycle, on-demand memory
// emission (emit_memory), fixpoint gate-cell/false-loop inlining pre-steps.
// simgen-7: callee Moore/state-only classification walks THROUGH a Memory
// (async dout is comb in the read address — the dino register-file stale-read
// miscompile); simgen-6 artifacts classified such callees as Moore.
// simgen-8: recursive clock-guard ports — `<field>__tick` is declared for
// every input port whose SUBTREE clocks state on it (stateless wrappers
// included) and FORWARDED at call sites, composing with local gate folds.
// simgen-9: callee partitioning — per-output-group `__settle_g<k>` methods on
// split defs, group-scheduled parents, and NO false-loop inlining.
// simgen-11: single-use forestation (a fan-out-1 comb node binds as a pasted
// expression, not a `cg_N` temp), 2-arm muxes as a lazy C++ ternary, guarded
// next-state and lazy write staging on gated state, and the Get_mask raw
// width-adjust pass-through.
// simgen-15: native rolled-loop ABI. The digest includes the complete HHDS
// Subnode_loop descriptor so a warm workdir cannot reuse code for a different
// count/domain/role mapping.
// simgen-16: dead-temporary sweep — a local no statement in the method reads is
// dropped (with its now-dead operands) instead of emitted; a dead call keeps
// its side effect as a discarded statement.
// simgen-17: conditional Sub calls execute behind their structural __valid OR
// reset guard, so an inactive hierarchy costs one branch instead of a full
// evaluation whose clock commits are merely disabled at the end.
// simgen-18: activation-bearing Subnode_loop descriptors stay native. Their
// std::array wrapper carries the active recurrence and emits a runtime if per
// ordinal instead of materializing one physical Sub per source iteration.
// simgen-19: a callee's forwarded top-level __valid is already enforced at its
// activation boundary. Do not redundantly wrap every internal child/group in
// that same guard (which can also hide recursively scheduled declarations in a
// nested C++ scope); only a locally composed path guard creates a runtime if.
// simgen-24: after the ordinary period settle has propagated post-rise inputs
// down the hierarchy, refresh mixed-phase children's negedge state for the
// next period. This preserves the established rise schedule while preventing
// a nested negedge preview from retaining the prior cycle's input. Preserve a
// Hotmux's full one-hot selector width independently of its result width. Let
// reset_cycle(bool) supply zero for otherwise-uninitialized state (sim.init_zero).
// simgen-40: uninitialized reset-free memories power on zero under both
// policies, and a posclk=false flop on a sole explicit clock net whose
// spelling is not clock-like (Design_clocks::name_looks_like_clock) is
// edge-detected via sec_clock instead of committing once per period.
// simgen-39: group and state-boundary snapshots copy only demanded Slop fields,
// never an entire child Out struct; cyclic children retain their full boundary
// settle while acyclic children defer it to the root walk.
// simgen-35: cycle() returns persistent __last_out by const reference; acyclic
// child calls skip their private trailing settle because the root post-edge
// walk will visit them. Cyclic boundaries retain it to publish post-commit
// state-only outputs that break the parent's settle ring.
// simgen-32: direct constant Set_mask splices also cover multi-word results.
// simgen-26: constant one-word Set_mask cells emit direct scalar splices.
// simgen-25: the negedge refresh is STRUCTURAL (a callee whose only rise-half
// state is a latch) rather than a whitelist of four port names, so every child
// the latch classification moves out of `negedge_only` gets the compensating
// refresh; a compact loop never propagates it (the wrapper declares no
// refresh_negedge); a negedge-only child advanced in the parent's fall reads
// its outputs from `__out`, not from a `cycle()` snapshot it never took; and an
// unknown memory power-on fills per ENTRY instead of through one whole-array
// Slop. The bump is not cosmetic: a warm workdir would otherwise pair a fresh
// parent with a stale child that declares no `refresh_negedge()`.
static constexpr std::string_view kSimGenVersion = "simgen-40";

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
  namespace gu         = livehd::graph_util;
  uint64_t h           = 0xcbf29ce484222325ULL;
  // The emitted shape depends on the SPLIT REGISTRY: this def's own group
  // methods, and (for a parent) the group scheduling of its callees. Fold both
  // so a structural change anywhere that flips a split re-emits the right defs.
  auto     fold_groups = [this](uint64_t hh, const hhds::Graph* def) -> uint64_t {
    if (splits_ == nullptr) {
      return hh;
    }
    auto it = splits_->find(def);
    if (it == splits_->end()) {
      return fnv1a(hh, 0);
    }
    hh = fnv1a(hh, static_cast<uint64_t>(it->second.size()) + 1);
    for (const auto& sg : it->second) {
      for (const auto& o : sg.outs) {
        hh = fnv1a(hh, ((static_cast<uint64_t>(o.pid) << 24) ^ (static_cast<uint64_t>(o.lo) << 10) ^ o.len) * 2 + 1);
      }
      for (const auto& a : sg.support) {
        hh = fnv1a(hh, ((static_cast<uint64_t>(a.pid) << 24) ^ (static_cast<uint64_t>(a.lo) << 10) ^ a.len) * 2);
      }
    }
    return hh;
  };
  h = fold_groups(h, g);
  absl::flat_hash_map<hhds::Class_index, uint32_t> seq;
  uint32_t                                         ni = 0;
  for (auto n : g->body().nodes()) {
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
  for (auto n : g->body().nodes()) {
    auto op = gu::type_op_of(n);
    h       = fnv1a(h, static_cast<uint64_t>(op));
    if (gu::has_name(n)) {
      h = fnv1a_str(h, gu::node_name_of(n));
    }
    if (op == Ntype_op::Sub) {
      auto cg = n.get_subnode_graph();
      h       = fnv1a_str(h, cg ? cg->get_name() : std::string_view{});
      h       = fold_groups(h, cg.get());
      if (auto loop = n.subnode_loop()) {
        h                            = fnv1a(h, 0x4c4f4f50ULL);  // "LOOP": distinguish descriptor absence
        h                            = fnv1a(h, static_cast<uint64_t>(loop->first));
        h                            = fnv1a(h, static_cast<uint64_t>(loop->step));
        h                            = fnv1a(h, loop->count);
        const auto fold_optional_pid = [&](uint64_t hh, const std::optional<hhds::Port_id>& pid) {
          hh = fnv1a(hh, pid ? 1u : 0u);
          return pid ? fnv1a(hh, static_cast<uint64_t>(*pid)) : hh;
        };
        h = fold_optional_pid(h, loop->index_input);
        h = fold_optional_pid(h, loop->activation_input);
        h = fold_optional_pid(h, loop->next_active_output);
      } else {
        h = fnv1a(h, 0);
      }
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
// Returns the guard operand pins, or EMPTY when the cone is not a foldable ICG.
// Empty is "cannot fold", never "no guard needed": the caller must then ask
// `plain_clock_cone` whether the cone was a gate at all, and REFUSE if it was,
// rather than silently commit every tick.
//
// todo_sim_pipeline.md step 1d: this used to be a PRIVATE ICG matcher — the
// FOURTH in the tree (latch_contract.hpp names the other three) — and it cost
// exactly what a duplicated shared answer costs: it hardcoded `pn == "clk"` and
// missed `clk_i` on 7 flops, and it matched only ONE `And` level, so a gate on
// an already gated clock came out with no clock operand at all and the whole
// chain was refused. It is now a thin wrapper over `clock_op_of`, the ONE
// recognizer LEC and the phase schedule already use, plus the two policy checks
// that are genuinely sim's own (below).
std::vector<hhds::Pin_class> Cgen_sim::icg_guards(const hhds::Pin_class& clock_driver, std::string_view clock_port,
                                                  const livehd::latch_contract::Design_clocks* clocks, bool* fall_commit) {
  if (clocks == nullptr || clock_driver.is_invalid()) {
    return {};  // no clock_pin: the module's implicit clock, never gated
  }
  auto cone = livehd::latch_contract::clock_op_of(clock_driver, *clocks);
  if (!cone || cone->enables.empty()) {
    return {};
  }
  // SIM POLICY 2 — a DIVIDER is not the reference clock. One tick is one
  // reference period, so a div-by-N gate does not fold into a per-tick guard
  // until the divider's initial phase is modelled (the named refusal
  // latch_contract.hpp documents for `div != 1`).
  if (cone->div != 1) {
    return {};
  }
  // SIM POLICY 1 — an INVERTED reference (`~clk & en`) gates the FALLING edge.
  // A caller with no fall sub-tick (null `fall_commit`) keeps the refusal: the
  // rise fold would move the commit half a period with no diagnostic. A FLOP
  // endpoint opts in and commits these guards in the FALL pass instead. The
  // cone's clock may itself be a GATED net (`~(clk & e1) & e2` resolves to
  // clock=<the inner gate's output>): the fall edge of that net only exists
  // in a period where the inner gate pulsed, so absorb every inner cone's
  // enables down to the root clock — each enable is held by its cell's own
  // enable latch, so evaluating ALL of them in the fall pass reads each at
  // the phase its latch already pinned (the step-1 per-cell guard records,
  // folded here at the endpoint).
  if (cone->clock_inverted) {
    if (fall_commit == nullptr) {
      return {};
    }
    auto            guards  = cone->enables;
    hhds::Pin_class clk_net = cone->clock;
    for (int hops = 0;
         hops < 4 && !clk_net.is_invalid() && !is_const_pin(clk_net) && !livehd::graph_util::is_graph_input_pin(clk_net);
         ++hops) {
      auto inner = livehd::latch_contract::clock_op_of(clk_net, *clocks);
      if (!inner || inner->div != 1) {
        break;
      }
      if (inner->clock_inverted) {
        return {};  // double inversion: not a shape the two sub-ticks model
      }
      guards.insert(guards.end(), inner->enables.begin(), inner->enables.end());
      clk_net = inner->clock;
    }
    *fall_commit = true;
    return guards;
  }
  // WHICH clock domain this gate belongs to is deliberately NOT re-asked here.
  // `resolve_icg` already identified the cone's clock through `Design_clocks`
  // (a net some flop clocks on, else a conventionally named input), and adding
  // an exact `pin_name_of(clock) == clock_port` test on top of that measured
  // WORSE on minion, not better: it refused cones the shared analysis had
  // correctly resolved. A design with two clock INPUTS whose gate rides the
  // non-reference one is still folded into this tick — a real hole, and the
  // reason step 1a puts the resolved endpoint (root + edge + guards) on one
  // record instead of leaving every consumer to re-derive the domain.
  (void)clock_port;
  return cone->enables;
}

// Is `clock_driver` a PLAIN clock net wearing an identity wrapper — `clk & 1`
// (the slang reader's 1-bit width mask), `clk == 1`, a Get_mask/Sext width
// adjust — rather than a gate? Such a cone needs no guard AND no refusal: it
// ticks exactly when the reference clock does.
//
// The distinction is load-bearing. `resolve_icg` skips constant operands (a
// constant is a mask, not an enable) and then refuses a cone with no enables
// left, so `clk & 1` comes back as "not a foldable ICG" — indistinguishable, to
// a caller that only looks at the guard list, from a genuinely underivable
// clock. Reading that as a refusal cost two modules on minion (intpipe_csr_file,
// prim_mul_div) whose clock cones are masks, not gates.
bool Cgen_sim::plain_clock_cone(const hhds::Pin_class& clock_driver, const livehd::latch_contract::Design_clocks& clocks) {
  if (clock_driver.is_invalid()) {
    return false;
  }
  // A cone the shared recognizer DOES decode as a gate is never "plain", even
  // when this emitter declined to fold it (an inverted reference, a divider).
  // Skipping this test turns every unfoldable gate into a silent commit-every-
  // tick — the exact miscompile the refusal exists to prevent.
  if (livehd::latch_contract::clock_op_of(clock_driver, clocks)) {
    return false;
  }
  // STRICT IDENTITY PEEL — deliberately NOT control_root(). That helper
  // resolves PHASE, not value: it walks a `cond ? clk : 0` mux to its
  // SELECTOR and an `x == 0` to x. A mux-style clock gate therefore peels to
  // its ENABLE, and `is_clock` answers true about the enable because
  // `Design_clocks` seeds its roots with `resolve_phase(clock_pin)` of every
  // flop — including this one. The cone then reads "an identity wrapper, not a
  // gate", the flop is emitted with no commit guard, and the gate is dead
  // code. Only VALUE-PRESERVING wrappers may be peeled here.
  hhds::Pin_class p     = clock_driver;
  bool            moved = false;
  for (int hops = 0; hops < 16 && !p.is_invalid(); ++hops) {
    if (livehd::graph_util::is_graph_input_pin(p) || is_const_pin(p)) {
      break;
    }
    auto       n  = p.get_master_node();
    const auto op = type_op_of(n);
    auto       e  = sorted_inp(n);
    if (op == Ntype_op::Get_mask && !e.empty()) {
      if (e.size() >= 2) {
        if (!is_const_pin(e[1].driver)) {
          break;
        }
        auto mv = hydrate_const(e[1].driver);
        if (!mv.is_just_i64() || mv.to_just_i64() != -1) {
          break;  // only the to-positive `mask == -1` idiom is an identity
        }
      }
      p     = e[0].driver;  // unary width adjust, or Get_mask(v, -1)
      moved = true;
      continue;
    }
    if ((op == Ntype_op::And || op == Ntype_op::EQ) && e.size() == 2) {
      const int ci = is_const_pin(e[1].driver) ? 1 : (is_const_pin(e[0].driver) ? 0 : -1);
      if (ci < 0) {
        break;  // both operands real: an ICG cone (`clk & en`), never an identity
      }
      auto cv = hydrate_const(e[ci].driver);
      if (op == Ntype_op::And) {
        // `x & <low mask>`: the width mask the slang reader puts on a boolean
        // control. Any other constant trims real bits.
        if (cv.has_unknowns() || cv.is_negative()) {
          break;
        }
        auto [mb, me] = cv.get_mask_range();
        if (mb != 0 || me <= 0) {
          break;
        }
      } else if (!cv.is_just_i64() || cv.to_just_i64() != 1) {
        break;  // `x == 1` is the identity; `x == 0` is a NEGATION (a negedge domain)
      }
      p     = e[1 - ci].driver;
      moved = true;
      continue;
    }
    break;
  }
  // The walk must have actually PEELED something, and must land on a module
  // INPUT. Anything else — a Sub output, a flop Q, a memory dout — is a
  // DERIVED clock, and `is_clock` would answer about it only because the state
  // hanging off this very cone registered it as a root: a self-answer.
  if (!moved || p.is_invalid() || !livehd::graph_util::is_graph_input_pin(p)) {
    return false;
  }
  return clocks.is_clock(p);
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
  for ([[maybe_unused]] auto node : g->body().nodes()) {
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
  int                                       inlined = 0;
  // Recurse first. Held as shared_ptrs so a child def stays alive while we walk
  // it, independent of what happens to the instance node in this body.
  std::vector<std::shared_ptr<hhds::Graph>> children;
  for (auto n : g->body().nodes()) {
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

  // Snapshot the victims: inline_sub_instance deletes nodes and body().nodes() is
  // a live view over the node table.
  std::vector<hhds::Node_class> victims;
  for (auto n : g->body().nodes()) {
    if (type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    auto cg = n.get_subnode_graph();
    if (!cg) {
      continue;  // body-less black box (liberty cell / external IP): nothing to inline
    }
    if (!n.is_loop_subnode() && graph_node_count(cg.get()) <= flatten_budget) {
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

// Every STRUCTURAL rewrite the emitter makes to a body, factored out of
// do_from_graph so the caller can run it over the WHOLE library first.
//
// This ordering is not cosmetic. to_cgen_sim's partition pre-scan captures
// Pin_class handles INTO callee bodies (Split_group::Out::leaf), and each step
// here deletes nodes or rewires edges in those same bodies — so a pre-scan
// interleaved with the rewrites hands the emitter class indices that no longer
// mean what the scan measured. Run once per graph, before any measuring.
bool Cgen_sim::prepare_graph(const std::shared_ptr<hhds::Graph>& graph) {
  hhds::Graph* g      = graph.get();
  const auto   gname  = std::string{graph->get_name()};
  const auto   entity = gname.substr(gname.rfind('.') + 1);
  if (!entity.empty() && entity.front() == '%') {
    return true;  // a `test` block: never emitted, so never rewritten (see do_from_graph)
  }
  // EXACTLY ONCE per body. `flatten_small_subs` is not idempotent — inlining a
  // callee exposes ITS children as direct instances, and a second pass would
  // absorb those too — so re-running it after the partition pre-scan would
  // re-open the staleness this split exists to close. Process-wide (one `lhd`
  // invocation): the emitter builds a fresh Cgen_sim per graph. The map holds
  // a STRONG reference so a freed graph's address can never be reused by a
  // later one and read back as "already prepared".
  // The bool is whether preparation SUCCEEDED: a second call must report the
  // first one's failure rather than answering "already prepared, all good".
  static absl::flat_hash_map<const hhds::Graph*, std::pair<std::shared_ptr<hhds::Graph>, bool>> prepared;
  if (auto [pit, fresh] = prepared.try_emplace(g, graph, false); !fresh) {
    return pit->second.second;
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
  // A parent-level feedback ring still takes the shared expansion fallback.
  // Activation/break descriptors remain compact: the native wrapper below
  // carries the cumulative active bit, bypasses carries for inactive lanes,
  // and skips their calls at runtime (while reset keeps the call open).
  // Snapshot in reverse storage order for the occurrence formatter's
  // module-scoped ordinal accounting.
  std::vector<hhds::Node_class> fallback_loops;
  for (auto n : g->body().nodes()) {
    if (auto d = n.subnode_loop(); d && compact_loop_has_external_ring(n)) {
      fallback_loops.push_back(n);
    }
  }
  for (auto it = fallback_loops.rbegin(); it != fallback_loops.rend(); ++it) {
    if (!livehd::graph_util::materialize_occurrence(g, *it, "inou.cgen.sim")) {
      return false;
    }
  }
  // sim.flatten=N: absorb small sub-instances into this body first, so
  // everything below — the digest, the schedule, the emission — sees the
  // flattened graph. A no-op at the default N=0.
  if (const int nin = flatten_small_subs(g); nin > 0) {
    livehd::diag::info("inou.cgen.sim", "sim-flatten", "progress")
        .msg("`{}`: inlined {} sub-instance(s) with <= {} nodes (sim.flatten)", gname, nin, flatten_budget)
        .emit();
  }
  // NO false-loop inlining (2026-08-06 ruling). A false combinational loop
  // through an atomic instance is dissolved by CALLEE PARTITIONING instead:
  // the callee emits per-output-group `__settle_g<k>` methods (see Split_group)
  // and the parent evaluates exactly the group a demanded output needs, so no
  // body is ever cloned. The inliners this block used to run multiplied cloned
  // subtrees up the hierarchy (77 MB minion_top.cpp, hour-plus host-clang) —
  // the only structural inlining left is the CLOCK-GATE CELL fold, whose
  // bodies are a latch and an AND, and the sim.flatten budget the user opts
  // into. Gate cells can sit behind gate cells, so iterate until quiet.
  int inlined_gate_cells = 0;
  for (int round = 0; round < 8; ++round) {
    const int ncg = livehd::latch_contract::inline_clock_gate_cells(g, "inou.cgen.sim");
    if (::getenv("LIVEHD_SIM_CLK_DEBUG") != nullptr) {
      std::fprintf(stderr, "[icgdbg] %s: gate-cell round %d inlined %d\n", gname.c_str(), round, ncg);
    }
    if (ncg == 0) {
      break;
    }
    inlined_gate_cells += ncg;
  }
  if (inlined_gate_cells > 0) {
    livehd::diag::info("inou.cgen.sim", "clock-gate-inlined", "progress")
        .msg("`{}`: inlined {} clock-gate cell(s) so their gate folds into a flop enable", gname, inlined_gate_cells)
        .emit();
  }
  // Break a false WORD-level combinational loop through a packed wire: redirect
  // each constant Get_mask slice-read of an `Or`-of-disjoint-ranges net to the one
  // operand that drives the read range. A no-op unless a genuine word-level cycle
  // exists; a real bit-level loop is never split (still fails loudly below).
  livehd::graph_util::split_packed_selfref_wires(g);
  prepared.at(g).second = true;  // re-looked-up: the steps above may have inserted
  return true;
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
  // Every structural rewrite this emitter makes ran in prepare_graph(), which
  // to_cgen_sim drives over the WHOLE library before it measures anything.
  // Calling it here too keeps a standalone Cgen_sim user correct; it is a
  // no-op on an already-prepared graph. A refusal (an unexpandable replicated
  // instance) means this body cannot be emitted correctly — stop, the located
  // diagnostic already came from prepare_graph.
  if (!prepare_graph(graph)) {
    return;
  }
  // The shared clock-role analysis -- the same `Design_clocks` that pass/lec's
  // phase schedule and pass.single_edge build. Built AFTER the rewrites above,
  // which change the graph.
  const livehd::latch_contract::Design_clocks design_clocks(g, /*hier=*/false);
  // Structural clock/reset interface memo for this emission only. Same reason
  // as design_clocks above: it answers about the graph AS PREPARED, so it must
  // not outlive the body it was measured on.
  livehd::latch_contract::Clock_port_cache    port_cache;

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
    for (auto n : g->body().nodes()) {
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
  fout->append("#include \"checkpoint.hpp\"  // name-keyed dump_state/load_state helpers\n");
  fout->append("#include <cassert>\n#include <cstddef>\n#include <initializer_list>\n\n");
  fout->append(
      "template <int W, int SW>\n"
      "static Slop<W> __lhd_hotmux(const Slop<SW>& sel, std::initializer_list<Slop<W>> values) {\n"
      "  assert(values.size() != 0);\n"
      "  if (sel.popcount() != 1) { assert(false && \"hotmux select must be one-hot\"); return Slop<W>::invalid(); }\n"
      "  const int bit = sel.get_first_bit_set();\n"
      "  if (bit < 0 || static_cast<size_t>(bit) >= values.size()) return Slop<W>::invalid();\n"
      "  return *(values.begin() + bit);\n"
      "}\n\n");

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
    for (auto node : g->body().nodes()) {
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
      // miscompile, not a slowdown. A FLOP endpoint also folds the ACTIVE-LOW
      // flavour (fall-committed guards), so the scan opts in the same way the
      // flop construction below does.
      bool scan_fall = false;
      if (!icg_guards(d, clock_port, &design_clocks, is_type_flop(node) ? &scan_fall : nullptr).empty()) {
        clock_nets.insert("\x01implicit");  // folded: commits on the reference clock, qualified
        continue;
      }
      if (plain_clock_cone(d, design_clocks)) {
        clock_nets.insert("\x01implicit");  // an identity wrapper, not a gate: ticks with the reference
        continue;
      }
      if (::getenv("LIVEHD_SIM_CLK_DEBUG") != nullptr) {
        auto        cone = livehd::latch_contract::clock_op_of(d, design_clocks);
        std::string dbg  = absl::StrCat("[clkdbg] flop ",
                                        debug_name(node),
                                        " clock_op_of=",
                                        cone ? (cone->clock_inverted ? "INVERTED" : (cone->div != 1 ? "DIV" : "cone")) : "nullopt");
        if (cone) {
          absl::StrAppend(&dbg,
                          " clock=",
                          cone->clock.is_invalid() ? "<inv>" : debug_name(cone->clock.get_master_node()),
                          " enables=",
                          cone->enables.size(),
                          " div=",
                          cone->div);
        }
        absl::StrAppend(&dbg, "\n[clkdbg] cone walk from ", debug_name(d.get_master_node()), ":\n");
        std::vector<hhds::Pin_class>          stk{d};
        absl::flat_hash_set<hhds::Node_class> seen;
        int                                   budget = 40;
        while (!stk.empty() && budget-- > 0) {
          auto p = stk.back();
          stk.pop_back();
          if (p.is_invalid()) {
            continue;
          }
          auto n = p.get_master_node();
          if (!seen.insert(n).second) {
            continue;
          }
          absl::StrAppend(&dbg,
                          "[clkdbg]   ",
                          debug_name(n),
                          " op=",
                          op_name(type_op_of(n)),
                          " gin=",
                          livehd::graph_util::is_graph_input_pin(p) ? pin_name_of(p) : "-",
                          "\n");
          if (livehd::graph_util::is_graph_input_pin(p) || is_const_pin(p)) {
            continue;
          }
          for (auto e : n.inp_edges()) {
            stk.push_back(e.driver);
          }
        }
        std::fputs(dbg.c_str(), stderr);
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
    // A MEMORY is state too, and its clock is the same question. Before
    // 2026-08-05 this loop iterated `is_type_flop` only, so a clock-gated
    // memory compiled CLEAN and wrote every tick with the gate as dead code —
    // the silent half of this gap (`inou.cgen.verilog` gets it right on the
    // same graph: the emitted `cgen_memory_*` instance takes `.clk(clk & en)`).
    // The fold itself lands on the write enable further down; here we only
    // refuse the cones it cannot fold.
    for (auto node : g->body().nodes()) {
      if (type_op_of(node) != Ntype_op::Memory) {
        continue;
      }
      // A Memory carries a clock_pin PER PORT (`<n>clock_pin`), and the fold
      // below keeps a single `m.clock_guards` for the whole array. So this
      // probe must see every port's clock, and two DIFFERENT ones are refused
      // outright: inspecting one port and folding another's cone silently
      // either drops a gate or applies it to writes it does not qualify.
      auto                         drivers = memory_clock_drivers_of(node);
      std::vector<hhds::Pin_class> distinct;
      for (const auto& raw : drivers) {
        auto rd = resolve_passthrough(raw);
        if (rd.is_invalid()) {
          continue;
        }
        const bool dup = std::any_of(distinct.begin(), distinct.end(), [&](const hhds::Pin_class& o) {
          return o.get_class_index() == rd.get_class_index();
        });
        if (!dup) {
          distinct.push_back(rd);
        }
      }
      if (distinct.size() > 1) {
        livehd::diag::err("inou.cgen.sim", "gated-clock-unsupported", "unsupported")
            .msg("module `{}`: memory `{}` has {} distinct port clocks; inou.cgen.sim models one clock per array",
                 gname,
                 debug_name(node),
                 distinct.size())
            .hint(
                "a per-port gated clock would be folded into a single array-wide write guard, so one port's enable "
                "would qualify another port's writes — a silent miscompile. Give every port the same clock, or "
                "simulate the emitted Verilog with an event-driven simulator")
            .emit();
        return;
      }
      auto d = distinct.empty() ? hhds::Pin_class{} : distinct.front();
      if (d.is_invalid() || livehd::graph_util::is_graph_input_pin(d) || is_const_pin(d)) {
        continue;  // unclocked array, or a plain clock net
      }
      if (!icg_guards(d, clock_port, &design_clocks).empty() || plain_clock_cone(d, design_clocks)) {
        continue;  // folded into the write enable, or not a gate at all
      }
      livehd::diag::err("inou.cgen.sim", "gated-clock-unsupported", "unsupported")
          .msg("module `{}`: memory `{}` has a derived clock inou.cgen.sim cannot fold into a write guard", gname, debug_name(node))
          .hint(
              "only the ICG shape `<clock> & <enable>` is folded (write when the enable is high at the reference "
              "edge); any other derived clock would be simulated as if the memory ticked every step, with the gate "
              "as dead code — a silent miscompile. Model the gate as the memory's write enable instead, or simulate "
              "the emitted Verilog with an event-driven simulator")
          .emit();
      return;
    }
    // A DERIVED CLOCK CROSSING INTO A CHILD is the same question one level
    // down, and the two scans above cannot see it: they iterate state in THIS
    // body, and the module that merely computes a gate and wires it to a
    // child's clock port often has no local state on that net at all.
    //
    // The only thing the `__tick` channel can say is "the reference period,
    // qualified by these enables". An INVERTED or DIVIDED cone has no spelling
    // there, so emit_sub_call writes no guard word, the child's default `true`
    // stands, and the child commits every period with the gate as dead code.
    for (auto node : g->body().nodes()) {
      if (type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      auto cdef = node.get_subnode_graph();
      if (!cdef) {
        continue;  // body-less blackbox: nothing of ours commits inside it
      }
      const auto& guard_pids = clock_guard_ports(cdef, port_cache);
      if (guard_pids.empty()) {
        continue;
      }
      for (const auto& e : node.inp_edges()) {
        if (!guard_pids.contains(static_cast<uint32_t>(e.sink.get_port_id()))) {
          continue;  // not a clock port the child clocks state on
        }
        auto d = resolve_passthrough(e.driver);
        if (d.is_invalid() || livehd::graph_util::is_graph_input_pin(d) || is_const_pin(d)) {
          continue;  // a plain clock net: forwarded (or ungated) as-is
        }
        // Exactly the emit-site arm that DROPS the word: a decoded gate whose
        // enables cannot ride the tick channel because the reference is
        // inverted or divided. Everything else either folds (the enables
        // become the conjunction) or forwards the root's own word.
        auto cone = livehd::latch_contract::clock_op_of(d, design_clocks);
        if (!cone || cone->enables.empty() || (!cone->clock_inverted && cone->div == 1)) {
          continue;
        }
        livehd::diag::err("inou.cgen.sim", "gated-clock-unsupported", "unsupported")
            .msg("module `{}`: instance `{}` is driven with a derived clock inou.cgen.sim cannot forward as a tick guard",
                 gname,
                 debug_name(node))
            .hint(
                "the child is told `<port>__tick` — the reference period qualified by enables — so only the ICG shape "
                "`<clock> & <enable>` crosses the boundary. An inverted or divided clock has no spelling there, and "
                "the child would commit every period with the gate as dead code — a silent miscompile. Model the gate "
                "as the child's `enable` instead, or simulate the emitted Verilog with an event-driven simulator")
            .emit();
        return;
      }
    }
    // MULTI-CLOCK is supported as of M6 — the M0 refusal here is LIFTED. State
    // on a net other than the reference clock commits on a detected EDGE of
    // that net (see Flop::sec_clock), so a second clock is simply a data input
    // the testbench toggles. What is NOT lifted is a derived clock we cannot
    // fold (handled above): that still fails closed, because there is no net to
    // detect an edge on without inventing one.
    (void)clock_nets;
  }

  // CLOCK INPUT PORTS this definition's state actually commits on. Each gets a
  // companion `<field>__tick` bool in `In`, defaulted TRUE.
  //
  // WHY. A clock GATE in the parent whose output crosses into this module as a
  // clock port is invisible from here: the port is an ordinary graph input, so
  // this emitter picks it as the reference clock and commits every tick with the
  // parent's gate as dead code. That is a SILENT miscompile, not a refusal, and
  // it covered 681 state elements on minion. The guard is the channel the parent
  // uses to say "the clock did not tick for you this period".
  //
  // Defaulted true and emitted for EVERY clock port, so an ungated
  // instantiation, an existing checkpoint and every hand-written testbench are
  // bit-for-bit unchanged; only a parent that gates the port ever writes it.
  absl::flat_hash_set<std::string> clock_in_fields;
  {
    // The RECURSIVE guard-port analysis: covers state clocked on the port
    // directly, state clocked through a local gate ROOTED at the port, and
    // ports that merely FORWARD into a child's guard port (the stateless
    // wrapper levels of minion's txfma chain).
    const auto& gports = clock_guard_ports(graph, port_cache);
    if (!gports.empty() && gio) {
      for (const auto& d : gio->get_input_pin_decls()) {
        if (!gports.contains(static_cast<uint32_t>(d.port_id))) {
          continue;
        }
        for (const auto& io : ios) {
          if (io.is_input && io.raw == d.name) {
            clock_in_fields.insert(io.field);
            break;
          }
        }
      }
    }
  }

  // ---- flops (Flop cells; Latch/Memory -> later phase) ----
  // Generated STRUCT MEMBER names — flops, memories, sub-instances — must be
  // unique, and node names alone do not guarantee it. Two producers of
  // collisions are real: a generate-loop's instances all carry the same
  // callsite name (`::[name=u_unit_compressor]` repeated per iteration in
  // generated Pyrope — minion's txfma_4_2_compressor_array declares nine
  // members named u_unit_compressor), and the false-loop inliner cloning
  // several same-named instances' bodies (vpu_top's eight lanes are all
  // `lane`, so eight copies of `lane.f3_tenb_pass` land in one graph). Legal
  // in the graph, an immediate host-compile error in C++.
  // First occurrence keeps the plain name; later ones get `__i2`, `__i3`, ...
  // Every consumer (calls, checkpoint, iface.json, VCD hierarchy) reads the
  // stored member string, so the suffix is consistent everywhere.
  absl::flat_hash_set<std::string> used_member_names;
  // A PORT name is claimed before any of them. Ports are struct fields too, and
  // the emitted `cycle()` builds a local `Out <port>` on top of that — so a sub
  // instance named after the port it drives (`o = subcnt(en=en)`, whose
  // instance name is now the LHS variable) shadows the member and the body
  // reads `o.__gen` off the Out struct. Reserving here demotes the collider to
  // `<port>__i2` instead.
  if (gio) {
    // Reserve the STRUCT MEMBER, which is the top-level name: a dotted tuple
    // leaf (`io.data`) is emitted as a field of the GROUP member `io` (see
    // cpp_port_path / emit_io_block), so reserving cpp_id("io.data") ==
    // `io_data` would claim a name that is never a member — demoting an
    // unrelated instance called `io_data` to `io_data__i2` — while leaving the
    // real member `io` free for an instance to shadow.
    const auto member_of = [](std::string_view n) {
      const auto dot = n.find('.');
      return cpp_id(dot == std::string_view::npos ? n : n.substr(0, dot));
    };
    for (const auto& d : gio->get_input_pin_decls()) {
      used_member_names.insert(member_of(d.name));
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      used_member_names.insert(member_of(d.name));
    }
  }
  auto unique_member = [&used_member_names](std::string base) -> std::string {
    if (base.empty()) {
      base = "u";
    }
    std::string name = base;
    for (int i = 2; !used_member_names.insert(name).second; ++i) {
      name = absl::StrCat(base, "__i", i);
    }
    return name;
  };

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
    // The `In` field of the clock PORT this element commits on, when that port
    // carries a tick guard (see clock_in_fields). Empty otherwise. A parent that
    // gates the port drives `<field>__tick` false for the periods it withholds.
    std::string                  tick_field;
    // ACTIVE-LOW gate flavour (icg_guards' fall_commit): the clock cone is an
    // INVERTED gated reference, so this flop commits in the FALL sub-tick
    // (posedge=false) AND its clock_guards must be sampled THERE — the guard
    // latches are transparent during the high phase, so their rise-pass
    // (pre-tick) values are one half-period stale. emit_commit_enable skips
    // such a flop in the rise; the fall pass computes its `_cen` fresh.
    bool                         gate_fall = false;
  };
  std::vector<Flop> flops;
  for (auto node : g->body().nodes()) {
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
    Flop              f{node,
                        unique_member(cpp_id(wire_name(qpin))),
                        wbits_of(qpin),
                        depth,
                        {},
                        true,
                        type_op_of(node) == Ntype_op::Latch,
                        false,
                        {},
                        {},
                        {},
                        {}};
    const std::string ref_clock = clock_input_of(g);
    bool              gate_fall = false;
    f.clock_guards
        = icg_guards(get_driver(find_sink_pin(node, "clock_pin")), ref_clock, &design_clocks, f.is_latch ? nullptr : &gate_fall);
    {
      // The port whose tick guard this element honors: the clock driver
      // resolved to its ROOT — directly a port, or a local gate cone whose
      // reference is the port (the guards fold into clock_guards above; the
      // parent's word about the PORT itself composes through tick_field).
      auto            cdp  = get_driver(find_sink_pin(node, "clock_pin"));
      hhds::Pin_class root = cdp;
      if (!cdp.is_invalid() && !livehd::graph_util::is_graph_input_pin(cdp)) {
        if (auto cone = livehd::latch_contract::clock_op_of(cdp, design_clocks); cone && !cone->clock.is_invalid()) {
          root = cone->clock;
        } else {
          root = livehd::latch_contract::control_root(cdp).net;
        }
      }
      if (!root.is_invalid() && livehd::graph_util::is_graph_input_pin(root)) {
        for (const auto& io : ios) {
          if (io.is_input && io.raw == pin_name_of(root) && clock_in_fields.contains(io.field)) {
            f.tick_field = io.field;
            break;
          }
        }
      }
    }
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
      if (!cd.is_invalid() && livehd::graph_util::is_graph_input_pin(cd)) {
        const std::string cd_name{pin_name_of(cd)};
        // A NEGEDGE flop whose net IS the adopted reference still needs a
        // detected edge when that net is not conventionally SPELLED as a
        // clock: clock_input_of() then adopted it only because no register
        // sits on the implicit reference clock (flop_sim_negedge_sole_clock --
        // `clock_pin=ref rclk` as the design's sole clock), and the net is
        // really a signal the testbench drives, so a posclk=false flop would
        // commit at every tick instead of holding across the rises.
        //
        // Deliberately NARROW on both axes, because each wider form breaks a
        // pinned behavior:
        //  * posedge flops keep the one-tick-one-period model even on an
        //    unconventional sole net -- gate_through_wrapper/hier_gate_port
        //    children commit per gated period on a `gclk` PORT through the
        //    `<port>__tick` channel, never by a toggling port value;
        //  * a conventionally spelled reference (clk, clock, clk_i, rf_clk_i,
        //    ...) is never edge-detected -- conditional_state_named_clock pins
        //    that a sole explicit `clk_i` commits once per tick. The spelling
        //    test is the shared Design_clocks::name_looks_like_clock notion,
        //    not a private list.
        bool ref_needs_edge = false;
        if (cd_name == ref_clock && !livehd::latch_contract::Design_clocks::name_looks_like_clock(cd_name)
            && type_op_of(node) != Ntype_op::Latch) {
          auto pc        = get_driver(find_sink_pin(node, "posclk"));
          ref_needs_edge = !pc.is_invalid() && is_const_pin(pc) && hydrate_const(pc).is_known_false();
        }
        if (cd_name != ref_clock || ref_needs_edge) {
          f.sec_clock   = get_driver(find_sink_pin(node, "clock_pin"));
          f.prev_member = absl::StrCat("__clkprev_", cpp_id(cd_name));
        }
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
    // ACTIVE-LOW gate flavour (icg_guards' fall_commit): the flop's clock is
    // an inverted gated reference. Applied AFTER any posclk word, because the
    // gate's inversion COMPOSES with the element's own edge rather than
    // replacing it:
    //   posedge element on `~clk & en` -> samples on clk's FALL: fall sub-tick,
    //                                     guards read there (gate_fall);
    //   negedge element on `~clk & en` -> the two inversions cancel: it samples
    //                                     on clk's RISE, which is the ordinary
    //                                     rise sub-tick with rise-read guards.
    // Overriding posedge unconditionally put the second shape a half period
    // late, silently.
    if (gate_fall) {
      const bool cancels = !f.posedge;  // a negedge element: inversion cancels
      f.posedge          = cancels;
      f.gate_fall        = !cancels;
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
    Order                        order     = Order::old;
    int                          n_user_wr = 0;  // trailing ports are reset/restore: never forward
    std::vector<int>             fwd_upto;       // per read port; Order::program only
    std::vector<MemPort>         ports;          // real ports, in port order (phantoms dropped)
    // Whole-array support (the `update` bus is driven): one update/read_all bus
    // instead of N per-entry ports. registered when a clock is present.
    hhds::Pin_class              update, update_enable, init, reset, clock;
    // In-field of the guarded clock PORT this memory commits on (see Flop).
    std::string                  tick_field;
    // ICG FOLD, memory side (todo_sim_pipeline.md step 1). Non-empty => this
    // memory's clock is a recognized gate, and every write is qualified by the
    // gate's enables. Empty => an ungated clock (the refusal probe above has
    // already rejected any derived clock that does NOT fold), i.e. write every
    // tick, exactly as before.
    std::vector<hhds::Pin_class> clock_guards;
    bool                         has_read_all = false;
    bool                         is_whole() const { return !update.is_invalid(); }
    bool                         registered() const { return !clock.is_invalid(); }
  };
  std::vector<Mem> mems;
  for (auto node : g->body().nodes()) {
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
    // ICG FOLD, memory side. The refusal probe above has already rejected any
    // derived memory clock this cannot fold, so an empty result here means
    // "ungated", never "unrecognized".
    m.clock_guards = icg_guards(m.clock, clock_input_of(g), &design_clocks);
    {
      hhds::Pin_class root = m.clock;
      if (!root.is_invalid() && !livehd::graph_util::is_graph_input_pin(root)) {
        if (auto cone = livehd::latch_contract::clock_op_of(root, design_clocks); cone && !cone->clock.is_invalid()) {
          root = cone->clock;
        } else {
          root = livehd::latch_contract::control_root(root).net;
        }
      }
      if (!root.is_invalid() && livehd::graph_util::is_graph_input_pin(root)) {
        for (const auto& io : ios) {
          if (io.is_input && io.raw == pin_name_of(root) && clock_in_fields.contains(io.field)) {
            m.tick_field = io.field;
            break;
          }
        }
      }
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
    m.member = unique_member(cpp_id(default_instance_name(node)));
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
    hhds::Node_class                                 node;
    std::string                                      inst;           // member name
    std::string                                      callee_struct;  // cpp_id of the callee module name
    bool                                             refresh_negedge;
    bool                                             child_refresh_negedge;
    bool                                             negedge_only;  // state advances in the fall half, after the parent's rise
    std::optional<hhds::Subnode_loop>                loop;
    std::vector<std::pair<std::string, std::string>> carries;  // callee input -> output
    std::string                                      loop_struct;
  };
  std::vector<Sub>         subs;
  std::vector<std::string> sub_includes;  // distinct callee headers
  struct Phase_info {
    bool posedge = false;  // a FLOP that commits at the rise
    bool negedge = false;  // a flop that commits at the fall
    // LEVEL-SENSITIVE state. A Latch commits in the RISE sub-tick (the `flops`
    // build above gives one posedge=true unconditionally: on a Latch `posclk`
    // is the enable POLARITY, not an edge), so a child that owns one is not
    // fall-only either. Tracked apart from `posedge` because the refresh
    // predicate below has to tell the two apart.
    bool latch   = false;
    bool refresh = false;  // this graph has a descendant needing the refresh
  };
  absl::flat_hash_map<const hhds::Graph*, Phase_info> phase_memo;
  absl::flat_hash_set<const hhds::Graph*>             phase_visiting;
  // Does this callee need the post-settle negedge refresh (`refresh_negedge`)?
  // A child that owns BOTH rise-half and fall-half state runs its whole
  // `cycle()` inside the parent's RISE, so its fall half samples inputs the
  // parent has not committed yet -- one period stale, forever.
  //
  // Deliberately narrowed to the children whose rise-half state is a LATCH and
  // nothing else, because those are EXACTLY the ones `latch` keeps out of
  // `negedge_only` below: a fall-deferred child already sees post-rise inputs
  // and needs no refresh, so the reclassification and its compensation always
  // travel together (this used to be a whitelist of four port names, which
  // compensated one of the three preview register files and left the other two
  // reclassified-but-stale). A child that ALSO has a posedge flop was
  // rise-scheduled long before any of this, and re-running its fall half is
  // only sound while that half is a pure input capture -- a negedge flop whose
  // next state reads its own Q would ADVANCE TWICE -- so that shape keeps the
  // behavior it has always had.
  const auto needs_neg_refresh = [](const Phase_info& i) { return i.negedge && i.latch && !i.posedge; };
  std::function<Phase_info(const std::shared_ptr<hhds::Graph>&)> graph_phases;
  graph_phases = [&](const std::shared_ptr<hhds::Graph>& pg) -> Phase_info {
    if (!pg) {
      return {};
    }
    if (auto it = phase_memo.find(pg.get()); it != phase_memo.end()) {
      return it->second;
    }
    if (!phase_visiting.insert(pg.get()).second) {
      return {};
    }
    Phase_info info;
    for (auto pn : pg->body().nodes()) {
      if (type_op_of(pn) == Ntype_op::Latch) {
        info.latch = true;
      } else if (is_type_flop(pn)) {
        bool pos = true;
        if (auto pc = get_driver(find_sink_pin(pn, "posclk")); !pc.is_invalid() && is_const_pin(pc)) {
          pos = !hydrate_const(pc).is_known_false();
        }
        info.posedge |= pos;
        info.negedge |= !pos;
      } else if (is_type_sub(pn)) {
        // MIRROR the filters the `subs` build below applies, or this summary
        // and the callee's own `has_refresh` disagree and the parent emits a
        // call to a `refresh_negedge()` the child never declares: a compact
        // loop is dropped (`!loop` guards both refresh fields, and the wrapper
        // struct has no refresh method at all), and so is anything that never
        // becomes a `Sub` entry.
        auto pio = pn.get_subnode_io();
        if (!pio || pn.subnode_loop()) {
          continue;
        }
        const std::string pname{pio->get_name()};
        if (pname.empty() || pname == livehd::graph_util::lgassert_module_name
            || pname == livehd::graph_util::fproperty_module_name) {
          continue;
        }
        const auto sub  = graph_phases(pn.get_subnode_graph());
        info.refresh   |= needs_neg_refresh(sub) || sub.refresh;
      }
    }
    phase_visiting.erase(pg.get());
    return phase_memo.emplace(pg.get(), info).first->second;
  };
  for (auto node : g->body().nodes()) {
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
    const auto                                       child_phase = graph_phases(node.get_subnode_graph());
    const std::string                                inst        = unique_member(cpp_id(default_instance_name(node)));
    std::optional<hhds::Subnode_loop>                loop        = node.subnode_loop();
    std::vector<std::pair<std::string, std::string>> carries;
    if (loop) {
      try {
        node.subnode_group().validate();
      } catch (const std::exception& e) {
        livehd::diag::err("inou.cgen.sim", "compact-loop-invalid", "unsupported")
            .msg("cannot emit compact loop instance '{}': {}", inst, e.what())
            .fatal();
      }
      for (const auto& c : node.subnode_group().carries()) {
        std::string in_name;
        std::string out_name;
        for (const auto& d : sio->get_input_pin_decls()) {
          if (d.port_id == c.input_port()) {
            in_name = d.name;
            break;
          }
        }
        for (const auto& d : sio->get_output_pin_decls()) {
          if (d.port_id == c.output_port()) {
            out_name = d.name;
            break;
          }
        }
        if (in_name.empty() || out_name.empty()) {
          livehd::diag::err("inou.cgen.sim", "compact-loop-carry", "internal")
              .msg("compact loop instance '{}' has a carry whose callee port declaration is unavailable", inst)
              .fatal();
        }
        carries.emplace_back(std::move(in_name), std::move(out_name));
      }
    }
    subs.push_back({node,
                    inst,
                    cpp_id(cname),
                    !loop && needs_neg_refresh(child_phase),
                    !loop && child_phase.refresh,
                    child_phase.negedge && !child_phase.posedge && !child_phase.latch,
                    loop,
                    std::move(carries),
                    // The wrapper struct is defined at GLOBAL scope in this
                    // module's header, and the parent #includes every callee
                    // header, so the name must be unique across the whole
                    // include tree. `inst` alone is not: rolled-loop instances
                    // are numbered by a PER-MODULE counter, so the first rolled
                    // loop of every module is `u_loop_0` and two callee headers
                    // would both define `struct __compact_u_loop_0`. The callee
                    // struct name embeds the parent module (`<mod>.__loop<n>`),
                    // so prefixing with it makes the name module-unique.
                    loop ? absl::StrCat("__compact_", cpp_id(cname), "_", inst) : std::string{}});
    auto hdr = absl::StrCat(sim_file_stem(cname), ".hpp");
    if (std::find(sub_includes.begin(), sub_includes.end(), hdr) == sub_includes.end()) {
      sub_includes.push_back(hdr);
    }
  }

  for (const auto& h : sub_includes) {
    hout->append(absl::StrCat("#include \"", h, "\"\n"));
  }

  // A compact loop presents the exact public surface of one callee to the
  // parent scheduler, but owns COUNT independent callee states. That keeps the
  // mature scheduling/VCD/checkpoint machinery unchanged at the boundary while
  // the wrapper performs the eager carry chain with a runtime ordinal loop.
  for (const auto& s : subs) {
    if (!s.loop) {
      continue;
    }
    const auto& loop          = *s.loop;
    auto        sio           = s.node.get_subnode_io();
    const auto  carry_out_for = [&](std::string_view out) -> const std::string* {
      for (const auto& [in_name, out_name] : s.carries) {
        if (out_name == out) {
          return &in_name;
        }
      }
      return nullptr;
    };
    std::string activation_field;
    if (loop.activation_input) {
      for (const auto& d : sio->get_input_pin_decls()) {
        if (d.port_id == *loop.activation_input) {
          activation_field = cpp_port_path(d.name);
          break;
        }
      }
      if (activation_field.empty()) {
        livehd::diag::err("inou.cgen.sim", "compact-loop-activation", "internal")
            .msg("compact loop instance '{}' has no declaration for its activation input", s.inst)
            .fatal();
      }
    }
    bool        activation_skip_safe = false;
    std::string reset_run_condition;
    if (loop.activation_input) {
      const auto& resets   = reset_guard_ports(s.node.get_subnode_graph(), port_cache);
      activation_skip_safe = resets.complete && resets.ports.size() <= 1;
      if (activation_skip_safe && !resets.ports.empty()) {
        const auto& rp = resets.ports.front();
        for (const auto& d : sio->get_input_pin_decls()) {
          if (static_cast<uint32_t>(d.port_id) == rp.port_id) {
            reset_run_condition
                = absl::StrCat("(lane.__in.", cpp_port_path(d.name), rp.active_low ? ").is_known_false()" : ").is_known_true()");
            break;
          }
        }
        if (reset_run_condition.empty()) {
          activation_skip_safe = false;
        }
      }
    }
    auto lane_prefix_expr = [](std::string_view base, std::string_view ordinal) {
      return absl::StrCat("(",
                          base,
                          ".empty() ? std::string{} : ",
                          base,
                          ".substr(0, ",
                          base,
                          ".size() - 1)) + \"[\" + std::to_string(",
                          ordinal,
                          ") + \"].\"");
    };
    hout->append("\nstruct ", s.loop_struct, " {\n");
    hout->append("  using Callee = ", s.callee_struct, ";\n");
    hout->append("  using In = Callee::In;\n  using Out = Callee::Out;\n");
    hout->append("  static constexpr std::size_t count = ", std::to_string(loop.count), ";\n");
    hout->append("  std::array<Callee, count> lanes{};\n  In __in{};\n  Out __last_out{};\n  Out __out{};\n");
    hout->append("  uint64_t __gen = 1, __kids = 0;\n");
    if (vcd_on) {
      hout->append("  std::shared_ptr<vcd::VCDWriter> __vcd;\n  std::string __vcd_path;\n");
    }
    hout->append(
        "  void __sync_kids() { uint64_t n = 0; for (const auto& lane : lanes) n += lane.__gen; "
        "if (n != __kids) { __kids = n; ++__gen; } }\n");
    const auto emit_lane_inputs = [&](std::string_view lane, std::string_view ordinal) {
      for (const auto& d : sio->get_input_pin_decls()) {
        const std::string field = cpp_port_path(d.name);
        std::string       rhs;
        if (loop.index_input && d.port_id == *loop.index_input) {
          rhs = absl::StrCat("Slop<",
                             std::max<uint32_t>(1, d.bits),
                             ">::create_integer(",
                             loop.first,
                             " + static_cast<int64_t>(",
                             ordinal,
                             ") * ",
                             loop.step,
                             ")");
        } else if (loop.activation_input && d.port_id == *loop.activation_input) {
          rhs = "__active";
        } else {
          for (size_t ci = 0; ci < s.carries.size(); ++ci) {
            if (s.carries[ci].first == d.name) {
              rhs = absl::StrCat("__carry", ci);
              break;
            }
          }
          if (rhs.empty()) {
            rhs = absl::StrCat("__in.", field);
          }
        }
        hout->append(absl::StrCat("      ", lane, ".__gen += slop_update(", lane, ".__in.", field, ", ", rhs, ");\n"));
        if (auto cg = s.node.get_subnode_graph();
            cg && clock_guard_ports(cg, port_cache).contains(static_cast<uint32_t>(d.port_id))) {
          hout->append(
              absl::StrCat("      ", lane, ".__gen += slop_update(", lane, ".__in.", field, "__tick, __in.", field, "__tick);\n"));
        }
      }
    };
    const auto emit_carry_decls = [&] {
      for (size_t ci = 0; ci < s.carries.size(); ++ci) {
        hout->append("    auto __carry", std::to_string(ci), " = __in.", cpp_port_path(s.carries[ci].first), ";\n");
      }
    };
    const auto emit_activation_decl = [&] {
      if (loop.activation_input) {
        hout->append("    auto __active = __in.", activation_field, ".zext_to<1>();\n");
      }
    };
    const auto emit_carry_advances = [&](std::string_view out) {
      for (size_t ci = 0; ci < s.carries.size(); ++ci) {
        if (loop.activation_input) {
          hout->append(absl::StrCat("      if (__lane_active) __carry",
                                    ci,
                                    " = ",
                                    out,
                                    ".",
                                    cpp_port_path(s.carries[ci].second),
                                    ";  // inactive lane preserves its carry\n"));
        } else {
          hout->append(absl::StrCat("      __carry", ci, " = ", out, ".", cpp_port_path(s.carries[ci].second), ";\n"));
        }
      }
    };
    const auto emit_next_active = [&](std::string_view out) {
      if (!loop.activation_input || !loop.next_active_output) {
        return;
      }
      for (const auto& d : sio->get_output_pin_decls()) {
        if (d.port_id == *loop.next_active_output) {
          hout->append("      __active = __lane_active ? ",
                       out,
                       ".",
                       cpp_port_path(d.name),
                       ".zext_to<1>() : Slop<1>::create_integer(0);\n");
          return;
        }
      }
    };
    const auto emit_outputs = [&](std::string_view dst, std::string_view last) {
      for (const auto& d : sio->get_output_pin_decls()) {
        const std::string field = cpp_port_path(d.name);
        if (const auto* carry_in = carry_out_for(d.name)) {
          size_t ci = 0;
          for (; ci < s.carries.size() && s.carries[ci].first != *carry_in; ++ci) {
          }
          hout->append(absl::StrCat("    __gen += slop_update(", dst, ".", field, ", __carry", ci, ");\n"));
        } else if (loop.count != 0) {
          hout->append(absl::StrCat("    __gen += slop_update(", dst, ".", field, ", ", last, ".", field, ");\n"));
        }
      }
    };
    hout->append("  void __settle() {\n");
    emit_carry_decls();
    emit_activation_decl();
    hout->append("    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {\n      auto& lane = lanes[ordinal];\n");
    emit_lane_inputs("lane", "ordinal");
    if (loop.activation_input) {
      hout->append("      const bool __lane_active = (__active).is_known_true();\n");
    }
    if (loop.activation_input && activation_skip_safe) {
      hout->append("      if (__lane_active",
                   reset_run_condition.empty() ? "" : absl::StrCat(" || ", reset_run_condition),
                   ") lane.__settle();  // conditional activation (reset keeps it open)\n");
    } else {
      hout->append("      lane.__settle();\n");
    }
    emit_carry_advances("lane.__out");
    emit_next_active("lane.__out");
    hout->append("    }\n");
    emit_outputs("__out", "lanes[count - 1].__out");
    hout->append("    __sync_kids();\n  }\n");
    hout->append("  const Out& cycle(bool __finish = true) {\n");
    emit_carry_decls();
    emit_activation_decl();
    if (loop.count != 0) {
      hout->append("    const Out* __last = nullptr;\n");
    }
    hout->append("    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {\n      auto& lane = lanes[ordinal];\n");
    emit_lane_inputs("lane", "ordinal");
    if (loop.activation_input) {
      hout->append("      const bool __lane_active = (__active).is_known_true();\n");
      hout->append("      const Out* __period = &lane.__out;\n");
      if (activation_skip_safe) {
        hout->append("      if (__lane_active",
                     reset_run_condition.empty() ? "" : absl::StrCat(" || ", reset_run_condition),
                     ") __period = &lane.cycle(false);  // wrapper performs the post-edge lane settle as one walk\n");
      } else {
        hout->append("      __period = &lane.cycle(false);\n");
      }
    } else {
      hout->append("      const auto& __period = lane.cycle(false);\n");
    }
    emit_carry_advances(loop.activation_input ? "(*__period)" : "__period");
    emit_next_active(loop.activation_input ? "(*__period)" : "__period");
    if (loop.count != 0) {
      hout->append(loop.activation_input ? "      __last = __period;\n" : "      __last = &__period;\n");
    }
    hout->append("    }\n");
    emit_outputs("__last_out", "(*__last)");
    hout->append("    __sync_kids();\n    if (__finish) __settle();\n    return __last_out;\n  }\n");
    hout->append(
        "  void reset_cycle(bool zero_uninitialized = false) { for (auto& lane : lanes) "
        "lane.reset_cycle(zero_uninitialized); ++__gen; __settle(); }\n");
    if (vcd_on) {
      hout->append(
          "  void __vcd_hier(vcd::VCDWriter* w, const std::string& s) { for (std::size_t i = 0; i < count; ++i) { "
          "lanes[i].__vcd.reset(); lanes[i].__vcd_path.clear(); lanes[i].__vcd_hier(w, s + \"[\" + std::to_string(i) + \"]\"); } "
          "}\n");
      hout->append("  void __vcd_clk(vcd::VCDWriter* w, bool rise) { for (auto& lane : lanes) lane.__vcd_clk(w, rise); }\n");
      if (vcd_fakedelay) {
        hout->append(
            "  void __vcd_dump_x(vcd::VCDWriter* w, bool pos, bool root) { for (auto& lane : lanes) "
            "lane.__vcd_dump_x(w, pos, root); }\n");
      }
      hout->append(
          "  void __vcd_dump_data(vcd::VCDWriter* w, bool pos, bool root) { for (auto& lane : lanes) "
          "lane.__vcd_dump_data(w, pos, root); }\n");
    }
    const std::string lp = lane_prefix_expr("p", "i");
    hout->append(
        "  void dump_state(const std::string& p, std::map<std::string, std::string>& r, const std::string& d) const { "
        "for (std::size_t i = 0; i < count; ++i) lanes[i].dump_state(",
        lp,
        ", r, d); }\n");
    hout->append(
        "  void load_state(const std::string& p, const std::map<std::string, std::string>& r, const std::string& d) { "
        "for (std::size_t i = 0; i < count; ++i) lanes[i].load_state(",
        lp,
        ", r, d); ++__gen; }\n");
    hout->append(
        "  std::uint64_t design_hash() const { std::uint64_t h = 14695981039346656037ULL; auto fold = [&](std::uint64_t v) { "
        "for (int b = 0; b < 8; ++b) { h ^= (v >> (b * 8)) & 0xffU; h *= 1099511628211ULL; } }; fold(count); "
        "for (const auto& lane : lanes) fold(lane.design_hash()); return h; }\n");
    hout->append(
        "  void describe_signals(const std::string& p, std::vector<hlop::ckpt::Signal>& v) const { for (std::size_t i = 0; i < "
        "count; ++i) "
        "lanes[i].describe_signals(",
        lp,
        ", v); }\n");
    hout->append(
        "  void probe_signals(const std::string& p, std::map<std::string, long>& m) const { for (std::size_t i = 0; i < count; "
        "++i) "
        "lanes[i].probe_signals(",
        lp,
        ", m); }\n");
    hout->append(
        "  void observe_signals(const std::string& p, std::map<std::string, std::string>& m) const { for (std::size_t i = 0; i < "
        "count; ++i) "
        "lanes[i].observe_signals(",
        lp,
        ", m); }\n");
    hout->append(
        "  bool observe_mem(const std::string& n, long i, std::string& o) const { if (n.empty() || n[0] != '[') return false; "
        "const auto rb = n.find(\"]\"); if (rb == std::string::npos || rb + 1 >= n.size() || n[rb + 1] != '.') return false; "
        "std::size_t ordinal = 0; try { ordinal = static_cast<std::size_t>(std::stoull(n.substr(1, rb - 1))); } catch (...) { "
        "return false; } "
        "return ordinal < count && lanes[ordinal].observe_mem(n.substr(rb + 2), i, o); }\n");
    hout->append("};\n");
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
  // "This array takes an edge this period": every local gate enable high, and
  // (for a memory whose clock arrives through a port the parent gates) the
  // parent's tick word set. Empty means ungated. ONE definition, because every
  // edge-triggered thing the array does — writes, the whole-array update, the
  // sync-read register — has to be qualified by exactly the same condition.
  auto mem_gate_cond = [&](const Mem& m) -> std::string {
    std::string cond;
    for (const auto& gp : m.clock_guards) {
      absl::StrAppend(&cond, cond.empty() ? "" : " && ", "(", operand(gp, 1), ").is_known_true()");
    }
    if (!m.tick_field.empty()) {
      absl::StrAppend(&cond, cond.empty() ? "" : " && ", "__in.", m.tick_field, "__tick");
    }
    return cond;
  };
  auto emit_wen = [&](const Mem& m, const MemPort& wp) -> std::string {
    std::string wen
        = wp.enable.is_invalid() ? absl::StrCat("Slop<", m.wensize, ">::create_integer(-1)") : operand(wp.enable, m.wensize);
    // A GATED CLOCK is a write qualifier: the memory takes an edge only on the
    // reference edges where every gate enable is high, which for a one-tick-is-
    // one-period sim is exactly "write iff the guards are true". Gating the
    // enable rather than skipping the staged write keeps the read port's
    // same-address forwarding consistent with what actually lands.
    const std::string cond = mem_gate_cond(m);
    if (cond.empty()) {
      return wen;
    }
    return absl::StrCat("((", cond, ") ? (", wen, ") : Slop<", m.wensize, ">::create_integer(0))");
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
  const bool has_refresh
      = std::any_of(subs.begin(), subs.end(), [](const auto& s) { return s.refresh_negedge || s.child_refresh_negedge; });

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
    hout->append(absl::StrCat("  ",
                              s.loop ? s.loop_struct : s.callee_struct,
                              " ",
                              s.inst,
                              s.loop ? ";  // compact loop instance\n" : ";  // sub instance\n"));
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
        if (want_input && io.raw == "__valid") {
          // Hidden activation is true for standalone use. Parent instances
          // overwrite it from the transported call guard before evaluation.
          hout->append("    Slop<1> ", io.field, " = Slop<1>::create_integer(1);\n");
        } else {
          hout->append("    Slop<", std::to_string(io.bits), "> ", io.field, "{};\n");
        }
        if (want_input && clock_in_fields.contains(io.field)) {
          hout->append("    bool ", io.field, "__tick = true;  // did this clock port tick? false == the parent gated it off\n");
        }
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
  // CHANGE-GATED EVALUATION. `__gen` counts observable changes to this
  // instance's world: every __in field write that actually changed the value
  // (callers funnel through slop_update) and every state commit that stored a
  // different value. Each eval method remembers the (__gen, sum of child
  // __gen) pair it last ENTERED with; re-entering with the same pair means
  // nothing this method reads has changed AND its last run reached a fixed
  // point (had it committed a change, __gen would have grown past the stored
  // entry value), so the run is skipped. This is what lets an idle or
  // clock-gated cone quiesce to a couple of integer compares per cycle.
  hout->append("  uint64_t __gen = 1;  // observable-change generation (inputs + state commits)\n");
  hout->append("  uint64_t __done_pos = 0, __kids_pos = 0;\n");
  if (has_fall) {
    hout->append("  uint64_t __done_neg = 0, __kids_neg = 0;\n");
  }
  hout->append("  uint64_t __done_settle = 0, __kids_settle = 0;\n");
  if (splits_ != nullptr) {
    if (auto sitg = splits_->find(g); sitg != splits_->end()) {
      for (size_t gi0 = 0; gi0 < sitg->second.size(); ++gi0) {
        hout->append("  uint64_t __done_g", std::to_string(gi0), " = 0, __kids_g", std::to_string(gi0), " = 0;\n");
      }
    }
  }
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
  // Slice-group EXPORT members: one Slop per published bit range (see the
  // slice-export block in the period body). Written only by `__settle_g<k>`.
  if (splits_ != nullptr) {
    if (auto sit0 = splits_->find(g); sit0 != splits_->end()) {
      for (size_t gi0 = 0; gi0 < sit0->second.size(); ++gi0) {
        const auto& sg0 = sit0->second[gi0];
        for (size_t j0 = 0; j0 < sg0.outs.size(); ++j0) {
          if (sg0.outs[j0].len == 0) {
            continue;
          }
          hout->append(absl::StrCat("  Slop<",
                                    sg0.outs[j0].len,
                                    "> __ps_g",
                                    gi0,
                                    "_",
                                    j0,
                                    "{};  // slice export, out pid ",
                                    sg0.outs[j0].pid,
                                    " [",
                                    sg0.outs[j0].lo + sg0.outs[j0].len - 1,
                                    ":",
                                    sg0.outs[j0].lo,
                                    "]\n"));
        }
      }
    }
  }

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
        add(io.field, io.bits, absl::StrCat("__in.", io.field), true, true);  // clock gets the dedicated waveform
      }
    }
    for (const auto& io : ios) {
      if (!io.is_input) {
        add(io.field, io.bits, absl::StrCat("__o.", io.field));
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
  hout->append("  void reset_cycle(bool zero_uninitialized = false);\n");
  // The two halves of a tick, each callable on its own so a PARENT can order a
  // child's phases (a negedge consumer in the parent needs the child's fall to
  // have happened, but not its rise a second time). `eval_negedge` is emitted
  // only for a module that has negedge state -- dino has none, so it runs one
  // pass per tick exactly as before.
  // PORTS ARE MEMBERS (`__in`): a caller writes `child.__in.<field> = ...`
  // directly and the eval methods take NO argument — no fresh In per call, no
  // by-value struct copy at each boundary, and every instance's checkpoint
  // dumps the inputs it actually saw (the old In-by-value flow left
  // sub-instance `__in` permanently zero). One deliberate semantic shift: a
  // field a call site does not write holds its LAST value, not zero — closer
  // to hardware, and the direction that made the whole-array In-drop loud
  // instead of silently-zero. This is also the substrate for change-gated
  // evaluation (an unchanged `__in` + unadvanced state can skip a settle).
  hout->append("  void eval_posedge();  // rise: comb from pre-edge state, then commit posedge state\n");
  if (has_fall) {
    hout->append("  void eval_negedge();  // fall: the negedge cones, re-read post-rise, then commit them\n");
  }
  if (has_refresh) {
    hout->append("  void refresh_negedge();  // refresh nested mixed-edge state after post-rise inputs settle\n");
  }
  // One ROOT clock edge is SETTLE -> COMMIT -> SETTLE. cycle()'s body settles
  // the comb cone from the pre-edge state (computing both `o`/__last_out and
  // every next-state) and commits it. The hierarchy root owns the recursive
  // post-edge settle. A child on a parent-level word cycle passes __finish=true
  // too: its post-commit state-only outputs are the boundary values that break
  // that settle ring. Acyclic children pass false and avoid computing the same
  // post-edge outputs once privately and then again in the root walk.
  //
  // The trailing settle is what makes a `sigref` possible, and it must NOT be
  // hoisted to the front of the next cycle(): committing a next-state that was
  // settled against the PREVIOUS period's `__in` would make every driven input
  // land one cycle late (`acc.reset = clock < 2` would reset cycles 1-2, not
  // 0-1). Both settles are load-bearing and they settle against different state.
  hout->append("  const Out& cycle(bool __finish = true) {  // one clock period (inputs already written into __in)\n");
  hout->append("    eval_posedge();\n");
  if (has_fall) {
    hout->append("    eval_negedge();\n");
  }
  hout->append("    if (__finish) __settle();  // cyclic boundaries and the root need a post-edge output view\n");
  if (has_refresh) {
    hout->append("    if (__finish) refresh_negedge();  // only after post-rise inputs have propagated\n");
  }
  hout->append("    return __last_out;  // the during-period outputs the rise recorded\n");
  hout->append("  }\n");
  // The testbench writes __in fields directly (no compare-on-write), so step()
  // force-bumps __gen: the top instance always evaluates; gating lives at the
  // instance boundaries below it.
  hout->append("  void step() { ++__gen; cycle(); }  // drive __in, then advance one clock\n");
  hout->append("  void __settle();  // recompute __out from the CURRENT committed state; no commit, no VCD, no state change\n");
  // Per-output-group settle methods (callee partitioning): declared only for
  // definitions some parent instantiates on a word-level cycle.
  if (splits_ != nullptr) {
    if (auto sit = splits_->find(g); sit != splits_->end()) {
      for (size_t gi = 0; gi < sit->second.size(); ++gi) {
        hout->append("  void __settle_g",
                     std::to_string(gi),
                     "();  // group settle: only the output cones sharing one input-support\n");
      }
    }
  }
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

  // ---- reset_cycle: establish each state element's power-on value (using the
  // `initial` pin when present). `zero_uninitialized` is the opt-in
  // sim.init_zero policy: it supplies an initial zero ONLY when neither an
  // initializer nor reset exists. It never changes the reset expression used
  // by eval_rise/eval_negedge. ----
  fout->append("void ", mod, "::reset_cycle(bool zero_uninitialized) {\n");
  fout->append("    (void)zero_uninitialized;\n");
  for (const auto& f : flops) {
    auto        init  = get_driver(find_sink_pin(f.node, "initial"));
    auto        reset = get_driver(find_sink_pin(f.node, "reset_pin"));
    const auto  zv    = absl::StrCat("Slop<", f.bits, ">::create_integer(0)");
    const auto  xv    = absl::StrCat("Slop<", f.bits, ">::unknown(", f.bits, ")");
    std::string rv;
    if (!init.is_invalid()) {
      rv = operand(init, f.bits);
    } else if (reset.is_invalid()) {
      rv = absl::StrCat("zero_uninitialized ? ", zv, " : ", xv);
    } else {
      rv = xv;
    }
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
    // Power-on contents: apply the comptime `init` bus (ROM / `const`/`mut`
    // array initial value) when present. Otherwise preserve unknown power-on
    // state, except that sim.init_zero supplies zero when there is ALSO no
    // reset. A runtime initializer counts as present but cannot be applied here,
    // so it receives an unspecified seeded value rather than being silently
    // replaced by zero.
    //
    // The unknown fill is PER ENTRY. A whole-array `apply_update(Slop<bits*size>
    // ::unknown(...))` instantiates one Slop as wide as the array -- Slop<262144>
    // for a 4096x64 memory -- so both host-compile time and stack footprint would
    // scale with the array instead of with its entry width. Only the comptime
    // `init` bus, which genuinely IS a whole-array value, keeps that shape.
    const auto mem_unknown = absl::StrCat("Slop<", m.bits, ">::unknown(", m.bits, ")");
    if (!m.init.is_invalid() && is_const_pin(m.init)) {
      fout->append(absl::StrCat("    ", m.member, ".apply_update(", operand(m.init, m.bits * m.size), ");\n"));
    } else if (m.init.is_invalid() && m.reset.is_invalid()) {
      // Uninitialized, reset-free memory powers on ZERO under BOTH policies.
      // This is what cgen_memory_*.v under a 2-state simulator (Verilator)
      // reads back, and mem_wensize_lanes pins it: a wensize lane the test
      // never writes must read 0, which a PRNG fill turned into a
      // seed-dependent value. The flop power-on PRNG ruling (2026-08-09) is
      // about FLOPS and stands unchanged; sim.init_zero still exists to zero
      // those. State WITH a runtime initializer or a reset keeps the seeded
      // unknown fill below -- lhd_sim_init_zero_test pins that reset-only
      // state stays unknown until its reset actually asserts.
      fout->append(absl::StrCat("    ", m.member, ".fill(Slop<", m.bits, ">::create_integer(0));\n"));
    } else {
      fout->append(absl::StrCat("    for (auto& __e : ", m.member, ") __e = ", mem_unknown, ";\n"));
    }
    fout->append(absl::StrCat("    ", m.member, ".clear_pending();\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("    ",
                                  m.member,
                                  "_q",
                                  p.rdidx,
                                  " = zero_uninitialized ? Slop<",
                                  m.bits,
                                  ">::create_integer(0) : Slop<",
                                  m.bits,
                                  ">::unknown(",
                                  m.bits,
                                  ");\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append("    ", s.inst, ".reset_cycle(zero_uninitialized);\n");
  }
  // Leave the design SETTLED at t=0. Every read a testbench makes before its
  // first `step` -- and the docs use that shape (05-assert.md's `assert(x == 0)`
  // ahead of any step) -- goes through __out, which would otherwise be
  // default-zero rather than the outputs the reset state actually drives. Also
  // covers load_state(): __out is derived, absent from the checkpoint, and this
  // is what re-derives it after a restore.
  fout->append("    ++__gen;  // state re-initialized: every gated evaluation must recompute\n");
  fout->append("    __settle();\n");
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
  bool settle_mode = false;
  // The three emissions of one clock period. Rise and Fall are the two halves of
  // the tick; Settle is the trailing comb refresh that keeps `__out` (and any
  // `sigref` bound to it) current. A module with no negedge state emits no Fall
  // at all, so dino stays exactly one pass per tick.
  enum class Pass { Rise, Fall, Settle };
  // OUTPUT-GROUP emission state (the no-inlining callee partitioning). When a
  // group is active, the settle body is restricted to that group's output
  // cones and writes only that group's `__out` fields, emitted as
  // `__settle_g<idx>(In)`. `nullptr` = the ordinary full passes.
  const Split_group*                         active_group     = nullptr;
  int                                        active_group_idx = -1;
  // Output field -> port id (the group tables speak pids; `ios` speaks names).
  absl::flat_hash_map<std::string, uint32_t> out_field2pid;
  if (gio) {
    for (const auto& d : gio->get_output_pin_decls()) {
      for (const auto& io : ios) {
        if (!io.is_input && io.raw == d.name) {
          out_field2pid[io.field] = static_cast<uint32_t>(d.port_id);
          break;
        }
      }
    }
  }
  absl::flat_hash_set<hhds::Class_index> settle_cone;
  auto                                   build_settle_cone = [&](const Split_group* group) {
    settle_cone.clear();
    std::vector<hhds::Pin_class> work;
    if (group != nullptr) {
      for (const auto& o : group->outs) {
        if (o.len != 0 && !o.leaf.is_invalid()) {
          work.push_back(o.leaf);  // slice: only THIS range's producing cone
          continue;
        }
        for (const auto& d : gio->get_output_pin_decls()) {
          if (static_cast<uint32_t>(d.port_id) != o.pid) {
            continue;
          }
          auto spin = g->get_output_pin(d.name);
          if (!spin.is_invalid()) {
            work.push_back(get_driver(spin));
          }
          break;
        }
      }
    } else {
      for (const auto& io : ios) {
        if (io.is_input) {
          continue;
        }
        auto spin = g->get_output_pin(io.raw);
        if (!spin.is_invalid()) {
          work.push_back(get_driver(spin));
        }
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
  };
  build_settle_cone(nullptr);

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
        // ...AND the backward cone of its INPUTS. The fall pass starts from a
        // cleared pin2var, so every value the deferred call needs must be
        // re-emitted by this walk; inserting only the Sub node left its inputs
        // outside the cone, and `ensure_ready` cannot rescue a Memory dout
        // (it returns early for Memory/registers, on the assumption that an
        // unbound one means a real cycle — true in the rise, false here). The
        // symptom was `negedge-operand-unresolved` naming a memory read on a
        // module with no negedge state and no latch of its own: minion's
        // minion_frontend_thread_buffer, whose only negedge-ness is that its
        // clock-gate child is negedge_only.
        std::vector<hhds::Pin_class> sw;
        for (const auto& e : s.node.inp_edges()) {
          sw.push_back(e.driver);
        }
        while (!sw.empty()) {
          auto pp = sw.back();
          sw.pop_back();
          if (pp.is_invalid() || is_const_pin(pp) || !seen.insert(pp.get_class_index()).second) {
            continue;
          }
          auto n = pp.get_master_node();
          fall_cone.insert(n.get_class_index());
          if (is_type_register(n)) {
            continue;  // state: its q is emitted by its own node, not walked through
          }
          for (const auto& e : n.inp_edges()) {
            sw.push_back(e.driver);
          }
        }
      }
    }
  }

  auto emit_period_body = [&](Pass pass_) -> bool {
    const bool settle     = pass_ == Pass::Settle;
    const bool fall       = pass_ == Pass::Fall;
    // Where this method's text begins: the dead-temporary sweep detaches and
    // rewrites exactly this region once the body is closed (see strip_dead_temps).
    const auto body_mark  = fout->mark();
    auto       close_body = [&] {  // call on EVERY successful exit (the settle closes early, below)
      auto body = fout->detach_from(body_mark);
      strip_dead_temps(body);
      fout->append(body);
    };
    // `settle_mode` means "do NOT advance a child" -- true for the fall as well as
    // the settle. A sub-instance already ran its whole `cycle()` (both halves plus
    // its own trailing settle) during this module's RISE, so all the fall needs
    // from it is its outputs re-evaluated against the inputs the rise just
    // committed: a comb re-settle, not a second advance. That is precisely what
    // the old `negedge-through-instance` refusal existed to prevent, and why it
    // can now be lifted -- before the settle/commit split there was no way to ask
    // a child for its outputs without also stepping it.
    settle_mode = settle || fall;
    if (pass_ != Pass::Rise) {
      // Fresh binding environment. The settle reads the COMMITTED members rather
      // than the temporaries the rise left behind; the fall likewise starts from
      // the state the rise just committed, which is what makes its half-cycle
      // handover fall out of an ordinary walk instead of the invalidate/re-ready
      // dance a single monolithic body needed.
      pin2var.clear();
      canonical_.clear();
      seq_volatile_.clear();
      if (active_group != nullptr) {
        fout->append("void ", mod, "::__settle_g", std::to_string(active_group_idx), "() {\n");
      } else {
        fout->append("void ", mod, "::", settle ? "__settle" : "eval_negedge", "() {\n");
      }
    } else {
      fout->append("void ", mod, "::eval_posedge() {\n");
    }
    // ---- CHANGE-GATED evaluation (see the __gen comment in the header). Skip
    // when this method last ENTERED with the same (own __gen, sum of child
    // __gen) — i.e. nothing it reads changed and its last run was a fixed point
    // (a run that committed a change grew __gen past the stored entry value,
    // and a child that advanced grew the sum). Storing the ENTRY values, never
    // the exit ones, is what keeps a mid-flight pipeline running: its own
    // commits keep the stored pair behind until the subtree quiesces. Disabled
    // under VCD (a skipped body would freeze __vcd_tick and the waveform). ----
    std::string gate_done, gate_kids, gate_ksum;
    if (vcd_file.empty() && ::getenv("LIVEHD_SIM_NOGATE") == nullptr) {
      if (active_group != nullptr) {
        gate_done = absl::StrCat("__done_g", active_group_idx);
        gate_kids = absl::StrCat("__kids_g", active_group_idx);
      } else if (pass_ == Pass::Rise) {
        gate_done = "__done_pos";
        gate_kids = "__kids_pos";
      } else if (fall) {
        gate_done = "__done_neg";
        gate_kids = "__kids_neg";
      } else {
        gate_done = "__done_settle";
        gate_kids = "__kids_settle";
      }
      gate_ksum = "0ULL";
      for (const auto& s : subs) {
        absl::StrAppend(&gate_ksum, " + ", s.inst, ".__gen");
      }
      fout->append("    const uint64_t __g0 = __gen;\n");
      fout->append("    const uint64_t __k0 = ", gate_ksum, ";\n");
      fout->append("    if (", gate_done, " == __g0 && ", gate_kids, " == __k0) return;\n");
    }
    // map input ports and flop q outputs into pin2var
    for (const auto& io : ios) {
      if (!io.is_input) {
        continue;
      }
      auto pin = g->get_input_pin(io.raw);
      if (!pin.is_invalid()) {
        pin2var[pin.get_class_index()] = absl::StrCat("__in.", io.field);
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
    // GROUP-SCHEDULED instances (callee partitioning): outputs bind on demand
    // through `__settle_g<k>` calls, the atomic advance defers to after the walk.
    absl::flat_hash_set<hhds::Class_index>                                 group_sched;
    std::vector<const Sub*>                                                deferred_group;
    // (instance, group) pairs already evaluated this pass, and the subset whose
    // emission is CURRENTLY on the demand stack (a re-entry = the genuine cycle).
    absl::flat_hash_set<std::pair<hhds::Class_index, int>>                 emitted_groups;
    absl::flat_hash_set<std::pair<hhds::Class_index, int>>                 inflight_groups;
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
    // Nodes the SINGLE-PASS schedule sees on a word-level cycle (Sub and Memory as
    // ordinary nodes — exactly what `forward_class` does below).
    absl::flat_hash_set<hhds::Node_class> sched_cycle;
    livehd::graph_util::word_level_cycle_nodes(g, /*strict=*/true, sched_cycle);

    for (const auto& s : subs) {
      if (s.loop) {
        // A descriptor carry self-edge is the eager chain inside the compact
        // wrapper, not a parent-level combinational ring. A genuinely
        // negedge-only callee still advances in the parent's fall half.
        if (s.negedge_only) {
          fall_deferred.insert(s.node.get_class_index());
          // Deferring is only half of it. Every OTHER fall_deferred sub also
          // gets the `__pre` snapshot + pin2var pre-binding below (it reaches
          // that code with moore_capable true by construction), and skipping it
          // here left the loop's outputs unbound during the rise: a parent cell
          // reading one hits `raw_operand` with no pin2var entry, which sets
          // cycle_unresolved_ and aborts emission with a bogus
          // "combinational-cycle" refusal naming the loop instance — for a
          // design that has no combinational cycle at all.
          auto                                       loop_sio = s.node.get_subnode_io();
          absl::flat_hash_map<uint32_t, std::string> loop_pid2name;
          for (const auto& d : loop_sio->get_output_pin_decls()) {
            loop_pid2name[static_cast<uint32_t>(d.port_id)] = d.name;
          }
          // Only pins that EXIST (walk the out-edges): a declared-but-unread
          // output has no created pin and hhds' name lookup asserts on it.
          for (const auto& e : s.node.out_edges()) {
            auto opin = e.driver;
            if (opin.is_invalid() || pin2var.contains(opin.get_class_index())) {
              continue;
            }
            auto lit = loop_pid2name.find(static_cast<uint32_t>(opin.get_port_id()));
            if (lit != loop_pid2name.end()) {
              const auto field = cpp_port_path(lit->second);
              if (settle) {
                pin2var[opin.get_class_index()] = absl::StrCat(s.inst, ".__out.", field);
              } else {
                const auto var = absl::StrCat(s.inst, "__pre_p", static_cast<uint32_t>(opin.get_port_id()));
                fout->append(absl::StrCat("    const auto ",
                                          var,
                                          " = ",
                                          s.inst,
                                          ".__out.",
                                          field,
                                          ";  // negedge-only compact-loop boundary\n"));
                pin2var[opin.get_class_index()] = var;
              }
            }
          }
        }
        continue;
      }
      auto       fed_back      = sub_false_loop_output_pids(s.node, sub_out_is_state_only);
      const bool moore_capable = s.negedge_only || callee_is_moore(s.node.get_subnode_graph(), s.node.get_subnode_io());
      // A RING THROUGH SEVERAL INSTANCES. `sub_false_loop_output_pids` looks for
      // the instance's OWN output coming back, and it treats a Moore sibling's
      // output as a boundary — correctly. But then a ring of three Moore cells
      // (u0 -> u1 -> u2 -> u0, minion's `intpipe_mul_div_ctl` chain of
      // `prim_phase_pair_lo_hi`) reports `fed_back` EMPTY for every member, so
      // none was deferred, while the graph still had a Sub->Sub->Sub cycle that
      // `forward_class` cannot order. The result was `combinational-loop` on a
      // design whose every hop is through a latch — no comb path at all.
      //
      // A Moore callee's outputs are pure current-state reads, so pre-binding them
      // is always sound; the only reason not to do it unconditionally is the cost
      // of the `__pre` copy. Being ON the schedule's cycle is exactly the
      // condition that makes it worth paying.
      //
      // And the SAME argument holds per-output for a MEALY callee on the cycle:
      // `fed_back` walks stop at a Memory (a state boundary for the DIRECT
      // feedback question), so a ring that threads through a memory's ASYNC read
      // — vpu_ctrl's `mask.ex_mask_rf_q[ex_thread_id]` where the ADDRESS comes
      // from `trans.ex_trans_thread_id_o`, a pure flop read, while `trans`'s own
      // input needs the memory's dout — reports `fed_back` EMPTY for the very
      // instance whose state-only output would break it. Likewise vpu_ml's
      // tensor 3-clique, where every crossing output is a function of registers
      // THROUGH comb logic (`enabled = enabled_int | ...`) and each callee is
      // Mealy only because of unrelated dcache outputs. Pre-binding the
      // state-only outputs of ANY sub the schedule sees on a word-level cycle is
      // sound for exactly the Moore reason, output by output.
      const bool on_cycle      = sched_cycle.contains(s.node);
      if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
        std::fprintf(stderr,
                     "[splitdbg] %s: sub %s on_cycle=%d fed_back=%zu negedge=%d moore=%d\n",
                     gname.c_str(),
                     s.inst.c_str(),
                     on_cycle ? 1 : 0,
                     fed_back.size(),
                     s.negedge_only ? 1 : 0,
                     moore_capable ? 1 : 0);
      }
      if (fed_back.empty() && !s.negedge_only && !on_cycle) {
        continue;
      }
      const bool                    moore = moore_capable;
      absl::flat_hash_set<uint32_t> state_only;
      const bool                    groups_avail = splits_ != nullptr && [&] {
        auto cg2 = s.node.get_subnode_graph();
        return cg2 && splits_->contains(cg2.get());
      }();
      // A NEGEDGE-ONLY callee ON A RING takes the GROUP path (its comb cones
      // answer demands per group in every pass), with the atomic advance placed
      // in the parent's FALL by the deferred_group emission — the fall_deferred
      // path would pre-bind its ring-feeding comb output one period STALE (the
      // fall rebuilds inputs from `__out` refreshed at the PREVIOUS period's
      // fall; tests/sim/neg_flop_in_callee_ring.prp).
      const bool neg_group = s.negedge_only && on_cycle && groups_avail;
      if (s.negedge_only && !neg_group) {
        fall_deferred.insert(s.node.get_class_index());
      } else if (moore && !neg_group) {
        moore_deferred.insert(s.node.get_class_index());
      } else {
        state_only = callee_state_only_outputs(s.node.get_subnode_graph(), s.node.get_subnode_io());
        if (on_cycle && groups_avail) {
          // Callee partitioning owns the ordering: outputs bind on demand via
          // `__settle_g<k>`, the atomic advance defers, and the INPUT-INDEPENDENT
          // outputs take the cheaper `__pre` prebind below. The prebind set comes
          // from the REGISTRY (an output in no group has empty comb support per
          // the hierarchical port-reach summaries), NOT from the local
          // classifier: that one conservatively disqualifies any output whose
          // cone passes through a nested Sub, and minion's txfmactl reaches its
          // state-only control decodes through comb ROM-lookup callees — leaving
          // them neither prebound nor group-callable refused the whole cone.
          group_sched.insert(s.node.get_class_index());
          auto                          cg2 = s.node.get_subnode_graph();
          auto                          sit = splits_->find(cg2.get());
          absl::flat_hash_set<uint32_t> grouped;
          for (const auto& sg : sit->second) {
            for (const auto& o : sg.outs) {
              grouped.insert(o.pid);
            }
          }
          state_only.clear();
          if (auto sio2 = s.node.get_subnode_io(); sio2) {
            for (const auto& od : sio2->get_output_pin_decls()) {
              if (!grouped.contains(static_cast<uint32_t>(od.port_id))) {
                state_only.insert(static_cast<uint32_t>(od.port_id));
              }
            }
          }
          if (state_only.empty()) {
            continue;  // nothing to prebind
          }
        } else {
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
          if (fed_back.empty() && state_only.empty()) {
            continue;  // on-cycle Mealy with nothing pre-bindable: nothing to gain
          }
          mealy_prebound.insert(s.node.get_class_index());
        }
      }
      // The child's outputs for its CURRENT committed state are exactly what its
      // own trailing settle left in `__out` -- which is what the old
      // `peek({})` recomputed, at the price of snapshotting and restoring the
      // whole child subtree. In the CYCLE pass each demanded FIELD must be
      // snapshotted: the deferred `<inst>.cycle(...)` emitted after the walk
      // refreshes `__out`, and these bindings have to keep reading the pre-call
      // values. Copying the entire Out struct here is unnecessary and can be
      // enormous. In the SETTLE pass
      // nothing advances the child, so bind straight at the member; the only
      // writer there is a Mealy sub's own `__settle`, and every pin bound here is
      // a pure state read, which a settle does not change.
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
          const auto field = cpp_port_path(it->second);
          if (settle) {
            pin2var[opin.get_class_index()] = absl::StrCat(s.inst, ".__out.", field);
          } else {
            const auto var = absl::StrCat(s.inst, "__pre_p", pid);
            fout->append(absl::StrCat("    const auto ",
                                      var,
                                      " = ",
                                      s.inst,
                                      ".__out.",
                                      field,
                                      s.negedge_only ? ";  // negedge-only sub boundary\n"
                                      : moore        ? ";  // Moore state-output boundary\n"
                                                     : ";  // Mealy state-only output boundary\n"));
            pin2var[opin.get_class_index()] = var;
          }
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
    // One memory emission, shared by the scheduled walk below and the on-demand
    // path inside ensure_ready — the same treatment emit_sub_call gets, for the
    // same reason. forward_class emits every loop_last node (Memory, Sub) as a
    // SOURCE in storage order, so whether a memory whose dout feeds another
    // source's input cone was emitted "in time" was pure luck of that order.
    // Emitting it on demand makes the order irrelevant: reads bind dout pins,
    // and a fwd/program-ordered memory's staged writes pull their own operand
    // cones recursively. The guard is load-bearing: reads must bind exactly
    // once, and a re-entry DURING the memory's own emission (a genuine
    // dout -> write cone -> dout cycle) returns without binding, which falls
    // through to the loud unbound-operand diagnostic.
    // Demand-chain trace for the loud refusals below (env-gated): who asked for
    // what — group calls, atomic calls, memory emissions — when a cycle closes.
    std::vector<std::string>               demand_stack;
    absl::flat_hash_set<hhds::Class_index> emitted_mems;
    auto                                   emit_memory = [&](auto&& ensure_fn, const hhds::Node_class& node) -> void {
      if (!emitted_mems.insert(node.get_class_index()).second) {
        return;
      }
      // Emit the read data (read-first: the current array) and register each
      // read port's dout driver pin so downstream nodes resolve. Writes commit
      // at the edge (below). Sync read (type 0) consumers see the dout register.
      for (const auto& m : mems) {
        if (m.node.get_class_index() != node.get_class_index()) {
          continue;
        }
        demand_stack.push_back(absl::StrCat(m.member, ".mem"));
        struct PopM {
          std::vector<std::string>* v;
          ~PopM() { v->pop_back(); }
        } pop_m{&demand_stack};
        // Combinational whole-array: the `update` bus IS the contents this cycle
        // (no clock edge), so scatter it into `member` BEFORE the reads so reads
        // and read_all observe the post-update value. (Registered whole-arrays
        // apply update at the edge below; reads here see committed state.)
        if (m.is_whole() && !m.registered()) {
          ensure_fn(m.update);
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
            ensure_fn(wp.addr);
            if (!wp.enable.is_invalid()) {
              ensure_fn(wp.enable);
            }
            // The gated-clock guards emit_wen folds into the write enable are
            // cones too. Without readying them, `operand()` below degrades an
            // unemitted guard to the UNRESOLVED-CYCLE constant and the module
            // is refused with a bogus back-edge diagnostic.
            for (const auto& gp : m.clock_guards) {
              ensure_fn(gp);
            }
            // ordering="none" never reads the staged DATA -- the collision value
            // is undefined -- so only the address and enable are needed to
            // detect it. Skipping the din keeps a read-modify-write on a "none"
            // memory from becoming a false cycle too.
            std::string din = absl::StrCat("Slop<", m.bits, ">::create_integer(0)");
            if (m.order != Mem::Order::none) {
              ensure_fn(wp.din);
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
        // READ-PORT ORDER. Every latency-0 read of a REGISTERED memory returns
        // the same committed contents, so the reads are mutually independent —
        // but they were emitted in `rdidx` order, and each one prefetches its
        // own ADDRESS cone at its own position. When port 0's address is
        // computed from port 3's dout (a perfectly ordinary register-file
        // read-pointer chain: minion's `vpu_tensorfma` and `vpu_ctrl` both do
        // it), port 0 asked for a dout this loop had not emitted yet,
        // `ensure_ready` declined to bind it (a Memory dout is bound by its own
        // emission), and the result was reported as `combinational-loop` on the
        // memory. There is no cycle: only an emission order nobody had chosen.
        //
        // So choose one. Sort the read ports so a port whose address cone reads
        // another port's dout comes AFTER it. A genuine cycle among the ports
        // leaves the order untouched and still fails loudly below.
        std::vector<const MemPort*> rd_order;
        for (const auto& p : m.ports) {
          if (p.rd && !p.addr.is_invalid()) {
            rd_order.push_back(&p);
          }
        }
        // Only when every read port stages the SAME write prefix. `stage_through`
        // walks a monotonic counter, so under ordering="program" — the one mode
        // whose prefix varies per port — program order IS the contract and must
        // not be permuted.
        if (m.order != Mem::Order::program && rd_order.size() > 1) {
          absl::flat_hash_map<hhds::Class_index, int> dout2idx;
          for (size_t i = 0; i < rd_order.size(); ++i) {
            auto dp = node.create_driver_pin(static_cast<hhds::Port_id>(rd_order[i]->dout_pid));
            if (!dp.is_invalid()) {
              dout2idx[dp.get_class_index()] = static_cast<int>(i);
            }
          }
          // deps[i] = read ports whose dout appears in port i's address cone
          std::vector<std::vector<int>> deps(rd_order.size());
          for (size_t i = 0; i < rd_order.size(); ++i) {
            absl::flat_hash_set<hhds::Class_index> seen_p;
            std::vector<hhds::Pin_class>           work{rd_order[i]->addr};
            while (!work.empty()) {
              auto pp = work.back();
              work.pop_back();
              if (pp.is_invalid() || is_const_pin(pp) || !seen_p.insert(pp.get_class_index()).second) {
                continue;
              }
              if (auto it = dout2idx.find(pp.get_class_index()); it != dout2idx.end()) {
                if (it->second != static_cast<int>(i)) {
                  deps[i].push_back(it->second);
                }
                continue;  // the dout itself is the boundary
              }
              auto pn = pp.get_master_node();
              if (is_type_register(pn) || type_op_of(pn) == Ntype_op::Sub || type_op_of(pn) == Ntype_op::Memory) {
                continue;
              }
              for (const auto& pe : pn.inp_edges()) {
                work.push_back(pe.driver);
              }
            }
          }
          std::vector<int> indeg(rd_order.size(), 0);
          for (size_t i = 0; i < deps.size(); ++i) {
            indeg[i] = static_cast<int>(deps[i].size());
          }
          std::vector<const MemPort*> sorted_ports;
          std::vector<bool>           done(rd_order.size(), false);
          for (size_t pass = 0; pass < rd_order.size(); ++pass) {
            bool progress = false;
            for (size_t i = 0; i < rd_order.size(); ++i) {
              if (done[i]) {
                continue;
              }
              bool ready = true;
              for (const int d : deps[i]) {
                if (!done[static_cast<size_t>(d)]) {
                  ready = false;
                  break;
                }
              }
              if (ready) {
                done[i] = true;
                sorted_ports.push_back(rd_order[i]);
                progress = true;
              }
            }
            if (!progress) {
              break;  // a genuine cycle among the ports: keep the original order
            }
          }
          if (sorted_ports.size() == rd_order.size()) {
            rd_order = std::move(sorted_ports);
          }
        }
        for (const auto* pp_ : rd_order) {
          const auto& p    = *pp_;
          auto        dout = node.create_driver_pin(static_cast<hhds::Port_id>(p.dout_pid));
          if (m.type == 1) {
            pin2var[dout.get_class_index()] = absl::StrCat(m.member, "_q", std::to_string(p.rdidx));
            seq_volatile_.insert(dout.get_class_index());  // slop_update'd mid-sequential-section
          } else {
            // Latency-0 read: the memory resolves its own ordering mode against
            // the writes staged above, exactly as the cgen_memory wrapper's
            // UNDEF-then-FWD-then-stored chain does (and at latency 1 the same
            // resolved value is what the read register latches, at the edge).
            stage_through(read_prefix(p.rdidx));  // program-order staging, see above
            ensure_fn(p.addr);                    // computed read address emitted before this early (loop_last) memory node
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
    };

    absl::flat_hash_set<hhds::Class_index> emitted_subs;
    auto                                   sub_input_driver = [&](const Sub& s, std::string_view name, hhds::Port_id pid) {
      auto sink = find_sink_pin(s.node, name);
      if (!s.loop) {
        return get_driver(sink);
      }
      if (s.loop->index_input && pid == *s.loop->index_input) {
        return hhds::Pin_class{};  // occurrence-supplied by the wrapper
      }
      if (sink.is_invalid()) {
        // find_sink_pin returns an INVALID pin for a declared callee input with
        // no materialized pin (the non-loop path gets this guard for free from
        // get_driver). A compact loop node stores no external edge for any
        // descriptor-provided role or unbound input, and inp_edges() asserts on
        // a null graph_.
        return hhds::Pin_class{};
      }
      for (const auto& e : sink.inp_edges()) {
        if (e.driver.get_master_node() != s.node) {
          return e.driver;  // carry initial value or invariant
        }
      }
      return hhds::Pin_class{};
    };
    // A child's clock input still carries the ROOT clock as a data member; a
    // recognized Clock_cell's enables travel separately through `<port>__tick`
    // below.  Asking operand() for the cell output is both unnecessary and
    // forbidden (a Clock_cell has timing semantics, not a data value).
    auto child_clock_data_driver = [&](const Sub& s, uint32_t port_id, const hhds::Pin_class& drv) {
      auto cdef = s.node.get_subnode_graph();
      if (!cdef || !clock_guard_ports(cdef, port_cache).contains(port_id)) {
        return drv;
      }
      auto cone = livehd::latch_contract::clock_op_of(drv, design_clocks);
      if (cone && !cone->clock.is_invalid() && !cone->clock_inverted && cone->div == 1) {
        return cone->clock;
      }
      return drv;  // unsupported timing shapes keep the existing loud refusal
    };

    // Runtime skip for a conditionally activated child. The hidden __valid
    // actual is the complete caller/path guard. Reset is discovered from the
    // same structural interface analysis used by activation Clock_cell
    // insertion, including active-low polarity; if a reset cone is not fully
    // reducible to declared ports, return no condition and visit the child
    // unconditionally (safe, only less efficient).
    auto sub_run_condition = [&](auto&& ensure_fn, const Sub& s) -> std::string {
      if (s.loop) {
        return {};  // the compact wrapper applies its per-ordinal activation/reset guard
      }
      auto cdef = s.node.get_subnode_graph();
      auto sio  = s.node.get_subnode_io();
      if (!cdef || !sio) {
        return {};
      }

      hhds::Pin_class active;
      for (const auto& d : sio->get_input_pin_decls()) {
        if (d.name == "__valid") {
          active = sub_input_driver(s, d.name, d.port_id);
          break;
        }
      }
      if (active.is_invalid() || (is_const_pin(active) && hydrate_const(active).is_known_true())) {
        return {};
      }
      // The caller already skipped this whole definition when its own
      // top-level activation was false. Repeating that guard on every
      // unconditional child is wasted work, and for group-scheduled callees it
      // can put a recursively demanded group's declarations inside another
      // group's lexical `if` even though later consumers need them outside.
      // A local path condition is an And/Mux cone and therefore does not reduce
      // to this graph-input root; it still receives the runtime guard below.
      const auto active_root = livehd::latch_contract::control_root(active).net;
      if (!active_root.is_invalid() && livehd::graph_util::is_graph_input_pin(active_root)
          && pin_name_of(active_root) == "__valid") {
        return {};
      }
      ensure_fn(active);
      if (!is_const_pin(active) && !pin2var.contains(active.get_class_index())) {
        return {};  // unresolved feedback: the ordinary call path diagnoses it
      }
      std::string cond = absl::StrCat("(", operand(active, 1), ").is_known_true()");

      const auto& resets = reset_guard_ports(cdef, port_cache);
      if (!resets.complete || resets.ports.size() > 1) {
        return {};  // rule 16: no skip when reset cannot be represented exactly
      }
      if (!resets.ports.empty()) {
        const auto&     rp = resets.ports.front();
        hhds::Pin_class reset;
        for (const auto& d : sio->get_input_pin_decls()) {
          if (static_cast<uint32_t>(d.port_id) == rp.port_id) {
            reset = sub_input_driver(s, d.name, d.port_id);
            break;
          }
        }
        if (reset.is_invalid()) {
          return {};
        }
        ensure_fn(reset);
        if (!is_const_pin(reset) && !pin2var.contains(reset.get_class_index())) {
          return {};
        }
        absl::StrAppend(&cond, " || (", operand(reset, 1), rp.active_low ? ").is_known_false()" : ").is_known_true()");
      }
      return cond;
    };

    // A GATED CLOCK CROSSING INTO THE CHILD. If this port carries the child's
    // clock and we are driving it with a recognized gate, the child cannot see
    // that: inside it the port is an ordinary graph input, so it would commit
    // every period with our gate as dead code. Tell it via the `<port>__tick`
    // guard word. The guard field is defaulted true, so an UNGATED port is
    // untouched — but ONLY write it when the child actually DECLARED the guard
    // (it really clocks state on this port); writing a field the callee never
    // emitted is a compile error, and a gate output used as ordinary DATA by
    // the child is legal and common. SHARED by every call path that fills a
    // child's inputs: emit_sub_call AND the deferred-Moore / deferred-fall
    // drains — the drains used to skip it, so a tick-guarded Moore or
    // negedge-only callee committed on gated-off periods (found by the
    // adversarial review of the ports-as-members redesign; the fresh-In flow
    // had the same hole, defaulting the guard true on every call).
    // `bind_only` runs the cone-BINDING half alone: a caller about to open a
    // runtime-activation `if` needs every declaration ensure_fn emits to land
    // at method scope, never inside that nested C++ block.
    auto emit_child_tick = [&](auto&&                 ensure_fn,
                               const Sub&             s,
                               std::string_view       port_name,
                               uint32_t               port_id,
                               const hhds::Pin_class& drv,
                               bool                   bind_only = false) -> void {
      const bool child_wants_tick = [&] {
        auto cdef = s.node.get_subnode_graph();
        if (!cdef) {
          return false;
        }
        // The RECURSIVE guard-port set: covers state clocked on the port
        // directly, through a gate inside the child, and ports the child
        // merely forwards into ITS children — the stateless-wrapper levels
        // of a multi-boundary gate chain (tests/sim/gate_through_wrapper).
        return clock_guard_ports(cdef, port_cache).contains(port_id);
      }();
      if (!child_wants_tick) {
        return;
      }
      std::string     cond;
      hhds::Pin_class root = drv;
      if (auto cone = livehd::latch_contract::clock_op_of(drv, design_clocks); cone) {
        if (!cone->enables.empty() && !cone->clock_inverted && cone->div == 1) {
          for (const auto& gp : cone->enables) {
            ensure_fn(gp);
            absl::StrAppend(&cond, cond.empty() ? "" : " && ", "(", operand(gp, 1), ").is_known_true()");
          }
          root = cone->clock;
        } else if (!cone->enables.empty()) {
          root = {};  // inverted / divided gate: same named limits as the local fold — no guard word
        } else {
          root = cone->clock;
        }
      } else {
        root = livehd::latch_contract::control_root(drv).net;
      }
      // FORWARD my own guard word: when the (root of the) net feeding the
      // child's clock port is one of MY guarded input ports, whatever my
      // parent says about that port composes into what I tell the child.
      if (!root.is_invalid() && livehd::graph_util::is_graph_input_pin(root)) {
        for (const auto& io : ios) {
          if (io.is_input && io.raw == pin_name_of(root) && clock_in_fields.contains(io.field)) {
            absl::StrAppend(&cond, cond.empty() ? "" : " && ", "__in.", io.field, "__tick");
            break;
          }
        }
      }
      if (!cond.empty() && !bind_only) {
        fout->append(absl::StrCat("    ",
                                  s.inst,
                                  ".__gen += slop_update(",
                                  s.inst,
                                  ".__in.",
                                  cpp_port_path(port_name),
                                  "__tick, ",
                                  cond,
                                  ");  // gated clock into the child: it commits only when the gate is open\n"));
      }
    };

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
        demand_stack.push_back(absl::StrCat(s.inst, ".atomic"));
        struct PopA {
          std::vector<std::string>* v;
          ~PopA() { v->pop_back(); }
        } pop_a{&demand_stack};
        const std::string run_condition = sub_run_condition(ensure_fn, s);
        const bool        runtime_skip  = !run_condition.empty();
        // A negedge-only child advances ONLY its fall half here (eval_negedge +
        // a re-settle), never `cycle()`, so there is no `<inst>__o` snapshot to
        // name: like settle mode, its outputs bind straight at `__out`, which
        // the re-settle just refreshed. This IS reachable with `settle_mode`
        // false -- the deferred_group advance clears it for exactly this case
        // (advance = negedge_only ? Fall : Rise) -- and reading `<inst>__o`
        // there named an undeclared identifier in the generated C++.
        const bool        fall_advance  = fall && s.negedge_only;
        const bool        out_is_member = settle_mode || fall_advance;
        if (runtime_skip && !out_is_member) {
          // cycle() returns the pre-edge observation. Keep it outside the if so
          // downstream caller muxes can read a stable (masked) value when the
          // child is inactive.
          fout->append(absl::StrCat("    const auto* ", s.inst, "__o = &", s.inst, ".__out;\n"));
        }
        if (runtime_skip) {
          // BIND the input cones before the guard opens. ensure_fn DECLARES
          // what it binds at the current output position (`Slop<W> cg_N`, and a
          // demanded nested instance's own `<inst>__o`) while pin2var keeps
          // naming it for the rest of the method, so a cone first reached
          // inside the `if` would be scoped to that C++ block and out of scope
          // at its later consumers — the generated file would not compile.
          // It also keeps a demanded nested instance's advance unconditional:
          // emitted_subs marks it done, so an advance left under a false guard
          // is simply lost. Exactly the drivers the fill loop below asks for,
          // so nothing extra is demanded.
          for (const auto& d : sio->get_input_pin_decls()) {
            if (s.loop && s.loop->index_input && d.port_id == *s.loop->index_input) {
              continue;
            }
            auto pre_drv = sub_input_driver(s, d.name, d.port_id);
            ensure_fn(child_clock_data_driver(s, static_cast<uint32_t>(d.port_id), pre_drv));
            emit_child_tick(ensure_fn, s, d.name, static_cast<uint32_t>(d.port_id), pre_drv, /*bind_only=*/true);
          }
          fout->append(absl::StrCat("    if (", run_condition, ") {  // conditional activation (reset keeps it open)\n"));
        }
        // Ports are members: write straight into the child's __in — no local In,
        // no by-value copy at the call.
        for (const auto& d : sio->get_input_pin_decls()) {
          if (s.loop && s.loop->index_input && d.port_id == *s.loop->index_input) {
            continue;  // the wrapper supplies first + ordinal*step
          }
          auto drv       = sub_input_driver(s, d.name, d.port_id);
          int  wb        = d.bits > 0 ? static_cast<int>(d.bits) : 1;
          auto value_drv = child_clock_data_driver(s, static_cast<uint32_t>(d.port_id), drv);
          // Emit any pending operand cone on demand (conservative: stops at
          // state elements / consts; another atomic Sub recurses through this
          // same helper). A genuinely cyclic cone stays unbound and falls
          // through to the loud Stage-0 diagnostic below.
          ensure_fn(value_drv);
          // Stage 0: a valid, non-const driver feeding this instance input that is
          // not yet bound is a combinational cycle threading THROUGH this atomic Sub
          // call (the false-loop-through-instance case). Report it precisely.
          if (!value_drv.is_invalid() && !is_const_pin(value_drv) && !pin2var.contains(value_drv.get_class_index())
              && !cycle_reported_) {
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
          fout->append(absl::StrCat("    ",
                                    s.inst,
                                    ".__gen += slop_update(",
                                    s.inst,
                                    ".__in.",
                                    cpp_port_path(d.name),
                                    ", ",
                                    operand(value_drv, wb),
                                    ");\n"));
          emit_child_tick(ensure_fn, s, d.name, static_cast<uint32_t>(d.port_id), drv);
        }
        // In settle mode the child is re-settled against the inputs just rebuilt
        // from the parent's COMMITTED state, and its outputs are read from its
        // own __out member. cycle() would advance it a second time in the period.
        if (fall_advance) {
          fout->append(absl::StrCat("    ", s.inst, ".eval_negedge();\n"));
          fout->append(absl::StrCat("    ", s.inst, ".__settle();\n"));
        } else if (settle_mode) {
          fout->append(absl::StrCat("    ", s.inst, ".__settle();\n"));
        } else if (runtime_skip) {
          fout->append(
              absl::StrCat("    ", s.inst, "__o = &", s.inst, sched_cycle.contains(node) ? ".cycle();\n" : ".cycle(false);\n"));
        } else {
          fout->append(absl::StrCat("    const auto& ",
                                    s.inst,
                                    "__o = ",
                                    s.inst,
                                    sched_cycle.contains(node) ? ".cycle();\n" : ".cycle(false);\n"));
        }
        if (runtime_skip) {
          fout->append("    }\n");
        }
        const std::string sub_out = out_is_member  ? absl::StrCat(s.inst, ".__out.")
                                    : runtime_skip ? absl::StrCat(s.inst, "__o->")
                                                   : absl::StrCat(s.inst, "__o.");
        for (const auto& d : sio->get_output_pin_decls()) {
          auto opin = find_driver_pin(node, d.name);
          if (!opin.is_invalid()) {
            pin2var[opin.get_class_index()] = absl::StrCat(sub_out, cpp_port_path(d.name));
          }
        }
        break;
      }
    };

    // ---- GROUP evaluation of a group-scheduled instance (slice-aware) ----
    // A demanded output pin triggers exactly the group(s) that compute it. A
    // whole-port group binds the pin from a `__out` snapshot as before; SLICE
    // groups publish `__ps_g<k>_<j>` exports, and a whole-pin demand assembles
    // them once every covering group has run. In-fills are per SUPPORT ATOM:
    // a (pid, lo, len) atom fills only those bits of the callee's In field,
    // sliced out of the parent net by `slice_operand` — this is what lets two
    // packed buses exchange disjoint fields in one cycle without a false cycle.
    std::function<bool(const hhds::Node_class&, int)>                           emit_one_group;
    std::function<std::string(const hhds::Pin_class&, uint32_t, uint32_t, int)> slice_operand;
    std::function<void(const hhds::Pin_class&)>                                 ensure_ready_fn;  // set below

    // Slice [lo, lo+len) of the value a pin carries, as a Slop<len> expression.
    // Returns "" when the shape cannot be sliced (caller falls back to a whole
    // read, which may in turn refuse loudly on a genuine cycle).
    slice_operand = [&](const hhds::Pin_class& pin, uint32_t lo, uint32_t len, int depth) -> std::string {
      if (pin.is_invalid() || len == 0 || depth > 32) {
        return {};
      }
      auto whole_slice = [&]() -> std::string {
        auto w = operand(pin, static_cast<int>(lo + len));
        if (lo == 0) {
          return absl::StrCat("(", w, ").zext_to<", len, ">()");
        }
        return absl::StrCat("Slop<", len, ">::sra_op(", w, ", ", lo, ")");
      };
      if (is_const_pin(pin) || pin2var.contains(pin.get_class_index()) || livehd::graph_util::is_graph_input_pin(pin)) {
        return whole_slice();
      }
      auto       n  = pin.get_master_node();
      const auto op = type_op_of(n);
      if (op == Ntype_op::Sub) {
        if (!group_sched.contains(n.get_class_index()) || splits_ == nullptr) {
          return {};
        }
        auto cg = n.get_subnode_graph();
        if (!cg) {
          return {};
        }
        auto sit = splits_->find(cg.get());
        if (sit == splits_->end()) {
          return {};
        }
        const auto want_pid = static_cast<uint32_t>(pin.get_port_id());
        for (size_t gi = 0; gi < sit->second.size(); ++gi) {
          const auto& sg = sit->second[gi];
          for (size_t oj = 0; oj < sg.outs.size(); ++oj) {
            const auto& o = sg.outs[oj];
            if (o.pid != want_pid || o.len == 0 || o.lo > lo || lo + len > o.lo + o.len) {
              continue;
            }
            if (!emit_one_group(n, static_cast<int>(gi))) {
              return {};
            }
            std::string inst;
            for (const auto& s : subs) {
              if (s.node.get_class_index() == n.get_class_index()) {
                inst = s.inst;
                break;
              }
            }
            if (inst.empty()) {
              return {};
            }
            const std::string mem = absl::StrCat(inst, ".__ps_g", gi, "_", oj);
            if (o.lo == lo && o.len == len) {
              return absl::StrCat("(", mem, ")");
            }
            return absl::StrCat("Slop<", len, ">::sra_op(", mem, ".zext_to<", o.len, ">(), ", lo - o.lo, ")");
          }
        }
        return {};
      }
      if (op == Ntype_op::SHL) {
        hhds::Pin_class val, amt;
        for (const auto& e : n.inp_edges()) {
          if (e.sink.get_port_id() == 0) {
            val = e.driver;
          } else {
            amt = e.driver;
          }
        }
        if (val.is_invalid() || amt.is_invalid() || !is_const_pin(amt)) {
          return {};
        }
        auto av = hydrate_const(amt);
        if (!av.is_just_i64() || av.to_just_i64() < 0) {
          return {};
        }
        const auto k = static_cast<uint32_t>(av.to_just_i64());
        if (lo >= k) {
          return slice_operand(val, lo - k, len, depth + 1);
        }
        if (lo + len <= k) {
          return absl::StrCat("Slop<", len, ">::create_integer(0)");
        }
        return {};
      }
      if (op == Ntype_op::Or) {
        // bitwise: a slice of an OR is the OR of the slices — unconditionally
        std::string acc;
        for (const auto& e : n.inp_edges()) {
          auto part = slice_operand(e.driver, lo, len, depth + 1);
          if (part.empty()) {
            return {};
          }
          acc = acc.empty() ? part : absl::StrCat("Slop<", len, ">::or_op(", acc, ", ", part, ")");
        }
        return acc;
      }
      if (op == Ntype_op::Set_mask) {
        // A wire built field by field is a SET_MASK CHAIN (the producer idiom
        // concat_leaves decodes for the registry): value bits land LSB-aligned
        // in the contiguous masked range, everything else shows the base. A
        // requested slice fully INSIDE the range is a slice of the value; fully
        // OUTSIDE, a slice of the base; straddling the boundary is not the
        // disjoint-field idiom and fails to the caller's whole-read path.
        hhds::Pin_class base, msk, value;
        for (const auto& e : n.inp_edges()) {
          switch (e.sink.get_port_id()) {
            case 0 : base = e.driver; break;
            case 2 : msk = e.driver; break;
            case 4 : value = e.driver; break;
            default: break;
          }
        }
        if (msk.is_invalid() || !is_const_pin(msk)) {
          return {};
        }
        auto smv = hydrate_const(msk);
        if (smv.has_unknowns() || smv.is_negative()) {
          return {};
        }
        auto [smb, sme] = smv.get_mask_range();
        if (smb < 0 || sme <= smb) {
          return {};
        }
        const auto wlo = static_cast<uint32_t>(smb), whi = static_cast<uint32_t>(sme);
        if (lo >= wlo && lo + len <= whi) {
          return value.is_invalid() ? std::string{} : slice_operand(value, lo - wlo, len, depth + 1);
        }
        if (lo + len <= wlo || lo >= whi) {
          return base.is_invalid() ? std::string{} : slice_operand(base, lo, len, depth + 1);
        }
        return {};
      }
      if (op == Ntype_op::Get_mask) {
        hhds::Pin_class val, msk;
        for (const auto& e : n.inp_edges()) {
          if (e.sink.get_port_id() == 0) {
            val = e.driver;
          } else {
            msk = e.driver;
          }
        }
        if (val.is_invalid()) {
          return {};
        }
        if (msk.is_invalid()) {  // unary width adjust: identity for slicing
          return slice_operand(val, lo, len, depth + 1);
        }
        if (!is_const_pin(msk)) {
          return {};
        }
        auto mv = hydrate_const(msk);
        if (mv.has_unknowns()) {
          return {};
        }
        if (mv.is_just_i64() && mv.to_just_i64() == -1) {  // to-positive idiom: identity
          return slice_operand(val, lo, len, depth + 1);
        }
        if (mv.is_negative()) {
          return {};
        }
        auto [mb, me] = mv.get_mask_range();
        if (mb < 0 || me <= mb) {
          return {};
        }
        const auto avail = static_cast<uint32_t>(me - mb);
        if (lo >= avail) {
          return absl::StrCat("Slop<", len, ">::create_integer(0)");
        }
        const uint32_t eff = std::min(len, avail - lo);
        auto           e2  = slice_operand(val, static_cast<uint32_t>(mb) + lo, eff, depth + 1);
        if (e2.empty()) {
          return {};
        }
        if (eff == len) {
          return e2;
        }
        return absl::StrCat("(", e2, ").zext_to<", len, ">()");  // bits past the mask read 0
      }
      return {};
    };

    // Emit ONE group of one instance (guarded per pass). Returns false only on
    // an in-flight re-entry — the genuine slice-grain cycle.
    emit_one_group = [&](const hhds::Node_class& node, int gidx) -> bool {
      auto        cg  = node.get_subnode_graph();
      auto        sit = splits_->find(cg.get());
      const auto& grp = sit->second[static_cast<size_t>(gidx)];
      const auto  key = std::pair<hhds::Class_index, int>{node.get_class_index(), gidx};
      if (emitted_groups.contains(key)) {
        return !inflight_groups.contains(key);
      }
      emitted_groups.insert(key);
      inflight_groups.insert(key);
      bool ok = true;
      for (const auto& s : subs) {
        if (s.node.get_class_index() != node.get_class_index()) {
          continue;
        }
        auto sio = node.get_subnode_io();
        if (!sio) {
          break;
        }
        demand_stack.push_back(absl::StrCat(s.inst, ".g", gidx));
        struct Pop {
          std::vector<std::string>* v;
          ~Pop() { v->pop_back(); }
        } pop_{&demand_stack};
        absl::flat_hash_map<uint32_t, std::pair<std::string, int>> pid2in;
        for (const auto& d : sio->get_input_pin_decls()) {
          pid2in[static_cast<uint32_t>(d.port_id)] = {d.name, d.bits > 0 ? static_cast<int>(d.bits) : 1};
        }
        bool any_whole = false;
        for (const auto& o : grp.outs) {
          any_whole = any_whole || o.len == 0;
        }
        const std::string run_condition = sub_run_condition(ensure_ready_fn, s);
        const bool        runtime_skip  = !run_condition.empty();
        const std::string snap          = absl::StrCat(s.inst, "__g", gidx);
        struct Whole_out {
          hhds::Pin_class pin;
          std::string     field;
          std::string     var;
        };
        std::vector<Whole_out>                     whole_outs;
        absl::flat_hash_map<uint32_t, std::string> pid2name;
        for (const auto& d : sio->get_output_pin_decls()) {
          pid2name[static_cast<uint32_t>(d.port_id)] = d.name;
        }
        if (any_whole) {
          absl::flat_hash_set<pin_key_t> whole_seen;
          for (const auto& e : node.out_edges()) {
            auto opin = e.driver;
            if (opin.is_invalid() || pin2var.contains(opin.get_class_index())
                || !whole_seen.insert(opin.get_class_index()).second) {
              continue;
            }
            const auto epid  = static_cast<uint32_t>(opin.get_port_id());
            bool       whole = false;
            for (const auto& o : grp.outs) {
              if (o.pid == epid && o.len == 0) {
                whole = true;
                break;
              }
            }
            if (!whole) {
              continue;
            }
            if (auto itn = pid2name.find(epid); itn != pid2name.end()) {
              whole_outs.push_back({opin, cpp_port_path(itn->second), absl::StrCat(snap, "_p", epid)});
            }
          }
        }
        if (runtime_skip) {
          for (const auto& o : whole_outs) {
            fout->append(absl::StrCat("    auto ",
                                      o.var,
                                      " = ",
                                      s.inst,
                                      ".__out.",
                                      o.field,
                                      ";  // inactive group value is caller-masked\n"));
          }
        }
        // Ports are members: the group's support fields are written straight
        // into the child's persistent __in — one fill per distinct input pid;
        // atoms of the same pid OR together. A field NO group writes keeps its
        // last value (construction default until first written), which is the
        // stale-not-zero semantics of the member-port design.
        //
        // The fills are COLLECTED, not appended, so the guard `if` can be
        // opened after them: building an operand binds its cone HERE, and a
        // `Slop<W> cg_N` (or a demanded group's declaration) emitted inside
        // that nested C++ block would be out of scope at the later consumers
        // pin2var still names it for. Only the settle and the snapshot need
        // the guard.
        const std::string             gin = absl::StrCat(s.inst, ".__in");
        std::vector<std::string>      fills;
        absl::flat_hash_set<uint32_t> filled;
        for (const auto& a : grp.support) {
          if (!filled.insert(a.pid).second) {
            continue;
          }
          auto it = pid2in.find(a.pid);
          if (it == pid2in.end()) {
            continue;  // undeclared/unconnected: the member's default models it
          }
          auto      drv   = get_driver(find_sink_pin(node, it->second.first));
          const int wb    = it->second.second;
          // whole-port atom for this pid anywhere in the support?
          bool      whole = false;
          for (const auto& a2 : grp.support) {
            if (a2.pid == a.pid && a2.len == 0) {
              whole = true;
              break;
            }
          }
          std::string expr;
          if (!whole) {
            // partial fill: OR of (slice << lo) per atom; any failure -> whole
            std::string acc = absl::StrCat("Slop<", wb, ">::create_integer(0)");
            bool        all = true;
            for (const auto& a2 : grp.support) {
              if (a2.pid != a.pid) {
                continue;
              }
              auto se = slice_operand(drv, a2.lo, a2.len, 0);
              if (se.empty()) {
                all = false;
                break;
              }
              acc = absl::StrCat("Slop<", wb, ">::or_op(", acc, ", Slop<", wb, ">::shl_op(", se, ", ", a2.lo, "))");
            }
            if (all) {
              expr = acc;
            }
          }
          if (expr.empty()) {
            ensure_ready_fn(drv);
            if (!drv.is_invalid() && !is_const_pin(drv) && !pin2var.contains(drv.get_class_index()) && !cycle_reported_) {
              if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
                std::string chain;
                for (const auto& d2 : demand_stack) {
                  chain += d2 + " -> ";
                }
                std::fprintf(stderr,
                             "[splitdbg] DEMAND CHAIN: %sinput %s (blocked at %s)\n",
                             chain.c_str(),
                             it->second.first.c_str(),
                             debug_name(drv.get_master_node()).c_str());
              }
              livehd::diag::err("inou.cgen.sim", "comb-loop-through-instance", "unsupported")
                  .msg(
                      "combinational loop through instance `{}` ({}::{}): input `{}` is needed by output group {} and "
                      "is fed by logic that depends on that same group",
                      s.inst,
                      gname,
                      s.callee_struct,
                      it->second.first,
                      gidx)
                  .hint(
                      "the callee is split by output group and bit slice, so this cycle is REAL at the finest grain "
                      "the analysis can express: the demanded output's own cone reads an input that depends on it")
                  .emit();
              cycle_reported_ = true;
              ok              = false;
            }
            expr = operand(drv, wb);
          }
          fills.push_back(absl::StrCat("    ",
                                       s.inst,
                                       ".__gen += slop_update(",
                                       gin,
                                       ".",
                                       cpp_port_path(it->second.first),
                                       ", ",
                                       expr,
                                       ");\n"));
        }
        if (runtime_skip) {
          fout->append(absl::StrCat("    if (", run_condition, ") {  // conditional activation (reset keeps it open)\n"));
        }
        for (const auto& f : fills) {
          fout->append(f);
        }
        fout->append(absl::StrCat("    ", s.inst, ".__settle_g", gidx, "();\n"));
        // A whole output must remain stable after the deferred child cycle
        // overwrites __out. Snapshot only the demanded Slop field, never the
        // callee's entire Out struct (which can contain hundreds of unrelated
        // ports). Mixed groups still publish their slice members separately.
        for (const auto& o : whole_outs) {
          fout->append(absl::StrCat("    ",
                                    runtime_skip ? "" : "const auto ",
                                    o.var,
                                    " = ",
                                    s.inst,
                                    ".__out.",
                                    o.field,
                                    ";  // group ",
                                    gidx,
                                    " output\n"));
          pin2var[o.pin.get_class_index()] = o.var;
        }
        if (runtime_skip) {
          fout->append("    }\n");
        }
        break;
      }
      inflight_groups.erase(key);
      return ok;
    };

    // A `Get_mask(sub_out, contiguous-const)` consumer of a group-scheduled
    // instance binds from the SLICE that computes those bits — the whole-pin
    // path would demand every slice and manufacture a ring whenever a sibling
    // slice is mid-fill. Returns true when the node's value was bound.
    auto try_slice_read = [&](const hhds::Node_class& node) -> bool {
      // Decode the three slice-read idioms a packed field access lowers to —
      // Get_mask(x, contiguous), And(x, low-mask) (cprop's rewrite of a low
      // Get_mask), SRA(x, k), and the And(SRA(x, k), low-mask) composition —
      // the SAME set graph/port_reach's input_atom_of accepts, so a slice the
      // registry could refine is also a slice this consumer bind can serve.
      const auto      nop = type_op_of(node);
      hhds::Pin_class val;
      int64_t         lo = 0, len = 0;
      // Peel tolg's identity wrappers (unary Get_mask width adjust, the
      // to-positive `mask == -1` idiom, Sext) so the Sub-pin check below sees
      // the instance output itself — mirrors port_reach's peel_ident, which is
      // how the REGISTRY refined the very slice this consumer wants to bind.
      auto            peel_ident = [&](hhds::Pin_class p) -> hhds::Pin_class {
        for (int hops = 0; hops < 8 && !p.is_invalid(); ++hops) {
          if (is_const_pin(p) || livehd::graph_util::is_graph_input_pin(p)) {
            break;
          }
          auto       pn  = p.get_master_node();
          const auto pop = type_op_of(pn);
          if (pop == Ntype_op::Sext) {
            p = livehd::graph_util::first_value_driver(pn);
            continue;
          }
          if (pop == Ntype_op::Get_mask) {
            hhds::Pin_class v2, m2;
            for (const auto& e : pn.inp_edges()) {
              if (e.sink.get_port_id() == 0) {
                v2 = e.driver;
              } else {
                m2 = e.driver;
              }
            }
            if (m2.is_invalid()) {
              p = v2;  // unary width adjust
              continue;
            }
            if (is_const_pin(m2)) {
              auto m2v = hydrate_const(m2);
              if (m2v.is_just_i64() && m2v.to_just_i64() == -1) {
                p = v2;  // to-positive idiom
                continue;
              }
            }
            break;
          }
          break;
        }
        return p;
      };
      if (nop == Ntype_op::Get_mask || nop == Ntype_op::And) {
        hhds::Pin_class v, msk;
        int             n_ops = 0;
        for (const auto& e : node.inp_edges()) {
          ++n_ops;
          if (nop == Ntype_op::Get_mask ? e.sink.get_port_id() == 0 : !is_const_pin(e.driver)) {
            v = e.driver;
          } else {
            msk = e.driver;
          }
        }
        if (v.is_invalid() || msk.is_invalid() || !is_const_pin(msk) || (nop == Ntype_op::And && n_ops != 2)) {
          return false;
        }
        auto mv = hydrate_const(msk);
        if (mv.has_unknowns() || mv.is_negative()) {
          return false;
        }
        auto [mb, me] = mv.get_mask_range();
        if (mb < 0 || me <= mb || (nop == Ntype_op::And && mb != 0)) {
          return false;  // And only stands in for a LOW slice
        }
        val = peel_ident(v);
        lo  = mb;
        len = me - mb;
        // And(SRA(x, k), low-mask): compose to the [k, k+me) slice of x.
        if (nop == Ntype_op::And && !val.is_invalid() && !is_const_pin(val) && !livehd::graph_util::is_graph_input_pin(val)) {
          auto vn = val.get_master_node();
          if (type_op_of(vn) == Ntype_op::SRA) {
            hhds::Pin_class v2, amt;
            for (const auto& e : vn.inp_edges()) {
              if (e.sink.get_port_id() == 0) {
                v2 = e.driver;
              } else {
                amt = e.driver;
              }
            }
            if (!v2.is_invalid() && !amt.is_invalid() && is_const_pin(amt)) {
              auto av = hydrate_const(amt);
              if (av.is_just_i64() && av.to_just_i64() >= 0) {
                val  = peel_ident(v2);
                lo  += av.to_just_i64();
              }
            }
          }
        }
      } else if (nop == Ntype_op::SRA) {
        hhds::Pin_class v, amt;
        for (const auto& e : node.inp_edges()) {
          if (e.sink.get_port_id() == 0) {
            v = e.driver;
          } else {
            amt = e.driver;
          }
        }
        if (v.is_invalid() || amt.is_invalid() || !is_const_pin(amt)) {
          return false;
        }
        auto av = hydrate_const(amt);
        if (!av.is_just_i64() || av.to_just_i64() < 0) {
          return false;
        }
        auto dp0 = node.get_driver_pin(0);
        if (dp0.is_invalid()) {
          return false;
        }
        val = peel_ident(v);
        lo  = av.to_just_i64();
        len = wbits_of(dp0);
      } else {
        return false;
      }
      if (val.is_invalid() || is_const_pin(val) || livehd::graph_util::is_graph_input_pin(val) || len <= 0) {
        return false;
      }
      if (type_op_of(val.get_master_node()) != Ntype_op::Sub || !group_sched.contains(val.get_master_node().get_class_index())) {
        return false;
      }
      // And/SRA are width-sensitive on a SIGNED source (high bits would be sign
      // copies, not stored bits): only bind a slice that lies fully inside the
      // pin. Get_mask keeps its historical extract semantics unguarded, and an
      // UNKNOWN width (wbits_of can be 0 on a not-yet-bound Sub pin) defers to
      // slice_operand's own per-slice range check rather than rejecting here.
      if (nop != Ntype_op::Get_mask) {
        const int vw = wbits_of(val);
        if (vw > 0 && lo + len > vw) {
          if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
            std::fprintf(stderr,
                         "[splitdbg] slice-read MISS: %s wants [%lld+%lld) but %s is only %d bits\n",
                         debug_name(node).c_str(),
                         static_cast<long long>(lo),
                         static_cast<long long>(len),
                         debug_name(val.get_master_node()).c_str(),
                         vw);
          }
          return false;
        }
      }
      auto se = slice_operand(val, static_cast<uint32_t>(lo), static_cast<uint32_t>(len), 0);
      if (se.empty()) {
        if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
          std::fprintf(stderr,
                       "[splitdbg] slice-read MISS: %s wants [%lld+%lld) of %s (no covering export)\n",
                       debug_name(node).c_str(),
                       static_cast<long long>(lo),
                       static_cast<long long>(len),
                       debug_name(val.get_master_node()).c_str());
        }
        return false;
      }
      auto dp = node.get_driver_pin(0);
      if (dp.is_invalid()) {
        return false;
      }
      if (!pin2var.contains(dp.get_class_index())) {
        const int wb  = wbits_of(dp);
        auto      var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
        fout->append(
            absl::StrCat("    Slop<", wb, "> ", var, " = (", se, ").zext_to<", wb, ">();  // slice read via group export\n"));
        pin2var[dp.get_class_index()] = var;
        canonical_.insert(dp.get_class_index());
      }
      return true;
    };

    auto emit_group_call = [&](auto&& /*ensure_fn (recursion uses the captured ensure_ready)*/,
                               const hhds::Node_class& node,
                               const hhds::Pin_class&  want) -> void {
      if (splits_ == nullptr) {
        return;
      }
      auto cg = node.get_subnode_graph();
      if (!cg) {
        return;
      }
      auto sit = splits_->find(cg.get());
      if (sit == splits_->end()) {
        return;
      }
      const auto want_pid = static_cast<uint32_t>(want.get_port_id());
      bool       any = false, sliced = false;
      for (size_t gi = 0; gi < sit->second.size(); ++gi) {
        if (!sit->second[gi].covers_out(want_pid)) {
          continue;
        }
        any = true;
        for (const auto& o : sit->second[gi].outs) {
          if (o.pid == want_pid && o.len != 0) {
            sliced = true;  // decided per PID: a merged group can hold other pids' slices
          }
        }
        emit_one_group(node, static_cast<int>(gi));
      }
      if (!any) {
        return;  // a prebound state-only output, or a genuine cycle -> loud diagnostic elsewhere
      }
      if (sliced && !pin2var.contains(want.get_class_index())) {
        // Assemble the whole pin from the published slices, in range order.
        auto        sio = node.get_subnode_io();
        std::string inst;
        for (const auto& s : subs) {
          if (s.node.get_class_index() == node.get_class_index()) {
            inst = s.inst;
            break;
          }
        }
        int wb = 1;
        if (sio) {
          for (const auto& d : sio->get_output_pin_decls()) {
            if (static_cast<uint32_t>(d.port_id) == want_pid && d.bits > 0) {
              wb = static_cast<int>(d.bits);
            }
          }
        }
        std::string acc      = absl::StrCat("Slop<", wb, ">::create_integer(0)");
        bool        complete = true;
        for (size_t gi = 0; gi < sit->second.size() && complete; ++gi) {
          const auto& sg = sit->second[gi];
          for (size_t oj = 0; oj < sg.outs.size(); ++oj) {
            const auto& o = sg.outs[oj];
            if (o.pid != want_pid) {
              continue;
            }
            if (o.len == 0) {
              if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
                std::fprintf(stderr, "[splitdbg]   assembly: pid %u group %zu has WHOLE out\n", want_pid, gi);
              }
              complete = false;  // mixed whole+slice for one pid: whole path owns it
              break;
            }
            const auto key = std::pair<hhds::Class_index, int>{node.get_class_index(), static_cast<int>(gi)};
            if (inflight_groups.contains(key) || !emitted_groups.contains(key)) {
              if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
                std::fprintf(stderr,
                             "[splitdbg]   assembly: pid %u group %zu inflight=%d emitted=%d\n",
                             want_pid,
                             gi,
                             inflight_groups.contains(key) ? 1 : 0,
                             emitted_groups.contains(key) ? 1 : 0);
              }
              complete = false;  // a covering group could not run: leave unbound (loud path)
              break;
            }
            acc = absl::
                StrCat("Slop<", wb, ">::or_op(", acc, ", Slop<", wb, ">::shl_op(", inst, ".__ps_g", gi, "_", oj, ", ", o.lo, "))");
          }
        }
        if (complete && !inst.empty()) {
          auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
          fout->append(absl::StrCat("    Slop<", wb, "> ", var, " = ", acc, ";  // assembled from slice exports\n"));
          pin2var[want.get_class_index()] = var;
        } else if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
          std::fprintf(stderr,
                       "[splitdbg] ASSEMBLY FAILED: %s out pid %u (complete=%d inst='%s')\n",
                       gname.c_str(),
                       want_pid,
                       complete ? 1 : 0,
                       inst.c_str());
        }
      }
    };

    // Bind one combinational node's value. SINGLE-FANOUT nodes bind as a
    // parenthesized EXPRESSION instead of a named temp — the consumer inlines
    // the whole tree, the intermediate Slop is never materialized, and the
    // conversions that existed only to LAND the value in a typed variable go
    // with it (the host compiler then keeps the tree in registers). Everything
    // pasted is pure, every referenced name is an SSA temp / member / __in
    // field that is never reassigned within the pass, and pin2var is
    // pass-local, so an expression string means the same thing at any later
    // paste point — the same reasoning that lets `inst.__out.f` bindings work.
    // The one class of name that DOES get rewritten mid-emission is a
    // latency-1 memory read register (see seq_volatile_), so a cone reading one
    // materializes instead; that also ends the taint for its own consumers,
    // which is why testing the DIRECT drivers is enough.
    //
    // A node with fan-out > 1 (or a huge tree, or under LIVEHD_SIM_NOINLINE for
    // debuggability) still materializes, with `= expr` so the hlop cross-width
    // ctor's EXPLICIT keyword keeps its job: a node_expr that does not land at
    // the declared width fails the build instead of silently truncating and
    // sign-extending. `sub_width_expr_` opts a declaration into brace-init for
    // the one arm that deliberately returns a narrower expression (the Get_mask
    // raw pass-through), where the value is an unsigned canonical whose top
    // stored bit is clear, so the ctor's sign-extend equals the zext.
    const bool forest_ok = ::getenv("LIVEHD_SIM_NOINLINE") == nullptr;
    auto       bind_comb = [&](const hhds::Node_class& n, const hhds::Pin_class& dp, const char* tag) {
      sub_width_expr_ = false;
      const int   wb  = wbits_of(dp);
      std::string ex  = node_expr(n, wb);

      bool frozen = false;  // reads a name the sequential section rewrites
      for (const auto& ie : n.inp_edges()) {
        if (!ie.driver.is_invalid() && seq_volatile_.contains(ie.driver.get_class_index())) {
          frozen = true;
          break;
        }
      }
      if (forest_ok && !frozen && ex.size() <= 4000) {
        int nout = 0;
        for (const auto& oe : n.out_edges()) {
          (void)oe;
          if (++nout > 1) {
            break;
          }
        }
        if (nout == 1) {
          pin2var[dp.get_class_index()] = absl::StrCat("(", ex, ")");
          canonical_.insert(dp.get_class_index());
          return;
        }
      }
      auto        var  = absl::StrCat("cg_", std::to_string(tmp_cnt++));
      const char* open = sub_width_expr_ ? "{" : " = ";
      const char* shut = sub_width_expr_ ? "};" : ";";
      fout->append(absl::StrCat("    Slop<", wb, "> ", var, open, ex, shut, "  // ", op_name(type_op_of(n)), tag, "\n"));
      pin2var[dp.get_class_index()] = var;
      canonical_.insert(dp.get_class_index());  // node_expr result: canonical at its declared width
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
        if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr && !demand_stack.empty()) {
          std::string chain;
          for (const auto& d2 : demand_stack) {
            chain += d2 + " -> ";
          }
          std::fprintf(stderr,
                       "[splitdbg] RE-ENTRY: %s%s (the cycle-closing demand)\n",
                       chain.c_str(),
                       debug_name(drv.get_master_node()).c_str());
        }
        return;
      }
      auto n   = drv.get_master_node();
      auto nop = type_op_of(n);
      if (::getenv("LIVEHD_SIM_SPLIT_DEBUG2") != nullptr && !demand_stack.empty()) {
        std::fprintf(stderr, "[splitdbg]   walk %s (%s)\n", debug_name(n).c_str(), op_name(nop));
      }
      if ((nop == Ntype_op::Get_mask || nop == Ntype_op::And || nop == Ntype_op::SRA) && try_slice_read(n)) {
        return;  // bound from the slice export that computes exactly those bits
      }
      if (nop == Ntype_op::Sub) {
        // A GROUP-SCHEDULED instance answers a demanded output with exactly the
        // group that computes it; the atomic call stays deferred.
        if (group_sched.contains(n.get_class_index())) {
          emit_group_call([&](const hhds::Pin_class& p) { self(self, p); }, n, drv);
          return;
        }
        // A deferred-Moore instance's outputs are pre-bound (peek) -- nothing to
        // emit. Any other atomic call is emitted on demand, its own input cones
        // first (prefetch_seen above already broke re-entry on a real cycle).
        if (!moore_deferred.contains(n.get_class_index())) {
          emit_sub_call([&](const hhds::Pin_class& p) { self(self, p); }, n);
        }
        return;
      }
      if (nop == Ntype_op::Memory) {
        // Emit the memory on demand, like an atomic Sub call above. If this is a
        // RE-ENTRY during the memory's own emission (dout -> write cone -> dout,
        // a genuine cycle) the guard returns without binding and the caller's
        // operand() raises the loud diagnostic.
        emit_memory([&](const hhds::Pin_class& p) { self(self, p); }, n);
        return;
      }
      if (is_type_register(n)) {
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
      bind_comb(n, dp, " (mem-operand prefetch)");
    };
    auto ensure_ready = [&](const hhds::Pin_class& drv) { ensure_ready_impl(ensure_ready_impl, drv); };
    ensure_ready_fn   = ensure_ready;

    // combinational SSA bindings, in dependency order
    for (auto node : g->body().nodes(hhds::Node_order::forward)) {
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
        emit_memory(ensure_ready, node);
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
        if (group_sched.contains(node.get_class_index())) {
          // outputs bind on demand through `__settle_g<k>` calls; the single
          // atomic call — advance in the rise, refresh in fall/settle — is
          // emitted after the walk, when every input operand is bound.
          for (const auto& s : subs) {
            if (s.node.get_class_index() == node.get_class_index()) {
              deferred_group.push_back(&s);
              break;
            }
          }
          continue;
        }
        emit_sub_call(ensure_ready, node);
        continue;
      }
      if (op == Ntype_op::Clock_cell) {
        // Timing-only. Local state folds this cell into its commit guard, and a
        // child clock port receives the root clock plus `<port>__tick` above.
        // Emitting the cell as ordinary combinational data would either fatal
        // in node_expr() or, worse, discard its enable.
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
      if ((op == Ntype_op::Get_mask || op == Ntype_op::And || op == Ntype_op::SRA) && try_slice_read(node)) {
        continue;  // a field read of a group-scheduled bundle: bound from its slice export
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
      bind_comb(node, dpin, "");
    }

    // Deferred Moore-sub calls: every comb value is bound now; the child call
    // only advances the child's state (its outputs were pre-bound from __out).
    // Inputs are bound from the instance's EXISTING sink edges (an unconnected
    // declared input has no created pin -- hhds asserts on a name lookup -- and
    // the In{} zero-init already models it as 0).
    //
    // Nothing to do in the settle pass: it never advances a child, and the
    // pre-binding above already pointed these outputs straight at `<inst>.__out`.
    // GROUP-SCHEDULED instances: the single atomic call, now that every input
    // operand is bound. In the RISE this is the state advance (`cycle(in)`,
    // returning pre-edge outputs, value-equal to the group snapshots); in FALL /
    // SETTLE it is a full `__settle(in)` so `__out` leaves the pass COHERENT —
    // group calls refreshed only their own fields, and the next period's
    // prebinds (and any bound sigref) read the whole struct. emit_sub_call
    // carries the gated-clock `__tick` writes with it.
    for (const auto* sp : deferred_group) {
      // The advance follows the child's edge: a negedge-only child samples at
      // the parent's FALL (its ring inputs are rebuilt post-rise, and the walk
      // above answered them from fresh __settle_g<k> exports); every other
      // child advances at the RISE as before. All remaining passes refresh with
      // a full __settle so `__out` leaves the pass coherent.
      const bool advance = sp->negedge_only ? (pass_ == Pass::Fall) : (pass_ == Pass::Rise);
      const bool save    = settle_mode;
      settle_mode        = !advance;
      emit_sub_call(ensure_ready, sp->node);
      settle_mode = save;
    }

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
      const std::string run_condition = sub_run_condition(ensure_ready, s);
      if (!run_condition.empty()) {
        // Bind the input cones BEFORE the guard opens — same reason as
        // emit_sub_call: a `cg_N` declared inside the block is out of scope at
        // the later consumers pin2var still names it for. Same pids, same
        // dedupe as the fill loop below, so nothing extra is demanded.
        absl::flat_hash_set<uint32_t> pre_seen;
        for (auto e : s.node.inp_edges()) {
          auto pid = static_cast<uint32_t>(e.sink.get_port_id());
          auto it  = pid2in.find(pid);
          if (it == pid2in.end() || !pre_seen.insert(pid).second) {
            continue;
          }
          ensure_ready(child_clock_data_driver(s, pid, e.driver));
          emit_child_tick(ensure_ready, s, it->second.first, pid, e.driver, /*bind_only=*/true);
        }
        fout->append(absl::StrCat("    if (", run_condition, ") {  // conditional activation (reset keeps it open)\n"));
      }
      for (auto e : s.node.inp_edges()) {
        auto pid = static_cast<uint32_t>(e.sink.get_port_id());
        auto it  = pid2in.find(pid);
        if (it == pid2in.end() || !bound_pids.emplace(pid, nullptr).second) {
          continue;
        }
        // cpp_port_path for the same reason as the __pre read above: In mirrors a
        // tuple port as a nested struct, so the leaf is `io_data.instruction`.
        auto value_drv = child_clock_data_driver(s, pid, e.driver);
        ensure_ready(value_drv);
        fout->append(absl::StrCat("    ",
                                  s.inst,
                                  ".__gen += slop_update(",
                                  s.inst,
                                  ".__in.",
                                  cpp_port_path(it->second.first),
                                  ", ",
                                  operand(value_drv, it->second.second),
                                  ");\n"));
        emit_child_tick(ensure_ready, s, it->second.first, pid, e.driver);
      }
      fout->append(absl::StrCat("    ", s.inst, ".cycle();  // deferred Moore-sub state advance\n"));
      if (!run_condition.empty()) {
        fout->append("    }\n");
      }
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
      if (f.gate_fall && pass_ == Pass::Rise) {
        return;  // fall-gated: its guards are sampled POST-rise, in the fall pass
      }
      // The guard and secondary-clock cones are NOT in flop_operand_ports, so no
      // caller readies them on the flop's behalf. Doing it here covers every
      // path into `_cen` — an unready cone would otherwise reach operand() as
      // `0 /*UNRESOLVED-CYCLE*/` and the flop would silently never load.
      for (const auto& gp : f.clock_guards) {
        ensure_ready_fn(gp);
      }
      ensure_ready_fn(f.sec_clock);
      std::string cen;
      for (const auto& gp : f.clock_guards) {
        absl::StrAppend(&cen, cen.empty() ? "" : " && ", "(", operand(gp, 1), ").is_known_true()");
      }
      if (!f.sec_clock.is_invalid()) {
        const std::string cur = absl::StrCat("(", operand(f.sec_clock, 1), ").is_known_true()");
        absl::StrAppend(
            &cen,
            cen.empty() ? "" : " && ",
            f.posedge ? absl::StrCat("(", cur, " && !", f.prev_member, ")") : absl::StrCat("(!", cur, " && ", f.prev_member, ")"));
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
      const std::string rstval
          = initp.is_invalid() ? absl::StrCat("Slop<", f.bits, ">::create_integer(0)") : operand(initp, f.bits);
      std::string rtest;  // C++ bool: reset asserted (empty = no reset)
      bool        reset_always = false;
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
      auto        next_name  = [&](const std::string& base) { return absl::StrCat(base, latch_low ? "_low" : "_din"); };
      auto        hold_name  = [&](const std::string& base) { return latch_high ? absl::StrCat(base, "_low") : base; };
      const char* decl       = latch_low ? "auto " : "";
      // LAZY GUARDED NEXT-STATE: a gated flop whose commit will not fire this
      // period does not need its `_din` computed at all — and with single-use
      // forestation the din EXPRESSION carries the flop's whole exclusive comb
      // chain, so a closed gate skips that entire cone. The guard wraps the
      // COMPUTE-phase assignment only; the two-phase structure stays (every din
      // still reads PRE-commit neighbor state — sinking the expression into the
      // commit itself would read post-commit values). `_cen` therefore emits
      // BEFORE the din block here instead of after. Latch windows keep their
      // unconditional evaluation (transparency semantics), and VCD builds keep
      // the eager form (an X-window dump reads dins the gate would skip).
      const bool  lazy_guard = !f.is_latch && !latch_low && !latch_high && vcd_file.empty()
                               && ::getenv("LIVEHD_SIM_NOLAZY") == nullptr
                               && (!f.clock_guards.empty() || !f.sec_clock.is_invalid() || !f.tick_field.empty());
      std::string gcond;
      if (lazy_guard) {
        if (with_cen) {
          emit_commit_enable(f);  // readies its own guard/secondary-clock cones
        }
        if (!f.clock_guards.empty() || !f.sec_clock.is_invalid()) {
          gcond = absl::StrCat(f.member, "_cen");
        }
        if (!f.tick_field.empty()) {
          absl::StrAppend(&gcond, gcond.empty() ? "" : " && ", "__in.", f.tick_field, "__tick");
        }
        fout->append("    if (", gcond, ") {  // gated: skip the whole next-state cone when the edge cannot fire\n");
      }
      const std::string din_expr = din.is_invalid() ? f.member : operand(din, f.bits);
      if (f.depth <= 1) {
        fout->append("    ", decl, next_name(f.member), " = ", next_of(din_expr, hold_name(f.member)), ";\n");
      } else {
        fout->append("    ", decl, next_name(f.stages[0]), " = ", next_of(din_expr, hold_name(f.stages[0])), ";\n");
        for (size_t i = 1; i < f.stages.size(); ++i) {
          fout->append("    ", decl, next_name(f.stages[i]), " = ", next_of(f.stages[i - 1], hold_name(f.stages[i])), ";\n");
        }
        fout->append("    ", decl, next_name(f.member), " = ", next_of(f.stages.back(), hold_name(f.member)), ";\n");
      }
      if (lazy_guard) {
        fout->append("    }\n");
      } else if (with_cen && !latch_low) {
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
      const bool        cen     = !f.clock_guards.empty() || !f.sec_clock.is_invalid();
      const bool        guarded = cen || !f.tick_field.empty();
      const std::string ind     = guarded ? "      " : "    ";
      if (guarded) {
        std::string cond;
        if (cen) {
          cond = absl::StrCat(f.member, "_cen");
        }
        if (!f.tick_field.empty()) {
          // The clock arrived on a PORT, and the parent says whether it ticked.
          absl::StrAppend(&cond, cond.empty() ? "" : " && ", "__in.", f.tick_field, "__tick");
        }
        fout->append("    if (", cond, ") {  // commit only when this element's clock edge actually fired\n");
      }
      // Compare-on-write commits: __gen advances only when the stored value
      // actually changed — a quiesced pipeline (same state, same din) stops
      // bumping and the change-gated settles above it go idle.
      for (const auto& s : f.stages) {
        fout->append(ind, "__gen += slop_update(", s, ", ", s, "_din);\n");
      }
      fout->append(ind,
                   "__gen += slop_update(",
                   f.member,
                   ", ",
                   f.member,
                   f.posedge ? "_din);\n" : "_din);  // negedge: commits at the FALL\n");
      if (guarded) {
        fout->append("    }\n");
      }
    };

    // NOTE the visited set: this walk recurses through inp_edges, and without
    // dedup a reconvergent cone re-walks every diamond on every path — measured
    // EXPONENTIAL on minion's intpipe_top (the emission sat for 25+ minutes
    // inside this lambda; every ICG enable latch's window invalidates a cone
    // full of shared CSR decode logic). Erasing a binding twice is idempotent,
    // so visiting each node once is exactly equivalent and linear.
    absl::flat_hash_set<hhds::Class_index> invalidate_seen;
    auto                                   invalidate_upstream_impl = [&](auto&& self, const hhds::Pin_class& p) -> void {
      if (p.is_invalid() || is_const_pin(p) || livehd::graph_util::is_graph_input_pin(p)) {
        return;
      }
      auto n = p.get_master_node();
      if (!invalidate_seen.insert(n.get_class_index()).second) {
        return;
      }
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
    auto invalidate_upstream
        = [&](auto&& /*self: kept for the callers' recursive-call signature*/, const hhds::Pin_class& p) -> void {
      invalidate_seen.clear();  // per-invocation: each window's invalidation is its own wave
      invalidate_upstream_impl(invalidate_upstream_impl, p);
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
      // A latch Q read DURING THE HIGH WINDOW is that latch's post-rise value —
      // its staged `_din`, already emitted by this same loop for every latch
      // EARLIER in flop order — not the pre-rise member. The member binding
      // made a gated enable chain (`if (clk and e1l) { e2l = e2 }`, the ICG
      // enable-latch cascade) sample with the PREVIOUS period's gate value
      // (tests/sim/chained_clock_gates_mixed_phase.prp). A latch LATER in flop
      // order keeps the member binding, exactly as before.
      for (const auto& f2 : flops) {
        if (&f2 == &f) {
          break;
        }
        if (!f2.is_latch) {
          continue;
        }
        auto q2 = f2.node.get_driver_pin(0);
        if (!q2.is_invalid()) {
          pin2var[q2.get_class_index()] = absl::StrCat(f2.member, "_din");
        }
      }
      for (const auto* port : flop_operand_ports) {
        ensure_ready(get_driver(find_sink_pin(f.node, port)));
      }
      emit_flop_next(f, false, true);
    }
    if (pass_ == Pass::Rise && !ref_clock_pin.is_invalid()) {
      pin2var[ref_clock_pin.get_class_index()] = absl::StrCat("__in.", cpp_port_path(ref_clock_name));
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
        // the end of this pass. (emit_commit_enable readies the guard and
        // secondary-clock cones itself.)
        emit_commit_enable(f);
      }
    }

    // Outputs from the state this pass reads. In the CYCLE pass that is the
    // PRE-edge state, and the result is the returned `o`/`__last_out` (what the
    // output drove during the period). In the SETTLE pass it is the committed
    // state, and the result lands directly in the `__out` member a `sigref` binds.
    // (The FALL pass drives no outputs: `__last_out` is the during-period value
    // the rise recorded, and `__out` is refreshed by the trailing settle.)
    // `__o` (not `o`): every other identifier this emitter mints is `__`-prefixed
    // precisely so a source-derived struct member can never shadow it. A bare `o`
    // broke that — a sub instance named `o` (the LHS variable of `mut o =
    // packer(…)`) put a local `Out o` on top of the member, and every `o.<...>`
    // in the pass after it resolved to the output struct.
    const std::string out_dst = settle ? "    __out." : "    __o.";
    if (!fall) {
      if (!settle) {
        fout->append("    Out __o;\n");
      }
      for (const auto& io : ios) {
        if (io.is_input) {
          continue;
        }
        if (active_group != nullptr) {
          auto it            = out_field2pid.find(io.field);
          bool whole_covered = false;
          if (it != out_field2pid.end()) {
            for (const auto& o : active_group->outs) {
              if (o.pid == it->second && o.len == 0) {
                whole_covered = true;
                break;
              }
            }
          }
          if (!whole_covered) {
            continue;  // another group's output, or a SLICE (exports below write it)
          }
        }
        auto spin = g->get_output_pin(io.raw);
        auto drv  = spin.is_invalid() ? hhds::Pin_class{} : get_driver(spin);
        if (drv.is_invalid()) {
          fout->append("    // output ", io.field, " is undriven\n");
        } else {
          // ensure_ready FIRST. This loop runs AFTER the latch windows above have
          // called `invalidate_downstream(qpin)`, which erases the pin2var binding
          // of every comb node transitively downstream of a latch Q so the
          // post-window value gets re-emitted. A module OUTPUT driven by such a
          // node — `assign clk_o = clk_i & en_latch` in minion's own prim_clk_gate
          // is the canonical one — then read an erased binding, and `operand()`
          // reported that as `negedge-operand-unresolved: a combinational-cycle
          // back-edge`. It is neither: there is no cycle and, for prim_clk_gate,
          // no negedge state at all (the module has no fall pass). Every other
          // operand() site after the invalidation already re-readies.
          ensure_ready(drv);
          fout->append(out_dst, io.field, " = ", operand(drv, io.bits), ";\n");
        }
      }
    }
    // SLICE EXPORTS: a bit-range group publishes each range's value into its
    // generated `__ps_g<k>_<j>` member — the parent assembles whole-pin reads
    // from these and slices partial In-fills out of them. `__out` stays owned
    // by the full settle, so no partial read-modify-write of a field ever
    // happens.
    if (active_group != nullptr) {
      for (size_t j = 0; j < active_group->outs.size(); ++j) {
        const auto& o = active_group->outs[j];
        if (o.len == 0 || o.leaf.is_invalid()) {
          continue;
        }
        ensure_ready(o.leaf);
        std::string expr;
        if (o.shifted) {
          expr = absl::StrCat("Slop<", o.len, ">::sra_op(", operand(o.leaf, static_cast<int>(o.lo + o.len)), ", ", o.lo, ")");
        } else {
          expr = absl::StrCat("(", operand(o.leaf, static_cast<int>(o.len)), ")");
        }
        fout->append(absl::StrCat("    __ps_g",
                                  active_group_idx,
                                  "_",
                                  j,
                                  " = ",
                                  expr,
                                  ";  // slice [",
                                  o.lo + o.len - 1,
                                  ":",
                                  o.lo,
                                  "] of out pid ",
                                  o.pid,
                                  "\n"));
      }
    }

    // The settle pass ends here: everything below is state advance (VCD sample,
    // memory commit, flop commit, the fall phase) and must happen exactly once
    // per period.
    if (settle) {
      if (!gate_done.empty()) {
        // Same store-the-entry-pair epilogue as the far tail (settle bodies —
        // full and per-group — close HERE, which is why the settle gate was
        // dead code until this store existed), including the transitive bump.
        fout->append("    if ((", gate_ksum, ") != __k0) ++__gen;\n");
        fout->append("    ", gate_done, " = __g0;\n");
        fout->append("    ", gate_kids, " = __k0;\n");
      }
      fout->append("}\n");
      close_body();
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
        // LAZY WRITE STAGING: evaluate the (cheap, shared) write-enable first; a
        // known-false enable skips the address and data cones entirely — with
        // forestation those are whole inlined expressions, so a gated / idle write
        // port costs one boolean test.
        //
        // The skip is NOT free on its own: `stage_write` opens with `p.clear()`,
        // which is what CANCELS a write this port already staged eagerly from the
        // combinational section (stage_through, for a latency-0 read's program-order
        // resolution). Skipping the call would leave that entry live and `tick()`
        // would commit it. `clear_pending()` up front restores the invariant the
        // eager form got for free — every visited port's pending state is
        // re-established from THIS cycle's enable — so the skip is once again
        // behavior-identical. The ports the loop skips below (reads, invalid
        // addr/din, const-false enable) are exactly the ones stage_through skips
        // too, so nothing that was meant to survive is dropped.
        const bool lazy_wr = vcd_file.empty() && ::getenv("LIVEHD_SIM_NOLAZY") == nullptr;
        bool       cleared = false;
        for (const auto& p : m.ports) {
          if (p.rd || p.addr.is_invalid() || p.din.is_invalid()) {
            continue;
          }
          if (!p.enable.is_invalid() && is_const_pin(p.enable) && hydrate_const(p.enable).is_known_false()) {
            continue;
          }
          if (lazy_wr) {
            if (!cleared) {
              fout->append(absl::StrCat("    ", m.member, ".clear_pending();  // a skipped stage_write must still cancel\n"));
              cleared = true;
            }
            fout->append(absl::StrCat("    { auto _w = ",
                                      emit_wen(m, p),
                                      "; if (!_w.is_known_false()) ",
                                      m.member,
                                      ".stage_write<",
                                      p.wridx,
                                      ">(_w, ",
                                      operand(p.addr, std::max(1, bits_of(p.addr))),
                                      ", ",
                                      operand(p.din, m.bits),
                                      "); }\n"));
          } else {
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
        }
        for (const auto& p : m.ports) {
          if (p.rd && m.type == 1 && !p.addr.is_invalid()) {
            const int         ab   = std::max(1, bits_of(p.addr));
            // A latency-1 read is a REGISTER on the array's clock, so it takes the
            // same gate the writes take. Latching it unconditionally made the read
            // data follow the address through periods the gate holds — the silent
            // half of the memory clock-gate fold.
            const std::string cond = mem_gate_cond(m);
            fout->append(absl::StrCat("    ",
                                      cond.empty() ? "" : absl::StrCat("if (", cond, ") "),
                                      "__gen += slop_update(",
                                      m.member,
                                      "_q",
                                      p.rdidx,
                                      ", ",
                                      m.member,
                                      ".read<",
                                      p.rdidx,
                                      ">(",
                                      operand(p.addr, ab),
                                      "));  // sync read: the RESOLVED value\n"));
          }
        }
        std::string whole_close;
        if (m.is_whole()) {
          const int W = m.bits * m.size;
          if (!m.reset.is_invalid()) {
            const std::string initbus = m.init.is_invalid() ? absl::StrCat("Slop<", W, ">::create_integer(0)") : operand(m.init, W);
            fout->append(absl::StrCat("    if ((",
                                      operand(m.reset, 1),
                                      ").is_known_true()) { __gen += ",
                                      m.member,
                                      ".apply_update(",
                                      initbus,
                                      "); ",
                                      m.member,
                                      ".clear_pending(); } else {\n"));
            whole_close = "    }\n";
          }
          // The WHOLE-ARRAY update bus takes the same gated-clock guard the
          // per-port writes get through emit_wen: without it a clock-gated whole
          // array re-applies its update every tick with the gate as dead code,
          // which is the same silent miscompile one level up.
          std::string ucond;
          if (!m.update_enable.is_invalid()) {
            absl::StrAppend(&ucond, "(", operand(m.update_enable, 1), ").is_known_true()");
          }
          if (const std::string gc = mem_gate_cond(m); !gc.empty()) {
            absl::StrAppend(&ucond, ucond.empty() ? "" : " && ", gc);
          }
          const std::string ue = ucond.empty() ? "" : absl::StrCat("if (", ucond, ") ");
          fout->append(absl::StrCat("    ", ue, "__gen += ", m.member, ".apply_update(", operand(m.update, W), ");\n"));
        }
        fout->append(whole_close);
        fout->append(absl::StrCat("    __gen += ", m.member, ".tick();\n"));
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
        const std::string run_condition = sub_run_condition(ensure_ready, s);
        if (!run_condition.empty()) {
          // Bind the input cones BEFORE the guard opens — same reason as
          // emit_sub_call: a `cg_N` declared inside the block is out of scope
          // at the later consumers pin2var still names it for.
          for (const auto& d : sio->get_input_pin_decls()) {
            auto pre_drv = sub_input_driver(s, d.name, d.port_id);
            ensure_ready(child_clock_data_driver(s, static_cast<uint32_t>(d.port_id), pre_drv));
            emit_child_tick(ensure_ready, s, d.name, static_cast<uint32_t>(d.port_id), pre_drv, /*bind_only=*/true);
          }
          fout->append(absl::StrCat("    if (", run_condition, ") {  // conditional activation (reset keeps it open)\n"));
        }
        // Ports are members: rebuild the child's __in from the just-committed
        // parent state, then advance it (no local In, no by-value copy).
        for (const auto& d : sio->get_input_pin_decls()) {
          auto drv       = sub_input_driver(s, d.name, d.port_id);
          int  wb        = d.bits > 0 ? static_cast<int>(d.bits) : 1;
          auto value_drv = child_clock_data_driver(s, static_cast<uint32_t>(d.port_id), drv);
          ensure_ready(value_drv);
          fout->append(absl::StrCat("    ",
                                    s.inst,
                                    ".__gen += slop_update(",
                                    s.inst,
                                    ".__in.",
                                    cpp_port_path(d.name),
                                    ", ",
                                    operand(value_drv, wb),
                                    ");\n"));
          emit_child_tick(ensure_ready, s, d.name, static_cast<uint32_t>(d.port_id), drv);
        }
        fout->append(absl::StrCat("    ", s.inst, ".cycle();\n"));
        if (!run_condition.empty()) {
          fout->append("    }\n");
        }
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
        // secondary-clock level both phases must observe. EXCEPTION: a
        // fall-GATED flop (gate_fall) samples its ICG guards at the fall — the
        // guard latches were transparent during the high phase, so only the
        // post-rise values are the ones its silicon enable latch pinned; its
        // `_cen` is therefore computed HERE, from the re-readied cones above.
        if (f.gate_fall) {
          emit_commit_enable(f);
        }
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
      fout->append("    __last_out = __o;  // 2f-sim B: free output observation for the query engine\n");
    }
    if (!gate_done.empty()) {
      // TRANSITIVE change propagation: my direct-kids sum only sees children's
      // OWN __gen, and a stateless wrapper never bumps its own — so a
      // grandchild's still-moving pipeline would be invisible to MY parent and
      // it would gate this whole subtree off mid-flight (minion_top skipped
      // while core_top's grandchildren still retired instructions). If the
      // subtree advanced during this run, reflect it in MY __gen; the wave
      // bubbles one level per run and dies out one run after the deepest
      // mover quiesces.
      fout->append("    if ((", gate_ksum, ") != __k0) ++__gen;\n");
      fout->append("    ", gate_done, " = __g0;\n");
      fout->append("    ", gate_kids, " = __k0;\n");
    }
    fout->append("}\n");
    close_body();
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

  // The ordinary trailing settle above has rebuilt every nested input from the
  // parent's committed post-rise state. Refresh only mixed-edge descendants:
  // their normal atomic cycle sampled the negedge before those inputs existed.
  // Do not re-settle this module afterward; its externally observed output must
  // retain the established during-period phase, while the corrected nested
  // state is ready for the next rise.
  if (has_refresh) {
    fout->append("void ", mod, "::refresh_negedge() {\n");
    for (const auto& s : subs) {
      if (s.refresh_negedge) {
        fout->append("    ", s.inst, ".eval_negedge();\n");
        fout->append("    ", s.inst, ".__settle();\n");
      }
      if (s.child_refresh_negedge) {
        fout->append("    ", s.inst, ".refresh_negedge();\n");
        fout->append("    ", s.inst, ".__settle();\n");
      }
    }
    fout->append("}\n");
  }

  // The per-output-group settle methods (see Split_group in the header): one
  // restricted settle body per distinct comb input-support, so a parent can
  // evaluate exactly the cone a demanded output needs while the rest of this
  // instance's inputs are still unresolved. Emitted only for definitions some
  // parent instantiates on a word-level cycle.
  if (splits_ != nullptr) {
    if (auto sit = splits_->find(g); sit != splits_->end()) {
      for (size_t gi = 0; gi < sit->second.size(); ++gi) {
        active_group     = &sit->second[gi];
        active_group_idx = static_cast<int>(gi);
        build_settle_cone(active_group);
        const bool ok    = emit_period_body(Pass::Settle);
        active_group     = nullptr;
        active_group_idx = -1;
        if (!ok) {
          return;
        }
      }
      build_settle_cone(nullptr);
    }
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
  fout->append("  ++__gen;  // loaded state: every gated evaluation must recompute\n");
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
    for ([[maybe_unused]] auto node : g->body().nodes()) {
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
        fout->append(absl::StrCat("  _m[_p + \"",
                                  m.member,
                                  "_q",
                                  p.rdidx,
                                  "\"] = ",
                                  m.member,
                                  "_q",
                                  p.rdidx,
                                  ".to_hex(",
                                  hexdigits(m.bits),
                                  ");\n"));
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
      fout->append(absl::StrCat("  _m[_p + \"", io.field, "\"] = __last_out.", io.field, ".to_hex(", hexdigits(io.bits), ");\n"));
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
    if (s.loop) {
      fout->append(absl::StrCat("  if (_n.compare(0, ", s.inst.size() + 1, ", \"", s.inst, "[\") == 0) {\n"));
      fout->append(absl::StrCat("    return ", s.inst, ".observe_mem(_n.substr(", s.inst.size(), "), _i, _o);\n  }\n"));
    } else {
      fout->append(absl::StrCat("  if (_n.compare(0, ", s.inst.size() + 1, ", \"", s.inst, ".\") == 0) {\n"));
      fout->append(absl::StrCat("    return ", s.inst, ".observe_mem(_n.substr(", s.inst.size() + 1, "), _i, _o);\n  }\n"));
    }
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
    auto        jout     = std::make_shared<File_output>(absl::StrCat(base, ".iface.json"));
    // Internal-net width -> source-declared width (unsigned drops the sign slot).
    const auto  decl_reg = [](int b, bool uns) { return uns ? std::max(1, b - 1) : b; };
    std::string j;
    absl::StrAppend(&j,
                    "{\"schema_version\":1,\"kind\":\"sim_iface\",\"gen\":\"",
                    kSimGenVersion,
                    "\",\"module\":\"",
                    mod,
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
      absl::StrAppend(&j,
                      first ? "" : ",\n      ",
                      "{\"name\":\"",
                      io.field,
                      "\",\"dir\":\"",
                      io.is_input ? "input" : "output",
                      "\",\"bits\":",
                      io.bits,
                      ",\"declared_bits\":",
                      io.bits,
                      ",\"signed\":",
                      uns ? "false" : "true",
                      io.is_input && io.raw == "__valid" ? ",\"role\":\"activation\",\"default\":true" : "",
                      "}");
      first = false;
    }
    absl::StrAppend(&j, "],\n");

    // State: pipe stages precede their flop, matching describe_signals() order.
    absl::StrAppend(&j, " \"regs\":[");
    first = true;
    for (const auto& f : flops) {
      const bool uns = is_unsign(f.node.get_driver_pin(0));
      for (const auto& s : f.stages) {
        absl::StrAppend(&j,
                        first ? "" : ",\n        ",
                        "{\"name\":\"",
                        s,
                        "\",\"kind\":\"pipe\",\"bits\":",
                        f.bits,
                        ",\"declared_bits\":",
                        decl_reg(f.bits, uns),
                        ",\"signed\":",
                        uns ? "false" : "true",
                        "}");
        first = false;
      }
      absl::StrAppend(&j,
                      first ? "" : ",\n        ",
                      "{\"name\":\"",
                      f.member,
                      "\",\"kind\":\"flop\",\"bits\":",
                      f.bits,
                      ",\"declared_bits\":",
                      decl_reg(f.bits, uns),
                      ",\"signed\":",
                      uns ? "false" : "true",
                      ",\"latch\":",
                      f.is_latch ? "true" : "false",
                      "}");
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
      absl::StrAppend(&j,
                      first ? "" : ",\n        ",
                      "{\"name\":\"",
                      m.member,
                      "\",\"bits\":",
                      m.bits,
                      ",\"declared_bits\":",
                      m.bits,
                      ",\"signed\":",
                      uns ? "false" : "true",
                      ",\"size\":",
                      m.size,
                      ",\"ordering\":\"",
                      m.order == Mem::Order::fwd       ? "fwd"
                      : m.order == Mem::Order::none    ? "none"
                      : m.order == Mem::Order::program ? "program"
                                                       : "old",
                      "\",\"wensize\":",
                      m.wensize,
                      ",\"n_rd\":",
                      m.n_rd,
                      ",\"n_wr\":",
                      m.n_wr,
                      ",\"sync_read\":",
                      m.type == 1 ? "true" : "false",
                      ",\"rd_regs\":[",
                      rds,
                      "]}");
      first = false;
    }
    absl::StrAppend(&j, "],\n");

    absl::StrAppend(&j, " \"subs\":[");
    first = true;
    for (const auto& s : subs) {
      absl::StrAppend(&j,
                      first ? "" : ",\n        ",
                      "{\"inst\":\"",
                      s.inst,
                      "\",\"module\":\"",
                      s.callee_struct,
                      "\"",
                      // `loop` is what tells a consumer that this instance's state
                      // is published per lane (`<inst>[<i>].<sig>`). `count` alone
                      // cannot: a single-trip rolled loop and an ordinary Sub both
                      // read as 1, and they are named differently.
                      s.loop ? absl::StrCat(",\"loop\":true,\"count\":", s.loop->count) : std::string{},
                      "}");
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
