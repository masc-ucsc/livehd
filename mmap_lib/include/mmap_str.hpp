//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "mmap_map.hpp"
//#include "mmap_vector.hpp"

namespace mmap_lib {

class str {
protected:
  // Keeping the code constexpr for small strings (not long) requires templates (A challenge but reasonable).
  // Some references:
  // https://github.com/tcsullivan/constexpr-to-string
  // https://github.com/vesim987/constexpr_string/blob/master/constexpr_string.hpp
  // https://github.com/vivkin/constexprhash/blob/master/constexprhash.h
  // https://github.com/unterumarmung/fixed_string
  // https://github.com/proxict/constexpr-string
  // https://github.com/tonypilz/ConstexprString
  //
  // 16 bytes data structure:
  //
  // ptr_or_start:
  // 10 "special" character (allow to encode 2 digits as 1 character when (c&0x80) is true)
  // _size is the str _size (original not compressed)
  //
  // NOTE: Maybe it is faster/easier to have this instead:
  //
  // ptr_or_start
  // 12 special characters
  // if e[11]&0x80 || e[12]==0 -> overflow (ptr not start)
  // end of string is first zero (or last e[11]&0x80)
  //
  // This avoid having a "size" in the costly str in-place data. The overflow
  // can have a string size like a string_view does
  //
  // The only drawback is that to compute size, it needs to iterate over the e
  // field, but asking size is not a common operation in LiveHD

  uint32_t             ptr_or_start;  // 4 chars if _size < 14, is a ptr to string_map2 otherwise
  std::array<char, 10> e;             // last 10 "special <128" characters ending the string
  uint16_t             _size;         // 2 bytes
  constexpr bool       is_digit(char c) const { return c >= '0' && c <= '9'; }
  constexpr uint8_t    posShifter(uint8_t s) const { return s < 4 ? (s - 1) : 3; }
  constexpr uint8_t    posStopper(uint8_t s) const { return s < 4 ? s : 4; }
  constexpr char       isol8(uint32_t ps, uint8_t s) const { return (ps >> (s * 8)) & 0xff; }
  constexpr uint32_t   l8(uint32_t size, uint8_t i) const { return i - (size - 10); }
  constexpr uint32_t   mid(uint32_t ps, uint8_t i) const { return ps + (i - 2); }

public:
  // FIXME: This is a non persistent map. Something like string_map2("lgdb","global_str");
  static mmap_lib::map<std::string_view, uint32_t> string_map2;
  // static mmap_lib::map<std::string_view, uint32_t> string_map2("lgdb", "global_str_map");

  // FIXME: Change this for a mmap_lib::vector<int> string_vector("lgdb","global_str_vector");
  inline static std::vector<int> string_vector;  // ptr_or_start points here!
  // inline static mmap_lib::vector<int> string_vector("lgdb", "global_str_vector");
  // static mmap_lib::vector<int> string_vector;

  //===========constructor 0 (template obj) ============
  str() : ptr_or_start(0), e{0}, _size(0) {}

  //===========constructor 1 (_size <= 13) ============
  template <std::size_t N, typename = std::enable_if_t<(N - 1) < 14>>
  constexpr str(const char (&s)[N]) : ptr_or_start(0), e{0}, _size(N - 1) {
    // if _size is less than 4, then whole thing will be in ptr_or_start
    auto stop = posStopper(_size);
    // ptr_or_start will hold first 4 chars
    // | first | second | third | forth |
    // 31      23       15      7       0
    for (auto i = 0; i < stop; ++i) {
      ptr_or_start <<= 8;
      ptr_or_start |= s[i];
    }
    auto e_pos = 0u;  // e indx starts at 0
    // stores rest in e, starting from stop to end of string
    for (auto i = stop; i < N - 1; ++i) {
      e[e_pos] = s[i];
      ++e_pos;
    }
  }

