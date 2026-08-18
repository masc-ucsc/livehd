// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_llvm.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "file_output.hpp"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"

namespace {

constexpr size_t word_count(uint32_t width) { return (static_cast<size_t>(width) + 63) / 64; }

llvm::Value* cast_integer(llvm::IRBuilder<>& builder, llvm::Value* value, unsigned width, bool source_unsigned) {
  const unsigned source_width = value->getType()->getIntegerBitWidth();
  if (source_width == width) {
    return value;
  }
  auto* type = builder.getIntNTy(width);
  if (source_width > width) {
    return builder.CreateTrunc(value, type);
  }
  return source_unsigned ? builder.CreateZExt(value, type) : builder.CreateSExt(value, type);
}

}  // namespace

class Cgen_llvm::Impl {
public:
  struct Output {
    size_t index = 0;
    Value  value;
  };

  llvm::LLVMContext                      context;
  std::unique_ptr<llvm::Module>          module;
  llvm::IRBuilder<>                      builder;
  llvm::Function*                        function = nullptr;
  llvm::Value*                           inputs   = nullptr;
  llvm::Value*                           outputs  = nullptr;
  llvm::Value*                           changed  = nullptr;
  llvm::Value*                           owner    = nullptr;
  std::vector<llvm::Value*>              values;
  std::vector<std::pair<uint32_t, bool>> input_types;
  std::vector<Output>                    output_values;
  std::string                            error;

  Impl(std::string_view function_name, const std::vector<std::pair<uint32_t, bool>>& input_desc)
      : module(std::make_unique<llvm::Module>("livehd.sim.color", context)), builder(context), input_types(input_desc) {
    auto* i64_ptr = llvm::PointerType::getUnqual(context);
    auto* fn_type = llvm::FunctionType::get(builder.getVoidTy(), {i64_ptr, i64_ptr, i64_ptr, i64_ptr}, false);
    function      = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, llvm::StringRef(function_name), *module);
    auto argument = function->arg_begin();
    inputs        = &*argument++;
    outputs       = &*argument++;
    changed       = &*argument++;
    owner         = &*argument;
    inputs->setName("inputs");
    outputs->setName("outputs");
    changed->setName("changed");
    owner->setName("owner");
    auto* entry = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(entry);

    values.reserve(input_types.size());
    size_t word_offset = 0;
    for (size_t i = 0; i < input_types.size(); ++i) {
      const auto [width, unused_unsigned] = input_types[i];
      (void)unused_unsigned;
      if (width == 0) {
        error = "LLVM color inputs must have a non-zero width";
        return;
      }
      values.push_back(load_packed(inputs, word_offset, width, "in"));
      word_offset += word_count(width);
    }
  }

  llvm::Value* load_packed(llvm::Value* base, size_t offset, uint32_t width, llvm::StringRef name) {
    auto*        type   = builder.getIntNTy(width);
    llvm::Value* result = llvm::ConstantInt::get(type, 0);
    auto*        i64    = builder.getInt64Ty();
    for (size_t word = 0; word < word_count(width); ++word) {
      auto* ptr   = builder.CreateConstInBoundsGEP1_64(i64, base, offset + word);
      auto* chunk = builder.CreateLoad(i64, ptr, name + ".word");
      auto* lane  = cast_integer(builder, chunk, width, true);
      if (word != 0) {
        lane = builder.CreateShl(lane, llvm::ConstantInt::get(type, word * 64));
      }
      result = builder.CreateOr(result, lane, name + ".bits");
    }
    return result;
  }

  void store_packed(llvm::Value* value, llvm::Value* base, size_t offset, uint32_t width) {
    auto* i64 = builder.getInt64Ty();
    for (size_t word = 0; word < word_count(width); ++word) {
      auto* lane = value;
      if (word != 0) {
        lane = builder.CreateLShr(lane, llvm::ConstantInt::get(value->getType(), word * 64));
      }
      lane      = cast_integer(builder, lane, 64, true);
      auto* ptr = builder.CreateConstInBoundsGEP1_64(i64, base, offset + word);
      builder.CreateStore(lane, ptr);
    }
  }

  llvm::Value* packed_alloca(llvm::Value* value, uint32_t width, llvm::StringRef name) {
    auto* words = builder.CreateAlloca(builder.getInt64Ty(), builder.getInt64(word_count(width)), name);
    store_packed(value, words, 0, width);
    return words;
  }

  Value remember(llvm::Value* value, uint32_t width, bool unsign) {
    const size_t id = values.size();
    values.push_back(value);
    return Value{id, width, unsign};
  }

  llvm::Value* get(Value value) {
    if (value.id >= values.size() || values[value.id] == nullptr || value.width == 0) {
      if (error.empty()) {
        error = "invalid LLVM color value";
      }
      return nullptr;
    }
    return values[value.id];
  }

  void trap_if(llvm::Value* condition, llvm::StringRef label) {
    auto* ok  = llvm::BasicBlock::Create(context, label + ".ok", function);
    auto* bad = llvm::BasicBlock::Create(context, label + ".bad", function);
    builder.CreateCondBr(condition, bad, ok);
    builder.SetInsertPoint(bad);
    auto* trap = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::trap);
    builder.CreateCall(trap);
    builder.CreateUnreachable();
    builder.SetInsertPoint(ok);
  }
};

