//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_tolg.hpp"

#include <bit>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "const.hpp"
#include "graph_library_singleton.hpp"
#include "lnast_ntype.hpp"
#include "node_util.hpp"
#include "pass.hpp"

namespace {

using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::set_bits;
using livehd::graph_util::set_sign;
using livehd::graph_util::set_unsign;
using livehd::graph_util::setup_sink_by_name;

using Pin      = hhds::Pin_class;
using WriteMap = absl::flat_hash_map<std::string, Pin>;

// One lowered value: its driver pin + meaningful (unsigned) bit width `mw`.
// LGraph stores values signed; an unsigned N-bit value occupies N+1 pin bits
// (a leading 0 sign bit), which is what cgen's add_to_pin2var expects (it does
// `--bits` for unsigned dpins). We track `mw` (the N) and stamp `mw+1` bits.
struct Val {
  Pin    pin;
  Bits_t mw{0};
};

// Bits to represent a non-negative value as unsigned (>=1).
[[nodiscard]] Bits_t mw_of_val(int64_t v) {
  if (v <= 0) {
    return 1;
  }
  return static_cast<Bits_t>(std::bit_width(static_cast<uint64_t>(v)));
}

// Builds one hhds::Graph from one post-upass / post-SSA function-tree Lnast.
class Tolg {
public:
  Tolg(const std::shared_ptr<Lnast>& lnast, hhds::Graph* g) : lnast_(lnast), g_(g) {}

  void build() {
    // Inputs: from io_meta(). Unsigned inputs are wrapped in a to-positive
    // Get_mask so signed-declared ports read with their unsigned value (e.g.
    // a 3-bit `a` = 0b111 reads as 7, not -1) — mirrors lgyosys tposs.
    for (const auto& e : lnast_->io_meta().inputs) {
      auto   raw = g_->get_input_pin(e.name);  // body driver pin for the port
      Bits_t mw  = io_mw(e);
      if (e.kind == Io_kind::boolean || mw <= 1) {
        set_bits(raw, 1);
        if (e.is_signed) {
          set_sign(raw);
        } else {
          set_unsign(raw);
        }
        record(e.name, raw, 1);
      } else if (e.is_signed) {
        set_bits(raw, mw);
        set_sign(raw);
        record(e.name, raw, mw);
      } else {
        // Stamp width AND sign on the raw pin too: passes may reconnect
        // consumers straight to the port (cprop mask folds), and a bits-less
        // pin miscompiles in cgen (b[-1] sign-replicate). The raw port reads
        // SIGNED (cgen declares every port signed) — only the to-positive
        // wrapper provides the unsigned view, so mark raw signed to keep
        // sign-sensitive folds (cprop get_mask rule 4) away from it.
        set_bits(raw, mw);
        set_sign(raw);
        record(e.name, to_positive(raw, mw), mw);  // unsigned -> positive
      }
    }
    for (const auto& e : lnast_->io_meta().outputs) {
      out_names_.push_back(e.name);
    }

    // Body: lower the `stmts` child of `top`.
    auto top = lnast_->get_root();
    for (auto c = lnast_->get_first_child(top); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      if (Lnast_ntype::is_stmts(lnast_->get_type(c))) {
        lower_stmts(c);
      }
    }

    // Outputs: connect each output's bound driver to its graph output sink.
    for (const auto& name : out_names_) {
      auto sink = g_->get_output_pin(name);
      if (sink.is_invalid()) {
        continue;
      }
      auto it = pin_map_.find(name);
      if (it == pin_map_.end()) {
        Pass::warn("upass.tolg: output '{}' not driven by body — wiring nil (0sb?)", name);
        sink.connect_driver(create_const(*g_, *Const::from_pyrope("0sb?")));
        continue;
      }
      sink.connect_driver(it->second);
    }
  }

private:
  // ── width / value helpers ───────────────────────────────────────────────────

  [[nodiscard]] static Bits_t io_mw(const Lnast_io_entry& e) {
    if (e.kind == Io_kind::boolean) {
      return 1;
    }
    return e.bits > 0 ? static_cast<Bits_t>(e.bits) : Bits_t{1};
  }

  [[nodiscard]] Pin nil_pin() { return create_const(*g_, *Const::from_pyrope("0sb?")); }

