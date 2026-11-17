"""EPH205 的端到端清单。

tools/e2e/run_all.py 会发现这个文件并调用 run(ctx)。
产品自己的端口、exe 名、脚本顺序都写在这里，tools 不需要认识 EPH205。

run(ctx) 返回 [(名字, 是否通过或 None, 秒数)]；None 表示跳过。
ctx 由 run_all 提供：root / build / python / step_delay。
"""

import os
import socket
import subprocess
import time
import urllib.error
import urllib.request

NAME = "EPH205"
SERVER_EXE = "eph205_server.exe"
CLIENT_EXE = "EPH205.exe"
PORT = 8790
BASE = f"http://127.0.0.1:{PORT}"
CLIENT_API = "http://127.0.0.1:8791"
DEVTOOLS = "http://127.0.0.1:9223"
EDGE_DRIVER = "http://127.0.0.1:9515"

HERE = os.path.dirname(os.path.abspath(__file__))


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


def run_py(ctx, name, env=None):
    script = os.path.join(HERE, name)
    print(f"  -> {name}", flush=True)
    child_env = dict(os.environ)
    child_env.update(env or {})
    # 脚本要按仓库根定位 temp/、build/，所以 cwd 用仓库根
    return subprocess.run([ctx.python, "-I", script],
                          cwd=ctx.root, env=child_env).returncode == 0


class Server:
    """起一个带 --auth 的服务端，用完关掉。"""

    def __init__(self, build_dir):
        self.exe = os.path.join(build_dir, SERVER_EXE)
        self.proc = None

    def __enter__(self):
        if port_busy(PORT):
            print(f"  {PORT} 已被占用，先关掉", flush=True)
            kill_by_name(SERVER_EXE)
            wait_port_free(PORT)
        self.proc = subprocess.Popen([self.exe, str(PORT), "--auth"],
                                     cwd=os.path.dirname(self.exe))
        wait_http(BASE + "/api/v1/health", 20, "服务端")
        return self

    def __exit__(self, *_):
        kill(self.proc)
        wait_port_free(PORT)


def wait_no_session(seconds=10):
    """上一段留下的会话要等它销毁，否则下一段建连会撞 409。"""
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(BASE + "/api/v1/state", timeout=2) as r:
                r.read()
        except urllib.error.HTTPError as err:
            if err.code == 409:
                return
        except Exception:
            return
        time.sleep(0.3)


# ------------------------------------------------------------------ 三段


def stage_api(ctx):
    with Server(ctx.build):
        ok_api = run_py(ctx, "api_test.py")
        wait_no_session()
        ok_fc = run_py(ctx, "fc_test.py")
    return ok_api and ok_fc


def stage_ui(ctx):
    driver = os.path.join(ctx.root, "3rdparty", "edgedriver_win64",
                          "msedgedriver.exe")
    proc = None
    if not http_ok(EDGE_DRIVER + "/status"):
        if not os.path.exists(driver):
            print("  没有 msedgedriver，跳过 Edge UI", flush=True)
            return None
        proc = subprocess.Popen([driver, "--port=9515"],
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        wait_http(EDGE_DRIVER + "/status", 20, "EdgeDriver")
    try:
        return run_py(ctx, "ui_edge.py",
                      {"E2E_STEP_DELAY": str(ctx.step_delay)})
    finally:
        if proc is not None:
            kill(proc)


def stage_cef(ctx):
    client = os.path.join(ctx.build, CLIENT_EXE)
    if not os.path.exists(client):
        print(f"  没有 {CLIENT_EXE}，跳过 CEF"
              "（要 build.bat x64 release client）", flush=True)
        return None
    if port_busy(9223) or port_busy(8791):
        print("  客户端还开着，先关掉（脚本要求从登录页开始）", flush=True)
        kill_by_name(CLIENT_EXE)
        wait_port_free(9223)
        wait_port_free(8791)
    proc = subprocess.Popen([client, "--devtools"],
                            cwd=os.path.dirname(client))
    try:
        wait_http(DEVTOOLS + "/json/version", 30, "客户端调试端口")
        wait_http(CLIENT_API + "/api/v1/health", 30, "客户端内服务端")
        return run_py(ctx, "ui_cef.py")
    finally:
        if ctx.keep_client:
            print("  CEF 客户端保留运行（--keep）", flush=True)
        else:
            kill(proc)


STAGES = [
    ("api", "API 全流程 + 流控", stage_api),
    ("ui", "Edge 网页 UI", stage_ui),
    ("cef", "CEF 桌面壳", stage_cef),
]


def run(ctx):
    results = []
    picked = ctx.only
    stages = [s for s in STAGES if not picked or s[0] in picked]
    for key, title, fn in stages:
        print(f"\n===== {NAME} / {title} =====", flush=True)
        began = time.time()
        try:
            ok = fn(ctx)
        except Exception as exc:  # 起不来也算失败，但别带崩别的产品
            print("  出错:", exc, flush=True)
            ok = False
        results.append((f"{NAME} {title}", ok, time.time() - began))
    return results
