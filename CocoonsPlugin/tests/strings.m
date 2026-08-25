// Cocoons L2 回归用例：字符串混淆
// 用 __attribute__((annotate("obfuscate"))) 标记的字符串应该被加密

#import <Foundation/Foundation.h>

#define OBF __attribute__((annotate("obfuscate")))

OBF const char c_array_marker[]  = "COCOONS_MARKER_C_ARRAY";
OBF const char *c_ptr_marker     = "COCOONS_MARKER_C_PTR";
OBF NSString *oc_marker          = @"COCOONS_MARKER_OC";

int main(void) {
    @autoreleasepool {
        NSLog(@"C Array : %s",  c_array_marker);
        NSLog(@"C Ptr   : %s",  c_ptr_marker);
        NSLog(@"OC      : %@",  oc_marker);
    }
    return 0;
}
