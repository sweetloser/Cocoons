#ifndef COCOONS_STRINGOBFUSCATIONPASS_H
#define COCOONS_STRINGOBFUSCATIONPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <cstdint>
#include <vector>
#include <set>

namespace cocoons {

class StringObfuscationPass : public llvm::PassInfoMixin<StringObfuscationPass> {
public:
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

    static bool isEnabled();

private:
    void processVariable(llvm::GlobalVariable *GV,
                         std::vector<llvm::GlobalVariable *> &Targets,
                         std::set<llvm::GlobalVariable *> &Visited,
                         llvm::Module &M);

    bool encryptRealData(llvm::Module &M, llvm::GlobalVariable *TargetGV, uint8_t BaseKey);

    void injectLocalDecryption(llvm::Module &M, llvm::GlobalVariable *EncryptedGV,
                               uint32_t Len, uint8_t BaseKey);

    void createDecryptionLoop(llvm::Instruction *InsertBefore, llvm::Value *StrPtr,
                              uint32_t Len, uint8_t BaseKey, llvm::Module &M);
};

} // namespace cocoons

#endif
