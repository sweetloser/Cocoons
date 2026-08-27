# ============================================================
# Cocoons Bazel integration (rules_cc / rules_apple + apple_common)
# ------------------------------------------------------------
# 这份目录里的 .bzl / BUILD 片段，展示如何把 CocoonsPlugin.dylib 作为
# cc_common.create_compile_action 的 -fpass-plugin，
# 或通过 `objc_library` copts + xcode_env 接入。
#
# 两种模式：
#   A. 全项目 bazel 构建（推荐）—— 在 cc_toolchain / features 里
#      全局注入插件，所有 cc_* / objc_* 规则自动生效。
#   B. 单个 objc_library / cc_binary 局部开启 —— 通过
#      copts = ["-fpass-plugin=$(location :CocoonsPlugin.dylib)"]。
#
# 注意：这些只是示例片段，Bazel 版本差异大，请按你的
# rules_apple / rules_cc 版本调整。
# ============================================================
