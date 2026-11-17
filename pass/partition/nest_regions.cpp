// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "nest_regions.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "attrs.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::partition {
namespace {
namespace gu = livehd::graph_util;

// The one Sub node of `top` instantiating `name` (nullopt when absent or instantiated more than once).
std::optional<hhds::Node_class> only_instance(hhds::Graph& top, std::string_view name) {
  std::optional<hhds::Node_class> found;
  for (auto node : top.body().nodes()) {
    if (gu::type_op_of(node) != Ntype_op::Sub) {
      continue;
    }
    const auto io = node.get_subnode_io();
    if (!io || io->get_name() != name) {
      continue;
    }
    if (found) {
      return std::nullopt;
    }
    found = node;
  }
  return found;
}

hhds::Port_id next_port_id(const hhds::GraphIO& io) {
  hhds::Port_id next = 1;
  for (const auto* decls : {&io.get_input_pin_decls(), &io.get_output_pin_decls()}) {
    for (const auto& d : *decls) {
      next = std::max<hhds::Port_id>(next, d.port_id + 1);
    }
  }
  return next;
}

std::string unique_port(const hhds::GraphIO& io, std::string base) {
  std::string name = base;
  for (int i = 1; io.has_input(name) || io.has_output(name); ++i) {
    name = std::format("{}_{}", base, i);
  }
  return name;
}

void stamp(const hhds::Pin_class& pin, uint32_t bits, bool unsign) {
  if (bits != 0) {
    gu::set_bits(pin, bits);
  }
  if (!unsign) {
    gu::set_sign(pin);
  }
}

// Move `child`'s instance from `top` into `parent`'s module. Returns false (nothing changed) when either is not
// instantiated exactly once by `top`.
bool nest_one(hhds::GraphLibrary& lib, hhds::Graph& top, std::string_view child, std::string_view parent) {
  const auto sub_c = only_instance(top, child);
  const auto sub_p = only_instance(top, parent);
  const auto c_io  = lib.find_io(child);
  const auto p_io  = lib.find_io(parent);
  if (!sub_c || !sub_p || !c_io || !p_io) {
    return false;
  }
  auto p_body = p_io->get_graph();
  if (!p_body) {
    return false;
  }
  auto inner = gu::create_typed_node(*p_body, Ntype_op::Sub);
  inner.set_subnode(c_io);
  auto port_id = next_port_id(*p_io);
  // A forwarded port is named after the child region without the def prefix: `g_outer_inner__a`.
  std::string_view tag = child;
  if (const auto prefix = std::string(top.get_name()) + "__"; tag.starts_with(prefix)) {
    tag.remove_prefix(prefix.size());
  }
  // An outside driver the parent already reads through one of its inputs feeds the child through that same input.
  absl::flat_hash_map<hhds::Pin_class, std::string> forwarded_in;  // outside driver -> parent input
  for (const auto& s : sub_p->inp_sorted_pins()) {
    if (const auto drv = s.get_driver_pin(); !drv.is_invalid() && drv.get_master_node() != *sub_c) {
      forwarded_in.emplace(drv, std::string{s.get_pin_name()});
    }
  }

  // Child inputs.
  std::vector<hhds::Pin_class> c_sinks;
  for (const auto& s : sub_c->inp_sorted_pins()) {
    c_sinks.push_back(s);
  }
  for (const auto& s : c_sinks) {
    const std::string name{s.get_pin_name()};
    const auto        drv  = s.get_driver_pin();
    auto              sink = inner.create_sink_pin(name);
    if (drv.is_invalid()) {
      continue;
    }
    if (drv.get_master_node() == *sub_p) {
      // Parent -> child: the wire inside the parent that feeds that parent output.
      const auto pout = p_body->get_output_pin(drv.get_pin_name());
      if (const auto inside = pout.get_driver_pin(); !inside.is_invalid()) {
        inside.connect_sink(sink);
      }
    } else if (drv.is_const()) {
      gu::create_const(*p_body, gu::const_of(drv)).connect_sink(sink);
    } else {
      auto it = forwarded_in.find(drv);
      if (it == forwarded_in.end()) {
        const auto pname = unique_port(*p_io, std::format("{}__{}", tag, name));
        p_io->add_input(pname, port_id++);
        p_io->set_bits(pname, c_io->get_bits(name));
        p_io->set_unsign(pname, c_io->is_unsign(name));
        drv.connect_sink(sub_p->create_sink_pin(pname));
        it = forwarded_in.emplace(drv, pname).first;
      }
      p_body->get_input_pin(it->second).connect_sink(sink);
    }
  }

  // Child outputs.
  std::vector<hhds::Pin_class> c_drivers;
  for (const auto& d : sub_c->out_sorted_pins()) {
    c_drivers.push_back(d);
  }
  std::vector<std::string> dead_parent_inputs;
  for (const auto& d : c_drivers) {
    const std::string name{d.get_pin_name()};
    const auto        bits   = c_io->get_bits(name);
    const auto        unsign = c_io->is_unsign(name);
    auto              src    = inner.create_driver_pin(name);
    stamp(src, bits, unsign);
    std::vector<hhds::Pin_class> sinks;
    for (const auto& e : d.out_edges()) {
      sinks.push_back(e.sink);
    }
    std::optional<hhds::Pin_class> forwarded_out;
    for (const auto& s : sinks) {
      if (s.get_master_node() == *sub_p) {
        // Child -> parent: whatever read that parent input inside now reads the child directly.
        const std::string pin{s.get_pin_name()};
        const auto        pin_in = p_body->get_input_pin(pin);
        std::vector<hhds::Edge_class> readers;
        for (const auto& e : pin_in.out_edges()) {
          readers.push_back(e);
        }
        for (auto& e : readers) {
          const auto reader = e.sink;
          e.del_edge();
          src.connect_sink(reader);
        }
        dead_parent_inputs.push_back(pin);
      } else {
        if (!forwarded_out) {
          const auto pname = unique_port(*p_io, std::format("{}__{}", tag, name));
          p_io->add_output(pname, port_id++);
          p_io->set_bits(pname, bits);
          p_io->set_unsign(pname, unsign);
          src.connect_sink(p_body->get_output_pin(pname));
          forwarded_out = sub_p->create_driver_pin(pname);
          stamp(*forwarded_out, bits, unsign);
        }
        forwarded_out->connect_sink(s);
      }
    }
  }
  sub_c->del_node();  // drops every remaining wrapper edge of the child
  for (const auto& pin : dead_parent_inputs) {
    if (const auto port = sub_p->try_get_sink_pin(p_io->get_input_port_id(pin)); !port.is_invalid()) {
      // The child's own edge into this port died with it; anything else still driving it is cut.
      for (const auto& drv : port.get_driver_pins()) {
        std::vector<hhds::Edge_class> edges;
        for (const auto& e : drv.out_edges()) {
          if (e.sink == port) {
            edges.push_back(e);
          }
        }
        for (auto& e : edges) {
          e.del_edge();
        }
      }
    }
    p_io->delete_input(pin);
  }
  p_body->commit();
  return true;
}
}  // namespace

