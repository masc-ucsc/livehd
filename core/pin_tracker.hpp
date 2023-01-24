#pragma once

#include "absl/container/node_hash_map.h"
#include "lconst.hpp"

template <typename Pin>
class Pin_tracker {
public:
  struct Pin_pos {
    Pin id;
    int pos = -1;
  };


  using Pin_vector = std::vector<Pin_pos>;

  Pin_tracker(Pin zero_pin) :Zero_pin(zero_pin) {}

  void add_input(Pin wname, Bits_t bits) {
    auto &pv = full_map[wname];
    I(pv.empty());  // Why to double insert inputs??

    pv.resize(bits, {Zero_pin,-1});
    for (auto i = 0; i < bits; ++i) {
      pv[i].id  = wname;
      pv[i].pos = i;
    }
  }

  void add_get_mask(Pin dst_pin, Pin a_pin, Bits_t a_sbits, Lconst mask) {
    auto &pv = full_map[dst_pin];
    pv.clear();

    auto it = full_map.find(a_pin);
    if (it == full_map.end()) {
      add_input(a_pin, a_sbits);
      it = full_map.find(a_pin);
    }

    auto pairs = mask.get_mask_range_pairs();
    for (const auto &p : pairs) {
      auto start = p.first;
      auto end   = p.first + p.second;  // [start,end)
      auto pos   = start;
      if (end > it->second.size()) {
        end = it->second.size() - 1;
      }

      while (pos < end) {
        pv.emplace_back(it->second[pos]);
        ++pos;
      }
    }
  }

  void add_set_mask(Pin dst_pin, Pin a_pin, Bits_t a_sbits, Lconst mask, Pin v_pin) {
    Pin_vector pv   = get_or_create_pv(a_pin, a_sbits);
    Pin_vector v_pv = get_or_create_pv(v_pin, mask.get_bits());

    if (pv.size() < v_pv.size()) {
      pv.resize(v_pv.size(), {Zero_pin, -1});
    }

    auto pick_v_pos = 0;
    auto pairs      = mask.get_mask_range_pairs();
    for (const auto &p : pairs) {
      auto start = p.first;
      auto end   = p.first + p.second;  // [start,end)
      auto pos   = start;

      if (pv.size() <= end) {
        pv.resize(end, {Zero_pin, -1});
      }

      while (pos < end) {
        if (v_pv.size() <= pick_v_pos) {
          pv[pos] = v_pv.back();
        } else {
          pv[pos] = v_pv[pick_v_pos];
        }
        ++pos;
        ++pick_v_pos;
      }
    }

    clean_excess_pv(pv);
    full_map.insert_or_assign(dst_pin, pv);
  }

  void add_shl(Pin dst_pin, Pin a_pin, Bits_t a_sbits, Lconst amount) {
    auto &pv = full_map[dst_pin];
    pv.resize(a_sbits+amount.to_i(), {Zero_pin, -1});

    for(auto i=0;i<amount.to_i();++i) {
      pv[i].id = Zero_pin;
      pv[i].pos = 0;
    }

    auto it = full_map.find(a_pin);
    if (it==full_map.end()) {
      add_input(a_pin, a_sbits);
      it = full_map.find(a_pin);
    }

    for(auto i=0;i<a_sbits;++i) {
      if (i>=it->second.size()) {
        pv[amount.to_i()+i].id  = it->second.back().id;
        pv[amount.to_i()+i].pos = it->second.back().pos;
      }else{
        pv[amount.to_i()+i].id  = it->second[i].id;
        pv[amount.to_i()+i].pos = it->second[i].pos;
      }
    }
  }
  void add_sra(Pin dst_pin, Pin a_pin, Bits_t a_sbits, Lconst amount) {
    I(a_sbits>0);
    I(amount.to_i()>=0);

    auto &pv = full_map[dst_pin];
    pv.resize(a_sbits-amount.to_i(), {Zero_pin, -1});

    auto it = full_map.find(a_pin);
    if (it == full_map.end()) {
      add_input(a_pin, a_sbits);
      it = full_map.find(a_pin);
    }

    for(auto i=0;i<a_sbits-amount.to_i();++i) {
      if (i>=it->second.size()) {
        pv[i].id  = it->second.back().id;
        pv[i].pos = it->second.back().pos;
      }else{
        pv[i].id  = it->second[i].id;
        pv[i].pos = it->second[i].pos;
      }
    }
  }

  void add_andor(Pin dst_pin, Pin a_pin) {
    auto it = full_map.find(a_pin);
    if (it == full_map.end()) {
      return;
    }
    auto &pv = full_map[dst_pin];
    if (pv.size()< it->second.size())
      pv.resize(it->second.size(), {Zero_pin, 0});

    it = full_map.find(a_pin); // The resize could destroy the iterator

    for(auto i=0;i<it->second.size();++i) {
      if (pv[i].id == Zero_pin && pv[i].pos == 0) {
        pv[i] = it->second[i];
      }else{
        pv[i].pos = -1;
      }
    }
  }

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
    for (const auto e : full_map) {
      fmt::print("name:{}\n", e.first);
      for (const auto &s : e.second) {
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

    a_pv.resize(a_sbits, {Zero_pin, -1});
    Bits_t pos = 0;
    for (auto &e : a_pv) {
      e.id  = a_name;
      e.pos = pos;
      ++pos;
    }

    return a_pv;
  }

  static void clean_excess_pv(Pin_vector &pv) {
    auto   rit  = pv.rbegin();
    size_t ndel = 0;
    while (rit != pv.rend()) {
      if (rit->pos >= 0) {
        break;
      }

      ++ndel;
      ++rit;
    }
    if (ndel) {
      pv.resize(pv.size() - ndel);
    }
  }

  Pin Zero_pin;
  absl::node_hash_map<Pin, Pin_vector> full_map;

private:
};
