#include "llvm/Transforms/Cocoons/StringObfuscation.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <cstdint>
#include <vector>

using namespace llvm;

// 定义开关
// 这里的字符串 "enable-str-obf" 就是你将在命令行里敲的参数名
static cl::opt<bool> EnableStrObf(
    "enable-str-obf", 
    cl::init(false), 
    cl::desc("Enable String Obfuscation for iOS"));

bool StringObfuscationPass::isEnabled() {
    return EnableStrObf;
}

PreservedAnalyses StringObfuscationPass::run(Module &M, ModuleAnalysisManager &AM) {
    std::vector<ObfuscateEntry> ObfList;

    // 1. 识别标记了 annotate("obfuscate") 的全局变量
    GlobalVariable *AnnoGV = M.getGlobalVariable("llvm.global.annotations");
    if (!AnnoGV) { return PreservedAnalyses::all(); }

    ConstantArray *AnnArr = dyn_cast<ConstantArray>(AnnoGV->getInitializer());
    if (!AnnArr) { return PreservedAnalyses::all(); }

    for (unsigned int i = 0; i < AnnArr->getNumOperands(); ++i) {
        ConstantStruct *CS = dyn_cast<ConstantStruct>(AnnArr->getOperand(i));
        if (!CS) { continue; }

        GlobalVariable *TargetGV = dyn_cast<GlobalVariable>(CS->getOperand(0)->stripPointerCasts());
        GlobalVariable  *AnnoStrGV = dyn_cast<GlobalVariable>(CS->getOperand(1)->stripPointerCasts());
        if (TargetGV && AnnoStrGV && AnnoStrGV->hasInitializer()) {
            ConstantDataSequential *AnnoData = dyn_cast<ConstantDataSequential>(AnnoStrGV->getInitializer());
            if (!AnnoData) { continue; }

            StringRef Annotation = AnnoData->getAsString();
            if (Annotation.starts_with("obfuscate")) {
                processVariable(TargetGV, ObfList, M);
            }
        }
    }

    if (!ObfList.empty()) {
        createMainDecryptFunc(M, ObfList);
    }

    return PreservedAnalyses::none();
}

void StringObfuscationPass::processVariable(GlobalVariable *GV, std::vector<ObfuscateEntry> &ObfList, Module &M) {
    if (!GV || !GV->hasInitializer()) return;

    Constant *Init = GV->getInitializer();
    Value *Stripped = Init->stripPointerCasts();
    
    // --- 探测点 A: 如果直接是字节数组 (终于到底了) ---
    if (ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(Stripped)) {
        errs() << ">>> [Level 1] 字节数组: " << GV->getName() << "\n";
        if (!CDS->getElementType()->isIntegerTy(8)) {
            errs() << ">>> 排除：不是 i8 类型，可能是 int 数组或其它数据。\n";
            return;
        }
        encryptRealData(GV, ObfList, M);
        return;
    } 

    // --- 探测点 B: 如果是 OC 结构体 (NSConstantString) ---
    if (ConstantStruct *CS = dyn_cast<ConstantStruct>(Stripped)) {
        errs() << ">>> [跳转] 识别到 OC 结构体，提取第 3 个成员...\n";
        if (CS->getNumOperands() >= 3) {
            Value *V = CS->getOperand(2)->stripPointerCasts();
            if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
                V = CE->getOperand(0)->stripPointerCasts();
            }
            if (GlobalVariable *nextGV = dyn_cast<GlobalVariable>(V)) {
                processVariable(nextGV, ObfList, M);
            }
        }
        return;
    }
    
    // --- 探测点 C: 如果是一个指针引用 (C 指针或变量引用) ---
    if (GlobalVariable *NextGV = dyn_cast<GlobalVariable>(Stripped)) {
        errs() << ">>> [跳转] 识别到变量引用: " << NextGV->getName() << "，继续挖掘...\n";
        processVariable(NextGV, ObfList, M); // 【递归】
        return;
    }
    // --- 探测点 D: 如果是复杂的常量表达式 (GEP 等) ---
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Stripped)) {
        Value *V = CE->getOperand(0)->stripPointerCasts();
        if (GlobalVariable *NextGV = dyn_cast<GlobalVariable>(V)) {
            processVariable(NextGV, ObfList, M); // 【递归】
        }
        return;
    }
}
void StringObfuscationPass::encryptRealData(GlobalVariable *TargetGV, std::vector<ObfuscateEntry> &ObfList, Module &M) {
    if (TargetGV && TargetGV->hasInitializer()) {
        Constant *ActualInit = TargetGV->getInitializer();
        if (ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(ActualInit)) {
            // 防止重复加密
            errs() << ">>>>>> 即将加密: " << TargetGV->getName() << " <<<<<<\n";

            if (TargetGV->getSection() == "__DATA,__obf_strings") return;

            errs() << ">>>>>> 执行加密逻辑: " << TargetGV->getName() << " <<<<<<\n";
            
            uint8_t key = 0xAD;
            std::vector<uint8_t> Data;
            for (unsigned i = 0; i < CDS->getNumElements(); ++i) {
                uint8_t Val = CDS->getElementAsInteger(i);
                // 最后一个 \0 不加密，否则 printf 无法结尾
                if (i < CDS->getNumElements() - 1) {
                    Val ^= key;
                }
                Data.push_back(Val);
            }

            // 更新底层数据
            TargetGV->setInitializer(ConstantDataArray::get(M.getContext(), Data));

            // 修改内存属性：必须设为非常量并迁移到可写段
            TargetGV->setConstant(false);
            // 注意：iOS/macOS 正确的段名建议用小写 data
            TargetGV->setSection("__DATA,__obf_strings");
            // 确保对齐
            TargetGV->setAlignment(MaybeAlign(1));

            ObfList.push_back({TargetGV, key});
            errs() << ">>>>>>> [DONE] Obfuscated: " << TargetGV->getName() << "\n";
        }
    }
}

