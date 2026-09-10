// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Region body <-> ABC translation for pass.abc. Each colored
// region (handed over by pass.partition's decomposition seam) is bit-blasted
// into an ABC AIG netlist, optimized + technology-mapped by ABC against a
// Liberty library, and read back as a netlist of 1-bit blackbox Sub cells named
// after the Liberty cells. The bit-blast boundary (multi-bit module IO <-> 1-bit
// ABC PI/PO) is handled with shift bit-selects on inputs -- in place, or via one
// shared unpacker def per width when a region reads most of a wide bus -- and a
// Concat on outputs, the modern equivalent of the old Pick/Join path.

#include "abc_map.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

#include "ware_module.hpp"
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif
#include <numeric>
#include <print>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "abc_blast.hpp"
#include "abc_boundary.hpp"
#include "abc_incr.hpp"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "cell.hpp"
#include "diag.hpp"
#include "dlop.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/graph.hpp"
#include "host_mem.hpp"
#include "node_util.hpp"
#include "predict_abc_size.hpp"
#include "rapidjson/document.h"
#include "synthesis_cost.hpp"
#include "worker_pool.hpp"

// clang-format off
// ABC headers must stay in dependency order: abc.h defines Abc_Frame_t (used by
// cmd.h/main.h) and the word/namespace macros. Do not sort.
extern "C" {
#include "base/abc/abc.h"       // brings abc_global.h (word, macros, ABC_NAMESPACE_*)
#include "base/main/abcapis.h"  // Abc_Frame_t
#include "base/main/main.h"
#include "base/main/mainInt.h"  // frame-owned SCL library capability gate
#include "base/cmd/cmd.h"
#include "aig/hop/hop.h"
#include "map/mio/mio.h"
#include "map/scl/sclLib.h"  // SC_Lib: the parsed NLDM (`buffer`/`dnsize` + QoR timing)
#include "map/scl/sclSize.h"  // SC_Man: physical NLDM QoR after sizing
#include "misc/extra/extra.h"
}
// clang-format on

namespace gu = livehd::graph_util;

namespace livehd::abc {

namespace {

// Built-in timing flow: ABC9 flow2 with 6-input LUT restructuring, followed
// by standard-cell mapping. The mux encoding sweep selected this for delay;
// the former baseline below remains the area objective. LUT delays are unit
// levels, NOT picoseconds: do not pass {D} to &if. The SCL tail and budget
// ladder enforce the physical target after &nf maps to Liberty cells.
// &scorr is deliberately omitted: it is a no-op for combinational inputs but
// may merge registers in sequential regions. All built-in transformations
// must preserve the latch correspondence (including the QN encoding).
//
// The `{F}` tail is the fanout fix. Without it ABC leaves nets far past the
// Liberty's characterized load -- dino had 283 nets over 32 sinks and a mapped
// net with 384 -- and `pass.opentimer` then EXTRAPOLATES off the end of the NLDM
// table (an `a21oi_1` came out at 3090 ns against a ~0.05 ns intrinsic delay).
// `buffer -N` caps mapped fanout exactly, and `dnsize {B}` area-recovers around
// the inserted buffers DOWN TO the region's delay budget (`-D <budget>`: a bare
// `dnsize` would only preserve whatever delay the mapper landed on). The mapper
// runs on `read_lib -s`'s unit-delay GENLIB, so `&nf` produces a min-depth
// mapping on the smallest cells and the SCL steps own every physical decision;
// `map_region` then runs `upsize {B}; dnsize {B}` ONLY when this result still
// misses the budget, and the unbounded `upsize; dnsize` only when even that
// does: upsize optimizes for the fastest achievable delay rather than the
// requested budget. On the 16x32 Bedrock one-hot mux, running it despite
// meeting 100 ps changed 30.79 um^2 at 52.22 ps into 45.72 um^2 at 46.47 ps --
// a 48% area increase. These are SCL commands: they need a MAPPED network, so
// they must follow `&put`, and they need `pLibScl`, which `read_lib -s` loads.
//
// It cannot fix everything: a net driven by a NATIVE (unblasted) node -- a wide
// SRA, packed wiring, a region boundary -- never reaches ABC, so its fanout
// survives. Those are the residual over-limit nets.
constexpr std::string_view kCombFlow
    = "strash; &get -n; &sweep; "
      "&synch2 -K 6 -C 500; &if -m -K 6; &mfs; &save; "
      "&dch -C 500; &if -m -K 6; &mfs; &save; "
      "&load; &st; &sopb -R 10 -C 4; "
      "&synch2 -K 6 -C 500; &if -m -K 6; &mfs; &save; "
      "&dch -C 500; &if -m -K 6; &mfs; &save; "
      "&load; &st; &nf {D}; &put -o";

// Appended to a BUILT-IN flow when max_fanout != 0. Not part of the constants
// above so that max_fanout=0 yields a clean unbuffered string rather than a
// stripped one; a custom flow places `{F}` (the bare number) and `{B}` itself.
// `{B}` is the region's delay BUDGET as `-D <ps>` (target minus the register
// margin when the region holds flops; empty without a target, so the tail
// degrades to a bare `dnsize`): `dnsize -D` lets the down-sizing consume the
// slack up to the budget instead of preserving the delay it started from.
// A primary input's fanout is treed by this same `buffer`: ABC only does so
// for an input that has a driving cell, which resolve_boundary_defaults
// declares (the `boundary_drive` stand-in) -- out of band of this string, so
// the cache recipe spells it (`|pi_drive=`).
constexpr std::string_view kBufferTail = "; buffer -N {F}; dnsize {B}";

// Bound SAT work in the area objective, as in the timing flow. Unbounded
// FRAIG conflicts dominated small regions of beamformer/CPU/KOIOS designs.
// The AREA objective otherwise retains the former baseline that won mux area.
// Without a delay it runs once, with kBufferTail. With a delay it is a second
// candidate, sized to the budget with kAreaTail and accepted only if it meets
// that budget with less SCL area. The timing result wins every tie.
constexpr std::string_view kAreaFlow
    = "strash; &get -n; &fraig -x -C 500; &put; dc2; strash; &get -n; &dch -f -C 500; &nf {D}; &put -o";
constexpr std::string_view kAreaTail = "; buffer -N {F}; upsize {B}; dnsize {B}";

// The MAPPER step of both built-in flows, spelled once so map_region's
// area-recovery pass can replay exactly it -- and nothing else -- after `&undo`.
constexpr std::string_view kMapCmd = "&nf {D}";

// The MAPPED-network hand-back both built-in flows END with. `-o` matters:
// when `&put` rebuilds the logic network it "decouples" every CO driver
// (Abc_NtkFromCellMappedGia -> Abc_NtkLogicMakeSimpleCos): a CI feeding a CO
// gets a buffer, and a gate feeding two or more COs gets -- WITHOUT `-o` -- a
// duplicate of itself, a real second cell (2,802 exact-duplicate comb cells on
// the lhdtrack asap7 corpus against yosys's 92; br_amba_axi_demux 171 of them)
// or -- WITH `-o` -- a CO-only buffer. The read-back aliases every such buffer
// away (identity-buffer bypass in map_region, see is_identity_gate), so `-o`
// turns the duplicates into nothing at all. Measured with the bypass in place,
// br_amba_axi_demux asap7: 2,704 -> 2,533 ABC cells, 158.6 -> 146.7 um^2 of
// ABC comb area (sky130 11,482 -> 9,990), ABC's own and OpenTimer's delay
// unchanged. The cost: a gate that used to be duplicated now drives every one
// of those COs itself (fanout still capped by the `buffer -N` tail), so a
// whole-netlist STA can see a heavier driver. The unmapped `&put` in the middle
// of the flow is unaffected (an AIG has no CO decoupling). The area-recovery
// remap replays exactly this step after `&undo`, so it is spelled once.
constexpr std::string_view kPutCmd = "&put -o";

// Built-in sequential flow (seq=true). Same comb opt/map as kCombFlow; the
// latches only carry the registers across ABC so it can optimize the logic
// BETWEEN them. Retiming (`dretime`) is deliberately NOT in the default
// (2opt-freq E ruling): moving registers reshapes the latch count/order,
// which (a) drops the register-preserving flop read-back to anonymous
// per-latch flops (breaking the tier-1 name correspondence post-synthesis
// LEC relies on, 3a-synth), (b) loses the din-cone source attribution
// (latch->source-flop mapping needs a stable count), and (c) is a
// latency-visible transform the 2opt-freq loop's cycle-accurate gate
// forbids. Opt in explicitly per run or per region when that is understood:
// `--set pass.abc.flow="strash; &get -n; &dc4; dretime; &dch -f; &nf {D};
// &put"` (the read-back stays robust to reshaped latches).
constexpr std::string_view kSeqFlow = kCombFlow;

// The remap in map_region assumes both built-in flows END with the mapper step
// followed by kPutCmd, because `&undo` reverses exactly one GIA transformation.
static_assert(kCombFlow.ends_with("; &nf {D}; &put -o"));
static_assert(kSeqFlow.ends_with("; &nf {D}; &put -o"));
static_assert(kCombFlow.ends_with(kPutCmd) && kSeqFlow.ends_with(kPutCmd));
static_assert(kCombFlow.find(kMapCmd) != std::string_view::npos);
// The tails size to the BUDGET ({B}); `{D}` there would size to the full
// period and hand every flop-bearing region back to OpenSTA over by the
// register overhead.
static_assert(kBufferTail.find("{B}") != std::string_view::npos && kBufferTail.find("{D}") == std::string_view::npos);
static_assert(kAreaTail.find("{B}") != std::string_view::npos && kAreaTail.find("{D}") == std::string_view::npos);

// Standard ABC synthesis scripts from berkeley-abc's abc.rc, installed as
// aliases so a `--set pass.abc.flow="resyn2"` (or any other abc.rc script name)
// works exactly as it does in an interactive ABC shell. LiveHD drives ABC
// through the library entry (Abc_Start) which — unlike the `abc` binary — never
// sources abc.rc, so the alias vocabulary is not present unless we install it.
// Bodies are copied verbatim from abc.rc; the short-name building blocks
// (b/rw/rs/...) MUST be registered too because the scripts expand to them
// recursively when the alias is applied. ';' inside the quoted body is protected
// by ABC's CmdSplitLine tokenizer (same path `source abc.rc` takes). Keep this
// list in sync with the cheat-sheet in pass_abc.cpp's `flow` help text.
constexpr std::string_view kAbcAliases[] = {
    // building blocks: short name -> real ABC command
    "alias b balance",
    "alias rw rewrite",
    "alias rwz rewrite -z",
    "alias rf refactor",
    "alias rfz refactor -z",
    "alias rs resub",
    "alias rsz resub -z",
    "alias st strash",
    "alias f fraig",
    "alias dret dretime",
    "alias ret retime",
    // AIG optimization scripts
    R"(alias resyn   "b; rw; rwz; b; rwz; b")",
    R"(alias resyn2  "b; rw; rf; b; rw; rwz; b; rfz; rwz; b")",
    R"(alias resyn2a "b; rw; b; rw; rwz; b; rwz; b")",
    R"(alias resyn3  "b; rs; rs -K 6; b; rsz; rsz -K 6; b; rsz -K 5; b")",
    R"(alias compress  "b -l; rw -l; rwz -l; b -l; rwz -l; b -l")",
    R"(alias compress2 "b -l; rw -l; rf -l; b -l; rw -l; rwz -l; b -l; rfz -l; rwz -l; b -l")",
    R"(alias choice  "fraig_store; resyn; fraig_store; resyn2; fraig_store; fraig_restore")",
    R"(alias choice2 "fraig_store; balance; fraig_store; resyn; fraig_store; resyn2; fraig_store; resyn2; fraig_store; fraig_restore")",
    // resubstitution-heavy scripts
    R"(alias src_rw  "st; rw -l; rwz -l; rwz -l")",
    R"(alias src_rs  "st; rs -K 6 -N 2 -l; rs -K 9 -N 2 -l; rs -K 12 -N 2 -l")",
    R"(alias src_rws "st; rw -l; rs -K 6 -N 2 -l; rwz -l; rs -K 9 -N 2 -l; rwz -l; rs -K 12 -N 2 -l")",
    R"(alias resyn2rs    "b; rs -K 6; rw; rs -K 6 -N 2; rf; rs -K 8; b; rs -K 8 -N 2; rw; rs -K 10; rwz; rs -K 10 -N 2; b; rs -K 12; rfz; rs -K 12 -N 2; rwz; b")",
    R"(alias compress2rs "b -l; rs -K 6 -l; rw -l; rs -K 6 -N 2 -l; rf -l; rs -K 8 -l; b -l; rs -K 8 -N 2 -l; rw -l; rs -K 10 -l; rwz -l; rs -K 10 -N 2 -l; b -l; rs -K 12 -l; rfz -l; rs -K 12 -N 2 -l; rwz -l; b -l")",
    // GIA (& space) optimization scripts
    R"(alias &dc3 "&b; &jf -K 6; &b; &jf -K 4; &b")",
    R"(alias &dc4 "&b; &jf -K 7; &fx; &b; &jf -K 5; &fx; &b")",
};

std::string subst(std::string s, std::string_view tok, std::string_view val) {
  for (auto pos = s.find(tok); pos != std::string::npos; pos = s.find(tok, pos)) {
    s.replace(pos, tok.size(), val);
  }
  return s;
}

// Adapter exposing the per-region ABC gate constructors as the arith::Ops
// bit-algebra (Bit = Abc_Obj_t*), so the templated adder/comparator builders in
// abc_arith.hpp drive ABC without any ABC dependency of their own (2i-abc_arith).
struct Abc_bit_ops {
  std::function<Abc_Obj_t*(bool)>                   konst;
  std::function<Abc_Obj_t*(Abc_Obj_t*)>             not_;
  std::function<Abc_Obj_t*(Abc_Obj_t*, Abc_Obj_t*)> and_fn;
  std::function<Abc_Obj_t*(Abc_Obj_t*, Abc_Obj_t*)> or_fn;
  std::function<Abc_Obj_t*(Abc_Obj_t*, Abc_Obj_t*)> xor_fn;
  Abc_Obj_t*                                        zero() { return konst(false); }
  Abc_Obj_t*                                        one() { return konst(true); }
  Abc_Obj_t*                                        inv(Abc_Obj_t* a) { return not_(a); }
  Abc_Obj_t*                                        and_(Abc_Obj_t* a, Abc_Obj_t* b) { return and_fn(a, b); }
  Abc_Obj_t*                                        or_(Abc_Obj_t* a, Abc_Obj_t* b) { return or_fn(a, b); }
  Abc_Obj_t*                                        xor_(Abc_Obj_t* a, Abc_Obj_t* b) { return xor_fn(a, b); }
};

}  // namespace

// The slack floor. Below it a remap cannot buy back anything ABC would not
// already have taken, and it would still cost a second mapping pass.
static constexpr double kMinRelaxPct = 25.0;

int area_relax_percent(float target, float achieved, uint32_t cap) {
  if (cap == 0 || target <= 0.0f || achieved <= 0.0f || achieved > target) {
    return 0;
  }
  const double slack_pct = (static_cast<double>(target) / achieved - 1.0) * 100.0;
  if (slack_pct < kMinRelaxPct) {
    return 0;
  }
  return static_cast<int>(std::min(slack_pct, static_cast<double>(cap)));
}

// {D}/{L} expand to the full FLAG (`-D <val>` / `-L <val>`) when the option is
// set and to nothing otherwise — `&nf {D}` needs `&nf -D 4`, and a bare value
// (`&nf 4`) is silently ignored by ABC, which made the delay target a no-op.
namespace {
std::string flag_subst(std::string f, std::string_view tok, char flag, const std::string& val) {
  return subst(std::move(f), tok, val.empty() ? std::string{} : std::format("-{} {}", flag, val));
}

// `read_lib` retains scalar timing arcs too, but a physical GENLIB needs actual
// slew/load surfaces. Its vTempls vector is not a capability flag: the
// Liberty reader consumes templates while constructing the per-pin surfaces,
// so a valid NLDM library such as ASAP7 can leave it empty. Inspect the parsed
// inverter timing surface instead.
//
// This is the ONE library-capability predicate for every SCL command the pass
// drives (the `buffer`/`dnsize` tail and the `stime`-shaped QoR
// timer). It is deliberately STRICTER than ABC's own `Abc_SclHasDelayInfo`,
// which is satisfied by a scalar-only arc: the SCL timer walks 2-D surfaces,
// so a scalar Liberty that happens to declare `lu_table_template` must NOT be
// accepted (the old vTempls proxy accepted exactly that, and rejected ASAP7).
// `Abc_SclFindInvertor` cannot be trusted to return NULL when the library has
// no inverter -- its `Vec_PtrForEachEntry` loop leaves the LAST cell class
// assigned when nothing matches -- so re-check that the cell really is a
// one-input inverter before indexing its timing arcs.
bool lib_has_nldm_timing(const SC_Lib* lib) {
  if (lib == nullptr) {
    return false;
  }
  auto* inv = Abc_SclFindInvertor(const_cast<SC_Lib*>(lib), 0);
  if (inv == nullptr || inv->n_inputs != 1) {
    return false;
  }
  auto* timing = Scl_CellPinTime(inv, 0);
  return timing != nullptr && Vec_FltSize(&timing->pCellRise.vIndex0) > 1 && Vec_FltSize(&timing->pCellRise.vIndex1) > 1;
}

// The identity-buffer bypass (map_region read-back, pass 1b). ABC materializes
// a pure WIRE as a Liberty buffer whenever a CI drives a CO or one gate drives
// two or more COs (Abc_NtkLogicMakeSimpleCos, run by `&put` and once more by
// Abc_NtkToNetlist): every PI->PO, latchQ->PO, PI->latchD and blackbox-boundary
// feed-through bit costs a cell. Yosys never pays it -- its ABC sub-netlist is
// built per signal, so a signal is never both PI and PO and no CO ever shares a
// driver. Measured on the lhdtrack corpus (asap7, syn_lhd_verilog): 20,905
// buffers = 4.1% of ALL cell area against yosys's 4; br_demux_onehot was 512
// HB1xp67 out of 528 cells (95.5% of its area, 31.26 vs yosys 1.40 um^2), and
// br_amba_apb_timing_slice 213 (108 flopQ->out + 105 in->flopD). These two
// predicates pick out exactly those buffers so the read-back can alias the
// buffer's output net to its input net instead of minting a Sub.
//
// A gate qualifies when it is a single-input NON-inverting function. Mio derives
// uTruth for every gate it reads and 0xAA.. is "output = input 0" -- the very
// test Mio_LibraryDetectSpecialGates uses to pick the library buffer -- so this
// is independent of which drive strength `dnsize`/`upsize` landed on. ...
bool is_identity_gate(Mio_Gate_t* g) { return Mio_GateReadPinNum(g) == 1 && Mio_GateReadTruth(g) == 0xAAAAAAAAAAAAAAAAULL; }

// ... and its output net feeds nothing but COs (a PO or a latch input: the two
// object kinds Abc_ObjIsCo names). A MakeSimpleCos buffer feeds exactly its
// CO(s); a `buffer -N` fanout-tree buffer always feeds at least one node
// (`buffer` never buffers CI nets, fBufPis=0), so the rule removes EVERY
// decoupling buffer and keeps EVERY fanout buffer. The tail buffers must stay:
// with max_fanout=0 br_amba_axi_shrinker's opensta went 292 -> 1146 ps. An
// inverted CI->CO edge is a real INV cell and never matches either.
bool only_co_fanouts(Abc_Obj_t* net) {
  if (Abc_ObjFanoutNum(net) == 0) {
    return false;
  }
  Abc_Obj_t* f = nullptr;
  int        k = 0;
  Abc_ObjForEachFanout(net, f, k) {
    if (!Abc_ObjIsCo(f)) {
      return false;
    }
  }
  return true;
}
}  // namespace

// A built-in flow gains the buffering tail; a caller-supplied flow does not (it
// owns its own command list and may place `{F}` where it wants).
std::string Mapper::resolve_flow(std::string_view builtin) const {
  std::string f = std::string{builtin};
  if (opts_.max_fanout != 0) {
    f += kBufferTail;
  }
  return f;
}

std::string Mapper::subst_flow(std::string f) const {
  f = flag_subst(std::move(f), "{D}", 'D', opts_.delay);
  f = flag_subst(std::move(f), "{L}", 'L', opts_.load);
  // {B} is the region budget, already spelled as a flag (`-D <ps>`) or empty.
  f = subst(std::move(f), "{B}", budget_flag_);
  // {F} is the bare fanout NUMBER (buffer's -N takes it), not a flag.
  return subst(std::move(f), "{F}", std::to_string(opts_.max_fanout));
}

std::string Mapper::comb_flow() const {
  if (!opts_.flow.empty()) {
    return subst_flow(opts_.flow);
  }
  if (opts_.delay.empty()) {
    // `none` disables the timed second candidate, not untimed synthesis.
    return opts_.area_flow == "none" ? subst_flow(resolve_flow(kAreaFlow)) : area_flow();
  }
  return subst_flow(resolve_flow(kCombFlow));
}

std::string Mapper::seq_flow() const { return comb_flow(); }

std::string Mapper::area_flow() const {
  if (opts_.area_flow == "none") {
    return {};
  }
  if (!opts_.area_flow.empty()) {
    return subst_flow(opts_.area_flow);  // caller-owned, like `flow`: no tail is appended
  }
  std::string f = std::string{kAreaFlow};
  if (opts_.max_fanout != 0) {
    f += opts_.delay.empty() ? kBufferTail : kAreaTail;
  }
  return subst_flow(std::move(f));
}

double Mapper::reg_margin_ps() const {
  if (opts_.reg_margin == "auto") {
    // The cell the netlist's registers become. Without one (register=false, or
    // a library with no plain DFF) the flops stay native and are mapped later
    // by whoever consumes the netlist -- their overhead is unknown here, so
    // none is assumed rather than a guess that would silently move every
    // region's budget.
    return dff_.has_value() ? dff_->clk_to_q_ps + dff_->setup_ps : 0.0;
  }
  char*        end = nullptr;
  const double v   = std::strtod(opts_.reg_margin.c_str(), &end);
  return (end != opts_.reg_margin.c_str() && *end == '\0' && v > 0.0) ? v : 0.0;
}

float Mapper::region_budget(float target, bool has_flops) const {
  if (target <= 0.0f) {
    return target;
  }
  if (!has_flops) {
    return target;
  }
  return std::max(1.0f, target - static_cast<float>(reg_margin_ps()));
}

bool Mapper::nldm_requested() const {
  // The GENLIB is installed ONCE, in start(), before any region maps: swapping
  // it mid-run would delete the Mio library that cell_descs_ is keyed on. So a
  // delay target anywhere in the run-level options or the CLI region_opts must
  // be visible here, not just `opts_.delay` for the current region.
  if (!startup_opts_.delay.empty() || !opts_.delay.empty()) {
    return true;
  }
  const auto timed = [](const Region_opts_map& map) {
    return std::any_of(map.begin(), map.end(), [](const auto& kv) {
      return kv.second.delay.has_value() && !kv.second.delay->empty();
    });
  };
  if (timed(region_opts_cli_)) {
    return true;
  }
  return std::any_of(graph_region_opts_.begin(), graph_region_opts_.end(), [&](const auto& kv) { return timed(kv.second); });
}

std::string Mapper::resolve_recipe() const {
  // Verbatim, not a hash: a hash collision here would reuse a netlist mapped
  // under a different recipe. Both flow strings are pinned (map_region picks one
  // by mode, and the mode is in the salt); '|' separates fields that never
  // contain '|'.
  //
  // `nldm` ("SCL sizing/timing requested") is NOT derivable from the flow
  // string: a region whose region_opts override `delay` back to empty spells
  // the same `&nf` command as a plain untimed run, yet its QoR row is in
  // picoseconds from the SCL timer. Without this field the incremental cache
  // would hand that row to a later run that never asked for timing. The
  // Liberty content is already in Incr_cache::make_salt, so the request is the
  // only missing half of the decision.
  //
  // `arelax` joins them for the same reason `nldm` did: the area-recovery remap
  // re-maps a region that beat its budget, so two runs that spell an identical
  // flow still produce different netlists (and different QoR rows) when the cap
  // differs.
  //
  // `genlib`/`area`/`margin`/`objective` pin the mapping objective: the mapper
  // now runs on the unit-delay GENLIB (a row mapped under the old gain-100 one
  // spells the same `&nf` command), the area candidate re-maps a region whose
  // delay flow met its budget (a run with a different `area_flow`, or with
  // the candidate off, must never reuse the winner of a comparison it did not
  // run), and the register margin decides that budget (already spelled into
  // the tails' `-D` for this region; repeated here so a margin change under a
  // flop-less region still reads as a different recipe).
  return std::format(
      "native-wiring=2|comb={}|seq={}|adder={}|block={}|mult={}|nldm={}|arelax={}|genlib=unit|area={}|margin={}|"
      "objective=budget|barrel={}",
      comb_flow(),
      seq_flow(),
      static_cast<int>(opts_.adder),
      opts_.block_size,
      static_cast<int>(opts_.multiplier),
      nldm_requested() ? 1 : 0,
      opts_.area_relax_pct,
      opts_.area_flow == "none" ? std::string{"none"} : area_flow(),
      reg_margin_ps(),
      opts_.reverse_barrel);
}

void Mapper::ensure_dff_cells() {
  // Register mapping target: scan the Liberty for a plain posedge D-flop (ABC's
  // read_lib already dropped it, so this is a separate text scan). A missing DFF
  // cell is not fatal — the read-back keeps flops native (the same shape as
  // register=false) so the netlist stays correct, just not fully cell-mapped.
  if (!startup_opts_.map_register || dff_preset_) {
    return;
  }
  auto sel    = liberty::resolve_dff_cells(startup_opts_.library, startup_opts_.dff_cell);
  dff_        = sel.base;
  dff_ladder_ = sel.ladder;
  if (dff_.has_value() && dff_ladder_.empty()) {
    dff_ladder_.push_back(*dff_);  // a ladder always has its base rung
  }
  dff_preset_ = true;  // resolved once; the pick is constant for the run
}

bool Mapper::start() {
  if (pabc_ != nullptr) {
    Abc_FrameEnter(static_cast<Abc_Frame_t*>(pabc_));
    return lib_loaded_;
  }
  pabc_ = Abc_FrameCreate();
  if (pabc_ == nullptr) {
    livehd::diag::err("pass.abc", "abc-frame", "internal").msg("could not initialize the ABC frame").fatal();
    return false;
  }
  auto* frame = static_cast<Abc_Frame_t*>(pabc_);
  Abc_FrameEnter(frame);
  // Install the abc.rc synthesis-script aliases (resyn2, compress2rs, ...) so a
  // user `--set pass.abc.flow="resyn2"` resolves. Best-effort: a malformed alias
  // would only fail later when used in `flow`, so do not abort the run here.
  for (auto a : kAbcAliases) {
    Cmd_CommandExecute(frame, std::string{a}.c_str());
  }
  // -s skips multi-output cells (sky130 fa/ha/...): the gate read-back speaks
  // single-output Mio gates only — a multi-output supergate would previously
  // read back as a null-pData node and silently collapse its cone to const0.
  auto cmd = std::string{"read_lib -s "} + startup_opts_.library;
  if (Cmd_CommandExecute(frame, cmd.c_str()) != 0) {
    livehd::diag::err("pass.abc", "read-lib", "unsupported")
        .msg("ABC could not read the Liberty library '{}'", startup_opts_.library)
        .fatal();
    return false;
  }
  lib_loaded_ = true;

  // ABC's structural mappers (`&nf`, `map`, `sfm`, ...) build their cell table
  // with Mio_CollectRootsNew*, which REQUIRES a buffer and an inverter gate.
  // `read_lib` only WARNS when the genlib reader cannot find them ("genlib
  // library reader cannot detect the buffer gate") and still returns 0, so the
  // load looks successful; the failure surfaces much later, inside the mapping
  // flow, as ABC printing `Error: Cannot find buffer gate in the library.` and
  // returning a NULL cell table that the very next mapper walk dereferences.
  // That lands as a bare SIGSEGV inside ABC -- and iassert's handler prints no
  // backtrace off glibc -- so the run ended with exit 1, an empty netlist
  // directory, and the reason visible only to whoever thought to `cat` the pass
  // log. Refuse here instead, naming the gates the library is missing.
  //
  // Checked on the GENLIB, not on the Liberty text: `read_lib -s` has already
  // dropped multi-output and sequential cells, and it is the surviving genlib
  // the mapper actually indexes.
  {
    auto*      mio    = static_cast<Mio_Library_t*>(Abc_FrameReadLibGen());
    const bool no_buf = mio == nullptr || Mio_LibraryReadBuf(mio) == nullptr;
    const bool no_inv = mio == nullptr || Mio_LibraryReadInv(mio) == nullptr;
    if (no_buf || no_inv) {
      const std::string_view what = (no_buf && no_inv) ? "buffer or inverter cell" : (no_buf ? "buffer cell" : "inverter cell");
      livehd::diag::err("pass.abc", "lib-no-buffer", "unsupported")
          .msg("Liberty library '{}' has no {}: ABC cannot technology-map against it", startup_opts_.library, what)
          .hint(
              "pass a Liberty that contains both a buffer and an inverter; a vendor library split across views (ASAP7 keeps "
              "them in its *_INVBUF_* view and its flops in *_SEQ_*) has to be merged into the single file synth.liberty takes")
          .fatal();
      return false;
    }
    // Cheapest gate per (pins, truth) for the QN read-back's inverting-twin
    // swap (see twin_index_). Multi-output and >6-input cells are already gone
    // (`read_lib -s`; Mio_GateReadTruth is 0 past 6 inputs) -- skip the latter.
    twin_index_.clear();
    Mio_Gate_t* g = nullptr;
    Mio_LibraryForEachGate(mio, g) {
      const int n = Mio_GateReadPinNum(g);
      if (n > 6) {
        continue;
      }
      const auto key = std::pair<int, uint64_t>{n, static_cast<uint64_t>(Mio_GateReadTruth(g))};
      auto       it  = twin_index_.find(key);
      if (it == twin_index_.end() || Mio_GateReadArea(g) < Mio_GateReadArea(static_cast<Mio_Gate_t*>(it->second))) {
        twin_index_[key] = g;
      }
    }
  }

  if (nldm_requested()) {
    // The mapper keeps `read_lib -s`'s unit-delay GENLIB (every pin 1.00: `&nf`
    // minimizes logic depth on the smallest cells) and the SCL steps -- the
    // `buffer`/`upsize`/`dnsize` tails and the `stime`-shaped QoR timer, all of
    // which walk `pAbc->pLibScl`'s per-pin NLDM surfaces -- own every physical
    // decision. The gain-100 GENLIB this used to install for a delay target
    // (`Abc_SclInstallGenlib(scl, 0, 100, fUseAll=1, 0)`) made `&nf` chase
    // delay it could not see the budget for: measured over 15 lhdtrack
    // designs it cost 1.63x yosys's area on ASAP7 (unit + `dnsize -D`: 1.24x)
    // and 1.15x on sky130 (1.22x before the area candidate, 1.07x with it) for
    // the same number of periods met. What the SCL path needs is 2-D tables;
    // a scalar-only Liberty keeps the unbuffered, unsized mapping (see the
    // tail strip in map_region) and says so once.
    if (lib_has_nldm_timing(static_cast<SC_Lib*>(Abc_FrameReadLibScl()))) {
      scl_timing_ok_ = true;
    } else {
      std::print("[pass.abc] delay target: '{}' has no 2-D slew/load NLDM tables; ABC cannot size or time cells\n",
                 startup_opts_.library);
    }
  }

  // Classic `map` consults the frame's SCL library on its own and derives a
  // physical GENLIB even for a scalar-only Liberty. ABC accepts those arcs on
  // read but its slew/load interpolation then yields zero and asserts. Keep
  // the already installed unit-delay GENLIB; expose SCL only when usable.
  if (auto* scl = static_cast<SC_Lib*>(frame->pLibScl); scl != nullptr && !lib_has_nldm_timing(scl)) {
    Abc_SclLibFree(scl);
    frame->pLibScl = nullptr;
  }
  resolve_boundary_defaults();

  ensure_dff_cells();
  if (startup_opts_.map_register) {
    if (dff_.has_value() && startup_opts_.verbose) {
      std::string rungs;
      for (const auto& c : dff_ladder_) {
        rungs += std::format("{}{} ({:.4f})", rungs.empty() ? "" : ", ", c.name, c.area);
      }
      std::print(
          "[pass.abc] register cell: {} (d={}, clk={}, {}={}{}); drive ladder: {}; overhead clk->Q {:.1f} + setup {:.1f} ps "
          "(reg_margin={} -> {:.1f} ps)\n",
          dff_->name,
          dff_->d_pin,
          dff_->clk_pin,
          dff_->q_inverted ? "qn" : "q",
          dff_->q_pin,
          dff_->q_inverted ? ", output inverted" : "",
          rungs,
          dff_->clk_to_q_ps,
          dff_->setup_ps,
          startup_opts_.reg_margin,
          reg_margin_ps(),
          opts_.reverse_barrel);
    }
    if (!dff_.has_value()) {
      livehd::diag::warn("pass.abc", "no-dff-cell", "unsupported")
          .msg("pass.abc register=true: no {} in '{}' — keeping flops native (no DFF-cell mapping)",
               startup_opts_.dff_cell.empty() ? "plain posedge D-flop cell" : std::format("cell '{}'", startup_opts_.dff_cell),
               startup_opts_.library)
          .emit();
    }
  }
  return true;
}

// Releases only THIS mapper's frame. It must not cascade into
// parallel_mappers_: map_regions calls stop() to drop a serial session before a
// batch, and tearing the workers' parsed Liberty down there would re-read the
// library once per worker per batch. finish_parallel() (and, as a backstop,
// ~Mapper destroying the unique_ptrs) owns the workers' sessions.
void Mapper::stop() {
  if (pabc_ != nullptr) {
    auto* frame = static_cast<Abc_Frame_t*>(pabc_);
    if (Abc_FrameReadGlobalFrame() == frame) {
      Abc_FrameLeave(nullptr);
    }
    Abc_FrameDestroy(frame);
    cell_descs_.clear();
    twin_index_.clear();
    pabc_          = nullptr;
    // The frame owned the parsed SCL library; a later start() must re-decide.
    // (`lib_loaded_` deliberately survives -- work() reads abc_started() AFTER
    // stop() to report whether ABC ran at all.)
    scl_timing_ok_ = false;
    scl_lib_ok_    = false;
    drive_cell_    = nullptr;  // an SC_Cell of the library the frame just freed
#if defined(__APPLE__)
    // Abc_Stop releases the frame's last networks to malloc, but Darwin may
    // retain those pages and their xzone reservations. A large final color can
    // therefore leave too little VA for the HHDS/QoR finalization that follows
    // even though the physical footprint is below its ceiling. Relieve only
    // after the frame is gone, when those allocations are actually reclaimable.
    const uint64_t pressure_ceiling = cost::configured_budget_bytes();
    if (pressure_ceiling != 0 && cost::process_footprint_bytes() > pressure_ceiling - pressure_ceiling / 4) {
      (void)malloc_zone_pressure_relief(nullptr, size_t{2} << 30);
    }
#endif
  }
}

namespace {

// One override entry {"flow":…,"delay":…,"load":…,"adder":…,"block_size":…,
// "multiplier":…} -> Region_opts. Unknown keys / bad values are hard errors:
// a mistyped agent hint must never silently no-op (2opt-freq contract).
bool parse_region_opts_entry(const rapidjson::Value& v, Region_opts& ro, std::string_view where, std::string_view color_key) {
  auto bad = [&](std::string_view what) {
    livehd::diag::err("pass.abc", "region-opts", "io").msg("{}: region_opts[\"{}\"]: {}", where, color_key, what).fatal();
    return false;
  };
  if (!v.IsObject()) {
    return bad("entry must be an object of per-region options");
  }
  for (const auto& mem : v.GetObject()) {
    const std::string_view key{mem.name.GetString(), mem.name.GetStringLength()};
    const auto&            val = mem.value;
    if (key == "ware") {
      if (!val.IsBool()) {
        return bad("'ware' must be true or false");
      }
      ro.ware = val.GetBool();
    } else if (key == "flow" || key == "delay" || key == "load") {
      if (!val.IsString()) {
        return bad(std::format("'{}' must be a string", key));
      }
      std::string s{val.GetString(), val.GetStringLength()};
      if (key == "flow") {
        ro.flow = std::move(s);
      } else if (key == "delay") {
        ro.delay = std::move(s);
      } else {
        ro.load = std::move(s);
      }
    } else if (key == "adder") {
      if (!val.IsString()) {
        return bad("'adder' must be a string (rca|cska|cla)");
      }
      auto a = arith::parse_adder_kind({val.GetString(), val.GetStringLength()});
      if (!a.has_value()) {
        return bad(std::format("unknown adder '{}' (use rca|cska|cla)", val.GetString()));
      }
      ro.adder = a.value();
    } else if (key == "barrel") {
      if (!val.IsString() || (std::string_view(val.GetString()) != "log" && std::string_view(val.GetString()) != "reverse")) {
        return bad("barrel must be log|reverse");
      }
      ro.reverse_barrel = std::string_view(val.GetString()) == "reverse";
    } else if (key == "multiplier") {
      if (!val.IsString()) {
        return bad("'multiplier' must be a string (array|tree)");
      }
      auto m = arith::parse_mult_kind({val.GetString(), val.GetStringLength()});
      if (!m.has_value()) {
        // No `auto` here, unlike the pass option: a region override IS the
        // explicit pick (leaving the key out is what keeps the search on), and
        // parse_mult_kind rejects the spelling -- advertising it made the
        // message name a value this very call refuses.
        return bad(std::format("unknown multiplier '{}' (use array|tree)", val.GetString()));
      }
      ro.multiplier = m.value();
    } else if (key == "block_size") {
      if (!val.IsInt() || val.GetInt() < 0) {
        return bad("'block_size' must be a non-negative integer");
      }
      ro.block_size = val.GetInt();
    } else {
      return bad(std::format("unknown option '{}' (use flow|delay|load|adder|block_size|multiplier|barrel|ware)", key));
    }
  }
  return true;
}

bool parse_region_opts_object(const rapidjson::Value& obj, Region_opts_map& out, std::string_view where) {
  if (!obj.IsObject()) {
    livehd::diag::err("pass.abc", "region-opts", "io")
        .msg("{}: region_opts must be a JSON object keyed by color id", where)
        .fatal();
    return false;
  }
  for (const auto& mem : obj.GetObject()) {
    const std::string_view key{mem.name.GetString(), mem.name.GetStringLength()};
    int                    color = 0;
    const auto*            b     = key.data();
    const auto*            e     = key.data() + key.size();
    auto [p, ec]                 = std::from_chars(b, e, color);
    if (ec != std::errc{} || p != e || color < 0) {
      livehd::diag::err("pass.abc", "region-opts", "io")
          .msg("{}: region_opts key '{}' is not a color id (non-negative integer)", where, key)
          .fatal();
      return false;
    }
    Region_opts ro;
    if (!parse_region_opts_entry(mem.value, ro, where, key)) {
      return false;
    }
    out[color] = std::move(ro);
  }
  return true;
}

}  // namespace

std::optional<Region_opts_map> parse_region_opts(std::string_view json, std::string_view where) {
  rapidjson::Document d;
  d.Parse(json.data(), json.size());
  if (d.HasParseError()) {
    livehd::diag::err("pass.abc", "region-opts", "io")
        .msg("{}: region_opts is not valid JSON (offset {})", where, d.GetErrorOffset())
        .fatal();
    return std::nullopt;
  }
  Region_opts_map m;
  if (!parse_region_opts_object(d, m, where)) {
    return std::nullopt;
  }
  return m;
}

void Mapper::prepare_region_opts(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  for (const auto& g : graphs) {
    if (!g || graph_region_opts_.contains(g.get())) {
      continue;
    }
    Region_opts_map options;
    if (auto info = g->get_input_node().attr(livehd::attrs::coloring_info); info.has()) {
      rapidjson::Document doc;
      const std::string   json{info.get()};
      doc.Parse(json.data(), json.size());
      if (!doc.HasParseError() && doc.IsObject()) {
        if (auto it = doc.FindMember("region_opts"); it != doc.MemberEnd()) {
          parse_region_opts_object(it->value, options, "coloring_info");
        }
      }
    }
    graph_region_opts_.emplace(g.get(), std::move(options));
  }
}

bool Mapper::apply_region_overrides(const livehd::partition::Region_body& rb) {
  const auto policy    = ware_policy(*rb.src, {opts_.ware_arith, opts_.ware_cmp, opts_.ware_shift});
  opts_.ware_arith     = policy.arith;
  opts_.ware_cmp       = policy.cmp;
  opts_.ware_shift     = policy.shift;
  bool flow_overridden = false;
  auto apply           = [&](const Region_opts& ro, std::string_view src) {
    if (ro.ware.has_value()) {
      opts_.ware = *ro.ware;
    }
    if (ro.flow.has_value()) {
      opts_.flow      = *ro.flow;
      flow_overridden = !ro.flow->empty();
    }
    if (ro.delay.has_value()) {
      opts_.delay = *ro.delay;
    }
    if (ro.load.has_value()) {
      opts_.load = *ro.load;
    }
    if (ro.adder.has_value()) {
      opts_.adder      = *ro.adder;
      opts_.auto_adder = false;
    }
    if (ro.block_size.has_value()) {
      opts_.block_size = *ro.block_size;
      opts_.auto_adder = false;
    }
    if (ro.reverse_barrel.has_value()) {
      opts_.reverse_barrel = *ro.reverse_barrel;
      opts_.auto_barrel    = false;
    }
    if (ro.multiplier.has_value()) {
      opts_.multiplier      = *ro.multiplier;
      opts_.auto_multiplier = false;
      opts_.auto_adder      = false;
    }
    std::print("[pass.abc] region '{}': color {} options override applied ({})\n", rb.module_name, rb.color, src);
  };

  // Graph-embedded overrides first (the block-attribute channel writes a
  // "region_opts" member into coloring_info), CLI second so --set wins.
  auto git = graph_region_opts_.find(rb.src);
  if (git == graph_region_opts_.end()) {
    Region_opts_map m;
    if (auto a = rb.src->get_input_node().attr(livehd::attrs::coloring_info); a.has()) {
      const std::string   info{a.get()};
      rapidjson::Document d;
      d.Parse(info.data(), info.size());
      if (!d.HasParseError() && d.IsObject()) {
        if (auto ro = d.FindMember("region_opts"); ro != d.MemberEnd()) {
          parse_region_opts_object(ro->value, m, "coloring_info");  // diag on malformed, best-effort continue
        }
      }
    }
    git = graph_region_opts_.emplace(rb.src, std::move(m)).first;
  }
  if (auto it = git->second.find(rb.color); it != git->second.end()) {
    apply(it->second, "coloring_info");
  }
  if (auto it = region_opts_cli_.find(rb.color); it != region_opts_cli_.end()) {
    apply(it->second, "--set region_opts");
  }
  return flow_overridden;
}

namespace {
// --- per-node diagnostic provenance ----------------------------------------
//
// A refusal that names only the REGION ("region 'miss_handler.miss_handler'")
// leaves the user staring at a 12k-node module with nothing to grep. Every
// per-node refusal in map_region therefore reports BOTH the original source
// location (hhds::attrs::srcid, resolved through the region's source graph)
// and the nearest user-visible signal name.

// A pipeline Flop stores one word at every declared cycle of delay.
[[nodiscard]] int pipeline_depth(const hhds::Node_class& node) {
  if (auto pm = gu::get_driver_of_sink_name(node, "pipe_min"); pm.is_const()) {
    return static_cast<int>(std::max<int64_t>(1, gu::const_of(pm).to_just_i64()));
  }
  return 1;
}

// Source span of a region node. Best-effort: a node with no srcid -- or a
// library whose srcmap was not loaded -- yields a null span, which renders
// location-less rather than wrong (diag::to_text never fabricates a location).
[[nodiscard]] livehd::diag::Span node_span(const livehd::partition::Region_body& rb, const hhds::Node_class& n) {
  if (rb.src == nullptr || n.is_invalid()) {
    return {};
  }
  auto a = n.attr(hhds::attrs::srcid);
  if (!a.has() || a.get() == 0) {
    return {};
  }
  return rb.src->source_locator().resolve_span(a.get());
}

// Nearest user-visible signal name for `n`. Synthesis-stage nodes are mostly
// UNNAMED (only what the source named keeps a name attr), so `debug_name`
// alone -- "shl_10620" -- tells a user nothing. Anchor on a real signal
// instead: this node's own named driver pin, else a bounded breadth-first walk
// of the fan-out (the named value it feeds, or the module port it reaches),
// else of the fan-in (the named operand it reads). `relation` comes back as
// "" / "feeds" / "reads" so the caller can say which way it had to look.
[[nodiscard]] std::string nearest_named_signal(const hhds::Node_class& n, std::string_view& relation) {
  // Two independent budgets, because two different things can blow up here: the
  // NODE budget bounds how far the search spreads, and the EDGE budget bounds
  // one step of it -- out_edges() is a lazy view over live storage and a
  // clock/reset-like pin fans out to 100k+ sinks, so bounding dequeues alone
  // would still let a single node enqueue the whole fan-out.
  constexpr size_t kMaxNodes = 256;
  constexpr size_t kMaxEdges = 4096;
  relation                   = {};
  if (n.is_invalid()) {
    return {};
  }
  // The value a node produces, named. out_pins() is the node's driver pins
  // directly -- walking out_edges() instead would re-visit one pin once per
  // consumer.
  const auto named_of = [](const hhds::Node_class& node) -> std::string {
    for (const auto& p : node.out_pins()) {
      if (auto nm = gu::pin_name_of(p); !nm.empty()) {
        return std::string{nm};
      }
    }
    auto nn = gu::node_name_of(node);
    return nn.empty() ? std::string{} : std::string{nn};
  };
  if (auto own = named_of(n); !own.empty()) {
    relation = "is";
    return own;
  }
  const auto walk = [&](bool downstream) -> std::string {
    absl::flat_hash_set<hhds::Node_class> seen{n};
    std::vector<hhds::Node_class>         frontier{n};
    size_t                                nodes = 0;
    size_t                                edges = 0;
    while (!frontier.empty() && nodes < kMaxNodes && edges < kMaxEdges) {
      std::vector<hhds::Node_class> next;
      for (const auto& cur : frontier) {
        if (++nodes > kMaxNodes || edges >= kMaxEdges) {
          break;
        }
        if (downstream) {
          for (const auto& e : cur.out_edges()) {
            if (++edges > kMaxEdges) {
              break;
            }
            // A module port is the best anchor of all: it is the name the
            // instantiating design uses for this value.
            if (gu::is_graph_output_pin(e.sink)) {
              if (auto nm = gu::pin_name_of(e.sink); !nm.empty()) {
                return std::string{nm};
              }
            }
            auto sn = e.sink.get_master_node();
            if (sn.is_invalid() || !seen.insert(sn).second) {
              continue;
            }
            if (auto nm = named_of(sn); !nm.empty()) {
              return nm;
            }
            next.push_back(sn);
          }
        } else {
          for (const auto& e : cur.inp_edges()) {
            if (++edges > kMaxEdges) {
              break;
            }
            if (auto nm = gu::pin_name_of(e.driver); !nm.empty()) {  // also resolves a module INPUT port
              return std::string{nm};
            }
            auto dn = e.driver.get_master_node();
            if (dn.is_invalid() || !seen.insert(dn).second) {
              continue;
            }
            if (auto nm = named_of(dn); !nm.empty()) {
              return nm;
            }
            next.push_back(dn);
          }
        }
      }
      frontier.swap(next);
    }
    return {};
  };
  if (auto down = walk(true); !down.empty()) {
    relation = "feeds";
    return down;
  }
  if (auto up = walk(false); !up.empty()) {
    relation = "reads";
    return up;
  }
  return {};
}

// A constant rendered for a ONE-LINE diagnostic: the literal, its PAYLOAD width,
// and how many of those bits are unknown.
//
// The width and the unknown count are not decoration. Dlop's carrier is SIGNED,
// so a non-negative value always renders with one leading `0` beyond its payload
// (`literal_payload_bits`): a u6 whose every bit is unknown prints as
// `0ub0??????`, and that leading digit reads as a seventh value bit -- or, worse,
// as a sign -- to anyone who did not write the renderer. Spelling out
// "(6 bits, 6 unknown)" says plainly that this is a six-bit value, entirely
// unknown, and not a negative one.
//
// An all-unknown 4096-bit literal would otherwise take the whole message
// hostage, so a long spelling is elided in the middle.
[[nodiscard]] std::string const_brief(const Dlop& v) {
  auto       s   = std::format("{}", v);  // Dlop's formatter renders to_pyrope()
  const auto bin = v.to_binary();
  const auto unk = static_cast<size_t>(std::count(bin.begin(), bin.end(), '?'));
  if (s.size() > 44) {
    s = std::format("{}...{}", s.substr(0, 28), s.substr(s.size() - 6));
  }
  return std::format("{} ({} bits, {} unknown)", s, gu::literal_payload_bits(v), unk);
}

// "<cell>_<nid>" plus the nearest named signal: `shl_10620 (feeds 'mshr_d')`.
[[nodiscard]] std::string node_identity(const hhds::Node_class& n) {
  std::string_view rel;
  const auto       nm = nearest_named_signal(n, rel);
  if (nm.empty()) {
    return gu::debug_name(n);
  }
  if (rel == "is") {
    return std::format("{} '{}'", gu::debug_name(n), nm);
  }
  return std::format("{} ({} '{}')", gu::debug_name(n), rel, nm);
}
// Resolve the original source "file:line" of region output `po` into q.crit_*
// (2opt-freq A). Best-effort: a missing srcid or an unresolvable span just
// leaves crit_src empty — the QoR row is still useful without provenance.
void qor_src_of_output(const livehd::partition::Region_body& rb, size_t po, Region_qor& q) {
  q.crit_output = rb.outputs[po].name;
  auto drv      = rb.outputs[po].src_driver;
  if (drv.is_invalid()) {
    return;
  }
  auto onode = drv.get_master_node();
  if (onode.is_invalid()) {
    return;
  }
  auto span = node_span(rb, onode);
  if (!span.file.empty() && span.start_line.has_value()) {
    q.crit_src = span.file + ":" + std::to_string(*span.start_line);
  }
}

}  // namespace

// Memory admission (2opt-incr subtask 0). Deliberately MEASURED, not predicted:
// a static op/width model cannot see the phase that actually blows up. The peak
// is inside Cmd_CommandExecute's strash/&dch/&nf, which hold several network
// forms at once -- and an external calibration of "bytes per gate" is not even
// well defined here, because ABC's read_lib fixed cost dominates small designs
// (measured: a 96-gate region and a 4640-gate region had comparable RSS).
// What IS reliable is our own RSS while we translate.
//
// Projection: RSS grows roughly linearly in nodes blasted, so
//   projected_translation = rss_before + growth_so_far * total / blasted
// The projection can reject a clearly hopeless translation early, while the
// repeated exact samples and process backstop police later ABC network forms.
bool Mapper::over_budget(std::string_view region, uint64_t rss_before, size_t blasted, size_t total) {
  const uint64_t budget = cost::budget_bytes(opts_.memory_budget_mb);
  if (budget == 0 || blasted == 0) {
    return false;  // unknown host and no explicit budget: unenforceable, do not gate
  }
  // Sample phys_footprint, not resident_size: it is the metric macOS jetsam
  // charges (and it counts compressed/paged pages resident_size drops under
  // pressure), so it is the number that actually decides whether we get killed.
  // NB it is equally STICKY after free() -- freed pages linger until the
  // allocator returns them. Subtract the color-entry baseline so a long run is
  // not refused merely because completed mapped modules remain in its output
  // library; this is still conservative within one color.
  const uint64_t rss = cost::process_footprint_bytes();
  if (rss == 0) {
    return false;
  }

  // RSS at the first sample moves in large allocator/startup steps. Multiplying
  // that extrapolation by a guessed post-translation ABC factor produced severe
  // false positives (ROB: 8.3 GiB measured, 363 GiB predicted at a 16 GiB
  // ceiling). Use translation growth itself, require a large signal, and refuse
  // early only when even the projection clears the budget by a wide margin.
  // The exact live-RSS check remains the resource guarantee.
  constexpr uint64_t kMinGrowthToProject = uint64_t{256} << 20;
  constexpr uint64_t kProjectionMargin   = 4;

  const uint64_t grown            = rss > rss_before ? rss - rss_before : 0;
  const double   fraction         = static_cast<double>(blasted) / static_cast<double>(total);
  const uint64_t projected_growth = static_cast<uint64_t>(static_cast<double>(grown) / fraction);

  // The exact reading is the guarantee; the projection is only allowed to make a
  // hopeless region die sooner.
  //
  // TWO ceilings, each measured against its OWN number. Per-color GROWTH is what
  // tells a single oversize region from a merely large run, but growth alone has
  // no ceiling on the accumulated footprint: process_footprint_bytes() is sticky
  // after free (host_mem.hpp), so the allocator-pressure relief at the color
  // boundary is part of the resource guarantee. Without it, a many-color run
  // whose colors each add well under the budget can still walk the process into
  // the address-space limit lhd_main armed as RLIMIT_AS -- and ABC does not
  // null-check its allocations, so that lands as a bare SIGSEGV with no
  // diagnostic and no qor.json.
  //
  // The absolute test must NOT reuse `budget`: pass.abc.memory_budget_mb is
  // documented and used as a per-color GROWTH knob, and lhd's own ~23 MiB
  // baseline already exceeds a modestly pinned one, so measuring TOTAL rss
  // against it would refuse the first region of every such run. Measure it
  // against the process-wide PHYSICAL budget instead. On Darwin RLIMIT_AS has
  // separate VA-only allocator headroom (host_mem.cpp); admission must not count
  // that as physical capacity. A zero budget still means "unenforceable".
  uint64_t total_ceiling = cost::configured_budget_bytes();
  if (coordinator_ && coordinator_->parallel_stats_.memory_limit != 0) {
    total_ceiling = total_ceiling ? std::min(total_ceiling, coordinator_->parallel_stats_.memory_limit)
                                  : coordinator_->parallel_stats_.memory_limit;
  }
  // Concurrent colors share the process reading; their growth cannot be
  // attributed to one region. Keep the aggregate allowance under the absolute
  // process ceiling above.
  const uint64_t growth_budget = gu::sat_mul(budget, coordinator_ ? coordinator_->parallel_stats_.limit : 1U);
  const bool     over_growth   = grown > growth_budget;
  const bool     over_total    = total_ceiling != 0 && rss > total_ceiling;
  const bool     over_now      = over_growth || over_total;
  const bool     over_projected
      = coordinator_ == nullptr && grown >= kMinGrowthToProject && projected_growth / kProjectionMargin > budget;
  if (!over_now && !over_projected) {
    return false;
  }

  const auto        mib = [](uint64_t b) { return b >> 20; };
  // Name the ceiling the refusal actually came from, or the message describes a
  // derivation that never happened. An explicit memory_budget_mb is taken
  // verbatim and no reserve is subtracted, so quoting a reserve there would be
  // a second such invention.
  const std::string budget_desc
      = coordinator_
            ? (over_total ? std::format("parallel process physical-memory ceiling {} MiB (half of physical RAM or the smaller "
                                        "configured process budget)",
                                        mib(total_ceiling))
                          : std::format("aggregate worker growth budget {} MiB ({} workers; process-wide growth since color entry)",
                                        mib(growth_budget),
                                        coordinator_->parallel_stats_.limit))
        : (!over_growth && over_total)
            ? std::format(
                  "process physical-memory ceiling {} MiB (LIVEHD_MEMORY_BUDGET_MB, else physical minus reserve; Darwin "
                  "RLIMIT_AS has separate VA-only allocator headroom) -- the whole-PROCESS footprint, not this color's growth",
                  mib(total_ceiling))
        : opts_.memory_budget_mb > 0 ? std::format("per-color growth budget {} MiB (pass.abc.memory_budget_mb)", mib(budget))
                                     : std::format("per-color growth budget {} MiB (physical {} MiB minus a {} MiB reserve)",
                                                   mib(budget),
                                                   mib(cost::physical_ram_bytes()),
                                                   mib(cost::reserve_bytes()));
  // Once earlier colors exist to blame, say so: their retained memory is the
  // cost, and the caller's stock `color.max_gate=<smaller>` hint is then the
  // wrong advice.
  const std::string cause = (coordinator_ || over_growth || qor_.empty())
                                ? std::string{}
                                : std::format(" (after {} completed color(s), whose retained memory is the cost)", qor_.size());
  refusal_                = std::format(
      "region '{}' does not fit in memory: {} of {} node(s) translated ({:.0f}%), RSS {} MiB "
      "(was {} MiB, {} added {} MiB){}, {}{}",
      region,
      blasted,
      total,
      100.0 * fraction,
      mib(rss),
      mib(rss_before),
      coordinator_ ? "process" : "color",
      mib(grown),
      over_now ? std::string{} : std::format(", projected color growth {} MiB", mib(projected_growth)),
      budget_desc,
      cause);
  return true;
}

namespace {
// Rewrite the TRIVIALLY convertible remainders in a region into a mask, in
// place, and report whether any survived.
//
// `a % 2^k` is `a & (2^k - 1)` -- but ONLY for a non-negative dividend. The op
// is truncated remainder (the sign follows the dividend), so `-9 % 8` is -1,
// not 7, and masking a negative value is simply a different function.
//
// upass/tolg's lower_mod already folds this shape when it lowers Pyrope, so a
// Rem arriving from THAT path is non-trivial by construction. This pass exists
// for the readers that build the cell directly -- inou/yosys turns `$mod` into
// a Rem with whatever divisor the Verilog had, so a perfectly ordinary
// `x % 8` read from Verilog would otherwise hit the error below despite being
// one AND gate.
void rewrite_trivial_rems(hhds::Graph* g) {
  std::vector<hhds::Node_class> to_fix;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Rem) {
      continue;
    }
    auto b = gu::get_driver_of_sink_name(n, "b");
    auto a = gu::get_driver_of_sink_name(n, "a");
    if (b.is_invalid() || a.is_invalid() || !b.is_const()) {
      continue;
    }
    const auto& bc = gu::const_of(b);
    if (bc.has_unknowns() || !bc.is_just_i64()) {
      continue;
    }
    const int64_t bv = bc.to_just_i64();
    const int64_t ba = bv < 0 ? -bv : bv;
    // A negative-capable dividend cannot use the mask (see above). `is_unsign`
    // is the same non-negativity test lower_mod uses.
    if (ba < 2 || (ba & (ba - 1)) != 0 || !gu::is_unsign(a)) {
      continue;
    }
    to_fix.push_back(n);
  }
  for (auto n : to_fix) {
    auto          a  = gu::get_driver_of_sink_name(n, "a");
    auto          b  = gu::get_driver_of_sink_name(n, "b");  // named: gcc -Wdangling-reference on const_of(temporary)
    const auto&   bc = gu::const_of(b);
    const int64_t bv = bc.to_just_i64();
    const int64_t ba = bv < 0 ? -bv : bv;

    auto andn = gu::create_typed_node(*g, Ntype_op::And, gu::bits_of(n.get_driver_pin(0)));
    gu::setup_sink_by_name(andn, "as").connect_driver(a);
    gu::setup_sink_by_name(andn, "as").connect_driver(gu::create_const(*g, *Dlop::create_integer(ba - 1)));
    auto newd = andn.create_driver_pin(0);
    for (const auto& e : n.get_driver_pin(0).out_edges()) {
      e.sink.connect_driver(newd);
    }
    n.del_node();
  }
}

