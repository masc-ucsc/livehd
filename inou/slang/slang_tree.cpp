
#include "slang_tree.hpp"

#include <charconv>
#include <string>
#include <utility>

#include "diag.hpp"
#include "iassert.hpp"
#include "slang/ast/ASTVisitor.h"
#include "slang/util/SmallVector.h"
#include "slang_location.hpp"

// #define LNAST_NODE_POS 1

Slang_tree::Slang_tree() { parsed_lnasts.clear(); }

hhds::SourceId Slang_tree::mint_loc(slang::SourceRange range) {
  if (sm_ == nullptr || lnast_builder.lnast == nullptr) {
    return hhds::SourceId_invalid;
  }
  return livehd::slang_loc::mint(lnast_builder.lnast->source_locator(), *sm_, range);
}

void Slang_tree::emit_unsupported(slang::SourceRange range, std::string_view code, std::string message,
                                  std::string_view hint) {
  livehd::diag::Span span;
  if (sm_ != nullptr) {
    span = livehd::slang_loc::span_of(*sm_, range);
  }
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = std::string(code),
                                                     .category = "unsupported",
                                                     .pass     = "inou.slang",
                                                     .message  = std::move(message),
                                                     .span     = std::move(span),
                                                     .hint     = std::string(hint)});
}

void Slang_tree::process_root(const slang::ast::RootSymbol& root) {
  auto topInstances = root.topInstances;
  for (auto inst : topInstances) {
    I(!has_lnast(inst->name));  // top level should not be already (may sub instances)
    // Failures inside emit a located diagnostic and fail the step; a benign
    // "already lowered" dedup just returns false without a diagnostic.
    process_top_instance(*inst);
  }
}

std::vector<std::shared_ptr<Lnast>> Slang_tree::pick_lnast() {
  std::vector<std::shared_ptr<Lnast>> v;

  for (auto& l : parsed_lnasts) {
    if (l.second) {  // do not push null ptr
      v.emplace_back(l.second);
    }
  }

  parsed_lnasts.clear();

  return v;
}

