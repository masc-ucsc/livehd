// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "ware_module.hpp"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <climits>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <tuple>

#include "affine_amount.hpp"
#include "attr_carry.hpp"
#include "node_util.hpp"
#include "synthesis_cost.hpp"

namespace livehd::synth {
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

// A dynamic word select `v[index]` (an unpacked-array read, `v[index*W +: W]`)
// lowers to a shift whose amount is `index*stride + bias` -- e.g.
// `(select << 5) + 32` for 32-bit words. Enclosed on its own, that amount would
// be an opaque module input: the stride's constant-zero low bits, which retire
// most of the barrel, would be invisible to ABC, and the blaster's affine
// word-select lowering could never match. So the amount is kept INSIDE the
// module instead: the port carries only the narrow index and the body rebuilds
// `index*stride + bias` in front of the shift.
struct Affine_amount {
  hhds::Port_id sink_pid = 0;  // the shift's amount sink
  Affine_chain  chain;         // index -> amount (affine_amount.hpp)
};

std::optional<Affine_amount> affine_amount(const hhds::Node_class& shift) {
  const auto amount = gu::get_driver_of_sink_name(shift, "b");
  auto       chain  = affine_chain(amount);
  if (!chain) {
    return std::nullopt;
  }
  // Worth it only when the index is genuinely narrower than the amount it
  // replaces (the blaster's affine form is bounded to a 16-bit index).
  const auto& index = chain->index;
  if (gu::bits_of(index) <= 0 || gu::bits_of(index) > 16 || gu::bits_of(index) >= gu::bits_of(amount)) {
    return std::nullopt;
  }
  return Affine_amount{Ntype::get_sink_pid(gu::type_op_of(shift), "b"), std::move(*chain)};
}

// Everything that shapes the rebuilt amount, so two modules share a body only
// when their amounts are the same function of the index.
std::string describe_affine(const Affine_amount& a) {
  std::string d = std::format("affine:idx{}:{}", gu::bits_of(a.chain.index), gu::is_unsign(a.chain.index));
  for (const auto& [node, out] : a.chain.links) {
    d += std::format("|op{}:{}:{}", static_cast<int>(gu::type_op_of(node)), gu::bits_of(out), gu::is_unsign(out));
    for (const auto& in : node.inp_sorted_pins()) {
      const auto drv = in.get_driver_pin();
      d += std::format(",p{}={}", in.get_port_id(), drv.is_const() ? gu::const_of(drv).to_pyrope() : std::string{"v"});
    }
  }
  return d;
}

std::string family(const hhds::Node_class& node, const Ware_policy& policy) {
  const auto op    = gu::type_op_of(node);
  int        width = 0;
  for (auto out_pin : node.out_sorted_pins()) {  // widest driver pin; consumers do not matter
    width = std::max(width, gu::bits_of(out_pin));
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
    for (auto in_pin : node.inp_sorted_pins()) {
      if (gu::bits_of(in_pin.get_driver_pin()) > 8) {
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
  // Sink-port ascending by contract (the pin chain is kept sorted), which is the
  // whole of the order now. The sort_drivers_within_pin call that used to follow
  // is GONE: it imposed a deterministic order on the SEVERAL DRIVERS OF ONE SINK
  // PIN, and under ONE DRIVER PER SINK PIN (graph/cell.hpp) every run it sorted
  // has length one. Nothing is left to order.
  auto                                     edges  = node.inp_pins_snapshot();
  const auto                               affine = (kind == "shl" || kind == "sra") ? affine_amount(node) : std::nullopt;
  const auto is_affine_edge = [&](const hhds::Pin_class& e) { return affine && e.get_port_id() == affine->sink_pid; };
  std::map<hhds::Port_id, hhds::Pin_class> outputs;
  for (auto out_pin : node.out_sorted_pins()) {  // the node's driver pins, once each
    outputs.emplace(out_pin.get_port_id(), out_pin);
  }
  const bool     narrowable   = kind == "sum" || kind == "shl" || kind == "sra" || kind == "mult";
  // `masked_output_width` measures DRIVER PIN 0 ONLY (node_util.hpp out_width),
  // so it may only bound pin 0.  Pin 0 is NOT guaranteed to be the widest
  // realization -- abc_map.cpp:3254 and pass/lec/encode.cpp:2226 both max over
  // out_edges precisely because it is not -- so applying it to every port
  // silently truncated the others.  It also returns 0 for an unsized pin 0
  // (an expected shape, cf. abc_map.cpp:3249), which collapsed EVERY port to
  // 1 bit; hence the `demand > 0` guard, matching abc_map.cpp:3261.
  const uint64_t demand       = narrowable ? gu::masked_output_width(node) : 0;
  const auto     output_width = [&](const auto& pin) {
    const int cap = (demand > 0 && pin.get_port_id() == 0) ? static_cast<int>(demand) : INT_MAX;
    return std::max(1, std::min(gu::bits_of(pin), cap));
  };
  // The full descriptor is stored on the module; its hash is only a short name.
  // Include source mapping options so sections with different policies never
  // accidentally share an implementation selected in a different context.
  std::string descriptor = kind;
  for (auto e : edges) {
    if (is_affine_edge(e)) {
      descriptor += std::format("/i{}:{}", e.get_port_id(), describe_affine(*affine));
      continue;
    }
    descriptor += std::format("/i{}:{}:{}", e.get_port_id(), gu::bits_of(e.get_driver_pin()), gu::is_unsign(e.get_driver_pin()));
    if (e.get_driver_pin().is_const()) {
      descriptor += ":" + gu::const_of(e.get_driver_pin()).to_pyrope();
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
      if (is_affine_edge(e)) {
        auto pname = std::format("i{}", i);
        io->add_input(pname, next++);
        io->set_bits(pname, std::max(gu::bits_of(affine->chain.index), 1));
        io->set_unsign(pname, gu::is_unsign(affine->chain.index));
        const auto index = body->get_input_pin(pname);
        shape(affine->chain.index, index);
        // Clone one node of the amount cone: constants are copied, the one
        // variable operand is `variable`.
        auto clone = [&](const hhds::Node_class& src, const hhds::Pin_class& src_out, const hhds::Pin_class& variable) {
          auto dst = gu::create_typed_node(*body, gu::type_op_of(src));
          gu::carry_node_attrs(src, dst);
          gu::set_color(dst, color);
          for (const auto& in : src.inp_sorted_pins()) {
            const auto      drv = in.get_driver_pin();
            hhds::Pin_class from;
            if (drv.is_const()) {
              from = gu::create_const(*body, gu::const_of(drv));
              gu::carry_pin_attrs(drv, from);
            } else {
              from = variable;
            }
            auto sink = dst.create_sink_pin(in.get_port_id());
            gu::carry_pin_attrs(in, sink);
            from.connect_sink(sink);
          }
          auto out = dst.create_driver_pin(src_out.get_port_id());
          shape(src_out, out);
          return out;
        };
        driver = index;
        for (const auto& [src, src_out] : affine->chain.links) {
          driver = clone(src, src_out, driver);
        }
        auto sink = inner.create_sink_pin(e.get_port_id());
        gu::carry_pin_attrs(e, sink);
        driver.connect_sink(sink);
        continue;
      }
      if (e.get_driver_pin().is_const()) {
        driver = gu::create_const(*body, gu::const_of(e.get_driver_pin()));
      } else {
        auto pname = std::format("i{}", i);
        io->add_input(pname, next++);
        io->set_bits(pname, std::max(gu::bits_of(e.get_driver_pin()), 1));
        io->set_unsign(pname, gu::is_unsign(e.get_driver_pin()));
        driver = body->get_input_pin(pname);
      }
      if (e.get_driver_pin().is_const()) {
        // An unsized constant is not a one-bit value. Preserve only explicit
        // source annotations; otherwise its Dlop value determines its width.
        gu::carry_pin_attrs(e.get_driver_pin(), driver);
      } else {
        shape(e.get_driver_pin(), driver);
      }
      auto sink = inner.create_sink_pin(e.get_port_id());
      gu::carry_pin_attrs(e, sink);
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
    if (const auto drv = edges[i].get_driver_pin(); !drv.is_const()) {
      const auto src = is_affine_edge(edges[i]) ? affine->chain.index : drv;
      src.connect_sink(inst.create_sink_pin(io->get_input_port_id(std::format("i{}", i))));
    }
  }
  for (auto [pid, pin] : outputs) {
    auto driver = inst.create_driver_pin(io->get_output_port_id(std::format("o{}", pid)));
    shape(pin, driver);
    gu::set_bits(driver, output_width(pin));
    // SNAPSHOT the fan-out: connect_sink below mutates the storage this lazy
    // view walks, and `node` is not deleted until after every port is rewired.
    std::vector<hhds::Pin_class> readers;
    for (auto e : pin.out_edges()) {
      readers.push_back(e.sink);
    }
    for (const auto& reader : readers) {
      driver.connect_sink(reader);
    }
  }
  node.del_node();
  if (affine) {
    // The shift now reads the index; the parent's own copy of the amount
    // chain is dead unless something else reads it. Retire it here, amount
    // side first, so a dead Sum link never becomes an orphan ware instance.
    for (auto it = affine->chain.links.rbegin(); it != affine->chain.links.rend(); ++it) {
      auto link = it->first;
      if (link.has_out_edges()) {
        break;
      }
      link.del_node();
    }
  }
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
    auto policy = ware_policy(*graph, fallback);
    // Shifts first: a word select's amount chain (`(index << 6) + 64`) may
    // hold a wide Sum that is itself a ware family. Enclosed first, that Sum
    // became an opaque instance and the shift's affine amount was lost -- the
    // module then took the full amount as an input and built a generic barrel
    // over a wide word array (dino's ALU result select: 4x the area of the
    // word mux). Enclosing the shift clones the chain into its body, and the
    // second scan only sees the Sums that still have other readers.
    for (const bool shifts : {true, false}) {
      std::vector<std::pair<hhds::Node_class, std::string>> nodes;
      for (auto node : graph->body().nodes()) {
        auto kind = family(node, policy);
        if (!kind.empty() && (kind == "shl" || kind == "sra") == shifts) {
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
  }
  return modules;
}
}  // namespace livehd::synth