// ABC uses compact one-bit SRA selectors at native wide boundaries. When the
// boundary is a Set_mask chain, those selectors make every intermediate packed
// value observable and force cgen/Yosys to retain a quadratic chain of wide
// copies. Resolve each demanded bit through the constant masks to the actual
// base/value driver. This is the same bit identity used by abc_bit() while
// mapping, applied to the reconstructed body after every connection exists.
void bypass_setmask_bit_reads(hhds::Graph* g) {
  struct Rewrite {
    hhds::Node_class node;
    hhds::Pin_class  source;
    int64_t          bit;
  };
  std::vector<Rewrite> rewrites;

  for (auto node : g->body().nodes()) {
    if (gu::type_op_of(node) != Ntype_op::SRA || gu::bits_of(node.get_driver_pin(0)) != 1) {
      continue;
    }
    auto source = gu::get_driver_of_sink_name(node, "a");
    auto amount = gu::get_driver_of_sink_name(node, "b");
    if (source.is_invalid() || amount.is_invalid() || !amount.is_const() || !gu::const_of(amount).is_just_i64()) {
      continue;
    }
    int64_t bit = gu::const_of(amount).to_just_i64();
    if (bit < 0 || source.is_const() || gu::type_op_of(source.get_master_node()) != Ntype_op::Set_mask) {
      continue;
    }

    absl::flat_hash_set<hhds::Class_index> visited;
    bool                                   resolved = false;
    while (!source.is_const() && gu::type_op_of(source.get_master_node()) == Ntype_op::Set_mask) {
      auto writer = source.get_master_node();
      if (!visited.insert(writer.get_class_index()).second) {
        resolved = false;
        break;
      }
      auto mask  = gu::get_driver_of_sink_name(writer, "mask");
      auto base  = gu::get_driver_of_sink_name(writer, "a");
      auto value = gu::get_driver_of_sink_name(writer, "value");
      if (mask.is_invalid() || base.is_invalid() || value.is_invalid() || !mask.is_const()) {
        resolved = false;
        break;
      }
      const auto& mv    = gu::const_of(mask);
      auto [begin, end] = mv.get_mask_range();
      if (mv.has_unknowns() || begin < 0 || end <= begin) {
        resolved = false;
        break;
      }
      resolved = true;
      if (bit >= begin && bit < end) {
        source  = value;
        bit    -= begin;
        break;
      }
      source = base;
    }
    if (resolved && !source.is_invalid()) {
      rewrites.push_back({node, source, bit});
    }
  }

  for (const auto& rewrite : rewrites) {
    hhds::Pin_class replacement = rewrite.source;
    if (rewrite.bit != 0 || gu::bits_of(replacement) != 1) {
      auto select = gu::create_typed_node(*g, Ntype_op::SRA);
      replacement.connect_sink(gu::setup_sink_by_name(select, "a"));
      gu::create_const(*g, *Dlop::create_integer(rewrite.bit)).connect_sink(gu::setup_sink_by_name(select, "b"));
      replacement = select.create_driver_pin(0);
      gu::set_bits(replacement, 1);
      gu::set_unsign(replacement);
    }
    for (const auto& edge : rewrite.node.out_edges()) {
      edge.sink.connect_driver(replacement);
    }
    rewrite.node.del_node();
  }
}
}  // namespace

// One IO decl per Liberty cell in the output library (find-or-create), plus the
// cell's pin names in Mio order -- the SAME construction for a cell first seen
// by a region read-back and for a cell a boundary re-size swaps in
// (abc_boundary.cpp): both must agree on port ids (pid k+1 = Mio pin k, then
// the output) or a swapped Sub's existing pins would point at the wrong ports.
Mapper::Cell_desc& Mapper::cell_desc_for(void* mio_gate) {
  auto* g = static_cast<Mio_Gate_t*>(mio_gate);
  if (auto it = cell_descs_.find(g); it != cell_descs_.end()) {
    return it->second;
  }
  auto [it, inserted] = cell_descs_.try_emplace(g);
  I(inserted);
  auto& desc       = it->second;
  desc.name        = Mio_GateReadName(g);
  desc.output_name = Mio_GateReadOutName(g);
  desc.io          = outlib_->find_io(desc.name);
  const bool fresh = !desc.io;
  if (fresh) {
    desc.io = outlib_->create_io(desc.name);
  }
  hhds::Port_id pid = 1;
  for (auto* pin = Mio_GateReadPins(g); pin != nullptr; pin = Mio_PinReadNext(pin)) {
    desc.input_names.emplace_back(Mio_PinReadName(pin));
    if (fresh) {
      desc.io->add_input(desc.input_names.back(), pid);
      desc.io->set_bits(desc.input_names.back(), 1);
    }
    ++pid;
  }
  if (fresh) {
    desc.io->add_output(desc.output_name, pid);
    desc.io->set_bits(desc.output_name, 1);
  }
  return desc;
}

