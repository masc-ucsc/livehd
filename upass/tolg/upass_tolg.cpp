//  This file is distributed under the BSD 3-Clause License. See LICENSE for
//  details.

#include "upass_tolg.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hlop/dlop.hpp"
#include "latch_contract.hpp"
#include "lnast_ntype.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "perf_tracing.hpp"  // TRACE_EVENT — no-op unless built with --define profiling=1
#include "split_selfref.hpp"

namespace {

using livehd::graph_util::create_const;
using livehd::graph_util::is_unsign;
using livehd::graph_util::set_bits;
using livehd::graph_util::set_sbits;
using livehd::graph_util::set_sign;
using livehd::graph_util::set_ubits;
using livehd::graph_util::set_unsign;
using livehd::graph_util::setup_sink_by_name;

using Pin      = hhds::Pin_class;
using WriteMap = absl::flat_hash_map<std::string, Pin>;

// Reserved clock/reset port-name recognition. Pyrope matches names
// case-sensitively, so these conventional signal names must match exactly (clk,
// RESET_N, … are all recognized). The `_n` suffix (active-low) folds too.
[[nodiscard]] inline bool is_clock_port_name(std::string_view n) { return (n == "clock") || (n == "clk"); }
[[nodiscard]] inline bool is_reset_port_name(std::string_view n) {
  return (n == "reset") || (n == "rst") || (n == "reset_n") || (n == "rst_n");
}

// One lowered value: its driver pin plus the literal container width `mw`.
// Despite the historical name, this is now the same unit as attrs::bits for
// both signed and unsigned values.
struct Val {
  Pin     pin;
  int32_t mw{0};
};

// Bits to represent a non-negative value as unsigned (>=1).
// Minimal literal width needed by a constant used in an inferred expression.
//
// A NEGATIVE value used to collapse to 1 here, whatever its magnitude, so every
// width computed from it was too small: `(-3) << ua` sized its result from
// mw(-3)==1 instead of 2 and produced a 9-bit intermediate for a shift that
// needs 10, silently wrapping `-3 << 7` from -384 to +128. Size a negative by
// its magnitude, exactly like a positive.
[[nodiscard]] int32_t mw_of_val(int64_t v) {
  if (v == 0) {
    return 1;
  }
  if (v < 0) {
    // Two's-complement signed width: -1 needs one bit, -3 needs three.
    return static_cast<int32_t>(std::bit_width(static_cast<uint64_t>(~v)) + 1U);
  }
  return std::max<int32_t>(1, static_cast<int32_t>(std::bit_width(static_cast<uint64_t>(v))));
}

// Literal width of a driver pin: its stamped bits, or — for a const pin
// (which carries no bits stamp) — the bits needed for its value. Used to size
// a merged mux/hotmux to the WIDEST arm so a narrow (e.g. const) arm does not
// truncate the wider ones. Returns 0 for an unstamped non-const pin.
[[nodiscard]] int32_t pin_mw_of(const Pin& p) {
  if (auto bb = livehd::graph_util::bits_of(p); bb > 0) {
    return bb;
  }
  if (livehd::graph_util::is_graph_input_pin(p) && p.get_graph() != nullptr) {
    if (const auto gio = p.get_graph()->get_io(); gio) {
      if (const auto bb = livehd::graph_util::bits_of(p, *gio, p.get_pin_name()); bb > 0) {
        return bb;
      }
    }
  }
  if (livehd::graph_util::is_const_pin(p)) {
    auto v = livehd::graph_util::hydrate_const(p);
    if (v.is_just_i64()) {
      return mw_of_val(v.to_just_i64());
    }
    // The literal PAYLOAD width, not Dlop's signed carrier: a non-negative
    // constant (an unsigned unknown such as `0ub?` included) carries one
    // leading zero beyond its payload, and that headroom must not widen a
    // Mux/Hotmux arm or an enclosing Concat lane. Shared with pass/cprop's
    // lossless-carrier rule and cgen_sim's mux-arm check.
    return livehd::graph_util::literal_payload_bits(v);
  }
  return 0;
}

// Can the value on this pin be NEGATIVE?
//
// An UNSIGNED hint proves non-negativity; a SIGNED pin may be negative. A
// CONSTANT pin carries no signed hint at all -- the same
// trap Cgen_verilog::operand_reads_signed documents -- so ask its VALUE instead
// of its stamp, or a literal `-2` arm reads as non-negative.
[[nodiscard]] bool pin_can_be_negative(const Pin& p) {
  if (p.is_invalid()) {
    return false;
  }
  if (livehd::graph_util::is_const_pin(p)) {
    auto v = livehd::graph_util::hydrate_const(p);
    return !v.has_unknowns() && v.is_negative();
  }
  return !is_unsign(p);
}

// Resolve a func_call callee name against the lnast registry the
// same way the runner's lookup_callee does: exact top-module-name match, else
// a UNIQUE "<module>.<name>" suffix match.
[[nodiscard]] std::shared_ptr<Lnast> resolve_callee_lnast(std::string_view                           name,
                                                          const std::vector<std::shared_ptr<Lnast>>& registry) {
  std::shared_ptr<Lnast> exact;
  std::shared_ptr<Lnast> suffix_hit;
  int                    suffix_matches = 0;
  const std::string      suffix         = "." + std::string(name);
  for (const auto& ln : registry) {
    if (!ln) {
      continue;
    }
    auto n = ln->get_top_module_name();
    if ((n == name)) {
      exact = ln;
    } else if (n.size() > suffix.size() && str_tools::ends_with(n, suffix)) {
      suffix_hit = ln;
      ++suffix_matches;
    }
  }
  if (exact) {
    return exact;
  }
  if (suffix_matches == 1) {
    return suffix_hit;
  }
  return nullptr;
}

// One pending time obligation: an asserted (min,max) landing
// interval on a value's driver pin (`is_sink=false`, from an undischarged
// `@[N]`) or on a GraphIO output sink (`is_sink=true`, the mod declared
// cycle / the pipe declared range). The combined checker verifies and REMOVES
// the paired pending_time attr; leftovers are compile errors.
struct Pending_rec {
  hhds::Pin_class pin;
  std::string     name;
  int64_t         min     = 0;
  int64_t         max     = 0;
  bool            is_sink = false;
};

// 2f-latch M9 — name the operation if this clock cone contains one that can
// never be a clock operator; "" when the cone is a legal clock.
//
// DENY-LIST, deliberately, not an allow-list. The allowed set is wider than it
// looks (`posedge ~clk` is legitimate and pinned PROVEN equal to `negedge clk`;
// `clk & en` is the ICG; a `Sub` output is an opaque gate or buffer cell; a
// flop-driven net is a DIVIDER, which the formal side refuses later with a
// better message than a compile error could give). So flag only what is
// UNAMBIGUOUS -- arithmetic on a clock, or a bitwise op that merges data INTO
// it -- and let everything else through. Guessing in the other direction is how
// a legal design gets a confident wrong rejection.
//
// Staging note: this is the narrow first slice of M9's "anything else on a
// clock is a compile error". Widening it is a breaking change and wants the R4
// corpus re-baselined first.
std::string_view illegal_clock_op(hhds::Pin_class d) {
  for (int hops = 0; hops < 8 && !d.is_invalid(); ++hops) {
    if (livehd::graph_util::is_graph_input_pin(d) || livehd::graph_util::is_const_pin(d)) {
      return {};
    }
    auto       n  = d.get_master_node();
    const auto op = livehd::graph_util::type_op_of(n);
    switch (op) {
      // Data merged into a clock, or arithmetic on one: never a clock operator.
      case Ntype_op::Or:
      case Ntype_op::Xor:
      case Ntype_op::Ror:
      case Ntype_op::Sum:
      case Ntype_op::Mult:
      case Ntype_op::Div:
      case Ntype_op::SHL:
      case Ntype_op::SRA:
      case Ntype_op::LT:
      case Ntype_op::GT:
      case Ntype_op::LUT:
      case Ntype_op::Hotmux  : return Ntype::get_name(op);
      // Identity / shaping wrappers a typed 1-bit read picks up: keep walking.
      case Ntype_op::Get_mask:
      case Ntype_op::Sext    :
      case Ntype_op::Not     : break;
      // And (the gate), Clock_cell, Sub (an opaque cell), Mux/EQ (boolean
      // shaping), a state element (a divider): all legal or handled later.
      default                : return {};
    }
    d = livehd::graph_util::first_value_driver(n);
  }
  return {};
}

// Shared phase-1 io+clock+reset GraphIO registration result. `clock_name` /
// `reset_name` are the graph inputs driving flop clock_pin / reset_pin (a
// declared input bound before minting, or the implicit "clock"/"reset"
// minted — the *_minted flags distinguish them so only minted pins get their
// width/sign stamped at first use). Empty names = the module needs none.
// `reset_neg` marks an active-low (…_n) module reset input.
struct Io_setup {
  std::string clock_name;
  bool        clock_minted = false;
  std::string reset_name;
  bool        reset_minted = false;
  bool        reset_neg    = false;
  std::string valid_name;
  bool        valid_minted = false;
  bool        valid_active = false;
};

// Builds one hhds::Graph from one post-upass / post-SSA function-tree Lnast.
class Tolg {
public:
  // `registry`/`lib` resolve pipe/mod call sites to Sub instances.
  // `async_default` is the upass.reset_style=async elaboration flag;
  // a per-reg `:[sync=…]` attr beats it.
  Tolg(const std::shared_ptr<Lnast>& lnast, hhds::Graph* g, Io_setup io_setup, const uPass_tolg::Registry* registry,
       hhds::GraphLibrary* lib, bool async_default)
      : lnast_(lnast)
      , g_(g)
      , registry_(registry)
      , lib_(lib)
      , clock_name_(std::move(io_setup.clock_name))
      , clock_minted_(io_setup.clock_minted)
      , reset_name_(std::move(io_setup.reset_name))
      , reset_minted_(io_setup.reset_minted)
      , reset_neg_(io_setup.reset_neg)
      , reset_async_default_(async_default)
      , valid_name_(std::move(io_setup.valid_name))
      , valid_minted_(io_setup.valid_minted)
      , valid_active_(io_setup.valid_active) {}

private:
  // Deferred stage-reg creation: a declare(reg)+stages does NOT
  // create the Flop immediately; the din store does, because only there the
  // effective depth is known (a Sub-fed stage reg realizes the DEFICIT
  // stage_N − callee_min; depth 0 = plain wire, no Flop at all). min/max ride
  // as raw const texts so "nil" stays distinguishable from 0.
  struct Pending_stage {
    std::string min_txt;
    std::string max_txt;
    Lnast_nid   decl_nid;        // for located diagnostics
    int32_t     decl_color = 0;  // block region at the declare (2opt-freq B)
  };

  // Per call-result name: the callee output's declared stages
  // interval + kind, recorded when the Sub is created and consumed by the
  // following stage-reg din store for the range check + deficit narrowing.
  struct Sub_out {
    int64_t          cmin    = 0;
    int64_t          cmax    = 0;  // pipe convention: 0 with cmin>=1 = unconstrained
    bool             is_pipe = false;
    hhds::Node_class node;  // to re-stamp time_range when stage[N] pins the pick
  };

public:
  void build() {
    index_mem_write_sites();
    // Module anchor: graph io nodes
    // cgen reads for the module header, at the unit's `mod`/`comb` declaration
    // (stamped on the LNAST root by func_extract / the specialize clone).
    if (const auto id = lnast_->get_srcid(lnast_->get_root()); id != hhds::SourceId_invalid) {
      cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
    }

    // Inputs: from io_meta(). LGraph values are signed, unbounded integers;
    // `unsign` records the non-negative range of an unsigned source port. The
    // physical W-bit -> non-negative boundary conversion belongs in each
    // backend, not in the graph as a Get_mask operation.
    for (const auto& e : lnast_->io_meta().inputs) {
      const std::string ename{canon_io_name(e.name)};    // strip slang's `` `ar.x` `` marker
      auto              raw = g_->get_input_pin(ename);  // body driver pin for the port
      if (cur_srcid_ != hhds::SourceId_invalid && !raw.is_invalid()) {
        raw.get_master_node().attr(hhds::attrs::srcid).set(cur_srcid_);
      }
      int32_t mw = io_mw(e);
      // A port's declared width is a real declared type (io_meta carries it
      // straight from the signature), so it can size a Concat lane. An
      // UNBOUNDED `int`/`unsigned` port has io_mw 0 and records nothing, which
      // is what makes `concat(unbounded_port, b)` the intended hard error.
      record_decl_type(e.name, e.kind == Io_kind::boolean ? int32_t{1} : mw, e.kind == Io_kind::boolean ? false : e.is_signed);
      if (e.kind == Io_kind::boolean) {
        set_ubits(raw, 1);
        record(e.name, raw, 1);
      } else if (mw <= 1) {
        set_bits(raw, 1);
        if (e.is_signed) {
          set_sign(raw);
          record(e.name, raw, 1);
        } else {
          set_unsign(raw);
          record(e.name, raw, 1);
        }
      } else if (e.is_signed) {
        set_bits(raw, mw);
        set_sign(raw);
        record(e.name, raw, mw);
      } else {
        // Stamp width and the non-negative range directly on the input pin.
        // Backends still know the GraphIO declaration is physically W bits.
        set_ubits(raw, mw);
        record(e.name, raw, mw);
      }
    }
    for (const auto& e : lnast_->io_meta().outputs) {
      // An OUTPUT port's declared width is a declared type just like an
      // input's, and it is the common destination of a concat (`z:u6 = ...`).
      // Recording it here rather than only at a `declare` is what lets
      // check_concat_dest_width see a signature-declared port at all.
      record_decl_type(e.name,
                       e.kind == Io_kind::boolean ? int32_t{1} : io_mw(e),
                       e.kind == Io_kind::boolean ? false : e.is_signed);
      if (cur_srcid_ != hhds::SourceId_invalid) {
        if (auto sink = g_->get_output_pin(canon_io_name(e.name)); !sink.is_invalid()) {
          sink.get_master_node().attr(hhds::attrs::srcid).set(cur_srcid_);
        }
      }
    }

    // Body: lower the `stmts` child of `top`.
    auto top = lnast_->get_root();
    for (auto c = lnast_->get_first_child(top); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (Lnast_ntype::is_stmts(lnast_->get_type(c))) {
        lower_stmts(c);
      }
    }
    // Walk done: drop the statement anchor so finalize-time diagnostics are
    // unlocated rather than mislocated at whatever statement came last
    // (finalize_regs / create_stage_flop re-anchor per entity).
    cur_srcid_ = hhds::SourceId_invalid;
    cur_color_ = 0;

    // Wire every declared reg's din/enable/reset/initial now that
    // all stores and per-reg attr overrides have been seen.
    finalize_regs();
    cur_color_ = 0;  // the last reg's region must not leak into mem/output glue
    // Sanity-check the per-memory port allocation.
    finalize_mems();
    // Bind any deferred field reads (forward references to a call result
    // lowered later) now that every call's Sub result exists.
    resolve_pending_tgets();
    // 2c-wire — wire each `wire` net's buffer input to its single accumulated
    // driver (position-independent reads already bind to the buffer output),
    // and enforce the single-driver / undriven rules. Runs after finalize_regs
    // so a `reset_pin = <wire>` resolves the wire's buffer pin, and after
    // resolve_pending_tgets so a driver that reads a forward call result is
    // bound first.
    finalize_wires();
    cur_color_ = 0;  // the last wire's region must not leak into output glue

    // Outputs: connect each output's bound driver to its graph output sink. The
    // GraphIO carries the port widths cgen emits; fetch it so an unbounded
    // output can be sized from its (now-lowered) driver below.
    auto out_gio = lib_ != nullptr ? lib_->find_io(std::string(lnast_->get_graph_name())) : nullptr;
    for (const auto& e : lnast_->io_meta().outputs) {
      const std::string ename{canon_io_name(e.name)};  // strip slang's `` `p.q` `` marker
      auto              sink = g_->get_output_pin(ename);
      if (sink.is_invalid()) {
        continue;
      }
      auto it = pin_map_.find(ename);
      if (it == pin_map_.end()) {
        // Every declared output must be assigned. A Verilog-origin module's
        // legally-undriven output (defaults to X) is already poison-inited to
        // `0sb?` at body top by inou.slang, so it never reaches here — reaching
        // here now genuinely means a Pyrope output the body forgot to drive.
        error_at(Lnast_nid{},
                 {"undriven-output", "type"},
                 "output '{}' is never driven by the body of '{}' — every "
                 "declared output must be assigned",
                 e.name,
                 lnast_->get_top_module_name());
        continue;
      }
      sink.connect_driver(it->second);
      // An UNBOUNDED output (`int`/`unsigned`, no declared width — io_meta bits
      // == 0) takes its driver's width+sign: 07-typesystem says an
      // unconstrained signal "uses whatever current value is found". Without
      // this the GraphIO port kept the setup_io_impl default of 1 bit (io_meta
      // e.bits==0) and the value was TRUNCATED to a single bit (e.g. `out:int =
      // int(c)` for a wider `c`, or `out:int = a + 1`). The port width cgen
      // emits lives on the GraphIO (set in setup_io_impl, before the body — and
      // the driver width is only known now), so restamp it there. A declared
      // width (uN/sN, or a bounded `int(max=…)`) keeps e.bits>0 and is left
      // untouched — the constraint is the contract.
      if (e.kind != Io_kind::boolean && e.bits == 0 && out_gio != nullptr) {
        const bool uns   = livehd::graph_util::is_unsign(it->second);
        int32_t    dbits = livehd::graph_util::bits_of(it->second);
        if (dbits <= 0) {
          // A const driver carries no `bits` attr; size from the constant's own
          // width (the width cgen emits for the literal) so `out:int = 300` is
          // not squeezed into a single bit.
          if (livehd::graph_util::is_const_pin(it->second)) {
            dbits = livehd::graph_util::hydrate_const(it->second).get_bits();
          } else {
            dbits = mw_lookup(e.name);
          }
        }
        if (dbits > 0) {
          // The output port's width+sign live on the GraphIO (authoritative;
          // the bits_of(pin, gio, name) overload falls back to it).
          // `bits`/`signed` are DRIVER-pin properties, so do NOT stamp them on
          // this output-port SINK — its width is its driver's, read through the
          // driver (see node_util.hpp set_bits, which now asserts driver-only).
          out_gio->set_bits(ename, static_cast<uint32_t>(dbits));
          out_gio->set_unsign(ename, uns);
        }
      }
    }

    // Lower the declared per-output intervals as pendings.
    stamp_output_pendings();

    // Guard — a stage declare whose din store never arrived would
    // silently drop the delay (and the value): hard error, never nil.
    if (!pending_stage_.empty()) {
      error_at(pending_stage_.begin()->second.decl_nid,
               "upass.tolg: stage reg '{}' in '{}' was declared but never "
               "stored — its delay would be silently lost",
               pending_stage_.begin()->first,
               lnast_->get_top_module_name());
    }

    // Persist the block-attribute regions (2opt-freq B): the coloring_info
    // JSON's "region_opts" member is what pass.abc reads for per-region ABC
    // options; the node colors themselves were stamped by make_node.
    write_region_info();
  }

private:
  // ── width / value helpers
  // ───────────────────────────────────────────────────

  [[nodiscard]] static int32_t io_mw(const Lnast_io_entry& e) {
    if (e.kind == Io_kind::boolean) {
      return 1;
    }
    return e.bits > 0 ? static_cast<int32_t>(e.bits) : int32_t{1};
  }

  [[nodiscard]] Pin nil_pin() { return create_const(*g_, *Dlop::from_pyrope("0sb?")); }

  // Backtick is LiveHD's general quoted-string IDENTIFIER syntax: `` `id` `` is
  // a literal id whose content is any character (a literal backtick rides as
  // `\`). It is the LNAST analogue of a Verilog escaped id `\id ` — slang emits
  // it for
  // `\ar.x ` so `.x` is not read as a tuple field access through upass. By tolg
  // names are flat strings (no field-access parsing), so the quoting is no
  // longer needed: recover the bare content as the canonical lg signal/IO name.
  // The yosys-verilog and Pyrope readers already emit the bare `ar.x`, so
  // unquoting makes the name identical across readers — without it a
  // cross-reader LEC sees
  // `` `ar.x` `` vs `ar.x` as two unrelated free inputs and falsely refutes.
  // cgen re-escapes by content (the `.`), so the marker is redundant
  // downstream. EXCEPTION: a content with WHITESPACE cannot be a bare lg name
  // (library.txt is whitespace-delimited), so keep it quoted — those rare ids
  // stay `` `a b` ``.
  [[nodiscard]] static std::string_view canon_io_name(std::string_view name) {
    if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
      auto inner = name.substr(1, name.size() - 2);
      for (const char c : inner) {
        if (std::isspace(static_cast<unsigned char>(c))) {
          return name;  // genuinely needs quoting (whitespace) — leave as-is
        }
      }
      return inner;
    }
    return name;
  }

  void record(std::string_view name_in, const Pin& pin, int32_t mw) {
    std::string_view name = canon_io_name(name_in);
    std::string      key{name};
    if (!branch_writes_.empty()) {
      // First write to `key` in this branch: remember how to undo it on branch
      // exit -- restore the pre-branch value, or erase if the name was absent.
      // Capturing lazily here is O(writes); lower_branch used to snapshot the
      // whole pin_map_ per branch, which is O(pin_map_) and turns quadratic on
      // if/elif-heavy designs (e.g. firtool mux chains) -- a deep hang.
      if (auto [it, inserted] = branch_writes_.back().try_emplace(key, pin); inserted) {
        auto pit = pin_map_.find(key);
        auto mit = mw_map_.find(key);
        branch_restore_.back().emplace(key,
                                       Branch_restore{pit != pin_map_.end() ? std::optional<Pin>{pit->second} : std::nullopt,
                                                      mit != mw_map_.end() ? std::optional<int32_t>{mit->second} : std::nullopt});
      } else {
        it->second = pin;  // keep the branch's latest value for the merge
      }
    }
    pin_map_[key] = pin;
    mw_map_[key]  = mw;
    // Track the LOGICAL variable's most-recent driver (SSA versions collapse to
    // the root) for derived reset_pin/clock_pin resolution and the wire buffer.
    // Shadow keys (\x01din:/\x01en:) are not user names, so skip them.
    if (!key.empty() && key.front() != '\x01') {
      const auto pos                                                     = key.find("___ssa_");
      logical_last_[pos == std::string::npos ? key : key.substr(0, pos)] = {pin, mw};
    }
  }

  [[nodiscard]] int32_t mw_lookup(std::string_view name) {
    auto it = mw_map_.find(std::string{canon_io_name(name)});
    return it != mw_map_.end() ? it->second : int32_t{1};
  }

  // The LOGICAL variable behind a (possibly SSA-versioned, possibly
  // backtick-marked) LNAST name -- the key decl_type_ uses, because a type is
  // declared once on the base name while every read is a fresh SSA version.
  [[nodiscard]] static std::string logical_key(std::string_view name) {
    std::string k{canon_io_name(name)};
    if (auto p = k.find("___ssa_"); p != std::string::npos) {
      k.resize(p);
    }
    return k;
  }

  // Declared width + signedness of one name (see decl_type_ below). Defined
  // here, not with the member, because a nested type must be declared before
  // the member functions whose SIGNATURE names it.
  struct Decl_type {
    int32_t mw{0};
    bool    is_signed{false};
  };

  void record_decl_type(std::string_view name, int32_t mw, bool is_signed) {
    if (mw <= 0) {
      return;  // untyped / unbounded: nothing declared to remember
    }
    decl_type_[logical_key(name)] = Decl_type{mw, is_signed};
  }

  [[nodiscard]] std::optional<Decl_type> decl_type_lookup(std::string_view name) const {
    auto it = decl_type_.find(logical_key(name));
    if (it == decl_type_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  [[nodiscard]] Pin resolve(std::string_view name_in) {
    std::string_view name = canon_io_name(name_in);
    std::string      key{name};
    auto             it = pin_map_.find(key);
    if (it != pin_map_.end()) {
      return it->second;
    }
    // Whole-array read (`x = mem`): materialize the cell's async read_all
    // output.
    if (auto mit = mem_map_.find(key); mit != mem_map_.end()) {
      return get_or_make_read_all(mit->second);
    }
    // A name that resolves to neither a driver nor a memory would be wired to
    // nil (0sb?). For Pyrope that drops whatever the reference carried — a
    // silent miscompile, so it is a hard error. A Verilog-origin module may
    // legally read an undriven wire (it defaults to X), so keep the warn + nil.
    const bool strict = !lnast_->is_verilog_origin();
    if (strict) {
      error_here(
          "upass.tolg: unresolved reference '{}' — it has no driver "
          "(often an unassigned variable or a value that "
          "resolved to nil)",
          name);
    } else {
      warn_at(Lnast_nid{}, {"unresolved-ref", "name"}, "unresolved ref '{}' — wiring nil (0sb?)", name);
    }
    auto p        = nil_pin();
    pin_map_[key] = p;
    mw_map_[key]  = 1;
    return p;
  }

  [[nodiscard]] Val leaf(const Lnast_nid& nid) {
    if (Lnast_ntype::is_const(lnast_->get_type(nid))) {
      auto    c  = Dlop::from_pyrope(lnast_->get_name(nid));
      int32_t mw = c->is_just_i64() ? mw_of_val(c->to_just_i64()) : std::max<int32_t>(1, static_cast<int32_t>(c->get_bits()));
      return {create_const(*g_, *c), mw};
    }
    auto name = lnast_->get_name(nid);
    return {resolve(name), mw_lookup(name)};
  }

  // Bind a computed result using the literal unsigned-width contract.
  void bind_result(std::string_view name, const Pin& drv, int32_t mw) {
    int32_t m = mw > 0 ? mw : int32_t{1};
    set_ubits(drv, m);
    record(name, drv, m);
  }

  // ── statement / node dispatch
  // ───────────────────────────────────────────────

  void lower_stmts(const Lnast_nid& stmts) {
    // A `__region` marker inside this stmts sets cur_color_ for the REST of
    // the block; restoring here bounds the region to its block (nested blocks
    // override and restore, if/match arms inherit the enclosing color).
    const auto saved_color = cur_color_;
    for (auto c = lnast_->get_first_child(stmts); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (lnast_->is_dce_dead(c)) {
        continue;  // dce:mark (lg-only flows): a dead statement is skipped here
                   // instead of the runner rebuilding the whole staging tree
      }
      lower_node(c);
    }
    cur_color_ = saved_color;
  }

  // The current statement's SourceId, re-minted into the graph's
  // locator. Every cell make_node creates while lowering this statement is
  // stamped with it, so LGraph nodes resolve back to Pyrope source.
  hhds::SourceId cur_srcid_{0};

  // Block-scoped synthesis region (2opt-freq B): the active `{ ::[abc=…,
  // color=…] }` region id, set by the block's `__region` marker and restored
  // at the enclosing stmts' exit; every node make_node mints while it is
  // non-zero gets livehd::attrs::color, which pass.partition/pass.abc turn
  // into a per-region mapping unit. region_abc_ collects the per-color ABC
  // flow payloads for the coloring_info "region_opts" member.
  int32_t                        cur_color_ = 0;
  std::map<int32_t, std::string> region_abc_;
  absl::flat_hash_set<int32_t>   region_colors_marked_;
  absl::flat_hash_set<int32_t>   region_colors_stamped_;

  // Anchor priority shared by error_at/warn_at: the given nid's SourceId,
  // falling back to the current statement's (re-minted into the graph).
  [[nodiscard]] livehd::diag::Diagnostic locate_record(const Lnast_nid& nid, livehd::diag::Severity sev, std::string_view code,
                                                       std::string_view category, std::string msg) const {
    livehd::diag::Span              span;
    std::vector<livehd::diag::Note> notes;
    if (!nid.is_invalid() && lnast_) {
      span  = lnast_->span_of(nid);
      notes = lnast_->notes_of(nid, "reached via this site");
    }
    if (span.is_null() && g_ != nullptr) {
      const auto rs = g_->source_locator().resolve_spans(cur_srcid_);
      span          = rs.primary;
      notes         = livehd::diag::notes_from(rs, "reached via this site");
    }
    return livehd::diag::Diagnostic{
        .severity = sev,
        .code     = std::string(code),
        .category = std::string(category),
        .pass     = "upass.tolg",
        .message  = std::move(msg),
        .span     = std::move(span),
        .notes    = std::move(notes),
    };
  }

  // Stage a located Diagnostic, then throw (Pass::error semantics — the
  // downstream flush seam emits the staged record exactly once, so the error
  // carries a resolved span instead of no location).
  template <typename... Args>
  [[noreturn]] void error_at(const Lnast_nid& nid, livehd::diag::Id id, std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::format(fmt, std::forward<Args>(args)...);
    livehd::diag::sink().stage(locate_record(nid, livehd::diag::Severity::error, id.code, id.category, msg));
    throw Eprp::parser_error(Pass::eprp, msg);
  }

  template <typename... Args>
  [[noreturn]] void error_at(const Lnast_nid& nid, std::format_string<Args...> fmt, Args&&... args) {
    error_at(nid, livehd::diag::Id{"tolg-error", "type"}, "{}", std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args>
  [[noreturn]] void error_here(std::format_string<Args...> fmt, Args&&... args) {
    error_at(Lnast_nid{}, "{}", std::format(fmt, std::forward<Args>(args)...));
  }

  // Non-fatal sibling: emit a located warning and continue lowering.
  template <typename... Args>
  void warn_at(const Lnast_nid& nid, livehd::diag::Id id, std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::format(fmt, std::forward<Args>(args)...);
    livehd::diag::sink().emit(locate_record(nid, livehd::diag::Severity::warning, id.code, id.category, std::move(msg)));
  }

  template <typename... Args>
  hhds::Node_class make_node(Args&&... args) {
    auto n = livehd::graph_util::create_typed_node(*g_, std::forward<Args>(args)...);
    if (cur_srcid_ != hhds::SourceId_invalid) {
      n.attr(hhds::attrs::srcid).set(cur_srcid_);
    }
    if (cur_color_ != 0) {
      livehd::graph_util::set_color(n, cur_color_);
      region_colors_stamped_.insert(cur_color_);
    }
    return n;
  }

  void lower_node(const Lnast_nid& nid) {
    const auto t           = lnast_->get_type(nid);
    // Anchor the statement: nested lower_* calls (and the cells they mint)
    // inherit it; statements without an id keep the enclosing one.
    const auto saved_srcid = cur_srcid_;
    if (const auto id = lnast_->get_srcid(nid); id != hhds::SourceId_invalid) {
      cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
    }
    lower_node_dispatch(nid, t);
    cur_srcid_ = saved_srcid;
  }

  void lower_node_dispatch(const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int t) {
    using N = Lnast_ntype;
    if (N::is_stmts(t)) {
      lower_stmts(nid);
    } else if (N::is_if(t)) {
      lower_if(nid);
    } else if (N::is_unique_if(t)) {
      lower_if(nid, /*unique=*/true);
    } else if (N::is_store(t)) {
      lower_store(nid);
    } else if (N::is_declare(t)) {
      lower_declare(nid);
    } else if (N::is_range(t)) {
      lower_range(nid);
    } else if (N::is_get_mask(t)) {
      lower_get_mask(nid);
    } else if (N::is_set_mask(t)) {
      lower_set_mask(nid);
    } else if (N::is_plus(t)) {
      lower_op(nid, Ntype_op::Sum, true, OpW::add);
    } else if (N::is_minus(t)) {
      lower_op(nid, Ntype_op::Sum, false, OpW::add);
    } else if (N::is_mult(t)) {
      lower_op(nid, Ntype_op::Mult, true, OpW::mul);
    } else if (N::is_bit_and(t) || N::is_log_and(t)) {
      lower_op(nid, Ntype_op::And, true, OpW::andw);
    } else if (N::is_bit_or(t) || N::is_log_or(t)) {
      lower_op(nid, Ntype_op::Or, true, OpW::maxw);
    } else if (N::is_bit_xor(t)) {
      lower_op(nid, Ntype_op::Xor, true, OpW::maxw);
    } else if (N::is_eq(t)) {
      lower_op(nid, Ntype_op::EQ, true, OpW::boolw);
    } else if (N::is_lt(t)) {
      lower_op(nid, Ntype_op::LT, false, OpW::boolw);
    } else if (N::is_gt(t)) {
      lower_op(nid, Ntype_op::GT, false, OpW::boolw);
    } else if (N::is_shl(t)) {
      lower_op(nid, Ntype_op::SHL, false, OpW::shlw);
    } else if (N::is_sra(t)) {
      lower_op(nid, Ntype_op::SRA, false, OpW::firstw);
    } else if (N::is_concat(t)) {
      lower_concat(nid);
    } else if (N::is_sext(t)) {
      lower_sext(nid);
    } else if (N::is_red_or(t)) {
      lower_red_or(nid);
    } else if (N::is_red_and(t)) {
      lower_red_and(nid);
    } else if (N::is_red_xor(t)) {
      lower_red_xor(nid);
    } else if (N::is_popcount(t)) {
      lower_popcount(nid);
    } else if (N::is_div(t)) {
      lower_op(nid, Ntype_op::Div, false, OpW::firstw);
    } else if (N::is_mod(t)) {
      // `a % b` has no general hardware lowering; lower_mod handles only the
      // easy/unambiguous cases (power-of-two, range-fit, `% 3`) to shift/mask
      // and HARD-errors the rest.
      lower_mod(nid);
    } else if (N::is_bit_not(t)) {
      lower_unary(nid, Ntype_op::Not);
    } else if (N::is_log_not(t)) {
      lower_log_not(nid);
    } else if (N::is_ne(t)) {
      lower_negated(nid, Ntype_op::EQ, true);
    } else if (N::is_le(t)) {
      lower_negated(nid, Ntype_op::GT, false);
    } else if (N::is_ge(t)) {
      lower_negated(nid, Ntype_op::LT, false);
    } else if (N::is_timecheck(t)) {
      // An `x@[N]` record the LNAST discharge could not decide
      // ("checked"-marked ones are already done): lower it to a PENDING
      // time-check attr on the named value's driver pin. The combined checker
      // verifies and removes it; a leftover pending is a compile error.
      lower_timecheck(nid);
    } else if (N::is_func_call(t)) {
      // Pipe/mod call sites lower to Ntype_op::Sub instances;
      // anything unresolvable (runtime wrap/sat, comb recursion) stays a
      // HARD error inside lower_func_call.
      lower_func_call(nid);
    } else if (N::is_rolled_for(t)) {
      lower_rolled_for(nid);
    } else if (N::is_attr_get(t)) {
      // Every attribute read folds in upass.attributes before tolg; one that
      // survives has no hardware lowering — a hard error inside.
      lower_attr_get(nid);
    } else if (N::is_attr_set(t)) {
      // Per-reg flop-attr overrides (reset_pin/sync/negreset/
      // initial); anything else keeps the unhandled warn below.
      lower_attr_set(nid);
    } else if (N::is_tuple_get(t)) {
      // An indexed read of a declared memory becomes a read port;
      // any other surviving tuple_get keeps the unhandled warn inside.
      lower_tuple_get(nid);
    } else if (N::is_tuple_add(t)) {
      // An all-const tuple literal is recorded as a potential array
      // initializer; anything else keeps the unhandled warn inside.
      lower_tuple_add(nid);
    } else if (N::is_tuple_concat(t)) {
      // `...` splice / `++` residue: comptime bookkeeping (the runner already
      // folded the spliced field wires upstream). Record the merged tuple.
      lower_tuple_concat(nid);
    } else if (N::is_for(t)) {
      // A `for` node reaching tolg means uPass_runner::unroll_for
      // could NOT unroll it: the iterable resolved to neither a comptime range
      // nor a known tuple shape. Pyrope `for` is comptime-only (must fully
      // unroll), so this is a user error (a runtime/unknown iterable), not a
      // silent miscompile. HARD error rather than the unhandled warn below.
      error_here(
          "upass.tolg: non-comptime `for` loop in '{}' — the iterable "
          "did not resolve to a comptime range "
          "or tuple, so the loop could not unroll (Pyrope for-loops are "
          "comptime-only and must fully unroll)",
          lnast_->get_top_module_name());
    } else if (N::is_type_spec(t)) {
      // Pure annotation, no datapath (the comb inliner's emit_inline_typespec,
      // or a folded type check). No hardware — skip silently.
    } else if (N::is_cassert(t)) {
      // A cassert upass.verifier could NOT discharge at comptime (unknown cond)
      // survives here. Materialize it as an `fproperty` Sub so pass.formal can
      // try to prove it and cgen can emit a runtime check for whatever is left.
      lower_cassert(nid);
    } else {
      // Any other node type reaching tolg has no LGraph lowering and would be
      // silently dropped (→ undriven wires / nil). That is a miscompile, so it
      // is a hard error, never a warning on an otherwise-"passing" run.
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: node type '{}' has no hardware lowering — it "
               "survived elaboration but cannot be turned "
               "into a netlist (this is usually an unresolved value or an "
               "unsupported runtime construct)",
               Lnast_ntype::to_sv(t));
    }
  }

  // Every attribute read folds during elaboration (upass.attributes); one that
  // survives to tolg has no hardware lowering, so it is a hard error.
  void lower_attr_get(const Lnast_nid& nid) {
    auto              dst       = lnast_->get_first_child(nid);
    auto              base      = dst.is_invalid() ? dst : lnast_->get_sibling_next(dst);
    auto              attr      = base.is_invalid() ? base : lnast_->get_sibling_next(base);
    const std::string attr_name = attr.is_invalid() ? std::string{} : std::string(lnast_->get_name(attr));
    error_at(nid,
             {"unhandled-node", "unsupported"},
             "upass.tolg: attribute read '.[{}]' has no hardware lowering — it "
             "should have folded during elaboration",
             attr_name);
  }

  // Re-resolve each deferred field read once the whole body (incl. later
  // calls) has lowered: by now the source name is a known Sub result / memory,
  // so lower_tuple_get binds the port driver. tget_final_ makes a genuinely
  // unresolvable one warn rather than defer again.
  void resolve_pending_tgets() {
    tget_final_ = true;
    for (const auto& nid : pending_tgets_) {
      lower_tuple_get(nid);
    }
    pending_tgets_.clear();
  }

  // ── 2c-wire — single-driver combinational nets
  // ────────────────────────────── A `wire` declares a passthrough buffer (Or)
  // whose OUTPUT every read binds to (record() at declare →
  // position-independent: a read before the driver appears textually still sees
  // the buffer), and whose INPUT is connected as soon as the single accumulated
  // driver is complete (the din shadow the branch-mux machinery merges, exactly
  // like a reg's din — but with no flop). The buffer is a transparent net, so
  // the time-checker's SCC sees through it: a real comb loop is an error; a
  // ring is legal only when a register breaks it.

  // The single-driver rule is enforced in the FRONTEND (prp2lnast
  // check_wire_drivers) on the pre-elaborate tree, before lnastfmt drops a dead
  // first write (which would hide a double-drive). COVERAGE is NOT a rule: one
  // driver may be conditional, and the merges below fill the unwritten paths
  // with the written value (see is_wire_din). tolg only wires the net and lets
  // the time-checker flag a real comb loop.

  // Wire every declared wire's buffer input to its single accumulated driver
  // (the din shadow the branch-mux machinery merged). The single-driver rule is
  // enforced in the frontend; here a missing driver only survives for a Verilog
  // net (legal X) or a loop-built Pyrope wire the frontend skipped — wire it to
  // nil rather than miscompile.
  void finalize_wires() {
    const bool verilog = lnast_->is_verilog_origin();
    for (const auto& name : wire_order_) {
      auto& info = wire_info_.at(name);

      // Anchor this wire's diagnostics at its declaration.
      cur_srcid_ = hhds::SourceId_invalid;
      if (const auto id = lnast_->get_srcid(info.decl_nid); id != hhds::SourceId_invalid) {
        cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
      }
      cur_color_ = info.decl_color;  // finalize glue lands in the wire's region

      // Wire the buffer input to the accumulated single driver, restamping the
      // buffer output width from the driver when the wire was untyped.
      Pin     din;
      int32_t mw     = info.decl_mw;
      bool    driven = false;
      if (auto dit = pin_map_.find(din_key(name)); dit != pin_map_.end()) {
        din    = dit->second;
        driven = true;
        if (mw <= 0) {
          mw = mw_lookup(din_key(name));
        }
      } else {
        if (!verilog) {
          error_here(
              "upass.tolg: wire '{}' is never driven in '{}' — a `wire` "
              "must have exactly one driver",
              name,
              lnast_->get_top_module_name());
        }
        din = nil_pin();  // Verilog net defaults to X; a loop-built undriven
                          // wire falls here too
        if (mw <= 0) {
          mw = 1;
        }
      }
      // An UNDRIVEN net still has to be wired and sized: leaving the passthrough
      // Or with no `as` input ships a dangling cell (a Verilog net legally
      // defaults to X, so `verilog` above only suppresses the diagnostic, not
      // the lowering). There is no driver to truncate in that case, so skip the
      // typed Get_mask -- `nil` is already the declared width's don't-care.
      if (!info.bound) {
        bind_wire_driver(name, din, mw, /*narrow_typed=*/driven);
      }
      resolve_wire_selfref(name);
    }
  }

  // Resolve a packed self-reference once the wire's driver is COMPLETE. Every
  // write has landed by now (finalize_wires runs after finalize_regs/_mems and
  // resolve_pending_tgets), so the splitter sees the whole accumulator instead
  // of a partial one, and a residual dependency is a genuine loop.
  void resolve_wire_selfref(std::string_view name) {
    auto it = wire_info_.find(std::string{name});
    if (it == wire_info_.end()) {
      return;
    }
    auto& info = it->second;
    if (info.bound_din.is_invalid() || info.early_readers.empty()) {
      return;
    }
    livehd::graph_util::split_packed_selfref_wire(g_, info.buf, info.bound_din, info.early_readers);
    if (!lnast_->is_verilog_origin() && livehd::graph_util::comb_pin_depends_on(info.bound_din, info.buf)) {
      // Lead with the established `combinational loop` vocabulary -- the same
      // words the time-checker's SCC uses at the end of this file. This IS
      // one; it is simply caught earlier and with a better source anchor.
      // //inou/prp:prp-wire_comb_loop matches the diagnostic on that phrase,
      // and the wire-only wording silently stopped matching it.
      error_here(
          "upass.tolg: combinational loop through wire '{}' in '{}' — its driver depends on itself and no disjoint "
          "packed-slice split can break it",
          name,
          lnast_->get_top_module_name());
    }
  }

  // Attach one wire's effective driver as soon as it is known. This must not
  // wait for end-of-module finalization: a later wire write may read this net,
  // and that later edge is the one that closes a multi-wire dependency.
  //
  // REBINDS. A wire assembled from several partial writes (`w#[3:0] = a;
  // w#[7:4] = b`) reaches here once per write, and lower_set_mask chains each
  // write onto the din accumulator -- so the LATEST driver is the complete one
  // and every earlier bind must be undone. Keeping the first bind (an early
  // `bound` return) silently dropped every write after the first.
  void bind_wire_driver(std::string_view name, Pin din, int32_t mw, bool narrow_typed = true) {
    auto it = wire_info_.find(std::string{name});
    if (it == wire_info_.end()) {
      return;
    }
    auto& info = it->second;

    if (info.bound) {
      // Drop the previous buffer input first: a second connect_driver on the
      // same `as` sink would make the passthrough Or a two-input OR of the
      // partial and the complete value.
      auto as_sink = livehd::graph_util::find_sink_pin(info.buf, "as");
      if (!as_sink.is_invalid()) {
        as_sink.del_sink();
      }
      if (!info.narrow.is_invalid() && !info.narrow.has_out_edges()) {
        info.narrow.del_node();  // the superseded typed-wire truncation cell
      }
      info.narrow = hhds::Node_class{};
    }

    // The consumers that exist NOW are the ones that can participate in this
    // net's definition: a genuine position-independent read, or a read taken
    // inside the branch bodies whose merge produced `din`. Captured here rather
    // than at each store so EVERY bind path (plain store, set_mask chain,
    // if/match merge, finalize) is covered by the self-reference resolution
    // below -- a merge-bound wire used to skip it entirely.
    capture_wire_readers(name);

    // A TYPED wire narrows its driver with a real precision-changing cell.
    if (narrow_typed && info.decl_mw > 0) {
      auto gm = make_node(Ntype_op::Get_mask);
      setup_sink_by_name(gm, "a").connect_driver(din);
      setup_sink_by_name(gm, "mask").connect_driver(create_const(*g_, *Dlop::get_mask_value(info.decl_mw)));
      auto gm_out = gm.create_driver_pin(0);
      if (info.is_signed) {
        set_sbits(gm_out, info.decl_mw);
      } else {
        set_ubits(gm_out, info.decl_mw);
      }
      din         = gm_out;
      info.narrow = gm;
    }

    setup_sink_by_name(info.buf, "as").connect_driver(din);
    // Record the driver; do NOT split here. A wire assembled from several
    // partial writes rebinds once per write, and splitting against a PARTIAL
    // accumulator resolves an early slice read onto the `0sb?` seed instead of
    // the lane's real driver -- permanently, because the split rewires the
    // reader off the buffer and the next bind then sees no early reader at all
    // (`w#[0..=3] = a ^ hi` before `w#[4..=7] = b` froze `hi` at don't-care,
    // while the same two writes in the opposite order were correct -- a `wire`
    // is a net, so write order must not change its value). resolve_wire_selfref
    // runs the split once, from finalize_wires, against the COMPLETE driver.
    info.bound_din = din;

    if (info.decl_mw <= 0) {
      const int32_t dbits = livehd::graph_util::bits_of(din);
      if (dbits > 0) {
        set_bits(info.out, dbits);
        if (livehd::graph_util::is_unsign(din)) {
          set_unsign(info.out);
        } else {
          set_sign(info.out);
        }
        mw = dbits;
      } else {
        set_ubits(info.out, mw);
      }
      mw_map_[std::string{name}] = mw;
    }
    info.bound = true;
  }

  void maybe_bind_wire_shadow(std::string_view shadow, const Pin& driver, int32_t mw) {
    if (!branch_writes_.empty() || !is_wire_din(shadow)) {
      return;  // inside a branch the merge is not complete yet
    }
    bind_wire_driver(shadow.substr(kDinPrefix.size()), driver, mw);
  }

  // Accumulate the consumers that exist at this wire's binds. There is no
  // reason to track reads taken after the LAST write: they are downstream of
  // the completed net and cannot participate in its definition. Called from
  // bind_wire_driver ONLY, right before the buffer input is attached, and it
  // UNIONS rather than rebuilds -- a partial-write rebind must not forget a
  // reader that the earlier write already saw.
  void capture_wire_readers(std::string_view name) {
    auto it = wire_info_.find(std::string{name});
    if (it == wire_info_.end()) {
      return;
    }
    auto& readers = it->second.early_readers;
    for (const auto& e : it->second.out.out_edges()) {
      auto reader = e.sink.get_master_node();
      if (reader.is_invalid() || reader == it->second.buf) {
        continue;
      }
      if (std::ranges::find(readers, reader) == readers.end()) {
        readers.push_back(reader);
      }
    }
  }

  // attr_set(ref(target), const(key), value) — record the per-reg
  // flop-attr overrides consumed by finalize_regs. `:[reset_pin=…, sync=…,
  // negreset, initial=N]` (04b-attributes.md); a per-reg `sync` beats the
  // upass.reset_style flag; `reset_pin=false` opts out of reset (only valid
  // with a nil init).
  void lower_attr_set(const Lnast_nid& nid) {
    auto tgt = lnast_->get_first_child(nid);
    if (tgt.is_invalid()) {
      return;
    }
    auto key_n = lnast_->get_sibling_next(tgt);
    if (key_n.is_invalid()) {
      return;
    }
    if (lnast_->get_name(key_n) == "__region") {
      // Synthesis-region marker (2opt-freq B): attr_set(%__region_<id>,
      // "__region", <abc-string | true>) — first statement of an annotated
      // `{ ::[…] … }` block. The region id rides the target name; a quoted
      // value is the per-region ABC flow payload. lower_stmts restores
      // cur_color_ at the block's exit.
      constexpr std::string_view kPrefix = "%__region_";
      auto                       tname   = lnast_->get_name(tgt);
      int32_t                    id      = 0;
      if (tname.size() > kPrefix.size() && tname.substr(0, kPrefix.size()) == kPrefix) {
        auto ds = tname.substr(kPrefix.size());
        std::from_chars(ds.data(), ds.data() + ds.size(), id);
      }
      if (id <= 0) {
        warn_at(tgt, {"region-marker-malformed", "internal"}, "malformed __region marker target '{}' (compiler bug?)", tname);
        return;
      }
      cur_color_ = id;
      region_colors_marked_.insert(id);
      if (auto val_n = lnast_->get_sibling_next(key_n); !val_n.is_invalid()) {
        std::string_view val = lnast_->get_name(val_n);
        if (val.size() >= 2 && ((val.front() == '\'' && val.back() == '\'') || (val.front() == '"' && val.back() == '"'))) {
          val                  = val.substr(1, val.size() - 2);
          auto [ait, inserted] = region_abc_.try_emplace(id, std::string(val));
          if (!inserted && ait->second != val) {
            warn_at(tgt,
                    {"region-abc-conflict", "unsupported"},
                    "region color {} carries conflicting abc= options; keeping "
                    "the first",
                    id);
          }
        }
      }
      return;
    }
    auto it = reg_info_.find(std::string(lnast_->get_name(tgt)));
    if (it == reg_info_.end()) {
      // Not a flop reg (yet): stash for a later array/memory declare (the
      // importer emits the attr_set before the declare it qualifies).
      auto key_sv = lnast_->get_name(key_n);
      auto val_n0 = lnast_->get_sibling_next(key_n);
      auto val_sv = val_n0.is_invalid() ? std::string_view{"true"} : std::string_view(lnast_->get_name(val_n0));
      pending_attrs_[std::string(lnast_->get_name(tgt))][std::string(key_sv)] = std::string(val_sv);
      return;
    }
    auto& info  = it->second;
    auto  key   = lnast_->get_name(key_n);
    auto  val_n = lnast_->get_sibling_next(key_n);
    auto  val   = val_n.is_invalid() ? std::string_view{"true"} : std::string_view(lnast_->get_name(val_n));
    if ((key == "reset_pin")) {
      info.reset_pin_name = std::string(val);
    } else if ((key == "clock_pin")) {
      info.clock_pin_name = std::string(val);
    } else if ((key == "posclk") || (key == "enable_high")) {
      // `enable_high` is the LATCH-facing spelling of the same pin (2f-latch
      // M2): on a latch, pid 6 is the ENABLE POLARITY, not a clock edge, so
      // `posclk` reads as a lie there. Both map to the same slot — the IR pin
      // keeps the Flop-shared name because find_sink_pin() resolves an unknown
      // name to invalid SILENTLY, and a rename that missed a consumer would
      // drop the polarity without a word.
      info.has_posclk = true;
      info.posclk_val = val != "false" && val != "0";
    } else if ((key == "sync")) {
      info.has_sync = true;
      info.sync_val = val != "false" && val != "0";
    } else if ((key == "async")) {
      // Canonical Pyrope-source spelling (04b-attributes.md known_attrs): the
      // inverse of the importer's `sync`.  `async=true` => async reset.
      info.has_sync = true;
      info.sync_val = val == "false" || val == "0";
    } else if ((key == "negreset")) {
      info.negreset = val != "false" && val != "0";
    } else if ((key == "initial") || (key == "init")) {
      // `init` is the Pyrope-source spelling; `initial` the importer's.  Both
      // override the declare's reset value.
      info.initial_txt = std::string(val);
    } else if ((key == "name")) {
      // Explicit local flop name (`reg x:[name="reg_x"]`) — overrides the
      // declared variable name; finalize_regs combines it with any hier prefix.
      // The value is a Pyrope string literal, so strip the surrounding quotes.
      std::string_view nm = val;
      if (nm.size() >= 2 && ((nm.front() == '\'' && nm.back() == '\'') || (nm.front() == '"' && nm.back() == '"'))) {
        nm = nm.substr(1, nm.size() - 2);
      }
      info.name_override = std::string(nm);
    } else if (key == "__hier") {
      // Runner-stamped instance-path prefix for a reg inside an inlined comb
      // (`pipeB_ex_mem`); finalize_regs prepends it (dotted) to the local name.
      info.hier_prefix = std::string(val);
    } else if ((key == "type") || (key == "comptime")) {
      // storage-class markers — already consumed by the declare
    } else {
      warn_at(tgt,
              {"reg-attr-not-lowered", "unsupported"},
              "reg '{}' attribute '{}' not lowered (attribute not in the "
              "lowered set)",
              lnast_->get_name(tgt),
              key);
    }
  }

  // Persist the block-attribute regions (2opt-freq B) as the graph-level
  // coloring_info JSON: algorithm "block-attr" marks the colors as
  // SOURCE-SEEDED (pass.color preserves seeded colors and allocates its own
  // ids above them), and "region_opts" carries each region's abc= flow string
  // for pass.abc. Emitted only when at least one annotated block exists, so
  // ordinary compiles keep no coloring_info.
  void write_region_info() {
    if (region_colors_marked_.empty()) {
      return;
    }
    // Absorb uncolored fan-in glue into its consumers' region. Helper-minted
    // conditioning nodes (e.g. node_util's to-positive Get_mask wrappers)
    // bypass the make_node funnel and would otherwise shatter into tiny
    // color-0 boundary regions between the block and its inputs — each a
    // separate mapping unit whose region cut costs delay. A color-0 node
    // whose every sink lives in ONE region belongs to that region; iterate to
    // a fixpoint so glue chains absorb too.
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto n : g_->body().nodes(hhds::Node_order::forward)) {
        if (n.is_invalid() || livehd::graph_util::is_builtin_node(n)) {
          continue;
        }
        auto op = livehd::graph_util::type_op_of(n);
        if (op == Ntype_op::Nconst || op == Ntype_op::IO) {
          continue;
        }
        if (livehd::graph_util::node_color_of(n) != 0) {
          continue;
        }
        int32_t c  = 0;
        bool    ok = false;
        for (const auto& e : n.out_edges()) {
          auto sn = e.sink.get_master_node();
          if (sn.is_invalid() || livehd::graph_util::is_builtin_node(sn)) {
            ok = false;  // drives an output/builtin: boundary glue, keep it out
            break;
          }
          auto sc = livehd::graph_util::node_color_of(sn);
          if (sc == 0 || (c != 0 && sc != c)) {
            ok = false;  // uncolored or multi-region fanout: stays background
            break;
          }
          c  = sc;
          ok = true;
        }
        if (ok && c != 0) {
          livehd::graph_util::set_color(n, c);
          changed = true;
        }
      }
    }
    for (auto c : region_colors_marked_) {
      if (!region_colors_stamped_.contains(c)) {
        warn_at(Lnast_nid{},
                {"block-attr-region-empty", "unsupported"},
                "block region color {} in '{}' produced no hardware (its "
                "statements folded away?) — the annotation has no effect",
                c,
                lnast_->get_top_module_name());
      }
    }
    auto jesc = [](std::string_view sv) {
      std::string out;
      out.reserve(sv.size());
      for (char ch : sv) {
        switch (ch) {
          case '"' : out += "\\\""; break;
          case '\\': out += "\\\\"; break;
          case '\n': out += "\\n"; break;
          case '\t': out += "\\t"; break;
          default  : out.push_back(ch);
        }
      }
      return out;
    };
    std::string j  = "{\"schema_version\":1,";
    j             += std::format("\"top\":\"{}\",", jesc(lnast_->get_graph_name()));
    j             += "\"algorithm\":\"block-attr\",\"params\":{},\"colors\":{},"
                     "\"region_opts\":{";
    bool first     = true;
    for (const auto& [color, abc] : region_abc_) {
      if (!first) {
        j += ",";
      }
      first  = false;
      j     += std::format("\"{}\":{{\"flow\":\"{}\"}}", color, jesc(abc));
    }
    j += "}}";
    g_->get_input_node().attr(livehd::attrs::coloring_info).set(j);
  }

  // Remove hold arms that branch lowering bakes into a latch's D before
  // cprop. This narrow structural proof matters for hierarchical LEC, where a
  // child definition can be inspected before a graph-pass sweep reaches it.
  // At every peeled layer, D's Q arm must correspond to a known-false arm of
  // the enable mux under the exact same selector.
  [[nodiscard]] Pin canonical_latch_din(Pin din, const Pin& q, Pin en) {
    const auto driver_at = [](const hhds::Node_class& n, hhds::Port_id pid) {
      for (const auto& e : n.inp_edges()) {
        if (!e.sink.is_invalid() && e.sink.get_port_id() == pid) {
          return e.driver;
        }
      }
      return Pin{};
    };
    const auto same = [](const Pin& a, const Pin& b) {
      return !a.is_invalid() && !b.is_invalid() && a.get_class_index() == b.get_class_index();
    };
    for (int depth = 0; depth < 64 && !din.is_invalid() && !en.is_invalid(); ++depth) {
      auto dm = din.get_master_node();
      auto em = en.get_master_node();
      if (livehd::graph_util::type_op_of(dm) != Ntype_op::Mux || livehd::graph_util::type_op_of(em) != Ntype_op::Mux) {
        break;
      }
      auto ds = driver_at(dm, 0);
      auto es = driver_at(em, 0);
      if (!same(ds, es)) {
        break;
      }
      auto d0    = driver_at(dm, 1);
      auto d1    = driver_at(dm, 2);
      int  q_arm = same(d0, q) ? 0 : (same(d1, q) ? 1 : -1);
      if (q_arm < 0) {
        break;
      }
      auto e_hold = driver_at(em, static_cast<hhds::Port_id>(q_arm + 1));
      if (e_hold.is_invalid() || !livehd::graph_util::is_const_pin(e_hold)
          || !livehd::graph_util::hydrate_const(e_hold).is_known_false()) {
        break;
      }
      din = q_arm == 0 ? d1 : d0;
      en  = driver_at(em, static_cast<hhds::Port_id>((1 - q_arm) + 1));
    }
    return din;
  }

  // Wire each declared reg's din / enable / reset_pin / initial /
  // async / negreset after the whole body has been lowered (stores and attr
  // overrides arrive in any order relative to the declare).
  void finalize_regs() {
    for (const auto& name : reg_order_) {
      auto& info = reg_info_.at(name);
      auto& flop = info.flop;

      // Hierarchical / overridden flop name. `name_override` (`reg
      // x::[name=…]`) replaces the local name; `hier_prefix` (runner `__hier`,
      // the instance path of the inlined comb) is prepended dotted — so an
      // inlined reg reads `pipeB_ex_mem.reg_x`, the same get_hier_name() a
      // non-inlined Sub gives.
      if (!info.hier_prefix.empty() || !info.name_override.empty()) {
        // Recover the clean source-local name from the connectivity name by
        // stripping the inliner's frame tag (`inl<salt>_`) and any SSA suffix.
        auto strip_inl_tag = [](std::string_view s) -> std::string_view {
          if (s.size() > 4 && s.compare(0, 3, "inl") == 0) {
            std::size_t i = 3;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
              ++i;
            }
            if (i > 3 && i < s.size() && s[i] == '_') {
              return s.substr(i + 1);
            }
          }
          return s;
        };
        std::string local;
        if (!info.name_override.empty()) {
          local = info.name_override;
        } else {
          local = std::string(strip_inl_tag(name));
          if (auto p = local.find("___ssa_"); p != std::string::npos) {
            local.resize(p);
          }
        }
        const std::string final_name = info.hier_prefix.empty() ? local : (info.hier_prefix + "." + local);
        if (!final_name.empty()) {
          auto qn = flop.create_driver_pin(0);
          livehd::graph_util::set_pin_name(qn, final_name);
          flop.set_name(final_name);
        }
      }

      // Runs after the walk: anchor this reg's diagnostics at its declaration
      // instead of whatever statement the walk ended on.
      cur_srcid_ = hhds::SourceId_invalid;
      if (const auto id = lnast_->get_srcid(info.decl_nid); id != hhds::SourceId_invalid) {
        cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
      }
      cur_color_ = info.decl_color;  // finalize glue lands in the reg's region

      // din: the final shadow value (last-write-wins; branch writes arrive
      // pre-muxed). A never-written reg holds its value forever: din <- q.
      auto q = flop.create_driver_pin(0);
      Pin  din;
      if (auto dit = pin_map_.find(din_key(name)); dit != pin_map_.end()) {
        din = dit->second;
      } else {
        din = q;
      }
      if (info.is_latch) {
        if (auto eit = pin_map_.find(en_key(name)); eit != pin_map_.end()) {
          din = canonical_latch_din(din, q, eit->second);
        }
      }
      setup_sink_by_name(flop, "din").connect_driver(din);

      if (info.is_latch) {
        // FAIL CLOSED on an attr this branch cannot honor (2f-latch M0), now
        // narrowed to the ones M2 did NOT wire. Before M0 every one of these
        // was silently DISCARDED: a
        // `reg l:u8:[latch=true, clock_pin=ck2, posclk=false, init=3]`
        // compiled exit 0, zero warnings, and emitted Verilog byte-identical to
        // a plain transparent-high latch. Authoring an attr that vanishes is
        // worse than not having it.
        //
        // Still refused: the reset family (M7 wires it; the pins are reserved
        // on the cell already) and `clock_pin`. clock_pin stays refused BY
        // DESIGN, not as a stub: a latch's gate IS its enable, so a second
        // clock/gate identity would recreate exactly the two-sources-of-truth
        // disagreement the enable-polarity ruling collapses. It is only
        // reserved on the cell so a future ICG model has a slot if one is ever
        // ruled in.
        {
          std::string_view dropped;
          std::string_view why = "todo/livehd/2f-latch M7 wires the reset family";
          if (!info.clock_pin_name.empty()) {
            dropped = "clock_pin";
            why
                = "a latch's gate IS its `enable` signal — write the "
                  "transparency condition in the `if`, not as a clock";
          } else if (info.has_posclk && !info.posclk_val) {
            // ACTIVE-LOW ENABLE IS NOT EXPRESSIBLE IN THE PYROPE SHAPE, and
            // wiring it as a bare pin flip is a SILENT MISCOMPILE (measured
            // 2026-07-20 — this is exactly the "posclk double-negation" a
            // symmetric before/after gate cannot see; it was caught only by
            // LEC-ing against an independent golden).
            //
            // Why: tolg bakes the hold mux into din from the SAME condition,
            //   din = cond ? d : q   and   enable = cond
            // so the enable is active-HIGH *by construction*. Flipping only the
            // polarity pin yields `if (!cond) q <= (cond ? d : q)`: while the
            // latch is transparent (cond==0) din resolves to q, so it writes
            // ITSELF forever and NEVER captures d. Emitting that would look
            // perfectly reasonable in the Verilog.
            //
            // Making it sound would mean rebuilding din against the inverted
            // condition. There is no need: `if !g { l = d }` already says
            // active-low exactly, correctly, and is the shipped spelling of the
            // live `latch_active_low` fixture. So the attribute is REFUSED here
            // rather than half-honored. The CELL still carries the polarity
            // (pid 6) for the YOSYS importer, whose raw-D + EN shape has no
            // hold mux and for which the flip IS sound.
            dropped = "enable_high=false (active-low enable)";
            why
                = "the Pyrope lowering builds `din = cond ? d : q` from the "
                  "SAME condition, so the enable is active-high by "
                  "construction and flipping only the polarity would make "
                  "the latch write itself and never capture din — write the "
                  "inverted condition instead: `if !g { ... }`";
          }
          // The RESET FAMILY (reset_pin / sync / async / negreset / init) is no
          // longer refused: M7 wires it through the SHARED flop path below, so
          // a latch gets the same reset semantics a flop does and cgen emits it.
          if (!dropped.empty()) {
            error_here(
                "upass.tolg: latch '{}' carries '{}', which the Latch "
                "cell cannot honor — the attribute would be SILENTLY "
                "DROPPED. {}",
                name,
                dropped,
                why);
            continue;
          }
        }
        // A latch now FALLS THROUGH to the shared q-width / enable / reset
        // wiring below (2f-latch M7) instead of duplicating the first two and
        // refusing the third. Only the clock/posclk block is skipped: a latch's
        // gate IS its enable, so it has no clock identity to bind.
      }

      // clock: explicit clock_pin=NAME beats the implicit/shared clock input.
      // A named clock is usually a module input (clk_i), but can also be an
      // internal/derived wire — e.g. a gated clock (a clock-gate cell's
      // `clk & en_latch` output) feeding a flop. Check has_input FIRST (a clock
      // input is NOT in pin_map_; an unrelated same-named signal might be,
      // which is why pin_map_-first is wrong), then fall back to pin_map_ for
      // the internal-wire case. get_input_pin would assert on a non-input.
      if (info.is_latch) {
        // no clock identity: the gate IS the enable (user ruling, 2f-latch M2)
      } else if (!info.clock_pin_name.empty()) {
        // 2c-wire — a wire clock signal (a gated/derived clock): use its DRIVER
        // (din) directly, not the passthrough buffer (cgen drops a buffer whose
        // only consumer is a flop control pin).
        std::string cn = info.clock_pin_name;
        if (g_->get_io()->has_input(cn)) {
          setup_sink_by_name(flop, "clock_pin").connect_driver(g_->get_input_pin(cn));
        } else if (auto dit = wire_names_.contains(cn) ? pin_map_.find(din_key(cn)) : pin_map_.end(); dit != pin_map_.end()) {
          setup_sink_by_name(flop, "clock_pin").connect_driver(dit->second);
        } else if (pin_map_.contains(cn)) {
          setup_sink_by_name(flop, "clock_pin").connect_driver(pin_map_.at(cn));
        } else {
          error_here(
              "upass.tolg: reg '{}' names clock_pin '{}' but '{}' has "
              "no such input/wire",
              name,
              info.clock_pin_name,
              lnast_->get_top_module_name());
          continue;
        }
      } else if (!clock_name_.empty()) {
        setup_sink_by_name(flop, "clock_pin").connect_driver(clock_pin());
      } else {
        warn_at(info.decl_nid, {"no-clock", "time"}, "reg '{}' has no clock input to bind", name);
      }
      // 2f-latch M9 -- NOTHING MAY BE DONE TO A CLOCK except a recognized clock
      // operation. A clock may be GATED (`clk and en`) or INVERTED (`not clk`);
      // both lower to a `Clock_cell` and every consumer knows what they mean.
      // Arithmetic, or a bitwise op that MERGES data into the clock, has no
      // such meaning -- and the failure is otherwise silent in the worst way: a
      // step-granular encoder cannot tell `clk or sel` from plain `clk`, so it
      // came back PROVEN equal to the ungated design (measured, and pinned by
      // lec_clock_blindness_test). Refuse at COMPILE time, where the source span
      // still exists to point at.
      if (const auto bad = illegal_clock_op(livehd::graph_util::get_driver_of_sink_name(flop, "clock_pin")); !bad.empty()) {
        error_at(info.decl_nid,
                 {"gated-clock-unsupported", "time"},
                 "reg '{}' takes its clock from a `{}` operation, which is not a clock operation -- a clock may only "
                 "be gated (`clk and en`) or inverted (`not clk`)",
                 name,
                 bad);
      }
      if (info.has_posclk && !info.posclk_val) {
        setup_sink_by_name(flop, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
      }

      // q width: untyped regs take the final din width (mw+1 unsigned).
      if (info.decl_mw == 0) {
        auto    dit = mw_map_.find(din_key(name));
        int32_t mw  = dit != mw_map_.end() ? dit->second : int32_t{1};
        set_ubits(q, mw);
        mw_map_[name] = mw;
      }

      // enable: still the seeded false const => never written. For a Flop, the
      // true const needs no pin (unconditional edge update is the default).
      // For a Latch it MUST remain explicit: enable=true means always
      // transparent, which lets cprop recognize that the cell stores nothing
      // and replace it with its combinational din. Any other pin is the
      // OR-of-conditions mux chain.
      if (auto eit = pin_map_.find(en_key(name)); eit != pin_map_.end()) {
        const auto en       = eit->second;
        const auto en_nid   = en.get_master_node().get_debug_nid();
        const bool is_true  = en_true_valid_ && en_nid == en_true_pin_.get_master_node().get_debug_nid();
        const bool is_false = en_false_valid_ && en_nid == en_false_pin_.get_master_node().get_debug_nid();
        if (!is_false) {
          if (info.is_latch) {
            // A latch has no clock to gate, so the transported instance
            // activation participates directly in its transparency enable.
            // Reset remains a separate, higher-priority control in cgen.
            const auto active = !valid_active_ ? Pin{} : valid_pin();
            // Dynamic enables come from lower_if's merged branch selectors,
            // which already include activation so din's hold mux and enable
            // remain structurally identical (the latch-contract proof relies
            // on that identity). Only an unconditional true needs gating here.
            setup_sink_by_name(flop, "enable")
                .connect_driver(is_true ? (active.is_invalid() ? en : active) : (valid_minted_ ? en : and2(en, active)));
          } else if (!is_true) {
            setup_sink_by_name(flop, "enable").connect_driver(en);
          }
        }
      }

      // Reset wiring. Effective init: an explicit `initial=N` attr overrides
      // the declare's [value]; "nil" (or absent) = NO reset (confirmed
      // 2026-06-07 ruling).
      const std::string init     = !info.initial_txt.empty() ? info.initial_txt : info.init_txt;
      const bool        has_init = !init.empty() && init != "nil";
      const bool        rp_false = info.reset_pin_name == "false";
      if (rp_false && has_init) {
        error_here(
            "upass.tolg: reg '{}' has a non-nil initializer but "
            "`reset_pin=false` — drop the init or the override",
            name);
        return;
      }
      const bool wants_reset = (has_init || (!info.reset_pin_name.empty() && !rp_false)) && !rp_false;
      if (!wants_reset) {
        continue;
      }

      Pin  rpin;
      bool neg = info.negreset;
      if (!info.reset_pin_name.empty()) {
        // Usually a graph input, but a reset synchronizer drives it from a
        // DERIVED module-level signal — wire from that signal's driver instead.
        // (get_input_pin ASSERTS on a non-input name; gate on has_input first.)
        if (g_->get_io()->has_input(info.reset_pin_name)) {
          rpin = g_->get_input_pin(info.reset_pin_name);
        } else {
          // Derived reset signal: its FINAL combinational driver lives in
          // logical_last_ (the last SSA version), NOT pin_map_[name] that
          // resolve() checks (which holds the read-site version, or nothing).
          std::string base = info.reset_pin_name;
          if (auto p = base.find("___ssa_"); p != std::string::npos) {
            base.resize(p);
          }
          // 2c-wire — a wire reset signal: use its DRIVER (din) directly, not
          // the passthrough buffer output. A buffer whose only consumer is a
          // flop control pin is dropped by cgen (the reset would reference an
          // undriven net); the din is the real combinational value.
          if (auto dit = wire_names_.contains(base) ? pin_map_.find(din_key(base)) : pin_map_.end(); dit != pin_map_.end()) {
            rpin = dit->second;
          } else if (auto lit = logical_last_.find(base); lit != logical_last_.end()) {
            rpin = lit->second.first;
          } else {
            rpin = resolve(info.reset_pin_name);
          }
        }
        if (str_tools::ends_with(info.reset_pin_name, "_n")) {
          neg = true;
        }
      } else if (!reset_name_.empty()) {
        rpin = reset_pin();
        if (reset_neg_) {
          neg = true;
        }
      } else {
        error_here(
            "upass.tolg: reg '{}' has a reset value but '{}' has no "
            "reset input (setup_io bug)",
            name,
            lnast_->get_top_module_name());
        return;
      }
      setup_sink_by_name(flop, "reset_pin").connect_driver(rpin);
      if (has_init) {
        // The reset value must be a compile-time constant. A body-`reg`'s
        // non-literal init is caught at the declare (lower_declare errors on
        // a ref init); an output-reg `-> (reg q = expr)` stringifies its
        // initializer, so a malformed parse can still arrive here — reject it
        // rather than deref a null Dlop.
        auto iv = Dlop::from_pyrope(init);
        if (!iv) {
          error_here(
              "upass.tolg: reg '{}' reset/initial value '{}' is not a "
              "compile-time constant",
              name,
              init);
          return;
        }
        setup_sink_by_name(flop, "initial").connect_driver(create_const(*g_, *iv));
      }
      if (neg) {
        setup_sink_by_name(flop, "negreset").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
      }
      // sync-vs-async: per-reg `sync` attr beats the elaboration flag.
      const bool async = info.has_sync ? !info.sync_val : reset_async_default_;
      if (async) {
        setup_sink_by_name(flop, "async").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
      }
    }
  }

  // store(ref(lhs), value) — scalar assignment / alias. A store whose lhs is
  // a declared reg connects the value to the Flop's din instead of rebinding
  // the name (reads keep seeing the q pin — Verilog `<=` semantics).
  void lower_store(const Lnast_nid& nid) {
    auto lhs = lnast_->get_first_child(nid);
    if (lhs.is_invalid()) {
      return;
    }
    auto rhs = lnast_->get_sibling_next(lhs);
    if (rhs.is_invalid()) {
      return;
    }
    const std::string lhs_text{lnast_->get_name(lhs)};

    // Comptime arrays have already been evaluated and every runtime use is
    // materialized by the runner (for example as a tuple-literal reg init).
    // Their original initializer stores remain in the marked LNAST for source
    // fidelity, but they must not mint hardware.
    if (comptime_array_names_.contains(lhs_text)) {
      return;
    }

    // Combinational typed positional array. A two-child store is a whole-value
    // initializer/replacement; additional children are indices followed by the
    // element value. The live representation is a packed bus with element 0 in
    // the least-significant lane.
    if (auto ait = array_scalar_views_.find(lhs_text); ait != array_scalar_views_.end()) {
      auto& view = ait->second;
      auto  next = lnast_->get_sibling_next(rhs);
      if (next.is_invalid()) {
        Pin value_pin;
        if (Lnast_ntype::is_const(lnast_->get_type(rhs))) {
          auto txt = lnast_->get_name(rhs);
          auto v   = (txt == "nil" || txt == "0sb?") ? Dlop::create_integer(0) : Dlop::from_pyrope(txt);
          if (!v || !v->is_integer()) {
            error_here("upass.tolg: whole-array value '{}' for '{}' is not an integer", txt, lhs_text);
            return;
          }
          auto lane   = v->and_op(*Dlop::get_mask_value(view.elem_mw));
          auto packed = Dlop::create_integer(0);
          for (int64_t i = 0; i < view.size; ++i) {
            packed = packed->or_op(*lane->shl_op(*Dlop::create_integer(i * view.elem_mw)));
          }
          value_pin = create_const(*g_, *packed);
        } else if (Lnast_ntype::is_ref(lnast_->get_type(rhs))) {
          const std::string rhs_name{lnast_->get_name(rhs)};
          if (auto tit = tuple_recs_.find(rhs_name); tit != tuple_recs_.end() && tit->second.named.empty()
                                                     && static_cast<int64_t>(tit->second.elems.size()) == view.size) {
            auto packed = Dlop::create_integer(0);
            for (int64_t i = 0; i < view.size; ++i) {
              const auto e = tit->second.elems[static_cast<size_t>(i)];
              if (!Lnast_ntype::is_const(lnast_->get_type(e))) {
                error_here("upass.tolg: runtime tuple whole-array value for '{}' is not supported", lhs_text);
                return;
              }
              auto ev = Dlop::from_pyrope(lnast_->get_name(e));
              if (!ev || !ev->is_integer()) {
                error_here("upass.tolg: array '{}' initializer element is not an integer", lhs_text);
                return;
              }
              auto lane = ev->and_op(*Dlop::get_mask_value(view.elem_mw));
              packed    = packed->or_op(*lane->shl_op(*Dlop::create_integer(i * view.elem_mw)));
            }
            value_pin = create_const(*g_, *packed);
          } else {
            value_pin = leaf(rhs).pin;
          }
        } else {
          value_pin = leaf(rhs).pin;
        }
        record(lhs_text, value_pin, static_cast<int32_t>(view.size * view.elem_mw));
        return;
      }

      // One-dimensional element store for now; nested dimensions stay on the
      // existing memory path until their row-major flattening is generalized.
      auto value_nid = next;
      if (!lnast_->get_sibling_next(next).is_invalid()) {
        error_here("upass.tolg: scalar-replaced array '{}' currently supports one element index", lhs_text);
        return;
      }
      // CANONICAL key: record() strips the backtick quoting, so a flattened
      // struct-field array (`` `bht_d.valid` `` out of inou/slang) is keyed
      // `bht_d.valid` -- the raw spelling missed here and every such array
      // read "written before it has an initializer" at its first element store.
      auto base_it = pin_map_.find(std::string(canon_io_name(lhs_text)));
      if (base_it == pin_map_.end()) {
        error_here("upass.tolg: array '{}' is written before it has an initializer", lhs_text);
        return;
      }
      Val  base{base_it->second, static_cast<int32_t>(view.size * view.elem_mw)};
      auto iv = leaf(value_nid);

      if (Lnast_ntype::is_const(lnast_->get_type(rhs))) {
        auto ci = Dlop::from_pyrope(lnast_->get_name(rhs));
        if (!ci || !ci->is_just_i64() || ci->to_just_i64() < 0 || ci->to_just_i64() >= view.size) {
          error_at(nid,
                   {"array-index-out-of-range", "type"},
                   "Pyrope array index {} is outside [0, {}) for '{}'",
                   lnast_->get_name(rhs),
                   view.size,
                   lhs_text);
          return;
        }
        const int64_t off  = ci->to_just_i64() * view.elem_mw;
        auto          mask = Dlop::get_mask_value(static_cast<int>(off + view.elem_mw - 1), static_cast<int>(off));
        auto          sm   = make_node(Ntype_op::Set_mask);
        setup_sink_by_name(sm, "a").connect_driver(base.pin);
        setup_sink_by_name(sm, "mask").connect_driver(create_const(*g_, *mask));
        setup_sink_by_name(sm, "value").connect_driver(iv.pin);
        auto out = sm.create_driver_pin(0);
        set_ubits(out, base.mw);
        record(lhs_text, out, base.mw);
        return;
      }

      auto index = leaf(rhs);
      auto mult  = make_node(Ntype_op::Mult);
      setup_sink_by_name(mult, "as").connect_driver(index.pin);
      setup_sink_by_name(mult, "as").connect_driver(create_const(*g_, *Dlop::create_integer(view.elem_mw)));
      auto offset = mult.create_driver_pin(0);
      set_ubits(offset, std::max<int32_t>(index.mw + std::bit_width(static_cast<uint32_t>(view.elem_mw)), 1));

      auto maskn = make_node(Ntype_op::SHL);
      setup_sink_by_name(maskn, "a").connect_driver(create_const(*g_, *Dlop::get_mask_value(view.elem_mw)));
      setup_sink_by_name(maskn, "b").connect_driver(offset);
      auto mask = maskn.create_driver_pin(0);
      set_ubits(mask, base.mw);

      auto shifted = make_node(Ntype_op::SHL);
      setup_sink_by_name(shifted, "a").connect_driver(iv.pin);
      setup_sink_by_name(shifted, "b").connect_driver(offset);
      auto placed = shifted.create_driver_pin(0);
      set_ubits(placed, base.mw);

      auto out = lower_dynamic_mask_rmw(base, mask, placed);
      record(lhs_text, out, base.mw);
      lower_array_index_assert(index, view.size, nid);
      return;
    }
    // 1a-mem — an indexed store to a declared memory becomes a write port;
    // the 2-child whole-array form is the mut/const array initializer.
    if (auto mit = mem_map_.find(std::string(lnast_->get_name(lhs))); mit != mem_map_.end()) {
      if (lnast_->get_sibling_next(rhs).is_invalid()) {
        lower_mem_init_store(rhs, lnast_->get_name(lhs), mit->second);
      } else {
        lower_mem_store(lhs, lnast_->get_name(lhs), mit->second);
      }
      return;
    }
    // A bit-view or whole-value update of a combinational typed array is
    // SSA-versioned (`r___ssa_N = packed_bus`). Keep that version as a scalar
    // packed alias with the original array layout. Subsequent bit reads use the
    // bus directly and element reads extract one declared-width lane.
    if (lnast_->get_sibling_next(rhs).is_invalid()) {
      const std::string base = logical_key(lhs_text);
      if (base != lhs_text) {
        if (auto ait = array_scalar_views_.find(base); ait != array_scalar_views_.end()) {
          auto v = leaf(rhs);
          record(lhs_text, v.pin, v.mw);
          auto view_copy                = ait->second;
          array_scalar_views_[lhs_text] = std::move(view_copy);
          return;
        }
        if (auto mit = mem_map_.find(base); mit != mem_map_.end() && mit->second.is_array) {
          auto v = leaf(rhs);
          record(lhs_text, v.pin, v.mw);
          array_scalar_views_[lhs_text] = Array_scalar_view{
              .size        = mit->second.size,
              .dims        = mit->second.dims,
              .elem_mw     = mit->second.elem_mw,
              .elem_signed = mit->second.elem_signed,
          };
          return;
        }
      }
    }
    if (!lnast_->get_sibling_next(rhs).is_invalid()) {
      error_at(lhs,
               {"tuple-store-unsupported", "unsupported"},
               "upass.tolg: tuple/field store to '{}' has no hardware lowering "
               "— the elaboration left a multi-element "
               "store that cannot be turned into wires",
               lnast_->get_name(lhs));
    }
    auto lhs_name = lnast_->get_name(lhs);
    // `c = concat(...)` — the destination's declared width must equal the lane
    // sum exactly. Checked at the STORE (and at the declare below) because the
    // concat node's own dst is always a compiler temp, so this is the first
    // point where a user-declared name and a concat result meet.
    check_concat_dest_width(nid, lhs_name, rhs);
    // Deferred stage-reg creation: the din store knows the
    // effective depth (deficit narrowing against a Sub callee; 0 = wire).
    if (auto pit = pending_stage_.find(lhs_name); pit != pending_stage_.end()) {
      auto pending = pit->second;
      pending_stage_.erase(pit);
      create_stage_flop(lhs_name, pending, rhs);
      return;
    }
    if (reg_map_.contains(lhs_name)) {
      // A store to a declared reg is a next-state write: rebind the
      // SHADOW din/enable keys (never the name — reads keep seeing q, Verilog
      // `<=` semantics). The branch-mux machinery merges conditional writes
      // into last-write-wins din + OR-of-conditions enable; finalize_regs()
      // wires the final pins.
      if (!reg_info_.contains(std::string(lhs_name))) {
        // A stage reg (created by its one din store) has no finalize record —
        // a second store would be silently lost.
        error_here("upass.tolg: stage reg '{}' stored more than once in '{}'", lhs_name, lnast_->get_top_module_name());
        return;
      }
      auto v = leaf(rhs);
      record(din_key(lhs_name), v.pin, v.mw);
      record(en_key(lhs_name), en_const(true), 1);
      return;
    }
    if (wire_names_.contains(std::string(lhs_name))) {
      // 2c-wire — a store to a wire is (part of) its single combinational
      // driver, recorded on the SHADOW din key (reads keep seeing the buffer
      // output, so they stay position-independent). The branch-mux machinery
      // merges conditional writes before the buffer input is bound. A
      // `= nil` forward-declare is not a driver — skip it.
      if (Lnast_ntype::is_const(lnast_->get_type(rhs)) && lnast_->get_name(rhs) == "nil") {
        return;
      }
      auto v = leaf(rhs);
      record(din_key(lhs_name), v.pin, v.mw);
      maybe_bind_wire_shadow(din_key(lhs_name), v.pin, v.mw);
      return;
    }
    // 1a-mem — a plain `name = <tuple-literal-ref>` / `name = <__memory
    // result>` aliases the record instead of binding a scalar pin (the
    // literal/result has no pin; its consumers resolve through the record).
    if (Lnast_ntype::is_ref(lnast_->get_type(rhs))) {
      const std::string rhs_name(lnast_->get_name(rhs));
      // prp_writer names a call result before feeding it to a stage:
      //   const t = pipe(...); stage[N] x = t
      // Preserve the callee's latency rider across that scalar alias so
      // create_stage_flop narrows N by the pipe's realized minimum instead of
      // charging N fresh flops on top of the callee.
      if (auto sit = sub_out_stages_.find(rhs_name); sit != sub_out_stages_.end()) {
        // Copy before inserting: operator[] may rehash the flat_hash_map and
        // invalidate sit before the RHS is read.
        const auto sub_out_copy                = sit->second;
        sub_out_stages_[std::string(lhs_name)] = sub_out_copy;
      }
      // Copy BEFORE inserting: operator[] may rehash and invalidate the
      // found iterator (the type_info_map rehash-invalidation UAF all over
      // again).
      if (auto tit = tuple_recs_.find(rhs_name); tit != tuple_recs_.end()) {
        auto rec_copy                      = tit->second;
        tuple_recs_[std::string(lhs_name)] = std::move(rec_copy);
        return;
      }
      if (auto ait = array_scalar_views_.find(rhs_name); ait != array_scalar_views_.end()) {
        auto view_copy                             = ait->second;
        array_scalar_views_[std::string(lhs_name)] = std::move(view_copy);
      }
      if (auto mrt = mem_results_.find(rhs_name); mrt != mem_results_.end()) {
        auto rec_copy                       = mrt->second;
        mem_results_[std::string(lhs_name)] = rec_copy;
        return;
      }
      // A call result bound to a named var (`mut tmp = add_sub(…)`): alias the
      // Sub_result so `tmp.add`/`tmp.sub` resolve through the named var, not
      // just the call's result temp. (Copy before insert — operator[] may
      // rehash and invalidate the found iterator.) A MULTI-output result has no
      // scalar pin, so the alias is the whole binding (return). A SINGLE-output
      // result IS scalar-bindable (the result temp has a direct pin record), so
      // alias it AND fall through to bind the scalar — otherwise a plain read
      // of the named var (`s1 + s2`, no `.field`) would find no driver.
      if (auto srt = sub_results_.find(rhs_name); srt != sub_results_.end()) {
        const bool multi                    = srt->second.outputs.size() > 1;
        auto       rec_copy                 = srt->second;
        sub_results_[std::string(lhs_name)] = std::move(rec_copy);
        if (multi) {
          return;
        }
        // single-output: fall through to the scalar record below
      }
    }
    auto v = leaf(rhs);
    record(lhs_name, v.pin, v.mw);
  }

  // declare(ref(name), type, const("wire")): a single-driver combinational net
  // (2c-wire). Create the passthrough buffer (Or) and bind the name to its
  // OUTPUT so every read — including one before the driver appears textually —
  // resolves to the net (position-independent). Stores record the din shadow;
  // the completed write binds the buffer input; finalize_wires() only enforces
  // the undriven rule (the single-driver rule is a frontend
  // check). No flop.
  void lower_wire_declare(const Lnast_nid& name_nid, const Lnast_nid& type_nid, const Lnast_nid& decl_nid) {
    auto name = lnast_->get_name(name_nid);
    if (!type_nid.is_invalid() && Lnast_ntype::is_comp_type_array(lnast_->get_type(type_nid))) {
      error_here(
          "upass.tolg: array `wire` is not supported — declare an array "
          "as `mut`/`reg`; a `wire` is a scalar net");
      return;
    }
    Wire_info info;
    info.buf      = make_node(Ntype_op::Or);  // single-input Or = pure passthrough (cgen `out = a`)
    info.out      = info.buf.create_driver_pin(0);
    info.decl_nid = decl_nid;
    if (!type_nid.is_invalid()) {
      std::tie(info.decl_mw, info.is_signed) = declared_width(type_nid);
    }
    if (info.decl_mw > 0) {
      if (info.is_signed) {
        set_sbits(info.out, info.decl_mw);
      } else {
        set_ubits(info.out, info.decl_mw);
      }
      record(name, info.out, info.decl_mw);
    } else {
      set_bits(info.out,
               1);  // provisional; finalize_wires restamps from the driver
      set_unsign(info.out);
      record(name, info.out, 1);
    }
    // Keep the net's RTL name on the buffer output (cgen / pass-lec
    // readability).
    {
      std::string base{name};
      if (auto p = base.find("___ssa_"); p != std::string::npos) {
        base.resize(p);
      }
      if (!base.empty()) {
        livehd::graph_util::set_pin_name(info.out, base);
      }
    }
    wire_names_.insert(std::string(name));
    wire_order_.emplace_back(name);
    info.decl_color = cur_color_;
    wire_info_.emplace(std::string(name), std::move(info));
    // A Verilog-origin comb-cycle net may legally close a same-cycle ring (e.g.
    // a ready/valid handshake whose dataflow loops through a submodule instance
    // but is not a real comb loop). Cut its buffer in-edge for the loop check,
    // preserving the pre-2c-wire leniency. Pyrope wires are NEVER cut, so a
    // real comb loop through a wire surfaces as a hard error.
    if (lnast_->is_verilog_origin()) {
      wire_cut_nids_.insert(wire_info_.at(name).buf.get_debug_nid());
    }
  }

  // declare(ref(name), type, const("reg")) [+ stages(min,max)]:
  // create the Flop cell (the first Flop on the Pyrope->LG path). The name
  // binds to the q pin so subsequent READS see q; the din store above wires
  // the input. Inserted pipeline flops carry the declared stages range on
  // the pipe_min/pipe_max comptime pins (LG pass1 narrows them by sigma
  // later). A pure-comb partition's flop is the no-reset shape — reset_pin/
  // initial/async/enable stay unconnected; posclk unset reads as posedge.
  void lower_declare(const Lnast_nid& nid) {
    auto name_nid = lnast_->get_first_child(nid);
    if (name_nid.is_invalid()) {
      return;
    }
    auto type_nid = lnast_->get_sibling_next(name_nid);
    auto mode_nid = type_nid.is_invalid() ? type_nid : lnast_->get_sibling_next(type_nid);
    auto mode     = mode_nid.is_invalid() || !Lnast_ntype::is_const(lnast_->get_type(mode_nid))
                        ? std::string_view{}
                        : std::string_view(lnast_->get_name(mode_nid));
    // Remember the DECLARED width for every flavour of declare (mut/const/wire/
    // reg/latch alike) before the per-mode branches return. Concat lanes read
    // this; nothing else does, so an unrecognised/absent type simply records
    // nothing and a lane on that name errors instead of silently mis-sizing.
    if (!type_nid.is_invalid()) {
      const auto [dmw, dsigned] = declared_width(type_nid);
      record_decl_type(lnast_->get_name(name_nid), dmw, dsigned);
    }
    // A declare's optional trailing [value] child carries the initializer, so
    // `const c:u12 = concat(a,b)` is checked here rather than at a store.
    for (auto c = mode_nid.is_invalid() ? mode_nid : lnast_->get_sibling_next(mode_nid); !c.is_invalid();
         c      = lnast_->get_sibling_next(c)) {
      if (Lnast_ntype::is_ref(lnast_->get_type(c))) {
        check_concat_dest_width(nid, lnast_->get_name(name_nid), c);
        break;
      }
    }
    // 2c-wire — a single-driver combinational net: declare its passthrough
    // buffer now so position-independent reads (a read before the driver) bind
    // to it; the completed write wires the buffer input to the single driver.
    if (mode == "wire" || mode.starts_with("wire ")) {
      lower_wire_declare(name_nid, type_nid, nid);
      return;
    }
    // Storage intent precedes representation: a `reg` array is persistent and
    // remains a Memory cell; a mut/const array is a combinational aggregate and
    // receives a packed scalar view that indexed operations can scalar-replace.
    const bool is_reg   = mode == "reg" || mode.starts_with("reg ");
    const bool is_latch = mode == "latch";  // level-sensitive latch (din+enable, no clock)
    if (!type_nid.is_invalid() && Lnast_ntype::is_comp_type_array(lnast_->get_type(type_nid))
        && (is_reg || mode == "mut" || mode == "const" || mode.starts_with("mut ") || mode.starts_with("const "))) {
      if (mode.find("comptime") != std::string_view::npos) {
        comptime_array_names_.insert(std::string(lnast_->get_name(name_nid)));
        return;
      }
      if (is_reg) {
        lower_mem_declare(name_nid, type_nid, mode_nid, /*is_array=*/false);
      } else {
        const auto elem_nid         = lnast_->get_first_child(type_nid);
        const bool multidimensional = !elem_nid.is_invalid() && Lnast_ntype::is_comp_type_array(lnast_->get_type(elem_nid));
        const bool has_inline_init  = !lnast_->get_sibling_next(mode_nid).is_invalid();
        if (multidimensional || has_inline_init) {
          // Slang ROM/array initializers are children of the declaration, and
          // nested arrays retain row-major address semantics. Keep those as a
          // Memory cell; the scalar view is for one-dimensional, store-driven
          // combinational arrays only.
          lower_mem_declare(name_nid, type_nid, mode_nid, /*is_array=*/true);
        } else {
          lower_comb_array_declare(name_nid, type_nid);
        }
      }
      return;
    }

    if (!is_reg && !is_latch) {
      // mut/const/type declares carry no graph payload here (values arrive
      // via their stores); nothing to lower. Remember the scalar name so a
      // later `b#[lo..=hi] = …` whose base is a still-undriven `mut b = nil`
      // can use a 0sb? base instead of erroring (see lower_set_mask).
      if (mode == "mut" || mode == "const" || mode.starts_with("mut ") || mode.starts_with("const ")) {
        // CANONICAL key: set_mask_base tests it against pin_map_, which
        // record()/resolve() key on the backtick-stripped name.
        scalar_decl_.insert(std::string(canon_io_name(lnast_->get_name(name_nid))));
      }
      if (mode == "const" || mode.starts_with("const ")) {
        const_decl_.insert(std::string(canon_io_name(lnast_->get_name(name_nid))));
      }
      return;
    }

    // stages(min,max) trailing child — DEFER the Flop creation to
    // the din store, which knows the effective depth (a Sub-fed stage reg
    // realizes the deficit stage_N − callee_min; depth 0 = wire, no Flop).
    // Safe because every emitted shape stores immediately after the declare
    // (prp2lnast enforces stage-needs-value; the pipe upass always emits the
    // din store) — no read can occur in between.
    for (auto c = lnast_->get_sibling_next(mode_nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (!Lnast_ntype::is_stages(lnast_->get_type(c))) {
        continue;
      }
      auto mn = lnast_->get_first_child(c);
      if (mn.is_invalid()) {
        break;
      }
      auto          mx = lnast_->get_sibling_next(mn);
      Pending_stage p;
      p.min_txt                                               = std::string(lnast_->get_name(mn));
      p.max_txt                                               = mx.is_invalid() ? p.min_txt : std::string(lnast_->get_name(mx));
      p.decl_nid                                              = nid;
      p.decl_color                                            = cur_color_;
      pending_stage_[std::string(lnast_->get_name(name_nid))] = std::move(p);
      return;
    }

    // Plain reg (no stages) — state/stage register: create the Flop
    // now; the name binds to q (reads see q), stores rebind the shadow
    // din/enable keys, finalize_regs() wires the pins. The declared type
    // gives q's width up front (a counter's `r + 1` read needs it before any
    // din store); untyped regs restamp from the final din width.
    auto flop = make_node(is_latch ? Ntype_op::Latch : Ntype_op::Flop);
    // clock wiring happens in finalize_regs (a clock_pin/posclk attr_set may
    // arrive after the declare). A latch has no clock/reset — finalize_regs
    // wires only its din + enable.
    auto name = lnast_->get_name(name_nid);
    auto q    = flop.create_driver_pin(0);
    // Keep the register's RTL name on q. The lnast path otherwise leaves the
    // flop unnamed (cgen then synthesizes `flop_<nid>`), losing the identity
    // that yosys-slang preserves — pass/lec needs it to put corresponding flops
    // of the two front-ends in 1:1 correspondence (and the emitted Verilog
    // reads better). Strip any SSA suffix so it matches the logical (declared)
    // name.
    {
      std::string base{name};
      if (auto p = base.find("___ssa_"); p != std::string::npos) {
        base.resize(p);
      }
      if (!base.empty()) {
        livehd::graph_util::set_pin_name(q, base);
        // Also stamp the flop NODE name so hhds get_hier_name() (which reads
        // the node `name` attr, not the LiveHD pin attr) reports the register's
        // hierarchical name `inst.reg` instead of the `n<id>` fallback.
        flop.set_name(base);
      }
    }

    Reg_info info;
    info.flop     = flop;
    info.is_latch = is_latch;
    info.decl_nid = nid;
    if (!type_nid.is_invalid()) {
      std::tie(info.decl_mw, info.is_signed) = declared_width(type_nid);
    }
    // The declare's optional trailing [value] child is the
    // power-on/reset value (a const after declare-folding; an unresolved ref
    // means a runtime initializer, which a reset value cannot be).
    for (auto c = lnast_->get_sibling_next(mode_nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      const auto ct = lnast_->get_type(c);
      if (Lnast_ntype::is_const(ct)) {
        info.init_txt = std::string(lnast_->get_name(c));
        break;
      }
      if (Lnast_ntype::is_ref(ct)) {
        error_here(
            "upass.tolg: reg '{}' initializer is not a compile-time "
            "constant — a reset value must be comptime",
            name);
        return;
      }
    }

    if (info.decl_mw > 0) {
      if (info.is_signed) {
        set_sbits(q, info.decl_mw);
      } else {
        set_ubits(q, info.decl_mw);
      }
      record(name, q, info.decl_mw);
    } else {
      record(name, q, 1);  // provisional width; finalize_regs restamps from din
    }
    reg_map_.emplace(std::string(name), flop);
    reg_order_.emplace_back(name);
    info.decl_color = cur_color_;
    reg_info_.emplace(std::string(name), std::move(info));
    plain_reg_flops_[flop.get_debug_nid()] = std::string(name);
    // Seed the enable shadow false: a store rebinds it true, the branch-mux
    // machinery turns conditional writes into the OR-of-conditions chain.
    record(en_key(name), en_const(false), 1);
  }

  // Per-reg lowering state recorded at the declare; consumed by
  // finalize_regs() after every store/attr_set has been seen.
  struct Reg_info {
    hhds::Node_class flop;
    Lnast_nid        decl_nid;
    int32_t          decl_color = 0;  // block region at the declare (finalize glue inherits it)
    std::string      init_txt;        // declare [value] child; "" = none, "nil" = explicit no-reset
    int32_t          decl_mw   = 0;   // declared type width; 0 = untyped
    bool             is_signed = false;
    // Per-reg flop-attr overrides (04b-attributes.md): a per-reg `sync` beats
    // the upass.reset_style flag; `reset_pin=false` opts out of reset.
    std::string      reset_pin_name;  // explicit reset_pin=NAME / "false"
    std::string      clock_pin_name;  // explicit clock_pin=NAME (beats implicit clock)
    bool             has_posclk = false;
    bool             posclk_val = true;  // false = negedge clock
    bool             has_sync   = false;
    bool             sync_val   = true;
    bool             negreset   = false;
    std::string      initial_txt;       // explicit initial=N (overrides init_txt)
    bool             is_latch = false;  // mode "latch": Ntype_op::Latch, wire din+enable only
    // Hierarchical naming (call-site `name=` on an inlined comb / `reg
    // x::[name=]`):
    std::string      name_override;  // explicit `name=` — replaces the local flop name
    std::string      hier_prefix;    // runner-stamped `__hier` instance path (e.g.
                                     // "pipeB_ex_mem")
  };

  // Shadow pin_map_ keys for a reg's next-state value and write-enable. The
  // \x01 prefix cannot collide with user identifiers or `___N` temps.
  static constexpr std::string_view kDinPrefix{
      "\x01"
      "din:"};
  [[nodiscard]] static std::string din_key(std::string_view n) { return std::string(kDinPrefix).append(n); }
  [[nodiscard]] static std::string en_key(std::string_view n) {
    return std::string(
               "\x01"
               "en:")
        .append(n);
  }

  // The "hold" value for a reg's din shadow on an unwritten conditional path:
  // the reg's q (current value), so a conditional write auto-holds (Verilog
  // non-blocking `<=` semantics) even when no branch reads the reg (a pure
  // write). Without it the unwritten path falls back to a don't-care, which is
  // only masked by the enable shadow — fragile, and it balloons the merge width
  // (nil_pin() is 64 bits). Returns nullopt for a NON-reg merge var (e.g. a
  // combinational match-expression result), which legitimately keeps its
  // don't-care none-of slot. `var` is a din shadow key iff it carries the
  // din_key() `\x01din:` prefix.
  [[nodiscard]] std::optional<Pin> reg_hold_pin(std::string_view var) {
    constexpr std::string_view din_prefix{
        "\x01"
        "din:"};
    if (!var.starts_with(din_prefix)) {
      return std::nullopt;
    }
    std::string name(var.substr(din_prefix.size()));
    if (auto it = reg_map_.find(name); it != reg_map_.end()) {
      return it->second.create_driver_pin(0);  // flop q = current registered value
    }
    return std::nullopt;
  }

  // 2c-wire — is `var` the din shadow key of a declared `wire`? Unlike a reg, a
  // wire has NO hold value on a branch path that does not write it: the net is
  // defined by its ONE driver, so an unwritten path is a DON'T-CARE, not an X.
  // A conditionally written wire therefore carries the driver's value on EVERY
  // path — exactly as if the assignment had been written unconditionally (the
  // frontend allows the conditional form for that reason). The branch merges
  // below fill the unwritten paths with a WRITTEN value instead of nil, so a
  // single writing arm needs no mux at all.
  [[nodiscard]] bool is_wire_din(std::string_view var) const {
    // Heterogeneous lookup: this runs per merge variable, so do not mint a
    // std::string just to probe the set.
    return var.starts_with(kDinPrefix) && wire_names_.contains(var.substr(kDinPrefix.size()));
  }

  // The same don't-care rule for a `const` still carrying no value at this
  // point. `const` is SINGLE-ASSIGNMENT: the one bind defines it, so — exactly
  // like a `wire` — a branch path that does not write it is a don't-care, and
  // `const x:T = nil; if c { x = v }` means `x == v`, not `c ? v : x`.
  // `mut` is deliberately NOT included: it is last-write-wins, so an unwritten
  // path legitimately keeps the pre-if value, and slang's poison-init
  // accumulators DEPEND on that value staying the `0sb?`/nil seed.
  // Only reached with no pre-if value in pin_map_, i.e. genuinely unbound.
  [[nodiscard]] bool is_unbound_const(std::string_view var) const {
    return !const_decl_.empty() && !var.empty() && var.front() != '\x01' && const_decl_.contains(logical_key(var));
  }

  // Either single-driver net shape: a `wire` din shadow, or a still-unbound
  // `const`. Both fill an unwritten branch path with a WRITTEN value instead
  // of nil, so a single writing arm needs no mux at all.
  [[nodiscard]] bool is_single_bind_net(std::string_view var) const { return is_wire_din(var) || is_unbound_const(var); }

  // Cached 1/0 const pins for the enable shadow (node identity doubles as the
  // "still unconditionally true/false" test in finalize_regs).
  [[nodiscard]] Pin en_const(bool v) {
    auto& pin   = v ? en_true_pin_ : en_false_pin_;
    auto& valid = v ? en_true_valid_ : en_false_valid_;
    if (!valid) {
      pin   = create_const(*g_, *Dlop::create_integer(v ? 1 : 0));
      valid = true;
    }
    return pin;
  }

  // ── 1a-mem: array-typed reg → Ntype_op::Memory
  // ────────────────────────────── One Memory cell per declared `reg
  // name:[N]T`; one write port per store site and one read port per tuple_get
  // site (no port merging here — that is a future LG pass). Per-port sink pids
  // stride by 12 (graph/cell.cpp); the r-th read port's data comes out on
  // driver pid (n_wr_total + r), so the write-site count is pre-scanned at the
  // declare.
  struct Mem_info {
    hhds::Node_class     node;
    int64_t              size = 0;         // total entries (∏dims)
    std::vector<int64_t> dims;             // outer dim first; size 1 for a flat array
    int32_t              elem_mw     = 0;  // element max-value width
    bool                 elem_signed = false;
    bool                 is_array    = false;  // type=2: mut/const array (no clock, no persistence)
    bool                 is_pub      = false;  // pub reg: a remote regref may attach accesses — no diagnostics
    bool                 init_wired  = false;
    int                  n_wr_total  = 0;  // user sites + the restore port (fixes dout pids)
    int                  n_user_wr   = 0;  // pre-scanned program write sites
    int                  wr_next     = 0;
    int                  rd_next     = 0;
    // Same-cycle ordering (Pyrope `ordering` attr): "program" (default) needs
    // each read port's POSITION in program order, so record `wr_next` as each
    // read port is minted — the number of program writes that textually
    // precede it. finalize_mems() turns this into the per-(read,write) `fwd`
    // matrix. "fwd" forwards every write to every read (position-blind),
    // "old" forwards nothing and every read is the DEFINED committed value,
    // "none" forwards nothing and a colliding read is UNDEFINED (the `undef`
    // matrix, graph/cell.cpp pid 15). "old" vs "none" is the distinction a
    // single `fwd` bit cannot make; the Verilog readers need "old" because a
    // nonblocking write is never visible to a same-timestep read.
    enum class Mem_order { program, fwd, old, none };
    int64_t                      legacy_fwd_mask = 0;  // set when the deprecated `fwd=` attr is used
    bool                         has_legacy_fwd  = false;
    std::vector<int>             rd_wr_before;  // per read port: writes minted before it
    // 1a-mem reset-restore — per-entry reset values: finalize_mems() turns
    // these into ONE restore write port (addr = a sweep counter, din =
    // init[addr], enable=reset) and gates the user ports' enables with !reset.
    // The restore port stays OUT of the fwd mask: a read during reset returns
    // the committed (old) contents.
    std::vector<spool_ptr<Dlop>> restore_vals;
    // Whole-array support: a runtime `mem = <bus>` store drives the cell's
    // `update` sink (size*elem_mw bus) instead of minting per-entry write
    // ports. A whole `x = mem` read materializes the async `read_all` driver
    // pin (cached so repeated reads share one output). For a registered array
    // the reset value bus rides the (now runtime-capable) `init` sink + the
    // `reset` cond pin.
    bool                         has_update = false;  // an update bus is wired (whole-array memory)
    Pin                          read_all_pin{};      // cached async read_all driver pin
    // Accumulator for MULTIPLE conditional whole-array stores (e.g. a reset arm
    // and a flush arm). Each later store folds into one
    // `update`/`update_enable` pair via a priority mux: `update_val = en ? this
    // : update_val` (later store wins where its path-cond holds) and `update_en
    // = update_en | en`. The if/else-if path conditions already encode source
    // priority (later arms negate earlier conditions), so "later wins" matches
    // Verilog nonblocking semantics.
    Pin                          update_val{};  // current accumulated update bus value
    Pin                          update_en{};   // current accumulated update enable (invalid => always-on)
  };

  // Packed scalar SSA version of a combinational typed positional array.
  // The value itself lives in pin_map_; this side record preserves the array
  // extent and lane type so tuple_get can recover element semantics.
  struct Array_scalar_view {
    int64_t              size = 0;
    std::vector<int64_t> dims;
    int32_t              elem_mw     = 0;
    bool                 elem_signed = false;
  };

  static constexpr int kMemPortStride = static_cast<int>(Ntype::Memory_port_stride);

  // Get-or-create the cell's async `read_all` driver pin (the whole-array read,
  // size*elem_mw bits wide, entry 0 in the low elem_mw). Cached on the Mem_info
  // so repeated whole reads share one output. Sits at the reserved driver pid
  // Memory_readall_pid (never collides with the sequential read douts).
  [[nodiscard]] Pin get_or_make_read_all(Mem_info& mi) {
    if (!mi.read_all_pin.is_invalid()) {
      return mi.read_all_pin;
    }
    auto d = mi.node.create_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
    set_ubits(d, static_cast<int>(mi.size * mi.elem_mw));
    mi.read_all_pin = d;
    return d;
  }  // Memory per-port sink stride, graph/cell.hpp

  // Branch path condition for memory write enables AND for property guards.
  //
  // The stack holds UNMATERIALIZED terms — the branch condition pins, which
  // exist anyway as mux selectors — and the and2/not1/nonzero1 chain is built
  // only when a consumer actually asks. That is why tracking can now be
  // UNCONDITIONAL: an `if` in a body with no memory and no property mints no
  // cells at all, so there is nothing to dead-strip and no node-id churn (the
  // measured symptom of always materializing was every emitted signal in every
  // design with an `if` getting renumbered).
  //
  // Unconditional tracking is what makes the path condition correct rather than
  // discovery-ordered. The old gate was `!mem_map_.empty()`, evaluated once when
  // lower_if was ENTERED, so a memory DECLARED INSIDE a branch was invisible:
  // `if c1 { reg m:[4]u8 = …; if c2 { m[a] = d } }` emitted `wr_enable = c2`,
  // dropping c1 entirely and writing the memory on a cycle the source does not.
  // Arming it from a property pre-scan instead only moved the seam — the same
  // body then lowered differently depending on whether an unrelated assert
  // existed elsewhere in it.
  //
  // Folds are memoized per prefix, so N consumers under one branch share cells.
  [[nodiscard]] Pin current_path_cond() {
    if (path_terms_.empty()) {
      return Pin{};
    }
    size_t i = 0;
    while (i < path_folded_.size() && !path_folded_[i].is_invalid()) {
      ++i;
    }
    Pin acc = i == 0 ? Pin{} : path_folded_[i - 1];
    for (; i < path_terms_.size(); ++i) {
      // nonzero1 FIRST: and2/not1 stamp bits=1, so a multi-bit branch condition
      // fed in raw would contribute only its LSB and silently narrow the path.
      Pin one = nonzero1(path_terms_[i].cond);
      if (path_terms_[i].negated) {
        one = not1(one);
      }
      acc             = and2(acc, one);
      path_folded_[i] = acc;
    }
    return acc;
  }

  // Full execution context for state, calls and source-visible effects. A
  // definition's transported activation composes with its local branch path;
  // an invalid term denotes constant true and therefore mints no glue.
  [[nodiscard]] Pin effect_path_cond() {
    const auto local = current_path_cond();
    return !valid_active_ ? local : and2(valid_pin(), local);
  }

  // Push one term (a branch condition, or its negation for a later arm/else).
  void push_path_term(const Pin& cond, bool negated) {
    path_terms_.push_back({cond, negated});
    path_folded_.emplace_back();  // lazily materialized by current_path_cond()
  }
  void truncate_path_terms(size_t depth) {
    path_terms_.resize(depth);
    path_folded_.resize(depth);
  }

  // a AND b as a 1-bit unsigned pin; an invalid operand means "true".
  [[nodiscard]] Pin and2(const Pin& a, const Pin& b) {
    if (a.is_invalid()) {
      return b;
    }
    if (b.is_invalid()) {
      return a;
    }
    auto node = make_node(Ntype_op::And);
    node.create_sink_pin(0).connect_driver(a);
    node.create_sink_pin(0).connect_driver(b);
    auto d = node.create_driver_pin(0);
    set_ubits(d, 1);
    return d;
  }

  // a OR b as an unsigned Boolean value; an invalid operand is the identity (returns
  // the other), so an unguarded caller mints no cell at all.
  [[nodiscard]] Pin or2(const Pin& a, const Pin& b) {
    if (a.is_invalid()) {
      return b;
    }
    if (b.is_invalid()) {
      return a;
    }
    const auto lhs  = nonzero1(a);
    const auto rhs  = nonzero1(b);
    auto       node = make_node(Ntype_op::Or);
    node.create_sink_pin(0).connect_driver(lhs);
    node.create_sink_pin(0).connect_driver(rhs);
    auto d = node.create_driver_pin(0);
    set_ubits(d, 1);
    return d;
  }

  // A glitch-free clock gate. `en` is sampled by the backend on the inactive
  // clock phase; div=1/invert=false are explicit so every consumer sees the
  // same v1 contract rather than relying on implicit pin defaults.
  [[nodiscard]] Pin clock_gate(const Pin& clk, const Pin& en) {
    if (clk.is_invalid() || en.is_invalid()) {
      return clk;
    }
    auto cell = make_node(Ntype_op::Clock_cell);
    setup_sink_by_name(cell, "clk_ref").connect_driver(clk);
    setup_sink_by_name(cell, "div").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    setup_sink_by_name(cell, "en").connect_driver(nonzero1(en));
    setup_sink_by_name(cell, "invert").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    auto out = cell.create_driver_pin(0);
    set_ubits(out, 1);
    return out;
  }

  // "a != 0" as a 1-bit unsigned pin: an OR-reduction over every bit, which is
  // exactly the nonzero test regardless of width or signedness (a two's
  // complement value is nonzero iff some bit is set).
  //
  // Needed because and2/or2/not1 all stamp their driver `bits=1, unsigned`.
  // Feeding a multi-bit value straight into one of them therefore keeps
  // only its LSB. That is harmless for a comparison result (already 1 bit) and a
  // silent miscompile for anything wider, so a wide operand must be reduced
  // BEFORE it reaches them. A 1-bit input makes this a no-op the folder removes.
  [[nodiscard]] Pin nonzero1(const Pin& a) {
    if (a.is_invalid()) {
      return a;
    }
    // A 1-bit operand already IS its own nonzero test, so return it untouched.
    // This is not just an optimization: the path condition feeds SYNTHESIZABLE
    // logic (a memory write enable), and every cell minted here has to survive
    // pass.abc. Practically every branch condition is a comparison or a bool, so
    // the common path must add nothing at all — and does not.
    if (pin_mw_of(a) <= 1) {
      return a;
    }
    // Wider: `a != 0` as EQ-to-zero plus a NOT. Deliberately NOT Ntype_op::Ror,
    // which is the obvious spelling and has no combinational bit-blast in
    // pass.abc — a Ror on a memory write-enable cone made `lhd pass abc` fail
    // with "cell 'ror' ... has no combinational bit-blast yet". eq and not are
    // both in abc's supported set.
    auto eq = make_node(Ntype_op::EQ);
    eq.create_sink_pin(0).connect_driver(a);
    eq.create_sink_pin(0).connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    auto z = eq.create_driver_pin(0);
    set_ubits(z, 1);
    return not1(z);
  }

  // Truth-value negation. Keep this robust at transported GraphIO/Sub
  // boundaries where the base pin may not carry the declaration's width attr:
  // EQ-to-zero is exact for both an honest u1 and any wider condition.
  [[nodiscard]] Pin not1(const Pin& a) {
    auto node = make_node(Ntype_op::EQ);
    node.create_sink_pin(0).connect_driver(a);
    node.create_sink_pin(0).connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    auto d = node.create_driver_pin(0);
    set_ubits(d, 1);
    return d;
  }

  // A 1-bit condition shifted to one-hot position `amount` (unique-if
  // selector packing). The value reaches 1<<amount (amount+1 magnitude
  // bits), hence an unsigned literal width of amount+1.
  [[nodiscard]] Pin shl1_by(const Pin& a, int amount) {
    if (amount == 0) {
      return a;
    }
    auto node = make_node(Ntype_op::SHL);
    setup_sink_by_name(node, "a").connect_driver(a);
    setup_sink_by_name(node, "b").connect_driver(create_const(*g_, *Dlop::create_integer(amount)));
    auto d = node.create_driver_pin(0);
    set_ubits(d, amount + 1);
    return d;
  }

  // Row-major flat address for a chained index list over `mi.dims`:
  // addr = ((i0*D1 + i1)*D2 + i2)… (Horner). Const indices fold at build
  // time; a runtime index materializes the accumulator and emits Mult/Sum
  // cells (widths mirror lower_op: mul = sum of operand mws, add = max+1).
  // Returns an invalid Pin after reporting (index-arity mismatch, non-integer
  // index, field access).
  [[nodiscard]] Pin flatten_mem_addr(const Mem_info& mi, const std::vector<Lnast_nid>& idxs, std::string_view name) {
    if (idxs.size() != mi.dims.size()) {
      error_here(
          "upass.tolg: memory '{}' has {} dimension(s) but the access "
          "supplies {} index(es)",
          name,
          mi.dims.size(),
          idxs.size());
      return {};
    }
    // A const index must be an integer — a string key would be a field
    // access, which memories don't have.
    auto const_index_of = [&](const Lnast_nid& nid, std::optional<int64_t>& out) -> bool {
      if (!Lnast_ntype::is_const(lnast_->get_type(nid))) {
        return true;  // runtime ref — resolved through leaf()
      }
      auto v = Dlop::from_pyrope(lnast_->get_name(nid));
      if (!v || !v->is_just_i64()) {
        error_here(
            "upass.tolg: memory '{}' index '{}' is not an integer — "
            "field access on a memory is not supported",
            name,
            lnast_->get_name(nid));
        return false;
      }
      out = v->to_just_i64();
      return true;
    };

    std::optional<int64_t> acc_c;
    Pin                    acc_p{};
    int32_t                acc_mw = 0;
    if (!const_index_of(idxs[0], acc_c)) {
      return {};
    }
    if (!acc_c) {
      auto v = leaf(idxs[0]);
      acc_p  = v.pin;
      acc_mw = v.mw;
    }
    for (size_t k = 1; k < idxs.size(); ++k) {
      const int64_t          d = mi.dims[k];
      std::optional<int64_t> ic;
      if (!const_index_of(idxs[k], ic)) {
        return {};
      }
      if (acc_c && ic) {
        acc_c = *acc_c * d + *ic;
        continue;
      }
      if (acc_c) {  // runtime index joins a const accumulator
        acc_p  = create_const(*g_, *Dlop::create_integer(*acc_c));
        acc_mw = mw_of_val(*acc_c);
        acc_c.reset();
      }
      if (d != 1) {
        auto mul = make_node(Ntype_op::Mult);
        setup_sink_by_name(mul, "as").connect_driver(acc_p);
        setup_sink_by_name(mul, "as").connect_driver(create_const(*g_, *Dlop::create_integer(d)));
        auto md  = mul.create_driver_pin(0);
        acc_mw  += mw_of_val(d);
        set_ubits(md, acc_mw);
        acc_p = md;
      }
      Pin     ip{};
      int32_t imw = 0;
      if (ic) {
        if (*ic == 0) {
          continue;  // + 0 — skip the Sum
        }
        ip  = create_const(*g_, *Dlop::create_integer(*ic));
        imw = mw_of_val(*ic);
      } else {
        auto v = leaf(idxs[k]);
        ip     = v.pin;
        imw    = v.mw;
      }
      auto add = make_node(Ntype_op::Sum);
      setup_sink_by_name(add, "as").connect_driver(acc_p);
      setup_sink_by_name(add, "as").connect_driver(ip);
      auto ad = add.create_driver_pin(0);
      acc_mw  = std::max(acc_mw, imw) + 1;
      set_ubits(ad, acc_mw);
      acc_p = ad;
    }
    if (acc_c) {
      return create_const(*g_, *Dlop::create_integer(*acc_c));
    }
    return acc_p;
  }

  // One immutable-tree pre-scan for indexed stores.  The former implementation
  // repeated this full recursive walk once per memory declaration.
  void index_mem_write_sites() {
    mem_write_site_counts_.clear();
    std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
      if (lnast_->is_dce_dead(nid)) {
        return;  // dce:mark — the lowering skips dead stores; counting them
                 // here would desync the pre-scan exactly like the decl-store
      }
      if (Lnast_ntype::is_store(lnast_->get_type(nid))) {
        auto c0 = lnast_->get_first_child(nid);
        if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast_->get_type(c0))) {
          auto c1 = lnast_->get_sibling_next(c0);
          if (!c1.is_invalid()) {
            auto c2 = lnast_->get_sibling_next(c1);
            // A real memory write is store(mem, idx, val); a typed declaration
            // — store(name, init, TYPE) — also has 3 children but its last is a
            // type node (an unpacked-array OUTPUT port emits both a flat packed
            // `= nil : int` decl-store AND its comp_type_array memory declare).
            // Counting the decl-store as a write desyncs the pre-scan from the
            // lowering (which ignores it) — skip type-tailed stores.
            if (!c2.is_invalid() && !Lnast_ntype::is_type(lnast_->get_type(c2))) {
              ++mem_write_site_counts_[lnast_->get_name_id(c0)];
            }
          }
        }
      }
      for (auto c = lnast_->get_first_child(nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
        walk(c);
      }
    };
    walk(lnast_->get_root());
  }

  [[nodiscard]] int count_mem_write_sites(const Lnast_nid& name_nid) const {
    if (auto it = mem_write_site_counts_.find(lnast_->get_name_id(name_nid)); it != mem_write_site_counts_.end()) {
      return it->second;
    }
    return 0;
  }

  // A mut/const positional array is combinational aggregate storage, not a
  // persistent Memory. Preserve its declared shape while representing the live
  // value as one packed scalar bus; indexed reads/writes below recover lanes.
  // This keeps the logical LNAST array intact and leaves any physical
  // per-lane expansion to downstream transformations.
  //
  // ONE-DIMENSIONAL by construction: lower_declare sends a nested
  // `comp_type_array` element (and any inline initializer) to lower_mem_declare
  // instead, so the element type reaching here is always a scalar. A nested
  // element that ever did reach here would take the "sized scalar element type"
  // error below (declared_width of an array type is 0), never a silent
  // mis-lowering — so there is no multi-dimension walk to maintain here.
  void lower_comb_array_declare(const Lnast_nid& name_nid, const Lnast_nid& type_nid) {
    auto    elem_nid = lnast_->get_first_child(type_nid);
    auto    len_nid  = elem_nid.is_invalid() ? elem_nid : lnast_->get_sibling_next(elem_nid);
    int64_t size     = 0;
    if (!elem_nid.is_invalid() && !len_nid.is_invalid()) {
      std::string len_txt{lnast_->get_name(len_nid)};
      if (len_txt.size() >= 2 && len_txt.front() == '[' && len_txt.back() == ']') {
        len_txt = len_txt.substr(1, len_txt.size() - 2);
      }
      auto d = Dlop::from_pyrope(len_txt);
      if (!d || !d->is_just_i64() || d->to_just_i64() <= 0) {
        error_here("upass.tolg: array '{}' size '{}' is not a positive comptime constant",
                   lnast_->get_name(name_nid),
                   lnast_->get_name(len_nid));
        return;
      }
      size = d->to_just_i64();
    }
    const auto [elem_mw, elem_signed] = declared_width(elem_nid);
    if (size <= 0 || elem_mw <= 0) {
      error_here("upass.tolg: array '{}' requires a sized scalar element type", lnast_->get_name(name_nid));
      return;
    }
    array_scalar_views_[std::string(lnast_->get_name(name_nid))] = Array_scalar_view{
        .size        = size,
        .dims        = {size},
        .elem_mw     = elem_mw,
        .elem_signed = elem_signed,
    };
  }

  // declare(ref name, comp_type_array(elem_type, const '[N]'), const mode
  // [, init]) — two flavors sharing one lowering:
  //  * reg  → async memory (type=0, fwd=1, 0-cycle read): writes commit at
  //    the cycle edge, same-cycle reads see them through forwarding. Only a
  //    nil/0sb? initializer is accepted in this slice (no reset hardware;
  //    the reset-sweep FSM is a later slice).
  //  * mut/const → comb array (type=2, no clock, no cross-cycle
  //    persistence): the per-cycle default is the init contents (the
  //    whole-array store wires the `init` pin); a const array with runtime
  //    reads is a ROM (init + read ports only).
  void lower_mem_declare(const Lnast_nid& name_nid, const Lnast_nid& type_nid, const Lnast_nid& mode_nid, bool is_array) {
    auto name     = lnast_->get_name(name_nid);
    auto elem_nid = lnast_->get_first_child(type_nid);
    auto len_nid  = elem_nid.is_invalid() ? elem_nid : lnast_->get_sibling_next(elem_nid);
    if (elem_nid.is_invalid() || len_nid.is_invalid()) {
      error_here(
          "upass.tolg: memory '{}' array type is missing its element "
          "type or size",
          name);
      return;
    }
    // Collect the dimension chain — each comp_type_array level is
    // (elem | nested comp_type_array, const '[N]'), nested OUTER dim first
    // (`[4][8]u8` → top len is '[4]'). The flat entry layout is row-major
    // (matrix_partial.prp contract): index (i,j) over dims (D0,D1) lands at
    // flat address i*D1 + j.
    std::vector<int64_t> dims;
    while (true) {
      // The size const's text is the raw '[N]' annotation — strip the brackets.
      auto len_txt = std::string(lnast_->get_name(len_nid));
      if (len_txt.size() >= 2 && len_txt.front() == '[' && len_txt.back() == ']') {
        len_txt = len_txt.substr(1, len_txt.size() - 2);
      }
      int64_t d = 0;
      if (auto c = Dlop::from_pyrope(len_txt); c && c->is_just_i64()) {
        d = c->to_just_i64();
      }
      if (d <= 0) {
        error_here(
            "upass.tolg: memory '{}' size '{}' is not a positive "
            "comptime constant",
            name,
            lnast_->get_name(len_nid));
        return;
      }
      dims.emplace_back(d);
      if (!Lnast_ntype::is_comp_type_array(lnast_->get_type(elem_nid))) {
        break;
      }
      auto inner_elem = lnast_->get_first_child(elem_nid);
      auto inner_len  = inner_elem.is_invalid() ? inner_elem : lnast_->get_sibling_next(inner_elem);
      if (inner_elem.is_invalid() || inner_len.is_invalid()) {
        error_here(
            "upass.tolg: memory '{}' array type is missing its element "
            "type or size",
            name);
        return;
      }
      elem_nid = inner_elem;
      len_nid  = inner_len;
    }
    auto [elem_mw, elem_signed] = declared_width(elem_nid);
    if (elem_mw == 0) {
      error_here(
          "upass.tolg: memory '{}' element type must be a sized integer "
          "or bool",
          name);
      return;
    }
    int64_t size = 1;
    for (auto d : dims) {
      size *= d;
    }
    // reg initializer — same treatment as a mut array (reg and not-reg
    // initialize alike): a concrete value becomes POWER-ON contents on the
    // `init` pin (a scalar broadcasts to every entry; a tuple literal packs
    // per entry). nil / 0sb? = uninitialized. It is ALSO the RESET value of
    // every entry (the same statement `= <const>` makes on a scalar reg), so
    // the module has a reset by construction and finalize_mems() builds the
    // one-entry-per-cycle restore SWEEP from these values. mut/const arrays get
    // theirs via the whole-array store instead.
    spool_ptr<Dlop>                       reg_init;
    std::vector<spool_ptr<Dlop>>          init_entries;
    // Read an INLINE init child on the declare. The Pyrope frontend gives
    // mut/const arrays their init via a separate whole-array store (so an array
    // declare has no child after `mode`, and this loop is a no-op for it); the
    // slang reader instead emits the `initial` contents INLINE as a scalar
    // const or a tuple_add literal on the declare — for BOTH regs and arrays.
    // Reading it here for arrays too lands the contents on the type==2 `init`
    // pin with NO reset-restore (wants_restore is gated on !is_array below),
    // i.e. pure power-on init: a `mut`/`const` array has no clock, so there is
    // nothing for a reset to re-load. A reg array DOES restore its init — a
    // memory still has no parallel reset port, so finalize_mems() realizes it
    // as a one-entry-per-cycle sweep. Flatten an inline tuple literal's
    // constant leaves row-major into entries.
    std::function<bool(const Lnast_nid&)> flatten_lit = [&](const Lnast_nid& tnid) -> bool {
      for (auto ch = lnast_->get_first_child(tnid); !ch.is_invalid(); ch = lnast_->get_sibling_next(ch)) {
        const auto cht = lnast_->get_type(ch);
        if (Lnast_ntype::is_tuple_add(cht)) {
          if (!flatten_lit(ch)) {
            return false;
          }
        } else if (Lnast_ntype::is_const(cht)) {
          auto v = Dlop::from_pyrope(lnast_->get_name(ch));
          if (!v || !v->is_just_i64()) {
            error_here(
                "upass.tolg: memory '{}' initializer '{}' is not an "
                "integer constant",
                name,
                lnast_->get_name(ch));
            return false;
          }
          init_entries.emplace_back(v->and_op(*Dlop::get_mask_value(elem_mw)));
        } else if (!Lnast_ntype::is_ref(cht)) {  // a leading self-ref (tuple target) is skipped
          error_here(
              "upass.tolg: memory '{}' initializer must be a comptime "
              "constant or tuple literal",
              name);
          return false;
        }
      }
      return true;
    };
    for (auto c = lnast_->get_sibling_next(mode_nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      const auto ct = lnast_->get_type(c);
      if (Lnast_ntype::is_stages(ct)) {
        error_here("upass.tolg: memory '{}' cannot carry a stage[] qualifier", name);
        return;
      }
      if (Lnast_ntype::is_const(ct)) {
        auto txt = lnast_->get_name(c);
        if (txt == "nil" || txt == "0sb?") {
          break;
        }
        auto v = Dlop::from_pyrope(txt);
        if (!v || !v->is_just_i64()) {
          error_here(
              "upass.tolg: memory '{}' initializer '{}' is not an "
              "integer constant",
              name,
              txt);
          return;
        }
        // Scalar broadcast: every entry = value (masked to the element).
        auto mask  = Dlop::get_mask_value(elem_mw);
        auto entry = v->and_op(*mask);
        reg_init   = Dlop::create_integer(0);
        for (int64_t i = 0; i < size; ++i) {
          reg_init = reg_init->or_op(*entry->shl_op(*Dlop::create_integer(i * elem_mw)));
          init_entries.emplace_back(entry);
        }
        break;
      }
      if (Lnast_ntype::is_ref(ct)) {
        auto tit = tuple_recs_.find(std::string(lnast_->get_name(c)));
        if (tit == tuple_recs_.end() || !tit->second.named.empty()) {
          error_here(
              "upass.tolg: memory '{}' initializer must be a comptime "
              "constant or tuple literal",
              name);
          return;
        }
        // Row-major flatten; nested literals must match the dims chain.
        if (!flatten_init_values(tit->second, dims, 0, name, elem_mw, init_entries)) {
          return;  // flatten_init_values reported
        }
        reg_init = pack_entries(init_entries, elem_mw);
        break;
      }
      if (Lnast_ntype::is_tuple_add(ct)) {  // slang's INLINE per-entry init literal
        if (!flatten_lit(c)) {
          return;  // flatten_lit reported
        }
        reg_init = pack_entries(init_entries, elem_mw);
        break;
      }
      error_here(
          "upass.tolg: memory '{}' initializer must be a comptime "
          "constant or tuple literal",
          name);
      return;
    }

    const int  user_sites      = count_mem_write_sites(name_nid);
    // `!init_entries.empty()` is not redundant with `reg_init`: it is the SAME
    // predicate finalize_mems() mints the restore port with. A zero-entry array
    // leaves `reg_init` set (the broadcast loop simply never runs) with no
    // per-entry values, and budgeting a port here that finalize_mems then
    // declines to mint would punch a hole in the write-port block -- every read
    // dout is recovered by COUNTING write ports (`n_write + r`), so the reads
    // would silently bind to the wrong driver pid.
    const bool wants_restore   = !is_array && reg_init && !init_entries.empty() && !reset_name_.empty();
    // Same-cycle ordering: the `fwd` sink is a per-(read,write) MATRIX that
    // finalize_mems() builds once every port is minted and each read port's
    // program position is known (`rd_wr_before`). The `ordering` attr is read
    // there too, not here — a hand-written Pyrope declaration emits its
    // attr_set AFTER the declare, so pending_attrs_ is not populated yet (the
    // same reason the clock wiring is deferred). The value driven below is
    // provisional, and is the final one only for the two cases finalize_mems
    // leaves alone: a `mut`/`const` array and a legacy `fwd=` escape hatch.
    int64_t    legacy_fwd_mask = 0;
    bool       has_legacy_fwd  = false;
    if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
      // Deprecated numeric `fwd=`: an explicit matrix, taken verbatim (a
      // per-WRITE-port mask still reads correctly on a 1-read memory, which is
      // every historical user).
      if (auto fit = pit->second.find("fwd"); fit != pit->second.end()) {
        if (auto fv = Dlop::from_pyrope(fit->second); fv && fv->is_just_i64()) {
          legacy_fwd_mask = fv->to_just_i64();
          has_legacy_fwd  = true;
        }
      }
    }
    // A `mut`/`const` array (type=2) has no clock: tolg lowers it
    // writes-before-reads (a read after a write is a hard error), so every
    // write is visible to every read and the matrix is irrelevant — the comb
    // encoders read the post-write array unconditionally.
    int64_t fwd_mask = is_array ? 1 : (int64_t{1} << user_sites) - 1;
    if (has_legacy_fwd) {
      fwd_mask = legacy_fwd_mask;
    }

    auto mem = make_node(Ntype_op::Memory);
    // Stamp the declared RTL name on the Memory NODE (SSA suffix stripped),
    // mirroring the flop path (~L1634): hhds get_hier_name() otherwise falls
    // back to the positional `n<id>`, which forces pass/lec to pair memories
    // ANONYMOUSLY by shape + occurrence ordinal — two front-ends that
    // enumerate reads/memories in a different order then tie DIFFERENT
    // logical arrays (or read ports) to one shared symbol and falsely refute.
    // The reader's detupled per-field regs give unique names in both flows
    // (e.g. `msg_port_conf.umode`).
    {
      std::string mem_base{name};
      if (auto p = mem_base.find("___ssa_"); p != std::string::npos) {
        mem_base.resize(p);
      }
      if (!mem_base.empty()) {
        mem.set_name(std::string(canon_io_name(mem_base)));
      }
    }
    setup_sink_by_name(mem, "bits").connect_driver(create_const(*g_, *Dlop::create_integer(elem_mw)));
    setup_sink_by_name(mem, "size").connect_driver(create_const(*g_, *Dlop::create_integer(size)));
    setup_sink_by_name(mem, "type").connect_driver(create_const(*g_, *Dlop::create_integer(is_array ? 2 : 0)));
    setup_sink_by_name(mem, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(fwd_mask)));
    setup_sink_by_name(mem, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    if (reg_init) {
      setup_sink_by_name(mem, "init").connect_driver(create_const(*g_, *reg_init));
    }
    // Clock wiring (posclk + clock_pin) for a clocked (non-array) memory is
    // deferred to finalize_mems: the slang reader emits the clock_pin/posclk
    // attr_set AFTER this declare in lnast order, so pending_attrs_ is not yet
    // populated here (unlike fwd, which the reader emits before the declare).

    Mem_info info;
    info.node            = mem;
    info.size            = size;
    info.dims            = std::move(dims);
    info.elem_mw         = elem_mw;
    info.elem_signed     = elem_signed;
    info.is_array        = is_array;
    // `pub` on a reg means a remote regref may attach reads/writes later —
    // suppress access diagnostics. Dormant today (prp2lnast restricts `pub`
    // to file scope), but the gate is mode-keyed so it activates with regref.
    info.is_pub          = std::string_view(lnast_->get_name(mode_nid)).find("pub") != std::string_view::npos;
    info.n_user_wr       = user_sites;
    // One restore port, not `size` of them: the reset re-load is a
    // one-write-per-cycle SWEEP (finalize_mems), so the port block grows by a
    // single slot no matter how many entries the array has.
    info.n_wr_total      = user_sites + (wants_restore ? 1 : 0);
    info.legacy_fwd_mask = legacy_fwd_mask;
    info.has_legacy_fwd  = has_legacy_fwd;
    if (wants_restore) {
      info.restore_vals = std::move(init_entries);
    }
    mem_map_.emplace(std::string(name), info);
    mem_order_.emplace_back(name);
  }

  // A surviving tuple literal, recorded by node id so a memory consumer can
  // resolve its elements later: positional const/ref children land in
  // `elems`, named fields (store children) in `named`. Consumers: the
  // mut/const array initializer (all-const elems → `init` packing) and the
  // __memory(cfg) builtin (named fields + per-port positional lists).
  struct Tuple_rec {
    std::vector<Lnast_nid>                      elems;
    absl::flat_hash_map<std::string, Lnast_nid> named;
  };

  // tuple_add(ref dst, e0 | store(name, v), …) — record the literal. Any
  // other child shape keeps the unhandled warn (nothing can consume it).
  void lower_tuple_add(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    Tuple_rec rec;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      const auto ct = lnast_->get_type(c);
      if (Lnast_ntype::is_const(ct) || Lnast_ntype::is_ref(ct)) {
        rec.elems.emplace_back(c);
        continue;
      }
      if (Lnast_ntype::is_store(ct)) {
        auto k = lnast_->get_first_child(c);
        auto v = k.is_invalid() ? k : lnast_->get_sibling_next(k);
        if (!v.is_invalid() && lnast_->get_sibling_next(v).is_invalid()) {
          rec.named[std::string(lnast_->get_name(k))] = v;
          continue;
        }
      }
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: tuple '{}' has an element that did not fold to a "
               "constant or wire — it has no hardware "
               "lowering (tuples must be fully resolved at compile time)",
               lnast_->get_name(dst));
    }
    tuple_recs_[std::string(lnast_->get_name(dst))] = std::move(rec);
  }

  // tuple_concat(ref dst, op…) — the `...` splice / `++` result. 2f-splice: a
  // tuple op is fully comptime, so by the time it reaches tolg its real data
  // has already folded into wire refs upstream (the runner propagates each
  // operand's runtime field slot-refs through the concat). The surviving node
  // is pure comptime bookkeeping, so RECORD the merged tuple — exactly like
  // lower_tuple_add — instead of warning + dropping the spliced field wires.
  // No hardware is created here (tuple ops never lower to a cell); a downstream
  // whole-tuple read resolves through the record like any other tuple literal.
  void lower_tuple_concat(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    Tuple_rec rec;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      const auto ct = lnast_->get_type(c);
      if (Lnast_ntype::is_ref(ct)) {
        // Splice an operand tuple: merge its recorded fields in order
        // (positional appended, named keyed). Constprop already reported any
        // field overlap.
        if (auto it = tuple_recs_.find(std::string(lnast_->get_name(c))); it != tuple_recs_.end()) {
          for (auto e : it->second.elems) {
            rec.elems.emplace_back(e);
          }
          for (const auto& [k, v] : it->second.named) {
            rec.named[k] = v;
          }
          continue;
        }
        rec.elems.emplace_back(c);  // a bare ref operand — append as one positional field
        continue;
      }
      if (Lnast_ntype::is_const(ct)) {
        rec.elems.emplace_back(c);
        continue;
      }
      if (Lnast_ntype::is_store(ct)) {
        auto k = lnast_->get_first_child(c);
        auto v = k.is_invalid() ? k : lnast_->get_sibling_next(k);
        if (!v.is_invalid() && lnast_->get_sibling_next(v).is_invalid()) {
          rec.named[std::string(lnast_->get_name(k))] = v;
          continue;
        }
      }
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: concatenated tuple '{}' has an operand that did "
               "not fold to a constant or wire — it has no "
               "hardware lowering (tuple `++`/`...` must be fully resolved at "
               "compile time)",
               lnast_->get_name(dst));
    }
    tuple_recs_[std::string(lnast_->get_name(dst))] = std::move(rec);
  }

  // The size*elem_mw-bit value bus a whole-array store contributes to `update`.
  // A runtime ref is the bus itself (leaf). A const scalar broadcasts to every
  // entry (each masked to the element, row-major); a comptime tuple literal
  // packs row-major; `nil`/`0sb?` is zero-filled. Returns an invalid Pin after
  // reporting (an unsupported value shape).
  [[nodiscard]] Pin mem_whole_value_pin(const Lnast_nid& rhs, std::string_view name, Mem_info& mi) {
    const auto rt           = lnast_->get_type(rhs);
    const bool is_tuple_lit = Lnast_ntype::is_ref(rt) && tuple_recs_.find(std::string(lnast_->get_name(rhs))) != tuple_recs_.end();
    if (!Lnast_ntype::is_const(rt) && !is_tuple_lit) {
      return leaf(rhs).pin;  // runtime whole-array bus
    }
    if (Lnast_ntype::is_const(rt)) {
      auto txt = lnast_->get_name(rhs);
      if (txt == "nil" || txt == "0sb?") {
        return create_const(*g_, *Dlop::create_integer(0));  // zero-filled
      }
      auto v = Dlop::from_pyrope(txt);
      if (!v || !v->is_just_i64()) {
        error_here(
            "upass.tolg: whole-array value '{}' for memory '{}' is not "
            "supported — use an integer, a tuple literal or nil",
            txt,
            name);
        return {};
      }
      auto entry  = v->and_op(*Dlop::get_mask_value(mi.elem_mw));
      auto packed = Dlop::create_integer(0);
      for (int64_t i = 0; i < mi.size; ++i) {
        packed = packed->or_op(*entry->shl_op(*Dlop::create_integer(i * mi.elem_mw)));
      }
      return create_const(*g_, *packed);
    }
    auto tit = tuple_recs_.find(std::string(lnast_->get_name(rhs)));
    if (tit == tuple_recs_.end() || !tit->second.named.empty()) {
      error_here(
          "upass.tolg: whole-array value for memory '{}' must be a "
          "comptime tuple literal",
          name);
      return {};
    }
    std::vector<spool_ptr<Dlop>> entries;
    if (!flatten_init_values(tit->second, mi.dims, 0, name, mi.elem_mw, entries)) {
      return {};  // flatten_init_values reported
    }
    return create_const(*g_, *pack_entries(entries, mi.elem_mw));
  }

  // Delete the single existing edge to the memory cell's sink `pid` (if any),
  // then drive it with `d` when `d` is valid (invalid => leave it unconnected).
  void redrive_mem_sink(Mem_info& mi, int pid, const Pin& d) {
    for (const auto& e : mi.node.inp_edges()) {
      if (!e.sink.is_invalid() && static_cast<int>(e.sink.get_port_id()) == pid) {
        e.del_edge();
        break;
      }
    }
    if (!d.is_invalid()) {
      mi.node.create_sink_pin(static_cast<hhds::Port_id>(pid)).connect_driver(d);
    }
  }

  // store(ref mem, rhs) — the whole-array form. For a mut/const array this
  // is its initializer: pack the recorded tuple consts into one wide const
  // (entry 0 in the low `bits`, row-major) on the `init` sink. `= nil` means
  // zero-filled (cgen's default).
  // store(mem, <value>) — the whole-array `update` write (runtime bus, const
  // broadcast, or comptime tuple literal). The size*elem_mw bus (entry 0 in the
  // low `elem_mw`, row-major) drives the cell's `update` sink; a conditional
  // whole-write (`if(c) mem=<value>`) carries the branch path-condition into
  // `update_enable` (absent => always-on). MULTIPLE conditional whole-array
  // stores (a reset arm + a flush arm) accumulate into one
  // `update`/`update_enable` pair: the later-lowered store wins where its
  // enable holds (priority mux), and `update_enable` is the OR of every enable;
  // the if/else-if path conditions already encode source priority. The ladder
  // reset > per-port write > (update_enable? update : hold) is realized by
  // cgen/cgen_sim/lec.
  void lower_mem_update_store(const Lnast_nid& rhs, std::string_view name, Mem_info& mi) {
    auto v = mem_whole_value_pin(rhs, name, mi);
    if (v.is_invalid()) {
      return;  // reported, or an empty driver
    }
    auto en = current_path_cond();  // invalid => unconditional
    if (!mi.has_update) {
      setup_sink_by_name(mi.node, "update").connect_driver(v);
      if (!en.is_invalid()) {
        setup_sink_by_name(mi.node, "update_enable").connect_driver(en);
      }
      mi.has_update = true;
      mi.update_val = v;
      mi.update_en  = en;
      // A whole-array read sees COMMITTED state: the clocked bulk update is the
      // next-state, never forwarded to a same-cycle read, and a per-entry write
      // to a registered array is likewise not forwarded (cgen emits `assign
      // dout = data[addr]`). Force fwd=0 so the cvc5 encoder reads a_cur,
      // matching cgen.
      for (const auto& e2 : mi.node.inp_edges()) {
        if (!e2.sink.is_invalid() && static_cast<int>(e2.sink.get_port_id()) == 5) {  // fwd (pid 5)
          e2.del_edge();
          break;
        }
      }
      setup_sink_by_name(mi.node, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
      return;
    }
    // A subsequent conditional whole-array write: later store wins where `en`,
    // else the previously-accumulated value. An unconditional later store
    // (`en` invalid) fully replaces the prior value and makes the bus
    // always-on.
    Pin merged_val;
    if (en.is_invalid()) {
      merged_val = v;
    } else {
      auto mux = make_node(Ntype_op::Mux);
      mux.create_sink_pin(0).connect_driver(en);             // selector
      mux.create_sink_pin(1).connect_driver(mi.update_val);  // false / else = previous value
      mux.create_sink_pin(2).connect_driver(v);              // true / then = this store
      merged_val = mux.create_driver_pin(0);
      // The Memory sink is the declared-width storage boundary; the Mux is an
      // ordinary unbounded operation and must first preserve the widest arm.
      // Stamp the widest literal width, exactly as bind_result does. Constants
      // have no pin-width attribute, so sizing
      // from bits_of alone collapsed an 80-bit fill value to a one-bit Mux.
      set_ubits(merged_val, std::max({1, pin_mw_of(mi.update_val), pin_mw_of(v)}));
    }
    // Combined enable: always-on (invalid) if either contributor is always-on.
    Pin merged_en = (mi.update_en.is_invalid() || en.is_invalid()) ? Pin{} : or2(mi.update_en, en);
    redrive_mem_sink(mi, 12, merged_val);  // update (pid 12)
    redrive_mem_sink(mi, 13, merged_en);   // update_enable (pid 13); invalid =>
                                           // leave unconnected (always-on)
    mi.update_val = merged_val;
    mi.update_en  = merged_en;
  }

  void lower_mem_init_store(const Lnast_nid& rhs, std::string_view name, Mem_info& mi) {
    const auto rt = lnast_->get_type(rhs);
    // A registered (`reg`) memory has NO declaration initializer through this
    // path — its declared init rides lower_mem_declare. Every whole-array store
    // to it is therefore a CONDITIONAL bulk write (a reset arm, a flush arm, a
    // runtime refill, …): route it to the `update` bus regardless of whether
    // the value is a runtime bus, a const broadcast, or a comptime tuple
    // literal. Multiple such stores accumulate (priority mux in
    // lower_mem_update_store).
    if (!mi.is_array) {
      lower_mem_update_store(rhs, name, mi);
      return;
    }
    // A RUNTIME whole-array value (`arr = <bus>` where the rhs is a wire/leaf,
    // not a constant and not a recorded comptime tuple literal) drives the
    // cell's `update` bus: the whole array is (re)written each cycle
    // combinationally
    // (`mut`/`const` array), instead of minting per-entry ports.
    const bool is_tuple_lit = Lnast_ntype::is_ref(rt) && tuple_recs_.find(std::string(lnast_->get_name(rhs))) != tuple_recs_.end();
    if (!Lnast_ntype::is_const(rt) && !is_tuple_lit) {
      lower_mem_update_store(rhs, name, mi);
      return;
    }
    // ---- declaration initializer (comptime const / tuple literal) ----
    if (mi.init_wired) {
      error_here(
          "upass.tolg: array '{}' is re-initialized — only the "
          "declaration initializer is supported",
          name);
      return;
    }
    if (Lnast_ntype::is_const(rt)) {
      auto txt = lnast_->get_name(rhs);
      if (txt == "nil" || txt == "0sb?") {
        mi.init_wired = true;  // zero-filled default
        return;
      }
      // Scalar broadcast: every entry = value (masked to the element) — the
      // same treatment the reg declare-initializer path applies.
      auto v = Dlop::from_pyrope(txt);
      if (!v || !v->is_just_i64()) {
        error_here(
            "upass.tolg: array '{}' initializer '{}' is not supported — "
            "use an integer, a tuple literal or nil",
            name,
            txt);
        return;
      }
      auto entry = v->and_op(*Dlop::get_mask_value(mi.elem_mw));
      auto init  = Dlop::create_integer(0);
      for (int64_t i = 0; i < mi.size; ++i) {
        init = init->or_op(*entry->shl_op(*Dlop::create_integer(i * mi.elem_mw)));
      }
      setup_sink_by_name(mi.node, "init").connect_driver(create_const(*g_, *init));
      mi.init_wired = true;
      return;
    }
    auto tit = tuple_recs_.find(std::string(lnast_->get_name(rhs)));
    if (tit == tuple_recs_.end() || !tit->second.named.empty()) {
      error_here("upass.tolg: array '{}' initializer must be a comptime tuple literal", name);
      return;
    }
    std::vector<spool_ptr<Dlop>> entries;
    if (!flatten_init_values(tit->second, mi.dims, 0, name, mi.elem_mw, entries)) {
      return;  // flatten_init_values reported
    }
    setup_sink_by_name(mi.node, "init").connect_driver(create_const(*g_, *pack_entries(entries, mi.elem_mw)));
    mi.init_wired = true;
  }

  // Flatten an init tuple literal to per-entry masked constants, ROW-MAJOR,
  // validating each level's entry count against the dims chain. A nested
  // dimension's literal arrives as a `ref` to its own recorded tuple_add
  // (`((1,2),(3,4))` → outer elems are refs into tuple_recs_). Returns false
  // after reporting.
  [[nodiscard]] bool flatten_init_values(const Tuple_rec& rec, const std::vector<int64_t>& dims, size_t level,
                                         std::string_view name, int32_t bits, std::vector<spool_ptr<Dlop>>& out) {
    if (static_cast<int64_t>(rec.elems.size()) != dims[level]) {
      error_here(
          "upass.tolg: '{}' initializer has {} entries where dimension "
          "{} holds {}",
          name,
          rec.elems.size(),
          level,
          dims[level]);
      return false;
    }
    auto mask = Dlop::get_mask_value(bits);
    for (size_t i = 0; i < rec.elems.size(); ++i) {
      const auto& e = rec.elems[i];
      if (level + 1 < dims.size()) {
        if (!Lnast_ntype::is_ref(lnast_->get_type(e))) {
          error_here(
              "upass.tolg: '{}' initializer entry {} must be a nested "
              "tuple literal (the memory has {} dimensions)",
              name,
              i,
              dims.size());
          return false;
        }
        auto tit = tuple_recs_.find(std::string(lnast_->get_name(e)));
        if (tit == tuple_recs_.end() || !tit->second.named.empty()) {
          error_here(
              "upass.tolg: '{}' initializer entry {} must be a comptime "
              "tuple literal",
              name,
              i);
          return false;
        }
        if (!flatten_init_values(tit->second, dims, level + 1, name, bits, out)) {
          return false;
        }
        continue;
      }
      if (!Lnast_ntype::is_const(lnast_->get_type(e))) {
        error_here(
            "upass.tolg: '{}' initializer entry {} is not a "
            "compile-time constant",
            name,
            i);
        return false;
      }
      auto v = Dlop::from_pyrope(lnast_->get_name(e));
      if (!v || !v->is_just_i64()) {
        error_here("upass.tolg: '{}' initializer entry {} is not an integer constant", name, i);
        return false;
      }
      out.emplace_back(v->and_op(*mask));
    }
    return true;
  }

  // Pack flat per-entry constants into the wide `init` value: entry 0 in the
  // low `bits`.
  [[nodiscard]] static spool_ptr<Dlop> pack_entries(const std::vector<spool_ptr<Dlop>>& entries, int32_t bits) {
    auto init = Dlop::create_integer(0);
    for (size_t i = 0; i < entries.size(); ++i) {
      init = init->or_op(*entries[i]->shl_op(*Dlop::create_integer(static_cast<int64_t>(i) * bits)));
    }
    return init;
  }

  // 1a-mem — the bound result of a `__memory(cfg)` call: `res[N]` reads the
  // N-th READ port's data (driver pid n_wr + N, port order).
  struct Mem_result {
    hhds::Node_class node;
    int              n_wr = 0;
    int              n_rd = 0;
    int32_t          bits = 0;
  };

  // fcall(ref dst, ref __memory, ref cfg) — direct Memory-cell instantiation
  // (08-memories.md RTL form). The cfg vocabulary is the cell pins VERBATIM
  // (decision 2026-06-09): addr/bits/clock_pin/din/enable/fwd/posclk/type/
  // wensize/size/rdport + init — no `latency`, type picks 0 async / 1 sync /
  // 2 array, rdport entries are strictly 0/1, dout comes back as a tuple
  // indexed by read-port order. Returns false when the call is not __memory.
  bool try_lower_memory_builtin(const Lnast_nid& nid, std::string_view callee_name) {
    if (callee_name != "__memory") {
      return false;
    }
    auto dst      = lnast_->get_first_child(nid);
    auto callee_n = lnast_->get_sibling_next(dst);
    auto arg      = lnast_->get_sibling_next(callee_n);
    if (arg.is_invalid() || !lnast_->get_sibling_next(arg).is_invalid()) {
      error_here("upass.tolg: __memory takes exactly one config tuple in '{}'", lnast_->get_top_module_name());
      return true;
    }
    auto rit = tuple_recs_.find(std::string(lnast_->get_name(arg)));
    if (rit == tuple_recs_.end()) {
      error_here(
          "upass.tolg: __memory config '{}' must be a single tuple "
          "literal (build it as `mut cfg = (addr=…, "
          "bits=…, …)`)",
          lnast_->get_name(arg));
      return true;
    }
    const auto& cfg = rit->second;

    // Guardrail: cell pins verbatim — diagnose the old doc vocabulary.
    static constexpr std::string_view known[]
        = {"addr", "bits", "clock_pin", "din", "enable", "fwd", "undef", "posclk", "type", "wensize", "size", "rdport", "init"};
    for (const auto& [k, v] : cfg.named) {
      if (std::find(std::begin(known), std::end(known), k) == std::end(known)) {
        error_here(
            "upass.tolg: unknown __memory config field '{}' — the "
            "vocabulary is the Memory cell pins verbatim "
            "(addr/bits/clock_pin/din/enable/fwd/undef/posclk/type/"
            "wensize/size/rdport/init; no `latency`, no `clock`)",
            k);
        return true;
      }
    }

    auto cfg_const = [&](std::string_view key, int64_t def, bool required, int64_t& out) -> bool {
      auto it = cfg.named.find(std::string(key));
      if (it == cfg.named.end()) {
        if (required) {
          error_here("upass.tolg: __memory config is missing the required '{}' field", key);
          return false;
        }
        out = def;
        return true;
      }
      if (!Lnast_ntype::is_const(lnast_->get_type(it->second))) {
        error_here(
            "upass.tolg: __memory config field '{}' must be a "
            "compile-time constant",
            key);
        return false;
      }
      auto v = Dlop::from_pyrope(lnast_->get_name(it->second));
      if (!v || !v->is_just_i64()) {
        // bool consts ("false"/"true") are integers in from_pyrope; anything
        // else is a config error.
        error_here("upass.tolg: __memory config field '{}' is not an integer constant", key);
        return false;
      }
      out = v->to_just_i64();
      return true;
    };

    int64_t bits = 0, size = 0, type = 0, fwd = 0, undef = 0, wensize = 1, posclk = 1;
    if (!cfg_const("bits", 0, true, bits) || !cfg_const("size", 0, true, size) || !cfg_const("type", 0, false, type)
        || !cfg_const("fwd", 0, false, fwd) || !cfg_const("undef", 0, false, undef) || !cfg_const("wensize", 1, false, wensize)
        || !cfg_const("posclk", 1, false, posclk)) {
      return true;
    }
    if (bits <= 0 || size <= 0) {
      error_here(
          "upass.tolg: __memory needs positive bits/size (got bits={}, "
          "size={})",
          bits,
          size);
      return true;
    }
    if (type < 0 || type > 2) {
      error_here(
          "upass.tolg: __memory type must be 0 (async), 1 (sync) or 2 "
          "(array) — got {}",
          type);
      return true;
    }

    // Per-port lists: a field is a positional tuple ref or a single scalar.
    auto cfg_list = [&](std::string_view key, std::vector<Lnast_nid>& out) -> bool {
      auto it = cfg.named.find(std::string(key));
      if (it == cfg.named.end()) {
        return true;  // empty
      }
      const auto vt = lnast_->get_type(it->second);
      if (Lnast_ntype::is_ref(vt)) {
        if (auto lit = tuple_recs_.find(std::string(lnast_->get_name(it->second))); lit != tuple_recs_.end()) {
          if (!lit->second.named.empty()) {
            error_here(
                "upass.tolg: __memory config field '{}' must be a "
                "positional tuple",
                key);
            return false;
          }
          out = lit->second.elems;
          return true;
        }
      }
      out = {it->second};  // single scalar = one port
      return true;
    };

    std::vector<Lnast_nid> addrs, clocks, dins, ens, rdports;
    if (!cfg_list("addr", addrs) || !cfg_list("din", dins) || !cfg_list("clock_pin", clocks) || !cfg_list("enable", ens)
        || !cfg_list("rdport", rdports)) {
      return true;
    }
    if (addrs.empty()) {
      error_here("upass.tolg: __memory config needs at least one 'addr' entry");
      return true;
    }
    const int n_ports = static_cast<int>(addrs.size());
    if (static_cast<int>(rdports.size()) != n_ports) {
      error_here("upass.tolg: __memory 'rdport' has {} entries but 'addr' has {}", rdports.size(), n_ports);
      return true;
    }
    if (clocks.size() > 1 && static_cast<int>(clocks.size()) != n_ports) {
      error_here("upass.tolg: __memory 'clock_pin' has {} entries but 'addr' has {} — pass one shared clock or one clock per port",
                 clocks.size(),
                 n_ports);
      return true;
    }

    int n_wr_cfg = 0;
    for (const auto& rp : rdports) {
      if (!Lnast_ntype::is_const(lnast_->get_type(rp))) {
        continue;  // diagnosed in the port loop below
      }
      auto v = Dlop::from_pyrope(lnast_->get_name(rp));
      if (!v || v->is_known_false()) {
        ++n_wr_cfg;
      }
    }
    // `fwd=true` means every write port forwards to every read port: the sink
    // is a per-(read,write) MATRIX (graph/cell.cpp), so the all-ones value
    // spans n_rd*n_wr bits, not n_wr. A value > 1 passes through as an explicit
    // matrix (the RTL escape hatch: __memory's vocabulary is the cell verbatim).
    const int n_rd_cfg            = n_ports - n_wr_cfg;
    const int fwd_bits            = n_rd_cfg * n_wr_cfg;
    // The all-ones expansion is keyed on the BOOLEAN literal, not on the value
    // 1. from_pyrope collapses `true` and `1` to the same integer, so testing
    // the value made the one-bit matrix `undef=1` (= read 0 / write 0 only)
    // unreachable: it silently became all-ones, and the lec X plane then masked
    // away EVERY read port's collision window — a genuinely wrong read port
    // proved equivalent. A numeric value is always an explicit matrix now.
    auto      cfg_is_true_literal = [&](std::string_view key) {
      auto it = cfg.named.find(std::string(key));
      return it != cfg.named.end() && lnast_->get_name(it->second) == "true";
    };
    const bool fwd_all   = cfg_is_true_literal("fwd");
    const bool undef_all = cfg_is_true_literal("undef");
    if ((fwd_all || undef_all) && fwd_bits > 62) {
      error_here(
          "upass.tolg: __memory has {} read x {} write ports — "
          "fwd=true/undef=true exceeds the 62-bit matrix this path "
          "builds; pass an explicit matrix instead",
          n_rd_cfg,
          n_wr_cfg);
      return true;
    }
    const int64_t fwd_mask   = fwd_all ? (int64_t{1} << fwd_bits) - 1 : fwd;
    // `undef` is the same shape (graph/cell.cpp pid 15) and takes the same
    // 0/true/explicit-matrix spellings. It is mutually exclusive with `fwd`
    // per (read,write) pair: forwarded data is defined by construction.
    const int64_t undef_mask = undef_all ? (int64_t{1} << fwd_bits) - 1 : undef;
    if ((fwd_mask & undef_mask) != 0) {
      error_here(
          "upass.tolg: __memory has fwd and undef both set for the same "
          "(read,write) pair (fwd={:#x}, undef={:#x}) — a forwarded "
          "read returns the new data, so it cannot also be undefined",
          fwd_mask,
          undef_mask);
      return true;
    }

    auto mem = make_node(Ntype_op::Memory);
    // Stamp the USER BINDING on the Memory node (same rationale as the
    // array-declare site: a null name degrades pass/lec memory pairing to
    // anonymous shape+ordinal). Calls lower through a compiler temporary:
    //
    //   fcall(%res_0, __memory, cfg)
    //   store(res, %res_0)
    //
    // so naming the cell directly from `dst` leaks `%res_0` into the state
    // correspondence key. Recover the adjacent source binding exactly as the
    // ordinary Sub path does; fall back to dst only when the result is consumed
    // directly by an expression.
    {
      std::string mem_base{lnast_->get_name(dst)};
      if (auto bound = lhs_var_of_temp_dst(nid, mem_base); !bound.empty()) {
        mem_base = std::move(bound);
      }
      if (auto p = mem_base.find("___ssa_"); p != std::string::npos) {
        mem_base.resize(p);
      }
      if (!mem_base.empty()) {
        mem.set_name(std::string(canon_io_name(mem_base)));
      }
    }
    setup_sink_by_name(mem, "bits").connect_driver(create_const(*g_, *Dlop::create_integer(bits)));
    setup_sink_by_name(mem, "size").connect_driver(create_const(*g_, *Dlop::create_integer(size)));
    setup_sink_by_name(mem, "type").connect_driver(create_const(*g_, *Dlop::create_integer(type)));
    setup_sink_by_name(mem, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(fwd_mask)));
    if (undef_mask != 0) {
      setup_sink_by_name(mem, "undef").connect_driver(create_const(*g_, *Dlop::create_integer(undef_mask)));
    }
    setup_sink_by_name(mem, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(wensize)));
    if (type != 2) {
      setup_sink_by_name(mem, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(posclk)));
      if (clocks.size() == 1) {
        setup_sink_by_name(mem, "clock_pin").connect_driver(leaf(clocks.front()).pin);
      } else if (clocks.empty() && !clock_name_.empty()) {
        setup_sink_by_name(mem, "clock_pin").connect_driver(clock_pin());
      } else if (clocks.empty()) {
        warn_at(Lnast_nid{}, {"no-clock", "time"}, "__memory has no clock to bind in '{}'", lnast_->get_top_module_name());
      }
      // A per-port list is connected below at base+2 once the port ordering is
      // known. Materializing every sink (rather than one shared pid 2) lets
      // cgen select the multiclock wrapper and retain each read/write clock.
    }
    if (auto it = cfg.named.find("init"); it != cfg.named.end()) {
      spool_ptr<Dlop> init;
      if (Lnast_ntype::is_const(lnast_->get_type(it->second))) {
        init = Dlop::from_pyrope(lnast_->get_name(it->second));
      } else if (auto lit = tuple_recs_.find(std::string(lnast_->get_name(it->second))); lit != tuple_recs_.end()) {
        const std::vector<int64_t>   flat_dims{size};  // __memory is always flat
        std::vector<spool_ptr<Dlop>> entries;
        if (flatten_init_values(lit->second, flat_dims, 0, "__memory init", static_cast<int32_t>(bits), entries)) {
          init = pack_entries(entries, static_cast<int32_t>(bits));
        }
      }
      if (!init) {
        error_here(
            "upass.tolg: __memory 'init' must be a comptime constant or "
            "tuple literal");
        return true;
      }
      setup_sink_by_name(mem, "init").connect_driver(create_const(*g_, *init));
    }

    int n_wr = 0;
    for (int i = 0; i < n_ports; ++i) {
      if (!Lnast_ntype::is_const(lnast_->get_type(rdports[i]))) {
        error_here(
            "upass.tolg: __memory 'rdport' entry {} must be a comptime "
            "0/1 constant",
            i);
        return true;
      }
      auto       v     = Dlop::from_pyrope(lnast_->get_name(rdports[i]));
      const bool is_rd = v && !v->is_known_false();
      if (!is_rd) {
        ++n_wr;
      }
    }

    for (int i = 0; i < n_ports; ++i) {
      const auto base  = i * kMemPortStride;
      auto       rdv   = Dlop::from_pyrope(lnast_->get_name(rdports[i]));
      const bool is_rd = rdv && !rdv->is_known_false();
      mem.create_sink_pin(static_cast<hhds::Port_id>(base + 0)).connect_driver(leaf(addrs[i]).pin);
      mem.create_sink_pin(static_cast<hhds::Port_id>(base + 10))
          .connect_driver(create_const(*g_, *Dlop::create_integer(is_rd ? 1 : 0)));
      if (type != 2 && clocks.size() > 1) {
        mem.create_sink_pin(static_cast<hhds::Port_id>(base + 2)).connect_driver(leaf(clocks[static_cast<size_t>(i)]).pin);
      }
      Pin en = i < static_cast<int>(ens.size()) ? leaf(ens[i]).pin : en_const(true);
      mem.create_sink_pin(static_cast<hhds::Port_id>(base + 4)).connect_driver(en);
      if (!is_rd) {
        if (i >= static_cast<int>(dins.size())) {
          error_here("upass.tolg: __memory write port {} has no 'din' entry", i);
          return true;
        }
        mem.create_sink_pin(static_cast<hhds::Port_id>(base + 3)).connect_driver(leaf(dins[i]).pin);
      }
    }

    mem_results_[std::string(lnast_->get_name(dst))] = Mem_result{mem, n_wr, n_ports - n_wr, static_cast<int32_t>(bits)};
    return true;
  }

  // store(ref mem, idx, val) — one write port per site. The enable is the
  // site's full branch-path condition (true when unconditional); same-cycle
  // conflicts between ports are defined by the memory config (fwd), not here.
  void lower_mem_store(const Lnast_nid& lhs, std::string_view lhs_name, Mem_info& mi) {
    // Gather the index chain; the LAST sibling is the stored value
    // (store(mem, i, j, …, val) is FLAT — one node, N index operands).
    std::vector<Lnast_nid> idxs;
    for (auto c = lnast_->get_sibling_next(lhs); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      idxs.emplace_back(c);
    }
    // Chunked masked write: store(mem, <idx…>, din, chunk_k) has dims+2
    // children — the trailing const is the per-chunk write-enable index, so the
    // enable becomes `path_cond << k` (the wensize byte/chunk-enable model; the
    // memory's wensize is set from the reader's pending attr in finalize_mems).
    int chunk = -1;
    if (idxs.size() == mi.dims.size() + 2) {
      if (auto cv = Dlop::from_pyrope(lnast_->get_name(idxs.back())); cv && cv->is_just_i64()) {
        chunk = static_cast<int>(cv->to_just_i64());
        idxs.pop_back();
      }
    }
    if (idxs.size() < 2) {
      error_here(
          "upass.tolg: whole-array assignment to memory '{}' is not "
          "supported — write one entry at a time",
          lhs_name);
      return;
    }
    auto val = idxs.back();
    idxs.pop_back();
    auto addr = flatten_mem_addr(mi, idxs, lhs_name);
    if (addr.is_invalid()) {
      return;  // flatten_mem_addr reported
    }
    if (mi.wr_next >= mi.n_user_wr) {
      error_here("upass.tolg: internal — memory '{}' write-site pre-scan undercounted", lhs_name);
      return;
    }
    if (mi.is_array && mi.rd_next > 0) {
      // A type=2 array is lowered writes-before-reads (forwarding), so a
      // source-order read placed BEFORE this write would wrongly see it.
      // reg memories are exempt: fwd semantics are order-free by contract.
      error_here(
          "upass.tolg: array '{}' is written after being read — "
          "same-cycle order is not preserved for "
          "mut/const arrays; reorder the accesses or use a `reg` memory",
          lhs_name);
      return;
    }
    const auto base = mi.wr_next * kMemPortStride;
    ++mi.wr_next;
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 0)).connect_driver(addr);           // addr
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 3)).connect_driver(leaf(val).pin);  // din
    auto en = current_path_cond();
    if (en.is_invalid()) {
      en = en_const(true);
    }
    if (chunk >= 0) {
      en = shl1_by(en, chunk);  // per-chunk write enable: bit `chunk` = path_cond
    }
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 4)).connect_driver(en);  // enable
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 10))
        .connect_driver(create_const(*g_, *Dlop::create_integer(0)));  // rdport = 0 (write)
  }

  // tuple_get(ref dst, ref mem, idx) — one read port per site, always
  // enabled; dst binds to the port's dout driver (pid n_wr_total + r).
  void lower_tuple_get(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    auto src = dst.is_invalid() ? dst : lnast_->get_sibling_next(dst);
    auto idx = src.is_invalid() ? src : lnast_->get_sibling_next(src);
    if (idx.is_invalid()) {
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: tuple/field read of '{}' has no index — it cannot "
               "be lowered to a netlist",
               src.is_invalid() ? std::string_view{"?"} : lnast_->get_name(src));
    }
    const std::string src_name{lnast_->get_name(src)};
    if (auto ait = array_scalar_views_.find(src_name); ait != array_scalar_views_.end()) {
      if (!lnast_->get_sibling_next(idx).is_invalid()) {
        error_here("upass.tolg: scalar-replaced array '{}' currently supports one element index", src_name);
        return;
      }
      auto packed = leaf(src);
      auto iv     = leaf(idx);
      Pin  offset;
      bool dynamic_index = false;
      if (Lnast_ntype::is_const(lnast_->get_type(idx))) {
        auto ci = Dlop::from_pyrope(lnast_->get_name(idx));
        if (!ci || !ci->is_just_i64() || ci->to_just_i64() < 0 || ci->to_just_i64() >= ait->second.size) {
          error_at(nid,
                   {"array-index-out-of-range", "type"},
                   "Pyrope array index {} is outside [0, {}) for '{}'",
                   lnast_->get_name(idx),
                   ait->second.size,
                   src_name);
          return;
        }
        offset = create_const(*g_, *Dlop::create_integer(ci->to_just_i64() * ait->second.elem_mw));
      } else {
        dynamic_index = true;
        auto mult     = make_node(Ntype_op::Mult);
        setup_sink_by_name(mult, "as").connect_driver(iv.pin);
        setup_sink_by_name(mult, "as").connect_driver(create_const(*g_, *Dlop::create_integer(ait->second.elem_mw)));
        offset = mult.create_driver_pin(0);
        set_ubits(offset, std::max<int32_t>(iv.mw + std::bit_width(static_cast<uint32_t>(ait->second.elem_mw)), 1));
      }

      auto sra = make_node(Ntype_op::SRA);
      setup_sink_by_name(sra, "a").connect_driver(packed.pin);
      setup_sink_by_name(sra, "b").connect_driver(offset);
      auto shifted = sra.create_driver_pin(0);
      set_ubits(shifted, packed.mw);

      auto gm = make_node(Ntype_op::Get_mask);
      setup_sink_by_name(gm, "a").connect_driver(shifted);
      setup_sink_by_name(gm, "mask").connect_driver(create_const(*g_, *Dlop::get_mask_value(ait->second.elem_mw)));
      auto out = gm.create_driver_pin(0);
      if (ait->second.elem_signed) {
        set_sbits(out, ait->second.elem_mw);
        record(lnast_->get_name(dst), out, ait->second.elem_mw);
      } else {
        bind_result(lnast_->get_name(dst), out, ait->second.elem_mw);
      }
      if (dynamic_index) {
        lower_array_index_assert(iv, ait->second.size, nid);
      }
      return;
    }
    auto it = mem_map_.find(src_name);
    if (it == mem_map_.end()) {
      // Multi-output Sub result: tuple_get(dst, result, 'port') binds that
      // output port's driver pin with the io-entry width/sign contract.
      if (auto srt = sub_results_.find(std::string(lnast_->get_name(src))); srt != sub_results_.end()) {
        // The read may carry MULTIPLE indices: a tuple-typed output port
        // flattens to a dotted leaf name (`rsp.sum`), and the dot-form read
        // `inst.rsp.sum` arrives as a flat all-const index chain
        // (tuple_get(dst, inst, 'rsp', 'sum')). Join the chain with '.' and
        // match the FULL dotted output name (no suffix/partial matching —
        // `inst.sum` stays a no-output error when the port is `rsp.sum`).
        // Each index is a string CONST: a bracket read `inst["port"]` reaches
        // here with the surrounding quotes still on the name (`'rdata'`),
        // while the dot form (`inst.port`) is bare — unquote each component
        // before matching (same trap as the quoted mod-import callee).
        std::string joined;
        for (auto ix = idx; !ix.is_invalid(); ix = lnast_->get_sibling_next(ix)) {
          if (!Lnast_ntype::is_const(lnast_->get_type(ix))) {
            error_here(
                "upass.tolg: a multi-output instance result is read by "
                "a single output-port name");
            return;
          }
          std::string_view comp = lnast_->get_name(ix);
          if (comp.size() >= 2 && ((comp.front() == '\'' && comp.back() == '\'') || (comp.front() == '"' && comp.back() == '"'))) {
            comp = comp.substr(1, comp.size() - 2);
          }
          if (!joined.empty()) {
            joined += '.';
          }
          joined += comp;
        }
        // BOTH sides go through canon_io_name: the io entry may carry slang's
        // `` `p.q` `` marker, and so may the READ (`inst.`p.q``, which arrives
        // as a const index with the backticks still on). Canonicalizing only
        // the declaration turned a legal quoted-port read into a hard
        // "instance result has no output named" error.
        std::string_view      pname = canon_io_name(joined);
        const Lnast_io_entry* oe    = nullptr;
        for (const auto& e : srt->second.outputs) {
          if (canon_io_name(e.name) == pname) {
            oe = &e;
            break;
          }
        }
        if (oe == nullptr) {
          error_here("upass.tolg: instance result has no output named '{}'", pname);
          return;
        }
        const std::string output_name{canon_io_name(oe->name)};
        auto              out_dpin = srt->second.sub.create_driver_pin(output_name);
        int32_t           mw       = io_mw(*oe);
        if (oe->kind == Io_kind::boolean) {
          set_ubits(out_dpin, 1);
          record(lnast_->get_name(dst), out_dpin, 1);
        } else if (mw <= 1) {
          set_bits(out_dpin, 1);
          if (oe->is_signed) {
            set_sign(out_dpin);
          } else {
            set_unsign(out_dpin);
          }
          record(lnast_->get_name(dst), out_dpin, 1);
        } else if (oe->is_signed) {
          set_bits(out_dpin, mw);
          set_sign(out_dpin);
          record(lnast_->get_name(dst), out_dpin, mw);
        } else {
          set_ubits(out_dpin, mw);
          record(lnast_->get_name(dst), out_dpin, mw);
        }
        return;
      }
      // 1a-mem — res[N] on a __memory result: bind the N-th read port's dout.
      if (auto mrt = mem_results_.find(std::string(lnast_->get_name(src))); mrt != mem_results_.end()) {
        const auto& mr = mrt->second;
        if (!Lnast_ntype::is_const(lnast_->get_type(idx)) || !lnast_->get_sibling_next(idx).is_invalid()) {
          error_here(
              "upass.tolg: a __memory result is indexed by a single "
              "comptime read-port number");
          return;
        }
        auto          v = Dlop::from_pyrope(lnast_->get_name(idx));
        const int64_t k = (v && v->is_just_i64()) ? v->to_just_i64() : -1;
        if (k < 0 || k >= mr.n_rd) {
          error_here(
              "upass.tolg: __memory result index {} out of range — the "
              "config has {} read port(s)",
              lnast_->get_name(idx),
              mr.n_rd);
          return;
        }
        auto dout = mr.node.create_driver_pin(static_cast<hhds::Port_id>(mr.n_wr + k));
        set_ubits(dout, mr.bits);  // __memory data is raw bits — unsigned
        record(lnast_->get_name(dst), dout, mr.bits);
        return;
      }
      // A single-field read (`src.field`) of a name that is not yet a known
      // memory / Sub result. It may be a forward reference to a call result
      // lowered later in the body (`c = tmp.add` reads tmp.add before
      // `tmp = add_sub(…)` runs). Defer the bind to end-of-pass; re-resolved
      // with tget_final_, a still-unresolved one warns.
      if (!tget_final_ && Lnast_ntype::is_const(lnast_->get_type(idx)) && lnast_->get_sibling_next(idx).is_invalid()) {
        pending_tgets_.emplace_back(nid);
        return;
      }
      error_at(nid,
               {"unhandled-node", "unsupported"},
               "upass.tolg: field/index read of '{}' could not be resolved — "
               "'{}' is not a memory, a multi-output "
               "instance result, or a resolved value (often an unassigned "
               "value/nil, or an unsupported runtime tuple "
               "index)",
               lnast_->get_name(src),
               lnast_->get_name(src));
    }
    auto&                  mi = it->second;
    // Gather the full index chain (tuple_get(dst, mem, i, j, …) is FLAT).
    std::vector<Lnast_nid> idxs;
    for (auto c = idx; !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      idxs.emplace_back(c);
    }
    auto addr = flatten_mem_addr(mi, idxs, lnast_->get_name(src));
    if (addr.is_invalid()) {
      return;  // flatten_mem_addr reported
    }
    const int  slot = mi.n_wr_total + mi.rd_next;
    const auto base = slot * kMemPortStride;
    // Program-order position: the writes minted so far are exactly those that
    // textually precede this read, i.e. the ones it may forward from.
    mi.rd_wr_before.emplace_back(mi.wr_next);
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 0)).connect_driver(addr);  // addr
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 4)).connect_driver(en_const(true));
    mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 10))
        .connect_driver(create_const(*g_, *Dlop::create_integer(1)));  // rdport = 1 (read)
    auto dout = mi.node.create_driver_pin(static_cast<hhds::Port_id>(mi.n_wr_total + mi.rd_next));
    ++mi.rd_next;
    auto dst_name = lnast_->get_name(dst);
    if (mi.elem_signed) {
      set_bits(dout, mi.elem_mw);
      set_sign(dout);
      record(dst_name, dout, mi.elem_mw);
    } else {
      set_ubits(dout, mi.elem_mw);
      record(dst_name, dout, mi.elem_mw);
    }
  }

  // 1a-mem reset-restore SWEEP: the (addr, din) pair that drives the single
  // restore write port. Entry k is written on the k-th cycle of the reset
  // window, so the array is fully restored only after `size` cycles of reset
  // held high — the "memories have no reset port" cost of spelling a reset
  // value on an array. The counter parks at 0 for as long as the module is OUT
  // of reset (its reset_pin is the INVERTED module reset), so every reset
  // pulse sweeps from entry 0, and it saturates at size-1 so a longer reset
  // just rewrites the last entry. A 1-entry array needs no counter, and the
  // `= <const>` broadcast (every entry equal) needs no data mux. `not_rst` is
  // the caller's inverted reset, shared with the user-write gating so both read
  // one net.
  [[nodiscard]] std::pair<Pin, Pin> build_restore_sweep(std::string_view name, const Mem_info& mi, const Pin& not_rst) {
    const auto& vals = mi.restore_vals;
    const auto  n    = static_cast<int64_t>(vals.size());
    I(n > 0);
    if (n == 1) {
      return {create_const(*g_, *Dlop::create_integer(0)), create_const(*g_, *vals[0])};
    }
    const bool uniform
        = std::all_of(vals.begin() + 1, vals.end(), [&](const auto& v) { return v->eq_op(*vals[0])->is_known_true(); });
    const int32_t addr_w = mw_of_val(n - 1);

    auto cnt = make_node(Ntype_op::Flop);
    if (!clock_name_.empty()) {
      setup_sink_by_name(cnt, "clock_pin").connect_driver(clock_pin());
    }
    setup_sink_by_name(cnt, "reset_pin").connect_driver(not_rst);
    setup_sink_by_name(cnt, "initial").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    auto q = cnt.create_driver_pin(0);
    set_ubits(q, addr_w);
    // Name it after the array: pass/lec pairs state BY NAME, and an anonymous
    // `flop_<nid>` here would drop both designs into the speculative tier-2
    // signature pass.
    std::string cnt_name{name};
    if (auto ssa = cnt_name.find("___ssa_"); ssa != std::string::npos) {
      cnt_name.resize(ssa);
    }
    cnt_name = absl::StrCat(cnt_name.empty() ? std::string_view{"mem"} : canon_io_name(cnt_name), "_rstcnt");
    cnt.set_name(cnt_name);
    livehd::graph_util::set_pin_name(q, cnt_name);
    // Real sequential state: register it so the Time_checker reads the q->din
    // self-loop as a state cut instead of "register feedback through stage
    // registers".
    plain_reg_flops_[cnt.get_debug_nid()] = cnt_name;
    // din = (q == size-1) ? q : q + 1
    auto eq                               = make_node(Ntype_op::EQ);  // commutative: both operands feed sink "a"
    eq.create_sink_pin(0).connect_driver(q);
    eq.create_sink_pin(0).connect_driver(create_const(*g_, *Dlop::create_integer(n - 1)));
    auto at_last = eq.create_driver_pin(0);
    set_ubits(at_last, 1);
    auto inc = make_node(Ntype_op::Sum);
    setup_sink_by_name(inc, "as").connect_driver(q);
    setup_sink_by_name(inc, "as").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    auto inc_d = inc.create_driver_pin(0);
    set_ubits(inc_d, addr_w + 1);
    // `q + 1` reaches `n`, which needs addr_w+1 bits, but the saturating mux
    // never SELECTS that value. Truncate the advance arm back to the counter's
    // own width so the mux — and therefore the flop's din — is exactly addr_w
    // wide. Without the Get_mask the din carrier is one bit wider than Q, and
    // pass/bitwidth's process_flop unions the din range into Q: the counter
    // grows a bit, which widens the memory address and pushes the ROM mux
    // selector past its last arm.
    auto inc_trunc = make_node(Ntype_op::Get_mask);
    setup_sink_by_name(inc_trunc, "a").connect_driver(inc_d);
    setup_sink_by_name(inc_trunc, "mask").connect_driver(create_const(*g_, *Dlop::get_mask_value(addr_w)));
    auto inc_w = inc_trunc.create_driver_pin(0);
    set_ubits(inc_w, addr_w);
    auto sat = make_node(Ntype_op::Mux);
    sat.create_sink_pin(0).connect_driver(at_last);
    sat.create_sink_pin(1).connect_driver(inc_w);  // sel==0: advance
    sat.create_sink_pin(2).connect_driver(q);      // sel==1: hold the last entry
    auto sat_d = sat.create_driver_pin(0);
    set_ubits(sat_d, addr_w);  // both arms are addr_w: the counter never leaves [0, n-1]
    setup_sink_by_name(cnt, "din").connect_driver(sat_d);

    if (uniform) {
      return {q, create_const(*g_, *vals[0])};
    }
    // Per-entry reset values: a ROM lookup of the init contents, one Mux arm
    // per entry (arm k sits at sink pid k+1, graph/cell.cpp).
    auto sel = make_node(Ntype_op::Mux);
    sel.create_sink_pin(0).connect_driver(q);
    for (int64_t k = 0; k < n; ++k) {
      sel.create_sink_pin(static_cast<hhds::Port_id>(k + 1)).connect_driver(create_const(*g_, *vals[static_cast<size_t>(k)]));
    }
    auto sel_d = sel.create_driver_pin(0);
    set_ubits(sel_d, mi.elem_mw);
    return {q, sel_d};
  }

  void finalize_mems() {
    for (const auto& name : mem_order_) {
      auto it = mem_map_.find(name);
      if (it == mem_map_.end()) {
        continue;
      }
      auto& mi = it->second;
      if (mi.wr_next != mi.n_user_wr) {
        error_here(
            "upass.tolg: internal — memory '{}' lowered {} write sites "
            "but the pre-scan counted {}",
            name,
            mi.wr_next,
            mi.n_user_wr);
      }
      // 1a-mem reset-restore — a concrete-init reg array with a bound reset
      // re-loads its init while reset is held. `reg arr:[N]T = <const>` is the
      // same statement a scalar `reg r:uW = <const>` makes: that const is the
      // reset value of every entry (user ruling 2026-08-20). A memory has no
      // parallel reset port to realize it with, so the restore is a SWEEP: one
      // write port (addr=<sweep counter>, din=init[addr], enable=reset) plus a
      // small counter that advances one entry per cycle while reset is high.
      // The array is therefore fully restored only after `size` cycles of
      // reset. (A bounded LEC still proves it against a one-cycle scalar reset
      // at the default 2, because it seeds a memory's cycle-0 state from the
      // `init` pin — which the same `= <const>` set — and the sweep then only
      // rewrites what is already there. Starting from an ARBITRARY array needs
      // `--set formal.reset_cycles=<size>`.) Every USER port's enable, and the
      // whole-array `update_enable`, is gated with !reset so program writes
      // stay suppressed while the sweep runs (exactly like a scalar reg's
      // din). The restore port is excluded from the fwd mask, so a same-cycle
      // read during reset returns the committed (old) contents.
      //
      // The port is minted UNCONDITIONALLY when there are restore values, even
      // for a whole-array cell that also takes the source-spelled one-cycle
      // reset below (where the sweep is then dead logic: that reset is the
      // top-priority arm, so this port's `reset` enable is only ever read
      // inside its `else`). `n_wr_total` — and with it every read dout's driver
      // pid — was budgeted at the DECLARE, and cgen_sim/lec recover a read's
      // dout by COUNTING the write ports they find (`n_write + r`). Skipping
      // the port here would leave a hole in that count and point every read at
      // the wrong dout.
      if (!mi.restore_vals.empty()) {
        Pin rst = reset_pin();
        if (reset_neg_) {
          rst = not1(rst);
        }
        const auto en_pid_off = 4;
        Pin        not_rst    = not1(rst);
        for (int u = 0; u < mi.n_user_wr; ++u) {
          const auto pid = static_cast<uint64_t>(u * kMemPortStride + en_pid_off);
          for (const auto& e : mi.node.inp_edges()) {
            if (!e.sink.is_invalid() && static_cast<uint64_t>(e.sink.get_port_id()) == pid) {
              auto old_en = e.driver;
              e.del_edge();
              mi.node.create_sink_pin(static_cast<hhds::Port_id>(pid)).connect_driver(and2(old_en, not_rst));
              break;
            }
          }
        }
        if (mi.has_update) {
          // A bulk update would overwrite the entries the sweep has already
          // restored, so it is suppressed for the whole reset window (an
          // always-on bus becomes `!reset`).
          mi.update_en = and2(mi.update_en, not_rst);
          redrive_mem_sink(mi, 13, mi.update_en);
        }
        const auto [sweep_addr, sweep_din] = build_restore_sweep(name, mi, not_rst);
        const auto base                    = mi.n_user_wr * kMemPortStride;
        mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 0)).connect_driver(sweep_addr);
        mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 3)).connect_driver(sweep_din);
        mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 4)).connect_driver(rst);
        mi.node.create_sink_pin(static_cast<hhds::Port_id>(base + 10))
            .connect_driver(create_const(*g_, *Dlop::create_integer(0)));  // rdport = 0 (write)
      }
      // Same-cycle ordering: build the per-(read,write) `fwd` matrix now that
      // every port is minted. Bit (r*n_wr + w) => read port r forwards write
      // port w. Only the USER write ports can forward; the restore ports
      // (reset) never do, so a read during reset sees the committed contents.
      //   "program" (default): row r = the writes that textually precede read r
      //                        (a PREFIX, recorded in rd_wr_before)
      //   "fwd":               every read forwards every user write
      //   "old":               nothing forwards; a colliding read is the
      //                        DEFINED committed value (what the Verilog
      //                        readers need — a nonblocking write is invisible
      //                        to a same-timestep read)
      //   "none":              nothing forwards and a colliding read is
      //                        UNDEFINED — the parallel `undef` matrix below,
      //                        which is the only thing that distinguishes it
      //                        from "old" (a zero `fwd` row cannot)
      // A type=2 array keeps its legacy single-bit value: it has no clock and
      // is lowered writes-before-reads, so every encoder reads the post-write
      // array unconditionally and the matrix is unused.
      // (A whole-array cell has already forced fwd=0 — a bulk update is a
      // next-state that no same-cycle read observes — so leave it alone.)
      auto ordering = Mem_info::Mem_order::program;  // Pyrope default
      if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
        if (auto oit = pit->second.find("ordering"); oit != pit->second.end()) {
          std::string_view ov{oit->second};
          // Attr values arrive as Pyrope source text: a string literal keeps
          // its quotes.
          while (ov.size() >= 2 && (ov.front() == '"' || ov.front() == '\'') && ov.back() == ov.front()) {
            ov = ov.substr(1, ov.size() - 2);
          }
          if (ov == "program") {
            ordering = Mem_info::Mem_order::program;
          } else if (ov == "fwd") {
            ordering = Mem_info::Mem_order::fwd;
          } else if (ov == "old") {
            ordering = Mem_info::Mem_order::old;
          } else if (ov == "none") {
            ordering = Mem_info::Mem_order::none;
          } else {
            error_here(
                "upass.tolg: memory '{}' has ordering=\"{}\" — the legal "
                "values are \"program\" (default), \"fwd\", \"old\" and "
                "\"none\"",
                name,
                ov);
          }
        }
      }
      const int n_rd = static_cast<int>(mi.rd_wr_before.size());
      if (!mi.is_array && !mi.has_legacy_fwd && !mi.has_update && mi.n_wr_total > 0 && n_rd > 0) {
        // Row-major bit string, MSB first: bit (r*n_wr + w) sits at index
        // n_bits-1-(r*n_wr+w). Built as TEXT so a wide matrix stays exact — a
        // whole-array expansion easily reaches 9rd x 8wr = 72 bits, and every
        // consumer reads it with Dlop::bit_test (arbitrary precision).
        const int   n_bits = n_rd * mi.n_wr_total;
        std::string bits(static_cast<size_t>(n_bits), '0');
        // ordering="none": the SAME layout, but the bits mean "undefined on a
        // collision" rather than "forward". A zero `fwd` row alone cannot say
        // whether the read is defined-OLD or undefined, so "none" needs its own
        // matrix (graph/cell.cpp pid 15). Only the USER write ports go in it —
        // a restore (reset) port is deterministic, exactly as for `fwd`.
        std::string ubits(static_cast<size_t>(n_bits), '0');
        for (int r = 0; r < n_rd; ++r) {
          int fwd_upto   = 0;
          int undef_upto = 0;
          switch (ordering) {
            case Mem_info::Mem_order::program: fwd_upto = mi.rd_wr_before[static_cast<size_t>(r)]; break;
            case Mem_info::Mem_order::fwd    : fwd_upto = mi.n_user_wr; break;
            case Mem_info::Mem_order::old    : fwd_upto = 0; break;
            case Mem_info::Mem_order::none   : undef_upto = mi.n_user_wr; break;
          }
          for (int w = 0; w < fwd_upto; ++w) {
            bits[static_cast<size_t>(n_bits - 1 - (r * mi.n_wr_total + w))] = '1';
          }
          for (int w = 0; w < undef_upto; ++w) {
            ubits[static_cast<size_t>(n_bits - 1 - (r * mi.n_wr_total + w))] = '1';
          }
        }
        // Same encoding for both: compact int64 while it fits (so the emitted
        // Verilog stays a plain decimal), exact `0ub…` text beyond that.
        auto pack = [&](const std::string& b) -> spool_ptr<Dlop> {
          // Width is carried independently by the memory's read/write port
          // counts; a zero mask therefore needs no leading-zero payload.  In
          // particular, ordering="old" on a large restored memory can make
          // this matrix several million zero bits wide.  Keeping those zeros
          // in a Dlop is unnecessary and exceeds Dlop's current word-count
          // representation even though the value itself is simply zero.
          if (b.find('1') == std::string::npos) {
            return Dlop::create_integer(0);
          }
          if (n_bits <= 62) {
            int64_t v = 0;
            for (int i = 0; i < n_bits; ++i) {
              if (b[static_cast<size_t>(n_bits - 1 - i)] == '1') {
                v |= int64_t{1} << i;
              }
            }
            return Dlop::create_integer(v);
          }
          // `b` is already the payload of an unsigned binary literal.  Going
          // through from_pyrope("0ub" + b) makes Dlop provision storage once
          // for the generic parser and then again in init_from_binary().  For
          // very large memories (XiangShan has forwarding matrices above
          // 512K bits), that provisional word count overflows Dlop's int16_t
          // size field before the binary parser releases it, corrupting the
          // pool free.  Parse the known binary payload directly: this is both
          // the exact intended representation and avoids the redundant wide
          // allocation altogether.
          return Dlop::from_binary(b, true);
        };
        auto redrive = [&](int pid, std::string_view pin_name, const spool_ptr<Dlop>& matrix) {
          if (!matrix) {
            return;
          }
          for (const auto& e : mi.node.inp_edges()) {
            if (!e.sink.is_invalid() && static_cast<int>(e.sink.get_port_id()) == pid) {
              e.del_edge();
              break;
            }
          }
          setup_sink_by_name(mi.node, pin_name).connect_driver(create_const(*g_, *matrix));
        };
        redrive(5, "fwd", pack(bits));  // fwd (pid 5)
        if (ubits.find('1') != std::string::npos) {
          redrive(15, "undef", pack(ubits));  // undef (pid 15)
        }
      }
      // Chunked masked writes (mem[addr][chunk]<=data) set a wensize > 1 via a
      // pending attr from the reader; the declare provisionally drove
      // wensize=1, so re-drive it here (after every write port is in place).
      // wensize is the single config pin at port_id 8 (see graph/cell.cpp
      // Memory pin names).
      if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
        if (auto wit = pit->second.find("wensize"); wit != pit->second.end()) {
          if (auto wv = Dlop::from_pyrope(wit->second); wv && wv->is_just_i64() && wv->to_just_i64() > 1) {
            for (const auto& e : mi.node.inp_edges()) {
              if (!e.sink.is_invalid() && static_cast<int>(e.sink.get_port_id()) == 8) {
                e.del_edge();
                break;
              }
            }
            setup_sink_by_name(mi.node, "wensize").connect_driver(create_const(*g_, *Dlop::create_integer(wv->to_just_i64())));
          }
        }
        // Re-drive the forwarding mask (fwd, port 5).  lower_mem_declare reads
        // `fwd` from pending_attrs_ at declare time, which only works when the
        // attr_set precedes the declare (the slang reader's order).  When the
        // Pyrope source folds the attr onto the declaration (`reg
        // t:[N]T:[fwd=0]`) prp2lnast emits the attr_set AFTER the declare, so
        // it lands here.
        // A whole-array cell keeps the fwd=0 that lower_mem_update_store
        // forced: a clocked bulk update is a next-state no same-cycle read
        // observes, and cgen emits `dout = data[addr]` for it regardless.
        if (auto fit = pit->second.find("fwd"); fit != pit->second.end() && !mi.has_update) {
          if (auto fv = Dlop::from_pyrope(fit->second); fv && fv->is_just_i64()) {
            for (const auto& e : mi.node.inp_edges()) {
              if (!e.sink.is_invalid() && static_cast<int>(e.sink.get_port_id()) == 5) {
                e.del_edge();
                break;
              }
            }
            setup_sink_by_name(mi.node, "fwd").connect_driver(create_const(*g_, *Dlop::create_integer(fv->to_just_i64())));
          }
        }
      }

      // Clocked (non-array) memory clock wiring, deferred from
      // lower_mem_declare (the clock_pin/posclk attr_set arrives after the
      // declare). Mirrors the per-reg wiring in finalize_regs: an explicit
      // clock_pin=<input> (the slang reader emits it for a non-`clk`/`clock`
      // write clock) beats the implicit shared clock; posclk=false marks a
      // negedge write clock.
      if (!mi.is_array) {
        bool        posclk_val = true;
        std::string clock_pin_name;
        if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
          if (auto cit = pit->second.find("clock_pin"); cit != pit->second.end()) {
            clock_pin_name = cit->second;
          }
          if (auto pcit = pit->second.find("posclk"); pcit != pit->second.end()) {
            posclk_val = pcit->second != "false" && pcit->second != "0";
          }
        }
        setup_sink_by_name(mi.node, "posclk").connect_driver(create_const(*g_, *Dlop::create_integer(posclk_val ? 1 : 0)));
        if (!clock_pin_name.empty()) {
          // Same resolution as the per-reg wiring above: a module input first,
          // then an internal/derived wire (a gated clock — clock-gate cell
          // output — clocking a reg array; use its DRIVER, not the
          // passthrough buffer), then any plain named pin.
          if (g_->get_io()->has_input(clock_pin_name)) {
            setup_sink_by_name(mi.node, "clock_pin").connect_driver(g_->get_input_pin(clock_pin_name));
          } else if (auto dit = wire_names_.contains(clock_pin_name) ? pin_map_.find(din_key(clock_pin_name)) : pin_map_.end();
                     dit != pin_map_.end()) {
            setup_sink_by_name(mi.node, "clock_pin").connect_driver(dit->second);
          } else if (pin_map_.contains(clock_pin_name)) {
            setup_sink_by_name(mi.node, "clock_pin").connect_driver(pin_map_.at(clock_pin_name));
          } else {
            error_here(
                "upass.tolg: memory '{}' names clock_pin '{}' but '{}' "
                "has no such input/wire",
                name,
                clock_pin_name,
                lnast_->get_top_module_name());
          }
        } else if (!clock_name_.empty()) {
          setup_sink_by_name(mi.node, "clock_pin").connect_driver(clock_pin());
        } else {
          warn_at(Lnast_nid{}, {"no-clock", "time"}, "memory '{}' has no clock input to bind", name);
        }
      }

      // Whole-array reset: a registered whole-array (`update` driven) loads its
      // reset value on reset via the cell's `reset` + runtime `init` pins (cgen
      // / cgen_sim / lec emit `if(reset) data[i] <= init[i]`). The slang reader
      // harvested the reset into `initial` (the reset-value bus const) +
      // `reset_pin` (+ `negreset`) attrs; consume them here. This is the
      // one-cycle parallel reset the SOURCE spelled out and it stays exact —
      // Pyrope's `= <const>` reset value, which no source hardware backs, is
      // the one realized as the restore sweep above.
      if (mi.has_update) {
        if (auto pit = pending_attrs_.find(std::string(name)); pit != pending_attrs_.end()) {
          auto&            attrs = pit->second;
          std::string_view rpn;
          if (auto rit = attrs.find("reset_pin"); rit != attrs.end()) {
            rpn = rit->second;
          }
          if (!rpn.empty() && rpn != "false") {
            // Reset value bus -> init sink (overrides any declare-time const
            // init).
            if (auto iit = attrs.find("initial"); iit != attrs.end() && iit->second != "false") {
              if (auto iv = Dlop::from_pyrope(iit->second)) {
                for (const auto& e : mi.node.inp_edges()) {
                  if (!e.sink.is_invalid() && static_cast<int>(e.sink.get_port_id()) == 11) {  // init (pid 11)
                    e.del_edge();
                    break;
                  }
                }
                setup_sink_by_name(mi.node, "init").connect_driver(create_const(*g_, *iv));
              }
            }
            // Reset condition -> reset sink (active-high; pre-invert negreset).
            Pin rp = g_->get_io()->has_input(std::string(rpn)) ? g_->get_input_pin(std::string(rpn)) : reset_pin();
            if (!rp.is_invalid()) {
              const bool neg = (attrs.count("negreset") && attrs.at("negreset") != "false") || reset_neg_;
              if (neg) {
                rp = not1(rp);
              }
              setup_sink_by_name(mi.node, "reset").connect_driver(rp);
            }
          }
        }
      }

      // Coexistence: a whole-array `update` bus AND per-entry write ports on
      // the same cell is well-defined (per-port writes OVERRIDE the bulk
      // update), but surface it so an accidental mix is visible.
      if (mi.has_update && mi.n_user_wr > 0) {
        warn_at(Lnast_nid{},
                {"memory-update-and-write", "time"},
                "memory '{}' mixes a whole-array `update` with {} per-entry "
                "write port(s); per-entry writes take priority",
                name,
                mi.n_user_wr);
      }

      // A read-less (or access-less) memory is a WARNING at most — its state
      // can be observed by a scan chain, and a future remote regref may
      // attach reads/writes. `pub` (regref potential) silences it entirely.
      if (!mi.is_pub && mi.rd_next == 0) {
        warn_at(Lnast_nid{},
                {"memory-never-read", "type"},
                "memory '{}' is never read — contents are only observable via "
                "scan/regref",
                name);
      }
    }
  }

  // Declared (mw, is_signed) from a declare's type child. prim_type_int(max,
  // min): unsigned iff min ≥ 0, mw mirrors the ssa io harvest (get_bits()-1
  // drops the sign bit when unsigned). prim_type_bool → 1. Unknown → (0,_).
  [[nodiscard]] std::pair<int32_t, bool> declared_width(const Lnast_nid& type_nid) {
    using N      = Lnast_ntype;
    const auto t = lnast_->get_type(type_nid);
    if (N::is_prim_type_bool(t)) {
      return {1, false};
    }
    if (!N::is_prim_type_int(t)) {
      return {0, false};
    }
    auto mx = lnast_->get_first_child(type_nid);
    if (mx.is_invalid()) {
      return {0, false};
    }
    auto mn    = lnast_->get_sibling_next(mx);
    auto max_v = Dlop::from_pyrope(lnast_->get_name(mx));
    if (!max_v || !max_v->is_integer()) {
      return {0, false};
    }
    bool    min_known = false;
    bool    min_neg   = false;
    int32_t min_bits  = 0;
    if (!mn.is_invalid()) {
      if (auto mn_v = Dlop::from_pyrope(lnast_->get_name(mn)); mn_v && mn_v->is_integer()) {
        min_known = true;
        min_neg   = mn_v->is_negative();
        min_bits  = static_cast<int32_t>(mn_v->get_bits());
      }
    }
    const bool is_signed = !(min_known && !min_neg);
    if (!is_signed) {
      auto bits = max_v->is_known_zero() ? int32_t{1} : static_cast<int32_t>(max_v->get_bits() - 1);
      return {bits, false};
    }
    // Signed: the WIDER of the two bounds' signed widths (mirrors the ssa io
    // harvest + io_mw — a min like -100 needs more bits than a max of 3).
    auto bits = static_cast<int32_t>(max_v->get_bits());
    if (min_known) {
      bits = std::max(bits, min_bits);
    }
    return {bits, true};
  }

  // Materialize a deferred stage reg at its din store. Effective
  // depth: plain RHS keeps the declared (min,max); a Sub call result narrows
  // to the DEFICIT against the callee (Phase-1 realization = callee at its
  // declared min, so deficit = stage_N − callee_min; for a mod callee the
  // output cycle is fixed and stage_N must match it exactly → deficit 0).
  // Depth (0,0) is a plain wire — no Flop is created at all.
  void create_stage_flop(std::string_view name, const Pending_stage& p, const Lnast_nid& rhs) {
    // Runs from finalize (no statement walk active): anchor the flop at the
    // stage declaration.
    cur_srcid_ = hhds::SourceId_invalid;
    if (const auto id = lnast_->get_srcid(p.decl_nid); id != hhds::SourceId_invalid) {
      cur_srcid_ = g_->source_locator().import_from(lnast_->source_locator(), id);
    }
    cur_color_         = p.decl_color;  // stage flop lands in its declare's region
    const bool min_nil = p.min_txt == "nil";
    const bool max_nil = p.max_txt == "nil";
    int64_t    smin    = 0;
    int64_t    smax    = 0;
    if (!min_nil) {
      auto c = Dlop::from_pyrope(p.min_txt);
      smin   = (c && c->is_just_i64()) ? c->to_just_i64() : 0;
    }
    if (!max_nil) {
      auto c = Dlop::from_pyrope(p.max_txt);
      smax   = (c && c->is_just_i64()) ? c->to_just_i64() : 0;
    }

    int64_t emin = smin;
    int64_t emax = smax;

    std::string rhs_name;
    if (Lnast_ntype::is_ref(lnast_->get_type(rhs))) {
      rhs_name = std::string(lnast_->get_name(rhs));
    }
    if (auto sit = sub_out_stages_.find(rhs_name); sit != sub_out_stages_.end()) {
      const auto& so = sit->second;
      if (min_nil || max_nil || smin != smax) {
        error_here(
            "upass.tolg: `stage[]` / ranged stage counts on a pipe/mod "
            "call are not supported yet — "
            "write a fixed `stage[N]` for '{}'",
            name);
        return;
      }
      const int64_t n = smin;
      if (so.is_pipe) {
        // pipe convention: cmax < cmin (e.g. bare pipe (1,0)) = no upper bound.
        if (n < so.cmin || (so.cmax >= so.cmin && n > so.cmax)) {
          if (so.cmax >= so.cmin) {
            error_here(
                "upass.tolg: stage[{}] on '{}' is outside the callee's "
                "declared latency range [{}, {}]",
                n,
                name,
                so.cmin,
                so.cmax);
          } else {
            error_here(
                "upass.tolg: stage[{}] on '{}' is below the callee's "
                "declared minimum latency {}",
                n,
                name,
                so.cmin);
          }
          return;
        }
        emin = emax = n - so.cmin;  // callee realized at its declared min
      } else {
        // mod callee: the output's landing cycle is fixed by its interface.
        if (so.cmin != so.cmax || n != so.cmin) {
          error_here(
              "upass.tolg: mod call result '{}' lands at its declared "
              "cycle {} — `stage[{}]` must match it "
              "(add a separate `stage[N] x = value` for extra delay)",
              name,
              so.cmin,
              n);
          return;
        }
        emin = emax = 0;
      }
      // The stage pick pins the REALIZED split: the callee
      // instance contributes its declared min (Phase-1 realization), the
      // caller-side deficit flop the remaining n − cmin. Stamping the full
      // pick on the instance would double-count the deficit.
      {
        const int64_t realized = so.is_pipe ? so.cmin : n;
        so.node.attr(livehd::attrs::time_range).set({realized, realized});
        sub_time_[so.node.get_debug_nid()] = {realized, realized};
      }
    } else if (min_nil || max_nil) {
      error_here(
          "upass.tolg: `stage[]` on '{}' has no chosen count at "
          "realization — write `stage[N]` (the toolchain-picked "
          "default lands in a later phase)",
          name);
      return;
    }

    auto v = leaf(rhs);
    if (emin == 0 && emax == 0) {
      record(name, v.pin, v.mw);  // zero-depth stage = wire
      return;
    }

    auto flop                         = make_node(Ntype_op::Flop);
    flop_depth_[flop.get_debug_nid()] = {emin, emax};
    // The LN-inserted pipe output flop (vs a user `stage[N]` reg) is
    // the narrowing target: LG pass1 rewrites its depth to (min−σ, max−σ).
    if (name.starts_with("%pipe_")) {
      inserted_flops_.insert(flop.get_debug_nid());
    }
    setup_sink_by_name(flop, "pipe_min").connect_driver(create_const(*g_, *Dlop::create_integer(emin)));
    setup_sink_by_name(flop, "pipe_max").connect_driver(create_const(*g_, *Dlop::create_integer(emax)));
    if (!clock_name_.empty()) {
      setup_sink_by_name(flop, "clock_pin").connect_driver(clock_pin());
    } else {
      warn_at(Lnast_nid{}, {"no-clock", "time"}, "reg '{}' has no clock input to bind", name);
    }
    setup_sink_by_name(flop, "din").connect_driver(v.pin);
    auto q = flop.create_driver_pin(0);
    set_ubits(q, v.mw);
    // Keep the stage register's RTL name on q, exactly like finalize_regs does
    // for a `reg`. Without it cgen synthesizes `flop_<nid>`, and pass/lec's
    // tier-1 state pairing (which is BY NAME) then matches nothing: every flop
    // of the def falls through to the speculative tier-2 signature pass, whose
    // uncertain pairs suppress a bounded-bmc PASS. A `stage[N]` design would
    // report UNKNOWN even though it is provably equivalent.
    //
    // `%pipe_<output>` is an LN-inserted pipe-output flop. The prefix is
    // lowering-only, but the suffix is the stable source/output state name and
    // is exactly the tier-1 correspondence anchor used by another front end.
    // Keeping the whole temp anonymous turns a fixed-latency pipe into an
    // unpairable `f:<nid>` frontier and cuts every downstream semdiff region.
    {
      std::string base{name};
      if (base.starts_with("%pipe_")) {
        base.erase(0, std::string_view{"%pipe_"}.size());
      }
      if (auto ssa = base.find("___ssa_"); ssa != std::string::npos) {
        base.resize(ssa);
      }
      if (!base.empty() && base.front() != '%') {
        livehd::graph_util::set_pin_name(q, base);
        // Also stamp the flop NODE name so get_hier_name() reports the register
        // name instead of the `n<id>` fallback (see finalize_regs).
        flop.set_name(base);
      }
    }
    reg_map_.emplace(std::string(name), flop);
    record(name, q, v.mw);
  }

  // True when `nid`'s subtree names `ref`. Used to bound the LHS search below:
  // once the call's result temp has been consumed by anything else, a later
  // store of that temp is no longer "the variable this call binds to".
  bool subtree_reads_ref(const Lnast_nid& nid, std::string_view ref) const {
    if (nid.is_invalid()) {
      return false;
    }
    if (Lnast_ntype::is_ref(lnast_->get_type(nid)) && lnast_->get_name(nid) == ref) {
      return true;
    }
    for (auto c : lnast_->children(nid)) {
      if (subtree_reads_ref(c, ref)) {
        return true;
      }
    }
    return false;
  }

  // The source variable a temp-dst call result is copied into, or "" when the
  // result is consumed by an expression instead (a multi-output instance read
  // through `tuple_get`, an inline `f(g(x))`, ...). A declared binding
  // (`const lane_q = lane(…)`, `var q = Mod(…)`) lowers to a temp dst plus a
  // following `store(lane_q, %t)`, so without this the instance loses the name
  // the source gave it and falls back to `u_<callee>_<temp>` — while the very
  // same call written `lane_q = lane(…)` keeps it. Scanning stops at the first
  // statement that reads the temp for any other purpose.
  std::string lhs_var_of_temp_dst(const Lnast_nid& call, std::string_view dst_txt) const {
    // The copy-out is emitted right behind the call (at most a `declare` in
    // between), so a handful of statements is all this ever needs to look at.
    // The bound also keeps a call whose result is UNUSED — nothing ever reads
    // the temp, so the scan has no natural stop — from walking the rest of the
    // module once per such call.
    int budget = 8;
    for (auto s = lnast_->get_sibling_next(call); !s.is_invalid() && budget-- > 0; s = lnast_->get_sibling_next(s)) {
      if (Lnast_ntype::is_store(lnast_->get_type(s))) {
        auto tgt = lnast_->get_first_child(s);
        auto src = tgt.is_invalid() ? Lnast_nid{} : lnast_->get_sibling_next(tgt);
        if (!src.is_invalid() && Lnast_ntype::is_ref(lnast_->get_type(src)) && lnast_->get_name(src) == dst_txt
            && lnast_->get_sibling_next(src).is_invalid()) {
          std::string v(lnast_->get_name(tgt));
          if (auto p = v.find("___ssa_"); p != std::string::npos) {
            v.resize(p);
          }
          // A dotted store (`t.f = %tmp`) names a tuple field, not an instance.
          if (v.empty() || v.front() == '%' || v.find('.') != std::string::npos) {
            return {};
          }
          // A PORT of the enclosing module is not an instance binding. `y =
          // add1(…)` says where the result goes, not what to call the box, and
          // taking it would give the instance the port's own spelling — which
          // both backends must then rename anyway (Verilog: an instance beside
          // `output reg y`; sim: a struct member beside the `Out` field). Fall
          // through to the synthesized name.
          for (const auto& e : lnast_->io_meta().inputs) {
            if (e.name == v) {
              return {};
            }
          }
          for (const auto& e : lnast_->io_meta().outputs) {
            if (e.name == v) {
              return {};
            }
          }
          return v;
        }
      }
      if (subtree_reads_ref(s, dst_txt)) {
        return {};
      }
    }
    return {};
  }

  // rolled_for owns an ordinary call payload but transports replication in an
  // explicit node, never in reserved actuals. Lower the hidden payload first,
  // then attach the native HHDS descriptor and literal carry self-edges to the
  // Sub that payload created.
  void lower_rolled_for(const Lnast_nid& nid) {
    std::vector<Lnast_nid> kids;
    for (auto c : lnast_->children(nid)) {
      kids.emplace_back(c);
    }
    if (kids.size() != lnast_rolled_for::arity || !Lnast_ntype::is_ref(lnast_->get_type(kids[lnast_rolled_for::index]))
        || !Lnast_ntype::is_tuple_add(lnast_->get_type(kids[lnast_rolled_for::carries]))
        || !Lnast_ntype::is_stmts(lnast_->get_type(kids[lnast_rolled_for::lowering_payload]))) {
      error_here("upass.tolg: malformed rolled_for transport in '{}'", lnast_->get_top_module_name());
      return;
    }

    int64_t  domain_first = 0;
    int64_t  domain_step  = 0;
    uint64_t domain_count = 0;
    if (!absl::SimpleAtoi(lnast_->get_name(kids[lnast_rolled_for::first]), &domain_first)
        || !absl::SimpleAtoi(lnast_->get_name(kids[lnast_rolled_for::step]), &domain_step)
        || !absl::SimpleAtoi(lnast_->get_name(kids[lnast_rolled_for::count]), &domain_count) || domain_step == 0) {
      error_here("upass.tolg: malformed rolled_for domain in '{}'", lnast_->get_top_module_name());
      return;
    }

    const std::string saved_index = std::exchange(rolled_index_port_, std::string(lnast_->get_name(kids[lnast_rolled_for::index])));
    last_lowered_sub_             = {};
    lower_stmts(kids[lnast_rolled_for::lowering_payload]);
    rolled_index_port_ = saved_index;
    auto sub           = last_lowered_sub_;
    last_lowered_sub_  = {};
    if (sub.is_invalid()) {
      error_here("upass.tolg: rolled_for payload did not create an instance in '{}'", lnast_->get_top_module_name());
      return;
    }
    auto gio = sub.get_subnode_io();
    if (!gio) {
      error_here("upass.tolg: rolled_for instance has no callee interface in '{}'", lnast_->get_top_module_name());
      return;
    }
    const auto input_pid = [&](std::string_view name) -> std::optional<hhds::Port_id> {
      for (const auto& d : gio->get_input_pin_decls()) {
        if (d.name == name) {
          return d.port_id;
        }
      }
      return std::nullopt;
    };
    const auto output_pid = [&](std::string_view name) -> std::optional<hhds::Port_id> {
      for (const auto& d : gio->get_output_pin_decls()) {
        if (d.name == name) {
          return d.port_id;
        }
      }
      return std::nullopt;
    };

    hhds::Subnode_loop desc;
    desc.first       = domain_first;
    desc.step        = domain_step;
    desc.count       = domain_count;
    desc.index_input = input_pid(lnast_->get_name(kids[lnast_rolled_for::index]));
    if (!desc.index_input) {
      error_here("upass.tolg: rolled_for index port '{}' is not a callee input", lnast_->get_name(kids[lnast_rolled_for::index]));
      return;
    }
    const auto activation_name = lnast_->get_name(kids[lnast_rolled_for::activation]);
    const auto next_name       = lnast_->get_name(kids[lnast_rolled_for::next_active]);
    if (!activation_name.empty()) {
      desc.activation_input = input_pid(activation_name);
      if (!desc.activation_input) {
        error_here("upass.tolg: rolled_for activation port '{}' is not a callee input", activation_name);
        return;
      }
    }
    if (!next_name.empty()) {
      desc.next_active_output = output_pid(next_name);
      if (!desc.next_active_output || !desc.activation_input) {
        error_here("upass.tolg: rolled_for next-active port '{}' is invalid", next_name);
        return;
      }
    }
    // hhds enforces distinct role inputs by THROWING (std::invalid_argument,
    // "set_subnode(loop): role inputs must be distinct"). plan_loop_roll declines
    // any loop whose source names collide with the reserved port names, so this
    // is unreachable from source — but reaching hhds with two roles on one pid
    // would abort the compiler instead of pointing at the offending loop.
    if (desc.activation_input && desc.index_input && *desc.activation_input == *desc.index_input) {
      error_here("upass.tolg: rolled_for index and activation resolve to the same callee input port '{}'", activation_name);
      return;
    }
    sub.set_subnode(gio, desc);
    for (auto map : lnast_->children(kids[lnast_rolled_for::carries])) {
      if (!Lnast_ntype::is_store(lnast_->get_type(map))) {
        error_here("upass.tolg: malformed rolled_for carry entry");
        return;
      }
      auto in_n  = lnast_->get_first_child(map);
      auto out_n = in_n.is_invalid() ? in_n : lnast_->get_sibling_next(in_n);
      auto ip    = in_n.is_invalid() ? std::optional<hhds::Port_id>{} : input_pid(lnast_->get_name(in_n));
      auto op    = out_n.is_invalid() ? std::optional<hhds::Port_id>{} : output_pid(lnast_->get_name(out_n));
      if (!ip || !op) {
        error_here("upass.tolg: rolled_for carry ports do not exist on the callee");
        return;
      }
      sub.create_driver_pin(*op).connect_sink(sub.create_sink_pin(*ip));
    }
  }

  // func_call(dst_tmp, callee_name, args...) → an Ntype_op::Sub
  // instance of the callee's graph. Args are positional refs/consts (mapped
  // to the callee's io_meta input order) or named store(argname, value)
  // children. The single output binds the dst name with the same
  // bits/sign/to-positive treatment a graph INPUT gets (the value enters
  // this graph from outside). The callee output's declared stages interval
  // is recorded for the following stage-reg store (deficit narrowing).
  void lower_func_call(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto callee_n = lnast_->get_sibling_next(dst);
    if (callee_n.is_invalid()) {
      return;
    }
    std::string callee_name(lnast_->get_name(callee_n));

    // 1a-mem — direct Memory-cell instantiation builtin.
    if (try_lower_memory_builtin(nid, callee_name)) {
      return;
    }

    // A resolved `import` call is comptime scaffolding: constprop
    // bound its namespace bundle / lambda ref and every consumer folded (an
    // UNRESOLVED live import never reaches tolg — pass.upass errors or the
    // kernel defers). Nothing lowers to hardware here.
    if (Lnast_ntype::is_const(lnast_->get_type(callee_n)) && callee_name == "import") {
      return;
    }

    std::string                    callee_full;
    std::shared_ptr<hhds::GraphIO> gio;
    const Lnast_tree_io*           cio_ptr = nullptr;
    Lnast_tree_io                  cio_lg;  // synthesized for an lg: black box
    std::string_view               kind;    // callee lambda kind ("" for an lg: black box)
    std::shared_ptr<Lnast>         callee;  // kept alive: cio_ptr may point into its io_meta()

    // An import-bound pipe/mod callee arrives as a string Dlop, so constprop
    // renders it QUOTED (`'unit.entity'` / `'lg:foo'`) when it folds the call's
    // callee ref to its value — unlike a by-name (same-file) callee, which
    // stays an unquoted ref (`file.entity`). A `comb` is unquoted-ref-resolved
    // and inlined by the runner before tolg, but pipe/mod calls reach here as
    // the folded const, so strip the surrounding quotes once so the registry
    // lookup, lg: detection, Sub instance name, and diagnostics all see the
    // bare callee name (mirrors the comb inliner's lambda-ref unquoting).
    if (callee_name.size() >= 2 && callee_name.front() == '\'' && callee_name.back() == '\'') {
      callee_name = callee_name.substr(1, callee_name.size() - 2);
    }

    // An `import("lg:foo")` binding folds the callee to the string
    // 'lg:foo'. Instantiate the foreign graph as a BLACK BOX: its GraphIO (the
    // kernel load_merge'd the lg: inputs into lib_) supplies the IO to wire by
    // name; cgen emits the instance by name and the body rides along in the
    // assembled library. There is no ln: lambda — synthesize the io_meta the
    // shared wiring below expects from the GraphIO's declared pins.
    std::string lg_name;
    if (callee_name.rfind("lg:", 0) == 0) {
      lg_name = callee_name.substr(3);
    }
    if (!lg_name.empty()) {
      gio = lib_ != nullptr ? lib_->find_io(lg_name) : nullptr;
      if (!gio) {
        error_here(
            "upass.tolg: imported lg: graph '{}' not found in any input "
            "library — pass it as an `lg:` "
            "input (or it failed to load)",
            lg_name);
        return;
      }
      auto kind_of_bits = [](uint32_t b) { return b == 1 ? Io_kind::boolean : Io_kind::integer; };
      for (const auto& d : gio->get_input_pin_decls()) {
        if ((d.name == "clock") || (d.name == "reset")) {
          continue;  // implicit; wired from the parent below, not an argument
        }
        cio_lg.inputs.push_back(Lnast_io_entry{.name      = d.name,
                                               .bits      = static_cast<int32_t>(d.bits),
                                               .is_signed = !d.unsign,
                                               .kind      = kind_of_bits(d.bits)});
      }
      for (const auto& d : gio->get_output_pin_decls()) {
        cio_lg.outputs.push_back(Lnast_io_entry{.name      = d.name,
                                                .bits      = static_cast<int32_t>(d.bits),
                                                .is_signed = !d.unsign,
                                                .kind      = kind_of_bits(d.bits)});
      }
      cio_ptr     = &cio_lg;
      callee_full = lg_name;
      callee_name = lg_name;  // Sub instance name + diagnostics
    } else {
      if (registry_ != nullptr) {
        callee = resolve_callee_lnast(callee_name, *registry_);
      }
      kind = callee ? callee->get_lambda_kind() : std::string_view{};
      // A `comb` callee normally inlines in the runner, but with
      // compile.upass.inline=false a fully-defined comb survives as a func_call
      // and lowers here to a Sub instance of its standalone module (same Sub
      // machinery as pipe/mod; a comb carries no clock/reset, so the minted-
      // clock/reset wiring below stays inert). An empty kind ("") is the lg:
      // black-box path handled above, never reached here.
      if (!callee || (kind != "pipe" && kind != "mod" && kind != "comb")) {
        // An unresolved call is ALWAYS a hard error: it is neither a defined
        // pipe/mod/comb, a built-in scalar cast (`signed`/`unsigned`/`uN`/`sN`/
        // `bool`/`string`), nor a `__cellop`. (`comb` may not call a
        // `pipe`/`mod`.)
        const std::string_view cn = callee_name;
        if (cn == "int" || cn == "uint" || cn == "integer") {
          // Tailored guidance for the removed `int`/`uint` cast.
          error_here(
              "the `{}(...)` cast was removed — use "
              "`signed(x)`/`unsigned(x)` to reinterpret a value's sign, "
              "or a sized cast `uN(x)`/`sN(x)`",
              callee_name);
        } else {
          error_here(
              "call to undefined function '{}' — no such pipe/mod/comb "
              "or built-in cast",
              callee_name);
        }
        return;
      }

      // Only a `mod` may instantiate a pipe/mod callee (06-functions.md: `comb`
      // may not call a `pipe`/`mod`; pipe bodies use stage inference, not
      // instantiation). Without this gate a comb would silently grow a
      // latency-carrying instance. A `comb` callee is exempt: it is
      // combinational (latency-0, stateless), so any body — comb, mod, or a top
      // — may instantiate it (compile.upass.inline=false path).
      if (kind != "comb" && lnast_->get_lambda_kind() != "mod") {
        error_here(
            "upass.tolg: '{}' (a {}) calls the {} '{}' — only `mod` "
            "bodies may instantiate pipe/mod",
            lnast_->get_top_module_name(),
            lnast_->get_lambda_kind().empty() ? std::string_view{"comb"} : lnast_->get_lambda_kind(),
            kind,
            callee_name);
        return;
      }

      // 2f-lg: the callee's GraphIO is keyed by its effective graph name (lg
      // override or mangled name) — the same key register_io/setup_io_impl
      // used. resolve_callee_lnast above still matched by top_module_name (the
      // import/call identity), so the rename never affects call resolution.
      callee_full = std::string(callee->get_graph_name());
      gio         = lib_ != nullptr ? lib_->find_io(callee_full) : nullptr;
      if (!gio) {
        error_here(
            "upass.tolg: callee '{}' has no registered GraphIO — "
            "register_io() phase missing",
            callee_full);
        return;
      }
      cio_ptr = &callee->io_meta();
    }
    const auto& cio = *cio_ptr;
    // A zero-output callee is a legitimate SINK instance (e.g. a verification /
    // DPI observer module — XiangShan `DiffExt*` / `DummyDPICWrapper` — which
    // under -DSYNTHESIS carries inputs but no outputs). It binds its inputs and
    // produces no result; handled below after the input/clock/reset wiring.

    // NOTE: set_subnode RE-STAMPS the raw hhds type to its own 2/3 loop-hint
    // encoding — type_op_of() recognizes Subs by the subnode LINK, never by
    // the stored type (see node_util.hpp).
    // Transport the complete caller execution context. Conditional callees
    // expose __valid in their GraphIO, so latch enables, properties, and any
    // descendants see the same guard that clocked state already receives.
    const Pin call_guard = effect_path_cond();
    auto      sub        = make_node(Ntype_op::Sub);
    sub.set_subnode(gio);
    last_lowered_sub_ = sub;
    {
      // Name the Sub by its RTL INSTANCE name so hhds get_hier_name() yields
      // the Verilog-style hierarchy (foo.bar.xx). The name is the LHS VARIABLE
      // the call result binds to (Pyrope `id_ex = Mod(...)`; slang passes
      // inst.name as the dst) — either the dst itself, or, when the dst is a
      // compiler temp, the variable the very next statement copies it into
      // (`const lane_q = lane(…)` lowers to `fcall(%t, lane, …)` +
      // `store(lane_q, %t)`). Strip an SSA suffix. Only a call whose result
      // never lands in a source variable falls back to the synthesized unique
      // `u_<module>_<id>` name. A call-site `name=` (reserved `__inst_name`
      // actual) takes precedence over both — it IS the explicit instance name,
      // so spelling `Mod::[name=x]` on `x = Mod(…)` is redundant, not required.
      std::string callsite_inst;
      std::string callsite_suffix;
      for (auto a = lnast_->get_sibling_next(callee_n); !a.is_invalid(); a = lnast_->get_sibling_next(a)) {
        if (!Lnast_ntype::is_store(lnast_->get_type(a))) {
          continue;
        }
        auto an = lnast_->get_first_child(a);
        if (an.is_invalid()) {
          continue;
        }
        const auto key = lnast_->get_name(an);
        if (key != "__inst_name" && key != "__inst_suffix") {
          continue;
        }
        if (auto v = lnast_->get_sibling_next(an); !v.is_invalid()) {
          (key == "__inst_name" ? callsite_inst : callsite_suffix) = std::string(lnast_->get_name(v));
        }
      }
      std::string dst_txt(lnast_->get_name(dst));
      // A `%`-prefixed compiler temp (1-char prefix); strip the `%` when
      // building the fallback instance name. The `___ssa_` infix below is the
      // unrelated user-var SSA convention — keep it.
      const bool  is_tmp    = !dst_txt.empty() && dst_txt[0] == '%';
      std::string inst_name = dst_txt;
      if (auto p = inst_name.find("___ssa_"); p != std::string::npos) {
        inst_name = inst_name.substr(0, p);
      }
      if (is_tmp) {
        inst_name = lhs_var_of_temp_dst(nid, dst_txt);
      }
      if (!callsite_inst.empty()) {
        sub.set_name(callsite_inst + callsite_suffix);
      } else if (!inst_name.empty()) {
        sub.set_name(inst_name + callsite_suffix);
      } else {
        std::string suffix = is_tmp ? dst_txt.substr(1) : dst_txt;
        sub.set_name("u_" + callee_name + "_" + suffix + callsite_suffix);
      }
    }

    // An explicit rolled_for supplies its index per occurrence, so that one
    // input is intentionally absent from the hidden ordinary call.
    const std::string supplied_index_port = rolled_index_port_;

    // Actuals → callee input sink pins. Named actuals (`port=value`, a `store`)
    // bind by port name; a bare positional actual binds the next declared input
    // in order. Track bound ports per-port so a duplicate bind or an omitted
    // input is caught individually — a bare count of provided-vs-declared could
    // net out equal when one port was bound twice and another left undriven.
    std::size_t                      pos = 0;
    absl::flat_hash_set<std::string> bound_ports;
    std::vector<std::pair<Pin, Pin>> deferred_clocks;  // (Sub sink, ungated parent clock)
    std::vector<Pin>                 active_resets;    // normalized active-high callee resets
    for (auto a = lnast_->get_sibling_next(callee_n); !a.is_invalid(); a = lnast_->get_sibling_next(a)) {
      std::string pname;
      Lnast_nid   val;
      if (Lnast_ntype::is_store(lnast_->get_type(a))) {
        auto an = lnast_->get_first_child(a);
        if (an.is_invalid()) {
          continue;
        }
        // A named actual's key may ride backtick-escaped (`` `port.leaf` `` —
        // a dotted flattened tuple-port leaf the prp_writer quoted); the
        // GraphIO port names are BARE — canonicalize like every other io name.
        pname = std::string(canon_io_name(lnast_->get_name(an)));
        val   = lnast_->get_sibling_next(an);
        if (val.is_invalid()) {
          continue;
        }
        // Namespace receiver marker: `lib.scale(args)` through an
        // import tuple carries the receiver in a `__ufcs_arg` store, but the
        // receiver names the NAMESPACE — it is not an argument of a no-self
        // callee. (A true `ref self` mod method splices in the runner and
        // never reaches the Sub path.)
        if (pname == "__ufcs_arg" && (cio.inputs.empty() || cio.inputs[0].name != "self")) {
          continue;
        }
        // Reserved call-site instance name / loop-iteration suffix — already
        // consumed for sub.set_name above; never a callee port (don't bind,
        // don't count toward arity).
        if (pname == "__inst_name" || pname == "__inst_suffix") {
          continue;
        }
      } else {
        if (pos >= cio.inputs.size()) {
          error_here(
              "upass.tolg: call to '{}' passes more arguments than its "
              "{} declared inputs",
              callee_full,
              cio.inputs.size());
          return;
        }
        pname = std::string(canon_io_name(cio.inputs[pos].name));
        ++pos;
        val = a;
      }
      auto v = leaf(val);
      // Generated activation-capable definitions expose `__valid` for
      // source-visible side effects. An unconditional call passes true; a call
      // under if/match conjoins the caller path so nested activation composes.
      if (pname == "__valid" && !call_guard.is_invalid()) {
        v.pin = and2(nonzero1(v.pin), call_guard);
        v.mw  = 1;
      }
      // 2f-lgimport — validate the port name BEFORE create_sink_pin: an unknown
      // port (e.g. a typo, or a call shaped for a different module) otherwise
      // asserts inside resolve_sink_port (graph.cpp). The compiler must never
      // abort on user input — emit a clean port-mismatch diagnostic instead.
      if (!gio->has_input(pname)) {
        error_here(
            "upass.tolg: call to '{}' names input '{}' which the "
            "imported module does not have",
            callee_full,
            pname);
        return;
      }
      if (!bound_ports.insert(pname).second) {
        error_here("upass.tolg: call to '{}' binds input '{}' more than once", callee_full, pname);
        return;
      }
      auto spin = sub.create_sink_pin(pname);
      if (spin.is_invalid()) {
        error_here("upass.tolg: callee '{}' has no input named '{}'", callee_full, pname);
        return;
      }
      if (is_clock_port_name(pname) && !call_guard.is_invalid()) {
        // Reset is not known until all actuals have been visited. Defer clock
        // wiring so the gate can use `guard | reset_asserted` and a synchronous
        // reset still reaches state while the source call is inactive.
        deferred_clocks.emplace_back(spin, v.pin);
      } else {
        spin.connect_driver(v.pin);
      }
      if (is_reset_port_name(pname)) {
        auto r = nonzero1(v.pin);
        if (str_tools::ends_with(pname, "_n")) {
          r = not1(r);
        }
        active_resets.push_back(r);
      }
    }
    // A compiler-minted activation port is deliberately absent from io_meta,
    // so source arity does not change. Missing explicit generated __valid is
    // also safe to fill here: unconditional context means true; otherwise the
    // complete caller guard is forwarded.
    if (gio->has_input("__valid") && !bound_ports.contains("__valid")) {
      auto active = call_guard.is_invalid() ? create_const(*g_, *Dlop::create_integer(1)) : call_guard;
      sub.create_sink_pin("__valid").connect_driver(active);
      bound_ports.insert("__valid");
    }
    // Every declared input must be driven — checked per-port so an omitted input
    // is caught even when another was bound twice (a bare provided==declared
    // count would miss that).
    for (const auto& ie : cio.inputs) {
      const std::string pname{canon_io_name(ie.name)};
      if (bound_ports.count(pname) == 0) {
        // A replicated instance's index input carries a different value per
        // ordinal, so realization (not the parent graph) drives it. That is the
        // ONLY input a call may leave unconnected.
        if (!supplied_index_port.empty() && pname == supplied_index_port) {
          continue;
        }
        error_here("upass.tolg: call to '{}' does not bind declared input '{}'", callee_full, pname);
        return;
      }
    }

    // Minted-clock wiring: the callee's implicit "clock" input exists on its
    // GraphIO (register_io pre-declared it) but not in its io_meta — wire it
    // to this graph's clock (needs_clock made sure we have one).
    bool callee_declares_clock = false;
    for (const auto& e : cio.inputs) {
      if (is_clock_port_name(e.name)) {
        callee_declares_clock = true;
        break;
      }
    }
    if (!callee_declares_clock && gio->has_input("clock")) {
      if (clock_name_.empty()) {
        error_here(
            "upass.tolg: instance of clocked '{}' but '{}' has no clock "
            "to forward (needs_clock bug)",
            callee_full,
            lnast_->get_top_module_name());
        return;
      }
      auto sink = sub.create_sink_pin("clock");
      if (call_guard.is_invalid()) {
        sink.connect_driver(clock_pin());
      } else {
        deferred_clocks.emplace_back(sink, clock_pin());
      }
    }

    // Minted-reset forwarding, same pattern: the callee's implicit
    // "reset" input (active-high by construction) exists on its GraphIO but
    // not in its io_meta. An active-low caller reset is inverted on the way
    // in so the callee's polarity contract holds.
    bool callee_declares_reset = false;
    for (const auto& e : cio.inputs) {
      if (is_reset_port_name(e.name)) {
        callee_declares_reset = true;
        break;
      }
    }
    if (!callee_declares_reset && gio->has_input("reset")) {
      if (reset_name_.empty()) {
        error_here(
            "upass.tolg: instance of reset-carrying '{}' but '{}' has "
            "no reset to forward (needs_reset bug)",
            callee_full,
            lnast_->get_top_module_name());
        return;
      }
      Pin r = reset_pin();
      if (reset_neg_) {
        // NOT the bitwise `Not` cell: an LGraph Not is unlimited precision
        // (`~x == -x-1`), so `Not(u1)` holds {-1,-2} and stamping its driver u1
        // is a lie -- and cprop's is_bool01 now trusts the u1 hint alone, so a
        // consumer that widens this pin would zero-fill -1. not1() is the same
        // EQ-against-0 spelling every other truth-value negation here uses, and
        // it is exact for any width.
        r = not1(r);
      }
      sub.create_sink_pin("reset").connect_driver(r);
      active_resets.push_back(nonzero1(r));
    }

    // Conditional state activation: each clock domain gets its own glitch-free
    // gate. Pyrope's generated defs have one canonical reset; accepting several
    // reset ports would require a per-state clock/reset-domain map, and OR-ing
    // unrelated resets could advance non-reset state while the call is absent.
    // Fail closed rather than guess that mapping.
    // Generated activation-capable callees are gated structurally after every
    // body has been built. A port name is neither necessary (`clk_i`) nor
    // sufficient (a minted but unused `clock`) evidence that it clocks state.
    // Keep the legacy spelling path only for imported callees without the
    // generated __valid ABI, where no post-lowering guard is available.
    if (gio->has_input("__valid")) {
      for (const auto& [sink, raw_clock] : deferred_clocks) {
        sink.connect_driver(raw_clock);
      }
      deferred_clocks.clear();
    }
    if (!call_guard.is_invalid() && !deferred_clocks.empty()) {
      if (active_resets.size() > 1) {
        error_here("upass.tolg: conditional call to '{}' has multiple reset inputs; clock/reset domain mapping is ambiguous",
                   callee_full);
        return;
      }
      Pin gate_en = call_guard;
      if (!active_resets.empty()) {
        gate_en = or2(gate_en, active_resets.front());
      }
      for (const auto& [sink, raw_clock] : deferred_clocks) {
        sink.connect_driver(clock_gate(raw_clock, gate_en));
      }
    }

    std::string dst_name(lnast_->get_name(dst));

    if (cio.outputs.empty()) {
      // Sink instance (no outputs): inputs/clock/reset are wired above; there
      // is no result pin to create and nothing for the caller to bind.
      return;
    }

    if (cio.outputs.size() > 1) {
      // Multi-output callee: the fcall result is a tuple; each
      // tuple_get(dst2, result, 'port') binds that port's driver pin
      // (lower_tuple_get below). Nothing binds the bare result name.
      // Create EVERY output pin now: downstream passes/cgen walk the
      // callee GraphIO and expect the pins to exist even when a port is
      // left unread (`.e()` unconnected-output style).
      for (const auto& oe2 : cio.outputs) {
        const std::string output_name{canon_io_name(oe2.name)};
        if (!gio->has_output(output_name)) {
          error_here("upass.tolg: callee '{}' has no output named '{}'", callee_full, output_name);
          return;
        }
        (void)sub.create_driver_pin(output_name);
      }
      sub_results_[dst_name] = Sub_result{
          sub,
          {cio.outputs.begin(), cio.outputs.end()}
      };
      return;
    }

    // Single output: bind dst like a graph input (external value entering).
    const auto&       oe          = cio.outputs.front();
    const std::string output_name = std::string(canon_io_name(oe.name));
    if (!gio->has_output(output_name)) {
      error_here("upass.tolg: callee '{}' has no output named '{}'", callee_full, output_name);
      return;
    }
    auto    out_dpin = sub.create_driver_pin(output_name);
    int32_t mw       = io_mw(oe);
    if (oe.kind == Io_kind::boolean) {
      set_ubits(out_dpin, 1);
      record(dst_name, out_dpin, 1);
    } else if (mw <= 1) {
      set_bits(out_dpin, 1);
      if (oe.is_signed) {
        set_sign(out_dpin);
      } else {
        set_unsign(out_dpin);
      }
      record(dst_name, out_dpin, 1);
    } else if (oe.is_signed) {
      set_bits(out_dpin, mw);
      set_sign(out_dpin);
      record(dst_name, out_dpin, mw);
    } else {
      set_ubits(out_dpin, mw);
      record(dst_name, out_dpin, mw);
    }
    // Also expose the single output by name so an explicit field read of the
    // result (`f(...).out`) resolves through lower_tuple_get, exactly like the
    // multi-output case — without it a single-output instance whose result is
    // read via `.out` left the field unbound (the port wire was dropped and the
    // consumer wired to nil). The bare-result form (`r = f(...)`) keeps using
    // the direct record above (resolve() reads pin_map_).
    sub_results_[dst_name] = Sub_result{sub, {oe}};
    // The instance is a timed crossing: stamp its declared
    // latency interval (a following stage[N] re-stamps the pinned pick).
    // Bare-pipe unconstrained max (cmax<cmin) propagates as min (the
    // Phase-1 realization) — the io stages remain the caller-facing truth.
    // A callee output declared `@[]` (stages -1) carries no interval: the
    // instance propagates like a comb crossing and stage[] picks over it
    // fall back to the plain-RHS path.
    // A `comb` callee (compile.upass.inline=false) is purely combinational —
    // latency 0, stateless — so it carries NO interval either: it propagates
    // like a comb crossing, so `stage[N] x = comb(...)` adds its flops over the
    // instance exactly as it would over the inlined logic (its stages_min is
    // the unannotated default 0, which must not pin the result to cycle 0).
    if (kind != "comb" && oe.stages_min >= 0) {
      const int64_t cmin = oe.stages_min;
      const int64_t cmax = oe.stages_max < oe.stages_min ? oe.stages_min : oe.stages_max;
      sub.attr(livehd::attrs::time_range).set({cmin, cmax});
      sub_time_[sub.get_debug_nid()] = {cmin, cmax};
      sub_out_stages_[dst_name]      = {oe.stages_min, oe.stages_max, kind == "pipe", sub};
    }
  }

  // Lower an undischarged timecheck statement to a pending
  // attr + record for the checker.
  void lower_timecheck(const Lnast_nid& nid) {
    auto ref = lnast_->get_first_child(nid);
    if (ref.is_invalid()) {
      return;
    }
    auto mn = lnast_->get_sibling_next(ref);
    if (mn.is_invalid()) {
      return;
    }
    auto mx = mn.is_invalid() ? mn : lnast_->get_sibling_next(mn);
    if (!mx.is_invalid()) {
      auto extra = lnast_->get_sibling_next(mx);
      if (!extra.is_invalid() && Lnast_ntype::is_const(lnast_->get_type(extra)) && lnast_->get_name(extra) == "checked") {
        return;  // discharged at LNAST
      }
    }
    const std::string name(canon_io_name(lnast_->get_name(ref)));  // pin_map_ is keyed canonically
    auto              it = pin_map_.find(name);
    if (it == pin_map_.end()) {
      error_here(
          "upass.tolg: `@[N]` check on '{}' — the value never "
          "materialized in the graph",
          name);
      return;
    }
    const int64_t a_min = const_val(mn);
    const int64_t a_max = mx.is_invalid() ? a_min : const_val(mx);
    it->second.attr(livehd::attrs::pending_time).set({a_min, a_max});
    pending_checks_.push_back({it->second, name, a_min, a_max});
  }

  // The clock graph-input pin. A minted implicit clock gets stamped 1-bit
  // unsigned on first use; a reused declared clk/clock input keeps the
  // width/sign the io loop already stamped.
  [[nodiscard]] Pin clock_pin() {
    if (!clock_pin_valid_) {
      auto p = g_->get_input_pin(clock_name_);
      if (clock_minted_) {
        set_ubits(p, 1);
      }
      clock_pin_       = p;
      clock_pin_valid_ = true;
    }
    return clock_pin_;
  }

  // The module reset graph-input pin (same lazy stamping contract
  // as clock_pin).
  [[nodiscard]] Pin reset_pin() {
    if (!reset_pin_valid_) {
      auto p = g_->get_input_pin(reset_name_);
      if (reset_minted_) {
        set_ubits(p, 1);
      }
      reset_pin_       = p;
      reset_pin_valid_ = true;
    }
    return reset_pin_;
  }

  // The activation graph-input pin. Minted pins are stamped lazily like the
  // implicit clock/reset; an explicit generated __valid keeps its IO stamp.
  [[nodiscard]] Pin valid_pin() {
    if (!valid_pin_valid_) {
      auto p = g_->get_input_pin(valid_name_);
      if (valid_minted_) {
        set_ubits(p, 1);
      }
      valid_pin_       = p;
      valid_pin_valid_ = true;
    }
    return valid_pin_;
  }

  // range(ref(dst), lo, hi) — record [lo,hi] for a later get_mask; no node.
  // A comptime range (both endpoints const) is folded to int bounds in
  // range_map_; a range with a runtime endpoint (`a#[n..=m]`) is kept as
  // (lo,hi) nids in range_dyn_map_ so lower_get_mask can synthesize the
  // shift+mask hardware select. An OPEN range (`a#[lo..]` — hi is the const
  // sentinel "nil") with a comptime lo goes to range_open_map_: its upper bound
  // is the sliced VALUE's MSB, which is not known here (lower_range sees only
  // the range, not which value indexes it), so the consuming get_mask/set_mask
  // closes it to `lo..=(value bits-1)`. (A runtime-lo open range stays in
  // range_dyn_map_, where lower_dynamic_range_select handles the nil hi.)
  void lower_range(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto lo = lnast_->get_sibling_next(dst);
    if (lo.is_invalid()) {
      return;
    }
    auto hi = lnast_->get_sibling_next(lo);
    if (hi.is_invalid()) {
      return;
    }
    std::string name{lnast_->get_name(dst)};
    const bool  open = Lnast_ntype::is_const(lnast_->get_type(hi)) && lnast_->get_name(hi) == "nil";
    if (Lnast_ntype::is_const(lnast_->get_type(lo)) && Lnast_ntype::is_const(lnast_->get_type(hi))) {
      if (open) {
        range_open_map_[name] = lo;  // close to lo..=(MSB) where the value's width is known
      } else {
        range_map_[name] = {const_val(lo), const_val(hi)};
      }
    } else {
      range_dyn_map_[name] = {lo, hi};
    }
  }

  [[nodiscard]] int64_t const_val(const Lnast_nid& nid) {
    auto c = Dlop::from_pyrope(lnast_->get_name(nid));
    return c->is_just_i64() ? c->to_just_i64() : 0;
  }

  // get_mask(ref(dst), value, mask) — mask is a const bitmask, a comptime range
  // ref, OR (the runtime-index exception) a non-const single-bit mask `1<<i` or
  // a runtime `range` ref. The runtime forms have no static bitmask, so they
  // lower to an explicit shift+mask select instead of a Get_mask cell.
  void lower_get_mask(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto val = lnast_->get_sibling_next(dst);
    if (val.is_invalid()) {
      return;
    }
    auto mask_op = lnast_->get_sibling_next(val);
    if (mask_op.is_invalid()) {
      return;
    }

    // Open-ended slice with a comptime offset — `a#[lo..]`. lower_range stashed
    // only the lo nid (the MSB is the sliced VALUE's, unknown until now); close
    // it to `lo..=(value bits-1)` and emit the exact Get_mask. Without this the
    // open range used to be recorded as a closed `{lo,0}` and selected the LOW
    // bits (dropping the offset, wrong width+sign).
    std::string mname{lnast_->get_name(mask_op)};
    if (auto it = range_open_map_.find(mname); it != range_open_map_.end()) {
      lower_open_range_select(dst, val, it->second);
      return;
    }
    // Runtime (non-comptime) bit index — `a#[n..=m]` with a non-const endpoint.
    // lower_range stashed the live lo/hi nids; build `(a>>n) &
    // ((1<<(m-n+1))-1)`.
    if (auto it = range_dyn_map_.find(mname); it != range_dyn_map_.end()) {
      lower_dynamic_range_select(dst, val, it->second.first, it->second.second, nid);
      return;
    }
    // Runtime single-bit index — `a#[i]`. prp2lnast emits the mask as `1<<i`
    // (a one-hot SHL tmp), so it is neither a const nor a recorded range.
    // Select bit i with `(a & (1<<i)) != 0` (== the `(a>>i)&1` workaround,
    // reusing the already-built one-hot mask). Result is a 1-bit unsigned.
    const bool runtime_mask = !Lnast_ntype::is_const(lnast_->get_type(mask_op)) && !range_map_.contains(mname);
    if (runtime_mask) {
      lower_dynamic_bit_select(dst, val, mask_op);
      return;
    }

    auto mask = mask_from_operand(mask_op);

    auto a_val = leaf(val);
    auto node  = make_node(Ntype_op::Get_mask);
    setup_sink_by_name(node, "a").connect_driver(a_val.pin);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *mask));
    auto    drv = node.create_driver_pin(0);
    // An all-ones mask (-1) is the open `#[..]` form: it selects EVERY bit of
    // `a`, so the result width is `a`'s width. popcount(-1) is NOT a finite bit
    // count (the spec mask is infinite ones); using it collapsed the result to
    // ~1 bit, which OpW::firstw then propagated into a following shift and the
    // SMT LEC truncated the shift operand (a false "not equivalent"). A finite
    // (non-negative) mask packs popcount(mask) selected bits, LSB-first.
    int32_t mw  = (mask->is_just_i64() && mask->to_just_i64() == -1) ? a_val.mw : mask_popcount(*mask);
    bind_result(lnast_->get_name(dst), drv, mw);
  }

  // `a#[i]` with a RUNTIME index — the runtime single-bit select. prp2lnast
  // already lowered the mask to the one-hot `1<<i` (the `mask_op` SHL tmp). A
  // one-hot AND isolates bit i of `a`; OR-reducing it yields that bit as a
  // 1-bit unsigned. This equals the documented `(a>>i)&1` workaround while
  // reusing the mask that was already built (no second variable shifter).
  void lower_dynamic_bit_select(const Lnast_nid& dst, const Lnast_nid& val, const Lnast_nid& mask_op) {
    auto a_val = leaf(val);
    auto m     = leaf(mask_op);

    auto andn = make_node(Ntype_op::And);  // commutative: both operands feed sink "a"
    setup_sink_by_name(andn, "as").connect_driver(a_val.pin);
    setup_sink_by_name(andn, "as").connect_driver(m.pin);
    const int32_t and_mw = std::max(a_val.mw, m.mw);
    auto          and_dp = andn.create_driver_pin(0);
    set_ubits(and_dp, and_mw);

    auto ror = make_node(Ntype_op::Ror);  // |(a & (1<<i)) -> the selected bit
    setup_sink_by_name(ror, "as").connect_driver(and_dp);
    bind_result(lnast_->get_name(dst), ror.create_driver_pin(0), 1);
  }

  // Close an open-ended `lo..` slice to the constant bitmask of bits
  // `lo..=msb`, where `msb` is the sliced value's most-significant bit. A `lo`
  // that starts at or past the MSB (or a negative `lo`, already diagnosed
  // upstream) leaves no bits, so the mask is 0 (an empty, zero slice).
  [[nodiscard]] spool_ptr<Dlop> closed_open_mask(int64_t lo, int32_t msb) {
    if (lo < 0 || lo > msb) {
      return Dlop::create_integer(0);
    }
    return Dlop::get_mask_value(msb, static_cast<int>(lo));  // h==l (single bit) handled inside
  }

  // `a#[lo..]` with a COMPTIME offset — the open-ended slice. The upper bound
  // is the sliced value's MSB (`a_val.mw-1`), so close the range to `lo..=msb`
  // and emit the exact Get_mask: bits lo..msb packed LSB-first as an UNSIGNED
  // value
  // (`#[]` zero-extends), with the tight `msb-lo+1` width. This is identical to
  // what the explicit `a#[lo..=msb]` workaround lowers to. (A runtime offset
  // `a#[k..]` keeps its `a>>k` lowering via range_dyn_map_.)
  void lower_open_range_select(const Lnast_nid& dst, const Lnast_nid& val, const Lnast_nid& lo) {
    auto          a_val = leaf(val);
    const int32_t msb   = a_val.mw > 0 ? a_val.mw - 1 : 0;

    auto    lo_c = Dlop::from_pyrope(lnast_->get_name(lo));
    int64_t lo_i = lo_c->is_just_i64() ? lo_c->to_just_i64() : 0;
    auto    mask = closed_open_mask(lo_i, msb);

    auto node = make_node(Ntype_op::Get_mask);
    setup_sink_by_name(node, "a").connect_driver(a_val.pin);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *mask));
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), mask_popcount(*mask));
  }

  // `a#[n..=m]` with a RUNTIME range — lower to `(a>>n) & ((1<<(m-n+1))-1)`,
  // the contiguous-slice select. `lo`/`hi` are the live range endpoints stashed
  // by lower_range. The open form `a#[n..]` (hi == const "nil") selects every
  // bit from n upward, i.e. just `a>>n`. A descending range (m<n) violates the
  // select precondition; lower_get_mask's caller emits the lgassert(m>=n).
  void lower_dynamic_range_select(const Lnast_nid& dst, const Lnast_nid& val, const Lnast_nid& lo, const Lnast_nid& hi,
                                  const Lnast_nid& loc_nid) {
    auto a_val = leaf(val);
    auto n     = leaf(lo);

    // shifted = a >> n   (arithmetic right shift; the only right shift cell —
    // for an unsigned `a` cgen's `>>>` fills zeros, matching the workaround).
    auto sra = make_node(Ntype_op::SRA);
    setup_sink_by_name(sra, "a").connect_driver(a_val.pin);
    setup_sink_by_name(sra, "b").connect_driver(n.pin);
    auto sra_dp = sra.create_driver_pin(0);
    if (pin_can_be_negative(a_val.pin)) {
      set_sbits(sra_dp, a_val.mw);
    } else {
      set_ubits(sra_dp, a_val.mw);
    }

    if (Lnast_ntype::is_const(lnast_->get_type(hi)) && lnast_->get_name(hi) == "nil") {
      // Open range `a#[n..]`: bits n..msb are exactly `a>>n`; no mask, no
      // m>=n precondition (there is no `m`).
      bind_result(lnast_->get_name(dst), sra_dp, a_val.mw);
      return;
    }

    auto m  = leaf(hi);
    auto rw = lower_range_width(n, m);

    // pow = 1 << width. One headroom bit represents 2^a_width before -1.
    auto pow = make_node(Ntype_op::SHL);
    setup_sink_by_name(pow, "a").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    setup_sink_by_name(pow, "b").connect_driver(rw.clamped);
    const int32_t pow_mw = a_val.mw + 1;
    auto          pow_dp = pow.create_driver_pin(0);
    set_ubits(pow_dp, pow_mw);

    // mask = pow - 1   (the low (m-n+1) bits set).
    auto maskn = make_node(Ntype_op::Sum);
    setup_sink_by_name(maskn, "as").connect_driver(pow_dp);
    setup_sink_by_name(maskn, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    auto mask_dp = maskn.create_driver_pin(0);
    set_ubits(mask_dp, pow_mw);

    // result = shifted & mask
    auto andn = make_node(Ntype_op::And);  // commutative: both operands feed sink "a"
    setup_sink_by_name(andn, "as").connect_driver(sra_dp);
    setup_sink_by_name(andn, "as").connect_driver(mask_dp);
    bind_result(lnast_->get_name(dst), andn.create_driver_pin(0), a_val.mw);

    lower_range_assert(rw.reversed, loc_nid);
  }

  // `hi + 1 - lo`, plus the one-bit "this range is REVERSED" flag that both the
  // data path and the runtime assert need. Shared by the range READ and the
  // range WRITE: the two halves of one bit view have to agree on the geometry.
  //
  // The difference is genuinely SIGNED. Nothing orders two runtime endpoints
  // (`lo`/`hi` are plain lowered values, and the `hi >= lo` obligation is a
  // runtime lgassert that no width/range inference consumes), which is the same
  // rule lower_op applies to every other subtraction -- "a subtraction can go
  // negative regardless of operand signs". Stamping it unsigned was a lie the
  // shifts below then read as a huge count: `1 << width` wrapped to 0, the low
  // mask to all-ones, and a WRITE clobbered every bit of its destination --
  // outside the requested range, and decided before the assert ever fires.
  //
  // The flag tests the SIGN OF THE WIDTH, not `hi < lo`. An LT node carries the
  // structural u1 hint on its output pin and cgen.verilog derives the
  // comparison's signedness from exactly that pin, so `hi < lo` emits a bare
  // `hi < lo` -- and Verilog makes a relational UNSIGNED as soon as one operand
  // is unsigned, so a negative `hi` (`a#[j..=(i-1)]`, i == 0) read as a huge
  // value and the guard silently never fired. cgen.sim instead takes the
  // comparison's signedness from the OPERAND pins, so the same node also
  // disagreed between the two backends. Both operands of `width < 0` are
  // signed, so every backend agrees, and it is the exact condition wanted:
  // `width == 0` (the empty `hi == lo - 1`) already yields a zero mask.
  struct Range_width {
    Pin     clamped;   // unsigned: the width, or 0 when the range is reversed
    Pin     reversed;  // u1: 1 when hi < lo
    int32_t mw;
  };

  Range_width lower_range_width(const Val& lo, const Val& hi) {
    // One bit WIDER than the endpoints' own carrier: the widest legal width,
    // `hi_max + 1`, needs the full unsigned `w_mw`, so a SIGNED carrier of the
    // same size would wrap it.
    const int32_t w_mw  = std::max(lo.mw, hi.mw) + 1;
    auto          width = make_node(Ntype_op::Sum);
    setup_sink_by_name(width, "as").connect_driver(hi.pin);
    setup_sink_by_name(width, "as").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    setup_sink_by_name(width, "bs").connect_driver(lo.pin);
    auto width_dp = width.create_driver_pin(0);
    set_sbits(width_dp, w_mw + 1);

    auto rev = make_node(Ntype_op::LT);  // positional: width < 0
    setup_sink_by_name(rev, "as").connect_driver(width_dp);
    setup_sink_by_name(rev, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    auto rev_dp = rev.create_driver_pin(0);
    set_ubits(rev_dp, 1);

    // A reversed range selects NO bits, so clamp its width to 0: the mask comes
    // out 0, so a read is 0 and a write leaves its destination untouched -- the
    // only sane data path for an empty range (the lgassert still reports it).
    auto sel = make_node(Ntype_op::Mux);
    sel.create_sink_pin(0).connect_driver(rev_dp);                                       // selector
    sel.create_sink_pin(1).connect_driver(width_dp);                                     // false: hi >= lo
    sel.create_sink_pin(2).connect_driver(create_const(*g_, *Dlop::create_integer(0)));  // true: reversed
    auto clamped = sel.create_driver_pin(0);
    // Unsigned (the clamp proves it) and never NARROWER than the widest arm:
    // cgen.sim rejects a Mux whose result carrier truncates an arm
    // ("mux-width-loss").
    set_ubits(clamped, w_mw + 1);

    return Range_width{.clamped = clamped, .reversed = rev_dp, .mw = w_mw + 1};
  }

  // Emit a runtime `lgassert(hi >= lo)` guarding a dynamic range select against
  // a descending range (the select precondition). The check is an `lgassert`
  // Sub instance: a recognized primitive cgen lowers to an inline SystemVerilog
  // immediate assertion (no data-path output, so LEC is unaffected). `loc_nid`
  // carries the `a#[lo..=hi]` source span for the assert message. Skipped when
  // there is no GraphLibrary to register the primitive in (the data-path
  // lowering is already complete and correct without the guard).
  // `reversed` is the flag lower_range_width already built for the data path.
  // Recomputing it here as `LT(hi, lo)` is what the guard used to do, and that
  // spelling could not fire for a signed `hi` -- see lower_range_width.
  void lower_range_assert(const Pin& reversed, const Lnast_nid& loc_nid) {
    if (lib_ == nullptr) {
      return;
    }
    // cond = (hi >= lo) = reversed XOR 1.
    auto notn = make_node(Ntype_op::Xor);
    setup_sink_by_name(notn, "as").connect_driver(reversed);
    setup_sink_by_name(notn, "as").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    auto cond = notn.create_driver_pin(0);
    set_ubits(cond, 1);

    // A dynamic range check is a source-visible effect just like an assert:
    // while this definition/branch is inactive, the obligation is vacuous.
    const auto guard = effect_path_cond();
    if (!guard.is_invalid()) {
      cond = or2(not1(nonzero1(guard)), nonzero1(cond));
    }

    auto gio = lib_->find_io(livehd::graph_util::lgassert_module_name);
    if (!gio) {
      gio = lib_->create_io(livehd::graph_util::lgassert_module_name);
      gio->add_input("cond", 1);
      gio->set_bits("cond", 1);
      gio->set_unsign("cond", true);
    }
    auto sub = make_node(Ntype_op::Sub);
    sub.set_subnode(gio);
    sub.create_sink_pin("cond").connect_driver(cond);
    // Carry the "line of code info" (file:line of the `a#[lo..=hi]`) on the
    // instance-name attr so cgen can fold it into the assertion message.
    const auto  sp  = lnast_->span_of(loc_nid);
    std::string loc = sp.file.empty() ? std::string{"?"} : sp.file;
    if (sp.start_line) {
      loc += ":" + std::to_string(*sp.start_line);
    }
    sub.attr(hhds::attrs::name).set(loc);
  }

  // Pyrope requires a dynamic out-of-range array access to fail at runtime.
  // Materialize `0 <= index < size` as the same lgassert primitive used for
  // dynamic range preconditions. Verilog-origin accesses are handled by slang
  // and retain SystemVerilog's X/ignored-write behavior instead.
  void lower_array_index_assert(const Val& index, int64_t size, const Lnast_nid& loc_nid) {
    if (lib_ == nullptr || lnast_->is_verilog_origin()) {
      return;
    }

    auto lt_size = make_node(Ntype_op::LT);
    setup_sink_by_name(lt_size, "as").connect_driver(index.pin);
    setup_sink_by_name(lt_size, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(size)));
    auto cond = lt_size.create_driver_pin(0);
    set_ubits(cond, 1);

    if (pin_can_be_negative(index.pin)) {
      auto lt_zero = make_node(Ntype_op::LT);
      setup_sink_by_name(lt_zero, "as").connect_driver(index.pin);
      setup_sink_by_name(lt_zero, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
      auto neg = lt_zero.create_driver_pin(0);
      set_ubits(neg, 1);
      cond = and2(cond, not1(neg));
    }

    const auto guard = effect_path_cond();
    if (!guard.is_invalid()) {
      cond = or2(not1(nonzero1(guard)), nonzero1(cond));
    }

    auto gio = lib_->find_io(livehd::graph_util::lgassert_module_name);
    if (!gio) {
      gio = lib_->create_io(livehd::graph_util::lgassert_module_name);
      gio->add_input("cond", 1);
      gio->set_bits("cond", 1);
      gio->set_unsign("cond", true);
    }
    auto sub = make_node(Ntype_op::Sub);
    sub.set_subnode(gio);
    sub.create_sink_pin("cond").connect_driver(cond);
    const auto  sp  = lnast_->span_of(loc_nid);
    std::string loc = "array index out of range";
    if (!sp.file.empty()) {
      loc += " at " + sp.file;
      if (sp.start_line) {
        loc += ":" + std::to_string(*sp.start_line);
      }
    }
    sub.attr(hhds::attrs::name).set(loc);
  }

  // Materialize a verifier-unknown cassert (assert / assert_always / assume) as
  // an `fproperty` Sub: a recognized primitive carrying the 1-bit cond, with
  // "<kind>\x1f<loc>\x1f<msg>" packed in the instance-name attr. pass.formal
  // proves/defers it; cgen emits a runtime check for what it could not prove.
  void lower_cassert(const Lnast_nid& nid) {
    if (lib_ == nullptr) {
      return;
    }
    auto cond_nid = lnast_->get_first_child(nid);
    if (cond_nid.is_invalid()) {
      return;
    }
    Val cond = leaf(cond_nid);
    if (cond.pin.is_invalid()) {
      return;
    }
    // Children after cond: an optional kind sentinel (assume / assert_always)
    // followed by an optional user message (both are const children that
    // survive upass re-emission, unlike the cassert node name).
    std::string kind = "assert";
    std::string msg;
    auto        nxt = lnast_->get_sibling_next(cond_nid);
    if (!nxt.is_invalid() && Lnast_ntype::is_const(lnast_->get_type(nxt))) {
      std::string s{lnast_->get_name(nxt)};
      // EXACT match, never a substring search: when the user wrote a plain
      // `assert` there is no sentinel and this child is the user's MESSAGE.
      // An unanchored find() there let `assert(x, "… __fkind__assume …")`
      // retype itself into an assume that the solver then USED as a
      // hypothesis — a silent false-PROVEN. The sentinel is emitted unquoted
      // so it cannot collide with any string message.
      if (s == "__fkind__assert_always") {
        kind = "assert_always";
        nxt  = lnast_->get_sibling_next(nxt);
      } else if (s == "__fkind__assume") {
        kind = "assume";
        nxt  = lnast_->get_sibling_next(nxt);
      } else if (s == "__fkind__assume_nocheck") {
        kind = "assume_nocheck";
        nxt  = lnast_->get_sibling_next(nxt);
      } else if (s == "__fkind__cassert") {
        kind = "cassert";
        nxt  = lnast_->get_sibling_next(nxt);
      }
    }
    // `cassert` is an ELABORATION check (user ruling, 2026-07-25): the upass
    // must fold it here, or it fails. It never becomes an fproperty, so it
    // never reaches pass.formal and never survives into the netlist as a
    // runtime check — that is exactly what distinguishes it from `assert`.
    if (kind == "cassert") {
      if (!livehd::graph_util::is_const_pin(cond.pin)) {
        error_at(nid,
                 {"cassert-not-comptime", "unsupported"},
                 "upass.tolg: cassert condition did not fold to a compile-time "
                 "value — cassert is an elaboration check; use `assert` for a "
                 "condition that must hold of the hardware");
        return;
      }
      // Discharge only on a known-TRUE fold. `!is_known_false()` is not the
      // same predicate: an X/unknown constant pin is const and not known-false,
      // so it would slip through as "proven" and emit a full netlist. A cassert
      // the compiler cannot decide is exactly the case that must fail.
      if (!livehd::graph_util::hydrate_const(cond.pin).is_known_true()) {
        error_at(nid, {"cassert-false", "unsupported"}, "upass.tolg: cassert condition is not true at compile time");
      }
      return;  // folded true: discharged here, nothing to materialize
    }
    if (!nxt.is_invalid() && Lnast_ntype::is_const(lnast_->get_type(nxt))) {
      msg = std::string{lnast_->get_name(nxt)};
    }
    auto gio = lib_->find_io(livehd::graph_util::fproperty_module_name);
    if (!gio) {
      gio = lib_->create_io(livehd::graph_util::fproperty_module_name);
      gio->add_input("cond", 1);
      gio->set_bits("cond", 1);
      gio->set_unsign("cond", true);
    }
    // R1 Phase 2 — the RAW guard, as a second port that is DIAGNOSTIC ONLY.
    // `cond` above already carries the full obligation (`!guard || cond`), so a
    // consumer that never reads this port is still CORRECT — it merely loses the
    // antecedent-vacuity diagnostic. That asymmetry is the whole reason the
    // implication is folded into one pin instead of being carried here: a
    // correctness-bearing second port reproduces the R1 bug once per consumer
    // that forgets it, while a diagnostic one can only cost a warning.
    // Guarded by has_input so an fproperty GraphIO loaded from an older `lg:`
    // artifact (cond only) degrades to "no vacuity check" rather than asserting.
    if (!gio->has_input("guard")) {
      gio->add_input("guard", 2);
      gio->set_bits("guard", 1);
      gio->set_unsign("guard", true);
    }
    // R1 — an `assert`/`assume` inside an `if`/`match` arm is guarded by that
    // arm's path condition, exactly as SystemVerilog does it: a procedural
    // assertion is only evaluated when control flow reaches it, which the LRM
    // models as an implication (`guard |-> cond`). Dropping the guard makes the
    // obligation STRICTLY STRONGER, which is silently wrong in both directions
    // — an assert fires on paths the user never claimed anything about, and an
    // over-constrained assume prunes traces (a false PROVEN). So fold the guard
    // into `cond` rather than carrying it as a second port: every consumer of
    // this Sub (pass.formal, the verify monitor encode, cgen_verilog,
    // cgen_sim) then honors it by construction, where a separate port would
    // reproduce this bug once per consumer that forgot to read it.
    //
    // IMPLICATION, not conjunction — `!guard || cond`. The other consumer of
    // current_path_cond() (memory write enables) wants `and2(guard, ...)`; the
    // same pin with the wrong combinator here yields an assert that fires
    // precisely when the guard is FALSE.
    //
    // `cassert` never reaches this point (it returned above): it is an
    // elaboration check that must fold to a comptime constant, and `!guard ||
    // cond` under a runtime guard never folds, so guarding it would turn
    // working code into `cassert-not-comptime`.
    //
    // or2 treats an invalid operand as the identity, so an unguarded property
    // (empty path stack) mints no cells at all — and, crucially, keeps its cond
    // pin EXACTLY as before, at full width, so the encoder's own nonzero test
    // still spans every bit.
    //
    // A guarded one must reduce first: or2/not1 stamp `bits=1`, which the
    // encoder fits to [0:0], so handing them a multi-bit condition would keep
    // only its LSB. `if c { assert(flags | 0x2) }` is always true (bit 1 is
    // always set) yet refuted on flags[0]==0 before nonzero1 was applied. Both
    // operands go through it: `cond` because the user may assert any integer,
    // and `guard` because it is only 1-bit by convention (prp2lnast gives an
    // if-condition a synthetic `:bool`), not by construction.
    const auto guard    = effect_path_cond();
    const auto eff_cond = guard.is_invalid() ? cond.pin : or2(not1(nonzero1(guard)), nonzero1(cond.pin));
    auto       sub      = make_node(Ntype_op::Sub);
    sub.set_subnode(gio);
    sub.create_sink_pin("cond").connect_driver(eff_cond);
    if (!guard.is_invalid() && gio->has_input("guard")) {
      sub.create_sink_pin("guard").connect_driver(guard);
    }
    const auto  sp  = lnast_->span_of(nid);
    std::string loc = sp.file.empty() ? std::string{} : sp.file;
    if (sp.start_line) {
      loc += ":" + std::to_string(*sp.start_line);
    }
    sub.attr(hhds::attrs::name).set(kind + "\x1f" + loc + "\x1f" + msg);
  }

  // The mask of a get_mask/set_mask is a full Dlop, never an int64: a 64-bit
  // (or wider) mask like 2^64-1 (`0x0ffffffffffffffff`, a full-width truncate)
  // overflows int64 and would silently collapse to 0 — the value `from_pyrope`
  // parses correctly is kept as-is.
  [[nodiscard]] spool_ptr<Dlop> mask_from_operand(const Lnast_nid& mask_op) {
    if (Lnast_ntype::is_const(lnast_->get_type(mask_op))) {
      return Dlop::from_pyrope(lnast_->get_name(mask_op));
    }
    auto it = range_map_.find(std::string{lnast_->get_name(mask_op)});
    if (it != range_map_.end()) {
      auto [lo, hi] = it->second;
      if (hi < lo) {
        std::swap(lo, hi);
      }
      if (lo < 0 || hi < lo) {
        return Dlop::create_integer(0);
      }
      return Dlop::get_mask_value(static_cast<int>(hi),
                                  static_cast<int>(lo));  // multi-word capable, no 63-bit cap
    }
    error_at(mask_op,
             {"mask-not-const", "unsupported"},
             "upass.tolg: get_mask mask operand '{}' is not a constant or "
             "range — a runtime mask has no lowering here "
             "(the mask must be comptime)",
             lnast_->get_name(mask_op));
  }

  // Number of set bits in a (non-negative) mask = the get_mask result width.
  static int32_t mask_popcount(const Dlop& m) {
    auto pc = m.popcount_op();
    return pc->is_just_i64() ? static_cast<int32_t>(pc->to_just_i64()) : 0;
  }

  // Highest set bit + 1 of a (non-negative) mask = the set_mask reach.
  static int32_t mask_high_bit(const Dlop& m) {
    int gb = m.is_positive() ? m.get_bits() : 0;  // get_bits() counts the sign bit too
    return gb > 0 ? static_cast<int32_t>(gb - 1) : int32_t{0};
  }

  // The base (`value`) operand of a set_mask. Normally `leaf(val)`, but a
  // `mut b:uN = nil` emits no init store, so the first `b#[lo..=hi] = …`
  // reads `b` with no driver. That is NOT an error: the bit-assignments
  // overwrite the covered bits; whatever is left uncovered honestly stays
  // unknown (0sb?), exactly as `= 0` would leave it 0. Only a name that was
  // DECLARED as a scalar mut/const gets this 0sb? base — a genuine undriven
  // reference (typo, dropped value) still errors through leaf()/resolve().
  //
  // Both lookups MUST go through canon_io_name. `pin_map_` is keyed on the
  // canonical (backtick-stripped) name because record()/resolve() canonicalize,
  // so probing it with the RAW name misses on every backtick-escaped
  // identifier -- `` `req_written_rearm.addr` ``, i.e. every struct leaf the
  // Pyrope writer emits. The miss then read as "declared but never driven" and
  // substituted a 0sb? base, DISCARDING the value the variable was carrying:
  // `x = a; if c { x#[hi..=lo] = v }` silently lost `a`'s uncovered bits under
  // the branch. It refuted `minion_dcache_replay_queue`; the shape is a
  // conditional partial write, which is why the unconditional form and a plain
  // identifier both looked fine.
  //
  // 2c-wire — a `wire` needs the SAME 0sb? seed, but the test above can never
  // fire for one: lower_wire_declare records the passthrough-Or OUTPUT under
  // the wire's name (so reads are position independent), so a wire is ALWAYS
  // in pin_map_. Taking leaf(val) there seeded the chain with the wire's own
  // buffer output while the completed write connects the chain's result back to
  // that buffer's INPUT — a manufactured combinational RING (`upass.tolg:
  // combinational loop`), the one class of self-referential set_mask chain the
  // frontends still produced. The uncovered bits of a wire assembled from
  // bit-range writes are undriven exactly like the mut case, so seed 0sb? at
  // the wire's DECLARED width and let the covered lanes overwrite it. Only the
  // FIRST partial write reaches here: lower_set_mask prefers the din
  // accumulator once one exists, so a chain still accumulates, and a wire with
  // a whole-value driver already recorded keeps that value as its base.
  [[nodiscard]] Val set_mask_base(const Lnast_nid& val) {
    if (Lnast_ntype::is_ref(lnast_->get_type(val))) {
      const std::string raw{lnast_->get_name(val)};
      const std::string name{canon_io_name(raw)};
      if (!pin_map_.contains(name) && scalar_decl_.contains(name)) {
        return {nil_pin(), 1};
      }
      if (auto wit = wire_info_.find(raw); wit != wire_info_.end() && !pin_map_.contains(din_key(raw))) {
        return {nil_pin(), wit->second.decl_mw > 0 ? wit->second.decl_mw : 1};
      }
    }
    return leaf(val);
  }

  void record_set_mask_result(std::string_view dst_name, const Pin& drv, int32_t mw) {
    const bool is_reg  = reg_map_.contains(std::string(dst_name)) && reg_info_.contains(std::string(dst_name));
    const bool is_wire = !is_reg && wire_names_.contains(std::string(dst_name));
    if (is_reg) {
      record(din_key(dst_name), drv, mw);
      record(en_key(dst_name), en_const(true), 1);
    } else if (is_wire) {
      record(din_key(dst_name), drv, mw);
      maybe_bind_wire_shadow(din_key(dst_name), drv, mw);
    } else {
      record(dst_name, drv, mw);
    }
  }

  // Build `(base & ~mask) | (shifted_value & mask)` using an explicitly
  // width-bounded inverse mask. This is the common full-value RMW used by
  // runtime bit and range writes; no dynamic Set_mask cell is required.
  [[nodiscard]] Pin lower_dynamic_mask_rmw(const Val& base, const Pin& mask, const Pin& shifted_value) {
    const int32_t out_mw = std::max<int32_t>(base.mw, 1);

    auto inv = make_node(Ntype_op::Xor);
    setup_sink_by_name(inv, "as").connect_driver(mask);
    setup_sink_by_name(inv, "as").connect_driver(create_const(*g_, *Dlop::get_mask_value(out_mw)));
    auto inv_dp = inv.create_driver_pin(0);
    set_ubits(inv_dp, out_mw);

    auto kept = make_node(Ntype_op::And);
    setup_sink_by_name(kept, "as").connect_driver(base.pin);
    setup_sink_by_name(kept, "as").connect_driver(inv_dp);
    auto kept_dp = kept.create_driver_pin(0);
    set_ubits(kept_dp, out_mw);

    auto inserted = make_node(Ntype_op::And);
    setup_sink_by_name(inserted, "as").connect_driver(shifted_value);
    setup_sink_by_name(inserted, "as").connect_driver(mask);
    auto inserted_dp = inserted.create_driver_pin(0);
    set_ubits(inserted_dp, out_mw);

    auto merged = make_node(Ntype_op::Or);
    setup_sink_by_name(merged, "as").connect_driver(kept_dp);
    setup_sink_by_name(merged, "as").connect_driver(inserted_dp);
    auto merged_dp = merged.create_driver_pin(0);
    set_ubits(merged_dp, out_mw);
    return merged_dp;
  }

  // `dst#[lo..=hi] = value` with runtime endpoints. The language operation
  // stays a range write through LNAST; tolg materializes its hardware as one
  // packed RMW. Later SROA can distribute the resulting value over leaves.
  void lower_dynamic_range_update(const Lnast_nid& dst, const Lnast_nid& val, const Lnast_nid& ins, const Lnast_nid& lo,
                                  const Lnast_nid& hi, const Lnast_nid& loc_nid) {
    auto base = set_mask_base(val);
    auto n    = leaf(lo);
    auto m    = leaf(hi);
    auto iv   = leaf(ins);

    // width = hi + 1 - lo, clamped to 0 on a reversed range so the mask comes
    // out 0 and the RMW leaves `dst` untouched. The clamped value is
    // non-negative by construction, so the shift amounts below never see a
    // negative-as-unsigned count.
    auto rw = lower_range_width(n, m);

    auto pow = make_node(Ntype_op::SHL);
    setup_sink_by_name(pow, "a").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    setup_sink_by_name(pow, "b").connect_driver(rw.clamped);
    auto pow_dp = pow.create_driver_pin(0);
    set_ubits(pow_dp, std::max<int32_t>(base.mw + 1, 2));

    auto low_mask = make_node(Ntype_op::Sum);
    setup_sink_by_name(low_mask, "as").connect_driver(pow_dp);
    setup_sink_by_name(low_mask, "bs").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    auto low_mask_dp = low_mask.create_driver_pin(0);
    set_ubits(low_mask_dp, std::max<int32_t>(base.mw + 1, 2));

    auto maskn = make_node(Ntype_op::SHL);
    setup_sink_by_name(maskn, "a").connect_driver(low_mask_dp);
    setup_sink_by_name(maskn, "b").connect_driver(n.pin);
    auto mask_dp = maskn.create_driver_pin(0);
    set_ubits(mask_dp, std::max<int32_t>(base.mw, 1));

    auto shifted = make_node(Ntype_op::SHL);
    setup_sink_by_name(shifted, "a").connect_driver(iv.pin);
    setup_sink_by_name(shifted, "b").connect_driver(n.pin);
    auto shifted_dp = shifted.create_driver_pin(0);
    set_ubits(shifted_dp, std::max<int32_t>(base.mw, 1));

    auto merged = lower_dynamic_mask_rmw(base, mask_dp, shifted_dp);
    record_set_mask_result(lnast_->get_name(dst), merged, std::max<int32_t>(base.mw, 1));
    lower_range_assert(rw.reversed, loc_nid);
  }

  // `dst#[idx] = value`: prp2lnast has already built the one-hot mask `1<<idx`.
  // Multiplying that mask by the checked one-bit value places the insertion at
  // the selected position without recovering the original index expression.
  void lower_dynamic_bit_update(const Lnast_nid& dst, const Lnast_nid& val, const Lnast_nid& mask_op, const Lnast_nid& ins) {
    auto base = set_mask_base(val);
    auto mask = leaf(mask_op);
    auto iv   = leaf(ins);

    auto placed = make_node(Ntype_op::Mult);
    setup_sink_by_name(placed, "as").connect_driver(mask.pin);
    setup_sink_by_name(placed, "as").connect_driver(iv.pin);
    auto placed_dp = placed.create_driver_pin(0);
    set_ubits(placed_dp, std::max<int32_t>(base.mw, 1));

    auto merged = lower_dynamic_mask_rmw(base, mask.pin, placed_dp);
    record_set_mask_result(lnast_->get_name(dst), merged, std::max<int32_t>(base.mw, 1));
  }

  // set_mask(ref(dst), value, mask, ins).
  void lower_set_mask(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto val = lnast_->get_sibling_next(dst);
    if (val.is_invalid()) {
      return;
    }
    auto mask_op = lnast_->get_sibling_next(val);
    if (mask_op.is_invalid()) {
      return;
    }
    auto ins = lnast_->get_sibling_next(mask_op);
    if (ins.is_invalid()) {
      return;
    }
    // An open-ended bit-range WRITE `dst#[lo..] = ins` would have to derive its
    // top bit from the destination's DECLARED width, which is not tracked per
    // logical name here (only the current value's width is). Closing it to the
    // current width would silently drop or extend bits, so require the explicit
    // upper bound instead. The READ form `a#[lo..]` IS supported
    // (lower_open_range_select). (range_open_map_ holds only comptime-lo
    // opens.)
    if (auto it = range_open_map_.find(std::string{lnast_->get_name(mask_op)}); it != range_open_map_.end()) {
      error_at(nid,
               {"open-range-write", "unsupported"},
               "upass.tolg: open-ended bit-range write `dst#[{}..] = …` is not "
               "supported — give the upper bound "
               "explicitly, e.g. `dst#[{}..=<msb>]`",
               lnast_->get_name(it->second),
               lnast_->get_name(it->second));
    }
    const std::string mask_name{lnast_->get_name(mask_op)};
    if (auto it = range_dyn_map_.find(mask_name); it != range_dyn_map_.end()) {
      if (Lnast_ntype::is_const(lnast_->get_type(it->second.second)) && lnast_->get_name(it->second.second) == "nil") {
        error_at(nid,
                 {"open-range-write", "unsupported"},
                 "upass.tolg: open-ended runtime bit-range write is not supported — give the upper bound explicitly");
      }
      lower_dynamic_range_update(dst, val, ins, it->second.first, it->second.second, nid);
      return;
    }
    const bool runtime_mask = !Lnast_ntype::is_const(lnast_->get_type(mask_op)) && !range_map_.contains(mask_name);
    if (runtime_mask) {
      lower_dynamic_bit_update(dst, val, mask_op, ins);
      return;
    }
    auto mask = mask_from_operand(mask_op);

    // A partial bit-range WRITE of a declared reg/wire is a next-state
    // read-modify-write: the destination NAME stays bound to q (a reg) / the
    // buffer output (a wire) so OTHER reads keep Verilog `<=` semantics, while
    // the masked result accumulates on the SHADOW din key. Without this the old
    // `bind_result(dst)` rebound the name to the combinational set_mask result:
    // the reg/wire din was never set (finalize_regs then wired din=q, an
    // identity `___next = q`), so EVERY partial write (`r.field <= …`,
    // `r[hi:lo] <= …`) was silently dropped. The din shadow also lets a chain
    // of partial writes ACCUMULATE: the 2nd `r[..]<=` must read the 1st write's
    // result, not q — so when the base operand is the destination itself, read
    // the current din accumulator (if any) instead of resolving the name to q.
    const std::string dst_name{lnast_->get_name(dst)};
    // RMW base = current din accumulator if a prior partial write set it, else
    // the committed value (q / buffer output) via the name. This must key off
    // the BASE OPERAND's name, not just dst: prp2lnast lowers `r#[lo..=hi] = x`
    // to a copy-temp shape (`set_mask %t, r, mask, x` + `r = %t`), so dst is a
    // TEMP while the read of `r` still needs the accumulated value — resolving
    // it to q made the SECOND conditional partial write rebuild its whole-word
    // image from stale q and silently drop the first write when both enables
    // fired (the DataModule__64entry per-entry register file: entry 63's write
    // vanished under a same-cycle entry-62 write).
    Val               vv;
    bool              base_from_accum = false;
    if (Lnast_ntype::is_ref(lnast_->get_type(val))) {
      const std::string val_name{lnast_->get_name(val)};
      const bool        vreg  = reg_map_.contains(val_name) && reg_info_.contains(val_name);
      const bool        vwire = !vreg && wire_names_.contains(val_name);
      if (vreg || vwire) {
        if (auto dit = pin_map_.find(din_key(val_name)); dit != pin_map_.end()) {
          vv              = Val{dit->second, mw_lookup(din_key(val_name))};
          base_from_accum = true;
        }
      }
    }
    if (!base_from_accum) {
      vv = set_mask_base(val);
    }

    auto node = make_node(Ntype_op::Set_mask);
    setup_sink_by_name(node, "a").connect_driver(vv.pin);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *mask));
    setup_sink_by_name(node, "value").connect_driver(leaf(ins).pin);
    int32_t mask_mw = mask_high_bit(*mask);
    auto    drv     = node.create_driver_pin(0);
    int32_t res_mw  = std::max(vv.mw, mask_mw);
    set_ubits(drv, res_mw);
    record_set_mask_result(dst_name, drv, res_mw);
  }

  // ── concat ───────────────────────────────────────────────────────────────
  //
  // `concat( dst, v_msb, w_msb, …, v_lsb, w_lsb )` lowers 1:1 to one
  // Ntype_op::Concat: the LNAST node ALREADY carries the interleaved
  // (value, width) shape, and the cell's sinks are the same pairs at pids
  // 2i / 2i+1, MSB-first.
  //
  // A width operand may still be the `nil` sentinel here, meaning no upass pass
  // could bind it from the lane's declared type. That is a HARD ERROR, never a
  // guess: mw_lookup() would happily hand back the live value's width (or its
  // default of 1), and the value's width is precisely what a concat may not be
  // sized by -- narrowing one lane shifts every lane above it.
  //
  // This is also the LAST place the destination's declared width is checked.
  // The concat's own dst is a compiler temp, so the user-facing `c:u12 = …`
  // check rides where that temp is BOUND to a declared name; see
  // check_concat_dest_width, called from the declare/store paths.

  // The bound width of one lane's width operand, or nullopt when it is `nil`
  // (or otherwise not a positive comptime integer).
  [[nodiscard]] std::optional<int32_t> concat_bound_width(const Lnast_nid& nid) const {
    if (nid.is_invalid() || !Lnast_ntype::is_const(lnast_->get_type(nid))) {
      return std::nullopt;
    }
    const auto txt = lnast_->get_name(nid);
    if (txt.empty() || txt == "nil") {
      return std::nullopt;
    }
    auto v = Dlop::from_pyrope(txt);
    if (!v || !v->is_integer() || !v->is_just_i64()) {
      return std::nullopt;
    }
    const auto w = v->to_just_i64();
    if (w <= 0 || w > std::numeric_limits<int32_t>::max()) {
      return std::nullopt;
    }
    return static_cast<int32_t>(w);
  }

  void lower_concat(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }

    // Resolve every lane FIRST: a bad lane must abort before any cell is
    // minted, so a rejected concat leaves no half-wired node behind.
    struct Lane {
      Lnast_nid nid;
      int32_t   width;
    };
    std::vector<Lane> lanes;
    for (auto v = lnast_->get_sibling_next(dst); !v.is_invalid();) {
      auto w = lnast_->get_sibling_next(v);
      if (w.is_invalid()) {
        error_at(nid,
                 {"concat-malformed", "internal"},
                 "upass.tolg: concat lane '{}' has no width operand — every lane is a (value, width) PAIR",
                 lnast_->get_name(v));
      }
      const auto bound = concat_bound_width(w);
      if (!bound) {
        error_at(nid,
                 {"concat-untyped-lane", "type"},
                 "upass.tolg: concat lane '{}' has no declared bit width — a concat window is sized by the lane's "
                 "DECLARED type, never by its value or an inferred range, because narrowing one lane would shift "
                 "every lane above it (bind it to a typed name first: `const w:u4 = <expr>`)",
                 lnast_->get_name(v));
      }
      lanes.push_back(Lane{v, *bound});
      v = lnast_->get_sibling_next(w);
    }
    if (lanes.empty()) {
      error_at(nid, {"concat-empty", "type"}, "upass.tolg: concat needs at least one lane");
    }

    auto    node   = make_node(Ntype_op::Concat);
    int32_t sum_mw = 0;
    for (size_t i = 0; i < lanes.size(); ++i) {
      auto v = leaf(lanes[i].nid);
      node.create_sink_pin(static_cast<hhds::Port_id>(2 * i)).connect_driver(v.pin);
      node.create_sink_pin(static_cast<hhds::Port_id>(2 * i + 1))
          .connect_driver(create_const(*g_, *Dlop::create_integer(lanes[i].width)));
      sum_mw += lanes[i].width;
    }

    auto out = node.create_driver_pin(0);
    // The assembled value is always NON-NEGATIVE, so bind_result stamps the
    // exact literal sum(w_i) width as unsigned.
    bind_result(lnast_->get_name(dst), out, sum_mw);
    // The result has a declared width BY CONSTRUCTION, which is what makes
    // `concat(concat(a,b), c)` legal and what the destination check compares
    // a declared `c:u12` against.
    record_decl_type(lnast_->get_name(dst), sum_mw, /*is_signed=*/false);
    concat_result_mw_[logical_key(lnast_->get_name(dst))] = sum_mw;
  }

  // `const c:u12 = concat(a:u4, b:u8)` — the destination's declared width must
  // equal the lane sum EXACTLY. Not `>=`: a concat states a bit layout, and a
  // destination that quietly zero-extends it is a layout the source does not
  // say. Signedness is free (`u12` and `s12` are both 12-bit fields), so only
  // the width is compared.
  //
  // Checked where the concat's TEMP is bound to a declared name, because the
  // concat node's own dst is always a compiler temp.
  void check_concat_dest_width(const Lnast_nid& anchor, std::string_view dest_name, const Lnast_nid& value_nid) {
    // Pyrope only -- see the twin guard in upass.runner. Verilog declares its
    // widths its own way and its assignment rules pad/truncate rather than
    // reject, so the Pyrope "destination states the layout" rule would refuse
    // ordinary imported RTL.
    if (lnast_->is_verilog_origin()) {
      return;
    }
    if (value_nid.is_invalid() || !Lnast_ntype::is_ref(lnast_->get_type(value_nid))) {
      return;
    }
    auto cit = concat_result_mw_.find(logical_key(lnast_->get_name(value_nid)));
    if (cit == concat_result_mw_.end()) {
      return;  // not a concat result
    }
    // Only a SOURCE-level destination is checked -- the same exemption
    // upass.runner's twin (check_concat_dest) documents. A store into a
    // COMPILER TEMP (`___N`, an SSA staging name a frontend minted) is an
    // internal move, not a declaration the user wrote: demanding a declared
    // type of it would turn a frontend's own staging store into a hard
    // `concat-untyped-dest` error on legal source.
    if (const auto dkey = logical_key(dest_name); dkey.empty() || dkey.starts_with("___")) {
      return;
    }
    auto dt = decl_type_lookup(dest_name);
    if (!dt) {
      error_at(anchor,
               {"concat-untyped-dest", "type"},
               "upass.tolg: '{}' is assigned a concat but has no declared type — a concat's destination must declare "
               "the {}-bit width the lanes add up to (e.g. `{}:u{}` or `{}:s{}`)",
               dest_name,
               cit->second,
               dest_name,
               cit->second,
               dest_name,
               cit->second);
    }
    if (dt->mw != cit->second) {
      error_at(anchor,
               {"concat-width-mismatch", "type"},
               "upass.tolg: '{}' is declared {} bits but the concat assigned to it is {} bits — a concat's "
               "destination must match the lane sum EXACTLY, so that the bit layout the source states is the layout "
               "the destination has",
               dest_name,
               dt->mw,
               cit->second);
    }
  }

  enum class OpW { add, mul, maxw, andw, firstw, boolw, shlw };

  // n-ary op: child0 = dst, children 1..N = operands. Commutative ops feed all
  // operands into sink "a"; positional binary ops use "a" then "b".
  void lower_op(const Lnast_nid& nid, Ntype_op op, bool commutative, OpW wmode) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto    node           = make_node(op);
    int32_t max_mw         = 0;
    int32_t sum_mw         = 0;
    int32_t min_nonneg_mw  = 0;      // andw: narrowest NON-NEGATIVE operand...
    bool    any_nonneg     = false;  // ...which is only meaningful once this is set (mw 0 is legal)
    bool    any_negative   = false;  // at least one operand may carry a negative value
    int32_t signed_mw      = 0;      // width needed if the result must use a signed carrier
    bool    first_negative = false;  // shifts take their result sign from the value operand
    int32_t first_mw       = 0;
    int32_t second_mw      = 0;   // shift-amount magnitude width (shlw)
    int64_t shl_amt        = -1;  // shift-amount value when constant (shlw); <0 = dynamic
    bool    first          = true;
    int     opnd_idx       = 0;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      auto v        = leaf(c);
      max_mw        = std::max(max_mw, v.mw);
      sum_mw       += v.mw;
      any_negative  = any_negative || pin_can_be_negative(v.pin);
      // A literal uW operand needs W+1 bits when represented in a signed
      // carrier; an sW operand already includes its sign bit.  Bitwise ops
      // become signed when any operand may be negative, so remember the
      // widest lossless signed representation while walking the inputs.
      signed_mw     = std::max(signed_mw, v.mw + (pin_can_be_negative(v.pin) ? 0 : 1));
      if (wmode == OpW::andw && livehd::graph_util::is_const_pin(v.pin)) {
        // A bitwise AND is bounded by its NARROWEST NON-NEGATIVE operand: `x & m`
        // can only keep bits that `m` has set, so the result never exceeds m --
        // whatever x is, and however wide.
        //
        // Only a NON-NEGATIVE operand bounds it. A negative one is an infinite
        // run of leading ones (`x & -2` keeps every bit of x but bit 0), so its
        // magnitude width bounds nothing and `min` there would truncate.
        //
        // CONSTANTS ONLY, deliberately. A non-const operand's `is_unsign` stamp
        // is NOT a proof of non-negativity: bind_result stamps every computed op
        // unsigned, and the OPEN note at the end of this function records that a
        // BITWISE op over signed operands is stamped unsigned while its value is
        // negative (`sa ^ sb` can be -1). Narrowing an AND to such an operand's
        // magnitude width truncates the OTHER operand -- `(-1) & w` must be w,
        // but a 5-bit stamp keeps only w's low 5 bits, a silent miscompile that
        // OpW::maxw did not have. Trusting the stamp measured just 1.1 points
        // better on minion (-16.2% vs -15.1% of op words); the win is dominated
        // by the constant masks, which are exact.
        //
        // Test the lowered PIN rather than the LNAST leaf kind: cprop can prove
        // a reference temporary constant before tolg, and that constant is just
        // as valid a mask. The value comes from that pin, so there is no second
        // Dlop parse.
        const auto cv = livehd::graph_util::hydrate_const(v.pin);
        if (cv.is_numeric() && !cv.is_negative() && (!any_nonneg || v.mw < min_nonneg_mw)) {
          min_nonneg_mw = v.mw;
          any_nonneg    = true;
        }
      }
      if (first) {
        first_mw       = v.mw;
        first_negative = pin_can_be_negative(v.pin);
      } else if (second_mw == 0) {
        second_mw = v.mw;
        if (Lnast_ntype::is_const(lnast_->get_type(c))) {
          // Read back the const pin leaf() built, rather than re-parsing the
          // same literal text a second time.
          const auto cv = livehd::graph_util::hydrate_const(v.pin);
          if (cv.is_just_i64()) {
            shl_amt = cv.to_just_i64();
          }
        }
      }
      // SHL b is single-driver: the runtime one-hot `a << (b0, b1, …)` form was
      // removed (comptime cases fold in upass.constprop before reaching tolg).
      // A 3rd+ operand would build a multi-driver b, so reject it cleanly.
      if (op == Ntype_op::SHL && opnd_idx >= 2) {
        livehd::diag::err("upass.tolg", "shl-onehot-removed", "unsupported")
            .msg(
                "runtime one-hot shift 'a << (b0, b1, ...)' is no longer "
                "supported; shift by a single amount")
            .emit();
        break;
      }
      // op varies (Sum/Mult/And/.../Div/SHL/SRA): address by pid so the right
      // sink name resolves per op (pid 0 = a/as, pid 1 = b) without hardcoding.
      node.create_sink_pin((commutative || first) ? 0 : 1).connect_driver(v.pin);
      first = false;
      ++opnd_idx;
    }
    int32_t mw = 1;
    switch (wmode) {
      case OpW::add   : mw = static_cast<int32_t>(max_mw + 1); break;
      case OpW::mul   : mw = sum_mw > 0 ? sum_mw : int32_t{1}; break;
      case OpW::maxw  : mw = max_mw; break;
      // AND narrows: bounded by the narrowest non-negative operand when there is
      // one, else (no operand proven non-negative) it keeps the widest.
      case OpW::andw  : mw = any_nonneg ? min_nonneg_mw : max_mw; break;
      case OpW::firstw: mw = first_mw; break;
      case OpW::boolw : mw = 1; break;
      case OpW::shlw  : {
        // A left shift GROWS: out = a_width + shift_amount. A constant amount is
        // exact; a dynamic amount uses the 2^amount_width-1 upper bound (capped
        // to avoid pathological blow-up). Without this the result kept the input
        // width (old OpW::maxw) and `b<<N` silently truncated in intermediates.
        int64_t grow = shl_amt >= 0 ? shl_amt : (second_mw >= 12 ? int64_t{4096} : ((int64_t{1} << second_mw) - 1));
        mw           = static_cast<int32_t>(first_mw + grow);
        break;
      }
    }
    auto out = node.create_driver_pin(0);
    bind_result(lnast_->get_name(dst), out, mw);
    // A subtraction can go negative regardless of operand signs. An addition
    // can go negative whenever at least one operand can. In either case the
    // arithmetic carrier is signed; only an explicit mask/cast may turn its
    // finite low-bit projection into an unsigned value.
    if (op == Ntype_op::Sum && ((!commutative && opnd_idx >= 2) || any_negative)) {
      set_sbits(out, std::max<int32_t>(1, mw));
    }
    if ((op == Ntype_op::Mult || op == Ntype_op::Div) && any_negative) {
      set_sbits(out, std::max<int32_t>(1, mw));
    }
    if ((op == Ntype_op::SHL || op == Ntype_op::SRA) && first_negative) {
      set_sbits(out, std::max<int32_t>(1, mw));
    }
    // Or/Xor preserve the infinite leading ones of a negative operand. And does
    // too only when every operand may be negative: one proven non-negative
    // operand is a finite mask and bounds the result to its own unsigned width.
    // Keep the unbounded cases signed at the widest lossless signed width.
    if ((op == Ntype_op::Or || op == Ntype_op::Xor || (op == Ntype_op::And && !any_nonneg)) && any_negative) {
      set_sbits(out, std::max<int32_t>(1, signed_mw));
    }
  }

  // LNAST sext(dst, a, b): reinterpret bit POSITION b of `a` as the sign
  // (the Dlop::sext_op convention constprop folds with). The LGraph Sext
  // cell's b operand is the kept bit COUNT instead (cgen slices [b-1:0],
  // bitwidth ranges sbits=b, lgyosys Pick passes the width) - convert here.
  // The result is SIGNED with meaningful width b+1.
  void lower_sext(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto b = lnast_->get_sibling_next(a);
    if (b.is_invalid()) {
      return;
    }
    if (!Lnast_ntype::is_const(lnast_->get_type(b))) {
      error_at(b,
               {"sext-runtime-pos", "unsupported"},
               "upass.tolg: sign-extend with a runtime sign position has no "
               "lowering — the sign-bit position must be a "
               "compile-time constant");
    }
    const auto pos  = const_val(b);
    auto       av   = leaf(a);
    auto       node = make_node(Ntype_op::Sext);
    setup_sink_by_name(node, "a").connect_driver(av.pin);
    setup_sink_by_name(node, "b").connect_driver(create_const(*g_, *Dlop::create_integer(pos + 1)));
    const auto mw  = static_cast<int32_t>(pos) + 1;
    auto       drv = node.create_driver_pin(0);
    set_bits(drv, mw > 0 ? mw : 1);
    set_sign(drv);
    record(lnast_->get_name(dst), drv, mw > 0 ? mw : 1);
  }

  // `~x` — bitwise NOT (the only unary lowered through here).
  //
  // An unsigned `mw`-bit operand ranges through 2^mw-1. Flipping its unlimited
  // leading zeros makes the result negative — range
  // [-(2^mw), -1] — and has to be stamped SIGNED across all mw+1 bits, the same
  // shape lower_sext() uses for its signed result.
  //
  // Binding it through bind_result() stamped it UNSIGNED instead. Consumers
  // then disagreed about the required sign extension: the LEC read one bit too few, and abc
  // zero-filled a widening that had to sign-extend — `(~ec)#[0..=9]` mapped to
  // 511 where the RTL says 1023, i.e. a genuinely wrong netlist. (cgen emits an
  // explicitly signed net and so stayed correct, which is what made this look
  // like an abc-only bug.)
  void lower_unary(const Lnast_nid& nid, Ntype_op op) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto v    = leaf(a);
    auto node = make_node(op);
    setup_sink_by_name(node, "a").connect_driver(v.pin);
    const int32_t m     = v.mw > 0 ? v.mw : 1;
    const int32_t out_m = pin_can_be_negative(v.pin) ? m : m + 1;
    auto          drv   = node.create_driver_pin(0);
    set_sbits(drv, out_m);
    record(lnast_->get_name(dst), drv, out_m);
  }

  // `!x` is a truth-value negation, not the signed bitwise `~x` operation.
  // Pyrope conditions may carry a wider integer, so compare against zero;
  // XOR-with-one is equivalent only after a separate u1 proof.
  void lower_log_not(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto v   = leaf(a);
    auto neg = make_node(Ntype_op::EQ);  // commutative: both operands feed sink "a"
    setup_sink_by_name(neg, "as").connect_driver(v.pin);
    setup_sink_by_name(neg, "as").connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    bind_result(lnast_->get_name(dst), neg.create_driver_pin(0), 1);
  }

  // ne/le/ge = Not(eq/gt/lt(...)). Result is 1-bit boolean.
  void lower_negated(const Lnast_nid& nid, Ntype_op inner_op, bool commutative) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto inner = make_node(inner_op);
    bool first = true;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      // inner_op is EQ/GT/LT; address by pid (0 = a/as, 1 = bs) so the right
      // multi-driver sink name resolves per op without hardcoding.
      inner.create_sink_pin((commutative || first) ? 0 : 1).connect_driver(leaf(c).pin);
      first = false;
    }
    // The inner comparator (EQ/GT/LT) is a literal one-bit unsigned boolean.
    // Without this the inner
    // driver pin is left at bits==0 and leaks an unbounded cell past tolg into
    // cprop/cgen (e.g. slang `!=` lowering to ~(a==b)).
    auto inner_dp = inner.create_driver_pin(0);
    set_ubits(inner_dp, 1);
    // Logical negation of a u1 is XOR with one.
    auto neg = make_node(Ntype_op::Xor);  // commutative: both operands feed sink "a"
    setup_sink_by_name(neg, "as").connect_driver(inner_dp);
    setup_sink_by_name(neg, "as").connect_driver(create_const(*g_, *Dlop::create_integer(1)));
    bind_result(lnast_->get_name(dst), neg.create_driver_pin(0), 1);
  }

  // ── Bit-insensitive reductions (`foo#|/&/^/+[range]`)
  // ────────────────────────
  //
  // prp2lnast lowers every `foo#OP[range]` to a `get_mask` (which packs the
  // selected bits LSB-first into an unsigned slice `rr` of width
  // popcount(mask)) followed by the reduction node over `rr`. A comptime `foo`
  // folds in constprop; only a RUNTIME `foo` reaches tolg here, so the
  // reduction's single operand is always the already-lowered get_mask result,
  // whose magnitude width
  // (`av.mw`) is the selected-bit count `k`. An open `#[..]` masks every bit,
  // so `k` is then `foo`'s full width — the "use the type's number of bits"
  // rule.

  // Explode the low `mw` bits of `src` into individual 1-bit driver pins (bit i
  // packed to position 0 via Get_mask). Shared by the parity (XOR) and popcount
  // (adder) trees.
  [[nodiscard]] std::vector<Pin> explode_bits(const Pin& src, int32_t mw) {
    const int32_t    n = mw > 0 ? mw : 1;
    std::vector<Pin> bits;
    bits.reserve(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
      auto gm = make_node(Ntype_op::Get_mask);
      setup_sink_by_name(gm, "a").connect_driver(src);
      setup_sink_by_name(gm, "mask").connect_driver(create_const(*g_, *Dlop::get_mask_value(i, i)));  // bit i only
      auto b = gm.create_driver_pin(0);
      set_ubits(b, 1);
      bits.push_back(b);
    }
    return bits;
  }

  // `foo#|[range]`: OR-reduce the selected bits → int 0/1. The graph Ror cell
  // reduces every bit of its operand (cgen emits `|expr`).
  void lower_red_or(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto av   = leaf(a);
    auto node = make_node(Ntype_op::Ror);
    setup_sink_by_name(node, "as").connect_driver(av.pin);
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), 1);
  }

  // `foo#&[range]`: AND-reduce → int 0/1. Sign-extend the packed slice from its
  // top bit so an all-ones slice reads as the signed -1, then compare `== -1`
  // (a width-independent all-ones test; 1 iff every selected bit is set).
  void lower_red_and(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto          av = leaf(a);
    const int32_t k  = av.mw > 0 ? av.mw : 1;  // selected-bit count
    // Sext cell `b` operand is the kept bit COUNT (cgen slices [b-1:0]); the
    // result is signed with width k, == -1 exactly when bits 0..k-1 are all
    // set.
    auto          sx = make_node(Ntype_op::Sext);
    setup_sink_by_name(sx, "a").connect_driver(av.pin);
    setup_sink_by_name(sx, "b").connect_driver(create_const(*g_, *Dlop::create_integer(k)));
    auto srr = sx.create_driver_pin(0);
    set_bits(srr, k);
    set_sign(srr);
    auto eq = make_node(Ntype_op::EQ);  // commutative: both operands feed sink "a"
    setup_sink_by_name(eq, "as").connect_driver(srr);
    setup_sink_by_name(eq, "as").connect_driver(create_const(*g_, *Dlop::create_integer(-1)));
    bind_result(lnast_->get_name(dst), eq.create_driver_pin(0), 1);
  }

  // `foo#^[range]`: XOR-reduce (parity) → int 0/1, via a balanced binary tree
  // of 2-input Xor cells over the exploded bits (an odd leaf carries up a
  // level).
  void lower_red_xor(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto av   = leaf(a);
    auto bits = explode_bits(av.pin, av.mw);
    while (bits.size() > 1) {
      std::vector<Pin> next;
      next.reserve((bits.size() + 1) / 2);
      for (size_t i = 0; i + 1 < bits.size(); i += 2) {
        auto x = make_node(Ntype_op::Xor);  // commutative: both into "a"
        setup_sink_by_name(x, "as").connect_driver(bits[i]);
        setup_sink_by_name(x, "as").connect_driver(bits[i + 1]);
        auto d = x.create_driver_pin(0);
        set_ubits(d, 1);
        next.push_back(d);
      }
      if (bits.size() & 1) {
        next.push_back(bits.back());  // odd leaf rides to the next level
      }
      bits = std::move(next);
    }
    bind_result(lnast_->get_name(dst), bits.front(), 1);
  }

  // `foo#+[range]`: popcount (number of set bits) → integer, via a balanced
  // binary adder tree over the exploded bits. Each Sum grows the width by one;
  // the final result holds 0..k.
  void lower_popcount(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto             av   = leaf(a);
    auto             bits = explode_bits(av.pin, av.mw);
    std::vector<Val> terms;
    terms.reserve(bits.size());
    for (const auto& b : bits) {
      terms.push_back({b, 1});
    }
    while (terms.size() > 1) {
      std::vector<Val> next;
      next.reserve((terms.size() + 1) / 2);
      for (size_t i = 0; i + 1 < terms.size(); i += 2) {
        auto s = make_node(Ntype_op::Sum);  // both operands ADD on sink "a"
        setup_sink_by_name(s, "as").connect_driver(terms[i].pin);
        setup_sink_by_name(s, "as").connect_driver(terms[i + 1].pin);
        const int32_t mw = std::max(terms[i].mw, terms[i + 1].mw) + 1;
        auto          d  = s.create_driver_pin(0);
        set_ubits(d, mw);
        next.push_back({d, mw});
      }
      if (terms.size() & 1) {
        next.push_back(terms.back());
      }
      terms = std::move(next);
    }
    bind_result(lnast_->get_name(dst), terms.front().pin, terms.front().mw);
  }

  // ── `a % b` (modulo) — the easy, unambiguous cases only
  // ───────────────────── Pyrope has no general hardware modulo: the signed
  // semantics differ across languages and a full divider is expensive (docs
  // pyrope/02-basics). LiveHD uses TRUNCATED semantics (the remainder's sign
  // follows the dividend, matching Dlop::rem_op and Verilog `%`). We lower the
  // cases that collapse to shift/mask and HARD-error the rest:
  //   (2) |a| < |b| over their whole ranges          → a            (no
  //   remainder) (1) b a comptime power-of-two, a non-negative   → a & (|b|-1)
  //   (low bits) (3) |b| == 3 (comptime),       a non-negative   → base-4
  //   digit-sum reduce
  // Build the general `a % b` cell. |a % b| <= |a| under truncated semantics, so
  // the dividend's width is always a sound result width. The result sign follows
  // the dividend, which is also why there is exactly ONE remainder op.
  void emit_rem(const std::string& dst_name, const Val& av, const Lnast_nid& b) {
    auto remn = make_node(Ntype_op::Rem);
    setup_sink_by_name(remn, "a").connect_driver(av.pin);
    setup_sink_by_name(remn, "b").connect_driver(leaf(b).pin);
    const int32_t bits = av.mw > 0 ? av.mw : 1;
    auto          out  = remn.create_driver_pin(0);
    bind_result(dst_name, out, bits);
    if (pin_can_be_negative(av.pin)) {
      set_sbits(out, bits);
    }
  }

  void lower_mod(const Lnast_nid& nid) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto a = lnast_->get_sibling_next(dst);
    if (a.is_invalid()) {
      return;
    }
    auto b = lnast_->get_sibling_next(a);
    if (b.is_invalid()) {
      error_here("upass.tolg: modulo in '{}' is missing its divisor operand", lnast_->get_top_module_name());
      return;
    }

    const std::string dst_name{lnast_->get_name(dst)};
    auto              av      = leaf(a);
    const auto        a_range = range_of_operand(a);
    const auto        b_range = range_of_operand(b);

    // `a` is non-negative when its pin is unsigned (the common case — every
    // typed unsigned port and every computed result reads unsigned) or its
    // published range proves min >= 0.
    const bool a_nonneg = is_unsign(av.pin) || (a_range && a_range->first >= 0);

    // (2) Range fit. If |a| < |b| for every (a,b) pair then `a % b == a` under
    // truncated semantics (the quotient truncates to 0), regardless of sign.
    if (a_range && b_range && a_range->first != std::numeric_limits<int64_t>::min()
        && a_range->second != std::numeric_limits<int64_t>::min()) {
      const int64_t a_absmax = std::max(iabs64(a_range->first), iabs64(a_range->second));
      // Smallest |b| over b's range. 0 when the range straddles 0 (a possible
      // divisor of 0/±1 is not a guaranteed no-op), which fails the test below.
      int64_t       b_absmin = 0;
      if (b_range->first >= 1) {
        b_absmin = b_range->first;
      } else if (b_range->second <= -1 && b_range->second != std::numeric_limits<int64_t>::min()) {
        b_absmin = -b_range->second;
      }
      if (b_absmin >= 2 && a_absmax < b_absmin) {
        record(dst_name, av.pin, av.mw);  // `a % b == a` — alias the dividend
        return;
      }
    }

    // A RUNTIME divisor is a first-class remainder cell. It used to be a hard
    // error here, which put the "can this be synthesized?" question in the
    // wrong pass: bitwidth, constprop, LEC and sim can all reason about `%`
    // perfectly well, and only the netlist mapper cannot. pass.abc raises the
    // diagnostic now, so the rest of LiveHD handles remainder normally.
    if (!Lnast_ntype::is_const(lnast_->get_type(b))) {
      emit_rem(dst_name, av, b);
      return;
    }
    const int64_t bval = const_val(b);
    if (bval == 0) {  // constprop already errors comptime mod-by-zero; guard anyway.
      error_at(b, {"mod-by-zero", "type"}, "upass.tolg: modulo by zero is an illegal operation");
      return;
    }
    const int64_t babs = iabs64(bval);
    if (babs == 1) {  // `a % ±1 == 0` for any sign of `a`.
      record(dst_name, create_const(*g_, *Dlop::create_integer(0)), 1);
      return;
    }

    // A possibly-NEGATIVE dividend is no longer ambiguous: the op is defined as
    // TRUNCATED remainder (sign follows the dividend, like Verilog `%` and
    // Dlop::rem_op), so it needs no constraint on `a` -- only the mask/digit-sum
    // shortcuts below do, and those are optimizations, not the semantics.
    if (!a_nonneg) {
      emit_rem(dst_name, av, b);
      return;
    }

    // (1) Power-of-two divisor → mask off the low log2(|b|) bits.
    if ((babs & (babs - 1)) == 0) {
      int32_t k = 0;  // |b| == 2^k
      while ((int64_t{1} << k) < babs) {
        ++k;
      }
      auto andn = make_node(Ntype_op::And);  // commutative: both operands feed sink "a"
      setup_sink_by_name(andn, "as").connect_driver(av.pin);
      setup_sink_by_name(andn, "as").connect_driver(create_const(*g_, *Dlop::create_integer(babs - 1)));
      bind_result(dst_name, andn.create_driver_pin(0), k > 0 ? k : 1);
      return;
    }

    // (3) Modulo 3 → base-4 digit-sum reduction. Needs a SOUND int64 upper
    // bound on `a` (the digit-sum loop's termination + width bound is int64
    // math): a non-negative published range, else the stored width when it fits
    // in 62 bits. A wider untyped dividend has no reliable int64 bound — hard
    // error.
    if (babs == 3) {
      int64_t init_max = 0;
      if (a_range && a_range->first >= 0) {
        init_max = a_range->second;  // exact, sound
      } else if (av.mw > 0 && av.mw <= 62) {
        init_max = (int64_t{1} << av.mw) - 1;  // sound for ≤ 62 bits
      } else {
        // No sound int64 bound for the digit-sum loop -- fall back to the cell.
        emit_rem(dst_name, av, b);
        return;
      }
      bind_result(dst_name, lower_mod3(av, init_max), 2);  // result in [0, 2]
      return;
    }

    // Any other divisor: the general cell.
    emit_rem(dst_name, av, b);
  }

  // `a % 3` for a non-negative `a` whose max value is `init_max`. Because
  // 4 ≡ 1 (mod 3), `a ≡ Σ(base-4 digits of a)  (mod 3)`. Each round splits the
  // value into 2-bit base-4 digits and sums them with a balanced adder tree;
  // the running max strictly shrinks while it is ≥ 4 (max_base4_digit_sum(M) <
  // M for M ≥ 4), so the generation loop terminates. Once the value is in [0,
  // 3] a single correction maps the one non-reduced point 3 → 0.
  [[nodiscard]] Pin lower_mod3(const Val& av, int64_t init_max) {
    Pin     cur     = av.pin;
    int64_t cur_max = init_max < 0 ? 0 : init_max;
    int32_t cur_mw  = mw_of_val(cur_max);
    if (cur_mw > av.mw && av.mw > 0) {
      cur_mw = av.mw;  // never read past the dividend's stored bits
    }
    while (cur_max >= 4) {
      std::vector<Val> digits;
      digits.reserve(static_cast<size_t>((cur_mw + 1) / 2));
      for (int32_t lo = 0; lo < cur_mw; lo += 2) {
        const int32_t hi = std::min(lo + 1, cur_mw - 1);
        auto          gm = make_node(Ntype_op::Get_mask);
        setup_sink_by_name(gm, "a").connect_driver(cur);
        setup_sink_by_name(gm, "mask").connect_driver(create_const(*g_, *Dlop::get_mask_value(hi, lo)));
        auto          d   = gm.create_driver_pin(0);
        const int32_t dmw = hi - lo + 1;  // 1 or 2 bits per base-4 digit
        set_ubits(d, dmw);
        digits.push_back({d, dmw});
      }
      cur     = adder_tree(std::move(digits)).pin;
      cur_max = max_base4_digit_sum(cur_max);
      cur_mw  = mw_of_val(cur_max);
    }
    // `cur` ∈ [0, 3]: result = (cur == 3) ? 0 : cur  ==  cur - 3*(cur == 3).
    auto eq = make_node(Ntype_op::EQ);  // commutative: both operands feed sink "a"
    setup_sink_by_name(eq, "as").connect_driver(cur);
    setup_sink_by_name(eq, "as").connect_driver(create_const(*g_, *Dlop::create_integer(3)));
    auto eqp = eq.create_driver_pin(0);
    set_ubits(eqp, 1);
    auto mul = make_node(Ntype_op::Mult);  // 3 * (cur == 3) ∈ {0, 3}
    setup_sink_by_name(mul, "as").connect_driver(eqp);
    setup_sink_by_name(mul, "as").connect_driver(create_const(*g_, *Dlop::create_integer(3)));
    auto mulp = mul.create_driver_pin(0);
    set_ubits(mulp, 2);
    auto sub = make_node(Ntype_op::Sum);  // cur - 3*(cur == 3)
    setup_sink_by_name(sub, "as").connect_driver(cur);
    setup_sink_by_name(sub, "bs").connect_driver(mulp);
    auto subp = sub.create_driver_pin(0);
    set_ubits(subp, 2);
    return subp;
  }

  // Balanced binary adder tree over `terms` (each a non-negative value).
  // Mirrors the popcount tree: every Sum grows the width by one bit; an odd
  // leaf rides up a level untouched. Requires at least one term.
  [[nodiscard]] Val adder_tree(std::vector<Val> terms) {
    while (terms.size() > 1) {
      std::vector<Val> next;
      next.reserve((terms.size() + 1) / 2);
      for (size_t i = 0; i + 1 < terms.size(); i += 2) {
        auto s = make_node(Ntype_op::Sum);  // both operands ADD on sink "a"
        setup_sink_by_name(s, "as").connect_driver(terms[i].pin);
        setup_sink_by_name(s, "as").connect_driver(terms[i + 1].pin);
        const int32_t mw = std::max(terms[i].mw, terms[i + 1].mw) + 1;
        auto          d  = s.create_driver_pin(0);
        set_ubits(d, mw);
        next.push_back({d, mw});
      }
      if (terms.size() & 1) {
        next.push_back(terms.back());  // odd leaf rides to the next level
      }
      terms = std::move(next);
    }
    return terms.front();
  }

  // Maximum base-4 digit sum over [0, M] (the standard "max digit sum of
  // numbers ≤ N" DP). Strictly less than M for M ≥ 4, which is what makes
  // lower_mod3's loop terminate.
  [[nodiscard]] static int64_t max_base4_digit_sum(int64_t M) {
    if (M < 0) {
      return 0;
    }
    int top = 0;  // highest base-4 digit position
    while (top < 31 && (int64_t{1} << (2 * (top + 1))) <= M) {
      ++top;
    }
    int64_t best = 0, prefix = 0;
    for (int pos = top; pos >= 0; --pos) {
      const int64_t d = (M >> (2 * pos)) & 3;
      if (d > 0) {  // drop this digit to d-1 and fill the rest with 3s
        best = std::max(best, prefix + (d - 1) + int64_t{3} * pos);
      }
      prefix += d;  // keep this digit tight
    }
    return std::max(best, prefix);  // ...or M itself
  }

  // |v| with INT64_MIN guarded (callers exclude it before reaching here).
  [[nodiscard]] static int64_t iabs64(int64_t v) {
    if (v == std::numeric_limits<int64_t>::min()) {
      return std::numeric_limits<int64_t>::max();
    }
    return v < 0 ? -v : v;
  }

  // Published bounded range by LNAST name. nullopt = unbounded / unavailable.
  [[nodiscard]] std::optional<std::pair<int64_t, int64_t>> range_of_name(std::string_view name) const {
    const std::string nm{name};
    const auto&       meta = lnast_->bw_meta();
    auto              it   = meta.ranges.find(nm);
    if (it == meta.ranges.end()) {
      it = meta.ranges.find(std::string{canon_io_name(nm)});  // unquoted slang `` `x` `` form
    }
    if (it != meta.ranges.end() && !it->second.unbounded) {
      return std::make_pair(it->second.min, it->second.max);
    }
    // A read-only input param has no derived bw_meta entry — fall back to its
    // declared envelope (io_meta), mirroring
    // uPass_bitwidth::envelope_of_operand.
    std::string_view base = canon_io_name(name);
    if (const auto pos = base.find("___ssa_"); pos != std::string_view::npos) {
      base = base.substr(0, pos);
    }
    for (const auto& in : lnast_->io_meta().inputs) {
      if (in.name != base || in.kind != Io_kind::integer) {
        continue;
      }
      if (in.has_range) {
        return std::make_pair(in.range_min,
                              in.range_max);  // exact `int(min,max)`
      }
      if (in.bits > 0 && in.bits <= 62) {
        if (in.is_signed) {
          return std::make_pair(-(int64_t{1} << (in.bits - 1)), (int64_t{1} << (in.bits - 1)) - 1);
        }
        return std::make_pair(int64_t{0}, (int64_t{1} << in.bits) - 1);
      }
      break;
    }
    return std::nullopt;
  }

  // Published value range of an operand: exact for a comptime const, else the
  // bitwidth pass' derived [min, max] (bw_meta), else — for a never-written
  // input port — its declared envelope from io_meta. nullopt = unbounded.
  [[nodiscard]] std::optional<std::pair<int64_t, int64_t>> range_of_operand(const Lnast_nid& nid) const {
    if (Lnast_ntype::is_const(lnast_->get_type(nid))) {
      auto c = Dlop::from_pyrope(lnast_->get_name(nid));
      if (c->is_just_i64()) {
        const int64_t v = c->to_just_i64();
        return std::make_pair(v, v);
      }
      return std::nullopt;
    }
    return range_of_name(lnast_->get_name(nid));
  }

  // Lower an if-branch body into a fresh write scope; rolls pin_map_ back so
  // branch-local writes don't leak. Returns the names the branch bound. The
  // rollback restores only the names this branch wrote (recorded lazily in
  // record()), so it is O(writes) -- no per-branch copy of the whole pin_map_.
  WriteMap lower_branch(const Lnast_nid& stmts) {
    branch_writes_.emplace_back();
    branch_restore_.emplace_back();
    if (Lnast_ntype::is_stmts(lnast_->get_type(stmts))) {
      lower_stmts(stmts);
    } else {
      lower_node(stmts);
    }
    auto writes = std::move(branch_writes_.back());
    branch_writes_.pop_back();
    auto restore = std::move(branch_restore_.back());
    branch_restore_.pop_back();
    for (const auto& [name, old] : restore) {
      if (old.pin.has_value()) {
        pin_map_[name] = *old.pin;
      } else {
        pin_map_.erase(name);
      }
      if (old.mw.has_value()) {
        mw_map_[name] = *old.mw;
      } else {
        mw_map_.erase(name);
      }
    }
    return writes;
  }

  // if(cond, then-stmts, [cond, stmts]*, [else-stmts]) -> per-variable binary
  // Mux chains. Mux pins: 0 = selector, 1 = false/else, 2 = true/then.
  //
  // unique_if (the `unique if` / `match` chain) declares the conditions
  // mutually exclusive, so the per-variable merge is ONE Hotmux instead: a
  // shared one-hot selector packs bit i = cond_i plus a final
  // none-of-the-conds bit (the else / fall-through slot), and values ride
  // p1..pN. The selector is one-hot by construction exactly when the
  // uniqueness assume holds; a violation makes it multi-hot, which the
  // Hotmux contract flags at runtime (cgen's case default).
  struct Branch {
    bool     is_else{false};
    Pin      cond;
    WriteMap writes;
  };

  void lower_if(const Lnast_nid& nid, bool unique = false) {
    std::vector<Branch> branches;

    auto child = lnast_->get_first_child(nid);
    if (child.is_invalid()) {
      return;
    }
    // 1a-mem — memory write enables need each branch's full path condition;
    // R1 — so do assert/assume guards (lower_cassert turns the path condition
    // into an IMPLICATION, where a memory enable uses it as a CONJUNCTION).
    // Tracking is UNCONDITIONAL: the stack holds the raw condition pins (which
    // exist anyway as mux selectors) and current_path_cond() materializes the
    // and2/not1/nonzero1 chain only when a consumer asks, so a body with neither
    // consumer mints nothing. The old `!mem_map_.empty()` gate was evaluated on
    // ENTRY, which lost the guard of any `if` enclosing a memory's declaration.
    // The MUX selector (`branches[].cond`) keeps the RAW pin — that is datapath,
    // with its own established semantics; only the path copy is reduced.
    // `match` rides this too: it lowers to a unique_if whose arms are
    // branch-lowered here before lower_unique_merge runs.
    const size_t     path_base = path_terms_.size();
    // Arm k is taken when every earlier condition is false and c_k is true, so
    // its terms are ¬c_0 … ¬c_{k-1}, c_k; the bare else drops the final c_k.
    std::vector<Pin> prior_conds;
    auto             merge_cond = [&](const Pin& raw) { return !valid_minted_ ? raw : and2(valid_pin(), nonzero1(raw)); };
    auto             lower_arm  = [&](const Lnast_nid& stmts, const Pin& cond, bool is_else) {
      for (const auto& pc : prior_conds) {
        push_path_term(pc, /*negated=*/true);
      }
      if (!is_else) {
        push_path_term(cond, /*negated=*/false);
      }
      auto w = lower_branch(stmts);
      truncate_path_terms(path_base);
      return w;
    };

    Pin first_cond = leaf(child).pin;  // child0 = condition
    child          = lnast_->get_sibling_next(child);
    if (child.is_invalid()) {
      return;
    }
    branches.push_back({false, merge_cond(first_cond), lower_arm(child, first_cond, /*is_else=*/false)});
    prior_conds.push_back(first_cond);

    child = lnast_->get_sibling_next(child);
    while (!child.is_invalid()) {
      bool last = lnast_->is_last_child(child);
      if (last && Lnast_ntype::is_stmts(lnast_->get_type(child))) {
        if (!valid_minted_) {
          branches.push_back({true, Pin{}, lower_arm(child, Pin{}, /*is_else=*/true)});
        } else {
          // Inactive means NO arm, including else. Spell the else as an
          // explicit `active & !c0 & ...` arm so its writes fall through to
          // the pre-if value while inactive and unique-if stays one-hot.
          Pin else_cond = valid_pin();
          for (const auto& pc : prior_conds) {
            else_cond = and2(else_cond, not1(nonzero1(pc)));
          }
          branches.push_back({false, else_cond, lower_arm(child, Pin{}, /*is_else=*/true)});
        }
        break;
      }
      Pin elif_cond = leaf(child).pin;
      child         = lnast_->get_sibling_next(child);
      if (child.is_invalid()) {
        break;
      }
      branches.push_back({false, merge_cond(elif_cond), lower_arm(child, elif_cond, /*is_else=*/false)});
      prior_conds.push_back(elif_cond);
      child = lnast_->get_sibling_next(child);
    }

    absl::flat_hash_set<std::string> all_vars_set;
    for (auto& br : branches) {
      for (auto& [name, _] : br.writes) {
        all_vars_set.insert(name);
      }
    }
    // Deterministic merge order: flat_hash_set iteration is randomized per
    // PROCESS, and each var below creates mux nodes -- a random order makes
    // the produced graph (node ids, emission order, every file generated from
    // it) differ run to run, defeating downstream content caches (bazel on
    // the sim C++, run_id, ...). Sort once; both merge paths share it.
    std::vector<std::string> all_vars(all_vars_set.begin(), all_vars_set.end());
    std::sort(all_vars.begin(), all_vars.end());

    const bool      has_else    = !branches.empty() && branches.back().is_else;
    const WriteMap& else_writes = has_else ? branches.back().writes : empty_writes_;

    if (unique && !all_vars.empty()) {
      lower_unique_merge(branches, all_vars, has_else, else_writes);
      return;
    }

    for (const auto& var : all_vars) {
      // `pre` is the var's value ENTERING this if (it already encodes every
      // prior statement's writes — e.g. an earlier separate `if(inr) ov<=0`).
      // A branch that does not write `var` must fall back to `pre`, NOT to the
      // else value: the else only applies on the all-conds-false path. Seeding
      // the chain's false arm (`cur`) with the else value while still using
      // `cur` as the fallback for non-writing branches (the old code) leaked
      // the else value up into the then/elif arms, clobbering `pre`. For a
      // reg's enable shadow this collapsed a conditional enable into constant
      // true (write-every-cycle); for its din it forced the else value.
      auto      base      = pin_map_.find(var);
      auto      ew        = else_writes.find(var);
      const int n         = static_cast<int>(branches.size());
      const int last_cond = has_else ? n - 2 : n - 1;
      // 2c-wire — a wire's din shadow has no pre-if value to hold and no X to
      // fall back to (see is_wire_din): the unwritten paths are don't-cares, so
      // fill them with a value the wire IS written with. `wire_seed` is the
      // lowest-priority writing arm, which becomes the chain's base — with a
      // single writing arm that leaves the driver bare, no mux at all.
      Pin       wire_fill;
      int       wire_seed = -1;
      if (base == pin_map_.end() && is_single_bind_net(var)) {
        if (ew != else_writes.end()) {
          wire_fill = ew->second;
        } else {
          for (int i = last_cond; i >= 0; --i) {
            if (auto wr = branches[i].writes.find(var); wr != branches[i].writes.end()) {
              wire_fill = wr->second;
              wire_seed = i;
              break;
            }
          }
        }
      }
      // A non-writing branch falls back to `pre`. For a reg's din shadow with
      // no recorded pre-value (a pure conditional write, no prior write and no
      // read), `pre` is the reg's q (hold) — NOT a don't-care.
      Pin pre;
      if (base != pin_map_.end()) {
        pre = base->second;
      } else if (auto hold = reg_hold_pin(var)) {
        pre = *hold;
      } else if (!wire_fill.is_invalid()) {
        pre = wire_fill;
      } else {
        pre = nil_pin();
      }
      // A wire arm that writes nothing selects nothing: skip it (and the seed
      // arm, already the chain's base) instead of muxing in a don't-care.
      auto skip_arm = [&](int i, bool writes) { return !wire_fill.is_invalid() && (i == wire_seed || !writes); };
      Pin  cur      = (ew != else_writes.end()) ? ew->second : pre;

      // The merged value's width is the widest among the branch sources;
      // mw_lookup alone holds whatever the LAST write recorded (or 1 for a
      // never-bound io output), which under-sizes the mux and truncates the
      // wider arms. Take the max over every contributing pin's stamped bits
      // (bits >= mw by construction, so this only ever widens). pin_mw_of
      // shares this with the Hotmux path (lower_unique_merge).
      // Size from VALUES, not the destination variable's declared envelope.
      // In unbounded LNAST/LGraph semantics a declaration is a boundary
      // contract, not a request to inflate every internal mux. The eventual
      // store/GraphIO/register landing performs a lossless widen; a real wide
      // pre-value or arm is already represented by its pin below.
      int32_t signed_mw   = 0;
      int32_t unsigned_mw = 0;
      // Tracked SEPARATELY from `signed_mw`: an arm that can be negative but
      // carries no width stamp leaves pin_mw_of() at 0, and deriving
      // "any signed" from `signed_mw > 0` would then stamp the merge UNSIGNED
      // -- the exact zero-fill miscompile described below.
      bool    any_signed  = false;
      auto    note_arm    = [&](const Pin& arm) {
        if (pin_can_be_negative(arm)) {
          any_signed = true;
          signed_mw  = std::max(signed_mw, pin_mw_of(arm));
        } else {
          unsigned_mw = std::max(unsigned_mw, pin_mw_of(arm));
        }
      };
      note_arm(cur);
      // A merge is only as unsigned as its ARMS. bind_result stamps UNSIGNED
      // unconditionally, which is a lie the moment one arm can go negative, so
      // every consumer that widens the merge zero-fills a value that had to
      // sign-extend. `c ? -2 : s7` came back as an unsigned net, cgen declared
      // it `reg [65:0]`, and that turned the whole enclosing Verilog expression
      // unsigned: `(-s1) + (c ? -2 : s7)` evaluated to 2 where the golden says
      // 6 (vloghammer wideexpr_00093, confirmed against iverilog). Collect the
      // sign over the same sources the width is collected over.
      // Finalize the merged width BEFORE building the chain so every mux in it
      // (not only the outermost one bind_result stamps) carries it. An if/elif
      // with >=2 conditions builds a chain of muxes; leaving the inner muxes at
      // bits==0 leaks an unbounded cell past tolg (e.g. an else-less `if/elif`
      // whose fall-through arm is a nil `pre` value --
      // assert_ifelse2.pick_max).
      for (int i = last_cond; i >= 0; --i) {
        auto wr = branches[i].writes.find(var);
        if (skip_arm(i, wr != branches[i].writes.end())) {
          continue;
        }
        if (wr != branches[i].writes.end()) {
          note_arm(wr->second);
        } else {
          // Only a NON-WRITING conditional arm can select the pre-if value.
          // When every condition and the explicit else write `var`, `pre` is
          // not connected to this mux at all and must not inflate its carrier
          // (a u64 declaration around `c ? 1 : 0` used to make Slop<66>).
          note_arm(pre);
        }
      }
      const auto mw = std::max<int32_t>(1, any_signed ? std::max(signed_mw, unsigned_mw > 0 ? unsigned_mw + 1 : 0) : unsigned_mw);
      bool       minted = false;
      for (int i = last_cond; i >= 0; --i) {
        auto& br = branches[i];
        auto  wr = br.writes.find(var);
        if (skip_arm(i, wr != br.writes.end())) {
          continue;
        }
        Pin true_val = (wr != br.writes.end()) ? wr->second : pre;

        auto mux = make_node(Ntype_op::Mux);
        mux.create_sink_pin(0).connect_driver(br.cond);   // selector
        mux.create_sink_pin(1).connect_driver(cur);       // false / else
        mux.create_sink_pin(2).connect_driver(true_val);  // true / then
        cur    = mux.create_driver_pin(0);
        minted = true;
        if (i != 0) {  // inner mux; bind_result stamps the outermost (i==0) below
          if (any_signed) {
            set_sbits(cur, mw);
          } else {
            set_ubits(cur, mw);
          }
        }
      }
      if (!minted) {
        // 2c-wire with a single writing arm: `cur` IS that arm's driver pin,
        // owned by the node that produced it. record() it (bind_result would
        // re-stamp a pin this merge did not mint).
        record(var, cur, mw);
        maybe_bind_wire_shadow(var, cur, mw);
        continue;
      }
      bind_result(var, cur, mw);
      if (any_signed) {
        set_sbits(cur, mw);
      }
      // AFTER the final stamp: bind_result stamps `cur` UNSIGNED, and a wire
      // bind copies the driver's width/sign onto the passthrough output, so
      // binding first left a signed driver behind an unsigned buffer.
      maybe_bind_wire_shadow(var, cur, mw);
    }
  }

  // unique_if merge: one Hotmux per variable over a shared one-hot selector.
  // Selector bit i (i < n_conds) is branches[i].cond; the top bit is
  // "none of the conds" — the else / fall-through slot. Hotmux pins:
  // 0 = one-hot selector, p(i+1) = arm i's value, p(n_conds+1) = else value
  // (the variable's pre-if value when the arm / else doesn't write it).
  void lower_unique_merge(const std::vector<Branch>& branches, const std::vector<std::string>& all_vars, bool has_else,
                          const WriteMap& else_writes) {
    const int n_conds = static_cast<int>(branches.size()) - (has_else ? 1 : 0);
    I(n_conds >= 1);

    // none = (OR of all conds) == 0: exactly one of {cond_0..cond_k, none}
    // is set when the uniqueness assume holds. EQ (not Not) on purpose: a
    // bitwise Not of a 1-bit bool carries infinite high bits (LSB-only by
    // convention, safe under And but NOT under the SHL/Or packing below).
    Pin or_all = branches[0].cond;
    if (n_conds > 1) {
      auto or_node = make_node(Ntype_op::Or);
      for (int i = 0; i < n_conds; ++i) {
        or_node.create_sink_pin(0).connect_driver(branches[i].cond);
      }
      or_all = or_node.create_driver_pin(0);
      set_ubits(or_all, 1);
    }
    auto none_node = make_node(Ntype_op::EQ);
    none_node.create_sink_pin(0).connect_driver(or_all);
    none_node.create_sink_pin(0).connect_driver(create_const(*g_, *Dlop::create_integer(0)));
    const Pin none = none_node.create_driver_pin(0);
    set_ubits(none, 1);

    auto sel_node = make_node(Ntype_op::Or);
    for (int i = 0; i < n_conds; ++i) {
      sel_node.create_sink_pin(0).connect_driver(shl1_by(branches[i].cond, i));
    }
    sel_node.create_sink_pin(0).connect_driver(shl1_by(none, n_conds));
    auto sel = sel_node.create_driver_pin(0);
    // n_conds+1 one-hot positions require exactly n_conds+1 unsigned bits.
    set_ubits(sel, n_conds + 1);

    for (const auto& var : all_vars) {
      auto       base    = pin_map_.find(var);
      bool       has_pre = base != pin_map_.end();
      // A reg's din shadow with no recorded pre-value still HOLDS on an
      // unwritten / none-of arm: fall back to the reg's q (current value), not
      // a don't-care. Treat that q as a real pre-value so the none-of slot
      // below drives the hold instead of `Dlop::unknown`. A non-reg var
      // (combinational match-expression result) keeps has_pre=false →
      // don't-care none-of slot.
      auto       ew      = else_writes.find(var);
      const bool has_ev  = ew != else_writes.end();
      // 2c-wire — a wire's din shadow has no hold and no X fallback (see
      // is_wire_din): every unwritten arm and the none-of slot are don't-cares,
      // so fill them with a value the wire IS written with instead of an
      // unknown. When that is the ONLY value written, the wire's driver needs no
      // Hotmux at all.
      Pin        wire_fill;
      if (!has_pre && is_single_bind_net(var)) {
        if (has_ev) {
          wire_fill = ew->second;
        } else {
          for (int i = n_conds - 1; i >= 0; --i) {
            if (auto wr = branches[i].writes.find(var); wr != branches[i].writes.end()) {
              wire_fill = wr->second;
              break;
            }
          }
        }
        if (!wire_fill.is_invalid()) {
          bool uniform = true;
          for (const auto& br : branches) {
            if (auto wr = br.writes.find(var); wr != br.writes.end() && !(wr->second == wire_fill)) {
              uniform = false;
              break;
            }
          }
          if (uniform) {  // one distinct driver over all arms: that IS the net
            record(var, wire_fill, std::max<int32_t>(1, pin_mw_of(wire_fill)));
            continue;
          }
        }
      }
      Pin pre;
      if (has_pre) {
        pre = base->second;
      } else if (auto hold = reg_hold_pin(var)) {
        pre     = *hold;
        has_pre = true;
      } else if (!wire_fill.is_invalid()) {
        pre     = wire_fill;
        has_pre = true;
      } else {
        pre = nil_pin();
      }
      Pin else_val = has_ev ? ew->second : pre;

      // The Hotmux result width is the WIDEST among its REAL arm sources,
      // exactly as the Mux chain above sizes itself. mw_lookup alone holds
      // whatever the LAST recorded write left (or 1 for a const arm like a
      // `match … else {0}` slot), which under-sizes the result and truncates
      // the wider arms (the match-expression `o = match s {…else{0}}`
      // 1-bit-output miscompile).
      //
      // A SYNTHETIC nil fallback must never count toward the width. When a
      // `match` omits its `else` and the result has no pre-match value (a fresh
      // match-expression result tmp), the none-of slot is a pure don't-care;
      // but nil_pin() is 64 bits wide, so counting it would balloon the whole
      // Hotmux to 65 bits and truncate the real arms back down. Size from real
      // sources only, then drive the none-of slot with a width-correct
      // don't-care.
      // As in the ordinary Mux path, derive the Hotmux carrier from its real
      // values. A wide declaration around narrow arms is not an arithmetic
      // operand and must not widen the internal selection tree.
      int32_t signed_mw   = 0;
      int32_t unsigned_mw = 0;
      // Tracked SEPARATELY from `signed_mw`: an arm that can be negative but
      // carries no width stamp leaves pin_mw_of() at 0, and deriving
      // "any signed" from `signed_mw > 0` would then stamp the merge UNSIGNED
      // -- the exact zero-fill miscompile described below.
      bool    any_signed  = false;
      auto    note_arm    = [&](const Pin& arm) {
        if (pin_can_be_negative(arm)) {
          any_signed = true;
          signed_mw  = std::max(signed_mw, pin_mw_of(arm));
        } else {
          unsigned_mw = std::max(unsigned_mw, pin_mw_of(arm));
        }
      };
      if (has_ev) {
        note_arm(else_val);
      } else if (has_pre) {
        // No explicit else: none-of selects the pre-if value.
        note_arm(pre);
      }

      // Collect first, connect second: a non-writing arm of a fresh match
      // result has no pre-value.  Its placeholder is a don't-care and must be
      // minted only AFTER the real arms establish `mw`; using nil_pin() here
      // leaked the typeless signed unknown into cgen, where deterministic-X=0
      // materialized it as a 65-bit negative sentinel in an otherwise 1-bit
      // Hotmux.
      std::vector<Pin> arm_values;
      arm_values.reserve(n_conds);
      for (int i = 0; i < n_conds; ++i) {
        auto wr  = branches[i].writes.find(var);
        // A non-writing arm keeps the pre-match value; only real writes size.
        Pin  val = wr != branches[i].writes.end() ? wr->second : (has_pre ? pre : Pin{});
        if (wr != branches[i].writes.end()) {
          note_arm(val);
        } else if (has_pre) {
          // This condition does not write `var`, so its real value arm is the
          // pre-if value even when a separate explicit else exists.
          note_arm(pre);
        }
        arm_values.push_back(val);
      }

      const auto mw = std::max<int32_t>(1, any_signed ? std::max(signed_mw, unsigned_mw > 0 ? unsigned_mw + 1 : 0) : unsigned_mw);

      auto hot = make_node(Ntype_op::Hotmux);
      hot.create_sink_pin(0).connect_driver(sel);
      for (int i = 0; i < n_conds; ++i) {
        const Pin val = arm_values[i].is_invalid() ? create_const(*g_, *Dlop::unknown(mw)) : arm_values[i];
        hot.create_sink_pin(static_cast<hhds::Port_id>(i + 1)).connect_driver(val);
      }
      // none-of slot: explicit else / pre value when present; otherwise an
      // exhaustive else-less match — drive the unreachable slot with a
      // width-matched don't-care (`mw`-bit 0sb?) so it adds no width pressure.
      const Pin none_val = (has_ev || has_pre) ? else_val : create_const(*g_, *Dlop::unknown(mw));
      hot.create_sink_pin(static_cast<hhds::Port_id>(n_conds + 1)).connect_driver(none_val);
      auto hot_out = hot.create_driver_pin(0);
      bind_result(var, hot_out, mw);
      if (any_signed) {
        set_sbits(hot_out, mw);
      }
      maybe_bind_wire_shadow(var, hot_out, mw);  // AFTER the final stamp (see lower_if_merge)
    }
  }

  std::shared_ptr<Lnast>      lnast_;
  hhds::Graph*                g_;
  const uPass_tolg::Registry* registry_ = nullptr;
  hhds::GraphLibrary*         lib_      = nullptr;

  absl::flat_hash_map<std::string, Pin>                             pin_map_;
  absl::flat_hash_map<std::string, int32_t>                         mw_map_;
  // The last driver written to each LOGICAL variable (SSA versions x /
  // x___ssa_1 / … collapsed to "x"): the value after ALL in-cycle writes, used
  // for a derived `reset_pin = <signal>` / `clock_pin = <signal>` resolution
  // and for a `wire`'s buffer pin.
  absl::flat_hash_map<std::string, std::pair<Pin, int32_t>>         logical_last_;
  // A field read whose source is a Sub result created by a call lowered LATER
  // in the body. Deferred to end-of-pass, then re-resolved with tget_final_ so
  // a still-unresolved one warns instead of looping.
  std::vector<Lnast_nid>                                            pending_tgets_;
  bool                                                              tget_final_ = false;
  absl::flat_hash_map<std::string, std::pair<int64_t, int64_t>>     range_map_;
  // A `range` whose endpoints are NOT comptime constants (`a#[n..=m]` with
  // runtime n/m). Keyed by the range tmp name; carries the lo/hi LNAST nids so
  // lower_get_mask can build the shift+mask select. An open `lo..` form stores
  // a const "nil" hi nid. (Comptime ranges stay in range_map_ as folded ints.)
  absl::flat_hash_map<std::string, std::pair<Lnast_nid, Lnast_nid>> range_dyn_map_;
  // An open-ended `lo..` range with a COMPTIME lo (`a#[3..]`): only the lo nid
  // is stashed (keyed by the range tmp name). The upper bound is the sliced
  // value's MSB, known only at the consuming get_mask/set_mask, which closes
  // the range to `lo..=(value bits-1)`. (A runtime-lo open range lives in
  // range_dyn_map_ with a const "nil" hi.)
  absl::flat_hash_map<std::string, Lnast_nid>                       range_open_map_;
  std::vector<WriteMap>                                             branch_writes_;
  struct Branch_restore {
    std::optional<Pin>     pin;
    std::optional<int32_t> mw;
  };
  // Parallel to branch_writes_: per active branch, the pre-branch value of each
  // name it wrote (nullopt = absent before the branch). Both the driver and its
  // width are transactional: restoring only pin_map_ lets a narrow then-arm
  // poison the width used while lowering the else-arm. lower_branch replays
  // this to roll both maps back, avoiding full per-branch copies.
  std::vector<absl::flat_hash_map<std::string, Branch_restore>> branch_restore_;
  WriteMap                                                      empty_writes_;

  // 2c-wire — per-wire lowering state recorded at the declare. Binding connects
  // the buffer input to the accumulated driver and restamps untyped outputs;
  // finalize_wires() handles only still-unbound/undriven declarations.
  struct Wire_info {
    hhds::Node_class              buf;             // the passthrough Or (cgen `out = a`)
    hhds::Node_class              narrow;          // typed-wire Get_mask of the CURRENT bind (dropped on rebind)
    Pin                           out;             // the buffer output (what reads bind to)
    Lnast_nid                     decl_nid;        // diag anchor (the `wire x` site)
    int32_t                       decl_color = 0;  // block region at the declare (2opt-freq B)
    int32_t                       decl_mw    = 0;  // declared width; 0 = untyped (restamp from driver)
    bool                          is_signed  = false;
    bool                          bound      = false;
    Pin                           bound_din;      // driver of the LATEST bind (what finalize splits against)
    std::vector<hhds::Node_class> early_readers;  // consumers present at any bind
  };
  absl::flat_hash_set<std::string>            wire_names_;  // gates lower_store
  std::vector<std::string>                    wire_order_;  // declaration order
  absl::flat_hash_map<std::string, Wire_info> wire_info_;

  // Reg lowering state. reg_map_ holds each declared reg's Flop
  // node (reads resolve to its q via pin_map_; stores rebind the shadow
  // din/enable keys). clock_*/reset_* lazily bind the clock/reset graph
  // inputs. reg_info_/reg_order_ carry the finalize metadata for
  // PLAIN regs (stage regs live only in reg_map_/flop_depth_).
  absl::flat_hash_map<std::string, hhds::Node_class>  reg_map_;
  absl::flat_hash_map<std::string, Reg_info>          reg_info_;
  std::vector<std::string>                            reg_order_;
  // Scalar `mut`/`const` declares (NOT reg/latch/array). A `mut b:uN = nil`
  // emits no init store, so its name never gets a driver — but using it as a
  // `b#[lo..=hi] = …` bit-assembly base is legal (the covered bits are
  // overwritten). lower_set_mask substitutes a 0sb? base for such a name; the
  // set guards that only a DECLARED scalar gets the treatment (a genuine typo
  // still errors). `= 0` never hits this — its base already folds to const 0.
  absl::flat_hash_set<std::string>                    scalar_decl_;
  // Scalar names declared `const` (see is_unbound_const).
  absl::flat_hash_set<std::string>                    const_decl_;
  // Declared TYPE width per LOGICAL name (canonical, SSA suffix stripped), for
  // the one op whose semantics depend on the DECLARED width rather than on
  // whatever value currently drives the name: Concat.
  //
  // Deliberately NOT mw_map_. That map tracks the live value's magnitude width,
  // so `var a:u4 = 3` leaves 2 there — and a `concat(a, b)` lane must still be
  // 4 bits wide, because narrowing it would shift every lane above it. Filled
  // from io_meta and from every `declare` that carries a type child; a nested
  // `concat`'s own result registers here too (its width is the lane sum, by
  // construction).
  absl::flat_hash_map<std::string, Decl_type>         decl_type_;
  // Names whose value is a `concat` result, with the lane sum. Read only by
  // check_concat_dest_width: the concat node's own dst is a compiler temp, so
  // the user-facing `c:u12 = concat(...)` width check has to happen where that
  // temp is bound to a declared name.
  absl::flat_hash_map<std::string, int32_t>           concat_result_mw_;
  // Declared memories (array-typed regs + mut/const arrays), the
  // branch-path stack lower_if maintains for their write enables, the
  // recorded tuple literals (array initializers / __memory configs), and the
  // bound __memory results.
  absl::flat_hash_map<std::string, Mem_info>          mem_map_;
  absl::flat_hash_map<std::string, Array_scalar_view> array_scalar_views_;
  absl::flat_hash_set<std::string>                    comptime_array_names_;
  absl::flat_hash_map<int32_t, int>                   mem_write_site_counts_;
  std::vector<std::string>                            mem_order_;
  // Path-condition stack: one entry per enclosing branch arm, UNMATERIALIZED
  // (see current_path_cond). `path_folded_[i]` caches the fold of terms[0..i]
  // once some consumer asks for it; an invalid entry is "not built yet".
  struct Path_term {
    Pin  cond;
    bool negated = false;
  };
  std::vector<Path_term>                                                          path_terms_;
  std::vector<Pin>                                                                path_folded_;
  absl::flat_hash_map<std::string, Tuple_rec>                                     tuple_recs_;
  absl::flat_hash_map<std::string, Mem_result>                                    mem_results_;
  // attr_set seen before its target's declare (memory fwd overrides etc).
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, std::string>> pending_attrs_;
  std::string                                                                     clock_name_;
  bool                                                                            clock_minted_ = false;
  Pin                                                                             clock_pin_;
  bool                                                                            clock_pin_valid_ = false;
  std::string                                                                     reset_name_;
  bool                                                                            reset_minted_        = false;
  bool                                                                            reset_neg_           = false;
  bool                                                                            reset_async_default_ = false;
  Pin                                                                             reset_pin_;
  bool                                                                            reset_pin_valid_ = false;
  std::string                                                                     valid_name_;
  bool                                                                            valid_minted_ = false;
  bool                                                                            valid_active_ = false;
  Pin                                                                             valid_pin_;
  bool                                                                            valid_pin_valid_ = false;
  Pin                                                                             en_true_pin_;
  Pin                                                                             en_false_pin_;
  bool                                                                            en_true_valid_  = false;
  bool                                                                            en_false_valid_ = false;

  absl::flat_hash_map<std::string, Pending_stage> pending_stage_;
  absl::flat_hash_map<std::string, Sub_out>       sub_out_stages_;
  // Multi-output instance results: fcall dst name -> (Sub node, callee
  // outputs); consumed by tuple_get field reads.
  struct Sub_result {
    hhds::Node_class            sub;
    std::vector<Lnast_io_entry> outputs;
  };
  absl::flat_hash_map<std::string, Sub_result> sub_results_;
  // Explicit rolled_for lowering hand-off. The index port is allowed to be
  // absent from the hidden ordinary call; lower_rolled_for then attaches the
  // descriptor to exactly the Sub created by that payload.
  std::string                                  rolled_index_port_;
  hhds::Node_class                             last_lowered_sub_;

  // Checker inputs gathered while building: pending records,
  // per-Flop effective crossing depth, per-Sub pinned latency interval.
  // Also adds plain_reg_flops_ (state/stage classification candidates) and
  // inserted_flops_ (the LN-inserted pipe output flops — narrowing targets).
  std::vector<Pending_rec>                                   pending_checks_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> flop_depth_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> sub_time_;
  absl::flat_hash_map<uint64_t, std::string>                 plain_reg_flops_;
  absl::flat_hash_set<uint64_t>                              inserted_flops_;
  // 2c-wire — Verilog-origin comb-cycle wire buffers (a net that may legally
  // close a same-cycle ring through a submodule instance; a later lgraph pass
  // detects/handles real ones). The time-checker cuts these nodes' in-edges
  // instead of flagging the loop, preserving the pre-2c-wire leniency. PYROPE
  // wire buffers are NEVER added here, so a real comb loop through a Pyrope
  // wire is flagged as an error.
  absl::flat_hash_set<uint64_t>                              wire_cut_nids_;

public:
  // Lower the partition's declared per-output intervals as
  // pending checks on the GraphIO output sinks (mod: the @[N] landing cycle;
  // pipe: the declared range — comb bodies must land at exactly that range,
  // sigma>0 narrowing lights up here). `@[]`-opted-out outputs (nil) are
  // skipped. Called at the end of build().
  void stamp_output_pendings() {
    const auto kind = lnast_->get_lambda_kind();
    if (kind != "pipe" && kind != "mod") {
      return;
    }
    auto      root = lnast_->get_root();
    Lnast_nid io_nid;
    for (auto c = lnast_->get_first_child(root); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (Lnast_ntype::is_io(lnast_->get_type(c))) {
        io_nid = c;
        break;
      }
    }
    if (io_nid.is_invalid()) {
      return;
    }
    auto in_tup = lnast_->get_first_child(io_nid);
    if (in_tup.is_invalid()) {
      return;
    }
    auto out_tup = lnast_->get_sibling_next(in_tup);
    if (out_tup.is_invalid()) {
      return;
    }
    for (auto st = lnast_->get_first_child(out_tup); !st.is_invalid(); st = lnast_->get_sibling_next(st)) {
      if (!Lnast_ntype::is_store(lnast_->get_type(st))) {
        continue;
      }
      auto name_nid = lnast_->get_first_child(st);
      if (name_nid.is_invalid()) {
        continue;
      }
      Lnast_nid stages_nid;
      for (auto c = lnast_->get_sibling_next(name_nid); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
        if (Lnast_ntype::is_stages(lnast_->get_type(c))) {
          stages_nid = c;
          break;
        }
      }
      if (stages_nid.is_invalid()) {
        continue;
      }
      auto mn = lnast_->get_first_child(stages_nid);
      if (mn.is_invalid()) {
        continue;
      }
      auto mx = lnast_->get_sibling_next(mn);
      if (lnast_->get_name(mn) == "nil" || (!mx.is_invalid() && lnast_->get_name(mx) == "nil")) {
        continue;  // @[] opt-out — unconstrained
      }
      int64_t a_min = const_val(mn);
      int64_t a_max = mx.is_invalid() ? a_min : const_val(mx);
      if (a_max < a_min) {
        a_max = a_min;  // bare-pipe (1,0) sentinel realizes at min
      }
      const std::string name(lnast_->get_name(name_nid));
      auto              sink = g_->get_output_pin(name);
      if (sink.is_invalid()) {
        continue;
      }
      sink.attr(livehd::attrs::pending_time).set({a_min, a_max});
      pending_checks_.push_back({sink, name, a_min, a_max, /*is_sink=*/true});
    }
  }

  [[nodiscard]] std::vector<Pending_rec>&& take_pending_checks() { return std::move(pending_checks_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& take_flop_depths() { return std::move(flop_depth_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& take_sub_times() { return std::move(sub_time_); }
  [[nodiscard]] absl::flat_hash_map<uint64_t, std::string>&& take_plain_reg_flops() { return std::move(plain_reg_flops_); }
  [[nodiscard]] absl::flat_hash_set<uint64_t>&&              take_inserted_flops() { return std::move(inserted_flops_); }
  [[nodiscard]] absl::flat_hash_set<uint64_t>&&              take_wire_cuts() { return std::move(wire_cut_nids_); }
};

// The combined pipe/mod LG time checker (written once for both kinds).
// Runs at the tolg seam on the just-built
// graph:
//   1. Tarjan SCC over the node digraph (flop din->q edges included, i.e.
//      node-level cycles). A non-trivial SCC must contain a STATE-eligible
//      flop (a plain reg); an SCC with only stage/inserted flops is
//      a cross-stage register feedback error, one with no flop at all is the
//      classic combinational-loop error. A plain reg is also STATE when its
//      `enable` is driven (conditional write = enable-encoded hold feedback,
//      invisible to SCC); every other flop is a STAGE crossing.
//   2. Forward (min,max) interval propagation, twice: graph inputs (0,0);
//      constants unify with anything; comb cells take the equal-meet of
//      their operands (a mismatch is the 06c misalignment error); a stage
//      Flop adds its effective crossing depth; a Sub adds its pinned
//      instance interval (clock sinks excluded). Pass 1 treats every state
//      flop's q as unconstrained; then sigma(q) := sigma(din) (state regs
//      pin to their HOME stage — no crossing) and pass 2 re-checks every
//      meet with the pinned values.
//   3. Narrow each LN-inserted pipe output flop to the body deficit
//      (min−sigma, max−sigma); (0,0) realizes as a wire (the flop is
//      bypassed and deleted). sigma > min is the latency-exceeded error.
//   4. Discharge every pending record: computed == asserted -> REMOVE the
//      pending_time attr; mismatch (or an undischargeable record) -> error.
//      A declared state output (`-> (reg q@[N])`) pins q and din to the same
//      home stage, so its interface cycle discharges directly against home.
class Time_checker {
public:
  struct TR {
    int64_t min = 0;
    int64_t max = 0;
    bool    any = false;  // constants — unify with any cycle
  };

  Time_checker(hhds::Graph* g, const std::shared_ptr<Lnast>& ln, std::vector<Pending_rec>&& pendings,
               absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& flop_depth,
               absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>>&& sub_time,
               absl::flat_hash_map<uint64_t, std::string>&& plain_regs, absl::flat_hash_set<uint64_t>&& inserted,
               absl::flat_hash_set<uint64_t>&& wire_cuts)
      : g_(g)
      , ln_(ln)
      , pendings_(std::move(pendings))
      , flop_depth_(std::move(flop_depth))
      , sub_time_(std::move(sub_time))
      , plain_regs_(std::move(plain_regs))
      , inserted_(std::move(inserted))
      , wire_cuts_(std::move(wire_cuts)) {
    for (const auto& [nid, name] : plain_regs_) {
      reg_flop_by_name_.emplace(name, nid);
    }
    // Regs with an explicit declared cycle (`reg q@[N]` output, or an `@[N]`
    // assertion) are LEGITIMATELY feedforward — their landing cycle is part of
    // the contract. They must NOT be force-classified as cycle-0 state (the mod
    // default below), or a `q@[1]` delay reg would collapse to a pass-through.
    // Collect them.
    for (const auto& rec : pendings_) {
      // Map the declared-cycle record to its flop. A `@[N]` assertion pins a
      // value pin (its master node); a `reg q@[N]` output pin is DRIVEN by the
      // reg flop (the output port name need not equal the internal reg name, so
      // match by the driver node, not the name). Mark the flop iff it is a
      // plain reg.
      auto consider = [&](const hhds::Node_class& mn) {
        if (!mn.is_invalid() && plain_regs_.contains(mn.get_debug_nid())) {
          decl_cycle_regs_.insert(mn.get_debug_nid());
        }
      };
      if (rec.is_sink) {
        if (auto e = rec.pin.inp_edges(); !e.empty()) {
          consider(e.front().driver.get_master_node());
        }
      } else if (!rec.pin.is_invalid()) {
        consider(rec.pin.get_master_node());
      }
      if (auto it = reg_flop_by_name_.find(rec.name); it != reg_flop_by_name_.end()) {
        decl_cycle_regs_.insert(it->second);  // name match too (defensive)
      }
    }
  }

  // Stage a Diagnostic located via the graph node's srcid (stamped
  // by the lowering walk, resolved through the graph's locator) before the
  // Pass::error-style throw. An id-less or invalid node degrades to an
  // unlocated record.
  template <typename... Args>
  [[noreturn]] void error_at_node(const hhds::Node_class& node, livehd::diag::Id id, std::format_string<Args...> fmt,
                                  Args&&... args) {
    auto                            msg = std::format(fmt, std::forward<Args>(args)...);
    livehd::diag::Span              span;
    std::vector<livehd::diag::Note> notes;
    if (g_ != nullptr && !node.is_invalid()) {
      if (auto ref = node.attr(hhds::attrs::srcid); ref.has()) {
        const auto rs = g_->source_locator().resolve_spans(ref.get());
        span          = rs.primary;
        notes         = livehd::diag::notes_from(rs, "reached via this site");
      }
    }
    livehd::diag::sink().stage(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = std::string(id.code),
        .category = std::string(id.category),
        .pass     = "upass.tolg",
        .message  = msg,
        .span     = std::move(span),
        .notes    = std::move(notes),
    });
    throw Eprp::parser_error(Pass::eprp, msg);
  }

  template <typename... Args>
  [[noreturn]] void error_at_node(const hhds::Node_class& node, std::format_string<Args...> fmt, Args&&... args) {
    error_at_node(node, livehd::diag::Id{"tolg-time-error", "time"}, "{}", std::format(fmt, std::forward<Args>(args)...));
  }

  // The pending record's value driver, as the diag anchor (invalid when
  // undriven -- error_at_node degrades to an unlocated record).
  [[nodiscard]] static hhds::Node_class pending_anchor(const auto& rec) {
    if (rec.is_sink) {
      auto edges = rec.pin.inp_edges();
      return edges.empty() ? hhds::Node_class{} : edges.front().driver.get_master_node();
    }
    return rec.pin.get_master_node();
  }

  void run() {
    using livehd::graph_util::is_graph_input_pin;
    using livehd::graph_util::is_type_const;
    using livehd::graph_util::is_type_flop;
    using livehd::graph_util::type_op_of;

    // 1. Collect nodes + node-level digraph (consts/graph-inputs excluded).
    std::vector<hhds::Node_class>         nodes;
    absl::flat_hash_map<uint64_t, size_t> idx;
    for (auto n : g_->body().nodes()) {
      if (is_type_const(n)) {
        continue;
      }
      idx.emplace(n.get_debug_nid(), nodes.size());
      nodes.push_back(n);
    }
    const size_t                  nn = nodes.size();
    std::vector<std::vector<int>> succ(nn);
    std::vector<std::vector<int>> pred(nn);
    auto                          node_idx_of_pin = [&](const hhds::Pin_class& dpin) -> int {
      if (dpin.is_invalid() || is_graph_input_pin(dpin)) {
        return -1;
      }
      auto mn = dpin.get_master_node();
      if (mn.is_invalid() || is_type_const(mn) || type_op_of(mn) == Ntype_op::Nconst) {
        return -1;
      }
      auto it = idx.find(mn.get_debug_nid());
      return it == idx.end() ? -1 : static_cast<int>(it->second);
    };
    for (size_t i = 0; i < nn; ++i) {
      // 2c-wire — a Verilog-origin comb-cycle wire buffer: cut its in-edge for
      // loop detection (like a state flop's q). Such a net may legally close a
      // same-cycle ring through a submodule; a later lgraph pass handles real
      // comb loops. Pyrope wire buffers are never in this set, so a real comb
      // loop through a Pyrope wire is flagged below.
      if (wire_cuts_.contains(nodes[i].get_debug_nid())) {
        continue;
      }
      for (const auto& e : nodes[i].inp_edges()) {
        // A compact loop carry is a literal Sub self-edge for edge/binding
        // visibility, but HHDS orders the group as if unrolled. Mirror that
        // dependency rule in this domain-specific SCC classifier.
        if (nodes[i].is_loop_subnode() && e.driver.get_master_node() == nodes[i]) {
          continue;
        }
        const int p = node_idx_of_pin(e.driver);
        if (p >= 0) {
          pred[i].push_back(p);
          succ[static_cast<size_t>(p)].push_back(static_cast<int>(i));
        }
      }
    }

    // 2. Tarjan SCC (iterative). Non-trivial SCCs classify their flops.
    std::vector<int> scc_id(nn, -1);
    {
      std::vector<int>    low(nn, -1), num(nn, -1);
      std::vector<bool>   on_stack(nn, false);
      std::vector<int>    stk;
      int                 counter  = 0;
      int                 next_scc = 0;
      std::vector<size_t> scc_size;
      for (size_t root = 0; root < nn; ++root) {
        if (num[root] >= 0) {
          continue;
        }
        // explicit DFS: (node, next-successor-cursor)
        std::vector<std::pair<int, size_t>> dfs;
        dfs.emplace_back(static_cast<int>(root), 0);
        num[root] = low[root] = counter++;
        stk.push_back(static_cast<int>(root));
        on_stack[root] = true;
        while (!dfs.empty()) {
          auto& [v, cur] = dfs.back();
          if (cur < succ[static_cast<size_t>(v)].size()) {
            const int w = succ[static_cast<size_t>(v)][cur++];
            if (num[w] < 0) {
              num[w] = low[w] = counter++;
              stk.push_back(w);
              on_stack[w] = true;
              dfs.emplace_back(w, 0);
            } else if (on_stack[w]) {
              low[v] = std::min(low[v], num[w]);
            }
            continue;
          }
          if (low[v] == num[v]) {
            size_t members = 0;
            int    w;
            do {
              w = stk.back();
              stk.pop_back();
              on_stack[w] = false;
              scc_id[w]   = next_scc;
              ++members;
            } while (w != v);
            scc_size.push_back(members);
            ++next_scc;
          }
          const int done = v;
          dfs.pop_back();
          if (!dfs.empty()) {
            low[dfs.back().first] = std::min(low[dfs.back().first], low[done]);
          }
        }
      }
      // self-loops count as non-trivial too
      std::vector<bool>             nontrivial(static_cast<size_t>(next_scc), false);
      // Bucket each node by its SCC id in this same O(nn) pass (ascending i, so
      // members[s][0] is the lowest-index member == the diag-anchor rep). The
      // offending-SCC scan below then iterates only the members of each
      // nontrivial SCC instead of re-scanning all nn nodes per SCC (was
      // O(nontrivial_scc * nn); XSCore has many register-feedback rings).
      std::vector<std::vector<int>> scc_members(static_cast<size_t>(next_scc));
      for (size_t i = 0; i < nn; ++i) {
        scc_members[static_cast<size_t>(scc_id[i])].push_back(static_cast<int>(i));
        if (scc_size[static_cast<size_t>(scc_id[i])] > 1) {
          nontrivial[static_cast<size_t>(scc_id[i])] = true;
        }
        for (int s : succ[i]) {
          if (s == static_cast<int>(i)) {
            nontrivial[static_cast<size_t>(scc_id[i])] = true;
          }
        }
      }
      // Classify: every non-trivial SCC needs a state-eligible flop. A plain
      // reg with neither an enable nor a feedback SCC is otherwise a pyrope
      // FEEDFORWARD (`@[stage]`) flop: σ(q)=σ(din)+1. But a VERILOG `always_ff`
      // reg is always a 1-cycle STATE element (q reads at the current cycle,
      // σ=0) — never a feedforward pipeline stage. Treating it as feedforward
      // gave a spurious stage-1 that tripped the "mixes values at different
      // cycles" check when the reg's output was combined with a stage-0 value
      // (e.g. a concat field). For a Verilog-origin module, all plain regs are
      // state.
      //
      // The SAME holds for a pyrope `mod`: a `mod` is Mealy/Moore, so its plain
      // `reg`s are cycle-0 STATE and its only structural latency is from
      // explicit `stage[N]` decls (NOT plain_regs_, untouched here). Only a
      // `pipe` infers a feedforward stage from a plain reg written purely from
      // inputs/earlier regs. Without this, an unconditionally-written `mod`
      // pipeline register (a flush-or-capture stage reg with no hold path) was
      // mis-classified as a +1-cycle stage, forcing a spurious runtime enable
      // that diverged from the Verilog it mirrors (issues.txt A3).
      const bool verilog_origin = ln_->is_verilog_origin();
      const bool mod_default    = ln_->get_lambda_kind() == "mod";
      const auto en_pid         = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, "enable"));
      for (size_t i = 0; i < nn; ++i) {
        if (!is_type_flop(nodes[i])) {
          continue;
        }
        const auto nid      = nodes[i].get_debug_nid();
        const bool eligible = plain_regs_.contains(nid);
        if (eligible) {
          bool en_driven = false;
          for (const auto& e : nodes[i].inp_edges()) {
            if (!e.sink.is_invalid() && static_cast<uint64_t>(e.sink.get_port_id()) == en_pid && !e.driver.is_invalid()) {
              en_driven = true;
              break;
            }
          }
          // A mod's plain regs default to cycle-0 state — UNLESS the reg
          // carries an explicit @[N]/interface cycle (then it is a declared
          // feedforward stage).
          const bool state_default = verilog_origin || (mod_default && !decl_cycle_regs_.contains(nid));
          if (state_default || en_driven || nontrivial[static_cast<size_t>(scc_id[i])]) {
            state_.insert(nid);
          }
        }
      }
      // Memory nodes are sequential state as well: a read dout reflects STORED
      // state (decoupled from this cycle's write din for the acyclicity check),
      // so a reg-array pipeline like `pfOp[1] <= pfOp[0]` — whose write din is
      // a read of the same memory node — is NOT a combinational loop.  Cut them
      // like flops so the node-level dout->din self-edge does not
      // false-positive.  A Latch holds stored state too: its din hold arm
      // (`din = cond ? d : q`) reads its own q by construction — the standard
      // inferred-latch idiom (prim_clk_gate's ICG), not a loop.
      for (size_t i = 0; i < nn; ++i) {
        const auto op = type_op_of(nodes[i]);
        if (op == Ntype_op::Memory || op == Ntype_op::Latch) {
          state_.insert(nodes[i].get_debug_nid());
        }
      }
      for (int s = 0; s < next_scc; ++s) {
        if (!nontrivial[static_cast<size_t>(s)]) {
          continue;
        }
        bool             has_state = false;
        bool             has_flop  = false;
        hhds::Node_class rep;  // a member of the offending SCC, for the diag anchor
        for (const int mi : scc_members[static_cast<size_t>(s)]) {
          const size_t i = static_cast<size_t>(mi);
          if (rep.is_invalid()) {
            rep = nodes[i];
          }
          if (is_type_flop(nodes[i])) {
            has_flop = true;
            if (state_.contains(nodes[i].get_debug_nid())) {
              has_state = true;
            }
          } else if (type_op_of(nodes[i]) == Ntype_op::Memory || type_op_of(nodes[i]) == Ntype_op::Latch) {
            // A memory's stored state breaks the cycle (its read dout reflects
            // the flopped contents, decoupled from this cycle's write din) — a
            // reg-array pipeline `pfOp[1] <= pfOp[0]` is sequential, not a
            // loop. A latch's q likewise holds state; its hold-arm q read is
            // the inferred-latch idiom, not a loop.
            has_state = true;
          }
        }
        if (!has_state) {
          if (has_flop) {
            error_at_node(rep,
                          "upass.tolg: '{}' has register feedback through "
                          "stage registers — make the looping register a plain "
                          "`reg` (state)",
                          ln_->get_top_module_name());
          } else {
            error_at_node(rep, "upass.tolg: combinational loop in '{}'", ln_->get_top_module_name());
          }
        }
      }
    }

    // 3. Kahn topo with state-flop in-edges cut (their q is pinned later,
    // not derived from din during the forward pass). Leftover cycle =
    // a comb loop that the state cut did not break.
    std::vector<int> indeg(nn, 0);
    for (size_t i = 0; i < nn; ++i) {
      if (state_.contains(nodes[i].get_debug_nid())) {
        continue;  // source: q decoupled from din
      }
      indeg[i] = static_cast<int>(pred[i].size());
    }
    std::vector<size_t> queue;
    std::vector<size_t> order;
    order.reserve(nn);
    for (size_t i = 0; i < nn; ++i) {
      if (indeg[i] == 0) {
        queue.push_back(i);
      }
    }
    while (!queue.empty()) {
      const size_t i = queue.back();
      queue.pop_back();
      order.push_back(i);
      for (int s : succ[i]) {
        if (state_.contains(nodes[static_cast<size_t>(s)].get_debug_nid())) {
          continue;
        }
        if (--indeg[static_cast<size_t>(s)] == 0) {
          queue.push_back(static_cast<size_t>(s));
        }
      }
    }
    if (order.size() < nn) {
      // state flops never enter the order (indeg cut makes them sources —
      // they ARE in the order); a shortfall is a residual comb cycle.
      hhds::Node_class rep;
      for (size_t i = 0; i < nn; ++i) {
        if (indeg[i] > 0) {
          rep = nodes[i];  // a node still on the cycle — the diag anchor
          break;
        }
      }
      error_at_node(rep, "upass.tolg: combinational loop in '{}'", ln_->get_top_module_name());
    }

    // 2c-wire — a `comb` runs steps 1-3 ONLY (acyclicity), to catch a comb loop
    // through a self-feeding wire in a standalone-compiled comb top. It has no
    // flops and no `@[N]` landing cycles, so the σ-timing phase below is
    // pipe/mod-specific and skipped.
    if (ln_->get_lambda_kind() == "comb") {
      return;
    }

    // 4. Forward σ with state q pinned to σ(din). Pass 1 leaves state q
    // unconstrained, then we pin σ(q):=σ(din) and re-propagate. A state
    // flop's din may transit through ANOTHER state flop's q, so iterate to a
    // fixpoint (bounded by the state count) — otherwise a chained state reg
    // would home at `any` and silently pass its `@[N]` check.
    const auto din_pid    = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, "din"));
    auto       din_driver = [&](const hhds::Node_class& flop) -> hhds::Pin_class {
      for (const auto& e : flop.inp_edges()) {
        if (!e.sink.is_invalid() && static_cast<uint64_t>(e.sink.get_port_id()) == din_pid) {
          return e.driver;
        }
      }
      return {};
    };
    for (const size_t i : order) {
      eval_node(nodes[i]);
    }
    // Fast exit for modules with no declared timing (the Verilog-origin case,
    // e.g. every XSCore module). The σ-fixpoint below and phases 5/6
    // produce/consume tr_ ONLY through pendings_ (declared `@[N]` / interface
    // cycles) and inserted_ (LN-minted `%pipe_` flops), so with both empty
    // nothing reads the fixpoint's result. The fixpoint is also the only thing
    // that pins a STATE flop's σ from `any` to a concrete cycle (re-walking
    // every node's inp_edges() once per pass, O(state_count * nn)); skipping it
    // can only hide a "mixes values at different cycles" error if some node
    // actually carries a non-zero σ. Non-zero σ requires a feedforward path: an
    // explicit stage depth (flop_depth_ non-empty) or a `pipe`'s plain regs
    // (which default to +1). When there is none — no flop_depth_ and a non-pipe
    // lambda (a `mod`/Verilog reg is cycle-0 state) — every σ is 0, so the
    // single eval pass above already ran an exhaustive mix check and the
    // fixpoint is pure dead work.
    if (pendings_.empty() && inserted_.empty() && flop_depth_.empty() && ln_->get_lambda_kind() != "pipe") {
      return;
    }
    const size_t state_count = state_.size();
    for (size_t pass = 0; pass <= state_count; ++pass) {
      bool changed = false;
      for (size_t i = 0; i < nn; ++i) {
        const auto nid = nodes[i].get_debug_nid();
        if (!state_.contains(nid)) {
          continue;
        }
        const TR pinned = pin_tr(din_driver(nodes[i]));  // σ(q) = σ(din)
        auto     it     = tr_.find(nid);
        if (it == tr_.end() || it->second.any != pinned.any || it->second.min != pinned.min || it->second.max != pinned.max) {
          tr_[nid] = pinned;
          changed  = true;
        }
      }
      for (const size_t i : order) {
        if (state_.contains(nodes[i].get_debug_nid())) {
          continue;  // pinned above
        }
        eval_node(nodes[i]);
      }
      if (!changed) {
        break;
      }
    }

    // 5. Narrow LN-inserted pipe output flops to the body deficit.
    for (const auto& rec : pendings_) {
      if (!rec.is_sink) {
        continue;
      }
      auto edges = rec.pin.inp_edges();
      if (edges.empty()) {
        continue;
      }
      auto mn = edges.front().driver.get_master_node();
      if (mn.is_invalid() || !inserted_.contains(mn.get_debug_nid())) {
        continue;
      }
      const TR sb = pin_tr(din_driver(mn));
      if (sb.any || sb.min != sb.max) {
        continue;  // const-driven or ranged body sigma — declared depth stands
      }
      const int64_t sigma = sb.min;
      if (sigma > rec.min) {
        error_at_node(mn,
                      "upass.tolg: output '{}' of '{}' lands at stage {}, pipe "
                      "declares {}",
                      rec.name,
                      ln_->get_top_module_name(),
                      sigma,
                      rec.min);
      }
      const int64_t nmin = rec.min - sigma;
      const int64_t nmax = rec.max - sigma;
      replace_const_sink(mn, "pipe_min", nmin);
      replace_const_sink(mn, "pipe_max", nmax);
      flop_depth_[mn.get_debug_nid()] = {nmin, nmax};
      if (nmin == 0 && nmax == 0) {
        // (0,0) realizes as a wire: bypass and delete the flop.
        auto din = din_driver(mn);
        edges.front().del_edge();
        rec.pin.connect_driver(din);
        mn.del_node();
      } else {
        tr_[mn.get_debug_nid()] = {sigma + nmin, sigma + nmax, false};
      }
    }

    // 6. Discharge pendings.
    for (const auto& rec : pendings_) {
      // Declared-reg output (`-> (reg q@[N])`). A STATE reg's crossing is
      // already represented by the state cell: sigma(q)=sigma(din), so its
      // home equals the declared landing cycle. A feedforward (stage) reg as
      // output is rejected for a PIPE; for a MOD it discharges through the
      // normal path.
      if (rec.is_sink) {
        if (auto rit = reg_flop_by_name_.find(rec.name); rit != reg_flop_by_name_.end()) {
          const auto fnid     = rit->second;
          const bool is_state = state_.contains(fnid);
          if (!is_state && ln_->get_lambda_kind() == "pipe") {
            error_at_node(pending_anchor(rec),
                          {"pipe-output-reg", "time"},
                          "feedforward register '{}' in the output list of "
                          "'{}' — the output is already "
                          "registered by the pipe contract",
                          rec.name,
                          ln_->get_top_module_name());
          }
          if (is_state) {
            if (rec.min != rec.max) {
              error_at_node(pending_anchor(rec),
                            {"reg-output-cycle", "time"},
                            "register output '{}' of '{}' needs a fixed "
                            "declared cycle (got [{}, {}])",
                            rec.name,
                            ln_->get_top_module_name(),
                            rec.min,
                            rec.max);
            }
            const TR      home          = tr_.contains(fnid) ? tr_.at(fnid) : TR{0, 0, true};
            const int64_t required_home = rec.min;
            if (!home.any && home.min != required_home) {
              error_at_node(pending_anchor(rec),
                            {"reg-output-cycle", "time"},
                            "state register '{}' of '{}' homes at stage {} but "
                            "its declared landing cycle {} "
                            "requires home {}",
                            rec.name,
                            ln_->get_top_module_name(),
                            home.min,
                            rec.min,
                            required_home);
            }
            rec.pin.attr(livehd::attrs::pending_time).del();
            continue;
          }
        }
      }
      TR               cur;
      hhds::Node_class anchor_node;  // the value's driver cell, for the diag span
      if (rec.is_sink) {
        auto edges = rec.pin.inp_edges();
        if (edges.empty()) {
          continue;  // undriven output already warned/nil-wired
        }
        cur         = pin_tr(edges.front().driver);
        anchor_node = edges.front().driver.get_master_node();
      } else {
        cur         = pin_tr(rec.pin);
        anchor_node = rec.pin.get_master_node();
      }
      if (cur.any || (cur.min == rec.min && cur.max == rec.max)) {
        rec.pin.attr(livehd::attrs::pending_time).del();  // removed once checked
        continue;
      }
      error_at_node(anchor_node,
                    "upass.tolg: '{}' in '{}' lands at cycle(s) ({},{}) but "
                    "({},{}) is {}",
                    rec.name,
                    ln_->get_top_module_name(),
                    cur.min,
                    cur.max,
                    rec.min,
                    rec.max,
                    rec.is_sink ? "declared at the interface" : "asserted by `@[N]`");
    }
  }

private:
  [[nodiscard]] TR pin_tr(const hhds::Pin_class& dpin) {
    using livehd::graph_util::is_graph_input_pin;
    using livehd::graph_util::is_type_const;
    using livehd::graph_util::type_op_of;
    if (dpin.is_invalid()) {
      return {0, 0, true};
    }
    if (is_graph_input_pin(dpin)) {
      return {0, 0, false};
    }
    auto mn = dpin.get_master_node();
    if (mn.is_invalid() || is_type_const(mn) || type_op_of(mn) == Ntype_op::Nconst) {
      return {0, 0, true};
    }
    auto it = tr_.find(mn.get_debug_nid());
    if (it == tr_.end()) {
      return {0, 0, true};
    }
    return it->second;
  }

  // Replace a comptime const sink (pipe_min/pipe_max) with a new value.
  void replace_const_sink(const hhds::Node_class& node, std::string_view pin_name, int64_t value) {
    const auto pid = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, pin_name));
    for (const auto& e : node.inp_edges()) {
      if (!e.sink.is_invalid() && static_cast<uint64_t>(e.sink.get_port_id()) == pid) {
        e.del_edge();
        break;
      }
    }
    setup_sink_by_name(const_cast<hhds::Node_class&>(node), pin_name)
        .connect_driver(create_const(*g_, *Dlop::create_integer(value)));
  }

  void eval_node(const hhds::Node_class& node) {
    using livehd::graph_util::is_type_flop;
    using livehd::graph_util::is_type_sub;
    using livehd::graph_util::type_op_of;

    const auto nid = node.get_debug_nid();

    // A state flop's q is pinned (sigma(q)=sigma(din)) outside this
    // function; pass 1 leaves it unconstrained.
    if (state_.contains(nid)) {
      if (!tr_.contains(nid)) {
        tr_[nid] = {0, 0, true};
      }
      return;
    }

    TR meet{0, 0, true};

    // Operand selection per kind: a Flop reads only din; a Sub skips its
    // clock/reset sinks; a Memory skips its clock pins; a Mux/Hotmux skips
    // its SELECT and unions the arm intervals (two paths of different depth
    // = a cycle RANGE, the declaration covers it with `@[a..=b]` or `@[]`).
    // When EVERY arm unifies (constants — e.g. the OR-of-conditions enable
    // chain muxes true/false), the select's σ times the value instead.
    // Everything else meets all inputs (consts unify).
    absl::flat_hash_set<uint64_t> skip_pids;
    bool                          din_only    = false;
    const bool                    is_mem      = type_op_of(node) == Ntype_op::Memory;
    bool                          mem_clocked = false;
    const bool                    is_mux      = type_op_of(node) == Ntype_op::Mux || type_op_of(node) == Ntype_op::Hotmux;
    TR                            mux_sel{0, 0, true};
    if (is_mux) {
      skip_pids.insert(0);  // pid 0 = "s" — the select never adds path depth
      for (const auto& e : node.inp_edges()) {
        if (!e.sink.is_invalid() && e.sink.get_port_id() == 0) {
          mux_sel = pin_tr(e.driver);
          break;
        }
      }
    }
    if (is_type_flop(node)) {
      din_only = true;
    } else if (is_mem) {
      // 2f-mem — a memory is treated like a register (08-memories.md): an
      // ASYNC read (type 0/2) returns committed state with no added stage,
      // exactly like a scalar reg read — @[0] from the read address. Only a
      // SYNC read (type=1, registered dout) charges the +1 crossing. The
      // clock sinks are excluded from the meet either way.
      constexpr int kStride = static_cast<int>(Ntype::Memory_port_stride);  // Memory per-port sink
                                                                            // stride, graph/cell.hpp
      for (const auto& e : node.inp_edges()) {
        if (e.sink.is_invalid()) {
          continue;
        }
        const auto raw_pid   = static_cast<int>(e.sink.get_port_id());
        const auto sink_name = Ntype::get_sink_name(Ntype_op::Memory, raw_pid % kStride);
        if (sink_name == "clock_pin") {
          skip_pids.insert(static_cast<uint64_t>(raw_pid));
        } else if (sink_name == "type" && raw_pid < kStride) {
          skip_pids.insert(static_cast<uint64_t>(raw_pid));
          if (auto v = livehd::graph_util::hydrate_const(e.driver); v.is_just_i64() && v.to_just_i64() == 1) {
            mem_clocked = true;  // sync read: dout is registered
          }
        }
      }
    } else if (is_type_sub(node)) {
      auto gio = node.get_subnode_io();
      if (gio) {
        for (const auto& d : gio->get_input_pin_decls()) {
          if (is_clock_port_name(d.name) || is_reset_port_name(d.name)) {
            skip_pids.insert(static_cast<uint64_t>(d.port_id));
          }
        }
      }
    }
    const auto din_pid = static_cast<uint64_t>(Ntype::get_sink_pid(Ntype_op::Flop, "din"));

    for (const auto& e : node.inp_edges()) {
      if (e.sink.is_invalid()) {
        continue;
      }
      const auto spid = static_cast<uint64_t>(e.sink.get_port_id());
      if (din_only && spid != din_pid) {
        continue;
      }
      if (!din_only && !skip_pids.empty() && skip_pids.contains(spid)) {
        continue;
      }
      TR t = pin_tr(e.driver);
      if (t.any) {
        continue;
      }
      if (meet.any) {
        meet = t;
        continue;
      }
      if (meet.min != t.min || meet.max != t.max) {
        if (is_mux) {
          meet = {std::min(meet.min, t.min), std::max(meet.max, t.max), false};
          continue;
        }
        error_at_node(node,
                      "upass.tolg: '{}' mixes values at different cycles "
                      "(({},{}) vs ({},{})) at a {} cell (sink pid {}) "
                      "— align them with `stage[N]` first",
                      ln_->get_top_module_name(),
                      meet.min,
                      meet.max,
                      t.min,
                      t.max,
                      Ntype::get_name(type_op_of(node)),
                      spid);
      }
    }

    if (is_mux && meet.any) {
      meet = mux_sel;  // const-armed mux: the select times the value
    }

    TR out = meet.any ? TR{0, 0, true} : meet;
    if (is_mem) {
      if (mem_clocked) {
        if (out.any) {
          out = {0, 0, false};
        }
        out = {out.min + 1, out.max + 1, false};
      }
    } else if (is_type_flop(node)) {
      auto    it   = flop_depth_.find(nid);
      int64_t dmin = 1;
      int64_t dmax = 1;
      if (it != flop_depth_.end()) {
        dmin = it->second.first;
        dmax = it->second.second < it->second.first ? it->second.first : it->second.second;
      }
      if (out.any) {
        out = {0, 0, false};
      }
      out = {out.min + dmin, out.max + dmax, false};
    } else if (is_type_sub(node)) {
      auto    it   = sub_time_.find(nid);
      int64_t dmin = it != sub_time_.end() ? it->second.first : 0;
      int64_t dmax = it != sub_time_.end() ? it->second.second : 0;
      if (out.any) {
        out = {0, 0, false};
      }
      out = {out.min + dmin, out.max + dmax, false};
    }
    tr_[nid] = out;
  }

  hhds::Graph*                                               g_;
  std::shared_ptr<Lnast>                                     ln_;
  std::vector<Pending_rec>                                   pendings_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> flop_depth_;
  absl::flat_hash_map<uint64_t, std::pair<int64_t, int64_t>> sub_time_;
  absl::flat_hash_map<uint64_t, std::string>                 plain_regs_;
  absl::flat_hash_set<uint64_t>                              inserted_;
  absl::flat_hash_set<uint64_t>                              wire_cuts_;  // 2c-wire: cut Verilog comb-cycle wire in-edges
  absl::flat_hash_map<std::string, uint64_t>                 reg_flop_by_name_;
  absl::flat_hash_set<uint64_t>                              decl_cycle_regs_;  // regs with an explicit @[N]/interface cycle
                                                                                // (feedforward)
  absl::flat_hash_set<uint64_t>                              state_;
  absl::flat_hash_map<uint64_t, TR>                          tr_;
};

// Transitive clock need. A module needs a clock when its own
// tree declares a reg (stage decls synthesize reg declares) or when any
// pipe/mod CALLEE transitively needs one (its instance's minted clock pin
// must be forwarded). Memoized over the registry; an instantiation cycle
// (illegal mutual hierarchy) breaks false so we never hang — the real
// recursion diagnostic belongs to a later phase.
[[nodiscard]] bool tree_declares_reg(const std::shared_ptr<Lnast>& lnast) {
  auto& cache_slot = lnast->tolg_scan_cache().declares_reg;
  if (cache_slot.has_value()) {
    return *cache_slot;
  }
  // A reg whose `clock_pin=NAME` attr names its clock explicitly does not
  // need the implicit `clock` input (the slang reader stamps these for
  // non-clk/clock Verilog clock names); collect the covered names first.
  absl::flat_hash_set<std::string>      clocked_elsewhere;
  std::function<void(const Lnast_nid&)> scan_attrs = [&](const Lnast_nid& nid) {
    if (Lnast_ntype::is_attr_set(lnast->get_type(nid))) {
      auto tgt = lnast->get_first_child(nid);
      auto key = tgt.is_invalid() ? tgt : lnast->get_sibling_next(tgt);
      if (!key.is_invalid() && lnast->get_name(key) == "clock_pin") {
        clocked_elsewhere.emplace(lnast->get_name(tgt));
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      scan_attrs(c);
    }
  };
  scan_attrs(lnast->get_root());

  std::function<bool(const Lnast_nid&)> has_reg = [&](const Lnast_nid& nid) -> bool {
    // 1a-mem — a __memory(cfg) instantiation needs the clock too (a type=2
    // array config leaves the minted input unused; acceptable, documented).
    if (Lnast_ntype::is_func_call(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      auto c1 = c0.is_invalid() ? c0 : lnast->get_sibling_next(c0);
      if (!c1.is_invalid() && lnast->get_name(c1) == "__memory") {
        return true;
      }
    }
    if (Lnast_ntype::is_declare(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        if (!c1.is_invalid()) {
          auto c2 = lnast->get_sibling_next(c1);
          if (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2))) {
            auto mode = lnast->get_name(c2);
            if ((mode == "reg" || mode.starts_with("reg ")) && !clocked_elsewhere.contains(lnast->get_name(c0))) {
              return true;
            }
          }
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (has_reg(c)) {
        return true;
      }
    }
    return false;
  };
  const bool result = has_reg(lnast->get_root());
  cache_slot        = result;
  return result;
}

void collect_callee_names_impl(const std::shared_ptr<Lnast>& lnast, std::vector<std::string>& out) {
  std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
    if (lnast->is_dce_dead(nid)) {
      return;  // dce:mark — a dead instance call must not pull clock/reset
               // onto the parent (the rebuilt-tree path would have dropped it)
    }
    if (Lnast_ntype::is_func_call(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        // A by-name (same-file) callee is an unquoted ref; an import-bound
        // pipe/mod callee folds to a QUOTED const string (`'unit.entity'`).
        // Accept both and unquote, so the transitive clock/reset walk reaches
        // imported stateful children too (else the parent skips registering the
        // clock/reset it must forward — the "needs_reset bug" at
        // instantiation).
        if (!c1.is_invalid() && (Lnast_ntype::is_ref(lnast->get_type(c1)) || Lnast_ntype::is_const(lnast->get_type(c1)))) {
          std::string nm(lnast->get_name(c1));
          if (nm.size() >= 2 && nm.front() == '\'' && nm.back() == '\'') {
            nm = nm.substr(1, nm.size() - 2);
          }
          out.emplace_back(std::move(nm));
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      walk(c);
    }
  };
  walk(lnast->get_root());
}

// Cached callee-name list. The unquoted by-name/import callees are a pure
// function of this immutable-during-tolg tree, queried by needs_clock_rec /
// needs_reset_rec once per ancestor and phase — compute once.
const std::vector<std::string>& collect_callee_names(const std::shared_ptr<Lnast>& lnast) {
  auto& cache_slot = lnast->tolg_scan_cache().callee_names;
  if (!cache_slot.has_value()) {
    std::vector<std::string> names;
    collect_callee_names_impl(lnast, names);
    cache_slot = std::move(names);
  }
  return *cache_slot;
}

// Registry-wide ABI facts are immutable during one tolg invocation. Keep the
// linear call-graph analysis outside Lnast: the cache owns no units and is
// replaced when a different registry is presented, so it cannot extend Lnast
// lifetime or alter the object's layout/destructor state.
struct Registry_abi_cache {
  const uPass_tolg::Registry*             registry = nullptr;
  absl::flat_hash_map<const Lnast*, bool> needs_clock;
  absl::flat_hash_map<const Lnast*, bool> needs_reset;
  absl::flat_hash_map<const Lnast*, bool> activation_capable;
};

Registry_abi_cache& registry_abi_cache() {
  static thread_local Registry_abi_cache cache;
  return cache;
}

void prepare_registry_abi(const uPass_tolg::Registry& registry);

// Memoized, cycle-guarded transitive "does this module (or any pipe/mod callee)
// satisfy `declares`" walk. `declares` is the per-tree leaf predicate — a plain
// reg declare (needs a clock) or a reset-carrying reg declare (needs a reset).
[[nodiscard]] bool needs_transitive(const std::shared_ptr<Lnast>& lnast, const uPass_tolg::Registry& registry,
                                    absl::flat_hash_map<std::string, bool>& memo, absl::flat_hash_set<std::string>& visiting,
                                    bool (*declares)(const std::shared_ptr<Lnast>&)) {
  const std::string key(lnast->get_top_module_name());
  if (auto it = memo.find(key); it != memo.end()) {
    return it->second;
  }
  if (!visiting.insert(key).second) {
    return false;  // cycle guard
  }
  bool needs = declares(lnast);
  if (!needs) {
    for (const auto& cn : collect_callee_names(lnast)) {
      auto callee = resolve_callee_lnast(cn, registry);
      if (!callee) {
        continue;
      }
      auto kind = callee->get_lambda_kind();
      if (kind != "pipe" && kind != "mod") {
        continue;
      }
      if (needs_transitive(callee, registry, memo, visiting, declares)) {
        needs = true;
        break;
      }
    }
  }
  visiting.erase(key);
  memo[key] = needs;
  return needs;
}

[[nodiscard]] bool needs_clock_rec(const std::shared_ptr<Lnast>& lnast, const uPass_tolg::Registry& registry,
                                   absl::flat_hash_map<std::string, bool>& memo, absl::flat_hash_set<std::string>& visiting) {
  prepare_registry_abi(registry);
  if (const auto it = registry_abi_cache().needs_clock.find(lnast.get()); it != registry_abi_cache().needs_clock.end()) {
    return it->second;
  }
  return needs_transitive(lnast, registry, memo, visiting, &tree_declares_reg);
}

// True when any plain-reg declare carries a non-nil initializer (the
// declare's trailing const [value] child) without an explicit per-reg
// `reset_pin` attr override (those bind their own reset input). A ref init is
// counted (tolg later requires it const; the reset NEED is already real).
[[nodiscard]] bool tree_declares_reset_reg_impl(const std::shared_ptr<Lnast>& lnast) {
  absl::flat_hash_set<std::string>      explicit_rp;
  std::function<void(const Lnast_nid&)> collect_rp = [&](const Lnast_nid& nid) {
    if (Lnast_ntype::is_attr_set(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        if (!c1.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c1)) && lnast->get_name(c1) == "reset_pin") {
          explicit_rp.emplace(lnast->get_name(c0));
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      collect_rp(c);
    }
  };
  collect_rp(lnast->get_root());

  std::function<bool(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) -> bool {
    if (Lnast_ntype::is_declare(lnast->get_type(nid))) {
      auto c0 = lnast->get_first_child(nid);
      if (!c0.is_invalid()) {
        auto c1 = lnast->get_sibling_next(c0);
        auto c2 = c1.is_invalid() ? c1 : lnast->get_sibling_next(c1);
        if (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2))) {
          auto       mode     = lnast->get_name(c2);
          // 1a-mem — an array reg is a memory, and `reg arr:[N]T = <const>`
          // means exactly what it means on a scalar: that const is the RESET
          // value of every entry (user ruling 2026-08-20), so an init'd array
          // needs the implicit reset input just like a flop does. A memory has
          // no parallel reset port, so finalize_mems() realizes the reset as a
          // one-write-per-cycle SWEEP; that is a lowering detail, not a reason
          // to withhold the port. `0sb?` is the array spelling of "no reset
          // value" (lower_mem_declare stops on it exactly like `nil`).
          const bool is_array = !c1.is_invalid() && Lnast_ntype::is_comp_type_array(lnast->get_type(c1));
          // "latch" counts too (2f-latch M7): a latch with a reset value needs
          // the module's reset input created just as a flop does. Keying this
          // on "reg" alone is why `reg l:u8:[latch=true] = 3` used to die with
          // "has a reset value but <mod> has no reset input (setup_io bug)" —
          // the reg was real, the PORT was never made.
          if (mode == "reg" || mode.starts_with("reg ") || (!is_array && mode == "latch")) {
            for (auto c = lnast->get_sibling_next(c2); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
              const auto ct = lnast->get_type(c);
              if (Lnast_ntype::is_stages(ct)) {
                break;  // stage reg — no init slot
              }
              if (Lnast_ntype::is_const(ct) || Lnast_ntype::is_ref(ct)) {
                const auto txt      = lnast->get_name(c);
                const bool nil_init = Lnast_ntype::is_const(ct) && (txt == "nil" || (is_array && txt == "0sb?"));
                if (!nil_init && !explicit_rp.contains(std::string(lnast->get_name(c0)))) {
                  return true;
                }
                break;
              }
            }
          }
        }
      }
    }
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (walk(c)) {
        return true;
      }
    }
    return false;
  };
  return walk(lnast->get_root());
}

// True when any reg (or latch) declare carries a reset value — including an
// ARRAY-typed one, whose concrete (non-nil, non-`0sb?`) initializer is the
// reset value of every entry. The module binds a reset-candidate input, or
// mints the implicit `reset`; the memory lowering then builds a
// one-entry-per-cycle restore sweep.
[[nodiscard]] bool tree_declares_reset_reg(const std::shared_ptr<Lnast>& lnast) {
  auto& slot = lnast->tolg_scan_cache().declares_reset_reg;
  if (!slot.has_value()) {
    slot = tree_declares_reset_reg_impl(lnast);
  }
  return *slot;
}

[[nodiscard]] bool needs_reset_rec(const std::shared_ptr<Lnast>& lnast, const uPass_tolg::Registry& registry,
                                   absl::flat_hash_map<std::string, bool>& memo, absl::flat_hash_set<std::string>& visiting) {
  prepare_registry_abi(registry);
  if (const auto it = registry_abi_cache().needs_reset.find(lnast.get()); it != registry_abi_cache().needs_reset.end()) {
    return it->second;
  }
  return needs_transitive(lnast, registry, memo, visiting, &tree_declares_reset_reg);
}

// Activation is an ABI property of the CALLEE, but it is discovered at call
// sites: a definition reached below a runtime if/match arm must be able to hold
// every kind of state and suppress every property/side effect while that arm is
// inactive. Include transitive descendants because an activated A may call B
// unconditionally; B still runs in A's activation context.
[[nodiscard]] std::vector<std::string> collect_guarded_callee_names(const std::shared_ptr<Lnast>& lnast) {
  std::vector<std::string>                    out;
  std::function<void(const Lnast_nid&, bool)> walk = [&](const Lnast_nid& nid, bool guarded) {
    if (lnast->is_dce_dead(nid)) {
      return;
    }
    const auto type = lnast->get_type(nid);
    if (guarded && Lnast_ntype::is_func_call(type)) {
      auto dst = lnast->get_first_child(nid);
      auto cal = dst.is_invalid() ? dst : lnast->get_sibling_next(dst);
      if (!cal.is_invalid() && (Lnast_ntype::is_ref(lnast->get_type(cal)) || Lnast_ntype::is_const(lnast->get_type(cal)))) {
        std::string name(lnast->get_name(cal));
        if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'') {
          name = name.substr(1, name.size() - 2);
        }
        out.emplace_back(std::move(name));
      }
    }
    const bool branches = Lnast_ntype::is_if(type) || Lnast_ntype::is_unique_if(type);
    size_t     ordinal  = 0;
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c), ++ordinal) {
      // child 0 is the first condition, evaluated in the surrounding context.
      // Every later child is an arm or a later condition, hence conditionally
      // reached. The runner has already removed compile-time-dead arms.
      walk(c, guarded || (branches && ordinal != 0));
    }
  };
  walk(lnast->get_root(), false);
  return out;
}

void prepare_registry_abi(const uPass_tolg::Registry& registry) {
  auto& cache = registry_abi_cache();
  if (cache.registry == &registry) {
    return;
  }
  cache.registry = nullptr;
  cache.needs_clock.clear();
  cache.needs_reset.clear();
  cache.activation_capable.clear();

  std::vector<std::shared_ptr<Lnast>> units;
  units.reserve(registry.size());
  absl::flat_hash_map<const Lnast*, size_t> by_ptr;
  absl::flat_hash_map<std::string, size_t>  by_exact_name;
  for (const auto& ln : registry) {
    if (!ln) {
      continue;
    }
    const size_t idx = units.size();
    units.push_back(ln);
    by_ptr.emplace(ln.get(), idx);
    by_exact_name[std::string(ln->get_top_module_name())] = idx;
  }

  auto resolve_index = [&](std::string_view name) -> std::optional<size_t> {
    if (const auto it = by_exact_name.find(name); it != by_exact_name.end()) {
      return it->second;
    }
    auto ln = resolve_callee_lnast(name, registry);
    if (!ln) {
      return std::nullopt;
    }
    const auto it = by_ptr.find(ln.get());
    return it == by_ptr.end() ? std::nullopt : std::optional<size_t>{it->second};
  };

  std::vector<std::vector<size_t>> edges(units.size());
  std::vector<std::vector<size_t>> state_reverse(units.size());
  std::vector<uint8_t>             activation(units.size(), 0);
  std::vector<uint8_t>             clock(units.size(), 0);
  std::vector<uint8_t>             reset(units.size(), 0);

  std::vector<uint8_t> control_root(units.size(), 0);
  for (size_t i = 0; i < units.size(); ++i) {
    clock[i]        = tree_declares_reg(units[i]);
    reset[i]        = tree_declares_reset_reg(units[i]);
    control_root[i] = std::any_of(units[i]->io_meta().outputs.begin(), units[i]->io_meta().outputs.end(), [](const auto& e) {
      return e.name == "__next_active";
    });
    // Only a NON-template unit is a caller the activation flood may start
    // from (the pre-index scan skipped templates on the caller side); a
    // template's own `__next_active` output still makes it capable, but that
    // is added after the flood so it never spreads to its callees.
    // OR, never assign: an EARLIER unit's guarded-callee loop below may already
    // have marked this one, and overwriting that mark loses the whole reason it
    // is activation capable (the caller precedes the callee in registry order
    // whenever the callee is imported, which is the common case).
    if (!units[i]->is_template()) {
      activation[i] |= control_root[i];
    }

    for (const auto& name : collect_callee_names(units[i])) {
      const auto child = resolve_index(name);
      if (!child.has_value()) {
        continue;
      }
      edges[i].push_back(*child);
      const auto kind = units[*child]->get_lambda_kind();
      if (kind == "pipe" || kind == "mod") {
        state_reverse[*child].push_back(i);
      }
    }
    if (units[i]->is_template()) {
      continue;
    }
    for (const auto& name : collect_guarded_callee_names(units[i])) {
      if (const auto child = resolve_index(name); child.has_value()) {
        activation[*child] = 1;
      }
    }
  }

  auto flood = [](std::vector<uint8_t>& marked, const std::vector<std::vector<size_t>>& adjacency) {
    std::vector<size_t> queue;
    queue.reserve(marked.size());
    for (size_t i = 0; i < marked.size(); ++i) {
      if (marked[i]) {
        queue.push_back(i);
      }
    }
    for (size_t head = 0; head < queue.size(); ++head) {
      for (const auto next : adjacency[queue[head]]) {
        if (!marked[next]) {
          marked[next] = 1;
          queue.push_back(next);
        }
      }
    }
  };
  flood(activation, edges);
  flood(clock, state_reverse);
  flood(reset, state_reverse);
  // A unit that itself publishes `__next_active` is activation capable no
  // matter who reaches it — including a template, which is never a flood root.
  for (size_t i = 0; i < units.size(); ++i) {
    activation[i] |= control_root[i];
  }

  cache.needs_clock.reserve(units.size());
  cache.needs_reset.reserve(units.size());
  cache.activation_capable.reserve(units.size());
  for (size_t i = 0; i < units.size(); ++i) {
    cache.needs_clock.emplace(units[i].get(), clock[i] != 0);
    cache.needs_reset.emplace(units[i].get(), reset[i] != 0);
    cache.activation_capable.emplace(units[i].get(), activation[i] != 0);
  }
  cache.registry = &registry;
}

void reset_registry_abi(const uPass_tolg::Registry& registry) {
  auto& cache    = registry_abi_cache();
  cache.registry = nullptr;
  cache.needs_clock.clear();
  cache.needs_reset.clear();
  cache.activation_capable.clear();
  prepare_registry_abi(registry);
}

[[nodiscard]] bool activation_reaches(const std::shared_ptr<Lnast>& from, const std::shared_ptr<Lnast>& target,
                                      const uPass_tolg::Registry& registry, absl::flat_hash_set<std::string>& visiting) {
  if (from == target || from->get_top_module_name() == target->get_top_module_name()) {
    return true;
  }
  const std::string key(from->get_top_module_name());
  if (!visiting.insert(key).second) {
    return false;
  }
  for (const auto& name : collect_callee_names(from)) {
    auto child = resolve_callee_lnast(name, registry);
    if (child && activation_reaches(child, target, registry, visiting)) {
      visiting.erase(key);
      return true;
    }
  }
  visiting.erase(key);
  return false;
}

[[nodiscard]] bool is_activation_capable(const std::shared_ptr<Lnast>& target, const uPass_tolg::Registry& registry) {
  prepare_registry_abi(registry);
  if (const auto it = registry_abi_cache().activation_capable.find(target.get());
      it != registry_abi_cache().activation_capable.end()) {
    return it->second;
  }
  // Runtime-control loops can deactivate later occurrences through
  // __next_active even when their compact call is not inside a source if.
  for (const auto& e : target->io_meta().outputs) {
    if (e.name == "__next_active") {
      return true;
    }
  }
  for (const auto& caller : registry) {
    if (!caller || caller->is_template()) {
      continue;
    }
    const bool runtime_control_root = std::any_of(caller->io_meta().outputs.begin(),
                                                  caller->io_meta().outputs.end(),
                                                  [](const auto& e) { return e.name == "__next_active"; });
    if (runtime_control_root) {
      absl::flat_hash_set<std::string> visiting;
      if (activation_reaches(caller, target, registry, visiting)) {
        return true;
      }
    }
    for (const auto& name : collect_guarded_callee_names(caller)) {
      auto root = resolve_callee_lnast(name, registry);
      if (!root) {
        continue;
      }
      absl::flat_hash_set<std::string> visiting;
      if (activation_reaches(root, target, registry, visiting)) {
        return true;
      }
    }
  }
  return false;
}

// Shared phase-1 io+clock+reset/activation GraphIO registration. Idempotent (the GraphIO
// add calls are has_-guarded). Returns the clock/reset binding for the body
// build; empty names = the module needs none.
[[nodiscard]] Io_setup setup_io_impl(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path,
                                     const uPass_tolg::Registry& registry) {
  auto& lib      = livehd::Hhds_graph_library::instance(lib_path);
  auto  mod_name = std::string(lnast->get_graph_name());

  auto gio = lib.find_io(mod_name);
  if (!gio) {
    gio = lib.create_io(mod_name);
  }

  // Declare I/O on the GraphIO: positional pin ids + literal bits + sign.
  // Boolean ports are always the unsigned one-bit realization, independently
  // of the legacy io_meta signed flag used by the distinct Pyrope bool kind.
  hhds::Port_id pid     = 1;
  auto          declare = [&](const Lnast_io_entry& e, bool is_input) {
    // Canonical external port name: unquote a slang-read escaped id's backtick
    // form (`` `ar.x` `` -> `ar.x`) so the GraphIO port the LEC matches on is
    // identical to the yosys / Pyrope readers' name. (Mirrors canon_io_name in
    // the lowering class; this declare lambda is a free function so it
    // inlines.) Keep a whitespace content quoted — it cannot be a bare lg name.
    std::string_view nm = e.name;
    if (nm.size() >= 2 && nm.front() == '`' && nm.back() == '`') {
      auto inner = nm.substr(1, nm.size() - 2);
      if (inner.find_first_of(" \t\n\r\f\v") == std::string_view::npos) {
        nm = inner;
      }
    }
    uint32_t bits = e.kind == Io_kind::boolean ? 1u : (e.bits > 0 ? static_cast<uint32_t>(e.bits) : 1u);
    if (is_input) {
      if (!gio->has_input(nm) && !gio->has_output(nm)) {
        gio->add_input(nm, pid);
      }
    } else {
      if (!gio->has_output(nm) && !gio->has_input(nm)) {
        gio->add_output(nm, pid);
      }
    }
    gio->set_bits(nm, bits);
    gio->set_unsign(nm, e.kind == Io_kind::boolean || !e.is_signed);
    ++pid;
  };
  for (const auto& e : lnast->io_meta().inputs) {
    declare(e, /*is_input=*/true);
  }
  for (const auto& e : lnast->io_meta().outputs) {
    declare(e, /*is_input=*/false);
  }

  // Append-only hidden activation ABI. Do not perturb ordinary public tops:
  // only a definition reached from a runtime-conditional call receives the
  // compiler-minted port. Lifted loop bodies already declare __valid, but only
  // runtime-control or conditionally called bodies consume it as execution
  // context; ordinary always-active loops keep their pre-activation netlist.
  std::string valid_name;
  bool        valid_minted   = false;
  bool        valid_active   = is_activation_capable(lnast, registry);
  const bool  explicit_valid = std::any_of(lnast->io_meta().inputs.begin(), lnast->io_meta().inputs.end(), [](const auto& e) {
    return e.name == "__valid";
  });
  if (valid_active || explicit_valid) {
    valid_name = "__valid";
    if (!gio->has_input(valid_name) && !gio->has_output(valid_name)) {
      gio->add_input(valid_name, pid);
      ++pid;
      valid_minted = true;
    }
    gio->set_bits(valid_name, 1);
    gio->set_unsign(valid_name, true);
  }

  // Implicit clock: when the tree holds state (own regs OR,
  // transitively, any pipe/mod callee instance — its minted clock must be
  // forwarded) and the partition has no clk/clock input, mint a 1-bit
  // unsigned "clock" graph input for the flops'/instances' clock_pin.
  std::string clock_name;
  bool        clock_minted = false;
  {
    absl::flat_hash_map<std::string, bool> memo;
    absl::flat_hash_set<std::string>       visiting;
    if (needs_clock_rec(lnast, registry, memo, visiting)) {
      // Reuse a declared clk/clock input only when it can actually be a
      // clock (bool or <=1-bit; untyped bits==0 included) — a multi-bit
      // DATA port that happens to be named clk/clock must not be hijacked
      // as the flop clock.
      for (const auto& e : lnast->io_meta().inputs) {
        if (is_clock_port_name(e.name) && (e.kind == Io_kind::boolean || e.bits <= 1)) {
          clock_name = e.name;
          break;
        }
      }
      if (clock_name.empty()) {
        // Minting the implicit "clock" input collides with any existing
        // multi-bit clock-named port — diagnose instead of double-driving.
        for (const auto& e : lnast->io_meta().inputs) {
          if ((e.name == "clock")) {
            livehd::diag::err("upass.tolg", "clock-collision", "time")
                .msg(
                    "input 'clock' of '{}' is not usable as the pipeline "
                    "clock (multi-bit data port) "
                    "and collides with the implicit clock — rename it or "
                    "declare it 1-bit",
                    mod_name)
                .fatal();
          }
        }
        for (const auto& e : lnast->io_meta().outputs) {
          if ((e.name == "clock")) {
            livehd::diag::err("upass.tolg", "clock-collision", "time")
                .msg(
                    "output 'clock' of '{}' collides with the implicit "
                    "pipeline clock — rename it",
                    mod_name)
                .fatal();
          }
        }
        clock_name = "clock";
        if (!gio->has_input(clock_name) && !gio->has_output(clock_name)) {
          gio->add_input(clock_name, pid);
          ++pid;
        }
        gio->set_bits(clock_name, 1);
        gio->set_unsign(clock_name, true);
        clock_minted = true;
      }
    }
  }

  // Implicit reset: when the tree (or, transitively, any pipe/mod
  // callee instance) holds a reg with a non-nil initializer, bind an existing
  // reset/rst/reset_n/rst_n input (exactly one candidate; the `_n` variants
  // are active-low) or mint a 1-bit unsigned "reset" graph input — the same
  // bind-before-mint pattern as the implicit clock.
  std::string reset_name;
  bool        reset_minted = false;
  bool        reset_neg    = false;
  {
    auto bind_reset_candidate = [&]() {
      int candidates = 0;
      for (const auto& e : lnast->io_meta().inputs) {
        const bool is_cand = (is_reset_port_name(e.name)) && (e.kind == Io_kind::boolean || e.bits <= 1);
        if (!is_cand) {
          continue;
        }
        ++candidates;
        if (reset_name.empty()) {
          reset_name = e.name;
          reset_neg  = e.name.size() > 2 && str_tools::ends_with(e.name, "_n");
        }
      }
      if (candidates > 1) {
        livehd::diag::err("upass.tolg", "reset-ambiguous", "time")
            .msg(
                "'{}' has multiple reset-candidate inputs — give each reg an "
                "explicit `:[reset_pin=…]`",
                mod_name)
            .hint(
                "name exactly one of reset/rst/reset_n/rst_n, or bind "
                "per-reg with `:[reset_pin=…]`")
            .fatal();
      }
    };
    absl::flat_hash_map<std::string, bool> memo;
    absl::flat_hash_set<std::string>       visiting;
    if (needs_reset_rec(lnast, registry, memo, visiting)) {
      bind_reset_candidate();
      if (reset_name.empty()) {
        for (const auto& e : lnast->io_meta().inputs) {
          if ((e.name == "reset")) {
            livehd::diag::err("upass.tolg", "reset-collision", "time")
                .msg(
                    "input 'reset' of '{}' is not usable as the register "
                    "reset (multi-bit data port) "
                    "and collides with the implicit reset — rename it or "
                    "declare it 1-bit",
                    mod_name)
                .fatal();
          }
        }
        for (const auto& e : lnast->io_meta().outputs) {
          if ((e.name == "reset")) {
            livehd::diag::err("upass.tolg", "reset-collision", "time")
                .msg(
                    "output 'reset' of '{}' collides with the implicit "
                    "register reset — rename it",
                    mod_name)
                .fatal();
          }
        }
        reset_name = "reset";
        if (!gio->has_input(reset_name) && !gio->has_output(reset_name)) {
          gio->add_input(reset_name, pid);
          ++pid;
        }
        gio->set_bits(reset_name, 1);
        gio->set_unsign(reset_name, true);
        reset_minted = true;
      }
    }
  }

  return {clock_name, clock_minted, reset_name, reset_minted, reset_neg, valid_name, valid_minted, valid_active};
}

}  // namespace

void uPass_tolg::detect_lg_collisions(const Registry& registry) {
  std::vector<std::pair<std::string, std::string>> seen;  // (graph name, owning unit)
  for (const auto& ln : registry) {
    if (!ln || ln->is_template() || ln->io_meta().empty()) {
      continue;  // same filter as register_io/run: only units that mint a
                 // GraphIO
    }
    std::string gname(ln->get_graph_name());
    std::string unit(ln->get_top_module_name());
    for (const auto& [g, u] : seen) {
      if (g == gname && u != unit) {
        livehd::diag::err("upass.tolg", "lg-name-collision", "type")
            .msg(
                "two units map to the same lgraph/module name '{}' (units "
                "'{}' and '{}')",
                gname,
                u,
                unit)
            .hint("give each `pub` definition a distinct name and/or filename")
            .fatal();
      }
    }
    seen.emplace_back(std::move(gname), std::move(unit));
  }

  // A Registry is normally stack-owned, so a later invocation may reuse the
  // same address with different Lnast objects. Refresh the non-owning ABI
  // analysis once per lowering pass rather than trusting pointer identity.
  reset_registry_abi(registry);
}

// A compiler-minted (`%`-named entity) unit — the comb a `test` block lowers
// to, or a `spawn` block — is simulation-only: its body holds
// `tick`/`step`/`assert` that the `lhd sim` driver (prp_sim) runs, never
// synthesizable hardware. The front-end even drops the `tick` body (an
// unhandled statement), so a value written only inside the loop stays nil and
// its `assert` reads a driverless temp. `inou.cgen.sim` already skips these on
// the same `%`-entity signal; tolg must too. The io_meta().empty() guards below
// only catch a *parameterless* testbench — once a `test` declares a parameter
// (`test t(cycles:u20=20)`) it gains io_meta, defeating that guard, and the
// sim-only body would otherwise be lowered and fail with a dangling `cassert`
// reference.
static bool is_sim_only_unit(const std::shared_ptr<Lnast>& lnast) {
  const auto name   = lnast->get_graph_name();
  const auto entity = name.substr(name.rfind('.') + 1);
  return !entity.empty() && entity.front() == '%';
}

// Decide ONE surviving `cassert` node on the post-upass tree, with exactly the
// predicate lower_cassert applies to the folded driver pin.
static void decide_unlowered_cassert(const std::shared_ptr<Lnast>& lnast, const Lnast_nid& nid) {
  auto cond = lnast->get_first_child(nid);
  if (cond.is_invalid()) {
    return;
  }
  // `assert` / `assert_always` / `assume` share this node type and are DESIGN
  // obligations: they may legally stay unresolved (a testbench `assert` is the
  // runtime check prp_sim emits, an `assert` in a body becomes an fproperty).
  // Only `cassert` is an elaboration check. The kind rides an EXACT-match
  // sentinel const child ahead of the optional user message — never a substring
  // search, or a message merely CONTAINING the text would retype the
  // obligation (the false-PROVEN bug lower_cassert documents).
  auto kind_nid = lnast->get_sibling_next(cond);
  if (kind_nid.is_invalid() || !Lnast_ntype::is_const(lnast->get_type(kind_nid))
      || lnast->get_name(kind_nid) != "__fkind__cassert") {
    return;
  }
  std::string msg;
  if (auto m = lnast->get_sibling_next(kind_nid); !m.is_invalid() && Lnast_ntype::is_const(lnast->get_type(m))) {
    msg = std::string{lnast->get_name(m)};
    if (msg.size() >= 2 && msg.front() == '\'' && msg.back() == '\'') {
      msg = msg.substr(1, msg.size() - 2);  // strip Lconst::to_pyrope quoting
    }
  }
  const bool const_cond = Lnast_ntype::is_const(lnast->get_type(cond));
  if (const_cond) {
    auto v = Dlop::from_pyrope(lnast->get_name(cond));
    // Discharge ONLY on a known-TRUE fold, or on a comptime nil (an unset
    // attribute reads as nil; upass.verifier discharges that same way per
    // attributes_spec §Phase 2). `!is_known_false()` is NOT this predicate: an
    // X/unknown constant is const and not known-false, so it would slip through
    // as "proven" — and a cassert the compiler cannot decide is exactly the
    // case that must fail.
    if (v && !v->is_invalid() && (v->is_nil() || v->is_known_true())) {
      return;
    }
  }
  std::string text = const_cond ? "upass.tolg: cassert condition is not true at compile time"
                                : "upass.tolg: cassert condition did not fold to a "
                                  "compile-time value";
  if (!msg.empty()) {
    text += ": " + msg;
  }
  livehd::diag::err("upass.tolg", const_cond ? "cassert-false" : "cassert-not-comptime", "unsupported")
      .at(lnast->span_of(nid))
      .msg("{}", text)
      .hint(
          "cassert is an elaboration check: it must fold to true at compile "
          "time; use `assert` for a condition that must hold of the hardware")
      .fatal();
}

// `cassert` is an ELABORATION check (user ruling, 2026-07-25): the compiler
// folds it to true, or it is a diagnostic error. It never becomes an LGraph
// node, a netlist check, a simulation check or a formal obligation — that is
// exactly what separates it from `assert`. `lower_cassert` enforces that for
// every lambda BODY the builder walks, but two trees are handed to tolg and
// returned UNLOWERED, so no lower_cassert ever runs on them:
//
//   * the FILE-SCOPE statement tree (empty io_meta — the top-level
//     `mut`/`const`/`cassert` statements of the source file), and
//   * a `%`-named SIM-ONLY unit (the comb a `test` / `spawn` block lowers to),
//     whose casserts inou.prp (prp_sim) would otherwise code-generate as
//     RUNTIME checks that fail during `lhd sim` instead of at build time. The
//     verifier is stripped for those spawned units, so even a comptime-FALSE
//     one survives here undiagnosed.
//
// Both are CLOSED scopes: no call site is left that could bind a value and fold
// the condition later, so an undischarged cassert there is final. A TEMPLATE
// body is the opposite case — it is realized (and folded) per call site — and a
// pre-elaborated import / `.__pub` index is not re-walked this run; all three
// are skipped.
//
// This runs at the tolg seam on purpose: reaching tolg is what says "this
// compilation is producing hardware". An LNAST-only flow (lnast-dump, the LSP,
// the `comptime`/`upass` test tiers, which all pass upass.tolg=false) may still
// carry an unresolved cassert, exactly as the ruling allows.
static void check_unlowered_casserts(const std::shared_ptr<Lnast>& lnast) {
  if (!lnast || lnast->is_template() || lnast->is_pre_elaborated() || lnast->get_top_module_name().ends_with(".__pub")) {
    return;
  }
  // Whole-subtree walk: a cassert can sit inside a `tick` body, an `if`/`uif`
  // arm or a nested `stmts`. (A DEAD `comptime if` / const-`if` arm is already
  // gone — the runner prunes the untaken arm and its casserts with it — so this
  // never sees one. A `match` lowers to a `uif` that KEEPS its untaken arms;
  // deciding the casserts in them is the same behavior lower_cassert already
  // has for lambda bodies.)
  std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
    for (auto c = lnast->get_first_child(nid); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (Lnast_ntype::is_cassert(lnast->get_type(c))) {
        decide_unlowered_cassert(lnast, c);
      }
      walk(c);
    }
  };
  walk(lnast->get_root());
}

void uPass_tolg::gate_activation_clocks(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  // Calls may be lowered before their callee body. In that case HHDS can only
  // classify the Sub from its boundary declarations, so a stateful callee with
  // no loop-break port is provisionally stamped combinational. Refresh every
  // instance bottom-up now that all bodies exist; otherwise a legal feedback
  // path through a child flop remains a local combinational cycle and cgen's
  // cycle tail emits blocking assignments in storage order (a consumer can
  // appear before its producer).
  absl::flat_hash_set<hhds::Graph*> refreshed;
  absl::flat_hash_set<hhds::Graph*> refreshing;
  std::function<void(hhds::Graph*)> refresh_subs = [&](hhds::Graph* graph) {
    if (graph == nullptr || refreshed.contains(graph) || !refreshing.insert(graph).second) {
      return;
    }
    for (auto node : graph->body().nodes()) {
      if (!livehd::graph_util::is_type_sub(node)) {
        continue;
      }
      auto gio = node.get_subnode_io();
      if (gio == nullptr) {
        continue;
      }
      if (gio->has_graph()) {
        refresh_subs(gio->get_graph().get());
      }
      if (auto loop = node.subnode_loop()) {
        node.set_subnode(gio, *loop);
      } else {
        node.set_subnode(gio);
      }
    }
    refreshing.erase(graph);
    refreshed.insert(graph);
  };
  for (const auto& graph : graphs) {
    refresh_subs(graph.get());
  }

  livehd::latch_contract::Clock_port_cache cache;
  for (const auto& graph : graphs) {
    if (graph) {
      (void)livehd::latch_contract::gate_activation_clocks(graph.get(), "upass.tolg", cache);
    }
  }
}

void uPass_tolg::register_io(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path, const Registry& registry) {
  if (!lnast || lnast->io_meta().empty()) {
    return;  // not a lowerable module (e.g. the empty file-root tree)
  }
  if (is_sim_only_unit(lnast)) {
    return;  // testbench / spawn comb — never reserve a GraphIO for it
  }
  // A deferred template (untyped/var-args/generic signature) emits no
  // LGraph: it is realized per call site (comb inlines, pipe/mod/fluid
  // specialize into a concrete clone). Never reserve a GraphIO for it, or a
  // call site could mis-bind to a port-less interface.
  if (lnast->is_template()) {
    return;
  }
  (void)setup_io_impl(lnast, lib_path, registry);
}

std::shared_ptr<hhds::Graph> uPass_tolg::run(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path,
                                             const Registry& registry, std::string_view reset_style) {
  if (!lnast || lnast->io_meta().empty()) {
    // Not a lowerable module (the file-root tree, or a parameterless `test`
    // block). No lower_cassert will run on it, so discharge-or-fail its
    // casserts here before dropping it.
    check_unlowered_casserts(lnast);
    return nullptr;
  }
  // One perfetto slice per lowered unit (profiling builds only) — tolg is
  // invoked directly by the kernel (not via run_step), so without this the
  // whole LNAST->LGraph phase was a blank stretch in the trace.
  TRACE_EVENT("pass", "lnast.tolg", "unit", std::string(lnast->get_top_module_name()));
  if (is_sim_only_unit(lnast)) {
    // Testbench / spawn comb — checked by `lhd sim`, not lowered to hardware.
    // Its `assert`s stay for prp_sim; its `cassert`s are elaboration checks
    // that must be discharged here (nothing downstream can).
    check_unlowered_casserts(lnast);
    return nullptr;
  }
  // A deferred template produces no LGraph (see register_io). A
  // template selected as a synthesis top simply yields no module; it is never
  // a hard error at definition time (contract decision 3).
  if (lnast->is_template()) {
    return nullptr;
  }

  auto io_setup = setup_io_impl(lnast, lib_path, registry);

  auto& lib = livehd::Hhds_graph_library::instance(lib_path);
  auto  gio = lib.find_io(std::string(lnast->get_graph_name()));  // 2f-lg: lg override or mangled name
  // Re-emitting into a --emit-dir that already holds a prior build: instance()
  // deserializes the persisted bodies from disk (GraphLibrary::load
  // materializes every graph_<gid>/body.bin), so gio->has_graph() is true here
  // even though we are about to rebuild this module from its LNAST — the source
  // of truth. Reusing that stale body would make builder.build() append a
  // SECOND copy of the whole module on top of it, fabricating register-feedback
  // cycles the Time_checker then mis-reports as "register feedback through
  // stage registers" (so a clean design crashes on the 2nd `compile` into the
  // same lg: dir). Drop the loaded body but keep the gid + IO decls (set up
  // just above) so the rebuilt graph reuses the same stable IDs across runs.
  if (gio->has_graph()) {
    lib.delete_graph(gio->get_gid());
  }
  auto g_shared = gio->create_graph();

  Tolg builder(lnast, g_shared.get(), std::move(io_setup), &registry, &lib, reset_style == "async");
  builder.build();

  // The combined pipe/mod time checker at the tolg seam. A `comb` runs it too
  // (2c-wire) — but only its acyclicity phase — to catch a combinational loop
  // through a self-feeding wire in a standalone-compiled comb top.
  {
    const auto kind = lnast->get_lambda_kind();
    if (kind == "pipe" || kind == "mod" || kind == "comb") {
      Time_checker checker(g_shared.get(),
                           lnast,
                           builder.take_pending_checks(),
                           builder.take_flop_depths(),
                           builder.take_sub_times(),
                           builder.take_plain_reg_flops(),
                           builder.take_inserted_flops(),
                           builder.take_wire_cuts());
      checker.run();
    }
  }

  g_shared->commit();

#ifndef NDEBUG
  // tolg output invariant (-c dbg): every value-producing cell must be sized.
  // Self-check here (not only at cprop entry) so the guarantee holds even under
  // --recipe O0, where no graph pass runs after tolg.
  livehd::graph_util::debug_assert_cells_sized(*g_shared, "upass.tolg");
#endif

  return g_shared;
}
