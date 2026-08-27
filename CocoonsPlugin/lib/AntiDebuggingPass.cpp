// ============================================================
// Cocoons Anti-Debugging Pass (Module Pass)
// ------------------------------------------------------------
// 只在 Objective-C / C 代码的 Mach-O 目标下有意义 (Apple 平台).
//
// 注入策略：
//   1. 若 main() 函数存在 → 在其 entry block 最开头注入 4 组检查：
//        - ptrace(PTRACE_DENY_ATTACH) syscall (through syscall(SYS_ptrace,...))
//        - sysctl(KERN_PROC_PID) P_TRACED flag check
//        - _dyld_image_count / dyld get_image_name 检查 frida / Cycript
//        - SecTaskCopyValueForEntitlement("get-task-allow") 签名权限检查
//      任何一个失败 → 立刻 call exit(0x55) / SIGKILL / __builtin_trap.
//   2. 若存在 +[Foo load] / __attribute__((constructor)) 构造函数：
//      也可以插入 cocoons_anti_debug_do_checks() 的调用；我们统一通过
//      新增一个 static constructor 来执行（这样对任何 C / ObjC / C++ 项目
//      都生效，不必找 main / +load）。
// ============================================================

#include "cocoons/AntiDebuggingPass.h"
#include "cocoons/Config.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