namespace {
uint64_t parallel_memory_limit() {
  const auto half       = cost::physical_ram_bytes() / 2;
  const auto configured = cost::configured_budget_bytes();
  return configured && half ? std::min(half, configured) : half;
}
}  // namespace

void Mapper::map_regions(std::span<const livehd::partition::Region_body> regions) {
  if (!refusal_.empty() || !time_refusal_.empty()) {
    return;
  }
  const auto     ware_begin    = ware_regions_.size();
  const unsigned limit         = synthesis_thread_limit(opts_.threads, std::thread::hardware_concurrency());
  parallel_stats_.limit        = limit;
  parallel_stats_.memory_limit = parallel_memory_limit();
  if (limit == 1 || regions.size() < 2 || parallel_stats_.memory_limit == 0) {
    for (const auto& rb : regions) {
      map_region(rb);
    }
    return;
  }

  // The partitioner owns every span/pre-body until this synchronous batch
  // returns. Workers share graph storage only while holding graph_mutex_.
  if (pabc_) {
    stop();  // release THIS mapper's serial session; the workers keep theirs
  }
  const uint64_t        baseline = cost::process_footprint_bytes();
  std::error_code       ec;
  const auto            library_size  = std::filesystem::file_size(opts_.library, ec);
  const uint64_t        liberty_bytes = ec ? 0 : library_size;
  std::vector<uint64_t> projections;
  for (const auto& rb : regions) {
    uint64_t aig = 0;
    for (const auto& node : rb.nodes) {
      aig = gu::sat_add(aig, gu::predict_abc_size(node));
    }
    projections.push_back(projected_abc_memory(aig, liberty_bytes));
  }
  const uint32_t first_id  = next_region_id_;
  next_region_id_         += static_cast<uint32_t>(regions.size());
  struct Lane {
    Mapper*              mapper = nullptr;
    // NOT std::async/std::thread: Darwin gives a secondary thread 512 KiB and
    // both ABC's recursive DFS/mapping helpers and the HHDS read-back walk
    // overflow that in -c dbg (see core/worker_pool.hpp kWorkerStackBytes).
    livehd::Async_worker result;
    size_t               job        = 0;
    uint64_t             projection = 0;
  };
  std::vector<Lane> lanes(std::min<size_t>(limit, regions.size()));
  for (size_t i = 0; i < lanes.size() && i < parallel_mappers_.size(); ++i) {
    lanes[i].mapper = parallel_mappers_[i].get();
  }
  std::vector<std::optional<Region_qor>> results(regions.size());
  std::exception_ptr                     failure;
  size_t                                 next     = 0;
  unsigned                               active   = 0;
  uint64_t                               reserved = 0;
  std::mutex                             done_mutex;
  std::condition_variable                done;
  // A refusal is a ROOT CAUSE, so the one the run reports must be the FIRST
  // region's in partition order -- not whichever lane happened to finish first.
  size_t                                 refusal_job = regions.size();
  std::string                            first_refusal, first_time_refusal;
  auto                                   note_refusal = [&](size_t job, const std::string& refusal, const std::string& timeout) {
    if ((refusal.empty() && timeout.empty()) || job >= refusal_job) {
      return;
    }
    refusal_job        = job;
    first_refusal      = refusal;
    first_time_refusal = timeout;
  };
  // Even a failed thread creation must join existing jobs before their captured
  // result buffers, condition variable and region views are destroyed.
  struct Drain {
    std::vector<Lane>& lanes;
    ~Drain() {
      for (auto& lane : lanes) {
        lane.result.join();
      }
    }
  } drain{lanes};

  auto harvest = [&] {
    for (auto& lane : lanes) {
      if (!lane.result.ready()) {
        continue;
      }
      try {
        lane.result.get();
      } catch (...) {
        if (!failure) {
          failure = std::current_exception();
        }
      }
      --active;
      reserved -= lane.projection;
      note_refusal(lane.job, lane.mapper->refusal_, lane.mapper->time_refusal_);
    }
  };

  // Nothing is running and no worker fits: there is no allocation to wait for,
  // so map this region on the caller's thread under the existing per-color and
  // process memory limits instead of spinning on the admission gate.
  auto map_serially = [&](size_t job) {
    const auto saved_id = next_region_id_;
    next_region_id_     = first_id + static_cast<uint32_t>(job);
    const auto n        = qor_.size();
    map_region(regions[job]);
    if (qor_.size() != n) {
      results[job] = std::move(qor_.back());
      qor_.pop_back();
    }
    next_region_id_ = saved_id;
    note_refusal(job, refusal_, time_refusal_);
  };

  while (next < regions.size() || active != 0) {
    harvest();
    if (failure || refusal_job != regions.size()) {
      next = regions.size();  // drain running work without launching more
    }
    bool launched = false;
    if (next < regions.size()) {
      for (auto& lane : lanes) {
        if (lane.result.valid()) {
          continue;
        }
        const uint64_t actual     = cost::process_footprint_bytes();
        const auto     projection = projections[next];
        if (!admit_abc_worker(parallel_stats_.memory_limit, actual, baseline, reserved, projection)) {
          ++parallel_stats_.memory_waits;
          if (active == 0) {
            map_serially(next++);
            launched = true;
          }
          break;
        }
        if (!lane.mapper) {
          std::lock_guard lock(graph_mutex_);
          auto            worker     = std::make_unique<Mapper>(startup_opts_);
          worker->coordinator_       = this;
          worker->outlib_            = outlib_;
          worker->flat_              = flat_;
          worker->incr_              = incr_;
          worker->region_opts_cli_   = region_opts_cli_;
          worker->graph_region_opts_ = graph_region_opts_;
          worker->dff_               = dff_;
          worker->dff_ladder_        = dff_ladder_;
          worker->dff_preset_        = dff_preset_;
          lane.mapper                = worker.get();
          parallel_mappers_.push_back(std::move(worker));
        }
        // Creating the worker's graph state may have waited for translation
        // under graph_mutex_. Recheck actual memory immediately before launch.
        if (!admit_abc_worker(parallel_stats_.memory_limit, cost::process_footprint_bytes(), baseline, reserved, projection)) {
          ++parallel_stats_.memory_waits;
          if (active == 0) {
            map_serially(next++);
            launched = true;
          }
          break;
        }
        // Nothing is committed until the thread actually starts: a failed
        // pthread_create must not leave a phantom reservation behind.
        lane.job                     = next;
        lane.projection              = projection;
        lane.mapper->next_region_id_ = first_id + static_cast<uint32_t>(lane.job);
        lane.result.start([&, worker = lane.mapper, job = lane.job] {
          struct Notify {
            std::mutex&              mu;
            std::condition_variable& cv;
            ~Notify() {
              {
                const std::lock_guard lock(mu);  // close the lost-wakeup window
              }
              cv.notify_one();
            }
          } notify{done_mutex, done};
          worker->map_region(regions[job]);
          if (!worker->qor_.empty()) {
            results[job] = std::move(worker->qor_.back());
            worker->qor_.clear();
          }
        });
        ++next;
        reserved += projection;
        ++active;
        parallel_stats_.peak_workers = std::max(parallel_stats_.peak_workers, active);
        launched                     = true;
        break;  // resample actual memory before each additional admission
      }
    }
    if (!launched && active != 0) {
      std::unique_lock lock(done_mutex);
      done.wait_for(lock, std::chrono::milliseconds(20));
    }
  }

  // All workers are joined: publish in partition order, independent of timing.
  // No ABC-owned pointer crosses sessions. Boundary refinement gets its own
  // session; only graph snapshots, options and plain QoR values are merged.
  for (auto& lane : lanes) {
    if (!lane.mapper) {
      continue;
    }
    auto& worker  = *lane.mapper;
    lib_loaded_  |= worker.lib_loaded_;
    region_delay_targets_.insert(worker.region_delay_targets_.begin(), worker.region_delay_targets_.end());
    worker.region_delay_targets_.clear();
  }
  if (refusal_job != regions.size()) {
    refusal_      = first_refusal;  // the lowest-index region's, whoever mapped it
    time_refusal_ = first_time_refusal;
  }
  for (auto& row : results) {
    if (row) {
      qor_.push_back(std::move(*row));
    }
  }
  const auto order = [&](std::string_view module) {
    return std::find_if(regions.begin(), regions.end(), [&](const auto& rb) { return rb.module_name == module; }) - regions.begin();
  };
  std::stable_sort(ware_regions_.begin() + static_cast<std::ptrdiff_t>(ware_begin),
                   ware_regions_.end(),
                   [&](const auto& a, const auto& b) { return order(a.rb.module_name) < order(b.rb.module_name); });
  if (failure) {
    std::rethrow_exception(failure);
  }
}

