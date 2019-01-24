//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "dense.hpp"
#include "graph_library.hpp"
#include "lgedge.hpp"
#include "tech_library.hpp"

class Fast_edge_iterator;

class Lgraph_base_core {
protected:
  class Setup_path {
  private:
    static std::string last_path;  // Just try to optimize to avoid too many frequent syscalls

  public:
    Setup_path(std::string_view path);
  };
  Setup_path        p;  // Must be first in base object
  const std::string path;
  const std::string name;
  const std::string long_name;
  const Lg_type_id  lgraph_id;

  Dense<Node_Internal> node_internal;

  bool locked;

  // Integrate graph and tech library?
  Graph_library *library;
  Tech_library * tlibrary;

  Lgraph_base_core() = delete;
  explicit Lgraph_base_core(std::string_view _path, std::string_view _name, Lg_type_id lgid);
  virtual ~Lgraph_base_core(){};

  Index_ID fast_next(Index_ID nid) const {
    while (true) {
      nid.value++;
      if (nid >= static_cast<Index_ID>(node_internal.size())) return 0;
      if (!node_internal[nid].is_node_state()) continue;
      if (node_internal[nid].is_master_root()) return nid;
    }

    return 0;
  }

  friend Fast_edge_iterator;

public:
  void get_lock();

  virtual bool close();
  virtual void clear();
  virtual void sync();

  std::string_view get_name() const { return std::string_view(name); }
  const Lg_type_id lg_id() const { return lgraph_id; }

  const std::string &  get_path() const { return path; }
  const Graph_library &get_library() const { return *library; }
  const Tech_library & get_tlibrary() const { return *tlibrary; }
  Tech_library &       get_tech_library() { return *tlibrary; }

  Fast_edge_iterator fast() const;
};
