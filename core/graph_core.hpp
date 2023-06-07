//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "hash_set8.hpp"

// This is a graph representation optimized based on structure and use in
// LiveHD.
//
// Some key characteristics:
//
// -Graph mutates (add/remove edges and nodes)
//
// -At most 32 bit ID for node (graph would be partitioned if larger is needed)
//
// -Once an ID is created, even if node is deleted, it can not be reused. This
// sometimes called a "tombstone deletion"
//
// -Delete node is frequent (code optimization)
//
// -2 main types of traversal (any order or topological sort)
//
// -Add edges is not so frequent after the 1st phase of graph creation
//
// -Nodes has several "pins" and the edges are bi-directional
//
// -In practice (topological sort), most edges are "short" no need to keep large
// 32 bit integer. Delta optimization to fit more edges.
//
// -Meta information (attributes) are kept separate with the exception of type
//
// -Most nodes have under 8 edges, but some (reset/clk) have LOTS of edges
//
// -The graph operations do NOT need to be multithreaded with updates. The
// parallelism is extracted from parallel thread operations. (parallel read-only
// may happen).

// Some related (but different) graph representations:
//
// Pandey, Prashant, et al. "Terrace: A hierarchical graph container for skewed
// dynamic graphs." Proceedings of the 2021 International Conference on
// Management of Data. 2021.
//
// Winter, Martin, et al. "faimGraph: high performance management of
// fully-dynamic graphs under tight memory constraints on the GPU." SC18:
// International Conference for High Performance Computing, Networking, Storage
// and Analysis. IEEE, 2018.
//
// Bader, David A., et al. "Stinger: Spatio-temporal interaction networks and
// graphs (sting) extensible representation." Georgia Institute of Technology,
// Tech. Rep (2009).
//
// Feng, Guanyu, et al. "Risgraph: A real-time streaming system for evolving
// graphs to support sub-millisecond per-update analysis at millions ops/s."
// Proceedings of the 2021 International Conference on Management of Data. 2021.

// Overall data structure:
//
// Vector with 4 types of nodes: Free, Node, Pin, Overflow
//
// Node/Pin are 16 byte, Overflow 32 byte
//
// Node is the master node and also the output pin
//
// Pin is either of the other node pins (input 0, output 1....)
//
// Node/Pin have a very small amount of edges. If more are needed, the overflow
// is used. If more are needed an index to emhash8::HashSet is used (overflow is
// deleted and moved to the set)
//
// The overflow are just a continuous (not sorted) array. Scanning is needed if
// overflow is used.

#include <array>
#include <cassert>
#include <string_view>
#include <vector>

#include "graph_core_node.hpp"
#include "graph_core_overflow.hpp"
#include "graph_core_pin.hpp"
#include "graph_sizing.hpp"
#include "iassert.hpp"

class Graph_core {
public:
  Graph_core(std::string_view name);

  uint8_t get_type(uint32_t id) const {
    I(id && id < table.size());
    I(table[id].is_node());
    return table[id].get_type();
  }

  void set_type(uint32_t id, uint8_t type) {
    I(id && id < table.size());
    I(table[id].is_node());
    table[id].set_type(type);
  }

  Port_ID get_pid(uint32_t id) const {
    I(!is_invalid(id));
    return table[id].get_pid();
  }

  void set_bits(uint32_t id, Bits_t bits) {
    I(id && id < table.size());
    table[id].set_bits(bits);
  }
  Bits_t get_bits(uint32_t id) {
    I(id && id < table.size());
    return table[id].get_bits();
  }

  bool is_invalid(uint32_t id) const {
    if (id == 0 || table.size() <= id) {
      return true;
    }

    return table[id].overflow_vertex;  // overflow set in deleted nodes
  }

  bool is_node(uint32_t id) const {
    I(id && id < table.size());
    return table[id].is_node();
  }
  bool is_pin(uint32_t id) const {
    I(id && id < table.size());
    return table[id].is_pin();
  }

  uint32_t create_node();
  uint32_t create_pin(uint32_t node_id, const Port_ID pid);

  uint32_t get_node(uint32_t id) {
    I(!is_invalid(id));

    if (table[id].is_node()) {
      return id;
    }

    return table[id].get_prev_ptr();
  }

  bool has_edges(uint32_t id) const {
    I(!is_invalid(id));
    return table[id].has_edges();
  }

