# =====================================================================
# cocoons_copts.bzl — 一个小 helper，给 objc_library/cc_binary 生成
#                      -fpass-plugin=... + COCOONS_* 开关（action_env）
# =====================================================================

"""
Usage:

load("//cocoons-plugin:cocoons_copts.bzl", "cocoons_objc_library")

cocoons_objc_library(
    name = "MyAppLib",
    srcs = glob(["Sources/**/*.m"]),
    hdrs = glob(["Sources/**/*.h"]),

    # 可选: 覆盖默认开关（默认全开）
    cocoons_enable_str = True,
    cocoons_enable_sub = True,
    cocoons_sub_loop   = 2,
    cocoons_enable_fla = True,
    cocoons_enable_anti_debug = False,
    cocoons_config     = "//config:.cocoons.json",   # 可选, 若提供会 export COCOONS_CONFIG=$(location ...)
)
"""

load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@build_bazel_rules_apple//apple:objc.bzl", "objc_library")

def cocoons_env_dict(
    enable_str = True,
    enable_sub = True,
    sub_loop   = 1,
    enable_fla = True,
    enable_anti_debug = False,
    config_file = None,
):
    env = {
        "COCOONS_ENABLE_STR":       "1" if enable_str       else "0",
        "COCOONS_ENABLE_SUB":       "1" if enable_sub       else "0",
        "COCOONS_SUB_LOOP":         str(sub_loop),
        "COCOONS_ENABLE_FLA":       "1" if enable_fla       else "0",
        "COCOONS_ENABLE_ANTI_DEBUG":"1" if enable_anti_debug else "0",
    }
    if config_file != None:
        env["COCOONS_CONFIG"] = "$(location %s)" % config_file
    return env

def cocoons_objc_library(
    name,
    srcs = [],
    hdrs = [],
    deps = [],
    cocoons_enable_str = True,
    cocoons_enable_sub = True,
    cocoons_sub_loop   = 1,
    cocoons_enable_fla = True,
    cocoons_enable_anti_debug = False,
    cocoons_config     = None,
    **kwargs
):
    plugin_label = Label("@cocoons_plugin//:plugin_prebuilt")  # 或 :build_plugin_with_cmake
    env = cocoons_env_dict(
        enable_str = cocoons_enable_str,
        enable_sub = cocoons_enable_sub,
        sub_loop   = cocoons_sub_loop,
        enable_fla = cocoons_enable_fla,
        enable_anti_debug = cocoons_enable_anti_debug,
        config_file = cocoons_config,
    )
    data = [plugin_label]
    if cocoons_config != None:
        data.append(cocoons_config)

    # Bazel 没有原生 per-target env var, 所以要把 COCOONS_* 放进 copts
    # 通过 -Wl,--undefined=<sym> 也不行; 唯一可靠方式: 使用 genrule
    # 包装编译, 或在 rule 里拼: 我们把 env vars 拼成 bazel target-level
    # `--define COCOONS_*=x`; 用户在 .bazelrc 中把这些 define 透传给 cc
    # action env；见同目录 README。
    extra_copts = [
        "-fpass-plugin=$(location %s)" % plugin_label,
    ]
    objc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = deps,
        copts = kwargs.pop("copts", []) + extra_copts,
        data  = kwargs.pop("data",  []) + data,
        **kwargs
    )
    return name
