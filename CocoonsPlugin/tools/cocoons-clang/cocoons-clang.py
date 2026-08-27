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
    """识别 --enable-str 等简化参数，剥离并注入 COCOONS_* 环境变量。"""
    remain: List[str] = []
    i = 0
    while i < len(argv):
        a = argv[i]
        consumed = True
        if a == "--enable-str":
            os.environ["COCOONS_ENABLE_STR"] = "1"
        elif a == "--enable-sub":
            os.environ["COCOONS_ENABLE_SUB"] = "1"
        elif a.startswith("--sub-loop="):
            n = a.split("=", 1)[1]
            os.environ["COCOONS_SUB_LOOP"] = n
        elif a == "--enable-fla":
            os.environ["COCOONS_ENABLE_FLA"] = "1"
        elif a == "--enable-all":
            os.environ["COCOONS_ENABLE_STR"] = "1"
            os.environ["COCOONS_ENABLE_SUB"] = "1"
            os.environ["COCOONS_ENABLE_FLA"] = "1"
        elif a.startswith("--config="):
            os.environ["COCOONS_CONFIG"] = a.split("=", 1)[1]
        elif a == "--disable-str":
            os.environ["COCOONS_ENABLE_STR"] = "0"
        elif a == "--disable-sub":
            os.environ["COCOONS_ENABLE_SUB"] = "0"
        elif a == "--disable-fla":
            os.environ["COCOONS_ENABLE_FLA"] = "0"
        elif a == "--verbose":
            os.environ["COCOONS_VERBOSE"] = "1"
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
    return [], remain


def main() -> int:
    invoked_as = Path(sys.argv[0]).name
    argv = sys.argv[1:]

    _, argv = _parse_extra_flags(argv)

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
    cmd.extend(argv)

    if os.environ.get("COCOONS_VERBOSE", "0") == "1":
        envs = [f"{k}={os.environ[k]}" for k in sorted(os.environ) if k.startswith("COCOONS_")]
        print(f"[cocoons-clang] ENV:  {' '.join(envs)}", file=sys.stderr)
        print(f"[cocoons-clang] EXEC: {' '.join(cmd)}", file=sys.stderr)

    return subprocess.call(cmd, env=os.environ)


if __name__ == "__main__":
    sys.exit(main())
