// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Partition-boundary environment for pass.abc -- see abc_boundary.hpp for the
// why. Three pieces live here:
//
//   * Boundary_table: the per-PI/PO `SC_Bnd` table the local ABC patch's SCL
//     timer reads (packages/abc.patch, map/scl/sclSize.[ch]), owned and
//     installed on the frame from LiveHD.
//   * Phase A (Mapper::fill_static_boundary): the source-graph estimate a
//     region is mapped against, before any neighbour exists.
//   * Phase B (Mapper::refine_boundaries): re-import every mapped region into
//     ABC, join every port bit through the wrappers to its real driver cell
//     and real sink pins, re-size each region in place against that exact
//     environment, and write the cell swaps back into the netlist.

#include "abc_boundary.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abc_incr.hpp"
#include "abc_map.hpp"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "diag.hpp"
#include "dlop.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "host_mem.hpp"
#include "node_util.hpp"
#include "pin_tracker.hpp"

// clang-format off
// ABC headers must stay in dependency order (see abc_map.cpp). Do not sort.
extern "C" {
#include "base/abc/abc.h"
#include "base/main/abcapis.h"
#include "base/main/main.h"
#include "base/main/mainInt.h"
#include "base/cmd/cmd.h"
#include "map/mio/mio.h"
#include "map/scl/sclLib.h"
#include "map/scl/sclSize.h"
#include "misc/extra/extra.h"
}
// clang-format on

namespace gu = livehd::graph_util;

namespace livehd::abc {

// ---------------------------------------------------------------------------
// Boundary_table
// ---------------------------------------------------------------------------

Boundary_table::Boundary_table(size_t n_pi, size_t n_po) {
  auto* b     = ABC_CALLOC(SC_Bnd, 1);
  b->vPoLoad  = Vec_FltStart(static_cast<int>(n_po));
  b->vPoDelay = Vec_FltStart(static_cast<int>(n_po));
  b->vPiCell  = Vec_PtrStart(static_cast<int>(n_pi));
  b->vPiArr   = Vec_FltStart(static_cast<int>(n_pi));
  Vec_FltFill(b->vPoLoad, static_cast<int>(n_po), -1.0f);
  Vec_FltFill(b->vPoDelay, static_cast<int>(n_po), -1.0f);
  Vec_FltFill(b->vPiArr, static_cast<int>(n_pi), -1.0f);
  bnd_ = b;
}

Boundary_table::~Boundary_table() {
  uninstall();
  auto* b = static_cast<SC_Bnd*>(bnd_);
  if (b == nullptr) {
    return;
  }
  Vec_FltFreeP(&b->vPoLoad);
  Vec_FltFreeP(&b->vPoDelay);
  Vec_PtrFreeP(&b->vPiCell);
  Vec_FltFreeP(&b->vPiArr);
  ABC_FREE(b);
  bnd_ = nullptr;
}

void Boundary_table::set_po(size_t i, float load_ff, float delay_ps) {
  auto* b = static_cast<SC_Bnd*>(bnd_);
  if (static_cast<int>(i) >= Vec_FltSize(b->vPoLoad)) {
    return;
  }
  Vec_FltWriteEntry(b->vPoLoad, static_cast<int>(i), load_ff);
  Vec_FltWriteEntry(b->vPoDelay, static_cast<int>(i), delay_ps);
}

void Boundary_table::set_pi(size_t i, void* sc_cell, float arrival_ps) {
  auto* b = static_cast<SC_Bnd*>(bnd_);
  if (static_cast<int>(i) >= Vec_PtrSize(b->vPiCell)) {
    return;
  }
  Vec_PtrWriteEntry(b->vPiCell, static_cast<int>(i), sc_cell);
  Vec_FltWriteEntry(b->vPiArr, static_cast<int>(i), arrival_ps);
}

float Boundary_table::po_load(size_t i) const {
  auto* b = static_cast<SC_Bnd*>(bnd_);
  return static_cast<int>(i) < Vec_FltSize(b->vPoLoad) ? Vec_FltEntry(b->vPoLoad, static_cast<int>(i)) : -1.0f;
}

void Boundary_table::install() {
  Abc_FrameSetBnd(bnd_);
  installed_ = true;
}

void Boundary_table::uninstall() {
  if (installed_) {
    // Only clear the frame slot when it still points at THIS table: a nested
    // table (Phase B inside a Phase A scope cannot happen, but be exact).
    if (Abc_FrameReadBnd() == bnd_) {
      Abc_FrameSetBnd(nullptr);
    }
    installed_ = false;
  }
}

// ---------------------------------------------------------------------------
// Library-derived defaults (start())
// ---------------------------------------------------------------------------

namespace {

bool lib_can_size(const SC_Lib* lib) {
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

// The library's "typical input pin": the mean input capacitance over the
// smallest cell of every single-output class with up to four inputs -- what
// an unmapped consumer beyond the partition most plausibly costs. In fF (ABC
// normalizes every SCL library to ps/fF on read).
float typical_input_cap_ff(SC_Lib* lib) {
  double   sum = 0.0;
  uint64_t n   = 0;
  SC_Cell* cls = nullptr;
  int      i   = 0;
  SC_LibForEachCellClass(lib, cls, i) {
    auto* rep = cls->pRepr != nullptr ? cls->pRepr : cls;
    if (rep->n_inputs < 1 || rep->n_inputs > 4 || rep->n_outputs != 1) {
      continue;
    }
    sum += SC_CellPinCapAve(rep);
    ++n;
  }
  if (n == 0) {
    return -1.0f;
  }
  return static_cast<float>(sum / static_cast<double>(n));
}

}  // namespace

void Mapper::resolve_boundary_defaults() {
  auto* scl   = static_cast<SC_Lib*>(Abc_FrameReadLibScl());
  scl_lib_ok_ = lib_can_size(scl);
  drive_cell_ = nullptr;
  if (!scl_lib_ok_ || !startup_opts_.boundary) {
    return;
  }
  typical_cap_ff_  = typical_input_cap_ff(scl);
  const auto& want = startup_opts_.boundary_drive;
  if (want == "none") {
    drive_cell_ = nullptr;
  } else if (!want.empty()) {
    const int id = Abc_SclCellFind(scl, const_cast<char*>(want.c_str()));
    if (id < 0) {
      livehd::diag::err("pass.abc", "boundary-drive", "io")
          .msg("pass.abc.boundary_drive names '{}', which is not a combinational cell of '{}'", want, startup_opts_.library)
          .hint("name a single-output cell of the Liberty (a buffer or inverter), or `none` for an ideal driver")
          .fatal();
      return;
    }
    drive_cell_ = SC_LibCell(scl, id);
  } else {
    // The smallest ORDINARY buffer: the stand-in for "some upstream gate or
    // flop". A library's buffer class also holds its delay lines (sky130
    // dlygate4sd3_1 is what the class representative resolves to), so rank
    // every single-input non-inverting cell by delay at a moderate load and
    // keep the smallest one within twice the fastest -- a buffer, never a
    // delay element. Falls back to the inverter class.
    SC_Cell*                                best     = nullptr;
    float                                   best_dly = std::numeric_limits<float>::infinity();
    std::vector<std::pair<SC_Cell*, float>> cands;
    SC_Cell*                                c    = nullptr;
    int                                     i    = 0;
    const float                             load = 4.0f * std::max(typical_cap_ff_, 0.0f);
    SC_LibForEachCell(scl, c, i) {
      if (c->n_inputs != 1 || c->n_outputs != 1 || Vec_WrdEntry(&SC_CellPin(c, 1)->vFunc, 0) != ABC_CONST(0xAAAAAAAAAAAAAAAA)
          || Scl_CellPinTime(c, 0) == nullptr) {
        continue;
      }
      const float dly = Scl_LibPinArrivalEstimate(c, 0, 0.0f, load);
      cands.emplace_back(c, dly);
      best_dly = std::min(best_dly, dly);
    }
    for (const auto& [cell, dly] : cands) {
      if (dly <= 2.0f * best_dly && (best == nullptr || cell->area < best->area)) {
        best = cell;
      }
    }
    if (best == nullptr) {
      if (auto* inv = Abc_SclFindInvertor(scl, 0); inv != nullptr && inv->n_inputs == 1 && inv->n_outputs == 1) {
        best = inv;
      }
    }
    drive_cell_ = best;
  }
  if (startup_opts_.verbose) {
    std::print("[pass.abc] boundary: typical input pin {:.3f} fF, stand-in driver {}, io_load {:.3f} fF\n",
               typical_cap_ff_,
               drive_cell_ != nullptr ? static_cast<SC_Cell*>(drive_cell_)->pName : "none",
               startup_opts_.io_load >= 0.0f ? startup_opts_.io_load : typical_cap_ff_);
  }
}

// ---------------------------------------------------------------------------
// Phase A: the static estimate
// ---------------------------------------------------------------------------

int Mapper::fill_static_boundary(Boundary_table& table, const livehd::partition::Region_body& rb,
                                 const absl::flat_hash_set<hhds::Node_class>& region, const std::vector<int>& pi_port,
                                 const std::vector<std::pair<size_t, int>>& po_order) {
  const float        io_load = opts_.io_load >= 0.0f ? opts_.io_load : std::max(typical_cap_ff_, 0.0f);
  int                bits    = 0;
  // Per output port: consumer pins outside the region (each reads the whole
  // bus, the per-bit refinement is Phase B's) and whether it feeds a graph
  // output of the def.
  std::vector<float> port_load(rb.outputs.size(), -1.0f);
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    const auto& drv = rb.outputs[po].src_driver;
    if (drv.is_invalid() || drv.is_const()) {
      continue;
    }
    uint32_t ext     = 0;
    bool     primary = false;
    for (const auto& e : drv.out_edges()) {
      if (gu::is_graph_output_pin(e.sink)) {
        primary = true;
        continue;
      }
      const auto m = e.sink.get_master_node();
      if (m.is_invalid() || region.contains(m)) {
        continue;
      }
      ++ext;
    }
    if (ext == 0 && !primary) {
      continue;
    }
    port_load[po] = static_cast<float>(ext) * std::max(typical_cap_ff_, 0.0f) + (primary ? io_load : 0.0f);
  }
  for (size_t i = 0; i < po_order.size(); ++i) {
    const auto po = po_order[i].first;
    if (po < port_load.size() && port_load[po] >= 0.0f) {
      table.set_po(i, port_load[po], -1.0f);
      ++bits;
    }
  }
  // Per input port: a stand-in driver unless the source is a constant.
  for (size_t i = 0; i < pi_port.size(); ++i) {
    if (pi_port[i] < 0 || static_cast<size_t>(pi_port[i]) >= rb.inputs.size()) {
      continue;
    }
    const auto& drv = rb.inputs[static_cast<size_t>(pi_port[i])].src_driver;
    if (drv.is_invalid() || drv.is_const()) {
      continue;
    }
    table.set_pi(i, drive_cell_, -1.0f);
    ++bits;
  }
  return bits;
}

// ---------------------------------------------------------------------------
// Phase B: exact refinement over the stitched netlist
// ---------------------------------------------------------------------------

namespace {

using Pin_key = std::pair<hhds::Pin_class, int>;  // (leaf driver pin, bit)

// What one imported PI/PO stands for. `port` indexes the def's input (PI) or
// output (PO) decls; `inst` indexes Imp_def::insts when the terminal is an
// instance pin (a PI for a child's output bit, a PO for a child's input bit);
// both -1 for an opaque net (a native flop, an unmapped node, a memory).
struct Terminal {
  int         port = -1;
  int         bit  = -1;
  int         inst = -1;
  std::string inst_port;  // child decl name when inst >= 0
};

struct Instance {
  hhds::Node_class node;
  std::string      def;       // child def name (an imported def, or an opaque one)
  int              idx = -1;  // index into Refine::defs, -1 when not imported
};

// One def of the output library reachable from --top: region modules and
// wrappers alike (a wrapper is a def with zero cells and only instances).
struct Imp_def {
  bool                                                        depth_complete = true;
  std::string                                                 name;
  hhds::Graph*                                                g   = nullptr;
  Abc_Ntk_t*                                                  ntk = nullptr;  // netlist form, ABC_FUNC_MAP
  std::vector<std::string>                                    in_names, out_names;
  std::vector<int>                                            in_bits, out_bits;
  std::vector<Terminal>                                       pi, po;    // by PI/PO index
  std::vector<hhds::Node_class>                               node_sub;  // netlist node id -> Liberty cell Sub
  std::vector<Instance>                                       insts;
  absl::flat_hash_map<std::pair<int, int>, int>               in_pi;   // (input port, bit) -> PI index
  absl::flat_hash_map<std::pair<int, int>, int>               out_po;  // (output port, bit) -> PO index
  // (instance idx, child output decl, bit) -> PI ; (instance idx, child input decl, bit) -> PO
  absl::flat_hash_map<std::tuple<int, std::string, int>, int> inst_out_pi, inst_in_po;
  int                                                         cells    = 0;
  int                                                         latches  = 0;
  bool                                                        imported = false;
  // ABC object id -> PI / PO index (ABC's own iTemp/iData are scratch that
  // the logic conversion and the sizer overwrite).
  std::vector<int>                                            pi_of_obj, po_of_obj;
  // Environment memo (per round)
  std::vector<float>                                          pi_load;  // internal load of PI i (fF), <0 = not computed
  // Port times propagated between rounds (ps): the arrival at each PI /
  // output driver, the departure (longest path to an endpoint) at each PI /
  // beyond each PO.
  std::vector<float>                                          pi_arr, pi_dept, po_arr, po_delay;
};

struct Refine {
  Mapper&                                                     mapper;
  hhds::GraphLibrary&                                         outlib;
  Abc_Frame_t*                                                frame = nullptr;
  Mio_Library_t*                                              mio   = nullptr;
  SC_Lib*                                                     scl   = nullptr;
  std::vector<std::unique_ptr<Imp_def>>                       defs;
  absl::flat_hash_map<std::string, int>                       def_idx;
  absl::flat_hash_set<std::string>                            dff_names;
  absl::flat_hash_map<std::string, SC_Cell*>                  cell_by_name;
  float                                                       typical_cap = 0.0f;
  float                                                       io_load     = 0.0f;
  SC_Cell*                                                    drive_cell  = nullptr;
  int                                                         top         = -1;
  // Mio pin names of a gate, in pin order (Mapper::cell_desc_for -- the same
  // decl a region read-back mints, so the swap below keeps port ids).
  std::function<const std::vector<std::string>&(Mio_Gate_t*)> pin_names;
  // parents: def idx -> list of (parent def idx, instance idx)
  std::vector<std::vector<std::pair<int, int>>>               parents;
  uint64_t                                                    unresolved_bits = 0;

