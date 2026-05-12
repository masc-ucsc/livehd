//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_ssa.hpp"

#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "lnast_ntype.hpp"

namespace {
constexpr std::string_view k_empty_tuple = "__empty_tuple";

// Returns true for LNAST statement types whose first child is the write
// destination (LHS) and whose remaining children are read operands (RHS).
// This range covers: assign, dp_assign, delay_assign, all arithmetic/logic/
// comparison/shift/bit-manipulation ops up to `ge` inclusive.
// Excluded: if, for, while, func_*, stmts, io, top, ref, const, tuple_*,
// attr_*, cassert, type_*, and all type-leaf nodes.
constexpr bool stmt_has_dest(Lnast_ntype::Lnast_ntype_int t) {
  return t >= Lnast_ntype::Lnast_ntype_assign && t <= Lnast_ntype::Lnast_ntype_ge;
}
}  // namespace

// ── is_user_var ──────────────────────────────────────────────────────────────
bool uPass_ssa::is_user_var(std::string_view name) {
  if (name.empty()) return false;
  // Port sigils — pinned to graph I/O, never rename.
  if (name[0] == '$' || name[0] == '%') return false;
  // Compiler-generated temporaries — already unique per func_extract.
  if (name.size() >= 3 && name[0] == '_' && name[1] == '_' && name[2] == '_') return false;
  return true;
}

// ── copy_subtree ─────────────────────────────────────────────────────────────
void uPass_ssa::copy_subtree(const std::shared_ptr<Lnast>& src,
                              const Lnast_nid&               src_nid,
                              const std::shared_ptr<Lnast>& dst,
                              const Lnast_nid&               dst_parent) {
  auto      type    = src->get_type(src_nid);
  Lnast_nid new_nid;
  if (Lnast_ntype::is_ref(type)) {
    new_nid = dst->add_child(dst_parent, Lnast_node::create_ref(src->get_name(src_nid)));
  } else if (Lnast_ntype::is_const(type)) {
    new_nid = dst->add_child(dst_parent, Lnast_node::create_const(src->get_name(src_nid)));
  } else if (Lnast_ntype::is_invalid(type)) {
    new_nid = dst->add_child(dst_parent, Lnast_node::create_invalid());
  } else {
    new_nid = dst->add_child(dst_parent, type);
  }
  for (auto child : src->children(src_nid)) {
    copy_subtree(src, child, dst, new_nid);
  }
}

// ── copy_with_rename ─────────────────────────────────────────────────────────
void uPass_ssa::copy_with_rename(
    const std::shared_ptr<Lnast>&                       src,
    const Lnast_nid&                                    src_nid,
    const std::shared_ptr<Lnast>&                       dst,
    const Lnast_nid&                                    dst_parent,
    const std::unordered_map<std::string, std::string>& rename_map) {
  auto      type    = src->get_type(src_nid);
  Lnast_nid new_nid;
  if (Lnast_ntype::is_ref(type)) {
    auto        name = std::string(src->get_name(src_nid));
    auto        it   = rename_map.find(name);
    const auto& out  = (it != rename_map.end()) ? it->second : name;
    new_nid = dst->add_child(dst_parent, Lnast_node::create_ref(out));
  } else if (Lnast_ntype::is_const(type)) {
    new_nid = dst->add_child(dst_parent, Lnast_node::create_const(src->get_name(src_nid)));
  } else if (Lnast_ntype::is_invalid(type)) {
    new_nid = dst->add_child(dst_parent, Lnast_node::create_invalid());
  } else {
    new_nid = dst->add_child(dst_parent, type);
  }
  for (auto child : src->children(src_nid)) {
    copy_with_rename(src, child, dst, new_nid, rename_map);
  }
}

