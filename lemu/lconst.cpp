
#include "lconst.hpp"

#include "absl/types/span.h"
#include "boost/multiprecision/cpp_int.hpp"
#include "fmt/format.h"
#include "iassert.hpp"
#include "lbench.hpp"
#include "lrand.hpp"
#include "mmap_hash.hpp"

mmap_lib::str Lconst::serialize() const {
  std::vector<unsigned char> v;
  unsigned char c = (explicit_str ? 0x10 : (is_negative() ? 0x01 : 0));
  v.emplace_back(c);
  v.emplace_back(bits >> 16);
  v.emplace_back(bits >> 8);
  v.emplace_back(bits);

  boost::multiprecision::export_bits(num, std::back_inserter(v), 8);

  auto str = mmap_lib::str(v.data(), v.size());

  Number res_num1;
  boost::multiprecision::import_bits(res_num1, v.begin() + 4, v.end());

  std::vector<unsigned char> v2;
  for(auto i=0u;i<str.size();++i)
    v2.emplace_back(str[i]);
  Number res_num2;
  boost::multiprecision::import_bits(res_num2, v2.begin() + 4, v2.end());

  return str;
}

Lconst Lconst::unserialize(const mmap_lib::str &v) {

  I(v.size() > 4);  // invalid otherwise

  std::vector<unsigned char> v2;
  for(auto i=0u;i<v.size();++i)
    v2.emplace_back(v[i]);

  uint8_t  c0 = v2[0];
  uint32_t c1 = v2[1];
  uint32_t c2 = v2[2];
  uint32_t c3 = v2[3];

  auto res_explicit_str = (c0 & 0x10) ? true : false;

  auto res_bits = (c1 << 16) | (c2 << 8) | c3;

  Number res_num;
  boost::multiprecision::import_bits(res_num, v2.begin() + 4, v2.end());
  if (c0 & 0x01) {  // negative
    res_num = -res_num;
  }

  return Lconst(res_explicit_str, res_bits, res_num);
}


uint64_t Lconst::hash() const {
  std::vector<uint64_t> v;
  uint64_t              c = (explicit_str ? 0x10 : (is_negative() ? 0x01 : 0));
  c                       = (c << 32) | bits;
  v.emplace_back(c);

  boost::multiprecision::export_bits(num, std::back_inserter(v), 64);

  return mmap_lib::hash64(v.data(), v.size() * 8);
}

Lconst::Lconst(absl::Span<unsigned char> v) {
  I(v.size() > 3);  // invalid otherwise

  uint8_t  c0 = v[0];
  uint32_t c1 = v[1];
  uint32_t c2 = v[2];
  uint32_t c3 = v[3];

  explicit_str = (c0 & 0x10) ? true : false;

  bits = (c1 << 16) | (c2 << 8) | c3;

  auto s = v.subspan(4);
  boost::multiprecision::import_bits(num, s.begin(), s.end());

  if (c0 & 0x01) {  // negative
    num = -num;
  }
}

Lconst::Lconst() {
  explicit_str = false;
  bits         = 0;  // zero bits. Nothing is set 0 or ""
  num          = 0;
}

Lconst::Lconst(int64_t v) {
  explicit_str = false;
  num          = v;
  bits         = calc_num_bits();
}

Lconst::Lconst(Number v) {
  explicit_str = false;
  num          = v;
  bits         = calc_num_bits();
}


