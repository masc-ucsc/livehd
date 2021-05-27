//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "mmap_map.hpp"
#include "mmap_vector.hpp"

#define append_debug 0

namespace mmap_lib {

template <int map_id>
class __attribute__((packed)) str {
protected:
  // Some references:
  // https://github.com/tcsullivan/constexpr-to-string
  // https://github.com/vesim987/constexpr_string/blob/master/constexpr_string.hpp
  // https://github.com/vivkin/constexprhash/blob/master/constexprhash.h
  // https://github.com/unterumarmung/fixed_string
  // https://github.com/proxict/constexpr-string
  // https://github.com/tonypilz/ConstexprString
  //
  // 16 bytes data structure:
  // ptr_or_start: 4 bytes
  // e:            10 bytes
  // _size:        2 bytes
  //
  // This avoid having a "size" in the costly str in-place data. The overflow
  // can have a string size like a string_view does

  uint32_t             ptr_or_start;  // 4 chars if _size < 14, ptr to string_vector otherwise
  std::array<char, 10> e;             // last 10 chars of string, or first 2 + last 8 of string
  uint16_t             _size;         // string length
  constexpr bool       is_digit(char c) const { return c >= '0' && c <= '9'; }
  constexpr uint8_t    posShifter(uint8_t s) const { return s < 4 ? (s - 1) : 3; }
  constexpr uint8_t    posStopper(uint8_t s) const { return s < 4 ? s : 4; }
  constexpr char       isol8(uint32_t ps, uint8_t s) const { return (ps >> (s * 8)) & 0xff; }
  constexpr uint32_t   l8(uint32_t size, uint32_t i) const { return i + 10 - size; }

  static mmap_lib::map<std::string_view, bool> m0, m1, m2, m3;

public:
  str() : ptr_or_start(0), e{0}, _size(0) {}       // constructor 0 (empty obj)
  str(char c) : ptr_or_start(0), e{0}, _size(1) {  // constructor 0.5 (single char)
    ptr_or_start <<= 8;
    ptr_or_start |= static_cast<uint8_t>(c);
  }

  // constructor 1 (_size <= 13)
  template <std::size_t N, typename = std::enable_if_t<(N - 1) < 14>>
  constexpr str(const char (&s)[N]) : ptr_or_start(0), e{0}, _size(N - 1) {
    auto stop = posStopper(_size);  // if _size < 4, whole string is in ptr_or_start
    // ptr_or_start will hold first 4 chars
    // | first | second | third | forth |
    // 31      23       15      7       0
    for (auto i = 0; i < stop; ++i) {
      ptr_or_start <<= 8;
      ptr_or_start |= s[i];
    }
    auto e_pos = 0u;
    for (auto i = stop; i < N - 1; ++i) {
      e[e_pos] = s[i];
      ++e_pos;
    }
  }

  mmap_lib::map<std::string_view, bool> &map_ref() {
    return map_id == 1 ? m1 : map_id == 2 ? m2 : map_id == 3 ? m3 : m0;
  }

  mmap_lib::map<std::string_view, bool> &map_cref() const {
    return map_id == 1 ? m1 : map_id == 2 ? m2 : map_id == 3 ? m3 : m0;
  }

  int get_map_id() const { return map_id; }

  static void clear_map() { 
    map_id == 1 ? m1.clear() : map_id == 2 ? m2.clear() : map_id == 3 ? m3.clear() : m0.clear(); 
  }


  //=====helper function to check if a string exists in string_vector=====
  // If the string being searched exists inside the map, then return info about string
  // If the string being searched does not exist, then the function inserts string into map
  uint32_t insertfind(const char *string_to_check, uint32_t size) {
    std::string_view sv(string_to_check);
    auto             it = map_ref().find(sv.substr(0, size));
    if (it == map_ref().end()) {
      //<std::string_view, uint32_t(position in vec)> string_map2
      auto rec = map_ref().set(sv.substr(0, size), 0);
      return static_cast<uint32_t>(rec->first);
    } else { 
      return static_cast<uint32_t>(it->first);
      // pair is (ptr_or_start, size of string)
    }
  }

