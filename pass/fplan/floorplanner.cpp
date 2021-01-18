#include "floorplanner.hpp"

#include "core/ann_place.hpp"
#include "core/lgedgeiter.hpp"

void Lhd_floorplanner::create() {
  bool success = root_layout->layout(AspectRatio, 1);
  if (!success) {
    throw std::runtime_error("unable to lay out floorplan!");
  }
}

void Lhd_floorplanner::write_file(const std::string_view filename) {
  ostream& fos = outputHotSpotHeader(filename.data());
  root_layout->outputHotSpotLayout(fos);
  outputHotSpotFooter(fos);
}

void Lhd_floorplanner::write_lhd(const std::string_view lgdb_path) {

  // make sure all nodes have a hier color
  root_lg->each_hier_fast_direct([](Node& n) {
    n.set_hier_color(0);
    return true;
  });

  root_layout->writeLiveHD(lgdb_path, root_lg);

  root_lg->each_hier_fast_direct([](const Node& n) {
    if (!n.is_type_synth()) {
      return true;
    }

    // basic sanity checking for returned floorplans
    I(n.is_hierarchical());
    I(n.has_hier_color());
    I(n.get_hier_color() == 1);
    I(n.has_place());

    if (debug_print) {
      fmt::print("node {} ", n.debug_name());
      fmt::print("level {} pos {} ", n.get_hidx().level, n.get_hidx().pos);

      const Ann_place& p = n.get_place();
      fmt::print("x: {:.3f}, y: {:.3f}, width: {:.3f}, height: {:.3f}\n",
                 p.get_pos_x(),
                 p.get_pos_y(),
                 p.get_len_x(),
                 p.get_len_y());
    }

    return true;
  });
}