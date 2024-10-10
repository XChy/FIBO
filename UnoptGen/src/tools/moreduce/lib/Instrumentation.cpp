#include "Instrumentation.h"
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DebugLoc.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Metadata.h>
#include <llvm/Transforms/Utils/Local.h>
#include <string>

using namespace llvm;

static int instruct_time = 0;

static std::string insertMarkFor(IRBuilder<> &Builder, Instruction *I) {
  Builder.SetInsertPoint(I);
  auto Call = Builder.CreateCall(I->getModule()->getOrInsertFunction(
      Signature + std::to_string(instruct_time),
      llvm::Type::getVoidTy(I->getContext())));

  instruct_time++;

  return Call->getCalledFunction()->getName().str();
}

static SmallVector<std::string, 16> markDiffOnModule(DiffEngine &Engine,
                                                     Module &M) {
  MarkList Ret;
  IRBuilder<> Builder(M.getContext());
  for (auto &LF : M)
    for (auto &BB : LF)
      for (auto &I : BB)
        if (!Engine.Values.contains(&I) && isMarkable(&I))
          Ret.push_back(insertMarkFor(Builder, &I));

  return Ret;
}

MarkListPair llvm::markDiffs(DiffEngine &Engine, Module &L, Module &R) {

  return {markDiffOnModule(Engine, L), markDiffOnModule(Engine, R)};
}

bool llvm::isMarkable(const Instruction *I) {
  // FIXME: cannot handle phi node yet
  return !(isa<PHINode>(I));
}
