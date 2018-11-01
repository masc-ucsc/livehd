//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef CHAR_ARRAY_H
#define CHAR_ARRAY_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "sparsehash/dense_hash_map"

#include "dense.hpp"

typedef int32_t Char_Array_ID;

template <typename Data_type> class Char_Array {
public:
  typedef uint32_t Hash_sign;
  enum {sign_nok = (sizeof(Hash_sign)>=2) && !(sizeof(Hash_sign) & (sizeof(Hash_sign) - 1))};
  static_assert(sign_nok, "should use a power2 and bigger than 2 for the Hash_sign size");

  class Const_Char_Array_Iter {
  public:
    explicit Const_Char_Array_Iter(const uint16_t *ptr)
        : ptr(ptr) {
    }
    Const_Char_Array_Iter operator++() {
      Const_Char_Array_Iter i(ptr);
      const uint16_t *sz = ptr;
      ptr += (*sz + 1); // +1 for the size itself
      return i;
    }
    Const_Char_Array_Iter operator--() {
      // Only forward iterator in this structure
      assert(0);
    }
    bool operator!=(const Const_Char_Array_Iter &other) {
      return ptr != other.ptr;
    }
    const char *operator*() const {
      return (const char *)(&ptr[1 + (sizeof(Data_type)+sizeof(Hash_sign))/sizeof(uint16_t)]);
    } // [3?] to skip ptr and Hash_sign
  private:
    const uint16_t *ptr;
  };

private:
  const std::string long_name;
  mutable Dense<uint16_t> variable_internal;
  mutable google::dense_hash_map<Hash_sign, Char_Array_ID> hash2id;
  mutable bool pending_clear_reload;
  mutable bool synced;

  const uint16_t *first() const {
    if(variable_internal.size() <= 1)
      return last();
    return &variable_internal[1];
  }
  const uint16_t *last() const {
    return (uint16_t *)variable_internal.end();
  }

  explicit Char_Array() = delete; // Not allowed

  Hash_sign prepare_hash(const char *str, uint16_t str_len) {
    const std::string key(str,str_len);
    return prepare_hash(key);
  }

  Hash_sign re_hash(Hash_sign c_hash, Hash_sign seed) const {
    c_hash += seed;
    if (c_hash==0)
      c_hash = 1023;
    return c_hash;
  }

  Hash_sign seed_hash(const std::string &key, Hash_sign &seed) const {
    std::size_t str_hash = std::hash<std::string>{}(key);
    Hash_sign c_hash = str_hash ^ (str_hash>>32);
    if (c_hash==0)
      c_hash = 1023;

    seed = str_hash>>13;

    return c_hash;
  }

  Hash_sign prepare_hash(const std::string &key) {

    Hash_sign seed=0;
    Hash_sign c_hash = seed_hash(key, seed);

    auto it = hash2id.find(c_hash);
    if (it != hash2id.end()) {
      while(true) {
        Char_Array_ID cid = it->second;

        if (cid>0)
          it->second = -cid; // Negative means re-hash entry
        c_hash  = re_hash(c_hash, seed);

        it = hash2id.find(c_hash);
        if (it == hash2id.end())
          break;
      }
    }

    return c_hash;
  }
  void reload() const;

public:
  explicit Char_Array(const std::string &long_name_)
    : long_name(long_name_)
    ,variable_internal(long_name_) {

    hash2id.set_empty_key(0);

    pending_clear_reload = true;
    synced = true;

    reload();
  }

  virtual ~Char_Array() {
    sync();
  }

  void clear();

  void sync();

  Const_Char_Array_Iter begin() const { return Const_Char_Array_Iter(first()); }
  Const_Char_Array_Iter end() const { return Const_Char_Array_Iter(last()); }


  Char_Array_ID create_id(const char *str, Data_type dt=0);
  Char_Array_ID create_id(const std::string &str, Data_type dt=0) {
    return create_id(str.c_str(), dt);
  }

  const char *get_char(Char_Array_ID id) const {
    if(pending_clear_reload)
      reload();

    assert(id >= 0);
    assert(variable_internal.size() > (id + 1 + (sizeof(Data_type) + sizeof(Hash_sign))/sizeof(uint16_t)));

    // SKIP len + hash + data
    return (const char *)&variable_internal[id + 1 + (sizeof(Data_type) + sizeof(Hash_sign))/sizeof(uint16_t)];
  }

  const Data_type &get_field(Char_Array_ID id) const {
    if(pending_clear_reload)
      reload();

    assert(id >= 0);
    assert(variable_internal.size() > (id + 1 + (sizeof(Data_type) + sizeof(Hash_sign))/sizeof(uint16_t)));

    // SKIP Len + Hash
    return *(const Data_type *)&variable_internal[id + 1 + (sizeof(Hash_sign))/sizeof(uint16_t)];
  }

  const Data_type &get_field(const std::string &s) const { return get_field(s.c_str()); }

  const Data_type &get_field(const char *ptr) const {
    assert(!pending_clear_reload);
    int id = get_id(ptr);
    return get_field(id);
  }

  bool include(const char *str) const {
    const std::string key(str);
    return include(key);
  }

  bool include(const std::string &str) const {
    return get_id(str.c_str())!=0;
  }

  int get_id(const char *str) const;
  int get_id(const std::string &str) const { return get_id(str.c_str()); }
};

#endif
