// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "inline_sub.hpp"

#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

TEST(SubInline, FeedThroughChainRetainsItsExternalDriver) {
  // The instance appears cyclic as a node, but each output simply feeds the
  // next independent lane. Its final output is exactly the parent input.
  constexpr int count = 128;
  auto&         lib   = livehd::Hhds_graph_library::instance("lgdb_inline_alias_chain");
  auto          cio   = lib.create_io("lanes");
  for (int i = 0; i < count; ++i) {
    cio->add_input(std::format("x{}", i), static_cast<hhds::Port_id>(i));
    cio->add_output(std::format("y{}", i), static_cast<hhds::Port_id>(count + i));
    cio->set_bits(std::format("x{}", i), 8);
    cio->set_bits(std::format("y{}", i), 8);
  }
  auto child = cio->create_graph();
  for (int i = 0; i < count; ++i) {
    child->get_input_pin(std::format("x{}", i)).connect_sink(child->get_output_pin(std::format("y{}", i)));
  }
  auto pio = lib.create_io("parent");
  pio->add_input("data", 0);
  pio->add_output("result", 1);
  pio->set_bits("data", 8);
  pio->set_bits("result", 8);
  auto parent = pio->create_graph();
  auto inst   = gu::create_typed_node(*parent, Ntype_op::Sub);
  inst.set_subnode(cio);
  auto data = parent->get_input_pin("data");
  data.connect_sink(inst.create_sink_pin(0));
  for (int i = 1; i < count; ++i) {
    inst.create_driver_pin(static_cast<hhds::Port_id>(count + i - 1))
        .connect_sink(inst.create_sink_pin(static_cast<hhds::Port_id>(i)));
  }
  inst.create_driver_pin(static_cast<hhds::Port_id>(2 * count - 1)).connect_sink(parent->get_output_pin("result"));
  ASSERT_TRUE(gu::inline_sub_instance(parent.get(), inst, "test"));
  int drivers = 0;
  for (auto edge_drv : parent->get_output_pin("result").get_driver_pins()) {
    EXPECT_EQ(edge_drv, data);
    ++drivers;
  }
  EXPECT_EQ(drivers, 1);
}

namespace {

// The cell `c` as a Verilog netlist declares it: inputs a, b (ports 0, 1),
// outputs y, z (ports 2, 3). Its model lives in a SIDE library (a `lec --lib`
// gensim graph) and numbers the same ports differently: b=0, a=1, y=5, z=6,
// with y = ~a and z = b. `model_drop` leaves a port out of the model and
// `a_bits` states a's width on the model. Every port is unsigned, as a Liberty
// cell's is (pass/liberty), so no connection needs a boundary fit.
struct Override_fixture {
  std::shared_ptr<hhds::Graph> parent;
  std::shared_ptr<hhds::Graph> model;
  hhds::Node_class             inst;