  //=====helper function to check if a string exists in string_vector=====
  std::pair<int, int> insertfind(const char *string_to_check, uint32_t size) {
    std::string_view sv(string_to_check);  // string to sv
    // using substr here to take out all the weird things that come with sview
    auto it = string_map2.find(sv.substr(0, size));  // find the sv in the string_map2
    if (it == string_map2.end()) {                   // if we can't find the sv
      //<std::string_view, uint32_t(position in vec)> string_map2
      string_map2.set(sv.substr(0, size), string_vector.size());  // insert it
      return std::make_pair(0, 0);
    } else {
      return std::make_pair(string_map2.get(it), size);  // found it, return
      // pair is (ptr_or_start, size of string)
    }

    return std::make_pair(string_map2.get(it), size);
  }

  //==========constructor 2 (_size >= 14) ==========
  template <std::size_t N, typename = std::enable_if_t<(N - 1) >= 14>, typename = void>
  str(const char (&s)[N]) : ptr_or_start(0), e{0}, _size(N - 1) {
    // the first two characters saved in e
    e[0] = s[0];
    e[1] = s[1];
    // the last eight characters saved in e
    for (int i = 0; i < 8; i++) {
      e[9 - i] = s[_size - 1 - i];
    }

    char long_str[_size - 10];  // holds the long part
    // filling long_str with long part of string
    for (int i = 0; i < (_size - 10); ++i) {
      long_str[i] = s[i + 2];
    }
    // checking if long part of string already exists in vector
    std::pair<int, int> pair = insertfind(long_str, _size - 10);

    // if string exists in vector, get the ptr to it in string_map2
    // pair is (ptr_or_start, size of string)
    if (pair.second) {
      ptr_or_start = pair.first;
    } else {  // if string is not invector, put it in vector
      for (int i = 0; i < _size - 10; i++) {
        string_vector.emplace_back(long_str[i]);
      }
      ptr_or_start = string_vector.size() - (_size - 10);
    }
  }

  //============constructor 3=============
  // const char * will go through this one
  // implicit conversion from const char* --> string_view
  //
  // std::string will also go through this one
  str(std::string_view sv) : ptr_or_start(0), e{0}, _size(sv.size()) {
    if (_size < 14) {  // constructor 1 logic
      auto stop = posStopper(_size);
      for (auto i = 0; i < stop; ++i) {
        ptr_or_start <<= 8;
        ptr_or_start |= sv.at(i);
      }
      auto e_pos = 0u;
      for (auto i = stop; i < _size; ++i) {
        e[e_pos] = sv.at(i);
        ++e_pos;
      }
    } else {  // constructor 2 logic
      e[0] = sv.at(0);
      e[1] = sv.at(1);
      // the last eight  characters
      for (int i = 0; i < 8; i++) {
        e[9 - i] = sv.at(_size - 1 - i);  // there is no null terminator in sv
      }
      // checking if it exists
      char long_str[_size - 10];
      for (int i = 0; i < _size - 10; i++) {
        long_str[i] = sv.at(i + 2);
      }
      std::pair<int, int> pair = insertfind(long_str, _size - 10);
      if (pair.second) {
        ptr_or_start = pair.first;
      } else {
        for (int i = 0; i < _size - 10; i++) {
          string_vector.emplace_back(long_str[i]);
        }
        ptr_or_start = string_vector.size() - (_size - 10);
      }
    }
  }

  //=========Printing==============
  void print_PoS() const {
    std::cout << "ptr_or_start is";
    if (_size >= 14) {
      std::cout << "(ptr): " << ptr_or_start << std::endl;
    } else if (_size < 14) {
      std::cout << "(start): ";
      // [first] [sec] [thr] [fourth]
      for (int i = 3; i >= 0; --i) {
        std::cout << char(ptr_or_start >> (i * 8));
      }
      std::cout << std::endl;
    }
  }

  void print_e() const {
    std::cout << "e is: [ ";
    for (auto i = 0u; i < e.size(); ++i) {
      std::cout << e[i] << " ";
    }
    std::cout << "]" << std::endl;
  }

  void print_StrVec() const {
    std::cout << "StrVec{ ";
    for (auto i = string_vector.begin(); i != string_vector.end(); ++i) {
      std::cout << char(*i) << " ";
    }
    std::cout << "}" << std::endl;
  }