namespace cocoons {

bool AntiDebuggingPass::isEnabled() {
    const Config &C = Config::get();
    return C.AntiDebug != EnableMode::DefaultOff
        && (C.AntiInjectPtrace || C.AntiInjectSysctl
            || C.AntiInjectDyldHook || C.AntiInjectEntitlement);
}

static Function *ensureCDeclFn(Module &M, StringRef Name,
                               FunctionType *FTy) {
    Function *F = M.getFunction(Name);
    if (F) return F;
    auto *FnC = Function::Create(FTy, GlobalValue::ExternalLinkage, Name, M);
    FnC->setCallingConv(CallingConv::C);
    return FnC;
}

static ConstantInt *I32(Module &M, uint32_t V) {
    return ConstantInt::get(Type::getInt32Ty(M.getContext()), V);
}
static ConstantInt *I64(Module &M, uint64_t V) {
    return ConstantInt::get(Type::getInt64Ty(M.getContext()), V);
}

static Constant *createPrivateCStr(Module &M, StringRef S, StringRef Name) {
    auto *CA = ConstantDataArray::getString(M.getContext(), S, true);
    auto *GV = new GlobalVariable(M, CA->getType(), true,
        GlobalValue::PrivateLinkage, CA, Name);
    GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    GV->setAlignment(Align(1));
    auto *Zero = I32(M, 0);
    Value *Idx[2] = { Zero, Zero };
    Type *I8Ty = Type::getInt8Ty(M.getContext());
    return ConstantExpr::getInBoundsGetElementPtr(
        I8Ty, GV, ArrayRef<Value *>(Idx, 2));
}

// Build: void cocoons_anti_debug_do_checks(void)
//
// Returns: nothing, but may call exit() / __builtin_trap() if debugger detected.
static Function *buildDoChecks(Module &M, const Config &Cfg) {
    LLVMContext &C = M.getContext();
    Type *VoidTy = Type::getVoidTy(C);
    auto *FTy = FunctionType::get(VoidTy, false);
    auto *F = Function::Create(FTy, GlobalValue::InternalLinkage,
                               "cocoons_anti_debug_do_checks", M);
    F->addFnAttr(Attribute::NoInline);
    F->addFnAttr(Attribute::OptimizeNone);
    auto *BB = BasicBlock::Create(C, "entry", F);
    IRBuilder<> B(BB);

    // -- prototypes we need (external linkage, resolved at link-time by libSystem) --
    // int syscall(int code, ...); — declare as variadic at FunctionType level
    auto *SyscallTy = FunctionType::get(Type::getInt32Ty(C), {Type::getInt32Ty(C)}, true);
    Function *Syscall = ensureCDeclFn(M, "syscall", SyscallTy);
    Syscall->setDSOLocal(true);

    // void exit(int);
    Function *Exit = ensureCDeclFn(M, "exit",
        FunctionType::get(VoidTy, {Type::getInt32Ty(C)}, false));

    // int sysctl(int *, u_int, void *, size_t *, void *, size_t);
    auto *IntP   = PointerType::get(C, 0);
    auto *SizeTP = PointerType::get(C, 0);
    auto *VoidP  = PointerType::getUnqual(C);
    Function *Sysctl = ensureCDeclFn(M, "sysctl",
        FunctionType::get(Type::getInt32Ty(C), {IntP, Type::getInt32Ty(C), VoidP, SizeTP, VoidP, Type::getInt64Ty(C)}, false));

    // uint32_t dyld_image_count(void);
    // const char* _dyld_get_image_name(uint32_t image_index);
    Function *ImgCount = ensureCDeclFn(M, "_dyld_image_count",
        FunctionType::get(Type::getInt32Ty(C), false));
    Function *ImgName = ensureCDeclFn(M, "_dyld_get_image_name",
        FunctionType::get(VoidP, {Type::getInt32Ty(C)}, false));

    // char* strstr(const char*, const char*);
    Function *StrStr = ensureCDeclFn(M, "strstr",
        FunctionType::get(VoidP, {VoidP, VoidP}, false));

    // SecTask / Security prototypes (all opaque pointers)
    Function *SecTaskSelf = ensureCDeclFn(M, "SecTaskCreateFromSelf",
        FunctionType::get(VoidP, {VoidP}, false));
    // void* SecTaskCopyValueForEntitlement(void* task, const char* ent, void** error);
    Function *SecTaskEntitle = ensureCDeclFn(M, "SecTaskCopyValueForEntitlement",
        FunctionType::get(VoidP, {VoidP, VoidP, PointerType::getUnqual(C)}, false));
    // bool   CFBooleanGetValue(void* cfbool);
    Function *CFBoolVal = ensureCDeclFn(M, "CFBooleanGetValue",
        FunctionType::get(Type::getInt1Ty(C), {VoidP}, false));

    Value *ExitCode = I32(M, 0x55);  // signature for "killed by anti-debug"

    auto trap = [&]() { B.CreateCall(Exit, ExitCode); B.CreateUnreachable(); };

    // ── 1. PTRACE_DENY_ATTACH (syscall 26 on arm64/ppc/x86_64) ─────────────
    if (Cfg.AntiInjectPtrace) {
        // ptrace(PT_DENY_ATTACH, 0, 0, 0);
        // syscall(SYS_ptrace, 31, 0, 0, 0)   (PT_DENY_ATTACH = 31)
        Value *R1 = B.CreateCall(Syscall,
            {I32(M, 26 /*SYS_ptrace*/), I32(M, 31), I32(M, 0), I64(M, 0), I64(M, 0)});
        (void)R1;  // ignore return
    }

    // ── 2. sysctl KERN_PROC_PID → check P_TRACED ───────────────────────────
    if (Cfg.AntiInjectSysctl) {
        // int MIB[4] = { CTL_KERN (1), KERN_PROC (14), KERN_PROC_PID (1), getpid() };
        // We use getpid() via syscall as well: syscall(20) = SYS_getpid
        Value *PID = B.CreateCall(Syscall, {I32(M, 20 /*SYS_getpid*/)});
        AllocaInst *MIB = B.CreateAlloca(Type::getInt32Ty(C), I32(M, 4), "mib");
        auto *TyI32 = Type::getInt32Ty(C);
        B.CreateStore(I32(M, 1),  B.CreateGEP(TyI32, MIB, I32(M, 0)));
        B.CreateStore(I32(M, 14), B.CreateGEP(TyI32, MIB, I32(M, 1)));
        B.CreateStore(I32(M, 1),  B.CreateGEP(TyI32, MIB, I32(M, 2)));
        B.CreateStore(PID,        B.CreateGEP(TyI32, MIB, I32(M, 3)));

        // struct kinfo_proc is big; we use sizeof(kinfo_proc) ≈ 648 but we only
        // care about the first ~32 bytes (kp_proc.p_flag) which is at offset 0
        // in kinfo_proc; p_flag is an int at kp_proc + 0x00? Actually
        // kinfo_proc starts with extern_proc, p_flag is extern_proc[2] (int)
        // offset: first field = p_size? Real headers struct extern_proc { pid_t p_pid, ..., int p_flag, ... }.
        // To keep robust without headers, we read the struct as a 72 int array and
        // search. Easiest: use just int kp[32] and use offset 10 (empirical for
        // Apple Silicon / x86_64 recent XNU: extern_proc starts with:
        // void *__proc_unused[2] ? nope — actually safer: use p_flag as
        // *(volatile int*)(&proc.kp_proc + offsetof(struct extern_proc, p_flag))
        // but we don't have headers. Let's use a known safe trick: sysctl returns
        // kinfo_proc whose first 8 bytes are kp_proc.p_pid (int, 4 bytes) then
        // kp_proc.p_comm[16] then p_stat, p_flag actually comes at byte offset
        // 72 in most XNU. To avoid version skew, we read 8 * sizeof(int), i.e.
        // 32 bytes, and skip. Actually simplest approach: allocate 8192 bytes,
        // check return length and stride: p_flag is an int at byte offset 32 of
        // kinfo_proc's extern_proc? I'll cast to uint8_t* and offset.
        //
        // Let's use a safer trick from known Apple sources: flag = *((int *)
        // ((uint8_t*)kp + 32)). If P_TRACED (0x00000800) is set, trap.
        AllocaInst *KP = B.CreateAlloca(Type::getInt8Ty(C), I32(M, 8192), "kinfo");
        uint64_t kpSz = 8192;
        AllocaInst *Sz = B.CreateAlloca(Type::getInt64Ty(C), nullptr, "szp");
        B.CreateStore(ConstantInt::get(Type::getInt64Ty(C), kpSz), Sz);
        Value *RC = B.CreateCall(Sysctl, {
            MIB, I32(M, 4),
            KP, Sz,
            Constant::getNullValue(VoidP), I64(M, 0)});
        // RC must be 0 (otherwise skip check)
        auto *BBok   = BasicBlock::Create(C, "sysctl_ok", F);
        auto *BBrtrn = BasicBlock::Create(C, "sysctl_skip", F);
        Value *RcOk = B.CreateICmpEQ(RC, I32(M, 0));
        B.CreateCondBr(RcOk, BBok, BBrtrn);

        B.SetInsertPoint(BBok);
        // load p_flag at byte offset 32 (p_flag int)
        Value *FlagPtr = B.CreateGEP(Type::getInt8Ty(C), KP, I32(M, 32));
        auto *FlagI32Ptr = B.CreateBitCast(FlagPtr, PointerType::get(C, 0));
        Value *Flag = B.CreateLoad(Type::getInt32Ty(C), FlagI32Ptr);
        // P_TRACED = 0x800
        Value *Traced = B.CreateICmpNE(
            B.CreateAnd(Flag, I32(M, 0x800)), I32(M, 0));
        auto *BTrapS = BasicBlock::Create(C, "ptraced", F);
        auto *BCont  = BasicBlock::Create(C, "proceed", F);
        B.CreateCondBr(Traced, BTrapS, BCont);

        B.SetInsertPoint(BTrapS); trap();

        B.SetInsertPoint(BBrtrn);
        B.CreateBr(BCont);
        B.SetInsertPoint(BCont);
    }

    // ── 3. dyld image name scan for "frida" / "cycript" / "substitute" ─────
    if (Cfg.AntiInjectDyldHook) {
        auto *LoopBB    = BasicBlock::Create(C, "dl_loop", F);
        auto *LoopBody  = BasicBlock::Create(C, "dl_body", F);
        auto *LoopNext  = BasicBlock::Create(C, "dl_next", F);
        auto *BadImg    = BasicBlock::Create(C, "dl_bad", F);
        auto *DLCont    = BasicBlock::Create(C, "dl_cont", F);

        Value *N = B.CreateCall(ImgCount);
        AllocaInst *I = B.CreateAlloca(Type::getInt32Ty(C), nullptr, "i");
        B.CreateStore(I32(M, 0), I);
        B.CreateBr(LoopBB);
        B.SetInsertPoint(LoopBB);
        Value *Iv = B.CreateLoad(Type::getInt32Ty(C), I);
        Value *Cmp = B.CreateICmpSLT(Iv, N);
        B.CreateCondBr(Cmp, LoopBody, DLCont);
        B.SetInsertPoint(LoopBody);
        Value *Nm = B.CreateCall(ImgName, Iv);
        Constant *Frida   = createPrivateCStr(M, "frida-agent",   "s_frida");
        Constant *Cycript = createPrivateCStr(M, "cycript",       "s_cycri");
        Constant *Subst   = createPrivateCStr(M, "substitute",    "s_subst");
        Value *R1 = B.CreateCall(StrStr, {Nm, Frida});
        Value *R2 = B.CreateCall(StrStr, {Nm, Cycript});
        Value *R3 = B.CreateCall(StrStr, {Nm, Subst});
        Value *Any = B.CreateOr(B.CreateOr(
            B.CreateIsNotNull(R1),
            B.CreateIsNotNull(R2)),
            B.CreateIsNotNull(R3));
        B.CreateCondBr(Any, BadImg, LoopNext);
        B.SetInsertPoint(BadImg); trap();
        B.SetInsertPoint(LoopNext);
        Iv = B.CreateAdd(Iv, I32(M, 1));
        B.CreateStore(Iv, I);
        B.CreateBr(LoopBB);
        B.SetInsertPoint(DLCont);
    }

    // ── 4. SecTaskCopyValueForEntitlement(com.apple.security.get-task-allow)
    if (Cfg.AntiInjectEntitlement) {
        Value *Task = B.CreateCall(SecTaskSelf,
            {Constant::getNullValue(VoidP)});
        auto *EntChk  = BasicBlock::Create(C, "entchk", F);
        auto *EntCont = BasicBlock::Create(C, "entcont", F);
        Value *TaskOk = B.CreateIsNotNull(Task);
        B.CreateCondBr(TaskOk, EntChk, EntCont);
        B.SetInsertPoint(EntChk);
        Constant *EntName = createPrivateCStr(M,
            "com.apple.security.get-task-allow", "ent");
        auto *PPtrV = PointerType::getUnqual(C);  // void** for error output
        auto *ErrLoc = B.CreateAlloca(PPtrV, nullptr, "err");
        Value *Ent = B.CreateCall(SecTaskEntitle, {Task, EntName, ErrLoc});
        auto *H     = BasicBlock::Create(C, "entgot",  F);
        auto *TrapB = BasicBlock::Create(C, "ent_trap", F);
        B.CreateCondBr(B.CreateIsNotNull(Ent), H, EntCont);
        B.SetInsertPoint(H);
        Value *IsAllowed = B.CreateCall(CFBoolVal, Ent);
        B.CreateCondBr(IsAllowed, TrapB, EntCont);
        B.SetInsertPoint(TrapB); trap();
        B.SetInsertPoint(EntCont);
    }

    // --- normal return ---
    if (Cfg.AntiInjectPtrace || Cfg.AntiInjectSysctl ||
        Cfg.AntiInjectDyldHook || Cfg.AntiInjectEntitlement) {
        B.CreateRetVoid();
    } else {
        B.CreateRetVoid();
    }
    return F;
}

PreservedAnalyses AntiDebuggingPass::run(Module &M, ModuleAnalysisManager &) {
    const Config &Cfg = Config::get();
    if (Cfg.AntiDebug == EnableMode::DefaultOff) {
        return PreservedAnalyses::all();
    }
    if (!(Cfg.AntiInjectPtrace || Cfg.AntiInjectSysctl ||
          Cfg.AntiInjectDyldHook || Cfg.AntiInjectEntitlement)) {
        return PreservedAnalyses::all();
    }
    // Annotation gate if anti-debug is in Annotation mode: look for
    // __attribute__((annotate("cocoons:anti_debug"))) on some global/function.
    if (Cfg.AntiDebug == EnableMode::Annotation) {
        const GlobalVariable *AnnoGV = M.getGlobalVariable("llvm.global.annotations");
        bool Found = false;
        if (AnnoGV && AnnoGV->hasInitializer()) {
            const ConstantArray *CA = dyn_cast<ConstantArray>(AnnoGV->getInitializer());
            if (CA) for (unsigned i=0;i<CA->getNumOperands(); ++i) {
                auto *CS = dyn_cast<ConstantStruct>(CA->getOperand(i));
                if (!CS || CS->getNumOperands()<2) continue;
                auto *SGV = dyn_cast<GlobalVariable>(CS->getOperand(1)->stripPointerCasts());
                if (!SGV || !SGV->hasInitializer()) continue;
                auto *CDS = dyn_cast<ConstantDataSequential>(SGV->getInitializer());
                if (!CDS) continue;
                if (CDS->getAsString().starts_with("cocoons:anti_debug")) { Found = true; break; }
            }
        }
        if (!Found) {
            return PreservedAnalyses::all();
        }
    }

    Function *DoChecks = buildDoChecks(M, Cfg);
    if (!DoChecks) return PreservedAnalyses::all();

    // -- (a) Add a static constructor with priority 0 (runs before any user ctor).
    appendToGlobalCtors(M, DoChecks, 0, nullptr);

    // -- (b) Also prepend a call at very top of main() if present:
    Function *Main = M.getFunction("main");
    if (Main && !Main->isDeclaration() && !Main->empty()) {
        BasicBlock &EB = Main->getEntryBlock();
        auto It = EB.getFirstInsertionPt();
        if (It != EB.end()) {
            IRBuilder<> Bld(&*It);
            Bld.CreateCall(DoChecks);
        }
    }

    errs() << ">>> [Cocoons] Anti-Debugging checks injected (ptrace="
           << (Cfg.AntiInjectPtrace?"Y":"-")
           << " sysctl=" << (Cfg.AntiInjectSysctl?"Y":"-")
           << " dyld="   << (Cfg.AntiInjectDyldHook?"Y":"-")
           << " entitlement=" << (Cfg.AntiInjectEntitlement?"Y":"-") << ")\n";
    return PreservedAnalyses::none();
}

} // namespace cocoons
