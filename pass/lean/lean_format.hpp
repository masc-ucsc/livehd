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
// WHAT THIS DOES AND DOES NOT GUARANTEE.
//
// On a throw from either `emit`, or a failed open, write, close or rename, it
// unwinds to the previous state -- SO LONG AS THE UNWIND ITSELF SUCCEEDS. That
// qualifier is the honest version: a persistent filesystem error (a full disk, a
// revoked permission, something occupying a target path) can prevent the restore
// from completing. When that happens the previous artifact is NOT lost -- it is
// still in its `.bak` -- and the exception NAMES that path so it can be moved
// back by hand. What is not promised is that the targets are back where they
// were.
//
// It is NOT crash atomic: two renames cannot be, and a crash between them leaves
// the new first file beside the old second. It is NOT safe against a CONCURRENT
// writer to the same targets, since the backups are keyed on the target names.
//
// A pre-existing `.bak` is REFUSED before anything is mutated, rather than
// overwritten: it may be an interrupted publish's only copy of a previous
// artifact.
void publish_pair(const std::string& path_a, const std::function<void(std::ostream&)>& emit_a,
                  const std::string& path_b, const std::function<void(std::ostream&)>& emit_b);

namespace detail {
// The rollback half of `publish_pair`, exposed so it can be tested directly.
//
// The interesting case -- a restore that FAILS -- cannot be provoked from
// outside `publish_pair`: the window between moving an original aside and
// putting it back is entirely inside one call, with no hook in it. Rather than
// add an injection seam to production code for one test, the decision logic is a
// function, and the test drives it with a blocked target.
//
// `moved_*` / `put_*` say what actually happened, because the loss this guards
// against came from ASSUMING both originals had been backed up. Returns the
// paths of any backups that could not be restored -- empty when the unwind was
// clean.
std::string rollback_pair(const std::string& path_a, const std::string& path_b, bool moved_a, bool moved_b,
                          bool put_a, bool put_b);
}  // namespace detail
}  // namespace lean_export
