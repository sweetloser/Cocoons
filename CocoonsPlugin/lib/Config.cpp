// ============================================================
// Cocoons Config implementation
// ============================================================

#include "cocoons/Config.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

using namespace llvm;

namespace cocoons {

static bool envBool(const char *Name, bool Default) {
    const char *V = std::getenv(Name);
    if (!V) return Default;
    return StringRef(V).equals_insensitive("1")
        || StringRef(V).equals_insensitive("true")
        || StringRef(V).equals_insensitive("on")
        || StringRef(V).equals_insensitive("yes");
}

static int envInt(const char *Name, int Default) {
    const char *V = std::getenv(Name);
    if (!V) return Default;
    int N = Default;
    if (StringRef(V).getAsInteger(10, N)) return Default;
    return N;
}

static double envDouble(const char *Name, double Default) {
    const char *V = std::getenv(Name);
    if (!V) return Default;
    char *End = nullptr;
    double X = std::strtod(V, &End);
    if (!End || End == V) return Default;
    return X;
}

static EnableMode envMode(const char *Name, EnableMode Default) {
    const char *V = std::getenv(Name);
    if (!V) return Default;
    StringRef S = V;
    if (S.equals_insensitive("annotation") || S.equals_insensitive("annotations")
        || S.equals_insensitive("only-annotations") || S == "anno")
        return EnableMode::Annotation;
    if (S.equals_insensitive("0") || S.equals_insensitive("false")
        || S.equals_insensitive("off") || S.equals_insensitive("no"))
        return EnableMode::DefaultOff;
    if (S.equals_insensitive("1") || S.equals_insensitive("true")
        || S.equals_insensitive("on") || S.equals_insensitive("yes"))
        return EnableMode::DefaultOn;
    return Default;
}

// ── very small handwritten JSON parser (subset: object with bool/int/number/string/array<string>) ──
// Not general purpose; only covers our schema.
namespace {
struct JParser {
    StringRef S;
    size_t I = 0;
    bool fail = false;
    JParser(StringRef s) : S(s) {}

    void skipWs() {
        while (I < S.size() && (S[I]==' '||S[I]=='\t'||S[I]=='\n'||S[I]=='\r')) ++I;
    }
    bool eof() const { return I >= S.size() || fail; }
    char peek() { skipWs(); return eof() ? '\0' : S[I]; }
    char eat() { return eof() ? '\0' : S[I++]; }
    bool match(char c) {
        skipWs();
        if (!eof() && S[I] == c) { ++I; return true; }
        return false;
    }
    void expect(char c) { if (!match(c)) { fail = true; } }

