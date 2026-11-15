//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The tree aegraph's import table against lnast.tolg. The rule fuzz/prove tests
// check every rule on micro-LGraphs; what they cannot see is whether the tree
// maps an LNAST statement to the cells tolg builds for it. Two checks per mapped
// op:
//   value      `o = op(c1, c2)` over random small, negative and wide constants:
//              the constant the tree step leaves in `o`'s store (the import
//              folded through the Rewriter) equals tolg's graph for the same
//              statement evaluated with aegraph::eval_op (the engine's fold).
//   structure  `o = op(a, b)` over inputs: the e-node (op, bank per operand,
//              nested cells) equals the cells tolg wires, sink pid by sink pid.

#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "aegraph.hpp"
#include "gtest/gtest.h"
#include "hlop/dlop.hpp"
#include "lnast.hpp"
#include "node_util.hpp"
#include "upass_aeg.hpp"
#include "upass_tolg.hpp"

namespace {

namespace fs = std::filesystem;
namespace gu = livehd::graph_util;
using N      = Lnast_ntype;
using Ltype  = Lnast_ntype::Lnast_ntype_int;

std::string fresh_lib_dir() {
  static int  serial = 0;
  const char* tmp    = std::getenv("TEST_TMPDIR");
  auto        path   = fs::path(tmp ? tmp : fs::temp_directory_path().string()) / std::format("treeaeg_conf_{}", serial++);
  fs::remove_all(path);
  return path.string();
}

bool is_const_text(std::string_view s) {
  return !s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) != 0 || s[0] == '-' || s == "true" || s == "false");
}

// top -> stmts -> [op(%t, operands...), store(o, %t)] with ports `ins` (integer
// unless named b*: boolean) and the output `o`.
std::shared_ptr<Lnast> unit(std::string_view name, Ltype t, const std::vector<std::string>& ops, const std::vector<std::string>& ins) {
  auto ln    = std::make_shared<Lnast>(name);
  auto root  = ln->set_root(N::create_top());
  auto stmts = ln->add_child(root, N::create_stmts());
  for (const auto& i : ins) {
    Lnast_io_entry e;
    e.name = i;
    e.bits = 16;
    e.kind = i.starts_with("b") ? Io_kind::boolean : Io_kind::integer;
    ln->io_meta().inputs.push_back(e);
  }
  // A declared width (wider than any case value): an output with none takes its
  // driver's width as its port, so the step keeps that cone as written and
  // would fold nothing here. The graph side reads the driver, not the port.
  Lnast_io_entry o;
  o.name = "o";
  o.bits = 1024;
  o.kind = Io_kind::integer;
  ln->io_meta().outputs.push_back(o);
  auto s = ln->add_child(stmts, t);
  ln->add_child(s, Lnast_node::create_ref("%t"));
  for (const auto& v : ops) {
    ln->add_child(s, is_const_text(v) ? Lnast_node::create_const(v) : Lnast_node::create_ref(v));
  }
  auto st = ln->add_child(stmts, N::create_store());
  ln->add_child(st, Lnast_node::create_ref("o"));
  ln->add_child(st, Lnast_node::create_ref("%t"));
  return ln;
}

std::shared_ptr<hhds::Graph> lower(const std::shared_ptr<Lnast>& ln) {
  const auto               lib = fresh_lib_dir();
  uPass_tolg::Registry     reg{ln};
  uPass_tolg::detect_lg_collisions(reg);
  uPass_tolg::register_io(ln, lib, reg);
  return uPass_tolg::run(ln, lib, reg);
}

hhds::Pin_class output_driver(const std::shared_ptr<hhds::Graph>& g) {
  const auto drivers = g->get_output_pin("o").get_driver_pins();
  return drivers.size() == 1 ? drivers.front() : hhds::Pin_class{};
}

// Integers only: a Bool is the u1 it lowers to.
std::string norm(const Dlop& v) {
  if (v.is_bool()) {
    return v.is_known_true() ? "1" : "0";
  }
  return v.to_pyrope();
}

