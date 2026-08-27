#include "cocoons/FlatteningPass.h"
#include "cocoons/Config.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cstdlib>
#include <vector>
#include <random>
#include <map>

using namespace llvm;

namespace cocoons {

bool FlatteningPass::isEnabled() {
    const Config &C = Config::get();
    return C.Fla != EnableMode::DefaultOff;
}

PreservedAnalyses FlatteningPass::run(Function &F, FunctionAnalysisManager &AM) {
    Config &Cfg = Config::get();
    unsigned BC = F.size();
    if (!Cfg.shouldRunFla(F, {}, BC)) {
        return PreservedAnalyses::all();
    }

    if (!flatten(F)) {
        return PreservedAnalyses::all();
    }

    errs() << ">>> [Cocoons] Control Flow Flattening applied on " << F.getName() << "\n";
    return PreservedAnalyses::none();
}

bool FlatteningPass::flatten(Function &F) {
    std::vector<PHINode *> PHIs;
    std::vector<Instruction *> Regs;
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            if (auto *PN = dyn_cast<PHINode>(&I)) {
                PHIs.push_back(PN);
            } else {
                for (User *U : I.users()) {
                    if (auto *UI = dyn_cast<Instruction>(U)) {
                        if (UI->getParent() != &BB) {
                            Regs.push_back(&I);
                            break;
                        }
                    }
                }
            }
        }
    }
    for (PHINode *PN : PHIs) DemotePHIToStack(PN);
    for (Instruction *I : Regs) DemoteRegToStack(*I);

    std::vector<BasicBlock *> OrigBBs;
    for (BasicBlock &BB : F) {
        OrigBBs.push_back(&BB);
    }

    BasicBlock *EntryBB = &F.getEntryBlock();
    OrigBBs.erase(OrigBBs.begin());

    if (EntryBB->getTerminator()->getNumSuccessors() > 1) {
        BasicBlock *NewBB = SplitBlock(EntryBB, EntryBB->getTerminator());
        OrigBBs.insert(OrigBBs.begin(), NewBB);
    }

    Config &Cfg = Config::get();
    std::uniform_int_distribution<uint32_t> Dist;
    std::map<BasicBlock *, uint32_t> BBKeys;
    for (BasicBlock *BB : OrigBBs) {
        BBKeys[BB] = Dist(Cfg.Rng);
    }

    BasicBlock *LoopHeader = BasicBlock::Create(F.getContext(), "fla.header", &F, EntryBB->getNextNode());
    BasicBlock *LoopEnd = BasicBlock::Create(F.getContext(), "fla.end", &F);
    BasicBlock *SwitchDefault = BasicBlock::Create(F.getContext(), "fla.default", &F, LoopEnd);

    IRBuilder<> EntryBuilder(&EntryBB->front());
    AllocaInst *SwitchVar = EntryBuilder.CreateAlloca(Type::getInt32Ty(F.getContext()), nullptr, "fla.switchVar");

    BasicBlock *FirstBB = EntryBB->getTerminator()->getSuccessor(0);
    EntryBB->getTerminator()->eraseFromParent();
    IRBuilder<> EntryTermBuilder(EntryBB);
    EntryTermBuilder.CreateStore(ConstantInt::get(Type::getInt32Ty(F.getContext()), BBKeys[FirstBB]), SwitchVar);
    EntryTermBuilder.CreateBr(LoopHeader);

    IRBuilder<> HeaderBuilder(LoopHeader);
    Value *LoadKey = HeaderBuilder.CreateLoad(Type::getInt32Ty(F.getContext()), SwitchVar, "fla.load");
    SwitchInst *SwitchI = HeaderBuilder.CreateSwitch(LoadKey, SwitchDefault);

    IRBuilder<>(LoopEnd).CreateBr(LoopHeader);
    IRBuilder<>(SwitchDefault).CreateBr(LoopEnd);

    for (BasicBlock *BB : OrigBBs) {
        SwitchI->addCase(ConstantInt::get(Type::getInt32Ty(F.getContext()), BBKeys[BB]), BB);
        Instruction *Term = BB->getTerminator();

        if (Term->getNumSuccessors() == 0) continue;

        IRBuilder<> TermBuilder(Term);
        if (auto *BI = dyn_cast<BranchInst>(Term)) {
            if (BI->isUnconditional()) {
                TermBuilder.CreateStore(
                    ConstantInt::get(Type::getInt32Ty(F.getContext()), BBKeys[BI->getSuccessor(0)]),
                    SwitchVar);
            } else {
                Value *SelectKey = TermBuilder.CreateSelect(BI->getCondition(),
                    ConstantInt::get(Type::getInt32Ty(F.getContext()), BBKeys[BI->getSuccessor(0)]),
                    ConstantInt::get(Type::getInt32Ty(F.getContext()), BBKeys[BI->getSuccessor(1)]));
                TermBuilder.CreateStore(SelectKey, SwitchVar);
            }
        }
        else if (auto *SI = dyn_cast<SwitchInst>(Term)) {
            Value *NextKey = ConstantInt::get(Type::getInt32Ty(F.getContext()), BBKeys[SI->getDefaultDest()]);
            for (auto Case : SI->cases()) {
                NextKey = TermBuilder.CreateSelect(
                    TermBuilder.CreateICmpEQ(SI->getCondition(), Case.getCaseValue()),
                    ConstantInt::get(Type::getInt32Ty(F.getContext()), BBKeys[Case.getCaseSuccessor()]),
                    NextKey
                );
            }
            TermBuilder.CreateStore(NextKey, SwitchVar);
        }

        TermBuilder.CreateBr(LoopEnd);
        Term->eraseFromParent();
    }

    return true;
}

} // namespace cocoons
