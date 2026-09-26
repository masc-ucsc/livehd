// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <array>
#include <cstdint>

#include "blast.hpp"
#include "gtest/gtest.h"

namespace {
namespace gu = livehd::graph_util;
using Bit    = uint8_t;
struct Bit_ops {
  Bit zero() { return 0; }
  Bit one() { return 1; }
  Bit inv(Bit a) { return !a; }
  Bit and_(Bit a, Bit b) { return a && b; }
  Bit or_(Bit a, Bit b) { return a || b; }
  Bit xor_(Bit a, Bit b) { return a != b; }
};

TEST(BlastCompare, EveryBankOperandParticipates) {
  for (auto op : {Ntype_op::LT, Ntype_op::GT}) {
    for (bool reversed : {false, true}) {
      hhds::GraphLibrary lib;
      auto               io = lib.create_io("compare");
      for (int i = 0; i < 4; ++i) {
        const auto name = std::string(1, 'a' + i);
        io->add_input(name, i + 1);
        io->set_bits(name, 2);
        io->set_unsign(name, i % 2 != 0);
      }
      auto                           graph = io->create_graph();
      std::array<hhds::Pin_class, 4> pins;
      for (int i = 0; i < 4; ++i) {
        pins[i] = graph->get_input_pin(std::string(1, 'a' + i));
        if (i % 2) {
          gu::set_ubits(pins[i], 2);
        } else {
          gu::set_sbits(pins[i], 2);
        }
      }
      auto node = gu::create_typed_node(*graph, op);
      for (int i = 0; i < 4; ++i) {
        const int index = reversed ? (i + 2) % 4 : i;
        gu::setup_sink_pid(node, index % 2).connect_driver(pins[index]);
      }
      gu::set_ubits(node.create_driver_pin(0), 1);
      Bit_ops                               ops;
      livehd::partition::Region_body        body;
      absl::flat_hash_set<hhds::Node_class> region{node};
      const auto                            refuse       = [](hhds::Node_class,
                             std::string_view,
                             std::string_view,
                             std::string_view message,
                             std::string_view,
                             hhds::Pin_class  = {},
                             std::string_view = {}) { ADD_FAILURE() << message; };
      const auto                            refuse_shift = [](const auto&...) { ADD_FAILURE() << "unexpected shift refusal"; };
      for (int a = -2; a < 2; ++a) {
        for (int b = 0; b < 4; ++b) {
          for (int c = -2; c < 2; ++c) {
            for (int d = 0; d < 4; ++d) {
              const std::array<int, 4> values{a, b, c, d};
              const auto               read = [&](hhds::Pin_class pin, int bit) -> Bit {
                for (size_t i = 0; i < pins.size(); ++i) {
                  if (pin == pins[i]) {
                    return (static_cast<uint64_t>(values[i]) >> bit) & 1;
                  }
                }
                ADD_FAILURE() << "unexpected input";
                return 0;
              };
              std::vector<Bit> output(4, 1);
              livehd::synth::blast_comb(node, 4, output, ops, read, {}, body, region, refuse, refuse_shift);
              const bool expected = op == Ntype_op::LT ? std::max(a, c) < std::min(b, d) : std::min(a, c) > std::max(b, d);
              ASSERT_EQ(output[0], expected) << a << ',' << b << ',' << c << ',' << d << " reverse=" << reversed;
              EXPECT_EQ(output[1] | output[2] | output[3], 0);
            }
          }
        }
      }
    }
  }
}
TEST(BlastCompare, NegativeAndWideLiteralsUseTheirValues) {
  hhds::GraphLibrary lib;
  auto               graph = lib.create_io("constant_compare")->create_graph();
  const auto         wide  = Dlop::create_integer(1)->shl_op(Dlop::create_integer(80));
  const auto         high  = Dlop::create_integer(1)->shl_op(Dlop::create_integer(128));
  for (auto op : {Ntype_op::LT, Ntype_op::GT}) {
    auto node = gu::create_typed_node(*graph, op);
    gu::setup_sink_pid(node, 0).connect_driver(gu::create_const(*graph, *Dlop::create_integer(-1)));
    gu::setup_sink_pid(node, 0).connect_driver(gu::create_const(*graph, *wide));
    gu::setup_sink_pid(node, 1).connect_driver(gu::create_const(*graph, *wide->add_op(Dlop::create_integer(1))));
    gu::setup_sink_pid(node, 1).connect_driver(gu::create_const(*graph, *high));
    gu::set_ubits(node.create_driver_pin(0), 1);
    Bit_ops                               ops;
    std::vector<Bit>                      output(1);
    livehd::partition::Region_body        body;
    absl::flat_hash_set<hhds::Node_class> region{node};
    const auto                            refuse       = [](hhds::Node_class,
                           std::string_view,
                           std::string_view,
                           std::string_view message,
                           std::string_view,
                           hhds::Pin_class  = {},
                           std::string_view = {}) { ADD_FAILURE() << message; };
    const auto                            refuse_shift = [](const auto&...) { ADD_FAILURE() << "unexpected shift refusal"; };
    const auto read = [](hhds::Pin_class pin, int bit) -> Bit { return gu::const_of(pin).bit_test(bit); };
    livehd::synth::blast_comb(node, 1, output, ops, read, {}, body, region, refuse, refuse_shift);
    EXPECT_EQ(output[0], op == Ntype_op::LT);
  }
}
}  // namespace
