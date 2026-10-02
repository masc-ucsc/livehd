//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// register_io over a library that already holds the module: an `--emit-dir
// lg:` directory an earlier compile saved, or an absorbed `lg:` input the
// source redefines. The GraphIO must end up declaring exactly the ports a
// fresh library would get. The add-only merge it replaced kept a renamed
// port next to its new name on one port id: cgen emitted a phantom input and
// pass.partition crashed on the unnamed pin. A module the compile does not
// rebuild, still bound to the old port ids, is refused (`stale-instance`) --
// at once, or after the kernel's prune for a leftover of a prior generation.

#include "upass_tolg.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "node_util.hpp"

namespace {

namespace fs = std::filesystem;
namespace gu = livehd::graph_util;

struct Port {
  std::string name;
  bool        input;
  uint32_t    pid;
};
using Ports = std::vector<Port>;
Port in(std::string_view name, uint32_t pid) { return {std::string(name), true, pid}; }
Port out(std::string_view name, uint32_t pid) { return {std::string(name), false, pid}; }

// A library directory no earlier call used: instance() of a path that does not
// exist yet is an empty library, and a path is never handed out twice (the
// instance() singletons outlive a test, so a reused path would reuse its
// in-memory library under --gtest_repeat).
std::string fresh_lib_dir(std::string_view tag) {
  static int  serial = 0;
  const char* tmp    = std::getenv("TEST_TMPDIR");
  auto        path   = fs::path(tmp ? tmp : fs::temp_directory_path().string()) / std::format("tolg_io_{}_{}", tag, serial++);
  fs::remove_all(path);
  return path.string();
}

std::shared_ptr<Lnast> module_unit(std::string_view name, const Ports& ports) {
  auto ln   = std::make_shared<Lnast>(name);
  auto root = ln->set_root(Lnast_ntype::create_top());
  (void)ln->add_child(root, Lnast_ntype::create_stmts());
  for (const auto& p : ports) {
    auto& list = p.input ? ln->io_meta().inputs : ln->io_meta().outputs;
    list.push_back(Lnast_io_entry{.name = p.name, .bits = 8, .is_signed = false});
  }
  return ln;
}

// (direction, name, port id, bits, unsigned) in declaration order.
using Decl = std::tuple<bool, std::string, uint32_t, uint32_t, bool>;
std::vector<Decl> decls_of(const hhds::GraphIO& gio) {
  std::vector<Decl> out;
  for (const auto& d : gio.get_input_pin_decls()) {
    out.emplace_back(true, d.name, d.port_id, d.bits, d.unsign);
  }
  for (const auto& d : gio.get_output_pin_decls()) {
    out.emplace_back(false, d.name, d.port_id, d.bits, d.unsign);
  }
  return out;
}

// A module an earlier compile left in `lib`: `ports` declared at their pids,
// every input wired to the first output so no stale pin is droppable without
// first dropping the body.
std::shared_ptr<hhds::GraphIO> stale_module(hhds::GraphLibrary& lib, std::string_view name, const Ports& ports) {
  auto gio = lib.create_io(name);
  for (const auto& p : ports) {
    if (p.input) {
      gio->add_input(p.name, p.pid);
    } else {
      gio->add_output(p.name, p.pid);
    }
    gio->set_bits(p.name, 4);
  }
  auto             body = gio->create_graph();
  hhds::Node_class first;
  for (const auto& p : ports) {
    if (p.input) {
      auto inv = gu::create_typed_node(*body, Ntype_op::Not, 4);
      body->get_input_pin(p.name).connect_sink(inv.create_sink_pin(0));
      if (first.is_invalid()) {
        first = inv;
      }
    }
  }
  for (const auto& p : ports) {
    if (!p.input && !first.is_invalid()) {
      first.create_driver_pin(0).connect_sink(body->get_output_pin(p.name));
      break;
    }
  }
  return gio;
}

void register_all(const uPass_tolg::Registry& registry, const std::string& lib_path) {
  uPass_tolg::detect_lg_collisions(registry);
  for (const auto& ln : registry) {
    uPass_tolg::register_io(ln, lib_path, registry);
  }
}

// What a library that never saw the module declares for `ports`.
std::vector<Decl> fresh_decls(std::string_view tag, std::string_view name, const Ports& ports) {
  const auto                 path = fresh_lib_dir(std::format("{}_fresh", tag));
  const uPass_tolg::Registry registry{module_unit(name, ports)};
  register_all(registry, path);
  return decls_of(*livehd::Hhds_graph_library::instance(path).find_io(name));
}

// Re-register `now` over a stale module declared as `before`; the result must
// equal a fresh library's, on the same gid.
void expect_redeclared_like_fresh(std::string_view tag, const Ports& before, const Ports& now) {
  SCOPED_TRACE(std::string(tag));
  const auto path  = fresh_lib_dir(tag);
  auto&      lib   = livehd::Hhds_graph_library::instance(path);
  const auto stale = stale_module(lib, "t.top", before);
  const auto gid   = stale->get_gid();

  const uPass_tolg::Registry registry{module_unit("t.top", now)};
  register_all(registry, path);

  auto gio = lib.find_io("t.top");
  ASSERT_TRUE(gio);
  EXPECT_EQ(gio->get_gid(), gid) << "the gid is stable across the re-declaration";
  EXPECT_EQ(decls_of(*gio), fresh_decls(tag, "t.top", now));
  EXPECT_FALSE(gio->has_graph()) << "the stale body that wired a dropped port is gone (run() rebuilds it)";
}

}  // namespace

