// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_region.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <tuple>
#include <stdexcept>
#include <unordered_map>

#include "abc_timing.hpp"
#include "lut_cover.hpp"
#include "diag.hpp"
#include "host_mem.hpp"
#include "json_util.hpp"
#include "metrics.hpp"
#include "resource_budget.hpp"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "map/mio/mio.h"
}
// clang-format on

namespace livehd::synth {
namespace {
struct Delete_network {
  void operator()(Abc_Ntk_t* n) const {
    if (n) {
      Abc_NtkDelete(n);
    }
  }
};
using Network = std::unique_ptr<Abc_Ntk_t, Delete_network>;

struct Phase_effort {
  std::chrono::steady_clock::time_point started;
  Resource_observation                  memory;
  double                                elapsed_ms = 0;
  void                                  start() { started = std::chrono::steady_clock::now(); }
  void        stop() { elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); }
  std::string json() const {
    return std::format(
        R"({{"elapsed_ms":{},"memory_scope":"parent_plus_active_synthesis_worker","samples":{},"parent_samples_unavailable":{},"sampled_peak_bytes":{},"worker_samples":{},"worker_samples_unavailable":{},"sampled_worker_peak_bytes":{}}})",
        elapsed_ms,
        memory.samples,
        memory.parent_unavailable,
        memory.peak_bytes,
        memory.worker_samples,
        memory.worker_unavailable,
        memory.worker_peak_bytes);
  }
};

Logic_network import_aig(Abc_Ntk_t* aig, const std::function<bool()>& admit) {
  Logic_network   result;
  std::vector<Id> ids(static_cast<size_t>(Abc_NtkObjNumMax(aig)), std::numeric_limits<Id>::max());
  for (int i = 0; i < Abc_NtkCiNum(aig); ++i) {
    ids[Abc_ObjId(Abc_NtkCi(aig, i))] = result.add_source();
  }
  ids[Abc_ObjId(Abc_AigConst1(aig))] = result.add_function({}, Truth_table(0, true));
  auto* order                        = Abc_NtkDfs(aig, 0);
  for (int i = 0; i < Vec_PtrSize(order); ++i) {
    if (i % 1024 == 0 && !admit()) {
      Vec_PtrFree(order);
      return {};
    }
    auto*       obj = static_cast<Abc_Obj_t*>(Vec_PtrEntry(order, i));
    Truth_table table(2);
    for (uint32_t x = 0; x < 4; ++x) {
      const auto a = ((x & 1) != 0) != (Abc_ObjFaninC0(obj) != 0);
      const auto b = ((x & 2) != 0) != (Abc_ObjFaninC1(obj) != 0);
      table.set(x, a && b);
    }
    ids[Abc_ObjId(obj)]
        = result.add_function({ids[Abc_ObjId(Abc_ObjFanin0(obj))], ids[Abc_ObjId(Abc_ObjFanin1(obj))]}, std::move(table));
  }
  Vec_PtrFree(order);
  for (int i = 0; i < Abc_NtkCoNum(aig); ++i) {
    auto* co  = Abc_NtkCo(aig, i);
    auto  out = ids[Abc_ObjId(Abc_ObjFanin0(co))];
    if (Abc_ObjFaninC0(co)) {
      Truth_table inv(1);
      inv.set(0, true);
      out = result.add_function({out}, std::move(inv));
    }
    result.outputs.push_back(out);
  }
  return result;
}

}  // namespace

