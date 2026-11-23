//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_aeg.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "aeg_map.hpp"
#include "aeg_rules.hpp"
#include "diag.hpp"
#include "fold_engine.hpp"
#include "iassert.hpp"
#include "lnast.hpp"
#include "literal_kind.hpp"
#include "lnast_ntype.hpp"
#include "rewriter.hpp"
#include "runtime/analyses.hpp"
#include "runtime/match.hpp"

namespace upass::aeg {

namespace {

namespace ag = livehd::aegraph;
namespace rt = livehd::aegraph::rt;
using N      = Lnast_ntype;
using Ltype  = Lnast_ntype::Lnast_ntype_int;
using ag::Class_id;
using ag::kNoClass;

constexpr uint32_t kNone = UINT32_MAX;

// ---------------------------------------------------------------------------
// Dense per-unit name ids
// ---------------------------------------------------------------------------

// Lnast name ids are signed dense indices into the thread-shared name pool
// (negative = a `%` temp). A thread_local table indexed by |id| maps a name to
// this unit's dense local id; the epoch makes the per-unit reset O(1), since the
// pool (and the table) outlive every unit. One Name_table is alive per thread
// at a time (one unit at a time).
thread_local std::vector<std::pair<uint32_t, uint32_t>> t_slot;  // (epoch, local)
thread_local uint32_t                                   t_epoch = 0;

class Name_table {
public:
  Name_table() {
    if (++t_epoch == 0) {
      std::fill(t_slot.begin(), t_slot.end(), std::pair<uint32_t, uint32_t>{0, 0});
      t_epoch = 1;
    }
  }
  // The local id `id` maps to, if this unit has seen it.
  [[nodiscard]] bool seen(int32_t id, uint32_t& local) {
    const auto& s = slot(id);
    local         = s.second;
    return s.first == t_epoch;
  }
  // A fresh local for `id`.
  uint32_t mint(int32_t id) {
    slot(id) = {t_epoch, static_cast<uint32_t>(ids_.size())};
    ids_.push_back(id);
    return static_cast<uint32_t>(ids_.size() - 1);
  }
  // `id` spells the name `local` already stands for.
  void bind(int32_t id, uint32_t local) { slot(id) = {t_epoch, local}; }
  [[nodiscard]] int32_t id(uint32_t local) const { return ids_[local]; }
  [[nodiscard]] size_t  size() const { return ids_.size(); }

private:
  static std::pair<uint32_t, uint32_t>& slot(int32_t id) {
    const auto idx = static_cast<size_t>(id < 0 ? -static_cast<int64_t>(id) : id);
    if (idx >= t_slot.size()) {
      t_slot.resize(std::max(idx + 1, t_slot.size() * 2), {0, 0});
    }
    return t_slot[idx];
  }

  std::vector<int32_t> ids_;
};

// ---------------------------------------------------------------------------
// Statement classes
// ---------------------------------------------------------------------------

// Statements the walk never looks into: their payload has scoping or execution
// rules of its own (exclusive arms, a lifted loop body, a runtime cycle loop).
[[nodiscard]] bool is_barrier_compound(Ltype t) {
  return N::is_unique_if(t) || N::is_rolled_for(t) || N::is_tick(t) || N::is_for(t) || N::is_while(t) || N::is_func_def(t);
}

// Leaf statements whose lowering the step understands well enough to treat as
// opaque: they write their child 0 (or nothing) and nothing else in scope.
[[nodiscard]] bool is_known_leaf(Ltype t) {
  return is_mapped_op(t) || N::is_store(t) || N::is_declare(t) || N::is_delay_assign(t) || N::is_attr_set(t) || N::is_attr_get(t)
         || N::is_tuple_add(t) || N::is_tuple_get(t) || N::is_tuple_concat(t) || N::is_type_spec(t) || N::is_cassert(t)
         || N::is_timecheck(t) || N::is_range(t) || N::is_func_call(t) || N::is_step(t) || N::is_red_and(t) || N::is_red_xor(t)
         || N::is_popcount(t) || N::is_mod(t) || N::is_set_mask(t) || N::is_concat(t);
}

// Opaque statements that may write names other than child 0.
[[nodiscard]] bool writes_any_ref(Ltype t) {
  return N::is_func_call(t) || N::is_tuple_add(t) || N::is_tuple_get(t) || N::is_tuple_concat(t);
}

// Results upass.tolg keys a name-based side channel on (a memory read port, a
// call's outputs, a deferred attribute, a delayed read, a partial-write chain).
[[nodiscard]] bool special_dst(Ltype t) {
  return N::is_tuple_get(t) || N::is_func_call(t) || N::is_attr_get(t) || N::is_delay_assign(t) || N::is_set_mask(t);
}

[[nodiscard]] Kind const_kind(std::string_view text) {
  switch (upass::literal_kind(text)) {
    case upass::Kind::integer: return Kind::integer;
    case upass::Kind::boolean: return Kind::boolean;
    default                  : return Kind::unknown;  // the scalar e-graph has no string or nil sort
  }
}

// A const leaf's text that may name a variable (a rolled_for carry output or
// activation port is spelled as const text).
[[nodiscard]] bool name_like(std::string_view text) {
  if (text.empty() || text == "true" || text == "false" || text == "nil") {
    return false;
  }
  const auto c = static_cast<unsigned char>(text.front());
  return c == '%' || c == '_' || c == '$' || c == '`' || std::isalpha(c) != 0;
}

// The numeric value of a const leaf (null for a string, nil or malformed text).
[[nodiscard]] const Dlop* numeric_const(const Lnast& ln, std::string_view text) {
  if (text.empty() || text == "nil") {
    return nullptr;
  }
  try {
    const Dlop& v = ln.get_const_value(text);
    return v.is_numeric() ? &v : nullptr;
  } catch (...) {
    return nullptr;
  }
}

// The const text of `v` read as `k` ("" when it cannot be: a value with
// unknowns, a non-0/1 value of a bool, an unknown kind).
[[nodiscard]] std::string const_text(const Dlop& v, Kind k) {
  if (!v.is_numeric() || v.has_unknowns() || k == Kind::unknown) {
    return {};
  }
  if (k == Kind::boolean) {
    if (v.is_bool()) {
      return v.to_pyrope();
    }
    if (v.is_just_i64() && (v.to_just_i64() == 0 || v.to_just_i64() == 1)) {
      return v.to_just_i64() == 1 ? "true" : "false";
    }
    return {};
  }
  if (v.is_bool()) {
    return v.is_known_true() ? "1" : "0";
  }
  return v.to_pyrope();
}

// Statements whose lowering reads an operand's structural width or sign
// (W-seeds): their cone must keep its exact spelling.
[[nodiscard]] bool width_seed(const Lnast& ln, const Lnast_nid& nid, Ltype t) {
  if (N::is_red_and(t) || N::is_red_xor(t) || N::is_popcount(t) || N::is_mod(t) || N::is_set_mask(t) || N::is_attr_get(t)) {
    return true;
  }
  const auto dst = nid.first_child();
  if (N::is_get_mask(t)) {
    const auto val = dst.is_invalid() ? dst : dst.next_sibling();
    const auto lo  = val.is_invalid() ? val : val.next_sibling();
    const auto hi  = lo.is_invalid() ? lo : lo.next_sibling();
    if (lo.is_invalid() || !N::is_const(ln.get_type(lo))) {
      return true;
    }
    return !hi.is_invalid() && (!N::is_const(ln.get_type(hi)) || ln.get_name(hi) == "nil");
  }
  if (N::is_sext(t)) {
    const auto a   = dst.is_invalid() ? dst : dst.next_sibling();
    const auto pos = a.is_invalid() ? a : a.next_sibling();
    return pos.is_invalid() || !N::is_const(ln.get_type(pos));
  }
  if (N::is_concat(t)) {
    size_t i = 0;
    for (auto c = dst.is_invalid() ? dst : dst.next_sibling(); !c.is_invalid(); c = c.next_sibling(), ++i) {
      if ((i % 2) == 1 && (!N::is_const(ln.get_type(c)) || const_kind(ln.get_name(c)) != Kind::integer)) {
        return true;
      }
    }
    return false;
  }
  return !is_known_leaf(t) && !is_barrier_compound(t) && !N::is_stmts(t) && !N::is_if(t);
}

// ---------------------------------------------------------------------------
// Prescan
// ---------------------------------------------------------------------------

struct Name_info {
  uint32_t reads     = 0;
  uint32_t live      = 0;      // reads by statements the DCE keeps
  uint32_t defs      = 0;
  uint32_t def_head  = kNone;  // the definitions, linked through Stmt_info::next_def
  uint32_t reader    = kNone;  // the first statement the DCE keeps that reads it
  uint32_t agg_stamp = kNone;
  uint32_t base      = kNone;  // a stream-SSA version's base name (x___ssa_N -> x): the same declared type
  uint32_t last_ref  = 0;      // the last statement that mentions the name (a read or a write)
  Kind     kind      = Kind::unknown;
  bool     kind_set  = false;
  bool     tmp       = false;
  bool     state     = false;  // named by a declare (mut/reg/wire), tmp-prefixed state included
  bool     special   = false;
  bool     clock     = false;  // a Clock/Reset port or declaration, or a value derived from a clock net
  bool     clock_net = false;  // a clock net (a Clock port, a clock_pin or latch enable): taints its readers
  bool     latch     = false;  // declared `latch` (its `enable` is a clock, a flop's is data)
  bool     io_input  = false;  // a scalar input port
  bool     input     = false;  // any input port (never written in the body)
  bool     output    = false;  // an output port
  bool     dflt      = false;  // a defaulted input's prologue local
  bool     unbounded = false;  // an output with no declared width: tolg gives its port the driver's width and sign
  bool     out_cycle = false;  // an output declared at a non-zero cycle (a checked unit)
  bool     reg       = false;  // declared `reg` (a stage too)
  bool     time_src  = false;  // may sit at a non-zero cycle on its own (see time_taint)
  bool     timed     = false;  // time_src, or computed from one
  bool     pin_seen  = false;
  uint32_t twin_group = kNone;  // the twin group of its latest definition (see twin_key)
  uint32_t io_pos     = 0;      // an input port's declaration position (tolg's input pin order)
};

struct Stmt_info {
  uint32_t end        = 0;  // the first statement index after this statement's subtree
  uint32_t dst        = kNone;
  uint32_t next_def   = kNone;
  uint32_t read_begin = 0;
  uint32_t read_end   = 0;
  uint32_t region     = 0;
  uint32_t parent_if  = kNone;  // the innermost if / unique_if this statement is in
  uint32_t agg_begin  = 0;      // an outermost opaque compound: its names in agg_def_
  uint32_t agg_end    = 0;
  Ltype    ltype      = N::Lnast_ntype_invalid;
  bool     w_seed     = false;
  bool     pinned     = false;
  bool     droppable  = false;  // on the DCE's spine and writes nothing but its temp child 0
  bool     dead       = false;  // the DCE right after the step sweeps it
  bool     time_op    = false;  // reads a timed name (or is an if arm under a timed condition)
  bool     arm_seen   = false;  // time_taint visited it as part of a timed if's arms
  bool     if_up      = false;  // time_taint marked this if (and its enclosing ifs) a W-seed
  bool     no_slice   = false;  // ends a cycle-check chain: never respelled to a get_mask or sext
  uint32_t twin       = kNone;  // twin group: statements that will hash-cons to one class
};

class Prescan {
public:
  // `timed`: the unit's cycle checks run on the post-step body (see
  // time_taint); `pipe`: its plain regs are feedforward stages.
  Prescan(const Lnast& ln, const Lnast_tree_io& io, bool timed = false, bool pipe = false) : ln_(ln), timed_(timed), pipe_(pipe) {
    uint32_t io_pos = 0;
    for (const auto& e : io.inputs) {
      auto& n    = info(name_local(e.name));
      n.io_pos    = io_pos++;
      n.io_input  = e.array_size == 0 && e.sig == Io_sig::none;
      n.input     = true;
      n.clock     = e.sig != Io_sig::none;
      n.clock_net = e.sig == Io_sig::clock;
      set_kind_io(n, e.kind);
      if (e.has_default) {
        info(name_local(Lnast_io_entry::default_value_name(e.name))).dflt = true;
      }
    }
    for (const auto& e : io.outputs) {
      auto& n = info(name_local(e.name));
      set_kind_io(n, e.kind);
      // tolg's output wiring test (an integer or untyped port, bits == 0).
      n.unbounded = e.kind != Io_kind::boolean && e.bits == 0;
      n.output    = true;
      n.out_cycle = timed_ && (e.stages_min > 0 || e.stages_max > 0);
    }
    // A generic port's deferred width bound names a body name upass.tolg reads
    // outside the tree: keep it read, and keep its readers reading it.
    for (const auto* ports : {&io.inputs, &io.outputs}) {
      for (const auto& e : *ports) {
        for (const auto* bound : {&e.bound_max_text, &e.bound_min_text}) {
          if (name_like(*bound)) {
            auto& n = info(name_local(*bound));
            ++n.reads;
            n.special = true;
          }
        }
      }
    }
    for (auto c = ln.get_root().first_child(); !c.is_invalid(); c = c.next_sibling()) {
      if (N::is_stmts(ln.get_type(c))) {
        scan_block(c, false, true);
      }
    }
    isolate_attributed();
    clock_nets();
    dead_temps();
    unbounded_outputs();
    time_taint();
    pin_closure();
    for (const auto s : shape_) {
      stmt_[s].pinned = true;  // after the closure: a chain link pins itself, not its cone
    }
    clock_taint();
    finalize_twins();
  }

