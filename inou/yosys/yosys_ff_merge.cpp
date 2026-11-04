//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// lhd_ff_merge: one flop per variable bit when two clocked processes write the same variable.
//
// IEEE 1800 forbids writing a variable from an always_ff and from any other process; for a plain
// `always @(posedge clk)` it is a race (the standard does not say which write is kept). Both yosys front
// ends build ONE flop per process, so the variable ends up with two flops driving the same wire, and the
// importer kept only one of them: the yosys-slang reader lost the first process's writes and the
// yosys-verilog reader the second's (a variable written by `if (a) x <= 5;` in one block and `if (b) x <= 9;`
// in another read 0 9 9 9 / 5 5 5 5 where Verilator, Icarus and the native slang reader give 5 9 5 9).
//
// The rule kept here is the native slang reader's: every write is kept, and in a cycle where several
// processes write, the one later in the source wins. Simulators do not agree on that cycle, and neither
// follows one rule (measured: Icarus kept the earlier write in some designs and the later in others;
// Verilator kept the later one of two writers, except when the earlier one also READ the variable).
//
// Before `proc`, each process's next value for the variable starts from an unconditional "hold" action
// (`assign <next> <variable>`) that the process's own assignments then override. The pass keeps the later
// process's flop and starts its next value from the EARLIER process's next value instead of the hold, then
// drops the earlier flop's copy of those bits. The result is exactly "both processes run, in source order,
// and the later write wins". An explicit `x <= x` in the later process is an assignment, not the hold, so it
// still wins.
//
// Every merge is reported with a warning that names the variable and both processes. A pair the pass cannot
// merge exactly (different clocks or edges, an async reset or load, a clock enable, processes it cannot
// order) stops the read with an error instead of losing one writer silently. Arrays that become memories are
// not touched: their write ports already keep the later process's write (port priority).

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"

#include <algorithm>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "kernel/ff.h"
#include "kernel/newcelltypes.h"
#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#pragma GCC diagnostic pop

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// One clocked process's flop: a flop cell (yosys-slang builds the flop itself) or the edge sync rule of a
// process (yosys-verilog leaves that to proc_dff).
struct Writer {
  RTLIL::Cell*     cell = nullptr;
  RTLIL::Process*  proc = nullptr;
  RTLIL::SyncRule* sync = nullptr;
  std::string      src;           // the process's source location, as the front end recorded it
  std::string      file;          // ordering key: file, line, column of the first location in `src`
  int              line   = -1;
  int              column = -1;
  RTLIL::SigBit    clk;
  bool             pol_clk = true;
  std::string      unmergeable;  // why this flop cannot take part in a merge ("" = it can)
};

// "file.sv:12.3-14.8|..." -> file.sv, 12, 3. False when the location cannot be read.
bool parse_src(const std::string& src, std::string& file, int& line, int& column) {
  const std::string first = src.substr(0, src.find('|'));
  const auto        colon = first.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  file = first.substr(0, colon);
  int l = 0, c = 0;
  if (sscanf(first.c_str() + colon + 1, "%d.%d", &l, &c) != 2) {
    return false;
  }
  line   = l;
  column = c;
  return true;
}

// Unconditional actions of a case tree: the root's own actions, and those of every `switch {}` whose single
// arm has no label (the wrapper read_slang puts around a process's starting values).
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

// Every left-hand side bit any action of the tree assigns, conditional or not.
void assigned_bits(RTLIL::CaseRule* cs, const SigMap& sigmap, pool<RTLIL::SigBit>& out) {
  for (auto& action : cs->actions) {
    for (auto bit : sigmap(action.first)) {
      out.insert(bit);
    }
  }
  for (auto* sw : cs->switches) {
    for (auto* arm : sw->cases) {
      assigned_bits(arm, sigmap, out);
    }
  }
}

struct Merger {
  RTLIL::Module* module;
  SigMap         sigmap;

