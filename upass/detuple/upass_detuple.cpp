//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_detuple.hpp"

#include <algorithm>
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
  std::string name;      // e.g. "a"
  Lnast_nid   type_nid;  // the prim_type_* subtree (in the SOURCE tree) to copy
  Lnast_nid   reset_nid; // struct-reg only: the const reset subtree to copy (invalid = no reset)
};

class Detupler {
public:
  explicit Detupler(const std::shared_ptr<Lnast>& ln) : src_(ln) {}

  // Returns true if it rewrote the tree (and replaced the body).
  bool run() {
    analyze();
    if (split_mem_.empty() && split_reg_.empty()) {
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
  // ── Struct (scalar tuple) REGISTERS: `reg V:(x:u32,y:u20) = (20,40)` ──
  // LGraph (and tolg's Flop lowering) has no tuples, so a tuple-typed register
  // must become one scalar Flop per leaf field, each carrying the matching
  // slice of the reset value. constprop does NOT handle this (the runtime
  // tuple-reg case is the pre-existing gap), so the split happens here.
  struct Struct_reg {
    std::vector<Field> fields;    // per leaf: name, type, reset value (invalid = no reset)
    std::string        init_tmp;  // the reset-value `tuple_add(init_tmp, …)` temp (dropped)
    std::string        mode;      // declare mode: "reg" (Flop+reset) or "wire"/"mut" (a scalar net per leaf, NO reset)
  };
  absl::flat_hash_map<std::string, Struct_reg> split_reg_;
  absl::flat_hash_set<std::string>             drop_temp_;      // tuple_add dst temps to drop (shape/init bundles)
  absl::flat_hash_set<std::string>             drop_typespec_;  // dotted `type_spec(V.f)` targets to drop
  absl::flat_hash_map<std::string, Lnast_nid>  tadd_of_temp_;   // tuple_add dst temp name -> its tuple_add nid
  // Whole-tuple literal reassign `V = (a=1,b=2)`: bundle temp -> (field -> value nid).
  // handle_struct_reg_store splits `store(V, temp)` into per-field `store(V.f, val)`.
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, Lnast_nid>> whole_write_;
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
      if ((f.name == txt)) {
        return true;
      }
    }
    return false;
  }