  void print_StrMap() const {
    std::cout << "StrMap{ ";
    for (auto it = string_map2.begin(), end = string_map2.end(); it != end; ++it) {
      std::string key   = std::string(string_map2.get_key(it));
      uint32_t    value = string_map2.get(it);
      std::cout << "<" << key << ", " << value << "> ";
    }
    std::cout << "}" << std::endl;
  }

  void print_string() {
    if (_size <= 13) {
      uint8_t mx = posShifter(_size);
      for (uint8_t i = mx; i <= 3u; --i) {
        std::cout << isol8(ptr_or_start, i);
      }
      if (_size > 4) {
        for (uint8_t j = 0; j < e.size(); ++j) {
          std::cout << static_cast<char>(e[j]);
        }
      }
    } else {
      std::cout << char(e[0]) << char(e[1]);
      for (auto i = 0; i < _size - 10; ++i) {
        std::cout << static_cast<char>(string_vector.at(i + ptr_or_start));
      }
      for (uint8_t k = 2; k < 10; ++k) {
        std::cout << static_cast<char>(e[k]);
      }
    }
  }

  //=================================

  [[nodiscard]] constexpr std::size_t size() const { return _size; }
  [[nodiscard]] constexpr std::size_t length() const { return _size; }
  [[nodiscard]] constexpr std::size_t max_size() const { return 65535; }
  [[nodiscard]] constexpr bool        empty() const { return 0 == _size; }

  template <std::size_t N>
  constexpr bool operator==(const char (&rhs)[N]) const {
    auto rhs_size = N - 1;
    if (_size != rhs_size) {
      return false;
    }  // if size doesnt match, false
    if (_size < 14) {
      return str(rhs) == *this;
    } else if (_size >= 14) {  // string_vector ptr in ptr_or_start, chars in e
      if (e[0] != rhs[0] || e[1] != rhs[1]) {
        return false;
      }  // check first two
      uint8_t idx = 8;
      for (auto i = 2; i < 10; ++i) {
        if (e[i] != rhs[rhs_size - idx--]) {
          return false;
        }  // check last eight
      }

      // Getting data from string_vector and comparing with rest of rhs
      auto j = 2;  // rhs[2 .. _size - 8] --> the long part
      // for loop range: (ptr_or_start) .. (ptr_or_start + _size-10)
      for (auto i = ptr_or_start; i < (ptr_or_start + _size - 10); ++i) {
        if (string_vector[i] != rhs[j]) {
          return false;
        }
        j = j < _size - 8 ? j + 1 : j;
      }
      return true;
    }
    return false;
  }

  // const char * will go through this one
  // implicit conversion from const char* --> string_view
  //
  // std::string will also go through this one
  constexpr bool operator==(std::string_view rhs) const {
    auto rhs_size = rhs.size();
    if (_size != rhs_size) {
      return false;
    }  // if size doesnt match, false
    if (_size < 14) {
      return str(rhs) == *this;
    } else if (_size >= 14) {  // string_vector ptr in ptr_or_start, chars in e
      if (e[0] != rhs.at(0) || e[1] != rhs.at(1)) {
        return false;
      }  // chk first two
      uint8_t idx = 8;
      for (auto i = 2; i < 10; ++i) {
        if (e[i] != rhs.at(rhs_size - idx--)) {
          return false;
        }  // chk last eight
      }
      // return if rhs w/out first two and last eight is in string_map2
      return !(string_map2.find(rhs.substr(2, rhs_size - 10)) == string_map2.end());
    }
    return false;
  }

  constexpr bool operator==(const str &rhs) const {
    if (_size == 0 && rhs._size == 0) {
      return true;
    }  // size
    if (_size != rhs._size) {
      return false;
    }
    for (auto i = 0u; i < e.size(); ++i) {  // e[]
      if (e[i] != rhs.e[i]) {
        return false;
      }
    }
    return (ptr_or_start == rhs.ptr_or_start);  // p_o_s
  }

  constexpr bool operator!=(const str &rhs) const { return !(*this == rhs); }

  template <std::size_t N>
  constexpr bool operator!=(const char (&rhs)[N]) const {
    return !(*this == rhs);
  }

  constexpr bool operator!=(std::string_view rhs) const { return !(*this == rhs); }

