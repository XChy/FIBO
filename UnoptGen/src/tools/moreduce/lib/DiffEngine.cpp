#include "DiffEngine.h"
#include "utils/Debug.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>
using namespace llvm;

const ReturnInst *findSingleReturn(const Function *F) {

  const ReturnInst *Ret = nullptr;
  for (auto &BB : *F)
    for (auto &I : BB)
      if (I.getOpcode() == Instruction::Ret) {
        if (Ret)
          return nullptr;
        Ret = cast<ReturnInst>(&I);
      }
  return Ret;
}

void DiffEngine::diff(const Module *L, const Module *R) {
  // Global variables are identical
  for (auto &GL : L->globals())
    for (auto &GR : R->globals())
      if (GL.getName() == GR.getName()) {
        refine(&GL, &GR);
        if (GL.hasOneUser() && GR.hasOneUser()) {
          auto *GLUser = dyn_cast<Instruction>(*GL.user_begin());
          auto *GRUser = dyn_cast<Instruction>(*GR.user_begin());
          // FIXME: Or just candidates?
          if (GLUser && GRUser && !mustDifferent(GLUser, GRUser))
            refine(GLUser, GRUser);
        }
      }

  // Global variables are refined
  for (const Function &FL : *L)
    for (const Function &FR : *R)
      if (FL.getName() == FR.getName())
        refine(&FL, &FR);

  for (const Function &FL : *L)
    for (const Function &FR : *R)
      if (FL.getName() == FR.getName() && !FL.isDeclaration())
        diff(&FL, &FR);
}

bool DiffEngine::propagateForward(const BasicBlock *LB, const BasicBlock *RB) {
  bool Changed = false;
  auto LStart = LB->begin();
  auto RStart = RB->begin();
  auto LE = LB->end();
  auto RE = RB->end();

  while (LStart != LE && RStart != RE) {
    if (isRefined(&*LStart, &*RStart)) {
      LStart++;
      RStart++;
      continue;
    }
    if (judgeInstAsUser(&*LStart, &*RStart)) {
      MODEBUG(dbgs() << "User: " << *LStart << " : " << *RStart << "\n");
      tryRefine(&*LStart, &*RStart);
      addCandidates(&*LStart, &*RStart);
      LStart++;
      RStart++;
      Changed = true;
    } else {
      RStart++;
    }
  }
  return Changed;
}

bool DiffEngine::propagateBackward(const BasicBlock *LB, const BasicBlock *RB) {
  bool Changed = false;
  auto LStart = LB->rbegin();
  auto RStart = RB->rbegin();
  auto LE = LB->rend();
  auto RE = RB->rend();

  while (LStart != LE && RStart != RE) {
    if (isRefined(&*LStart, &*RStart)) {
      LStart++;
      RStart++;
      continue;
    }
    MODEBUG(dbgs() << "Try Usee: " << *LStart << " : " << *RStart << "\n");
    if (judgeInstAsUsee(&*LStart, &*RStart)) {
      MODEBUG(dbgs() << "Usee: " << *LStart << " : " << *RStart << "\n");
      tryRefine(&*LStart, &*RStart);
      addCandidates(&*LStart, &*RStart);
      Changed = true;
    }
    LStart++;
    RStart++;
  }
  return Changed;
}