void Mapper::map_region(const livehd::partition::Region_body& rb) {
  std::unique_lock graph_lock(coordinator_ ? coordinator_->graph_mutex_ : graph_mutex_);
  if (!coordinator_) {
    parallel_stats_.limit        = synthesis_thread_limit(opts_.threads, std::thread::hardware_concurrency());
    parallel_stats_.memory_limit = parallel_memory_limit();
  }
  struct Frame_restore {
    Abc_Frame_t* previous = Abc_FrameReadGlobalFrame();
    ~Frame_restore() { Abc_FrameLeave(previous); }
  } frame_restore;
  // A refusal already happened: work() will make it fatal once the ABC frame is
  // torn down, so translating the remaining regions can only burn time and
  // overwrite the FIRST refusal -- the one that is the actual root cause.
  if (!refusal_.empty() || !time_refusal_.empty()) {
    return;
  }

  // Source excerpts for this region's diagnostics. The srcids stamped on the
  // region's nodes resolve through the SOURCE graph's locator (which chains to
  // the library's shared srcmap), so every refusal below can print the original
  // Pyrope/Verilog line, not just a node id. Restored on scope exit.
  livehd::diag::Locator_scope diag_scope(rb.src != nullptr ? &rb.src->source_locator() : nullptr);

  // Per-region wall time: the only way to tell a cache that hits a lot from a
  // cache that saves time. A hit on a 200ms region and a miss on a 200s one
  // count the same in hits/misses and nothing alike in the total.
  const auto t_start = std::chrono::steady_clock::now();
  const auto since
      = [&t_start] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count(); };
  const uint64_t process_peak_before = cost::process_peak_rss_bytes();
  uint64_t       sampled_peak_rss    = cost::process_footprint_bytes();
  // The ACCOUNTING baseline for color_peak_rss_kb: this region's footprint on
  // entry, captured unconditionally. Deliberately not the admission baseline
  // `rss_before` below, which is pinned to 0 under allow_oversize -- reusing it
  // made every color report the whole sticky process peak as its own growth,
  // exactly in the large-design mode the per-color number exists to explain.
  const uint64_t rss_entry           = sampled_peak_rss;
  const auto     trace_stage         = [&](std::string_view stage) {
    sampled_peak_rss = std::max(sampled_peak_rss, cost::process_footprint_bytes());
    if (!opts_.verbose) {
      return;
    }
    std::print("[pass.abc] region '{}': stage {} at {:.0f} ms\n", rb.module_name, stage, since());
    std::fflush(stdout);
  };

  // Reporting metadata belongs to the freshly emitted mapped library, not to
  // the persistent cache row. Stamp it once on the graph input on every exit
  // path (including a cache hit and the native-wiring fast path), after
  // reuse_hit may have replaced the complete body. Do not duplicate the region
  // string on every mapped cell; physical flattening propagates the compact id
  // only into its transient scratch nodes for OpenTimer.
  bool           resynthesized = incr_ == nullptr;
  const uint32_t region_id     = next_region_id_++;
  struct Region_report_stamp {
    const livehd::partition::Region_body* rb;
    uint32_t                              region_id;
    const bool*                           resynthesized;
    ~Region_report_stamp() {
      auto input = rb->body->get_input_node();
      input.attr(livehd::attrs::synth_region).set(rb->module_name);
      input.attr(livehd::attrs::synth_region_id).set(region_id);
      input.attr(livehd::attrs::color).set(rb->color);
      if (*resynthesized) {
        input.attr(livehd::attrs::resynth).set({});
      } else {
        input.attr(livehd::attrs::resynth).del();
      }
    }
  } report_stamp{&rb, region_id, &resynthesized};

  // Per-region options are temporary (every helper below reads opts_) and are
  // restored on every exit path.
  const Map_options saved_opts = opts_;
  struct Opts_restore {
    Map_options*       dst;
    const Map_options* src;
    std::string*       budget_flag;  // the `{B}` substitution is per region too
    ~Opts_restore() {
      *dst = *src;
      budget_flag->clear();
    }
  } opts_restore{&opts_, &saved_opts, &budget_flag_};
  // Structural input size belongs in every QoR row, including cache hits. It
  // makes recipe/runtime changes explainable without relying on module names:
  // mapped gates are only known after ABC and can move with the very recipe
  // being compared, while these two values describe the invariant input cone.
  const uint64_t input_nodes = rb.nodes.size();
  uint64_t       input_ge    = 0;
  uint64_t       pred_aig    = 0;
  for (const auto& node : rb.nodes) {
    input_ge += gu::synthesis_ge_weight(node);
    pred_aig  = gu::sat_add(pred_aig, gu::predict_abc_size(node));
  }

  uint64_t register_bits = 0;
  for (const auto& node : rb.nodes) {
    if (!gu::is_type_flop(node)) {
      continue;
    }
    const int bits  = gu::bits_of(node.create_driver_pin(0));
    register_bits  += static_cast<uint64_t>(std::max(bits, 1)) * pipeline_depth(node);
  }
  // Off by default (register_max_bits=0): every flop maps, as yosys does. The
  // old 4096-bit default was tripped by a single bit-blasted 64x64 memory
  // (mem_lower puts the entry flops in the memory's region) and silently
  // handed those flops to lhdtrack's yosys normalize instead of pass.abc. A
  // diag, not a print: the decision changes the netlist's cell mix and has to
  // land in the diagnostics stream next to memory-max-bits, where a QoR
  // reader looks for "why is this register native".
  if (opts_.map_register && opts_.register_max_bits != 0 && register_bits > opts_.register_max_bits) {
    opts_.map_register = false;
    livehd::diag::info("pass.abc", "register-kept-native", "unsupported")
        .msg(
            "pass.abc region '{}': keeping {} register bits native (above register_max_bits={}); the data cones are still "
            "mapped",
            rb.module_name,
            register_bits,
            opts_.register_max_bits)
        .emit();
  }

  // A coarse size tier is intentionally selected before color-keyed overrides:
  // a user naming one specific region always has the final say. `input_ge` is
  // invariant source-logic cost, unlike mapped gates, so cache recipes and
  // threshold decisions remain stable when the mapping flow changes.
  bool       tool_owned_flow = opts_.flow.empty();
  const bool control_tier    = rb.ctrl && opts_.ctrl_flow != "inherit";
  //
  // Every tier goes through resolve_flow, not the raw string: the size TIERS are
  // tool-chosen defaults like kCombFlow, so max_fanout's buffering tail applies
  // to them too. Only an explicit user `flow` is left alone (it owns its command
  // list and can place `{F}` itself). Without this a region over large_ge
  // silently mapped with NO fanout cap -- minion kept a 3562-sink mapped net
  // that way while dino, which has no such region, capped correctly at 16.
  //
  // The `*_flow selected` lines are the only external evidence that a tier fired
  // (lhd/tests/lhd_abc_test.sh greps for them); keep them on every branch that
  // substitutes a recipe.
  if (control_tier) {
    opts_.area_relax_pct = opts_.ctrl_area_relax;
    opts_.flow           = resolve_flow(opts_.ctrl_flow);
    tool_owned_flow      = true;
    if (opts_.verbose) {
      std::print("[pass.abc] region '{}': ctrl_flow selected ({} GE)\n", rb.module_name, input_ge);
    }
    // Large control cones still receive the protected inexpensive tier.
    if (opts_.large_ge && input_ge >= opts_.large_ge && !opts_.large_flow.empty()) {
      opts_.flow = resolve_flow(opts_.large_flow);
      if (opts_.verbose) {
        std::print("[pass.abc] region '{}': large_flow selected ({} GE >= {})\n", rb.module_name, input_ge, opts_.large_ge);
      }
    }
  } else {
    if (opts_.small_ge && !opts_.small_flow.empty() && input_ge >= opts_.small_min_ge && input_ge <= opts_.small_ge) {
      opts_.flow = resolve_flow(opts_.small_flow);
      if (opts_.verbose) {
        std::print("[pass.abc] region '{}': small_flow selected ({} <= {} GE <= {})\n",
                   rb.module_name,
                   opts_.small_min_ge,
                   input_ge,
                   opts_.small_ge);
      }
    }
    if (opts_.flow.empty() && opts_.large_ge && !opts_.large_flow.empty() && input_ge >= opts_.large_ge) {
      opts_.flow = resolve_flow(opts_.large_flow);
      if (opts_.verbose) {
        std::print("[pass.abc] region '{}': large_flow selected ({} GE >= {})\n", rb.module_name, input_ge, opts_.large_ge);
      }
    }
  }
  if (!ware_trial_ && apply_region_overrides(rb)) {
    tool_owned_flow = false;
  }
  region_delay_targets_[rb.module_name] = ware_delay_target(opts_.delay);
  struct Control_deadline {
    std::mutex              mu;
    std::condition_variable cv;
    bool                    done = false;
    std::thread             worker;
    Control_deadline(uint64_t ms, const std::string& name) {
      if (!ms) {
        return;
      }
      worker = std::thread([this, ms, name] {
        std::unique_lock lock(mu);
        if (!cv.wait_for(lock, std::chrono::milliseconds(ms), [this] { return done; })) {
          std::fprintf(stderr,
                       "[pass.abc] control region '%s' exceeded ctrl_time_budget_ms=%llu; stopping synthesis\n",
                       name.c_str(),
                       static_cast<unsigned long long>(ms));
          std::fflush(stderr);
          std::_Exit(6);
        }
      });
    }
    ~Control_deadline() {
      {
        std::lock_guard lock(mu);
        done = true;
      }
      cv.notify_one();
      if (worker.joinable()) {
        worker.join();
      }
    }
  } ctrl_deadline(control_tier ? opts_.ctrl_time_budget_ms : 0, rb.module_name);
  // The region's delay BUDGET: the target minus the register margin when the
  // region holds flops (mapped or native -- a native flop is mapped to the
  // same cell by whoever times the netlist, so its overhead is on the path
  // either way). Spelled into `{B}` before ANY flow string of this region is
  // resolved: the built-in tails' `dnsize`/`upsize` take it as `-D`, and the
  // recipe below therefore carries it verbatim. ABC's `-D` is an integer
  // (atoi), so the budget is floored to whole picoseconds and the ladder below
  // compares against the same value it sized to.
  float delay_target = 0.0f;
  {
    char*       end = nullptr;
    const float t   = std::strtof(opts_.delay.c_str(), &end);
    if (!opts_.delay.empty() && end != opts_.delay.c_str() && *end == '\0' && t > 0.0f) {
      delay_target = t;
    }
  }
  ensure_dff_cells();  // the auto margin needs the DFF pick (start() is lazy, below the cache lookup)
  const float budget      = delay_target > 0.0f ? std::floor(region_budget(delay_target, register_bits > 0)) : -1.0f;
  budget_flag_            = budget > 0.0f ? std::format("-D {}", static_cast<int>(budget)) : std::string{};
  // Is the ABC command list this region runs the BUILT-IN one (kSeqFlow /
  // kCombFlow + tail)? Stricter than `tool_owned_flow`, which stays true under
  // a size-tier `small_flow`/`large_flow` -- a user-authored command list that
  // may retime (`dretime`) or sequentially sweep (`scorr`/`lcorr`). The QN
  // AIG-side encoding (Seq_flop::d_inverted) is exact only under combinational
  // transformations, so it is gated on THIS flag, not on flow ownership.
  const bool custom_area  = opts_.flow.empty() && opts_.delay.empty() && !opts_.area_flow.empty() && opts_.area_flow != "none";
  const bool builtin_flow = opts_.flow.empty() && !custom_area;
  if (custom_area) {
    tool_owned_flow = false;
  }
  if (opts_.verbose) {
    uint64_t input_bits  = 0;
    uint64_t output_bits = 0;
    for (const auto& port : rb.inputs) {
      input_bits += static_cast<uint64_t>(std::max(port.bits, 1));
    }
    for (const auto& port : rb.outputs) {
      output_bits += static_cast<uint64_t>(std::max(port.bits, 1));
    }
    std::print("[pass.abc] mapping region '{}': {} nodes, {} GE, {} register bits\n",
               rb.module_name,
               input_nodes,
               input_ge,
               register_bits);
    std::print("[pass.abc] region '{}': {} input port(s)/{} bits, {} output port(s)/{} bits\n",
               rb.module_name,
               rb.inputs.size(),
               input_bits,
               rb.outputs.size(),
               output_bits);
    std::fflush(stdout);
  }

  // Incremental reuse (2opt-incr A+C), lgraph-compare edition: the PARTITIONER
  // rebuilt the region's pre-ABC logic into a throwaway lib (rb.pre_body, via the
  // SAME build_module construction the classic path uses -- a byte-stable
  // compare artifact, unlike a hand re-derivation which drifts). Structurally
  // compare it (plus the resolved recipe) against the cache, and on a match
  // REPLACE this region's body with the cached mapped netlist IN PLACE -- ABC
  // never starts. `recipe`/`pre_g` live to the store site below (a miss
  // snapshots them). Null when reuse-ineligible or flattening (uncacheable).
  // The effective per-region state mode is part of the cache recipe. The comb
  // and seq command strings can be identical, but their read-back semantics are
  // not: one carries flops through ABC and the other preserves native state.
  if (!ware_trial_) {
    // One ware list per run: a worker records into the coordinator (still under
    // graph_lock, so the appends stay serialized) with ITS region's options.
    (coordinator_ ? *coordinator_ : *this).remember_ware(rb, opts_);
  }
  std::shared_ptr<const Satopt_result> satopt_facts;
  if (opts_.satopt && rb.src != nullptr) {
    auto& results    = coordinator_ ? coordinator_->satopt_results_ : satopt_results_;
    auto [it, fresh] = results.try_emplace(rb.src);
    if (fresh) {
      it->second = satopt(rb.src, incr_ ? incr_->dir() + "/../satopt_cache" : "");
    }
    satopt_facts = it->second;
  }
  std::string              recipe = resolve_recipe();
  std::vector<std::string> fact_keys;
  if (satopt_facts) {
    auto keys = satopt_region_facts(*satopt_facts, rb);
    if (keys) {
      fact_keys = std::move(*keys);
    } else {
      satopt_facts.reset();
    }  // no stable subject identity: map without facts
  }

  recipe += rb.ctrl ? "|ctrl=1" : "|ctrl=0";
  // The frame's driving cell (resolve_boundary_defaults) makes `buffer` tree
  // every input's fanout out of band of the flow string: spell the decision.
  // From the OPTIONS, not the frame -- an all-hit run never starts ABC, and
  // the resolved cell is a function of the library (in the cache salt) and
  // `boundary_drive` alone.
  recipe += "|pi_drive=";
  recipe
      += (opts_.boundary_buffer && opts_.max_fanout != 0) ? (opts_.boundary_drive.empty() ? "auto" : opts_.boundary_drive) : "none";
  recipe             += opts_.map_register ? "\n# livehd-register=abc" : "\n# livehd-register=native";
  hhds::Graph* pre_g  = (incr_ != nullptr && rb.reuse_eligible) ? rb.pre_body : nullptr;
  // EXPERIMENTAL (ABC_INCR_COMPARE_ONLY): exercise compare/store with NO ABC -- a
  // fast diagnostic for why a region misses on a comment edit.
  if (incr_ != nullptr && std::getenv("ABC_INCR_COMPARE_ONLY") != nullptr) {
    bool hit = false;
    if (pre_g != nullptr) {
      hit = incr_->lookup_compare(rb, pre_g, recipe, fact_keys).hit;
      incr_->store_pre(rb, *rb.pre_lib, rb.pre_name, recipe, fact_keys);
    }
    std::print("COMPARE {} {}\n",
               rb.module_name,
               !rb.reuse_eligible ? "INELIGIBLE" : (pre_g == nullptr ? "REBUILD-FAIL" : (hit ? "HIT" : "MISS")));
    Region_qor q;
    q.satopt_facts = fact_keys.size();
    q.module       = rb.module_name;
    q.color        = rb.color;
    q.ctrl         = rb.ctrl;
    q.input_nodes  = input_nodes;
    q.input_ge     = input_ge;
    q.pred_aig     = pred_aig;
    q.cache        = hit ? "hit" : "miss";
    q.resynth      = !hit;
    qor_.push_back(std::move(q));
    report_completion(qor_.back());
    return;
  }
  if (incr_ != nullptr && rb.reuse_eligible) {
    if (pre_g != nullptr) {
      auto res = incr_->lookup_compare(rb, pre_g, recipe, fact_keys);
      if (res.hit && incr_->reuse_hit(rb, res, outlib_)) {
        Region_qor q;
        q.satopt_facts = fact_keys.size();
        q.module       = rb.module_name;
        q.color        = rb.color;
        q.ctrl         = rb.ctrl;
        q.input_nodes  = input_nodes;
        q.input_ge     = input_ge;
        q.pred_aig     = pred_aig;
        q.gates        = res.row->gates;
        q.area         = res.row->area;
        q.delay        = res.row->delay;
        q.logic_depth  = res.row->logic_depth;
        q.crit_src     = res.row->crit_src;
        q.crit_output  = res.crit_output;
        q.div_blackbox = res.row->div_blackbox;
        q.cache        = "hit";
        q.resynth      = false;
        q.ms           = since();
        qor_.push_back(std::move(q));
        report_completion(qor_.back());
        return;
      }
    }
    incr_->note_miss();
  }

  resynthesized = true;

  // Do not pay Abc_Start/read_lib for an all-hit rebuild. The cache salt has
  // already folded the Liberty content and run-level mapping modes, while the
  // exact pre-body comparison authorized the reused result. Only a real miss
  // needs the mapper process and parsed library.
  // Session initialization and Liberty parsing touch only this worker's ABC
  // frame. Let them overlap too; large timing libraries dominate small colors.
  if (coordinator_) {
    graph_lock.unlock();
  }
  bool abc_ready = false;
  try {
    abc_ready = start();
  } catch (...) {
    if (!graph_lock.owns_lock()) {
      graph_lock.lock();
    }
    throw;
  }
  if (!graph_lock.owns_lock()) {
    graph_lock.lock();
  }
  if (!abc_ready) {
    return;  // diagnostic already emitted
  }

  auto* manNtk  = Abc_NtkAlloc(ABC_NTK_NETLIST, ABC_FUNC_AIG, 1);
  manNtk->pName = Extra_UtilStrsav(const_cast<char*>(rb.module_name.c_str()));
  auto* manFunc = static_cast<Hop_Man_t*>(manNtk->pManFunc);

  // bit i of an original driver pin -> the ABC net carrying it. The OUTER map is
  // a node_hash_map (pointer-stable values): several sites bind `auto& slots =
  // bitnet[pin]` and then keep writing through it while `abc_bit` inserts *new*
  // outer keys (input/const drivers). A flat_hash_map would rehash on those
  // inserts and leave `slots` dangling — harmless for a small colored region but
  // a use-after-free once an uncolored design folds the whole graph into one
  // large region. node_hash_map keeps each inner map's address fixed across
  // outer rehashes, so every held `slots` reference stays valid.
  absl::node_hash_map<hhds::Pin_class, absl::flat_hash_map<int, Abc_Obj_t*>> bitnet;
  // Region node membership (handles into rb.src).
  absl::flat_hash_set<hhds::Node_class>                                      region;
  for (const auto& n : rb.nodes) {
    region.insert(n);
  }

  // --- ABC gate constructors (each returns the new gate's output net) ---
  auto new_net = [&](Abc_Obj_t* node) {
    auto* net = Abc_NtkCreateNet(manNtk);
    Abc_ObjAddFanin(net, node);
    return net;
  };
  Abc_Obj_t* const1     = nullptr;
  Abc_Obj_t* const0     = nullptr;
  auto       abc_const1 = [&]() {
    if (const1 == nullptr) {
      auto* node  = Abc_NtkCreateNode(manNtk);
      node->pData = Hop_ManConst1(manFunc);
      const1      = new_net(node);
    }
    return const1;
  };
  auto abc_const0 = [&]() {
    if (const0 == nullptr) {
      auto* node  = Abc_NtkCreateNode(manNtk);
      node->pData = Hop_Not(Hop_ManConst1(manFunc));
      const0      = new_net(node);
    }
    return const0;
  };
  auto abc_not = [&](Abc_Obj_t* a) {
    if (a == abc_const1()) {
      return abc_const0();
    }
    if (a == abc_const0()) {
      return abc_const1();
    }
    auto* node  = Abc_NtkCreateNode(manNtk);
    node->pData = Hop_Not(Hop_IthVar(manFunc, 0));
    Abc_ObjAddFanin(node, a);
    return new_net(node);
  };
  auto abc_bin = [&](Abc_Obj_t* a, Abc_Obj_t* b, char kind) {
    auto* zero = abc_const0();
    auto* one  = abc_const1();
    if (kind == '&') {
      if (a == zero || b == zero) {
        return zero;
      }
      if (a == one) {
        return b;
      }
      if (b == one || a == b) {
        return a;
      }
    } else if (kind == '|') {
      if (a == one || b == one) {
        return one;
      }
      if (a == zero) {
        return b;
      }
      if (b == zero || a == b) {
        return a;
      }
    } else {
      if (a == zero) {
        return b;
      }
      if (b == zero) {
        return a;
      }
      if (a == b) {
        return zero;
      }
    }
    auto* node  = Abc_NtkCreateNode(manNtk);
    node->pData = kind == '&' ? Hop_CreateAnd(manFunc, 2) : kind == '|' ? Hop_CreateOr(manFunc, 2) : Hop_CreateExor(manFunc, 2);
    Abc_ObjAddFanin(node, a);
    Abc_ObjAddFanin(node, b);
    return new_net(node);
  };
  auto abc_const_bit = [&](bool v) { return v ? abc_const1() : abc_const0(); };
  // 2:1 mux on ABC nets: sel ? t : f  ==  (sel & t) | (~sel & f).
  auto abc_mux       = [&](Abc_Obj_t* sel, Abc_Obj_t* t, Abc_Obj_t* f) {
    return abc_bin(abc_bin(sel, t, '&'), abc_bin(abc_not(sel), f, '&'), '|');
  };

  // Set when a region node cannot be mapped (unsupported cell / mask); the
  // region is abandoned after the blast loop. Declared here so abc_bit can
  // suppress its per-bit unmaterialized-driver diagnostics once the ONE real
  // unsupported-cell error has fired (the producer wrote no slots, so every
  // downstream read would otherwise flood the log).
  bool unsupported = false;

  // Per-node refusal, with provenance. Unlike the region-level message it
  // replaces, every record now carries the offending node's identity and its
  // original source location -- so two of these no longer collapse into one
  // line by the sink's (code, span, message) dedup. That is the point (a
  // 12k-node region can hold dozens of independently broken cells), but it
  // also means a systematically broken region would print one line per node:
  // report the first kMaxRefusals in full and summarize the rest.
  constexpr size_t kMaxRefusals = 10;
  size_t           refusals     = 0;
  const auto       refuse       = [&](const hhds::Node_class& bad,
                                      std::string_view        code,
                                      std::string_view        category,
                                      std::string_view        what,
                                      std::string_view        hint     = {},
                                      const hhds::Pin_class&  note_pin = {},
                                      std::string_view        note_msg = {}) {
    unsupported = true;
    if (refusals++ >= kMaxRefusals) {
      return;  // counted; the post-loop summary reports the total
    }
    auto b = livehd::diag::err("pass.abc", code, category);
    b.at(node_span(rb, bad));
    b.msg("pass.abc: {} in region '{}': {}", node_identity(bad), rb.module_name, what);
    if (!hint.empty()) {
      b.hint(hint);
    }
    if (!note_pin.is_invalid() && !note_msg.empty()) {
      if (auto sp = node_span(rb, note_pin.get_master_node()); !sp.is_null()) {
        b.note(note_msg, sp);
      }
    }
    b.emit();
  };

  // SHL / SRA share this: the amount is a constant the mapper cannot use.
  const auto refuse_shift_amount
      = [&](const hhds::Node_class& bad, std::string_view op_name, const Dlop& amt, const hhds::Pin_class& amt_pin) {
          // UNKNOWN is tested FIRST, and that order is load-bearing: Dlop::unknown()
          // fills the base plane with -1 (hlop init_unknown) and Dlop::is_negative()
          // reads only that plane, so EVERY x-carrying value answers "negative".
          // `!has_unknowns() && is_negative()` is the idiom upass/tolg already uses
          // (pin_can_be_negative); with the tests the other way round every unknown
          // amount is misreported as a livehd internal bug.
          if (!amt.has_unknowns() && amt.is_negative()) {
            refuse(
                bad,
                "negative-shift-amount",
                "internal",
                std::format("{} shift amount is the NEGATIVE constant {} -- a shift count must be >= 0", op_name, const_brief(amt)),
                "no pass should have produced this: upass.bitwidth rejects a negative shift count, so a negative "
                "constant reaching synthesis is a folding/lowering bug in livehd, not a design error",
                amt_pin,
                "shift amount defined here");
            return;
          }
          refuse(bad,
                 "unknown-shift-amount",
                 "unsupported",
                 std::format("{} shift amount is the constant {}, which carries unknown (?) bits -- ABC has no X value, so "
                             "the shift cannot be technology-mapped",
                             op_name,
                             const_brief(amt)),
                 "a RUNTIME (non-constant) shift amount is supported and becomes a barrel shifter; only an UNKNOWN constant "
                 "is not. Trace the amount back to the value that was never given a definite assignment",
                 amt_pin,
                 "shift amount defined here");
        };

  Wiring_blaster<Abc_Obj_t*> wiring_blaster;

  // Region inputs are bit-demanded, not eagerly exploded. Wide packed-state
  // ports often expose tens of thousands of bits while this region reads only
  // a small slice; creating every unused PI also forces readback to build an
  // equally large selector forest. `pi_order` records the exact lazy creation
  // order, so the mapped PI readback remains positional and deterministic.
  enum class Pi_kind : uint8_t { region_input, bbox_output };
  struct Pi_origin {
    Pi_kind kind;
    size_t  index;
  };
  std::vector<std::pair<size_t, int>>          pi_order;
  std::vector<Pi_origin>                       all_pi_order;
  absl::flat_hash_map<hhds::Pin_class, size_t> region_input_index;
  // Native boundary outputs follow the same demand-driven rule as region
  // inputs.  Wide packed-wiring boundaries can expose tens of thousands of
  // bits while the mapped cone reads only a handful; eagerly creating every PI
  // made those unused bits survive through ABC readback as an equally large
  // selector forest.  The origin table is populated by the boundary scan below
  // before any combinational node is bit-blasted.
  using Bbox_output_origin = std::pair<int, int>;  // (bbox index, output index)
  absl::flat_hash_map<hhds::Pin_class, Bbox_output_origin> bbox_output_index;
  std::vector<std::tuple<int, int, int>>                   bbox_pi;  // demanded PI -> (bbox, output, bit)
  for (size_t pi = 0; pi < rb.inputs.size(); ++pi) {
    region_input_index.emplace(rb.inputs[pi].src_driver, pi);
  }

  // --- bit i of an original driver pin, with sign/zero extension past width ---
  std::function<Abc_Obj_t*(const hhds::Pin_class&, int)> abc_bit = [&](const hhds::Pin_class& drv, int i) -> Abc_Obj_t* {
    if (drv.is_invalid()) {
      return abc_const_bit(false);
    }
    int  w    = gu::bits_of(drv);
    bool sign = !gu::is_unsign(drv);
    int  eff  = i;
    if (w != 0 && i >= w) {
      eff = sign ? w - 1 : -1;  // -1 => constant 0 above an unsigned width
    }
    if (eff < 0) {
      return abc_const_bit(false);
    }
    auto& slots = bitnet[drv];
    if (auto it = slots.find(eff); it != slots.end()) {
      return it->second;
    }
    if (drv.is_const()) {
      const auto& val = gu::const_of(drv);
      auto*       net = abc_const_bit(val.bit_test(eff));
      slots[eff]      = net;
      return net;
    }
    if (auto it = region_input_index.find(drv); it != region_input_index.end()) {
      const size_t pi  = it->second;
      auto*        obj = Abc_NtkCreatePi(manNtk);
      auto*        net = Abc_NtkCreateNet(manNtk);
      auto         nm  = std::format("{}_b{}", rb.inputs[pi].name, eff);
      Abc_ObjAssignName(net, const_cast<char*>(nm.c_str()), nullptr);
      Abc_ObjAddFanin(net, obj);
      slots[eff] = net;
      all_pi_order.push_back({Pi_kind::region_input, pi_order.size()});
      pi_order.emplace_back(pi, eff);
      return net;
    }
    if (auto it = bbox_output_index.find(drv); it != bbox_output_index.end()) {
      auto* obj = Abc_NtkCreatePi(manNtk);
      auto* net = Abc_NtkCreateNet(manNtk);
      Abc_ObjAddFanin(net, obj);
      slots[eff] = net;
      all_pi_order.push_back({Pi_kind::bbox_output, bbox_pi.size()});
      bbox_pi.emplace_back(it->second.first, it->second.second, eff);
      return net;
    }
    auto master = drv.get_master_node();
    if (gu::type_op_of(master) == Ntype_op::Set_mask || gu::type_op_of(master) == Ntype_op::Concat) {
      auto* net  = wiring_blaster.bit(drv, eff, abc_bit, abc_const0, [&](std::string_view why) {
        if (!unsupported) {
          refuse(master, "unsupported-cell", "unsupported", why);
        }
      });
      slots[eff] = net;
      return net;
    }
    // A region-internal node not yet materialized (a genuine node-level cycle
    // the fixpoint scheduler could not resolve) or an unexpected boundary:
    // use a temporary constant only to let translation unwind, but reject the
    // region. Accepting that placeholder would silently miscompile the whole
    // downstream cone. Suppress follow-on diagnostics once the first missing
    // producer has identified the region-level failure.
    if (!unsupported) {
      refuse(drv.get_master_node(),
             "unmaterialized-driver",
             "internal",
             std::format("bit {} of this driver could not be materialized", eff),
             "the colored region contains a combinational cycle or an invalid boundary; refusing to emit a wrong netlist");
    }
    auto* net  = abc_const_bit(false);
    slots[eff] = net;
    return net;
  };

  auto real_width = [&](const hhds::Pin_class& p) -> int { return std::max(1, gu::real_width(p)); };

  // arith::Ops view over the gate constructors, for the Sum/comparator builders.
  Abc_bit_ops ops;
  ops.konst  = abc_const_bit;
  ops.not_   = abc_not;
  ops.and_fn = [&](Abc_Obj_t* x, Abc_Obj_t* y) { return abc_bin(x, y, '&'); };
  ops.or_fn  = [&](Abc_Obj_t* x, Abc_Obj_t* y) { return abc_bin(x, y, '|'); };
  ops.xor_fn = [&](Abc_Obj_t* x, Abc_Obj_t* y) { return abc_bin(x, y, '^'); };

  // --- sequential: each region Flop -> N 1-bit ABC latches (seq=true only) ---
  // The latch output (Q) seeds bitnet so the combinational cells read it as a
  // source; the latch input (D) is wired to the folded next-state cone AFTER the
  // comb loop (it may depend on logic that has not been bit-blasted yet). On
  // read-back a latch becomes a plain Liberty DFF cell (register=true) or a
  // native flop; either way the latch is what lets ABC optimize across the
  // register boundary.
  struct Seq_flop {
    hhds::Node_class        node;
    std::string             root;
    int                     bits = 0;
    hhds::Pin_class         q_pin;
    hhds::Pin_class         din_drv, en_drv, rst_drv, rval_drv, clk_drv;
    bool                    neg_reset = false;
    bool                    neg_clock = false;
    // The register has a SYNCHRONOUS reset (`reset_pin` driven, `async` not
    // asserted): the reset is folded into the latch's D cone as
    // `rst ? rval : (en ? din : Q)` and `initial` is the RESET value, not a
    // power-on one. Snapshotted like has_init (the read-back decides per bit
    // whether an init must keep a native flop, and `rst_drv` is a source-side
    // handle the rewritten region no longer resolves).
    bool                    has_reset = false;
    // The `initial` (power-on / reset) value, SNAPSHOT at crossing time. The
    // read-back below runs after map_region has rewritten the region, so the
    // source const node behind `rval_drv` may already be gone -- re-reading the
    // pin there silently answers "no init" and refines an init-carrying flop
    // into a plain DFF cell (measured: abc_flat_names lost `a.r`/`b.r`).
    bool                    has_init  = false;
    Dlop                    init_val;
    // The latch carries ~next_state and the read-back wires the DFF cell's QN
    // pin as Q. Set only for an init-less register mapped to a QN-only cell
    // under the BUILT-IN flow: the inversion the QN cell needs then lands in
    // the mapper's own phase assignment (`&nf` costs both phases of every
    // node), which is where it is cheapest in aggregate -- +52 um^2 of comb
    // over the 10-test asap7 set (mixed per test: br_credit_sender -6.8,
    // br_arb_rr +3.3, br_ram_flops +28) against ~560 um^2 of flop savings,
    // where the flow-independent read-back absorption (pass 1b twin swap /
    // D inverter) costs ~0.03 per flop (~+230 um^2). The encoding is exact only
    // under combinational transformations (the machine ABC sees is
    // BO' = ~F(BO, x)), hence the flow gate; every other latch keeps the honest
    // next state and takes the read-back path.
    bool                    d_inverted = false;
    std::vector<Abc_Obj_t*> bi;        // per-bit latch BI (data-in terminal)
    std::vector<Abc_Obj_t*> qbits;     // this stage, including hidden pipeline stages
    std::vector<Abc_Obj_t*> previous;  // preceding stage; empty for the din stage
  };
  // Region-input driver -> port name. Used twice: to reconnect a flop
  // boundary's control pins natively (see the boundary scan below), and to
  // decide whether a register's clock even HAS a native source on read-back.
  absl::flat_hash_map<hhds::Pin_class, std::string> region_in_name;
  for (const auto& port : rb.inputs) {
    region_in_name.emplace(port.src_driver, port.name);
  }

  // A region consisting solely of a constant shift is pure bus wiring (plus
  // sign extension for SRA). ABC turns its padding into one mapped object per
  // output bit (Rob has hundreds of these regions, growing to 10k bits each).
  // Rebuild the typed wiring node directly; OpenTimer tracks constant shift bit
  // identity and no Liberty delay is being skipped because there is no Boolean
  // gate here.
  const auto single_shift_op = rb.nodes.size() == 1 ? gu::type_op_of(rb.nodes.front()) : Ntype_op::Invalid;
  if (single_shift_op == Ntype_op::SHL || single_shift_op == Ntype_op::SRA) {
    const auto src_node = rb.nodes.front();
    const auto a        = gu::get_driver_of_sink_name(src_node, "a");
    const auto b        = gu::get_driver_of_sink_name(src_node, "b");
    auto       ait      = region_in_name.find(a);
    if (!a.is_invalid() && b.is_const() && (a.is_const() || ait != region_in_name.end())) {
      auto node = gu::create_typed_node(*rb.body, single_shift_op);
      if (a.is_const()) {
        gu::create_const(*rb.body, gu::const_of(a)).connect_sink(gu::setup_sink_by_name(node, "a"));
      } else {
        rb.body->get_input_pin(ait->second).connect_sink(gu::setup_sink_by_name(node, "a"));
      }
      gu::create_const(*rb.body, gu::const_of(b)).connect_sink(gu::setup_sink_by_name(node, "b"));
      auto out  = node.create_driver_pin(0);
      int  bits = 1;
      for (const auto& port : rb.outputs) {
        bits = std::max(bits, port.bits);
      }
      gu::set_bits(out, bits);
      if (!gu::is_unsign(src_node.create_driver_pin(0))) {
        gu::set_sign(out);
      }
      if (auto pn = gu::pin_name_of(src_node.create_driver_pin(0)); !pn.empty()) {
        gu::set_pin_name(out, pn);
      }
      for (const auto& port : rb.outputs) {
        out.connect_sink(rb.body->get_output_pin(port.name));
      }
      Region_qor q;
      q.satopt_facts = fact_keys.size();
      q.module       = rb.module_name;
      q.color        = rb.color;
      q.ctrl         = rb.ctrl;
      q.input_nodes  = input_nodes;
      q.input_ge     = input_ge;
      q.pred_aig     = pred_aig;
      q.gates        = 0;
      q.area         = 0;
      q.delay        = 0;
      q.cache        = "miss";
      q.resynth      = true;
      q.ms           = since();
      qor_.push_back(std::move(q));
      Abc_NtkDelete(manNtk);
      report_completion(qor_.back());
      return;
    }
  }

  std::vector<Seq_flop>                 flops;
  // Registers that cannot be represented by the selected plain Liberty DFF
  // stay native boundaries. A derived clock has no native source after latch
  // read-back. An ASYNCHRONOUS reset is an event, not data: folding it into D
  // would make the reset land only on a clock edge (and pass/lec's encode.cpp
  // models async and sync resets differently under the phase schedule). A
  // SYNCHRONOUS reset is exactly a D-cone mux with priority over the enable
  // (`if (rst) q <= rval; else if (en) q <= din;` is what cgen emits and what
  // the LEC encodes, ITE(rst, init, ITE(en, din, q))), so it crosses ABC like
  // any other next-state logic and maps to a plain DFF cell; its `initial` is
  // the reset value, realized on D, never a power-on value (cvc5 + lgyosys both
  // prove the folded netlist against the reset_pin+initial source). Keeping
  // sync-reset registers native cost br_delay's Pyrope flow 32 native flops
  // that yosys's normalize then mapped to DFFHQNx1 + 64 INVx1 + 24 extra HB1
  // (18.196 vs 17.729 um^2, 114.5 vs 102.8 ps on ASAP7), and left every
  // reset-cone node native with fanout 77-113 (br_amba_axi_demux 2045 ps).
  absl::flat_hash_set<hhds::Node_class> clk_demoted;
  absl::flat_hash_set<hhds::Node_class> reset_demoted;
  if (opts_.map_register) {
    for (const auto& n : rb.nodes) {
      if (!gu::is_type_flop(n)) {
        continue;
      }
      Seq_flop f;
      f.node  = n;
      f.q_pin = n.create_driver_pin(0);
      f.bits  = gu::bits_of(f.q_pin);
      if (f.bits == 0) {
        f.bits = 1;
      }
      f.root = gu::wire_name(f.q_pin);  // the register's signal name (e.g. "r")
      if (f.root.empty()) {
        f.root = std::format("{}__flop{}", rb.module_name, n.get_debug_nid());
      }
      f.din_drv  = gu::get_driver_of_sink_name(n, "din");
      f.en_drv   = gu::get_driver_of_sink_name(n, "enable");
      f.rst_drv  = gu::get_driver_of_sink_name(n, "reset_pin");
      f.rval_drv = gu::get_driver_of_sink_name(n, "initial");
      f.clk_drv  = gu::get_driver_of_sink_name(n, "clock_pin");
      if (auto edge = gu::get_driver_of_sink_name(n, "posclk"); edge.is_const()) {
        f.neg_clock = gu::const_of(edge).is_known_false();
      }
      // ABC latches and the selected plain Liberty DFF have no reset pin. Only
      // an ASYNCHRONOUS reset needs one (see the set's comment above): keep
      // that flop native so cgen retains the `or posedge rst` event and its
      // reset value; its data cone still crosses the boundary and is mapped.
      // The `async` sink is a comptime flavour pin set by tolg (`async=true` /
      // `sync=false`, upass.reset_style=async) and the slang reader for an
      // `always_ff @(posedge clk or posedge rst)`; cgen and pass/lec read it
      // the same way -- const and not known-false => asynchronous, anything
      // else (absent, or a malformed non-const driver) => synchronous -- so
      // the fold agrees with both the emitted Verilog and the LEC model.
      if (!f.rst_drv.is_invalid()) {
        auto async = gu::get_driver_of_sink_name(n, "async");
        if (async.is_const() && !gu::const_of(async).is_known_false()) {
          reset_demoted.insert(n);
          continue;
        }
        f.has_reset = true;
      }
      // tolg may wrap a call-site clock in 1-bit Get_mask coercions (`x:u1`
      // casts survive cprop when the source is signed). On a 1-bit operand they are wire
      // identities regardless of the declared output width — trace to the root
      // so the register's clock is recognized as region-input-driven and the
      // DFF clock pin connects DIRECTLY to it (never through mapped logic).
      //
      // Whole-design flatten can stack one such coercion per hierarchy level,
      // so trace the identity chain to the structural clock source.
      for (int guard = 0; guard < 64 && !f.clk_drv.is_invalid(); ++guard) {  // guard: cycle net, > any sane hierarchy depth
        // Stop AT the partition boundary. A region input IS the structural
        // clock source as far as this region is concerned (its port is what
        // the read-back wires the DFF clk pin from), and peeling past it lands
        // on a pin the region never sees -- which then fails the
        // region_in_name test below and demoted every such register to a
        // native flop. The shipped `cones` coloring routinely leaves a
        // clock-carrying Sext in a neighbour region, so this is the common
        // case, not a corner: without the break, a yosys-read design's only
        // register stays native (lhd_macro_declarations_test).
        if (region_in_name.contains(f.clk_drv)) {
          break;
        }
        auto m = f.clk_drv.get_master_node();
        if (gu::type_op_of(m) == Ntype_op::Sext && real_width(f.clk_drv) == 1) {
          // Yosys' signed clock-pin carrier preserves the single input bit.
          auto a = gu::get_driver_of_sink_name(m, "a");
          if (a.is_invalid() || real_width(a) != 1) {
            break;
          }
          f.clk_drv = a;
          continue;
        }
        if (gu::type_op_of(m) != Ntype_op::Get_mask) {
          break;
        }
        auto a    = gu::get_driver_of_sink_name(m, "a");
        auto mask = gu::get_driver_of_sink_name(m, "mask");
        if (a.is_invalid() || real_width(a) != 1 || mask.is_invalid() || !mask.is_const() || !gu::const_of(mask).bit_test(0)) {
          break;
        }
        f.clk_drv = a;  // get_mask(bit0) of a 1-bit wire == the wire
      }
      // A clock driven by region-INTERNAL logic (a genuinely gated/derived
      // clock — a shape whole-design flatten makes reachable, since everything
      // is one region) cannot cross as a latch: the read-back has no native
      // source for the DFF/flop clock pin and used to silently drop the
      // connection. Demote the register to a boundary box (the register=false
      // machinery): it stays a native flop and its clock cone is
      // technology-mapped and reconnected like any comb-driven boundary input.
      if (!f.clk_drv.is_invalid() && !f.clk_drv.is_const() && !region_in_name.contains(f.clk_drv)) {
        clk_demoted.insert(n);
        continue;
      }
      if (auto nr = gu::get_driver_of_sink_name(n, "negreset"); nr.is_const()) {
        f.neg_reset = gu::const_of(nr).bit_test(0);
      }
      bool has_rval                     = f.rval_drv.is_const();
      auto rval                         = has_rval ? gu::const_of(f.rval_drv) : Dlop{};
      f.has_init                        = has_rval;
      f.init_val                        = rval;  // read-back cannot re-resolve the source pin (see Seq_flop::has_init)
      // A resetless init is a TRUE power-on value: that bit is rebuilt native
      // on read-back and must keep the honest encoding (its latch init would
      // be complemented too). With a reset the init is the reset value, folded
      // into D below, and the bit maps to a cell like an init-less one.
      const bool power_on_init          = has_rval && !f.has_reset;
      f.d_inverted                      = dff_.has_value() && dff_->q_inverted && !power_on_init && builtin_flow;
      // pipe_min encodes real clocked storage, not an optimization hint.
      // Cross every stage into ABC and expose only the final Q to graph users.
      const int               depth     = pipeline_depth(n);
      const auto              prototype = f;
      std::vector<Abc_Obj_t*> previous;
      for (int stage = 0; stage < depth; ++stage) {
        f = prototype;
        if (stage + 1 < depth) {
          // Match Cgen_verilog's stage spelling (get_append_to_name(name,
          // "___pipe<i>_"), a PREFIX, stage 0 closest to D -- same order as
          // here) so the post-synthesis LEC still pairs the hidden stages by
          // name instead of dropping them into a flat SAT solve.
          f.root = std::format("___pipe{}_{}", stage, prototype.root);
        }
        f.previous = previous;
        for (int b = 0; b < f.bits; ++b) {
          auto* bo    = Abc_NtkCreateBo(manNtk);
          auto* latch = Abc_NtkCreateLatch(manNtk);
          auto* bi    = Abc_NtkCreateBi(manNtk);
          Abc_ObjAddFanin(bo, latch);
          Abc_ObjAddFanin(latch, bi);
          // Only a power-on init is told to ABC. A reset-backed register powers
          // on X exactly like the DFF cell it maps to (the reset value arrives
          // through D on the first asserted edge); declaring its reset value as
          // the latch init would let a sequential user flow (`dretime`, `scorr`)
          // assume a start state the cell never provides. The built-in flows
          // contain no sequential optimization, so this is about honesty, not QoR.
          if (power_on_init && !rval.unknown_bit_test(b)) {
            rval.bit_test(b) ? Abc_LatchSetInit1(latch) : Abc_LatchSetInit0(latch);
          } else {
            Abc_LatchSetInitDc(latch);
          }
          auto* qnet = Abc_NtkCreateNet(manNtk);
          Abc_ObjAddFanin(qnet, bo);
          // Source signal names need not be unique (a generated reset counter
          // can share a spelling with a user register). ABC's netlist converter
          // merges CI nets by name, so distinguish every crossed register.
          // Readback recovers source identities from the ordered snapshot.
          auto nm = std::format("{}_%r{}_{}", f.root, flops.size(), b);
          Abc_ObjAssignName(qnet, const_cast<char*>(nm.c_str()), nullptr);
          f.qbits.push_back(qnet);  // stage-local Q
          if (stage + 1 == depth) {
            bitnet[f.q_pin][b] = qnet;
          }
          f.bi.push_back(bi);
        }
        previous = f.qbits;
        flops.push_back(std::move(f));
      }
    }
    // Name the registers, not just their count: "3 register(s)" in a 12k-node
    // region is unactionable. First few by identity + declaration site; the set
    // is unordered, so sort by nid to keep the report reproducible.
    auto report_demoted = [&](const absl::flat_hash_set<hhds::Node_class>& set, std::string_view diag_id, const std::string& why) {
      if (set.empty()) {
        return;
      }
      std::vector<hhds::Node_class> demoted(set.begin(), set.end());
      std::sort(demoted.begin(), demoted.end(), [](const auto& a, const auto& b) { return a.get_debug_nid() < b.get_debug_nid(); });
      auto w = livehd::diag::warn("pass.abc", diag_id, "unsupported");
      w.at(node_span(rb, demoted.front())).msg("pass.abc region '{}': {} {}", rb.module_name, demoted.size(), why);
      constexpr size_t kMaxNamed = 5;
      for (size_t k = 0; k < std::min(kMaxNamed, demoted.size()); ++k) {
        w.note(std::format("kept native: {}", node_identity(demoted[k])), node_span(rb, demoted[k]));
      }
      if (demoted.size() > kMaxNamed) {
        w.note(std::format("... and {} more register(s)", demoted.size() - kMaxNamed));
      }
      w.emit();
    };
    report_demoted(clk_demoted,
                   "derived-clock-native",
                   "register(s) clocked by region-internal logic (a gated/derived clock) kept as native flops — a DFF "
                   "cell cannot take its clock from mapped logic; the clock cone is still mapped and reconnected");
    report_demoted(reset_demoted,
                   "reset-native",
                   "asynchronous-reset register(s) kept as native flops — the selected plain DFF cell has no "
                   "asynchronous reset pin (a synchronous reset is folded into D and mapped); their surrounding data "
                   "cones are still mapped");
  }

  // A very wide OR of non-overlapping, constant-position shifts is a packed-bus
  // assembly, not Boolean logic. Sending its thousands of identity bits through
  // ABC is pathological (Rob's 24x511 -> 10911 pack spent minutes in &nf).
  // Keep the SHLs and their OR as native zero-delay wiring, just like
  // Get_mask/Set_mask at the mapper boundary. pass.opentimer's pin tracker
  // understands both operations, so timing identity is preserved bit-for-bit.
  // Width at or above which a pure-wiring cell (Concat / constant-control
  // slice, pack, shift, sext) is kept as a NATIVE boundary instead of being
  // bit-blasted. Below it the bit-level cone is both cheaper and better
  // (constant lanes reach ABC, and a one-bit boundary net does not acquire
  // hundreds of dead mapped-cell loads); above it, materializing one ABC
  // PI/PO per bit is pathological (Backend carries megabit-scale slice/pack
  // colors). Native reconstruction is the scalability escape hatch for
  // genuinely wide buses, not the default representation for ordinary structs.
  constexpr int kNativeWiringBits = 4096;

  // Native boundary nodes. Most are zero-delay packed wiring discovered below;
  // a structurally cyclic combinational remainder is added after the wiring
  // scan because ABC itself only accepts acyclic Boolean networks.
  absl::flat_hash_set<hhds::Node_class> native_wiring;
  absl::flat_hash_set<hhds::Node_class> native_comb_logic;
  auto                                  node_output_width = [](const hhds::Node_class& n) {
    int width = 0;
    for (const auto& e : n.out_edges()) {
      width = std::max(width, gu::bits_of(e.driver));
    }
    return width != 0 ? width : gu::bits_of(n.create_driver_pin(0));
  };

  // These cells only rearrange or select bits when their control operand is a
  // constant. Rebuilding them as exact typed nodes is both more faithful to
  // their zero-delay wiring role and dramatically smaller than materializing
  // one Liberty buffer/inverter per bit (Backend contains hundreds of
  // megabit-scale slice/pack colors). Variable shifts remain real logic and
  // still cross ABC. And/Or are deliberately excluded: only the proven
  // disjoint wide-OR shape below is wiring rather than Boolean logic.
  const auto const_operand = [](const hhds::Node_class& n, std::string_view sink) {
    const auto d = gu::get_driver_of_sink_name(n, sink);
    return d.is_const();
  };
  // A Concat lane whose driver is WIDER than its declared window is a width
  // boundary only the native reconstruction path can spell (see fit_native_ins,
  // which recreates the missing low-bit cast). Bit-blasting such a cell instead
  // trips abc_bit's lane-table invariant, so keep it native regardless of width.
  const auto concat_has_overwide_lane = [](const hhds::Node_class& n) {
    const auto lanes = gu::concat_lanes(n);
    return !lanes.empty() && !gu::concat_lane_violation(lanes).empty();
  };
  for (const auto& n : rb.nodes) {
    const auto op     = gu::type_op_of(n);
    const int  width  = node_output_width(n);
    // Below kNativeWiringBits a wiring cell stays inside the bit-level cone.
    bool       wiring = op == Ntype_op::Concat && (width >= kNativeWiringBits || concat_has_overwide_lane(n));
    // The control operand must be CONSTANT for the cell to be a bit rename.
    // A non-constant mask/position is real logic -- and, crucially, one the
    // bit-blast loop below REFUSES with a precise per-node diagnostic. Marking
    // it native here would skip that refusal and silently hand pass.opentimer a
    // node its pin tracker also cannot model, turning a clean ABC refusal into a
    // fatal in a later pass.
    if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) {
      wiring = width >= kNativeWiringBits && const_operand(n, "mask");
    } else if (op == Ntype_op::Sext || op == Ntype_op::SRA || op == Ntype_op::SHL) {
      wiring = width >= kNativeWiringBits && const_operand(n, "b");
    }
    if (wiring) {
      native_wiring.insert(n);
    }
  }
  for (const auto& n : rb.nodes) {
    if (gu::type_op_of(n) != Ntype_op::Or || node_output_width(n) < kNativeWiringBits) {
      continue;
    }
    struct Span {
      int lo;
      int hi;
    };
    std::vector<Span>             spans;
    std::vector<hhds::Node_class> shifts;
    bool                          packing         = true;
    int                           unshifted_lanes = 0;
    std::string                   reject;
    for (const auto& e : n.inp_edges()) {
      if (e.driver.is_const()) {
        // A constant lane is already synthesized: zero is padding and one
        // fixes the corresponding output bit. It needs no Liberty cell and
        // does not participate in variable-lane overlap.
        continue;
      }
      const auto shl = e.driver.get_master_node();
      if (gu::type_op_of(shl) != Ntype_op::SHL) {
        // Packed assemblies commonly leave the low lane unshifted (Rob's low
        // 20 bits arrive from an extracted Sub) and shift every higher lane.
        // One such lane is safe: interval overlap below proves it is disjoint.
        const int width = gu::bits_of(e.driver);
        if (++unshifted_lanes > 1 || width <= 0) {
          packing = false;
          reject  = "multiple or widthless unshifted inputs";
          break;
        }
        spans.push_back({0, width});
        continue;
      }
      const auto a = gu::get_driver_of_sink_name(shl, "a");
      const auto b = gu::get_driver_of_sink_name(shl, "b");
      if (a.is_invalid() || !b.is_const()) {
        packing = false;
        reject  = "shift lacks data or constant amount";
        break;
      }
      const int width     = gu::bits_of(a);
      const int shl_width = node_output_width(shl);
      if (width <= 0 || shl_width < width) {
        packing = false;
        reject  = "invalid shift width stamps";
        break;
      }
      const int out_width = node_output_width(n);
      // For a non-negative constant SHL, bitwidth stamps exactly
      // input-width+amount on the result. Recover the occupied interval from
      // those stamps, avoiding a lossy int64 conversion of an arbitrary-size
      // Dlop constant.
      const int lo        = std::min(shl_width - width, out_width);
      const int hi        = std::min(shl_width, out_width);
      if (lo < hi) {
        spans.push_back({lo, hi});
      }
      shifts.push_back(shl);
    }
    if (!packing || shifts.empty()) {
      if (opts_.verbose) {
        std::print("[pass.abc] region '{}': rejected {}-bit shift/OR pack after {} shift(s): {}\n",
                   rb.module_name,
                   node_output_width(n),
                   shifts.size(),
                   reject.empty() ? "no shifted lane" : reject);
      }
      continue;
    }
    std::ranges::sort(spans, {}, &Span::lo);
    for (size_t i = 1; i < spans.size(); ++i) {
      if (spans[i].lo < spans[i - 1].hi) {
        packing = false;
        reject  = std::format("overlap {}..{} with {}..{}", spans[i - 1].lo, spans[i - 1].hi, spans[i].lo, spans[i].hi);
        break;
      }
    }
    if (!packing) {
      if (opts_.verbose) {
        std::print("[pass.abc] region '{}': rejected {}-bit shift/OR pack after {} shift(s): {}\n",
                   rb.module_name,
                   node_output_width(n),
                   shifts.size(),
                   reject);
      }
      continue;
    }
    native_wiring.insert(n);
    for (const auto& shl : shifts) {
      if (region.contains(shl)) {
        native_wiring.insert(shl);
      }
    }
    if (opts_.verbose) {
      std::print("[pass.abc] region '{}': keeping {}-bit disjoint shift/OR pack as native wiring ({} shifts)\n",
                 rb.module_name,
                 node_output_width(n),
                 shifts.size());
    }
  }
  // A wide packed-bus assembler exported directly by the region is also
  // wiring, not a Boolean cone. Keeping it as a native boundary avoids an ABC
  // PO for every bit of every exported packed bus (Rob c33: 27.7M interface
  // bits for only 19.8k GE). Its narrow computed lane inputs still become ABC
  // POs, while wide base/region-input lanes reconnect natively below.
  std::vector<hhds::Node_class> exported_wiring;
  for (const auto& port : rb.outputs) {
    const auto n            = port.src_driver.get_master_node();
    const auto op           = gu::type_op_of(n);
    const bool constant_shl = op == Ntype_op::SHL && gu::get_driver_of_sink_name(n, "b").is_const();
    const bool wide_pack    = node_output_width(n) >= kNativeWiringBits && (op == Ntype_op::Concat || op == Ntype_op::Set_mask);
    if (!region.contains(n) || (!constant_shl && !wide_pack)) {
      continue;
    }
    if (native_wiring.insert(n).second) {
      exported_wiring.push_back(n);
    }
  }
  for (size_t head = 0; head < exported_wiring.size(); ++head) {
    for (const auto& e : exported_wiring[head].inp_edges()) {
      if (e.driver.is_invalid() || e.driver.is_const()) {
        continue;
      }
      const auto parent = e.driver.get_master_node();
      const auto op     = gu::type_op_of(parent);
      if (!region.contains(parent) || node_output_width(parent) < kNativeWiringBits
          || (op != Ntype_op::Concat && op != Ntype_op::Set_mask)) {
        continue;
      }
      if (native_wiring.insert(parent).second) {
        exported_wiring.push_back(parent);
      }
    }
  }
  if (opts_.verbose && !exported_wiring.empty()) {
    std::print("[pass.abc] region '{}': kept {} exported packed-wiring node(s) native including ancestors\n",
               rb.module_name,
               exported_wiring.size());
  }

  // ABC cannot ingest a combinational SCC. Preserve such logic exactly as
  // native typed nodes and map every acyclic cone around it. This is a boundary
  // cut, not an approximation: native->mapped edges become ABC PIs,
  // mapped->native edges become POs, and read-back reconnects the original
  // feedback. Kahn's unpeeled remainder includes the SCC plus any nodes whose
  // only schedule predecessor is that SCC; keeping the whole remainder native
  // is conservative and prevents an arbitrary edge choice from changing with
  // traversal order.
  {
    std::vector<hhds::Node_class>         comb;
    absl::flat_hash_set<hhds::Node_class> comb_set;
    for (const auto& n : rb.nodes) {
      const auto op = gu::type_op_of(n);
      if (gu::is_type_register(n) || op == Ntype_op::Sub || op == Ntype_op::Clock_cell || op == Ntype_op::Rem) {
        continue;
      }
      comb.push_back(n);
      comb_set.insert(n);
    }
    absl::flat_hash_map<hhds::Node_class, size_t>                        indegree;
    absl::flat_hash_map<hhds::Node_class, std::vector<hhds::Node_class>> consumers;
    std::vector<hhds::Node_class>                                        queue;
    indegree.reserve(comb.size());
    consumers.reserve(comb.size());
    queue.reserve(comb.size());
    for (const auto& n : comb) {
      size_t degree = 0;
      for (const auto& e : n.inp_edges()) {
        const auto producer = e.driver.get_master_node();
        if (!producer.is_invalid() && comb_set.contains(producer)) {
          ++degree;
          consumers[producer].push_back(n);
        }
      }
      indegree.emplace(n, degree);
      if (degree == 0) {
        queue.push_back(n);
      }
    }
    size_t peeled = 0;
    for (size_t head = 0; head < queue.size(); ++head) {
      const auto n = queue[head];
      ++peeled;
      if (auto it = consumers.find(n); it != consumers.end()) {
        for (const auto& consumer : it->second) {
          auto& degree = indegree.at(consumer);
          if (--degree == 0) {
            queue.push_back(consumer);
          }
        }
      }
    }
    if (peeled != comb.size()) {
      std::vector<hhds::Node_class> cyclic_remainder;
      cyclic_remainder.reserve(comb.size() - peeled);
      for (const auto& n : comb) {
        if (indegree.at(n) != 0) {
          cyclic_remainder.push_back(n);
          native_wiring.insert(n);
          native_comb_logic.insert(n);
        }
      }
      auto w = livehd::diag::warn("pass.abc", "comb-loop-native", "unsupported");
      w.at(node_span(rb, cyclic_remainder.front()))
          .msg(
              "pass.abc region '{}': preserved {} node(s) in a combinational-cycle remainder as native logic; acyclic cones "
              "around it are still technology-mapped",
              rb.module_name,
              cyclic_remainder.size())
          .hint("remove the combinational feedback to obtain an all-standard-cell region and a complete timing score");
      constexpr size_t kMaxNamed = 5;
      for (size_t k = 0; k < std::min(kMaxNamed, cyclic_remainder.size()); ++k) {
        w.note(std::format("kept native: {}", node_identity(cyclic_remainder[k])), node_span(rb, cyclic_remainder[k]));
      }
      if (cyclic_remainder.size() > kMaxNamed) {
        w.note(std::format("... and {} more node(s)", cyclic_remainder.size() - kMaxNamed));
      }
      w.emit();
    }
  }

  if (opts_.verbose) {
    std::fflush(stdout);
  }

  // --- blackbox boundary nodes (Sub instances + memories): never bit-blasted.
  // Each consumed output driver pin becomes fresh ABC PIs (a source for the
  // surrounding logic, seeded into bitnet); each combinationally-driven input
  // becomes ABC POs (the cone feeding it, created after the comb loop); constant
  // inputs are recreated directly on read-back. The node itself is rebuilt
  // natively and reconnected. Boundary PIs/POs are appended AFTER the region
  // ports so the region-port read-back stays index-aligned (region first). ---
  struct Bbox_out {
    hhds::Pin_class src_pin;
    int             port_id;
    int             bits;
    bool            sign;
    bool            abc_bits;
  };
  struct Bbox_in {
    int             port_id;
    hhds::Pin_class drv;
    int             bits;
    bool            sign;  // operand signedness — load-bearing for a Div boundary (the LEC fit()s its operands by sign)
  };
  struct Bbox {
    hhds::Node_class                             node;
    Ntype_op                                     op;
    std::vector<Bbox_out>                        outs;
    std::vector<Bbox_in>                         ins;
    std::vector<std::pair<int, hhds::Pin_class>> const_ins;   // (port_id, const driver)
    std::vector<std::pair<int, hhds::Pin_class>> native_ins;  // flop boundary: (port_id, region-input driver) reconnected directly
    std::vector<std::tuple<int, hhds::Pin_class, int, bool>> fit_native_ins;  // direct driver, truncated to a Concat lane
  };
  // region_in_name (built above the register scan) reconnects a flop
  // boundary's control pins (clock/reset/enable that come straight from a
  // region input) NATIVELY on read-back. Routing such a clock through the
  // combinational AIG would map it to a logic buffer and make the rebuilt flop
  // clock on `posedge <data-wire>` -- logically correct but unusable as a real
  // netlist (breaks clock-tree synthesis and timing). Only the flop's din cone
  // (genuine comb logic) crosses into ABC. A demoted register's derived-clock
  // or reset data cone, by contrast, IS genuine logic and crosses as a PO.
  // Convert the trivially-mappable remainders BEFORE the boundary scan, so the
  // refusal below only fires for a shape that genuinely has no gate translation.
  auto& rewritten = coordinator_ ? coordinator_->rems_rewritten_graphs_ : rems_rewritten_graphs_;
  if (rewritten.insert(rb.src).second) {
    rewrite_trivial_rems(rb.src);
  }

  std::vector<Bbox> bboxes;
  for (const auto& n : rb.nodes) {
    auto op = gu::type_op_of(n);
    // A materialized PROPERTY marker (`fproperty` from a user assert/assume,
    // `lgassert` from a runtime `a#[lo..=hi]` guard) is not hardware: it has no
    // Liberty cell and no body, and the mapped netlist is a synthesis artifact
    // that cannot represent it. Dropping it here is what makes the netlist
    // WELL-FORMED. Carrying it as a blackbox boundary instead left a Sub whose
    // module was never declared in the output library, so `get_subnode_io()`
    // came back null downstream: cgen silently emitted nothing for it (the
    // runtime check was already lost), and `pass.opentimer` refused the WHOLE
    // design over a black box with an empty type name -- which is what took out
    // every cva6 synthesis run, and any design containing one runtime bit-range
    // select. The check lives on in the pre-ABC design, which is what pass.formal
    // and the source-level flows read.
    if (gu::is_property_marker(n)) {
      continue;
    }
    // A flop in a !seq (combinational-only) map is kept as a native boundary,
    // exactly like a Sub/Memory: its Q feeds the mapped logic as a fresh PI, its
    // din/enable/clock/reset are cut as POs (or recreated when const), and the
    // Flop node is rebuilt unchanged on read-back (never bit-blasted). In seq
    // mode flops instead cross into ABC as 1-bit latches (handled above), so they
    // are excluded from the boundary set there — EXCEPT registers demoted for a
    // region-internal clock or an asynchronous reset, which take this boundary
    // path.
    bool       flop_boundary = gu::is_type_flop(n) && (!opts_.map_register || clk_demoted.contains(n) || reset_demoted.contains(n));
    // A LATCH is a boundary in BOTH modes, unconditionally (2f-latch M2).
    // TERMINOLOGY TRAP: an ABC/AIGER "latch" is an edge-triggered unit-delay
    // register on an implicit global clock, NOT a level-sensitive latch — and
    // ABC's BLIF reader silently DISCARDS the `.latch` control tokens. So
    // letting a real latch cross into ABC in seq mode (the way a flop does)
    // would not be an error, it would be a silent MISMODEL. Keeping it native
    // means q feeds the mapped logic as a fresh PI and din/enable are cut as
    // POs, exactly like a Sub/Memory. Before this, a Latch matched none of the
    // cases below and fell into the bit-blast loop, aborting the whole region.
    const bool latch_boundary = op == Ntype_op::Latch;
    if (op != Ntype_op::Sub && op != Ntype_op::Memory && op != Ntype_op::Clock_cell && op != Ntype_op::Rem && !flop_boundary
        && !latch_boundary && !native_wiring.contains(n)) {
      continue;
    }
    if (op == Ntype_op::Rem) {
      // REMAINDER is where the synthesis constraint lives, and it is an ERROR
      // rather than a warning. The rest of LiveHD handles `%` as an ordinary
      // op -- bitwidth ranges it, constprop folds it, the LEC encoder proves it
      // (SREM), the simulator runs it -- because none of those need it to become
      // gates. Only the netlist mapper does. Raising it HERE, rather than at
      // lowering time, is what lets a design that merely CONTAINS `%` compile,
      // simulate and verify.
      //
      // Anything trivially convertible was already rewritten to a mask by
      // rewrite_trivial_rems() above, so reaching this point means the shape
      // genuinely has no easy gate-level translation.
      livehd::diag::err("pass.abc", "rem-unsupported", "unsupported")
          .at(node_span(rb, n))
          .msg("pass.abc: {} in region '{}': remainder (`%`) has no gate-level translation", node_identity(n), rb.module_name)
          .hint(
              "only a power-of-two divisor over a non-negative dividend converts trivially (to a mask); keep other "
              "remainders out of the synthesized region")
          .emit();
    }
    Bbox bb;
    bb.node                              = n;
    bb.op                                = op;
    int                           bb_idx = static_cast<int>(bboxes.size());
    absl::flat_hash_map<int, int> concat_lane_width;
    if (op == Ntype_op::Concat) {
      const auto lanes = gu::concat_lanes(n);
      for (size_t lane = 0; lane < lanes.size(); ++lane) {
        concat_lane_width.emplace(static_cast<int>(2 * lane), lanes[lane].width);
      }
    }
    // outputs: distinct driver pins that feed region logic -> fresh PI sources.
    // btree_map (ascending port_id) so the fresh-PI creation order — hence ABC
    // ObjId assignment and the read-back `g<id>_<cell>` gate names — is
    // deterministic; a flat_hash_map iterates in run-to-run-varying order.
    absl::btree_map<int, hhds::Pin_class> out_pins;
    for (const auto& e : n.out_edges()) {
      out_pins.emplace(static_cast<int>(e.driver.get_port_id()), e.driver);
    }
    for (auto& [pid, op_pin] : out_pins) {
      int w = gu::bits_of(op_pin);
      if (w == 0) {
        w = 1;
      }
      // A one-node native boundary has no combinational consumer inside this
      // region. Its output can reconnect straight to the region output; making
      // one ABC PI/PO buffer per bit is pure overhead (Rob has 20k--42k-bit
      // register-only regions).
      bool needs_abc = rb.nodes.size() != 1 && !native_wiring.contains(n);
      if (!needs_abc) {
        for (const auto& e : op_pin.out_edges()) {
          const auto sink_node = e.sink.get_master_node();
          if (region.contains(sink_node) && !native_wiring.contains(sink_node)) {
            needs_abc = true;
            break;
          }
        }
      }
      int oi = static_cast<int>(bb.outs.size());
      bb.outs.push_back({op_pin, pid, w, !gu::is_unsign(op_pin), needs_abc});
      if (!needs_abc) {
        continue;  // boundary-to-boundary bus reconnects natively on read-back
      }
      bbox_output_index.emplace(op_pin, Bbox_output_origin{bb_idx, oi});
    }
    // inputs: const-driven recreated directly; comb-driven cut as POs. Any pin
    // driven straight by a region input is reconnected natively instead: there
    // is no Boolean logic for ABC to optimize. This is essential for wide
    // shared-Sub inputs (Rob carries a 10,260-bit source bus into hundreds of
    // instances); routing a direct wire through ABC otherwise materializes one
    // output buffer per bit. It also subsumes the clock/reset/enable treatment
    // for native flop/latch boundaries.
    for (const auto& e : n.inp_edges()) {
      int pid = static_cast<int>(e.sink.get_port_id());
      if (e.driver.is_const()) {
        bb.const_ins.emplace_back(pid, e.driver);
      } else {
        const auto lane_fit      = concat_lane_width.find(pid);
        const bool needs_fit     = lane_fit != concat_lane_width.end() && gu::bits_of(e.driver) > lane_fit->second;
        const bool native_driver = region_in_name.contains(e.driver) || native_wiring.contains(e.driver.get_master_node());
        if (native_driver) {
          if (needs_fit) {
            // A Concat lane is an explicit width boundary. The source normally
            // has a Get_mask in front of an over-wide packed-array carrier, but
            // that zero-delay wrapper can disappear while native boundaries
            // are cut and reconstructed. Recreate it natively below rather
            // than mapping W identity buffers through ABC.
            bb.fit_native_ins.emplace_back(pid, e.driver, lane_fit->second, !gu::is_unsign(e.driver));
          } else {
            bb.native_ins.emplace_back(pid, e.driver);
          }
          continue;
        }
        int w = gu::bits_of(e.driver);
        if (w == 0) {
          w = 1;
        }
        if (lane_fit != concat_lane_width.end()) {
          w = std::min(w, lane_fit->second);
        }
        bb.ins.push_back({pid, e.driver, w, !gu::is_unsign(e.driver)});
      }
    }
    bboxes.push_back(std::move(bb));
  }

  // --- bit-blast each region node in dependency order. `rb.nodes` (the order
  // the partitioner collected the region in) is
  // *mostly* topological, but it can emit a reader before its producer (the
  // same phenomenon the LEC encoder fixpoints around for forward_hier — seen
  // on the DINO top, where a wide packed-bus Get_mask was read by an Sra a
  // thousand nodes before the Get_mask was visited). A single pass would then
  // read the unmaterialized operand as const0 and silently miscompile the
  // whole cone. Schedule with a dependency queue: a node is ready when every
  // comb operand is a constant, a region input, a seeded boundary, or an
  // earlier-blasted node. A repeated whole-pending-list fixpoint is quadratic
  // on reverse-ordered cones (Rob has 12k-node regions); the queue visits every
  // dependency once. A stuck remainder (a genuine node-level cycle or broken
  // boundary) is appended in traversal order so abc_bit's unmaterialized-driver
  // diagnostic pinpoints the const0 reads.
  std::vector<hhds::Node_class> blast_order;
  {
    std::vector<hhds::Node_class> pending;
    for (const auto& n : rb.nodes) {
      auto op = gu::type_op_of(n);
      if (op == Ntype_op::Sub || op == Ntype_op::Memory || op == Ntype_op::Clock_cell || op == Ntype_op::Rem
          || native_wiring.contains(n)) {
        continue;  // native boundary -- never eagerly bit-blasted
      }
      if (gu::is_type_flop(n)) {
        continue;  // flop: a 1-bit latch in seq mode, a native boundary in !seq mode -- never bit-blasted
      }
      if (op == Ntype_op::Latch) {
        continue;  // level-sensitive latch: always a native boundary (2f-latch M2), never bit-blasted
      }
      pending.push_back(n);
    }
    absl::flat_hash_set<hhds::Pin_class> ready;  // driver pins with materialized (or scheduled) bit slots
    ready.reserve(bitnet.size() + pending.size());
    for (const auto& kv : bitnet) {
      ready.insert(kv.first);
    }
    // Native boundary outputs are demand-created PIs, so they need not have
    // any entry in bitnet yet. They are nevertheless available immediately.
    // Otherwise their consumers enter the stuck remainder in arbitrary order
    // and a later slice can be read before it has been bit-blasted.
    for (const auto& kv : bbox_output_index) {
      ready.insert(kv.first);
    }
    absl::flat_hash_map<hhds::Node_class, size_t>                       unresolved;
    absl::flat_hash_map<hhds::Pin_class, std::vector<hhds::Node_class>> waiters;
    std::vector<hhds::Node_class>                                       queue;
    unresolved.reserve(pending.size());
    waiters.reserve(pending.size());
    queue.reserve(pending.size());
    for (const auto& n : pending) {
      size_t count = 0;
      for (const auto& e : n.inp_edges()) {
        const auto& d = e.driver;
        if (d.is_invalid() || d.is_const() || ready.contains(d) || region_input_index.contains(d)) {
          continue;
        }
        ++count;
        waiters[d].push_back(n);
      }
      unresolved.emplace(n, count);
      if (count == 0) {
        queue.push_back(n);
      }
    }
    blast_order.reserve(pending.size());
    size_t scheduled_count = 0;
    for (size_t head = 0; head < queue.size(); ++head) {
      const auto n = queue[head];
      ++scheduled_count;
      // Concat has no Boolean logic. Keep it in this dependency queue so all
      // lane producers precede its consumers, but resolve only demanded bits
      // through abc_bit instead of eagerly copying every bit of every lane.
      if (gu::type_op_of(n) != Ntype_op::Concat) {
        blast_order.push_back(n);
      }
      absl::flat_hash_set<hhds::Pin_class> produced;
      for (const auto& e : n.out_edges()) {
        produced.insert(e.driver);
      }
      if (produced.empty()) {
        produced.insert(n.create_driver_pin(0));
      }
      for (const auto& d : produced) {
        ready.insert(d);
        auto wit = waiters.find(d);
        if (wit == waiters.end()) {
          continue;
        }
        for (const auto& consumer : wit->second) {
          auto& count = unresolved.at(consumer);
          if (--count == 0) {
            queue.push_back(consumer);
          }
        }
      }
    }
    if (scheduled_count != pending.size()) {
      std::vector<hhds::Node_class> stuck;
      stuck.reserve(pending.size() - scheduled_count);
      for (const auto& n : pending) {
        if (unresolved.at(n) != 0) {
          stuck.push_back(n);
        }
      }
      if (opts_.verbose) {
        std::print("[pass.abc] region '{}': scheduler stuck with {} node(s); unresolved dependencies:\n",
                   rb.module_name,
                   stuck.size());
        size_t shown = 0;
        for (const auto& n : stuck) {
          for (const auto& e : n.inp_edges()) {
            const auto& d = e.driver;
            if (d.is_invalid() || d.is_const() || ready.contains(d) || region_input_index.contains(d)) {
              continue;
            }
            const auto dn = d.get_master_node();
            std::print("  {} ({}) <- {} ({}) p{} region={} seeded={}\n",
                       gu::debug_name(n),
                       Ntype::get_name(gu::type_op_of(n)),
                       gu::debug_name(dn),
                       Ntype::get_name(gu::type_op_of(dn)),
                       d.get_port_id(),
                       region.contains(dn),
                       bitnet.contains(d));
            if (++shown == 32) {
              break;
            }
          }
          if (shown == 32) {
            break;
          }
        }
        std::fflush(stdout);
      }
      for (const auto& n : stuck) {
        if (gu::type_op_of(n) != Ntype_op::Concat) {
          blast_order.push_back(n);
        }
      }
    }
  }
  trace_stage("scheduled");
  // Memory admission: sample our own RSS as the region is bit-blasted, and stop
  // before the ABC flow if this region will not fit. The first sample is at ~5%
  // of the work (early enough that a hopeless region dies cheaply), then every
  // 2% so a region that grows non-linearly is still caught. RSS is a syscall, so
  // it is sampled -- never read per node.
  const uint64_t rss_before  = opts_.allow_oversize ? 0 : cost::process_footprint_bytes();
  const size_t   blast_total = blast_order.size();
  const size_t   sample_step = std::max<size_t>(1, blast_total / 50);
  size_t         blasted     = 0;

  for (const auto& n : blast_order) {
    if (opts_.verbose && blasted != 0 && blasted % 1000 == 0) {
      std::print("[pass.abc] region '{}': blast {}/{} before {} at {:.0f} ms\n",
                 rb.module_name,
                 blasted,
                 blast_total,
                 Ntype::get_name(gu::type_op_of(n)),
                 since());
      std::fflush(stdout);
    }
    if (!opts_.allow_oversize && ++blasted % sample_step == 0 && blasted >= blast_total / 20) {
      if (over_budget(rb.module_name, rss_before, blasted, blast_total)) {
        Abc_NtkDelete(manNtk);  // emit no partial result; work() raises refusal_ after stop()
        return;
      }
    }
    auto op       = gu::type_op_of(n);
    auto out_pin  = n.create_driver_pin(0);
    int  out_bits = gu::bits_of(out_pin);
    if (out_bits == 0) {
      out_bits = 1;
    }
    if (op == Ntype_op::Sum) {
      // All Sum outputs are realizations of the same integer expression.
      // Build one adder at the largest requested width and share its prefixes.
      for (const auto& e : n.out_edges()) {
        out_bits = std::max(out_bits, gu::bits_of(e.driver));
      }
    }
    if (op == Ntype_op::SHL || op == Ntype_op::Not) {
      const auto demand = gu::masked_output_width(n, [&](const auto& consumer) { return region.contains(consumer); });
      if (demand > 0) {
        out_bits = std::min(out_bits, static_cast<int>(demand));
      }
    }
    auto& slots = bitnet[out_pin];

    const std::vector<Mux_fact>* node_facts = nullptr;
    if (satopt_facts) {
      const auto found = satopt_facts->mux.find(static_cast<uint64_t>(n.get_debug_nid()));
      if (found != satopt_facts->mux.end() && satopt_crosses(n)) {
        node_facts = &found->second;
      }
    }
    blast_comb(n, out_bits, slots, ops, abc_bit, opts_, rb, region, refuse, refuse_shift_amount, node_facts);
    if (op == Ntype_op::Sum) {
      absl::flat_hash_set<hhds::Pin_class> outputs;
      for (const auto& e : n.out_edges()) {
        outputs.insert(e.driver);
      }
      for (const auto& output : outputs) {
        if (output == out_pin) {
          continue;
        }
        auto& target = bitnet[output];
        for (int bit = 0; bit < std::max(1, gu::bits_of(output)); ++bit) {
          target[bit] = slots.at(bit);
        }
      }
    }
  }
  if (unsupported) {
    if (refusals > kMaxRefusals) {
      livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
          .msg("pass.abc: region '{}': {} further node(s) were refused; only the first {} are reported above",
               rb.module_name,
               refusals - kMaxRefusals,
               kMaxRefusals)
          .emit();
    }
    Abc_NtkDelete(manNtk);
    return;
  }
  trace_stage("blast-complete");

  // --- sequential: wire each latch's data-in (D) to the folded next-state ---
  // Asynchronous-reset flops were kept as native boundaries above because the
  // selected plain DFF cannot represent the reset event. Every crossed flop's
  // next state is therefore `rst ? rval : (en ? din : Q)` -- a synchronous
  // reset has priority over the enable, exactly cgen's
  // `if (rst) q <= rval; else if (en) q <= din;` and pass/lec's
  // ITE(rst, init, ITE(en, din, q)) -- and a missing `initial` resets to 0
  // like tolg's nil init. Folding enable and reset into the AIG means the
  // reconstructed flop is a plain D-flop (only clock + a resetless power-on
  // init reattached), and ABC sees the true next-state function so
  // retiming/sweeping stays sound.
  // enable/reset are single control signals: an N-bit pin asserts on (pin != 0),
  // i.e. the OR-reduction of its bits (matches cgen/yosys reg semantics and the
  // LEC's `rst != 0`). Reduce once per flop, not per data bit.
  auto reduce_or = [&](const hhds::Pin_class& p) -> Abc_Obj_t* {
    int w = gu::bits_of(p);
    if (w <= 0) {
      w = 1;
    }
    Abc_Obj_t* acc = abc_bit(p, 0);
    for (int k = 1; k < w; ++k) {
      acc = abc_bin(acc, abc_bit(p, k), '|');
    }
    return acc;
  };
  for (auto& f : flops) {
    Abc_Obj_t* en_active  = f.en_drv.is_invalid() ? nullptr : reduce_or(f.en_drv);
    Abc_Obj_t* rst_active = nullptr;
    if (!f.rst_drv.is_invalid()) {
      rst_active = reduce_or(f.rst_drv);
      if (f.neg_reset) {
        rst_active = abc_not(rst_active);
      }
    }
    for (int b = 0; b < f.bits; ++b) {
      Abc_Obj_t* d = f.previous.empty() ? abc_bit(f.din_drv, b) : f.previous[b];
      if (en_active != nullptr) {
        d = abc_mux(en_active, d, f.qbits[b]);  // (en != 0)? din : Q
      }
      if (rst_active != nullptr) {
        Abc_Obj_t* rval = f.rval_drv.is_invalid() ? abc_const_bit(false) : abc_bit(f.rval_drv, b);
        d               = abc_mux(rst_active, rval, d);  // reset? rval : (en? din : Q)
      }
      // QN cell under the built-in flow: the latch stores ~next_state (abc_not
      // folds constants; strash turns it into a complemented edge), so `&nf`
      // maps ~f as part of its own phase assignment and mints an INV only where
      // nothing absorbs it (a D fed straight by a port). It does perturb the
      // mapping either way -- same binary, br_arb_rr comb 205 gates / 14.70
      // um^2 -> 244 / 17.96, br_credit_sender 67.0 -> 60.2 -- but the aggregate
      // is far cheaper than the read-back absorption (Seq_flop::d_inverted),
      // which the other latches take.
      Abc_ObjAddFanin(f.bi[b], f.d_inverted ? abc_not(d) : d);
    }
  }

  // --- region outputs -> per-bit ABC POs ---
  std::vector<std::pair<size_t, int>>  po_order;  // PO index -> (output port, bit)
  std::vector<bool>                    direct_native_output(rb.outputs.size(), false);
  absl::flat_hash_set<hhds::Pin_class> direct_boundary_outputs;
  bool                                 has_dummy_po = false;
  for (const auto& bb : bboxes) {
    for (const auto& out : bb.outs) {
      if (!out.abc_bits) {
        direct_boundary_outputs.insert(out.src_pin);
      }
    }
  }
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    const auto& port = rb.outputs[po];
    if (native_wiring.contains(port.src_driver.get_master_node()) || direct_boundary_outputs.contains(port.src_driver)) {
      direct_native_output[po] = true;
      continue;
    }
    int w = port.bits == 0 ? 1 : port.bits;
    for (int b = 0; b < w; ++b) {
      auto* value = abc_bit(port.src_driver, b);
      auto  nm    = std::format("__po{}_{}_b{}", po, port.name, b);
      auto* onet  = Abc_NtkCreateNet(manNtk);
      Abc_ObjAssignName(onet, const_cast<char*>(nm.c_str()), nullptr);
      Abc_ObjAddFanin(onet, Abc_ObjFanin0(value));
      auto* obj = Abc_NtkCreatePo(manNtk);
      // A PO is already a connectivity boundary, so the source node gets a
      // uniquely named NET alias rather than an explicit identity node (ABC's
      // netlist checker requires unique CO net names; an explicit node would
      // cost wide shared-Sub inputs one cell per boundary bit, Rob: 523 x
      // 10,260). The alias alone does NOT avoid a cell, though: `&put`
      // re-decouples every CO driver (Abc_NtkLogicMakeSimpleCos), so a PO fed
      // straight by a PI, a latch Q or a blackbox output comes back as a
      // Liberty buffer anyway -- 512 of br_demux_onehot's 528 cells were
      // exactly that. What removes them is the identity-buffer bypass in the
      // read-back (is_identity_gate / only_co_fanouts, pass 1b), which aliases
      // the buffer's output net to its input net and mints no Sub. Read-back
      // pairs POs by creation order.
      Abc_ObjAddFanin(obj, onet);
      po_order.emplace_back(po, b);
    }
  }
  trace_stage("region-pos");

  // --- blackbox combinational inputs -> per-bit ABC POs (appended after the
  // region outputs so the region-output read-back stays index-aligned) ---
  struct Bbox_po_target {
    int bx;
    int input;
    int bit;
  };
  // One ABC PO per UNIQUE (source driver, bit), with every blackbox input that
  // consumes it. Repeated shared instances often read the same very wide bus;
  // emitting a PO per consumer duplicates pure interface work quadratically.
  std::vector<std::vector<Bbox_po_target>>                   bbox_po;
  absl::flat_hash_map<hhds::Pin_class, std::vector<int32_t>> bbox_po_index;
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    auto& bb = bboxes[bi];
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      const auto& in    = bb.ins[ii];
      auto&       index = bbox_po_index[in.drv];
      if (static_cast<int>(index.size()) < in.bits) {
        index.resize(in.bits, -1);
      }
      for (int b = 0; b < in.bits; ++b) {
        if (index[b] < 0) {
          auto* value = abc_bit(in.drv, b);
          auto  nm    = std::format("__bb{}_i{}_b{}", bi, ii, b);
          auto* onet  = Abc_NtkCreateNet(manNtk);
          Abc_ObjAssignName(onet, const_cast<char*>(nm.c_str()), nullptr);
          Abc_ObjAddFanin(onet, Abc_ObjFanin0(value));
          auto* obj = Abc_NtkCreatePo(manNtk);
          Abc_ObjAddFanin(obj, onet);
          index[b] = static_cast<int32_t>(bbox_po.size());
          bbox_po.emplace_back();
        }
        bbox_po[static_cast<size_t>(index[b])].push_back({static_cast<int>(bi), static_cast<int>(ii), b});
      }
    }
  }
  trace_stage("bbox-pos");

  // A region made entirely of direct native boundaries has no real ABC
  // outputs. ABC's dch implementation crashes on that empty network; retain a
  // single unobserved constant PO as a mapper sentinel. Readback intentionally
  // ignores it because it is absent from both po_order and bbox_po.
  if (Abc_NtkPoNum(manNtk) == 0) {
    has_dummy_po = true;
    auto* value  = abc_const_bit(false);
    auto* onet   = Abc_NtkCreateNet(manNtk);
    char  name[] = "__livehd_dummy_po";
    Abc_ObjAssignName(onet, name, nullptr);
    Abc_ObjAddFanin(onet, Abc_ObjFanin0(value));
    auto* obj = Abc_NtkCreatePo(manNtk);
    Abc_ObjAddFanin(obj, onet);
  }

  Abc_NtkFinalizeRead(manNtk);
  if (!Abc_NtkCheck(manNtk)) {
    livehd::diag::err("pass.abc", "abc-check", "internal").msg("ABC netlist check failed for region '{}'", rb.module_name).fatal();
    Abc_NtkDelete(manNtk);
    return;
  }
  trace_stage("translated");

  // --- Phase A boundary environment (abc_boundary.hpp): what this region's
  // ports see beyond the partition, estimated from the SOURCE graph, handed to
  // ABC's SCL timer for every sizing/timing step of this region (the tail,
  // the budget ladder, the area candidate, the QoR timer). PI/PO indices are
  // the translation's creation order (`all_pi_order`, `po_order`), the same
  // order the read-back pairs by. Lives to the end of map_region; the table
  // uninstalls itself. Exact loads come later (refine_boundaries). ---
  Boundary_table boundary_table(static_cast<size_t>(Abc_NtkPiNum(manNtk)), static_cast<size_t>(Abc_NtkPoNum(manNtk)));
  int            boundary_bits = 0;
  if (opts_.boundary && scl_lib_ok_) {
    std::vector<int> pi_port(all_pi_order.size(), -1);
    for (size_t i = 0; i < all_pi_order.size(); ++i) {
      if (all_pi_order[i].kind == Pi_kind::region_input) {
        pi_port[i] = static_cast<int>(pi_order[all_pi_order[i].index].first);
      }
    }
    boundary_bits = fill_static_boundary(boundary_table, rb, region, pi_port, po_order);
    boundary_table.install();
    if (opts_.verbose) {
      std::print("[pass.abc] region '{}': boundary estimate on {} crossing port bit(s)\n", rb.module_name, boundary_bits);
    }
  }

  // Only private ABC objects are accessed in this interval. HHDS graph reads,
  // mutation, cache operations and result publication stay under graph_lock.
  struct Graph_pause {
    std::unique_lock<std::mutex>& lock;
    Mapper*                       owner;
    bool                          paused = false;
    Graph_pause(std::unique_lock<std::mutex>& l, Mapper* o) : lock(l), owner(o) {
      if (owner) {
        const auto active               = owner->active_abc_.fetch_add(1) + 1;
        owner->parallel_stats_.peak_abc = std::max(owner->parallel_stats_.peak_abc, active);
        paused                          = true;
        lock.unlock();
      }
    }
    void resume() {
      if (paused) {
        owner->active_abc_.fetch_sub(1);
        lock.lock();
        paused = false;
      }
    }
    ~Graph_pause() { resume(); }
  } graph_pause(graph_lock, coordinator_);

  // --- run the flow: logic -> optimize -> map ---
  auto* frame  = static_cast<Abc_Frame_t*>(pabc_);
  auto* pLogic = Abc_NtkToLogic(manNtk);
  Abc_NtkDelete(manNtk);
  Abc_FrameClearVerifStatus(frame);
  auto flow = (opts_.map_register || opts_.memory_fold != Memory_fold::Never) ? seq_flow() : comb_flow();
  if (opts_.verbose) {
    std::print("[pass.abc] region '{}': resolved flow: {}\n", rb.module_name, flow);
  }
  // Which mapping OBJECTIVE steps may run on this region. The budget ladder
  // (below) needs the built-in or a size-tier flow (`tool_owned_flow`: a user
  // command list is run verbatim and never re-sized), a Liberty the SCL steps
  // can walk, and a delay target. The area CANDIDATE is stricter: it belongs to
  // the BUILT-IN objective only (a size tier is a deliberately cheap or
  // deliberately direct mapper -- re-running the area baseline would defeat
  // it), it is bounded by `large_ge` even when the large tier is off (a second map
  // on a 123k-node mem_lower tile would double the ABC time of the one region
  // that already dominates), it is switched off by `area_flow=none`, and it
  // skips the dummy-PO sentinel (nothing to compare on a region with no real
  // outputs).
  const bool        ladder_on    = tool_owned_flow && scl_timing_ok_ && budget > 0.0f;
  const std::string area_cmd     = area_flow();
  const bool        candidate_on = ladder_on && !control_tier && builtin_flow && !area_cmd.empty() && !has_dummy_po
                                   && (opts_.large_ge == 0 || input_ge <= opts_.large_ge);
  // The area candidate re-maps from the SAME pre-flow logic network, so keep a
  // copy of it before the frame takes ownership of `pLogic`: every
  // Abc_FrameReplaceCurrentNetwork below DELETES the network it replaces. The
  // copy lives until the decision is made (or an early return), so at that
  // point two networks are alive at once -- the mapped result and this logic
  // dup -- which the RSS admission check after the flow sees as part of the
  // region's footprint.
  Abc_Ntk_t*        pre          = candidate_on ? Abc_NtkDup(pLogic) : nullptr;
  struct Pre_guard {
    Abc_Ntk_t** ntk;
    ~Pre_guard() {
      if (*ntk != nullptr) {
        Abc_NtkDelete(*ntk);
        *ntk = nullptr;
      }
    }
  } pre_guard{&pre};
  // Regions are independent synthesis jobs, not interactive ABC undo steps.
  // SetCurrentNetwork links the previous (potentially enormous) region as a
  // backup; carrying that network into every later job caused tiny regions to
  // stall after Rob's 10k-bit pack.  Replace deletes the old current network
  // while retaining the parsed Liberty library and command aliases.
  Abc_FrameReplaceCurrentNetwork(frame, pLogic);
  // The `{F}` tail is SCL: `buffer`/`dnsize` TIME the mapped network,
  // walking the per-pin NLDM tables of `pAbc->pLibScl`. A Liberty with no
  // `lu_table_template` builds none -- ABC says exactly that ("Templates are not
  // defined.") and then read_lib still returns 0, so `start()` above saw a
  // successful load. Running the tail on such a library does NOT fail the
  // command: Abc_SclTimeNode ASSERTS (sclSize.c, `assert(pCell->n_outputs > 1)`)
  // and aborts the whole lhd process with no diagnostic. Every small hermetic
  // test Liberty has this shape, so the shipped max_fanout=16 default took down
  // `lhd synth`/`pass abc` on all of them.
  //
  // Strip the tail rather than predicting it: the decision is a pure function of
  // the Liberty, and the Liberty content is already folded into the incremental
  // cache salt (Incr_cache::make_salt), so a recipe that still names the tail
  // cannot be reused across a library where the answer differs. The area
  // candidate's tail (`upsize {B}; dnsize {B}`) needs no strip: the candidate
  // only runs under scl_timing_ok_, which is the same predicate.
  if (opts_.max_fanout != 0) {
    // Same predicate the SCL gate uses. `vTempls` was a proxy for it and is
    // wrong in BOTH directions: the Liberty reader consumes the templates while
    // building the per-pin surfaces, so ASAP7 leaves it empty (the tail was
    // silently dropped and fanout left uncapped), while a scalar-only Liberty
    // that merely declares a template passed it and drove the SCL timer into
    // its abort.
    const auto*       scl  = static_cast<const SC_Lib*>(Abc_FrameReadLibScl());
    const bool        able = lib_has_nldm_timing(scl);
    const std::string tail = subst_flow(std::string{kBufferTail});
    if (!tail.empty() && flow.ends_with(tail) && !able) {
      flow.resize(flow.size() - tail.size());
      if (!warned_no_scl_) {
        warned_no_scl_ = true;
        // A plain note, not a diagnostic: this is a property of the LIBRARY, not
        // of the design, so it must not move `diagnostics_count` for every run
        // against a template-less Liberty (the same channel the register_max_bits
        // fallback above uses).
        std::print(
            "[pass.abc] max_fanout={}: '{}' has no 2-D slew/load NLDM timing tables, so ABC cannot size "
            "cells -- skipping `buffer -N`/`dnsize`; mapped fanout is NOT capped\n",
            opts_.max_fanout,
            startup_opts_.library);
      }
    }
  }
  // Can the area-recovery pass below replay JUST the mapper? `&undo` reverses one
  // GIA transformation, so the flow has to END with the built-in mapper step plus
  // kPutCmd and (when it is on) the buffering tail -- anything after that would
  // survive the undo and be applied twice. Derive it from the RESOLVED string
  // rather than from `tool_owned_flow`: that flag is computed before the
  // size-tier `small_flow`/`large_flow` substitution, so a tier flow is still
  // "tool owned" while being an arbitrary command list.
  const std::string map_step   = subst_flow(std::string{kMapCmd});
  const std::string flow_tail  = subst_flow(std::string{kBufferTail});
  const bool        tail_on    = !flow_tail.empty() && flow.ends_with(flow_tail);
  const std::string put_step   = std::string{"; "} + std::string{kPutCmd};
  const std::string remap_post = put_step + (tail_on ? flow_tail : "");
  const bool        remappable = tool_owned_flow && flow.ends_with(map_step + remap_post);
  if (Cmd_CommandExecute(frame, flow.c_str()) != 0) {
    livehd::diag::err("pass.abc", "abc-flow", "internal").msg("ABC flow failed for region '{}': {}", rb.module_name, flow).fatal();
    return;
  }

  // A delay is a BUDGET, in both directions -- and the budget is the target
  // minus the register margin (see `budget` above). The SCL timer sees one
  // region's combinational cone; OpenSTA's period check also pays the launch
  // flop's clk->Q and the capture flop's setup (69 ps of a 400 ps ASAP7 period
  // on br_arb_rr), so a region timed to the full target misses by exactly that.
  //
  // The ladder, cheapest step first, each only when the previous still misses:
  //
  //   1. the flow's own `buffer -N; dnsize -D <budget>` (already run);
  //   2. `upsize -D <budget>; dnsize -D <budget>`: `upsize -D` stops as soon as
  //      the SCL delay is inside the budget (sclUpsize.c) and the down-size
  //      recovers around it -- the bounded speed-grade step;
  //   3. `upsize; dnsize`, the UNBOUNDED sweep: `upsize` without a target
  //      chases the fastest cell assignment and `dnsize` then preserves that
  //      newly tightened delay instead of the budget. Only for a real miss.
  //
  // SLACK: `&nf -D` is silently IGNORED by ABC's mapper -- giaNf.c consults only
  // `Jf_Par_t::MapDelayTarget`, which the `-D` switch never writes (it sets the
  // unread `DelayTarget`), so the sole knob that relaxes required times is `-R`,
  // a PERCENTAGE of the mapper's own achieved delay (logic DEPTH on the
  // unit-delay GENLIB). Under a tight ASAP7 target the margin is nil; under a
  // relaxed sky130 one a region beats its clock by 6-380x. Re-map with the
  // measured slack handed back as `-R`, bounded by `area_relax`.
  //
  // Only `&nf` is repeated, not the whole flow: `&undo` restores the GIA the
  // mapper consumed (ABC keeps exactly one, in `pGia2`), so `&fraig`/`dc2`/`&dch`
  // -- the expensive part -- run once. It restores only ONE step, which is why
  // there is no second undo back to the minimum-delay netlist; the relaxation is
  // capped by the slack that was actually measured, and a miss is repaired with
  // the same budget-directed sizing step 2 uses.
  //
  // Then the AREA CANDIDATE (kAreaFlow): a region that met its budget is
  // re-mapped from the pre-flow copy with the former baseline + `buffer; upsize -D;
  // dnsize -D`, timed by the same SCL timer, and the netlist with the smaller
  // SCL area AMONG THOSE THAT MEET THE BUDGET is kept (the delay flow wins a
  // tie). Both networks are complete mapped logic networks, so the read-back
  // below is indifferent to which one won. Both use &put -o's decoupling
  // buffers and preserve the latches, including the QN encoding's ~f.
  //
  // Custom flows remain fully caller-owned: none of this touches them.
  const auto scl_qor = [&](Abc_Ntk_t* mapped) -> std::optional<std::pair<float, double>> {
    if (mapped == nullptr || !Abc_NtkIsMappedLogic(mapped)) {
      return std::nullopt;
    }
    // Matching ABC's `stime` (Abc_SclTimePerform): it first REFUSES a network
    // that is not in topo order or has dangling nodes -- the SCL timer
    // propagates in object-id order, so without that gate a bad network yields
    // a silently wrong number instead of no number.
    auto*                                   timing_ntk = mapped->nBarBufs2 > 0 ? Abc_NtkDupDfsNoBarBufs(mapped) : mapped;
    std::optional<std::pair<float, double>> out;
    if (Abc_SclCheckNtk(timing_ntk, 0)) {
      auto* timing = Abc_SclManStart(static_cast<SC_Lib*>(Abc_FrameReadLibScl()), timing_ntk, 0, 1, 0.0f, 0);
      out          = std::pair{timing->MaxDelay0, static_cast<double>(timing->SumArea0)};
      Abc_SclManFree(timing);
    }
    if (timing_ntk != mapped) {
      Abc_NtkDelete(timing_ntk);
    }
    return out;
  };
  // What the objective decided, for the QoR row (filled below).
  std::string                             candidate;
  std::optional<std::pair<float, double>> delay_flow_qor;
  std::optional<std::pair<float, double>> area_flow_qor;
  if (ladder_on) {
    const std::string size_to_budget = std::format("upsize {0}; dnsize {0}", budget_flag_);
    auto              d1             = scl_qor(Abc_FrameReadNtk(frame));
    if (d1 && d1->first > budget) {
      if (Cmd_CommandExecute(frame, size_to_budget.c_str()) != 0) {
        livehd::diag::err("pass.abc", "abc-flow", "internal")
            .msg("ABC budget sizing failed for region '{}' after missing delay budget {} ps: {}",
                 rb.module_name,
                 budget,
                 size_to_budget)
            .fatal();
        return;
      }
      d1 = scl_qor(Abc_FrameReadNtk(frame));
    }
    if (d1 && d1->first > budget) {
      if (Cmd_CommandExecute(frame, "upsize; dnsize") != 0) {
        livehd::diag::err("pass.abc", "abc-flow", "internal")
            .msg("ABC conditional sizing failed for region '{}' after missing delay budget {} ps", rb.module_name, budget)
            .fatal();
        return;
      }
      d1 = scl_qor(Abc_FrameReadNtk(frame));
    } else if (d1 && remappable) {
      const int relax = area_relax_percent(budget, d1->first, opts_.area_relax_pct);
      if (relax > 0) {
        const std::string remap = std::format("&undo; {} -R {}{}", map_step, relax, remap_post);
        if (Cmd_CommandExecute(frame, remap.c_str()) != 0) {
          livehd::diag::err("pass.abc", "abc-flow", "internal")
              .msg("ABC area-recovery remap failed for region '{}': {}", rb.module_name, remap)
              .fatal();
          return;
        }
        // The relaxation was derived from the SCL timer while `-R` relaxes the
        // mapper's own depth model, so the two can disagree. Repair with the
        // same budget-directed sizing step 2 uses rather than trusting the
        // request.
        d1 = scl_qor(Abc_FrameReadNtk(frame));
        if (d1 && d1->first > budget) {
          if (Cmd_CommandExecute(frame, size_to_budget.c_str()) != 0) {
            livehd::diag::err("pass.abc", "abc-flow", "internal")
                .msg("ABC sizing repair failed for region '{}' after area recovery", rb.module_name)
                .fatal();
            return;
          }
          d1 = scl_qor(Abc_FrameReadNtk(frame));
        }
      }
    }
    delay_flow_qor = d1;
    if (pre != nullptr && d1 && d1->first <= budget) {
      // Keep the delay flow's result aside (Abc_NtkDup copies the Mio gate
      // pointers of a mapped network, abcObj.c Abc_NtkDupObj) and hand the
      // pre-flow copy to the frame -- which deletes the delay result the frame
      // held -- for the second mapping.
      Abc_Ntk_t* delay_ntk = Abc_NtkDup(Abc_FrameReadNtk(frame));
      Abc_FrameReplaceCurrentNetwork(frame, pre);
      pre = nullptr;  // owned by the frame now
      if (Cmd_CommandExecute(frame, area_cmd.c_str()) != 0) {
        Abc_NtkDelete(delay_ntk);
        livehd::diag::err("pass.abc", "abc-flow", "internal")
            .msg("ABC area-candidate flow failed for region '{}': {}", rb.module_name, area_cmd)
            .fatal();
        return;
      }
      area_flow_qor = scl_qor(Abc_FrameReadNtk(frame));
      if (area_flow_qor && area_flow_qor->first <= budget && area_flow_qor->second < d1->second) {
        Abc_NtkDelete(delay_ntk);
        candidate = "area";
      } else {
        Abc_FrameReplaceCurrentNetwork(frame, delay_ntk);  // deletes the area result
        candidate = "delay";
      }
      if (opts_.verbose) {
        std::print("[pass.abc] region '{}': budget {} ps: delay flow {:.1f} ps / {:.2f}, area flow {} -> kept {}\n",
                   rb.module_name,
                   budget,
                   d1->first,
                   d1->second,
                   area_flow_qor ? std::format("{:.1f} ps / {:.2f}", area_flow_qor->first, area_flow_qor->second) : "untimed",
                   candidate);
      }
    }
  }
  if (pre != nullptr) {
    Abc_NtkDelete(pre);  // the candidate did not run (budget missed, or untimed)
    pre = nullptr;
  }
  trace_stage("flow-complete");

  // Translation is sampled repeatedly above, but ABC's optimization/mapping
  // command can create several network forms between those samples. Check the
  // exact live footprint again at the color boundary. Calling with a completed
  // fraction suppresses extrapolation and reports the real post-flow RSS.
  if (!opts_.allow_oversize && over_budget(rb.module_name, rss_before, blast_total, blast_total)) {
    return;  // mapper.stop() owns the current ABC network; work() raises refusal_
  }

  graph_pause.resume();

  // --- QoR read-back (2opt-freq A): critical delay/area/gates from the Liberty
  // pin-to-pin data while the flow's result is still a mapped LOGIC network
  // (Abc_NtkDelayTrace requires one; the netlist conversion below is only for
  // the gate read-back). Per-region numbers: paths crossing the region or
  // blackbox boundary are pass.opentimer's job, not scored here.
  {
    Region_qor q;
    q.satopt_facts = fact_keys.size();
    q.module       = rb.module_name;
    q.color        = rb.color;
    q.ctrl         = rb.ctrl;
    q.input_nodes  = input_nodes;
    q.input_ge     = input_ge;
    q.pred_aig     = pred_aig;
    for (const auto& bb : bboxes) {
      if (bb.op == Ntype_op::Div || bb.op == Ntype_op::Rem) {
        ++q.div_blackbox;  // unmapped cone: the region score is partial
      }
    }
    q.budget        = budget;
    q.candidate     = candidate;
    q.boundary_bits = boundary_bits;
    if (delay_flow_qor) {
      q.delay_flow_delay = delay_flow_qor->first;
      q.delay_flow_area  = delay_flow_qor->second;
    }
    if (area_flow_qor) {
      q.area_flow_delay = area_flow_qor->first;
      q.area_flow_area  = area_flow_qor->second;
    }
    if (auto* pMappedLogic = Abc_FrameReadNtk(frame); pMappedLogic != nullptr && Abc_NtkIsMappedLogic(pMappedLogic)) {
      q.delay       = Abc_NtkDelayTrace(pMappedLogic, nullptr, nullptr, 0);
      q.area        = Abc_NtkGetMappedArea(pMappedLogic);
      q.gates       = Abc_NtkNodeNum(pMappedLogic);
      q.logic_depth = Abc_NtkLevel(pMappedLogic);
      if (scl_timing_ok_) {
        // Abc_NtkDelayTrace reads the unit-delay GENLIB (logic depth). Once the
        // sizing steps have selected concrete drive strengths, time the
        // resulting network with those cells' actual NLDM surfaces, matching
        // ABC's `stime` (the same timer the budget ladder judged by).
        if (const auto phys = scl_qor(pMappedLogic)) {
          q.delay = phys->first;
          q.area  = phys->second;
        }
      }
      // Worst-arrival REGION output (the delay trace leaves per-node arrivals
      // behind; POs beyond po_order are blackbox-input cuts, not outputs).
      // NOTE: these arrivals are the GAIN-model ones -- the SCL timer's per-node
      // times die with its SC_Man above -- so with a physical GENLIB
      // `crit_output`/`crit_src` name the gain-model worst output, which need
      // not be the one that sets `q.delay`.
      float      worst = -1.0f;
      int        wpo   = -1;
      Abc_Obj_t* pPo   = nullptr;
      int        poi   = 0;
      Abc_NtkForEachPo(pMappedLogic, pPo, poi) {
        if (poi >= static_cast<int>(po_order.size())) {
          break;
        }
        float arr = Abc_NodeReadArrivalWorst(Abc_ObjFanin0(pPo));
        if (arr > worst) {
          worst = arr;
          wpo   = static_cast<int>(po_order[static_cast<size_t>(poi)].first);
        }
      }
      if (wpo >= 0) {
        qor_src_of_output(rb, static_cast<size_t>(wpo), q);
      }
    }
    qor_.push_back(std::move(q));
  }

  auto* mapped = Abc_NtkToNetlist(Abc_FrameReadNtk(frame));
  if (mapped == nullptr || !Abc_NtkHasMapping(mapped)) {
    livehd::diag::err("pass.abc", "abc-unmapped", "internal")
        .msg("ABC produced no mapped netlist for region '{}' (check the Liberty library)", rb.module_name)
        .fatal();
    if (mapped != nullptr) {
      Abc_NtkDelete(mapped);
    }
    return;
  }
  trace_stage("netlist-ready");

  // --- read back: each mapped gate -> a 1-bit blackbox Sub in the body ---
  auto* body = rb.body;

  // Source-map carry-through (task 2a-abc): ABC's strash/dch destroy per-node
  // provenance, so re-mint each output port's original driver srcid. Into the
  // output LIBRARY's shared srcmap, never the body's own locator: a per-body
  // import re-copies the per-FILE metadata (line-offset tables) into every
  // region body -- the pass.partition std::bad_alloc shape -- while the body
  // resolves the id through its library base chain either way.
  auto&                       out_srcmap = body->get_io()->get_library()->source_map();
  std::vector<hhds::SourceId> po_srcid(rb.outputs.size(), hhds::SourceId_invalid);
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    auto drv = rb.outputs[po].src_driver;
    if (drv.is_invalid()) {
      continue;
    }
    auto onode = drv.get_master_node();
    if (onode.is_invalid()) {
      continue;
    }
    if (auto a = onode.attr(hhds::attrs::srcid); a.has() && a.get() != 0) {
      po_srcid[po] = out_srcmap.import_from(rb.src->source_locator(), a.get());
    }
  }

  // find-or-declare a 1-bit blackbox cell def (Liberty pins) in the out library
  auto cell_desc = [&](Mio_Gate_t* g) -> Cell_desc& { return cell_desc_for(g); };

  // Select one bit as an explicit Get_mask with the one-hot `(1 << b)` mask.
  //
  // This used to be `(bus >> b)` stamped to one bit, which kept a W-bit
  // boundary's selector constants at O(W log W) instead of the O(W^2) the
  // one-hot bigints cost. That form is NOT reload-safe (see below), so the
  // one-hot is the price of correctness; a W in the tens of thousands (whole
  // design flatten forces this path at every width, below) pays ~W^2/8 bytes of
  // pooled constants. Revisit as `Get_mask(SRA(bus, b), mask=1)` -- 3 nodes per
  // bit but log-sized constants -- if that ever becomes the bottleneck.
  auto extract_body_bit = [&](const hhds::Pin_class& bus, int b) {
    // A shift with a one-bit hint is still an unlimited-precision shift.
    // Its high bits reappear when a saved mapped graph is compiled again,
    // violating any Concat lane it feeds. Select the bit explicitly.
    auto select = gu::create_typed_node(*body, Ntype_op::Get_mask);
    bus.connect_sink(gu::setup_sink_by_name(select, "a"));
    gu::create_const(*body, *Dlop::get_mask_value(b, b)).connect_sink(gu::setup_sink_by_name(select, "mask"));
    auto out = select.create_driver_pin(0);
    gu::set_ubits(out, 1);
    return out;
  };

  // Lazily build bit b of a body input pin (compact shift-select; pin itself if 1-bit).
  std::vector<std::vector<hhds::Pin_class>> in_bit(rb.inputs.size());
  std::vector<hhds::Pin_class>              body_input_pin(rb.inputs.size());
  std::vector<hhds::Node_class>             body_input_splitter(rb.inputs.size());
  for (size_t port_idx = 0; port_idx < rb.inputs.size(); ++port_idx) {
    body_input_pin[port_idx] = body->get_input_pin(rb.inputs[port_idx].name);
  }
  // Which bits of each region input the mapping actually reads, MOST
  // SIGNIFICANT FIRST. `pi_order` is FINAL here -- abc_bit appends to it during
  // bit-blast and creates each (port, bit) PI at most once -- so the read-back
  // knows every port's exact demand before it materializes anything. Without it
  // the unpacker below would have to assume the whole bus is read.
  //
  // The descending sort is load-bearing, not cosmetic: hhds keeps a node's pins
  // in ascending port order and both insert paths break at the list head, so a
  // strictly DECREASING sequence costs O(1) per pin -- sparse or dense --
  // whereas ascending rescans the whole list for every bit.
  std::vector<std::vector<int>> port_demand(rb.inputs.size());
  for (const auto& [demanded_pi, demanded_bit] : pi_order) {
    port_demand[demanded_pi].push_back(demanded_bit);
  }
  for (auto& bits : port_demand) {
    std::sort(bits.begin(), bits.end(), std::greater<int>());
  }
  auto shared_input_splitter = [&](int width) -> Input_splitter& {
    if (auto it = input_splitters_.find(width); it != input_splitters_.end()) {
      return it->second;
    }

    Input_splitter split;
    auto           name = std::format("__livehd_abc_input_bits_{}", width);
    split.bit_port.resize(width);
    // FIND-or-create, exactly like blackbox_io above: an incremental cache HIT
    // re-declares the splitter defs a reused body references, and a second
    // pass.abc into the same output library sees the ones this run created.
    // A second create_io under a live name aborts the library.
    split.io         = outlib_->find_io(name);
    const bool fresh = !split.io;
    if (fresh) {
      split.io = outlib_->create_io(name);
      split.io->add_input("a", 1);
      split.io->set_bits("a", width);
    }
    // HHDS stores a node's nonzero-port pins in ascending order. Adding ports
    // in descending order always inserts at the head; ascending order rescans
    // the whole list for every bit (O(width^2), 52M comparisons at 10260b).
    for (int b = width - 1; b >= 0; --b) {
      split.bit_port[b] = static_cast<hhds::Port_id>(b + 2);
      if (fresh) {
        auto output = std::format("b{}", b);
        split.io->add_output(output, split.bit_port[b]);
        split.io->set_bits(output, 1);
      }
    }
    if (!split.io->get_graph()) {  // a re-declared def carries the IO only
      auto split_body = split.io->create_graph();
      auto input      = split_body->get_input_pin("a");
      for (int b = width - 1; b >= 0; --b) {
        auto output = std::format("b{}", b);
        auto shift  = gu::create_typed_node(*split_body, Ntype_op::SRA);
        input.connect_sink(gu::setup_sink_by_name(shift, "a"));
        gu::create_const(*split_body, *Dlop::create_integer(b)).connect_sink(gu::setup_sink_by_name(shift, "b"));
        auto bit = shift.create_driver_pin(0);
        gu::set_bits(bit, 1);
        gu::set_unsign(bit);
        bit.connect_sink(split_body->get_output_pin(output));
      }
      split_body->commit();
    }
    auto [it, inserted] = input_splitters_.emplace(width, std::move(split));
    I(inserted);
    return it->second;
  };
  auto input_bit = [&](size_t port_idx, int b) -> hhds::Pin_class {
    auto&       cache = in_bit[port_idx];
    const auto& port  = rb.inputs[port_idx];
    const int   w     = port.bits == 0 ? 1 : port.bits;
    // Sized by b, not by w: abc_bit only clamps a demanded bit against a
    // NON-ZERO width (`if (w != 0 && i >= w)`), so an unstamped port -- w
    // forced to 1 here -- can legitimately demand bit 5 and index past a
    // w-sized cache.
    if (static_cast<int>(cache.size()) <= std::max(b, w - 1)) {
      cache.resize(std::max(b + 1, w));
    }
    if (!cache[b].is_invalid()) {
      return cache[b];
    }
    auto ipin = body_input_pin[port_idx];
    if (w == 1 && b == 0) {
      cache[b] = ipin;  // the pin IS its only bit
      return ipin;
    }
    // The shared splitter DEF is all-or-nothing (its body and its w output
    // decls are minted together and carried through the region cache as one
    // unit), so it only pays when this region reads MOST of a wide bus: on a
    // region reading 20 bits of a 10k-bit port the def alone is ~2000x the work
    // the lazy PI path just avoided, and one port can never repay it. It is not
    // free otherwise either -- it puts a non-cell module in the emitted netlist
    // (a whole-design flatten is contracted to emit exactly ONE module, see
    // lhd_abc_flat_test) and it has to be carried through the region cache.
    // Below any of the three gates, extract the demanded bits in place -- the
    // same shift-select the blackbox/latch read-back uses, at 2 nodes per
    // DEMANDED bit, with no def at all.
    constexpr int kSharedSplitterMinBits = 256;
    const auto&   demand                 = port_demand[port_idx];
    if (flat_ || w < kSharedSplitterMinBits || static_cast<int>(demand.size()) < w - w / 2) {
      cache[b] = extract_body_bit(ipin, b);
      return cache[b];
    }
    auto& inst = body_input_splitter[port_idx];
    if (inst.is_invalid()) {
      auto& split = shared_input_splitter(w);
      inst        = gu::create_typed_node(*body, Ntype_op::Sub);
      inst.set_subnode(split.io);
      ipin.connect_sink(inst.create_sink_pin(1));
      // Only the DEMANDED bits, and in the descending order `demand` is already
      // sorted into, so each pin is a head insert. The INSTANCE may be partial
      // even though the def is not: every consumer of a Sub resolves its ports
      // from EDGES (cgen create_subs, cgen_sim, lec encode), and a pin for an
      // unread bit would carry no edge in either case. Retaining every handle
      // also keeps the PI loop from searching the node's long pin list.
      for (int bit : demand) {
        cache[bit] = inst.create_driver_pin(split.bit_port[bit]);
      }
    }
    if (cache[b].is_invalid()) {  // a bit outside the precomputed demand
      cache[b] = inst.create_driver_pin(shared_input_splitter(w).bit_port[b]);
    }
    return cache[b];
  };

  // ABC object IDs are dense indices. Direct vectors avoid two pointer-hash
  // operations for every mapped edge during read-back (millions on Rob).
  const auto                    mapped_obj_slots = static_cast<size_t>(Abc_NtkObjNumMax(mapped));
  std::vector<hhds::Pin_class>  net2drv(mapped_obj_slots);
  std::vector<hhds::Node_class> mapped_node2sub(mapped_obj_slots);
  // Identity-buffer bypass (is_identity_gate above): a bypassed buffer's OUTPUT
  // net resolves to its INPUT net. Resolved lazily in get_net_driver rather
  // than copied at bypass time because the input net's driver may not exist
  // yet -- a latch Q net is only filled in pass 1c, after the gate pass -- and
  // every consumer (gate fanins, flop/DFF din, POs, blackbox inputs) already
  // goes through get_net_driver, so no pass has to move.
  std::vector<int32_t>          net_alias(mapped_obj_slots, -1);
  int                           bypassed_bufs  = 0;
  auto                          set_net_driver = [&](Abc_Obj_t* net, const hhds::Pin_class& driver) {
    I(net != nullptr);
    const auto id = static_cast<size_t>(Abc_ObjId(net));
    I(id < net2drv.size());
    net2drv[id] = driver;
  };
  auto get_net_driver = [&](Abc_Obj_t* net) -> hhds::Pin_class {
    if (net == nullptr) {
      return {};
    }
    auto id = static_cast<size_t>(Abc_ObjId(net));
    // Follow the alias chain (a bypassed buffer fed by another bypassed
    // buffer). An alias only ever points from a buffer's output net to its
    // input net, so the chain is acyclic and ends at a real driver.
    while (id < net_alias.size() && net_alias[id] >= 0) {
      id = static_cast<size_t>(net_alias[id]);
    }
    if (id >= net2drv.size()) {
      return {};
    }
    return net2drv[id];
  };
  int        i    = 0;
  Abc_Obj_t* pObj = nullptr;

  // pass 1.bbox: rebuild each blackbox node (Sub instance / memory) natively.
  // Its output pins drive the boundary PIs (mapped in pass 1a); its inputs are
  // wired in pass 2c once their driving cones resolve. Const inputs are wired now.
  struct Bbox_recon {
    hhds::Node_class                          node;
    std::vector<hhds::Pin_class>              out_pin;  // [out idx] -> reconstructed full-width driver
    std::vector<std::vector<hhds::Pin_class>> out_bit;  // [out idx][bit] -> body driver
    std::vector<std::vector<hhds::Pin_class>> in_bit;   // [in idx][bit] -> body driver (filled pass 3)
  };
  // Only these boundary bits became ABC PIs.  Recreate selectors for exactly
  // that set; extracting every bit of the full bus here would merely move the
  // eager explosion from translation to readback.
  std::vector<std::vector<std::vector<int>>> bbox_demand(bboxes.size());
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    bbox_demand[bi].resize(bboxes[bi].outs.size());
  }
  for (const auto& [bx, oi, bit] : bbox_pi) {
    I(bx >= 0 && static_cast<size_t>(bx) < bbox_demand.size());
    I(oi >= 0 && static_cast<size_t>(oi) < bbox_demand[static_cast<size_t>(bx)].size());
    bbox_demand[static_cast<size_t>(bx)][static_cast<size_t>(oi)].push_back(bit);
  }
  std::vector<Bbox_recon> bbox_recon(bboxes.size());
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    auto& bb = bboxes[bi];
    auto& br = bbox_recon[bi];
    auto  nn = gu::create_typed_node(*body, bb.op);
    if (bb.op == Ntype_op::Sub) {
      if (auto child = bb.node.get_subnode_io()) {
        // A def with a body was partitioned children-first; a body-less black
        // box (e.g. a liberty cell when re-mapping an already-mapped netlist)
        // is cloned as an IO-only decl so the instance stays opaque.
        if (auto out_child = livehd::partition::resolve_or_clone_subdef(outlib_, bb.node)) {
          nn.set_subnode(out_child);
        } else {
          livehd::diag::err("pass.abc", "missing-subdef", "unsupported")
              .msg("pass.abc: sub-instance in region '{}' references child def '{}' missing from the output library",
                   rb.module_name,
                   std::string{child->get_name()})
              .emit();
          unsupported = true;
        }
      }
    }
    if (auto nm = gu::node_name_of(bb.node); !nm.empty()) {
      nn.attr(hhds::attrs::name).set(std::string{nm});
    }
    if (auto sid = bb.node.attr(hhds::attrs::srcid); sid.has() && sid.get() != 0) {
      nn.attr(hhds::attrs::srcid).set(out_srcmap.import_from(rb.src->source_locator(), sid.get()));
    }
    if (native_comb_logic.contains(bb.node)) {
      nn.attr(livehd::attrs::native_comb_boundary).set({});
    }
    br.node = nn;
    br.out_pin.resize(bb.outs.size());
    br.out_bit.resize(bb.outs.size());
    for (size_t oi = 0; oi < bb.outs.size(); ++oi) {
      const auto& o  = bb.outs[oi];
      auto        dp = nn.create_driver_pin(o.port_id);
      gu::set_bits(dp, o.bits);
      if (o.sign) {
        gu::set_sign(dp);
      }
      br.out_pin[oi] = dp;
      // A native boundary output is split into one ABC PI per bit and then
      // reassembled here.  Width/sign alone are not enough: for a latch the
      // driver pin is the state bus, and its pin_name is the stable RTL name
      // used by cgen/OpenTimer after the mapped region is read back.  Losing it
      // renames a wide latch to the synthetic node name (or, for non-zero
      // ports, <node>_<pid>) and makes the per-bit boundary impossible to map
      // back to the original bus.  Keep the same pin metadata partition does.
      if (auto pn = gu::pin_name_of(o.src_pin); !pn.empty()) {
        gu::set_pin_name(dp, pn);
      }
      if (auto off = o.src_pin.attr(livehd::attrs::pin_offset); off.has()) {
        dp.attr(livehd::attrs::pin_offset).set(off.get());
      }
      br.out_bit[oi].resize(o.bits);
      const auto& demand = bbox_demand[bi][oi];
      if (!o.abc_bits || demand.empty()) {
        continue;
      }
      if (o.bits == 1) {
        br.out_bit[oi][0] = dp;
      } else {
        for (int b : demand) {
          I(b >= 0 && b < o.bits);
          br.out_bit[oi][b] = extract_body_bit(dp, b);
        }
      }
    }
    // Declared outputs with no consumer in the source region still get a driver
    // pin (edge-less), mirroring tolg: readers probe every declared output
    // (cgen create_subs, LEC pairing) and hhds find_pin asserts on a pin that
    // was never created. Width/sign come from the child decl (the source pin
    // is edge-less too, so hhds cannot enumerate it).
    if (bb.op == Ntype_op::Sub) {
      if (auto sio = nn.get_subnode_io()) {
        absl::flat_hash_set<int> made;
        for (const auto& o : bb.outs) {
          made.insert(o.port_id);
        }
        for (const auto& d : sio->get_output_pin_decls()) {
          if (!made.insert(static_cast<int>(d.port_id)).second) {
            continue;
          }
          auto dp = nn.create_driver_pin(d.port_id);
          gu::set_bits(dp, d.bits != 0 ? static_cast<int>(d.bits) : 1);
          // No sign stamp: decl.unsign==false also means "unspecified" (e.g.
          // blackbox cell decls), and an edge-less pin has no reader — leave
          // the attr absent (the unsigned default) rather than plant `signed`.
        }
      }
    }
    for (const auto& [pid, cdrv] : bb.const_ins) {
      gu::create_const(*body, gu::const_of(cdrv)).connect_sink(nn.create_sink_pin(pid));
    }
    // flop boundary: control pins straight from a region input reconnect to the
    // body input pin directly (the clock/reset never enters the AIG).
    for (const auto& [pid, src_drv] : bb.native_ins) {
      if (auto it = region_in_name.find(src_drv); it != region_in_name.end()) {
        body->get_input_pin(it->second).connect_sink(nn.create_sink_pin(pid));
      }
    }
    br.in_bit.resize(bb.ins.size());
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      br.in_bit[ii].assign(bb.ins[ii].bits, hhds::Pin_class{});
    }
  }
  // Reconnect native boundary-to-boundary buses without exploding them into
  // ABC PIs/POs. This is what makes a wide SHL -> packing OR chain remain one
  // named bus on read-back instead of millions of per-bit interface objects.
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> native_boundary_driver;
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    for (size_t oi = 0; oi < bboxes[bi].outs.size(); ++oi) {
      native_boundary_driver.emplace(bboxes[bi].outs[oi].src_pin, bbox_recon[bi].out_pin[oi]);
    }
  }
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    for (const auto& [pid, src_drv] : bboxes[bi].native_ins) {
      if (region_in_name.contains(src_drv)) {
        continue;  // already connected above from the body input pin
      }
      if (auto it = native_boundary_driver.find(src_drv); it != native_boundary_driver.end()) {
        it->second.connect_sink(bbox_recon[bi].node.create_sink_pin(pid));
      }
    }
    for (const auto& [pid, src_drv, bits, sign] : bboxes[bi].fit_native_ins) {
      hhds::Pin_class source;
      if (auto nit = region_in_name.find(src_drv); nit != region_in_name.end()) {
        source = body->get_input_pin(nit->second);
      } else if (auto bit = native_boundary_driver.find(src_drv); bit != native_boundary_driver.end()) {
        source = bit->second;
      }
      if (source.is_invalid()) {
        continue;
      }
      auto fit = gu::create_typed_node(*body, Ntype_op::Get_mask);
      source.connect_sink(gu::setup_sink_by_name(fit, "a"));
      gu::create_const(*body, *Dlop::get_mask_value(bits)).connect_sink(gu::setup_sink_by_name(fit, "mask"));
      auto fitted = fit.create_driver_pin(0);
      gu::set_bits(fitted, bits);
      sign ? gu::set_sign(fitted) : gu::set_unsign(fitted);
      fitted.connect_sink(bbox_recon[bi].node.create_sink_pin(pid));
    }
  }
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    if (!direct_native_output[po]) {
      continue;
    }
    if (auto it = native_boundary_driver.find(rb.outputs[po].src_driver); it != native_boundary_driver.end()) {
      it->second.connect_sink(body->get_output_pin(rb.outputs[po].name));
    }
  }
  if (unsupported) {
    Abc_NtkDelete(mapped);
    return;
  }
  trace_stage("readback-boundaries");

  // pass 1a: PI nets -> body input bit drivers (match by creation order — ABC
  // preserves CI/CO order across the flow, more robust than name parsing).
  Abc_NtkForEachPi(mapped, pObj, i) {
    if (i >= static_cast<int>(all_pi_order.size())) {
      continue;
    }
    const auto origin = all_pi_order[i];
    if (origin.kind == Pi_kind::region_input) {
      const auto [pi, b] = pi_order[origin.index];
      set_net_driver(Abc_ObjFanout0(pObj), input_bit(pi, b));
    } else {
      const auto [bx, oi, b] = bbox_pi[origin.index];
      set_net_driver(Abc_ObjFanout0(pObj), bbox_recon[bx].out_bit[oi][b]);
    }
  }
  trace_stage("readback-pis");

  // Surviving latches (stable vBoxes order) and their source-flop
  // correspondence. Shared by pass 1b's QN-inversion absorption and pass 1c's
  // register read-back.
  //
  // Per-latch source flop, so clock, reset and init are decided PER FLOP (not
  // region-wide): the crossing creates latches in flops order, one per bit, so
  // when the latch count is preserved (the default flow does not retime) latch
  // k maps to its origin flop. A retime-reshaped count falls back to the first
  // flop (clock/reset) and to ABC's own latch init.
  std::vector<Abc_Obj_t*>      lat;
  std::vector<const Seq_flop*> latch_owner;
  std::vector<int>             latch_owner_bit;
  int                          crossed_bits = 0;
  if (opts_.map_register && !flops.empty()) {
    Abc_NtkForEachLatch(mapped, pObj, i) { lat.push_back(pObj); }
    for (const auto& f : flops) {
      crossed_bits += f.bits;
    }
    if (static_cast<int>(lat.size()) == crossed_bits) {
      for (const auto& f : flops) {
        for (int b = 0; b < f.bits; ++b) {
          latch_owner.push_back(&f);
          latch_owner_bit.push_back(b);
        }
      }
    }
  }
  // A latch init is a TRUE power-on value only when its source flop has NO
  // reset. With a (synchronous) reset, the init is the reset value — already
  // folded into the D cone — so the flop resets to it and LEC pins reset; a
  // plain DFF cell (power-on X, exactly like the reset flop's own cgen) is then
  // equivalent and the bit maps to a cell. Only a resetless init must keep its
  // native flop.
  //
  // Retime-reshaped fallback (no per-latch owner): a retimed latch has no
  // single source register, so "reset-backed" is a REGION property there. It
  // holds only when every crossed register has a reset -- then no latch can
  // carry a power-on contract and all of them may take cells. With even one
  // resetless-init register in the mix a latch with a concrete init is kept
  // native (with that init): dropping a real power-on value is the silent
  // miscompile, keeping an extra native flop is merely conservative. This is
  // reachable only under a user retime flow (`dretime`) on a region mixing
  // memory-init flops (mem_lower) with reset registers.
  auto owner_has_reset = [&](int k) -> bool {
    if (k < static_cast<int>(latch_owner.size())) {
      return latch_owner[k]->has_reset;
    }
    return std::all_of(flops.begin(), flops.end(), [](const Seq_flop& f) { return f.has_reset; });
  };
  // ABC is allowed to pick a concrete value for a don't-care latch init while
  // optimizing. That choice is an internal optimization witness, NOT a new
  // hardware power-on guarantee. Use the source snapshot to decide WHETHER
  // the bit has an init. Its VALUE must come from the transformed ABC latch:
  // ABC can complement a known-one latch and invert its readers, even without
  // changing the latch count. Restoring the original one then initializes the
  // complemented state incorrectly. An init-less/unknown source still stays
  // unconstrained regardless of ABC's internal witness.
  //
  // Answers the POWER-ON init only: a reset-backed bit's `initial` is its
  // reset value, realized on D, and is dropped here on purpose -- a rebuilt
  // native flop or a DFF cell carrying it as a power-on value would claim a
  // start state the source register (power-on X, then reset) never had.
  auto source_init_bit = [&](int k) -> std::optional<bool> {
    if (owner_has_reset(k)) {
      return std::nullopt;
    }
    if (k < static_cast<int>(latch_owner.size())) {
      const auto* f = latch_owner[k];
      if (!f->has_init || f->init_val.unknown_bit_test(latch_owner_bit[k])) {
        return std::nullopt;
      }
      const int transformed = Abc_LatchInit(lat[k]);
      if (transformed == 1 || transformed == 2) {
        return transformed == 2;
      }
      return f->init_val.bit_test(latch_owner_bit[k]);
    }
    int v = Abc_LatchInit(lat[k]);  // 1=zero, 2=one, else dc/none
    if (v == 1 || v == 2) {
      return v == 2;
    }
    return std::nullopt;
  };
  // resetless power-on init: such a bit must keep a native flop so the value
  // survives (a plain DFF cell has no init pin)
  auto needs_native = [&](int k) -> bool { return source_init_bit(k).has_value(); };

  // QN cell (dff_->q_inverted): the cell computes QN(t+1) = !D(t) and the
  // read-back wires its QN pin as the register's Q, so the D pin must see ~f.
  // A latch crossed with Seq_flop::d_inverted already holds it (built-in flow).
  // For every OTHER cell-bound latch -- a user or size-tier flow, which may
  // retime, or a reshaped latch count -- `qn_dnet` holds the data-in nets that
  // feed NOTHING but that latch's BI, so their driver can be rewritten to
  // compute ~f without touching any other consumer. Pass 1b absorbs the
  // inversion there: a root inverter is dropped (the net aliases to the
  // inverter's input), any other root gate is swapped for its inverting twin
  // when the library has one (twin_index_; AND2x2 -> NAND2xp33 is a saving,
  // AOI21xp33 -> AO21x1 costs 0.0146, both below INVx1's 0.0437), and what it
  // could not absorb gets one inverter on D in pass 2b. Exact under any flow
  // (it is a local rewrite of the MAPPED netlist, not of the machine ABC
  // optimized). yosys keeps the inversion on the Q side as an INV cell that
  // survives on ~40% of ASAP7 flops; the D side has fanout 1, so a min-size
  // cell always suffices and the register's own drive ladder carries the Q
  // fanout.
  absl::flat_hash_set<Abc_Obj_t*> qn_dnet;
  absl::flat_hash_set<Abc_Obj_t*> qn_absorbed;  // subset whose driver now computes ~f
  if (dff_.has_value() && dff_->q_inverted) {
    for (size_t k = 0; k < lat.size(); ++k) {
      if (needs_native(static_cast<int>(k)) || (k < latch_owner.size() && latch_owner[k]->d_inverted)) {
        continue;
      }
      auto* dnet = Abc_ObjFanin0(Abc_ObjFanin0(lat[k]));  // latch <- BI <- D net
      if (Abc_ObjFanoutNum(dnet) == 1) {
        qn_dnet.insert(dnet);
      }
    }
  }
  auto* const  mio_inv     = static_cast<Mio_Gate_t*>(Mio_LibraryReadInv(static_cast<Mio_Library_t*>(Abc_FrameReadLibGen())));
  const double inv_area    = mio_inv != nullptr ? Mio_GateReadArea(mio_inv) : 0.0;
  auto         is_inverter = [](Mio_Gate_t* g) {
    return Mio_GateReadPinNum(g) == 1 && static_cast<uint64_t>(Mio_GateReadTruth(g)) == ~UINT64_C(0xAAAAAAAAAAAAAAAA);
  };
  auto inverting_twin = [&](Mio_Gate_t* g) -> Mio_Gate_t* {
    const int n = Mio_GateReadPinNum(g);
    if (n > 6) {
      return nullptr;
    }
    auto it = twin_index_.find(std::pair<int, uint64_t>{n, ~static_cast<uint64_t>(Mio_GateReadTruth(g))});
    return it == twin_index_.end() ? nullptr : static_cast<Mio_Gate_t*>(it->second);
  };
  int qn_dropped = 0;  // root inverters removed
  int qn_swapped = 0;  // root gates replaced by their inverting twin

  // pass 1b: each mapped gate -> a Sub; map its output net -> Sub output pin.
  // A decoupling buffer -- single-input identity gate whose output feeds only
  // COs -- is not a gate at all (see is_identity_gate): alias its output net to
  // its input net and mint nothing, so the CO reads the buffer's own driver
  // (a PI bit, a latch Q, a blackbox output, or the gate that `&put -o` would
  // otherwise have duplicated). No cell_desc call either: a bypassed cell must
  // not leave an unused cell decl in the output library.
  std::vector<std::pair<hhds::Node_class, Abc_Obj_t*>> gates;
  double                                               emitted_area = 0.0;
  Abc_NtkForEachNode(mapped, pObj, i) {
    auto* g = static_cast<Mio_Gate_t*>(pObj->pData);
    if (g == nullptr) {
      // A mapped node without Mio data cannot be read back — skipping it
      // would silently collapse its fanout cone to const0 (seen with
      // multi-output supergates before read_lib -s). Never miscompile.
      livehd::diag::err("pass.abc", "abc-readback", "internal")
          .msg("region '{}': mapped node {} carries no Mio gate — unreadable mapping (multi-output cell?)",
               rb.module_name,
               Abc_ObjId(pObj))
          .fatal();
      Abc_NtkDelete(mapped);
      return;
    }
    if (Abc_ObjFaninNum(pObj) == 1 && is_identity_gate(g)) {
      auto* onet = Abc_ObjFanout0(pObj);  // netlist: node -> its output net
      if (only_co_fanouts(onet)) {
        // output net -> input net; mapped_node2sub stays invalid for this node
        // (the srcmap attribution walk below guards on it)
        net_alias[static_cast<size_t>(Abc_ObjId(onet))] = static_cast<int32_t>(Abc_ObjId(Abc_ObjFanin0(pObj)));
        ++bypassed_bufs;
        continue;
      }
    }
    if (auto* onet = Abc_ObjFanout0(pObj); qn_dnet.contains(onet)) {
      // The D-cone root of a QN-cell register (see qn_dnet): absorb the
      // inversion here when that is free or cheaper than an inverter.
      if (Abc_ObjFaninNum(pObj) == 1 && is_inverter(g)) {
        net_alias[static_cast<size_t>(Abc_ObjId(onet))] = static_cast<int32_t>(Abc_ObjId(Abc_ObjFanin0(pObj)));
        qn_absorbed.insert(onet);
        ++qn_dropped;
        continue;
      }
      if (auto* twin = inverting_twin(g); twin != nullptr && Mio_GateReadArea(twin) - Mio_GateReadArea(g) < inv_area) {
        // The twin computes ~g over the same pins in the same order, so pass 2
        // wires it exactly like g. The ABC node takes the twin so that the
        // mapped network and the netlist agree (srcmap, cell_desc).
        pObj->pData = twin;
        g           = twin;
        qn_absorbed.insert(onet);
        ++qn_swapped;
      }
    }
    auto& desc = cell_desc(g);
    auto  sub  = gu::create_typed_node(*body, Ntype_op::Sub);
    sub.set_subnode(desc.io);
    sub.attr(hhds::attrs::name).set(std::format("g{}_{}", Abc_ObjId(pObj), desc.name));
    auto outpin = sub.create_driver_pin(desc.output_name);
    gu::set_bits(outpin, 1);
    gu::set_unsign(outpin);
    set_net_driver(Abc_ObjFanout0(pObj), outpin);
    const auto obj_id = static_cast<size_t>(Abc_ObjId(pObj));
    I(obj_id < mapped_node2sub.size());
    mapped_node2sub[obj_id] = sub;
    gates.emplace_back(sub, pObj);
    emitted_area += Mio_GateReadArea(g);
  }
  // QoR accounting. `gates`/`area` were read off the mapped LOGIC network
  // above, which still carries the decoupling buffers -- and Abc_NtkToNetlist
  // can add a few more of its own (a `buffer -N` leaf left feeding several
  // COs), which that count never saw. abc.json `total.area` is what lhdtrack
  // scores as lhd_area and the incremental cache stores this same row, so
  // both must describe the cells actually minted: br_demux_onehot reported
  // 528 gates / 31.26 um^2 for a netlist of 16 cells. Mio's gate area is the
  // Liberty area (the GENLIB is derived from the parsed SC_Lib), i.e. the
  // same quantity the SCL timer summed. `delay` is deliberately left alone:
  // it still includes one buffer on a feed-through path (pessimistic by a
  // buffer delay only when such a path is the region's critical one).
  // The QN twin swap changes a cell's area without changing the count, and a
  // dropped root inverter is a cell the ABC network still holds: both make the
  // minted netlist the only truthful source too.
  if (bypassed_bufs != 0 || qn_dropped != 0 || qn_swapped != 0 || static_cast<int>(gates.size()) != qor_.back().gates) {
    qor_.back().bypassed = bypassed_bufs;
    qor_.back().gates    = static_cast<int>(gates.size());
    qor_.back().area     = emitted_area;
  }
  trace_stage("readback-gates");

  // pass 1c (seq): each ABC latch -> a native LGraph Flop. Flops are never
  // mapped to library DFFs (locked design decision): the latch only carried the
  // register across ABC so it could optimize/retime the surrounding logic. The
  // latch output net (Q) is mapped into net2drv so the comb fanins/outputs read
  // the flop's Q; the latch input net (D) is recorded and wired in pass 2b (its
  // driving gate is created in pass 2). Reassembly: when the latch count is
  // preserved (the default flow does not retime) each source register is
  // rebuilt as ONE multi-bit flop with its ORIGINAL name — including any bit
  // the DFF-cell path must keep native for a resetless power-on init. A
  // retime-reshaped count falls back to a single-root collapse (one register
  // name in the region) or per-latch deterministically-named 1-bit flops.
  struct Recon_flop {
    hhds::Node_class        node;
    int                     bits = 0;
    std::vector<Abc_Obj_t*> dnet;  // per-bit latch data-in net (wired in pass 2b)
  };
  std::vector<Recon_flop> recon;
  // register=true DFF-cell mapping: one library DFF Sub per surviving latch (its
  // din is wired in pass 2b, like a native flop's). `dff_` is set only when the
  // Liberty had a plain posedge D-flop; otherwise the native path below runs.
  struct Recon_dff {
    hhds::Node_class         sub;
    Abc_Obj_t*               dnet;
    const liberty::Dff_cell* cell;   // the ladder rung this Sub instantiates (pass 2b wires its d_pin)
    bool                     d_inv;  // QN cell whose D-cone root could not absorb the inversion: add an inverter on D
  };
  std::vector<Recon_dff> dff_recon;
  int                    clock_inv_cells = 0;
  bool                   init_dropped    = false;  // a concrete power-on init lost to a plain DFF cell
  if (opts_.map_register && !flops.empty()) {
    // src external driver -> body driver pin (region input port, or recreated const)
    absl::flat_hash_map<hhds::Pin_class, std::string> src_in_to_name;
    for (const auto& port : rb.inputs) {
      src_in_to_name[port.src_driver] = port.name;
    }
    auto body_pin_for_src = [&](const hhds::Pin_class& d) -> hhds::Pin_class {
      if (d.is_invalid()) {
        return {};
      }
      if (auto it = src_in_to_name.find(d); it != src_in_to_name.end()) {
        return body->get_input_pin(it->second);
      }
      if (d.is_const()) {
        return gu::create_const(*body, gu::const_of(d));
      }
      return {};
    };
    auto region_clk = body_pin_for_src(flops.front().clk_drv);

    // surviving latches (`lat`, filled before pass 1b) in stable vBoxes order
    int m = static_cast<int>(lat.size());

    auto owner_clk = [&](int k) -> hhds::Pin_class {
      return k < static_cast<int>(latch_owner.size()) ? body_pin_for_src(latch_owner[k]->clk_drv) : region_clk;
    };
    auto owner_negedge
        = [&](int k) { return k < static_cast<int>(latch_owner.size()) ? latch_owner[k]->neg_clock : flops.front().neg_clock; };
    absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> inverted_clocks;
    auto                                                  mapped_owner_clk = [&](int k) -> hhds::Pin_class {
      auto clk = owner_clk(k);
      if (clk.is_invalid() || !owner_negedge(k)) {
        return clk;
      }
      if (auto it = inverted_clocks.find(clk); it != inverted_clocks.end()) {
        return it->second;
      }
      // The selected Liberty cell is posedge-only. Share one physical clock
      // inverter across the negative-edge register bits on this clock.
      I(mio_inv != nullptr);
      auto& desc = cell_desc(mio_inv);
      auto  inv  = gu::create_typed_node(*body, Ntype_op::Sub);
      inv.set_subnode(desc.io);
      inv.attr(hhds::attrs::name).set(std::format("abc_clock_inv_{}", clock_inv_cells));
      clk.connect_sink(inv.create_sink_pin(desc.input_names.front()));
      auto inverted = inv.create_driver_pin(desc.output_name);
      gu::set_ubits(inverted, 1);
      inverted_clocks.emplace(clk, inverted);
      ++clock_inv_cells;
      return inverted;
    };

    // Original-name reconstruction: with the latch count preserved, latches
    // [start, start+bits) are flops[i]'s bits in crossing order, so a native
    // read-back can rebuild each source register as ONE multi-bit flop under
    // its ORIGINAL (hierarchical) name. That keeps the flop-name
    // correspondence across synthesis — the LEC collapses same-name state
    // pairs instead of solving thousands of anonymous 1-bit registers, which
    // is load-bearing for the whole-design flatten (one region holds EVERY
    // register of the design).
    struct Span {
      const Seq_flop* f;
      int             start;
    };
    std::vector<Span> spans;
    if (m == crossed_bits) {
      int s = 0;
      for (const auto& f : flops) {
        spans.push_back({&f, s});
        s += f.bits;
      }
    }
    // Nothing enforces wire-name uniqueness across a region's registers; two
    // same-named rebuilt flops would cgen as two `reg` declarations of one name.
    //
    // BUT THE SUFFIX IS A LAST RESORT, NOT A FIX. It renames the IMPL side only,
    // so a duplicate that the reference still spells one way becomes an
    // asymmetric pair: the post-synthesis LEC corresponds state BY NAME, and
    // every name-keyed map between here and cgen has to pick one of the two.
    // Measured (bedrock br_fifo_shared_*): a genvar-loop
    // instantiation whose instances shared a name flattened to two registers
    // under one name, and the mapped netlist then computed
    // `{credit_initial[5:3], credit_initial[5:3]}` where the RTL gives
    // `credit_initial` — 56 of 64 values wrong, confirmed by an independent
    // iverilog simulation against the PDK's own cell models. So say it out loud
    // rather than quietly renaming: a collision here means something upstream
    // minted two distinct registers under one hierarchical name.
    absl::flat_hash_map<std::string, int> name_used;
    int                                   dup_names = 0;
    std::string                           dup_first;
    auto                                  unique_flop_name = [&](const std::string& base) {
      int& n = name_used[base];
      ++n;
      if (n > 1) {
        if (dup_names++ == 0) {
          dup_first = base;
        }
        return std::format("{}__dup{}", base, n - 1);
      }
      return base;
    };
    // Rebuild one native flop covering the latches `idx` (bit 0 first): Q bits
    // feed the mapped logic via net2drv, din is Concat-reassembled in pass
    // 2b, and a POWER-ON init is recovered from the transformed latch values
    // only where the source promised one. A synchronous reset is
    // already in the D cone, so the rebuilt flop carries neither a reset_pin
    // nor the reset value as an init -- the plain `always @(posedge)` with the
    // mux on D is the same machine (proven by cvc5/lgyosys in lhd_abc_seq_test).
    auto build_native_flop = [&](const std::string& name, const hhds::Pin_class& clk, const std::vector<int>& idx) {
      int  k = static_cast<int>(idx.size());
      auto F = gu::create_typed_node(*body, Ntype_op::Flop);
      F.attr(hhds::attrs::name).set(name);
      auto Fq = F.create_driver_pin(0);
      gu::set_bits(Fq, k);
      gu::set_unsign(Fq);
      if (!clk.is_invalid()) {
        clk.connect_sink(gu::setup_sink_by_name(F, "clock_pin"));
      }
      if (!idx.empty() && owner_negedge(idx.front())) {
        gu::create_const(*body, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(F, "posclk"));
      }
      // Build MSB->LSB so widths past 64 bits stay exact. Uninitialized bits
      // remain unknown even when grouped with initialized bits in one register.
      bool        any_init = false;
      std::string init_bits(k, '?');  // index 0 = MSB (bit k-1)
      for (int b = 0; b < k; ++b) {
        if (auto v = source_init_bit(idx[b]); v.has_value()) {
          any_init             = true;
          init_bits[k - 1 - b] = *v ? '1' : '0';
        }
      }
      if (any_init) {
        gu::create_const(*body, *Dlop::from_binary(init_bits, /*unsigned_result=*/true))
            .connect_sink(gu::setup_sink_by_name(F, "initial"));
      }
      Recon_flop rf;
      rf.node = F;
      rf.bits = k;
      for (int b = 0; b < k; ++b) {
        auto*           L    = lat[idx[b]];
        auto*           qnet = Abc_ObjFanout0(Abc_ObjFanout0(L));  // latch -> BO -> Q net
        auto*           dnet = Abc_ObjFanin0(Abc_ObjFanin0(L));    // latch <- BI <- D net
        hhds::Pin_class qd;
        if (k == 1) {
          qd = Fq;
        } else {
          qd = extract_body_bit(Fq, b);
        }
        set_net_driver(qnet, qd);
        rf.dnet.push_back(dnet);
      }
      recon.push_back(std::move(rf));
    };

    if (dff_.has_value()) {
      // --- register=true: map each surviving latch to a library DFF-cell Sub ---
      // (D/CLK/Q). Q feeds the comb read-back via net2drv; D is wired in pass 2b
      // from the latch's data-in net; CLK comes straight from the region clock
      // (never through the AIG). A plain posedge D-flop cell has NO init pin, so a
      // latch carrying a concrete power-on init CANNOT be represented by the cell
      // without changing power-on behavior — such a bit stays a native flop (the
      // netlist stays equivalent); init-less and synchronous-reset bits (their
      // init is the reset value on D) become DFF cells under the register name.
      // The AIG-side QN encoding (Seq_flop::d_inverted) is exact only while
      // latch k is still the latch flop-bit k crossed as: with the count
      // reshaped there is no telling which BI nets carry ~f, and reading a QN
      // pin as Q on the wrong one is a silent miscompile. Unreachable (only the
      // built-in flow sets d_inverted, and it never retimes); a guard, not a
      // path -- every other flow takes the read-back absorption.
      if (spans.empty() && std::any_of(flops.begin(), flops.end(), [](const Seq_flop& f) { return f.d_inverted; })) {
        livehd::diag::err("pass.abc", "abc-readback", "internal")
            .msg(
                "pass.abc region '{}': the latch set was reshaped ({} latches for {} register bits) under the QN-only DFF "
                "cell '{}' -- the D-side inversion cannot be attributed; the built-in flow never retimes",
                rb.module_name,
                m,
                crossed_bits,
                dff_->name)
            .fatal();
        Abc_NtkDelete(mapped);
        return;
      }
      // One IO decl per ladder rung actually used (create_dff_io is find-or-
      // create, so an unused rung leaves no stray decl in the output library).
      std::vector<std::shared_ptr<hhds::GraphIO>> rung_io(dff_ladder_.size());
      auto                                        rung_for_fanout = [&](int fanout) -> size_t {
        // <=8 loads: x1 (the fastest rung there per the NLDM tables, and 97%
        // of registers); <=16: x2; above: x3 -- clamped to the ladder the
        // library has (test.lib / sky130 have one rung; ASAP7 three).
        const size_t want = fanout <= 8 ? 0 : (fanout <= 16 ? 1 : 2);
        return std::min(want, dff_ladder_.size() - 1);
      };
      auto rung_io_for = [&](size_t r) {
        if (!rung_io[r]) {
          rung_io[r] = liberty::create_dff_io(*outlib_, dff_ladder_[r]);
        }
        return rung_io[r];
      };
      // `owner` names the SOURCE register bit this latch came from (empty when
      // the latch count was reshaped and no correspondence survives). A mapped
      // DFF cell otherwise lands as `g<abcId>_<cell>`, which drops the register
      // name that the post-synthesis LEC's tier-1 state correspondence pairs on
      // — `id_q` then has no counterpart in the netlist and the def can only come
      // back inconclusive (every //bench:*_synth_lec_* target).
      // Scalar-replacement provenance for a bit-blasted register. `pass.semdiff`
      // reassembles the group from these and pairs it with the ref's single wide
      // flop; without them a mapped `q_0..q_7` faces a ref `q` that tier-1 cannot
      // match one-to-many, the whole register lands in `tier-2 unpaired state`,
      // and the flop-cut inductive miter then cuts only the flops that DID pair
      // and returns PROVEN off an obligation set covering a fraction of the
      // state (br_arb_weighted_rr: 18 cuts against 17 ref / 96 impl flops left
      // unpaired, PROVEN while lgyosys held a real 7-cycle counterexample).
      // Every attribute below is required by semdiff's valid_aggregate_group;
      // a register whose bits do not ALL become cells simply fails its ordinal
      // cover there and stays unpaired, exactly as before.
      struct Blast_lane {
        const std::string* root   = nullptr;
        int32_t            lane   = 0;
        int32_t            extent = 0;
        int32_t            source = 0;
      };
      auto map_dff_cell = [&](int k, const std::string& owner = {}, const Blast_lane* lane = nullptr) {
        auto*       L    = lat[k];
        auto*       qnet = Abc_ObjFanout0(Abc_ObjFanout0(L));  // latch -> BO -> Q net
        auto*       dnet = Abc_ObjFanin0(Abc_ObjFanin0(L));    // latch <- BI <- D net
        const auto  r    = rung_for_fanout(Abc_ObjFanoutNum(qnet));
        const auto& cell = dff_ladder_[r];
        auto        sub  = gu::create_typed_node(*body, Ntype_op::Sub);
        sub.set_subnode(rung_io_for(r));
        sub.attr(hhds::attrs::name).set(owner.empty() ? std::format("g{}_{}", Abc_ObjId(L), cell.name) : unique_flop_name(owner));
        // The cell's output pin IS the register's Q for every consumer -- for
        // a QN cell because the D side carries ~f: crossed that way
        // (Seq_flop::d_inverted), absorbed in pass 1b, or an inverter added in
        // pass 2b (`d_inv`).
        auto q = sub.create_driver_pin(cell.q_pin);
        gu::set_bits(q, 1);
        gu::set_unsign(q);
        set_net_driver(qnet, q);
        if (auto lclk = mapped_owner_clk(k); !lclk.is_invalid()) {
          lclk.connect_sink(sub.create_sink_pin(cell.clk_pin));
        }
        if (lane != nullptr) {
          sub.attr(livehd::attrs::aggregate_origin).set(*lane->root);
          sub.attr(livehd::attrs::aggregate_extent).set(lane->extent);
          sub.attr(livehd::attrs::aggregate_lane_ordinal).set(lane->lane);
          sub.attr(livehd::attrs::aggregate_source_index).set(lane->source);
          // One bit per lane, packed low-to-high, so the reassembled ranges are
          // [b, b+1) and cover [0, bits) with no gap.
          sub.attr(livehd::attrs::aggregate_bit_offset).set(lane->lane);
          sub.attr(livehd::attrs::aggregate_bit_width).set(1);
        }
        const bool crossed_inverted = k < static_cast<int>(latch_owner.size()) && latch_owner[k]->d_inverted;
        dff_recon.push_back({sub, dnet, &cell, cell.q_inverted && !crossed_inverted && !qn_absorbed.contains(dnet)});
      };
      auto native_single = [&](int k) {
        init_dropped = true;
        build_native_flop(unique_flop_name(std::format("{}__rinit{}", rb.module_name, Abc_ObjId(lat[k]))), owner_clk(k), {k});
      };
      // Distinguishes two registers that were blasted in the same region; the
      // matcher only requires every lane of ONE group to agree on it.
      int32_t blast_source_index = 0;
      if (!spans.empty()) {
        for (const auto& sp : spans) {
          ++blast_source_index;
          bool native = false;
          bool cell   = false;
          for (int b = 0; b < sp.f->bits; ++b) {
            (needs_native(sp.start + b) ? native : cell) = true;
          }
          if (native && !cell) {
            // the whole register keeps its power-on init: rebuild it as one
            // native flop under its original name
            init_dropped = true;
            std::vector<int> idx(sp.f->bits);
            std::iota(idx.begin(), idx.end(), sp.start);
            build_native_flop(unique_flop_name(sp.f->root), body_pin_for_src(sp.f->clk_drv), idx);
          } else {
            // init-less or sync-reset register -> per-bit DFF cells; a mixed
            // register (should not occur: init is stamped per register)
            // degrades to per-bit handling, never to a dropped init
            for (int b = 0; b < sp.f->bits; ++b) {
              int k = sp.start + b;
              // Per-bit name under the source register: a 1-bit register keeps
              // its plain name, a wider one indexes (`id_q[0]`) — the spelling a
              // hand-flattened design uses, which canon_flop_name already folds.
              if (needs_native(k)) {
                native_single(k);
              } else if (sp.f->bits == 1) {
                map_dff_cell(k, sp.f->root);  // pairs by name; nothing was blasted
              } else {
                const Blast_lane lane{&sp.f->root, b, sp.f->bits, blast_source_index};
                map_dff_cell(k, std::format("{}_{}", sp.f->root, b), &lane);
              }
            }
          }
        }
      } else {
        // retime-reshaped latch count: no per-register correspondence survives
        for (int k = 0; k < m; ++k) {
          needs_native(k) ? native_single(k) : map_dff_cell(k);
        }
      }
    } else {
      if (!spans.empty()) {
        // one flop per source register under its ORIGINAL name — multi-root
        // regions (the whole-design flatten) keep the name correspondence
        for (const auto& sp : spans) {
          std::vector<int> idx(sp.f->bits);
          std::iota(idx.begin(), idx.end(), sp.start);
          build_native_flop(unique_flop_name(sp.f->root), body_pin_for_src(sp.f->clk_drv), idx);
        }
      } else {
        absl::flat_hash_set<std::string> roots;
        for (const auto& f : flops) {
          roots.insert(f.root);
        }
        if (roots.size() == 1) {
          // retime-reshaped single-root region (one register name): collapse
          // every surviving latch into one named flop
          std::vector<int> idx(m);
          std::iota(idx.begin(), idx.end(), 0);
          build_native_flop(unique_flop_name(flops.front().root), region_clk, idx);
        } else {
          // retime-reshaped multi-register region: per-latch 1-bit flops --
          // always LEC-correct regardless of how retiming reshaped or reordered
          // the latches (each latch is faithfully its own 1-bit register; no
          // cross-register order assumption), clocked from its OWN source flop
          for (int k = 0; k < m; ++k) {
            build_native_flop(unique_flop_name(std::format("{}__r{}", rb.module_name, k)), owner_clk(k), {k});
          }
        }
      }
    }  // else (native-flop read-back)
    if (dup_names != 0) {
      livehd::diag::warn("pass.abc", "duplicate-register-name", "internal")
          .msg(
              "pass.abc region '{}': {} register(s) share a name with another register in the same region (first: '{}'); "
              "the read-back had to suffix them '__dup<N>' on the IMPLEMENTATION side only. Two distinct registers under "
              "one hierarchical name is an upstream naming defect: it breaks the post-synthesis LEC's name-based state "
              "correspondence and can alias the registers outright",
              rb.module_name,
              dup_names,
              dup_first)
          .emit();
    }
  }
  if (init_dropped) {
    livehd::diag::warn("pass.abc", "dff-init-kept-native", "unsupported")
        .msg(
            "pass.abc region '{}': register(s) carry a power-on init value that the plain DFF cell '{}' has no pin for; "
            "they were kept as native flops (still correct) while init-less registers mapped to DFF cells",
            rb.module_name,
            dff_->name)
        .emit();
  }
  trace_stage("readback-cells");

  // pass 2: wire each Sub's fanins (fanin k <-> Liberty pin k)
  auto const0_pin = [&]() { return gu::create_const(*body, *Dlop::create_integer(0)); };
  for (auto& [sub, obj] : gates) {
    auto*       g    = static_cast<Mio_Gate_t*>(obj->pData);
    const auto& pins = cell_desc(g).input_names;
    int         k    = 0;
    Abc_Obj_t*  fin  = nullptr;
    Abc_ObjForEachFanin(obj, fin, k) {
      if (k >= static_cast<int>(pins.size())) {
        break;
      }
      auto spin   = sub.create_sink_pin(pins[k]);
      // No set_bits on this cell-input SINK: `bits` is a driver-pin property (the
      // 1-bit width lives on the gate's GraphIO port + the 1-bit driver net).
      auto driver = get_net_driver(fin);
      if (!driver.is_invalid()) {
        driver.connect_sink(spin);
      } else {
        const0_pin().connect_sink(spin);  // structurally complete; should not occur
      }
    }
  }

  // Reassemble a vector of LSB-first one-bit drivers as one canonical Concat
  // (whose lanes are MSB-first). A Set_mask chain creates W full-width
  // intermediate values; downstream Verilog tools then expand W*W bits.
  hhds::Pin_class concat_width_one;
  auto            assemble_bits = [&](std::vector<hhds::Pin_class>& dbit, bool sign, hhds::SourceId sid = hhds::SourceId_invalid) {
    I(!dbit.empty());
    const auto      w = static_cast<int>(dbit.size());
    hhds::Pin_class out;
    if (w == 1) {
      out = dbit.front();
    } else {
      if (concat_width_one.is_invalid()) {
        concat_width_one = gu::create_const(*body, *Dlop::create_integer(1));
      }
      auto concat = gu::create_typed_node(*body, Ntype_op::Concat);
      if (sid != hhds::SourceId_invalid) {
        concat.attr(hhds::attrs::srcid).set(sid);
      }
      // Port IDs still encode MSB-first lanes, but create them in descending
      // order. HHDS's per-node pin list is sorted; ascending creation rescans
      // the growing list for every pin and makes a W-bit Concat O(W^2).
      for (size_t b = 0; b < dbit.size(); ++b) {
        auto data_pid = static_cast<hhds::Port_id>(2 * (dbit.size() - 1 - b));
        concat_width_one.connect_sink(concat.create_sink_pin(data_pid + 1));
        dbit[b].connect_sink(concat.create_sink_pin(data_pid));
      }
      out = concat.create_driver_pin(0);
      gu::set_bits(out, w);
      // A Concat driver is UNSIGNED by construction and every consumer relies
      // on it: each lane masks into its own window, so the value is in
      // [0, 2^sum(w)).
      gu::set_unsign(out);
    }
    if (!sign) {
      return out;
    }
    // Preserve the operand's signedness on the reassembled value. For a Div
    // the LEC fit()s each operand by its sign (SDIV/UDIV sign-extend vs
    // zero-extend), so a signed operand narrower than the divider's width must
    // stay signed or ref/impl diverge.
    auto sx = gu::create_typed_node(*body, Ntype_op::Sext);
    if (sid != hhds::SourceId_invalid) {
      sx.attr(hhds::attrs::srcid).set(sid);
    }
    out.connect_sink(gu::setup_sink_by_name(sx, "a"));
    gu::create_const(*body, *Dlop::create_integer(w)).connect_sink(gu::setup_sink_by_name(sx, "b"));
    auto sout = sx.create_driver_pin(0);
    gu::set_bits(sout, w);
    gu::set_sign(sout);
    return sout;
  };

  // pass 2b (seq): wire each reconstructed flop's din from the body driver that
  // feeds its latch D net (now resolvable: PIs in 1a, gates in 1b/2).
  for (auto& rf : recon) {
    int                          k = rf.bits;
    std::vector<hhds::Pin_class> dbits(k);
    for (int b = 0; b < k; ++b) {
      auto driver = get_net_driver(rf.dnet[b]);
      dbits[b]    = !driver.is_invalid() ? driver : const0_pin();
    }
    assemble_bits(dbits, false).connect_sink(gu::setup_sink_by_name(rf.node, "din"));
  }

  // pass 2b (register=true): wire each mapped DFF Sub's D pin from its latch's
  // data-in net (each DFF cell is 1-bit, so no Set_mask reassembly is needed).
  // A QN cell whose D-cone root could not absorb the inversion (pass 1b: the
  // root feeds other logic too, is a port / another register's Q, or has no
  // inverting twin) gets one min-size inverter here, on the D side: fanout 1,
  // so INVx1 always suffices, and the register's own drive ladder carries the
  // Q fanout. Still 0.2916 + 0.0437 against DFFHQx4's 0.3645.
  int    qn_inv_cells = 0;
  double qn_inv_area  = 0;
  for (auto& rd : dff_recon) {
    auto d = get_net_driver(rd.dnet);
    if (d.is_invalid()) {
      d = const0_pin();
    }
    if (rd.d_inv) {
      I(mio_inv != nullptr);  // start() refuses a Liberty without an inverter
      auto& desc = cell_desc(mio_inv);
      auto  inv  = gu::create_typed_node(*body, Ntype_op::Sub);
      inv.set_subnode(desc.io);
      inv.attr(hhds::attrs::name).set(std::format("{}__dinv", std::string{rd.sub.attr(hhds::attrs::name).get_or("")}));
      d.connect_sink(inv.create_sink_pin(desc.input_names.front()));
      d = inv.create_driver_pin(desc.output_name);
      gu::set_bits(d, 1);
      gu::set_unsign(d);
      ++qn_inv_cells;
      qn_inv_area += inv_area;
    }
    d.connect_sink(rd.sub.create_sink_pin(rd.cell->d_pin));
  }
  // Those inverters are minted standard cells the mapped LOGIC network never
  // saw: count them where the identity-buffer bypass corrected the same row,
  // so abc.json `gates`/`area` (what lhdtrack scores as lhd_area, and what the
  // incremental cache persists) describe the netlist that was actually written.
  if (qn_inv_cells != 0 || clock_inv_cells != 0) {
    qor_.back().gates += qn_inv_cells + clock_inv_cells;
    qor_.back().area  += qn_inv_area + clock_inv_cells * inv_area;
  }
  trace_stage("readback-fanins");

  // pass 3: POs -> reassemble multi-bit outputs (one Concat). Match by
  // creation order (po_order), consistent with the PI readback.
  std::vector<std::vector<hhds::Pin_class>> out_bits(rb.outputs.size());
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    int w = rb.outputs[po].bits == 0 ? 1 : rb.outputs[po].bits;
    out_bits[po].resize(w);
  }
  if (Abc_NtkPoNum(mapped) != static_cast<int>(po_order.size() + bbox_po.size() + (has_dummy_po ? 1 : 0))) {
    livehd::diag::warn("pass.abc", "abc-readback", "internal")
        .msg("pass.abc: region '{}': mapped PO count {} != created {} (region {} + bbox {}) — read-back misaligned",
             rb.module_name,
             Abc_NtkPoNum(mapped),
             po_order.size() + bbox_po.size(),
             po_order.size(),
             bbox_po.size())
        .emit();
  }
  Abc_NtkForEachPo(mapped, pObj, i) {
    if (has_dummy_po && i >= static_cast<int>(po_order.size() + bbox_po.size())) {
      continue;  // the all-native sentinel PO: no read-back target, and its net
                 // has no driver entry (a lookup here would warn and leak a const)
    }
    auto* net = Abc_ObjFanin0(pObj);
    auto  drv = get_net_driver(net);
    if (drv.is_invalid()) {
      livehd::diag::warn("pass.abc", "abc-readback", "internal")
          .msg("pass.abc: region '{}': PO {} ('{}') fanin net has no read-back driver — emitted const0",
               rb.module_name,
               i,
               Abc_ObjName(pObj))
          .emit();
    }
    if (drv.is_invalid()) {
      drv = const0_pin();
    }
    if (i < static_cast<int>(po_order.size())) {
      out_bits[po_order[i].first][po_order[i].second] = drv;
    } else if (int j = i - static_cast<int>(po_order.size()); j < static_cast<int>(bbox_po.size())) {
      for (const auto& target : bbox_po[static_cast<size_t>(j)]) {
        bbox_recon[target.bx].in_bit[target.input][target.bit] = drv;  // wired to the recon node sink below
      }
    }
  }

  // pass 3b: wire each rebuilt blackbox node's combinational inputs from the
  // captured PO drivers (multi-bit reassembled with one Concat).
  // The same source can feed more than one native boundary through different
  // explicit width windows (two Concat lanes are the common case).  Width and
  // signedness are part of the cast, so caching only by source can reconnect a
  // previously assembled 6-bit value to a later 5-bit lane and leave an
  // invalid over-wide Concat in the mapped graph.
  using Bbox_input_key = std::tuple<hhds::Pin_class, int, bool>;
  absl::flat_hash_map<Bbox_input_key, hhds::Pin_class> reassembled_bbox_input;
  for (size_t bx = 0; bx < bboxes.size(); ++bx) {
    auto& bb = bboxes[bx];
    auto& br = bbox_recon[bx];
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      int                  w    = bb.ins[ii].bits;
      auto                 sink = br.node.create_sink_pin(bb.ins[ii].port_id);
      auto&                dbit = br.in_bit[ii];
      const Bbox_input_key cache_key{bb.ins[ii].drv, w, bb.ins[ii].sign};
      if (auto it = reassembled_bbox_input.find(cache_key); it != reassembled_bbox_input.end()) {
        it->second.connect_sink(sink);
        continue;
      }
      for (int b = 0; b < w; ++b) {
        if (dbit[b].is_invalid()) {
          dbit[b] = const0_pin();
        }
      }
      if (w == 1 && !bb.ins[ii].sign) {
        dbit[0].connect_sink(sink);  // unsigned 1-bit: drive the sink directly
        reassembled_bbox_input.emplace(cache_key, dbit[0]);
        continue;
      }
      auto acc = assemble_bits(dbit, bb.ins[ii].sign);
      acc.connect_sink(sink);
      reassembled_bbox_input.emplace(cache_key, acc);
    }
  }

  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    if (direct_native_output[po]) {
      continue;
    }
    const auto& port = rb.outputs[po];
    int         w    = port.bits == 0 ? 1 : port.bits;
    auto        opin = body->get_output_pin(port.name);
    auto&       bits = out_bits[po];
    for (int b = 0; b < w; ++b) {
      if (bits[b].is_invalid()) {
        bits[b] = const0_pin();
      }
    }
    if (w == 1) {
      bits[0].connect_sink(opin);
      continue;
    }
    auto acc = assemble_bits(bits, port.sign, po_srcid[po]);
    acc.connect_sink(opin);
  }
  trace_stage("readback-outputs");

  // --- source-map carry-through: stamp each mapped gate with the srcid of the
  // original output cone it feeds. Walk output roots in ascending order with
  // ONE global visited set, so each mapped gate is attributed to the first
  // (lowest-index) output that reaches it. A shared gate therefore gets a stable
  // primary anchor rather than a combined source set. ABC's optimization is
  // lossy either way, and this keeps attribution linear in gates+edges instead
  // of re-walking the whole cone once per boundary output (minutes on Rob). ---
  {
    // roots = the mapped gate driving each PO bit, grouped by output port
    std::vector<std::vector<Abc_Obj_t*>> port_roots(rb.outputs.size());
    std::vector<hhds::SourceId>          cone_srcid(po_srcid);
    Abc_NtkForEachPo(mapped, pObj, i) {
      if (i >= static_cast<int>(po_order.size())) {
        continue;
      }
      auto* drv = Abc_ObjFanin0(Abc_ObjFanin0(pObj));  // PO -> net -> driving node
      if (drv != nullptr && Abc_ObjIsNode(drv)) {
        port_roots[po_order[i].first].push_back(drv);
      }
    }
    // Latch-input pseudo-outputs (seq): a gate feeding a register din gets the
    // ORIGINAL register's srcid, so a post-map critical path ending at a flop
    // still points at source. Latch k maps to its source flop by creation
    // order — valid only when the latch count survived the flow unchanged
    // (the same assumption the 1:1 flop read-back makes); a retime-reshaped
    // region keeps PO-cone attribution only.
    if (opts_.map_register && !flops.empty()) {
      int total_bits = 0;
      for (const auto& f : flops) {
        total_bits += f.bits;
      }
      std::vector<Abc_Obj_t*> lat_objs;
      Abc_NtkForEachLatch(mapped, pObj, i) { lat_objs.push_back(pObj); }
      if (static_cast<int>(lat_objs.size()) == total_bits) {
        std::vector<const Seq_flop*> owner;
        owner.reserve(static_cast<size_t>(total_bits));
        for (const auto& f : flops) {
          for (int b = 0; b < f.bits; ++b) {
            owner.push_back(&f);
          }
        }
        for (size_t k = 0; k < lat_objs.size(); ++k) {
          auto sid_attr = owner[k]->node.attr(hhds::attrs::srcid);
          if (!sid_attr.has() || sid_attr.get() == 0) {
            continue;
          }
          auto* bi_obj = Abc_ObjFanin0(lat_objs[k]);                           // latch -> BI
          auto* dnet   = bi_obj != nullptr ? Abc_ObjFanin0(bi_obj) : nullptr;  // BI -> net
          auto* drv    = dnet != nullptr ? Abc_ObjFanin0(dnet) : nullptr;      // net -> driving node
          if (drv == nullptr || !Abc_ObjIsNode(drv)) {
            continue;
          }
          port_roots.push_back({drv});
          // Into the library srcmap, not the body locator (see po_srcid above).
          cone_srcid.push_back(body->get_io()->get_library()->source_map().import_from(rb.src->source_locator(), sid_attr.get()));
        }
      }
    }
    // Per output, claim every not-yet-attributed gate in its fanin cone.
    std::vector<uint8_t> attributed(mapped_obj_slots);
    for (size_t po = 0; po < port_roots.size(); ++po) {
      if (cone_srcid[po] == hhds::SourceId_invalid) {
        continue;  // no provenance to attribute this cone with
      }
      std::vector<Abc_Obj_t*> stack = port_roots[po];
      while (!stack.empty()) {
        auto* g = stack.back();
        stack.pop_back();
        // `attributed` is both the global claim set and this walk's visited set:
        // a gate claimed by an earlier output already had its whole fanin cone
        // claimed by that same walk, so stopping here loses nothing.
        const auto gid = static_cast<size_t>(Abc_ObjId(g));
        if (gid >= attributed.size() || attributed[gid]) {
          continue;
        }
        attributed[gid] = 1;
        if (!mapped_node2sub[gid].is_invalid()) {
          mapped_node2sub[gid].attr(hhds::attrs::srcid).set(cone_srcid[po]);
        }
        Abc_Obj_t* fin = nullptr;
        int        k   = 0;
        Abc_ObjForEachFanin(g, fin, k) {
          auto* d = Abc_ObjFanin0(fin);  // fanin net -> its driving node
          if (d != nullptr && Abc_ObjIsNode(d)) {
            const auto did = static_cast<size_t>(Abc_ObjId(d));
            if (did >= attributed.size() || attributed[did]) {
              continue;
            }
            stack.push_back(d);
          }
        }
      }
    }
  }
  trace_stage("readback-srcmap");

  bypass_setmask_bit_reads(body);
  trace_stage("readback-packed-bits");

  trace_stage("readback-complete");

  // rb.body now holds the complete mapped netlist: snapshot it (and the pre-abc
  // body built above) into the cache so the next run's identical region is a
  // whole-module copy, not an ABC run. A region whose pre-body could not be
  // rebuilt (pre_g == nullptr) is uncacheable and simply re-maps next time.
  if (incr_ != nullptr && pre_g != nullptr) {
    incr_->store(rb, *rb.pre_lib, rb.pre_name, qor_.back(), recipe, outlib_, fact_keys);
  }
  qor_.back().cache             = "miss";
  qor_.back().ms                = since();
  const uint64_t process_peak   = cost::process_peak_rss_bytes();
  qor_.back().peak_rss_kb       = process_peak >> 10;
  // A process HWM is sticky across colors. Charge this color the new HWM only
  // when it advanced; otherwise use the largest live-RSS stage sample. This
  // avoids attributing an early large color's retained HWM to every later tiny
  // color while still capturing peaks at the end of ABC's blocking flow.
  const uint64_t color_peak     = process_peak > process_peak_before ? process_peak : sampled_peak_rss;
  qor_.back().color_peak_rss_kb = coordinator_ == nullptr && color_peak > rss_entry ? (color_peak - rss_entry) >> 10 : 0;
  Abc_NtkDelete(mapped);
  // &get/&dc4/&dch/&nf leave GIA managers in the global frame even after
  // &put.  A large region then poisons the next tiny job (Rob's 438-node
  // NewRobDeqPtrWrapper stalled for minutes after a 10k-bit pack, versus
  // 0.35 s in a fresh frame).  Clear all per-network/GIA workspace while
  // retaining the frame's parsed Liberty library and installed aliases.
  Abc_FrameDeleteAllNetworks(frame);
  if (opts_.time_budget_ms != 0 && qor_.back().ms > static_cast<double>(opts_.time_budget_ms)) {
    time_refusal_
        = std::format("region '{}' took {:.0f} ms in ABC (soft limit {} ms)", rb.module_name, qor_.back().ms, opts_.time_budget_ms);
  }
  // Publish the completed color before allocator housekeeping: a pressure scan
  // can itself take time, and the heartbeat should identify the finished work
  // immediately rather than making that pause look like part of ABC mapping.
  report_completion(qor_.back());
