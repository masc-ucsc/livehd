// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_llvm.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "cgen_salt.hpp"
#include "debug_env.hpp"
#include "file_output.hpp"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/Scalar/Sink.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"
#include "sim_llvm_target.hpp"

namespace {

// LHD_LLVM_TIME_PASSES enables pass/analysis timers for the embedded LLVM.
// LHD_LLVM_TIME_PASSES_DIR optionally saves one report per kernel or host
// module; this survives lhd discarding successful host-build stderr.
class Llvm_pass_profile {
  bool                                  enabled_ = livehd::debug::env<"LHD_LLVM_TIME_PASSES">() != nullptr;
  std::string_view                      stage_;
  std::string_view                      path_;
  llvm::PassInstrumentationCallbacks    callbacks_;
  llvm::TimePassesHandler               timers_{enabled_};
  std::unique_ptr<llvm::raw_fd_ostream> report_;
  using Clock = std::chrono::steady_clock;
  Clock::time_point                                started_;
  Clock::time_point                                marked_;
  std::vector<std::pair<std::string_view, double>> phases_;

public:
  Llvm_pass_profile(std::string_view stage, std::string_view path) : stage_(stage), path_(path) {
    if (enabled_) {
      started_ = marked_        = Clock::now();
      llvm::TimePassesIsEnabled = true;
      llvm::EnableStatistics();
      if (const char* directory = livehd::debug::env<"LHD_LLVM_TIME_PASSES_DIR">()) {
        const auto      filename = std::filesystem::path(directory)
                                   / (std::filesystem::path(path).filename().string() + "." + std::string(stage) + ".log");
        std::error_code ec;
        report_ = std::make_unique<llvm::raw_fd_ostream>(filename.string(), ec);
        if (ec) {
          llvm::errs() << "LLVM timing report: " << ec.message() << '\n';
          report_.reset();
        }
      }
      timers_.setOutStream(report_ ? *report_ : llvm::errs());
      timers_.registerCallbacks(callbacks_);
    }
  }

  ~Llvm_pass_profile() {
    if (enabled_) {
      auto& out = report_ ? *report_ : llvm::errs();
      out << "LHD LLVM TIMING " << stage_ << " " << path_ << '\n';
      out << "LHD LLVM ELAPSED " << std::chrono::duration<double>(Clock::now() - started_).count() << '\n';
      for (const auto& [name, seconds] : phases_) {
        out << "LHD LLVM PHASE " << name << " " << seconds << '\n';
      }
      timers_.print();
      llvm::reportAndResetTimings(&out);
    }
  }

  llvm::PassInstrumentationCallbacks* callbacks() { return enabled_ ? &callbacks_ : nullptr; }

  void mark(std::string_view name) {
    if (enabled_) {
      const auto now = Clock::now();
      phases_.emplace_back(name, std::chrono::duration<double>(now - marked_).count());
      marked_ = now;
    }
  }
};

// Recover a slice only through operations that preserve every requested bit.
// The same representation lets concat join adjacent lanes and lets scalar mux
// instances share one word select after hierarchy has been expanded.
struct Integer_slice {
  llvm::Value* source;
  uint32_t     lo;
  uint32_t     width;
};

Integer_slice integer_slice(llvm::Value* source, uint32_t width) {
  uint32_t lo = 0;
  // Only peel operations that preserve the requested bits. In particular,
  // do not turn an extension's high zero/sign bits into bits of its source.
  while (true) {
    // Shift lowering retains an explicit oversized-count select until the
    // optimization pipeline, even when its predicate is already constant.
    if (auto* select = llvm::dyn_cast<llvm::SelectInst>(source)) {
      if (auto* condition = llvm::dyn_cast<llvm::ConstantInt>(select->getCondition())) {
        source = condition->isZero() ? select->getFalseValue() : select->getTrueValue();
        continue;
      }
    }
    if (auto* cast = llvm::dyn_cast<llvm::CastInst>(source)) {
      if ((cast->getOpcode() == llvm::Instruction::Trunc || cast->getOpcode() == llvm::Instruction::ZExt
           || cast->getOpcode() == llvm::Instruction::SExt)
          && uint64_t{lo} + width <= cast->getOperand(0)->getType()->getIntegerBitWidth()) {
        source = cast->getOperand(0);
        continue;
      }
    }
    if (auto* shift = llvm::dyn_cast<llvm::BinaryOperator>(source); shift && shift->getOpcode() == llvm::Instruction::LShr) {
      if (auto* amount = llvm::dyn_cast<llvm::ConstantInt>(shift->getOperand(1))) {
        const auto bits = amount->getValue().getLimitedValue();
        if (bits < source->getType()->getIntegerBitWidth()
            && uint64_t{lo} + width + bits <= source->getType()->getIntegerBitWidth()) {
          lo     += static_cast<uint32_t>(bits);
          source  = shift->getOperand(0);
          continue;
        }
      }
    }
    break;
  }
  return {source, lo, width};
}

constexpr size_t word_count(uint32_t width) { return (static_cast<size_t>(width) + 63) / 64; }

std::vector<uint32_t> widths_of(const std::vector<std::pair<uint32_t, bool>>& types) {
  std::vector<uint32_t> widths;
  widths.reserve(types.size());
  for (const auto& [width, unsign] : types) {
    (void)unsign;
    widths.push_back(width);
  }
  return widths;
}

// Native full unroll of an embedded rolled loop (Cgen_llvm::inline_loop).
// Rolled, a per-bit loop pays a variable shift, mask and insert per lane every
// cycle (br_flow_arb_rr: ~900 native instructions/cycle against ~400 for the
// unrolled Verilog), and LLVM may vectorize it over a stack copy of a wide
// operand (store-to-load forwarding stalls). Unrolled, every lane index is a
// constant and the body folds to the fixed-position logic the equivalent
// unrolled Verilog produces. The loop stays a loop in the graph and in the
// reference (Slop) code; only the native kernel is unrolled, and only while
// the replicated body stays within this many compute instructions (see
// unroll_cost; lhdtrack br_arb_lru's 16x16 nest needs ~15k and runs 3x faster
// unrolled, larger loops such as XiangShan's 520-lane ones stay rolled).
constexpr uint64_t            kUnrollBudget       = 16384;
constexpr uint64_t            kUnrollMaxTrips     = 256;
// All unrolls of one kernel together add at most this many compute
// instructions, so a color holding many loop sites cannot grow into one
// enormous function.
constexpr uint64_t            kUnrollKernelBudget = 32768;
// Instructions that the full unrolls pending in a function still add: a
// sealed body carries it in its bitcode, so an enclosing loop sizes its own
// unroll with the inner loops already expanded.
constexpr llvm::StringLiteral kUnrolledExtra      = "lhd-unrolled-extra";

// The instructions a trip of `function` costs once inlined into a loop: its
// arithmetic, compares, selects and control. Loads, stores and address
// arithmetic are the sealed body's packed-word ABI, which inlining and SROA
// remove (a 512-bit operand alone is a dozen of them), and casts are free.
uint64_t unroll_cost(const llvm::Function& function) {
  uint64_t cost = 0;
  for (const auto& block : function) {
    for (const auto& instruction : block) {
      if (!llvm::isa<llvm::LoadInst>(instruction) && !llvm::isa<llvm::StoreInst>(instruction)
          && !llvm::isa<llvm::GetElementPtrInst>(instruction) && !llvm::isa<llvm::CastInst>(instruction)
          && !llvm::isa<llvm::FreezeInst>(instruction)) {
        ++cost;
      }
    }
  }
  return cost;
}

uint64_t unrolled_extra(const llvm::Function& function) {
  uint64_t   extra = 0;
  const auto attr  = function.getFnAttribute(kUnrolledExtra);
  if (!attr.isStringAttribute() || attr.getValueAsString().getAsInteger(10, extra)) {
    return 0;
  }
  return extra;
}

llvm::Value* cast_integer(llvm::IRBuilder<>& builder, llvm::Value* value, unsigned width, bool source_unsigned) {
  const unsigned source_width = value->getType()->getIntegerBitWidth();
  if (source_width == width) {
    return value;
  }
  auto* type = builder.getIntNTy(width);
  if (source_width > width) {
    return builder.CreateTrunc(value, type);
  }
  // LLVM's DAGCombiner::CombineZExtLogicopShiftLoad rewrites zext(and/or(shift(load, c), k)) by
  // reusing the narrow shift's amount operand, which is i8 on x86. A destination wider than 256
  // bits needs a 9+ bit amount, so assertion-enabled LLVM (-c dbg) aborts with "Invalid use of
  // small shift amount with oversized value". Freezing the non-constant operand hides the
  // logic-op/shift/load shape from that combine; the value is a defined memory word, so freeze
  // is an identity here.
  if (width > 256 && !llvm::isa<llvm::Constant>(value)) {
    value = builder.CreateFreeze(value);
  }
  return source_unsigned ? builder.CreateZExt(value, type) : builder.CreateSExt(value, type);
}

// Conditional regions (README "Conditional regions in LLVM kernels"). A color
// kernel is fully if-converted: every Mux is a `select`, so both arms of every
// `if`/`case` in the design are computed every cycle (picorv32's largest
// kernel: 841 selects, no branch; Verilator runs only the taken path). This
// rebuilds the branches after optimization:
//
//  * an instruction belongs to arm (group, side) when every user is a select
//    of that group with the instruction on that side, or another instruction
//    of the same arm -- its whole value is dead when the other side is taken;
//  * a group is selects on the SAME condition value (GVN already merged
//    equal conditions), so several outputs updated under one `if` share ONE
//    branch (fusion), as long as no user of an earlier select of the group
//    precedes its last select;
//  * a group whose arms hold at least `min_cost` instructions of real work
//    (selects, casts and freezes are free) becomes if/else blocks holding
//    the arms, and its selects become phis.
//
// Only side-effect-free, non-memory instructions move: a load or call stays
// where it is (sinking a load past a store could change the value it reads).
// The new blocks are processed again, so an `elif` chain becomes nested
// branches. Returns the number of regions made.
// Opt-in (LHD_SIM_LLVM_SHRINK=1): measured neutral on AArch64, whose Hybrid
// DAG scheduler and pressure-tracking machine scheduler reorder each block
// anyway (picorv32: 2160 vs 2156 spill accesses, same time).
// Rematerialization distance (instructions); 0 disables. Opt-in:
// LHD_SIM_LLVM_REMAT_DIST=<n> (32 is a good value). Measured neutral on
// picorv32: it traded spill reloads (928 -> 569) for owner-table reloads
// (97 -> 454) at the same time.
unsigned remat_distance() {
  const char* dist = livehd::debug::env<"LHD_SIM_LLVM_REMAT_DIST">();
  return dist == nullptr ? 0u : static_cast<unsigned>(std::strtoul(dist, nullptr, 0));
}

// Rematerialize instead of spill (README "Live ranges in LLVM kernels"). A
// value computed only from invariant loads (an object this kernel never
// writes, the owner table, the packed inputs) through a few cheap operations
// can be recomputed anywhere; keeping one copy live across a large kernel is
// what spilled (picorv32: 306 stack slots). A use more than `distance`
// instructions after the definition (or in another block) gets its own copy
// of the chain right before it; nearby uses share that copy.
unsigned rematerialize_invariant(llvm::Function& function, unsigned distance) {
  constexpr unsigned                               kMaxChain = 8;
  std::unordered_map<const llvm::Value*, unsigned> chain;  // instructions to rebuild; absent = not rematerializable
  const auto                                       cheap = [](const llvm::Instruction& inst) {
    if (llvm::isa<llvm::CastInst>(inst) || llvm::isa<llvm::GetElementPtrInst>(inst) || llvm::isa<llvm::FreezeInst>(inst)) {
      return true;
    }
    if (const auto* bin = llvm::dyn_cast<llvm::BinaryOperator>(&inst)) {
      switch (bin->getOpcode()) {
        case llvm::Instruction::And :
        case llvm::Instruction::Or  :
        case llvm::Instruction::Xor :
        case llvm::Instruction::Shl :
        case llvm::Instruction::LShr:
        case llvm::Instruction::AShr:
        case llvm::Instruction::Add :
        case llvm::Instruction::Sub : return true;
        default                     : return false;
      }
    }
    return llvm::isa<llvm::ICmpInst>(inst);
  };
  const auto remat_cost = [&](const llvm::Value* value) -> std::optional<unsigned> {
    if (llvm::isa<llvm::Constant>(value) || llvm::isa<llvm::Argument>(value)) {
      return 0u;
    }
    const auto it = chain.find(value);
    return it == chain.end() ? std::nullopt : std::optional<unsigned>(it->second);
  };
  // Classify in program order (definitions precede uses within a block, and
  // blocks are visited in layout order, which dominance follows here).
  for (auto& block : function) {
    for (auto& inst : block) {
      const auto* load = llvm::dyn_cast<llvm::LoadInst>(&inst);
      const bool  root = load != nullptr && load->hasMetadata(llvm::LLVMContext::MD_invariant_load) && !load->isVolatile();
      if (!root && !cheap(inst)) {
        continue;
      }
      unsigned total = 1;
      bool     ok    = true;
      for (const auto& operand : inst.operands()) {
        const auto cost  = remat_cost(operand.get());
        ok               = ok && cost.has_value();
        total           += cost.value_or(0);
      }
      if (ok && total <= kMaxChain) {
        chain[&inst] = total;
      }
    }
  }
  unsigned cloned = 0;
  for (auto& block : function) {
    std::unordered_map<const llvm::Instruction*, size_t> position;
    size_t                                               pos = 0;
    for (auto& inst : block) {
      position[&inst] = pos++;
    }
    // Per original value: the latest copy made in this block, and where.
    std::unordered_map<const llvm::Value*, std::pair<llvm::Value*, size_t>> copies;
    std::function<llvm::Value*(llvm::Value*, llvm::Instruction*, size_t)>   rebuild
        = [&](llvm::Value* value, llvm::Instruction* before, size_t at) -> llvm::Value* {
      auto* inst = llvm::dyn_cast<llvm::Instruction>(value);
      if (inst == nullptr || !chain.contains(inst)) {
        return value;
      }
      if (inst->getParent() == &block && at >= position[inst] && at - position[inst] <= distance) {
        return value;  // the original is close enough
      }
      if (const auto it = copies.find(inst); it != copies.end() && at - it->second.second <= distance) {
        return it->second.first;
      }
      auto* copy = inst->clone();
      for (unsigned i = 0; i < copy->getNumOperands(); ++i) {
        copy->setOperand(i, rebuild(copy->getOperand(i), before, at));
      }
      copy->insertBefore(before->getIterator());
      copies[inst] = {copy, at};
      ++cloned;
      return copy;
    };
    std::vector<llvm::Instruction*> users;
    for (auto& inst : block) {
      users.push_back(&inst);
    }
    for (auto* user : users) {
      if (llvm::isa<llvm::PHINode>(user)) {
        continue;
      }
      const size_t at = position[user];
      for (unsigned i = 0; i < user->getNumOperands(); ++i) {
        auto* operand = llvm::dyn_cast<llvm::Instruction>(user->getOperand(i));
        if (operand == nullptr || !chain.contains(operand) || chain.contains(user)) {
          continue;  // rebuild chains at their consumers, not inside other chains
        }
        if (operand->getParent() == &block && at - position[operand] <= distance) {
          continue;
        }
        user->setOperand(i, rebuild(operand, user, at));
      }
    }
  }
  // Originals whose every use moved to a copy.
  for (bool changed = true; changed;) {
    changed = false;
    for (auto& block : function) {
      for (auto it = block.begin(); it != block.end();) {
        auto& inst = *it++;
        if (chain.contains(&inst) && inst.use_empty() && !inst.isVolatile()) {
          chain.erase(&inst);
          inst.eraseFromParent();
          changed = true;
        }
      }
    }
  }
  return cloned;
}

bool shrink_enabled() { return livehd::debug::env<"LHD_SIM_LLVM_SHRINK">() != nullptr; }

// Debug only: LHD_SIM_LLVM_ARGS="-opt1 -opt2" reaches LLVM's command-line
// options once per process (scheduler/register-allocation experiments).
std::string llvm_debug_args() {
  const char*           args = livehd::debug::env<"LHD_SIM_LLVM_ARGS">();
  static std::once_flag once;
  std::call_once(once, [args] {
    if (args == nullptr) {
      return;
    }
    std::vector<std::string> words{"lhd"};
    std::istringstream       in(args);
    for (std::string word; in >> word;) {
      words.push_back(word);
    }
    std::vector<const char*> argv;
    for (const auto& word : words) {
      argv.push_back(word.c_str());
    }
    llvm::cl::ParseCommandLineOptions(static_cast<int>(argv.size()), argv.data(), "lhd sim LLVM debug options\n");
  });
  return args == nullptr ? std::string{} : std::string{args};
}

// 0 = disabled.
unsigned branch_min_cost() {
  if (livehd::debug::env<"LHD_SIM_LLVM_NO_BRANCH">() != nullptr) {
    return 0;
  }
  const char* min = livehd::debug::env<"LHD_SIM_LLVM_BRANCH_MIN">();
  return min == nullptr ? 4u : static_cast<unsigned>(std::strtoul(min, nullptr, 0));
}

// `select c, load A, load B` (each load used only here, same type) reads both
// objects to keep one. Select the address instead: one load, no branch, no
// merge (a branch around each load left a PHI whose extension no longer folds
// into the load: matched_filter +18%). Each load may move down to the select
// only if nothing between may write its object (alias scope per object).
unsigned select_load_addresses(llvm::Function& function) {
  unsigned merged = 0;
  for (auto& block : function) {
    std::vector<llvm::SelectInst*> selects;
    for (auto& inst : block) {
      if (auto* select = llvm::dyn_cast<llvm::SelectInst>(&inst); select != nullptr && select->getCondition()->getType()->isIntegerTy(1)) {
        selects.push_back(select);
      }
    }
    for (auto* select : selects) {
      auto* a = llvm::dyn_cast<llvm::LoadInst>(select->getTrueValue());
      auto* b = llvm::dyn_cast<llvm::LoadInst>(select->getFalseValue());
      if (a == nullptr || b == nullptr || a == b || !a->isSimple() || !b->isSimple() || !a->hasOneUse() || !b->hasOneUse()
          || a->getParent() != &block || b->getParent() != &block || a->getType() != b->getType()
          || a->getPointerOperandType() != b->getPointerOperandType()) {
        continue;
      }
      const auto safe_to_select = [&](llvm::LoadInst* load) {
        const auto* scope = load->getMetadata(llvm::LLVMContext::MD_alias_scope);
        for (auto it = std::next(load->getIterator()); &*it != select; ++it) {
          if (!it->mayWriteToMemory()) {
            continue;
          }
          const auto* other = it->getMetadata(llvm::LLVMContext::MD_alias_scope);
          if (!llvm::isa<llvm::StoreInst>(&*it) || scope == nullptr || other == nullptr || other == scope) {
            return false;
          }
        }
        return true;
      };
      if (!safe_to_select(a) || !safe_to_select(b)) {
        continue;
      }
      llvm::IRBuilder<> builder(select);
      auto* address = builder.CreateSelect(select->getCondition(), a->getPointerOperand(), b->getPointerOperand(), "addr.sel");
      auto* load    = builder.CreateAlignedLoad(a->getType(), address, std::min(a->getAlign(), b->getAlign()), "load.sel");
      // Either object: keep only what holds for both (a range union).
      if (const auto* ra = a->getMetadata(llvm::LLVMContext::MD_range)) {
        if (const auto* rb = b->getMetadata(llvm::LLVMContext::MD_range)) {
          load->setMetadata(llvm::LLVMContext::MD_range, llvm::MDNode::getMostGenericRange(const_cast<llvm::MDNode*>(ra), const_cast<llvm::MDNode*>(rb)));
        }
      }
      select->replaceAllUsesWith(load);
      select->eraseFromParent();
      a->eraseFromParent();
      b->eraseFromParent();
      ++merged;
    }
  }
  return merged;
}

// Scope-aware sinking after region formation. Splitting a block (an if/else
// region) strands values defined above the branch whose every use is below
// it: a load and its extension then sit in different blocks, so instruction
// selection (per block) cannot fold them, and the value stays live across the
// arms. Move such an instruction to just before its first use when all its uses
// are in one block it dominates. A load moves only if no write that may alias
// it (a call, an unscoped write, or a store of its !alias.scope) lies on any
// path between; LLVM's Sink refuses any load followed by any store.
unsigned sink_into_use_blocks(llvm::Function& function) {
  llvm::DominatorTree dom(function);
  unsigned            moved = 0;
  const auto          writes_scope = [](const llvm::Instruction& inst, const llvm::MDNode* scope) {
    if (!inst.mayWriteToMemory()) {
      return false;
    }
    const auto* other = inst.getMetadata(llvm::LLVMContext::MD_alias_scope);
    return !llvm::isa<llvm::StoreInst>(&inst) || scope == nullptr || other == nullptr || other == scope;
  };
  for (bool changed = true; changed;) {
    changed = false;
    for (auto& block : function) {
      for (auto it = block.rbegin(); it != block.rend();) {
        auto& inst = *it++;
        if (inst.isTerminator() || llvm::isa<llvm::PHINode>(inst) || inst.use_empty() || inst.mayHaveSideEffects()) {
          continue;
        }
        const auto* load = llvm::dyn_cast<llvm::LoadInst>(&inst);
        if (inst.mayReadOrWriteMemory() && (load == nullptr || !load->isSimple())) {
          continue;
        }
        llvm::BasicBlock* target = nullptr;
        bool              ok     = true;
        for (const auto* user : inst.users()) {
          const auto* user_inst = llvm::dyn_cast<llvm::Instruction>(user);
          if (user_inst == nullptr || llvm::isa<llvm::PHINode>(user_inst)) {
            ok = false;
            break;
          }
          auto* ub = const_cast<llvm::BasicBlock*>(user_inst->getParent());
          if (target != nullptr && target != ub) {
            ok = false;
            break;
          }
          target = ub;
        }
        if (!ok || target == nullptr || target == &block || !dom.dominates(&block, target)) {
          continue;
        }
        if (load != nullptr) {
          const auto* scope = load->getMetadata(llvm::LLVMContext::MD_alias_scope);
          for (auto rest = std::next(inst.getIterator()); rest != block.end() && ok; ++rest) {
            ok = !writes_scope(*rest, scope);
          }
          // Every block on a path from `block` to `target` (exclusive).
          std::vector<llvm::BasicBlock*>           work(llvm::succ_begin(&block), llvm::succ_end(&block));
          std::unordered_set<const llvm::BasicBlock*> seen;
          while (ok && !work.empty()) {
            auto* bb = work.back();
            work.pop_back();
            if (bb == target || !seen.insert(bb).second) {
              continue;
            }
            if (!dom.dominates(&block, bb)) {
              ok = false;  // a path escapes the region: keep it simple
              break;
            }
            for (const auto& other : *bb) {
              if (writes_scope(other, scope)) {
                ok = false;
                break;
              }
            }
            work.insert(work.end(), llvm::succ_begin(bb), llvm::succ_end(bb));
          }
          // The target's own prefix up to the first use.
          for (auto& other : *target) {
            if (!ok) {
              break;
            }
            if (std::ranges::any_of(inst.users(), [&](const llvm::User* u) { return u == &other; })) {
              break;
            }
            ok = !writes_scope(other, scope);
          }
          if (!ok) {
            continue;
          }
        }
        llvm::Instruction* first = nullptr;
        for (auto& other : *target) {
          if (std::ranges::any_of(inst.users(), [&](const llvm::User* u) { return u == &other; })) {
            first = &other;
            break;
          }
        }
        if (first == nullptr) {
          continue;
        }
        inst.moveBefore(first->getIterator());
        ++moved;
        changed = true;
      }
    }
  }
  return moved;
}

unsigned conditionalize_selects(llvm::Function& function, unsigned min_cost) {
  unsigned                       regions = 0;
  if (livehd::debug::env<"LHD_SIM_LLVM_NO_ADDR_SELECT">() == nullptr) {
    select_load_addresses(function);
  }
  std::vector<llvm::BasicBlock*> work;
  for (auto& block : function) {
    work.push_back(&block);
  }
  // A simple load is a candidate too: it moves into its arm (later in the
  // block) only if no store or call that may write its object lies between it
  // and the branch (next_write, checked per region below). Kernel storage
  // carries one !alias.scope per object, so a store to another object does
  // not block it.
  static const bool arm_loads = livehd::debug::env<"LHD_SIM_LLVM_NO_ARM_LOADS">() == nullptr;
  const auto        movable   = [](const llvm::Instruction& inst) {
    if (const auto* load = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
      return arm_loads && load->isSimple();
    }
    return !inst.isTerminator() && !llvm::isa<llvm::PHINode>(inst) && !llvm::isa<llvm::AllocaInst>(inst)
           && !inst.mayHaveSideEffects() && !inst.mayReadOrWriteMemory();
  };
  const auto scope_of = [](const llvm::Instruction* inst) { return inst->getMetadata(llvm::LLVMContext::MD_alias_scope); };
  const auto scalar_select = [](const llvm::Instruction* inst) -> const llvm::SelectInst* {
    const auto* select = llvm::dyn_cast<llvm::SelectInst>(inst);
    return select != nullptr && select->getCondition()->getType()->isIntegerTy(1) ? select : nullptr;
  };
  while (!work.empty()) {
    auto* block = work.back();
    work.pop_back();
    std::vector<llvm::Instruction*>                      order;
    std::unordered_map<const llvm::Instruction*, size_t> position;
    for (auto& inst : *block) {
      position[&inst] = order.size();
      order.push_back(&inst);
    }
    // Per load: the block position of the first later instruction that may
    // write its object (a call, an unscoped write, or a store of its scope).
    std::unordered_map<const llvm::Instruction*, size_t> next_write;
    {
      std::unordered_map<const llvm::MDNode*, size_t> next_store_of_scope;
      size_t next_barrier = std::numeric_limits<size_t>::max();
      for (size_t k = order.size(); k-- > 0;) {
        auto* inst = order[k];
        if (llvm::isa<llvm::LoadInst>(inst)) {
          const auto* scope = scope_of(inst);
          size_t      limit = next_barrier;
          if (scope == nullptr) {
            for (const auto& [other, at] : next_store_of_scope) {
              limit = std::min(limit, at);
            }
          } else if (const auto it = next_store_of_scope.find(scope); it != next_store_of_scope.end()) {
            limit = std::min(limit, it->second);
          }
          next_write[inst] = limit;
        } else if (inst->mayWriteToMemory()) {
          const auto* scope = scope_of(inst);
          if (llvm::isa<llvm::StoreInst>(inst) && scope != nullptr) {
            next_store_of_scope[scope] = k;
          } else {
            next_barrier = k;
          }
        }
      }
    }
    // 1. Which instructions already sit inside another condition's arm (an
    //    inner select of an elif chain): those are handled after the outer
    //    region moves them into its block.
    using Arm = std::pair<const llvm::Value*, bool>;  // condition, side (true arm)
    std::unordered_map<const llvm::Instruction*, Arm> arm_of_condition;
    const auto                                        user_arm
        = [&](const llvm::Instruction* inst, const llvm::User* user, const auto& arms, const auto& key_of) -> std::optional<Arm> {
      const auto* inst_user = llvm::dyn_cast<llvm::Instruction>(user);
      if (inst_user == nullptr || inst_user->getParent() != block) {
        return std::nullopt;
      }
      if (const auto* select = scalar_select(inst_user); select != nullptr && key_of(select).first != nullptr) {
        const bool on_true  = select->getTrueValue() == inst;
        const bool on_false = select->getFalseValue() == inst;
        if (select->getCondition() == inst || on_true == on_false) {
          return std::nullopt;
        }
        return Arm{key_of(select).first, on_true};
      }
      const auto it = arms.find(inst_user);
      return it == arms.end() ? std::nullopt : std::optional<Arm>(it->second);
    };
    const auto propagate = [&](std::unordered_map<const llvm::Instruction*, Arm>& arms, const auto& key_of) {
      for (auto it = order.rbegin(); it != order.rend(); ++it) {
        auto* inst = *it;
        if (!movable(*inst) || inst->use_empty()) {
          continue;
        }
        std::optional<Arm> agreed;
        bool               ok = true;
        for (const auto* user : inst->users()) {
          const auto arm = user_arm(inst, user, arms, key_of);
          if (!arm || (agreed && *agreed != *arm)) {
            ok = false;
            break;
          }
          agreed = arm;
        }
        if (ok && agreed) {
          arms[inst] = *agreed;
        }
      }
    };
    propagate(arm_of_condition, [](const llvm::SelectInst* select) { return Arm{select->getCondition(), true}; });
    // 2. Groups: top-level selects on one condition, in order, cut where a
    //    user of an earlier member would precede the group's last member.
    struct Group {
      std::vector<llvm::SelectInst*> selects;
      size_t                         first_user = std::numeric_limits<size_t>::max();
    };
    std::vector<Group>                                  groups;
    std::unordered_map<const llvm::Value*, size_t>      open_group;
    std::unordered_map<const llvm::SelectInst*, size_t> group_of;
    for (auto* inst : order) {
      auto* select = llvm::dyn_cast<llvm::SelectInst>(inst);
      if (select == nullptr || scalar_select(select) == nullptr || arm_of_condition.contains(select)) {
        continue;
      }
      size_t first_user = std::numeric_limits<size_t>::max();
      for (const auto* user : select->users()) {
        const auto* user_inst = llvm::dyn_cast<llvm::Instruction>(user);
        if (user_inst != nullptr && user_inst->getParent() == block) {
          first_user = std::min(first_user, position[user_inst]);
        }
      }
      auto it = open_group.find(select->getCondition());
      if (it == open_group.end() || groups[it->second].first_user <= position[select]) {
        open_group[select->getCondition()] = groups.size();
        groups.push_back({});
        it = open_group.find(select->getCondition());
      }
      auto& group      = groups[it->second];
      group.first_user = std::min(group.first_user, first_user);
      group.selects.push_back(select);
      group_of[select] = it->second;
    }
    // 3. Arms per group (an instruction feeding two groups of one condition
    //    stays outside both). The arm key is the group index.
    std::unordered_map<const llvm::Instruction*, Arm> arm_of_group;
    std::vector<const llvm::Value*>                   group_key(groups.size());
    for (size_t g = 0; g < groups.size(); ++g) {
      group_key[g] = groups[g].selects.front();  // a unique, stable identity per group
    }
    propagate(arm_of_group, [&](const llvm::SelectInst* select) {
      const auto it = group_of.find(select);
      return it == group_of.end() ? Arm{nullptr, true} : Arm{group_key[it->second], true};
    });
    // 4. Make the first profitable group a region; its blocks are revisited.
    for (size_t g = 0; g < groups.size(); ++g) {
      std::vector<llvm::Instruction*> then_arm, else_arm;
      // A load may join the arm only if nothing writes its object before the
      // branch; whatever then feeds an instruction left outside stays too.
      const size_t                                at = position[groups[g].selects.back()];
      std::unordered_set<const llvm::Instruction*> in_arm;
      for (auto* inst : order) {
        const auto it = arm_of_group.find(inst);
        if (it != arm_of_group.end() && it->second.first == group_key[g]) {
          const auto nw = next_write.find(inst);
          if (nw == next_write.end() || nw->second > at) {
            in_arm.insert(inst);
          }
        }
      }
      for (bool changed = true; changed;) {
        changed = false;
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
          auto* inst = *it;
          if (!in_arm.contains(inst)) {
            continue;
          }
          const bool fed_outside = std::ranges::any_of(inst->users(), [&](const llvm::User* user) {
            const auto* user_inst = llvm::dyn_cast<llvm::Instruction>(user);
            if (user_inst == nullptr || in_arm.contains(user_inst)) {
              return false;
            }
            const auto* select = llvm::dyn_cast<llvm::SelectInst>(user_inst);
            return select == nullptr || !group_of.contains(select) || group_of.at(select) != g;
          });
          if (fed_outside) {
            in_arm.erase(inst);
            changed = true;
          }
        }
      }
      for (auto* inst : order) {
        if (in_arm.contains(inst)) {
          (arm_of_group.at(inst).second ? then_arm : else_arm).push_back(inst);
        }
      }
      // Cost = real work in the arms. Selects (a mux tree only routes values),
      // casts and freezes are free: branching over pure routing buys nothing
      // and costs a branch.
      const auto work_in = [](const std::vector<llvm::Instruction*>& arm) {
        return std::ranges::count_if(arm, [](const llvm::Instruction* inst) {
          return !llvm::isa<llvm::SelectInst>(inst) && !llvm::isa<llvm::CastInst>(inst) && !llvm::isa<llvm::FreezeInst>(inst);
        });
      };
      // A region pays when its arms hold at least `min_cost` real operations
      // (a load counts as one; a lone load pair became one load above).
      if (static_cast<size_t>(work_in(then_arm) + work_in(else_arm)) < min_cost) {
        continue;
      }
      // Splitting the block costs: every value defined above the branch and
      // used below it crosses blocks (instruction selection is per block: a
      // load no longer folds its extension, and the value stays live across
      // the arms). Count them.
      size_t crossing = 0;
      {
        const size_t split = position[groups[g].selects.back()];
        for (size_t k = 0; k < split; ++k) {
          auto* inst = order[k];
          if (in_arm.contains(inst) || inst->getType()->isVoidTy()) {
            continue;
          }
          crossing += std::ranges::any_of(inst->users(), [&](const llvm::User* user) {
            const auto* ui = llvm::dyn_cast<llvm::Instruction>(user);
            return ui != nullptr && ui->getParent() == block && position[ui] > split && !in_arm.contains(ui);
          });
        }
      }
      // One operation of required work per `cross_per_op` crossing values
      // (matched_filter: a 4-op region crossing 254 values cost +12%).
      static const size_t cross_per_op = [] {
        const char* v = livehd::debug::env<"LHD_SIM_LLVM_BRANCH_CROSS">();
        return v == nullptr ? size_t{8} : std::max<size_t>(1, std::strtoul(v, nullptr, 0));
      }();
      const auto arm_work = static_cast<size_t>(work_in(then_arm) + work_in(else_arm));
      if (livehd::debug::env<"LHD_SIM_LLVM_BRANCH_DEBUG">() != nullptr) {
        std::fprintf(stderr, "[region] work=%zu crossing=%zu block=%zu\n", arm_work, crossing, order.size());
      }
      if (arm_work < min_cost + crossing / cross_per_op) {
        continue;
      }
      auto&              group     = groups[g];
      llvm::Instruction* last      = group.selects.back();
      llvm::Instruction* then_term = nullptr;
      llvm::Instruction* else_term = nullptr;
      llvm::SplitBlockAndInsertIfThenElse(group.selects.front()->getCondition(), last->getIterator(), &then_term, &else_term);
      for (auto* inst : then_arm) {
        inst->moveBefore(then_term->getIterator());
      }
      for (auto* inst : else_arm) {
        inst->moveBefore(else_term->getIterator());
      }
      auto*             tail = last->getParent();
      llvm::IRBuilder<> phis(tail, tail->begin());
      for (auto* select : group.selects) {
        // A narrow value whose only use extends it: extend in each arm and
        // merge the wide value, so the extension still folds into a load
        // (a PHI of i8 then sxtb in the join block cost matched_filter +18%).
        auto* ext = select->hasOneUse() ? llvm::dyn_cast<llvm::CastInst>(*select->user_begin()) : nullptr;
        if (ext != nullptr && (llvm::isa<llvm::SExtInst>(ext) || llvm::isa<llvm::ZExtInst>(ext))) {
          llvm::IRBuilder<> on_true(then_term);
          llvm::IRBuilder<> on_false(else_term);
          auto*             t   = on_true.CreateCast(ext->getOpcode(), select->getTrueValue(), ext->getType());
          auto*             f   = on_false.CreateCast(ext->getOpcode(), select->getFalseValue(), ext->getType());
          auto*             phi = phis.CreatePHI(ext->getType(), 2, select->getName() + ".arm");
          phi->addIncoming(t, then_term->getParent());
          phi->addIncoming(f, else_term->getParent());
          ext->replaceAllUsesWith(phi);
          ext->eraseFromParent();
          select->eraseFromParent();
          continue;
        }
        auto* phi = phis.CreatePHI(select->getType(), 2, select->getName() + ".arm");
        phi->addIncoming(select->getTrueValue(), then_term->getParent());
        phi->addIncoming(select->getFalseValue(), else_term->getParent());
        select->replaceAllUsesWith(phi);
        select->eraseFromParent();
      }
      ++regions;
      work.push_back(block);
      work.push_back(then_term->getParent());
      work.push_back(else_term->getParent());
      work.push_back(tail);
      break;
    }
  }
  return regions;
}

