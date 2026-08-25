/*
 * Cocoons L2 回归用例：指令替换
 * hot_func 内的 + - & | ^ 应该被替换为复杂的位运算组合
 * 验证方式：llvm-objdump -d build/substitution | grep -E "addl|subl" 的数量
 *         vs 未混淆的二进制
 */

__attribute__((noinline))
static int hot_func(int a, int b, int c, int d) {
    int sum  = a + b + c + d;             // Add
    int diff = a - b - c;                 // Sub
    int and  = a & b & c;                 // And
    int or   = a | b | c;                 // Or
    int xor  = a ^ b ^ c;                 // Xor
    int combo = (sum & diff) | (and ^ or) - xor;
    return combo;
}

int main(void) {
    int acc = 0;
    for (int i = 0; i < 100; ++i) {
        acc += hot_func(i, i * 3, i >> 1, i & 0xFF);
    }
    return acc == 0 ? 0 : 1;
}
