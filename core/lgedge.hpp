//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

#include <cassert>

#include "iassert.hpp"
#include "fmt/format.h"

#include "explicit_type.hpp"

using Index_ID = Explicit_type<uint64_t, struct Index_ID_struct>;

typedef int64_t  SIndex_ID;  // Short Edge must be signed +- offset
typedef uint16_t Port_ID;    // ports have a set order (a-b != b-a)

constexpr int Index_bits = 34;
constexpr int Port_bits  = 12;

struct __attribute__((packed)) LEdge_Internal {  // 6 bytes total

protected:
  Port_ID get_inp_pid() const { return inp_pid; }

public:
  friend class Edge;

  // TODO: the snode and input can be avoided by accessing the Node_Internal information
  bool     snode : 1;             // 1 bit
  bool     input : 1;             // 1 bit
  uint64_t raw_idx : Index_bits;  // abs (too much, 32 bits enough already not live)
  Port_ID  inp_pid : Port_bits;   // 10 bits ; abs

  bool     is_snode() const { return snode; }
  bool     is_input() const { return input; }
  Index_ID get_idx() const {
    I(raw_idx);
    return raw_idx;
  }

  bool set(Index_ID _idx, Port_ID _inp_pid, bool _input) {
    I(_idx < (1LL << Index_bits));
    I(_inp_pid < (1 << Port_bits));

    snode   = 0;
    input   = _input;
    raw_idx = _idx.value;
    inp_pid = _inp_pid;

    return true;
  }

  void set_idx(Index_ID _idx) {
    I(snode == 0);
    I(_idx < ((1LL << Index_bits) - 1));
    raw_idx = _idx.value;
  }
};

struct __attribute__((packed)) SEdge_Internal {  // 2 bytes total
  bool      snode : 1;                           //  1 bit
  bool      input : 1;                           //  1 bit
  SIndex_ID ridx : 12;                           //  relative
  Port_ID   inp_pid : 2;                         //   2 bits ; abs

  bool     is_snode() const { return snode; }
  bool     is_input() const { return input; }
  Port_ID  get_inp_pid() const { return inp_pid; }
  Index_ID get_page_idx() const;

  Index_ID get_idx(Index_ID idx) const {
    I(ridx);
    I(idx == get_page_idx());
    return idx + ridx;
  }

  bool set(Index_ID _idx, Port_ID _inp_pid, bool _input) {
    if (_inp_pid > 3) {  // 2 bits
      // fmt::print("P:{}\n",_inp_pid);
      return false;
    }
    Index_ID  abs_idx   = static_cast<SIndex_ID>(get_page_idx());
    SIndex_ID delta_idx = static_cast<SIndex_ID>(_idx) - abs_idx;
    if (delta_idx >= ((1 << 11) - 1) || delta_idx < (-((1 << 11) - 1))) {
      // fmt::print("D:{}\n",delta_idx);
      return false;
    }

    snode   = 1;
    input   = _input;
    ridx    = delta_idx;
    inp_pid = _inp_pid;

    return true;
  }
};

class LGraph;

#if 0
// WARNING: deprecated
// Node_driver_pin : public Node_pin
// Node_sink_pin   : public Node_pin
#endif
class Node_pin {
protected:
  friend class ConstNode;
  friend class Node;
  friend class Edge;
  friend class LGraph_Base;
  friend class LGraph;
  // TODO: add LGraph so that it has more self-contained operations
  // E.g: it can find/check that idx matches the (nid,pid) entry for speed
  //LGraph   *g;
  Index_ID idx;
  Port_ID  pid;
  bool     input;

  Node_pin(Index_ID _idx, Port_ID _pid, bool _input) : idx(_idx), pid(_pid), input(_input) { I(_idx); }
public:
  Node_pin() : idx(0), pid(0), input(false) { }

#if 0
  // TODO: once we have the attribute and collapse lgraphbase and lgraph
  static driver_pin(const Node &node, Port_ID _pid) {
    return Node_pin(node.get_nid(), _pid, false);
  }
#endif

#if 1
  // WARNING: deprecated: This should be moved to protected, and friend by lgraph only
  Index_ID get_idx()   const { I(idx); return idx;    }
  Port_ID  get_pid()   const { I(idx); return pid;    }
#endif

  bool     is_input()  const { I(idx); return input;  }
  bool     is_output() const { I(idx); return !input; }

#if 0
  // FUTURE:
  std::string_view get_wirename() const;
#endif

