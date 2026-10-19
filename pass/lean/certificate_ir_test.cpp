// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "certificate_ir.hpp"

#include <algorithm>
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
  // A signed OPERAND makes the comparison signed; a non-negative constant then
  // carries a zero sign bit (256 needs 9 bits, so 10).
  auto signed_a        = input(2000000000, 8);
  signed_a.is_signed   = true;
  auto d               = binary(ScanOp::LT, 1, signed_a, constant("256", 9));
  auto c               = build_certificate(d, {});
  EXPECT_EQ(c.nodes[0].op.kind, Operation::SLT);
  EXPECT_EQ(c.sources[0].width, 10);
  d                = binary(ScanOp::LT, 1, input(2000000000, 8), constant("256", 9));
  d.nodes[0].op    = ScanOp::Sext;
  d.nodes[0].width = 16;
  c                = build_certificate(d, {});
  EXPECT_EQ(c.nodes[0].op.kind, Operation::Sext);
  EXPECT_EQ(c.sources[0].width, 9);
}
TEST(CertificateIR, DivSignednessIsPerOperandNotTheQuotients) {
  auto signed_input = [](uint32_t id, uint32_t width) {
    auto p      = input(id, width);
    p.is_signed = true;
    return p;
  };
  auto const_source = [](const CertificateIR& c) {
    const auto it = std::find_if(c.sources.begin(), c.sources.end(), [](const Source& s) { return s.kind == SourceKind::Const; });
    EXPECT_NE(it, c.sources.end());
    return it == c.sources.end() ? Source{} : *it;
  };
  // Both unsigned: Op_UDiv, even when the quotient pin is signed.
  auto d               = binary(ScanOp::Div, 8, input(2000000000, 8), input(2000000001, 4));
  d.nodes[0].is_signed = true;
  auto c               = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 1);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::UDiv);
  EXPECT_EQ(c.nodes[0].deps, (std::vector<uint32_t>{2000000000, 2000000001}));
  // A signed dividend over an unsigned divisor: Op_SDiv, and the unsigned
  // divisor gains a zero bit first so its two's-complement value is its value.
  d                    = binary(ScanOp::Div, 8, signed_input(2000000000, 8), input(2000000001, 4));
  c                    = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 2);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::Or);
  EXPECT_EQ(c.nodes[0].width, 5);
  EXPECT_EQ(c.nodes[0].deps, (std::vector<uint32_t>{2000000001}));
  EXPECT_EQ(c.nodes[1].op.kind, Operation::SDiv);
  EXPECT_EQ(c.nodes[1].deps, (std::vector<uint32_t>{2000000000, c.nodes[0].id}));
  // Both signed: no resize.
  d = binary(ScanOp::Div, 8, signed_input(2000000000, 8), signed_input(2000000001, 4));
  c = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 1);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::SDiv);
  // A negative constant makes the division signed; a non-negative constant
  // under signed math carries a zero sign bit.
  d = binary(ScanOp::Div, 8, input(2000000000, 8), constant("-3", 3));
  c = build_certificate(d, {});
  EXPECT_EQ(c.nodes.back().op.kind, Operation::SDiv);
  EXPECT_EQ(const_source(c).width, 3);
  EXPECT_EQ(const_source(c).const_int, "-3");
  EXPECT_EQ(c.nodes.front().op.kind, Operation::Or);
  EXPECT_EQ(c.nodes.front().width, 9);
  d = binary(ScanOp::Div, 8, signed_input(2000000000, 8), constant("7", 3));
  c = build_certificate(d, {});
  EXPECT_EQ(c.nodes.back().op.kind, Operation::SDiv);
  EXPECT_EQ(const_source(c).width, 4);
}
TEST(CertificateIR, DivConstantKeepsItsValueUnderANarrowQuotient) {
  // `a / 200` with an 8-bit `a` has a 1-bit quotient. Materializing the divisor
  // at the quotient width would make it 200 mod 2 = 0.
  const auto d = binary(ScanOp::Div, 1, input(2000000000, 8), constant("200", 8));
  const auto c = build_certificate(d, {});
  EXPECT_EQ(c.nodes[0].op.kind, Operation::UDiv);
  ASSERT_EQ(c.sources.size(), 2);
  const auto& k = c.sources[0].kind == SourceKind::Const ? c.sources[0] : c.sources[1];
  EXPECT_EQ(k.width, 8);
  EXPECT_EQ(k.const_int, "200");
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
  m.fwd_rows     = {{true}};  // read 0 forwards write 0
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
  // A reset computed from STATE (another flop's Q) is still refused: an
  // override node is exact only for a reset fixed within the step.
  Flop g;
  g.id    = 11;
  g.width = 1;
  g.din   = constant("1", 1);
  d.flops.push_back(g);
  d.flops[0].reset_input.reset();
  d.flops[0].reset = PinRef{PinKind::Flop, 11, 0, 1, 0, {}};
  EXPECT_THROW(build_certificate(d, {}), std::runtime_error);
}
// Decision 2B: an asynchronous reset that is a combinational function of
// primary inputs (CORE-ET B05's `scanmode ? scan_reset_n : rst_ni`). The reader
// gets `MuxBool(reset, Q, resetValue)` -- arms swapped for an active-low reset
// -- whose selector is the slot `FlopDesc.resetPin` names; the source itself is
// the plain stored value, and the cone is lowered before the reader even though
// the reader comes first in the scan's order.
TEST(CertificateIR, InputDerivedAsyncResetReadsThroughAnOverride) {
  DesignScan d;
  d.name   = "async_mux";
  d.inputs = {
      {"scanmode", 2000000000, 1, std::nullopt},
      {"scan_rst", 2000000001, 1, std::nullopt},
      {   "rst_ni", 2000000002, 1, std::nullopt}
  };
  Flop f;
  f.id           = 10;
  f.width        = 8;
  f.din          = constant("12", 4);
  f.reset        = PinRef{PinKind::Node, 30, 0, 1, 0, {}};
  f.asynchronous = true;
  f.active_low   = true;
  f.initial      = "7";
  d.flops.push_back(f);
  // The READER first: q + 1, which the scan orders before the reset mux.
  DesignNode reader;
  reader.id       = 20;
  reader.op       = ScanOp::Sum;
  reader.width    = 8;
  reader.operands = {
      {0, 0, PinRef{PinKind::Flop, 10, 0, 8, 0, {}}},
      {2, 0, constant("1", 8)}
  };
  DesignNode rmux;
  rmux.id       = 30;
  rmux.op       = ScanOp::Mux;
  rmux.width    = 1;
  rmux.operands = {
      {0, 0, input(2000000000, 1)},
      {1, 1, input(2000000002, 1)},
      {2, 2, input(2000000001, 1)}
  };
  d.nodes = {reader, rmux};
  d.outputs.push_back({"q", 0, 8, PinRef{PinKind::Node, 20, 0, 8, 0, {}}});
  const auto c = build_certificate(d, {});
  ASSERT_EQ(c.flops.size(), 1);
  EXPECT_EQ(c.flops[0].reset_pin, 30);
  EXPECT_TRUE(c.flops[0].async_reset);
  EXPECT_TRUE(c.flops[0].reset_active_low);
  // The reset mux is lowered first, then the override, then the reader.
  ASSERT_EQ(c.nodes.size(), 3);
  EXPECT_EQ(c.nodes[0].id, 30);
  const auto& ov = c.nodes[1];
  EXPECT_EQ(ov.op.kind, Operation::MuxBool);
  ASSERT_EQ(ov.deps.size(), 3);
  EXPECT_EQ(ov.deps[0], 30);  // the selector IS the reset slot
  EXPECT_EQ(ov.deps[2], 10);  // active LOW: reset=1 means NOT asserted, so Q on the true arm
  EXPECT_EQ(c.nodes[2].id, 20);
  EXPECT_EQ(c.nodes[2].deps[0], ov.id);  // the reader reads the override, never the raw Q
  bool raw_q = false;
  for (const auto& src : c.sources) {
    if (src.id == 10) {
      raw_q = true;
      EXPECT_FALSE(src.async_reset);  // a plain flopQ: the stored value
    }
    if (src.id == ov.deps[1]) {
      EXPECT_EQ(src.kind, SourceKind::Const);
      EXPECT_EQ(src.const_int, "7");
    }
  }
  EXPECT_TRUE(raw_q);
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

// ---- operand signedness (the certificate's operators read unsigned and
// zero-extend, except SLT/SGT/SRA/SDiv; LiveHD reads each pin as the integer
// its own signedness gives)
PinRef signed_in(uint32_t id, uint32_t width) {
  auto p      = input(id, width);
  p.is_signed = true;
  return p;
}
TEST(CertificateIR, CompareSignednessFollowsTheOperandsNotTheResult) {
  // `$signed(a) < $signed(b)` has an UNSIGNED 1-bit result: the result pin's
  // signedness used to pick ULT.
  auto d               = binary(ScanOp::LT, 1, signed_in(2000000000, 32), signed_in(2000000001, 32));
  d.nodes[0].is_signed = false;
  auto c               = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 1);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::SLT);
  // unsigned operands under a signed-looking result: still unsigned
  d                    = binary(ScanOp::GT, 1, input(2000000000, 32), input(2000000001, 32));
  d.nodes[0].is_signed = true;
  c                    = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 1);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::UGT);
  // mixed: SLT, the unsigned operand gains a zero bit
  d = binary(ScanOp::LT, 1, signed_in(2000000000, 8), input(2000000001, 4));
  c = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 2);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::Or);
  EXPECT_EQ(c.nodes[0].width, 5);
  EXPECT_EQ(c.nodes[1].op.kind, Operation::SLT);
  EXPECT_EQ(c.nodes[1].deps, (std::vector<uint32_t>{2000000000, c.nodes[0].id}));
}
TEST(CertificateIR, SraOfAnUnsignedValueShiftsInZeros) {
  // `a >> k` of an unsigned 32-bit `a` is an LGraph SRA whose value is
  // non-negative: bv_sra must not read bit 31 as a sign.
  auto d = binary(ScanOp::SRA, 32, input(2000000000, 32), input(2000000001, 5));
  auto c = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 2);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::Or);
  EXPECT_EQ(c.nodes[0].width, 33);
  EXPECT_EQ(c.nodes[1].op.kind, Operation::SRA);
  EXPECT_EQ(c.nodes[1].deps, (std::vector<uint32_t>{c.nodes[0].id, 2000000001}));
  // a signed value keeps its own width
  d = binary(ScanOp::SRA, 32, signed_in(2000000000, 32), input(2000000001, 5));
  c = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 1);
  EXPECT_EQ(c.nodes[0].deps, (std::vector<uint32_t>{2000000000, 2000000001}));
}
TEST(CertificateIR, NarrowSignedOperandIsSignExtendedForAZeroExtendingRead) {
  // Sum reads its operands unsigned at the result width: a 3-bit signed -4
  // (100) must read 11111100, not 00000100.
  auto d = binary(ScanOp::Sum, 8, signed_in(2000000000, 3), input(2000000001, 8));
  auto c = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 2);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::Sext);
  EXPECT_EQ(c.nodes[0].width, 8);
  ASSERT_EQ(c.nodes[0].deps.size(), 2);
  EXPECT_EQ(c.nodes[0].deps[0], 2000000000);
  const auto amount = std::find_if(c.sources.begin(), c.sources.end(), [](const Source& s) { return s.kind == SourceKind::Const; });
  ASSERT_NE(amount, c.sources.end());
  EXPECT_EQ(amount->const_int, "3") << "sign position: the operand's own top bit";
  EXPECT_EQ(c.nodes[1].op.kind, Operation::Sum);
  EXPECT_EQ(c.nodes[1].deps, (std::vector<uint32_t>{c.nodes[0].id, 2000000001}));
  // as wide as the read: no extension
  d = binary(ScanOp::Sum, 8, signed_in(2000000000, 8), input(2000000001, 8));
  c = build_certificate(d, {});
  EXPECT_EQ(c.nodes.size(), 1);
}
TEST(CertificateIR, EqualityOfMixedSignednessUsesOneWiderWidth) {
  // signed 3-bit -4 (100) against unsigned 3-bit 4 (100): different integers,
  // the same bits. One extra bit separates them.
  auto d = binary(ScanOp::EQ, 1, signed_in(2000000000, 3), input(2000000001, 3));
  auto c = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 2);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::Sext);
  EXPECT_EQ(c.nodes[0].width, 4);
  EXPECT_EQ(c.nodes[1].op.kind, Operation::EQ);
}
TEST(CertificateIR, MuxOptionSignedNarrowerIsSignExtended) {
  // LiveHD rewrites 4'b1100 as the signed 3-bit -4; a 5-bit mux must read it
  // as 11100 (bv_resize zero-extends).
  DesignScan d;
  d.name   = "tiny";
  d.inputs = {
      {"s", 2000000000, 1, std::nullopt},
      {"a", 2000000001, 3, std::nullopt},
      {"b", 2000000002, 5, std::nullopt}
  };
  d.nodes = {
      {10, ScanOp::Mux, 5, false, {{0, 0, input(2000000000, 1)}, {1, 1, signed_in(2000000001, 3)}, {2, 2, input(2000000002, 5)}}}
  };
  d.outputs = {
      {"y", 0, 5, PinRef{PinKind::Node, 10, 0, 5, 0, {}}}
  };
  auto c = build_certificate(d, {});
  ASSERT_EQ(c.nodes.size(), 2);
  EXPECT_EQ(c.nodes[0].op.kind, Operation::Sext);
  EXPECT_EQ(c.nodes[0].width, 5);
  EXPECT_EQ(c.nodes[1].op.kind, Operation::MuxBool);
  EXPECT_EQ(c.nodes[1].deps, (std::vector<uint32_t>{2000000000, c.nodes[0].id, 2000000002}));
}
}  // namespace lean_export
