#pragma once

#include "DiffEngine.h"
namespace llvm {

class Canonicalizer {
public:
  void run(Module &L, Module &R, DiffEngine &Oracle);
};
} // namespace llvm
