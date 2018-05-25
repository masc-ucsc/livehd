#ifndef DUMP_YOSYS_H_
#define DUMP_YOSYS_H_

#include "inou.hpp"
#include "lgraph.hpp"

#include "kernel/rtlil.h"
#include "kernel/yosys.h"

USING_YOSYS_NAMESPACE
//PRIVATE_NAMESPACE_BEGIN

class Dump_yosys : public Inou_trivial {
private:
  RTLIL::Design *design;
  RTLIL::Wire *  get_wire(Index_ID idx, Port_ID pid, bool can_fail);

  void to_yosys(const LGraph *g);

  std::map<Index_ID, RTLIL::Wire *>                     input_map;
  std::map<Index_ID, RTLIL::Wire *>                     output_map;
  std::map<std::pair<Index_ID, Port_ID>, RTLIL::Wire *> cell_output_map;
  std::map<Index_ID, std::vector<RTLIL::SigChunk>>      mem_output_map;

  std::set<LGraph *> _subgraphs;

  uint64_t ids = 0;

  bool hierarchy;

  RTLIL::IdString next_id() {
    return RTLIL::IdString("\\lgraph_id_" + std::to_string(ids++));
  }

  //FIXME: any way of merging these two?
  typedef RTLIL::Cell *(RTLIL::Module::*add_cell_fnc_sign)(RTLIL::IdString, RTLIL::SigSpec, RTLIL::SigSpec, RTLIL::SigSpec, bool, const std::string &);
  typedef RTLIL::Cell *(RTLIL::Module::*add_cell_fnc)(RTLIL::IdString, RTLIL::SigSpec, RTLIL::SigSpec, RTLIL::SigSpec, const std::string &);

  RTLIL::Wire *create_tree(const LGraph *g, std::vector<RTLIL::Wire *> &wires, RTLIL::Module *mod,
                           add_cell_fnc add_cell, RTLIL::Wire *result_wire);

  RTLIL::Wire *create_tree(const LGraph *g, std::vector<RTLIL::Wire *> &wires, RTLIL::Module *mod,
                           add_cell_fnc_sign add_cell, bool sign, RTLIL::Wire *result_wire);

  RTLIL::Wire *create_wire(const LGraph* g, const Index_ID idx, RTLIL::Module* module, bool input, bool output);
protected:
public:
  Dump_yosys(RTLIL::Design *design, Options_pack opt, bool hier = false) : Inou_trivial(opt), design(design) {
    hierarchy = hier;
  };

  void generate(std::vector<const LGraph *> &out) final {
    for(const auto &g : out) {
      std::cout << g->get_name() << std::endl;
      to_yosys(g);
    }
  }

  void generate(std::vector<LGraph *> out) {
    for(auto &g : out) {
      std::cout << g->get_name() << std::endl;
      to_yosys(const_cast<LGraph *>(g));
    }
  }

  using Inou_trivial::generate;

  const std::set<LGraph *> subgraphs() const {
    return _subgraphs;
  }
};

//PRIVATE_NAMESPACE_END

#endif