// Register-pressure-aware reordering of each block (README "Live ranges in
// LLVM kernels"). A kernel's IR follows the color plan's topological member
// order, which defines many values long before their first use; AArch64's
// DAG/machine schedulers mostly keep the order they are given, and LLVM's
// LiveRangeShrink is an X86-only pass that only hoists an instruction to its
// operands. This list-schedules a block bottom-up to shorten live ranges (the
// sum of live interval lengths, Shobaki et al.'s SLIL cost, greedily): among
// the ready instructions -- every user already placed -- take the one that
// makes the fewest new values live, crediting the value its result stops
// keeping live; ties keep the original order, so a definition sinks to just
// before its first use. Dependences: data uses, and memory order (a load
// stays after the previous store/call, a store/call after every earlier
// memory access). PHIs, allocas and the terminator stay put.
unsigned shrink_live_ranges(llvm::Function& function) {
  unsigned moved = 0;
  for (auto& block : function) {
    std::vector<llvm::Instruction*> body;
    for (auto& inst : block) {
      if (!llvm::isa<llvm::PHINode>(inst) && !llvm::isa<llvm::AllocaInst>(inst) && !inst.isTerminator()) {
        body.push_back(&inst);
      }
    }
    if (body.size() < 8) {
      continue;
    }
    const size_t                                         n = body.size();
    std::unordered_map<const llvm::Instruction*, size_t> index;
    for (size_t i = 0; i < n; ++i) {
      index[body[i]] = i;
    }
    // successors[i]: instructions that must stay AFTER i.
    std::vector<std::vector<size_t>> successors(n);
    std::vector<size_t>              pending(n, 0);  // successors not yet placed (bottom-up readiness)
    const auto                       edge = [&](size_t before, size_t after) {
      successors[before].push_back(after);
      ++pending[before];
    };
    size_t              last_write = n;  // n: none
    std::vector<size_t> reads_since_write;
    for (size_t i = 0; i < n; ++i) {
      auto* inst = body[i];
      for (const auto& operand : inst->operands()) {
        if (const auto* def = llvm::dyn_cast<llvm::Instruction>(operand.get()); def != nullptr && def->getParent() == &block) {
          if (const auto it = index.find(def); it != index.end()) {
            edge(it->second, i);
          }
        }
      }
      const bool writes = inst->mayWriteToMemory() || inst->mayHaveSideEffects();
      const bool reads  = inst->mayReadFromMemory();
      if (writes) {
        if (last_write != n) {
          edge(last_write, i);
        }
        for (const size_t read : reads_since_write) {
          edge(read, i);
        }
        reads_since_write.clear();
        last_write = i;
      } else if (reads) {
        if (last_write != n) {
          edge(last_write, i);
        }
        reads_since_write.push_back(i);
      }
    }
    // Values live below the block's body: used by the terminator, a PHI, or
    // another block.
    std::vector<bool> live(n, false);
    std::vector<int>  users_left(n, 0);  // in-body users not yet placed
    for (size_t i = 0; i < n; ++i) {
      for (const auto* user : body[i]->users()) {
        const auto* user_inst = llvm::dyn_cast<llvm::Instruction>(user);
        const auto  it        = user_inst == nullptr ? index.end() : index.find(user_inst);
        if (it == index.end()) {
          live[i] = true;
        } else {
          ++users_left[i];
        }
      }
    }
    std::vector<size_t> ready;
    for (size_t i = 0; i < n; ++i) {
      if (pending[i] == 0) {
        ready.push_back(i);
      }
    }
    std::vector<std::vector<size_t>> predecessors(n);
    for (size_t i = 0; i < n; ++i) {
      for (const size_t after : successors[i]) {
        predecessors[after].push_back(i);
      }
    }
    std::vector<size_t> bottom_up;
    bottom_up.reserve(n);
    while (!ready.empty()) {
      size_t best       = 0;
      int    best_score = std::numeric_limits<int>::max();
      for (size_t r = 0; r < ready.size(); ++r) {
        const size_t i     = ready[r];
        int          score = live[i] ? -1 : 0;  // placing its definition ends its live range above
        for (const auto& operand : body[i]->operands()) {
          const auto* def = llvm::dyn_cast<llvm::Instruction>(operand.get());
          const auto  it  = def == nullptr ? index.end() : index.find(def);
          if (it != index.end() && !live[it->second]) {
            ++score;  // a new value becomes live from its definition down to here
          }
        }
        if (score < best_score || (score == best_score && i > ready[best])) {
          best       = r;
          best_score = score;
        }
      }
      const size_t i = ready[best];
      ready[best]    = ready.back();
      ready.pop_back();
      bottom_up.push_back(i);
      live[i] = false;
      for (const auto& operand : body[i]->operands()) {
        const auto* def = llvm::dyn_cast<llvm::Instruction>(operand.get());
        if (const auto it = def == nullptr ? index.end() : index.find(def); it != index.end()) {
          live[it->second] = true;
        }
      }
      for (const size_t before : predecessors[i]) {
        if (--pending[before] == 0) {
          ready.push_back(before);
        }
      }
    }
    if (bottom_up.size() != n) {
      continue;  // not a DAG (cannot happen for SSA in one block); keep the order
    }
    auto* anchor = block.getTerminator();
    for (auto it = bottom_up.rbegin(); it != bottom_up.rend(); ++it) {
      body[*it]->moveBefore(anchor->getIterator());  // appended in schedule order, top to bottom
    }
    for (size_t pos = 0; pos < n; ++pos) {
      moved += bottom_up[n - 1 - pos] != pos;
    }
  }
  return moved;
}

