// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_llvm.hpp"

#include <bit>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "gtest/gtest.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

TEST(CgenLlvm, WideShiftKnownBits) {
  constexpr unsigned bits = 131064;
  llvm::KnownBits    lhs(bits);
  llvm::KnownBits    rhs(bits);
  lhs.Zero          = llvm::APInt::getHighBitsSet(bits, bits - 64);
  rhs.Zero          = llvm::APInt::getHighBitsSet(bits, bits - 16);
  const auto result = llvm::KnownBits::shl(lhs, rhs, true, true);
  EXPECT_EQ(result.Zero, llvm::APInt::getHighBitsSet(bits, bits - 64 - 65535));
  EXPECT_TRUE(result.One.isZero());

  lhs.One  = llvm::APInt(bits, 1);
  lhs.Zero = ~lhs.One;
  rhs.Zero.setAllBits();
  rhs.Zero.clearBit(0);
  rhs.Zero.clearBit(15);
  llvm::APInt possible(bits, 0);
  possible.setBit(1);
  possible.setBit(32768);
  possible.setBit(32769);
  const auto nonzero = llvm::KnownBits::shl(lhs, rhs, true, true, true);
  EXPECT_EQ(nonzero.Zero, ~possible);
  EXPECT_TRUE(nonzero.One.isZero());
}

TEST(CgenLlvm, WideShiftKnownBitsWithPoisonConstraints) {
  constexpr unsigned bits = 131064;
  llvm::KnownBits    lhs(bits);
  llvm::KnownBits    rhs(bits);
  lhs.Zero.setBit(0);
  rhs.Zero                  = llvm::APInt::getHighBitsSet(bits, bits - 16);
  const auto signed_unknown = llvm::KnownBits::shl(lhs, rhs, false, true);
  EXPECT_EQ(signed_unknown.Zero, llvm::APInt(bits, 1));
  EXPECT_TRUE(signed_unknown.One.isZero());

  lhs.One.setSignBit();
  const auto negative = llvm::KnownBits::shl(lhs, rhs, false, true, true);
  EXPECT_EQ(negative.Zero, llvm::APInt::getLowBitsSet(bits, 2));
  EXPECT_EQ(negative.One, llvm::APInt::getSignMask(bits));

  lhs.One            = llvm::APInt(bits, 1);
  lhs.Zero           = ~lhs.One;
  rhs.Zero           = llvm::APInt::getHighBitsSet(bits, bits - 17);
  const auto bounded = llvm::KnownBits::shl(lhs, rhs, false, false);
  EXPECT_TRUE(bounded.isUnknown());
}

TEST(CgenLlvm, ShiftKnownBitsMatchConcreteValues) {
  struct Case {
    llvm::KnownBits       known;
    std::vector<unsigned> values;
  };
  for (unsigned bits = 1; bits <= 5; ++bits) {
    const unsigned limit    = 1u << bits;
    unsigned       patterns = 1;
    for (unsigned i = 0; i < bits; ++i) {
      patterns *= 3;
    }
    std::vector<Case> cases;
    for (unsigned code = 0; code < patterns; ++code) {
      Case     sample{llvm::KnownBits(bits), {}};
      unsigned digits = code;
      for (unsigned bit = 0; bit < bits; ++bit, digits /= 3) {
        if (digits % 3 == 1) {
          sample.known.Zero.setBit(bit);
        } else if (digits % 3 == 2) {
          sample.known.One.setBit(bit);
        }
      }
      for (unsigned value = 0; value < limit; ++value) {
        if ((value & sample.known.Zero.getZExtValue()) == 0
            && (value & sample.known.One.getZExtValue()) == sample.known.One.getZExtValue()) {
          sample.values.push_back(value);
        }
      }
      cases.push_back(std::move(sample));
    }
    for (const auto& lhs : cases) {
      for (const auto& rhs : cases) {
        for (unsigned flags = 0; flags < 8; ++flags) {
          const bool nuw        = (flags & 1) != 0;
          const bool nsw        = (flags & 2) != 0;
          const bool nonzero    = (flags & 4) != 0;
          unsigned   may_be_one = 0;
          unsigned   always_one = limit - 1;
          bool       defined    = false;
          for (unsigned value : lhs.values) {
            for (unsigned amount : rhs.values) {
              if (amount >= bits || (nonzero && amount == 0)) {
                continue;
              }
              const unsigned shifted = value << amount;
              const int      signed_value
                  = value >= limit / 2 ? static_cast<int>(value) - static_cast<int>(limit) : static_cast<int>(value);
              const int signed_shifted = signed_value * static_cast<int>(1u << amount);
              if ((nuw && shifted >= limit)
                  || (nsw && (signed_shifted < -static_cast<int>(limit / 2) || signed_shifted >= static_cast<int>(limit / 2)))) {
                continue;
              }
              defined     = true;
              may_be_one |= shifted & (limit - 1);
              always_one &= shifted & (limit - 1);
            }
          }
          const auto result = llvm::KnownBits::shl(lhs.known, rhs.known, nuw, nsw, nonzero);
          if (!defined) {
            continue;
          }
          ASSERT_EQ(result.Zero.getZExtValue() & may_be_one, 0u) << bits << " bits, flags=" << flags;
          ASSERT_EQ(result.One.getZExtValue() & ~always_one, 0u) << bits << " bits, flags=" << flags;
          if (!nuw && !nsw && !lhs.known.isUnknown() && rhs.values.back() < bits) {
            ASSERT_EQ(result.Zero.getZExtValue(), (~may_be_one & (limit - 1)));
            ASSERT_EQ(result.One.getZExtValue(), always_one);
          }
        }
      }
    }
  }
}

