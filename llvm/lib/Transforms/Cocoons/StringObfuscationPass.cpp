#include "llvm/Transforms/Cocoons/StringObfuscationPass.h"
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
#include "llvm/Pass.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <cstdint>
#include <random>
#include <vector>

namespace cocoons {

// 定义开关
// 这里的字符串 "enable-str-obf" 就是你将在命令行里敲的参数名
static cl::opt<bool> EnableStrObf(
    "cocoons-enable-str", 
    cl::init(false), 
    cl::desc("Enable String Obfuscation for iOS"));

bool StringObfuscationPass::isEnabled() {
    return EnableStrObf;
}


PreservedAnalyses StringObfuscationPass::run(Module &M, ModuleAnalysisManager &AM) {
    // 1. 识别标记了 annotate("obfuscate") 的全局变量
    GlobalVariable *AnnoGV = M.getGlobalVariable("llvm.global.annotations");
    if (!AnnoGV) { return PreservedAnalyses::all(); }

    ConstantArray *AnnArr = dyn_cast<ConstantArray>(AnnoGV->getInitializer());
    if (!AnnArr) { return PreservedAnalyses::all(); }

    std::vector<GlobalVariable *> Targets;
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
                processVariable(TargetGV, Targets, M);
            }
        }
    }
    if (Targets.empty()) {
        errs() << ">>> 没有可混淆的字符串。\n";
        return PreservedAnalyses::all();
    }

    // 2. 准备随机数生成器
    std::random_device RD;
    std::default_random_engine Engine(RD());
    std::uniform_int_distribution<int> Dist(1, 255);
    errs() << "待加密：" << Targets.size() << "\n"; 
    for (GlobalVariable *GV : Targets) {
        ConstantDataSequential *CDS = cast<ConstantDataSequential>(GV->getInitializer());
        uint32_t Len = CDS->getNumElements() * CDS->getElementByteSize();
        uint8_t Key = (uint8_t)Dist(Engine);
        errs() << "待加密：" << *GV << "====Len:" << Len << "=====Key:"<< Key << "====" << "\n"; 
        if (encryptRealData(M, GV, Key)) {
            // 5. 将解密所需的“线索”存入段
            emitMetadata(M, GV, Len, Key);
        }
    }

    injectDecrypter(M);
    
    // 既然我们修改了全局变量，告诉 LLVM 分析结果已失效
    return PreservedAnalyses::none();
}
void StringObfuscationPass::processVariable(GlobalVariable *GV, std::vector<GlobalVariable *> &Targets, Module &M) {
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
        Targets.push_back(GV);
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
                processVariable(nextGV, Targets, M);
            }
        }
        return;
    }
    
    // --- 探测点 C: 如果是一个指针引用 (C 指针或变量引用) ---
    if (GlobalVariable *NextGV = dyn_cast<GlobalVariable>(Stripped)) {
        errs() << ">>> [跳转] 识别到变量引用: " << NextGV->getName() << "，继续挖掘...\n";
        processVariable(NextGV, Targets, M); // 【递归】
        return;
    }
    // --- 探测点 D: 如果是复杂的常量表达式 (GEP 等) ---
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Stripped)) {
        Value *V = CE->getOperand(0)->stripPointerCasts();
        if (GlobalVariable *NextGV = dyn_cast<GlobalVariable>(V)) {
            processVariable(NextGV, Targets, M); // 【递归】
        }
        return;
    }
}

