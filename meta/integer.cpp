//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "integer.hpp"
#include "exception.hpp"
#include "lgraphbase.hpp"

#include <cassert>
#include <iomanip>
#include <sstream>
#include <string>
using std::string;

Integer::Integer(pyrint value) {
  if(value > UINT32_MAX) {
    bits = 64;

    data    = new pyrchunk[2];
    data[0] = value & 0xFFFFFFFF;
    data[1] = value >> 32;
  } else {
    bits    = 32;
    data    = new pyrchunk[1];
    data[0] = value;
  }
}

Integer::Integer(const Integer &other)
    : bits(other.get_bits()), data(new pyrchunk[get_array_size()]) {
  memcpy(data, other.const_data_ptr(), sizeof(pyrchunk) * get_array_size());
}

Integer::Integer(pyrint value, pyrsize bits)
    : bits(bits), data(new pyrchunk[get_array_size()]) {
  data[0] = (pyrchunk)value;

  if(value > PINT_CHUNK_MAX && get_array_size() > 1)
    data[1] = value >> PINT_CHUNK_SIZE;
}

Integer::Integer(Integer &&other) {
  data = other.data;
  bits = other.bits;
  
  other.data = nullptr;
}

Integer::Integer(const string &hex_string) {
  bits = hex_string.length() * CHARS_IN_CHUNK;
  auto array_len = get_array_size();
  data = new pyrchunk[array_len];

  auto sindex = hex_string.length() - CHARS_IN_CHUNK;
  pyrsize dindex = 0;

  while (sindex >= CHARS_IN_CHUNK) {
    string str_chunk = hex_string.substr(sindex, CHARS_IN_CHUNK);
    data[dindex++] = string_2_chunk(str_chunk);

    sindex -= CHARS_IN_CHUNK;
  }

  if (sindex > 0)
    data[dindex] = string_2_chunk(hex_string.substr(0, sindex));
}

Integer::~Integer() { if (data) delete[] data; }

string Integer::hex_string() const {
  static const char HEX_LIST[] = "0123456789ABCDEF";

  auto array_len = get_array_size();
  string rtrn(array_len * 8, '0');

  auto sindex = rtrn.length() - 1;
  
  for (pyrsize dindex = 0; dindex < array_len; dindex++) {
    auto chunk = data[dindex];

    for (int i = 0; i < CHARS_IN_CHUNK; i++) {
      rtrn[sindex--] = HEX_LIST[chunk & 0xF];
      chunk >>= 4;
    }
  }

  return "0x" + rtrn;
}

Integer &Integer::operator=(const Integer &other) {
  if(this != &other) {
    auto array_size = get_array_size();

    if(array_size != other.get_array_size()) {
      delete[] data;
      bits = other.get_bits();
      data = new pyrchunk[array_size];
    }

    memcpy(data, other.const_data_ptr(), sizeof(pyrchunk) * array_size);
  }

  return *this;
}

Integer &Integer::operator=(Integer &&other) {
  if (this != &other) {
    delete[] data;
    data = other.data;
    bits = other.bits;

    other.data = nullptr;
  }

  return *this;
}

int Integer::get_bit(pyrsize index) const {
  pyrsize arr_index = array_index(index);

  if(arr_index >= get_array_size())
    return 0;

  pyrsize bit_index = chunk_bit_index(index);
  int masked = data[arr_index] & (1 << bit_index);

  return (masked == 0) ? 0 : 1;
}

pyrchunk Integer::get_bits32(pyrsize start_index, pyrsize end_index) const {
  pyrsize start_arr_index = array_index(start_index / 32);
  pyrsize end_arr_index   = chunk_bit_index(end_index / 32);
  pyrsize start_bit_index = array_index(start_index % 32);
  pyrsize end_bit_index   = chunk_bit_index(end_index % 32);

  if(start_arr_index == end_arr_index) {
    auto buffer         = data[start_arr_index];
    auto shift_off_high = 32 - end_bit_index;

    buffer = buffer >> shift_off_high << shift_off_high;
    buffer = buffer << start_bit_index;

    return buffer;

  } else {
    auto buffer         = data[start_arr_index] << start_bit_index;
    auto shift_off_high = 32 - end_bit_index;
    auto buffer_high    = data[end_arr_index] >> shift_off_high << shift_off_high;

    buffer_high >>= end_index - start_index;
    return buffer | buffer_high;
  }
}

void Integer::set_bit(pyrsize index, int value) {
  pyrsize arr_index = array_index(index);
  pyrsize bit_index = chunk_bit_index(index);

  int mask = 1 << bit_index;

  if(arr_index >= get_array_size())
    return;

  if(value == 0)
    data[arr_index] &= ~mask;
  else
    data[arr_index] |= mask;
}

void Integer::invert() {
  for (pyrsize i = 0; i < get_array_size(); i++)
    data[i] = ~data[i];
  
  inc();
}