TEST(CgenLlvm, LinksWideBitTestCounts) {
  llvm::LLVMContext context;
  llvm::Module      host("wide_bit_test_counts", context);
  llvm::IRBuilder<> builder(context);
  auto*             word     = builder.getInt64Ty();
  auto*             function = llvm::Function::Create(llvm::FunctionType::get(word, {word, builder.getPtrTy()}, false),
                                                      llvm::GlobalValue::ExternalLinkage,
                                                      "narrow_bt_count",
                                                      host);
  builder.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
  auto* value = function->getArg(0);
  auto* count = builder.CreateAnd(value, builder.getInt64(3));
  // Keep the masked count shared. X86 can narrow the bit-test source to i32
  // while its count remains i64, including after inlining a color kernel.
  builder.CreateStore(count, function->getArg(1));
  auto* fshr    = llvm::Intrinsic::getOrInsertDeclaration(&host, llvm::Intrinsic::fshr, {word});
  auto* shifted = builder.CreateCall(fshr, {builder.getInt64(0), value, count});
  auto* bit     = builder.CreateTrunc(shifted, builder.getInt1Ty());
  builder.CreateRet(builder.CreateSelect(bit, value, builder.getInt64(0)));

  const auto      base    = std::filesystem::temp_directory_path() / "livehd-cgen-wide-bit-test";
  const auto      bitcode = base.string() + ".bc";
  const auto      object  = base.string() + ".o";
  std::error_code io_error;
  {
    llvm::raw_fd_ostream output(bitcode, io_error);
    ASSERT_FALSE(io_error) << io_error.message();
    llvm::WriteBitcodeToFile(host, output);
  }
  std::string error;
  ASSERT_TRUE(Cgen_llvm::link_bitcode_object(bitcode, {}, object, error)) << error;
  auto native = llvm::MemoryBuffer::getFile(object);
  ASSERT_TRUE(native);
  EXPECT_TRUE((*native)->getBuffer().contains("narrow_bt_count"));
  std::filesystem::remove(bitcode);
  std::filesystem::remove(object);
}

TEST(CgenLlvm, EmitsBitcode) {
  Cgen_llvm   llvm("lhd_llvm_add",
                   {
                       {8, true},
                       {8, true}
  });
  auto        sum = llvm.binary(Cgen_llvm::Binary_op::add, llvm.input(0), llvm.input(1), 9, true);
  std::string error;
  ASSERT_TRUE(llvm.add_output(0, sum, error)) << error;

  const auto path = std::filesystem::temp_directory_path() / "livehd-cgen-llvm-test.bc";
  ASSERT_TRUE(llvm.write_object(path.string(), error)) << error;
  EXPECT_TRUE(std::filesystem::is_regular_file(path));
  EXPECT_GT(std::filesystem::file_size(path), 0u);
  std::filesystem::remove(path);
}

TEST(CgenLlvm, VerifiesArbitraryWidthOperations) {
  Cgen_llvm llvm("lhd_llvm_wide",
                 {
                     {130,  true},
                     { 97, false},
                     {  9,  true}
  });
  auto      wide         = llvm.binary(Cgen_llvm::Binary_op::add, llvm.input(0), llvm.input(1), 131, true);
  auto      shift_amount = llvm.resize(llvm.input(2), 131, true);
  wide                   = llvm.binary(Cgen_llvm::Binary_op::shl, wide, shift_amount, 131, true);
  wide                   = llvm.bitfield_insert(wide, llvm.input(1), 65, 113, 131, true);
  auto signed_wide       = llvm.sign_extend_from(wide, 96, 131, false);
  auto any               = llvm.reduce_or(signed_wide, 1, true);
  auto selected          = llvm.indexed_mux(llvm.input(2), {wide, signed_wide, llvm.input(0)}, 131, true);
  auto table             = llvm.constant(8, 0b10110100, true);
  auto address           = llvm.resize(llvm.input(2), 3, true);
  auto lut               = llvm.lut(table, address, 1, true);

  std::string error;
  ASSERT_TRUE(llvm.add_output(0, selected, error)) << error;
  ASSERT_TRUE(llvm.add_output(1, any, error)) << error;
  ASSERT_TRUE(llvm.add_output(2, lut, error)) << error;

  const auto path = std::filesystem::temp_directory_path() / "livehd-cgen-llvm-wide-test.bc";
  ASSERT_TRUE(llvm.write_object(path.string(), error)) << error;
  EXPECT_TRUE(std::filesystem::is_regular_file(path));
  EXPECT_GT(std::filesystem::file_size(path), 0u);
  std::filesystem::remove(path);
}

