//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "dlop.hpp"

#include <cstdlib>

#include "fmt/format.h"
#include "likely.hpp"
#include "str_tools.hpp"

void Dlop::free(size_t sz, int64_t *ptr) {
  assert(free_pool.size() > (sz >> 3));

  free_pool[sz >> 3]->release_ptr(ptr);
}

int64_t *Dlop::alloc(size_t sz) {  // sz is in int64 chunks (8bytes)
  assert(sz >= 1);                 // less than 1 is not valid in dlop

  if (likely(free_pool.size() > (sz >> 3))) {
    return static_cast<int64_t *>(free_pool[sz >> 3]->get_ptr());
  }

  while ((sz >> 3) >= free_pool.size()) {
    auto *ptr = new raw_ptr_pool((free_pool.size() + 1) << 6);  // 64bytes chunk multiples
    free_pool.emplace_back(ptr);
  }

  return static_cast<int64_t *>(free_pool[sz >> 3]->get_ptr());
}

spool_ptr<Dlop> Dlop::create_bool(bool val) {
  auto dlop = spool_ptr<Dlop>::make(Type::Boolean, 1);

  dlop->base[0]  = val ? -1 : 0;
  dlop->extra[0] = 0;

  return dlop;
}

spool_ptr<Dlop> Dlop::create_integer(int64_t val) {
  auto dlop = spool_ptr<Dlop>::make(Type::Integer, 1);

  dlop->base[0]  = val;
  dlop->extra[0] = 0;

  return dlop;
}

spool_ptr<Dlop> Dlop::create_string(std::string_view txt) {
  auto dlop = spool_ptr<Dlop>::make(Type::String, txt.size());

  // alnum string
  for (int i = txt.size() - 1; i >= 0; --i) {
    dlop->shl_base(8);
    dlop->or_base(static_cast<unsigned char>(txt[i]));
  }

  return dlop;
}

