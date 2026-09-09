// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass.hpp"
#include "satopt.hpp"
#include "satopt_memory.hpp"

namespace {
class Pass_satopt : public Pass {
public:
  explicit Pass_satopt(const Eprp_var& var) : Pass("pass.satopt", var) {}
  static void work(Eprp_var& var) {
    hhds::GraphLibrary                        scratch;
    std::vector<std::shared_ptr<hhds::Graph>> memories, originals;
    const auto                                top = var.get("top", "");
    for (const auto& g : var.graphs) {
      if (!g || (!top.empty() && g->get_name() != top)) {
        continue;
      }
      auto* lib = g->get_io()->get_library();
      for (const auto& def : g->definitions().graphs()) {
        if (scratch.find_io(def->get_name())) {
          continue;
        }
        if (scratch.copy_from(*lib, def->get_name())) {
          memories.push_back(scratch.find_io(def->get_name())->get_graph());
          originals.push_back(def);
        }
      }
    }
    if (!top.empty() && originals.empty()) {
      livehd::diag::err("pass.satopt", "top-not-found", "name").msg("definition '{}' was not found", top).fatal();
    }
    livehd::abc::optimize_memories(memories, var.get("cache_dir", ""));

    for (const auto& g : originals) {
      if (g) {
        livehd::abc::satopt(g.get(), var.get("cache_dir", ""), true);
      }
    }
  }
  static void setup() {
    Eprp_method method("pass.satopt", "Prove combinational mux and memory simplifications for later ABC consumption", &work);
    method.add_label_optional("top", "Analyze this definition and its reachable definitions", "");
    method.add_label_optional("cache_dir", "INTERNAL persistent proof cache under the named workdir", "");
    register_pass(method);
  }
};
Pass_plugin plugin("pass_satopt", Pass_satopt::setup);
}  // namespace
