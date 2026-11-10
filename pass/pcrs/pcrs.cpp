// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pcrs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "attrs.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::pcrs {
namespace {
namespace gu = graph_util;
using Node   = hhds::Node_class;
using Pin    = hhds::Pin_class;
using Key    = std::pair<uint64_t, uint64_t>;

constexpr std::string_view kPrefix = "__pcrs_rst";

Key key_of(const Pin& pin) { return {pin.get_master_node().get_debug_nid(), pin.get_port_id()}; }
Key key_of(const Node& node) { return {node.get_debug_nid(), 0}; }

// The cells the pass reads and mints. Everything is resolved by master (Liberty cell) name.
struct Lib {
  const physical::Technology&                     tech;
  std::map<std::string, const liberty::Comb_cell*> buffers, inverters;
  std::map<std::string, const liberty::Dff_cell*>  flops, async_flops;
  std::map<std::string, const liberty::Icg_cell*>  icgs;
  const liberty::Comb_cell*                        buffer   = nullptr;  // smallest identity buffer
  const liberty::Comb_cell*                        inverter = nullptr;  // smallest inverter
  const liberty::Dff_cell*                         flop     = nullptr;  // smallest plain posedge flop

  bool placeable(const std::string& name, std::initializer_list<const std::string*> pins) const {
    const auto macro = tech.macros.find(name);
    if (macro == tech.macros.end() || !macro->second.klass.starts_with("CORE")) {
      return false;
    }
    return std::ranges::all_of(pins, [&](const std::string* pin) { return macro->second.pins.contains(*pin); });
  }

  Lib(const physical::Technology& t, const liberty::Cell_library& cells) : tech(t) {
    const auto smaller = [](const liberty::Comb_cell* a, const liberty::Comb_cell* b) {
      return !b || a->area < b->area || (a->area == b->area && a->name < b->name);
    };
    for (const auto& cell : cells.combinational) {
      if (cell.physical_barrier || cell.inputs.size() != 1 || !placeable(cell.name, {&cell.inputs.front(), &cell.output})) {
        continue;
      }
      const auto truth = liberty::cell_truth(cell);
      if (truth == uint64_t{0xaaaaaaaaaaaaaaaa}) {
        buffers[cell.name] = &cell;
        buffer             = smaller(&cell, buffer) ? &cell : buffer;
      } else if (truth == uint64_t{0x5555555555555555}) {
        inverters[cell.name] = &cell;
        inverter             = smaller(&cell, inverter) ? &cell : inverter;
      }
    }
    for (const auto& cell : cells.dffs) {
      if (cell.latch || !placeable(cell.name, {&cell.d_pin, &cell.clk_pin, &cell.q_pin})) {
        continue;
      }
      flops[cell.name] = &cell;
      const auto better = [](const liberty::Dff_cell* a, const liberty::Dff_cell* b) {
        if (!b) {
          return true;
        }
        // The register-mapping rank: area, output count, non-inverted Q first, name.
        return std::tuple(a->area, a->n_out, a->q_inverted, a->name) < std::tuple(b->area, b->n_out, b->q_inverted, b->name);
      };
      flop = better(&cell, flop) ? &cell : flop;
    }
    for (const auto& cell : cells.async_dffs) {
      if (!cell.latch) {
        async_flops[cell.name] = &cell;
      }
    }
    for (const auto& icg : cells.icgs) {
      icgs[icg.name] = &icg;
    }
  }
};

std::string master_name(const Node& node) {
  const auto io = node.get_subnode_io();
  return io ? std::string(io->get_name()) : std::string{};
}

bool is_core_cell(const Lib& lib, const Node& node) {
  const auto io = node.get_subnode_io();
  if (!io) {
    return false;
  }
  const auto macro = lib.tech.macros.find(std::string(io->get_name()));
  return macro != lib.tech.macros.end() && macro->second.klass.starts_with("CORE");
}

bool minted(const Node& node) {
  const auto name = std::string(node.get_name());
  return instance_leaf(name).starts_with(kPrefix);
}

// A cell that only moves a reset through: identity buffer, inverter, or a flop of this pass.
struct Transit {
  std::string input, output;
  bool        inverting = false;
};
std::optional<Transit> transit_of(const Lib& lib, const Node& node) {
  const auto name = master_name(node);
  if (name.empty()) {
    return std::nullopt;
  }
  if (const auto it = lib.buffers.find(name); it != lib.buffers.end()) {
    return Transit{it->second->inputs.front(), it->second->output, false};
  }
  if (const auto it = lib.inverters.find(name); it != lib.inverters.end()) {
    return Transit{it->second->inputs.front(), it->second->output, true};
  }
  if (const auto it = lib.flops.find(name); it != lib.flops.end() && minted(node)) {
    return Transit{it->second->d_pin, it->second->q_pin, it->second->q_inverted};
  }
  return std::nullopt;
}

Pin driver_of(const Node& node, const std::string& pin) {
  const auto sink = node.get_sink_pin(pin);
  if (sink.is_invalid()) {
    return {};
  }
  const auto drivers = sink.get_driver_pins();
  return drivers.size() == 1 ? drivers.front() : Pin{};
}

struct Leaf {
  Pin  sink;
  bool negative = false;
};
struct Network {
  Pin               head;
  std::vector<Leaf> leaves;
  std::vector<Node> dead;
  std::vector<Pin>  ports;  // graph outputs found behind a transit cell: they go back on the head
};

class Scanner {
public:
  Scanner(const Lib& lib) : lib_(lib) {}

