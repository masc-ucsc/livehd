//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "mmap_gc.hpp"

#define append_debug 0

namespace mmap_lib {

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
  // size_ctrl   : 4 bytes
  // ptr_or_start: 4 bytes
  // start       : 8 bytes
  //
  // empty   :  size_ctrl == 1
  //
  // SSO mode: (size_ctrl & 1) == 1
  // size = ((size_ctrl>>1) & 0xF)
  //
  // large mode:
  // size = ((size_ctrl>>1)
  //
  // size=0 size_ctrl=1 ""
  // size=1 size_ctrl=3 "1"
  // size=2 size_ctrl=5 "12"
  // size=3 size_ctrl=7 "123"
  // size=4 size_ctrl=9 "1234"
  // ...
  // size=14 size_ctrl=29 "1234_5678_9abc_de"
  // size=15 size_ctrl=31 "1234_5678_9abc_def"
  //
  // size=16 size_ctrl=32 "1"    ptr = "234_5678_9abc_defg"   add_tail
  // size=17 size_ctrl=34 "12"   ptr = "34_5678_9abc_defg_j"  add_tail
  // size=18 size_ctrl=36 "123"  ptr = "4_5678_9abc_defg_jk"  add_tail
  // size=19 size_ctrl=38 "0123" ptr = "4_5678_9abc_defg_jk"  add_head('0')
  //
  // NOTE: large mode is optimized for head operations (not tail). This is
  // because this is more common in LiveHD (check '__', drop '$'...)

  uint32_t size_ctrl;
  uint32_t ptr_or_start;
  uint64_t data_storage;

  char *data() {
    assert(size_ctrl & 1);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    char *base = (char *)(this);
#pragma GCC diagnostic pop
    return base + 1;
  }
  const char *data() const {
    assert(size_ctrl & 1);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    char *base = (char *)(this);
#pragma GCC diagnostic pop
    return base + 1;
  }
  char *ref_base_sso() {
    assert(size_ctrl & 1);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    char *base = (char *)(this);
#pragma GCC diagnostic pop
    return base + 1;
  }

  constexpr const char *ref_base_sso() const {
    assert(size_ctrl & 1);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    const char *base = (const char *)(this);
#pragma GCC diagnostic pop
    return base + 1;
  }

  char *ref_data() {
    assert(!(size_ctrl & 1));  // not SSO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    char *base = (char *)(&data_storage);
#pragma GCC diagnostic pop
    return base;
  }

  const char *ref_data() const {
    assert(!(size_ctrl & 1));  // not SSO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    const char *base = (const char *)(&data_storage);
#pragma GCC diagnostic pop
    return base;
  }

  inline constexpr bool is_sso() const { return size_ctrl & 1; }
  inline constexpr bool is_sso_max() const { return (size_ctrl & 0xFF) == 31; }
  inline constexpr bool is_sso_with_space() const { return is_sso() && size_ctrl < 31; }

  inline constexpr size_t get_n_data_chars() const {
    assert(!is_sso());
    return 1 + ((size_ctrl & 0xF) >> 1);
  }

  struct Map_entry {
    uint32_t ptr;
    uint16_t size;
  };

  struct Table {
    std::string name;
    char       *base;
    uint32_t   *size;
    uint32_t    capacity;

    Table() : base(nullptr), size(nullptr), capacity(0) {}

    bool gc_done(void *base2, bool force_recycle) const noexcept {
      (void)base2;
      (void)force_recycle;

      assert(base - 4 == base2);
      return false;  // No GC the common STR maps
    }

    void setup(const std::string &new_name) {
      if (base) {
        assert(name == new_name);
        return;
      }
      name = new_name;
      assert(base == nullptr);

      auto fd = mmap_gc::open(name);

      uint32_t n_entries;
      int      sz = read(fd, &n_entries, 4);
      if (sz != 4) {
        n_entries = 4096;
      } else if (sz < 4096) {
        n_entries = 4096;
      }

      auto   gc_func = std::bind(&Table::gc_done, this, std::placeholders::_1, std::placeholders::_2);
      void  *map_ptr;
      size_t map_sz;
      std::tie(map_ptr, map_sz) = mmap_gc::mmap(name, fd, n_entries, gc_func);

      base     = static_cast<char *>(map_ptr) + 4;
      size     = static_cast<uint32_t *>(map_ptr);
      capacity = map_sz;
    }

