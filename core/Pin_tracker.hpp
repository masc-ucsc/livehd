#pragma once

#include "absl/container/node_hash_map.h"

#include "lconst.hpp"

template<typename Pin>
class Pin_tracker {
public:
  struct Pin_pos {
    Pin    id;
    int    pos = -1;
  };

  using Pin_vector=std::vector<Pin_pos>;

  Pin_tracker() {}

  void add_input(Pin wname, Bits_t bits) {
    auto &pv = full_map[wname];
    I(pv.empty()); // Why to double insert inputs??

    pv.resize(bits);
    for(auto i=0;i<bits;++i) {
      pv[i].id  = wname;
      pv[i].pos = i;
    }
  }

  void add_get_mask(Pin dst_pin, Pin a_pin, Lconst mask) {
    auto it = full_map.find(a_pin);
    if (it == full_map.end())
      return; // nothing to do

    Pin_vector pv;

    auto pairs = mask.get_mask_range_pairs();
    for(const auto &p:pairs) {
      auto start = p.first;
      auto end   = p.first + p.second;  // [start,end)
      auto pos   = start;
      if (end> it->second.size()) {
        end = it->second.size()-1;
      }

      while(pos<end) {
        pv.emplace_back(it->second[pos]);
        ++pos;
      }
    }

    full_map.insert_or_assign(dst_pin, pv);
  }

  void add_set_mask(Pin dst_pin, Pin a_pin, Bits_t a_sbits, Lconst mask, Pin v_pin) {

    Pin_vector pv = get_or_create_pv(a_pin, a_sbits);
    Pin_vector v_pv = get_or_create_pv(v_pin, mask.get_bits());

    auto pairs = mask.get_mask_range_pairs();

    if (pv.size()<v_pv.size())
      pv.resize(v_pv.size());

    auto pick_v_pos = 0;
    for(const auto &p:pairs) {
      auto start = p.first;
      auto end   = p.first + p.second; // [start,end)
      auto pos   = start;

      if (pv.size()<=end)
        pv.resize(end);

      while(pos<end) {
        if (v_pv.size()<=pick_v_pos) {
          pv[pos] = v_pv.back();
        }else{
          pv[pos] = v_pv[pick_v_pos];
        }
        ++pos;
        ++pick_v_pos;
      }
    }

    clean_excess_pv(pv);
    full_map.insert_or_assign(dst_pin, pv);
  }

  void add_shl(Pin dst_pin, Pin a_pin, Lconst amount);
  void add_shr(Pin dst_pin, Pin a_pin, Lconst amount);
  void add_and(Pin dst_pin, Pin a_pin, Lconst amount);

  const Pin_vector &get_pin_vector(Pin dst_pin) const {
    const auto it = full_map.find(dst_pin);
    if (it == full_map.end()) {
      static Pin_vector empty;
      return empty;
    }
    return it->second;
  }

  /* LCOV_EXCL_START */
  void dump() {
    for(const auto e:full_map) {
      fmt::print("name:{}\n", e.first);
      for(const auto &s:e.second) {
        fmt::print(" id:{} pos:{}\n", s.id, s.pos);
      }
    }
  }
  /* LCOV_EXCL_STOP */

protected:
  Pin_vector get_or_create_pv(Pin a_name, Bits_t a_sbits) const {

    auto it = full_map.find(a_name);
    if (it != full_map.end()) {
      return it->second;
    }

    Pin_vector a_pv;

    a_pv.resize(a_sbits);
    Bits_t pos = 0;
    for(auto &e:a_pv) {
      e.id = a_name;
      e.pos = pos;
      ++pos;
    }

    return a_pv;
  }

  static void clean_excess_pv(Pin_vector &pv) {
    auto rit = pv.rbegin();
    size_t ndel = 0;
    while (rit!=pv.rend()) {
      if (rit->pos>=0)
        break;

      ++ndel;
      ++rit;
    }
    if (ndel) {
      pv.resize(pv.size()-ndel);
    }
  }

  absl::node_hash_map<Pin, Pin_vector> full_map;
private:
};
