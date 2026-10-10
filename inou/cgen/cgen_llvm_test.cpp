// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_llvm.hpp"

#include <bit>
#include <filesystem>
#include <fstream>
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
#include "llvm/IR/Verifier.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "sim_compile_workers.hpp"
#include "sim_native_rt.hpp"

TEST(CgenLlvm, ConcatCoalescesRepeatedContiguousSlices) {
  Cgen_llvm                     kernel("concat_slices", Cgen_llvm::State_layout{9, {{0, 64, true}}});
  std::vector<Cgen_llvm::Value> lanes;
  for (unsigned repeat = 0; repeat < 16; ++repeat) {
    for (unsigned bit = 49; bit-- > 17;) {
      lanes.push_back(kernel.binary(Cgen_llvm::Binary_op::lshr, kernel.input(0), kernel.constant(64, bit), 1, true));
    }
  }
  std::string error;
  const auto  result = kernel.concat(lanes);
  ASSERT_EQ(result.width, 512u);
  ASSERT_TRUE(kernel.add_state_output(1, 1, result, {}, error)) << error;
  const auto dir     = std::filesystem::temp_directory_path();
  const auto bitcode = dir / "livehd-concat-slices.bc";
  ASSERT_TRUE(kernel.write_bitcode(bitcode.string(), error, false)) << error;
  auto buffer = llvm::MemoryBuffer::getFile(bitcode.string());
  ASSERT_TRUE(buffer);
  llvm::LLVMContext context;
  auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  ASSERT_TRUE(module);
  size_t shifts = 0;
  for (const auto& function : **module) {
    for (const auto& block : function) {
      for (const auto& instruction : block) {
        shifts += instruction.isShift();
      }
    }
  }
  // Reconstructing a repeated 32-bit bus must not retain hundreds of bit shifts.
  EXPECT_LT(shifts, 32u);
  const auto object = dir / "livehd-concat-slices.o";
  ASSERT_TRUE(kernel.write_object(object.string(), error, false)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(object.string(), error)) << error;
  const auto fn = objects.lookup("concat_slices", error);
  ASSERT_NE(fn, nullptr) << error;
  for (const uint64_t input : {uint64_t{0}, ~uint64_t{0}, uint64_t{0x243f6a8885a308d3}, uint64_t{0xdeadbeef12345678}}) {
    uint64_t state[9] = {input};
    fn(state, nullptr);
    const uint64_t lane = (input >> 17) & 0xffffffff;
    for (size_t word = 1; word < 9; ++word) {
      EXPECT_EQ(state[word], lane | (lane << 32));
    }
  }
  std::filesystem::remove(bitcode);
  std::filesystem::remove(object);
}

TEST(CgenLlvm, ConcatPreservesExtensionsGapsAndLaneOrder) {
  Cgen_llvm   kernel("concat_mixed", Cgen_llvm::State_layout{4, {{0, 8, false}}});
  const auto  input    = kernel.input(0);
  const auto  low      = kernel.resize(input, 4, true);
  const auto  high     = kernel.binary(Cgen_llvm::Binary_op::lshr, input, kernel.constant(8, 4), 4, true);
  const auto  extended = kernel.resize(input, 16, false);
  const auto  sign     = kernel.binary(Cgen_llvm::Binary_op::lshr, extended, kernel.constant(16, 8), 8, true);
  const auto  swapped  = kernel.concat({low, high});
  const auto  mixed    = kernel.concat({sign, kernel.constant(3, 5), high, kernel.constant(1, 0), low});
  std::string error;
  ASSERT_TRUE(kernel.add_state_output(1, 1, swapped, {}, error)) << error;
  ASSERT_TRUE(kernel.add_state_output(2, 2, mixed, {}, error)) << error;
  ASSERT_EQ(kernel.concat({}).width, 0u);
  const auto object = std::filesystem::temp_directory_path() / "livehd-concat-mixed.o";
  ASSERT_TRUE(kernel.write_object(object.string(), error, false)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(object.string(), error)) << error;
  const auto fn = objects.lookup("concat_mixed", error);
  ASSERT_NE(fn, nullptr) << error;
  for (uint64_t input_bits = 0; input_bits < 256; ++input_bits) {
    uint64_t state[] = {input_bits, 0, 0, 0};
    fn(state, nullptr);
    EXPECT_EQ(state[1], ((input_bits & 15) << 4) | (input_bits >> 4));
    const uint64_t upper = (input_bits & 128) ? 255 : 0;
    EXPECT_EQ(state[2], (upper << 12) | (5 << 9) | ((input_bits >> 4) << 5) | (input_bits & 15));
  }
  std::filesystem::remove(object);
}

TEST(CgenLlvm, SharedCodePreservesBindingsAndIndependentState) {
  const auto make = [](std::string_view name, uint64_t increment, size_t input_word) {
    Cgen_llvm   kernel(name, Cgen_llvm::State_layout{3, {{input_word, 64, true}}});
    std::string error;
    const auto  sum = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(0), kernel.constant(64, increment), 64, true);
    EXPECT_TRUE(kernel.add_state_output(2, 2, sum, {}, error)) << error;
    return kernel;
  };
  auto        first               = make("shared_first", 1, 0);
  auto        second              = make("shared_second", 1, 0);
  auto        different_operation = make("shared_first", 2, 0);
  auto        different_binding   = make("shared_first", 1, 1);
  std::string error;
  const auto  key = first.sharing_key(error);
  ASSERT_FALSE(key.empty()) << error;
  EXPECT_EQ(second.sharing_key(error), key);
  EXPECT_NE(different_operation.sharing_key(error), key);
  EXPECT_NE(different_binding.sharing_key(error), key);
  EXPECT_EQ(first.sharing_key(error), key);
  const auto path = std::filesystem::temp_directory_path() / "livehd-shared-code.o";
  ASSERT_TRUE(first.write_object(path.string(), error)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(path.string(), error)) << error;
  const auto fn = objects.lookup("shared_first", error);
  ASSERT_NE(fn, nullptr) << error;
  uint64_t a[] = {10, 0, 0};
  uint64_t b[] = {20, 0, 0};
  fn(a, nullptr);
  EXPECT_EQ(a[2], 11u);
  EXPECT_EQ(b[2], 0u);
  fn(b, nullptr);
  EXPECT_EQ(a[2], 11u);
  EXPECT_EQ(b[2], 21u);
  std::filesystem::remove(path);
}