  // A sink can be deleted-through when every load of the cell is a movable cell pin, another such cell, or a
  // graph output that can go back on the head: the block's interface keeps the undelayed reset, so it must see the
  // head's polarity. `negative` is the polarity at the cell's input (one driver per cell: one polarity per cell).
  bool transit_ok(const Node& node, bool negative) {
    const auto key = key_of(node);
    if (const auto it = ok_.find(key); it != ok_.end()) {
      return it->second;
    }
    ok_[key] = false;  // a loop is never transit
    const auto t = transit_of(lib_, node);
    bool       ok = t.has_value();
    if (ok) {
      const auto out      = node.get_driver_pin(t->output);
      const bool inverted = negative != t->inverting;
      for (const auto& edge : out.out_edges()) {
        const auto sink = edge.sink;
        const auto next = sink.get_master_node();
        const auto nt   = transit_of(lib_, next);
        const bool through
            = nt && sink.get_port_id() == next.get_sink_pin(nt->input).get_port_id() && transit_ok(next, inverted);
        const bool leaf = is_core_cell(lib_, next) && sink.get_driver_pins().size() == 1;
        const bool port = next.is_output_node() && !inverted && sink.get_driver_pins().size() == 1;
        if (!through && !leaf && !port) {
          ok = false;
          break;
        }
      }
    }
    ok_[key] = ok;
    return ok;
  }

  void walk(const Pin& driver, bool negative, Network& net, bool below = false) {
    for (const auto& edge : driver.out_edges()) {
      const auto sink = edge.sink;
      const auto node = sink.get_master_node();
      const auto t    = transit_of(lib_, node);
      if (t && sink.get_port_id() == node.get_sink_pin(t->input).get_port_id() && transit_ok(node, negative)) {
        net.dead.push_back(node);
        walk(node.get_driver_pin(t->output), negative != t->inverting, net, true);
      } else if (is_core_cell(lib_, node) && sink.get_driver_pins().size() == 1) {
        net.leaves.push_back({sink, negative});
      } else if (below && node.is_output_node()) {
        net.ports.push_back(sink);
      }
    }
  }

