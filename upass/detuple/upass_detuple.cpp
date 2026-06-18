//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_detuple.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hhds/tree.hpp"
#include "lnast_ntype.hpp"

namespace {

// Leaf separator between a tuple var and its field. NOTE: '.' is what the SSA
// I/O flattening uses for ports, but constprop parses '.' as a bundle field
// path, so a body name like `mem.a` could be re-tupled downstream. Kept as a
// single knob so the choice can be revisited after empirical testing.
constexpr std::string_view kSep = ".";

struct Field {
  std::string name;     // e.g. "a"
  Lnast_nid   type_nid; // the prim_type_* subtree (in the SOURCE tree) to copy
};

class Detupler {
public:
  explicit Detupler(const std::shared_ptr<Lnast>& ln) : src_(ln) {}

  // Returns true if it rewrote the tree (and replaced the body).
  bool run() {
    analyze();
    if (split_mem_.empty()) {
      return false;  // nothing to lower — leave the tree untouched
    }

    auto forest   = hhds::Forest::create();
    auto name     = std::string(src_->get_top_module_name());
    auto body     = forest->create_tree_temp(name);
    dst_          = std::make_shared<Lnast>(body, name);
    auto src_root = src_->get_root();
    auto dst_root = dst_->set_root(src_->get_type(src_root));
    carry(src_root, dst_root);
    copy_transformed(src_root, dst_root);
    src_->replace_body(dst_->tree_ptr());
    return true;
  }

private:
  const std::shared_ptr<Lnast>& src_;
  std::shared_ptr<Lnast>        dst_;

  // ── Resolved named-type layouts: typename -> ordered fields (with type nid) ──
  absl::flat_hash_map<std::string, std::vector<Field>> type_fields_;
  // Split declarations: var -> fields (+ array dim const nid; invalid for scalar)
  struct Split {
    std::vector<Field> fields;
    Lnast_nid          dim_nid;  // the '[N]' const for a memory; invalid for a scalar var
    std::string        mode;     // declare mode ("reg"/"mut"/"const")
  };
  absl::flat_hash_map<std::string, Split> split_mem_;  // comp_type_array tuple memories
  // Named types consumed ENTIRELY by split memories: their `type T=(...)` region
  // is dead after the split and is dropped (else a re-emit — e.g. the Pyrope
  // writer — serializes its bare field refs as undefined-variable reads).
  absl::flat_hash_set<std::string> drop_types_;
  // Memory read fusion: index-step temp -> (mem var, index operand nid).
  struct Index_step {
    std::string var;
    Lnast_nid   idx_nid;
  };
  absl::flat_hash_map<std::string, Index_step> index_step_;
  absl::flat_hash_set<std::string>             drop_index_temp_;  // index steps safe to drop

  // ── helpers ────────────────────────────────────────────────────────────────
  std::string_view name_of(const Lnast_nid& n) const { return src_->get_name(n); }
  Lnast_ntype::Lnast_ntype_int type_of(const Lnast_nid& n) const { return src_->get_type(n); }

  void carry(const Lnast_nid& src_n, const Lnast_nid& dst_n) {
    if (const auto id = src_->get_srcid(src_n); id != hhds::SourceId_invalid) {
      dst_->set_srcid(dst_n, id);
    }
  }

  // Verbatim recursive copy of src_n's subtree under dst_parent.
  Lnast_nid copy_subtree(const Lnast_nid& src_n, const Lnast_nid& dst_parent) {
    const auto type = type_of(src_n);
    Lnast_nid  d;
    if (Lnast_ntype::is_ref(type)) {
      d = dst_->add_child(dst_parent, Lnast_node::create_ref(name_of(src_n)));
    } else if (Lnast_ntype::is_const(type)) {
      d = dst_->add_child(dst_parent, Lnast_node::create_const(name_of(src_n)));
    } else {
      d = dst_->add_child(dst_parent, type);
    }
    if (Lnast::srcid_carries(type)) {
      carry(src_n, d);
    }
    for (auto c = src_->get_first_child(src_n); !c.is_invalid(); c = src_->get_sibling_next(c)) {
      copy_subtree(c, d);
    }
    return d;
  }

