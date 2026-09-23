// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_tmap.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <set>

#include "abc_boundary.hpp"
#include "abc_timing.hpp"
#include "abc_worker.hpp"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "map/mio/mio.h"
#include "map/scl/sclLib.h"
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
struct Restore_frame {
  Abc_Frame_t* previous;
  ~Restore_frame() { Abc_FrameLeave(previous); }
};

Network logic(uint32_t inputs) {
  Network net(Abc_NtkAlloc(ABC_NTK_LOGIC, ABC_FUNC_SOP, 1));
  char    network_name[] = "unate_function";
  net->pName             = Abc_UtilStrsav(network_name);
  for (uint32_t i = 0; i < inputs; ++i) {
    auto* pi   = Abc_NtkCreatePi(net.get());
    auto  name = "i" + std::to_string(i);
    Abc_ObjAssignName(pi, name.data(), nullptr);
  }
  return net;
}

// The block as an SOP network: one node per logic function, its minterms as
// cubes; one named PO per output. Sources come first.
Network block_network(const Logic_network& block, uint32_t sources) {
  auto                    reference = logic(sources);
  std::vector<Abc_Obj_t*> objs(block.nodes.size(), nullptr);
  for (size_t id = 0, pi = 0; id < block.nodes.size(); ++id) {
    const auto& node = block.nodes[id];
    if (node.source) {
      objs[id] = Abc_NtkPi(reference.get(), static_cast<int>(pi++));
      continue;
    }
    auto* f = Abc_NtkCreateNode(reference.get());
    for (auto in : node.inputs) {
      Abc_ObjAddFanin(f, objs[in]);
    }
    std::string sop;
    for (uint32_t x = 0; x < (uint32_t{1} << node.table.inputs); ++x) {
      if (!node.table.get(x)) {
        continue;
      }
      std::string cube(node.table.inputs, '0');
      for (uint32_t j = 0; j < node.table.inputs; ++j) {
        cube[j] = ((x >> j) & 1) ? '1' : '0';
      }
      sop += cube + " 1\n";
    }
    if (sop.empty()) {
      sop = std::string(node.table.inputs, '-') + " 0\n";
    }
    f->pData = Abc_SopRegister(static_cast<Mem_Flex_t*>(reference->pManFunc), sop.c_str());
    objs[id] = f;
  }
  for (size_t k = 0; k < block.outputs.size(); ++k) {
    auto* po = Abc_NtkCreatePo(reference.get());
    Abc_ObjAddFanin(po, objs[block.outputs[k]]);
    auto name = "y" + std::to_string(k);
    Abc_ObjAssignName(po, name.data(), nullptr);
  }
  return reference;
}

void output(Abc_Ntk_t* net, Abc_Obj_t* value) {
  auto* po = Abc_NtkCreatePo(net);
  Abc_ObjAddFanin(po, value);
  char name[] = "y";
  Abc_ObjAssignName(po, name, nullptr);
}

Network request_network(const Mapping_request& r) {
  auto  n = logic(static_cast<uint32_t>(r.inputs.size()));
  auto* f = Abc_NtkCreateNode(n.get());
  for (uint32_t i = 0; i < r.inputs.size(); ++i) {
    Abc_ObjAddFanin(f, Abc_NtkPi(n.get(), static_cast<int>(i)));
  }
  std::string sop;
  if (r.inverter) {
    sop = "0 1\n";
  } else {
    for (const auto& term : r.terms) {
      std::string cube(r.inputs.size(), '-');
      for (auto port : term) {
        cube[port] = '1';
      }
      sop += cube + " 1\n";
    }
    if (sop.empty()) {
      sop = std::string(r.inputs.size(), '-') + " 0\n";
    }
  }
  f->pData = Abc_SopRegister(static_cast<Mem_Flex_t*>(n->pManFunc), sop.c_str());
  output(n.get(), f);
  return n;
}