  [[nodiscard]] Name_info&       info(uint32_t n) { return name_[n]; }
  [[nodiscard]] const Name_info& info(uint32_t n) const { return name_[n]; }
  [[nodiscard]] const Stmt_info& stmt(uint32_t s) const { return stmt_[s]; }
  [[nodiscard]] size_t           num_stmts() const { return stmt_.size(); }
  [[nodiscard]] size_t           num_names() const { return name_.size(); }
  [[nodiscard]] int32_t          name_id(uint32_t n) const { return names_.id(n); }
  [[nodiscard]] uint32_t         prologue_last() const { return prologue_last_; }
  [[nodiscard]] uint64_t         edges() const { return edges_; }
  [[nodiscard]] bool             worth_walking() const { return mapped_ != 0 || tmp_stores_ != 0; }
  [[nodiscard]] std::span<const uint32_t> reads_of(uint32_t s) const {
    return {reads_.data() + stmt_[s].read_begin, stmt_[s].read_end - stmt_[s].read_begin};
  }
  // Readers to credit when statement s imports (precount_twins): its group's
  // total for the first member, its own live readers otherwise.
  [[nodiscard]] uint32_t twin_readers(uint32_t s) const {
    const auto g   = stmt_[s].twin;
    const auto own = stmt_[s].dst == kNone ? 0 : name_[stmt_[s].dst].live;
    return g != kNone && g < twin_first_.size() && twin_first_[g] == s ? twin_total_[g] : own;
  }
  // A later member of a twin group: its readers were credited to the first,
  // unless its import makes a class of its own.
  [[nodiscard]] bool later_twin(uint32_t s) const {
    const auto g = stmt_[s].twin;
    return g != kNone && g < twin_first_.size() && twin_first_[g] != kNone && twin_first_[g] != s;
  }

  uint32_t local(int32_t id) {
    uint32_t n = 0;
    if (names_.seen(id, n)) {
      return n;
    }
    // tolg keys its name model on canon_io_name: a backticked spelling without
    // whitespace is the bare name, one binding under both spellings.
    const auto text = ln_.resolve_name(id);
    if (text.size() >= 2 && text.front() == '`' && text.back() == '`'
        && std::none_of(text.begin() + 1, text.end() - 1, [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; })) {
      n = local(ln_.intern_name(text.substr(1, text.size() - 2)));
      names_.bind(id, n);
      return n;
    }
    n = names_.mint(id);
    name_.emplace_back();
    name_.back().tmp = Lnast::is_tmp(id);
    if (const auto pos = text.find("___ssa_"); pos != std::string_view::npos && pos > 0 && !name_.back().tmp) {
      const auto b  = local(ln_.intern_name(text.substr(0, pos)));
      name_[n].base = b;
    }
    return n;
  }

  // A name's value kind: its port, declaration or definitions, else its SSA base's.
  [[nodiscard]] Kind kind(uint32_t n) const {
    const auto& i = name_[n];
    if (i.kind_set) {
      return i.kind;
    }
    if (i.base != kNone && name_[i.base].kind_set) {
      return name_[i.base].kind;
    }
    return Kind::unknown;
  }
  uint32_t name_local(std::string_view text) { return local(ln_.intern_name(text)); }

  // Readers are counted when a statement is imported, but a structural twin
  // can come AFTER the first reader of its class was built: t1 = a | b read by
  // t2 = t1 | c, and a later t1' = a | b. `flatten` asking "does t1 have one
  // reader?" while importing t2 would absorb t1 and leave t1' as a second a | b
  // (the LGraph engine's precount_twins). So statements that will hash-cons to
  // one class -- same op and region, same operand groups -- share one reader
  // total, credited to the first. The key ignores operand order and treats a
  // multi-def name by its id, so it can only group too much: that blocks a
  // flatten, never allows a wrong one.
  [[nodiscard]] uint32_t twin_group_of(const Lnast_nid& dst, Ltype t) {
    absl::InlinedVector<uint64_t, 4> ids;
    for (auto c = dst.next_sibling(); !c.is_invalid(); c = c.next_sibling()) {
      const auto ct = ln_.get_type(c);
      if (N::is_ref(ct)) {
        const auto x = local(ln_.get_name_id(c));
        const auto g = name_[x].twin_group;
        ids.push_back(g != kNone && name_[x].defs == 1 ? (uint64_t{1} << 40) | g : (uint64_t{1} << 62) | x);
      } else if (N::is_const(ct)) {
        ids.push_back((uint64_t{1} << 61) | (std::hash<std::string_view>{}(ln_.get_name(c)) & ((uint64_t{1} << 60) - 1)));
      } else {
        return kNone;
      }
    }
    std::ranges::sort(ids);
    uint64_t key = mix64(0x7477696eULL ^ static_cast<uint64_t>(t), region_);
    for (const auto id : ids) {
      key = mix64(key, id);
    }
    const auto [it, fresh] = twin_ids_.try_emplace(key, static_cast<uint32_t>(twin_ids_.size()));
    return it->second;
  }
  static uint64_t mix64(uint64_t a, uint64_t b) {
    uint64_t x  = a ^ (b + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2));
    x          ^= x >> 31;
    x          *= 0xBF58476D1CE4E5B9ULL;
    return x ^ (x >> 29);
  }
  // After the DCE marks and the cycle-check freezes: the first imported member
  // of each group carries the group's live readers.
  void finalize_twins() {
    twin_first_.assign(twin_ids_.size(), kNone);
    twin_total_.assign(twin_ids_.size(), 0);
    for (uint32_t s = 0; s < stmt_.size(); ++s) {
      const auto g = stmt_[s].twin;
      if (g == kNone || stmt_[s].dead || time_frozen(s) || stmt_[s].dst == kNone) {
        continue;
      }
      if (twin_first_[g] == kNone) {
        twin_first_[g] = s;
      }
      twin_total_[g] += name_[stmt_[s].dst].live;
    }
  }

  // A declaration attribute (`const n::[synth.adder="prefix"] = a + b`) is an
  // attr_set next to n's declaration, and tolg applies it to n's whole fan-in
  // cone (apply_synth_targets walks drivers until another anchor), rooted at n
  // and at every stream-SSA version n___ssa_k. Each attributed name's cone --
  // every definition of every name it reads, and the conditions of the ifs
  // around those definitions -- gets a color of its own, so no cell of it is
  // hash-consed with an unannotated twin outside (which would carry the
  // attribute onto the twin's logic, or drop it). The first cone to reach a
  // name owns it, as tolg stops at another anchor. One visit per name and
  // statement.
  void isolate_attributed() {
    if (attributed_.empty()) {
      return;
    }
    absl::flat_hash_map<uint32_t, uint32_t> color_of;  // attributed base -> its cone's color
    for (const auto n : attributed_) {
      if (color_of.try_emplace(n, 0).second) {
        color_of[n] = ++region_serial_;
      }
    }
    std::vector<std::pair<uint32_t, uint32_t>> stack;  // (name, color)
    for (uint32_t m = 0; m < name_.size(); ++m) {
      auto it = color_of.find(m);
      if (it == color_of.end() && name_[m].base != kNone) {
        it = color_of.find(name_[m].base);
      }
      if (it != color_of.end()) {
        stack.emplace_back(m, it->second);
      }
    }
    std::vector<uint8_t> name_done(name_.size(), 0);
    std::vector<uint8_t> stmt_done(stmt_.size(), 0);
    const auto           push_reads = [&](uint32_t s, uint32_t color) {
      for (const auto r : reads_of(s)) {
        stack.emplace_back(r, color);
      }
    };
    while (!stack.empty()) {
      const auto [m, color] = stack.back();
      stack.pop_back();
      if (m >= name_done.size() || std::exchange(name_done[m], 1) != 0 || name_[m].input) {
        continue;
      }
      for (auto d = name_[m].def_head; d != kNone; d = stmt_[d].next_def) {
        if (std::exchange(stmt_done[d], 1) != 0) {
          continue;
        }
        stmt_[d].region = color;
        push_reads(d, color);
        for (auto p = stmt_[d].parent_if; p != kNone && std::exchange(stmt_done[p], 1) == 0; p = stmt_[p].parent_if) {
          push_reads(p, color);  // an arm's write is muxed by the if's condition
        }
      }
    }
  }

  // The definition `s` of a single-definition, single-read temp whose one
  // reader is a same-op statement in the same region: that reader absorbs it
  // (flatten), so the inner link must not flatten on its own.
  [[nodiscard]] bool chain_inner(uint32_t s) const {
    const auto& st = stmt_[s];
    if (st.dst == kNone) {
      return false;
    }
    const auto& n = name_[st.dst];
    if (!n.tmp || n.defs != 1 || n.live != 1 || n.reader == kNone) {
      return false;
    }
    // A pinned reader still absorbs: it keeps its spelling, but each link of a
    // pinned chain flattening on its own rebuilt the whole chain so far, O(k^2)
    // operands that also drained the unit's rule budget. A reader the walk
    // never imports (cycle-checked) cannot absorb, so its link flattens itself.
    const auto op = chain_op(st.ltype);
    const auto& r  = stmt_[n.reader];
    return op != Ntype_op::Invalid && chain_op(r.ltype) == op && r.region == st.region && !time_frozen(n.reader);
  }

  // A statement the walk keeps exactly as written and never looks through: it
  // computes or consumes a value whose cycle the checks derive (time_taint).
  [[nodiscard]] bool time_frozen(uint32_t s) const {
    const auto& st = stmt_[s];
    return st.time_op || (st.dst != kNone && name_[st.dst].timed);
  }

private:
  static void set_kind_io(Name_info& n, Io_kind k) {
    if (k == Io_kind::integer || k == Io_kind::boolean) {
      n.kind     = k == Io_kind::integer ? Kind::integer : Kind::boolean;
      n.kind_set = true;
    }
  }
  static void set_kind_def(Name_info& n, Kind k) {
    n.kind     = n.kind_set ? meet(n.kind, k) : k;
    n.kind_set = true;
  }

  void scan_block(const Lnast_nid& block, bool opaque, bool top) {
    const auto saved_region = region_;
    for (auto c = block.first_child(); !c.is_invalid(); c = c.next_sibling()) {
      const auto s = scan_stmt(c, opaque);
      if (top && N::is_store(stmt_[s].ltype) && stmt_[s].dst != kNone && name_[stmt_[s].dst].dflt) {
        prologue_last_ = s;
      }
    }
    region_ = saved_region;  // a region marker governs the rest of its own block only
  }

