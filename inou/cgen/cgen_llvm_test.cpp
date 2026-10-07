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
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "sim_compile_workers.hpp"
#include "sim_native_rt.hpp"

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
        EXPECT_EQ(load->getType()->getIntegerBitWidth(), 65u);
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
