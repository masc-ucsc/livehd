
#include "slang_tree.hpp"

#include <charconv>

#include "fmt/format.h"
#include "iassert.hpp"
#include "slang/compilation/Compilation.h"
#include "slang/compilation/Definition.h"
#include "slang/symbols/ASTSerializer.h"
#include "slang/symbols/CompilationUnitSymbols.h"
#include "slang/symbols/InstanceSymbols.h"
#include "slang/symbols/PortSymbols.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/types/Type.h"

Slang_tree::Slang_tree() {

  parsed_lnasts.clear();

}

void Slang_tree::process_root(const slang::RootSymbol &root) {
  auto topInstances = root.topInstances;
  for (auto inst : topInstances) {
    //fmt::print("slang_tree top:{}\n", inst->name);

    I(!has_lnast(inst->name));  // top level should not be already (may sub instances)
    auto ok = process_top_instance(*inst);
    if (!ok) {
      Pass::info("unable to process top module:{}\n", inst->name);
    }
  }
}

std::vector<std::shared_ptr<Lnast>> Slang_tree::pick_lnast() {
  std::vector<std::shared_ptr<Lnast>> v;

  for (auto &l : parsed_lnasts) {
    if (l.second)  // do not push null ptr
      v.emplace_back(l.second);
  }

  parsed_lnasts.clear();

  return v;
}

