
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
  lnast = std::make_unique<Lnast>("module name");
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

void Lnast_visitor::handle(const slang::AssignmentExpression& expr) {
  int line_num = 0, pos1 = 0, pos2 = 0;
  if (numErrors > errorLimit)
    return;
  
  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  auto node_stmts  = Lnast_node::create_stmts ("stmts",  line_num, pos1, pos2);
  auto idx_stmts   = lnast->add_child(lnast->get_root(), node_stmts);     
  fmt::print("Start RHS recursion\n");
  handle(expr.right());
  // check verilog list
  fmt::print("printing operator recursion\n");
  for (auto it = verilogList.cbegin(); it != verilogList.cend(); ++it) std::cout << " " << *it;
  // std::cout<<'\n';
  fmt::print("\nprinting operand recursion\n");
  for (auto it = operandList.cbegin(); it != operandList.cend(); ++it) std::cout << " " << *it;
  // std::cout<<'check\n';
  fmt::print("\nlnast time\n");
  int count=0;
  int last_flag=0;
  int last_op=0;
  for (auto it = verilogList.crbegin(); it != verilogList.crend(); ++it){
      if (operandList.size()==0) break;
      else if (operandList.size()==1){
        last_op=1;
        break;
      }
      fmt::print("check:{} ",operandList.size());
      count++;
      if (*it =="AND"){
        fmt::print("AND \n");
        auto idx_and = lnast->add_child(idx_stmts, Lnast_node::create_and ("AND"));
        auto node_and   = Lnast_node::create_ref (lnast->add_string("__and"+std::to_string(count)));
        tmpList.insert(tmpList.begin(),("__and"+std::to_string(count))); //for connection the node refs
        lnast->add_child(idx_and, node_and);

        auto node_op1    = Lnast_node::create_ref (lnast->add_string(operandList.back()));
        lnast->add_child(idx_and, node_op1);
        operandList.pop_back();
        auto node_op2    = Lnast_node::create_ref (lnast->add_string(operandList.back()));
        lnast->add_child(idx_and, node_op2);
        operandList.pop_back();
        
      }
      else if (*it =="OR"){
        auto idx_or = lnast->add_child(idx_stmts, Lnast_node::create_or ("OR"));
        auto node_or   = Lnast_node::create_ref (lnast->add_string("__or"+std::to_string(count)));
        tmpList.insert(tmpList.begin(),("__or"+std::to_string(count))); //for connection the node refs
        lnast->add_child(idx_or, node_or);

        auto node_op1    = Lnast_node::create_ref (lnast->add_string(operandList.back()));
        lnast->add_child(idx_or, node_op1);
        operandList.pop_back();
        auto node_op2    = Lnast_node::create_ref (lnast->add_string(operandList.back()));
        lnast->add_child(idx_or, node_op2);
        operandList.pop_back();
        
      }
      else if (*it =="XOR"){
        auto idx_xor = lnast->add_child(idx_stmts, Lnast_node::create_xor ("XOR"));
        auto node_xor   = Lnast_node::create_ref (lnast->add_string("__xor"+std::to_string(count)));
        tmpList.insert(tmpList.begin(),("__xor"+std::to_string(count))); //for connection the node refs
        lnast->add_child(idx_xor, node_xor);

        auto node_op1    = Lnast_node::create_ref (lnast->add_string(operandList.back()));
        lnast->add_child(idx_xor, node_op1);
        operandList.pop_back();
        auto node_op2    = Lnast_node::create_ref (lnast->add_string(operandList.back()));
        lnast->add_child(idx_xor, node_op2);
        operandList.pop_back();
      }
      else if (*it =="NOT"){
        fmt::print("NOT \n");
        auto idx_not = lnast->add_child(idx_stmts, Lnast_node::create_not ("NOT"));
        auto node_not   = Lnast_node::create_ref (lnast->add_string("__not"+std::to_string(count)));
        tmpList.insert(tmpList.begin(),"__not"+std::to_string(count)); //for connection the node refs
        lnast->add_child(idx_not, node_not);

        auto node_op1    = Lnast_node::create_ref (lnast->add_string(operandList.back()));
        lnast->add_child(idx_not, node_op1);
        operandList.pop_back();        
      }

  }

  int inc      = 0;
  int tmp_size = tmpList.size();

  for (auto it = verilogList.crbegin(); it != verilogList.crend(); ++it) {
    if (inc < count) {
      inc++;
    } else {
      inc++;
      auto node_and   = Lnast_node::create_ref (lnast->add_string("__and"+std::to_string(inc)));
      auto node_or   = Lnast_node::create_ref (lnast->add_string("__or"+std::to_string(inc)));
      auto node_xor   = Lnast_node::create_ref (lnast->add_string("__xor"+std::to_string(inc)));
      // auto node_not   = Lnast_node::create_ref (lnast->add_string("__not"+std::to_string(inc)));

      fmt::print("tmp list check: \n");
      for (auto check = tmpList.cbegin(); check!= tmpList.cend(); ++check) std::cout << " " << *check;
      
      if(*it=="AND"){ //can i somehow make this shorter
        tmpList.insert(tmpList.begin(),"__and"+std::to_string(inc)); //for connection the node refs
        auto idx_and = lnast->add_child(idx_stmts, Lnast_node::create_and ("AND"));
        lnast->add_child(idx_and, node_and);
        if (last_flag != 1) {
          auto node_tmp = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
          tmpList.pop_back();
          lnast->add_child(idx_and, node_tmp);
          tmp_size--;
          if (tmp_size == 0) {
            last_flag = 1;
          } else {
            auto node_tmp1 = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
            tmpList.pop_back();
            lnast->add_child(idx_and, node_tmp1);
            tmp_size--;
          }
        }
        if (last_op && last_flag) {
          last_op       = 0;
          last_flag     = 0;
          tmp_size      = tmpList.size();
          auto node_op1 = Lnast_node::create_ref(lnast->add_string(operandList.back()));
          lnast->add_child(idx_and, node_op1);
          operandList.pop_back();
        }
      } else if (*it == "OR") {
        tmpList.insert(tmpList.begin(), "__or" + std::to_string(inc));  // for connection the node refs
        auto idx_or = lnast->add_child(idx_stmts, Lnast_node::create_and("OR"));
        lnast->add_child(idx_or, node_or);
        if (last_flag != 1) {
          auto node_tmp = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
          tmpList.pop_back();
          lnast->add_child(idx_or, node_tmp);
          tmp_size--;
          if (tmp_size == 0) {
            last_flag = 1;
          } else {
            auto node_tmp1 = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
            tmpList.pop_back();
            lnast->add_child(idx_or, node_tmp1);
            tmp_size--;
          }
        }
        if (last_op && last_flag) {
          last_op       = 0;
          last_flag     = 0;
          tmp_size      = tmpList.size();
          auto node_op1 = Lnast_node::create_ref(lnast->add_string(operandList.back()));
          lnast->add_child(idx_or, node_op1);
          operandList.pop_back();
        }
      }
      else if(*it=="XOR"){
        tmpList.insert(tmpList.begin(),"__xor"+std::to_string(inc)); //for connection the node refs
        auto idx_xor = lnast->add_child(idx_stmts, Lnast_node::create_and ("XOR"));
        lnast->add_child(idx_xor, node_xor);
        if (last_flag != 1) {
          auto node_tmp = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
          tmpList.pop_back();
          lnast->add_child(idx_xor, node_tmp);
          tmp_size--;
          if (tmp_size == 0) {
            last_flag = 1;
          } else {
            auto node_tmp1 = Lnast_node::create_ref(lnast->add_string(tmpList.back()));
            tmpList.pop_back();
            lnast->add_child(idx_xor, node_tmp1);
            tmp_size--;
          }
        }
        if (last_op && last_flag) {
          last_op       = 0;
          last_flag     = 0;
          tmp_size      = tmpList.size();
          auto node_op1 = Lnast_node::create_ref(lnast->add_string(operandList.back()));
          lnast->add_child(idx_xor, node_op1);
          operandList.pop_back();
        }
      }
    }
  }
  auto node_dpa    = Lnast_node::create_dp_assign ("dp_assign",  line_num, pos1, pos2);
  auto idx_assign  = lnast->add_child(idx_stmts,  node_dpa);
  const auto& lhs = expr.left();
  if (lhs.kind == ExpressionKind::NamedValue) {
    const auto& var = lhs.as<NamedValueExpression>();
    fmt::print("bits:{} {} =  ", var.type->getBitWidth(), var.symbol.name);
    // operandList.emplace_back(var.symbol.name);
    lnast->add_child(idx_assign, Lnast_node::create_ref(var.symbol.name)); // string_view = %out
    auto node_tmp1   = Lnast_node::create_ref (lnast->add_string(tmpList.back()));
    tmpList.pop_back();
    lnast->add_child(idx_assign, node_tmp1);
  } 
  else {
    fmt::print("TODO. What is this");

  }
  lnast->dump();
  parsed_lnasts.push_back(std::move(lnast));
  //std::move(lnast);
}
void Lnast_visitor::handle(const slang::Expression& expr){
  // if (numErrors > errorLimit)
  //   return;
  // if (expr==NULL){
  //   return;
  // }

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
        verilogList.emplace_back("NOT");
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
      operandList.emplace_back(op2.symbol.name);
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
        verilogList.emplace_back("+");
        break;
      }
      case BinaryOperator::Subtract: {
        // idx    = lnast->add_child(idx_stmts, Lnast_node::create_minus  ("-"));
        verilogList.emplace_back("-");
        break;
      }
      case BinaryOperator::Multiply: {
        fmt::print(" * ");
        break;
      }
      case BinaryOperator::Divide: fmt::print(" / "); break;
      case BinaryOperator::Mod: fmt::print(" % "); break;
      case BinaryOperator::BinaryAnd: {
        verilogList.emplace_back("AND");
        // idx = lnast->add_child(idx_stmts, Lnast_node::create_and ("AND"));
        fmt::print(" & ");
        break;
      }
      case BinaryOperator::BinaryOr:
        verilogList.emplace_back("OR");
        // idx = lnast->add_child(idx_stmts, Lnast_node::create_or ("OR"));
        // fmt::print("my list says {}\n", verilogList.back());
        fmt::print(" | ");
        break;
      case BinaryOperator::BinaryXor: {
        verilogList.emplace_back("XOR");
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
        fmt::print("check!\n");
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

    if(check1.kind==ExpressionKind::NamedValue){
      const auto &rhs_1=check1.as<NamedValueExpression>();
      operandList.emplace_back(rhs_1.symbol.name);
      fmt::print(" {} ", rhs_1.symbol.name);
    }

    if (check2.kind == ExpressionKind::NamedValue) {
      const auto& rhs_2 = check2.as<NamedValueExpression>();
      // const auto  operand2 = rhs_2.symbol.name;
      // auto node_op2    = Lnast_node::create_ref (operand2);
      // auto idx_op2     = lnast->add_child(idx, node_op2);
      operandList.emplace_back(rhs_2.symbol.name);
      fmt::print(" {} \n", rhs_2.symbol.name);
    }
    const auto& recurse = expr.as<BinaryExpression>();
    // const auto &lhs=recurse.left().as<BinaryExpression>();
    // const auto &rhs=recurse.right().as<BinaryExpression>();
    const auto& lhs = recurse.left();
    const auto& rhs = recurse.right();
    // fmt::print(" {} \n",recurse.left().kind);
    // fmt::print("handle recursively the rhs (binaryOp,unaryop,...)\n");
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

  symbol.resolvePortConnections();
  for (const auto& p : symbol.body.getPortList()) {
    if (p->kind == SymbolKind::Port) {
      const auto& port = p->as<PortSymbol>();
      (void)port;

      I(port.defaultValue == nullptr);  // give me a case to DEBUG
      fmt::print("port:{} dir:{} bits:{}\n",
                 port.name,
                 port.direction == ArgumentDirection::In ? "in" : "out",
                 port.getType().getBitWidth());
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
}

void Lnast_visitor::handle(const slang::GenerateBlockSymbol& symbol) {
  if (!symbol.isInstantiated)
    return;
  handleDefault(symbol);
}
