#pragma once
#include <optional>
#include <utility> // for std::pair

template <class K, class V>
class bstar {
public:
  bstar();
  void insert(std::pair<K, V> kv);
  void remove(K key);
  std::optional<V> find(K key);
};