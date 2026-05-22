//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_ssa.hpp"

#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "lnast_ntype.hpp"

namespace {
// Returns true for LNAST statement types whose first child is the write
// destination (LHS) and whose remaining children are read operands (RHS).
// This range covers: assign, dp_assign, delay_assign, all arithmetic/logic/
// comparison/shift/bit-manipulation ops up to `ge` inclusive.
// Excluded: if, for, while, func_*, stmts, io, top, ref, const, tuple_*,
// attr_*, cassert, type_*, and all type-leaf nodes.
constexpr bool stmt_has_dest(Lnast_ntype::Lnast_ntype_int t) {
  return t >= Lnast_ntype::Lnast_ntype_assign && t <= Lnast_ntype::Lnast_ntype_ge;
}

// Parse the bits/is_signed from a prim_type_uint/prim_type_sint subtree
// (or any other type ntype). Returns {bits=0, is_signed=true} on miss.
struct Type_info {
  int32_t bits      = 0;
  bool    is_signed = true;
};
Type_info type_info_from(const std::shared_ptr<Lnast>& lnast, Lnast_nid type_nid) {
  Type_info ti;
  if (type_nid.is_invalid()) {
    return ti;
  }
  const auto tty = lnast->get_type(type_nid);
  if (Lnast_ntype::is_prim_type_uint(tty)) {
    ti.is_signed = false;
  }
  if (Lnast_ntype::is_prim_type_uint(tty) || Lnast_ntype::is_prim_type_sint(tty)) {
    auto wnid = lnast->get_first_child(type_nid);
    if (!wnid.is_invalid() && Lnast_ntype::is_const(lnast->get_type(wnid))) {
      auto    txt    = lnast->get_name(wnid);
      int32_t parsed = 0;
      for (char c : txt) {
        if (c < '0' || c > '9') {
          parsed = 0;
          break;
        }
        parsed = parsed * 10 + (c - '0');
      }
      ti.bits = parsed;
    }
  }
  return ti;
}
}  // namespace

// ── is_user_var ──────────────────────────────────────────────────────────────
bool uPass_ssa::is_user_var(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  // Port sigils — pinned to graph I/O, never rename.
  if (name[0] == '$' || name[0] == '%') {
    return false;
  }
  // Compiler-generated temporaries — already unique per func_extract.
  if (name.size() >= 3 && name[0] == '_' && name[1] == '_' && name[2] == '_') {
    return false;
  }
  return true;
}

