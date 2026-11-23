// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"

namespace livehd::sim {
// Object emitters and the shared runtime can initialize concurrently. They
// must use the same once flag before touching LLVM's process-wide registry.
inline void initialize_llvm_target() {
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
  });
}

// The kernel and testbench objects run in the same JIT. Set their triple,
// relocation model, CPU features and data layout from one target machine.
inline std::unique_ptr<llvm::TargetMachine> object_machine(llvm::Module& module, std::string& error,
                                                           llvm::CodeGenOptLevel level = llvm::CodeGenOptLevel::Default) {
  initialize_llvm_target();
  auto triple = module.getTargetTriple();
  if (triple.getTriple().empty()) {
    triple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
  }
  const auto* target = llvm::TargetRegistry::lookupTarget(triple, error);
  if (!target) {
    return nullptr;
  }
  llvm::TargetOptions                  options;
  std::unique_ptr<llvm::TargetMachine> machine(
      target->createTargetMachine(triple, "generic", "", options, llvm::Reloc::PIC_, std::nullopt, level));
  if (!machine) {
    error = "LLVM could not create a native target machine";
    return nullptr;
  }
  module.setTargetTriple(triple);
  module.setDataLayout(machine->createDataLayout());
  return machine;
}
}  // namespace livehd::sim
