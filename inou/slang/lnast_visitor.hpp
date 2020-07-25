
#pragma once

#include "fmt/format.h"

#include "slang/compilation/Compilation.h"
#include "slang/symbols/ASTSerializer.h"
#include "slang/symbols/ASTVisitor.h"
#include "slang/symbols/CompilationUnitSymbols.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"

class Lnast_visitor : public slang::ASTVisitor<Lnast_visitor, false, false> {
protected:
  slang::Compilation& compilation;
  const size_t& numErrors;
  flat_hash_set<const slang::InstanceBodySymbol*> visitedInstanceBodies;
  uint32_t errorLimit;
  uint32_t hierarchyDepth = 0;

public:
  Lnast_visitor(slang::Compilation& _compilation, const size_t& _numErrors, uint32_t _errorLimit);

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

  void handle(const slang::ExplicitImportSymbol& symbol);
  void handle(const slang::WildcardImportSymbol& symbol);
  void handle(const slang::IntegerLiteral& expr);
  void handle(const slang::VariableDeclStatement& stmt);
  void handle(const slang::ContinuousAssignSymbol& symbol);
	void handle(const slang::AssignmentExpression& expr);
  void handle(const slang::ExpressionStatement& expr);
	void handle(const slang::StatementBlockSymbol& symbol);
	void handle(const slang::ProceduralBlockSymbol& expr);
  void handle(const slang::InstanceSymbol& symbol);
  void handle(const slang::GenerateBlockSymbol& symbol);
};

