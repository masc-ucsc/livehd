//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef OVERFLOW_POOL_H_
#define OVERFLOW_POOL_H_

#include "char_array.hpp"
#include "integer.hpp"
#include <string>

class Overflow_Pool {
public:
  Overflow_Pool(const std::string &path) : pool(path, "_ofp") {}

  Char_Array_ID save(const Integer &i) {
    auto  buffer_size = i.get_array_size() * sizeof(pyrchunk);
    char *buffer      = new char[buffer_size + 1];

    memcpy(buffer, i.const_data_ptr(), buffer_size);
    buffer[buffer_size] = '\0';

    auto id = pool.create_id(buffer, i.get_bits());
    delete[] buffer;

    return id;
  }

  Integer load(Char_Array_ID id) const {
    const char *buffer = pool.get_char(id);
    pyrsize     size   = pool.get_field(id);

    return Integer::from_buffer((const pyrchunk *)buffer, size);
  }

private:
  Char_Array<pyrsize> pool;
};

#endif
