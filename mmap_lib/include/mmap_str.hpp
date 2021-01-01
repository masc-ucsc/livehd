//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mmap_lib {

class str {
protected:
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

  uint32_t ptr_or_start;  // 4 chars if _size < 14, ptr to mmap otherwise
  std::array<char, 10> e; // last 10 "special <128" characters ending the string
  uint16_t _size;          // 2 bytes

  constexpr bool is_digit(char c) const {
    return c>='0' && c <='9';
  }

public:

  // Must be constexpr to allow fast (constexpr) cmp for things like IDs.
  template<std::size_t N, typename = std::enable_if_t<(N-1)<14>>
    constexpr str(const char(&s)[N]): ptr_or_start(0), e{0}, _size(N-1) { // N-1 because str includes the zero
      auto stop    = _size<4?_size:4;
      for(auto i=0;i<stop;++i) {
        ptr_or_start <<= 8;
        ptr_or_start |= s[i];
      }
      auto e_pos = 0;
      for(auto i=stop;i<_size;++i) {
        assert(s[i]<128); // FIXME: use ptr if so
        if (is_digit(s[i]) && i<_size && is_digit(s[i+1])) {
          uint8_t v = (s[i]-'0')*10+s[i+1]-'0';
          assert(v<100); // 2 digits only
          e[e_pos] = 0x80 | v;
          ++i; // skip one more
        }else{
          e[e_pos] = s[i];
        }
        ++e_pos;
      }
    }

  template<std::size_t N, typename = std::enable_if_t<(N-1)>=14>, typename=void>
    constexpr str(const char(&s)[N]): ptr_or_start(0), e{0}, _size(N-1) { // N-1 because str includes the zero
      ptr_or_start = 0;
      auto e_pos   = 0u;
      for(auto i=(N-1-8);i<N-1;++i) { // 8 (not 10 to allow to grow a bit) last positions
        assert(s[i]<128); // FIXME: use ptr if so
        if (is_digit(s[i]) && i<_size && is_digit(s[i+1])) {
          uint8_t v = (s[i]-'0')*10+s[i+1]-'0';
          assert(v<100); // 2 digits only
          e[e_pos] = 0x80 | v;
          ++i; // skip one more
          --_size;
        }else{
          e[e_pos] = s[i];
        }
        ++e_pos;
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
    return e.end();
  }
#endif

  [[nodiscard]] constexpr std::size_t size() const { return _size; }
  [[nodiscard]] constexpr std::size_t length()   const { return _size; }
  [[nodiscard]] constexpr std::size_t max_size() const { return 65535; }

  [[nodiscard]] constexpr bool empty() const { return 0 == _size; }

  template<std::size_t N>
  constexpr bool operator==(const char(&s)[N]) const {
    return str(s) == *this;
  }

  constexpr bool operator==(const str& rhs) const {
    for(auto i=0u;i<e.size();++i) {
      if (e[i] != rhs.e[i])
        return false;
    }
    return ptr_or_start == ptr_or_start && _size == rhs._size;
  }
  constexpr bool operator!=(const str& rhs) const {
    return !(*this == rhs);
  }

  constexpr char operator[](std::size_t pos) const {
#ifndef NDEBUG
    if (pos >= _size)
      throw std::out_of_range("");
#endif
    if (_size<14) {
      if (pos<4)
        return (ptr_or_start>>(8*(3-pos))) & 0xFF;
      return e[pos-4]; // FIXME: this fails if string has digits like "f33a"
    }

    assert(false); // FIXME for long strings
    return 0;
  }

  bool starts_with(const str &v) const;
  bool starts_with(std::string_view sv) const;
  bool starts_with(std::string s) const;

  bool ends_with(const str &v) const;
  bool ends_with(std::string_view sv) const;
  bool ends_with(std::string s) const;

  std::size_t find(const str &v, std::size_t pos = 0 ) const;
  std::size_t find(char c, std::size_t pos = 0 ) const;
  template<std::size_t N>
  constexpr std::size_t find(const char(&s)[N], std::size_t pos=0) {
    return find(str(s), pos );
  }

  std::size_t rfind(const str &v, std::size_t pos = 0 ) const;
  std::size_t rfind(char c, std::size_t pos = 0 ) const;
  std::size_t rfind(const char *s, std::size_t pos, std::size_t n ) const;
  std::size_t rfind(const char *s, std::size_t pos = 0 ) const;

  static str concat(const str &a, const str &b);
  static str concat(std::string_view a, const str &b);
  static str concat(const str &a, std::string_view b);
  static str concat(const str &a, int v);

  std::vector<str> split(const char chr);

};

}  // namespace mmap_lib
