<p align="center"> 
  <h1 align="center"> Cocoons </h1>
</p> 
<p align="center">
    <strong>作茧，不是自缚，每一根丝的加持，都是为了保护！</strong>
</p> 
<p align="center">
    <a><img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/home.jpg?raw=true" width="600" height="450" alt="Cocoons Logo"></a>
</p>
<p align="center">
    <div align="center"> 基于 LLVM 的 iOS / macOS 代码混淆工具链 · v1.2.0 (Out-of-Tree Plugin)</div>
</p>

---

## 📖 目录
* [项目简介](#项目简介)
* [功能特性](#功能特性)
* [环境与安装](#环境与安装)
* [配置系统（3 层优先级，v1.2.0 起）](#配置系统)
* [使用指南](#使用指南)
  * [Pass 功能总览](#pass-功能总览v120-起不再使用-mllvm)
  * [Xcode 集成](#xcode-集成)
  * [Bazel 集成](#bazel-集成)
  * [代码标记（注解粒度黑白名单）](#代码标记注解粒度黑白名单v120-新增)
* [更新日志](#更新日志)
* [演示效果](#演示效果)
* [支持与贡献](#支持) 

---

## <a id="项目简介"></a> 💡 项目简介

**Cocoons** 是一个基于 **LLVM Pass Plugin（Out-of-Tree）架构** 的 iOS / macOS 代码混淆工具链（兼容 LLVM `21.x`，推荐 `21.1.8`）。通过在编译期的 IR 层对代码进行变换，有效增加逆向工程（静态分析、动态调试）的难度，从而保护 iOS / macOS 应用程序的核心逻辑和敏感数据。

> 🔧 **架构亮点**：采用 Out-of-Tree Pass Plugin 模式，**零侵入上游 LLVM 源码**，支持 Homebrew 安装的官方 LLVM，单独编译、单独发布、秒级调试。  
> 🎚️ **配置亮点 (v1.2.0)**：3 层优先级配置系统 **「源码注解 > 环境变量 > `.cocoons.json`」**，适配 CLI / Xcode / Bazel / CI 任何构建环境；不再使用 `-mllvm -cocoons-*`（会因 cl::opt 注册时机导致 `Unknown command line argument`）。

---

## <a id="功能特性"></a> ✨ 功能特性

- [x] **C/OC 字符串混淆 (String Obfuscation)**：
  - **动态流加密**：编译期生成随机种子并对标记字符串进行流加密，连同 `\0` 一并混淆，彻底免疫字频分析。
  - **局部使用时解密 (Just-in-Time Decryption)**：摒弃了传统的启动时全局解密，转而在每个字符串的调用处利用栈（Stack）动态分配并解密。实现“阅后即焚”，彻底防御全量内存 Dump 并发竞争，且实现零“脏内存”增长。
  - **常量特征混淆**：解密循环中的密钥与长度均通过数学拆解隐藏，避免逆向工具提取解密逻辑。
  - 兼容 C 数组、C 指针及 Objective-C 的 `NSString`（内部支持 OC 字符串去重合并处理）。
- [x] **指令替换 (Instruction Substitution)**：
  - 拦截并替换 5 种基础二元运算（加法、减法、按位与、按位或、按位异或）。
  - **随机调度机制**：为每种运算提供多种等价的复杂数学位运算规则，每次编译随机选取，确保生成的二进制特征多态。
  - **多轮递归膨胀**：支持指定混淆深度（`COCOONS_SUB_LOOP`），对替换后生成的新指令进行二次或多次替换，令代码体积与理解成本呈指数级上升。
- [x] **控制流平坦化 (Control Flow Flattening)**：打乱原有的代码执行逻辑，将所有基本块放入一个巨大的状态机（`switch-case`）中循环执行，极大提高控制流图（CFG）的复杂度。
  - 新增 **概率 + BB 数量阈值**（v1.2.0）：`fla_probability ∈ [0,1]`、`fla_min_bbs`、`fla_max_bbs`，避免对 1000+ BB 的巨型函数或 2- BB 的 getter 做无意义 flatten。
- [x] **反调试 Anti-Debugging (v1.2.0 新增)**：在编译期直接向目标 Mach-O 注入 IR 级检查：
  - `ptrace(PTRACE_DENY_ATTACH=31, 0, 0, 0)`（原生拒绝 attach）
  - `sysctl KERN_PROC_PID → p_flag & P_TRACED(0x800)`（系统级 tracer 检查）
  - `_dyld_image_count() / _dyld_get_image_name(i)` 扫描加载的动态库名中是否有 `frida-agent / cycript / substitute`（拦截常见动态注入）
  - `SecTaskCopyValueForEntitlement("com.apple.security.get-task-allow")`（检查调试签名权限）
  - 注入点：**`llvm.global.ctors` (priority=0，比 `+load` 更早)** + **`main()` 首指令前置调用**（双保险）

---

## <a id="环境与安装"></a> 🛠 环境与安装

> 📌 **自 v1.1.0 起，Cocoons 采用全新的 Out-of-Tree Pass Plugin 架构**，不再需要 Fork LLVM。
> 如果您仍使用旧的 `build.sh` 全量编译 Toolchain 方式，请参考 [Legacy 安装说明](#legacy-toolchain)。

### 1. 下载源代码

```shell
$ git clone https://github.com/sweetloser/Cocoons.git
$ cd Cocoons
```

### 2. 准备 LLVM SDK（仅一次）

推荐通过 Homebrew 安装官方 LLVM 21（与 Cocoons 基线一致）：

```shell
# macOS (推荐)
$ brew install llvm@21

# 或：使用已有的 LLVM（包括自行编译出来的 LLVM 21）
# export LLVM_DIR=/path/to/llvm/lib/cmake/llvm
```

验证 LLVM 可用：
```shell
$ $(brew --prefix llvm@21)/bin/clang --version  # 期望 >= 21.0
```

### 3. 编译 Cocoons Plugin

```shell
$ cd CocoonsPlugin
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_DIR="$(brew --prefix llvm@21)/lib/cmake/llvm"
$ cmake --build build -j
```

编译完成后，产物会自动输出到 `CocoonsPlugin/dist/`：
```
CocoonsPlugin/dist/
├── cocoons-clang                ← 包装脚本，传 env var 而不是 -mllvm
└── libCocoonsPlugin.dylib       ← 核心插件 (~240KB)
```

### 4. 一键自检（推荐）

```shell
$ ./CocoonsPlugin/tests/run_tests.sh
# 自动完成：插件符号检查 → opt cocoons-full 调用 → 字符串运行时解密 → 指令密度对比
# 全部 PASS 即代表环境就绪

$ ./CocoonsPlugin/demo/build.sh
# 额外 4 gate demo：Str/Sub/Fla + 运行时算法输出一致性，期望 4 PASS / 4 TOTAL
```

---

## <a id="配置系统"></a> 🎚️ 配置系统（v1.2.0 起）

Cocoons 采用 **3 层优先级配置**（从上到下优先级依次降低）：

```
┌───────────────────────────────────────────────────────────────┐
│ P1. 源码注解 __attribute__((annotate("cocoons:*")))            │
│      - 白名单: cocoons:str / cocoons:sub / cocoons:fla /       │
│                 cocoons:anti_debug / annotate("obfuscate")     │
│      - 黑名单: cocoons:no   (强制所有 Pass 跳过本目标)          │
├───────────────────────────────────────────────────────────────┤
│ P2. 环境变量 COCOONS_* (在调用 clang/opt 之前 export)          │
│     - cocoons-clang 包装脚本会把 --enable-str 等翻译为 env var  │
│     - Xcode: 通过 Build Phases 前置 Run Script export         │
│     - Bazel: --action_env=COCOONS_* 或 COCOONS_CONFIG          │
├───────────────────────────────────────────────────────────────┤
│ P3. JSON 配置文件 `.cocoons.json`                              │
│     - 搜索顺序：$COCOONS_CONFIG → $(pwd)/.cocoons.json →       │
│                 向上遍历父目录直到文件系统根（自动定位）        │
└───────────────────────────────────────────────────────────────┘
```

### 完整配置项速查表

| 配置项 | 环境变量 (P2) | JSON 键 (P3) | 类型/可选值 | 默认值 | 说明 |
| :--- | :--- | :--- | :--- | :---: | :--- |
| 字符串加密 | `COCOONS_ENABLE_STR` | `enable_str` | `0/1/on/off/annotation` | **on** (v1.1.x 兼容) | `annotation`= 只处理带 `annotate("obfuscate")` 或 `cocoons:str` 的全局变量 |
| 指令替换 | `COCOONS_ENABLE_SUB` | `enable_sub` | `0/1/on/off/annotation` | **on** | `annotation`= 只处理带 `cocoons:sub` 注解的函数 |
| Sub 深度 | `COCOONS_SUB_LOOP` | `sub_loop` | 整数 `[1, 5]` | `1` | Release 建议 2~3，值 >5 体积与时间成本会陡增 |
| 控制流平坦化 | `COCOONS_ENABLE_FLA` | `enable_fla` | `0/1/on/off/annotation` | **on** | `annotation`= 只处理带 `cocoons:fla` 注解的函数 |
| Fla 概率 | `COCOONS_FLA_PROBABILITY` | `fla_probability` | 小数 `[0.0, 1.0]` | `1.0` | 0=完全不开；0.7=每个候选函数以 70% 概率 flatten |
| Fla 最小 BB | `COCOONS_FLA_MIN_BBS` | `fla_min_bbs` | 正整数 | `2` | 低于此 BB 数的函数（如 getter/setter）直接跳过，避免负收益 |
| Fla 最大 BB | `COCOONS_FLA_MAX_BBS` | `fla_max_bbs` | 正整数 | `4096` | 超过此 BB 数的巨型函数跳过，避免 OOM / 编译挂起 |
| 反调试 | `COCOONS_ENABLE_ANTI_DEBUG` | `enable_anti_debug` | `0/1/on/off/annotation` | **off** | 默认关防止影响 Debug 构建 / App Store 审核；开 = 注入 ptrace+sysctl+dyld+entitlement 4 重检查 |
| Anti Ptrace | `COCOONS_ANTI_PTRACE` | `anti_ptrace` | `0/1` | `1` | 精细开关，关闭则不注入 PTRACE_DENY_ATTACH |
| Anti Sysctl | `COCOONS_ANTI_SYSCTL` | `anti_sysctl` | `0/1` | `1` | 关闭则不检查 P_TRACED flag |
| Anti Dyld Hook | `COCOONS_ANTI_DYLD_HOOK` | `anti_dyld_hook` | `0/1` | `1` | 关闭则不扫 frida-agent 等注入模块名 |
| Anti Entitlement | `COCOONS_ANTI_ENTITLEMENT` | `anti_entitlement` | `0/1` | `1` | 关闭则不检查 get-task-allow |
| 跳过文件列表 | `COCOONS_SKIP_FILES`（暂不支持 env 数组，走 JSON） | `skip_files` | `string[]` (glob) | `[]` | 例: `["Pods/**/*.m", "**/vendor/**/*"]`，支持 `** / * / ?` |
| 跳过函数名列表 | —（同上，走 JSON） | `skip_functions` | `string[]` (glob) | `[]` | 例: `["*_debug_*", "-[NSObject(XX) *]", "objc_msgSend*"]` |

示例文件：仓库根 [`.cocoons.example.json`](file:///Users/sion/Documents/github/Cocoons/.cocoons.example.json)，直接 `cp .cocoons.example.json .cocoons.json` 再按项目情况修改即可。

---

## <a id="使用指南"></a> 🚀 使用指南

### Pass 功能总览（v1.2.0 起不再使用 `-mllvm`）

> ⚠️ **重要**：从 v1.2.0 开始，所有 `-mllvm -cocoons-*` 旗子全部废弃（会因为 cl::opt 在 `-fpass-plugin` 后注册导致 `Unknown command line argument`）。请使用**环境变量**或**`.cocoons.json`**，下表为每一个 Pass 的三种触发方式速查：

| 混淆功能 | 环境变量 | JSON 键 | 注解（白名单，优先级最高） |
| :--- | :--- | :--- | :--- |
| 🔐 字符串加密 | `COCOONS_ENABLE_STR=1` | `enable_str: true` | `__attribute__((annotate("obfuscate")))` 或 `annotate("cocoons:str")` |
| 🔄 指令替换 | `COCOONS_ENABLE_SUB=1` `COCOONS_SUB_LOOP=N` | `enable_sub: true` `sub_loop: N` | `annotate("cocoons:sub")` |
| 🔀 控制流平坦化 | `COCOONS_ENABLE_FLA=1` `COCOONS_FLA_PROBABILITY` `COCOONS_FLA_MIN_BBS` `COCOONS_FLA_MAX_BBS` | `enable_fla: true` + `fla_*` 三项 | `annotate("cocoons:fla")` |
| 🛡️ 反调试 (新) | `COCOONS_ENABLE_ANTI_DEBUG=1` 及 4 个分项 `ANTI_PTRACE/SYSCTL/DYLD_HOOK/ENTITLEMENT` | `enable_anti_debug: true` 及 4 个 `anti_*` 分项 | `annotate("cocoons:anti_debug")` 白名单注解（仅在 Annotation 模式下生效）|
| 🚫 强制跳过（黑名单） | — (不适用，逐符号只能靠注解) | — (不适用，逐符号只能靠注解) | **`__attribute__((annotate("cocoons:no")))`**，贴在函数或全局变量上，所有 Pass 100% 跳过，无论全局开关。 |

#### 命令行快速用法（研发本地）

```shell
# 方式一：直接用环境变量 + clang -fpass-plugin
export PATH="$(brew --prefix llvm@21)/bin:$PATH"
export COCOONS_ENABLE_STR=1 COCOONS_ENABLE_SUB=1 COCOONS_SUB_LOOP=2 \
       COCOONS_ENABLE_FLA=1 COCOONS_FLA_PROBABILITY=1.0
clang -fpass-plugin=./CocoonsPlugin/dist/libCocoonsPlugin.dylib -O2 -c main.m -o main.o

# 方式二：用提供的 cocoons-clang 包装器（把 flag 翻译成 env var，避免写错）
./CocoonsPlugin/dist/cocoons-clang --enable-all --sub-loop=2 --verbose -O2 main.m -o main
```

---

### <a id="xcode-集成"></a> 1. Xcode 集成

> 🧠 **原理**：Xcode `.xcconfig` 中的 `KEY = VALUE` **不是**环境变量，clang 子进程读不到。必须借助 Build Phases 中「Compile Sources」*之前*的 Run Script，在脚本里 `export COCOONS_*`；这样 xcodebuild 会把脚本 export 的变量透传给后续 clang 调用。  
> 不想写脚本的团队：直接把 `.cocoons.json` 放 `$(SRCROOT)`（项目根目录），Config 单例会自动向上找，零脚本也能工作。

#### ✅ 方式一：xcconfig + 前置 Run Script（推荐，可 Debug/Release 差异化）

1. 把 [cocoons.xcconfig](file:///Users/sion/Documents/github/Cocoons/CocoonsPlugin/tools/xcode-integration/cocoons.xcconfig) 拖进工程，选中 target，让它成为 `Release` 的 Base Configuration。文件内部只做一件事：`OTHER_CFLAGS = ... -fpass-plugin=...`（**没有任何 -mllvm**）。
2. 在 Target → Build Phases → 点 `+` → `New Run Script Phase`，把它**拖到 Compile Sources 上面**，命名为 `[Cocoons] Export Environment`，脚本内容：

```bash
# Release 全开 / Debug 全关（按需改）
if [ "$CONFIGURATION" = "Release" ]; then
  export COCOONS_ENABLE_STR=1
  export COCOONS_ENABLE_SUB=1
  export COCOONS_SUB_LOOP=2
  export COCOONS_ENABLE_FLA=1
  export COCOONS_FLA_PROBABILITY=0.9
  export COCOONS_FLA_MIN_BBS=3
  export COCOONS_FLA_MAX_BBS=200
  # export COCOONS_ENABLE_ANTI_DEBUG=1   # 上架前再考虑
fi
```

> 如果团队共享同一套配置，**推荐删除上面整个脚本，改用 `.cocoons.json`**：把 [`.cocoons.example.json`](file:///Users/sion/Documents/github/Cocoons/.cocoons.example.json) 拷贝到 `$(SRCROOT)/.cocoons.json`（和 `.xcodeproj` 同目录），脚本全删掉也 OK。

#### ✅ 方式二：CocoaPods Hook（多 Pod 工程）

复制仓库根 [Podfile.cocoons.example.rb](file:///Users/sion/Documents/github/Cocoons/CocoonsPlugin/tools/xcode-integration/Podfile.cocoons.example.rb) 末尾的 `post_install` 块到你项目的 Podfile 里。它会自动做三件事：

1. 在 `Pods/Target Support Files/Pods-<Target>/Cocoons-Environment.xcconfig` 里生成一份 shell 格式的 `export COCOONS_*=...` 列表（开关都写在 `COCOONS_SWITCHES` 字典里，一眼可改）。
2. 给每一个 Pod 子 Pod target 在 Release 配置里注入 `OTHER_CFLAGS += -fpass-plugin=<path>`，Swift 混编工程同时注入 `-Xcc <same>`。
3. 在每一个 target 的 Build Phases **最顶端**插入一个 `[Cocoons] Export Environment` 脚本，内容是 grep 上面生成的 xcconfig 然后 source 进去 export。

```ruby
# 用法: 在你 Podfile 尾部追加
require_relative 'CocoonsPlugin/tools/xcode-integration/Podfile.cocoons.example.rb'
```

---

### <a id="bazel-集成"></a> 2. Bazel 集成

完整文档见 [tools/bazel-integration/README.md](file:///Users/sion/Documents/github/Cocoons/CocoonsPlugin/tools/bazel-integration/README.md)，两种接入模式：

#### 模式 A：全局 `--action_env + copt`（一次配置，所有 rules_cc / rules_apple 生效）

```bzl
# WORKSPACE 或 MODULE.bazel (示意):
local_repository(
    name = "cocoons_plugin",
    path = "third_party/cocoons/CocoonsPlugin",   # 你放 Cocoons 的路径
)

# 然后在 .bazelrc 里:
build --action_env=COCOONS_ENABLE_STR=1
build --action_env=COCOONS_ENABLE_SUB=1
build --action_env=COCOONS_SUB_LOOP=2
build --action_env=COCOONS_ENABLE_FLA=1
build --action_env=COCOONS_FLA_PROBABILITY=0.9
build --action_env=COCOONS_FLA_MIN_BBS=3
build --action_env=COCOONS_FLA_MAX_BBS=200
# build --action_env=COCOONS_CONFIG=/path/to/team-shared/.cocoons.json
build --copt=-fpass-plugin=external/cocoons_plugin/dist/libCocoonsPlugin.dylib
build --per_file_copt=.*\\.mm$@-fpass-plugin=external/cocoons_plugin/dist/libCocoonsPlugin.dylib
```

#### 模式 B：单规则局部注入（推荐使用封装好的 `cocoons_objc_library` 宏）

```python
load("@cocoons_plugin//tools:bazel-integration/cocoons_copts.bzl", "cocoons_objc_library")

cocoons_objc_library(
    name = "MySecretLib",
    srcs = ["Secret.m", "Utils.m"],
    hdrs = ["Secret.h"],

    # Cocoons 开关（不传则走 BUILD 外面的 COCOONS_CONFIG 环境变量）
    cocoons_enable_str = True,
    cocoons_enable_sub = True,
    cocoons_sub_loop = 2,
    cocoons_enable_fla = "annotation",   # 只 flatten 带 cocoons:fla 注解的函数
    cocoons_fla_probability = 0.9,
    cocoons_config_file = "//:.cocoons.json",
)
```

宏实现：[cocoons_copts.bzl](file:///Users/sion/Documents/github/Cocoons/CocoonsPlugin/tools/bazel-integration/cocoons_copts.bzl)；本地构建插件（不用预构建）的 `genrule` 示例见 [BUILD.cocoons.bazel](file:///Users/sion/Documents/github/Cocoons/CocoonsPlugin/tools/bazel-integration/BUILD.cocoons.bazel)。

---

### <a id="代码标记注解粒度黑白名单v120-新增"></a> 3. 代码标记（注解粒度黑白名单，v1.2.0 新增）

#### 🔐 字符串加密

在全局变量上加 `annotate("obfuscate")` 或 v1.2.0 新增的 `annotate("cocoons:str")` 均可（两者等价）：

```objc
__attribute__((annotate("obfuscate")))   const char   kCStrArray[]  = "C Array Hello World!";
__attribute__((annotate("cocoons:str"))) const char  *kCStrPtr      = "C Pointer Hello World!";
__attribute__((annotate("obfuscate")))   NSString    *kOCStr        = @"OC Hello World!";
```

#### 🔄 / 🔀 Sub 与 Flatten（白名单 + 黑名单）

```objc
// 黑名单：加 cocoons:no，无论全局开关如何，这个函数所有 Pass 都不碰
__attribute__((annotate("cocoons:no")))
int hot_path_compute(int a, int b) { return (a + b) ^ (a - b); }

// 白名单（在 COCOONS_ENABLE_SUB=annotation 时才生效）：这个函数强制 Sub + Fla
__attribute__((annotate("cocoons:sub")))
__attribute__((annotate("cocoons:fla")))
int login_verify(const char *token) { /* ... */ }
```

#### 🛡️ 反调试白名单（Annotation 模式下用）

反调试一般是全局开关（`COCOONS_ENABLE_ANTI_DEBUG=1`）。如果你不想开全局、只想在某个入口函数里自己调 `cocoons_anti_debug_do_checks()`，可以加注解 + annotation 模式单独拉起来，但**通常不建议**（反调试双保险是 global ctor + main prepend，只在单一函数里调用容易被攻击者 NOP 掉）。

---

## <a id="更新日志"></a> 📝 更新日志

<details open>
  <summary><strong>最近更新</strong></summary>

| 版本  | 发布时间 | LLVM 版本 | 更新内容 |
| :---: | :---: | :---: | :--- |
| **1.2.0** | **2026-08-27** | 21.1.8 | ① 引入 3 层优先级配置系统（注解 > 环境变量 > `.cocoons.json`），废弃 `-mllvm -cocoons-*`； ② 修复 `cocoons-full` 命名管线 **遗漏 StringObfuscationPass** 的 bug（旧代码错误地把 ModulePass 注册在 FunctionPM callback）； ③ Flattening 新增概率 + BB 数阈值参数（`fla_probability / fla_min_bbs / fla_max_bbs`），避免死函数/巨型函数 OOM； ④ 新增 **Anti-Debugging Pass**（ptrace / sysctl / dyld 模块扫描 / SecTask entitlement 4 重检查，ctor + main 首指令双注入）； ⑤ 新增 `skip_files / skip_functions` glob 黑名单； ⑥ 提供 cocoons-clang 包装脚本（`--enable-all` 等直接映射到 env var）； ⑦ 提供 Xcode xcconfig / CocoaPods Hook / Bazel (`cocoons_objc_library`) 三种完整接入示例。 |
| 1.1.0 | 2026-07-01 | 21.1.8 | 架构重写：从 Fork LLVM 全量仓库迁移为 **Out-of-Tree Pass Plugin**，repo 从 1.5GB → ~300KB。 |
| 1.0.1 | 2026-02-10 | 21.1.8 | 修复字符串混淆在多 module 下的 bug。 |
| 1.0.0 | 2026-02-09 | 21.1.8 | 首版：C/OC 字符串混淆 + 指令替换 + 控制流平坦化 3 Pass 初版。 |

</details>

完整 Release Note（含升级注意事项）请见 [Documentation/RELEASE_NOTE_CN.md](Documentation/RELEASE_NOTE_CN.md)。

---

## <a id="演示效果"></a> 📸 演示效果

> 可本地执行 `./CocoonsPlugin/demo/build.sh` 4 步自检复现以下效果。

### 字符串混淆
| 混淆前 (IDA 视角) | 混淆后 (IDA 视角) |
| :---: | :---: |
| <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-StringObfuscation-Before.png?raw=true" width="400"> | <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-StringObfuscation-After.png?raw=true" width="400"> |

### 指令替换
| 混淆前 (CFG/伪代码) | 混淆后 (CFG/伪代码) |
| :---: | :---: |
| <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-Substitution-Before.png?raw=true" width="400"> | <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-Substitution-After.png?raw=true" width="400"> |

### 控制流平坦化（BB 数膨胀）
```
flattenDispatch BB BEFORE = 11
flattenDispatch BB AFTER  = 17    (demo / COCOONS_FLA_PROBABILITY=1.0)
```

### Anti-Debug（符号 & constructor 可见性，v1.2.0）
```bash
$ nm -a build/after | grep cocoons_anti
0000000100003a4c t _cocoons_anti_debug_do_checks

$ llvm-readobj --ctors build/after | grep 3a4c
    Function: _cocoons_anti_debug_do_checks (0x100003A4C)
```

---

## <a id="支持"></a> ❤️ 支持与贡献

如果您觉得 Cocoons 对您有帮助，欢迎点击右上角给一个 **★ Star**！

[🔝 回到顶部](#readme)
