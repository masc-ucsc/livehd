//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// 2g — front-end lambda split. This is the materialize-free replacement for the
// old runner-based `func_extract` upass: instead of running a full uPass_runner
// (which re-materialized the WHOLE module tree via emit_* even on inputs with no
// lambdas), the split is a direct DFS + a one-shot rebuild that only fires when a
// `comb`/`pipe`/`mod` func_def is actually present. The IR it produces is the
// same the old pass produced — one `top -> [io, stmts]` Lnast per lambda, the
// func_defs dropped from the module tree — so SSA/bitwidth/tolg/the inliner all
// consume identical trees and the runner's template inline/specialize machinery
// is untouched.

#include "upass_func_extract.hpp"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dlop.hpp"
#include "hhds/tree.hpp"
#include "lnast_manager.hpp"
#include "lnast_ntype.hpp"

namespace upass {

namespace {

// Resolve a child cursor node to a Dlop. Dlop literals decode via from_pyrope;
// refs resolve through latest_outer_value first (named outer scalars), then
// temp_scalar_value (SSA temps). Returns nullopt on anything we can't statically
// resolve. (Verbatim from the old func_extract pass.)
std::optional<Dlop> resolve_child_scalar(const std::string& name, bool is_ref, bool is_const,
                                         const std::unordered_map<std::string, Dlop>& latest,
                                         const std::unordered_map<std::string, Dlop>& temps) {
  if (is_const) {
    try {
      return *Dlop::from_pyrope(name);
    } catch (...) {
      return std::nullopt;
    }
  }
  if (is_ref) {
    auto it = latest.find(name);
    if (it != latest.end()) {
      return it->second;
    }
    auto it2 = temps.find(name);
    if (it2 != temps.end()) {
      return it2->second;
    }
  }
  return std::nullopt;
}

// The split is a stateful single DFS over the module tree (mirroring the old
// runner walk + func_extract pass hooks) followed by a rebuild that drops the
// extracted func_defs. All cursor navigation goes through an Lnast_manager so
// the closure-capture / io-copy logic can be ported from the pass verbatim.
struct Lambda_extractor {
  explicit Lambda_extractor(const std::shared_ptr<Lnast>& ln) : lm(std::make_shared<Lnast_manager>(ln)) {}

  std::shared_ptr<Lnast_manager> lm;

  std::vector<std::shared_ptr<Lnast>> extracted_lnasts;
  std::unordered_set<std::string>     extracted_names;
  // Class-index keys of the func_def nodes pulled out — the rebuild skips these
  // subtrees (and only these) when copying the module body.
  std::unordered_set<uint64_t> drop_keys;

  // Live walk-time capture state — identical to the old pass. See
  // upass_func_extract.hpp history for the per-map rationale; in short:
  // latest_outer_value/bundle/import hold the comptime constants visible at the
  // current def site, temp_* recover SSA-temp values, outer_non_const records
  // names disqualified from capture.
  std::unordered_map<std::string, Dlop>                                  latest_outer_value;
  std::unordered_map<std::string, std::unordered_map<std::string, Dlop>> latest_outer_bundle;
  std::unordered_map<std::string, Dlop>                                  temp_scalar_value;
  std::unordered_map<std::string, std::unordered_map<std::string, Dlop>> temp_bundle_value;
  std::unordered_map<std::string, std::string>                           temp_import_text;
  std::unordered_map<std::string, std::string>                           latest_outer_import;
  std::unordered_set<std::string>                                        outer_non_const;

  int  stmts_depth{0};
  bool drop_current_func_def{false};
  bool any_dropped{false};

  // ── cursor convenience wrappers (so the ported bodies read unchanged) ──
  bool             move_to_child() { return lm->move_to_child(); }
  bool             move_to_sibling() { return lm->move_to_sibling(); }
  void             move_to_parent() { lm->move_to_parent(); }
  std::string_view current_text() const { return lm->current_text(); }

  static uint64_t key_of(const Lnast_nid& nid) { return static_cast<uint64_t>(nid.get_class_index().value); }

