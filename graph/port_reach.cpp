// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "port_reach.hpp"

#include <vector>

#include "cell.hpp"
#include "node_util.hpp"

namespace livehd::port_reach {

namespace gu = livehd::graph_util;

namespace {

// Decompose a packed-output driver into disjoint (lo, leaf) ranges when it is
// the concat idiom a packed struct lowers to: an `Or` tree whose operands are
// `SHL(leaf, const)` (or one bare leaf at offset 0). Returns ranges sorted by
// lo with lens derived from the NEXT offset (the last runs to out_bits); an
// unrecognized shape yields empty (the caller stays at port grain).
// A concat leaf: bit range [lo, lo+len) of the output, driven by `pin`. A len
// of 0 means "runs to the next leaf's lo" (the Or/SHL idiom carries no widths;
// the Set_mask idiom carries exact ones from its masks).
struct Leaf {
  uint32_t        lo  = 0;
  uint32_t        len = 0;
  hhds::Pin_class pin;
};

std::vector<Leaf> concat_leaves(const hhds::Pin_class& drv) {
  std::vector<Leaf>                                 leaves;
  std::vector<std::pair<uint32_t, hhds::Pin_class>> work{{0, drv}};
  int                                               budget = 64;
  while (!work.empty()) {
    if (--budget < 0) {
      return {};
    }
    auto [off, p] = work.back();
    work.pop_back();
    if (p.is_invalid()) {
      return {};
    }
    if (gu::is_graph_input_pin(p) || gu::is_const_pin(p)) {
      leaves.push_back({off, 0, p});
      continue;
    }
    auto       n  = p.get_master_node();
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Or) {
      for (const auto& e : n.inp_edges()) {
        work.emplace_back(off, e.driver);
      }
      continue;
    }
    if (op == Ntype_op::SHL) {
      hhds::Pin_class val, amt;
      for (const auto& e : n.inp_edges()) {
        if (e.sink.get_port_id() == 0) {
          val = e.driver;
        } else {
          amt = e.driver;
        }
      }
      if (val.is_invalid() || amt.is_invalid() || !gu::is_const_pin(amt)) {
        return {};
      }
      auto av = gu::hydrate_const(amt);
      if (!av.is_just_i64() || av.to_just_i64() < 0) {
        return {};
      }
      work.emplace_back(off + static_cast<uint32_t>(av.to_just_i64()), val);
      continue;
    }
    if (op == Ntype_op::Set_mask && off == 0) {
      // The OTHER producer idiom: a struct built field by field lowers as a
      // SET_MASK CHAIN — set_mask(set_mask(base, mask1, v1), mask2, v2)...
      // Each link contributes value bits (LSB-aligned) into its contiguous
      // masked range; the chain terminates at the undriven `0sb?` base (or
      // any other base, which becomes the leaf under everything else).
      hhds::Pin_class base, msk, val;
      for (const auto& e : n.inp_edges()) {
        switch (e.sink.get_port_id()) {
          case 0: base = e.driver; break;
          case 2: msk = e.driver; break;
          case 4: val = e.driver; break;
          default: break;
        }
      }
      if (msk.is_invalid() || val.is_invalid() || !gu::is_const_pin(msk)) {
        return {};
      }
      auto mv = gu::hydrate_const(msk);
      if (mv.has_unknowns() || mv.is_negative()) {
        return {};
      }
      auto [mb, me] = mv.get_mask_range();
      if (mb < 0 || me <= mb) {
        return {};
      }
      leaves.push_back({static_cast<uint32_t>(mb), static_cast<uint32_t>(me - mb), val});
      if (!base.is_invalid() && !gu::is_const_pin(base)) {
        work.emplace_back(0, base);  // keep unwinding the chain
      }
      continue;
    }
    leaves.push_back({off, 0, p});  // any other node: a leaf covering [off, next)
  }
  std::sort(leaves.begin(), leaves.end(), [](const Leaf& a, const Leaf& b) { return a.lo < b.lo; });
  for (size_t i = 1; i < leaves.size(); ++i) {
    const auto& prev = leaves[i - 1];
    if (leaves[i].lo < prev.lo + std::max<uint32_t>(prev.len, 1)) {
      return {};  // overlapping ranges: not the disjoint concat idiom
    }
  }
  return leaves;
}

void add_atom(std::vector<In_atom>& v, uint32_t pid, uint32_t lo, uint32_t len) {
  for (auto& a : v) {
    if (a.pid != pid) {
      continue;
    }
    if (a.len == 0) {
      return;  // whole-port already recorded
    }
    if (len == 0) {
      a.lo  = 0;
      a.len = 0;
      return;
    }
    // merge to the covering hull (per-pid intervals are rare enough)
    const uint32_t hi = std::max(a.lo + a.len, lo + len);
    a.lo              = std::min(a.lo, lo);
    a.len             = hi - a.lo;
    return;
  }
  v.push_back({pid, len == 0 ? 0 : lo, len});
}

}  // namespace