Logic_network import_tape(const livehd::abc::Blast_tape& tape, bool xor_nodes, const std::function<bool()>& admit) {
  using Op = livehd::abc::Blast_tape::Op;
  // A literal is (node id << 1) | complemented. Structural hashing, constant
  // folding and complement absorption stand in for strash: a complemented
  // fanin folds into its consumer's table, and only a complemented output
  // gets an explicit inverter.
  using Lit = uint64_t;
  Logic_network work;
  const auto    sources = tape.pis.size() + tape.latches.size();
  for (size_t i = 0; i < sources; ++i) {
    work.add_source();
  }
  const Lit                                                    one  = Lit{work.add_function({}, Truth_table(0, true))} << 1;
  const Lit                                                    zero = one | 1;
  std::unordered_map<Lit, Id>                                  and_nodes, xor_nodes_by_key;
  const auto                                                   key = [](Lit a, Lit b) { return (a << 32) | b; };
  const auto                                                   gate = [&](Lit a, Lit b, bool is_xor) -> Lit {
    auto& nodes = is_xor ? xor_nodes_by_key : and_nodes;
    if (auto it = nodes.find(key(a, b)); it != nodes.end()) {
      return Lit{it->second} << 1;
    }
    Truth_table table(2);
    for (uint32_t x = 0; x < 4; ++x) {
      const bool va = ((x & 1) != 0) != ((a & 1) != 0);
      const bool vb = ((x & 2) != 0) != ((b & 1) != 0);
      table.set(x, is_xor ? va != vb : va && vb);
    }
    const auto id = work.add_function({static_cast<Id>(a >> 1), static_cast<Id>(b >> 1)}, std::move(table));
    nodes.emplace(key(a, b), id);
    return Lit{id} << 1;
  };
  const auto and_ = [&](Lit a, Lit b) -> Lit {
    if (a == zero || b == zero || a == (b ^ 1)) {
      return zero;
    }
    if (a == one || a == b) {
      return b;
    }
    if (b == one) {
      return a;
    }
    return gate(std::min(a, b), std::max(a, b), false);
  };
  const auto xor_ = [&](Lit a, Lit b) -> Lit {
    const Lit phase = (a ^ b) & 1;
    a &= ~Lit{1};
    b &= ~Lit{1};
    if (a == one) {
      return b ^ 1 ^ phase;
    }
    if (b == one) {
      return a ^ 1 ^ phase;
    }
    if (a == b) {
      return zero ^ phase;
    }
    if (a > b) {
      std::swap(a, b);
    }
    if (xor_nodes) {
      return gate(a, b, true) ^ phase;
    }
    return (and_(and_(a, b ^ 1) ^ 1, and_(a ^ 1, b) ^ 1) ^ 1) ^ phase;
  };
  std::vector<Lit> lit(tape.nodes.size(), zero);
  for (size_t i = 0; i < tape.nodes.size(); ++i) {
    if (i % 1024 == 0 && !admit()) {
      return {};
    }
    const auto& e = tape.nodes[i];
    switch (e.op) {
      case Op::const0: lit[i] = zero; break;
      case Op::const1: lit[i] = one; break;
      case Op::inv: lit[i] = lit[e.a] ^ 1; break;
      case Op::and2: lit[i] = and_(lit[e.a], lit[e.b]); break;
      case Op::or2: lit[i] = and_(lit[e.a] ^ 1, lit[e.b] ^ 1) ^ 1; break;
      case Op::xor2: lit[i] = xor_(lit[e.a], lit[e.b]); break;
      case Op::pi: lit[i] = Lit{e.a} << 1; break;
      case Op::latch: lit[i] = Lit{tape.pis.size() + e.a} << 1; break;
      case Op::po: break;
    }
  }
  std::vector<Lit> outs;
  outs.reserve(tape.pos.size() + tape.latches.size());
  for (const auto& po : tape.pos) {
    outs.push_back(lit[po.bit]);
  }
  for (const auto& latch : tape.latches) {
    if (latch.d == livehd::abc::Blast_tape::kNone) {
      return {};
    }
    outs.push_back(lit[latch.d]);
  }
  // Keep every source and the constant; drop gates no output reaches.
  std::vector<bool> live(work.nodes.size(), false);
  for (size_t i = 0; i <= sources; ++i) {
    live[i] = true;
  }
  for (const auto out : outs) {
    live[out >> 1] = true;
  }
  for (size_t i = work.nodes.size(); i-- > sources + 1;) {
    if (live[i]) {
      for (const auto in : work.nodes[i].inputs) {
        live[in] = true;
      }
    }
  }
  Logic_network   result;
  std::vector<Id> ids(work.nodes.size(), std::numeric_limits<Id>::max());
  for (size_t i = 0; i < work.nodes.size(); ++i) {
    if (!live[i]) {
      continue;
    }
    if (work.nodes[i].source) {
      ids[i] = result.add_source();
      continue;
    }
    std::vector<Id> inputs;
    for (const auto in : work.nodes[i].inputs) {
      inputs.push_back(ids[in]);
    }
    ids[i] = result.add_function(std::move(inputs), work.nodes[i].table);
  }
  std::unordered_map<Id, Id> inverters;
  for (const auto out : outs) {
    auto id = ids[out >> 1];
    if (out & 1) {
      auto [it, fresh] = inverters.try_emplace(id, 0);
      if (fresh) {
        Truth_table inv(1);
        inv.set(0, true);
        it->second = result.add_function({id}, std::move(inv));
      }
      id = it->second;
    }
    result.outputs.push_back(id);
  }
  return result;
}

