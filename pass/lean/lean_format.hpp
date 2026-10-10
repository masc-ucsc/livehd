// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "certificate_ir.hpp"
namespace lean_export {
std::string format_op(const Op& op);
std::string sanitize_lean(std::string_view name);
std::string lean_integer(std::string_view decimal);
// A Lean STRING LITERAL, quoted and escaped. Distinct from `sanitize_lean`,
// which makes an IDENTIFIER: a clock's name is data inside the certificate, not
// a declaration name, so mangling it would lose the net it came from while
// escaping it keeps the name readable and the literal well-formed.
std::string lean_string(std::string_view text);
std::string nat_array(const std::vector<uint32_t>& values);
std::string lit_bv(uint32_t width, std::string_view integer_expression);
std::string lit_zero(uint32_t width);
std::string lit_one(uint32_t width);
std::string nat_list(const std::vector<uint32_t>& values);
std::string bst_literal(std::vector<std::pair<uint32_t, std::string>> pairs);
void        write_atomic(const std::string& path, const std::function<void(std::ostream&)>& emit);

// Publish TWO files as a pair, keeping the previous coherent pair on failure.
//
// `write_atomic` is atomic for ONE file. A certificate and its sidecar are only
// useful together, and publishing them is TWO renames -- so a failure between
// them leaves a new certificate beside a stale sidecar unless something undoes
// it. D2's helper removed the newly published certificate and did NOT restore
// the preexisting one, which loses BOTH: the caller ends with no certificate at
// all where it previously had a working pair.
//
// Here, a failure at any point leaves whatever was there before. If both targets
// existed they are both still the old ones; if neither did, neither is created.
//
// WHAT THIS DOES AND DOES NOT GUARANTEE. It is exception- and
// command-failure-safe: a throw from either `emit`, a failed open, write, close
// or rename unwinds to the previous state. It is NOT crash atomic -- two renames
// cannot be, and a crash between them leaves the new first file with the old
// second. Nor is it safe against a CONCURRENT writer to the same targets; the
// backups are keyed on the target names and a second publisher would race them.
void publish_pair(const std::string& path_a, const std::function<void(std::ostream&)>& emit_a,
                  const std::string& path_b, const std::function<void(std::ostream&)>& emit_b);
}  // namespace lean_export
