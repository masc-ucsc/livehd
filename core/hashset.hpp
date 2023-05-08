// See LICENSE for details


#pragma once

#include <iostream>
#include <cstdint>

class Hashset {
private:
  uint64_t h1{0};
  uint64_t h2{0};

public:
  // Constructor
  explicit Hashset() = default;

  // Method to insert a value and update the hashes
  void insert(uint64_t value) {
    h1 += value;
    h2 ^= value;
  }

  // Method to get the hash value of h1 and h2
  uint64_t get_value() const {
    return h1 ^ h2;
  }
};