  bool is_field_of(const Split& s, std::string_view txt) const {
    for (const auto& f : s.fields) {
      if (f.name == txt) {
        return true;
      }
    }
    return false;
  }

  // ── analysis ────────────────────────────────────────────────────────────────
  void analyze() {
    resolve_types();
    collect_splits();
    collect_read_chains();
  }

  // Build typename -> fields by a structural read of each `type X = (...)` def.
  // The post-parse shape (one tree, pre-SSA) is a contiguous region from the
  // type's `declare` to its `store(X, T)`. Two field-type shapes occur:
  //   * with field defaults `(a:u32=nil, …)`:
  //       tuple_add(T, store(a,_), store(b,_))           -> field order
  //       tuple_get(ta, T, 'a') ; type_spec(ta, prim_type_int)  -> a's type
  //   * no defaults `(a:u8, …)`:
  //       type_spec(a, prim_type_int)                    -> a's type (keyed by name)
  //       tuple_add(T, ref 'a', ref 'b')                 -> field order
  // The region scope keeps two types that share a field name from colliding.
  void resolve_types() {
    for (const auto n : src_->depth_preorder(src_->get_root())) {
      if (n.is_invalid() || !Lnast_ntype::is_declare(type_of(n))) {
        continue;
      }
      auto c0 = src_->get_first_child(n);
      auto c1 = c0.is_invalid() ? c0 : src_->get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : src_->get_sibling_next(c1);
      if (c0.is_invalid() || c2.is_invalid() || !Lnast_ntype::is_ref(type_of(c0))
          || !Lnast_ntype::is_const(type_of(c2)) || name_of(c2) != "type") {
        continue;
      }
      const std::string tname{name_of(c0)};
      resolve_one_type(n, tname);
    }
  }

