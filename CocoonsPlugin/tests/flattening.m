// Cocoons L2 回归用例：控制流平坦化
// complex_logic 原本是多个 if/else，平坦化后应该变成 switch-case 状态机

#import <Foundation/Foundation.h>

__attribute__((noinline))
static NSInteger complex_logic(NSInteger mode, NSInteger x, NSInteger y) {
    NSInteger r = 0;
    if (mode == 0) {
        r = x + y;
    } else if (mode == 1) {
        r = x - y;
    } else if (mode == 2) {
        r = x * y;
    } else if (mode == 3) {
        r = (x > y) ? x : y;
    } else {
        r = x ^ y;
    }
    for (NSInteger i = 0; i < 4; ++i) {
        r += i;
        if (r % 7 == 0) r *= 2;
    }
    return r;
}

int main(void) {
    @autoreleasepool {
        for (NSInteger m = 0; m < 5; ++m) {
            NSLog(@"mode=%ld => %ld", (long)m, (long)complex_logic(m, 37, 23));
        }
    }
    return 0;
}
