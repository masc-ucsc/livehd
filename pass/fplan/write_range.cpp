//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "write_range.hpp"

#include <random>
#include <string>
#include <vector>

#include "ann_place.hpp"
#include "node_type_area.hpp"

void Pass_fplan_write_range::setup() {
  auto wr = Eprp_method("pass.fplan.write_range",
                        "randomly generates aspect ratio and area for nodes, within bounds",
                        &Pass_fplan_write_range::pass);

  wr.add_label_optional("min_ar", "minimum aspect ratio of nodes in lgraph", "1.0");
  // aspect ratio is super high to try and avoid overlapping floorplans as much as possible, but
  // can be reduced if required
  wr.add_label_optional("max_ar", "maximum aspect ratio of nodes in lgraph", "50.0");
  wr.add_label_optional("min_area", "minimum area of nodes in lgraph (mm²)", "1.0");
  wr.add_label_optional("max_area", "maximum area of nodes in lgraph (mm²)", "4.0");

  wr.add_label_optional("path", "where to write area information", "lgdb");

  register_pass(wr);
}

Pass_fplan_write_range::Pass_fplan_write_range(const Eprp_var& var) : Pass("pass.fplan", var) {
#if 0
  std::default_random_engine            g;
  std::uniform_real_distribution<float> rd(min_area, max_area);

  // write all areas with clamped randoms
  const uint8_t start = static_cast<uint8_t>(Ntype_op::Invalid) + 1;
  const uint8_t end   = static_cast<uint8_t>(Ntype_op::Last_invalid);


  float min_ar   = std::stof(var.get("min_ar").to_s());
  float max_ar   = std::stof(var.get("max_ar").to_s());
  float min_area = std::stof(var.get("min_area").to_s());
  float max_area = std::stof(var.get("max_area").to_s());

  // FIXME: See docs/cleanup.md node_type_area TODO to re-enable this code
  Ntype_area na(path);

  for (uint8_t op = start; op < end; op++) {
    const Ntype_op nop = static_cast<Ntype_op>(op);
    if (Ntype::is_synthesizable(nop)) {
      float area = rd(g);
      Ntype_area::dim dim{min_ar, max_ar, area};
      na.set_dim(nop, dim);
      // fmt::print("{}\t{:.3f}\t{:.3f}\t{:.3f}\n", Ntype::get_name(nop), area, min_ar, max_ar);
    }
  }
#endif
}

void Pass_fplan_write_range::pass(Eprp_var& var) { Pass_fplan_write_range w(var); }
