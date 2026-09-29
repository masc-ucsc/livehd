// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The ABC mapping backend (abc_backend.hpp). A region arrives translated
// (synth::Region_blast): its Lnet is replayed into a 1-bit AIG netlist,
// optimized and technology-mapped by ABC against a Liberty library, and handed
// back as a synth::Cell_netlist the region driver writes into the body.

#include "abc_backend.hpp"

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

#include "abc_flow.hpp"
#include "abc_lnet.hpp"
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

// Built-in flow, for combinational and sequential regions alike: latches only
// carry the registers across ABC so it can optimize the logic BETWEEN them.
// Retiming (`dretime`) is deliberately NOT in it (2opt-freq E ruling): moving
// registers reshapes the latch count/order, which (a) drops the register-
// preserving flop read-back to anonymous per-latch flops (breaking the tier-1
// name correspondence post-synthesis LEC relies on, 3a-synth), (b) loses the
// din-cone source attribution, and (c) is a latency-visible transform the
// 2opt-freq loop's cycle-accurate gate forbids. Opt in explicitly per run or
// per region when that is understood: `--set pass.abc.flow="strash; &get -n;
// &dc4; dretime; &dch -f; &nf {D}; &put"` (the read-back stays robust to
// reshaped latches).
//
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

// The remap in map_region assumes the built-in flow ENDS with the mapper step
// followed by kPutCmd, because `&undo` reverses exactly one GIA transformation.
static_assert(kCombFlow.ends_with("; &nf {D}; &put -o"));
static_assert(kCombFlow.ends_with(kPutCmd));
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

}  // namespace

// {D}/{L} expand to the full FLAG (`-D <val>` / `-L <val>`) when the option is
// set and to nothing otherwise — `&nf {D}` needs `&nf -D 4`, and a bare value
// (`&nf 4`) is silently ignored by ABC, which made the delay target a no-op.
namespace {
std::string flag_subst(std::string f, std::string_view tok, char flag, const std::string& val) {
  return subst(std::move(f), tok, val.empty() ? std::string{} : std::format("-{} {}", flag, val));
}

}  // namespace

// A built-in flow gains the buffering tail; a caller-supplied flow does not (it
// owns its own command list and may place `{F}` where it wants).
std::string Abc_backend::resolve_flow(std::string_view builtin) const {
  std::string f = std::string{builtin};
  if (region_.max_fanout != 0) {
    f += kBufferTail;
  }
  return f;
}

std::string Abc_backend::subst_flow(std::string f) const {
  f = flag_subst(std::move(f), "{D}", 'D', region_.delay);
  f = flag_subst(std::move(f), "{L}", 'L', region_.load);
  // {B} is the region budget, already spelled as a flag (`-D <ps>`) or empty.
  f = subst(std::move(f), "{B}", budget_flag_);
  // {F} is the bare fanout NUMBER (buffer's -N takes it), not a flag.
  return subst(std::move(f), "{F}", std::to_string(region_.max_fanout));
}

std::string Abc_backend::comb_flow() const {
  if (!region_.flow.empty()) {
    return subst_flow(region_.flow);
  }
  if (region_.delay.empty()) {
    // `none` disables the timed second candidate, not untimed synthesis.
    return region_.area_flow == "none" ? subst_flow(resolve_flow(kAreaFlow)) : area_flow();
  }
  return subst_flow(resolve_flow(kCombFlow));
}

std::string Abc_backend::area_flow() const {
  if (region_.area_flow == "none") {
    return {};
  }
  if (!region_.area_flow.empty()) {
    return subst_flow(region_.area_flow);  // caller-owned, like `flow`: no tail is appended
  }
  std::string f = std::string{kAreaFlow};
  if (region_.max_fanout != 0) {
    f += region_.delay.empty() ? kBufferTail : kAreaTail;
  }
  return subst_flow(std::move(f));
}