// Preserve short-circuit evaluation for absorbing Boolean loop carries. After
// AND reaches false (or OR reaches true), the expensive operand cannot change
// that carry again. Slop exposes this control flow through C++ &&/||, while
// eagerly computed LLVM bitwise operands otherwise keep doing indexed reads.
// This only guards the operand: other outputs and every loop iteration remain.
void short_circuit_loop_carries(llvm::Function& function) {
  std::vector<llvm::BinaryOperator*> candidates;
  for (auto& block : function) {
    for (auto& instruction : block) {
      auto* binary = llvm::dyn_cast<llvm::BinaryOperator>(&instruction);
      if (binary && binary->getType()->isIntegerTy(1)
          && (binary->getOpcode() == llvm::Instruction::And || binary->getOpcode() == llvm::Instruction::Or)) {
        candidates.push_back(binary);
      }
    }
  }
  for (auto* binary : candidates) {
    llvm::PHINode* carry   = nullptr;
    llvm::Value*   operand = nullptr;
    for (unsigned side = 0; side < 2; ++side) {
      auto* phi = llvm::dyn_cast<llvm::PHINode>(binary->getOperand(side));
      if (phi && phi->getNumIncomingValues() == 2 && (phi->getIncomingValue(0) == binary || phi->getIncomingValue(1) == binary)) {
        carry   = phi;
        operand = binary->getOperand(1 - side);
        break;
      }
    }
    if (!carry) {
      continue;
    }
    // Sink only an exclusive, same-block dependence slice. A skipped load must
    // not cross a write; rejecting all intervening writes avoids an alias query.
    std::vector<llvm::Instruction*>     slice;
    llvm::SmallVector<llvm::Value*, 64> work{operand};
    while (!work.empty() && slice.size() < 256) {
      auto* inst = llvm::dyn_cast<llvm::Instruction>(work.pop_back_val());
      if (!inst || !inst->hasOneUse() || inst->getParent() != binary->getParent() || inst->isTerminator()
          || inst->mayHaveSideEffects() || llvm::isa<llvm::PHINode>(inst) || llvm::isa<llvm::AllocaInst>(inst)) {
        continue;
      }
      if (inst->mayReadFromMemory()) {
        auto* load = llvm::dyn_cast<llvm::LoadInst>(inst);
        if (!load || !load->isSimple()) {
          continue;
        }
        bool intervening_write = false;
        for (auto it = std::next(inst->getIterator()); &*it != binary; ++it) {
          intervening_write |= it->mayWriteToMemory();
        }
        if (intervening_write) {
          continue;
        }
      }
      slice.push_back(inst);
      for (auto& input : inst->operands()) {
        work.push_back(input);
      }
    }
    // Keep small operations branchless and bound analysis of very large DAGs.
    if (slice.size() < 8 || !work.empty()) {
      continue;
    }
    std::sort(slice.begin(), slice.end(), [](const auto* a, const auto* b) { return a->comesBefore(b); });
    auto* predecessor = binary->getParent();
    auto* merge       = predecessor->splitBasicBlock(binary->getIterator(), "carry.join");
    auto* evaluate    = llvm::BasicBlock::Create(function.getContext(), "carry.eval", &function, merge);
    auto* branch      = llvm::UncondBrInst::Create(merge, evaluate);
    for (auto* inst : slice) {
      inst->moveBefore(branch->getIterator());
    }
    const bool conjunction = binary->getOpcode() == llvm::Instruction::And;
    predecessor->getTerminator()->eraseFromParent();
    llvm::CondBrInst::Create(carry, conjunction ? evaluate : merge, conjunction ? merge : evaluate, predecessor);
    auto* result = llvm::PHINode::Create(binary->getType(), 2, "carry.result", binary->getIterator());
    result->addIncoming(llvm::ConstantInt::get(binary->getType(), !conjunction), predecessor);
    result->addIncoming(operand, evaluate);
    binary->replaceAllUsesWith(result);
    binary->eraseFromParent();
  }
}

}  // namespace

class Cgen_llvm::Impl {
public:
  struct Output {
    size_t                     index = 0;
    Value                      value;
    llvm::Instruction*         anchor = nullptr;
    // A direct-storage output (add_storage_output): no packed field.
    std::optional<Storage>     dest;
    std::optional<Storage>     compare;
    bool                       report_changed = true;
    std::vector<Change_effect> effects;
    std::optional<Value>       guard;  // publish only while set (add_storage_output)
  };
  struct State_output {
    size_t                  word;
    size_t                  previous_word;
    Value                   value;
    std::vector<Dirty_mask> dirty;
  };

  llvm::LLVMContext                                                                               context;
  std::unique_ptr<llvm::Module>                                                                   module;
  llvm::IRBuilder<>                                                                               builder;
  llvm::Function*                                                                                 function      = nullptr;
  llvm::Function*                                                                                 loop_function = nullptr;
  llvm::Value*                                                                                    inputs        = nullptr;
  llvm::Value*                                                                                    outputs       = nullptr;
  llvm::Value*                                                                                    changed       = nullptr;
  llvm::Value*                                                                                    owner         = nullptr;
  std::vector<llvm::Value*>                                                                       values;
  // Values are shared only within the current block: cached selections must
  // dominate every extracted lane, including kernels that contain loop blocks.
  std::map<std::tuple<llvm::BasicBlock*, llvm::Value*, llvm::Value*, llvm::Value*>, llvm::Value*> word_muxes;
  std::map<std::tuple<llvm::BasicBlock*, llvm::Value*, uint32_t, uint32_t>, llvm::Value*>         mux_words;
  std::map<std::tuple<llvm::BasicBlock*, llvm::Value*, uint32_t>, llvm::Value*>                   mux_rotations;
  std::vector<size_t>                                                                             input_word_offsets;
  std::vector<uint32_t>                                                                           input_bits;
  // The packed ABI layout of the inputs (Cgen_llvm::packed_layout); empty for the state ABI.
  std::vector<Packed_field>                                                                       input_fields;
  size_t                                                                                          input_words = 0;
  // Storage-bound inputs (bind_storage_input) have no packed field: the packed
  // layout is computed over the others on first use, after every binding.
  std::vector<std::optional<Storage>>                                                             input_storage;
  bool                                                                                            input_layout_ready = false;
  // Every load/store of a storage object, by owner-table resource: seal()
  // gives each object its own alias scope, noalias against every other one.
  std::vector<std::pair<llvm::Instruction*, std::pair<size_t, size_t>>>                           storage_accesses;
  std::vector<std::optional<Value>>                                                               deferred_casts;
  std::map<size_t, uint64_t>              speculations;   // input id -> inactive value (speculate_input)
  std::map<size_t, llvm::WeakTrackingVH>  input_handles;  // materialized inputs (null once erased)
  std::vector<std::pair<uint32_t, bool>>  input_types;
  std::vector<Output>                     output_values;
  std::string                             error;
  std::string                             cache_path;
  bool*                                   cache_reused = nullptr;
  std::optional<bool>                     sealed_changed;
  bool                                    scalar_abi   = false;
  bool                                    state_abi    = false;
  size_t                                  state_words  = 0;
  uint64_t                                unroll_extra = 0;  // see kUnrolledExtra
  std::vector<State_output>               state_outputs;
  std::unordered_map<std::string, Memory> memories;

  Impl(std::string_view function_name, const std::vector<std::pair<uint32_t, bool>>& input_desc, bool use_scalar_abi,
       const State_layout* layout = nullptr)
      : module(std::make_unique<llvm::Module>("livehd.sim.color", context))
      , builder(context)
      , input_types(input_desc)
      , scalar_abi(use_scalar_abi)
      , state_abi(layout != nullptr)
      , state_words(layout == nullptr ? 0 : layout->words) {
    auto*                          i64_ptr = llvm::PointerType::getUnqual(context);
    llvm::SmallVector<llvm::Type*> argument_types;
    if (scalar_abi) {
      argument_types.assign(input_types.size(), builder.getInt64Ty());
      argument_types.push_back(i64_ptr);
    } else if (state_abi) {
      argument_types.assign(2, i64_ptr);
    } else {
      argument_types.assign(4, i64_ptr);
    }
    auto* fn_type = llvm::FunctionType::get(scalar_abi ? builder.getInt64Ty() : builder.getVoidTy(), argument_types, false);
    function      = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, llvm::StringRef(function_name), *module);
    function->addFnAttr(llvm::Attribute::AlwaysInline);
    auto argument = function->arg_begin();
    if (state_abi) {
      inputs = outputs = &*argument++;
      inputs->setName("state");
    } else if (!scalar_abi) {
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
    input_storage.resize(input_types.size());
    for (size_t i = 0; i < input_types.size(); ++i) {
      const auto [width, unused_unsigned] = input_types[i];
      (void)unused_unsigned;
      if (width == 0) {
        error = "LLVM color inputs must have a non-zero width";
        return;
      }
      // The packed ABI's offsets come later (ensure_input_layout), once the
      // storage bindings are known; the state ABI's are fixed here.
      const auto offset = layout == nullptr ? 0 : layout->inputs[i].word;
      const auto bit    = layout == nullptr ? 0u : layout->inputs[i].bit;
      if (state_abi
          && (bit >= 64 || width > std::numeric_limits<uint32_t>::max() - bit || offset > state_words
              || word_count(width + bit) > state_words - offset)) {
        error = "LLVM state input is outside its allocation";
        return;
      }
      input_word_offsets.push_back(offset);
      input_bits.push_back(bit);
      if (scalar_abi) {
        auto* value = &*argument++;
        value->setName("in");
        values.push_back(cast_integer(builder, value, width, true));
      } else {
        values.push_back(nullptr);  // Materialize at first use, not at kernel entry.
      }
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
        // Load the WHOLE words, then truncate. A `load i70` is undefined LLVM
        // when the bits past bit 69 of its last byte were not written by an
        // i70 store, and packed words of a signed Slop are sign-extended: the
        // garbage reached a later zext (`zext i70 -> i72` set bits 70-71) and a
        // wide `x % d` divided by the wrong divisor (random Verilog sim fuzz vs
        // Icarus/Verilator/slop, 2026-10-09).
        // (A whole-byte width has no padding bits, so it keeps its exact load.)
        auto*      ptr   = builder.CreateConstInBoundsGEP1_64(builder.getInt64Ty(), base, offset);
        const auto words = width % 8 == 0 ? width : static_cast<unsigned>(word_count(width) * 64);
        auto* whole = builder.CreateAlignedLoad(builder.getIntNTy(words), ptr, llvm::Align(alignof(uint64_t)), name + ".words");
        return words == width ? whole : builder.CreateTrunc(whole, builder.getIntNTy(width), name + ".bits");
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

  // The packed input layout over the inputs that are not storage-bound. Fixed
  // at the first packed access; bind_storage_input refuses after that.
  void ensure_input_layout() {
    if (input_layout_ready || state_abi) {
      input_layout_ready = true;
      return;
    }
    input_layout_ready = true;
    std::vector<uint32_t> widths;
    for (size_t i = 0; i < input_types.size(); ++i) {
      if (!input_storage[i]) {
        widths.push_back(input_types[i].first);
      }
    }
    const auto packed = packed_layout(widths, &input_words);
    input_fields.assign(input_types.size(), Packed_field{});
    size_t next = 0;
    for (size_t i = 0; i < input_types.size(); ++i) {
      if (!input_storage[i]) {
        input_fields[i]       = packed[next++];
        input_word_offsets[i] = input_fields[i].word;
        input_bits[i]         = input_fields[i].bit;
      }
    }
  }
  // Direct storage (Cgen_llvm::Storage): tag word, then value words; native
  // storage is the value alone at its natural width.
  static size_t storage_words(const Storage& s) { return word_count(s.canonical ? s.width + 1 : s.width); }
  static unsigned native_bits(const Storage& s) {
    return s.width <= 8 ? 8 : s.width <= 16 ? 16 : s.width <= 32 ? 32 : static_cast<unsigned>(64 * word_count(s.width));
  }
  static size_t storage_bytes(const Storage& s) { return s.native ? native_bits(s) / 8 : 8 * (1 + storage_words(s)); }
  // The integer image a load or store moves.
  static unsigned image_bits(const Storage& s) { return s.native ? native_bits(s) : static_cast<unsigned>(storage_words(s) * 64); }
  static uint64_t image_align(const Storage& s) { return s.native ? std::min<uint64_t>(8, native_bits(s) / 8) : 8; }
  // Accesses are keyed by (resource, byte offset): one object, or one array element.
  static std::pair<size_t, size_t> storage_key(const Storage& s) { return {s.resource, s.offset + s.element * storage_bytes(s)}; }
  llvm::Value*                     storage_base(const Storage& s) {
    auto* slot = builder.CreateConstInBoundsGEP1_64(builder.getPtrTy(), owner, s.resource);
    auto* load = builder.CreateLoad(builder.getPtrTy(), slot, "storage");
    // No !invariant.load: it let LLVM hoist every table load to the entry,
    // and hundreds of live object pointers spilled (picorv32: 4 KB frame).
    // Reloading a pointer next to its use is one L1 hit.
    load->setMetadata(llvm::LLVMContext::MD_nonnull, llvm::MDNode::get(context, {}));
    const size_t bytes = s.offset + s.element * storage_bytes(s);
    if (bytes == 0) {
      return load;
    }
    return builder.CreateConstInBoundsGEP1_64(builder.getInt8Ty(), load, bytes);
  }
  llvm::Value* storage_words_ptr(llvm::Value* base) { return builder.CreateConstInBoundsGEP1_64(builder.getInt64Ty(), base, 1); }
  llvm::Value* storage_value_ptr(const Storage& s, llvm::Value* base) { return s.native ? base : storage_words_ptr(base); }
  llvm::Value* load_storage(const Storage& s, uint32_t width, llvm::StringRef name) {
    // The whole word image (always in bounds), then the low `width` bits.
    auto* image = builder.CreateAlignedLoad(builder.getIntNTy(image_bits(s)),
                                            storage_value_ptr(s, storage_base(s)),
                                            llvm::Align(image_align(s)),
                                            name);
    storage_accesses.emplace_back(image, storage_key(s));
    // A native value is stored extended from its width (zero when canonical,
    // else sign): say so, so a 1-bit flag held in a byte needs no mask or test
    // beyond the byte itself.
    if (s.native && s.width < image_bits(s) && s.width < 64) {
      const auto bits = image_bits(s);
      llvm::APInt lo  = s.canonical ? llvm::APInt(bits, 0) : llvm::APInt::getSignedMinValue(s.width).sext(bits);
      llvm::APInt hi  = s.canonical ? llvm::APInt::getOneBitSet(bits, s.width) : llvm::APInt::getSignedMaxValue(s.width).sext(bits) + 1;
      llvm::MDBuilder md(context);
      image->setMetadata(llvm::LLVMContext::MD_range, md.createRange(lo, hi));
    }
    return cast_integer(builder, image, width, true);
  }
  // The whole word image a store of `value` leaves (and identical() compares).
  llvm::Value* storage_image(const Storage& s, llvm::Value* value) {
    const auto bits = image_bits(s);
    auto*      v    = cast_integer(builder, value, s.width, true);
    return s.canonical ? builder.CreateZExt(v, builder.getIntNTy(bits)) : builder.CreateSExt(v, builder.getIntNTy(bits));
  }
  // Whether the stored words differ from `value`'s image. Slop::identical
  // would also call a non-Integer tag different; that only re-settles a
  // value whose bits did not move (every consumer reads the bits), and the
  // store below makes the tag Integer, so the tag is not loaded here.
  llvm::Value* storage_differs(const Storage& s, llvm::Value* value) {
    auto* base = storage_base(s);
    auto* old  = builder.CreateAlignedLoad(builder.getIntNTy(image_bits(s)),
                                           storage_value_ptr(s, base),
                                           llvm::Align(image_align(s)),
                                           "storage.old");
    storage_accesses.emplace_back(llvm::cast<llvm::Instruction>(old), storage_key(s));
    return builder.CreateICmpNE(old, storage_image(s, value));
  }
  void store_storage(const Storage& s, llvm::Value* value) {
    auto* base  = storage_base(s);
    auto* store = builder.CreateStore(storage_image(s, value), storage_value_ptr(s, base));
    store->setAlignment(llvm::Align(image_align(s)));
    storage_accesses.emplace_back(store, storage_key(s));
    if (!s.native) {
      auto* tag = builder.CreateStore(builder.getInt64(0), base);  // Type::Integer
      storage_accesses.emplace_back(tag, storage_key(s));
    }
  }
  // Distinct storage objects never overlap (each resource is one Slop
  // object), so their accesses may be reordered freely: without this every
  // input load stayed above every earlier output store and its value lived
  // in a register (and spilled) until its use. One object's load and store
  // share a scope, so an in-place register keeps its order.
  void scope_storage() {
    if (storage_accesses.empty()) {
      return;
    }
    llvm::MDBuilder                                    md(context);
    auto*                                              domain = md.createAnonymousAliasScopeDomain("lhd.storage");
    std::map<std::pair<size_t, size_t>, llvm::MDNode*> scopes;
    for (const auto& [instruction, resource] : storage_accesses) {
      if (!scopes.contains(resource)) {
        scopes[resource] = md.createAnonymousAliasScope(domain, "object");
      }
    }
    std::map<std::pair<size_t, size_t>, llvm::MDNode*> others;
    for (const auto& [resource, scope] : scopes) {
      llvm::SmallVector<llvm::Metadata*> list;
      for (const auto& [other, other_scope] : scopes) {
        if (other != resource) {
          list.push_back(other_scope);
        }
      }
      others[resource] = llvm::MDNode::get(context, list);
    }
    for (const auto& [instruction, resource] : storage_accesses) {
      instruction->setMetadata(llvm::LLVMContext::MD_alias_scope, llvm::MDNode::get(context, {scopes[resource]}));
      instruction->setMetadata(llvm::LLVMContext::MD_noalias, others[resource]);
    }
  }
  // Loads that read one value for the whole call -- an object this kernel
  // never writes, the owner table, the packed inputs -- become
  // `!invariant.load` so rematerialize_invariant may repeat them near each
  // use. Only at write_module, on this kernel's own function: a native loop
  // or an inlined body has already copied the body by then, and there the
  // transport buffer it reads changes every iteration.
  void mark_invariant_loads() {
    // A body still called from another function (a loop, an inlined
    // caller) runs more than once per call of that function: keep it plain.
    if (remat_distance() == 0 || state_abi || function == nullptr || !function->use_empty()) {
      return;
    }
    std::set<std::pair<size_t, size_t>> written;
    for (const auto& [instruction, resource] : storage_accesses) {
      if (llvm::isa<llvm::StoreInst>(instruction)) {
        written.insert(resource);
      }
    }
    for (const auto& [instruction, resource] : storage_accesses) {
      if (llvm::isa<llvm::LoadInst>(instruction) && instruction->getFunction() == function && !written.contains(resource)) {
        instruction->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(context, {}));
      }
    }
    for (auto& block : *function) {
      for (auto& inst : block) {
        auto* load = llvm::dyn_cast<llvm::LoadInst>(&inst);
        if (load == nullptr) {
          continue;
        }
        const auto* base = llvm::getUnderlyingObject(load->getPointerOperand());
        if ((inputs != nullptr && base == inputs) || (owner != nullptr && base == owner)) {
          load->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(context, {}));
        }
      }
    }
  }