spool_ptr<Dlop> Dlop::from_binary(std::string_view txt, bool unsigned_result) {
  auto dlop = spool_ptr<Dlop>::make(Type::Integer, 1 + txt.size() / 64);
  if (!unsigned_result) {
    // Look for the first not underscore character (this is the sign)
    for (auto i = 0u; i < txt.size(); ++i) {
      const auto ch2 = txt[i];
      if (ch2 == '_') {
        continue;
      }
      if (ch2 == '1') {
        dlop->extend_base(-1);
      } else if (ch2 == '?') {
        dlop->extend_extra(-1);
      }
      break;
    }
  }

  for (auto i = 0u; i < txt.size(); ++i) {
    const auto ch2 = txt[i];
    if (ch2 == '_') {
      continue;
    }

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

spool_ptr<Dlop> Dlop::from_pyrope(std::string_view orig_txt) {
  if (orig_txt.empty()) {
    return spool_ptr<Dlop>::make(Type::Invalid, 0);
  }

  auto txt = str_tools::to_lower(orig_txt);

  // Special cases
  if (txt == "true") {
    return Dlop::create_bool(true);
  } else if (txt == "false") {
    return Dlop::create_bool(false);
  }

  bool negative   = false;
  auto skip_chars = 0u;

  if (txt.front() == '-') {
    negative   = true;
    skip_chars = 1;
  } else if (txt.front() == '+') {
    skip_chars = 1;
  }

  auto shift_mode      = 0;
  bool unsigned_result = false;

  if (txt.size() >= (1 + skip_chars) && std::isdigit(txt[skip_chars])) {
    shift_mode = 10;  // decimal (maybe not if starts with 0
    if (txt.size() >= (2 + skip_chars) && txt[skip_chars] == '0') {
      ++skip_chars;
      auto sel_ch = txt[skip_chars];
      if (sel_ch == 's') {
        // 0sb (signed). Too confusing otherwise. is 0sx80 negative and 0sx10 also negative (where is the MSB?)
        ++skip_chars;
        sel_ch = txt[skip_chars];
        if (sel_ch != 'b') {
          throw std::runtime_error(fmt::format("ERROR: {} unknown pyrope encoding only binary can be signed 0sb...\n", orig_txt));
        }
        assert(!unsigned_result);
      } else {
        unsigned_result = true;
      }

      if (sel_ch == 'x') {  // 0x or 0sx
        shift_mode = 4;
        ++skip_chars;
      } else if (sel_ch == 'b') {
        shift_mode = 1;
        ++skip_chars;
      } else if (sel_ch == 'd') {
        shift_mode = 10;  // BASE 10 decimal
        ++skip_chars;
      } else if (std::isdigit(sel_ch)) {  // 0 or 0o or 0s or 0so
        shift_mode = 3;
      } else if (sel_ch == 'o') {  // 0 or 0o or 0s or 0so
        shift_mode = 3;
        ++skip_chars;
      } else {
        throw std::runtime_error(fmt::format("ERROR: {} unknown pyrope encoding (leading {})\n", orig_txt, sel_ch));
      }
    }
  } else {
    int start_i = static_cast<int>(orig_txt.size());
    int end_i   = 0;

    if (orig_txt.size() > 1 && orig_txt.front() == '\'' && orig_txt.back() == '\'') {
      --start_i;
      ++end_i;
    }

    return Dlop::create_string(orig_txt.substr(end_i, start_i - end_i));
  }

  auto dlop = spool_ptr<Dlop>::make(Type::Integer, 1 + txt.size() / 16);  // conservative size for hex

  if (shift_mode == 10) {  // decimal
    for (auto i = skip_chars; i < txt.size(); ++i) {
      auto v = char_to_val[(uint8_t)txt[i]];
      if (likely(v >= 0)) {
        dlop->mult_base(10);
        dlop->add_base(v);
      } else {
        if (txt[i] == '_') {
          continue;
        }

        throw std::runtime_error(fmt::format("ERROR: {} encoding could not use {}\n", orig_txt, txt[i]));
      }
    }
  } else if (shift_mode == 1) {  // 0b binary
    auto v = from_binary(txt.substr(skip_chars), unsigned_result);
    if (!negative) {
      return v;
    }

    dlop->negate_mut();
    return dlop;
  } else {
    assert(shift_mode == 3 || shift_mode == 4);  // octal or hexa

    auto first_digit = -1;
    for (auto i = skip_chars; i < txt.size(); ++i) {
      if (txt[i] == '_') {
        continue;
      }

      auto v = char_to_val[(uint8_t)txt[i]];
      if (unlikely(v < 0)) {
        throw std::runtime_error(fmt::format("ERROR: {} encoding could not use {}\n", orig_txt, txt[i]));
      }
      if (first_digit < 0) {
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

#if 0
spool_ptr<Dlop> Dlop::and_op(spool_ptr<Dlop> other) {

  res.base = self.base & other.base;
  res.extra = (self.extra|other.extra) & res.base;

}

spool_ptr<Dlop> Dlop::or_op(spool_ptr<Dlop> other) {

  res.base = self.base | other.base;
  res.extra = (self.extra|other.extra) & (~res.base);

}
#endif

spool_ptr<Dlop> Dlop::add_op(spool_ptr<Dlop> other) {
  // TODO: deal with extra
  auto dlop = spool_ptr<Dlop>::make(Type::Integer, size);  // TODO: it can be size+1 (get_bits)

  assert(size == other->size);  // TODO:

  Blop::addn(dlop->base, dlop->size, base, other->base);

  return dlop;
}

void Dlop::mut_add(spool_ptr<Dlop> other) {
  assert(size == other->size);  // TODO:

  Blop::addn(base, size, base, other->base);  // FIXME: have an carry addn?
}

void Dlop::mut_add(int64_t other) {
  if (size > 1) {
    int64_t extended[size];
    bzero(extended + 1, sizeof(int64_t) * (size - 1));
    extended[0] = other;
    Blop::addn(base, size, base, extended);
  } else {
    Blop::addn(base, 1, base, &other);
  }
}

spool_ptr<Dlop> Dlop::add_op(int64_t other) {
  // TODO: deal with extra
  auto dlop = spool_ptr<Dlop>::make(Type::Integer, size);  // TODO: it can be size+1 (get_bits)

  if (dlop->size > 1) {
    int64_t extended[dlop->size];
    Blop::extend(extended, dlop->size, other);
    Blop::addn(dlop->base, dlop->size, base, extended);
  } else {
    Blop::addn(dlop->base, 1, base, &other);
  }

  return dlop;
}

void Dlop::dump() const {
  fmt::print("size:{}\n  base:0x", size);
  for (int i = size - 1; i >= 0; --i) {
    fmt::print("_{:016x}", (uint64_t)base[i]);
  }
  fmt::print("\n extra:0x", size);
  for (int i = size - 1; i >= 0; --i) {
    fmt::print("_{:016x}", (uint64_t)extra[i]);
  }
  fmt::print("\n");
}
