// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "acyclic.hpp"

#include <algorithm>
#include <format>
#include <functional>
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
  Comb_deps(port_reach::Cache& reach, port_reach::Cache& slices) : reach_(reach), slices_(slices) {}

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
  port_reach::Cache&                                             slices_;
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
  // Slice summaries, consulted only for instance outputs a field select reads
  // on a candidate cycle (each def's slices are computed on first such use).
  port_reach::Cache slices{port_reach::Callee_reach{}, /*slices=*/true};
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
};

Scc_scan scan(hhds::Graph* g, Scan_ctx& ctx) {
  Comb_deps                    comb(ctx.reach, ctx.slices);
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

}  // namespace

Acyclic_result make_acyclic(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, [[maybe_unused]] Split_state& state) {  // loops are never split now: nothing is recorded
  Acyclic_result result;
  Scan_ctx       ctx;
  for (const auto& gp : callee_first(graphs)) {
    auto*                 g    = gp.get();
    const auto            name = std::string{g->get_name()};
    auto&                 done = result.inlined[g];
    std::set<std::string> through_module;  // instances inlined off an arc-level cycle
    auto                  found = scan(g, ctx);
    for (int round = 0; round < 64; ++round) {
      // Every non-loop instance with a body on an arc-level cycle is inlined:
      // the path really leaves and re-enters its ports, so its boundary goes.
      std::vector<hhds::Node_class>         subs = found.state_free;
      absl::flat_hash_set<hhds::Node_class> seen(subs.begin(), subs.end());
      for (const auto& members : found.cycles) {
        for (const auto& p : members) {
          const auto m = p.get_master_node();
          if (gu::type_op_of(m) == Ntype_op::Sub && m.get_subnode_graph() && !m.is_loop_subnode() && seen.insert(m).second) {
            subs.push_back(m);
            through_module.insert(instance_label(m));
          }
        }
      }
      if (subs.empty()) {
        break;
      }
      std::ranges::sort(subs, [](const auto& a, const auto& b) { return a.get_debug_nid() < b.get_debug_nid(); });
      bool progress = false;
      for (const auto& s : subs) {
        if (s.is_invalid()) {
          continue;
        }
        const auto callee = std::string{s.get_subnode_graph()->get_name()};
        if (gu::inline_sub_instance(g, s, kAcyclicPass)) {
          done.push_back(callee);
          ++result.instances_inlined;
          progress = true;
        }
      }
      if (!progress) {
        break;
      }
      found = scan(g, ctx);
    }
    if (!through_module.empty()) {
      std::string list;
      for (const auto& s : through_module) {
        list += (list.empty() ? "" : ", ") + s;
      }
      diag::warn(kAcyclicPass, "comb-loop-through-module", "time")
          .msg("combinational path loops through module port(s) in '{}'; inlined {} -- ideally split the bus/module in the RTL",
               name,
               list)
          .emit();
    }
    // What is left has no inlinable instance on it: packed slices inside this
    // body (false), a real path through a rolled loop, or a true loop.
    if (!found.cycles.empty()) {
      result.slices_rewired += gu::split_packed_cycle_slices(g);
      found = scan(g, ctx);
    }
    for (const auto& members : found.cycles) {
      std::string hops;
      bool        loop_sub = false;
      for (size_t i = 0; i < members.size(); ++i) {
        const auto m  = members[i].get_master_node();
        loop_sub     |= gu::type_op_of(m) == Ntype_op::Sub && m.is_loop_subnode();
        if (i < 16) {
          hops += (i == 0 ? "" : " -> ") + pin_name(members[i]);
        }
      }
      ++result.loops;
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
  return result;
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
