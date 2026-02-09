#ifndef LLVM_TRANSFORMS_COCOONS_STRINGOBFUSCATIONPASS_H
#define LLVM_TRANSFORMS_COCOONS_STRINGOBFUSCATIONPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <cstdint>
#include <vector>

using namespace llvm;

namespace cocoons {

class StringObfuscationPass : public PassInfoMixin<StringObfuscationPass> {
public:
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

    static bool isEnabled();

private:
    struct ObfuscateEntry {
        GlobalVariable *GV;
        uint8_t Key;
    };

private:
    void processVariable(GlobalVariable *GV, std::vector<ObfuscateEntry> &ObfList, Module &M);
    Function* createDecodeHelper(Module &M);
    void createMainDecryptFunc(Module &M, std::vector<ObfuscateEntry> &ObfList);
    void encryptRealData(GlobalVariable *TargetGV, std::vector<ObfuscateEntry> &ObfList, Module &M);
};
} // namespace cocoons

#endif