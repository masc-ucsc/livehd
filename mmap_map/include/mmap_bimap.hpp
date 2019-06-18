//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "mmap_map.hpp"

namespace mmap_map {
template <typename Key, typename T>
class bimap {
public:
  using Key2val_type = typename mmap_map::map<Key,T>;
  using Val2key_type = typename mmap_map::map<T,Key>;
  Key2val_type key2val;
  Val2key_type val2key;

  using iterator       = typename Key2val_type::iterator;
  using const_iterator = typename Key2val_type::const_iterator;

	explicit bimap(std::string_view _map_name)
    :key2val(std::string(_map_name) + "_k2v")
    ,val2key(std::string(_map_name) + "_v2k")
  {
  }
	explicit bimap(std::string_view _path, std::string_view _map_name)
    :key2val(_path, std::string(_map_name) + "_k2v")
    ,val2key(_path, std::string(_map_name) + "_v2k")
  {
  }

	void clear() {
    key2val.clear();
    val2key.clear();
  }
	const_iterator set(Key&& key, T &&val) {
    val2key.set(val,key);
		return key2val.set(key,val);
	}
	const_iterator set(const Key& key, T &&val) {
    val2key.set(val,key);
		return key2val.set(key,val);
	}
	const_iterator set(const Key& key, const T &val) {
    val2key.set(val,key);
		return key2val.set(key,val);
	}
	const_iterator set(Key&& key, const T &val) {
    val2key.set(val,key);
		return key2val.set(key,val);
	}

  [[nodiscard]] bool has_key(const Key &key) const { return key2val.has(key); }
  [[nodiscard]] bool has_val(const T   &val) const { return val2key.has(val); }

	[[nodiscard]] T    get_val(const Key &key) const { return key2val.get(key); }
	[[nodiscard]] Key  get_key(const T &val  ) const { return val2key.get(val); }

  [[nodiscard]] iterator        find(const Key &key)       { return key2val.find(key); }
  [[nodiscard]] const_iterator  find(const Key &key) const { return key2val.find(key); }
  [[nodiscard]] const_iterator  find_val(const T &val) const {
    const auto it = val2key.find(val);
    if (it == val2key.end())
      return key2val.end();

    const Key key = val2key.get(it);
    const auto it2 = key2val.find(key);
    assert(it2!=key2val.end());
    return it2;
  }

  [[nodiscard]] iterator        begin()       { return key2val.begin();  }
  [[nodiscard]] const_iterator  begin() const { return key2val.cbegin(); }
  [[nodiscard]] const_iterator cbegin() const { return key2val.cbegin(); }

  [[nodiscard]] iterator        end()       { return key2val.end();  }
  [[nodiscard]] const_iterator  end() const { return key2val.cend(); }
  [[nodiscard]] const_iterator cend() const { return key2val.cend(); }

	iterator erase(const_iterator pos) {
    val2key.erase(pos.second);
    return key2val.erase(pos);
  }

	iterator erase(iterator pos) {
    val2key.erase(pos.second);
    return key2val.erase(pos);
  }

  size_t erase_key(const Key &key) {
    auto it = key2val.find(key);
    if (it == key2val.end())
      return 0;

    val2key.erase(it.second);
    key2val.erase(it);

    return 1;
  }

  void reserve(size_t sz) {
    key2val.reserve(sz);
    val2key.reserve(sz);
  }

  [[nodiscard]] size_t size()  const { return key2val.size();  }
  [[nodiscard]] bool   empty() const { return key2val.empty(); }

  [[nodiscard]] size_t capacity() const { return key2val.capacity(); }

	[[nodiscard]] Key get_key(const iterator &it) const       { return key2val.get_key(it); }
	[[nodiscard]] Key get_key(const const_iterator &it) const { return key2val.get_key(it); }
  [[nodiscard]] T get_val(const iterator &it) const       { return key2val.get(it); }
	[[nodiscard]] T get_val(const const_iterator &it) const { return key2val.get(it); }

};

} // namespace mmap_map