#if defined(__GLIBC__)
  // Hundreds of ROB colors showed the glibc heap RSS growing monotonically
  // even though the ABC networks above were deleted. Return completely free
  // heap pages at the color boundary so the next color's 16-GiB admission
  // check measures live state, not reusable pages retained by malloc arenas.
  // QoR's peak sample is intentionally taken before this trim.
  (void)malloc_trim(0);
#elif defined(__APPLE__)
  // Darwin's allocator has the same retained-page behavior, exposed more
  // directly by TASK_VM_INFO.phys_footprint. Do not scan every malloc zone
  // after each tiny color: thousands of needless maximal-relief calls fragment
  // Darwin's allocator address space and can make a later allocation fail even
  // while the physical footprint is safe. Once the process is genuinely under
  // pressure, periodically ask all zones to return a bounded amount of
  // reclaimable pages. The mapped HHDS body remains live and is untouched.
  // QoR's peak sample is intentionally taken first.
  //
  // Backend has more than 3,000 colors. Once it crossed the pressure threshold,
  // calling maximal all-zone relief after every color performed more than 2,000
  // complete zone scans and eventually made a later allocation fail while
  // physical footprint was still about 10 GiB below the process ceiling. Even
  // rate-limited maximal all-zone scans reproduced the failure at a different
  // color. The Darwin API documents a nonzero goal as best-effort bounded
  // release, so combine a 2-GiB goal with one all-zone scan per 64 completed
  // colors. A default-zone-only scan still let Backend's other zones cross the
  // physical ceiling at color 3,240. The nonzero goal bounds virtual-address
  // churn, and host_mem's Darwin VA allowance leaves room for the holes, while
  // retaining ample physical headroom between scans.
  const uint64_t     pressure_ceiling        = cost::configured_budget_bytes();
  constexpr uint64_t kPressureReliefInterval = 64;
  constexpr size_t   kPressureReliefGoal     = size_t{2} << 30;
  const bool relief_due = !pressure_relief_done_ || completed_regions_ - last_pressure_relief_region_ >= kPressureReliefInterval;
  if (pressure_ceiling != 0 && relief_due && cost::process_footprint_bytes() > pressure_ceiling - pressure_ceiling / 4) {
    (void)malloc_zone_pressure_relief(nullptr, kPressureReliefGoal);
    last_pressure_relief_region_ = completed_regions_;
    pressure_relief_done_        = true;
  }
