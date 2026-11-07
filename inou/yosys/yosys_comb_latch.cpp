//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// lhd_comb_latch: a combinational block that leaves a variable unassigned on some path builds a LATCH.
//
// IEEE 1800: such a variable keeps its value (an `always_comb` that infers a latch; tools should warn), and
// simulators hold it. read_slang --no-proc writes a combinational block as a process that computes a
// temporary `T`, plus `$buf T -> v` for the variable `v`, and it starts the process with an unconditional
// "hold" action `assign T v`. proc builds latches only from a `sync always` update, so it never saw the
// unassigned path: `v` became `v = c ? d : v`, a combinational loop that pass.legalize refused (SoomRV
// SoC.sv:182, `if (!IF_cache.we[NUM_AGUS]) IF_cache.busy[NUM_AGUS] = ...;`; even `always_comb if (!we)
// b = !wr;` failed this way).
//
// For a bit that some path leaves unassigned AND that something else reads, the pass replaces the $buf
// copy with a `sync always` update `v <= T` in the block's process -- read_verilog's form for `always @*`
// -- so proc_dlatch builds the latch and warns ("Latch inferred for signal ..."). Every other bit, and every
// action, is left exactly as it was: a block without a latch, or a hold nothing reads (dead logic today,
// e.g. CVA6 `final_fetch_ppn`, read only in the branch that writes it), produces the same netlist as
// before. The yosys-verilog reader is not affected (yosys itself refuses a latch from always_comb there).

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"

#include <functional>
#include <vector>

#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#pragma GCC diagnostic pop

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

void unconditional_actions(RTLIL::CaseRule* cs, std::vector<RTLIL::SigSig*>& out) {
  for (auto& action : cs->actions) {
    out.push_back(&action);
  }
  for (auto* sw : cs->switches) {
    if (GetSize(sw->signal) == 0 && sw->cases.size() == 1 && sw->cases[0]->compare.empty()) {
      unconditional_actions(sw->cases[0], out);
    }
  }
}

// True when every path through `cs` assigns the temporary bit `t` from something other than its own
// variable `v` (an assignment `t := v` is the hold, not a write). A switch covers all paths only with a
// default arm (no labels) or the full_case attribute.
bool assigned_on_all_paths(RTLIL::CaseRule* cs, const SigMap& sigmap, RTLIL::SigBit t, RTLIL::SigBit v) {
  for (auto& action : cs->actions) {
    for (int i = 0; i < GetSize(action.first); ++i) {
      if (sigmap(action.first[i]) == t && sigmap(action.second[i]) != v) {
        return true;
      }
    }
  }
  for (auto* sw : cs->switches) {
    bool full = sw->get_bool_attribute(ID::full_case);
    for (auto* arm : sw->cases) {
      full = full || arm->compare.empty();
    }
    if (!full || sw->cases.empty()) {
      continue;
    }
    bool all = true;
    for (auto* arm : sw->cases) {
      all = all && assigned_on_all_paths(arm, sigmap, t, v);
    }
    if (all) {
      return true;
    }
  }
  return false;
}