  // The driver at the top of a chain of buffers/inverters/pass flops.
  Pin head_of(Pin driver) {
    for (int guard = 0; guard < 4096 && !driver.is_invalid(); ++guard) {
      const auto node = driver.get_master_node();
      const auto t    = transit_of(lib_, node);
      if (!t || node.get_driver_pin(t->output).get_port_id() != driver.get_port_id()) {
        break;
      }
      const auto up = driver_of(node, t->input);
      if (up.is_invalid() || up.is_const()) {
        break;
      }
      driver = up;
    }
    return driver;
  }

private:
  const Lib&                 lib_;
  std::map<Key, bool>        ok_;
};

// The unconditional clock a flop is on: through integrated clock gates and identity buffers.
Pin root_clock(const Lib& lib, Pin driver) {
  for (int guard = 0; guard < 64 && !driver.is_invalid(); ++guard) {
    const auto node = driver.get_master_node();
    const auto name = master_name(node);
    Pin        up;
    if (const auto it = lib.icgs.find(name); it != lib.icgs.end()) {
      up = driver_of(node, it->second->clk_pin);
    } else if (const auto b = lib.buffers.find(name); b != lib.buffers.end()) {
      up = driver_of(node, b->second->inputs.front());
    }
    if (up.is_invalid() || up.is_const()) {
      break;
    }
    driver = up;
  }
  return driver;
}

const liberty::Dff_cell* flop_of(const Lib& lib, const Node& node) {
  const auto name = master_name(node);
  if (const auto it = lib.flops.find(name); it != lib.flops.end()) {
    return it->second;
  }
  if (const auto it = lib.async_flops.find(name); it != lib.async_flops.end()) {
    return it->second;
  }
  return nullptr;
}

// Clock domain of a leaf: the flop's own clock, or the clock of the flops its logic feeds (the D pins
// within a few cells), by majority.
std::optional<Pin> leaf_clock(const Lib& lib, const Pin& sink) {
  const auto node = sink.get_master_node();
  if (const auto* flop = flop_of(lib, node)) {
    const auto clk = driver_of(node, flop->clk_pin);
    if (clk.is_invalid() || clk.is_const()) {
      return std::nullopt;
    }
    return root_clock(lib, clk);
  }
  std::map<Key, std::pair<Pin, unsigned>> votes;
  std::set<Key>                            seen{key_of(node)};
  std::vector<std::pair<Node, int>>        work{{node, 0}};
  for (size_t i = 0; i < work.size(); ++i) {
    const auto [cell, depth] = work[i];
    for (const auto& out : cell.out_sorted_pins()) {
      for (const auto& edge : out.out_edges()) {
        const auto next = edge.sink.get_master_node();
        if (const auto* flop = flop_of(lib, next)) {
          if (edge.sink.get_port_id() == next.get_sink_pin(flop->d_pin).get_port_id()) {
            const auto clk = driver_of(next, flop->clk_pin);
            if (!clk.is_invalid() && !clk.is_const()) {
              const auto root = root_clock(lib, clk);
              auto&      vote = votes[key_of(root)];
              vote.first      = root;
              ++vote.second;
            }
          }
        } else if (depth < 4 && is_core_cell(lib, next) && seen.insert(key_of(next)).second) {
          work.push_back({next, depth + 1});
        }
      }
    }
  }
  const auto best = std::max_element(votes.begin(), votes.end(), [](const auto& a, const auto& b) {
    return std::tie(a.second.second, b.first) < std::tie(b.second.second, a.first);
  });
  if (best == votes.end()) {
    return std::nullopt;
  }
  return best->second.first;
}

// A graph input by its declared name, any other driver by its cell name and port id.
std::string pin_label(const hhds::Graph& graph, const Pin& pin) {
  const auto node = pin.get_master_node();
  if (node.is_input_node()) {
    for (const auto& decl : graph.get_io()->get_input_pin_decls()) {
      if (decl.port_id == pin.get_port_id()) {
        return std::string(decl.name);
      }
    }
  }
  return std::format("{}:{}", std::string(node.get_name()), static_cast<uint64_t>(pin.get_port_id()));
}

// Bisect into groups of at most `fanout` neighbours: split sizes are multiples of fanout, so only the last
// group of a run is short.
void cluster(std::vector<std::pair<Leaf, std::pair<double, double>>>& items, size_t lo, size_t hi, unsigned fanout,
             std::vector<std::vector<Leaf>>& groups) {
  const size_t n = hi - lo;
  if (n <= fanout) {
    std::vector<Leaf> group;
    for (size_t i = lo; i < hi; ++i) {
      group.push_back(items[i].first);
    }
    groups.push_back(std::move(group));
    return;
  }
  double min_x = 1e300, max_x = -1e300, min_y = 1e300, max_y = -1e300;
  for (size_t i = lo; i < hi; ++i) {
    min_x = std::min(min_x, items[i].second.first);
    max_x = std::max(max_x, items[i].second.first);
    min_y = std::min(min_y, items[i].second.second);
    max_y = std::max(max_y, items[i].second.second);
  }
  const bool   by_x = (max_x - min_x) >= (max_y - min_y);
  const auto   cmp  = [by_x](const auto& a, const auto& b) {
    return by_x ? std::tie(a.second.first, a.second.second) < std::tie(b.second.first, b.second.second)
                : std::tie(a.second.second, a.second.first) < std::tie(b.second.second, b.second.first);
  };
  std::sort(items.begin() + lo, items.begin() + hi, cmp);
  const size_t groups_n = (n + fanout - 1) / fanout;
  const size_t left     = std::max<size_t>(1, groups_n / 2) * fanout;
  cluster(items, lo, lo + left, fanout, groups);
  cluster(items, lo + left, hi, fanout, groups);
}

std::vector<std::vector<Leaf>> group_leaves(std::vector<Leaf> leaves, unsigned fanout, const Position_of& position_of) {
  std::vector<std::vector<Leaf>> groups;
  std::vector<std::pair<Leaf, std::pair<double, double>>> placed;
  if (position_of) {
    bool all = true;
    for (const auto& leaf : leaves) {
      const auto at = position_of(std::string(leaf.sink.get_master_node().get_name()));
      if (!at) {
        all = false;
        break;
      }
      placed.push_back({leaf, *at});
    }
    if (all && !placed.empty()) {
      cluster(placed, 0, placed.size(), fanout, groups);
      return groups;
    }
  }
  std::stable_sort(leaves.begin(), leaves.end(), [](const Leaf& a, const Leaf& b) {
    return std::pair(std::string(a.sink.get_master_node().get_name()), static_cast<uint64_t>(a.sink.get_port_id()))
           < std::pair(std::string(b.sink.get_master_node().get_name()), static_cast<uint64_t>(b.sink.get_port_id()));
  });
  for (size_t lo = 0; lo < leaves.size(); lo += fanout) {
    groups.emplace_back(leaves.begin() + lo, leaves.begin() + std::min(leaves.size(), lo + fanout));
  }
  return groups;
}

class Builder {
public:
  Builder(hhds::Graph& graph, const Lib& lib, const Options& options, Stats& stats)
      : graph_(graph), lib_(lib), options_(options), stats_(stats) {
    auto* library = graph.get_io()->get_library();
    buffer_io_    = identity_io(library, *lib.buffer);
    inverter_io_  = identity_io(library, *lib.inverter);
    flop_io_      = liberty::create_dff_io(*library, *lib.flop);
    for (auto n : graph.body().nodes()) {
      if (const auto name = std::string(n.get_name()); name.find(kPrefix) != std::string::npos) {
        names_.insert(name);
      }
    }
  }

