//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lnast_sim.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>

#include "absl/container/flat_hash_set.h"
#include "cpp_ident.hpp"
#include "file_name.hpp"
#include "lnast.hpp"
#include "lnast_llvm.hpp"
#include "lnast_sim_rt.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "pass_lg2lnast.hpp"

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

// A port name: an identifier, or a tuple leaf `bundle.inner.leaf`, the driver's `__in.bundle.inner.leaf`.
bool port_name(std::string_view name) {
  for (;;) {
    const auto dot = name.find('.');
    if (!plain_name(name.substr(0, dot))) {
      return false;
    }
    if (dot == std::string_view::npos) {
      return true;
    }
    name.remove_prefix(dot + 1);
  }
}

// The members of a port struct: plain ports as fields, tuple leaves as nested structs, one per
// bundle (members in first-appearance order).
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
    Port_tree*       at   = &root;
    std::string_view name = f.name;
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

  std::vector<std::shared_ptr<Lnast>>              lnasts;
  std::vector<std::pair<std::string, std::string>> skipped;
  if (!Pass_lg2lnast::lower(graphs, request.passes, lnasts, &skipped, false)) {
    return fail("unknown LNAST stream pass in pass.lg2lnast.passes");
  }
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
    const auto gen = generate_sim(*top, lnasts, objects);
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
    h += "// Generated by lhd sim (LNAST flow). Do not edit.\n#pragma once\n#include \"lnast_sim_rt.hpp\"\n";
    h += std::format("struct {} : __lnast_dut {{\n", cls);
    h += std::format("  static constexpr std::size_t __lnast_words = {};\n", gen.words);
    h += "  struct In {\n";
    for (const auto& c : gen.clocks) {
      h += std::format("    Slop_u<1> {}{{}};\n    bool {}__tick = true;\n", c, c);
    }
    h += port_members(gen.inputs);
    h += "  };\n  struct Out {\n";
    h += port_members(gen.outputs);
    h += "  };\n  In __in{};\n  Out __out{};\n";
    h += std::format("  {}();\n", cls);
    h += "  void __push() {\n";
    for (const auto& f : gen.inputs) {
      h += std::format("    __in.{}.copy_packed_words(&st_[{}]);\n", f.name, f.word);
    }
    h += "  }\n  void __pull() {\n";
    for (const auto& f : gen.outputs) {
      h += std::format("    __out.{} = {}::from_packed_words(&st_[{}]);\n", f.name, slop_type(f), f.word);
    }
    h += "  }\n";
    h += "  void step() {\n    ++__gen;\n    __push();\n    __edge();\n    __settle_kernels();\n    __pull();\n  }\n";
    h += "  void __settle() {\n    __push();\n    __settle_kernels();\n    __pull();\n  }\n";
    h += "  void __edge();            // one clock: rise (next state, memory writes, commit), then fall\n";
    h += "  void __settle_kernels();  // outputs on the current state\n";
    h += "  void reset_cycle(bool zero_uninitialized = false);\n";
    h += "  void after_load() override;\n";
    h += "};\n";
    write_if_different(base + ".hpp", h);

    // ---- <stem>.cpp: the field table and the cold entry points ----
    std::string c;
    c += std::format("// Generated by lhd sim (LNAST flow). Do not edit.\n#include \"{}.hpp\"\nnamespace {{\n", stem);
    std::string table;
    size_t      fields = 0;
    for (size_t i = 0; i < gen.regs.size(); ++i) {
      const auto& r = gen.regs[i];
      if (r.entries == 0 && r.has_init) {
        c              += std::format("const std::uint64_t init_{}[] = {{", i);
        const size_t n  = (r.bits + 63) / 64;
        for (size_t w = 0; w < n; ++w) {
          uint64_t v = w < r.init_words.size() ? r.init_words[w] : 0;
          if (r.sign && w + 1 == n && r.bits % 64 != 0 && ((v >> ((r.bits - 1) % 64)) & 1) != 0) {
            v |= ~((uint64_t{1} << (r.bits % 64)) - 1);  // the state keeps a signed value sign-extended over its last word
          }
          c += std::format("{}0x{:x}ULL", w ? ", " : "", v);
        }
        c += "};\n";
      }
    }
    const auto row = [&](const Field& f, int kind, std::string_view init, bool has_init) {
      table += std::format("  {{\"{}\", {}, {}, {}, {}, {}ULL, {}, {}}},\n",
                           f.name,
                           kind == 3 ? f.entry_bits : f.bits,
                           f.word,
                           kind,
                           f.sign ? "true" : "false",
                           f.entries,
                           init,
                           has_init ? "true" : "false");
      ++fields;
    };
    for (const auto& f : gen.inputs) {
      row(f, 0, "nullptr", true);
    }
    for (const auto& f : gen.outputs) {
      row(f, 1, "nullptr", true);
    }
    for (size_t i = 0; i < gen.regs.size(); ++i) {
      const auto& r = gen.regs[i];
      row(r, r.entries != 0 ? 3 : 2, r.entries == 0 && r.has_init ? std::format("init_{}", i) : "nullptr", r.has_init);
    }
    c += std::format("const __lnast_field fields[] = {{\n{}}};\n}}  // namespace\n", table);
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
    c += std::format("void {}::__edge() {{\n  auto* s = st_.get();\n", cls);
    for (const auto* list : {&gen.edge_functions, &gen.fall_functions}) {
      for (const auto& fn : *list) {
        c += std::format("  {}(s, nullptr);\n", fn);
      }
      const auto& commit = list == &gen.edge_functions ? gen.commit_function : gen.fall_commit_function;
      if (!commit.empty()) {
        c += std::format("  {}(s, nullptr);\n", commit);
      }
    }
    c += std::format("}}\nvoid {}::__settle_kernels() {{\n  auto* s = st_.get();\n", cls);
    for (const auto& fn : gen.settle_functions) {
      c += std::format("  {}(s, nullptr);\n", fn);
    }
    c += "}\n";
    c += std::format("{0}::{0}() : __lnast_dut(fields, {1}, __lnast_words) {{}}\n", cls, fields);
    c += std::format("void {}::reset_cycle(bool zero_uninitialized) {{\n  power_on(zero_uninitialized);\n  __settle();\n}}\n", cls);
    c += std::format("void {}::after_load() {{\n", cls);
    for (const auto& f : gen.inputs) {
      c += std::format("  __in.{} = {}::from_packed_words(&st_[{}]);\n", f.name, slop_type(f), f.word);
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
      io(f.name, "input", f.bits, f.sign, false);
    }
    for (const auto& f : gen.outputs) {
      io(f.name, "output", f.bits, f.sign, false);
    }
    j     += "],\"regs\":[";
    first  = true;
    for (const auto& r : gen.regs) {
      if (r.entries == 0 && plain_name(r.name)) {
        j += std::format("{}{{\"name\":\"{}\",\"kind\":\"flop\",\"bits\":{},\"declared_bits\":{},\"signed\":{},\"latch\":false}}",
                         first ? "" : ",",
                         r.name,
                         r.bits,
                         r.bits,
                         r.sign ? "true" : "false");
        first = false;
      }
    }
    j     += "],\"mems\":[";
    first  = true;
    for (const auto& r : gen.regs) {
      if (r.entries != 0 && plain_name(r.name)) {
        j += std::format(
            "{}{{\"name\":\"{}\",\"bits\":{},\"declared_bits\":{},\"signed\":false,\"size\":{},\"ordering\":\"old\",\"rd_regs\":[]}"
            "}",
            first ? "" : ",",
            r.name,
            r.entry_bits,
            r.entry_bits,
            r.entries);
        first = false;
      }
    }
    j += "],\"subs\":[]}\n";
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

  result.ok = true;
  return result;
}

}  // namespace livehd::lnast_llvm
