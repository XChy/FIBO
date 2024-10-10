#pragma once

#include "llvm/ADT/StringRef.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/Hashing.h>
#include <llvm/ADT/PriorityWorklist.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <unordered_set>
#include <queue>

using InstPair =
    std::pair<const llvm::Instruction *, const llvm::Instruction *>;

namespace std {
template <> struct hash<InstPair> {
  size_t operator()(const InstPair &X) const {
    return llvm::hash_combine(X.first, X.second);
  }
};
} // namespace std

namespace llvm {
class Function;
class GlobalValue;
class Instruction;
class LLVMContext;
class Module;
class Twine;
class Value;

using BBPair = std::pair<const BasicBlock *, const BasicBlock *>;

/// A class for performing structural comparisons of LLVM assembly.
class DiffEngine {
public:
  DiffEngine() {}

  void diff(const Module *L, const Module *R);
  void diff(const Function *L, const Function *R);

  bool propagateForward(const BasicBlock *LB, const BasicBlock *RB);
  bool propagateBackward(const BasicBlock *LB, const BasicBlock *RB);

  bool provedRefined(const Value *L, const Value *R);
  bool judgeInstAsUser(const Instruction *L, const Instruction *R);
  bool judgeInstAsUsee(const Instruction *L, const Instruction *R);
  bool identicalConst(const Constant *L, const Constant *R);
  bool identicalConstExpr(const ConstantExpr *L, const ConstantExpr *R);
  bool identicalCall(const CallBase &L, const CallBase &R);

  bool mustDifferent(const Instruction *L, const Instruction *R);
  // NOTE: Insert (L, R) pair to TentativeValues, but not Values.
  void tryRefine(const Value *L, const Value *R);
  void tryRefine(const BasicBlock *L, const BasicBlock *R);

  void addCandidates(const Instruction *L, const Instruction *R);

  // Add (L, R) refined pair to Values
  void refine(const Value *L, const Value *R);
  void refineBB(const BasicBlock *L, const BasicBlock *R);


  // Return true if (L, R) in Values or TentativeValues
  bool isRefined(const Value *V);
  bool isRefined(const Value *L, const Value *R);
  bool isBBRefined(const BasicBlock *L, const BasicBlock *R);

  // Equivalent Pair
  DenseMap<const Value *, const Value *> TentativeValues;
  DenseMap<const Value *, const Value *> Values;
  DenseMap<const BasicBlock *, const BasicBlock *> Blocks;
  DenseMap<const Function *, const Function *> Functions;

  // Basic block pairs as candidates
  std::queue<BBPair> BBWorklist;
  DenseMap<const BasicBlock *, int> Visited;

  // Inst pairs as candidates. Only when (L, R) are in the corresponding BB,
  // can we say they are refined
  std::unordered_set<InstPair> InstWorklist;
};
} // namespace llvm