const Def_reach& Cache::of(const std::shared_ptr<hhds::Graph>& g) {
  static const Def_reach kEmpty{};
  if (!g) {
    return kEmpty;
  }
  if (auto it = memo_.find(g.get()); it != memo_.end()) {
    return it->second;
  }
  // A recursive instantiation cannot happen in a sane library (the instance
  // graph is a DAG); if it does, answer "no dependence" for the inner query
  // rather than recursing forever — the outer summary then under-reports,
  // which a consumer treats as schedulable-earlier and the emitter's own
  // cycle diagnostics still catch anything genuinely unordered.
  if (!busy_.insert(g.get()).second) {
    return kEmpty;
  }

  Def_reach r;
  auto      io = g->get_io();
  if (io) {
    // ONE walk engine for both grains. It traverses exactly like the original
    // port-level walk (state cuts, port-accurate memories, Sub summary
    // splices) and reports every reached input as an ATOM — refined to a bit
    // range when the last hop is the `Get_mask(input, contiguous-const)` slice
    // read a packed field access lowers to, whole-port otherwise.
    // input port pid -> declared bits (bounds the open-ended SRA slice reads)
    absl::flat_hash_map<uint32_t, uint32_t> in_bits;
    for (const auto& d : io->get_input_pin_decls()) {
      in_bits[static_cast<uint32_t>(d.port_id)] = d.bits > 0 ? static_cast<uint32_t>(d.bits) : 1;
    }
    // The slice-read idioms a packed field access lowers to, rooted DIRECTLY
    // at a graph input: Get_mask(in, contiguous), SRA(in, k), and
    // And(SRA(in, k) | in, low-mask). Returns true when an atom was recorded.
    auto input_atom_of = [&](const hhds::Pin_class& d, std::vector<In_atom>& atoms) -> bool {
      // Peel the identity wrappers tolg puts on a typed port read (unary
      // Get_mask, the to-positive `mask == -1` idiom, Sext) so the idioms
      // below see the input itself.
      auto peel_ident = [&](hhds::Pin_class p) -> hhds::Pin_class {
        for (int hops = 0; hops < 8 && !p.is_invalid(); ++hops) {
          if (gu::is_graph_input_pin(p) || gu::is_const_pin(p)) {
            break;
          }
          auto       n  = p.get_master_node();
          const auto op = gu::type_op_of(n);
          if (op == Ntype_op::Sext) {
            p = gu::first_value_driver(n);
            continue;
          }
          if (op == Ntype_op::Get_mask) {
            hhds::Pin_class val, msk;
            for (const auto& e : n.inp_edges()) {
              if (e.sink.get_port_id() == 0) {
                val = e.driver;
              } else {
                msk = e.driver;
              }
            }
            if (msk.is_invalid()) {
              p = val;  // unary width adjust
              continue;
            }
            if (gu::is_const_pin(msk)) {
              auto mv = gu::hydrate_const(msk);
              if (mv.is_just_i64() && mv.to_just_i64() == -1) {
                p = val;  // to-positive idiom
                continue;
              }
            }
            break;
          }
          break;
        }
        return p;
      };
      auto sra_of = [&](const hhds::Pin_class& p_in, uint32_t* pid, uint32_t* k) -> bool {
        auto p = peel_ident(p_in);
        if (p.is_invalid() || gu::is_const_pin(p)) {
          return false;
        }
        if (gu::is_graph_input_pin(p)) {
          *pid = static_cast<uint32_t>(p.get_port_id());
          *k   = 0;
          return true;
        }
        auto n = p.get_master_node();
        if (gu::type_op_of(n) != Ntype_op::SRA) {
          return false;
        }
        hhds::Pin_class val, amt;
        for (const auto& e : n.inp_edges()) {
          if (e.sink.get_port_id() == 0) {
            val = e.driver;
          } else {
            amt = e.driver;
          }
        }
        val = peel_ident(val);
        if (val.is_invalid() || amt.is_invalid() || !gu::is_const_pin(amt) || !gu::is_graph_input_pin(val)) {
          return false;
        }
        auto av = gu::hydrate_const(amt);
        if (!av.is_just_i64() || av.to_just_i64() < 0) {
          return false;
        }
        *pid = static_cast<uint32_t>(val.get_port_id());
        *k   = static_cast<uint32_t>(av.to_just_i64());
        return true;
      };
      if (d.is_invalid() || gu::is_const_pin(d) || gu::is_graph_input_pin(d)) {
        return false;
      }
      auto       n  = d.get_master_node();
      const auto op = gu::type_op_of(n);
      uint32_t   pid = 0, k = 0;
      if (op == Ntype_op::Get_mask) {
        hhds::Pin_class val, msk;
        for (const auto& e : n.inp_edges()) {
          if (e.sink.get_port_id() == 0) {
            val = e.driver;
          } else {
            msk = e.driver;
          }
        }
        val = peel_ident(val);
        if (!val.is_invalid() && gu::is_graph_input_pin(val) && !msk.is_invalid() && gu::is_const_pin(msk)) {
          auto mv = gu::hydrate_const(msk);
          if (!mv.has_unknowns() && !mv.is_negative()) {
            auto [mb, me] = mv.get_mask_range();
            if (mb >= 0 && me > mb) {
              add_atom(atoms, static_cast<uint32_t>(val.get_port_id()), static_cast<uint32_t>(mb),
                       static_cast<uint32_t>(me - mb));
              return true;
            }
          }
        }
        return false;
      }
      if (op == Ntype_op::SRA && sra_of(d, &pid, &k)) {
        const uint32_t w = in_bits.contains(pid) ? in_bits[pid] : 0;
        // `w <= k` means the declared width does not bound the read (an
        // unknown width, or a shift past the end). Fall back to the WHOLE
        // port: a 1-bit atom would UNDER-report the support, and a consumer
        // fills the callee's input from the atoms alone — every unlisted bit
        // reads 0 in the generated code.
        if (w <= k) {
          add_atom(atoms, pid, 0, 0);
        } else {
          add_atom(atoms, pid, k, w - k);
        }
        return true;
      }
      if (op == Ntype_op::And) {
        hhds::Pin_class other, msk;
        int             cnt = 0;
        for (const auto& e : n.inp_edges()) {
          ++cnt;
          if (gu::is_const_pin(e.driver)) {
            msk = e.driver;
          } else {
            other = e.driver;
          }
        }
        if (cnt != 2 || msk.is_invalid() || other.is_invalid()) {
          return false;
        }
        auto mv = gu::hydrate_const(msk);
        if (mv.has_unknowns() || mv.is_negative()) {
          return false;
        }
        auto [mb, me] = mv.get_mask_range();
        if (mb != 0 || me <= 0) {
          return false;  // only a LOW mask trims a slice read; anything else is data
        }
        if (sra_of(other, &pid, &k)) {
          const uint32_t w = in_bits.contains(pid) ? in_bits[pid] : 0;
          if (w <= k) {
            add_atom(atoms, pid, 0, 0);  // unbounded read: the whole port (see the SRA arm above)
          } else {
            add_atom(atoms, pid, k, std::min<uint32_t>(static_cast<uint32_t>(me), w - k));
          }
          return true;
        }
        return false;
      }
      return false;
    };
    auto run_walk = [&](const std::vector<hhds::Pin_class>& seeds, std::vector<In_atom>& atoms) {
      absl::flat_hash_set<hhds::Class_index> seen_pins;
      absl::flat_hash_set<hhds::Node_class>  expanded;
      std::vector<hhds::Pin_class>           stk = seeds;
      while (!stk.empty()) {
        auto d = stk.back();
        stk.pop_back();
        if (d.is_invalid() || gu::is_const_pin(d)) {
          continue;
        }
        if (gu::is_graph_input_pin(d)) {
          add_atom(atoms, static_cast<uint32_t>(d.get_port_id()), 0, 0);
          continue;
        }
        if (!seen_pins.insert(d.get_class_index()).second) {
          continue;
        }
        if (input_atom_of(d, atoms)) {
          continue;  // a recognized slice read of an input: recorded as a range atom
        }
        auto       m  = d.get_master_node();
        const auto op = gu::type_op_of(m);
        if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
          continue;  // true state boundary: q is last period's value
        }
        if (op == Ntype_op::Get_mask) {
          // The packed-field read idiom: Get_mask(x, contiguous const) is
          // "(x >> mb) truncated" — when x is DIRECTLY a graph input, record
          // the bit range instead of the whole port. Any other shape falls
          // through to plain traversal.
          hhds::Pin_class val, msk;
          for (const auto& e : m.inp_edges()) {
            if (e.sink.get_port_id() == 0) {
              val = e.driver;
            } else {
              msk = e.driver;
            }
          }
          if (!val.is_invalid() && gu::is_graph_input_pin(val) && !msk.is_invalid() && gu::is_const_pin(msk)) {
            auto mv = gu::hydrate_const(msk);
            if (!mv.has_unknowns() && !mv.is_negative()) {
              auto [mb, me] = mv.get_mask_range();  // half-open; {-1,-1} = noncontiguous
              if (mb >= 0 && me > mb) {
                add_atom(atoms, static_cast<uint32_t>(val.get_port_id()), static_cast<uint32_t>(mb),
                         static_cast<uint32_t>(me - mb));
                continue;
              }
            }
          }
          if (expanded.insert(m).second) {
            for (const auto& e : m.inp_edges()) {
              stk.push_back(e.driver);
            }
          }
          continue;
        }
        if (op == Ntype_op::Sub) {
          auto cg = m.get_subnode_graph();
          if (!cg) {
            if (expanded.insert(m).second) {
              for (const auto& e : m.inp_edges()) {
                stk.push_back(e.driver);  // body-less blackbox: depend on everything connected
              }
            }
            continue;
          }
          const auto& cr = of(cg);  // memoized; hierarchy is a DAG
          if (auto it = cr.out2ins.find(static_cast<uint32_t>(d.get_port_id())); it != cr.out2ins.end()) {
            for (const uint32_t ipid : it->second) {
              for (const auto& e : m.inp_edges()) {
                if (static_cast<uint32_t>(e.sink.get_port_id()) == ipid) {
                  stk.push_back(e.driver);
                }
              }
            }
          }
          continue;
        }
        if (op == Ntype_op::Memory) {
          // PORT-ACCURATE traversal (see the header): async dout -> that read
          // port's address/enable cone (+ write cones only under same-cycle
          // forwarding); sync dout -> a register; read_all -> boundary when
          // clocked. Undecoded shapes fall back to the blanket join.
          const auto want_pid = static_cast<hhds::Port_id>(d.get_port_id());
          struct MP {
            hhds::Pin_class addr, en, din;
            bool            rd = false;
          };
          std::vector<MP>              pv;
          std::vector<hhds::Pin_class> wr_cones;
          hhds::Pin_class              update;
          bool                         has_clock   = false;
          bool                         fwd_nonzero = false;
          int                          mtype       = 2;
          for (const auto& e : m.inp_edges()) {
            const int  raw = static_cast<int>(e.sink.get_port_id());
            const auto pn  = Ntype::get_sink_name(Ntype_op::Memory, raw);
            const auto idx = static_cast<size_t>(raw) / Ntype::Memory_port_stride;
            if (pn == "fwd" || pn == "undef") {
              if (gu::is_const_pin(e.driver)) {
                auto c = gu::hydrate_const(e.driver);
                if (!(c.is_just_i64() && c.to_just_i64() == 0)) {
                  fwd_nonzero = true;
                }
              } else {
                fwd_nonzero = true;
              }
            } else if (pn == "type") {
              if (gu::is_const_pin(e.driver)) {
                mtype = static_cast<int>(gu::hydrate_const(e.driver).to_just_i64());
              }
            } else if (pn == "update") {
              update = e.driver;
            } else if (pn == "update_enable" || pn == "reset" || pn == "init" || pn == "bits" || pn == "size"
                       || pn == "wensize") {
            } else if (pn.ends_with("clock_pin")) {
              has_clock = true;
            } else {
              if (pv.size() <= idx) {
                pv.resize(idx + 1);
              }
              if (pn.ends_with("addr")) {
                pv[idx].addr = e.driver;
              } else if (pn.ends_with("enable")) {
                pv[idx].en = e.driver;
              } else if (pn.ends_with("din")) {
                pv[idx].din = e.driver;
              } else if (pn.ends_with("rdport")) {
                pv[idx].rd = gu::is_const_pin(e.driver) && !gu::hydrate_const(e.driver).is_known_false();
              }
            }
          }
          int n_wr = 0;
          for (const auto& mp : pv) {
            if (!mp.addr.is_invalid() && !mp.rd) {
              ++n_wr;
              wr_cones.push_back(mp.addr);
              wr_cones.push_back(mp.din);
              wr_cones.push_back(mp.en);
            }
          }
          bool handled = false;
          if (want_pid == Ntype::Memory_readall_pid) {
            handled = true;
            if (!has_clock) {
              stk.push_back(update);
              for (const auto& w : wr_cones) {
                stk.push_back(w);
              }
            }
          } else {
            int rd = 0;
            for (const auto& mp : pv) {
              if (mp.addr.is_invalid() && mp.din.is_invalid() && mp.en.is_invalid()) {
                continue;  // phantom slot — mirror cgen_sim
              }
              if (!mp.rd) {
                continue;
              }
              if (static_cast<hhds::Port_id>(n_wr + rd) == want_pid) {
                handled = true;
                if (mtype != 1) {
                  stk.push_back(mp.addr);
                  stk.push_back(mp.en);
                  // Write cones flow into a SAME-CYCLE read in two cases:
                  // explicit forwarding, or an UNCLOCKED memory — a pure comb
                  // array is a mux tree, its contents are current-cycle
                  // functions of update/din (the readall arm below already
                  // applies the !has_clock rule; dropping it here silently
                  // zeroed a split callee's whole-array data input —
                  // tests/sim/whole_array_in_split_callee.prp).
                  if (fwd_nonzero || !has_clock) {
                    stk.push_back(update);
                    for (const auto& w : wr_cones) {
                      stk.push_back(w);
                    }
                  }
                }
                break;
              }
              ++rd;
            }
          }
          if (!handled && expanded.insert(m).second) {
            for (const auto& e : m.inp_edges()) {
              stk.push_back(e.driver);
            }
          }
          continue;
        }
        if (expanded.insert(m).second) {
          for (const auto& e : m.inp_edges()) {
            stk.push_back(e.driver);
          }
        }
      }
    };

    for (const auto& od : io->get_output_pin_decls()) {
      auto  opin = g->get_output_pin(od.name);
      auto& ins  = r.out2ins[static_cast<uint32_t>(od.port_id)];
      if (opin.is_invalid()) {
        continue;
      }
      hhds::Pin_class drv;
      for (const auto& e : opin.get_master_node().inp_edges()) {
        if (e.sink.get_port_id() == opin.get_port_id()) {
          drv = e.driver;
          break;
        }
      }
      if (drv.is_invalid()) {
        continue;
      }
      std::vector<In_atom> atoms;
      run_walk({drv}, atoms);
      for (const auto& a : atoms) {
        ins.insert(a.pid);
      }

      // ---- SLICE decomposition (bit-level refinement, used only under loop
      // pressure downstream: slices with identical supports merge back).
      const auto out_bits = static_cast<uint32_t>(od.bits > 0 ? od.bits : 0);
      if (out_bits == 0) {
        continue;
      }
      auto leaves = concat_leaves(drv);
      if (leaves.size() == 1 && !leaves[0].pin.is_invalid() && !gu::is_graph_input_pin(leaves[0].pin)
          && !gu::is_const_pin(leaves[0].pin)
          && gu::type_op_of(leaves[0].pin.get_master_node()) == Ntype_op::Sub) {
        // PASS-THROUGH of a sliced callee output (minion's intpipe_top wraps
        // the CSR file's bundle): adopt the callee's slices, composing each
        // support atom through this instance's input wiring.
        auto        sn = leaves[0].pin.get_master_node();
        auto        cg = sn.get_subnode_graph();
        const auto& cr = of(cg);
        if (auto it = cr.out_slices.find(static_cast<uint32_t>(leaves[0].pin.get_port_id()));
            cg && it != cr.out_slices.end()) {
          std::vector<Out_slice> mine;
          bool                   ok = true;
          for (const auto& cs : it->second) {
            Out_slice ps;
            ps.lo      = cs.lo;
            ps.len     = cs.len;
            ps.leaf    = leaves[0].pin;
            ps.shifted = true;  // the leaf is the callee's whole bundle pin
            for (const auto& ca : cs.ins) {
              hhds::Pin_class idrv;
              for (const auto& e : sn.inp_edges()) {
                if (static_cast<uint32_t>(e.sink.get_port_id()) == ca.pid) {
                  idrv = e.driver;
                  break;
                }
              }
              if (idrv.is_invalid()) {
                continue;  // unconnected input: reads as 0
              }
              if (gu::is_graph_input_pin(idrv)) {
                add_atom(ps.ins, static_cast<uint32_t>(idrv.get_port_id()), ca.lo, ca.len);
              } else {
                std::vector<In_atom> sub_atoms;
                run_walk({idrv}, sub_atoms);
                for (const auto& a : sub_atoms) {
                  add_atom(ps.ins, a.pid, a.lo, a.len);
                }
              }
            }
            std::sort(ps.ins.begin(), ps.ins.end(),
                      [](const In_atom& a, const In_atom& b) { return std::tie(a.pid, a.lo, a.len) < std::tie(b.pid, b.lo, b.len); });
            mine.push_back(std::move(ps));
          }
          if (ok && mine.size() > 1) {
            r.out_slices.emplace(static_cast<uint32_t>(od.port_id), std::move(mine));
          }
        }
        continue;
      }
      if (leaves.size() < 2) {
        continue;  // no decomposition: the port-level row stands alone
      }
      std::vector<Out_slice> slices;
      bool                   ok      = true;
      uint32_t               covered = 0;  // the decomposition must TILE [0, out_bits)
      for (size_t k = 0; k < leaves.size(); ++k) {
        const uint32_t lo = leaves[k].lo;
        const uint32_t hi = leaves[k].len != 0 ? std::min(lo + leaves[k].len, out_bits)
                                               : ((k + 1 < leaves.size()) ? leaves[k + 1].lo : out_bits);
        if (hi <= lo || lo >= out_bits) {
          ok = false;
          break;
        }
        // A GAP is fatal, not ignorable. Set_mask leaves carry exact lengths
        // and the chain's CONSTANT base is dropped above, so a partially
        // constant bundle decomposes into slices that leave real bits
        // uncovered — and the consumer assembles the port by OR-ing the slice
        // exports into a ZERO accumulator, silently reading those bits as 0.
        // Refusing the decomposition falls back to the whole-port row, which
        // is always correct (just coarser).
        if (lo != covered) {
          ok = false;
          break;
        }
        covered = hi;
        Out_slice sl;
        sl.lo   = lo;
        sl.len  = hi - lo;
        sl.leaf = leaves[k].pin;
        run_walk({sl.leaf}, sl.ins);
        std::sort(sl.ins.begin(), sl.ins.end(),
                  [](const In_atom& a, const In_atom& b) { return std::tie(a.pid, a.lo, a.len) < std::tie(b.pid, b.lo, b.len); });
        slices.push_back(std::move(sl));
      }
      if (ok && covered != out_bits) {
        ok = false;  // a trailing gap: the top bits belong to no slice
      }
      if (ok && slices.size() > 1) {
        r.out_slices.emplace(static_cast<uint32_t>(od.port_id), std::move(slices));
      }
    }
  }
  busy_.erase(g.get());
  return memo_.emplace(g.get(), std::move(r)).first->second;
}

}  // namespace livehd::port_reach
