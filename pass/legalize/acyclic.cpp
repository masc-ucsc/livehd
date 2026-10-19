// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "acyclic.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <bit>
#include <optional>
#include <climits>
#include <map>
#include <set>
#include <tuple>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "diag.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "port_reach.hpp"
#include "split_selfref.hpp"

namespace livehd::legalize {

namespace {

namespace gu = livehd::graph_util;

// A compact loop's carry-in also holds the loop's OWN carry-out: the previous
// ordinal's value, a sequencing edge, never a combinational one.
bool carry_self_edge(const hhds::Node_class& m, const hhds::Pin_class& sink, const hhds::Pin_class& drv) {
  if (!m.is_loop_subnode() || drv.get_master_node() != m) {
    return false;
  }
  for (const auto& c : m.subnode_group().carries()) {
    if (c.output_port() == drv.get_port_id() && c.input_port() == sink.get_port_id()) {
      return true;
    }
  }
  return false;
}

// The bits of one pin a walk has already queued: disjoint [lo, hi) intervals.
// add() returns the newly covered pieces (what still has to be walked). Past
// kMax intervals the closest pair merges -- the gap becomes covered too and is
// returned, so the set stays a superset, never a subset. A wide record bus is
// reached at hundreds of scattered fields, so the cap is generous: a coarse
// merge is exactly what turns a false loop back into a reported one.
struct Range_set {
  static constexpr size_t kMax = 4096;
  std::map<int, int>      iv;  // lo -> hi

  std::vector<std::pair<int, int>> add(int lo, int hi) {
    std::vector<std::pair<int, int>> fresh;
    if (hi <= lo) {
      return fresh;
    }
    // Start from the interval that may cover `lo`.
    auto it = iv.upper_bound(lo);
    if (it != iv.begin() && std::prev(it)->second >= lo) {
      --it;
    }
    int nlo = lo, nhi = hi, cur = lo;
    while (it != iv.end() && it->first <= hi) {
      if (it->first > cur) {
        fresh.emplace_back(cur, it->first);
      }
      cur = std::max(cur, it->second);
      nlo = std::min(nlo, it->first);
      nhi = std::max(nhi, it->second);
      it  = iv.erase(it);
    }
    if (cur < hi) {
      fresh.emplace_back(cur, hi);
    }
    iv.emplace(nlo, nhi);
    while (iv.size() > kMax) {  // merge the closest neighbours
      auto best = iv.begin();
      int  gap  = INT_MAX;
      for (auto a = iv.begin(), n = std::next(a); n != iv.end(); ++a, ++n) {
        if (n->first - a->second < gap) {
          gap  = n->first - a->second;
          best = a;
        }
      }
      auto next = std::next(best);
      fresh.emplace_back(best->second, next->first);
      best->second = next->second;
      iv.erase(next);
    }
    return fresh;
  }
};

// The combinational fan-in of driver pins of ONE definition. `refined` false
// is the ATOMIC view (an instance output depends on every input); true is the
// ARC view (the callee's port_reach summary).
class Comb_deps {
public:
  explicit Comb_deps(port_reach::Cache& reach) : reach_(reach) {}

  // The refined view is port-level (each instance output on its port_reach
  // row) unless `bits` is on; bit-level walks are only needed inside the
  // cycles the cheap port-level view still finds.
  void set_bits(bool on) { bits_ = on; }

  // Calls `out` with every driver `d` depends on in the same cycle.
  void deps(const hhds::Pin_class& d, bool refined, const std::function<void(const hhds::Pin_class&)>& out) {
    if (d.is_invalid() || d.is_const() || gu::is_graph_input_pin(d)) {
      return;
    }
    const auto m = d.get_master_node();
    if (m.is_invalid()) {
      return;
    }
    // ARC view, bit-refined: a field select of a packed bus depends only on
    // the bits that reach its window. Without this a 1-bit `get_mask` of a
    // record (`io`) bus depended on the whole bus, every false word-level loop
    // through a packed port read as a loop through the instance, and the
    // instance was inlined -- cascading (xs NewCSR children -> NewCSR -> CSR ->
    // ExeUnitImp -> ExuBlock) until LEC could not prove the flattened top.
    if (refined && bits_ && gu::type_op_of(m) == Ntype_op::Get_mask) {
      if (const auto window = gu::bit_range(m)) {
        const auto src = gu::get_driver_of_sink_name(m, "a");
        if (!src.is_invalid()) {
          const int wlo = window->first;
          const auto walk = [&](int lo, int hi, const std::function<void(const hhds::Pin_class&)>& sink, Range_set* hits) {
            budget_ = kRangeBudget;
            range_deps(src, wlo + lo, wlo + hi, sink, d, hits);
          };
          Range_set hits;
          walk(0, window->second - wlo, out, &hits);
          if (self_loop(hits, walk)) {
            out(d);  // some output bits of this select really feed themselves
          }
          return;
        }
      }
    }
    const auto sink_drivers = [&](const hhds::Pin_class& sink) {
      for (auto drv : sink.get_driver_pins()) {
        if (!carry_self_edge(m, sink, drv)) {
          out(drv);
        }
      }
    };
    const auto all_inputs = [&] {
      for (auto sink : m.inp_sorted_pins()) {
        sink_drivers(sink);
      }
    };
    const auto op = gu::type_op_of(m);
    // Before the register cut: is_type_register counts a Memory, but an async
    // read port is combinational in its own address/enable. Write cones
    // (forwarding, an unclocked array's update) are sequenced after the read
    // by every consumer, so they are no loop; an undecoded port stays a cut,
    // as a Memory always was here.
    if (op == Ntype_op::Memory) {
      auto [it, fresh] = memories_.try_emplace(m, m);
      (void)it->second.deps(static_cast<hhds::Port_id>(d.get_port_id()), out, /*writes=*/false);
      return;
    }
    if (gu::is_type_register(m)) {
      return;  // last period's value
    }
    if (op != Ntype_op::Sub) {
      all_inputs();
      return;
    }
    auto callee = m.get_subnode_graph();
    if (!callee) {
      return;  // a body-less blackbox is opaque to every consumer: its outputs are sources
    }
    if (!refined) {
      all_inputs();
      return;
    }
    // Bit-refined arc: only the input BITS that reach this output count, so a
    // feedback into other fields of a packed input bus is no loop.
    if (const int w = gu::bits_of(d); bits_ && w > 0 && !m.is_loop_subnode()) {
      budget_ = kRangeBudget;
      if (sub_range_deps(m, d, 0, w, out)) {
        return;
      }
    }
    const auto& summary = reach_.callee_of(callee);
    const auto  row     = summary.out2ins.find(static_cast<uint32_t>(d.get_port_id()));
    if (row == summary.out2ins.end()) {
      return;  // a pure function of the callee's state
    }
    absl::flat_hash_set<uint32_t> ins(row->second.begin(), row->second.end());
    if (m.is_loop_subnode()) {
      // The summary is ONE ordinal. From the second on, a carry-in holds the
      // previous carry-out, so the final output also depends on whatever that
      // carry-out reads: close the row over the carries.
      for (bool grew = true; grew;) {
        grew = false;
        for (const auto& c : m.subnode_group().carries()) {
          if (!ins.contains(static_cast<uint32_t>(c.input_port()))) {
            continue;
          }
          if (auto it = summary.out2ins.find(static_cast<uint32_t>(c.output_port())); it != summary.out2ins.end()) {
            for (const auto in : it->second) {
              grew |= ins.insert(in).second;
            }
          }
        }
      }
    }
    for (auto sink : m.inp_sorted_pins()) {
      if (ins.contains(static_cast<uint32_t>(sink.get_port_id()))) {
        sink_drivers(sink);
      }
    }
  }

private:
  static constexpr int kRangeBudget = 1 << 16;