  constexpr char operator[](std::size_t pos) const {
    if (pos >= _size) {
      throw std::out_of_range("[] operator out of range.");
    }
    if (_size < 14) {
      if (pos < 4) {
        uint8_t mx = posShifter(_size);
        mx         = mx - pos;
        return isol8(ptr_or_start, mx);
      } else {
        return e[pos - 4];
      }
    } else {
      if (pos < 2) {
        return e[pos];
      } else if (pos >= static_cast<size_t>(_size - 8)) {
        return e[l8(_size, pos)];
      } else {
        return string_vector[mid(ptr_or_start, pos)];
      }
    }
  }

  // checks if *this pstr starts with st
  // Thought...
  // Can use substr function of sview in cases where both st and *this are LONG,
  //    and just compare sviews
  bool starts_with(str &st) const {
    if (st.size() > _size) {
      return false;
    }  // st.size > *this.size, false
    else if (st.size() == _size) {
      return *this == st;
    } else if (st.size() == 0) {
      return true;
    } else {  // if (st._size < *this._size), compare
      for (auto i = 0u; i < st.size(); ++i) {
        if ((*this)[i] != st[i]) {
          return false;
        }
      }
      return true;
    }
  }

  // const char * and std::string will come thru here
  bool starts_with(std::string_view st) const {
    if (st.size() > _size) {
      return false;
    } else if (st.size() == _size) {
      return *this == st;
    } else if (st.size() == 0) {
      return true;
    }
    for (auto i = 0u; i < st.size(); ++i) {
      if ((*this)[i] != st[i]) {
        return false;
      }
    }
    return true;
  }

  // checks if *this pstr ends with en
  bool ends_with(const str &en) const {
    if (en.size() > _size) {
      return false;
    } else if (en.size() == _size) {
      return *this == en;
    } else if (en.size() == 0) {
      return true;
    }
    for (uint32_t j = _size - en.size(), i = 0; j < _size; ++j, ++i) {
      if ((*this)[j] != en[i]) {
        return false;
      }
    }
    return true;
  }

  bool ends_with(std::string_view en) const {
    if (en.size() > _size) {
      return false;
    } else if (en.size() == _size) {
      return *this == en;
    } else if (en.size() == 0) {
      return true;
    }
    // Actual compare logic
    for (uint32_t j = _size - en.size(), i = 0; j < _size; ++j, ++i) {
      if ((*this)[j] != en[i]) {
        return false;
      }
    }
    return true;
  }

  // will use the string_view function
  bool ends_with(std::string en) const { return ends_with(std::string_view(en.c_str())); }

  std::size_t find(const str &v, std::size_t pos = 0) const {
    char first = v[0];
    for (size_t i = ((pos == 0) ? 0 : pos); i < _size; i++) {
      if ((first == (*this)[i]) and ((i + v._size) <= _size)) {
        for (size_t j = i, k = 0; j < i + v._size; j++, k++) {
          if ((*this)[j] != v[k])
            break;
          if (j == (i + v._size - 1))
            return i;
        }
      }
    }
    return -1;
  }

  std::size_t find(char c, std::size_t pos = 0) const {
    if (pos >= _size)
      return -1;
    for (size_t i = pos; i < _size; i++) {
      if ((*this)[i] == c)
        return i;
    }
    return -1;
  }

  template <std::size_t N>
  constexpr std::size_t find(const char (&s)[N], std::size_t pos = 0) {
    return find(str(s), pos);
  }

  // last occurance
  // atatatatatatat at
  std::size_t rfind(const str &v, std::size_t pos = 0) const {
    int position = _size - 1;
    if (pos != 0)
      position = pos;
    char   first    = v[0];
    size_t retvalue = -1;
    for (int i = (_size - v._size); i >= 0; i--) {
      // std::cout << "i is :" << i << std::endl;
      if ((first == (*this)[i]) and ((i + v._size) <= _size)) {
        if (v._size == 1)
          return i;
        size_t k = 0;
        for (int j = i; k < v._size && j < i + v._size; j++, k++) {  // FIXME: is hist k<v && j<i ??
          // std::cout << "J is :" << (*this)[j]  << V[k];
          if ((*this)[j] != v[k])
            break;
          if ((j == (i + v._size - 1)) and (i <= position))
            return i;
        }
      }
    }
    return retvalue;
  }