Lconst Lconst::from_binary(const mmap_lib::str txt, bool unsigned_result) {

  // handle binary with special characters ?xZ...
  mmap_lib::str bin;
  if (unsigned_result) {
    bin = "0"; // always unsigned, must have a leading 0 (implicit)
  }else{
    // Look for the first not underscore character (this is the sign)
    for (auto i = 0u; i < txt.size(); ++i) {
      const auto ch2 = txt[i];
      if (ch2 == '_')
        continue;
      bin = bin.append(ch2);
      break;
    }
  }
  bool unknown_found = false;

  Number num=0;

  for (auto i = 0u; i < txt.size(); ++i) {
    const auto ch2 = txt[i];
    if (ch2 == '_')
      continue;

    if (ch2 == '?' || ch2 == 'x') {
      bin = bin.append('?');
      unknown_found = true;
    } else if (ch2 == 'z') {
      bin = bin.append('z');
      unknown_found = true;
    } else if (ch2 == '0') {
      if (bin != "0") { // remove too many leading zeroes
        num = (num << 1);
        bin = bin.append('0');
      }
    } else if (ch2 == '1') {
      if (bin != "1") { // remove too many leading ones if signed
        num = (num << 1) | 1;
        bin = bin.append('1');
      }
    } else {
      throw std::runtime_error(fmt::format("ERROR: {} binary encoding could not use {}\n", txt, ch2));
    }
  }

  if (unknown_found) {
    num = 0;
    for (int i = bin.size() - 1; i >= 0; --i) {
      num <<= 8;
      num |= static_cast<uint8_t>(bin[i]);
    }

    return Lconst(true, bin.size(), num);
  }

  if (!unsigned_result && bin.front() == '1') {
    num = num-(Number(1)<<(bin.size()-1));
  }

  return Lconst(false, calc_num_bits(num), num);
}

Lconst Lconst::from_string(mmap_lib::str orig_txt) {
  Number num;

  for (int i = orig_txt.size() - 1; i >= 0; --i) {
    num <<= 8;
    num += orig_txt[i];
  }

  return Lconst(true, orig_txt.size() * 8, num);
}

