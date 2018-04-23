#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include "lgbench.hpp"

// Hack: Let the JIT know about the external function, so it can do a simple
// manual lookup.
int *customIntAllocator(unsigned items);

#include "SimpleOrcJit.h"

// Show the error message and exit.
LLVM_ATTRIBUTE_NORETURN static void fatalError(llvm::Error E) {
  llvm::handleAllErrors(std::move(E), [&](const llvm::ErrorInfoBase &EI) {
    llvm::errs() << "Fatal Error: ";
    EI.log(llvm::errs());
    llvm::errs() << "\n";
    llvm::errs().flush();
  });

  exit(1);
}

llvm::Expected<std::string> codegenIR(llvm::Module *module, unsigned items) {
  using namespace llvm;

  LLVMContext &ctx = module->getContext();
  IRBuilder<> Builder(ctx);

  auto name = "integerDistance";
  auto intTy = Type::getInt32Ty(ctx);
  auto intPtrTy = intTy->getPointerTo();
  auto signature = FunctionType::get(intPtrTy, {intPtrTy, intPtrTy}, false);
  auto linkage = Function::ExternalLinkage;

  auto fn = Function::Create(signature, linkage, name, module);
  fn->setName(name); // so the CompileLayer can find it

  Builder.SetInsertPoint(BasicBlock::Create(ctx, "entry", fn));
  {
    auto argIt = fn->arg_begin();
    Argument &argPtrX = *argIt;
    Argument &argPtrY = *(++argIt);

    argPtrX.setName("x");
    argPtrY.setName("y");

    auto allocSig = FunctionType::get(intPtrTy, {intTy}, false);
    Value *allocFunction =
        module->getOrInsertFunction("customIntAllocator", allocSig);

    Value *results_size = ConstantInt::get(intTy, items);
    Value *results_ptr = Builder.CreateCall(allocFunction, {results_size});

    auto absSig = FunctionType::get(intTy, {intTy}, false);
    Value *absFunction = module->getOrInsertFunction("abs", absSig);

    for (unsigned int i = 0; i < items; i++) {
      Value *xi_ptr = Builder.CreateConstInBoundsGEP1_32(intTy, &argPtrX, i);
      Value *yi_ptr = Builder.CreateConstInBoundsGEP1_32(intTy, &argPtrY, i);

      Value *x0 = Builder.CreateLoad(xi_ptr);
      Value *y0 = Builder.CreateLoad(yi_ptr);
      Value *difference = Builder.CreateSub(x0, y0);
      Value *absDifference = Builder.CreateCall(absFunction, {difference});

      Value *ri_ptr = Builder.CreateConstInBoundsGEP1_32(intTy, results_ptr, i);
      Builder.CreateStore(absDifference, ri_ptr);
    }

    Builder.CreateRet(results_ptr);
  }

  std::string error;
  raw_string_ostream es(error);

  if (verifyFunction(*fn, &es))
    return make_error<StringError>(
        Twine("Function verification failed:\n", es.str()),
        llvm::inconvertibleErrorCode());

  if (verifyModule(*module, &es))
    return make_error<StringError>(
        Twine("Module verification failed:\n", es.str()),
        llvm::inconvertibleErrorCode());

  return name;
}

// Determine the size of a C array at compile-time.
template <typename T, size_t sizeOfArray>
constexpr unsigned arrayElements(T (&)[sizeOfArray]) {
  return sizeOfArray;
}

// This function will be called from JITed code.
int *customIntAllocator(unsigned items) {
  static int memory[100];
  static unsigned allocIdx = 0;

  if (allocIdx + items > arrayElements(memory))
    exit(-1);

  int *block = memory + allocIdx;
  allocIdx += items;

  return block;
}

int main(int argc, char **argv) {
  using namespace llvm;

  LGBench b;

  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);

  atexit(llvm_shutdown);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Parse -debug and -debug-only options.
  cl::ParseCommandLineOptions(argc, argv, "JitFromScratch example project\n");

  int x[]{0, 1, 2};
  int y[]{3, 1, -1};

  auto targetMachine = EngineBuilder().selectTarget();
  auto jit = llvm::make_unique<SimpleOrcJit>(*targetMachine);

  LLVMContext context;
  auto module = llvm::make_unique<Module>("JitFromScratch", context);
  module->setDataLayout(targetMachine->createDataLayout());

  b.sample("setup");

  // Generate LLVM IR for the function.
  Expected<std::string> jitedFnName = codegenIR(module.get(), arrayElements(x));
  if (!jitedFnName)
    fatalError(jitedFnName.takeError());

  b.sample("codegen");

  {
    //auto module2 = llvm::make_unique<Module>("JitFromScratch", context);

    b.sample("setup2");

    // Generate LLVM IR for the function.
    //codegenIR(module2.get(), arrayElements(x));

    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();

    auto Filename = "output.o";
    std::error_code EC;
    raw_fd_ostream dest(Filename, EC, sys::fs::F_None);

    if (EC) {
      errs() << "Could not open file: " << EC.message();
      return 1;
    }

    auto TargetTriple = sys::getDefaultTargetTriple();
    module->setTargetTriple(TargetTriple);

    std::string Error;
    auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);

    // Print an error and exit if we couldn't find the requested target.
    // This generally occurs if we've forgotten to initialise the
    // TargetRegistry or we have a bogus target triple.
    if (!Target) {
      errs() << Error;
      return 1;
    }

    auto CPU = "generic";
    auto Features = "";

    TargetOptions opt;
    auto RM = Optional<Reloc::Model>();
    RM = Reloc::PIC_; // PIC code needed for so libraries
    auto TheTargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);

    module->setDataLayout(TheTargetMachine->createDataLayout());

    legacy::PassManager pass;
    auto FileType = TargetMachine::CGFT_ObjectFile;

    if (TheTargetMachine->addPassesToEmitFile(pass, dest, FileType)) {
      errs() << "TargetMachine can't emit a file of this type";
      return 1;
    }

    pass.run(*module);
    dest.flush();
  }

  // Compile to machine code and link.
  jit->submitModule(std::move(module));
  auto jitedFn = jit->getFunction<int *(int *, int *)>(*jitedFnName);
  if (!jitedFn)
    fatalError(jitedFn.takeError());

  b.sample("jit");
  // Invoke JITed function.
  auto integerDistances = *jitedFn;
  int *z = integerDistances(x, y);

  b.sample("jitcall");


  b.sample("objgen");
  outs() << "Integer Distances: ";
  outs() << z[0] << ", " << z[1] << ", " << z[2] << "\n\n";
  outs().flush();

  return 0;
}