std::optional<Dlop> eval_pin(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return std::nullopt;
  }
  if (pin.is_const()) {
    return gu::const_of(pin);
  }
  const auto node = pin.get_master_node();
  const auto op   = gu::type_op_of(node);
  std::vector<livehd::aegraph::Eval_arg> args;
  for (const auto& sink : node.inp_sorted_pins()) {
    const auto drivers = sink.get_driver_pins();
    if (drivers.size() != 1) {
      return std::nullopt;
    }
    const auto v = eval_pin(drivers.front());
    if (!v) {
      return std::nullopt;
    }
    args.push_back({sink.get_port_id(), static_cast<uint32_t>(Ntype::sink_bank(op, sink.get_port_id())), *v});
  }
  return livehd::aegraph::eval_op(op, args);
}

// The same canonical spelling as upass::aeg::import_view.
std::string describe(const hhds::Pin_class& pin) {
  if (pin.is_const()) {
    return norm(gu::const_of(pin));
  }
  if (gu::is_graph_input_pin(pin)) {
    return std::string(gu::pin_name_of(pin));
  }
  const auto               node = pin.get_master_node();
  const auto               op   = gu::type_op_of(node);
  std::vector<std::string> parts;
  for (const auto& sink : node.inp_sorted_pins()) {
    for (const auto& d : sink.get_driver_pins()) {
      parts.push_back(std::format("{}:{}", Ntype::sink_bank(op, sink.get_port_id()), describe(d)));
    }
  }
  std::ranges::sort(parts);
  std::string out = std::format("{}(", Ntype::get_name(op));
  for (size_t i = 0; i < parts.size(); ++i) {
    out += (i == 0 ? "" : ",") + parts[i];
  }
  return out + ")";
}

struct Case {
  Ltype                    t;
  std::vector<std::string> ops;
};

std::string spell(int64_t v) { return Dlop::create_integer(v)->to_pyrope(); }

// Random constants: small, negative, and wide (above 64 bits).
std::string rand_value(std::mt19937_64& rng) {
  switch (rng() % 4) {
    case 0 : return spell(static_cast<int64_t>(rng() % 17) - 8);
    case 1 : return spell(static_cast<int64_t>(rng() % 100000));
    case 2 : return spell(-static_cast<int64_t>(rng() % 100000) - 1);
    default: {
      auto v = Dlop::create_integer(static_cast<int64_t>(rng() >> 1));
      v      = v->shl_op(*Dlop::create_integer(static_cast<int64_t>(rng() % 40)));
      return (rng() % 2 == 0 ? v : v->sub_op(*Dlop::create_integer(0))->mult_op(*Dlop::create_integer(-1)))->to_pyrope();
    }
  }
}