#endif
}

void Mapper::report_completion(const Region_qor& q) {
  if (coordinator_) {
    // The run-level heartbeat is the coordinator's, but the Darwin pressure-scan
    // rate limiter in map_region reads THIS mapper's counter -- leaving it at 0
    // pinned `relief_due` false after the worker's very first color.
    ++completed_regions_;
    coordinator_->report_completion(q);
    return;
  }

  ++completed_regions_;
  const std::string_view cache = q.cache == nullptr || q.cache[0] == '\0' ? "none" : q.cache;
  std::string line = std::format("PROGRESS pass.abc completed={} region='{}' color={} resynth={} cache={} ge={} gates={} ms={:.1f}",
                                 completed_regions_,
                                 q.module,
                                 q.color,
                                 q.resynth ? 1 : 0,
                                 cache,
                                 q.input_ge,
                                 q.gates,
                                 q.ms);
  std::print("{}\n", line);  // captured in the pass's complete internal step log
  std::fflush(stdout);
  livehd::diag::sink().progress("pass.abc",
                                line,
                                {
                                    {"completed", std::to_string(completed_regions_)},
                                    {"region", q.module},
                                    {"color", std::to_string(q.color)},
                                    {"ctrl", q.ctrl ? "1" : "0"},
                                    {"resynth", q.resynth ? "1" : "0"},
                                    {"cache", std::string{cache}},
                                    {"input_nodes", std::to_string(q.input_nodes)},
                                    {"input_ge", std::to_string(q.input_ge)},
                                    {"gates", std::to_string(q.gates)},
                                    {"bypassed", std::to_string(q.bypassed)},
                                    {"area", std::format("{:.2f}", q.area)},
                                    {"delay", std::format("{:.2f}", q.delay)},
                                    {"ms", std::format("{:.1f}", q.ms)},
                                    {"color_peak_rss_kb", std::to_string(q.color_peak_rss_kb)},
                                    {"process_peak_rss_kb", std::to_string(q.peak_rss_kb)},
                                    {"critical_output", q.crit_output},
                                    {"critical_src", q.crit_src},
  });
}