  void resolve_one_type(const Lnast_nid& decl, const std::string& tname) {
    std::string                                               value_temp;  // T in store(tname, T)
    absl::flat_hash_map<std::string, std::vector<std::string>> tuple_fields;  // temp -> field order
    absl::flat_hash_map<std::string, std::string>             tget_rev;  // "base\0field" -> result temp
    absl::flat_hash_map<std::string, Lnast_nid>               typespec;  // ref name (temp OR field) -> type nid

    for (auto s = src_->get_sibling_next(decl); !s.is_invalid(); s = src_->get_sibling_next(s)) {
      const auto t = type_of(s);
      if (Lnast_ntype::is_store(t)) {
        auto a = src_->get_first_child(s);
        auto b = a.is_invalid() ? a : src_->get_sibling_next(a);
        if (!a.is_invalid() && Lnast_ntype::is_ref(type_of(a)) && name_of(a) == tname && !b.is_invalid()
            && src_->get_sibling_next(b).is_invalid() && Lnast_ntype::is_ref(type_of(b))) {
          value_temp = std::string(name_of(b));
          break;  // region ends at the type's value store
        }
      } else if (Lnast_ntype::is_tuple_add(t)) {
        auto r = src_->get_first_child(s);
        if (r.is_invalid() || !Lnast_ntype::is_ref(type_of(r))) {
          continue;
        }
        std::vector<std::string> fields;
        for (auto c = src_->get_sibling_next(r); !c.is_invalid(); c = src_->get_sibling_next(c)) {
          if (Lnast_ntype::is_ref(type_of(c))) {
            fields.emplace_back(name_of(c));  // no-default form: bare ref field
          } else if (Lnast_ntype::is_store(type_of(c))) {
            auto k = src_->get_first_child(c);  // with-default form: store(field, val)
            if (!k.is_invalid() && Lnast_ntype::is_ref(type_of(k))) {
              fields.emplace_back(name_of(k));
            }
          }
        }
        if (!fields.empty()) {
          tuple_fields[std::string(name_of(r))] = std::move(fields);
        }
      } else if (Lnast_ntype::is_tuple_get(t)) {
        auto a = src_->get_first_child(s);
        auto b = a.is_invalid() ? a : src_->get_sibling_next(a);
        auto c = b.is_invalid() ? b : src_->get_sibling_next(b);
        if (!c.is_invalid() && src_->get_sibling_next(c).is_invalid() && Lnast_ntype::is_ref(type_of(b))
            && Lnast_ntype::is_const(type_of(c))) {
          tget_rev[std::string(name_of(b)) + '\0' + std::string(name_of(c))] = std::string(name_of(a));
        }
      } else if (Lnast_ntype::is_type_spec(t)) {
        auto a = src_->get_first_child(s);
        auto b = a.is_invalid() ? a : src_->get_sibling_next(a);
        if (!b.is_invalid() && Lnast_ntype::is_ref(type_of(a))) {
          typespec[std::string(name_of(a))] = b;
        }
      }
    }

    if (value_temp.empty()) {
      return;
    }
    auto fit = tuple_fields.find(value_temp);
    if (fit == tuple_fields.end()) {
      return;
    }
    std::vector<Field> fields;
    for (const auto& f : fit->second) {
      Lnast_nid ty;
      if (auto d = typespec.find(f); d != typespec.end()) {
        ty = d->second;  // no-default: type_spec keyed directly by field name
      } else if (auto r = tget_rev.find(value_temp + '\0' + f); r != tget_rev.end()) {
        if (auto ts = typespec.find(r->second); ts != typespec.end()) {
          ty = ts->second;  // with-default: type_spec on the tuple_get temp
        }
      }
      if (ty.is_invalid()) {
        return;  // a field whose type we cannot read — leave the whole type alone
      }
      const auto tt = type_of(ty);
      if (!Lnast_ntype::is_prim_type_int(tt) && !Lnast_ntype::is_prim_type_bool(tt)) {
        return;  // nested tuple / non-scalar field — defer
      }
      fields.push_back(Field{f, ty});
    }
    if (!fields.empty()) {
      type_fields_[tname] = std::move(fields);
    }
  }

  // Find tuple-typed declares (memory + scalar var) whose element/type is a
  // resolved named type.
  void collect_splits() {
    for (const auto n : src_->depth_preorder(src_->get_root())) {
      if (n.is_invalid() || !Lnast_ntype::is_declare(type_of(n))) {
        continue;
      }
      auto c0 = src_->get_first_child(n);  // var ref
      if (c0.is_invalid() || !Lnast_ntype::is_ref(type_of(c0))) {
        continue;
      }
      auto type_n = src_->get_sibling_next(c0);
      auto mode_n = type_n.is_invalid() ? type_n : src_->get_sibling_next(type_n);
      if (type_n.is_invalid() || mode_n.is_invalid() || !Lnast_ntype::is_const(type_of(mode_n))) {
        continue;
      }
      const std::string var{name_of(c0)};
      const std::string mode{name_of(mode_n)};
      // Only MEMORIES (comp_type_array tuple element) are lowered here. A
      // scalar tuple var (`mut v:Named`) is intentionally left alone: constprop
      // already tracks and resolves it correctly for the comptime case, and
      // splitting it structurally this early breaks whole-tuple assignment,
      // field defaults, and the unknown-field error path. (The runtime-valued
      // scalar tuple case is a separate, pre-existing limitation.)
      if (Lnast_ntype::is_comp_type_array(type_of(type_n))) {
        // Single-dim tuple memory: element is a bare `ref T`, then the '[N]' const.
        auto elem = src_->get_first_child(type_n);
        auto dim  = elem.is_invalid() ? elem : src_->get_sibling_next(elem);
        if (elem.is_invalid() || dim.is_invalid() || !Lnast_ntype::is_ref(type_of(elem))
            || !Lnast_ntype::is_const(type_of(dim))) {
          continue;
        }
        auto tit = type_fields_.find(std::string(name_of(elem)));
        if (tit == type_fields_.end()) {
          continue;  // not a (resolved) tuple element — leave alone
        }
        split_mem_[var] = Split{tit->second, dim, mode};
        drop_types_.insert(std::string(name_of(elem)));
      }
    }
  }