std::vector<Case> value_cases(std::mt19937_64& rng) {
  std::vector<Case> cases;
  for (int rep = 0; rep < 12; ++rep) {
    const auto a = rand_value(rng);
    const auto b = rand_value(rng);
    for (const auto t : {N::Lnast_ntype_plus,
                         N::Lnast_ntype_minus,
                         N::Lnast_ntype_mult,
                         N::Lnast_ntype_bit_and,
                         N::Lnast_ntype_bit_or,
                         N::Lnast_ntype_bit_xor,
                         N::Lnast_ntype_eq,
                         N::Lnast_ntype_ne,
                         N::Lnast_ntype_lt,
                         N::Lnast_ntype_le,
                         N::Lnast_ntype_gt,
                         N::Lnast_ntype_ge}) {
      cases.push_back({t, {a, b}});
    }
    if (b != "0") {
      cases.push_back({N::Lnast_ntype_div, {a, b}});
    }
    const auto amt = spell(static_cast<int64_t>(rng() % 20));
    cases.push_back({N::Lnast_ntype_shl, {a, amt}});
    cases.push_back({N::Lnast_ntype_sra, {a, amt}});
    cases.push_back({N::Lnast_ntype_bit_not, {a}});
    const auto bits = static_cast<int64_t>(rng() % 16) + 1;
    cases.push_back({N::Lnast_ntype_bit_not, {spell(static_cast<int64_t>(rng() % (uint64_t{1} << bits))), spell(bits)}});
    const auto lo = static_cast<int64_t>(rng() % 10);
    cases.push_back({N::Lnast_ntype_get_mask, {a, spell(lo), spell(lo + static_cast<int64_t>(rng() % 12) - 2)}});
    cases.push_back({N::Lnast_ntype_get_mask, {a, spell(lo)}});
    cases.push_back({N::Lnast_ntype_sext, {a, spell(static_cast<int64_t>(rng() % 12))}});
    cases.push_back({N::Lnast_ntype_red_or, {a}});
    const std::string ba = rng() % 2 == 0 ? "true" : "false";
    const std::string bb = rng() % 2 == 0 ? "true" : "false";
    cases.push_back({N::Lnast_ntype_log_and, {ba, bb}});
    cases.push_back({N::Lnast_ntype_log_or, {ba, bb}});
    cases.push_back({N::Lnast_ntype_log_not, {ba}});
  }
  return cases;
}

}  // namespace

TEST(TreeAegConformance, ConstantFoldsMatchTolg) {
  std::mt19937_64 rng(20261114);
  size_t          checked = 0;
  size_t          serial  = 0;
  for (const auto& c : value_cases(rng)) {
    const auto name = std::format("conf_v{}", serial++);
    // The tree side: the constant the step leaves in o's store.
    auto tree = unit(name, c.t, c.ops, {});
    (void)upass::aeg::rewrite_staging({*tree, tree->io_meta(), name});
    const auto store = tree->get_root().first_child().first_child().next_sibling();
    const auto rhs   = store.first_child().next_sibling();
    // The graph side.
    auto g = lower(unit(name, c.t, c.ops, {}));
    ASSERT_TRUE(g) << name;
    const auto graph_value = eval_pin(output_driver(g));
    ASSERT_TRUE(graph_value.has_value()) << N::to_sv(c.t) << " " << c.ops.front();
    if (!N::is_const(tree->get_type(rhs))) {
      // Only a value with unknown bits stays a name (a '?' is never copied).
      EXPECT_TRUE(graph_value->has_unknowns()) << N::to_sv(c.t) << "(" << c.ops.front() << ") left unfolded";
      continue;
    }
    const auto tree_value = Dlop::from_pyrope(tree->get_name(rhs));
    std::string args;
    for (const auto& o : c.ops) {
      args += o + " ";
    }
    EXPECT_EQ(norm(*tree_value), norm(*graph_value)) << N::to_sv(c.t) << " " << args;
    ++checked;
  }
  EXPECT_GT(checked, 200U);
}