  Refine(Mapper& m, hhds::GraphLibrary& lib) : mapper(m), outlib(lib) {}

  SC_Cell* sc_cell(Mio_Gate_t* g) {
    if (g == nullptr) {
      return nullptr;
    }
    const std::string name = Mio_GateReadName(g);
    if (auto it = cell_by_name.find(name); it != cell_by_name.end()) {
      return it->second;
    }
    SC_Cell*  c  = nullptr;
    const int id = Abc_SclCellFind(scl, const_cast<char*>(name.c_str()));
    if (id >= 0) {
      c = SC_LibCell(scl, id);
    }
    cell_by_name.emplace(name, c);
    return c;
  }
  // Input capacitance of pin k of the cell behind gate g (typical when the
  // cell is unknown to the SCL library).
  float pin_cap(Mio_Gate_t* g, int k) {
    auto* c = sc_cell(g);
    if (c == nullptr || k < 0 || k >= c->n_inputs) {
      return typical_cap;
    }
    return SC_CellPinCap(c, k);
  }
  // The D pin of a DFF cell (sequential cells are usually absent from the SCL
  // library: typical).
  float dff_d_cap(std::string_view cell, std::string_view d_pin) {
    const int id = Abc_SclCellFind(scl, const_cast<char*>(std::string{cell}.c_str()));
    if (id < 0) {
      return typical_cap;
    }
    auto* c = SC_LibCell(scl, id);
    for (int k = 0; k < c->n_inputs; ++k) {
      if (d_pin == SC_CellPinName(c, k)) {
        return SC_CellPinCap(c, k);
      }
    }
    return typical_cap;
  }
};

bool is_splitter_def(std::string_view n) { return n.starts_with("__livehd_abc_input_bits_"); }
bool is_tie_def(std::string_view n) { return n == "_const0_" || n == "_const1_"; }

}  // namespace

// Import one def's mapped body into an ABC mapped netlist. Every port bit the
// body consumes/produces becomes a PI/PO; Liberty cells become mapped nodes;
// DFF cells become latches; instances (region modules inside a wrapper, child
// defs inside a region) and anything native become PI/PO cuts recorded as
// Terminals so the join can follow them. Returns false when the body cannot
// be represented (the def is then left alone: its neighbours still see it
// through the estimate).
static bool import_def(Refine& R, Imp_def& d) {
  auto*      g   = d.g;
  const auto gio = g->get_io();
  if (!gio) {
    return false;
  }
  Pin_tracker<hhds::Pin_class>                      trk{hhds::Pin_class{}};
  absl::flat_hash_map<Pin_key, Abc_Obj_t*>          leaf_net;
  absl::flat_hash_map<hhds::Pin_class, int>         in_port_of;   // input pin -> decl index
  absl::flat_hash_map<hhds::Pin_class, int>         opaque_inst;  // opaque driver pin -> instance idx (or -1)
  absl::flat_hash_map<hhds::Pin_class, std::string> opaque_port;
  struct Cell {
    hhds::Node_class node;
    Mio_Gate_t*      gate = nullptr;
    hhds::Pin_class  out;
    Abc_Obj_t*       abc = nullptr;
  };
  struct Latch {
    hhds::Node_class node;
    hhds::Pin_class  q;
    hhds::Pin_class  d_drv;
    Abc_Obj_t*       bi = nullptr;
  };
  struct Cut_in {  // a PO that feeds something native: (driver, bit) with its terminal
    hhds::Pin_class drv;
    int             bit;
    Terminal        term;
  };
  std::vector<Cell>   cells;
  std::vector<Latch>  latches;
  std::vector<Cut_in> cut_ins;

  d.ntk           = Abc_NtkAlloc(ABC_NTK_NETLIST, ABC_FUNC_MAP, 1);
  d.ntk->pName    = Extra_UtilStrsav(const_cast<char*>(d.name.c_str()));
  d.ntk->pManFunc = R.mio;
  auto* ntk       = d.ntk;

  // ports
  for (const auto& decl : gio->get_input_pin_decls()) {
    auto pin = g->get_input_pin(decl.name);
    if (pin.is_invalid()) {
      continue;
    }
    int bits = gu::bits_of(pin);
    if (bits <= 0) {
      bits = static_cast<int>(decl.bits);
    }
    bits = std::max(bits, 1);
    in_port_of.emplace(pin, static_cast<int>(d.in_names.size()));
    d.in_names.push_back(decl.name);
    d.in_bits.push_back(bits);
    trk.add_input(pin, bits);
  }
  for (const auto& decl : gio->get_output_pin_decls()) {
    d.out_names.push_back(decl.name);
    d.out_bits.push_back(std::max(static_cast<int>(decl.bits), 1));
  }

  // The width a driver pin carries, for tracker seeding.
  const auto width_of = [&](const hhds::Pin_class& p) -> int {
    const int b = gu::bits_of(p);
    return b > 0 ? b : 1;
  };
  const auto seed = [&](const hhds::Pin_class& p) {
    if (p.is_invalid() || p.is_const() || trk.has_pin(p)) {
      return;
    }
    // An operand nobody declared: a cell output seen before its producer, or
    // a native driver. Provisional bus identity; the producer's own entry
    // (add_scalar/add_opaque) replaces it when the walk reaches it.
    trk.add_input(p, width_of(p));
  };
  const auto mark_opaque = [&](const hhds::Node_class& node, const hhds::Pin_class& out, int inst, std::string_view port) {
    if (out.is_invalid()) {
      return;
    }
    trk.add_opaque(out, width_of(out));
    opaque_inst[out] = inst;
    opaque_port[out] = std::string{port};
    (void)node;
  };
  const auto driver_of_pid = [&](const hhds::Node_class& node, hhds::Port_id pid) -> hhds::Pin_class {
    for (const auto& e : node.inp_edges()) {
      if (e.sink.get_port_id() == pid) {
        return e.driver;
      }
    }
    return {};
  };

  // 1. classify nodes; trackable glue is replayed in a deferral loop.
  std::vector<hhds::Node_class> pending;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    const auto op = gu::type_op_of(node);
    if (op == Ntype_op::Sub) {
      auto io = node.get_subnode_io();
      if (!io) {
        continue;
      }
      const std::string tname{io->get_name()};
      if (is_tie_def(tname)) {
        auto out = node.get_driver_pin(io->get_output_pin_decls().empty() ? 0 : io->get_output_pin_decls().front().port_id);
        if (!out.is_invalid()) {
          trk.add_constant(out, 1);
          auto* cn  = Abc_NtkCreateNode(ntk);
          cn->pData = tname == "_const1_" ? Mio_LibraryReadConst1(R.mio) : Mio_LibraryReadConst0(R.mio);
          auto* net = Abc_NtkCreateNet(ntk);
          Abc_ObjAddFanin(net, cn);
          leaf_net[{out, 0}] = net;
        }
        continue;
      }
      if (R.dff_names.contains(tname)) {
        // d=1, clk=2, q=3 (liberty::create_dff_io)
        Latch l;
        l.node  = node;
        l.q     = node.get_driver_pin(3);
        l.d_drv = driver_of_pid(node, 1);
        if (!l.q.is_invalid()) {
          trk.add_scalar(l.q, 1);
        }
        latches.push_back(std::move(l));
        continue;
      }
      if (is_splitter_def(tname)) {
        // output b<k> (pid k+2) = input bit k of `a` (pid 1)
        auto a = driver_of_pid(node, 1);
        if (a.is_invalid()) {
          continue;
        }
        seed(a);
        for (const auto& od : io->get_output_pin_decls()) {
          auto out = node.get_driver_pin(od.port_id);
          if (out.is_invalid()) {
            continue;
          }
          const int k = static_cast<int>(od.port_id) - 2;
          trk.add_get_mask(out, a, width_of(a), *Dlop::get_mask_value(k, k));
        }
        continue;
      }
      if (auto* gate = Mio_LibraryReadGateByName(R.mio, const_cast<char*>(tname.c_str()), nullptr); gate != nullptr) {
        Cell c;
        c.node             = node;
        c.gate             = gate;
        const auto& odecls = io->get_output_pin_decls();
        c.out              = odecls.empty() ? hhds::Pin_class{} : node.get_driver_pin(odecls.front().port_id);
        if (c.out.is_invalid()) {
          // A cell with no consumer of its output (dangling): still a node.
          c.out = node.create_driver_pin(odecls.empty() ? hhds::Port_id{1} : odecls.front().port_id);
        }
        trk.add_scalar(c.out, 1);
        cells.push_back(std::move(c));
        continue;
      }
      // An instance of another def (region module, child wrapper, memory,
      // external IP): outputs are opaque sources, inputs are cuts.
      Instance inst;
      inst.node = node;
      inst.def  = tname;
      if (auto it = R.def_idx.find(tname); it != R.def_idx.end()) {
        inst.idx = it->second;
      }
      const int ii = static_cast<int>(d.insts.size());
      d.insts.push_back(inst);
      for (const auto& od : io->get_output_pin_decls()) {
        auto out = node.get_driver_pin(od.port_id);
        if (out.is_invalid()) {
          continue;
        }
        mark_opaque(node, out, ii, od.name);
      }
      for (const auto& id : io->get_input_pin_decls()) {
        auto drv = driver_of_pid(node, id.port_id);
        if (drv.is_invalid()) {
          continue;
        }
        const int w = std::max(static_cast<int>(id.bits), 1);
        for (int b = 0; b < w; ++b) {
          Terminal t;
          t.inst      = ii;
          t.inst_port = id.name;
          t.bit       = b;
          cut_ins.push_back({drv, b, t});
        }
      }
      continue;
    }
    if (op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Fflop || op == Ntype_op::Memory) {
      // Native state: every output bit is an opaque source; din/enable/reset
      // (and every memory input) are cuts of unknown load.
      for (const auto& out : node.out_pins()) {
        mark_opaque(node, out, -1, "");
      }
      for (const auto& e : node.inp_edges()) {
        const auto pid = e.sink.get_port_id();
        if (op != Ntype_op::Memory && (pid == 2 || pid == 0 || pid == 1 || pid == 6 || pid == 5)) {
          continue;  // clock / async / initial / posclk / negreset: not a data load
        }
        const int w = width_of(e.driver);
        for (int b = 0; b < w; ++b) {
          cut_ins.push_back({e.driver, b, Terminal{}});
        }
      }
      continue;
    }
    if (Ntype::is_pin_trackable(op)) {
      if (node.attr(livehd::attrs::native_comb_boundary).has()) {
        d.depth_complete = false;
        for (const auto& out : node.out_pins()) {
          mark_opaque(node, out, -1, "");
        }
        for (const auto& e : node.inp_edges()) {
          const int w = width_of(e.driver);
          for (int b = 0; b < w; ++b) {
            cut_ins.push_back({e.driver, b, Terminal{}});
          }
        }
        continue;
      }
      pending.push_back(node);
      continue;
    }
    if (op == Ntype_op::AttrSet || op == Ntype_op::Nconst) {
      continue;  // constants resolve through Pin_class::is_const
    }
    d.depth_complete = false;
    // Any other native node (an unmapped operator, a preserved SCC member):
    // an opaque box for the environment.
    for (const auto& out : node.out_pins()) {
      mark_opaque(node, out, -1, "");
    }
    for (const auto& e : node.inp_edges()) {
      const int w = width_of(e.driver);
      for (int b = 0; b < w; ++b) {
        cut_ins.push_back({e.driver, b, Terminal{}});
      }
    }
  }

