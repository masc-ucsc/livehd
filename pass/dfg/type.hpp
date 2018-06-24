//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef TYPE_H_
#define TYPE_H_

#include <cmath>
#include <cstdint>

#include "integer.hpp"

enum TypeName { NUMERIC,
                LOGICAL,
                MAP,
                STRING,
                UNDEF };

class Symbol_Table;

class Type {
public:
  Type(TypeName n = UNDEF, pyrint mi = 0, pyrint ma = 0, pyrint l = 0) : name(n), min(mi), max(ma), len(l), _is_signed(false), _is_input(false), _is_output(false), _is_register(false), _is_private(false), _min_overflow(false), _max_overflow(false), _len_overflow(false), _min_fixed(false), _max_fixed(false), _len_fixed(false) {}
  Type(const Type &t) : name(t.name), min(t.min), max(t.max), len(t.len), _is_signed(t._is_signed), _is_input(t._is_input), _is_output(t._is_output), _is_register(t._is_register), _is_private(t._is_private), _min_overflow(t._min_overflow), _max_overflow(t._max_overflow), _len_overflow(t._len_overflow), _min_fixed(t._min_fixed), _max_fixed(t._max_fixed), _len_fixed(t._len_fixed) {}

  virtual ~Type() {}

  Type &operator=(const Type &other) {
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

  static Type create_logical() { return Type(LOGICAL); }
  static Type create_unsigned(pyrint max, pyrint min) { return Type(NUMERIC, max, min); }
  static Type create_unsigned(pyrint bits) { return Type(NUMERIC, 0, pow(2, bits)); }
  static Type create_string(pyrint len) { return Type(STRING, 0, 0, len); }

  static Type create_signed(pyrint max, pyrint min) {
    Type t(NUMERIC, max, min);
    t.set_signed();

    return t;
  }

  static Type create_signed(pyrint bits) {
    Type t(NUMERIC, pow(2, bits - 1) - 1, -pow(2, bits - 1));
    t.set_signed();

    return t;
  }

  void    merge(const Type &other);
  pyrsize bits() const;

  TypeName get_name() const { return name; }

  pyrint  get_min() const { return min; }
  Integer get_overflow_min() const;
  bool    min_overflow() const { return _min_overflow; }
  void    set_min_overflow(bool v = true) { _min_overflow = v; }

  pyrint  get_max() const { return max; }
  Integer get_overflow_max() const;
  bool    max_overflow() const { return _max_overflow; }
  void    set_max_overflow(bool v = true) { _max_overflow = v; }

  pyrint  get_len() const { return len; }
  Integer get_overflow_len() const;
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

  bool flags_match(const Type &) const;

  std::string to_string() const;
  std::string type_name() const;

  static Symbol_Table *context;   // set in Context
  static const Type undefined;    // undefined type

protected:
  // merge a generic attribute with another, return true if result overflows
  static bool merge_attribute(pyrint *attr1_out, bool attr1_overflow, bool attr1_fixed,
                              pyrint attr2, bool attr2_overflow, int polarity = 1);

  static int compare_attributes(pyrint attr1, bool attr1_overflow, pyrint attr2, bool attr2_overflow);
  static int compare_to_zero(pyrint attr1, bool attr1_overflow) {
    return compare_attributes(attr1, attr1_overflow, 0, false);
  }

  static pyrsize signed_bits_required(pyrint attr, bool attr_overflow);
  static pyrsize unsigned_bits_required(pyrint attr, bool attr_overflow);

  TypeName name;
  pyrint   min, max, len;

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

bool operator==(const Type &t1, const Type &t2);
bool operator!=(const Type &t1, const Type &t2);

#endif