  // Bits [lo, hi) of `p` depend on: `out` receives the driver pins at which the
  // walk stops (anything that is not bit-preserving wiring). A superset of the
  // true bit dependence, never a subset: an operand narrower than the range
  // contributes nothing past its width when unsigned (zero extension) and its
  // top bit when signed (sign extension); an unknown width or an
  // exhausted budget falls back to the whole pin. Iterative (a packed bus is
  // built by chains thousands of cells long), with one interval set per pin.
  //
  // The walk passes THROUGH instance outputs and wiring, so a loop could close
  // inside it without ever becoming an edge. Reaching `origin` (the vertex
  // whose deps are being listed) records the origin BITS reached in `hits`;
  // the caller decides (self_loop) whether some of those bits really feed
  // themselves -- a wide origin reached at other bits than the ones walked is
  // no loop. A loop through another bypassed pin is found from that pin's own
  // walk, which runs because every driver pin is an SCC vertex.
  void range_deps(const hhds::Pin_class& p0, int lo0, int hi0, const std::function<void(const hhds::Pin_class&)>& out,
                  const hhds::Pin_class& origin, Range_set* hits) {
    absl::flat_hash_map<hhds::Class_index, Range_set>           seen;
    absl::flat_hash_set<hhds::Class_index>                      emitted;
    std::vector<std::tuple<hhds::Pin_class, int, int>>          work;
    const auto emit = [&](const hhds::Pin_class& p) {
      if (emitted.insert(p.get_class_index()).second) {
        out(p);
      }
    };
    // Queue bits [lo, hi) of `drv`, clipped to its width; past it, the top bit.
    const auto push = [&](const hhds::Pin_class& drv, int lo, int hi) {
      if (drv.is_invalid() || drv.is_const() || hi <= lo) {
        return;
      }
      if (drv.get_class_index() == origin.get_class_index()) {
        (void)hits->add(lo, hi);  // which ORIGIN bits the walk came back to
        return;
      }
      const int w = gu::bits_of(drv);
      if (w <= 0) {
        emit(drv);
        return;
      }
      lo = std::max(lo, 0);
      if (lo >= w) {
        if (gu::is_unsign(drv)) {
          return;  // zero extension: those bits depend on nothing
        }
        lo = w - 1;  // sign extension: the top bit
        hi = w;
      }
      hi = std::min(hi, w);
      if (gu::is_graph_input_pin(drv)) {
        emit(drv);
        return;
      }
      for (const auto& [plo, phi] : seen[drv.get_class_index()].add(lo, hi)) {
        work.emplace_back(drv, plo, phi);
      }
    };
    if (p0.is_invalid() || p0.is_const() || hi0 <= lo0) {
      return;
    }
    if (p0.get_class_index() == origin.get_class_index()) {
      (void)hits->add(lo0, hi0);
      return;
    }
    if (gu::is_graph_input_pin(p0)) {
      emit(p0);
      return;
    }
    work.emplace_back(p0, lo0, hi0);
    while (!work.empty()) {
      const auto [p, lo, hi] = work.back();
      work.pop_back();
      if (--budget_ <= 0) {
        emit(p);
        continue;  // exhausted: every pin still queued is kept whole
      }
      const auto m  = p.get_master_node();
      const auto op = gu::type_op_of(m);
      const auto const_amount = [&](const char* sink) -> std::optional<int> {
        const auto amt = gu::get_driver_of_sink_name(m, sink);
        if (amt.is_invalid() || !amt.is_const()) {
          return std::nullopt;
        }
        const auto v = gu::const_of(amt);
        if (!v.is_just_i64() || v.to_just_i64() < 0 || v.to_just_i64() > (1 << 24)) {
          return std::nullopt;
        }
        return static_cast<int>(v.to_just_i64());
      };
      bool mapped = true;
      switch (op) {
        case Ntype_op::Get_mask:
          if (const auto w = gu::bit_range(m)) {
            const int len = w->second - w->first;
            if (lo < len) {  // above the window: zero fill
              push(gu::get_driver_of_sink_name(m, "a"), w->first + lo, w->first + std::min(hi, len));
            }
          } else {
            mapped = false;
          }
          break;
        case Ntype_op::Set_mask:
          if (const auto w = gu::bit_range(m)) {
            const int ilo = std::max(lo, w->first), ihi = std::min(hi, w->second);
            if (ilo < ihi) {
              push(gu::get_driver_of_sink_name(m, "value"), ilo - w->first, ihi - w->first);
            }
            const auto a = gu::get_driver_of_sink_name(m, "a");
            if (lo < w->first) {
              push(a, lo, std::min(hi, w->first));
            }
            if (hi > w->second) {
              push(a, std::max(lo, w->second), hi);
            }
          } else {
            mapped = false;
          }
          break;
        case Ntype_op::Concat: {
          const auto lanes = gu::concat_lanes(m);
          if (lanes.empty()) {
            mapped = false;
            break;
          }
          for (const auto& l : lanes) {
            const int llo = std::max(lo, l.offset), lhi = std::min(hi, l.offset + l.width);
            if (llo < lhi) {
              push(l.value, llo - l.offset, lhi - l.offset);
            }
          }
          break;
        }
        case Ntype_op::SHL:
          if (const auto k = const_amount("b")) {
            if (hi > *k) {
              push(gu::get_driver_of_sink_name(m, "a"), std::max(lo - *k, 0), hi - *k);
            }
          } else {
            mapped = false;
          }
          break;
        case Ntype_op::SRA:
          if (const auto k = const_amount("b")) {
            push(gu::get_driver_of_sink_name(m, "a"), lo + *k, hi + *k);
          } else {
            mapped = false;
          }
          break;
        case Ntype_op::And:
        case Ntype_op::Or:
        case Ntype_op::Xor:
        case Ntype_op::Not:
          for (const auto& sink : m.inp_sorted_pins()) {
            for (const auto& drv : sink.get_driver_pins()) {
              push(drv, lo, hi);
            }
          }
          break;
        case Ntype_op::Mux: {
          const auto sel = Ntype::get_sink_pid(Ntype_op::Mux, "s");
          for (const auto& sink : m.inp_sorted_pins()) {
            for (const auto& drv : sink.get_driver_pins()) {
              if (sink.get_port_id() == sel) {
                emit(drv);  // the select is read whole
              } else {
                push(drv, lo, hi);
              }
            }
          }
          break;
        }
        case Ntype_op::Sub: {
          const auto* atoms = instance_atoms(m, p, lo, hi);
          if (atoms == nullptr) {
            mapped = false;
            break;
          }
          for (const auto& [drv, alo, ahi] : *atoms) {
            if (ahi == 0) {
              emit(drv);
            } else {
              push(drv, alo, ahi);
            }
          }
          break;
        }
        default: mapped = false; break;
      }
      if (!mapped) {
        emit(p);
      }
    }
  }

  // The parent drivers (and their bit ranges, [lo,hi); hi == 0 = whole) that
  // bits [lo, hi) of instance output `p` reach through the callee, or nullptr
  // when the callee gives no refinement (keep `p` and its port-level arc).
  const std::vector<std::tuple<hhds::Pin_class, int, int>>* instance_atoms(const hhds::Node_class& m, const hhds::Pin_class& p,
                                                                           int lo, int hi) {
    if (m.is_loop_subnode()) {
      return nullptr;
    }
    const auto callee = m.get_subnode_graph();
    if (!callee) {
      return nullptr;
    }
    const auto* atoms = callee_atoms(callee.get(), static_cast<uint32_t>(p.get_port_id()), lo, hi);
    if (atoms == nullptr) {
      return nullptr;
    }
    scratch_.clear();
    for (const auto& sink : m.inp_sorted_pins()) {
      const auto pid = static_cast<uint32_t>(sink.get_port_id());
      for (const auto& atom : *atoms) {
        if (atom.pid != pid) {
          continue;
        }
        for (const auto& drv : sink.get_driver_pins()) {
          if (!carry_self_edge(m, sink, drv)) {
            scratch_.emplace_back(drv, static_cast<int>(atom.lo), atom.len == 0 ? 0 : static_cast<int>(atom.lo + atom.len));
          }
        }
      }
    }
    return &scratch_;
  }

  // Bits [lo, hi) of instance output `p`, resolved into the parent's own
  // drivers. false = no usable callee refinement (keep the port-level arc).
  bool sub_range_deps(const hhds::Node_class& m, const hhds::Pin_class& p, int lo, int hi,
                      const std::function<void(const hhds::Pin_class&)>& out) {
    if (instance_atoms(m, p, lo, hi) == nullptr) {
      return false;
    }
    const auto walk = [&](int wlo, int whi, const std::function<void(const hhds::Pin_class&)>& sink, Range_set* hits) {
      const auto& emit_to = sink;
      const auto* atoms = instance_atoms(m, p, wlo, whi);
      if (atoms == nullptr) {
        // No bit refinement for this piece (budget): its port-level row, every
        // input it lists read whole -- the same answer the port view gives.
        const auto  callee = m.get_subnode_graph();
        const auto& row    = reach_.callee_of(callee).out2ins;
        const auto  it     = row.find(static_cast<uint32_t>(p.get_port_id()));
        if (it == row.end()) {
          return;
        }
        for (const auto& in : m.inp_sorted_pins()) {
          if (!it->second.contains(static_cast<uint32_t>(in.get_port_id()))) {
            continue;
          }
          for (const auto& drv : in.get_driver_pins()) {
            if (carry_self_edge(m, in, drv)) {
              continue;
            }
            budget_ = kRangeBudget;
            range_deps(drv, 0, std::max(gu::bits_of(drv), 1), emit_to, p, hits);
          }
        }
        return;
      }
      const auto copy = *atoms;  // range_deps reuses the scratch buffer
      for (const auto& [drv, alo, ahi] : copy) {
        if (drv.get_class_index() == p.get_class_index()) {
          // The instance's own output feeds this input: the origin bits it
          // reads are the atom's, not the ones queried.
          (void)hits->add(alo, ahi == 0 ? std::max(gu::bits_of(p), 1) : ahi);
          continue;
        }
        budget_ = kRangeBudget;  // per atom: one wide instance has many
        if (ahi == 0) {
          sink(drv);
        } else {
          range_deps(drv, alo, ahi, sink, p, hits);
        }
      }
    };
    Range_set hits;
    walk(lo, hi, out, &hits);
    if (self_loop(hits, walk)) {
      out(p);
    }
    return true;
  }

