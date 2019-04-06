//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <stdint.h>
#include <string.h>

#include <string_view>
#include <stdexcept>
#include <cassert>
#include <vector>

#include "sparsehash/dense_hash_map"

#include "dense.hpp"

typedef int32_t Char_Array_ID;

template <typename KeyType, typename Data_type> struct MapSerializer {
  bool operator()(FILE *fp, std::pair<const KeyType, Data_type> const &value) const {

    if(fwrite(&value.first, sizeof(value.first), 1, fp) != 1)
      return false;

    if(fwrite(&value.second, sizeof(value.second), 1, fp) != 1)
      return false;

    return true;
  }
  bool operator()(FILE *fp, std::pair<const KeyType, Data_type> *value) const {

    if(fread(const_cast<KeyType *>(&value->first), sizeof(value->first), 1, fp) != 1)
      return false;

    if(fread(const_cast<Data_type *>(&value->second), sizeof(value->second), 1, fp) != 1)
      return false;

    return true;
  }
};

template <typename Data_type> class Char_Array {
public:
  typedef uint32_t Hash_sign;
  enum { sign_nok = (sizeof(Hash_sign) >= 2) && !(sizeof(Hash_sign) & (sizeof(Hash_sign) - 1)) };
  static_assert(sign_nok, "should use a power2 and bigger than 2 for the Hash_sign size");

  class Const_Char_Array_Iter {
  public:
    explicit Const_Char_Array_Iter(const uint16_t *_first, const uint16_t *_ptr)
        : first(_first)
        , ptr(_ptr) {
    }
    Const_Char_Array_Iter operator++() {
      Const_Char_Array_Iter i(first, ptr);
      const uint16_t *      sz = ptr;
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

    Char_Array_ID get_id() const {
      return (uint16_t *)ptr - first + 1; // distance with first which was 1 (hence + 1 for id)
    }

    const Data_type &get_field() const {
      return *(const Data_type *)&ptr[1 + (sizeof(Hash_sign)) / sizeof(uint16_t)]; // Skip signature
    }

    Data_type &get_field() {
      return *(Data_type *)&ptr[1 + (sizeof(Hash_sign)) / sizeof(uint16_t)]; // Skip signature
    }

    size_t get_name_len() const {
      auto sz1 = *ptr;
      auto sz2 = sz1 - (sizeof(Data_type)+sizeof(Hash_sign))/sizeof(uint16_t);
      assert(sz2>0);
      if (ptr[sz1] == 0)
        sz2 = sz2 * 2 - 2; // 2 bytes used as end of string
      else
        sz2 = sz2 * 2 - 1; // 1 byte used as end of string

      return sz2;
    }

    std::string_view get_name() const {
      auto raw_str = (const char *)&ptr[1 + (sizeof(Data_type) + sizeof(Hash_sign)) / sizeof(uint16_t)];

      return std::string_view(raw_str,get_name_len());
    }

  private:
    const uint16_t *const first;
    const uint16_t *      ptr;
  };

private:
  std::string                                              long_name;
  mutable Dense<uint16_t>                                  variable_internal;
  mutable google::dense_hash_map<Hash_sign, Char_Array_ID> hash2id;

  const uint16_t *first() const {
    if(variable_internal.size() <= 1)
      return last();
    return &variable_internal[1];
  }
  const uint16_t *last() const {
    return (uint16_t *)variable_internal.end();
  }

  explicit Char_Array() = delete; // Not allowed

  Hash_sign re_hash(Hash_sign c_hash, Hash_sign seed) const {
    c_hash += seed;
    if(c_hash == 0)
      c_hash = 1023;
    return c_hash;
  }

  Hash_sign seed_hash(std::string_view key, Hash_sign &seed) const {
    std::size_t str_hash = std::hash<std::string_view>{}(key);
    Hash_sign   c_hash   = str_hash ^ (str_hash >> 32);
    if(c_hash == 0)
      c_hash = 1023;

    seed = str_hash >> 13;

    return c_hash;
  }

  Hash_sign prepare_hash(std::string_view key) {

    Hash_sign seed   = 0;
    Hash_sign c_hash = seed_hash(key, seed);

    auto it = hash2id.find(c_hash);
    if(it != hash2id.end()) {
      while(true) {
        Char_Array_ID cid = it->second;

        if(cid > 0)
          it->second = -cid; // Negative means re-hash entry
        c_hash = re_hash(c_hash, seed);

        it = hash2id.find(c_hash);
        if(it == hash2id.end())
          break;
      }
    }

    return c_hash;
  }

  void reload() const {           // WARNING: NO REAL CONST, but called from many places to hot-reload
    assert(variable_internal.size()==0);

    FILE *fp_in  = fopen((long_name + "_map").c_str(), "r");
    if(fp_in) {
      bool failed=false;
      size_t sz;
      if(fread(&sz, sizeof(size_t), 1, fp_in) != 1) // first is empty
        failed = true;
      failed |= !hash2id.unserialize(MapSerializer<Hash_sign, Char_Array_ID>(), fp_in);
      fclose(fp_in);
      variable_internal.reload(sz);

      if (!failed)
        return;

      std::string msg = "char_array::reload for is inconsistent for ";
      msg.append(long_name + "_map");
      throw std::runtime_error(msg);
    }

    if(variable_internal.size() == 0) {
      variable_internal.emplace_back(); // so that ID zero is not used
    }

    variable_internal.sync();
  }

public:
  explicit Char_Array(std::string_view long_name_)
      : long_name(long_name_)
      , variable_internal(long_name_) {

    hash2id.set_empty_key(0);

    reload();

    assert(variable_internal.size()>0);
  }

  virtual ~Char_Array() {
    sync();
  }

  bool empty() const {
    assert(variable_internal.size()>0);
    return variable_internal.size()==1;
  }

  void clear() {
    variable_internal.clear();
    variable_internal.emplace_back();
    hash2id.clear();
    unlink((long_name + "_map").c_str());
    assert(variable_internal.size()>0);
  }

  void sync() {
    assert(variable_internal.size()>0);
    variable_internal.sync();

    if(hash2id.empty()) {
      return;
    }

    FILE *fp = fopen((long_name + "_map").c_str(), "w");
    if(fp) {
      size_t sz = variable_internal.size();
      fwrite(&sz, sizeof(size_t), 1, fp);
      hash2id.serialize(MapSerializer<Hash_sign, Char_Array_ID>(), fp);
      fclose(fp);
      return;
    }

    std::string msg = "char_array::sync for is inconsistent for ";
    msg.append(long_name + "_map");
    throw std::runtime_error(msg);
  }

  Const_Char_Array_Iter begin() const {
    return Const_Char_Array_Iter(first(), first());
  }
  Const_Char_Array_Iter end() const {
    return Const_Char_Array_Iter(first(), last());
  }

  Char_Array_ID create_id(std::string_view str, const Data_type &dt = 0) {
    Char_Array_ID start = get_id(str);
    if(start) {

#ifndef NDEBUG
      size_t slen = str.size();
      slen++;      // for zero
      if(slen & 1) // multiple of 2 bytes storage
        slen++;
      size_t len = slen;

      assert(len < 32768); // uint16_t for delta
      assert(variable_internal[start] == (len + sizeof(Data_type) + sizeof(Hash_sign)) / sizeof(uint16_t));
#endif

      uint16_t *x = (uint16_t *)&dt;
      for(size_t i = 0; i < sizeof(Data_type) / sizeof(uint16_t); i++) {
        // SKIP: ptr + hash
        variable_internal[start + 1 + sizeof(Hash_sign) / sizeof(uint16_t) + i] = x[i];
      }

      return start;
    }

    size_t t = variable_internal.size();
    assert(t);

    assert(t < 0x8FFFFFFF); // Just reserve some space
    // NOTE: If we need more space. We could create a separate table that does
    // id2internal. The internal can grow to 64 bits, and still keep the id as
    // 31 bits. Then, we can have ~1B different IDs per lgraph.
    start = static_cast<Char_Array_ID>(t);

    size_t slen = str.size();
    size_t len  = slen;
    len++;      // for zero
    if(len & 1) // multiple of 2 bytes storage
      len++;

    len += sizeof(Data_type) + sizeof(Hash_sign);

    assert((sizeof(Data_type) & 1) == 0); // multiple of 2 bytes storage

    //--------------------- LEN (string + data + hash + ptr)
    variable_internal.push_back(len / 2);

    //--------------------- SIGNATURE
    Hash_sign c_hash = prepare_hash(str);
    assert(hash2id.find(c_hash) == hash2id.end());
    hash2id[c_hash] = start;
    {
      uint16_t *x = (uint16_t *)&c_hash;
      for(size_t i = 0; i < sizeof(Hash_sign) / 2; i++) {
        variable_internal.emplace_back(x[i]);
      }
    }

    //--------------------- DATA
    uint16_t *x = (uint16_t *)&dt;
    for(size_t i = 0; i < sizeof(Data_type) / 2; i++) {
      variable_internal.emplace_back(x[i]);
    }

    // String
    slen++;      // OK to increase to add zero
    if(slen & 1) // multiple of 2 bytes storage
      slen++;
    for(size_t i = 0; i < slen; i += 2) {
      uint16_t val = 0;
      if(str[i] != 0) { // Do not go over limit
        val = str[i + 1];
        val <<= 8;
      }
      val |= str[i];
      variable_internal.emplace_back(val);
    }

    return start;
  }

  size_t get_name_len(Char_Array_ID id) const {
    auto sz1 = variable_internal[id];
    auto sz2 = sz1 - (sizeof(Data_type)+sizeof(Hash_sign))/sizeof(uint16_t);
    assert(sz2>0);
    if (variable_internal[id+sz1] == 0)
      sz2 = sz2 * 2 - 2; // 2 bytes used as end of string
    else
      sz2 = sz2 * 2 - 1; // 1 byte used as end of string
    return sz2;
  }

  std::string_view get_name(Char_Array_ID id) const {
    if(id == 0) {
      static const std::string_view empty = "";
      return empty;
    }

    assert(variable_internal.size() > (id + 1 + (sizeof(Data_type) + sizeof(Hash_sign)) / sizeof(uint16_t)));

    // SKIP len + hash + data
    const char *raw_str = (const char *)&variable_internal[id + 1 + (sizeof(Data_type) + sizeof(Hash_sign)) / sizeof(uint16_t)];
    return std::string_view(raw_str,get_name_len(id));
  }

  const Data_type &get_field(Char_Array_ID id) const {
    assert(id >= 0);
    assert(variable_internal.size() > (id + 1 + (sizeof(Data_type) + sizeof(Hash_sign)) / sizeof(uint16_t)));

    // SKIP Len + Hash
    return *(const Data_type *)&variable_internal[id + 1 + (sizeof(Hash_sign)) / sizeof(uint16_t)];
  }

  Data_type &get_field(Char_Array_ID id) {
    assert(id >= 0);
    assert(variable_internal.size() > (id + 1 + (sizeof(Data_type) + sizeof(Hash_sign)) / sizeof(uint16_t)));

    // SKIP Len + Hash
    return *(Data_type *)&variable_internal[id + 1 + (sizeof(Hash_sign)) / sizeof(uint16_t)];
  }

  const Hash_sign &get_hash(Char_Array_ID id) const {
    assert(id >= 0);
    assert(variable_internal.size() > (id + 1 + sizeof(Hash_sign) / sizeof(uint16_t)));

    // SKIP Len
    return *(const Hash_sign *)&variable_internal[id + 1];
  }

  const Data_type &get_field(std::string_view ptr) const {
    return get_field(get_id(ptr));
  }

  bool include(std::string_view str) const {
    return get_id(str) != 0;
  }

  int get_id(std::string_view key) const {
    Hash_sign         seed   = 0;
    Hash_sign         c_hash = seed_hash(key, seed);
    auto              it     = hash2id.find(c_hash);
    if(it == hash2id.end())
      return 0;
    Char_Array_ID cid = it->second;
    if(cid > 0) {
      if(get_name(cid) ==  key)
        return cid;
      return 0;
    }

    while(cid < 0) {
      cid = -cid;
      if(get_name(cid) == key)
        return cid;

      c_hash  = re_hash(c_hash, seed);
      auto it = hash2id.find(c_hash);
      if(it == hash2id.end())
        return 0;
      cid = it->second;
    }

    if(get_name(cid) == key)
      return cid;
    return 0;
  }
};

