//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgraph_base_core.hpp"

#include <cassert>
#include <vector>
#include <string_view>

class Graph_core;

class Index_iter {
protected:
  Graph_core     *gc;

public:
  class Fast_iter {
  private:
    Graph_core     *gc;
    Index_ID        id;
    // May need to add extra data here

  public:
    constexpr Fast_iter(Graph_core *_gc, const Index_ID _id ) : gc(_gc), id(_id)   {}
    constexpr Fast_iter(const Fast_iter &it) : gc(it.gc), id(it.id) {}

    constexpr Fast_iter &operator=(const Fast_iter &it) {
      gc = it.gc;
      id = it.id;
      return *this;
    }

    Fast_iter &operator++(); // call Graph_core::xx if needed

    constexpr bool operator!=(const Fast_iter &other) const { assert(gc==other.gc); return id != other.id; }
    constexpr bool operator==(const Fast_iter &other) const { assert(gc==other.gc); return id == other.id; }

    constexpr Index_ID operator*() const { return id; }
  };

  Index_iter() = delete;
  explicit Index_iter(Graph_core *_gc) : gc(_gc) {}

  Fast_iter begin() const; // Find first elemnt in Graph_core
  Fast_iter end() const { return Fast_iter(gc, 0); } // More likely 0 ID for end
};

class Graph_core {
public:
  class __attribute__((packed)) Entry64 { // this ensure the correctness of the graph, i.e. we can store
  // edges even if nodes are farther than 255 apart
  public:
    Index_ID driver;
    Index_ID sink;
    Entry64(Index_ID d, Index_ID s) : driver(d), sink(s) {
    }
  };

  class __attribute__((packed)) Entry16 { // AKA master or master_root entry
  public:
    uint8_t  edge_storage[16-5];
    uint16_t edge_storage_or_pid_bits; // edge_store in master_root, 16 pid bits in master
    uint8_t  pid_bits_or_type:5;       // type in master_root, 6 pid bits in master
    uint8_t  driver_set:1;
    uint8_t  sink_set:1;               // different from inp_mask!=0 because bidirectional edges
    uint8_t  ptrs;                     // master_next in master_root (master_prev in master) and overflow_next
    uint8_t  inp_mask:7;               // 7bits inp_mask (8 or 0b111 means not used)
    uint8_t  master_root:1;            // for speed good to remember root vs master (pid==0?)
    uint8_t writable:1;
    uint8_t overflow:1;
    constexpr Entry16() : edge_storage{0,}, edge_storage_or_pid_bits(0), pid_bits_or_type(0), driver_set(0), sink_set(0), ptrs(0xFF), inp_mask(0), master_root(0), writable(1), overflow(0) {
    }

    void set_master_root() { master_root = 1; writable = 0;}
    void set_master();

    constexpr uint8_t get_overflow() const { return overflow; } // returns the next Entry64 if overflow, zero otherwise
    void set_overflow(uint8_t o) { overflow = o; }
    constexpr Index_ID get_next() const;     // returns the next Entry16 that is master, zero if none

    constexpr bool is_driver_set()  const { return driver_set; }
    constexpr bool is_sink_set()    const { return sink_set; }
    constexpr bool is_master_root() const { return master_root; }

    constexpr uint8_t  get_type()   const { return pid_bits_or_type; }
    constexpr void set_type(uint8_t type ) { pid_bits_or_type = type; }
    constexpr uint32_t get_pid()    const {
      if(is_master_root())
        return 0;

      uint32_t pid = pid_bits_or_type;
      pid <<=16;
      pid  |= edge_storage_or_pid_bits;

      return pid; //22 bits PID
    }

    constexpr Index_ID get_master_root() const { // ptr to master root (zero if itself is root)
      if (is_master_root())
        return 0;
      else
        return 0;
    }

    bool is_writable() { return writable; }
    bool set_writable() { writable = 1; }

    void fill_inp(std::vector<Index_ID> &ev) const; // fill the list of edges to ev (requires expand)
    void fill_out(std::vector<Index_ID> &ev) const; // fill the list of edges to ev (requires expand)
    bool try_add_driver(Index_ID id);
    bool try_add_sink(Index_ID id);
    size_t available_index() {
      return index_of_val(0);
    }

    size_t index_of_val(uint8_t val) {
      size_t i = 0;
      while (i < std::size(edge_storage) && edge_storage[i] != val) {
        i++;
      }

      return i;
    }
  };

public:
  std::vector<Entry16> table;
  std::vector<Entry64> overflow; // to be replaced by mmap_lib::vector once it works

  Index_ID table_next_free;
  Index_ID overflow_next_free;
  /*
  Index_ID next12_free;       // Pointer to 12byte free chunks
  Index_ID next48_free;       // Pointer to 48byte free chunks
  */

public:
  Graph_core(std::string_view path, std::string_view name);
  void add_edge(const Index_ID sink_id, const Index_ID driver_id); // Add edge from s->d and d->s
  void del_edge(const Index_ID sink_id, const Index_ID driver_id); // Remove both s->d and d->s

  // Make sure that this methods have "c++ copy elision" (strict rules in return)
  const std::vector<Index_ID> get_setup_drivers(const Index_ID master_root_id) const;  // the drivers set for master_root_id
  const std::vector<Index_ID> get_setup_sinks(const Index_ID master_root_id) const;    // the sinks set for master_root_id

  // unlike the const iterator, it should allow to delete edges/nodes while
  //   // traversing
  Index_ID fast_next(Index_ID start); // faster iterator returning all the master_root Index_ID (0 if last)

  // Unlike get_setup_drivers, this returns all the drivers/sinks that reach
  // the s index. This can be a large list, so it is not a short vector but an
  // iterator.
  Index_iter out_ids(const Index_ID s);  // Iterate over the out edges of s (*it is Index_ID)
  Index_iter inp_ids(const Index_ID s);  // Iterate over the inp edges of s

  std::vector<Index_ID> get_edges(Index_ID s);

  uint8_t get_type(const Index_ID master_root_id) const { return table.at(master_root_id).get_type(); }  // set/get type on the master_root id (s or pointed by s)
  void    set_type(const Index_ID master_root_id, uint8_t type) { table.at(master_root_id).set_type(type); };



  Port_ID get_pid(const Index_ID master_root_id) const; // pid for master or 0 for master_root

  // Create a master root node
  Index_ID create_master_root(uint8_t type);
  // Create a master and point to master root m
  Index_ID create_master(const Index_ID master_root_id, const Port_ID pid);
  // Delete node s, all related edges and masters (if master root)
  void del(const Index_ID s);

  Index_ID get_next_free() {
    Index_ID i = 0;
    while (i < table.size() && !table.at(i).is_writable())
      i = i + 1;

    return i;
  }
};