  // Does some nonempty set of origin bits feed itself? `hits` are the origin
  // bits a walk of the whole query reached; re-walk only from those, keep the
  // bits reached again, until the set is stable (a real loop) or empty (none).
  template <typename Walk>
  bool self_loop(const Range_set& hits, const Walk& walk) {
    std::vector<std::pair<int, int>> cur(hits.iv.begin(), hits.iv.end());
    const auto ignore = [](const hhds::Pin_class&) {};
    for (int round = 0; round < 16 && !cur.empty(); ++round) {
      Range_set again;
      for (const auto& [lo, hi] : cur) {
        walk(lo, hi, ignore, &again);
      }
      std::vector<std::pair<int, int>> next;  // again ∩ cur
      for (const auto& [a, b] : again.iv) {
        for (const auto& [c, d] : cur) {
          const int lo = std::max(a, c), hi = std::min(b, d);
          if (lo < hi) {
            next.emplace_back(lo, hi);
          }
        }
      }
      std::ranges::sort(next);
      if (next == cur) {
        return true;
      }
      cur = std::move(next);
    }
    return !cur.empty();  // undecided after the bound: report it
  }

  // The callee input bit ranges (pid, lo, len) with a combinational path to
  // bits [lo, hi) of output `out_pid`, by a backward bit-range walk of the
  // callee body. Same cuts as port_reach: registers stop it, a memory's async
  // read reaches its address/enable (Memory_deps, writes=false), a nested
  // instance recurses, a body-less one depends on all of its inputs. Ranges a
  // pin is reached with are kept as a small interval set per pin (a superset),
  // so the walk stays linear in the cone. nullptr = the walk gave up (budget).
  const std::vector<port_reach::In_atom>* callee_atoms(hhds::Graph* g, uint32_t out_pid, int lo, int hi) {
    const auto key = std::make_tuple(g, out_pid, lo, hi);
    if (auto it = atoms_.find(key); it != atoms_.end()) {
      return it->second ? &*it->second : nullptr;
    }
    if (!atoms_busy_.insert(key).second) {
      return nullptr;  // recursion through the hierarchy: be conservative
    }
    auto&                                                         slot = atoms_[key];
    absl::flat_hash_map<hhds::Class_index, Range_set>             seen;
    std::vector<std::tuple<hhds::Pin_class, int, int>>            work;
    absl::flat_hash_map<uint32_t, Range_set>                      in_bits;  // pid -> covered bits
    absl::flat_hash_set<uint32_t>                                 in_whole;
    int                                                           steps = 0;
    bool                                                          ok    = true;
    const auto push = [&](const hhds::Pin_class& drv, int plo, int phi) {
      if (drv.is_invalid() || drv.is_const() || phi <= plo) {
        return;
      }
      const int w = gu::bits_of(drv);
      if (w > 0) {
        if (plo >= w && gu::is_unsign(drv)) {
          return;  // zero extension: those bits depend on nothing
        }
        plo = std::clamp(plo, 0, w - 1);
        phi = std::clamp(phi, plo + 1, w);
      }
      if (gu::is_graph_input_pin(drv)) {
        const auto pid = static_cast<uint32_t>(drv.get_port_id());
        if (w <= 0) {
          in_whole.insert(pid);
          return;
        }
        (void)in_bits[pid].add(plo, phi);
        return;
      }
      for (const auto& [nlo, nhi] : seen[drv.get_class_index()].add(plo, phi)) {
        work.emplace_back(drv, nlo, nhi);
      }
    };
    const auto whole = [&](const hhds::Pin_class& drv) { push(drv, 0, std::max(gu::bits_of(drv), 1 << 20)); };
    {
      hhds::Pin_class opin;
      for (const auto& decl : g->get_io()->get_output_pin_decls()) {
        if (static_cast<uint32_t>(decl.port_id) == out_pid) {
          opin = g->get_output_pin(decl.name);
          break;
        }
      }
      if (opin.is_invalid()) {
        atoms_busy_.erase(key);
        return nullptr;
      }
      for (const auto& drv : opin.get_driver_pins()) {
        push(drv, lo, hi);
      }
    }
    while (ok && !work.empty()) {
      if (++steps > kAtomBudget) {
        ok = false;
        break;
      }
      const auto [p, plo, phi] = work.back();
      work.pop_back();
      const auto m  = p.get_master_node();
      const auto op = gu::type_op_of(m);
      if (gu::is_type_register(m) && op != Ntype_op::Memory) {
        continue;  // last period's value
      }
      if (op == Ntype_op::Memory) {
        auto [mit, fresh] = memories_.try_emplace(m, m);
        if (!mit->second.deps(static_cast<hhds::Port_id>(p.get_port_id()), whole, /*writes=*/false)) {
          for (const auto& sink : m.inp_sorted_pins()) {
            for (const auto& drv : sink.get_driver_pins()) {
              whole(drv);
            }
          }
        }
        continue;
      }
      if (op == Ntype_op::Sub) {
        if (port_reach::state_output(m, p)) {
          continue;  // e.g. a synchronous Liberty SRAM output without a body
        }
        const auto nested = m.get_subnode_graph();
        const std::vector<port_reach::In_atom>* sub = nullptr;
        if (nested && !m.is_loop_subnode()) {
          sub = callee_atoms(nested.get(), static_cast<uint32_t>(p.get_port_id()), plo, phi);
        }
        absl::flat_hash_map<uint32_t, hhds::Pin_class> sink_of;
        for (const auto& sink : m.inp_sorted_pins()) {
          sink_of.emplace(static_cast<uint32_t>(sink.get_port_id()), sink);
        }
        if (sub == nullptr) {
          // A rolled loop, a body-less blackbox, or an exhausted walk: its
          // port-level summary when it has a body, else every input.
          absl::flat_hash_set<uint32_t> ins;
          bool                          all = !nested;
          if (nested) {
            const auto& summary = reach_.callee_of(nested);
            if (auto row = summary.out2ins.find(static_cast<uint32_t>(p.get_port_id())); row != summary.out2ins.end()) {
              ins.insert(row->second.begin(), row->second.end());
            }
            if (m.is_loop_subnode()) {
              all = true;  // carries close the row; stay conservative
            }
          }
          for (const auto& [pid, sink] : sink_of) {
            if (all || ins.contains(pid)) {
              for (const auto& drv : sink.get_driver_pins()) {
                if (!carry_self_edge(m, sink, drv)) {
                  whole(drv);
                }
              }
            }
          }
          continue;
        }
        for (const auto& atom : *sub) {
          if (auto sit = sink_of.find(atom.pid); sit != sink_of.end()) {
            for (const auto& drv : sit->second.get_driver_pins()) {
              push(drv, static_cast<int>(atom.lo), static_cast<int>(atom.lo + atom.len));
            }
          }
        }
        continue;
      }
      // Bit-preserving wiring maps the range; anything else reads its operands whole.
      bool mapped = true;
      switch (op) {
        case Ntype_op::Get_mask:
          if (const auto w = gu::bit_range(m)) {
            const int len = w->second - w->first;
            if (plo < len) {
              push(gu::get_driver_of_sink_name(m, "a"), w->first + plo, w->first + std::min(phi, len));
            }
          } else {
            mapped = false;
          }
          break;
        case Ntype_op::Set_mask:
          if (const auto w = gu::bit_range(m)) {
            const int ilo = std::max(plo, w->first), ihi = std::min(phi, w->second);
            if (ilo < ihi) {
              push(gu::get_driver_of_sink_name(m, "value"), ilo - w->first, ihi - w->first);
            }
            const auto a = gu::get_driver_of_sink_name(m, "a");
            if (plo < w->first) {
              push(a, plo, std::min(phi, w->first));
            }
            if (phi > w->second) {
              push(a, std::max(plo, w->second), phi);
            }
          } else {
            mapped = false;
          }
          break;
        case Ntype_op::Concat: {
          const auto lanes = gu::concat_lanes(m);
          if (lanes.empty()) {
            mapped = false;
            break;
          }
          for (const auto& l : lanes) {
            const int llo = std::max(plo, l.offset), lhi = std::min(phi, l.offset + l.width);
            if (llo < lhi) {
              push(l.value, llo - l.offset, lhi - l.offset);
            }
          }
          break;
        }
        case Ntype_op::And:
        case Ntype_op::Or:
        case Ntype_op::Xor:
        case Ntype_op::Not:
          for (const auto& sink : m.inp_sorted_pins()) {
            for (const auto& drv : sink.get_driver_pins()) {
              push(drv, plo, phi);
            }
          }
          break;
        case Ntype_op::Mux: {
          const auto sel = Ntype::get_sink_pid(Ntype_op::Mux, "s");
          for (const auto& sink : m.inp_sorted_pins()) {
            for (const auto& drv : sink.get_driver_pins()) {
              if (sink.get_port_id() == sel) {
                whole(drv);
              } else {
                push(drv, plo, phi);
              }
            }
          }
          break;
        }
        case Ntype_op::SHL:
        case Ntype_op::SRA: {
          const auto amt = gu::get_driver_of_sink_name(m, "b");
          if (amt.is_invalid() || !amt.is_const() || !gu::const_of(amt).is_just_i64() || gu::const_of(amt).to_just_i64() < 0
              || gu::const_of(amt).to_just_i64() > (1 << 24)) {
            mapped = false;
            break;
          }
          const int k = static_cast<int>(gu::const_of(amt).to_just_i64());
          const auto a = gu::get_driver_of_sink_name(m, "a");
          if (op == Ntype_op::SHL) {
            if (phi > k) {
              push(a, std::max(plo - k, 0), phi - k);
            }
          } else {
            push(a, plo + k, phi + k);
          }
          break;
        }
        default: mapped = false; break;
      }
      if (!mapped) {
        for (const auto& sink : m.inp_sorted_pins()) {
          for (const auto& drv : sink.get_driver_pins()) {
            whole(drv);
          }
        }
      }
    }
    atoms_busy_.erase(key);
    if (!ok) {
      slot.reset();
      return nullptr;
    }
    std::vector<port_reach::In_atom> atoms;
    for (const auto pid : in_whole) {
      atoms.push_back({pid, 0, 0});
    }
    for (const auto& [pid, bits] : in_bits) {
      if (!in_whole.contains(pid)) {
        for (const auto& [lo2, hi2] : bits.iv) {  // map: lo -> hi
          atoms.push_back({pid, static_cast<uint32_t>(lo2), static_cast<uint32_t>(hi2 - lo2)});
        }
      }
    }
    std::ranges::sort(atoms, [](const auto& x, const auto& y) { return std::tie(x.pid, x.lo) < std::tie(y.pid, y.lo); });
    slot = std::move(atoms);
    return &*atoms_[key];
  }