  Override_fixture(std::string_view tag, std::string_view model_drop = "", uint32_t a_bits = 1) {
    auto& lib  = livehd::Hhds_graph_library::instance(std::format("lgdb_inline_ovr_{}", tag));
    auto& side = livehd::Hhds_graph_library::instance(std::format("lgdb_inline_ovr_{}_side", tag));
    auto  decl = lib.create_io("c");  // body-less: the netlist's own view of the cell
    decl->add_input("a", 0);
    decl->add_input("b", 1);
    decl->add_output("y", 2);
    decl->add_output("z", 3);
    for (const auto* p : {"a", "b", "y", "z"}) {
      decl->set_bits(p, 1);
      decl->set_unsign(p, true);
    }
    auto mio = side.create_io("c");
    if (model_drop != "b") {
      mio->add_input("b", 0);
    }
    mio->add_input("a", 1);
    mio->add_output("y", 5);
    if (model_drop != "z") {
      mio->add_output("z", 6);
    }
    mio->add_output("iq", 7);  // a model-only output: nothing on the instance reads it
    mio->set_bits("a", a_bits);
    for (const auto& d : mio->get_input_pin_decls()) {
      mio->set_unsign(d.name, true);
    }
    for (const auto& d : mio->get_output_pin_decls()) {
      mio->set_unsign(d.name, true);
    }
    model    = mio->create_graph();
    auto inv = gu::create_typed_node(*model, Ntype_op::Not);
    model->get_input_pin("a").connect_sink(inv.create_sink_pin(0));
    auto ny = inv.create_driver_pin(0);
    gu::set_bits(ny, 1);
    ny.connect_sink(model->get_output_pin("y"));
    if (model_drop != "b" && model_drop != "z") {
      model->get_input_pin("b").connect_sink(model->get_output_pin("z"));
    }

    auto pio = lib.create_io("top");
    pio->add_input("pa", 0);
    pio->add_input("pb", 1);
    pio->add_output("ry", 2);
    pio->add_output("rz", 3);
    for (const auto* p : {"pa", "pb", "ry", "rz"}) {
      pio->set_bits(p, 1);
      pio->set_unsign(p, true);
    }
    parent = pio->create_graph();
    inst   = gu::create_typed_node(*parent, Ntype_op::Sub);
    inst.set_subnode(decl);
    parent->get_input_pin("pa").connect_sink(inst.create_sink_pin(0));
    parent->get_input_pin("pb").connect_sink(inst.create_sink_pin(1));
    inst.create_driver_pin(2).connect_sink(parent->get_output_pin("ry"));
    inst.create_driver_pin(3).connect_sink(parent->get_output_pin("rz"));
  }

  [[nodiscard]] int subs() const {
    int n = 0;
    for (auto node : parent->body().nodes()) {
      n += gu::type_op_of(node) == Ntype_op::Sub ? 1 : 0;
    }
    return n;
  }
};

}  // namespace

// A def override is bound by port NAME through the instance's own IO. By port
// id, ~a would read the parent's `pb` (the model's port 1 is a, the instance's
// is b) and neither instance output (2, 3) would match a model output (5, 6):
// both readers silently disconnected.
TEST(SubInline, OverrideBindsByPortName) {
  Override_fixture f("name");
  EXPECT_EQ(gu::sub_def_port_mismatch(f.inst, f.model.get()), "");
  ASSERT_TRUE(gu::inline_sub_instance(f.parent.get(), f.inst, "test", f.model.get()));
  EXPECT_EQ(f.subs(), 0);

  auto ry = f.parent->get_output_pin("ry").get_driver_pin();
  ASSERT_FALSE(ry.is_invalid()) << "the y reader lost its driver";
  ASSERT_EQ(gu::type_op_of(ry.get_master_node()), Ntype_op::Not);
  hhds::Pin_class not_in;
  for (auto sink : ry.get_master_node().inp_sorted_pins()) {
    not_in = sink.get_driver_pin();
  }
  EXPECT_EQ(not_in, f.parent->get_input_pin("pa")) << "~a must read the parent's driver of port a";

  auto rz = f.parent->get_output_pin("rz").get_driver_pin();
  EXPECT_EQ(rz, f.parent->get_input_pin("pb")) << "z = b must forward the parent's driver of port b";
}

// A model that does not describe the instance's ports is refused BEFORE any
// mutation: the instance and its edges stay exactly as they were.
TEST(SubInline, OverrideMissingPortRefusesUntouched) {
  for (const auto* drop : {"b", "z"}) {
    Override_fixture f(std::format("drop_{}", drop), drop);
    const auto       why = gu::sub_def_port_mismatch(f.inst, f.model.get());
    EXPECT_NE(why.find(std::format("'{}'", drop)), std::string::npos) << why;
    EXPECT_FALSE(gu::inline_sub_instance(f.parent.get(), f.inst, "test", f.model.get()));
    EXPECT_EQ(f.subs(), 1);
    EXPECT_EQ(f.parent->get_output_pin("ry").get_driver_pin().get_master_node(), f.inst);
    EXPECT_EQ(f.parent->get_output_pin("rz").get_driver_pin().get_master_node(), f.inst);
  }
}

