#include "Phaser.h"
#include "transforms/CFGExpand/CFGExpand.h"
#include "transforms/CFGRandomSimplify/CFGRandomSimplify.h"
#include "transforms/ExprExpand/ExprExpandPass.h"
#include "transforms/InstRandomCombine/InstRandomCombine.h"
#include "transforms/RandomCVP/RandomCVP.h"
#include "transforms/RandomDSE/RandomDSE.h"
#include "transforms/RandomLICM/RandomLICM.h"
#include "transforms/RandomLoopSink/RandomLoopSink.h"
#include "transforms/RandomMem2Reg/RandomMem2Reg.h"
#include "transforms/RandomReg2Mem/RandomReg2Mem.h"
#include "transforms/RandomScalarizer/RandomScalarizer.h"
#include "transforms/RandomSink/RandomSink.h"
#include "utils/Debug.h"
#include "utils/Files.h"
#include "utils/Random.h"
#include <cassert>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/IRPrinter/IRPrintingPasses.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/CorrelatedValuePropagation.h>
#include <llvm/Transforms/Scalar/DeadStoreElimination.h>
#include <llvm/Transforms/Scalar/LoopPassManager.h>
#include <llvm/Transforms/Scalar/LoopSimplifyCFG.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SCCP.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/LCSSA.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>
#include <llvm/Transforms/Vectorize/LoopVectorize.h>
#include <llvm/Transforms/Vectorize/SLPVectorizer.h>
#include <random>
#include <string>

using namespace llvm;

static cl::opt<std::string> passes("passes", cl::desc("<passes>"));

#define PASS_CREATOR(Pass, Weight, Name)                                       \
  {                                                                            \
    [](FunctionPassManager &FPM) { FPM.addPass(Pass); }, (Weight), (Name)      \
  }

typedef std::function<void(FunctionPassManager &)> FuncPassBuilder;
typedef std::tuple<FuncPassBuilder, int, std::string> PassEntry;

static std::vector<PassEntry> PipelineGen = {
    PASS_CREATOR(ExprExpandPass(), 50, "random-inst-expand"),
    PASS_CREATOR(CFGExpandPass(), 50, "random-cfg-expand"),
    PASS_CREATOR(RandomSinkPass(), 50, "random-sink"),
    PASS_CREATOR(RandomRegToMemPass(), 50, "random-mem2reg"),
    PASS_CREATOR(RandomLoopSinkPass(), 50, "random-loopsink"),
    PASS_CREATOR(RandomScalarizerPass(), 50, "random-scalarizer"),
};

void Phaser::generateOrReadPipeline() {}

void Phaser::storeConfig() {}

int Phaser::mutate(Module &M) {

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassBuilder PB;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  FunctionPassManager FPM;
  ModulePassManager MPM;

  std::string PassesStr = passes;
  for (int i = 0; i < PassesStr.size(); i++) {
    auto &[Builder, _, Name] = PipelineGen[PassesStr[i] - '0'];
    Builder(FPM);
  }

  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  MPM.run(M, MAM);
  return 0;
}
