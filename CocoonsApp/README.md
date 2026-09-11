# Cocoons macOS App

CocoonsApp 是工程批处理产品的第一阶段实现：

```text
输入 MyApp/
    ↓
校验 Xcode 工程
    ↓
复制到临时目录
    ↓
保护显式标记的字符串
    ↓
原子生成 MyApp_cocoons/
```

输入工程不会被修改。输出目录中包含：

- 完整的工程副本；
- `.cocoons/config.json`；
- `.cocoons/report.json`；
- `COCOONS_REPORT.md`。

处理完成后可以关闭 Cocoons.app，直接用 Xcode 打开输出工程。

## 当前支持

- 输入根目录下存在 `.xcodeproj` 或 `.xcworkspace`；
- 扫描 C、C++、Objective-C、Objective-C++ 源文件；
- 跳过 `.git`、Pods、Carthage、Vendor、DerivedData 和构建目录；
- 对以下显式标记的简单全局字符串声明做源到源保护：
  - `annotate("obfuscate")`
  - `annotate("cocoons:str")`
  - `OBF`
- C 字符数组、C 字符指针、Objective-C `NSString *`；
- 固定种子，可生成可复现结果；
- 输出已存在时拒绝覆盖。

## 当前限制

- 字符串转换采用保守规则，只处理简单的全局声明；宏展开、复杂声明和函数内字符串会跳过；
- 指令替换和控制流平坦化仍属于原有 LLVM 插件能力，尚未迁移到自包含的源码处理引擎；
- 当前只验证工程结构与源码变换，不自动执行用户工程的完整 `xcodebuild archive`；
- Swift 源码尚未处理。

## 生成与测试

需要 XcodeGen：

```shell
brew install xcodegen
xcodegen generate --spec CocoonsApp/project.yml --project CocoonsApp
```

运行测试：

```shell
xcodebuild \
  -project CocoonsApp/Cocoons.xcodeproj \
  -scheme Cocoons \
  -destination 'platform=macOS' \
  -derivedDataPath CocoonsApp/.derivedData \
  test CODE_SIGNING_ALLOWED=NO
```

测试会验证：

- 输入工程保持不变；
- 同级 `_cocoons` 输出；
- C 字符串保护后的源码可编译并正确解密；
- Objective-C 字符串保护后的源码可编译并正确解密；
- 已有输出不会被覆盖。
