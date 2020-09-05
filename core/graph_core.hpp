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
protected:
  class __attribute__((packed)) Entry48 {
    uint8_t  edge_storage[48-1];
    uint8_t  last_byte;
  protected:
    constexpr Entry48() : edge_storage{0,},last_byte(0x80) {
    }
    void set_input()  { last_byte |= 0xC0; } // set 6thbit (and 7th is always set)
    void set_output() { last_byte &= 0xBF; } // clear 6th bit

    constexpr Index_ID get_overflow() const; // returns the next Entry48 if overflow, zero otherwise

    constexpr bool is_overflow() const { return (last_byte & 0x80)==0x80; }

    void fill_inp(std::vector<Index_ID> &ev) const; // fill the list of edges to ev (requires expand)
    void fill_out(std::vector<Index_ID> &ev) const; // fill the list of edges to ev (requires expand)
    bool try_add_driver(Index_ID id); // return false if there was no space
    bool try_add_sink(Index_ID id); // return false if there was no space

  };
  class __attribute__((packed)) Entry12 {
    uint8_t  edge_storage[12-4];
    uint8_t  edge_storate_or_pid_bits; // edge_store in master_root, pid in master
    uint8_t  ptrs_next; // master_next (only in master_root, otherwise pid bits) and overflow_next
    uint8_t  inp_mask;  // 6bits inp_mast, 2 lower bits is driver_set, sink_set
    uint8_t  last_byte;
  protected:
    constexpr Entry12() : edge_storage{0,}, edge_storate_or_pid_bits(0), ptrs_next(0xFF), inp_mask(0), last_byte(0) {
    }

    void set_master_root();
    void set_master();

    constexpr Index_ID get_overflow() const; // returns the next Entry48 if overflow, zero otherwise
    constexpr Index_ID get_next() const;     // returns the next Entry12 that is master, zero if none

    constexpr bool is_driver_set()  const { return inp_mask & 0x1; }
    constexpr bool is_sink_set()    const { return inp_mask & 0x2; }

    constexpr bool is_overflow()    const { return (last_byte & 0x80) == 0x80; }
    constexpr bool is_master_root() const { return (last_byte & 0xC0) == 0x40; }
    constexpr bool is_master()      const { return (last_byte & 0xC0) == 0x00; }

    constexpr uint8_t  get_type()   const { assert(is_master_root()); return last_byte & 0x3F; }
    constexpr uint32_t get_pid()    const {
      if(is_master_root())
        return 0;

      auto pid = (static_cast<uint32_t>(edge_storate_or_pid_bits)<<10)
               | (static_cast<uint32_t>(ptrs_next&0xF0)<<2)
               | (last_byte & 0x3F);

      return pid;
    }

    constexpr Index_ID get_master_root() const; // ptr to master root (zero if itself is root)

    void fill_inp(std::vector<Index_ID> &ev) const; // fill the list of edges to ev (requires expand)
    void fill_out(std::vector<Index_ID> &ev) const; // fill the list of edges to ev (requires expand)
    bool try_add_driver(Index_ID id); // return false if there was no space
    bool try_add_sink(Index_ID id); // return false if there was no space

  };

  std::vector<Entry48> table; // to be replaced by mmap_lib::vector once it works

  Index_ID next12_free;       // Pointer to 12byte free chunks
  Index_ID next48_free;       // Pointer to 48byte free chunks

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

  uint8_t get_type(const Index_ID master_root_id) const;  // set/get type on the master_root id (s or pointed by s)
  void    set_type(const Index_ID master_root_id);

  Port_ID get_pid(const Index_ID master_root_id) const; // pid for master or 0 for master_root

  // Create a master root node
  Index_ID create_master_root(uint8_t type);
  // Create a master and point to master root m
  Index_ID create_master(const Index_ID master_root_id, const Port_ID pid);
  // Delete node s, all related edges and masters (if master root)
  void del(const Index_ID s);
};