    uint32_t insert_entry(const void *data, uint32_t sz) {
      assert(base);

      const void *data_local = data;

      if (MMAP_LIB_UNLIKELY((*size + sz + 16) >= capacity)) {
        // WARNING: data could be in the mmap, so create a copy
        void *ptr = alloca(sz);
        memcpy(ptr, data, sz);
        data_local = ptr;

        auto  new_capacity = capacity + 64 * 1024;
        void *base2;
        std::tie(base2, new_capacity) = mmap_gc::remap(name, static_cast<void *>(size), capacity, new_capacity);
        capacity                      = new_capacity;
        base                          = reinterpret_cast<char *>(base2) + 4;
        size                          = reinterpret_cast<uint32_t *>(base2);

        assert((*size + sz + 16) < capacity);
      }

      auto pos = *size;

      memcpy(base + *size, data_local, sz);

      *size += sz;

      return pos;
    }

    const char *get_data(uint32_t pos) const {
      assert(pos < *size);
      return &base[pos];
    }

    void nuke() {
      if (base == nullptr)
        return;

      mmap_gc::delete_file(size);
      base = nullptr;
    }

    size_t get_size() const { return *size; }
  };

  struct Pool {
    std::string name;
    Table       key2sv_vector;
    Table       map_vector;
    std::mutex  pool_mutex;

    absl::flat_hash_map<std::string, uint32_t> map;

    uint32_t add_new_sv(std::string_view sv) {
      std::lock_guard<std::mutex> guard(pool_mutex);

      auto it = map.find(sv);
      if (MMAP_LIB_UNLIKELY(it != map.end())) { // re-check inside the lock
        return it->second;
      }

      auto key = key2sv_vector.insert_entry(sv.data(), sv.size());

      // WARNING: key2sv_vector can trigger a remap, and sv.data is wrong. Just use the new ptr
      map.emplace(std::string(key2sv_vector.get_data(key), sv.size()), key);

      Map_entry me;
      me.ptr  = key;
      me.size = key2sv_vector.get_size() - key;

      map_vector.insert_entry(&me, sizeof(Map_entry));

      return key;
    }

    std::string_view get_sv(uint32_t pos, uint16_t sz) const {
      const auto *data = key2sv_vector.get_data(pos);
      return std::string_view(data, sz);
    }

    char get_char(uint32_t pos) const {
      const char *data = key2sv_vector.get_data(pos);
      return *data;
    }

    const char *get_char_ptr(uint32_t pos) const { return key2sv_vector.get_data(pos); }

    void nuke() {
      map.clear();
      key2sv_vector.nuke();
      map_vector.nuke();
    }

    void setup(const std::string &new_name) {
      name = new_name;

      key2sv_vector.setup(absl::StrCat(new_name, "_kv"));
      map_vector.setup(absl::StrCat(new_name, "_mv"));

      Map_entry *me_ptr_end = reinterpret_cast<Map_entry *>(map_vector.base + map_vector.get_size());

      char *key2sv_ptr = static_cast<char *>(key2sv_vector.base);

      for (Map_entry *me_ptr = reinterpret_cast<Map_entry *>(map_vector.base); me_ptr < me_ptr_end; ++me_ptr) {
        if (me_ptr->size == 0)
          break;

        auto key_id   = me_ptr->ptr;
        auto key_size = me_ptr->size;

        assert((key_id + key_size) <= key2sv_vector.get_size());

        std::string_view sv(&key2sv_ptr[key_id], key_size);

        map.emplace(sv, key_id);
      }
    }

    uint32_t insert_find(const char *string_to_check, uint32_t size) {
      std::string_view sv(string_to_check, size);

      auto it = map.find(sv);
      if (it == map.end()) {
        return add_new_sv(sv);
      }
      return it->second;
    }
  };

  static inline Pool pool;

  static inline const Pool &ref() { return pool; }  // API to future multi-map
  static inline Pool       &mut_ref() { return pool; }

