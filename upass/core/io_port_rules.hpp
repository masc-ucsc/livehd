//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hlop/dlop.hpp"
#include "lnast.hpp"

// Call-binding rules for a callee's declared inputs, shared by the runner (a
// comb call it inlines) and upass.tolg (a Sub instance it wires), so an omitted
// input binds the same way on both paths.
namespace upass::io_port {

// Clocks and resets bind BY TYPE (docs 07-typesystem "Clock and Reset", 04b
// "Implicit clock and reset"): an input typed `Clock` is a clock and one typed
// `Reset` is a reset, whatever its name; an input merely NAMED clk/rst is data.
// A unit read from Verilog has no Clock/Reset type, so upass.ssa stamps its
// 1-bit inputs named by the Verilog reader's convention (verilog_clock_name /
// verilog_reset_name) with the class instead -- the one place a name decides.
[[nodiscard]] inline bool verilog_clock_name(std::string_view n) { return n == "clock" || n == "clk"; }
[[nodiscard]] inline bool verilog_reset_name(std::string_view n) {
  return n == "reset" || n == "rst" || n == "reset_n" || n == "rst_n";
}
// A clock / reset input of a callee (the implicit-clock/reset candidates).
[[nodiscard]] inline bool is_clock_candidate(const Lnast_io_entry& e) { return e.sig == Io_sig::clock; }
[[nodiscard]] inline bool is_reset_candidate(const Lnast_io_entry& e) { return e.sig == Io_sig::reset; }
// The named input of `io` is a Clock / Reset.
[[nodiscard]] inline bool is_clock_input(const Lnast_tree_io& io, std::string_view n) {
  const auto* e = io.find(n);
  return e != nullptr && e->sig == Io_sig::clock;
}
[[nodiscard]] inline bool is_reset_input(const Lnast_tree_io& io, std::string_view n) {
  const auto* e = io.find(n);
  return e != nullptr && e->sig == Io_sig::reset;
}

// A Verilog reset port's polarity by its name alone: the `_n` suffix is
// active-low. Pyrope gives a name no polarity (docs 04b: `rst_n` is
// active-high without `negreset=true`), so only a Verilog-origin unit uses it.
[[nodiscard]] inline bool reset_name_active_low(std::string_view n) { return n.size() > 2 && n.ends_with("_n"); }

// The polarity at which the regs of `callee` see reset input `port` asserted
// (true = active-low): active-high unless a reg on it sets `negreset=true`
// (docs 04b; a Verilog-origin callee defaults to its `_n` name). A reg is on
// `port` when it names it (`reset_pin=port`) or names no reset pin (the
// implicit reset, which `port` is when a caller auto-wires it). nullopt when
// the regs on it disagree. The wire itself always crosses a call unchanged
// (docs 04b); this level only gates a conditional call's clock while the
// child's regs see their reset asserted.
[[nodiscard]] inline std::optional<bool> reset_input_active_low(const Lnast* callee, std::string_view port) {
  const bool by_name = callee != nullptr && callee->is_verilog_origin() && reset_name_active_low(port);
  if (callee == nullptr) {
    return by_name;
  }
  absl::flat_hash_set<std::string_view>                   regs;
  absl::flat_hash_map<std::string_view, std::string_view> pin;  // reg -> its reset_pin
  absl::flat_hash_map<std::string_view, bool>             neg;  // reg -> its explicit negreset
  for (const auto& n : callee->depth_preorder()) {
    const auto k   = Lnast_nid(n);
    const auto tgt = callee->get_first_child(k);
    if (tgt.is_invalid()) {
      continue;
    }
    if (Lnast_ntype::is_declare(callee->get_type(k))) {
      for (auto c = callee->get_sibling_next(tgt); !c.is_invalid(); c = callee->get_sibling_next(c)) {
        if (Lnast_ntype::is_const(callee->get_type(c)) && callee->get_name(c) == "reg") {
          regs.insert(callee->get_name(tgt));
          break;
        }
      }
      continue;
    }
    const auto key = Lnast_ntype::is_attr_set(callee->get_type(k)) ? callee->get_sibling_next(tgt) : Lnast_nid{};
    if (key.is_invalid()) {
      continue;
    }
    const auto val = callee->get_sibling_next(key);
    const auto v   = val.is_invalid() ? std::string_view{"true"} : callee->get_name(val);
    if (callee->get_name(key) == "reset_pin") {
      pin[callee->get_name(tgt)] = v;
    } else if (callee->get_name(key) == "negreset") {
      neg[callee->get_name(tgt)] = v != "false" && v != "0";
    }
  }
  std::optional<bool> seen;
  for (const auto reg : regs) {
    if (const auto p = pin.find(reg); p != pin.end() && p->second != port) {
      continue;  // on another reset, or none (`reset_pin=false`)
    }
    const auto n  = neg.find(reg);
    const bool nv = n == neg.end() ? by_name : n->second;
    if (seen && *seen != nv) {
      return std::nullopt;
    }
    seen = nv;
  }
  return seen.value_or(by_name);
}

// Whether `callee`'s body reads its input `port` (a default expression of a
// later input counts: it sits in the body prologue). A flattened tuple port's
// leaf (`p.x`) is read by a read of the whole tuple (`p`) too, and a read of a
// field below it (`p.x.y`) reads it.
[[nodiscard]] inline bool body_reads_input(const Lnast& callee, std::string_view port) {
  const auto stmts = callee.get_sibling_next(callee.get_first_child(callee.get_root()));
  if (stmts.is_invalid()) {
    return false;
  }
  const auto on_path = [](std::string_view outer, std::string_view inner) {
    return inner.size() > outer.size() && inner.starts_with(outer) && inner[outer.size()] == '.';
  };
  for (const auto& n : callee.depth_preorder(stmts)) {
    if (!Lnast_ntype::is_ref(callee.get_type(Lnast_nid(n)))) {
      continue;
    }
    const auto name = callee.get_name(Lnast_nid(n));
    if (name == port || on_path(name, port) || on_path(port, name)) {
      return true;
    }
  }
  return false;
}

// Whether `callee`'s body reads input `port` as DATA: body_reads_input minus
// the reads that are a register's `*_pin` attribute value (`clock_pin=ca`
// uses `ca` as a clock, not as data).
[[nodiscard]] inline bool body_reads_input_as_data(const Lnast& callee, std::string_view port) {
  const auto stmts = callee.get_sibling_next(callee.get_first_child(callee.get_root()));
  if (stmts.is_invalid()) {
    return false;
  }
  const auto on_path = [](std::string_view outer, std::string_view inner) {
    return inner.size() > outer.size() && inner.starts_with(outer) && inner[outer.size()] == '.';
  };
  std::function<bool(const Lnast_nid&)> reads = [&](const Lnast_nid& n) -> bool {
    const auto t = callee.get_type(n);
    if (Lnast_ntype::is_ref(t)) {
      const auto name = callee.get_name(n);
      return name == port || on_path(name, port) || on_path(port, name);
    }
    if (Lnast_ntype::is_attr_set(t)) {
      const auto tgt = callee.get_first_child(n);
      const auto key = tgt.is_invalid() ? tgt : callee.get_sibling_next(tgt);
      if (!key.is_invalid() && callee.get_name(key).ends_with("_pin")) {
        return false;  // a clock/reset pin connection
      }
    }
    for (auto c = callee.get_first_child(n); !c.is_invalid(); c = callee.get_sibling_next(c)) {
      if (reads(c)) {
        return true;
      }
    }
    return false;
  };
  return reads(stmts);
}

// A `comb` input `e` that its body never reads is DEAD (user ruling
// 2026-09-28 (30)), judged on the registered (comptime-folded) body, so a read
// only under a condition that folds to false does not count: a call may omit
// it, whatever its name, and nothing is wired
// into it -- the inliner leaves it unbound and a comb Sub
// (`compile.upass.inline=false`) ties its port off. A comb holds no state, so
// its `clk`/`rst` are plain data: an omitted input the comb READS is a missing
// argument, never an auto-wired clock/reset (that is for `mod`/`pipe` only).
[[nodiscard]] inline bool comb_port_is_dead(const Lnast& callee, const Lnast_io_entry& e) {
  return callee.get_lambda_kind() == "comb" && !body_reads_input(callee, e.name);
}

// The default slot (the io store's 2nd child) of `callee`'s input `e` when it
// holds a declared default (`rst:u1 = 0`): any value other than `nil` and the
// `ref`/`...` markers, the comb `__default` sentinel included. Invalid if none.
[[nodiscard]] inline Lnast_nid input_default_slot(const Lnast* callee, const Lnast_io_entry& e) {
  if (callee == nullptr) {
    return Lnast_nid{};
  }
  const auto io = callee->get_first_child(callee->get_root());
  if (io.is_invalid() || !Lnast_ntype::is_io(callee->get_type(io))) {
    return Lnast_nid{};
  }
  const auto ins = callee->get_first_child(io);
  for (auto st = ins.is_invalid() ? ins : callee->get_first_child(ins); !st.is_invalid(); st = callee->get_sibling_next(st)) {
    const auto nm = callee->get_first_child(st);
    if (nm.is_invalid() || callee->get_name(nm) != e.name) {
      continue;
    }
    const auto dv = callee->get_sibling_next(nm);
    if (dv.is_invalid()) {
      return Lnast_nid{};
    }
    const auto t = callee->get_name(dv);
    return !Lnast_ntype::is_const(callee->get_type(dv)) || (t != "nil" && t != "ref" && t != "...") ? dv : Lnast_nid{};
  }
  return Lnast_nid{};
}

// Whether `callee` declares a default for input `e` (see input_default_slot).
[[nodiscard]] inline bool declares_input_default(const Lnast* callee, const Lnast_io_entry& e) {
  return e.has_default || !input_default_slot(callee, e).is_invalid();
}

// The default of a `mod`/`pipe` input: the compile-time constant prp2lnast
// folded into its io slot (`b:u8 = 3`, `c:E = E.A`). nullopt when there is
// none, and for a comb's `__default` sentinel (that value is a body-prologue
// local only the inliner binds).
[[nodiscard]] inline std::optional<Dlop> input_default_const(const Lnast* callee, const Lnast_io_entry& e) {
  const auto dv = input_default_slot(callee, e);
  if (dv.is_invalid() || !Lnast_ntype::is_const(callee->get_type(dv)) || callee->get_name(dv) == "__default") {
    return std::nullopt;
  }
  const auto v = Dlop::from_pyrope(callee->get_name(dv));
  if (!v || v->is_invalid() || v->is_nil()) {
    return std::nullopt;
  }
  return *v;
}

// The nets of `u` (below `from`) a register or memory is clocked by -- a
// `clock_pin` / `__store_clock_pin` value -- and every net copied into one
// (`assign clock = clk_i`, a plain two-child store): copying a clock into a
// clock net is a clock use, not a data read. Ruling 80: in imported Verilog
// this use, not the name, makes an input a clock.
[[nodiscard]] inline absl::flat_hash_set<std::string> clock_nets(const Lnast& u, const Lnast_nid& from) {
  absl::flat_hash_set<std::string>              nets;
  absl::flat_hash_map<std::string, std::string> copy_of;
  if (from.is_invalid()) {
    return nets;
  }
  for (const auto& nid : u.depth_preorder(from)) {
    const auto t   = u.get_type(nid);
    const auto tgt = u.get_first_child(nid);
    const auto key = tgt.is_invalid() ? tgt : u.get_sibling_next(tgt);
    if (Lnast_ntype::is_store(t) && !key.is_invalid() && u.is_last_child(key) && Lnast_ntype::is_ref(u.get_type(tgt))
        && Lnast_ntype::is_ref(u.get_type(key))) {
      copy_of.try_emplace(std::string(u.get_name(tgt)), std::string(u.get_name(key)));
    } else if (Lnast_ntype::is_attr_set(t) && !key.is_invalid()
               && (u.get_name(key) == "clock_pin" || u.get_name(key) == "__store_clock_pin")) {
      const auto val = u.get_sibling_next(key);
      if (!val.is_invalid() && Lnast_ntype::is_ref(u.get_type(val))) {
        nets.insert(std::string(u.get_name(val)));
      }
    }
  }
  std::vector<std::string> work(nets.begin(), nets.end());
  while (!work.empty()) {
    const auto n = std::move(work.back());
    work.pop_back();
    if (const auto it = copy_of.find(n); it != copy_of.end() && nets.insert(it->second).second) {
      work.push_back(it->second);
    }
  }
  return nets;
}

}  // namespace upass::io_port