  // Reset-free fast path (speculate_input): the exported function becomes a
  // dispatcher that reads the speculated inputs and calls `<name>.fast`, a copy
  // with them folded to their inactive constants, when all of them hold it, and
  // `<name>.slow` (the kernel as built) otherwise. Returns the bodies.
  std::vector<llvm::Function*> split_speculation() {
    if (speculations.empty() || state_abi || scalar_abi || loop_function != nullptr || function == nullptr || !function->use_empty()
        || livehd::debug::env<"LHD_SIM_LLVM_NO_RESET_SPLIT">() != nullptr) {
      return {function};
    }
    // A speculated value must be computable on entry: loads, casts and
    // arithmetic over the arguments only (the kernel stores nothing before).
    std::function<bool(const llvm::Value*, unsigned)> entry_computable = [&](const llvm::Value* v, unsigned depth) {
      if (llvm::isa<llvm::Argument>(v) || llvm::isa<llvm::Constant>(v)) {
        return true;
      }
      const auto* inst = llvm::dyn_cast<llvm::Instruction>(v);
      if (inst == nullptr || depth > 16 || llvm::isa<llvm::PHINode>(inst) || llvm::isa<llvm::CallBase>(inst)
          || (inst->mayHaveSideEffects() && !llvm::isa<llvm::LoadInst>(inst))) {
        return false;
      }
      return std::ranges::all_of(inst->operands(), [&](const llvm::Use& op) { return entry_computable(op.get(), depth + 1); });
    };
    std::vector<std::pair<llvm::Instruction*, uint64_t>> specs;
    for (const auto& [id, inactive] : speculations) {
      const auto handle = input_handles.find(id);
      auto*      inst   = handle == input_handles.end() ? nullptr : llvm::dyn_cast_or_null<llvm::Instruction>(handle->second);
      if (inst != nullptr && inst->getFunction() == function && inst->getType()->isIntegerTy() && entry_computable(inst, 0)) {
        specs.emplace_back(inst, inactive);
      }
    }
    if (specs.empty()) {
      return {function};
    }
    const std::string       name = function->getName().str();
    llvm::ValueToValueMapTy fast_map;
    auto*                   fast = llvm::CloneFunction(function, fast_map);
    auto*                   slow = function;
    slow->setName(name + ".slow");
    fast->setName(name + ".fast");
    // Inlined into the dispatcher by O2 (one function, two regions, no call on
    // the hot path). LHD_SIM_LLVM_RESET_CALLS=1 keeps them calls (debug).
    const bool calls = livehd::debug::env<"LHD_SIM_LLVM_RESET_CALLS">() != nullptr;
    for (auto* body : {slow, fast}) {
      body->setLinkage(llvm::GlobalValue::InternalLinkage);
      body->removeFnAttr(calls ? llvm::Attribute::AlwaysInline : llvm::Attribute::NoInline);
      body->addFnAttr(calls ? llvm::Attribute::NoInline : llvm::Attribute::AlwaysInline);
    }
    for (const auto& [inst, inactive] : specs) {
      auto* copy = llvm::cast<llvm::Instruction>(fast_map[inst]);
      copy->replaceAllUsesWith(llvm::ConstantInt::get(copy->getType(), inactive));
    }
    auto* dispatch = llvm::Function::Create(slow->getFunctionType(), llvm::GlobalValue::ExternalLinkage, name, *module);
    dispatch->setAttributes(slow->getAttributes());
    dispatch->removeFnAttr(llvm::Attribute::NoInline);
    dispatch->addFnAttr(llvm::Attribute::AlwaysInline);  // as the kernel it replaces
    auto*                   entry = llvm::BasicBlock::Create(context, "entry", dispatch);
    llvm::IRBuilder<>       at(entry);
    llvm::ValueToValueMapTy entry_map;
    for (auto [from, to] = std::pair{slow->arg_begin(), dispatch->arg_begin()}; from != slow->arg_end(); ++from, ++to) {
      to->setName(from->getName());
      entry_map[&*from] = &*to;
    }
    std::function<llvm::Value*(llvm::Value*)> copy_to_entry = [&](llvm::Value* v) -> llvm::Value* {
      if (llvm::isa<llvm::Constant>(v)) {
        return v;
      }
      if (auto it = entry_map.find(v); it != entry_map.end()) {
        return it->second;
      }
      auto* inst  = llvm::cast<llvm::Instruction>(v);
      auto* clone = inst->clone();
      for (auto& op : clone->operands()) {
        op.set(copy_to_entry(op.get()));
      }
      at.Insert(clone);
      entry_map[v] = clone;
      return clone;
    };
    llvm::Value* quiet = at.getTrue();
    for (const auto& [inst, inactive] : specs) {
      auto* value = copy_to_entry(inst);
      quiet       = at.CreateAnd(quiet, at.CreateICmpEQ(value, llvm::ConstantInt::get(value->getType(), inactive)));
    }
    auto* fast_block = llvm::BasicBlock::Create(context, "reset.free", dispatch);
    auto* slow_block = llvm::BasicBlock::Create(context, "reset", dispatch);
    at.CreateCondBr(quiet, fast_block, slow_block, llvm::MDBuilder(context).createLikelyBranchWeights());
    std::vector<llvm::Value*> args;
    for (auto& arg : dispatch->args()) {
      args.push_back(&arg);
    }
    for (auto [block, callee] : {
             std::pair{fast_block, fast},
             std::pair{slow_block, slow}
    }) {
      llvm::IRBuilder<> call_at(block);
      auto*             call = call_at.CreateCall(callee, args);
      call->setTailCallKind(llvm::CallInst::TCK_Tail);
      call_at.CreateRetVoid();
    }
    function = dispatch;
    return {fast, slow};
  }

