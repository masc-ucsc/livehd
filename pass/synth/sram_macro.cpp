// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sram_macro.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <string>

#include "diag.hpp"
#include "graph/attrs.hpp"
#include "graph/cell.hpp"
#include "memory_module.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

namespace livehd::synth {
namespace {

struct Pin {
  std::string name;
  hhds::Port_id id = 0;
  uint32_t    bits = 1;
  bool        unsign = true;
  bool        input = true;
};

// What the enclosing module's Memory says; absent fields keep the values of a plain 1R1W array.
struct Shape {
  uint64_t            bits = 0, size = 0, wensize = 1;
  std::vector<bool>   read_sync;  // per read port, in port order
  int                 writes = 0;
  std::string         refuse;     // why the memory has no macro realization
};

Shape shape_of(const hhds::Node_class& memory) {
  Shape s;
  const std::optional<hhds::Node_class> mem = memory;
  if (gu::memory_image_of(memory)) {
    s.refuse = "loader image";
    return s;
  }
  std::map<int, int> rdport;  // port block -> 0 write, 1 async read, 2 sync read
  for (const auto& in_pin : mem->inp_sorted_pins()) {
    const auto drv = in_pin.get_driver_pin();
    const int  pid = static_cast<int>(in_pin.get_port_id()), off = pid % Ntype::Memory_port_stride;
    const auto cst = [&]() -> std::optional<int64_t> {
      if (drv.is_invalid() || !drv.is_const() || gu::const_of(drv).has_unknowns() || !gu::const_of(drv).is_integer()) {
        return std::nullopt;
      }
      return gu::const_of(drv).to_just_i64();
    };
    switch (off) {
      case 1: s.bits = static_cast<uint64_t>(cst().value_or(0)); break;
      case 9: s.size = static_cast<uint64_t>(cst().value_or(0)); break;
      case 8: s.wensize = static_cast<uint64_t>(std::max<int64_t>(cst().value_or(1), 1)); break;
      case 5:
        if (cst().value_or(1) != 0) {
          s.refuse = "forwarding between ports";
        }
        break;
      case 10: rdport[pid / Ntype::Memory_port_stride] = static_cast<int>(cst().value_or(1)); break;
      case 11:
        if (!cst() || *cst() != 0) {
          s.refuse = "power-on contents";
        }
        break;
      case 12: case 14: s.refuse = "whole-array update or reset"; break;
      case 7:
        if (cst().value_or(0) == Ntype::Memory_type_array) {
          s.refuse = "combinational array";
        }
        break;
      default: break;
    }
  }
  for (const auto& out_pin : mem->out_sorted_pins()) {
    if (out_pin.get_port_id() == Ntype::Memory_readall_pid) {
      s.refuse = "whole-array read";
    }
  }
  for (const auto& [block, kind] : rdport) {
    if (kind == 0) {
      ++s.writes;
    } else {
      s.read_sync.push_back(kind == Ntype::Memory_rdport_sync);
    }
  }
  if (s.refuse.empty() && (s.bits == 0 || s.size == 0)) {
    s.refuse = "unknown width or depth";
  }
  return s;
}

// "rd_addr_3" -> role "rd_addr", port 3
std::pair<std::string, int> role_of(const std::string& name) {
  const auto cut = name.rfind('_');
  if (cut == std::string::npos || cut + 1 >= name.size() || !std::isdigit(static_cast<unsigned char>(name[cut + 1]))) {
    return {name, -1};
  }
  return {name.substr(0, cut), std::atoi(name.c_str() + cut + 1)};
}

double snap_up(double v, double grid) { return grid > 0 ? std::ceil(v / grid - 1e-9) * grid : v; }

struct Geometry {
  double w = 0, h = 0;
};

Geometry geometry_of(const Sram_tech& t, uint64_t bits, uint64_t size, int ports, size_t in_pins, size_t out_pins, size_t clocks) {
  const double stored = static_cast<double>(bits) * static_cast<double>(size);
  const double area   = stored * t.um2_per_bit * (1.0 + t.port_growth * std::max(0, ports - 2));
  double       h      = std::sqrt(area * t.aspect);
  double       w      = area / h;
  const double off_lr = 6 * t.lr_pitch_um, off_tb = 6 * t.tb_pitch_um;
  // pins: inputs split over the left and right edges, outputs and clocks over the top and bottom
  h = std::max(h, 2 * off_lr + std::ceil(static_cast<double>(in_pins) / 2) * t.lr_pitch_um);
  w = std::max(w, 2 * off_tb + std::ceil(static_cast<double>(out_pins + clocks) / 2) * t.tb_pitch_um);
  return {snap_up(std::max(w, t.min_dim_um), t.snap_w_um), snap_up(std::max(h, t.min_dim_um), t.snap_h_um)};
}

std::string lef_of(const std::string& name, const std::vector<Pin>& pins, const Geometry& g, const Sram_tech& t) {
  std::string out = std::format("MACRO {}\n  FOREIGN {} 0 0 ;\n  SYMMETRY X Y R90 ;\n  SIZE {:.3f} BY {:.3f} ;\n  CLASS BLOCK ;\n", name, name, g.w, g.h);
  const double off_lr = 6 * t.lr_pitch_um, off_tb = 6 * t.tb_pitch_um;
  size_t       left = 0, right = 0, top = 0, bottom = 0;
  const auto   emit = [&](const std::string& pin, bool input, const std::string& layer, const std::string& rect) {
    out += std::format("  PIN {}\n    DIRECTION {} ;\n    USE SIGNAL ;\n    SHAPE ABUTMENT ;\n    PORT\n      LAYER {} ;\n      RECT {} ;\n    END\n  END {}\n",
                       pin, input ? "INPUT" : "OUTPUT", layer, rect, pin);
  };
  for (const auto& p : pins) {
    const bool clock = p.input && p.name.starts_with("clk");
    for (uint32_t b = 0; b < p.bits; ++b) {
      const auto pname = p.bits > 1 ? std::format("{}[{}]", p.name, b) : p.name;
      if (p.input && !clock) {
        const bool   on_left = (left + right) % 2 == 0;
        const double y       = off_lr + static_cast<double>(on_left ? left++ : right++) * t.lr_pitch_um;
        const double x0      = on_left ? 0.0 : g.w - t.lr_len_um, x1 = on_left ? t.lr_len_um : g.w;
        emit(pname, true, t.lr_layer, std::format("{:.3f} {:.3f} {:.3f} {:.3f}", x0, y, x1, y + t.lr_width_um));
      } else {
        const bool   on_top = (top + bottom) % 2 == 0;
        const double x      = off_tb + static_cast<double>(on_top ? top++ : bottom++) * t.tb_pitch_um;
        const double y0     = on_top ? g.h - t.tb_len_um : 0.0, y1 = on_top ? g.h : t.tb_len_um;
        emit(pname, p.input, t.tb_layer, std::format("{:.3f} {:.3f} {:.3f} {:.3f}", x, y0, x + t.tb_width_um, y1));
      }
    }
  }
  for (const char* rail : {"VSS", "VDD"}) {
    const bool vdd = std::string(rail) == "VDD";
    out += std::format("  PIN {}\n    DIRECTION INOUT ;\n    USE {} ;\n    PORT\n      LAYER {} ;\n", rail, vdd ? "POWER" : "GROUND", t.supply_layer);
    for (double y = t.supply_pitch_um * (vdd ? 1.5 : 0.5); y + t.supply_width_um < g.h; y += 2 * t.supply_pitch_um) {
      out += std::format("      RECT 0.216 {:.3f} {:.3f} {:.3f} ;\n", y, g.w - 0.216, y + t.supply_width_um);
    }
    out += std::format("    END\n  END {}\n", rail);
  }
  out += std::format("  OBS\n    LAYER M1 ;\n    RECT 0 0 {:.3f} {:.3f} ;\n    LAYER M2 ;\n    RECT 0 0 {:.3f} {:.3f} ;\n    LAYER M3 ;\n    RECT 0 0 {:.3f} {:.3f} ;\n    LAYER M4 ;\n    RECT 0 0 {:.3f} {:.3f} ;\n  END\nEND {}\n\n",
                     g.w, g.h, g.w, g.h, g.w, g.h, g.w, g.h, name);
  return out;
}

std::string table(const std::string& tpl, double v) {
  return std::format("(\"{}\") {{\n                index_1 (\"0.009, 0.227\");\n                index_2 (\"0.005, 0.5\");\n                values ( \\\n                  \"{:.3f}, {:.3f}\", \\\n                  \"{:.3f}, {:.3f}\" \\\n                )\n            }}", tpl, v, v, v, v);
}

std::string liberty_of(const std::string& name, const std::vector<Pin>& pins, const Shape& s, const Geometry& g, const Sram_tech& t,
                       const std::map<std::string, bool>& sync_out) {
  const uint32_t aw = static_cast<uint32_t>(std::max<uint64_t>(1, static_cast<uint64_t>(std::ceil(std::log2(static_cast<double>(s.size))))));
  std::string    o;
  o += std::format("library({}) {{\n    technology (cmos);\n    delay_model : table_lookup;\n    revision : 1.0;\n    comment : \"generated SRAM macro (synth.sram)\";\n"
                   "    time_unit : \"1ns\";\n    voltage_unit : \"1V\";\n    current_unit : \"1uA\";\n    leakage_power_unit : \"1uW\";\n    nom_process : 1;\n"
                   "    nom_temperature : 25.000;\n    nom_voltage : {};\n    capacitive_load_unit (1,pf);\n    pulling_resistance_unit : \"1kohm\";\n"
                   "    operating_conditions(tt_1.0_25.0) {{\n        process : 1;\n        temperature : 25.000;\n        voltage : {};\n        tree_type : balanced_tree;\n    }}\n"
                   "    default_cell_leakage_power : 0;\n    default_fanout_load : 1;\n    default_inout_pin_cap : 0.0;\n    default_input_pin_cap : 0.0;\n    default_output_pin_cap : 0.0;\n"
                   "    default_max_transition : 0.227;\n    default_operating_conditions : tt_1.0_25.0;\n    default_leakage_power_density : 0.0;\n"
                   "    slew_derate_from_library : 1.000;\n    slew_lower_threshold_pct_fall : 20.000;\n    slew_upper_threshold_pct_fall : 80.000;\n"
                   "    slew_lower_threshold_pct_rise : 20.000;\n    slew_upper_threshold_pct_rise : 80.000;\n    input_threshold_pct_fall : 50.000;\n"
                   "    input_threshold_pct_rise : 50.000;\n    output_threshold_pct_fall : 50.000;\n    output_threshold_pct_rise : 50.000;\n",
                   name, t.voltage, t.voltage);
  o += std::format("    lu_table_template({0}_delay) {{\n        variable_1 : input_net_transition;\n        variable_2 : total_output_net_capacitance;\n"
                   "            index_1 (\"1000, 1001\");\n            index_2 (\"1000, 1001\");\n    }}\n"
                   "    lu_table_template({0}_slew) {{\n        variable_1 : total_output_net_capacitance;\n            index_1 (\"1000, 1001\");\n    }}\n"
                   "    lu_table_template({0}_constraint) {{\n        variable_1 : related_pin_transition;\n        variable_2 : constrained_pin_transition;\n"
                   "            index_1 (\"1000, 1001\");\n            index_2 (\"1000, 1001\");\n    }}\n",
                   name);
  std::set<uint32_t> widths;
  for (const auto& p : pins) {
    if (p.bits > 1) {
      widths.insert(p.bits);
    }
  }
  for (const auto w : widths) {
    o += std::format("    type ({0}_b{1}) {{\n        base_type : array ;\n        data_type : bit ;\n        bit_width : {1};\n        bit_from : {2};\n        bit_to : 0 ;\n        downto : true ;\n    }}\n",
                     name, w, w - 1);
  }
  o += std::format("cell({}) {{\n    area : {:.3f};\n    interface_timing : true;\n    memory() {{\n        type : ram;\n        address_width : {};\n        word_width : {};\n    }}\n",
                   name, g.w * g.h, aw, s.bits);
  const auto head = [&](const Pin& p) {
    return std::format("    {}({}) {{\n", p.bits > 1 ? "bus" : "pin", p.name) + (p.bits > 1 ? std::format("        bus_type : {}_b{};\n", name, p.bits) : std::string{});
  };
  const auto constraint = [&](const char* type) {
    std::string c = std::format("        timing() {{\n            related_pin : clk;\n            timing_type : {} ;\n", type);
    for (const char* edge : {"rise_constraint", "fall_constraint"}) {
      c += std::format("            {}({}_constraint) {{\n                index_1 (\"0.009, 0.227\");\n                index_2 (\"0.009, 0.227\");\n"
                       "                values ( \\\n                  \"{:.3f}, {:.3f}\", \\\n                  \"{:.3f}, {:.3f}\" \\\n                )\n            }}\n",
                       edge, name, std::string(type) == "setup_rising" ? t.setup_ns : t.hold_ns, std::string(type) == "setup_rising" ? t.setup_ns : t.hold_ns,
                       std::string(type) == "setup_rising" ? t.setup_ns : t.hold_ns, std::string(type) == "setup_rising" ? t.setup_ns : t.hold_ns);
    }
    return c + "        }\n";
  };
  // the read port an async output belongs to: its address pin is the one whose arcs reach it
  for (const auto& p : pins) {
    const auto [role, port] = role_of(p.name);
    if (p.input && p.name.starts_with("clk")) {
      o += head(p) + std::format("        direction : input;\n        capacitance : {};\n        clock : true;\n        min_period : {} ;\n    }}\n",
                                 t.clk_cap_pf, t.min_period_ns);
    } else if (p.input) {
      const bool comb_read = (role == "rd_addr" || role == "rd_enable") && port >= 0 && !(static_cast<size_t>(port) < s.read_sync.size() && s.read_sync[static_cast<size_t>(port)]);
      o += head(p) + std::format("        direction : input;\n        capacitance : {};\n", t.pin_cap_pf);
      if (!comb_read) {
        o += constraint("setup_rising") + constraint("hold_rising");
      }
      o += "    }\n";
    } else {
      const bool sync = sync_out.at(p.name);
      o += head(p) + std::format("        direction : output;\n        max_capacitance : 0.5;\n        memory_read() {{\n            address : rd_addr_{};\n        }}\n", port);
      const auto arc = [&](const std::string& related, const char* type) {
        std::string a = std::format("        timing() {{\n            related_pin : \"{}\" ;\n", related);
        if (type) {
          a += std::format("            timing_type : {};\n", type);
        }
        a += "            timing_sense : non_unate;\n";
        for (const char* edge : {"cell_rise", "cell_fall"}) {
          a += std::format("            {}{}\n", edge, table(name + "_delay", t.clk_to_q_ns));
        }
        for (const char* edge : {"rise_transition", "fall_transition"}) {
          a += std::format("            {}({}_slew) {{\n                index_1 (\"0.005, 0.5\");\n                values (\"0.009, 0.227\")\n            }}\n", edge, name);
        }
        return a + "        }\n";
      };
      if (sync) {
        o += arc("clk", "rising_edge");
      } else {
        std::string related;
        for (const auto& q : pins) {
          if (q.input && q.name == std::format("rd_addr_{}", port)) {
            for (uint32_t b = 0; b < q.bits; ++b) {
              related += (related.empty() ? "" : " ") + (q.bits > 1 ? std::format("{}[{}]", q.name, b) : q.name);
            }
          }
        }
        o += arc(related, nullptr);
      }
      o += "    }\n";
    }
  }
  return o + "}\n}\n";
}

Shape shape_of_module(const hhds::Graph& g) {
  std::optional<hhds::Node_class> mem;
  for (const auto node : g.body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Memory) {
      if (mem) {
        Shape s;
        s.refuse = "several memories in one module";
        return s;
      }
      mem = node;
    }
  }
  if (!mem) {
    Shape s;
    s.refuse = "no memory in the module";
    return s;
  }
  return shape_of(*mem);
}

}  // namespace