void report_stats(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view top, const Map_options& opts) {
  std::print("pass.abc stats: top='{}' library='{}' register={} memory={}\n",
             top,
             opts.library,
             opts.map_register,
             memory_fold_name(opts.memory_fold));
  struct Op_stats {
    uint64_t nodes  = 0;
    uint64_t ge     = 0;
    uint64_t max_ge = 0;
  };
  struct Region_stats {
    uint64_t                               nodes         = 0;
    uint64_t                               ge            = 0;
    uint64_t                               register_bits = 0;
    absl::btree_map<std::string, uint64_t> op_ge;
  };
  absl::btree_map<std::string, Op_stats>                     by_op;
  absl::btree_map<std::pair<std::string, int>, Region_stats> by_region;
  uint64_t                                                   total_nodes = 0;
  uint64_t                                                   total_ge    = 0;
  for (const auto& graph : graphs) {
    for (const auto& node : graph->body().nodes()) {
      if (gu::is_builtin_node(node)) {
        continue;
      }
      const auto ge      = gu::synthesis_ge_weight(node);
      const auto op_name = std::string{Ntype::get_name(gu::type_op_of(node))};
      auto&      s       = by_op[op_name];
      ++s.nodes;
      s.ge     += ge;
      s.max_ge  = std::max(s.max_ge, ge);
      ++total_nodes;
      total_ge        += ge;
      // pass.partition treats an uncolored node as color zero, so the read-only
      // report must do the same. Newly extracted pattern definitions are
      // intentionally uncolored until the next color pass; omitting them here
      // hid exactly the shared body an optimization run needed to inspect.
      const int color  = gu::has_color(node) ? gu::color_of(node) : 0;
      auto&     rs     = by_region[{std::string{graph->get_name()}, color}];
      ++rs.nodes;
      rs.ge             += ge;
      rs.op_ge[op_name] += ge;
      if (gu::is_type_flop(node)) {
        rs.register_bits += static_cast<uint64_t>(std::max(gu::bits_of(node.create_driver_pin(0)), 1)) * pipeline_depth(node);
      }
    }
  }
  std::vector<std::pair<std::string_view, const Op_stats*>> ranked;
  ranked.reserve(by_op.size());
  for (const auto& [name, stats] : by_op) {
    ranked.emplace_back(name, &stats);
  }
  std::ranges::sort(ranked, [](const auto& lhs, const auto& rhs) { return lhs.second->ge > rhs.second->ge; });
  std::print("  operation GE: {} nodes, {} total synthesis GE across {} def(s)\n", total_nodes, total_ge, graphs.size());
  for (const auto& [name, stats] : ranked) {
    std::print("    {:<12} nodes {:>8}  GE {:>12}  max/node {:>10}\n", name, stats->nodes, stats->ge, stats->max_ge);
  }
  if (opts.verbose) {
    std::print("  regions (definition color: nodes, GE, register bits, leading operation GE):\n");
    for (const auto& [key, stats] : by_region) {
      std::vector<std::pair<std::string_view, uint64_t>> ops;
      ops.reserve(stats.op_ge.size());
      for (const auto& [name, ge] : stats.op_ge) {
        ops.emplace_back(name, ge);
      }
      std::ranges::sort(ops, [](const auto& lhs, const auto& rhs) {
        return lhs.second != rhs.second ? lhs.second > rhs.second : lhs.first < rhs.first;
      });
      std::string leaders;
      for (size_t i = 0; i < std::min<size_t>(ops.size(), 6); ++i) {
        leaders += std::format("{}{}={}", i == 0 ? "" : ",", ops[i].first, ops[i].second);
      }
      std::print("    {} c{}: nodes {}  GE {}  reg_bits {}  {}\n",
                 key.first,
                 key.second,
                 stats.nodes,
                 stats.ge,
                 stats.register_bits,
                 leaders);
    }
  }
  std::print("  (run with --emit-dir lg:DIR to produce the mapped netlist library)\n");
}

}  // namespace livehd::abc
