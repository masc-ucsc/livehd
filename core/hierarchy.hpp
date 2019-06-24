//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "absl/container/flat_hash_map.h"

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

class Hierarchy {
private:
  struct Hierarchy_data {  // 96bits total
    Hierarchy_data(const Lg_type_id _top_id, const Lg_type_id _class_id, const Index_ID _nid)
      :top_id(_top_id)
       ,class_id(_class_id)
       ,class_nid(_nid) {
       }
    Lg_type_id top_id;
    Lg_type_id class_id;
    Index_ID   class_nid;

    template <typename H>
    friend H AbslHashValue(H h, const Hierarchy_data& s) {
      return H::combine(std::move(h), s.top_id, s.class_id, s.class_nid);
    };
  };

  // FIXME: Migrate part of this to LGraph, and use mmap_map
  static inline absl::flat_hash_map<Hierarchy_index, Lg_type_id> hierarchy_map;
  static inline std::map<std::pair<Hierarchy_index, Index_ID>, Hierarchy_index> hierarchy_pool;
  static inline Hierarchy_index hierarchy_pool_max=0; // FIXME: Unique per top, and some way to cleanup max

protected:
  Hierarchy_index hidx;

public:
  Hierarchy(Hierarchy_index _hidx)
    :hidx(_hidx) {

    I(hidx);
  }

  static Hierarchy_index get_hierarchy_index(const Hierarchy_index &current_hidx, Index_ID class_nid, Lg_type_id class_lgid) {
    Hierarchy_index hidx;

    const auto &it = hierarchy_pool.find(std::pair(current_hidx,class_nid));
    if (it == hierarchy_pool.end()) {
      hidx = ++hierarchy_pool_max;
      hierarchy_pool[std::pair(hidx,class_nid)] = hidx;
    }else{
      hidx = it->second;
    }
    hierarchy_map.insert(std::pair(hidx, class_lgid));

    return hidx;
  }

  Hierarchy_index get_index() const { return hidx; }

  Lg_type_id get_class_lgid() const {
    const auto &it2 = hierarchy_map.find(hidx);
    I(it2 != hierarchy_map.end());
    return it2->second;
  }
};