  // ── analysis ────────────────────────────────────────────────────────────────
  void analyze() {
    resolve_types();
    collect_splits();
    collect_struct_regs();
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
      fields.push_back(Field{f, ty, Lnast_nid{}});
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

  // Find scalar tuple-typed REGISTERS (`reg V:(x:u32,y:u20) = (20,40)`) and the
  // material to split them into one scalar Flop per field. At this (post-parse,
  // pre-SSA) stage the cluster is, in one stmts block:
  //   tuple_add(INIT, …)               // the reset value (positional consts or store(f,v))
  //   declare(V, prim_type_none, reg, [ref INIT | const 'nil'])
  //   type_spec(f0, T0) type_spec(f1, T1)        // bare-name field types (kept verbatim)
  //   tuple_add(SHAPE, ref f0, ref f1) store(V, ref SHAPE)   // the tuple shape (dropped)
  //   type_spec(V.f0, T0) type_spec(V.f1, T1)    // dotted field types (give the layout)
  // The dotted `type_spec(V.f, T)` nodes carry the field order + concrete types;
  // the reset `tuple_add` carries the per-field reset values. Names (V, INIT,
  // SHAPE) are unique, so a global name-keyed scan is enough — no parent walk.
  void collect_struct_regs() {
    // Pass A: reg declares that are NOT memories (no comp_type_array type).
    struct Cand {
      Lnast_nid   type_nid;
      std::string init_tmp;   // non-empty: ref-init reset bundle temp
      bool        init_nil;   // declare init child is `const 'nil'` (no reset)
      bool        bad;        // a non-const/unsupported reset → leave verbatim
      std::string mode;       // "reg" | "wire" | "mut"
      bool        is_reg;     // mode is reg (gets a Flop + reset); else a plain net per leaf
    };
    absl::flat_hash_map<std::string, Cand> cand;
    for (const auto n : src_->depth_preorder(src_->get_root())) {
      if (n.is_invalid() || !Lnast_ntype::is_declare(type_of(n))) {
        continue;
      }
      auto c0 = src_->get_first_child(n);                                 // var ref
      auto c1 = c0.is_invalid() ? c0 : src_->get_sibling_next(c0);        // type
      auto c2 = c1.is_invalid() ? c1 : src_->get_sibling_next(c1);        // mode const
      if (c0.is_invalid() || c2.is_invalid() || !Lnast_ntype::is_ref(type_of(c0)) || !Lnast_ntype::is_const(type_of(c2))) {
        continue;
      }
      const std::string mode{name_of(c2)};
      const bool        is_reg  = (mode == "reg" || mode.starts_with("reg "));
      const bool        is_wire = (mode == "wire" || mode.starts_with("wire "));
      // A scalar tuple register splits into per-leaf Flops (with reset); a scalar
      // tuple WIRE splits into per-leaf nets (no reset — the value arrives via the
      // field stores / the wire's single driver). Both share the same shape
      // (declare(prim_type_none) + dotted type_specs + field stores/tuple_gets).
      // A scalar tuple `mut` is intentionally LEFT ALONE — constprop resolves it
      // (and emits the field-kind / overflow diagnostics on the bundle); splitting
      // it here would bypass those checks. A flattened-struct bundle that needs
      // hardware lowering is always emitted as a `wire` (slang reader / prp_writer).
      if (!is_reg && !is_wire) {
        continue;
      }
      if (Lnast_ntype::is_comp_type_array(type_of(c1))) {
        continue;  // a memory — handled by collect_splits (or unsupported)
      }
      Cand cd;
      cd.type_nid = c1;
      cd.init_nil = false;
      cd.bad      = false;
      cd.mode     = mode;
      cd.is_reg   = is_reg;
      auto c3     = src_->get_sibling_next(c2);  // optional reset value
      if (!is_reg) {
        cd.init_nil = true;  // wire/mut: no reset child — init/drivers are body stores
      } else if (!c3.is_invalid()) {
        if (Lnast_ntype::is_ref(type_of(c3))) {
          cd.init_tmp = std::string(name_of(c3));
        } else if (Lnast_ntype::is_const(type_of(c3)) && name_of(c3) == "nil") {
          cd.init_nil = true;
        } else {
          cd.bad = true;  // a scalar non-nil const reset on a (would-be) struct — not ours
        }
      } else {
        cd.init_nil = true;  // declared with no reset child
      }
      cand.emplace(std::string(name_of(c0)), std::move(cd));
    }
    if (cand.empty()) {
      return;
    }

    // Pass B: dotted `type_spec(V.f, T)` → ordered (field,type) per candidate V.
    absl::flat_hash_map<std::string, std::vector<Field>> fields;
    for (const auto n : src_->depth_preorder(src_->get_root())) {
      if (n.is_invalid() || !Lnast_ntype::is_type_spec(type_of(n))) {
        continue;
      }
      auto a = src_->get_first_child(n);
      auto b = a.is_invalid() ? a : src_->get_sibling_next(a);
      if (a.is_invalid() || b.is_invalid() || !Lnast_ntype::is_ref(type_of(a))) {
        continue;
      }
      const std::string tgt{name_of(a)};
      const auto        dot = tgt.find('.');
      if (dot == std::string::npos) {
        continue;  // bare-name type_spec — not a dotted field
      }
      const std::string root{tgt.substr(0, dot)};
      const std::string fld{tgt.substr(dot + 1)};
      auto              cit = cand.find(root);
      if (cit == cand.end() || fld.find('.') != std::string::npos) {
        continue;  // not a candidate, or a NESTED field (defer that whole reg)
      }
      const auto tt = type_of(b);
      if (!Lnast_ntype::is_prim_type_int(tt) && !Lnast_ntype::is_prim_type_bool(tt)) {
        cit->second.bad = true;  // non-scalar field type — leave verbatim
        continue;
      }
      auto& fv = fields[root];
      if (std::none_of(fv.begin(), fv.end(), [&](const Field& f) { return f.name == fld; })) {
        fv.push_back(Field{fld, b, Lnast_nid{}});
      }
    }

    // Pass C: index every tuple_add by its dst temp (the shape/reset/whole-write
    // bundles are looked up here and classified by content, not by position).
    for (const auto n : src_->depth_preorder(src_->get_root())) {
      if (n.is_invalid() || !Lnast_ntype::is_tuple_add(type_of(n))) {
        continue;
      }
      auto r = src_->get_first_child(n);
      if (!r.is_invalid() && Lnast_ntype::is_ref(type_of(r))) {
        tadd_of_temp_[std::string(name_of(r))] = n;
      }
    }

    // Finalize: a candidate becomes a split iff its leaf fields are known and
    // its per-field reset resolves (nil = no reset, else all-const values).
    for (auto& [v, cd] : cand) {
      if (cd.bad) {
        continue;
      }
      auto       fit   = fields.find(v);
      const bool typed = (fit != fields.end() && !fit->second.empty());
      std::vector<Field> fv;
      if (typed) {
        fv = fit->second;  // leaf names + concrete types from dotted type_specs
      } else if (!cd.init_nil && !cd.init_tmp.empty()) {
        // Untyped struct reg `reg V = (x=20, y=40)`: derive leaves from a NAMED
        // init tuple. Each leaf is an untyped (prim_type_none) Flop whose width
        // the bitwidth pass infers from its reset value + writes — exactly like an
        // untyped scalar `reg foo = 55`. Positional untyped `(20,40)` is array-like
        // and intentionally left alone.
        auto ait = tadd_of_temp_.find(cd.init_tmp);
        if (ait == tadd_of_temp_.end()) {
          continue;
        }
        bool ok = true;
        for (auto c = src_->get_sibling_next(src_->get_first_child(ait->second)); !c.is_invalid();
             c      = src_->get_sibling_next(c)) {
          auto k   = Lnast_ntype::is_store(type_of(c)) ? src_->get_first_child(c) : Lnast_nid{};
          auto val = k.is_invalid() ? k : src_->get_sibling_next(k);
          if (k.is_invalid() || val.is_invalid() || !Lnast_ntype::is_ref(type_of(k)) || !Lnast_ntype::is_const(type_of(val))) {
            ok = false;  // positional / runtime / malformed entry — leave verbatim
            break;
          }
          fv.push_back(Field{std::string(name_of(k)), Lnast_nid{}, val});  // type inferred; reset = val
        }
        if (!ok || fv.empty()) {
          continue;
        }
      } else {
        continue;  // no leaves to split (e.g. untyped nil reg)
      }
      // Distribute the reset value across the TYPED fields (the untyped branch set
      // each field's reset above). Rides a tuple_add(init_tmp, …): positional bare
      // consts, or named store(field, val) — every value must be a const.
      if (typed && !cd.init_nil) {
        auto ait = tadd_of_temp_.find(cd.init_tmp);
        if (ait == tadd_of_temp_.end()) {
          continue;  // can't find the reset bundle — defer
        }
        std::vector<Lnast_nid> pos;  // positional const values
        absl::flat_hash_map<std::string, Lnast_nid> named;  // field -> const value
        bool ok = true;
        for (auto c = src_->get_sibling_next(src_->get_first_child(ait->second)); !c.is_invalid();
             c      = src_->get_sibling_next(c)) {
          const auto ct = type_of(c);
          if (Lnast_ntype::is_const(ct)) {
            pos.push_back(c);
          } else if (Lnast_ntype::is_store(ct)) {
            auto k = src_->get_first_child(c);                          // field name
            auto val = k.is_invalid() ? k : src_->get_sibling_next(k);  // value
            if (k.is_invalid() || val.is_invalid() || !Lnast_ntype::is_ref(type_of(k)) || !Lnast_ntype::is_const(type_of(val))) {
              ok = false;
              break;
            }
            named[std::string(name_of(k))] = val;
          } else {
            ok = false;  // a runtime (non-const) reset entry — not comptime
            break;
          }
        }
        if (!ok) {
          continue;
        }
        if (!named.empty()) {
          for (auto& f : fv) {
            auto vit = named.find(f.name);
            if (vit == named.end()) {
              ok = false;
              break;
            }
            f.reset_nid = vit->second;
          }
        } else {
          if (pos.size() != fv.size()) {
            continue;  // positional arity mismatch — defer
          }
          for (size_t i = 0; i < fv.size(); ++i) {
            fv[i].reset_nid = pos[i];
          }
        }
        if (!ok) {
          continue;
        }
      }
      Struct_reg sr;
      sr.fields   = std::move(fv);
      sr.init_tmp = cd.init_nil ? std::string{} : cd.init_tmp;
      sr.mode     = cd.mode;
      // SAFETY GATE: only split V when EVERY use of whole-V is one we fully
      // rewrite. Anything else (a whole-tuple read `out = bank`, a non-literal
      // whole write, V handed to a call) would be silently mis-lowered after the
      // split — so leave such a V verbatim and let the prior (loud) tolg error
      // stand. This never makes a working program silently wrong. The drops it
      // discovers (shape/whole bundle temps, per-field whole-writes) commit only
      // on success.
      absl::flat_hash_set<std::string>                                       drop_t;
      absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, Lnast_nid>> wholes;
      if (!validate_struct_reg_uses(v, sr, drop_t, wholes)) {
        continue;  // un-handleable whole-V use — do not split (no regression)
      }
      if (!sr.init_tmp.empty()) {
        drop_temp_.insert(sr.init_tmp);
      }
      for (const auto& f : sr.fields) {
        drop_typespec_.insert(v + std::string(kSep) + f.name);
      }
      drop_temp_.insert(drop_t.begin(), drop_t.end());  // shape + whole-write bundle temps
      for (auto& [tmp, bf] : wholes) {
        whole_write_[tmp] = std::move(bf);
      }
      split_reg_.emplace(v, std::move(sr));
    }
  }

  // Classify a `store(V, ref tmp)` by the CONTENT of tmp's tuple_add literal:
  //   shape  : entries are bare refs naming exactly V's fields (the type-shape store)
  //   whole  : field VALUES (named `store(f,v)` or positional) covering ALL fields
  //            -> `byfield` is filled with field -> value nid
  //   unknown: tmp is not a literal / partial / malformed
  enum class Vstore { shape, whole, unknown };
  Vstore classify_v_store(const Struct_reg& sr, const std::string& tmp, absl::flat_hash_map<std::string, Lnast_nid>& byfield) {
    auto tit = tadd_of_temp_.find(tmp);
    if (tit == tadd_of_temp_.end()) {
      return Vstore::unknown;  // RHS not a tuple literal (reg-to-reg copy, call, …)
    }
    std::vector<Lnast_nid>                      pos;        // positional entries (refs/consts)
    absl::flat_hash_map<std::string, Lnast_nid> named;      // named store(f,val) entries
    std::vector<std::string>                    ref_names;  // names of bare-ref entries
    bool                                        all_ref = true;
    for (auto e = src_->get_sibling_next(src_->get_first_child(tit->second)); !e.is_invalid();
         e      = src_->get_sibling_next(e)) {
      const auto et = type_of(e);
      if (Lnast_ntype::is_ref(et)) {
        ref_names.emplace_back(name_of(e));
        pos.push_back(e);
      } else if (Lnast_ntype::is_store(et)) {
        all_ref  = false;
        auto k   = src_->get_first_child(e);
        auto val = k.is_invalid() ? k : src_->get_sibling_next(k);
        if (k.is_invalid() || val.is_invalid() || !Lnast_ntype::is_ref(type_of(k))) {
          return Vstore::unknown;
        }
        named[std::string(name_of(k))] = val;
      } else {
        all_ref = false;
        pos.push_back(e);  // const value
      }
    }
    // Shape store: all bare refs, one per field, naming exactly the fields.
    if (all_ref && ref_names.size() == sr.fields.size()
        && std::all_of(sr.fields.begin(), sr.fields.end(),
                       [&](const Field& f) { return std::find(ref_names.begin(), ref_names.end(), f.name) != ref_names.end(); })) {
      return Vstore::shape;
    }
    // Whole-tuple write: every field gets a value.
    if (!named.empty()) {
      for (const auto& f : sr.fields) {
        auto it = named.find(f.name);
        if (it == named.end()) {
          return Vstore::unknown;  // partial named write — leave verbatim
        }
        byfield[f.name] = it->second;
      }
      return Vstore::whole;
    }
    if (pos.size() == sr.fields.size()) {
      for (size_t i = 0; i < sr.fields.size(); ++i) {
        byfield[sr.fields[i].name] = pos[i];
      }
      return Vstore::whole;
    }
    return Vstore::unknown;
  }

  // Verify every reference to whole-`v` is a form the split fully rewrites:
  //   declare(v,…) | store(v, 'f', val) | tuple_get(t, v, 'f')
  //   store(v, ref TMP)  where TMP is the type-shape bundle (dropped) OR a
  //                      tuple-literal value covering all fields (split per-field)
  // Returns false on ANY other use of `v` (then the reg is left un-split).
  bool validate_struct_reg_uses(const std::string& v, const Struct_reg& sr, absl::flat_hash_set<std::string>& drop_t,
                                absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, Lnast_nid>>& wholes) {
    for (const auto n : src_->depth_preorder(src_->get_root())) {
      if (n.is_invalid()) {
        continue;
      }
      const auto t = type_of(n);
      int        idx = 0;  // position-aware scan of v among this node's children
      for (auto c = src_->get_first_child(n); !c.is_invalid(); c = src_->get_sibling_next(c), ++idx) {
        if (!Lnast_ntype::is_ref(type_of(c)) || name_of(c) != v) {
          continue;
        }
        if (Lnast_ntype::is_declare(t) && idx == 0) {
          break;  // the declare of v
        }
        if (Lnast_ntype::is_tuple_get(t) && idx == 1) {
          auto f = src_->get_sibling_next(c);  // field read tuple_get(t, v, 'f')
          if (!f.is_invalid() && src_->get_sibling_next(f).is_invalid() && Lnast_ntype::is_const(type_of(f))
              && reg_has_field(sr, name_of(f))) {
            break;
          }
          return false;  // index step / non-field use of v as a base
        }
        if (Lnast_ntype::is_store(t) && idx == 0) {
          std::vector<Lnast_nid> rest;
          for (auto k = src_->get_sibling_next(c); !k.is_invalid(); k = src_->get_sibling_next(k)) {
            rest.emplace_back(k);
          }
          if (rest.size() == 2 && Lnast_ntype::is_const(type_of(rest[0])) && reg_has_field(sr, name_of(rest[0]))) {
            break;  // field write store(v, 'f', val)
          }
          if (rest.size() == 1 && Lnast_ntype::is_const(type_of(rest[0]))
              && (name_of(rest[0]) == "nil" || name_of(rest[0]) == "0sb?")) {
            break;  // whole nil/poison init (wire forward-declare or mut poison) — handled in store rewrite
          }
          if (rest.size() == 1 && Lnast_ntype::is_ref(type_of(rest[0]))) {
            const std::string                           tmp{name_of(rest[0])};
            absl::flat_hash_map<std::string, Lnast_nid> byfield;
            const auto                                  kind = classify_v_store(sr, tmp, byfield);
            if (kind == Vstore::shape) {
              drop_t.insert(tmp);
              break;
            }
            if (kind == Vstore::whole) {
              drop_t.insert(tmp);
              wholes[tmp] = std::move(byfield);
              break;
            }
            return false;  // non-literal / partial whole write
          }
          return false;  // some other store shape on v
        }
        return false;  // v used as a value operand somewhere (whole read, arg, …)
      }
    }
    return true;
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

  // Build `declare(V.f, TYPE, "reg"[, RESET])` for a struct register's leaf.
  // The RESET const (when present) folds into the declare's optional 4th child
  // exactly like a scalar reg's `= value`, so tolg wires it on the Flop's reset.
  void emit_reg_field_declare(const Lnast_nid& orig, const Lnast_nid& dst_parent, std::string_view var, const Field& f) {
    auto d = dst_->add_child(dst_parent, Lnast_ntype::create_declare());
    carry(orig, d);
    dst_->add_child(d, Lnast_node::create_ref(std::string(var) + std::string(kSep) + f.name));
    if (f.type_nid.is_invalid()) {
      dst_->add_child(d, Lnast_ntype::create_prim_type_none());  // untyped leaf — width inferred
    } else {
      copy_subtree(f.type_nid, d);
    }
    dst_->add_child(d, Lnast_node::create_const("reg"));
    if (!f.reset_nid.is_invalid()) {
      copy_subtree(f.reset_nid, d);  // power-on/reset value (nil-reset leaves this out)
    }
  }

  // Build `declare(V.f, TYPE, mode)` for a scalar tuple WIRE/MUT leaf. Unlike a
  // reg leaf there is NO reset child: a `wire` leaf's value is its single
  // continuous driver and a `mut` leaf's is its body stores (both arrive as the
  // per-field stores handle_struct_reg_store / handle_tuple_get rewrite). This is
  // what lets a `wire io:(operation:u5, …)` tuple lower as independent nets — and
  // what makes the slang packed-struct bundle and the prp_writer's re-emitted
  // bundle round-trip through tolg (which has no tuple concept).
  void emit_net_field_declare(const Lnast_nid& orig, const Lnast_nid& dst_parent, std::string_view var, const Field& f,
                              std::string_view mode) {
    auto d = dst_->add_child(dst_parent, Lnast_ntype::create_declare());
    carry(orig, d);
    dst_->add_child(d, Lnast_node::create_ref(std::string(var) + std::string(kSep) + f.name));
    if (f.type_nid.is_invalid()) {
      dst_->add_child(d, Lnast_ntype::create_prim_type_none());  // untyped leaf — width inferred
    } else {
      copy_subtree(f.type_nid, d);
    }
    dst_->add_child(d, Lnast_node::create_const(std::string(mode)));
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
    // Struct-reg housekeeping: drop the now-dead shape/reset bundle temps and
    // the dotted field type_specs (folded into the per-field declares).
    if (Lnast_ntype::is_tuple_add(t) && !drop_temp_.empty()) {
      auto r = src_->get_first_child(c);
      if (!r.is_invalid() && Lnast_ntype::is_ref(type_of(r)) && drop_temp_.contains(std::string(name_of(r)))) {
        return true;  // drop the bundle-building tuple_add
      }
    }
    if (Lnast_ntype::is_type_spec(t) && !drop_typespec_.empty()) {
      auto a = src_->get_first_child(c);
      if (!a.is_invalid() && Lnast_ntype::is_ref(type_of(a)) && drop_typespec_.contains(std::string(name_of(a)))) {
        return true;  // drop the dotted field type_spec
      }
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
    if (auto it = split_reg_.find(var); it != split_reg_.end()) {
      const bool is_reg = it->second.mode == "reg" || it->second.mode.starts_with("reg ");
      for (const auto& f : it->second.fields) {
        if (is_reg) {
          emit_reg_field_declare(c, dst_parent, var, f);
        } else {
          emit_net_field_declare(c, dst_parent, var, f, it->second.mode);  // wire/mut leaf — no reset
        }
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
    if (auto rit = split_reg_.find(var); rit != split_reg_.end()) {
      return handle_struct_reg_store(c, dst_parent, var, rit->second);
    }
    auto mit = split_mem_.find(var);
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

  static bool reg_has_field(const Struct_reg& sr, std::string_view f) {
    return std::any_of(sr.fields.begin(), sr.fields.end(), [&](const Field& x) { return (x.name == f); });
  }

  // Rewrites of a split struct register's stores:
  //   store(V, ref SHAPE)     -> dropped (the tuple-shape bundle is now dead)
  //   store(V, 'f', val)      -> store(V.f, val)            (runtime field write)
  // Anything else (whole-tuple runtime assign) is left verbatim (deferred).
  bool handle_struct_reg_store(const Lnast_nid& c, const Lnast_nid& dst_parent, std::string_view var, const Struct_reg& sr) {
    auto c0 = src_->get_first_child(c);
    std::vector<Lnast_nid> rest;
    for (auto k = src_->get_sibling_next(c0); !k.is_invalid(); k = src_->get_sibling_next(k)) {
      rest.emplace_back(k);
    }
    // Whole nil / poison init `store(V, nil|0sb?)`: a `wire V:(...) = nil` is a
    // forward-declare placeholder whose real drivers are the per-field writes, so
    // DROP it (each leaf wire keeps its single field driver). A `mut V:(...) = nil`
    // poison-inits every leaf (mirrors the scalar mut path).
    if (rest.size() == 1 && Lnast_ntype::is_const(type_of(rest[0]))) {
      const auto txt = name_of(rest[0]);
      if (txt == "nil" || txt == "0sb?") {
        const bool is_wire = sr.mode == "wire" || sr.mode.starts_with("wire ");
        if (!is_wire) {
          for (const auto& f : sr.fields) {
            auto st = dst_->add_child(dst_parent, Lnast_ntype::create_store());
            carry(c, st);
            dst_->add_child(st, Lnast_node::create_ref(std::string(var) + std::string(kSep) + f.name));
            dst_->add_child(st, Lnast_node::create_const(txt));
          }
        }
        return true;  // wire: dropped (field writes drive each leaf)
      }
    }
    if (rest.size() == 2 && Lnast_ntype::is_const(type_of(rest[0])) && reg_has_field(sr, name_of(rest[0]))) {
      const std::string field{name_of(rest[0])};  // field write store(V, 'f', val)
      auto              st = dst_->add_child(dst_parent, Lnast_ntype::create_store());
      carry(c, st);
      dst_->add_child(st, Lnast_node::create_ref(std::string(var) + std::string(kSep) + field));
      copy_subtree(rest[1], st);  // value
      return true;
    }
    if (rest.size() == 1 && Lnast_ntype::is_ref(type_of(rest[0]))) {
      const std::string tmp{name_of(rest[0])};
      // The type-shape bundle `store(V, shape)` is dead after the split — drop it.
      if (drop_temp_.contains(tmp) && whole_write_.find(tmp) == whole_write_.end()) {
        return true;
      }
      // Whole-tuple literal reassign -> one store(V.f, val) per field (validation
      // guaranteed the literal covers every field).
      if (auto wit = whole_write_.find(tmp); wit != whole_write_.end()) {
        for (const auto& f : sr.fields) {
          auto vit = wit->second.find(f.name);
          if (vit == wit->second.end()) {
            continue;
          }
          auto st = dst_->add_child(dst_parent, Lnast_ntype::create_store());
          carry(c, st);
          dst_->add_child(st, Lnast_node::create_ref(std::string(var) + std::string(kSep) + f.name));
          copy_subtree(vit->second, st);  // field value (const or runtime ref/expr)
        }
        return true;
      }
    }
    return false;  // unrecognized shape — leave verbatim
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

    // Struct-reg field read `tuple_get(t, V, 'f')` -> plain copy `store(t, V.f)`
    // (V.f is now a scalar Flop; reads of it are plain q reads).
    if (auto rit = split_reg_.find(base); rit != split_reg_.end() && Lnast_ntype::is_const(type_of(x))
        && reg_has_field(rit->second, name_of(x))) {
      auto st = dst_->add_child(dst_parent, Lnast_ntype::create_store());
      carry(c, st);
      dst_->add_child(st, Lnast_node::create_ref(dst));
      dst_->add_child(st, Lnast_node::create_ref(base + std::string(kSep) + std::string(name_of(x))));
      return true;
    }

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
