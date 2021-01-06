
#include "fmt/format.h"
#include "boost/multiprecision/cpp_int.hpp"
#include "absl/types/span.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

#include "mmap_hash.hpp"
#include "lbench.hpp"
#include "lrand.hpp"
#include "iassert.hpp"

#include "lconst.hpp"

std::string_view Lconst::skip_underscores(std::string_view txt) const {
  if (txt.empty() || txt[0] != '_')
    return txt;

  for (auto i = 1u; i < txt.size(); ++i) {
    if (txt[i] != '_') {
      return txt.substr(i);
    }
  }
  return txt; // orig if only underscores
}

Bits_t Lconst::read_bits(std::string_view txt) {
  if (txt.empty())
    return 0;
  if (!std::isdigit(txt[0]))
    return 0;

  unsigned int tmp=0;
  bool ok = absl::SimpleAtoi(txt, &tmp);
  if (!ok && tmp==0) {
    throw std::runtime_error(fmt::format("ERROR: {} should have the number of bits in decimal after u", txt));
  }
  if (tmp <= 0) {
    throw std::runtime_error(fmt::format("ERROR: {} the number of bits should be positive not {}", txt, tmp));
  }
  if (tmp >= Bits_max) {
    throw std::runtime_error(fmt::format("ERROR: {} the number of bits is too big {}", txt, tmp));
  }

  return tmp;
}

Lconst::Container Lconst::serialize() const {

  Container v;
  unsigned char c = (explicit_str?0x10:0) | (is_negative()?0x01:0);
  v.emplace_back(c);
  v.emplace_back(bits>>16);
  v.emplace_back(bits>>8 );
  v.emplace_back(bits    );

  boost::multiprecision::export_bits(num, std::back_inserter(v), 8);

  return v;
}

uint64_t Lconst::hash() const {

  std::vector<uint64_t> v;
  uint64_t c = (explicit_str?0x10:0) | (is_negative()?0x01:0);
  c = (c<<32) | bits;
  v.emplace_back(c);

  boost::multiprecision::export_bits(num, std::back_inserter(v), 64);

  return mmap_lib::hash64(v.data(),v.size()*8);
}

Lconst::Lconst(absl::Span<unsigned char> v) {

  I(v.size()>3); // invalid otherwise

  uint8_t c0  = v[0];
  uint32_t c1 = v[1];
  uint32_t c2 = v[2];
  uint32_t c3 = v[3];

  explicit_str  = (c0 & 0x10)?true:false;

  bits = (c1<<16) | (c2<<8) | c3;

  auto s = v.subspan(4);
  boost::multiprecision::import_bits(num,s.begin(),s.end());

  if (c0&0x01) { // negative
    num = -num;
  }
}

Lconst::Lconst(const Container &v) {

  I(v.size()>4); // invalid otherwise

  uint8_t c0  = v[0];
  uint32_t c1 = v[1];
  uint32_t c2 = v[2];
  uint32_t c3 = v[3];

  explicit_str  = (c0 & 0x10)?true:false;

  bits = (c1<<16) | (c2<<8) | c3;

  boost::multiprecision::import_bits(num,v.begin()+4,v.end());
  if (c0&0x01) { // negative
    num = -num;
  }
}

Lconst::Lconst() {
  explicit_str  = false;
  bits          = 1;
  num           = 0;
}

Lconst::Lconst(int64_t v) {
  explicit_str  = false;
  num           = v;
  bits          = calc_num_bits();
}

Lconst::Lconst(Number v) {
  explicit_str  = false;
  num           = v;
  bits          = calc_num_bits();
}