    std::string parseString() {
        if (!match('"')) { fail = true; return {}; }
        std::string out;
        while (!eof() && S[I] != '"') {
            char c = S[I++];
            if (c == '\\' && !eof()) {
                char e = S[I++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case '"': case '\\': case '/': out += e; break;
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        expect('"');
        return out;
    }

    using ObjectMap = SmallVector<std::pair<std::string, std::string>, 16>;
    // parseValue returns "typename:<raw>"
    // For object: "obj:" (we call parseObject separately because nested arrays not needed)
    // We'll implement: value = string | number | bool | null | array<string> | object
    // We keep it simple: at top we parse an object, each value is one of the four scalar types or array<string>.

    void parseTopObject(ObjectMap &Out);
};

void JParser::parseTopObject(ObjectMap &Out) {
    expect('{');
    skipWs();
    if (match('}')) return;
    for (;;) {
        skipWs();
        std::string key = parseString();
        if (fail) return;
        skipWs();
        expect(':');
        skipWs();
        std::string valRepr;
        char c = peek();
        if (c == '"') {
            valRepr = "s:" + parseString();
        } else if (c == 't' || c == 'f') {
            // true/false
            size_t start = I;
            while (I < S.size() && (std::isalpha((unsigned char)S[I]) || S[I]=='_')) ++I;
            StringRef word = S.slice(start, I);
            valRepr = "b:" + word.str();
        } else if (c == '-' || std::isdigit((unsigned char)c)) {
            size_t start = I;
            bool dot = false;
            while (I < S.size() && (std::isdigit((unsigned char)S[I]) || S[I]=='.' || S[I]=='-' || S[I]=='+' ||
                                    S[I]=='e' || S[I]=='E')) {
                if (S[I]=='.') dot=true; ++I;
            }
            valRepr = std::string(dot?"n":"i") + ":" + S.slice(start, I).str();
        } else if (c == '[') {
            // array<string>
            eat();  // '['
            valRepr = "a:";
            bool first = true;
            skipWs();
            while (!eof() && peek() != ']') {
                if (!first) expect(',');
                skipWs();
                std::string s = parseString();
                if (!first) valRepr += '\x01';
                valRepr += s;
                first = false;
                skipWs();
            }
            expect(']');
        } else {
            fail = true; return;
        }
        Out.push_back({std::move(key), std::move(valRepr)});
        skipWs();
        if (match(',')) continue;
        if (match('}')) return;
        fail = true; return;
    }
}

static bool toBool(StringRef Raw) {
    return Raw.equals_insensitive("true")
        || Raw.equals_insensitive("1")
        || Raw.equals_insensitive("on")
        || Raw.equals_insensitive("yes");
}
} // anon namespace

// ── glob match: supports **/*/?  ──
static bool globMatch(StringRef Pat, StringRef Str) {
    // Classic DP glob.
    size_t n = Pat.size(), m = Str.size();
    SmallVector<bool> dpPrev(m + 1, false), dpCur(m + 1, false);
    dpPrev[0] = true;
    for (size_t i = 1; i <= n; ++i) {
        char pc = Pat[i-1];
        dpCur.assign(m+1, false);
        // leading ** or * in pattern can match empty
        if (pc == '*') dpCur[0] = dpPrev[0];
        for (size_t j = 1; j <= m; ++j) {
            if (pc == '*') {
                bool seq = (i >= 2 && Pat[i-2] == '*');  // **
                // '*' matches empty string (dpPrev[j]) OR any single char (dpCur[j-1])
                // '**' additionally can match across '/'.
                dpCur[j] = dpPrev[j] || dpCur[j-1];
                (void)seq;
            } else if (pc == '?' || pc == Str[j-1]) {
                dpCur[j] = dpPrev[j-1];
            } else if (pc == '/' && Str[j-1] == '/') {
                dpCur[j] = dpPrev[j-1];
            }
        }
        dpPrev.swap(dpCur);
    }
    return dpPrev[m];
}

bool Config::fileMatchesAnyGlob(StringRef File, const SmallVectorImpl<std::string> &Globs) {
    if (Globs.empty()) return false;
    for (const auto &G : Globs) {
        if (globMatch(G, File)) return true;
    }
    return false;
}

Config &Config::get() {
    static Config Cfg([]{
        Config C;
        std::random_device RD;
        C.Rng.seed(RD());
        return C;
    }());
    static bool Loaded = false;
    if (!Loaded) {
        Loaded = true;
        Cfg.loadFromEnv();
        Cfg.autoLocateAndLoadJSON();
    }
    return Cfg;
}

void Config::loadFromEnv() {
    Str = envMode("COCOONS_ENABLE_STR", Str);
    Sub = envMode("COCOONS_ENABLE_SUB", Sub);
    Fla = envMode("COCOONS_ENABLE_FLA", Fla);
    AntiDebug = envMode("COCOONS_ENABLE_ANTI_DEBUG", AntiDebug);
    SubLoop = std::max(1, envInt("COCOONS_SUB_LOOP", SubLoop));
    FlaProbability = std::clamp(envDouble("COCOONS_FLA_PROBABILITY", FlaProbability), 0.0, 1.0);
    FlaMinBBs = std::max(1, envInt("COCOONS_FLA_MIN_BBS", FlaMinBBs));
    FlaMaxBBs = std::max(FlaMinBBs, envInt("COCOONS_FLA_MAX_BBS", FlaMaxBBs));
    AntiInjectPtrace      = envBool("COCOONS_ANTI_PTRACE", AntiInjectPtrace);
    AntiInjectSysctl      = envBool("COCOONS_ANTI_SYSCTL", AntiInjectSysctl);
    AntiInjectDyldHook    = envBool("COCOONS_ANTI_DYLD", AntiInjectDyldHook);
    AntiInjectEntitlement = envBool("COCOONS_ANTI_ENTITLEMENT", AntiInjectEntitlement);
}

bool Config::loadFromJSON(StringRef Path) {
    auto Buf = MemoryBuffer::getFileOrSTDIN(Path);
    if (!Buf) {
        return false;
    }
    StringRef Text = Buf.get()->getBuffer();
    JParser P(Text);
    JParser::ObjectMap Map;
    P.parseTopObject(Map);
    if (P.fail) return false;

    ConfigFilePath = Path.str();
    auto findVal = [&](StringRef k) -> StringRef {
        for (const auto &kv : Map) if (kv.first == k) return kv.second;
        return {};
    };
    auto parseBool = [&](StringRef k, bool &Out) {
        StringRef v = findVal(k);
        if (v.empty()) return;
        if (v.starts_with("b:")) Out = toBool(v.drop_front(2));
        else if (v.starts_with("i:")) { long long x; if (!v.drop_front(2).getAsInteger(10, x)) Out = (bool)x; }
    };
    auto parseInt = [&](StringRef k, int &Out) {
        StringRef v = findVal(k);
        if (v.starts_with("i:")) { long long x; if (!v.drop_front(2).getAsInteger(10, x)) Out = (int)x; }
    };
    auto parseNum = [&](StringRef k, double &Out) {
        StringRef v = findVal(k);
        if (v.starts_with("n:")) {
            StringRef S = v.drop_front(2);
            char *End = nullptr;
            double X = std::strtod(S.data(), &End);
            if (End && End != S.data()) Out = X;
        } else if (v.starts_with("i:")) { long long x; if (!v.drop_front(2).getAsInteger(10, x)) Out = (double)x; }
    };
    auto parseMode = [&](StringRef k, EnableMode &Out) {
        StringRef v = findVal(k);
        if (!v.starts_with("s:")) return;
        StringRef s = v.drop_front(2);
        if (s.equals_insensitive("annotation") || s.equals_insensitive("annotations")
            || s.equals_insensitive("only-annotations")) Out = EnableMode::Annotation;
        else if (s.equals_insensitive("0") || s.equals_insensitive("false") || s.equals_insensitive("off") || s.equals_insensitive("no")) Out = EnableMode::DefaultOff;
        else if (s.equals_insensitive("1") || s.equals_insensitive("true")  || s.equals_insensitive("on")  || s.equals_insensitive("yes")) Out = EnableMode::DefaultOn;
    };
    auto parseStringArr = [&](StringRef k, SmallVectorImpl<std::string> &Out) {
        StringRef v = findVal(k);
        if (!v.starts_with("a:")) return;
        SmallVector<StringRef, 8> parts;
        v.drop_front(2).split(parts, '\x01', -1, false);
        Out.clear();
        for (auto &p : parts) Out.push_back(p.str());
    };

    parseMode("enable_str", Str);
    parseMode("enable_sub", Sub);
    parseMode("enable_fla", Fla);
    parseMode("enable_anti_debug", AntiDebug);
    parseInt("sub_loop", SubLoop); if (SubLoop < 1) SubLoop = 1;
    parseNum("fla_probability", FlaProbability);
    if (FlaProbability < 0.0) FlaProbability = 0.0;
    if (FlaProbability > 1.0) FlaProbability = 1.0;
    parseInt("fla_min_bbs", FlaMinBBs); if (FlaMinBBs < 1) FlaMinBBs = 1;
    parseInt("fla_max_bbs", FlaMaxBBs); if (FlaMaxBBs < FlaMinBBs) FlaMaxBBs = FlaMinBBs;
    parseStringArr("skip_files", SkipFileGlobs);
    parseStringArr("skip_functions", SkipFuncNames);
    parseBool("anti_ptrace", AntiInjectPtrace);
    parseBool("anti_sysctl", AntiInjectSysctl);
    parseBool("anti_dyld_hook", AntiInjectDyldHook);
    parseBool("anti_entitlement", AntiInjectEntitlement);
    return true;
}

bool Config::autoLocateAndLoadJSON(StringRef HintDir) {
    SmallString<128> P;
    const char *EnvPath = std::getenv("COCOONS_CONFIG");
    if (EnvPath && *EnvPath) {
        return loadFromJSON(EnvPath);
    }
    // Walk up HintDir or current working directory looking for .cocoons.json
    SmallString<128> Cwd;
    if (!HintDir.empty()) Cwd = HintDir;
    else if (std::error_code EC = sys::fs::current_path(Cwd)) (void)EC;
    StringRef Start = !HintDir.empty() ? HintDir : StringRef(Cwd);
    if (Start.empty()) return false;
    P = Start;
    for (int levels = 0; levels < 16; ++levels) {
        SmallString<128> Cand(P);
        sys::path::append(Cand, ".cocoons.json");
        if (sys::fs::exists(Cand)) {
            return loadFromJSON(Cand);
        }
        StringRef Parent = sys::path::parent_path(P);
        if (Parent.empty() || Parent == P) break;
        P = Parent;
    }
    return false;
}

// ── annotation scanner (llvm.global.annotations format) ─────────────────────
// { i8* bitcast(GV or Fn to i8*), i8* getelementptr("cocoons:fla"), i32 0, i8* null }
static void walkAnnotationGV(const Module &M,
    function_ref<void(GlobalVariable *AnnoStrGV, GlobalVariable *TargetGV, Function *TargetFn)>
        Visit) {
    const GlobalVariable *AnnoGV = M.getGlobalVariable("llvm.global.annotations");
    if (!AnnoGV) return;
    const ConstantArray *Arr = dyn_cast<ConstantArray>(AnnoGV->getInitializer());
    if (!Arr) return;
    for (unsigned i = 0; i < Arr->getNumOperands(); ++i) {
        auto *CS = dyn_cast<ConstantStruct>(Arr->getOperand(i));
        if (!CS) continue;
        if (CS->getNumOperands() < 2) continue;
        Value *TargetRaw = CS->getOperand(0)->stripPointerCasts();
        auto *ASGV = dyn_cast<GlobalVariable>(CS->getOperand(1)->stripPointerCasts());
        if (!ASGV || !ASGV->hasInitializer()) continue;
        if (auto *CDS = dyn_cast<ConstantDataSequential>(ASGV->getInitializer())) {
            (void)CDS;
        }
        ConstantDataSequential *AnnoCDS = dyn_cast<ConstantDataSequential>(ASGV->getInitializer());
        if (!AnnoCDS) continue;
        GlobalVariable *TG = dyn_cast<GlobalVariable>(TargetRaw);
        Function     *TF = dyn_cast<Function>(TargetRaw);
        if (!TG && !TF) continue;
        Visit(ASGV, TG, TF);
    }
}

static StringRef annotationString(const GlobalVariable &AnnoStrGV) {
    if (!AnnoStrGV.hasInitializer()) return {};
    auto *CDS = dyn_cast<ConstantDataSequential>(AnnoStrGV.getInitializer());
    if (!CDS) return {};
    StringRef S = CDS->getAsString();
    // drop trailing \0
    while (!S.empty() && S.back() == '\0') S = S.drop_back();
    return S;
}

bool Config::hasAnnotation(const Function &F, StringRef Key) {
    const Module *M = F.getParent();
    if (!M) return false;
    bool Found = false;
    walkAnnotationGV(*M, [&](GlobalVariable *AnnoGV, GlobalVariable *, Function *TargetFn){
        if (!TargetFn || TargetFn != &F) return;
        StringRef S = annotationString(*AnnoGV);
        if (S.starts_with(Key)) { Found = true; }
    });
    return Found;
}

bool Config::hasAnnotation(const GlobalVariable &GV, StringRef Key) {
    const Module *M = GV.getParent();
    if (!M) return false;
    bool Found = false;
    walkAnnotationGV(*M, [&](GlobalVariable *AnnoGV, GlobalVariable *TargetGV, Function *){
        if (!TargetGV || TargetGV != &GV) return;
        StringRef S = annotationString(*AnnoGV);
        if (S.starts_with(Key)) { Found = true; }
    });
    return Found;
}

// ── per-pass / per-target decisions ─────────────────────────────────────────
static bool skipFileAndFunc(StringRef File, StringRef FnOrGVName,
                            const SmallVectorImpl<std::string> &Globs,
                            const SmallVectorImpl<std::string> &FuncNames) {
    if (!File.empty() && Config::fileMatchesAnyGlob(File, Globs)) return true;
    if (!FnOrGVName.empty()) {
        for (const auto &N : FuncNames) {
            if (globMatch(N, FnOrGVName)) return true;
        }
    }
    return false;
}

bool Config::shouldRunStr(const GlobalVariable *GV, StringRef SourceFile) const {
    if (!GV) return false;
    if (hasCocoonsNo(*GV)) return false;
    StringRef Name = GV->hasName() ? GV->getName() : "";
    if (skipFileAndFunc(SourceFile, Name, SkipFileGlobs, SkipFuncNames)) return false;
    switch (Str) {
        case EnableMode::DefaultOff: return hasAnnotation(*GV, "cocoons:str") || hasAnnotation(*GV, "obfuscate");
        case EnableMode::DefaultOn:  return true;   // classic behavior (obfuscate only applies when Str = DefaultOn)
        case EnableMode::Annotation: return hasAnnotation(*GV, "cocoons:str") || hasAnnotation(*GV, "obfuscate");
    }
    return false;
}

bool Config::shouldRunSub(const Function &F, StringRef SourceFile) const {
    if (F.isDeclaration()) return false;
    if (hasCocoonsNo(F)) return false;
    if (F.getMetadata("cocoons_protected")) return false;
    if (hasAnnotation(F, "cocoons:no")) return false;
    StringRef Name = F.getName();
    if (skipFileAndFunc(SourceFile, Name, SkipFileGlobs, SkipFuncNames)) return false;
    switch (Sub) {
        case EnableMode::DefaultOff: return hasAnnotation(F, "cocoons:sub");
        case EnableMode::DefaultOn:  return true;
        case EnableMode::Annotation: return hasAnnotation(F, "cocoons:sub");
    }
    return false;
}

bool Config::shouldRunFla(const Function &F, StringRef SourceFile, unsigned BBcount) const {
    if (F.isDeclaration()) return false;
    if (hasCocoonsNo(F)) return false;
    if (F.getMetadata("cocoons_protected")) return false;
    if (hasAnnotation(F, "cocoons:no")) return false;
    StringRef Name = F.getName();
    if (skipFileAndFunc(SourceFile, Name, SkipFileGlobs, SkipFuncNames)) return false;
    // BB count threshold:
    unsigned BC = BBcount ? BBcount : F.size();
    if (BC < (unsigned)FlaMinBBs) return false;
    if (BC > (unsigned)FlaMaxBBs) return false;
    bool Allow = false;
    switch (Fla) {
        case EnableMode::DefaultOff: Allow = hasAnnotation(F, "cocoons:fla"); break;
        case EnableMode::DefaultOn:  Allow = true; break;
        case EnableMode::Annotation: Allow = hasAnnotation(F, "cocoons:fla"); break;
    }
    if (!Allow) return false;
    if (FlaProbability >= 1.0) return true;
    if (FlaProbability <= 0.0) return false;
    std::uniform_real_distribution<double> D(0.0, 1.0);
    Config &NonConst = const_cast<Config&>(*this);
    double r = D(NonConst.Rng);
    return r < FlaProbability;
}

} // namespace cocoons
