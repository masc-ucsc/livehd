//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_coalescer.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include "lnast.hpp"
#include "lnast_ntype.hpp"

// Registered once here (not in the header) to avoid duplicate-registration
// errors when multiple TUs include upass_coalescer.hpp.
static upass::uPass_plugin plugin_coalescer("coalescer", upass::uPass_wrapper<uPass_coalescer>::get_upass);

void uPass_coalescer::set_options(const upass::Options_map& opts) {
  auto it = opts.find("coalescer");
  if (it == opts.end()) {
    return;
  }
  // Match the same lower/0/false/no/off truthiness convention pass.upass uses.
  std::string v = it->second;
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  enabled = !(v.empty() || v == "0" || v == "false" || v == "no" || v == "off");
}

void uPass_coalescer::process_declare() {
  // declare(ref(var), TYPE, const(mode), [value]). Record `mut`-declared names
  // (mode const at child 2). Mirrors uPass_constprop::process_declare. The
  // cursor is restored by the runner's dispatch_to_passes, but be tidy here too.
  const auto here = lm->save_cursor();
  if (move_to_child() && is_type(Lnast_ntype::Lnast_ntype_ref)) {
    auto var = std::string(current_text());
    if (move_to_sibling() && move_to_sibling() && is_type(Lnast_ntype::Lnast_ntype_const)) {
      auto mode = current_text();
      if (mode == "mut" || mode.starts_with("mut ")) {
        mut_decl_names.insert(std::move(var));
      }
    }
  }
  lm->restore_cursor(here);
}

bool uPass_coalescer::is_comptime(std::string_view name) const {
  if (!runner_fold_fn) {
    return false;
  }
  auto folded = runner_fold_fn(name);
  return folded && !folded->is_invalid() && !folded->has_unknowns();
}

void uPass_coalescer::handle_op() {
  if (!enabled) {
    return;
  }

  std::string lhs_text;
  bool        lhs_is_ref = false;

  // Walk children. Child 0 is the LHS (a ref). Subsequent children are
  // operands; each `ref` operand is a read of that name and triggers a
  // pending flush.
  bool got = move_to_child();
  if (got) {
    lhs_is_ref = is_type(Lnast_ntype::Lnast_ntype_ref);
    if (lhs_is_ref) {
      lhs_text = std::string{current_text()};
    }
  } else {
    return;
  }

  while (move_to_sibling()) {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto             ref_text = current_text();
      std::string_view key      = strip_io_prefix(ref_text);
      // Fast path: most refs have no pending entry — probe by string_view
      // (absl's transparent hashing skips the std::string allocation).
      // Avoids one allocation per ref read on the bulk-arithmetic loop.
      auto             p        = pending.find(ref_text);
      if (p == pending.end() && key != ref_text) {
        p = pending.find(key);
      }
      if (p != pending.end()) {
        // Copy the key out before flush_one erases the slot — flush_one
        // takes by std::string& and we want a stable name to pass.
        std::string name = p->first;
        // A comptime-known read is served by fold_ref at the consumer — the
        // parked store need not be materialized for this read to be correct.
        // Skipping the flush lets a later write to the same name supersede
        // (DSE) the parked store. This is what makes `mut a=1; a+=1; a+=1`
        // collapse: each `a+=1` lowers to `plus(_, a, 1)` which READS `a`;
        // without this gate the read flushes the pending store every time, so
        // no store is ever superseded. A genuine runtime read still flushes.
        if (!is_comptime(name)) {
          flush_one(name);
        }
      }
    }
  }
  move_to_parent();

  // Self-read tracking: whether any ref operand in RHS reads the LHS name.
  // A self-read forces flush of the prior pending (so the RHS sees the
  // correct value); after the flush, parking the new stmt is still legal
  // because the new stmt has its own materialized RHS.
  std::string lhs_strip{strip_io_prefix(lhs_text)};

  if (!lhs_is_ref || lhs_text.empty()) {
    return;
  }

  // Boundary signals carry timing & boundary semantics — never park; let the
  // runner emit them normally.
  if (is_boundary(lhs_text)) {
    return;
  }

  // If constprop (or any earlier pass) has already proven the LHS to be a
  // known compile-time constant, leave parking off so constprop's
  // classify_statement::drop fires and the stmt vanishes outright. Parking
  // here would later flush the stmt and bypass that drop, regressing the
  // current behavior.
  //
  // EXCEPT for `mut`-declared names: constprop's classify_statement keeps
  // every store to a mut var (the task-2u guard, see upass_constprop.cpp), so
  // there is no drop to defer to. Park comptime mut writes here so the
  // coalescer's own DSE supersedes the dead ones; the surviving last write is
  // emitted at flush (flush_one never comptime-drops a mut store).
  if (!mut_decl_names.contains(lhs_strip) && is_comptime(lhs_strip)) {
    return;
  }

  park_current(lhs_text);
}

void uPass_coalescer::park_current(const std::string& lhs) {
  // DSE: if a pending for this LHS already exists, discard it. The new write
  // strictly supersedes the old since no read intervened (any read would
  // have flushed in handle_op above).
  auto it = pending.find(lhs);
  if (it != pending.end()) {
    ++stat_dse_dropped;
    pending.erase(it);
  }

  pending.emplace(lhs, lm->get_current_nid());
  ++stat_parked;
  parked_current_stmt = true;
}

bool uPass_coalescer::flush_one(const std::string& name) {
  auto it = pending.find(name);
  if (it == pending.end()) {
    return false;
  }
  Lnast_nid src = it->second;
  pending.erase(it);

  // If by the time we flush, the LHS has become comptime-known (for example,
  // a later constprop fold in this walk resolved it), drop instead of
  // emit — matches what constprop's classify_statement would have voted on
  // the original emit path.
  //
  // EXCEPT a `mut`-declared name: constprop keeps comptime mut stores (task-2u
  // guard), and a comptime-eliminated block (`if true{…}`, unrolled loop) may
  // read the bare mut ref expecting this store as its driver. So emit the
  // surviving mut store rather than comptime-dropping it — the coalescer's
  // DSE has already removed the superseded ones, leaving just the last write.
  if (!mut_decl_names.contains(std::string(strip_io_prefix(name))) && is_comptime(strip_io_prefix(name))) {
    return false;
  }

  if (runner_emit_at_fn) {
    runner_emit_at_fn(src);
    ++stat_flushed;
    return true;
  }
  return false;
}

void uPass_coalescer::flush_all() {
  if (pending.empty()) {
    return;
  }
  // Snapshot keys first — flush_one mutates `pending`. flat_hash_map gives
  // unspecified iteration order, so sort the keys to keep the emitted order
  // deterministic across runs (matches the std::map behavior this replaced).
  std::vector<std::string> keys;
  keys.reserve(pending.size());
  for (const auto& kv : pending) {
    keys.emplace_back(kv.first);
  }
  std::sort(keys.begin(), keys.end());
  for (const auto& k : keys) {
    flush_one(k);
  }
}