Lconst::Lconst(std::string_view orig_txt) {

  explicit_str  = false;
  bits          = 0;
  num           = 0;

  if (orig_txt.empty())
    return;

  // Skip leading _ as needed

  std::string_view txt { orig_txt };

  if (!txt.empty() && txt[0] == '_') {
    auto txt2 = skip_underscores(txt);

    if (std::isdigit(txt2[0]) || txt2[0] == '-' || txt2[0] == '+') {
      txt = txt2;
    }
  }

  bool negative = false;
  if (txt[0] == '-') {  // negative (may be unsigned -1u)
    negative = true;
    txt = skip_underscores(txt.substr(1)); // skip -
  }

  Bits_t nbits_used = 0;

  int shift_mode = 0;
  if (txt.size() > 2 && txt[0] == '0') {
    shift_mode = char_to_shift_mode[(uint8_t)txt[1]];
  }
  if (shift_mode == 1 && negative) {
    shift_mode   = 0;  // string
    explicit_str = true;
  }

  bool unsign_set = false;

  if (shift_mode) {
    if (shift_mode==1) { // 0b binary
      for(const auto ch:txt.substr(2)) {
        if (ch == '0' || ch == '1' || ch == '_')
          continue;

        // handle binary with special characters ?xZ...
        std::string bin;
        for(auto i=2u;i<txt.size();++i) {
          const auto ch2 = txt[i];
          if (ch2=='_')
            continue;
          if (ch2 == '?' || ch2 == 'x' || ch2 == 'X')
            bin.append(1, '?');
          else if (ch2 == 'Z' || ch2 == 'z')
            bin.append(1, 'z');
          else if (ch2 == '0' || ch2 == '1')
            bin.append(1, ch2);
          else {
            throw std::runtime_error(fmt::format("ERROR: {} binary encoding could not use {}\n", orig_txt, ch2));
          }
        }

        for (int i = bin.size() - 1; i >= 0; --i) {
          if (bin[i] == '_') continue;
          if (bin[i] == 'b') break; // delim for 0b reached

          num <<= 8;
          num  += bin[i];
          nbits_used++;
        }
        bits = nbits_used;

        I(nbits_used<=bits);
        explicit_str  = true;

        I(!negative);

        return;
      }
    }
    I(shift_mode == 1 || shift_mode == 3 || shift_mode==4); // binary or octal or hexa

    for(auto i=2u;i<txt.size();++i) {
      auto v = char_to_val[(uint8_t)txt[i]];
      if (likely(v >= 0)) {
        auto char_sa = char_to_bits[(uint8_t)txt[i]];
        if (unlikely(char_sa > shift_mode)) {
          throw std::runtime_error(fmt::format("ERROR: {} invalid syntax for number {} bits needed for '{}'", orig_txt, char_sa, txt[i]));
          return;
        }
        num = (num << shift_mode) | v;
        if (nbits_used==0) // leading zeroes adds 0 bits, leading 3 adds 2 bits...
          nbits_used = char_sa;
        else
          nbits_used += shift_mode;
        continue;
      }else{
        if (txt[i] == '_') continue;

        throw std::runtime_error(fmt::format("ERROR: {} encoding could not use {}\n", orig_txt, txt[i]));
      }
    }
    if (nbits_used==0)
      nbits_used=1;
  }else if (!explicit_str && (std::isdigit(txt[0]) || txt[0] == '-' || txt[0] == '+')) {
    auto start_i = 0u;
    if (txt.size() > 2 && txt[0] == '0' && (txt[1] == 'd' || txt[1] == 'D')) {
      start_i = 2;
    }
    for (auto i = start_i; i < txt.size(); ++i) {
      auto v = char_to_val[(uint8_t)txt[i]];
      if (likely(v >= 0)) {
        num = (num*10) + v;
      } else {
        if (txt[i] == '_') continue;

        throw std::runtime_error(fmt::format("ERROR: {} encoding could not use {}\n", orig_txt, txt[i]));
      }
    }

    nbits_used = calc_num_bits();
  }else{
    // alnum string (not number, still convert to num)
    for(int i=orig_txt.size()-1; i>=0 ;--i) {
      num <<= 8;
      num += orig_txt[i];
    }
    bits          = orig_txt.size() * 8;
    explicit_str  = true;
  }

  if (bits && !explicit_str) {
    if (num>0 && bits < (nbits_used-1)) {
      throw std::runtime_error(
          fmt::format("ERROR: {} bits set would truncate the positive value:{} which needs bits:{}\n", bits, orig_txt, nbits_used));
    }else if (num<0 && bits < nbits_used) {
      throw std::runtime_error(
          fmt::format("ERROR: {} bits set would truncate the negative value:{} which needs bits:{}\n", bits, orig_txt, nbits_used));
    }
  }

  if (negative && !explicit_str) {
    num  = -num;
  }

  if (!explicit_str) {
    if (num<0 && unsign_set) {
      throw std::runtime_error(fmt::format("ERROR: {} is negative and unsigned but bits not set to truncate\n", orig_txt));
    }
    bits = calc_num_bits();
  }

  I(bits);
}

void Lconst::dump() const {
  if (explicit_str)
    fmt::print("str:{} bits:{}\n", to_string(), bits);
  else
    fmt::print("num:{} bits:{}\n", num.str(), bits);
}

Lconst Lconst::adjust(const Number &res_num, const Lconst &o) const {
  // explicit kept if both explicit and agree
  auto res_explicit_str  = explicit_str && o.explicit_str;

  return Lconst(res_explicit_str, calc_num_bits(res_num), res_num);
}

