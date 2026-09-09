// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "memory_module.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "attr_carry.hpp"
#include "diag.hpp"
#include "file_utils.hpp"
#include "graph_library_singleton.hpp"
#include "node_util.hpp"
#include "pass.hpp"

namespace gu = livehd::graph_util;
namespace fs = std::filesystem;
namespace livehd::abc {
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
      roots.emplace_back(fs::path(p) / "_main");
    }
  }
  auto exe = fs::path(file_utils::get_exe_path());
  for (int i = 0; i < 6 && !exe.empty(); ++i, exe = exe.parent_path()) {
    roots.push_back(exe);
    roots.push_back(exe / "lhd.runfiles/_main");
  }
  for (const auto& root : roots) {
    auto dir = root / "ware/rtl";
    if (fs::is_regular_file(dir / "cgen_memory_1rd_1wr.v")) {
      return fs::absolute(dir);
    }
  }
  diag::err("pass.abc", "memory-rtl", "io").msg("could not locate ware/rtl/cgen_memory_*.v for memory lowering").fatal();
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

std::shared_ptr<hhds::Graph> enclose(hhds::Graph& parent, const hhds::Node_class& mem, bool lower, const fs::path& scratch,
                                     const fs::path& rtl_dir) {
  auto edges = mem.inp_edges();
  std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.sink.get_port_id() < b.sink.get_port_id(); });
  std::map<int, bool>        read;
  std::map<int, std::string> port_names;
  hhds::Pin_class            clock;
  bool                       single_clock = true;
  for (const auto& e : edges) {
    const int pid = e.sink.get_port_id(), off = pid % Ntype::Memory_port_stride;
    if (off == 10) {
      read[pid / Ntype::Memory_port_stride] = !gu::const_of(e.driver).is_known_zero();
    }
    if (off == 2) {
      if (clock.is_invalid()) {
        clock = e.driver;
      } else if (clock != e.driver) {
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
  const auto base        = std::format("cgen_memory_{}rd_{}wr_{}_{}",
                                       nr,
                                       nw,
                                       lower ? "lowered" : "instance",
                                       std::format("{:016x}", name_hash(std::string(parent.get_name()) + "/" + source_name)));
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
  for (const auto& e : edges) {
    const int       pid = e.sink.get_port_id(), off = pid % Ntype::Memory_port_stride;
    // Configuration is specialized into the child. init may instead be a
    // runtime reset-value bus, so only a constant init is a parameter.
    const bool      config = off == 1 || off == 5 || off == 6 || off == 7 || off == 8 || off == 9 || off == 10 || off == 15
                             || (off == 11 && e.driver.is_const());
    hhds::Pin_class driver;
    if (config) {
      driver = gu::create_const(*body, gu::const_of(e.driver));
    } else {
      std::string pname;
      const auto  field = off == 0 ? "addr" : off == 2 ? "clock" : off == 3 ? "din" : off == 4 ? "enable" : "";
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
        io->set_bits(pname, std::max(gu::bits_of(e.driver), 1));
        io->set_unsign(pname, gu::is_unsign(e.driver));
        auto pin = body->get_input_pin(pname);
        shape(e.driver, pin);
        input_pins[pname] = pin;
        inputs.push_back({pname, e.driver});
      }
      // Specialize constant-address ports without deleting their interface.
      driver = e.driver.is_const() ? gu::create_const(*body, gu::const_of(e.driver)) : input_pins.at(pname);
    }
    driver.connect_sink(inner.create_sink_pin(e.sink.get_port_id()));
  }
  std::map<hhds::Port_id, hhds::Pin_class> douts;
  for (const auto& e : mem.out_edges()) {
    douts.emplace(e.driver.get_port_id(), e.driver);
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
      std::ofstream out(script);
      out << "read_slang --top " << name << " --no-proc --ignore-timing -I " << rtl_dir.string() << ' '
          << (dir / (name + ".v")).string() << '\n'
          << "hierarchy -top " << name << "\nflatten\nproc -ifx\nopt -nosdff\nmemory_map\nopt -nosdff\npmuxtree\nbmuxmap\n"
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
    Pass::eprp.run_method_now("pass.cprop", parsed, {});
    auto& imported = Hhds_graph_library::instance((dir / "lg").string());
    auto  result   = imported.find_io(name);
    if (!result || !result->get_graph()) {
      diag::err("pass.abc", "memory-lowering", "internal").msg("memory RTL lowering did not produce '{}'", name).fatal();
    }
    for (const auto node : result->get_graph()->body().nodes()) {
      // The parent instance carries the source memory name. Keep an entry's
      // local name _mem<N>, so hierarchical canonicalization yields the same
      // <memory>__mem<N> bank keys as the legacy flat lowering. LEC still proves
      // the transition relation; the names only propose its state pairing.
      if (gu::type_op_of(node) == Ntype_op::Flop) {
        constexpr std::string_view prefix   = "__lhdmem_h64617461_e.data[";  // cgen's inner `data` memory
        const auto                 old_name = gu::node_name_of(node);
        if (old_name.starts_with(prefix) && old_name.ends_with("]")) {
          const auto index = old_name.substr(prefix.size(), old_name.size() - prefix.size() - 1);
          if (!index.empty() && std::ranges::all_of(index, [](char c) { return c >= '0' && c <= '9'; })) {
            const auto local_name = "_mem" + std::string(index);
            node.attr(hhds::attrs::name).set(local_name);
            gu::set_pin_name(node.create_driver_pin(0), local_name);
          }
        }
      }
      if (gu::type_op_of(node) == Ntype_op::Memory) {
        diag::err("pass.abc", "memory-lowering", "internal").msg("memory RTL lowering retained a Memory in '{}'", name).fatal();
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

std::vector<std::shared_ptr<hhds::Graph>> build_memory_modules(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, bool lower,
                                                               uint64_t max_bits) {
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
      for (const auto& e : mem.inp_edges()) {
        auto off = e.sink.get_port_id() % Ntype::Memory_port_stride;
        if (off == 1 && e.driver.is_const()) {
          bits = gu::const_of(e.driver).to_just_i64();
        }
        if (off == 9 && e.driver.is_const()) {
          size = gu::const_of(e.driver).to_just_i64();
        }
        if (off == 7 && e.driver.is_const() && gu::const_of(e.driver).to_just_i64() == 2) {
          inline_array = true;
        }
        if (off == 12) {
          inline_array = true;
        }
      }
      for (const auto& e : mem.out_edges()) {
        if (e.driver.get_port_id() == Ntype::Memory_readall_pid) {
          inline_array = true;
        }
      }
      const bool oversized = max_bits && bits && size > max_bits / bits;
      if (lower && oversized) {
        diag::warn("pass.abc", "memory-max-bits", "unsupported")
            .msg("memory '{}': {} x {} = {} bits exceeds memory_max_bits={}; keeping its native instance",
                 gu::default_instance_name(mem),
                 size,
                 bits,
                 size * bits,
                 max_bits)
            .emit();
      }
      const bool map = lower && !oversized;
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
}  // namespace livehd::abc
