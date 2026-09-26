// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "memory_module.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "attr_carry.hpp"
#include "bus_name.hpp"
#include "diag.hpp"
#include "file_utils.hpp"
#include "graph_library_singleton.hpp"
#include "node_util.hpp"
#include "pass.hpp"

namespace gu = livehd::graph_util;
namespace fs = std::filesystem;
namespace livehd::synth {
namespace {

struct Scratch {
  fs::path path;
  Scratch() {
    auto  pattern = std::string{"/tmp/lhd-memory-XXXXXX"};
    auto* p       = mkdtemp(pattern.data());
    if (!p) {
      diag::err("pass.abc", "memory-scratch", "io").msg("could not create memory RTL scratch directory").fatal();
    }
    path = p;
  }
  ~Scratch() {
    if (std::getenv("LHD_MEMORY_KEEP_SCRATCH") != nullptr) {
      return;  // debugging: keep the generated RTL and yosys script for inspection
    }
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

// The files are runtime data in both source checkouts and installed/runfiles
// layouts. Use the same roots as inou.yosys's bundled-script resolver.
fs::path memory_rtl_dir() {
  std::vector<fs::path> roots{fs::current_path()};
  for (auto env : {"LIVEHD_SOURCE_ROOT", "BUILD_WORKSPACE_DIRECTORY", "RUNFILES_DIR", "TEST_SRCDIR"}) {
    if (const auto* p = std::getenv(env); p && *p) {
      roots.emplace_back(p);
      // livehd's own repo directory inside the runfiles tree. `_main` is the
      // ROOT module, so it is only right when livehd IS the root; as a
      // DEPENDENCY (lhdsuite, lhdtrack) the data lands under the repo name
      // instead, and the exe walk below cannot find it either because the
      // runfiles `lhd` is a symlink into bazel-out/external/<repo>, which holds
      // build outputs and no `ware/`. Same probe list as inou.yosys's bundled
      // script resolver, for the same reason.
      for (auto workspace : {"livehd+", "livehd", "_main"}) {
        roots.emplace_back(fs::path(p) / workspace);
      }
    }
  }
  auto exe = fs::path(file_utils::get_exe_path());
  for (int i = 0; i < 6 && !exe.empty(); ++i, exe = exe.parent_path()) {
    roots.push_back(exe);
    for (auto workspace : {"livehd+", "livehd", "_main"}) {
      roots.push_back(exe / "lhd.runfiles" / workspace);
    }
  }
  for (const auto& root : roots) {
    auto dir = root / "ware/rtl";
    if (fs::is_regular_file(dir / "cgen_memory_1rd_1wr.v")) {
      return fs::absolute(dir);
    }
  }
  // Name what was probed: the failure mode is a LAYOUT mismatch, and without
  // the list the next reader has to re-derive the whole root set by hand.
  std::string tried;
  for (const auto& root : roots) {
    if (!tried.empty()) {
      tried += ", ";
    }
    tried += (root / "ware/rtl").string();
  }
  diag::err("pass.abc", "memory-rtl", "io")
      .msg("could not locate ware/rtl/cgen_memory_*.v for memory lowering")
      .note(std::format("probed: {}", tried))
      .fatal();
  return {};
}

uint64_t name_hash(std::string_view text) {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char ch : text) {
    h = (h ^ ch) * 1099511628211ULL;
  }
  return h;
}

void shape(const hhds::Pin_class& from, const hhds::Pin_class& to) {
  gu::set_bits(to, std::max(gu::bits_of(from), 1));
  gu::is_unsign(from) ? gu::set_unsign(to) : gu::set_sign(to);
}

struct Connection {
  std::string     name;
  hhds::Pin_class pin;
};

// cgen wraps a stateful Memory in a `cgen_memory_*` instance whose storage
// array is named `data`; the instance name encodes the source Memory name
// (inou/cgen/cgen_verilog.cpp, decoded back in pass/lec/query.cpp).
constexpr std::string_view kEntryPrefix = "__lhdmem_h64617461_e.data[";
// The lowered storage array inside the memory instance: entry N is the flop
// `_mem[N]` (core/bus_name.hpp), so the whole-design name is `<memory>._mem[N]`.
constexpr std::string_view kStorageBus = "_mem";

bool all_digits(std::string_view v) {
  return !v.empty() && std::ranges::all_of(v, [](char c) { return c >= '0' && c <= '9'; });
}

// `__lhdmem_h64617461_e.data[<idx>][<hi>:<lo>]` -> (idx, [lo, hi+1)). A
// whole-word entry (`...data[<idx>]`) and every other name yield nullopt.
struct Lane_name {
  int64_t entry = 0;
  int     lo    = 0;
  int     hi    = 0;  // exclusive
};
std::optional<Lane_name> parse_entry_lane(std::string_view name) {
  if (!name.starts_with(kEntryPrefix) || !name.ends_with("]")) {
    return std::nullopt;
  }
  const auto rest = name.substr(kEntryPrefix.size(), name.size() - kEntryPrefix.size() - 1);  // "<idx>][<hi>:<lo>"
  const auto sep  = rest.find("][");
  if (sep == std::string_view::npos) {
    return std::nullopt;
  }
  const auto idx   = rest.substr(0, sep);
  const auto range = rest.substr(sep + 2);
  const auto colon = range.find(':');
  if (colon == std::string_view::npos || !all_digits(idx) || !all_digits(range.substr(0, colon))
      || !all_digits(range.substr(colon + 1))) {
    return std::nullopt;
  }
  Lane_name out;
  out.entry = std::stoll(std::string(idx));
  out.hi    = static_cast<int>(std::stoll(std::string(range.substr(0, colon)))) + 1;
  out.lo    = static_cast<int>(std::stoll(std::string(range.substr(colon + 1))));
  if (out.lo < 0 || out.hi <= out.lo) {
    return std::nullopt;
  }
  return out;
}

// yosys `memory_map` writes a byte-enabled entry with one $dffe per write lane
// (`data[N][15:8] <= ...`), so inou.yosys lands one PARTIAL-RANGE flop per lane
// (`<inst>.data[N][hi:lo]`) where a single-enable memory lands the whole word
// (`<inst>.data[N]`). Merge an entry's lanes back into ONE `bits`-wide flop --
// D is the lane data with each lane's own enable folded into a hold mux, Q is
// sliced back out to the lane's readers -- so the `_mem<N>` naming contract
// below holds for every write shape.
//
// Without it a byte-enabled memory keeps the yosys spelling, pass/lec's
// Memory <-> storage-bank bridge (find_mem_entry_bank) finds no bank, and the
// netlist's storage bits face the reference's array as INDEPENDENT free
// symbols: a read of a never-written entry then refutes an equivalent netlist
// at the first checked step (lhd/tests/lhd_synth_test.sh ram128_repro).
void merge_entry_lanes(hhds::Graph& g, int64_t mem_bits) {
  if (mem_bits <= 0) {
    return;
  }
  struct Lane {
    hhds::Node_class flop;
    int              lo = 0;
    int              hi = 0;
  };
  std::map<int64_t, std::vector<Lane>> by_entry;
  for (const auto node : g.body().nodes()) {
    if (gu::type_op_of(node) != Ntype_op::Flop) {
      continue;
    }
    if (auto ln = parse_entry_lane(gu::node_name_of(node))) {
      by_entry[ln->entry].push_back({node, ln->lo, ln->hi});
    }
  }
  for (auto& [entry, lanes] : by_entry) {
    std::ranges::sort(lanes, [](const Lane& a, const Lane& b) { return a.lo < b.lo; });
    // Merge only a TOTAL, gap-free tiling of the word at the declared widths:
    // a partially written entry holds fewer state bits than the Memory it came
    // from and has no honest whole-word flop to become.
    hhds::Pin_class clk, posclk;
    int             cursor   = 0;
    bool            ok       = true;
    bool            any_init = false;
    bool            all_init = true;
    for (size_t i = 0; i < lanes.size() && ok; ++i) {
      const auto& l = lanes[i];
      auto        q = l.flop.get_driver_pin(0);
      if (l.lo != cursor || q.is_invalid() || gu::bits_of(q) != l.hi - l.lo) {
        ok = false;
        break;
      }
      cursor = l.hi;
      // The whole-array reset is re-applied on the merged flop further down;
      // nothing may already carry one here.
      if (!gu::get_driver_of_sink_name(l.flop, "reset_pin").is_invalid()
          || !gu::get_driver_of_sink_name(l.flop, "async").is_invalid()) {
        ok = false;
        break;
      }
      auto lclk = gu::get_driver_of_sink_name(l.flop, "clock_pin");
      auto lpos = gu::get_driver_of_sink_name(l.flop, "posclk");
      if (lclk.is_invalid()) {
        ok = false;
        break;
      }
      if (i == 0) {
        clk    = lclk;
        posclk = lpos;
      } else if (clk != lclk || posclk != lpos) {
        ok = false;
        break;
      }
      auto init = gu::get_driver_of_sink_name(l.flop, "initial");
      if (init.is_invalid()) {
        all_init = false;
      } else {
        any_init = true;
        ok       = init.is_const();
      }
    }
    if (!ok || cursor != mem_bits || lanes.size() < 2 || (any_init && !all_init)) {
      continue;
    }
    auto merged = gu::create_typed_node(g, Ntype_op::Flop);
    auto q      = merged.create_driver_pin(0);
    gu::set_bits(q, static_cast<int>(mem_bits));
    gu::set_unsign(q);
    gu::setup_sink_by_name(merged, "clock_pin").connect_driver(clk);
    if (!posclk.is_invalid()) {
      gu::setup_sink_by_name(merged, "posclk").connect_driver(posclk);
    }
    if (any_init) {  // MSB-first, each lane's power-on value at its own offset
      std::string text(static_cast<size_t>(mem_bits), '0');
      for (const auto& l : lanes) {
        const auto& lane_init = gu::const_of(gu::get_driver_of_sink_name(l.flop, "initial"));
        for (int b = l.lo; b < l.hi; ++b) {
          const auto o                                = static_cast<size_t>(b - l.lo);
          text[static_cast<size_t>(mem_bits - 1 - b)] = lane_init.unknown_bit_test(o) ? '?' : lane_init.bit_test(o) ? '1' : '0';
        }
      }
      gu::setup_sink_by_name(merged, "initial")
          .connect_driver(gu::create_const(g, *Dlop::from_binary(text, /*unsigned_result=*/true)));
    }
    // Per lane: the merged Q sliced back out, and the lane's D with its own
    // enable folded in (the merged flop has ONE enable pin and the lanes do not
    // agree on it).
    std::vector<hhds::Pin_class> q_slice(lanes.size()), d_lane(lanes.size());
    for (size_t i = 0; i < lanes.size(); ++i) {
      const auto& l  = lanes[i];
      const int   w  = l.hi - l.lo;
      auto        gm = gu::create_typed_node(g, Ntype_op::Get_mask);
      livehd::graph_util::connect_mask_operands(gm, q, gu::create_const(g, gu::mask_window_const(l.lo, l.hi)));
      q_slice[i] = gm.create_driver_pin(0);
      gu::set_bits(q_slice[i], w);
      gu::set_unsign(q_slice[i]);
      auto din = gu::get_driver_of_sink_name(l.flop, "din");
      auto en  = gu::get_driver_of_sink_name(l.flop, "enable");
      if (en.is_invalid()) {
        d_lane[i] = din;
        continue;
      }
      auto mx = gu::create_typed_node(g, Ntype_op::Mux);  // Y = s ? p2 : p1
      gu::setup_sink_by_name(mx, "s").connect_driver(en);
      gu::setup_sink_by_name(mx, "p1").connect_driver(q_slice[i]);
      gu::setup_sink_by_name(mx, "p2").connect_driver(din);
      d_lane[i] = mx.create_driver_pin(0);
      gu::set_bits(d_lane[i], w);
      gu::set_unsign(d_lane[i]);
    }
    // Concat sinks are interleaved (value, width) pairs MSB-FIRST
    // (graph/node_util.hpp), created in descending pid order so hhds does not
    // rescan the growing pin list per pin.
    auto      cat = gu::create_typed_node(g, Ntype_op::Concat);
    const int n   = static_cast<int>(lanes.size());
    for (int i = n - 1; i >= 0; --i) {
      const auto slot = static_cast<size_t>(n - 1 - i);
      const auto pid  = static_cast<hhds::Port_id>(2 * i);
      gu::create_const(g, *Dlop::create_integer(lanes[slot].hi - lanes[slot].lo)).connect_sink(cat.create_sink_pin(pid + 1));
      d_lane[slot].connect_sink(cat.create_sink_pin(pid));
    }
    auto packed = cat.create_driver_pin(0);
    gu::set_bits(packed, static_cast<int>(mem_bits));
    gu::set_unsign(packed);
    gu::setup_sink_by_name(merged, "din").connect_driver(packed);
    // Hand every lane reader its slice of the merged Q, then drop the lane flop.
    for (size_t i = 0; i < lanes.size(); ++i) {
      auto                         lq = lanes[i].flop.get_driver_pin(0);
      std::vector<hhds::Pin_class> sinks;
      for (const auto& e : lq.out_edges()) {
        sinks.push_back(e.sink);
      }
      for (const auto& sink : sinks) {
        q_slice[i].connect_sink(sink);
      }
      lanes[i].flop.del_node();
    }
    merged.attr(hhds::attrs::name).set(std::format("{}{}]", kEntryPrefix, entry));  // the whole-word spelling
  }
}

std::shared_ptr<hhds::Graph> enclose(hhds::Graph& parent, const hhds::Node_class& mem, bool lower, const fs::path& scratch,
                                     const fs::path& rtl_dir) {
  std::map<int, bool>        read;
  std::map<int, std::string> port_names;
  hhds::Pin_class            clock;
  bool                       single_clock = true;
  // The vector this used to be stored into was never mutated through and the
  // walk below touches nothing in `parent`, so iterate the pins in place. Sink
  // pids stay ascending (the pin chain is sorted), which is what the Memory
  // port-stride arithmetic reads.
  for (const auto& in_pin : mem.inp_sorted_pins()) {
    const auto in_drv = in_pin.get_driver_pin();
    const int  pid = in_pin.get_port_id(), off = pid % Ntype::Memory_port_stride;
    if (off == 10) {
      read[pid / Ntype::Memory_port_stride] = !gu::const_of(in_drv).is_known_zero();
    }
    if (off == 2) {
      if (clock.is_invalid()) {
        clock = in_drv;
      } else if (clock != in_drv) {
        single_clock = false;
      }
    }
  }
  int nr = 0, nw = 0;
  for (const auto& [port, rd] : read) {
    port_names[port] = std::format("{}_{{}}_{}", rd ? "rd" : "wr", rd ? nr++ : nw++);
  }
  auto*      lib         = parent.get_io()->get_library();
  const auto source_name = gu::default_instance_name(mem);
  // Name the BLASTED form as the macro-instance form plus a `_blasted` suffix, so
  // the two are relatable by stripping that suffix instead of differing in the
  // middle of the name (it used to be `..._lowered_<hash>` vs `..._instance_<hash>`,
  // which share no usable prefix/suffix relation). Two things get easier:
  //   - LEC/semdiff can pair a bit-blasted implementation with the native memory it
  //     stands for, instead of seeing two unrelated defs;
  //   - a diff/incremental run can tell "same memory, different realization" from
  //     "different memory" by name alone.
  // The hash keys the parent/instance identity and stays where it was, so
  // uniqueness semantics are unchanged.
  const auto base        = std::format("cgen_memory_{}rd_{}wr_{}{}",
                                       nr,
                                       nw,
                                       std::format("{:016x}", name_hash(std::string(parent.get_name()) + "/" + source_name)),
                                       lower ? "_blasted" : "");
  auto       name        = base;
  for (unsigned i = 1; lib->find_io(name); ++i) {
    name = base + "_" + std::to_string(i);
  }

  hhds::GraphLibrary native;
  auto               io    = native.create_io(name);
  auto               body  = io->create_graph();
  auto               inner = gu::create_typed_node(*body, Ntype_op::Memory);
  inner.attr(hhds::attrs::name).set("data");
  if (auto a = mem.attr(attrs::memory_async_reset); a.has()) {
    inner.attr(attrs::memory_async_reset).set(a.get());
  }
  std::vector<Connection>                inputs, outputs;
  std::map<std::string, hhds::Pin_class> input_pins;
  hhds::Port_id                          next_pid = 1;
  // Whole-array reset of a LOWERED memory: kept as a module input and
  // re-applied on the storage flops after yosys `memory_map` (see below).
  hhds::Pin_class                        reset_input;
  std::string                            reset_input_name;
  std::optional<Dlop>                    init_const;
  int64_t                                mem_bits = 0;
  // Lazy over `mem`'s own pins: every mutation in this loop lands in the CHILD
  // graph (`body`, `inner`, `io`), never in the parent body being walked.
  for (const auto& in_pin : mem.inp_sorted_pins()) {
    const auto in_drv = in_pin.get_driver_pin();
    const int  pid = in_pin.get_port_id(), off = pid % Ntype::Memory_port_stride;
    // Configuration is specialized into the child. init may instead be a
    // runtime reset-value bus, so only a constant init is a parameter.
    const bool config = off == 1 || off == 5 || off == 6 || off == 7 || off == 8 || off == 9 || off == 10 || off == 15
                        || (off == 11 && in_drv.is_const());
    if (off == 1 && in_drv.is_const()) {
      mem_bits = gu::const_of(in_drv).to_just_i64();
    }
    if (off == 11 && in_drv.is_const()) {
      init_const = gu::const_of(in_drv);
    }
    hhds::Pin_class driver;
    std::string     pname;
    if (config) {
      driver = gu::create_const(*body, gu::const_of(in_drv));
    } else {
      const auto field = off == 0 ? "addr" : off == 2 ? "clock" : off == 3 ? "din" : off == 4 ? "enable" : "";
      if (off == 2 && single_clock) {
        pname = "clk";
      } else if (*field && port_names.contains(pid / Ntype::Memory_port_stride)) {
        pname = port_names.at(pid / Ntype::Memory_port_stride);
        pname.replace(pname.find("{}"), 2, field);
      } else {
        pname = std::format("memory_{}", pid);
      }
      if (!input_pins.contains(pname)) {
        io->add_input(pname, next_pid++);
        io->set_bits(pname, std::max(gu::bits_of(in_drv), 1));
        io->set_unsign(pname, gu::is_unsign(in_drv));
        auto pin = body->get_input_pin(pname);
        shape(in_drv, pin);
        input_pins[pname] = pin;
        inputs.push_back({pname, in_drv});
      }
      // Specialize constant-address ports without deleting their interface.
      driver = in_drv.is_const() ? gu::create_const(*body, gu::const_of(in_drv)) : input_pins.at(pname);
    }
    if (lower && off == 14 && !config) {
      // The whole-array `reset` (every `reg m:[N]T = <const>` with a reset)
      // is NOT handed to the inner memory: the shipped cgen_memory_* wrapper
      // -- which yosys `memory_map` turns into one storage flop per entry,
      // `data[N]`, the name the `_mem<N>` rename below keys on -- has no
      // reset port, and the inline reg-array form cgen emits for a reset
      // memory is sliced by yosys `proc` into anonymous `$auto$ff.cc` flops
      // that no longer name their entry (which breaks pass/lec's memory <->
      // storage-bank bridge). The `initial` pin still rides as the wrapper's
      // INIT (power-on contents); the reset is re-applied on the lowered
      // flops as reset_pin + initial + async -- exactly the flop a scalar
      // `reg` with a reset lowers to.
      reset_input      = driver;
      reset_input_name = pname;
      continue;
    }
    driver.connect_sink(inner.create_sink_pin(in_pin.get_port_id()));
  }
  // The memory's dout DRIVER PINS. The out-edge walk re-emplaced the same
  // pin once per consumer; out_sorted_pins() yields each exactly once.
  std::map<hhds::Port_id, hhds::Pin_class> douts;
  for (const auto& out_pin : mem.out_sorted_pins()) {
    douts.emplace(out_pin.get_port_id(), out_pin);
  }
  for (const auto& [pid, pin] : douts) {
    auto pname = pid == Ntype::Memory_readall_pid ? std::string("read_all") : std::format("rd_dout_{}", int(pid) - nw);
    io->add_output(pname, next_pid++);
    io->set_bits(pname, std::max(gu::bits_of(pin), 1));
    io->set_unsign(pname, gu::is_unsign(pin));
    auto driver = inner.create_driver_pin(pid);
    shape(pin, driver);
    driver.connect_sink(body->get_output_pin(pname));
    outputs.push_back({pname, pin});
  }

  if (lower) {
    // Use the SAME emitter as native generation, including shipped wrappers,
    // INIT, byte enables and collision policies. Only this one memory's body
    // is flattened and mapped to flops; no parent logic enters the operation.
    auto dir = scratch / name;
    fs::create_directories(dir);
    Eprp_var emit;
    emit.add(body);
    Pass::eprp.run_method_now("inou.cgen.verilog",
                              emit,
                              {
                                  {"odir", dir.string()}
    });
    auto script = dir / "lower.ys";
    {
      // `-keepdc` on the post-memory_map opt keeps EVERY entry's storage flop:
      // without it opt_dff folds an uninitialized entry whose next state is a
      // constant (an entry hard-wired by a clockless constant write port, e.g.
      // minion's prim_rf_2r1w_preview `assign rf_q[0] = '0`) into that
      // constant -- an X-refinement at power-on that drops the entry from the
      // `<mem>._mem[i]` storage bank, so pass/lec can no longer pair the bank
      // with the source Memory (find_mem_entry_bank needs every entry) and the
      // mapped netlist REFUTES on a read of a never-written entry. A scalar
      // constant-D reg keeps its flop the same way.
      std::ofstream out(script);
      out << "read_slang --top " << name << " --no-proc --ignore-timing -I " << rtl_dir.string() << ' '
          << (dir / (name + ".v")).string() << '\n'
          << "hierarchy -top " << name << "\nflatten\nproc -ifx\nopt -nosdff\nmemory_map\nopt -nosdff -keepdc\npmuxtree\nbmuxmap\n"
          << "yosys2lg -path {{path}}\n";
    }
    Eprp_var parsed;
    Pass::eprp.run_method_now("inou.yosys.tolg",
                              parsed,
                              {
                                  { "files", (dir / (name + ".v")).string()},
                                  {   "top",                           name},
                                  {  "path",          (dir / "lg").string()},
                                  {"script",                script.string()}
    });
    Pass::eprp.run_method_now("pass.cprop", parsed, {});
    Pass::eprp.run_method_now("pass.bitwidth", parsed, {});
    Pass::eprp.run_method_now("pass.enableopt", parsed, {});
    Pass::eprp.run_method_now("pass.cprop", parsed, {});
    Pass::eprp.run_method_now("pass.bitwidth", parsed, {});
    auto& imported = Hhds_graph_library::instance((dir / "lg").string());
    auto  result   = imported.find_io(name);
    if (!result || !result->get_graph()) {
      diag::err("pass.abc", "memory-lowering", "internal").msg("memory RTL lowering did not produce '{}'", name).fatal();
    }
    // A byte-enabled write leaves one partial-range flop per lane; fold each
    // entry back into the whole-word flop the rename below expects.
    merge_entry_lanes(*result->get_graph(), mem_bits);
    for (const auto node : result->get_graph()->body().nodes()) {
      // The parent instance carries the source memory name. An entry's local
      // name is entry N of the storage bus `_mem` (core/bus_name.hpp):
      // `_mem[N]`, so the hierarchical name is `<memory>._mem[N]` and a
      // bit-blasted entry's DFF cells are `<memory>._mem[N][b]`. LEC still
      // proves the transition relation; the names only propose its state
      // pairing (pass/lec find_mem_entry_bank).
      if (gu::type_op_of(node) == Ntype_op::Flop) {
        const auto  old_name = gu::node_name_of(node);
        std::string index;
        if (old_name.starts_with(kEntryPrefix) && old_name.ends_with("]")) {
          index = old_name.substr(kEntryPrefix.size(), old_name.size() - kEntryPrefix.size() - 1);
        } else {
          // read_all uses cgen's packed inline array. Yosys splits its Q bus
          // into entry-width slices; retain that exact offset instead of
          // naming every slice after the same packed bus.
          constexpr std::string_view packed = "__lhdmem_h64617461_e_data[";
          if (old_name.starts_with(packed) && old_name.ends_with("]") && mem_bits > 0) {
            auto range  = old_name.substr(packed.size(), old_name.size() - packed.size() - 1);
            auto colon  = range.find(':');
            auto digits = [](std::string_view v) {
              return !v.empty() && std::ranges::all_of(v, [](char c) { return c >= '0' && c <= '9'; });
            };
            if (colon != std::string_view::npos && digits(range.substr(0, colon)) && digits(range.substr(colon + 1))) {
              const auto hi = std::stoll(std::string(range.substr(0, colon)));
              const auto lo = std::stoll(std::string(range.substr(colon + 1)));
              if (hi - lo + 1 == mem_bits && lo % mem_bits == 0) {
                index = std::to_string(lo / mem_bits);
              }
            }
          }
        }
        if (!index.empty() && std::ranges::all_of(index, [](char c) { return c >= '0' && c <= '9'; })) {
          const auto local_name = livehd::bus_name::entry(kStorageBus, std::stoll(index));
          node.attr(hhds::attrs::name).set(local_name);
          gu::set_pin_name(node.create_driver_pin(0), local_name);
        }
      }
      if (gu::type_op_of(node) == Ntype_op::Memory) {
        diag::err("pass.abc", "memory-lowering", "internal").msg("memory RTL lowering retained a Memory in '{}'", name).fatal();
      }
    }
    if (!reset_input_name.empty()) {
      // Re-apply the whole-array reset on the lowered storage flops (see the
      // edge loop above): entry N resets to lane N of the init contents (0
      // when the memory has none), asynchronously when the memory says so.
      auto lowered = result->get_graph();
      auto rst     = lowered->get_input_pin(reset_input_name);
      if (rst.is_invalid()) {
        diag::err("pass.abc", "memory-lowering", "internal")
            .msg("memory RTL lowering of '{}' lost its reset input '{}'", name, reset_input_name)
            .fatal();
      }
      const bool async_reset = [&] {
        auto a = mem.attr(attrs::memory_async_reset);
        return a.has() && a.get() != 0;
      }();
      const auto initial_pid = Ntype::get_sink_pid(Ntype_op::Flop, "initial");
      for (const auto node : lowered->body().nodes()) {
        if (gu::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        const auto entry = livehd::bus_name::parse_bus_piece(gu::node_name_of(node));
        if (!entry || entry->base != kStorageBus) {
          continue;
        }
        const int64_t index = entry->index;
        // memory_map seeded the power-on value from INIT on the `initial`
        // sink; the same pin is the reset value on a reset flop, so redrive it
        // with the entry's lane (they agree by construction).
        // SNAPSHOT, not the lazy view: the body DELETES the driver it finds,
        // and del_sink() is a structural mutation that would invalidate a live
        // pin iterator. The snapshot is a plain vector of handles, and only the
        // EDGE is removed (never the pin or the node), so the remaining
        // elements stay valid -- which is why the old collect-then-delete
        // second pass is no longer needed. One driver per sink pin, so dropping
        // the pin's driver IS dropping the edge this used to delete.
        for (const auto& in_pin : node.inp_pins_snapshot()) {
          if (!in_pin.is_invalid() && in_pin.get_port_id() == initial_pid) {
            in_pin.del_sink();
          }
        }
        Dlop lane = *Dlop::create_integer(0);
        if (init_const && mem_bits > 0) {
          lane = *init_const->get_mask_op_opt(static_cast<int>(index * mem_bits), static_cast<int>((index + 1) * mem_bits));
        }
        gu::create_const(*lowered, lane).connect_sink(gu::setup_sink_by_name(node, "initial"));
        rst.connect_sink(gu::setup_sink_by_name(node, "reset_pin"));
        if (async_reset) {
          gu::create_const(*lowered, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_by_name(node, "async"));
        }
      }
    }
    if (!lib->copy_from(imported, name)) {
      diag::err("pass.abc", "memory-copy", "internal").msg("could not copy lowered memory {}", name).fatal();
    }
    imported.delete_graphio(name);
  } else {
    if (!lib->copy_from(native, name)) {
      diag::err("pass.abc", "memory-copy", "internal").msg("could not copy native memory {}", name).fatal();
    }
  }
  auto child_io = lib->find_io(name);
  auto child    = child_io->get_graph();
  child->get_input_node().attr(attrs::memory_module).set(1);
  auto inst = gu::create_typed_node(parent, Ntype_op::Sub);
  inst.set_subnode(child_io);
  gu::carry_node_attrs(mem, inst);
  gu::carry_srcid(mem, inst);
  inst.attr(hhds::attrs::name).set(source_name);
  for (const auto& c : inputs) {
    c.pin.connect_sink(inst.create_sink_pin(child_io->get_input_port_id(c.name)));
  }
  for (const auto& c : outputs) {
    auto driver = inst.create_driver_pin(child_io->get_output_port_id(c.name));
    shape(c.pin, driver);
    std::vector<hhds::Pin_class> sinks;
    for (const auto& e : c.pin.out_edges()) {
      sinks.push_back(e.sink);
    }
    for (const auto& sink : sinks) {
      driver.connect_sink(sink);
    }
  }
  mem.del_node();
  return child;
}
}  // namespace

std::vector<std::shared_ptr<hhds::Graph>> build_memory_modules(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                                               Memory_fold mode, uint64_t max_bits) {
  std::vector<std::shared_ptr<hhds::Graph>> modules;
  std::unique_ptr<Scratch>                  scratch;
  fs::path                                  rtl;
  for (const auto& graph : graphs) {
    if (!graph || graph->get_input_node().attr(attrs::memory_module).has()) {
      continue;
    }
    std::vector<hhds::Node_class> memories;
    for (const auto node : graph->body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Memory) {
        memories.push_back(node);
      }
    }
    for (const auto& mem : memories) {
      // Assign the fallback before either realization can clone/re-number it.
      if (!gu::has_name(mem)) {
        mem.attr(hhds::attrs::name).set(gu::default_instance_name(mem));
      }
      uint64_t bits = 0, size = 0;
      bool     inline_array = false;
      // Every read/write port block carries exactly one address pin (offset 0
      // of its Memory_port_stride block), so counting those edges counts ports.
      uint64_t ports        = 0;
      for (const auto& in_pin : mem.inp_sorted_pins()) {
        const auto in_drv = in_pin.get_driver_pin();
        auto       off    = in_pin.get_port_id() % Ntype::Memory_port_stride;
        if (off == 0) {
          ++ports;
        }
        if (off == 1 && in_drv.is_const()) {
          bits = gu::const_of(in_drv).to_just_i64();
        }
        if (off == 9 && in_drv.is_const()) {
          size = gu::const_of(in_drv).to_just_i64();
        }
        if (off == 7 && in_drv.is_const() && gu::const_of(in_drv).to_just_i64() == 2) {
          inline_array = true;
        }
        if (off == 12) {
          inline_array = true;
        }
        if (off == 14) {
          // A whole-array `reset` (every `reg m:[N]T = <const>` with a reset)
          // has no cgen_memory_* wrapper either: cgen emits it as an inline
          // reg array, so it takes the same instance-module boundary as a
          // bulk-update cell whether or not it is bit-blasted.
          inline_array = true;
        }
      }
      for (const auto& out_pin : mem.out_sorted_pins()) {  // a port test: pins, not consumers
        if (out_pin.get_port_id() == Ntype::Memory_readall_pid) {
          inline_array = true;
        }
      }
      // The division form never overflows; an unknown `bits` is not oversized.
      const bool oversized  = max_bits && bits && size > max_bits / bits;
      const bool many_ports = ports > kAutoFoldPortsAbove;
      bool       map        = mode == Memory_fold::Always;
      if (mode == Memory_fold::Auto) {
        // `many_ports` deliberately OVERRIDES memory_max_bits. It is a
        // MACRO-REALIZABILITY fact, not a budget: ware/rtl ships cgen_memory_*
        // only up to 4rd_2wr, so there is no macro for an over-3-port array and
        // keeping it native emits an instantiation of a module that does not
        // exist. Measured 2026-09-16: dropping this override made dino's
        // netlist reference `cgen_memory_4rd_32wr_nofwd`, which is not in
        // ware/rtl -- an unsynthesizable netlist.
        //
        // The cost is real and is NOT fixed here: folding dino's 32x64 36-port
        // regfile bit-blasts it into ~15409 nodes, and because
        // //bench:dino_synth_lec_synth compares the pre-synthesis design (native
        // Memory) against the mapped netlist (bit-blasted), LEC sees ~2048
        // unmatched state cuts and goes UNKNOWN in 3042s where memory=false
        // PROVES in 145s. That asymmetry belongs to pass/lec -- it should pair a
        // native memory with the `_blasted` realization that stands for it --
        // not to this predicate. Do not "fix" the LEC symptom by making the
        // netlist unimplementable.
        map = !oversized || many_ports;
        if (map && oversized) {
          // Folded on port count alone. Say so: the storage is above the
          // threshold the user set, and this is the reason it folded anyway.
          diag::info("pass.abc", "memory-ports", "unsupported")
              .msg(
                  "memory '{}': {} ports (over {}) has no macro realization; bit-blasting its {} x {} = {} bits despite "
                  "memory_max_bits={}",
                  gu::default_instance_name(mem),
                  ports,
                  kAutoFoldPortsAbove,
                  size,
                  bits,
                  size * bits,
                  max_bits)
              .emit();
        } else if (!map) {
          // The documented `auto` outcome, so a note and not a warning -- the
          // user just needs to see WHICH memory it was to raise the limit (or
          // pass memory=true) deliberately.
          diag::info("pass.abc", "memory-max-bits", "unsupported")
              .msg(
                  "memory '{}': {} x {} = {} bits exceeds memory_max_bits={}; keeping its native instance (memory=true folds "
                  "it anyway)",
                  gu::default_instance_name(mem),
                  size,
                  bits,
                  size * bits,
                  max_bits)
              .emit();
        }
      }
      if (!map && !inline_array) {
        continue;
      }
      if (map && !scratch) {
        scratch = std::make_unique<Scratch>();
        fs::create_directory_symlink(memory_rtl_dir(), scratch->path / "rtl");
        rtl = scratch->path / "rtl";
      }
      modules.push_back(enclose(*graph, mem, map, scratch ? scratch->path : fs::path{}, rtl));
    }
  }
  return modules;
}
}  // namespace livehd::synth
