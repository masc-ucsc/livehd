// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace livehd::usyn {

using Id = uint32_t;

// The admission bound on a truth table: its inputs.
inline constexpr uint32_t max_logical_inputs = 12;

// The table's bit i is f(x), with x[j] = (i >> j) & 1. Storage is dynamic; the
// bounded enumerator admits at most `max_logical_inputs` logical inputs.
struct Truth_table {
  uint32_t              inputs = 0;
  std::vector<uint64_t> words;
  explicit Truth_table(uint32_t n = 0, bool value = false);
  bool        get(uint32_t assignment) const;
  void        set(uint32_t assignment, bool value);
  Truth_table complement() const;
  bool        operator==(const Truth_table&) const = default;
};

struct Cube {
  uint32_t care                          = 0;
  uint32_t ones                          = 0;
  bool     operator==(const Cube&) const = default;
};

enum class Status { feasible, search_exhausted, unsupported, invalid };
const char* status_name(Status status);

// Deterministic work budget for prime enumeration and selection. Exhaustion is
// never an infeasibility certificate.
struct Budget {
  uint64_t              remaining;
  bool                  exhausted = false;
  std::function<bool()> admission{};
  uint64_t              checkpoint_work    = 0;
  bool                  resource_exhausted = false;
  // Work between admission checks. A check samples the process (a syscall), so
  // it stays off the per-cut path; tests shorten it to hit a cancellation.
  uint64_t              admission_interval = 262144;
  bool                  spend(uint64_t amount = 1);
};

struct Form {
  Status            status = Status::search_exhausted;
  std::vector<Cube> cubes;
  uint32_t          literals = 0;
  uint32_t          series   = 0;
  uint32_t          positive = 0;
  uint32_t          negative = 0;
  // Literals of a greedy algebraic factoring of `cubes` (a series-parallel
  // pull-down network); 0 unless exact_form computed it.
  uint32_t          factored = 0;
};

// Construct a total SOP completion. Both signs denote real rail demand, never
// an implicit free inverter. max_series limits the constructed form only.
// The care overload chooses a total completion; its caller must independently
// prove that this care mask covers all reachable input assignments.
Form make_form(const Truth_table& table, uint32_t max_literals, uint32_t max_series, Budget& budget);
Form make_form(const Truth_table& table, const Truth_table& care, uint32_t max_literals, uint32_t max_series, Budget& budget);
bool check_form(const Truth_table& table, const Form& form, Budget& budget);
// Exact minimum-literal SOP (ties: fewer cubes) of a total function of at most
// 8 inputs, over primes of at most max_series literals. `factored` also counts
// the literals of an algebraic factoring of that SOP -- the transistors of a
// series-parallel pull-down network -- and checks max_literals against that
// count instead of the SOP's. A wider table falls back to make_form.
Form exact_form(const Truth_table& table, uint32_t max_literals, uint32_t max_series, bool factored, Budget& budget);
// Literals of a greedy algebraic factoring (most shared literal first).
uint32_t factor_literals(const std::vector<Cube>& cubes);
bool check_form(const Truth_table& table, const Truth_table& care, const Form& form, Budget& budget);

// The limits of one domino gate: its inputs (support), the literals of its
// factored pull-down network, and the literals of one product term (series).
struct Recipe {
  uint32_t support                         = 6;
  uint32_t literals                        = 16;
  uint32_t series                          = 4;
  bool     operator==(const Recipe&) const = default;
};
// Transistor proxies of the LUT cover. Inverters on
// inputs and outputs are free: every signal is available in both polarities.
// A domino gate costs its factored pull-down literals plus a fixed overhead
// (precharge, foot, keeper, output inverter); a function no domino gate builds
// is a static CMOS LUT: cmos_factor transistors per factored literal (pull-up
// and pull-down) plus an output inverter, all times the non-unate penalty.
struct Cover_cost {
  uint32_t domino_overhead                     = 5;
  uint32_t cmos_factor                         = 2;
  uint32_t static_overhead                     = 2;
  uint32_t nonunate_penalty                    = 2;
  bool     operator==(const Cover_cost&) const = default;
};
struct Search_options {
  Recipe                recipe{};
  uint32_t              max_nodes           = 100000;
  uint32_t              recovery_rounds     = 2;  // exact-area recovery sweeps; 0 disables
  // Cover (lut_cover.hpp). cover_cuts: priority cuts kept per node.
  // domino_levels: an output that some cover builds in at most this many
  // chained domino gates must be built so (a half-cycle each: 2 = one cycle);
  // deeper outputs only minimize transistors. 0 ignores depth.
  Cover_cost            cover_cost{};
  uint32_t              cover_cuts          = 12;
  uint32_t              domino_levels       = 2;
  // depth_slack >= 0: every deeper output is also required at its minimum
  // domino depth plus this many levels (depth-optimal, then area recovery);
  // -1 leaves deeper outputs unconstrained (transistors only).
  int32_t               depth_slack         = -1;
  // A node with at least this many sinks is always a LUT boundary: consumers
  // read it as a leaf and never replicate its logic. 0 lets the cost decide.
  uint32_t              fanout_boundary     = 0;
  // false: a gate may absorb a node with several readers only when all its
  // readers are inside that gate, so no logic is ever computed twice
  // (reconvergent fan-out is absorbed; other multi-reader nodes are gate outputs).
  bool                  duplicate           = true;
  // false: a blasted memory region (a register file) is left to the pass.abc
  // flow, as the domino plan keeps memories as separate wares.
  bool                  cover_memories      = true;
  // How the cover reaches ABC. tmap: the whole cover network, each gate as its
  // minimum SOP, technology-mapped only (`&nf`). opt: that network through the
  // full pass.abc optimize-and-map flow. only: no cover -- the original region
  // logic through the pass.abc flow (same partition as the other modes).
  enum class Abc_mode { tmap, opt, only };
  Abc_mode              abc_mode            = Abc_mode::tmap;
  // false: a covered mode never hands a region the cover could not build to
  // the ABC flow -- it is an error naming the reason. true: that region is
  // mapped from its own logic by the pass.abc flow (abc_fallback).
  bool                  fallback            = false;
  std::function<bool()> admission{};
};

}  // namespace livehd::usyn