namespace {
Network export_mapped(Abc_Ntk_t* original, const Mapped_network& net) {
  // StartFrom copies every latch, init value and CI/CO in order. The new
  // combinational functions only replace their drivers; no state is optimized.
  Network                 result(Abc_NtkStartFrom(original, ABC_NTK_LOGIC, ABC_FUNC_MAP));
  std::vector<Abc_Obj_t*> wires;
  for (auto source : net.source_origins) {
    if (source >= static_cast<uint32_t>(Abc_NtkCiNum(result.get()))) {
      return {};
    }
    wires.push_back(Abc_NtkCi(result.get(), static_cast<int>(source)));
  }
  auto* library = static_cast<Mio_Library_t*>(Abc_FrameReadLibGen());
  for (const auto& cell : net.cells) {
    auto  name = cell.name;
    auto  port = cell.output_pin;
    auto* gate = Mio_LibraryReadGateByName(library, name.data(), port.data());
    if (!gate || cell.cell_sop != Mio_GateReadSop(gate)) {
      return {};
    }
    auto* node  = Abc_NtkCreateNode(result.get());
    node->pData = gate;
    for (auto in : cell.inputs) {
      if (in >= wires.size()) {
        return {};
      }
      Abc_ObjAddFanin(node, wires[in]);
    }
    wires.push_back(node);
  }
  if (net.outputs.size() != static_cast<size_t>(Abc_NtkCoNum(result.get()))) {
    return {};
  }
  for (uint32_t j = 0; j < net.outputs.size(); ++j) {
    if (net.outputs[j] >= wires.size()) {
      return {};
    }
    Abc_ObjAddFanin(Abc_NtkCo(result.get(), static_cast<int>(j)), wires[net.outputs[j]]);
  }
  return Abc_NtkCheck(result.get()) ? std::move(result) : Network{};
}
}  // namespace

namespace {
// The cover as an unmapped SOP logic network in `original`'s PI/PO/latch
// skeleton: one node per LUT with its minimum SOP (ABC factors it when it
// strashes), a wire LUT as its leaf or an inverter, constants as constants.
Network export_logic(Abc_Ntk_t* original, const Logic_network& source, const Cover_result& cover) {
  Network                 result(Abc_NtkStartFrom(original, ABC_NTK_LOGIC, ABC_FUNC_SOP));
  std::vector<Abc_Obj_t*> objs(source.nodes.size(), nullptr);
  uint32_t                ci = 0;
  for (size_t id = 0; id < source.nodes.size(); ++id) {
    if (source.nodes[id].source) {
      if (ci >= static_cast<uint32_t>(Abc_NtkCiNum(result.get()))) {
        return {};
      }
      objs[id] = Abc_NtkCi(result.get(), static_cast<int>(ci++));
    }
  }
  auto* manager = static_cast<Mem_Flex_t*>(result->pManFunc);
  for (const auto& lut : cover.luts) {
    const auto& f = lut.fn;
    if (f.constant) {
      const bool one = !f.form.cubes.empty() != f.complemented;
      objs[lut.root] = one ? Abc_NtkCreateNodeConst1(result.get()) : Abc_NtkCreateNodeConst0(result.get());
      continue;
    }
    for (auto leaf : lut.leaves) {
      if (!objs[leaf]) {
        return {};
      }
    }
    if (f.alias) {
      const bool inverted = (f.form.cubes.size() == 1 && f.form.cubes.front().ones == 0) != f.complemented;
      objs[lut.root] = inverted ? Abc_NtkCreateNodeInv(result.get(), objs[lut.leaves.front()]) : objs[lut.leaves.front()];
      continue;
    }
    auto* node = Abc_NtkCreateNode(result.get());
    for (auto leaf : lut.leaves) {
      Abc_ObjAddFanin(node, objs[leaf]);
    }
    std::string sop;
    for (const auto& cube : f.form.cubes) {
      for (uint32_t j = 0; j < lut.leaves.size(); ++j) {
        sop += ((cube.care >> j) & 1) ? (((cube.ones >> j) & 1) ? '1' : '0') : '-';
      }
      sop += f.complemented ? " 0\n" : " 1\n";
    }
    if (sop.empty()) {
      return {};
    }
    node->pData    = Abc_SopRegister(manager, sop.c_str());
    objs[lut.root] = node;
  }
  if (source.outputs.size() != static_cast<size_t>(Abc_NtkCoNum(result.get()))) {
    return {};
  }
  for (size_t j = 0; j < source.outputs.size(); ++j) {
    auto* driver = objs[source.outputs[j]];
    if (!driver) {
      return {};
    }
    Abc_ObjAddFanin(Abc_NtkCo(result.get(), static_cast<int>(j)), driver);
  }
  return Abc_NtkCheck(result.get()) ? std::move(result) : Network{};
}
}  // namespace

