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
// -Meta information (attributes) are kept separate unless VERY dense and
// frequent. Currently only: (1) type info per node; (2) bits for driver pin.
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

#include <cassert>
#include <string_view>
#include <vector>

//#include "boost/container/static_vector.hpp"
#include "absl/container/inlined_vector.h"
#include "iassert.hpp"
#include "graph_sizing.hpp"

    class Graph_core;

class Index_iter {
protected:
  Graph_core *gc;

public:
  class Fast_iter {
  private:
    Graph_core *gc;
    uint32_t    id;
    // May need to add extra data here

  public:
    constexpr Fast_iter(Graph_core *_gc, uint32_t _id) : gc(_gc), id(_id) {}
    constexpr Fast_iter(const Fast_iter &it) : gc(it.gc), id(it.id) {}

    constexpr Fast_iter &operator=(const Fast_iter &it) {
      gc = it.gc;
      id = it.id;
      return *this;
    }

    Fast_iter &operator++();  // call Graph_core::xx if needed

    constexpr bool operator!=(const Fast_iter &other) const {
      assert(gc == other.gc);
      return id != other.id;
    }
    constexpr bool operator==(const Fast_iter &other) const {
      assert(gc == other.gc);
      return id == other.id;
    }

    constexpr uint32_t operator*() const { return id; }
  };

  Index_iter() = delete;
  explicit Index_iter(Graph_core *_gc) : gc(_gc) {}

  Fast_iter begin() const;                            // Find first elemnt in Graph_core
  Fast_iter end() const { return Fast_iter(gc, 0); }  // More likely 0 ID for end
};

class Graph_core {
protected:
  class Master_entry;

  class __attribute__((packed)) Overflow_entry {
  protected:

  public:
    Overflow_entry() { clear(); }
    void clear() {
      n_ledges = 0;
      n_sedges = 0;
      overflow_vertex = 1;
    }

    bool del_sedge(uint16_t other_id);
    bool add_sedge(uint16_t other_id);
    bool del_ledge(uint32_t other_id);
    bool add_ledge(uint32_t other_id);

    static inline constexpr size_t max_ledges   = 6;
    static inline constexpr size_t max_sedges   = 19;

    // BEGIN cache line
    uint8_t  overflow_vertex : 1;  // Overflow or not overflow node
    uint8_t  n_ledges:7;           // number of ledges (6 max)
    uint8_t  n_sedges;             // number of sedges (19 max)

    std::array<uint16_t, max_sedges> sedges;
    std::array<uint32_t, max_ledges> ledges;

    bool has_local_edges() const { return (n_ledges | n_sedges)!=0; }
    size_t get_num_local_edges() const { return n_ledges+n_sedges; }

    void dump(uint32_t self_id) const;
  };

  class __attribute__((packed)) Master_entry {  // AKA pin or node entry
  public:
    static inline constexpr size_t Num_sedges = 3;

    // CTRL: Byte 0
    uint8_t  overflow_vertex : 1;  // Overflow or not overflow node
    uint8_t  node_vertex : 1;      // node or just pin
    uint8_t  input : 1;            // are the edges input or output edges
    uint8_t  n_sedges : 3;         // number of input or output edges (excluding ledge0_or_prev ledge1_or_overflow bits_or_sedge0)
    uint8_t  set_link: 1;          // set link, overflow is not set
    uint8_t  overflow_link : 1;    // When set, ledge_min points to overflow
    // CTRL: Byte 1:3
    uint32_t bits_or_sedge0 : 23;  // bits or input sedge (bits only for output pin)
    uint16_t always_pos:1;         // always positive or unsigned
    // CTRL: Byte 4:5
    uint16_t pid_or_type;         // type in node, pid bits in pin
    // SEDGE: 6:11
    int16_t  sedge[Num_sedges];
    // LEDGE: Byte 12:15
    uint32_t ledge0_or_prev;       // prev pointer (only for pin)
    // LEDGE: Byte 16:19
    uint32_t ledge1_or_overflow;
    // LEDGE: Byte 20:23
    uint32_t next_pin_ptr;         // next pointer (pin)
    // void *: Byte 24:31
    union {
      emhash8::HashSet<uint32_t> *set;
      uint32_t ledge[2];
    };

    Master_entry() { clear(); }

    void clear() {
      bzero(this, sizeof(Master_entry));  // set zero everything
    }

    bool add_sedge(int16_t rel_id);
    bool add_ledge(uint32_t id);
    bool del_edge(uint32_t self_id, uint32_t other_id);

    void delete_node(uint32_t self_id, std::vector<Master_entry> &table);

    bool is_pin() const { return !node_vertex; }
    bool is_node() const { return node_vertex; }

    void set_overflow(uint32_t oid) {
      I(!overflow_link);
      overflow_link = 1;
      I(ledge1_or_overflow == 0);
      ledge1_or_overflow = oid;
    }

    Bits_t get_bits() const {
      I(!input);
      return bits_or_sedge0; }
    void   set_bits(Bits_t b) {
      I(!input);
      bits_or_sedge0 = b;
    }

    uint8_t get_type() const { I(is_node()); return pid_or_type; }
    void    set_type(uint8_t type) { I(is_node()); pid_or_type = type; }

    constexpr uint32_t get_overflow() const;  // returns the next Entry64 if overflow, zero otherwise

    uint32_t remove_free_next() {
      auto tmp       = ledge0_or_prev;
      ledge0_or_prev = 0;
      return tmp;
    }

    void insert_free_next(uint32_t ptr) {
      if (set_link)
        delete set;
      clear();
      ledge0_or_prev = ptr;
    }

    [[nodiscard]] uint32_t get_prev_ptr() const {
      I(is_pin());
      return ledge0_or_prev;
    }

    [[nodiscard]] size_t get_num_local_edges() const {
      auto total = n_sedges
        + (input        ? (bits_or_sedge0?1:0):0)
        + (node_vertex  ? (ledge0_or_prev?1:0):0)
        + (overflow_link? 0:(ledge1_or_overflow?1:0))
        + (set_link     ? 0:((ledge[0]?1:0)+(ledge[1]?1:0)));

      return total;
    }

    bool has_edges() const { return (overflow_link|n_sedges)!=0; }

    bool     has_overflow() const { return overflow_link; }
    uint32_t get_overflow_id() const {
      if (overflow_link) {
        return ledge1_or_overflow;
      }
      return 0;
    }

    void set_pid(const Port_ID pid) {
      I(is_pin());
      pid_or_type  = pid;
    }

    uint32_t get_pid() const {
      if (is_node()) {
        return 0;
      }

      return pid_or_type;
    }

    void dump(uint32_t self_id) const;
  };

  std::vector<Master_entry> table;

  uint32_t free_master_id;
  uint32_t free_overflow_id;

  uint32_t allocate_overflow();

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

public:
  Graph_core(std::string_view path, std::string_view name);

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

  void add_edge(uint32_t driver_id, uint32_t sink_id) {
    add_edge_int(driver_id, sink_id, true);
    add_edge_int(sink_id, driver_id, false);
  }

  void del_edge(uint32_t driver_id, uint32_t sink_id) {
    del_edge_int(driver_id, sink_id, true);
    del_edge_int(sink_id, driver_id, false);
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
  void del_node(uint32_t id);            // delete all the pins and edges in the node

  // Delete edges between pins
  void del_edges(uint32_t id);

  void dump(uint32_t id) const;

  size_t size_bytes() const { return sizeof(Master_entry)*table.size(); }

  static_assert(sizeof(Graph_core::Master_entry)==32);
  static_assert(sizeof(Graph_core::Overflow_entry)==64);
};
