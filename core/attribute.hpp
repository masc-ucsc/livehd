//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

/*
Attribute_pin_dense<T>  apd;
Attribute_pin_sparse<T> asd;
Attribute_node_sparse<T> asd;

Attribute_sink_pin_sparse<T> asd;
Attribute_driver_pin_sparse<T> asd;

Attribute<I0, T>     single;
Attribute<I0, I1, T> pair;

Attribute<Index_ID, std::string_view>  node_name(Lgraph *g, std::string_view attr_name);
Attribute<Index_ID, int>               node_whatever;
Attribute<Index_ID, bool, std::string_view> pin_name;

node_name.has(Index_ID idx) const
node_name.set(Index_ID idx, T v)
const T &node_name.get(Index_ID idx) const
T &node_name.at(Index_ID idx);

pin_name.has(Index_ID idx, Y x) const
pin_name.set(Index_ID idx, Y x, T v)
const T &pin_name.get(Index_ID idx, Y x) const
T &pin_name.at(Index_ID idx, Y x);A


pin_name.sync();

*/

#include <string_view>
#include <cassert>

#include "char_array.hpp"
#include "iassert.hpp"
//#include "absl/container/flat_hash_map.h"
#include "absl/strings/substitute.h"
#include "yas/serialize.hpp"
#include "yas/std_types.hpp"

template <typename Index, typename Data> class Attribute {
protected:
  Data last_set;

public:

  // Persistent constructor (path provided)
  Attribute(std::string_view path, std::string_view name) {
    I(false);
  };

  // Ephemeral constructor (no path)
  Attribute(std::string_view name) {
    I(false);
  };

  // Check if idx is populated
  bool has(const Index &idx) const {
    return false;
  };

  // Get const data reference, assert if idx does not exist
  const Data &get(const Index &idx) const {
    return last_set;
  };

  // Get data reference, assert if does not exist
  Data &at(const Index &idx) {
    return last_set;
  };

  // Set data, overwrite if previously set
  void set(const Index &idx, const Data &data) {
    last_set = data;
  };

  // Create entry, assert if already exists
  Data &emplace(const Index &idx) {
    return last_set;
  };

};

template <typename Index> class Sttribute {
protected:
  bool clean;
  mutable bool loaded;

  const std::string path;
  const std::string name;

  const std::string idx2w_file;

#if 0
  // FUTURE?
  using Idx2w = absl::flat_hash_map<Index, Char_Array_ID>;
  using Names = absl::flat_hash_set<std::string, uint32_t>;
#else
  using Idx2w = std::unordered_map<uint64_t, uint32_t>;
  using Names = Char_Array<uint16_t>;
#endif

  mutable Names   names;
  mutable Idx2w   idx2w;

  void reload() const {
    if (loaded)
      return;
    loaded = true;

    constexpr std::size_t flags =
      yas::file // IO type
      |yas::binary; // IO format

    if (access(idx2w_file.c_str(), F_OK) != -1) {
      yas::load<flags>(idx2w_file.c_str(),
          YAS_OBJECT("idx2w", idx2w)
          );
    }
  };

public:
  Sttribute() = delete;

  Sttribute(std::string_view _path, std::string_view _name)
    : path(_path)
    , name(_name)
    , names(absl::StrCat(_path, "/lgraph_", _name, "_names_attr"))
    , idx2w_file(absl::StrCat(_path, "/lgraph_", _name, "_id2wd_attr"))
  {
    static_assert(sizeof(Index)==sizeof(uint64_t));
#ifndef NDEBUG
    struct stat sb;
    std::string sdir(path);
    I(stat(sdir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode));
#endif

    clean   = true;
    loaded  = false;
  };

  ~Sttribute() {
    sync();
  }

  // Check if idx is populated
  bool has(const Index &idx) const {
    if (!loaded)
      reload();
    return idx2w.find(idx) != idx2w.end();
  };

  // Get const data reference, assert if idx does not exist
  std::string_view get(const Index &idx) const {
    if (!loaded)
      reload();
    auto it1 = idx2w.find(idx);
    I(it1 != idx2w.end());
    I(it1->second); // zero is not valid

    auto name = names.get_name(it1->second);
    I(name != "");

    return name;
  };

  // Get data reference, assert if does not exist
  std::string_view &at(const Index &idx) {
    I(false); // No modify support for std::string_view. Use set/get
    return get(idx);
  };

  // Set data, overwrite if previously set
  void set(const Index &idx, std::string_view data) {
    if (!loaded)
      reload();
    clean = false;
    auto wid = names.get_id(data);
    if(wid==0) {
      wid = names.create_id(data);
    }

    idx2w[idx] = wid;
  };

  // Create entry, assert if already exists
  std::string_view emplace(const Index &idx) {
    I(false); // use set/get for string_view
    return "";
  };

  void clear() {
    if (!loaded && clean)
      return;
    loaded  = false;
    clean   = true;

    idx2w.clear();
    names.clear();
    unlink(idx2w_file.c_str());
  }

  void sync() {
    if (clean || !loaded)
      return;
    clean = true;

    constexpr std::size_t flags =
      yas::file // IO type
      |yas::binary; // IO format

    yas::save<flags>(idx2w_file.c_str(),
        YAS_OBJECT("idx2w", idx2w)
        );

    names.sync();
  };

};

