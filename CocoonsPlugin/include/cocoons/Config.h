// ============================================================
// Cocoons Global Config
// ------------------------------------------------------------
// 所有 Pass（Str/Sub/Fla/AntiDebug）共享的开关 & 规则。
//
// 配置来源优先级（从高到低，后者覆盖前者）：
//   1) 环境变量：COCOONS_ENABLE_*, COCOONS_SUB_LOOP,
//                COCOONS_FLA_PROBABILITY / _MIN_BBS / _MAX_BBS,
//                COCOONS_CONFIG
//   2) JSON 配置文件（路径来自 COCOONS_CONFIG 或默认查找
//      $(SRCROOT)/.cocoons.json 或 $(pwd)/.cocoons.json 或 Module
//      source-file-dir 往上搜到的第一个 .cocoons.json）
//   3) 函数/全局变量注解：
//        __attribute__((annotate("cocoons:str")))
//        __attribute__((annotate("cocoons:sub")))
//        __attribute__((annotate("cocoons:fla")))
//        __attribute__((annotate("cocoons:no")))
//      白名单模式：当 Str/Sub/Fla 中某项配置为 "annotations" 时，
//      仅对带对应注解的对象生效；黑名单 cocoons:no 永远跳过。
// ============================================================

#ifndef COCOONS_CONFIG_H
#define COCOONS_CONFIG_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"

#include <cstdint>
#include <random>
#include <string>

namespace cocoons {

enum class EnableMode {
    DefaultOff = 0,  // 明确关闭，除非注解 cocoons:* 白名单强制开
    DefaultOn  = 1,  // 默认开，遇到 cocoons:no 才关
    Annotation = 2,  // 只对有 cocoons:<passname> 白名单注解的生效
};

struct Config {
    // --- global on/off / policy ---
    EnableMode Str = EnableMode::DefaultOn;
    EnableMode Sub = EnableMode::DefaultOn;
    EnableMode Fla = EnableMode::DefaultOn;
    EnableMode AntiDebug = EnableMode::DefaultOff;

    // --- substitution ---
    int SubLoop = 1;

    // --- flattening ---
    double FlaProbability = 1.0;
    int    FlaMinBBs      = 2;
    int    FlaMaxBBs      = 4096;

    // --- skip lists (filename globs / function names) ---
    llvm::SmallVector<std::string, 8> SkipFileGlobs;
    llvm::SmallVector<std::string, 8> SkipFuncNames;

    // --- anti-debug constructor injection targets ---
    bool AntiInjectPtrace   = true;
    bool AntiInjectSysctl   = true;
    bool AntiInjectDyldHook = true;
    bool AntiInjectEntitlement = true;

    // --- JSON file path actually used (for diags) ---
    std::string ConfigFilePath;

    // --- random engine (seeded once from random_device) ---
    std::mt19937 Rng;

    // --- load helpers ---
    static Config &get();   // singleton (per-process = per-plugin-load)

    void loadFromEnv();
    bool loadFromJSON(llvm::StringRef Path);
    bool autoLocateAndLoadJSON(llvm::StringRef HintDir = {});

    // --- per-Pass / per-target decision helpers ---
    bool shouldRunStr(const llvm::GlobalVariable *GV,
                      llvm::StringRef SourceFile = {}) const;
    bool shouldRunSub(const llvm::Function &F,
                      llvm::StringRef SourceFile = {}) const;
    bool shouldRunFla(const llvm::Function &F,
                      llvm::StringRef SourceFile = {},
                      unsigned BBcount = 0) const;

    // --- annotation helpers ---
    static bool hasAnnotation(const llvm::Function &F, llvm::StringRef Key);
    static bool hasAnnotation(const llvm::GlobalVariable &GV, llvm::StringRef Key);
    static bool hasCocoonsNo(const llvm::Function &F) {
        return hasAnnotation(F, "cocoons:no");
    }
    static bool hasCocoonsNo(const llvm::GlobalVariable &GV) {
        return hasAnnotation(GV, "cocoons:no");
    }
    static bool fileMatchesAnyGlob(llvm::StringRef File,
                                   const llvm::SmallVectorImpl<std::string> &Globs);
};

} // namespace cocoons

#endif // COCOONS_CONFIG_H
