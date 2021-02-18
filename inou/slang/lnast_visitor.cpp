
#include "lnast_visitor.hpp"

#include <iostream>

#include "fmt/format.h"
#include "iassert.hpp"
#include "slang/symbols/ASTSerializer.h"
#include "slang/symbols/CompilationUnitSymbols.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"

using namespace slang;  // just inside this file

Lnast_visitor::Lnast_visitor(slang::Compilation& _compilation, const size_t& _numErrors, uint32_t _errorLimit)
    : compilation(_compilation), numErrors(_numErrors), errorLimit(_errorLimit) {
}

void Lnast_visitor::handle(const slang::ExplicitImportSymbol& symbol) {
  if (!handleDefault(symbol))
    return;
  symbol.importedSymbol();
}

void Lnast_visitor::handle(const slang::WildcardImportSymbol& symbol) {
  if (!handleDefault(symbol))
    return;
  symbol.getPackage();
}

void Lnast_visitor::handle(const slang::IntegerLiteral& expr) {
  const auto& svint = expr.getValue();
  if (svint.isSingleWord())
    fmt::print("value val:{} bits:{}\n", *svint.getRawPtr(), svint.getActiveBits());
  else
    fmt::print("value bits:{}\n", svint.getActiveBits());
}

void Lnast_visitor::handle(const slang::VariableDeclStatement& stmt) { fmt::print("var decl:{}\n", stmt.symbol.name); }

void Lnast_visitor::handle(const slang::ContinuousAssignSymbol& symbol) {
  if (!handleDefault(symbol))
    return;

  handle(symbol.getAssignment().as<AssignmentExpression>());
}
// void Lnast_visitor::handle(const slang::AssignmentExpression& expr) {
//   if (numErrors > errorLimit)
//     return;
//   const auto &lhs =  expr.left();
//   const auto &rhs =  expr.right();
//   //base cases, traverse recursion for expr.right() checking nested lefts and rights
// }
void Lnast_visitor::addLnast(const mmap_lib::Tree_index& idx_stmts, operators List, int count, int& first, int& tmp_flag, int& not_flag, int& last_op){
  //idk if the flags can carry over properly
  mmap_lib::Tree_index idx_;
  std::string tmpIn;
  auto node_=Lnast_node::create_ref("");
  switch (List){
    case (operators::AND):
      fmt::print("AND \n");
      idx_ = lnast->add_child(idx_stmts, Lnast_node::create_and("AND"));
      tmpIn="__and" + std::to_string(count);
      node_ = Lnast_node::create_ref(lnast->add_string(tmpIn));
      break;

    case (operators::MINUS):
      fmt::print("MINUS \n");
      idx_ = lnast->add_child(idx_stmts, Lnast_node::create_minus("MINUS"));
      tmpIn="__minus" + std::to_string(count);
      node_ = Lnast_node::create_ref(lnast->add_string(tmpIn));
      break;

    case (operators::PLUS):
      fmt::print("PLUS \n");
      idx_ = lnast->add_child(idx_stmts, Lnast_node::create_plus("PLUS"));
      tmpIn="__plus" + std::to_string(count);
      node_ = Lnast_node::create_ref(lnast->add_string(tmpIn));
      break;

    case (operators::OR):
      fmt::print("OR \n");
      idx_ = lnast->add_child(idx_stmts, Lnast_node::create_or("OR"));
      tmpIn="__or" + std::to_string(count);
      node_ = Lnast_node::create_ref(lnast->add_string(tmpIn));
      break;

    case (operators::XOR):
      fmt::print("XOR \n");
      idx_ = lnast->add_child(idx_stmts, Lnast_node::create_xor("XOR"));
      tmpIn="__xor" + std::to_string(count);
      node_ = Lnast_node::create_ref(lnast->add_string(tmpIn));
      break;

    case (operators::NOT):
      fmt::print("NOT \n");
      idx_ = lnast->add_child(idx_stmts, Lnast_node::create_not("NOT"));
      tmpIn="__not" + std::to_string(count);
      node_ = Lnast_node::create_ref(lnast->add_string(tmpIn));
      break;

    default:
      // fmt::print("AND \n");
      // auto idx_ = lnast->add_child(idx_stmts, Lnast_node::create_and("AND"));
      // std::string tmpIn="__and" + std::to_string(count);
      // auto node_ = Lnast_node::create_ref(lnast->add_string(tmpIn));
      break;

  }
  lnast->add_child(idx_, node_);
  if (operandList.size()) {
    if (first)
      tmpList.insert(tmpList.begin(), tmpIn);
    auto node_op1 = Lnast_node::create_ref(lnast->add_string(operandList.back()));
    lnast->add_child(idx_, node_op1);
    operandList.pop_back();
  } else {
    auto node_tmp = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
    tmpList.pop_back();
    lnast->add_child(idx_, node_tmp);
    auto node_tmp1 = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
    tmpList.pop_back();
    lnast->add_child(idx_, node_tmp1);
    tmpList.insert(tmpList.end(), tmpIn);
    return;
  }
  if (tmp_flag || not_flag) {
    auto node_tmp = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
    tmpList.pop_back();
    lnast->add_child(idx_, node_tmp);
    if (not_flag) {
      tmpList.insert(tmpList.end(), tmpIn);  // for connection the node refs
      not_flag = 0;
      tmp_flag = 1;
    } else {
      tmp_flag = 0;
    }
  } else {
    auto node_op2 = Lnast_node::create_ref(lnast->add_string(operandList.back()));
    lnast->add_child(idx_, node_op2);
    operandList.pop_back();
    tmpList.insert(tmpList.end(), tmpIn);  // for connection the node refs
    tmp_flag = 1;
  }
  if (last_op) {
    tmpList.insert(tmpList.end(), tmpIn);  // for connection the node refs
    // lnast->add_child(idx_and, node_and);
  }
  if (not_flag != 1) {
    first = 1;
  }

}
// void addLnast(Lnast_node idx_stmts, operators List, int count, int& tmp_flag, int& not_flag, int& last_op);

