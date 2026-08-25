#ifndef COCOONS_SUBSTITUTIONPASS_H
#define COCOONS_SUBSTITUTIONPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"

namespace cocoons {

class SubstitutionPass : public llvm::PassInfoMixin<SubstitutionPass> {
public:
    llvm::PreservedAnalyses run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);
    static bool isEnabled();

private:
    void substitute(llvm::BinaryOperator *Bo);

    void substituteAdd(llvm::BinaryOperator *Bo);
    void substituteSub(llvm::BinaryOperator *Bo);
    void substituteAnd(llvm::BinaryOperator *Bo);
    void substituteOr(llvm::BinaryOperator *Bo);
    void substituteXor(llvm::BinaryOperator *Bo);
};

} // namespace cocoons

#endif