  // To-positive-signed: Get_mask(x, -1) -> same bits, guaranteed non-negative,
  // marked unsigned with one extra (sign) bit. Lets unsigned values flow
  // through signed LGraph arithmetic correctly.
  [[nodiscard]] Pin to_positive(const Pin& src, Bits_t mw) {
    auto node = create_typed_node(*g_, Ntype_op::Get_mask);
    setup_sink_by_name(node, "a").connect_driver(src);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *Const::create_integer(-1)));
    auto drv = node.create_driver_pin(0);
    set_bits(drv, mw + 1);
    set_unsign(drv);
    return drv;
  }

  void record(std::string_view name, const Pin& pin, Bits_t mw) {
    std::string key{name};
    pin_map_[key] = pin;
    mw_map_[key]  = mw;
    if (!branch_writes_.empty()) {
      branch_writes_.back()[key] = pin;
    }
  }

  [[nodiscard]] Bits_t mw_lookup(std::string_view name) {
    auto it = mw_map_.find(std::string{name});
    return it != mw_map_.end() ? it->second : Bits_t{1};
  }

  [[nodiscard]] Pin resolve(std::string_view name) {
    std::string key{name};
    auto        it = pin_map_.find(key);
    if (it != pin_map_.end()) {
      return it->second;
    }
    Pass::warn("upass.tolg: unresolved ref '{}' — wiring nil (0sb?)", name);
    auto p        = nil_pin();
    pin_map_[key] = p;
    mw_map_[key]  = 1;
    return p;
  }

  [[nodiscard]] Val leaf(const Lnast_nid& nid) {
    if (Lnast_ntype::is_const(lnast_->get_type(nid))) {
      auto   c  = Const::from_pyrope(lnast_->get_name(nid));
      Bits_t mw = c->is_i() ? mw_of_val(c->to_i()) : Bits_t{1};
      return {create_const(*g_, *c), mw};
    }
    auto name = lnast_->get_name(nid);
    return {resolve(name), mw_lookup(name)};
  }

  // Bind a computed result: stamp mw+1 unsigned bits and record name->(pin,mw).
  void bind_result(std::string_view name, const Pin& drv, Bits_t mw) {
    Bits_t m = mw > 0 ? mw : Bits_t{1};
    set_bits(drv, m + 1);
    set_unsign(drv);
    record(name, drv, m);
  }

  // ── statement / node dispatch ───────────────────────────────────────────────

  void lower_stmts(const Lnast_nid& stmts) {
    for (auto c = lnast_->get_first_child(stmts); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      lower_node(c);
    }
  }

  void lower_node(const Lnast_nid& nid) {
    using N      = Lnast_ntype;
    const auto t = lnast_->get_type(nid);
    if (N::is_stmts(t)) {
      lower_stmts(nid);
    } else if (N::is_if(t)) {
      lower_if(nid);
    } else if (N::is_store(t)) {
      lower_store(nid);
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
      lower_op(nid, Ntype_op::And, true, OpW::maxw);
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
      lower_op(nid, Ntype_op::SHL, false, OpW::maxw);
    } else if (N::is_sra(t)) {
      lower_op(nid, Ntype_op::SRA, false, OpW::firstw);
    } else if (N::is_bit_not(t) || N::is_log_not(t)) {
      lower_unary(nid, Ntype_op::Not);
    } else if (N::is_ne(t)) {
      lower_negated(nid, Ntype_op::EQ, true);
    } else if (N::is_le(t)) {
      lower_negated(nid, Ntype_op::GT, false);
    } else if (N::is_ge(t)) {
      lower_negated(nid, Ntype_op::LT, false);
    } else {
      Pass::warn("upass.tolg: unhandled node type '{}'", Lnast_ntype::to_sv(t));
    }
  }

  // store(ref(lhs), value) — scalar assignment / alias.
  void lower_store(const Lnast_nid& nid) {
    auto lhs = lnast_->get_first_child(nid);
    if (lhs.is_invalid()) {
      return;
    }
    auto rhs = lnast_->get_sibling_next(lhs);
    if (rhs.is_invalid()) {
      return;
    }
    if (!lnast_->get_sibling_next(rhs).is_invalid()) {
      Pass::warn("upass.tolg: tuple/field store not handled yet (lhs '{}')", lnast_->get_name(lhs));
      return;
    }
    auto v = leaf(rhs);
    record(lnast_->get_name(lhs), v.pin, v.mw);
  }

  // range(ref(dst), lo, hi) — record [lo,hi] for a later get_mask; no node.
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
    range_map_[std::string{lnast_->get_name(dst)}] = {const_val(lo), const_val(hi)};
  }

  [[nodiscard]] int64_t const_val(const Lnast_nid& nid) {
    auto c = Const::from_pyrope(lnast_->get_name(nid));
    return c->is_i() ? c->to_i() : 0;
  }

  // get_mask(ref(dst), value, mask) — mask is a const bitmask or a range ref.
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
    int64_t mask = mask_from_operand(mask_op);

    auto node = create_typed_node(*g_, Ntype_op::Get_mask);
    setup_sink_by_name(node, "a").connect_driver(leaf(val).pin);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *Const::create_integer(mask)));
    auto   drv = node.create_driver_pin(0);
    Bits_t mw  = static_cast<Bits_t>(std::popcount(static_cast<uint64_t>(mask)));
    bind_result(lnast_->get_name(dst), drv, mw);
  }

  [[nodiscard]] int64_t mask_from_operand(const Lnast_nid& mask_op) {
    if (Lnast_ntype::is_const(lnast_->get_type(mask_op))) {
      return const_val(mask_op);
    }
    auto it = range_map_.find(std::string{lnast_->get_name(mask_op)});
    if (it != range_map_.end()) {
      auto [lo, hi] = it->second;
      if (hi < lo) {
        std::swap(lo, hi);
      }
      int width = static_cast<int>(hi - lo + 1);
      if (width <= 0 || width > 63) {
        return 0;
      }
      return ((static_cast<int64_t>(1) << width) - 1) << lo;
    }
    Pass::warn("upass.tolg: get_mask mask operand '{}' not const/range — using 0", lnast_->get_name(mask_op));
    return 0;
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
    int64_t mask = mask_from_operand(mask_op);
    auto    vv   = leaf(val);

    auto node = create_typed_node(*g_, Ntype_op::Set_mask);
    setup_sink_by_name(node, "a").connect_driver(vv.pin);
    setup_sink_by_name(node, "mask").connect_driver(create_const(*g_, *Const::create_integer(mask)));
    setup_sink_by_name(node, "value").connect_driver(leaf(ins).pin);
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), vv.mw);
  }

  enum class OpW { add, mul, maxw, firstw, boolw };

  // n-ary op: child0 = dst, children 1..N = operands. Commutative ops feed all
  // operands into sink "a"; positional binary ops use "a" then "b".
  void lower_op(const Lnast_nid& nid, Ntype_op op, bool commutative, OpW wmode) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto   node    = create_typed_node(*g_, op);
    Bits_t max_mw  = 0;
    Bits_t sum_mw  = 0;
    Bits_t first_mw = 0;
    bool   first   = true;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      auto v      = leaf(c);
      max_mw      = std::max(max_mw, v.mw);
      sum_mw     += v.mw;
      if (first) {
        first_mw = v.mw;
      }
      setup_sink_by_name(node, (commutative || first) ? "a" : "b").connect_driver(v.pin);
      first = false;
    }
    Bits_t mw = 1;
    switch (wmode) {
      case OpW::add: mw = static_cast<Bits_t>(max_mw + 1); break;
      case OpW::mul: mw = sum_mw > 0 ? sum_mw : Bits_t{1}; break;
      case OpW::maxw: mw = max_mw; break;
      case OpW::firstw: mw = first_mw; break;
      case OpW::boolw: mw = 1; break;
    }
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), mw);
  }

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
    auto node = create_typed_node(*g_, op);
    setup_sink_by_name(node, "a").connect_driver(v.pin);
    bind_result(lnast_->get_name(dst), node.create_driver_pin(0), v.mw);
  }

  // ne/le/ge = Not(eq/gt/lt(...)). Result is 1-bit boolean.
  void lower_negated(const Lnast_nid& nid, Ntype_op inner_op, bool commutative) {
    auto dst = lnast_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto inner = create_typed_node(*g_, inner_op);
    bool first = true;
    for (auto c = lnast_->get_sibling_next(dst); !c.is_invalid(); c = lnast_->get_sibling_next(c)) {
      setup_sink_by_name(inner, (commutative || first) ? "a" : "b").connect_driver(leaf(c).pin);
      first = false;
    }
    auto not_node = create_typed_node(*g_, Ntype_op::Not);
    setup_sink_by_name(not_node, "a").connect_driver(inner.create_driver_pin(0));
    bind_result(lnast_->get_name(dst), not_node.create_driver_pin(0), 1);
  }

  // Lower an if-branch body into a fresh write scope; rolls pin_map_ back so
  // branch-local writes don't leak. Returns the names the branch bound.
  WriteMap lower_branch(const Lnast_nid& stmts) {
    branch_writes_.emplace_back();
    auto snapshot = pin_map_;
    if (Lnast_ntype::is_stmts(lnast_->get_type(stmts))) {
      lower_stmts(stmts);
    } else {
      lower_node(stmts);
    }
    auto writes = std::move(branch_writes_.back());
    branch_writes_.pop_back();
    for (const auto& [name, _] : writes) {
      auto it = snapshot.find(name);
      if (it == snapshot.end()) {
        pin_map_.erase(name);
      } else {
        pin_map_[name] = it->second;
      }
    }
    return writes;
  }

  // if(cond, then-stmts, [cond, stmts]*, [else-stmts]) -> per-variable binary
  // Mux chains. Mux pins: 0 = selector, 1 = false/else, 2 = true/then.
  void lower_if(const Lnast_nid& nid) {
    struct Branch {
      bool     is_else{false};
      Pin      cond;
      WriteMap writes;
    };
    std::vector<Branch> branches;

    auto child = lnast_->get_first_child(nid);
    if (child.is_invalid()) {
      return;
    }
    Pin first_cond = leaf(child).pin;  // child0 = condition
    child          = lnast_->get_sibling_next(child);
    if (child.is_invalid()) {
      return;
    }
    branches.push_back({false, first_cond, lower_branch(child)});  // then-stmts

    child = lnast_->get_sibling_next(child);
    while (!child.is_invalid()) {
      bool last = lnast_->is_last_child(child);
      if (last && Lnast_ntype::is_stmts(lnast_->get_type(child))) {
        branches.push_back({true, Pin{}, lower_branch(child)});  // bare else
        break;
      }
      Pin elif_cond = leaf(child).pin;
      child         = lnast_->get_sibling_next(child);
      if (child.is_invalid()) {
        break;
      }
      branches.push_back({false, elif_cond, lower_branch(child)});
      child = lnast_->get_sibling_next(child);
    }

    absl::flat_hash_set<std::string> all_vars;
    for (auto& br : branches) {
      for (auto& [name, _] : br.writes) {
        all_vars.insert(name);
      }
    }

    const bool      has_else    = !branches.empty() && branches.back().is_else;
    const WriteMap& else_writes = has_else ? branches.back().writes : empty_writes_;

    for (const auto& var : all_vars) {
      Pin  cur;
      auto base = pin_map_.find(var);
      cur       = (base != pin_map_.end()) ? base->second : nil_pin();
      auto ew   = else_writes.find(var);
      if (ew != else_writes.end()) {
        cur = ew->second;
      }

      int n         = static_cast<int>(branches.size());
      int last_cond = has_else ? n - 2 : n - 1;
      for (int i = last_cond; i >= 0; --i) {
        auto& br       = branches[i];
        auto  wr       = br.writes.find(var);
        Pin   true_val = (wr != br.writes.end()) ? wr->second : cur;

        auto mux = create_typed_node(*g_, Ntype_op::Mux);
        mux.create_sink_pin(0).connect_driver(br.cond);   // selector
        mux.create_sink_pin(1).connect_driver(cur);       // false / else
        mux.create_sink_pin(2).connect_driver(true_val);  // true / then
        cur = mux.create_driver_pin(0);
      }
      // The merged value's width is the widest among the branch sources.
      bind_result(var, cur, mw_lookup(var));
    }
  }

  std::shared_ptr<Lnast> lnast_;
  hhds::Graph*           g_;

  absl::flat_hash_map<std::string, Pin>                         pin_map_;
  absl::flat_hash_map<std::string, Bits_t>                      mw_map_;
  absl::flat_hash_map<std::string, std::pair<int64_t, int64_t>> range_map_;
  std::vector<WriteMap>                                         branch_writes_;
  std::vector<std::string>                                      out_names_;
  WriteMap                                                      empty_writes_;
};

}  // namespace

