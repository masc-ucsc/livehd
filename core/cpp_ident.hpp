//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <array>
#include <type_traits>
#include <cctype>
#include <string>
#include <string_view>

// THE one place that turns an RTL/Pyrope name into a legal C++ identifier.
//
// Two binaries mint C++ names for the generated simulator and they MUST agree:
// inou/cgen (Cgen_sim::cpp_id -- the struct members, flops, memories, sub
// instances and the `<stem>.iface.json` port manifest) and inou/prp
// (prp_sim's field_storage, which resolves a testbench's `dut.<field>` against
// that manifest). If they drift, a name is declared one way and referenced the
// other, so this lives here rather than in either one.
//
// NOT in core/str_tools.hpp on purpose: that file is part of //core:formal_srcs,
// which is hashed into //lhd:formal_salt, so editing it invalidates every cached
// formal/LEC verdict in the repo.

namespace livehd {

// C++23 keywords ([lex.key]) plus the 11 ALTERNATIVE TOKENS ([lex.digraph]:
// and and_eq bitand bitor compl not not_eq or or_eq xor xor_eq).
//
// The alternative tokens are the subtle half: they are TOKENS the lexer
// produces, never names, so a member declared `Slop_u<1> xor{};` is a hard
// parse error ("expected member name or ';' after declaration specifiers"),
// not a shadowing warning. `xor` is a perfectly legal Pyrope field name, so an
// ordinary design reaches it.
//
// Identifiers-with-special-meaning (`final`, `override`, `import`, `module`)
// are legal as members and are deliberately NOT listed.
[[nodiscard]] inline bool is_cpp_reserved(std::string_view s) {
  // Size is DEDUCED (std::to_array): an explicit count is one more thing to get
  // wrong, and getting it wrong is a confusing constant-initialization error
  // rather than an obvious one. A static_assert below pins the sort order,
  // which is what binary_search actually depends on.
  static constexpr auto kSorted = std::to_array<std::string_view>({
      "alignas", "alignof", "and", "and_eq", "asm", "auto",
      "bitand", "bitor", "bool", "break", "case", "catch",
      "char", "char16_t", "char32_t", "char8_t", "class", "co_await",
      "co_return", "co_yield", "compl", "concept", "const", "const_cast",
      "consteval", "constexpr", "constinit", "continue", "decltype", "default",
      "delete", "do", "double", "dynamic_cast", "else", "enum",
      "explicit", "export", "extern", "false", "float", "for",
      "friend", "goto", "if", "inline", "int", "long",
      "mutable", "namespace", "new", "noexcept", "not", "not_eq",
      "nullptr", "operator", "or", "or_eq", "private", "protected",
      "public", "register", "reinterpret_cast", "requires", "return", "short",
      "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
      "switch", "template", "this", "thread_local", "throw", "true",
      "try", "typedef", "typeid", "typename", "union", "unsigned",
      "using", "virtual", "void", "volatile", "wchar_t", "while",
      "xor", "xor_eq",
  });
  static_assert(std::is_sorted(kSorted.begin(), kSorted.end()), "kSorted must stay sorted for binary_search");
  return std::binary_search(kSorted.begin(), kSorted.end(), s);
}

// Sanitize ONE name segment to a legal C++ identifier: strip LNAST backtick
// quotes, map every character C++ cannot use to '_', keep a leading digit from
// starting the name, and finally escape a reserved word.
//
// The reserved check runs LAST on purpose: the character sanitize can CREATE a
// reserved word (`xor-eq` -> `xor_eq`). The escape is a TRAILING '_' -- never
// leading and never doubled -- which keeps clear of the names reserved to the
// implementation (a leading underscore at class scope, or any `__`).
[[nodiscard]] inline std::string cpp_ident(std::string_view name) {
  std::string r;
  r.reserve(name.size() + 1);
  if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {  // `a[0]`
    name.remove_prefix(1);
    name.remove_suffix(1);
  }
  for (char c : name) {
    r.push_back((std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') ? c : '_');
  }
  if (r.empty() || std::isdigit(static_cast<unsigned char>(r.front())) != 0) {
    r.insert(r.begin(), '_');
  }
  if (is_cpp_reserved(r)) {
    r.push_back('_');
  }
  return r;
}

}  // namespace livehd