  // ── tree-copy helpers (into an extracted-function Lnast; verbatim) ──────
  void copy_current_subtree(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent) {
    const auto type       = lm->current_type();
    auto       new_parent = (Lnast_ntype::is_ref(type) || Lnast_ntype::is_const(type)) ? dst->add_child(parent, lm->current_node())
                                                                                       : dst->add_child(parent, type);
    if (Lnast::srcid_carries(type)) {
      const auto& src = lm->get_lnast();
      if (const auto id = src->get_srcid(lm->get_current_nid()); id != hhds::SourceId_invalid) {
        dst->set_srcid(new_parent, dst->source_locator().import_from(src->source_locator(), id));
      }
    }
    if (!lm->has_child()) {
      return;
    }
    move_to_child();
    do {
      copy_current_subtree(dst, new_parent);
    } while (move_to_sibling());
    move_to_parent();
  }

  void copy_current_children(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent) {
    if (!lm->has_child()) {
      return;
    }
    move_to_child();
    do {
      copy_current_subtree(dst, parent);
    } while (move_to_sibling());
    move_to_parent();
  }

  // Copy the func_def's input/output signature subtree into the extracted lnast
  // under `io_idx` (composite tuple types stay nested; SSA flattens later).
  bool emit_io_tuple_from_decl(const std::shared_ptr<Lnast>& dst, const Lnast_nid& io_idx) {
    if (!lm->has_child()) {
      auto tup_idx = dst->add_child(io_idx, Lnast_ntype::create_tuple_add());
      dst->add_child(tup_idx, Lnast_node::create_ref("__empty_tuple"));
      return false;
    }
    auto tup_idx = dst->add_child(io_idx, Lnast_ntype::create_tuple_add());
    copy_current_children(dst, tup_idx);
    return true;
  }

  // Stamp the deferred-template flag when the signature is not fully typed.
  void stamp_template_if_untyped(const std::shared_ptr<Lnast>& dst) {
    if (dst->has_generics()) {
      dst->set_template(true);
      return;
    }
    auto root = dst->get_root();
    auto io_n = dst->get_first_child(root);
    if (io_n.is_invalid() || !Lnast_ntype::is_io(dst->get_type(io_n))) {
      return;
    }
    auto in_tup = dst->get_first_child(io_n);
    if (in_tup.is_invalid()) {
      return;
    }
    for (auto entry : dst->children(in_tup)) {
      if (!Lnast_ntype::is_store(dst->get_type(entry))) {
        continue;
      }
      auto name_n = dst->get_first_child(entry);
      if (name_n.is_invalid()) {
        continue;
      }
      const auto nm = dst->get_name(name_n);
      if (nm == "self" || nm == "__empty_tuple") {
        continue;
      }
      auto def_n = dst->get_sibling_next(name_n);
      if (!def_n.is_invalid() && Lnast_ntype::is_const(dst->get_type(def_n)) && dst->get_name(def_n) == "...") {
        dst->set_template(true);
        return;
      }
      auto type_n = def_n.is_invalid() ? def_n : dst->get_sibling_next(def_n);
      if (type_n.is_invalid()) {
        dst->set_template(true);
        return;
      }
    }
  }

  // ── capture hooks (ported from the pass; cursor must sit on the node) ───
  template <typename Op>
  void fold_temp_nary(Op op) {
    if (!lm->has_child()) {
      return;
    }
    const auto saved = lm->save_cursor();
    lm->move_to_child();
    if (!Lnast_ntype::is_ref(lm->current_type())) {
      lm->restore_cursor(saved);
      return;
    }
    std::string lhs_name(lm->current_text());
    if (!lm->move_to_sibling()) {
      lm->restore_cursor(saved);
      return;
    }
    std::optional<Dlop> acc = resolve_child_scalar(std::string(lm->current_text()),
                                                   Lnast_ntype::is_ref(lm->current_type()),
                                                   Lnast_ntype::is_const(lm->current_type()),
                                                   latest_outer_value,
                                                   temp_scalar_value);
    if (!acc.has_value() || acc->is_invalid()) {
      lm->restore_cursor(saved);
      return;
    }
    while (lm->move_to_sibling()) {
      auto rhs = resolve_child_scalar(std::string(lm->current_text()),
                                      Lnast_ntype::is_ref(lm->current_type()),
                                      Lnast_ntype::is_const(lm->current_type()),
                                      latest_outer_value,
                                      temp_scalar_value);
      if (!rhs.has_value() || rhs->is_invalid()) {
        lm->restore_cursor(saved);
        return;
      }
      try {
        *acc = op(*acc, *rhs);
      } catch (...) {
        lm->restore_cursor(saved);
        return;
      }
      if (acc->is_invalid()) {
        lm->restore_cursor(saved);
        return;
      }
    }
    temp_scalar_value[lhs_name] = *acc;
    lm->restore_cursor(saved);
  }