bool Slang_tree::process_top_instance(const slang::InstanceSymbol &symbol) {
  const auto &def = symbol.getDefinition();

  // Instance bodies are all the same, so if we've visited this one
  // already don't bother doing it again.
  if (parsed_lnasts.contains(def.name)) {
    fmt::print("slang_tree module:{} already parsed\n", def.name);
    return false;
  }

  I(symbol.isModule());  // modules are fine. Interfaces are a TODO

  I(lnast_create_obj.lnast == nullptr);

  parsed_lnasts.emplace(def.name, nullptr);  // insert to avoid recursion (reinsert at the end)

  lnast_create_obj.new_lnast(def.name);

  //symbol.resolvePortConnections();
  auto decl_pos = 0u;
  for (const auto &p : symbol.body.getPortList()) {
    if (p->kind == slang::SymbolKind::Port) {
      const auto &port = p->as<slang::PortSymbol>();

      //I(port.defaultValue == nullptr);  // give me a case to DEBUG

      std::string var_name;
      if (port.direction == slang::ArgumentDirection::In) {
        #ifdef LNAST_NODE
        var_name = absl::StrCat("$.:", decl_pos, ":", port.name);
        #else
        var_name = absl::StrCat("$", port.name);
        #endif
        lnast_create_obj.vname2lname.emplace(port.name, var_name);
      } else {
        #ifdef LNAST_NODE
        var_name = absl::StrCat("%.:", decl_pos, ":", port.name);
        #else
        var_name = absl::StrCat("%", port.name);
        #endif
        lnast_create_obj.vname2lname.emplace(port.name, var_name);
      }

      const auto &type = port.getType();
      if (type.hasFixedRange()) {
        auto range = type.getFixedRange();
        if (!range.isLittleEndian()) {
          fmt::print("WARNING: {} is big endian, Flipping IO to handle. Careful about mix/match with modules\n", port.name);
        }
      }
      lnast_create_obj.vname2lname.emplace(var_name, var_name);
      lnast_create_obj.create_declare_bits_stmts(var_name, type.isSigned(), type.getBitWidth());
      ++decl_pos;

    } else if (p->kind == slang::SymbolKind::InterfacePort) {
      const auto &port = p->as<slang::InterfacePortSymbol>();
      (void)port;

      fmt::print("port:{} FIXME\n", p->name);
    } else {
      I(false);  // What other type?
    }
  }

  for (const auto &member : symbol.body.members()) {
    if (member.kind == slang::SymbolKind::Port) {
      // already done
    } else if (member.kind == slang::SymbolKind::Net) {
      const auto &ns   = member.as<slang::NetSymbol>();
      auto       *expr = ns.getInitializer();
      if (expr) {
        // std::string lhs_var = lnast_create_obj.get_lnast_name(member.name);
        lnast_create_obj.create_assign_stmts(member.name, process_expression(*expr, true)); // get last value for assigns
      }
    } else if (member.kind == slang::SymbolKind::ContinuousAssign) {
      const auto &ca = member.as<slang::ContinuousAssignSymbol>();
      const auto &as = ca.getAssignment();
      bool        ok = process(as.as<slang::AssignmentExpression>());
      if (!ok) {
        lnast_create_obj.lnast = nullptr;
        return false;
      }
    } else if (member.kind == slang::SymbolKind::ProceduralBlock) {
      const auto &pbs = member.as<slang::ProceduralBlockSymbol>();

      if (pbs.procedureKind == slang::ProceduralBlockKind::Always) {
        const auto &stmt = pbs.getBody();

        if (stmt.kind == slang::StatementKind::Timed) {
          Pass::info("always with sensitivity list at {} pos:{}, assuming always_comb",
                     member.location.buffer().getId(),
                     member.location.offset());
          const auto &timed = stmt.as<slang::TimedStatement>();
          if (timed.stmt.kind == slang::StatementKind::Block) {
            const auto &block = timed.stmt.as<slang::BlockStatement>();
            I(block.getStatements().kind == slang::StatementKind::List);
            for (const auto &bstmt : block.getStatements().as<slang::StatementList>().list) {
              if (bstmt->kind == slang::StatementKind::ExpressionStatement) {
                const auto &expr = bstmt->as<slang::ExpressionStatement>().expr;
                bool        ok   = process(expr.as<slang::AssignmentExpression>());
                if (!ok) {
                  lnast_create_obj.lnast = nullptr;
                  return false;
                }
              } else {
                fmt::print("TODO: handle kind {}\n", (int)bstmt->kind);
              }
            }
          }

        } else {
          fmt::print("FIXME: missing sensitivity type\n");
        }
      }
    } else if (member.kind == slang::SymbolKind::UnknownModule) {
      const auto &mod = member.as<slang::UnknownModuleSymbol>();

      auto *library = Graph_library::instance("lgdb"); // FIXME: no hardcode path

      auto lgid = library->get_lgid(mod.moduleName);
      if (lgid == 0) {
        Pass::error("FIXME: handle unknown (not cell) module ({}). Maybe try in-order (sequential)\n", mod.moduleName);
      }
      Sub_node *sub = library->ref_sub(lgid);
      I(sub);

      const auto &plist = mod.getPortConnections();
      const auto &nlist = mod.getPortNames();
      I(plist.size() == nlist.size());

      // 1st- create input tuple
      std::vector<std::pair<std::string, std::string>> inp_tup;

      for(auto i=0u; i<plist.size(); ++i) {
        const auto &n = nlist[i];

        if (sub->is_output(n))
          continue;

        const auto &p = plist[i];
        I(p->kind == slang::AssertionExprKind::Simple);
        const auto &expr = p->as<slang::SimpleAssertionExpr>();

        inp_tup.emplace_back(std::make_pair(n, process_expression(expr.expr, true))); // module connections should use last_value write
      }

      auto inp_tup_var = lnast_create_obj.create_lnast_tmp();
      lnast_create_obj.create_named_tuple(inp_tup_var, inp_tup);

      // 2nd- create function call
      lnast_create_obj.create_func_call(mod.name, mod.moduleName, inp_tup_var);

      // 3rd- assign output tuple to associated variables
      for(auto i=0u; i<plist.size(); ++i) {
        const auto &n = nlist[i];

        if (!sub->is_output(n))
          continue;

        auto rhs_var = lnast_create_obj.create_tuple_get(mod.name, n);

        const auto &p = plist[i];
        I(p->kind == slang::AssertionExprKind::Simple);

        const auto &aexpr = p->as<slang::SimpleAssertionExpr>();
        process_lhs(aexpr.expr, rhs_var, true); // last_value in module
      }
    } else {
      Pass::error("FIXME: missing body type\n");
    }
  }

  //lnast_create_obj.lnast->dump();

  parsed_lnasts.insert_or_assign(def.name, lnast_create_obj.lnast);
  lnast_create_obj.lnast = nullptr;

  return true;
}