  std::vector<Writer> writers;
  // variable bit -> (writer, bit offset in that writer's Q / D)
  dict<RTLIL::SigBit, std::vector<std::pair<int, int>>> by_bit;
  // next-value bit -> the unconditional actions (and the bit offset in them) that set it from the variable
  dict<RTLIL::SigBit, std::vector<std::pair<RTLIL::SigSig*, int>>> holds;
  // next-value bits some process assigns (conditionally or not)
  pool<RTLIL::SigBit> proc_assigned;

  explicit Merger(RTLIL::Module* m) : module(m), sigmap(m) {}

  void add_writer_bits(int wi, const RTLIL::SigSpec& q) {
    for (int i = 0; i < GetSize(q); ++i) {
      auto bit = sigmap(q[i]);
      if (bit.wire != nullptr) {
        by_bit[bit].push_back({wi, i});
      }
    }
  }

  void collect() {
    for (auto* cell : module->cells()) {
      if (!StaticCellTypes::categories.is_ff(cell->type)) {
        continue;
      }
      FfData ff(nullptr, cell);
      Writer w;
      w.cell = cell;
      w.src  = cell->get_src_attribute();
      if (!ff.has_clk) {
        continue;  // latches and $ff are not clocked processes
      }
      w.clk     = sigmap(ff.sig_clk[0]);
      w.pol_clk = ff.pol_clk;
      if (ff.has_aload || ff.has_arst || ff.has_sr) {
        w.unmergeable = "it has an asynchronous reset or load";
      } else if (ff.has_ce || ff.has_srst) {
        w.unmergeable = "it has a clock enable or a synchronous reset cell";
      }
      if (!parse_src(w.src, w.file, w.line, w.column)) {
        w.unmergeable = "its source location is unknown";
      }
      writers.push_back(w);
      add_writer_bits(GetSize(writers) - 1, cell->getPort(ID::Q));
    }
    // One writer per process, whatever number of edge events it has: `always @(posedge clk or posedge rst)`
    // updates the same bits from both events and is ONE process.
    for (auto& [proc_name, proc] : module->processes) {
      std::vector<RTLIL::SyncRule*> edges;
      bool                          other = false;
      for (auto* sync : proc->syncs) {
        if (sync->actions.empty()) {
          continue;
        }
        if (sync->type == RTLIL::STp || sync->type == RTLIL::STn) {
          edges.push_back(sync);
        } else if (sync->type != RTLIL::STi) {
          other = true;  // level-sensitive or global-clock updates
        }
      }
      if (edges.empty()) {
        continue;
      }
      Writer w;
      w.proc = proc;
      w.src  = proc->get_src_attribute();
      if (edges.size() == 1 && !other && GetSize(edges[0]->signal) == 1) {
        w.sync    = edges[0];
        w.clk     = sigmap(edges[0]->signal[0]);
        w.pol_clk = edges[0]->type == RTLIL::STp;
      } else {
        w.unmergeable = "it has an asynchronous reset or more than one clock event";
      }
      if (!parse_src(w.src, w.file, w.line, w.column)) {
        w.unmergeable = "its source location is unknown";
      }
      writers.push_back(w);
      const int wi = GetSize(writers) - 1;
      for (auto* sync : edges) {
        for (auto& action : sync->actions) {
          add_writer_bits(wi, action.first);
        }
      }
    }
    for (auto& [proc_name, proc] : module->processes) {
      std::vector<RTLIL::SigSig*> acts;
      unconditional_actions(&proc->root_case, acts);
      for (auto* action : acts) {
        for (int i = 0; i < GetSize(action->first); ++i) {
          holds[sigmap(action->first[i])].push_back({action, i});
        }
      }
      assigned_bits(&proc->root_case, sigmap, proc_assigned);
    }
  }

  // The flop data input bit of writer `wi` for its Q bit `q`.
  RTLIL::SigBit next_bit(int wi, const RTLIL::SigBit& q) const {
    const auto& w = writers[wi];
    if (w.cell) {
      auto qs = w.cell->getPort(ID::Q);
      auto ds = w.cell->getPort(ID::D);
      for (int i = 0; i < GetSize(qs); ++i) {
        if (sigmap(qs[i]) == q) {
          return ds[i];
        }
      }
    } else {
      for (auto& action : w.sync->actions) {
        for (int i = 0; i < GetSize(action.first); ++i) {
          if (sigmap(action.first[i]) == q) {
            return action.second[i];
          }
        }
      }
    }
    log_abort();
  }