  std::size_t rfind(char c, std::size_t pos = 0) const {
    int position = _size - 1;
    if (pos != 0)
      position = pos;
    for (int i = position; i >= 0; i--) {
      if ((*this)[i] == c)
        return i;
    }
    return -1;
  }

  template <std::size_t N>
  constexpr std::size_t rfind(const char (&s)[N], std::size_t pos = 0) {
    return rfind(str(s), pos);
  }

  static str concat( str &a, const str &b) { return a.append(b); }

  static str concat(std::string_view a, const str &b) {
    mmap_lib::str temp(a);
    return temp.append(b);
  }

  static str concat( str &a, std::string_view b) { return a.append(b); }

  static str concat( str &a, int v) { return a.append(v); }

  str append(const str &b)  {
    // if _size <= 13, not in map yet
    //   now check _size + v._size
    //   if _size + v._size <= 13, change p_o_s and e
    //   else we add it to vec and map (new string)
    // else if _size > 13
    //   if the string is the last in the vec
    //     add to vec and add to  map and change e
    //   else
    //     DWWDN
    if (_size <= 13) { 
      if ((_size + b._size) <= 13) { // size and b size < = 13
        if (_size <= 3) {
          long unsigned int i = 0; 
          uint8_t e_ptr = _size <= 4 ? 0 : _size - 4;
          for (; i < b._size; ++i) {
            if (_size + i < 4) {
              //std::cout << "adding to p_o_s\n";
              ptr_or_start = (ptr_or_start << 8) | static_cast<uint8_t>(b[i]);
            } else {
              //std::cout << "adding to e\n";
              //std::cout << "e_ptr is: " << static_cast<int>(e_ptr) << std::endl;
              e[e_ptr++] = b[i];
            }
          }
        } else {
          //std::cout << "whoops\n";
          for (auto i = _size-4, j = 0; i< 10 ; ++i, ++j) {
            if (j >= b._size) break;
            e[i] = b[j];
          }
        }
      } else { // size and b size > 13
        // re make the string to be LONG
        std::string hold;
        for (auto i = 0; i < _size; ++i) { hold += (*this)[i]; }
        for (auto i = 0; i < b._size; ++i) { hold += b[i]; }
        str temp = str(hold);
        ptr_or_start = temp.ptr_or_start;
        for (auto i = 0; i < 10; ++i) { e[i] = temp.e[i]; }
      }
    } else {
      if ((ptr_or_start + _size) == string_vector.size()) { // last one inserted
        // add to vector end, change e
        for (auto k = 0; k < b._size; ++k) {
          if (b._size >= 8) {
            for (auto i = 2; i < 10; ++i) { string_vector.emplace_back(e[i]); }
            for (auto i = 0; i < b._size - 8; ++i) { string_vector.emplace_back(b[i]); }
            for (auto i = b._size - 8, j = 2; i < b._size; ++i, ++j) {
             if (j == 10) break;
              e[j] = b[i];
            } 
          } else { // b._size < 8
            for (auto i = 2; i < b._size + 2; ++i) { string_vector.emplace_back(e[i]); } // put e to vec
            auto i = 2;
            for (; i < b._size + 2; ++i) { e[i] = e[i+b._size]; } // overwrite e
            for (auto j = 0; j < b._size; ++i, ++j) { 
              if (i == 10 ) break;
              e[i] = b[j]; 
            } // put b in e
          }
        }
        std::string full_str = this->to_s(); // n
        full_str += b.to_s(); // m
        const char* full_string = full_str.c_str();
        //puting the string into maps
        insertfind(full_string, _size + b._size - 10);
      } else {
        std::string start = this->to_s(); // n
        start += b.to_s(); // m
        str temp_str  = str(start); // n + m
        ptr_or_start = temp_str.ptr_or_start;
        for (int i = 0; i< 10 ; i++) {e[i] = temp_str.e[i];}
      }
    }
    _size += b._size;
    return *this;      
  }

  str append(std::string_view b)  { return append(mmap_lib::str(b)); }

  str append(char c) {
    const char* hold = &c; 
    mmap_lib::str h(hold);
    return append(h);
  }

