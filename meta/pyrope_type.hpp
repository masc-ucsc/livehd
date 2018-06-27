//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef Pyrope_Type_H_
#define Pyrope_Type_H_

#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "char_array.hpp"
#include "integer.hpp"

enum Type_Name { NUMERIC,
                LOGICAL,
                MAP,
                STRING,
                UNDEF };

class LGraph_Symbol_Table;

class Pyrope_Type {
public:
  Pyrope_Type(Type_Name n = UNDEF, pyrint mi = 0, pyrint ma = 0, pyrint l = 0)
    : name(n), min(mi), max(ma), len(l), _is_signed(false), _is_input(false), _is_output(false)
    , _is_register(false), _is_private(false), _min_overflow(false), _max_overflow(false)
    , _len_overflow(false), _min_fixed(false), _max_fixed(false), _len_fixed(false) { }

  Pyrope_Type(const Pyrope_Type &t)
    : name(t.name), min(t.min), max(t.max), len(t.len), _is_signed(t._is_signed)
    , _is_input(t._is_input), _is_output(t._is_output), _is_register(t._is_register)
    , _is_private(t._is_private), _min_overflow(t._min_overflow), _max_overflow(t._max_overflow)
    , _len_overflow(t._len_overflow), _min_fixed(t._min_fixed), _max_fixed(t._max_fixed), _len_fixed(t._len_fixed) {}

  virtual ~Pyrope_Type() {}

  Pyrope_Type &operator=(const Pyrope_Type &other) {
    if(this != &other) {
      name = other.name;
      min  = other.min;
      max  = other.max;
      len  = other.len;

      _is_signed   = other._is_signed;
      _is_input    = other._is_input;
      _is_output   = other._is_output;
      _is_register = other._is_register;
      _is_private  = other._is_private;

      _max_overflow = other._max_overflow;
      _min_overflow = other._min_overflow;
      _len_overflow = other._len_overflow;

      _max_fixed = other._max_fixed;
      _min_fixed = other._min_fixed;
      _len_fixed = other._len_fixed;
    }

    return *this;
  }

  void    merge(LGraph_Symbol_Table *context, const Pyrope_Type &other);
  pyrsize bits(LGraph_Symbol_Table *context) const;

  Type_Name get_name() const { return name; }

  pyrint  get_min() const { return min; }
  Integer get_overflow_min(LGraph_Symbol_Table *context) const;
  bool    min_overflow() const { return _min_overflow; }
  void    set_min_overflow(bool v = true) { _min_overflow = v; }
  void    set_min(pyrint m) { min = m; }
  void    set_min(Char_Array_ID cid) {
    min = cid;
    _min_overflow = true;
  }

  pyrint  get_max() const { return max; }
  Integer get_overflow_max(LGraph_Symbol_Table *context) const;
  bool    max_overflow() const { return _max_overflow; }
  void    set_max(pyrint m) { max = m; }
  void    set_max(Char_Array_ID cid) {
    max = cid;
    _max_overflow = true;
  }

  void    set_max_overflow(bool v = true) { _max_overflow = v; }

  pyrint  get_len() const { return len; }
  void    set_len(pyrint l) { len = l; }
  Integer get_overflow_len(LGraph_Symbol_Table *context) const;
  bool    len_overflow() const { return _len_overflow; }
  void    set_len_overflow(bool v = true) { _len_overflow = v; }

  bool is_signed() const { return _is_signed; }
  void set_signed(bool v = true) { _is_signed = v; }

  bool is_input() const { return _is_input; }
  void set_input(bool v = true) { _is_input = v; }

  bool is_output() const { return _is_output; }
  void set_output(bool v = true) { _is_output = v; }

  bool is_register() const { return _is_register; }
  void set_register(bool v = true) { _is_register = v; }

  bool is_private() const { return _is_private; }
  void set_private(bool v = true) { _is_private = v; }

  bool flags_match(const Pyrope_Type &) const;

  std::string to_string() const;
  std::string type_name() const;

protected:
  // merge a generic attribute with another, return true if result overflows
  static bool merge_attribute(LGraph_Symbol_Table *context, pyrint *attr1_out, bool attr1_overflow, bool attr1_fixed,
                              pyrint attr2, bool attr2_overflow, int polarity = 1);

  static int compare_attributes(LGraph_Symbol_Table *context, pyrint attr1, bool attr1_overflow, pyrint attr2, bool attr2_overflow);
  static int compare_to_zero(LGraph_Symbol_Table *context, pyrint attr1, bool attr1_overflow) {
    return compare_attributes(context, attr1, attr1_overflow, 0, false);
  }

  static pyrsize signed_bits_required(LGraph_Symbol_Table *context, pyrint attr, bool attr_overflow);
  static pyrsize unsigned_bits_required(LGraph_Symbol_Table *context, pyrint attr, bool attr_overflow);

  LGraph_Symbol_Table    *lgref;
  Type_Name name;
  pyrint    min, max, len;

  bool _is_signed : 1;
  bool _is_input : 1;
  bool _is_output : 1;
  bool _is_register : 1;
  bool _is_private : 1;

  bool _min_overflow : 1;
  bool _max_overflow : 1;
  bool _len_overflow : 1;

  bool _min_fixed : 1;
  bool _max_fixed : 1;
  bool _len_fixed : 1;
};

bool operator==(const Pyrope_Type &t1, const Pyrope_Type &t2);
bool operator!=(const Pyrope_Type &t1, const Pyrope_Type &t2);

class Exception : public std::runtime_error {
public:
  Exception(const std::string &m) : std::runtime_error(msg) {}

protected:
  std::string msg;
};

class Type_Error : public Exception {
public:
  Type_Error(const std::string &msg) : Exception(msg) {}
};

class Debug_Error : public Exception {
public:
  Debug_Error(const std::string &msg) : Exception(msg) {}
  Debug_Error()                       : Exception("Debug Error") {}
};

#endif
