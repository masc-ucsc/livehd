#ifndef CHAR_ARRAY_H
#define CHAR_ARRAY_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <unordered_map>
#include <vector>

#include "dense.hpp"

#include "lglog.hpp"

typedef uint32_t Char_Array_ID;

// By Default use uint16_t as extra size field
template <typename Data_Type = uint16_t> class Char_Array {
public:
  class Const_Char_Array_Iter {
  public:
    explicit Const_Char_Array_Iter(const uint16_t *ptr)
        : ptr(ptr) {
    }
    Const_Char_Array_Iter operator++() {
      Const_Char_Array_Iter i(ptr);
      const uint16_t *      sz = ptr;
      ptr += (*sz + 1); // +1 for the ptr itself
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
      return (const char *)(&ptr[1]);
    } // [1] to skip size part
  private:
    const uint16_t *ptr;
  };

private:
  Dense<uint16_t> variable_internal;

  const uint16_t *first() const {
    if(variable_internal.size() <= 1)
      return last();
    return &variable_internal[1];
  }
  const uint16_t *last() const {
    return (uint16_t *)variable_internal.end();
  }

  std::unordered_map<std::string, Char_Array_ID> str2id;

  bool pending_clear_reload;

  explicit Char_Array() = delete; // Not allowed
public:
  explicit Char_Array(const std::string &path, const std::string &_name)
      : variable_internal(path + "/" + _name) {

    pending_clear_reload = true;
  }

  void clear() {
    assert(pending_clear_reload); // called once
    pending_clear_reload = false;

    variable_internal.clear();
    variable_internal.emplace_back(0); // so that ID zero is not used
    str2id.clear();
  }

  void reload() {
    assert(pending_clear_reload); // called once
    pending_clear_reload = false;

    if(variable_internal.empty()) {
      variable_internal.emplace_back(0); // so that ID zero is not used
    } else {
      for(size_t i = 1; i < variable_internal.size() - 1;) {
        const char *str = (const char *)&variable_internal[i + 1];
        str2id[str]     = i;
        i += variable_internal[i];
        i++;
      }
    }
  }

  void sync() {
    variable_internal.sync();
  }

  Const_Char_Array_Iter begin() const {
    return Const_Char_Array_Iter(first());
  }
  Const_Char_Array_Iter end() const {
    return Const_Char_Array_Iter(last());
  }

  Char_Array_ID create_id(const std::string &str) {
    return create_id(str.c_str());
  }

  Char_Array_ID create_id(const char *str) {
    assert(!pending_clear_reload);

    size_t len = strlen(str);
    len++;      // for zero
    if(len & 1) // multiple of 2 bytes storage
      len++;

    assert(len < 32768); // uint16_t for delta

    if(str2id.find(std::string(str)) != str2id.end()) {
      return str2id[str];
    }

    size_t start = variable_internal.size();
    variable_internal.emplace_back(len / 2);

    for(size_t i = 0; i < len; i += 2) {
      uint16_t val = str[i + 1];
      val <<= 8;
      val |= str[i];
      variable_internal.emplace_back(val);
    }

    assert(start < 0xFFFFFFFFUL); // sizeof(Char_Array_ID) < sizeof(size_t);

    str2id[str] = start;
    return start;
  }

  Char_Array_ID create_id(const std::string &str, Data_Type dt) {
    return create_id(str.c_str(), dt);
  }

  Char_Array_ID create_id(const char *str, Data_Type dt) {
    assert(!pending_clear_reload);

    size_t slen = strlen(str);
    slen++;      // for zero
    if(slen & 1) // multiple of 2 bytes storage
      slen++;
    size_t len = slen;

    assert(len < 32768); // uint16_t for delta

    if(str2id.find(str) != str2id.end()) {
      Char_Array_ID start = str2id[str];

      assert(variable_internal[start] == (len + sizeof(Data_Type)) / 2);

      uint16_t *x = (uint16_t *)&dt;
      for(size_t i = 0; i < sizeof(Data_Type) / 2; i++) {
        variable_internal[start + len / 2 + i + 1] = x[i];
      }

      return start;
    }

    len += sizeof(Data_Type);
    assert((sizeof(Data_Type) & 1) == 0); // multiple of 2 bytes storage
    size_t t = variable_internal.size();
    assert(t < 0x8FFFFFFF); // Just reserve some space
    Char_Array_ID start = static_cast<Char_Array_ID>(t);

    variable_internal.push_back(len / 2);

    for(size_t i = 0; i < slen; i += 2) {
      uint16_t val = str[i + 1];
      val <<= 8;
      val |= str[i];
      variable_internal.emplace_back(val);
    }
    uint16_t *x = (uint16_t *)&dt;
    for(size_t i = 0; i < sizeof(Data_Type) / 2; i++) {
      variable_internal.emplace_back(x[i]);
    }

    str2id[str] = start;
    return start;
  }

  const char *get_char(Char_Array_ID id) const {
    assert(!pending_clear_reload);
    assert(id >= 0);
    assert(variable_internal.size() > (id + 1));

    return (const char *)&variable_internal[id + 1];
  }

  const Data_Type &get_field(Char_Array_ID id) const {
    assert(!pending_clear_reload);

    const char *ptr = get_char(id);

    while(*ptr != 0)
      ptr++;
    ptr++;
    if(((uint64_t)ptr) & 1)
      ptr++;

    return *(const Data_Type *)ptr;
  };

  int self_id(const char *ptr) const {
    ptr -= 2;                             // Pos for the ptr
    return (uint16_t *)ptr - first() + 1; // zero
  }

  const Data_Type &get_field(const char *ptr) const {
    int id = get_id(ptr);
    return get_field(id);
  }

  void dump() const {
    for(size_t i = 0; i < variable_internal.size(); i++) {
      fmt::print("{}", (char)variable_internal[i]);
      fmt::print("{}", (char)(variable_internal[i] >> 8));
    }
    fmt::print(" size:{}\n", variable_internal.size());
  }

  bool include(const char *str) const {
    return str2id.find(str) != str2id.end();
  }

  int get_id(const char *str) const {
    assert(include(str));
    return str2id.at(str);
  }
};

#endif
