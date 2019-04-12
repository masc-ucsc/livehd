//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef DUMP_YOSYS_H_
#define DUMP_YOSYS_H_

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/substitute.h"

#include "pass.hpp"
#include "inou.hpp"
#include "lgraph.hpp"
#include "annotate.hpp"

#include "kernel/rtlil.h"
#include "kernel/yosys.h"

USING_YOSYS_NAMESPACE
// PRIVATE_NAMESPACE_BEGIN

class Lgyosys_dump : public Inou {
private:
  RTLIL::Design *design;
  RTLIL::Wire *get_wire(const Node_pin &pin);
  RTLIL::Wire *add_wire(RTLIL::Module *module, const Node_pin &pin);

  void to_yosys(LGraph *g);

  absl::flat_hash_set<std::string_view>                                 created_sub;
  absl::flat_hash_map<Node_pin::Compact, RTLIL::Wire *               >  input_map;
  absl::flat_hash_map<Node_pin::Compact, RTLIL::Wire *               >  output_map;
  absl::flat_hash_map<Node_pin::Compact, RTLIL::Wire *               >  cell_output_map;
  absl::flat_hash_map<Node::Compact    , std::vector<RTLIL::SigChunk>>  mem_output_map;

  std::set<LGraph *> _subgraphs;

  uint64_t ids        = 0;

  bool hierarchy;

  std::string unique_name(LGraph *g, const std::string &test) {
    std::string tmp;
    assert(test.size()>=1);
    if(test[0] == '\\')
      tmp = &test[1];
    else
      tmp = test;

    while(true) {

      tmp = absl::StrCat(test, "_", std::to_string(ids++));

      if (Ann_node_pin_name::find(g,tmp).is_invalid())
        if (Ann_node_name::find(g,tmp).is_invalid())
          return tmp;
    }
  }

  RTLIL::IdString next_id(LGraph *lg) {
    return RTLIL::IdString(absl::StrCat("\\",unique_name(lg,"lg")));
  }

  // FIXME: any way of merging these two?
  typedef RTLIL::Cell *(RTLIL::Module::*add_cell_fnc_sign)(RTLIL::IdString, RTLIL::SigSpec, RTLIL::SigSpec, RTLIL::SigSpec, bool,
                                                           const std::string &);
  typedef RTLIL::Cell *(RTLIL::Module::*add_cell_fnc)(RTLIL::IdString, RTLIL::SigSpec, RTLIL::SigSpec, RTLIL::SigSpec,
                                                      const std::string &);

  RTLIL::Wire *create_tree(LGraph *g, std::vector<RTLIL::Wire *> &wires, RTLIL::Module *mod, add_cell_fnc_sign add_cell, bool sign, RTLIL::Wire *result_wire);

  RTLIL::Wire *create_io_wire(Node_pin &pin, RTLIL::Module *module);
  void         create_wires(LGraph *g, RTLIL::Module *module);

  void create_blackbox(LGraph *subgraph, RTLIL::Design *design);
  void create_subgraph_outputs(LGraph *g, RTLIL::Module *module, Node &node);
  void create_subgraph(LGraph *g, RTLIL::Module *module, Node &node);
  void create_memory(LGraph *g, RTLIL::Module *module, Node &node);

protected:
public:
  Lgyosys_dump(RTLIL::Design *design, bool hier = false)
      : design(design) {
    hierarchy = hier;
  };

  void fromlg(std::vector<LGraph *> &out) final {
    for(const auto &g : out) {
      if(!g) {
        ::Pass::warn("null lgraph (ignoring)");
        continue;
      }
      std::cout << g->get_name() << std::endl;
      to_yosys(g);
    }
  }

  std::vector<LGraph *> tolg() final {
    assert(false);
    std::vector<LGraph *> empty;
    return empty; // to avoid warning
  };

  /*
  void generate(std::vector<LGraph *> out) {
    for(auto &g : out) {
      std::cout << g->get_name() << std::endl;
      to_yosys(const_cast<LGraph *>(g));
    }
  }*/

  const std::set<LGraph *> &subgraphs() const {
    return _subgraphs;
  }

  void set(const std::string &key, const std::string &value) final {
    assert(false); // No main_api interface for this module as it uses yosys plug-ins
  };
};

// PRIVATE_NAMESPACE_END

#endif