  // Identify the two-node memory read chain and which index temps are safe to
  // drop (consumed ONLY by field-step reads on a split memory).
  void collect_read_chains() {
    // First pass: record index steps `tuple_get(t, mem, idx)` (idx not a field).
    for (const auto n : src_->depth_preorder(src_->get_root())) {
      if (n.is_invalid() || !Lnast_ntype::is_tuple_get(type_of(n))) {
        continue;
      }
      auto d = src_->get_first_child(n);
      auto s = d.is_invalid() ? d : src_->get_sibling_next(d);
      auto x = s.is_invalid() ? s : src_->get_sibling_next(s);
      if (x.is_invalid() || !src_->get_sibling_next(x).is_invalid() || !Lnast_ntype::is_ref(type_of(d))
          || !Lnast_ntype::is_ref(type_of(s))) {
        continue;
      }
      auto mit = split_mem_.find(std::string(name_of(s)));
      if (mit == split_mem_.end()) {
        continue;
      }
      // idx must be an index (not a field name of the memory).
      if (Lnast_ntype::is_const(type_of(x)) && is_field_of(mit->second, name_of(x))) {
        continue;  // whole-array field read — not our index step
      }
      index_step_[std::string(name_of(d))] = Index_step{std::string(name_of(s)), x};
    }
    // Second pass: count consumers of each index temp; a temp is droppable iff
    // every use is a field-step read on it.
    absl::flat_hash_map<std::string, int> uses, field_steps;
    for (const auto n : src_->depth_preorder(src_->get_root())) {
      if (n.is_invalid()) {
        continue;
      }
      const auto t = type_of(n);
      // count any ref use that is NOT the def (first child) of its parent stmt
      auto first = src_->get_first_child(n);
      for (auto c = first.is_invalid() ? first : src_->get_sibling_next(first); !c.is_invalid();
           c      = src_->get_sibling_next(c)) {
        if (Lnast_ntype::is_ref(type_of(c))) {
          if (auto it = index_step_.find(std::string(name_of(c))); it != index_step_.end()) {
            ++uses[it->first];
          }
        }
      }
      if (Lnast_ntype::is_tuple_get(t)) {
        auto d = src_->get_first_child(n);
        auto s = d.is_invalid() ? d : src_->get_sibling_next(d);
        auto f = s.is_invalid() ? s : src_->get_sibling_next(s);
        if (!f.is_invalid() && src_->get_sibling_next(f).is_invalid() && Lnast_ntype::is_ref(type_of(s))
            && Lnast_ntype::is_const(type_of(f))) {
          auto it = index_step_.find(std::string(name_of(s)));
          if (it != index_step_.end()) {
            auto mit = split_mem_.find(it->second.var);
            if (mit != split_mem_.end() && is_field_of(mit->second, name_of(f))) {
              ++field_steps[it->first];
            }
          }
        }
      }
    }
    for (const auto& [tmp, st] : index_step_) {
      auto u = uses.find(tmp);
      auto fs = field_steps.find(tmp);
      if (u != uses.end() && fs != field_steps.end() && u->second == fs->second && fs->second > 0) {
        drop_index_temp_.insert(tmp);
      }
    }
  }

  // ── emit ─────────────────────────────────────────────────────────────────────
  // Build `declare(var.field, [comp_type_array(]TYPE[, '[N]')], mode)`.
  void emit_field_declare(const Lnast_nid& orig, const Lnast_nid& dst_parent, std::string_view var, const Field& f,
                          const Split& s) {
    auto d = dst_->add_child(dst_parent, Lnast_ntype::create_declare());
    carry(orig, d);
    dst_->add_child(d, Lnast_node::create_ref(std::string(var) + std::string(kSep) + f.name));
    if (!s.dim_nid.is_invalid()) {
      auto cta = dst_->add_child(d, Lnast_ntype::create_comp_type_array());
      copy_subtree(f.type_nid, cta);
      copy_subtree(s.dim_nid, cta);
    } else {
      copy_subtree(f.type_nid, d);
    }
    dst_->add_child(d, Lnast_node::create_const(s.mode));
  }

