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
  class Setup_path {
  private:
    static std::string last_path; // Just try to optimize to avoid too many frequent syscalls

  public:
    Setup_path(const std::string &path);
  };
  Setup_path p; // Must be first in base object
  const std::string path;
  const std::string name;
  const std::string long_name;

  Dense<Node_Internal> node_internal;

  Lgraph_base_core() = delete;
  explicit Lgraph_base_core(const std::string &_path, const std::string &_name);
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

  static bool is_path_ok(const std::string &path);

public:
  const std::string &get_name() const {
    assert(long_name == "lgraph_" + name);
    return name;
  }
  const std::string &get_path() const {
    return path;
  }

  Fast_edge_iterator fast() const;
};

#endif
