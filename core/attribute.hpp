// See LICENSE.txt for details

// NOTE: The skeleton of this method was created with GPT-4

#include <algorithm>
#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include "iassert.hpp"

template <typename T>
class Attribute {
public:
  Attribute() = default;

  void insert(uint32_t key, const T& value) {
    if (use_dense) {
      if (key >= dense_data.size()) {
        dense_data.resize(key + 1);
      }
      dense_data[key] = value;
    } else {
      sparse_data[key] = value;
    }
  }

  void insert(uint32_t key, T&& value) {
    if (use_dense) {
        if (key >= dense_data.size()) {
            dense_data.resize(key + 1);
        }
        dense_data[key] = std::forward<T>(value);
    } else {
        sparse_data[key] = std::forward<T>(value);
    }
  }


  [[nodiscard]] bool contains(uint32_t key) const {
    if (use_dense) {
      return key < dense_data.size() && dense_data[key] != T{};
    }
    return sparse_data.find(key) != sparse_data.end();
  }

  void erase(uint32_t key) {
    if (use_dense) {
      if (key < dense_data.size()) {
        dense_data[key] = T{};
      }
    } else {
      sparse_data.erase(key);
    }
  }

  [[nodiscard]] const T &get(uint32_t key) const {
    if (use_dense) {
      I(key < dense_data.size());
      return dense_data[key];
    } else {
      auto it = sparse_data.find(key);
      I(it!=sparse_data.end());
      return it->second;
    }
  }

  [[nodiscard]] T *ref(uint32_t key) const {
    if (use_dense) {
      I(key < dense_data.size());
      return &dense_data[key];
    } else {
      auto it = sparse_data.find(key);
      I(it!=sparse_data.end());
      return &it->second;
    }
  }

  void switch_to_sparse() {
    if (!use_dense)
      return;
    for (uint32_t i = 0; i < dense_data.size(); ++i) {
      if (dense_data[i] != T{}) {
        sparse_data[i] = dense_data[i];
      }
    }
    dense_data.clear();
    use_dense = false;
  }

  void switch_to_dense() {
    if (use_dense)
      return;

    uint32_t max_key = 0;
    for (const auto& kv : sparse_data) {
      max_key = std::max(max_key, kv.first);
    }
    dense_data.resize(max_key + 1, T{});
    for (const auto& kv : sparse_data) {
      dense_data[kv.first] = kv.second;
    }
    sparse_data.clear();
    use_dense = true;
  }

private:
  bool                             use_dense{false};
  absl::flat_hash_map<uint32_t, T> sparse_data;
  std::vector<T>                   dense_data;
};