  bool operator==(const Node_pin &other) const { I(idx); return (idx == other.idx) && (pid == other.pid) && (input == other.input); }

  bool operator!=(const Node_pin &other) const { I(idx); return (idx != other.idx) || (pid != other.pid) || (input != other.input); }

  bool operator<(const Node_pin &other) const {
    I(idx);
    return (idx < other.idx) || (idx == other.idx && pid < other.pid) ||
           (idx == other.idx && pid == other.pid && input && !other.input);
  }
};

class __attribute__((packed)) Edge {  // 2 bytes total
protected:
  friend class LGraph;
  friend class Node_Internal;

  uint64_t snode : 1;
  uint64_t input : 1;  // Same position for SEdge and LEdge
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused"
  uint64_t pad2 : 8;
#pragma clang diagnostic pop
  Index_ID get_page_idx() const;

  Port_ID get_dst_pid() const;

  Port_ID get_inp_pid() const {
    const SEdge_Internal *s = reinterpret_cast<const SEdge_Internal *>(this);
    if (is_snode()) return s->get_inp_pid();

    const LEdge_Internal *l = reinterpret_cast<const LEdge_Internal *>(this);
    return l->get_inp_pid();
  }

  const Edge *find_edge(const Edge *bt, const Edge *et, Index_ID ptr_nid, Port_ID inp_pod, Port_ID dst_pid) const;

  const Edge &get_reverse_for_deletion() const;

  Index_ID get_self_idx() const;  // WARNING: it can point to overflow. Be careful!
  Index_ID get_self_root_idx() const;

public:
  friend struct Node_Internal_Page;
  friend class Node_Internal;
  friend class LGraph_Base;
  friend struct LEdge;
  friend struct SEdge;

  int next_node_inc() const {
    if (is_snode()) return 1;
    return 3;
  };
  bool is_last_input() const;
  bool is_last_output() const;

  bool is_input() const { return input; }

  bool is_snode() const { return snode; }
  void set_snode(bool s) {
    I(snode == reinterpret_cast<const SEdge_Internal *>(this)->is_snode());
    snode = s;
    I(snode == reinterpret_cast<const SEdge_Internal *>(this)->is_snode());
  }

  // Output edge: inp (self_nid, dst_pid) -> out (idx, inp_pid)
  // Input edge : inp (idx, dst_pid)      -> out (self_nid, inp_pid)
  // TODO: Rename to setup_driver_pin and get_sink_pin
  Node_pin get_out_pin() const {
    if (is_input())
      return Node_pin(get_idx(), get_inp_pid(), false);
    else
      return Node_pin(get_self_root_idx(), get_dst_pid(), false);
  };
  Node_pin get_inp_pin() const {
    if (is_input())
      return Node_pin(get_self_root_idx(), get_dst_pid(), true);
    else
      return Node_pin(get_idx(), get_inp_pid(), true);
  };

  Index_ID get_self_nid() const;
  Index_ID get_idx() const {
    const SEdge_Internal *s = reinterpret_cast<const SEdge_Internal *>(this);
    if (is_snode()) return s->get_idx(get_page_idx());

    const LEdge_Internal *l = reinterpret_cast<const LEdge_Internal *>(this);
    return l->get_idx();
  }
  void dump() const {
    const SEdge_Internal *s = reinterpret_cast<const SEdge_Internal *>(this);
    Index_ID              a = -1;
    if (is_snode()) a = s->ridx;

    fmt::print("snode:{} page_idx:{} a:{} addr:{:x}", is_snode(), get_page_idx(), a, (uint64_t)this);
  }

  uint16_t get_bits() const;
  bool     is_root() const;

  bool is_page_align() const {
    return ((((uint64_t)this) & 0xFFF) == 0);  // page align.
  }

  bool set(Index_ID _idx, Port_ID _inp_pid, Port_ID _dst_pid, bool _input) {
    I(!is_page_align());
    I(get_dst_pid() == _dst_pid);

    SEdge_Internal *s = reinterpret_cast<SEdge_Internal *>(this);
    if (is_snode()) {
      return s->set(_idx, _inp_pid, _input);
    }
    LEdge_Internal *l = reinterpret_cast<LEdge_Internal *>(this);
    return l->set(_idx, _inp_pid, _input);
  }

private:  // all constructor&assignment should be marked as private
  Edge()                = default;
  Edge(const Edge &rhs) = default;
  Edge(Edge &&rhs)      = delete;
  ~Edge()               = default;
  Edge &operator=(const Edge &rhs) = default;
  Edge &operator=(Edge &&rhs) = delete;
};