  uint32_t scan_stmt(const Lnast_nid& nid, bool opaque) {
    const auto s = static_cast<uint32_t>(stmt_.size());
    const auto t = ln_.get_type(nid);
    stmt_.emplace_back();
    stmt_[s].ltype     = t;
    stmt_[s].region    = region_;
    stmt_[s].parent_if = cur_if_;
    const size_t base  = scratch_.size();

    if (N::is_stmts(t)) {
      scan_block(nid, opaque, false);
    } else if (N::is_if(t) || N::is_unique_if(t)) {
      const bool inner    = opaque || N::is_unique_if(t);
      const auto saved_if = cur_if_;
      cur_if_             = s;
      for (auto c = nid.first_child(); !c.is_invalid(); c = c.next_sibling()) {
        if (N::is_stmts(ln_.get_type(c))) {
          scan_block(c, inner, false);
        } else {
          scan_node(c, t, c.is_first_child(), s, inner);
        }
      }
      cur_if_ = saved_if;
    } else if (is_barrier_compound(t) || !is_known_leaf(t)) {
      // The outermost opaque compound collects every name it mentions (a loop
      // carry is spelled as const text) and stands as their definition, so a
      // pin closure that reaches it also reaches whatever feeds it.
      const bool outer = agg_ == kNone;
      if (outer) {
        agg_ = s;
        agg_reads_.clear();
      }
      for (auto c = nid.first_child(); !c.is_invalid(); c = c.next_sibling()) {
        if (N::is_stmts(ln_.get_type(c))) {
          scan_block(c, true, false);
        } else {
          scan_node(c, t, c.is_first_child(), s, true);
        }
      }
      stmt_[s].w_seed = !is_barrier_compound(t);
      if (outer) {
        scratch_.resize(base);
        scratch_.insert(scratch_.end(), agg_reads_.begin(), agg_reads_.end());
        stmt_[s].agg_begin = static_cast<uint32_t>(agg_def_.size());
        for (const auto n : agg_reads_) {
          link_def(n, s);
        }
        stmt_[s].agg_end = static_cast<uint32_t>(agg_def_.size());
        agg_             = kNone;
      }
    } else {
      scan_leaf(nid, s, t, opaque);
    }

    stmt_[s].end        = static_cast<uint32_t>(stmt_.size());
    stmt_[s].read_begin = static_cast<uint32_t>(reads_.size());
    reads_.insert(reads_.end(), scratch_.begin() + static_cast<std::ptrdiff_t>(base), scratch_.end());
    stmt_[s].read_end = static_cast<uint32_t>(reads_.size());
    scratch_.resize(base);
    return s;
  }

  // An aggregate defines many names: kept on a side list, indexed per name by pin_closure.
  void link_def(uint32_t n, uint32_t s) {
    ++name_[n].defs;
    agg_def_.push_back({n, s});
  }

  void scan_leaf(const Lnast_nid& nid, uint32_t s, Ltype t, bool opaque) {
    stmt_[s].w_seed = width_seed(ln_, nid, t);
    if (timed_ && N::is_timecheck(t)) {
      // An `@[N]` assertion: its cone keeps the dependencies the check derives
      // a cycle from, and the value it names is where tolg's chain to a reg
      // starts (time_taint).
      stmt_[s].w_seed = true;
      if (const auto tgt = nid.first_child(); !tgt.is_invalid() && N::is_ref(ln_.get_type(tgt))) {
        check_targets_.push_back(local(ln_.get_name_id(tgt)));
      }
    }
    if (N::is_attr_set(t)) {
      const auto tgt = nid.first_child();
      const auto key = tgt.is_invalid() ? tgt : tgt.next_sibling();
      const auto val = key.is_invalid() ? key : key.next_sibling();
      if (!key.is_invalid() && N::is_const(ln_.get_type(key))) {
        const auto k = ln_.get_name(key);
        if (k.starts_with("__region") || k == "__synth_scope" || k.starts_with("synth.")) {
          region_ = ++region_serial_;  // a new color for the rest of the block (hash-cons attrs)
        }
        if (k.starts_with("synth.") && !tgt.is_invalid() && N::is_ref(ln_.get_type(tgt))) {
          attributed_.push_back(local(ln_.get_name_id(tgt)));
        }
        if ((k == "clock_pin" || k == "enable") && !val.is_invalid() && N::is_ref(ln_.get_type(val)) && !tgt.is_invalid()
            && N::is_ref(ln_.get_type(tgt))) {
          clock_attrs_.push_back({s, local(ln_.get_name_id(tgt)), local(ln_.get_name_id(val)), k == "clock_pin"});
        }
      }
    }
    absl::InlinedVector<Kind, 4> operand_kinds;
    size_t                       children = 0;
    bool                         reads_special = false;
    for (auto c = nid.first_child(); !c.is_invalid(); c = c.next_sibling(), ++children) {
      const auto ct = ln_.get_type(c);
      if (children > 0 && (N::is_ref(ct) || N::is_const(ct))) {
        if (N::is_ref(ct)) {
          const auto n = local(ln_.get_name_id(c));
          operand_kinds.push_back(kind(n));
          reads_special = reads_special || name_[n].special;
        } else {
          operand_kinds.push_back(const_kind(ln_.get_name(c)));
        }
      }
      scan_node(c, t, children == 0, s, opaque);
    }
    if (is_mapped_op(t)) {
      ++mapped_;
      edges_ += children;
    }
    const auto dst = nid.first_child();
    if (dst.is_invalid() || !N::is_ref(ln_.get_type(dst)) || !N::defines_first_child(t)) {
      return;
    }
    const auto n        = local(ln_.get_name_id(dst));
    const auto twin     = is_mapped_op(t) && name_[n].tmp ? twin_group_of(dst, t) : kNone;
    auto&      ni       = name_[n];
    stmt_[s].dst        = n;
    stmt_[s].next_def   = ni.def_head;
    ni.def_head         = s;
    ++ni.defs;
    stmt_[s].twin = twin;
    ni.twin_group = twin;
    if (special_dst(t) || (N::is_get_mask(t) && reads_special)) {
      ni.special = true;  // a slice of a memory read or partial-write chain extends tolg's chain by name
    }
    // A call result (an instance output's declared latency), a memory read
    // port, a delayed read: a value whose cycle need not be its operands'.
    if (timed_ && (N::is_func_call(t) || N::is_tuple_get(t) || N::is_delay_assign(t))) {
      ni.time_src = true;
    }
    if (N::is_declare(t)) {
      ni.state = true;
      if (const auto ty = dst.next_sibling(); !ty.is_invalid()) {
        const auto tt = ln_.get_type(ty);
        if (const auto q = ty.next_sibling(); !q.is_invalid() && N::is_const(ln_.get_type(q))) {
          const auto mode = ln_.get_name(q);
          ni.latch        = mode.find("latch") != std::string_view::npos;
          ni.reg          = ni.reg || mode == "reg" || mode.starts_with("reg ");
          // A `stage[N]` declaration lands N cycles after its input; a pipe's
          // plain reg is a feedforward stage.
          for (auto c = q.next_sibling(); !c.is_invalid() && timed_; c = c.next_sibling()) {
            ni.time_src = ni.time_src || N::is_stages(ln_.get_type(c));
          }
          ni.time_src = ni.time_src || (timed_ && pipe_ && ni.reg);
        }
        if (N::is_prim_type_clock_or_reset(tt)) {
          ni.clock     = true;
          ni.clock_net = N::is_prim_type_clock(tt);
          ni.kind      = Kind::boolean;
          ni.kind_set  = true;
        } else if (N::is_prim_type_bool(tt) || N::is_prim_type_int(tt)) {
          ni.kind     = N::is_prim_type_bool(tt) ? Kind::boolean : Kind::integer;
          ni.kind_set = true;
        }
      }
      return;
    }
    if (!ni.tmp) {
      return;  // a user name's kind comes from its port or declaration only
    }
    // The DCE drops any unread temp definition on its spine; the ones the step
    // may skip are those whose only effect is child 0 (a call or a tuple
    // writes more, a set_mask feeds a partial-write chain by name).
    stmt_[s].droppable = !opaque
                         && (is_mapped_op(t) || (N::is_store(t) && children == 2) || N::is_red_and(t) || N::is_red_xor(t)
                             || N::is_popcount(t) || N::is_mod(t) || N::is_concat(t));
    if (is_mapped_op(t)) {
      set_kind_def(ni, result_kind(t, operand_kinds));
    } else if (N::is_store(t) && children == 2) {
      ++tmp_stores_;
      set_kind_def(ni, operand_kinds.empty() ? Kind::unknown : operand_kinds.front());
    } else {
      set_kind_def(ni, Kind::unknown);
    }
  }

  // One node under statement `s`: count its reads (the DCE's rule; inside an
  // opaque compound also a const leaf naming a temp), feed the aggregate, and
  // scan a nested statement block as statements of its own.
  void scan_node(const Lnast_nid& c, Ltype parent, bool first, uint32_t s, bool opaque) {
    const auto ct = ln_.get_type(c);
    if (N::is_ref(ct)) {
      const auto n = local(ln_.get_name_id(c));
      mention(n, s);
      // Inside an opaque compound every mention counts (a loop carry input is
      // child 0 of a store, yet the loop reads it).
      if (opaque || !(first && (N::defines_first_child(parent) || N::is_type_spec(parent)))) {
        read(n);
      }
      if (agg_ != kNone) {
        agg_add(n);
      }
      return;
    }
    if (N::is_const(ct)) {
      if (opaque || agg_ != kNone) {
        const auto text = ln_.get_name(c);
        if (name_like(text)) {
          const auto n = name_local(text);
          mention(n, s);
          if (opaque && name_[n].tmp) {
            read(n);
          }
          if (agg_ != kNone) {
            agg_add(n);
          }
        }
      }
      return;
    }
    if (N::is_stmts(ct)) {
      scan_block(c, true, false);
      return;
    }
    for (auto g = c.first_child(); !g.is_invalid(); g = g.next_sibling()) {
      scan_node(g, ct, g.is_first_child(), s, opaque);
    }
  }

  // Statements are numbered in preorder, so the last mention is the largest index.
  void mention(uint32_t n, uint32_t s) { name_[n].last_ref = std::max(name_[n].last_ref, s); }

  void read(uint32_t n) {
    ++name_[n].reads;
    scratch_.push_back(n);
  }

  void agg_add(uint32_t n) {
    if (name_[n].agg_stamp != agg_) {
      name_[n].agg_stamp = agg_;
      agg_reads_.push_back(n);
    }
  }

  // A state element's clock (a clock_pin, a latch's enable) is a clock net, and
  // the cone that computes it keeps its spelling: tolg and the writers
  // recognize clock gates by shape (control_root, clock_op_of), and a Verilog
  // clock is a clock only by this use.
  void clock_nets() {
    for (const auto& a : clock_attrs_) {
      if (a.clock_pin || name_[a.target].latch) {
        name_[a.value].clock_net = true;
        name_[a.value].clock     = true;
        stmt_[a.stmt].w_seed     = true;
      }
    }
  }

  // The definitions the runner's DCE sweeps right after the step: a temp no
  // kept statement reads, transitively (its worklist, over the temps). The step
  // never imports them, so one never holds a value a live reader is renamed
  // to, never seeds a pin and never counts as a reader. Such leftovers differ
  // between the bodies a warm and a cold compile walk (a restored comb keeps
  // what its own DCE only marked), and the step's result must not. Each
  // statement and each read is visited once.
  void dead_temps() {
    std::vector<uint32_t> work;
    for (uint32_t n = 0; n < name_.size(); ++n) {
      name_[n].live = name_[n].reads;
      if (name_[n].live == 0 && name_[n].tmp) {
        work.push_back(n);
      }
    }
    while (!work.empty()) {
      const auto n = work.back();
      work.pop_back();
      if (name_[n].state) {
        continue;
      }
      for (auto d = name_[n].def_head; d != kNone; d = stmt_[d].next_def) {
        if (stmt_[d].dead || !stmt_[d].droppable) {
          continue;
        }
        stmt_[d].dead = true;
        for (const auto r : reads_of(d)) {
          if (name_[r].live > 0 && --name_[r].live == 0 && name_[r].tmp) {
            work.push_back(r);
          }
        }
      }
    }
    // The first kept reader (chain_inner's one reader), in preorder.
    for (uint32_t s = 0; s < stmt_.size(); ++s) {
      if (stmt_[s].dead) {
        continue;
      }
      for (const auto r : reads_of(s)) {
        if (name_[r].reader == kNone) {
          name_[r].reader = s;
        }
      }
    }
  }

