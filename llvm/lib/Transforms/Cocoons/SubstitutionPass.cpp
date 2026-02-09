#include "llvm/Transforms/Cocoons/SubstitutionPass.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include <vector>

namespace cocoons {
// 定义开关
// 这里的字符串 "cocoons-enable-sub" 就是你将在命令行里敲的参数名
static cl::opt<bool> EnableSub(
    "cocoons-enable-sub",
    cl::init(false),
    cl::desc("Enable Substitutuin for iOS")
);

PreservedAnalyses SubstitutionPass::run(Function &F, FunctionAnalysisManager &AM) {
    
    // 1. 全量扫描，收集所有的二元操作符
    std::vector<BinaryOperator *>WorkList;

    for (auto &BB: F) {
        for (auto &I : BB) {
            if (auto *Bo = dyn_cast<BinaryOperator>(&I)) {
                WorkList.push_back(Bo);
            }
        }
    }

    // 2. 执行指令替换
    bool Changed = false;
    for (auto *Bo: WorkList) {
        substitute(Bo);
        if (Bo->use_empty()) {
            Bo->eraseFromParent();
            Changed = true;
        }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool SubstitutionPass::isEnabled() {
    return EnableSub;
}

void SubstitutionPass::substitute(BinaryOperator *Bo) {
    IRBuilder<> Builder(Bo);
    Value *LHS = Bo->getOperand(0);
    Value *RHS = Bo->getOperand(1);
    Value *NewVal = nullptr;

    // 目前仅处理整数类型的加法 (Add)
    if (Bo->getOpcode() == Instruction::Add && Bo->getType()->isIntegerTy()) {
        // 使用经典变换：a + b => (a ^ b) + 2 * (a & b)
        Value *VXor = Builder.CreateXor(LHS, RHS);
        Value *VAnd = Builder.CreateAnd(LHS, RHS);
        Value *VMul2 = Builder.CreateMul(VAnd, ConstantInt::get(VAnd->getType(), 2));
        NewVal = Builder.CreateAdd(VXor, VMul2);
    }

    if (NewVal) {
        Bo->replaceAllUsesWith(NewVal);
    }
}

} // namespace cocoons