bool Slang_tree::process_top_instance(const slang::ast::InstanceSymbol& symbol) {
  const auto& def = symbol.getDefinition();

  // Instance bodies are all the same, so if we've visited this one
  // already don't bother doing it again (benign dedup, not an error).
  if (parsed_lnasts.contains(def.name)) {
    return false;
  }

  I(symbol.isModule());  // modules are fine. Interfaces are a TODO

  I(lnast_builder.lnast == nullptr);

  parsed_lnasts.emplace(def.name, nullptr);  // insert to avoid recursion (reinsert at the end)

  lnast_builder.new_lnast(def.name);

  // symbol.resolvePortConnections();
#ifdef LNAST_NODE_POS
  auto decl_pos = 0u;
#endif
  for (const auto& p : symbol.body.getPortList()) {
    if (p->kind == slang::ast::SymbolKind::Port) {
      const auto& port = p->as<slang::ast::PortSymbol>();

      // I(port.defaultValue == nullptr);  // give me a case to DEBUG

      std::string var_name;
      if (port.direction == slang::ast::ArgumentDirection::In) {
#ifdef LNAST_NODE_POS
        var_name = absl::StrCat("p", decl_pos, "_", port.name);
#else
        var_name = port.name;
#endif
        lnast_builder.mark_input_name(var_name);
        lnast_builder.vname2lname.emplace(port.name, var_name);
      } else {
#ifdef LNAST_NODE_POS
        var_name = absl::StrCat("p", decl_pos, "_", port.name);
#else
        var_name = port.name;
#endif
        lnast_builder.vname2lname.emplace(port.name, var_name);
      }

      const auto& type = port.getType();
      if (type.hasFixedRange()) {
        auto range = type.getFixedRange();
        if (!range.isDescending()) {  // ascending [0:N] == big-endian (v10 isLittleEndian)
          livehd::diag::sink().emit(
              livehd::diag::Diagnostic{.severity = livehd::diag::Severity::warning,
                                       .code     = "big-endian-port",
                                       .category = "unsupported",
                                       .pass     = "inou.slang",
                                       .message  = std::string("port '") + std::string(port.name)
                                                   + "' is big-endian; flipping IO (mind mix/match with other modules)",
                                       .span = sm_ != nullptr ? livehd::slang_loc::span_of(*sm_, port.location) : livehd::diag::Span{}});
        }
      }
      lnast_builder.vname2lname.emplace(var_name, var_name);
      set_pending_loc(port.location);
      lnast_builder.create_declare_bits_stmts(var_name, type.isSigned(), type.getBitWidth());
      clear_pending_loc();
#ifdef LNAST_NODE_POS
      ++decl_pos;
#endif
    } else if (p->kind == slang::ast::SymbolKind::InterfacePort) {
      emit_unsupported(p->location, "unsupported-interface-port",
                       std::string("interface port '") + std::string(p->name) + "' is not supported by --reader slang",
                       "use --reader yosys-slang for interface ports");
      lnast_builder.lnast = nullptr;
      return false;
    } else {
      emit_unsupported(p->location, "unsupported-port-kind",
                       std::string("port '") + std::string(p->name) + "' has an unsupported kind");
      lnast_builder.lnast = nullptr;
      return false;
    }
  }

  for (const auto& member : symbol.body.members()) {
    if (member.kind == slang::ast::SymbolKind::Port) {
      // already done
    } else if (member.kind == slang::ast::SymbolKind::Net) {
      const auto& ns   = member.as<slang::ast::NetSymbol>();
      auto*       expr = ns.getInitializer();
      if (expr) {
        set_pending_loc(expr->sourceRange);
        lnast_builder.create_assign_stmts(member.name, process_expression(*expr, true));  // get last value for assigns
        clear_pending_loc();
      }
    } else if (member.kind == slang::ast::SymbolKind::ContinuousAssign) {
      const auto& ca = member.as<slang::ast::ContinuousAssignSymbol>();
      const auto& as = ca.getAssignment();
      set_pending_loc(as.sourceRange);
      bool ok = process(as.as<slang::ast::AssignmentExpression>());
      clear_pending_loc();
      if (!ok) {
        lnast_builder.lnast = nullptr;
        return false;
      }
    } else if (member.kind == slang::ast::SymbolKind::ProceduralBlock) {
      const auto& pbs = member.as<slang::ast::ProceduralBlockSymbol>();

      if (pbs.procedureKind == slang::ast::ProceduralBlockKind::Always) {
        const auto& stmt = pbs.getBody();

        if (stmt.kind == slang::ast::StatementKind::Timed) {
          const auto& timed = stmt.as<slang::ast::TimedStatement>();
          if (timed.stmt.kind == slang::ast::StatementKind::Block) {
            const auto& block = timed.stmt.as<slang::ast::BlockStatement>();
            I(block.body.kind == slang::ast::StatementKind::List);
            for (const auto& bstmt : block.body.as<slang::ast::StatementList>().list) {
              if (bstmt->kind == slang::ast::StatementKind::ExpressionStatement) {
                const auto& expr = bstmt->as<slang::ast::ExpressionStatement>().expr;
                set_pending_loc(expr.sourceRange);
                bool ok = process(expr.as<slang::ast::AssignmentExpression>());
                clear_pending_loc();
                if (!ok) {
                  lnast_builder.lnast = nullptr;
                  return false;
                }
              } else {
                emit_unsupported(bstmt->sourceRange, "unsupported-statement",
                                 "only assignment statements are supported inside always blocks");
                lnast_builder.lnast = nullptr;
                return false;
              }
            }
          }

        } else {
          emit_unsupported(stmt.sourceRange, "unsupported-always",
                           "always block without a sensitivity list is not supported by --reader slang",
                           "use always_comb / always @(...) with a supported body");
          lnast_builder.lnast = nullptr;
          return false;
        }
      }
    } else if (member.kind == slang::ast::SymbolKind::Instance) {
      emit_unsupported(member.location, "unsupported-instance",
                       std::string("submodule instance '") + std::string(member.name)
                           + "' is not supported by --reader slang yet (design hierarchy lands in 2s)",
                       "flatten the design, or use --reader yosys-slang for hierarchical Verilog");
      lnast_builder.lnast = nullptr;
      return false;
    } else {
      emit_unsupported(member.location, "unsupported-member",
                       std::string("module member '") + std::string(member.name) + "' is not supported by --reader slang");
      lnast_builder.lnast = nullptr;
      return false;
    }
  }

  // lnast_builder.lnast->dump();

  parsed_lnasts.insert_or_assign(def.name, std::move(lnast_builder.lnast));
  lnast_builder.lnast = nullptr;

  return true;
}

