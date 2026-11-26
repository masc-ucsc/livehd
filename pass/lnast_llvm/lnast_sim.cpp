//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lnast_sim.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cpp_ident.hpp"
#include "debug_env.hpp"
#include "file_name.hpp"
#include "lnast.hpp"
#include "lnast_llvm.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "pass_lg2lnast.hpp"
#include "sim_tune_rt.hpp"
#include "upass_aeg.hpp"
#include "width_validation.hpp"

namespace livehd::lnast_llvm {

namespace {

namespace fs = std::filesystem;

// Generated files are build inputs keyed on mtime: leave an unchanged one alone.
bool write_if_different(const std::string& path, std::string_view text) {
  {
    std::ifstream in(path, std::ios::binary);
    if (in) {
      std::stringstream old;
      old << in.rdbuf();
      if (old.str() == text) {
        return true;
      }
    }
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  out.close();
  return out.good();
}

}  // namespace

Sim_emitted emit_sim(const Sim_request& request) {
  Sim_emitted result;
  // LHD_SIM_LNAST_TIME (debug): where the time before and after the kernels goes.
  const bool  timed = livehd::debug::env<"LHD_SIM_LNAST_TIME">() != nullptr;
  auto        mark  = std::chrono::steady_clock::now();
  const auto  lap   = [&](const char* what) {
    if (timed) {
      const auto now = std::chrono::steady_clock::now();
      std::fprintf(stderr, "lnast_sim: %-34s %6.2f s\n", what, std::chrono::duration<double>(now - mark).count());
      mark = now;
    }
  };
  const auto  fail = [&](std::string why) {
    result.ok  = false;
    result.why = std::move(why);
    return result;
  };

  // The lowering splices instances that sit on an instance-level cycle into their parent, so it
  // works on a private copy of the library (the caller's graphs go on to the compile cache).
  hhds::GraphLibrary                        library;
  std::vector<std::shared_ptr<hhds::Graph>> graphs;
  absl::flat_hash_set<const hhds::Graph*>   visited;
  std::vector<std::shared_ptr<hhds::Graph>> definitions;
  for (const auto& source : request.graphs) {
    if (!source) {
      continue;
    }
    auto  source_io      = source->get_io();
    auto* source_library = source_io ? source_io->get_library() : nullptr;
    if (source_library == nullptr) {
      return fail(std::format("graph '{}' has no library", std::string(source->get_name())));
    }
    for (const auto& graph : source->definitions().graphs()) {
      if (!visited.insert(graph.get()).second) {
        continue;
      }
      livehd::graph_util::require_sized_definition(*graph, "pass.lnast_sim");
      definitions.push_back(graph);
      if (library.find_io(graph->get_name())) {
        continue;
      }
      if (!library.copy_from(*source_library, graph->get_name())) {
        return fail(std::format("could not copy '{}' into the simulator's private library", std::string(graph->get_name())));
      }
    }
  }
  // A body-less cell (an fproperty/lgassert/lgundef marker) is only a declaration, which copy_from
  // does not bring along: declare it here so its instances still resolve by name.
  for (const auto& graph : definitions) {
    for (const auto node : graph->body().nodes()) {
      if (livehd::graph_util::type_op_of(node) == Ntype_op::Sub) {
        if (auto io = node.get_subnode_io(); io && !library.find_io(io->get_name())) {
          (void)library.create_io(io->get_name());
        }
      }
    }
  }
  for (const auto gid : library.all_gids()) {
    if (auto g = library.get_graph(gid)) {
      graphs.push_back(std::move(g));
    }
  }

  lap("private library copy");
  // A compact loop whose body holds state (N engines written as a loop over one module) has no `for`
  // spelling: every trip owns its registers. Expand it into its N ordinary instances here, in the
  // private library; a stateless loop stays one rolled `for`.
  {
    absl::flat_hash_map<const hhds::Graph*, bool> memo;
    std::function<bool(const hhds::Graph*)>       stateful = [&](const hhds::Graph* g) -> bool {
      if (auto it = memo.find(g); it != memo.end()) {
        return it->second;
      }
      memo[g]  = false;  // a recursive definition adds nothing
      bool any = false;
      for (const auto node : g->body().nodes()) {
        const auto op = livehd::graph_util::type_op_of(node);
        if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Clock_cell) {
          any = true;
        } else if (op == Ntype_op::Sub) {
          if (auto child = node.get_subnode_graph()) {
            any = any || stateful(child.get());
          }
        }
        if (any) {
          break;
        }
      }
      return memo[g] = any;
    };
    for (bool changed = true; changed;) {
      changed = false;
      for (const auto& g : graphs) {
        std::vector<hhds::Node_class> loops;
        for (const auto node : g->body().nodes()) {
          if (node.subnode_loop()) {
            if (auto body = node.get_subnode_graph(); body && stateful(body.get())) {
              loops.push_back(node);
            }
          }
        }
        for (auto it = loops.rbegin(); it != loops.rend(); ++it) {
          if (!livehd::graph_util::materialize_occurrence(g.get(), *it, "pass.lnast_sim")) {
            return fail(std::format("{}: a compact loop with state in its body could not be expanded", std::string(g->get_name())));
          }
          changed = true;
        }
      }
    }
  }