  std::string name_of(const pool<RTLIL::SigBit>& bits) const {
    RTLIL::SigSpec s;
    for (auto bit : bits) {
      s.append(bit);
    }
    s.sort_and_unify();
    return log_signal(s);
  }

  int run() {
    collect();
    // Variable bits with two or more writers, grouped by the writers involved (one warning per group).
    std::map<std::vector<int>, pool<RTLIL::SigBit>> groups;
    for (auto& [bit, list] : by_bit) {
      std::vector<int> ws;
      for (auto& entry : list) {
        ws.push_back(entry.first);
      }
      std::sort(ws.begin(), ws.end());
      ws.erase(std::unique(ws.begin(), ws.end()), ws.end());
      if (ws.size() > 1) {
        groups[ws].insert(bit);
      }
    }
    if (groups.empty()) {
      return 0;
    }

    // Source order: the later process wins.
    auto later = [&](int a, int b) {
      return std::tie(writers[a].line, writers[a].column) < std::tie(writers[b].line, writers[b].column);
    };
    // The flop bits each writer gives up (offsets into its Q / sync actions).
    dict<int, pool<RTLIL::SigBit>> drop;
    int                            merged = 0;
    for (auto& [ws_unsorted, bits] : groups) {
      std::vector<int> ws = ws_unsorted;
      std::sort(ws.begin(), ws.end(), later);  // the checks below refuse a writer whose location is unknown
      const std::string var = name_of(bits);
      std::string       places;  // in source order
      for (int wi : ws) {
        places += (places.empty() ? "" : ", ") + (writers[wi].src.empty() ? std::string("<unknown>") : writers[wi].src);
      }
      for (int wi : ws) {
        const auto& w = writers[wi];
        if (!w.unmergeable.empty()) {
          log_error(
              "%s in module %s is written by %d clocked processes (%s), and the process at %s cannot be merged: %s. "
              "IEEE 1800 forbids two processes writing one always_ff variable; write the variable from one process.\n",
              var.c_str(), log_id(module), GetSize(ws), places.c_str(), w.src.c_str(), w.unmergeable.c_str());
        }
        if (w.clk != writers[ws[0]].clk || w.pol_clk != writers[ws[0]].pol_clk || w.clk.wire == nullptr) {
          log_error(
              "%s in module %s is written by clocked processes on different clocks or edges (%s); there is no "
              "single flop that keeps both. Write the variable from one process.\n",
              var.c_str(), log_id(module), places.c_str());
        }
        if (w.file != writers[ws[0]].file) {
          log_error(
              "%s in module %s is written by clocked processes in different files (%s); the reader cannot tell which "
              "comes later. Write the variable from one process.\n",
              var.c_str(), log_id(module), places.c_str());
        }
      }
      for (size_t k = 1; k < ws.size(); ++k) {
        if (std::tie(writers[ws[k - 1]].line, writers[ws[k - 1]].column) == std::tie(writers[ws[k]].line, writers[ws[k]].column)) {
          log_error("%s in module %s is written by two clocked processes at the same source location (%s).\n",
                    var.c_str(), log_id(module), places.c_str());
        }
      }

      for (auto bit : bits) {
        // The writers of this bit, in source order (a group's bits all share the same writers).
        for (size_t k = 1; k < ws.size(); ++k) {
          const int     early = ws[k - 1], late = ws[k];
          RTLIL::SigBit d_early = next_bit(early, bit);
          RTLIL::SigBit d_late  = sigmap(next_bit(late, bit));
          // Start the later process's next value from the earlier one's instead of from the variable: rewrite
          // the unconditional `assign <next> <variable>` (the hold).
          int rewritten = 0;
          if (holds.count(d_late)) {
            for (auto& [action, i] : holds.at(d_late)) {
              if (sigmap(action->second[i]) == bit) {
                action->second.replace(i, RTLIL::SigSpec(d_early));
                ++rewritten;
              }
            }
          }
          // No hold to rewrite is fine in two cases, and in both the later process writes the bit in every
          // cycle, so it always wins and the earlier write is never visible:
          //   * an unconditional action sets the next value from something other than the variable;
          //   * no process assigns it: the flop's data input is a plain expression (`x <= y`).
          // A process that sets it on some paths only, with no unconditional start, would leave the other paths
          // undefined. Neither front end builds that, so it is refused rather than guessed.
          if (rewritten == 0 && !holds.count(d_late) && proc_assigned.count(d_late)) {
            log_error(
                "%s in module %s: the process at %s assigns the variable on some paths only and has no starting value "
                "the reader can redirect, so it cannot be merged with the process at %s.\n",
                var.c_str(), log_id(module), writers[late].src.c_str(), writers[early].src.c_str());
          }
          drop[early].insert(bit);
        }
      }
      log_warning(
          "%s in module %s is written by %d clocked processes (%s). IEEE 1800 forbids this for always_ff (for plain "
          "`always` it is a race). LiveHD keeps every write; in a cycle where more than one process writes, the "
          "process later in the source wins. Simulators may keep a different write in that cycle; the standard "
          "does not say which.\n",
          var.c_str(), log_id(module), GetSize(ws), places.c_str());
      ++merged;
    }

    // Drop the earlier writers' copies. Flop cells are re-emitted without those bits; sync rules lose those updates.
    for (auto& [wi, bits] : drop) {
      auto& w = writers[wi];
      if (w.cell) {
        FfData           ff(nullptr, w.cell);
        std::vector<int> keep;
        for (int i = 0; i < ff.width; ++i) {
          if (!bits.count(sigmap(ff.sig_q[i]))) {
            keep.push_back(i);
          }
        }
        if (GetSize(keep) == ff.width) {
          continue;
        }
        if (!keep.empty()) {
          FfData part = ff.slice(keep);
          part.emit();
        }
        module->remove(w.cell);
        w.cell = nullptr;
      } else {
        for (auto& action : w.sync->actions) {
          RTLIL::SigSpec lhs, rhs;
          for (int i = 0; i < GetSize(action.first); ++i) {
            if (!bits.count(sigmap(action.first[i]))) {
              lhs.append(action.first[i]);
              rhs.append(action.second[i]);
            }
          }
          action.first  = lhs;
          action.second = rhs;
        }
        w.sync->actions.erase(std::remove_if(w.sync->actions.begin(), w.sync->actions.end(),
                                             [](const RTLIL::SigSig& a) { return GetSize(a.first) == 0; }),
                              w.sync->actions.end());
      }
    }
    return merged;
  }
};

