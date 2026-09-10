// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "ware_module.hpp"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <climits>
#include <format>
#include <map>
#include <string>
#include <tuple>

#include "attr_carry.hpp"
#include "node_util.hpp"
#include "synthesis_cost.hpp"

namespace livehd::abc {
namespace gu = graph_util;

Ware_policy ware_policy(const hhds::Graph& graph, Ware_policy policy) {
  if (auto a = graph.get_input_node().attr(attrs::coloring_info); a.has()) {
    const std::string   json{a.get()};
    rapidjson::Document doc;
    doc.Parse(json.data(), json.size());
    if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("params") && doc["params"].IsObject()) {
      const auto& params = doc["params"];
      auto        read   = [&](const char* key, bool& value) {
        if (params.HasMember(key) && params[key].IsBool()) {
          value = params[key].GetBool();
        }
      };
      read("ware_arith", policy.arith);
      read("ware_cmp", policy.cmp);
      read("ware_shift", policy.shift);
    }
  }
  return policy;
}

namespace {
void shape(const hhds::Pin_class& from, const hhds::Pin_class& to) {
  gu::set_bits(to, std::max(gu::bits_of(from), 1));
  gu::is_unsign(from) ? gu::set_unsign(to) : gu::set_sign(to);
}

std::string family(const hhds::Node_class& node, const Ware_policy& policy) {
  const auto op    = gu::type_op_of(node);
  int        width = 0;
  for (auto e : node.out_edges()) {
    width = std::max(width, gu::bits_of(e.driver));
  }
  if (policy.arith) {
    if (op == Ntype_op::Sum && width > 8) {
      return "sum";
    }
    if (op == Ntype_op::Mult) {
      return "mult";
    }
    if (op == Ntype_op::Div) {
      return "div";
    }
  }
  if (policy.cmp && (op == Ntype_op::LT || op == Ntype_op::GT)) {
    for (auto e : node.inp_edges()) {
      if (gu::bits_of(e.driver) > 8) {
        return op == Ntype_op::LT ? "lt" : "gt";
      }
    }
  }
  if (policy.shift && width > 8 && (op == Ntype_op::SHL || op == Ntype_op::SRA)) {
    auto amount = gu::get_driver_of_sink_name(node, "b");
    auto value  = gu::get_driver_of_sink_name(node, "a");
    if (!amount.is_invalid() && !amount.is_const() && !value.is_const()) {
      return op == Ntype_op::SHL ? "shl" : "sra";
    }
  }
  return {};
}

// Keep just this section's mapping options, not graph counts, source names or
// neighboring colors. Unrelated edits must not rename a width specialization.
std::string section_info(hhds::Graph& graph, int color, Ware_policy policy) {
  rapidjson::Document info;
  info.SetObject();
  auto&            alloc = info.GetAllocator();
  rapidjson::Value params(rapidjson::kObjectType);
  params.AddMember("ware_arith", policy.arith, alloc);
  params.AddMember("ware_cmp", policy.cmp, alloc);
  params.AddMember("ware_shift", policy.shift, alloc);
  info.AddMember("params", params, alloc);
  if (auto a = graph.get_input_node().attr(attrs::coloring_info); a.has()) {
    const std::string   json{a.get()};
    rapidjson::Document source;
    source.Parse(json.data(), json.size());
    const auto key = std::to_string(color);
    if (!source.HasParseError() && source.IsObject() && source.HasMember("region_opts") && source["region_opts"].IsObject()
        && source["region_opts"].HasMember(key.c_str())) {
      rapidjson::Value options(rapidjson::kObjectType);
      options.AddMember(rapidjson::Value(key.c_str(), alloc), rapidjson::Value(source["region_opts"][key.c_str()], alloc), alloc);
      info.AddMember("region_opts", options, alloc);
    }
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  info.Accept(writer);
  return {buffer.GetString(), buffer.GetSize()};
}

std::shared_ptr<hhds::Graph> enclose(hhds::Graph& parent, const hhds::Node_class& node, const std::string& kind,
                                     Ware_policy policy) {
  auto edges = node.inp_edges();
  std::stable_sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
    if (a.sink.get_port_id() != b.sink.get_port_id()) {
      return a.sink.get_port_id() < b.sink.get_port_id();
    }
    const auto key = [](const auto& pin) {
      return std::tuple{gu::bits_of(pin), gu::is_unsign(pin), pin.is_const() ? gu::const_of(pin).to_pyrope() : std::string{}};
    };
    return key(a.driver) < key(b.driver);
  });
  std::map<hhds::Port_id, hhds::Pin_class> outputs;
  for (auto e : node.out_edges()) {
    outputs.emplace(e.driver.get_port_id(), e.driver);
  }
  const bool     narrowable = kind == "sum" || kind == "shl" || kind == "sra" || kind == "mult";
  // `masked_output_width` measures DRIVER PIN 0 ONLY (node_util.hpp out_width),
  // so it may only bound pin 0.  Pin 0 is NOT guaranteed to be the widest
  // realization -- abc_map.cpp:3254 and pass/lec/encode.cpp:2226 both max over
  // out_edges precisely because it is not -- so applying it to every port
  // silently truncated the others.  It also returns 0 for an unsized pin 0
  // (an expected shape, cf. abc_map.cpp:3249), which collapsed EVERY port to
  // 1 bit; hence the `demand > 0` guard, matching abc_map.cpp:3261.
  const uint64_t demand     = narrowable ? gu::masked_output_width(node) : 0;
  const auto output_width   = [&](const auto& pin) {
    const int cap = (demand > 0 && pin.get_port_id() == 0) ? static_cast<int>(demand) : INT_MAX;
    return std::max(1, std::min(gu::bits_of(pin), cap));
  };
  // The full descriptor is stored on the module; its hash is only a short name.
  // Include source mapping options so sections with different policies never
  // accidentally share an implementation selected in a different context.
  std::string descriptor   = kind;
  for (auto e : edges) {
    descriptor += std::format("/i{}:{}:{}", e.sink.get_port_id(), gu::bits_of(e.driver), gu::is_unsign(e.driver));
    if (e.driver.is_const()) {
      descriptor += ":" + gu::const_of(e.driver).to_pyrope();
    }
  }
  for (auto [pid, pin] : outputs) {
    descriptor += std::format("/o{}:{}:{}", pid, output_width(pin), gu::is_unsign(pin));
  }
  const auto color  = gu::node_color_of(node);
  const auto info   = section_info(parent, color, policy);
  descriptor       += std::format("/color:{}", color);
  descriptor       += info;
  uint64_t hash     = 14695981039346656037ULL;
  for (unsigned char ch : descriptor) {
    hash = (hash ^ ch) * 1099511628211ULL;
  }
  auto*      lib  = parent.get_io()->get_library();
  const auto base = std::format("__ware_{}_{:016x}", kind, hash);
  auto       name = base;
  auto       io   = lib->find_io(name);
  for (unsigned suffix = 1; io; ++suffix) {
    if (auto g = io->get_graph(); g) {
      auto a = g->get_input_node().attr(attrs::ware_module);
      if (a.has() && a.get() == descriptor) {
        break;
      }
    }
    name = base + "_" + std::to_string(suffix);
    io   = lib->find_io(name);
  }
  if (!io) {
    io        = lib->create_io(name);
    auto body = io->create_graph();
    body->get_input_node().attr(attrs::ware_module).set(descriptor);
    body->get_input_node().attr(attrs::coloring_info).set(info);
    auto inner = gu::create_typed_node(*body, gu::type_op_of(node));
    gu::carry_node_attrs(node, inner);
    gu::set_color(inner, color);
    hhds::Port_id next = 1;
    for (size_t i = 0; i < edges.size(); ++i) {
      const auto&     e = edges[i];
      hhds::Pin_class driver;
      if (e.driver.is_const()) {
        driver = gu::create_const(*body, gu::const_of(e.driver));
      } else {
        auto pname = std::format("i{}", i);
        io->add_input(pname, next++);
        io->set_bits(pname, std::max(gu::bits_of(e.driver), 1));
        io->set_unsign(pname, gu::is_unsign(e.driver));
        driver = body->get_input_pin(pname);
      }
      if (e.driver.is_const()) {
        // An unsized constant is not a one-bit value. Preserve only explicit
        // source annotations; otherwise its Dlop value determines its width.
        gu::carry_pin_attrs(e.driver, driver);
      } else {
        shape(e.driver, driver);
      }
      auto sink = inner.create_sink_pin(e.sink.get_port_id());
      gu::carry_pin_attrs(e.sink, sink);
      driver.connect_sink(sink);
    }
    for (auto [pid, pin] : outputs) {
      auto pname = std::format("o{}", pid);
      io->add_output(pname, next++);
      io->set_bits(pname, output_width(pin));
      io->set_unsign(pname, gu::is_unsign(pin));
      auto driver = inner.create_driver_pin(pid);
      shape(pin, driver);
      gu::set_bits(driver, output_width(pin));
      driver.connect_sink(body->get_output_pin(pname));
    }
  }
  auto inst = gu::create_typed_node(parent, Ntype_op::Sub);
  inst.set_subnode(io);
  gu::carry_node_attrs(node, inst);
  gu::carry_srcid(node, inst);
  for (size_t i = 0; i < edges.size(); ++i) {
    if (!edges[i].driver.is_const()) {
      edges[i].driver.connect_sink(inst.create_sink_pin(io->get_input_port_id(std::format("i{}", i))));
    }
  }
  for (auto [pid, pin] : outputs) {
    auto driver = inst.create_driver_pin(io->get_output_port_id(std::format("o{}", pid)));
    shape(pin, driver);
    gu::set_bits(driver, output_width(pin));
    for (auto e : pin.out_edges()) {
      driver.connect_sink(e.sink);
    }
  }
  node.del_node();
  return io->get_graph();
}
}  // namespace

std::vector<std::shared_ptr<hhds::Graph>> build_ware_modules(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                                             Ware_policy                                      fallback) {
  std::vector<std::shared_ptr<hhds::Graph>> modules;
  for (const auto& graph : graphs) {
    if (!graph || graph->get_input_node().attr(attrs::ware_module).has()) {
      continue;
    }
    auto                                                  policy = ware_policy(*graph, fallback);
    std::vector<std::pair<hhds::Node_class, std::string>> nodes;
    for (auto node : graph->body().nodes()) {
      auto kind = family(node, policy);
      if (!kind.empty()) {
        nodes.emplace_back(node, kind);
      }
    }
    for (const auto& [node, kind] : nodes) {
      auto child = enclose(*graph, node, kind, policy);
      if (std::find(modules.begin(), modules.end(), child) == modules.end()) {
        modules.push_back(child);
      }
    }
  }
  return modules;
}
}  // namespace livehd::abc