  lap("expand stateful loops");
  // The DUT root: the named top, else the one definition nothing instantiates (a `%`-named graph
  // is a lowered `test` block, not hardware).
  absl::flat_hash_set<std::string> instantiated;
  for (const auto& g : graphs) {
    for (const auto node : g->body().nodes()) {
      if (livehd::graph_util::type_op_of(node) == Ntype_op::Sub) {
        if (auto child = node.get_subnode_graph()) {
          instantiated.insert(std::string(child->get_name()));
        }
      }
    }
  }
  const auto entity_of = [](std::string_view full) {
    const auto dot = full.rfind('.');
    return dot == std::string_view::npos ? full : full.substr(dot + 1);
  };
  std::vector<std::string> roots;
  for (const auto& g : graphs) {
    const std::string full(g->get_name());
    const auto        entity = entity_of(full);
    if (!entity.empty() && entity.front() == '%') {
      continue;
    }
    const bool selected = !request.top.empty()    ? request.top == full || request.top == entity
                          : !request.duts.empty() ? std::ranges::find(request.duts, full) != request.duts.end()
                                                  : !instantiated.contains(full);
    if (selected) {
      roots.push_back(full);
    }
  }
  if (roots.empty()) {
    return fail("no DUT root (every definition is instantiated by another one)");
  }
  std::sort(roots.begin(), roots.end());