void Lnast_visitor::handle(const slang::AssignmentExpression& expr) {
  int line_num = 0, pos1 = 0, pos2 = 0;
  if (numErrors > errorLimit)
    return;
  std::string output = "%";

  auto idx_stmts  = lnast->get_child(lnast->get_root());
  fmt::print("Start RHS recursion\n");
  handle(expr.right());
  // check verilog list
  fmt::print("printing operator recursion\n");
  for (auto it = verilogList.cbegin(); it != verilogList.cend(); ++it) std::cout << " " << *it;
  // std::cout<<'\n';
#if 0
  fmt::print("\nprinting operand recursion\n");
  std::vector<std::string> v;
  std::string              op_tmp = "";
  std::string              op_    = "";
  int                      set    = 0;
  // The bits for the inputs is not kept in string, printing when iterating over Ports
  for (auto it = operandList.cbegin(); it != operandList.cend(); ++it) {
    // std::cout << "OP: " << *it;
    for (auto iv = v.cbegin(); iv != v.cend(); ++iv) {
      op_tmp = (*it)[1];
      op_    = (*iv)[0];
      std::cout << " " << op_;

      if (op_[0] == op_tmp[0]) {
        set = 1;
      }
    }
    if (set != 1) {
      op_tmp = (*it)[1];
      v.emplace_back(op_tmp);
      set = 0;
    }
  }
  std::vector<std::string>::iterator ip;
  fmt::print("\nprinting inputs\n");

  for (ip = v.begin(); ip != v.end(); ++ip) {
    std::cout << " " << *ip;
    auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_select(""));
    lnast->add_child(idx_dot, Lnast_node::create_ref(lnast->add_string("___" + *ip)));
    lnast->add_child(idx_dot, Lnast_node::create_ref(lnast->add_string("$" + *ip)));
    lnast->add_child(idx_dot, Lnast_node::create_const("__ubits"));

    auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_assign(""));
    lnast->add_child(idx_assign, Lnast_node::create_ref(lnast->add_string("___" + *ip)));
    lnast->add_child(idx_assign, Lnast_node::create_const("1"));
  }
