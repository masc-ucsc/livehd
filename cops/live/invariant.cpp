//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "invariant.hpp"

#include <iostream>

void Invariant_boundaries::serialize(Invariant_boundaries* ib, std::ostream& ofs) {
  ofs << "Invariant_boundaries: " << ib->top << " ##sep: " << ib->hierarchical_separator << std::endl;
  ofs << ib->invariant_cones.size() << std::endl;
  for (auto& c : ib->invariant_cones) {
    ofs << c.first.first << " " << c.first.second << " " << c.second.size() << std::endl;
    for (auto& d : c.second) {
      ofs << d.first << " " << d.second << std::endl;
    }
  }
  ofs << ib->instance_type_map.size() << std::endl;
  for (auto inst : ib->instance_type_map) {
    ofs << inst.first << " " << inst.second << std::endl;
  }

  ofs << ib->instance_collection.size() << std::endl;
  for (auto& c : ib->instance_collection) {
    ofs << c.first << " " << c.second.size() << std::endl;
    for (auto& d : c.second) {
      if (d != "") {
        ofs << d << std::endl;
      } else {
        ofs << "##TOP##" << std::endl;
      }
    }
  }

  ofs << ib->invariant_cone_cells.size() << std::endl;
  for (auto& c : ib->invariant_cone_cells) {
    ofs << c.first.first << " " << c.first.second << " " << c.second.size() << std::endl;
    for (auto& d : c.second) {
      ofs << d << std::endl;
    }
  }

  ofs << ib->hierarchy_tree.size() << std::endl;
  for (auto& c : ib->hierarchy_tree) {
    ofs << c.first << " " << c.second.size() << std::endl;
    for (auto& d : c.second) {
      ofs << d << std::endl;
    }
  }

  ofs << ib->gate_appearances.size() << std::endl;
  for (auto inst : ib->gate_appearances) {
    ofs << inst.first << " " << inst.second << std::endl;
  }
}

Invariant_boundaries* Invariant_boundaries::deserialize(std::istream& ifs) {
  Invariant_boundaries* ib = new Invariant_boundaries;
  std::string           tmp1, tmp2;
  int                   size1, size2, tmp;
  ifs >> tmp1 >> ib->top;
  ifs >> tmp1 >> ib->hierarchical_separator;
  ifs >> size1;
  WireName_ID tmpid;
  uint32_t    bit;
  for (int i = 0; i < size1; i++) {
    ifs >> tmpid >> bit >> size2;
    Net_ID bound = std::make_pair(tmpid, bit);
    for (int j = 0; j < size2; j++) {
      ifs >> tmpid >> bit;
      Net_ID endpoint = std::make_pair(tmpid, bit);
      ib->invariant_cones[bound].insert(endpoint);
    }
  }
  ifs >> size1;
  for (int i = 0; i < size1; i++) {
    ifs >> tmp1 >> tmp2;
    ib->instance_type_map[tmp1] = tmp2;
  }

  ifs >> size1;
  for (int i = 0; i < size1; i++) {
    ifs >> tmp1 >> size2;
    for (int j = 0; j < size2; j++) {
      ifs >> tmp2;
      if (tmp2 == "##TOP##") {
        ib->instance_collection[tmp1].insert(tmp2);
      } else {
        ib->instance_collection[tmp1].insert("");
      }
    }
  }

  ifs >> size1;
  Index_id tmp_idx;
  for (int i = 0; i < size1; i++) {
    ifs >> tmpid >> bit >> size2;
    Net_ID net = std::make_pair(tmpid, bit);
    for (int j = 0; j < size2; j++) {
      ifs >> tmp_idx.value;
      ib->invariant_cone_cells[net].insert(tmp_idx);
    }
  }

  ifs >> size1;
  for (int i = 0; i < size1; i++) {
    ifs >> tmp1 >> size2;
    for (int j = 0; j < size2; j++) {
      ifs >> tmp2;
      ib->hierarchy_tree[tmp1].insert(tmp2);
    }
  }

  ifs >> size1;
  for (int i = 0; i < size1; i++) {
    ifs >> tmp_idx.value >> tmp;
    ib->gate_appearances[tmp_idx] = tmp;
  }
  return ib;
}
