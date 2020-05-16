//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgraph_base_core.hpp"

class Node_pin;

typedef int64_t SIndex_ID;  // Short Edge_raw must be signed +- offset

struct __attribute__((packed)) LEdge_Internal {  // 6 bytes total
protected:
  Port_ID get_inp_pid() const { return inp_pid; }

public:
  friend class Edge_raw;

  bool     snode : 1;            // 1 bit
  bool     input : 1;            // 1 bit
  Port_ID  inp_pid : Port_bits;  // 15 bits abs
  uint64_t raw_idx : Index_bits; // 31 bits abs

  bool     is_snode() const { return snode; }
  bool     is_input() const { return input; }
  Index_ID get_idx() const {
    I(raw_idx);
    return raw_idx;
  }

  bool set(Index_ID _idx, Port_ID _inp_pid, bool _input) {
    I(_idx < (1LL << (Index_bits - 1)));
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

struct __attribute__((packed)) SEdge_Internal {  // 3 bytes total
  bool      snode : 1;                           //  1 bit
  bool      input : 1;                           //  1 bit
  SIndex_ID ridx : 19;                           //  relative
  Port_ID   inp_pid : 3;                         //  3 bits ; abs

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
    if (_inp_pid > 7) {  // 3 bits
      // fmt::print("P:{}\n",_inp_pid);
      return false;
    }
    Index_ID  abs_idx   = static_cast<SIndex_ID>(get_page_idx());
    SIndex_ID delta_idx = static_cast<SIndex_ID>(_idx) - abs_idx;
    if (delta_idx >= ((1 << 18) - 1) || delta_idx < (-((1 << 18) - 1))) {
      // fmt::print("D:{}\n",delta_idx);
      return false;
    }
    I(delta_idx);

    snode   = 1;
    input   = _input;
    ridx    = delta_idx;
    inp_pid = _inp_pid;

    return true;
  }
};

class LGraph;

class __attribute__((packed)) Edge_raw {  // 3 bytes total
protected:
  friend class LGraph;
  friend class Node_Internal;
  friend class Node_pin;

  uint64_t snode : 1;
  uint64_t input : 1;  // Same position for SEdge and LEdge
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused"
  uint64_t pad2 : 22;
#pragma clang diagnostic pop
  Index_ID get_page_idx() const;

  Port_ID get_dst_pid() const;

  Port_ID get_inp_pid() const {
    const SEdge_Internal *s = reinterpret_cast<const SEdge_Internal *>(this);
    if (is_snode()) return s->get_inp_pid();

    const LEdge_Internal *l = reinterpret_cast<const LEdge_Internal *>(this);
    return l->get_inp_pid();
  }

  static const Edge_raw *find_edge(const Edge_raw *bt, const Edge_raw *et, Index_ID ptr_nid, Port_ID inp_pod, Port_ID dst_pid);

  const Edge_raw *get_reverse_for_deletion() const;

  Index_ID get_self_idx() const;  // WARNING: it can point to overflow. Be careful!
  Index_ID get_self_root_idx() const;

public:
  friend struct Node_Internal_Page;
  friend class Node_Internal;
  friend class LGraph_Base;
  friend struct LEdge;
  friend struct SEdge;

  int next_node_inc() const { return is_snode()? 1:2; }
  bool is_last_input() const;
  bool is_last_output() const;

  bool is_input() const { return input; }

  bool is_snode() const { return snode; }
  void set_snode(bool s) {
    I(snode == reinterpret_cast<const SEdge_Internal *>(this)->is_snode());
    snode = s;
    I(snode == reinterpret_cast<const SEdge_Internal *>(this)->is_snode());
  }

  Node_pin get_out_pin(LGraph *g, LGraph *cg, const Hierarchy_index &hidx) const;
  Node_pin get_inp_pin(LGraph *g, LGraph *cg, const Hierarchy_index &hidx) const;

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

  uint32_t get_bits() const;
  bool     is_root() const;