  constexpr inline void set_sso(const char *s, size_t sz) {
    assert(sz < 16);

    size_ctrl    = (sz << 1) | 1;
    ptr_or_start = 0;
    data_storage = 0;

    if (sz < 3) {
      for (auto i = 0u; i < sz; ++i) {
        uint64_t val = static_cast<unsigned char>(s[i]);
        size_ctrl |= val << (8 * (i + 1));
      }
    } else if (sz < 7) {
      for (auto i = 0u; i < 3; ++i) {
        uint64_t val = static_cast<unsigned char>(s[i]);
        size_ctrl |= val << (8 * (i + 1));
      }
      for (auto i = 3u; i < sz; ++i) {
        uint64_t val = static_cast<unsigned char>(s[i]);
        ptr_or_start |= val << (8 * (i - 3));
      }
    } else {
      for (auto i = 0u; i < 3; ++i) {
        uint64_t val = static_cast<unsigned char>(s[i]);
        size_ctrl |= val << (8 * (i + 1));
      }
      for (auto i = 3u; i < 7; ++i) {
        uint64_t val = static_cast<unsigned char>(s[i]);
        ptr_or_start |= val << (8 * (i - 3));
      }
      for (auto i = 7u; i < sz; ++i) {
        uint64_t val = static_cast<unsigned char>(s[i]);
        data_storage |= val << (8 * (i - 7));
      }
    }
  }

  constexpr char get_sso(size_t sz) const {
    assert(sz < 16);

    char ch = 0;

    if (sz < 3) {
      ch = size_ctrl >> (8 * (sz + 1));
    } else if (sz < 7) {
      ch = ptr_or_start >> (8 * (sz - 3));
    } else {
      ch = data_storage >> (8 * (sz - 7));
    }

    return ch;
  }

  void set_non_sso(const char *s, size_t sz) {
    assert(sz >= 16);

    size_ctrl    = sz << 1;
    ptr_or_start = 0;
    data_storage = 0;

    auto ptr_offset = get_n_data_chars();

    for (auto i = 0u; i < ptr_offset; ++i) {
      uint64_t d = static_cast<unsigned char>(s[i]);
      data_storage |= d << (8 * i);
    }

    // WARNING: insert_find last because it can remap the *s
    ptr_or_start = mut_ref().insert_find(s + ptr_offset, size() - ptr_offset);
  }

  size_t fill_txt(size_t skip_bytes, char *txt) const {
    // WARNING: MAKE SURE THAT txt is big enough

    if (skip_bytes >= size())
      return 0;

    if (is_sso()) {
      const auto *base = ref_base_sso();
      auto        n    = size() - skip_bytes;
      memcpy(txt, base + skip_bytes, n);
      return n;
    }

    auto n = 0u;

    auto n_data = get_n_data_chars();

    if (skip_bytes < n_data) {
      const auto *base = ref_data();
      auto        sz   = n_data - skip_bytes;
      memcpy(txt, base + skip_bytes, sz);
      n += sz;
      skip_bytes = 0;  // all the bytes skipped already
    } else {
      skip_bytes -= n_data;
    }

    if ((skip_bytes + n) < size()) {
      const auto *base = ref().get_char_ptr(ptr_or_start);
      auto        sz   = size() - (n_data + skip_bytes);
      memcpy(txt + n, base + skip_bytes, sz);
      n += sz;
    }

    return n;
  }

  constexpr str(uint64_t a, uint64_t b) : size_ctrl(a), ptr_or_start(a >> 32), data_storage(b) {}

  template <typename T>
  [[nodiscard]] std::size_t rfind_int(const T v, std::size_t pos, size_t sz) const {
    int position = size() - 1;
    if (pos != 0)
      position = pos;
    char first = v[0];
    for (int i = (size() - sz); i >= 0; i--) {
      if ((first == (*this)[i]) && ((i + sz) <= size())) {
        if (sz == 1)
          return i;
        size_t k = 0;
        for (int j = i; k < sz && j < static_cast<int>(i + sz); j++, k++) {
          if ((*this)[j] != v[k])
            break;
          if ((j == static_cast<int>(i + sz - 1)) && (i <= position))
            return i;
        }
      }
    }
    return std::string::npos;
  }

  template <typename T>
  [[nodiscard]] std::size_t find_int(const T v, std::size_t pos, size_t sz) const {
    pos = find(v[0], pos);
    if (pos >= size())
      return std::string::npos;

    char first = v[0];
    for (size_t i = pos; i < size(); i++) {
      if ((first == (*this)[i]) && ((i + sz) <= size())) {
        for (size_t j = i, k = 0; j < i + sz; j++, k++) {
          if ((*this)[j] != v[k])
            break;
          if (j == (i + sz - 1))
            return i;
        }
      }
    }

    return std::string::npos;
  }

