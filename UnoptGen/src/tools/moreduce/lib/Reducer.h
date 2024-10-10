#pragma once

#include <llvm/IR/Module.h>

namespace llvm {

bool reduceOnCriterion(Module &M, std::string InputFilename,
                       std::string OutputBitcode, std::string OutputFilename,
                       std::string criterion);

}
