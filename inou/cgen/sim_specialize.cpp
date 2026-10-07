// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sim_specialize.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "bitwidth.hpp"
#include "cprop.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"

namespace livehd::sim {
namespace gu = livehd::graph_util;

namespace {
bool nonnegative_index(const std::optional<hhds::Subnode_loop>& loop, const hhds::GraphIO& io) {
  if (!loop || !loop->index_input || loop->count == 0 || loop->first < 0) {
    return false;
  }
  for (const auto& port : io.get_input_pin_decls()) {
    if (port.port_id == *loop->index_input) {
      const unsigned bits    = std::max<uint32_t>(1, port.bits);
      const int64_t  maximum = bits >= (port.unsign ? 63u : 64u) ? INT64_MAX : (int64_t{1} << (bits - (port.unsign ? 0 : 1))) - 1;
      if (loop->first > maximum) {
        return false;
      }
      if (loop->step == 0) {
        return true;
      }
      return loop->step > 0
                 ? loop->count - 1 <= static_cast<uint64_t>(maximum - loop->first) / static_cast<uint64_t>(loop->step)
                 : loop->count - 1 <= static_cast<uint64_t>(loop->first) / (uint64_t{0} - static_cast<uint64_t>(loop->step));
    }
  }
  return false;
}
std::vector<hhds::Node_class> zero_index_shifts(hhds::Graph& graph, hhds::Port_id index) {
  hhds::Pin_class ordinal;
  for (const auto& port : graph.get_io()->get_input_pin_decls()) {
    if (port.port_id == index) {
      ordinal = graph.get_input_pin(port.name);
    }
  }
  std::vector<hhds::Node_class> result;
  if (ordinal.is_invalid()) {
    return result;
  }
  for (auto node : graph.body().nodes()) {
    const auto op = gu::type_op_of(node);
    if ((op == Ntype_op::SRA || op == Ntype_op::SHL) && gu::get_driver_of_sink_name(node, "a").is_known_false()
        && gu::get_driver_of_sink_name(node, "b") == ordinal) {
      result.push_back(node);
    }
  }
  return result;
}
}  // namespace

void refold_private_body(const std::shared_ptr<hhds::Graph>& graph) {
  Cprop{}.do_trans(graph);
  Bitwidth{16}.do_trans(graph);
}

void specialize_constants(std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  std::map<std::string, std::shared_ptr<hhds::Graph>> specializations;
  std::set<const hhds::Graph*>                        pending_refold;
  for (const auto& graph : graphs) {
    pending_refold.insert(graph.get());
  }
  bool                                                changed = true;
  while (changed) {
    changed             = false;
    const auto existing = graphs;
    for (const auto& graph : existing) {
      // Scan all call sites each round: a callee may have published a constant
      // since its parent was visited. Only bodies rewritten by this pass need
      // another expensive fold/inference sweep. The old global retry folded
      // every unaffected definition after any specialization in the library.
      if (pending_refold.erase(graph.get()) != 0) {
        refold_private_body(graph);
        // A fold can publish an output for a parent already visited this round.
        // Keep one following scan even if this body's call sites need no edits.
        changed = true;
      }
      std::vector<hhds::Node_class> nodes;
      for (auto node : graph->body().nodes()) {
        nodes.push_back(node);
      }
      for (auto node : nodes) {
        if (!gu::is_type_sub(node)) {
          continue;
        }
        auto body = node.get_subnode_graph();
        auto io   = node.get_subnode_io();
        if (!body || !io) {
          continue;
        }
        std::vector<std::pair<hhds::Port_id, hhds::Pin_class>> constants;
        std::string                                            key(io->get_name());
        const auto                                             loop        = node.subnode_loop();
        const bool                                             nonnegative = nonnegative_index(loop, *io);
        if (nonnegative) {
          key += ":nonnegative_index=" + std::to_string(*loop->index_input);
        }
        std::vector<std::pair<hhds::Port_id, hhds::Port_id>> carries;
        if (loop) {
          for (const auto& c : node.subnode_group().carries()) {
            carries.emplace_back(c.input_port(), c.output_port());
          }
        }
        for (const auto& port : io->get_input_pin_decls()) {
          if (loop
              && (loop->index_input == port.port_id || loop->activation_input == port.port_id
                  || std::ranges::any_of(carries, [&](const auto& c) { return c.first == port.port_id; }))) {
            continue;
          }
          const auto input = body->get_input_pin(port.name);
          if (input.out_edges().begin() == input.out_edges().end()) {
            continue;
          }
          const auto sink = node.get_sink_pin(port.port_id);
          if (sink.is_invalid()) {
            continue;
          }
          for (const auto& driver : sink.get_driver_pins()) {
            if (!driver.is_const() || gu::const_of(driver).has_unknowns()) {
              continue;
            }
            constants.emplace_back(port.port_id, driver);
            key += ":" + std::to_string(port.port_id) + "=" + std::string(gu::const_of(driver).to_pyrope());
          }
        }
        const bool fold_index = nonnegative && !zero_index_shifts(*body, *loop->index_input).empty();
        if (!constants.empty() || fold_index) {
          if (auto found = specializations.find(key); found != specializations.end()) {
            body = found->second;
          } else {
            auto*      library = io->get_library();
            const auto name    = "__sim_const_" + std::to_string(graph->get_gid()) + "_" + std::to_string(specializations.size());
            auto       specialized_io = library->create_io(name);
            for (const auto& port : io->get_input_pin_decls()) {
              specialized_io->add_input(port.name, port.port_id, port.loop_break);
              specialized_io->set_bits(port.name, port.bits);
              specialized_io->set_unsign(port.name, port.unsign);
            }
            for (const auto& port : io->get_output_pin_decls()) {
              specialized_io->add_output(port.name, port.port_id);
              specialized_io->set_bits(port.name, port.bits);
              specialized_io->set_unsign(port.name, port.unsign);
            }
            auto specialized = specialized_io->create_graph();
            if (!library->replace_body_from(name, *body)) {
              throw std::runtime_error("simulator constant specialization copy failed");
            }
            for (const auto& [pid, value] : constants) {
              const auto constant = gu::create_const(*specialized, gu::const_of(value));
              for (const auto& port : io->get_input_pin_decls()) {
                if (port.port_id == pid) {
                  const auto input   = specialized->get_input_pin(port.name);
                  const auto readers = gu::Edge_vec(input.out_edges().begin(), input.out_edges().end());
                  for (const auto& edge : readers) {
                    edge.sink.del_sink(input);
                    gu::fit_to_port(*specialized, constant, std::max<uint32_t>(1, port.bits), !port.unsign).connect_sink(edge.sink);
                  }
                }
              }
            }
            Cprop{}.do_trans(specialized);
            // Zero shifted by an ordinal stays zero over the descriptor's
            // proven nonnegative domain. Cprop deliberately ignores hints, so
            // expose this instance-specific fact here, without expanding it.
            if (nonnegative) {
              for (auto shift : zero_index_shifts(*specialized, *loop->index_input)) {
                const auto driver  = shift.get_driver_pin(0);
                const auto readers = gu::Edge_vec(driver.out_edges().begin(), driver.out_edges().end());
                for (const auto& edge : readers) {
                  edge.sink.del_sink(driver);
                  gu::create_const(*specialized, *Dlop::create_integer(0)).connect_sink(edge.sink);
                }
                shift.del_node();
              }
              Cprop{}.do_trans(specialized);
            }
            Bitwidth{16}.do_trans(specialized);
            specializations.emplace(key, specialized);
            graphs.push_back(specialized);
            pending_refold.insert(specialized.get());
            body = std::move(specialized);
          }
          if (loop) {
            node.set_subnode(body->get_io(), *loop);
          } else {
            node.set_subnode(body->get_io());
          }
          changed = true;
          pending_refold.insert(graph.get());
        }
        // A statically returned value can bypass the instance. Zero trips and
        // early exit must not accidentally acquire the body's constant.
        for (const auto& port : body->get_io()->get_output_pin_decls()) {
          const auto output         = body->get_output_pin(port.name);
          auto       value          = output.get_driver_pin();
          bool       identity_carry = false;
          if (loop) {
            for (const auto& carry : carries) {
              if (carry.second == port.port_id) {
                for (const auto& input : io->get_input_pin_decls()) {
                  if (input.port_id == carry.first && value == body->get_input_pin(input.name)) {
                    const auto sink = node.get_sink_pin(carry.first);
                    for (const auto& seed : sink.get_driver_pins()) {
                      if (seed.get_master_node() != node && seed.is_const()) {
                        const int  bits   = std::max<uint32_t>(1, input.bits);
                        const auto fitted = input.unsign ? gu::const_of(seed).get_mask_op(0, bits)
                                                         : gu::const_of(seed).sext_op(Dlop::create_integer(bits));
                        value             = gu::create_const(*graph, *fitted);
                        identity_carry    = true;
                        break;
                      }
                    }
                  }
                }
              }
            }
          }
          if (value.is_invalid() || !value.is_const() || gu::const_of(value).has_unknowns()) {
            continue;
          }
          if (loop && !identity_carry) {
            if (loop->count == 0 || loop->activation_input) {
              continue;
            }
          }
          const auto driver = node.get_driver_pin(port.port_id);
          if (driver.is_invalid()) {
            continue;
          }
          const auto readers = gu::Edge_vec(driver.out_edges().begin(), driver.out_edges().end());
          for (const auto& edge : readers) {
            if (edge.sink.get_master_node() != node) {
              edge.sink.del_sink(driver);
              auto replacement = gu::create_const(*graph, gu::const_of(value));
              replacement      = gu::fit_to_port(*graph, replacement, std::max<uint32_t>(1, port.bits), !port.unsign);
              replacement      = gu::fit_to_port(*graph, replacement, std::max(1, gu::bits_of(driver)), !gu::is_unsign(driver));
              replacement.connect_sink(edge.sink);
              changed = true;
              pending_refold.insert(graph.get());
            }
          }
        }
      }
    }
  }
}
}  // namespace livehd::sim
