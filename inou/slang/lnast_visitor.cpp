
#include "lnast_visitor.hpp"

#include "iassert.hpp"

using namespace slang; // just inside this file

Lnast_visitor::Lnast_visitor(slang::Compilation& _compilation, const size_t& _numErrors, uint32_t _errorLimit) :
  compilation(_compilation), numErrors(_numErrors), errorLimit(_errorLimit) {}

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
  const auto &svint = expr.getValue();
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

void Lnast_visitor::handle(const slang::AssignmentExpression& expr) {
  if (numErrors > errorLimit)
    return;
    const auto &lhs =  expr.left();
    if (lhs.kind == ExpressionKind::NamedValue) {
      const auto &var = lhs.as<NamedValueExpression>();
      fmt::print("bits:{} {} =  \n", var.type->getBitWidth(), var.symbol.name);
    } else {
      fmt::print("TODO. What is this\n");
    }
    fmt::print("handle recursively the rhs (binaryOp,unaryop,...)\n");
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
  if (numErrors > errorLimit)
    return;
  fmt::print("HERE.3\n");
}


void Lnast_visitor::handle(const slang::ProceduralBlockSymbol& pblock) {
  if (numErrors > errorLimit)
    return;
  fmt::print("HERE.2\n");

  auto& stmt = pblock.getBody();
  if (stmt.kind == StatementKind::Timed) {
    auto &timed = stmt.as<TimedStatement>();
    fmt::print("TODO: check that the sentitivity list is completed (no implicit latch)\n");
    if (timed.stmt.kind == StatementKind::Block) {
      auto &block = timed.stmt.as<BlockStatement>();
      I(block.getStatements().kind == StatementKind::List); // TODO: fix if single statement
      for (auto& stmt : block.getStatements().as<StatementList>().list) {
        if (stmt->kind == StatementKind::ExpressionStatement) {
          handle(stmt->as<ExpressionStatement>()); // This call handle ExpressionStatement
        } else
          fmt::print("TODO: handle kind {}\n", stmt->kind);
      }
    }
  }
}


void Lnast_visitor::handle(const slang::InstanceSymbol& symbol) {
  if (numErrors > errorLimit)
    return;

  const auto &def = symbol.getDefinition();
  fmt::print("definition:{}\n", def.name);

  symbol.resolvePortConnections();
  for (const auto& p : symbol.body.getPortList()) {
    if (p->kind == SymbolKind::Port) {
      const auto& port = p->as<PortSymbol>();
      I(port.defaultValue==nullptr); // give me a case to DEBUG
      fmt::print("port:{} dir:{} bits:{}\n", port.name, port.direction == PortDirection::In? "in":"out", port.getType().getBitWidth());
    }else if (p->kind == SymbolKind::InterfacePort) {
      const auto& port = p->as<InterfacePortSymbol>();
      fmt::print("port:{} FIXME\n", p->name);
    } else {
      I(false); // What other type?
    }
  }

  for (auto attr : compilation.getAttributes(symbol))
    attr->getValue();

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
