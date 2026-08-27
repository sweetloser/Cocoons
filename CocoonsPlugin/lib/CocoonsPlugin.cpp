#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"

#include "cocoons/StringObfuscationPass.h"
#include "cocoons/SubstitutionPass.h"
#include "cocoons/FlatteningPass.h"
#include "cocoons/AntiDebuggingPass.h"
#include "cocoons/Config.h"

using namespace llvm;

namespace {
struct CocoonsFPMAdaptor : public PassInfoMixin<CocoonsFPMAdaptor> {
    FunctionPassManager FPM;
    CocoonsFPMAdaptor(FunctionPassManager &&M) : FPM(std::move(M)) {}
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (Function &F : M) {
            if (!F.isDeclaration() && F.hasFnAttribute(Attribute::OptimizeNone))
                F.removeFnAttr(Attribute::OptimizeNone);
        }
        PreservedAnalyses PA = PreservedAnalyses::all();
        FunctionAnalysisManager &FAM =
            AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            PA.intersect(FPM.run(F, FAM));
        }
        return PA;
    }
};
} // anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "Cocoons",
        "1.2.0",
        [](PassBuilder &PB) {
            // Touch Config singleton so JSON + env vars are parsed once (before any
            // pass runs). Log loaded config for diagnostics.
            const cocoons::Config &Cfg = cocoons::Config::get();
            auto enabledStr = [](cocoons::EnableMode M) -> const char * {
                switch (M) {
                    case cocoons::EnableMode::DefaultOff: return "off";
                    case cocoons::EnableMode::DefaultOn:  return "on";
                    case cocoons::EnableMode::Annotation: return "annotations-only";
                }
                return "off";
            };
            errs() << ">>> [Cocoons] plugin v1.2.0 loaded; config: "
                   << "str=" << enabledStr(Cfg.Str) << " "
                   << "sub=" << enabledStr(Cfg.Sub) << "(" << Cfg.SubLoop << ") "
                   << "fla=" << enabledStr(Cfg.Fla)
                   << "(P=" << Cfg.FlaProbability << " BB=[" << Cfg.FlaMinBBs << "," << Cfg.FlaMaxBBs << "]) "
                   << "anti_debug=" << enabledStr(Cfg.AntiDebug);
            if (!Cfg.ConfigFilePath.empty()) errs() << " cfg=\"" << Cfg.ConfigFilePath << "\"";
            if (!Cfg.SkipFileGlobs.empty()) errs() << " skip_files=" << Cfg.SkipFileGlobs.size();
            if (!Cfg.SkipFuncNames.empty()) errs() << " skip_funcs=" << Cfg.SkipFuncNames.size();
            errs() << "\n";

            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level, ThinOrFullLTOPhase) {
                    if (Level == OptimizationLevel::O0) return;

                    const cocoons::Config &Cfg = cocoons::Config::get();
                    bool UseStr = Cfg.Str != cocoons::EnableMode::DefaultOff;
                    bool UseSub = Cfg.Sub != cocoons::EnableMode::DefaultOff;
                    bool UseFla = Cfg.Fla != cocoons::EnableMode::DefaultOff;
                    bool UseAD  = Cfg.AntiDebug != cocoons::EnableMode::DefaultOff;
                    if (!(UseStr || UseSub || UseFla || UseAD)) return;

                    FunctionPassManager CocoonsFPM;
                    if (UseSub) CocoonsFPM.addPass(cocoons::SubstitutionPass());
                    if (UseFla) CocoonsFPM.addPass(cocoons::FlatteningPass());
                    if (UseSub || UseFla) {
                        MPM.addPass(CocoonsFPMAdaptor(std::move(CocoonsFPM)));
                    }
                    // Module-level passes (Str then Anti-Debug)
                    if (UseStr) MPM.addPass(cocoons::StringObfuscationPass());
                    if (UseAD)  MPM.addPass(cocoons::AntiDebuggingPass());
                }
            );

            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    bool Matched = false;
                    if (Name == "cocoons-str" || Name == "cocoons-full") {
                        MPM.addPass(cocoons::StringObfuscationPass());
                        Matched = true;
                    }
                    if (Name == "cocoons-anti-debug" || Name == "cocoons-full") {
                        MPM.addPass(cocoons::AntiDebuggingPass());
                        Matched = true;
                    }
                    if (Name == "cocoons-full") {
                        FunctionPassManager FPM;
                        FPM.addPass(cocoons::SubstitutionPass());
                        FPM.addPass(cocoons::FlatteningPass());
                        MPM.addPass(CocoonsFPMAdaptor(std::move(FPM)));
                        Matched = true;
                    }
                    return Matched;
                }
            );

            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "cocoons-sub") {
                        FPM.addPass(cocoons::SubstitutionPass());
                        return true;
                    }
                    if (Name == "cocoons-fla") {
                        FPM.addPass(cocoons::FlatteningPass());
                        return true;
                    }
                    return false;
                }
            );
        }
    };
}
