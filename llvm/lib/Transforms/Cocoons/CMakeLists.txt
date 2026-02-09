# 定义一个 LLVM 组件库
add_llvm_component_library(LLVMCocoons
  StringObfuscationPass.cpp
  SubstitutionPass.cpp

  # 声明对应的头文件目录
  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/Obfuscation

  # 依赖项：生成 TableGen 定义的头文件
  DEPENDS
  intrinsics_gen

  # 链接组件
  LINK_COMPONENTS
  Core            # 提供 BasicBlock, Function, Module 等基础 IR 对象
  Support         # 提供 cl::opt 命令行解析和 errs() 打印
  TransformUtils  # 提供 appendToGlobalCtors 等工具函数
)