//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 12/18/17.
//

#pragma once

#include <string>

#include "pass.hpp"
#include "rapidjson/document.h"
#include "lgraph.hpp"

class Inou_json : public Pass {
private:
protected:
  //absl::flat_hash_map<int, Node> json_remap;
  absl::flat_hash_map<int, Node::Compact_class> json_remap;
  // for (auto ent: json_remap) {
  //   auto node = Node(g, ent.second);
  // }

  bool is_const_op(const std::string &s) const;
  bool is_int(const std::string &s) const;

  void from_json(LGraph *g, rapidjson::Document &document);
  void to_json(LGraph *lg, const std::string &filename) const;

  static void tolg(Eprp_var &var);
  static void fromlg(Eprp_var &var);

public:
  Inou_json(const Eprp_var &var);

  static void setup();
};

// Internal class used by inou_json.cpp
using PrettySbuffWriter = rapidjson::PrettyWriter<rapidjson::StringBuffer>;
class Inou_Tojson {
  using IPair = std::pair<uint32_t, uint32_t>;
  using Cells = std::vector<Node::Compact_class>;
  private:
    LGraph *toplg;
    PrettySbuffWriter &writer;
    absl::flat_hash_map<Node_pin::Compact_class, IPair> indices;
    absl::flat_hash_map<Node_pin::Compact_class, uint32_t> pick_cache;
    ssize_t next_idx;
    ///
    void reset_indices() {
      pick_cache.clear();
      indices.clear();
      next_idx = 2;
    }
    // ipair.first: start idx, ipair.second: # of bits starting at start idx
    //    -> if ipair.second == 0, then we've started the recursion
    // precondition: call writer.StartArray() before calling this func
    // postcondition: call writer.EndArray()
    int backtrace_sink_pin(const Node_pin &spin, IPair ipair = {0, 0});
    // same rules as above apply to this function:
    int backtrace_edge(const XEdge &edge, IPair ipair = {0, 0});
    int backtrace_sink_pin_wrapper(const Node_pin &spin) {
      writer.StartArray();
      int retval = backtrace_sink_pin(spin);
      writer.EndArray();
      return retval;
    }
    int get_ports(LGraph *lg);
    int write_cells(LGraph *lg, const Cells &cells);
    int write_netnames(LGraph *lg);
  public:
    Inou_Tojson(LGraph *toplg_, PrettySbuffWriter &writer_): toplg(toplg_), writer(writer_) {}
    int dump_graph(Lg_type_id lgid);
};