void StringObfuscationPass::createMainDecryptFunc(Module &M, std::vector<ObfuscateEntry> &ObfList) {
    LLVMContext &Ctx = M.getContext();
    IRBuilder<> Builder(Ctx);

    // 1. 先造出通用的“工具函数”
    Function *Helper = createDecodeHelper(M);

    // 2. 定义主初始化函数
    FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
    Function *MainFunc = Function::Create(FTy, GlobalValue::InternalLinkage, ".llvm_decrypt_strings", &M);
    BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", MainFunc);
    Builder.SetInsertPoint(Entry);

    // 3. 遍历每个被标记的字符串，直接调用 Helper
    for (ObfuscateEntry Entry : ObfList) {
        GlobalVariable *GV = Entry.GV;
        uint64_t Len = cast<ConstantDataArray>(GV->getInitializer())->getType()->getNumElements();
        if (Len <= 1) continue;
        uint8_t Key = Entry.Key;

        // 准备参数: (char*)GV, (int)Len-1, (int)Key
        Value *DataPtr = Builder.CreateBitCast(GV, PointerType::get(Ctx, 0));
        Value *LenVal = ConstantInt::get(Type::getInt32Ty(Ctx), Len - 1);
        Value *KeyVal = ConstantInt::get(Type::getInt8Ty(Ctx), Key);

        // 发射调用指令
        Builder.CreateCall(Helper, {DataPtr, LenVal, KeyVal});
    }

    Builder.CreateRetVoid();
    MainFunc->setLinkage(GlobalVariable::InternalLinkage);
    // 将函数注册到 
    appendToGlobalCtors(M, MainFunc, 0);
}

Function* StringObfuscationPass::createDecodeHelper(Module &M) {
    LLVMContext &Ctx = M.getContext();
    Type *I32Ty = Type::getInt32Ty(Ctx);
    Type *I8Ty = Type::getInt8Ty(Ctx);
    Type *PtrTy = PointerType::get(Ctx, 0); // 统用指针类型

    // 定义签名: void decode_logic(i8* data, i32 len, i8 key)
    FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), {PtrTy, I32Ty, I8Ty}, false);
    Function *Helper = Function::Create(FTy, GlobalValue::InternalLinkage, "decode_logic", &M);

    // 获取参数
    Value *DataPtr = Helper->getArg(0);
    Value *Len = Helper->getArg(1);
    Value *Key = Helper->getArg(2);

    // --- 构建循环结构 (和之前类似，但现在是针对参数运行) ---
    BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Helper);
    BasicBlock *Cond = BasicBlock::Create(Ctx, "cond", Helper);
    BasicBlock *Body = BasicBlock::Create(Ctx, "body", Helper);
    BasicBlock *End = BasicBlock::Create(Ctx, "end", Helper);

    IRBuilder<> B(Entry);
    B.CreateBr(Cond);
    B.SetInsertPoint(Cond);

    PHINode *IV = B.CreatePHI(I32Ty, 2, "i");
    IV->addIncoming(ConstantInt::get(I32Ty, 0), Entry);
    
    // i < len
    Value *Cmp = B.CreateICmpULT(IV, Len);
    B.CreateCondBr(Cmp, Body, End);

    B.SetInsertPoint(Body);
    // 指令操作: data[i] = data[i] ^ Key
    Value *CurPtr = B.CreateInBoundsGEP(I8Ty, DataPtr, IV);
    Value *Val = B.CreateLoad(I8Ty, CurPtr);
    Value *Xored = B.CreateXor(Val, Key);
    B.CreateStore(Xored, CurPtr);

    // i++
    Value *NextIV = B.CreateAdd(IV, ConstantInt::get(I32Ty, 1));
    IV->addIncoming(NextIV, Body);
    B.CreateBr(Cond);

    B.SetInsertPoint(End);
    B.CreateRetVoid();

    Helper->setLinkage(GlobalVariable::InternalLinkage);
    return Helper;
}