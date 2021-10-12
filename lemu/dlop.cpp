//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <stdlib.h>

#include "dlop.hpp"
#include "lbench.hpp"

void Dlop::free(size_t sz, int64_t *ptr) {
  assert(free_pool.size() > sz);

  if (free_pool[sz]) {
    auto tmp = (int64_t)free_pool[sz];
    *ptr = tmp;
  }else{
    *ptr = 0;
  }

  free_pool[sz] = ptr;
}

int64_t *Dlop::alloc(size_t sz) {
  assert(sz>=1); // less than 1 is not valid in dlop

  if (sz>=free_pool.size()) {
    free_pool.resize(sz+1,nullptr);
  }

  if (free_pool[sz] == nullptr) { // TODO: Maybe malloc many and put in free_pool to avoid many mallocs
    return static_cast<int64_t *>(malloc(8*sz));
  }

  int64_t *ptr = free_pool[sz];
  if (*ptr) {
    int64_t *tmp = (int64_t *)(*ptr);
    free_pool[sz] = tmp;
  }

  return ptr;
}

std::shared_ptr<Dlop> Dlop::create(bool val) {
  auto dlop = std::make_shared<Dlop>(Type::Boolean, 1);

  dlop->base[0]  = val?-1:0;
  dlop->extra[0] = 0;

  return dlop;
}

std::shared_ptr<Dlop> Dlop::create_string(const mmap_lib::str txt) {

  auto dlop = std::make_shared<Dlop>(Type::String, txt.size());

  // alnum string
  for (int i = txt.size() - 1; i >= 0; --i) {
    dlop->shl_base(8);
    dlop->or_base(static_cast<unsigned char>(txt[i]));
  }

  return dlop;
}

std::shared_ptr<Dlop> Dlop::from_binary(const mmap_lib::str txt, bool unsigned_result) {

  auto dlop = std::make_shared<Dlop>(Type::Integer, 1+txt.size()/64);
  if (!unsigned_result) {
    // Look for the first not underscore character (this is the sign)
    for (auto i = 0u; i < txt.size(); ++i) {
      const auto ch2 = txt[i];
      if (ch2 == '_')
        continue;
      if (ch2=='1') {
        dlop->extend_base(-1);
      }else if (ch2=='?') {
        dlop->extend_extra(-1);
      }
      break;
    }
  }

  for (auto i = 0u; i < txt.size(); ++i) {
    const auto ch2 = txt[i];
    if (ch2 == '_')
      continue;

    dlop->shl_base(1);
    dlop->shl_extra(1);
    if (ch2 == '?' || ch2 == 'x') {
      dlop->or_extra(1);
    } else if (ch2 == 'z') {
      dlop->or_extra(1);
    } else if (ch2 == '0') {
    } else if (ch2 == '1') {
      dlop->or_base(1);
    } else {
      throw std::runtime_error(fmt::format("ERROR: {} binary encoding could not use {}\n", txt, ch2));
    }
  }

  return dlop;
}