  struct Mark {
    size_t cells, pins;
  };
  Mark mark() const { return {minted_names_.size(), minted_pins_.size()}; }
  std::vector<std::string> cells_since(const Mark& m) const { return {minted_names_.begin() + m.cells, minted_names_.end()}; }
  std::vector<std::string> pins_since(const Mark& m) const { return {minted_pins_.begin() + m.pins, minted_pins_.end()}; }

  // The head serves every domain of its network, and each domain connects up to `fanout` loads to it (its first
  // flop level, or the sinks of a flat domain): with many small domains the head itself is the unbuffered net
  // (NVDLA partition_p: 214 loads over 60 gated-clock domains). Put them behind one buffer tree, neighbours
  // together. Buffers add no latency, so every domain keeps its depth.
  void cap_head(const Pin& head, const Position_of& position_of, Domain_stats& domain) {
    std::vector<Leaf> loads;
    for (const auto& edge : head.out_edges()) {
      const auto sink = edge.sink;
      if (is_core_cell(lib_, sink.get_master_node()) && sink.get_driver_pins().size() == 1) {
        loads.push_back({sink, false});
      }
    }
    if (loads.size() <= options_.fanout) {
      return;
    }
    // A cell minted in this call has no position yet: it sits with the load it was named after.
    const Position_of where = [&](std::string_view instance) -> std::optional<std::pair<double, double>> {
      if (!position_of) {
        return std::nullopt;
      }
      const auto owner = minted_owner_.find(instance);
      return position_of(owner == minted_owner_.end() ? instance : std::string_view{owner->second});
    };
    const auto        first = mark();
    std::vector<Pin>  pins;
    std::vector<Node> owners;
    for (const auto& group : group_leaves(std::move(loads), options_.fanout, position_of ? where : Position_of{})) {
      for (const auto& leaf : group) {
        pins.push_back(leaf.sink);
        owners.push_back(leaf.sink.get_master_node());
      }
    }
    fanout(head, std::move(pins), owners, domain);
    for (auto& name : cells_since(first)) {
      domain.cells.push_back(std::move(name));
    }
  }

  // One (reset root, clock domain) tree at `depth` flop levels. Both polarities share the depth.
  void build(const Pin& head, const Pin& clock, std::vector<Leaf> leaves, unsigned depth, const Position_of& position_of,
             Domain_stats& domain) {
    for (const bool negative : {false, true}) {
      std::vector<Leaf> mine;
      std::ranges::copy_if(leaves, std::back_inserter(mine), [&](const Leaf& l) { return l.negative == negative; });
      if (mine.empty()) {
        continue;
      }
      build_polarity(head, clock, negative, group_leaves(std::move(mine), options_.fanout, position_of), depth, domain);
    }
  }

private:
  std::shared_ptr<hhds::GraphIO> identity_io(hhds::GraphLibrary* library, const liberty::Comb_cell& cell) {
    if (const auto io = library->find_io(cell.name)) {
      return io;
    }
    auto io = library->create_io(cell.name);
    io->add_input(cell.inputs.front(), 0);
    io->set_bits(cell.inputs.front(), 1);
    io->add_output(cell.output, 0);
    io->set_bits(cell.output, 1);
    return io;
  }

  Node mint(const std::shared_ptr<hhds::GraphIO>& io, const Node& owner, std::string_view tag, unsigned level) {
    auto        cell      = gu::create_typed_node(graph_, Ntype_op::Sub);
    cell.set_subnode(io);
    const auto  load_name = std::string(owner.get_name());
    const auto  dot       = load_name.rfind('.');
    std::string name;
    do {
      name = std::format("{}{}{}{}_{}", dot == std::string::npos ? std::string{} : load_name.substr(0, dot + 1), kPrefix, tag, level,
                         serial_++);
    } while (!names_.insert(name).second);
    cell.set_name(name);
    minted_names_.emplace_back(name);
    minted_owner_.emplace(name, load_name);
    cell.attr(attrs::color).set(gu::node_color_of(owner));
    if (const auto group = owner.attr(attrs::synth_color_group); group.has()) {
      cell.attr(attrs::synth_color_group).set(std::string(group.get()));
    }
    if (const auto block = owner.attr(attrs::placement_block); block.has()) {
      cell.attr(attrs::placement_block).set(std::string(block.get()));
    }
    if (const auto region = owner.attr(attrs::synth_region_id); region.has() && region.get()) {
      cell.attr(attrs::synth_region_id).set(region.get());
    }
    return cell;
  }