Network fragment_network(const Mapped_fragment& fragment) {
  auto                    n = logic(fragment.inputs);
  std::vector<Abc_Obj_t*> wires;
  for (uint32_t j = 0; j < fragment.inputs; ++j) {
    wires.push_back(Abc_NtkPi(n.get(), static_cast<int>(j)));
  }
  for (const auto& cell : fragment.cells) {
    auto* node = Abc_NtkCreateNode(n.get());
    for (auto in : cell.inputs) {
      if (in >= wires.size()) {
        return {};
      }
      Abc_ObjAddFanin(node, wires[in]);
    }
    node->pData = Abc_SopRegister(static_cast<Mem_Flex_t*>(n->pManFunc), cell.cell_sop.c_str());
    wires.push_back(node);
  }
  if (fragment.output >= wires.size()) {
    return {};
  }
  output(n.get(), wires[fragment.output]);
  return n;
}

Mapped_fragment read_fragment(Abc_Ntk_t* mapped, uint32_t limit) {
  Mapped_fragment result;
  result.inputs = static_cast<uint32_t>(Abc_NtkPiNum(mapped));
  if (!Abc_NtkIsMappedLogic(mapped) || Abc_NtkLatchNum(mapped) || Abc_NtkPoNum(mapped) != 1) {
    result.reason = "ABC returned a non-combinational or unmapped fragment";
    return result;
  }
  if (static_cast<uint32_t>(Abc_NtkNodeNum(mapped)) > limit) {
    result.status = Map_status::exhausted;
    result.reason = "mapped cell limit exceeded";
    return result;
  }
  std::vector<Id> ids(static_cast<size_t>(Abc_NtkObjNumMax(mapped)), std::numeric_limits<Id>::max());
  for (uint32_t j = 0; j < result.inputs; ++j) {
    ids[Abc_ObjId(Abc_NtkPi(mapped, static_cast<int>(j)))] = j;
  }
  auto* order = Abc_NtkDfs(mapped, 0);
  bool  valid = true;
  for (int j = 0; j < Vec_PtrSize(order) && valid; ++j) {
    auto* obj  = static_cast<Abc_Obj_t*>(Vec_PtrEntry(order, j));
    auto* gate = static_cast<Mio_Gate_t*>(obj->pData);
    if (!gate || !Mio_GateReadSop(gate)) {
      valid = false;
      break;
    }
    Mapped_cell cell;
    cell.name       = Mio_GateReadName(gate);
    cell.output_pin = Mio_GateReadOutName(gate);
    cell.cell_sop   = Mio_GateReadSop(gate);
    cell.area       = Mio_GateReadArea(gate);
    for (auto* pin = Mio_GateReadPins(gate); pin; pin = Mio_PinReadNext(pin)) {
      cell.input_pins.emplace_back(Mio_PinReadName(pin));
    }
    for (int i = 0; i < Abc_ObjFaninNum(obj); ++i) {
      auto in = ids[Abc_ObjId(Abc_ObjFanin(obj, i))];
      if (in == std::numeric_limits<Id>::max()) {
        valid = false;
        break;
      }
      cell.inputs.push_back(in);
    }
    if (cell.inputs.size() != cell.input_pins.size()) {
      valid = false;
    }
    ids[Abc_ObjId(obj)]  = result.inputs + static_cast<Id>(result.cells.size());
    result.area         += cell.area;
    result.cells.push_back(std::move(cell));
  }
  Vec_PtrFree(order);
  result.output = ids[Abc_ObjId(Abc_ObjFanin0(Abc_NtkPo(mapped, 0)))];
  if (!valid || result.output == std::numeric_limits<Id>::max()) {
    result.reason = "ABC fragment port/cell readback failed";
    return result;
  }
  result.status = Map_status::mapped;
  return result;
}
// One ABC SOP cover ("01- 1\n" per cube; a trailing 0 describes the off-set)
// evaluated on its input values.
bool eval_sop(std::string_view sop, const std::vector<bool>& in) {
  bool any = false, onset = true;
  while (!sop.empty()) {
    const auto end  = sop.find('\n');
    auto       line = sop.substr(0, end);
    sop.remove_prefix(end == std::string_view::npos ? sop.size() : end + 1);
    if (line.empty()) {
      continue;
    }
    const auto space = line.rfind(' ');
    onset            = line[space + 1] == '1';
    bool cube        = true;
    for (size_t j = 0; j < space && cube; ++j) {
      cube = line[j] == '-' || (line[j] == '1') == in[j];
    }
    any = any || cube;
  }
  return any == onset;
}