  // 2. the glue, replayed until every operand is known (a preserved cycle
  // through glue alone is not representable: its members become opaque).
  const auto ready = [&](const hhds::Pin_class& p) { return p.is_invalid() || p.is_const() || trk.has_pin(p); };
  while (!pending.empty()) {
    std::vector<hhds::Node_class> deferred;
    bool                          progress = false;
    for (auto& node : pending) {
      const auto op  = gu::type_op_of(node);
      auto       out = node.get_driver_pin(0);
      if (out.is_invalid()) {
        progress = true;
        continue;  // nobody reads it
      }
      if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask || op == Ntype_op::SRA || op == Ntype_op::SHL
          || op == Ntype_op::Sext) {
        auto a = gu::get_driver_of_sink_name(node, "a");
        auto b = gu::get_driver_of_sink_name(node, op == Ntype_op::Get_mask || op == Ntype_op::Set_mask ? "mask" : "b");
        auto v = op == Ntype_op::Set_mask ? gu::get_driver_of_sink_name(node, "value") : hhds::Pin_class{};
        if (a.is_invalid() || b.is_invalid() || !b.is_const()) {
          d.depth_complete = false;
          for (const auto& o : node.out_pins()) {
            mark_opaque(node, o, -1, "");
          }
          progress = true;
          continue;
        }
        if (!ready(a) || (!v.is_invalid() && !ready(v))) {
          deferred.push_back(node);
          continue;
        }
        seed(a);
        const int a_bits = a.is_const() ? std::max(static_cast<int>(gu::const_of(a).get_bits()), 1) : width_of(a);
        if (a.is_const()) {
          trk.add_constant(a, a_bits);
        }
        const auto& k = gu::const_of(b);
        if (op == Ntype_op::Get_mask) {
          trk.add_get_mask(out, a, a_bits, k);
        } else if (op == Ntype_op::Set_mask) {
          if (v.is_const()) {
            trk.add_constant(v, std::max(static_cast<int>(k.get_bits()), 1));
          } else {
            seed(v);
          }
          trk.add_set_mask(out, a, a_bits, k, v);
        } else if (op == Ntype_op::SRA) {
          trk.add_sra(out, a, a_bits, k);
        } else if (op == Ntype_op::SHL) {
          trk.add_shl(out, a, a_bits, k);
        } else {
          trk.add_sext(out, a, a_bits, k);
        }
        progress = true;
        continue;
      }
      if (op == Ntype_op::Concat) {
        const auto lanes = gu::concat_lanes(node);
        if (lanes.empty() || !gu::concat_lane_violation(lanes).empty()) {
          for (const auto& o : node.out_pins()) {
            mark_opaque(node, o, -1, "");
          }
          progress = true;
          continue;
        }
        bool ok = true;
        for (const auto& l : lanes) {
          ok = ok && ready(l.value);
        }
        if (!ok) {
          deferred.push_back(node);
          continue;
        }
        std::vector<Pin_tracker<hhds::Pin_class>::Concat_src> srcs;
        for (const auto& l : lanes) {
          if (l.value.is_const()) {
            trk.add_constant(l.value, l.width);
          } else {
            seed(l.value);
          }
          srcs.push_back({l.value, l.value.is_const() ? l.width : width_of(l.value), l.width, l.offset});
        }
        trk.add_concat(out, srcs, gu::concat_total_width(lanes));
        progress = true;
        continue;
      }
      d.depth_complete = false;
      // And/Or glue (packed lane selects in native wiring): an opaque cut is
      // exact enough for a load model.
      for (const auto& o : node.out_pins()) {
        mark_opaque(node, o, -1, "");
      }
      for (const auto& e : node.inp_edges()) {
        const int w = width_of(e.driver);
        for (int b = 0; b < w; ++b) {
          cut_ins.push_back({e.driver, b, Terminal{}});
        }
      }
      progress = true;
    }
    if (!progress) {
      d.depth_complete = false;
      for (auto& node : deferred) {  // glue cycle: cut it
        for (const auto& o : node.out_pins()) {
          mark_opaque(node, o, -1, "");
        }
      }
      break;
    }
    pending.swap(deferred);
  }

  // 3. ABC objects for cells and latches (output nets first, fanins after
  // every leaf exists).
  for (auto& c : cells) {
    c.abc        = Abc_NtkCreateNode(ntk);
    c.abc->pData = c.gate;
    auto* net    = Abc_NtkCreateNet(ntk);
    Abc_ObjAddFanin(net, c.abc);
    leaf_net[{c.out, 0}] = net;
  }
  for (auto& l : latches) {
    auto* bo    = Abc_NtkCreateBo(ntk);
    auto* latch = Abc_NtkCreateLatch(ntk);
    auto* bi    = Abc_NtkCreateBi(ntk);
    Abc_ObjAddFanin(bo, latch);
    Abc_ObjAddFanin(latch, bi);
    Abc_LatchSetInitDc(latch);
    auto* qnet = Abc_NtkCreateNet(ntk);
    Abc_ObjAddFanin(qnet, bo);
    if (!l.q.is_invalid()) {
      leaf_net[{l.q, 0}] = qnet;
    }
    l.bi = bi;
  }
  Abc_Obj_t* const_net[2] = {nullptr, nullptr};
  const auto const_bit    = [&](bool one) -> Abc_Obj_t* {
    auto*& slot = const_net[one ? 1 : 0];
    if (slot == nullptr) {
      auto* cn  = Abc_NtkCreateNode(ntk);
      cn->pData = one ? Mio_LibraryReadConst1(R.mio) : Mio_LibraryReadConst0(R.mio);
      slot      = Abc_NtkCreateNet(ntk);
      Abc_ObjAddFanin(slot, cn);
    }
    return slot;
  };
  // A fresh PI for an external/opaque source bit.
  const auto new_pi = [&](const Pin_key& key, Terminal term) -> Abc_Obj_t* {
    auto* pi  = Abc_NtkCreatePi(ntk);
    auto* net = Abc_NtkCreateNet(ntk);
    Abc_ObjAddFanin(net, pi);
    const auto nm = std::format("__bpi{}", Abc_NtkPiNum(ntk) - 1);
    Abc_ObjAssignName(net, const_cast<char*>(nm.c_str()), nullptr);
    const int idx = Abc_NtkPiNum(ntk) - 1;
    if (term.port >= 0) {
      d.in_pi[{term.port, term.bit}] = idx;
    } else if (term.inst >= 0) {
      d.inst_out_pi[{term.inst, term.inst_port, term.bit}] = idx;
    }
    d.pi.push_back(std::move(term));
    leaf_net[key] = net;
    return net;
  };
  // Resolve (driver pin, bit) to the ABC net carrying it.
  const std::function<Abc_Obj_t*(const hhds::Pin_class&, int)> resolve = [&](const hhds::Pin_class& p, int bit) -> Abc_Obj_t* {
    if (p.is_invalid()) {
      return const_bit(false);
    }
    if (p.is_const()) {
      const auto& v = gu::const_of(p);
      return const_bit(v.bit_test(bit));
    }
    hhds::Pin_class leaf = p;
    int             lb   = bit;
    if (trk.has_pin(p)) {
      const auto& pv = trk.get_pin_vector(p);
      if (pv.empty()) {
        return const_bit(false);
      }
      const auto& pos = pv[static_cast<size_t>(std::min<int>(bit, static_cast<int>(pv.size()) - 1))];
      if (pos.pos < 0) {
        ++R.unresolved_bits;
        return const_bit(false);
      }
      leaf = pos.id();
      lb   = pos.pos;
      if (leaf.is_invalid()) {
        return const_bit(false);  // the tracker's zero pin
      }
      if (leaf.is_const()) {
        return const_bit(gu::const_of(leaf).bit_test(lb));
      }
    }
    if (auto it = leaf_net.find({leaf, lb}); it != leaf_net.end()) {
      return it->second;
    }
    Terminal t;
    if (auto it = in_port_of.find(leaf); it != in_port_of.end()) {
      t.port = it->second;
      t.bit  = lb;
      if (lb >= d.in_bits[static_cast<size_t>(t.port)]) {
        return const_bit(false);  // sign/zero fill above the port width
      }
    } else if (auto oi = opaque_inst.find(leaf); oi != opaque_inst.end()) {
      t.inst      = oi->second;
      t.inst_port = opaque_port[leaf];
      t.bit       = lb;
    } else {
      ++R.unresolved_bits;  // a cell output the walk never saw (should not happen)
    }
    return new_pi({leaf, lb}, std::move(t));
  };

  // 4. fanins
  for (auto& c : cells) {
    const auto& pins = R.pin_names(c.gate);
    for (size_t k = 0; k < pins.size(); ++k) {
      auto drv = driver_of_pid(c.node, static_cast<hhds::Port_id>(k + 1));
      Abc_ObjAddFanin(c.abc, resolve(drv, 0));
    }
    const auto id = static_cast<size_t>(Abc_ObjId(c.abc));
    if (d.node_sub.size() <= id) {
      d.node_sub.resize(id + 1);
    }
    d.node_sub[id] = c.node;
  }
  for (auto& l : latches) {
    Abc_ObjAddFanin(l.bi, resolve(l.d_drv, 0));
  }
  // 5. POs: output ports, then the cuts (instance inputs, native sinks).
  for (size_t o = 0; o < d.out_names.size(); ++o) {
    auto opin = g->get_output_pin(d.out_names[o]);
    if (opin.is_invalid()) {
      continue;
    }
    hhds::Pin_class drv;
    for (const auto& e : opin.inp_edges()) {
      drv = e.driver;
      break;
    }
    for (int b = 0; b < d.out_bits[o]; ++b) {
      auto* net = resolve(drv, b);
      auto* po  = Abc_NtkCreatePo(ntk);
      Abc_ObjAddFanin(po, net);
      Terminal t;
      t.port                = static_cast<int>(o);
      t.bit                 = b;
      d.out_po[{t.port, b}] = Abc_NtkPoNum(ntk) - 1;
      d.po.push_back(std::move(t));
    }
  }
  for (auto& ci : cut_ins) {
    auto* net = resolve(ci.drv, ci.bit);
    auto* po  = Abc_NtkCreatePo(ntk);
    Abc_ObjAddFanin(po, net);
    if (ci.term.inst >= 0) {
      d.inst_in_po[{ci.term.inst, ci.term.inst_port, ci.term.bit}] = Abc_NtkPoNum(ntk) - 1;
    }
    d.po.push_back(ci.term);
  }
  if (Abc_NtkPoNum(ntk) == 0) {
    auto* po = Abc_NtkCreatePo(ntk);
    Abc_ObjAddFanin(po, const_bit(false));
    d.po.push_back(Terminal{});
  }
  d.cells   = static_cast<int>(cells.size());
  d.latches = static_cast<int>(latches.size());
  {
    // Every net of an ABC netlist carries a name (the checker and the
    // logic conversion key on them); the PI nets got theirs above.
    Abc_Obj_t* net = nullptr;
    int        i   = 0;
    Abc_NtkForEachNet(ntk, net, i) {
      (void)Abc_ObjName(net);  // assigns a unique name to a still-unnamed net
    }
  }
  Abc_NtkFinalizeRead(ntk);
  if (!Abc_NtkCheck(ntk)) {
    return false;
  }
  d.imported = true;
  return true;
}

