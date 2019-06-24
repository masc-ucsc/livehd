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

class Fast_edge_iterator {
public:

private:
protected:
  LGraph                *top_g;
  LGraph                *current_g;
  const Hierarchy_index  it_hidx;
  const Index_ID         it_nid;
  const bool             visit_sub;

public:
  Fast_edge_iterator() = delete;
  explicit Fast_edge_iterator(LGraph *_g, LGraph *_cg, const Hierarchy_index &_hidx, const Index_ID _nid, bool _visit_sub) : top_g(_g), current_g(_cg), it_hidx(_hidx), it_nid(_nid), visit_sub(_visit_sub) {}

  CFast_edge_iterator begin() const { return CFast_edge_iterator(top_g, current_g, it_hidx, it_nid, visit_sub); }
  CFast_edge_iterator end() const { return CFast_edge_iterator(top_g, top_g, top_g->hierarchy_root(), 0, visit_sub); }  // 0 is end index for iterator
};

using Frontier_type = absl::flat_hash_map<Node::Compact, int32_t>;
using Node_set_type = absl::flat_hash_set<Node::Compact>;

class Edge_raw_iterator_base {
protected:
  LGraph           *top_g;
  LGraph           *current_g;
  Hierarchy_index   hidx;
  Index_ID          nid;
  const bool        visit_sub;

  Frontier_type  *frontier;  // 2G inputs at most
  Node_set_type  *pending;   // vertex that cleared the frontier
  Index_ID       *hardcoded_nid;

  Node_set_type   global_visited;

  void find_dce_nodes();

public:
  Edge_raw_iterator_base(LGraph *_g, LGraph *_cg, const Hierarchy_index &_hidx, Index_ID _nid, bool _visit_sub, Frontier_type *_frontier, Node_set_type *_pending, Index_ID *_hardcoded_nid)
      : top_g(_g), current_g(_cg), hidx(_hidx), nid(_nid), visit_sub(_visit_sub), frontier(_frontier), pending(_pending), hardcoded_nid(_hardcoded_nid) {}

  virtual void    set_current_node_as_visited() = 0;
  Node operator*() const {
    return Node(top_g, current_g, hidx, nid);
  }

  bool update_frontier();

  void set_next_node_to_visit() {
    if (unlikely(pending->empty())) {
      if (!update_frontier()) {
        nid = 0;  // We are done
        // keep hidx
        return;
      }
    }

    I(!pending->empty());
    auto it = pending->begin();
    nid = it->nid;
    hidx = it->hidx;
    I(nid);
    pending->erase(it);
  };
};

class CForward_edge_iterator : public Edge_raw_iterator_base {
public:
  CForward_edge_iterator(LGraph *_g, LGraph *_cg, const Hierarchy_index &_hidx, Index_ID _nid, bool _visit_sub, Frontier_type *_frontier, Node_set_type *_pending, Index_ID *_hardcoded_nid)
      : Edge_raw_iterator_base(_g, _cg, _hidx, _nid, _visit_sub, _frontier, _pending, _hardcoded_nid) {}

  bool operator!=(const CForward_edge_iterator &other) {
    I(top_g == other.top_g);
    I(frontier == other.frontier);
    I(pending == other.pending);
    I(visit_sub == other.visit_sub);

    return nid != 0;
  };

  void set_current_node_as_visited();

  CForward_edge_iterator operator++() {
    I(nid);  // Do not call ++ after end
    CForward_edge_iterator i(top_g, current_g, hidx, nid, visit_sub, frontier, pending, hardcoded_nid);
    set_current_node_as_visited();
    set_next_node_to_visit();
    return i;
  };
};

class Forward_edge_iterator {
public:

private:
protected:
  LGraph        *top_g;
  LGraph        *current_g;
  const bool     visit_sub;
  Frontier_type  frontier;  // 2G inputs at most
  Node_set_type  pending;   // vertex that cleared the frontier
  Index_ID       hardcoded_nid;

public:
  Forward_edge_iterator() = delete;
  explicit Forward_edge_iterator(LGraph *_g, LGraph *_cg, bool _visit_sub) : top_g(_g), current_g(_cg), visit_sub(_visit_sub) {
  }

  CForward_edge_iterator begin();

  CForward_edge_iterator end() { return CForward_edge_iterator(top_g, top_g, top_g->hierarchy_root(), 0, visit_sub, &frontier, &pending, &hardcoded_nid); }  // 0 is end index for iterator
};

class CBackward_edge_iterator : public Edge_raw_iterator_base {
private:
public:
  CBackward_edge_iterator(LGraph *_g, LGraph *_cg, const Hierarchy_index &_hidx, Index_ID _nid, bool _visit_sub, Frontier_type *_frontier, Node_set_type *_pending, Index_ID *_hardcoded_nid)
    : Edge_raw_iterator_base(_g, _cg, _hidx, _nid, _visit_sub, _frontier, _pending, _hardcoded_nid) {
  }

  bool operator!=(const CBackward_edge_iterator &other) {
    I(top_g == other.top_g);
    I(frontier == other.frontier);
    I(pending == other.pending);

    return nid != other.nid || hidx != other.hidx;
  };

  // find nodes not connected to output that are preventing the propagation
  // only use in case the backward fails
  void set_current_node_as_visited();

  CBackward_edge_iterator operator++() {
    I(nid);  // Do not call ++ after end
    CBackward_edge_iterator i(top_g, current_g, hidx, nid, visit_sub, frontier, pending, hardcoded_nid);
    set_current_node_as_visited();
    set_next_node_to_visit();
    return i;
  };
};

class Backward_edge_iterator {
public:

private:
protected:
  LGraph        *top_g;
  LGraph        *current_g;
  const bool     visit_sub;
  Frontier_type  frontier;  // 2G inputs at most
  Node_set_type  pending;   // vertex that cleared the frontier
  Index_ID       hardcoded_nid;

public:
  Backward_edge_iterator() = delete;
  explicit Backward_edge_iterator(LGraph *_g, LGraph *_cg, bool _visit_sub) : top_g(_g), current_g(_cg), visit_sub(_visit_sub) {
  }

  CBackward_edge_iterator begin();

  CBackward_edge_iterator end() { return CBackward_edge_iterator(top_g, top_g, top_g->hierarchy_root(), 0, visit_sub, &frontier, &pending, &hardcoded_nid); }  // 0 is end index for iterator
};
