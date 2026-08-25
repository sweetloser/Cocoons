# 更新日志

## 1.1.0（架构升级 · Out-of-Tree Pass Plugin）

> 2026-08-25 · 与 v1.0.x 功能兼容，二进制产物一致

### ✨ 架构重构（核心变更）

- **【脱离 LLVM monorepo】** 将 Cocoons 三大混淆 Pass（字符串 / 指令 / 控制流）从 `llvm/lib/Transforms/Cocoons/` 迁移为独立的 Out-of-Tree 工程：`CocoonsPlugin/`
- **【零侵入上游】** 不再硬编码修改 `PassBuilderPipelines.cpp` 与 `Transforms/CMakeLists.txt`，`llvm/` 目录完全恢复到上游原生状态，后续升级 LLVM 版本不再需要 rebase
- **【官方扩展点】** 改用 LLVM Pass Plugin ABI 入口 `llvmGetPassPluginInfo()`，通过：
  - `registerOptimizerLastEPCallback` 自动注入 O1~O3 优化管线
  - `registerPipelineParsingCallback` 注册 `cocoons-str` / `cocoons-sub` / `cocoons-fla` / `cocoons-full` 四条命名管线（兼容 `opt -passes=`）
- **【秒级编译】** Pass 单独编译（cmake 编译 CocoonsPlugin ~15 秒，之前全量 LLVM 编译 >30 分钟，提速约 80x）
- **【轻量分发】** 产物从 10GB+ 的 `.xctoolchain` 压缩到 **<10MB** 的 `libCocoonsPlugin.dylib`

### 🛠 新增接入方式

新增 `CocoonsPlugin/tools/` 目录，包含 Xcode / CocoaPods / Bazel 全场景接入方案：

1. **Xcode 原生 Other C Flags 注入**（推荐）：`-fpass-plugin=$(COCOONS_PLUGIN_DIR)/libCocoonsPlugin.dylib`
2. **CocoaPods post_install Hook**：示例见 `CocoonsPlugin/tools/xcode-integration/Podfile.cocoons.example.rb`
3. **xcconfig 一键接入**：示例见 `CocoonsPlugin/tools/xcode-integration/cocoons.xcconfig`
4. **clang 包装器 `cocoons-clang`**：`--enable-str / --enable-sub / --enable-fla / --enable-all` 简化参数（Python 脚本，跨平台）

### ✅ 新增质量保障

- 新增 `CocoonsPlugin/tests/` L1+L2 回归套件：`strings.m` / `substitution.c` / `flattening.m`
- 新增一键冒烟脚本 `CocoonsPlugin/tests/run_tests.sh`，覆盖：
  - L1 插件符号检查、opt 命名管线调用
  - L2 字符串混淆二进制不可见 + 运行时解密正确性
  - L2 指令替换 add/sub 指令密度对比
  - L2 CFG 平坦化基本块膨胀对比
- 所有 cocoons 日志统一前缀 `>>> [Cocoons]`，便于识别归属

### 🐛 修复 & 小改动

- 修复 `SubstitutionPass` cl::desc 拼写错误：`Substitutuin` → `Substitution`
- 移除头文件 `using namespace llvm;`，改用命名空间前缀或在 cpp 内部局部 `using namespace`，降低 ADL 污染

### ⚠️ 兼容性说明

- **功能兼容**：所有 `cocoons-enable-*` / `cocoons-sub-loop` 命令行参数完全保持不变
- **部署兼容**：原有的 `build.sh` 全量编译 Toolchain 方式依然可用（但已标记为 Legacy，新工程请使用 `CocoonsPlugin` 新方式）
- **版本要求**：LLVM >= 20.x（推荐 >= 21.1.8 与原基线一致）

---

## 1.0.1

- 修复字符串混淆在多 module 下的 bug

## 1.0.0

- 增加 C/OC 字符串混淆
- 增加加法指令替换