struct Lhd_ff_merge_Pass : public Pass {
  Lhd_ff_merge_Pass() : Pass("lhd_ff_merge", "keep one flop when two clocked processes write the same variable") {}

  void help() override {
    log("\n");
    log("    lhd_ff_merge [selection]\n");
    log("\n");
    log("Run before proc. When two or more clocked processes write the same variable bits, keep the flop of the\n");
    log("process that comes last in the source, start its next value from the earlier process's next value\n");
    log("instead of from the variable, and drop the earlier flops' copies of those bits: the later write wins\n");
    log("in a cycle where both write (the native slang reader's rule), and a cycle where only the earlier one\n");
    log("writes keeps its write.\n");
    log("Each merge is a warning naming the variable and the processes; a pair that cannot be merged\n");
    log("exactly (different clocks, async reset/load, clock enable, unknown order) is an error.\n");
    log("\n");
  }

  void execute(std::vector<std::string> args, RTLIL::Design* design) override {
    log_header(design, "Executing LHD_FF_MERGE pass (one flop per variable written by several clocked processes).\n");
    extra_args(args, 1, design);

    int merged = 0;
    for (auto* mod : design->selected_modules()) {
      Merger m(mod);
      merged += m.run();
    }
    log("Merged %d variable(s) written by more than one clocked process.\n", merged);
  }
} Lhd_ff_merge_Pass;

PRIVATE_NAMESPACE_END