  // Lower what the roots reach, on demand: lowering a graph splices the instances that sit on an
  // instance-level ring into it, so afterwards it calls fewer modules than it instantiated -- and a
  // module that was spliced everywhere (or that nothing instantiates any more) needs no LNAST of its
  // own. minion's frontend is spliced into the core and the core into the top: lowered standalone as
  // well, the same logic was lowered three times.
  std::vector<std::shared_ptr<Lnast>>              lnasts;
  std::vector<std::pair<std::string, std::string>> skipped;
  absl::flat_hash_map<std::string, std::string>    init_programs;  // graph name -> its simulation-only initialization program
  {
    absl::flat_hash_set<std::string>          lowered;
    std::vector<std::shared_ptr<hhds::Graph>> work;
    for (const auto& g : graphs) {
      if (std::ranges::find(roots, std::string(g->get_name())) != roots.end()) {
        work.push_back(g);
      }
    }
    while (!work.empty()) {
      auto g = std::move(work.back());
      work.pop_back();
      if (!lowered.insert(std::string(g->get_name())).second) {
        continue;
      }
      // Simulation-only initialization (`initial` blocks with $display / $value$plusargs / $fatal): a program the graph
      // carries beside the circuit (core/sim_program.hpp). It is not circuit state: it travels in `<stem>.dut.json` and
      // the runtime interprets it at each instance's power-on.
      if (auto init = g->get_input_node().attr(livehd::attrs::simulation_init); init.has() && !init.get().empty()) {
        init_programs[std::string(g->get_name())] = init.get();
      }
      if (!Pass_lg2lnast::lower({g}, request.passes, lnasts, &skipped, false, true)) {
        return fail("unknown LNAST stream pass in pass.lg2lnast.passes");
      }
      for (const auto node : g->body().nodes()) {  // what it still calls
        if (livehd::graph_util::type_op_of(node) == Ntype_op::Sub && !node.is_loop_subnode()) {
          if (auto child = node.get_subnode_graph(); child && !lowered.contains(std::string(child->get_name()))) {
            work.push_back(std::move(child));
          }
        }
      }
    }
  }
  lap("lower to LNAST (pass.lg2lnast)");
  // The LNAST tree aegraph (upass/aeg) on each lowered module. It only retypes and renames statements
  // (order_view emits just what a root needs, so what it orphans is dropped there). lg2lnast nests a
  // Mux arm's own cells inside the arm and the step scopes its sharing to an arm, so it never merges
  // the exclusive cones cgen_llvm's conditional regions skip when the arm is not taken.
  //
  // Its gain is sharing ACROSS kernels, which LLVM cannot do (minion -3.7%, cva6 -2.8%). A
  // design under kTreeAegMinStmts lowered statements is one kernel, where LLVM's GVN shares the same
  // values (rocket and ibex: post-O2 instruction counts within 0.3%): there the step can only reorder
  // the kernel, and rocket's single kernel ran 11% slower reordered. Every multi-kernel lhdsuite
  // design has 82k statements or more, every single-kernel one 21k or fewer.
  constexpr size_t kTreeAegMinStmts = 50000;
  size_t           lowered_stmts    = 0;
  {
    // Statements, nested blocks and arms included (the step's own `stmts` count); stops at the cutoff.
    std::vector<std::pair<const Lnast*, Lnast_nid>> blocks;
    for (const auto& ln : lnasts) {
      if (const auto root = ln->get_root(); !root.is_invalid()) {
        for (auto c = root.first_child(); !c.is_invalid(); c = c.next_sibling()) {
          if (ln->get_type(c) == Lnast_ntype::Lnast_ntype_stmts) {
            blocks.emplace_back(ln.get(), c);
          }
        }
      }
    }
    while (!blocks.empty() && lowered_stmts < kTreeAegMinStmts) {
      const auto [ln, block] = blocks.back();
      blocks.pop_back();
      for (auto st = block.first_child(); !st.is_invalid(); st = st.next_sibling()) {
        ++lowered_stmts;
        const auto t = ln->get_type(st);
        if (t == Lnast_ntype::Lnast_ntype_stmts) {
          blocks.emplace_back(ln, st);
        } else if (Lnast_ntype::is_if(t) || t == Lnast_ntype::Lnast_ntype_unique_if || t == Lnast_ntype::Lnast_ntype_for) {
          for (auto arm = st.first_child(); !arm.is_invalid(); arm = arm.next_sibling()) {
            if (ln->get_type(arm) == Lnast_ntype::Lnast_ntype_stmts) {
              blocks.emplace_back(ln, arm);
            }
          }
        }
      }
    }
  }
  if (lowered_stmts >= kTreeAegMinStmts) {
    upass::Staging_rewrite_stats                                   total;
    absl::flat_hash_map<std::string, std::shared_ptr<hhds::Graph>> by_name;
    for (const auto& g : graphs) {
      by_name.emplace(std::string(g->get_name()), g);
    }
    for (const auto& ln : lnasts) {
      Lnast_tree_io io;
      if (const auto it = by_name.find(ln->get_top_module_name()); it != by_name.end() && it->second->get_io()) {
        const auto&                      g   = it->second;
        auto                             gio = g->get_io();
        // lg2lnast types every port as an integer; an input that clocks state is a clock to the step:
        // a flop's clock_pin, a latch's enable (its gate) and every memory port's clock (offset 2 of
        // each port block). A flop's or memory's enable is data.
        absl::flat_hash_set<std::string> clocks;
        const auto                       note_clock = [&](const hhds::Pin_class& d) {
          if (!d.is_invalid() && livehd::graph_util::is_graph_input_pin(d)) {
            clocks.emplace(std::string(d.get_pin_name()));
          }
        };
        for (auto node : g->body().nodes()) {
          const auto op = livehd::graph_util::type_op_of(node);
          if (op == Ntype_op::Flop) {
            note_clock(livehd::graph_util::get_driver_of_sink_name(node, "clock_pin"));
          } else if (op == Ntype_op::Latch) {
            note_clock(livehd::graph_util::get_driver_of_sink_name(node, "enable"));
          } else if (op == Ntype_op::Memory) {
            for (const auto& sink : node.inp_sorted_pins()) {
              const auto pid = sink.get_port_id();
              if (pid < Ntype::Memory_readall_pid && pid % Ntype::Memory_port_stride == 2) {
                note_clock(sink.get_driver_pin());
              }
            }
          }
        }
        for (const auto& d : gio->get_input_pin_decls()) {
          Lnast_io_entry e;
          e.name      = std::string(d.name);
          e.bits      = static_cast<int32_t>(d.bits);
          e.is_signed = !d.unsign;
          e.kind      = Io_kind::integer;
          e.sig       = clocks.contains(e.name) ? Io_sig::clock : Io_sig::none;
          io.inputs.push_back(std::move(e));
        }
        for (const auto& d : gio->get_output_pin_decls()) {
          Lnast_io_entry e;
          e.name      = std::string(d.name);
          e.bits      = static_cast<int32_t>(d.bits);
          e.is_signed = !d.unsign;
          e.kind      = Io_kind::integer;
          io.outputs.push_back(std::move(e));
        }
      }
      total += upass::aeg::rewrite_staging({*ln, io, ln->get_top_module_name(), false, false});
    }
    if (timed) {
      std::fprintf(stderr,
                   "lnast_sim: tree-aeg units=%llu stmts=%llu forwarded=%llu renamed=%llu respelled=%llu\n",
                   static_cast<unsigned long long>(total.units),
                   static_cast<unsigned long long>(total.stmts),
                   static_cast<unsigned long long>(total.forwarded),
                   static_cast<unsigned long long>(total.renamed),
                   static_cast<unsigned long long>(total.respelled));
    }
    lap("tree aegraph (upass/aeg)");
  }
  absl::flat_hash_set<std::string> mine;
  std::error_code                  ec;
  fs::create_directories(request.odir, ec);
  for (const auto& root : roots) {
    std::shared_ptr<Lnast> top;
    for (const auto& ln : lnasts) {
      if (ln->get_top_module_name() == root) {
        top = ln;
        break;
      }
    }
    if (!top) {
      for (const auto& [name, why] : skipped) {
        if (name == root) {
          return fail(std::format("{}: {}", name, why));
        }
      }
      return fail(std::format("{} was not lowered to LNAST", root));
    }

    const std::string stem = livehd::unit_file_stem(root);
    const std::string cls  = livehd::cpp_ident(root);
    const std::string base = request.odir + "/" + stem;

    Sim_objects objects;
    objects.symbol         = "__lnast_" + cls;
    objects.object         = base + ".lnast.llvm.o";
    objects.incremental    = request.incremental;
    objects.dump_bitcode   = request.dump_bitcode;
    objects.unknown_zero   = request.unknown_zero;
    // sim.warn_undefined is a run-time switch (drv.bin --set sim.warn_undefined=false), so the sites
    // are always instrumented, as inou.cgen.sim does.
    objects.warn_undefined = true;
    const auto gen         = generate_sim(*top, lnasts, objects);
    lap("kernels (lnast_llvm)");
    if (!gen.ok) {
      for (const auto& [name, why] : skipped) {  // usually the real reason: a callee that was not lowered
        return fail(std::format("{} ({}: {})", gen.why, name, why));
      }
      return fail(gen.why);
    }
    // No C++ at all (todo/livehd/2sl-llvm-only-sim.md): `<stem>.dut.json` describes the kernels and the state layout,
    // and the testbench object (inou/prp/tb_llvm) calls the kernels directly and reads/writes the port words in place.
    {
      const auto esc = [](std::string_view s) {
        std::string out;
        for (const char ch : s) {
          if (ch == '"' || ch == '\\') {
            out += '\\';
          }
          out += (static_cast<unsigned char>(ch) < 0x20) ? ' ' : ch;
        }
        return out;
      };
      const auto str_list = [&](const std::vector<std::string>& v) {
        std::string out = "[";
        for (size_t i = 0; i < v.size(); ++i) {
          out += std::format("{}\"{}\"", i ? "," : "", esc(v[i]));
        }
        return out + "]";
      };
      std::string d = std::format("{{\"schema_version\":1,\"kind\":\"sim_dut\",\"stem\":\"{}\",\"module\":\"{}\",\"words\":{}", stem, esc(root), gen.words);
      d += std::format(",\"gate_word\":{},\"gate_bits\":{}", gen.gate_word, gen.gate_bits);
      d += ",\"edge\":" + str_list(gen.edge_functions) + ",\"fall\":" + str_list(gen.fall_functions)
           + ",\"settle\":" + str_list(gen.settle_functions);
      d += std::format(",\"commit\":\"{}\",\"fall_commit\":\"{}\"", esc(gen.commit_function), esc(gen.fall_commit_function));
      d += ",\"clocks\":" + str_list(gen.clocks);
      if (const auto it = init_programs.find(root); it != init_programs.end()) {
        std::string quoted = "\"";
        for (const unsigned char ch : it->second) {  // a full JSON string: the program's frames carry any byte
          if (ch == '"' || ch == '\\') {
            quoted += '\\';
            quoted += static_cast<char>(ch);
          } else if (ch < 0x20) {
            quoted += std::format("\\u{:04x}", static_cast<unsigned>(ch));
          } else {
            quoted += static_cast<char>(ch);
          }
        }
        d += ",\"sim_init\":" + quoted + "\"";
      }
      std::vector<std::string> object_names;
      for (const auto& object : gen.objects) {
        object_names.push_back(fs::path(object).filename().string());
      }
      d += ",\"objects\":" + str_list(object_names);
      d += ",\"fields\":[";
      bool first_field = true;
      const auto field_row = [&](const Field& f, int kind) {
        std::string init = "null";
        const bool  has_table = (f.entries == 0 && f.has_init) || (f.entries != 0 && !f.init_words.empty());
        if (has_table) {
          init                = "[";
          const size_t n      = f.entries != 0 ? f.init_words.size() : (f.bits + 63) / 64;
          for (size_t w = 0; w < n; ++w) {
            uint64_t v = w < f.init_words.size() ? f.init_words[w] : 0;
            if (f.entries == 0 && f.sign && w + 1 == n && f.bits % 64 != 0 && ((v >> ((f.bits - 1) % 64)) & 1) != 0) {
              v |= ~((uint64_t{1} << (f.bits % 64)) - 1);
            }
            init += std::format("{}\"0x{:x}\"", w ? "," : "", v);
          }
          init += "]";
        }
        std::string image = "null";
        if (!f.image_path.empty()) {
          image = std::format("{{\"path\":\"{}\",\"radix\":{}}}", esc(f.image_path), f.image_radix);
        }
        std::string marks = "[";
        for (size_t m = 0; m < f.marks.size(); ++m) {
          marks += std::format("{}{}", m ? "," : "", f.marks[m]);
        }
        marks += "]";
        d += std::format("{}{{\"name\":\"{}\",\"bits\":{},\"word\":{},\"kind\":{},\"sign\":{},\"entries\":{},\"fill\":{},\"init\":{},\"image\":{},\"marks\":{}}}",
                         first_field ? "" : ",",
                         esc(f.name),
                         kind == 3 ? f.entry_bits : f.bits,
                         f.word,
                         kind,
                         f.sign ? "true" : "false",
                         f.entries,
                         f.fill,
                         init,
                         image,
                         marks);
        first_field = false;
      };
      for (const auto& f : gen.inputs) {
        field_row(f, 0);
      }
      for (const auto& f : gen.outputs) {
        field_row(f, 1);
      }
      for (const auto& r : gen.regs) {
        field_row(r, r.entries != 0 ? 3 : 2);
      }
      d += "],\"undefined\":[";
      for (size_t i = 0; i < gen.undefined.size(); ++i) {
        d += std::format("{}{{\"what\":\"{}\",\"where\":\"{}\",\"word\":{}}}", i ? "," : "", esc(gen.undefined[i].what),
                         esc(gen.undefined[i].where), gen.undefined[i].word);
      }
      d += "]}\n";
      if (!write_if_different(base + ".dut.json", d)) {
        return fail("could not write " + base + ".dut.json");
      }
      mine.insert(stem + ".dut.json");
      for (const auto& object : gen.objects) {
        const auto name = fs::path(object).filename().string();
        mine.insert(name);
        if (request.dump_bitcode) {
          mine.insert(fs::path(name).replace_extension(".bc").string());
        }
        mine.insert(name.substr(0, name.size() - 1) + "k");  // its reuse sidecar
      }
    }
    result.stems.push_back(stem);
    result.words        += gen.words;
    result.instructions += gen.instructions;
  }

  // Anything else a previous generation left here (another flow's modules, a renamed top) would be
  // compiled into, or read as part of, this simulator.
  for (const auto& entry : fs::directory_iterator(request.odir, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (mine.contains(name) || name.starts_with("drv.") || name.starts_with("drv_")) {
      continue;
    }
    if (name == "gen_digests.json") {  // inou.cgen.sim's generation index: its files are gone
      fs::remove(entry.path(), ec);
      continue;
    }
    for (std::string_view ext : {".hpp", ".cpp", ".iface.json", ".dut.json", ".llvm.o", ".llvm.k", ".llvm.bc", ".inc", ".txt"}) {
      if (name.ends_with(ext)) {
        fs::remove(entry.path(), ec);
        break;
      }
    }
  }

  lap("class, manifest, cleanup");
  result.ok = true;
  return result;
}

}  // namespace livehd::lnast_llvm
