//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>

#include "pass.hpp"
#include "lnast.hpp"
#include "mmap_tree.hpp"
#include "firrtl.pb.h"

class Inou_firrtl : public Pass {
protected:

  // Helper Functions (for handling specific cases)
  void CreateBitwidthAttribute(uint32_t bw, Lnast_nid& parent_node, std::string port_id);
  void HandleMuxAssign(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs_of_asg);
  void CreateConditionNode(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node);
  void CreateConditionNode(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const std::string tail);

  // Deconstructing Protobuf Hierarchy
  void ListTypeInfo(const firrtl::FirrtlPB_Type& type, Lnast_nid& parent_node, std::string port_id);
  void ListPortInfo(const firrtl::FirrtlPB_Port& port, Lnast_nid parent_node);

  void PrintPrimOp(const firrtl::FirrtlPB_Expression_PrimOp& op, const std::string symbol, Lnast_nid& parent_node);
  void ListPrimOpInfo(const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node);
  void ListExprInfo(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node);
  void ListExprInfo(const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string tail);



  void ListStatementInfo(const firrtl::FirrtlPB_Statement& stmt, Lnast_nid& parent_node);

  Lnast ListUserModuleInfo(const firrtl::FirrtlPB_Module& module);
  Lnast ListModuleInfo(const firrtl::FirrtlPB_Module& module);
  void IterateModules(const firrtl::FirrtlPB_Circuit& circuit);
  void IterateCircuits(const firrtl::FirrtlPB& firrtl_input);

  static void toLNAST(Eprp_var &var);

private:
  Lnast lnast;
  std::vector<Lnast> lnast_vec;

public:
  Inou_firrtl(const Eprp_var &var);

  static void setup();

  Lnast *ref_lnast() { return &lnast; };//FIXME: Temporary workaround for graphviz to work (only works for 1 module)
};
