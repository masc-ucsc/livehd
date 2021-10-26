//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "explicit_type.hpp"
#include "fmt/format.h"
#include "iassert.hpp"
#include "lconst.hpp"
#include "likely.hpp"
#include "mmap_tree.hpp"

using Lg_id_t = uint32_t;

// Lgraph basic core types used all over
using Lg_type_id  = Explicit_type<Lg_id_t, struct Lg_type_id_struct, 0>;  // Global used all over
using Index_id    = Explicit_type<Lg_id_t, struct Index_id_struct, 0>;
using Lut_type_id = Explicit_type<Lg_id_t, struct Lut_type_id_struct, 0>;

class Hierarchy_data {  // 64bits total
public:
  Lg_type_id lgid;
  Index_id   up_nid;
  Hierarchy_data() : lgid(0), up_nid(0) {}
  Hierarchy_data(const Lg_type_id& _class_id, const Index_id& _nid) : lgid(_class_id), up_nid(_nid) {}

  bool is_invalid() const { return lgid == 0; }
};

using Hierarchy_index = mmap_lib::str;

struct Lg_type_id_hash {
  size_t operator()(const Lg_type_id& obj) const { return obj.value; }
};

struct Index_id_hash {
  size_t operator()(const Index_id& obj) const { return obj.value; }
};

using Port_ID = uint16_t;  // ports have a set order (a-b != b-a)

constexpr Index_id Hardcoded_input_nid  = 1;
constexpr Index_id Hardcoded_output_nid = 2;

constexpr int Index_bits = std::numeric_limits<Lg_id_t>::digits - 1;  // 31 bit to have Sink/Driver + Index in 32 bits
constexpr int Port_bits  = std::numeric_limits<Port_ID>::digits - 1;

// NOTE: Bits_bits defined in lconst.hpp
constexpr Port_ID Port_invalid   = std::numeric_limits<Port_ID>::max();  // Max Port_bits allowed
constexpr int     LUT_input_bits = 4;

class Graph_library;

class Lgraph_base_core {
protected:
  class Setup_path {
  private:
    static mmap_lib::str last_path;  // Just try to optimize to avoid too many frequent syscalls

  public:
    Setup_path(const mmap_lib::str &path);
  };

  Setup_path        _p;  // Must be first in base object
  mmap_lib::str       path;
  mmap_lib::str       name;
  const mmap_lib::str unique_name;
  const mmap_lib::str long_name;
  const Lg_type_id  lgid;

  bool locked;

  Lgraph_base_core() = delete;
  explicit Lgraph_base_core(const mmap_lib::str &_path, const mmap_lib::str &_name, Lg_type_id _lgid);
  virtual ~Lgraph_base_core(){};

public:
  void get_lock();

  virtual void clear();
  virtual void sync();

  const mmap_lib::str &get_unique_name() const { return unique_name; }
  const mmap_lib::str &get_name() const { return name; }
  const mmap_lib::str &get_path() const { return path; }

  const Lg_type_id get_lgid() const { return lgid; }
};
