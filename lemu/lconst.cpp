
#include "fmt/format.h"
#include "boost/multiprecision/cpp_int.hpp"
#include "absl/types/span.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

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

  int tmp=0;
  bool ok = absl::SimpleAtoi(txt, &tmp);
  if (!ok && tmp==0) {
    throw std::runtime_error(fmt::format("ERROR: {} should have the number of bits in decimal after u", txt));
  }
  if (tmp <= 0) {
    throw std::runtime_error(fmt::format("ERROR: {} the number of bits should be positive not {}", txt, tmp));
  }
  if (tmp >= ((1UL<<Bits_bits)-1)) {
    throw std::runtime_error(fmt::format("ERROR: {} the number of bits is too big {}", txt, tmp));
  }

  return tmp;
}

void Lconst::process_ending(std::string_view txt, size_t pos) {
  bool u = (txt[pos] == 'u' || txt[pos] == 'U');
  bool s = (txt[pos] == 's' || txt[pos] == 'S');

  explicit_sign = u || s;
  sign = s;

  if (explicit_sign) {
    pos++;
    if (pos < txt.size()) {
      while(pos < txt.size() && txt[pos] == '_')
        pos++;

      bits = read_bits(txt.substr(pos));
      explicit_bits = true;
    }
  }else{
    throw std::runtime_error(fmt::format("ERROR: {} invalid number format", txt));
  }
}

Lconst::Container Lconst::serialize() const {

  Container v;
  unsigned char c = (explicit_str?0x10:0) | (explicit_sign?0x8:0) | (explicit_bits?0x4:0) | (sign?0x2:0);
  v.emplace_back(c);
  v.emplace_back(bits>>16);
  v.emplace_back(bits>>8 );
  v.emplace_back(bits    );

  boost::multiprecision::export_bits(num, std::back_inserter(v), 8);

  return v;
}

uint64_t Lconst::hash() const {

  std::vector<uint64_t> v;
  uint64_t c = (explicit_str?0x10:0) | (explicit_sign?0x8:0) | (explicit_bits?0x4:0) | (sign?0x2:0);
  c = (c<<32) | bits;
  v.emplace_back(c);

  boost::multiprecision::export_bits(num, std::back_inserter(v), 64);

  auto h=0;
  for (auto e : v) {
    h ^= e;
  }

  return h;
}

Lconst::Lconst(absl::Span<unsigned char> v) {

  I(v.size()>3); // invalid otherwise

  uint8_t c0  = v[0];
  uint32_t c1 = v[1];
  uint32_t c2 = v[2];
  uint32_t c3 = v[3];

  explicit_str  = (c0 & 0x10)?true:false;
  explicit_sign = (c0 & 0x08)?true:false;
  explicit_bits = (c0 & 0x04)?true:false;
  sign          = (c0 & 0x02)?true:false;

  bits = (c1<<16) | (c2<<8) | c3;

  auto s = v.subspan(4);
  boost::multiprecision::import_bits(num,s.begin(),s.end());
}

Lconst::Lconst(const Container &v) {

  I(v.size()>4); // invalid otherwise

  uint8_t c0  = v[0];
  uint32_t c1 = v[1];
  uint32_t c2 = v[2];
  uint32_t c3 = v[3];

  explicit_str  = (c0 & 0x10)?true:false;
  explicit_sign = (c0 & 0x08)?true:false;
  explicit_bits = (c0 & 0x04)?true:false;
  sign          = (c0 & 0x02)?true:false;

  bits = (c1<<16) | (c2<<8) | c3;

  boost::multiprecision::import_bits(num,v.begin()+4,v.end());
}

Lconst::Lconst() {
  explicit_str  = false;
  explicit_sign = false;
  explicit_bits = false;
  sign          = false;
  bits          = 1;
  num           = 0;
}

Lconst::Lconst(uint64_t v) {
  explicit_str  = false;
  explicit_sign = false;
  explicit_bits = false;
  sign          = false;
  num           = v;
  bits          = calc_bits();
}

Lconst::Lconst(Number v) {
  explicit_str  = false;
  explicit_sign = false;
  explicit_bits = false;
  sign          = v<0;
  num           = v;
  bits          = calc_bits();
}

Lconst::Lconst(uint64_t v, Bits_t b) {
  explicit_str  = false;
  explicit_sign = false;
  explicit_bits = true;
  sign          = false;
  num           = (v & ((1ULL<<b)-1)); // clear upper bits if present
  bits          = b;
  I(calc_bits() <= bits);
}

