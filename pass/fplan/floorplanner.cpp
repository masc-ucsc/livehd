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
  auto ht = root_lg->ref_htree();

  // make sure all nodes have a hier color
  for (const auto& hidx : ht->depth_preorder()) {
    LGraph* lg = ht->ref_lgraph(hidx);
    for (auto fn : lg->fast()) {
      if (!fn.is_type_synth() && !fn.is_type_sub()) {
        continue;
      }

      Node hn(root_lg, hidx, fn.get_compact_class());

      hn.set_hier_color(0);
    }
  }

  /*
  for (const auto& hidx : ht->depth_preorder()) {
    LGraph* lg = ht->ref_lgraph(hidx);
    for (auto fn : lg->fast()) {
      if (!fn.is_type_synth()) {
        continue;
      }

      Node hn(root_lg, hidx, fn.get_compact_class());

      fmt::print("node: {}, level: {}, pos: {}\n", hn.debug_name(), hn.get_hidx().level, hn.get_hidx().pos);
    }
  }
  */

  root_layout->writeLiveHD(lgdb_path, root_lg, ht);

  if (debug_print) {
    for (const auto& hidx : ht->depth_preorder()) {
      LGraph* lg = ht->ref_lgraph(hidx);
      for (auto fn : lg->fast()) {
        if (!fn.is_type_synth()) {
          continue;
        }

        Node hn(root_lg, hidx, fn.get_compact_class());

        fmt::print("node {} ", hn.debug_name());

        I(hn.is_hierarchical());
        fmt::print("level {} pos {} ", hn.get_hidx().level, hn.get_hidx().pos);

        I(hn.has_hier_color());
        I(hn.get_hier_color() == 1);

        I(hn.has_place());
        const Ann_place& p = hn.get_place();

        fmt::print("x: {:.3f}, y: {:.3f}, width: {:.3f}, height: {:.3f}\n",
                   p.get_pos_x(),
                   p.get_pos_y(),
                   p.get_len_x(),
                   p.get_len_y());
      }
    }
  }
}