  std::pair<size_t, size_t> get_num_pin_edges(uint32_t id) const;

  size_t get_num_pin_outputs(uint32_t id) const { return get_num_pin_edges(id).second; }
  size_t get_num_pin_inputs(uint32_t id) const { return get_num_pin_edges(id).first; }

  // A common operation while optimizing a graph is to move all the edges from a
  // node_pin to another node_pin.  Both the current_pin and new_pin should be
  // the same type (either both sink or both drivers).
  void move_edges(uint32_t current_pin, uint32_t new_pin);

  void add_edge(uint32_t driver_id, uint32_t sink_id) {
    I(table[sink_id].is_pin());

    add_edge_int(driver_id, sink_id);
    add_edge_int(sink_id, driver_id);
  }

  void del_edge(uint32_t driver_id, uint32_t sink_id) {
    I(table[sink_id].is_pin());

    del_edge_int(driver_id, sink_id);
    del_edge_int(sink_id, driver_id);
  }

  // Make sure that this methods have "c++ copy elision" (strict rules in return)
  const absl::InlinedVector<uint32_t, 40> get_setup_drivers(uint32_t node_id) const;  // the drivers set for node_id
  const absl::InlinedVector<uint32_t, 40> get_setup_sinks(uint32_t node_id) const;    // the sinks set for node_id

  // unlike the const iterator, it should allow to delete edges/nodes while
  [[nodiscard]] uint32_t fast_next(uint32_t start) const;

  Index_iter node_out_ids(uint32_t id);  // Iterate over the out edges of s (*it is uint32_t)
  Index_iter node_inp_ids(uint32_t id);  // Iterate over the inp edges of s

  // Delete edges and pin itself (if pin is node, then it is kept as empty because pins need a node)
  void del_pin(uint32_t id);
  void del_node(uint32_t id);  // delete all the pins and edges in the node

  // Delete edges between pins
  void del_edges(uint32_t id);

  void dump(uint32_t id) const;

  size_t size_bytes() const { return sizeof(Master_entry) * table.size(); }

  static_assert(sizeof(Graph_core::Master_entry) == 32);
  static_assert(sizeof(Graph_core::Overflow_entry) == 64);

protected:
  class __attribute__((packed)) Graph_core_free {
  public:
    uint8_t  data[12];
    uint32_t next_ptr;

    Graph_core_free() { bzero(this, sizeof(Graph_core_free)); }

    [[nodiscard]] Graph_core_node *ref_node() {
      next_ptr              = 0;
      Graph_core_node *node = (Graph_core_node *)(data);
      node->clear();
      return node;
    }
    [[nodiscard]] Graph_core_pin *ref_pin() {
      next_ptr            = 0;
      Graph_core_pin *pin = (Graph_core_pin *)(data);
      pin->clear();
      return pin;
    }

    [[nodiscard]] bool is_node() const { return static_cast<Entry_type>(data[0]) == Entry_type::Node; }
    [[nodiscard]] bool is_pin() const { return static_cast<Entry_type>(data[0]) == Entry_type::Pin; }
    [[nodiscard]] bool is_overflow() const { return static_cast<Entry_type>(data[0]) == Entry_type::Overflow; }
    [[nodiscard]] bool is_free() const { return static_cast<Entry_type>(data[0]) == Entry_type::Free; }
  };

  class __attribute__((packed)) Graph_core_free_overflow {
  public:
    uint8_t  data[12 + 16];
    uint32_t next_ptr;

    Graph_core_free_overflow() { bzero(this, sizeof(Graph_core_free_overflow)); }

    std::pair<Graph_core_node *, Graph_core_free *> ref_node() {
      next_ptr              = 0;
      Graph_core_node *node = (Graph_core_node *)(data);
      node->clear();

      Graph_core_free *f = (Graph_core_free *)&data[16];

      return std::pair(node, f);
    }

    std::pair<Graph_core_pin *, Graph_core_free *> ref_pin() {
      next_ptr            = 0;
      Graph_core_pin *pin = (Graph_core_pin *)(data);
      pin->clear();

      Graph_core_free *f = (Graph_core_free *)&data[16];

      return std::pair(pin, f);
    }

    Graph_core_overflow *ref_overflow() {
      next_ptr                = 0;
      Graph_core_overflow *ov = (Graph_core_overflow *)(data);
      ov->clear();
      return ov;
    }
  };