  void process_tuple_add() {
    if (!lm->has_child()) {
      return;
    }
    const auto saved = lm->save_cursor();
    lm->move_to_child();
    if (!Lnast_ntype::is_ref(lm->current_type())) {
      lm->restore_cursor(saved);
      return;
    }
    std::string lhs_name(lm->current_text());

    {
      const auto peek = lm->save_cursor();
      if (lm->move_to_sibling() && Lnast_ntype::is_const(lm->current_type()) && lm->is_last_child()) {
        std::string ctext(lm->current_text());
        lm->restore_cursor(saved);
        try {
          temp_scalar_value[lhs_name] = *Dlop::from_pyrope(ctext);
        } catch (...) {
        }
        return;
      }
      lm->restore_cursor(peek);
    }

    std::unordered_map<std::string, Dlop> fields;
    while (lm->move_to_sibling()) {
      if (!Lnast_ntype::is_store(lm->current_type()) || !lm->has_child()) {
        lm->restore_cursor(saved);
        return;
      }
      lm->move_to_child();
      if (!Lnast_ntype::is_ref(lm->current_type())) {
        lm->move_to_parent();
        lm->restore_cursor(saved);
        return;
      }
      std::string key(lm->current_text());
      if (!lm->move_to_sibling()) {
        lm->move_to_parent();
        lm->restore_cursor(saved);
        return;
      }
      auto val = resolve_child_scalar(std::string(lm->current_text()),
                                      Lnast_ntype::is_ref(lm->current_type()),
                                      Lnast_ntype::is_const(lm->current_type()),
                                      latest_outer_value,
                                      temp_scalar_value);
      lm->move_to_parent();
      if (!val.has_value() || val->is_invalid()) {
        lm->restore_cursor(saved);
        return;
      }
      fields[key] = *val;
    }
    if (!fields.empty()) {
      temp_bundle_value[lhs_name] = std::move(fields);
    }
    lm->restore_cursor(saved);
  }

  void process_func_call() {
    if (!lm->has_child()) {
      return;
    }
    const auto saved = lm->save_cursor();
    lm->move_to_child();
    std::string dst;
    if (Lnast_ntype::is_ref(lm->current_type())) {
      dst = std::string(lm->current_text());
    }
    if (!dst.empty() && lm->move_to_sibling() && Lnast_ntype::is_const(lm->current_type()) && lm->current_raw_text() == "import"
        && lm->move_to_sibling() && Lnast_ntype::is_const(lm->current_type())) {
      temp_import_text[dst] = std::string(lm->current_raw_text());
    }
    lm->restore_cursor(saved);
  }

  void process_assign() {
    if (!lm->has_child()) {
      return;
    }
    const auto saved = lm->save_cursor();
    lm->move_to_child();
    std::string lhs_name;
    if (Lnast_ntype::is_ref(lm->current_type())) {
      lhs_name = std::string(lm->current_text());
    }
    if (lhs_name.empty() || !lm->move_to_sibling()) {
      lm->restore_cursor(saved);
      return;
    }
    const bool  rhs_is_const = Lnast_ntype::is_const(lm->current_type());
    const bool  rhs_is_ref   = Lnast_ntype::is_ref(lm->current_type());
    std::string rhs_text;
    if (rhs_is_const || rhs_is_ref) {
      rhs_text = std::string(lm->current_text());
    }
    lm->restore_cursor(saved);

    auto invalidate = [&]() {
      latest_outer_value.erase(lhs_name);
      latest_outer_bundle.erase(lhs_name);
      latest_outer_import.erase(lhs_name);
      outer_non_const.insert(lhs_name);
    };

    if (outer_non_const.contains(lhs_name)) {
      return;
    }
    if (latest_outer_value.contains(lhs_name) || latest_outer_bundle.contains(lhs_name)
        || latest_outer_import.contains(lhs_name)) {
      invalidate();
      return;
    }
    if (stmts_depth > 1) {
      invalidate();
      return;
    }
    if (rhs_is_const) {
      try {
        latest_outer_value[lhs_name] = *Dlop::from_pyrope(rhs_text);
      } catch (...) {
        invalidate();
      }
      return;
    }
    if (rhs_is_ref) {
      auto sit = temp_scalar_value.find(rhs_text);
      if (sit != temp_scalar_value.end()) {
        latest_outer_value[lhs_name] = sit->second;
        return;
      }
      auto bit = temp_bundle_value.find(rhs_text);
      if (bit != temp_bundle_value.end()) {
        latest_outer_bundle[lhs_name] = bit->second;
        return;
      }
      auto imit = temp_import_text.find(rhs_text);
      if (imit != temp_import_text.end()) {
        latest_outer_import[lhs_name] = imit->second;
        return;
      }
      auto lvit = latest_outer_value.find(rhs_text);
      if (lvit != latest_outer_value.end()) {
        latest_outer_value[lhs_name] = lvit->second;
        return;
      }
      auto lbit = latest_outer_bundle.find(rhs_text);
      if (lbit != latest_outer_bundle.end()) {
        latest_outer_bundle[lhs_name] = lbit->second;
        return;
      }
    }
    invalidate();
  }