struct __attribute__((packed)) LEdge : public Edge {  // 6 bytes total
  LEdge() { snode = 0; };
  uint64_t pad : 32;
};

struct __attribute__((packed)) SEdge : public Edge {  // 2 bytes total
  SEdge() { snode = 1; };
};

enum Node_State {
  Invalid_State = 0,  // No used or initialized
  // bit3 == 1 is_node_state
  Free_Node_State = 1,  // Node was deleted, it is in a free list
  Page_Node_State = 2,  // No node in use, page info (page align)
  Next_Node_State = 6,  // Entry in use, but it is an extension from another root, but there are more in the list
  Last_Node_State = 7   // Entry in use, but it is an extension from another, and it is the last in the list
};

class Node_Internal;

struct alignas(32) Node_Internal_Page {
  Node_State state : 3;  // 1byte
  uint8_t    pad1[7];    // 7bytes waste just to get Index_ID aligned
  Index_ID   idx;        // 36bits but OK to give 8bytes for speed
  Index_ID   free_idx;   // ptr to the first free node
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused"
  uint8_t pad2[32 - 24];
#pragma clang diagnostic pop

  Index_ID get_idx() const { return idx; }

  static Node_Internal_Page &get(const SEdge_Internal *ptr) {
    // Every 1 Page a full Node is reserved for pointer keeping
    uint64_t root_int = (uint64_t)ptr;
    root_int          = root_int >> 12;
    root_int          = root_int << 12;

    Node_Internal_Page *root = (Node_Internal_Page *)root_int;
    I(root->state == Page_Node_State);

    return *root;
  }
  static Node_Internal_Page &get(const Edge *ptr) { return get(reinterpret_cast<const SEdge_Internal *>(ptr)); }

  static Node_Internal_Page &get(const Node_Internal *ptr) { return get(reinterpret_cast<const SEdge_Internal *>(ptr)); }

  bool is_page_align() const {
    return ((((uint64_t)this) & 0xFFF) == 0);  // page align.
  }
  void set_page(Index_ID _idx) {
    I(is_page_align());
    I(_idx < (1LL << Index_bits));
    idx   = _idx;
    state = Page_Node_State;
  }
};

// NOTE:
// Maybe we should have an overflow table. Then only roots are in the idx
// (node_pin). 4G pins per lgraph (constraint Index_bits to 32)
//
// Only large netlist could have more pins, the idea is that lgraph loaders
// should split in subgraphs large netlist. The split is transparent in reading
// and output.
//
class __attribute__((packed)) Node_Internal {
private:
  // BEGIN 10 Bytes common payload
  Node_State state : 3;  // State must be the first thing (Node_Internal_Page)
  uint16_t   root : 1;
  uint16_t   inp_pos : 4;
  uint16_t   bits : 14;
  uint16_t   graph_io_input : 1; // FIXME: remove this bits. Use idx==1 
  uint16_t   graph_io_output : 1; // FIXME: remove this bits. Use idx==2 
  uint16_t   out_pos : 4;
  uint16_t   next_lower2 : 2;
  uint16_t   inp_long : 2;
  // 4 bytes aligned
public:
  // WARNING: This must be here not at the end of the structure. OTherwise the
  // iterator goes the 64byte boundary for the outputs
  static constexpr int Num_SEdges = 16 - 5;  // 5 entries for the 80 bits (10 bytes)
  SEdge                sedge[Num_SEdges];    // WARNING: Must not be the last field in struct or iterators fail
private:
  uint64_t nid : Index_bits;     // 36bits, 4 byte aligned
  Port_ID  dst_pid : Port_bits;  // Not well aligned
  uint16_t out_long : 2;
  // END 10 Bytes common payload

  void try_recycle();
  void del_input_int(const Edge &out_edge);
  void del_output_int(const Edge &out_edge);

  void del_output(const Edge &out_edge) {
    // Node_Internal *ptr_node = &Node_Internal::get(&out_edge);
    // Index_ID       ptr_idx  = ptr_node->get_self_idx();
    // Index_ID       ptr_nid  = ptr_node->get_nid();

    const Edge &inp_edge = out_edge.get_reverse_for_deletion();
    I(inp_edge.is_input());
    I(!out_edge.is_input());

    Node_Internal::get(&inp_edge).del_input_int(inp_edge);
    del_output_int(out_edge);

    try_recycle();
  }