TEST(CgenLlvm, CachedObjectsValidateInputsAndObjectBytes) {
  const auto dir = std::filesystem::temp_directory_path() / "livehd-native-object-cache";
  std::filesystem::create_directories(dir);
  const auto object = (dir / "color.o").string();
  const auto cache  = (dir / "color.key").string();
  const auto emit   = [&](uint64_t increment, bool reuse, bool expected_hit) {
    Cgen_llvm::State_layout layout{2, {{0, 64, true}}};
    Cgen_llvm               kernel("cached_add", layout);
    std::string             error;
    const auto sum = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(0), kernel.constant(64, increment), 64, true);
    ASSERT_TRUE(kernel.add_state_output(1, 1, sum, {}, error)) << error;
    bool hit = false;
    ASSERT_TRUE(kernel.write_object(object, error, false, reuse ? cache : "", &hit)) << error;
    EXPECT_EQ(hit, expected_hit);
    livehd::sim::Native_objects objects;
    ASSERT_TRUE(objects.load(object, error)) << error;
    auto* address = objects.lookup("cached_add", error);
    ASSERT_NE(address, nullptr) << error;
    uint64_t state[] = {100, 0};
    address(state, nullptr);
    EXPECT_EQ(state[1], 100 + increment);
  };
  std::filesystem::remove(cache);
  emit(1, true, false);
  const auto written = std::filesystem::last_write_time(object);
  emit(1, true, true);
  EXPECT_EQ(std::filesystem::last_write_time(object), written);
  emit(2, true, false);
  emit(2, true, true);
  {
    std::ofstream output(object);
    output << "damaged object";
  }
  emit(2, true, false);
  {
    std::ofstream output(cache);
    output << "truncated key";
  }
  emit(2, true, false);
  emit(2, false, false);
  std::filesystem::remove(object);
  emit(2, true, false);
  std::filesystem::remove_all(dir);
}

TEST(CgenLlvm, DynamicSingleBitExtractionUsesOneSourceWord) {
  Cgen_llvm   kernel("extract_bit",
                   Cgen_llvm::State_layout{
                       7,
                         {{0, 256, true}, {4, 64, true}}
  });
  std::string error;
  const auto  bit = kernel.dynamic_extract(kernel.input(0), kernel.input(1), 1, 64, true);
  ASSERT_TRUE(kernel.add_state_output(5, 5, bit, {}, error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-dynamic-bit.bc";
  ASSERT_TRUE(kernel.write_bitcode(path.string(), error, false)) << error;
  auto buffer = llvm::MemoryBuffer::getFile(path.string());
  ASSERT_TRUE(buffer);
  llvm::LLVMContext context;
  auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  ASSERT_TRUE(module);
  // Exactly one source-word load plus the index load; no adjacent-word load.
  size_t loads = 0;
  for (const auto& block : *(*module)->getFunction("extract_bit")) {
    for (const auto& instruction : block) {
      loads += llvm::isa<llvm::LoadInst>(instruction);
    }
  }
  EXPECT_EQ(loads, 2u);
  const auto object = path.string() + ".o";
  ASSERT_TRUE(kernel.write_object(object, error, false)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(object, error)) << error;
  const auto fn = objects.lookup("extract_bit", error);
  ASSERT_NE(fn, nullptr) << error;
  for (uint64_t index = 0; index < 320; ++index) {
    uint64_t   state[]  = {0x8000000000000001, 0xaaaaaaaaaaaaaaaa, 0x5555555555555555, 0xfedcba9876543210, index, 0, 0};
    const auto expected = index < 256 ? ((state[index / 64] >> (index % 64)) & 1) : 0;
    fn(state, nullptr);
    EXPECT_EQ(state[5], expected) << index;
  }
  std::filesystem::remove(path);
  std::filesystem::remove(object);
}

TEST(CgenLlvm, ComputedExtractionScratchIsLocalToTheLoopFrame) {
  Cgen_llvm   kernel("extract_after_shift",
                     {
                       {4097, true},
                       {  32, true},
                       {  32, true}
  });
  std::string error;
  // The packed shift leaves the insertion point in a non-entry block.
  // Extracting from that computed value must not allocate each iteration.
  const auto  shifted = kernel.binary(Cgen_llvm::Binary_op::lshr, kernel.input(0), kernel.input(1), 130, true);
  const auto  lane    = kernel.dynamic_extract(shifted, kernel.constant(32, 65), 8, 32, true);
  const auto  sum     = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(2), lane, 32, true);
  ASSERT_TRUE(kernel.add_output(0, sum, error)) << error;
  Cgen_llvm::Loop_layout layout;
  layout.inputs = {
      {4097, true},
      {  32, true},
      {  32, true}
  };
  layout.bindings = {
      {0, 0},
      {1, 0},
      {2, 0}
  };
  layout.index   = 1;
  layout.carries = {
      {2, 0}
  };
  ASSERT_TRUE(kernel.add_loop("extract_loop", layout, error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-local-extraction.bc";
  ASSERT_TRUE(kernel.write_bitcode(path.string(), error)) << error;
  auto buffer = llvm::MemoryBuffer::getFile(path.string());
  ASSERT_TRUE(buffer);
  llvm::LLVMContext context;
  auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  ASSERT_TRUE(module);
  ASSERT_FALSE(llvm::verifyModule(**module, &llvm::errs()));
  const auto* loop_ir = (*module)->getFunction("extract_loop");
  ASSERT_NE(loop_ir, nullptr);
  for (const auto& block : *loop_ir) {
    for (const auto& instruction : block) {
      if (const auto* allocation = llvm::dyn_cast<llvm::AllocaInst>(&instruction)) {
        EXPECT_TRUE(allocation->isStaticAlloca());
      }
      if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
        const auto* callee = call->getCalledFunction();
        ASSERT_NE(callee, nullptr);
        EXPECT_TRUE(callee->isIntrinsic());
        EXPECT_NE(callee->getIntrinsicID(), llvm::Intrinsic::stacksave);
        EXPECT_NE(callee->getIntrinsicID(), llvm::Intrinsic::stackrestore);
      }
    }
  }
  const auto object = path.string() + ".o";
  ASSERT_TRUE(kernel.write_object(object, error)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(object, error)) << error;
  using Loop      = void (*)(const uint64_t*, uint64_t*, uint64_t, int64_t, int64_t);
  const auto loop = std::bit_cast<Loop>(objects.lookup("extract_loop", error));
  ASSERT_NE(loop, nullptr) << error;
  std::vector<uint64_t> inputs(67);
  for (size_t word = 0; word < 64; ++word) {
    inputs[word] = 0x123456789abcdef0ULL ^ (word * 0x87654321ULL);
  }
  inputs[64] = 1;
  inputs[66] = 123;
  for (uint64_t trips : {0, 1, 257, 5000}) {
    uint64_t output = 0;
    loop(inputs.data(), &output, trips, 0, 1);
    uint32_t expected = 123;
    for (uint64_t index = 0; index < trips; ++index) {
      for (unsigned bit = 0; bit < 8; ++bit) {
        const auto position = index + 65 + bit;
        if (position < 4097) {
          expected += ((inputs[position / 64] >> (position % 64)) & 1) << bit;
        }
      }
    }
    EXPECT_EQ(output, expected) << trips;
  }
  std::filesystem::remove(path);
  std::filesystem::remove(object);
}

TEST(CgenLlvm, WideVariableShiftsCompileAsPackedLoops) {
  for (const uint32_t width : {4096u, 4097u, 131064u}) {
    Cgen_llvm   kernel("variable_shift",
                       {
                         {width, false},
                         {   17,  true}
    });
    std::string error;
    size_t      output = 0;
    for (const auto op : {Cgen_llvm::Binary_op::shl, Cgen_llvm::Binary_op::lshr, Cgen_llvm::Binary_op::ashr}) {
      const auto result_width = op == Cgen_llvm::Binary_op::shl ? width : 64u;
      const auto shifted      = kernel.binary(op, kernel.input(0), kernel.input(1), result_width, false);
      EXPECT_EQ(shifted.width, result_width);
      ASSERT_TRUE(kernel.add_output(output++, shifted, error)) << error;
      const auto constant = kernel.binary(op, kernel.input(0), kernel.constant(17, 4, true), result_width, false);
      EXPECT_EQ(constant.width, result_width);
    }
    EXPECT_EQ(kernel.dynamic_extract(kernel.input(0), kernel.input(1), 64, 64, true).width, 64u);
    if (width == 131064) {
      const auto path = std::filesystem::temp_directory_path() / "livehd-cgen-wide-variable-shift.bc";
      ASSERT_TRUE(kernel.write_bitcode(path.string(), error, false)) << error;
      const auto native = path.string() + ".o";
      ASSERT_TRUE(Cgen_llvm::link_bitcode_object(path.string(), {}, native, error)) << error;
      std::filesystem::remove(native);
      std::filesystem::remove(path);
    }
  }
}

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
  ASSERT_TRUE(llvm.write_bitcode(path.string(), error)) << error;
  EXPECT_TRUE(std::filesystem::is_regular_file(path));
  EXPECT_GT(std::filesystem::file_size(path), 0u);
  auto buffer = llvm::MemoryBuffer::getFile(path.string());
  ASSERT_TRUE(buffer);
  llvm::LLVMContext context;
  auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  ASSERT_TRUE(module);
  const auto* function = (*module)->getFunction("lhd_llvm_add");
  ASSERT_NE(function, nullptr);
  EXPECT_TRUE(function->hasFnAttribute(llvm::Attribute::OptimizeForSize));
  EXPECT_FALSE(function->hasFnAttribute(llvm::Attribute::MinSize));  // Os, not Oz.
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
  ASSERT_TRUE(llvm.write_bitcode(path.string(), error)) << error;
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
  ASSERT_TRUE(kernel.write_bitcode(path.string(), error, false)) << error;
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
        // A width that is not a whole number of bytes loads its whole words and
        // truncates: `load i65` over sign-extended packed words is undefined.
        EXPECT_EQ(load->getType()->getIntegerBitWidth(), 128u);
      }
      EXPECT_EQ(load->getAlign().value(), alignof(uint64_t));
    }
  }
  EXPECT_EQ(input_loads, 2u);
  std::filesystem::remove(path);
}

