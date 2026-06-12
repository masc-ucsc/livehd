//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Regression test for stable / deterministic SSA & tmp ids (TODO 2p).
//
// create_lnast_tmp() must mint ids that are (a) deterministic across rebuilds
// and (b) stable under small source edits: a temp's id depends only on its
// scope (the statement's destination) and a per-scope counter, never on a
// single global counter that renumbers the whole function when a line is
// inserted ahead. The `___` prefix is always preserved so downstream `is_tmp`
// checks keep working.

#include <algorithm>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "lnast_builder.hpp"

namespace {

int failures = 0;

void check(bool ok, std::string_view what) {
  if (!ok) {
    std::print(stderr, "FAIL: {}\n", what);
    ++failures;
  }
}

void check_eq(std::string_view got, std::string_view want, std::string_view what) {
  if (got != want) {
    std::print(stderr, "FAIL: {} — got '{}', want '{}'\n", what, got, want);
    ++failures;
  }
}

// Mint the temps a small function would produce, in source order.
// `lead` optionally prepends an extra scoped statement ("insert a line ahead").
std::vector<std::string> mint_function(bool lead) {
  Lnast_builder b;
  b.new_lnast("t");

  std::vector<std::string> ids;
  if (lead) {
    Lnast_builder::Tmp_scope_guard g(b, "w");
    ids.push_back(b.create_lnast_tmp());  // ___w_0
    ids.push_back(b.create_lnast_tmp());  // ___w_1
  }
  {
    Lnast_builder::Tmp_scope_guard g(b, "x:u8");  // type annotation must not leak into the label
    ids.push_back(b.create_lnast_tmp());          // ___x_0
    ids.push_back(b.create_lnast_tmp());          // ___x_1
  }
  {
    Lnast_builder::Tmp_scope_guard g(b, "y");
    ids.push_back(b.create_lnast_tmp());  // ___y_0
  }
  return ids;
}

}  // namespace

