// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_llvm.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "file_output.hpp"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"

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

  llvm::LLVMContext                       context;
  std::unique_ptr<llvm::Module>           module;
  llvm::IRBuilder<>                       builder;
  llvm::Function*                         function = nullptr;
  llvm::Value*                            inputs   = nullptr;
  llvm::Value*                            outputs  = nullptr;
  llvm::Value*                            changed  = nullptr;
  llvm::Value*                            owner    = nullptr;
  std::vector<llvm::Value*>               values;
  std::vector<size_t>                     input_word_offsets;
  std::vector<std::optional<Value>>       deferred_casts;
  std::vector<std::pair<uint32_t, bool>>  input_types;
  std::vector<Output>                     output_values;
  std::string                             error;
  bool                                    scalar_abi = false;
  std::unordered_map<std::string, Memory> memories;

  Impl(std::string_view function_name, const std::vector<std::pair<uint32_t, bool>>& input_desc, bool use_scalar_abi)
      : module(std::make_unique<llvm::Module>("livehd.sim.color", context))
      , builder(context)
      , input_types(input_desc)
      , scalar_abi(use_scalar_abi) {
    auto*                          i64_ptr = llvm::PointerType::getUnqual(context);
    llvm::SmallVector<llvm::Type*> argument_types;
    if (scalar_abi) {
      argument_types.assign(input_types.size(), builder.getInt64Ty());
      argument_types.push_back(i64_ptr);
    } else {
      argument_types.assign(4, i64_ptr);
    }
    auto* fn_type = llvm::FunctionType::get(scalar_abi ? builder.getInt64Ty() : builder.getVoidTy(), argument_types, false);
    function      = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, llvm::StringRef(function_name), *module);
    function->addFnAttr(llvm::Attribute::AlwaysInline);
    auto argument = function->arg_begin();
    if (!scalar_abi) {
      inputs  = &*argument++;
      outputs = &*argument++;
      changed = &*argument++;
      inputs->setName("inputs");
      outputs->setName("outputs");
      changed->setName("changed");
      // The generated caller uses three disjoint packed buffers. Owner points
      // at module storage, not into any of those local buffers.
      for (unsigned index = 0; index < 4; ++index) {
        function->addParamAttr(index, llvm::Attribute::NoAlias);
      }
      function->addParamAttr(0, llvm::Attribute::ReadOnly);
    }
    auto* entry = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(entry);

    values.reserve(input_types.size());
    deferred_casts.resize(input_types.size());
    size_t word_offset = 0;
    for (size_t i = 0; i < input_types.size(); ++i) {
      const auto [width, unused_unsigned] = input_types[i];
      (void)unused_unsigned;
      if (width == 0) {
        error = "LLVM color inputs must have a non-zero width";
        return;
      }
      input_word_offsets.push_back(word_offset);
      if (scalar_abi) {
        auto* value = &*argument++;
        value->setName("in");
        values.push_back(cast_integer(builder, value, width, true));
      } else {
        values.push_back(nullptr);  // Materialize at first use, not at kernel entry.
      }
      word_offset += word_count(width);
    }
    owner = &*argument;
    owner->setName("owner");
  }

  llvm::Value* load_packed(llvm::Value* base, size_t offset, uint32_t width, llvm::StringRef name) {
    // Packed words are least-significant first. A native little-endian wide
    // load reads that layout directly and keeps packed arrays out of the
    // optimizer's word-by-word shift/OR reconstruction.
    if constexpr (std::endian::native == std::endian::little) {
      if (width > 64) {
        auto* ptr = builder.CreateConstInBoundsGEP1_64(builder.getInt64Ty(), base, offset);
        return builder.CreateAlignedLoad(builder.getIntNTy(width), ptr, llvm::Align(alignof(uint64_t)), name + ".bits");
      }
    }
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
    if constexpr (std::endian::native == std::endian::little) {
      if (width > 64) {
        auto* ptr    = builder.CreateConstInBoundsGEP1_64(i64, base, offset);
        auto* padded = cast_integer(builder, value, static_cast<unsigned>(word_count(width) * 64), true);
        auto* store  = builder.CreateStore(padded, ptr);
        store->setAlignment(llvm::Align(alignof(uint64_t)));
        return;
      }
    }
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

  llvm::Value* resource(size_t index) {
    auto* slot = builder.CreateConstInBoundsGEP1_64(builder.getPtrTy(), owner, index);
    return builder.CreateLoad(builder.getPtrTy(), slot, "resource");
  }
  llvm::Value* word_ptr(llvm::Value* base, uint64_t offset) {
    return builder.CreateConstInBoundsGEP1_64(builder.getInt64Ty(), base, offset);
  }
  llvm::Value* pending_ptr(const Memory& m, uint32_t port) {
    return word_ptr(resource(m.pending), uint64_t(port) * (3 * word_count(m.bits) + 2));
  }
  llvm::Value* fired_ptr(const Memory& m, llvm::Value* pending) { return word_ptr(pending, 3 * word_count(m.bits) + 1); }
  llvm::Value* valid_address(Value address, uint64_t size) {
    auto*      a     = get(address);
    const auto width = std::max(64u, address.width);
    a                = cast_integer(builder, a, width, address.unsign);
    return builder.CreateICmpULT(a, llvm::ConstantInt::get(builder.getIntNTy(width), size));
  }
  template <class Fn>
  void when(llvm::Value* condition, Fn emit) {
    auto* yes  = llvm::BasicBlock::Create(context, "memory.active", function);
    auto* done = llvm::BasicBlock::Create(context, "memory.done", function);
    builder.CreateCondBr(condition, yes, done);
    builder.SetInsertPoint(yes);
    emit();
    builder.CreateBr(done);
    builder.SetInsertPoint(done);
  }
  llvm::Value* random_value(const Memory& m) {
    auto*        state  = resource(m.random);
    auto*        draws  = resource(m.draws);
    llvm::Value* result = llvm::ConstantInt::get(builder.getIntNTy(m.bits), 0);
    for (size_t word = 0; word < word_count(m.bits); ++word) {
      auto* next = builder.CreateAdd(builder.CreateLoad(builder.getInt64Ty(), state), builder.getInt64(0x9e3779b97f4a7c15ULL));
      builder.CreateStore(next, state);
      builder.CreateStore(builder.CreateAdd(builder.CreateLoad(builder.getInt64Ty(), draws), builder.getInt64(1)), draws);
      auto* z = builder.CreateMul(builder.CreateXor(next, builder.CreateLShr(next, 30)), builder.getInt64(0xbf58476d1ce4e5b9ULL));
      z       = builder.CreateMul(builder.CreateXor(z, builder.CreateLShr(z, 27)), builder.getInt64(0x94d049bb133111ebULL));
      z       = builder.CreateXor(z, builder.CreateLShr(z, 31));
      z       = cast_integer(builder, z, m.bits, true);
      result  = builder.CreateOr(result, builder.CreateShl(z, llvm::ConstantInt::get(z->getType(), word * 64)));
    }
    return result;
  }

  void commit_memory(const Memory& m) {
    for (uint32_t port = 0; port < m.writes; ++port) {
      auto* pending = pending_ptr(m, port);
      auto* fired   = fired_ptr(m, pending);
      auto* active  = builder.CreateICmpNE(builder.CreateLoad(builder.getInt8Ty(), fired), builder.getInt8(0));
      when(active, [&] {
        auto* address = builder.CreateLoad(builder.getInt64Ty(), word_ptr(pending, 3 * word_count(m.bits)));
        auto* ptr     = builder.CreateInBoundsGEP(builder.getInt64Ty(),
                                                  resource(m.data),
                                                  builder.CreateMul(address, builder.getInt64(word_count(m.bits))));
        auto* old     = load_packed(ptr, 0, m.bits, "memory.old");
        auto* data    = load_packed(pending, 0, m.bits, "memory.new");
        auto* mask    = load_packed(pending, word_count(m.bits), m.bits, "memory.mask");
        store_packed(builder.CreateOr(builder.CreateAnd(old, builder.CreateNot(mask)), builder.CreateAnd(data, mask)),
                     ptr,
                     0,
                     m.bits);
        builder.CreateStore(builder.getInt8(0), fired);
      });
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
    deferred_casts.emplace_back();
    return Value{id, width, unsign};
  }

  llvm::Value* get(Value value) {
    if (value.id < input_types.size() && value.id < values.size() && values[value.id] == nullptr && value.width != 0) {
      const auto width = input_types[value.id].first;
      values[value.id] = load_packed(inputs, input_word_offsets[value.id], width, "in");
    }
    if (value.id < values.size() && values[value.id] == nullptr && deferred_casts[value.id]) {
      const auto source  = *deferred_casts[value.id];
      auto*      operand = get(source);
      if (operand != nullptr) {
        values[value.id] = cast_integer(builder, operand, value.width, source.unsign);
      }
    }
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

Cgen_llvm::Cgen_llvm(std::string_view function_name, const std::vector<std::pair<uint32_t, bool>>& inputs, bool scalar_abi)
    : impl_(std::make_unique<Impl>(function_name, inputs, scalar_abi)) {}

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
  if (value.width == 0 || result_width == 0 || value.id >= impl_->values.size()) {
    return {};
  }
  if (value.width == result_width) {
    return Value{value.id, result_width, result_unsign};
  }
  const auto result                = impl_->remember(nullptr, result_width, result_unsign);
  impl_->deferred_casts[result.id] = value;
  return result;
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

Cgen_llvm::Value Cgen_llvm::count_bits(Value value, uint32_t count, uint32_t result_width, bool parity) {
  if (count == 0) {
    return constant(result_width, 0, true);
  }
  auto* operand = impl_->get(value);
  if (operand == nullptr || result_width == 0) {
    return {};
  }
  operand                 = cast_integer(impl_->builder, operand, count, value.unsign);
  llvm::Value* population = impl_->builder.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, operand);
  if (parity) {
    population = impl_->builder.CreateAnd(population, llvm::ConstantInt::get(population->getType(), 1));
  }
  return impl_->remember(cast_integer(impl_->builder, population, result_width, true), result_width, true);
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

Cgen_llvm::Value Cgen_llvm::dynamic_extract(Value source, Value count, uint32_t len, uint32_t result_width, bool result_unsign) {
  auto& builder = impl_->builder;
  if (len == 0 || len > 64 || result_width == 0 || source.width == 0) {
    return {};
  }
  auto* raw_count = impl_->get(count);
  if (raw_count == nullptr) {
    return {};
  }
  const size_t nwords = word_count(source.width);
  llvm::Value* base   = nullptr;
  size_t       offset = 0;
  if (impl_->inputs != nullptr && source.id < impl_->input_types.size() && impl_->input_types[source.id].first == source.width) {
    base   = impl_->inputs;  // a kernel input: its packed words are already in the input buffer
    offset = impl_->input_word_offsets[source.id];
  } else {
    auto* value = impl_->get(source);
    if (value == nullptr) {
      return {};
    }
    base = impl_->packed_alloca(cast_integer(builder, value, source.width, source.unsign), source.width, "extract.words");
  }
  auto* i64       = builder.getInt64Ty();
  auto* n64       = cast_integer(builder, raw_count, 64, true);
  auto* word      = builder.CreateLShr(n64, llvm::ConstantInt::get(i64, 6), "extract.word");
  auto* shift     = builder.CreateAnd(n64, llvm::ConstantInt::get(i64, 63), "extract.shift");
  auto* limit     = llvm::ConstantInt::get(i64, nwords);
  auto  load_word = [&](llvm::Value* index) -> llvm::Value* {
    auto* in_range = builder.CreateICmpULT(index, limit);
    auto* clamped  = builder.CreateSelect(in_range, index, llvm::ConstantInt::get(i64, nwords - 1));
    auto* ptr      = builder.CreateInBoundsGEP(i64, base, builder.CreateAdd(clamped, llvm::ConstantInt::get(i64, offset)));
    auto* loaded   = builder.CreateLoad(i64, ptr, "extract.load");
    return builder.CreateSelect(in_range, loaded, llvm::ConstantInt::get(i64, 0));
  };
  auto* low  = load_word(word);
  auto* high = load_word(builder.CreateAdd(word, llvm::ConstantInt::get(i64, 1)));
  auto* fshr = llvm::Intrinsic::getOrInsertDeclaration(impl_->module.get(), llvm::Intrinsic::fshr, {i64});
  auto* lane = builder.CreateCall(fshr, {high, low, shift}, "extract.lane");
  auto* bits = cast_integer(builder, lane, len, true);
  return impl_->remember(cast_integer(builder, bits, result_width, true), result_width, result_unsign);
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
    const unsigned     work_width               = op == Binary_op::shl ? result_width : std::max<unsigned>(lhs.width, result_width);
    // A wide variable barrel shifter expands into thousands of dependent
    // machine operations. Keep native code bounded by shifting packed words
    // in an LLVM loop instead; constant shifts retain their direct path.
    constexpr unsigned max_variable_shift_width = 4096;
    if (work_width > max_variable_shift_width && !llvm::isa<llvm::ConstantInt>(right)) {
      auto&      b               = impl_->builder;
      auto*      i64             = b.getInt64Ty();
      const auto source_words    = word_count(work_width);
      const auto output_words    = word_count(result_width);
      left                       = cast_integer(b, left, work_width, lhs.unsign);
      auto*             padded   = cast_integer(b, left, static_cast<unsigned>(source_words * 64), op != Binary_op::ashr);
      auto*             function = b.GetInsertBlock()->getParent();
      llvm::IRBuilder<> allocations(&function->getEntryBlock(), function->getEntryBlock().getFirstInsertionPt());
      auto*             source = allocations.CreateAlloca(i64, llvm::ConstantInt::get(i64, source_words), "shift.source");
      auto*             output = allocations.CreateAlloca(i64, llvm::ConstantInt::get(i64, output_words), "shift.output");
      source->setAlignment(llvm::Align(alignof(uint64_t)));
      output->setAlignment(llvm::Align(alignof(uint64_t)));
      impl_->store_packed(padded, source, 0, static_cast<uint32_t>(source_words * 64));
      const auto   compare_width = std::max<unsigned>(rhs.width, 32);
      auto*        count         = cast_integer(b, right, compare_width, true);
      auto*        oversized     = b.CreateICmpUGE(count, llvm::ConstantInt::get(count->getType(), work_width));
      auto*        amount        = cast_integer(b, right, 64, true);
      auto*        words         = b.CreateLShr(amount, llvm::ConstantInt::get(i64, 6));
      auto*        bits          = b.CreateAnd(amount, llvm::ConstantInt::get(i64, 63));
      llvm::Value* fill          = llvm::ConstantInt::get(i64, 0);
      if (op == Binary_op::ashr) {
        auto* sign = b.CreateTrunc(b.CreateLShr(left, llvm::ConstantInt::get(left->getType(), work_width - 1)), b.getInt1Ty());
        fill       = b.CreateSExt(sign, i64);
      }
      auto* entry = b.GetInsertBlock();
      auto* loop  = llvm::BasicBlock::Create(impl_->context, "shift.loop", function);
      auto* done  = llvm::BasicBlock::Create(impl_->context, "shift.done", function);
      b.CreateBr(loop);
      b.SetInsertPoint(loop);
      auto* index = b.CreatePHI(i64, 2, "shift.word");
      index->addIncoming(llvm::ConstantInt::get(i64, 0), entry);
      const auto load_word = [&](llvm::Value* position, llvm::Value* valid) {
        valid               = b.CreateAnd(valid, b.CreateNot(oversized));
        auto* safe_position = b.CreateSelect(valid, position, llvm::ConstantInt::get(i64, 0));
        auto* ptr           = b.CreateInBoundsGEP(i64, source, safe_position);
        auto* value         = b.CreateLoad(i64, ptr);
        return b.CreateSelect(valid, value, fill);
      };
      llvm::Value* shifted = nullptr;
      if (op == Binary_op::shl) {
        auto* position = b.CreateSub(index, words);
        auto* low      = load_word(position, b.CreateICmpUGE(index, words));
        auto* high     = load_word(b.CreateSub(position, llvm::ConstantInt::get(i64, 1)), b.CreateICmpUGT(index, words));
        auto* funnel   = llvm::Intrinsic::getOrInsertDeclaration(impl_->module.get(), llvm::Intrinsic::fshl, {i64});
        shifted        = b.CreateCall(funnel, {low, high, bits});
      } else {
        auto* position = b.CreateAdd(index, words);
        auto* next     = b.CreateAdd(position, llvm::ConstantInt::get(i64, 1));
        auto* low      = load_word(position, b.CreateICmpULT(position, llvm::ConstantInt::get(i64, source_words)));
        auto* high     = load_word(next, b.CreateICmpULT(next, llvm::ConstantInt::get(i64, source_words)));
        auto* funnel   = llvm::Intrinsic::getOrInsertDeclaration(impl_->module.get(), llvm::Intrinsic::fshr, {i64});
        shifted        = b.CreateCall(funnel, {high, low, bits});
      }
      b.CreateStore(shifted, b.CreateInBoundsGEP(i64, output, index));
      auto* next     = b.CreateAdd(index, llvm::ConstantInt::get(i64, 1));
      auto* backedge = b.CreateCondBr(b.CreateICmpULT(next, llvm::ConstantInt::get(i64, output_words)), loop, done);
      index->addIncoming(next, loop);
      auto* no_unroll = llvm::MDNode::get(impl_->context, {llvm::MDString::get(impl_->context, "llvm.loop.unroll.disable")});
      auto* loop_id   = llvm::MDNode::getDistinct(impl_->context, {nullptr, no_unroll});
      loop_id->replaceOperandWith(0, loop_id);
      backedge->setMetadata(llvm::LLVMContext::MD_loop, loop_id);
      b.SetInsertPoint(done);
      return impl_->remember(impl_->load_packed(output, 0, result_width, "shift.result"), result_width, result_unsign);
    }
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

  const bool comparison      = op == Binary_op::eq || op == Binary_op::ne || op == Binary_op::lt || op == Binary_op::le
                               || op == Binary_op::gt || op == Binary_op::ge;
  const bool ordered         = op == Binary_op::lt || op == Binary_op::le || op == Binary_op::gt || op == Binary_op::ge;
  // An ORDERED compare is sign-aware and a mixed-sign pair has no common
  // interpretation at max(width): the unsigned side's top bit would be read as
  // a sign. One extra bit gives each side room to extend by its OWN rule, which
  // is exactly the `cw += 1` the reference Slop lowering applies. EQ is a
  // bit-pattern compare and must NOT get the headroom.
  unsigned   operation_width = comparison ? std::max(lhs.width, rhs.width) : result_width;
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

      if (operation_width > 64) {
        // Native targets lower wide division through compiler-rt otherwise.
        // Restoring division keeps the entire operation inside this object.
        auto&      b           = impl_->builder;
        const bool signed_op   = !lhs.unsign || !rhs.unsign;
        auto*      negative_l  = signed_op ? b.CreateICmpSLT(left, zero) : b.getFalse();
        auto*      negative_r  = signed_op ? b.CreateICmpSLT(right, zero) : b.getFalse();
        auto*      numerator   = b.CreateSelect(negative_l, b.CreateNeg(left), left);
        auto*      denominator = b.CreateSelect(negative_r, b.CreateNeg(right), right);
        auto*      entry       = b.GetInsertBlock();
        auto*      loop        = llvm::BasicBlock::Create(impl_->context, "divide.bits", impl_->function);
        auto*      done        = llvm::BasicBlock::Create(impl_->context, "divide.done", impl_->function);
        b.CreateBr(loop);
        b.SetInsertPoint(loop);
        auto* count     = b.CreatePHI(b.getInt32Ty(), 2);
        auto* quotient  = b.CreatePHI(left->getType(), 2);
        auto* remainder = b.CreatePHI(left->getType(), 2);
        count->addIncoming(b.getInt32(operation_width), entry);
        quotient->addIncoming(zero, entry);
        remainder->addIncoming(zero, entry);
        auto* bit      = b.CreateSub(count, b.getInt32(1));
        auto* shift    = cast_integer(b, bit, operation_width, true);
        auto* one      = llvm::ConstantInt::get(left->getType(), 1);
        auto* carry    = b.CreateICmpSLT(remainder, zero);
        auto* shifted  = b.CreateOr(b.CreateShl(remainder, 1), b.CreateAnd(b.CreateLShr(numerator, shift), one));
        auto* take     = b.CreateOr(carry, b.CreateICmpUGE(shifted, denominator));
        auto* next_r   = b.CreateSelect(take, b.CreateSub(shifted, denominator), shifted);
        auto* next_q   = b.CreateOr(quotient, b.CreateSelect(take, b.CreateShl(one, shift), zero));
        auto* backedge = b.CreateCondBr(b.CreateICmpNE(bit, b.getInt32(0)), loop, done);
        count->addIncoming(bit, loop);
        quotient->addIncoming(next_q, loop);
        remainder->addIncoming(next_r, loop);
        auto* no_unroll = llvm::MDNode::get(impl_->context, {llvm::MDString::get(impl_->context, "llvm.loop.unroll.disable")});
        auto* loop_id   = llvm::MDNode::getDistinct(impl_->context, {nullptr, no_unroll});
        loop_id->replaceOperandWith(0, loop_id);
        backedge->setMetadata(llvm::LLVMContext::MD_loop, loop_id);
        b.SetInsertPoint(done);
        auto* magnitude = op == Binary_op::div ? next_q : next_r;
        auto* negative  = op == Binary_op::div ? b.CreateXor(negative_l, negative_r) : negative_l;
        result          = b.CreateSelect(negative, b.CreateNeg(magnitude), magnitude);
      } else if (lhs.unsign && rhs.unsign) {
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

Cgen_llvm::Value Cgen_llvm::hotmux(const std::vector<Value>& inputs, uint32_t result_width, bool result_unsign) {
  if (result_width == 0) {
    return {};
  }
  llvm::Value* result = llvm::ConstantInt::get(impl_->builder.getIntNTy(result_width), 0);
  if (inputs.size() % 2) {
    auto* fallback = impl_->get(inputs.back());
    if (fallback == nullptr) {
      return {};
    }
    result = cast_integer(impl_->builder, fallback, result_width, inputs.back().unsign);
  }
  llvm::Value*              seen    = llvm::ConstantInt::getFalse(impl_->context);
  llvm::Value*              overlap = llvm::ConstantInt::getFalse(impl_->context);
  std::vector<llvm::Value*> active;
  active.reserve(inputs.size() / 2);
  for (size_t i = 0; i + 1 < inputs.size(); i += 2) {
    auto* control = impl_->get(inputs[i]);
    if (control == nullptr) {
      return {};
    }
    auto* a = impl_->builder.CreateICmpNE(control, llvm::ConstantInt::get(control->getType(), 0));
    overlap = impl_->builder.CreateOr(overlap, impl_->builder.CreateAnd(seen, a));
    seen    = impl_->builder.CreateOr(seen, a);
    active.push_back(a);
  }
  // Fold from the LAST arm back so the FIRST active control wins -- the priority
  // cgen_verilog's `unique case` and the SMT encoders use. Exclusive controls
  // (the cell contract, enforced by the trap below) make the order unobservable;
  // matching it keeps a contract violation from reading differently per backend.
  for (size_t k = active.size(); k-- > 0;) {
    auto* value = impl_->get(inputs[2 * k + 1]);
    if (value == nullptr) {
      return {};
    }
    value  = cast_integer(impl_->builder, value, result_width, inputs[2 * k + 1].unsign);
    result = impl_->builder.CreateSelect(active[k], value, result);
  }
  impl_->trap_if(overlap, "hotmux.controls");
  return impl_->remember(result, result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::indexed_mux(Value select, const std::vector<Value>& arms, uint32_t result_width, bool result_unsign) {
  auto* selector = impl_->get(select);
  if (selector == nullptr || arms.empty() || result_width == 0) {
    return {};
  }
  const unsigned arm_index_bits = llvm::APInt(64, arms.size()).getActiveBits();
  if (arm_index_bits <= select.width) {
    auto* limit = llvm::ConstantInt::get(selector->getType(), arms.size());
    impl_->trap_if(impl_->builder.CreateICmpUGE(selector, limit), "mux.select");
  }
  llvm::Value* result = llvm::ConstantInt::get(impl_->builder.getIntNTy(result_width), 0);
  for (size_t i = arms.size(); i-- > 0;) {
    auto* arm = impl_->get(arms[i]);
    if (arm == nullptr) {
      return {};
    }
    arm            = cast_integer(impl_->builder, arm, result_width, arms[i].unsign);
    auto* selected = impl_->builder.CreateICmpEQ(selector, llvm::ConstantInt::get(selector->getType(), i));
    result         = impl_->builder.CreateSelect(selected, arm, result);
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

void Cgen_llvm::bind_memory(std::string_view symbol, const Memory& memory) { impl_->memories.emplace(symbol, memory); }

Cgen_llvm::Value Cgen_llvm::memory_read(std::string_view symbol, Value address, uint32_t result_width, bool result_unsign) {
  const auto it = impl_->memories.find(std::string(symbol));
  if (it == impl_->memories.end() || address.width == 0 || result_width == 0) {
    return {};
  }
  const auto& m     = it->second;
  auto&       b     = impl_->builder;
  auto*       valid = impl_->valid_address(address, m.size);
  auto*       index = cast_integer(b, impl_->get(address), 64, address.unsign);
  auto*       safe  = b.CreateSelect(valid, index, b.getInt64(0));
  auto*       ptr = b.CreateInBoundsGEP(b.getInt64Ty(), impl_->resource(m.data), b.CreateMul(safe, b.getInt64(word_count(m.bits))));
  llvm::Value* value = impl_->load_packed(ptr, 0, m.bits, "memory.read");
  for (uint32_t port = 0; port < std::max(m.forward, m.undefined); ++port) {
    auto* pending = impl_->pending_ptr(m, port);
    auto* active  = b.CreateICmpNE(b.CreateLoad(b.getInt8Ty(), impl_->fired_ptr(m, pending)), b.getInt8(0));
    auto* same    = b.CreateICmpEQ(index, b.CreateLoad(b.getInt64Ty(), impl_->word_ptr(pending, 3 * word_count(m.bits))));
    auto* data    = impl_->load_packed(pending, 0, m.bits, "memory.forward");
    auto* mask    = impl_->load_packed(pending, word_count(m.bits), m.bits, "memory.mask");
    auto* merged  = b.CreateOr(b.CreateAnd(value, b.CreateNot(mask)), b.CreateAnd(data, mask));
    if (port < m.undefined) {
      auto* old_block = b.GetInsertBlock();
      auto* collision = llvm::BasicBlock::Create(impl_->context, "memory.undefined", impl_->function);
      auto* done      = llvm::BasicBlock::Create(impl_->context, "memory.resolved", impl_->function);
      b.CreateCondBr(b.CreateAnd(valid, b.CreateAnd(active, same)), collision, done);
      b.SetInsertPoint(collision);
      auto* random      = impl_->random_value(m);
      auto* unspecified = b.CreateOr(b.CreateAnd(value, b.CreateNot(mask)), b.CreateAnd(random, mask));
      b.CreateBr(done);
      b.SetInsertPoint(done);
      auto* phi = b.CreatePHI(value->getType(), 2);
      phi->addIncoming(value, old_block);
      phi->addIncoming(unspecified, collision);
      value = phi;
    } else {
      value = b.CreateSelect(b.CreateAnd(active, same), merged, value);
    }
  }
  value = b.CreateSelect(valid, value, llvm::ConstantInt::get(value->getType(), 0));
  if (address.width > 64 || (address.unsign && address.width == 64)) {
    // The shared Memory contract treats values outside int64's range as
    // unknown addresses; an ordinary in-range but out-of-array index reads 0.
    auto*      raw   = impl_->get(address);
    const auto width = std::max(65u, address.width);
    raw              = cast_integer(b, raw, width, address.unsign);
    auto* known
        = address.unsign
              ? b.CreateICmpULE(raw, llvm::ConstantInt::get(raw->getType(), uint64_t{INT64_MAX}))
              : b.CreateAnd(b.CreateICmpSLE(raw, llvm::ConstantInt::get(raw->getType(), INT64_MAX, true)),
                            b.CreateICmpSGE(raw, llvm::ConstantInt::get(raw->getType(), static_cast<uint64_t>(INT64_MIN), true)));
    auto* prior   = b.GetInsertBlock();
    auto* unknown = llvm::BasicBlock::Create(impl_->context, "memory.unknown_address", impl_->function);
    auto* done    = llvm::BasicBlock::Create(impl_->context, "memory.address_resolved", impl_->function);
    b.CreateCondBr(known, done, unknown);
    b.SetInsertPoint(unknown);
    auto* random = impl_->random_value(m);
    b.CreateBr(done);
    b.SetInsertPoint(done);
    auto* phi = b.CreatePHI(value->getType(), 2);
    phi->addIncoming(value, prior);
    phi->addIncoming(random, unknown);
    value = phi;
  }
  return impl_->remember(cast_integer(b, value, result_width, result_unsign), result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::memory_read_all(std::string_view symbol, uint32_t result_width, bool result_unsign) {
  const auto it = impl_->memories.find(std::string(symbol));
  if (it == impl_->memories.end() || result_width == 0) {
    return {};
  }
  const auto& m = it->second;
  if (m.commit_before_read) {
    impl_->commit_memory(m);
  }
  auto& b    = impl_->builder;
  auto* data = impl_->resource(m.data);
  if (m.packed_value) {
    return impl_->remember(impl_->load_packed(data, 0, result_width, "resource.value"), result_width, result_unsign);
  }
  llvm::Value* result = llvm::ConstantInt::get(b.getIntNTy(result_width), 0);
  for (uint64_t i = 0; i < m.size; ++i) {
    auto* entry = impl_->load_packed(data, i * word_count(m.bits), m.bits, "memory.entry");
    entry       = cast_integer(b, entry, result_width, true);
    result      = b.CreateOr(result, b.CreateShl(entry, llvm::ConstantInt::get(entry->getType(), i * m.bits)));
  }
  return impl_->remember(result, result_width, result_unsign);
}

bool Cgen_llvm::memory_apply(std::string_view symbol, Value data) {
  const auto it = impl_->memories.find(std::string(symbol));
  if (it == impl_->memories.end() || data.width == 0) {
    return false;
  }
  const auto& m     = it->second;
  auto&       b     = impl_->builder;
  auto*       value = impl_->get(data);
  auto*       ptr   = impl_->resource(m.data);
  if (m.packed_value) {
    impl_->store_packed(value, ptr, 0, data.width);
    return true;
  }
  for (uint64_t i = 0; i < m.size; ++i) {
    auto* entry = b.CreateLShr(value, llvm::ConstantInt::get(value->getType(), i * m.bits));
    impl_->store_packed(cast_integer(b, entry, m.bits, true), ptr, i * word_count(m.bits), m.bits);
  }
  return true;
}

bool Cgen_llvm::memory_clear(std::string_view symbol) {
  const auto it = impl_->memories.find(std::string(symbol));
  if (it == impl_->memories.end()) {
    return false;
  }
  const auto& m = it->second;
  for (uint32_t port = 0; port < m.writes; ++port) {
    impl_->builder.CreateStore(impl_->builder.getInt8(0), impl_->fired_ptr(m, impl_->pending_ptr(m, port)));
  }
  return true;
}

bool Cgen_llvm::memory_stage_whole(std::string_view symbol, Value enable, Value force, Value data) {
  const auto it = impl_->memories.find(std::string(symbol));
  if (it == impl_->memories.end() || !memory_apply(symbol, data)) {
    return false;
  }
  const auto& m      = it->second;
  auto&       b      = impl_->builder;
  auto*       wen    = impl_->get(enable);
  auto*       rst    = impl_->get(force);
  auto*       active = b.CreateICmpNE(wen, llvm::ConstantInt::get(wen->getType(), 0));
  if (m.gated) {
    active = b.CreateAnd(active, b.CreateICmpNE(b.CreateLoad(b.getInt64Ty(), impl_->resource(m.gate)), b.getInt64(0)));
  }
  active = b.CreateOr(active, b.CreateICmpNE(rst, llvm::ConstantInt::get(rst->getType(), 0)));
  b.CreateStore(b.CreateZExt(active, b.getInt64Ty()), impl_->resource(m.pending));
  return true;
}

bool Cgen_llvm::memory_stage_write(std::string_view symbol, Value enable, Value address, Value data) {
  const auto it = impl_->memories.find(std::string(symbol));
  if (it == impl_->memories.end() || data.width == 0 || enable.width == 0 || address.width == 0) {
    return false;
  }
  const auto&  m         = it->second;
  auto&        b         = impl_->builder;
  auto*        pending   = impl_->pending_ptr(m, m.port);
  auto*        wen       = impl_->get(enable);
  auto*        mask_type = b.getIntNTy(m.bits);
  llvm::Value* mask      = llvm::ConstantInt::get(mask_type, 0);
  for (uint32_t lane = 0; lane < m.lanes; ++lane) {
    llvm::Value* active;
    if (m.lanes == 1) {
      active = b.CreateICmpNE(wen, llvm::ConstantInt::get(wen->getType(), 0));
    } else if (lane >= enable.width) {
      active = enable.unsign ? b.getFalse() : b.CreateICmpSLT(wen, llvm::ConstantInt::get(wen->getType(), 0));
    } else {
      active = b.CreateTrunc(b.CreateLShr(wen, llvm::ConstantInt::get(wen->getType(), lane)), b.getInt1Ty());
    }
    auto lane_mask = llvm::APInt::getBitsSet(m.bits, lane * (m.bits / m.lanes), (lane + 1) * (m.bits / m.lanes));
    mask = b.CreateOr(mask,
                      b.CreateSelect(active, llvm::ConstantInt::get(mask_type, lane_mask), llvm::ConstantInt::get(mask_type, 0)));
  }
  auto* valid = b.CreateAnd(impl_->valid_address(address, m.size), b.CreateICmpNE(mask, llvm::ConstantInt::get(mask_type, 0)));
  if (m.gated) {
    valid = b.CreateAnd(valid, b.CreateICmpNE(b.CreateLoad(b.getInt64Ty(), impl_->resource(m.gate)), b.getInt64(0)));
  }
  impl_->store_packed(impl_->get(data), pending, 0, m.bits);
  impl_->store_packed(mask, pending, word_count(m.bits), m.bits);
  impl_->store_packed(llvm::ConstantInt::get(mask_type, 0), pending, 2 * word_count(m.bits), m.bits);
  b.CreateStore(cast_integer(b, impl_->get(address), 64, address.unsign), impl_->word_ptr(pending, 3 * word_count(m.bits)));
  b.CreateStore(b.CreateZExt(valid, b.getInt8Ty()), impl_->fired_ptr(m, pending));
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

bool Cgen_llvm::write_object(std::string_view path, std::string& error, bool track_changed) {
  return write_module(path, error, track_changed, true);
}
bool Cgen_llvm::write_bitcode(std::string_view path, std::string& error, bool track_changed) {
  return write_module(path, error, track_changed, false);
}
bool Cgen_llvm::write_module(std::string_view path, std::string& error, bool track_changed, bool native) {
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
  if (impl_->scalar_abi) {
    if (impl_->output_values.size() != 1 || impl_->output_values.front().value.width > 64) {
      error = "scalar LLVM ABI requires exactly one output no wider than 64 bits";
      return false;
    }
    impl_->builder.CreateRet(cast_integer(impl_->builder, impl_->get(impl_->output_values.front().value), 64, true));
  } else {
    auto*        i64           = impl_->builder.getInt64Ty();
    const size_t changed_words = std::max<size_t>(1, word_count(static_cast<uint32_t>(impl_->output_values.size())));
    if (track_changed) {
      for (size_t word = 0; word < changed_words; ++word) {
        auto* ptr = impl_->builder.CreateConstInBoundsGEP1_64(i64, impl_->changed, word);
        impl_->builder.CreateStore(llvm::ConstantInt::get(i64, 0), ptr);
      }
    }
    size_t output_word_offset = 0;
    for (const auto& output : impl_->output_values) {
      auto* value = impl_->get(output.value);
      if (track_changed) {
        auto*        old  = impl_->load_packed(impl_->outputs, output_word_offset, output.value.width, "out.old");
        auto*        diff = impl_->builder.CreateICmpNE(old, value);
        auto*        ptr  = impl_->builder.CreateConstInBoundsGEP1_64(i64, impl_->changed, output.index / 64);
        llvm::Value* bits = impl_->builder.CreateLoad(i64, ptr, "changed.old");
        auto*        flag = llvm::ConstantInt::get(i64, uint64_t{1} << (output.index % 64));
        bits              = impl_->builder.CreateOr(bits, impl_->builder.CreateSelect(diff, flag, llvm::ConstantInt::get(i64, 0)));
        impl_->builder.CreateStore(bits, ptr);
      }
      impl_->store_packed(value, impl_->outputs, output_word_offset, output.value.width);
      output_word_offset += word_count(output.value.width);
    }
    impl_->builder.CreateRetVoid();
  }

  if (llvm::verifyModule(*impl_->module, &llvm::errs())) {
    error = "LLVM rejected the generated simulator module";
    return false;
  }

  static std::once_flag initialize_target;
  std::call_once(initialize_target, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
  });

  const llvm::Triple  triple(llvm::sys::getDefaultTargetTriple());
  std::string         lookup_error;
  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookup_error);
  if (target == nullptr) {
    error = lookup_error;
    return false;
  }
  llvm::TargetOptions                  options;
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
  // color spent more than 14 minutes in GVN alone. These kernels contain only bounded packed-word loops
  // and no internal calls, so one pass each of stack promotion, local CSE,
  // bit folding, CFG cleanup, and dead-code removal captures the useful
  // simplifications without the inliner/GVN compile-time cliff.
  llvm::LoopAnalysisManager     loop_analyses;
  llvm::FunctionAnalysisManager function_analyses;
  llvm::CGSCCAnalysisManager    cgscc_analyses;
  llvm::ModuleAnalysisManager   module_analyses;
  llvm::PipelineTuningOptions   tuning;
  tuning.LoopUnrolling = false;
  llvm::PassBuilder pass_builder(machine.get(), tuning);
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

  // Legalizing an oversized integer produces thousands of native operations.
  // Keep such kernels in separate functions so AlwaysInliner cannot combine
  // many wide packed-array operations into one enormous scheduling region.
  const auto wide_integer = [](llvm::Type* type) { return type->isIntegerTy() && type->getIntegerBitWidth() > 4096; };
  bool       wide_kernel  = false;
  for (auto& block : *impl_->function) {
    for (auto& instruction : block) {
      wide_kernel = wide_integer(instruction.getType());
      for (const auto& operand : instruction.operands()) {
        wide_kernel |= wide_integer(operand->getType());
      }
      if (wide_kernel) {
        break;
      }
    }
    if (wide_kernel) {
      break;
    }
  }
  if (wide_kernel) {
    impl_->function->removeFnAttr(llvm::Attribute::AlwaysInline);
    impl_->function->addFnAttr(llvm::Attribute::NoInline);
  }

  // Emit into MEMORY, then hand the bytes to File_output rather than writing
  // the file directly. The object is a link input keyed on mtime by the
  // generated build.ninja, so an unconditional truncate+rewrite forced a relink
  // on every single `lhd sim` — including one where nothing changed at all and
  // the object came out byte-identical. Every other generated artifact in the
  // sim tree already goes through the same write-if-different path; this one
  // was the lone hole, and it is why the LLVM backend could never reach a
  // no-work warm rebuild.
  //
  // Buffering one color object also lets us validate its external dependencies
  // before publishing it as a usable build input.
  llvm::SmallVector<char, 0> object_buffer;
  llvm::raw_svector_ostream  object(object_buffer);
  if (native) {
    // Expand memory intrinsics before instruction selection can turn a large
    // packed copy/clear into a call to libc.
    std::vector<llvm::MemIntrinsic*> memory_ops;
    for (auto& block : *impl_->function) {
      for (auto& instruction : block) {
        if (auto* memory = llvm::dyn_cast<llvm::MemIntrinsic>(&instruction)) {
          memory_ops.push_back(memory);
        }
      }
    }
    const auto& tti = function_analyses.getResult<llvm::TargetIRAnalysis>(*impl_->function);
    for (auto* memory : memory_ops) {
      if (auto* copy = llvm::dyn_cast<llvm::MemCpyInst>(memory)) {
        llvm::expandMemCpyAsLoop(copy, tti);
      } else if (auto* clear = llvm::dyn_cast<llvm::MemSetInst>(memory)) {
        llvm::expandMemSetAsLoop(clear);
      } else if (auto* move = llvm::dyn_cast<llvm::MemMoveInst>(memory)) {
        if (!llvm::expandMemMoveAsLoop(move, tti)) {
          error = "cannot inline memory move";
          return false;
        }
      } else {
        error = "unsupported memory intrinsic";
        return false;
      }
      memory->eraseFromParent();
    }
    llvm::legacy::PassManager emit;
    if (machine->addPassesToEmitFile(emit, object, nullptr, llvm::CodeGenFileType::ObjectFile)) {
      error = "LLVM target cannot emit native color objects";
      return false;
    }
    emit.run(*impl_->module);
    auto parsed = llvm::object::ObjectFile::createObjectFile(
        llvm::MemoryBufferRef(llvm::StringRef(object_buffer.data(), object_buffer.size()), "color.o"));
    if (!parsed) {
      error = llvm::toString(parsed.takeError());
      return false;
    }
    for (const auto& symbol : (*parsed)->symbols()) {
      auto flags = symbol.getFlags();
      if (!flags) {
        error = llvm::toString(flags.takeError());
        return false;
      }
      if ((*flags & llvm::object::BasicSymbolRef::SF_Undefined) == 0) {
        continue;
      }
      auto name = symbol.getName();
      if (!name) {
        error = llvm::toString(name.takeError());
        return false;
      }
      // ELF's mandatory null symbol has no dependency. Every named undefined
      // symbol, including a compiler-generated libcall, violates the ABI.
      if (!name->empty()) {
        error = "native LLVM color requires external symbol " + name->str();
        return false;
      }
    }

  } else {
    llvm::WriteBitcodeToFile(*impl_->module, object);
  }
  {
    File_output out{path};
    out.append(std::string_view(object_buffer.data(), object_buffer.size()));
  }
  return true;
}

bool Cgen_llvm::link_bitcode_object(std::string_view host_path, const std::vector<std::string>& kernel_paths,
                                    std::string_view object_path, std::string& error) {
  llvm::LLVMContext context;
  const auto        read_module = [&](std::string_view path) -> std::unique_ptr<llvm::Module> {
    auto buffer = llvm::MemoryBuffer::getFile(path);
    if (!buffer) {
      error = std::string(path) + ": " + buffer.getError().message();
      return nullptr;
    }
    auto parsed = llvm::parseBitcodeFile((*buffer)->getMemBufferRef(), context);
    if (!parsed) {
      error = std::string(path) + ": " + llvm::toString(parsed.takeError());
      return nullptr;
    }
    return std::move(*parsed);
  };

  auto module = read_module(host_path);
  if (!module) {
    return false;
  }
  llvm::Linker linker(*module);
  for (const auto& path : kernel_paths) {
    auto kernel = read_module(path);
    if (!kernel) {
      return false;
    }
    kernel->setTargetTriple(module->getTargetTriple());
    kernel->setDataLayout(module->getDataLayout());
    if (linker.linkInModule(std::move(kernel))) {
      error = "LLVM failed to link color bitcode " + path;
      return false;
    }
  }

  // The host bitcode was produced by the HOST compiler, and lhd -- not that
  // compiler -- now lowers it. A frontend may therefore have requested a stack
  // probe this LLVM's backend cannot emit: Apple clang stamps
  // `"probe-stack"="__chkstk_darwin"` on every function with a large frame (the
  // generated evaluator's Slop locals reach that easily), and
  // AArch64FunctionInfo hard-fails with `report_fatal_error("Unsupported stack
  // probing method")` on any value but "inline-asm". That is an abort inside
  // llvm_sim_link, not a diagnosable error, so it must be normalized here.
  //
  // REWRITTEN, not dropped: the probe exists so a frame larger than a guard page
  // touches every page on the way down. LLVM's own "inline-asm" probing does the
  // same job with code this backend can emit, so protection is preserved.
  const auto normalize_stack_probe = [](llvm::Module& m) {
    if (const auto* flag = llvm::dyn_cast_or_null<llvm::MDString>(m.getModuleFlag("probe-stack"));
        flag != nullptr && flag->getString() != "inline-asm") {
      m.setModuleFlag(llvm::Module::Override, "probe-stack", llvm::MDString::get(m.getContext(), "inline-asm"));
    }
    for (auto& function : m.functions()) {
      if (function.hasFnAttribute("probe-stack") && function.getFnAttribute("probe-stack").getValueAsString() != "inline-asm") {
        function.addFnAttr("probe-stack", "inline-asm");  // replaces the existing string attribute
      }
    }
  };
  normalize_stack_probe(*module);

  static std::once_flag initialize_target;
  std::call_once(initialize_target, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
  });
  llvm::Triple triple(module->getTargetTriple());
  if (triple.getTriple().empty()) {
    triple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
    module->setTargetTriple(triple);
  }
  std::string         lookup_error;
  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookup_error);
  if (target == nullptr) {
    error = lookup_error;
    return false;
  }
  llvm::TargetOptions                  options;
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(triple,
                                                                           "generic",
                                                                           "",
                                                                           options,
                                                                           llvm::Reloc::PIC_,
                                                                           std::nullopt,
                                                                           llvm::CodeGenOptLevel::Aggressive));
  if (!machine) {
    error = "LLVM could not create a native target machine for linked simulator bitcode";
    return false;
  }
  module->setDataLayout(machine->createDataLayout());

  // Which kernels does THIS module actually call? Recorded BEFORE the inliner
  // runs, because afterwards every in-module call site is gone and use_empty()
  // can no longer tell "inlined away here" from "called from another TU".
  //
  // cgen_sim SHARDS the evaluator above ~16k version sites
  // (direct_color_eval_shards) and emits those color bodies -- the inlined LLVM
  // ABI call included -- into `<mod>.color-eval-<n>.cpp`, which the host build
  // compiles to an ORDINARY object. The kernel bitcode is still grouped with
  // `<mod>.cpp`, so for a sharded color this module holds the DEFINITION and
  // never the call. Internalizing every kernel regardless, and letting GlobalDCE
  // reclaim what the inliner emptied, deleted the definition the shard object
  // still names -- an undefined-symbol link failure that appears only on a
  // design big enough to shard.
  llvm::StringSet<> locally_called;
  for (auto& function : module->functions()) {
    if (!function.isDeclaration() && function.getName().starts_with("__lhd_color_kernel_") && !function.use_empty()) {
      locally_called.insert(function.getName());
    }
  }

  llvm::LoopAnalysisManager     loop_analyses;
  llvm::FunctionAnalysisManager function_analyses;
  llvm::CGSCCAnalysisManager    cgscc_analyses;
  llvm::ModuleAnalysisManager   module_analyses;
  llvm::PipelineTuningOptions   tuning;
  tuning.LoopUnrolling = false;
  llvm::PassBuilder pass_builder(machine.get(), tuning);
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
  pipeline.addPass(llvm::AlwaysInlinerPass());
  pipeline.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functions)));
  pipeline.run(*module, module_analyses);

  for (auto& function : module->functions()) {
    if (!function.isDeclaration() && locally_called.contains(function.getName())) {
      function.setLinkage(llvm::GlobalValue::InternalLinkage);  // inlined here; GlobalDCE below reclaims it
    }
  }
  llvm::ModulePassManager cleanup;
  cleanup.addPass(llvm::GlobalDCEPass());
  cleanup.run(*module, module_analyses);

  llvm::SmallVector<char, 0> native_buffer;
  llvm::raw_svector_ostream  native(native_buffer);
  llvm::legacy::PassManager  emit;
  if (machine->addPassesToEmitFile(emit, native, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    error = "LLVM target cannot emit linked simulator objects";
    return false;
  }
  emit.run(*module);
  File_output out{object_path};
  out.append(std::string_view(native_buffer.data(), native_buffer.size()));
  return true;
}