std::shared_ptr<hhds::Graph> uPass_tolg::run(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path) {
  if (!lnast || lnast->io_meta().empty()) {
    return nullptr;  // not a lowerable module (e.g. the empty file-root tree)
  }

  auto& lib      = livehd::Hhds_graph_library::instance(lib_path);
  auto  mod_name = std::string(lnast->get_top_module_name());

  auto gio = lib.find_io(mod_name);
  if (!gio) {
    gio = lib.create_io(mod_name);
  }

  // Declare I/O on the GraphIO: positional pin ids + meaningful bits + sign.
  // Port widths cgen emits come from these decls (meaningful width, not the +1
  // internal convention).
  hhds::Port_id pid = 1;
  auto declare = [&](const Lnast_io_entry& e, bool is_input) {
    uint32_t bits = e.kind == Io_kind::boolean ? 1u : (e.bits > 0 ? static_cast<uint32_t>(e.bits) : 1u);
    if (is_input) {
      if (!gio->has_input(e.name) && !gio->has_output(e.name)) {
        gio->add_input(e.name, pid);
      }
    } else {
      if (!gio->has_output(e.name) && !gio->has_input(e.name)) {
        gio->add_output(e.name, pid);
      }
    }
    gio->set_bits(e.name, bits);
    gio->set_unsign(e.name, !e.is_signed);
    ++pid;
  };
  for (const auto& e : lnast->io_meta().inputs) {
    declare(e, /*is_input=*/true);
  }
  for (const auto& e : lnast->io_meta().outputs) {
    declare(e, /*is_input=*/false);
  }

  auto g_shared = gio->has_graph() ? gio->get_graph() : gio->create_graph();

  Tolg builder(lnast, g_shared.get());
  builder.build();

  g_shared->commit();
  return g_shared;
}
