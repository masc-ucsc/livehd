//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef CHAR_ARRAY_H
#define CHAR_ARRAY_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <unordered_map>
#include <vector>

#include "sparsehash/dense_hash_map"

#include "lglog.hpp"
#include "dense.hpp"

typedef int32_t Char_Array_ID;

template<typename KeyType, typename Data_type>
struct MapSerializer {
  bool operator()(FILE* fp, std::pair<const KeyType, Data_type> const& value) const {

    if (fwrite(&value.first, sizeof(value.first), 1, fp) != 1)
      return false;

    if (fwrite(&value.second, sizeof(value.second), 1, fp) != 1)
      return false;

    return true;
  }
  bool operator()(FILE* fp, std::pair<const KeyType, Data_type>* value) const {

    if (fread(const_cast<KeyType*>(&value->first), sizeof(value->first), 1, fp) != 1)
      return false;

    if (fread(const_cast<Data_type*>(&value->second), sizeof(value->second), 1, fp) != 1)
      return false;

    return true;
  }
};

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

  void reload() const { // NO CONST, but called from many places to hot-reload
    assert(pending_clear_reload); // called once
    pending_clear_reload = false;
    synced = true;

    FILE* fp_in = fopen((long_name + "_map").c_str(), "r");
    bool failed = false;
    if(fp_in) {
      size_t sz;
      if (fread(&sz, sizeof(size_t), 1, fp_in) != 1) // first is variable_internal size
        failed = true;
      failed = !hash2id.unserialize(MapSerializer<Hash_sign, Char_Array_ID>(), fp_in);
      fclose(fp_in);
      variable_internal.reload(sz);
    }else{
      failed = true;
    }
    if (failed) {
      variable_internal.emplace_back(0); // so that ID zero is not used
      console->warn("char_array:reload for {} failed, creating empty",long_name);
    }
  }

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

  void clear() {
    pending_clear_reload = false;

    variable_internal.clear();
    variable_internal.emplace_back(0); // so that ID zero is not used
    hash2id.clear();
    synced = false;
  }

  void sync() {
    if (synced)
      return;

    assert(!pending_clear_reload);

    synced = true;
    variable_internal.sync();
    FILE *fp = fopen((long_name + "_map").c_str(), "w");
    if (fp) {
      size_t sz = variable_internal.size();
      fwrite(&sz, sizeof(size_t), 1, fp);
      hash2id.serialize(MapSerializer<Hash_sign, Char_Array_ID>(), fp);
      fclose(fp);
    }else{
      console->error("char_array::sync could not sync {}",long_name);
    }
  }

  Const_Char_Array_Iter begin() const {
    return Const_Char_Array_Iter(first());
  }
  Const_Char_Array_Iter end() const {
    return Const_Char_Array_Iter(last());
  }

  Char_Array_ID create_id(const std::string &str, Data_type dt=0) {
    return create_id(str.c_str(), dt);
  }

  Char_Array_ID create_id(const char *str, Data_type dt=0) {
    synced = false;
    if(pending_clear_reload)
      reload();

    Char_Array_ID start = get_id(str);
    if(start) {

#ifndef NDEBUG
      size_t slen = strlen(str);
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
        variable_internal[start + 1 + sizeof(Hash_sign)/sizeof(uint16_t) + i] = x[i];
      }

      return start;
    }

    size_t t = variable_internal.size();
    assert(t < 0x8FFFFFFF); // Just reserve some space
    // NOTE: If we need more space. We could create a separate table that does
    // id2internal. The internal can grow to 64 bits, and still keep the id as
    // 31 bits. Then, we can have ~1B different IDs per lgraph.
    start = static_cast<Char_Array_ID>(t);

    size_t slen = strlen(str);
    size_t len = slen;
    len++;      // for zero
    if(len & 1) // multiple of 2 bytes storage
      len++;

    len += sizeof(Data_type) + sizeof(Hash_sign);

    assert((sizeof(Data_type) & 1) == 0); // multiple of 2 bytes storage

    //--------------------- LEN (string + data + ptr)
    variable_internal.push_back(len / 2);

    //--------------------- SIGNATURE
    Hash_sign c_hash = prepare_hash(str,slen);
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
    slen++; // OK to increase to add zero
    if(slen & 1) // multiple of 2 bytes storage
      slen++;
    for(size_t i = 0; i < slen; i += 2) {
      uint16_t val = 0;
      if (str[i] != 0) { // Do not go over limit
        val = str[i + 1];
        val <<= 8;
      }
      val |= str[i];
      variable_internal.emplace_back(val);
    }

    return start;
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

  const Hash_sign &get_hash(Char_Array_ID id) const {
    if(pending_clear_reload)
      reload();

    assert(id >= 0);
    assert(variable_internal.size() > (id + 1 + sizeof(Hash_sign)/sizeof(uint16_t)));

    // SKIP Len
    return *(const Hash_sign *)&variable_internal[id + 1];
  }

  int self_id(const char *ptr) const {
    if(pending_clear_reload)
      reload();

    ptr -= 2;                             // Pos for the ptr
    return (uint16_t *)ptr - first() + 1; // skip zero
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

  int get_id(const char *str) const {
    if(pending_clear_reload)
      reload();

    const std::string key(str);
    Hash_sign seed=0;
    Hash_sign c_hash = seed_hash(key,seed);
    auto it = hash2id.find(c_hash);
    if (it == hash2id.end())
      return 0;
    Char_Array_ID cid = it->second;
    if (cid>0) {
      if(strcmp(get_char(cid),str)==0)
        return cid;
      return 0;
    }

    while(cid < 0) {
      cid = -cid;
      if (strcmp(get_char(cid),str)==0)
        return cid;

      c_hash = re_hash(c_hash, seed);
      auto it = hash2id.find(c_hash);
      if (it == hash2id.end())
        return 0;
      cid = it->second;
    }

    if (strcmp(get_char(cid),str)==0)
      return cid;
    return 0;
  }

  int get_id(const std::string &str) const { return get_id(str.c_str()); }
};

#endif