Lconst Lconst::get_mask(Bits_t bits) {
  return Lconst((Number(1)<<bits)-1);
}

Lconst Lconst::get_mask() const {
  return get_mask(get_bits());
}

Lconst Lconst::tposs_op() const {

  Number res_num;
  if (num<0) {
    res_num = (Number(1)<<get_bits())+num;  // mask+num+1
  }else{
    res_num = num;
  }

  return Lconst(explicit_str, calc_num_bits(res_num), res_num);
}

Lconst Lconst::add_op(const Lconst &o) const {

  if (explicit_str || o.explicit_str) {
    auto max_bits = std::max(bits, o.bits);

    std::string str;
    std::string o_str;
    if (explicit_str)
      str = to_string();
    if (o.explicit_str)
      o_str = o.to_string();

    if (str.size()*8==bits && o_str.size()*8==o.bits) { // Both strings (concat)
      return Lconst(absl::StrCat(str, o_str));
    }
    std::string qmarks("0b");
    qmarks.append(max_bits, '?');
    return Lconst(qmarks);
  }

  Number res_num = get_num() + o.get_num();

  return adjust(res_num, o);
}

Lconst Lconst::sub_op(const Lconst &o) const {

  if (explicit_str || o.explicit_str) {
    auto max_bits = std::max(bits, o.bits);

    std::string qmarks("0b");
    qmarks.append(max_bits, '?');
    return Lconst(qmarks);
  }

  Number res_num = get_num() - o.get_num();

  return adjust(res_num, o);
}

Lconst Lconst::lsh_op(Bits_t amount) const {

  if (explicit_str) {
    auto qmarks = to_string();
    qmarks.append(amount, '0');
    return Lconst(qmarks);
  }

  auto res_num  = num << amount;

  return Lconst(explicit_str, calc_num_bits(res_num), res_num);
}

Lconst Lconst::rsh_op(Bits_t amount) const {
  if (explicit_str) {
    auto qmarks = to_string();
    auto s = qmarks.substr(amount);
    return Lconst(s);
  }

  auto res_num  = num >> amount;

  return Lconst(explicit_str, calc_num_bits(res_num), res_num);
}

Lconst Lconst::or_op(const Lconst &o) const {

  if (unlikely(explicit_str || o.explicit_str)) {
    auto   res_bits = std::max(bits, o.bits);
    std::string str;
    std::string o_str;
    if (explicit_str)
      str = to_string();
    if (o.explicit_str)
      o_str = o.to_string();
    if (explicit_str && o.explicit_str) {
      std::string max_str;
      std::string_view min_str;
      if (str.size()>o_str.size()) {
        max_str = str;
        min_str = o_str;
      }else{
        max_str = o_str;
        min_str = str;
      }
      for(auto i= 0u;i<min_str.size();++i) {
        if (o_str[i] == '1' || str[i] == '1')
          max_str[i] = '1';
        else if (o_str[i] == '?' || str[i] == '?')
          max_str[i] = '?';
      }

      return Lconst(absl::StrCat("0b",max_str));
    }
    std::string qmarks("0b");
    qmarks.append(res_bits, '?');
    return Lconst(qmarks);
  }

  Number res_num  = get_num() | o.get_num();

  return adjust(res_num, o);
}

Lconst Lconst::and_op(const Lconst &o) const {

  if (unlikely(explicit_str || o.explicit_str)) {
    auto   res_bits = std::max(bits, o.bits);
    std::string str;
    std::string o_str;
    if (explicit_str)
      str = to_string();
    if (o.explicit_str)
      o_str = o.to_string();
    if (explicit_str && o.explicit_str) {
      std::string max_str;
      std::string_view min_str;
      if (str.size()>o_str.size()) {
        max_str = str;
        min_str = o_str;
      }else{
        max_str = o_str;
        min_str = str;
      }
      for(auto i= 0u;i<min_str.size();++i) {
        if (o_str[i] == '0' || str[i] == '0')
          max_str[i] = '0';
        else if (o_str[i] == '?' || str[i] == '?')
          max_str[i] = '?';
      }

      return Lconst(absl::StrCat("0b",max_str));
    }
    std::string qmarks("0b");
    qmarks.append(res_bits, '?');
    return Lconst(qmarks);
  }
  Number res_num  = get_num() & o.get_num();

  return adjust(res_num, o);
}

