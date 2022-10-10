#pragma once

#include "absl/container/node_hash_map.h"

#include "lconst.hpp"

template<class Pin>
class Pin_tracker {
public:
  Pin_tracker(Pin invalid);

  void get_mask(Pin dst_pin, Pin a_pin, Lconst mask);
  void set_mask(Pin dst_pin, Pin a_pin, Lconst mask, Pin value_pin);

  const std::vector<Pin> &get_pin(Pin dst_pin) const {
    auto it = map.find(dst_pin);
    if (it == map.end()) {
      static std::vector<Pin> empty;
      return empty;
    }
    return it->second;
  }

protected:

  absl::node_hash_map<Pin, std::vector<Pin>> map;
private:
};
