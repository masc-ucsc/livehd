// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <mutex>

#include "llvm/Support/TargetSelect.h"

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
}  // namespace livehd::sim