  std::vector<Graph_core_free> table;
  const std::string            name;

  uint32_t free_master_id;
  uint32_t free_overflow_id;

  std::pair<Graph_core_overflow *, uint32_t> allocate_overflow();

  void add_edge_int(uint32_t self_id, uint32_t other_id, bool out);
  void del_edge_int(uint32_t self_id, uint32_t other_id, bool out);

  Overflow_entry *ref_overflow(uint32_t id) {
#if 0
    I((id + 1) < table.size());  // overflow uses 2 entries in table
    I(table[id].overflow_vertex);
#endif

    return (Overflow_entry *)&table[id];
  }
  const Overflow_entry *ref_overflow(uint32_t id) const {
    I((id + 1) < table.size());  // overflow uses 2 entries in table
    I(table[id].overflow_vertex);

    return (const Overflow_entry *)&table[id];
  }
};

//----
// Graph_core_iterator

#include <cstdint>
#include <iterator>
#include <map>
#include <vector>

class Graph_core_iterator {
public:
  using MapType = std::map<uint32_t, uint32_t>;

  void push_back(const uint32_t &value) { vec.push_back(value); }

  void insert(uint32_t key, const uint32_t &value) { mp.insert({key, value}); }

  // Iterator class definition
  class iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = uint32_t;
    using pointer           = uint32_t *;
    using reference         = uint32_t &;

    iterator(typename std::vector<uint32_t>::iterator vec_it, typename std::vector<uint32_t>::iterator vec_end,
             typename MapType::iterator map_it)
        : vec_it(vec_it), vec_end(vec_end), map_it(map_it) {}

    reference operator*() const { return vec_it != vec_end ? *vec_it : map_it->second; }

    pointer operator->() const { return vec_it != vec_end ? &(*vec_it) : &(map_it->second); }

    iterator &operator++() {
      if (vec_it != vec_end) {
        ++vec_it;
      } else {
        ++map_it;
      }
      return *this;
    }

    const iterator operator++(int) {
      iterator tmp = *this;
      operator++();
      return tmp;
    }

    bool operator==(const iterator &rhs) const { return vec_it == rhs.vec_it && map_it == rhs.map_it; }

    bool operator!=(const iterator &rhs) const { return !(*this == rhs); }

  private:
    typename std::vector<uint32_t>::iterator vec_it;
    typename std::vector<uint32_t>::iterator vec_end;
    typename MapType::iterator               map_it;
  };

  // Const iterator class definition
  class const_iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = uint32_t;
    using pointer           = const uint32_t *;
    using reference         = const uint32_t &;

    const_iterator(typename std::vector<uint32_t>::const_iterator vec_it, typename std::vector<uint32_t>::const_iterator vec_end,
                   typename MapType::const_iterator map_it)
        : vec_it(vec_it), vec_end(vec_end), map_it(map_it) {}

    reference operator*() const { return vec_it != vec_end ? *vec_it : map_it->second; }

    pointer operator->() const { return vec_it != vec_end ? &(*vec_it) : &(map_it->second); }

    const_iterator &operator++() {
      if (vec_it != vec_end) {
        ++vec_it;
      } else {
        ++map_it;
      }
      return *this;
    }

    const const_iterator operator++(int) {
      const_iterator tmp = *this;
      operator++();
      return tmp;
    }

    bool operator==(const const_iterator &rhs) const { return vec_it == rhs.vec_it && map_it == rhs.map_it; }

    bool operator!=(const const_iterator &rhs) const { return !(*this == rhs); }

  private:
    typename std::vector<uint32_t>::const_iterator vec_it;
    typename std::vector<uint32_t>::const_iterator vec_end;
    typename MapType::const_iterator

        map_it;
  };

  iterator begin() { return iterator(vec.begin(), vec.end(), mp.begin()); }

  iterator end() { return iterator(vec.end(), vec.end(), mp.end()); }

  const_iterator begin() const { return const_iterator(vec.begin(), vec.end(), mp.begin()); }

  const_iterator end() const { return const_iterator(vec.end(), vec.end(), mp.end()); }

  const_iterator cbegin() const { return const_iterator(vec.begin(), vec.end(), mp.begin()); }

  const_iterator cend() const { return const_iterator(vec.end(), vec.end(), mp.end()); }
};
