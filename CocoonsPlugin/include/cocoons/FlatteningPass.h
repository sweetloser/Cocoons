#ifndef COCOONS_FLATTENINGPASS_H
#define COCOONS_FLATTENINGPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"

namespace cocoons {

class FlatteningPass : public llvm::PassInfoMixin<FlatteningPass> {
public:
    llvm::PreservedAnalyses run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);
    static bool isEnabled();

private:
    bool flatten(llvm::Function &F);
};

} // namespace cocoons

#endif