Cgen_llvm::Cgen_llvm(std::string_view function_name, const std::vector<std::pair<uint32_t, bool>>& inputs)
    : impl_(std::make_unique<Impl>(function_name, inputs)) {}

Cgen_llvm::~Cgen_llvm()                               = default;
Cgen_llvm::Cgen_llvm(Cgen_llvm&&) noexcept            = default;
Cgen_llvm& Cgen_llvm::operator=(Cgen_llvm&&) noexcept = default;

Cgen_llvm::Value Cgen_llvm::input(size_t index) const {
  if (index >= impl_->input_types.size()) {
    return {};
  }
  return Value{index, impl_->input_types[index].first, impl_->input_types[index].second};
}

Cgen_llvm::Value Cgen_llvm::constant(uint32_t width, uint64_t value, bool unsign) { return constant_words(width, {value}, unsign); }

Cgen_llvm::Value Cgen_llvm::constant_words(uint32_t width, const std::vector<uint64_t>& words, bool unsign) {
  if (width == 0 || !impl_->error.empty()) {
    return {};
  }
  llvm::APInt bits(width, llvm::ArrayRef<uint64_t>(words));
  return impl_->remember(llvm::ConstantInt::get(impl_->context, bits), width, unsign);
}

Cgen_llvm::Value Cgen_llvm::resize(Value value, uint32_t result_width, bool result_unsign) {
  auto* operand = impl_->get(value);
  if (operand == nullptr || result_width == 0) {
    return {};
  }
  return impl_->remember(cast_integer(impl_->builder, operand, result_width, value.unsign), result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::unary_not(Value value, uint32_t result_width, bool result_unsign) {
  auto* operand = impl_->get(value);
  if (operand == nullptr || result_width == 0) {
    return {};
  }
  operand = cast_integer(impl_->builder, operand, result_width, value.unsign);
  return impl_->remember(impl_->builder.CreateNot(operand), result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::reduce_or(Value value, uint32_t result_width, bool result_unsign) {
  auto* operand = impl_->get(value);
  if (operand == nullptr || result_width == 0) {
    return {};
  }
  auto* nonzero = impl_->builder.CreateICmpNE(operand, llvm::ConstantInt::get(operand->getType(), 0));
  return impl_->remember(cast_integer(impl_->builder, nonzero, result_width, true), result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::bitfield_insert(Value base, Value inserted, uint32_t lo, uint32_t hi, uint32_t result_width,
                                            bool result_unsign) {
  auto* original = impl_->get(base);
  auto* value    = impl_->get(inserted);
  if (original == nullptr || value == nullptr || result_width == 0 || lo >= hi || hi > result_width) {
    return {};
  }
  auto* type = impl_->builder.getIntNTy(result_width);
  original   = cast_integer(impl_->builder, original, result_width, base.unsign);
  value      = cast_integer(impl_->builder, value, result_width, inserted.unsign);
  if (lo != 0) {
    value = impl_->builder.CreateShl(value, llvm::ConstantInt::get(type, lo));
  }
  const llvm::APInt mask = llvm::APInt::getBitsSet(result_width, lo, hi);
  auto*             bits = llvm::ConstantInt::get(impl_->context, mask);
  original               = impl_->builder.CreateAnd(original, impl_->builder.CreateNot(bits));
  value                  = impl_->builder.CreateAnd(value, bits);
  return impl_->remember(impl_->builder.CreateOr(original, value), result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::sign_extend_from(Value value, uint32_t sign_bit, uint32_t result_width, bool result_unsign) {
  auto* operand = impl_->get(value);
  if (operand == nullptr || result_width == 0 || sign_bit >= result_width) {
    return {};
  }
  const uint32_t signed_width = sign_bit + 1;
  operand                     = cast_integer(impl_->builder, operand, signed_width, value.unsign);
  operand                     = cast_integer(impl_->builder, operand, result_width, false);
  return impl_->remember(operand, result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::binary(Binary_op op, Value lhs, Value rhs, uint32_t result_width, bool result_unsign) {
  auto* left  = impl_->get(lhs);
  auto* right = impl_->get(rhs);
  if (left == nullptr || right == nullptr || result_width == 0) {
    return {};
  }

  // LLVM shifts are poison when the count is >= the value width. Slop instead
  // returns zero (logical shifts) or the sign fill (arithmetic shift), so make
  // that behavior explicit before target lowering.
  if (op == Binary_op::shl || op == Binary_op::lshr || op == Binary_op::ashr) {
    // A RIGHT shift has to run at the SOURCE width and land afterwards: the
    // bitwidth pass legitimately narrows `a >> k`'s result below `a`'s width,
    // and truncating first would shift the very bits the node selects out of
    // existence (Slop shifts the untruncated carrier and lands the result).
    const unsigned work_width
        = op == Binary_op::shl ? result_width : std::max<unsigned>(lhs.width, result_width);
    left                         = cast_integer(impl_->builder, left, work_width, lhs.unsign);
    const unsigned compare_width = std::max<unsigned>(rhs.width, 32);
    auto*          count         = cast_integer(impl_->builder, right, compare_width, true);
    auto*          too_large
        = impl_->builder.CreateICmpUGE(count, llvm::ConstantInt::get(impl_->builder.getIntNTy(compare_width), work_width));
    auto* shift_count = cast_integer(impl_->builder, right, work_width, true);
    auto* shifted     = op == Binary_op::shl    ? impl_->builder.CreateShl(left, shift_count)
                        : op == Binary_op::lshr ? impl_->builder.CreateLShr(left, shift_count)
                                                : impl_->builder.CreateAShr(left, shift_count);
    auto* overflow    = op == Binary_op::ashr
                            ? impl_->builder.CreateAShr(left, llvm::ConstantInt::get(left->getType(), work_width - 1))
                            : llvm::ConstantInt::get(left->getType(), 0);
    auto* selected    = impl_->builder.CreateSelect(too_large, overflow, shifted);
    return impl_->remember(cast_integer(impl_->builder, selected, result_width, true), result_width, result_unsign);
  }

  const bool comparison = op == Binary_op::eq || op == Binary_op::ne || op == Binary_op::lt || op == Binary_op::le
                          || op == Binary_op::gt || op == Binary_op::ge;
  const bool ordered    = op == Binary_op::lt || op == Binary_op::le || op == Binary_op::gt || op == Binary_op::ge;
  // An ORDERED compare is sign-aware and a mixed-sign pair has no common
  // interpretation at max(width): the unsigned side's top bit would be read as
  // a sign. One extra bit gives each side room to extend by its OWN rule, which
  // is exactly the `cw += 1` the reference Slop lowering applies. EQ is a
  // bit-pattern compare and must NOT get the headroom.
  unsigned operation_width = comparison ? std::max(lhs.width, rhs.width) : result_width;
  if (ordered && (!lhs.unsign || !rhs.unsign)) {
    ++operation_width;
  }
  left  = cast_integer(impl_->builder, left, operation_width, lhs.unsign);
  right = cast_integer(impl_->builder, right, operation_width, rhs.unsign);

  llvm::Value* result = nullptr;
  switch (op) {
    case Binary_op::add: result = impl_->builder.CreateAdd(left, right); break;
    case Binary_op::sub: result = impl_->builder.CreateSub(left, right); break;
    case Binary_op::mul: result = impl_->builder.CreateMul(left, right); break;
    case Binary_op::div:
    case Binary_op::rem: {
      auto* zero = llvm::ConstantInt::get(right->getType(), 0);
      impl_->trap_if(impl_->builder.CreateICmpEQ(right, zero), "divide.zero");

      if (lhs.unsign && rhs.unsign) {
        result = op == Binary_op::div ? impl_->builder.CreateUDiv(left, right) : impl_->builder.CreateURem(left, right);
      } else {
        auto* min_value = llvm::ConstantInt::get(impl_->context, llvm::APInt::getSignedMinValue(operation_width));
        auto* neg_one   = llvm::ConstantInt::get(right->getType(), -1, true);
        auto* overflow
            = impl_->builder.CreateAnd(impl_->builder.CreateICmpEQ(left, min_value), impl_->builder.CreateICmpEQ(right, neg_one));
        auto* safe_rhs = impl_->builder.CreateSelect(overflow, llvm::ConstantInt::get(right->getType(), 1), right);
        auto* divided
            = op == Binary_op::div ? impl_->builder.CreateSDiv(left, safe_rhs) : impl_->builder.CreateSRem(left, safe_rhs);
        auto* wrapped = op == Binary_op::div ? min_value : zero;
        result        = impl_->builder.CreateSelect(overflow, wrapped, divided);
      }
      break;
    }
    case Binary_op::bit_and: result = impl_->builder.CreateAnd(left, right); break;
    case Binary_op::bit_or : result = impl_->builder.CreateOr(left, right); break;
    case Binary_op::bit_xor: result = impl_->builder.CreateXor(left, right); break;
    case Binary_op::shl    : break;
    case Binary_op::lshr   : break;
    case Binary_op::ashr   : break;
    case Binary_op::eq     : result = impl_->builder.CreateICmpEQ(left, right); break;
    case Binary_op::ne     : result = impl_->builder.CreateICmpNE(left, right); break;
    case Binary_op::lt:
      result = lhs.unsign && rhs.unsign ? impl_->builder.CreateICmpULT(left, right) : impl_->builder.CreateICmpSLT(left, right);
      break;
    case Binary_op::le:
      result = lhs.unsign && rhs.unsign ? impl_->builder.CreateICmpULE(left, right) : impl_->builder.CreateICmpSLE(left, right);
      break;
    case Binary_op::gt:
      result = lhs.unsign && rhs.unsign ? impl_->builder.CreateICmpUGT(left, right) : impl_->builder.CreateICmpSGT(left, right);
      break;
    case Binary_op::ge:
      result = lhs.unsign && rhs.unsign ? impl_->builder.CreateICmpUGE(left, right) : impl_->builder.CreateICmpSGE(left, right);
      break;
  }
  result = cast_integer(impl_->builder, result, result_width, true);
  return impl_->remember(result, result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::mux(Value select, Value when_false, Value when_true, uint32_t result_width, bool result_unsign) {
  auto* condition = impl_->get(select);
  auto* on_false  = impl_->get(when_false);
  auto* on_true   = impl_->get(when_true);
  if (condition == nullptr || on_false == nullptr || on_true == nullptr || result_width == 0) {
    return {};
  }
  condition = impl_->builder.CreateICmpNE(condition, llvm::ConstantInt::get(condition->getType(), 0));
  on_false  = cast_integer(impl_->builder, on_false, result_width, when_false.unsign);
  on_true   = cast_integer(impl_->builder, on_true, result_width, when_true.unsign);
  return impl_->remember(impl_->builder.CreateSelect(condition, on_true, on_false), result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::indexed_mux(Value select, const std::vector<Value>& arms, bool one_hot, uint32_t result_width,
                                        bool result_unsign) {
  auto* selector = impl_->get(select);
  if (selector == nullptr || arms.empty() || result_width == 0) {
    return {};
  }
  if (one_hot) {
    auto* zero     = llvm::ConstantInt::get(selector->getType(), 0);
    auto* one      = llvm::ConstantInt::get(selector->getType(), 1);
    auto* one_less = impl_->builder.CreateSub(selector, one);
    auto* multiple = impl_->builder.CreateICmpNE(impl_->builder.CreateAnd(selector, one_less), zero);
    auto* none     = impl_->builder.CreateICmpEQ(selector, zero);
    auto* valid_bits
        = llvm::ConstantInt::get(impl_->context,
                                 llvm::APInt::getLowBitsSet(select.width, std::min<size_t>(select.width, arms.size())));
    auto* outside = impl_->builder.CreateICmpNE(impl_->builder.CreateAnd(selector, impl_->builder.CreateNot(valid_bits)), zero);
    impl_->trap_if(impl_->builder.CreateOr(impl_->builder.CreateOr(none, multiple), outside), "hotmux.select");
  } else {
    const unsigned arm_index_bits = llvm::APInt(64, arms.size()).getActiveBits();
    if (arm_index_bits <= select.width) {
      auto* limit = llvm::ConstantInt::get(selector->getType(), arms.size());
      impl_->trap_if(impl_->builder.CreateICmpUGE(selector, limit), "mux.select");
    }
  }
  llvm::Value* result = llvm::ConstantInt::get(impl_->builder.getIntNTy(result_width), 0);
  for (size_t i = arms.size(); i-- > 0;) {
    auto* arm = impl_->get(arms[i]);
    if (arm == nullptr) {
      return {};
    }
    arm = cast_integer(impl_->builder, arm, result_width, arms[i].unsign);
    llvm::Value* selected;
    if (one_hot) {
      if (i >= select.width) {
        selected = llvm::ConstantInt::getFalse(impl_->context);
      } else {
        auto* shifted = i == 0 ? selector : impl_->builder.CreateLShr(selector, llvm::ConstantInt::get(selector->getType(), i));
        selected      = impl_->builder.CreateTrunc(shifted, impl_->builder.getInt1Ty());
      }
    } else {
      selected = impl_->builder.CreateICmpEQ(selector, llvm::ConstantInt::get(selector->getType(), i));
    }
    result = impl_->builder.CreateSelect(selected, arm, result);
  }
  return impl_->remember(result, result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::lut(Value table, Value address, uint32_t result_width, bool result_unsign) {
  auto* bits = impl_->get(table);
  auto* addr = impl_->get(address);
  if (bits == nullptr || addr == nullptr || result_width == 0) {
    return {};
  }
  const unsigned compare_width = std::max<unsigned>(address.width, 32);
  auto*          count         = cast_integer(impl_->builder, addr, compare_width, true);
  auto*          out_of_range
      = impl_->builder.CreateICmpUGE(count, llvm::ConstantInt::get(impl_->builder.getIntNTy(compare_width), table.width));
  auto* safe  = impl_->builder.CreateSelect(out_of_range, llvm::ConstantInt::get(addr->getType(), 0), addr);
  auto* shift = cast_integer(impl_->builder, safe, table.width, true);
  auto* bit   = impl_->builder.CreateTrunc(impl_->builder.CreateLShr(bits, shift), impl_->builder.getInt1Ty());
  bit         = impl_->builder.CreateSelect(out_of_range, llvm::ConstantInt::getFalse(impl_->context), bit);
  return impl_->remember(cast_integer(impl_->builder, bit, result_width, true), result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::external_read(std::string_view symbol, Value address, uint32_t result_width, bool result_unsign) {
  auto* addr = impl_->get(address);
  if (addr == nullptr || address.width == 0 || result_width == 0 || symbol.empty()) {
    return {};
  }
  auto* pointer        = llvm::PointerType::getUnqual(impl_->context);
  auto* type           = llvm::FunctionType::get(impl_->builder.getVoidTy(), {pointer, pointer, pointer}, false);
  auto  callee         = impl_->module->getOrInsertFunction(llvm::StringRef(symbol), type);
  auto* packed_address = impl_->packed_alloca(addr, address.width, "mem.addr");
  auto* packed_result
      = impl_->builder.CreateAlloca(impl_->builder.getInt64Ty(), impl_->builder.getInt64(word_count(result_width)), "mem.result");
  impl_->builder.CreateCall(callee, {impl_->owner, packed_address, packed_result});
  return impl_->remember(impl_->load_packed(packed_result, 0, result_width, "mem.read"), result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::external_read_all(std::string_view symbol, uint32_t result_width, bool result_unsign) {
  if (result_width == 0 || symbol.empty()) {
    return {};
  }
  auto* pointer = llvm::PointerType::getUnqual(impl_->context);
  auto* type    = llvm::FunctionType::get(impl_->builder.getVoidTy(), {pointer, pointer}, false);
  auto  callee  = impl_->module->getOrInsertFunction(llvm::StringRef(symbol), type);
  auto* packed_result
      = impl_->builder.CreateAlloca(impl_->builder.getInt64Ty(), impl_->builder.getInt64(word_count(result_width)), "mem.all");
  impl_->builder.CreateCall(callee, {impl_->owner, packed_result});
  return impl_->remember(impl_->load_packed(packed_result, 0, result_width, "mem.read_all"), result_width, result_unsign);
}

bool Cgen_llvm::external_apply(std::string_view symbol, Value data) {
  auto* din = impl_->get(data);
  if (din == nullptr || data.width == 0 || symbol.empty()) {
    return false;
  }
  auto* pointer = llvm::PointerType::getUnqual(impl_->context);
  auto* type    = llvm::FunctionType::get(impl_->builder.getVoidTy(), {pointer, pointer}, false);
  auto  callee  = impl_->module->getOrInsertFunction(llvm::StringRef(symbol), type);
  impl_->builder.CreateCall(callee, {impl_->owner, impl_->packed_alloca(din, data.width, "mem.update")});
  return true;
}

bool Cgen_llvm::external_clear(std::string_view symbol) {
  if (symbol.empty()) {
    return false;
  }
  auto* pointer = llvm::PointerType::getUnqual(impl_->context);
  auto* type    = llvm::FunctionType::get(impl_->builder.getVoidTy(), {pointer}, false);
  auto  callee  = impl_->module->getOrInsertFunction(llvm::StringRef(symbol), type);
  impl_->builder.CreateCall(callee, {impl_->owner});
  return true;
}

bool Cgen_llvm::external_stage_whole(std::string_view symbol, Value enable, Value force, Value data) {
  auto* wen = impl_->get(enable);
  auto* rst = impl_->get(force);
  auto* din = impl_->get(data);
  if (wen == nullptr || rst == nullptr || din == nullptr || enable.width == 0 || force.width == 0 || data.width == 0
      || symbol.empty()) {
    return false;
  }
  auto* pointer = llvm::PointerType::getUnqual(impl_->context);
  auto* type    = llvm::FunctionType::get(impl_->builder.getVoidTy(), {pointer, pointer, pointer, pointer}, false);
  auto  callee  = impl_->module->getOrInsertFunction(llvm::StringRef(symbol), type);
  impl_->builder.CreateCall(callee,
                            {impl_->owner,
                             impl_->packed_alloca(wen, enable.width, "mem.update_enable"),
                             impl_->packed_alloca(rst, force.width, "mem.update_force"),
                             impl_->packed_alloca(din, data.width, "mem.update")});
  return true;
}

bool Cgen_llvm::external_stage_write(std::string_view symbol, Value enable, Value address, Value data) {
  auto* wen  = impl_->get(enable);
  auto* addr = impl_->get(address);
  auto* din  = impl_->get(data);
  if (wen == nullptr || addr == nullptr || din == nullptr || enable.width == 0 || address.width == 0 || data.width == 0
      || symbol.empty()) {
    return false;
  }
  auto* pointer = llvm::PointerType::getUnqual(impl_->context);
  auto* type    = llvm::FunctionType::get(impl_->builder.getVoidTy(), {pointer, pointer, pointer, pointer}, false);
  auto  callee  = impl_->module->getOrInsertFunction(llvm::StringRef(symbol), type);
  impl_->builder.CreateCall(callee,
                            {impl_->owner,
                             impl_->packed_alloca(wen, enable.width, "mem.wen"),
                             impl_->packed_alloca(addr, address.width, "mem.addr"),
                             impl_->packed_alloca(din, data.width, "mem.din")});
  return true;
}

bool Cgen_llvm::add_output(size_t index, Value value, std::string& error) {
  if (value.width == 0 || impl_->get(value) == nullptr) {
    error = impl_->error.empty() ? "invalid LLVM color output" : impl_->error;
    return false;
  }
  impl_->output_values.push_back({index, value});
  return true;
}

bool Cgen_llvm::write_object(std::string_view path, std::string& error) {
  if (!impl_->error.empty()) {
    error = impl_->error;
    return false;
  }
  std::ranges::sort(impl_->output_values, {}, &Impl::Output::index);
  for (size_t i = 0; i < impl_->output_values.size(); ++i) {
    if (impl_->output_values[i].index != i) {
      error = "LLVM color outputs must be unique and densely indexed";
      return false;
    }
  }
  auto*        i64           = impl_->builder.getInt64Ty();
  const size_t changed_words = std::max<size_t>(1, word_count(static_cast<uint32_t>(impl_->output_values.size())));
  for (size_t word = 0; word < changed_words; ++word) {
    auto* ptr = impl_->builder.CreateConstInBoundsGEP1_64(i64, impl_->changed, word);
    impl_->builder.CreateStore(llvm::ConstantInt::get(i64, 0), ptr);
  }
  size_t output_word_offset = 0;
  for (const auto& output : impl_->output_values) {
    auto*        value = impl_->get(output.value);
    auto*        old   = impl_->load_packed(impl_->outputs, output_word_offset, output.value.width, "out.old");
    auto*        diff  = impl_->builder.CreateICmpNE(old, value);
    auto*        ptr   = impl_->builder.CreateConstInBoundsGEP1_64(i64, impl_->changed, output.index / 64);
    llvm::Value* bits  = impl_->builder.CreateLoad(i64, ptr, "changed.old");
    auto*        flag  = llvm::ConstantInt::get(i64, uint64_t{1} << (output.index % 64));
    bits               = impl_->builder.CreateOr(bits, impl_->builder.CreateSelect(diff, flag, llvm::ConstantInt::get(i64, 0)));
    impl_->builder.CreateStore(bits, ptr);
    impl_->store_packed(value, impl_->outputs, output_word_offset, output.value.width);
    output_word_offset += word_count(output.value.width);
  }
  impl_->builder.CreateRetVoid();

  if (llvm::verifyModule(*impl_->module, &llvm::errs())) {
    error = "LLVM rejected the generated simulator module";
    return false;
  }

  static std::once_flag initialize_target;
  std::call_once(initialize_target, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
  });

  const llvm::Triple  triple(llvm::sys::getDefaultTargetTriple());
  std::string         lookup_error;
  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookup_error);
  if (target == nullptr) {
    error = lookup_error;
    return false;
  }
  llvm::TargetOptions options;
  // PIC, explicitly. The object is linked into an executable the host driver
  // builds, and every mainstream Linux toolchain defaults that link to PIE:
  // the Static model LLVM picks for a null reloc model emits absolute
  // relocations the PIE link then refuses.
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(triple,
                                                                          "generic",
                                                                          "",
                                                                          options,
                                                                          llvm::Reloc::PIC_,
                                                                          std::nullopt,
                                                                          llvm::CodeGenOptLevel::Aggressive));
  if (!machine) {
    error = "LLVM could not create a native target machine";
    return false;
  }
  impl_->module->setTargetTriple(triple);
  impl_->module->setDataLayout(machine->createDataLayout());

  // Keep exact-width bit operations visible to a deliberately bounded scalar
  // pipeline before instruction selection. The generic O2 module pipeline is
  // a poor fit for generated color kernels: Minion's largest straight-line
  // color spent more than 14 minutes in GVN alone. These kernels contain no
  // internal calls or loops, so one pass each of stack promotion, local CSE,
  // bit folding, CFG cleanup, and dead-code removal captures the useful
  // simplifications without the inliner/GVN compile-time cliff.
  llvm::LoopAnalysisManager     loop_analyses;
  llvm::FunctionAnalysisManager function_analyses;
  llvm::CGSCCAnalysisManager    cgscc_analyses;
  llvm::ModuleAnalysisManager   module_analyses;
  llvm::PassBuilder             pass_builder(machine.get());
  pass_builder.registerModuleAnalyses(module_analyses);
  pass_builder.registerCGSCCAnalyses(cgscc_analyses);
  pass_builder.registerFunctionAnalyses(function_analyses);
  pass_builder.registerLoopAnalyses(loop_analyses);
  pass_builder.crossRegisterProxies(loop_analyses, function_analyses, cgscc_analyses, module_analyses);
  llvm::FunctionPassManager functions;
  functions.addPass(llvm::SROAPass(llvm::SROAOptions::ModifyCFG));
  functions.addPass(llvm::EarlyCSEPass());
  functions.addPass(llvm::InstCombinePass());
  functions.addPass(llvm::SimplifyCFGPass());
  functions.addPass(llvm::ADCEPass());
  llvm::ModulePassManager pipeline;
  pipeline.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functions)));
  pipeline.run(*impl_->module, module_analyses);

  // Emit into MEMORY, then hand the bytes to File_output rather than writing
  // the file directly. The object is a link input keyed on mtime by the
  // generated build.ninja, so an unconditional truncate+rewrite forced a relink
  // on every single `lhd sim` — including one where nothing changed at all and
  // the object came out byte-identical. Every other generated artifact in the
  // sim tree already goes through the same write-if-different path; this one
  // was the lone hole, and it is why the LLVM backend could never reach a
  // no-work warm rebuild.
  //
  // Object files are the smallest thing in that tree (the C++ they replace is
  // far larger), so buffering one is a cheaper trade than the relink it avoids.
  llvm::SmallVector<char, 0>  buffer;
  llvm::raw_svector_ostream   object(buffer);
  llvm::legacy::PassManager   emit;
  if (machine->addPassesToEmitFile(emit, object, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    error = "LLVM target cannot emit object files";
    return false;
  }
  emit.run(*impl_->module);
  {
    File_output out{path};
    out.append(std::string_view(buffer.data(), buffer.size()));
  }
  return true;
}
