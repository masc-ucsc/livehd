//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>
#include <string>
#include <cstdint>

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

#include "fmt/format.h"

#include "explicit_type.hpp"
#include "iassert.hpp"
#include "mmap_tree.hpp"

// LGraph basic core types used all over
using Lg_type_id   = Explicit_type<uint32_t, struct Lg_type_id_struct, 0>;  // Global used all over
using Index_ID     = Explicit_type<uint32_t, struct Index_ID_struct, 0>;
using Lut_type_id  = Explicit_type<uint32_t, struct Lut_type_id_struct, 0>;

class Hierarchy_data { // 64bits total
public:
  Lg_type_id lgid;
  Index_ID   up_nid;
  Hierarchy_data() : lgid(0), up_nid(0) {}
  Hierarchy_data(const Lg_type_id &_class_id, const Index_ID &_nid)
   :lgid(_class_id)
   ,up_nid(_nid) {
   }

  bool is_invalid() const { return lgid == 0; }
};

using Hierarchy_index=mmap_lib::Tree_index;


struct Lg_type_id_hash {
  size_t operator()(const Lg_type_id& obj) const { return obj.value; }
};

struct Index_ID_hash {
  size_t operator()(const Index_ID& obj) const { return obj.value; }
};

using Port_ID    = uint32_t;    // ports have a set order (a-b != b-a)

constexpr int Index_bits = 31; // 31 bit to have Sink/Driver + Index in 32 bits
constexpr int Port_bits  = 28;
constexpr int Bits_bits  = 17;
constexpr Port_ID Port_invalid = ((1ULL<<Port_bits)-1); // Max Port_bits allowed
constexpr int LUT_input_bits  = 4;

class Graph_library;

class Lgraph_base_core {
protected:
  class Setup_path {
  private:
    static std::string last_path;  // Just try to optimize to avoid too many frequent syscalls

  public:
    Setup_path(std::string_view path);
  };

  Setup_path  p;  // Must be first in base object
  std::string path;
  std::string name;
  std::string long_name;
  Lg_type_id  lgid;

  bool locked;

  Lgraph_base_core() = delete;
  explicit Lgraph_base_core(std::string_view _path, std::string_view _name, Lg_type_id lgid);
  virtual ~Lgraph_base_core(){};

public:
  void get_lock();

  virtual void clear();
  virtual void sync();

  std::string_view get_name() const { return std::string_view(name); }

  const Lg_type_id get_lgid() const { return lgid; }

  std::string_view     get_path() const { return path; }
};