  // Extract the func_def under the cursor (comb/pipe/mod only). Sets
  // drop_current_func_def when the node is to be removed from the module tree.
  // Ported from the old pass's process_func_def.
  void process_func_def() {
    drop_current_func_def = false;

    if (!lm->has_child()) {
      return;
    }

    move_to_child();
    const auto func_name = std::string(current_text());
    if (!move_to_sibling()) {
      move_to_parent();
      return;
    }
    const auto func_kind = std::string(current_text());

    if ((func_kind != "comb" && func_kind != "pipe" && func_kind != "mod") || func_name.empty()) {
      move_to_parent();
      return;
    }

    const auto extracted_name = std::string(lm->get_top_module_name()) + "." + func_name;
    drop_current_func_def     = true;

    if (extracted_names.contains(extracted_name)) {
      move_to_parent();
      return;
    }
    extracted_names.insert(extracted_name);

    auto new_lnast = std::make_shared<Lnast>(extracted_name);
    new_lnast->set_lambda_kind(func_kind);
    if (const auto& file_ln = lm->get_lnast(); file_ln) {
      for (const auto& p : file_ln->get_pub_list()) {
        if (p.name == func_name && !p.lg.empty()) {
          new_lnast->set_lg_name(p.lg);
          break;
        }
      }
    }
    auto root_nid = new_lnast->set_root(Lnast_ntype::create_top());

    {
      const auto& src = lm->get_lnast();
      auto        nid = lm->get_current_nid();
      auto        id  = src->get_srcid(nid);
      while (id == hhds::SourceId_invalid && nid.is_valid()) {
        nid = src->get_parent(nid);
        if (!nid.is_valid()) {
          break;
        }
        id = src->get_srcid(nid);
      }
      if (id != hhds::SourceId_invalid) {
        new_lnast->set_srcid(root_nid, new_lnast->source_locator().import_from(src->source_locator(), id));
      }
    }

    std::vector<std::string> generics;
    if (move_to_sibling()) {  // kind -> generics
      if (lm->has_child()) {
        const auto saved_g = lm->save_cursor();
        move_to_child();
        do {
          if (Lnast_ntype::is_ref(lm->current_type())) {
            generics.emplace_back(lm->current_text());
          }
        } while (move_to_sibling());
        lm->restore_cursor(saved_g);
      }
    }
    new_lnast->set_generics(std::move(generics));

    if (!move_to_sibling()) {  // generics -> inputs
      new_lnast->add_child(root_nid, Lnast_ntype::create_stmts());
      move_to_parent();
      if (new_lnast->has_generics()) {
        new_lnast->set_template(true);
      }
      extracted_lnasts.emplace_back(std::move(new_lnast));
      return;
    }

    auto io_idx = new_lnast->add_child(root_nid, Lnast_ntype::create_io());
    auto stmts  = new_lnast->add_child(root_nid, Lnast_ntype::create_stmts());

    emit_io_tuple_from_decl(new_lnast, io_idx);

    if (move_to_sibling()) {
      emit_io_tuple_from_decl(new_lnast, io_idx);
    } else {
      auto tup_idx = new_lnast->add_child(io_idx, Lnast_ntype::create_tuple_add());
      new_lnast->add_child(tup_idx, Lnast_node::create_ref("__empty_tuple"));
    }

    if (move_to_sibling()) {
      if (!latest_outer_value.empty() || !latest_outer_bundle.empty() || !latest_outer_import.empty()) {
        std::unordered_set<std::string> body_refs;
        {
          const auto&                           src      = lm->get_lnast();
          const auto                            body_nid = lm->get_current_nid();
          std::function<void(const Lnast_nid&)> walk     = [&](const Lnast_nid& nid) {
            if (nid.is_invalid()) {
              return;
            }
            if (Lnast_ntype::is_ref(src->get_type(nid))) {
              body_refs.insert(std::string(src->get_name(nid)));
            }
            for (auto c = src->get_child(nid); !c.is_invalid(); c = src->get_sibling_next(c)) {
              walk(c);
            }
          };
          walk(body_nid);
        }
        std::unordered_set<std::string> local_io;
        for (auto sig : new_lnast->children(io_idx)) {
          for (auto entry : new_lnast->children(sig)) {
            if (!Lnast_ntype::is_store(new_lnast->get_type(entry))) {
              continue;
            }
            auto nm = new_lnast->get_first_child(entry);
            if (!nm.is_invalid()) {
              local_io.insert(std::string(new_lnast->get_name(nm)));
            }
          }
        }
        for (const auto& name : body_refs) {
          if (local_io.contains(name)) {
            continue;
          }
          auto it = latest_outer_value.find(name);
          if (it != latest_outer_value.end()) {
            auto a_idx = new_lnast->add_child(stmts, Lnast_ntype::create_store());
            new_lnast->add_child(a_idx, Lnast_node::create_ref(name));
            new_lnast->add_child(a_idx, Lnast_node::create_const(it->second.to_pyrope()));
            continue;
          }
          if (auto iit = latest_outer_import.find(name); iit != latest_outer_import.end()) {
            auto f_idx = new_lnast->add_child(stmts, Lnast_ntype::create_func_call());
            new_lnast->add_child(f_idx, Lnast_node::create_ref(name));
            new_lnast->add_child(f_idx, Lnast_node::create_const("import"));
            new_lnast->add_child(f_idx, Lnast_node::create_const(iit->second));
            continue;
          }
          auto bit = latest_outer_bundle.find(name);
          if (bit != latest_outer_bundle.end()) {
            for (const auto& [field, val] : bit->second) {
              auto a_idx = new_lnast->add_child(stmts, Lnast_ntype::create_store());
              new_lnast->add_child(a_idx, Lnast_node::create_ref(name + "." + field));
              new_lnast->add_child(a_idx, Lnast_node::create_const(val.to_pyrope()));
            }
          }
        }
      }
      copy_current_children(new_lnast, stmts);
    }

    stamp_template_if_untyped(new_lnast);

    move_to_parent();
    extracted_lnasts.emplace_back(std::move(new_lnast));
  }