Lconst Lconst::from_pyrope(const mmap_lib::str orig_txt) {

  if (orig_txt.empty())
    return Lconst();

  mmap_lib::str txt = orig_txt.to_lower();

  // Special cases
  if (txt == "true"_str) {
    return Lconst(false, 1, -1);
  } else if (txt == "false"_str) {
    return Lconst(false, 1, 0);
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
        I(!unsigned_result);
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

    Number num;
    // alnum string (not number, still convert to num)
    bool prev_escaped=false;
    for (int i = start_i - 1; i >= end_i; --i) {
      num <<= 8;
      num |= orig_txt[i];

      if (orig_txt[i] == '\'' && !prev_escaped && i!=0) {
        throw std::runtime_error(fmt::format("ERROR: {} malformed pyrope string. ' must be escaped\n", orig_txt));
      }

      if (orig_txt[i] == '\\') {
        if (prev_escaped) // \\ sequence
          prev_escaped = false;
        else // \x sequence
          prev_escaped = true;
      }
    }

    return Lconst(true, (start_i-end_i) * 8, num);
  }

  Number num;

  if (shift_mode == 10) { // decimal
    for (auto i = skip_chars; i < txt.size(); ++i) {
      auto v = char_to_val[(uint8_t)txt[i]];
      if (likely(v >= 0)) {
        num = (num * 10) + v;
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

    num = -v.get_num();
    return Lconst(false, calc_num_bits(num), num);
  }else{
    I(shift_mode == 3 || shift_mode == 4);  // octal or hexa

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
      num = (num << shift_mode) | v;
    }

    if (!unsigned_result) { // check if MSB is 1 for 2s complement
      if (first_digit>>(shift_mode-1)) {
        num = num - (Number(1)<<(calc_num_bits(num)-1));
      }
    }
  }

  if (negative) {
    num = -num;
    if (unsigned_result && num<0) {
      throw std::runtime_error(fmt::format("ERROR: {} negative value but it must be unsigned\n", orig_txt));
    }
  }

  return Lconst(false, calc_num_bits(num), num);
}

Lconst Lconst::unknown(Bits_t nbits) {
  Lconst lc;

  for (Bits_t i = 0u; i < nbits; ++i) {
    lc.num <<= 8;
    lc.num |= '?';
  }
  lc.bits = nbits;  // 0sb?>>>
  if (nbits > 0)
    lc.explicit_str = true;

  return lc;
}

Lconst Lconst::unknown_positive(Bits_t nbits) {
  Lconst lc;

  for (Bits_t i = 0u; i < nbits - 1; ++i) {
    lc.num <<= 8;
    lc.num |= '?';
  }
  lc.bits = nbits;  // 0sb?>>>
  if (nbits > 1) {
    lc.num <<= 8;
    lc.num |= '0';
    lc.explicit_str = true;
  }

  return lc;
}

Lconst Lconst::unknown_negative(Bits_t nbits) {
  Lconst lc;

  for (Bits_t i = 0u; i < nbits - 1; ++i) {
    lc.num <<= 8;
    lc.num |= '?';
  }
  lc.bits = nbits;  // 0sb?>>>
  if (nbits > 1) {
    lc.num <<= 8;
    lc.num |= '1';
    lc.explicit_str = true;
  }

  return lc;
}


void Lconst::dump() const {
  if (explicit_str)
    fmt::print("str:{} bits:{}\n", to_string(), bits);
  else
    fmt::print("num:{} bits:{}\n", num.str(), bits);
}

void Lconst::adjust(const Lconst &o) {
  explicit_str = o.explicit_str && (bits == 0 || explicit_str);
  bits         = calc_num_bits(num);
}

std::pair<mmap_lib::str, mmap_lib::str> Lconst::match_binary(const Lconst &l, const Lconst &r) {

  auto l_str = l.to_binary();
  auto r_str = r.to_binary();
  if (l_str.size()!=r_str.size()) {
    if (l_str.size() > r_str.size()) {
      r_str = r_str.append(l_str.size()-r_str.size(), r_str.back());
    }else{
      l_str = l_str.append(r_str.size()-l_str.size(), l_str.back());
    }
  }

  return std::make_pair(l_str, r_str);
}

bool Lconst::is_known_true() const {
  if (!explicit_str)
    return num!=0;

  if (has_unknowns()) {
    // if there is any one, it is true
    Number      tmp = num;
    while (tmp) {
      auto ch = static_cast<unsigned char>(tmp & 0xFF);
      if (ch == '1')
        return true;
      tmp >>= 8;
    }

    return false;
  }

  return true; // plain string
}

std::pair<int,int> Lconst::get_mask_range() const {
	if (num==0)
    return std::make_pair(-1,-1); // No continuous range

	auto range_end = get_bits();
	if (is_positive())
		--range_end;

  if (is_mask())  // continuous sequence of ones. Nice
    return std::make_pair(0,range_end);

  auto trail = get_trailing_zeroes();
  if (trail==0)
    return std::make_pair(-1,-1); // No continuous range

  auto v2 = rsh_op(trail);
  if (v2.is_mask())  // continuous sequence of ones. Nice
    return std::make_pair(trail, range_end);

  return std::make_pair(-1,-1); // No continuous range
}

Lconst Lconst::get_mask_value(Bits_t bits) {
	return Lconst((Number(1) << bits) - 1);
}

Lconst Lconst::get_mask_value(Bits_t h, Bits_t l) {
	if (h==l) {
		return Lconst(Number(1) << h);
	}
	assert(h>l);

	auto res_num = ((Number(1)<<(h-l))-1)<<l;

	return Lconst(false, calc_num_bits(res_num), res_num);
}

Lconst Lconst::get_neg_mask_value(Bits_t bits) { return Lconst((Number(-1) << bits)); }

Lconst Lconst::get_mask_value() const { return get_mask_value(get_bits()); }

size_t Lconst::get_trailing_zeroes() const {
  if (num == 0)
    return 0;

  if (num > 0)
    return boost::multiprecision::lsb(num);
  return boost::multiprecision::lsb(-num);
#if 0
  Number tmp = num;
  int conta = 0;
  while ((tmp & 1)==0) {
    tmp = tmp>>1;
    conta++;
  }

  return conta;
#endif
}

Lconst Lconst::sext_op(Bits_t ebits) const {
  I(!is_string());  // FIXME: handle 0b0??

  if (ebits >= bits)
    return *this;

  // boost keeps an unsigned + sign, so must get correct bits
  Number num_2s = num;
  if (num < 0) {
    num_2s = (-num) + 2;
  }

  bool msb_set = boost::multiprecision::bit_test(num_2s, ebits);

  if (ebits == 0) {
    if (msb_set)
      return Lconst(-1);
    return Lconst(0);
  }

  Number res_num;
  if (msb_set) {  // negative number
    // remove leading ones
    auto pos = ebits - 1;
    while (boost::multiprecision::bit_test(num_2s, pos)) {
      if (pos == 0)
        return Lconst(-1);
      --pos;
    }
    res_num = (num_2s & ((Number(1) << pos) - 1)) - (Number(1) << (pos + 1));
    I(res_num < 0);
  } else {
    res_num = num_2s & ((Number(1) << ebits) - 1);
    I(res_num >= 0);
  }

  auto v = Lconst(false, calc_num_bits(res_num), res_num);
  v.dump();

  return v;
}

Lconst Lconst::get_mask_op() const {
  if (has_unknowns()) {
		auto sign = static_cast<unsigned char>(num & 0xFF);
		if (sign == '0')
			return Lconst(explicit_str, bits, num);
	  Number res_num = get_num()<<8;
		res_num |= '0'; // add ZERO to be 0b0whatever
		return Lconst(explicit_str, bits+8, res_num);
  }

  if (explicit_str) {
    return Lconst(explicit_str, bits, num);
  }

  Number res_num;
  if (num < 0) {
    res_num = (Number(1) << get_bits()) + num;  // mask+num+1
  } else {
    res_num = num;
  }

  return Lconst(false, calc_num_bits(res_num), res_num);
}

Lconst Lconst::get_mask_op(const Lconst &mask) const {
  if (mask == Lconst(-1)) {
    return get_mask_op();  // faster logic for common case
  }

  if (unlikely(mask.is_string())) {
    throw std::runtime_error(fmt::format("ERROR: can not get_bits for strings {} + {}", to_pyrope(), mask.to_pyrope()));
  }

  Number res_num;
  auto   mask_num  = mask.get_num();
  auto   mask_bits = mask.get_bits();
  if (mask < 0) {
    auto m = std::max(get_bits(), mask.get_bits());
    // mask_num = (Number(1) << m) + ((Lconst(1) - mask).not_op()).get_num();
    mask_num = ((Number(1) << m) - 1) & (((Lconst(-1) - mask).not_op()).get_num());
  }

  if (has_unknowns()) {
    // Each bit has 8 bits when has_unknowns(). Expand mask
    Number new_mask;
    Number pos(1);
    pos <<= mask.get_bits();
    Number pos_mask(0xFF);
    while (pos) {
      new_mask <<= 8;
      if (mask_num & pos) {
        new_mask = new_mask | pos_mask;
      }
      pos >>= 1;
    }
    mask_num = new_mask;
    mask_bits *= 8;
  }

  auto   max_bits = std::max(get_bits(), mask_bits);
  assert(max_bits<Bits_max);
  Number src_num;
  if (num < 0) {
    I(!explicit_str);
    src_num = (Number(1) << max_bits) + num;  // mask+num+1
  } else {
    src_num = num;
  }

  Number pos(1);
  pos = pos << max_bits;
  while (pos) {
    if (mask_num & pos) {
      auto v  = src_num & pos;
      res_num = (res_num << 1) | (v ? 1 : 0);
    }
    pos >>= 1;
  }

  if (has_unknowns()) {
    auto str = mmap_lib::str::concat("0b", to_string(res_num));  // Use the string to remove leading zeroes
    return Lconst::from_pyrope(str);
  }

  if (!is_string())
    return Lconst(false, calc_num_bits(res_num), res_num);

  mmap_lib::str str;
  while (res_num) {
    unsigned char ch = static_cast<unsigned char>(res_num & 0xFF);
    if (ch)  // remove zeroes from string
      str = str.append(1, ch);
    res_num >>= 8;
  }

  return from_string(str);
}

// set_mask(0xFFF, 0xF, 0xa) -> 0xFFa
// set_mask(0xFFF, -16, 0xa) -> 0x0aF
// set_mask(0xFFF, -16, -1)  -> -1
// set_mask(0xFF0, 0xF, -1)  -> -16 == 0b11111...111_0000
// set_mask(foo, -1, bar)  -> bar
// set_mask(foo, -2, bar)  -> (bar& (~1)) | (foo&1)
// set_mask(foo,  0, bar)  -> foo
// set_mask(foo,  1, bar)  -> (bar&1) | (bar&(-1))
// set_mask(foo, a, bar)  -> set_mask(bar, 1-a, foo)
//
// set_mask(foo ^ (rand & bar_mask), bar_mask, get_mask(foo, bar_mask)) == foo

Lconst Lconst::set_mask_op(const Lconst &mask, const Lconst &value) const {
  if (unlikely(mask.is_string())) {
    throw std::runtime_error(
        fmt::format("ERROR: no string in mask get_bits({},{},{})", to_pyrope(), mask.to_pyrope(), value.to_pyrope()));
  }

  if (mask == Lconst(0)) {
    return *this;  // no mask, just copy this
  }
  if (mask == Lconst(-1)) {
    return value;  // all ones mask, just copy value
  }

  I(!mask.has_unknowns() && !has_unknowns());  // FIXME: this should work (just someone else could do it?)

  auto mask_min = mask.get_trailing_zeroes();
  auto mask_max = mask.get_bits();

  auto res_num = get_num();

  // If base, and value are positive
  I(!is_negative());        // FIXME: Not tested
  I(!value.is_negative());  // FIXME: Not tested
  auto value_pos    = 0u;
  auto mask_num     = mask.get_num();
  bool mask_on_zero = mask.is_negative();
  if (mask.is_negative()) {
    mask_num = mask.not_op().get_num();
  }
  for (auto i = mask_min; i < mask_max; ++i) {
    if ((boost::multiprecision::bit_test(mask_num, i) ? true : false) == mask_on_zero)
      continue;

    if (boost::multiprecision::bit_test(value.num, value_pos)) {
      bit_set(res_num, i);
    } else {
      bit_unset(res_num, i);
    }
    ++value_pos;
  }

  if (mask.is_negative()) {
    auto res_value = (value.num >> value_pos) << mask_max;  // clear used bits, get in position
    res_num &= (Number(1) << mask_max) - 1;                 // clear upper result_bits to use res_value left
    res_num |= res_value;
  }

  return Lconst(false, calc_num_bits(res_num), res_num);
}

Lconst Lconst::add_op(const Lconst &o) const {
  if (unlikely(is_string() || o.is_string())) {
    throw std::runtime_error(fmt::format("ERROR: can not add strings {} + {}", to_pyrope(), o.to_pyrope()));
  }

  if (has_unknowns() || o.has_unknowns()) {
    auto s_txt = to_binary();
    auto o_txt = o.to_binary();

    auto        max_size = std::max(s_txt.size(), o_txt.size());
    mmap_lib::str result("0b");

    char carry = '0';
    for (auto i = 0u; i < max_size; ++i) {
      auto n_ones     = 0;
      auto n_unknowns = 0;
      if (i >= s_txt.size() || s_txt[i] == '0') {
      }else if (s_txt[i] == '1'){
        ++n_ones;
      }else{
        ++n_unknowns;
      }

      if (i >= o_txt.size() || o_txt[i] == '0') {
      }else if (o_txt[i] == '1'){
        ++n_ones;
      }else{
        ++n_unknowns;
      }

      if (carry == '1') {
        ++n_ones;
      }else if (carry == '0') {
      }else{
        ++n_unknowns;
      }

      unsigned char ch;
      if (n_unknowns == 0 && n_ones == 0) {
        ch    = '0';
        carry = '0';
      } else if (n_unknowns == 0 && n_ones == 1) {
        ch    = '1';
        carry = '0';
      } else if (n_unknowns == 0 && n_ones == 2) {
        ch    = '0';
        carry = '1';
      } else if (n_unknowns == 0 && n_ones == 3) {
        ch    = '1';
        carry = '1';
      } else if (n_unknowns == 1 && n_ones == 0) {
        ch    = '?';
        carry = '0';
      } else if (n_unknowns == 1 && n_ones == 2) {
        ch    = '?';
        carry = '1';
      } else {
        ch    = '?';
        carry = '?';
      }

      result = result.append(ch);
    }
    result = result.append(carry);

    return Lconst::from_pyrope(result);
  }

  Lconst res;
  res.num = get_num() + o.get_num();
  res.adjust(o);

  return res;
}

Lconst Lconst::concat_op(const Lconst &o) const {
  if (unlikely(is_string() || o.is_string())) {
    mmap_lib::str str;
    mmap_lib::str o_str;
    if (is_string())
      str = to_string();
    else if (is_i())
      str = mmap_lib::str(to_i());
    else
      str = to_binary();

    if (o.is_string())
      o_str = o.to_string();
    else if (o.is_i())
      o_str = mmap_lib::str(o.to_i());
    else
      o_str = o.to_binary();

    return Lconst::from_string(mmap_lib::str::concat(str, o_str));
  }

  Number res_num = get_num() << o.get_bits() | o.get_num();

  return Lconst(false, calc_num_bits(res_num), res_num);
}

Lconst Lconst::mult_op(const Lconst &o) const {
  if (is_string() || o.is_string()) {
    throw std::runtime_error(fmt::format("ERROR: {}*{} not allowed because one is a string\n", to_pyrope(), o.to_pyrope()));

    return Lconst::unknown(0);
  }
  if (has_unknowns() || o.has_unknowns()) {
    auto n1 = is_negative() ? -1 : 1;
    auto n2 = o.is_negative() ? -1 : 1;
    if (n1 * n2 < 0)
      return Lconst::unknown_negative(get_bits() + o.get_bits());
    return Lconst::unknown_positive(get_bits() + o.get_bits());
  }

  Lconst res;
  res.num = get_num() * o.get_num();
  res.adjust(o);

  return res;
}

Lconst Lconst::div_op(const Lconst &o) const {
  if (is_string() || o.is_string()) {
    throw std::runtime_error(fmt::format("ERROR: {}/{} not allowed because one is a string\n", to_pyrope(), o.to_pyrope()));

    return Lconst::unknown(0);
  }

  if (o.get_num() == 0) {
    if (is_negative())
      return Lconst::unknown_negative(2);
    return Lconst::unknown_positive(2);
  }
  if (has_unknowns() || o.has_unknowns()) {
    auto n1 = is_negative() ? -1 : 1;
    auto n2 = o.is_negative() ? -1 : 1;

    int b = get_bits();
    if (!o.has_unknowns()) {
      b -= o.get_bits();
      if (b <= 0)
        return Lconst(0);
    }

    if (n1 * n2 < 0)
      return Lconst::unknown_negative(b);
    return Lconst::unknown_positive(b);
  }

  Lconst res;
  res.num = get_num() / o.get_num();
  res.adjust(o);

  return res;
}

Lconst Lconst::sub_op(const Lconst &o) const {
  if (is_string() || o.is_string()) {
    throw std::runtime_error(fmt::format("ERROR: {}-{} not allowed because one is a string\n", to_pyrope(), o.to_pyrope()));

    return Lconst::unknown(0);
  }

  Lconst res;
  res.num = get_num() - o.get_num();
  res.adjust(o);

  return res;
}

Lconst Lconst::lsh_op(Bits_t amount) const {
  if (bits == 0)
    return *this;

  if (has_unknowns()) {
    auto qmarks = to_pyrope();
    return Lconst::from_pyrope(qmarks.append(amount, '0')); // TODO: faster just shift (but big changes once mask is set)
  }

  auto res_num = num << amount;

  return Lconst(is_string(), calc_num_bits(res_num), res_num);
}

Lconst Lconst::rsh_op(Bits_t amount) const {
  if (bits == 0)
    return *this;

  if (is_string()) {
    auto qmarks = to_pyrope();
    return Lconst::from_pyrope(qmarks.substr(amount));
  }

  auto res_num = num >> amount;

  return Lconst(is_string(), calc_num_bits(res_num), res_num);
}

Lconst Lconst::ror_op(const Lconst &o) const {
  Number res_num = (num != 0 || o != 0) ? 1 : 0;

  return Lconst(false, 1, res_num);
}

Lconst Lconst::or_op(const Lconst &o) const {
  if (unlikely(has_unknowns() || o.has_unknowns())) {

    auto [l_str,r_str] = match_binary(*this, o);

    mmap_lib::str result;

    for (auto i = 0u; i < l_str.size(); ++i) {
      if (l_str[i] == '1' || r_str[i] == '1')
        result = result.append('0');
      else if (l_str[i] == '?' || r_str[i] == '?')
        result = result.append('?');
      else
        result = result.append('0');
    }

    return Lconst::from_binary(result, true);
  }

  Lconst res;
  res.num = get_num() | o.get_num();
  res.adjust(o);

  return res;
}

Lconst Lconst::not_op() const {
  if (unlikely(is_string())) {
    throw std::runtime_error(fmt::format("ERROR: can not 'not' strings {}", to_pyrope()));
  }

  auto res_num = -1 - get_num();

  return Lconst(false, calc_num_bits(res_num), res_num);
}

Lconst Lconst::and_op(const Lconst &o) const {
  if (unlikely(has_unknowns() || o.has_unknowns())) {

    auto [l_str,r_str] = match_binary(*this, o);

    mmap_lib::str result;

    for (auto i = 0u; i < l_str.size(); ++i) {
      if (l_str[i] == '0' || r_str[i] == '0')
        result = result.append('0');
      else if (l_str[i] == '?' || r_str[i] == '?')
        result = result.append('?');
      else
        result = result.append('1');
    }

    return Lconst::from_binary(result, true);
  }

  Lconst res;
  res.num = get_num() & o.get_num();
  res.adjust(o);

  return res;
}

Lconst Lconst::eq_op(const Lconst &o) const {
  if (unlikely(is_string() && o.is_string())) {
    return to_string() == o.to_string() ? Lconst(-1) : Lconst(0);
  }

  if (unlikely(is_string() || o.is_string())) {
    return Lconst::from_pyrope("0sb?");
  }

  if (unlikely(has_unknowns() || o.has_unknowns())) {
    auto [l_str,r_str] = match_binary(*this, o);

    for (auto i = 0u; i < l_str.size(); ++i) {
      if (l_str[i] == '0' || r_str[i] == '0')
        continue;
      if (l_str[i] == '1' || r_str[i] == '1')
        continue;
      if (l_str[i] == '?' || r_str[i] == '?')
        return Lconst::from_pyrope("0sb?");
      else
        return Lconst(0);
    }

    return Lconst(-1);
  }

  return num == o.num ? -1 : 0;
}

Lconst Lconst::adjust_bits(Bits_t amount) const {
  I(amount > 0);

  Number r(1);
  Number res_num = num & ((r << amount) - 1);

  return Lconst(is_string(), calc_num_bits(res_num), res_num);
}

mmap_lib::str Lconst::to_string(Number num) {
  mmap_lib::str str;
  Number      tmp = num;
  while (tmp) {
    auto ch = static_cast<unsigned char>(tmp & 0xFF);
    str = str.append(ch);
    tmp >>= 8;
  }

  return str;
}

Lconst Lconst::to_known_rand() const {
  if (!has_unknowns())
    return *this;

  static Lrand<bool> rbool;

  I(is_string());
  mmap_lib::str str;
  Number      tmp = num;
  while (tmp) {
    auto ch = static_cast<unsigned char>(tmp & 0xFF);
    if (ch == '?' || ch == 'z')
      str = str.append(rbool.any()?'0':'1');
    else
      str = str.append(ch);
    tmp >>= 8;
  }
  auto sign = static_cast<unsigned char>(num & 0xFF);
  if (sign=='?' || sign=='z')
    return Lconst::from_binary(str,rbool.any());
  return Lconst::from_binary(str,sign=='0');
}

mmap_lib::str Lconst::to_field() const {
  if (explicit_str) {
    I(!has_unknowns()); // no tuple field should have unknown
    return to_string();
  }

  const auto        v = get_num();
  std::stringstream ss;

  if (v < 0) {
    ss << -v;

    fmt::print("warning: strange negative {} field\n", ss.str());
    return mmap_lib::str::concat("-",ss.str());
  }
  ss << v;

  return mmap_lib::str(ss.str());
}

mmap_lib::str Lconst::to_pyrope() const {
  if (explicit_str) {
    auto str_no_underscore = to_string();
    if (str_no_underscore.empty())
      return mmap_lib::str("''");

    if (has_unknowns()) {
      auto sign = static_cast<unsigned char>(num & 0xFF);

      mmap_lib::str str;
      if (sign=='0') {
        str = "0b";
      }else{
        str = "0sb";
      }

      size_t next_underscore;
      auto xtra = get_bits()&3;
      if (xtra) {
        auto ndigits = 1+4-xtra;
        str = str.append(ndigits, sign);
        next_underscore = 4-ndigits;
      }else{
        str = str.append(sign);
        next_underscore = 3;
      }

      for(auto i=1u;i<str_no_underscore.size();++i) {
        if (next_underscore==0) {
          str = str.append('_');
          next_underscore = 4;
        }
        str = str.append(str_no_underscore[i]);
        --next_underscore;
      }

      return str;
    }

    if (str_no_underscore.front()=='\'' && str_no_underscore.back() == '\'')
      return str_no_underscore;

    return mmap_lib::str::concat("'", str_no_underscore, "'");
  }

  const auto        v = get_num();
  std::stringstream ss;

  bool print_hexa = v > 63;
  if (print_hexa) {
    ss << std::hex;
  }

  if (v < 0) {
    ss << -v;
    if (print_hexa)
      return mmap_lib::str::concat("0sx", ss.str());

    return mmap_lib::str::concat("-",ss.str());
  }
  ss << v;

  if (print_hexa)
    return mmap_lib::str::concat("0x", ss.str());

  return mmap_lib::str(ss.str());
}

size_t Lconst::popcount() const {
  I(!is_string());

  if (num == 0)
    return 0;

  auto       popcount = 0;
  auto       i        = boost::multiprecision::lsb(num);
  const auto end      = boost::multiprecision::msb(num);
  for (; i <= end; ++i) {
    if (boost::multiprecision::bit_test(num, i) != 0) {
      ++popcount;
    }
  }

  return popcount;
}

mmap_lib::str Lconst::to_firrtl() const {
  // Note->hunter: FIRRTL-Proto requires the string output here is a decimal
  // value (no 0x or 0d allowed. Only #).  Also means it can't have 'x' or 'z'.

  Number v;
  if (explicit_str) {
    if (is_string())
      return to_string();

    v = to_known_rand().get_num();
  } else {
    v = get_num();
  }
  std::stringstream ss;

  if (v < 0)
    ss << -v;
  else
    ss << v;

  if (is_negative())
    return mmap_lib::str::concat("-", ss.str());

  return mmap_lib::str(ss.str());
}

int64_t Lconst::to_i() const {
  I(is_i());
  return static_cast<long int>(num);
}

mmap_lib::str Lconst::to_binary() const {
  if (has_unknowns()) {
    return to_string();
  }

  auto v = get_num();

  mmap_lib::str txt;
  for (auto i = 0u; i < get_bits(); ++i) {
    if (v & 1) {
      txt = txt.prepend('1');
    } else {
      txt = txt.prepend('0');
    }
    v = v >> 1;
  }

  return txt;
}

mmap_lib::str Lconst::to_verilog() const {
  if (num==0)
    return "'b0"_str;

  if (explicit_str) {
    if (has_unknowns()) {
      auto sign = static_cast<unsigned char>(num & 0xFF);

      if (sign=='0')
        return mmap_lib::str::concat(get_bits()-1, "'b", to_string().substr(1));

      return mmap_lib::str::concat(get_bits(), "'sb", to_string());
    }

    return mmap_lib::str::concat("\"", to_string(), "\"");
  }

  std::stringstream ss;
  ss << std::hex;

  if (num < 0) {
    ss << (Number(1)<<get_bits())+num;
    return mmap_lib::str::concat(get_bits(), "'sh", ss.str());
  }
  ss << num;

  return mmap_lib::str::concat(get_bits()-1, "'h", ss.str());
}

