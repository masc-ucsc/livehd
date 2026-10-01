// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <stdexcept>

#include "sim_program.hpp"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang_context.hpp"

using namespace slang::ast;
using Sim_node = livehd::sim_ir::Node;

namespace {
struct Plusarg_calls : ASTVisitor<Plusarg_calls, VisitFlags::AllGood> {
  bool found = false;
  void handle(const CallExpression& call) {
    if (call.isSystemCall() && (call.getSubroutineName() == "$test$plusargs" || call.getSubroutineName() == "$value$plusargs")) {
      found = true;
    }
    visitDefault(call);
  }
};
struct Plusarg_blocks : ASTVisitor<Plusarg_blocks, VisitFlags::AllGood> {
  std::vector<const ProceduralBlockSymbol*> blocks;
  std::vector<const VariableSymbol*>        initializers;
  void                                      handle(const VariableSymbol& var) {
    if (const auto* init = var.getInitializer()) {
      Plusarg_calls calls;
      init->visit(calls);
      if (calls.found) {
        initializers.push_back(&var);
      }
    }
  }
  void handle(const InstanceSymbol&) {}  // Each child is checked in its own module context.
  void handle(const ProceduralBlockSymbol& block) {
    Plusarg_calls calls;
    block.getBody().visit(calls);
    if (calls.found) {
      blocks.push_back(&block);
    }
  }
};
struct Debug_vars : ASTVisitor<Debug_vars, VisitFlags::AllGood> {
  std::vector<const ValueSymbol*>         vars;
  absl::flat_hash_set<const ValueSymbol*> local_declarations;
  void                                    add(const ValueSymbol& sym) {
    if (sym.kind == SymbolKind::Parameter || sym.kind == SymbolKind::EnumValue) {
      return;
    }
    if (std::find(vars.begin(), vars.end(), &sym) == vars.end()) {
      vars.push_back(&sym);
    }
  }
  void handle(const ValueExpressionBase& value) { add(value.symbol); }
  void handle(const VariableDeclStatement& stmt) {
    local_declarations.insert(&stmt.symbol);
    add(stmt.symbol);
    visitDefault(stmt);
  }
};
}  // namespace

// Elaborate debug initialization once per physical instance into the root.
// Its variables are block-private, so circuit flattening and dead logic removal
// cannot duplicate or erase simulation effects.
void Slang_context::hoist_plusargs(const RootSymbol& root) {
  // Check the whole elaboration as well: a parent can read a child's private
  // variable via a hierarchical reference. Canonicalized instances can use
  // distinct symbol objects for the same declaration, so compare locations too.
  struct Isolation : ASTVisitor<Isolation, VisitFlags::AllGood> {
    Slang_context& ctx;
    explicit Isolation(Slang_context& c) : ctx(c) {}
    bool is_debug(const ValueSymbol& sym) const {
      return std::any_of(ctx.simulation_only_vars_.begin(), ctx.simulation_only_vars_.end(), [&](const auto* var) {
        return var == &sym || var->location == sym.location;
      });
    }
    void handle(const ProceduralBlockSymbol& block) {
      Plusarg_calls calls;
      block.getBody().visit(calls);
      if (!calls.found) {
        visitDefault(block);
      }
    }
    void handle(const VariableSymbol& var) {
      if (!is_debug(var)) {
        visitDefault(var);
      }
    }
    void handle(const ValueExpressionBase& value) {
      if (is_debug(value.symbol)) {
        ctx.emit_unsupported(value.sourceRange,
                             "plusarg-hardware",
                             "plusarg debug state cannot escape its initial block through hierarchy");
      }
    }
  } isolation(*this);
  if (!simulation_only_vars_.empty()) {
    root.visit(isolation);
  }
  std::vector<std::pair<std::shared_ptr<Lnast>, std::string>> roots;
  for (const auto* top : root.topInstances) {
    Sim_node program{"seq", "", 0, false, {}};
    struct Collect : ASTVisitor<Collect, VisitFlags::AllGood> {
      Slang_context& ctx;
      Sim_node&      program;
      Collect(Slang_context& c, Sim_node& p) : ctx(c), program(p) {}
      void handle(const InstanceSymbol& inst) {
        const auto* body = inst.getCanonicalBody();
        if (!body) {
          body = &inst.body;
        }
        if (auto it = ctx.lowered_.find(body); it != ctx.lowered_.end() && it->second) {
          const auto payload = it->second->get_simulation_init();
          if (!payload.empty()) {
            program.kids.push_back(livehd::sim_ir::decode(payload));
          }
        }
        body->visit(*this);
      }
    } collect(*this, program);
    collect.handle(*top);
    const auto* body = top->getCanonicalBody();
    if (!body) {
      body = &top->body;
    }
    if (auto it = lowered_.find(body); it != lowered_.end() && it->second && !program.kids.empty()) {
      roots.emplace_back(it->second, livehd::sim_ir::encode(program));
    }
  }
  for (const auto& [body, ln] : lowered_) {
    if (ln) {
      ln->set_simulation_init("");
    }
  }
  for (const auto& [ln, program] : roots) {
    ln->set_simulation_init(program);
  }
}