void DiffEngine::diff(const Function *L, const Function *R) {
  assert(L->arg_size() == R->arg_size() &&
         "Function's signature must be identical");

  // ReturnInsts are always corresponding
  auto *LReturn = findSingleReturn(L);
  auto *RReturn = findSingleReturn(R);
  if (LReturn && RReturn)
    refine(LReturn, RReturn);

  // Arguments are corresponding to each other
  for (int i = 0; i < L->arg_size(); ++i)
    refine(L->getArg(i), R->getArg(i));

  // Entry blocks are corresponding to each other
  tryRefine(&(L->getEntryBlock()), &(R->getEntryBlock()));
  // Exit blocks are corresponding to each other
  if (LReturn && RReturn)
    tryRefine(LReturn->getParent(), RReturn->getParent());

  bool Changed = true;
  while (!BBWorklist.empty()) {
    Changed = false;
    auto [LB, RB] = BBWorklist.front();
    BBWorklist.pop();

    if (isRefined(LB) || isRefined(RB))
      continue;

    MODEBUG(dbgs() << "BB:" << LB->getName() << " : " << RB->getName() << "\n");

    // Drop all tentative values
    TentativeValues.clear();

    // Forward propagating equivalence
    Changed |= propagateForward(LB, RB);

    // Backward propagating equivalence
    Changed |= propagateBackward(LB, RB);

    // Add all predecessors to BBWorklist
    auto LPred = pred_begin(LB);
    auto RPred = pred_begin(RB);
    for (int i = 0; i < pred_size(LB) && i < pred_size(RB); ++i) {
      tryRefine(*LPred, *RPred);
      LPred++;
      RPred++;
    }

    // Process candidates
    for (auto iter = InstWorklist.begin(); iter != InstWorklist.end();) {
      auto &[LI, RI] = *iter;
      MODEBUG(dbgs() << "Candidates:" << *LI << " : " << *RB << "\n");

      if (isRefined(LI) || isRefined(RI)) {
        iter = InstWorklist.erase(iter);
        continue;
      }

      if (LI->getParent() == LB && RI->getParent() == RB &&
          judgeInstAsUser(LI, RI)) {
        Changed = true;
        tryRefine(LI, RI);
        addCandidates(LI, RI);
      }

      iter++;
    }

    if (TentativeValues.size() >= std::min(LB->size(), RB->size()) / 2) {
      // Flush all tentative values
      for (auto VP : TentativeValues)
        Values.insert(VP);
      refineBB(LB, RB);
    }
  }
}

bool DiffEngine::identicalConst(const Constant *L, const Constant *R) {
  // Use equality as a preliminary filter.
  if (L == R)
    return true;

  if (L->getValueID() != R->getValueID())
    return false;

  // Compare constant expressions structurally.
  if (isa<ConstantExpr>(L))
    return identicalConstExpr(cast<ConstantExpr>(L), cast<ConstantExpr>(R));

  // Constants of the "same type" don't always actually have the same
  // type; I don't know why.  Just white-list them.
  if (isa<ConstantPointerNull>(L) || isa<UndefValue>(L) ||
      isa<ConstantAggregateZero>(L))
    return true;

  // Block addresses only match if we've already encountered the
  // block.  FIXME: tentative matches?
  if (isa<BlockAddress>(L))
    return Blocks[cast<BlockAddress>(L)->getBasicBlock()] ==
           cast<BlockAddress>(R)->getBasicBlock();

  // If L and R are ConstantVectors, compare each element
  if (isa<ConstantVector>(L)) {
    const ConstantVector *CVL = cast<ConstantVector>(L);
    const ConstantVector *CVR = cast<ConstantVector>(R);
    if (CVL->getType()->getNumElements() != CVR->getType()->getNumElements())
      return false;
    for (unsigned i = 0; i < CVL->getType()->getNumElements(); i++) {
      if (!identicalConst(CVL->getOperand(i), CVR->getOperand(i)))
        return false;
    }
    return true;
  }

  // If L and R are ConstantArrays, compare the element count and types.
  if (isa<ConstantArray>(L)) {
    const ConstantArray *CAL = cast<ConstantArray>(L);
    const ConstantArray *CAR = cast<ConstantArray>(R);
    // Sometimes a type may be equivalent, but not uniquified---e.g. it may
    // contain a GEP instruction. Do a deeper comparison of the types.
    if (CAL->getType()->getNumElements() != CAR->getType()->getNumElements())
      return false;

    for (unsigned I = 0; I < CAL->getType()->getNumElements(); ++I) {
      if (!identicalConst(CAL->getAggregateElement(I),
                          CAR->getAggregateElement(I)))
        return false;
    }

    return true;
  }

  // If L and R are ConstantStructs, compare each field and type.
  if (isa<ConstantStruct>(L)) {
    const ConstantStruct *CSL = cast<ConstantStruct>(L);
    const ConstantStruct *CSR = cast<ConstantStruct>(R);

    const StructType *LTy = cast<StructType>(CSL->getType());
    const StructType *RTy = cast<StructType>(CSR->getType());

    // The StructTypes should have the same attributes. Don't use
    // isLayoutIdentical(), because that just checks the element pointers,
    // which may not work here.
    if (LTy->getNumElements() != RTy->getNumElements() ||
        LTy->isPacked() != RTy->isPacked())
      return false;

    for (unsigned I = 0; I < LTy->getNumElements(); I++) {
      const Value *LAgg = CSL->getAggregateElement(I);
      const Value *RAgg = CSR->getAggregateElement(I);

      if (!provedRefined(LAgg, RAgg))
        return false;
    }

    return true;
  }

  return false;
}