// Exhaustive check of a mapped fragment against its requested function.
bool simulate_equal(const Mapping_request& request, const Mapped_fragment& fragment) {
  const auto        n = static_cast<uint32_t>(request.inputs.size());
  std::vector<bool> wire(n + fragment.cells.size());
  std::vector<bool> operands;
  for (uint32_t x = 0; x < (uint32_t{1} << n); ++x) {
    for (uint32_t j = 0; j < n; ++j) {
      wire[j] = ((x >> j) & 1) != 0;
    }
    bool expected = false;
    if (request.inverter) {
      expected = !wire[0];
    } else {
      for (const auto& term : request.terms) {
        bool product = true;
        for (auto port : term) {
          product = product && wire[port];
        }
        expected = expected || product;
      }
    }
    for (size_t c = 0; c < fragment.cells.size(); ++c) {
      const auto& cell = fragment.cells[c];
      operands.assign(cell.inputs.size(), false);
      for (size_t i = 0; i < cell.inputs.size(); ++i) {
        if (cell.inputs[i] >= n + c) {
          return false;
        }
        operands[i] = wire[cell.inputs[i]];
      }
      wire[n + c] = eval_sop(cell.cell_sop, operands);
    }
    if (fragment.output >= wire.size() || wire[fragment.output] != expected) {
      return false;
    }
  }
  return true;
}
// A cell's SOP as a truth table over its ordered pins (bit x = f(pins = x)).
std::vector<bool> sop_table(const Mapped_cell& cell) {
  const auto        k = static_cast<uint32_t>(cell.inputs.size());
  std::vector<bool> table(size_t{1} << k), in(k);
  for (uint32_t x = 0; x < (uint32_t{1} << k); ++x) {
    for (uint32_t j = 0; j < k; ++j) {
      in[j] = ((x >> j) & 1) != 0;
    }
    table[x] = eval_sop(cell.cell_sop, in);
  }
  return table;
}

// 64 patterns at a time: OR over the on-set minterms of the AND of the
// matching input literals.
uint64_t eval_words(const std::vector<bool>& table, const std::vector<uint64_t>& in) {
  uint64_t out = 0;
  for (size_t m = 0; m < table.size(); ++m) {
    if (!table[m]) {
      continue;
    }
    uint64_t product = ~uint64_t{0};
    for (size_t j = 0; j < in.size(); ++j) {
      product &= ((m >> j) & 1) ? in[j] : ~in[j];
    }
    out |= product;
  }
  return out;
}

// The mapped block against its logic: exhaustive up to 12 inputs, else 1024
// fixed pseudo-random patterns. A readback or pin-order mistake shows up as a
// mismatch; the stitched design is still checked by `lhd lec`.
bool simulate_block(const Logic_network& logic, const Mapped_block& block) {
  const auto inputs = block.inputs;
  const bool exhaustive = inputs <= 12;
  const auto words      = exhaustive ? std::max<size_t>(1, (size_t{1} << inputs) / 64) : 16;
  std::vector<std::vector<bool>> cell_tables;
  for (const auto& cell : block.cells) {
    if (cell.inputs.size() > 16) {
      return false;
    }
    cell_tables.push_back(sop_table(cell));
  }
  std::vector<std::vector<bool>> node_tables;
  for (const auto& node : logic.nodes) {
    std::vector<bool> t(size_t{1} << node.table.inputs);
    for (uint32_t x = 0; x < t.size(); ++x) {
      t[x] = node.table.get(x);
    }
    node_tables.push_back(std::move(t));
  }
  uint64_t state = 0x9e3779b97f4a7c15ULL;
  const auto next = [&] {
    state += 0x9e3779b97f4a7c15ULL;
    auto z = state;
    z      = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z      = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  };
  std::vector<uint64_t> wire(inputs + block.cells.size()), value(logic.nodes.size()), in;
  for (size_t w = 0; w < words; ++w) {
    uint32_t source = 0;
    for (uint32_t j = 0; j < inputs; ++j) {
      uint64_t pattern = 0;
      if (exhaustive) {
        for (uint32_t b = 0; b < 64; ++b) {
          pattern |= uint64_t{((w * 64 + b) >> j) & 1} << b;
        }
      } else {
        pattern = next();
      }
      wire[j] = pattern;
    }
    for (size_t id = 0; id < logic.nodes.size(); ++id) {
      const auto& node = logic.nodes[id];
      if (node.source) {
        value[id] = wire[source++];
        continue;
      }
      in.clear();
      for (auto i : node.inputs) {
        in.push_back(value[i]);
      }
      value[id] = eval_words(node_tables[id], in);
    }
    for (size_t c = 0; c < block.cells.size(); ++c) {
      in.clear();
      for (auto i : block.cells[c].inputs) {
        in.push_back(wire[i]);
      }
      wire[inputs + c] = eval_words(cell_tables[c], in);
    }
    for (size_t k = 0; k < logic.outputs.size(); ++k) {
      if (wire[block.outputs[k]] != value[logic.outputs[k]]) {
        return false;
      }
    }
  }
  return true;
}
}  // namespace

