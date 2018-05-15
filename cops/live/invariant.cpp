
//serialization stuff
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>
#include <iostream>

#include "invariant.hpp"

void Invariant_boundaries::serialize(Invariant_boundaries *ib, std::ostream &ofs) {
  boost::archive::text_oarchive oa(ofs);
  oa << *ib;
  /*ofs << "Invatiant_boundaries: " << ib->top << std::endl;
  ofs << ib->invariant_cones.size() << std::endl;
  for(auto& c : ib->invariant_cones) {
    ofs << c.first.first << " " << c.first.second << " " << c.second.size() << std::endl;
    for(auto& d : c.second) {
      ofs << d.first << " " << d.second << std::endl;
    }
  }
  ofs << ib->instance_type_map.size() << std::endl;
  for(auto inst : ib->instance_type_map) {
    ofs << inst.first << " " << inst.second << std::endl;
  }

  ofs << ib->instance_collection.size() << std::endl;
  for(auto& c : ib->instance_collection) {
    ofs << c.first << " " << c.second.size() << std::endl;
    for(auto& d : c.second) {
      if(d != "")
        ofs << d << std::endl;
      else
        ofs << "##TOP##" << std::endl;
    }
  }

  ofs << ib->invariant_cone_cells.size() << std::endl;
  for(auto& c : ib->invariant_cone_cells) {
    ofs << c.first.first << " " << c.first.second << " " << c.second.size() << std::endl;
    for(auto& d : c.second) {
      ofs << d << std::endl;
    }
  }

  ofs << ib->hierarchy_tree.size() << std::endl;
  for(auto& c : ib->hierarchy_tree) {
    ofs << c.first << " " << c.second.size() << std::endl;
    for(auto& d : c.second) {
      ofs << d << std::endl;
    }
  }

  ofs << ib->gate_appearances.size() << std::endl;
  for(auto inst : ib->gate_appearances) {
    ofs << inst.first << " " << inst.second << std::endl;
  }*/
}

Invariant_boundaries *Invariant_boundaries::deserialize(std::istream &ifs) {
  Invariant_boundaries *        ib = new Invariant_boundaries;
  boost::archive::text_iarchive ia(ifs);
  ia >> *ib;
  /*std::string tmp1, tmp2;
  int size1, size2, tmp;
  ifs >> tmp1 >> ib->top;
  ifs >> size1;
  WireName_ID tmpid;
  uint32_t    bit;
  for(int i = 0; i < size1; i++) {
    ifs >> tmpid >> bit >> size2;
    Net_ID bound = std::make_pair(tmpid, bit);
    for(int j = 0; j < size2; j++) {
      ifs >> tmpid >> bit;
      Net_ID endpoint = std::make_pair(tmpid, bit);
      ib->invariant_cones[bound].insert(endpoint);
    }
  }
  ifs >> size1;
  for(int i = 0; i < size1; i++) {
    ifs >> tmp1 >> tmp2;
    ib->instance_type_map[tmp1] = tmp2;
  }

  ifs >> size1;
  for(int i = 0; i < size1; i++) {
    ifs >> tmp1 >> size2;
    for(int j = 0; j < size2; j++) {
      ifs >> tmp2;
      if(tmp2 == "##TOP##")
        ib->instance_collection[tmp1].insert(tmp2);
      else
        ib->instance_collection[tmp1].insert("");
    }
  }

  ifs >> size1;
  Index_ID tmp_idx;
  for(int i = 0; i < size1; i++) {
    ifs >> tmpid >> bit >> size2;
    Net_ID net = std::make_pair(tmpid, bit);
    for(int j = 0; j < size2; j++) {
      ifs >> tmp_idx;
      ib->invariant_cone_cells[net].insert(tmp_idx);
    }
  }

  ifs >> size1;
  for(int i = 0; i < size1; i++) {
    ifs >> tmp1 >> size2;
    for(int j = 0; j < size2; j++) {
      ifs >> tmp2;
      ib->hierarchy_tree[tmp1].insert(tmp2);
    }
  }

  ifs >> size1;
  for(int i = 0; i < size1; i++) {
    ifs >> tmp_idx >> tmp;
    ib->gate_appearances[tmp_idx] = tmp;
  }*/
  return ib;
}

template <class Archive>
void Invariant_boundaries::serialize(Archive &ar, const unsigned int version) {
  ar &top;
  ar &hierarchy_tree;
  ar &gate_appearances;
  ar &instance_collection;
  ar &instance_type_map;
  ar &invariant_cones;
  ar &invariant_cone_cells;
}