void Slang_tree::process_lhs(const slang::ast::Expression& lhs, const std::string& rhs_var, bool last_value) {
  std::string var_name;
  bool        dest_var_sign;
  int         dest_var_bits;

  std::string dest_max_bit;
  std::string dest_min_bit;

  if (lhs.kind == slang::ast::ExpressionKind::NamedValue) {
    const auto& var = lhs.as<slang::ast::NamedValueExpression>();
    var_name        = lnast_builder.get_lnast_lhs_name(var.symbol.name);
    dest_var_sign   = var.type->isSigned();
    dest_var_bits   = var.type->getBitWidth();
    I(!var.type->isStruct());  // FIXME: structs
  } else if (lhs.kind == slang::ast::ExpressionKind::ElementSelect) {
    const auto& es = lhs.as<slang::ast::ElementSelectExpression>();
    I(es.value().kind == slang::ast::ExpressionKind::NamedValue);

    dest_max_bit = process_expression(es.selector(), last_value);
    dest_min_bit = dest_max_bit;

    const auto& var = es.value().as<slang::ast::NamedValueExpression>();
    var_name        = lnast_builder.get_lnast_lhs_name(var.symbol.name);

    dest_var_sign = false;
    dest_var_bits = 1;
  } else {
    I(lhs.kind == slang::ast::ExpressionKind::RangeSelect);
    const auto& rs = lhs.as<slang::ast::RangeSelectExpression>();
    I(rs.value().kind == slang::ast::ExpressionKind::NamedValue);

    const auto& var = rs.value().as<slang::ast::NamedValueExpression>();
    var_name        = lnast_builder.get_lnast_lhs_name(var.symbol.name);
    dest_var_sign   = var.type->isSigned();
    dest_var_bits   = var.type->getBitWidth();
    I(!var.type->isStruct());  // FIXME: structs

    dest_max_bit = process_expression(rs.left(), last_value);
    dest_min_bit = process_expression(rs.right(), last_value);
  }

  auto it = lnast_builder.vname2lname.find(var_name);
  if (it == lnast_builder.vname2lname.end()) {
    lnast_builder.vname2lname.emplace(var_name, var_name);
    lnast_builder.create_declare_bits_stmts(var_name, dest_var_sign, dest_var_bits);
    if (dest_var_sign) {
      lnast_builder.create_assign_stmts(var_name, "0sb?");
    } else {
      std::string qmarks(dest_var_bits, '?');

      lnast_builder.create_assign_stmts(var_name,
                                        absl::StrCat("0b", qmarks));  // mark with x so that potential use is poison if needed
    }
  }

  if (dest_min_bit.empty() && dest_max_bit.empty()) {
    lnast_builder.create_assign_stmts(var_name, rhs_var);
  } else {
    auto bitmask = lnast_builder.create_bitmask_stmts(dest_max_bit, dest_min_bit);
#ifdef LNASTOP_DONE
    lnast_builder.create_set_mask_stmts(var_name, bitmask, rhs_var);
#else
    auto tmp_var = lnast_builder.create_lnast_tmp();
    lnast_builder.create_assign_stmts(tmp_var, var_name);
    lnast_builder.create_set_mask_stmts(tmp_var, bitmask, rhs_var);
    lnast_builder.create_assign_stmts(var_name, tmp_var);
#endif
  }
}

bool Slang_tree::process(const slang::ast::AssignmentExpression& expr) {
  auto rhs_var = process_expression(expr.right(), true);

  const auto& lhs = expr.left();

  process_lhs(lhs, rhs_var, true);

  return true;
}

