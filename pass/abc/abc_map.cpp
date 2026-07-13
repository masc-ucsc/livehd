// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Region body <-> ABC translation for pass.abc (task 2a-abc). Each colored
// region (handed over by pass.partition's decomposition seam) is bit-blasted
// into an ABC AIG netlist, optimized + technology-mapped by ABC against a
// Liberty library, and read back as a netlist of 1-bit blackbox Sub cells named
// after the Liberty cells. The bit-blast boundary (multi-bit module IO <-> 1-bit
// ABC PI/PO) is handled with Get_mask bit-selects on inputs and a Set_mask
// concat on outputs, exactly the modern equivalent of the old Pick/Join path.

#include "abc_map.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <print>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
constexpr std::string_view kCombFlow = "strash; &get -n; &fraig -x; &put; &get -n; &dch -f; &nf {D}; &put";

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
// `--set pass.abc.flow="strash; &get -n; &fraig -x; &put; dretime; &get -n;
// &dch -f; &nf {D}; &put"` (the read-back stays robust to reshaped latches).
constexpr std::string_view kSeqFlow = "strash; &get -n; &fraig -x; &put; &get -n; &dch -f; &nf {D}; &put";

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
    "alias b balance", "alias rw rewrite", "alias rwz rewrite -z", "alias rf refactor", "alias rfz refactor -z",
    "alias rs resub", "alias rsz resub -z", "alias st strash", "alias f fraig", "alias dret dretime", "alias ret retime",
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

// One-hot mask value (only bit `b` set) for Get_mask/Set_mask bit-select/concat,
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