bool DiffEngine::judgeInstAsUser(const Instruction *L, const Instruction *R) {
  if (isRefined(L, R))
    return true;

  // Different opcodes always imply different operations.
  if (L->getOpcode() != R->getOpcode())
    return false;

  if (isa<CmpInst>(L)) {
    if (cast<CmpInst>(L)->getPredicate() != cast<CmpInst>(R)->getPredicate())
      return false;
  } else if (isa<CallInst>(L)) {
    return identicalCall(cast<CallInst>(*L), cast<CallInst>(*R));
  } else if (isa<PHINode>(L)) {
    const PHINode &LI = cast<PHINode>(*L);
    const PHINode &RI = cast<PHINode>(*R);

    if (LI.getType() != RI.getType())
      return false;

    if (LI.getNumIncomingValues() != RI.getNumIncomingValues())
      return false;

    for (unsigned I = 0; I < LI.getNumIncomingValues(); ++I) {
      tryRefine(LI.getIncomingBlock(I), RI.getIncomingBlock(I));

      if (!provedRefined(LI.getIncomingValue(I), RI.getIncomingValue(I)))
        return false;
    }

    return true;
  } else if (isa<InvokeInst>(L)) {
    const InvokeInst &LI = cast<InvokeInst>(*L);
    const InvokeInst &RI = cast<InvokeInst>(*R);
    return identicalCall(LI, RI);
  } else if (isa<BranchInst>(L)) {
    const BranchInst *LI = cast<BranchInst>(L);
    const BranchInst *RI = cast<BranchInst>(R);

    if (LI->isConditional() && RI->isConditional()) {
      tryRefine(LI->getSuccessor(0), RI->getSuccessor(0));
      tryRefine(LI->getSuccessor(1), RI->getSuccessor(1));
      if (!provedRefined(LI->getCondition(), RI->getCondition())) {
        // Or possibly in reverse order
        tryRefine(LI->getSuccessor(1), RI->getSuccessor(0));
        tryRefine(LI->getSuccessor(0), RI->getSuccessor(1));
      }

    } else if (LI->isUnconditional() && RI->isUnconditional())
      tryRefine(LI->getSuccessor(0), RI->getSuccessor(0));

    return true;
  } else if (isa<IndirectBrInst>(L)) {
    const IndirectBrInst *LI = cast<IndirectBrInst>(L);
    const IndirectBrInst *RI = cast<IndirectBrInst>(R);
    if (LI->getNumDestinations() != RI->getNumDestinations())
      return false;

    return provedRefined(LI->getAddress(), RI->getAddress());

  } else if (isa<SwitchInst>(L)) {
    const SwitchInst *LI = cast<SwitchInst>(L);
    const SwitchInst *RI = cast<SwitchInst>(R);
    if (!provedRefined(LI->getCondition(), RI->getCondition()))
      return false;

    bool Difference = false;

    DenseMap<const ConstantInt *, const BasicBlock *> LCases;
    for (auto Case : LI->cases())
      LCases[Case.getCaseValue()] = Case.getCaseSuccessor();

    for (auto Case : RI->cases()) {
      const ConstantInt *CaseValue = Case.getCaseValue();
      const BasicBlock *LCase = LCases[CaseValue];
    }

    return !Difference;
  } else if (isa<UnreachableInst>(L)) {
    return true;
  } else if (isa<AllocaInst>(L)) {
    return cast<AllocaInst>(L)->getAllocatedType()->getTypeID() ==
           cast<AllocaInst>(R)->getAllocatedType()->getTypeID();
  }

  if (L->getNumOperands() != R->getNumOperands())
    return false;

  for (unsigned I = 0, E = L->getNumOperands(); I != E; ++I) {
    Value *LO = L->getOperand(I), *RO = R->getOperand(I);
    if (!provedRefined(LO, RO))
      return false;
  }

  return true;
}

