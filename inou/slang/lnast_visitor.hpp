
#pragma once

#include "fmt/format.h"

#include "slang/compilation/Compilation.h"
#include "slang/symbols/ASTSerializer.h"
#include "slang/symbols/ASTVisitor.h"
#include "slang/symbols/CompilationUnitSymbols.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"

struct Lnast_visitor : public slang::ASTVisitor<Lnast_visitor, false, false> {

  Lnast_visitor(slang::Compilation& _compilation, const size_t& _numErrors, uint32_t _errorLimit) :
    compilation(_compilation), numErrors(_numErrors), errorLimit(_errorLimit) {}

  template<typename T>
    void handle(const T& symbol) {
      handleDefault(symbol);
    }

  template<typename T>
    bool handleDefault(const T& symbol) {
      if (numErrors > errorLimit)
        return false;

      if constexpr (std::is_base_of_v<slang::Symbol, T>) {
        auto declaredType = symbol.getDeclaredType();
        if (declaredType) {
          declaredType->getType();
          declaredType->getInitializer();
        }

        if constexpr (std::is_same_v<slang::ParameterSymbol, T> ||
            std::is_same_v<slang::EnumValueSymbol, T>) {
          const auto &svint = symbol.getValue().integer();
          fmt::print("parameter:{} value val:{} bits:{}\n", symbol.name, *svint.getRawPtr(), svint.getActiveBits());
        }

        for (auto attr : compilation.getAttributes(symbol))
          attr->getValue();
      }

      if constexpr (slang::is_detected_v<getBody_t, T>)
        symbol.getBody().visit(*this);

      visitDefault(symbol);
      return true;
    }

  void handle(const slang::ExplicitImportSymbol& symbol) {
    if (!handleDefault(symbol))
      return;
    symbol.importedSymbol();
  }

  void handle(const slang::WildcardImportSymbol& symbol) {
    if (!handleDefault(symbol))
      return;
    symbol.getPackage();
  }

  void handle(const slang::IntegerLiteral& expr) {
    const auto &svint = expr.getValue();
    if (svint.isSingleWord())
      fmt::print("value val:{} bits:{}\n", *svint.getRawPtr(), svint.getActiveBits());
    else
      fmt::print("value bits:{}\n", svint.getActiveBits());
  }

  void handle(const slang::VariableDeclStatement& stmt) { fmt::print("var decl:{}\n", stmt.symbol.name); }

  void handle(const slang::ContinuousAssignSymbol& symbol) {
    if (!handleDefault(symbol))
      return;
    symbol.getAssignment();
  }

  void handle(const slang::InstanceSymbol& symbol) {
    if (numErrors > errorLimit)
      return;

    const auto &def = symbol.getDefinition();
    fmt::print("definition:{}\n", def.name);

    symbol.resolvePortConnections();
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

  void handle(const slang::GenerateBlockSymbol& symbol) {
    if (!symbol.isInstantiated)
      return;
    handleDefault(symbol);
  }

  slang::Compilation& compilation;
  const size_t& numErrors;
  flat_hash_set<const slang::InstanceBodySymbol*> visitedInstanceBodies;
  uint32_t errorLimit;
  uint32_t hierarchyDepth = 0;
};