  template <typename T>
  [[nodiscard]] str append_array(const T b) const {
    auto new_size = size() + b.size();
    if (new_size < 16) {
      str s{*this};

      char *base = s.ref_base_sso() + s.size_sso();
      memcpy(base, b.data(), b.size());

      s.size_ctrl >>= 1;
      s.size_ctrl += b.size();
      s.size_ctrl <<= 1;
      s.size_ctrl |= 1;

      return s;
    }

    auto n = (new_size & 0x7) + 1;  // bytes in data

    str s;
    s.size_ctrl = new_size << 1;

    auto bytes_from_a = 0u;
    auto bytes_from_b = 0u;
    for (auto i = 0u; i < n; ++i) {
      uint64_t v;
      if (size() > i) {
        ++bytes_from_a;
        v = (*this)[i];
      } else {
        v = b[i - size()];
        ++bytes_from_b;
      }

      v <<= (i * 8);
      s.data_storage |= v;
    }

    assert(n == (bytes_from_a + bytes_from_b));

    char txt[new_size - n + 1];
    auto pos = 0u;

    pos += fill_txt(bytes_from_a, txt);
    if constexpr (std::is_same_v<T, mmap_lib::str>) {
      pos += b.fill_txt(bytes_from_b, txt + pos);
    } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
      memcpy(txt + pos, b.data() + bytes_from_b, b.size() - bytes_from_b);
      pos += b.size() - bytes_from_b;
    } else {
      assert(false);  // only mmap_lib::str/string/string_view objects to append
    }

    assert(pos == new_size - n);  // no zero

    s.ptr_or_start = mut_ref().insert_find(txt, pos);

    return s;
  }

public:
  constexpr str() : size_ctrl(1), ptr_or_start(0), data_storage(0) {}
#if 0
  constexpr str(char c) : size_ctrl((static_cast<uint32_t>(c)<<8) | 3), ptr_or_start(0), data_storage(0) {
  }
#endif

  static void setup() { mut_ref().setup("lgdb_strmap"); }

  static void nuke() { mut_ref().nuke(); }

  template <std::size_t N>
  inline constexpr str(const char (&s)[N]) : size_ctrl(0), ptr_or_start(0), data_storage(0) {
    if constexpr ((N - 1) <= 15)
      set_sso(s, (N - 1));
    else
      set_non_sso(s, (N - 1));
  }

  str(int64_t v) {
    std::array<char, 15> str2;
    auto [ptr, ec] = std::to_chars(str2.data(), str2.data() + str2.size(), v, 10);
    (void)ec;
    set_sso(str2.data(), ptr - str2.data());
  }

  // Explicit creators
  explicit str(size_t sz, char c) {
    std::string s(sz, c);
    if (s.size() <= 15) {
      set_sso(s.data(), s.size());
    } else {
      set_non_sso(s.data(), s.size());
    }
  }

  str(const std::string_view sv) {
    if (sv.size() <= 15) {
      set_sso(sv.data(), sv.size());
    } else {
      set_non_sso(sv.data(), sv.size());
    }
  }

  explicit str(const char *txt, size_t sz) {
    if (sz <= 15) {
      set_sso(txt, sz);
    } else {
      set_non_sso(txt, sz);
    }
  }

  explicit str(void *txt, size_t sz) {
    if (sz <= 15) {
      set_sso(static_cast<const char *>(txt), sz);
    } else {
      set_non_sso(static_cast<const char *>(txt), sz);
    }
  }

#if 0
  // ambiguity
  str &operator=(const std::string &s) {
    if (s.size()<=15)
      set_sso(s.data(),s.size()-1);
    else
      set_non_sso(s.data(),s.size()-1);

    return *this;
  }
