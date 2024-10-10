
#include "llvm/Support/Program.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/WithColor.h"
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/iterator_range.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>

using namespace llvm;

cl::OptionCategory MOReducerOptions("MOReducer Options");

static cl::opt<std::string> LeftFilename(cl::Positional,
                                         cl::desc("<first file>"), cl::Required,
                                         cl::cat(MOReducerOptions));
static cl::opt<std::string> RightFilename(cl::Positional,
                                          cl::desc("<second file>"),
                                          cl::Required,
                                          cl::cat(MOReducerOptions));
static cl::opt<bool> DoInstrumentation("i", cl::desc("Do instrumentation"),
                                       cl::cat(MOReducerOptions));

static cl::opt<bool> UseSlicer("s", cl::desc("Use slicer"),
                               cl::cat(MOReducerOptions));

static std::unique_ptr<Module> readModule(LLVMContext &Context,
                                          StringRef Name) {
  SMDiagnostic Diag;
  std::unique_ptr<Module> M = parseIRFile(Name, Diag, Context);
  if (!M)
    Diag.print("moreduce", errs());
  return M;
}

template <typename T> static std::string joinStringList(T Set) {
  return llvm::join(llvm::make_range(Set.begin(), Set.end()), ",");
}

static void writeModule(Module &M, StringRef Name) {
  // TODO: Support bitcode writer
  std::error_code EC;
  raw_fd_ostream Out(Name, EC, sys::fs::OF_Text);
  M.print(Out, nullptr);
}

int main(int Argc, char **Argv) {
  cl::HideUnrelatedOptions({&MOReducerOptions, &getColorCategory()});
  cl::ParseCommandLineOptions(Argc, Argv);

  // Read IR, FIXME: share one context and don't lose type equivalence
  // information
  LLVMContext Context;
  LLVMContext Context1;

  std::unique_ptr<Module> LModule = readModule(Context, LeftFilename);
  std::unique_ptr<Module> RModule = readModule(Context1, RightFilename);
  if (!LModule || !RModule)
    return 1;
}