TEST(CgenLlvm, WidePackedInputsUseConstantSizeLoads) {
  if constexpr (std::endian::native != std::endian::little) {
    GTEST_SKIP() << "wide native loads use the little-endian packed layout";
  }
  Cgen_llvm   kernel("packed_wide",
                     {
                         {     7, true},
                         {131048, true},
                         {    65, true}
  });
  std::string error;
  ASSERT_TRUE(kernel.add_output(0, kernel.input(1), error)) << error;
  ASSERT_TRUE(kernel.add_output(1, kernel.input(2), error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-cgen-packed-wide.bc";
  ASSERT_TRUE(kernel.write_object(path.string(), error, false)) << error;
  auto buffer = llvm::MemoryBuffer::getFile(path.string());
  ASSERT_TRUE(buffer);
  llvm::LLVMContext context;
  auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  ASSERT_TRUE(module);
  const auto* function = (*module)->getFunction("packed_wide");
  ASSERT_NE(function, nullptr);
  unsigned input_loads = 0;
  for (const auto& block : *function) {
    for (const auto& instruction : block) {
      const auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction);
      if (!load) {
        continue;
      }
      const auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(load->getPointerOperand());
      if (!gep || gep->getPointerOperand() != function->getArg(0)) {
        continue;
      }
      llvm::APInt offset((*module)->getDataLayout().getPointerSizeInBits(), 0);
      ASSERT_TRUE(gep->accumulateConstantOffset((*module)->getDataLayout(), offset));
      ++input_loads;
      if (offset.getZExtValue() == 8) {
        EXPECT_EQ(load->getType()->getIntegerBitWidth(), 131048u);
      } else {
        EXPECT_EQ(offset.getZExtValue(), 2049u * 8);
        EXPECT_EQ(load->getType()->getIntegerBitWidth(), 65u);
      }
      EXPECT_EQ(load->getAlign().value(), alignof(uint64_t));
    }
  }
  EXPECT_EQ(input_loads, 2u);
  std::filesystem::remove(path);
}

TEST(CgenLlvm, DefersInputLoadsAndCastsAndMarksDisjointBuffers) {
  Cgen_llvm  kernel("late_inputs",
                    {
                        {64, true},
                        {64, true},
                        {64, true}
  });
  const auto later = kernel.resize(kernel.input(1), 65, true);
  const auto first = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(0), kernel.constant(64, 1), 64, true);
  ASSERT_TRUE(kernel.external_apply("record_first", first));
  const auto  last = kernel.binary(Cgen_llvm::Binary_op::add, later, first, 65, true);
  std::string error;
  ASSERT_TRUE(kernel.add_output(0, last, error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-cgen-late-inputs.bc";
  ASSERT_TRUE(kernel.write_object(path.string(), error)) << error;
  auto buffer = llvm::MemoryBuffer::getFile(path.string());
  ASSERT_TRUE(buffer);
  llvm::LLVMContext context;
  auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  ASSERT_TRUE(module);
  const auto* function = (*module)->getFunction("late_inputs");
  ASSERT_NE(function, nullptr);
  for (unsigned i = 0; i < 4; ++i) {
    EXPECT_TRUE(function->hasParamAttribute(i, llvm::Attribute::NoAlias));
  }
  EXPECT_TRUE(function->hasParamAttribute(0, llvm::Attribute::ReadOnly));
  bool recorded_first = false;
  bool loaded_later   = false;
  for (const auto& block : *function) {
    for (const auto& instruction : block) {
      if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
        recorded_first |= call->getCalledFunction() && call->getCalledFunction()->getName() == "record_first";
      }
      const auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction);
      if (!load) {
        continue;
      }
      const auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(load->getPointerOperand());
      if (!gep || gep->getPointerOperand() != function->getArg(0)) {
        continue;
      }
      llvm::APInt offset((*module)->getDataLayout().getPointerSizeInBits(), 0);
      if (!gep->accumulateConstantOffset((*module)->getDataLayout(), offset)) {
        continue;
      }
      EXPECT_NE(offset.getZExtValue(), 16u) << "unused input must not load";
      if (offset.getZExtValue() == 8) {
        EXPECT_TRUE(recorded_first) << "the boundary cast must not force an eager input load";
        loaded_later = true;
      }
    }
  }
  EXPECT_TRUE(loaded_later);
  std::filesystem::remove(path);
}
