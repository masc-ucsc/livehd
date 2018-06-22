//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LGRAPHBASE_CORE_H
#define LGRAPHBASE_CORE_H

#include "dense.hpp"
#include "lgedge.hpp"

class Fast_edge_iterator;
class Forward_edge_iterator;
class Backward_edge_iterator;

class Lgraph_base_core {
protected:
  Dense<Node_Internal> node_internal;

  Lgraph_base_core() = delete;
  explicit Lgraph_base_core(const std::string &path, const std::string &name)
      : node_internal(path + "/" + name + "_nodes"){};
  virtual ~Lgraph_base_core(){};

  Index_ID fast_next(Index_ID nid) const {
    while(true) {
      nid++;
      if(nid >= static_cast<Index_ID>(node_internal.size()))
        return 0;
      if(node_internal[nid].is_master_root())
        return nid;
      assert(!node_internal[nid].is_master_root());
    }

    return 0;
  }

  friend Fast_edge_iterator;

public:
  Fast_edge_iterator fast() const;
};

#endif