namespace {

// A one-state cell `s` (d, en -> q) whose state element carries the name its
// OWN copy gave it (`latch_16`, the name cgen invents for a model's anonymous
// latch), instantiated as `u`. Returns the spliced state node's name, or
// "<unnamed>".
std::string spliced_state_name(std::string_view tag, Ntype_op op, const std::optional<std::string>& state_name, bool name_state) {
  auto& lib = livehd::Hhds_graph_library::instance(std::format("lgdb_inline_state_{}", tag));
  auto  cio = lib.create_io("s");
  cio->add_input("d", 0);
  cio->add_input("en", 1);
  cio->add_output("q", 2);
  for (const auto* p : {"d", "en", "q"}) {
    cio->set_bits(p, 1);
  }
  auto child = cio->create_graph();
  auto st    = gu::create_typed_node(*child, op);
  st.attr(hhds::attrs::name).set(op == Ntype_op::Latch ? "latch_16" : "flop_16");
  child->get_input_pin("d").connect_sink(gu::setup_sink_by_name(st, "din"));
  child->get_input_pin("en").connect_sink(gu::setup_sink_by_name(st, op == Ntype_op::Latch ? "enable" : "clock_pin"));
  auto q = st.create_driver_pin(0);
  gu::set_bits(q, 1);
  q.connect_sink(child->get_output_pin("q"));

  auto pio = lib.create_io("top");
  pio->add_input("pd", 0);
  pio->add_input("pe", 1);
  pio->add_output("pq", 2);
  for (const auto* p : {"pd", "pe", "pq"}) {
    pio->set_bits(p, 1);
  }
  auto parent = pio->create_graph();
  auto inst   = gu::create_typed_node(*parent, Ntype_op::Sub);
  inst.set_subnode(cio);
  inst.attr(hhds::attrs::name).set("u");
  parent->get_input_pin("pd").connect_sink(inst.create_sink_pin(0));
  parent->get_input_pin("pe").connect_sink(inst.create_sink_pin(1));
  inst.create_driver_pin(2).connect_sink(parent->get_output_pin("pq"));
  EXPECT_TRUE(gu::inline_sub_instance(parent.get(), inst, "test", nullptr, name_state, true, false, state_name));
  for (auto node : parent->body().nodes()) {
    if (gu::type_op_of(node) == op) {
      return gu::has_name(node) ? std::string{gu::node_name_of(node)} : std::string{"<unnamed>"};
    }
  }
  return "<missing>";
}

}  // namespace

// `state_name` replaces the name a spliced state element carries from the body
// (lec names a `--lib` cell's state after the MODEL, whichever copy of the cell
// was spliced): unset keeps the body's own name, empty leaves it anonymous
// (or, with name_state, names a flop after the instance), non-empty is
// prefixed like any other name.
TEST(SubInline, StateNameOverride) {
  EXPECT_EQ(spliced_state_name("keep", Ntype_op::Latch, std::nullopt, false), "u.latch_16");
  EXPECT_EQ(spliced_state_name("anon", Ntype_op::Latch, std::string{}, false), "<unnamed>");
  EXPECT_EQ(spliced_state_name("model", Ntype_op::Latch, std::string{"IQ"}, false), "u.IQ");
  EXPECT_EQ(spliced_state_name("flop_inst", Ntype_op::Flop, std::string{}, true), "u");
  EXPECT_EQ(spliced_state_name("flop_keep", Ntype_op::Flop, std::nullopt, true), "u.flop_16");
}

TEST(SubInline, OverrideWidthMismatchRefuses) {
  Override_fixture f("width", "", /*a_bits=*/4);
  const auto       why = gu::sub_def_port_mismatch(f.inst, f.model.get());
  EXPECT_NE(why.find("1 bit(s) on the instance but 4"), std::string::npos) << why;
  EXPECT_FALSE(gu::inline_sub_instance(f.parent.get(), f.inst, "test", f.model.get()));
  EXPECT_EQ(f.subs(), 1);
  EXPECT_EQ(gu::sub_def_port_mismatch(f.inst, nullptr), "the definition has no declared IO");
}