bool DiffEngine::judgeInstAsUsee(const Instruction *L, const Instruction *R) {
  if (isRefined(L, R))
    return true;

  if (isRefined(L) || isRefined(R))
    return false;

  // Different opcodes always imply different operations.
  if (L->getOpcode() != R->getOpcode())
    return false;

  if (L->getValueID() != R->getValueID())
    return false;

  if (isa<CmpInst>(L)) {
    if (cast<CmpInst>(L)->getPredicate() != cast<CmpInst>(R)->getPredicate())
      return false;
  } else if (isa<PHINode>(L)) {
    const PHINode &LI = cast<PHINode>(*L);
    const PHINode &RI = cast<PHINode>(*R);

    if (LI.getType() != RI.getType())
      return false;

    if (LI.getNumIncomingValues() != RI.getNumIncomingValues())
      return false;

    for (unsigned I = 0; I < LI.getNumIncomingValues(); ++I) {
      auto *LIncomingValue = LI.getIncomingValue(I);
      auto *RIncomingValue = RI.getIncomingValue(I);
      if ((!isa<Instruction>(LIncomingValue) ||
           !isa<Instruction>(RIncomingValue)) &&
          !provedRefined(LIncomingValue, RIncomingValue))
        return false;
      tryRefine(LI.getIncomingBlock(I), RI.getIncomingBlock(I));
    }

    return true;
  }

  if (isa<IndirectBrInst>(L)) {
    const IndirectBrInst *LI = cast<IndirectBrInst>(L);
    const IndirectBrInst *RI = cast<IndirectBrInst>(R);
    if (LI->getNumDestinations() != RI->getNumDestinations())
      return false;
  } else if (isa<SwitchInst>(L)) {
    const SwitchInst *LI = cast<SwitchInst>(L);
    const SwitchInst *RI = cast<SwitchInst>(R);
    if (!provedRefined(LI->getCondition(), RI->getCondition()))
      return false;

    bool Difference = false;

    DenseMap<const ConstantInt *, const BasicBlock *> LCases;
    for (auto Case : LI->cases())
      LCases[Case.getCaseValue()] = Case.getCaseSuccessor();

    for (auto Case : RI->cases()) {
      const ConstantInt *CaseValue = Case.getCaseValue();
      const BasicBlock *LCase = LCases[CaseValue];
    }

    return !Difference;
  } else if (isa<UnreachableInst>(L)) {
    return true;
  } else if (isa<AllocaInst>(L)) {
    return cast<AllocaInst>(L)->getAllocatedType()->getTypeID() ==
           cast<AllocaInst>(R)->getAllocatedType()->getTypeID();
  }

  if (L->getNumUses() != R->getNumUses())
    return false;

  auto LUI = L->use_begin();
  auto RUI = R->use_begin();
  for (unsigned I = 0, E = L->getNumUses(); I != E; ++I)
    if (!provedRefined(LUI->getUser(), RUI->getUser()))
      return false;

  return true;
}