// ── copy_subtree ─────────────────────────────────────────────────────────────
void uPass_ssa::copy_subtree(const std::shared_ptr<Lnast>& src, const Lnast_nid& src_nid, const std::shared_ptr<Lnast>& dst,
                             const Lnast_nid& dst_parent) {
  auto      type = src->get_type(src_nid);
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
void uPass_ssa::copy_with_rename(const std::shared_ptr<Lnast>& src, const Lnast_nid& src_nid, const std::shared_ptr<Lnast>& dst,
                                 const Lnast_nid& dst_parent, const std::unordered_map<std::string, std::string>& rename_map) {
  auto      type = src->get_type(src_nid);
  Lnast_nid new_nid;
  if (Lnast_ntype::is_ref(type)) {
    auto        name = std::string(src->get_name(src_nid));
    auto        it   = rename_map.find(name);
    const auto& out  = (it != rename_map.end()) ? it->second : name;
    new_nid          = dst->add_child(dst_parent, Lnast_node::create_ref(out));
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
    if (Lnast_ntype::is_io(t)) {
      io_nid   = child;
      found_io = true;
    }
    if (Lnast_ntype::is_stmts(t)) {
      stmts_nid   = child;
      found_stmts = true;
    }
  }
  if (!found_io || !found_stmts) {
    return;  // Not a post-func_extract function LNAST — nothing to do.
  }

  // ── Walk io's two tuple_add children (inputs, outputs) ──────────────────
  //
  // Each child is a `tuple_add` whose children are arg-shape assigns:
  //   assign(ref name, const default-or-ref-marker, type-subtree?)
  // A composite tuple type has the 3rd child as a `tuple_add` of recursive
  // assigns. We flatten those into dotted leaf names (`ar.x`, `ar.y`) and
  // populate io_meta in the same order. The flat form is also re-emitted in
  // the staging tree's io node so the post-SSA dump shows the expanded
  // signature.
  Lnast_nid in_tup_nid;
  Lnast_nid out_tup_nid;
  {
    auto it = lnast->children(io_nid).begin();
    auto end = lnast->children(io_nid).end();
    if (it != end) {
      in_tup_nid = *it;
      ++it;
    }
    if (it != end) {
      out_tup_nid = *it;
    }
  }

  Lnast_tree_io& meta = lnast->io_meta();

  // Build flat (name, bits, is_signed, is_ref) tuples. Recursive helper:
  // when an assign's type slot is a `tuple_add`, expand each field with a
  // dotted-name prefix; otherwise emit one leaf entry.
  using Flat_field = Lnast_io_entry;
  std::vector<Flat_field>      flat_inputs;
  std::vector<Flat_field>      flat_outputs;
  std::function<void(Lnast_nid, const std::string&, bool, std::vector<Flat_field>&)> flatten_assign;
  flatten_assign = [&](Lnast_nid assign_nid, const std::string& prefix, bool collect_is_ref,
                       std::vector<Flat_field>& out) {
    if (!Lnast_ntype::is_assign(lnast->get_type(assign_nid))) {
      return;
    }
    auto name_nid = lnast->get_first_child(assign_nid);
    if (name_nid.is_invalid() || !Lnast_ntype::is_ref(lnast->get_type(name_nid))) {
      return;
    }
    auto leaf_name = std::string(lnast->get_name(name_nid));
    auto full      = prefix.empty() ? leaf_name : prefix + "." + leaf_name;

    auto rhs_nid  = lnast->get_sibling_next(name_nid);
    auto type_nid = rhs_nid.is_invalid() ? rhs_nid : lnast->get_sibling_next(rhs_nid);

    // Composite tuple type → recurse on each inner assign with a dotted prefix.
    if (!type_nid.is_invalid() && Lnast_ntype::is_tuple_add(lnast->get_type(type_nid))) {
      for (auto inner : lnast->children(type_nid)) {
        flatten_assign(inner, full, collect_is_ref, out);
      }
      return;
    }

    bool is_ref = false;
    if (collect_is_ref && !rhs_nid.is_invalid() && Lnast_ntype::is_const(lnast->get_type(rhs_nid))
        && lnast->get_name(rhs_nid) == "ref") {
      is_ref = true;
    }
    auto ti = type_info_from(lnast, type_nid);
    out.push_back({full, ti.bits, ti.is_signed, is_ref});
  };

  if (!in_tup_nid.is_invalid()) {
    for (auto entry : lnast->children(in_tup_nid)) {
      flatten_assign(entry, std::string{}, /*collect_is_ref=*/true, flat_inputs);
    }
  }
  if (!out_tup_nid.is_invalid()) {
    for (auto entry : lnast->children(out_tup_nid)) {
      flatten_assign(entry, std::string{}, /*collect_is_ref=*/false, flat_outputs);
    }
  }
  meta.inputs  = flat_inputs;
  meta.outputs = flat_outputs;

  // ── Build staging tree with SSA renaming ─────────────────────────────────
  auto staging_body = lnast->forest()->create_tree_temp(std::format("ssa-{}", lnast->get_top_module_name()));
  auto staging      = std::make_shared<Lnast>(staging_body, lnast->get_top_module_name());
  auto new_root     = staging->set_root(Lnast_ntype::create_top());

  // Re-emit the io node with flattened leaf entries (no nested tuple_add
  // type subtree). Width/signedness for each entry come from io_meta.
  auto new_io       = staging->add_child(new_root, Lnast_ntype::create_io());
  auto emit_section = [&](const std::vector<Flat_field>& fields, bool is_input) {
    auto tup = staging->add_child(new_io, Lnast_ntype::create_tuple_add());
    for (const auto& f : fields) {
      auto a = staging->add_child(tup, Lnast_ntype::create_assign());
      staging->add_child(a, Lnast_node::create_ref(f.name));
      staging->add_child(a, Lnast_node::create_const(is_input && f.is_ref ? "ref" : "nil"));
      if (f.bits > 0) {
        auto ty = staging->add_child(a, f.is_signed ? Lnast_ntype::create_prim_type_sint()
                                                    : Lnast_ntype::create_prim_type_uint());
        staging->add_child(ty, Lnast_node::create_const(std::to_string(f.bits)));
      }
    }
  };
  emit_section(flat_inputs, /*is_input=*/true);
  emit_section(flat_outputs, /*is_input=*/false);

  auto new_stmts = staging->add_child(new_root, Lnast_ntype::create_stmts());

  // SSA rename state (straight-line scope only; branches copied verbatim).
  std::unordered_map<std::string, std::string> rename_map;  // base → current SSA name
  std::unordered_map<std::string, int>         ssa_count;   // version counter
  std::unordered_set<std::string>              seen_lhs;    // first-seen tracker

  for (auto child : lnast->children(stmts_nid)) {
    auto type = lnast->get_type(child);

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
          auto        lhs_name = std::string(lnast->get_name(sub));
          std::string out_name = lhs_name;

          if (is_user_var(lhs_name)) {
            if (seen_lhs.count(lhs_name)) {
              // Preview next SSA version — do NOT touch rename_map yet.
              int n       = ssa_count[lhs_name] + 1;
              out_name    = lhs_name + "___ssa_" + std::to_string(n);
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
