//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "mmap_map.hpp"

template <typename Key, typename T>
class unordered_bimap {
public:
  using iterator = idx2val::iterator;
  using const_iterator = idx2val::const_iterator;

	explicit unordered_bimap(std::string_view _map_name) {
  }

	void clear() {
    idx2val.clear();
    val2idx.clear();
  }
	const_iterator set(key_type&& key, T &&val) {
    val2idx.set(val,key);
		return idx2val.set(key,val);
	}
	const_iterator set(const key_type& key, T &&val) {
    val2idx.set(val,key);
		return idx2val.set(key,val);
	}
	const_iterator set(const key_type& key, const T &val) {
    val2idx.set(val,key);
		return idx2val.set(key,val);
	}
	const_iterator set(key_type&& key, const T &val) {
    val2idx.set(val,key);
		return idx2val.set(key,val);
	}

  [[nodiscard]] bool has_key(const Key &key) const { return idx2val.has(key); }
  [[nodiscard]] bool has_val(const T   &val) const { return val2idx.has(val); }

	[[nodiscard]] T const& get_val(const Key &key) const { return idx2val.get(key); }
	[[nodiscard]] Key const& get_key(const T &val) const { return val2idx.get(val); }

  [[nodiscard]] iterator        begin()       { return idx2val.begin();  }
  [[nodiscard]] const_iterator  begin() const { return idx2val.cbegin(); }
  [[nodiscard]] const_iterator cbegin() const { return idx2val.cbegin(); }

  [[nodiscard]] iterator        end()       { return idx2val.end();  }
  [[nodiscard]] const_iterator  end() const { return idx2val.cend(); }
  [[nodiscard]] const_iterator cend() const { return idx2val.cend(); }

	iterator erase(const_iterator pos) {
    val2idx.erase(pos.second);
    return idx2val.erase(pos);
  }

	iterator erase(iterator pos) {
    val2idx.erase(pos.second);
    return idx2val.erase(pos);
  }

  size_t erase_key(const Key &key) {
    auto it = idx2val.find(key);
    if (it == idx2val.end())
      return 0;

    val2idx.erase(it.second);
    idx2val.erase(it);

    return 1;
  }

  void reserve(size_t sz) {
    idx2val.reserve(sz);
    val2idx.reserve(sz);
  }

  [[nodiscard]] size_t size()  const { return idx2val.size();  }
  [[nodiscard]] bool   empty() const { return idx2val.empty(); }

  [[nodiscard]] size_t capacity() const { return idx2val.capacity(); }

	[[nodiscard]] std::string_view get_key(const iterator &it) const       { return idx2val.get_key(it); }
	[[nodiscard]] std::string_view get_key(const const_iterator &it) const { return idx2val.get_key(it); }
	[[nodiscard]] std::string_view get_val(const iterator &it) const       { return idx2val.get_val(it); }
	[[nodiscard]] std::string_view get_val(const const_iterator &it) const { return idx2val.get_val(it); }

};


