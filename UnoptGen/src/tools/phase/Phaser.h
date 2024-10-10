#pragma once

#include <llvm/IR/Module.h>

namespace llvm {

/*
 * Control the main mutating procedure, managing all
 * semantics-preserving passes.
 */

class Phaser {
public:
  Phaser(const std::string &PipelineFile) : PipelineFile(PipelineFile) {}

  // Mutate M with a pipeline of semantics-preserving passes (randomized or
  // determined). Return 0 if succeeding, otherwise return -1.
  int mutate(Module &M);
  void storeConfig();

  void generateOrReadPipeline();

private:
  std::string PipelineFile;
  std::vector<std::string> Pipeline;
};

} // namespace llvm
