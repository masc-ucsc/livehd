//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

#include "lgset.hpp"
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
  CFast_edge_iterator(LGraph *_g, Hierarchy_id _hid, const Index_ID _nid, bool _visit_sub) : top_g(_g), hid(_hid), nid(_nid), visit_sub(_visit_sub) {}
  CFast_edge_iterator operator++();
  bool operator!=(const CFast_edge_iterator &other) {
    I(top_g == other.top_g);
    return nid != other.nid;
  }
  Node operator*() const {
    return Node(top_g, hid, nid);
  }

private:
  std::vector<Node::Compact> h_stack;
  LGraph        *top_g;
  Hierarchy_id   hid;
  Index_ID       nid;
  bool           visit_sub;
};

class Fast_edge_iterator {
public:

private:
protected:
  LGraph             *top_g;
  const Hierarchy_id  it_hid;
  const Index_ID      it_nid;
  const bool          visit_sub;

public:
  Fast_edge_iterator() = delete;
  explicit Fast_edge_iterator(LGraph *_g, const Hierarchy_id _hid, const Index_ID _nid, bool _visit_sub) : top_g(_g), it_hid(_hid), it_nid(_nid), visit_sub(_visit_sub) {}

  CFast_edge_iterator begin() const { return CFast_edge_iterator(top_g, it_hid, it_nid, visit_sub); }
  CFast_edge_iterator end() const { return CFast_edge_iterator(top_g, 0, 0, visit_sub); }  // 0 is end index for iterator
};

using Frontier_type = absl::flat_hash_map<Node::Compact, int32_t>;

class Edge_raw_iterator_base {
protected:
  LGraph        *top_g;
  Hierarchy_id   hid;
  Index_ID       nid;

  Frontier_type *frontier;  // 2G inputs at most
  Node_set      *pending;   // vertex that cleared the frontier
  Index_ID      *hardcoded_nid;

public:
  Edge_raw_iterator_base(LGraph *_g, Hierarchy_id _hid, Index_ID _nid, Frontier_type *_frontier, Node_set *_pending, Index_ID *_hardcoded_nid)
      : top_g(_g), hid(_hid), nid(_nid), frontier(_frontier), pending(_pending), hardcoded_nid(_hardcoded_nid) {}

  virtual void    set_current_node_as_visited() = 0;
  Node operator*() const {
    return Node(top_g, hid, nid);
  }

  bool update_frontier();

  void set_next_node_to_visit() {
    if (likely(!pending->empty())) {
      const auto it = pending->begin();
      nid = it->nid;
      hid = it->hid;
      return;
    }

    if (!update_frontier()) {
      nid = 0;  // We are done
      // keep hid
      return;
    }

    I(!pending->empty());
    auto it2 = pending->begin();
    nid = it2->nid;
    hid = it2->hid;
    I(nid);
  };
};

class CForward_edge_iterator : public Edge_raw_iterator_base {
public:
  CForward_edge_iterator(LGraph *_g, Hierarchy_id _hid, Index_ID _nid, Frontier_type *_frontier, Node_set *_pending, Index_ID *_hardcoded_nid)
      : Edge_raw_iterator_base(_g, _hid, _nid, _frontier, _pending, _hardcoded_nid) {}

  bool operator!=(const CForward_edge_iterator &other) {
    I(top_g == other.top_g);
    I(frontier == other.frontier);
    I(pending == other.pending);

    return nid != 0;
  };

  void set_current_node_as_visited();

  CForward_edge_iterator operator++() {
    I(nid);  // Do not call ++ after end
    CForward_edge_iterator i(top_g, hid, nid, frontier, pending, hardcoded_nid);
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
  Frontier_type  frontier;  // 2G inputs at most
  Node_set       pending;   // vertex that cleared the frontier
  Index_ID       hardcoded_nid;

public:
  Forward_edge_iterator() = delete;
  explicit Forward_edge_iterator(LGraph *_g) : top_g(_g) {
  }

  CForward_edge_iterator begin();

  CForward_edge_iterator end() { return CForward_edge_iterator(top_g, 0, 0, &frontier, &pending, &hardcoded_nid); }  // 0 is end index for iterator
};

class CBackward_edge_iterator : public Edge_raw_iterator_base {
private:
  Node_set back_iter_global_visited;

public:
  CBackward_edge_iterator(LGraph *_g, Hierarchy_id _hid, Index_ID _nid, Frontier_type *_frontier, Node_set *_pending, Index_ID *_hardcoded_nid)
    : Edge_raw_iterator_base(_g, _hid, _nid, _frontier, _pending, _hardcoded_nid) {
  }

  bool operator!=(const CBackward_edge_iterator &other) {
    I(top_g == other.top_g);
    I(frontier == other.frontier);
    I(pending == other.pending);

    return nid != other.nid || hid != other.hid;
  };

  // find nodes not connected to output that are preventing the propagation
  // only use in case the backward fails
  void find_dce_nodes();

  void set_current_node_as_visited();

  CBackward_edge_iterator operator++() {
    I(nid);  // Do not call ++ after end
    CBackward_edge_iterator i(top_g, hid, nid, frontier, pending, hardcoded_nid);
    back_iter_global_visited.set(Node::Compact(hid, nid));
    set_current_node_as_visited();
    if (pending->empty()) {
      find_dce_nodes();
    }
    set_next_node_to_visit();
    return i;
  };
};

class Backward_edge_iterator {
public:

private:
protected:
  LGraph        *top_g;
  Frontier_type  frontier;  // 2G inputs at most
  Node_set       pending;   // vertex that cleared the frontier
  Index_ID       hardcoded_nid;

public:
  Backward_edge_iterator() = delete;
  explicit Backward_edge_iterator(LGraph *_g) : top_g(_g) {
  }

  CBackward_edge_iterator begin();

  CBackward_edge_iterator end() { return CBackward_edge_iterator(top_g, 0, 0, &frontier, &pending, &hardcoded_nid); }  // 0 is end index for iterator
};