// ---- the join -------------------------------------------------------------

namespace {

// Internal load of PI `pi` of def `d` in fF: what its net drives inside the
// def -- cell pins, DFF D pins, and, recursively, whatever its cut POs feed
// (instance input ports, feed-throughs to the parents).
float pi_load(Refine& R, int di, int pi, std::vector<char>& visiting);

// External load presented to PO `po` of def `d`: over every parent instance,
// the parent's PI load of that instance output bit (the max over parents when
// a def is instantiated in several places); io_load at the top.
float po_ext_load(Refine& R, int di, int po, std::vector<char>& visiting) {
  auto&       d = *R.defs[static_cast<size_t>(di)];
  const auto& t = d.po[static_cast<size_t>(po)];
  if (t.port < 0) {
    return -1.0f;  // an internal cut, not an output port
  }
  float load = -1.0f;
  if (di == R.top) {
    load = R.io_load;
  }
  for (const auto& [pi_idx, inst_idx] : R.parents[static_cast<size_t>(di)]) {
    auto& p  = *R.defs[static_cast<size_t>(pi_idx)];
    auto  it = p.inst_out_pi.find({inst_idx, d.out_names[static_cast<size_t>(t.port)], t.bit});
    if (it == p.inst_out_pi.end()) {
      continue;  // that parent never reads this bit
    }
    load = std::max(load, pi_load(R, pi_idx, it->second, visiting));
  }
  return load;
}

float pi_load(Refine& R, int di, int pi, std::vector<char>& visiting) {
  auto& d = *R.defs[static_cast<size_t>(di)];
  if (!d.imported) {
    return R.typical_cap;
  }
  if (pi < 0 || static_cast<size_t>(pi) >= d.pi_load.size()) {
    return R.typical_cap;
  }
  if (d.pi_load[static_cast<size_t>(pi)] >= 0.0f) {
    return d.pi_load[static_cast<size_t>(pi)];
  }
  const size_t key = static_cast<size_t>(di);
  if (visiting[key] != 0) {
    return R.typical_cap;  // a feed-through cycle: bound it
  }
  visiting[key]   = 1;
  float      load = 0.0f;
  auto*      net  = Abc_ObjFanout0(Abc_NtkPi(d.ntk, pi));
  Abc_Obj_t* fo   = nullptr;
  int        k    = 0;
  Abc_ObjForEachFanout(net, fo, k) {
    if (Abc_ObjIsNode(fo)) {
      load += R.pin_cap(static_cast<Mio_Gate_t*>(fo->pData), Abc_NodeFindFanin(fo, net));
    } else if (Abc_ObjIsBi(fo)) {
      load += R.typical_cap;  // DFF D pin
    } else if (Abc_ObjIsPo(fo)) {
      const int po = d.po_of_obj[static_cast<size_t>(Abc_ObjId(fo))];
      if (po < 0) {
        load += R.typical_cap;
        continue;
      }
      const auto& t = d.po[static_cast<size_t>(po)];
      if (t.port >= 0) {
        const float ext  = po_ext_load(R, di, po, visiting);
        load            += ext >= 0.0f ? ext : 0.0f;
      } else if (t.inst >= 0) {
        const auto& inst = d.insts[static_cast<size_t>(t.inst)];
        if (inst.idx >= 0) {
          auto& c  = *R.defs[static_cast<size_t>(inst.idx)];
          int   ci = -1;
          for (size_t i = 0; i < c.in_names.size(); ++i) {
            if (c.in_names[i] == t.inst_port) {
              ci = static_cast<int>(i);
              break;
            }
          }
          auto it  = ci >= 0 ? c.in_pi.find({ci, t.bit}) : c.in_pi.end();
          load    += it != c.in_pi.end() ? pi_load(R, inst.idx, it->second, visiting) : 0.0f;
        } else {
          load += R.typical_cap;  // an opaque instance (memory, external IP)
        }
      } else {
        load += R.typical_cap;  // native sink
      }
    }
  }
  visiting[key]                      = 0;
  d.pi_load[static_cast<size_t>(pi)] = load;
  return load;
}

// The cell driving PO `po` of def `d` (nullptr: a latch, a constant, an
// unresolvable source), following feed-throughs upstream.
SC_Cell* po_driver(Refine& R, int di, int po, int depth);

SC_Cell* pi_driver(Refine& R, int di, int pi, int depth) {
  auto& d = *R.defs[static_cast<size_t>(di)];
  if (depth > 16) {
    return R.drive_cell;
  }
  const auto& t = d.pi[static_cast<size_t>(pi)];
  if (t.inst >= 0) {
    const auto& inst = d.insts[static_cast<size_t>(t.inst)];
    if (inst.idx < 0) {
      return R.drive_cell;
    }
    auto& c  = *R.defs[static_cast<size_t>(inst.idx)];
    int   co = -1;
    for (size_t i = 0; i < c.out_names.size(); ++i) {
      if (c.out_names[i] == t.inst_port) {
        co = static_cast<int>(i);
        break;
      }
    }
    auto it = co >= 0 ? c.out_po.find({co, t.bit}) : c.out_po.end();
    return it != c.out_po.end() ? po_driver(R, inst.idx, it->second, depth + 1) : R.drive_cell;
  }
  if (t.port >= 0) {
    // Driven from a parent: the first parent that feeds this instance's
    // input bit decides.
    for (const auto& [pi_idx, inst_idx] : R.parents[static_cast<size_t>(di)]) {
      auto& p  = *R.defs[static_cast<size_t>(pi_idx)];
      auto  it = p.inst_in_po.find({inst_idx, d.in_names[static_cast<size_t>(t.port)], t.bit});
      if (it == p.inst_in_po.end()) {
        continue;
      }
      return po_driver(R, pi_idx, it->second, depth + 1);
    }
    return R.drive_cell;  // a primary input of the design
  }
  return R.drive_cell;  // native state / opaque
}

SC_Cell* po_driver(Refine& R, int di, int po, int depth) {
  auto& d = *R.defs[static_cast<size_t>(di)];
  if (!d.imported) {
    return R.drive_cell;
  }
  auto* net = Abc_ObjFanin0(Abc_NtkPo(d.ntk, po));
  auto* drv = Abc_ObjFanin0(net);
  if (drv == nullptr) {
    return nullptr;
  }
  if (Abc_ObjIsNode(drv)) {
    auto* gate = static_cast<Mio_Gate_t*>(drv->pData);
    if (gate == Mio_LibraryReadConst0(R.mio) || gate == Mio_LibraryReadConst1(R.mio)) {
      return nullptr;
    }
    return R.sc_cell(gate);
  }
  if (Abc_ObjIsPi(drv)) {
    const int pi = d.pi_of_obj[static_cast<size_t>(Abc_ObjId(drv))];
    return pi >= 0 ? pi_driver(R, di, pi, depth + 1) : R.drive_cell;
  }
  return R.drive_cell;  // a latch output (a DFF cell) -- the stand-in stands for its Q drive
}

// SCL timing of a mapped logic network under the installed table.
std::optional<std::pair<float, double>> time_ntk(SC_Lib* scl, Abc_Ntk_t* ntk) {
  if (ntk == nullptr || !Abc_NtkIsMappedLogic(ntk)) {
    return std::nullopt;
  }
  auto*                                   t = ntk->nBarBufs2 > 0 ? Abc_NtkDupDfsNoBarBufs(ntk) : ntk;
  std::optional<std::pair<float, double>> out;
  if (Abc_SclCheckNtk(t, 0)) {
    auto* man = Abc_SclManStart(scl, t, 0, 1, 0.0f, 0);
    out       = std::pair{man->MaxDelay0, static_cast<double>(man->SumArea0)};
    Abc_SclManFree(man);
  }
  if (t != ntk) {
    Abc_NtkDelete(t);
  }
  return out;
}

}  // namespace

