#ifndef CHAR_ARRAY_H
#define CHAR_ARRAY_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <vector>
#include <unordered_map>

#include "dense.hpp"
#include "AAlloc.h"

#include "lglog.hpp"

typedef uint32_t Char_Array_ID;

// By Default use uint16_t as extra size field
template <typename Data_Type=uint16_t>
class Char_Array {
public:
  class Const_Char_Array_Iter {
  public:
    Const_Char_Array_Iter(const uint16_t *ptr): ptr(ptr){}
    Const_Char_Array_Iter operator++() {
      Const_Char_Array_Iter i(ptr);
      const uint16_t *sz = ptr;
      ptr+=(*sz+1); // +1 for the ptr itself
      return i;
    }
    Const_Char_Array_Iter operator--() {
      // Only forward iterator in this structure
      assert(0);
    }
    bool operator!=(const Const_Char_Array_Iter & other) { return ptr != other.ptr; }
    const char *operator*() const { return (const char *)(&ptr[1]); } // [1] to skip size part
  private:
    const uint16_t *ptr;
  };
private:
  //std::vector<uint16_t     , AAlloc::AlignedAllocator<uint16_t,4096> > variable_internal; // variable lenght
  Dense<uint16_t> variable_internal;

  const uint16_t *first() const {
    return &variable_internal[1];
  }
  const uint16_t *last() const {
    return &variable_internal[variable_internal.size()];
  }

  std::unordered_map<std::string, int> str2id;

  bool pending_clear_reload;

public:
  Char_Array(std::string path, std::string _name)
    : variable_internal(path + "/" + _name)  {

      pending_clear_reload = true;
  }

  void clear() {
    assert(pending_clear_reload); // called once
    pending_clear_reload = false;

    variable_internal.clear();
    variable_internal.push_back(0); // so that ID zero is not used
    str2id.clear();
  }

  void reload() {
    assert(pending_clear_reload); // called once
    pending_clear_reload = false;

    if (variable_internal.empty()) {
      variable_internal.push_back(0); // so that ID zero is not used
    }else{
      for(int i=1;i<variable_internal.size();) {
        const char *str = (const char *)&variable_internal[i+1];

        str2id[str] = i;
        i += variable_internal[i];
        i++;
      }
    }
  }

  void sync() {
    variable_internal.sync();
  }

  Const_Char_Array_Iter begin() const { return Const_Char_Array_Iter(first()); }
  Const_Char_Array_Iter end() const { return Const_Char_Array_Iter(last()); }

  Char_Array_ID create_id(const char *str) {
    assert(!pending_clear_reload);

    int len = strlen(str);
    len++; // for zero
    if (len&1) // multiple of 2 bytes storage
      len++;

    assert(len<32768); // uint16_t for delta

    if (str2id.find(std::string(str)) != str2id.end()) {
      return str2id[str];
    }

    int start = variable_internal.size();
    variable_internal.push_back(len/2);

    for(int i=0;i<len;i+=2) {
      uint16_t val = str[i+1];
      val <<= 8;
      val  |= str[i];
      variable_internal.push_back(val);
    }

    assert(start < 0xFFFFFFFF);

    str2id[str] = start;
    return start;
  }

  Char_Array_ID create_id(const char *str, Data_Type dt) {
    assert(!pending_clear_reload);

    int slen = strlen(str);
    slen++; // for zero
    if (slen&1) // multiple of 2 bytes storage
      slen++;
    int len = slen;

    assert(len<32768); // uint16_t for delta

    if (str2id.find(str) != str2id.end())  {
      int start = str2id[str];

      assert(variable_internal[start]==(len+sizeof(Data_Type))/2);

      uint16_t *x = (uint16_t *)&dt;
      for(int i=0;i<sizeof(Data_Type)/2;i++) {
        variable_internal[start+len/2+i+1] = x[i];
      }

      return start;
    }

    len += sizeof(Data_Type);
    assert((sizeof(Data_Type)&1) == 0); // multiple of 2 bytes storage
    int start = variable_internal.size();

    variable_internal.push_back(len/2);

    for(int i=0;i<slen;i+=2) {
      uint16_t val = str[i+1];
      val <<= 8;
      val  |= str[i];
      variable_internal.push_back(val);
    }
    uint16_t *x = (uint16_t *)&dt;
    for(int i=0;i<sizeof(Data_Type)/2;i++) {
      variable_internal.push_back(x[i]);
    }

    str2id[str] = start;
    assert(start < 0xFFFFFFFF);
    return start;
  }

  const char *get_char(int id) const {
    assert(!pending_clear_reload);
    assert(id>=0);
    assert(variable_internal.size()>(id+1));

    return (const char*)&variable_internal[id+1];
  }

  const Data_Type &get_field(int id) const {
    assert(!pending_clear_reload);

    const char *ptr = get_char(id);

    while(*ptr != 0)
      ptr++;
    ptr++;
    if (((uint64_t)ptr)&1)
      ptr++;

    return *(const Data_Type *)ptr;
  };

  int self_id(const char *ptr) const {
    ptr -= 2; // Pos for the ptr
    return (uint16_t *)ptr-first()+1; // zero
  }

  const Data_Type &get_field(const char *ptr) const {
    int id = self_id(ptr);
    return get_field(id);
  }

  void dump() const {
    for(int i=0;i<variable_internal.size();i++) {
      fmt::print("{}", (char)variable_internal[i]);
      fmt::print("{}", (char)(variable_internal[i]>>8));
    }
    fmt::print(" size:{}\n", variable_internal.size());
  }

  bool include(const char* str) const {
    return str2id.find(str) != str2id.end();
  }

  int get_id(const char* str) const {
    assert(include(str));
    return str2id.at(str);
  }

};


#endif

