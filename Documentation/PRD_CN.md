# Cocoons 产品需求文档

| 字段 | 内容 |
| --- | --- |
| 产品 | Cocoons |
| 版本 | 0.4 |
| 状态 | MVP 实施中 |
| 日期 | 2026-09-01 |

## 1. 产品定位

Cocoons 是一款 macOS 上的 Apple 工程保护应用。用户选择一个本地 Xcode 工程目录，
Cocoons 按保护策略处理后，在输入工程同级生成 `{原工程名}_cocoons`。原工程不被修改，
输出工程可在不启动 Cocoons.app 的情况下用 Xcode 独立编译。

```text
/path/MyApp/
        ↓ Cocoons.app
/path/MyApp_cocoons/
```

第一版通过官网 DMG 分发，不以 Mac App Store 为交付目标。

## 2. 目标用户

- 希望降低 iOS / macOS Native 代码逆向风险的开发者；
- 需要可审计、可重复执行加固任务的安全团队；
- 希望保留原工程、用旁路工程出包的 SDK 或 App 团队。

## 3. 核心原则

1. 输入工程默认只读，不做原地覆盖。
2. 所有写入先发生在临时目录，成功后原子生成正式输出。
3. 输出工程不依赖 Cocoons.app 常驻。
4. 默认只保护显式选中的目标，不对未知代码做激进全量改写。
5. 不承诺“无法逆向”，只提升逆向与篡改成本。
6. 产品主路径不要求用户维护 LLVM fork。

## 4. MVP 用户旅程

1. 从官网下载并安装 Cocoons.app。
2. 选择包含 `.xcodeproj` 或 `.xcworkspace` 的工程根目录。
3. 开启“保护显式标记的字符串”，配置确定性种子。
4. 点击“开始处理”。
5. 查看进度、日志和处理结果。
6. 用 Xcode 打开同级生成的 `_cocoons` 工程。
7. 在输出工程中完成 Release / Archive。

## 5. MVP 功能

| 功能 | 优先级 | 状态 |
| --- | --- | --- |
| macOS 原生应用与工程目录选择 | P0 | 已实现 |
| 输入工程结构校验 | P0 | 已实现 |
| 临时目录处理与同级原子输出 | P0 | 已实现 |
| 拒绝覆盖已有输出与原工程 | P0 | 已实现 |
| C 字符数组/指针的显式字符串保护 | P0 | 已实现（保守声明范围） |
| Objective-C `NSString *` 显式字符串保护 | P0 | 已实现（保守声明范围） |
| 固定种子与可复现输出 | P0 | 已实现 |
| JSON 处理配置与报告 | P0 | 已实现 |
| Xcode 一键打开输出工程 | P1 | 已实现 |
| 完整执行用户工程 `xcodebuild archive` 验证 | P1 | 待实现 |
| 多 workspace/scheme/target 选择 | P1 | 待实现 |
| 历史任务 | P1 | 待实现 |
| 源码级指令替换 | P2 | 待设计 |
| 源码级控制流混淆 | P2 | 待设计 |
| Swift 保护 | P2 | 待评估 |

## 6. 字符串保护范围

第一版只处理简单全局声明，支持以下显式标记：

```objc
__attribute__((annotate("obfuscate")))
const char secret[] = "secret";

__attribute__((annotate("cocoons:str")))
const char *token = "token";

__attribute__((annotate("obfuscate")))
NSString *message = @"message";
```

也兼容项目中定义的 `OBF` 标记。复杂宏、函数内局部字符串、不确定所有权语义的声明应
安全跳过，不能为了覆盖率破坏工程正确性。

## 7. 输出契约

输出目录必须包含：

- 可独立打开的完整工程副本；
- `.cocoons/config.json`；
- `.cocoons/report.json`；
- `COCOONS_REPORT.md`。

输出目录已存在时，MVP 直接拒绝处理，避免误覆盖。后续可增加用户明确确认后的“清理并
重新生成”，但始终禁止覆盖输入工程。

## 8. 非目标

- 第一版不做 IPA 输入/IPA 输出；
- 不提供完整替代 Apple Toolchain 的编译器发行版；
- 不宣称覆盖所有 C++/Objective-C 宏与语法；
- 不把现有 LLVM Pass 的全部能力伪装成源码处理能力；
- 不默认上传用户源码。

## 9. 验收标准

- 输入工程在处理前后字节内容不变；
- 输出名称固定为 `{原工程名}_cocoons`；
- 已保护字符串不再以原始字符串字面量存在于输出源码；
- 生成的 C 与 Objective-C 示例可由 Apple clang 编译并在运行时正确还原；
- 失败时删除临时目录，不生成冒充成功的输出；
- App 关闭后输出工程仍可使用。

## 10. 下一阶段

1. 读取 `xcodebuild -list -json`，支持 workspace/scheme 选择。
2. 在处理前后执行可选的 `xcodebuild` 验证。
3. 增加任务历史、取消处理、重新生成。
4. 将字符串处理从保守规则升级为 Clang AST/Token 感知引擎。
5. 单独评审指令替换、控制流混淆的源码级可行性。