std::optional<Source_boundary> abc_source_boundary(void* network, const std::function<bool()>& admission) {
  auto* net = static_cast<Abc_Ntk_t*>(network);
  if (!net || Abc_NtkCiNum(net) + Abc_NtkCoNum(net) + Abc_NtkBoxNum(net) > 200000) {
    return {};
  }
  Source_boundary                   result;
  std::unordered_map<int, uint32_t> inputs, outputs;
  size_t                            bytes = 0, names = 0;
  const auto                        name = [&](Abc_Obj_t* obj) -> std::optional<std::string> {
    if (names++ % 1024 == 0 && admission && !admission()) {
      return {};
    }
    const char* text = Abc_ObjName(obj);
    if (!text) {
      return {};
    }
    const auto length  = strnlen(text, 65537);
    bytes             += length;
    if (length > 65536 || bytes > 16 * 1024 * 1024) {
      return {};
    }
    return std::string(text, length);
  };
  for (int i = 0; i < Abc_NtkCiNum(net); ++i) {
    auto* obj   = Abc_NtkCi(net, i);
    auto  label = name(obj);
    if (!label) {
      return {};
    }
    inputs.emplace(Abc_ObjId(obj), i);
    result.inputs.push_back({std::move(*label), Abc_ObjIsPi(obj) ? "primary" : "opaque", -1});
  }
  for (int i = 0; i < Abc_NtkCoNum(net); ++i) {
    auto* obj   = Abc_NtkCo(net, i);
    auto  label = name(obj);
    if (!label) {
      return {};
    }
    outputs.emplace(Abc_ObjId(obj), i);
    result.outputs.push_back({std::move(*label), Abc_ObjIsPo(obj) ? "primary" : "opaque", -1});
  }
  Abc_Obj_t* latch;
  int        index;
  Abc_NtkForEachLatch(net, latch, index) {
    if (Abc_ObjFaninNum(latch) != 1 || Abc_ObjFanoutNum(latch) != 1) {
      return {};
    }
    auto ci    = inputs.find(Abc_ObjId(Abc_ObjFanout0(latch)));
    auto co    = outputs.find(Abc_ObjId(Abc_ObjFanin0(latch)));
    auto label = name(latch);
    if (ci == inputs.end() || co == outputs.end() || !label || result.inputs[ci->second].state != -1
        || result.outputs[co->second].state != -1) {
      return {};
    }
    if (!Abc_LatchIsInitNone(latch) && !Abc_LatchIsInit0(latch) && !Abc_LatchIsInit1(latch) && !Abc_LatchIsInitDc(latch)) {
      return {};
    }
    const auto state = static_cast<int64_t>(result.states.size());
    const auto init  = Abc_LatchIsInit0(latch)    ? "zero"
                       : Abc_LatchIsInit1(latch)  ? "one"
                       : Abc_LatchIsInitDc(latch) ? "dont_care"
                                                  : "unspecified";
    result.states.push_back({std::move(*label), init, co->second, ci->second});
    result.inputs[ci->second].kind   = "state";
    result.inputs[ci->second].state  = state;
    result.outputs[co->second].kind  = "state";
    result.outputs[co->second].state = state;
  }
  return result;
}

namespace {
// The optimizer's objective on one network: unate functions (twins included),
// total literal occurrences, total ports. Source inverters are free rails.
struct Unate_cost {
  uint64_t functions = 0, twins = 0, literals = 0, ports = 0, source_inverters = 0;
  auto     key() const { return std::tuple{functions, literals, ports}; }
};
}  // namespace

namespace {
// An XOR source node is formable by the fan-in fallback only when every
// recipe admits its 4-literal, 2-series form on 2 inputs.
bool tape_xor_nodes(const Search_options& search) {
  return std::ranges::all_of(search.recipes, [](const Recipe& r) { return r.support >= 2 && r.literals >= 4 && r.series >= 2; });
}
}  // namespace

