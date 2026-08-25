#!/usr/bin/env python3
"""
cocoons-clang: clang / clang++ 包装器，自动注入 Cocoons Pass Plugin

用法:
    cocoons-clang [clang-args ...]
    cocoons-clang -cc1 ...
    cocoons-clang --enable-str --enable-sub --sub-loop=2 -- main.m -o out
"""

from __future__ import annotations

import os
import sys
import shutil
import subprocess
from pathlib import Path
from typing import List, Optional, Tuple

MY_PATH = Path(__file__).resolve()
PLUGIN_NAME = "libCocoonsPlugin.dylib" if sys.platform == "darwin" else "libCocoonsPlugin.so"


def _find_plugin_dir() -> Optional[Path]:
    """按顺序查找插件所在目录"""
    candidates: List[Path] = [
        MY_PATH.parent,
        MY_PATH.parent.parent / "dist",
    ]
    # 允许通过环境变量覆盖
    env_dir = os.environ.get("COCOONS_PLUGIN_DIR")
    if env_dir:
        candidates.insert(0, Path(env_dir).expanduser().resolve())
    for d in candidates:
        if (d / PLUGIN_NAME).exists():
            return d
    return None


def _find_clang() -> Tuple[str, str]:
    """查找 clang / clang++ 可执行文件。
    优先顺序: COCOONS_CLANG / LLVM_DIR / PATH 中的 clang
    """
    if "COCOONS_CLANG" in os.environ:
        clang = os.environ["COCOONS_CLANG"]
        return (clang, clang.replace("clang", "clang++"))

    llvm_dir = os.environ.get("LLVM_DIR", "")
    for base in [
        Path(llvm_dir).parent.parent.parent / "bin" if llvm_dir else None,
        Path.home() / "Library/Developer/Toolchains/llvm-21.1.8.xctoolchain/usr/bin",
        Path("/usr/local/opt/llvm@21/bin"),
        Path("/usr/local/opt/llvm/bin"),
        None,  # fallback to PATH
    ]:
        if base is None:
            c = shutil.which("clang")
            cp = shutil.which("clang++")
        else:
            c = str(base / "clang") if (base / "clang").exists() else None
            cp = str(base / "clang++") if (base / "clang++").exists() else None
        if c and cp:
            return (c, cp)
    return ("clang", "clang++")


def _parse_extra_flags(argv: List[str]) -> Tuple[List[str], List[str]]:
    """识别 --enable-str 等简化参数，剥离并返回 (cocoons_flags, remain_argv)"""
    cocoons_flags: List[str] = []
    remain: List[str] = []
    i = 0
    while i < len(argv):
        a = argv[i]
        consumed = True
        if a == "--enable-str":
            cocoons_flags += ["-mllvm", "-cocoons-enable-str"]
        elif a == "--enable-sub":
            cocoons_flags += ["-mllvm", "-cocoons-enable-sub"]
        elif a.startswith("--sub-loop="):
            n = a.split("=", 1)[1]
            cocoons_flags += ["-mllvm", f"-cocoons-sub-loop={n}"]
        elif a == "--enable-fla":
            cocoons_flags += ["-mllvm", "-cocoons-enable-fla"]
        elif a == "--enable-all":
            cocoons_flags += [
                "-mllvm", "-cocoons-enable-str",
                "-mllvm", "-cocoons-enable-sub",
                "-mllvm", "-cocoons-enable-fla",
            ]
        elif a == "--":
            remain.extend(argv[i + 1 :])
            break
        else:
            consumed = False
            remain.append(a)
        if consumed:
            i += 1
        else:
            i += 1
    return cocoons_flags, remain


def main() -> int:
    invoked_as = Path(sys.argv[0]).name
    argv = sys.argv[1:]

    cocoons_flags, argv = _parse_extra_flags(argv)

    plugin_dir = _find_plugin_dir()
    if plugin_dir is None:
        print(
            f"[cocoons-clang] ERROR: 找不到 {PLUGIN_NAME}，请通过 COCOONS_PLUGIN_DIR 环境变量指定目录",
            file=sys.stderr,
        )
        return 127

    plugin_path = plugin_dir / PLUGIN_NAME

    clang_bin, clangxx_bin = _find_clang()
    driver = clangxx_bin if ("++" in invoked_as) else clang_bin

    cmd = [driver]
    cmd.append(f"-fpass-plugin={plugin_path}")
    cmd.extend(cocoons_flags)
    cmd.extend(argv)

    if os.environ.get("COCOONS_VERBOSE", "0") == "1":
        print(f"[cocoons-clang] EXEC: {' '.join(cmd)}", file=sys.stderr)

    return subprocess.call(cmd, env=os.environ)


if __name__ == "__main__":
    sys.exit(main())