  // ── the single DFS over the module tree ─────────────────────────────────
  // Mirrors the old runner walk: pre-order, fire the capture hook on each node,
  // and at a comb/pipe/mod func_def extract + mark-drop WITHOUT descending (the
  // body rides into the extracted tree verbatim, so nested defs are not split —
  // exactly as process_drop_candidate_verbatim behaved).
  void walk_children(const Lnast_nid& parent) {
    const auto& ln = lm->get_lnast();
    for (auto c = ln->get_first_child(parent); !c.is_invalid(); c = ln->get_sibling_next(c)) {
      const auto t = ln->get_type(c);

      if (Lnast_ntype::is_func_def(t)) {
        lm->move_to_nid(c);
        process_func_def();
        if (drop_current_func_def) {
          drop_keys.insert(key_of(c));
          any_dropped          = true;
          drop_current_func_def = false;
        }
        continue;  // never descend into a func_def body (verbatim, like the old pass)
      }

      lm->move_to_nid(c);
      if (Lnast_ntype::is_store(t)) {
        if (lm->current_num_children() <= 2) {  // scalar store routes to the assign capture (Task 1t: assign==store)
          process_assign();
        }
      } else if (Lnast_ntype::is_plus(t)) {
        fold_temp_nary([](const Dlop& a, const Dlop& b) { return *a.add_op(b); });
      } else if (Lnast_ntype::is_minus(t)) {
        fold_temp_nary([](const Dlop& a, const Dlop& b) { return *a.sub_op(b); });
      } else if (Lnast_ntype::is_mult(t)) {
        fold_temp_nary([](const Dlop& a, const Dlop& b) { return *a.mult_op(b); });
      } else if (Lnast_ntype::is_div(t)) {
        fold_temp_nary([](const Dlop& a, const Dlop& b) { return *a.div_op(b); });
      } else if (Lnast_ntype::is_bit_and(t)) {
        fold_temp_nary([](const Dlop& a, const Dlop& b) { return *a.and_op(b); });
      } else if (Lnast_ntype::is_bit_or(t)) {
        fold_temp_nary([](const Dlop& a, const Dlop& b) { return *a.or_op(b); });
      } else if (Lnast_ntype::is_bit_xor(t)) {
        fold_temp_nary([](const Dlop& a, const Dlop& b) { return *a.xor_op(b); });
      } else if (Lnast_ntype::is_tuple_add(t)) {
        process_tuple_add();
      } else if (Lnast_ntype::is_func_call(t)) {
        process_func_call();
      }

      const bool is_stmts = Lnast_ntype::is_stmts(t);
      if (is_stmts) {
        ++stmts_depth;
      }
      walk_children(c);
      if (is_stmts) {
        --stmts_depth;
      }
    }
  }

