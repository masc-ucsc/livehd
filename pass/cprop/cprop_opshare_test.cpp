// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bitwidth.hpp"
#include "cprop.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "query.hpp"

namespace {
namespace gu = livehd::graph_util;

TEST(CpropOpSharingProof, SignedEqualityComparesIntegerValues) {
  for (int bits : {1, 4}) {
    for (bool outside : {false, true}) {
      SCOPED_TRACE("bits=" + std::to_string(bits) + " outside=" + std::to_string(outside));
      hhds::GraphLibrary reference_library, implementation_library;
      const auto         build = [&](hhds::GraphLibrary& library, bool constant_result) {
        auto io = library.create_io("signed_eq");
        io->add_input("a", 1);
        io->set_bits("a", bits);
        io->set_unsign("a", false);
        io->add_output("out", 2);
        io->set_bits("out", 1);
        io->set_unsign("out", true);
        auto graph = io->create_graph();
        gu::set_sbits(graph->get_input_pin("a"), bits);
        auto output = gu::create_const(*graph, *Dlop::create_integer(0));
        if (!constant_result) {
          auto eq = gu::create_typed_node(*graph, Ntype_op::EQ);
          gu::setup_sink_pid(eq, 0).connect_driver(graph->get_input_pin("a"));
          const int value = (1 << (outside ? bits : bits - 1)) - 1;
          gu::setup_sink_pid(eq, 0).connect_driver(gu::create_const(*graph, *Dlop::create_integer(value)));
          output = eq.create_driver_pin(0);
          gu::set_ubits(output, 1);
        }
        output.connect_sink(graph->get_output_pin("out"));
        return graph;
      };
      auto                     reference      = build(reference_library, false);
      auto                     implementation = build(implementation_library, true);
      livehd::lec::Lec_options options;
      options.cones   = "false";
      options.timeout = 5;
      auto result     = livehd::lec::prove_equal(reference.get(), implementation.get(), options);
      EXPECT_EQ(result.verdict, outside ? livehd::lec::Verdict::Proven : livehd::lec::Verdict::Refuted) << result.detail;
    }
  }
}

TEST(CpropOpSharingProof, MixedSignsAndNarrowObservation) {
  for (auto op : {Ntype_op::Sum,
                  Ntype_op::Mult,
                  Ntype_op::LT,
                  Ntype_op::GT,
                  Ntype_op::EQ,
                  Ntype_op::SRA,
                  Ntype_op::Div,
                  Ntype_op::Rem,
                  Ntype_op::Sext,
                  Ntype_op::Rxor,
                  Ntype_op::Popcount,
                  Ntype_op::Get_mask}) {
    for (int output_bits : {3, 12}) {
      SCOPED_TRACE(std::string{Ntype::get_name(op)} + " output=" + std::to_string(output_bits));
      hhds::GraphLibrary reference_library, implementation_library;
      auto               io = reference_library.create_io("opshare");
      io->add_input("s", 1);
      io->set_bits("s", 8);
      io->set_unsign("s", false);
      io->add_input("a", 2);
      io->set_bits("a", 4);
      io->set_unsign("a", false);
      io->add_input("b", 3);
      io->set_bits("b", 6);
      io->set_unsign("b", true);
      io->add_output("out", 4);
      io->set_bits("out", output_bits);
      io->set_unsign("out", true);
      auto ref = io->create_graph();
      // Match the reader's complete IO contract: the finite encoders consume
      // pin sign hints while bitwidth seeds from the GraphIO declaration.
      gu::set_sbits(ref->get_input_pin("s"), 8);
      gu::set_sbits(ref->get_input_pin("a"), 4);
      gu::set_ubits(ref->get_input_pin("b"), 6);
      const auto constant   = [&](int value) { return gu::create_const(*ref, *Dlop::create_integer(value)); };
      const auto expression = [&](const char* name) {
        auto n = gu::create_typed_node(*ref, op);
        if (op == Ntype_op::Rem) {
          // Remainder has no range-inference rule yet. With divisor 3 its
          // result is in [-2,2], including the signed source's remainder.
          gu::set_sbits(n.create_driver_pin(0), 3);
        }
        if (op == Ntype_op::Get_mask) {
          gu::connect_mask_operands(n, ref->get_input_pin(name), constant(7));
        } else {
          gu::setup_sink_pid(n, 0).connect_driver(ref->get_input_pin(name));
          gu::setup_sink_pid(n, Ntype::sink_bank_count(op) == 1 || op == Ntype_op::Sum ? 0 : 1)
              .connect_driver(constant(op == Ntype_op::EQ ? 15 : 3));
        }
        return n.create_driver_pin(0);
      };
      auto mux = gu::create_typed_node(*ref, Ntype_op::Mux);
      mux.create_sink_pin(0).connect_driver(ref->get_input_pin("s"));
      mux.create_sink_pin(1).connect_driver(expression("a"));
      mux.create_sink_pin(2).connect_driver(expression("b"));
      mux.create_driver_pin(0).connect_sink(ref->get_output_pin("out"));
      Bitwidth{10}.do_trans(ref);
      ASSERT_TRUE(implementation_library.copy_from(reference_library, "opshare"));
      auto impl = implementation_library.find_io("opshare")->get_graph();
      Cprop{}.do_trans(impl);
      Bitwidth{10}.do_trans(impl);
      size_t operators = 0;
      for (auto n : impl->body().nodes()) {
        operators += gu::type_op_of(n) == op;
      }
      EXPECT_EQ(operators, 1);
      livehd::lec::Lec_options options;
      options.cones     = "false";
      options.timeout   = 5;
      const auto result = livehd::lec::prove_equal(ref.get(), impl.get(), options);
      EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail << '\n' << result.witness;
    }
  }
}

TEST(CpropMuxTreeProof, TwoGroupSelectionAndExclusiveDefaults) {
  for (bool hotmux : {false, true}) {
    for (bool default_is_b : {false, true}) {
      hhds::GraphLibrary reference_library, implementation_library;
      auto               io   = reference_library.create_io("select_tree");
      int                port = 0;
      for (auto name : {"s", "c", "d", "e", "f", "a", "b"}) {
        io->add_input(name, ++port);
        io->set_bits(name, 8);
        io->set_unsign(name, false);
      }
      io->add_output("out", ++port);
      io->set_bits("out", 8);
      io->set_unsign("out", false);
      auto ref = io->create_graph();
      for (auto name : {"s", "c", "d", "e", "f", "a", "b"}) {
        gu::set_sbits(ref->get_input_pin(name), 8);
      }
      const auto mux = [&](hhds::Pin_class s, hhds::Pin_class f, hhds::Pin_class t) {
        auto n = gu::create_typed_node(*ref, Ntype_op::Mux);
        n.create_sink_pin(0).connect_driver(s);
        n.create_sink_pin(1).connect_driver(f);
        n.create_sink_pin(2).connect_driver(t);
        return n.create_driver_pin(0);
      };
      auto            a = ref->get_input_pin("a"), b = ref->get_input_pin("b");
      auto            left  = mux(ref->get_input_pin("c"), a, b);
      auto            right = mux(ref->get_input_pin("d"), a, b);
      hhds::Pin_class output;
      if (hotmux) {
        auto n = gu::create_typed_node(*ref, Ntype_op::Hotmux);
        for (int i : {0, 1}) {
          auto eq = gu::create_typed_node(*ref, Ntype_op::EQ);
          gu::setup_sink_pid(eq, 0).connect_driver(ref->get_input_pin("s"));
          gu::setup_sink_pid(eq, 0).connect_driver(gu::create_const(*ref, *Dlop::create_integer(i)));
          n.create_sink_pin(2 * i).connect_driver(eq.create_driver_pin(0));
          n.create_sink_pin(2 * i + 1).connect_driver(i ? right : left);
        }
        n.create_sink_pin(4).connect_driver(default_is_b ? b : a);
        output = n.create_driver_pin(0);
      } else {
        output = mux(ref->get_input_pin("s"), left, right);
        if (default_is_b) {
          // Opposite pair order needs a logical inversion. Five old muxes
          // still save one node after charging that inversion explicitly.
          output = mux(ref->get_input_pin("e"), output, mux(ref->get_input_pin("f"), b, a));
        }
      }
      output.connect_sink(ref->get_output_pin("out"));
      Bitwidth{10}.do_trans(ref);
      ASSERT_TRUE(implementation_library.copy_from(reference_library, "select_tree"));
      auto impl = implementation_library.find_io("select_tree")->get_graph();
      livehd::share_mux_regions(*impl, false);
      Bitwidth{10}.do_trans(impl);
      size_t muxes = 0;
      for (auto node : impl->body().nodes()) {
        muxes += gu::type_op_of(node) == Ntype_op::Mux || gu::type_op_of(node) == Ntype_op::Hotmux;
      }
      EXPECT_EQ(muxes, !hotmux && default_is_b ? 3 : 2);
      livehd::lec::Lec_options options;
      options.cones   = "false";
      options.timeout = 5;
      auto result     = livehd::lec::prove_equal(ref.get(), impl.get(), options);
      EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail << '\n' << result.witness;
    }
  }
}
}  // namespace