  // An output with no declared width takes its driver's tolg width and sign as
  // its port (upass_tolg's output wiring), so its driver's spelling is part of
  // the interface: re-spelling the cone changes the emitted port, and forwards
  // it to a spelling whose tolg width may not hold the value. Every write of
  // the port (SSA versions and arm writes included) is a W-seed, so the pin
  // closure keeps the whole cone as written.
  void unbounded_outputs() {
    for (auto& st : stmt_) {
      if (st.dst == kNone || N::is_declare(st.ltype)) {
        continue;
      }
      const auto& n = name_[st.dst];
      if (n.unbounded || (n.base != kNone && name_[n.base].unbounded)) {
        st.w_seed = true;
      }
    }
  }

  // Cycle checks (a mod or pipe whose timing is on): upass/timecheck after the
  // queue, tolg's `@[N]` lowering and its LG checker all judge the post-step
  // body, and they derive a cycle from a value's dependencies, not from the
  // value: `(r - r) + x` mixes r's cycle into x's although it equals x, and a
  // reg an output or an `@[N]` reaches through a chain of copies and slices
  // (get_mask, sext) is a feedforward stage (tolg's decl_cycle_regs_), one
  // reached any other way cycle-0 state. So:
  //  * on such a chain from an output value that reads a reg, the copies and
  //    slices keep their spelling (shape_: pinned, no closure) and the cell
  //    that ends it is never respelled to a slice (no_slice); a reg at its end
  //    is a stage;
  //  * a value that may sit at a non-zero cycle on its own (a stage, a pipe's
  //    reg, a call result, a memory or delayed read, such a reg) and,
  //    transitively, every name computed from one (an if's arm writes under a
  //    timed condition included) is timed: the statements that compute or read
  //    one are W-seeds and are never looked through, and so is every enclosing
  //    if of a timed write (its condition times the merge);
  //  * an `@[N]` (scan_leaf) and a write of an output declared at a non-zero
  //    cycle are W-seeds: a cone folded to a constant has no cycle to check.
  // Each statement, read and name is visited a bounded number of times.
  void time_taint() {
    if (!timed_) {
      return;
    }
    // The readers of each name, kept statements only (CSR).
    std::vector<uint32_t> head(name_.size() + 1, 0);
    for (uint32_t s = 0; s < stmt_.size(); ++s) {
      if (!stmt_[s].dead) {
        for (const auto n : reads_of(s)) {
          ++head[n + 1];
        }
      }
    }
    for (size_t i = 1; i < head.size(); ++i) {
      head[i] += head[i - 1];
    }
    std::vector<uint32_t> readers(head.back());
    {
      std::vector<uint32_t> fill(head.begin(), head.end() - 1);
      for (uint32_t s = 0; s < stmt_.size(); ++s) {
        if (!stmt_[s].dead) {
          for (const auto n : reads_of(s)) {
            readers[fill[n]++] = s;
          }
        }
      }
    }
    std::vector<uint32_t> work;
    const auto            each_write = [&](uint32_t s, const auto& fn) {
      if (stmt_[s].dst != kNone) {
        fn(stmt_[s].dst);
      }
      for (auto i = stmt_[s].agg_begin; i < stmt_[s].agg_end; ++i) {
        fn(agg_def_[i].first);
      }
    };

    // Names whose value may depend on a reg: only an output value among them
    // can reach one through a chain, before the step or after it.
    std::vector<uint8_t> from_reg(name_.size(), 0);
    for (uint32_t n = 0; n < name_.size(); ++n) {
      if (name_[n].reg) {
        from_reg[n] = 1;
        work.push_back(n);
      }
    }
    while (!work.empty()) {
      const auto n = work.back();
      work.pop_back();
      for (auto i = head[n]; i < head[n + 1]; ++i) {
        each_write(readers[i], [&](uint32_t w) {
          if (from_reg[w] == 0) {
            from_reg[w] = 1;
            work.push_back(w);
          }
        });
      }
    }

    std::vector<uint8_t>  seen(stmt_.size(), 0);
    std::vector<uint8_t>  name_seen(name_.size(), 0);
    std::vector<uint32_t> links;
    // A copy or slice link keeps its spelling and continues down the value it
    // copies or slices (tolg aliases a copy); any other cell ends the chain and
    // only must not become a slice.
    const auto            link = [&](uint32_t d) {
      if (seen[d] != 0) {
        return;
      }
      seen[d]      = 1;
      const auto t = stmt_[d].ltype;
      if (N::is_store(t) || N::is_get_mask(t) || N::is_sext(t)) {
        shape_.push_back(d);
        links.push_back(d);
      } else {
        stmt_[d].no_slice = true;
      }
    };
    const auto follow = [&](uint32_t n) {
      // A name continues the chain through its one value definition (a declare
      // carries none); several (a merge), none (a port) or an opaque write end
      // tolg's chain at a cell that is no slice.
      if (name_seen[n] != 0) {
        return;
      }
      name_seen[n] = 1;
      if (name_[n].reg) {
        name_[n].time_src = true;
        return;
      }
      uint32_t decls = 0;
      uint32_t vals  = 0;
      uint32_t val   = kNone;
      for (auto d = name_[n].def_head; d != kNone; d = stmt_[d].next_def) {
        if (N::is_declare(stmt_[d].ltype)) {
          ++decls;
        } else {
          ++vals;
          val = d;
        }
      }
      if (vals == 1 && decls + vals == name_[n].defs && !stmt_[val].dead) {
        link(val);
      }
    };
    const auto chains = [&] {
      while (!links.empty()) {
        const auto reads = reads_of(links.back());
        links.pop_back();
        if (!reads.empty()) {
          follow(reads.front());  // the copied value, the sliced operand
        }
      }
    };
    for (uint32_t s = 0; s < stmt_.size(); ++s) {
      const auto d = stmt_[s].dst;
      if (d == kNone || stmt_[s].dead || N::is_declare(stmt_[s].ltype) || !is_output(d)) {
        continue;
      }
      if (name_[d].out_cycle || (name_[d].base != kNone && name_[name_[d].base].out_cycle)) {
        stmt_[s].w_seed = true;
      }
      const auto reads = reads_of(s);
      if (std::any_of(reads.begin(), reads.end(), [&](uint32_t n) { return from_reg[n] != 0; })) {
        link(s);
        chains();
      }
    }
    for (const auto n : check_targets_) {
      follow(n);
      chains();
    }

    const auto taint = [&](uint32_t n) {
      if (!name_[n].timed && !name_[n].input) {
        name_[n].timed = true;
        work.push_back(n);
      }
    };
    const auto read_timed = [&](uint32_t s) {
      if (stmt_[s].time_op) {
        return;
      }
      stmt_[s].time_op = true;
      each_write(s, taint);
      const auto t = stmt_[s].ltype;
      if (!(N::is_if(t) || N::is_unique_if(t)) || stmt_[s].arm_seen) {
        return;
      }
      // A timed condition times the merge of every name an arm writes. An if
      // already seen had its arms covered: skip its subtree.
      stmt_[s].arm_seen = true;
      for (auto a = s + 1; a < stmt_[s].end;) {
        if (stmt_[a].arm_seen) {
          a = (N::is_if(stmt_[a].ltype) || N::is_unique_if(stmt_[a].ltype)) ? stmt_[a].end : a + 1;
          continue;
        }
        stmt_[a].arm_seen = true;
        each_write(a, taint);
        ++a;
      }
    };
    for (uint32_t n = 0; n < name_.size(); ++n) {
      if (name_[n].time_src) {
        taint(n);
      }
    }
    while (!work.empty()) {
      const auto n = work.back();
      work.pop_back();
      for (auto i = head[n]; i < head[n + 1]; ++i) {
        read_timed(readers[i]);
      }
    }

    for (uint32_t s = 0; s < stmt_.size(); ++s) {
      if (stmt_[s].dead) {
        continue;
      }
      bool timed_write = false;
      each_write(s, [&](uint32_t w) { timed_write = timed_write || name_[w].timed; });
      if (stmt_[s].time_op || timed_write) {
        stmt_[s].w_seed = true;
      }
      if (timed_write) {
        for (auto p = stmt_[s].parent_if; p != kNone && !stmt_[p].if_up; p = stmt_[p].parent_if) {
          stmt_[p].if_up  = true;
          stmt_[p].w_seed = true;
        }
      }
    }
  }

  [[nodiscard]] bool is_output(uint32_t n) const {
    return name_[n].output || (name_[n].base != kNone && name_[name_[n].base].output);
  }

  // Forward from the clock nets, in preorder (a definition precedes its reads
  // outside barriers): a statement that reads one computes a gated or derived
  // clock, and stays as written along with everything that reads its result.
  void clock_taint() {
    for (uint32_t s = 0; s < stmt_.size(); ++s) {
      const auto d = stmt_[s].dst;
      // An attr_set or declare names its target; it computes no value from what it reads.
      if (d == kNone || name_[d].clock_net || N::is_attr_set(stmt_[s].ltype) || N::is_declare(stmt_[s].ltype)) {
        continue;
      }
      for (const auto n : reads_of(s)) {
        if (name_[n].clock_net) {
          name_[d].clock_net = true;
          name_[d].clock     = true;
          break;
        }
      }
    }
  }

  // Backward closure from the W-seeds through every definition (arms, user
  // names and opaque aggregates included). Each statement and each name is
  // visited once.
  void pin_closure() {
    // Aggregate definitions are kept on a side list (an aggregate defines many
    // names); index them per name for the closure.
    std::vector<uint32_t> agg_head(name_.size(), kNone);
    std::vector<uint32_t> agg_next(agg_def_.size(), kNone);
    for (uint32_t i = 0; i < agg_def_.size(); ++i) {
      agg_next[i]                   = agg_head[agg_def_[i].first];
      agg_head[agg_def_[i].first]   = i;
    }
    std::vector<uint32_t> work;
    const auto            pin = [&](uint32_t s) {
      if (stmt_[s].pinned) {
        return;
      }
      stmt_[s].pinned = true;
      for (const auto n : reads_of(s)) {
        if (!name_[n].pin_seen) {
          name_[n].pin_seen = true;
          work.push_back(n);
        }
      }
    };
    for (uint32_t s = 0; s < stmt_.size(); ++s) {
      if (stmt_[s].w_seed && !stmt_[s].dead) {
        pin(s);
      }
    }
    while (!work.empty()) {
      const auto n = work.back();
      work.pop_back();
      for (auto d = name_[n].def_head; d != kNone; d = stmt_[d].next_def) {
        pin(d);
      }
      for (auto i = agg_head[n]; i != kNone; i = agg_next[i]) {
        pin(agg_def_[i].second);
      }
    }
  }

  const Lnast&                               ln_;
  Name_table                                 names_;
  std::vector<Name_info>                     name_;
  std::vector<Stmt_info>                     stmt_;
  std::vector<uint32_t>                      reads_;
  std::vector<uint32_t>                      attributed_;  // targets of a synth.* attr_set
  absl::flat_hash_map<uint64_t, uint32_t>    twin_ids_;    // twin key -> group
  std::vector<uint32_t>                      twin_first_;  // per group: its first imported statement
  std::vector<uint32_t>                      twin_total_;  // per group: live readers of all its statements
  std::vector<uint32_t>                      scratch_;
  std::vector<uint32_t>                      agg_reads_;
  std::vector<std::pair<uint32_t, uint32_t>> agg_def_;  // (name, aggregate statement)
  struct Clock_attr {
    uint32_t stmt;
    uint32_t target;
    uint32_t value;
    bool     clock_pin;
  };
  std::vector<Clock_attr>                    clock_attrs_;
  std::vector<uint32_t>                      check_targets_;  // the names `@[N]` checks name
  std::vector<uint32_t>                      shape_;          // cycle-check chain links (time_taint)
  bool                                       timed_          = false;
  bool                                       pipe_           = false;
  uint32_t                                   cur_if_         = kNone;
  uint32_t                                   agg_            = kNone;
  uint32_t                                   region_         = 0;
  uint32_t                                   region_serial_  = 0;
  uint32_t                                   prologue_last_  = kNone;
  uint64_t                                   edges_          = 0;
  uint32_t                                   mapped_         = 0;
  uint32_t                                   tmp_stores_     = 0;
};

// ---------------------------------------------------------------------------
// The walk
// ---------------------------------------------------------------------------

class Tree_rewriter final : public ag::Rewriter {
public:
  Tree_rewriter(Prescan& pre, Lnast& ln, const ag::Options& options, std::string unit)
      : Rewriter(options, std::move(unit)), pre_(pre), ln_(ln) {
    // The allowlist on top of rules_disable / rules_only.
    const auto& allow = allowed_rule_mask();
    rules_active_     = false;
    for (size_t id = 1; id < enabled_.size(); ++id) {
      enabled_[id]  = enabled_[id] != 0 && id < allow.size() && allow[id] != 0 ? 1 : 0;
      rules_active_ = rules_active_ || enabled_[id] != 0;
    }
    // Rules spend from a budget proportional to the input (Rewriter's default 0 runs none).
    work_budget_ = opt_.edge_budget == 0 ? UINT64_MAX : uint64_t{opt_.edge_budget} * pre_.edges();
    cur_.assign(pre_.num_names(), Binding{});
    written_.assign(pre_.num_names(), 0);
    retired_.assign(pre_.num_names(), 0);
  }