  // Recursively copy children of src_n under dst_parent, applying the rewrites.
  void copy_transformed(const Lnast_nid& src_n, const Lnast_nid& dst_parent) {
    std::string drop_typedef_of;  // non-empty: dropping a dead `type T=(...)` region for this T
    for (auto c = src_->get_first_child(src_n); !c.is_invalid(); c = src_->get_sibling_next(c)) {
      // Drop the contiguous `type T=(...)` region (declare(T,'type') .. store(T,Ttemp))
      // for a type fully consumed by split memories — it is dead and re-emits as
      // bare field refs.
      if (!drop_typedef_of.empty()) {
        if (Lnast_ntype::is_store(type_of(c))) {
          auto a = src_->get_first_child(c);
          if (!a.is_invalid() && Lnast_ntype::is_ref(type_of(a)) && name_of(a) == drop_typedef_of) {
            drop_typedef_of.clear();  // this store(T,Ttemp) terminates the region
          }
        }
        continue;  // drop every node of the region (incl. the terminator)
      }
      if (Lnast_ntype::is_declare(type_of(c))) {
        auto t0 = src_->get_first_child(c);
        auto t1 = t0.is_invalid() ? t0 : src_->get_sibling_next(t0);
        auto t2 = t1.is_invalid() ? t1 : src_->get_sibling_next(t1);
        if (!t0.is_invalid() && !t2.is_invalid() && Lnast_ntype::is_ref(type_of(t0))
            && Lnast_ntype::is_const(type_of(t2)) && name_of(t2) == "type"
            && drop_types_.contains(std::string(name_of(t0)))) {
          drop_typedef_of = std::string(name_of(t0));
          continue;  // drop the typedef declare; the region follows
        }
      }
      if (handle(c, dst_parent)) {
        continue;  // node was rewritten (or dropped) — do not copy verbatim
      }
      // Verbatim copy of this node, recursing into children with rewrites.
      const auto type = type_of(c);
      Lnast_nid  d;
      if (Lnast_ntype::is_ref(type)) {
        d = dst_->add_child(dst_parent, Lnast_node::create_ref(name_of(c)));
      } else if (Lnast_ntype::is_const(type)) {
        d = dst_->add_child(dst_parent, Lnast_node::create_const(name_of(c)));
      } else {
        d = dst_->add_child(dst_parent, type);
      }
      if (Lnast::srcid_carries(type)) {
        carry(c, d);
      }
      copy_transformed(c, d);
    }
  }

  // Returns true if `c` was handled (rewritten or dropped).
  bool handle(const Lnast_nid& c, const Lnast_nid& dst_parent) {
    const auto t = type_of(c);
    if (Lnast_ntype::is_declare(t)) {
      return handle_declare(c, dst_parent);
    }
    if (Lnast_ntype::is_store(t)) {
      return handle_store(c, dst_parent);
    }
    if (Lnast_ntype::is_tuple_get(t)) {
      return handle_tuple_get(c, dst_parent);
    }
    return false;
  }

  bool handle_declare(const Lnast_nid& c, const Lnast_nid& dst_parent) {
    auto c0 = src_->get_first_child(c);
    if (c0.is_invalid() || !Lnast_ntype::is_ref(type_of(c0))) {
      return false;
    }
    const std::string var{name_of(c0)};
    if (auto it = split_mem_.find(var); it != split_mem_.end()) {
      for (const auto& f : it->second.fields) {
        emit_field_declare(c, dst_parent, var, f, it->second);
      }
      return true;
    }
    return false;
  }