void StringObfuscationPass::emitMetadata(Module &M, GlobalVariable *EncryptedGV, uint32_t Len, uint8_t Key) {
    LLVMContext &Ctx = M.getContext();
    // 1. 定义结构体类型: { i8* addr, i32 len, i8 key }
    StructType *MetaTy = StructType::getTypeByName(Ctx, "CocoonsMetaTy");
    if (!MetaTy) {
        MetaTy = StructType::create(Ctx, {
            PointerType::get(Ctx, 0),
            IntegerType::get(Ctx, 32),
            IntegerType::get(Ctx, 8),
        });
        MetaTy->setName("CocoonsMetaTy");
    }

    // 赋值
    Constant *Addr = ConstantExpr::getBitCast(EncryptedGV, PointerType::get(Ctx, 0));
    Constant *CLen = ConstantInt::get(IntegerType::get(Ctx, 32), Len);
    Constant *CKey = ConstantInt::get(IntegerType::get(Ctx, 8), Key);
    Constant *StructVal = ConstantStruct::get(MetaTy, {Addr, CLen, CKey});

    std::string MetaName = "_cocoons_meta_" + EncryptedGV->getName().str();
    GlobalVariable *MetaGV = new GlobalVariable(M, MetaTy, false, GlobalValue::InternalLinkage, StructVal, MetaName);
    MetaGV->setSection("__DATA,__cocoons_obs");

    MetaGV->setAlignment(MaybeAlign(8));

    // 将该变量加入 llvm.used，防止被 Linker 或 GlobalOpt 优化掉
    markUsed(M, MetaGV);
}

void StringObfuscationPass::markUsed(Module &M, GlobalVariable *GV) {
    appendToUsed(M, {cast<GlobalValue>(GV)});
}