TEST(UpassTolgIo, RenamedInputReplacesTheStalePort) {
  // The reported bug: `a` renamed to `a2` left `input 2 a` and `input 2 a2`.
  expect_redeclared_like_fresh("rename", {in("a", 1), in("b", 2), out("y", 3)}, {in("a2", 1), in("b", 2), out("y", 3)});
}

TEST(UpassTolgIo, RemovedAndAddedPortsFollowTheSource) {
  expect_redeclared_like_fresh("remove", {in("a", 1), in("b", 2), out("y", 3)}, {in("a", 1), out("y", 2)});
  expect_redeclared_like_fresh("add", {in("a", 1), out("y", 2)}, {in("a", 1), in("c", 2), out("y", 3)});
}

TEST(UpassTolgIo, ReorderedPortsTakeTheirFreshPositions) {
  // The add-only merge kept `a` on 1 and `b` on 2 (the old header order).
  expect_redeclared_like_fresh("reorder", {in("a", 1), in("b", 2), out("y", 3)}, {in("b", 1), in("a", 2), out("y", 3)});
}

TEST(UpassTolgIo, DirectionChangeRedeclaresThePort) {
  // The add-only merge kept `z` an INPUT (its has_input guard skipped the output).
  expect_redeclared_like_fresh("direction", {in("a", 1), in("z", 2)}, {in("a", 1), out("z", 2)});
}

TEST(UpassTolgIo, UnchangedInterfaceKeepsItsDeclarationsAndBody) {
  const auto path  = fresh_lib_dir("unchanged");
  auto&      lib   = livehd::Hhds_graph_library::instance(path);
  const auto ports = Ports{in("a", 1), in("b", 2), out("y", 3)};
  (void)stale_module(lib, "t.top", ports);

  const uPass_tolg::Registry registry{module_unit("t.top", ports)};
  register_all(registry, path);

  auto gio = lib.find_io("t.top");
  ASSERT_TRUE(gio);
  EXPECT_EQ(decls_of(*gio), fresh_decls("unchanged", "t.top", ports)) << "the widths are re-stamped from the source";
  EXPECT_TRUE(gio->has_graph()) << "a warm recompile leaves the body for run() to replace";

  // A second registration in the same lowering (run()'s own re-declaration)
  // is a no-op.
  uPass_tolg::register_io(registry.front(), path, registry);
  EXPECT_EQ(decls_of(*gio), fresh_decls("unchanged2", "t.top", ports));
}