Mapped_block Abc_tmap::map_block(const Block_request& request) {
  const auto admitted  = [&] { return !request.admission || request.admission(); };
  const auto exhausted = [] { return Mapped_block{.status = Map_status::exhausted, .reason = "block resource budget exhausted"}; };
  if (!admitted()) {
    return exhausted();
  }
  const auto& block   = request.logic;
  uint32_t    sources = 0;
  for (const auto& node : block.nodes) {
    if (node.source) {
      if (sources != static_cast<uint32_t>(&node - block.nodes.data())) {
        return {.status = Map_status::unsupported, .reason = "block sources must come first"};
      }
      ++sources;
    }
  }
  if (!block.valid() || block.outputs.empty()) {
    return {.status = Map_status::unsupported, .reason = "invalid block logic"};
  }
  if (request.library.find_first_of("\";\n\r") != std::string::npos || !std::filesystem::is_regular_file(request.library)) {
    return {.status = Map_status::unsupported, .reason = "invalid Liberty path"};
  }
  if (!frame_) {
    frame_ = Abc_FrameCreate();
  }
  if (!frame_) {
    return {.status = Map_status::exhausted, .reason = "ABC frame allocation failed"};
  }
  auto*         frame = static_cast<Abc_Frame_t*>(frame_);
  Restore_frame restore{Abc_FrameEnter(frame)};
  if (library_ != request.library) {
    Abc_FrameDeleteAllNetworks(frame);
    auto command = "read_lib -s \"" + request.library + "\"";
    if (Cmd_CommandExecute(frame, command.c_str()) != 0) {
      library_.clear();
      return {.status = Map_status::unsupported, .reason = "ABC could not read Liberty"};
    }
    library_ = request.library;
  }
  auto reference = block_network(block, sources);
  if (!Abc_NtkCheck(reference.get())) {
    return {.status = Map_status::invalid, .reason = "invalid block SOP network"};
  }
  Abc_FrameReplaceCurrentNetwork(frame, Abc_NtkDup(reference.get()));
  // Technology mapping only: no &dch, &fraig or dc2.
  for (const auto* command : {"strash", "&get -n", "&nf", "&put -o"}) {
    if (!admitted()) {
      return exhausted();
    }
    if (Cmd_CommandExecute(frame, command) != 0) {
      return {.status = Map_status::unsupported, .reason = "ABC block technology mapping failed"};
    }
  }
  auto* mapped = Abc_FrameReadNtk(frame);
  if (!mapped || !Abc_NtkIsMappedLogic(mapped) || Abc_NtkLatchNum(mapped) || Abc_NtkPiNum(mapped) != static_cast<int>(sources)
      || Abc_NtkPoNum(mapped) != static_cast<int>(block.outputs.size())) {
    return {.status = Map_status::unsupported, .reason = "ABC returned a non-combinational or unmapped block"};
  }
  if (static_cast<uint32_t>(Abc_NtkNodeNum(mapped)) > request.max_cells) {
    return {.status = Map_status::exhausted, .reason = "mapped block cell limit exceeded"};
  }
  Mapped_block result;
  result.inputs = sources;
  std::vector<Id> ids(static_cast<size_t>(Abc_NtkObjNumMax(mapped)), std::numeric_limits<Id>::max());
  for (uint32_t j = 0; j < sources; ++j) {
    ids[Abc_ObjId(Abc_NtkPi(mapped, static_cast<int>(j)))] = j;
  }
  auto* order = Abc_NtkDfs(mapped, 0);
  bool  valid = true;
  for (int j = 0; j < Vec_PtrSize(order) && valid; ++j) {
    auto* obj  = static_cast<Abc_Obj_t*>(Vec_PtrEntry(order, j));
    auto* gate = static_cast<Mio_Gate_t*>(obj->pData);
    if (!gate || !Mio_GateReadSop(gate)) {
      valid = false;
      break;
    }
    Mapped_cell cell;
    cell.name       = Mio_GateReadName(gate);
    cell.output_pin = Mio_GateReadOutName(gate);
    cell.cell_sop   = Mio_GateReadSop(gate);
    cell.area       = Mio_GateReadArea(gate);
    for (auto* pin = Mio_GateReadPins(gate); pin; pin = Mio_PinReadNext(pin)) {
      cell.input_pins.emplace_back(Mio_PinReadName(pin));
    }
    for (int i = 0; i < Abc_ObjFaninNum(obj); ++i) {
      auto in = ids[Abc_ObjId(Abc_ObjFanin(obj, i))];
      if (in == std::numeric_limits<Id>::max()) {
        valid = false;
        break;
      }
      cell.inputs.push_back(in);
    }
    valid                = valid && cell.inputs.size() == cell.input_pins.size();
    ids[Abc_ObjId(obj)]  = sources + static_cast<Id>(result.cells.size());
    result.area         += cell.area;
    result.cells.push_back(std::move(cell));
  }
  Vec_PtrFree(order);
  for (size_t k = 0; k < block.outputs.size() && valid; ++k) {
    const auto wire = ids[Abc_ObjId(Abc_ObjFanin0(Abc_NtkPo(mapped, static_cast<int>(k))))];
    valid           = wire != std::numeric_limits<Id>::max();
    result.outputs.push_back(wire);
  }
  if (!valid) {
    return {.status = Map_status::unsupported, .reason = "ABC block port/cell readback failed"};
  }
  if (!admitted()) {
    return exhausted();
  }
  if (!simulate_block(block, result)) {
    return {.status = Map_status::mismatch, .reason = "mapped block failed simulation against its logic"};
  }
  result.status = Map_status::mapped;
  return result;
}

