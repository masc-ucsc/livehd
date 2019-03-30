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


Attr_sview_raw
Attr_data_raw

pin_name.sync();

*/

#include <string_view>
#include <cassert>
#include <fstream>

#include "char_array.hpp"
#include "dense.hpp"
#include "iassert.hpp"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/substitute.h"
#include "yas/serialize.hpp"
#include "yas/std_types.hpp"
#include "yas/object.hpp"

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif


template <typename Index, bool Unique> class Attr_sview_raw {
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
  using Idx2w = std::unordered_map<Index, Char_Array_ID>;
  using Names = Char_Array<uint16_t>;
#endif

  mutable Names   names;
  mutable Idx2w   idx2w;

  constexpr static std::size_t yas_flags = yas::file|yas::binary;

  void reload() const {
    if (loaded)
      return;
    loaded = true;

    if (access(idx2w_file.c_str(), F_OK) != -1) {
      yas::load<yas_flags>(idx2w_file.c_str(),
          YAS_OBJECT("idx2w", idx2w)
          );
    }
  };

public:
  Attr_sview_raw() = delete;

  Attr_sview_raw(std::string_view _path, std::string_view _name)
    : path(_path)
    , name(_name)
    , idx2w_file(absl::StrCat(_path, "/lgraph_", _name, "_id2wd_attr"))
    , names(absl::StrCat(_path, "/lgraph_", _name, "_names_attr"))
  {
    static_assert(sizeof(Index)<=sizeof(uint64_t) && sizeof(Index)>=sizeof(uint8_t));
#ifndef NDEBUG
    struct stat sb;
    std::string sdir(path);
    I(stat(sdir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode));
#endif

    clean   = true;
    loaded  = false;
  };

  ~Attr_sview_raw() {
    sync();
  }

  // Check if idx is populated
  bool has(const Index &idx) const {
    I(idx); // zero is not valid
    if (unlikely(!loaded))
      reload();
    return idx2w.find(idx) != idx2w.end();
  };

  // Get const data reference, assert if idx does not exist
  std::string_view get(const Index &idx) const {
    I(idx); // zero is not valid
    if (unlikely(!loaded))
      reload();
    auto it1 = idx2w.find(idx);
    I(it1 != idx2w.end());
    I(it1->second); // zero is not valid

    auto name = names.get_name(it1->second);
    I(name != "");

    return name;
  };

  Index find(std::string_view name) const {
    auto str_id = names.get_id(name);
    if (str_id==0)
      return 0;

    return names.get_field(str_id);
  }

  // Get data reference, assert if does not exist
  std::string_view &at(const Index &idx) {
    I(idx); // zero is not valid
    I(false); // No modify support for std::string_view. Use set/get
    return get(idx);
  };

  // Set data, overwrite if previously set (!Unique)
  void set(const Index &idx, std::string_view data) {
    I(idx); // zero is not valid
    if (unlikely(!loaded))
      reload();
    clean = false;
    auto wid = names.get_id(data);
    if (Unique) {
      if(wid==0) {
        wid = names.create_id(data, idx);
        idx2w[idx] = wid;
      }else{
        // If this fails. More likely the labels are not unique
        I(idx2w[idx] == wid);
      }
    }else{
      if(wid==0) {
        wid = names.create_id(data, idx);
      }
      idx2w[idx] = wid;
    }
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

    yas::save<yas_flags>(idx2w_file.c_str(),
        YAS_OBJECT("idx2w", idx2w)
        );

    names.sync();
  };

};

