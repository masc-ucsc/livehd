// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "specialize.hpp"

#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "bitwidth.hpp"
#include "hash_util.hpp"
#include "cprop.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"

namespace livehd::specialize {
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

namespace {
// A node this pass created (a folded clone's new cells, the fit_to_port masks
// around a published output constant) has no color. In a colored graph it
// would land in color 0 -- a stray region of its own -- so it takes the color
// of a colored neighbor (drivers first, then readers), to a fixpoint. ONLY
// those nodes: a node pass.color left uncolored on purpose (a clock-gate
// barrier the mapper keeps native) stays uncolored. Graph IO and constants are
// never colored.
void inherit_colors(hhds::Graph& g, const absl::flat_hash_set<uint64_t>* existed) {
  std::vector<hhds::Node_class> open;
  bool                          colored = false;
  for (auto n : g.body().nodes()) {
    if (gu::has_color(n)) {
      colored = true;
    } else if (existed == nullptr || !existed->contains(static_cast<uint64_t>(n.get_debug_nid()))) {
      open.push_back(n);
    }
  }
  if (!colored) {
    return;
  }
  for (bool grew = true; grew && !open.empty();) {
    grew = false;
    std::erase_if(open, [&](const hhds::Node_class& n) {
      for (auto sink : n.inp_sorted_pins()) {
        for (auto drv : sink.get_driver_pins()) {
          if (!drv.is_const() && gu::has_color(drv.get_master_node())) {
            gu::set_color(n, gu::color_of(drv.get_master_node()));
            grew = true;
            return true;
          }
        }
      }
      for (const auto& e : n.out_edges()) {
        if (gu::has_color(e.sink.get_master_node())) {
          gu::set_color(n, gu::color_of(e.sink.get_master_node()));
          grew = true;
          return true;
        }
      }
      return false;
    });
  }
}
}  // namespace

namespace {
// max_versions > 1: one content-named clone per distinct constant binding, at
// most `max_versions` per callee (a further binding leaves that instance on the
// shared definition).
int specialize_per_binding(std::vector<std::shared_ptr<hhds::Graph>>& graphs, const Options& options) {
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> versions;  // callee -> binding keys
  // closure of a callee holds no state (memoized per definition)
  std::map<const hhds::Graph*, bool> state_free_memo;
  std::function<bool(const hhds::Graph*)> state_free = [&](const hhds::Graph* g) {
    if (auto it = state_free_memo.find(g); it != state_free_memo.end()) {
      return it->second;
    }
    state_free_memo[g] = false;  // recursion guard
    bool ok = true;
    for (auto n : g->body().nodes()) {
      if (gu::is_type_register(n)) {
        ok = false;
        break;
      }
      if (gu::type_op_of(n) == Ntype_op::Sub) {
        const auto callee = n.get_subnode_graph();
        if (!callee || !state_free(callee.get())) {
          ok = false;
          break;
        }
      }
    }
    state_free_memo[g] = ok;
    return ok;
  };
  std::map<std::string, std::shared_ptr<hhds::Graph>> specializations;
  std::set<const hhds::Graph*>                        pending_refold;
  // The nodes each input graph held before this pass (only for inherit_colors).
  absl::flat_hash_map<const hhds::Graph*, absl::flat_hash_set<uint64_t>> existed;
  if (options.inherit_colors) {
    for (const auto& graph : graphs) {
      auto& set = existed[graph.get()];
      for (auto n : graph->body().nodes()) {
        set.insert(static_cast<uint64_t>(n.get_debug_nid()));
      }
    }
  }
  // A specialized clone: its copied nodes are the callee's (colors included).
  absl::flat_hash_map<const hhds::Graph*, const hhds::Graph*> cloned_from;
  if (options.refold_all) {
    for (const auto& graph : graphs) {
      pending_refold.insert(graph.get());
    }
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
        if (options.state_free_only && !state_free(body.get())) {
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
        bool       in_budget  = true;
        if (!constants.empty() || fold_index) {
          // over the version budget: this instance keeps the shared definition
          auto& seen = versions[std::string{io->get_name()}];
          in_budget  = seen.contains(key) || std::cmp_less(seen.size(), options.max_versions);
          if (in_budget) {
            seen.insert(key);
          }
        }
        if ((!constants.empty() || fold_index) && in_budget) {
          if (auto found = specializations.find(key); found != specializations.end()) {
            body = found->second;
          } else {
            auto*      library = io->get_library();
            // Named by CONTENT (callee + the constant binding), never by a
            // gid or a counter: stable across runs, so a region/kernel cache
            // keyed on module names keeps hitting.
            const auto name = std::format("{}__k{:016x}", io->get_name(), livehd::hash_util::fnv1a64(key));
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
            if (options.inherit_colors) {
              cloned_from[specialized.get()] = body.get();  // a body copy keeps the callee's nids
            }
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
  if (options.inherit_colors) {
    for (const auto& graph : graphs) {
      const absl::flat_hash_set<uint64_t>* before = nullptr;
      if (auto it = existed.find(graph.get()); it != existed.end()) {
        before = &it->second;
      } else if (auto src = cloned_from.find(graph.get()); src != cloned_from.end()) {
        if (auto e = existed.find(src->second); e != existed.end()) {
          before = &e->second;  // a table copy: the clone's nids are the callee's
        }
      }
      if (before != nullptr) {
        inherit_colors(*graph, before);
      }
    }
  }
  return static_cast<int>(specializations.size());
}
}  // namespace
namespace {

// A definition whose closure holds no state (no Flop/Latch/Fflop/Memory, no
// body-less blackbox).
bool is_state_free(const hhds::Graph& g, absl::flat_hash_map<const hhds::Graph*, bool>& memo) {
  if (auto it = memo.find(&g); it != memo.end()) {
    return it->second;
  }
  memo[&g] = false;  // recursion guard
  bool ok = true;
  for (auto n : g.body().nodes()) {
    if (gu::is_type_register(n)) {
      ok = false;
      break;
    }
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      const auto callee = n.get_subnode_graph();
      if (!callee || !is_state_free(*callee, memo)) {
        ok = false;
        break;
      }
    }
  }
  memo[&g] = ok;
  return ok;
}

// One instance of a definition, in a parent that is part of the design.
struct Site {
  hhds::Graph*     parent = nullptr;
  hhds::Node_class node;
};

// What crosses into one definition from ALL its instances, joined: the one
// specialization every instance can share (max_versions == 1).
struct Context {
  std::vector<std::pair<hhds::Port_id, Dlop>>                 constants;  // input tied to the same constant everywhere
  std::vector<std::tuple<hhds::Port_id, uint32_t, bool>>      widths;     // input narrower than declared: (pid, bits, unsign)
  bool                                                        zero_index_fold = false;
  std::string                                                 key;  // content of the context (names the copy)
  [[nodiscard]] bool empty() const { return constants.empty() && widths.empty() && !zero_index_fold; }
};

std::vector<hhds::Pin_class> site_drivers(const Site& s, hhds::Port_id pid) {
  std::vector<hhds::Pin_class> out;
  const auto                   sink = s.node.get_sink_pin(pid);
  if (sink.is_invalid()) {
    return out;
  }
  for (const auto& d : sink.get_driver_pins()) {
    if (d.get_master_node() != s.node) {  // a loop's carry self-edge is no context
      out.push_back(d);
    }
  }
  return out;
}

Context join_context(const hhds::Graph& body, const hhds::GraphIO& io, const std::vector<Site>& sites) {
  Context ctx;
  // Inputs a rolled loop drives per ordinal (index, activation, carries) never
  // take one value for the whole body.
  absl::flat_hash_set<hhds::Port_id> per_ordinal;
  bool                               all_nonnegative = !sites.empty();
  std::optional<hhds::Port_id>       index;
  for (const auto& s : sites) {
    const auto loop = s.node.subnode_loop();
    if (!loop) {
      all_nonnegative = false;
      continue;
    }
    if (loop->index_input) {
      per_ordinal.insert(*loop->index_input);
    }
    if (loop->activation_input) {
      per_ordinal.insert(*loop->activation_input);
    }
    for (const auto& c : s.node.subnode_group().carries()) {
      per_ordinal.insert(c.input_port());
    }
    all_nonnegative = all_nonnegative && nonnegative_index(loop, io) && (!index || *index == *loop->index_input);
    if (loop->index_input) {
      index = loop->index_input;
    }
  }
  for (const auto& port : io.get_input_pin_decls()) {
    if (per_ordinal.contains(port.port_id)) {
      continue;
    }
    const auto input = body.get_input_pin(port.name);
    if (input.is_invalid() || input.out_edges().begin() == input.out_edges().end()) {
      continue;  // unread: nothing to gain
    }
    // constant: every instance drives the SAME fully known constant
    std::optional<std::string> value;
    std::optional<Dlop>        first;
    bool                       same_const = true;
    // width: every instance's driver fits, with one signedness
    uint32_t                   max_bits = 0;
    std::optional<bool>        unsign;
    bool                       widths_ok = true;
    for (const auto& s : sites) {
      const auto drivers = site_drivers(s, port.port_id);
      if (drivers.size() != 1) {
        same_const = widths_ok = false;
        break;
      }
      const auto& d = drivers.front();
      if (d.is_const() && !gu::const_of(d).has_unknowns()) {
        const auto text = std::string{gu::const_of(d).to_pyrope()};
        if (!value) {
          value = text;
          first = gu::const_of(d);
        } else if (*value != text) {
          same_const = false;
        }
      } else {
        same_const = false;
      }
      const int  b = gu::bits_of(d);
      const bool u = gu::is_unsign(d);
      if (b <= 0 || (unsign && *unsign != u)) {
        widths_ok = false;
      } else {
        unsign   = u;
        max_bits = std::max<uint32_t>(max_bits, static_cast<uint32_t>(b));
      }
    }
    if (same_const && first) {
      ctx.constants.emplace_back(port.port_id, *first);
      ctx.key += std::format(":c{}={}", port.port_id, *value);
      continue;
    }
    // narrower than declared, same sign, or an unsigned value into a signed port
    if (widths_ok && unsign && port.bits > 0 && max_bits < port.bits && (*unsign || !port.unsign)) {
      ctx.widths.emplace_back(port.port_id, max_bits, *unsign);
      ctx.key += std::format(":w{}={}{}", port.port_id, max_bits, *unsign ? 'u' : 's');
    }
  }
  if (all_nonnegative && index && !zero_index_shifts(const_cast<hhds::Graph&>(body), *index).empty()) {
    ctx.zero_index_fold = true;
    ctx.key += std::format(":nonnegative_index={}", *index);
  }
  return ctx;
}

// Apply `ctx` to `g` (a copy this pass owns): fit the narrower inputs, tie
// the constant inputs. The INTERFACE never changes (every consumer marshals a
// port at its declared width): a narrower input is a Get_mask/Sext right after
// the input pin, so cprop and bitwidth see the range the instances provide.
void apply_context(hhds::Graph& g, const Context& ctx) {
  auto io = g.get_io();
  for (const auto& [pid, bits, unsign] : ctx.widths) {
    for (const auto& port : io->get_input_pin_decls()) {
      if (port.port_id != pid) {
        continue;
      }
      const auto input   = g.get_input_pin(port.name);
      const auto readers = gu::Edge_vec(input.out_edges().begin(), input.out_edges().end());
      if (readers.empty()) {
        continue;
      }
      const auto fitted = gu::fit_to_port(g, input, static_cast<int>(bits), !unsign);
      for (const auto& edge : readers) {
        edge.sink.del_sink(input);
        fitted.connect_sink(edge.sink);
      }
    }
  }
  for (const auto& [pid, value] : ctx.constants) {
    for (const auto& port : io->get_input_pin_decls()) {
      if (port.port_id != pid) {
        continue;
      }
      const auto constant = gu::create_const(g, value);
      const auto input    = g.get_input_pin(port.name);
      const auto readers  = gu::Edge_vec(input.out_edges().begin(), input.out_edges().end());
      for (const auto& edge : readers) {
        edge.sink.del_sink(input);
        gu::fit_to_port(g, constant, std::max<uint32_t>(1, port.bits), !port.unsign).connect_sink(edge.sink);
      }
    }
  }
}

// The range a child's output provably has is a property of the child alone,
// so the PARENT may use it without customizing anything: an instance output
// narrower than declared is read through a Get_mask/Sext of that width.
// `done` keeps one fit per (instance, port). Returns true when the parent
// changed.
bool narrow_site_outputs(const Site& s, absl::flat_hash_set<std::pair<uint64_t, uint64_t>>& done) {
  auto       node    = s.node;
  auto       body    = node.get_subnode_graph();
  bool       changed = false;
  if (!body || node.subnode_loop()) {
    return false;  // a rolled loop's outputs are its carries: left alone
  }
  for (const auto& port : body->get_io()->get_output_pin_decls()) {
    const auto out = body->get_output_pin(port.name);
    if (out.is_invalid() || port.bits == 0) {
      continue;
    }
    const auto drv = out.get_driver_pin();
    if (drv.is_invalid() || drv.is_const() || gu::is_graph_input_pin(drv)) {
      continue;
    }
    const int  b = gu::bits_of(drv);
    const bool u = gu::is_unsign(drv);
    if (b <= 0 || static_cast<uint32_t>(b) >= port.bits || (!u && port.unsign)) {
      continue;
    }
    const auto key = std::make_pair(static_cast<uint64_t>(node.get_debug_nid()) ^ (reinterpret_cast<uintptr_t>(s.parent) << 20),
                                    static_cast<uint64_t>(port.port_id));
    if (!done.insert(key).second) {
      continue;
    }
    const auto driver  = node.get_driver_pin(port.port_id);
    if (driver.is_invalid()) {
      continue;
    }
    const auto readers = gu::Edge_vec(driver.out_edges().begin(), driver.out_edges().end());
    if (readers.empty()) {
      continue;
    }
    const auto fitted = gu::fit_to_port(*s.parent, driver, b, !u);
    for (const auto& edge : readers) {
      if (edge.sink.get_master_node() == node) {
        continue;
      }
      edge.sink.del_sink(driver);
      fitted.connect_sink(edge.sink);
      changed = true;
    }
  }
  return changed;
}

// Publish a callee's constant outputs into one instance's parent (an output
// the callee drives with a constant, or a zero-trip-safe identity carry of a
// constant seed). Returns true when the parent changed.
bool publish_outputs(const Site& s) {
  auto       node    = s.node;
  auto       body    = node.get_subnode_graph();
  auto       io      = node.get_subnode_io();
  auto*      graph   = s.parent;
  const auto loop    = node.subnode_loop();
  bool       changed = false;
  std::vector<std::pair<hhds::Port_id, hhds::Port_id>> carries;
  if (loop) {
    for (const auto& c : node.subnode_group().carries()) {
      carries.emplace_back(c.input_port(), c.output_port());
    }
  }
  for (const auto& port : body->get_io()->get_output_pin_decls()) {
    const auto output         = body->get_output_pin(port.name);
    auto       value          = output.get_driver_pin();
    bool       identity_carry = false;
    if (loop) {
      for (const auto& carry : carries) {
        if (carry.second != port.port_id) {
          continue;
        }
        for (const auto& input : io->get_input_pin_decls()) {
          if (input.port_id == carry.first && value == body->get_input_pin(input.name)) {
            for (const auto& seed : node.get_sink_pin(carry.first).get_driver_pins()) {
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
    if (value.is_invalid() || !value.is_const() || gu::const_of(value).has_unknowns()) {
      continue;
    }
    if (loop && !identity_carry && (loop->count == 0 || loop->activation_input)) {
      continue;  // zero trips or an early exit must not acquire the body's constant
    }
    const auto driver = node.get_driver_pin(port.port_id);
    if (driver.is_invalid()) {
      continue;
    }
    const auto readers = gu::Edge_vec(driver.out_edges().begin(), driver.out_edges().end());
    for (const auto& edge : readers) {
      if (edge.sink.get_master_node() == node) {
        continue;
      }
      edge.sink.del_sink(driver);
      auto replacement = gu::create_const(*graph, gu::const_of(value));
      replacement      = gu::fit_to_port(*graph, replacement, std::max<uint32_t>(1, port.bits), !port.unsign);
      replacement      = gu::fit_to_port(*graph, replacement, std::max(1, gu::bits_of(driver)), !gu::is_unsign(driver));
      replacement.connect_sink(edge.sink);
      changed = true;
    }
  }
  return changed;
}

// max_versions == 1: per definition, the context ALL its instances share. A
// definition that receives one gets ONE copy `<name>__k<hash>` (the original
// stays untouched -- a later run, or another flow, may still want it) and
// every instance is re-pointed at it; a copy this pass made is refined in
// place. Parents change only where they reference the child. To a fixpoint,
// top-down (contexts flow into children) then bottom-up (constant outputs and
// narrower copy outputs flow into parents).
int specialize_shared(std::vector<std::shared_ptr<hhds::Graph>>& graphs, const Options& options) {
  absl::flat_hash_map<const hhds::Graph*, bool> state_free_memo;
  absl::flat_hash_set<const hhds::Graph*> created;   // copies this pass owns
  std::set<const hhds::Graph*>            pending;   // bodies to refold
  absl::flat_hash_map<const hhds::Graph*, absl::flat_hash_set<uint64_t>> existed;
  absl::flat_hash_map<const hhds::Graph*, const hhds::Graph*>             cloned_from;
  absl::flat_hash_set<std::pair<const hhds::Graph*, hhds::Port_id>>       fitted_inputs;  // (copy, input) already fitted
  absl::flat_hash_set<std::pair<uint64_t, uint64_t>>                      fitted_outputs;
  if (options.inherit_colors) {
    for (const auto& graph : graphs) {
      auto& set = existed[graph.get()];
      for (auto n : graph->body().nodes()) {
        set.insert(static_cast<uint64_t>(n.get_debug_nid()));
      }
    }
  }
  if (options.refold_all) {
    for (const auto& graph : graphs) {
      pending.insert(graph.get());
    }
  }
  // The design's roots: the definitions nothing instantiates (before any copy).
  absl::flat_hash_set<const hhds::Graph*> instantiated;
  for (const auto& g : graphs) {
    for (auto n : g->body().nodes()) {
      if (gu::is_type_sub(n)) {
        if (auto c = n.get_subnode_graph()) {
          instantiated.insert(c.get());
        }
      }
    }
  }
  std::vector<std::shared_ptr<hhds::Graph>> roots;
  for (const auto& g : graphs) {
    if (!instantiated.contains(g.get())) {
      roots.push_back(g);
    }
  }
  int copies = 0;
  for (int round = 0; round < 8; ++round) {
    for (const auto& g : graphs) {
      if (pending.erase(g.get()) != 0) {
        refold_private_body(g);
      }
    }
    // Parents-first order over the design reachable from the roots, and every
    // instance of each definition.
    std::vector<std::shared_ptr<hhds::Graph>>                      order;
    absl::flat_hash_map<const hhds::Graph*, std::vector<Site>>     sites;
    absl::flat_hash_set<const hhds::Graph*>                        seen;
    std::vector<std::shared_ptr<hhds::Graph>>                      postorder;
    const auto visit = [&](auto& self, const std::shared_ptr<hhds::Graph>& g) -> void {
      if (!seen.insert(g.get()).second) {
        return;
      }
      for (auto n : g->body().nodes()) {
        if (!gu::is_type_sub(n)) {
          continue;
        }
        if (auto c = n.get_subnode_graph()) {
          sites[c.get()].push_back(Site{.parent = g.get(), .node = n});
          self(self, c);
        }
      }
      postorder.push_back(g);
    };
    for (const auto& r : roots) {
      visit(visit, r);
    }
    order.assign(postorder.rbegin(), postorder.rend());
    bool changed = false;
    // top-down: the shared context into each child
    for (const auto& body : order) {
      auto it = sites.find(body.get());
      if (it == sites.end() || !body->get_io()) {
        continue;
      }
      if (options.state_free_only && !created.contains(body.get()) && !is_state_free(*body, state_free_memo)) {
        continue;
      }
      auto ctx = join_context(*body, *body->get_io(), it->second);
      if (created.contains(body.get())) {
        std::erase_if(ctx.widths, [&](const auto& w) { return fitted_inputs.contains({body.get(), std::get<0>(w)}); });
      }
      if (ctx.empty()) {
        continue;
      }
      std::shared_ptr<hhds::Graph> target = body;
      if (!created.contains(body.get())) {
        auto*      library = body->get_io()->get_library();
        const auto name    = std::format("{}__k{:016x}", body->get_name(), livehd::hash_util::fnv1a64(ctx.key));
        if (auto known = library->find_io(name); known && known->get_graph() && created.contains(known->get_graph().get())) {
          target = known->get_graph();
        } else {
          auto io = library->create_io(name);
          for (const auto& port : body->get_io()->get_input_pin_decls()) {
            io->add_input(port.name, port.port_id, port.loop_break);
            io->set_bits(port.name, port.bits);
            io->set_unsign(port.name, port.unsign);
          }
          for (const auto& port : body->get_io()->get_output_pin_decls()) {
            io->add_output(port.name, port.port_id);
            io->set_bits(port.name, port.bits);
            io->set_unsign(port.name, port.unsign);
          }
          target = io->create_graph();
          if (!library->replace_body_from(name, *body)) {
            throw std::runtime_error("pass.specialize: could not copy " + std::string{body->get_name()});
          }
          created.insert(target.get());
          graphs.push_back(target);
          cloned_from[target.get()] = body.get();
          ++copies;
        }
        for (const auto& s : it->second) {
          auto node = s.node;
          if (const auto loop = node.subnode_loop()) {
            node.set_subnode(target->get_io(), *loop);
          } else {
            node.set_subnode(target->get_io());
          }
          pending.insert(s.parent);
        }
      }
      apply_context(*target, ctx);
      for (const auto& w : ctx.widths) {
        fitted_inputs.insert({target.get(), std::get<0>(w)});
      }
      if (ctx.zero_index_fold) {
        for (const auto& s : it->second) {
          const auto loop = s.node.subnode_loop();
          for (auto shift : zero_index_shifts(*target, *loop->index_input)) {
            const auto driver  = shift.get_driver_pin(0);
            const auto readers = gu::Edge_vec(driver.out_edges().begin(), driver.out_edges().end());
            for (const auto& edge : readers) {
              edge.sink.del_sink(driver);
              gu::create_const(*target, *Dlop::create_integer(0)).connect_sink(edge.sink);
            }
            shift.del_node();
          }
          break;
        }
      }
      refold_private_body(target);
      changed = true;
    }
    // bottom-up: constant outputs into parents; narrower outputs of copies
    for (const auto& body : postorder) {
      auto it = sites.find(body.get());
      if (it == sites.end()) {
        continue;
      }
      for (const auto& s : it->second) {
        if (narrow_site_outputs(s, fitted_outputs)) {
          pending.insert(s.parent);
          changed = true;
        }
        if (publish_outputs(s)) {
          pending.insert(s.parent);
          changed = true;
        }
      }
    }
    if (!changed && pending.empty()) {
      break;
    }
  }
  for (const auto& g : graphs) {
    if (pending.erase(g.get()) != 0) {
      refold_private_body(g);
    }
  }
  // An original every instance left STAYS in `graphs`: a simulation testbench
  // may drive any module directly, and a later flow may still want it.
  if (options.inherit_colors) {
    for (const auto& graph : graphs) {
      const absl::flat_hash_set<uint64_t>* before = nullptr;
      if (auto e = existed.find(graph.get()); e != existed.end()) {
        before = &e->second;
      } else if (auto src = cloned_from.find(graph.get()); src != cloned_from.end()) {
        if (auto e2 = existed.find(src->second); e2 != existed.end()) {
          before = &e2->second;  // a table copy: the clone's nids are the source's
        }
      }
      if (before != nullptr) {
        inherit_colors(*graph, before);
      }
    }
  }
  return copies;
}

}  // namespace

int specialize_constants(std::vector<std::shared_ptr<hhds::Graph>>& graphs, const Options& options) {
  if (options.max_versions <= 0) {
    return 0;  // no hierarchical traversal: every instance boundary as compiled
  }
  if (options.max_versions == 1) {
    return specialize_shared(graphs, options);
  }
  return specialize_per_binding(graphs, options);
}
}  // namespace livehd::specialize