  // Rebuild the module body without the extracted func_defs. srcids copy
  // verbatim — they index the original locator, which replace_body preserves.
  void copy_skipping(const std::shared_ptr<Lnast>& src, const Lnast_nid& src_node, const std::shared_ptr<Lnast>& dst,
                     const Lnast_nid& dst_node) {
    for (auto c = src->get_first_child(src_node); !c.is_invalid(); c = src->get_sibling_next(c)) {
      if (drop_keys.contains(key_of(c))) {
        continue;
      }
      const auto type = src->get_type(c);
      Lnast_nid  new_c;
      if (Lnast_ntype::is_ref(type)) {
        new_c = dst->add_child(dst_node, Lnast_node::create_ref(src->get_name(c)));
      } else if (Lnast_ntype::is_const(type)) {
        new_c = dst->add_child(dst_node, Lnast_node::create_const(src->get_name(c)));
      } else {
        new_c = dst->add_child(dst_node, type);
      }
      if (Lnast::srcid_carries(type)) {
        if (const auto id = src->get_srcid(c); id != hhds::SourceId_invalid) {
          dst->set_srcid(new_c, id);
        }
      }
      copy_skipping(src, c, dst, new_c);
    }
  }

  std::vector<std::shared_ptr<Lnast>> run() {
    const auto& ln = lm->get_lnast();

    // Cheap pre-scan: nothing to do on a function-free module (the common case,
    // and the whole win — the old pass re-materialized the tree regardless).
    bool has_lambda = false;
    for (auto n : ln->depth_preorder(ln->get_root())) {
      if (n.is_invalid()) {
        continue;
      }
      if (Lnast_ntype::is_func_def(ln->get_type(n))) {
        has_lambda = true;
        break;
      }
    }
    if (!has_lambda) {
      return {};
    }

    walk_children(ln->get_root());

    if (any_dropped) {
      // Rebuild the module body once, dropping the extracted func_defs. A
      // forest-independent temp tree (Tree::create(nullptr)) so it survives
      // after this forest goes away — exactly the runner's staging pattern.
      auto forest = hhds::Forest::create();
      auto name   = std::string(ln->get_top_module_name());
      auto body   = forest->create_tree_temp(name);
      auto out    = std::make_shared<Lnast>(body, name);

      auto src_root = ln->get_root();
      auto dst_root = out->set_root(ln->get_type(src_root));
      if (const auto id = ln->get_srcid(src_root); id != hhds::SourceId_invalid) {
        out->set_srcid(dst_root, id);
      }
      copy_skipping(ln, src_root, out, dst_root);

      ln->replace_body(out->tree_ptr());
    }

    return std::move(extracted_lnasts);
  }
};

}  // namespace

std::vector<std::shared_ptr<Lnast>> extract_lambda_functions(const std::shared_ptr<Lnast>& ln) {
  Lambda_extractor ex(ln);
  return ex.run();
}

}  // namespace upass