  // Connect `loads` to `driver` through identity buffers so that no driver carries more than `fanout`.
  void fanout(const Pin& driver, std::vector<Pin> loads, const std::vector<Node>& owners, Domain_stats& domain) {
    std::vector<Node> own = owners;
    unsigned          level = 0;
    for (auto& load : loads) {
      gu::drop_drivers(load);
    }
    while (loads.size() > options_.fanout) {
      std::vector<Pin>  next;
      std::vector<Node> next_owner;
      for (size_t lo = 0; lo < loads.size(); lo += options_.fanout) {
        const size_t hi   = std::min(loads.size(), lo + options_.fanout);
        auto         cell = mint(buffer_io_, own[lo], "b", level);
        ++stats_.buffers;
        ++domain.buffers;
        auto out = cell.create_driver_pin(lib_.buffer->output);
        gu::set_bits(out, 1);
        for (size_t i = lo; i < hi; ++i) {
          loads[i].connect_driver(out);
        }
        next.push_back(cell.create_sink_pin(lib_.buffer->inputs.front()));
        next_owner.push_back(own[lo]);
      }
      loads = std::move(next);
      own   = std::move(next_owner);
      ++level;
    }
    for (auto& load : loads) {
      load.connect_driver(driver);
    }
  }

  void build_polarity(const Pin& head, const Pin& clock, bool negative, std::vector<std::vector<Leaf>> groups, unsigned depth,
                      Domain_stats& domain) {
    const auto f = options_.fanout;
    // Flops of an inverting-Q family flip the reset once per level: where the parity is wrong one inverter fixes it.
    const bool flips = depth % 2 == 1 && lib_.flop->q_inverted;
    const bool invert = negative != flips;
    std::vector<Node> group_owner;
    for (const auto& g : groups) {
      group_owner.push_back(g.front().sink.get_master_node());
    }
    if (depth == 0) {
      // Flat: one inverter at the root where the polarity asks for it, then buffers where the cap asks for them.
      std::vector<Pin>  leaf_sinks;
      std::vector<Node> leaf_owner;
      for (size_t g = 0; g < groups.size(); ++g) {
        for (const auto& leaf : groups[g]) {
          leaf_sinks.push_back(leaf.sink);
          leaf_owner.push_back(group_owner[g]);
        }
      }
      Pin source = head;
      if (invert) {
        auto inv = mint(inverter_io_, group_owner.front(), "i", 0);
        auto out = inv.create_driver_pin(lib_.inverter->output);
        gu::set_bits(out, 1);
        inv.create_sink_pin(lib_.inverter->inputs.front()).connect_driver(head);
        source = out;
      }
      fanout(source, leaf_sinks, leaf_owner, domain);
      return;
    }
    // G leaf groups, one leaf flop each. The tree branches by r per level, r = G^(1/depth) (capped by the fanout):
    // a deeper tree has narrower fan-out at every hop, so every hop is shorter, not just one more stage.
    const size_t G = groups.size();
    size_t       r = static_cast<size_t>(std::ceil(std::pow(double(G), 1.0 / double(depth))));
    r              = std::clamp<size_t>(r, 2, f);
    std::vector<size_t> counts(depth);  // counts[k] flops at level k+1
    counts[depth - 1] = G;
    for (int k = int(depth) - 2; k >= 0; --k) {
      counts[k] = (counts[k + 1] + r - 1) / r;
    }
    std::vector<std::vector<Node>> owners(depth);
    owners[depth - 1] = group_owner;
    for (int k = int(depth) - 2; k >= 0; --k) {
      for (size_t j = 0; j < counts[k]; ++j) {
        owners[k].push_back(owners[k + 1][j * r]);
      }
    }
    std::vector<Pin> previous;  // Q outputs of the level above
    for (unsigned k = 0; k < depth; ++k) {
      std::vector<Pin> dpins, qpins;
      for (size_t j = 0; j < counts[k]; ++j) {
        auto       cell      = mint(flop_io_, owners[k][j], "f", k + 1);
        const auto flop_name = minted_names_.back();
        minted_pins_.push_back(flop_name + ":" + lib_.flop->d_pin);
        minted_pins_.push_back(flop_name + ":" + lib_.flop->q_pin);
        cell.attr(attrs::pcrs_stage).set(uint32_t{k + 1} | (lib_.flop->q_inverted ? uint32_t{1} << 16 : uint32_t{0}));
        ++stats_.flops;
        ++domain.flops;
        auto q = cell.create_driver_pin(lib_.flop->q_pin);
        gu::set_bits(q, 1);
        cell.create_sink_pin(lib_.flop->clk_pin).connect_driver(clock);
        dpins.push_back(cell.create_sink_pin(lib_.flop->d_pin));
        qpins.push_back(q);
      }
      if (k == 0) {
        fanout(head, dpins, owners[0], domain);
      } else {
        for (size_t j = 0; j < dpins.size(); ++j) {
          dpins[j].connect_driver(previous[j / r]);
        }
      }
      previous = std::move(qpins);
    }
    // The leaf flop of a group drives its loads, through one local inverter when the polarity needs it.
    for (size_t g = 0; g < groups.size(); ++g) {
      Pin driver = previous[g];
      if (invert) {
        auto inv = mint(inverter_io_, group_owner[g], "i", depth);
        auto out = inv.create_driver_pin(lib_.inverter->output);
        gu::set_bits(out, 1);
        inv.create_sink_pin(lib_.inverter->inputs.front()).connect_driver(driver);
        driver = out;
      }
      for (const auto& leaf : groups[g]) {
        leaf.sink.connect_driver(driver);
      }
    }
  }

