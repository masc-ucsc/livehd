//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Parametric type aliases (`type Req<W=32> = (addr:Unsigned(bits=W), valid:Bool)`).
//
// An alias with generics is a TEMPLATE: it lowers to nothing by itself. Every
// application (`Req<16>`, `pkg.Req<W, K=2>`, a bare `Req` when every generic has a
// default) is expanded where it is written, before the construct that holds it
// is lowered: the alias body is rewritten with each generic replaced by its
// argument (token-level, so a field label or a `.field` named like a generic is
// untouched) and parsed as an ordinary type, so every position that accepts an
// inline type accepts the application (ports, locals, `reg`, tuple fields, array
// elements, generic arguments, other aliases). The expansion is purely
// structural: `Req<32>` is the tuple type `(addr:Unsigned(bits=32), valid:Bool)`.
//
// These helpers are string-level and independent of the lowering: Prp2lnast owns
// the registry, the import lookup and the AST splice.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "prpparse/ast.hpp"

namespace prp_type_alias {

struct Param {
  std::string name;
  std::string default_text;
  bool        has_default = false;
};

struct Template {
  std::string        name;
  std::vector<Param> params;
  std::string        body;  // source text of the aliased type
  bool               is_pub = false;
};

// What a unit's source declares: its templates plus the names it declares at
// file scope (types, enums, constants, lambdas), which a template body may
// mention and an importer must therefore qualify (`unit.Name`).
struct Unit_templates {
  std::map<std::string, Template> by_name;
  std::set<std::string>           top_names;
};

// The template a `type` statement declares: it must have generics and an aliased
// type (`type T<W> = …`; a function-type alias is not a template). `src` is the
// text its byte offsets index.
[[nodiscard]] std::optional<Template> template_from_statement(std::string_view src, const prpparse::Ast* type_statement);

// A cheap text screen (no parse): can `source` declare a parametric alias at all?
[[nodiscard]] bool might_have_templates(std::string_view source);

// Parse `source` and collect its templates. Returns an empty set when the file
// does not parse (the full parse owns the syntax diagnostics).
[[nodiscard]] Unit_templates scan_unit(std::string_view path, std::string_view source);

struct Arg {
  std::string name;  // empty: positional
  std::string text;  // the argument's source text
};

struct Expansion {
  std::string text;  // the expanded type; empty on error
  std::string error_code;
  std::string error_message;
  std::string error_hint;
};

// Bind `args` to the template's generics (positional first, then named; a
// missing one takes its declared default, which may use the generics before it)
// and rewrite the body. `qualifier` is the importer's name for the declaring
// unit (empty for a template of the same file): a file-scope name of the unit
// that the body mentions is written `qualifier.name`.
[[nodiscard]] Expansion expand(const Template& t, const std::vector<Arg>& args, std::string_view qualifier,
                               const std::set<std::string>& unit_names);

}  // namespace prp_type_alias