  //==========constructor 2 (_size >= 14) ==========
  template <std::size_t N, typename = std::enable_if_t<(N - 1) >= 14>, typename = void>
  str(const char (&s)[N]) : ptr_or_start(0), e{0}, _size(N - 1) {
    e[0] = s[0];
    e[1] = s[1];
    for (int i = 0; i < 8; i++) {
      e[9 - i] = s[_size - 1 - i];
    }
    char long_str[_size - 10];
    for (int i = 0; i < (_size - 10); ++i) {
      long_str[i] = s[i + 2];
    }
    // pair is (ptr_or_start, size of string)
    ptr_or_start = insertfind(long_str, _size - 10);
  }

  //============constructor 3=============
  // const char* and std::string will go through this one
  // implicit conversion from const char* --> string_view
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
      for (int i = 0; i < 8; i++) {
        e[9 - i] = sv.at(_size - 1 - i);
      }
      char long_str[_size - 10];
      for (int i = 0; i < _size - 10; i++) {
        long_str[i] = sv.at(i + 2);
      }
      ptr_or_start = insertfind(long_str, _size - 10);
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

  void print_StrMap() const {
    std::cout << "StrMap{ ";
    for (auto it = map_cref().begin(), end = map_cref().end(); it != end; ++it) {
      std::string key   = std::string(map_cref().get_key(it));
      bool    value = map_cref().get(it);
      std::cout << "<" << key << ", " << value << "> ";
    }
    std::cout << "}" << std::endl;
  }

  void print_string() const {
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
      std::cout << map_cref().get_sview(ptr_or_start).substr(0, _size);
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
  [[nodiscard]] constexpr uint32_t    get_pos() const { return ptr_or_start; }
  [[nodiscard]] constexpr char        get_e(uint8_t i) const { return e[i]; }
  [[nodiscard]] constexpr std::size_t e_size() const { return e.size(); }


  template <int m_id>
  constexpr bool operator==(const str<m_id> &rhs) const {
    if (get_map_id() == rhs.get_map_id() || (_size < 14 && rhs.size() < 14)) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
      const uint64_t *a = (const uint64_t *)(this);
      const uint64_t *b = (const uint64_t *)(&rhs);
#pragma GCC diagnostic pop
      return a[0] == b[0] && a[1] == b[1];  // 16byte compare
    } else {
      //FIXME: cross template compare
      // compare first 2
      for (uint8_t i = 0; i < 10; ++i) {
        if (e[i] != rhs.get_e(i)) {
          return false;
        }
      }
      if (map_cref().get_sview(ptr_or_start) == rhs.map_cref().get_sview(rhs.get_pos())) {
        return true;
      } else {
        return false;
      }
    }
  }
  
  // const char* and std::string will go through this one
  bool operator==(std::string_view rhs) const { return (*this == str<map_id>(rhs)); }
  
  template <std::size_t N>
  bool operator==(const char (&rhs)[N]) const { return (*this == str<map_id>(rhs)); }

  template<int m_id>
  constexpr bool operator!=(const str<m_id> &rhs) const { return !(*this == rhs); }

  bool operator!=(std::string_view rhs) const { return !(*this == rhs); }
 
