//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

#include "lgraph.hpp"
#include "lgedge.hpp"

class Edge_raw_iterator {
public:
  class CPod_iterator {
  public:
    CPod_iterator(const Edge_raw *_ptr, const Edge_raw *_e, bool _inputs) : ptr(_ptr), e(_e), inputs(_inputs) {}
    CPod_iterator operator++();
    CPod_iterator operator--() {
      CPod_iterator i(ptr, e, inputs);
      ptr -= ptr->next_node_inc();
      return i;
    }
    bool        operator!=(const CPod_iterator &other) { return ptr != other.ptr; }
    const Edge_raw &operator*() const { return *ptr; }

  private:
    const Edge_raw *ptr;
    const Edge_raw *e;
    const bool  inputs;
  };

private:
protected:
  const bool  inputs;
  const Edge_raw *b;
  const Edge_raw *e;

public:
  Edge_raw_iterator() = delete;
  explicit Edge_raw_iterator(const Edge_raw *_b, const Edge_raw *_e, bool _inputs) : inputs(_inputs) {
    b = _b;
    e = _e;
    I(Node_Internal::get(e).is_node_state());
  }

  CPod_iterator begin() const { return CPod_iterator(b, e, inputs); }
  CPod_iterator end() const { return CPod_iterator(e, e, inputs); }
};

class CFast_edge_iterator {
public:
  CFast_edge_iterator(LGraph *_g, LGraph *_cg, const Hierarchy_index &_hidx, const Index_ID _nid, bool _visit_sub) : top_g(_g), current_g(_cg), hidx(_hidx), nid(_nid), visit_sub(_visit_sub) { I(hidx); }
  CFast_edge_iterator operator++();
  bool operator!=(const CFast_edge_iterator &other) {
    I(top_g == other.top_g);
    return nid != other.nid || hidx != other.hidx;
  }
  Node operator*() const {
    return Node(top_g, current_g, hidx, nid);
  }

private:
  std::vector<Node> h_stack;
  LGraph           *top_g;
  LGraph           *current_g;
  Hierarchy_index   hidx;
  Index_ID          nid;
  bool              visit_sub;
};


using Frontier_type = absl::flat_hash_map<Node::Compact, int32_t>;
using Node_set_type = absl::flat_hash_set<Node::Compact>;

class Edge_raw_iterator_base {
protected:
  Node            current_node;

  // State built during iteration
  const bool      visit_sub;
  Frontier_type  *frontier;      // 2G inputs at most
  Node_set_type  *pending;       // vertex that cleared the frontier
  Index_ID       *hardcoded_nid;
  Node_set_type  *global_visited;

  Edge_raw_iterator_base(const Edge_raw_iterator_base &other)
    :current_node(other.current_node)
    ,visit_sub(other.visit_sub)
    ,frontier(other.frontier)
    ,pending(other.pending)
    ,hardcoded_nid(other.hardcoded_nid)
    ,global_visited(other.global_visited) {
  }
public:
  Edge_raw_iterator_base(bool _visit_sub, Frontier_type *_frontier, Node_set_type *_pending, Index_ID *_hardcoded_nid, Node_set_type *_global_visited)
      : visit_sub(_visit_sub), frontier(_frontier), pending(_pending), hardcoded_nid(_hardcoded_nid), global_visited(_global_visited) { }

  virtual void    set_current_node_as_visited() = 0;

  Node operator*() const { return current_node; }
  // FIXME: Try this instead const &Node operator*() const { return current_node; }

  bool update_frontier();

  void set_next_node_to_visit() {
    if (unlikely(pending->empty())) {
      if (!update_frontier()) {
        current_node.update(0);
        I(current_node.is_invalid());
        return;
      }
    }

    I(!pending->empty());
    auto it = pending->begin();
    current_node.update(it->hidx, it->nid);
    I(!current_node.is_invalid());
    pending->erase(it);
  }
};

class CForward_edge_iterator : public Edge_raw_iterator_base {
protected:

  void insert_forward_graph_start_points(LGraph *lg, Hierarchy_index down_hidx);

public:
  CForward_edge_iterator(const CForward_edge_iterator &other)
    :Edge_raw_iterator_base(other) {
  }

  CForward_edge_iterator(LGraph *_g, bool _visit_sub, Frontier_type *_frontier, Node_set_type *_pending, Index_ID *_hardcoded_nid, Node_set_type *_global_visited);
  CForward_edge_iterator(LGraph *lg, bool _visit_sub)
    : Edge_raw_iterator_base(_visit_sub, nullptr, nullptr, nullptr, nullptr) {

    current_node.invalidate(lg);
  }

