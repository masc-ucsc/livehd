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

void uPass_coalescer::handle_op() {
  if (!enabled) {
    return;
  }

  // Walk children. Child 0 is the LHS (a ref). Subsequent children are
  // operands; each `ref` operand is a read of that name and triggers a
  // pending flush. We don't strip I/O prefixes for the read trigger because
  // pending keys are stored in the same prefixed form they were parked with.
  bool        got = move_to_child();
  std::string lhs_text;
  bool        lhs_is_ref = false;
  if (got) {
    lhs_is_ref = is_type(Lnast_ntype::Lnast_ntype_ref);
    if (lhs_is_ref) {
      lhs_text = std::string{current_text()};
    }
  } else {
    return;
  }

  // Self-read tracking: whether any ref operand in RHS reads the LHS name.
  // A self-read forces flush of the prior pending (so the RHS sees the
  // correct value); after the flush, parking the new stmt is still legal
  // because the new stmt has its own materialized RHS.
  std::string lhs_strip{strip_io_prefix(lhs_text)};

  while (move_to_sibling()) {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto             ref_text = current_text();
      std::string_view key      = strip_io_prefix(ref_text);
      // Try the stripped key first (constprop's storage convention) and the
      // raw text as a fallback. Pending uses raw LHS text so a `%out` write
      // and a downstream `%out` read both match without the strip.
      auto p = pending.find(std::string{ref_text});
      if (p == pending.end() && std::string{key} != std::string{ref_text}) {
        p = pending.find(std::string{key});
      }
      if (p != pending.end()) {
        flush_one(p->first);
      }
    }
    // const operands and nested subtrees (e.g. tuple_add's assign children)
    // don't trigger reads at this level. The runner's emit_op_with_fold
    // handles them verbatim when the op finally emits.
  }
  move_to_parent();

  if (!lhs_is_ref || lhs_text.empty()) {
    return;
  }

  // Boundary signals (regs / inputs / outputs) carry timing & boundary
  // semantics — never park; let the runner emit them normally.
  if (is_boundary(lhs_text)) {
    return;
  }

  // If constprop (or any earlier pass) has already proven the LHS to be a
  // known compile-time constant, leave parking off so constprop's
  // classify_statement::drop fires and the stmt vanishes outright. Parking
  // here would later flush the stmt and bypass that drop, regressing the
  // current behavior.
  if (runner_fold_fn) {
    auto folded = runner_fold_fn(lhs_strip);
    if (folded && !folded->is_invalid() && !folded->has_unknowns()) {
      return;
    }
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
  // a downstream constprop iteration folded it through), drop instead of
  // emit — matches what constprop's classify_statement would have voted on
  // the original emit path.
  if (runner_fold_fn) {
    auto folded = runner_fold_fn(strip_io_prefix(name));
    if (folded && !folded->is_invalid() && !folded->has_unknowns()) {
      return false;
    }
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
  // Snapshot keys first — flush_one mutates `pending`. Order: insertion order
  // would be ideal but std::map gives us name order, which is stable and
  // deterministic. Reorder is acceptable in hardware (no aliasing).
  std::vector<std::string> keys;
  keys.reserve(pending.size());
  for (const auto& kv : pending) {
    keys.emplace_back(kv.first);
  }
  for (const auto& k : keys) {
    flush_one(k);
  }
}
