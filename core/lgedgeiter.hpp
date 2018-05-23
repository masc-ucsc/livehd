#ifndef LGEDGEITER_H
#define LGEDGEITER_H

#include <stdint.h>

#include <iostream>
#include <vector>

#include "sparsehash/dense_hash_map"

#include "lgraph.hpp"
#include "lgraphbase.hpp"

class Edge_iterator {
public:
  class CPod_iterator {
  public:
    CPod_iterator(const Edge *_ptr, const Edge *_e, bool _inputs) : ptr(_ptr), e(_e), inputs(_inputs) {}
    CPod_iterator operator++();
    CPod_iterator operator--() {
      CPod_iterator i(ptr, e, inputs);
      ptr -= ptr->next_node_inc();
      return i;
    }
    bool        operator!=(const CPod_iterator &other) { return ptr != other.ptr; }
    const Edge &operator*() const { return *ptr; }

  private:
    const Edge *ptr;
    const Edge *e;
    const bool  inputs;
  };

private:
protected:
  const bool  inputs;
  const Edge *b;
  const Edge *e;

public:
  Edge_iterator() = delete;
  explicit Edge_iterator(const Edge *_b, const Edge *_e, bool _inputs)
      : inputs(_inputs) {
    b = _b;
    e = _e;
  }

  CPod_iterator begin() const { return CPod_iterator(b, e, inputs); }
  CPod_iterator end() const { return CPod_iterator(e, e, inputs); }
};

class Fast_edge_iterator {
public:
  class CFast_edge_iterator {
  public:
    CFast_edge_iterator(const Index_ID _nid, const LGraph_Base *_g) : nid(_nid), g(_g) {}
    CFast_edge_iterator operator++() {
      CFast_edge_iterator i(nid, g);

      nid = g->fast_next(nid);

      return i;
    };
    bool operator!=(const CFast_edge_iterator &other) {
      assert(g == other.g);
      return nid != other.nid;
    }
    const Index_ID &operator*() const { return nid; }

  private:
    Index_ID           nid;
    const LGraph_Base *g;
  };

private:
protected:
  const LGraph_Base *g;
  const Index_ID     b;

public:
  Fast_edge_iterator() = delete;
  explicit Fast_edge_iterator(const Index_ID _b, const LGraph_Base *_g)
      : g(_g), b(_b) {
  }

  CFast_edge_iterator begin() const { return CFast_edge_iterator(b, g); }
  CFast_edge_iterator end() const { return CFast_edge_iterator(0, g); } // 0 is end index for iterator
};

typedef google::dense_hash_map<Index_ID, int32_t> Frontier_type;
typedef std::vector<Index_ID>                     Pending_type;
typedef std::set<Index_ID>                        Deadcode_type;

class Edge_iterator_base {
protected:
  Index_ID       nid;
  const LGraph * g;
  Frontier_type *frontier; // 2G inputs at most
  Pending_type * pending;  // vertex that cleared the frontier

public:
  Edge_iterator_base(const Index_ID _nid, const LGraph *_g, Frontier_type *_frontier, Pending_type *_pending)
      : nid(_nid), g(_g), frontier(_frontier), pending(_pending) {
  }

  virtual void    add_node(Index_ID nid) = 0;
  const Index_ID &operator*() const { return nid; }

  bool check_frontier() {
    bool pushed = false;
    for(auto &it : *frontier) {
      if(it.second > 0) {
        if(g->node_type_get(it.first).is_pipelined()) {
          pending->push_back(it.first);
          it.second = -1; // Mark as pipelined, but keep not to visit twice
          pushed    = true;
        }
      }
    }
    if(!pushed) {
      return false;
    }
    return true;
  }

