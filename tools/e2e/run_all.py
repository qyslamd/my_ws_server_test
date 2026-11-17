"""端到端一键跑：发现每个产品的清单，逐个执行。

约定很简单：产品目录下有 e2e/suite.py，里面提供 run(ctx) 并返回
[(名字, 是否通过或 None, 秒数)]。本文件不认识任何具体产品——加产品
不用改这里。

用法：
  python -I tools/e2e/run_all.py                 全部产品、全部段
  python -I tools/e2e/run_all.py --only api,ui   只跑这些段
  python -I tools/e2e/run_all.py --delay 3       UI 步骤之间停 3 秒
  python -I tools/e2e/run_all.py --keep          CEF 客户端跑完不关

前置：已经 build 过（build/ninja-x64-release 里有 exe）。
"""

import argparse
import glob
import importlib.util
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build", "ninja-x64-release")


class Ctx:
    """交给各产品 suite.run() 的上下文。"""

    def __init__(self, only, step_delay, keep_client):
        self.root = ROOT
        self.build = BUILD
        self.python = sys.executable
        self.only = only
        self.step_delay = step_delay
        self.keep_client = keep_client


def load_suite(path):
    name = "e2e_suite_" + os.path.basename(os.path.dirname(os.path.dirname(path)))
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def discover():
    pattern = os.path.join(ROOT, "src", "apps", "*", "e2e", "suite.py")
    found = []
    for path in sorted(glob.glob(pattern)):
        try:
            found.append((path, load_suite(path)))
        except Exception as exc:
            print(f"跳过 {path}：{exc}")
    return found


def main():
    ap = argparse.ArgumentParser(description="端到端一键跑")
    ap.add_argument("--only", default="", help="逗号分隔的段名，如 api,ui,cef")
    ap.add_argument("--delay", type=float, default=1.5,
                    help="UI 步骤之间的停顿秒数，默认 1.5")
    ap.add_argument("--keep", action="store_true", help="CEF 跑完不关客户端")
    args = ap.parse_args()

    if not os.path.isdir(BUILD):
        print("找不到", BUILD, "，先 build")
        return 2

    suites = discover()
    if not suites:
        print("src/apps/*/e2e/suite.py 一个都没找到")
        return 2

    ctx = Ctx([x.strip() for x in args.only.split(",") if x.strip()],
              args.delay, args.keep)

    results = []
    for path, module in suites:
        title = getattr(module, "NAME", os.path.basename(os.path.dirname(
            os.path.dirname(path))))
        print(f"\n########## {title} ##########", flush=True)
        began = time.time()
        try:
            results.extend(module.run(ctx))
        except Exception as exc:
            print("  出错:", exc, flush=True)
            results.append((title, False, time.time() - began))

    print("\n===== 汇总 =====")
    failed = 0
    for name, ok, cost in results:
        mark = "跳过" if ok is None else ("通过" if ok else "失败")
        if ok is False:
            failed += 1
        print(f"  {name:<26} {mark}  {cost:.1f}s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
