//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <random>
#include <string>

#include "ann_place.hpp"
#include "node_type_area.hpp"
#include "writearea.hpp"

void Pass_fplan_writearea::setup() {
  auto w = Eprp_method("pass.fplan.writearea",
                       "randomly generates aspect ratio and area for nodes, within bounds",
                       &Pass_fplan_writearea::pass);

  w.add_label_optional("min_aspect_ratio", "minimum aspect ratio of nodes in lgraph", "1.0");
  w.add_label_optional("max_aspect_ratio", "maximum aspect ratio of nodes in lgraph", "50.0");
  w.add_label_optional("min_area", "minimum area of nodes in lgraph (mm²)", "1.0");
  w.add_label_optional("max_area", "maximum area of nodes in lgraph (mm²)", "4.0");

  register_pass(w);
}

Pass_fplan_writearea::Pass_fplan_writearea(const Eprp_var& var) : Pass("pass.fplan", var) {
  float min_asp  = std::stof(var.get("min_aspect_ratio").data());
  float max_asp  = std::stof(var.get("max_aspect_ratio").data());
  float min_area = std::stof(var.get("min_area").data());
  float max_area = std::stof(var.get("max_area").data());

  std::default_random_engine            g;
  std::uniform_real_distribution<float> rd(min_area, max_area);

  // write all areas with clamped randoms
  const uint8_t start = static_cast<uint8_t>(Ntype_op::Invalid) + 1;
  const uint8_t end   = static_cast<uint8_t>(Ntype_op::Last_invalid);

  Ntype_area na(path);

  fmt::print("type\tarea\tminasp\tmaxasp\n");
  fmt::print("------------------------------\n");
  for (uint8_t op = start; op < end; op++) {
    const Ntype_op nop = static_cast<Ntype_op>(op);
    if (Ntype::is_synthesizable(nop)) {
      float area = rd(g);
      na.set_dim(nop, {min_asp, max_asp, area});
      fmt::print("{}\t{:.3f}\t{:.3f}\t{:.3f}\n", Ntype::get_name(nop), area, min_asp, max_asp);
    }
  }
}

void Pass_fplan_writearea::pass(Eprp_var& var) { Pass_fplan_writearea w(var); }