uint64_t Mapper::refine_boundaries(hhds::GraphLibrary& outlib, std::string_view top) {
  if (!opts_.boundary || !start() || !scl_lib_ok_ || !scl_timing_ok_) {
    return 0;
  }
  float delay_target = 0.0f;
  {
    char*       end = nullptr;
    const float t   = std::strtof(startup_opts_.delay.c_str(), &end);
    if (!startup_opts_.delay.empty() && end != startup_opts_.delay.c_str() && *end == '\0' && t > 0.0f) {
      delay_target = t;
    }
  }
  if (delay_target <= 0.0f) {
    return 0;  // nothing to size to
  }
  auto*  frame = static_cast<Abc_Frame_t*>(pabc_);
  Refine R(*this, outlib);
  R.frame       = frame;
  R.mio         = static_cast<Mio_Library_t*>(Abc_FrameReadLibGen());
  R.scl         = static_cast<SC_Lib*>(Abc_FrameReadLibScl());
  R.typical_cap = std::max(typical_cap_ff_, 0.0f);
  R.io_load     = opts_.io_load >= 0.0f ? opts_.io_load : R.typical_cap;
  R.drive_cell  = static_cast<SC_Cell*>(drive_cell_);
  R.pin_names   = [this](Mio_Gate_t* g) -> const std::vector<std::string>& { return cell_desc_for(g).input_names; };
  if (R.mio == nullptr || R.scl == nullptr) {
    return 0;
  }
  for (const auto& c : dff_ladder_) {
    R.dff_names.insert(c.name);
  }
  if (dff_.has_value()) {
    R.dff_names.insert(dff_->name);
  }

  // 1. the defs reachable from top, children after parents does not matter:
  // every def is imported before any environment is read.
  std::vector<std::string> order{std::string{top}};
  R.def_idx.emplace(std::string{top}, 0);
  for (size_t i = 0; i < order.size(); ++i) {
    auto io = outlib.find_io(order[i]);
    auto g  = io ? io->get_graph() : nullptr;
    auto d  = std::make_unique<Imp_def>();
    d->name = order[i];
    d->g    = g.get();
    R.defs.push_back(std::move(d));
    if (!g) {
      continue;
    }
    for (auto n : g->body().nodes(hhds::Node_order::forward)) {
      if (gu::type_op_of(n) != Ntype_op::Sub) {
        continue;
      }
      auto cio = n.get_subnode_io();
      if (!cio) {
        continue;
      }
      const std::string cname{cio->get_name()};
      if (R.def_idx.contains(cname) || is_splitter_def(cname) || is_tie_def(cname) || R.dff_names.contains(cname)
          || Mio_LibraryReadGateByName(R.mio, const_cast<char*>(cname.c_str()), nullptr) != nullptr) {
        continue;
      }
      if (!cio->get_graph()) {
        continue;  // bodyless: external IP / memory model -- opaque
      }
      R.def_idx.emplace(cname, static_cast<int>(order.size()));
      order.push_back(cname);
    }
  }
  R.top = 0;
  // Graphs pinned for the duration (find_io/get_graph hand out shared_ptrs).
  std::vector<std::shared_ptr<hhds::Graph>> pinned;
  for (auto& d : R.defs) {
    if (auto io = outlib.find_io(d->name)) {
      pinned.push_back(io->get_graph());
    }
  }
  // 2. import
  uint64_t imported_cells = 0;
  for (auto& d : R.defs) {
    if (d->g == nullptr) {
      continue;
    }
    if (!import_def(R, *d)) {
      if (d->ntk != nullptr) {
        Abc_NtkDelete(d->ntk);
        d->ntk = nullptr;
      }
      d->imported = false;
      livehd::diag::warn("pass.abc", "boundary-import", "internal")
          .msg(
              "pass.abc boundary: module '{}' could not be re-imported for the exact boundary re-size; its neighbours keep the "
              "estimate",
              d->name)
          .emit();
      continue;
    }
    imported_cells += static_cast<uint64_t>(d->cells);
    d->pi_of_obj.assign(static_cast<size_t>(Abc_NtkObjNumMax(d->ntk)), -1);
    d->po_of_obj.assign(static_cast<size_t>(Abc_NtkObjNumMax(d->ntk)), -1);
    Abc_Obj_t* po = nullptr;
    int        i  = 0;
    Abc_NtkForEachPo(d->ntk, po, i) { d->po_of_obj[static_cast<size_t>(Abc_ObjId(po))] = i; }
    Abc_Obj_t* pi = nullptr;
    Abc_NtkForEachPi(d->ntk, pi, i) { d->pi_of_obj[static_cast<size_t>(Abc_ObjId(pi))] = i; }
  }
  R.parents.assign(R.defs.size(), {});
  for (size_t di = 0; di < R.defs.size(); ++di) {
    auto& d = *R.defs[di];
    for (size_t ii = 0; ii < d.insts.size(); ++ii) {
      if (d.insts[ii].idx >= 0) {
        R.parents[static_cast<size_t>(d.insts[ii].idx)].emplace_back(static_cast<int>(di), static_cast<int>(ii));
      }
    }
  }

  // 3. size every def against its exact environment, in ROUNDS. A round
  // times every def under the current table (loads and drivers off the
  // netlists as they stand, plus the arrivals and downstream delays the
  // previous round propagated), re-sizes it to its budget, and records the
  // arrival at each output driver and the departure (longest path to any
  // endpoint) at each input. Between rounds those travel across the
  // hierarchy: a def input inherits the arrival of the parent-side driver, a
  // def output inherits the departure of every sink it feeds. One round
  // moves the budget one region hop, so a path through k regions needs k
  // rounds; `rounds` bounds it.
  uint64_t          resized  = 0;
  uint64_t          crossing = 0;
  std::vector<char> visiting(R.defs.size(), 0);
  for (auto& d : R.defs) {
    const size_t npi = static_cast<size_t>(d->ntk != nullptr ? Abc_NtkPiNum(d->ntk) : 0);
    const size_t npo = static_cast<size_t>(d->ntk != nullptr ? Abc_NtkPoNum(d->ntk) : 0);
    d->pi_arr.assign(npi, 0.0f);
    d->pi_dept.assign(npi, 0.0f);
    d->po_arr.assign(npo, 0.0f);
    d->po_delay.assign(npo, 0.0f);
  }
  const int rounds = std::max(opts_.boundary_rounds, 1);
  for (int round = 0; round < rounds; ++round) {
    for (auto& d : R.defs) {
      d->pi_load.assign(static_cast<size_t>(d->ntk != nullptr ? Abc_NtkPiNum(d->ntk) : 0), -1.0f);
    }
    crossing = 0;
    for (size_t di = 0; di < R.defs.size(); ++di) {
      auto& d = *R.defs[di];
      if (!d.imported) {
        continue;
      }
      if (d.cells == 0) {
        // Pure wiring (a wrapper): arrivals and departures pass straight
        // through; nothing to size.
        for (int i = 0; i < Abc_NtkPoNum(d.ntk); ++i) {
          auto* drv                        = Abc_ObjFanin0(Abc_ObjFanin0(Abc_NtkPo(d.ntk, i)));
          d.po_arr[static_cast<size_t>(i)] = 0.0f;
          if (drv != nullptr && Abc_ObjIsPi(drv)) {
            const int pi = d.pi_of_obj[static_cast<size_t>(Abc_ObjId(drv))];
            if (pi >= 0) {
              d.po_arr[static_cast<size_t>(i)] = d.pi_arr[static_cast<size_t>(pi)];
            }
          }
        }
        for (int i = 0; i < Abc_NtkPiNum(d.ntk); ++i) {
          float      dept = 0.0f;
          auto*      net  = Abc_ObjFanout0(Abc_NtkPi(d.ntk, i));
          Abc_Obj_t* fo   = nullptr;
          int        k    = 0;
          Abc_ObjForEachFanout(net, fo, k) {
            if (Abc_ObjIsPo(fo)) {
              const int po = d.po_of_obj[static_cast<size_t>(Abc_ObjId(fo))];
              if (po >= 0) {
                dept = std::max(dept, d.po_delay[static_cast<size_t>(po)]);
              }
            }
          }
          d.pi_dept[static_cast<size_t>(i)] = dept;
        }
        continue;
      }
      // Environment for this def.
      Boundary_table table(static_cast<size_t>(Abc_NtkPiNum(d.ntk)), static_cast<size_t>(Abc_NtkPoNum(d.ntk)));
      int            bits = 0;
      for (int i = 0; i < Abc_NtkPiNum(d.ntk); ++i) {
        const auto& t = d.pi[static_cast<size_t>(i)];
        if (t.port < 0 && t.inst < 0) {
          table.set_pi(static_cast<size_t>(i), R.drive_cell, -1.0f);  // native state: the stand-in
          continue;
        }
        table.set_pi(static_cast<size_t>(i), pi_driver(R, static_cast<int>(di), i, 0), d.pi_arr[static_cast<size_t>(i)]);
        ++bits;
      }
      for (int i = 0; i < Abc_NtkPoNum(d.ntk); ++i) {
        const auto& t = d.po[static_cast<size_t>(i)];
        float       load;
        if (t.port >= 0) {
          load = po_ext_load(R, static_cast<int>(di), i, visiting);
          ++bits;
        } else if (t.inst >= 0) {
          // an instance input inside this def: the child's port load
          const auto& inst = d.insts[static_cast<size_t>(t.inst)];
          load             = R.typical_cap;
          if (inst.idx >= 0) {
            auto& c  = *R.defs[static_cast<size_t>(inst.idx)];
            int   ci = -1;
            for (size_t k = 0; k < c.in_names.size(); ++k) {
              if (c.in_names[k] == t.inst_port) {
                ci = static_cast<int>(k);
                break;
              }
            }
            auto it = ci >= 0 ? c.in_pi.find({ci, t.bit}) : c.in_pi.end();
            load    = it != c.in_pi.end() ? pi_load(R, inst.idx, it->second, visiting) : 0.0f;
          }
          ++bits;
        } else {
          load = R.typical_cap;  // native sink (flop din, memory input)
        }
        table.set_po(static_cast<size_t>(i), load, d.po_delay[static_cast<size_t>(i)]);
      }
      crossing += static_cast<uint64_t>(bits);

      // The mapped logic network to size, in topological order (the SCL timer
      // walks objects in id order and refuses anything else -- the import
      // created cells in LGraph order). Netlist node -> logic node -> DFS copy
      // through the two pCopy links the conversions leave behind.
      auto* logic0 = Abc_NtkToLogic(d.ntk);
      if (logic0 == nullptr) {
        continue;
      }
      auto* logic = Abc_NtkDupDfs(logic0);
      struct Pair {
        Abc_Obj_t*       net_obj;
        Abc_Obj_t*       obj;
        hhds::Node_class sub;
      };
      std::vector<Pair> pairs;
      {
        Abc_Obj_t* obj = nullptr;
        int        i   = 0;
        Abc_NtkForEachNode(d.ntk, obj, i) {
          const auto id = static_cast<size_t>(Abc_ObjId(obj));
          if (id < d.node_sub.size() && !d.node_sub[id].is_invalid() && obj->pCopy != nullptr && obj->pCopy->pCopy != nullptr) {
            pairs.push_back({obj, obj->pCopy->pCopy, d.node_sub[id]});
          }
        }
      }
      Abc_NtkDelete(logic0);
      if (logic == nullptr) {
        continue;
      }
      const float budget = region_budget(delay_target, d.latches > 0);
      table.install();
      Abc_FrameReplaceCurrentNetwork(frame, logic);
      const auto before   = time_ntk(R.scl, logic);
      const int  budget_i = static_cast<int>(std::floor(budget));
      const auto cmd      = std::format("upsize -D {0}; dnsize -D {0}", budget_i);
      if (Cmd_CommandExecute(frame, cmd.c_str()) != 0) {
        livehd::diag::warn("pass.abc", "boundary-resize", "internal")
            .msg("pass.abc boundary: ABC sizing failed on module '{}': {}", d.name, cmd)
            .emit();
        table.uninstall();
        continue;
      }
      logic = Abc_FrameReadNtk(frame);
      // Time the sized network and read the port times for the next round:
      // the arrival at each output's driver (without the port's own downstream
      // delay) and the departure at each input (with it).
      std::optional<std::pair<float, double>> after;
      if (Abc_SclCheckNtk(logic, 0)) {
        auto* man      = Abc_SclManStart(R.scl, logic, 0, 1, budget, 0);
        // MaxDelay0 is clamped up to the target when the reverse pass runs;
        // the true worst arrival is the priority queue's head.
        after          = std::pair{Abc_SclReadMaxDelay(man), static_cast<double>(man->SumArea0)};
        Abc_Obj_t* obj = nullptr;
        int        i   = 0;
        Abc_NtkForEachPo(logic, obj, i) {
          if (i < Abc_NtkPoNum(d.ntk)) {
            d.po_arr[static_cast<size_t>(i)] = Abc_SclObjTimeMax(man, Abc_ObjFanin0(obj));
          }
        }
        Abc_NtkForEachPi(logic, obj, i) {
          if (i < Abc_NtkPiNum(d.ntk)) {
            const auto* dp                    = Abc_SclObjDept(man, obj);
            d.pi_dept[static_cast<size_t>(i)] = std::max(dp->rise, dp->fall);
          }
        }
        Abc_SclManFree(man);
      }
      table.uninstall();
      // write back
      uint64_t swapped = 0;
      for (auto& [nobj, lobj, sub] : pairs) {
        auto* gate = static_cast<Mio_Gate_t*>(lobj->pData);
        if (gate == nullptr) {
          continue;
        }
        nobj->pData = gate;  // keep the netlist in step for the neighbours' driver lookups
        auto cur    = sub.get_subnode_io();
        if (!cur || cur->get_name() == std::string_view{Mio_GateReadName(gate)}) {
          continue;
        }
        const auto& nd  = cell_desc_for(gate);
        // Same pins in the same order, or the Sub's existing pin ids would land
        // on the wrong ports.
        const auto& ind = cur->get_input_pin_decls();
        bool        ok  = ind.size() == nd.input_names.size();
        for (size_t k = 0; ok && k < ind.size(); ++k) {
          ok = ind[k].name == nd.input_names[k] && ind[k].port_id == static_cast<hhds::Port_id>(k + 1);
        }
        const auto& outd = cur->get_output_pin_decls();
        ok               = ok && outd.size() == 1 && outd.front().name == nd.output_name;
        if (!ok) {
          continue;
        }
        sub.set_subnode(nd.io);
        if (auto a = sub.attr(hhds::attrs::name); a.has()) {
          const std::string_view old = a.get();
          if (const auto us = old.find('_'); us != std::string_view::npos && old.starts_with("g")) {
            a.set(std::format("{}_{}", old.substr(0, us), nd.name));
          }
        }
        ++swapped;
      }
      resized += swapped;
      for (auto& q : qor_) {
        if (q.module != d.name) {
          continue;
        }
        q.boundary_bits     = bits;
        q.boundary_resized += static_cast<int>(swapped);
        if (before && round == 0) {
          q.boundary_delay_pre = before->first;
          q.boundary_area_pre  = before->second;
        }
        if (after) {
          q.delay = after->first;
          q.area  = after->second;
        }
        if (incr_ != nullptr) {
          incr_->refresh_qor(d.name, q);  // the cached body is the refined one
        }
        break;
      }
      if (opts_.verbose) {
        std::print(
            "[pass.abc] boundary round {}: module '{}': {} crossing bit(s), budget {:.0f} ps, {} -> {}, {} cell(s) re-sized\n",
            round,
            d.name,
            bits,
            budget,
            before ? std::format("{:.1f} ps / {:.2f}", before->first, before->second) : "untimed",
            after ? std::format("{:.1f} ps / {:.2f}", after->first, after->second) : "untimed",
            swapped);
      }
    }
    if (round + 1 == rounds) {
      break;
    }
    // Propagate the port times across the hierarchy for the next round.
    for (size_t di = 0; di < R.defs.size(); ++di) {
      auto& d = *R.defs[di];
      if (!d.imported) {
        continue;
      }
      for (int i = 0; i < Abc_NtkPiNum(d.ntk); ++i) {
        const auto& t   = d.pi[static_cast<size_t>(i)];
        float       arr = 0.0f;
        if (t.inst >= 0) {
          const auto& inst = d.insts[static_cast<size_t>(t.inst)];
          if (inst.idx >= 0) {
            auto& c  = *R.defs[static_cast<size_t>(inst.idx)];
            int   co = -1;
            for (size_t k = 0; k < c.out_names.size(); ++k) {
              if (c.out_names[k] == t.inst_port) {
                co = static_cast<int>(k);
                break;
              }
            }
            auto it = co >= 0 ? c.out_po.find({co, t.bit}) : c.out_po.end();
            if (it != c.out_po.end()) {
              arr = c.po_arr[static_cast<size_t>(it->second)];
            }
          }
        } else if (t.port >= 0) {
          for (const auto& [pi_idx, inst_idx] : R.parents[di]) {
            auto& p  = *R.defs[static_cast<size_t>(pi_idx)];
            auto  it = p.inst_in_po.find({inst_idx, d.in_names[static_cast<size_t>(t.port)], t.bit});
            if (it != p.inst_in_po.end()) {
              arr = std::max(arr, p.po_arr[static_cast<size_t>(it->second)]);
            }
          }
        }
        d.pi_arr[static_cast<size_t>(i)] = arr;
      }
      for (int i = 0; i < Abc_NtkPoNum(d.ntk); ++i) {
        const auto& t     = d.po[static_cast<size_t>(i)];
        float       delay = 0.0f;
        if (t.inst >= 0) {
          const auto& inst = d.insts[static_cast<size_t>(t.inst)];
          if (inst.idx >= 0) {
            auto& c  = *R.defs[static_cast<size_t>(inst.idx)];
            int   ci = -1;
            for (size_t k = 0; k < c.in_names.size(); ++k) {
              if (c.in_names[k] == t.inst_port) {
                ci = static_cast<int>(k);
                break;
              }
            }
            auto it = ci >= 0 ? c.in_pi.find({ci, t.bit}) : c.in_pi.end();
            if (it != c.in_pi.end()) {
              delay = c.pi_dept[static_cast<size_t>(it->second)];
            }
          }
        } else if (t.port >= 0) {
          for (const auto& [pi_idx, inst_idx] : R.parents[di]) {
            auto& p  = *R.defs[static_cast<size_t>(pi_idx)];
            auto  it = p.inst_out_pi.find({inst_idx, d.out_names[static_cast<size_t>(t.port)], t.bit});
            if (it != p.inst_out_pi.end()) {
              delay = std::max(delay, p.pi_dept[static_cast<size_t>(it->second)]);
            }
          }
        }
        d.po_delay[static_cast<size_t>(i)] = delay;
      }
    }
  }
  for (auto& d : R.defs) {
    if (d->ntk != nullptr) {
      Abc_NtkDelete(d->ntk);
      d->ntk = nullptr;
    }
  }
  std::print("pass.abc boundary: {} module(s) re-imported ({} cells), {} crossing bit(s), {} cell(s) re-sized{}\n",
             R.defs.size(),
             imported_cells,
             crossing,
             resized,
             R.unresolved_bits != 0 ? std::format(", {} unresolved bit(s)", R.unresolved_bits) : std::string{});
  return resized;
}