  bool operator!=(const CForward_edge_iterator &other) {
    I(current_node.get_top_lgraph() == other.current_node.get_top_lgraph());
    return current_node != other.current_node;
  }

  void set_current_node_as_visited();

  CForward_edge_iterator operator++() {
    I(!current_node.is_invalid());  // Do not call ++ after end
    CForward_edge_iterator i(*this);
    set_current_node_as_visited();
    set_next_node_to_visit();
    return i;
  }
};


class CBackward_edge_iterator : public Edge_raw_iterator_base {
private:
  void insert_backward_graph_start_points(LGraph *lg, Hierarchy_index down_hidx);

public:
  CBackward_edge_iterator(const CBackward_edge_iterator &other)
    :Edge_raw_iterator_base(other) {
  }

  CBackward_edge_iterator(LGraph *_g, bool _visit_sub, Frontier_type *_frontier, Node_set_type *_pending, Index_ID *_hardcoded_nid, Node_set_type *_global_visited);
  CBackward_edge_iterator(LGraph *lg, bool _visit_sub)
    : Edge_raw_iterator_base(_visit_sub, nullptr, nullptr, nullptr, nullptr) {

    current_node.invalidate(lg);
  }

  bool operator!=(const CBackward_edge_iterator &other) {
    I(current_node.get_top_lgraph() == other.current_node.get_top_lgraph());
    return current_node != other.current_node;
  }

  // find nodes not connected to output that are preventing the propagation
  // only use in case the backward fails
  void set_current_node_as_visited();

  CBackward_edge_iterator operator++() {
    I(!current_node.is_invalid());  // Do not call ++ after end
    CBackward_edge_iterator i(*this);

    set_current_node_as_visited();
    set_next_node_to_visit();
    return i;
  }
};

// Main iterator entry points: Fast_edge_iterator, Forward_edge_iterator, Backward_edge_iterator

class Fast_edge_iterator {
public:

private:
protected:
  LGraph                *top_g;
  const Hierarchy_index  it_hidx;
  const Index_ID         it_nid;
  const bool             visit_sub;

public:
  Fast_edge_iterator() = delete;
  explicit Fast_edge_iterator(LGraph *_g, bool _visit_sub) : top_g(_g), it_hidx(_g->hierarchy_root()), it_nid(0), visit_sub(_visit_sub) { }

  CFast_edge_iterator begin() const;
  CFast_edge_iterator end() const { return CFast_edge_iterator(top_g, top_g, top_g->hierarchy_root(), 0, visit_sub); }  // 0 is end index for iterator
};

class Backward_edge_iterator {
public:

private:
protected:
  LGraph        *top_g;
  const bool     visit_sub;
  Frontier_type  frontier;  // 2G inputs at most
  Node_set_type  pending;   // vertex that cleared the frontier
  Index_ID       hardcoded_nid;
  Node_set_type  global_visited;

public:
  Backward_edge_iterator() = delete;
  explicit Backward_edge_iterator(LGraph *_g, bool _visit_sub) : top_g(_g), visit_sub(_visit_sub) {
    hardcoded_nid = Node::Hardcoded_input_nid;
  }

  CBackward_edge_iterator begin() {
    if (top_g->empty())
      return end();

    CBackward_edge_iterator it2(top_g, visit_sub, &frontier, &pending, &hardcoded_nid, &global_visited);

    return it2;
  }

  CBackward_edge_iterator end() { return CBackward_edge_iterator(top_g, visit_sub); }
};

class Forward_edge_iterator {
public:

private:
protected:
  LGraph        *top_g;
  const bool     visit_sub;
  Frontier_type  frontier;  // 2G inputs at most
  Node_set_type  pending;   // vertex that cleared the frontier
  Node_set_type  global_visited;
  Index_ID       hardcoded_nid;

public:
  Forward_edge_iterator() = delete;
  explicit Forward_edge_iterator(LGraph *_g, bool _visit_sub) : top_g(_g), visit_sub(_visit_sub) {
    hardcoded_nid = Node::Hardcoded_output_nid;
  }

  CForward_edge_iterator begin() {
    if (top_g->empty())
      return end();

    CForward_edge_iterator it2(top_g, visit_sub, &frontier, &pending, &hardcoded_nid, &global_visited);

    return it2;
  }

  CForward_edge_iterator end() { return CForward_edge_iterator(top_g, visit_sub); }
};