void Slang_context::prepare_plusargs(const InstanceBodySymbol& body) {
  Plusarg_blocks scan;
  body.visit(scan);
  for (const auto* var : scan.initializers) {
    emit_unsupported(var->location, "plusarg-placement", "put plusarg reads in a simulation-only initial block");
  }
  if (scan.blocks.empty()) {
    return;
  }
  std::map<const ValueSymbol*, const ProceduralBlockSymbol*> owners;
  Sim_node                                                   all{"seq", "", 0, false, {}};
  for (const auto* block : scan.blocks) {
    if (block->procedureKind != ProceduralBlockKind::Initial) {
      emit_unsupported(block->location, "plusarg-placement", "plusarg reads currently require a simulation-only initial block");
      continue;
    }
    plusarg_blocks_.insert(block);
    Debug_vars vars;
    block->getBody().visit(vars);
    for (const auto* var : vars.vars) {
      plusarg_vars_.insert(var);
      simulation_only_vars_.insert(var);
      if (input_syms_.contains(var) || output_syms_.contains(var)) {
        emit_unsupported(var->location, "plusarg-hardware", "plusarg initialization cannot read or write hardware ports");
      }
      if (auto [it, fresh] = owners.emplace(var, block); !fresh && it->second != block) {
        emit_unsupported(var->location, "plusarg-shared-state", "a plusarg debug variable must belong to one initial block");
      }
    }
    std::map<const ValueSymbol*, Sim_node> references;
    for (const auto* var : vars.vars) {
      const auto& type = var->getType().getCanonicalType();
      if (!type.isString() && (!type.isIntegral() || type.getBitWidth() > 64)) {
        emit_unsupported(var->location,
                         "plusarg-type",
                         "plusarg debug variables require strings or integral values of at most 64 bits");
        continue;
      }
      references.emplace(var,
                         Sim_node{"var",
                                  std::to_string(references.size()),
                                  type.isString() ? 0 : static_cast<int>(type.getBitWidth()),
                                  type.isSigned(),
                                  {}});
    }
    auto literal_string = [&](const Expression& e) {
      const auto value = try_eval(e);
      if (!value || (!value->isString() && !value->isInteger())) {
        throw std::runtime_error("plusarg and debug format strings must be constant");
      }
      return value->convertToStr().str();
    };
    // Slang binds output actuals as Assignment(lhs, EmptyArgument).
    auto target = [&](const Expression& e) -> Sim_node {
      const Expression* lhs = &e;
      if (lhs->kind == ExpressionKind::Assignment) {
        lhs = &lhs->as<AssignmentExpression>().left();
      }
      if (lhs->kind != ExpressionKind::NamedValue) {
        throw std::runtime_error("$value$plusargs destination must be a whole local debug variable");
      }
      return references.at(&lhs->as<NamedValueExpression>().symbol);
    };
    std::function<Sim_node(const Expression&)> expression;
    expression = [&](const Expression& e) -> Sim_node {
      if (e.kind == ExpressionKind::StringLiteral) {
        if (auto value = try_eval(e)) {
          return {"str", value->convertToStr().str(), 0, false, {}};
        }
      }
      const auto& type = e.type->getCanonicalType();
      const int   bits = type.isIntegral() ? static_cast<int>(type.getBitWidth()) : 0;
      const bool  sign = type.isSigned();
      if (bits > 64) {
        throw std::runtime_error("plusarg debug expressions wider than 64 bits are unsupported");
      }
      if (e.kind == ExpressionKind::NamedValue) {
        const auto& sym = e.as<NamedValueExpression>().symbol;
        if (auto it = references.find(&sym); it != references.end()) {
          return it->second;
        }
      }
      if (e.kind == ExpressionKind::Conversion) {
        return {"cast", "", bits, sign, {expression(e.as<ConversionExpression>().operand())}};
      }
      if (e.kind == ExpressionKind::Call) {
        const auto& call = e.as<CallExpression>();
        const auto  args = call.arguments();
        if (!call.isSystemCall()) {
          throw std::runtime_error("user subroutines in plusarg initialization are unsupported");
        }
        const auto name = call.getSubroutineName();
        if (name == "$test$plusargs" && args.size() == 1) {
          return {"test", literal_string(*args[0]), 32, true, {}};
        }
        if (name == "$value$plusargs" && args.size() == 2) {
          const auto fmt        = literal_string(*args[0]);
          // One format conversion; preserve prefix text and escaped percent.
          char       conversion = 0;
          for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] != '%') {
              continue;
            }
            if (++i == fmt.size()) {
              break;
            }
            if (fmt[i] == '%') {
              continue;
            }
            if (fmt[i] == '0' && ++i == fmt.size()) {
              break;
            }
            conversion = static_cast<char>(std::tolower(static_cast<unsigned char>(fmt[i])));
            break;
          }
          if (!conversion || std::string("dbhxos").find(conversion) == std::string::npos) {
            throw std::runtime_error("$value$plusargs supports %d, %b, %h, %x, %o and %s conversions");
          }
          auto dst = target(*args[1]);
          if (dst.bits == 0 && conversion != 's') {
            throw std::runtime_error("string plusarg destinations require %s");
          }
          return {"value", fmt, 32, true, {std::move(dst)}};
        }
        throw std::runtime_error("unsupported expression call in plusarg initialization: " + std::string(name));
      }
      if (e.kind == ExpressionKind::UnaryOp) {
        const auto& u = e.as<UnaryExpression>();
        std::string op;
        switch (u.op) {
          case UnaryOperator::LogicalNot: op = "!"; break;
          case UnaryOperator::BitwiseNot: op = "~"; break;
          case UnaryOperator::Minus     : op = "-"; break;
          case UnaryOperator::Plus      : op = "+"; break;
          default                       : throw std::runtime_error("unsupported unary operation in plusarg initialization");
        }
        return {"unary", op, bits, sign, {expression(u.operand())}};
      }
      if (e.kind == ExpressionKind::BinaryOp) {
        const auto& b  = e.as<BinaryExpression>();
        auto        op = std::string(OpInfo::getText(b.op));
        if (op == "===") {
          op = "==";
        }
        if (op == "!==") {
          op = "!=";
        }
        if (std::string(" + - * & | ^ == != < <= > >= && || ").find(" " + op + " ") == std::string::npos) {
          throw std::runtime_error("unsupported binary operation in plusarg initialization");
        }
        return {
            "binary",
            op,
            bits,
            sign,
            {expression(b.left()), expression(b.right())}
        };
      }
      if (auto value = try_eval(e)) {
        if (value->isString()) {
          return {"str", value->str(), 0, false, {}};
        }
        if (value->isInteger() && !value->integer().hasUnknown()) {
          auto number = value->integer().as<uint64_t>();
          if (!number) {
            if (auto signed_number = value->integer().as<int64_t>()) {
              number = static_cast<uint64_t>(*signed_number);
            }
          }
          if (number) {
            return {"num", std::to_string(*number), bits, sign, {}};
          }
        }
      }
      throw std::runtime_error("unsupported expression in plusarg initialization");
    };
    std::function<Sim_node(const Statement&)> statement;
    statement = [&](const Statement& s) -> Sim_node {
      Sim_node result{"seq", "", 0, false, {}};
      if (s.kind == StatementKind::Empty) {
        return result;
      }
      if (s.kind == StatementKind::Block) {
        return statement(s.as<BlockStatement>().body);
      }
      if (s.kind == StatementKind::List) {
        for (auto* item : s.as<StatementList>().list) {
          result.kids.push_back(statement(*item));
        }
        return result;
      }
      if (s.kind == StatementKind::VariableDeclaration) {
        const auto& sym = s.as<VariableDeclStatement>().symbol;
        if (auto init = sym.getInitializer()) {
          return {
              "set",
              "",
              0,
              false,
              {references.at(&sym), expression(*init)}
          };
        }
        return result;
      }
      if (s.kind == StatementKind::Conditional) {
        const auto& cond = s.as<ConditionalStatement>();
        if (cond.conditions.size() != 1 || cond.conditions[0].pattern) {
          throw std::runtime_error("pattern conditions in plusarg initialization are unsupported");
        }
        result = {
            "if",
            "",
            0,
            false,
            {expression(*cond.conditions[0].expr), statement(cond.ifTrue)}
        };
        if (cond.ifFalse) {
          result.kids.push_back(statement(*cond.ifFalse));
        }
        return result;
      }
      if (s.kind == StatementKind::ImmediateAssertion) {
        const auto& assertion = s.as<ImmediateAssertionStatement>();
        if (assertion.isDeferred || assertion.assertionKind != AssertionKind::Assert) {
          throw std::runtime_error("plusarg initialization supports immediate assert only");
        }
        if (assertion.ifTrue || assertion.ifFalse) {
          Sim_node action{
              "if",
              "",
              0,
              false,
              {expression(assertion.cond), assertion.ifTrue ? statement(*assertion.ifTrue) : Sim_node{"seq", "", 0, false, {}}}
          };
          action.kids.push_back(assertion.ifFalse
                                    ? statement(*assertion.ifFalse)
                                    : Sim_node{"fatal", "SystemVerilog plusarg initialization assertion failed", 0, false, {}});
          return action;
        }
        return {"assert", "SystemVerilog plusarg initialization assertion failed", 0, false, {expression(assertion.cond)}};
      }
      if (s.kind == StatementKind::ExpressionStatement) {
        const auto& e = s.as<ExpressionStatement>().expr;
        if (e.kind == ExpressionKind::Assignment) {
          const auto& a = e.as<AssignmentExpression>();
          if (a.isNonBlocking() || a.isCompound()) {
            throw std::runtime_error("plusarg initialization requires simple blocking assignments");
          }
          return {
              "set",
              "",
              0,
              false,
              {target(a.left()), expression(a.right())}
          };
        }
        if (e.kind == ExpressionKind::Call) {
          const auto& call = e.as<CallExpression>();
          const auto  name = call.isSystemCall() ? call.getSubroutineName() : std::string_view{};
          if (name == "$display" || name == "$write" || name == "$fatal") {
            const auto   args  = call.arguments();
            const size_t first = name == "$fatal" ? 1 : 0;
            result             = {name == "$fatal" ? "fatal" : name == "$display" ? "display" : "write", "", 0, false, {}};
            if (args.size() > first) {
              result.text = literal_string(*args[first]);
            }
            for (size_t i = first + 1; i < args.size(); ++i) {
              result.kids.push_back(expression(*args[i]));
            }
            return result;
          }
        }
        return {"expr", "", 0, false, {expression(e)}};
      }
      throw std::runtime_error("timed or unsupported statement in plusarg initialization");
    };
    try {
      Sim_node program{"seq", "", 0, false, {}};
      for (const auto* sym : vars.vars) {
        auto decl = references.at(sym);
        decl.op   = "decl";
        decl.kids.push_back(decl.bits ? Sim_node{"num", "0", decl.bits, decl.sign, {}} : Sim_node{"str", "", 0, false, {}});
        // Module-scope declarations initialize before their initial process.
        if (!vars.local_declarations.contains(sym) && sym->kind == SymbolKind::Variable) {
          if (const auto* init = sym->as<VariableSymbol>().getInitializer()) {
            if (!try_eval(*init)) {
              throw std::runtime_error("module-scope debug initializers must be constant; assign inside the initial block");
            }
            decl.kids[0] = expression(*init);
          }
        }
        program.kids.push_back(std::move(decl));
      }
      program.kids.push_back(statement(block->getBody()));
      (void)livehd::sim_ir::statement(program);  // Validate the complete backend vocabulary now.
      all.kids.push_back(std::move(program));
    } catch (const std::exception& e) {
      emit_unsupported(block->location, "plusarg-initialization", e.what());
    }
  }
  // Every variable in an extracted block belongs exclusively to debug. Reject
  // ALL external references, which covers both data and control dependencies,
  // ports/instances, aliases, and reads from another procedural block.
  struct Check : ASTVisitor<Check, VisitFlags::AllGood> {
    Slang_context& ctx;
    explicit Check(Slang_context& c) : ctx(c) {}
    void handle(const InstanceSymbol& inst) { visitDefault(inst); }
    void handle(const ProceduralBlockSymbol& block) {
      if (!ctx.plusarg_blocks_.contains(&block)) {
        visitDefault(block);
      }
    }
    void handle(const VariableSymbol& var) {
      if (!ctx.plusarg_vars_.contains(&var)) {
        visitDefault(var);
      }
    }
    void handle(const ValueExpressionBase& value) {
      if (ctx.plusarg_vars_.contains(&value.symbol)) {
        ctx.emit_unsupported(value.sourceRange,
                             "plusarg-hardware",
                             "plusarg debug state is referenced outside its simulation-only initial block");
      }
    }
  } check(*this);
  body.visit(check);
  builder_.lnast->set_simulation_init(livehd::sim_ir::encode(all));
}