  void del_input(const Edge &inp_edge) {
    // Node_Internal *ptr_node = &Node_Internal::get(&inp_edge);
    // Index_ID       ptr_idx  = ptr_node->get_self_idx();
    // Index_ID       ptr_nid  = ptr_node->get_nid();

    const Edge &out_edge = inp_edge.get_reverse_for_deletion();
    I(inp_edge.is_input());
    I(!out_edge.is_input());

    Node_Internal::get(&out_edge).del_output_int(out_edge);
    del_input_int(inp_edge);

    try_recycle();
  }

protected:
  friend class Edge;
  Index_ID get_self_idx() const; // WARNING: It can point to overflow

public:
  Node_Internal() { reset(); }

  uint8_t get_inp_pos() const { return inp_pos; }
  uint8_t get_out_pos() const { return out_pos; }

  uint8_t get_num_local_inputs() const {
    uint8_t n = inp_pos;
    I(inp_long * 2 <= n);
    n -= 2 * inp_long;
    return n;
  }
  uint8_t get_num_local_outputs() const {
    uint8_t n = out_pos;
    I(out_long * 2 <= n);
    n -= 2 * out_long;
    return n;
  }

  int32_t get_node_num_inputs() const;
  int32_t get_node_num_outputs() const;
  bool    has_node_inputs() const;
  bool    has_node_outputs() const;

  bool    has_pin_inputs() const;
  bool    has_pin_outputs() const;

  void reset() {
    bits            = 0;
    dst_pid         = 0;
    root            = 1;
    graph_io_input  = false;
    graph_io_output = false;
    inp_pos         = 0;  // SEdge uses 1, LEdge uses 3
    out_pos         = 0;  // SEdge uses 1, LEdge uses 3
    inp_long        = 0;
    out_long        = 0;
    nid             = 0;
    state           = Last_Node_State;
  }

  bool is_root() const {
    I(is_node_state());
    return root;
  }
  bool is_master_root() const {
    I(is_node_state());
    bool ms = nid == get_self_idx().value;
    if (ms) I(root);

    return ms;
  }
  bool is_graph_io() const { return graph_io_input || graph_io_output; }
  bool is_graph_io_input() const { return graph_io_input; }
  bool is_graph_io_output() const { return graph_io_output; }
  void set_graph_io_input() {
    I(!graph_io_output);
    graph_io_input = true;
  }
  void set_graph_io_output() {
    I(!graph_io_input);
    graph_io_output = true;
  }

  void set_root() { root = true; }
  void clear_root() {
    root            = false;
    graph_io_input  = false;
    graph_io_output = false;
  }

  Port_ID get_dst_pid() const { return dst_pid; }
  void    set_dst_pid(Port_ID eid) {
    I(eid < (1 << Port_bits));
    dst_pid = eid;
  }
  Index_ID get_nid() const {
    I(nid);
    return nid;
  }
  Index_ID             get_master_root_nid() const {
    I(nid);
    if (likely(root)) return nid;

    I(get_root().get_nid() == get_master_root().get_nid());

    return get_root().get_nid();  // No need to do get_master_root
  }

  const Node_Internal &get_root() const;
  const Node_Internal &get_master_root() const;

  void set_nid(Index_ID _nid) {
    I(_nid < (1LL << Index_bits));
    I(!is_graph_io());
    nid = _nid.value;
    GI(nid == get_self_idx().value, root);
  }

  const Node_Internal &get(Index_ID idx2) const {
    const Node_Internal *root_n = this;
    return root_n[idx2.value - get_self_idx().value];
  }

  static Node_Internal &get(const Edge *ptr) {
    // WARNING: this belongs to a structure that it is cache aligned (32 bytes)
    uint64_t root_int = (uint64_t)ptr;
    root_int          = root_int >> 5;
    root_int          = root_int << 5;

    Node_Internal *root_n = reinterpret_cast<Node_Internal *>(root_int);
    I(root_n->is_node_state());

    return *root_n;
  }

  Index_ID get_next() const {
    I(is_next_state());
    uint32_t *idx_upp = (uint32_t *)(&sedge[0]);
    uint64_t  idx_val = *idx_upp;
    idx_val <<= 2;
    idx_val |= next_lower2;
    return idx_val;
  }

