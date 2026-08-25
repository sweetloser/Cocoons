#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"

#include "cocoons/StringObfuscationPass.h"
#include "cocoons/SubstitutionPass.h"
#include "cocoons/FlatteningPass.h"

using namespace llvm;

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "Cocoons",
        "1.1.0",
        [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level, ThinOrFullLTOPhase) {
                    if (Level == OptimizationLevel::O0) return;

                    bool UseStr = cocoons::StringObfuscationPass::isEnabled();
                    bool UseSub = cocoons::SubstitutionPass::isEnabled();
                    bool UseFla = cocoons::FlatteningPass::isEnabled();
                    if (!(UseStr || UseSub || UseFla)) return;

                    FunctionPassManager CocoonsFPM;
                    if (UseSub) CocoonsFPM.addPass(cocoons::SubstitutionPass());
                    if (UseFla) CocoonsFPM.addPass(cocoons::FlatteningPass());
                    if (UseStr) MPM.addPass(cocoons::StringObfuscationPass());
                    if (UseSub || UseFla) {
                        MPM.addPass(createModuleToFunctionPassAdaptor(std::move(CocoonsFPM)));
                    }
                }
            );

            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "cocoons-str") {
                        MPM.addPass(cocoons::StringObfuscationPass());
                        return true;
                    }
                    return false;
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
                    if (Name == "cocoons-full") {
                        FPM.addPass(cocoons::SubstitutionPass());
                        FPM.addPass(cocoons::FlatteningPass());
                        return true;
                    }
                    return false;
                }
            );
        }
    };
}