void Slang_tree::process_lhs(const slang::Expression &lhs, const std::string &rhs_var, bool last_value) {

  std::string var_name;
  bool        dest_var_sign;
  int         dest_var_bits;

  std::string dest_max_bit;
  std::string dest_min_bit;

  if (lhs.kind == slang::ExpressionKind::NamedValue) {
    const auto &var = lhs.as<slang::NamedValueExpression>();
    var_name        = lnast_create_obj.get_lnast_lhs_name(var.symbol.name);
    dest_var_sign   = var.type->isSigned();
    dest_var_bits   = var.type->getBitWidth();
    I(!var.type->isStruct());  // FIXME: structs
  } else if (lhs.kind == slang::ExpressionKind::ElementSelect) {
    const auto &es = lhs.as<slang::ElementSelectExpression>();
    I(es.value().kind == slang::ExpressionKind::NamedValue);

    dest_max_bit = process_expression(es.selector(), last_value);
    dest_min_bit = dest_max_bit;

    const auto &var = es.value().as<slang::NamedValueExpression>();
    var_name        = lnast_create_obj.get_lnast_lhs_name(var.symbol.name);

    dest_var_sign = false;
    dest_var_bits = 1;
  } else {
    I(lhs.kind == slang::ExpressionKind::RangeSelect);
    const auto &rs = lhs.as<slang::RangeSelectExpression>();
    I(rs.value().kind == slang::ExpressionKind::NamedValue);

    const auto &var = rs.value().as<slang::NamedValueExpression>();
    var_name        = lnast_create_obj.get_lnast_lhs_name(var.symbol.name);
    dest_var_sign   = var.type->isSigned();
    dest_var_bits   = var.type->getBitWidth();
    I(!var.type->isStruct());  // FIXME: structs

    dest_max_bit = process_expression(rs.left(), last_value);
    dest_min_bit = process_expression(rs.right(), last_value);
  }

  auto it = lnast_create_obj.vname2lname.find(var_name);
  if (it == lnast_create_obj.vname2lname.end()) {
    lnast_create_obj.vname2lname.emplace(var_name, var_name);
    lnast_create_obj.create_declare_bits_stmts(var_name, dest_var_sign, dest_var_bits);
    if (dest_var_sign)
      lnast_create_obj.create_assign_stmts(var_name, "0sb?");
    else {
      std::string qmarks(dest_var_bits,'?');

      lnast_create_obj.create_assign_stmts(var_name, absl::StrCat("0b", qmarks));  // mark with x so that potential use is poison if needed
    }
  }

  if (dest_min_bit.empty() && dest_max_bit.empty()) {
    lnast_create_obj.create_assign_stmts(var_name, rhs_var);
  } else {
    auto bitmask = lnast_create_obj.create_bitmask_stmts(dest_max_bit, dest_min_bit);
#ifdef LNASTOP_DONE
    lnast_create_obj.create_set_mask_stmts(var_name, bitmask, rhs_var);
#else
    auto tmp_var = lnast_create_obj.create_lnast_tmp();
    lnast_create_obj.create_assign_stmts(tmp_var, var_name);
    lnast_create_obj.create_set_mask_stmts(tmp_var, bitmask, rhs_var);
    lnast_create_obj.create_assign_stmts(var_name, tmp_var);
#endif
  }
}

bool Slang_tree::process(const slang::AssignmentExpression &expr) {
  auto rhs_var = process_expression(expr.right(), true);

  const auto &lhs = expr.left();

  process_lhs(lhs, rhs_var, true);

  return true;
}