  static constexpr int kAtomBudget = 1 << 20;

  port_reach::Cache&                                             reach_;
  absl::node_hash_map<hhds::Node_class, port_reach::Memory_deps> memories_;
  int                                                            budget_ = kRangeBudget;
  bool                                                           bits_   = false;
  std::vector<std::tuple<hhds::Pin_class, int, int>>                             scratch_;
  using Atom_key = std::tuple<hhds::Graph*, uint32_t, int, int>;
  absl::node_hash_map<Atom_key, std::optional<std::vector<port_reach::In_atom>>> atoms_;
  absl::flat_hash_set<Atom_key>                                                  atoms_busy_;
};

// Iterative Tarjan over driver pins. `roots` seeds the walk; `inside` (when
// non-null) restricts it to a vertex subset. Calls `component` with every
// strongly connected component that is a real cycle (more than one pin, or
// one pin that depends on itself).
void comb_sccs(const std::vector<hhds::Pin_class>& roots, const absl::flat_hash_set<hhds::Class_index>* inside, bool refined,
               Comb_deps& comb, const std::function<void(const std::vector<hhds::Pin_class>&)>& component) {
  struct Info {
    uint32_t index;
    uint32_t low;
    bool     on_stack;
  };
  absl::flat_hash_map<hhds::Class_index, Info> info;
  std::vector<hhds::Pin_class>                 stack;
  struct Frame {
    hhds::Pin_class              pin;
    std::vector<hhds::Pin_class> succ;
    size_t                       next = 0;
    bool                         self = false;
  };
  std::vector<Frame> frames;
  uint32_t           counter = 0;

  const auto admit = [&](const hhds::Pin_class& p) {
    return !p.is_invalid() && !p.is_const() && (inside == nullptr || inside->contains(p.get_class_index()));
  };
  const auto push = [&](const hhds::Pin_class& p) {
    info[p.get_class_index()] = Info{counter, counter, true};
    ++counter;
    stack.push_back(p);
    Frame f;
    f.pin = p;
    comb.deps(p, refined, [&](const hhds::Pin_class& q) {
      if (admit(q)) {
        f.self |= q.get_class_index() == p.get_class_index();
        f.succ.push_back(q);
      }
    });
    frames.push_back(std::move(f));
  };

  for (const auto& root : roots) {
    if (!admit(root) || info.contains(root.get_class_index())) {
      continue;
    }
    push(root);
    while (!frames.empty()) {
      auto& f = frames.back();
      if (f.next < f.succ.size()) {
        const auto q  = f.succ[f.next++];
        auto       it = info.find(q.get_class_index());
        if (it == info.end()) {
          push(q);  // invalidates `f`
        } else if (it->second.on_stack) {
          auto& mine = info[f.pin.get_class_index()];
          mine.low   = std::min(mine.low, it->second.index);
        }
        continue;
      }
      const auto pin  = f.pin;
      const bool self = f.self;
      frames.pop_back();
      const auto me = info[pin.get_class_index()];
      if (!frames.empty()) {
        auto& parent = info[frames.back().pin.get_class_index()];
        parent.low   = std::min(parent.low, me.low);
      }
      if (me.low != me.index) {
        continue;
      }
      std::vector<hhds::Pin_class> members;
      for (;;) {
        const auto top = stack.back();
        stack.pop_back();
        info[top.get_class_index()].on_stack = false;
        members.push_back(top);
        if (top.get_class_index() == pin.get_class_index()) {
          break;
        }
      }
      if (members.size() > 1 || self) {
        component(members);
      }
    }
  }
}

std::string pin_name(const hhds::Pin_class& p) {
  const auto m = p.get_master_node();
  return std::string{gu::has_name(m) ? gu::node_name_of(m) : std::string_view{Ntype::get_name(gu::type_op_of(m))}};
}

// Children before parents, so an instance is inlined from an already-acyclic body.
std::vector<std::shared_ptr<hhds::Graph>> callee_first(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  absl::flat_hash_map<hhds::Gid, std::shared_ptr<hhds::Graph>> by_gid;
  for (const auto& g : graphs) {
    if (g != nullptr && g->get_io() != nullptr) {
      by_gid.emplace(g->get_gid(), g);
    }
  }
  std::vector<std::shared_ptr<hhds::Graph>> order;
  absl::flat_hash_set<hhds::Gid>            done;
  const auto visit = [&](auto& self, const std::shared_ptr<hhds::Graph>& g) -> void {
    if (!done.insert(g->get_gid()).second) {
      return;
    }
    for (auto n : g->body().nodes()) {
      if (!n.is_invalid() && gu::type_op_of(n) == Ntype_op::Sub) {
        if (auto it = by_gid.find(n.get_subnode_gid()); it != by_gid.end()) {
          self(self, it->second);
        }
      }
    }
    order.push_back(g);
  };
  for (const auto& g : graphs) {
    if (g != nullptr && g->get_io() != nullptr) {
      visit(visit, g);
    }
  }
  return order;
}

// A non-loop instance whose whole closure holds no state (no
// Flop/Fflop/Latch/Memory, no rolled loop, no blackbox): inlining it changes
// no state identity. Shared across the design: callee-first order means a
// callee's closure is final before any parent asks.
class State_free {
public:
  bool operator()(const hhds::Node_class& sub) {
    if (sub.is_invalid() || sub.is_loop_subnode() || !sub.get_subnode_graph()) {
      return false;
    }
    return def(sub.get_subnode_graph().get());
  }

private:
  bool def(hhds::Graph* g) {
    if (auto it = cache_.find(g->get_gid()); it != cache_.end()) {
      return it->second;
    }
    cache_.emplace(g->get_gid(), false);  // recursion guard
    bool ok = true;
    for (auto n : g->body().nodes()) {
      if (gu::is_type_register(n) || (gu::type_op_of(n) == Ntype_op::Sub && !(*this)(n))) {
        ok = false;
        break;
      }
    }
    cache_[g->get_gid()] = ok;
    return ok;
  }
  absl::flat_hash_map<hhds::Gid, bool> cache_;
};

// Shared by every scan of one make_acyclic call. A callee's summary is final
// once that callee is processed (callee first); a split half is a NEW def; and
// a def deleted in between stays alive in Split_state::removed, so no memo
// entry outlives its graph.
struct Scan_ctx {
  port_reach::Cache reach{port_reach::Callee_reach{}, /*slices=*/false};
  State_free        state_free;
};

struct Scc_scan {
  // ARC-level cycles: an instance's output depends on the inputs its
  // port_reach summary lists (a Moore output on none), the arcs every consumer
  // orders instances by. A valid/ready handshake is no such cycle; a real path
  // that leaves and re-enters a port is.
  std::vector<std::vector<hhds::Pin_class>> cycles;
  // On a cycle only with instances ATOMIC -- no cycle at arc level, but a
  // consumer would have to evaluate the instance in pieces (sim's
  // per-output-group callee partitions): state-free instances, inlined (no
  // state moves). Rolled loops are never here -- never split, never inlined.
  std::vector<hhds::Node_class> state_free;
  // A component through an instance that the arc view refined away. It can
  // still hold a packed-slice loop INSIDE this body: a field read of a packed
  // word that feeds another field of that same word is no loop at bit level
  // (which is what the refined view sees once any instance shares the
  // component), but it IS a word-level cycle for every consumer that orders
  // this body's cells one word at a time -- LEC's encoder stalls on it.
  bool refined_away = false;
};

Scc_scan scan(hhds::Graph* g, Scan_ctx& ctx) {
  Comb_deps                    comb(ctx.reach);
  std::vector<hhds::Pin_class> roots;
  for (auto node : g->body().nodes()) {
    for (auto drv : node.out_sorted_pins()) {
      roots.push_back(drv);
    }
  }
  Scc_scan                              out;
  absl::flat_hash_set<hhds::Node_class> seen;
  comb_sccs(roots, nullptr, /*refined=*/false, comb, [&](const std::vector<hhds::Pin_class>& members) {
    absl::flat_hash_set<hhds::Class_index> inside;
    for (const auto& p : members) {
      inside.insert(p.get_class_index());
    }
    bool through_sub = false;
    for (const auto& p : members) {
      const auto m = p.get_master_node();
      if (gu::type_op_of(m) != Ntype_op::Sub) {
        continue;
      }
      through_sub = true;
      if (!seen.insert(m).second) {
        continue;
      }
      // A rolled loop (a Sub with loop annotations) is never split or
      // inlined: on an atomic-only cycle it is no loop at all, and on an
      // arc-level one the path is a TRUE combinational loop through it, which
      // is reported below as an error (comb-loop-through-loop).
      if (!m.is_loop_subnode() && ctx.state_free(m)) {
        out.state_free.push_back(m);
      }
    }
    if (!through_sub) {
      out.cycles.push_back(members);
      return;
    }
    // Port-level arcs first (cheap), then bit level only inside the cycles
    // they leave: a false loop through a packed bus is port-level only.
    comb_sccs(members, &inside, /*refined=*/true, comb, [&](const std::vector<hhds::Pin_class>& port_cycle) {
      out.refined_away = true;  // a port-level cycle; the bit view below may refine it away
      absl::flat_hash_set<hhds::Class_index> in_cycle;
      for (const auto& p : port_cycle) {
        in_cycle.insert(p.get_class_index());
      }
      comb.set_bits(true);
      comb_sccs(port_cycle, &in_cycle, /*refined=*/true, comb, [&](const std::vector<hhds::Pin_class>& arc) {
        out.cycles.push_back(arc);
      });
      comb.set_bits(false);
    });
  });
  return out;
}

std::string instance_label(const hhds::Node_class& m) {
  if (gu::has_name(m)) {
    return std::string{gu::node_name_of(m)};
  }
  const auto callee = m.get_subnode_graph();
  return callee ? std::string{callee->get_name()} : std::string{"-"};
}


// ---------------------------------------------------------------------------
// FALSE CYCLES: a full case's "no branch matched" hold arm.
//
// The yosys reader runs `proc -ifx`, which skips proc_rmdead, so a full case
// (`case (nshc) 1'b0: y = a; 1'b1: y = b; endcase` -- CORE-ET's txfma family,
// B07) keeps the arm taken when NO item matched, and pmuxtree builds it as
// `y = s1 ? a : (s2 ? b : y)`: a combinational cycle the RTL does not have
// (Verilator lints the VPU with no UNOPTFLAT). Turning -ifx off globally is not
// safe (measured on the d2 branch: one module's process lowering never
// terminates, two fail the latch contract), so the cycle is broken HERE, and
// only when it is provably never active.
//
// A simple cycle through two-arm muxes is ACTIVE under an input iff every mux
// on it selects its cycle arm. If that conjunction is UNSATISFIABLE, every
// input breaks the cycle at some mux taking its other arm. Then unrolling it
// once at a mux M is exact: M's cycle arm is fed by a CLONE of the path around
// the cycle in which M's own output is replaced by a constant. Whenever M takes
// that arm, some other mux on the clone takes its non-cycle arm (same selector,
// same drivers), so the clone delivers the original value; when M takes its
// other arm the cycle arm is not read. The function is unchanged for every
// 2-state input -- which is the Lean model's domain.
//
// The condition is decided EXHAUSTIVELY over the selectors' support, with a
// deliberately narrow, exact value model (EQ against an in-range constant or
// between two same-width values, bitwise And/Or/Xor/Not at one width, Ror,
// in-range bit extracts, one-bit wrappers, two-arm Mux); every other node is
// an enumerated variable, and a support wider than
// 20 bits, a cycle through a selector, or any other mux kind fails closed --
// the cycle is then reported exactly as before.
constexpr int kMaxCondBits = 20;

struct Cond_model {
  // A VARIABLE is a base pin plus the bits of it the conditions READ (a full
  // read sets every bit; a Get_mask slice of a would-be variable sets its
  // range). Enumerating exactly those bits of ONE variable per base keeps two
  // slices of one selector correlated: `sel[1:0] == 2` and `sel[1]` cannot
  // disagree. Unread bits are 0 in every assignment, and nothing reads them.
  std::vector<hhds::Pin_class>                 vars;
  std::vector<uint64_t>                        used;  // per var: the bits read
  absl::flat_hash_map<hhds::Class_index, int>  var_of;
  absl::flat_hash_map<hhds::Class_index, bool> evaluable;  // per pin: modelled as an op (true) or a var (false)
  int                                          bits = 0;   // total bits enumerated
  bool                                         ok   = true;
};

int pin_w(const hhds::Pin_class& p) { return static_cast<int>(gu::bits_of(p)); }

std::optional<int64_t> small_const(const hhds::Pin_class& p) {
  if (!p.is_const()) {
    return std::nullopt;
  }
  const auto& c = gu::const_of(p);
  if (c.has_unknowns() || !c.is_just_i64()) {
    return std::nullopt;
  }
  return c.to_just_i64();
}

uint64_t mask_w(int w);

// Make `p` a variable (or widen its read bits by `bits_read`).
void add_var(const hhds::Pin_class& p, Cond_model& m, uint64_t bits_read) {
  const int w = pin_w(p);
  if (w <= 0 || w > 62) {
    m.ok = false;
    return;
  }
  bits_read &= mask_w(w);
  auto it = m.var_of.find(p.get_class_index());
  if (it == m.var_of.end()) {
    it = m.var_of.emplace(p.get_class_index(), static_cast<int>(m.vars.size())).first;
    m.vars.push_back(p);
    m.used.push_back(0);
  }
  auto& u = m.used[static_cast<size_t>(it->second)];
  m.bits += std::popcount(bits_read & ~u);
  u |= bits_read;
  if (m.bits > kMaxCondBits) {
    m.ok = false;
  }
}
void add_var(const hhds::Pin_class& p, Cond_model& m) { add_var(p, m, ~uint64_t{0}); }

// Register `p` in the model: as an operation over modelled operands, or as a
// variable. Fails the model (m.ok = false) on what cannot be either.
void model_pin(const hhds::Pin_class& p, Cond_model& m, int depth) {
  if (!m.ok || p.is_invalid()) {
    m.ok = false;
    return;
  }
  if (small_const(p)) {
    return;
  }
  if (p.is_const() || depth > 32) {
    m.ok = false;
    return;
  }
  if (m.evaluable.contains(p.get_class_index()) || m.var_of.contains(p.get_class_index())) {
    return;
  }
  if (gu::is_graph_input_pin(p)) {
    add_var(p, m);
    return;
  }
  const auto n   = p.get_master_node();
  const auto op  = gu::type_op_of(n);
  const auto ins = gu::inp_sink_drivers(n);
  const int  w   = pin_w(p);
  bool       as_op = false;
  if (op == Ntype_op::EQ && ins.size() == 2) {
    const auto c0 = small_const(ins[0].driver), c1 = small_const(ins[1].driver);
    if (static_cast<bool>(c0) != static_cast<bool>(c1)) {
      const auto& x  = c0 ? ins[1].driver : ins[0].driver;
      const auto  c  = c0 ? *c0 : *c1;
      const int   xw = pin_w(x);
      as_op          = w == 1 && xw > 0 && xw <= 62 && c >= 0 && c < (int64_t{1} << xw);
      if (as_op) {
        model_pin(x, m, depth + 1);
      }
    } else if (!c0 && !c1) {
      // two VALUES of one width: unsigned equality is exact
      as_op = w == 1 && pin_w(ins[0].driver) == pin_w(ins[1].driver) && pin_w(ins[0].driver) > 0
              && pin_w(ins[0].driver) <= 62;
      if (as_op) {
        model_pin(ins[0].driver, m, depth + 1);
        model_pin(ins[1].driver, m, depth + 1);
      }
    }
  } else if ((op == Ntype_op::Or || op == Ntype_op::And || op == Ntype_op::Xor) && w > 0 && w <= 62 && !ins.empty()) {
    // Bitwise at the OUTPUT width: every value operand of that width, every
    // constant within it (or -1, all ones). Exact unsigned bit-vector logic --
    // the casez match `(sel & 2'b10) == 2'b10` is one of these.
    as_op = std::ranges::all_of(ins, [&](const auto& in) {
      const auto c = small_const(in.driver);
      return c ? (*c == -1 || (*c >= 0 && *c < (int64_t{1} << w))) : pin_w(in.driver) == w;
    });
    if (as_op) {
      for (const auto& in : ins) {
        model_pin(in.driver, m, depth + 1);
      }
    }
  } else if (op == Ntype_op::Not && w > 0 && w <= 62 && ins.size() == 1 && pin_w(ins[0].driver) == w) {
    as_op = true;
    model_pin(ins[0].driver, m, depth + 1);
  } else if (op == Ntype_op::Ror && w == 1 && ins.size() == 1 && pin_w(ins[0].driver) > 0 && pin_w(ins[0].driver) <= 62) {
    as_op = true;
    model_pin(ins[0].driver, m, depth + 1);
  } else if (op == Ntype_op::Get_mask) {
    const auto rng = gu::bit_range(n);
    const auto a   = gu::get_driver_of_sink_name(n, "a");
    as_op = rng && !a.is_invalid() && rng->first >= 0 && rng->second > rng->first && rng->second <= pin_w(a) && pin_w(a) <= 62
            && w == rng->second - rng->first;
    // A bit slice of something that would only be a VARIABLE (an input, state,
    // an op this model does not evaluate) is itself the variable: enumerate the
    // slice, not the word. Any pin may be a free variable -- that only adds
    // assignments, so an UNSAT verdict stays a proof -- and slicing keeps a
    // 19-bit flop read one bit at a time inside the enumeration budget.
    if (as_op && !small_const(a) && !m.evaluable.contains(a.get_class_index())) {
      const auto ao = gu::is_graph_input_pin(a) || a.is_const() ? Ntype_op::Invalid : gu::type_op_of(a.get_master_node());
      const bool a_evaluable_kind = !gu::is_graph_input_pin(a) && !gu::is_type_register(a.get_master_node())
                                    && (ao == Ntype_op::EQ || ao == Ntype_op::Or || ao == Ntype_op::And
                                        || ao == Ntype_op::Xor || ao == Ntype_op::Not || ao == Ntype_op::Ror
                                        || ao == Ntype_op::Get_mask || ao == Ntype_op::Sext || ao == Ntype_op::Mux);
      if (!a_evaluable_kind) {
        // the operand is a variable: read only this slice of it
        add_var(a, m, mask_w(rng->second) & ~mask_w(rng->first));
        m.evaluable[p.get_class_index()] = true;
        return;
      }
    }
    if (as_op) {
      model_pin(a, m, depth + 1);
    }
  } else if (op == Ntype_op::Sext && w == 1) {
    const auto a = gu::first_value_driver(n);
    as_op        = !a.is_invalid() && pin_w(a) == 1;
    if (as_op) {
      model_pin(a, m, depth + 1);
    }
  } else if (op == Ntype_op::Mux && ins.size() == 3 && w > 0 && w <= 62) {
    as_op = pin_w(ins[0].driver) > 0 && pin_w(ins[0].driver) <= 62
            && std::ranges::all_of(ins, [&](const auto& in) { return in.get_port_id() == 0 || small_const(in.driver) || pin_w(in.driver) == w; });
    if (as_op) {
      for (const auto& in : ins) {
        model_pin(in.driver, m, depth + 1);
      }
    }
  }
  if (as_op) {
    m.evaluable[p.get_class_index()] = true;
  } else {
    add_var(p, m);
  }
}

uint64_t mask_w(int w) { return w >= 64 ? ~uint64_t{0} : ((uint64_t{1} << w) - 1); }

uint64_t eval_pin(const hhds::Pin_class& p, const Cond_model& m, const std::vector<uint64_t>& asg,
                  absl::flat_hash_map<hhds::Class_index, uint64_t>& memo);

// The value of `p` restricted to its own width. A constant keeps its value: its pin
// can report width 0 (an unsized or folded literal), and masking by that width made
// `1'b1` read as false, so a cycle whose muxes all select the cycle arm through
// constant selectors looked unsatisfiable and a real loop was unrolled away.
uint64_t eval_masked(const hhds::Pin_class& p, const Cond_model& m, const std::vector<uint64_t>& asg,
                     absl::flat_hash_map<hhds::Class_index, uint64_t>& memo) {
  const auto v = eval_pin(p, m, asg, memo);
  if (small_const(p)) {
    return v;
  }
  const int w = pin_w(p);
  return w > 0 ? (v & mask_w(w)) : v;
}

uint64_t eval_pin(const hhds::Pin_class& p, const Cond_model& m, const std::vector<uint64_t>& asg,
                  absl::flat_hash_map<hhds::Class_index, uint64_t>& memo) {
  if (const auto c = small_const(p)) {
    return static_cast<uint64_t>(*c);
  }
  if (auto v = m.var_of.find(p.get_class_index()); v != m.var_of.end()) {
    return asg[static_cast<size_t>(v->second)];
  }
  if (auto it = memo.find(p.get_class_index()); it != memo.end()) {
    return it->second;
  }
  const auto n   = p.get_master_node();
  const auto op  = gu::type_op_of(n);
  const auto ins = gu::inp_sink_drivers(n);
  const int  w   = pin_w(p);
  uint64_t   r   = 0;
  if (op == Ntype_op::EQ) {
    const auto c0 = small_const(ins[0].driver), c1 = small_const(ins[1].driver);
    if (c0 || c1) {
      const auto& x = c0 ? ins[1].driver : ins[0].driver;
      const auto  c = static_cast<uint64_t>(c0 ? *c0 : *c1);
      r             = eval_masked(x, m, asg, memo) == c ? 1 : 0;
    } else {
      const auto mw = mask_w(pin_w(ins[0].driver));
      r = (eval_pin(ins[0].driver, m, asg, memo) & mw) == (eval_pin(ins[1].driver, m, asg, memo) & mw) ? 1 : 0;
    }
  } else if (op == Ntype_op::Or || op == Ntype_op::And || op == Ntype_op::Xor) {
    r = op == Ntype_op::And ? mask_w(w) : 0;
    for (const auto& in : ins) {
      const auto v = eval_pin(in.driver, m, asg, memo) & mask_w(w);
      r            = op == Ntype_op::And ? (r & v) : op == Ntype_op::Or ? (r | v) : (r ^ v);
    }
  } else if (op == Ntype_op::Not) {
    r = (~eval_pin(ins[0].driver, m, asg, memo)) & mask_w(w);
  } else if (op == Ntype_op::Ror) {
    r = eval_masked(ins[0].driver, m, asg, memo) != 0 ? 1 : 0;
  } else if (op == Ntype_op::Get_mask) {
    const auto rng = gu::bit_range(n);
    const auto a   = gu::get_driver_of_sink_name(n, "a");
    r              = (eval_masked(a, m, asg, memo) >> rng->first) & mask_w(w);
  } else if (op == Ntype_op::Sext) {
    r = eval_pin(gu::first_value_driver(n), m, asg, memo) & 1;
  } else if (op == Ntype_op::Mux) {
    hhds::Pin_class sel, a0, a1;
    for (const auto& in : ins) {
      (in.get_port_id() == 0 ? sel : in.get_port_id() == 1 ? a0 : a1) = in.driver;
    }
    const bool t = eval_masked(sel, m, asg, memo) != 0;
    r            = eval_pin(t ? a1 : a0, m, asg, memo) & mask_w(w);
  }
  memo.emplace(p.get_class_index(), r);
  return r;
}

// Is there an assignment of the model's variables under which every
// (selector, wanted-truth) holds? Exhaustive.
bool conds_satisfiable(const std::vector<std::pair<hhds::Pin_class, bool>>& conds, const Cond_model& m) {
  std::vector<uint64_t> asg(m.vars.size(), 0);
  const uint64_t        total = uint64_t{1} << m.bits;
  for (uint64_t k = 0; k < total; ++k) {
    uint64_t rest = k;
    for (size_t i = 0; i < m.vars.size(); ++i) {
      uint64_t v = 0;  // deposit the next popcount(used) enumeration bits on the read positions
      for (uint64_t u = m.used[i]; u != 0; u &= u - 1) {
        if (rest & 1) {
          v |= u & (~u + 1);
        }
        rest >>= 1;
      }
      asg[i] = v;
    }
    absl::flat_hash_map<hhds::Class_index, uint64_t> memo;
    bool                                             all = true;
    for (const auto& [sel, want] : conds) {
      const bool t = eval_masked(sel, m, asg, memo) != 0;
      if (t != want) {
        all = false;
        break;
      }
    }
    if (all) {
      return true;
    }
  }
  return false;
}

// One simple cycle inside the component `inside`, through `start` (a two-arm
// Mux's output): the path [(sink entering node_i, driver leaving node_i)],
// ending at the sink of `start`'s node the cycle re-enters. nullopt if none.
struct Cycle_hop {
  hhds::Pin_class sink, drv;
};
std::optional<std::vector<Cycle_hop>> simple_cycle(const hhds::Pin_class& start,
                                                   const absl::flat_hash_set<hhds::Class_index>& inside) {
  const auto start_node = start.get_master_node();
  absl::flat_hash_map<hhds::Class_index, std::pair<hhds::Pin_class, hhds::Pin_class>> parent;  // drv -> (prev drv, sink)
  std::vector<hhds::Pin_class> frontier{start};
  absl::flat_hash_set<hhds::Class_index> seen{start.get_class_index()};
  while (!frontier.empty()) {
    std::vector<hhds::Pin_class> next;
    for (const auto& d : frontier) {
      for (const auto& e : d.out_edges()) {
        const auto sn = e.sink.get_master_node();
        if (sn.get_class_index() == start_node.get_class_index()) {
          // closed: rebuild the path back to start
          std::vector<Cycle_hop> path;
          path.push_back({e.sink, start});
          auto cur = d;
          while (cur.get_class_index() != start.get_class_index()) {
            const auto& [prev, sink] = parent.at(cur.get_class_index());
            path.push_back({sink, cur});
            cur = prev;
          }
          std::reverse(path.begin(), path.end());
          return path;  // path[i].sink enters path[i].drv's node; the last hop re-enters start's node
        }
        for (auto nd : sn.out_sorted_pins()) {
          if (!inside.contains(nd.get_class_index()) || !seen.insert(nd.get_class_index()).second) {
            continue;
          }
          parent.emplace(nd.get_class_index(), std::make_pair(d, e.sink));
          next.push_back(nd);
        }
      }
    }
    frontier = std::move(next);
  }
  return std::nullopt;
}

// Try to break ONE provably inactive cycle in the component. True if rewired.
bool break_false_cycle(hhds::Graph* g, const std::vector<hhds::Pin_class>& members) {
  absl::flat_hash_set<hhds::Class_index> inside;
  for (const auto& p : members) {
    inside.insert(p.get_class_index());
  }
  for (const auto& start : members) {
    const auto sn = start.get_master_node();
    if (gu::type_op_of(sn) != Ntype_op::Mux || gu::inp_sink_drivers(sn).size() != 3) {
      continue;
    }
    const auto cyc = simple_cycle(start, inside);
    if (!cyc) {
      continue;
    }
    // path[k]: hop into node(path[k].drv) via path[k].sink; the LAST hop's drv
    // is `start` and its sink is the cycle arm of start's node.
    std::vector<std::pair<hhds::Pin_class, bool>> conds;
    bool                                          ok = true;
    for (const auto& h : *cyc) {
      const auto n  = h.sink.get_master_node();
      const auto op = gu::type_op_of(n);
      if (op == Ntype_op::Mux) {
        const auto pid = h.sink.get_port_id();
        if (pid == 0 || gu::inp_sink_drivers(n).size() != 3) {
          ok = false;  // through a selector, or not a two-arm mux
          break;
        }
        hhds::Pin_class sel;
        for (const auto& in : gu::inp_sink_drivers(n)) {
          if (in.get_port_id() == 0) {
            sel = in.driver;
          }
        }
        conds.emplace_back(sel, pid == 2);
      } else if (op == Ntype_op::Hotmux || op == Ntype_op::Sub || gu::is_type_register(n)) {
        ok = false;
        break;
      }
    }
    if (!ok || conds.empty()) {
      continue;
    }
    Cond_model m;
    for (const auto& [sel, want] : conds) {
      model_pin(sel, m, 0);
    }
    if (!m.ok || conds_satisfiable(conds, m)) {
      continue;  // possibly active (or not decidable here): a real loop, reported as before
    }
    // UNROLL at start: clone the path from start's output around to the driver
    // of start's cycle arm, with start's output replaced by a constant.
    const auto&     arm_hop = cyc->back();
    hhds::Pin_class prev    = gu::create_const(*g, *Dlop::create_integer(0));
    for (size_t k = 0; k + 1 < cyc->size(); ++k) {
      const auto& h   = (*cyc)[k];
      const auto  n   = h.sink.get_master_node();
      auto        neo = gu::create_typed_node(*g, gu::type_op_of(n));
      for (auto sink : n.inp_sorted_pins()) {
        const auto pid = static_cast<int>(sink.get_port_id());
        for (const auto& drv : sink.get_driver_pins()) {
          (sink.get_class_index() == h.sink.get_class_index() ? prev : drv)
              .connect_sink(gu::setup_sink_pid(neo, pid));
        }
      }
      auto out = neo.create_driver_pin(h.drv.get_port_id());
      gu::set_bits(out, gu::bits_of(h.drv));
      if (gu::is_unsign(h.drv)) {
        gu::set_unsign(out);
      } else {
        gu::set_sign(out);
      }
      prev = out;
    }
    const auto arm_pid = static_cast<int>(arm_hop.sink.get_port_id());
    arm_hop.sink.del_sink();
    prev.connect_sink(gu::setup_sink_pid(sn, arm_pid));
    return true;
  }
  return false;
}

void report_cycles(const std::string& name, const std::vector<std::vector<hhds::Pin_class>>& cycles) {
  for (const auto& members : cycles) {
    std::string hops;
    bool        loop_sub = false;
    for (size_t i = 0; i < members.size(); ++i) {
      const auto m  = members[i].get_master_node();
      loop_sub     |= gu::type_op_of(m) == Ntype_op::Sub && m.is_loop_subnode();
      if (i < 16) {
        hops += (i == 0 ? "" : " -> ") + pin_name(members[i]);
      }
    }
    if (loop_sub) {
      diag::err(kAcyclicPass, "comb-loop-through-loop", "time")
          .msg("combinational loop through a rolled loop instance in '{}': {}{}", name, hops, members.size() > 16 ? " ..." : "")
          .hint("a value computed in one iteration feeds the same loop's inputs combinationally; break it with a register")
          .emit();
    } else {
      diag::err(kAcyclicPass, "comb-loop", "time")
          .msg("combinational loop in '{}': {}{}", name, hops, members.size() > 16 ? " ..." : "")
          .hint("a value depends on itself with no register, latch or registered memory read on the path; insert a register")
          .emit();
    }
  }
}

}  // namespace

Acyclic_result make_acyclic(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Split_state& state, bool defer_inlined_loops,
                            const std::vector<std::shared_ptr<hhds::Graph>>& roots) {
  Acyclic_result                          result;
  Scan_ctx                                ctx;
  absl::flat_hash_set<const hhds::Graph*> instantiated;
  for (const auto& graph : graphs) {
    if (!graph || !graph->get_io()) {
      continue;
    }
    for (auto node : graph->body().nodes()) {
      if (gu::is_type_sub(node)) {
        if (auto callee = node.get_subnode_graph()) {
          instantiated.insert(callee.get());
        }
      }
    }
  }
  for (const auto& root : roots) {
    instantiated.erase(root.get());
  }
  absl::flat_hash_set<const hhds::Graph*>   unresolved;
  std::vector<std::shared_ptr<hhds::Graph>> candidates;
  for (const auto& gp : callee_first(graphs)) {
    auto*                 g    = gp.get();
    const auto            name = std::string{g->get_name()};
    auto&                 done = result.inlined[g];
    std::set<std::string> through_module;
    auto                  found   = scan(g, ctx);
    bool                  changed = false;
    for (int round = 0; round < 64; ++round) {
      std::vector<hhds::Node_class>         subs = found.state_free;
      absl::flat_hash_set<hhds::Node_class> seen(subs.begin(), subs.end());
      // A child's internal candidate need not form an external SCC in its
      // caller. Inline just that child so the caller's bindings can resolve it.
      if (defer_inlined_loops) {
        for (auto node : g->body().nodes()) {
          if (!gu::is_type_sub(node) || node.is_loop_subnode()) {
            continue;
          }
          if (auto callee = node.get_subnode_graph(); callee && unresolved.contains(callee.get()) && seen.insert(node).second) {
            subs.push_back(node);
            through_module.insert(instance_label(node));
          }
        }
      }
      for (const auto& members : found.cycles) {
        for (const auto& p : members) {
          const auto m = p.get_master_node();
          if (gu::is_type_sub(m) && m.get_subnode_graph() && !m.is_loop_subnode()) {
            through_module.insert(instance_label(m));
            if (seen.insert(m).second) {
              subs.push_back(m);
            }
          }
        }
      }
      if (subs.empty()) {
        break;
      }
      std::ranges::sort(subs, [](const auto& a, const auto& b) { return a.get_debug_nid() < b.get_debug_nid(); });
      bool progress = false;
      for (const auto& sub : subs) {
        if (sub.is_invalid()) {
          continue;
        }
        const auto callee = std::string{sub.get_subnode_graph()->get_name()};
        if (gu::inline_sub_instance(g, sub, kAcyclicPass)) {
          done.push_back(callee);
          ++result.instances_inlined;
          progress = true;
        }
      }
      if (!progress) {
        break;
      }
      changed = true;
      found   = scan(g, ctx);
    }
    if (!through_module.empty()) {
      std::string list;
      for (const auto& instance : through_module) {
        list += (list.empty() ? "" : ", ") + instance;
      }
      diag::warn(kAcyclicPass, "comb-loop-through-module", "time")
          .msg("candidate combinational loop in '{}'; inlined {} for constant cleanup", name, list)
          .emit();
    }
    if (defer_inlined_loops && changed) {
      // The caller's configuration can come from another module's constant
      // output (a stateful controller, for example). Expose those literals to
      // the scheduled cleanup without specializing or inlining that module.
      std::vector<hhds::Node_class> instances;
      for (auto node : g->body().nodes()) {
        if (gu::is_type_sub(node) && !node.is_loop_subnode()) {
          instances.push_back(node);
        }
      }
      for (const auto& node : instances) {
        const auto callee = node.get_subnode_graph();
        if (!callee) {
          continue;
        }
        const auto io = callee->get_io();
        for (const auto& port : io->get_output_pin_decls()) {
          const auto output       = node.get_driver_pin(port.port_id);
          const auto child_output = callee->get_output_pin(port.name);
          if (output.is_invalid() || child_output.is_invalid()) {
            continue;
          }
          const auto value = child_output.get_driver_pin();
          if (value.is_invalid() || !value.is_const() || gu::const_of(value).has_unknowns()) {
            continue;
          }
          auto fitted = gu::fit_to_port(*g,
                                        gu::create_const(*g, gu::const_of(value)),
                                        gu::sub_port_width(child_output, *io, port.name),
                                        !io->is_unsign(port.name));
          fitted      = gu::fit_to_port(*g, fitted, gu::sub_port_width(output, *io, port.name), !gu::is_unsign(output));
          const gu::Edge_vec readers(output.out_edges().begin(), output.out_edges().end());
          for (const auto& edge : readers) {
            edge.sink.del_sink(output);
            fitted.connect_sink(edge.sink);
          }
        }
      }
      found = scan(g, ctx);
    }
    // What is left has no inlinable instance on it: packed slices inside this
    // body (false), a real path through a rolled loop, or a true loop. A
    // component the arc view refined away can hide the same packed-slice loop
    // (br_amba_axi_shrinker: inlining its state-free mux left the struct
    // `shift` field feeding the `data` field of the same muxed word, in one
    // component with stateful instances), so split there too; the
    // splitter is a no-op on a body with no word-level cycle.
    if (!found.cycles.empty() || found.refined_away) {
      const int rewired      = gu::split_packed_cycle_slices(g);
      result.slices_rewired += rewired;
      changed               |= rewired != 0;
      found                  = scan(g, ctx);
    }
    // A cycle no input can activate (a hold arm whose selectors are exhaustive
    // across several switches, which lhd_full_case cannot see) is unrolled once
    // and the scan repeated; what is left is deferred or reported.
    const int false_before = result.false_cycles;
    for (int round = 0; round < 4096 && !found.cycles.empty(); ++round) {
      bool broke = false;
      for (const auto& members : found.cycles) {
        if (break_false_cycle(g, members)) {
          ++result.false_cycles;
          broke = true;
        }
      }
      if (!broke) {
        break;
      }
      found = scan(g, ctx);
    }
    if (result.false_cycles > false_before) {
      changed = true;
      diag::info(kAcyclicPass, "false-cycle-unrolled", "progress")
          .msg("unrolled {} combinational cycle(s) in '{}' that no input can activate (a full case's hold arm under "
               "`proc -ifx`)",
               result.false_cycles - false_before,
               name)
          .emit();
    }
    result.loops += static_cast<int>(found.cycles.size());
    if (found.cycles.empty()) {
      continue;
    }
    if (defer_inlined_loops && (changed || instantiated.contains(g))) {
      unresolved.insert(g);
      candidates.push_back(gp);
    } else {
      report_cycles(name, found.cycles);
    }
  }
  if (defer_inlined_loops && !candidates.empty()) {
    // Keep the original roots. A cyclic generic body whose occurrences were
    // all inlined is no longer part of the instantiated design to validate.
    absl::flat_hash_set<const hhds::Graph*>   reachable;
    std::vector<std::shared_ptr<hhds::Graph>> pending;
    for (const auto& graph : graphs) {
      if (graph && graph->get_io() && !instantiated.contains(graph.get())) {
        pending.push_back(graph);
      }
    }
    while (!pending.empty()) {
      auto graph = pending.back();
      pending.pop_back();
      if (!reachable.insert(graph.get()).second) {
        continue;
      }
      for (auto node : graph->body().nodes()) {
        if (gu::is_type_sub(node)) {
          if (auto callee = node.get_subnode_graph()) {
            pending.push_back(callee);
          }
        }
      }
    }
    for (const auto& graph : candidates) {
      if (reachable.contains(graph.get())) {
        if (result.instances_inlined != 0 || result.slices_rewired != 0) {
          result.needs_loop_check = true;
        } else {
          report_cycles(std::string{graph->get_name()}, scan(graph.get(), ctx).cycles);
        }
      } else {
        pending.push_back(graph);
      }
    }
    // Keep unrelated acyclic definitions available. An unused caller of a
    // discarded candidate must go with it, so no surviving Sub dangles.
    absl::flat_hash_map<const hhds::Graph*, std::vector<std::shared_ptr<hhds::Graph>>> callers;
    for (const auto& graph : graphs) {
      if (!graph || !graph->get_io()) {
        continue;
      }
      for (auto node : graph->body().nodes()) {
        if (gu::is_type_sub(node)) {
          if (auto callee = node.get_subnode_graph()) {
            callers[callee.get()].push_back(graph);
          }
        }
      }
    }
    absl::flat_hash_set<const hhds::Graph*> removed;
    while (!pending.empty()) {
      const auto graph = pending.back();
      pending.pop_back();
      if (reachable.contains(graph.get()) || !removed.insert(graph.get()).second) {
        continue;
      }
      state.removed.push_back(graph);
      if (auto it = callers.find(graph.get()); it != callers.end()) {
        pending.insert(pending.end(), it->second.begin(), it->second.end());
      }
    }
  }
  return result;
}

int check_comb_cycles(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  Scan_ctx ctx;
  int      cycles = 0;
  for (const auto& graph : callee_first(graphs)) {
    const auto found  = scan(graph.get(), ctx);
    cycles           += static_cast<int>(found.cycles.size());
    report_cycles(std::string{graph->get_name()}, found.cycles);
  }
  return cycles;
}

int count_comb_cycles(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::vector<std::string>* where) {
  Scan_ctx ctx;
  int      cycles = 0;
  for (const auto& gp : callee_first(graphs)) {
    for (const auto& members : scan(gp.get(), ctx).cycles) {
      ++cycles;
      if (where != nullptr) {
        std::string hops;
        for (size_t i = 0; i < members.size() && i < 16; ++i) {
          hops += (i == 0 ? "" : " -> ") + pin_name(members[i]);
        }
        where->push_back(std::string{gp->get_name()} + ": " + hops + (members.size() > 16 ? " ..." : ""));
      }
    }
  }
  return cycles;
}

}  // namespace livehd::legalize