  hhds::Graph&                   graph_;
  const Lib&                     lib_;
  const Options&                 options_;
  Stats&                         stats_;
  std::shared_ptr<hhds::GraphIO> buffer_io_, inverter_io_, flop_io_;
  std::set<std::string>          names_;
  std::vector<std::string>       minted_names_, minted_pins_;
  std::map<std::string, std::string, std::less<>> minted_owner_;  // minted cell -> the load it was named after
  unsigned                       serial_ = 0;
};

}  // namespace

std::string_view instance_leaf(std::string_view instance) {
  const auto dot = instance.rfind('.');
  return dot == std::string_view::npos ? instance : instance.substr(dot + 1);
}

unsigned bypass_stages(hhds::Graph& graph) {
  std::vector<Node> stages;
  for (auto n : graph.body().nodes()) {
    if (n.attr(attrs::pcrs_stage).has()) {
      stages.push_back(n);
    }
  }
  unsigned bypassed = 0;
  for (auto& node : stages) {
    const auto io = node.get_subnode_io();
    if (!io) {
      continue;
    }
    std::string d_name;
    for (const auto& decl : io->get_input_pin_decls()) {
      if (decl.port_id == 1) {
        d_name = std::string(decl.name);
      }
    }
    const auto q = node.try_get_driver_pin(3);
    if (d_name.empty() || q.is_invalid()) {
      continue;
    }
    const auto d = driver_of(node, d_name);
    if (d.is_invalid()) {
      continue;
    }
    Pin source = d;
    if (node.attr(attrs::pcrs_stage).get() & (uint32_t{1} << 16)) {
      auto inverter = gu::create_typed_node(graph, Ntype_op::Not);
      d.connect_sink(inverter.create_sink_pin(0));
      source = inverter.create_driver_pin(0);
      gu::set_bits(source, 1);
      gu::set_unsign(source);
    }
    std::vector<Pin> loads;
    for (const auto& edge : q.out_edges()) {
      loads.push_back(edge.sink);
    }
    for (auto& load : loads) {
      gu::drop_drivers(load);
      load.connect_driver(source);
    }
    gu::drop_drivers(node);
    node.del_node();
    ++bypassed;
  }
  return bypassed;
}

std::optional<unsigned> Depth_state::get(std::string_view domain) const {
  const auto it = depth_.find(domain);
  return it == depth_.end() ? std::nullopt : std::optional<unsigned>{it->second};
}

void Depth_state::set(std::string_view domain, unsigned depth) { depth_[std::string(domain)] = std::min(depth, max_depth); }

Depth_policy Depth_state::policy(unsigned fanout) const {
  return [this, fanout](std::string_view domain, size_t sinks, std::optional<unsigned> current) {
    if (const auto wanted = get(domain)) {
      return *wanted;
    }
    return current ? *current : (sinks <= fanout ? 0u : 1u);
  };
}

namespace {
// `instance:pin` -> the leaf of the instance name (past the hierarchy prefix).
std::string_view label_leaf(std::string_view label) {
  const auto colon = label.rfind(':');
  const auto instance = colon == std::string_view::npos ? label : label.substr(0, colon);
  return instance_leaf(instance);
}
}  // namespace

std::string retarget_for_wns(const Stats& stats, const std::vector<std::string>& worst_path, Depth_state& state, unsigned branching) {
  std::map<std::string, const Domain_stats*, std::less<>> owner;
  for (const auto& domain : stats.domains) {
    for (const auto& cell : domain.cells) {
      owner.emplace(std::string(instance_leaf(cell)), &domain);
    }
  }
  for (const auto& label : worst_path) {
    const auto it = owner.find(label_leaf(label));
    if (it == owner.end()) {
      continue;
    }
    const auto& domain = *it->second;
    const auto  now    = state.get(domain.name).value_or(domain.depth);
    if (now >= state.max_depth) {
      return {};  // as deep as allowed: nothing a deeper tree can still buy
    }
    // Leaf groups of `fanout` sinks, then log_branching of them: the depth at which every hop branches by <= `branching`.
    const double groups = stats.fanout ? std::ceil(double(domain.sinks) / stats.fanout) : 1.0;
    const auto   narrow = branching >= 2 && groups > 1 ? static_cast<unsigned>(std::ceil(std::log(groups) / std::log(double(branching)))) : 1u;
    state.set(domain.name, std::max(now + 1, narrow));
    return domain.name;
  }
  return {};
}

