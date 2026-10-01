// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "region_emit.hpp"

#include <algorithm>
#include <tuple>

#include "attr_carry.hpp"
#include "node_util.hpp"

namespace livehd::usyn {
namespace gu = graph_util;

Region_emission emit_logical_region(const partition::Region_body& rb, const synth::Region_blast& blast,
                                    const Stateful_region& selected, Budget& work, uint32_t max_nodes) {
  const auto invalid   = [](std::string reason) { return Region_emission{Status::invalid, std::move(reason)}; };
  const auto exhausted = [] { return Region_emission{Status::search_exhausted, "region emission budget"}; };
  if (!rb.body || !rb.src || rb.body == rb.src || !blast.logical_state || blast.status != synth::Region_blast::Status::blasted
      || !blast.source_state || !selected.source.logical_boundary || !blast.icgs.empty() || blast.has_dummy_po
      || blast.direct_native_output.size() != rb.outputs.size() || blast.all_pi_order.size() != blast.lnet.inputs().size()
      || blast.po_order.size() + blast.bbox_po.size() > blast.lnet.outputs().size()) {
    return invalid("invalid logical region boundary");
  }
  if (rb.inputs.size() > max_nodes || rb.outputs.size() > max_nodes || blast.bboxes.size() > max_nodes
      || blast.all_pi_order.size() > max_nodes || blast.bbox_po.size() > max_nodes || blast.bbox_pi.size() > max_nodes
      || !work.spend(rb.inputs.size() + rb.outputs.size() + blast.bboxes.size())) {
    return exhausted();
  }
  for (auto node : rb.body->body().nodes()) {
    (void)node;
    return invalid("logical region emission requires a fresh body");
  }
  for (const auto& port : rb.inputs) {
    if (!rb.body->get_io()->has_input(port.name)) {
      return invalid("region input is not declared on the destination");
    }
  }
  for (const auto& port : rb.outputs) {
    if (!rb.body->get_io()->has_output(port.name)) {
      return invalid("region output is not declared on the destination");
    }
  }
  auto logical = write_logical_module(selected, "__usyn_logical", work, max_nodes);
  if (!logical.module) {
    return {logical.status, std::move(logical.reason)};
  }
  auto& module = *logical.module;
  if (module.inputs.size() != blast.lnet.inputs().size() || module.outputs.size() != blast.lnet.outputs().size()) {
    return invalid("logical module boundary does not match region blast");
  }
  uint64_t planned = rb.inputs.size() + rb.outputs.size() + blast.bboxes.size() + 3 * uint64_t{blast.all_pi_order.size()};
  for (auto node : module.graph->body().nodes()) {
    (void)node;
    ++planned;
  }
  for (const auto& bb : blast.bboxes) {
    if (bb.latch.map) {
      return invalid("logical region contains a mapped latch boundary");
    }
    planned += bb.const_ins.size() + bb.native_ins.size() + bb.outs.size() + 2 * bb.fit_native_ins.size();
    for (const auto& in : bb.ins) {
      if (in.bits <= 0) {
        return invalid("native boundary has an invalid input width");
      }
      planned += in.bits + 2;
    }
  }
  uint64_t output_lanes = 0;
  for (size_t i = 0; i < rb.outputs.size(); ++i) {
    if (!blast.direct_native_output[i]) {
      output_lanes += std::max(1, rb.outputs[i].bits);
    }
  }
  if (output_lanes != blast.po_order.size()) {
    return invalid("incomplete region output bit table");
  }
  planned += output_lanes + 2 * rb.outputs.size();
  if (planned > max_nodes || !work.spend(planned)) {
    return exhausted();
  }
  auto&                                                 body    = *rb.body;
  auto*                                                 library = body.get_io()->get_library();
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> native;
  std::vector<hhds::Node_class>                         boundaries;
  const auto                                            constant = [&](const Dlop& value) { return gu::create_const(body, value); };
  const auto                                            clone_node = [&](hhds::Node_class old, hhds::Graph& source) {
    auto neo = gu::create_typed_node(body, gu::type_op_of(old));
    gu::carry_node_attrs(old, neo);
    gu::carry_srcid(old, neo, &source, library);
    return neo;
  };
  for (const auto& input : rb.inputs) {
    if (!work.spend() || input.src_driver.is_invalid()) {
      return input.src_driver.is_invalid() ? invalid("missing region input driver") : exhausted();
    }
    auto pin = body.get_input_pin(input.name);
    gu::set_bits(pin, input.bits);
    input.sign ? gu::set_sign(pin) : gu::set_unsign(pin);
    if (!native.emplace(input.src_driver, pin).second) {
      return invalid("duplicate region input driver");
    }
  }
  for (const auto& bb : blast.bboxes) {
    if (!work.spend()) {
      return exhausted();
    }
    auto node = clone_node(bb.node, *rb.src);
    if (bb.op == Ntype_op::Sub) {
      auto child = partition::resolve_or_clone_subdef(library, bb.node);
      if (!child) {
        return invalid("native child definition is missing from output library");
      }
      if (auto loop = bb.node.subnode_loop()) {
        node.set_subnode(child, *loop);
      } else {
        node.set_subnode(child);
      }
    }
    if (blast.native_comb_logic.contains(bb.node)) {
      node.attr(attrs::native_comb_boundary).set({});
    }
    boundaries.push_back(node);
    for (const auto& out : bb.outs) {
      auto pin = node.create_driver_pin(out.port_id);
      gu::carry_pin_attrs(out.src_pin, pin);
      gu::set_bits(pin, out.bits);
      out.sign ? gu::set_sign(pin) : gu::set_unsign(pin);
      native.emplace(out.src_pin, pin);
    }
    if (bb.op == Ntype_op::Sub) {
      for (const auto& decl : node.get_subnode_io()->get_output_pin_decls()) {
        auto pin = node.create_driver_pin(decl.port_id);
        if (gu::bits_of(pin) == 0) {
          gu::set_bits(pin, decl.bits ? decl.bits : 1);
        }
      }
    }
    if (bb.node.is_loop_subnode()) {
      for (const auto& carry : bb.node.subnode_group().carries()) {
        node.create_driver_pin(carry.output_port()).connect_sink(node.create_sink_pin(carry.input_port()));
      }
    }
  }
  const auto resolve = [&](hhds::Pin_class pin) -> hhds::Pin_class {
    if (pin.is_const()) {
      return constant(gu::const_of(pin));
    }
    auto it = native.find(pin);
    return it == native.end() ? hhds::Pin_class{} : it->second;
  };
  // Extract only demanded bits; never allocate a vector sized to a wide
  // native output bus. Shift before masking so even a high bit uses only a
  // small integer offset and a one-bit mask. The mask is necessary: a width
  // hint on SRA alone does not truncate its value on graph save/recompile.
  absl::flat_hash_map<std::pair<hhds::Pin_class, int>, hhds::Pin_class> slices;
  const auto bit = [&](hhds::Pin_class pin, int index) -> hhds::Pin_class {
    if (index < 0 || pin.is_invalid()) {
      return {};
    }
    auto value = resolve(pin);
    if (value.is_invalid()) {
      return {};
    }
    // Only an unsigned one-bit pin is already the {0,1} driver the logical
    // writer assumes (it inverts with Xor(x, 1)); a signed one-bit true is -1.
    if (index == 0 && gu::bits_of(value) == 1 && gu::is_unsign(value)) {
      return value;
    }
    auto [it, fresh] = slices.try_emplace({pin, index});
    if (fresh) {
      if (index != 0) {
        auto shift = gu::create_typed_node(body, Ntype_op::SRA);
        value.connect_sink(gu::setup_sink_by_name(shift, "a"));
        constant(*Dlop::create_integer(index)).connect_sink(gu::setup_sink_by_name(shift, "b"));
        const int  width = gu::bits_of(value);
        const bool sign  = !gu::is_unsign(value);
        value            = shift.create_driver_pin(0);
        if (width > 0) {
          gu::set_bits(value, std::max(1, width - index));
        }
        sign ? gu::set_sign(value) : gu::set_unsign(value);
      }
      auto select = gu::create_get_mask(body, value, 0, 1);
      it->second  = select.create_driver_pin(0);
      gu::set_ubits(it->second, 1);
    }
    return it->second;
  };
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> logical_pins;
  for (size_t i = 0; i < blast.all_pi_order.size(); ++i) {
    if (!work.spend()) {
      return exhausted();
    }
    const auto&     origin = blast.all_pi_order[i];
    hhds::Pin_class value;
    if (origin.kind == synth::Pi_kind::region_input && origin.index < blast.pi_order.size()) {
      const auto [port, lane] = blast.pi_order[origin.index];
      if (port < rb.inputs.size()) {
        value = bit(rb.inputs[port].src_driver, lane);
      }
    } else if (origin.kind == synth::Pi_kind::bbox_output && origin.index < blast.bbox_pi.size()) {
      const auto [box, port, lane] = blast.bbox_pi[origin.index];
      if (box >= 0 && static_cast<size_t>(box) < blast.bboxes.size() && port >= 0
          && static_cast<size_t>(port) < blast.bboxes[box].outs.size()) {
        value = bit(blast.bboxes[box].outs[port].src_pin, lane);
      }
    }
    if (value.is_invalid()) {
      return invalid("unresolved logical region input bit");
    }
    logical_pins.emplace(module.graph->get_input_pin(module.inputs[i]), value);
  }
  std::vector<std::pair<hhds::Node_class, hhds::Node_class>> copied;
  for (auto old : module.graph->body().nodes()) {
    if (!work.spend()) {
      return exhausted();
    }
    if (Ntype::has_multiple_driver_pins(gu::type_op_of(old))) {
      return invalid("unexpected multi-output cell in logical CMOS module");
    }
    auto neo     = clone_node(old, *module.graph);
    auto old_pin = old.get_driver_pin(0), new_pin = neo.create_driver_pin(0);
    gu::carry_pin_attrs(old_pin, new_pin);
    logical_pins.emplace(old_pin, new_pin);
    copied.emplace_back(old, neo);
  }
  const auto logical_driver = [&](hhds::Pin_class pin) -> hhds::Pin_class {
    if (pin.is_const()) {
      return constant(gu::const_of(pin));
    }
    auto it = logical_pins.find(pin);
    return it == logical_pins.end() ? hhds::Pin_class{} : it->second;
  };
  for (const auto& [old, neo] : copied) {
    for (const auto& in : gu::inp_sink_drivers(old)) {
      if (!work.spend()) {
        return exhausted();
      }
      auto value = logical_driver(in.driver);
      if (value.is_invalid()) {
        return invalid("unresolved logical cell input");
      }
      auto sink = neo.create_sink_pin(in.sink.get_port_id());
      gu::carry_pin_attrs(in.sink, sink);
      value.connect_sink(sink);
    }
  }
  std::vector<hhds::Pin_class> outputs;
  for (const auto& name : module.outputs) {
    auto value = logical_driver(module.graph->get_output_pin(name).get_driver_pin());
    if (value.is_invalid()) {
      return invalid("unresolved logical module output");
    }
    outputs.push_back(value);
  }
  const auto assemble = [&](const std::vector<hhds::Pin_class>& bits, bool sign) -> hhds::Pin_class {
    if (bits.empty() || std::any_of(bits.begin(), bits.end(), [](auto pin) { return pin.is_invalid(); })) {
      return {};
    }
    auto value = bits.front();
    if (bits.size() > 1) {
      auto concat = gu::create_typed_node(body, Ntype_op::Concat);
      for (size_t b = 0; b < bits.size(); ++b) {
        const auto pid = static_cast<hhds::Port_id>(2 * (bits.size() - 1 - b));
        constant(*Dlop::create_integer(1)).connect_sink(concat.create_sink_pin(pid + 1));
        bits[b].connect_sink(concat.create_sink_pin(pid));
      }
      value = concat.create_driver_pin(0);
      gu::set_ubits(value, bits.size());
    }
    if (sign) {
      auto sext = gu::create_typed_node(body, Ntype_op::Sext);
      value.connect_sink(gu::setup_sink_by_name(sext, "a"));
      constant(*Dlop::create_integer(bits.size())).connect_sink(gu::setup_sink_by_name(sext, "b"));
      value = sext.create_driver_pin(0);
      gu::set_bits(value, bits.size());
      gu::set_sign(value);
    }
    return value;
  };
  std::vector<std::vector<std::vector<hhds::Pin_class>>> box_inputs(blast.bboxes.size());
  for (size_t b = 0; b < blast.bboxes.size(); ++b) {
    for (const auto& input : blast.bboxes[b].ins) {
      box_inputs[b].emplace_back(input.bits);
    }
  }
  for (size_t i = 0; i < blast.bbox_po.size(); ++i) {
    for (const auto& target : blast.bbox_po[i]) {
      if (target.bx < 0 || static_cast<size_t>(target.bx) >= box_inputs.size() || target.input < 0
          || static_cast<size_t>(target.input) >= box_inputs[target.bx].size() || target.bit < 0
          || static_cast<size_t>(target.bit) >= box_inputs[target.bx][target.input].size()) {
        return invalid("invalid native boundary output correspondence");
      }
      auto& lane = box_inputs[target.bx][target.input][target.bit];
      if (!lane.is_invalid()) {
        return invalid("duplicate native boundary input bit");
      }
      lane = outputs[blast.po_order.size() + i];
    }
  }
  for (size_t b = 0; b < blast.bboxes.size(); ++b) {
    const auto& box  = blast.bboxes[b];
    auto        node = boundaries[b];
    const auto  wire = [&](int pid, hhds::Pin_class value) {
      if (value.is_invalid()) {
        return false;
      }
      value.connect_sink(node.create_sink_pin(pid));
      return true;
    };
    for (const auto& [pid, pin] : box.const_ins) {
      if (!wire(pid, resolve(pin))) {
        return invalid("unresolved native constant input");
      }
    }
    for (const auto& [pid, pin] : box.native_ins) {
      if (!wire(pid, resolve(pin))) {
        return invalid("unresolved native bus input");
      }
    }
    for (const auto& [pid, pin, width, sign] : box.fit_native_ins) {
      auto value = resolve(pin);
      if (value.is_invalid() || width <= 0) {
        return invalid("unresolved native width conversion");
      }
      auto mask = gu::create_get_mask(body, value, 0, width);
      value     = mask.create_driver_pin(0);
      gu::set_bits(value, width);
      sign ? gu::set_sign(value) : gu::set_unsign(value);
      wire(pid, value);
    }
    for (size_t i = 0; i < box.ins.size(); ++i) {
      if (!work.spend(box.ins[i].bits + 2)) {
        return exhausted();
      }
      if (!wire(box.ins[i].port_id, assemble(box_inputs[b][i], box.ins[i].sign))) {
        return invalid("incomplete native boundary input");
      }
    }
  }
  std::vector<std::vector<hhds::Pin_class>> ports(rb.outputs.size());
  for (size_t i = 0; i < rb.outputs.size(); ++i) {
    if (!blast.direct_native_output[i]) {
      ports[i].resize(std::max(1, rb.outputs[i].bits));
    }
  }
  for (size_t i = 0; i < blast.po_order.size(); ++i) {
    const auto [port, lane] = blast.po_order[i];
    if (port >= ports.size() || lane < 0 || static_cast<size_t>(lane) >= ports[port].size() || !ports[port][lane].is_invalid()) {
      return invalid("invalid region output correspondence");
    }
    ports[port][lane] = outputs[i];
  }
  for (size_t i = 0; i < rb.outputs.size(); ++i) {
    if (!work.spend(ports[i].size() + 1)) {
      return exhausted();
    }
    const auto& port  = rb.outputs[i];
    auto        value = blast.direct_native_output[i] ? resolve(port.src_driver) : assemble(ports[i], port.sign);
    if (value.is_invalid()) {
      return invalid("incomplete region output");
    }
    value.connect_sink(body.get_output_pin(port.name));
  }
  return {Status::feasible, {}};
}

}  // namespace livehd::usyn