std::string Slang_tree::process_expression(const slang::Expression &expr, bool last_value) {
  if (expr.kind == slang::ExpressionKind::NamedValue) {
    const auto &nv = expr.as<slang::NamedValueExpression>();
    return lnast_create_obj.get_lnast_name(nv.symbol.name, last_value);
  }

  if (expr.kind == slang::ExpressionKind::IntegerLiteral) {
    const auto                       &il    = expr.as<slang::IntegerLiteral>();
    auto                              svint = il.getValue();
    slang::SmallVectorSized<char, 32> buffer;
    if (!svint.hasUnknown() && svint.getMinRepresentedBits() < 8) {
      svint.writeTo(buffer, slang::LiteralBase::Decimal, false);
      return std::string(buffer.data(), buffer.size());
    }

    svint.writeTo(buffer, slang::LiteralBase::Hex, false);
    return absl::StrCat("0x", std::string_view(buffer.data(), buffer.size()));
  }

  if (expr.kind == slang::ExpressionKind::BinaryOp) {
    const auto &op  = expr.as<slang::BinaryExpression>();
    auto        lhs = process_expression(op.left(), last_value);
    auto        rhs = process_expression(op.right(), last_value);

    std::string var;
    switch (op.op) {
      case slang::BinaryOperator::Add: var = lnast_create_obj.create_plus_stmts(lhs, rhs); break;
      case slang::BinaryOperator::Subtract: var = lnast_create_obj.create_minus_stmts(lhs, rhs); break;
      case slang::BinaryOperator::Multiply: var = lnast_create_obj.create_mult_stmts(lhs, rhs); break;
      case slang::BinaryOperator::Divide: var = lnast_create_obj.create_div_stmts(lhs, rhs); break;
      case slang::BinaryOperator::Mod: var = lnast_create_obj.create_mod_stmts(lhs, rhs); break;
      case slang::BinaryOperator::BinaryAnd: var = lnast_create_obj.create_bit_and_stmts(lhs, rhs); break;
      case slang::BinaryOperator::BinaryOr: var = lnast_create_obj.create_bit_or_stmts({lhs, rhs}); break;
      case slang::BinaryOperator::BinaryXor: var = lnast_create_obj.create_bit_xor_stmts(lhs, rhs); break;
      case slang::BinaryOperator::BinaryXnor:
        var = lnast_create_obj.create_bit_not_stmts(lnast_create_obj.create_bit_xor_stmts(lhs, rhs));
        break;
      default: {
        fmt::print("FIXME unimplemented binary operator\n");
        var = "fix_binary_op";
      }
    }

    auto bw = std::to_string(op.type->getBitWidth());
    if (op.type->isSigned()) {
      return lnast_create_obj.create_sext_stmts(var, bw);
    }

    auto mask = lnast_create_obj.create_mask_stmts(bw);
    return lnast_create_obj.create_bit_and_stmts(var, mask);
  }

  if (expr.kind == slang::ExpressionKind::UnaryOp) {
    const auto &op = expr.as<slang::UnaryExpression>();

    if (op.op == slang::UnaryOperator::BitwiseAnd)
      return process_mask_and(op, last_value);
    if (op.op == slang::UnaryOperator::BitwiseNand)
      return lnast_create_obj.create_bit_not_stmts(process_mask_and(op, last_value));
    if (op.op == slang::UnaryOperator::BitwiseXor)
      return process_mask_xor(op, last_value);
    if (op.op == slang::UnaryOperator::BitwiseXnor)
      return lnast_create_obj.create_bit_not_stmts(process_mask_xor(op, last_value));

    auto lhs = process_expression(op.operand(), last_value);
    switch (op.op) {
      case slang::UnaryOperator::BitwiseNot: return lnast_create_obj.create_bit_not_stmts(lhs);
      case slang::UnaryOperator::LogicalNot: return lnast_create_obj.create_logical_not_stmts(lhs);
      case slang::UnaryOperator::Plus: return lhs;
      case slang::UnaryOperator::Minus: return lnast_create_obj.create_minus_stmts("0", lhs);
      case slang::UnaryOperator::BitwiseOr: return lnast_create_obj.create_reduce_or_stmts(lhs);
      // do I use bit not or logical not?
      // Also is it ok for it to be two connected references if we have no lnast node?
      case slang::UnaryOperator::BitwiseNor:
        return lnast_create_obj.create_bit_not_stmts(lnast_create_obj.create_reduce_or_stmts(lhs));
        // case UnaryOperator::Preincrement:
        // case UnaryOperator::Predecrement:
        // case UnaryOperator::Postincrement:
        // case UnaryOperator::Postdecrement:
      default: {
        fmt::print("FIXME unimplemented unary operator\n");
      }
    }
  }

  if (expr.kind == slang::ExpressionKind::Conversion) {
    const auto        &conv    = expr.as<slang::ConversionExpression>();
    const slang::Type *to_type = conv.type;

    auto res = process_expression(conv.operand(), last_value);  // NOTHING TO DO? (the dp_assign handles it?)

    const slang::Type *from_type = conv.operand().type;

    if (to_type->isSigned() == from_type->isSigned() && to_type->getBitWidth() >= from_type->getBitWidth())
      return res;  // no need to add mask if expanding

    auto min_bits = std::to_string(std::min(to_type->getBitWidth(), from_type->getBitWidth()));

    if (to_type->isSigned())
      return lnast_create_obj.create_sext_stmts(res, min_bits);

    I(!to_type->isSigned());
    // and(and(X,a),b) -> and(X,min(a,b))
    auto bw   = std::to_string(to_type->getBitWidth());
    auto mask = lnast_create_obj.create_mask_stmts(bw);
    return lnast_create_obj.create_bit_and_stmts(res, mask);
#if 0
    if (to_type->isSigned() && !from_type->isSigned()) {
      if (to_type->getBitWidth()<=from_type->getBitWidth()) {
        // sext(and(X,a),b) && a>b -> sext(X,b)
        return lnast_create_obj.create_sext_stmts(res, create_lnast(min_bits));
      }else{
        // sext(and(X,a),b) && a<b -> and(X,a)
        auto mask = lnast_create_obj.create_mask_stmts(create_lnast(min_bits));
        return lnast_create_obj.create_bit_and_stmts(res, mask);
      }
    }

    I(!to_type->isSigned() && from_type->isSigned());

    auto tmp = lnast_create_obj.create_sext_stmts(res, create_lnast(from_type->getBitWidth()));
    auto mask = lnast_create_obj.create_mask_stmts(create_lnast(to_type->getBitWidth()));
    return lnast_create_obj.create_bit_and_stmts(tmp, mask);
#endif
  }

  if (expr.kind == slang::ExpressionKind::Concatenation) {
    const auto &concat = expr.as<slang::ConcatenationExpression>();

    auto offset = 0u;

    std::vector<std::string> adjusted_fields;

    for (const auto &e : concat.operands()) {
      auto bits = e->type->getBitWidth();

      auto res_var = process_expression(*e, last_value);

      if (offset) {
        res_var = lnast_create_obj.create_shl_stmts(res_var, std::to_string(offset));
      }
      adjusted_fields.emplace_back(res_var);

      offset += bits;
    }

    return lnast_create_obj.create_bit_or_stmts(adjusted_fields);
  }

  if (expr.kind == slang::ExpressionKind::ElementSelect) {
    const auto &es = expr.as<slang::ElementSelectExpression>();
    I(es.value().kind == slang::ExpressionKind::NamedValue);

    const auto &var = es.value().as<slang::NamedValueExpression>();

    auto sel_var  = lnast_create_obj.get_lnast_name(var.symbol.name, last_value);
    auto sel_bit  = process_expression(es.selector(), last_value);
    auto sel_mask = lnast_create_obj.create_shl_stmts("1", sel_bit);

    return lnast_create_obj.create_get_mask_stmts(sel_var, sel_mask);
  }

  if (expr.kind == slang::ExpressionKind::RangeSelect) {
    const auto &es = expr.as<slang::RangeSelectExpression>();
    I(es.value().kind == slang::ExpressionKind::NamedValue);

    const auto &var = es.value().as<slang::NamedValueExpression>();

    auto sel_var = lnast_create_obj.get_lnast_name(var.symbol.name, last_value);
    auto max_bit = process_expression(es.left(), last_value);
    auto min_bit = process_expression(es.right(), last_value);

    auto sel_mask = lnast_create_obj.create_bitmask_stmts(max_bit, min_bit);
    return lnast_create_obj.create_get_mask_stmts(sel_var, sel_mask);
  }

  fmt::print("FIXME still unimplemented Expression kind\n");

  return "FIXME_op";
}

std::string Slang_tree::process_mask_and(const slang::UnaryExpression &uexpr, bool last_value) {
  // reduce and does not have a direct mapping in Lgraph
  // And(Not(Ror(Not(inp))), inp.MSB)

  const auto &op      = uexpr.operand();
  auto        msb_pos = op.type->getBitWidth() - 1;

  auto inp = process_expression(op, last_value);

  auto tmp
      = lnast_create_obj.create_bit_not_stmts(lnast_create_obj.create_reduce_or_stmts(lnast_create_obj.create_bit_not_stmts(inp)));
  return lnast_create_obj.create_bit_and_stmts(
      tmp,
      lnast_create_obj.create_sra_stmts(inp, std::to_string(msb_pos)));  // No need pick (reduce is 1 bit)
}

std::string Slang_tree::process_mask_xor(const slang::UnaryExpression &uexpr, bool last_value) {
  const auto &op = uexpr.operand();

  auto inp = process_expression(op, last_value);

  auto mask = lnast_create_obj.create_mask_stmts(std::to_string(op.type->getBitWidth()));
  return lnast_create_obj.create_mask_xor_stmts(mask, inp);
}
