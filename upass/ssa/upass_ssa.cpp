//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_ssa.hpp"

#include <format>
#include <string>

#include "lnast_ntype.hpp"

namespace {
constexpr std::string_view k_empty_tuple = "__empty_tuple";
}

// ── copy_subtree ─────────────────────────────────────────────────────────────
void uPass_ssa::copy_subtree(const std::shared_ptr<Lnast>& src,
                              const Lnast_nid&               src_nid,
                              const std::shared_ptr<Lnast>& dst,
                              const Lnast_nid&               dst_parent) {
  auto     type    = src->get_type(src_nid);
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

    // First child of tuple_add is the ref to the tuple name.
    Lnast_nid first_ch = lnast->get_first_child(child);
    if (first_ch.is_invalid()) continue;
    auto tuple_ref = std::string(lnast->get_name(first_ch));

    const bool is_input  = !input_ref_name.empty()
                           && tuple_ref == input_ref_name
                           && tuple_ref != k_empty_tuple;
    const bool is_output = !output_ref_name.empty()
                           && tuple_ref == output_ref_name
                           && tuple_ref != k_empty_tuple;
    if (!is_input && !is_output) continue;

    // Each remaining assign child carries one field name.
    for (auto field_node : lnast->children(child)) {
      if (!Lnast_ntype::is_assign(lnast->get_type(field_node))) continue;
      Lnast_nid lhs = lnast->get_first_child(field_node);
      if (lhs.is_invalid()) continue;
      auto fname = std::string(lnast->get_name(lhs));
      if (is_input)  meta.inputs.push_back({fname, 0, true});
      if (is_output) meta.outputs.push_back({fname, 0, true});
    }
  }

  // ── Build staging tree: top → stmts (body only, no io / tuple_add I/O) ───
  auto staging_body = lnast->forest()->create_tree_temp(
      std::format("ssa-{}", lnast->get_top_module_name()));
  auto staging   = std::make_shared<Lnast>(staging_body, lnast->get_top_module_name());
  auto new_root  = staging->set_root(Lnast_ntype::create_top());
  auto new_stmts = staging->add_child(new_root, Lnast_ntype::create_stmts());

  // Copy body statements, skipping tuple_add I/O nodes.
  for (auto child : lnast->children(stmts_nid)) {
    if (Lnast_ntype::is_tuple_add(lnast->get_type(child))) {
      // I/O tuple_adds have been harvested above — drop them.
      // Body-internal tuple_adds (local struct ops) are also skipped for
      // now; full Slice 6 tuple flattening is a future extension.
      continue;
    }
    copy_subtree(lnast, child, staging, new_stmts);
  }

  // Commit: replace the original LNAST body with the staging tree.
  lnast->replace_body(staging->tree_ptr());
}
