// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Region body <-> ABC translation for pass.abc (task 2a-abc). Each colored
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
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <print>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
#include "rapidjson/document.h"

// clang-format off
// ABC headers must stay in dependency order: abc.h defines Abc_Frame_t (used by
// cmd.h/main.h) and the word/namespace macros. Do not sort.
extern "C" {
#include "base/abc/abc.h"       // brings abc_global.h (word, macros, ABC_NAMESPACE_*)
#include "base/main/abcapis.h"  // Abc_Frame_t
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "aig/hop/hop.h"
#include "map/mio/mio.h"
#include "misc/extra/extra.h"
}
// clang-format on

namespace gu = livehd::graph_util;

namespace livehd::abc {

namespace {

// Built-in combinational flow (task default). {D}/{L} substituted from opts.
constexpr std::string_view kCombFlow = "strash; &get -n; &dc4; &dch -f; &nf {D}; &put";

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
constexpr std::string_view kSeqFlow = "strash; &get -n; &dc4; &dch -f; &nf {D}; &put";

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

// One-hot mask value (only bit `b` set) for the flop-din Set_mask reassembly,
// valid for ANY bit position (`int64_t{1} << b` is UB for b >= 63, so it cannot
// build masks for buses wider than 64 bits). from_binary builds MSB->LSB, so bit
// b is a leading '1' followed by b zeros.
spool_ptr<Dlop> bit_mask(int b) {
  return Dlop::from_binary(std::string("1") + std::string(static_cast<size_t>(b), '0'), /*unsigned_result=*/true);
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

// {D}/{L} expand to the full FLAG (`-D <val>` / `-L <val>`) when the option is
// set and to nothing otherwise — `&nf {D}` needs `&nf -D 4`, and a bare value
// (`&nf 4`) is silently ignored by ABC, which made the delay target a no-op.
namespace {
std::string flag_subst(std::string f, std::string_view tok, char flag, const std::string& val) {
  return subst(std::move(f), tok, val.empty() ? std::string{} : std::format("-{} {}", flag, val));
}
}  // namespace

std::string Mapper::comb_flow() const {
  std::string f = opts_.flow.empty() ? std::string{kCombFlow} : opts_.flow;
  f             = flag_subst(std::move(f), "{D}", 'D', opts_.delay);
  f             = flag_subst(std::move(f), "{L}", 'L', opts_.load);
  return f;
}

std::string Mapper::seq_flow() const {
  std::string f = opts_.flow.empty() ? std::string{kSeqFlow} : opts_.flow;
  f             = flag_subst(std::move(f), "{D}", 'D', opts_.delay);
  f             = flag_subst(std::move(f), "{L}", 'L', opts_.load);
  return f;
}

std::string Mapper::resolve_recipe() const {
  // Verbatim, not a hash: a hash collision here would reuse a netlist mapped
  // under a different recipe. Both flow strings are pinned (map_region picks one
  // by mode, and the mode is in the salt); '|' separates fields that never
  // contain '|'.
  return std::format("comb={}|seq={}|adder={}|block={}|mult={}",
                     comb_flow(),
                     seq_flow(),
                     static_cast<int>(opts_.adder),
                     opts_.block_size,
                     static_cast<int>(opts_.multiplier));
}

bool Mapper::start() {
  if (pabc_ != nullptr) {
    return lib_loaded_;
  }
  Abc_Start();
  pabc_ = Abc_FrameGetGlobalFrame();
  if (pabc_ == nullptr) {
    livehd::diag::err("pass.abc", "abc-frame", "internal").msg("could not initialize the ABC frame").fatal();
    return false;
  }
  auto* frame = static_cast<Abc_Frame_t*>(pabc_);
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

  // Register mapping target: scan the Liberty for a plain posedge D-flop (ABC's
  // read_lib already dropped it, so this is a separate text scan). A missing DFF
  // cell is not fatal — the read-back keeps flops native (the same shape as
  // register=false) so the netlist stays correct, just not fully cell-mapped.
  if (startup_opts_.map_register) {
    dff_ = liberty::find_dff_cell(startup_opts_.library, startup_opts_.dff_cell);
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

void Mapper::stop() {
  if (pabc_ != nullptr) {
    Abc_Stop();
    pabc_ = nullptr;
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
    if (key == "flow" || key == "delay" || key == "load") {
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
    } else if (key == "multiplier") {
      if (!val.IsString()) {
        return bad("'multiplier' must be a string (array)");
      }
      auto m = arith::parse_mult_kind({val.GetString(), val.GetStringLength()});
      if (!m.has_value()) {
        return bad(std::format("unknown multiplier '{}' (use array)", val.GetString()));
      }
      ro.multiplier = m.value();
    } else if (key == "block_size") {
      if (!val.IsInt() || val.GetInt() < 0) {
        return bad("'block_size' must be a non-negative integer");
      }
      ro.block_size = val.GetInt();
    } else {
      return bad(std::format("unknown option '{}' (use flow|delay|load|adder|block_size|multiplier)", key));
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

void Mapper::apply_region_overrides(const livehd::partition::Region_body& rb) {
  auto apply = [&](const Region_opts& ro, std::string_view src) {
    if (ro.flow.has_value()) {
      opts_.flow = *ro.flow;
    }
    if (ro.delay.has_value()) {
      opts_.delay = *ro.delay;
    }
    if (ro.load.has_value()) {
      opts_.load = *ro.load;
    }
    if (ro.adder.has_value()) {
      opts_.adder = *ro.adder;
    }
    if (ro.block_size.has_value()) {
      opts_.block_size = *ro.block_size;
    }
    if (ro.multiplier.has_value()) {
      opts_.multiplier = *ro.multiplier;
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
}

namespace {
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
  auto a = onode.attr(hhds::attrs::srcid);
  if (!a.has() || a.get() == 0) {
    return;
  }
  auto span = rb.src->source_locator().resolve_span(a.get());
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
// and the ABC flow that follows costs multiples of the translated netlist again.
// kFlowPeakFactor is intentionally conservative-but-modest; the guard does not
// lean on it, because it re-checks on every sample and refuses the moment the
// real RSS crosses the budget regardless of any projection.
bool Mapper::over_budget(std::string_view region, uint64_t rss_before, size_t blasted, size_t total) {
  const uint64_t budget = cost::budget_bytes(opts_.memory_budget_mb);
  if (budget == 0 || blasted == 0) {
    return false;  // unknown host and no explicit budget: unenforceable, do not gate
  }
  // Sample phys_footprint, not resident_size: it is the metric macOS jetsam
  // charges (and it counts compressed/paged pages resident_size drops under
  // pressure), so it is the number that actually decides whether we get killed.
  // NB it is equally STICKY after free() -- freed pages linger until the
  // allocator returns them -- so this stays a conservative "total footprint vs
  // budget" gate, not a live-bytes count (that needs a malloc interposer).
  const uint64_t rss = cost::process_footprint_bytes();
  if (rss == 0) {
    return false;
  }

  // How much more than the translated netlist the ABC flow peaks at. MEASURED on
  // flattened single-region designs (probe at 5% vs peak RSS of the full run):
  // ALU 6.1x, RegisterFile 2.5x, ImmediateGenerator 29.6x, dino CPU 12.3x. It is
  // emphatically NOT a constant -- read_lib's fixed cost dominates small regions,
  // which is what makes ImmediateGenerator's 96 gates look 29x.
  constexpr double   kFlowPeakFactor     = 10.0;
  // Extrapolating from a 5% sample multiplies whatever it sees by ~20, and then
  // by the factor above: ~200x. RSS at that point moves in malloc-arena steps, so
  // a single arena faulting in would project GiBs out of noise and FATAL a region
  // that fits. Two guards keep the projection honest: only extrapolate a signal
  // far larger than an arena step, and only act on it when it clears the budget
  // by a wide margin. Anything in between is left to the exact test below.
  constexpr uint64_t kMinGrowthToProject = uint64_t{256} << 20;
  constexpr uint64_t kProjectionMargin   = 4;

  const uint64_t grown     = rss > rss_before ? rss - rss_before : 0;
  const double   fraction  = static_cast<double>(blasted) / static_cast<double>(total);
  const uint64_t projected = rss_before + static_cast<uint64_t>(static_cast<double>(grown) / fraction);
  const uint64_t peak      = rss_before + static_cast<uint64_t>(static_cast<double>(projected - rss_before) * kFlowPeakFactor);

  // The exact reading is the guarantee; the projection is only allowed to make a
  // hopeless region die sooner.
  const bool over_now       = rss > budget;
  const bool over_projected = grown >= kMinGrowthToProject && peak / kProjectionMargin > budget;
  if (!over_now && !over_projected) {
    return false;
  }

  const auto        mib         = [](uint64_t b) { return b >> 20; };
  // Say which budget this actually is: an explicit memory_budget_mb is taken
  // verbatim and no reserve is subtracted, so quoting a reserve there would
  // describe a derivation that never happened.
  const std::string budget_desc = opts_.memory_budget_mb > 0 ? std::format("budget {} MiB (pass.abc.memory_budget_mb)", mib(budget))
                                                             : std::format("budget {} MiB (physical {} MiB minus a {} MiB reserve)",
                                                                           mib(budget),
                                                                           mib(cost::physical_ram_bytes()),
                                                                           mib(cost::reserve_bytes()));
  refusal_                      = std::format(
      "region '{}' does not fit in memory: {} of {} node(s) translated ({:.0f}%), RSS {} MiB "
                           "(was {} MiB){}, {}",
      region,
      blasted,
      total,
      100.0 * fraction,
      mib(rss),
      mib(rss_before),
      over_now ? std::string{}
                                    : std::format(", projected {} MiB translated and ~{} MiB at the ABC mapping peak", mib(projected), mib(peak)),
      budget_desc);
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
    if (b.is_invalid() || a.is_invalid() || !gu::is_const_pin(b)) {
      continue;
    }
    const auto bc = gu::hydrate_const(b);
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
    auto          bc = gu::hydrate_const(gu::get_driver_of_sink_name(n, "b"));
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
}  // namespace

void Mapper::map_region(const livehd::partition::Region_body& rb) {
  // A refusal already happened: work() will make it fatal once the ABC frame is
  // torn down, so translating the remaining regions can only burn time and
  // overwrite the FIRST refusal -- the one that is the actual root cause.
  if (!refusal_.empty()) {
    return;
  }

  // Per-region wall time: the only way to tell a cache that hits a lot from a
  // cache that saves time. A hit on a 200ms region and a miss on a 200s one
  // count the same in hits/misses and nothing alike in the total.
  const auto t_start = std::chrono::steady_clock::now();
  const auto since
      = [&t_start] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count(); };
  const auto trace_stage = [&](std::string_view stage) {
    if (!opts_.verbose) {
      return;
    }
    std::print("[pass.abc] region '{}': stage {} at {:.0f} ms\n", rb.module_name, stage, since());
    std::fflush(stdout);
  };

  // Per-region options are temporary (every helper below reads opts_) and are
  // restored on every exit path.
  const Map_options saved_opts = opts_;
  struct Opts_restore {
    Map_options*       dst;
    const Map_options* src;
    ~Opts_restore() { *dst = *src; }
  } opts_restore{&opts_, &saved_opts};
  // Structural input size belongs in every QoR row, including cache hits. It
  // makes recipe/runtime changes explainable without relying on module names:
  // mapped gates are only known after ABC and can move with the very recipe
  // being compared, while these two values describe the invariant input cone.
  const uint64_t input_nodes = rb.nodes.size();
  uint64_t       input_ge    = 0;
  for (const auto& node : rb.nodes) {
    input_ge += gu::mappable_ge_weight(node);
  }

  uint64_t register_bits = 0;
  for (const auto& node : rb.nodes) {
    if (!gu::is_type_flop(node)) {
      continue;
    }
    const int bits  = gu::bits_of(node.create_driver_pin(0));
    register_bits  += static_cast<uint64_t>(std::max(bits, 1));
  }
  if (opts_.map_register && opts_.register_max_bits != 0 && register_bits > opts_.register_max_bits) {
    opts_.map_register = false;
    std::print("[pass.abc] region '{}': keeping {} register bits native (limit {})\n",
               rb.module_name,
               register_bits,
               opts_.register_max_bits);
  }

  // A coarse size tier is intentionally selected before color-keyed overrides:
  // a user naming one specific region always has the final say. `input_ge` is
  // invariant source-logic cost, unlike mapped gates, so cache recipes and
  // threshold decisions remain stable when the mapping flow changes.
  if (opts_.small_ge != 0 && !opts_.small_flow.empty() && input_ge >= opts_.small_min_ge && input_ge <= opts_.small_ge) {
    opts_.flow = opts_.small_flow;
    if (opts_.verbose) {
      std::print("[pass.abc] region '{}': small_flow selected ({} <= {} GE <= {})\n",
                 rb.module_name,
                 opts_.small_min_ge,
                 input_ge,
                 opts_.small_ge);
    }
  }
  apply_region_overrides(rb);
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
  std::string recipe  = resolve_recipe();
  recipe             += opts_.map_register ? "\n# livehd-register=abc" : "\n# livehd-register=native";
  hhds::Graph* pre_g  = (incr_ != nullptr && rb.reuse_eligible) ? rb.pre_body : nullptr;
  // EXPERIMENTAL (ABC_INCR_COMPARE_ONLY): exercise compare/store with NO ABC -- a
  // fast diagnostic for why a region misses on a comment edit.
  if (incr_ != nullptr && std::getenv("ABC_INCR_COMPARE_ONLY") != nullptr) {
    bool hit = false;
    if (pre_g != nullptr) {
      hit = incr_->lookup_compare(rb, pre_g, recipe).hit;
      incr_->store_pre(rb, *rb.pre_lib, rb.pre_name, recipe);
    }
    std::print("COMPARE {} {}\n",
               rb.module_name,
               !rb.reuse_eligible ? "INELIGIBLE" : (pre_g == nullptr ? "REBUILD-FAIL" : (hit ? "HIT" : "MISS")));
    Region_qor q;
    q.module      = rb.module_name;
    q.color       = rb.color;
    q.input_nodes = input_nodes;
    q.input_ge    = input_ge;
    q.cache       = hit ? "hit" : "miss";
    qor_.push_back(std::move(q));
    return;
  }
  if (incr_ != nullptr && rb.reuse_eligible) {
    if (pre_g != nullptr) {
      auto res = incr_->lookup_compare(rb, pre_g, recipe);
      if (res.hit && incr_->reuse_hit(rb, res, outlib_)) {
        Region_qor q;
        q.module       = rb.module_name;
        q.color        = rb.color;
        q.input_nodes  = input_nodes;
        q.input_ge     = input_ge;
        q.gates        = res.row->gates;
        q.area         = res.row->area;
        q.delay        = res.row->delay;
        q.crit_src     = res.row->crit_src;
        q.crit_output  = res.crit_output;
        q.div_blackbox = res.row->div_blackbox;
        q.cache        = "hit";
        q.ms           = since();
        std::print("[pass.abc] region '{}': cache hit -- {} gates, area {:.2f}, delay {:.2f} ({:.0f} ms)\n",
                   rb.module_name,
                   q.gates,
                   q.area,
                   q.delay,
                   q.ms);
        qor_.push_back(std::move(q));
        return;
      }
    }
    incr_->note_miss();
  }

  // Do not pay Abc_Start/read_lib for an all-hit rebuild. The cache salt has
  // already folded the Liberty content and run-level mapping modes, while the
  // exact pre-body comparison authorized the reused result. Only a real miss
  // needs the mapper process and parsed library.
  if (!start()) {
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

  // Set_mask is wiring, not logic. Materializing every output bit is ruinous
  // for sparse updates of a wide packed state bus (Rob: 7,760 nodes expanded
  // to 179M bitnet hash entries). Keep the selected positions as compact runs
  // and resolve an alias only when a downstream gate actually asks for it.
  struct Set_mask_run {
    int lo;
    int hi;          // exclusive
    int value_base;  // compact value-bit position corresponding to lo
  };
  struct Set_mask_alias {
    hhds::Pin_class           base;
    hhds::Pin_class           value;
    std::vector<Set_mask_run> runs;
  };
  // node_hash_map, not flat: the Set_mask arm binds `auto& alias = it->second`
  // and then passes `alias.value` / `alias.base` BY REFERENCE into a recursive
  // abc_bit() that can try_emplace a nested Set_mask and rehash this very map,
  // which would leave that reference (and the argument bound to it) dangling.
  absl::node_hash_map<hhds::Node_class, Set_mask_alias>               set_mask_aliases;
  absl::node_hash_map<hhds::Pin_class, absl::flat_hash_set<int>>      resolving_wiring_bit;
  // Decoded lane table per Concat node. concat_lanes() walks inp_edges and
  // allocates a map + a vector on every call, and the lazy path asks for one
  // bit at a time -- without this the decode (and the lane-layout check, a
  // per-NODE property) would run once per demanded BIT. node_hash_map, not
  // flat: the arm holds a reference into this table across a recursive
  // abc_bit() that can decode a nested concat and rehash it.
  absl::node_hash_map<hhds::Node_class, std::vector<gu::Concat_lane>> concat_lanes_of;

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
    if (gu::is_const_pin(drv)) {
      auto  val  = gu::hydrate_const(drv);
      auto* net  = abc_const_bit(val.bit_test(eff));
      slots[eff] = net;
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
    auto master = drv.get_master_node();
    if (gu::type_op_of(master) == Ntype_op::Set_mask) {
      auto& resolving = resolving_wiring_bit[drv];
      if (!resolving.insert(eff).second) {
        if (!unsupported) {
          livehd::diag::err("pass.abc", "combinational-cycle", "unsupported")
              .msg("pass.abc: region '{}': bit {} of '{}' has a combinational wiring cycle",
                   rb.module_name,
                   eff,
                   gu::debug_name(master))
              .emit();
          unsupported = true;
        }
        return abc_const_bit(false);
      }
      auto [it, inserted] = set_mask_aliases.try_emplace(master);
      auto& alias         = it->second;
      if (inserted) {
        alias.base       = gu::get_driver_of_sink_name(master, "a");
        alias.value      = gu::get_driver_of_sink_name(master, "value");
        auto mask_driver = gu::get_driver_of_sink_name(master, "mask");
        if (gu::is_const_pin(mask_driver)) {
          const auto mask     = gu::hydrate_const(mask_driver);
          const bool negative = mask.is_negative();
          const int  prefix   = std::max(0, static_cast<int>(mask.get_bits()) - (negative ? 1 : 0));
          const int  limit    = w == 0 ? prefix : std::min(prefix, w);
          int        run_lo   = -1;
          int        selected = 0;
          for (int bit = 0; bit < limit; ++bit) {
            const bool take = negative ? !mask.bit_test(bit) : mask.bit_test(bit);
            if (take && run_lo < 0) {
              run_lo = bit;
            } else if (!take && run_lo >= 0) {
              alias.runs.push_back({run_lo, bit, selected});
              selected += bit - run_lo;
              run_lo    = -1;
            }
          }
          if (run_lo >= 0) {
            alias.runs.push_back({run_lo, limit, selected});
            selected += limit - run_lo;
          }
          // A negative mask selects every sign-extended mask position above
          // its explicit prefix. Merge that tail with an adjacent final run.
          const int tail_hi = w == 0 ? prefix : w;
          if (negative && prefix < tail_hi) {
            if (!alias.runs.empty() && alias.runs.back().hi == prefix) {
              alias.runs.back().hi = tail_hi;
            } else {
              alias.runs.push_back({prefix, tail_hi, selected});
            }
          }
        }
      }
      for (const auto& run : alias.runs) {
        if (eff >= run.lo && eff < run.hi) {
          auto* net  = abc_bit(alias.value, run.value_base + eff - run.lo);
          slots[eff] = net;
          resolving.erase(eff);
          return net;
        }
      }
      auto* net  = abc_bit(alias.base, eff);
      slots[eff] = net;
      resolving.erase(eff);
      return net;
    }
    if (gu::type_op_of(master) == Ntype_op::Concat) {
      auto& resolving = resolving_wiring_bit[drv];
      if (!resolving.insert(eff).second) {
        if (!unsupported) {
          livehd::diag::err("pass.abc", "combinational-cycle", "unsupported")
              .msg("pass.abc: region '{}': bit {} of '{}' has a combinational wiring cycle",
                   rb.module_name,
                   eff,
                   gu::debug_name(master))
              .emit();
          unsupported = true;
        }
        return abc_const_bit(false);
      }
      auto [lane_it, lane_new] = concat_lanes_of.try_emplace(master);
      if (lane_new) {
        lane_it->second = gu::concat_lanes(master);
        if (!lane_it->second.empty()) {
          // A lane's window width is an explicit const operand precisely
          // because it is NOT recoverable from its driver, so an overlapping or
          // receding window would shift every lane ABOVE the bad one -- a
          // silent miscompile. Once per node, on its first demanded bit.
          const auto lane_bad = gu::concat_lane_violation(lane_it->second);
          I(lane_bad.empty(), lane_bad.c_str());
        }
      }
      const auto& lanes = lane_it->second;
      if (lanes.empty()) {
        // Empty means MALFORMED (odd/missing pin, non-const or non-positive
        // lane width), never "zero lanes" -- fail closed like the non-constant
        // mask/position arms rather than emitting a const0 bus.
        if (!unsupported) {
          livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
              .msg("pass.abc: malformed concat (missing lane operand, or a non-constant lane width) in region '{}'", rb.module_name)
              .emit();
          unsupported = true;
        }
        resolving.erase(eff);
        return abc_const_bit(false);
      }
      Abc_Obj_t* net = nullptr;
      for (const auto& lane : lanes) {
        if (eff < lane.offset || eff >= lane.offset + lane.width) {
          continue;
        }
        const int bit = eff - lane.offset;
        if (gu::is_const_pin(lane.value)) {
          net = abc_bit(lane.value, bit);
        } else {
          const int ew = std::max(1, gu::real_width(lane.value));
          net          = bit < ew ? abc_bit(lane.value, bit)
                                  : (gu::is_unsign(lane.value) ? abc_const_bit(false) : abc_bit(lane.value, ew - 1));
        }
        break;
      }
      if (net == nullptr) {
        net = abc_const_bit(false);  // output stamp above the concat contract width
      }
      slots[eff] = net;
      resolving.erase(eff);
      return net;
    }
    // A region-internal node not yet materialized (a genuine node-level cycle
    // the fixpoint scheduler could not resolve) or an unexpected boundary:
    // use a temporary constant only to let translation unwind, but reject the
    // region. Accepting that placeholder would silently miscompile the whole
    // downstream cone. Suppress follow-on diagnostics once the first missing
    // producer has identified the region-level failure.
    if (!unsupported) {
      livehd::diag::err("pass.abc", "unmaterialized-driver", "internal")
          .msg("pass.abc: region '{}': bit {} of driver '{}' (node op {}) could not be materialized",
               rb.module_name,
               eff,
               gu::debug_name(drv.get_master_node()),
               Ntype::get_name(gu::type_op_of(drv.get_master_node())))
          .hint("the colored region contains a combinational cycle or an invalid boundary; refusing to emit a wrong netlist")
          .emit();
      unsupported = true;
    }
    auto* net  = abc_const_bit(false);
    slots[eff] = net;
    return net;
  };

  auto real_width = [&](const hhds::Pin_class& p) -> int { return std::max(1, gu::real_width(p)); };

  // Width hints are literal at region boundaries and on internal nets alike.
  auto eff_width = [&](const hhds::Pin_class& d) -> int {
    if (gu::is_const_pin(d)) {
      // A constant driver usually carries NO bits attribute (bits_of == 0), so
      // An unstamped constant would clamp to 1 bit and a width-sensitive consumer
      // (mult/sra) would read e.g. 342 as its bit 0 only — collapsing the whole
      // cone to a constant (the const-mult miscompile). Size a constant from
      // its VALUE: get_bits() is the minimal two's-complement width, which is
      // exactly how the LEC reads the literal.
      return std::max(1, static_cast<int>(gu::hydrate_const(d).get_bits()));
    }
    return real_width(d);
  };
  // Bit i of an operand as the LEC sees it: the real bit below its effective
  // width, then sign/zero extension above it.
  auto abc_eff_bit = [&](const hhds::Pin_class& d, int i) -> Abc_Obj_t* {
    if (gu::is_const_pin(d)) {
      // Constants are exact in abc_bit: with no bits attr (w == 0) it reads the
      // literal's two's-complement bit at ANY position (negatives sign-extend
      // via bit_test), and with a stamped attr it clamps like every other
      // consumer. Bypassing the eff-width clamp avoids truncating the value.
      return abc_bit(d, i);
    }
    int ew = eff_width(d);
    if (i < ew) {
      return abc_bit(d, i);
    }
    return gu::is_unsign(d) ? abc_const_bit(false) : abc_bit(d, ew - 1);
  };

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
  // comb loop (it may depend on logic that has not been bit-blasted yet). Flops
  // stay NATIVE on read-back (never mapped to library DFFs) -- the latch only
  // exists so ABC can optimize/retime across the register boundary.
  struct Seq_flop {
    hhds::Node_class        node;
    std::string             root;
    int                     bits = 0;
    hhds::Pin_class         q_pin;
    hhds::Pin_class         din_drv, en_drv, rst_drv, rval_drv, clk_drv;
    bool                    neg_reset = false;
    std::vector<Abc_Obj_t*> bi;  // per-bit latch BI (data-in terminal)
  };
  // Region-input driver -> port name. Used twice: to reconnect a flop
  // boundary's control pins natively (see the boundary scan below), and to
  // decide whether a register's clock even HAS a native source on read-back.
  absl::flat_hash_map<hhds::Pin_class, std::string> region_in_name;
  for (const auto& port : rb.inputs) {
    region_in_name.emplace(port.src_driver, port.name);
  }

  // A region consisting solely of a constant left shift is pure bus wiring.
  // ABC turns the zero-padding into one mapped object per output bit (Rob has
  // hundreds of these regions, growing to 10k bits each). Rebuild the typed
  // wiring node directly; OpenTimer tracks constant SHL bit identity and no
  // Liberty delay is being skipped because there is no Boolean gate here.
  if (rb.nodes.size() == 1 && gu::type_op_of(rb.nodes.front()) == Ntype_op::SHL) {
    const auto src_node = rb.nodes.front();
    const auto a        = gu::get_driver_of_sink_name(src_node, "a");
    const auto b        = gu::get_driver_of_sink_name(src_node, "b");
    auto       ait      = region_in_name.find(a);
    if (!a.is_invalid() && gu::is_const_pin(b) && (gu::is_const_pin(a) || ait != region_in_name.end())) {
      auto node = gu::create_typed_node(*rb.body, Ntype_op::SHL);
      if (gu::is_const_pin(a)) {
        gu::create_const(*rb.body, gu::hydrate_const(a)).connect_sink(gu::setup_sink_by_name(node, "a"));
      } else {
        rb.body->get_input_pin(ait->second).connect_sink(gu::setup_sink_by_name(node, "a"));
      }
      gu::create_const(*rb.body, gu::hydrate_const(b)).connect_sink(gu::setup_sink_by_name(node, "b"));
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
      q.module      = rb.module_name;
      q.color       = rb.color;
      q.input_nodes = input_nodes;
      q.input_ge    = input_ge;
      q.gates       = 0;
      q.area        = 0;
      q.delay       = 0;
      q.cache       = "miss";
      q.ms          = since();
      qor_.push_back(std::move(q));
      std::print("[pass.abc] region '{}': constant SHL kept as native wiring, {:.0f} ms\n", rb.module_name, since());
      Abc_NtkDelete(manNtk);
      return;
    }
  }

  std::vector<Seq_flop>                 flops;
  absl::flat_hash_set<hhds::Node_class> clk_demoted;  // registers demoted to boundary: clock from region-internal logic
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
      // tolg may wrap a call-site clock in 1-bit Get_mask coercions (`x:u1`
      // casts survive cprop when the source is signed). On a 1-bit operand they are wire
      // identities regardless of the declared output width — trace to the root
      // so the register's clock is recognized as region-input-driven and the
      // DFF clock pin connects DIRECTLY to it (never through mapped logic).
      //
      // Whole-design flatten can stack one such coercion per hierarchy level,
      // so trace the identity chain to the structural clock source.
      for (int guard = 0; guard < 64 && !f.clk_drv.is_invalid(); ++guard) {  // guard: cycle net, > any sane hierarchy depth
        auto m = f.clk_drv.get_master_node();
        if (gu::type_op_of(m) != Ntype_op::Get_mask) {
          break;
        }
        auto a    = gu::get_driver_of_sink_name(m, "a");
        auto mask = gu::get_driver_of_sink_name(m, "mask");
        if (a.is_invalid() || real_width(a) != 1 || mask.is_invalid() || !gu::is_const_pin(mask)
            || !gu::hydrate_const(mask).bit_test(0)) {
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
      if (!f.clk_drv.is_invalid() && !gu::is_const_pin(f.clk_drv) && !region_in_name.contains(f.clk_drv)) {
        clk_demoted.insert(n);
        continue;
      }
      if (auto nr = gu::get_driver_of_sink_name(n, "negreset"); !nr.is_invalid() && gu::is_const_pin(nr)) {
        f.neg_reset = gu::hydrate_const(nr).bit_test(0);
      }
      bool  has_rval = !f.rval_drv.is_invalid() && gu::is_const_pin(f.rval_drv);
      auto  rval     = has_rval ? gu::hydrate_const(f.rval_drv) : Dlop{};
      auto& slots    = bitnet[f.q_pin];
      for (int b = 0; b < f.bits; ++b) {
        auto* bo    = Abc_NtkCreateBo(manNtk);
        auto* latch = Abc_NtkCreateLatch(manNtk);
        auto* bi    = Abc_NtkCreateBi(manNtk);
        Abc_ObjAddFanin(bo, latch);
        Abc_ObjAddFanin(latch, bi);
        if (has_rval) {
          rval.bit_test(b) ? Abc_LatchSetInit1(latch) : Abc_LatchSetInit0(latch);
        } else {
          Abc_LatchSetInitDc(latch);
        }
        auto* qnet = Abc_NtkCreateNet(manNtk);
        Abc_ObjAddFanin(qnet, bo);
        auto nm = f.bits == 1 ? std::format("{}_%r", f.root) : std::format("{}_%r_{}", f.root, b);
        Abc_ObjAssignName(qnet, const_cast<char*>(nm.c_str()), nullptr);
        slots[b] = qnet;  // flop Q bit -> latch output net (a CI source for the AIG)
        f.bi.push_back(bi);
      }
      flops.push_back(std::move(f));
    }
    if (!clk_demoted.empty()) {
      livehd::diag::warn("pass.abc", "derived-clock-native", "unsupported")
          .msg(
              "pass.abc region '{}': {} register(s) clocked by region-internal logic (a gated/derived clock) kept as "
              "native flops — a DFF cell cannot take its clock from mapped logic; the clock cone is still mapped and "
              "reconnected",
              rb.module_name,
              clk_demoted.size())
          .emit();
    }
  }

  // A very wide OR of non-overlapping, constant-position shifts is a packed-bus
  // assembly, not Boolean logic. Sending its thousands of identity bits through
  // ABC is pathological (Rob's 24x511 -> 10911 pack spent minutes in &nf).
  // Keep the SHLs and their OR as native zero-delay wiring, just like
  // Get_mask/Set_mask at the mapper boundary. pass.opentimer's pin tracker
  // understands both operations, so timing identity is preserved bit-for-bit.
  absl::flat_hash_set<hhds::Node_class> native_wiring;
  auto                                  node_output_width = [](const hhds::Node_class& n) {
    int width = 0;
    for (const auto& e : n.out_edges()) {
      width = std::max(width, gu::bits_of(e.driver));
    }
    return width != 0 ? width : gu::bits_of(n.create_driver_pin(0));
  };
  for (const auto& n : rb.nodes) {
    if (gu::type_op_of(n) != Ntype_op::Or || node_output_width(n) < 4096) {
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
      if (gu::is_const_pin(e.driver)) {
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
      if (a.is_invalid() || !gu::is_const_pin(b)) {
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
    if (!packing || shifts.size() < 2) {
      if (opts_.verbose) {
        std::print("[pass.abc] region '{}': rejected {}-bit shift/OR pack after {} shift(s): {}\n",
                   rb.module_name,
                   node_output_width(n),
                   shifts.size(),
                   reject.empty() ? "too few shifts" : reject);
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
    const bool constant_shl = op == Ntype_op::SHL && gu::is_const_pin(gu::get_driver_of_sink_name(n, "b"));
    const bool wide_pack    = node_output_width(n) >= 4096 && (op == Ntype_op::Concat || op == Ntype_op::Set_mask);
    if (!region.contains(n) || (!constant_shl && !wide_pack)) {
      continue;
    }
    if (native_wiring.insert(n).second) {
      exported_wiring.push_back(n);
    }
  }
  for (size_t head = 0; head < exported_wiring.size(); ++head) {
    for (const auto& e : exported_wiring[head].inp_edges()) {
      if (e.driver.is_invalid() || gu::is_const_pin(e.driver)) {
        continue;
      }
      const auto parent = e.driver.get_master_node();
      const auto op     = gu::type_op_of(parent);
      if (!region.contains(parent) || node_output_width(parent) < 4096 || (op != Ntype_op::Concat && op != Ntype_op::Set_mask)) {
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
  };
  // region_in_name (built above the register scan) reconnects a flop
  // boundary's control pins (clock/reset/enable that come straight from a
  // region input) NATIVELY on read-back. Routing such a clock through the
  // combinational AIG would map it to a logic buffer and make the rebuilt flop
  // clock on `posedge <data-wire>` -- logically correct but unusable as a real
  // netlist (breaks clock-tree synthesis and timing). Only the flop's din cone
  // (genuine comb logic) crosses into ABC. A clk_demoted register's GATED
  // clock cone, by contrast, IS genuine logic and does cross as a PO.
  // Convert the trivially-mappable remainders BEFORE the boundary scan, so the
  // refusal below only fires for a shape that genuinely has no gate translation.
  if (rems_rewritten_graphs_.insert(rb.src).second) {
    rewrite_trivial_rems(rb.src);
  }

  std::vector<Bbox>                      bboxes;
  std::vector<std::tuple<int, int, int>> bbox_pi;  // appended PI -> (bbox, out, bit)
  for (const auto& n : rb.nodes) {
    auto       op             = gu::type_op_of(n);
    // A flop in a !seq (combinational-only) map is kept as a native boundary,
    // exactly like a Sub/Memory: its Q feeds the mapped logic as a fresh PI, its
    // din/enable/clock/reset are cut as POs (or recreated when const), and the
    // Flop node is rebuilt unchanged on read-back (never bit-blasted). In seq
    // mode flops instead cross into ABC as 1-bit latches (handled above), so they
    // are excluded from the boundary set there — EXCEPT registers demoted for a
    // region-internal (gated/derived) clock, which take this boundary path.
    bool       flop_boundary  = gu::is_type_flop(n) && (!opts_.map_register || clk_demoted.contains(n));
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
    if (op != Ntype_op::Sub && op != Ntype_op::Memory && op != Ntype_op::Clock_cell && op != Ntype_op::Div && op != Ntype_op::Rem
        && !flop_boundary && !latch_boundary && !native_wiring.contains(n)) {
      continue;
    }
    if (op == Ntype_op::Div) {
      // Division is not bit-blasted: a synthesizable divider is large and out of
      // scope. The Div node is kept native as a blackbox boundary (its output
      // feeds the AIG as a fresh PI, its inputs are cut as POs), exactly like a
      // Sub/Memory instance, and rebuilt unchanged on read-back. Warn so the
      // user knows this cone is not technology-mapped.
      livehd::diag::warn("pass.abc", "div-blackbox", "unsupported")
          .msg("pass.abc: division in region '{}' is blackboxed (kept as a native div, not technology-mapped)", rb.module_name)
          .emit();
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
          .msg("pass.abc: remainder (`%`) in region '{}' has no gate-level translation", rb.module_name)
          .hint(
              "only a power-of-two divisor over a non-negative dividend converts trivially (to a mask); keep other "
              "remainders out of the synthesized region")
          .emit();
    }
    Bbox bb;
    bb.node                                      = n;
    bb.op                                        = op;
    int                                   bb_idx = static_cast<int>(bboxes.size());
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
      auto& slots = bitnet[op_pin];
      for (int b = 0; b < w; ++b) {
        auto* obj = Abc_NtkCreatePi(manNtk);
        auto* net = Abc_NtkCreateNet(manNtk);
        Abc_ObjAddFanin(net, obj);
        slots[b] = net;
        all_pi_order.push_back({Pi_kind::bbox_output, bbox_pi.size()});
        bbox_pi.emplace_back(bb_idx, oi, b);
      }
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
      if (gu::is_const_pin(e.driver)) {
        bb.const_ins.emplace_back(pid, e.driver);
      } else if (region_in_name.contains(e.driver) || native_wiring.contains(e.driver.get_master_node())) {
        bb.native_ins.emplace_back(pid, e.driver);
      } else {
        int w = gu::bits_of(e.driver);
        if (w == 0) {
          w = 1;
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
      if (op == Ntype_op::Sub || op == Ntype_op::Memory || op == Ntype_op::Clock_cell || op == Ntype_op::Div || op == Ntype_op::Rem
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
        if (d.is_invalid() || gu::is_const_pin(d) || ready.contains(d) || region_input_index.contains(d)) {
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
            if (d.is_invalid() || gu::is_const_pin(d) || ready.contains(d) || region_input_index.contains(d)) {
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
    auto& slots = bitnet[out_pin];

    if (op == Ntype_op::Not) {
      hhds::Pin_class a;
      for (const auto& e : n.inp_edges()) {
        a = e.driver;
      }
      for (int b = 0; b < out_bits; ++b) {
        slots[b] = abc_not(abc_bit(a, b));
      }
    } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
      char                         kind = op == Ntype_op::And ? '&' : (op == Ntype_op::Or ? '|' : '^');
      std::vector<hhds::Pin_class> ins;
      for (const auto& e : n.inp_edges()) {
        ins.push_back(e.driver);
      }
      for (int b = 0; b < out_bits; ++b) {
        Abc_Obj_t* acc = nullptr;
        for (const auto& d : ins) {
          auto* bit = abc_bit(d, b);
          acc       = acc == nullptr ? bit : abc_bin(acc, bit, kind);
        }
        slots[b] = acc == nullptr ? abc_const_bit(false) : acc;
      }
    } else if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
      hhds::Pin_class                       sel;
      absl::btree_map<int, hhds::Pin_class> data;  // pid-1 (value) -> driver; ordered so the OR-tree fed to ABC is deterministic
      int                                   max_v = -1;
      for (const auto& e : n.inp_edges()) {
        auto pid = e.sink.get_port_id();
        if (pid == 0) {
          sel = e.driver;
        } else {
          data[static_cast<int>(pid) - 1] = e.driver;
          max_v                           = std::max(max_v, static_cast<int>(pid) - 1);
        }
      }
      int sel_bits = gu::bits_of(sel);
      if (sel_bits == 0) {
        sel_bits = 1;
      }
      for (int b = 0; b < out_bits; ++b) {
        Abc_Obj_t* acc = nullptr;
        for (const auto& [v, drv] : data) {
          Abc_Obj_t* term = abc_bit(drv, b);  // data_v[b]
          Abc_Obj_t* hit  = nullptr;          // selector matches value v
          if (op == Ntype_op::Hotmux) {
            hit = abc_bit(sel, v);  // one-hot: bit v of selector
          } else {
            for (int sb = 0; sb < sel_bits; ++sb) {
              auto* sbit = abc_bit(sel, sb);
              auto* lit  = ((v >> sb) & 1) ? sbit : abc_not(sbit);
              hit        = hit == nullptr ? lit : abc_bin(hit, lit, '&');
            }
            if (hit == nullptr) {
              hit = abc_const_bit(true);
            }
          }
          auto* prod = abc_bin(term, hit, '&');
          acc        = acc == nullptr ? prod : abc_bin(acc, prod, '|');
        }
        slots[b] = acc == nullptr ? abc_const_bit(false) : acc;
      }
    } else if (op == Ntype_op::Get_mask) {
      // out[j] = a[positions[j]] where positions = mask-selected source bits.
      auto a_drv = gu::get_driver_of_sink_name(n, "a");
      auto m_drv = gu::get_driver_of_sink_name(n, "mask");
      if (!gu::is_const_pin(m_drv)) {
        livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
            .msg("pass.abc: get_mask with a non-constant mask in region '{}' is not supported", rb.module_name)
            .emit();
        unsupported = true;
      } else {
        auto mask   = gu::hydrate_const(m_drv);
        bool neg    = mask.is_negative();
        int  mb     = mask.get_bits();
        int  pmb    = neg ? mb - 1 : mb;
        int  a_bits = gu::bits_of(a_drv);
        if (a_bits == 0 && gu::is_const_pin(a_drv)) {
          // A CONSTANT driver carries no `bits` attr, so bits_of is 0 (see
          // eff_width above — create_const stamps only the value, never a width).
          // The zero-extend idiom Get_mask(a, -1) puts EVERY source position in
          // the negative fill loop below, which is bounded by a_bits: left at 0
          // it yields an empty `pos` and the final loop writes const0 into every
          // output bit, silently replacing the literal with 0. Note abc_bit is
          // never reached, so its unmaterialized-driver diagnostic cannot warn.
          // Size the literal from its VALUE, exactly as eff_width does.
          a_bits = std::max(1, static_cast<int>(gu::hydrate_const(a_drv).get_bits()));
        }
        std::vector<int> pos;
        for (int k = 0; k < pmb; ++k) {
          bool sel = neg ? !mask.bit_test(k) : mask.bit_test(k);
          if (sel) {
            pos.push_back(k);
          }
        }
        if (neg) {
          for (int k = pmb; k < a_bits; ++k) {
            pos.push_back(k);
          }
        }
        for (int b = 0; b < out_bits; ++b) {
          slots[b] = b < static_cast<int>(pos.size()) ? abc_bit(a_drv, pos[b]) : abc_const_bit(false);
        }
      }
    } else if (op == Ntype_op::Set_mask) {
      // Pure wiring, resolved lazily by abc_bit above. Do not materialize every
      // bit of a wide sparse-update bus here.
      auto m_drv = gu::get_driver_of_sink_name(n, "mask");
      if (!gu::is_const_pin(m_drv)) {
        livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
            .msg("pass.abc: set_mask with a non-constant mask in region '{}' is not supported", rb.module_name)
            .emit();
        unsupported = true;
      }
    } else if (op == Ntype_op::Sext) {
      // out[i] = a[min(i, from_bit)] (sign bit at from_bit replicated above).
      auto a_drv = gu::get_driver_of_sink_name(n, "a");
      auto b_drv = gu::get_driver_of_sink_name(n, "b");
      if (!gu::is_const_pin(b_drv)) {
        livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
            .msg("pass.abc: sext with a non-constant bit position in region '{}' is not supported", rb.module_name)
            .emit();
        unsupported = true;
      } else {
        int from_bit = static_cast<int>(gu::hydrate_const(b_drv).to_just_i64());
        for (int b = 0; b < out_bits; ++b) {
          slots[b] = abc_bit(a_drv, std::min(b, from_bit));
        }
      }
    } else if (op == Ntype_op::Sum) {
      // result = sum(A terms, pid 0) - sum(B terms, pid 1), at width out_bits
      // (the bitwidth-resolved result width, wide enough for carry growth).
      // Each operand is sign/zero-extended to that width by abc_bit; A terms
      // accumulate (cin=0), B terms subtract via two's complement (~b + 1).
      std::vector<hhds::Pin_class> a_drv;
      std::vector<hhds::Pin_class> b_drv;
      for (const auto& e : n.inp_edges()) {
        if (e.sink.get_port_id() == 0) {
          a_drv.push_back(e.driver);
        } else if (e.sink.get_port_id() == 1) {
          b_drv.push_back(e.driver);
        }
      }
      int  bs     = opts_.block_size > 0 ? opts_.block_size : arith::default_block_size(out_bits);
      auto extend = [&](const hhds::Pin_class& d) {
        std::vector<Abc_Obj_t*> v(out_bits);
        for (int i = 0; i < out_bits; ++i) {
          v[i] = abc_bit(d, i);
        }
        return v;
      };
      std::vector<Abc_Obj_t*> acc;
      size_t                  ai = 0;
      if (a_drv.empty()) {
        acc.assign(out_bits, abc_const_bit(false));
      } else {
        acc = extend(a_drv[0]);
        ai  = 1;
      }
      for (; ai < a_drv.size(); ++ai) {
        acc = arith::build_add(opts_.adder, bs, ops, acc, extend(a_drv[ai]), abc_const_bit(false)).sum;
      }
      for (const auto& bd : b_drv) {
        acc = arith::build_add(opts_.adder, bs, ops, acc, arith::bv_invert(ops, extend(bd)), abc_const_bit(true)).sum;
      }
      for (int b = 0; b < out_bits; ++b) {
        slots[b] = acc[b];
      }
    } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
      // 1-bit result. pid 0 = a, pid 1 = b; LT = a<b, GT = a>b == b<a. Compare
      // at max(width)+1 (one guard bit so a-b can't overflow the signed range).
      hhds::Pin_class a_d;
      hhds::Pin_class b_d;
      for (const auto& e : n.inp_edges()) {
        if (e.sink.get_port_id() == 0) {
          a_d = e.driver;
        } else if (e.sink.get_port_id() == 1) {
          b_d = e.driver;
        }
      }
      bool                    uns = gu::is_unsign(a_d) && gu::is_unsign(b_d);
      int                     w   = std::max(gu::bits_of(a_d), gu::bits_of(b_d)) + 1;
      int                     bs  = opts_.block_size > 0 ? opts_.block_size : arith::default_block_size(w);
      std::vector<Abc_Obj_t*> av(w);
      std::vector<Abc_Obj_t*> bv(w);
      for (int i = 0; i < w; ++i) {
        av[i] = abc_bit(a_d, i);
        bv[i] = abc_bit(b_d, i);
      }
      Abc_Obj_t* res = op == Ntype_op::LT ? arith::build_lt(opts_.adder, bs, ops, av, bv, uns)
                                          : arith::build_lt(opts_.adder, bs, ops, bv, av, uns);
      slots[0]       = res;
      for (int b = 1; b < out_bits; ++b) {
        slots[b] = abc_const_bit(false);
      }
    } else if (op == Ntype_op::EQ) {
      // 1-bit result; n-ary all-equal (operands on pid 0). Compare at
      // max(width)+1 so sign-extension differences are caught.
      std::vector<hhds::Pin_class> ds;
      for (const auto& e : n.inp_edges()) {
        ds.push_back(e.driver);
      }
      if (ds.size() <= 1) {
        slots[0] = abc_const_bit(true);
      } else {
        int w = 0;
        for (const auto& d : ds) {
          w = std::max(w, gu::bits_of(d));
        }
        ++w;
        std::vector<std::vector<Abc_Obj_t*>> operands(ds.size());
        for (size_t k = 0; k < ds.size(); ++k) {
          operands[k].resize(w);
          for (int i = 0; i < w; ++i) {
            operands[k][i] = abc_bit(ds[k], i);
          }
        }
        slots[0] = arith::build_eq(ops, operands);
      }
      for (int b = 1; b < out_bits; ++b) {
        slots[b] = abc_const_bit(false);
      }
    } else if (op == Ntype_op::SHL) {
      // Logical left shift, in a single combinational cone. pid 0 = value `a`,
      // pid 1 = shift amount `b` (both single-driver; the old one-hot multi-shift
      // `n<<(b0,b1,…)` form was removed). The cvc5 LEC encodes SHL identically
      // (fit `a` to the result width, shift unsigned). A CONSTANT amount becomes
      // pure bit re-wiring; a RUNTIME amount becomes a barrel/log shifter
      // (arith::build_shl). `a` is sign/zero extended to out_bits by abc_bit,
      // matching the LEC's fit-to-W.
      hhds::Pin_class a_d;
      hhds::Pin_class b_d;
      for (const auto& e : n.inp_edges()) {
        if (e.sink.get_port_id() == 0) {
          a_d = e.driver;
        } else if (e.sink.get_port_id() == 1) {
          b_d = e.driver;
        }
      }
      std::vector<Abc_Obj_t*> av(out_bits);
      for (int i = 0; i < out_bits; ++i) {
        av[i] = abc_bit(a_d, i);
      }
      std::vector<Abc_Obj_t*> sh;  // empty => no amount (result == a)
      if (!b_d.is_invalid()) {
        if (gu::is_const_pin(b_d)) {
          auto amt_c = gu::hydrate_const(b_d);
          if (amt_c.has_unknowns() || amt_c.is_negative()) {
            // An unknown (x-bit) or negative constant shift amount cannot be
            // soundly technology-mapped (ABC has no X; a negative shift is
            // rejected upstream by upass.bitwidth), never from a clean `a<<N`.
            livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
                .msg("pass.abc: shl with an unknown/negative constant shift amount in region '{}' is not supported", rb.module_name)
                .emit();
            unsupported = true;
          } else {
            // Clean non-negative integer: out[i] = a[i-amt], 0 below. A value too
            // big for i64 (or simply >= out_bits) shifts everything out -> 0.
            int64_t amt = amt_c.is_just_i64() ? amt_c.to_just_i64() : static_cast<int64_t>(out_bits);
            sh.resize(out_bits);
            for (int i = 0; i < out_bits; ++i) {
              sh[i] = (i - amt >= 0) ? av[static_cast<int>(i - amt)] : abc_const_bit(false);
            }
          }
        } else {
          int bw = gu::bits_of(b_d);
          if (bw <= 0) {
            bw = 1;
          }
          std::vector<Abc_Obj_t*> bv(bw);
          for (int i = 0; i < bw; ++i) {
            bv[i] = abc_bit(b_d, i);  // unsigned shift count
          }
          sh = arith::build_shl(ops, av, bv, out_bits);
        }
      }
      for (int b = 0; b < out_bits; ++b) {
        slots[b] = sh.empty() ? av[b] : sh[b];
      }
    } else if (op == Ntype_op::SRA) {
      // Right shift: pid 0 = value `a` (single), pid 1 = shift amount `b`
      // (single). Arithmetic (sign-replicating) when `a` is signed, logical
      // otherwise — mirroring Verilog `>>>` and the cvc5 LEC (BITVECTOR_ASHR vs
      // BITVECTOR_LSHR). A right shift pulls bits DOWN from higher positions, so
      // the value must be at its FULL width before shifting: the LEC shifts at
      // cw = max(operand_width, output_width) and truncates the result to W, so
      // `a` is sign/zero extended (by abc_bit) to cw, the amount is read unsigned
      // and fit to cw (bits at/above cw are dropped, matching the LEC's fit), and
      // the low out_bits become the result. A CONSTANT amount becomes pure bit
      // re-wiring; a RUNTIME amount a combinational barrel shifter (build_shr).
      hhds::Pin_class a_d;
      hhds::Pin_class b_d;
      for (const auto& e : n.inp_edges()) {
        if (e.sink.get_port_id() == 0) {
          if (a_d.is_invalid()) {
            a_d = e.driver;
          }
        } else if (e.sink.get_port_id() == 1) {
          if (b_d.is_invalid()) {
            b_d = e.driver;  // first amount driver, matching the LEC's pid(1)[0]
          }
        }
      }
      bool a_sign          = !gu::is_unsign(a_d);
      int  a_width         = eff_width(a_d);       // operand width as the LEC reads it (port=bits_of, internal=real_width)
      int  out_w           = real_width(out_pin);  // result magnitude width (LEC W)
      int  cw              = std::max(a_width, std::max(out_w, 1));  // shift at the wider of the two
      int  demand_w        = out_w;
      bool sliced_demand   = false;
      bool boundary_output = false;
      // A constant Get_mask is pure wiring. If every in-region consumer only
      // selects a narrow prefix of this shift, build just the barrel-shifter
      // window that can affect those selected bits. A region output or any
      // other consumer conservatively demands the full result.
      for (const auto& port : rb.outputs) {
        if (port.src_driver == out_pin) {
          boundary_output = true;
          break;
        }
      }
      if (!boundary_output) {
        int  selected_hi = 0;
        bool all_slices  = true;
        bool saw_use     = false;
        for (const auto& e : out_pin.out_edges()) {
          auto sink_node = e.sink.get_master_node();
          if (!region.contains(sink_node) || gu::type_op_of(sink_node) != Ntype_op::Get_mask) {
            all_slices = false;
            break;
          }
          auto mask_drv = gu::get_driver_of_sink_name(sink_node, "mask");
          if (!gu::is_const_pin(mask_drv)) {
            all_slices = false;
            break;
          }
          const auto mask = gu::hydrate_const(mask_drv);
          if (mask.is_negative()) {
            all_slices = false;
            break;
          }
          const int wanted = std::max(0, real_width(sink_node.create_driver_pin(0)));
          int       found  = 0;
          int       hi     = 0;
          for (int bit = 0; bit < static_cast<int>(mask.get_bits()) && found < wanted; ++bit) {
            if (mask.bit_test(bit)) {
              hi = bit + 1;
              ++found;
            }
          }
          selected_hi = std::max(selected_hi, hi);
          saw_use     = true;
        }
        if (all_slices && saw_use && selected_hi > 0 && selected_hi < out_w) {
          demand_w      = selected_hi;
          sliced_demand = true;
        }
      }
      if (sliced_demand && opts_.verbose) {
        std::print("[pass.abc] region '{}': right-shift demand reduced from {} to {} bits\n", rb.module_name, out_w, demand_w);
      }
      std::vector<Abc_Obj_t*> av(cw);
      for (int i = 0; i < cw; ++i) {
        av[i] = abc_eff_bit(a_d, i);  // a, sign/zero-extended (past its effective width) to the shift width cw
      }
      Abc_Obj_t*              fill = a_sign ? av[cw - 1] : abc_const_bit(false);  // sign bit (arith) or 0 (logical)
      std::vector<Abc_Obj_t*> res;                                                // cw-wide shifted value
      if (gu::is_const_pin(b_d)) {
        auto amt_c = gu::hydrate_const(b_d);
        if (amt_c.has_unknowns() || amt_c.is_negative()) {
          // Unknown (x-bit) or negative constant shift: unmappable (ABC has no X;
          // a negative shift is rejected upstream by upass.bitwidth). Mirrors SHL.
          livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
              .msg("pass.abc: sra with an unknown/negative constant shift amount in region '{}' is not supported", rb.module_name)
              .emit();
          unsupported = true;
        } else {
          int64_t amt = amt_c.is_just_i64() ? amt_c.to_just_i64() : static_cast<int64_t>(cw);
          // The LEC fits the amount to cw bits (BITVECTOR_ASHR/LSHR operands are
          // same-width), so a count whose magnitude needs MORE than cw bits is read
          // modulo 2^cw, not saturated. Mask to the low cw bits to match (cw>=63
          // can't overflow an i64 amount, so it needs no mask).
          if (cw < 63) {
            amt &= (int64_t{1} << cw) - 1;
          }
          res.resize(demand_w);
          for (int i = 0; i < demand_w; ++i) {
            res[i] = (amt < cw && i + amt < cw) ? av[static_cast<int>(i + amt)] : fill;  // amt >= cw => all fill
          }
        }
      } else {
        int                     nb = std::min(eff_width(b_d), cw);  // amount bits at/above its eff width or cw are 0 (LEC fit)
        std::vector<Abc_Obj_t*> bv(nb);
        for (int i = 0; i < nb; ++i) {
          bv[i] = abc_bit(b_d, i);  // unsigned shift count (i < eff width, so the real bit)
        }
        // Recognize amount = index*scale + bias. This is the canonical packed
        // dynamic word-select lowering. With a narrow demanded prefix, select
        // directly among source words rather than building a full-width barrel.
        hhds::Pin_class index;
        int64_t         scale          = 1;
        int64_t         bias           = 0;
        bool            affine         = false;
        auto            positive_const = [](const hhds::Pin_class& pin, int64_t& value) {
          if (!gu::is_const_pin(pin)) {
            return false;
          }
          const auto c = gu::hydrate_const(pin);
          if (!c.is_just_i64() || c.is_negative()) {
            return false;
          }
          value = c.to_just_i64();
          return true;
        };
        auto amount_node = b_d.get_master_node();
        if (sliced_demand && region.contains(amount_node) && gu::type_op_of(amount_node) == Ntype_op::Sum) {
          hhds::Pin_class term;
          bool            valid = true;
          for (const auto& e : amount_node.inp_edges()) {
            const int sign = e.sink.get_port_id() == 1 ? -1 : 1;
            int64_t   value;
            if (positive_const(e.driver, value)) {
              bias += sign * value;
            } else if (term.is_invalid() && sign > 0) {
              term = e.driver;
            } else {
              valid = false;
            }
          }
          if (valid && !term.is_invalid() && bias >= 0 && region.contains(term.get_master_node())
              && gu::type_op_of(term.get_master_node()) == Ntype_op::Mult) {
            scale = 1;
            for (const auto& e : term.get_master_node().inp_edges()) {
              int64_t value;
              if (positive_const(e.driver, value)) {
                if (value == 0 || scale > INT64_MAX / value) {
                  valid = false;
                  break;
                }
                scale *= value;
              } else if (index.is_invalid()) {
                index = e.driver;
              } else {
                valid = false;
                break;
              }
            }
            affine = valid && !index.is_invalid() && scale > 0;
          }
        }
        const int  index_w     = affine ? eff_width(index) : 0;
        // The two lowerings must agree, because only a COST heuristic picks
        // between them. The generic barrel reads the amount as the nb-bit net it
        // actually is, so an index*scale+bias that overflows that net WRAPS to a
        // small shift and selects real data; build_affine_shr_prefix rebuilds the
        // untruncated math value instead and would fill those bits. Take the
        // affine form only when no reachable index can overflow the amount net,
        // so the choice stays a pure performance decision.
        const bool affine_fits = [&] {
          if (!affine || index_w <= 0 || index_w > 16) {
            return false;
          }
          if (nb >= 62) {
            return true;  // any 16-bit index * scale below fits; avoids the shift UB
          }
          const int64_t max_index = (int64_t{1} << index_w) - 1;
          if (max_index != 0 && scale > (INT64_MAX - bias) / max_index) {
            return false;  // the product alone overflows int64: certainly not nb bits
          }
          return scale * max_index + bias < (int64_t{1} << nb);
        }();
        if (affine_fits
            && (uint64_t{1} << index_w) * static_cast<uint64_t>(demand_w)
                   < static_cast<uint64_t>(cw) * static_cast<uint64_t>(std::max(nb, 1))) {
          std::vector<Abc_Obj_t*> iv(index_w);
          for (int i = 0; i < index_w; ++i) {
            iv[i] = abc_bit(index, i);
          }
          res = arith::build_affine_shr_prefix(ops, av, iv, fill, scale, bias, demand_w);
          if (opts_.verbose) {
            std::print("[pass.abc] region '{}': affine right shift selected ({} output bits, {}-bit index, scale {}, bias {})\n",
                       rb.module_name,
                       demand_w,
                       index_w,
                       scale,
                       bias);
          }
        } else {
          res = arith::build_shr_prefix(ops, av, bv, fill, demand_w);
        }
      }
      // result = low out_w bits of the cw-wide shift. The bit(s) above the
      // magnitude width follow the RESULT's sign, which Verilog takes from the
      // LEFT operand (the amount never counts): the LEC's SRA arm carries
      // `out_signed |= a.is_signed` and sign-extends the W-bit result into a
      // wider consumer/port, and cgen emits `$signed(a) >>> n`, which
      // sign-fills. tolg's bind_result stamps the pin unsigned even for an
      // arithmetic shift, so the "spare" slot is NOT always 0 -- padding it
      // with const0 zero-extended a negative result (abc_mathops __c5: c = -8,
      // n = 0 read 8 instead of 24 on the 5-bit region boundary). Replicate
      // the top kept bit for a signed operand; a logical shift still pads 0.
      Abc_Obj_t* pad = abc_const_bit(false);
      if (a_sign && out_w > 0 && out_w <= static_cast<int>(res.size())) {
        pad = res[out_w - 1];
      }
      for (int b = 0; b < out_bits; ++b) {
        slots[b] = (b < out_w && b < static_cast<int>(res.size())) ? res[b] : pad;
      }
    } else if (op == Ntype_op::Mult) {
      // n-ary product of every input driver (all on pid 0), at width out_bits
      // (the bitwidth-resolved result width). Each operand is sign/zero-extended
      // to out_bits by abc_bit and the running product is kept mod 2^out_bits, so
      // the low out_bits are correct for signed and unsigned operands alike —
      // matching the LEC (fit each operand to W, then BITVECTOR_MULT). A simple
      // single-cycle array multiplier (build_mul) reuses the selected adder for
      // partial-product accumulation. An empty product is 1 (LEC convention).
      std::vector<hhds::Pin_class> ds;
      for (const auto& e : n.inp_edges()) {
        ds.push_back(e.driver);
      }
      int  out_w  = real_width(out_pin);  // result magnitude width (LEC W); product is mod 2^out_w
      int  bs     = opts_.block_size > 0 ? opts_.block_size : arith::default_block_size(out_w);
      auto extend = [&](const hhds::Pin_class& d) {
        std::vector<Abc_Obj_t*> v(out_w);
        for (int i = 0; i < out_w; ++i) {
          v[i] = abc_eff_bit(d, i);  // operand fit to out_w at its effective width (no stray internal spare bit)
        }
        return v;
      };
      std::vector<Abc_Obj_t*> acc;
      if (ds.empty()) {
        acc.assign(out_w, abc_const_bit(false));
        if (out_w > 0) {
          acc[0] = abc_const_bit(true);  // empty product == 1
        }
      } else {
        acc = extend(ds[0]);
        for (size_t k = 1; k < ds.size(); ++k) {
          acc = arith::build_mul(opts_.multiplier, opts_.adder, bs, ops, acc, extend(ds[k]), out_w);
        }
      }
      // low out_w bits are the product; the spare bit(s) above the magnitude
      // width are 0 (an unsigned product is non-negative; a signed product has
      // out_w == bits_of so there are no spare bits to fill).
      for (int b = 0; b < out_bits; ++b) {
        slots[b] = (b < out_w && b < static_cast<int>(acc.size())) ? acc[b] : abc_const_bit(false);
      }
    } else {
      livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
          .msg(
              "pass.abc: cell '{}' in region '{}' has no combinational bit-blast yet "
              "(supported: and/or/xor/not/mux/hotmux/sum/mult/lt/gt/eq/get_mask/set_mask/sext/shl/sra/const; "
              "concat is pure wiring, resolved per demanded bit; div/mod are blackboxed)",
              Ntype::get_name(op),
              rb.module_name)
          .emit();
      unsupported = true;
    }
  }
  if (unsupported) {
    Abc_NtkDelete(manNtk);
    return;
  }
  trace_stage("blast-complete");

  // --- sequential: wire each latch's data-in (D) to the folded next-state ---
  // The native LGraph flop's next state is  reset? rval : (enable? din : Q).
  // Folding enable+reset into the AIG means the reconstructed flop is a plain
  // D-flop (only clock + power-on init reattached), and ABC sees the true
  // next-state function so retiming/sweeping stays sound.
  // enable/reset are single control signals: an N-bit pin asserts on (pin != 0),
  // i.e. the OR-reduction of its bits (matches cgen/yosys reg semantics). Reduce
  // once per flop, not per data bit.
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
      Abc_Obj_t* d = abc_bit(f.din_drv, b);
      if (en_active != nullptr) {
        d = abc_mux(en_active, d, abc_bit(f.q_pin, b));  // (en != 0)? din : Q
      }
      if (rst_active != nullptr) {
        Abc_Obj_t* rval = f.rval_drv.is_invalid() ? abc_const_bit(false) : abc_bit(f.rval_drv, b);
        d               = abc_mux(rst_active, rval, d);  // reset? rval : (...)
      }
      Abc_ObjAddFanin(f.bi[b], d);
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
      // A PO is already a connectivity boundary. An explicit identity node
      // here becomes a real Liberty buffer after mapping; wide shared-Sub
      // inputs then pay one bogus cell per boundary bit (Rob: 523 x 10,260).
      // Give the same source node a uniquely named NET alias instead: ABC's
      // netlist checker requires unique CO net names, but an alias carries no
      // Boolean node and therefore maps to no cell. Read-back pairs POs by
      // creation order.
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

  // --- run the flow: logic -> optimize -> map ---
  auto* frame  = static_cast<Abc_Frame_t*>(pabc_);
  auto* pLogic = Abc_NtkToLogic(manNtk);
  Abc_NtkDelete(manNtk);
  Abc_FrameClearVerifStatus(frame);
  // Regions are independent synthesis jobs, not interactive ABC undo steps.
  // SetCurrentNetwork links the previous (potentially enormous) region as a
  // backup; carrying that network into every later job caused tiny regions to
  // stall after Rob's 10k-bit pack.  Replace deletes the old current network
  // while retaining the parsed Liberty library and command aliases.
  Abc_FrameReplaceCurrentNetwork(frame, pLogic);
  auto flow = (opts_.map_register || opts_.map_memory) ? seq_flow() : comb_flow();
  if (Cmd_CommandExecute(frame, flow.c_str()) != 0) {
    livehd::diag::err("pass.abc", "abc-flow", "internal").msg("ABC flow failed for region '{}': {}", rb.module_name, flow).fatal();
    return;
  }
  trace_stage("flow-complete");

  // --- QoR read-back (2opt-freq A): critical delay/area/gates from the Liberty
  // pin-to-pin data while the flow's result is still a mapped LOGIC network
  // (Abc_NtkDelayTrace requires one; the netlist conversion below is only for
  // the gate read-back). Per-region numbers: paths crossing the region or
  // blackbox boundary are pass.opentimer's job, not scored here.
  {
    Region_qor q;
    q.module      = rb.module_name;
    q.color       = rb.color;
    q.input_nodes = input_nodes;
    q.input_ge    = input_ge;
    for (const auto& bb : bboxes) {
      if (bb.op == Ntype_op::Div || bb.op == Ntype_op::Rem) {
        ++q.div_blackbox;  // unmapped cone: the region score is partial
      }
    }
    if (auto* pMappedLogic = Abc_FrameReadNtk(frame); pMappedLogic != nullptr && Abc_NtkIsMappedLogic(pMappedLogic)) {
      q.delay          = Abc_NtkDelayTrace(pMappedLogic, nullptr, nullptr, 0);
      q.area           = Abc_NtkGetMappedArea(pMappedLogic);
      q.gates          = Abc_NtkNodeNum(pMappedLogic);
      // Worst-arrival REGION output (the delay trace leaves per-node arrivals
      // behind; POs beyond po_order are blackbox-input cuts, not outputs).
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
  auto cell_desc = [&](Mio_Gate_t* g) -> Cell_desc& {
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
  };

  // Select one bit without materializing the one-hot bigint `(1 << b)` used by
  // Get_mask. For a one-bit result, `(bus >> b)` is exactly bit b for signed
  // and unsigned buses alike; stamping the result to one bit discards the
  // arithmetic-fill tail. This keeps a W-bit boundary's selector constants
  // O(W log W) instead of O(W^2) serialized bigint storage.
  auto extract_body_bit = [&](const hhds::Pin_class& bus, int b) {
    auto shift = gu::create_typed_node(*body, Ntype_op::SRA);
    bus.connect_sink(gu::setup_sink_by_name(shift, "a"));
    gu::create_const(*body, *Dlop::create_integer(b)).connect_sink(gu::setup_sink_by_name(shift, "b"));
    auto out = shift.create_driver_pin(0);
    gu::set_bits(out, 1);
    gu::set_unsign(out);
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
    const auto id = static_cast<size_t>(Abc_ObjId(net));
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
      if (!o.abc_bits) {
        continue;
      }
      if (o.bits == 1) {
        br.out_bit[oi][0] = dp;
      } else {
        for (int b = 0; b < o.bits; ++b) {
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
      gu::create_const(*body, gu::hydrate_const(cdrv)).connect_sink(nn.create_sink_pin(pid));
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

  // pass 1b: each mapped gate -> a Sub; map its output net -> Sub output pin
  std::vector<std::pair<hhds::Node_class, Abc_Obj_t*>> gates;
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
    hhds::Node_class sub;
    Abc_Obj_t*       dnet;
  };
  std::vector<Recon_dff> dff_recon;
  bool                   init_dropped = false;  // a concrete power-on init lost to a plain DFF cell
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
      if (gu::is_const_pin(d)) {
        return gu::create_const(*body, gu::hydrate_const(d));
      }
      return {};
    };
    auto region_clk = body_pin_for_src(flops.front().clk_drv);

    // surviving latches, in stable vBoxes order
    std::vector<Abc_Obj_t*> lat;
    Abc_NtkForEachLatch(mapped, pObj, i) { lat.push_back(pObj); }
    int m = static_cast<int>(lat.size());

    // Per-latch source flop, so clock and reset are decided PER FLOP (not region-
    // wide): the crossing creates latches in flops order, one per bit, so when the
    // latch count is preserved (the default flow does not retime) latch k maps to
    // its origin flop. A retime-reshaped count falls back to the first flop.
    int total_bits = 0;
    for (const auto& f : flops) {
      total_bits += f.bits;
    }
    std::vector<const Seq_flop*> latch_owner;
    if (m == total_bits) {
      for (const auto& f : flops) {
        for (int b = 0; b < f.bits; ++b) {
          latch_owner.push_back(&f);
        }
      }
    }
    auto owner_clk = [&](int k) -> hhds::Pin_class {
      return k < static_cast<int>(latch_owner.size()) ? body_pin_for_src(latch_owner[k]->clk_drv) : region_clk;
    };
    // A latch init is a TRUE power-on value only when its source flop has NO
    // reset. With a reset, the init is the reset value — already folded into the D
    // cone — so the flop resets to it and LEC pins reset; a plain DFF cell (power-
    // on X, exactly like the reset flop's own cgen) is then equivalent and the bit
    // can map to a cell. Only a resetless init must keep its native flop.
    auto owner_has_reset = [&](int k) -> bool {
      return k < static_cast<int>(latch_owner.size()) ? !latch_owner[k]->rst_drv.is_invalid() : !flops.front().rst_drv.is_invalid();
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
    if (m == total_bits) {
      int s = 0;
      for (const auto& f : flops) {
        spans.push_back({&f, s});
        s += f.bits;
      }
    }
    // Nothing enforces wire-name uniqueness across a region's registers; two
    // same-named rebuilt flops would cgen as two `reg` declarations of one name.
    absl::flat_hash_map<std::string, int> name_used;
    auto                                  unique_flop_name = [&](const std::string& base) {
      int& n = name_used[base];
      ++n;
      return n == 1 ? base : std::format("{}__dup{}", base, n - 1);
    };
    // Rebuild one native flop covering the latches `idx` (bit 0 first): Q bits
    // feed the mapped logic via net2drv, din is Set_mask-reassembled in pass
    // 2b, and the power-on init is recovered from the latch init values.
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
      // power-on / reset init from the (possibly retimed) latch init values.
      // Build the value MSB->LSB as a binary string so widths past 64 bits stay
      // exact (an int64 accumulator would overflow / be UB).
      bool        any_init = false;
      std::string init_bits(k, '0');  // index 0 = MSB (bit k-1)
      for (int b = 0; b < k; ++b) {
        int v = Abc_LatchInit(lat[idx[b]]);  // 1=zero, 2=one, else dc/none
        if (v == 1 || v == 2) {
          any_init = true;
          if (v == 2) {
            init_bits[k - 1 - b] = '1';
          }
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
      // netlist stays equivalent), only init-less bits become DFF cells.
      auto io           = liberty::create_dff_io(*outlib_, *dff_);
      // resetless power-on init: such a bit must keep a native flop so the
      // value survives
      auto needs_native = [&](int k) -> bool {
        int v = Abc_LatchInit(lat[k]);
        return (v == 1 || v == 2) && !owner_has_reset(k);
      };
      // `owner` names the SOURCE register bit this latch came from (empty when
      // the latch count was reshaped and no correspondence survives). A mapped
      // DFF cell otherwise lands as `g<abcId>_<cell>`, which drops the register
      // name that the post-synthesis LEC's tier-1 state correspondence pairs on
      // — `id_q` then has no counterpart in the netlist and the def can only come
      // back inconclusive (every //bench:*_synth_lec_* target).
      auto map_dff_cell = [&](int k, const std::string& owner = {}) {
        auto* L    = lat[k];
        auto* qnet = Abc_ObjFanout0(Abc_ObjFanout0(L));  // latch -> BO -> Q net
        auto* dnet = Abc_ObjFanin0(Abc_ObjFanin0(L));    // latch <- BI <- D net
        auto  sub  = gu::create_typed_node(*body, Ntype_op::Sub);
        sub.set_subnode(io);
        sub.attr(hhds::attrs::name).set(owner.empty() ? std::format("g{}_{}", Abc_ObjId(L), dff_->name) : unique_flop_name(owner));
        auto q = sub.create_driver_pin(dff_->q_pin);
        gu::set_bits(q, 1);
        gu::set_unsign(q);
        set_net_driver(qnet, q);
        if (auto lclk = owner_clk(k); !lclk.is_invalid()) {
          lclk.connect_sink(sub.create_sink_pin(dff_->clk_pin));
        }
        dff_recon.push_back({sub, dnet});
      };
      auto native_single = [&](int k) {
        init_dropped = true;
        build_native_flop(unique_flop_name(std::format("{}__rinit{}", rb.module_name, Abc_ObjId(lat[k]))), owner_clk(k), {k});
      };
      if (!spans.empty()) {
        for (const auto& sp : spans) {
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
            // init-less register -> per-bit DFF cells; a mixed register
            // (should not occur: init is stamped per register) degrades to
            // per-bit handling, never to a dropped init
            for (int b = 0; b < sp.f->bits; ++b) {
              int k = sp.start + b;
              // Per-bit name under the source register: a 1-bit register keeps
              // its plain name, a wider one indexes (`id_q[0]`) — the spelling a
              // hand-flattened design uses, which canon_flop_name already folds.
              if (needs_native(k)) {
                native_single(k);
              } else {
                map_dff_cell(k, sp.f->bits == 1 ? sp.f->root : std::format("{}_{}", sp.f->root, b));
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

  // pass 2b (seq): wire each reconstructed flop's din from the body driver that
  // feeds its latch D net (now resolvable: PIs in 1a, gates in 1b/2). Multi-bit
  // din is reassembled with a Set_mask concat, mirroring the PO reassembly.
  for (auto& rf : recon) {
    int                          k = rf.bits;
    std::vector<hhds::Pin_class> dbits(k);
    for (int b = 0; b < k; ++b) {
      auto driver = get_net_driver(rf.dnet[b]);
      dbits[b]    = !driver.is_invalid() ? driver : const0_pin();
    }
    auto din_sink = gu::setup_sink_by_name(rf.node, "din");
    if (k == 1) {
      dbits[0].connect_sink(din_sink);
      continue;
    }
    hhds::Pin_class acc = gu::create_const(*body, *Dlop::create_integer(0));
    for (int b = 0; b < k; ++b) {
      auto sm = gu::create_typed_node(*body, Ntype_op::Set_mask);
      acc.connect_sink(gu::setup_sink_by_name(sm, "a"));
      gu::create_const(*body, *bit_mask(b)).connect_sink(gu::setup_sink_by_name(sm, "mask"));
      dbits[b].connect_sink(gu::setup_sink_by_name(sm, "value"));
      acc = sm.create_driver_pin(0);
      gu::set_bits(acc, b + 1);
      gu::set_unsign(acc);
    }
    gu::set_bits(acc, k);
    acc.connect_sink(din_sink);
  }

  // pass 2b (register=true): wire each mapped DFF Sub's D pin from its latch's
  // data-in net (each DFF cell is 1-bit, so no Set_mask reassembly is needed).
  for (auto& rd : dff_recon) {
    auto d = get_net_driver(rd.dnet);
    if (d.is_invalid()) {
      d = const0_pin();
    }
    d.connect_sink(rd.sub.create_sink_pin(dff_->d_pin));
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

  // Reassemble a vector of LSB-first one-bit drivers as one canonical Concat
  // (whose lanes are MSB-first). The old Set_mask chain created W nodes and W
  // progressively wider mask constants for a W-bit boundary, turning a wide
  // region interface into quadratic graph-construction work.
  hhds::Pin_class concat_width_one;
  auto            assemble_bits = [&](std::vector<hhds::Pin_class>& dbit, bool sign, hhds::SourceId sid = hhds::SourceId_invalid) {
    I(!dbit.empty());
    if (dbit.size() == 1) {
      return dbit.front();
    }
    if (concat_width_one.is_invalid()) {
      concat_width_one = gu::create_const(*body, *Dlop::create_integer(1));
    }
    auto concat = gu::create_typed_node(*body, Ntype_op::Concat);
    if (sid != hhds::SourceId_invalid) {
      concat.attr(hhds::attrs::srcid).set(sid);
    }
    // Port IDs still encode MSB-first lanes, but create them in descending
    // order. HHDS's per-node pin list is sorted; ascending creation rescans the
    // growing list for every pin and makes a W-bit Concat O(W^2).
    for (size_t b = 0; b < dbit.size(); ++b) {
      auto data_pid = static_cast<hhds::Port_id>(2 * (dbit.size() - 1 - b));
      concat_width_one.connect_sink(concat.create_sink_pin(data_pid + 1));
      dbit[b].connect_sink(concat.create_sink_pin(data_pid));
    }
    auto out = concat.create_driver_pin(0);
    gu::set_bits(out, static_cast<int>(dbit.size()));
    sign ? gu::set_sign(out) : gu::set_unsign(out);
    return out;
  };

  // pass 3b: wire each rebuilt blackbox node's combinational inputs from the
  // captured PO drivers (multi-bit reassembled with one Concat).
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> reassembled_bbox_input;
  for (size_t bx = 0; bx < bboxes.size(); ++bx) {
    auto& bb = bboxes[bx];
    auto& br = bbox_recon[bx];
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      int   w    = bb.ins[ii].bits;
      auto  sink = br.node.create_sink_pin(bb.ins[ii].port_id);
      auto& dbit = br.in_bit[ii];
      if (auto it = reassembled_bbox_input.find(bb.ins[ii].drv); it != reassembled_bbox_input.end()) {
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
        reassembled_bbox_input.emplace(bb.ins[ii].drv, dbit[0]);
        continue;
      }
      auto acc = assemble_bits(dbit, bb.ins[ii].sign);
      acc.connect_sink(sink);
      reassembled_bbox_input.emplace(bb.ins[ii].drv, acc);
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

  {
    // One QoR line per region (stdout is the step log under lhd). qor_.back()
    // is this region's row: pushed above, and every later exit path is fatal.
    const auto& q = qor_.back();
    std::string crit;
    if (!q.crit_output.empty()) {
      crit = std::format("  critical output '{}'", q.crit_output);
      if (!q.crit_src.empty()) {
        crit += std::format(" @ {}", q.crit_src);
      }
    }
    std::print("[pass.abc] region '{}': {} gates, area {:.2f}, delay {:.2f}, {:.0f} ms{}\n",
               rb.module_name,
               q.gates,
               q.area,
               q.delay,
               since(),
               crit);
  }
  trace_stage("readback-complete");

  // rb.body now holds the complete mapped netlist: snapshot it (and the pre-abc
  // body built above) into the cache so the next run's identical region is a
  // whole-module copy, not an ABC run. A region whose pre-body could not be
  // rebuilt (pre_g == nullptr) is uncacheable and simply re-maps next time.
  if (incr_ != nullptr && pre_g != nullptr) {
    incr_->store(rb, *rb.pre_lib, rb.pre_name, qor_.back(), recipe, outlib_);
  }
  qor_.back().cache = "miss";
  qor_.back().ms    = since();
  Abc_NtkDelete(mapped);
  // &get/&dc4/&dch/&nf leave GIA managers in the global frame even after
  // &put.  A large region then poisons the next tiny job (Rob's 438-node
  // NewRobDeqPtrWrapper stalled for minutes after a 10k-bit pack, versus
  // 0.35 s in a fresh frame).  Clear all per-network/GIA workspace while
  // retaining the frame's parsed Liberty library and installed aliases.
  Abc_FrameDeleteAllNetworks(frame);
}

void report_stats(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view top, const Map_options& opts) {
  std::print("pass.abc stats: top='{}' library='{}' register={} memory={}\n",
             top,
             opts.library,
             opts.map_register,
             opts.map_memory);
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
      const auto ge      = gu::mappable_ge_weight(node);
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
        rs.register_bits += static_cast<uint64_t>(std::max(gu::bits_of(node.create_driver_pin(0)), 1));
      }
    }
  }
  std::vector<std::pair<std::string_view, const Op_stats*>> ranked;
  ranked.reserve(by_op.size());
  for (const auto& [name, stats] : by_op) {
    ranked.emplace_back(name, &stats);
  }
  std::ranges::sort(ranked, [](const auto& lhs, const auto& rhs) { return lhs.second->ge > rhs.second->ge; });
  std::print("  operation GE: {} nodes, {} total MAPPABLE GE across {} def(s)\n", total_nodes, total_ge, graphs.size());
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
