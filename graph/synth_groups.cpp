// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "synth_groups.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "diag.hpp"
#include "hash_util.hpp"
#include "node_util.hpp"
#include "predict_abc_size.hpp"
#include "synth_policy.hpp"

namespace livehd::synth_attr {
namespace {
using Node   = hhds::Node_class;
namespace gu = livehd::graph_util;
std::string clean(std::string_view s) {
  std::string out;
  for (unsigned char c : s) {
    out += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : '_';
  }
  return out;
}
std::string label(std::string_view path) {
  rapidjson::Document d;
  d.Parse(path.data(), path.size());
  std::string out;
  for (const auto& s : d.GetArray()) {
    if (!out.empty()) {
      out += "_";
    }
    out += clean(s.IsString() ? std::string(s.GetString(), s.GetStringLength()) : json(s));
  }
  return out;
}
}  // namespace

void specialize_calls(std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  std::map<std::string, std::shared_ptr<hhds::Graph>> copies;
  for (size_t gi = 0; gi < graphs.size(); ++gi) {
    const auto parent = graphs[gi];
    for (auto inst : parent->body().nodes()) {
      if (gu::type_op_of(inst) != Ntype_op::Sub) {
        continue;
      }
      auto a = inst.attr(livehd::attrs::synth_policy);
      if (!a.has()) {
        continue;
      }
      auto call = read(a.get());
      if (call.contains("_specialized")) {
        continue;
      }
      const auto child = inst.get_subnode_graph();
      if (!child) {
        continue;  // native memory / technology black box
      }
      const auto io    = child->get_io();
      auto*      lib   = io->get_library();
      const auto key   = std::string(io->get_name()) + write(call);
      auto       found = copies.find(key);
      if (found == copies.end()) {
        const auto name = std::string(io->get_name()) + "__s_" + std::format("{:016x}", livehd::hash_util::fnv1a64(key));
        auto       neo  = lib->create_io(name);
        for (const auto& p : io->get_input_pin_decls()) {
          neo->add_input(p.name, p.port_id, p.loop_break);
          neo->set_bits(p.name, p.bits);
          neo->set_unsign(p.name, p.unsign);
        }
        for (const auto& p : io->get_output_pin_decls()) {
          neo->add_output(p.name, p.port_id);
          neo->set_bits(p.name, p.bits);
          neo->set_unsign(p.name, p.unsign);
        }
        auto body = neo->create_graph();
        if (!lib->replace_body_from(name, *child)) {
          throw std::runtime_error("cannot copy synthesis specialization");
        }
        for (auto n : body->body().nodes()) {
          auto na     = n.attr(livehd::attrs::synth_policy);
          auto policy = na.has() ? read(na.get()) : Policy{};
          rescope(policy, call);
          n.attr(livehd::attrs::synth_policy).set(write(policy));
          if (gu::has_color(n)) {
            gu::del_color(n);
          }
        }
        body->get_input_node().attr(livehd::attrs::coloring_info).del();
        found = copies.emplace(key, body).first;
        graphs.push_back(body);
      }
      inst.set_subnode(found->second->get_io());
      call["_specialized"] = {"true", 3};
      a.set(write(call));
    }
  }
}

bool seed_groups(hhds::Graph* g) {
  struct Group {
    int               color = 0;
    bool              grow  = true;
    std::string       name;
    std::vector<Node> anchors;
  };
  std::map<std::string, Group>           groups;
  absl::flat_hash_map<Node, std::string> explicit_ids;
  absl::flat_hash_set<Node>              legacy_walls;
  int                                    base = 0;
  for (auto node : g->body().nodes()) {
    base   = std::max(base, gu::node_color_of(node));
    auto a = node.attr(livehd::attrs::synth_policy);
    if (!a.has()) {
      if (gu::node_color_of(node) > 0) {
        legacy_walls.insert(node);
      }
      continue;
    }
    auto p  = read(a.get());
    auto id = path(p);
    if (id.empty()) {
      continue;
    }
    auto& group = groups[id];
    group.anchors.push_back(node);
    if (auto it = p.find("grow"); it != p.end()) {
      group.grow &= text(it->second.value) != "false";
    }
    explicit_ids[node] = id;
  }
  if (groups.empty()) {
    return false;
  }
  std::set<std::string> names;
  for (auto& [id, group] : groups) {
    group.color = ++base;
    group.name  = label(id);
    if (!names.insert(group.name).second) {
      livehd::diag::err("pass.color", "synth-name-collision", "syntax")
          .msg("synthesis IDs collide after sanitizing: {} -> {}", id, group.name)
          .fatal();
    }
  }
  // Cache input adjacency once. No cone walk repeatedly scans fanout slots.
  absl::flat_hash_map<Node, std::vector<Node>> fanin;
  absl::flat_hash_map<Node, std::vector<Node>> fanout;
  std::vector<std::vector<Node>>               cones;
  const auto                                   add_root = [&](Node n) {
    if (!n.is_invalid() && !gu::is_builtin_node(n)) {
      cones.push_back({n});
    }
  };
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    for (auto sink : node.inp_sorted_pins()) {
      auto pin = sink.get_driver_pin();
      if (pin.is_invalid() || pin.is_const() || gu::is_graph_input_pin(pin)) {
        continue;
      }
      auto drv = pin.get_master_node();
      fanin[node].push_back(drv);
      fanout[drv].push_back(node);
      if (node.is_loop_break() && sink.get_port_id() == 3) {
        add_root(drv);
      }
    }
  }
  for (const auto& od : g->get_io()->get_output_pin_decls()) {
    auto pin = g->get_output_pin(od.name).get_driver_pin();
    if (!pin.is_invalid() && !pin.is_const()) {
      add_root(pin.get_master_node());
    }
  }
  // Every state/output cone is a candidate, with one physical owner per node.
  // Foreign explicit nodes and all state are traversal boundaries.
  for (auto& cone : cones) {
    absl::flat_hash_set<Node> seen;
    std::vector<Node>         walk = std::move(cone);
    cone.clear();
    for (size_t i = 0; i < walk.size(); ++i) {
      auto n = walk[i];
      if (!seen.insert(n).second || gu::is_builtin_node(n) || n.is_loop_break()) {
        continue;
      }
      cone.push_back(n);
      for (auto d : fanin[n]) {
        walk.push_back(d);
      }
    }
  }
  absl::flat_hash_map<Node, std::string> owner = explicit_ids;
  // Stable group order resolves shared unannotated ownership. Repeated IDs
  // always retain one group, including disconnected roots and soft overflow.
  for (auto& [id, group] : groups) {
    if (!group.grow) {
      continue;
    }
    auto claim = [&](const std::vector<Node>& roots) {
      absl::flat_hash_set<Node> seen;
      std::vector<Node>         walk = roots;
      for (size_t i = 0; i < walk.size(); ++i) {
        auto n = walk[i];
        if (!seen.insert(n).second || gu::is_builtin_node(n) || legacy_walls.contains(n)) {
          continue;
        }
        auto it = owner.find(n);
        if (it != owner.end() && it->second != id) {
          continue;
        }
        owner[n] = id;
        if (n.is_loop_break()) {
          // An explicit register seeds D; do not absorb clock/reset/stall.
          if (!explicit_ids.contains(n)) {
            continue;
          }
          for (auto sink : n.inp_sorted_pins()) {
            if (sink.get_port_id() == 3) {
              auto pin = sink.get_driver_pin();
              if (!pin.is_invalid() && !pin.is_const() && !gu::is_graph_input_pin(pin) && !pin.get_master_node().is_loop_break()) {
                walk.push_back(pin.get_master_node());
              }
            }
          }
        } else {
          for (auto d : fanin[n]) {
            if (!d.is_loop_break()) {
              walk.push_back(d);
            }
          }
        }
      }
    };
    claim(group.anchors);
    std::vector<bool> used(cones.size());
    while (true) {
      size_t   best         = cones.size();
      uint64_t best_overlap = 0;
      for (size_t c = 0; c < cones.size(); ++c) {
        if (used[c]) {
          continue;
        }
        uint64_t overlap = 0;
        bool     foreign = false, fresh = false;
        for (auto n : cones[c]) {
          if (legacy_walls.contains(n)) {
            foreign = true;
            continue;
          }
          auto it = owner.find(n);
          if (it == owner.end()) {
            fresh = true;
          } else if (it->second == id) {
            overlap += 1 + gu::predict_abc_size(n);
          } else if (explicit_ids.contains(n)) {
            foreign = true;
          }
        }
        if (!foreign && fresh && overlap > best_overlap) {
          best         = c;
          best_overlap = overlap;
        }
      }
      if (best == cones.size()) {
        break;
      }
      used[best] = true;
      claim(cones[best]);
    }
  }
  std::string entries = "{";
  for (const auto& [id, group] : groups) {
    if (entries.size() > 1) {
      entries += ",";
    }
    entries += quote(std::to_string(group.color)) + ":{\"path\":" + id + ",\"name\":" + quote(group.name)
               + ",\"grow\":" + (group.grow ? "true" : "false") + ",\"anchors\":" + std::to_string(group.anchors.size()) + "}";
    // Cheap connectivity warning: disconnected components of the owned group.
    absl::flat_hash_set<Node> visited;
    size_t                    components = 0, nodes = 0;
    for (const auto& [n, gid] : owner) {
      if (gid != id) {
        continue;
      }
      ++nodes;
      if (!visited.insert(n).second) {
        continue;
      }
      ++components;
      std::vector<Node> walk{n};
      for (size_t i = 0; i < walk.size(); ++i) {
        const auto visit = [&](Node d) {
          auto it = owner.find(d);
          if (it != owner.end() && it->second == id && visited.insert(d).second) {
            walk.push_back(d);
          }
        };
        for (auto d : fanin[walk[i]]) {
          visit(d);
        }
        for (auto d : fanout[walk[i]]) {
          visit(d);
        }
      }
    }
    if (components > 1) {
      livehd::diag::warn("pass.color", "synth-zero-overlap", "unsupported")
          .msg("synthesis group {} retains {} disconnected components in one region", id, components)
          .emit();
    }
    if (nodes <= 1) {
      livehd::diag::warn("pass.color", "synth-trivial-group", "unsupported")
          .msg("synthesis group {} has only {} hardware node(s)", id, nodes)
          .emit();
    }
  }
  entries += "}";
  for (const auto& [node, id] : owner) {
    gu::set_color(node, groups.at(id).color);
  }
  rapidjson::Document doc;
  auto                old = g->get_input_node().attr(livehd::attrs::coloring_info);
  if (old.has()) {
    const std::string s{old.get()};
    doc.Parse(s.data(), s.size());
  }
  if (!doc.IsObject()) {
    doc.SetObject();
  }
  auto& a = doc.GetAllocator();
  if (doc.HasMember("seeded")) {
    doc.RemoveMember("seeded");
  }
  doc.AddMember("seeded", true, a);
  if (doc.HasMember("packed")) {
    doc.RemoveMember("packed");
  }
  doc.AddMember("packed", true, a);
  if (doc.HasMember("synth_groups")) {
    doc.RemoveMember("synth_groups");
  }
  rapidjson::Document gs;
  gs.Parse(entries.c_str());
  rapidjson::Value copy(gs, a);
  doc.AddMember("synth_groups", copy, a);
  g->get_input_node().attr(livehd::attrs::coloring_info).set(json(doc));
  return true;
}

std::string group_name(hhds::Graph* g, int color) {
  auto a = g->get_input_node().attr(livehd::attrs::coloring_info);
  if (!a.has()) {
    return {};
  }
  const std::string   s{a.get()};
  rapidjson::Document d;
  d.Parse(s.c_str());
  if (!d.IsObject() || !d.HasMember("synth_groups")) {
    return {};
  }
  auto it = d["synth_groups"].FindMember(std::to_string(color).c_str());
  return it == d["synth_groups"].MemberEnd() ? std::string{} : it->value["name"].GetString();
}
}  // namespace livehd::synth_attr