Lconst::Lconst(std::string_view orig_txt) {

  explicit_str  = false;
  explicit_sign = false;
  explicit_bits = false;
  sign          = false;
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

  if (shift_mode) {
    if (shift_mode==1) { // 0b binary
      for(const auto ch:txt.substr(2)) {
        if (ch == '0' || ch == '1' || ch == '_')
          continue;
        if (ch == 'u' || ch == 'U' || ch == 's' || ch == 'S')
          break;

        // handle binary with special characters ?xZ...
        std::string bin;
        for(auto i=2u;i<txt.size();++i) {
          const auto ch2 = txt[i];
          if (ch2=='_')
            continue;
          if (ch2 == 'u' || ch2 == 'U' || ch2 == 's' || ch2 == 'S') {
            process_ending(txt, i);
            break;
          }
          bin.append(1, ch2);
        }

        for (int i = bin.size() - 1; i >= 0; --i) {
          if (bin[i] == '_') continue;
          if (bin[i] == 'b') break; // delim for 0b reached

          num <<= 8;
          num  += bin[i];
          nbits_used++;
        }
        if (bits==0)
          bits = nbits_used;

        I(nbits_used<=bits);
        explicit_str  = true;
        explicit_bits = true;

#if 0
        num <<=8;
        num += (int)'b';

        num <<=8;
        num  += (int)'0';
#endif

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
          throw std::runtime_error(fmt::format("ERROR: {} invalid syntax for number {} bits needed for '{}'", txt, char_sa, txt[i]));
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

        process_ending(txt, i);
        break;
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

        process_ending(txt, i);
        break;
      }
    }

    nbits_used = calc_bits();
  }else{
    // alnum string (not number, still convert to num)
    for(int i=orig_txt.size()-1; i>=0 ;--i) {
      num <<= 8;
      num += orig_txt[i];
    }
    bits          = orig_txt.size() * 8;
    explicit_str  = true;
    explicit_bits = true;
  }

  if (bits && bits < nbits_used) {
    throw std::runtime_error(
        fmt::format("ERROR: {} bits set would truncate value {} which needs {} bits\n", bits, txt, nbits_used));
  }

  if (negative && !explicit_str) {
    num  = -num;
    if (!explicit_sign)
      sign = true;
  }

  if (explicit_bits) {
    if (num < 0 && !sign && explicit_sign) {  // convert to positive
      Number mask(1);
      mask = (mask << bits) - 1;
      num  = num & mask;
      I(num>0);
    }
  } else {
    bits = nbits_used;
  }

  I(bits);
}

void Lconst::dump() const {
  if (explicit_str)
    fmt::print("str:{} bits:{}\n", to_string(), bits);
  else
    fmt::print("num:{} sign:{} bits:{} explicit_bits:{} explicit_sign:{}\n", num.str(), sign, bits, explicit_bits, explicit_sign);
}

Lconst Lconst::add_op(const Lconst &o) const {
  auto max_bits = std::max(bits, o.bits);

  Number res_num = get_num(max_bits) + o.get_num(max_bits);

  Bits_t res_bits=0u;
  if (res_num<0)
    res_bits = msb(-res_num)+1;
  else if (res_num==0)
    res_bits = 0;
  else
    res_bits = msb(res_num)+1;

  auto res_sign = sign && o.sign;
  if (res_sign)
    res_bits++;

  // explicit kept if both explicit and agree
  auto res_explicit_str  = explicit_str && o.explicit_str;
  auto res_explicit_sign = explicit_sign && o.explicit_sign && sign == o.sign;
  bool res_explicit_bits = false;

  return Lconst(res_explicit_str, res_explicit_sign, res_explicit_bits, res_sign, res_bits, res_num);
}

Lconst Lconst::sub_op(const Lconst &o) const {
  auto max_bits = std::max(bits, o.bits);

  Number res_num = get_num(max_bits) - o.get_num(max_bits);

  Bits_t res_bits=0u;
  if (res_num<0)
    res_bits = msb(-res_num)+1;
  else
    res_bits = msb(res_num)+1;

  auto res_sign = sign && o.sign;
  if (res_sign)
    res_bits++;

  // explicit kept if both explicit and agree
  auto res_explicit_str  = explicit_str && o.explicit_str;
  auto res_explicit_sign = explicit_sign && o.explicit_sign && sign == o.sign;
  bool res_explicit_bits = false;

  return Lconst(res_explicit_str, res_explicit_sign, res_explicit_bits, res_sign, res_bits, res_num);
}

