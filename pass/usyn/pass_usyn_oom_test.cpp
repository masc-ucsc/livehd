// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// pass.usyn's memory boundary. This binary replaces the global operator new so
// that one allocation of a synthesis run throws std::bad_alloc, exactly as an
// allocation does at the RLIMIT_AS memory backstop lhd arms at startup. Every
// such run must end in a clean `memory-exhausted` TIMEOUT-memory diagnostic
// (a diag fatal), never a raw bad_alloc or a crash, and the process must still
// synthesize afterwards.
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>

#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass_usyn.hpp"

namespace {
// Only allocations made on the arming (test) thread count, so the injected
// failure lands at the same point of the run however workers are scheduled.
thread_local bool     armed      = false;
thread_local uint64_t seen       = 0;
thread_local uint64_t fail_after = 0;  // throw on this allocation number, then disarm

void* allocate(std::size_t size) {
  if (armed && ++seen == fail_after) {
    armed = false;
    throw std::bad_alloc();
  }
  if (void* p = std::malloc(size == 0 ? 1 : size)) {
    return p;
  }
  throw std::bad_alloc();
}
}  // namespace

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void  operator delete(void* p) noexcept { std::free(p); }
void  operator delete[](void* p) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
namespace fs = std::filesystem;
namespace gu = livehd::graph_util;

std::shared_ptr<hhds::Graph> design(hhds::GraphLibrary& library) {
  auto io = library.create_io("top");
  io->add_input("a", 1);
  io->add_input("b", 2);
  io->add_input("clk", 3);
  io->add_output("y", 4);
  for (auto name : {"a", "b", "clk", "y"}) {
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  auto graph = io->create_graph();
  auto node  = gu::create_typed_node(*graph, Ntype_op::And);
  for (auto name : {"a", "b"}) {
    graph->get_input_pin(name).connect_sink(gu::setup_sink_pid(node, 0));
  }
  auto pin = node.create_driver_pin(0);
  gu::set_ubits(pin, 1);
  auto reg = gu::create_typed_node(*graph, Ntype_op::Flop);
  pin.connect_sink(gu::setup_sink_by_name(reg, "din"));
  graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(reg, "clock_pin"));
  auto q = reg.create_driver_pin(0);
  gu::set_ubits(q, 1);
  gu::set_pin_name(q, "state");
  q.connect_sink(graph->get_output_pin("y"));
  return graph;
}

const livehd::diag::Diagnostic* memory_exhausted() {
  const livehd::diag::Diagnostic* found = nullptr;
  for (const auto& r : livehd::diag::sink().records()) {
    if (r.code == "memory-exhausted") {
      found = &r;
    }
  }
  return found;
}
}  // namespace

TEST(PassUsynMemory, AllocationFailureAnywhereIsTimeoutMemory) {
  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_human_stderr(false);
  sink.set_jsonl_path("off");
  auto root = (fs::temp_directory_path() / "livehd-usyn-oom-XXXXXX").string();
  ASSERT_NE(mkdtemp(root.data()), nullptr);

  hhds::GraphLibrary                          input;
  const auto                                  source = design(input);
  const auto                                  output = root + "/net";
  livehd::Hhds_graph_library::Scoped_instance scope(output);
  Eprp_var                                    var;
  var.graphs.push_back(source);
  var.dict["top"]  = "top";
  var.dict["tmap"] = "none";
  var.dict["out"]  = output;
  var.dict["qor"]  = root + "/qor.json";
  var.set_stage_labels(var.dict);

  // From the first allocation of the run to deep inside the native search:
  // each point either fails cleanly or (past the run's last allocation)
  // completes normally. The first one must fail.
  for (uint64_t at : {1ULL, 10ULL, 100ULL, 1000ULL, 10000ULL, 100000ULL}) {
    SCOPED_TRACE(at);
    sink.clear();
    seen       = 0;
    fail_after = at;
    armed      = true;
    bool failed = false;
    try {
      Pass_usyn::work(var);
    } catch (const std::bad_alloc&) {
      armed = false;
      FAIL() << "std::bad_alloc escaped pass.usyn";
    } catch (const std::runtime_error&) {
      failed = true;  // a diag fatal
    }
    const bool injected = !armed;
    armed               = false;
    if (!injected) {
      EXPECT_FALSE(failed) << "the run failed without an allocation failure";
      break;  // past the run's last allocation
    }
    // A run whose injected failure was swallowed below (a caught allocation
    // that the code recovers from) may still succeed; one that fails must be
    // the memory boundary's TIMEOUT-memory, never another error.
    if (failed || sink.has_halting_errors()) {
      const auto* rec = memory_exhausted();
      ASSERT_NE(rec, nullptr) << (sink.records().empty() ? std::string{} : sink.records().back().message);
      EXPECT_EQ(rec->category, "unsupported");
      EXPECT_TRUE(rec->message.starts_with("TIMEOUT-memory: pass.usyn ran out of memory")) << rec->message;
      EXPECT_TRUE(rec->message.find("process memory budget of") != std::string::npos
                  || rec->message.find("the host's memory") != std::string::npos)
          << rec->message;
    }
    if (at == 1) {
      EXPECT_TRUE(failed) << "an allocation failure at the first allocation must fail the run";
    }
  }

  // The process is intact: a normal run still synthesizes and publishes.
  sink.clear();
  Pass_usyn::work(var);
  EXPECT_FALSE(sink.has_halting_errors()) << (sink.records().empty() ? std::string{} : sink.records().back().message);
  EXPECT_TRUE(livehd::Hhds_graph_library::instance(output).find_io("top"));
  sink.clear();
  fs::remove_all(root);
}
