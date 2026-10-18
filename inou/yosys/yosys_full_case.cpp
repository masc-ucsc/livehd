//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// lhd_full_case: mark every RTLIL `case` that enumerates all values of a narrow selector as `full_case`.
//
// read_slang --no-proc leaves a combinational block's incomplete-assignment fallback as an explicit "hold":
// `always @(*) case (s) 2'b00: r = a; ... 2'b11: r = d; endcase` starts from `r = r`, and `proc_mux` builds
// the unmatched path of the case from that hold value even when the labels already cover every selector
// value. The unreachable path is a combinational loop through the mux, which pass.legalize rejects as
// `comb-loop` (Vortex VX_fp_rounding / VX_alu_int). `proc_mux` already ignores the incoming value of every
// bit that is assigned in all arms of a switch carrying the `full_case` attribute, so this pass only has to
// prove the coverage: it sets the attribute (and drops the dead `default` arm) when the constant labels
// span the whole selector space. Selectors with few variable bits only: the proof is an exact enumeration,
// so it is cheap and cannot blow up the way the cube-based `proc_rmdead` does on wide casex/casez decoders.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"

#include <cstdint>
#include <string>
#include <vector>

#include "kernel/yosys.h"

#pragma GCC diagnostic pop

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// 2^12 selector values is far more than any handwritten enumeration needs, and bounds the work per switch.
constexpr int kMaxSelectorBits = 12;

// True when the constant labels (0, 1 and `-` don't-care bits only) of `sw` cover every value the selector
// can take. A selector wider than its variable part is common: SystemVerilog sizes the labels of
// `case (op[2:0]) 0,1,2: ...` to 32 bits, so the selector is {29'b0, op[2:0]} and only its non-constant
// bits (counted once each) span the value space. A label with an x/z bit matches only by `===` under
// proc -ifx, and a non-constant label is data dependent; neither contributes to the proof, so a switch
// relying on them stays as it was.
bool labels_cover_selector(const RTLIL::SwitchRule* sw) {
  const int width = GetSize(sw->signal);
  if (width <= 0) {
    return false;
  }
  // Distinct variable bits of the selector; `index[i]` is the variable of selector bit i, or -1 for a
  // constant bit, whose value is in `constant[i]`.
  dict<RTLIL::SigBit, int> variable;
  std::vector<int>         index(static_cast<size_t>(width), -1);
  std::vector<bool>        constant(static_cast<size_t>(width), false);
  for (int i = 0; i < width; ++i) {
    const RTLIL::SigBit bit = sw->signal[i];
    if (bit.wire == nullptr) {
      if (bit.data != RTLIL::State::S0 && bit.data != RTLIL::State::S1) {
        return false;
      }
      constant[static_cast<size_t>(i)] = bit.data == RTLIL::State::S1;
      continue;
    }
    const int next                = GetSize(variable);
    index[static_cast<size_t>(i)] = variable.emplace(bit, next).first->second;
  }
  const int nvars = GetSize(variable);
  if (nvars <= 0 || nvars > kMaxSelectorBits) {
    return false;
  }
  const size_t         total = size_t{1} << nvars;
  std::vector<uint8_t> seen(total, 0);
  size_t               covered = 0;
  for (const auto* cs : sw->cases) {
    for (const auto& label : cs->compare) {
      if (GetSize(label) != width) {
        continue;
      }
      uint32_t fixed_mask  = 0;  // variables the label pins
      uint32_t fixed_value = 0;  // and the value it pins them to
      bool     matches     = true;
      for (int i = 0; i < width && matches; ++i) {
        const RTLIL::SigBit bit = label[i];
        if (bit.wire != nullptr || (bit.data != RTLIL::State::S0 && bit.data != RTLIL::State::S1 && bit.data != RTLIL::State::Sa)) {
          matches = false;  // not an exact 0/1/- label: no coverage claimed
          break;
        }
        if (bit.data == RTLIL::State::Sa) {
          continue;
        }
        const bool want = bit.data == RTLIL::State::S1;
        const int  var  = index[static_cast<size_t>(i)];
        if (var < 0) {
          matches = constant[static_cast<size_t>(i)] == want;  // a constant selector bit can differ: label never fires
          continue;
        }
        const uint32_t m = uint32_t{1} << var;
        if ((fixed_mask & m) != 0 && ((fixed_value & m) != 0) != want) {
          matches = false;  // the same selector bit pinned to both values
        }
        fixed_mask |= m;
        fixed_value = want ? (fixed_value | m) : (fixed_value & ~m);
      }
      if (!matches) {
        continue;
      }
      // Every assignment of the variables the label leaves free is one covered selector value.
      const uint32_t all  = static_cast<uint32_t>(total - 1);
      const uint32_t wild = all & ~fixed_mask;
      uint32_t       sub  = wild;
      for (;;) {
        const size_t value = fixed_value | sub;
        if (seen[value] == 0) {
          seen[value] = 1;
          ++covered;
        }
        if (sub == 0) {
          break;
        }
        sub = (sub - 1) & wild;
      }
    }
  }
  return covered == total;
}

int mark_full_cases(RTLIL::CaseRule* root) {
  int marked = 0;
  for (auto* sw : root->switches) {
    for (auto* arm : sw->cases) {
      marked += mark_full_cases(arm);
    }
    if (sw->get_bool_attribute(ID::full_case) || !labels_cover_selector(sw)) {
      continue;
    }
    // A `default` arm after complete labels never matches. Left in place it would also stop proc_mux from
    // seeing the switch as assigning every bit in every arm (it intersects over all arms).
    for (size_t i = 0; i < sw->cases.size();) {
      if (sw->cases[i]->compare.empty()) {
        delete sw->cases[i];
        sw->cases.erase(sw->cases.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
    sw->set_bool_attribute(ID::full_case);
    ++marked;
  }
  return marked;
}

struct Lhd_full_case_Pass : public Pass {
  Lhd_full_case_Pass() : Pass("lhd_full_case", "mark case statements that enumerate every selector value as full_case") {}

  void help() override {
    log("\n");
    log("    lhd_full_case [selection]\n");
    log("\n");
    log("Set the full_case attribute on every process switch whose constant labels cover all values of a\n");
    log("selector with at most %d variable bits, and drop its (unreachable) default arm. Run before proc_mux so the\n", kMaxSelectorBits);
    log("hold value of an incompletely written combinational block is not wired into the unmatched path of a\n");
    log("case that has none.\n");
    log("\n");
  }

  void execute(std::vector<std::string> args, RTLIL::Design* design) override {
    log_header(design, "Executing LHD_FULL_CASE pass (mark exhaustive case statements).\n");
    extra_args(args, 1, design);

    int marked = 0;
    for (auto* mod : design->selected_modules()) {
      for (auto* proc : mod->selected_processes()) {
        marked += mark_full_cases(&proc->root_case);
      }
    }
    log("Marked %d exhaustive case statement(s) as full_case.\n", marked);
  }
} Lhd_full_case_Pass;

PRIVATE_NAMESPACE_END