int latch_holds(RTLIL::Module* module) {
  SigMap sigmap(module);
  // variable bit -> the temporary bit a $buf copies into it
  dict<RTLIL::SigBit, RTLIL::SigBit> temp_of;
  for (auto* cell : module->cells()) {
    if (cell->type != ID($buf)) {
      continue;
    }
    auto a = cell->getPort(ID::A);
    auto y = cell->getPort(ID::Y);
    for (int i = 0; i < GetSize(y) && i < GetSize(a); ++i) {
      if (a[i].wire != nullptr && y[i].wire != nullptr) {
        temp_of[sigmap(y[i])] = sigmap(a[i]);
      }
    }
  }
  // Variable bits something reads, other than a hold (`assign T v` that starts v's own block): cell
  // inputs, output ports, and the right sides, switch signals and labels of every process. A hold bit
  // nothing else reads is dead logic today (B04 `final_fetch_ppn`, read only in the branch that writes it,
  // through the block's own value); it stays as it is, so no unused latch appears.
  pool<RTLIL::SigBit> read;
  for (auto* cell : module->cells()) {
    for (auto& [port, sig] : cell->connections()) {
      if (cell->input(port)) {
        for (auto bit : sigmap(sig)) {
          read.insert(bit);
        }
      }
    }
  }
  for (auto* wire : module->wires()) {
    if (wire->port_output) {
      for (auto bit : sigmap(RTLIL::SigSpec(wire))) {
        read.insert(bit);
      }
    }
  }
  std::function<void(RTLIL::CaseRule*)> reads_of = [&](RTLIL::CaseRule* cs) {
    for (auto& action : cs->actions) {
      for (int i = 0; i < GetSize(action.second); ++i) {
        const auto v  = sigmap(action.second[i]);
        const auto it = temp_of.find(v);
        if (it != temp_of.end() && it->second == sigmap(action.first[i])) {
          continue;  // the hold of v's own block
        }
        read.insert(v);
      }
    }
    for (auto* sw : cs->switches) {
      for (auto bit : sigmap(sw->signal)) {
        read.insert(bit);
      }
      for (auto* arm : sw->cases) {
        for (auto& label : arm->compare) {
          for (auto bit : sigmap(label)) {
            read.insert(bit);
          }
        }
        reads_of(arm);
      }
    }
  };
  for (auto& [name, proc] : module->processes) {
    reads_of(&proc->root_case);
    for (auto* sync : proc->syncs) {
      for (auto bit : sigmap(sync->signal)) {
        read.insert(bit);
      }
      for (auto& action : sync->actions) {
        for (auto bit : sigmap(action.second)) {
          read.insert(bit);
        }
      }
    }
  }
  int                 moved_bits = 0;
  pool<RTLIL::SigBit> moved;  // variable bits now updated by a process's `sync always`
  for (auto& [name, proc] : module->processes) {
    if (!proc->syncs.empty()) {
      continue;  // clocked: the hold is the flop's own value
    }
    std::vector<RTLIL::SigSig*> acts;
    unconditional_actions(&proc->root_case, acts);
    RTLIL::SigSpec upd_v, upd_t;
    for (auto* action : acts) {
      // Read only: the action itself is never rewritten (a rebuilt SigSpec, even with the same bits, can
      // change the order later passes create cells in, and so the netlist of a design with no latch).
      for (int i = 0; i < GetSize(action->first); ++i) {
        const auto t  = sigmap(action->first[i]);
        const auto v  = sigmap(action->second[i]);
        const auto it = temp_of.find(v);
        // Only a bit that some path leaves unassigned needs a latch; every other bit keeps today's form, so a
        // block without a latch produces the same netlist as before.
        if (it != temp_of.end() && it->second == t && !moved.count(v) && read.count(v)
            && !assigned_on_all_paths(&proc->root_case, sigmap, t, v)) {
          // `assign T v` where `$buf T -> v`: the hold. Keep it, and let the process update v from T in a
          // `sync always` instead of the $buf: read_verilog's form for `always @*`, which proc_dlatch turns
          // into a latch where the mux tree of T still reaches v (a path that leaves v unassigned).
          ++moved_bits;
          moved.insert(v);
          upd_v.append(action->second[i]);
          upd_t.append(action->first[i]);
        }
      }
    }
    if (GetSize(upd_v) > 0) {
      auto* sync = new RTLIL::SyncRule;
      sync->type = RTLIL::STa;
      sync->actions.push_back(RTLIL::SigSig(upd_v, upd_t));
      proc->syncs.push_back(sync);
    }
  }
  // The moved bits are no longer copied by the $buf (the process's update drives them).
  std::vector<RTLIL::Cell*> bufs;
  for (auto* cell : module->cells()) {
    if (cell->type == ID($buf)) {
      bufs.push_back(cell);
    }
  }
  for (auto* cell : bufs) {
    auto           a = cell->getPort(ID::A);
    auto           y = cell->getPort(ID::Y);
    RTLIL::SigSpec ka, ky;
    for (int i = 0; i < GetSize(y) && i < GetSize(a); ++i) {
      if (!moved.count(sigmap(y[i]))) {
        ka.append(a[i]);
        ky.append(y[i]);
      }
    }
    if (GetSize(ky) == GetSize(y)) {
      continue;
    }
    if (GetSize(ky) == 0) {
      module->remove(cell);
    } else {
      cell->setPort(ID::A, ka);
      cell->setPort(ID::Y, ky);
      cell->setParam(ID::WIDTH, GetSize(ky));
    }
  }
  return moved_bits;
}

struct Lhd_comb_latch_Pass : public Pass {
  Lhd_comb_latch_Pass() : Pass("lhd_comb_latch", "build a latch where a combinational block leaves a variable unassigned") {}

  void help() override {
    log("\n");
    log("    lhd_comb_latch [selection]\n");
    log("\n");
    log("Run after read_slang --no-proc, before proc. For a variable bit that a combinational process leaves\n");
    log("unassigned on some path, and that something else reads, the $buf copy `T -> v` becomes a `sync always`\n");
    log("update of the process, so proc_dlatch builds a latch instead of a combinational loop. Nothing else\n");
    log("changes.\n");
    log("\n");
  }

  void execute(std::vector<std::string> args, RTLIL::Design* design) override {
    log_header(design, "Executing LHD_COMB_LATCH pass (latches of combinational processes).\n");
    extra_args(args, 1, design);
    int moved = 0;
    for (auto* mod : design->selected_modules()) {
      moved += latch_holds(mod);
    }
    log("Moved %d latch bit(s) of combinational processes to a sync-always update.\n", moved);
  }
} Lhd_comb_latch_Pass;

PRIVATE_NAMESPACE_END
