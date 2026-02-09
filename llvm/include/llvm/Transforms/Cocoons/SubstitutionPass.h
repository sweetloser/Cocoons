#ifndef LLVM_TRANSFORMS_COCOONS_SUBSTITUTIONPASS_H
#define LLVM_TRANSFORMS_COCOONS_SUBSTITUTIONPASS_H
#include "llvm/IR/Analysis.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

namespace cocoons {
class SubstitutionPass: public PassInfoMixin<SubstitutionPass> {
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
    static bool isEnabled();
    
private:
    void substitute(BinaryOperator *Bo);
};
} // namespace cocoons
#endif