Lconst Lconst::lsh_op(Bits_t amount) const {
  auto res_bits = bits + amount;
  auto res_num  = num << amount;

  return Lconst(explicit_str, explicit_sign, explicit_bits, sign, res_bits, res_num);
}

Lconst Lconst::or_op(const Lconst &o) const {
  auto   res_bits = std::max(bits, o.bits);
  Number res_num  = get_num(res_bits) | o.get_num(res_bits);

  auto res_explicit_str  = explicit_str && o.explicit_str;
  auto res_explicit_sign = explicit_sign && o.explicit_sign && sign == o.sign;
  bool res_explicit_bits = explicit_bits && explicit_bits;
  auto res_sign = sign && o.sign;

  return Lconst(res_explicit_str, res_explicit_sign, res_explicit_bits, res_sign, res_bits, res_num);
}

Lconst Lconst::and_op(const Lconst &o) const {
  auto   res_bits = std::max(bits, o.bits);
  Number res_num  = get_num(res_bits) & o.get_num(res_bits);

  auto res_explicit_str  = explicit_str && o.explicit_str;
  auto res_explicit_sign = explicit_sign && o.explicit_sign && sign == o.sign;
  bool res_explicit_bits = explicit_bits && explicit_bits;
  auto res_sign = sign && o.sign;

  return Lconst(res_explicit_str, res_explicit_sign, res_explicit_bits, res_sign, res_bits, res_num);
}

bool Lconst::eq_op(const Lconst &o) const {
  auto b = num & o.num;  // zero-extend or drop bits from negative
  if (num<0 && o.num>0)
    return b == o.num;
  if (num>0 && o.num<0)
    return b == num;
  return (b==num) && (b==o.num);
}

Lconst Lconst::adjust_bits(Bits_t amount) const {
  I(amount>0);

  auto res_bits = amount;

  Number r(1);
  Number res_num = num & ((r<<amount)-1);

  return Lconst(explicit_str, explicit_sign, true, sign, res_bits, res_num);
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
  fmt::print("to_string_firrtl: {}\n", str);

  return str;
}

void Lconst::pyrope_bits(std::string *str) const {
  if (!explicit_sign && !explicit_bits) {
    return;
  }

  absl::StrAppend(str, sign?"s":"u");

  if (explicit_bits && bits>0) {
    absl::StrAppend(str, bits, bits>1?"bits":"bit");
  }
}

std::string Lconst::to_pyrope() const {

  if (explicit_str) {
    // Either string or 0b with special characters like ?xz
    auto str = to_string();
    if (str.size()*8 == bits)
      return absl::StrCat("'", str, "'");

    I(str[0] != '-');
    auto str2 = absl::StrCat("0b", str);
    pyrope_bits(&str2);
    return str2;
  }

  const auto v = get_num(bits);
  std::stringstream ss;

  bool print_hexa = explicit_bits || v > 6;
  if (print_hexa) {
    ss << std::hex;
  }

  if (v<0)
    ss << -v;
  else
    ss << v;

  std::string str;
  if (is_negative())
    str.append(1,'-');

  if (print_hexa)
    absl::StrAppend(&str, "0x");
  absl::StrAppend(&str, ss.str());

  pyrope_bits(&str);

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
    v = temp_lconst.get_num(bits);
  } else {
    v = get_num(bits);
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

long int Lconst::to_i() const {
  I(is_i());
  return static_cast<long int>(num);
}

std::string Lconst::to_yosys() const {

  if (explicit_str) {
    // Either string or 0b with special characters like ?xz
    return to_string();
  }

  const auto v = get_num(bits);
  std::string str;
  for (int i = bits-1; i >= 0; --i) {
    str.append(1, bit_test(v, i) ? '1' : '0');
  }

  return str;
}

std::string Lconst::to_verilog() const {

  if (explicit_str) {
    // Either string or 0b with special characters like ?xz
    auto str = to_string();
    if (str.size()*8 == bits)
      return absl::StrCat("\"", str, "\"");

    I(str[0] != '-');
    if (explicit_bits)
      return absl::StrCat((int)bits, "'b", str);
    return absl::StrCat("'b", str);
  }

  std::stringstream ss;
  ss << std::hex;
  const auto v = get_num(bits);
  if (v<0)
    ss << -v;
  else
    ss << v;

  std::string str;
  if (is_negative())
    str.append(1,'-');

  if (explicit_bits)
    absl::StrAppend(&str, (int)bits);

  absl::StrAppend(&str, "'h", ss.str());

  return str;
}

