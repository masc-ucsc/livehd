//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PYROPE_INTEGER_H_
#define PYROPE_INTEGER_H_

#include <cstdint>
#include <cstring>
#include <string>

using pyrsize   = size_t;
using pyrchunk  = uint32_t;
using pyrint    = long;     // IMPORTANT: this must be larger than Char_Array_ID, defined in char_array.hpp, which is uint32_t

const int      PINT_CHUNK_SIZE = 32;
const pyrchunk PINT_CHUNK_MASK = 0xFFFFFFFF;
const pyrchunk PINT_CHUNK_MAX  = 0xFFFFFFFF;

class Integer {
public:
  Integer(pyrint value);
  Integer(pyrint value, pyrsize size);
  Integer(const Integer &other);
  Integer(Integer &&other);
  ~Integer();

  Integer &operator=(const Integer &other);
  Integer &operator=(      Integer &&other);
  int      cmp(const Integer &other) const;

  pyrchunk get_chunk(pyrsize index) const {
    return data[index];
  }
  void set_chunk(pyrsize index, pyrchunk value) {
    data[index] = value;
  }

  pyrchunk get_bits32(pyrsize start_index, pyrsize end_index) const;
  int      get_bit(pyrsize) const;
  void     set_bit(pyrsize, int);
  void     set_bits64(pyrsize s, pyrsize e, uint64_t value);

  pyrsize get_bits() const {
    return bits;
  }
  pyrsize get_array_size() const {
    return (bits >> 5) + (((bits & 0x1f) != 0) ? 1 : 0);
  }
  pyrsize highest_set_bit() const;

  std::string     str() const;
  const pyrchunk *const_data_ptr() const {
    return data;
  }
  pyrchunk *data_ptr() {
    return data;
  }

  void invert();
  Integer &inc();       // optimized to add one
  void set_value(const Integer &other, bool sign_extend = false);

  static Integer from_buffer(const pyrchunk *, pyrsize); // could not overload as constructor

  static pyrsize array_index(pyrsize bits) {
    return bits >> 5;
  }
  static pyrsize chunk_bit_index(pyrsize bits) {
    return bits & 0x1f;
  }

  std::string x_string() const;

protected:
  void write_upper_chunk_helper(pyrsize bits_to_write, pyrsize remaining_bits_to_write, int chunk_index, uint64_t value);

  pyrsize   bits;
  pyrchunk *data;
};

bool operator>(const Integer &i1, const Integer &i2);
bool operator<(const Integer &i1, const Integer &i2);

#endif
