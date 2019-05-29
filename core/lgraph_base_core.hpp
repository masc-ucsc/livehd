//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>
#include <string>
#include <cstdint>

#include "fmt/format.h"

#include "char_array.hpp"
#include "dense.hpp"
#include "explicit_type.hpp"
#include "iassert.hpp"

//#include "graph_library.hpp"
//#include "lgedge.hpp"
//#include "tech_library.hpp"

// LGraph basic core types used all over
using Lg_type_id   = Explicit_type<uint32_t, struct Lg_type_id_struct>;  // Global used all over
using Index_ID     = Explicit_type<uint64_t, struct Index_ID_struct>;
using Lut_type_id  = Explicit_type<uint32_t, struct Lut_type_id_struct>;
using Hierarchy_id = uint16_t;

struct Lg_type_id_hash {
  size_t operator()(const Lg_type_id& obj) const { return obj.value; }
};

struct Index_ID_hash {
  size_t operator()(const Index_ID& obj) const { return obj.value; }
};

using Port_ID    = uint32_t;    // ports have a set order (a-b != b-a)

constexpr int Index_bits = 31; // 31 bit to have Sink/Driver + Index in 32 bits
constexpr int Port_bits  = 30;
constexpr Port_ID Port_invalid = ((1ULL<<Port_bits)+1); // Anything over 1<<30
constexpr int LUT_input_bits  = 4;

class Graph_library;
class Tech_library;

class Lgraph_base_core {
protected:
  class Setup_path {
  private:
    static std::string last_path;  // Just try to optimize to avoid too many frequent syscalls

  public:
    Setup_path(std::string_view path);
  };
  Setup_path        p;  // Must be first in base object
  const std::string path;
  const std::string name;
  const std::string long_name;
  const Lg_type_id  lgraph_id;

  bool locked;

  // Integrate graph and tech library?
  Graph_library *library;
  Tech_library * tlibrary;

  Lgraph_base_core() = delete;
  explicit Lgraph_base_core(std::string_view _path, std::string_view _name, Lg_type_id lgid);
  virtual ~Lgraph_base_core(){};


public:
  void get_lock();

  virtual bool close();
  virtual void clear();
  virtual void sync();

  std::string_view get_name() const { return std::string_view(name); }

  const Lg_type_id get_lgid() const { return lgraph_id; }
  const Lg_type_id lg_id()    const { return lgraph_id; } // WARNING: deprecated: use get_lgid()

  const std::string   &get_path() const { return path; }
  const Graph_library &get_library() const { return *library; }
  const Tech_library  &get_tlibrary() const { return *tlibrary; }
  Tech_library &       get_tech_library() { return *tlibrary; }
};
