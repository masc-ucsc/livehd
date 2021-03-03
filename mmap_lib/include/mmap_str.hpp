//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <stdio.h>
#include <stdlib.h>
#include "mmap_map.hpp"


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

  uint32_t             ptr_or_start;  // 4 chars if _size < 14, ptr to mmap otherwise
  std::array<char, 10> e;             // last 10 "special <128" characters ending the string
  uint16_t             _size;         // 2 bytes
  //int                  map_key;
  //using Map = typename mmap_lib::map<uint32_t, uint32_t>;

  constexpr bool is_digit(char c) const { return c >= '0' && c <= '9'; }


public:
  static mmap_lib::map<uint32_t, uint32_t> string_map; //LUT  <our class str, position in vector>
#if 0
  inline static mmap_lib::map<std::string_view, bool> string_map;
#endif

  inline static std::vector<int> string_vector;
  
  // FIXME: This type of constructor is needed to be a constexpr
   /*"================================= "
    "============constructor 1========= "
    "================================== "*/
  template<std::size_t N, typename = std::enable_if_t<(N-1)<14>>
  constexpr str(const char(&s)[N]): ptr_or_start(0), e{0}, _size(N-1) {
    auto stop    = _size<4?_size:4;
    //isptr =  _size<14?false:true;
    for(auto i=0;i<stop;++i) {
      ptr_or_start <<= 8;
      ptr_or_start |= s[i];
    }
    auto e_pos   = 0u;
    for(auto i=stop ;i<N-1;++i) { // 8 (not 10 to allow to grow a bit) last positions
       e[e_pos] = s[i];
      ++e_pos;
    }
  }

  //  "============helper function to check if a string exists========= "
  std::pair<int, int> str_exists(const char *string_to_check, uint32_t size) {
#if 0
    std::string_view sv(string_to_check);
    auto it = string_map.find(sv);
    if (it==string_map.end()) {
      it = string_map.insert(sv,false);
    }
    return std::make_pair(it.first, size);
#endif

    bool vector_flag = true;
    
    // the line below should be constant time
    
    for (auto i = string_map.begin(), end = string_map.end(); i != end; ++i) {
      uint32_t key   = string_map.get_key(i);
      uint16_t value = string_map.get(i);
      if (value != size)
        continue;
      for (int i = 0; i < size; i++) {
        if (string_vector.at(key + i) != string_to_check[i]) {
          vector_flag = false;
          break;
        }
        if (vector_flag)
          return std::make_pair(key, value);
        vector_flag = true;
      }
    }
    return std::make_pair(0, 0);
  }

   /*"================================= "
    "============constructor 2========= "
    "================================== "*/
 
  template<std::size_t N, typename = std::enable_if_t<(N-1)>=14>, typename=void>
  str(const char (&s)[N]) : ptr_or_start(0), e{0}, _size(N - 1) {
    // the first two charactors
    e[0] = s[0];
    e[1] = s[1];
    // the last eight  charactors
    for (int i = 0; i < 8; i++) {
      e[9-i] = s[_size - 1 -i];
    }
    // checking if it exists
    
    char long_str[_size-10];
    for (int i = 0; i < _size - 8; i++) {
      long_str[i] = s[i + 2];
    }
    
    std::pair<int, int> pair = str_exists(long_str, _size - 10);
    if (pair.second) {
      ptr_or_start = pair.first;
    } else {
      for (int i = 0; i < _size - 10; i++) {
        string_vector.push_back(long_str[i]);
      }
      ptr_or_start = string_vector.size()-(_size-10);
      str::string_map.set(ptr_or_start, _size - 10);
    }

  }
  
  

   /*"================================= "
    "============constructor 3========= "
    "================================== "
    sv might not have null terminator
    don't copy one beyond what you need
    keep copying till we find a \0
    */
  str(std::string_view sv) : ptr_or_start(0), e{0}, _size(sv.size()) {
    //claim is to treat it as a normal string 
  	if (_size < 14 ){
		auto stop    = _size<4?_size:4;
	    for(auto i=0;i<stop;++i) {
	      ptr_or_start <<= 8;
	      ptr_or_start |= sv.at(i);
	    }
	    auto e_pos   = 0u;
	    for(auto i=stop;i<_size;++i) { // 8 (not 10 to allow to grow a bit) last positions
	       e[e_pos] = sv.at(i);
	       ++e_pos;
	  	}

  	} else {

  		e[0] = sv.at(0);
	    e[1] = sv.at(1);
	    // the last eight  characters
	    for (int i = 0; i < 8; i++) {
	      e[9-i] = sv.at(_size -1- i); //there is no null terminator in sv
	    }
	    // checking if it exists
	    char long_str[_size-10];
	    for (int i = 0; i < _size - 8; i++) {
	      long_str[i] = sv.at(i + 2);
	    }
	    std::pair<int, int> pair = str_exists(long_str, _size - 10);
	    if (pair.second) {
	      ptr_or_start = pair.first;
	    } else {
	      for (int i = 0; i < _size - 10; i++) {
	        string_vector.push_back(long_str[i]);
	      }
        ptr_or_start = string_vector.size() - (_size-10);
	      str::string_map.set(ptr_or_start, _size - 10);
	    }
      
  	}
 }

  void print_PoS () { 
    std::cout << "This is ptr_or_start: " << std::endl;
    if (_size >= 14) {
      std::cout << "Pointer: " << ptr_or_start << std::endl;
    } else if (_size < 14) {
      std::cout << "Start: ";
      // [first] [sec] [thr] [fourth] 
      for (int i = 3; i >= 0; --i) {
        std::cout << char(ptr_or_start >> (i * 8));
      }
      std::cout << std::endl;
    } 
  }

  void print_e () {
    std::cout << "this is e: ";
    for (int i = 0; i < 10; ++i) { std::cout << e[i] << " "; }
    std::cout << std::endl;
  }

  void print_StrVec () {
    std::cout << "this is string_vector: ";
    /*
    for (std::vector<int>::const_iterator i : string_vector) {
      std::cout << i << std::endl;
    }
    */
    for (std::vector<int>::const_iterator i = string_vector.begin(); i != string_vector.end(); ++i) 
    {
      std::cout << char(*i) << " ";
    }
    std::cout << std::endl;
  }

  void print_key_val_str () {
    std::cout << "Key:  " << "Val:  " << "String:  " << std::endl;
    for (auto it = string_map.begin(), end = string_map.end(); it != end; ++it) {
      uint32_t key   = string_map.get_key(it);
      uint32_t value = string_map.get(it);
      std::cout << key << "   " << value << "   ";
      for (int i = key; i < (key+value); ++i) {
        std::cout << static_cast<char>(string_vector.at(i));
      }
      std::cout << std::endl;
    }
  }

