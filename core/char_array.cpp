//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "char_array.hpp"
#include "lglog.hpp"

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


template <typename Data_type>
void Char_Array<Data_type>::reload() const { // NO CONST, but called from many places to hot-reload
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
    console->info("char_array:reload for {} failed, creating empty",long_name);
  }
}

template <typename Data_type>
void Char_Array<Data_type>::clear() {
  pending_clear_reload = false;

  variable_internal.clear();
  variable_internal.emplace_back(0); // so that ID zero is not used
  hash2id.clear();
  synced = false;
}

template <typename Data_type>
void Char_Array::sync() {
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

template <typename Data_type>
Char_Array_ID Char_Array::create_id(const char *str, Data_type dt=0) {
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

template <typename Data_type>
int Char_Array::get_id(const char *str) const {
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
