#include "cocoons/SubstitutionPass.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <vector>
#include <random>

using namespace llvm;

namespace cocoons {

static bool envBool(const char *Name, bool Default) {
    const char *V = std::getenv(Name);
    if (!V) return Default;
    return StringRef(V).equals_insensitive("1")
        || StringRef(V).equals_insensitive("true")
        || StringRef(V).equals_insensitive("on")
        || StringRef(V).equals_insensitive("yes");
}

static int envInt(const char *Name, int Default) {
    const char *V = std::getenv(Name);
    if (!V) return Default;
    int N = Default;
    if (StringRef(V).getAsInteger(10, N)) return Default;
    return N < 1 ? 1 : N;
}

static bool EnableSub = envBool("COCOONS_ENABLE_SUB", true);
static int  SubLoop   = envInt("COCOONS_SUB_LOOP", 1);

static std::random_device RD;
static std::mt19937 Gen(RD());

PreservedAnalyses SubstitutionPass::run(Function &F, FunctionAnalysisManager &AM) {
    if (F.hasFnAttribute(Attribute::OptimizeNone)) {
        return PreservedAnalyses::all();
    }
    if (F.getMetadata("cocoons_protected")) {
        return PreservedAnalyses::all();
    }

    bool Changed = false;

    for (int i = 0; i < SubLoop; ++i) {
        std::vector<BinaryOperator *> WorkList;
        for (auto &BB: F) {
            for (auto &I : BB) {
                if (auto *Bo = dyn_cast<BinaryOperator>(&I)) {
                    if (Bo->getType()->isIntegerTy()) {
                        WorkList.push_back(Bo);
                    }
                }
            }
        }

        if (WorkList.empty()) {
            break;
        }

        for (auto *Bo: WorkList) {
            substitute(Bo);
            if (Bo->use_empty()) {
                Bo->eraseFromParent();
                Changed = true;
            }
        }
    }

    if (Changed) {
        errs() << ">>> [Cocoons] Instruction Substitution applied on " << F.getName() << "\n";
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool SubstitutionPass::isEnabled() {
    return EnableSub;
}

void SubstitutionPass::substitute(BinaryOperator *Bo) {
    switch (Bo->getOpcode()) {
        case Instruction::Add:
            substituteAdd(Bo);
            break;
        case Instruction::Sub:
            substituteSub(Bo);
            break;
        case Instruction::And:
            substituteAnd(Bo);
            break;
        case Instruction::Or:
            substituteOr(Bo);
            break;
        case Instruction::Xor:
            substituteXor(Bo);
            break;
        default:
            break;
    }
}

void SubstitutionPass::substituteAdd(BinaryOperator *Bo) {
    IRBuilder<> Builder(Bo);
    Value *LHS = Bo->getOperand(0);
    Value *RHS = Bo->getOperand(1);
    Value *NewVal = nullptr;

    std::uniform_int_distribution<int> Dist(0, 3);
    int Choice = Dist(Gen);

    switch (Choice) {
        case 0: {
            Value *NotB = Builder.CreateNot(RHS);
            Value *Sub1 = Builder.CreateSub(LHS, NotB);
            NewVal = Builder.CreateSub(Sub1, ConstantInt::get(Bo->getType(), 1));
            break;
        }
        case 1: {
            Value *VXor = Builder.CreateXor(LHS, RHS);
            Value *VAnd = Builder.CreateAnd(LHS, RHS);
            Value *VMul2 = Builder.CreateMul(VAnd, ConstantInt::get(Bo->getType(), 2));
            NewVal = Builder.CreateAdd(VXor, VMul2);
            break;
        }
        case 2: {
            Value *VOr = Builder.CreateOr(LHS, RHS);
            Value *VAnd = Builder.CreateAnd(LHS, RHS);
            NewVal = Builder.CreateAdd(VOr, VAnd);
            break;
        }
        case 3: {
            Value *VOr = Builder.CreateOr(LHS, RHS);
            Value *VMul2 = Builder.CreateMul(VOr, ConstantInt::get(Bo->getType(), 2));
            Value *VXor = Builder.CreateXor(LHS, RHS);
            NewVal = Builder.CreateSub(VMul2, VXor);
            break;
        }
    }

    if (NewVal) {
        Bo->replaceAllUsesWith(NewVal);
    }
}

void SubstitutionPass::substituteSub(BinaryOperator *Bo) {
    IRBuilder<> Builder(Bo);
    Value *LHS = Bo->getOperand(0);
    Value *RHS = Bo->getOperand(1);
    Value *NewVal = nullptr;

    std::uniform_int_distribution<int> Dist(0, 2);
    int Choice = Dist(Gen);

    switch (Choice) {
        case 0: {
            Value *NotB = Builder.CreateNot(RHS);
            Value *Add1 = Builder.CreateAdd(LHS, NotB);
            NewVal = Builder.CreateAdd(Add1, ConstantInt::get(Bo->getType(), 1));
            break;
        }
        case 1: {
            Value *NotB = Builder.CreateNot(RHS);
            Value *VXor = Builder.CreateXor(LHS, NotB);
            Value *VAnd = Builder.CreateAnd(LHS, NotB);
            Value *VMul2 = Builder.CreateMul(VAnd, ConstantInt::get(Bo->getType(), 2));
            Value *Add1 = Builder.CreateAdd(VXor, VMul2);
            NewVal = Builder.CreateAdd(Add1, ConstantInt::get(Bo->getType(), 1));
            break;
        }
        case 2: {
            Value *NotA = Builder.CreateNot(LHS);
            Value *NotB = Builder.CreateNot(RHS);
            Value *Term1 = Builder.CreateAnd(LHS, NotB);
            Value *Term2 = Builder.CreateAnd(NotA, RHS);
            NewVal = Builder.CreateSub(Term1, Term2);
            break;
        }
    }

    if (NewVal) {
        Bo->replaceAllUsesWith(NewVal);
    }
}

void SubstitutionPass::substituteAnd(BinaryOperator *Bo) {
    IRBuilder<> Builder(Bo);
    Value *LHS = Bo->getOperand(0);
    Value *RHS = Bo->getOperand(1);
    Value *NewVal = nullptr;

    std::uniform_int_distribution<int> Dist(0, 1);
    int Choice = Dist(Gen);

    switch (Choice) {
        case 0: {
            Value *NotA = Builder.CreateNot(LHS);
            Value *NotB = Builder.CreateNot(RHS);
            Value *VOr = Builder.CreateOr(NotA, NotB);
            NewVal = Builder.CreateNot(VOr);
            break;
        }
        case 1: {
            Value *NotA = Builder.CreateNot(LHS);
            Value *VOr = Builder.CreateOr(NotA, RHS);
            NewVal = Builder.CreateAnd(VOr, LHS);
            break;
        }
    }

    if (NewVal) {
        Bo->replaceAllUsesWith(NewVal);
    }
}

void SubstitutionPass::substituteOr(BinaryOperator *Bo) {
    IRBuilder<> Builder(Bo);
    Value *LHS = Bo->getOperand(0);
    Value *RHS = Bo->getOperand(1);
    Value *NewVal = nullptr;

    std::uniform_int_distribution<int> Dist(0, 1);
    int Choice = Dist(Gen);

    switch (Choice) {
        case 0: {
            Value *NotA = Builder.CreateNot(LHS);
            Value *NotB = Builder.CreateNot(RHS);
            Value *VAnd = Builder.CreateAnd(NotA, NotB);
            NewVal = Builder.CreateNot(VAnd);
            break;
        }
        case 1: {
            Value *VAnd = Builder.CreateAnd(LHS, RHS);
            Value *VXor = Builder.CreateXor(LHS, RHS);
            NewVal = Builder.CreateOr(VAnd, VXor);
            break;
        }
    }

    if (NewVal) {
        Bo->replaceAllUsesWith(NewVal);
    }
}

void SubstitutionPass::substituteXor(BinaryOperator *Bo) {
    IRBuilder<> Builder(Bo);
    Value *LHS = Bo->getOperand(0);
    Value *RHS = Bo->getOperand(1);
    Value *NewVal = nullptr;

    std::uniform_int_distribution<int> Dist(0, 1);
    int Choice = Dist(Gen);

    switch (Choice) {
        case 0: {
            Value *NotA = Builder.CreateNot(LHS);
            Value *NotB = Builder.CreateNot(RHS);
            Value *Term1 = Builder.CreateAnd(NotA, RHS);
            Value *Term2 = Builder.CreateAnd(LHS, NotB);
            NewVal = Builder.CreateOr(Term1, Term2);
            break;
        }
        case 1: {
            Value *VOr = Builder.CreateOr(LHS, RHS);
            Value *VAnd = Builder.CreateAnd(LHS, RHS);
            NewVal = Builder.CreateSub(VOr, VAnd);
            break;
        }
    }

    if (NewVal) {
        Bo->replaceAllUsesWith(NewVal);
    }
}

} // namespace cocoons