std::string Slang_tree::process_expression(const slang::ast::Expression& expr, bool last_value) {
  if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
    const auto& nv = expr.as<slang::ast::NamedValueExpression>();
    return lnast_builder.get_lnast_name(nv.symbol.name, last_value);
  }

  if (expr.kind == slang::ast::ExpressionKind::IntegerLiteral) {
    const auto& il    = expr.as<slang::ast::IntegerLiteral>();
    auto        svint = il.getValue();

    if (svint.hasUnknown()) {  // unknowns in binary
      auto buffer = svint.toString(slang::LiteralBase::Binary, false, 32768);
      return absl::StrCat("0b", std::string_view(buffer.data(), buffer.size()));
    }

    if (svint.getMinRepresentedBits() < 8) {  // small numbers in decimal (easier to read)
      auto buffer = svint.toString(slang::LiteralBase::Decimal, false, 32768);
      return std::string(buffer.data(), buffer.size());
    }

    auto buffer = svint.toString(slang::LiteralBase::Hex, false, 32768);
    return absl::StrCat("0x", std::string_view(buffer.data(), buffer.size()));
  }

  if (expr.kind == slang::ast::ExpressionKind::BinaryOp) {
    const auto& op  = expr.as<slang::ast::BinaryExpression>();
    auto        lhs = process_expression(op.left(), last_value);
    auto        rhs = process_expression(op.right(), last_value);

    std::string var;
    switch (op.op) {
      case slang::ast::BinaryOperator::Add: var = lnast_builder.create_plus_stmts(lhs, rhs); break;
      case slang::ast::BinaryOperator::Subtract: var = lnast_builder.create_minus_stmts(lhs, rhs); break;
      case slang::ast::BinaryOperator::Multiply: var = lnast_builder.create_mult_stmts(lhs, rhs); break;
      case slang::ast::BinaryOperator::Divide: var = lnast_builder.create_div_stmts(lhs, rhs); break;
      case slang::ast::BinaryOperator::Mod: var = lnast_builder.create_mod_stmts(lhs, rhs); break;
      case slang::ast::BinaryOperator::BinaryAnd: var = lnast_builder.create_bit_and_stmts(lhs, rhs); break;
      case slang::ast::BinaryOperator::BinaryOr: var = lnast_builder.create_bit_or_stmts({lhs, rhs}); break;
      case slang::ast::BinaryOperator::BinaryXor: var = lnast_builder.create_bit_xor_stmts(lhs, rhs); break;
      case slang::ast::BinaryOperator::BinaryXnor:
        var = lnast_builder.create_bit_not_stmts(lnast_builder.create_bit_xor_stmts(lhs, rhs));
        break;
      default: {
        emit_unsupported(expr.sourceRange, "unsupported-binary-op",
                         "this binary operator is not supported by --reader slang yet");
        return "0";
      }
    }

    auto bw = std::to_string(op.type->getBitWidth());
    if (op.type->isSigned()) {
      return lnast_builder.create_sext_stmts(var, bw);
    }

    auto mask = lnast_builder.create_mask_stmts(bw);
    return lnast_builder.create_bit_and_stmts(var, mask);
  }

  if (expr.kind == slang::ast::ExpressionKind::UnaryOp) {
    const auto& op = expr.as<slang::ast::UnaryExpression>();

    auto lhs = process_expression(op.operand(), last_value);
    switch (op.op) {
      case slang::ast::UnaryOperator::BitwiseNot: return lnast_builder.create_bit_not_stmts(lhs);
      case slang::ast::UnaryOperator::LogicalNot: return lnast_builder.create_log_not_stmts(lhs);
      case slang::ast::UnaryOperator::Plus: return lhs;
      case slang::ast::UnaryOperator::Minus: return lnast_builder.create_minus_stmts("0", lhs);
      case slang::ast::UnaryOperator::BitwiseAnd: return lnast_builder.create_red_and_stmts(lhs);
      case slang::ast::UnaryOperator::BitwiseNand:
        return lnast_builder.create_bit_not_stmts(lnast_builder.create_red_and_stmts(lhs));
      case slang::ast::UnaryOperator::BitwiseXor: return lnast_builder.create_red_xor_stmts(lhs);
      case slang::ast::UnaryOperator::BitwiseXnor:
        return lnast_builder.create_bit_not_stmts(lnast_builder.create_red_xor_stmts(lhs));
      case slang::ast::UnaryOperator::BitwiseOr: return lnast_builder.create_red_or_stmts(lhs);
      // do I use bit not or logical not?
      // Also is it ok for it to be two connected references if we have no lnast node?
      case slang::ast::UnaryOperator::BitwiseNor:
        return lnast_builder.create_bit_not_stmts(lnast_builder.create_red_or_stmts(lhs));
        // case UnaryOperator::Preincrement:
        // case UnaryOperator::Predecrement:
        // case UnaryOperator::Postincrement:
        // case UnaryOperator::Postdecrement:
      default: {
        emit_unsupported(expr.sourceRange, "unsupported-unary-op",
                         "this unary operator is not supported by --reader slang yet");
        return "0";
      }
    }
  }

  if (expr.kind == slang::ast::ExpressionKind::Conversion) {
    const auto&             conv    = expr.as<slang::ast::ConversionExpression>();
    const slang::ast::Type* to_type = conv.type;

    auto res = process_expression(conv.operand(), last_value);  // NOTHING TO DO? (the dp_assign handles it?)

    const slang::ast::Type* from_type = conv.operand().type;

    if (to_type->isSigned() == from_type->isSigned() && to_type->getBitWidth() >= from_type->getBitWidth()) {
      return res;  // no need to add mask if expanding
    }

    auto min_bits = std::to_string(std::min(to_type->getBitWidth(), from_type->getBitWidth()));

    if (to_type->isSigned()) {
      return lnast_builder.create_sext_stmts(res, min_bits);
    }

    I(!to_type->isSigned());
    // and(and(X,a),b) -> and(X,min(a,b))
    auto bw   = std::to_string(to_type->getBitWidth());
    auto mask = lnast_builder.create_mask_stmts(bw);
    return lnast_builder.create_bit_and_stmts(res, mask);
  }

  if (expr.kind == slang::ast::ExpressionKind::Concatenation) {
    const auto& concat = expr.as<slang::ast::ConcatenationExpression>();

    auto offset = 0u;

    std::vector<std::string> adjusted_fields;

    for (const auto& e : concat.operands()) {
      auto bits = e->type->getBitWidth();

      auto res_var = process_expression(*e, last_value);

      if (offset) {
        res_var = lnast_builder.create_shl_stmts(res_var, std::to_string(offset));
      }
      adjusted_fields.emplace_back(res_var);

      offset += bits;
    }

    return lnast_builder.create_bit_or_stmts(adjusted_fields);
  }

  if (expr.kind == slang::ast::ExpressionKind::ElementSelect) {
    const auto& es = expr.as<slang::ast::ElementSelectExpression>();
    I(es.value().kind == slang::ast::ExpressionKind::NamedValue);

    const auto& var = es.value().as<slang::ast::NamedValueExpression>();

    auto sel_var  = lnast_builder.get_lnast_name(var.symbol.name, last_value);
    auto sel_bit  = process_expression(es.selector(), last_value);
    auto sel_mask = lnast_builder.create_shl_stmts("1", sel_bit);

    return lnast_builder.create_get_mask_stmts(sel_var, sel_mask);
  }

  if (expr.kind == slang::ast::ExpressionKind::RangeSelect) {
    const auto& es = expr.as<slang::ast::RangeSelectExpression>();
    I(es.value().kind == slang::ast::ExpressionKind::NamedValue);

    const auto& var = es.value().as<slang::ast::NamedValueExpression>();

    auto sel_var = lnast_builder.get_lnast_name(var.symbol.name, last_value);
    auto max_bit = process_expression(es.left(), last_value);
    auto min_bit = process_expression(es.right(), last_value);

    auto sel_mask = lnast_builder.create_bitmask_stmts(max_bit, min_bit);
    return lnast_builder.create_get_mask_stmts(sel_var, sel_mask);
  }

  // CIRCT-style default fallback: any AST expression kind the importer does not
  // handle becomes a located diagnostic instead of slipping through silently.
  emit_unsupported(expr.sourceRange, "unsupported-expression",
                   "this expression is not supported by --reader slang yet");
  return "0";
}