#endif
  const auto& lhs  = expr.left();
  std::string temp = "___";
  if (lhs.kind == ExpressionKind::NamedValue) {
    const auto& var = lhs.as<NamedValueExpression>();
    fmt::print("bits:{} {} =  ", var.type->getBitWidth(), var.symbol.name);
    auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_select(""));
    lnast->add_child(idx_dot, Lnast_node::create_ref(lnast->add_string(temp.append(var.symbol.name))));
    temp = "___";
    lnast->add_child(idx_dot, Lnast_node::create_ref(lnast->add_string(output.append(var.symbol.name))));
    lnast->add_child(idx_dot, Lnast_node::create_const("__ubits"));
    output          = "%";
    auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_assign(""));
    lnast->add_child(idx_assign, Lnast_node::create_ref(lnast->add_string(temp.append(var.symbol.name))));
    lnast->add_child(idx_assign, Lnast_node::create_const(lnast->add_string(std::to_string(var.type->getBitWidth()))));
  }
  // std::cout<<'check\n';
  fmt::print("\nlnast time\n");

  int count    = 0;
  int tmp_flag = 0;
  int last_op  = 0;
  int not_flag = 0;
  int first    = 0;
  for (auto it = verilogList.crbegin(); it != verilogList.crend(); ++it) {
    // if (operandList.size()==0) last_op=1;
    if (operandList.size() == 1) {
      last_op = 1;
      // break;
    }
    fmt::print("check:{} ", operandList.size());
    count++;
    fmt::print("tmp list check: \n");
    addLnast (idx_stmts, *it, count, first, tmp_flag, not_flag, last_op);
  }
  auto node_dpa = Lnast_node::create_dp_assign("", line_num, pos1, pos2);
  auto idx_dpa  = lnast->add_child(idx_stmts, node_dpa);
  // const auto& lhs = expr.left();
  if (lhs.kind == ExpressionKind::NamedValue) {
    const auto& var = lhs.as<NamedValueExpression>();
    fmt::print("bits:{} {} =  ", var.type->getBitWidth(), var.symbol.name);
    // operandList.emplace_back(var.symbol.name);
    lnast->add_child(idx_dpa, Lnast_node::create_ref(lnast->add_string(output.append(var.symbol.name))));  // string_view = %out
    output         = "%";
    auto node_tmp1 = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
    tmpList.pop_back();
    lnast->add_child(idx_dpa, node_tmp1);
  } else {
    fmt::print("TODO. What is this");
  }
}

