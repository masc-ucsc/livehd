// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "certificate_ir.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "emit_design_cert.hpp"
#include "gtest/gtest.h"
#include "lean_format.hpp"

namespace lean_export {
namespace {
PinRef constant(std::string value, uint32_t intrinsic, uint32_t width = 0) {
  return {PinKind::Constant, 3, 0, width, intrinsic, std::move(value)};
}
PinRef     input(uint32_t id, uint32_t width) { return {PinKind::Input, id, 0, width, 0, {}}; }
DesignScan binary(ScanOp op, uint32_t width, PinRef a, PinRef b) {
  DesignScan d;
  d.name   = "tiny";
  d.inputs = {
      {"a", 2000000000, a.width, std::nullopt},
      {"b", 2000000001, b.width, std::nullopt}
  };
  d.nodes = {
      {10, op, width, false, {{0, 0, a}, {1, 1, b}}}
  };
  d.outputs = {
      {"y", 0, width, PinRef{PinKind::Node, 10, 0, width, 0, {}}}
  };
  return d;
}
TEST(CertificateIR, UnsizedShiftConstantsPreserveValueAndSign) {
  const auto d = binary(ScanOp::SRA, 29, constant("100663296", 27), constant("4", 3));
  const auto c = build_certificate(d, {});
  ASSERT_EQ(c.sources.size(), 2);
  EXPECT_EQ(c.sources[0].width, 29);
  EXPECT_EQ(c.sources[0].const_int, "100663296");
  EXPECT_EQ(c.sources[1].width, 3);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::SRA);
  EXPECT_EQ(c.slot_of.at(10), 2);
}
TEST(CertificateIR, GetMaskWidensMaskAndPreservesSourceWidth) {
  auto       d = binary(ScanOp::GetMask, 16, input(2000000000, 8), constant("-1", 1));
  const auto c = build_certificate(d, {});
  EXPECT_EQ(c.sources[0].width, 16);
  EXPECT_EQ(c.sources[0].const_int, "-1");
  EXPECT_EQ(c.sources[1].width, 8);
}
TEST(CertificateIR, GetMaskOneBitSourceUnderWideWindowKeepsSourceWidthAndFitsMask) {
  // The EXACT one-bit-source-under-a-32-bit-window case.  It is stated here
  // rather than as an RTL fixture because constant folding removes it from any
  // tiny module before the scan ever sees it.
  //
  // The source keeps its own width 1 -- nothing widens an unsigned value -- and
  // the mask is declared wide enough to HOLD its value, which is the property
  // `pass/lean/tests/d4/getmask_const_width_regress.sh` checks on emitted
  // artifacts.
  auto       d = binary(ScanOp::GetMask, 32, input(2000000000, 1), constant("4294967295", 32, 32));
  const auto c = build_certificate(d, {});
  EXPECT_EQ(c.sources[0].width, 32);
  EXPECT_EQ(c.sources[0].const_int, "4294967295");
  EXPECT_EQ(c.sources[1].width, 1);
}
TEST(CertificateIR, GetMaskMaskWidthJoinsItsOwnCheckedWidthInsteadOfBeingOverridden) {
  // THE DEFECT.  `max(src_width, node.width)` was written as an OVERRIDE, so a
  // mask whose value needs more bits than that join was emitted at the join and
  // silently truncated: `mask_window_const(4, 12) = 4080` landed at width 8 and
  // read back as 240 -- window [4,8) instead of [4,12).
  //
  // Here src_width is 8 and node.width is 4, so the old two-way join is 8 and
  // the mask's own checked width 12 must win.
  auto       d = binary(ScanOp::GetMask, 4, input(2000000000, 8), constant("4080", 12, 12));
  const auto c = build_certificate(d, {});
  EXPECT_EQ(c.sources[0].width, 12) << "the mask must not be truncated below its own value";
  EXPECT_EQ(c.sources[0].const_int, "4080");
  EXPECT_EQ(c.sources[1].width, 8) << "the SOURCE width is preserved, not widened by the mask";
}
TEST(CertificateIR, GetMaskNegativeConstantValueIsDeclaredAtTheWindowCarrier) {
  // SIGNED NEGATIVE CONSTANTS ARE NOT SELF-SUFFICIENT.
  //
  // The native lowering materializes a constant at the TARGET width
  // (`Slop<target_bits>::create_integer(v)` in inou/cgen/cgen_sim.cpp
  // operand()), so the sign occupies every position the window can select.  A
  // certificate source normalized at its own narrow width reads ZERO above it.
  // For a window reaching above that width the two disagree, so excluding
  // constants on the grounds that "a raw value carries its own sign" is not
  // sufficient on its own.
  //
  // -3 needs 3 bits; the window carrier here is 12.
  auto       d = binary(ScanOp::GetMask, 4, constant("-3", 3), constant("4080", 12, 12));
  const auto c = build_certificate(d, {});
  ASSERT_EQ(c.sources.size(), 2);
  const auto& value = c.sources[0].const_int == "-3" ? c.sources[0] : c.sources[1];
  const auto& mask  = c.sources[0].const_int == "-3" ? c.sources[1] : c.sources[0];
  EXPECT_EQ(value.const_int, "-3") << "the raw value is preserved, only its declared width changes";
  EXPECT_EQ(value.width, 12) << "a negative constant must reach the window carrier";
  EXPECT_EQ(mask.width, 12);
}
TEST(CertificateIR, GetMaskPositiveConstantValueKeepsItsOwnNarrowWidth) {
  // The counterpart: a POSITIVE constant is untouched.  Zero-fill above it is
  // already what both sides do, so widening it would be an unneeded change.
  auto       d = binary(ScanOp::GetMask, 4, constant("5", 3), constant("4080", 12, 12));
  const auto c = build_certificate(d, {});
  const auto& value = c.sources[0].const_int == "5" ? c.sources[0] : c.sources[1];
  EXPECT_EQ(value.width, 3);
}
TEST(CertificateIR, ComparisonAndSextWidths) {
  auto d               = binary(ScanOp::LT, 1, input(2000000000, 8), constant("256", 9));
  d.nodes[0].is_signed = true;
  auto c               = build_certificate(d, {});
  EXPECT_EQ(c.nodes[0].op.kind, Operation::SLT);
  EXPECT_EQ(c.sources[0].width, 9);
  d.nodes[0].op    = ScanOp::Sext;
  d.nodes[0].width = 16;
  c                = build_certificate(d, {});
  EXPECT_EQ(c.nodes[0].op.kind, Operation::Sext);
  EXPECT_EQ(c.sources[0].width, 9);
}
TEST(CertificateIR, MuxKeepsFalseBeforeTrue) {
  auto d = binary(ScanOp::Mux, 8, input(2000000000, 1), constant("11", 4));
  d.nodes[0].operands.push_back({2, 2, constant("22", 5)});
  auto c = build_certificate(d, {});
  EXPECT_EQ(c.nodes[0].op.kind, Operation::MuxBool);
  EXPECT_EQ(c.nodes[0].deps, (std::vector<uint32_t>{2000000000, 1000000000, 1000000001}));
  EXPECT_EQ(c.sources[0].const_int, "11");
  EXPECT_EQ(c.sources[1].const_int, "22");
}
TEST(CertificateIR, SetMaskAndSumKeepOperandOrder) {
  auto d = binary(ScanOp::SetMask, 16, constant("255", 8), constant("3", 2));
  d.nodes[0].operands.push_back({2, 2, constant("7", 3)});
  auto c = build_certificate(d, {});
  EXPECT_EQ(c.nodes[0].op.kind, Operation::SetMask);
  EXPECT_EQ(c.sources[0].width, 8);
  EXPECT_EQ(c.sources[1].width, 2);
  // A SECOND addend. Under one-driver-per-sink-pin it cannot reuse pid 0: the
  // adds are the even pids, so this operand stays at pid 2 and is an addend
  // because its BANK is 0. Selecting on the raw pid would keep only the pid-0
  // addend and drop this one -- the add count would read 1 and the dependency
  // list would be a term short, with nothing to flag it.
  d.nodes[0].op               = ScanOp::Sum;
  d.nodes[0].operands[2].bank = 0;
  c                           = build_certificate(d, {});
  EXPECT_EQ(c.nodes[0].op.parameter, 2);
  EXPECT_EQ(c.nodes[0].deps.size(), 3u);
  EXPECT_EQ(c.sources[1].const_int, "7");
  EXPECT_EQ(c.sources[2].const_int, "3");
}
TEST(CertificateIR, WidthAndIdentityErrorsFailBeforeEmission) {
  auto d = binary(ScanOp::EQ, 1, constant("64", 7, 6), input(2000000001, 8));
  EXPECT_THROW(build_certificate(d, {}), std::runtime_error);
  d.nodes[0].operands[0].driver = input(2000000000, 0);
  EXPECT_THROW(build_certificate(d, {}), std::runtime_error);
  CertificateIR c;
  c.nodes.push_back({10, {}, 1, {11}});
  EXPECT_THROW(index_certificate(c), std::runtime_error);
  c.nodes.clear();
  c.sources.resize(2);
  EXPECT_THROW(index_certificate(c), std::runtime_error);
}
DesignScan memory_design(bool sync, bool rom) {
  DesignScan d;
  d.name = "memory";
  Memory m;
  m.id         = 10;
  m.bits       = 8;
  m.addr_width = 2;
  m.size       = 3;
  m.sync       = sync;
  m.is_rom     = rom;
  if (rom) {
    m.rom_contents = {"7", "11", "23"};
  }
  m.ports.push_back({0, 0, constant("1", 1), {}, constant("1", 1), std::nullopt});
  m.read_ports = {0};
  d.memories.emplace(10, m);
  d.nodes.push_back({10, ScanOp::Memory, 0, false, {}});
  d.outputs.push_back({
      "y",
      0,
      8,
      PinRef{PinKind::Memory, 10, 0, 8, 0, {}}
  });
  return d;
}
TEST(CertificateIR, SynchronousRomHasReadRegisterAndNoMutableImage) {
  const auto c = build_certificate(memory_design(true, true), {});
  ASSERT_EQ(c.flops.size(), 1);
  EXPECT_TRUE(c.memories.empty());
  EXPECT_EQ(c.sources.front().kind, SourceKind::RomConst);
  EXPECT_EQ(c.sources.front().rom_contents, (std::vector<std::string>{"7", "11", "23"}));
  EXPECT_EQ(c.nodes.back().op.kind, Operation::MuxBool);
  EXPECT_EQ(c.flops.front().din, c.nodes.back().id);
  EXPECT_NE(c.outputs.front().id, c.flops.front().din);
}
TEST(CertificateIR, ReadForwardingSelectsWriteChainAndByteLanes) {
  auto  d   = memory_design(false, false);
  auto& m   = d.memories.at(10);
  m.wensize = 2;
  m.ports.push_back({1, 0, constant("1", 1), constant("42", 6), constant("3", 2), std::nullopt});
  m.write_ports = {1};
  auto old      = build_certificate(d, {});
  EXPECT_EQ(old.nodes[2].op.kind, Operation::MemRead);
  EXPECT_EQ(old.nodes[2].deps[0], old.sources[0].id);
  m.fwd          = 1;
  auto forwarded = build_certificate(d, {});
  EXPECT_EQ(forwarded.nodes[3].op.kind, Operation::MemWriteBE);
  EXPECT_EQ(forwarded.nodes[3].op.parameter, 4);
  EXPECT_EQ(forwarded.nodes.back().op.kind, Operation::MemRead);
  EXPECT_EQ(forwarded.nodes.back().deps[0], forwarded.memories[0].next_img);
}
TEST(CertificateIR, AsyncResetSourceAndFlopControlsRemainDistinct) {
  DesignScan d;
  d.name   = "async";
  d.inputs = {
      {   "rst", 2000000000, 1, std::nullopt},
      {"enable", 2000000001, 1, std::nullopt}
  };
  Flop f;
  f.id           = 10;
  f.width        = 8;
  f.din          = constant("12", 4);
  f.enable       = input(2000000001, 1);
  f.reset        = input(2000000000, 1);
  f.asynchronous = true;
  f.active_low   = true;
  f.initial      = "7";
  f.reset_input  = 2000000000;
  d.flops.push_back(f);
  d.outputs.push_back({
      "q",
      0,
      8,
      PinRef{PinKind::Flop, 10, 0, 8, 0, {}}
  });
  const auto c = build_certificate(d, {});
  ASSERT_EQ(c.flops.size(), 1);
  EXPECT_EQ(c.flops[0].reset_pin, 2000000000);
  EXPECT_EQ(c.flops[0].enable, 2000000001);
  EXPECT_EQ(c.flops[0].reset_value, "7");
  EXPECT_TRUE(c.flops[0].reset_active_low);
  EXPECT_TRUE(c.sources[0].async_reset);
  EXPECT_TRUE(c.sources[0].reset_active_low);
  d.flops[0].reset_input.reset();
  EXPECT_THROW(build_certificate(d, {}), std::runtime_error);
}
TEST(CertificateIR, EmitterAndAtomicFailureNeedNoGraph) {
  const auto         c = build_certificate(binary(ScanOp::Or, 8, constant("3", 2), constant("-1", 1)), {});
  std::ostringstream out;
  emit_design_cert("tiny", c, out);
  EXPECT_NE(out.str().find("SourceDesc.const 8 ((-Int.ofNat 1))"), std::string::npos);
  EXPECT_NE(out.str().find("deps := #[0, 1], origin := 10"), std::string::npos);
  EXPECT_NE(out.str().find("compileAndRun_correct tiny_designCert tiny_compiles"), std::string::npos);
  const auto path = (std::filesystem::current_path() / "atomic_failure.lean").string();
  {
    std::ofstream existing(path);
    existing << "previous";
  }
  EXPECT_THROW(write_atomic(path,
                            [](std::ostream& os) {
                              os << "partial";
                              throw std::runtime_error("failure");
                            }),
               std::runtime_error);
  std::ifstream file(path);
  std::string   value;
  file >> value;
  EXPECT_EQ(value, "previous");
  EXPECT_FALSE(std::filesystem::exists(path + ".tmp"));
  std::filesystem::remove(path);
}
}  // namespace
}  // namespace lean_export