  str append(int b)  {
    std::string hold = std::to_string(b);
    return append(mmap_lib::str(hold));
  }

  // used as a tokenizing func, return vector of pstr's
  std::vector<str> split(const char chr) {
    std::vector<str> vec;
    std::string      hold;
    // std::string_view hold;

    if (_size == 0) {
      return vec;
    }
    uint8_t track = 0;
    for (auto i = 0; i < _size; ++i) {
      if ((*this)[i] == chr) {
        vec.push_back(mmap_lib::str(hold));
        hold.clear();
        track = 0;
      } else {
        hold += (*this)[i];
        ++track;
      }
    }
    if (hold.size() != 0) {
      vec.push_back(mmap_lib::str(hold));
    }
    return vec;
  }

  bool is_i() const {
    if (_size < 14) {
      int  temp  = (_size >= 4) ? 3 : (_size - 1);
      char first = (ptr_or_start >> (8 * (temp))) & 0xFF;
      if (first != '-' and (first < '0' or first > '9')) {
        return false;
      }
      for (int i = 1; i < (_size > 4 ? 4 : _size); i++) {
        switch ((ptr_or_start >> (8 * (temp - i))) & 0xFF) {
          case '0' ... '9': break;
          default: return false; break;
        }
      }
      for (int i = 0; i < (_size > 4 ? _size - 4 : 0); i++) {
        switch (e[i]) {
          case '0' ... '9': break;
          default: return false; break;
        }
      }
    } else {
      char first = e[0];
      if (first != '-' and (first < '0' or first > '9')) {
        return false;
      }
      for (int i = 1; i < 10; i++) {
        switch (e[i]) {
          case '0' ... '9': break;
          default: return false; break;
        }
      }
      for (int i = ptr_or_start; i < _size - 10; i++) {
        switch (static_cast<int>(string_vector[i])) {
          case '0' ... '9': break;
          default: return false; break;
        }
      }
    }
    return true;
  }

  int64_t to_i() const {  // convert to integer
    if (is_i()) {
      std::string temp = to_s();
      return stoi(temp);
    } else {
      return 0;
    }
  }

  std::string to_s() const {  // convert to string
    std::string out;
    for (auto i = 0; i < _size; ++i) {
      out += (*this)[i];
    }
    return out;
  }

  str get_str_after_last(const char chr) const {
    size_t      val = rfind(chr);
    std::string out;
    if (_size == 0 || val >= static_cast<size_t>(_size - 1)) {
      return str();
    }
    for (size_t i = val + 1; i < _size; i++) {
      out += (*this)[i];
    }
    return str(out);
  }

  str get_str_after_first(const char chr) const {
    size_t      val = find(chr);
    std::string out;
    if (_size == 0 || val >= static_cast<size_t>(_size - 1)) {
      return str();
    }
    for (size_t i = val + 1; i < _size; i++) {
      out += (*this)[i];
    }
    return str(out);
  }

  str get_str_before_last(const char chr) const {
    size_t      val = rfind(chr);
    std::string out;
    if (_size == 0 || val >= static_cast<size_t>(_size)) {
      return str();
    }
    for (size_t i = 0; i < val; i++) {
      out += (*this)[i];
    }
    return str(out);
  }

  str get_str_before_first(const char chr) const {
    size_t      val = find(chr);
    std::string out;
    if (_size == 0 || val >= static_cast<size_t>(_size)) {
      return str();
    }
    for (size_t i = 0; i < val; i++) {
      out += (*this)[i];
    }
    return str(out);
  }

  str substr(size_t start) const { return substr(start, _size - start); }

  str substr(size_t start, size_t end) const {
    if (_size == 0 || start > static_cast<size_t>(_size - 1)) {
      return mmap_lib::str();
    }
    std::string hold;
    size_t      adj_end = (end > (_size - start)) ? (_size - start) : end;  // adjusting end for overflow
    for (auto i = start; i < (start + adj_end); ++i) {
      hold += (*this)[i];
    }
    return mmap_lib::str(hold);
  }
};

//mmap_lib::map<std::string_view, uint32_t> str::string_map2;

}  // namespace mmap_lib
