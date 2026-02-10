#ifndef LLVM_TRANSFORMS_COCOONS_STRINGOBFUSCATIONPASS_H
#define LLVM_TRANSFORMS_COCOONS_STRINGOBFUSCATIONPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <cstdint>
#include <vector>

using namespace llvm;

namespace cocoons {

// class StringObfuscationPass : public PassInfoMixin<StringObfuscationPass> {
// public:
//     PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

//     static bool isEnabled();

// private:
//     struct ObfuscateEntry {
//         GlobalVariable *GV;
//         uint8_t Key;
//     };

// private:
//     void processVariable(GlobalVariable *GV, std::vector<ObfuscateEntry> &ObfList, Module &M);
//     Function* createDecodeHelper(Module &M);
//     void createMainDecryptFunc(Module &M, std::vector<ObfuscateEntry> &ObfList);
//     void encryptRealData(GlobalVariable *TargetGV, std::vector<ObfuscateEntry> &ObfList, Module &M);
// };

class StringObfuscationPass : public PassInfoMixin<StringObfuscationPass> {
public:
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

    static bool isEnabled();

private:
    /**
     * @brief 处理全局变量，识别可混淆的字符串
     * @param GV 当前全局变量
     * @param Targets 存储所有可混淆字符串的全局变量列表
     * @param M 当前 Module
     */
    void processVariable(GlobalVariable *GV, std::vector<GlobalVariable *> &Targets, Module &M);
    
    /** 
     * @brief 加密字符串
     */
    bool encryptRealData(Module &M, GlobalVariable *TargetGV, uint8_t Key);
    /**
     * @brief 生成混淆元数据并写入指定 Section
     * @param M 当前 Module
     * @param EncryptedGV 已加密的全局字符串变量
     * @param Len 字符串长度
     * @param Key 混淆所使用的 XOR Key
     */
    void emitMetadata(Module &M, GlobalVariable *EncryptedGV, uint32_t Len, uint8_t Key);

    /**
     * @brief 将元数据变量标记为已使用，防止被链接器优化剔除
     */
    void markUsed(Module &M, GlobalVariable *GV);

    /**
     *
     */
    void injectDecrypter(Module &M);
};

} // namespace cocoons

#endif