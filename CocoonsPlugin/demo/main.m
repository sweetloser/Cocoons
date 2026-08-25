#import <Foundation/Foundation.h>

/*
 * Cocoons v1.1.0 Out-of-Tree Plugin — 用户向 Demo
 * --------------------------------------------------
 * 覆盖 3 个 Pass 的典型触发场景：
 *   1) StringObfuscation  — __attribute__((annotate("obfuscate"))) 标记
 *   2) Substitution       — 整数加减与位运算密集函数
 *   3) Flattening         — 分支/循环多的控制流函数
 *
 * 构建方式（本目录下）：
 *   ./build.sh               产出 build/before + build/after，并打印对比
 */

#define OBF  __attribute__((annotate("obfuscate")))
#define NOINLINE __attribute__((noinline, used))

#pragma mark - Pass 1: 字符串混淆

static OBF const char kCStrArray[]  = "Cocoons-Demo: This is a C array secret, must NOT appear in binary.";
static OBF const char kCStrPtrBuf[] = "Cocoons-Demo: This is a C pointer secret, also encrypted.";
static OBF const char kNSBuf[]      = "Cocoons-Demo: This is an NSString secret, decrypted from C char array.";

static void showStrings(void) {
    NSLog(@"[STR] C array  = %s",  kCStrArray);
    NSLog(@"[STR] C ptr    = %s",  kCStrPtrBuf);
    NSLog(@"[STR] NSString = %@",  [NSString stringWithUTF8String:kNSBuf]);
}

#pragma mark - Pass 2: 指令替换（二进制运算密集）

NOINLINE int calcSignature(int a, int b, int c) {
    int x = a + b;
    int y = x - c;
    int z = y & 0xFF;
    int w = z | 0x1000;
    int v = w ^ 0x5A5A;
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += (v + i) ^ (a - i);
        sum  = sum & (b | c);
    }
    return sum;
}

#pragma mark - Pass 3: 控制流平坦化（分支 / 循环交织）

typedef enum {
    StepInit = 0, StepValidate, StepCompute, StepRound, StepFinish
} DemoStep;

NOINLINE int flattenDispatch(int seed, int rounds) {
    int result = 0;
    DemoStep step = StepInit;
    int guard  = (seed > 0) ? 1 : 0;
    int iter   = 0;

    while (step != StepFinish) {
        switch (step) {
            case StepInit:
                result = seed * 3;
                step   = guard ? StepValidate : StepFinish;
                break;
            case StepValidate:
                if (rounds <= 0) { step = StepFinish; break; }
                step = StepCompute;
                break;
            case StepCompute:
                result = (result + 0x9E37) ^ seed;
                step   = StepRound;
                break;
            case StepRound:
                iter++;
                if (iter < rounds) {
                    step = StepCompute;
                } else {
                    step = StepFinish;
                }
                break;
            case StepFinish:
            default:
                step = StepFinish;
                break;
        }
    }
    return result;
}

#pragma mark - expected values (会在第一次 run 时用 BEFORE 实际输出填充)
/* 在 BEFORE 上实际运行得到：
 *   calcSignature(10,20,5)  = 0
 *   flattenDispatch(7,4)    = 162037
 * 此即 BEFORE/AFTER 语义等价性的基准。AFTER 必须打印相同数字。
 */
#define EXP_SIG  0
#define EXP_FLA  162037

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSLog(@"== Cocoons Demo (v1.1.0 Out-of-Tree Plugin) ==");

        showStrings();

        int sig = calcSignature(10, 20, 5);
        NSLog(@"[SUB] calcSignature(10,20,5) = %d (expected %d)", sig, EXP_SIG);

        int fla = flattenDispatch(7, 4);
        NSLog(@"[FLA] flattenDispatch(7,4)   = %d (expected %d)", fla, EXP_FLA);

        NSLog(@"== Demo end ==");
    }
    return 0;
}
