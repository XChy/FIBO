#include "Canonicalizer.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Module.h>

using namespace llvm;

static void nameAll(Function &F) {
  for (BasicBlock &BB : F) {
    if (!BB.hasName())
      BB.setName("bb");

    for (Instruction &I : BB) {
      if (!I.hasName() && !I.getType()->isVoidTy())
        I.setName("i");
    }
  }
}

void Canonicalizer::run(Module &L, Module &R, DiffEngine &Oracle) {
  for (auto &F : L)
    nameAll(F);
  for (auto &F : R)
    nameAll(F);

  for (auto &F : R)
    for (BasicBlock &BB : F) {
      // Not keep
      if (Oracle.Blocks.contains(&BB))
        BB.setName(Oracle.Blocks[&BB]->getName());

      for (Instruction &I : BB) {
        if (I.getType()->isVoidTy())
          continue;

        if (Oracle.Values.contains(&I)) {
          I.setName(Oracle.Values[&I]->getName());
          if (I.getName() != Oracle.Values[&I]->getName()) {
            ((Value *)Oracle.Values[&I])->setName(I.getName());
          }
        }
      }
    }
}