void record_region_index(hhds::Graph& top, const std::vector<std::pair<std::string, std::string>>& child_parent,
                         const std::vector<Region_entry>& regions) {
  if (child_parent.empty() && regions.empty()) {
    return;
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("nesting");
  w.StartArray();
  for (const auto& [child, parent] : child_parent) {
    w.StartArray();
    w.String(child.c_str());
    w.String(parent.c_str());
    w.EndArray();
  }
  w.EndArray();
  w.Key("regions");
  w.StartArray();
  for (const auto& r : regions) {
    w.StartObject();
    w.Key("module");
    w.String(r.module.c_str());
    w.Key("color");
    w.Int(r.color);
    w.Key("path");
    w.StartArray();
    for (const auto& p : r.path) {
      w.String(p.c_str());
    }
    w.EndArray();
    w.Key("parent");
    w.String(r.parent.c_str());
    w.Key("files");
    w.StartArray();
    for (const auto& f : r.files) {
      w.String(f.c_str());
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  top.get_input_node().attr(livehd::attrs::region_nesting).set(std::string{buffer.GetString()});
}

std::vector<Region_entry> region_entries(hhds::Graph& top) {
  std::vector<Region_entry> out;
  auto                      attr = top.get_input_node().attr(livehd::attrs::region_nesting);
  if (!attr.has()) {
    return out;
  }
  const std::string   text{attr.get()};
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  if (!doc.IsObject() || !doc.HasMember("regions") || !doc["regions"].IsArray()) {
    return out;
  }
  for (const auto& r : doc["regions"].GetArray()) {
    Region_entry e;
    e.module = r["module"].GetString();
    e.color  = r["color"].GetInt();
    e.parent = r["parent"].GetString();
    for (const auto& p : r["path"].GetArray()) {
      e.path.emplace_back(p.GetString());
    }
    for (const auto& f : r["files"].GetArray()) {
      e.files.emplace_back(f.GetString());
    }
    out.push_back(std::move(e));
  }
  return out;
}

size_t nest_regions(hhds::GraphLibrary& lib, hhds::Graph& top) {
  auto attr = top.get_input_node().attr(livehd::attrs::region_nesting);
  if (!attr.has()) {
    return 0;
  }
  const std::string   text{attr.get()};
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  size_t moved = 0;
  if (doc.IsObject() && doc.HasMember("nesting") && doc["nesting"].IsArray()) {
    for (const auto& pair : doc["nesting"].GetArray()) {
      if (pair.IsArray() && pair.Size() == 2 && pair[0].IsString() && pair[1].IsString()) {
        moved += nest_one(lib, top, pair[0].GetString(), pair[1].GetString()) ? 1 : 0;
      }
    }
  }
  attr.del();
  top.commit();
  return moved;
}

}  // namespace livehd::partition