  void run(Staging_rewrite_stats& st) {
    st_ = &st;
    for (auto c = ln_.get_root().first_child(); !c.is_invalid(); c = c.next_sibling()) {
      if (N::is_stmts(ln_.get_type(c))) {
        walk_block(c);
      }
    }
    const auto& counts = stats().rule_counts;
    st.rules.assign(counts.size(), {0, 0, 0});
    for (size_t id = 1; id < counts.size(); ++id) {
      st.rules[id] = {counts[id].tried, counts[id].matched, counts[id].applied};
    }
    st.budget_hit += stats().budget_hit;
  }

  // import_view: the first imported statement's own e-node.
  [[nodiscard]] std::string describe_first() const { return first_raw_ == kNoClass ? std::string{} : describe_class(first_raw_); }

protected:
  void ensure(Class_id cls) override {
    Rewriter::ensure(cls);
    if (holder_.size() <= cls) {
      holder_.resize(cls + 1);
      io_leaf_.resize(cls + 1, 0);
    }
  }

private:
  struct Binding {
    Class_id cls = kNoClass;  // kNoClass: unbound (a leaf is minted on the first read)
    uint32_t gen = 0;
    // The statement as spelled reads only what `cls` depends on: its spelling
    // is the class (unchanged or respelled) over exact names. Only an exact
    // name holds a class, so a rename onto a holder never adds a read the
    // renamed operand did not already have (tolg's comb-loop and cycle checks
    // read structure, not values).
    bool exact = true;
  };
  // A name whose current value is the class: valid while the name keeps the
  // binding generation it had and no barrier came after the statement.
  // `id` is the spelling it was bound under: tolg reads `x` and `` `x` `` as one
  // name, but the runner's DCE keeps a definition alive only through a read of
  // its own spelling.
  struct Holder {
    uint32_t name = kNone;
    uint32_t gen  = 0;
    uint32_t seq  = 0;
    int32_t  id   = 0;
  };
  struct Undo {
    uint32_t name;
    Binding  old;
  };
  struct Opnd {
    Lnast_nid   nid;
    uint32_t    name  = kNone;
    Class_id    cls   = kNoClass;
    const Dlop* value = nullptr;  // a fully known numeric const
  };
  struct Spelled {
    bool        is_ref = false;
    int32_t     id     = 0;
    std::string text;
    Kind        kind = Kind::unknown;
  };

  // ---- name model ----
  // The local id of a name; a name the prescan never saw (a const-spelled name
  // under an opaque statement) grows the per-name tables.
  uint32_t nm(int32_t id) {
    const auto n = pre_.local(id);
    grow_names();
    return n;
  }
  uint32_t nm(std::string_view text) {
    const auto n = pre_.name_local(text);
    grow_names();
    return n;
  }

  void set(uint32_t n, Class_id cls, bool exact = true) {
    undo_.push_back({n, cur_[n]});
    cur_[n] = {cls, ++gen_, exact};
  }
  [[nodiscard]] bool reads_exact(std::span<const Opnd> ops) const {
    return std::ranges::all_of(ops, [this](const Opnd& o) { return o.name == kNone || cur_[o.name].exact; });
  }
  void kill(uint32_t n) { set(n, kNoClass); }
  void rollback(size_t mark) {
    while (undo_.size() > mark) {
      cur_[undo_.back().name] = undo_.back().old;
      undo_.pop_back();
    }
  }

  Class_id read_class(uint32_t n, uint32_t s, int32_t id) {
    if (retired_[n] != 0) {
      // The prescan saw no mention past the if that retired it; should one
      // reach here anyway, it reads an unknown value, never the binding the
      // arm rollback restored (that is the name's value before the if).
      const auto leaf = eg_.add_leaf();
      ensure(leaf);
      return leaf;
    }
    auto& b = cur_[n];
    if (b.cls == kNoClass) {
      b.cls = eg_.add_leaf();
      ensure(b.cls);
      holder_[b.cls]  = {n, b.gen, s, id};
      io_leaf_[b.cls] = pre_.info(n).io_input && b.gen == 0 ? 1 : 0;
      if (io_leaf_[b.cls] && kind_of(n) == Kind::boolean) {
        rt::seed_leaf(eg_, b.cls, 1, true);  // Bool inputs denote 0 or 1 even before bitwidth.
      }
      return b.cls;
    }
    const auto c = eg_.resolve(b.cls);
    // The name is read here, so a barrier behind its own leaf does not hide it.
    if (eg_.cls(c).kind == ag::Egraph::Kind::leaf && holder_[c].name == n && holder_[c].gen == b.gen) {
      holder_[c].seq = std::max(holder_[c].seq, s);
    }
    return c;
  }

  [[nodiscard]] uint32_t valid_holder(Class_id c) const {
    if (c >= holder_.size()) {
      return kNone;
    }
    const auto& h = holder_[c];
    if (h.name == kNone || cur_[h.name].gen != h.gen || h.seq < min_valid_seq_ || retired_[h.name] != 0) {
      return kNone;
    }
    return h.name;
  }

  void make_holder(Class_id c, uint32_t n, uint32_t s, int32_t id) {
    if (cur_[n].exact && valid_holder(c) == kNone) {
      holder_[c] = {n, cur_[n].gen, s, id};
    }
  }

  [[nodiscard]] Kind kind_of(uint32_t n) const { return pre_.kind(n); }

  void kill_subtree(const Lnast_nid& root) {
    absl::InlinedVector<Lnast_nid, 16> stack{root};
    while (!stack.empty()) {
      const auto nid = stack.back();
      stack.pop_back();
      const auto t = ln_.get_type(nid);
      if (N::is_ref(t)) {
        kill(nm(ln_.get_name_id(nid)));
      } else if (N::is_const(t)) {
        if (const auto text = ln_.get_name(nid); name_like(text)) {
          kill(nm(text));
        }
      }
      for (auto c = nid.first_child(); !c.is_invalid(); c = c.next_sibling()) {
        stack.push_back(c);
      }
    }
  }

  void grow_names() {
    if (cur_.size() < pre_.num_names()) {
      cur_.resize(pre_.num_names(), Binding{});
      written_.resize(pre_.num_names(), 0);
      retired_.resize(pre_.num_names(), 0);
    }
  }

  // ---- statements ----
  void walk_block(const Lnast_nid& block) {
    for (auto c = block.first_child(); !c.is_invalid(); c = c.next_sibling()) {
      walk_stmt(c);
    }
  }

  void walk_stmt(const Lnast_nid& nid) {
    const auto  s  = next_stmt_++;
    const auto& si = pre_.stmt(s);
    const auto  t  = si.ltype;
    ++st_->stmts;
    if (N::is_stmts(t)) {
      walk_block(nid);  // name-transparent, as tolg's lower_stmts
    } else if (N::is_if(t)) {
      walk_if(nid, s);
    } else {
      if (is_barrier_compound(t) || !is_known_leaf(t)) {
        kill_subtree(nid);
        min_valid_seq_ = si.end;  // nothing defined before is renamed into after it
      } else if (si.dead) {
        kill(si.dst);  // the DCE sweeps it: no import, no read, never a holder
      } else if (N::is_store(t)) {
        walk_store(nid, s);
      } else if (is_mapped_op(t)) {
        walk_op(nid, s, t);
      } else {
        opaque(nid, t);
      }
      next_stmt_ = si.end;
    }
    // The walk and the prescan number statements alike (a barrier skips its own).
    I(next_stmt_ == si.end, "tree aegraph: statement numbering diverged from the prescan");
    if (s == pre_.prologue_last()) {
      // tolg drops the defaulted-input prologue by liveness: no later reader may
      // start reading a name defined in it.
      min_valid_seq_ = next_stmt_;
    }
  }

  void opaque(const Lnast_nid& nid, Ltype t) {
    if (writes_any_ref(t)) {
      kill_subtree(nid);
      return;
    }
    if (const auto d = nid.first_child(); !d.is_invalid() && N::is_ref(ln_.get_type(d))) {
      if (N::is_type_spec(t) && Lnast::is_tmp(ln_.get_name_id(d))) {
        return;  // tolg ignores a temp's type_spec (note_type_spec): an annotation, not a write
      }
      kill(nm(ln_.get_name_id(d)));
    }
  }

  void walk_if(const Lnast_nid& nid, uint32_t s) {
    const uint32_t        serial = ++if_serial_;
    std::vector<uint32_t> written;
    for (auto c = nid.first_child(); !c.is_invalid(); c = c.next_sibling()) {
      if (!N::is_stmts(ln_.get_type(c))) {
        rename_cond(c, s);  // conditions are read in the scope before the if
        continue;
      }
      const auto mark = undo_.size();
      walk_block(c);
      for (auto i = mark; i < undo_.size(); ++i) {
        const auto n = undo_[i].name;
        if (written_[n] != serial) {
          written_[n] = serial;
          written.push_back(n);
        }
      }
      rollback(mark);
    }
    // tolg merges every name an arm wrote into a Mux: a new value. A name
    // nothing past the if mentions (an arm-local, which tolg does not merge
    // either) is retired instead: no holder of it is valid again, so no rename
    // can start reading it. A kill would push an undo entry that every
    // enclosing if collects and kills again, O(depth) per arm-local name;
    // retired, each costs one visit, and only names live past the if pay a
    // kill per level, as tolg pays a Mux per level for them.
    const auto end = pre_.stmt(s).end;
    for (const auto n : written) {
      if (pre_.info(n).last_ref < end) {
        retired_[n] = 1;
      } else {
        kill(n);
      }
    }
  }

  void rename_cond(const Lnast_nid& c, uint32_t s) {
    if (!N::is_ref(ln_.get_type(c)) || pre_.stmt(s).pinned) {
      return;  // a pinned if's condition times a merge a cycle check reads
    }
    const auto n = nm(ln_.get_name_id(c));
    if (!pre_.info(n).tmp || pre_.info(n).special || kind_of(n) != Kind::boolean) {
      return;
    }
    const auto cls = read_class(n, s, ln_.get_name_id(c));
    if (eg_.cls(cls).kind != ag::Egraph::Kind::node) {
      return;  // a condition stays a name (never a constant: constprop already folded those)
    }
    const auto h = valid_holder(cls);
    if (h == kNone || h == n || pre_.info(h).special || kind_of(h) != Kind::boolean) {
      return;
    }
    ln_.set_name_id(c, holder_[cls].id);
    ++st_->renamed;
  }

  // A reader operand `op` (a temp) now reads the constant or the older name its
  // value has. `reads` = the names the reader already reads (a special name is
  // reused only by a reader that reads it anyway).
  void try_rename(Opnd& op, std::span<const Opnd> reads, bool store_rhs) {
    if (op.name == kNone || !pre_.info(op.name).tmp || op.cls == kNoClass) {
      return;
    }
    const auto& c = eg_.cls(op.cls);
    if (c.kind == ag::Egraph::Kind::konst) {
      const auto text = const_text(*eg_.value_of(op.cls), kind_of(op.name));
      if (!text.empty()) {
        ln_.set_type(op.nid, N::Lnast_ntype_const);
        ln_.set_name(op.nid, text);
        op.name  = kNone;
        op.value = eg_.value_of(op.cls);
        ++st_->consts;
      }
      return;
    }
    if (c.kind == ag::Egraph::Kind::literal) {
      return;  // a `?` is one unknown per spelling: never copied
    }
    const auto h = valid_holder(op.cls);
    if (h == kNone || h == op.name) {
      return;
    }
    const auto k = kind_of(op.name);
    if (k == Kind::unknown || kind_of(h) != k) {
      return;
    }
    if (pre_.info(h).special && std::none_of(reads.begin(), reads.end(), [h](const Opnd& o) { return o.name == h; })) {
      return;
    }
    if (store_rhs) {
      // A copy reads a name only when that is a plain value: an expression temp
      // or a scalar input (tolg's store keeps alias maps for everything else).
      const bool ok = (c.kind == ag::Egraph::Kind::node && pre_.info(h).tmp) || (c.kind == ag::Egraph::Kind::leaf && io_leaf_[op.cls] != 0);
      if (!ok) {
        return;
      }
    }
    ln_.set_name_id(op.nid, holder_[op.cls].id);
    op.name = h;
    ++st_->renamed;
  }