  bool is_page_align() const {
    return ((((uint64_t)this) & 0xFFF) == 0);  // page align.
  }

private:  // all constructor&assignment should be marked as private
  Edge_raw()                    = default;
  Edge_raw(const Edge_raw &rhs) = default;
  Edge_raw(Edge_raw &&rhs)      = delete;
  ~Edge_raw()                   = default;
  Edge_raw &operator=(const Edge_raw &rhs) = default;
  Edge_raw &operator=(Edge_raw &&rhs) = delete;
};

struct __attribute__((packed)) LEdge : public Edge_raw {  // 6 bytes total
  LEdge() { snode = 0; };
  uint64_t pad_match : 24; // 3bytes in edge_raw + 24/3 = 6bytes total
};

struct __attribute__((packed)) SEdge : public Edge_raw {  // 3 bytes total
  SEdge() { snode = 1; };
};

enum Node_state {
  Invalid_state = 0,  // No used or initialized
  // bit3 == 1 is_node_state
  Free_node_state = 1,  // Node was deleted, it is in a free list
  Page_node_state = 2,  // No node in use, page info (page align)
  Next_node_state = 6,  // Entry in use, but it is an extension from another root, but there are more in the list
  Last_node_state = 7   // Entry in use, but it is an extension from another, and it is the last in the list
};

class Node_Internal;

struct __attribute__((packed)) Node_Internal_Page {
  Node_state state : 3;  // 1byte
  uint8_t    pad1[7];    // 7bytes waste just to get Index_ID aligned
  uint32_t   idx;        // 4bytes 32bits but for speed
  uint32_t   free_idx;   // 4bytes 32bits to the first free node
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused"
  uint8_t pad2[32 - 16];
#pragma clang diagnostic pop

  Index_ID get_idx() const { return idx; }

  static Node_Internal_Page &get(const SEdge_Internal *ptr) {
    // Every 1 Page a full Node is reserved for pointer keeping
    uint64_t root_int = (uint64_t)ptr;
    root_int          = root_int >> 12;
    root_int          = root_int << 12;

    Node_Internal_Page *root = (Node_Internal_Page *)root_int;
    I(root->state == Page_node_state);

    return *root;
  }
  static Node_Internal_Page &get(const Edge_raw *ptr) { return get(reinterpret_cast<const SEdge_Internal *>(ptr)); }

  static Node_Internal_Page &get(const Node_Internal *ptr) { return get(reinterpret_cast<const SEdge_Internal *>(ptr)); }

