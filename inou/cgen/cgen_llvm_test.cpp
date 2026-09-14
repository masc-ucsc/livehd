// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_llvm.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"

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