  void walk_store(const Lnast_nid& nid, uint32_t s) {
    const auto dst = nid.first_child();
    const auto rhs = dst.is_invalid() ? dst : dst.next_sibling();
    if (dst.is_invalid() || rhs.is_invalid() || !rhs.next_sibling().is_invalid() || !N::is_ref(ln_.get_type(dst))) {
      kill_subtree(nid);  // a field-path store writes into its base
      return;
    }
    const auto& si = pre_.stmt(s);
    const auto  n  = nm(ln_.get_name_id(dst));
    if (pre_.time_frozen(s)) {
      kill(n);
      return;
    }
    Opnd op;
    op.nid      = rhs;
    bool frozen = false;
    if (N::is_ref(ln_.get_type(rhs))) {
      op.name = nm(ln_.get_name_id(rhs));
      frozen = pre_.info(op.name).special || pre_.info(op.name).clock;  // a clock alias stays as written
      op.cls = read_class(op.name, s, ln_.get_name_id(rhs));
      if (!frozen && !si.pinned) {
        try_rename(op, std::span<const Opnd>(&op, 1), true);
      }
    } else if (N::is_const(ln_.get_type(rhs))) {
      if (const auto* v = numeric_const(ln_, ln_.get_name(rhs)); v != nullptr && !v->has_unknowns()) {
        op.cls = eg_.add_const(*v);
        ensure(op.cls);
      }
    }
    const auto& ni = pre_.info(n);
    if (frozen || !ni.tmp || ni.state || ni.special || op.cls == kNoClass) {
      kill(n);
      return;
    }
    const auto kind = eg_.cls(op.cls).kind;
    if (!(kind == ag::Egraph::Kind::konst || kind == ag::Egraph::Kind::node || io_leaf_[op.cls] != 0)) {
      kill(n);
      return;
    }
    const auto h = valid_holder(op.cls);
    set(n, op.cls, reads_exact(std::span<const Opnd>(&op, 1)));
    if (kind == ag::Egraph::Kind::konst || (h != kNone && h != n)) {
      ++st_->forwarded;
    } else {
      make_holder(op.cls, n, s, ln_.get_name_id(dst));
    }
  }

  void walk_op(const Lnast_nid& nid, uint32_t s, Ltype t) {
    const auto dst = nid.first_child();
    if (dst.is_invalid() || !N::is_ref(ln_.get_type(dst))) {
      opaque(nid, t);
      return;
    }
    const auto n = nm(ln_.get_name_id(dst));
    if (ln_.is_dce_dead(nid) || pre_.time_frozen(s)) {
      kill(n);  // a cycle check judges it as written: never imported, so never a holder
      return;
    }
    const auto& si = pre_.stmt(s);

    absl::InlinedVector<Opnd, 4> ops;
    bool                         frozen = false;
    for (auto c = dst.next_sibling(); !c.is_invalid(); c = c.next_sibling()) {
      Opnd       op;
      const auto ct = ln_.get_type(c);
      op.nid        = c;
      if (N::is_ref(ct)) {
        op.name = nm(ln_.get_name_id(c));
        const auto& oi = pre_.info(op.name);
        if (oi.clock) {
          kill(n);  // an ICG `clk & en` stays exactly as written
          return;
        }
        frozen = frozen || oi.special;
      } else if (N::is_const(ct)) {
        const auto* v = numeric_const(ln_, ln_.get_name(c));
        if (v == nullptr) {
          kill(n);
          return;
        }
        if (v->has_unknowns()) {
          op.cls = eg_.add_literal(*v);
        } else {
          op.value = v;
          op.cls   = eg_.add_const(*v);
        }
        ensure(op.cls);
      } else {
        kill(n);
        return;
      }
      ops.push_back(op);
    }
    if (frozen) {
      kill(n);  // tolg keys its memory/call chains on these names: keep the statement and its readers as written
      return;
    }
    for (auto& op : ops) {
      if (op.name != kNone) {
        op.cls = read_class(op.name, s, ln_.get_name_id(op.nid));
      }
    }
    if (!si.pinned && !N::is_bit_not(t)) {
      for (auto& op : ops) {
        try_rename(op, ops, false);
      }
    }
    const auto& ni = pre_.info(n);
    if (!ni.tmp || ni.state || ni.special) {
      kill(n);  // a user or state destination is never looked through
      return;
    }

    ag::Enode_attrs attrs;
    attrs.has_color = si.region != 0;
    attrs.color     = static_cast<int32_t>(si.region);
    const auto imp  = import(t, ops, attrs, pre_.twin_readers(s), pre_.later_twin(s), pre_.chain_inner(s));
    if (!imp) {
      kill(n);
      return;
    }
    ++st_->imported;
    const auto [raw, r] = *imp;
    if (first_raw_ == kNoClass) {
      first_raw_ = raw;
    }
    const auto h        = valid_holder(r);
    // Still spelled as written: exact only if that spelling is the class.
    set(n, r, r == raw && reads_exact(ops));
    if (si.pinned) {
      make_holder(r, n, s, ln_.get_name_id(dst));  // the cone of a width-reading statement keeps its spelling
      return;
    }
    const auto kind = eg_.cls(r).kind;
    if (kind == ag::Egraph::Kind::konst || (h != kNone && h != n)) {
      ++st_->forwarded;  // readers take the constant or the older name; the DCE drops this one
      return;
    }
    if (kind == ag::Egraph::Kind::literal) {
      return;
    }
    if (r != raw && kind == ag::Egraph::Kind::node && respell(nid, t, n, r, ops, si.no_slice)) {
      cur_[n].exact = true;  // spelled over holders and constants only
    }
    make_holder(r, n, s, ln_.get_name_id(dst));
  }

  // The import table: exactly the cells upass.tolg lowers each statement to.
  // Returns (the statement's own class, the class after the rules).
  std::optional<std::pair<Class_id, Class_id>> import(Ltype t, std::span<const Opnd> o, const ag::Enode_attrs& attrs,
                                                      uint32_t readers, bool later_twin, bool chain) {
    absl::InlinedVector<ag::Operand, 8> v;
    Ntype_op                            op = Ntype_op::Invalid;
    const size_t                        k  = o.size();
    const auto all_bank0 = [&] {
      for (const auto& x : o) {
        v.push_back({0, x.cls});
      }
    };
    const auto konst = [&](int64_t val) {
      const auto c = eg_.add_const(*Dlop::create_integer(val));
      ensure(c);
      return c;
    };
    const auto inner = [&](Ntype_op iop, std::span<const ag::Operand> iops) {
      auto in = add_original(iop, iops, attrs);
      if (in.folded) {
        return in.cls;
      }
      readers_[in.raw] += 1;
      if (!in.created) {
        return in.cls;
      }
      cur_attrs_    = attrs;
      current_rule_ = rt::kNoRule;
      depth_        = 0;
      return eg_.resolve(rewrite(in.cls, 0));
    };
    const auto int_of = [&](const Opnd& x) -> std::optional<int64_t> {
      if (x.value == nullptr || !x.value->is_integer() || !x.value->is_just_i64()) {
        return std::nullopt;
      }
      return x.value->to_just_i64();
    };
    switch (t) {
      case N::Lnast_ntype_plus:
        if (k < 1) {
          return std::nullopt;
        }
        op = Ntype_op::Sum;
        all_bank0();
        break;
      case N::Lnast_ntype_minus:
        if (k < 1) {
          return std::nullopt;
        }
        op = Ntype_op::Sum;
        for (size_t i = 0; i < k; ++i) {
          v.push_back({i == 0 ? 0U : 1U, o[i].cls});
        }
        break;
      case N::Lnast_ntype_mult:
      case N::Lnast_ntype_bit_and:
      case N::Lnast_ntype_log_and:
      case N::Lnast_ntype_bit_or:
      case N::Lnast_ntype_log_or:
      case N::Lnast_ntype_bit_xor:
        if (k < 1) {
          return std::nullopt;
        }
        op = chain_op(t);
        all_bank0();
        break;
      case N::Lnast_ntype_eq:
        if (k != 2) {
          return std::nullopt;
        }
        op = Ntype_op::EQ;
        all_bank0();
        break;
      case N::Lnast_ntype_lt:
      case N::Lnast_ntype_gt:
      case N::Lnast_ntype_shl:
      case N::Lnast_ntype_sra:
      case N::Lnast_ntype_div:
        if (k != 2) {
          return std::nullopt;
        }
        op = N::is_lt(t)    ? Ntype_op::LT
             : N::is_gt(t)  ? Ntype_op::GT
             : N::is_shl(t) ? Ntype_op::SHL
             : N::is_sra(t) ? Ntype_op::SRA
                            : Ntype_op::Div;
        v.push_back({0, o[0].cls});
        v.push_back({1, o[1].cls});
        break;
      case N::Lnast_ntype_ne:
      case N::Lnast_ntype_le:
      case N::Lnast_ntype_ge: {
        if (k != 2) {
          return std::nullopt;
        }
        // lower_negated: Xor(cmp, 1) with the compare a one-bit value.
        const auto iop = N::is_ne(t) ? Ntype_op::EQ : N::is_le(t) ? Ntype_op::GT : Ntype_op::LT;
        const ag::Operand iops[2] = {{0, o[0].cls}, {iop == Ntype_op::EQ ? 0U : 1U, o[1].cls}};
        const auto        cmp     = inner(iop, iops);
        op                        = Ntype_op::Xor;
        v.push_back({0, cmp});
        v.push_back({0, konst(1)});
        break;
      }
      case N::Lnast_ntype_log_not:
        if (k != 1) {
          return std::nullopt;
        }
        op = Ntype_op::EQ;  // lower_log_not: a truth test, EQ(x, 0)
        v.push_back({0, o[0].cls});
        v.push_back({0, konst(0)});
        break;
      case N::Lnast_ntype_bit_not:
        if (k == 1) {
          op = Ntype_op::Not;
          v.push_back({0, o[0].cls});
        } else if (k == 2) {
          // lower_unary: the typed `~` of an unsigned N-bit value is Not, then Get_mask(0, N).
          const auto bits = int_of(o[1]);
          if (!bits || *bits <= 0 || *bits > std::numeric_limits<int>::max()) {
            return std::nullopt;
          }
          const ag::Operand nops[1] = {{0, o[0].cls}};
          const auto        notx    = inner(Ntype_op::Not, nops);
          op                        = Ntype_op::Get_mask;
          v.push_back({0, notx});
          v.push_back({2, konst(0)});
          v.push_back({3, o[1].cls});
        } else {
          return std::nullopt;
        }
        break;
      case N::Lnast_ntype_get_mask: {
        if (k != 2 && k != 3) {
          return std::nullopt;
        }
        const auto lo = int_of(o[1]);
        const auto hi = k == 3 ? int_of(o[2]) : (lo ? std::optional<int64_t>{*lo + 1} : std::nullopt);
        if (!lo || !hi || *lo < 0 || *hi > std::numeric_limits<int>::max()) {
          return std::nullopt;
        }
        if (*hi <= *lo) {
          const auto zero = konst(0);
          return std::pair{zero, zero};
        }
        op = Ntype_op::Get_mask;
        v.push_back({0, o[0].cls});
        v.push_back({2, konst(*lo)});
        v.push_back({3, konst(*hi)});
        break;
      }
      case N::Lnast_ntype_sext: {
        if (k != 2) {
          return std::nullopt;
        }
        const auto pos = int_of(o[1]);
        if (!pos || *pos < 0 || *pos >= std::numeric_limits<int>::max()) {
          return std::nullopt;
        }
        op = Ntype_op::Sext;  // LNAST names the sign-bit position, the cell takes the bit count
        v.push_back({0, o[0].cls});
        v.push_back({1, konst(*pos + 1)});
        break;
      }
      case N::Lnast_ntype_red_or:
        if (k != 1) {
          return std::nullopt;
        }
        op = Ntype_op::Ror;
        v.push_back({0, o[0].cls});
        break;
      default: return std::nullopt;
    }

    auto top = add_original(op, v, attrs);
    if (top.folded) {
      return std::pair{top.cls, top.cls};
    }
    readers_[top.raw] += later_twin && !top.created ? 0 : readers;  // a twin hash-consed into the first's class
    Class_id r = top.cls;
    if (top.created) {
      chain_inner_[top.raw] = chain ? 1 : 0;
      cur_attrs_            = attrs;
      current_rule_         = rt::kNoRule;
      depth_                = 0;
      r                     = rewrite(top.cls, 0);
    }
    return std::pair{top.raw, eg_.resolve(r)};
  }

