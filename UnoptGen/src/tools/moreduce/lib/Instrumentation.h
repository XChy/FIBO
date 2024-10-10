#include "DiffEngine.h"
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
constexpr std::string Signature = "optreduce_use_";
namespace llvm {

typedef SmallVector<std::string, 16> MarkList;
typedef std::pair<MarkList, MarkList> MarkListPair;

MarkListPair markDiffs(DiffEngine &Engine, Module &L, Module &R);

bool isMarkable(const Instruction *I);

} // namespace llvm
