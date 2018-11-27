//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef DUMP_YOSYS_H_
#define DUMP_YOSYS_H_

#include "inou.hpp"
#include "lgraph.hpp"

#include "kernel/rtlil.h"
#include "kernel/yosys.h"

USING_YOSYS_NAMESPACE
// PRIVATE_NAMESPACE_BEGIN

class Lgyosys_dump : public Inou {
private:
  RTLIL::Design *design;
  RTLIL::Wire *  get_wire(Index_ID idx, Port_ID pid, bool can_fail);

  void to_yosys(const LGraph *g);

  std::map<Index_ID, RTLIL::Wire *>                     input_map;
  std::map<Index_ID, RTLIL::Wire *>                     output_map;
  std::map<std::pair<Index_ID, Port_ID>, RTLIL::Wire *> cell_output_map;
  std::map<Index_ID, std::vector<RTLIL::SigChunk>>      mem_output_map;

  std::set<const LGraph *> _subgraphs;

  uint64_t ids        = 0;
  uint64_t spare_wire = 0;

  bool hierarchy;

  RTLIL::IdString next_id() {
    return RTLIL::IdString("\\lgraph_id_" + std::to_string(ids++));
  }

  std::string next_wire() {
    return ("lgraph_spare_wire_" + std::to_string(spare_wire++));
  }

  // FIXME: any way of merging these two?
  typedef RTLIL::Cell *(RTLIL::Module::*add_cell_fnc_sign)(RTLIL::IdString, RTLIL::SigSpec, RTLIL::SigSpec, RTLIL::SigSpec, bool,
                                                           const std::string &);
  typedef RTLIL::Cell *(RTLIL::Module::*add_cell_fnc)(RTLIL::IdString, RTLIL::SigSpec, RTLIL::SigSpec, RTLIL::SigSpec,
                                                      const std::string &);

  RTLIL::Wire *create_tree(const LGraph *g, std::vector<RTLIL::Wire *> &wires, RTLIL::Module *mod, add_cell_fnc add_cell,
                           RTLIL::Wire *result_wire);

  RTLIL::Wire *create_tree(const LGraph *g, std::vector<RTLIL::Wire *> &wires, RTLIL::Module *mod, add_cell_fnc_sign add_cell,
                           bool sign, RTLIL::Wire *result_wire);

  RTLIL::Wire *create_wire(const LGraph *g, const Index_ID idx, RTLIL::Module *module, bool input, bool output);
  void         create_wires(const LGraph *g, RTLIL::Module *module);

  void create_blackbox(const LGraph &subgraph, RTLIL::Design *design);
  void create_subgraph_outputs(const LGraph *g, RTLIL::Module *module, Index_ID idx);
  void create_subgraph(const LGraph *g, RTLIL::Module *module, Index_ID idx);
  void create_memory(const LGraph *g, RTLIL::Module *module, Index_ID idx);

protected:
public:
  Lgyosys_dump(RTLIL::Design *design, bool hier = false)
      : design(design) {
    hierarchy = hier;
  };

  void fromlg(std::vector<const LGraph *> &out) final {
    for(const auto &g : out) {
      if(!g) {
        console->warn("lgraph not found\n");
        continue;
      }
      std::cout << g->get_name() << std::endl;
      to_yosys(g);
    }
  }

  std::vector<LGraph *> tolg() final {
    assert(false);
  };

  /*
  void generate(std::vector<LGraph *> out) {
    for(auto &g : out) {
      std::cout << g->get_name() << std::endl;
      to_yosys(const_cast<LGraph *>(g));
    }
  }*/

  const std::set<const LGraph *> subgraphs() const {
    return _subgraphs;
  }

  void set(const std::string &key, const std::string &value) final {
    assert(false); // No main_api interface for this module as it uses yosys plug-ins
  };
};

// PRIVATE_NAMESPACE_END

#endif
