#include "cocoons/StringObfuscationPass.h"
#include "cocoons/Config.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>
#include <set>

using namespace llvm;

namespace cocoons {

bool StringObfuscationPass::isEnabled() {
    const Config &C = Config::get();
    // pass-level on/off (used by plugin registration)
    return C.Str != EnableMode::DefaultOff || C.SkipFileGlobs.size() || C.SkipFuncNames.size();
}

PreservedAnalyses StringObfuscationPass::run(Module &M, ModuleAnalysisManager &AM) {
    Config &Cfg = Config::get();

    GlobalVariable *AnnoGV = M.getGlobalVariable("llvm.global.annotations");
    if (!AnnoGV) { return PreservedAnalyses::all(); }

    ConstantArray *AnnArr = dyn_cast<ConstantArray>(AnnoGV->getInitializer());
    if (!AnnArr) { return PreservedAnalyses::all(); }

    std::vector<GlobalVariable *> Targets;
    std::set<GlobalVariable *> Visited;
    for (unsigned int i = 0; i < AnnArr->getNumOperands(); ++i) {
        ConstantStruct *CS = dyn_cast<ConstantStruct>(AnnArr->getOperand(i));
        if (!CS) { continue; }

        GlobalVariable *TargetGV = dyn_cast<GlobalVariable>(CS->getOperand(0)->stripPointerCasts());
        GlobalVariable  *AnnoStrGV = dyn_cast<GlobalVariable>(CS->getOperand(1)->stripPointerCasts());
        if (TargetGV && AnnoStrGV && AnnoStrGV->hasInitializer()) {
            ConstantDataSequential *AnnoData = dyn_cast<ConstantDataSequential>(AnnoStrGV->getInitializer());
            if (!AnnoData) { continue; }

            StringRef Annotation = AnnoData->getAsString();
            if (!(Annotation.starts_with("obfuscate") || Annotation.starts_with("cocoons:str"))) {
                continue;
            }
            // Apply cocoons:no + skip_files/functions to the string global.
            if (!Cfg.shouldRunStr(TargetGV, {})) {
                continue;
            }
            processVariable(TargetGV, Targets, Visited, M);
        }
    }
    if (Targets.empty()) {
        return PreservedAnalyses::all();
    }

    std::uniform_int_distribution<int> Dist(1, 255);
    errs() << ">>> [Cocoons] 待加密字符串: " << Targets.size() << "\n";
    for (GlobalVariable *GV : Targets) {
        ConstantDataSequential *CDS = cast<ConstantDataSequential>(GV->getInitializer());
        uint32_t Len = CDS->getNumElements() * CDS->getElementByteSize();
        uint8_t BaseKey = (uint8_t)Dist(Cfg.Rng);
        if (encryptRealData(M, GV, BaseKey)) {
            injectLocalDecryption(M, GV, Len, BaseKey);
        }
    }

    return PreservedAnalyses::none();
}

void StringObfuscationPass::processVariable(GlobalVariable *GV,
                                            std::vector<GlobalVariable *> &Targets,
                                            std::set<GlobalVariable *> &Visited,
                                            Module &M) {
    if (!GV || !GV->hasInitializer()) return;

    if (Visited.count(GV)) {
        return;
    }
    Visited.insert(GV);

    Constant *Init = GV->getInitializer();
    Value *Stripped = Init->stripPointerCasts();

    if (ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(Stripped)) {
        errs() << ">>> [Cocoons] [Level 1] 字节数组: " << GV->getName() << "\n";
        if (!CDS->getElementType()->isIntegerTy(8)) {
            errs() << ">>> [Cocoons] 排除：不是 i8 类型。\n";
            return;
        }
        Targets.push_back(GV);
        return;
    }

    if (ConstantStruct *CS = dyn_cast<ConstantStruct>(Stripped)) {
        errs() << ">>> [Cocoons] [跳转] 识别到 OC 结构体，提取第 3 个成员...\n";
        if (CS->getNumOperands() >= 3) {
            Value *V = CS->getOperand(2)->stripPointerCasts();
            if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
                V = CE->getOperand(0)->stripPointerCasts();
            }
            if (GlobalVariable *NextGV = dyn_cast<GlobalVariable>(V)) {
                processVariable(NextGV, Targets, Visited, M);
            }
        }
        return;
    }

    if (GlobalVariable *NextGV = dyn_cast<GlobalVariable>(Stripped)) {
        errs() << ">>> [Cocoons] [跳转] 识别到变量引用: " << NextGV->getName() << "，继续挖掘...\n";
        processVariable(NextGV, Targets, Visited, M);
        return;
    }
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Stripped)) {
        Value *V = CE->getOperand(0)->stripPointerCasts();
        if (GlobalVariable *NextGV = dyn_cast<GlobalVariable>(V)) {
            processVariable(NextGV, Targets, Visited, M);
        }
        return;
    }
}