// ── run ──────────────────────────────────────────────────────────────────────
void uPass_ssa::run(const std::shared_ptr<Lnast>& lnast) {
  auto root = lnast->get_root();

  // ── Detect post-func_extract shape: top must have an 'io' child ──────────
  bool      found_io    = false;
  bool      found_stmts = false;
  Lnast_nid io_nid;
  Lnast_nid stmts_nid;

  for (auto child : lnast->children(root)) {
    const auto t = lnast->get_type(child);
    if (Lnast_ntype::is_io(t))    { io_nid    = child; found_io    = true; }
    if (Lnast_ntype::is_stmts(t)) { stmts_nid = child; found_stmts = true; }
  }
  if (!found_io || !found_stmts) {
    return;  // Not a post-func_extract function LNAST — nothing to do.
  }

  // ── Read input/output tuple ref names from the io node ───────────────────
  std::string input_ref_name;
  std::string output_ref_name;
  {
    auto io_children = lnast->children(io_nid);
    auto it          = io_children.begin();
    if (it != io_children.end()) {
      input_ref_name = std::string(lnast->get_name(*it));
      ++it;
    }
    if (it != io_children.end()) {
      output_ref_name = std::string(lnast->get_name(*it));
    }
  }

  // ── Harvest field names from the matching tuple_add nodes in stmts ───────
  Lnast_tree_io& meta = lnast->io_meta();
  for (auto child : lnast->children(stmts_nid)) {
    if (!Lnast_ntype::is_tuple_add(lnast->get_type(child))) continue;

    Lnast_nid first_ch = lnast->get_first_child(child);
    if (first_ch.is_invalid()) continue;
    auto tuple_ref = std::string(lnast->get_name(first_ch));

    const bool is_input  = !input_ref_name.empty() && tuple_ref == input_ref_name
                           && tuple_ref != k_empty_tuple;
    const bool is_output = !output_ref_name.empty() && tuple_ref == output_ref_name
                           && tuple_ref != k_empty_tuple;
    if (!is_input && !is_output) continue;

    for (auto field_node : lnast->children(child)) {
      if (!Lnast_ntype::is_assign(lnast->get_type(field_node))) continue;
      Lnast_nid lhs = lnast->get_first_child(field_node);
      if (lhs.is_invalid()) continue;
      auto fname = std::string(lnast->get_name(lhs));
      if (is_input)  meta.inputs.push_back({fname, 0, true});
      if (is_output) meta.outputs.push_back({fname, 0, true});
    }
  }

  // ── Build staging tree with SSA renaming ─────────────────────────────────
  auto staging_body = lnast->forest()->create_tree_temp(
      std::format("ssa-{}", lnast->get_top_module_name()));
  auto staging   = std::make_shared<Lnast>(staging_body, lnast->get_top_module_name());
  auto new_root  = staging->set_root(Lnast_ntype::create_top());
  auto new_stmts = staging->add_child(new_root, Lnast_ntype::create_stmts());

  // SSA rename state (straight-line scope only; branches copied verbatim).
  std::unordered_map<std::string, std::string> rename_map;   // base → current SSA name
  std::unordered_map<std::string, int>         ssa_count;    // version counter
  std::unordered_set<std::string>              seen_lhs;     // first-seen tracker

  for (auto child : lnast->children(stmts_nid)) {
    auto type = lnast->get_type(child);

    // ── Skip I/O tuple_adds (harvested above) ──────────────────────────────
    if (Lnast_ntype::is_tuple_add(type)) {
      Lnast_nid first_ch = lnast->get_first_child(child);
      if (!first_ch.is_invalid()) {
        auto       tuple_ref  = std::string(lnast->get_name(first_ch));
        const bool is_io_inp  = !input_ref_name.empty()  && tuple_ref == input_ref_name
                                && tuple_ref != k_empty_tuple;
        const bool is_io_out  = !output_ref_name.empty() && tuple_ref == output_ref_name
                                && tuple_ref != k_empty_tuple;
        if (is_io_inp || is_io_out) continue;
      }
    }

    // ── Statements with a write dest as first child: apply SSA renaming ────
    if (stmt_has_dest(type)) {
      auto stmt_node = staging->add_child(new_stmts, type);

      // Defer rename_map update until after RHS is fully copied so that a
      // self-referencing assignment like `t = t + 1` (second assignment)
      // reads the *previous* SSA version of `t` on the RHS, not the new one.
      std::string pending_lhs;  // empty = no deferred update
      std::string pending_ssa;

      bool first = true;
      for (auto sub : lnast->children(child)) {
        if (first) {
          // ── LHS ────────────────────────────────────────────────────────
          auto lhs_name = std::string(lnast->get_name(sub));
          std::string out_name = lhs_name;

          if (is_user_var(lhs_name)) {
            if (seen_lhs.count(lhs_name)) {
              // Preview next SSA version — do NOT touch rename_map yet.
              int n    = ssa_count[lhs_name] + 1;
              out_name = lhs_name + "___ssa_" + std::to_string(n);
              pending_lhs = lhs_name;
              pending_ssa = out_name;
            } else {
              seen_lhs.insert(lhs_name);
              // Initialise identity mapping so RHS reads work correctly
              // even before a rename is needed.
              if (rename_map.find(lhs_name) == rename_map.end()) {
                rename_map[lhs_name] = lhs_name;
              }
            }
          }
          staging->add_child(stmt_node, Lnast_node::create_ref(out_name));
          first = false;
        } else {
          // ── RHS: rename_map still holds OLD mapping — correct. ──────────
          copy_with_rename(lnast, sub, staging, stmt_node, rename_map);
        }
      }

      // Apply deferred update: subsequent statements now see the new SSA name.
      if (!pending_lhs.empty()) {
        ++ssa_count[pending_lhs];
        rename_map[pending_lhs] = pending_ssa;
      }
    } else {
      // Control-flow / structural nodes (if, func_def, …): copy verbatim.
      // The lowerer's lower_branch() + Mux logic handles if/else correctly
      // without LNAST-level join nodes.
      copy_subtree(lnast, child, staging, new_stmts);
    }
  }

  // Commit: replace the original LNAST body with the staging tree.
  lnast->replace_body(staging->tree_ptr());
}
