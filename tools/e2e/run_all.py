"""端到端一键跑：API / 流控 / Edge UI / CEF 壳。

每一段自己起自己需要的东西，跑完收摊；任何一段失败不影响后面的，
最后给一张汇总表。

用法：
  python -I tools/e2e/run_all.py                 全部
  python -I tools/e2e/run_all.py --only api,fc   只跑指定的几段
  python -I tools/e2e/run_all.py --delay 3       UI 步骤之间停 3 秒
  python -I tools/e2e/run_all.py --keep          CEF 客户端跑完不关

前置：已经 build 过（build/ninja-x64-release 里有 exe）。CEF 那一段
还要求 EPH205.exe 存在，缺了会跳过而不是失败。
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build", "ninja-x64-release")
SERVER = os.path.join(BUILD, "eph205_server.exe")
CLIENT = os.path.join(BUILD, "EPH205.exe")
EDGEDRIVER = os.path.join(ROOT, "3rdparty", "edgedriver_win64", "msedgedriver.exe")
API_TEST = os.path.join(ROOT, "tools", "e2e", "api", "eph205_api_test.py")
FC_TEST = os.path.join(ROOT, "tools", "e2e", "api", "eph205_fc_test.py")
UI_TEST = os.path.join(ROOT, "tools", "e2e", "ui", "edge_webdriver", "eph205_e2e.py")
CEF_TEST = os.path.join(ROOT, "tools", "e2e", "ui", "cef_test", "eph205_client_check.py")

BASE = "http://127.0.0.1:8790"
DEVPORT = "http://127.0.0.1:9223"
CLIENT_API = "http://127.0.0.1:8791"
EDGE_DRIVER = "http://127.0.0.1:9515"

DELAY = 1.5


# ------------------------------------------------------------------ 小工具


def http_ok(url, timeout=2):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return resp.status < 500
    except urllib.error.HTTPError:
        return True  # 4xx 也说明在应答
    except Exception:
        return False


def wait_http(url, seconds=30, label=""):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if http_ok(url):
            return True
        time.sleep(0.3)
    raise RuntimeError(f"等不到 {label or url}")


def port_busy(port):
    with socket.socket() as s:
        s.settimeout(0.3)
        return s.connect_ex(("127.0.0.1", port)) == 0


def wait_port_free(port, seconds=20):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if not port_busy(port):
            return
        time.sleep(0.3)


def kill(proc):
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def kill_by_name(name):
    subprocess.run(["taskkill", "/F", "/IM", name],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run_py(script, env=None):
    """跑一个 e2e 脚本，输出直接透传。返回是否成功。"""
    print(f"  → {os.path.relpath(script, ROOT)}", flush=True)
    child_env = dict(os.environ)
    child_env.update(env or {})
    return subprocess.run([sys.executable, "-I", script],
                          cwd=ROOT, env=child_env).returncode == 0


# ------------------------------------------------------------------ 各段


def ensure_no_server():
    """确保 8790 上没有别人留下的实例，免得测到旧的。"""
    if port_busy(8790):
        print("  8790 已被占用，先关掉", flush=True)
        kill_by_name("eph205_server.exe")
        wait_port_free(8790)


class Server:
    """起一个带 --auth 的服务端，用完关掉。"""

    def __enter__(self):
        ensure_no_server()
        self.proc = subprocess.Popen([SERVER, "8790", "--auth"], cwd=BUILD)
        wait_http(BASE + "/api/v1/health", 20, "服务端")
        return self

    def __exit__(self, *_):
        kill(self.proc)
        wait_port_free(8790)


def wait_no_session(seconds=10):
    """上一段留下的会话要等它销毁，否则下一段建连会撞 409。"""
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            req = urllib.request.Request(BASE + "/api/v1/state")
            with urllib.request.urlopen(req, timeout=2) as resp:
                resp.read()
        except urllib.error.HTTPError as err:
            if err.code == 409:
                return
        except Exception:
            return
        time.sleep(0.3)


def suite_api():
    with Server():
        ok_api = run_py(API_TEST)
        wait_no_session()
        ok_fc = run_py(FC_TEST)
    return ok_api and ok_fc


def suite_ui():
    proc = None
    if not http_ok(EDGE_DRIVER + "/status"):
        if not os.path.exists(EDGEDRIVER):
            print("  没有 msedgedriver，跳过 UI", flush=True)
            return None
        proc = subprocess.Popen([EDGEDRIVER, "--port=9515"],
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        wait_http(EDGE_DRIVER + "/status", 20, "EdgeDriver")
    try:
        return run_py(UI_TEST, {"E2E_STEP_DELAY": str(DELAY)})
    finally:
        if proc is not None:
            kill(proc)


def suite_cef(keep):
    if not os.path.exists(CLIENT):
        print("  没有 EPH205.exe，跳过 CEF（要 build.bat x64 release client）",
              flush=True)
        return None
    if port_busy(9223) or port_busy(8791):
        print("  客户端还开着，先关掉（脚本要求从登录页开始）", flush=True)
        kill_by_name("EPH205.exe")
        wait_port_free(9223)
        wait_port_free(8791)
    proc = subprocess.Popen([CLIENT, "--devtools"], cwd=BUILD)
    try:
        wait_http(DEVPORT + "/json/version", 30, "客户端调试端口")
        wait_http(CLIENT_API + "/api/v1/health", 30, "客户端内服务端")
        return run_py(CEF_TEST)
    finally:
        if keep:
            print("  CEF 客户端保留运行（--keep）", flush=True)
        else:
            kill(proc)


SUITES = [
    ("api", "API 全流程 + 流控", suite_api),
    ("ui", "Edge 网页 UI", suite_ui),
    ("cef", "CEF 桌面壳", suite_cef),
]


def main():
    global DELAY
    ap = argparse.ArgumentParser(description="端到端一键跑")
    ap.add_argument("--only", default="",
                    help="逗号分隔：api / ui / cef")
    ap.add_argument("--delay", type=float, default=1.5,
                    help="UI 步骤之间的停顿秒数，默认 1.5")
    ap.add_argument("--keep", action="store_true", help="CEF 跑完不关客户端")
    args = ap.parse_args()
    DELAY = args.delay

    picked = [x.strip() for x in args.only.split(",") if x.strip()]
    if picked:
        unknown = [x for x in picked if x not in {n for n, _, _ in SUITES}]
        if unknown:
            print("未知的段:", ", ".join(unknown))
            return 2
        suites = [s for s in SUITES if s[0] in picked]
    else:
        suites = SUITES

    if not os.path.exists(SERVER):
        print("找不到", SERVER, "，先 build")
        return 2

    results = []
    for name, title, fn in suites:
        print(f"\n===== {title} =====", flush=True)
        began = time.time()
        try:
            ok = fn(args.keep) if name == "cef" else fn()
        except Exception as exc:  # 起不来也算失败，但别带崩别的段
            print("  出错:", exc, flush=True)
            ok = False
        results.append((name, title, ok, time.time() - began))

    print("\n===== 汇总 =====")
    failed = 0
    for name, title, ok, cost in results:
        mark = "跳过" if ok is None else ("通过" if ok else "失败")
        if ok is False:
            failed += 1
        print(f"  {title:<14} {mark}  {cost:.1f}s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
