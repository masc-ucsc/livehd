#pragma once

#include "absl/container/node_hash_map.h"

#include "lconst.hpp"

template<class Pin>
class Pin_tracker {
public:
  using Pin_map_entry=std::vector<std::pair<Pin,size_t>>;

  Pin_tracker(Pin invalid, Pin zero) : invalid_pin(invalid), zero_pin(zero) {}

  void do_get_mask(Pin dst_pin, Pin a_pin, Lconst mask) {
    auto it = map.find(a_pin);
    if (it == map.end())
      return; // nothing to do



  }
  void do_set_mask(Pin dst_pin, Pin a_pin, Lconst mask, Pin value_pin);
  void do_shl(Pin dst_pin, Pin a_pin, Lconst amount);
  void do_shr(Pin dst_pin, Pin a_pin, Lconst amount);
  void do_and(Pin dst_pin, Pin a_pin, Lconst amount);

  const Pin_map_entry &get_pin(Pin dst_pin) const {
    auto it = map.find(dst_pin);
    if (it == map.end()) {
      static Pin_map_entry empty;
      return empty;
    }
    return it->second;
  }

protected:
  Pin invalid_pin;
  Pin zero_pin;

  absl::node_hash_map<Pin, Pin_map_entry> map;
private:
};