namespace {

// `top.result = narrow(a = x).y` with `x` an unsigned 8-bit input. `a_bits`
// and `a_unsigned` declare the callee input (0 = width-less), `y_bits` the
// callee output (of a's sign), which forwards `a` unchanged. `x_const`, when set, drives
// the instance with that constant instead of `x`.
struct Fit_fixture {
  std::shared_ptr<hhds::Graph> parent;
  hhds::Node_class             inst;

  Fit_fixture(std::string_view tag, uint32_t a_bits, bool a_unsigned, uint32_t y_bits, std::optional<int64_t> x_const = {},
              bool read_a = true) {
    auto& lib = livehd::Hhds_graph_library::instance(std::format("lgdb_inline_fit_{}", tag));
    auto  cio = lib.create_io("narrow");
    cio->add_input("a", 0);
    cio->set_bits("a", a_bits);
    cio->set_unsign("a", a_unsigned);
    cio->add_output("y", 1);
    cio->set_bits("y", y_bits);
    cio->set_unsign("y", a_unsigned);
    auto child = cio->create_graph();
    if (read_a) {
      child->get_input_pin("a").connect_sink(child->get_output_pin("y"));
    } else {
      gu::create_const(*child, *Dlop::create_integer(0)).connect_sink(child->get_output_pin("y"));
    }

    auto pio = lib.create_io("top");
    pio->add_input("x", 0);
    pio->set_bits("x", 8);
    pio->set_unsign("x", true);
    pio->add_output("result", 1);
    pio->set_bits("result", 8);
    pio->set_unsign("result", true);
    parent = pio->create_graph();
    inst   = gu::create_typed_node(*parent, Ntype_op::Sub);
    inst.set_subnode(cio);
    const auto x = x_const ? gu::create_const(*parent, *Dlop::create_integer(*x_const)) : parent->get_input_pin("x");
    x.connect_sink(inst.create_sink_pin(0));
    inst.create_driver_pin(1).connect_sink(parent->get_output_pin("result"));
  }

  [[nodiscard]] int nodes() const {
    int n = 0;
    for ([[maybe_unused]] auto node : parent->body().nodes()) {
      ++n;
    }
    return n;
  }

  [[nodiscard]] hhds::Pin_class result() const { return parent->get_output_pin("result").get_driver_pin(); }
};

// The first driver of `node`'s sink `pid`.
hhds::Pin_class operand(const hhds::Node_class& node, hhds::Port_id pid) {
  for (auto sink : node.inp_sorted_pins()) {
    if (sink.get_port_id() == pid) {
      return sink.get_driver_pin();
    }
  }
  return {};
}

}  // namespace

// A width-less port is a scalar in cgen and cgen_sim: an 8-bit driver keeps
// one bit. Dissolving the instance used to thread `x` straight through -- the
// kernel LEC's inline then proved the design at 8 bits, a false PROVEN against
// the netlist's own Verilog.
TEST(SubInline, WidthlessInputIsFitToAScalar) {
  Fit_fixture f("widthless", 0, true, 8);
  ASSERT_TRUE(gu::inline_sub_instance(f.parent.get(), f.inst, "test"));
  const auto r = f.result();
  ASSERT_EQ(gu::type_op_of(r.get_master_node()), Ntype_op::Get_mask);
  EXPECT_EQ(operand(r.get_master_node(), 0), f.parent->get_input_pin("x"));
  EXPECT_EQ(gu::bits_of(r), 1);
  EXPECT_TRUE(gu::is_unsign(r));
}

// A signed port reinterprets the unsigned 0..255 as -128..127: the fit is a
// Sext keeping the port's 8 bits (its `b` is the kept COUNT, not bit 7).
TEST(SubInline, SignedPortIsFitWithASextOfItsWidth) {
  Fit_fixture f("signed", 8, false, 8);
  ASSERT_TRUE(gu::inline_sub_instance(f.parent.get(), f.inst, "test"));
  const auto r = f.result();
  ASSERT_EQ(gu::type_op_of(r.get_master_node()), Ntype_op::Sext);
  EXPECT_EQ(operand(r.get_master_node(), 0), f.parent->get_input_pin("x"));
  const auto keep = operand(r.get_master_node(), 1);
  ASSERT_TRUE(keep.is_const());
  EXPECT_EQ(gu::const_of(keep).to_just_i64(), 8);
  EXPECT_EQ(gu::bits_of(r), 8);
  EXPECT_FALSE(gu::is_unsign(r));
}