#endif

  [[nodiscard]] constexpr std::size_t size_sso() const {
    assert(size_ctrl & 1);
    return (size_ctrl >> 1) & 0xF;
  }
  [[nodiscard]] constexpr std::size_t size() const { return size_ctrl & 1 ? (size_ctrl >> 1) & 0xF : (size_ctrl >> 1); }
  [[nodiscard]] constexpr std::size_t max_size() const { return 65535; }
  [[nodiscard]] constexpr bool        empty() const { return size_ctrl == 1; }

  constexpr bool operator==(const str rhs) const {
    return size_ctrl == rhs.size_ctrl && ptr_or_start == rhs.ptr_or_start && data_storage == rhs.data_storage;
  }
  bool operator<(const str rhs) const {
    if (is_sso() && rhs.is_sso()) {
      std::string_view lhs_sv(ref_base_sso(), size());
      std::string_view rhs_sv(rhs.ref_base_sso(), rhs.size());

      return lhs_sv < rhs_sv;
    }

    if ((*this)[0] == rhs[0])
      return to_s() < rhs.to_s();

    return (*this)[0] < rhs[0];
  }

  bool operator==(const std::string_view rhs) const {
    const auto sz = size();
    if (rhs.size() != sz)
      return false;

    if (sz < 16) {
      std::string_view self_sv(ref_base_sso(), sz);
      return self_sv == rhs;
    }

    auto ptr_offset = get_n_data_chars();

    uint64_t rhs_data = 0;
    for (auto i = 0u; i < ptr_offset; ++i) {
      uint64_t d = rhs[i];
      rhs_data |= d << (8 * i);
    }
    if (data_storage != rhs_data)
      return false;

    std::string_view self_end_sv = ref().get_sv(ptr_or_start, sz - ptr_offset);
    if (self_end_sv != rhs.substr(ptr_offset))
      return false;
    return true;
  }

  template <std::size_t N>
  constexpr bool operator==(const char (&rhs)[N]) const {
    if constexpr ((N - 1) <= 15) {
      str str_rhs(rhs, (N - 1));  // SSO constexpr optimized
      return size_ctrl == str_rhs.size_ctrl && ptr_or_start == str_rhs.ptr_or_start && data_storage == str_rhs.data_storage;
    } else {
      return *this == std::string_view(rhs, N - 1);
    }
  }

  constexpr bool operator!=(const str rhs) const { return !(*this == rhs); }

  bool operator!=(std::string_view rhs) const { return !(*this == rhs); }

  template <std::size_t N>
  constexpr bool operator!=(const char (&rhs)[N]) const {
    return !(*this == rhs);
  }

  [[nodiscard]] constexpr char front() const {
    assert(size());
    if (is_sso()) {
      return (size_ctrl >> 8) & 0xFF;
    }
    return data_storage & 0xFF;
  }

  [[nodiscard]] char back() const {
    assert(size());
    return (*this)[size() - 1];
  }

  char operator[](std::size_t pos) const {
    assert(pos < size());  // throw std::out_of_range("[] operator out of range.");

    if (is_sso()) {
      const char *base = ref_base_sso();
      return base[pos];
    }
    auto n = get_n_data_chars();
    if (pos < n) {
      assert(pos < 8);
      const char *base = ref_data();
      return base[pos];
    }
    return ref().get_char(ptr_or_start + pos - n);
  }

  template <typename T, typename = std::enable_if_t<!std::is_same_v<T, char *const>>>
  [[nodiscard]] bool starts_with(const T st) const {
    if (MMAP_LIB_LIKELY(st.size() > size()))
      return false;

    if (is_sso()) {
      auto *base_st   = st.data();
      auto *base_self = ref_base_sso();
      return memcmp(base_self, base_st, st.size()) == 0;
    }

    for (auto i = 0u; i < st.size(); ++i) {
      if ((*this)[i] != st[i]) {
        return false;
      }
    }
    return true;
  }

  template <std::size_t N>
  [[nodiscard]] bool starts_with(const char (&v)[N]) const {
    return starts_with(std::string_view(v, (N - 1)));
  }

  // checks if *this pstr ends with en
  template <typename T, typename = std::enable_if_t<!std::is_same_v<T, char *const>>>
  [[nodiscard]] bool ends_with(const T en) const {
    if (MMAP_LIB_LIKELY(en.size() > size()))
      return false;

    if (en.size() == size()) {
      return *this == en;  // faster path
    }

    if (is_sso()) {
      const auto *base_en   = en.data();
      const auto *base_self = ref_base_sso() + size() - en.size();
      return memcmp(base_self, base_en, en.size()) == 0;
    }

    for (size_t j = size() - en.size(), i = 0u; j < size(); ++j, ++i) {
      if ((*this)[j] != en[i]) {
        return false;
      }
    }
    return true;
  }

  template <std::size_t N>
  [[nodiscard]] bool ends_with(const char (&v)[N]) const {
    return ends_with(std::string_view(v, (N - 1)));
  }

  template <typename T, typename = std::enable_if_t<!std::is_same_v<T, char *const>>>
  [[nodiscard]] std::size_t find(const T v, std::size_t pos = 0) const {
    return find_int(v, pos, v.size());
  }

  template <std::size_t N>
  [[nodiscard]] std::size_t find(const char (&v)[N], std::size_t pos = 0) const {
    return find_int(v, pos, (N - 1));
  }

