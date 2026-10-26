// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "custom_cells.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <stdexcept>
#include <utility>

#include "hash_util.hpp"

namespace livehd::placement {
namespace {
std::string optional_text(const std::optional<double>& value) { return value ? std::format("{:.17g}", *value) : "null"; }
// The interface a variant declares: its rails and clock as inputs, its output.
void        require_interface(const hhds::GraphIO& io, const Custom_variant& variant) {
  std::set<std::string> inputs, expected(variant.rails.begin(), variant.rails.end());
  expected.insert(variant.clock);
  for (const auto& pin : io.get_input_pin_decls()) {
    inputs.insert(pin.name);
  }
  const auto& outputs = io.get_output_pin_decls();
  if (inputs != expected || expected.size() != variant.rails.size() + 1 || outputs.size() != 1
      || outputs.front().name != variant.output) {
    throw std::invalid_argument(
        std::format("declaration {} does not have the pins of custom variant {}", io.get_name(), variant.name));
  }
}
bool same_pins(const hhds::GraphIO& a, const hhds::GraphIO& b) {
  const auto same = [](const auto& x, const auto& y) {
    return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), [](const auto& p, const auto& q) {
             return p.name == q.name && p.port_id == q.port_id && p.loop_break == q.loop_break && p.bits == q.bits
                    && p.unsign == q.unsign;
           });
  };
  return same(a.get_input_pin_decls(), b.get_input_pin_decls()) && same(a.get_output_pin_decls(), b.get_output_pin_decls());
}
}  // namespace

const Custom_variant* Custom_catalog::find(std::string_view name) const {
  const auto it = variants.find(name);
  return it == variants.end() ? nullptr : &it->second;
}

const Custom_variant* Custom_catalog::step(std::string_view name, int delta) const {
  const auto* variant = find(name);
  if (!variant) {
    return nullptr;
  }
  const auto& members = families.at(variant->family);
  const auto  index   = std::find(members.begin(), members.end(), variant->name) - members.begin() + delta;
  return index < 0 || index >= static_cast<std::ptrdiff_t>(members.size()) ? nullptr : find(members[index]);
}

std::shared_ptr<const Custom_catalog> custom_catalog(std::vector<Custom_variant> variants) {
  auto catalog = std::make_shared<Custom_catalog>();
  std::sort(variants.begin(), variants.end(), [](const auto& a, const auto& b) {
    return std::tie(a.family, a.rank, a.name) < std::tie(b.family, b.rank, b.name);
  });
  uint64_t hash = hash_util::fnv1a64("custom-catalog-v1");
  for (auto& variant : variants) {
    if (variant.name.empty() || variant.family.empty()) {
      throw std::invalid_argument("a custom variant needs a name and a family");
    }
    auto& members = catalog->families[variant.family];
    if (!members.empty()) {
      const auto& first = catalog->variants.at(members.front());
      if (first.rails != variant.rails || first.clock != variant.clock || first.output != variant.output) {
        throw std::invalid_argument(
            std::format("custom variants {} and {} of family {} have different pins", first.name, variant.name, variant.family));
      }
      if (catalog->variants.at(members.back()).rank == variant.rank) {
        throw std::invalid_argument(std::format("custom family {} repeats rank {}", variant.family, variant.rank));
      }
    }
    std::string text = std::format("{}|{}|{}|{}|{}|{}|{:.17g}|{:.17g}|{}|{}|{}",
                                   variant.name,
                                   variant.family,
                                   variant.register_cell,
                                   variant.rank,
                                   variant.clock,
                                   variant.output,
                                   variant.area_um2,
                                   variant.estimate_um2,
                                   variant.width_dbu,
                                   optional_text(variant.rail_ff),
                                   optional_text(variant.clock_ff));
    for (const auto& rail : variant.rails) {
      text += "|" + rail;
    }
    if (const auto& d = variant.drive) {
      text += std::format("|drive:{}:{}:{}:{:.17g}:{:.17g}:{}:{}:{}",
                          d->sequential,
                          d->name,
                          d->output,
                          d->intrinsic_ps,
                          d->resistance_ohm,
                          optional_text(d->max_capacitance_ff),
                          optional_text(d->max_transition_ps),
                          optional_text(d->max_fanout));
    }
    hash = hash_util::fnv1a64(text + "\n", hash);
    members.push_back(variant.name);
    const auto name = variant.name;
    if (!catalog->variants.emplace(name, std::move(variant)).second) {
      throw std::invalid_argument("custom variant listed twice: " + name);
    }
  }
  catalog->identity = std::format("{:016x}", hash);
  return catalog;
}

namespace {
// The body-less declaration `name` with like's pins, existing or new.
std::shared_ptr<hhds::GraphIO> clone_declaration(hhds::GraphLibrary& library, const hhds::GraphIO& like, const std::string& name) {
  if (const auto existing = library.find_io(name)) {
    if (existing->has_graph() || !same_pins(*existing, like)) {
      throw std::invalid_argument("a different declaration already owns custom cell " + name);
    }
    return existing;
  }
  auto io = library.create_io(name);
  for (const auto& pin : like.get_input_pin_decls()) {
    io->add_input(pin.name, pin.port_id, pin.loop_break);
  }
  for (const auto& pin : like.get_output_pin_decls()) {
    io->add_output(pin.name, pin.port_id, pin.loop_break);
  }
  for (const auto* decls : {&like.get_input_pin_decls(), &like.get_output_pin_decls()}) {
    for (const auto& pin : *decls) {
      if (pin.bits != 0) {
        io->set_bits(pin.name, pin.bits);
      }
      io->set_unsign(pin.name, pin.unsign);
    }
  }
  return io;
}
}  // namespace

std::shared_ptr<hhds::GraphIO> declare_custom_cell(hhds::GraphLibrary& library, const hhds::GraphIO& like,
                                                   const Custom_variant& to) {
  require_interface(like, to);
  return clone_declaration(library, like, to.name);
}

void change_custom_declaration(hhds::Node_class node, const std::string& name) {
  const auto io = node.get_subnode_io();
  if (!io || io->has_graph() || name.empty()) {
    throw std::invalid_argument(std::format("instance {} is not of a body-less custom cell", node.get_name()));
  }
  if (io->get_name() != name) {
    node.set_subnode(clone_declaration(*node.get_graph()->get_io()->get_library(), *io, name));
  }
}

void change_custom_master(hhds::Node_class node, const Custom_variant& from, const Custom_variant& to) {
  const auto io = node.get_subnode_io();
  if (!io || io->get_name() != from.name) {
    throw std::invalid_argument(std::format("instance {} is not of custom variant {}", node.get_name(), from.name));
  }
  if (from.family != to.family || from.rails != to.rails || from.clock != to.clock || from.output != to.output) {
    throw std::invalid_argument(std::format("custom variants {} and {} are not one family with one interface", from.name, to.name));
  }
  // hhds keys pins by port id and set_subnode records only the declaration,
  // so equal port ids keep every pin with its edges and attributes.
  node.set_subnode(declare_custom_cell(*node.get_graph()->get_io()->get_library(), *io, to));
}
}  // namespace livehd::placement