std::vector<std::pair<uint64_t, uint32_t>> Abc_tmap::lut_reference(const Logic_network& logic_in, uint32_t k, bool choices) {
  std::vector<std::pair<uint64_t, uint32_t>> luts;
  uint32_t                                   sources = 0;
  for (const auto& node : logic_in.nodes) {
    if (node.source) {
      if (sources != static_cast<uint32_t>(&node - logic_in.nodes.data())) {
        return luts;
      }
      ++sources;
    }
  }
  if (!logic_in.valid() || logic_in.outputs.empty() || k < 2 || k > 6) {
    return luts;
  }
  if (!frame_) {
    frame_ = Abc_FrameCreate();
  }
  if (!frame_) {
    return luts;
  }
  auto*         frame = static_cast<Abc_Frame_t*>(frame_);
  Restore_frame restore{Abc_FrameEnter(frame)};
  auto          reference = block_network(logic_in, sources);
  if (!Abc_NtkCheck(reference.get())) {
    return luts;
  }
  Abc_FrameReplaceCurrentNetwork(frame, reference.release());
  const auto mapping = std::format("&if -K {} -a", k);
  for (const auto* command : {"strash", "&get -n", choices ? "&dch" : "", mapping.c_str(), "&put"}) {
    if (*command && Cmd_CommandExecute(frame, command) != 0) {
      return {};
    }
  }
  auto* mapped = Abc_FrameReadNtk(frame);
  if (!mapped || !Abc_NtkIsLogic(mapped) || (!Abc_NtkIsSopLogic(mapped) && !Abc_NtkToSop(mapped, -1, ABC_INFINITY))) {
    return {};
  }
  Abc_Obj_t* obj;
  int        i;
  Abc_NtkForEachNode(mapped, obj, i) {
    const auto inputs = static_cast<uint32_t>(Abc_ObjFaninNum(obj));
    if (inputs > 6) {
      return {};
    }
    auto*    sop   = static_cast<char*>(obj->pData);
    uint64_t table = 0;
    for (uint32_t x = 0; x < (1U << inputs); ++x) {
      bool  value = false;
      char* cube;
      Abc_SopForEachCube(sop, static_cast<int>(inputs), cube) {
        bool hit = true;
        for (uint32_t j = 0; j < inputs && hit; ++j) {
          const bool bit = ((x >> j) & 1) != 0;
          hit            = cube[j] == '-' || (cube[j] == '1') == bit;
        }
        value |= hit;
      }
      if (value != (Abc_SopIsComplement(sop) != 0)) {
        table |= uint64_t{1} << x;
      }
    }
    luts.emplace_back(table, inputs);
  }
  return luts;
}