// Extension over the  Bit Twiddling Hacks By Sean Eron Anderson seander@cs.stanford.edu
#define word_has_zero(v)      (((v)-0x01010101UL) & ~(v)&0x80808080UL)
#define word_has_value(x, n)  (word_has_zero((x) ^ (((~0UL) / 255) * (n))))
#define dword_has_zero(v)     (((v)-0x0101010101010101ULL) & ~(v)&0x8080808080808080ULL)
#define dword_has_value(x, n) (dword_has_zero((x) ^ (((~0ULL) / 255) * (n))))

  [[nodiscard]] std::size_t find(char ch, std::size_t pos = 0) const {
    if (is_sso()) {
      if (pos < 3) {
        auto a = word_has_value(size_ctrl >> (pos * 8 + 8), ch);
        if (a) {  // match found
          return pos + __builtin_ctz(a) / 8;
        }
        pos = 3;
      }
      if (pos < 7) {
        auto b = word_has_value(ptr_or_start >> ((pos - 3) * 8), ch);
        if (b) {  // match found
          return pos + __builtin_ctz(b) / 8;
        }
        pos = 7;
      }
      auto c = dword_has_value(data_storage >> (pos - 7) * 8, ch);
      if (c) {  // match found
        return pos + __builtin_ctzll(c) / 8;
      }
      return std::string::npos;
    }

    if (pos >= size())
      return std::string::npos;
    for (size_t i = pos; i < size(); i++) {
      if ((*this)[i] == ch)
        return i;
    }
    return std::string::npos;
  }

  template <typename T>
  constexpr bool contains(const T s) const {
    return find(s) != std::string::npos;
  }

  template <std::size_t N>
  constexpr bool contains(const char (&s)[N]) const {
    return find(std::string_view(s, (N - 1))) != std::string::npos;
  }

  template <typename T, typename = std::enable_if_t<!std::is_same_v<T, char *const>>>
  [[nodiscard]] std::size_t rfind(const T v, std::size_t pos = 0) const {
    return rfind_int(v, pos, v.size());
  }

  template <std::size_t N>
  [[nodiscard]] std::size_t rfind(const char (&v)[N], std::size_t pos = 0) const {
    return rfind_int(v, pos, (N - 1));
  }

  [[nodiscard]] std::size_t rfind(char ch, std::size_t pos = std::string::npos) const {
    int position;
    if (pos == std::string::npos)
      position = size() - 1;
    else
      position = static_cast<int>(pos);

    for (int i = position; i >= 0; i--) {
      if ((*this)[i] == ch)
        return i;
    }
    return std::string::npos;
  }

  [[nodiscard]] std::string to_s() const {  // convert to std::string
    if (is_sso()) {
      return std::string(ref_base_sso(), size_sso());
    }
    std::string s;
    s.reserve(size() + 1);

    s.append(ref_data(), get_n_data_chars());
    s.append(ref().get_char_ptr(ptr_or_start), size() - get_n_data_chars());

    return s;
  }

  [[nodiscard]] constexpr int to_i() const {  // convert to integer
    if (is_sso()) {
      int         result = 0;
      const auto *base   = ref_base_sso();
      if (empty() || !std::isdigit(base[0]))
        return 0;
      std::from_chars(base, base + size_sso(), result);
      return result;
    }
    assert(false);  // overflow non SSO is a TOO long integer
    return 0;
  }

  [[nodiscard]] static str from_u64_to_hex(uint64_t v) {
    std::array<char, 15> str2;
    auto [ptr, ec] = std::to_chars(str2.data(), str2.data() + str2.size(), v, 16);
    (void)ec;
    return str(str2.data(), ptr - str2.data());
  }

  [[nodiscard]] constexpr int to_u64_from_hex() const {  // convert to integer from hexa
    if (is_sso()) {
      int         result = 0;
      const auto *base   = ref_base_sso();
      if (empty())
        return 0;
      std::from_chars(base, base + size_sso(), result, 16);
      return result;
    }
    assert(false);  // overflow non SSO is a TOO long integer
    return 0;
  }

  [[nodiscard]] bool is_string() const {
    if (size() == 0)
      return false;

    auto ch = front();
    if (std::isdigit(ch) || ch == '-')
      return false;

    return true;
  }

  [[nodiscard]] bool is_i() const {
    if (!is_sso())
      return false;
    int         result;
    const auto *base = ref_base_sso();
    if (size() == 0 || !(std::isdigit(base[0]) || base[0] == '-'))
      return false;
    auto [p, ec] = std::from_chars(base, base + size(), result);
    (void)p;
    if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range)
      return false;

    return true;
  }

  template <typename T>
  [[nodiscard]] str append(const T b) const {
    if constexpr (std::is_same_v<T, char> || std::is_same_v<T, unsigned char>) {
      return append(1, b);
    } else if constexpr (std::is_same_v<T, char const *>) {
      auto sv = std::string_view(b);
      return append_array(sv);
    } else {
      return append_array(b);
    }
  }

