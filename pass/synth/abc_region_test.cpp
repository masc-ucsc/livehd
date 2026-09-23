// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_region.hpp"

#include <filesystem>
#include <fstream>

#include "abc_tmap.hpp"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
}
// clang-format on

namespace livehd::synth {
namespace {
// A backend that refuses every function: the region must fall back to the ABC
// flow (frame untouched) and say why.
class Refusing_backend final : public Tmap_backend {
  Mapped_fragment map(const Mapping_request&) override {
    return {.status = Map_status::unsupported, .reason = "test backend refuses"};
  }
};

TEST(AbcRegion, CutManifestPreservesOrderedStatePairsAndAllInitializations) {
  auto* frame    = Abc_FrameCreate();
  auto* previous = Abc_FrameEnter(frame);
  auto* net      = Abc_NtkAlloc(ABC_NTK_LOGIC, ABC_FUNC_SOP, 1);
  struct Cleanup {
    Abc_Frame_t* frame;
    Abc_Frame_t* previous;
    Abc_Ntk_t*   net;
    ~Cleanup() {
      Abc_NtkDelete(net);
      Abc_FrameLeave(previous);
      Abc_FrameDestroy(frame);
    }
  } cleanup{frame, previous, net};
  auto*       pi                = Abc_NtkCreatePi(net);
  auto*       po                = Abc_NtkCreatePo(net);
  const char* initializations[] = {"unspecified", "zero", "one", "dont_care"};
  for (int i = 0; i < 4; ++i) {
    auto* latch  = Abc_NtkCreateLatch(net);
    auto* input  = Abc_NtkCreateBi(net);
    auto* output = Abc_NtkCreateBo(net);
    Abc_ObjAddFanin(input, pi);
    Abc_ObjAddFanin(latch, input);
    Abc_ObjAddFanin(output, latch);
    if (i == 0) {
      Abc_LatchSetInitNone(latch);
      Abc_ObjAddFanin(po, output);
    }
    if (i == 1) {
      Abc_LatchSetInit0(latch);
    }
    if (i == 2) {
      Abc_LatchSetInit1(latch);
    }
    if (i == 3) {
      Abc_LatchSetInitDc(latch);
    }
  }
  const auto boundary = abc_source_boundary(net);
  ASSERT_TRUE(boundary);
  ASSERT_EQ(boundary->inputs.size(), 5);
  ASSERT_EQ(boundary->outputs.size(), 5);
  ASSERT_EQ(boundary->states.size(), 4);
  EXPECT_EQ(boundary->inputs[0].kind, "primary");
  EXPECT_EQ(boundary->outputs[0].kind, "primary");
  for (uint32_t i = 0; i < 4; ++i) {
    EXPECT_EQ(boundary->states[i].init, initializations[i]);
    EXPECT_EQ(boundary->states[i].input, i + 1);
    EXPECT_EQ(boundary->states[i].output, i + 1);
    EXPECT_EQ(boundary->inputs[i + 1].state, i);
    EXPECT_EQ(boundary->outputs[i + 1].state, i);
    EXPECT_EQ(boundary->inputs[i + 1].kind, "state");
    EXPECT_EQ(boundary->outputs[i + 1].kind, "state");
  }
  EXPECT_FALSE(abc_source_boundary(nullptr));
  EXPECT_FALSE(abc_source_boundary(net, [] { return false; }));
  Abc_NtkBox(net, 0)->pData = reinterpret_cast<void*>(uintptr_t{99});
  EXPECT_FALSE(abc_source_boundary(net));
}

// A one-output region read into a fresh frame with the hermetic test Liberty.
struct Region_frame {
  std::filesystem::path dir;
  Abc_Frame_t*          frame    = nullptr;
  Abc_Frame_t*          previous = nullptr;
  Abc_Ntk_t*            original = nullptr;
  explicit Region_frame(std::string_view blif) {
    auto pattern = (std::filesystem::temp_directory_path() / "livehd-unate-region-XXXXXX").string();
    if (!mkdtemp(pattern.data())) {
      throw std::runtime_error("mkdtemp");
    }
    dir = pattern;
    std::ofstream(dir / "region.blif") << blif;
    frame    = Abc_FrameCreate();
    previous = Abc_FrameEnter(frame);
    if (Cmd_CommandExecute(frame, "read_lib -s inou/prp/tests/abc/test.lib")
        || Cmd_CommandExecute(frame, ("read_blif " + (dir / "region.blif").string()).c_str())) {
      throw std::runtime_error("cannot load the test region");
    }
    original = Abc_NtkDup(Abc_FrameReadNtk(frame));
  }
  ~Region_frame() {
    Abc_NtkDelete(original);
    Abc_FrameLeave(previous);
    Abc_FrameDestroy(frame);
    std::filesystem::remove_all(dir);
  }
};

constexpr std::string_view kMajority
    = ".model top\n.inputs a b c d\n.outputs y z\n.names a b c y\n11- 1\n1-1 1\n-11 1\n.names a b d z\n1-1 1\n-01 1\n.end\n";

TEST(AbcRegion, DecomposesIntoUnateFunctionsAndInstallsOnlyTechnologyMappedCells) {
  Region_frame region(kMajority);
  Search_options               search;  // default: one recipe, unbounded depth
  livehd::abc::Map_options     mapping;
  mapping.library = "inou/prp/tests/abc/test.lib";
  Witness_archive witnesses(region.dir / "witness.jsonl", "test", 1024 * 1024, search);
  Abc_tmap        backend("lhd/lhd");  // no time budget: maps in process
  const auto      report = map_abc_region(region.frame, region.original, nullptr, "top", mapping, search, -1, {}, backend, witnesses);
  rapidjson::Document doc;
  doc.Parse(report.data(), report.size());
  ASSERT_FALSE(doc.HasParseError()) << report;
  EXPECT_STREQ(doc["status"].GetString(), "unate") << report;
  EXPECT_GT(doc["functions"].GetUint64(), 0);
  EXPECT_GT(doc["depth"].GetUint(), 0);
  EXPECT_LE(doc["max_support"].GetUint(), 6);
  auto* mapped = Abc_FrameReadNtk(region.frame);
  ASSERT_TRUE(Abc_NtkIsMappedLogic(mapped));
  // The installed network computes the region's function (checked here, in the
  // test: synthesis itself leaves equivalence checking to `lhd lec`).
  auto* golden  = Abc_NtkStrash(region.original, 0, 1, 0);
  auto* dup     = Abc_NtkDup(mapped);
  auto* revised = Abc_NtkStrash(dup, 0, 1, 0);
  auto* miter   = Abc_NtkMiter(golden, revised, 1, 0, 0, 0);
  EXPECT_EQ(Abc_NtkMiterSat(miter, 100000, 0, 0, nullptr, nullptr), 1);
  for (auto* n : {miter, revised, dup, golden}) {
    Abc_NtkDelete(n);
  }
}

// Sources take the bits of `inputs` in id order.
std::vector<bool> evaluate(const Logic_network& net, uint32_t inputs) {
  std::vector<bool> value(net.nodes.size());
  uint32_t          source = 0;
  for (size_t i = 0; i < net.nodes.size(); ++i) {
    const auto& n = net.nodes[i];
    if (n.source) {
      value[i] = ((inputs >> source++) & 1) != 0;
      continue;
    }
    uint32_t x = 0;
    for (size_t k = 0; k < n.inputs.size(); ++k) {
      x |= uint32_t{value[n.inputs[k]]} << k;
    }
    value[i] = n.table.get(x);
  }
  std::vector<bool> out;
  for (auto o : net.outputs) {
    out.push_back(value[o]);
  }
  return out;
}

// The blaster's tape becomes the optimizer's source with no ABC in between:
// sources in CI order (PIs, then latches, although the latch was recorded
// first), hashed and folded gates, complements absorbed, one output per CO.
TEST(AbcRegion, TapeImportHashesFoldsAndKeepsTheCiCoOrder) {
  using Op = livehd::abc::Blast_tape::Op;
  livehd::abc::Blast_tape tape;
  auto                    node = [&](Op op, uint32_t a = 0, uint32_t b = 0) {
    tape.nodes.push_back({op, a, b});
    return static_cast<uint32_t>(tape.nodes.size() - 1);
  };
  auto po = [&](const char* name, uint32_t bit) {
    tape.pos.push_back({name, bit});
    node(Op::po, bit, static_cast<uint32_t>(tape.pos.size() - 1));
  };
  tape.latches.push_back({"q_%r0_0", 'x'});
  const auto q = node(Op::latch, 0);
  tape.pis.push_back({"a_b0"});
  const auto a = node(Op::pi, 0);
  tape.pis.push_back({});
  const auto b    = node(Op::pi, 1);
  const auto zero = node(Op::const0);
  const auto one  = node(Op::const1);
  const auto nb   = node(Op::inv, b);
  const auto y    = node(Op::and2, a, nb);
  const auto z    = node(Op::and2, nb, a);  // the same gate, commuted
  node(Op::or2, a, b);                      // never observed
  const auto xnor = node(Op::inv, node(Op::xor2, a, q));
  const auto k    = node(Op::and2, a, one);  // folds to a
  po("y", y);
  po("z", z);
  po("w", xnor);
  po("c", zero);
  po("k", k);
  tape.latches[0].d = xnor;
  for (const bool xor_nodes : {true, false}) {
    const auto net = import_tape(tape, xor_nodes);
    ASSERT_TRUE(net.valid());
    ASSERT_GE(net.nodes.size(), 4u);
    for (int i = 0; i < 3; ++i) {
      EXPECT_TRUE(net.nodes[i].source) << i;
    }
    EXPECT_FALSE(net.nodes[3].source);  // the constant
    ASSERT_EQ(net.outputs.size(), 6u);  // five POs, then the latch input
    EXPECT_EQ(net.outputs[0], net.outputs[1]);
    EXPECT_EQ(net.outputs[4], 0u);  // a itself
    EXPECT_EQ(net.outputs[2], net.outputs[5]);
    size_t gates = 0, inverters = 0;
    for (const auto& n : net.nodes) {
      gates += !n.source && n.inputs.size() == 2;
      inverters += !n.source && n.inputs.size() == 1;
    }
    // y, then the XOR as one node or three ANDs (whose last one is the XNOR).
    EXPECT_EQ(gates, xor_nodes ? 2u : 4u);
    // NOT(const1) for the zero output, and NOT(xor) only with an XOR node.
    EXPECT_EQ(inverters, xor_nodes ? 2u : 1u);
    for (uint32_t v = 0; v < 8; ++v) {
      const bool va = (v & 1) != 0, vb = (v & 2) != 0, vq = (v & 4) != 0;
      const auto out = evaluate(net, v);
      EXPECT_EQ(out[0], va && !vb) << v;
      EXPECT_EQ(out[1], va && !vb) << v;
      EXPECT_EQ(out[2], va == vq) << v;
      EXPECT_FALSE(out[3]) << v;
      EXPECT_EQ(out[4], va) << v;
      EXPECT_EQ(out[5], va == vq) << v;
    }
  }
}

// pass.synth.split on a region with a small cone (z, one gate) and a wide
// one (y = AND of 12 XORs over 24 inputs: more than three gates). The wide cone
// keeps its top gates over an ABC-tmapped remainder; the installed network
// computes the region's function.
constexpr std::string_view kWide = ".model top\n.inputs x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 x10 x11 x12 x13 x14 x15 x16 x17 x18 x19 x20 x21 x22 x23\n.outputs y z\n.names x0 x1 e0\n10 1\n01 1\n.names x2 x3 e1\n10 1\n01 1\n.names x4 x5 e2\n10 1\n01 1\n.names x6 x7 e3\n10 1\n01 1\n.names x8 x9 e4\n10 1\n01 1\n.names x10 x11 e5\n10 1\n01 1\n.names x12 x13 e6\n10 1\n01 1\n.names x14 x15 e7\n10 1\n01 1\n.names x16 x17 e8\n10 1\n01 1\n.names x18 x19 e9\n10 1\n01 1\n.names x20 x21 e10\n10 1\n01 1\n.names x22 x23 e11\n10 1\n01 1\n.names e0 e1 a0\n11 1\n.names e2 e3 a1\n11 1\n.names e4 e5 a2\n11 1\n.names e6 e7 a3\n11 1\n.names e8 e9 a4\n11 1\n.names e10 e11 a5\n11 1\n.names a0 a1 a6\n11 1\n.names a2 a3 a7\n11 1\n.names a4 a5 a8\n11 1\n.names a6 a7 a9\n11 1\n.names a9 a8 a10\n11 1\n.names a10 y\n1 1\n.names x0 x1 z\n11 1\n.end\n";

TEST(AbcRegion, SplitKeepsTopGatesOverAnAbcTmappedRemainder) {
  Region_frame             region(kWide);
  Search_options           search;
  search.split = true;
  livehd::abc::Map_options mapping;
  mapping.library = "inou/prp/tests/abc/test.lib";
  Witness_archive witnesses(region.dir / "witness.jsonl", "test", 1024 * 1024, search);
  Abc_tmap        backend("lhd/lhd");
  const auto      report = map_abc_region(region.frame, region.original, nullptr, "top", mapping, search, -1, {}, backend, witnesses);
  rapidjson::Document doc;
  doc.Parse(report.data(), report.size());
  ASSERT_FALSE(doc.HasParseError()) << report;
  EXPECT_STREQ(doc["status"].GetString(), "unate") << report;
  EXPECT_STREQ(doc["variant"].GetString(), "split");
  EXPECT_EQ(doc["cones_2"].GetUint64(), 1u) << report;
  EXPECT_EQ(doc["cones_more"].GetUint64(), 1u) << report;
  EXPECT_GT(doc["remainder_outputs"].GetUint64(), 0u);
  EXPECT_GT(doc["remainder_cells"].GetUint64(), 0u);
  EXPECT_GT(doc["functions"].GetUint64(), 0u);
  auto* mapped = Abc_FrameReadNtk(region.frame);
  ASSERT_TRUE(mapped && Abc_NtkIsMappedLogic(mapped));
  auto* golden  = Abc_NtkStrash(region.original, 0, 1, 0);
  auto* dup     = Abc_NtkDup(mapped);
  auto* revised = Abc_NtkStrash(dup, 0, 1, 0);
  auto* miter   = Abc_NtkMiter(golden, revised, 1, 0, 0, 0);
  EXPECT_EQ(Abc_NtkMiterSat(miter, 100000, 0, 0, nullptr, nullptr), 1);
  for (auto* n : {miter, revised, dup, golden}) {
    Abc_NtkDelete(n);
  }
}

TEST(AbcRegion, TmapFailureLeavesTheRegionToTheAbcFlow) {
  Region_frame                 region(kMajority);
  auto*                        before = Abc_FrameReadNtk(region.frame);
  Search_options               search;
  livehd::abc::Map_options     mapping;
  mapping.library = "inou/prp/tests/abc/test.lib";
  Witness_archive  witnesses(region.dir / "witness.jsonl", "test", 1024 * 1024, search);
  Refusing_backend backend;
  const auto       report = map_abc_region(region.frame, region.original, nullptr, "top", mapping, search, -1, {}, backend, witnesses);
  rapidjson::Document doc;
  doc.Parse(report.data(), report.size());
  ASSERT_FALSE(doc.HasParseError()) << report;
  EXPECT_STREQ(doc["status"].GetString(), "abc_fallback");
  EXPECT_NE(std::string(doc["reason"].GetString()).find("test backend refuses"), std::string::npos) << report;
  EXPECT_EQ(Abc_FrameReadNtk(region.frame), before);
}
}  // namespace
}  // namespace livehd::synth
