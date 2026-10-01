// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace livehd::usyn {

using Id = uint32_t;

// The admission bound on a truth table: its inputs.
inline constexpr uint32_t max_logical_inputs = 16;

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

inline constexpr uint64_t unlimited_work = std::numeric_limits<uint64_t>::max();

// How a finished run depended on its initial credits (Budget::credit_floor).
// Any initial credits it `reproduces` replay the identical run: the same
// decisions, results and consumed work.
struct Credit_floor {
  uint64_t work    = 0;      // credits the run consumed
  uint64_t floor   = 0;      // least initial credits under which the same run happens
  bool     bound   = false;  // the run depended exactly on its initial credits
  uint64_t credits = 0;      // those initial credits when bound; 0 otherwise
  bool     reproduces(uint64_t initial) const { return bound ? initial == credits : initial >= floor; }
  bool     operator==(const Credit_floor&) const = default;
};

// Deterministic work budget for prime enumeration and selection. Exhaustion is
// never an infeasibility certificate.
//
// Reproduction tracking: the initial credits are `consumed + remaining`. `floor`
// is the least initial credits under which this same run happens and `bound`
// means the run depended exactly on its initial credits. Every decision that
// depends on the credits records its requirement: a successful spend needs
// initial >= consumed afterwards; a spend refused for lack of work, or a failed
// has(), sets `bound` (so does an overdrawn absorb); a Credit_share records what
// each of its answers needs, and absorb() lifts a slice's requirement through
// its size rule. Code must never branch on, or size anything from, `remaining`
// directly (reporting a consumed delta is fine). A process/time admission
// refusal (resource_exhausted) is not a reproduction fact: such a result must
// never be reused.
struct Budget {
  uint64_t              remaining;
  bool                  exhausted = false;
  std::function<bool()> admission{};
  uint64_t              checkpoint_work    = 0;
  bool                  resource_exhausted = false;
  // Work between admission checks. A check samples the process (a syscall), so
  // it stays off the per-cut path; tests shorten it to hit a cancellation.
  uint64_t              admission_interval = 262144;
  uint64_t              consumed           = 0;
  uint64_t              floor              = 0;
  bool                  bound              = false;
  // How slice() sized this budget from its parent; absorb() reads it.
  struct Origin {
    uint64_t consumed = 0, remaining = 0, cap = unlimited_work, divisor = 1, reserve = 0;
  };
  Origin origin{};

  bool         spend(uint64_t amount = 1);
  // remaining >= amount, recorded either way (a false answer binds the run).
  bool         has(uint64_t amount = 1);
  bool         available(uint64_t amount = 1) { return !exhausted && has(amount); }
  // A child of min(cap, (remaining - reserve) / divisor) credits (zero when
  // remaining <= reserve) that shares this budget's admission cadence: it
  // resumes the current checkpoint instead of sampling on its first spend, and
  // inherits sticky exhaustion/refusal.
  Budget       slice(uint64_t cap, uint64_t divisor = 1, uint64_t reserve = 0) const;
  // Charge a finished child created by slice(). Completed work always
  // transfers, and no admission sample is taken while unwinding; a refusal the
  // child observed stays sticky here. The child's reproduction requirement
  // becomes this budget's: an unbound child needs its size to stay >= its
  // floor; a bound child needs its exact size, which a cap-limited size keeps
  // for remaining >= reserve + divisor*cap and a remaining-limited size binds.
  void         absorb(const Budget& child);
  Credit_floor credit_floor() const { return {consumed, floor, bound, bound ? consumed + remaining : 0}; }
};

// min(cap, remaining / divisor) read once from `budget` and compared later, a
// sub-cap that other work shares. Each answer records only what it needs: an
// answer that needs the value to stay >= y needs remaining >= divisor*y at the
// read; one that needs it to stay <= y binds the run unless cap <= y.
class Credit_share {
public:
  Credit_share(Budget& budget, uint64_t cap, uint64_t divisor = 1);
  bool     exceeds(uint64_t x);  // value > x
  uint64_t clamp(uint64_t x);    // min(value, x)

private:
  void     at_least(uint64_t y);
  void     at_most(uint64_t y);
  Budget&  budget_;
  uint64_t consumed_, cap_, divisor_, value_;
};

// A Budget::slice charged back to its parent when it goes out of scope.
class Work_slice {
public:
  Work_slice(Budget& parent, uint64_t cap, uint64_t divisor = 1, uint64_t reserve = 0)
      : parent_(parent), work(parent.slice(cap, divisor, reserve)) {}
  ~Work_slice() { parent_.absorb(work); }
  Work_slice(const Work_slice&)            = delete;
  Work_slice& operator=(const Work_slice&) = delete;

private:
  Budget& parent_;

public:
  Budget work;
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
// Minimum-literal SOP (ties: fewer cubes) of a total function of at most 8
// inputs, over primes of at most max_series literals, when the bounded
// (20000-node) branch and bound completes; otherwise the best cover found, at
// least as good as the greedy seed and still reported feasible. `factored` also counts
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