  // One value of a packed buffer (Cgen_llvm::packed_layout).
  llvm::Value* load_field(llvm::Value* base, const Packed_field& field, uint32_t width, llvm::StringRef name) {
    if (field.bit == 0) {
      return load_packed(base, field.word, width, name);
    }
    auto* raw = load_packed(base, field.word, width + field.bit, name);
    return cast_integer(builder, builder.CreateLShr(raw, field.bit), width, true);
  }
  // A shared word is read-modify-written: its other bits belong to other
  // values. Transport buffers holding shared words are zeroed before use.
  void store_field(llvm::Value* value, llvm::Value* base, const Packed_field& field, uint32_t width) {
    if (field.alone) {
      store_packed(value, base, field.word, width);
      return;
    }
    auto*          i64  = builder.getInt64Ty();
    auto*          ptr  = builder.CreateConstInBoundsGEP1_64(i64, base, field.word);
    const uint64_t mask = (width >= 64 ? ~uint64_t{0} : (uint64_t{1} << width) - 1) << field.bit;
    auto*          bits = builder.CreateShl(cast_integer(builder, value, 64, true), field.bit);
    auto*          old  = builder.CreateLoad(i64, ptr, "field.word");
    builder.CreateStore(
        builder.CreateOr(builder.CreateAnd(old, builder.getInt64(~mask)), builder.CreateAnd(bits, builder.getInt64(mask))),
        ptr);
  }
  void zero_words(llvm::Value* base, size_t words) {
    for (size_t i = 0; i < words; ++i) {
      builder.CreateStore(builder.getInt64(0), builder.CreateConstInBoundsGEP1_64(builder.getInt64Ty(), base, i));
    }
  }
  // Over output_values in index order; a storage output gets an unused field.
  std::vector<Packed_field> output_fields(size_t* words) const {
    std::vector<uint32_t> widths;
    for (const auto& output : output_values) {
      if (!output.dest) {
        widths.push_back(output.value.width);
      }
    }
    const auto                packed = packed_layout(widths, words);
    std::vector<Packed_field> fields(output_values.size());
    size_t                    next = 0;
    for (size_t i = 0; i < output_values.size(); ++i) {
      if (!output_values[i].dest) {
        fields[i] = packed[next++];
      }
    }
    return fields;
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
  // A native color has no external symbols, so it cannot call the reporter:
  // it sets the site's `hit` byte (after its two string pointers) and the
  // caller polls it (kUndefinedWarnHelper's __lhd_undef_poll).
  void undefined_if(llvm::Value* hit, size_t site) {
    auto* yes  = llvm::BasicBlock::Create(context, "undefined.hit", function);
    auto* done = llvm::BasicBlock::Create(context, "undefined.done", function);
    builder.CreateCondBr(hit, yes, done, llvm::MDBuilder(context).createUnlikelyBranchWeights());
    builder.SetInsertPoint(yes);
    builder.CreateStore(builder.getInt8(1),
                        builder.CreateConstInBoundsGEP1_64(builder.getInt8Ty(), resource(site), 2 * sizeof(void*)));
    builder.CreateBr(done);
    builder.SetInsertPoint(done);
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

  llvm::AllocaInst* local_words(size_t words, llvm::StringRef name) {
    // One fixed allocation per invocation, even when the use follows a branch
    // or packed-word loop. Inlining must not introduce per-iteration stack
    // save/restore, and the array extent must be visible to SROA.
    auto&             entry = builder.GetInsertBlock()->getParent()->getEntryBlock();
    llvm::IRBuilder<> allocations(&entry, entry.getFirstInsertionPt());
    auto* buffer = allocations.CreateAlloca(llvm::ArrayType::get(builder.getInt64Ty(), std::max<size_t>(1, words)), nullptr, name);
    buffer->setAlignment(llvm::Align(alignof(uint64_t)));
    return buffer;
  }

  struct Array_local {
    uint32_t          bits       = 0;  // entry width
    uint32_t          store_bits = 0;  // natural storage width of one entry
    uint64_t          size       = 0;
    llvm::AllocaInst* storage    = nullptr;
  };
  std::vector<Array_local> arrays;

  llvm::Value* packed_alloca(llvm::Value* value, uint32_t width, llvm::StringRef name) {
    auto* words = local_words(word_count(width), name);
    store_packed(value, words, 0, width);
    return words;
  }

  void add_unroll_extra(uint64_t extra) {
    if (extra == 0) {
      return;
    }
    constexpr uint64_t cap = uint64_t{1} << 48;  // saturate: only compared against small budgets
    unroll_extra           = std::min(cap, unroll_extra + std::min(cap, extra));
    function->addFnAttr(kUnrolledExtra, std::to_string(unroll_extra));
  }

  Value remember(llvm::Value* value, uint32_t width, bool unsign) {
    const size_t id = values.size();
    values.push_back(value);
    deferred_casts.emplace_back();
    return Value{id, width, unsign};
  }

  llvm::Value* get(Value value) {
    if (value.id < input_types.size() && value.id < values.size() && values[value.id] == nullptr && value.width != 0 && !state_abi
        && input_storage[value.id]) {
      const auto width        = input_types[value.id].first;
      values[value.id]        = cast_integer(builder, load_storage(*input_storage[value.id], width, "in.storage"), width, true);
      input_handles[value.id] = values[value.id];
    }
    if (value.id < input_types.size() && value.id < values.size() && values[value.id] == nullptr && value.width != 0) {
      ensure_input_layout();
      const auto width  = input_types[value.id].first;
      const auto bit    = input_bits[value.id];
      auto*      loaded = load_packed(inputs, input_word_offsets[value.id], width + bit, "in");
      if (bit != 0) {
        loaded = builder.CreateLShr(loaded, bit);
      }
      values[value.id]        = cast_integer(builder, loaded, width, true);
      input_handles[value.id] = values[value.id];
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

Cgen_llvm::Cgen_llvm(std::string_view function_name, const State_layout& layout) {
  std::vector<std::pair<uint32_t, bool>> inputs;
  for (const auto& input : layout.inputs) {
    inputs.emplace_back(input.width, input.unsign);
  }
  impl_ = std::make_unique<Impl>(function_name, inputs, false, &layout);
}

Cgen_llvm::~Cgen_llvm()                               = default;
Cgen_llvm::Cgen_llvm(Cgen_llvm&&) noexcept            = default;
Cgen_llvm& Cgen_llvm::operator=(Cgen_llvm&&) noexcept = default;

std::vector<Cgen_llvm::Packed_field> Cgen_llvm::packed_layout(const std::vector<uint32_t>& widths, size_t* words) {
  const bool                word_abi = livehd::debug::env<"LHD_SIM_LLVM_WORD_ABI">();
  std::vector<Packed_field> fields;
  fields.reserve(widths.size());
  std::vector<uint32_t> users;  // narrow values per word
  size_t                word = 0;
  uint32_t              bit  = 0;
  for (const auto width : widths) {
    if (width > 64 || word_abi) {
      if (bit != 0) {
        ++word;
        bit = 0;
      }
      fields.push_back(Packed_field{word, 0, true});
      word += std::max<size_t>(1, word_count(width));
      continue;
    }
    if (bit + width > 64) {
      ++word;
      bit = 0;
    }
    fields.push_back(Packed_field{word, bit, true});
    users.resize(word + 1, 0);
    ++users[word];
    bit += width;
  }
  const size_t total = word + (bit != 0 ? 1 : 0);
  for (size_t i = 0; i < fields.size(); ++i) {
    if (widths[i] <= 64 && !word_abi && fields[i].word < users.size()) {
      fields[i].alone = users[fields[i].word] == 1;
    }
  }
  if (words != nullptr) {
    *words = total;
  }
  return fields;
}

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

Cgen_llvm::Value Cgen_llvm::concat(const std::vector<Value>& lanes) {
  std::vector<Integer_slice> slices;
  uint64_t                   total = 0;
  for (const auto lane : lanes) {
    auto* source = impl_->get(lane);
    if (source == nullptr || lane.width == 0) {
      return {};
    }
    total += lane.width;
    if (total > llvm::IntegerType::MAX_INT_BITS) {
      return {};
    }
    const auto slice = integer_slice(source, lane.width);
    source           = slice.source;
    const auto lo    = slice.lo;
    if (!slices.empty() && slices.back().source == source && slices.back().lo == lo + lane.width) {
      slices.back().lo     = lo;
      slices.back().width += lane.width;
    } else {
      slices.push_back({source, lo, lane.width});
    }
  }
  if (total == 0) {
    return {};
  }
  auto&        b         = impl_->builder;
  auto*        type      = b.getIntNTy(static_cast<uint32_t>(total));
  llvm::Value* result    = llvm::ConstantInt::get(type, 0);
  auto         remaining = static_cast<uint32_t>(total);
  for (const auto& slice : slices) {
    auto* value = slice.source;
    if (slice.lo) {
      value = b.CreateLShr(value, llvm::ConstantInt::get(value->getType(), slice.lo));
    }
    value      = cast_integer(b, value, slice.width, true);
    value      = cast_integer(b, value, static_cast<uint32_t>(total), true);
    remaining -= slice.width;
    if (remaining) {
      value = b.CreateShl(value, llvm::ConstantInt::get(type, remaining));
    }
    result = b.CreateOr(result, value);
  }
  return impl_->remember(result, static_cast<uint32_t>(total), true);
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
  if (impl_->inputs != nullptr && source.id < impl_->input_types.size() && impl_->input_types[source.id].first == source.width
      && (impl_->ensure_input_layout(), true) && !impl_->input_storage[source.id] && impl_->input_bits[source.id] == 0
      && (impl_->input_fields.empty() || impl_->input_fields[source.id].alone)) {
    base   = impl_->inputs;  // a kernel input: its packed words are already in the input buffer
    offset = impl_->input_word_offsets[source.id];
  } else {
    auto* value = impl_->get(source);
    if (value == nullptr) {
      return {};
    }
    base = impl_->packed_alloca(cast_integer(builder, value, source.width, source.unsign), source.width, "extract.words");
  }
  const auto count_bits = llvm::computeKnownBits(raw_count, impl_->module->getDataLayout());
  const auto alignment  = std::min(6u, count_bits.countMinTrailingZeros());
  if constexpr (std::endian::native == std::endian::little) {
    if ((len == 8 || len == 16 || len == 32 || len == 64) && len <= (uint32_t{1} << alignment) && source.width % len == 0) {
      // An array-style lane can load its native integer directly. The word
      // path otherwise needs an indexed i64 load followed by another variable
      // shift, even for the ubiquitous i*32 +: 32 mux/reduction loop.
      auto* index  = builder.CreateLShr(cast_integer(builder, raw_count, 64, true), std::countr_zero(len));
      auto* valid  = builder.CreateICmpULT(index, builder.getInt64(source.width / len));
      auto* safe   = builder.CreateSelect(valid, index, builder.getInt64(0));
      auto* start  = impl_->word_ptr(base, offset);
      auto* ptr    = builder.CreateInBoundsGEP(builder.getIntNTy(len), start, safe);
      auto* loaded = builder.CreateAlignedLoad(builder.getIntNTy(len), ptr, llvm::Align(len / 8));
      auto* lane   = builder.CreateSelect(valid, loaded, llvm::ConstantInt::get(builder.getIntNTy(len), 0));
      return impl_->remember(cast_integer(builder, lane, result_width, true), result_width, result_unsign);
    }
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
  auto*        low = load_word(word);
  llvm::Value* lane;
  if (len <= (uint32_t{1} << alignment) || nwords == 1) {
    // Aligned lanes (for example i*32 +: 32), like single bits, never
    // cross a word. Do not load an adjacent word and funnel-shift it only
    // to discard all its bits. A one-word source has no adjacent data.
    lane = builder.CreateLShr(low, shift, "extract.bit");
  } else {
    auto* high = load_word(builder.CreateAdd(word, llvm::ConstantInt::get(i64, 1)));
    auto* fshr = llvm::Intrinsic::getOrInsertDeclaration(impl_->module.get(), llvm::Intrinsic::fshr, {i64});
    lane       = builder.CreateCall(fshr, {high, low, shift}, "extract.lane");
  }
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
      auto&      b            = impl_->builder;
      auto*      i64          = b.getInt64Ty();
      const auto source_words = word_count(work_width);
      const auto output_words = word_count(result_width);
      left                    = cast_integer(b, left, work_width, lhs.unsign);
      auto* padded            = cast_integer(b, left, static_cast<unsigned>(source_words * 64), op != Binary_op::ashr);
      auto* function          = b.GetInsertBlock()->getParent();
      auto* source            = impl_->local_words(source_words, "shift.source");
      auto* output            = impl_->local_words(output_words, "shift.output");
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
  // is exactly the `cw += 1` the reference Slop lowering applies. EQ/NE compare
  // VALUES too: a same-sign pair needs no headroom, but a mixed-sign one does,
  // or `t0 == i1` with t0:S16 = wrap i1 compared the shared bit pattern and was
  // true for i1 >= 32768 (slop and Icarus: false; random Pyrope sim fuzz,
  // 2026-10-09).
  unsigned   operation_width = comparison ? std::max(lhs.width, rhs.width) : result_width;
  if ((ordered && (!lhs.unsign || !rhs.unsign)) || ((op == Binary_op::eq || op == Binary_op::ne) && lhs.unsign != rhs.unsign)) {
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
  // Module boundaries can hide a bus-wide mux from graph rewrite rules. Once
  // those instances are expanded here, aligned scalar arms are slices of the
  // same words. Share the selection before extracting each bit; LLVM can then
  // keep a word mux through the whole structured tree instead of hundreds of
  // independent bit selects. Unequal offsets/types retain ordinary lowering.
  if (result_width == 1) {
    const auto left        = integer_slice(on_false, 1);
    const auto right       = integer_slice(on_true, 1);
    const auto left_width  = left.source->getType()->getIntegerBitWidth();
    const auto right_width = right.source->getType()->getIntegerBitWidth();
    if (left_width == right_width && left_width > 1 && left_width <= 64) {
      // Scalar slices can be different rotated views of a word. Align the
      // true arm to the false arm's bit position before sharing the select.
      // Each returned bit is unchanged; neighboring lanes with the same
      // relative offset now reuse the whole selection, including wraparound.
      const uint32_t delta   = (right.lo + left_width - left.lo) % left_width;
      auto*          aligned = right.source;
      if (delta) {
        const auto key      = std::tuple{impl_->builder.GetInsertBlock(), right.source, delta};
        auto [it, inserted] = impl_->mux_rotations.try_emplace(key, nullptr);
        if (inserted) {
          auto* low  = impl_->builder.CreateLShr(aligned, llvm::ConstantInt::get(aligned->getType(), delta));
          auto* high = impl_->builder.CreateShl(aligned, llvm::ConstantInt::get(aligned->getType(), left_width - delta));
          it->second = impl_->builder.CreateOr(low, high);
        }
        aligned = it->second;
      }
      const auto key      = std::tuple{impl_->builder.GetInsertBlock(), condition, left.source, aligned};
      auto [it, inserted] = impl_->word_muxes.try_emplace(key, nullptr);
      if (inserted) {
        auto* enabled = impl_->builder.CreateICmpNE(condition, llvm::ConstantInt::get(condition->getType(), 0));
        it->second    = impl_->builder.CreateSelect(enabled, aligned, left.source);
      }
      auto* selected = it->second;
      if (left.lo) {
        selected = impl_->builder.CreateLShr(selected, llvm::ConstantInt::get(selected->getType(), left.lo));
      }
      return impl_->remember(cast_integer(impl_->builder, selected, 1, true), 1, result_unsign);
    }
    uint32_t width = 64;
    // Choose aligned machine-word chunks whose lane positions agree. This is
    // exact for every retained bit, including packed buses with different arm
    // offsets; no knowledge of module names or declared HDL word sizes is used.
    while (width > 1
           && (left.lo % width != right.lo % width || left.lo / width * width + width > left_width
               || right.lo / width * width + width > right_width)) {
      width /= 2;
    }
    if (width > 1) {
      auto word = [&](const Integer_slice& slice) {
        const uint32_t offset = slice.lo / width * width;
        const auto     key    = std::tuple{impl_->builder.GetInsertBlock(), slice.source, offset, width};
        auto [it, inserted]   = impl_->mux_words.try_emplace(key, nullptr);
        if (inserted) {
          auto* value = slice.source;
          if (auto* load = llvm::dyn_cast<llvm::LoadInst>(value);
              load && load->isSimple() && width >= 8 && std::endian::native == std::endian::little) {
            // A packed input is a wide load. Extract the word at that load's
            // original position so intervening stores cannot change its
            // snapshot. Byte-sized aligned chunks need no padding bits and
            // avoid LLVM turning selected slice offsets into wide shifts.
            llvm::IRBuilder<> at_load(load);
            auto*             ptr = at_load.CreateConstGEP1_64(at_load.getInt8Ty(), load->getPointerOperand(), offset / 8);
            it->second            = at_load.CreateAlignedLoad(at_load.getIntNTy(width),
                                                              ptr,
                                                              llvm::commonAlignment(load->getAlign(), offset / 8),
                                                              "mux.word");
          } else {
            if (offset) {
              value = impl_->builder.CreateLShr(value, llvm::ConstantInt::get(value->getType(), offset));
            }
            it->second = cast_integer(impl_->builder, value, width, true);
          }
        }
        return it->second;
      };
      auto*      left_word  = word(left);
      auto*      right_word = word(right);
      const auto key        = std::tuple{impl_->builder.GetInsertBlock(), condition, left_word, right_word};
      auto [it, inserted]   = impl_->word_muxes.try_emplace(key, nullptr);
      if (inserted) {
        auto* enabled = impl_->builder.CreateICmpNE(condition, llvm::ConstantInt::get(condition->getType(), 0));
        it->second    = impl_->builder.CreateSelect(enabled, right_word, left_word);
      }
      auto*          selected = it->second;
      const uint32_t bit      = left.lo % width;
      if (bit) {
        selected = impl_->builder.CreateLShr(selected, llvm::ConstantInt::get(selected->getType(), bit));
      }
      return impl_->remember(cast_integer(impl_->builder, selected, 1, true), 1, result_unsign);
    }
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
  std::vector<llvm::Value*> active;
  active.reserve(inputs.size() / 2);
  for (size_t i = 0; i + 1 < inputs.size(); i += 2) {
    auto* control = impl_->get(inputs[i]);
    if (control == nullptr) {
      return {};
    }
    active.push_back(impl_->builder.CreateICmpNE(control, llvm::ConstantInt::get(control->getType(), 0)));
  }
  // Fold from the LAST arm back so the FIRST active control wins -- the priority
  // cgen_verilog's `unique case`, the SMT encoders and the slop backend use.
  // Overlapping controls break the cell's one-hot contract (pass.formal reports
  // it), but simulation RESOLVES them to the first active arm, it never stops:
  // a `unique if` whose conditions overlap at run time (picorv32's ALU result
  // under BARREL_SHIFTER + TWO_CYCLE_ALU, which Verilator simulates first-match)
  // used to reach a trap here and kill the driver with SIGILL.
  for (size_t k = active.size(); k-- > 0;) {
    auto* value = impl_->get(inputs[2 * k + 1]);
    if (value == nullptr) {
      return {};
    }
    value  = cast_integer(impl_->builder, value, result_width, inputs[2 * k + 1].unsign);
    result = impl_->builder.CreateSelect(active[k], value, result);
  }
  return impl_->remember(result, result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::indexed_mux(Value select, const std::vector<Value>& arms, uint32_t result_width, bool result_unsign) {
  auto* selector = impl_->get(select);
  if (selector == nullptr || arms.empty() || result_width == 0) {
    return {};
  }
  // A selector past the last arm (a `match` without `else` that sees an
  // unlisted key, a reset-time value) reads X; two-state native code reads it
  // as 0, the fold's seed, as sim.unknown_zero does. It must not stop the run:
  // a trap here was a SIGILL with no diagnostic.
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

size_t Cgen_llvm::array_create(uint32_t bits, uint64_t size) {
  Impl::Array_local a;
  a.bits       = std::max<uint32_t>(bits, 1);
  a.store_bits = a.bits <= 8 ? 8 : a.bits <= 16 ? 16 : a.bits <= 32 ? 32 : static_cast<uint32_t>(word_count(a.bits) * 64);
  a.size       = std::max<uint64_t>(size, 1);
  auto&             entry = impl_->builder.GetInsertBlock()->getParent()->getEntryBlock();
  llvm::IRBuilder<> allocations(&entry, entry.getFirstInsertionPt());
  a.storage = allocations.CreateAlloca(llvm::ArrayType::get(impl_->builder.getIntNTy(a.store_bits), a.size), nullptr, "array");
  a.storage->setAlignment(llvm::Align(a.store_bits >= 64 ? 8 : a.store_bits / 8));
  impl_->arrays.push_back(a);
  return impl_->arrays.size() - 1;
}

bool Cgen_llvm::array_seed(size_t array, Value base) {
  if (array >= impl_->arrays.size() || base.width == 0) {
    return false;
  }
  const auto& a     = impl_->arrays[array];
  auto&       b     = impl_->builder;
  auto*       value = impl_->get(base);
  if (value == nullptr) {
    return false;
  }
  const auto whole = static_cast<uint32_t>(a.bits * a.size);
  value            = cast_integer(b, value, whole, base.unsign);
  if (a.store_bits == a.bits) {
    // Entry i sits at bits [i*bits, (i+1)*bits) of the packed value, which is
    // exactly its little-endian place in the array: one wide store.
    b.CreateAlignedStore(value, a.storage, llvm::Align(1));
    return true;
  }
  for (uint64_t i = 0; i < a.size; ++i) {
    auto* entry = b.CreateLShr(value, llvm::ConstantInt::get(value->getType(), i * a.bits));
    entry       = cast_integer(b, cast_integer(b, entry, a.bits, true), a.store_bits, true);
    b.CreateStore(entry, b.CreateConstInBoundsGEP2_64(a.storage->getAllocatedType(), a.storage, 0, i));
  }
  return true;
}

bool Cgen_llvm::array_write(size_t array, Value enable, Value address, Value data) {
  if (array >= impl_->arrays.size() || enable.width == 0 || address.width == 0 || data.width == 0) {
    return false;
  }
  const auto& a     = impl_->arrays[array];
  auto&       b     = impl_->builder;
  auto*       wen   = impl_->get(enable);
  auto*       value = impl_->get(data);  // materialized before the branch: later uses must be dominated
  auto*       index = cast_integer(b, impl_->get(address), 64, address.unsign);
  if (wen == nullptr || value == nullptr) {
    return false;
  }
  auto* valid = b.CreateAnd(impl_->valid_address(address, a.size), b.CreateICmpNE(wen, llvm::ConstantInt::get(wen->getType(), 0)));
  auto* entry = cast_integer(b, cast_integer(b, value, a.bits, data.unsign), a.store_bits, true);
  impl_->when(valid, [&] {
    b.CreateStore(entry, b.CreateInBoundsGEP(a.storage->getAllocatedType(), a.storage, {b.getInt64(0), index}));
  });
  return true;
}

Cgen_llvm::Value Cgen_llvm::array_read(size_t array, Value address, uint32_t result_width, bool result_unsign) {
  if (array >= impl_->arrays.size() || address.width == 0 || result_width == 0) {
    return {};
  }
  const auto& a     = impl_->arrays[array];
  auto&       b     = impl_->builder;
  auto*       valid = impl_->valid_address(address, a.size);
  auto*       index = b.CreateSelect(valid, cast_integer(b, impl_->get(address), 64, address.unsign), b.getInt64(0));
  llvm::Value* value
      = b.CreateLoad(b.getIntNTy(a.store_bits), b.CreateInBoundsGEP(a.storage->getAllocatedType(), a.storage, {b.getInt64(0), index}));
  value = cast_integer(b, value, a.bits, true);
  value = b.CreateSelect(valid, value, llvm::ConstantInt::get(value->getType(), 0));  // out of range reads 0
  return impl_->remember(cast_integer(b, value, result_width, result_unsign), result_width, result_unsign);
}

Cgen_llvm::Value Cgen_llvm::array_read_all(size_t array, uint32_t result_width, bool result_unsign) {
  if (array >= impl_->arrays.size() || result_width == 0) {
    return {};
  }
  const auto&  a     = impl_->arrays[array];
  auto&        b     = impl_->builder;
  const auto   whole = static_cast<uint32_t>(a.bits * a.size);
  llvm::Value* value = nullptr;
  if (a.store_bits == a.bits) {
    value = b.CreateAlignedLoad(b.getIntNTy(whole), a.storage, llvm::Align(1));
  } else {
    value = llvm::ConstantInt::get(b.getIntNTy(whole), 0);
    for (uint64_t i = 0; i < a.size; ++i) {
      llvm::Value* entry
          = b.CreateLoad(b.getIntNTy(a.store_bits), b.CreateConstInBoundsGEP2_64(a.storage->getAllocatedType(), a.storage, 0, i));
      entry = cast_integer(b, cast_integer(b, entry, a.bits, true), whole, true);
      value = b.CreateOr(value, b.CreateShl(entry, llvm::ConstantInt::get(entry->getType(), i * a.bits)));
    }
  }
  return impl_->remember(cast_integer(b, value, result_width, result_unsign), result_width, result_unsign);
}

void Cgen_llvm::bind_memory(std::string_view symbol, const Memory& memory) { impl_->memories.emplace(symbol, memory); }

bool Cgen_llvm::memory_commit(std::string_view symbol) {
  const auto found = impl_->memories.find(std::string(symbol));
  if (found == impl_->memories.end()) {
    impl_->error = "LLVM memory commit references an unbound memory";
    return false;
  }
  impl_->commit_memory(found->second);
  return true;
}

Cgen_llvm::Value Cgen_llvm::memory_read(std::string_view symbol, Value address, uint32_t result_width, bool result_unsign) {
  const auto it = impl_->memories.find(std::string(symbol));
  if (it == impl_->memories.end() || address.width == 0 || result_width == 0) {
    return {};
  }
  const auto& m     = it->second;
  auto&       b     = impl_->builder;
  auto*       valid = impl_->valid_address(address, m.size);
  if (m.warn_site != no_site) {
    impl_->undefined_if(b.CreateNot(valid), m.warn_site);
  }
  auto* index = cast_integer(b, impl_->get(address), 64, address.unsign);
  auto* safe  = b.CreateSelect(valid, index, b.getInt64(0));
  auto* ptr   = b.CreateInBoundsGEP(b.getInt64Ty(), impl_->resource(m.data), b.CreateMul(safe, b.getInt64(word_count(m.bits))));
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
  auto* in_range = impl_->valid_address(address, m.size);
  auto* enabled  = b.CreateICmpNE(mask, llvm::ConstantInt::get(mask_type, 0));
  if (m.gated) {
    enabled = b.CreateAnd(enabled, b.CreateICmpNE(b.CreateLoad(b.getInt64Ty(), impl_->resource(m.gate)), b.getInt64(0)));
  }
  if (m.warn_site != no_site) {
    impl_->undefined_if(b.CreateAnd(enabled, b.CreateNot(in_range)), m.warn_site);
  }
  auto* valid = b.CreateAnd(in_range, enabled);
  if (m.direct_write) {
    // ABI inputs are cached lazily. Materialize the data before branching so
    // later reads/writes sharing this input are dominated by its definition.
    auto* write_data = impl_->get(data);
    auto* write      = llvm::BasicBlock::Create(impl_->context, "memory.write", impl_->function);
    auto* done       = llvm::BasicBlock::Create(impl_->context, "memory.write.done", impl_->function);
    b.CreateCondBr(valid, write, done);
    b.SetInsertPoint(write);
    auto*        index = cast_integer(b, impl_->get(address), 64, address.unsign);
    auto*        ptr   = b.CreateGEP(b.getInt64Ty(), impl_->resource(m.data), b.CreateMul(index, b.getInt64(word_count(m.bits))));
    llvm::Value* next  = write_data;
    if (m.lanes > 1) {
      auto* old = impl_->load_packed(ptr, 0, m.bits, "memory.old");
      next      = b.CreateOr(b.CreateAnd(old, b.CreateNot(mask)), b.CreateAnd(write_data, mask));
    }
    impl_->store_packed(next, ptr, 0, m.bits);
    b.CreateBr(done);
    b.SetInsertPoint(done);
    return true;
  }
  auto* pending = impl_->pending_ptr(m, m.port);
  impl_->store_packed(impl_->get(data), pending, 0, m.bits);
  impl_->store_packed(mask, pending, word_count(m.bits), m.bits);
  impl_->store_packed(llvm::ConstantInt::get(mask_type, 0), pending, 2 * word_count(m.bits), m.bits);
  b.CreateStore(cast_integer(b, impl_->get(address), 64, address.unsign), impl_->word_ptr(pending, 3 * word_count(m.bits)));
  b.CreateStore(b.CreateZExt(valid, b.getInt8Ty()), impl_->fired_ptr(m, pending));
  return true;
}

bool Cgen_llvm::undefined_if(Value condition, size_t site) {
  auto* hit = impl_->get(condition);
  if (hit == nullptr) {
    return false;
  }
  impl_->undefined_if(impl_->builder.CreateICmpNE(hit, llvm::ConstantInt::get(hit->getType(), 0)), site);
  return true;
}

bool Cgen_llvm::add_output(size_t index, Value value, std::string& error) {
  if (impl_->state_abi) {
    error = "state ABI requires add_state_output";
    return false;
  }
  if (value.width == 0 || impl_->get(value) == nullptr) {
    error = impl_->error.empty() ? "invalid LLVM color output" : impl_->error;
    return false;
  }
  // Retain the publication point until the complete output layout is known.
  // Packed buffers are disjoint from inputs/resources, so publishing here
  // cannot change a later computation. Deferring every store/pack to the
  // return kept all independent outputs live together (and caused spills).
  // This marker is removed by seal, before verification or optimization.
  auto* anchor = new llvm::FreezeInst(impl_->get(value), "output.anchor", impl_->builder.GetInsertBlock());
  impl_->output_values.push_back({index, value, anchor});
  return true;
}

bool Cgen_llvm::bind_storage_input(size_t index, const Storage& storage, std::string& error) {
  if (impl_->state_abi || impl_->scalar_abi || index >= impl_->input_types.size() || storage.width == 0
      || storage.width != impl_->input_types[index].first) {
    error = "invalid LLVM storage input";
    return false;
  }
  if (impl_->input_layout_ready || impl_->values[index] != nullptr) {
    error = "LLVM storage inputs must be bound before any input is used";
    return false;
  }
  impl_->input_storage[index] = storage;
  return true;
}

bool Cgen_llvm::is_storage_input(size_t index) const {
  return index < impl_->input_storage.size() && impl_->input_storage[index].has_value();
}

bool Cgen_llvm::add_storage_output(size_t index, Value value, const Storage& dest, const std::optional<Storage>& compare,
                                   std::string& error, bool report_changed, const std::vector<Change_effect>& effects,
                                   std::optional<Value> guard) {
  if (guard && (guard->width != 1 || impl_->get(*guard) == nullptr)) {
    error = "invalid LLVM storage output guard";
    return false;
  }
  if (impl_->state_abi || impl_->scalar_abi || dest.width == 0 || (compare && compare->width != dest.width)) {
    error = "invalid LLVM storage output";
    return false;
  }
  if (value.width == 0 || impl_->get(value) == nullptr) {
    error = impl_->error.empty() ? "invalid LLVM color output" : impl_->error;
    return false;
  }
  // An object this kernel also reads (an in-place register's Q): load the
  // old value now, so every read of it -- earlier or later -- sees the value
  // before this store. The store then publishes here like any output instead
  // of waiting for the end, which kept every in-place next-state live across
  // the whole kernel (picorv32: 57 registers, a 4 KB spill frame).
  // The same OBJECT, not merely the same table entry: one native frame base
  // serves every register and slot (Storage::offset), and matching on the
  // resource alone loaded every input before the first store (picorv32: all
  // loads at the kernel entry, +250 spills).
  const auto bytes_of = [&](const Storage& st) {
    const size_t begin = st.offset + st.element * Impl::storage_bytes(st);
    return std::pair{begin, begin + Impl::storage_bytes(st)};
  };
  const auto [dest_begin, dest_end] = bytes_of(dest);
  for (size_t input = 0; input < impl_->input_storage.size(); ++input) {
    const auto& in = impl_->input_storage[input];
    if (!in || in->resource != dest.resource) {
      continue;
    }
    const auto [in_begin, in_end] = bytes_of(*in);
    if (in_begin < dest_end && dest_begin < in_end) {
      (void)impl_->get(this->input(input));
    }
  }
  auto* anchor = new llvm::FreezeInst(impl_->get(value), "output.anchor", impl_->builder.GetInsertBlock());
  impl_->output_values.push_back({index, value, anchor, dest, compare, report_changed || !effects.empty(), effects, guard});
  return true;
}

bool Cgen_llvm::speculate_input(Value value, uint64_t inactive) {
  // Look through widening resizes: the input then holds the same value.
  while (value.id < impl_->deferred_casts.size() && impl_->deferred_casts[value.id] && value.id >= impl_->input_types.size()) {
    const auto source = *impl_->deferred_casts[value.id];
    if (source.width > value.width) {
      return false;  // a narrowing view does not pin the whole input
    }
    value = source;
  }
  if (value.id >= impl_->input_types.size() || value.width == 0 || value.width > 64 || impl_->state_abi || impl_->scalar_abi
      || (value.width < 64 && (inactive >> value.width) != 0)) {
    return false;
  }
  const auto [it, inserted] = impl_->speculations.try_emplace(value.id, inactive);
  if (!inserted && it->second != inactive) {
    impl_->speculations.erase(it);  // two readings of one net disagree: leave it dynamic
    return false;
  }
  return true;
}

bool Cgen_llvm::add_state_output(size_t word, size_t previous_word, Value value, const std::vector<Dirty_mask>& dirty,
                                 std::string& error) {
  const auto fits
      = [&](size_t offset) { return offset <= impl_->state_words && word_count(value.width) <= impl_->state_words - offset; };
  if (!impl_->state_abi || value.width == 0 || !fits(word) || !fits(previous_word) || impl_->get(value) == nullptr) {
    error = "invalid LLVM state output";
    return false;
  }
  for (const auto& mask : dirty) {
    if (mask.word >= impl_->state_words) {
      error = "LLVM dirty mask is outside its allocation";
      return false;
    }
  }
  impl_->state_outputs.push_back({word, previous_word, value, dirty});
  return true;
}

bool Cgen_llvm::copy_state(size_t destination, size_t source, size_t words, std::string& error) {
  if (!impl_->state_abi || destination > impl_->state_words || source > impl_->state_words
      || words > impl_->state_words - destination || words > impl_->state_words - source
      || words > std::numeric_limits<uint64_t>::max() / sizeof(uint64_t)) {
    error = "LLVM state copy is outside its allocation";
    return false;
  }
  if (words == 0 || destination == source) {
    return true;
  }
  if (destination < source + words && source < destination + words) {
    error = "LLVM state copy spans overlap";
    return false;
  }
  impl_->builder.CreateMemCpy(impl_->word_ptr(impl_->outputs, destination),
                              llvm::Align(8),
                              impl_->word_ptr(impl_->inputs, source),
                              llvm::Align(8),
                              words * sizeof(uint64_t));
  return true;
}

size_t Cgen_llvm::instruction_count() const {
  size_t count = 0;
  if (impl_ && impl_->module) {
    for (const auto& fn : *impl_->module) {
      for (const auto& block : fn) {
        count += block.size();
      }
    }
  }
  return count;
}

bool Cgen_llvm::write_object(std::string_view path, std::string& error, bool track_changed, std::string_view cache_path,
                             bool* reused) {
  impl_->cache_path   = cache_path;
  impl_->cache_reused = reused;
  if (reused != nullptr) {
    *reused = false;
  }
  return write_module(path, error, track_changed, true);
}

bool Cgen_llvm::write_state_object(std::string_view path, std::string_view descriptor, size_t public_words,
                                   const std::vector<livehd::sim::Native_initial_word>& initial, std::string& error) {
  if (!impl_->state_abi || public_words > impl_->state_words || descriptor.empty()
      || impl_->module->getNamedValue(llvm::StringRef(descriptor)) != nullptr) {
    error = "invalid LLVM state descriptor";
    return false;
  }
  auto sorted = initial;
  std::ranges::sort(sorted, {}, &livehd::sim::Native_initial_word::word);
  for (size_t i = 0; i < sorted.size(); ++i) {
    if (sorted[i].word >= impl_->state_words || (i != 0 && sorted[i - 1].word == sorted[i].word)) {
      error = "invalid LLVM state initializer";
      return false;
    }
  }
  std::erase_if(sorted, [](const auto& word) { return word.value == 0; });

  auto&                        builder   = impl_->builder;
  auto*                        word_type = llvm::StructType::get(builder.getInt64Ty(), builder.getInt64Ty());
  std::vector<llvm::Constant*> words;
  words.reserve(sorted.size());
  for (const auto& word : sorted) {
    words.push_back(llvm::ConstantStruct::get(word_type, {builder.getInt64(word.word), builder.getInt64(word.value)}));
  }
  llvm::Constant* defaults = llvm::ConstantPointerNull::get(builder.getPtrTy());
  if (!words.empty()) {
    auto* values = llvm::ConstantArray::get(llvm::ArrayType::get(word_type, words.size()), words);
    auto* table  = new llvm::GlobalVariable(*impl_->module,
                                            values->getType(),
                                            true,
                                            llvm::GlobalValue::PrivateLinkage,
                                            values,
                                            "state.defaults");
    table->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    defaults = table;
  }
  auto* image_type = llvm::StructType::get(builder.getInt64Ty(),
                                           builder.getInt64Ty(),
                                           builder.getInt64Ty(),
                                           builder.getInt64Ty(),
                                           builder.getPtrTy(),
                                           builder.getPtrTy());
  auto* image      = llvm::ConstantStruct::get(image_type,
                                               {builder.getInt64(livehd::sim::native_state_abi),
                                                builder.getInt64(impl_->state_words),
                                                builder.getInt64(public_words),
                                                builder.getInt64(words.size()),
                                                defaults,
                                                impl_->function});
  new llvm::GlobalVariable(*impl_->module,
                           image_type,
                           true,
                           llvm::GlobalValue::ExternalLinkage,
                           image,
                           llvm::StringRef(descriptor));
  impl_->function->setLinkage(llvm::GlobalValue::InternalLinkage);
  return write_module(path, error, true, true);
}

bool Cgen_llvm::write_bitcode(std::string_view path, std::string& error, bool track_changed) {
  return write_module(path, error, track_changed, false);
}
bool Cgen_llvm::seal(std::string& error, bool track_changed) {
  if (impl_->sealed_changed) {
    if (*impl_->sealed_changed != track_changed) {
      error = "LLVM color changed-bit ABI cannot change after finalization";
      return false;
    }
    return true;
  }

  if (!impl_->error.empty()) {
    error = impl_->error;
    return false;
  }
  const auto publication_order = impl_->output_values;
  std::ranges::sort(impl_->output_values, {}, &Impl::Output::index);
  for (size_t i = 0; i < impl_->output_values.size(); ++i) {
    if (impl_->output_values[i].index != i) {
      error = "LLVM color outputs must be unique and densely indexed";
      return false;
    }
  }
  if (impl_->state_abi) {
    // Values have already been materialized by add_state_output. Snapshot all
    // comparisons before storing: a swap or pending/current alias must not
    // observe another output's new value. Accumulate dirty bits in SSA once.
    std::map<size_t, llvm::Value*> changed;
    for (const auto& output : impl_->state_outputs) {
      if (output.dirty.empty()) {
        continue;
      }
      auto* old       = impl_->load_packed(impl_->inputs, output.previous_word, output.value.width, "state.old");
      auto* different = impl_->builder.CreateICmpNE(old, impl_->get(output.value));
      for (const auto& mask : output.dirty) {
        auto* bits = impl_->builder.CreateSelect(different, impl_->builder.getInt64(mask.mask), impl_->builder.getInt64(0));
        auto [it, inserted] = changed.try_emplace(mask.word, bits);
        if (!inserted) {
          it->second = impl_->builder.CreateOr(it->second, bits);
        }
      }
    }
    for (const auto& output : impl_->state_outputs) {
      impl_->store_packed(impl_->get(output.value), impl_->outputs, output.word, output.value.width);
    }
    for (const auto& [word, bits] : changed) {
      auto* ptr = impl_->word_ptr(impl_->outputs, word);
      auto* old = impl_->builder.CreateLoad(impl_->builder.getInt64Ty(), ptr);
      impl_->builder.CreateStore(impl_->builder.CreateOr(old, bits), ptr);
    }
    impl_->builder.CreateRetVoid();
  } else if (impl_->scalar_abi) {
    uint32_t     total_bits = 0;
    llvm::Value* result     = impl_->builder.getInt64(0);
    for (const auto& output : impl_->output_values) {
      if (output.dest) {
        error = "scalar LLVM ABI cannot publish into direct storage";
        return false;
      }
      if (output.value.width > 64 - total_bits) {
        error = "scalar LLVM ABI outputs exceed 64 packed bits";
        return false;
      }
      auto* value  = cast_integer(impl_->builder, impl_->get(output.value), 64, true);
      result       = impl_->builder.CreateOr(result, impl_->builder.CreateShl(value, total_bits));
      total_bits  += output.value.width;
    }
    if (total_bits == 0) {
      error = "scalar LLVM ABI requires an output";
      return false;
    }
    impl_->builder.CreateRet(result);
  } else {
    auto*        i64           = impl_->builder.getInt64Ty();
    const size_t changed_words = (impl_->output_values.size() + 63) / 64;
    // The return exists first: a guarded output splits blocks, and the tail
    // code below goes right before the return wherever it ends up.
    auto*        ret           = impl_->builder.CreateRetVoid();
    impl_->builder.SetInsertPoint(ret);
    const bool publish_early = impl_->output_values.size() > 1;
    // Words shared by several narrow outputs (packed_layout) are zeroed up
    // front, after their previous values are read for the changed bits, and
    // every output ORs its field in where it is published.
    const auto fields        = impl_->output_fields(nullptr);
    const bool any_storage   = std::ranges::any_of(impl_->output_values, [](const auto& o) { return o.dest.has_value(); });
    std::map<size_t, llvm::Value*> old_words;  // shared word -> its value on entry
    {
      auto& entry = impl_->function->getEntryBlock();
      if (publish_early || track_changed) {
        impl_->builder.SetInsertPoint(&entry, entry.getFirstInsertionPt());
      }
      for (const auto& field : fields) {
        if (!field.alone && !old_words.contains(field.word)) {
          old_words[field.word] = track_changed
                                      ? impl_->builder.CreateLoad(i64, impl_->word_ptr(impl_->outputs, field.word), "out.old.word")
                                      : nullptr;
        }
      }
      for (const auto& [word, unused] : old_words) {
        (void)unused;
        impl_->builder.CreateStore(llvm::ConstantInt::get(i64, 0), impl_->word_ptr(impl_->outputs, word));
      }
      // A storage output always reports its changed bit (the adapter's dirty
      // and commit logic reads it), whatever track_changed says. The bits are
      // accumulated locally and every word is written at the end (below).
      if (!publish_early) {
        impl_->builder.SetInsertPoint(ret);
      }
    }
    // Changed bits accumulate in a local array SROA keeps in registers, and
    // reach the caller's buffer once, at the end: a load/or/store on the
    // caller's word per output was a third of a large kernel's memory traffic.
    llvm::Value* changed_acc = nullptr;
    if (any_storage || track_changed) {
      changed_acc             = impl_->local_words(changed_words, "changed.acc");
      auto&             entry = impl_->function->getEntryBlock();
      llvm::IRBuilder<> init(&entry, std::next(llvm::cast<llvm::Instruction>(changed_acc)->getIterator()));
      for (size_t word = 0; word < changed_words; ++word) {
        init.CreateStore(llvm::ConstantInt::get(i64, 0), init.CreateConstInBoundsGEP1_64(i64, changed_acc, word));
      }
    }
    const auto set_changed = [&](size_t index, llvm::Value* diff) {
      auto*        ptr  = impl_->builder.CreateConstInBoundsGEP1_64(i64, changed_acc, index / 64);
      llvm::Value* bits = impl_->builder.CreateLoad(i64, ptr, "changed.old");
      auto*        flag = llvm::ConstantInt::get(i64, uint64_t{1} << (index % 64));
      bits              = impl_->builder.CreateOr(bits, impl_->builder.CreateSelect(diff, flag, llvm::ConstantInt::get(i64, 0)));
      impl_->builder.CreateStore(bits, ptr);
    };
    // Change effects accumulate per target (an OR mask, or a flag) in SSA
    // and reach memory once, at the end (below).
    using Effect_key = std::tuple<size_t, size_t, bool>;  // resource, byte offset, flag
    std::map<Effect_key, llvm::Value*> effect_acc;
    // A target only one output marks (a staged flop's commit flag) is updated
    // at that output: accumulating one value per such flag kept ~150 live
    // registers across picorv32's largest kernel. Shared targets (dirty
    // words) accumulate.
    std::map<Effect_key, size_t>       effect_users;
    for (const auto& output : impl_->output_values) {
      for (const auto& effect : output.effects) {
        ++effect_users[{effect.resource, effect.offset, effect.flag}];
      }
    }
    const auto apply_effect = [&](const Effect_key& key, llvm::Value* bits) {
      const auto [index, offset, flag] = key;
      const std::pair<size_t, size_t> resource{index, offset};
      auto* slot = impl_->builder.CreateConstInBoundsGEP1_64(impl_->builder.getPtrTy(), impl_->owner, index);
      auto* base = impl_->builder.CreateLoad(impl_->builder.getPtrTy(), slot, "effect");
      auto* ptr  = offset == 0 ? base : impl_->builder.CreateConstInBoundsGEP1_64(impl_->builder.getInt8Ty(), base, offset);
      if (flag) {  // a C++ bool: one byte, 0 or 1
        auto* old = impl_->builder.CreateLoad(impl_->builder.getInt8Ty(), ptr, "effect.flag");
        auto* set = impl_->builder.CreateOr(old, impl_->builder.CreateZExt(bits, impl_->builder.getInt8Ty()));
        impl_->storage_accesses.emplace_back(old, resource);
        impl_->storage_accesses.emplace_back(impl_->builder.CreateStore(set, ptr), resource);
      } else {
        auto* old = impl_->builder.CreateLoad(i64, ptr, "effect.word");
        impl_->storage_accesses.emplace_back(old, resource);
        impl_->storage_accesses.emplace_back(impl_->builder.CreateStore(impl_->builder.CreateOr(old, bits), ptr), resource);
      }
    };
    // Compare (against the old object, or `compare`), report, then store.
    // A guarded output does all of it inside `if (guard)`; its shared effects
    // are applied there too (an SSA accumulator would not dominate the end).
    const auto publish_storage = [&](const Impl::Output& output) {
      auto* value = impl_->get(output.value);
      if (output.guard) {
        auto* cond = impl_->get(*output.guard);
        if (cond->getType() != impl_->builder.getInt1Ty()) {
          cond = impl_->builder.CreateTrunc(cond, impl_->builder.getInt1Ty());
        }
        auto* then_term = llvm::SplitBlockAndInsertIfThen(cond, impl_->builder.GetInsertPoint(), false);
        impl_->builder.SetInsertPoint(then_term);
      }
      if (output.report_changed) {
        auto* diff = impl_->storage_differs(output.compare.value_or(*output.dest), value);
        set_changed(output.index, diff);
        for (const auto& effect : output.effects) {
          auto* term = effect.flag
                           ? diff
                           : impl_->builder.CreateSelect(diff, impl_->builder.getInt64(effect.mask), impl_->builder.getInt64(0));
          const Effect_key key{effect.resource, effect.offset, effect.flag};
          if (effect_users[key] == 1 || output.guard) {
            apply_effect(key, term);
            continue;
          }
          auto [it, inserted] = effect_acc.try_emplace(key, term);
          if (!inserted) {
            it->second = impl_->builder.CreateOr(it->second, term);
          }
        }
      }
      impl_->store_storage(*output.dest, value);
      if (output.guard) {
        impl_->builder.SetInsertPoint(impl_->builder.GetInsertBlock()->getSingleSuccessor()->getFirstNonPHIIt());
      }
    };
    // An object this kernel also reads was loaded before its output's anchor
    // (add_storage_output), so every storage output publishes in place.
    for (const auto& output : publication_order) {
      if (publish_early) {
        impl_->builder.SetInsertPoint(output.anchor);
      }
      if (output.dest) {
        publish_storage(output);
        continue;
      }
      const auto& field = fields[output.index];
      auto*       value = impl_->get(output.value);
      if (track_changed) {
        llvm::Value* old = nullptr;
        if (field.alone) {
          old = impl_->load_packed(impl_->outputs, field.word, output.value.width, "out.old");
        } else {
          old = cast_integer(impl_->builder, impl_->builder.CreateLShr(old_words[field.word], field.bit), output.value.width, true);
        }
        set_changed(output.index, impl_->builder.CreateICmpNE(old, value));
      }
      impl_->store_field(value, impl_->outputs, field, output.value.width);
    }
    impl_->builder.SetInsertPoint(ret);
    for (const auto& [key, bits] : effect_acc) {
      apply_effect(key, bits);
    }
    if (changed_acc != nullptr) {
      for (size_t word = 0; word < changed_words; ++word) {
        auto* bits
            = impl_->builder.CreateLoad(i64, impl_->builder.CreateConstInBoundsGEP1_64(i64, changed_acc, word), "changed.bits");
        impl_->builder.CreateStore(bits, impl_->builder.CreateConstInBoundsGEP1_64(i64, impl_->changed, word));
      }
    }
  }

  for (auto& output : impl_->output_values) {
    output.anchor->eraseFromParent();
    output.anchor = nullptr;
  }
  impl_->scope_storage();

  if (llvm::verifyModule(*impl_->module, &llvm::errs())) {
    error = "LLVM rejected the generated simulator module";
    return false;
  }
  impl_->sealed_changed = track_changed;
  return true;
}

bool Cgen_llvm::inline_body(const Inline_body& body, const std::vector<Value>& inputs, std::vector<Value>& outputs,
                            std::string& error) {
  if (inputs.size() != body.inputs.size()) {
    error = "inline color input count mismatch";
    return false;
  }
  auto parsed = llvm::parseBitcodeFile(llvm::MemoryBufferRef(body.bitcode, "inline.color"), impl_->context);
  if (!parsed) {
    error = llvm::toString(parsed.takeError());
    return false;
  }
  auto* source = (*parsed)->getFunction("__lhd_shared_color");
  if (!source) {
    error = "inline color has no body";
    return false;
  }
  // Privatize before linking, so repeated instances cannot resolve to another
  // occurrence's definition. Only the inlined instructions survive.
  source->setName("__lhd_inline_color");
  for (auto& fn : **parsed) {
    if (!fn.isDeclaration() && &fn != source) {
      fn.setLinkage(llvm::GlobalValue::InternalLinkage);
    }
  }
  if (llvm::Linker::linkModules(*impl_->module, std::move(*parsed))) {
    error = "could not import inline color";
    return false;
  }
  // The linker may move or replace definitions; locate the unique imported
  // name (the previous import is erased below).
  auto* callee = impl_->module->getFunction("__lhd_inline_color");
  if (!callee) {
    error = "imported inline color is missing";
    return false;
  }
  impl_->add_unroll_extra(unrolled_extra(*callee));
  auto&                           b   = impl_->builder;
  auto*                           ptr = llvm::PointerType::getUnqual(impl_->context);
  llvm::SmallVector<llvm::Value*> args;
  size_t                          input_words = 0, output_words = 0;
  const auto                      in_fields  = packed_layout(widths_of(body.inputs), &input_words);
  const auto                      out_fields = packed_layout(widths_of(body.outputs), &output_words);
  auto*                           packed_in  = impl_->local_words(input_words, "inline.inputs");
  auto*                           packed_out = impl_->local_words(output_words, "inline.outputs");
  auto* changed = impl_->local_words(word_count(static_cast<uint32_t>(body.outputs.size())), "inline.changed");
  if (!body.scalar) {
    impl_->zero_words(packed_in, input_words);
  }
  for (size_t i = 0; i < inputs.size(); ++i) {
    auto* value = impl_->get(resize(inputs[i], body.inputs[i].first, body.inputs[i].second));
    if (body.scalar) {
      args.push_back(cast_integer(b, value, 64, true));
    } else {
      impl_->store_field(value, packed_in, in_fields[i], body.inputs[i].first);
    }
  }
  // Changed tracking is dead after inlining, but its old-output reads must
  // still have defined values before dead-code elimination.
  for (size_t i = 0; i < output_words; ++i) {
    b.CreateStore(b.getInt64(0), b.CreateConstInBoundsGEP1_64(b.getInt64Ty(), packed_out, i));
  }
  if (!body.scalar) {
    args = {packed_in, packed_out, changed};
  }
  args.push_back(llvm::ConstantPointerNull::get(ptr));
  auto*    call = b.CreateCall(callee, args);
  uint32_t bit  = 0;
  outputs.clear();
  for (size_t i = 0; i < body.outputs.size(); ++i) {
    const auto [width, unsign] = body.outputs[i];
    auto* value                = body.scalar ? cast_integer(b, b.CreateLShr(call, bit), width, true)
                                             : impl_->load_field(packed_out, out_fields[i], width, "inline.result");
    // Inlining replaces and deletes a scalar call. Keep an instruction user
    // whose operand is rewritten, rather than remembering the deleted call.
    outputs.push_back(impl_->remember(b.CreateFreeze(value), width, unsign));
    bit += width;
  }
  auto*                    continuation = b.CreateUnreachable();
  llvm::InlineFunctionInfo info;
  if (!llvm::InlineFunction(*call, info).isSuccess()) {
    error = "could not inline color body";
    return false;
  }
  callee->eraseFromParent();
  auto* block = continuation->getParent();
  continuation->eraseFromParent();
  b.SetInsertPoint(block);
  return true;
}

bool Cgen_llvm::inline_loop(const Inline_body& body, const Loop_layout& layout, uint64_t count, int64_t first, int64_t step,
                            const std::vector<Value>& inputs, std::vector<Value>& outputs, std::string& error,
                            std::optional<size_t> tables_resource) {
  if (inputs.size() != layout.inputs.size() || layout.bindings.size() != body.inputs.size()) {
    error = "inline loop input count mismatch";
    return false;
  }
  if (layout.trip_tables != tables_resource.has_value() || (layout.trip_tables && (body.scalar || impl_->owner == nullptr))) {
    error = "inline loop per-trip tables need a resource of a packed-ABI kernel";
    return false;
  }
  const auto input_valid = [&](std::optional<size_t> input) { return !input || *input < layout.inputs.size(); };
  if (!input_valid(layout.index) || !input_valid(layout.activation)
      || (layout.next_active && (!layout.activation || *layout.next_active >= body.outputs.size()))) {
    error = "inline loop has an invalid index or activation binding";
    return false;
  }
  if (layout.index_bits && (!layout.index || layout.index_bits > layout.inputs[*layout.index].first)) {
    error = "inline loop index range exceeds its declared port";
    return false;
  }
  for (const auto& binding : layout.bindings) {
    if (binding.input >= layout.inputs.size() || binding.bit >= layout.inputs[binding.input].first) {
      error = "inline loop input slice is outside its port";
      return false;
    }
  }
  std::vector<bool> carry_inputs(layout.inputs.size(), false), carry_outputs(body.outputs.size(), false);
  for (const auto& [input, output] : layout.carries) {
    if (input >= layout.inputs.size() || output >= body.outputs.size() || carry_inputs[input] || carry_outputs[output]
        || layout.index == input || layout.activation == input) {
      error = "inline loop has an invalid or overlapping carry binding";
      return false;
    }
    carry_inputs[input] = carry_outputs[output] = true;
  }
  auto parsed = llvm::parseBitcodeFile(llvm::MemoryBufferRef(body.bitcode, "inline.loop"), impl_->context);
  if (!parsed) {
    error = llvm::toString(parsed.takeError());
    return false;
  }
  auto* source = (*parsed)->getFunction("__lhd_shared_color");
  if (!source) {
    error = "inline loop has no body";
    return false;
  }
  source->setName("__lhd_inline_loop_body");
  for (auto& fn : **parsed) {
    if (!fn.isDeclaration() && &fn != source) {
      fn.setLinkage(llvm::GlobalValue::InternalLinkage);  // the body's own native-loop entry is dead here
    }
  }
  if (llvm::Linker::linkModules(*impl_->module, std::move(*parsed))) {
    error = "could not import inline loop body";
    return false;
  }
  auto* callee = impl_->module->getFunction("__lhd_inline_loop_body");
  if (!callee) {
    error = "imported inline loop body is missing";
    return false;
  }
  // Per trip, a full unroll replicates the body and the inner loops it embeds
  // as they will be once expanded (kUnrolledExtra).
  const uint64_t body_extra = unrolled_extra(*callee);
  const uint64_t trip_cost  = unroll_cost(*callee) + body_extra;
  const bool     unroll     = count <= kUnrollMaxTrips && trip_cost <= kUnrollBudget && count * trip_cost <= kUnrollBudget
                              && impl_->unroll_extra + count * trip_cost <= kUnrollKernelBudget;
  impl_->add_unroll_extra(body_extra + (unroll && count > 0 ? (count - 1) * trip_cost : 0));

  auto& b       = impl_->builder;
  auto& context = impl_->context;
  auto* ptr     = llvm::PointerType::getUnqual(context);
  auto* i64     = b.getInt64Ty();
  auto* fn      = impl_->function;

  std::vector<llvm::Value*> initial;
  initial.reserve(inputs.size());
  for (size_t i = 0; i < inputs.size(); ++i) {
    initial.push_back(impl_->get(resize(inputs[i], layout.inputs[i].first, layout.inputs[i].second)));
    if (initial.back() == nullptr) {
      error = "inline loop operand is invalid";
      return false;
    }
  }
  size_t     input_words = 0, output_words = 0;
  const auto in_fields  = packed_layout(widths_of(body.inputs), &input_words);
  const auto out_fields = packed_layout(widths_of(body.outputs), &output_words);
  auto*      packed_in  = impl_->local_words(input_words, "loop.inputs");
  auto*      packed_out = impl_->local_words(output_words, "loop.outputs");
  auto*      changed    = impl_->local_words(word_count(static_cast<uint32_t>(body.outputs.size())), "loop.changed");
  impl_->zero_words(packed_in, input_words);
  for (size_t i = 0; i < output_words; ++i) {
    b.CreateStore(b.getInt64(0), b.CreateConstInBoundsGEP1_64(i64, packed_out, i));
  }
  for (const auto& [input, output] : layout.carries) {
    const auto width = body.outputs[output].first;
    impl_->store_field(cast_integer(b, initial[input], width, layout.inputs[input].second), packed_out, out_fields[output], width);
  }
  const auto bound = [&](size_t i, llvm::Value* value) {
    const auto& binding = layout.bindings[i];
    if (binding.bit) {
      value = b.CreateLShr(value, binding.bit);
    }
    return cast_integer(b, value, body.inputs[i].first, layout.inputs[binding.input].second);
  };
  // Invariant ports are packed once, ahead of the loop.
  for (size_t i = 0; i < layout.bindings.size(); ++i) {
    const auto input = layout.bindings[i].input;
    if (!carry_inputs[input] && layout.index != input && layout.activation != input) {
      impl_->store_field(bound(i, initial[input]), packed_in, in_fields[i], body.inputs[i].first);
    }
  }
  // A stateful body's per-trip tables: loaded once, ahead of the loop.
  llvm::Value* trip_tables = nullptr;
  if (tables_resource) {
    auto* slot  = b.CreateConstInBoundsGEP1_64(ptr, impl_->owner, *tables_resource);
    trip_tables = b.CreateLoad(ptr, slot, "trip.tables");
  }
  auto* start  = b.GetInsertBlock();
  auto* header = llvm::BasicBlock::Create(context, "inline.loop", fn);
  auto* bodyb  = llvm::BasicBlock::Create(context, "inline.body", fn);
  auto* done   = llvm::BasicBlock::Create(context, "inline.done", fn);
  b.CreateBr(header);
  b.SetInsertPoint(header);
  auto* ordinal = b.CreatePHI(i64, 2, "ordinal");
  ordinal->addIncoming(b.getInt64(0), start);
  std::vector<llvm::Value*>   values = initial;
  std::vector<llvm::PHINode*> carries;
  for (const auto& [input, output] : layout.carries) {
    (void)output;
    auto* phi = b.CreatePHI(initial[input]->getType(), 2, "carry");
    phi->addIncoming(initial[input], start);
    carries.push_back(phi);
    values[input] = phi;
  }
  llvm::PHINode* active = nullptr;
  if (layout.activation) {
    active = b.CreatePHI(initial[*layout.activation]->getType(), 2, "active");
    active->addIncoming(initial[*layout.activation], start);
    values[*layout.activation] = active;
  }
  b.CreateCondBr(b.CreateICmpULT(ordinal, b.getInt64(count)), bodyb, done);
  b.SetInsertPoint(bodyb);
  if (layout.index) {
    auto* index
        = b.CreateAdd(b.getInt64(static_cast<uint64_t>(first)), b.CreateMul(ordinal, b.getInt64(static_cast<uint64_t>(step))));
    if (layout.index_bits) {
      index = cast_integer(b, index, layout.index_bits, true);
    }
    values[*layout.index] = cast_integer(b, index, layout.inputs[*layout.index].first, layout.index_bits != 0);
  }
  for (size_t i = 0; i < layout.bindings.size(); ++i) {
    const auto input = layout.bindings[i].input;
    if (carry_inputs[input] || layout.index == input || layout.activation == input) {
      impl_->store_field(bound(i, values[input]), packed_in, in_fields[i], body.inputs[i].first);
    }
  }
  llvm::SmallVector<llvm::Value*> args;
  if (body.scalar) {
    for (size_t i = 0; i < layout.bindings.size(); ++i) {
      args.push_back(cast_integer(b, impl_->load_field(packed_in, in_fields[i], body.inputs[i].first, "arg"), 64, true));
    }
    args.push_back(llvm::ConstantPointerNull::get(ptr));
  } else {
    llvm::Value* owner = llvm::ConstantPointerNull::get(ptr);
    if (trip_tables != nullptr) {
      owner = b.CreateLoad(ptr, b.CreateInBoundsGEP(ptr, trip_tables, ordinal), "trip.owner");
    }
    args = {packed_in, packed_out, changed, owner};
  }
  auto* call = b.CreateCall(callee, args);
  if (body.scalar) {
    uint32_t bit = 0;
    for (size_t i = 0; i < body.outputs.size(); ++i) {
      const auto width = body.outputs[i].first;
      impl_->store_field(cast_integer(b, b.CreateLShr(call, bit), width, true), packed_out, out_fields[i], width);
      bit += width;
    }
  }
  auto*                     is_active = active ? b.CreateICmpNE(active, llvm::ConstantInt::get(active->getType(), 0)) : b.getTrue();
  std::vector<llvm::Value*> next_carries;
  for (size_t i = 0; i < layout.carries.size(); ++i) {
    const auto [input, output] = layout.carries[i];
    const auto width           = body.outputs[output].first;
    auto*      next            = cast_integer(b,
                                              impl_->load_field(packed_out, out_fields[output], width, "next.carry"),
                                              layout.inputs[input].first,
                                              body.outputs[output].second);
    next_carries.push_back(b.CreateSelect(is_active, next, carries[i]));
  }
  llvm::Value* next_active = active;
  if (layout.next_active) {
    const auto output = *layout.next_active;
    const auto width  = body.outputs[output].first;
    next_active       = cast_integer(b,
                                     impl_->load_field(packed_out, out_fields[output], width, "next.active"),
                                     layout.inputs[*layout.activation].first,
                                     body.outputs[output].second);
    next_active       = b.CreateSelect(is_active, next_active, llvm::ConstantInt::get(active->getType(), 0));
  }
  // Carry outputs publish the retained carry, including inactive/zero trips.
  for (size_t i = 0; i < layout.carries.size(); ++i) {
    const auto [input, output] = layout.carries[i];
    const auto width           = body.outputs[output].first;
    impl_->store_field(cast_integer(b, next_carries[i], width, layout.inputs[input].second), packed_out, out_fields[output], width);
  }
  auto* next_ordinal = b.CreateAdd(ordinal, b.getInt64(1));
  auto* latch        = b.GetInsertBlock();
  ordinal->addIncoming(next_ordinal, latch);
  for (size_t i = 0; i < carries.size(); ++i) {
    carries[i]->addIncoming(next_carries[i], latch);
  }
  if (active) {
    active->addIncoming(next_active, latch);
  }
  auto* backedge = b.CreateBr(header);
  // The pipeline runs LoopFullUnrollPass only when forced (PipelineTuningOptions
  // LoopUnrolling is off), so this metadata alone decides, per loop.
  auto* policy
      = llvm::MDNode::get(context, {llvm::MDString::get(context, unroll ? "llvm.loop.unroll.full" : "llvm.loop.unroll.disable")});
  auto* loop_id = llvm::MDNode::getDistinct(context, {nullptr, policy});
  loop_id->replaceOperandWith(0, loop_id);
  backedge->setMetadata(llvm::LLVMContext::MD_loop, loop_id);
  b.SetInsertPoint(done);
  outputs.clear();
  for (size_t i = 0; i < body.outputs.size(); ++i) {
    const auto [width, unsign] = body.outputs[i];
    outputs.push_back(impl_->remember(impl_->load_field(packed_out, out_fields[i], width, "loop.result"), width, unsign));
  }
  // Inline explicitly: a large body still stays ONE copy inside the rolled loop.
  llvm::InlineFunctionInfo info;
  if (!llvm::InlineFunction(*call, info).isSuccess()) {
    error = "could not inline the body into the rolled loop";
    return false;
  }
  callee->eraseFromParent();
  b.SetInsertPoint(done);
  return true;
}

bool Cgen_llvm::add_loop(std::string_view entry, const Loop_layout& layout, std::string& error, bool track_changed) {
  const bool storage = std::ranges::any_of(impl_->input_storage, [](const auto& s) { return s.has_value(); })
                       || std::ranges::any_of(impl_->output_values, [](const auto& o) { return o.dest.has_value(); });
  // The body inputs a loop port feeds: every input of a stateless body; the
  // packed ones (not storage-bound) of a stateful one.
  std::vector<size_t> bound_inputs;
  for (size_t i = 0; i < impl_->input_types.size(); ++i) {
    if (!(i < impl_->input_storage.size() && impl_->input_storage[i].has_value())) {
      bound_inputs.push_back(i);
    }
  }
  if (impl_->state_abi || impl_->loop_function || layout.bindings.size() != bound_inputs.size()
      || (!layout.trip_tables && (!impl_->memories.empty() || storage)) || (layout.trip_tables && impl_->scalar_abi)) {
    error = "native loop requires a stateless body (or per-trip tables) and a complete input binding";
    return false;
  }
  if (!seal(error, track_changed)) {
    return false;
  }
  const auto input_valid = [&](std::optional<size_t> input) { return !input || *input < layout.inputs.size(); };
  if (!input_valid(layout.index) || !input_valid(layout.activation)
      || (layout.next_active && (!layout.activation || *layout.next_active >= impl_->output_values.size()))) {
    error = "native loop has an invalid index or activation binding";
    return false;
  }
  if (layout.index_bits && (!layout.index || layout.index_bits > layout.inputs[*layout.index].first)) {
    error = "native loop index range exceeds its declared port";
    return false;
  }
  for (size_t i = 0; i < layout.bindings.size(); ++i) {
    const auto& binding = layout.bindings[i];
    if (binding.input >= layout.inputs.size() || binding.bit >= layout.inputs[binding.input].first) {
      error = "native loop input slice is outside its port";
      return false;
    }
  }
  std::vector<bool> carry_inputs(layout.inputs.size(), false), carry_outputs(impl_->output_values.size(), false);
  for (const auto& [input, output] : layout.carries) {
    if (input >= layout.inputs.size() || output >= impl_->output_values.size() || carry_inputs[input] || carry_outputs[output]
        || layout.index == input || layout.activation == input || impl_->output_values[output].dest) {
      error = "native loop has an invalid or overlapping carry binding";
      return false;
    }
    carry_inputs[input] = carry_outputs[output] = true;
  }
  for (const auto& [width, unsign] : layout.inputs) {
    (void)unsign;
    if (width == 0) {
      error = "native loop has a zero-width input";
      return false;
    }
  }
  auto& b              = impl_->builder;
  auto& context        = impl_->context;
  auto* ptr            = llvm::PointerType::getUnqual(context);
  auto* i64            = b.getInt64Ty();
  std::vector<llvm::Type*> params{ptr, ptr, i64, i64, i64};
  if (layout.trip_tables) {
    params.push_back(ptr);
  }
  auto* type           = llvm::FunctionType::get(b.getVoidTy(), params, false);
  auto* fn             = llvm::Function::Create(type, llvm::Function::ExternalLinkage, llvm::StringRef(entry), *impl_->module);
  impl_->loop_function = fn;
  fn->addParamAttr(0, llvm::Attribute::NoAlias);
  fn->addParamAttr(0, llvm::Attribute::ReadOnly);
  fn->addParamAttr(1, llvm::Attribute::NoAlias);
  auto* inputs  = fn->getArg(0);
  auto* outputs = fn->getArg(1);
  inputs->setName("inputs");
  outputs->setName("outputs");
  auto* start  = llvm::BasicBlock::Create(context, "entry", fn);
  auto* header = llvm::BasicBlock::Create(context, "loop", fn);
  auto* body   = llvm::BasicBlock::Create(context, "body", fn);
  auto* done   = llvm::BasicBlock::Create(context, "done", fn);
  b.SetInsertPoint(start);
  std::vector<llvm::Value*> initial;
  size_t                    offset = 0;
  for (const auto& [width, unsign] : layout.inputs) {
    (void)unsign;
    initial.push_back(impl_->load_packed(inputs, offset, width, "initial"));
    offset += word_count(width);
  }
  // The body's transport buffers use its own packed layout; the loop entry's
  // `outputs` keep one word run per value (the C++ loop adapter's layout).
  size_t     output_words = 0;
  const auto out_fields   = impl_->output_fields(&output_words);
  impl_->ensure_input_layout();
  const auto& in_fields  = impl_->input_fields;
  auto*       packed_in  = impl_->local_words(impl_->input_words, "body.inputs");
  auto*       packed_out = impl_->local_words(output_words, "body.outputs");
  auto*       changed    = impl_->local_words(word_count(static_cast<uint32_t>(impl_->output_values.size())), "body.changed");
  impl_->zero_words(packed_in, impl_->input_words);
  for (size_t i = 0; i < output_words; ++i) {
    b.CreateStore(b.getInt64(0), b.CreateConstInBoundsGEP1_64(i64, packed_out, i));
  }
  for (const auto& [input, output] : layout.carries) {
    const auto width = impl_->output_values[output].value.width;
    impl_->store_field(cast_integer(b, initial[input], width, layout.inputs[input].second), packed_out, out_fields[output], width);
  }
  // Pack invariant ports once. Mutable ports are SSA values across iterations.
  // Binding `i` feeds body input bound_inputs[i].
  const auto bound = [&](size_t i, llvm::Value* value) {
    const auto& binding = layout.bindings[i];
    if (binding.bit) {
      value = b.CreateLShr(value, binding.bit);
    }
    return cast_integer(b, value, impl_->input_types[bound_inputs[i]].first, layout.inputs[binding.input].second);
  };
  const auto store_bound = [&](size_t i, llvm::Value* value) {
    const auto in = bound_inputs[i];
    impl_->store_field(bound(i, value), packed_in, in_fields[in], impl_->input_types[in].first);
  };
  for (size_t i = 0; i < layout.bindings.size(); ++i) {
    const auto input = layout.bindings[i].input;
    if (!carry_inputs[input] && layout.index != input && layout.activation != input) {
      store_bound(i, initial[input]);
    }
  }
  b.CreateBr(header);
  b.SetInsertPoint(header);
  auto* ordinal = b.CreatePHI(i64, 2, "ordinal");
  ordinal->addIncoming(b.getInt64(0), start);
  std::vector<llvm::Value*>   values = initial;
  std::vector<llvm::PHINode*> carries;
  for (const auto& [input, output] : layout.carries) {
    (void)output;
    auto* phi = b.CreatePHI(initial[input]->getType(), 2, "carry");
    phi->addIncoming(initial[input], start);
    carries.push_back(phi);
    values[input] = phi;
  }
  llvm::PHINode* active = nullptr;
  if (layout.activation) {
    active = b.CreatePHI(initial[*layout.activation]->getType(), 2, "active");
    active->addIncoming(initial[*layout.activation], start);
    values[*layout.activation] = active;
  }
  b.CreateCondBr(b.CreateICmpULT(ordinal, fn->getArg(2)), body, done);
  b.SetInsertPoint(body);
  if (layout.index) {
    auto* index = b.CreateAdd(fn->getArg(3), b.CreateMul(ordinal, fn->getArg(4)));
    if (layout.index_bits) {
      index = cast_integer(b, index, layout.index_bits, true);
    }
    values[*layout.index] = cast_integer(b, index, layout.inputs[*layout.index].first, layout.index_bits != 0);
  }
  for (size_t i = 0; i < layout.bindings.size(); ++i) {
    const auto input = layout.bindings[i].input;
    if (carry_inputs[input] || layout.index == input || layout.activation == input) {
      store_bound(i, values[input]);
    }
  }
  llvm::SmallVector<llvm::Value*> args;
  if (impl_->scalar_abi) {
    for (size_t i = 0; i < layout.bindings.size(); ++i) {
      args.push_back(cast_integer(b, impl_->load_field(packed_in, in_fields[i], impl_->input_types[i].first, "arg"), 64, true));
    }
    args.push_back(llvm::ConstantPointerNull::get(ptr));
  } else {
    llvm::Value* owner = llvm::ConstantPointerNull::get(ptr);
    if (layout.trip_tables) {
      owner = b.CreateLoad(ptr, b.CreateInBoundsGEP(ptr, fn->getArg(5), ordinal), "trip.owner");
    }
    args = {packed_in, packed_out, changed, owner};
  }
  auto* call = b.CreateCall(impl_->function, args);
  if (impl_->scalar_abi) {
    uint32_t bit = 0;
    for (size_t i = 0; i < impl_->output_values.size(); ++i) {
      const auto width = impl_->output_values[i].value.width;
      auto*      value = cast_integer(b, b.CreateLShr(call, bit), width, true);
      impl_->store_field(value, packed_out, out_fields[i], width);
      bit += width;
    }
  }
  auto*                     is_active = active ? b.CreateICmpNE(active, llvm::ConstantInt::get(active->getType(), 0)) : b.getTrue();
  std::vector<llvm::Value*> next_carries;
  for (size_t i = 0; i < layout.carries.size(); ++i) {
    const auto [input, output] = layout.carries[i];
    const auto& out            = impl_->output_values[output].value;
    auto*       next           = cast_integer(b,
                                              impl_->load_field(packed_out, out_fields[output], out.width, "next.carry"),
                                              layout.inputs[input].first,
                                              out.unsign);
    next                       = b.CreateSelect(is_active, next, carries[i]);
    next_carries.push_back(next);
  }
  llvm::Value* next_active = active;
  if (layout.next_active) {
    const auto  output = *layout.next_active;
    const auto& out    = impl_->output_values[output].value;
    next_active        = cast_integer(b,
                                      impl_->load_field(packed_out, out_fields[output], out.width, "next.active"),
                                      layout.inputs[*layout.activation].first,
                                      out.unsign);
    next_active        = b.CreateSelect(is_active, next_active, llvm::ConstantInt::get(active->getType(), 0));
  }
  // Carry outputs publish the retained carry, including inactive/zero trips.
  for (size_t i = 0; i < layout.carries.size(); ++i) {
    const auto [input, output] = layout.carries[i];
    const auto width           = impl_->output_values[output].value.width;
    impl_->store_field(cast_integer(b, next_carries[i], width, layout.inputs[input].second), packed_out, out_fields[output], width);
  }
  auto* next_ordinal = b.CreateAdd(ordinal, b.getInt64(1));
  auto* latch        = b.GetInsertBlock();
  ordinal->addIncoming(next_ordinal, latch);
  for (size_t i = 0; i < carries.size(); ++i) {
    carries[i]->addIncoming(next_carries[i], latch);
  }
  if (active) {
    active->addIncoming(next_active, latch);
  }
  auto* backedge = b.CreateBr(header);
  auto* disable  = llvm::MDNode::get(context, {llvm::MDString::get(context, "llvm.loop.unroll.disable")});
  auto* loop_id  = llvm::MDNode::getDistinct(context, {nullptr, disable});
  loop_id->replaceOperandWith(0, loop_id);
  backedge->setMetadata(llvm::LLVMContext::MD_loop, loop_id);
  b.SetInsertPoint(done);
  // A storage-bound output (a register, a runtime slot) was published in its
  // lane during the trip; only packed outputs cross the entry.
  std::vector<size_t> published = layout.entry_outputs;
  if (published.empty()) {
    for (size_t i = 0; i < impl_->output_values.size(); ++i) {
      if (!impl_->output_values[i].dest) {
        published.push_back(i);
      }
    }
  }
  size_t entry_word = 0;
  for (const size_t i : published) {
    if (i >= impl_->output_values.size() || impl_->output_values[i].dest) {
      error = "native loop entry output is not a packed body output";
      return false;
    }
    const auto width = impl_->output_values[i].value.width;
    impl_->store_packed(impl_->load_field(packed_out, out_fields[i], width, "result"), outputs, entry_word, width);
    entry_word += word_count(width);
  }
  b.CreateRetVoid();
  // Inline explicitly before optimization: even a large body remains one copy
  // inside a rolled loop, independent of the general inliner's cost model.
  // InlineFunction names alias scopes after the callee. Normalize that name
  // too, so identical phase bodies do not acquire different sharing keys.
  const auto body_name = impl_->function->getName().str();
  impl_->function->setName("__lhd_loop_body");
  llvm::InlineFunctionInfo info;
  const auto               inlined = llvm::InlineFunction(*call, info);
  impl_->function->setName(body_name);
  if (!inlined.isSuccess() || llvm::verifyModule(*impl_->module, &llvm::errs())) {
    error = "LLVM could not inline the body into the native loop";
    return false;
  }
  return true;
}

std::string Cgen_llvm::sharing_key(std::string& error, bool track_changed) {
  if (!seal(error, track_changed)) {
    return {};
  }
  const std::string name = impl_->function->getName().str();
  impl_->function->setName("__lhd_shared_color");
  const auto loop_name = impl_->loop_function ? impl_->loop_function->getName().str() : std::string{};
  if (impl_->loop_function) {
    impl_->loop_function->setName("__lhd_shared_loop");
  }
  llvm::SmallVector<char, 0> bytes;
  llvm::raw_svector_ostream  stream(bytes);
  llvm::WriteBitcodeToFile(*impl_->module, stream);
  for (const auto& [input, inactive] : impl_->speculations) {  // write_module splits on these
    stream << "|speculate:" << input << '=' << inactive;
  }
  impl_->function->setName(name);
  if (impl_->loop_function) {
    impl_->loop_function->setName(loop_name);
  }
  return {bytes.data(), bytes.size()};
}

std::string Cgen_llvm::inline_bitcode(std::string& error, bool track_changed) {
  if (!seal(error, track_changed)) {
    return {};
  }
  const std::string name = impl_->function->getName().str();
  impl_->function->setName("__lhd_shared_color");
  const auto loop_name = impl_->loop_function ? impl_->loop_function->getName().str() : std::string{};
  if (impl_->loop_function) {
    impl_->loop_function->setName("__lhd_shared_loop");
  }
  llvm::SmallVector<char, 0> bytes;
  llvm::raw_svector_ostream  stream(bytes);
  llvm::WriteBitcodeToFile(*impl_->module, stream);
  impl_->function->setName(name);
  if (impl_->loop_function) {
    impl_->loop_function->setName(loop_name);
  }
  return {bytes.data(), bytes.size()};
}

bool Cgen_llvm::write_module(std::string_view path, std::string& error, bool track_changed, bool native) {
  (void)llvm_debug_args();
  Llvm_pass_profile profile("kernel", path);
  if (!seal(error, track_changed)) {
    return false;
  }
  profile.mark("finalize-and-verify");

  livehd::sim::initialize_llvm_target();

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
  std::unique_ptr<llvm::TargetMachine> machine(
      target->createTargetMachine(triple, "generic", "", options, llvm::Reloc::PIC_, std::nullopt, llvm::CodeGenOptLevel::Default));
  if (!machine) {
    error = "LLVM could not create a native target machine";
    return false;
  }
  impl_->module->setTargetTriple(triple);
  impl_->module->setDataLayout(machine->createDataLayout());
  profile.mark("target-setup");

  // Reset-free fast path (speculate_input); the bitcode digest below sees it.
  auto body_names = std::vector<std::string>{};
  for (auto* body : impl_->split_speculation()) {
    body_names.push_back(body->getName().str());
  }
  const auto digest = [](llvm::StringRef bytes) {
    llvm::SHA256 hash;
    hash.update(bytes);
    const auto result = hash.final();
    return std::string(reinterpret_cast<const char*>(result.data()), result.size());
  };
  std::string input_digest;
  if (native && !impl_->cache_path.empty()) {
    llvm::SmallVector<char, 0> bitcode;
    llvm::raw_svector_ostream  stream(bitcode);
    llvm::WriteBitcodeToFile(*impl_->module, stream);
    llvm::SHA256 hash;
    hash.update(llvm::StringRef(bitcode.data(), bitcode.size()));
    hash.update(std::to_string(livehd::kCgenSrcSalt));
    hash.update(std::to_string(branch_min_cost()));  // debug knobs that change the object
    hash.update(shrink_enabled() ? "shrink" : "keep");
    hash.update(llvm_debug_args());
    hash.update(std::to_string(remat_distance()));
    const auto key = hash.final();
    input_digest.assign(reinterpret_cast<const char*>(key.data()), key.size());
    const auto cached = llvm::MemoryBuffer::getFile(impl_->cache_path);
    if (cached && (*cached)->getBufferSize() == 64 && (*cached)->getBuffer().take_front(32) == input_digest) {
      const auto object = llvm::MemoryBuffer::getFile(path);
      if (object && (*cached)->getBuffer().drop_front(32) == digest((*object)->getBuffer())) {
        if (impl_->cache_reused != nullptr) {
          *impl_->cache_reused = true;
        }
        return true;
      }
    }
  }

  // Optimize the native circuit for execution speed. Explicit loop inlining
  // has already run; retain rolled bodies without restricting straight-line
  // arithmetic and vector simplifications to size-oriented transforms.
  llvm::LoopAnalysisManager     loop_analyses;
  llvm::FunctionAnalysisManager function_analyses;
  llvm::CGSCCAnalysisManager    cgscc_analyses;
  llvm::ModuleAnalysisManager   module_analyses;
  llvm::PipelineTuningOptions   tuning;
  tuning.LoopUnrolling    = false;
  tuning.LoopInterleaving = false;
  llvm::PassBuilder pass_builder(machine.get(), tuning, std::nullopt, profile.callbacks());
  pass_builder.registerModuleAnalyses(module_analyses);
  pass_builder.registerCGSCCAnalyses(cgscc_analyses);
  pass_builder.registerFunctionAnalyses(function_analyses);
  pass_builder.registerLoopAnalyses(loop_analyses);
  pass_builder.crossRegisterProxies(loop_analyses, function_analyses, cgscc_analyses, module_analyses);
  impl_->mark_invariant_loads();
  if (livehd::debug::env<"LHD_SIM_LLVM_DUMP_IR">() != nullptr) {  // also the module as built, before O2 (.pre.ll)
    std::error_code      ec;
    llvm::raw_fd_ostream ir(std::string(path) + ".pre.ll", ec);
    if (!ec) {
      impl_->module->print(ir, nullptr);
    }
  }
  auto pipeline = pass_builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
  pipeline.run(*impl_->module, module_analyses);
  if (impl_->loop_function != nullptr) {
    // Clean up hoisted loop expressions without perturbing straight-line
    // colors with a second global instruction-combining sweep.
    llvm::FunctionPassManager loop_cleanup;
    loop_cleanup.addPass(llvm::InstCombinePass());
    loop_cleanup.run(*impl_->loop_function, function_analyses);
    short_circuit_loop_carries(*impl_->loop_function);
    function_analyses.invalidate(*impl_->loop_function, llvm::PreservedAnalyses::none());
  }
  // Conditional regions: branch over expensive select arms (see
  // conditionalize_selects). Debug: LHD_SIM_LLVM_NO_BRANCH=1 disables it,
  // LHD_SIM_LLVM_BRANCH_MIN=<n> sets the arm-size threshold (default 4).
  // By name: interprocedural passes (dead-argument elimination) may have
  // replaced an internal body of the reset split with a new function, and the
  // inliner folds both into the dispatcher (impl_->function) by default.
  std::vector<llvm::Function*> post_bodies;
  for (const auto& body_name : body_names) {
    if (auto* body = impl_->module->getFunction(body_name); body != nullptr && !body->isDeclaration()) {
      post_bodies.push_back(body);
    }
  }
  if (post_bodies.empty() && impl_->function != nullptr) {
    post_bodies.push_back(impl_->function);
  }
  for (auto* body : post_bodies) {
    if (const auto min_cost = branch_min_cost(); min_cost != 0) {
      if (conditionalize_selects(*body, min_cost) != 0 && livehd::debug::env<"LHD_SIM_LLVM_NO_SCOPE_SINK">() == nullptr) {
        sink_into_use_blocks(*body);
        function_analyses.invalidate(*body, llvm::PreservedAnalyses::none());
      }
    }
    // conditionalize_selects never moves memory reads: a load only one arm
    // uses still runs before the branch, on every call. LLVM's Sink pass moves
    // an instruction (a load when no store in between may alias it) into the
    // single successor block that uses it.
    if (livehd::debug::env<"LHD_SIM_LLVM_NO_SINK">() == nullptr) {
      llvm::FunctionPassManager sink;
      sink.addPass(llvm::SinkingPass());
      sink.run(*body, function_analyses);
    }
    // Recompute far-used invariant values near their uses instead of keeping
    // them live (rematerialize_invariant).
    if (const auto distance = remat_distance(); distance != 0 && rematerialize_invariant(*body, distance) != 0) {
      function_analyses.invalidate(*body, llvm::PreservedAnalyses::none());
    }
    // Then shorten live ranges (shrink_live_ranges), opt-in: LHD_SIM_LLVM_SHRINK=1.
    if (shrink_enabled() && shrink_live_ranges(*body) != 0) {
      function_analyses.invalidate(*body, llvm::PreservedAnalyses::none());
    }
  }
  profile.mark("ir-optimization");

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
    for (auto& function : *impl_->module) {
      for (auto& block : function) {
        for (auto& instruction : block) {
          if (auto* memory = llvm::dyn_cast<llvm::MemIntrinsic>(&instruction)) {
            memory_ops.push_back(memory);
          }
        }
      }
    }
    for (auto* memory : memory_ops) {
      const auto& tti = function_analyses.getResult<llvm::TargetIRAnalysis>(*memory->getFunction());
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
    // Debug only: the optimized IR next to the object (`<object>.opt.ll`).
    if (livehd::debug::env<"LHD_SIM_LLVM_DUMP_IR">() != nullptr) {
      std::error_code      ec;
      llvm::raw_fd_ostream ir(std::string(path) + ".opt.ll", ec);
      if (!ec) {
        impl_->module->print(ir, nullptr);
      }
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
  if (!input_digest.empty()) {
    // Publish only after validation and the object write. A missing, truncated
    // or mismatched pair is a miss, including interruption between the writes.
    File_output cached{impl_->cache_path};
    cached.append(input_digest);
    cached.append(digest(llvm::StringRef(object_buffer.data(), object_buffer.size())));
  }
  profile.mark("bitcode-serialize-and-write");
  return true;
}

bool Cgen_llvm::link_bitcode_object(std::string_view host_path, const std::vector<std::string>& kernel_paths,
                                    std::string_view object_path, std::string& error) {
  Llvm_pass_profile profile("host-and-kernels", object_path);
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
  profile.mark("read-and-link-bitcode");

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

  livehd::sim::initialize_llvm_target();
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
  std::unique_ptr<llvm::TargetMachine> machine(
      target->createTargetMachine(triple, "generic", "", options, llvm::Reloc::PIC_, std::nullopt, llvm::CodeGenOptLevel::Default));
  if (!machine) {
    error = "LLVM could not create a native target machine for linked simulator bitcode";
    return false;
  }
  module->setDataLayout(machine->createDataLayout());
  for (auto& function : *module) {
    // Cold support may explicitly use optnone; LLVM forbids combining it
    // with optsize. Preserve that function-level override.
    if (!function.isDeclaration() && !function.hasFnAttribute(llvm::Attribute::OptimizeNone)) {
      function.addFnAttr(llvm::Attribute::OptimizeForSize);
    }
  }
  profile.mark("target-setup");

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
  tuning.LoopUnrolling    = false;
  tuning.LoopInterleaving = false;
  llvm::PassBuilder pass_builder(machine.get(), tuning, std::nullopt, profile.callbacks());
  pass_builder.registerModuleAnalyses(module_analyses);
  pass_builder.registerCGSCCAnalyses(cgscc_analyses);
  pass_builder.registerFunctionAnalyses(function_analyses);
  pass_builder.registerLoopAnalyses(loop_analyses);
  pass_builder.crossRegisterProxies(loop_analyses, function_analyses, cgscc_analyses, module_analyses);
  llvm::ModulePassManager pipeline;
  pipeline.addPass(llvm::AlwaysInlinerPass());
  // LLVM 23 expresses -Os as O2 plus the function optsize attributes above.
  pipeline.addPass(pass_builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2));
  pipeline.run(*module, module_analyses);

  for (auto& function : module->functions()) {
    if (!function.isDeclaration() && locally_called.contains(function.getName())) {
      function.setLinkage(llvm::GlobalValue::InternalLinkage);  // inlined here; GlobalDCE below reclaims it
    }
  }
  llvm::ModulePassManager cleanup;
  cleanup.addPass(llvm::GlobalDCEPass());
  cleanup.run(*module, module_analyses);
  profile.mark("ir-optimization");

  llvm::SmallVector<char, 0> native_buffer;
  llvm::raw_svector_ostream  native(native_buffer);
  llvm::legacy::PassManager  emit;
  if (machine->addPassesToEmitFile(emit, native, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    error = "LLVM target cannot emit linked simulator objects";
    return false;
  }
  emit.run(*module);
  profile.mark("native-code-generation");
  {
    File_output out{object_path};
    out.append(std::string_view(native_buffer.data(), native_buffer.size()));
  }
  profile.mark("native-object-write");
  return true;
}