std::shared_ptr<Dlop> Dlop::from_pyrope(const mmap_lib::str orig_txt) {

  if (orig_txt.empty())
    return std::make_shared<Dlop>(Type::Invalid, 0);

  mmap_lib::str txt = orig_txt.to_lower();

  // Special cases
  if (txt == "true"_str) {
    return Dlop::create(true);
  } else if (txt == "false"_str) {
    return Dlop::create(false);
  }

  bool negative   = false;
  auto skip_chars = 0u;

  if (txt.front() == '-') {
    negative   = true;
    skip_chars = 1;
  }else if (txt.front() == '+') {
    skip_chars = 1;
  }

  auto shift_mode      = 0;
  bool unsigned_result = false;

  if (txt.size() >= (1+skip_chars) && std::isdigit(txt[skip_chars])) {
    shift_mode = 10; // decimal (maybe not if starts with 0
    if (txt.size()>= (2+skip_chars) && txt[skip_chars] == '0') {
      ++skip_chars;
      auto sel_ch = txt[skip_chars];
      if (sel_ch == 's') {
        // 0sb (signed). Too confusing otherwise. is 0sx80 negative and 0sx10 also negative (where is the MSB?)
        ++skip_chars;
        sel_ch = txt[skip_chars];
        if (sel_ch!='b') {
          throw std::runtime_error(fmt::format("ERROR: {} unknown pyrope encoding only binary can be signed 0sb...\n", orig_txt));
        }
        assert(!unsigned_result);
      }else{
        unsigned_result = true;
      }

      if (sel_ch == 'x') { // 0x or 0sx
        shift_mode = 4;
        ++skip_chars;
      }else if (sel_ch == 'b') {
        shift_mode = 1;
        ++skip_chars;
      }else if (sel_ch == 'd') {
        shift_mode = 10; // BASE 10 decimal
        ++skip_chars;
      }else if (std::isdigit(sel_ch)) { // 0 or 0o or 0s or 0so
        shift_mode = 3;
      }else if (sel_ch == 'o') { // 0 or 0o or 0s or 0so
        shift_mode = 3;
        ++skip_chars;
      }else{
        throw std::runtime_error(fmt::format("ERROR: {} unknown pyrope encoding (leading {})\n", orig_txt, sel_ch));
      }
    }
  }else{
    int start_i = static_cast<int>(orig_txt.size());
    int end_i   = 0;

    if (orig_txt.size()>1 && orig_txt.front() == '\'' && orig_txt.back() == '\'') {
      --start_i;
      ++end_i;
    }

    return Dlop::create_string(orig_txt.substr(end_i, start_i-end_i));
  }

  auto dlop = std::make_shared<Dlop>(Type::Integer, 1+txt.size()/16); // conservative size for hex 

  if (shift_mode == 10) { // decimal
    for (auto i = skip_chars; i < txt.size(); ++i) {
      auto v = char_to_val[(uint8_t)txt[i]];
      if (likely(v >= 0)) {
        dlop->mult_base(10);
        dlop->add_base(v);
      } else {
        if (txt[i] == '_')
          continue;

        throw std::runtime_error(fmt::format("ERROR: {} encoding could not use {}\n", orig_txt, txt[i]));
      }
    }
  }else if (shift_mode == 1) {  // 0b binary
    auto v = from_binary(txt.substr(skip_chars), unsigned_result);
    if (!negative)
      return v;

    dlop->negate_mut();
    return dlop;
  }else{
    assert(shift_mode == 3 || shift_mode == 4);  // octal or hexa

    auto first_digit = -1;
    for (auto i = skip_chars; i < txt.size(); ++i) {
      if (txt[i] == '_')
        continue;

      auto v = char_to_val[(uint8_t)txt[i]];
      if (unlikely(v < 0)) {
        throw std::runtime_error(fmt::format("ERROR: {} encoding could not use {}\n", orig_txt, txt[i]));
      }
      if (first_digit<0) {
        first_digit = v;
      }

      auto char_sa = char_to_bits[(uint8_t)txt[i]];
      if (unlikely(char_sa > shift_mode)) {
        throw std::runtime_error(
            fmt::format("ERROR: {} invalid syntax for number {} bits needed for '{}'", orig_txt, char_sa, txt[i]));
      }
      dlop->shl_base(shift_mode);
      dlop->or_base(v);
    }

    assert(unsigned_result);
  }

  if (negative) {
    dlop->negate_mut();
    if (unsigned_result && dlop->is_negative()) {
      throw std::runtime_error(fmt::format("ERROR: {} negative value but it must be unsigned\n", orig_txt));
    }
  }

  return dlop;
}

void Dlop::dump() const {
  fmt::print("size:{}\n  base:0x", size);
  for(int i=size-1; i>=0;--i)
    fmt::print("_{:016x}", (uint64_t)base[i]);
  fmt::print("\n extra:0x", size);
  for(int i=size-1; i>=0;--i)
    fmt::print("_{:016x}", (uint64_t)extra[i]);
  fmt::print("\n");
}