TEST(CgenLlvm, DefersInputLoadsAndCastsAndMarksDisjointBuffers) {
  Cgen_llvm         kernel("late_inputs",
                           {
                       {64, true},
                       {64, true},
                       {64, true}
  });
  const auto        later = kernel.resize(kernel.input(1), 65, true);
  const auto        first = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(0), kernel.constant(64, 1), 64, true);
  Cgen_llvm::Memory memory;
  memory.packed_value = true;
  kernel.bind_memory("record_first", memory);
  ASSERT_TRUE(kernel.memory_apply("record_first", first));
  const auto  last = kernel.binary(Cgen_llvm::Binary_op::add, later, first, 65, true);
  std::string error;
  ASSERT_TRUE(kernel.add_output(0, last, error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-cgen-late-inputs.bc";
  ASSERT_TRUE(kernel.write_bitcode(path.string(), error)) << error;
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
      recorded_first   |= llvm::isa<llvm::StoreInst>(instruction);
      const auto* load  = llvm::dyn_cast<llvm::LoadInst>(&instruction);
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

TEST(CgenLlvm, EmitsIndependentNativeObjects) {
  Cgen_llvm         kernel("native_memory",
                           {
                       {32, true},
                       { 8, true},
                       { 1, true}
  });
  Cgen_llvm::Memory memory;
  memory.data    = 0;
  memory.pending = 1;
  memory.bits    = 32;
  memory.size    = 16;
  memory.writes  = 1;
  memory.forward = 1;
  kernel.bind_memory("memory", memory);
  ASSERT_TRUE(kernel.memory_stage_write("memory", kernel.input(2), kernel.input(1), kernel.input(0)));
  const auto  value = kernel.memory_read("memory", kernel.input(1), 32, true);
  std::string error;
  ASSERT_TRUE(kernel.add_output(0, value, error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-cgen-native-memory.o";
  ASSERT_TRUE(kernel.write_object(path.string(), error)) << error;
  EXPECT_GT(std::filesystem::file_size(path), 0u);
  std::filesystem::remove(path);
}

TEST(CgenLlvm, NativeWideCopiesAndDivisionHaveNoRuntimeDependencies) {
  for (const uint32_t width : {65u, 128u, 256u, 4097u, 131064u}) {
    Cgen_llvm   kernel("native_wide",
                       {
                         {width, true},
                         {width, true}
    });
    std::string error;
    auto        result
        = width <= 256 ? kernel.binary(Cgen_llvm::Binary_op::div, kernel.input(0), kernel.input(1), width, true) : kernel.input(0);
    ASSERT_TRUE(kernel.add_output(0, result, error)) << error;
    const auto path = std::filesystem::temp_directory_path() / "livehd-cgen-native-wide.o";
    ASSERT_TRUE(kernel.write_object(path.string(), error, false)) << width << ": " << error;
    std::filesystem::remove(path);
  }
}

TEST(CgenLlvm, StateObjectsRunWithoutHostCompilationAndPreserveAliasing) {
  const auto  path = std::filesystem::temp_directory_path() / "livehd-native-state-swap.o";
  Cgen_llvm   kernel("state_swap",
                   Cgen_llvm::State_layout{
                       6,
                         {{0, 64, true}, {1, 64, true}, {2, 9, true, 60}}
  });
  std::string error;
  ASSERT_TRUE(kernel.add_state_output(0,
                                      0,
                                      kernel.input(1),
                                      {
                                          {4, 1}
  },
                                      error))
      << error;
  ASSERT_TRUE(kernel.add_state_output(1,
                                      1,
                                      kernel.input(0),
                                      {
                                          {4, 2}
  },
                                      error))
      << error;
  ASSERT_TRUE(kernel.add_state_output(5,
                                      5,
                                      kernel.input(2),
                                      {
                                          {4, 4}
  },
                                      error))
      << error;
  ASSERT_TRUE(kernel.write_object(path.string(), error)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(path.string(), error)) << error;
  const auto fn = objects.lookup("state_swap", error);
  ASSERT_NE(fn, nullptr) << error;
  uint64_t state[] = {11, 29, uint64_t{13} << 60, 17, 8, 0};
  fn(state, nullptr);
  EXPECT_EQ(state[0], 29u);
  EXPECT_EQ(state[1], 11u);
  EXPECT_EQ(state[4], 15u);  // preserved old flags and both snapshot comparisons
  EXPECT_EQ(state[5], 285u);
  state[0] = state[1];
  state[4] = 8;
  fn(state, nullptr);
  EXPECT_EQ(state[4], 8u);  // unchanged outputs must not wake downstream work
  std::filesystem::remove(path);
}

TEST(CgenLlvm, NativeStateObjectsAndCommitsUseParallelWorkers) {
  livehd::sim::Compile_workers          workers(2);
  std::vector<std::future<std::string>> jobs;
  std::vector<std::filesystem::path>    paths;
  for (unsigned i = 0; i < 8; ++i) {
    const auto name = "native_state_" + std::to_string(i);
    paths.push_back(std::filesystem::temp_directory_path() / (name + ".o"));
    jobs.push_back(workers.submit([name, path = paths.back()] {
      Cgen_llvm   kernel(name, Cgen_llvm::State_layout{3, {{0, 64, true}}});
      auto        next = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(0), kernel.constant(64, 1), 64, true);
      std::string error;
      if (!kernel.add_state_output(1,
                                   0,
                                   next,
                                   {
                                       {2, 1}
      },
                                   error)
          || !kernel.write_object(path.string(), error)) {
        return error;
      }
      return std::string{};
    }));
  }
  Cgen_llvm   commit("native_commit", Cgen_llvm::State_layout{3, {}});
  std::string error;
  ASSERT_TRUE(commit.copy_state(0, 1, 1, error)) << error;
  const auto commit_path = std::filesystem::temp_directory_path() / "livehd-native-commit.o";
  ASSERT_TRUE(commit.write_object(commit_path.string(), error)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(commit_path.string(), error)) << error;
  auto commit_fn = objects.lookup("native_commit", error);
  ASSERT_NE(commit_fn, nullptr) << error;
  for (unsigned i = 0; i < jobs.size(); ++i) {
    ASSERT_TRUE(jobs[i].get().empty());
    ASSERT_TRUE(objects.load(paths[i].string(), error)) << error;
    auto eval_fn = objects.lookup("native_state_" + std::to_string(i), error);
    ASSERT_NE(eval_fn, nullptr) << error;
    livehd::sim::Native_instance instance(3);
    ASSERT_TRUE(instance.schedule(
        {
            {  eval_fn, 0, 0},
            {commit_fn, 2, 1}
    },
        error))
        << error;
    for (unsigned step = 0; step < 100; ++step) {
      instance.step();
      EXPECT_EQ(instance.state()[0], step + 1);
      EXPECT_EQ(instance.state()[2], 0u);
    }
    std::filesystem::remove(paths[i]);
  }
  std::filesystem::remove(commit_path);
}

TEST(CgenLlvm, NativeWideStateCommitHasNoExternalMemcpy) {
  constexpr size_t words = 2048;
  Cgen_llvm        kernel("state_commit_wide", Cgen_llvm::State_layout{2 * words, {}});
  std::string      error;
  ASSERT_TRUE(kernel.copy_state(0, words, words, error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-native-wide-commit.o";
  ASSERT_TRUE(kernel.write_object(path.string(), error)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(path.string(), error)) << error;
  auto fn = objects.lookup("state_commit_wide", error);
  ASSERT_NE(fn, nullptr) << error;
  std::vector<uint64_t> state(2 * words);
  for (size_t i = 0; i < words; ++i) {
    state[words + i] = i * 13 + 1;
  }
  fn(state.data(), nullptr);
  for (size_t i = 0; i < words; ++i) {
    EXPECT_EQ(state[i], state[words + i]);
  }
  std::filesystem::remove(path);
}

TEST(CgenLlvm, NativeStateRejectsInvalidSpans) {
  std::string error;
  Cgen_llvm   kernel("bad_state", Cgen_llvm::State_layout{4, {{0, 64, true}}});
  EXPECT_FALSE(kernel.add_state_output(4, 0, kernel.input(0), {}, error));
  EXPECT_FALSE(kernel.add_state_output(0, 4, kernel.input(0), {}, error));
  EXPECT_FALSE(kernel.add_state_output(0,
                                       0,
                                       kernel.input(0),
                                       {
                                           {4, 1}
  },
                                       error));
  EXPECT_FALSE(kernel.copy_state(0, 1, 2, error));
  EXPECT_FALSE(kernel.copy_state(0, 3, 2, error));
}

TEST(CgenLlvm, NativeMemoryCommitsAtThePhaseBarrierWithoutCppWrappers) {
  Cgen_llvm::Memory memory;
  memory.data    = 0;
  memory.pending = 1;
  memory.bits    = 13;
  memory.size    = 8;
  memory.writes  = 1;
  Cgen_llvm evaluate("native_memory_evaluate",
                     Cgen_llvm::State_layout{
                         3,
                         {{0, 1, true}, {1, 16, true}, {2, 13, true}}
  });
  evaluate.bind_memory("mem", memory);
  ASSERT_TRUE(evaluate.memory_stage_write("mem", evaluate.input(0), evaluate.input(1), evaluate.input(2)));
  Cgen_llvm commit("native_memory_commit", Cgen_llvm::State_layout{3, {}});
  commit.bind_memory("mem", memory);
  ASSERT_TRUE(commit.memory_commit("mem"));
  std::string error;
  const auto  eval_path   = std::filesystem::temp_directory_path() / "livehd-native-memory-evaluate.o";
  const auto  commit_path = std::filesystem::temp_directory_path() / "livehd-native-memory-commit.o";
  ASSERT_TRUE(evaluate.write_object(eval_path.string(), error)) << error;
  ASSERT_TRUE(commit.write_object(commit_path.string(), error)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(eval_path.string(), error)) << error;
  ASSERT_TRUE(objects.load(commit_path.string(), error)) << error;
  auto eval_fn = objects.lookup("native_memory_evaluate", error);
  ASSERT_NE(eval_fn, nullptr) << error;
  auto commit_fn = objects.lookup("native_memory_commit", error);
  ASSERT_NE(commit_fn, nullptr) << error;
  uint64_t data[8]     = {};
  uint64_t pending[5]  = {};
  void*    resources[] = {data, pending};
  uint64_t state[]     = {1, 3, 55};
  eval_fn(state, resources);
  EXPECT_EQ(data[3], 0u);  // capture must not publish before the commit barrier
  commit_fn(state, resources);
  EXPECT_EQ(data[3], 55u);
  EXPECT_EQ(pending[4], 0u);
  state[0] = 0;
  state[2] = 99;
  eval_fn(state, resources);
  commit_fn(state, resources);
  EXPECT_EQ(data[3], 55u);
  // An out-of-array address never creates a pending write.
  state[0] = 1;
  state[1] = 8;
  eval_fn(state, resources);
  EXPECT_EQ(pending[4], 0u);
  commit_fn(state, resources);
  std::filesystem::remove(eval_path);
  std::filesystem::remove(commit_path);
}

TEST(CgenLlvm, ObjectOwnedStateIsPrivateAndIndependentAcrossInstances) {
  livehd::sim::Compile_workers          workers(2);
  std::vector<std::future<std::string>> jobs;
  std::vector<std::filesystem::path>    paths;
  for (unsigned i = 0; i < 2; ++i) {
    paths.push_back(std::filesystem::temp_directory_path() / ("livehd-native-private-" + std::to_string(i) + ".o"));
    jobs.push_back(workers.submit([i, path = paths.back()] {
      // Deliberately identical local names in different objects. The public
      // port is word 0; the counter at word 1 is private to this color instance.
      Cgen_llvm   kernel("private_counter", Cgen_llvm::State_layout{2, {{1, 64, true}}});
      const auto  next = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(0), kernel.constant(64, 1), 64, true);
      std::string error;
      if (!kernel.add_state_output(0,
                                   0,
                                   next,
                                   {
      },
                                   error)
          || !kernel.add_state_output(1, 1, next, {}, error)
          || !kernel.write_state_object(path.string(), "counter_image_" + std::to_string(i), 1, {{1, 40 + i}}, error)) {
        return error;
      }
      return std::string{};
    }));
  }
  std::unique_ptr<livehd::sim::Native_instance> survivor;
  {
    livehd::sim::Native_objects objects;
    for (unsigned i = 0; i < paths.size(); ++i) {
      ASSERT_TRUE(jobs[i].get().empty());
      std::string error;
      auto        object = llvm::object::ObjectFile::createObjectFile(paths[i].string());
      ASSERT_TRUE(object) << llvm::toString(object.takeError());
      size_t exports = 0;
      for (const auto& symbol : object->getBinary()->symbols()) {
        auto flags = symbol.getFlags();
        ASSERT_TRUE(flags) << llvm::toString(flags.takeError());
        if ((*flags & llvm::object::BasicSymbolRef::SF_Global) != 0) {
          auto name = symbol.getName();
          ASSERT_TRUE(name) << llvm::toString(name.takeError());
          EXPECT_EQ(name->str(), "counter_image_" + std::to_string(i));
          EXPECT_EQ(*flags & llvm::object::BasicSymbolRef::SF_Undefined, 0u);
          ++exports;
        }
      }
      EXPECT_EQ(exports, 1u);
      ASSERT_TRUE(objects.load(paths[i].string(), error)) << error;
      auto first  = objects.instantiate("counter_image_" + std::to_string(i), error);
      auto second = objects.instantiate("counter_image_" + std::to_string(i), error);
      ASSERT_NE(first, nullptr) << error;
      ASSERT_NE(second, nullptr) << error;
      ASSERT_EQ(first->state().size(), 1u);
      ASSERT_EQ(second->state().size(), 1u);
      EXPECT_EQ(first->state()[0], 0u);
      for (unsigned cycle = 0; cycle < 100; ++cycle) {
        first->step();
        EXPECT_EQ(first->state()[0], 41 + i + cycle);
      }
      EXPECT_EQ(second->state()[0], 0u);
      second->step();
      EXPECT_EQ(second->state()[0], 41 + i);
      first->reset();
      EXPECT_EQ(first->state()[0], 0u);
      first->step();
      EXPECT_EQ(first->state()[0], 41 + i);
      survivor = std::move(second);
      std::filesystem::remove(paths[i]);
    }
  }
  // Code and constants must outlive their loader while an instance uses them.
  survivor->step();
  EXPECT_EQ(survivor->state()[0], 43u);
}

TEST(CgenLlvm, NativeStateDescriptorRejectsInvalidInitialization) {
  const auto  path = std::filesystem::temp_directory_path() / "livehd-native-bad-state.o";
  Cgen_llvm   kernel("private_counter", Cgen_llvm::State_layout{2, {}});
  std::string error;
  EXPECT_FALSE(kernel.write_state_object(path.string(), "", 1, {}, error));
  EXPECT_FALSE(kernel.write_state_object(path.string(), "private_counter", 1, {}, error));
  EXPECT_FALSE(kernel.write_state_object(path.string(), "image", 3, {}, error));
  EXPECT_FALSE(kernel.write_state_object(path.string(),
                                         "image",
                                         1,
                                         {
                                             {2, 42}
  },
                                         error));
  EXPECT_FALSE(kernel.write_state_object(path.string(),
                                         "image",
                                         1,
                                         {
                                             {1, 42},
                                             {1, 43}
  },
                                         error));
  // Zero-only defaults need no table. An object with no public words is valid.
  ASSERT_TRUE(kernel.write_state_object(path.string(),
                                        "image",
                                        0,
                                        {
                                            {1, 0}
  },
                                        error))
      << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(path.string(), error)) << error;
  auto instance = objects.instantiate("image", error);
  ASSERT_NE(instance, nullptr) << error;
  EXPECT_TRUE(instance->state().empty());
  instance->step();
  std::filesystem::remove(path);
}

TEST(CgenLlvm, ScalarResultsPackMultipleWidthsIntoOneRegister) {
  Cgen_llvm   kernel("packed_results",
                     {
                       { 8, false},
                       {56,  true}
  },
                   true);
  std::string error;
  auto        negative = kernel.binary(Cgen_llvm::Binary_op::lt, kernel.input(0), kernel.constant(8, 0, false), 1, true);
  ASSERT_TRUE(kernel.add_output(0, negative, error)) << error;
  ASSERT_TRUE(kernel.add_output(1, kernel.resize(kernel.input(0), 7, false), error)) << error;
  ASSERT_TRUE(kernel.add_output(2, kernel.input(1), error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-packed-results.o";
  ASSERT_TRUE(kernel.write_object(path.string(), error)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(path.string(), error)) << error;
  using Kernel_function = uint64_t (*)(uint64_t, uint64_t, void*);
  const auto run        = std::bit_cast<Kernel_function>(objects.lookup("packed_results", error));
  ASSERT_NE(run, nullptr) << error;
  for (uint64_t a : {0, 1, 63, 127, 128, 255}) {
    for (uint64_t b : {uint64_t{0}, uint64_t{0x00abcdef87654321}, uint64_t{0xffffffffffffffff}}) {
      EXPECT_EQ(run(a, b, nullptr), ((a >> 7) & 1) | ((a & 127) << 1) | ((b & 0x00ffffffffffffffULL) << 8));
    }
  }
  std::filesystem::remove(path);
}

TEST(CgenLlvm, ScalarResultsRejectMoreThanOneRegister) {
  Cgen_llvm   kernel("too_wide",
                     {
                       {64, true}
  },
                   true);
  std::string error;
  ASSERT_TRUE(kernel.add_output(0, kernel.input(0), error)) << error;
  ASSERT_TRUE(kernel.add_output(1, kernel.constant(1, 1), error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-too-wide-scalar.o";
  EXPECT_FALSE(kernel.write_object(path.string(), error));
  EXPECT_NE(error.find("exceed 64 packed bits"), std::string::npos);
  std::filesystem::remove(path);
}

TEST(CgenLlvm, NativeScalarLoopUnpacksAllResults) {
  Cgen_llvm   kernel("packed_loop_body",
                     {
                       {8, true},
                       {8, true}
  },
                   true);
  std::string error;
  auto        sum = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(0), kernel.input(1), 8, true);
  ASSERT_TRUE(kernel.add_output(0, sum, error)) << error;
  ASSERT_TRUE(kernel.add_output(1, kernel.input(0), error)) << error;
  ASSERT_TRUE(kernel.add_output(2, kernel.resize(kernel.input(0), 1, true), error)) << error;
  Cgen_llvm::Loop_layout layout;
  layout.inputs = {
      {8, true},
      {8, true}
  };
  layout.bindings = {
      {0, 0},
      {1, 0}
  };
  layout.index   = 0;
  layout.carries = {
      {1, 0}
  };
  ASSERT_TRUE(kernel.add_loop("packed_loop", layout, error)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-packed-loop.bc";
  ASSERT_TRUE(kernel.write_bitcode(path.string(), error)) << error;
  auto buffer = llvm::MemoryBuffer::getFile(path.string());
  ASSERT_TRUE(buffer);
  llvm::LLVMContext context;
  auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  ASSERT_TRUE(module);
  ASSERT_FALSE(llvm::verifyModule(**module, &llvm::errs()));
  const auto* fn = (*module)->getFunction("packed_loop");
  ASSERT_NE(fn, nullptr);
  bool rolled = false;
  for (const auto& block : *fn) {
    for (const auto& instruction : block) {
      EXPECT_FALSE(llvm::isa<llvm::CallBase>(instruction));
      rolled |= instruction.getMetadata(llvm::LLVMContext::MD_loop) != nullptr;
    }
  }
  EXPECT_TRUE(rolled);
  const auto object = path.string() + ".o";
  ASSERT_TRUE(kernel.write_object(object, error)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(object, error)) << error;
  using Loop      = void (*)(const uint64_t*, uint64_t*, uint64_t, int64_t, int64_t);
  const auto loop = std::bit_cast<Loop>(objects.lookup("packed_loop", error));
  ASSERT_NE(loop, nullptr) << error;
  uint64_t inputs[] = {99, 200};
  for (uint64_t count : {0, 1, 8, 300}) {
    for (int64_t step : {-1, 1}) {
      uint64_t output[] = {12345, 12345, 12345};
      loop(inputs, output, count, 252, step);
      uint64_t expected = 200, last = 0;
      for (uint64_t i = 0; i < count; ++i) {
        last     = (252 + i * static_cast<uint64_t>(step)) & 255;
        expected = (expected + last) & 255;
      }
      EXPECT_EQ(output[0], expected);
      EXPECT_EQ(output[1], last);
      EXPECT_EQ(output[2], last & 1);
    }
  }
  std::filesystem::remove(path);
  std::filesystem::remove(object);
}

TEST(CgenLlvm, MultipleColorBodiesInlineIntoOneRolledLoop) {
  // Two independently emitted colors, one scalar and one packed, chained
  // through a temporary. This used to require a scheduler visit per trip.
  std::string                                  error;
  const std::vector<std::pair<uint32_t, bool>> ports{
      {64, true},
      {64, true}
  };
  Cgen_llvm first("first", ports, true);
  ASSERT_TRUE(first.add_output(0, first.binary(Cgen_llvm::Binary_op::add, first.input(0), first.input(1), 64, true), error));
  Cgen_llvm::Inline_body scalar{first.sharing_key(error), ports, {{64, true}}, true};
  Cgen_llvm              second("second", ports);
  const auto             sum = second.binary(Cgen_llvm::Binary_op::add, second.input(0), second.input(1), 64, true);
  ASSERT_TRUE(second.add_output(0, second.mux(second.input(1), second.input(0), sum, 64, true), error));
  Cgen_llvm::Inline_body        packed{second.sharing_key(error), ports, {{64, true}}, false};
  Cgen_llvm                     body("composed", ports);
  std::vector<Cgen_llvm::Value> a, b;
  ASSERT_TRUE(body.inline_body(scalar, {body.input(0), body.input(1)}, a, error)) << error;
  ASSERT_TRUE(body.inline_body(packed, {a[0], body.input(0)}, b, error)) << error;
  // Repeated import must not resolve to or retain another occurrence's body.
  ASSERT_TRUE(body.inline_body(scalar, {b[0], body.constant(64, 1)}, a, error)) << error;
  ASSERT_TRUE(body.add_output(0, a[0], error)) << error;
  Cgen_llvm::Loop_layout layout;
  layout.inputs   = ports;
  layout.bindings = {
      {0, 0},
      {1, 0}
  };
  layout.index   = 0;
  layout.carries = {
      {1, 0}
  };
  ASSERT_TRUE(body.add_loop("composed_loop", layout, error, false)) << error;
  const auto path = std::filesystem::temp_directory_path() / "livehd-composed-loop.bc";
  ASSERT_TRUE(body.write_bitcode(path.string(), error, false)) << error;
  auto buffer = llvm::MemoryBuffer::getFile(path.string());
  ASSERT_TRUE(buffer);
  llvm::LLVMContext context;
  auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  ASSERT_TRUE(module);
  auto* fn = (*module)->getFunction("composed_loop");
  ASSERT_NE(fn, nullptr);
  unsigned rolled = 0;
  for (const auto& block : *fn) {
    for (const auto& instruction : block) {
      EXPECT_FALSE(llvm::isa<llvm::CallBase>(instruction));
      EXPECT_FALSE(llvm::isa<llvm::AllocaInst>(instruction));
      if (const auto* md = instruction.getMetadata(llvm::LLVMContext::MD_loop)) {
        for (unsigned i = 1; i < md->getNumOperands(); ++i) {
          const auto* property = llvm::dyn_cast<llvm::MDNode>(md->getOperand(i));
          if (property && property->getNumOperands() == 1) {
            const auto* name  = llvm::dyn_cast<llvm::MDString>(property->getOperand(0));
            rolled           += name && name->getString() == "llvm.loop.unroll.disable";
          }
        }
      }
    }
  }
  EXPECT_EQ(rolled, 1u);
  const auto object = path.string() + ".o";
  ASSERT_TRUE(body.write_object(object, error, false)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(object, error)) << error;
  using Loop      = void (*)(const uint64_t*, uint64_t*, uint64_t, int64_t, int64_t);
  const auto loop = std::bit_cast<Loop>(objects.lookup("composed_loop", error));
  ASSERT_NE(loop, nullptr) << error;
  for (uint64_t count : {0, 1, 4, 257, 5000}) {
    uint64_t inputs[] = {99, 7}, outputs[] = {0};
    loop(inputs, outputs, count, 0, 1);
    EXPECT_EQ(outputs[0], 7 + count * count);
  }
  std::filesystem::remove(path);
  std::filesystem::remove(object);
}

TEST(CgenLlvm, NativeReductionStaysRolledWithNoBodyCall) {
  for (unsigned variant = 0; variant < 4; ++variant) {
    const bool  scalar  = (variant & 1) != 0;
    const bool  bounded = (variant & 2) != 0;
    Cgen_llvm   kernel("body",
                       {
                         { 8, false},
                         {64,  true}
    },
                     scalar);
    std::string error;
    const auto  sum = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(0), kernel.input(1), 64, true);
    ASSERT_TRUE(kernel.add_output(0, sum, error)) << error;
    Cgen_llvm::Loop_layout layout;
    layout.inputs = {
        {64,  true}, // declared input unused by the body
        { 8, false},
        {64,  true}
    };
    layout.bindings = {
        {1, 0},
        {2, 0}
    };
    layout.index   = 1;
    layout.carries = {
        {2, 0}
    };
    layout.index_bits = bounded ? 4 : 0;
    ASSERT_TRUE(kernel.add_loop("reduction", layout, error)) << error;
    const auto path = std::filesystem::temp_directory_path() / "livehd-native-reduction.bc";
    ASSERT_TRUE(kernel.write_bitcode(path.string(), error)) << error;
    auto buffer = llvm::MemoryBuffer::getFile(path.string());
    ASSERT_TRUE(buffer);
    llvm::LLVMContext context;
    auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
    ASSERT_TRUE(module);
    const auto* fn = (*module)->getFunction("reduction");
    ASSERT_NE(fn, nullptr);
    unsigned rolled = 0;
    for (const auto& block : *fn) {
      for (const auto& inst : block) {
        EXPECT_FALSE(llvm::isa<llvm::CallBase>(inst));
        EXPECT_FALSE(llvm::isa<llvm::AllocaInst>(inst));
        if (const auto* md = inst.getMetadata(llvm::LLVMContext::MD_loop)) {
          for (unsigned i = 1; i < md->getNumOperands(); ++i) {
            const auto* property = llvm::dyn_cast<llvm::MDNode>(md->getOperand(i));
            if (!property || property->getNumOperands() != 1) {
              continue;
            }
            const auto* name = llvm::dyn_cast<llvm::MDString>(property->getOperand(0));
            if (name && name->getString() == "llvm.loop.unroll.disable") {
              ++rolled;
            }
          }
        }
      }
    }
    EXPECT_EQ(rolled, 1u);
    const auto object = path.string() + ".o";
    ASSERT_TRUE(kernel.write_object(object, error)) << error;
    livehd::sim::Native_objects objects;
    ASSERT_TRUE(objects.load(object, error)) << error;
    using Loop      = void (*)(const uint64_t*, uint64_t*, uint64_t, int64_t, int64_t);
    const auto loop = std::bit_cast<Loop>(objects.lookup("reduction", error));
    ASSERT_NE(loop, nullptr) << error;
    const uint64_t inputs[]  = {0x12345678, 99, 100};
    uint64_t       outputs[] = {0};
    loop(inputs, outputs, 0, 5, -1);
    EXPECT_EQ(outputs[0], 100u);
    loop(inputs, outputs, 4, 1, 1);
    EXPECT_EQ(outputs[0], 110u);
    loop(inputs, outputs, 16, 15, -1);
    EXPECT_EQ(outputs[0], 220u);
    if (!bounded) {
      loop(inputs, outputs, 5, 2, -1);
      EXPECT_EQ(outputs[0], 100u);
      loop(inputs, outputs, 100000, 0, 1);
      int64_t expected = 100;
      for (uint64_t i = 0; i < 100000; ++i) {
        expected += static_cast<int8_t>(i);
      }
      EXPECT_EQ(outputs[0], static_cast<uint64_t>(expected));
    }
    std::filesystem::remove(path);
    std::filesystem::remove(object);
  }
}

TEST(CgenLlvm, AbsorbingBooleanCarrySkipsReadsButRetainsOtherOutputs) {
  for (bool conjunction : {false, true}) {
    Cgen_llvm   kernel("carry_body",
                       {
                         { 64, false},
                         {256,  true},
                         {  1,  true}
    });
    std::string error;
    auto        index      = kernel.input(0);
    auto        next_index = kernel.binary(Cgen_llvm::Binary_op::add, index, kernel.constant(64, 17), 64, true);
    auto        a          = kernel.dynamic_extract(kernel.input(1), index, 1, 1, true);
    auto        b          = kernel.dynamic_extract(kernel.input(1), next_index, 1, 1, true);
    auto        expensive  = kernel.binary(Cgen_llvm::Binary_op::bit_xor, a, b, 1, true);
    auto        value      = kernel.binary(conjunction ? Cgen_llvm::Binary_op::bit_and : Cgen_llvm::Binary_op::bit_or,
                               kernel.input(2),
                               expensive,
                               1,
                               true);
    ASSERT_TRUE(kernel.add_output(0, value, error)) << error;
    ASSERT_TRUE(kernel.add_output(1, index, error)) << error;
    Cgen_llvm::Loop_layout layout;
    layout.inputs = {
        { 64, false},
        {256,  true},
        {  1,  true}
    };
    layout.bindings = {
        {0, 0},
        {1, 0},
        {2, 0}
    };
    layout.index   = 0;
    layout.carries = {
        {2, 0}
    };
    ASSERT_TRUE(kernel.add_loop("carry_loop", layout, error)) << error;
    const auto path = std::filesystem::temp_directory_path() / (conjunction ? "livehd-and-carry.bc" : "livehd-or-carry.bc");
    ASSERT_TRUE(kernel.write_bitcode(path.string(), error)) << error;
    auto buffer = llvm::MemoryBuffer::getFile(path.string());
    ASSERT_TRUE(buffer);
    llvm::LLVMContext context;
    auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
    ASSERT_TRUE(module);
    ASSERT_FALSE(llvm::verifyModule(**module, &llvm::errs()));
    const auto* fn = (*module)->getFunction("carry_loop");
    ASSERT_NE(fn, nullptr);
    bool guarded_read = false;
    for (const auto& block : *fn) {
      if (block.getName().starts_with("carry.eval")) {
        for (const auto& instruction : block) {
          guarded_read |= llvm::isa<llvm::LoadInst>(instruction);
        }
      }
    }
    EXPECT_TRUE(guarded_read);
    const auto object = path.string() + ".o";
    ASSERT_TRUE(kernel.write_object(object, error)) << error;
    livehd::sim::Native_objects objects;
    ASSERT_TRUE(objects.load(object, error)) << error;
    using Loop      = void (*)(const uint64_t*, uint64_t*, uint64_t, int64_t, int64_t);
    const auto loop = std::bit_cast<Loop>(objects.lookup("carry_loop", error));
    ASSERT_NE(loop, nullptr) << error;
    uint64_t inputs[] = {0, 0x123456789abcdef0ULL, 0xfedcba9876543210ULL, 0xaaaaaaaa55555555ULL, 0xf0f00f0f96966969ULL, 0};
    for (uint64_t initial : {0, 1}) {
      inputs[5] = initial;
      for (int64_t first : {-20, 0, 63, 255, 300}) {
        for (int64_t step : {-1, 1}) {
          for (uint64_t count : {0, 1, 40, 320}) {
            uint64_t outputs[] = {42, 42};
            loop(inputs, outputs, count, first, step);
            bool       expected = initial != 0;
            uint64_t   last     = 0;
            const auto bit
                = [&](uint64_t position) { return position < 256 && ((inputs[1 + position / 64] >> (position % 64)) & 1) != 0; };
            for (uint64_t i = 0; i < count; ++i) {
              last               = static_cast<uint64_t>(first) + i * static_cast<uint64_t>(step);
              const bool operand = bit(last) != bit(last + 17);
              expected           = conjunction ? expected && operand : expected || operand;
            }
            EXPECT_EQ(outputs[0], static_cast<uint64_t>(expected));
            EXPECT_EQ(outputs[1], last);
          }
        }
      }
    }
    std::filesystem::remove(path);
    std::filesystem::remove(object);
  }
}

TEST(CgenLlvm, NativeLoopRetainsWideCarriesWhenInactive) {
  Cgen_llvm   kernel("active_body",
                     {
                       {  8, true},
                       {128, true},
                       {  1, true},
                       {  8, true}
  });
  std::string error;
  auto        sum    = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(1), kernel.input(0), 128, true);
  auto        active = kernel.binary(Cgen_llvm::Binary_op::lt, kernel.input(0), kernel.constant(8, 2), 1, true);
  auto        narrow = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(3), kernel.constant(8, 3), 8, true);
  ASSERT_TRUE(kernel.add_output(0, sum, error));
  ASSERT_TRUE(kernel.add_output(1, active, error));
  ASSERT_TRUE(kernel.add_output(2, narrow, error));
  Cgen_llvm::Loop_layout layout;
  layout.inputs = {
      {  8, true},
      {128, true},
      {  1, true},
      {  8, true}
  };
  layout.bindings = {
      {0, 0},
      {1, 0},
      {2, 0},
      {3, 0}
  };
  layout.index       = 0;
  layout.activation  = 2;
  layout.next_active = 1;
  layout.carries     = {
      {1, 0},
      {3, 2}
  };
  ASSERT_TRUE(kernel.add_loop("active_loop", layout, error)) << error;
  const auto path    = std::filesystem::temp_directory_path() / "livehd-active-loop.o";
  const auto bitcode = path.string() + ".bc";
  ASSERT_TRUE(kernel.write_bitcode(bitcode, error)) << error;
  auto buffer = llvm::MemoryBuffer::getFile(bitcode);
  ASSERT_TRUE(buffer);
  llvm::LLVMContext context;
  auto              module = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
  ASSERT_TRUE(module);
  const auto* fn = (*module)->getFunction("active_loop");
  ASSERT_NE(fn, nullptr);
  for (const auto& block : *fn) {
    for (const auto& instruction : block) {
      EXPECT_FALSE(llvm::isa<llvm::AllocaInst>(instruction));
      EXPECT_FALSE(llvm::isa<llvm::CallBase>(instruction));
    }
  }
  ASSERT_TRUE(kernel.write_object(path.string(), error)) << error;
  livehd::sim::Native_objects objects;
  ASSERT_TRUE(objects.load(path.string(), error)) << error;
  using Loop      = void (*)(const uint64_t*, uint64_t*, uint64_t, int64_t, int64_t);
  const auto loop = std::bit_cast<Loop>(objects.lookup("active_loop", error));
  ASSERT_NE(loop, nullptr) << error;
  uint64_t inputs[]   = {0, UINT64_MAX, 7, 1, 250};
  uint64_t outputs[4] = {};
  loop(inputs, outputs, 0, 0, 1);
  EXPECT_EQ(outputs[0], UINT64_MAX);
  EXPECT_EQ(outputs[1], 7u);
  EXPECT_EQ(outputs[2], 0u);
  EXPECT_EQ(outputs[3], 250u);
  loop(inputs, outputs, 10, 0, 1);
  EXPECT_EQ(outputs[0], 2u);
  EXPECT_EQ(outputs[1], 8u);
  EXPECT_EQ(outputs[2], 0u);
  EXPECT_EQ(outputs[3], 3u);
  inputs[3] = 0;
  loop(inputs, outputs, 10, 0, 1);
  EXPECT_EQ(outputs[0], UINT64_MAX);
  EXPECT_EQ(outputs[1], 7u);
  EXPECT_EQ(outputs[3], 250u);
  std::filesystem::remove(path);
  std::filesystem::remove(bitcode);
}

TEST(CgenLlvm, NativeLoopSharingIgnoresEntryAndInlineScopeNames) {
  const auto make = [](std::string_view name, size_t carry_input) {
    Cgen_llvm   kernel(name,
                       {
                         {8, true},
                         {8, true}
    });
    std::string error;
    const auto  sum = kernel.binary(Cgen_llvm::Binary_op::add, kernel.input(0), kernel.input(1), 8, true);
    EXPECT_TRUE(kernel.add_output(0, sum, error)) << error;
    Cgen_llvm::Loop_layout layout;
    layout.inputs = {
        {8, true},
        {8, true}
    };
    layout.bindings = {
        {0, 0},
        {1, 0}
    };
    layout.carries = {
        {carry_input, 0}
    };
    EXPECT_TRUE(kernel.add_loop(std::string(name) + "_loop", layout, error)) << error;
    return kernel.sharing_key(error);
  };
  const auto first = make("phase_one", 0);
  ASSERT_FALSE(first.empty());
  EXPECT_EQ(first, make("phase_two", 0));
  EXPECT_NE(first, make("phase_one", 1));
}
