//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <set>
#include <vector>

#include "pass.hpp"

#include "sparsehash/dense_hash_map"
#include "sparsehash/sparse_hash_set"

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

class Fast_edge_iterator {
public:
  class CFast_edge_iterator {
  public:
    CFast_edge_iterator(const Index_ID _nid, const LGraph *_g) : nid(_nid), g(_g) {}
    CFast_edge_iterator operator++();
    bool operator!=(const CFast_edge_iterator &other) {
      assert(g == other.g);
      return nid != other.nid;
    }
    const Index_ID &operator*() const { return nid; }

  private:
    Index_ID                nid;
    const LGraph *g;
  };

private:
protected:
  const LGraph *g;
  const Index_ID          b;

public:
  Fast_edge_iterator() = delete;
  explicit Fast_edge_iterator(const Index_ID _b, const LGraph *_g) : g(_g), b(_b) {}

  CFast_edge_iterator begin() const { return CFast_edge_iterator(b, g); }
  CFast_edge_iterator end() const { return CFast_edge_iterator(0, g); }  // 0 is end index for iterator
};

typedef google::dense_hash_map<uint64_t, int32_t> Frontier_type;
typedef std::vector<uint64_t>                     Pending_type;
typedef google::sparse_hash_set<uint64_t>         Deadcode_type;

class Edge_raw_iterator_base {
protected:
  Index_ID           nid;
  const LGraph *g;
  Frontier_type *    frontier;  // 2G inputs at most
  Pending_type *     pending;   // vertex that cleared the frontier

public:
  Edge_raw_iterator_base(const Index_ID _nid, const LGraph *_g, Frontier_type *_frontier, Pending_type *_pending)
      : nid(_nid), g(_g), frontier(_frontier), pending(_pending) {}

  virtual void    add_node(Index_ID nid) = 0;
  const Index_ID &operator*() const { return nid; }

  bool check_frontier();

  void set_next_nid() {
    if (pending->empty())
      if (!check_frontier()) {
        nid = 0;  // We are done
        return;
      }

#if DEBUG
    // Benchmark pending and frontier sizes
    static size_t p_max_size = 0;
    static size_t f_max_size = 0;
    if (pending->size() > (2 * p_max_size)) {
      fmt::print("pending {} {}\n", pending->size(), ((double)pending->size()) / g->size());
      p_max_size = pending->size();
    }
    if (frontier->size() > (2 * f_max_size)) {
      fmt::print("frontier {} {}\n", frontier->size(), ((double)frontier->size()) / g->size());
      f_max_size = frontier->size();
    }
#endif

    assert(!pending->empty());
    nid = pending->back();
    pending->pop_back();
  };
};

class Forward_edge_iterator {
public:
  class CForward_edge_iterator : public Edge_raw_iterator_base {
  public:
    CForward_edge_iterator(const Index_ID _nid, const LGraph *_g, Frontier_type *_frontier, Pending_type *_pending)
        : Edge_raw_iterator_base(_nid, _g, _frontier, _pending) {}

    bool operator!=(const CForward_edge_iterator &other) {
      assert(g == other.g);
      assert(frontier == other.frontier);
      assert(pending == other.pending);

      return nid != 0;
    };

    void add_node(const Index_ID nid);

    CForward_edge_iterator operator++() {
      assert(nid);  // Do not call ++ after end
      CForward_edge_iterator i(nid, g, frontier, pending);
      add_node(nid);
      set_next_nid();
      return i;
    };
  };

private:
protected:
  const LGraph *g;
  Frontier_type      frontier;  // 2G inputs at most
  Pending_type       pending;   // vertex that cleared the frontier

public:
  Forward_edge_iterator() = delete;
  explicit Forward_edge_iterator(const LGraph *_g) : g(_g) {
    frontier.set_empty_key(0);      // 0 is not allowed as key
    frontier.set_deleted_key(128);  // 128 is not allowed as key (4KB aligned)
    // frontier.resize(32+g->size()/16);
    // pending.reserve(32+g->size()/128);
  }

  CForward_edge_iterator begin();

  CForward_edge_iterator end() { return CForward_edge_iterator(0, g, &frontier, &pending); }  // 0 is end index for iterator
};

class Backward_edge_iterator {
public:
  class CBackward_edge_iterator : public Edge_raw_iterator_base {
  private:
    Deadcode_type global_visited;

  public:
    CBackward_edge_iterator(const Index_ID _nid, const LGraph *_g, Frontier_type *_frontier, Pending_type *_pending)
        : Edge_raw_iterator_base(_nid, _g, _frontier, _pending) {}

    bool operator!=(const CBackward_edge_iterator &other) {
      assert(g == other.g);
      assert(frontier == other.frontier);
      assert(pending == other.pending);

      return nid != 0;
    };

    // find nodes not connected to output that are preventing the propagation
    // only use in case the backward fails
    void find_dce_nodes();

    void add_node(const Index_ID nid);

    CBackward_edge_iterator operator++() {
      assert(nid);  // Do not call ++ after end
      CBackward_edge_iterator i(nid, g, frontier, pending);
      global_visited.insert(nid);
      add_node(nid);
      if (pending->empty()) {
        find_dce_nodes();
      }
      set_next_nid();
      return i;
    };
  };

private:
protected:
  const LGraph *g;
  Frontier_type      frontier;  // 2G inputs at most
  Pending_type       pending;   // vertex that cleared the frontier

public:
  Backward_edge_iterator() = delete;
  explicit Backward_edge_iterator(const LGraph *_g) : g(_g) {
    frontier.set_empty_key(0);      // 0 is not allowed as key
    frontier.set_deleted_key(128);  // 128 is not allowed as key (4KB aligned)
    frontier.resize(128);           // FIXME: do average or gsize ratio
    pending.reserve(128);
  }

  CBackward_edge_iterator begin();

  CBackward_edge_iterator end() { return CBackward_edge_iterator(0, g, &frontier, &pending); }  // 0 is end index for iterator
};