std::string map_abc_region(void* opaque_frame, void* opaque_original, const livehd::abc::Blast_tape* tape, std::string_view region,
                           const livehd::abc::Map_options& mapping, const Search_options& search, float budget_ps,
                           livehd::abc::Map_options::Alternative_resources resources, Tmap_backend& backend,
                           Witness_archive& witnesses) {
  const auto           start = std::chrono::steady_clock::now();
  Resource_budget      effort;
  Resource_observation observed;
  effort.time_limit_ms = mapping.time_budget_ms;
  effort.entry_bytes   = resources.entry_footprint_bytes;
  if (!mapping.allow_oversize) {
    effort.growth_limit_bytes  = livehd::cost::budget_bytes(mapping.memory_budget_mb);
    effort.process_limit_bytes = livehd::cost::configured_budget_bytes();
  }
  const auto sample = [&](std::optional<uint64_t> child = {}) {
    return observed.sample(livehd::cost::process_footprint_bytes(), child);
  };
  const auto since = [&] {
    return resources.elapsed_ms + std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  };
  // Admission samples the process footprint (a syscall). The search and every
  // tmap call ask often; re-sample at most every 2 ms.
  auto       last_admission = std::chrono::steady_clock::time_point{};
  bool       admitted_last  = true;
  const auto admit          = [&] {
    const auto now = std::chrono::steady_clock::now();
    if (admitted_last && now - last_admission < std::chrono::milliseconds(2)) {
      return true;
    }
    last_admission = now;
    admitted_last  = effort.admit(since(), sample());
    return admitted_last;
  };
  auto*      frame    = static_cast<Abc_Frame_t*>(opaque_frame);
  auto*      original = static_cast<Abc_Ntk_t*>(opaque_original);
  // Each unate function is technology-mapped on its own, for area and without a
  // timing environment: identical functions then share one cached mapping. The
  // stitched region still gets pass.abc's fanout buffering and gate sizing, and
  // the partition-boundary re-size, like any mapped region.
  const bool timed = abc_has_timing();

  std::string    status = "abc_fallback", reason, source_metrics = "null", variant_name = "none";
  Witness_record source_witness{-1, "not_constructed"};
  Unate_cost     selected_cost;
  uint32_t       depth = 0, max_support = 0, max_literals = 0, max_series = 0;
  double         area = -1, delay_ps = -1, unate_ms = 0, tmap_ms = 0;
  // pass.synth.split statistics (zero in the default cover mode).
  Split_result   split;
  Split_mapping  split_mapping;
  // Cover-mode statistics (zero in split mode) and the optional ABC reference.
  Cover_result    cover;
  Cover_reference reference;
  double          reference_ms = 0;
  // The source network comes straight from the blast tape: the blaster's own
  // gates, never optimized by ABC. Without a tape (a caller holding only an
  // ABC logic network) it is imported through strash.
  Logic_network                  source;
  std::optional<Source_boundary> boundary;
  bool                           imported = false;
  if (!admit()) {
    reason = effort.reason;
  } else if (tape) {
    if (tape->nodes.size() > search.max_nodes) {
      reason = "source node limit";
    } else {
      source = import_tape(*tape, tape_xor_nodes(search), admit);
      if (source.nodes.empty() || source.outputs.size() != static_cast<size_t>(Abc_NtkCoNum(original))) {
        reason = effort.reason.empty() ? "tape import incomplete" : effort.reason;
      } else if (source.nodes.size() > search.max_nodes) {
        reason = "source node limit";
      } else {
        // The copy's CI/CO order is the tape's (PIs, then latches).
        boundary = abc_source_boundary(original, admit);
        imported = true;
      }
    }
  } else if (static_cast<uint64_t>(Abc_NtkObjNum(original)) > search.max_nodes) {
    reason = "source node limit";
  } else {
    Network aig(Abc_NtkStrash(original, 0, 1, 0));
    if (!aig || static_cast<uint64_t>(Abc_NtkObjNum(aig.get())) > search.max_nodes) {
      reason = "AIG node limit";
    } else {
      source = import_aig(aig.get(), admit);
      if (source.nodes.empty() || source.outputs.size() != static_cast<size_t>(Abc_NtkCoNum(aig.get()))) {
        reason = effort.reason.empty() ? "AIG import incomplete" : effort.reason;
      } else {
        boundary = abc_source_boundary(aig.get(), admit);
        imported = true;
      }
    }
  }
  if (imported && search.split) {
    // Per-cone bounded split: at most 3 gates per cone, else its top 2 gates
    // over a remainder that ABC only technology-maps. No unate witness is
    // archived for such a region; `lhd lec` checks the stitched netlist.
    source_metrics           = source_metrics_json(source);
    source_witness           = witnesses.skip("split_mode");
    variant_name             = "split";
    auto guarded_search      = search;
    guarded_search.admission = admit;
    const auto unate_start   = std::chrono::steady_clock::now();
    split                    = split_cones(source, search.recipes.front(), guarded_search);
    unate_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - unate_start).count();
    if (split.status == Status::invalid) {
      livehd::diag::err("pass.synth", "invalid-decomposition", "internal")
          .msg("split cone search failed in region '{}': {}", region, split.reason)
          .fatal();
      return {};
    }
    if (split.status != Status::feasible) {
      reason = split.reason.empty() ? effort.reason : split.reason;
    } else {
      for (const auto& gate : split.gates) {
        ++selected_cost.functions;
        selected_cost.literals += gate.form.literals;
        max_support             = std::max<uint32_t>(max_support, static_cast<uint32_t>(gate.leaves.size()));
        max_literals            = std::max(max_literals, gate.form.literals);
        max_series              = std::max(max_series, gate.form.series);
      }
      Mapping_request defaults;
      defaults.library   = mapping.library;
      defaults.admission = admit;
      if (mapping.time_budget_ms) {
        defaults.remaining_ms     = [&] { return double(mapping.time_budget_ms) - since(); };
        defaults.worker_admission = [&](uint64_t child_bytes) { return effort.admit(since(), sample(child_bytes)); };
      }
      defaults.required_ps = -1;
      Block_request block;
      block.library         = mapping.library;
      block.admission       = admit;
      block.proof_conflicts = defaults.proof_conflicts;
      const auto tmap_start = std::chrono::steady_clock::now();
      if (admit()) {
        split_mapping = map_split(source, split, backend, defaults, block);
      } else {
        split_mapping.network = {.status = Map_status::exhausted, .reason = effort.reason};
      }
      tmap_ms          = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tmap_start).count();
      const auto& cells = split_mapping.network;
      if (cells.status == Map_status::mismatch || cells.status == Map_status::invalid) {
        livehd::diag::err("pass.synth", "invalid-fragment", "internal")
            .msg("split tmap defect in region '{}': {}", region, cells.reason)
            .fatal();
        return {};
      }
      if (cells.status != Map_status::mapped) {
        reason = std::format("tmap: {}", cells.reason);
      } else {
        Network candidate = export_mapped(original, cells);
        if (!candidate) {
          livehd::diag::err("pass.synth", "invalid-stitch", "internal")
              .msg("split mapped network stitching failed in region '{}'", region)
              .fatal();
          return {};
        }
        area = Abc_NtkGetMappedArea(candidate.get());
        if (timed) {
          delay_ps = abc_timing_qor(candidate.get()).delay_ps;
        }
        Abc_FrameReplaceCurrentNetwork(frame, candidate.release());
        status = "unate";
      }
    }
  } else if (imported
             && (search.abc_mode == Search_options::Abc_mode::only
                 || (!search.cover_memories && region.find("cgen_memory") != std::string_view::npos))) {
    // No cover: pass.abc maps the original region logic with its own flow.
    status       = "abc_only";
    variant_name = "only";
  } else if (imported
             && (search.abc_mode == Search_options::Abc_mode::source || search.abc_mode == Search_options::Abc_mode::source_tmap)) {
    // Control: the imported source network, one SOP node per source node,
    // through the same export as `opt`, then the pass.abc flow.
    Cover_result passthrough;
    Budget       local{std::numeric_limits<uint64_t>::max() / 4};
    for (Id id = 0; id < source.nodes.size(); ++id) {
      const auto& node = source.nodes[id];
      if (node.source) {
        continue;
      }
      passthrough.luts.push_back({id, node.inputs, node.table, function_cost(node.table, search.recipes.front(), search.cover_cost, local), 0});
    }
    Network logic = export_logic(original, source, passthrough);
    if (!logic) {
      livehd::diag::err("pass.synth", "invalid-stitch", "internal")
          .msg("source logic network export failed in region '{}'", region)
          .fatal();
      return {};
    }
    Abc_FrameReplaceCurrentNetwork(frame, logic.release());
    status       = "abc_source";
    variant_name = "source";
    if (search.abc_mode == Search_options::Abc_mode::source_tmap) {
      const auto nf = mapping.delay.empty() ? std::string{"&nf"} : std::format("&nf -D {}", mapping.delay);
      for (const auto* command : {"strash", "&get -n", nf.c_str(), "&put -o"}) {
        if (Cmd_CommandExecute(frame, command) != 0) {
          livehd::diag::err("pass.synth", "abc-tmap", "internal")
              .msg("ABC '{}' failed on the source network of region '{}'", command, region)
              .fatal();
          return {};
        }
      }
      variant_name = "source_tmap";
    }
  } else if (imported) {
    // The LUT cover: domino gates and static (non-unate) LUTs minimizing the
    // transistor proxy; each LUT is technology-mapped on its own. No unate
    // witness is archived; `lhd lec` checks the stitched netlist.
    source_metrics           = source_metrics_json(source);
    source_witness           = witnesses.skip("lut_cover");
    variant_name             = "cover";
    auto guarded_search      = search;
    guarded_search.admission = admit;
    const auto unate_start   = std::chrono::steady_clock::now();
    cover                    = lut_cover(source, guarded_search);
    unate_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - unate_start).count();
    if (cover.status == Status::invalid) {
      livehd::diag::err("pass.synth", "invalid-decomposition", "internal")
          .msg("LUT cover failed in region '{}': {}", region, cover.reason)
          .fatal();
      return {};
    }
    if (search.reference && admit()) {
      const auto ref_start = std::chrono::steady_clock::now();
      reference = classify_luts(backend.lut_reference(source, search.recipes.front().support, search.reference_choices), search.recipes.front(),
                                search.cover_cost);
      reference_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ref_start).count();
    }
    if (cover.status != Status::feasible) {
      reason = cover.reason.empty() ? effort.reason : cover.reason;
    } else if (search.abc_mode != Search_options::Abc_mode::gate) {
      // Hand the cover network to ABC: technology mapping only (tmap), or the
      // unmapped network for pass.abc's own optimize-and-map flow (opt).
      Network logic = export_logic(original, source, cover);
      if (!logic) {
        livehd::diag::err("pass.synth", "invalid-stitch", "internal")
            .msg("cover logic network export failed in region '{}'", region)
            .fatal();
        return {};
      }
      Abc_FrameReplaceCurrentNetwork(frame, logic.release());
      if (search.abc_mode == Search_options::Abc_mode::opt) {
        status       = "abc_opt";
        variant_name = "opt";
      } else {
        const auto tmap_start = std::chrono::steady_clock::now();
        const auto nf         = mapping.delay.empty() ? std::string{"&nf"} : std::format("&nf -D {}", mapping.delay);
        for (const auto* command : {"strash", "&get -n", nf.c_str(), "&put -o"}) {
          if (Cmd_CommandExecute(frame, command) != 0) {
            livehd::diag::err("pass.synth", "abc-tmap", "internal")
                .msg("ABC '{}' failed on the cover network of region '{}'", command, region)
                .fatal();
            return {};
          }
        }
        tmap_ms       = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tmap_start).count();
        auto* current = Abc_FrameReadNtk(frame);
        if (!current || !Abc_NtkIsMappedLogic(current)) {
          livehd::diag::err("pass.synth", "abc-tmap", "internal")
              .msg("ABC left no mapped cover network for region '{}'", region)
              .fatal();
          return {};
        }
        area = Abc_NtkGetMappedArea(current);
        if (timed) {
          delay_ps = abc_timing_qor(current).delay_ps;
        }
        status       = "abc_tmap";
        variant_name = "tmap";
      }
    } else {
      Split_result gates;
      gates.status = Status::feasible;
      for (const auto& lut : cover.luts) {
        gates.gates.push_back({lut.root, lut.leaves, lut.fn.form, lut.fn.complemented, 0});
        if (!lut.fn.constant && !lut.fn.alias) {
          ++selected_cost.functions;
          selected_cost.literals += lut.fn.form.factored;
          max_support             = std::max<uint32_t>(max_support, static_cast<uint32_t>(lut.leaves.size()));
          max_literals            = std::max(max_literals, lut.fn.form.factored);
          max_series              = std::max(max_series, lut.fn.form.series);
        }
        if (lut.level != 255) {
          depth = std::max<uint32_t>(depth, lut.level);
        }
      }
      Mapping_request defaults;
      defaults.library   = mapping.library;
      defaults.admission = admit;
      if (mapping.time_budget_ms) {
        defaults.remaining_ms     = [&] { return double(mapping.time_budget_ms) - since(); };
        defaults.worker_admission = [&](uint64_t child_bytes) { return effort.admit(since(), sample(child_bytes)); };
      }
      defaults.required_ps = -1;
      Block_request block;
      block.library         = mapping.library;
      block.admission       = admit;
      const auto tmap_start = std::chrono::steady_clock::now();
      if (admit()) {
        split_mapping = map_split(source, gates, backend, defaults, block);
      } else {
        split_mapping.network = {.status = Map_status::exhausted, .reason = effort.reason};
      }
      tmap_ms           = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tmap_start).count();
      const auto& cells = split_mapping.network;
      if (cells.status == Map_status::mismatch || cells.status == Map_status::invalid) {
        livehd::diag::err("pass.synth", "invalid-fragment", "internal")
            .msg("cover tmap defect in region '{}': {}", region, cells.reason)
            .fatal();
        return {};
      }
      if (cells.status != Map_status::mapped) {
        reason = std::format("tmap: {}", cells.reason);
      } else {
        Network candidate = export_mapped(original, cells);
        if (!candidate) {
          livehd::diag::err("pass.synth", "invalid-stitch", "internal")
              .msg("cover mapped network stitching failed in region '{}'", region)
              .fatal();
          return {};
        }
        area = Abc_NtkGetMappedArea(candidate.get());
        if (timed) {
          delay_ps = abc_timing_qor(candidate.get()).delay_ps;
        }
        Abc_FrameReplaceCurrentNetwork(frame, candidate.release());
        status = "unate";
      }
    }
  }
  if (status == "abc_fallback" && reason.empty()) {
    reason = effort.reason;
  }
  sample();
  const auto& din = cover.domino_inputs;
  return std::format(
      R"({{"region":"{}","status":"{}","reason":"{}","variant":"{}","functions":{},"twins":{},"source_inverters":{},"literals":{},"ports":{},"depth":{},"max_support":{},"max_literals":{},"max_series":{},"area":{},"delay_ps":{},"budget_ps":{},"unate_ms":{},"tmap_ms":{},)"
      R"("domino":{},"nonunate":{},"aliases":{},"constants":{},"cover_cost":{},"domino_cost":{},"nonunate_cost":{},"domino_literals":{},"flow_cost":{},"domino_in2":{},"domino_in3":{},"domino_in4":{},"domino_in5":{},"domino_in6":{},"domino_in7":{},"domino_in8":{},"domino_s1":{},"domino_s2":{},"domino_s3":{},"domino_s4":{},"domino_s5":{},"domino_s6":{},"cones_1gate":{},"cones_2gates":{},"cones_3gates":{},"cones_4plus":{},"outputs_shallow":{},"outputs_deep":{},"outputs_wire":{},"covered_nodes":{},"replicated_nodes":{},"source_lits_pos":{},"source_lits_neg":{},"cover_cuts_kept":{},"cover_functions":{},)"
      R"("ref_luts":{},"ref_domino":{},"ref_nonunate":{},"ref_aliases":{},"ref_cost":{},"ref_ms":{},)"
      R"("cones_wire":{},"cones_2":{},"cones_3":{},"cones_more":{},"remainder_outputs":{},"remainder_nodes":{},"remainder_cells":{},"remainder_area":{},"remainder_ms":{},"split_inverters":{},"split_fanin_fallbacks":{},"bound_gates":{},"shared_bound_gates":{},"region_ms":{},"source_metrics":{},"source_witness":{{"record":{},"status":"{}"}},"attempts":[],"resources":{{"memory_scope":"parent_plus_active_synthesis_worker","sampled_peak_bytes":{},"worker_samples":{},"sampled_worker_peak_bytes":{},"exhausted":{},"reason":"{}"}}}})",
      livehd::json_util::escape(region),
      status,
      livehd::json_util::escape(reason),
      variant_name,
      selected_cost.functions,
      selected_cost.twins,
      selected_cost.source_inverters,
      selected_cost.literals,
      selected_cost.ports,
      depth,
      max_support,
      max_literals,
      max_series,
      area,
      delay_ps,
      budget_ps,
      unate_ms,
      tmap_ms,
      cover.domino,
      cover.nonunate,
      cover.aliases,
      cover.constants,
      cover.cost,
      cover.domino_cost,
      cover.nonunate_cost,
      cover.domino_literals,
      cover.flow_cost,
      din[2],
      din[3],
      din[4],
      din[5],
      din[6],
      din[7],
      din[8],
      cover.domino_series[1],
      cover.domino_series[2],
      cover.domino_series[3],
      cover.domino_series[4],
      cover.domino_series[5],
      cover.domino_series[6],
      cover.cone_gates[1],
      cover.cone_gates[2],
      cover.cone_gates[3],
      cover.cone_gates[4],
      cover.outputs_shallow,
      cover.outputs_deep,
      cover.outputs_wire,
      cover.covered_nodes,
      cover.replicated_nodes,
      cover.source_literals_pos,
      cover.source_literals_neg,
      cover.cuts,
      cover.functions,
      reference.luts,
      reference.domino,
      reference.nonunate,
      reference.aliases,
      reference.cost,
      reference_ms,
      split.cones_wire,
      split.cones_2,
      split.cones_3,
      split.cones_more,
      split.remainder_outputs.size(),
      split.remainder_nodes,
      split_mapping.remainder_cells,
      split_mapping.remainder_area,
      split_mapping.remainder_ms,
      split_mapping.inverters,
      split.fanin_fallbacks,
      split.bound_gates,
      split.shared_bound_gates,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
      source_metrics,
      source_witness.record,
      source_witness.status,
      observed.peak_bytes,
      observed.worker_samples,
      observed.worker_peak_bytes,
      !effort.reason.empty(),
      livehd::json_util::escape(effort.reason));
}
}  // namespace livehd::synth
