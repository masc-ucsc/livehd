// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
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
  Bit mux(Bit s, Bit t, Bit f) { return s ? t : f; }
};

void check_mux(std::vector<int> indices, int selector_bits, bool constant_selector) {
  hhds::GraphLibrary library;
  auto               io = library.create_io("mux");
  io->add_input("sel", 1);
  io->set_bits("sel", selector_bits);
  io->set_unsign("sel", true);
  for (size_t i = 0; i < indices.size(); ++i) {
    const auto name = "data" + std::to_string(i);
    io->add_input(name, i + 2);
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  auto graph = io->create_graph();
  auto sel   = graph->get_input_pin("sel");
  gu::set_ubits(sel, selector_bits);
  std::vector<hhds::Pin_class> data;
  for (size_t i = 0; i < indices.size(); ++i) {
    data.push_back(graph->get_input_pin("data" + std::to_string(i)));
    gu::set_ubits(data.back(), 1);
  }
  for (uint32_t selector = 0; selector < (1U << selector_bits); ++selector) {
    auto       node    = gu::create_typed_node(*graph, Ntype_op::Mux);
    const auto control = constant_selector ? gu::create_const(*graph, *Dlop::create_integer(selector)) : sel;
    if (constant_selector) {
      gu::set_ubits(control, selector_bits);
    }
    gu::setup_sink_pid(node, 0).connect_driver(control);
    for (size_t i = 0; i < indices.size(); ++i) {
      gu::setup_sink_pid(node, indices[i] + 1).connect_driver(data[i]);
    }
    gu::set_ubits(node.create_driver_pin(0), 1);
    for (uint32_t values = 0; values < (1U << indices.size()); ++values) {
      const auto read = [&](hhds::Pin_class pin, int bit) -> Bit {
        if (pin == control) {
          return (selector >> bit) & 1U;
        }
        for (size_t i = 0; i < data.size(); ++i) {
          if (pin == data[i]) {
            const bool reachable = indices.size() == 2 && indices[0] == 0 && indices[1] == 1
                                       ? i == (selector != 0)
                                       : (indices[i] & ((1U << selector_bits) - 1)) == selector;
            if (constant_selector) {
              EXPECT_TRUE(reachable) << "unreachable constant arm was demanded";
            }
            return (values >> i) & 1U;
          }
        }
        ADD_FAILURE() << "unexpected input";
        return 0;
      };
      bool expected = false;
      for (size_t i = 0; i < indices.size(); ++i) {
        const bool selected  = indices.size() == 2 && indices[0] == 0 && indices[1] == 1
                                   ? i == (selector != 0)
                                   : (indices[i] & ((1U << selector_bits) - 1)) == selector;
        expected            |= selected && ((values >> i) & 1U);
      }
      for (bool tree : {false, true}) {
        Bit_ops                      ops;
        livehd::synth::Blast_options options;
        options.mux_tree = tree;
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
        livehd::synth::blast_comb(node, 1, output, ops, read, options, body, region, refuse, refuse_shift);
        EXPECT_EQ(output[0], expected) << "selector=" << selector << " data=" << values << " tree=" << tree;
      }
    }
  }
}

TEST(BlastMux, TreePreservesDenseSparseAliasedAndWidePredicateSemantics) {
  for (bool constant : {false, true}) {
    check_mux({0, 1, 2, 3}, 2, constant);
    check_mux({0, 2, 5, 7}, 3, constant);
    check_mux({0, 1, 4, 5}, 2, constant);
    check_mux({0, 1}, 4, constant);
  }
}
}  // namespace