  bool is_page_align() const {
    return ((((uint64_t)this) & 0xFFF) == 0);  // page align.
  }
  void set_page(Index_ID _idx) {
    I(is_page_align());
    I(_idx < (1LL << Index_bits));
    idx   = _idx;
    state = Page_node_state;
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
  // BEGIN 12 Bytes common payload
  Node_state state : 3;  // State must be the first thing (Node_Internal_Page)
  uint16_t   inp_long : 2;      // 6 bytes each. Just 9 at most
  uint16_t   out_long : 2;
  uint32_t   bits : Bits_bits;
  uint64_t   nid : Index_bits;  // 31bits, 4 byte aligned
  uint16_t   future_opcode:9;   // 9 bits to reduce type to have only in overflows
  uint16_t   root : 1;
  Port_ID    dst_pid : Port_bits;  // 15bits
  // 8 bytes aligned
public:
  // WARNING: This must be here not at the end of the structure. OTherwise the
  // iterator goes the 64byte boundary for the outputs
  static constexpr int Num_SEdges = 7;
  SEdge                sedge[Num_SEdges]; // WARNING: Must not be the last field in struct or iterators fail
private:
  // Start byte 8*17*3=59
  uint16_t   driver_setup : 1;
  uint16_t   sink_setup : 1;
  uint16_t   inp_pos : 3;
  uint16_t   out_pos : 3;
  // END 13 Bytes common payload

  void try_recycle();
  void del_input_int(const Edge_raw *out_edge);
  void del_output_int(const Edge_raw *out_edge);

  void del_output(const Edge_raw *out_edge) {
    // Node_Internal *ptr_node = &Node_Internal::get(&out_edge);
    // Index_ID       ptr_idx  = ptr_node->get_self_idx();
    // Index_ID       ptr_nid  = ptr_node->get_nid();

    const Edge_raw *inp_edge = out_edge->get_reverse_for_deletion();
    I(inp_edge->is_input());
    I(!out_edge->is_input());

    Node_Internal::get(inp_edge).del_input_int(inp_edge);
    del_output_int(out_edge);

    try_recycle();
  }

  void del_input(const Edge_raw *inp_edge) {
    // Node_Internal *ptr_node = &Node_Internal::get(&inp_edge);
    // Index_ID       ptr_idx  = ptr_node->get_self_idx();
    // Index_ID       ptr_nid  = ptr_node->get_nid();

    const Edge_raw *out_edge = inp_edge->get_reverse_for_deletion();
    I(inp_edge->is_input());
    I(!out_edge->is_input());

    Node_Internal::get(out_edge).del_output_int(out_edge);
    del_input_int(inp_edge);

    try_recycle();
  }

protected:
  friend class Edge_raw;
  Index_ID get_self_idx() const;  // WARNING: It can point to overflow

public:
  Node_Internal() { reset(); }

  uint8_t get_num_local_short() const { return inp_pos + out_pos - 2 * inp_long - 2 * out_long; }
  uint8_t get_num_local_long() const { return inp_long + out_long; }

  uint8_t get_inp_pos() const { return inp_pos; }
  uint8_t get_out_pos() const { return out_pos; }

  uint8_t get_num_local_inputs() const {
    uint8_t n = inp_pos;
    I(inp_long * (2) <= n);
    n -= (2 - 1) * inp_long;
    return n;
  }
  uint8_t get_num_local_outputs() const {
    uint8_t n = out_pos;
    I(out_long * (2) <= n);
    n -= (2 - 1) * out_long;
    return n;
  }

  int32_t get_node_num_inputs() const;
  int32_t get_node_num_outputs() const;
  bool    has_node_inputs() const;
  bool    has_node_outputs() const;

  bool has_pin_inputs() const;
  bool has_pin_outputs() const;

  void reset() {
    state        = Last_node_state;
    root         = 1;
    bits         = 0;
    dst_pid      = 0;
    inp_pos      = 0;  // SEdge uses 1, LEdge uses 2
    driver_setup = 0;
    sink_setup   = 0;
    out_pos      = 0;  // SEdge uses 1, LEdge uses 2
    inp_long     = 0;
    out_long     = 0;
    nid          = 0;
    future_opcode = 0;
  }

  bool is_deleted() const {
    if (likely(nid)) return false;
    if (state == Last_node_state) return true;
    if (state == Free_node_state) return false;

    I(false);  // if a node is deleted it should be Free (todo after garbage collect) or Last
    return false;
  }
  bool is_valid() const {
    return nid && is_node_state();
  }

  bool is_root() const {
    I(is_node_state());
    return root;
  }
  bool is_graph_io() const {
    return nid == 1 || nid == 2; // hardcoded input or output
  }
  bool is_master_root() const {
    I(is_node_state());
    bool ms = nid == get_self_idx().value;
    if (ms) I(root);

    return ms;
  }

  void set_root() { root = true; }
  void clear_root() { root = false; }

  Port_ID get_dst_pid() const { return dst_pid; }
  void    set_dst_pid(Port_ID eid) {
    I(eid < (1 << Port_bits));
    dst_pid = eid;
  }
  Index_ID get_nid() const {
    I(nid);
    return nid;
  }
  Index_ID get_master_root_nid() const {
    I(nid);
    if (likely(root)) return nid;

    I(get_root().get_nid() == get_master_root().get_nid());

    return get_root().get_nid();  // No need to do get_master_root
  }

  const Node_Internal &get_root() const;
  const Node_Internal &get_master_root() const;

  void set_nid(Index_ID _nid) {
    I(_nid < (1LL << Index_bits));
    nid = _nid.value;
    GI(nid == get_self_idx().value, root);
  }

  inline const Node_Internal &get(Index_ID idx2) const {
    const Node_Internal *root_n = this;
    return root_n[idx2.value - get_self_idx().value];
  }

  inline static Node_Internal &get(const Edge_raw *ptr) {
    // WARNING: this belongs to a structure that it is cache aligned (32 bytes)
    uint64_t root_int = (uint64_t)ptr;
    root_int          = root_int >> 5; // 32 byte alignment
    root_int          = root_int << 5; // 32 byte alignment

    Node_Internal *root_n = reinterpret_cast<Node_Internal *>(root_int);
    I(root_n->is_node_state());

    return *root_n;
  }

  Index_ID get_next() const {
    I(is_next_state());
    uint32_t *idx_upp = (uint32_t *)(&sedge[0]);
    return *idx_upp;
  }

  void set_next_state(Index_ID _idx) {
    I(is_next_state());
    uint32_t *idx_upp = (uint32_t *)(&sedge[0]);
    *idx_upp          = _idx.value;
  }

  void push_next_state(Index_ID _idx) {
    I(is_last_state());

    I((inp_pos + out_pos + 2) < Num_SEdges);
    for (int i = 0; i < inp_pos; i++) {
      sedge[inp_pos - i + 2 - 1] = sedge[inp_pos - i - 1];
    }
    state = Next_node_state;
    set_next_state(_idx);

    I(is_next_state());
  }

  void assimilate_edges(Node_Internal *other);

  void set_last_state() { state = Last_node_state; }
  void set_free_state() {
    state = Free_node_state;
    I(!root);  // For the moment
    nid = 0;
  }
  bool is_next_state() const { return state == Next_node_state; }
  bool is_free_state() const { return state == Free_node_state; }
  bool is_page_state() const { return state == Page_node_state; }
  bool is_last_state() const { return state == Last_node_state; }
  bool is_node_state() const {
    I(!((is_next_state() || is_last_state()) ^ ((state >> 2) & 1)));  // Same upper bit
    return (state >> 2) & 1;
  }
  bool is_page_align() const {
    return ((((uint64_t)this) & 0xFFF) == 0);  // page align.
  }

  void del(const Edge_raw *edge_raw);
  bool del(Index_ID src_idx, Port_ID pid, bool input);

  void inc_outputs(bool large = false) {
    if (large) {
      I(has_space_long());
      out_pos += 2;
      I(out_long < 9);  // To avoid overflow
      out_long++;
    } else {
      I(has_space_short());
      out_pos++;
    }
  }
  void inc_inputs(bool large = false) {
    if (large) {
      I(has_space_long());
      inp_pos += 2;
      I(inp_long < 9);  // To avoid overflow
      inp_long++;
    } else {
      I(has_space_short());
      inp_pos++;
    }
  }
  bool has_local_inputs() const { return inp_pos > 0; }
  bool has_local_outputs() const { return out_pos > 0; }
  int  get_space_available() const {
    auto reserve = state == Last_node_state ? 0 : 2;
    return (Num_SEdges - (inp_pos + out_pos + reserve));
  }

  bool has_next_space() const {
    I(state == Last_node_state);
    return (inp_pos + out_pos + 2) < Num_SEdges;  // pos 0 uses 2 entries (4bytes ptr next)
  }

  bool has_space_long() const {
    int reserve = state == Last_node_state ? 0 : 2;
    return inp_long < 8 && out_long < 8 && (reserve + inp_pos + out_pos + 2) < Num_SEdges;
  }
  bool has_space_short() const {
    int reserve = state == Last_node_state ? 0 : 2;
    return (inp_pos + out_pos + reserve) < Num_SEdges;  // pos 0 uses 2 entries (4bytes ptr next)
  }

  uint32_t get_bits() const {
    I(is_root());
    return bits;
  }
  void set_bits(uint32_t _bits) {
    I(_bits < (1UL << Bits_bits));
    I(is_root());
    bits = _bits;
  }
  bool is_driver_setup() const { return driver_setup != 0; }
  bool is_sink_setup() const { return sink_setup != 0; }
  void set_driver_setup() { driver_setup = 1; }
  void set_sink_setup() { sink_setup = 1; }

  const SEdge *get_input_begin() const { return &sedge[get_input_begin_pos_int()]; }
  const SEdge *get_input_end() const { return &sedge[get_input_end_pos_int()]; }

  const SEdge *get_output_begin() const { return &sedge[get_output_begin_pos_int() + 1]; }
  const SEdge *get_output_end() const { return &sedge[Num_SEdges]; } // WARNING: A position over the limit

  int next_free_input_pos() const {
    I(has_space_short());
    return get_input_end_pos_int();
  }

  int next_free_output_pos() const {
    I(has_space_short());
    return get_output_begin_pos_int();
  }

  void dump() const;
  void dump_full() const;

private:
  int get_input_end_pos_int() const {
    // if (inp_pos == 0) return get_input_begin_pos_int();

    int pos = inp_pos;
    if (state != Last_node_state) pos += 2;

    return pos;
  }

  int get_input_begin_pos_int() const {
    if (state == Last_node_state) return 0;
    return 2;
  }
  int get_output_begin_pos_int() const { return Num_SEdges - out_pos - 1; }
};