void Lnast_visitor::handle(const slang::Expression& expr) {
  // if (numErrors > errorLimit)
  //   return;
  // if (expr==NULL){
  //   return;
  // }
  std::string input = "$";
  if (expr.kind == ExpressionKind::UnaryOp) {
    const auto& op1 = expr.as<UnaryExpression>();
    switch (op1.op) {
      case (UnaryOperator::Plus): {
        fmt::print("UnaryOperator: + \n");
        break;
      }
      case (UnaryOperator::Minus): {
        fmt::print("UnaryOperator: - \n");
        break;
      }
      case (UnaryOperator::BitwiseNot): {
        verilogList.emplace_back(operators::NOT);
        fmt::print(" ~ ");
        break;
      }
      case (UnaryOperator::BitwiseAnd): {
        fmt::print("UnaryOperator: & \n");
        break;
      }

      case (UnaryOperator::BitwiseOr): {
        fmt::print("UnaryOperator: | \n");
        break;
      }
      case (UnaryOperator::BitwiseXor): {
        fmt::print("UnaryOperator: ^ \n");
        break;
      }
      case (UnaryOperator::BitwiseNand): fmt::print("UnaryOperator: NAND \n"); break;
      case (UnaryOperator::BitwiseNor): fmt::print("UnaryOperator: NOR \n"); break;
      case (UnaryOperator::BitwiseXnor): fmt::print("UnaryOperator: XNOR \n"); break;
      default:
        break;
        // case UnaryOperator::LogicalNot:
        // case UnaryOperator::Preincrement:
        // case UnaryOperator::Predecrement:
        // case UnaryOperator::Postincrement:
        // case UnaryOperator::Postdecrement:
    }
    // Unary operand place in back
    const auto& opswitch = op1.operand();
    if (opswitch.kind == ExpressionKind::NamedValue) {
      const auto& op2 = opswitch.as<NamedValueExpression>();
      operandList.emplace_back(input.append(op2.symbol.name));
      input = "$";
      fmt::print("RHS named value: {} \n", op2.symbol.name);
    }
  }
  if (expr.kind == ExpressionKind::BinaryOp) {
    const auto& op1    = expr.as<BinaryExpression>();
    const auto& check1 = op1.left();
    const auto& check2 = op1.right();
    switch (op1.op) {
      case BinaryOperator::Add: {
        // idx   = lnast->add_child(idx_stmts, Lnast_node::create_plus  ("+"));
        verilogList.emplace_back(operators::PLUS);
        break;
      }
      case BinaryOperator::Subtract: {
        // idx    = lnast->add_child(idx_stmts, Lnast_node::create_minus  ("-"));
        verilogList.emplace_back(operators::MINUS);
        break;
      }
      case BinaryOperator::Multiply: {
        verilogList.emplace_back(operators::MULT);
        break;
      }
      case BinaryOperator::Divide: verilogList.emplace_back(operators::DIV);break;
      case BinaryOperator::Mod: verilogList.emplace_back(operators::MOD); break;
      case BinaryOperator::BinaryAnd: {
        verilogList.emplace_back(operators::AND);
        // idx = lnast->add_child(idx_stmts, Lnast_node::create_and ("AND"));
        fmt::print(" & ");
        break;
      }
      case BinaryOperator::BinaryOr:
        verilogList.emplace_back(operators::OR);
        // idx = lnast->add_child(idx_stmts, Lnast_node::create_or ("OR"));
        // fmt::print("my list says {}\n", verilogList.back());
        fmt::print(" | ");
        break;
      case BinaryOperator::BinaryXor: {
        verilogList.emplace_back(operators::XOR);
        // idx = lnast->add_child(idx_stmts, Lnast_node::create_xor ("XOR"));
        fmt::print(" ^ \n");
        break;
      }
      case BinaryOperator::BinaryXnor: {
        fmt::print(" XNOR ");
        break;
      }
      default: {
        // int flag=0;
        fmt::print("what is this operator?\n");
        break;
      }
        // case BinaryOperator::Equality:
        // case BinaryOperator::Inequality:
        // case BinaryOperator::CaseEquality:
        // case BinaryOperator::CaseInequality:
        // case BinaryOperator::GreaterThanEqual:
        // case BinaryOperator::GreaterThan:
        // case BinaryOperator::LessThanEqual:
        // case BinaryOperator::LessThan:
        // case BinaryOperator::WildcardEquality:
        // case BinaryOperator::WildcardInequality:
        // case BinaryOperator::LogicalAnd:
        // case BinaryOperator::LogicalOr:
        // case BinaryOperator::LogicalImplication:
        // case BinaryOperator::LogicalEquivalence:
        // case BinaryOperator::LogicalShiftLeft:
        // case BinaryOperator::LogicalShiftRight:
        // case BinaryOperator::ArithmeticShiftLeft:
        // case BinaryOperator::ArithmeticShiftRight:
        // case BinaryOperator::Power:
    }

    if (check1.kind == ExpressionKind::NamedValue) {
      const auto& rhs_1 = check1.as<NamedValueExpression>();
      operandList.emplace_back(input.append(rhs_1.symbol.name));
      input = "$";
      fmt::print(" {} ", rhs_1.symbol.name);
    }

    if (check2.kind == ExpressionKind::NamedValue) {
      const auto& rhs_2 = check2.as<NamedValueExpression>();
      operandList.emplace_back(input.append(rhs_2.symbol.name));
      input = "$";
      fmt::print(" {} \n", rhs_2.symbol.name);
    }
    const auto& recurse = expr.as<BinaryExpression>();
    const auto& lhs     = recurse.left();
    const auto& rhs     = recurse.right();
    if (lhs.kind == ExpressionKind::BinaryOp || lhs.kind == ExpressionKind::UnaryOp) {
      handle(recurse.left());
    }
    if (rhs.kind == ExpressionKind::BinaryOp || rhs.kind == ExpressionKind::UnaryOp) {
      handle(recurse.right());
    }
  }
}
void Lnast_visitor::handle(const slang::ExpressionStatement& expr) {
  if (numErrors > errorLimit)
    return;

  fmt::print("HERE\n");
  if (expr.expr.kind == ExpressionKind::Assignment) {
    handle(expr.expr.as<AssignmentExpression>());
  } else {
    fmt::print("FIXME: handle this type\n");
  }
}