// Bit-accurate mapped-cell depth through the stitched occurrence hierarchy.
// It intentionally uses gate levels, not a sum of independent region delays.
Mapper::Ware_score Mapper::score_ware(hhds::GraphLibrary& outlib, std::string_view top) {
  if (!start()) {
    return {};
  }
  const uint64_t memory_entry  = cost::process_footprint_bytes();
  const uint64_t budget        = opts_.allow_oversize ? 0 : cost::budget_bytes(opts_.memory_budget_mb);
  const auto     within_budget = [&] {
    auto rss = cost::process_footprint_bytes();
    return budget == 0 || rss <= memory_entry || rss - memory_entry < budget;
  };
  uint64_t cells = 0;
  for (const auto& q : qor_) {
    cells += static_cast<uint64_t>(std::max(q.gates, 0));
  }
  // ABC netlist objects plus per-occurrence DFS arrays. Admission is repeated
  // during import; this estimate avoids allocating an obviously overlarge score.
  if (budget && cells > budget / 1024) {
    std::print("[pass.abc] ware: depth import estimate exceeds memory budget\n");
    return {};
  }
  auto*  frame = static_cast<Abc_Frame_t*>(pabc_);
  Refine R(*this, outlib);
  struct Cleanup {
    Refine& r;
    ~Cleanup() {
      for (auto& d : r.defs) {
        if (d->ntk) {
          Abc_NtkDelete(d->ntk);
        }
      }
    }
  } cleanup{R};
  R.frame       = frame;
  R.mio         = static_cast<Mio_Library_t*>(Abc_FrameReadLibGen());
  R.scl         = static_cast<SC_Lib*>(Abc_FrameReadLibScl());
  R.typical_cap = std::max(typical_cap_ff_, 0.0f);
  R.io_load     = opts_.io_load >= 0.0f ? opts_.io_load : R.typical_cap;
  R.drive_cell  = static_cast<SC_Cell*>(drive_cell_);
  R.pin_names   = [this](Mio_Gate_t* g) -> const std::vector<std::string>& { return cell_desc_for(g).input_names; };
  if (R.mio == nullptr) {
    return {};
  }
  for (const auto& c : dff_ladder_) {
    R.dff_names.insert(c.name);
  }
  if (dff_.has_value()) {
    R.dff_names.insert(dff_->name);
  }

  // 1. the defs reachable from top, children after parents does not matter:
  // every def is imported before any environment is read.
  std::vector<std::string> order{std::string{top}};
  R.def_idx.emplace(std::string{top}, 0);
  for (size_t i = 0; i < order.size(); ++i) {
    auto io = outlib.find_io(order[i]);
    auto g  = io ? io->get_graph() : nullptr;
    auto d  = std::make_unique<Imp_def>();
    d->name = order[i];
    d->g    = g.get();
    R.defs.push_back(std::move(d));
    if (!g) {
      return {};
    }
    for (auto n : g->body().nodes(hhds::Node_order::forward)) {
      if (gu::type_op_of(n) != Ntype_op::Sub) {
        continue;
      }
      auto cio = n.get_subnode_io();
      if (!cio) {
        continue;
      }
      const std::string cname{cio->get_name()};
      if (R.def_idx.contains(cname) || is_splitter_def(cname) || is_tie_def(cname) || R.dff_names.contains(cname)
          || Mio_LibraryReadGateByName(R.mio, const_cast<char*>(cname.c_str()), nullptr) != nullptr) {
        continue;
      }
      if (!cio->get_graph()) {
        continue;  // bodyless: external IP / memory model -- opaque
      }
      R.def_idx.emplace(cname, static_cast<int>(order.size()));
      order.push_back(cname);
    }
  }
  R.top = 0;
  // Graphs pinned for the duration (find_io/get_graph hand out shared_ptrs).
  std::vector<std::shared_ptr<hhds::Graph>> pinned;
  for (auto& d : R.defs) {
    if (auto io = outlib.find_io(d->name)) {
      pinned.push_back(io->get_graph());
    }
  }
  // 2. import

  for (auto& d : R.defs) {
    if (d->g == nullptr) {
      return {};
    }
    if (!within_budget()) {
      return {};
    }
    if (!import_def(R, *d)) {
      if (d->ntk != nullptr) {
        Abc_NtkDelete(d->ntk);
        d->ntk = nullptr;
      }
      d->imported = false;
      livehd::diag::warn("pass.abc", "boundary-import", "internal")
          .msg(
              "pass.abc boundary: module '{}' could not be re-imported for the exact boundary re-size; its neighbours keep the "
              "estimate",
              d->name)
          .emit();
      return {};
    }

    if (!d->depth_complete) {
      std::print("[pass.abc] ware: module '{}' has opaque combinational logic\n", d->name);
      return {};
    }
    d->pi_of_obj.assign(static_cast<size_t>(Abc_NtkObjNumMax(d->ntk)), -1);
    d->po_of_obj.assign(static_cast<size_t>(Abc_NtkObjNumMax(d->ntk)), -1);
    Abc_Obj_t* po = nullptr;
    int        i  = 0;
    Abc_NtkForEachPo(d->ntk, po, i) { d->po_of_obj[static_cast<size_t>(Abc_ObjId(po))] = i; }
    Abc_Obj_t* pi = nullptr;
    Abc_NtkForEachPi(d->ntk, pi, i) { d->pi_of_obj[static_cast<size_t>(Abc_ObjId(pi))] = i; }
  }
  // Instantiate contexts, not graph bodies. Sharing a definition must not
  // conflate two serial occurrences and create a false combinational cycle.
  struct Context {
    int                  def, parent = -1, instance = -1;
    std::vector<int>     children, depth;
    std::vector<uint8_t> state;
  };
  std::vector<Context> contexts;
  contexts.push_back({0, -1, -1, {}, {}, {}});
  for (size_t c = 0; c < contexts.size(); ++c) {
    if (!within_budget()) {
      return {};
    }
    auto& d = *R.defs[contexts[c].def];
    contexts[c].children.assign(d.insts.size(), -1);
    contexts[c].depth.assign(Abc_NtkObjNumMax(d.ntk), 0);
    contexts[c].state.assign(Abc_NtkObjNumMax(d.ntk), 0);
    for (size_t i = 0; i < d.insts.size(); ++i) {
      if (d.insts[i].idx < 0) {
        continue;
      }
      // Recursive hierarchy is not a finite combinational depth problem.
      for (int p = static_cast<int>(c); p >= 0; p = contexts[p].parent) {
        if (contexts[p].def == d.insts[i].idx) {
          return {};
        }
      }
      const int child         = static_cast<int>(contexts.size());
      contexts[c].children[i] = child;
      contexts.push_back({d.insts[i].idx, static_cast<int>(c), static_cast<int>(i), {}, {}, {}});
    }
  }
  using Key = std::pair<int, int>;  // occurrence context, ABC object id
  auto deps = [&](Key key) {
    std::vector<Key> result;
    auto&            c   = contexts[key.first];
    auto&            d   = *R.defs[c.def];
    auto*            obj = Abc_NtkObj(d.ntk, key.second);
    if (Abc_ObjIsBo(obj)) {
      return result;  // a register launches a new path
    }
    if (Abc_ObjIsPi(obj)) {
      const auto& t = d.pi[d.pi_of_obj[key.second]];
      if (t.inst >= 0 && c.children[t.inst] >= 0) {
        const int cc    = c.children[t.inst];
        auto&     child = *R.defs[contexts[cc].def];
        auto      p     = std::find(child.out_names.begin(), child.out_names.end(), t.inst_port);
        if (p != child.out_names.end()) {
          auto it = child.out_po.find({static_cast<int>(p - child.out_names.begin()), t.bit});
          if (it != child.out_po.end()) {
            result.emplace_back(cc, Abc_ObjId(Abc_NtkPo(child.ntk, it->second)));
          }
        }
      } else if (t.port >= 0 && c.parent >= 0) {
        auto& parent = *R.defs[contexts[c.parent].def];
        auto  it     = parent.inst_in_po.find({c.instance, d.in_names[t.port], t.bit});
        if (it != parent.inst_in_po.end()) {
          result.emplace_back(c.parent, Abc_ObjId(Abc_NtkPo(parent.ntk, it->second)));
        }
      }
      return result;
    }
    Abc_Obj_t* fi = nullptr;
    int        i  = 0;
    Abc_ObjForEachFanin(obj, fi, i) { result.emplace_back(key.first, Abc_ObjId(fi)); }
    return result;
  };
  auto weight = [&](Key k) {
    auto* obj = Abc_NtkObj(R.defs[contexts[k.first].def]->ntk, k.second);
    return Abc_ObjIsNode(obj) && Abc_ObjFaninNum(obj) > 0 ? 1 : 0;
  };
  struct Frame {
    Key              key;
    std::vector<Key> deps;
    size_t           next = 0;
    int              best = 0;
  };
  auto evaluate = [&](Key root) {
    std::vector<Frame> stack{
        {root, deps(root)}
    };
    while (!stack.empty()) {
      auto& f = stack.back();
      auto& c = contexts[f.key.first];
      if (c.state[f.key.second] == 2) {
        stack.pop_back();
        continue;
      }
      c.state[f.key.second] = 1;
      if (f.next < f.deps.size()) {
        auto  k  = f.deps[f.next];
        auto& dc = contexts[k.first];
        if (dc.state[k.second] == 1) {
          return false;
        }
        if (dc.state[k.second] == 0) {
          stack.push_back({k, deps(k)});
          continue;
        }
        f.best = std::max(f.best, dc.depth[k.second]);
        ++f.next;
      } else {
        c.depth[f.key.second] = f.best + weight(f.key);
        c.state[f.key.second] = 2;
        stack.pop_back();
      }
    }
    return true;
  };
  std::vector<Key> endpoints;
  for (size_t c = 0; c < contexts.size(); ++c) {
    auto&      d   = *R.defs[contexts[c].def];
    Abc_Obj_t* obj = nullptr;
    int        i   = 0;
    Abc_NtkForEachPo(d.ntk, obj, i) {
      const auto& t = d.po[i];
      if ((c == 0 && t.port >= 0) || (t.port < 0 && (t.inst < 0 || contexts[c].children[t.inst] < 0))) {
        endpoints.emplace_back(static_cast<int>(c), Abc_ObjId(obj));
      }
    }
    Abc_NtkForEachCo(d.ntk, obj, i) {
      if (Abc_ObjIsBi(obj)) {
        endpoints.emplace_back(static_cast<int>(c), Abc_ObjId(obj));
      }
    }
  }
  Ware_score score;
  int        worst = 0;
  for (auto k : endpoints) {
    if (!evaluate(k)) {
      std::print("[pass.abc] ware: combinational cycle in depth import\n");
      return {};
    }
    int depth = contexts[k.first].depth[k.second];
    score.endpoints.push_back(depth);
    worst = std::max(worst, depth);
  }
  // Union of ALL tied critical paths, including both reconvergent fanins.
  std::vector<Key> stack;
  for (auto k : endpoints) {
    if (contexts[k.first].depth[k.second] == worst) {
      stack.push_back(k);
    }
  }
  absl::flat_hash_set<Key> seen;
  while (!stack.empty()) {
    auto k = stack.back();
    stack.pop_back();
    if (!seen.insert(k).second) {
      continue;
    }
    if (weight(k)) {
      score.critical_regions.insert(R.defs[contexts[k.first].def]->name);
    }
    const int previous = contexts[k.first].depth[k.second] - weight(k);
    for (auto dep : deps(k)) {
      if (contexts[dep.first].depth[dep.second] == previous) {
        stack.push_back(dep);
      }
    }
  }
  std::sort(score.endpoints.begin(), score.endpoints.end(), std::greater<int>{});
  score.valid = R.unresolved_bits == 0;
  if (!score.valid) {
    std::print("[pass.abc] ware: {} unresolved imported bits\n", R.unresolved_bits);
  }
  return score;
}

}  // namespace livehd::abc
