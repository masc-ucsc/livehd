//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>

#include "pass.hpp"
#include "lnast.hpp"
#include "firrtl.pb.h"

class Inou_firrtl : public Pass {
protected:
  void ListTypeInfo(const firrtl::FirrtlPB_Type& type);
  void ListPortInfo(const firrtl::FirrtlPB_Port& port);
  //void IteratePrimOpExpr(const firrtl::FirrtlPB_Expression_PrimOp& op);
  void PrintPrimOp(const firrtl::FirrtlPB_Expression_PrimOp& op, const std::string symbol);
  void ListPrimOpInfo(const firrtl::FirrtlPB_Expression_PrimOp& op);
  void ListExprInfo(const firrtl::FirrtlPB_Expression& expr);
  void ListStatementInfo(const firrtl::FirrtlPB_Statement& stmt);
  void ListUserModuleInfo(const firrtl::FirrtlPB_Module& module);
  void ListModuleInfo(const firrtl::FirrtlPB_Module& module);
  void IterateModules(const firrtl::FirrtlPB_Circuit& circuit);
  void IterateCircuits(const firrtl::FirrtlPB& firrtl_input);

  static void toLNAST(Eprp_var &var);

public:
  Inou_firrtl(const Eprp_var &var);

  static void setup();
};