// A callee output declared narrower than what drives it truncates there.
TEST(SubInline, NarrowOutputIsFitToItsPort) {
  Fit_fixture f("narrow_out", 8, true, 4);
  ASSERT_TRUE(gu::inline_sub_instance(f.parent.get(), f.inst, "test"));
  const auto r = f.result();
  ASSERT_EQ(gu::type_op_of(r.get_master_node()), Ntype_op::Get_mask);
  EXPECT_EQ(operand(r.get_master_node(), 0), f.parent->get_input_pin("x"));
  EXPECT_EQ(gu::bits_of(r), 4);
}

// Consistent ports, a constant that fits, and a port the callee never reads
// (its driver is unobservable) all splice with no new node.
TEST(SubInline, WellFormedBoundaryGetsNoFit) {
  Fit_fixture same("same", 8, true, 8);
  const int   before = same.nodes();
  ASSERT_TRUE(gu::inline_sub_instance(same.parent.get(), same.inst, "test"));
  EXPECT_EQ(same.result(), same.parent->get_input_pin("x"));
  EXPECT_EQ(same.nodes(), before - 1);  // only the instance is gone

  Fit_fixture konst("const", 2, true, 8, 3);
  ASSERT_TRUE(gu::inline_sub_instance(konst.parent.get(), konst.inst, "test"));
  ASSERT_TRUE(konst.result().is_const());
  EXPECT_EQ(gu::const_of(konst.result()).to_just_i64(), 3);

  Fit_fixture unread("unread", 1, true, 8, 2, false);
  const int   unread_before = unread.nodes();
  ASSERT_TRUE(gu::inline_sub_instance(unread.parent.get(), unread.inst, "test"));
  EXPECT_EQ(unread.nodes(), unread_before - 1);
}

// A fit node sits in the parent like a clone of the body, so an instance of a
// colored backend graph (pass/synth loop cleanup) leaves no uncolored node.
TEST(SubInline, FitNodeInheritsTheInstanceColor) {
  Fit_fixture f("color", 0, true, 8);
  gu::set_color(f.inst, 7);
  ASSERT_TRUE(gu::inline_sub_instance(f.parent.get(), f.inst, "test", nullptr, false, true, /*inherit_color=*/true));
  const auto fit = f.result().get_master_node();
  ASSERT_EQ(gu::type_op_of(fit), Ntype_op::Get_mask);
  ASSERT_TRUE(gu::has_color(fit));
  EXPECT_EQ(gu::color_of(fit), 7);
}

// `y = a` in the child and the parent wiring inst.y back into inst.a is a
// combinational cycle whether or not the connection needs a fit (here the
// 8-bit y into the width-less a). With the fit on the loop the alias walk
// stops at the fit; rewiring inst.y's reader -- that fit -- to it would have
// closed a silent self-loop instead of the inline-cycle error.
TEST(SubInline, FeedThroughCycleThroughAFitIsStillACycle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_inline_fit_cycle");
  auto  cio = lib.create_io("thru");
  cio->add_input("a", 0);
  cio->set_unsign("a", true);  // width-less: a scalar
  cio->add_output("y", 1);
  cio->set_bits("y", 8);
  cio->set_unsign("y", true);
  auto child = cio->create_graph();
  child->get_input_pin("a").connect_sink(child->get_output_pin("y"));

  auto pio = lib.create_io("top");
  pio->add_output("result", 0);
  pio->set_bits("result", 8);
  pio->set_unsign("result", true);
  auto parent = pio->create_graph();
  auto inst   = gu::create_typed_node(*parent, Ntype_op::Sub);
  inst.set_subnode(cio);
  auto y = inst.create_driver_pin(1);
  y.connect_sink(inst.create_sink_pin(0));
  y.connect_sink(parent->get_output_pin("result"));
  EXPECT_THROW((void)gu::inline_sub_instance(parent.get(), inst, "test"), std::runtime_error);
}