  void set_next_nid() {
    if(pending->empty())
      if(!check_frontier()) {
        nid = 0; // We are done
        return;
      }

#if DEBUG
    // Benchmark pending and frontier sizes
    static size_t p_max_size = 0;
    static size_t f_max_size = 0;
    if(pending->size() > (2 * p_max_size)) {
      fmt::print("pending {} {}\n", pending->size(), ((double)pending->size()) / g->size());
      p_max_size = pending->size();
    }
    if(frontier->size() > (2 * f_max_size)) {
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
  class CForward_edge_iterator : public Edge_iterator_base {
  public:
    CForward_edge_iterator(const Index_ID _nid, const LGraph *_g, Frontier_type *_frontier, Pending_type *_pending)
        : Edge_iterator_base(_nid, _g, _frontier, _pending) {
    }

    bool operator!=(const CForward_edge_iterator &other) {
      assert(g == other.g);
      assert(frontier == other.frontier);
      assert(pending == other.pending);

      return nid != 0;
    };

    void add_node(Index_ID idx) {
      assert(g->get_node_int(idx).is_master_root());

      for(const auto &c : g->out_edges(idx)) {
        const auto &dst_node        = g->get_node_int(c.get_idx());
        Index_ID    master_root_nid = dst_node.get_master_root_nid();

        Frontier_type::iterator fit = frontier->find(master_root_nid);

        if(fit == frontier->end()) {
          int32_t ninputs = g->get_node_int(master_root_nid).get_num_inputs() - 1;
          assert(ninputs >= 0);
          if(ninputs == 0) { // Done already
            pending->push_back(master_root_nid);
          } else {
            (*frontier)[master_root_nid] = ninputs;
          }
        } else {
          int ninputs = (fit->second) - 1;
          if(ninputs == 0) { // Done
            pending->push_back(master_root_nid);
            frontier->erase(fit);
          } else {
            fit->second = ninputs;
          }
        }
      }
    }

    CForward_edge_iterator operator++() {
      assert(nid); // Do not call ++ after end
      CForward_edge_iterator i(nid, g, frontier, pending);
      add_node(nid);
      set_next_nid();
      return i;
    };
  };

private:
protected:
  const LGraph *g;
  Frontier_type frontier; // 2G inputs at most
  Pending_type  pending;  // vertex that cleared the frontier

public:
  Forward_edge_iterator() = delete;
  explicit Forward_edge_iterator(const LGraph *_g)
      : g(_g) {
    frontier.set_empty_key(0);     // 0 is not allowed as key
    frontier.set_deleted_key(128); // 128 is not allowed as key (4KB aligned)
    //frontier.resize(32+g->size()/16);
    //pending.reserve(32+g->size()/128);
  }

  CForward_edge_iterator begin() {
    for(const auto &it : g->inputs2node) {
      pending.push_back(it.second.nid);
    }
    // FIXME: output insertion should be moved to nid==0 (otherwise, and output with some logic but
    // still disconnected would not be generated)

    for(const auto &it : g->outputs2node) {
      if(!g->get_node_int(it.second.nid).has_inputs())
        pending.push_back(it.second.nid);
    }

    //for forward iteration we want to start from constants as well
    const LGraph *lgr       = dynamic_cast<const LGraph *>(g);
    const auto &  constants = lgr->get_const_node_ids();
    Index_ID      cid       = constants.get_first();
    while(cid) {
      assert(cid);
      pending.push_back(cid);
      cid = constants.get_next(cid);
    }

    Index_ID b = 0;
    if(!pending.empty()) {
      b = pending.back();
      pending.pop_back();
    }

    CForward_edge_iterator it(b, g, &frontier, &pending);

    return it;
  }
  CForward_edge_iterator end() { return CForward_edge_iterator(0, g, &frontier, &pending); } // 0 is end index for iterator
};

class Backward_edge_iterator {
public:
  class CBackward_edge_iterator : public Edge_iterator_base {
  public:
    CBackward_edge_iterator(const Index_ID _nid, const LGraph *_g, Frontier_type *_frontier, Pending_type *_pending)
        : Edge_iterator_base(_nid, _g, _frontier, _pending) {
    }

    bool operator!=(const CBackward_edge_iterator &other) {
      assert(g == other.g);
      assert(frontier == other.frontier);
      assert(pending == other.pending);

      return nid != 0;
    };


    //find nodes not connected to output that are preventing the propagation
    //only use in case the backward fails
    Deadcode_type find_dce_nodes(Index_ID idx) {
      Pending_type discovered;
      Pending_type visited;

      floating.insert(idx);
      discovered.push_back(idx);
      while(discovered.size() > 0) {
        Index_ID current = dicovered.back();
        discovered.pop_back();
        visited.insert(current);
        for(const auto& c : g->out_edges(current)) {
          floating.erase(current);

          if(visited.find(c.get_inp_pin().get_nid()) == visited.end()) {
            discovered.push_back(c.get_inp_pin().get_nid());
          }
        }
      }

      return floating;
    }

    bool check_frontier() {
      //this means no pending node, it could either be a loop or a dce like
      //node (ie no output). It may be hard to determine which, let us check for nodes with no outputs

    }

    void add_node(Index_ID idx) {
      assert(g->get_node_int(idx).is_master_root());

      for(const auto &c : g->inp_edges(idx)) {
        const auto &dst_node        = g->get_node_int(c.get_idx());
        Index_ID    master_root_nid = dst_node.get_master_root_nid();

        Frontier_type::iterator fit = frontier->find(master_root_nid);

        if(fit == frontier->end()) {
          int32_t noutputs = g->get_node_int(master_root_nid).get_num_outputs() - 1;
          assert(noutputs >= 0);
          if(noutputs == 0) { // Done already
            assert(false); // is this a real case?
            pending->push_back(master_root_nid);
          } else {
            (*frontier)[master_root_nid] = noutputs;
          }
        } else {
          int noutputs = (fit->second) - 1;
          if(noutputs == 0) { // Done
            pending->push_back(master_root_nid);
            frontier->erase(fit);
          } else {
            fit->second = noutputs;
          }
        }
      }
    }

    CBackward_edge_iterator operator++() {
      assert(nid); // Do not call ++ after end
      CBackward_edge_iterator i(nid, g, frontier, pending);
      add_node(nid);
      set_next_nid();
      return i;
    };
  };

private:
protected:
  const LGraph *g;
  Frontier_type frontier; // 2G inputs at most
  Pending_type  pending;  // vertex that cleared the frontier
  Deadcode_type floating; // nodes without fanout

public:
  Backward_edge_iterator() = delete;
  explicit Backward_edge_iterator(const LGraph *_g)
      : g(_g) {
    frontier.set_empty_key(0);     // 0 is not allowed as key
    frontier.set_deleted_key(128); // 128 is not allowed as key (4KB aligned)
    frontier.resize(128);          // FIXME: do average or gsize ratio
    pending.reserve(128);
  }

  CBackward_edge_iterator begin() {

    // FIXME: This may need to be moved to nid==0. If any input not visited, then add it (but only
    // if full input/output)

    for(const auto &it : g->inputs2node) { // inputs without connection to preserve them
      if(!g->get_node_int(it.second.nid).has_outputs())
        pending.push_back(it.second.nid);
    }
    for(const auto &it : g->outputs2node) {
      pending.push_back(it.second.nid);
    }

    Index_ID b = 0;
    if(!pending.empty()) {
      b = pending.back();
      pending.pop_back();
    }

    CBackward_edge_iterator it(b, g, &frontier, &pending);

    return it;
  }
  CBackward_edge_iterator end() { return CBackward_edge_iterator(0, g, &frontier, &pending); } // 0 is end index for iterator
};

#endif
