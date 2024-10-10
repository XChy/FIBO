#include "Reducer.h"
#include <format>

bool llvm::reduceOnCriterion(Module &M, std::string InputFilename,
                             std::string OutputBitcode,
                             std::string OutputFilename,
                             std::string criterion) {
  auto Command =
      std::format("llvm-slicer -c {} -entry {} -forward "
                  "-criteria-are-next-instr {} -o {} &&"
                  "llvm-dis {} -o {}",
                  /*slicing criterion*/ criterion,
                  /*function to slice*/
                  M.getFunctionList().begin()->getName().str(), InputFilename,
                  OutputBitcode, OutputBitcode, OutputFilename);
  int Ret = system(Command.c_str());
  return Ret == 0;
}