std::vector<std::string> retarget_shrink(const Stats& stats, const Timing_of& timing, double margin, Depth_state& state) {
  std::vector<std::string> shrunk;
  for (const auto& domain : stats.domains) {
    const auto now = state.get(domain.name).value_or(domain.depth);
    if (now == 0 || domain.flop_pins.empty()) {
      continue;
    }
    double worst_slack = 1e300, longest_hop = 0;
    bool   known       = true;
    for (const auto& label : domain.flop_pins) {
      const auto t = timing(label);
      if (!t.slack) {
        known = false;
        break;
      }
      worst_slack = std::min(worst_slack, *t.slack);
      if (t.arrival) {
        longest_hop = std::max(longest_hop, *t.arrival);
      }
    }
    // Merging two hops costs about one more hop: only a domain with that much slack to spare loses a level.
    if (known && worst_slack > longest_hop + margin) {
      state.set(domain.name, now - 1);
      shrunk.push_back(domain.name);
    }
  }
  return shrunk;
}

bool is_pcrs_cell(std::string_view instance) { return instance_leaf(instance).starts_with(kPrefix); }

Stats build_reset(hhds::Graph& graph, const physical::Technology& tech, const liberty::Cell_library& cells, const Options& options,
                  const Depth_policy& depth_of, const Position_of& position_of) {
  Stats stats;
  stats.fanout = options.fanout;
  if (options.fanout < 2) {
    return stats;
  }
  const Lib lib{tech, cells};
  if (!lib.buffer || !lib.inverter || !lib.flop) {
    return stats;  // no placeable buffer, inverter or flop: nothing this pass could mint
  }
  stats.flop_um2 = lib.tech.macros.at(lib.flop->name).width * lib.tech.macros.at(lib.flop->name).height;
  Scanner scanner{lib};

  // Heads: the top of every reset distribution (a graph input, a design flop, any non-transit driver).
  std::vector<Pin>          heads;
  std::set<Key>             seen;
  const auto                add = [&](const Pin& driver) {
    if (driver.is_invalid() || driver.is_const()) {
      return;
    }
    if (gu::bits_of(driver) != 1) {
      ++stats.skipped;
      return;
    }
    if (const auto io = driver.get_master_node().get_subnode_io(); io && io->get_input_pin_decls().empty() && is_core_cell(lib, driver.get_master_node())) {
      return;  // a tie cell: the inactive pin of a clear+preset flop
    }
    const auto head = scanner.head_of(driver);
    if (seen.insert(key_of(head)).second) {
      heads.push_back(head);
    }
  };
  const auto role_drivers = [&](const Node& node) {
    for (auto pin : node.out_sorted_pins()) {
      const auto r = gu::signal_roles_of(pin);
      if ((r & attrs::signal_reset) && !(r & attrs::signal_clock)) {
        add(pin);
      }
    }
  };
  role_drivers(graph.get_input_node());
  for (auto n : graph.body().nodes()) {
    role_drivers(n);
    // A tree this pass built earlier is a network of its own even when the role-tagged cells it replaced are
    // gone (a synchronous reset has no async pin to find it by): the top of its chain is the root.
    if (minted(n)) {
      if (const auto t = transit_of(lib, n)) {
        add(driver_of(n, t->input));
      }
    }
    if (const auto it = lib.async_flops.find(master_name(n)); it != lib.async_flops.end()) {
      for (const auto* pin : {&it->second->reset0_pin, &it->second->reset1_pin}) {
        if (!pin->empty()) {
          const auto driver = driver_of(n, *pin);
          if (!driver.is_invalid() && !driver.is_const()) {
            add(driver);
          }
        }
      }
    }
  }

  // Read-only: the network, its domains and their sinks.
  struct Domain {
    Pin                       clock;
    std::vector<Leaf>         leaves;
    std::optional<unsigned>   current;  // flop levels of the tree in the graph now
    std::vector<std::string>  cells;    // minted cells already serving it
    std::vector<std::string>  pins;     // their flop D/Q pin labels
    unsigned                  flops = 0, buffers = 0;
    unsigned                  depth = 0;
    std::string               name;
  };
  struct Planned {
    Network                  net;
    std::map<Key, Domain>    domains;
    bool                     keep = false;
  };
  std::vector<Planned> plan;
  for (const auto& head : heads) {
    Planned p;
    p.net.head = head;
    scanner.walk(head, false, p.net);
    if (p.net.leaves.empty()) {
      continue;
    }
    std::vector<Leaf> unclocked;
    for (const auto& leaf : p.net.leaves) {
      const auto clock = leaf_clock(lib, leaf.sink);
      if (!clock) {
        unclocked.push_back(leaf);
        continue;
      }
      auto& d = p.domains[key_of(*clock)];
      d.clock = *clock;
      d.leaves.push_back(leaf);
    }
    if (p.domains.empty()) {
      ++stats.skipped;  // no flop anywhere behind it: no clock to register the reset on
      continue;
    }
    // A load that reaches no flop joins the largest domain: it is delayed like the rest.
    auto largest = p.domains.begin();
    for (auto it = p.domains.begin(); it != p.domains.end(); ++it) {
      if (it->second.leaves.size() > largest->second.leaves.size()) {
        largest = it;
      }
    }
    for (const auto& leaf : unclocked) {
      largest->second.leaves.push_back(leaf);
    }
    // The tree already in the graph: its depth per domain (its flops sit on the domain's clock).
    bool any_minted = false;
    for (const auto& dead : p.net.dead) {
      if (!minted(dead)) {
        continue;
      }
      any_minted = true;
      if (const auto it = lib.flops.find(master_name(dead)); it != lib.flops.end()) {
        const auto clk = driver_of(dead, it->second->clk_pin);
        if (const auto found = p.domains.find(clk.is_invalid() ? Key{} : key_of(root_clock(lib, clk))); found != p.domains.end()) {
          const auto stage = dead.attr(attrs::pcrs_stage).has() ? (dead.attr(attrs::pcrs_stage).get() & 0xffffu) : 1u;
          found->second.current = std::max(found->second.current.value_or(0u), stage);
          ++found->second.flops;
          found->second.cells.emplace_back(std::string(dead.get_name()));
          found->second.pins.push_back(std::string(dead.get_name()) + ":" + it->second->d_pin);
          found->second.pins.push_back(std::string(dead.get_name()) + ":" + it->second->q_pin);
        }
      }
    }
    if (any_minted) {
      auto first = p.domains.begin();
      for (auto& [clock_key, domain] : p.domains) {
        if (!domain.current) {
          domain.current = 0;  // minted buffers but no flops: a depth-0 tree
        }
      }
      for (const auto& dead : p.net.dead) {
        if (minted(dead) && !lib.flops.contains(master_name(dead))) {
          first->second.cells.emplace_back(std::string(dead.get_name()));
          ++first->second.buffers;
        }
      }
    }
    // Depth per domain now, so a network that keeps every depth is not touched.
    bool all_keep = any_minted;
    for (auto& [clock_key, domain] : p.domains) {
      domain.name        = std::format("{}@{}", pin_label(graph, p.net.head), pin_label(graph, domain.clock));
      const auto sinks   = domain.leaves.size();
      const auto fallback = domain.current ? *domain.current : (sinks <= options.fanout ? 0u : 1u);
      domain.depth       = depth_of ? depth_of(domain.name, sinks, domain.current) : fallback;
      all_keep           = all_keep && domain.current && *domain.current == domain.depth;
    }
    p.keep = all_keep && !options.regroup;
    plan.push_back(std::move(p));
  }

  // Strip every previous distribution first (their names are free again), then mint the new trees.
  for (auto& p : plan) {
    if (p.keep) {
      continue;
    }
    for (auto& domain : p.domains) {
      for (const auto& leaf : domain.second.leaves) {
        gu::drop_drivers(leaf.sink);
      }
    }
    for (const auto& dead : p.net.dead) {
      gu::drop_drivers(dead);
    }
    for (auto& port : p.net.ports) {
      gu::drop_drivers(port);
      port.connect_driver(p.net.head);
    }
  }
  for (auto& p : plan) {
    if (p.keep) {
      continue;
    }
    for (auto& dead : p.net.dead) {
      dead.del_node();
      ++stats.removed;
    }
  }

  Builder builder{graph, lib, options, stats};
  for (auto& p : plan) {
    ++stats.networks;
    const size_t first_domain = stats.domains.size();
    for (auto& [clock_key, domain] : p.domains) {
      Domain_stats ds;
      ds.name  = domain.name;
      ds.depth = domain.depth;
      ds.sinks = static_cast<unsigned>(domain.leaves.size());
      stats.sinks += ds.sinks;
      if (p.keep) {
        ds.kept    = true;
        ds.flops   = domain.flops;
        ds.buffers = domain.buffers;
        ds.cells     = std::move(domain.cells);
        ds.flop_pins = std::move(domain.pins);
      } else {
        const auto first_cell = builder.mark();
        builder.build(p.net.head, domain.clock, std::move(domain.leaves), domain.depth, position_of, ds);
        ds.cells     = builder.cells_since(first_cell);
        ds.flop_pins = builder.pins_since(first_cell);
      }
      stats.domains.push_back(std::move(ds));
    }
    if (!p.keep && stats.domains.size() > first_domain) {
      builder.cap_head(p.net.head, position_of, stats.domains[first_domain]);
    }
  }
  return stats;
}

}  // namespace livehd::pcrs
