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
    <div align="center"> 基于 LLVM 的 iOS / macOS 代码混淆工具链</div>
</p>

---

## 📖 目录
* [项目简介](#项目简介)
* [功能特性](#功能特性)
* [环境与安装](#环境与安装)
* [使用指南](#使用指南)
  * [Xcode 配置](#xcode-配置)
  * [功能使用详解](#功能使用详解)
* [更新日志](#更新日志)
* [演示效果](#演示效果)
* [支持与贡献](#支持) 

---

## <a id="项目简介"></a> 💡 项目简介

**Cocoons** 是一个基于 **LLVM Pass Plugin（Out-of-Tree）架构** 的 iOS / macOS 代码混淆工具链（兼容 LLVM `20.x` / `21.x` / `22.x`，推荐 `21.1.8`）。通过在编译期的 IR 层对代码进行变换，有效增加逆向工程（静态分析、动态调试）的难度，从而保护 iOS / macOS 应用程序的核心逻辑和敏感数据。

> 🔧 **架构亮点**：采用 Out-of-Tree Pass Plugin 模式，**零侵入上游 LLVM 源码**，支持 Homebrew 安装的官方 LLVM，单独编译、单独发布、秒级调试。

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
  - **多轮递归膨胀**：支持指定混淆深度，对替换后生成的新指令进行二次或多次替换，令代码体积与理解成本呈指数级上升。
- [x] **控制流平坦化 (Control Flow Flattening)**：打乱原有的代码执行逻辑，将所有基本块放入一个巨大的状态机（`switch-case`）中循环执行，极大提高控制流图（CFG）的复杂度。（已在源码中实现）
- [ ] **反调试 (Anti-Debugging)**：计划中。

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

# 或：使用已有的 LLVM（包括项目内 llvm/ 子树自行编译出来的）
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
└── libCocoonsPlugin.dylib   ← 核心插件 (~8MB)
```

### 4. 一键自检（推荐）

```shell
$ ./CocoonsPlugin/tests/run_tests.sh
# 自动完成：插件编译 → 符号检查 → opt 调用 → 二进制字符串检查 → 指令密度对比
# 全部 PASS 即代表环境就绪
```

---

## <a id="使用指南"></a> 🚀 使用指南

### Pass 功能总览（命令行参数保持一致）

| 混淆功能 | 参数 | 默认值 | 说明 |
| :--- | :--- | :---: | :--- |
| 🔐 字符串加密 | `-mllvm -cocoons-enable-str` | 关闭 | 注解驱动，需标记 `__attribute__((annotate("obfuscate")))` |
| 🔄 指令替换 | `-mllvm -cocoons-enable-sub` | 关闭 | 拦截 Add/Sub/And/Or/Xor 五种二元运算 |
| 指令替换深度 | `-mllvm -cocoons-sub-loop=N` | `1` | 多轮递归膨胀，建议 Release 下 2~3 |
| 🔀 控制流平坦化 | `-mllvm -cocoons-enable-fla` | 关闭 | 将函数 CFG 改造成 switch-case 状态机 |

### 1. Xcode 接入（三种方式任选其一）

#### ✅ 方式一：Other C Flags 注入（最轻量，推荐）

**Step 1**：Build Settings → `+` → `Add User-Defined Setting`：
```
COCOONS_PLUGIN_DIR = $(SRCROOT)/CocoonsPlugin/dist
```

**Step 2**：Build Settings → **Other C Flags** → 追加：
```
-fpass-plugin=$(COCOONS_PLUGIN_DIR)/libCocoonsPlugin.dylib
-mllvm -cocoons-enable-str
-mllvm -cocoons-enable-sub
-mllvm -cocoons-sub-loop=2
-mllvm -cocoons-enable-fla
```

> 💡 **Debug/Release 差异化**：推荐只在 Release 开启 Sub/Fla 以加速日常编译。可使用 `xcconfig` 方案。

#### ✅ 方式二：xcconfig 一键接入

参考 [CocoonsPlugin/tools/xcode-integration/cocoons.xcconfig](file:///Users/sion/Documents/github/Cocoons/CocoonsPlugin/tools/xcode-integration/cocoons.xcconfig)

```xcconfig
// 直接把该文件拖入 Xcode 项目 → Project/Target → Based on Configuration File 选择即可
COCOONS_PLUGIN_DIR = $(SRCROOT)/CocoonsPlugin/dist
COCOONS_PLUGIN_FLAGS_RELEASE = -fpass-plugin=$(COCOONS_PLUGIN_DIR)/libCocoonsPlugin.dylib -mllvm -cocoons-enable-str -mllvm -cocoons-enable-sub -mllvm -cocoons-sub-loop=2 -mllvm -cocoons-enable-fla
COCOONS_PLUGIN_FLAGS_DEBUG =
OTHER_CFLAGS = $(inherited) $(COCOONS_PLUGIN_FLAGS_$(CONFIGURATION))
OTHER_SWIFT_FLAGS = $(inherited) -Xcc "$(COCOONS_PLUGIN_FLAGS_$(CONFIGURATION))"
```

#### ✅ 方式三：CocoaPods Hook（多 Pod 工程推荐）

在项目 `Podfile` 末尾追加：
```ruby
COCOONS_PLUGIN_PATH = File.expand_path('../CocoonsPlugin/dist/libCocoonsPlugin.dylib', __dir__)
COCOONS_FLAGS = [
  "-fpass-plugin=#{COCOONS_PLUGIN_PATH}",
  '-mllvm', '-cocoons-enable-str',
  '-mllvm', '-cocoons-enable-sub',
  '-mllvm', '-cocoons-sub-loop=2',
  '-mllvm', '-cocoons-enable-fla',
].join(' ')

post_install do |installer|
  installer.pods_project.targets.each do |target|
    target.build_configurations.each do |config|
      next if config.name == 'Debug'
      cflags = config.build_settings['OTHER_CFLAGS'] || '$(inherited)'
      config.build_settings['OTHER_CFLAGS'] = "#{cflags} #{COCOONS_FLAGS}"
    end
  end
end
```
完整示例见 [Podfile.cocoons.example.rb](file:///Users/sion/Documents/github/Cocoons/CocoonsPlugin/tools/xcode-integration/Podfile.cocoons.example.rb)

---

### 2. 代码标记方式

#### 🔐 字符串加密 (`-cocoons-enable-str`)
Cocoons 采用**注解驱动**的混淆方式，只对您标记的字符串进行加密。在代码中使用 `__attribute__((annotate("obfuscate")))` 标记目标字符串：

```objective-c
// C 字符数组混淆
__attribute__((annotate("obfuscate"))) const char c_const_array[] = "C Array Hello World!";

// C 字符串指针混淆
__attribute__((annotate("obfuscate"))) const char *c_const_string = "C String Hello World!";

// Objective-C 字符串混淆
__attribute__((annotate("obfuscate"))) NSString *ocConstString  = @"OC String Hello World!";
```

#### 🔄 指令替换 / 🔀 控制流平坦化
这两个 Pass 无需在源码中做任何标记，**开启后自动对所有函数生效**。可使用：
```objc
// 如需要对某个函数跳过保护，加属性：
__attribute__((optnone)) void hot_path_do_not_obfuscate(void) { ... }
// 或使用 metadata（代码中自行添加 cocoons_protected 元数据可防止重复处理）
```

---

## <a id="更新日志"></a> 📝 更新日志

<details open id="最近更新">
  <summary><strong>最近更新</strong></summary>

| 版本  |  发布时间  |  LLVM 版本 | 更新内容 |
| :---: | :--------: | :----: | :--- |
| 1.0.1 | 2026-02-10 | 21.1.8 | 修复字符串混淆在多 module 下的 bug |

</details>

<details id="历史记录">
  <summary><strong>历史记录</strong></summary>

| 版本 | 发布时间 | LLVM 版本 | 更新内容 |
| :---: | :--------: | :----: | :--- |
| [v1.0.1](Documentation/RELEASE_NOTE_CN.md#101) | 2026-02-10 | 21.1.8 | 修复多模块 Bug |
| [v1.0.0](Documentation/RELEASE_NOTE_CN.md#100) | 2026-02-09 | 21.1.8 | 增加 C/OC 字符串混淆及加法指令替换 |

</details>

---

## <a id="演示效果"></a> 📸 演示效果

### 字符串混淆
| 混淆前 (IDA 视角) | 混淆后 (IDA 视角) |
| :---: | :---: |
| <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-StringObfuscation-Before.png?raw=true" width="400"> | <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-StringObfuscation-After.png?raw=true" width="400"> |

### 指令替换
| 混淆前 (CFG/伪代码) | 混淆后 (CFG/伪代码) |
| :---: | :---: |
| <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-Substitution-Before.png?raw=true" width="400"> | <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-Substitution-After.png?raw=true" width="400"> |

---

## <a id="支持"></a> ❤️ 支持与贡献

如果您觉得 Cocoons 对您有帮助，欢迎点击右上角给一个 **★ Star**！

[🔝 回到顶部](#readme)