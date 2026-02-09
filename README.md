## 作茧，不是自缚，每一根丝的加持，都是为了保护！


## 目录
* [功能](#功能)
* [要求](#要求)
* [安装](#安装)
* [更新记录](#更新记录)
* [演示效果](#演示效果)
* [支持❤️](#支持❤️) 

## <a id="功能"></a> 功能

- [x] C/OC字符串混淆
- [ ] 指令替换
- [ ] 控制流平坦化
- [ ] 反调试

## <a id="安装"></a> 安装

###  下载源代码

```shell
$ git clone https://github.com/sweetloser/Cocoons.git
```

### 编译&安装

```shell
$ cd Cocoons
$ ./build.sh
```

## <a id="使用"></a> 使用

1. ### 选择编译后的xctoolschain

重启Xcode，点击 Xcode -> Toolchains -> org.llvm.21.1.8。

2. ### 加入编译参数

点击 Build Settings -> Other C Flags，输入"-mllvm"和"-enable-str-obf"。

3. ### 在需要混淆的字符串前加上`__attribute__((annotate("obfuscate")))`：

```objective-c
__attribute__((annotate("obfuscate"))) const char c_const_array[] = "Hello World!";
__attribute__((annotate("obfuscate"))) const char *c_const_tring = "Hello World!";
__attribute__((annotate("obfuscate"))) NSString *ocConstString  = @"Hello World!";
```

4. ### 正常编译即可；

## <a id="更新记录"></a> 更新日志

<details open id="最近更新">
  <summary><strong>最近更新</strong></summary>

| 版本  | 发布时间   | LLVM   |
| ----- | ---------- | ------ |
| 1.0.0 | 2026-02-09 | 21.1.8 |

</details>

<details id="历史记录">
  <summary><strong>历史记录</strong></summary>

| 版本                         | 发布时间   | LLVM   |
| ---------------------------- | ---------- | ------ |
| [v1.0.0](https://github.com) | 2026-02-09 | 21.1.8 |

</details>

## <a id="演示效果"></a> 演示效果

|                            混淆前                            |                            混淆后                            |
| :----------------------------------------------------------: | :----------------------------------------------------------: |
| <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-StringObfuscation-Before.png?raw=true"> | <img src="https://github.com/sweetloser/PictureMaterial/blob/main/Cocoons/README/Effect-StringObfuscation-After.png?raw=true"> |



## <a id="支持❤️"></a> 支持❤️
* [**★ Star**](#)

[🔝回到顶部](#readme)
