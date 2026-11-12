"""驱动 EPH205 客户端到几个状态，并抓「窗口级」截图（含系统标题栏）。

前置：build-client\\EPH205.exe --devtools 已运行（9223），ChromeDriver 在 9516。

抓图用 tools/capture_window.ps1（PrintWindow），所以窗口边框和标题栏都在。
"""

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")

DRIVER = "http://127.0.0.1:9516"
DEBUG_PORT = 9223
CHROMEDRIVER = r"D:\trae_workspace\qt5-cef5414.120-base-env\chromedriver_win32\chromedriver.exe"
ROOT = r"D:\trae_workspace\my_ws_server_test"
OUT = os.path.join(ROOT, "temp", "client")
CSV_PATH = os.path.join(ROOT, "temp", "eph205_items.csv")
CAPTURE = os.path.join(ROOT, "tools", "capture_window.ps1")

CSV = """序号,P,N,线制,量程,下限,上限,测量
1,1,2,二线,0,90,110,1
2,2,3,二线,0,90,110,1
3,3,4,二线,0,90,110,1
4,4,5,二线,0,90,110,1
5,5,6,四线,1,50,150,1
6,6,7,二线,0,500,,1
7,7,8,二线,0,90,110,0
"""


def http(method, url, data=None, timeout=60):
    body = json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(url, data=body, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as exc:
        raw = exc.read()
        raise RuntimeError(f"{exc.code} {raw.decode('utf-8', 'replace')[:300]}")
    return json.loads(raw) if raw else None


def wait_url(url, tries=80):
    for _ in range(tries):
        try:
            return http("GET", url, timeout=2)
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("等不到 " + url)


def main():
    os.makedirs(OUT, exist_ok=True)
    with open(CSV_PATH, "w", encoding="utf-8") as f:
        f.write(CSV)

    wait_url(f"http://127.0.0.1:{DEBUG_PORT}/json/version")
    try:
        wait_url(f"{DRIVER}/status", tries=3)
    except Exception:
        subprocess.Popen([CHROMEDRIVER, "--port=9516"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_url(f"{DRIVER}/status")

    session = http("POST", f"{DRIVER}/session", {
        "capabilities": {"alwaysMatch": {
            "goog:chromeOptions": {"debuggerAddress": f"127.0.0.1:{DEBUG_PORT}"}}}
    })
    sid = session["value"]["sessionId"]

    def call(method, path, data=None):
        return http(method, f"{DRIVER}/session/{sid}{path}", data)["value"]

    def js(script, args=None):
        return call("POST", "/execute/sync", {"script": script, "args": args or []})

    def text():
        return js("return document.body.innerText;") or ""

    def wait_text(needle, tries=200):
        for _ in range(tries):
            if needle in text():
                return
            time.sleep(0.25)
        raise RuntimeError("等不到文本: " + needle)

    def click(label):
        ok = js("""
            const b = [...document.querySelectorAll('button')]
              .find(x => x.innerText.trim() === arguments[0]);
            if (!b) return false;
            b.click(); return true;
        """, [label])
        if not ok:
            raise RuntimeError("找不到按钮: " + label)
        print(f"  点击「{label}」")
        time.sleep(1.2)

    def shot(name):
        path = os.path.join(OUT, name)
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                        "-File", CAPTURE, "-ProcessName", "EPH205", "-Out", path],
                       check=False, capture_output=True, timeout=40)
        print("  窗口截图:", name)

    print("[1] 登录页")
    wait_text("EP-H205")
    time.sleep(1.5)
    shot("w1-login.png")

    print("\n[2] 登录并连接")
    js("""
        for (const [id, v] of [['username','admin'],['password','admin']]) {
          const el = document.getElementById(id);
          const setter = Object.getOwnPropertyDescriptor(
              window.HTMLInputElement.prototype, 'value').set;
          setter.call(el, v);
          el.dispatchEvent(new Event('input', {bubbles:true}));
        }
        return true;
    """)
    click("登录")
    wait_text("管理员")
    if "仿真中" not in text():
        click("启动仿真")
    wait_text("仿真中")
    click("连接")
    wait_text("已连接 COM7")
    time.sleep(1.2)
    shot("w2-connected.png")

    print("\n[3] 导入并测量")
    js("const f=document.querySelector('input[type=file]');"
       "f.style.display='block';f.style.opacity='1';return true;")
    el = call("POST", "/element", {"using": "css selector", "value": "input[type=file]"})[
        "element-6066-11e4-a52e-4f735466cecf"]
    call("POST", f"/element/{el}/value", {"text": CSV_PATH, "value": list(CSV_PATH)})
    wait_text("导入 7 项")
    js("const f=document.querySelector('input[type=file]');"
       "if(f) f.style.display='none';return true;")
    click("开始测试")
    wait_text("已完成")
    time.sleep(1.5)
    js("const c=document.querySelector('canvas');"
       "if(c) c.scrollIntoView({block:'center'});return true;")
    time.sleep(0.8)
    stats = js("""
        const v = [...document.querySelectorAll('.stat-value')].map(e => e.textContent);
        const l = [...document.querySelectorAll('.stat-label')].map(e => e.textContent);
        return Object.fromEntries(l.map((k, i) => [k, v[i]]));
    """)
    print("  统计:", stats)
    shot("w3-measure.png")

    print("\n[4] 三维")
    click("帮助")
    click("三维视图")
    wait_text("已加载")
    time.sleep(2.5)
    shot("w4-3d.png")

    print("\n完成")


if __name__ == "__main__":
    main()