  template <std::size_t N>
  constexpr bool operator!=(const char (&rhs)[N]) const { return !(*this == rhs); }


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
        if (l8(_size, pos) >= e.size()) {
          this->print_string();
          std::cout << "\npos = " << pos << std::endl;
          std::cout << "_size is: " << _size << std::endl;
          std::cout << "l8 is: " << l8(_size, pos) << std::endl;
          throw std::out_of_range("[] operator out of range.");
        } else {
          return e[l8(_size, pos)];
        }
      } else {
        return map_cref().get_sview(ptr_or_start)[pos - 2];
        //return vec_cref()[mid(ptr_or_start, pos)];
      }
    }
  }

  template<int m_id>
  bool starts_with(str<m_id> st) const {
    if (st.size() > _size) {
      return false;
    } else if (st.size() == _size) {
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


  // const char * and std::string will come through here
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
  template<int m_id>
  bool ends_with(str<m_id> &en) const {
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

  template<int m_id>
  std::size_t find(const str<m_id> &v, std::size_t pos = 0) const {
    char first = v[0];
    for (size_t i = ((pos == 0) ? 0 : pos); i < _size; i++) {
      if ((first == (*this)[i]) and ((i + v.size()) <= _size)) {
        for (size_t j = i, k = 0; j < i + v.size(); j++, k++) {
          if ((*this)[j] != v[k])
            break;
          if (j == (i + v.size() - 1))
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
    return find(str<map_id>(s), pos);
  }

  template<int m_id>
  std::size_t rfind(const str<m_id> &v, std::size_t pos = 0) const {
    int position = _size - 1;
    if (pos != 0)
      position = pos;
    char   first    = v[0];
    size_t retvalue = -1;
    for (int i = (_size - v.size()); i >= 0; i--) {
      if ((first == (*this)[i]) and ((i + v.size()) <= _size)) {
        if (v.size() == 1)
          return i;
        size_t k = 0;
        for (int j = i; k < v.size() && j < i + v.size(); j++, k++) { 
          if ((*this)[j] != v[k])
            break;
          if ((j == (i + v.size() - 1)) and (i <= position))
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
    return rfind(str<map_id>(s), pos);
  }
  
  std::string to_s() const {  // convert to string
    std::string out;
    for (auto i = 0; i < _size; ++i) {
      out += (*this)[i];
    }
    return out;
  }
  
  int64_t to_i() const {  // convert to integer
    if (is_i()) {
      std::string temp = to_s();
      return stoi(temp);
    } else {
      return 0;
    }
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
      auto my_sv = map_cref().get_sview(ptr_or_start);
      for (int i = 0; i < _size - 10; i++) {
        switch (static_cast<int>(my_sv[i])) {
          case '0' ... '9': break;
          default: return false; break;
        }
      }
    }
    return true;
  }
  
  template<int m_id>
  str append(const str<m_id> &b) {
    if (_size <= 13) {
      if ((_size + b.size()) <= 13) {  // size and b size < = 13
        if (_size <= 3) {
          long unsigned int i     = 0;
          uint8_t           e_ptr = _size <= 4 ? 0 : _size - 4;
          for (; i < b.size(); ++i) {
            if (_size + i < 4) {
              ptr_or_start = (ptr_or_start << 8) | static_cast<uint8_t>(b[i]);
            } else {
              e[e_ptr++] = b[i];
            }
          }
        } else {
          for (auto i = _size - 4, j = 0; i < 10; ++i, ++j) {
            if (static_cast<uint8_t>(j) >= b.size())
              break;
            e[i] = b[j];
          }
        }
      } else {  // size and b size > 13
        char    full[_size + b.size() - 10];
        uint8_t indx = 2, b_indx = 0, e_indx = 2;
        if (b.size() > 8) {
          uint32_t i = 0;
          for (; i < _size + b.size() - 10; ++i) {
            if (indx <= _size - 1) {
              full[i] = (*this)[indx++];
            } else {
              full[i] = b[b_indx++];
            }
          }
          for (; i < _size + b.size() - 2; ++i) {
            e[e_indx++] = b[b_indx++];
          }
          e[0] = (*this)[0];
          e[1] = (*this)[1];
        } else if (b.size() < 8) {
          uint32_t i = 0;
          for (; i < _size + b.size() - 10; ++i) {
            full[i] = (*this)[indx++];
          }
          for (; i < _size + b.size() - 2; ++i) {
            if (indx <= _size - 1) {
              e[e_indx++] = (*this)[indx++];
            } else {
              e[e_indx++] = b[b_indx++];
            }
          }
          e[0] = (*this)[0];
          e[1] = (*this)[1];
        } else if (b.size() == 8) {
          uint32_t i = 0;
          for (; i < _size + b.size() - 10; ++i) {
            full[i] = (*this)[indx++];
          }
          for (; i < _size + b.size() - 2; ++i) {
            e[e_indx++] = b[b_indx++];
          }
          e[0] = (*this)[0];
          e[1] = (*this)[1];
        }
        ptr_or_start = insertfind(full, _size + b.size() - 10);
      }
    } else {
      std::string start = this->to_s();           // n
      start += b.to_s();                          // m
      str<map_id> temp_str(start);  // n + m
      ptr_or_start         = static_cast<uint32_t>(temp_str.get_pos());
      for (uint8_t i = 0; i < static_cast<uint8_t>(temp_str.e_size()); i++) {
        e[i] = temp_str.get_e(i);
      }
    }
    _size += b.size();
    return *this;
  }

  str append(std::string_view b) { return append(mmap_lib::str<map_id>(b)); }

  str append(char c) {
    mmap_lib::str<map_id> h(c);
    return append(h);
  }

  str append(int b) {
    std::string hold = std::to_string(b);
    return append(mmap_lib::str<map_id>(hold));
  }

  // str created from this will be whichever template we call this func from
  template<int m_id, int m_id2>
  static str concat(str<m_id> &a, const str<m_id2> &b) { 
    str<map_id> ret(a.to_s() + b.to_s());
    return  ret;
  }

  template<int m_id>
  static str concat(std::string_view a, const str<m_id> &b) {
    str<map_id> temp(a);
    return temp.append(b);
  }

  template<int m_id>
  static str concat(str<m_id> &a, std::string_view b) { 
    str<map_id> temp(a.to_s() + b);  
    return temp;
  }

  template<int m_id>
  static str concat(str<m_id> &a, int v) { 
    str<map_id> temp(a.to_s() + std::to_string(v));
    return temp;
  }


  // used as a tokenizing function
  // all str's in the vec will be same template as original str
  std::vector<str> split(const char chr) {
    std::vector<str> vec;
    std::string      hold;

    if (_size == 0) {
      return vec;
    }
    uint8_t track = 0;
    for (auto i = 0; i < _size; ++i) {
      if ((*this)[i] == chr) {
        vec.push_back(mmap_lib::str<map_id>(hold));
        hold.clear();
        track = 0;
      } else {
        hold += (*this)[i];
        ++track;
      }
    }
    if (hold.size() != 0) {
      vec.push_back(mmap_lib::str<map_id>(hold));
    }
    return vec;
  }

  // str created from these will have same template as original str
  str get_str_after_last(const char chr) const {
    size_t      val = rfind(chr);
    std::string out;
    if (_size == 0 || val >= static_cast<size_t>(_size - 1)) {
      return str<map_id>();
    }
    for (size_t i = val + 1; i < _size; i++) {
      out += (*this)[i];
    }
    return str<map_id>(out);
  }

  str get_str_after_first(const char chr) const {
    size_t      val = find(chr);
    std::string out;
    if (_size == 0 || val >= static_cast<size_t>(_size - 1)) {
      return str<map_id>();
    }
    for (size_t i = val + 1; i < _size; i++) {
      out += (*this)[i];
    }
    return str<map_id>(out);
  }

  str get_str_before_last(const char chr) const {
    size_t      val = rfind(chr);
    std::string out;
    if (_size == 0 || val >= static_cast<size_t>(_size)) {
      return str<map_id>();
    }
    for (size_t i = 0; i < val; i++) {
      out += (*this)[i];
    }
    return str<map_id>(out);
  }

  str get_str_before_first(const char chr) const {
    size_t      val = find(chr);
    std::string out;
    if (_size == 0 || val >= static_cast<size_t>(_size)) {
      return str<map_id>();
    }
    for (size_t i = 0; i < val; i++) {
      out += (*this)[i];
    }
    return str<map_id>(out);
  }



  // str created from this function will be same template as original str
  str substr(size_t start) const { return substr(start, _size - start); }

  str substr(size_t start, size_t end) const {
    if (_size == 0 || start > static_cast<size_t>(_size - 1)) {
      return mmap_lib::str<map_id>();
    }
    std::string hold;
    size_t      adj_end = (end > (_size - start)) ? (_size - start) : end;  // adjusting end for overflow
    for (auto i = start; i < (start + adj_end); ++i) {
      hold += (*this)[i];
    }
    return mmap_lib::str<map_id>(hold);
  }

};

}  // namespace mmap_lib