int Lconst::eq_op(const Lconst &o) const {
  if (unlikely(explicit_str || o.explicit_str)) {
    I(false); // if any of them has ??, it can not be computed at compile time. Runtime random answer
  }

  return num == o.num?-1:0;

#if 0
  auto b = num & o.num;  // zero-extend or drop bits from negative

  if (num<0 && o.num>0)
    return b == o.num;
  if (num>0 && o.num<0)
    return b == num;
  return (b==num) && (b==o.num);
#endif
}

Lconst Lconst::adjust_bits(Bits_t amount) const {
  I(amount>0);

  Number r(1);
  Number res_num = num & ((r<<amount)-1);

  return Lconst(explicit_str, calc_num_bits(res_num), res_num);
}

std::string Lconst::to_string() const {
  I(explicit_str);

  std::string str;
  Number tmp = num;
  while(tmp) {
    unsigned char ch = static_cast<unsigned char>(tmp & 0xFF);
    str.append(1, ch);
    tmp >>= 8;
  }

  return str;
}

std::string Lconst::to_string_no_xz() const {
  I(explicit_str);

  std::string str;
  Number tmp = num;
  while(tmp) {
    unsigned char ch = static_cast<unsigned char>(tmp & 0xFF);
    if (ch == 'z' || ch == 'x')
      str.append(1, '0');
    else
      str.append(1, ch);
    tmp >>= 8;
  }

  return str;
}

std::string Lconst::to_pyrope() const {

  if (explicit_str) {
    // Either string or 0b with special characters like ?xz
    auto str = to_string();
    if (str.size()*8 == bits)
      return absl::StrCat("'", str, "'");

    I(str[0] != '-');
    auto str2 = absl::StrCat("0b", str);
    return str2;
  }

  const auto v = get_num();
  std::stringstream ss;

  bool print_hexa = v > 63;
  if (print_hexa) {
    ss << std::hex;
  }

  std::string str;
  if (v<0) {
    str.append(1,'-');
    ss << -v;
  }else{
    ss << v;
  }

  if (print_hexa)
    absl::StrAppend(&str, "0x");
  absl::StrAppend(&str, ss.str());

  return str;
}

std::string Lconst::to_firrtl() const {

  /*Note->hunter: FIRRTL-Proto requires the string output
   * here is a decimal value (no 0x or 0d allowed. Only #).
   * Also means it can't have 'x' or 'z'. */
  Number v;
  if (explicit_str) {
    // Either string or 0b with special characters like ?xz
    auto str = to_string_no_xz();
    if (str.size()*8 == bits)
      return str;

    I(str[0] != '-');
    //Is in 0b form, need to convert from that.
    auto temp_lconst = Lconst(str);
    v = temp_lconst.get_num();
  } else {
    v = get_num();
  }
  std::stringstream ss;

  if (v<0)
    ss << -v;
  else
    ss << v;

  std::string str;
  if (is_negative())
    str.append(1,'-');

  absl::StrAppend(&str, ss.str());

  return str;
}

int64_t Lconst::to_i() const {
  I(is_i());
  return static_cast<long int>(num);
}

std::string Lconst::to_yosys(bool do_unsign) const {

  if (explicit_str) {
    // Either string or 0b with special characters like ?xz
    return to_string();
  }


  auto v = get_num();
  GI(do_unsign, v>=0);

  std::string txt;
  auto nbits = get_bits();
  if (do_unsign)
    --nbits;
  for(auto i=0u;i<nbits;++i) {
    if (v&1) {
      txt.append(1, '1');
    }else{
      txt.append(1, '0');
    }
    v = v>>1;
  }
  const std::string rev(txt.rbegin(),txt.rend());

  return rev;
}

std::string Lconst::to_verilog() const {

  if (explicit_str) {
    // Either string or 0b with special characters like ?xz
    auto str = to_string();
    if (str.size()*8 == bits)
      return absl::StrCat("\"", str, "\"");

    I(str[0] != '-');
    return absl::StrCat("'b", str);
  }

  std::string str;
  absl::StrAppend(&str, (int)bits);

  // Hexa
  auto v = get_num();
  if (v<0) { // Negative
    std::string txt;
    for(auto i=0u;i<get_bits();++i) {
      if (v&1) {
        txt.append(1, '1');
      }else{
        txt.append(1, '0');
      }
      v = v>>1;
    }
    const std::string rev(txt.rbegin(),txt.rend());

    absl::StrAppend(&str, "'sb", rev);
  }else{
    std::stringstream ss;
    ss << std::hex;
    ss << v;

    if (is_negative())
      absl::StrAppend(&str, "'sh", ss.str());
    else
      absl::StrAppend(&str, "'h", ss.str());
  }

  return str;
}