Abc_tmap::~Abc_tmap() {
  if (frame_) {
    auto* frame = static_cast<Abc_Frame_t*>(frame_);
    if (Abc_FrameReadGlobalFrame() == frame) {
      Abc_FrameLeave(nullptr);
    }
    Abc_FrameDestroy(frame);
  }
}

Mapped_fragment Abc_tmap::map(const Mapping_request& request) {
  const auto admitted = [&] { return !request.admission || request.admission(); };
  const auto exhausted
      = [] { return Mapped_fragment{.status = Map_status::exhausted, .reason = "function resource budget exhausted"}; };
  if (!admitted()) {
    return exhausted();
  }
  if (request.inputs.size() > 24 || request.terms.size() > 4096 || request.proof_conflicts == 0
      || (request.inverter && (request.inputs.size() != 1 || !request.terms.empty()))) {
    return {.status = Map_status::invalid, .reason = "invalid or unbounded function mapping request"};
  }
  for (const auto& term : request.terms) {
    if (term.size() > request.inputs.size()
        || std::any_of(term.begin(), term.end(), [&](uint32_t p) { return p >= request.inputs.size(); })) {
      return {.status = Map_status::invalid, .reason = "invalid ordered function port"};
    }
  }
  if (!std::isfinite(request.required_ps) || !std::isfinite(request.output_load_ff)
      || std::any_of(request.inputs.begin(), request.inputs.end(), [](const auto& p) {
           return !std::isfinite(p.arrival_ps) || p.arrival_ps < 0;
         })) {
    return {.status = Map_status::invalid, .reason = "invalid function timing environment"};
  }
  const bool timed = request.required_ps >= 0 || request.output_load_ff >= 0
                     || std::any_of(request.inputs.begin(), request.inputs.end(), [](const auto& p) {
                          return p.arrival_ps != 0 || !p.driving_cell.empty();
                        });
  // Reject ABC command delimiters before forming the read_lib command.
  if (request.library.find_first_of("\";\n\r") != std::string::npos || !std::filesystem::is_regular_file(request.library)) {
    return {.status = Map_status::invalid, .reason = "invalid Liberty path"};
  }
  if (request.remaining_ms) {
    ++workers_.calls;
    auto result        = map_in_worker(worker_executable_, request);
    workers_.stopped  += result.stopped;
    workers_.failures += result.failed;
    return std::move(result.fragment);
  }
  if (!frame_) {
    frame_ = Abc_FrameCreate();
  }
  if (!frame_) {
    return {.status = Map_status::exhausted, .reason = "ABC frame allocation failed"};
  }
  auto*         frame = static_cast<Abc_Frame_t*>(frame_);
  Restore_frame restore{Abc_FrameEnter(frame)};
  if (library_ != request.library) {
    // Discard the previous mapped network before replacing the library whose
    // Mio gate pointers it owns. The frame keeps only this one function.
    Abc_FrameDeleteAllNetworks(frame);
    auto command = "read_lib -s \"" + request.library + "\"";
    if (Cmd_CommandExecute(frame, command.c_str()) != 0) {
      library_.clear();
      return {.status = Map_status::unsupported, .reason = "ABC could not read Liberty"};
    }
    library_ = request.library;
  }
  if (!admitted()) {
    return exhausted();
  }
  std::unique_ptr<livehd::abc::Boundary_table> boundary;
  if (timed) {
    if (!abc_has_timing()) {
      return {.status = Map_status::unsupported, .reason = "Liberty has no usable NLDM timing surfaces"};
    }
    boundary = std::make_unique<livehd::abc::Boundary_table>(request.inputs.size(), 1);
    boundary->set_po(0, static_cast<float>(request.output_load_ff), 0);
    auto* scl = static_cast<SC_Lib*>(Abc_FrameReadLibScl());
    for (size_t i = 0; i < request.inputs.size(); ++i) {
      const auto& input  = request.inputs[i];
      SC_Cell*    driver = nullptr;
      if (!input.driving_cell.empty()) {
        auto      name = input.driving_cell;
        const int cell = Abc_SclCellFind(scl, name.data());
        if (cell < 0) {
          return {.status = Map_status::unsupported, .reason = "unknown input driving cell"};
        }
        driver = SC_LibCell(scl, cell);
        if (!driver || driver->n_inputs < 1 || driver->n_outputs != 1) {
          return {.status = Map_status::unsupported, .reason = "unsupported input driving cell"};
        }
      }
      boundary->set_pi(i, driver, static_cast<float>(input.arrival_ps));
    }
    boundary->install();
  }
  auto reference = request_network(request);
  if (!reference || !Abc_NtkCheck(reference.get())) {
    return {.status = Map_status::invalid, .reason = "invalid function SOP"};
  }
  Abc_FrameReplaceCurrentNetwork(frame, Abc_NtkDup(reference.get()));
  // Technology mapping only. No cross-function restructuring or retiming.
  for (const auto* command : {"strash", "&get -n", "&nf", "&put -o"}) {
    if (!admitted()) {
      return exhausted();
    }
    if (Cmd_CommandExecute(frame, command) != 0) {
      return {.status = Map_status::unsupported, .reason = "ABC function technology mapping failed"};
    }
  }
  if (!admitted()) {
    return exhausted();
  }
  if (timed) {
    // Preserve function boundaries: only buffer/size this function's cells.
    const auto budget
        = request.required_ps > 0 ? std::format(" -D {}", std::max(1.0, std::floor(request.required_ps))) : std::string{};
    for (const auto& command : {std::string{"buffer -N 16"}, "upsize" + budget, "dnsize" + budget}) {
      if (!admitted()) {
        return exhausted();
      }
      if (Cmd_CommandExecute(frame, command.c_str()) != 0) {
        return {.status = Map_status::unsupported, .reason = "ABC function sizing failed"};
      }
    }
  }
  if (!admitted()) {
    return exhausted();
  }
  auto result = read_fragment(Abc_FrameReadNtk(frame), request.max_cells);
  if (result.status != Map_status::mapped) {
    return result;
  }
  if (timed && !abc_fragment_timing(Abc_FrameReadNtk(frame), result)) {
    return {.status = Map_status::unsupported, .reason = "function timing unvalidated"};
  }
  auto exported = fragment_network(result);
  if (!exported || !Abc_NtkCheck(exported.get())) {
    return {.status = Map_status::invalid, .reason = "invalid exported cell fragment"};
  }
  // Check the EXPORTED fragment, so pin-order mistakes in readback cannot hide
  // behind a correct ABC mapping. A function of at most 12 inputs is checked
  // exhaustively by evaluating both SOP descriptions (no solver per call);
  // wider ones use the miter below.
  if (request.inputs.size() <= 12) {
    if (!simulate_equal(request, result)) {
      result.status = Map_status::mismatch;
      result.reason = "exported ABC fragment failed equivalence";
    }
    return result;
  }
  Network a(Abc_NtkStrash(reference.get(), 0, 1, 0));
  Network b(Abc_NtkStrash(exported.get(), 0, 1, 0));
  Network miter(Abc_NtkMiter(a.get(), b.get(), 1, 0, 0, 0));
  if (!miter) {
    return {.status = Map_status::invalid, .reason = "could not build fragment proof miter"};
  }
  if (!admitted()) {
    return exhausted();
  }
  auto proof = Abc_NtkMiterSat(miter.get(), request.proof_conflicts, 0, 0, nullptr, nullptr);
  if (proof == 0) {
    result.status = Map_status::mismatch;
    result.reason = "exported ABC fragment failed equivalence";
  } else if (!admitted()) {
    return exhausted();
  } else if (proof != 1) {
    result.status = Map_status::proof_inconclusive;
    result.reason = "fragment equivalence conflict budget exhausted";
  }
  return result;
}
}  // namespace livehd::synth