void Lnast_visitor::handle(const slang::StatementBlockSymbol& symbol) {
  (void)symbol;
  if (numErrors > errorLimit)
    return;
  fmt::print("HERE.3\n");
}

void Lnast_visitor::handle(const slang::ProceduralBlockSymbol& pblock) {
  if (numErrors > errorLimit)
    return;
  fmt::print("HERE.2\n");
  if (pblock.procedureKind == ProceduralBlockKind::Always) {
    fmt::print("Proc Block kind: {}\n", toString(pblock.procedureKind));
  }
  auto& stmt = pblock.getBody();
  if (stmt.kind == StatementKind::Timed) {
    auto& timed = stmt.as<TimedStatement>();
    fmt::print("TODO: check that the sentitivity list is completed (no implicit latch)\n");
    if (timed.stmt.kind == StatementKind::Block) {
      auto& block = timed.stmt.as<BlockStatement>();
      I(block.getStatements().kind == StatementKind::List);  // TODO: fix if single statement
      for (auto& bstmt : block.getStatements().as<StatementList>().list) {
        if (bstmt->kind == StatementKind::ExpressionStatement) {
          handle(bstmt->as<ExpressionStatement>());  // This call handle ExpressionStatement

        } else
          fmt::print("TODO: handle kind {}\n", bstmt->kind);
      }
    }
  }
}

void Lnast_visitor::handle(const slang::InstanceSymbol& symbol) {
  if (numErrors > errorLimit)
    return;

  const auto& def = symbol.getDefinition();
  fmt::print("definition:{}\n", def.name);

  lnast = std::make_unique<Lnast>(def.name);
  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));
  auto node_stmts = Lnast_node::create_stmts("___stmts");
  auto idx_stmts  = lnast->add_child(lnast->get_root(), node_stmts);

  symbol.resolvePortConnections();
  for (const auto& p : symbol.body.getPortList()) {
    if (p->kind == SymbolKind::Port) {
      const auto &port = p->as<PortSymbol>();
      // TODO: Nicer to do this instead (still issues at LNAST)
      // $ = (a.__ubits=1, c.__sbits=3)

      I(port.defaultValue == nullptr);  // give me a case to DEBUG

      auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_select(""));
      auto tmp     = lnast->add_string(absl::StrCat("___", port.name, "_attr"));
      std::string_view var_name;
      if (port.direction == ArgumentDirection::In)
        var_name = lnast->add_string(absl::StrCat("$", port.name));
      else
        var_name = lnast->add_string(absl::StrCat("%", port.name));

      lnast->add_child(idx_dot, Lnast_node::create_ref(tmp));
      lnast->add_child(idx_dot, Lnast_node::create_ref(var_name));
      lnast->add_child(idx_dot, Lnast_node::create_const("__ubits")); // FIXME: if input is signed use __sbits

      auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_assign(""));
      lnast->add_child(idx_assign, Lnast_node::create_ref(tmp));
      lnast->add_child(idx_assign, Lnast_node::create_const(lnast->add_string(std::to_string(port.getType().getBitWidth()))));
    } else if (p->kind == SymbolKind::InterfacePort) {
      const auto& port = p->as<InterfacePortSymbol>();
      (void)port;

      fmt::print("port:{} FIXME\n", p->name);
    } else {
      I(false);  // What other type?
    }
  }

  for (auto attr : compilation.getAttributes(symbol)) attr->getValue();

  // Instance bodies are all the same, so if we've visited this one
  // already don't bother doing it again.
  if (!visitedInstanceBodies.emplace(&symbol.body).second)
    return;

  // In order to avoid infinitely recursive instantiations, keep track
  // of how deep we are in the hierarchy tree and report an error if we
  // get too deep.
  if (hierarchyDepth > 100) {
    fmt::print("ERROR: recursive fcall\n");
    return;
  }

  hierarchyDepth++;
  visit(symbol.body);
  hierarchyDepth--;

  lnast->dump();
  parsed_lnasts.push_back(std::move(lnast));
}

void Lnast_visitor::handle(const slang::GenerateBlockSymbol& symbol) {
  if (!symbol.isInstantiated)
    return;
  handleDefault(symbol);
}