#if 0
  fixme_const_iterator begin()  const {
    for(const auto &ch:data.b) {
      if (ch!=0)
        return &ch;
    }
    if (size<16)
      return &e[0];

    return ptr;
  }
  fixme_const_iterator end()    const {
    if (size<16)
      return &e[size-4];

    for(const auto &ch:e) {
      if (ch==0)
        return &ch;
    }
    return e.end();}
#endif

  [[nodiscard]] constexpr std::size_t size() const { return _size; }
  [[nodiscard]] constexpr std::size_t length() const { return _size; }
  [[nodiscard]] constexpr std::size_t max_size() const { return 65535; }

  [[nodiscard]] constexpr bool empty() const { return 0 == _size; }

  template <std::size_t N>
  constexpr bool operator==(const char (&s)[N]) const {
    return false;  // FIXME
    // return str(s) == *this;
  }

  constexpr bool operator==(const str &rhs) const {
    for (auto i = 0u; i < e.size(); ++i) {
      if (e[i] != rhs.e[i])
        return false;
    }
    return ptr_or_start == ptr_or_start && _size == rhs._size;
  }
  constexpr bool operator!=(const str &rhs) const { return !(*this == rhs); }

  constexpr char operator[](std::size_t pos) const {
#ifndef NDEBUG
    if (pos >= _size)
      throw std::out_of_range("");
#endif
    if (_size < 14) {
      if (pos < 4)
        return (ptr_or_start >> (8 * (3 - pos))) & 0xFF;
      return e[pos - 4];  // FIXME: this fails if string has digits like "f33a"
    }

    assert(false);  // FIXME for long strings
    return 0;
  }

  bool starts_with(const str &v) const;
  bool starts_with(std::string_view sv) const;
  bool starts_with(std::string s) const;

  bool ends_with(const str &v) const;
  bool ends_with(std::string_view sv) const;
  bool ends_with(std::string s) const;

  std::size_t find(const str &v, std::size_t pos = 0) const;
  std::size_t find(char c, std::size_t pos = 0) const;
  template <std::size_t N>
  constexpr std::size_t find(const char (&s)[N], std::size_t pos = 0) {
    return find(str(s), pos);
  }

  std::size_t rfind(const str &v, std::size_t pos = 0) const;
  std::size_t rfind(char c, std::size_t pos = 0) const;
  std::size_t rfind(const char *s, std::size_t pos, std::size_t n) const;
  std::size_t rfind(const char *s, std::size_t pos = 0) const;

  //returns a pstr from two objects (pstr)
  static str concat(const str &a, const str &b);
  static str concat(std::string_view a, const str &b);
  static str concat(const str &a, std::string_view b);
  static str concat(const str &a, int v); // just puts two things together concat(x, b); -> x.append(b)
                                          //                               concat(b, x); -> b.append(x)


  str append(const str &b) const;
  str append(std::string_view b) const;
  str append(int b) const;


  std::vector<str> split(const char chr); // used as a tokenizing func, return vector of pstr's


  /*
  bool is_i() const{ // starts with digit -> is integer
    //this fun works when str size is <14   
    //if(!isptr){
      char chars[5];
      std::cout << "chars[] inside is_i(): ";
      for (int i =3, j=0;i>=0;i--,j++){
         chars[j] = (ptr_or_start >> (i*sizeof(char)*8)) & 0x000000ff;
         std::cout << chars[j];
      } 
      std::cout << std::endl;
      if (chars[0]!='-' and( chars[0]<'0' or chars[0]> '9')) {
        std::cout << "Non-number char detected in ptr_or_start[0]\n";
        return false; 
      }
      for (int i= 1; i<(_size>4?4:_size);i++){
        switch (chars[i]){
          case '0'...'9':
            break;
          default:
            std::cout << "Non-number char detected in ptr_or_start[1:3]\n";
            return false;
            break;
        }
      }
      for (int i=0; i<(_size>4?_size-4:0);i++){
        switch (e[i]){
          case '0'...'9':
            break;
          default:
            std::cout << "Non-number char detected in e\n";
            return false;
            break;
        }
      }
    //}
    return true;  
  } 
  

  // How to handle if it's not an int?
  // what to return/exceptions?
  int64_t to_i() const { // only works if _size < 14
    
    if (this.is_i()) {  
      int64_t hold = 0;
      // convert ptr_or_start first
      // convert e next
    } else {
      return;
    } 
    
  } // convert to integer
*/
  bool        is_i() const;  // starts with digit -> is integer
  int64_t     to_i() const;  // convert to integer
  std::string to_s() const;  // convert to string

  str get_str_after_last(const char chr) const;
  str get_str_after_first(const char chr) const;

  str get_str_before_last(const char chr) const;
  str get_str_before_first(const char chr) const;

  str substr(size_t start) const;
  str substr(size_t start, size_t end) const;
};

//For static string_map
mmap_lib::map<uint32_t, uint32_t> str::string_map;

}  // namespace mmap_lib