TEST(TreeAegConformance, ImportedCellsMatchTolgSinkBySink) {
  const std::vector<Case> cases = {
      {N::Lnast_ntype_plus, {"a", "c"}},      {N::Lnast_ntype_minus, {"a", "c"}},    {N::Lnast_ntype_mult, {"a", "c"}},
      {N::Lnast_ntype_div, {"a", "c"}},       {N::Lnast_ntype_bit_and, {"a", "c"}},  {N::Lnast_ntype_bit_or, {"a", "c"}},
      {N::Lnast_ntype_bit_xor, {"a", "c"}},   {N::Lnast_ntype_log_and, {"ba", "bc"}}, {N::Lnast_ntype_log_or, {"ba", "bc"}},
      {N::Lnast_ntype_log_not, {"ba"}},       {N::Lnast_ntype_eq, {"a", "c"}},       {N::Lnast_ntype_ne, {"a", "c"}},
      {N::Lnast_ntype_lt, {"a", "c"}},        {N::Lnast_ntype_le, {"a", "c"}},       {N::Lnast_ntype_gt, {"a", "c"}},
      {N::Lnast_ntype_ge, {"a", "c"}},        {N::Lnast_ntype_shl, {"a", "c"}},      {N::Lnast_ntype_sra, {"a", "c"}},
      {N::Lnast_ntype_bit_not, {"a"}},        {N::Lnast_ntype_bit_not, {"a", "8"}},  {N::Lnast_ntype_get_mask, {"a", "2", "9"}},
      {N::Lnast_ntype_get_mask, {"a", "3"}},  {N::Lnast_ntype_sext, {"a", "5"}},     {N::Lnast_ntype_red_or, {"a"}},
      {N::Lnast_ntype_minus, {"a", "c", "a"}}, {N::Lnast_ntype_plus, {"a", "c", "a"}},
  };
  size_t serial = 0;
  for (const auto& c : cases) {
    const auto              name = std::format("conf_s{}", serial++);
    std::vector<std::string> ins;
    for (const auto& o : c.ops) {
      if (!is_const_text(o) && std::find(ins.begin(), ins.end(), o) == ins.end()) {
        ins.push_back(o);
      }
    }
    auto       tree = unit(name, c.t, c.ops, ins);
    const auto view = upass::aeg::import_view(*tree, tree->io_meta());
    auto       g    = lower(unit(name, c.t, c.ops, ins));
    ASSERT_TRUE(g) << name;
    EXPECT_EQ(view, describe(output_driver(g))) << N::to_sv(c.t);
  }
}

// A wide n-ary statement (the tree step writes a flattened chain as one) gets
// each operand its bank's next slot, the pids setup_sink_pid appends: a Sum's
// added operands at 0, 2, 4, ..., its subtracted ones at 1, 3, 5, ..., a
// one-bank cell's at 0, 1, 2, ... tolg hands them out without rescanning the
// cell's pins per operand.
TEST(TreeAegConformance, WideStatementTakesConsecutiveBankSlots) {
  constexpr int            kWide = 64;
  std::vector<std::string> ins;
  for (int i = 0; i < kWide; ++i) {
    ins.push_back(std::format("i{}", i));
  }
  struct Wide {
    Ltype                 t;
    std::vector<uint32_t> pids;
  };
  std::vector<Wide> cases = {
      {  N::Lnast_ntype_plus, {}},
      { N::Lnast_ntype_minus, {}},
      {N::Lnast_ntype_bit_or, {}}
  };
  for (int i = 0; i < kWide; ++i) {
    cases[0].pids.push_back(static_cast<uint32_t>(2 * i));
    cases[1].pids.push_back(static_cast<uint32_t>(i == 0 ? 0 : 2 * i - 1));
    cases[2].pids.push_back(static_cast<uint32_t>(i));
  }
  size_t serial = 0;
  for (const auto& c : cases) {
    auto g = lower(unit(std::format("conf_w{}", serial++), c.t, ins, ins));
    ASSERT_TRUE(g) << N::to_sv(c.t);
    const auto drv = output_driver(g);
    ASSERT_FALSE(drv.is_invalid()) << N::to_sv(c.t);
    std::vector<uint32_t> pids;
    std::vector<uint32_t> operands;
    for (const auto& sink : drv.get_master_node().inp_sorted_pins()) {
      pids.push_back(static_cast<uint32_t>(sink.get_port_id()));
      const auto d = sink.get_driver_pins();
      ASSERT_EQ(d.size(), 1U) << N::to_sv(c.t);
      operands.push_back(static_cast<uint32_t>(std::stoi(std::string(gu::pin_name_of(d.front())).substr(1))));
    }
    EXPECT_EQ(pids, c.pids) << N::to_sv(c.t);
    // Operand k takes the k-th slot handed out, so pid order is statement order.
    std::vector<uint32_t> order(kWide);
    for (int i = 0; i < kWide; ++i) {
      order[i] = static_cast<uint32_t>(i);
    }
    EXPECT_EQ(operands, order) << N::to_sv(c.t);
  }
}