namespace {

// A module `name` in `lib` instantiating `child` on its `a` and `q` ports.
void instantiate_on_a_and_q(hhds::GraphLibrary& lib, std::string_view name, const std::shared_ptr<hhds::GraphIO>& child) {
  auto io = lib.create_io(name);
  io->add_input("x", 1);
  io->add_output("o", 2);
  io->set_bits("x", 4);
  io->set_bits("o", 4);
  auto body = io->create_graph();
  auto sub  = gu::create_typed_node(*body, Ntype_op::Sub);
  sub.set_subnode(child);
  body->get_input_pin("x").connect_sink(sub.create_sink_pin("a"));
  sub.create_driver_pin("q").connect_sink(body->get_output_pin("o"));
}

// True when the sink holds a stale-instance refusal naming `caller`.
bool refused_stale_instance_of(std::string_view caller) {
  for (const auto& d : livehd::diag::sink().records()) {
    if (d.code == "stale-instance" && d.message.find(std::format("'{}'", caller)) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(UpassTolgIo, ForeignInstanceOfAChangedInterfaceIsRefused) {
  // `other.top` (no unit of this compile owns it: an absorbed lg: input or
  // another compile's module) instantiates `c.child` on `a` and `q`. Its Sub
  // binds by port id, so after a reorder id 1 would silently mean `b`.
  const auto path  = fresh_lib_dir("foreign");
  auto&      lib   = livehd::Hhds_graph_library::instance(path);
  auto       child = stale_module(lib, "c.child", {in("a", 1), in("b", 2), out("q", 3)});
  instantiate_on_a_and_q(lib, "other.top", child);

  livehd::diag::sink().clear();
  const uPass_tolg::Registry registry{module_unit("c.child", {in("b", 1), in("a", 2), out("q", 3)})};
  EXPECT_THROW(register_all(registry, path), std::runtime_error);
  EXPECT_TRUE(refused_stale_instance_of("other.top"));
  livehd::diag::sink().clear();
}

namespace {

// What set_library_origins receives; `known` false = the call is skipped.
struct Origins {
  bool                                             known = false;
  std::string                                      working;
  std::vector<std::string>                         inputs;
  std::vector<std::pair<std::string, std::string>> absorbed;
};

// The stale-instance hint when `other.top` instantiates a reordered `c.child`,
// with `origins` set where the kernel sets them.
std::string stale_hint(std::string_view tag, const Origins& origins) {
  const uPass_tolg::Registry registry{module_unit("c.child", {in("b", 1), in("a", 2), out("q", 3)})};
  const auto                 path  = fresh_lib_dir(tag);
  auto&                      lib   = livehd::Hhds_graph_library::instance(path);
  auto                       child = stale_module(lib, "c.child", {in("a", 1), in("b", 2), out("q", 3)});
  instantiate_on_a_and_q(lib, "other.top", child);
  livehd::diag::sink().clear();
  uPass_tolg::detect_lg_collisions(registry);
  if (origins.known) {
    uPass_tolg::set_library_origins(origins.working, origins.inputs, origins.absorbed);
  }
  EXPECT_THROW(uPass_tolg::register_io(registry.front(), path, registry), std::runtime_error);
  std::string hint;
  for (const auto& d : livehd::diag::sink().records()) {
    if (d.code == "stale-instance") {
      hint = d.hint;
    }
  }
  livehd::diag::sink().clear();
  return hint;
}

void expect_hint_has(const std::string& hint, std::initializer_list<std::string_view> wants) {
  for (const auto want : wants) {
    EXPECT_NE(hint.find(want), std::string::npos) << "hint: " << hint << "\nwant: " << want;
  }
}

}  // namespace

TEST(UpassTolgIo, StaleInstanceHintNamesWhereTheCallerCameFrom) {
  // The fix lives where the stale caller came from: rebuild the lg: INPUT that
  // supplied its body, or the working library an earlier compile left it in.
  // Without set_library_origins both kinds of dir are named.
  expect_hint_has(stale_hint("hint_unknown", Origins{}), {"rebuild the lg: input or emit dir that holds 'other.top'"});

  Origins input;
  input.known   = true;
  input.working = "/w/L";
  input.inputs  = {"/in/L1"};
  input.absorbed.emplace_back("other.top", "/in/L1");
  expect_hint_has(stale_hint("hint_input", input),
                  {"'other.top' comes from the lg: input /in/L1", "compile the source that defines 'other.top'"});

  Origins emit;
  emit.known   = true;
  emit.working = "/w/L";
  expect_hint_has(stale_hint("hint_emit", emit), {"the lg: library /w/L still holds 'other.top'", "rebuild that directory fresh"});

  // Kept by the working library although lg: inputs were absorbed: it may have
  // been copied from one of them, so those are named too.
  auto emit_and_inputs   = emit;
  emit_and_inputs.inputs = {"/in/L1", "/in/L2"};
  emit_and_inputs.absorbed.emplace_back("other.x", "/in/L1");
  expect_hint_has(stale_hint("hint_emit_and_inputs", emit_and_inputs),
                  {"the lg: library /w/L still holds 'other.top'", "any lg: input that also holds it (/in/L1, /in/L2)"});

  // A scratch working library is never named.
  Origins scratch;
  scratch.known = true;
  expect_hint_has(stale_hint("hint_scratch", scratch), {"rebuild the lg: input or emit dir that holds 'other.top'"});
}

TEST(UpassTolgIo, LeftoverOfAPriorUnitIsRefusedOnlyIfItSurvivesThePrune) {
  // Unit `m` of this scope's previous generation left `m.mid`, instantiating
  // `c.child` on `a` and `q`; the edit dropped `m` from the import closure and
  // reorders `c.child`. The kernel prunes `m.mid` right after the lowering, so
  // register_io defers its refusal; check_leftover_instances then refuses only
  // when the module is still in the library (something live kept it).
  const Ports                before = {in("a", 1), in("b", 2), out("q", 3)};
  const Ports                now    = {in("b", 1), in("a", 2), out("q", 3)};
  const uPass_tolg::Registry registry{module_unit("c.child", now)};
  const auto                 library_with = [&](std::string_view tag, std::initializer_list<std::string_view> callers) {
    const auto path  = fresh_lib_dir(tag);
    auto&      lib   = livehd::Hhds_graph_library::instance(path);
    auto       child = stale_module(lib, "c.child", before);
    for (const auto caller : callers) {
      instantiate_on_a_and_q(lib, caller, child);
    }
    return path;
  };
  const auto lower_with_prior_m = [&](const std::string& path) {
    uPass_tolg::detect_lg_collisions(registry);
    uPass_tolg::set_prior_units({"m"});
    uPass_tolg::register_io(registry.front(), path, registry);
  };

  // `m` owns `m` and `m.<x>`, never `mx.<x>`: that one is refused at once.
  livehd::diag::sink().clear();
  EXPECT_THROW(lower_with_prior_m(library_with("leftover_foreign", {"m.mid", "mx.top"})), std::runtime_error);
  EXPECT_TRUE(refused_stale_instance_of("mx.top"));
  EXPECT_FALSE(refused_stale_instance_of("m.mid"));

  // Taken before the lowering: fresh_decls opens a lowering pass of its own,
  // which drops the deferred refusals.
  const auto fresh = fresh_decls("leftover", "c.child", now);
  for (const bool pruned : {true, false}) {
    SCOPED_TRACE(pruned ? "pruned" : "kept");
    livehd::diag::sink().clear();
    const auto path = library_with(pruned ? "leftover_pruned" : "leftover_kept", {"m.mid"});
    auto&      lib  = livehd::Hhds_graph_library::instance(path);
    EXPECT_NO_THROW(lower_with_prior_m(path));
    EXPECT_EQ(decls_of(*lib.find_io("c.child")), fresh);
    if (pruned) {
      lib.delete_graphio("m.mid");  // what compile_cache_prune_graphs does
      EXPECT_NO_THROW(uPass_tolg::check_leftover_instances());
    } else {
      EXPECT_THROW(uPass_tolg::check_leftover_instances(), std::runtime_error);
      EXPECT_TRUE(refused_stale_instance_of("m.mid"));
    }
    EXPECT_NO_THROW(uPass_tolg::check_leftover_instances()) << "the check consumes the deferred refusals";
  }

  // Without the prior generation (no compile cache, or a Verilog compile) a
  // leftover stays in the library, stale: it is refused at once.
  livehd::diag::sink().clear();
  EXPECT_THROW(register_all(registry, library_with("leftover_no_prior", {"m.mid"})), std::runtime_error);
  EXPECT_TRUE(refused_stale_instance_of("m.mid"));
  livehd::diag::sink().clear();
}

TEST(UpassTolgIo, OwnedOrUnaffectedInstancesAreNotRefused) {
  const auto path  = fresh_lib_dir("owned");
  auto&      lib   = livehd::Hhds_graph_library::instance(path);
  auto       child = stale_module(lib, "c.child", {in("a", 1), in("b", 2), out("q", 3)});
  // `c.ghost` is owned by unit `c` (a renamed-away definition the compile
  // prunes after lowering); `other.top` wires only `q`, whose id and name the
  // change keeps.
  for (const auto* name : {"c.ghost", "other.top"}) {
    auto io = lib.create_io(name);
    io->add_output("o", 1);
    io->set_bits("o", 4);
    auto body = io->create_graph();
    auto sub  = gu::create_typed_node(*body, Ntype_op::Sub);
    sub.set_subnode(child);
    if (std::string_view{name} == "c.ghost") {
      auto k = gu::create_const(*body, *Dlop::create_integer(1));
      k.connect_sink(sub.create_sink_pin("a"));
    }
    sub.create_driver_pin("q").connect_sink(body->get_output_pin("o"));
  }
  auto file_root = std::make_shared<Lnast>("c");  // the file unit: owns c.*
  (void)file_root->set_root(Lnast_ntype::create_top());
  const uPass_tolg::Registry registry{file_root, module_unit("c.child", {in("b", 1), in("a", 2), out("q", 3)})};
  EXPECT_NO_THROW(register_all(registry, path));
  EXPECT_EQ(decls_of(*lib.find_io("c.child")), fresh_decls("owned", "c.child", {in("b", 1), in("a", 2), out("q", 3)}));
}

namespace {

// `t.top` (a mod: `a` in, `y` out) whose body is one call `r = <callee>(a=a)`,
// with an explicit generic binding `store(__generic_arg, 8)` ahead of the
// actual when `generic_arg` is set (what prp2lnast emits for `f<W=8>(a=a)`).
std::shared_ptr<Lnast> caller_of(std::string_view callee, bool generic_arg) {
  auto ln = module_unit("t.top", {in("a", 1), out("y", 2)});
  ln->set_lambda_kind("mod");
  auto stmts = ln->get_first_child(ln->get_root());
  auto call  = ln->add_child(stmts, Lnast_ntype::create_func_call());
  ln->add_child(call, Lnast_node::create_ref("r"));
  ln->add_child(call, Lnast_node::create_ref(callee));
  if (generic_arg) {
    auto g = ln->add_child(call, Lnast_ntype::create_store());
    ln->add_child(g, Lnast_node::create_ref("__generic_arg"));
    ln->add_child(g, Lnast_node::create_const("8"));
  }
  auto arg = ln->add_child(call, Lnast_ntype::create_store());
  ln->add_child(arg, Lnast_node::create_ref("a"));
  ln->add_child(arg, Lnast_node::create_ref("a"));
  return ln;
}

// A comb `t.f` (`a` in, `y` out); `is_template` = its generic, unspecialized
// definition, which register_io/run skip.
std::shared_ptr<Lnast> comb_callee(bool is_template, std::string_view name = "t.f") {
  auto ln = module_unit(name, {in("a", 1), out("y", 2)});
  ln->set_lambda_kind("comb");
  ln->set_template(is_template);
  return ln;
}

// The error run() raises lowering `registry.front()`, or "" when it lowers.
std::string tolg_error(std::string_view tag, const uPass_tolg::Registry& registry) {
  const auto path = fresh_lib_dir(tag);
  register_all(registry, path);
  livehd::diag::sink().clear();
  std::string what;
  try {
    (void)uPass_tolg::run(registry.front(), path, registry);
  } catch (const std::runtime_error& e) {
    what = e.what();
  }
  livehd::diag::sink().clear();
  return what;
}

}  // namespace

TEST(UpassTolgCall, CallReachingOnlyAGenericTemplateIsNamedUnspecialized) {
  // The name matches a definition, but only its generic template: saying
  // "undefined function" sent users hunting for an import that is present.
  for (const auto* name : {"t.f", "f"}) {  // exact, and unique `.f` suffix
    SCOPED_TRACE(name);
    const auto what = tolg_error("tmpl", {caller_of(name, false), comb_callee(true)});
    EXPECT_NE(what.find(std::format("call to generic comb '{}' reached lowering unspecialized", name)), std::string::npos) << what;
    EXPECT_EQ(what.find("undefined function"), std::string::npos) << what;
  }
  // Two `.h` templates make the suffix scan ambiguous; the caller's own
  // nested helper `t.top.h` still wins, as it does for a specialization.
  {
    const auto scoped = tolg_error("tmpl_scoped", {caller_of("h", false), comb_callee(true, "t.top.h"), comb_callee(true, "u.h")});
    EXPECT_NE(scoped.find("call to generic comb 'h' reached lowering unspecialized"), std::string::npos) << scoped;
  }
  // No definition at all keeps the plain message.
  const auto what = tolg_error("tmpl_none", {caller_of("g", false), comb_callee(true)});
  EXPECT_NE(what.find("call to undefined function 'g'"), std::string::npos) << what;
}

TEST(UpassTolgCall, UnconsumedGenericBindingIsRefused) {
  // A call still carrying `f<W=8>` reached a non-template (identity/default)
  // definition: the binding would be silently dropped.
  const auto what = tolg_error("generic_arg", {caller_of("t.f", true), comb_callee(false)});
  EXPECT_NE(what.find("the explicit generic binding on the call to 't.f' was not consumed"), std::string::npos) << what;

  // Without the binding the same call lowers past the argument loop.
  const auto ok = tolg_error("generic_arg_none", {caller_of("t.f", false), comb_callee(false)});
  EXPECT_EQ(ok.find("generic binding"), std::string::npos) << ok;
}