#if 0
  [[nodiscard]] str append(const char ch) const {
    auto s = to_s();
    s.append(1,ch);

    return str(s);
  }
#endif

  [[nodiscard]] str prepend(const char ch) const {
    std::string s;
    s.append(1, ch);
    s.append(to_s());

    return str(s);
  }

  [[nodiscard]] str append(size_t sz, char ch) const {
    if (sz == 0)
      return *this;

    auto s = to_s();
    s.append(sz, ch);

    return str(s);
  }

  [[nodiscard]] str strip_leading(char ch) const {
    for (auto i = 0u; i < size(); ++i) {
      if ((*this)[i] != ch) {
        return substr(i);
      }
    }

    return str();  // empty string, all ch removed or nothing in size
  }

// Extension over the  Bit Twiddling Hacks By Sean Eron Anderson seander@cs.stanford.edu
#define has_byte_uppercase(x)                                                     \
  (((((((~0ULL) / 255) * (127 + (91))) - ((x) & ((~0ULL) / 255) * 127)) & (~(x))) \
    & (((x) & ((~0ULL) / 255) * 127) + (((~0ULL) / 255) * (127 - (64)))))         \
   & (((~0ULL) / 255) * 128))

  [[nodiscard]] str to_lower() const {
    if (is_sso()) {
      // the fist byte is size. Since it is SSO it can not be between A (64) and Z (91)
      // The bytes over the size are irrelevant (but they should be zero, so also out of range)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
      const uint64_t *a_orig = (const uint64_t *)(this);
      const uint64_t *b_orig = a_orig + 1;
#pragma GCC diagnostic pop
      uint64_t a = (has_byte_uppercase(*a_orig) >> 2) | (*a_orig);
      uint64_t b = (has_byte_uppercase(*b_orig) >> 2) | (*b_orig);

      return str(a, b);
    }

    auto s = to_s();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return str(s);
  }

protected:
  template <typename T>
  static inline typename std::enable_if<true == std::is_integral_v<T>, std::string>::type _concat_to_s(T const &val) {
    return std::to_string(val);
  }

  static inline std::string _concat_to_s(const str &s) { return s.to_s(); }

  static inline std::string_view _concat_to_s(const std::string &s) { return s; }

  static inline std::string_view _concat_to_s(const std::string_view &s) { return s; }

  static inline std::string_view _concat_to_s(const char *s) { return {s}; }

