"""验证 EPH205 桌面客户端（build-client\\EPH205.exe --devtools）。

检查：
  1. 窗口/页面加载、标题
  2. 登录 → 仿真 → 连接 → 导入 → 测量 → 三维（和网页版一致）
  3. 浏览器行为被禁：F5 / Ctrl+R 不刷新页面（用 JS 变量标记验证）
  4. 导出 CSV 落到 exe 同级的 exports\\

前置：EPH205.exe --devtools 已运行（远程调试端口 9223）。
"""

import base64
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
EXPORTS = os.path.join(ROOT, "build-client", "exports")

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

    print("CEF:", wait_url(f"http://127.0.0.1:{DEBUG_PORT}/json/version")["Browser"])
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
    print("附加成功")

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
        png = call("GET", "/screenshot")
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(base64.b64decode(png))
        print("  截图:", name)

    print("\n[1] 页面")
    wait_text("EP-H205")
    print("  document.title:", call("GET", "/title"))
    print("  URL:", js("return location.href;"))
    shot("01-login.png")

    print("\n[2] 键盘拦截")
    js("window.__marker = 42; return true;")
    for label, payload in (
        ("F5", [{"type": "keyDown", "value": "\ue035"},
                {"type": "keyUp", "value": "\ue035"}]),
        ("Ctrl+R", [{"type": "keyDown", "value": "\ue009"},
                    {"type": "keyDown", "value": "r"},
                    {"type": "keyUp", "value": "r"},
                    {"type": "keyUp", "value": "\ue009"}]),
    ):
        call("POST", "/actions", {"actions": [{"type": "key", "id": "kb", "actions": payload}]})
        time.sleep(1.0)
        alive = js("return window.__marker === 42;")
        print(f"  {label}: 页面{'没有' if alive else '被'}刷新")

    print("\n[3] 登录与测量")
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
    js("const f=document.querySelector('input[type=file]');"
       "f.style.display='block';f.style.opacity='1';return true;")
    el = call("POST", "/element", {"using": "css selector", "value": "input[type=file]"})[
        "element-6066-11e4-a52e-4f735466cecf" ]
    call("POST", f"/element/{el}/value", {"text": CSV_PATH, "value": list(CSV_PATH)})
    wait_text("导入 7 项")
    click("开始测试")
    wait_text("已完成")
    time.sleep(1.2)
    stats = js("""
        const v = [...document.querySelectorAll('.stat-value')].map(e => e.textContent);
        const l = [...document.querySelectorAll('.stat-label')].map(e => e.textContent);
        return Object.fromEntries(l.map((k, i) => [k, v[i]]));
    """)
    print("  统计:", stats)
    shot("02-measure.png")

    print("\n[4] 导出 CSV")
    before = set(os.listdir(EXPORTS)) if os.path.isdir(EXPORTS) else set()
    click("导出 CSV")
    time.sleep(2.0)
    after = set(os.listdir(EXPORTS)) if os.path.isdir(EXPORTS) else set()
    print("  exports 新增:", sorted(after - before))

    print("\n[5] 三维")
    click("帮助")
    click("三维视图")
    wait_text("已加载")
    time.sleep(2.0)
    info = js("""
        const c = document.querySelector('.viewer-host canvas');
        let gl = null;
        try { gl = c && (c.getContext('webgl2') || c.getContext('webgl')); } catch (e) {}
        return { w: c ? c.width : 0, gl: gl ? gl.getParameter(gl.VERSION) : null };
    """)
    print("  canvas:", info["w"], "| WebGL:", info["gl"])
    shot("03-3d.png")

    print("\n[6] 页面错误")
    errs = js("return window.__errors || [];")
    print("  ", errs if errs else "没有 JS 错误")

    print("\n完成。客户端保持运行。")


if __name__ == "__main__":
    main()