bool DiffEngine::identicalConstExpr(const ConstantExpr *L,
                                    const ConstantExpr *R) {
  if (L == R)
    return true;

  if (L->getOpcode() != R->getOpcode())
    return false;

  switch (L->getOpcode()) {
  case Instruction::ICmp:
  case Instruction::FCmp:
    if (L->getPredicate() != R->getPredicate())
      return false;
    break;
  default:
    break;
  }

  if (L->getNumOperands() != R->getNumOperands())
    return false;

  for (unsigned I = 0, E = L->getNumOperands(); I != E; ++I) {
    const auto *LOp = L->getOperand(I);
    const auto *ROp = R->getOperand(I);

    if (!identicalConst(LOp, ROp))
      return false;
  }

  return true;
}

bool DiffEngine::identicalCall(const CallBase &L, const CallBase &R) {

  if (!provedRefined(L.getCalledOperand(), R.getCalledOperand()))
    return false;

  if (L.arg_size() != R.arg_size())
    return false;

  for (unsigned I = 0, E = L.arg_size(); I != E; ++I)
    if (!provedRefined(L.getArgOperand(I), R.getArgOperand(I)))
      return false;

  return true;
}

bool DiffEngine::mustDifferent(const Instruction *L, const Instruction *R) {
  if (L->getOpcode() != R->getOpcode())
    return true;
  if (L->getValueID() != R->getValueID())
    return true;
  return false;
}

bool DiffEngine::provedRefined(const Value *L, const Value *R) {
  if (isRefined(L, R))
    return true;
  if (isa<Constant>(L) && isa<Constant>(R))
    return identicalConst(cast<Constant>(L), cast<Constant>(R));
  return false;
}

void DiffEngine::tryRefine(const Value *L, const Value *R) {
  if (isRefined(L) || isRefined(R))
    return;
  TentativeValues[L] = R;
  TentativeValues[R] = L;
}

void DiffEngine::tryRefine(const BasicBlock *L, const BasicBlock *R) {
  if (!Visited.contains(L))
    Visited[L] = 0;
  if (!Visited.contains(R))
    Visited[R] = 0;
  if (Visited[L] > 2 || Visited[R] > 2 || isRefined(L) || isRefined(R))
    return;

  BBWorklist.push({L, R});

  Visited[L]++;
  MODEBUG(dbgs() << "Visited:" << L->getName() << "  " << Visited[L] << "\n");
  Visited[R]++;
  MODEBUG(dbgs() << "Visited:" << R->getName() << "  " << Visited[R] << "\n");
}

void DiffEngine::addCandidates(const Instruction *L, const Instruction *R) {
  // Successors of refined value pair are also candidates for refinement.
  for (auto LU : L->users())
    for (auto RU : R->users())
      if (auto LUI = dyn_cast<Instruction>(LU))
        if (auto RUI = dyn_cast<Instruction>(RU))
          if (!isRefined(LUI) && !isRefined(RUI) && !mustDifferent(LUI, RUI))
            InstWorklist.insert({LUI, RUI});
}

void DiffEngine::refine(const Value *L, const Value *R) {
  Values[L] = R;
  Values[R] = L;
}

void DiffEngine::refineBB(const BasicBlock *L, const BasicBlock *R) {

  Values[L] = R;
  Values[R] = L;
  Blocks[L] = R;
  Blocks[R] = L;
}

bool DiffEngine::isRefined(const Value *V) {
  return TentativeValues.contains(V) || Values.contains(V);
}

bool DiffEngine::isRefined(const Value *L, const Value *R) {
  if (TentativeValues.contains(R))
    return TentativeValues[L] == R;
  if (Values.contains(L))
    return Values[L] == R;

  return false;
}

bool DiffEngine::isBBRefined(const BasicBlock *L, const BasicBlock *R) {
  if (!Blocks.contains(L))
    return false;
  return Blocks[L] == R;
}
