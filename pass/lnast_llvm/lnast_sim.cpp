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

#include "absl/container/flat_hash_set.h"
#include "cpp_ident.hpp"
#include "debug_env.hpp"
#include "file_name.hpp"
#include "lnast.hpp"
#include "lnast_llvm.hpp"
#include "lnast_sim_rt.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "pass_lg2lnast.hpp"
#include "sim_tune_rt.hpp"

namespace livehd::lnast_llvm {

namespace {

namespace fs = std::filesystem;

// Generated files are build inputs keyed on mtime: leave an unchanged one alone.
void write_if_different(const std::string& path, std::string_view text) {
  {
    std::ifstream in(path, std::ios::binary);
    if (in) {
      std::stringstream old;
      old << in.rdbuf();
      if (old.str() == text) {
        return;
      }
    }
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

std::string slop_type(const Field& f) { return std::format("{}<{}>", f.sign ? "Slop" : "Slop_u", f.bits); }

// A port or top-level register name the driver can spell as one C++ member.
bool plain_name(std::string_view name) {
  return !name.empty() && name.find('.') == std::string_view::npos && livehd::cpp_ident(name) == name;
}

// The C++ spelling of a port path: livehd::cpp_port_path, the one inou.cgen.sim uses (existing
// testbenches and the driver generator are written against it).
using livehd::cpp_port_path;

// A port name the class can declare: an identifier or a reserved word (escaped), optionally a bundle
// leaf `bundle.inner.leaf`.
bool port_name(std::string_view name) {
  if (name.empty() || name.front() == '.' || name.back() == '.') {
    return false;
  }
  return name.find("..") == std::string_view::npos;
}

// The members of a port struct: plain ports as fields, one nested struct per bundle (members in
// first-appearance order).
struct Port_tree {
  std::string                                    type;  // a leaf's Slop type
  std::vector<std::pair<std::string, Port_tree>> members;
  Port_tree&                                     member(const std::string& name) {
    for (auto& [n, m] : members) {
      if (n == name) {
        return m;
      }
    }
    members.emplace_back(name, Port_tree{});
    return members.back().second;
  }
  std::string text(int depth) const {
    std::string out;
    const auto  pad = std::string(static_cast<size_t>(depth) * 2, ' ');
    for (const auto& [name, m] : members) {
      if (m.members.empty()) {
        out += std::format("{}{} {}{{}};\n", pad, m.type, name);
      } else {
        out += std::format("{}struct {{\n{}{}}} {}{{}};\n", pad, m.text(depth + 1), pad, name);
      }
    }
    return out;
  }
};
std::string port_members(const std::vector<Field>& ports) {
  Port_tree root;
  for (const auto& f : ports) {
    Port_tree*        at = &root;
    const std::string spelled = cpp_port_path(f.name);
    std::string_view  name    = spelled;
    for (auto dot = name.find('.'); dot != std::string_view::npos; dot = name.find('.')) {
      at = &at->member(std::string(name.substr(0, dot)));
      name.remove_prefix(dot + 1);
    }
    at->member(std::string(name)).type = slop_type(f);
  }
  return root.text(2);
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
  for (const auto& source : request.graphs) {
    if (!source) {
      continue;
    }
    for (const auto& graph : source->definitions().graphs()) {
      for (const auto node : graph->body().nodes()) {
        if (livehd::graph_util::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
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
    if (!request.top.empty() ? (request.top == full || request.top == entity) : !instantiated.contains(full)) {
      roots.push_back(full);
    }
  }
  if (request.top.empty() && !request.duts.empty()) {
    std::vector<std::string> named;
    for (const auto& full : roots) {
      const auto entity = entity_of(full);
      for (const auto& dut : request.duts) {
        if (dut == full || dut == entity || entity_of(dut) == entity) {
          named.push_back(full);
          break;
        }
      }
    }
    if (!named.empty()) {
      roots = std::move(named);
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
      // Simulation-only initialization (`initial` blocks with $display / $value$plusargs / $fatal): a program
      // the graph carries beside the circuit and inou.cgen.sim runs at power-on. This flow has no such
      // program, and dropping it would silently lose its prints, plusargs and failures.
      if (auto init = g->get_input_node().attr(livehd::attrs::simulation_init); init.has() && !init.get().empty()) {
        return fail(std::format("{}: has a simulation-only initial block ($display / plusargs / $fatal)", std::string(g->get_name())));
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
  absl::flat_hash_set<std::string> mine{"lnast_sim_rt.hpp"};
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
    objects.symbol = "__lnast_" + cls;
    objects.object = base + ".lnast.llvm.o";
    objects.unknown_zero = request.unknown_zero;
    // sim.warn_undefined is a run-time switch (drv.bin --set sim.warn_undefined=false), so the sites
    // are always instrumented, as inou.cgen.sim does.
    objects.warn_undefined = true;
    const auto gen = generate_sim(*top, lnasts, objects);
    lap("kernels (lnast_llvm)");
    if (!gen.ok) {
      for (const auto& [name, why] : skipped) {  // usually the real reason: a callee that was not lowered
        return fail(std::format("{} ({}: {})", gen.why, name, why));
      }
      return fail(gen.why);
    }
    for (const auto& f : gen.inputs) {
      if (!port_name(f.name)) {
        return fail(std::format("port '{}' is not an identifier or a tuple leaf", f.name));
      }
    }
    for (const auto& f : gen.outputs) {
      if (!port_name(f.name)) {
        return fail(std::format("port '{}' is not an identifier or a tuple leaf", f.name));
      }
    }
    for (const auto& c : gen.clocks) {
      if (!plain_name(c)) {
        return fail(std::format("clock port '{}' is not a plain identifier", c));
      }
    }

    // ---- <stem>.hpp: the class the driver instantiates ----
    std::string h;
    h += "// Generated by lhd sim (LNAST flow). Do not edit.\n#pragma once\n";
    if (request.vcd) {
      h += "#define LHD_LNAST_VCD 1\n";
    }
    h += "#include \"lnast_sim_rt.hpp\"\n";
    h += std::format("struct {} : __lnast_dut {{\n", cls);
    h += "  struct In {\n";
    for (const auto& c : gen.clocks) {
      h += std::format("    Slop_u<1> {}{{}};\n    bool {}__tick = true;\n", c, c);
    }
    h += port_members(gen.inputs);
    h += "  };\n  struct Out {\n";
    h += port_members(gen.outputs);
    h += "  };\n  In __in{};\n  Out __out{};\n";
    h += std::format("  {}();\n", cls);
    // The header holds the ports and nothing of the layout: an edit that moves state words changes
    // `<stem>.cpp` and the kernels, and the testbench driver -- the slow file of the host build --
    // is not compiled again.
    h += "  void step();              // one clock and the outputs after it\n";
    h += "  void __settle();          // the outputs on the current state and inputs\n";
    h += "  void __push();\n  void __pull();\n";
    std::string body;  // of __push, then of __pull (defined in the .cpp)
    for (const auto& f : gen.inputs) {
      if (f.marks.empty()) {  // no kernel waits on this input
        body += std::format("    __in.{}.copy_packed_words(&st_[{}]);\n", cpp_port_path(f.name), f.word);
        continue;
      }
      // Activity gating: an input that changed wakes the kernels that read it.
      std::map<size_t, uint64_t> masks;
      for (const auto bit : f.marks) {
        masks[gen.gate_word + bit / 64] |= uint64_t{1} << (bit % 64);
      }
      const size_t n  = (f.bits + 63) / 64;
      body            += std::format("    {{\n      std::uint64_t w[{}];\n      __in.{}.copy_packed_words(w);\n", n, cpp_port_path(f.name));
      if (n == 1) {
        body += std::format("      if (w[0] != st_[{0}]) {{\n        st_[{0}] = w[0];\n", f.word);
      } else {
        body
            += std::format("      if (std::memcmp(w, &st_[{0}], sizeof w) != 0) {{\n        std::memcpy(&st_[{0}], w, sizeof w);\n",
                           f.word);
      }
      for (const auto& [word, mask] : masks) {
        body += std::format("        st_[{}] |= 0x{:x}ULL;\n", word, mask);
      }
      body += "      }\n    }\n";
    }
    const std::string push_body = std::move(body);
    body.clear();
    for (const auto& f : gen.outputs) {
      body += std::format("    __out.{} = {}::from_packed_words(&st_[{}]);\n", cpp_port_path(f.name), slop_type(f), f.word);
    }
    const std::string pull_body  = std::move(body);
    h += "  void __edge();            // one clock: rise (next state, memory writes, commit), then fall\n";
    h += "  void __settle_kernels();  // outputs on the current state\n";
    h += "  void reset_cycle(bool zero_uninitialized = false, bool unknown_zero = false);\n";
    h += "  void after_load() override;\n";
    h                += "  void __all_dirty();       // activity gating: every kernel runs once more\n";
    if (!gen.undefined.empty()) {
      h += "  void __undefined_report();  // sim.warn_undefined: report the sites the last clock hit\n";
    }
    // LHD_SIM_LNAST_STATS (debug): count how often each gated kernel really ran; printed at exit.
    const bool stats  = gen.gate_bits != 0 && livehd::debug::env<"LHD_SIM_LNAST_STATS">() != nullptr;
    if (stats) {
      h += std::format("  std::uint64_t __runs[{}]{{}};\n  ~{}();\n", gen.gate_bits + 1, cls);
    }
    h += "};\n";
    write_if_different(base + ".hpp", h);

    // ---- <stem>.cpp: the field table and the cold entry points ----
    std::string c;
    c += std::format("// Generated by lhd sim (LNAST flow). Do not edit.\n#include \"{}.hpp\"\n", stem);
    if (!gen.undefined.empty()) {
      // At namespace scope: its inline switch and cycle are the driver's (one definition program-wide).
      c += livehd::sim::kUndefinedWarnHelper;
    }
    c += "namespace {\n";
    std::string table;
    size_t      fields = 0;
    for (size_t i = 0; i < gen.regs.size(); ++i) {
      const auto& r = gen.regs[i];
      if ((r.entries == 0 && r.has_init) || (r.entries != 0 && !r.init_words.empty())) {
        c              += std::format("const std::uint64_t init_{}[] = {{", i);
        const size_t n  = r.entries != 0 ? r.init_words.size() : (r.bits + 63) / 64;
        for (size_t w = 0; w < n; ++w) {
          uint64_t v = w < r.init_words.size() ? r.init_words[w] : 0;
          if (r.entries == 0 && r.sign && w + 1 == n && r.bits % 64 != 0 && ((v >> ((r.bits - 1) % 64)) & 1) != 0) {
            v |= ~((uint64_t{1} << (r.bits % 64)) - 1);  // the state keeps a signed value sign-extended over its last word
          }
          c += std::format("{}0x{:x}ULL", w ? ", " : "", v);
        }
        c += "};\n";
      }
    }
    const auto row = [&](const Field& f, int kind, std::string_view init, int fill) {
      table += std::format("  {{\"{}\", {}, {}, {}, {}, {}ULL, {}, {}}},\n",
                           f.name,
                           kind == 3 ? f.entry_bits : f.bits,
                           f.word,
                           kind,
                           f.sign ? "true" : "false",
                           f.entries,
                           init,
                           fill);
      ++fields;
    };
    for (const auto& f : gen.inputs) {
      row(f, 0, "nullptr", 0);
    }
    for (const auto& f : gen.outputs) {
      row(f, 1, "nullptr", 0);
    }
    for (size_t i = 0; i < gen.regs.size(); ++i) {
      const auto& r = gen.regs[i];
      const bool has_table = (r.entries == 0 && r.has_init) || (r.entries != 0 && !r.init_words.empty());
      row(r, r.entries != 0 ? 3 : 2, has_table ? std::format("init_{}", i) : "nullptr", r.fill);
    }
    c += std::format("const __lnast_field fields[] = {{\n{}}};\n", table);
    // sim.warn_undefined: the edge kernels set a site's word (Result::undefined, consecutive words);
    // step() tests them all at once and reports through the driver's reporter (kUndefinedWarnHelper,
    // the text inou.cgen.sim and the driver use, so the run-time switch and the cycle are shared). A
    // statement in a module instantiated several times is one site per instance here: those report
    // through ONE __lhd_undef_site, so a site is reported once, as in inou.cgen.sim.
    std::vector<size_t> undefined_of;
    if (!gen.undefined.empty()) {
      std::map<std::pair<std::string, std::string>, size_t> distinct;
      std::string                                           sites;
      for (size_t i = 0; i < gen.undefined.size(); ++i) {
        const auto& u = gen.undefined[i];
        if (u.word != gen.undefined.front().word + i) {
          return fail("internal: the undefined-access words are not consecutive");
        }
        auto [it, fresh] = distinct.try_emplace({u.what, u.where}, distinct.size());
        if (fresh) {
          sites += std::format("  {{{}, {}, 0, false}},\n", livehd::cpp_string_literal(u.what), livehd::cpp_string_literal(u.where));
        }
        undefined_of.push_back(it->second);
      }
      c += std::format("__lhd_undef_site undefined_sites[] = {{\n{}}};\nconst std::uint32_t undefined_site_of[] = {{", sites);
      for (size_t i = 0; i < undefined_of.size(); ++i) {
        c += std::format("{}{}", i ? ", " : "", undefined_of[i]);
      }
      c += "};\n";
    }
    c += "}  // namespace\n";
    c += "extern \"C\" {\n";
    for (const auto* list : {&gen.edge_functions, &gen.fall_functions, &gen.settle_functions}) {
      for (const auto& fn : *list) {
        c += std::format("void {}(std::uint64_t* state, void** resources);\n", fn);
      }
    }
    for (const auto* commit : {&gen.commit_function, &gen.fall_commit_function}) {
      if (!commit->empty()) {
        c += std::format("void {}(std::uint64_t* state, void** resources);\n", *commit);
      }
    }
    c += "}\n";
    // Activity gating (lnast_llvm.hpp): a kernel runs when its dirty bit is set, cleared before the
    // call: one test and a direct call per kernel, under a test per eight kernels and one per dirty
    // word (most kernels of a large design rest most clocks, and resting kernels cluster). Every test
    // reads the word again: a kernel wakes later ones. Scanning the dirty words for set bits and
    // calling through a table instead is slower as soon as much of the design is active (boom, two
    // thirds of its kernels each clock: 28.9k cycles/s against 35.5k) -- one indirect call site
    // that the processor cannot predict.
    const auto run  = [&](size_t first, size_t count) {
      std::string out;
      const auto  mask_of = [&](size_t lo, size_t hi) {  // bits [lo, hi) of one word, clipped to [first, first + count)
        uint64_t m = 0;
        for (size_t b = std::max(lo, first); b < std::min(hi, first + count); ++b) {
          m |= uint64_t{1} << (b % 64);
        }
        return m;
      };
      const auto& name_of = [&](size_t bit) -> const std::string& {
        if (bit < gen.edge_functions.size()) {
          return gen.edge_functions[bit];
        }
        bit -= gen.edge_functions.size();
        return bit < gen.fall_functions.size() ? gen.fall_functions[bit] : gen.settle_functions[bit - gen.fall_functions.size()];
      };
      for (size_t w = first / 64; w * 64 < first + count; ++w) {
        const auto word  = gen.gate_word + w;
        out             += std::format("  if ((s[{}] & 0x{:x}ULL) != 0) {{\n", word, mask_of(w * 64, w * 64 + 64));
        for (size_t g = w * 64; g < w * 64 + 64; g += 8) {
          const auto group = mask_of(g, g + 8);
          if (group == 0) {
            continue;
          }
          out += std::format("    if ((s[{}] & 0x{:x}ULL) != 0) {{\n", word, group);
          for (size_t b = std::max(g, first); b < std::min(g + 8, first + count); ++b) {
            const auto bit  = uint64_t{1} << (b % 64);
            out            += std::format("      if ((s[{0}] & 0x{1:x}ULL) != 0) {{ s[{0}] &= ~0x{1:x}ULL; {2}(s, nullptr); }}\n",
                               word,
                               bit,
                               name_of(b));
          }
          out += "    }\n";
        }
        out += "  }\n";
      }
      return out;
    };
    const bool scan     = gen.gate_bits != 0 && !stats;
    size_t     gate_bit = 0;
    const auto call     = [&](const std::string& fn) {
      if (gen.gate_bits == 0) {
        return std::format("  {}(s, nullptr);\n", fn);
      }
      const auto word = gen.gate_word + gate_bit / 64;
      const auto mask = uint64_t{1} << (gate_bit % 64);
      ++gate_bit;
      return std::format("  if ((s[{0}] & 0x{1:x}ULL) != 0) {{\n    s[{0}] &= ~0x{1:x}ULL;\n{3}    {2}(s, nullptr);\n  }}\n",
                         word,
                         mask,
                         fn,
                         stats ? std::format("    ++__runs[{}];\n", gate_bit - 1) : std::string());
    };
    c += std::format("void {}::__edge() {{\n  auto* s = st_.get();\n{}",
                     cls,
                     stats ? std::format("  ++__runs[{}];\n", gen.gate_bits) : "");
    for (const auto* list : {&gen.edge_functions, &gen.fall_functions}) {
      if (scan) {
        c += run(list == &gen.edge_functions ? 0 : gen.edge_functions.size(), list->size());
      } else {
        for (const auto& fn : *list) {
          c += call(fn);
        }
      }
      const auto& commit = list == &gen.edge_functions ? gen.commit_function : gen.fall_commit_function;
      if (!commit.empty()) {
        c += std::format("  {}(s, nullptr);\n", commit);
      }
    }
    c += std::format("}}\nvoid {}::__settle_kernels() {{\n  auto* s = st_.get();\n", cls);
    if (scan) {
      c += run(gen.edge_functions.size() + gen.fall_functions.size(), gen.settle_functions.size());
    } else {
      for (const auto& fn : gen.settle_functions) {
        c += call(fn);
      }
    }
    c += "}\n";
    if (stats) {
      c += std::format(
          "{0}::~{0}() {{\n  std::uint64_t total = 0;\n  for (std::size_t i = 0; i < {1}; ++i) {{\n    total += __runs[i];\n"
          "    std::fprintf(stderr, \"lnast activity: kernel %zu ran %llu of %llu clocks\\n\", i, "
          "static_cast<unsigned long long>(__runs[i]), static_cast<unsigned long long>(__runs[{1}]));\n  }}\n"
          "  std::fprintf(stderr, \"lnast activity: %llu kernel runs of %llu ({1} kernels, %llu clocks)\\n\", "
          "static_cast<unsigned long long>(total), static_cast<unsigned long long>(__runs[{1}] * {1}), "
          "static_cast<unsigned long long>(__runs[{1}]));\n}}\n",
          cls,
          gen.gate_bits);
    }
    c += std::format("void {}::__all_dirty() {{\n", cls);
    for (size_t w = 0; w < (gen.gate_bits + 63) / 64; ++w) {
      const bool part  = w + 1 == (gen.gate_bits + 63) / 64 && gen.gate_bits % 64 != 0;
      c               += std::format("  st_[{}] = 0x{:x}ULL;\n",
                       gen.gate_word + w,
                       part ? (uint64_t{1} << (gen.gate_bits % 64)) - 1 : ~uint64_t{0});
    }
    c += "}\n";
    c += std::format("inline void {}::__push() {{\n{}}}\ninline void {}::__pull() {{\n{}}}\n", cls, push_body, cls, pull_body);
    c += std::format("void {}::step() {{\n  ++__gen;\n  __push();\n  __edge();\n  __settle_kernels();\n  __pull();\n", cls);
    if (!gen.undefined.empty()) {
      c += std::format(
          "  {{\n    std::uint64_t hit = 0;\n    for (std::size_t i = 0; i < {1}; ++i) {{\n      hit |= st_[{0} + i];\n    }}\n"
          "    if (hit != 0) [[unlikely]] {{\n      __undefined_report();\n    }}\n  }}\n",
          gen.undefined.front().word,
          gen.undefined.size());
    }
    if (request.vcd) {
      std::string clock_list;
      for (const auto& ck : gen.clocks) {
        clock_list += (clock_list.empty() ? "" : ",") + ck;
      }
      c += std::format("  if (!__vcd_path.empty() || __vcd) {{\n    __vcd_sample(\"{}\", \"{}\");\n  }}\n", cls, clock_list);
    }
    c += "}\n";
    c += std::format("void {}::__settle() {{\n  __push();\n  __settle_kernels();\n  __pull();\n}}\n", cls);
    if (!gen.undefined.empty()) {
      c += std::format(
          "void {0}::__undefined_report() {{\n  for (std::size_t i = 0; i < {2}; ++i) {{\n    if (st_[{1} + i] != 0) {{\n"
          "      st_[{1} + i] = 0;\n      __lhd_sim_undefined(&undefined_sites[undefined_site_of[i]]);\n    }}\n  }}\n}}\n",
          cls,
          gen.undefined.front().word,
          gen.undefined.size());
    }
    c += std::format("{0}::{0}() : __lnast_dut(fields, {1}, {2}) {{ __all_dirty(); }}\n", cls, fields, gen.words);
    c += std::format(
        "void {}::reset_cycle(bool zero_uninitialized, bool unknown_zero) {{\n  power_on(zero_uninitialized, unknown_zero);\n  "
        "__all_dirty();\n  __settle();\n}}\n",
        cls);
    c += std::format("void {}::after_load() {{\n  __all_dirty();\n", cls);
    for (const auto& f : gen.inputs) {
      c += std::format("  __in.{} = {}::from_packed_words(&st_[{}]);\n", cpp_port_path(f.name), slop_type(f), f.word);
    }
    c += "  __settle();\n}\n";
    write_if_different(base + ".cpp", c);
    write_if_different(request.odir + "/lnast_sim_rt.hpp", kSimRuntimeHeader);

    // ---- <stem>.iface.json: what the driver generator reads (schema: inou/prp/prp_sim.cpp) ----
    std::string j = std::format(
        "{{\"schema_version\":1,\"kind\":\"sim_iface\",\"gen\":\"lnast-1\",\"module\":\"{}\",\"executable\":true,\"io\":[",
        cls);
    bool       first = true;
    const auto io    = [&](std::string_view name, std::string_view dir, uint32_t bits, bool sign, bool clock) {
      j     += std::format("{}{{\"name\":\"{}\",\"dir\":\"{}\",\"bits\":{},\"declared_bits\":{},\"signed\":{}{}}}",
                       first ? "" : ",",
                       name,
                       dir,
                       bits,
                       bits,
                       sign ? "true" : "false",
                       clock ? ",\"clock\":true" : "");
      first  = false;
    };
    for (const auto& ck : gen.clocks) {
      io(ck, "input", 1, false, true);
    }
    for (const auto& f : gen.inputs) {
      io(cpp_port_path(f.name), "input", f.bits, f.sign, false);
    }
    for (const auto& f : gen.outputs) {
      io(cpp_port_path(f.name), "output", f.bits, f.sign, false);
    }
    // No `regs`, `mems` or `subs`: the class publishes ports only. A testbench that reads or drives a
    // register (`dut.c`, `regref(...)`) or a memory word, or walks into a sub-instance, would need members
    // this class does not have; leaving them out makes the driver generator say so, and `lhd sim` then
    // builds the design on inou.cgen.sim (see lnast_sim_may_fall_back in lhd_kernel_sim.cpp).
    j += "],\"regs\":[],\"mems\":[],\"subs\":[]}\n";
    write_if_different(base + ".iface.json", j);
    for (std::string_view ext : {".hpp", ".cpp", ".iface.json"}) {
      mine.insert(stem + std::string(ext));
    }
    for (const auto& object : gen.objects) {
      const auto name = fs::path(object).filename().string();
      mine.insert(name);
      mine.insert(name.substr(0, name.size() - 1) + "k");  // its reuse sidecar
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
    for (std::string_view ext : {".hpp", ".cpp", ".iface.json", ".llvm.o", ".llvm.k", ".inc", ".txt"}) {
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
