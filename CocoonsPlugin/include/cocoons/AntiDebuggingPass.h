#ifndef COCOONS_ANTIDEBUGPASS_H
#define COCOONS_ANTIDEBUGPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"

namespace cocoons {

class AntiDebuggingPass : public llvm::PassInfoMixin<AntiDebuggingPass> {
public:
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
    static bool isEnabled();
};

} // namespace cocoons

#endif