bool Mapper::start() {
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
  auto cmd = std::string{"read_lib -s "} + opts_.library;
  if (Cmd_CommandExecute(frame, cmd.c_str()) != 0) {
    livehd::diag::err("pass.abc", "read-lib", "unsupported")
        .msg("ABC could not read the Liberty library '{}'", opts_.library)
        .fatal();
    return false;
  }
  lib_loaded_ = true;
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
    int                    color   = 0;
    const auto*            b       = key.data();
    const auto*            e       = key.data() + key.size();
    auto [p, ec]                   = std::from_chars(b, e, color);
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
  auto drv = rb.outputs[po].src_driver;
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

void Mapper::map_region(const livehd::partition::Region_body& rb) {
  // Per-region option overrides (2opt-freq C): overlay onto opts_ for the
  // duration of this region (every helper below reads opts_), restored on
  // every exit path.
  const Map_options saved_opts = opts_;
  struct Opts_restore {
    Map_options*       dst;
    const Map_options* src;
    ~Opts_restore() { *dst = *src; }
  } opts_restore{&opts_, &saved_opts};
  apply_region_overrides(rb);

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

  // --- Formal-assume don't-cares (pass.formal -> ABC, task 2f-formal) ---
  // An `fproperty` assume Sub in this region carries a 1-bit `cond` that holds on
  // every reachable input; feeding it lets ABC treat violating minterms as
  // don't-cares. use_proven_assume (default) collects assumes pass.formal PROVED;
  // use_all_assume additionally collects DECLARED (unproven) ones. The cond
  // drivers gathered here are the ones the flow is permitted to exploit (the EXDC
  // don't-care network construction that hands them to ABC's mfs/fraig is the
  // follow-on; collecting + gating them per flag is the interface).
  std::vector<hhds::Pin_class> assume_constraints;
  if (opts_.use_proven_assume || opts_.use_all_assume) {
    for (auto n : rb.src->forward_class()) {
      if (!region.contains(n) || graph_util::type_op_of(n) != Ntype_op::Sub) {
        continue;
      }
      auto sio = n.get_subnode_io();
      if (!sio || sio->get_name() != graph_util::fproperty_module_name) {
        continue;
      }
      const bool is_proven   = graph_util::proven_of(n) == graph_util::kFormalAssume;
      const bool is_declared = graph_util::runtime_check_of(n) == graph_util::kFormalAssume;
      if ((is_proven && opts_.use_proven_assume) || (is_declared && opts_.use_all_assume)) {
        if (auto cond = graph_util::get_driver_of_sink_name(n, "cond"); !cond.is_invalid()) {
          assume_constraints.push_back(cond);
        }
      }
    }
    if (opts_.verbose && !assume_constraints.empty()) {
      std::print(stderr, "pass.abc: region '{}' has {} assume constraint(s) usable as don't-cares\n", rb.module_name,
                 assume_constraints.size());
    }
  }
  (void)assume_constraints;  // EXDC don't-care network construction is the follow-on (see 2f-formal)

  // --- ABC gate constructors (each returns the new gate's output net) ---
  auto new_net = [&](Abc_Obj_t* node) {
    auto* net = Abc_NtkCreateNet(manNtk);
    Abc_ObjAddFanin(net, node);
    return net;
  };
  Abc_Obj_t* const1     = nullptr;
  auto       abc_const1 = [&]() {
    if (const1 == nullptr) {
      auto* node  = Abc_NtkCreateNode(manNtk);
      node->pData = Hop_ManConst1(manFunc);
      const1      = new_net(node);
    }
    return const1;
  };
  auto abc_not = [&](Abc_Obj_t* a) {
    auto* node  = Abc_NtkCreateNode(manNtk);
    node->pData = Hop_Not(Hop_IthVar(manFunc, 0));
    Abc_ObjAddFanin(node, a);
    return new_net(node);
  };
  auto abc_bin = [&](Abc_Obj_t* a, Abc_Obj_t* b, char kind) {
    auto* node  = Abc_NtkCreateNode(manNtk);
    node->pData = kind == '&' ? Hop_CreateAnd(manFunc, 2) : kind == '|' ? Hop_CreateOr(manFunc, 2) : Hop_CreateExor(manFunc, 2);
    Abc_ObjAddFanin(node, a);
    Abc_ObjAddFanin(node, b);
    return new_net(node);
  };
  auto abc_const_bit = [&](bool v) { return v ? abc_const1() : abc_not(abc_const1()); };
  // 2:1 mux on ABC nets: sel ? t : f  ==  (sel & t) | (~sel & f).
  auto abc_mux       = [&](Abc_Obj_t* sel, Abc_Obj_t* t, Abc_Obj_t* f) {
    return abc_bin(abc_bin(sel, t, '&'), abc_bin(abc_not(sel), f, '&'), '|');
  };

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
    // A region-internal node not yet materialized (should not happen in topo
    // order) or an unexpected boundary: emit a constant 0 so the netlist stays
    // structurally valid; correctness is guarded by the unsupported-cell diag.
    auto* net  = abc_const_bit(false);
    slots[eff] = net;
    return net;
  };

  // LiveHD's magnitude+1 width convention (mirrors pass.lec's real_width): an
  // unsigned net reserves its top bit as an always-0 sign slot, so its true
  // operating width is bits-1; a signed net uses all its bits. Arithmetic that
  // truncates/sign-fills (mult, shift-right) must be sized at THIS width, not the
  // raw bits_of, or it diverges from the LEC on the spare bit when a region input
  // is wider than its value range (e.g. the to-positive-signed Get_mask that
  // feeds a shift/multiply makes a u8 a 9-bit signed port; the LEC drops the spare
  // bit, so the bit-blast must produce a 0 there too). Add/and/or/xor/compare are
  // bit-position-wise and already agree with the LEC's spare bit, so they keep
  // bits_of. Always >= 1.
  auto real_width = [&](const hhds::Pin_class& p) -> int {
    int b = gu::bits_of(p);
    if (b <= 0) {
      return 1;
    }
    return gu::is_unsign(p) ? std::max(1, b - 1) : b;
  };

  // The "effective" width at which the LEC reads an OPERAND driver: a region
  // INPUT port carries its full literal bus width (real_width_io == bits_of,
  // every bit meaningful — e.g. the 9-bit-unsigned to-positive Get_mask port that
  // feeds a shift), but an INTERNAL net is read at its magnitude width
  // (real_width = bits_of-1 for unsigned, dropping the always-0 spare top slot).
  // Reading an internal unsigned operand at raw bits_of would expose a spare slot
  // an upstream op may have driven to 1 (a wrapped Sum, a Mux, ...), which the LEC
  // drops — so shift/multiply operands must be read at this effective width.
  absl::flat_hash_set<hhds::Pin_class> region_input_drivers;
  for (const auto& p : rb.inputs) {
    region_input_drivers.insert(p.src_driver);
  }
  auto eff_width = [&](const hhds::Pin_class& d) -> int {
    if (region_input_drivers.contains(d)) {
      return std::max(1, gu::bits_of(d));
    }
    if (gu::is_const_pin(d)) {
      // A constant driver usually carries NO bits attribute (bits_of == 0), so
      // real_width would clamp it to 1 bit and a width-sensitive consumer
      // (mult/sra) would read e.g. 342 as its bit 0 only — collapsing the whole
      // cone to a constant (the const-mult miscompile). Size a constant from
      // its VALUE: get_bits() is the minimal two's-complement width, which is
      // exactly how the LEC reads the literal.
      return std::max(1, static_cast<int>(gu::hydrate_const(d).get_bits()));
    }
    return real_width(d);
  };
  // Bit i of an operand as the LEC sees it: the real bit below its effective
  // width, then sign/zero extension above it (NOT the raw stored spare slot).
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

  // --- region inputs -> per-bit ABC PIs (creation order == readback order) ---
  std::vector<std::pair<size_t, int>> pi_order;  // PI index -> (input port, bit)
  for (size_t pi = 0; pi < rb.inputs.size(); ++pi) {
    const auto& port = rb.inputs[pi];
    int         w    = port.bits == 0 ? 1 : port.bits;
    for (int b = 0; b < w; ++b) {
      auto* obj = Abc_NtkCreatePi(manNtk);
      auto* net = Abc_NtkCreateNet(manNtk);
      auto  nm  = std::format("{}_b{}", port.name, b);
      Abc_ObjAssignName(net, const_cast<char*>(nm.c_str()), nullptr);
      Abc_ObjAddFanin(net, obj);
      bitnet[port.src_driver][b] = net;
      pi_order.emplace_back(pi, b);
    }
  }

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
  std::vector<Seq_flop> flops;
  if (opts_.seq) {
    for (auto n : rb.src->forward_class()) {
      if (!region.contains(n)) {
        continue;
      }
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
    std::vector<std::pair<int, hhds::Pin_class>> const_ins;  // (port_id, const driver)
  };
  std::vector<Bbox>                      bboxes;
  std::vector<std::tuple<int, int, int>> bbox_pi;  // appended PI -> (bbox, out, bit)
  for (auto n : rb.src->forward_class()) {
    if (!region.contains(n)) {
      continue;
    }
    auto op = gu::type_op_of(n);
    if (op != Ntype_op::Sub && op != Ntype_op::Memory && op != Ntype_op::Div) {
      continue;
    }
    if (op == Ntype_op::Div) {
      // Division (and modulo, which lowers to a - (a/b)*b, hence a Div) is not
      // bit-blasted: a synthesizable divider is large and out of scope. The Div
      // node is kept native as a blackbox boundary (its output feeds the AIG as a
      // fresh PI, its inputs are cut as POs), exactly like a Sub/Memory instance,
      // and rebuilt unchanged on read-back. Warn so the user knows this cone is
      // not technology-mapped.
      livehd::diag::warn("pass.abc", "div-blackbox", "unsupported")
          .msg("pass.abc: division/modulo in region '{}' is blackboxed (kept as a native div, not technology-mapped)",
               rb.module_name)
          .emit();
    }
    Bbox bb;
    bb.node                                          = n;
    bb.op                                            = op;
    int                                       bb_idx = static_cast<int>(bboxes.size());
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
      int oi = static_cast<int>(bb.outs.size());
      bb.outs.push_back({op_pin, pid, w, !gu::is_unsign(op_pin)});
      auto& slots = bitnet[op_pin];
      for (int b = 0; b < w; ++b) {
        auto* obj = Abc_NtkCreatePi(manNtk);
        auto* net = Abc_NtkCreateNet(manNtk);
        Abc_ObjAddFanin(net, obj);
        slots[b] = net;
        bbox_pi.emplace_back(bb_idx, oi, b);
      }
    }
    // inputs: const-driven recreated directly; comb-driven cut as POs
    for (const auto& e : n.inp_edges()) {
      int pid = static_cast<int>(e.sink.get_port_id());
      if (gu::is_const_pin(e.driver)) {
        bb.const_ins.emplace_back(pid, e.driver);
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

  // --- bit-blast each region node in topological order ---
  bool unsupported = false;
  for (auto n : rb.src->forward_class()) {
    if (!region.contains(n)) {
      continue;
    }
    auto op = gu::type_op_of(n);
    if (op == Ntype_op::Sub || op == Ntype_op::Memory || op == Ntype_op::Div) {
      continue;  // blackbox boundary (Sub instance / memory / divider) -- handled separately
    }
    if (opts_.seq && gu::is_type_flop(n)) {
      continue;  // sequential register -- crosses into ABC as a latch (handled above)
    }
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
      hhds::Pin_class                           sel;
      absl::btree_map<int, hhds::Pin_class> data;  // pid-1 (value) -> driver; ordered so the OR-tree fed to ABC is deterministic
      int                                       max_v = -1;
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
        auto             mask   = gu::hydrate_const(m_drv);
        bool             neg    = mask.is_negative();
        int              mb     = mask.get_bits();
        int              pmb    = neg ? mb - 1 : mb;
        int              a_bits = gu::bits_of(a_drv);
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
      // out[i] = mask-selected ? value[next] : a[i].
      auto a_drv = gu::get_driver_of_sink_name(n, "a");
      auto m_drv = gu::get_driver_of_sink_name(n, "mask");
      auto v_drv = gu::get_driver_of_sink_name(n, "value");
      if (!gu::is_const_pin(m_drv)) {
        livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
            .msg("pass.abc: set_mask with a non-constant mask in region '{}' is not supported", rb.module_name)
            .emit();
        unsupported = true;
      } else {
        auto mask      = gu::hydrate_const(m_drv);
        bool neg       = mask.is_negative();
        int  mb        = mask.get_bits();
        int  pmb       = neg ? mb - 1 : mb;
        int  value_pos = 0;
        for (int b = 0; b < out_bits; ++b) {
          bool from_value;
          if (b < pmb) {
            bool mbit  = mask.bit_test(b);
            from_value = neg ? !mbit : mbit;
          } else {
            from_value = neg;
          }
          slots[b] = from_value ? abc_bit(v_drv, value_pos++) : abc_bit(a_drv, b);
        }
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
      bool a_sign  = !gu::is_unsign(a_d);
      int  a_width = eff_width(a_d);                      // operand width as the LEC reads it (port=bits_of, internal=real_width)
      int  out_w   = real_width(out_pin);                 // result magnitude width (LEC W)
      int  cw      = std::max(a_width, std::max(out_w, 1));  // shift at the wider of the two
      std::vector<Abc_Obj_t*> av(cw);
      for (int i = 0; i < cw; ++i) {
        av[i] = abc_eff_bit(a_d, i);  // a, sign/zero-extended (past its effective width) to the shift width cw
      }
      Abc_Obj_t*              fill = a_sign ? av[cw - 1] : abc_const_bit(false);  // sign bit (arith) or 0 (logical)
      std::vector<Abc_Obj_t*> res;                                               // cw-wide shifted value
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
          res.resize(cw);
          for (int i = 0; i < cw; ++i) {
            res[i] = (amt < cw && i + amt < cw) ? av[static_cast<int>(i + amt)] : fill;  // amt >= cw => all fill
          }
        }
      } else {
        int                     nb = std::min(eff_width(b_d), cw);  // amount bits at/above its eff width or cw are 0 (LEC fit)
        std::vector<Abc_Obj_t*> bv(nb);
        for (int i = 0; i < nb; ++i) {
          bv[i] = abc_bit(b_d, i);  // unsigned shift count (i < eff width, so the real bit)
        }
        res = arith::build_shr(ops, av, bv, fill);
      }
      // result = low out_w bits of the cw-wide shift; the spare bit(s) above the
      // magnitude width are 0 (an unsigned result is non-negative). A signed
      // result has out_w == bits_of so there are no spare bits to fill.
      for (int b = 0; b < out_bits; ++b) {
        slots[b] = (b < out_w && b < static_cast<int>(res.size())) ? res[b] : abc_const_bit(false);
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
              "div/mod are blackboxed)",
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
  std::vector<std::pair<size_t, int>> po_order;  // PO index -> (output port, bit)
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    const auto& port = rb.outputs[po];
    int         w    = port.bits == 0 ? 1 : port.bits;
    for (int b = 0; b < w; ++b) {
      auto* value = abc_bit(port.src_driver, b);
      auto* buf   = Abc_NtkCreateNode(manNtk);
      buf->pData  = Hop_IthVar(manFunc, 0);
      Abc_ObjAddFanin(buf, value);
      auto* onet = new_net(buf);
      auto  nm   = std::format("{}_b{}", port.name, b);
      Abc_ObjAssignName(onet, const_cast<char*>(nm.c_str()), nullptr);
      auto* obj = Abc_NtkCreatePo(manNtk);
      Abc_ObjAddFanin(obj, onet);
      po_order.emplace_back(po, b);
    }
  }

  // --- blackbox combinational inputs -> per-bit ABC POs (appended after the
  // region outputs so the region-output read-back stays index-aligned) ---
  std::vector<std::tuple<int, int, int>> bbox_po;  // appended PO -> (bbox, in, bit)
  for (size_t bi = 0; bi < bboxes.size(); ++bi) {
    auto& bb = bboxes[bi];
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      const auto& in = bb.ins[ii];
      for (int b = 0; b < in.bits; ++b) {
        auto* value = abc_bit(in.drv, b);
        auto* buf   = Abc_NtkCreateNode(manNtk);
        buf->pData  = Hop_IthVar(manFunc, 0);
        Abc_ObjAddFanin(buf, value);
        auto* onet = new_net(buf);
        auto* obj  = Abc_NtkCreatePo(manNtk);
        Abc_ObjAddFanin(obj, onet);
        bbox_po.emplace_back(static_cast<int>(bi), static_cast<int>(ii), b);
      }
    }
  }

  Abc_NtkFinalizeRead(manNtk);
  if (!Abc_NtkCheck(manNtk)) {
    livehd::diag::err("pass.abc", "abc-check", "internal").msg("ABC netlist check failed for region '{}'", rb.module_name).fatal();
    Abc_NtkDelete(manNtk);
    return;
  }

  // --- run the flow: logic -> optimize -> map ---
  auto* frame  = static_cast<Abc_Frame_t*>(pabc_);
  auto* pLogic = Abc_NtkToLogic(manNtk);
  Abc_NtkDelete(manNtk);
  Abc_FrameClearVerifStatus(frame);
  Abc_FrameSetCurrentNetwork(frame, pLogic);
  auto flow = opts_.seq ? seq_flow() : comb_flow();
  if (Cmd_CommandExecute(frame, flow.c_str()) != 0) {
    livehd::diag::err("pass.abc", "abc-flow", "internal").msg("ABC flow failed for region '{}': {}", rb.module_name, flow).fatal();
    return;
  }

  // --- QoR read-back (2opt-freq A): critical delay/area/gates from the Liberty
  // pin-to-pin data while the flow's result is still a mapped LOGIC network
  // (Abc_NtkDelayTrace requires one; the netlist conversion below is only for
  // the gate read-back). Per-region numbers: paths crossing the region or
  // blackbox boundary are pass.opentimer's job, not scored here.
  {
    Region_qor q;
    q.module = rb.module_name;
    q.color  = rb.color;
    for (const auto& bb : bboxes) {
      if (bb.op == Ntype_op::Div) {
        ++q.div_blackbox;  // unmapped cone: the region score is partial
      }
    }
    if (auto* pMappedLogic = Abc_FrameReadNtk(frame); pMappedLogic != nullptr && Abc_NtkIsMappedLogic(pMappedLogic)) {
      q.delay = Abc_NtkDelayTrace(pMappedLogic, nullptr, nullptr, 0);
      q.area  = Abc_NtkGetMappedArea(pMappedLogic);
      q.gates = Abc_NtkNodeNum(pMappedLogic);
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

  // --- read back: each mapped gate -> a 1-bit blackbox Sub in the body ---
  auto* body = rb.body;

  // Source-map carry-through (task 2a-abc): ABC's strash/dch destroy per-node
  // provenance, so re-mint each output port's original driver srcid into the
  // body locator. Computed up front so the output-concat glue can carry it and
  // the per-gate cone attribution below can reuse it.
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
      po_srcid[po] = body->source_locator().import_from(rb.src->source_locator(), a.get());
    }
  }

  // find-or-declare a 1-bit blackbox cell def (Liberty pins) in the out library
  auto blackbox_io = [&](Mio_Gate_t* g) -> std::shared_ptr<hhds::GraphIO> {
    std::string cell{Mio_GateReadName(g)};
    if (auto existing = outlib_->find_io(cell)) {
      return existing;
    }
    auto          io  = outlib_->create_io(cell);
    hhds::Port_id pid = 1;
    for (auto* pin = Mio_GateReadPins(g); pin != nullptr; pin = Mio_PinReadNext(pin)) {
      io->add_input(Mio_PinReadName(pin), pid++);
      io->set_bits(Mio_PinReadName(pin), 1);
    }
    io->add_output(Mio_GateReadOutName(g), pid++);
    io->set_bits(Mio_GateReadOutName(g), 1);
    return io;
  };

  // lazily build bit b of a body input pin (Get_mask bit-select; pin itself if 1-bit)
  std::vector<std::vector<hhds::Pin_class>> in_bit(rb.inputs.size());
  auto                                      input_bit = [&](size_t port_idx, int b) -> hhds::Pin_class {
    auto&       cache = in_bit[port_idx];
    const auto& port  = rb.inputs[port_idx];
    int         w     = port.bits == 0 ? 1 : port.bits;
    if (static_cast<int>(cache.size()) < w) {
      cache.resize(w);
    }
    if (!cache[b].is_invalid()) {
      return cache[b];
    }
    auto ipin = body->get_input_pin(port.name);
    if (w == 1) {
      cache[b] = ipin;
      return ipin;
    }
    auto gm = gu::create_typed_node(*body, Ntype_op::Get_mask);
    ipin.connect_sink(gu::setup_sink_by_name(gm, "a"));
    gu::create_const(*body, *bit_mask(b)).connect_sink(gu::setup_sink_by_name(gm, "mask"));
    auto d = gm.create_driver_pin(0);
    gu::set_bits(d, 1);
    gu::set_unsign(d);
    cache[b] = d;
    return d;
  };

  absl::flat_hash_map<Abc_Obj_t*, hhds::Pin_class> net2drv;
  int                                              i    = 0;
  Abc_Obj_t*                                       pObj = nullptr;

  // pass 1.bbox: rebuild each blackbox node (Sub instance / memory) natively.
  // Its output pins drive the boundary PIs (mapped in pass 1a); its inputs are
  // wired in pass 2c once their driving cones resolve. Const inputs are wired now.
  struct Bbox_recon {
    hhds::Node_class                          node;
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
        if (auto out_child = outlib_->find_io(child->get_name())) {
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
    br.out_bit.resize(bb.outs.size());
    for (size_t oi = 0; oi < bb.outs.size(); ++oi) {
      const auto& o  = bb.outs[oi];
      auto        dp = nn.create_driver_pin(o.port_id);
      gu::set_bits(dp, o.bits);
      if (!o.sign) {
        gu::set_unsign(dp);
      }
      br.out_bit[oi].resize(o.bits);
      if (o.bits == 1) {
        br.out_bit[oi][0] = dp;
      } else {
        for (int b = 0; b < o.bits; ++b) {
          auto gm = gu::create_typed_node(*body, Ntype_op::Get_mask);
          dp.connect_sink(gu::setup_sink_by_name(gm, "a"));
          gu::create_const(*body, *bit_mask(b)).connect_sink(gu::setup_sink_by_name(gm, "mask"));
          auto d = gm.create_driver_pin(0);
          gu::set_bits(d, 1);
          gu::set_unsign(d);
          br.out_bit[oi][b] = d;
        }
      }
    }
    for (const auto& [pid, cdrv] : bb.const_ins) {
      gu::create_const(*body, gu::hydrate_const(cdrv)).connect_sink(nn.create_sink_pin(pid));
    }
    br.in_bit.resize(bb.ins.size());
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      br.in_bit[ii].assign(bb.ins[ii].bits, hhds::Pin_class{});
    }
  }
  if (unsupported) {
    Abc_NtkDelete(mapped);
    return;
  }

  // pass 1a: PI nets -> body input bit drivers (match by creation order — ABC
  // preserves CI/CO order across the flow, more robust than name parsing).
  Abc_NtkForEachPi(mapped, pObj, i) {
    if (i < static_cast<int>(pi_order.size())) {
      net2drv[Abc_ObjFanout0(pObj)] = input_bit(pi_order[i].first, pi_order[i].second);
    } else if (int j = i - static_cast<int>(pi_order.size()); j < static_cast<int>(bbox_pi.size())) {
      auto [bx, oi, b]              = bbox_pi[j];
      net2drv[Abc_ObjFanout0(pObj)] = bbox_recon[bx].out_bit[oi][b];
    }
  }

  // pass 1b: each mapped gate -> a Sub; map its output net -> Sub output pin
  std::vector<std::pair<hhds::Node_class, Abc_Obj_t*>> gates;
  Abc_NtkForEachNode(mapped, pObj, i) {
    auto* g = static_cast<Mio_Gate_t*>(pObj->pData);
    if (g == nullptr) {
      // A mapped node without Mio data cannot be read back — skipping it
      // would silently collapse its fanout cone to const0 (seen with
      // multi-output supergates before read_lib -s). Never miscompile.
      livehd::diag::err("pass.abc", "abc-readback", "internal")
          .msg("region '{}': mapped node {} carries no Mio gate — unreadable mapping (multi-output cell?)", rb.module_name,
               Abc_ObjId(pObj))
          .fatal();
      Abc_NtkDelete(mapped);
      return;
    }
    auto io  = blackbox_io(g);
    auto sub = gu::create_typed_node(*body, Ntype_op::Sub);
    sub.set_subnode(io);
    sub.attr(hhds::attrs::name).set(std::format("g{}_{}", Abc_ObjId(pObj), Mio_GateReadName(g)));
    auto outpin = sub.create_driver_pin(Mio_GateReadOutName(g));
    gu::set_bits(outpin, 1);
    gu::set_unsign(outpin);
    net2drv[Abc_ObjFanout0(pObj)] = outpin;
    gates.emplace_back(sub, pObj);
  }

  // pass 1c (seq): each ABC latch -> a native LGraph Flop. Flops are never
  // mapped to library DFFs (locked design decision): the latch only carried the
  // register across ABC so it could optimize/retime the surrounding logic. The
  // latch output net (Q) is mapped into net2drv so the comb fanins/outputs read
  // the flop's Q; the latch input net (D) is recorded and wired in pass 2b (its
  // driving gate is created in pass 2). Reassembly: a single-root region (one
  // distinct register name) collapses every surviving latch into one named
  // flop; a multi-register region with a 1:1 latch count rebuilds one flop per
  // original register; otherwise (retiming reshaped the count) each surviving
  // latch becomes its own deterministically-named 1-bit flop.
  struct Recon_flop {
    hhds::Node_class        node;
    int                     bits = 0;
    std::vector<Abc_Obj_t*> dnet;  // per-bit latch data-in net (wired in pass 2b)
  };
  std::vector<Recon_flop> recon;
  if (opts_.seq && !flops.empty()) {
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

    absl::flat_hash_set<std::string> roots;
    for (const auto& f : flops) {
      roots.insert(f.root);
    }

    struct Group {
      std::string      name;
      std::vector<int> idx;  // indices into `lat`
      hhds::Pin_class  clk;
    };
    // Single-root region (one register name, possibly multi-bit): collapse every
    // surviving latch into one named flop. `pass color synth` splits each def at
    // its registers, so regions are single-root in practice and this is the path
    // the seq tests exercise. Any multi-register region falls back to per-latch
    // 1-bit flops -- always LEC-correct regardless of how retiming reshaped or
    // reordered the latches (each latch is faithfully its own 1-bit register; no
    // cross-register order assumption).
    std::vector<Group> groups;
    if (roots.size() == 1) {
      Group g{flops.front().root, {}, region_clk};
      for (int k = 0; k < m; ++k) {
        g.idx.push_back(k);
      }
      groups.push_back(std::move(g));
    } else {
      for (int k = 0; k < m; ++k) {
        groups.push_back(Group{std::format("{}__r{}", rb.module_name, k), {k}, region_clk});
      }
    }

    for (auto& g : groups) {
      int  k = static_cast<int>(g.idx.size());
      auto F = gu::create_typed_node(*body, Ntype_op::Flop);
      F.attr(hhds::attrs::name).set(g.name);
      auto Fq = F.create_driver_pin(0);
      gu::set_bits(Fq, k);
      gu::set_unsign(Fq);
      if (!g.clk.is_invalid()) {
        g.clk.connect_sink(gu::setup_sink_by_name(F, "clock_pin"));
      }
      // power-on / reset init from the (possibly retimed) latch init values.
      // Build the value MSB->LSB as a binary string so widths past 64 bits stay
      // exact (an int64 accumulator would overflow / be UB).
      bool        any_init = false;
      std::string init_bits(k, '0');  // index 0 = MSB (bit k-1)
      for (int b = 0; b < k; ++b) {
        int v = Abc_LatchInit(lat[g.idx[b]]);  // 1=zero, 2=one, else dc/none
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
        auto*           L    = lat[g.idx[b]];
        auto*           qnet = Abc_ObjFanout0(Abc_ObjFanout0(L));  // latch -> BO -> Q net
        auto*           dnet = Abc_ObjFanin0(Abc_ObjFanin0(L));    // latch <- BI <- D net
        hhds::Pin_class qd;
        if (k == 1) {
          qd = Fq;
        } else {
          auto gm = gu::create_typed_node(*body, Ntype_op::Get_mask);
          Fq.connect_sink(gu::setup_sink_by_name(gm, "a"));
          gu::create_const(*body, *bit_mask(b)).connect_sink(gu::setup_sink_by_name(gm, "mask"));
          qd = gm.create_driver_pin(0);
          gu::set_bits(qd, 1);
          gu::set_unsign(qd);
        }
        net2drv[qnet] = qd;
        rf.dnet.push_back(dnet);
      }
      recon.push_back(std::move(rf));
    }
  }

  // pass 2: wire each Sub's fanins (fanin k <-> Liberty pin k)
  auto const0_pin = [&]() { return gu::create_const(*body, *Dlop::create_integer(0)); };
  for (auto& [sub, obj] : gates) {
    auto*                    g = static_cast<Mio_Gate_t*>(obj->pData);
    std::vector<std::string> pins;
    for (auto* pin = Mio_GateReadPins(g); pin != nullptr; pin = Mio_PinReadNext(pin)) {
      pins.emplace_back(Mio_PinReadName(pin));
    }
    int        k   = 0;
    Abc_Obj_t* fin = nullptr;
    Abc_ObjForEachFanin(obj, fin, k) {
      if (k >= static_cast<int>(pins.size())) {
        break;
      }
      auto spin = sub.create_sink_pin(pins[k]);
      // No set_bits on this cell-input SINK: `bits` is a driver-pin property (the
      // 1-bit width lives on the gate's GraphIO port + the 1-bit driver net).
      auto it = net2drv.find(fin);
      if (it != net2drv.end()) {
        it->second.connect_sink(spin);
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
      auto it  = net2drv.find(rf.dnet[b]);
      dbits[b] = it != net2drv.end() ? it->second : const0_pin();
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

  // pass 3: POs -> reassemble multi-bit outputs (Set_mask concat). Match by
  // creation order (po_order), consistent with the PI readback.
  std::vector<std::vector<hhds::Pin_class>> out_bits(rb.outputs.size());
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    int w = rb.outputs[po].bits == 0 ? 1 : rb.outputs[po].bits;
    out_bits[po].resize(w);
  }
  Abc_NtkForEachPo(mapped, pObj, i) {
    auto* net = Abc_ObjFanin0(pObj);
    auto  dit = net2drv.find(net);
    auto  drv = dit != net2drv.end() ? dit->second : const0_pin();
    if (i < static_cast<int>(po_order.size())) {
      out_bits[po_order[i].first][po_order[i].second] = drv;
    } else if (int j = i - static_cast<int>(po_order.size()); j < static_cast<int>(bbox_po.size())) {
      auto [bx, ii, b]             = bbox_po[j];
      bbox_recon[bx].in_bit[ii][b] = drv;  // wired to the recon node sink below
    }
  }

  // pass 3b: wire each rebuilt blackbox node's combinational inputs from the
  // captured PO drivers (multi-bit reassembled with a Set_mask concat).
  for (size_t bx = 0; bx < bboxes.size(); ++bx) {
    auto& bb = bboxes[bx];
    auto& br = bbox_recon[bx];
    for (size_t ii = 0; ii < bb.ins.size(); ++ii) {
      int   w    = bb.ins[ii].bits;
      auto  sink = br.node.create_sink_pin(bb.ins[ii].port_id);
      auto& dbit = br.in_bit[ii];
      for (int b = 0; b < w; ++b) {
        if (dbit[b].is_invalid()) {
          dbit[b] = const0_pin();
        }
      }
      if (w == 1 && !bb.ins[ii].sign) {
        dbit[0].connect_sink(sink);  // unsigned 1-bit: drive the sink directly
        continue;
      }
      hhds::Pin_class acc = gu::create_const(*body, *Dlop::create_integer(0));
      for (int b = 0; b < w; ++b) {
        auto sm = gu::create_typed_node(*body, Ntype_op::Set_mask);
        acc.connect_sink(gu::setup_sink_by_name(sm, "a"));
        gu::create_const(*body, *bit_mask(b)).connect_sink(gu::setup_sink_by_name(sm, "mask"));
        dbit[b].connect_sink(gu::setup_sink_by_name(sm, "value"));
        acc = sm.create_driver_pin(0);
        gu::set_bits(acc, b + 1);
        gu::set_unsign(acc);
      }
      gu::set_bits(acc, w);
      // Preserve the operand's signedness on the reassembled value. For a Div the
      // LEC fit()s each operand by its sign (SDIV/UDIV sign-extend vs zero-extend),
      // so a signed operand narrower than the divider's width must stay signed or
      // ref/impl diverge. Harmless for Sub/Memory (their LEC compare never extends
      // an input up to a larger width). The intermediate accs stay unsigned; only
      // the final value that reaches the sink carries the sign.
      bb.ins[ii].sign ? gu::set_sign(acc) : gu::set_unsign(acc);
      acc.connect_sink(sink);
    }
  }

  for (size_t po = 0; po < rb.outputs.size(); ++po) {
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
    // acc = const0; acc = Set_mask(acc, 1<<b, bit_b)  for each bit
    hhds::Pin_class acc = gu::create_const(*body, *Dlop::create_integer(0));
    for (int b = 0; b < w; ++b) {
      auto sm = gu::create_typed_node(*body, Ntype_op::Set_mask);
      if (po_srcid[po] != hhds::SourceId_invalid) {
        sm.attr(hhds::attrs::srcid).set(po_srcid[po]);
      }
      acc.connect_sink(gu::setup_sink_by_name(sm, "a"));
      gu::create_const(*body, *bit_mask(b)).connect_sink(gu::setup_sink_by_name(sm, "mask"));
      bits[b].connect_sink(gu::setup_sink_by_name(sm, "value"));
      acc = sm.create_driver_pin(0);
      gu::set_bits(acc, b + 1);
      gu::set_unsign(acc);
    }
    gu::set_bits(acc, w);
    acc.connect_sink(opin);
  }

  // --- source-map carry-through: stamp each mapped gate with the srcid of the
  // original output cone(s) it feeds. Backward-DFS from every PO over the mapped
  // netlist; a gate reached from a single output gets that output's srcid, one
  // fanning out to several gets combine() (order = ascending output index, so
  // the lowest-index output is the primary anchor). ABC's optimization is lossy
  // so exact per-gate lineage is unrecoverable — the output cone is the
  // faithful-yet-cheap approximation. ---
  {
    absl::flat_hash_map<Abc_Obj_t*, hhds::Node_class> node2sub;
    for (auto& [sub, obj] : gates) {
      node2sub[obj] = sub;
    }
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
    if (opts_.seq && !flops.empty()) {
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
          auto* bi_obj = Abc_ObjFanin0(lat_objs[k]);                          // latch -> BI
          auto* dnet   = bi_obj != nullptr ? Abc_ObjFanin0(bi_obj) : nullptr;  // BI -> net
          auto* drv    = dnet != nullptr ? Abc_ObjFanin0(dnet) : nullptr;      // net -> driving node
          if (drv == nullptr || !Abc_ObjIsNode(drv)) {
            continue;
          }
          port_roots.push_back({drv});
          cone_srcid.push_back(body->source_locator().import_from(rb.src->source_locator(), sid_attr.get()));
        }
      }
    }
    // per output, DFS its fanin cone and record the port on each reached gate
    absl::flat_hash_map<Abc_Obj_t*, std::vector<int>> cone_ports;
    for (size_t po = 0; po < port_roots.size(); ++po) {
      if (cone_srcid[po] == hhds::SourceId_invalid) {
        continue;  // no provenance to attribute this cone with
      }
      absl::flat_hash_set<Abc_Obj_t*> visited;
      std::vector<Abc_Obj_t*>         stack = port_roots[po];
      while (!stack.empty()) {
        auto* g = stack.back();
        stack.pop_back();
        if (!visited.insert(g).second) {
          continue;
        }
        if (node2sub.contains(g)) {
          cone_ports[g].push_back(static_cast<int>(po));
        }
        Abc_Obj_t* fin = nullptr;
        int        k   = 0;
        Abc_ObjForEachFanin(g, fin, k) {
          auto* d = Abc_ObjFanin0(fin);  // fanin net -> its driving node
          if (d != nullptr && Abc_ObjIsNode(d) && !visited.contains(d)) {
            stack.push_back(d);
          }
        }
      }
    }
    for (auto& [g, ports] : cone_ports) {
      std::vector<hhds::SourceId> ids;
      for (int po : ports) {
        auto id = cone_srcid[static_cast<size_t>(po)];
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
          ids.push_back(id);  // distinct outputs may share a srcid; keep it unique
        }
      }
      if (ids.empty()) {
        continue;
      }
      auto sid
          = ids.size() == 1 ? ids.front() : body->source_locator().combine(std::span<const hhds::SourceId>(ids.data(), ids.size()));
      node2sub[g].attr(hhds::attrs::srcid).set(sid);
    }
  }

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
    std::print("[pass.abc] region '{}': {} gates, area {:.2f}, delay {:.2f}{}\n", rb.module_name, q.gates, q.area, q.delay, crit);
  }
  Abc_NtkDelete(mapped);
}

void report_stats(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view top, const Map_options& opts) {
  (void)graphs;
  std::print("pass.abc stats: top='{}' library='{}' seq={}\n", top, opts.library, opts.seq);
  std::print("  (run with --emit-dir lg:DIR to produce the mapped netlist library)\n");
}

}  // namespace livehd::abc