std::string Abc_backend::resolve_recipe(const synth::Region_ctx& ctx) const {
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
  // Liberty content is already in synth::Region_cache::make_salt, so the request is the
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
      "native-wiring=2|comb={}|adder={}|block={}|mult={}|nldm={}|arelax={}|genlib=unit|area={}|margin={}|"
      "objective=budget|barrel={}",
      comb_flow(),
      static_cast<int>(region_.adder),
      region_.block_size,
      static_cast<int>(region_.multiplier),
      ctx.timing_requested ? 1 : 0,
      region_.area_relax_pct,
      region_.area_flow == "none" ? std::string{"none"} : area_flow(),
      ctx.margin_ps,
      region_.reverse_barrel);
}

synth::Region_backend::Start Abc_backend::start(const synth::Region_ctx& ctx) {
  timing_requested_ = ctx.timing_requested;
  if (pabc_ != nullptr) {
    Abc_FrameEnter(static_cast<Abc_Frame_t*>(pabc_));
    return lib_loaded_ ? Start::resumed : Start::failed;
  }
  return start_session() ? Start::started : Start::failed;
}

bool Abc_backend::start_session() {
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
  auto cmd = std::string{"read_lib -s "} + base_.library;
  if (Cmd_CommandExecute(frame, cmd.c_str()) != 0) {
    livehd::diag::err("pass.abc", "read-lib", "unsupported")
        .msg("ABC could not read the Liberty library '{}'", base_.library)
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
          .msg("Liberty library '{}' has no {}: ABC cannot technology-map against it", base_.library, what)
          .hint(
              "pass a Liberty that contains both a buffer and an inverter; a vendor library split across views (ASAP7 keeps "
              "them in its *_INVBUF_* view and its flops in *_SEQ_*) has to be merged into the single file synth.liberty takes")
          .fatal();
      return false;
    }
    // The GENLIB as a Cell_library: the read-back's cell types, inverter and
    // inverting twins (the QN absorption), in Mio library order.
    build_cell_library(mio, cell_library_, gate_type_);
  }

  if (timing_requested_) {
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
                 base_.library);
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

  return true;
}