public:
  template <typename... Strs>
  [[nodiscard]] static str concat(const Strs... strs) {
    std::string s;

    (s.append(_concat_to_s(strs)), ...);

    return mmap_lib::str(s);
  }

  [[nodiscard]] std::vector<str> split(const char chr) const {
    std::vector<str> vec;

    auto pos = 0u;
    while (true) {
      auto pos2 = find(chr, pos);
      if (pos2 == std::string::npos) {
        vec.emplace_back(substr(pos));
        return vec;
      }
      vec.emplace_back(substr(pos, pos2 - pos));
      pos = pos2 + 1;
    }

    assert(false);
    return vec;
  }

  // str created from these will have same template as original str
  [[nodiscard]] str get_str_after_last(const char chr) const {
    auto pos = rfind(chr);
    if (pos == std::string::npos)
      return str();

    return substr(pos + 1);
  }

  [[nodiscard]] str get_str_after_last_if_exists(const char chr) const {
    auto pos = rfind(chr);
    if (pos == std::string::npos)
      return *this;

    return substr(pos + 1);
  }

  [[nodiscard]] str get_str_after_first(const char chr) const {
    auto pos = find(chr);
    if (pos == std::string::npos)
      return str();

    return substr(pos + 1);
  }

  [[nodiscard]] str get_str_after_first_if_exists(const char chr) const {
    auto pos = find(chr);
    if (pos == std::string::npos)
      return *this;

    return substr(pos + 1);
  }

  [[nodiscard]] str get_str_before_last(const char chr) const {
    auto pos = rfind(chr);
    if (pos == std::string::npos)
      return *this;

    return substr(0, pos);
  }

  [[nodiscard]] str get_str_before_first(const char chr) const {
    auto pos = find(chr);
    if (pos == std::string::npos)
      return *this;

    return substr(0, pos);
  }

  [[nodiscard]] str substr(size_t start) const {
    str s;

    if (is_sso()) {
      auto *base = ref_base_sso();

      s.set_sso(base + start, size_sso() - start);
      return s;
    }

    auto ptr_offset = get_n_data_chars();
    if (ptr_offset > start) {  // fast case, just shift data

      // Keep the pointers
      s.ptr_or_start = ptr_or_start;

      // get new size
      uint32_t sc = size_ctrl >> 1;
      sc -= start;
      s.size_ctrl = (sc << 1);

      // adjust data (beginning of string)
      s.data_storage = data_storage >> (8 * start);

      return s;
    }

    auto *base = ref().get_char_ptr(ptr_or_start);

    return str(base + start - ptr_offset, size() - start);
  }

  [[nodiscard]] str substr(size_t start, size_t max_size) const {
    if ((start + max_size) >= size() || max_size == std::string::npos)
      return substr(start);

    auto new_size = max_size;

    str s;
    if (is_sso()) {
      assert(new_size < 16);
      const auto *base = ref_base_sso();

      s.set_sso(base + start, new_size);
      return s;
    }

    if ((start + new_size) <= get_n_data_chars()) {  // only data
      assert(new_size < 16);
      const auto *base = ref_data();

      s.set_sso(base + start, new_size);
      return s;
    }

    auto ptr_offset = get_n_data_chars();
    if (start > ptr_offset) {  // all the data can be discarded
      const auto *base = ref().get_char_ptr(ptr_or_start);

      return str(base + start - ptr_offset, new_size);
    }

    assert((start + new_size) > ptr_offset);  // otherwise done already

    std::string_view sv_first(ref_data() + start, ptr_offset - start);
    std::string_view sv_last = ref().get_sv(ptr_or_start, size() - ptr_offset);

    return concat(sv_first, sv_last.substr(0, new_size - sv_first.size()));
  }

  // WARNING: efficient BUT dangerous API. Only use it if you understand it!!
  size_t fill_txt_direct(char *txt) const {  // FILL the str to the txt. txt should have space or seg-fault
    return fill_txt(0, txt);
  }

  [[nodiscard]] constexpr size_t hash() const {
    uint64_t h = size_ctrl;
    h <<= 32;
    h |= ptr_or_start;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccd;
    h ^= h >> 33;
    h ^= data_storage;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53;
    h ^= h >> 33;
    return static_cast<size_t>(h);
  }

  template <typename H>
  friend H AbslHashValue(H h, const str s) {
    return H::combine(std::move(h), s.hash());
  };

  __attribute__((noinline)) void dump() const;
};

#if 0
inline bool operator==(const std::string_view &a, const mmap_lib::str &b) {
  return b == a;
}
#endif

template <std::size_t N>
bool operator==(const char (&a)[N], const mmap_lib::str &b) {
  return b == a;
}

}  // namespace mmap_lib

#include "fmt/format.h"

template <>
struct fmt::formatter<mmap_lib::str> : formatter<string_view> {
  // parse is inherited from formatter<string_view>.
  template <typename FormatContext>
  auto format(mmap_lib::str c, FormatContext &ctx) {
    return formatter<string_view>::format(c.to_s(), ctx);
  }
};

#include <iostream>

inline std::ostream &operator<<(std::ostream &o, const mmap_lib::str &str) { return o << str.to_s(); }

inline mmap_lib::str operator"" _str(const char *s, std::size_t sz) { return mmap_lib::str(s, sz); }