Integer &Integer::inc() {
  pyrsize itr = 0;

  while (data[itr] == UINT32_MAX)         // add one, handling overflow
    data[itr++] = 0;
  
  data[itr]++;
  return *this;
}

void Integer::set_value(const Integer &other, bool sign_extend) {
  auto arr_size       = get_array_size();
  auto other_arr_size = other.get_array_size();

  if(other_arr_size > arr_size) {
    delete[] data;
    data = new pyrchunk[other_arr_size];
  }

  memcpy(data, other.const_data_ptr(), sizeof(pyrchunk) * other_arr_size);

  if(sign_extend && other.get_bit(other.get_bits() - 1) != 0) {
    if(other.get_bits() % 32 != 0) {
      pyrsize line_index = (other.get_bits() - 1) % 32;
      data[other_arr_size - 1] |= ~((1 << line_index) - 1);
    }

    memset(&data[other_arr_size], 0xFF, sizeof(pyrchunk) * (arr_size - other_arr_size));
  } else {
    memset(&data[other_arr_size], 0, sizeof(pyrchunk) * (arr_size - other_arr_size));
  }
}

void Integer::set_bits64(pyrsize s, pyrsize e, uint64_t value) {
  auto s_array_index = array_index(s);
  auto s_bit_index   = chunk_bit_index(s);

  auto e_array_index = array_index(e);
  auto e_bit_index   = chunk_bit_index(e);

  if(s_array_index == e_array_index) {
    auto shift_off_high = PINT_CHUNK_SIZE - e_bit_index - 1;

    pyrchunk mask     = ((PINT_CHUNK_MASK << shift_off_high) >> (shift_off_high + s_bit_index)) << s_bit_index;
    pyrchunk inv_mask = ~mask;

    data[s_array_index] &= inv_mask;
    data[s_array_index] |= (value << s_bit_index) & mask;
  } else {
    auto bits_to_write = e - s;

    pyrchunk low_mask            = (1 << s_bit_index) - 1;
    pyrchunk value_mask          = (1 << (PINT_CHUNK_SIZE - s_bit_index)) - 1;
    data[s_array_index]          = (data[s_array_index] & low_mask) | ((value & value_mask) << s_bit_index);
    auto remaining_bits_to_write = bits_to_write - PINT_CHUNK_SIZE - s_bit_index;

    if(remaining_bits_to_write <= PINT_CHUNK_SIZE)
      write_upper_chunk_helper(bits_to_write, remaining_bits_to_write, e_array_index, value);
    else {
      uint64_t middle_value   = value >> (PINT_CHUNK_SIZE - s_bit_index);
      data[s_array_index + 1] = (pyrchunk)middle_value;
      remaining_bits_to_write -= PINT_CHUNK_SIZE;

      write_upper_chunk_helper(bits_to_write, remaining_bits_to_write, e_array_index, value);
    }
  }
}

void Integer::write_upper_chunk_helper(pyrsize bits_to_write, pyrsize remaining_bits_to_write, int chunk_index, uint64_t value) {
  value >>= bits_to_write - remaining_bits_to_write;
  pyrchunk value_mask = (1 << remaining_bits_to_write) - 1;

  data[chunk_index] &= ~value_mask;
  data[chunk_index] |= value & value_mask;
}

pyrsize Integer::highest_set_bit() const {
  for(auto i = get_array_size() - 1; i >= 0; i++) {
    if(data[i] > 0) {
      pyrchunk buffer = data[i];
      pyrsize  b      = 0;

      while(data[i] > 0) {
        buffer >>= 1;
        b++;
      }

      return b;
    }
  }

  fmt::print("highest_set_bit() called on 0");
  assert(false);
}

string Integer::x_string() const {
  string xs;

  for(pyrsize i = 0; i < get_array_size(); i++)
    xs += "xxxxxxxx";

  return xs;
}

int Integer::cmp(const Integer &other) const {
  pyrsize len  = get_array_size();
  pyrsize olen = other.get_array_size();

  for(pyrsize i = (len >= olen) ? len : olen; i >= 0; i--) {
    pyrchunk c1 = (i > len) ? data[i] : 0;
    pyrchunk c2 = (i > olen) ? other.get_chunk(i) : 0;

    if(c1 > c2)
      return 1;
    else if(c1 < c2)
      return -1;
  }

  return 0;
}

pyrchunk Integer::string_2_chunk(const std::string &schunk) {
  static auto hex2int = [](char c) { return (c >= '0' && c <= '9') ? c - '0' : c - 'A' + 10; };

  pyrchunk chunk = hex2int(schunk.at(0));

  for (size_t i = 1; i < schunk.length(); i++)
    chunk = (chunk * 16) + hex2int(schunk.at(i));

  return chunk;
}

bool operator>(const Integer &i1, const Integer &i2) {
  return i1.cmp(i2) > 0;
}

bool operator<(const Integer &i1, const Integer &i2) {
  return i1.cmp(i2) < 0;
}