  bool handle_store(const Lnast_nid& c, const Lnast_nid& dst_parent) {
    auto c0 = src_->get_first_child(c);
    if (c0.is_invalid() || !Lnast_ntype::is_ref(type_of(c0))) {
      return false;
    }
    const std::string var{name_of(c0)};
    auto              mit = split_mem_.find(var);
    if (mit == split_mem_.end()) {
      return false;
    }
    const Split& s = mit->second;

    // Gather children after the lhs.
    std::vector<Lnast_nid> rest;
    for (auto k = src_->get_sibling_next(c0); !k.is_invalid(); k = src_->get_sibling_next(k)) {
      rest.emplace_back(k);
    }

    // Whole-(array) init `store(V, nil)`: per-field init store with the same value.
    if (rest.size() == 1 && Lnast_ntype::is_const(type_of(rest[0]))) {
      const auto txt = name_of(rest[0]);
      if (txt == "nil" || txt == "0sb?") {
        for (const auto& f : s.fields) {
          auto st = dst_->add_child(dst_parent, Lnast_ntype::create_store());
          carry(c, st);
          dst_->add_child(st, Lnast_node::create_ref(var + std::string(kSep) + f.name));
          dst_->add_child(st, Lnast_node::create_const(txt));
        }
        return true;
      }
      return false;  // a non-nil whole-tuple scalar init — leave verbatim (defer)
    }

    // Field write: find the first field-name const among `rest`.
    int field_pos = -1;
    for (int i = 0; i < static_cast<int>(rest.size()); ++i) {
      if (Lnast_ntype::is_const(type_of(rest[i])) && is_field_of(s, name_of(rest[i]))) {
        field_pos = i;
        break;
      }
    }
    // Need exactly one value after the field selector, and (for a memory) at
    // least one index before it; a scalar var has none.
    if (field_pos < 0 || field_pos != static_cast<int>(rest.size()) - 2) {
      return false;  // not a recognized single-value field write — defer
    }
    const std::string field{name_of(rest[field_pos])};
    auto              st = dst_->add_child(dst_parent, Lnast_ntype::create_store());
    carry(c, st);
    dst_->add_child(st, Lnast_node::create_ref(var + std::string(kSep) + field));
    for (int i = 0; i < field_pos; ++i) {  // index chain (none for scalar var)
      copy_subtree(rest[i], st);
    }
    copy_subtree(rest.back(), st);  // value
    return true;
  }

  bool handle_tuple_get(const Lnast_nid& c, const Lnast_nid& dst_parent) {
    auto d = src_->get_first_child(c);
    auto s = d.is_invalid() ? d : src_->get_sibling_next(d);
    auto x = s.is_invalid() ? s : src_->get_sibling_next(s);
    if (d.is_invalid() || s.is_invalid() || x.is_invalid() || !src_->get_sibling_next(x).is_invalid()
        || !Lnast_ntype::is_ref(type_of(d)) || !Lnast_ntype::is_ref(type_of(s))) {
      return false;
    }
    const std::string dst{name_of(d)};
    const std::string base{name_of(s)};

    // Index step we are fusing away.
    if (drop_index_temp_.contains(dst)) {
      return true;  // drop — its field-step consumers carry the index
    }
    // Field step on a dropped index temp -> fused per-field memory read.
    if (auto it = index_step_.find(base); it != index_step_.end() && drop_index_temp_.contains(base)
        && Lnast_ntype::is_const(type_of(x))) {
      const std::string field{name_of(x)};
      auto              g = dst_->add_child(dst_parent, Lnast_ntype::create_tuple_get());
      carry(c, g);
      dst_->add_child(g, Lnast_node::create_ref(dst));
      dst_->add_child(g, Lnast_node::create_ref(it->second.var + std::string(kSep) + field));
      copy_subtree(it->second.idx_nid, g);
      return true;
    }
    return false;
  }
};

}  // namespace

void uPass_detuple::run(const std::shared_ptr<Lnast>& lnast) {
  if (!lnast) {
    return;
  }
  Detupler d(lnast);
  d.run();
}