  // ---- write-back ----

  // The operand class `c` as statement text: a constant read as `ctx`, or a
  // name holding the value now.
  bool spell_operand(Class_id c, Kind ctx, std::span<const Opnd> reads, Spelled& out) {
    const auto& cl = eg_.cls(c);
    if (cl.kind == ag::Egraph::Kind::konst) {
      out.text = const_text(*eg_.value_of(c), ctx);
      out.kind = ctx;
      return !out.text.empty();
    }
    if (cl.kind == ag::Egraph::Kind::literal) {
      return false;
    }
    const auto h = valid_holder(c);
    if (h == kNone) {
      return false;
    }
    if (pre_.info(h).special && std::none_of(reads.begin(), reads.end(), [h](const Opnd& o) { return o.name == h; })) {
      return false;
    }
    out.is_ref = true;
    out.id     = holder_[c].id;
    out.kind   = kind_of(h);
    return true;
  }

  // Retype statement `nid` (child 0 = `n`) to the single node of class `r`,
  // in place: set_type, then children overwritten by position and extra
  // operands appended at the tail. Refused (and counted) when the result needs
  // a node no LNAST statement spells, an unnamed operand, fewer children, a
  // different kind, leaves tolg's partial-write op class, or (`no_slice`) would
  // extend an output's chain of slices to a reg (Prescan::time_taint).
  [[nodiscard]] bool respell(const Lnast_nid& nid, Ltype t, uint32_t n, Class_id r, std::span<const Opnd> reads, bool no_slice) {
    const auto* en = eg_.enode_of(r);
    // An untyped `~` keeps its spelling: the runner chose untyped from its
    // operand's type, a choice an inliner re-walk makes again on what it reads.
    if (en == nullptr || (N::is_bit_not(t) && reads.size() == 1)) {
      ++st_->refused_spell;
      return false;
    }
    const auto ops   = eg_.operands(*en);
    const auto k_dst = kind_of(n);
    Ltype      lt    = N::Lnast_ntype_invalid;
    absl::InlinedVector<Spelled, 4> out;

    const auto int_ctx  = [&](Class_id c, Spelled& sp) { return spell_operand(c, Kind::integer, reads, sp); };
    const auto konst_is = [&](Class_id c, int64_t val) {
      const auto* kv = eg_.cls(c).kind == ag::Egraph::Kind::konst ? eg_.value_of(c) : nullptr;
      return kv != nullptr && kv->is_just_i64() && kv->to_just_i64() == val;
    };
    // A two-operand compare node: the operands as (a, b) with tolg's banks.
    const auto compare_pair = [&](const ag::Enode& cmp, Class_id& a, Class_id& b) {
      const auto co = eg_.operands(cmp);
      if (co.size() != 2) {
        return false;
      }
      if (cmp.op == Ntype_op::EQ) {
        a = co[0].cls;
        b = co[1].cls;
        return true;
      }
      if ((cmp.op == Ntype_op::LT || cmp.op == Ntype_op::GT) && co[0].bank == 0 && co[1].bank == 1) {
        a = co[0].cls;
        b = co[1].cls;
        return true;
      }
      return false;
    };
    const auto spell_compare = [&](Class_id a, Class_id b) {
      // A constant side reads as the other side's kind.
      Spelled sa;
      Spelled sb;
      const bool ka = eg_.cls(a).kind == ag::Egraph::Kind::konst;
      const bool kb = eg_.cls(b).kind == ag::Egraph::Kind::konst;
      if (!ka && !spell_operand(a, Kind::unknown, reads, sa)) {
        return false;
      }
      if (!kb && !spell_operand(b, Kind::unknown, reads, sb)) {
        return false;
      }
      if (ka && !spell_operand(a, !kb && sb.kind != Kind::unknown ? sb.kind : Kind::integer, reads, sa)) {
        return false;
      }
      if (kb && !spell_operand(b, !ka && sa.kind != Kind::unknown ? sa.kind : Kind::integer, reads, sb)) {
        return false;
      }
      if (sa.kind != Kind::unknown && sb.kind != Kind::unknown && sa.kind != sb.kind) {
        return false;
      }
      out.push_back(std::move(sa));
      out.push_back(std::move(sb));
      return true;
    };
    // ne/le/ge: the negation of a compare (Xor(cmp, 1) or EQ(cmp, 0)).
    const auto negated = [&](Class_id cmp_cls) -> bool {
      const auto* cmp = eg_.enode_of(cmp_cls);
      Class_id    a   = kNoClass;
      Class_id    b   = kNoClass;
      if (cmp == nullptr || !compare_pair(*cmp, a, b)) {
        return false;
      }
      lt = cmp->op == Ntype_op::EQ ? N::Lnast_ntype_ne : cmp->op == Ntype_op::GT ? N::Lnast_ntype_le : N::Lnast_ntype_ge;
      if (cmp->op == Ntype_op::EQ && eg_.cls(a).kind == ag::Egraph::Kind::konst && eg_.cls(b).kind != ag::Egraph::Kind::konst) {
        std::swap(a, b);  // `x != 3`, the constant last as the plain EQ spelling has it
      }
      return spell_compare(a, b);
    };

    bool ok = false;
    switch (en->op) {
      case Ntype_op::Sum: {
        if (ops.size() > 2) {
          ++st_->refused_sum;  // tolg sizes every Sum as max+1, whatever its arity
          return false;
        }
        absl::InlinedVector<Class_id, 2> as;
        absl::InlinedVector<Class_id, 2> bs;
        for (const auto& x : ops) {
          (x.bank == 0 ? as : bs).push_back(x.cls);
        }
        Spelled a;
        Spelled b;
        if (as.size() == 2) {
          lt = N::Lnast_ntype_plus;
          if (eg_.cls(as[0]).kind == ag::Egraph::Kind::konst) {
            std::swap(as[0], as[1]);  // the constant last
          }
          ok = int_ctx(as[0], a) && int_ctx(as[1], b);
        } else if (as.size() == 1 && bs.size() == 1) {
          lt = N::Lnast_ntype_minus;
          ok = int_ctx(as[0], a) && int_ctx(bs[0], b);
        } else if (as.empty() && bs.size() == 1) {
          lt     = N::Lnast_ntype_minus;
          a.text = "0";
          a.kind = Kind::integer;
          ok     = int_ctx(bs[0], b);
        }
        if (ok) {
          out.push_back(std::move(a));
          out.push_back(std::move(b));
        }
        break;
      }
      case Ntype_op::Mult:
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor: {
        if (en->op == Ntype_op::Xor && ops.size() == 2 && (konst_is(ops[0].cls, 1) || konst_is(ops[1].cls, 1))) {
          if (negated(konst_is(ops[1].cls, 1) ? ops[0].cls : ops[1].cls)) {
            ok = true;
            break;
          }
          out.clear();
        }
        const bool bitwise = en->op != Ntype_op::Mult;
        const Kind ctx     = bitwise && k_dst != Kind::unknown ? k_dst : Kind::integer;
        ok                 = true;
        // Pin order is what tolg, the LGraph passes and the ABC AIG walk see (the
        // e-node keeps a commutative bank sorted by class, which perturbed
        // ibex's critical path and its simulator kernel order). The same
        // multiset as the statement reads keeps the statement's order. A changed
        // one (a flatten absorbed a chain link) takes the order the LGraph
        // engine's rewire() gives the same rewrite, so the result is what
        // the LGraph flatten would have built: drivers in creation order --
        // here the holder's statement, inputs first in port order -- newest
        // first for And/Or, oldest first for Xor/Mult, constants last.
        absl::InlinedVector<ag::Operand, 8> ordered(ops.begin(), ops.end());
        absl::InlinedVector<uint64_t, 8>                                rank(ordered.size(), 0);
        absl::flat_hash_map<Class_id, absl::InlinedVector<uint32_t, 2>> read_pos;  // a class read twice keeps both slots
        for (uint32_t j = static_cast<uint32_t>(reads.size()); j-- > 0;) {
          if (reads[j].cls != kNoClass) {
            read_pos[eg_.resolve(reads[j].cls)].push_back(j);  // reversed: pop_back yields the earliest
          }
        }
        bool same_multiset = ordered.size() == reads.size();
        for (size_t i = 0; i < ordered.size(); ++i) {
          if (auto it = read_pos.find(eg_.resolve(ordered[i].cls)); it != read_pos.end() && !it->second.empty()) {
            rank[i] = it->second.back();
            it->second.pop_back();
          } else {
            same_multiset = false;
          }
        }
        if (!same_multiset) {
          const bool newest_first = en->op == Ntype_op::And || en->op == Ntype_op::Or;
          for (size_t i = 0; i < ordered.size(); ++i) {
            const auto c = ordered[i].cls;
            if (eg_.cls(c).kind == ag::Egraph::Kind::konst) {
              rank[i] = ~uint64_t{0};
              continue;
            }
            const uint32_t h       = c < holder_.size() ? holder_[c].name : kNone;
            // created: an input pin first (by port), then a holder by its statement
            const uint64_t created = h != kNone && io_leaf_[c] != 0 ? pre_.info(h).io_pos
                                     : h != kNone                   ? (uint64_t{1} << 32) | holder_[c].seq
                                                                    : (uint64_t{1} << 33) - 1;
            rank[i]                = newest_first ? ((uint64_t{1} << 34) - 1 - created) : created;
          }
        }
        absl::InlinedVector<size_t, 8> perm(ordered.size());
        for (size_t i = 0; i < perm.size(); ++i) {
          perm[i] = i;
        }
        std::ranges::stable_sort(perm, [&](size_t x, size_t y) { return rank[x] < rank[y]; });
        const absl::InlinedVector<ag::Operand, 8> by_class(ordered.begin(), ordered.end());
        for (size_t i = 0; i < perm.size(); ++i) {
          ordered[i] = by_class[perm[i]];
        }
        for (const auto& x : ordered) {
          Spelled sp;
          if (!spell_operand(x.cls, ctx, reads, sp)) {
            ok = false;
            break;
          }
          out.push_back(std::move(sp));
        }
        if (!ok) {
          break;
        }
        const bool all_bool = std::ranges::all_of(out, [](const Spelled& sp) { return sp.kind == Kind::boolean; });
        if (en->op == Ntype_op::Mult) {
          lt = N::Lnast_ntype_mult;
        } else if (en->op == Ntype_op::And) {
          lt = N::is_log_and(t) && all_bool ? N::Lnast_ntype_log_and : N::Lnast_ntype_bit_and;
        } else if (en->op == Ntype_op::Or) {
          lt = N::is_log_or(t) && all_bool ? N::Lnast_ntype_log_or : N::Lnast_ntype_bit_or;
        } else {
          lt = N::Lnast_ntype_bit_xor;
        }
        break;
      }
      case Ntype_op::EQ: {
        if (ops.size() != 2) {
          break;
        }
        if (konst_is(ops[0].cls, 0) || konst_is(ops[1].cls, 0)) {
          const auto x = konst_is(ops[1].cls, 0) ? ops[0].cls : ops[1].cls;
          if (negated(x)) {
            ok = true;
            break;
          }
          out.clear();
          if (N::is_log_not(t)) {
            // `not` takes a boolean: an integer operand is spelled `x == 0` below.
            Spelled sp;
            if (spell_operand(x, Kind::unknown, reads, sp) && sp.kind == Kind::boolean) {
              lt = N::Lnast_ntype_log_not;
              out.push_back(std::move(sp));
              ok = true;
              break;
            }
          }
        }
        lt = N::Lnast_ntype_eq;
        // Commutative: the constant goes last, as the front ends spell it.
        ok = eg_.cls(ops[0].cls).kind == ag::Egraph::Kind::konst ? spell_compare(ops[1].cls, ops[0].cls)
                                                                 : spell_compare(ops[0].cls, ops[1].cls);
        break;
      }
      case Ntype_op::LT:
      case Ntype_op::GT: {
        Class_id a = kNoClass;
        Class_id b = kNoClass;
        if (compare_pair(*en, a, b)) {
          lt = en->op == Ntype_op::LT ? N::Lnast_ntype_lt : N::Lnast_ntype_gt;
          ok = spell_compare(a, b);
        }
        break;
      }
      case Ntype_op::SHL:
      case Ntype_op::SRA:
      case Ntype_op::Div: {
        if (ops.size() != 2 || ops[0].bank != 0 || ops[1].bank != 1) {
          break;
        }
        Spelled a;
        Spelled b;
        if (int_ctx(ops[0].cls, a) && int_ctx(ops[1].cls, b)) {
          lt = en->op == Ntype_op::SHL ? N::Lnast_ntype_shl : en->op == Ntype_op::SRA ? N::Lnast_ntype_sra : N::Lnast_ntype_div;
          out.push_back(std::move(a));
          out.push_back(std::move(b));
          ok = true;
        }
        break;
      }
      case Ntype_op::Get_mask: {
        if (ops.size() != 3 || ops[0].bank != 0 || ops[1].bank != 2 || ops[2].bank != 3) {
          break;
        }
        const auto* lo = eg_.cls(ops[1].cls).kind == ag::Egraph::Kind::konst ? eg_.value_of(ops[1].cls) : nullptr;
        const auto* hi = eg_.cls(ops[2].cls).kind == ag::Egraph::Kind::konst ? eg_.value_of(ops[2].cls) : nullptr;
        if (lo == nullptr || hi == nullptr || !lo->is_just_i64() || !hi->is_just_i64() || lo->to_just_i64() < 0
            || hi->to_just_i64() <= lo->to_just_i64()) {
          break;
        }
        Spelled v;
        Spelled l;
        Spelled h;
        if (int_ctx(ops[0].cls, v) && int_ctx(ops[1].cls, l) && int_ctx(ops[2].cls, h)) {
          lt = N::Lnast_ntype_get_mask;
          out.push_back(std::move(v));
          out.push_back(std::move(l));
          out.push_back(std::move(h));
          ok = true;
        }
        break;
      }
      case Ntype_op::Sext: {
        if (ops.size() != 2 || ops[0].bank != 0 || ops[1].bank != 1) {
          break;
        }
        const auto* kv = eg_.cls(ops[1].cls).kind == ag::Egraph::Kind::konst ? eg_.value_of(ops[1].cls) : nullptr;
        if (kv == nullptr || !kv->is_just_i64() || kv->to_just_i64() < 1) {
          break;
        }
        Spelled a;
        if (int_ctx(ops[0].cls, a)) {
          lt = N::Lnast_ntype_sext;
          out.push_back(std::move(a));
          Spelled p;
          p.text = std::to_string(kv->to_just_i64() - 1);
          p.kind = Kind::integer;
          out.push_back(std::move(p));
          ok = true;
        }
        break;
      }
      case Ntype_op::Ror: {
        Spelled a;
        if (ops.size() == 1 && int_ctx(ops[0].cls, a)) {
          lt = N::Lnast_ntype_red_or;
          out.push_back(std::move(a));
          ok = true;
        }
        break;
      }
      default: break;  // Not (a new `~` needs a typing decision only the walk made), Mux, Concat, Set_mask, Rem
    }
    if (!ok || lt == N::Lnast_ntype_invalid) {
      ++st_->refused_spell;
      return false;
    }

    // Kinds: a later inliner re-walk type-checks the statement again.
    absl::InlinedVector<Kind, 4> kinds;
    for (const auto& sp : out) {
      kinds.push_back(sp.kind);
    }
    const Kind k_new   = result_kind(lt, kinds);
    const bool int_new = N::is_plus(lt) || N::is_minus(lt) || N::is_mult(lt) || N::is_div(lt) || N::is_shl(lt) || N::is_sra(lt)
                         || N::is_get_mask(lt) || N::is_sext(lt) || N::is_red_or(lt);
    const bool kind_ok = (k_new == k_dst && k_new != Kind::unknown) || (k_new == Kind::unknown && k_dst == Kind::unknown && lt == t);
    const bool no_bool_in_int = !int_new || std::ranges::none_of(kinds, [](Kind k) { return k == Kind::boolean; });
    if (!kind_ok || !no_bool_in_int) {
      ++st_->refused_kind;
      return false;
    }
    if ((pure_temp_op(t) && !pure_temp_op(lt)) || (no_slice && (N::is_get_mask(lt) || N::is_sext(lt)))) {
      ++st_->refused_class;
      return false;
    }
    size_t have = 0;
    for (auto c = nid.first_child(); !c.is_invalid(); c = c.next_sibling()) {
      ++have;
    }
    if (out.size() + 1 < have) {
      ++st_->refused_arity;  // children are never deleted (hhds sibling-chain hazard)
      return false;
    }

    if (lt == t && out.size() + 1 == have) {
      // The rules took a detour back to the same statement (ne -> Xor(EQ, 1) ->
      // EQ(EQ, 0) -> ne): leave it untouched.
      bool same = true;
      auto c    = nid.first_child().next_sibling();
      for (size_t i = 0; i < out.size() && same; ++i, c = c.next_sibling()) {
        const auto ct = ln_.get_type(c);
        same = out[i].is_ref ? N::is_ref(ct) && ln_.get_name_id(c) == out[i].id : N::is_const(ct) && ln_.get_name(c) == out[i].text;
      }
      if (same) {
        return true;
      }
    }

    ln_.set_type(nid, lt);
    auto   c = nid.first_child().next_sibling();
    size_t i = 0;
    for (; i < out.size() && !c.is_invalid(); ++i, c = c.next_sibling()) {
      write_leaf(c, out[i]);
    }
    for (; i < out.size(); ++i) {
      const auto added = ln_.add_child(nid, out[i].is_ref ? N::Lnast_ntype_ref : N::Lnast_ntype_const);
      write_leaf(added, out[i]);
    }
    ++st_->respelled;
    return true;
  }