template <typename Index, typename Data> class Attr_data_raw {
protected:
  bool clean;
  mutable bool dense_mode;
  mutable bool loaded;

  const std::string path;
  const std::string name;

  const std::string sparse_file;
  const std::string dense_file;

  using Sparse = absl::flat_hash_map<Index, Data>;
  using Dense_holes = std::unordered_set<Index>;

  mutable Sparse      sparse;
  mutable Dense_holes dense_holes;
  Dense<Data>         dense;

  mutable Index idx_max; // used to switch dense/sparse

  constexpr static std::size_t yas_flags = yas::file|yas::binary;

  void reload() const {
    if (loaded)
      return;
    loaded = true;

    std::ifstream dense_stream;
    dense_stream.open(dense_file + "_max");
    if (dense_stream.is_open()) {
      dense_stream >> idx_max;
      dense_stream.close();
      dense_mode = true;
    }else{
      // preserve dense_mode
      idx_max = 0;
    }

    if (!dense.empty()) {
      I(dense_mode);

      I(dense.size() > idx_max);
      if(access((dense_file + "_holes").c_str(), F_OK) != -1) {
        yas::load<yas_flags>((dense_file + "_holes").c_str(),
            YAS_OBJECT("dense_holes", dense_holes)
            );
      }
    }else if (!dense_mode) {
      int fd = open(sparse_file.c_str(), O_RDONLY);
      if (fd>=0) {
        struct stat sb;
        fstat(fd, &sb);
        uint8_t *memblock = reinterpret_cast<uint8_t *>(mmap(0, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0));
        if (memblock == nullptr) {
          fprintf(stderr, "error, mmap failed\n");
          exit(-3);
        }
        int n_elems = sb.st_size/(sizeof(Data)+sizeof(Index));
        sparse.reserve(n_elems+1024);
        uint8_t *pos = memblock;
        for(int i=0;i<n_elems;i++) {

          Index *idx=(Index *)pos;
          pos+=sizeof(Index);

          Data *data=(Data *)pos;
          pos+=sizeof(Data);

          sparse[*idx]=*data; // TODO: use the range insert for faster speed
        }

        munmap(memblock, sb.st_size);
        close(fd);
      }
    }

    if (dense_mode) {
      I(access(sparse_file.c_str(), F_OK) == -1);
      I(sparse.empty()); // if dense, no sparse saved
      GI(idx_max,(idx_max+1)==dense.size()); // invariant
    }else{
      I(access((dense_file + "_max").c_str(), F_OK) == -1);
      I(dense.empty());
    }
  };

  void clear_sparse() {
    I(loaded);
    I(!sparse.empty());

    sparse.clear();
    unlink(sparse_file.c_str());
  }

  void clear_dense() {
    I(loaded);
    I(!dense.empty());

    dense.clear();
    unlink((dense_file + "_size").c_str());
    unlink((dense_file + "_holes").c_str());
  }

  void switch_dense() {
    I(!dense_mode);

    dense.resize(idx_max+1);
    for(const auto it:sparse) {
      dense[it.first] = std::move(it.second);
    }
    for(Index i=1;i<idx_max;++i) {
      if (sparse.find(i) == sparse.end())
        dense_holes.insert(i);
    }

    clear_sparse();
    dense_mode = true;
  }

  void switch_sparse() {
    I(dense_mode);

    for(Index i=1;i<idx_max;++i) {
      if (dense_holes.find(i) == dense_holes.end())
        sparse[i] = std::move(dense[i]);
    }

    clear_dense();
    dense_mode = false;
  }

public:
  Attr_data_raw() = delete;

  Attr_data_raw(std::string_view _path, std::string_view _name)
    : path(_path)
    , name(_name)
    , sparse_file(absl::StrCat(_path, "/lgraph_", _name, "_sparse_attr"))
    , dense_file (absl::StrCat(_path, "/lgraph_", _name, "_dense_attr"))
    , dense(dense_file)
  {
    static_assert(sizeof(Index)<=sizeof(uint64_t) && sizeof(Index)>=sizeof(uint8_t));

#ifndef NDEBUG
    struct stat sb;
    std::string sdir(path);
    I(stat(sdir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode));
#endif

    clean      = true;
    loaded     = false;
    dense_mode = false; // It unknown until loaded, Starts in !dense_mode if empty

    std::ifstream dense_stream;
    dense_stream.open(dense_file + "_size");
    if (dense_stream.is_open()) {
      uint64_t dense_size;
      dense_stream >> dense_size;
      dense_stream.close();

      dense.reload(dense_size);
      dense_mode = true;
    }

  };

  ~Attr_data_raw() {
    sync();
  }

  // Check if idx is populated
  bool has(const Index &idx) const {
    I(idx); // zero is not valid
    if (unlikely(!loaded))
      reload();

    if (!dense_mode)
      return sparse.find(idx) != sparse.end();

    return (idx<=idx_max) && dense_holes.find(idx) == dense_holes.end();
  };

  // Get const data reference, assert if idx does not exist
  const Data &get(const Index &idx) const {
    I(idx); // zero is not valid
    if (unlikely(!loaded))
      reload();

    I(has(idx));

    if (dense_mode)
      return dense[idx];

    return sparse[idx];
  };

  // Get data reference, assert if does not exist
  Data &at(const Index &idx) {
    I(idx); // zero is not valid
    if (unlikely(!loaded))
      reload();

    I(has(idx));

    if (dense_mode)
      return dense[idx];

    return sparse[idx];
  };

  // Set data, overwrite if previously set
  void set(const Index &idx, const Data &data) {
    I(idx); // zero is not valid
    if (unlikely(!loaded))
      reload();
    clean = false;

    if (!dense_mode) {
      sparse[idx] = data;
      if (idx>idx_max) {
        idx_max = idx;
      }
      static int conta=10000; // From time to time
      if (unlikely(conta--<0)) {
        conta = 10000;
        if (idx_max >10000 && (sparse.size()/(double)(idx_max)) > 0.3) {
          switch_dense();
        }
      }
      return;
    }

    GI(idx_max,(idx_max+1)==dense.size()); // invariant

    if (likely(idx<dense.size())) {
      dense[idx] = data;
      dense_holes.erase(idx);
      return;
    }
    if (likely(idx==dense.size())) {
      dense.emplace_back(data);
      idx_max=idx;
      I((idx_max+1)==dense.size()); // invariant
      return;
    }
    if (idx_max>10000 && (idx/((double)idx_max) > 1.4) && (idx_max/(double)(idx)) < 0.2) {
      switch_sparse();
      I(!dense_mode);
      sparse[idx] = data;
      idx_max = idx;
      return;
    }

    I(idx_max<idx);
    for(Index i=idx_max+1;i<idx;++i) {
      dense_holes.insert(i);
    }
    idx_max = idx;
    dense.resize(idx_max);
    dense.emplace_back(data);

    I((idx_max+1)==dense.size()); // invariant
  };

#if 0
  // Create entry, assert if already exists
  Data &emplace(const Index &idx) {
    if (unlikely(!loaded))
      reload();
    clean = false;

    I(!has(idx));

    if (!dense_mode) {
      sparse[idx] = 0; // FIXME: emplace?? data;
      return sparse[idx];
    }

    HERE
  };
#endif

  void clear() {
    if (!loaded && clean)
      return;
    if (idx_max==0) { // already cleared
      loaded  = false;
      clean   = true;
      return;
    }

    idx_max = 0;

    unlink((dense_file + "_max").c_str());

    if (!dense_mode) {
      I(access((dense_file + "_size").c_str(), F_OK) == -1);
      if (!sparse.empty())
        clear_sparse();
    }else{
      I(access(sparse_file.c_str(), F_OK) == -1);
      clear_dense();

      std::ofstream dense_stream;

      dense_stream.open(dense_file + "_size");
      I(dense_stream.is_open());
      // Save size 0, to remember that it is better dense mode
      uint64_t dense_size = 0;
      dense_stream << dense_size;
      dense_stream.close();
    }

    loaded  = false;
    clean   = true;
  };

  void sync() {
    if (clean || !loaded)
      return;
    clean = true;

    I(idx_max); // it should be clean if idx_max==0
    std::ofstream dense_stream;
    dense_stream.open(dense_file + "_max");
    I(dense_stream.is_open());
    dense_stream << idx_max;
    dense_stream.close();

    if (sparse.size() && !dense_mode) {

      size_t mmap_size = sparse.size() * (sizeof(Index)+sizeof(Data));

      int fd = open(sparse_file.c_str(), O_RDWR | O_CREAT, 0644);
      if (fd<0) {
        fprintf(stderr, "error, open %s failed\n", sparse_file.c_str());
        exit(-3);
      }
      int ret = ftruncate(fd, mmap_size);
      if (ret<0) {
        fprintf(stderr, "error, ftruncate failed failed\n");
        exit(-3);
      }
      uint8_t *memblock = reinterpret_cast<uint8_t *>(mmap(0, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));
      if (memblock == nullptr) {
        fprintf(stderr, "error, mmap failed\n");
        exit(-3);
      }

      uint8_t *pos = memblock;
      for(const auto &it:sparse) {

        Index *idx=(Index *)pos;
        *idx = it.first;
        pos+=sizeof(Index);

        Data *data=(Data *)pos;
        *data = it.second;
        pos+=sizeof(Data);
      }

      munmap(memblock, mmap_size);
      close(fd);

      I(dense.empty());
      I(access((dense_file + "_size").c_str(), F_OK) == -1);

      return;
    }

    unlink((dense_file + "_holes").c_str());
    yas::save<yas_flags>((dense_file + "_holes").c_str(),
        YAS_OBJECT("dense_holes", dense_holes)
        );

    I(access(sparse_file.c_str(), F_OK) == -1);

    dense_stream.open(dense_file + "_size");
    I(dense_stream.is_open());
    // Save size 0, to remember that it is better dense mode
    uint64_t dense_size = dense.size();
    dense_stream << dense_size;
    dense_stream.close();

    dense.sync();
  };

  bool is_dense() const { return dense_mode; };
};

