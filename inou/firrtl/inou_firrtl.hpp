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
  void CreateBitwidthAttribute(Lnast &lnast, uint32_t bw, Lnast_nid& parent_node, std::string port_id);
  void HandleMuxAssign(Lnast &lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs_of_asg);
  void HandleValidIfAssign(Lnast &lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs_of_asg);
  void CreateConditionNode(Lnast &lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node);
  void CreateConditionNode(Lnast &lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, const std::string tail);
  void HandleNEQOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs);
  void HandleUnaryOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs);
  void HandleNegateOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs);
  void HandleExtractBitsOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs);
  void HandleHeadOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs);
  void HandleTailOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs);
  void HandlePadOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs);

  // Deconstructing Protobuf Hierarchy
  void ListTypeInfo(Lnast &lnast, const firrtl::FirrtlPB_Type& type, Lnast_nid& parent_node, std::string port_id);
  void ListPortInfo(Lnast &lnast, const firrtl::FirrtlPB_Port& port, Lnast_nid parent_node);

  void PrintPrimOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, const std::string symbol, Lnast_nid& parent_node);
  void ListPrimOpInfo(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp& op, Lnast_nid& parent_node, std::string lhs);
  void AttachExprToOperator(Lnast &lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node);
  void AttachExprToOperator(Lnast &lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string tail);
  void InitialExprAdd(Lnast &lnast, const firrtl::FirrtlPB_Expression& expr, Lnast_nid& parent_node, std::string lhs, std::string tail);
  std::string ReturnExprString(const firrtl::FirrtlPB_Expression& expr);


  void ListStatementInfo(Lnast &lnast, const firrtl::FirrtlPB_Statement& stmt, Lnast_nid& parent_node);

  void ListUserModuleInfo(Eprp_var &var, const firrtl::FirrtlPB_Module& module);
  void ListModuleInfo(Eprp_var &var, const firrtl::FirrtlPB_Module& module);
  void IterateModules(Eprp_var &var, const firrtl::FirrtlPB_Circuit& circuit);
  void IterateCircuits(Eprp_var &var, const firrtl::FirrtlPB& firrtl_input);

  static void toLNAST(Eprp_var &var);

private:
  //std::shared_ptr<Lnast> lnast;
  //std::vector<Lnast> lnast_vec;

  uint32_t id_counter;

public:
  Inou_firrtl(const Eprp_var &var);

  static void setup();

  //std::shared_ptr<Lnast> ref_lnast() { return lnast; };//FIXME: Temporary workaround for graphviz to work (only works for 1 module)
};