bool StringObfuscationPass::encryptRealData(Module &M, GlobalVariable *TargetGV, uint8_t Key) {
    if (!TargetGV || !TargetGV->hasInitializer()) {
        return false;
    }
    errs() << "加密字符串：" << *TargetGV << "====key: " << Key << "\n";
    Constant *ActualInit = TargetGV->getInitializer();
    if (ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(ActualInit)) {
        // 防止重复加密
        errs() << ">>>>>> 即将加密: " << TargetGV->getName() << " <<<<<<\n";

        if (TargetGV->getSection() == "__DATA,__obf_strings") return false;

        errs() << ">>>>>> 执行加密逻辑: " << TargetGV->getName() << " <<<<<<\n";
        
        std::vector<uint8_t> Data;
        for (unsigned i = 0; i < CDS->getNumElements(); ++i) {
            uint8_t Val = CDS->getElementAsInteger(i);
            // 最后一个 \0 不加密，否则 printf 无法结尾
            if (i < CDS->getNumElements() - 1) {
                Val ^= Key;
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
        errs() << ">>>>>>> [DONE] Obfuscated: " << TargetGV->getName() << "\n";

        return true;
    }
    return false;
}

// 注入 LinkOnceODR 解密器
void StringObfuscationPass::injectDecrypter(Module &M) {
    LLVMContext &Ctx = M.getContext();
    const DataLayout &DL = M.getDataLayout();
    StructType *MetaTy = StructType::getTypeByName(Ctx, "CocoonsMetaTy");
    if (!MetaTy) {
        errs() << "未定义类型" << "\n";
        return;
    }
    Type *IntptrTy = DL.getIntPtrType(Ctx);
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *Int8Ty = Type::getInt8Ty(Ctx);
    PointerType *PtrTy = PointerType::get(Ctx, 0);
    uint64_t StructSize = DL.getTypeAllocSize(MetaTy);
    errs() << "StructSize: " << StructSize << "\n";
    // 1. 全局单例锁
    GlobalVariable *Guard = new GlobalVariable(M, Int32Ty, false, 
        GlobalValue::LinkOnceODRLinkage, ConstantInt::get(Int32Ty, 0), "__cocoons_dec_guard");
    Guard->setVisibility(GlobalValue::HiddenVisibility);
    Guard->setDSOLocal(true);

    // 2. Mach-O 边界符号引用
    GlobalVariable *StartSect = new GlobalVariable(M, Int8Ty, false, GlobalValue::ExternalLinkage, nullptr, "\1section$start$__DATA$__cocoons_obs");
    GlobalVariable *EndSect = new GlobalVariable(M, Int8Ty, false, GlobalValue::ExternalLinkage, nullptr, "\1section$end$__DATA$__cocoons_obs");

    // 3. 创建解密函数
    Function *DecFunc = Function::Create(FunctionType::get(Type::getVoidTy(Ctx), false), 
        GlobalValue::LinkOnceODRLinkage, "__cocoons_runtime_decrypter", &M);
    DecFunc->setVisibility(GlobalValue::HiddenVisibility);
    DecFunc->setDSOLocal(true);

    // 4. 构建解密循环 IR
    BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", DecFunc);
    BasicBlock *DoDecBB = BasicBlock::Create(Ctx, "do_dec", DecFunc);
    BasicBlock *LoopCond = BasicBlock::Create(Ctx, "loop.cond", DecFunc);
    BasicBlock *LoopBody = BasicBlock::Create(Ctx, "loop.body", DecFunc);
    BasicBlock *InnerXor = BasicBlock::Create(Ctx, "inner.xor", DecFunc);
    BasicBlock *LoopNext = BasicBlock::Create(Ctx, "loop.next", DecFunc);
    BasicBlock *ExitBB = BasicBlock::Create(Ctx, "exit", DecFunc);

    IRBuilder<> B(EntryBB);
    Value *IsInit = B.CreateLoad(Int32Ty, Guard);
    B.CreateCondBr(B.CreateICmpEQ(IsInit, ConstantInt::get(Int32Ty, 0)), DoDecBB, ExitBB);

    B.SetInsertPoint(DoDecBB);
    // 关键：将 GlobalVariable 强制转换为整数，这就是链接器填入的地址
    Value *StartVal = B.CreatePtrToInt(StartSect, Type::getInt64Ty(Ctx));
    Value *EndVal = B.CreatePtrToInt(EndSect, Type::getInt64Ty(Ctx));

    B.CreateStore(ConstantInt::get(Int32Ty, 1), Guard);
    B.CreateBr(LoopCond);

    B.SetInsertPoint(LoopCond);
    PHINode *CurrAddr = B.CreatePHI(IntptrTy, 2, "curr.addr");
    CurrAddr->addIncoming(B.CreatePtrToInt(StartSect, IntptrTy), DoDecBB);
    B.CreateCondBr(B.CreateICmpULT(CurrAddr, B.CreatePtrToInt(EndSect, IntptrTy)), LoopBody, ExitBB);

    B.SetInsertPoint(LoopBody);
    Value *EntryPtr = B.CreateIntToPtr(CurrAddr, PtrTy);
    Value *StrAddr = B.CreateLoad(PtrTy, B.CreateStructGEP(MetaTy, EntryPtr, 0));
    Value *StrLen = B.CreateLoad(Int32Ty, B.CreateStructGEP(MetaTy, EntryPtr, 1));
    Value *DecLen = B.CreateSub(StrLen, ConstantInt::get(Int32Ty, 1));
    Value *XorKey = B.CreateLoad(Int8Ty, B.CreateStructGEP(MetaTy, EntryPtr, 2));
    Value *HasPayload = B.CreateICmpSGT(DecLen, ConstantInt::get(Int32Ty, 0));

    // B.CreateCondBr(B.CreateICmpNE(StrLen, ConstantInt::get(Int32Ty, 0)), InnerXor, LoopNext);
    B.CreateCondBr(HasPayload, InnerXor, LoopNext);

    B.SetInsertPoint(InnerXor);
    PHINode *Idx = B.CreatePHI(Int32Ty, 2, "idx");
    Idx->addIncoming(ConstantInt::get(Int32Ty, 0), LoopBody);
    Value *BytePtr = B.CreateGEP(Int8Ty, StrAddr, Idx);
    B.CreateStore(B.CreateXor(B.CreateLoad(Int8Ty, BytePtr), XorKey), BytePtr);
    Value *NextIdx = B.CreateAdd(Idx, ConstantInt::get(Int32Ty, 1));
    Idx->addIncoming(NextIdx, InnerXor);
    B.CreateCondBr(B.CreateICmpULT(NextIdx, DecLen), InnerXor, LoopNext);

    B.SetInsertPoint(LoopNext);
    CurrAddr->addIncoming(B.CreateAdd(CurrAddr, ConstantInt::get(IntptrTy, StructSize)), LoopNext);
    B.CreateBr(LoopCond);

    B.SetInsertPoint(ExitBB);
    B.CreateRetVoid();

    appendToGlobalCtors(M, DecFunc, 0);
}

} // namespace cocoons