int main() {
  // ── 1. Deterministic across rebuilds ──────────────────────────────────────
  check(mint_function(false) == mint_function(false), "ids are identical across rebuilds");

  // ── 2. Scope label + per-scope counter shape ──────────────────────────────
  {
    auto ids = mint_function(false);
    check_eq(ids[0], "___x_0", "first x temp");
    check_eq(ids[1], "___x_1", "second x temp");
    check_eq(ids[2], "___y_0", "first y temp (independent counter)");
  }

  // ── 3. Stable under a line inserted ahead ─────────────────────────────────
  // The `x` and `y` temps must keep the SAME ids when an unrelated `w`
  // statement is prepended — only `w`'s temps are new.
  {
    auto base = mint_function(false);
    auto lead = mint_function(true);
    for (const auto& id : base) {
      bool kept = std::find(lead.begin(), lead.end(), id) != lead.end();
      check(kept, std::string("inserting a statement ahead preserves id ") + id);
    }
    check_eq(lead[0], "___w_0", "inserted statement gets its own scope");
  }

  // ── 4. Re-opening a scope keeps minting unique (monotonic) ids ─────────────
  {
    Lnast_builder b;
    b.new_lnast("t");
    std::string a0, a1, b0, a2;
    {
      Lnast_builder::Tmp_scope_guard g(b, "a");
      a0 = b.create_lnast_tmp();
      a1 = b.create_lnast_tmp();
    }
    {
      Lnast_builder::Tmp_scope_guard g(b, "b");
      b0 = b.create_lnast_tmp();
    }
    {
      Lnast_builder::Tmp_scope_guard g(b, "a");  // same label again
      a2 = b.create_lnast_tmp();
    }
    check_eq(a0, "___a_0", "a counter start");
    check_eq(a1, "___a_1", "a counter 1");
    check_eq(b0, "___b_0", "b counter independent");
    check_eq(a2, "___a_2", "re-opened a scope stays unique (no reset)");
  }

  // ── 5. Global fallback when no scope is open ───────────────────────────────
  {
    Lnast_builder b;
    b.new_lnast("t");
    auto f0 = b.create_lnast_tmp();
    auto f1 = b.create_lnast_tmp();
    check_eq(f0, "___1", "fallback id 1");
    check_eq(f1, "___2", "fallback id 2");
    // A scoped id can never collide with a fallback id: scoped ids always have
    // a non-digit immediately after `___`.
    Lnast_builder::Tmp_scope_guard g(b, "1abc");  // leading digit is dropped -> no scope
    check_eq(b.create_lnast_tmp(), "___3", "non-identifier dest falls back to global counter");
  }

  // ── 6. new_lnast() resets all id state ─────────────────────────────────────
  {
    Lnast_builder b;
    b.new_lnast("t");
    { Lnast_builder::Tmp_scope_guard g(b, "z"); b.create_lnast_tmp(); }
    b.create_lnast_tmp();  // fallback bumps global counter
    b.new_lnast("t2");
    check_eq(b.create_lnast_tmp(), "___1", "new_lnast resets the global counter");
    Lnast_builder::Tmp_scope_guard g(b, "z");
    check_eq(b.create_lnast_tmp(), "___z_0", "new_lnast resets per-label counters");
  }

  // ── 7. stabilize_fallback_tmps(): hash-named fallbacks ────────────────────
  // Build `plus(___1, p, q); minus(___2, ___1, r)` twice, with an unrelated
  // statement prepended the second time. After stabilization the fallback ids
  // must (a) derive from each def site (so they match across both builds even
  // though the raw counters differ), (b) keep the `___` prefix with a digit
  // right after it (no scoped-name collision), and (c) stay consistent
  // between def and use.
  {
    auto build = [](bool lead) {
      Lnast_builder b;
      b.new_lnast("t");
      if (lead) {
        // An unrelated fallback temp ahead: shifts the raw global counter.
        auto idx = b.add_child(Lnast_ntype::create_eq());
        b.add_child(idx, Lnast_node::create_ref(b.create_lnast_tmp()));  // ___1
        b.add_child(idx, Lnast_node::create_ref("s"));
        b.add_child(idx, Lnast_node::create_const("0"));
      }
      auto t0  = b.create_lnast_tmp();  // ___1 or ___2
      auto idx = b.add_child(Lnast_ntype::create_plus());
      b.add_child(idx, Lnast_node::create_ref(t0));
      b.add_child(idx, Lnast_node::create_ref("p"));
      b.add_child(idx, Lnast_node::create_ref("q"));
      auto t1   = b.create_lnast_tmp();
      auto idx2 = b.add_child(Lnast_ntype::create_minus());
      b.add_child(idx2, Lnast_node::create_ref(t1));
      b.add_child(idx2, Lnast_node::create_ref(t0));  // chained: hash must use t0's NEW name
      b.add_child(idx2, Lnast_node::create_ref("r"));
      b.stabilize_fallback_tmps();

      std::vector<std::string> names;  // [plus dst, minus dst, minus operand]
      names.emplace_back(b.lnast->get_name(b.lnast->get_first_child(idx)));
      names.emplace_back(b.lnast->get_name(b.lnast->get_first_child(idx2)));
      names.emplace_back(b.lnast->get_name(b.lnast->get_sibling_next(b.lnast->get_first_child(idx2))));
      return names;
    };

    auto base = build(false);
    auto lead = build(true);
    check_eq(lead[0], base[0], "fallback id survives a statement inserted ahead (plus dst)");
    check_eq(lead[1], base[1], "fallback id survives a statement inserted ahead (chained minus dst)");
    check_eq(base[2], base[0], "def and use of a fallback tmp get the same stabilized name");
    check(base[0] != base[1], "different def sites get different ids");
    for (const auto& n : base) {
      check(n.size() > 4 && n.substr(0, 3) == "___" && n[3] >= '0' && n[3] <= '9' && n.find('_', 3) != std::string::npos,
            std::string("stabilized id '") + n + "' has the ___<digits>_<n> shape");
    }
  }

  // ── 8. Same-hash repeats stay unique via the occurrence counter ───────────
  {
    Lnast_builder b;
    b.new_lnast("t");
    std::vector<Lnast_nid> dsts;
    for (int i = 0; i < 2; ++i) {  // two identical statements
      auto idx = b.add_child(Lnast_ntype::create_plus());
      dsts.push_back(b.add_child(idx, Lnast_node::create_ref(b.create_lnast_tmp())));
      b.add_child(idx, Lnast_node::create_ref("p"));
      b.add_child(idx, Lnast_node::create_ref("q"));
    }
    b.stabilize_fallback_tmps();
    auto n0 = std::string(b.lnast->get_name(dsts[0]));
    auto n1 = std::string(b.lnast->get_name(dsts[1]));
    check(n0 != n1, "identical statements still mint unique ids");
    check_eq(n1, n0.substr(0, n0.rfind('_')) + "_1", "second repeat is hash_1");
  }

  if (failures) {
    std::print(stderr, "{} check(s) failed\n", failures);
    return 1;
  }
  std::print("all stable-tmp checks passed\n");
  return 0;
}