  void set_next_state(Index_ID _idx) {
    I(is_next_state());
    next_lower2       = _idx.value;
    uint32_t *idx_upp = (uint32_t *)(&sedge[0]);
    *idx_upp          = _idx.value >> 2;
  }

  void push_next_state(Index_ID _idx) {
    I(is_last_state());

    I((inp_pos + out_pos + 2) < Num_SEdges);
    for (int i = 0; i < inp_pos; i++) {
      sedge[inp_pos - i + 2 - 1] = sedge[inp_pos - i - 1];
    }
    state = Next_Node_State;
    set_next_state(_idx);

    I(is_next_state());
  }

  void assimilate_edges(Node_Internal &other);

  void set_last_state() { state = Last_Node_State; }
  void set_free_state() {
    state = Free_Node_State;
    I(!root);  // For the moment
    nid = 0;
  }
  bool is_next_state() const { return state == Next_Node_State; }
  bool is_free_state() const { return state == Free_Node_State; }
  bool is_page_state() const { return state == Page_Node_State; }
  bool is_last_state() const { return state == Last_Node_State; }
  bool is_node_state() const {
    I(!((is_next_state() || is_last_state()) ^ ((state >> 2) & 1)));  // Same upper bit
    return (state >> 2) & 1;
  }
  bool is_page_align() const {
    return ((((uint64_t)this) & 0xFFF) == 0);  // page align.
  }

  void del(const Edge &edge);

  void inc_outputs(bool large = false) {
    I(has_space(large));
    if (large) {
      out_pos += 3;
      out_long++;
    } else {
      out_pos++;
    }
  }
  void inc_inputs(bool large = false) {
    if (large)
      I(((LEdge_Internal *)&sedge[next_free_input_pos()])->get_idx() != 0);
    else
      I(((SEdge_Internal *)&sedge[next_free_input_pos()])->get_idx(Node_Internal_Page::get(this).get_idx()) != 0);

    I(has_space(large));
    if (large) {
      inp_pos += 3;
      inp_long++;
    } else {
      inp_pos++;
    }
  }
  bool has_local_inputs() const { return inp_pos > 0; }
  bool has_local_outputs() const { return out_pos > 0; }
  int  get_space_available() const {
    if (state == Last_Node_State) return (Num_SEdges - (inp_pos + out_pos));
    return (Num_SEdges - (inp_pos + out_pos + 2));
  }

  bool has_next_space() const {
    I(state == Last_Node_State);
    return (inp_pos + out_pos + 2) < Num_SEdges;  // pos 0 uses 2 entries (4bytes ptr next)
  }

  bool has_space(bool large = false) const {
    int reserve = 0;
    if (large) reserve = 4;  // +2 is enough but +4 avoids cross boundary issues
    if (state == Last_Node_State) return (inp_pos + out_pos + reserve) < Num_SEdges;
    return (inp_pos + out_pos + 2 + reserve) < Num_SEdges;  // pos 0 uses 2 entries (4bytes ptr next)
  }

  uint16_t get_bits() const {
    I(is_root());
    return bits;
  }
  void set_bits(uint16_t _bits) {
    I(is_root());
    I(_bits < (1 << 14));
    bits = _bits;
  }

  const SEdge *get_input_begin() const { return &sedge[get_input_begin_pos_int()]; }
  const SEdge *get_input_end() const { return &sedge[get_input_end_pos_int()]; }

  const SEdge *get_output_begin() const { return &sedge[get_output_begin_pos_int() + 1]; }
  const SEdge *get_output_end() const { return &sedge[Num_SEdges]; }

  int next_free_input_pos() const {
    I(has_space());
    return get_input_end_pos_int();
  }

  int next_free_output_pos() const {
    I(has_space());
    return get_output_begin_pos_int();
  }

  void dump() const;
  void dump_full() const;

private:
  int get_input_end_pos_int() const {
    if (inp_pos == 0) return get_input_begin_pos_int();

    int pos = inp_pos;
    if (state != Last_Node_State) pos += 2;

    return pos;
  }

  int get_input_begin_pos_int() const {
    if (state == Last_Node_State) return 0;
    return 2;
  }
  int get_output_begin_pos_int() const { return Num_SEdges - out_pos - 1; }
};