  [[nodiscard]] std::string describe_class(Class_id c) const {
    const auto& cl = eg_.cls(c);
    if (cl.kind == ag::Egraph::Kind::konst) {
      const auto* v = eg_.value_of(c);
      return v->is_bool() ? (v->is_known_true() ? "1" : "0") : v->to_pyrope();
    }
    if (cl.kind == ag::Egraph::Kind::leaf) {
      return c < holder_.size() && holder_[c].name != kNone ? std::string(ln_.resolve_name(holder_[c].id)) : "leaf";
    }
    const auto* en = eg_.enode_of(c);
    if (en == nullptr) {
      return "?";
    }
    std::vector<std::string> parts;
    for (const auto& o : eg_.operands(*en)) {
      parts.push_back(std::format("{}:{}", o.bank, describe_class(o.cls)));
    }
    std::ranges::sort(parts);
    std::string out = std::format("{}(", Ntype::get_name(en->op));
    for (size_t i = 0; i < parts.size(); ++i) {
      out += (i == 0 ? "" : ",") + parts[i];
    }
    return out + ")";
  }

  void write_leaf(const Lnast_nid& c, const Spelled& sp) {
    if (sp.is_ref) {
      ln_.set_type(c, N::Lnast_ntype_ref);
      ln_.set_name_id(c, sp.id);
    } else {
      ln_.set_type(c, N::Lnast_ntype_const);
      ln_.set_name(c, sp.text);
    }
  }

  Prescan&               pre_;
  Lnast&                 ln_;
  Staging_rewrite_stats* st_ = nullptr;
  std::vector<Binding>   cur_;      // per name
  std::vector<uint32_t>  written_;  // per name: the walk_if serial that last listed it
  std::vector<uint8_t>   retired_;  // per name: written in an if and never mentioned past it
  std::vector<Holder>    holder_;   // per class
  std::vector<uint8_t>   io_leaf_;  // per class: the unwritten value of a scalar input port
  std::vector<Undo>      undo_;
  uint32_t               gen_           = 0;
  uint32_t               next_stmt_     = 0;
  uint32_t               min_valid_seq_ = 0;
  uint32_t               if_serial_     = 0;
  Class_id               first_raw_     = kNoClass;
};

ag::Options tree_options() {
  ag::Options o;
  o.rules_disable = livehd::fold_rules_disable();
  o.rewrite_depth = 5;
  o.edge_budget   = 32;
  o.choices       = false;
  o.guard_scope   = false;
  return o;
}

}  // namespace

Staging_rewrite_stats rewrite_staging(const Staging_rewrite_ctx& ctx) {
  Staging_rewrite_stats st;
  const auto            t0 = std::chrono::steady_clock::now();
  Prescan               pre(ctx.staging, ctx.io, ctx.timed, ctx.pipe);
  if (pre.worth_walking()) {
    Tree_rewriter rw(pre, ctx.staging, tree_options(), std::string(ctx.unit));
    rw.run(st);
    st.units = 1;
  }
  st.ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
  return st;
}

void report(const Staging_rewrite_stats& total) {
  std::print(
      "[upass.aeg] units={} stmts={} imported={} forwarded={} renamed={} consts={} respelled={} "
      "refused{{arity={},sum={},kind={},class={},spell={}}} budget_hit={} ms={:.1f}\n",
      total.units,
      total.stmts,
      total.imported,
      total.forwarded,
      total.renamed,
      total.consts,
      total.respelled,
      total.refused_arity,
      total.refused_sum,
      total.refused_kind,
      total.refused_class,
      total.refused_spell,
      total.budget_hit,
      static_cast<double>(total.ns) / 1e6);
  std::fflush(stdout);
  if (!livehd::diag::stat_enabled("pass.upass")) {
    return;
  }
  auto& reg = rt::Registry::instance();
  for (size_t id = 1; id < total.rules.size(); ++id) {
    const auto& c = total.rules[id];
    if (c[0] == 0) {
      continue;
    }
    const auto& info = reg.rule(static_cast<rt::Rule_id>(id));
    livehd::diag::stat("pass.upass", "treeaeg-rule", "progress")
        .msg("treeaeg-rule {} tried={} matched={} applied={}", info.name, c[0], c[1], c[2])
        .attr("rule", info.name)
        .attr("group", info.group)
        .attr("tag", std::string{rt::tag_name(info.tag)})
        .attr("tried", std::to_string(c[0]))
        .attr("matched", std::to_string(c[1]))
        .attr("applied", std::to_string(c[2]))
        .emit();
  }
}

std::string import_view(Lnast& ln, const Lnast_tree_io& io) {
  Prescan     pre(ln, io);
  auto        opt = tree_options();
  opt.rules_off   = true;
  Tree_rewriter rw(pre, ln, opt, "import_view");
  Staging_rewrite_stats st;
  rw.run(st);
  return rw.describe_first();
}

Prescan_report prescan_report(const Lnast& ln, const Lnast_tree_io& io) {
  Prescan        pre(ln, io);
  Prescan_report out;
  for (uint32_t n = 0; n < pre.num_names(); ++n) {
    const auto& i                                     = pre.info(n);
    out.names[std::string(ln.resolve_name(pre.name_id(n)))] = {i.reads, i.defs, pre.kind(n), i.tmp, i.state, i.special, i.clock};
  }
  for (uint32_t s = 0; s < pre.num_stmts(); ++s) {
    const auto& si = pre.stmt(s);
    Prescan_report::Stmt st;
    st.dst         = si.dst == kNone ? std::string{} : std::string(ln.resolve_name(pre.name_id(si.dst)));
    st.op          = std::string(Lnast_ntype::to_sv(si.ltype));
    st.pinned      = si.pinned;
    st.w_seed      = si.w_seed;
    st.chain_inner = pre.chain_inner(s);
    st.dead        = si.dead;
    out.stmts.push_back(std::move(st));
  }
  out.prologue_last = pre.prologue_last() == kNone ? -1 : static_cast<int64_t>(pre.prologue_last());
  return out;
}

}  // namespace upass::aeg