void StringObfuscationPass::injectLocalDecryption(Module &M, GlobalVariable *EncryptedGV,
                                                  uint32_t Len, uint8_t BaseKey) {
    if (Len <= 1) return;

    std::vector<Instruction *> UsesToProcess;

    std::vector<User *> WorkList;
    for (User *U : EncryptedGV->users()) {
        WorkList.push_back(U);
    }

    while (!WorkList.empty()) {
        User *U = WorkList.back();
        WorkList.pop_back();

        if (Instruction *Inst = dyn_cast<Instruction>(U)) {
            if (isa<PHINode>(Inst)) {
                UsesToProcess.push_back(&Inst->getParent()->front());
            } else {
                UsesToProcess.push_back(Inst);
            }
        } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(U)) {
            for (User *CEU : CE->users()) {
                WorkList.push_back(CEU);
            }
        }
    }

    for (Instruction *InsertBefore : UsesToProcess) {
        createDecryptionLoop(InsertBefore, EncryptedGV, Len, BaseKey, M);
    }
}

void StringObfuscationPass::createDecryptionLoop(Instruction *InsertBefore, Value *StrPtr,
                                                 uint32_t Len, uint8_t BaseKey, Module &M) {
    LLVMContext &Ctx = M.getContext();
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *Int8Ty = Type::getInt8Ty(Ctx);

    BasicBlock *OriginalBB = InsertBefore->getParent();
    IRBuilder<> EntryB(InsertBefore);

    AllocaInst *StackBuffer = EntryB.CreateAlloca(Int8Ty, ConstantInt::get(Int32Ty, Len), "dec.buffer");

    const DataLayout &DL = M.getDataLayout();
    EntryB.CreateMemCpy(StackBuffer, MaybeAlign(1), StrPtr, MaybeAlign(1),
                        ConstantInt::get(Int32Ty, Len));

    Function *F = OriginalBB->getParent();
    std::vector<User *> LocalUses;
    for (User *U : StrPtr->users()) {
        if (Instruction *I = dyn_cast<Instruction>(U)) {
            if (I->getParent()->getParent() == F && I != StackBuffer) {
                LocalUses.push_back(U);
            }
        }
    }

    InsertBefore->replaceUsesOfWith(StrPtr, StackBuffer);

    BasicBlock *NewBB = OriginalBB->splitBasicBlock(InsertBefore, "dec.end");

    BasicBlock *LoopCond = BasicBlock::Create(Ctx, "dec.cond", OriginalBB->getParent(), NewBB);
    BasicBlock *LoopBody = BasicBlock::Create(Ctx, "dec.body", OriginalBB->getParent(), NewBB);

    OriginalBB->getTerminator()->eraseFromParent();
    IRBuilder<> B(OriginalBB);
    B.CreateBr(LoopCond);

    B.SetInsertPoint(LoopCond);
    PHINode *Idx = B.CreatePHI(Int32Ty, 2, "idx");
    Idx->addIncoming(ConstantInt::get(Int32Ty, 0), OriginalBB);

    uint32_t ObfLenMult = Len * 3;
    Value *ObfLen = B.CreateUDiv(ConstantInt::get(Int32Ty, ObfLenMult),
                                 ConstantInt::get(Int32Ty, 3));

    Value *Cmp = B.CreateICmpULT(Idx, ObfLen);
    B.CreateCondBr(Cmp, LoopBody, NewBB);

    B.SetInsertPoint(LoopBody);
    Value *BytePtr = B.CreateGEP(Int8Ty, StackBuffer, Idx);
    Value *EncByte = B.CreateLoad(Int8Ty, BytePtr);

    uint8_t ObfBaseKey = BaseKey ^ 0xAA;
    Value *RealBaseKey = B.CreateXor(ConstantInt::get(Int8Ty, ObfBaseKey),
                                     ConstantInt::get(Int8Ty, 0xAA));

    Value *IdxMul = B.CreateMul(Idx, ConstantInt::get(Int32Ty, 0x33));
    Value *IdxTrunc = B.CreateTrunc(IdxMul, Int8Ty);
    Value *DynamicKey = B.CreateXor(RealBaseKey, IdxTrunc);

    Value *DecByte = B.CreateXor(EncByte, DynamicKey);
    B.CreateStore(DecByte, BytePtr);

    Value *NextIdx = B.CreateAdd(Idx, ConstantInt::get(Int32Ty, 1));
    Idx->addIncoming(NextIdx, LoopBody);
    B.CreateBr(LoopCond);
}

bool StringObfuscationPass::encryptRealData(Module &M, GlobalVariable *TargetGV, uint8_t BaseKey) {
    if (!TargetGV || !TargetGV->hasInitializer()) {
        return false;
    }
    Constant *ActualInit = TargetGV->getInitializer();
    if (ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(ActualInit)) {
        if (TargetGV->getSection() == "__DATA,__obf_strings") return false;

        std::vector<uint8_t> Data;
        for (unsigned i = 0; i < CDS->getNumElements(); ++i) {
            uint8_t Val = CDS->getElementAsInteger(i);
            uint8_t DynamicKey = BaseKey ^ ((i * 0x33) & 0xFF);
            Val ^= DynamicKey;

            Data.push_back(Val);
        }

        TargetGV->setInitializer(ConstantDataArray::get(M.getContext(), Data));

        TargetGV->setConstant(false);
        TargetGV->setSection("__DATA,__obf_strings");
        TargetGV->setAlignment(MaybeAlign(1));
        errs() << ">>> [Cocoons] [DONE] String Obfuscated: " << TargetGV->getName() << "\n";
        return true;
    }
    return false;
}

} // namespace cocoons