std::string sram_macro_refusal(const hhds::Node_class& mem) { return shape_of(mem).refuse; }

Sram_result macro_memories(const std::vector<std::shared_ptr<hhds::Graph>>& input, const Sram_tech& tech) {
  Sram_result result;
  std::set<std::string> done;
  // a memory still a node of its region (not yet in a module of its own) gets its module first
  std::vector<std::shared_ptr<hhds::Graph>> graphs = input;
  for (const auto& graph : input) {
    if (!graph || graph->get_input_node().attr(attrs::memory_module).has()) {
      continue;
    }
    std::vector<hhds::Node_class> memories;
    for (const auto node : graph->body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Memory) {
        memories.push_back(node);
      }
    }
    for (const auto& mem : memories) {
      const auto shape = shape_of(mem);
      if (!shape.refuse.empty() || shape.bits * shape.size < tech.min_bits) {
        result.kept.emplace_back(std::string(graph->get_name()) + "/" + std::string(gu::default_instance_name(mem)),
                                 shape.refuse.empty() ? "below synth.sram.min_bits" : shape.refuse);
        continue;
      }
      graphs.push_back(enclose_native_memory(*graph, mem));
    }
  }
  for (const auto& graph : graphs) {
    if (!graph || !graph->get_input_node().attr(attrs::memory_module).has()) {
      continue;
    }
    const std::string name{graph->get_name()};
    if (!done.insert(name).second) {
      continue;
    }
    const auto shape = shape_of_module(*graph);
    if (shape.refuse == "no memory in the module") {
      continue;  // a lowered (flops) memory module
    }
    if (!shape.refuse.empty()) {
      result.kept.emplace_back(name, shape.refuse);
      continue;
    }
    if (shape.bits * shape.size < tech.min_bits) {
      result.kept.emplace_back(name, "below synth.sram.min_bits");
      continue;
    }
    auto io = graph->get_io();
    if (io == nullptr) {
      result.kept.emplace_back(name, "no interface");
      continue;
    }
    std::vector<Pin> pins;
    for (const auto& d : io->get_input_pin_decls()) {
      pins.push_back({d.name, d.port_id, std::max<uint32_t>(d.bits, 1), d.unsign, true});
    }
    for (const auto& d : io->get_output_pin_decls()) {
      pins.push_back({d.name, d.port_id, std::max<uint32_t>(d.bits, 1), d.unsign, false});
    }
    // read ports are numbered in port order: rd_dout_<k> is the k-th read, sync or not
    std::map<std::string, bool> sync_out;
    bool                        ok = true;
    size_t                      in_bits = 0, out_bits = 0, clocks = 0;
    for (const auto& p : pins) {
      if (p.input) {
        if (p.name.starts_with("clk")) {
          ++clocks;
        } else {
          in_bits += p.bits;
        }
        continue;
      }
      const auto [role, port] = role_of(p.name);
      if (role != "rd_dout" || port < 0 || static_cast<size_t>(port) >= shape.read_sync.size()) {
        ok = false;
        break;
      }
      sync_out[p.name] = shape.read_sync[static_cast<size_t>(port)];
      out_bits += p.bits;
    }
    if (!ok || clocks != 1) {
      result.kept.emplace_back(name, "unexpected interface");
      continue;
    }
    const int  rd = static_cast<int>(shape.read_sync.size());
    const auto g  = geometry_of(tech, shape.bits, shape.size, rd + shape.writes, in_bits, out_bits, clocks);
    result.liberties.emplace_back(name, liberty_of(name, pins, shape, g, tech, sync_out));
    result.lef += lef_of(name, pins, g, tech);
    result.macros.push_back({name, shape.bits, shape.size, rd, shape.writes, g.w, g.h});

    // the module becomes the macro: no body, the same pins; a synchronous read is a registered boundary
    auto* lib = io->get_library();
    lib->delete_graph(io->get_gid());
    for (const auto& p : pins) {
      if (p.input) {
        continue;
      }
      io->delete_output(p.name);
      io->add_output(p.name, p.id, sync_out.at(p.name));
      io->set_bits(p.name, p.bits);
      io->set_unsign(p.name, p.unsign);
    }
  }
  return result;
}

}  // namespace livehd::synth