// Releases only THIS session: every parallel lane owns its own.
void Abc_backend::stop() {
  if (pabc_ != nullptr) {
    auto* frame = static_cast<Abc_Frame_t*>(pabc_);
    if (Abc_FrameReadGlobalFrame() == frame) {
      Abc_FrameLeave(nullptr);
    }
    Abc_FrameDestroy(frame);
    cell_library_ = {};  // its Mio gates die with the frame
    gate_type_.clear();
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

synth::Region_plan Abc_backend::plan(const synth::Region_ctx& ctx) {
  timing_requested_ = ctx.timing_requested;
  // The region's ABC options: the run level, the driver's resolved region
  // options, then this backend's own per-region choices.
  region_                                     = base_;
  static_cast<synth::Driver_options&>(region_) = ctx.options;
  // A ware trial re-maps a remembered region: its flow is the one recorded.
  tool_owned_flow_ = ctx.ware_trial ? ctx.flow.value_or(base_.flow).empty() : base_.flow.empty();
  // A coarse size tier is intentionally selected before color-keyed overrides:
  // a user naming one specific region always has the final say. `input_ge` is
  // invariant source-logic cost, unlike mapped gates, so cache recipes and
  // threshold decisions remain stable when the mapping flow changes.
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
  if (region_.flow.empty() && region_.large_ge && !region_.large_flow.empty() && ctx.input_ge >= region_.large_ge) {
    region_.flow = resolve_flow(region_.large_flow);
    if (region_.verbose) {
      std::print("[pass.abc] region '{}': large_flow selected ({} GE >= {})\n", ctx.rb.module_name, ctx.input_ge, region_.large_ge);
    }
  }
  if (ctx.flow) {
    region_.flow = *ctx.flow;
    if (!ctx.ware_trial && !ctx.flow->empty()) {
      tool_owned_flow_ = false;
    }
  }
  if (ctx.load) {
    region_.load = *ctx.load;
  }
  // The region's delay BUDGET, spelled into `{B}` before ANY flow string of
  // this region is resolved: the built-in tails' `dnsize`/`upsize` take it as
  // `-D`, and the recipe below therefore carries it verbatim. ABC's `-D` is an
  // integer (atoi), so the driver floors the budget to whole picoseconds and
  // the ladder compares against the same value it sized to.
  budget_flag_ = ctx.budget > 0.0f ? std::format("-D {}", static_cast<int>(ctx.budget)) : std::string{};
  // Is the ABC command list this region runs the BUILT-IN one (kCombFlow +
  // tail)? Stricter than `tool_owned_flow_`, which stays true under
  // a size-tier `large_flow` -- a user-authored command list that
  // may retime (`dretime`) or sequentially sweep (`scorr`/`lcorr`). The QN
  // AIG-side encoding (Seq_flop::d_inverted) is exact only under combinational
  // transformations, so it is gated on THIS flag, not on flow ownership.
  const bool custom_area = region_.flow.empty() && region_.delay.empty() && !region_.area_flow.empty() && region_.area_flow != "none";
  builtin_flow_          = region_.flow.empty() && !custom_area;
  if (custom_area) {
    tool_owned_flow_ = false;
  }

  synth::Region_plan plan;
  plan.preserves_latches = builtin_flow_;
  plan.recipe            = resolve_recipe(ctx);
  plan.recipe += ctx.rb.ctrl ? "|ctrl=1" : "|ctrl=0";
  // The frame's driving cell (resolve_boundary_defaults) makes `buffer` tree
  // every input's fanout out of band of the flow string: spell the decision.
  // From the OPTIONS, not the frame -- an all-hit run never starts ABC, and
  // the resolved cell is a function of the library (in the cache salt) and
  // `boundary_drive` alone.
  plan.recipe += "|pi_drive=";
  plan.recipe += (region_.boundary_buffer && region_.max_fanout != 0)
                     ? (region_.boundary_drive.empty() ? "auto" : region_.boundary_drive)
                     : "none";
  plan.recipe += region_.map_register ? "\n# livehd-register=abc" : "\n# livehd-register=native";
  return plan;
}

std::shared_ptr<void> Abc_backend::region_scope() {
  // ABC's global frame is restored, and the region's options and `{B}` are
  // dropped, whichever way the region exits.
  struct Scope {
    Abc_backend* backend;
    Abc_Frame_t* previous = Abc_FrameReadGlobalFrame();
    ~Scope() {
      Abc_FrameLeave(previous);
      backend->region_ = backend->base_;
      backend->budget_flag_.clear();
    }
  };
  return std::make_shared<Scope>(Scope{this});
}

void Abc_backend::end_region() {
  // &get/&dc4/&dch/&nf leave GIA managers in the global frame even after
  // &put.  A large region then poisons the next tiny job (Rob's 438-node
  // NewRobDeqPtrWrapper stalled for minutes after a 10k-bit pack, versus
  // 0.35 s in a fresh frame).  Clear all per-network/GIA workspace while
  // retaining the frame's parsed Liberty library and installed aliases.
  if (pabc_ != nullptr) {
    Abc_FrameDeleteAllNetworks(static_cast<Abc_Frame_t*>(pabc_));
  }
}

uint64_t Abc_backend::projected_memory(uint64_t aig_nodes) const {
  std::error_code ec;
  const auto      library_size = std::filesystem::file_size(base_.library, ec);
  return synth::projected_abc_memory(aig_nodes, ec ? 0 : library_size);
}

const synth::Cell_decl& Abc_backend::cell_desc_for(hhds::GraphLibrary& outlib, void* mio_gate) {
  return cell_library_.decl(outlib, gate_type_.at(mio_gate));
}

std::optional<synth::Cell_netlist> Abc_backend::map(const synth::Region_ctx& ctx, const synth::Region_blast& blast,
                                                    const synth::Region_rewrite& rewrite, synth::Region_qor& q) {
  using synth::Pi_kind;
  const auto& rb           = ctx.rb;
  const auto& region       = blast.region;
  const auto& pi_order     = blast.pi_order;
  const auto& all_pi_order = blast.all_pi_order;
  const auto& po_order     = blast.po_order;
  const bool  has_dummy_po = blast.has_dummy_po;
  const auto  rss_before   = blast.rss_before;
  const auto  blast_total  = blast.blast_total;
  const float budget       = ctx.budget;
  if (rewrite.map == synth::Region_rewrite::Map::refused) {
    return std::nullopt;  // the hook recorded the refusal; publish nothing
  }

  // The ABC objects are allocated here, not while blasting (the loop above
  // only estimated them), so this is where their footprint first shows.
  auto* manNtk = static_cast<Abc_Ntk_t*>(lnet_to_abc(blast.lnet, rb.module_name));
  if (ctx.fits && !ctx.fits(rss_before, blast_total, blast_total)) {
    Abc_NtkDelete(manNtk);
    return std::nullopt;  // emit no partial result; the refusal is recorded
  }
  Abc_NtkFinalizeRead(manNtk);
  if (!Abc_NtkCheck(manNtk)) {
    livehd::diag::err("pass.abc", "abc-check", "internal").msg("ABC netlist check failed for region '{}'", rb.module_name).fatal();
    Abc_NtkDelete(manNtk);
    return std::nullopt;
  }
  ctx.trace_stage("translated");

  // --- Phase A boundary environment (abc_boundary.hpp): what this region's
  // ports see beyond the partition, estimated from the SOURCE graph, handed to
  // ABC's SCL timer for every sizing/timing step of this region (the tail,
  // the budget ladder, the area candidate, the QoR timer). PI/PO indices are
  // the translation's creation order (`all_pi_order`, `po_order`), the same
  // order the read-back pairs by. Lives to the end of map_region; the table
  // uninstalls itself. Exact loads come later (refine_boundaries). ---
  Boundary_table boundary_table(static_cast<size_t>(Abc_NtkPiNum(manNtk)), static_cast<size_t>(Abc_NtkPoNum(manNtk)));
  int            boundary_bits = 0;
  if (region_.boundary && scl_lib_ok_) {
    std::vector<int> pi_port(all_pi_order.size(), -1);
    for (size_t i = 0; i < all_pi_order.size(); ++i) {
      if (all_pi_order[i].kind == Pi_kind::region_input) {
        pi_port[i] = static_cast<int>(pi_order[all_pi_order[i].index].first);
      }
    }
    boundary_bits = fill_static_boundary(boundary_table, rb, region, pi_port, po_order);
    boundary_table.install();
    if (region_.verbose) {
      std::print("[pass.abc] region '{}': boundary estimate on {} crossing port bit(s)\n", rb.module_name, boundary_bits);
    }
  }

  // Only private ABC objects are accessed in this interval: the driver may
  // release the shared graph lock until resume_graph().
  ctx.pause_graph();

  // --- run the flow: logic -> optimize -> map ---
  auto* frame  = static_cast<Abc_Frame_t*>(pabc_);
  auto* pLogic = Abc_NtkToLogic(manNtk);
  Abc_NtkDelete(manNtk);
  Abc_FrameClearVerifStatus(frame);
  auto flow = comb_flow();
  if (region_.verbose) {
    std::print("[pass.abc] region '{}': resolved flow: {}\n", rb.module_name, flow);
  }
  // Which mapping OBJECTIVE steps may run on this region. The budget ladder
  // (below) needs the built-in or a size-tier flow (`tool_owned_flow_`: a user
  // command list is run verbatim and never re-sized), a Liberty the SCL steps
  // can walk, and a delay target. The area CANDIDATE is stricter: it belongs to
  // the BUILT-IN objective only (a size tier is a deliberately cheap or
  // deliberately direct mapper -- re-running the area baseline would defeat
  // it), it is bounded by `large_ge` even when the large tier is off (a second map
  // on a 123k-node mem_lower tile would double the ABC time of the one region
  // that already dominates), it is switched off by `area_flow=none`, and it
  // skips the dummy-PO sentinel (nothing to compare on a region with no real
  // outputs).
  const bool        ladder_on = tool_owned_flow_ && scl_timing_ok_ && budget > 0.0f;
  const std::string area_cmd  = area_flow();
  const bool        candidate_on
      = ladder_on && builtin_flow_ && !area_cmd.empty() && !has_dummy_po && (region_.large_ge == 0 || ctx.input_ge <= region_.large_ge);
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
  if (region_.max_fanout != 0) {
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
            region_.max_fanout,
            base_.library);
      }
    }
  }
  // Can the area-recovery pass below replay JUST the mapper? `&undo` reverses one
  // GIA transformation, so the flow has to END with the built-in mapper step plus
  // kPutCmd and (when it is on) the buffering tail -- anything after that would
  // survive the undo and be applied twice. Derive it from the RESOLVED string
  // rather than from `tool_owned_flow_`: that flag is computed before the
  // size-tier `large_flow` substitution, so a tier flow is still
  // "tool owned" while being an arbitrary command list.
  const std::string map_step   = subst_flow(std::string{kMapCmd});
  const std::string flow_tail  = subst_flow(std::string{kBufferTail});
  const bool        tail_on    = !flow_tail.empty() && flow.ends_with(flow_tail);
  const std::string put_step   = std::string{"; "} + std::string{kPutCmd};
  const std::string remap_post = put_step + (tail_on ? flow_tail : "");
  const bool        remappable = tool_owned_flow_ && flow.ends_with(map_step + remap_post);
  Flow_plan         plan;
  plan.flow                 = flow;
  plan.size_to_budget       = std::format("upsize {0}; dnsize {0}", budget_flag_);
  plan.map_step             = map_step;
  plan.remap_post           = remap_post;
  plan.area_flow            = area_cmd;
  plan.ladder               = ladder_on;
  plan.remappable           = remappable;
  plan.area_candidate       = candidate_on;
  plan.budget               = budget;
  plan.area_relax_pct       = region_.area_relax_pct;
  const auto& flow_admission = ctx.admission;
  // A region hook's rewrite (pass/usyn) replaces the region's logic: in the
  // region's own PI/PO/latch skeleton, as the flow's input, or technology-
  // mapped only (`&nf`, no restructuring). A tmap result only gets the
  // fanout/sizing tail (buffering and gate sizing), like every built-in flow.
  bool rewrite_mapped = false;
  if (rewrite.map != synth::Region_rewrite::Map::region) {
    auto* logic = static_cast<Abc_Ntk_t*>(lnet_into_logic(rewrite.logic, Abc_FrameReadNtk(frame)));
    if (logic == nullptr) {
      livehd::diag::err("pass.abc", "invalid-rewrite", "internal")
          .msg("the region hook's logic for region '{}' does not match the region boundary", rb.module_name)
          .fatal();
      return std::nullopt;
    }
    Abc_FrameReplaceCurrentNetwork(frame, logic);
  }
  if (rewrite.map == synth::Region_rewrite::Map::tmap) {
    const auto nf = region_.delay.empty() ? std::string{"&nf"} : std::format("&nf -D {}", region_.delay);
    for (const auto* command : {"strash", "&get -n", nf.c_str(), "&put -o"}) {
      if (Cmd_CommandExecute(frame, command) != 0) {
        livehd::diag::err("pass.abc", "abc-tmap", "internal")
            .msg("ABC '{}' failed on the rewritten logic of region '{}'", command, rb.module_name)
            .fatal();
        return std::nullopt;
      }
    }
    auto* current  = Abc_FrameReadNtk(frame);
    rewrite_mapped = current != nullptr && Abc_NtkIsMappedLogic(current);
    if (!rewrite_mapped) {
      livehd::diag::err("pass.abc", "abc-tmap", "internal").msg("ABC left no mapped network for region '{}'", rb.module_name).fatal();
      return std::nullopt;
    }
    if (tail_on) {
      std::string_view rest = flow_tail;
      while (!rest.empty()) {
        const auto end     = rest.find(';');
        auto       command = rest.substr(0, end);
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
        while (!command.empty() && command.front() == ' ') {
          command.remove_prefix(1);
        }
        if (!command.empty() && Cmd_CommandExecute(frame, std::string{command}.c_str()) != 0) {
          livehd::diag::err("pass.abc", "abc-flow", "internal")
              .msg("ABC '{}' failed on the technology-mapped rewrite of region '{}'", command, rb.module_name)
              .fatal();
          return std::nullopt;
        }
      }
    }
  }
  Flow_result flow_result;
  if (!rewrite_mapped) {
    flow_result = execute_flow(frame, plan, flow_admission);
  }
  if (flow_result.status == Flow_status::refused) {
    if (flow_result.refusal == Flow_refusal::time) {
      ctx.refuse_time(flow_result.command);
    } else if (flow_result.refusal == Flow_refusal::memory) {
      ctx.refuse_memory(flow_result.command);
    }
    return std::nullopt;  // the caller reports the recorded time/memory refusal, without publication.
  }
  if (flow_result.status == Flow_status::failed) {
    livehd::diag::err("pass.abc", "abc-flow", "internal")
        .msg("ABC {} failed for region '{}': {}", flow_result.stage, rb.module_name, flow_result.command)
        .fatal();
    return std::nullopt;
  }
  const auto& candidate      = flow_result.candidate;
  const auto& delay_flow_qor = flow_result.delay_qor;
  const auto& area_flow_qor  = flow_result.area_qor;
  if (region_.verbose && !candidate.empty()) {
    std::print("[pass.abc] region '{}': budget {} ps: delay flow {:.1f} ps / {:.2f}, area flow {} -> kept {}\n",
               rb.module_name,
               budget,
               delay_flow_qor->first,
               delay_flow_qor->second,
               area_flow_qor ? std::format("{:.1f} ps / {:.2f}", area_flow_qor->first, area_flow_qor->second) : "untimed",
               candidate);
  }
  ctx.trace_stage("flow-complete");

  // Translation is sampled repeatedly above, but ABC's optimization/mapping
  // command can create several network forms between those samples. Check the
  // exact live footprint again at the color boundary. Calling with a completed
  // fraction suppresses extrapolation and reports the real post-flow RSS.
  if (ctx.fits && !ctx.fits(rss_before, blast_total, blast_total)) {
    return std::nullopt;  // stop() owns the current ABC network; the refusal is recorded
  }

  ctx.resume_graph();

  // --- QoR read-back (2opt-freq A): critical delay/area/gates from the Liberty
  // pin-to-pin data while the flow's result is still a mapped LOGIC network
  // (Abc_NtkDelayTrace requires one; the netlist conversion below is only for
  // the gate read-back). Per-region numbers: paths crossing the region or
  // blackbox boundary are pass.opentimer's job, not scored here.
  {
    q.budget          = budget;
    q.candidate       = candidate;
    q.baseline_worker = flow_result.observation_json;
    q.boundary_bits   = boundary_bits;
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
        if (const auto phys = physical_flow_qor(pMappedLogic)) {
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
        ctx.critical_output(static_cast<size_t>(wpo), q);
      }
    }
  }

  auto* mapped = Abc_NtkToNetlist(Abc_FrameReadNtk(frame));
  if (mapped == nullptr || !Abc_NtkHasMapping(mapped)) {
    livehd::diag::err("pass.abc", "abc-unmapped", "internal")
        .msg("ABC produced no mapped netlist for region '{}' (check the Liberty library)", rb.module_name)
        .fatal();
    if (mapped != nullptr) {
      Abc_NtkDelete(mapped);
    }
    return std::nullopt;
  }
  ctx.trace_stage("netlist-ready");

  // --- the mapped netlist as cells; the driver writes them into the body ---
  auto cells = abc_to_cells(mapped, gate_type_, rb.module_name);
  Abc_NtkDelete(mapped);
  return cells;
}
}  // namespace livehd::abc

namespace livehd::abc {
Mapper::Mapper(const Map_options& opts) : synth::Region_driver(opts, std::make_unique<Abc_backend>(opts)) {}
}  // namespace livehd::abc
