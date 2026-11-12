"""在本地编译的 CEF 109 里跑一遍 205 web（纯标准库 + ChromeDriver HTTP）。

为什么不用 selenium：selenium 4.52 自带的服务管理在附加到 cefclient 时
会卡住；直接用 WebDriver 的 HTTP 接口反而稳定，和仓库里 EdgeDriver 的
脚本是同一套写法。

流程：
  1. 在 8791 起一个 eph205_server（--auth），和 8790 的调试实例互不影响
  2. 启动 release 版 cefclient，开远程调试端口 9223，指向 205 页面
  3. 启动 109 的 ChromeDriver（9516），用 debuggerAddress 附加到 CEF
  4. 登录 → 仿真 → 连接 → 导入 → 测量 → 三维 → 历史，逐步截图
  5. 读出页面里收集的 JS 错误（app.js 的 window.__errors）

跑完 CEF、ChromeDriver、服务端都保留。
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

CEF_ROOT = r"D:\trae_workspace\qt5-cef5414.120-base-env"
CEF_EXE = os.path.join(CEF_ROOT, "build-cef-109", "tests", "cefclient", "Release", "cefclient.exe")
CEF_DIR = os.path.dirname(CEF_EXE)
CHROMEDRIVER = os.path.join(CEF_ROOT, "chromedriver_win32", "chromedriver.exe")

ROOT = r"D:\trae_workspace\my_ws_server_test"
SERVER = os.path.join(ROOT, "build", "eph205_server.exe")
PORT = 8791
DEBUG_PORT = 9223
DRIVER_PORT = 9516
APP = f"http://127.0.0.1:{PORT}/eph205/?v={int(time.time())}"
OUT = os.path.join(ROOT, "temp", "cef")
CSV_PATH = os.path.join(ROOT, "temp", "eph205_items.csv")

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


def wait_url(url, tries=120, label=""):
    for _ in range(tries):
        try:
            return http("GET", url, timeout=2)
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("等不到 " + (label or url))


def main():
    os.makedirs(OUT, exist_ok=True)
    with open(CSV_PATH, "w", encoding="utf-8") as f:
        f.write(CSV)

    # 1) 服务端
    try:
        wait_url(f"http://127.0.0.1:{PORT}/api/v1/health", tries=4)
        print(f"复用已在运行的服务端 :{PORT}")
    except Exception:
        server = subprocess.Popen([SERVER, str(PORT), "--auth"],
                                  cwd=os.path.join(ROOT, "build"))
        wait_url(f"http://127.0.0.1:{PORT}/api/v1/health", label="服务端")
        print(f"已启动服务端 pid={server.pid} :{PORT}")

    # 2) CEF
    version = None
    try:
        version = wait_url(f"http://127.0.0.1:{DEBUG_PORT}/json/version", tries=4)
        print("复用已打开的 CEF")
    except Exception:
        print("启动 CEF …")
        subprocess.Popen([CEF_EXE, f"--remote-debugging-port={DEBUG_PORT}",
                          f"--url={APP}"], cwd=CEF_DIR)
        version = wait_url(f"http://127.0.0.1:{DEBUG_PORT}/json/version", label="CEF 调试端口")
    print("CEF:", version.get("Browser"))

    # 3) ChromeDriver 109
    try:
        wait_url(f"http://127.0.0.1:{DRIVER_PORT}/status", tries=3)
    except Exception:
        subprocess.Popen([CHROMEDRIVER, f"--port={DRIVER_PORT}"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_url(f"http://127.0.0.1:{DRIVER_PORT}/status", label="ChromeDriver")
    print("ChromeDriver:", http("GET", f"http://127.0.0.1:{DRIVER_PORT}/status")["value"]["build"]["version"])

    # 4) 附加
    session = http("POST", f"http://127.0.0.1:{DRIVER_PORT}/session", {
        "capabilities": {"alwaysMatch": {
            "goog:chromeOptions": {"debuggerAddress": f"127.0.0.1:{DEBUG_PORT}"}}}
    })
    sid = session["value"]["sessionId"]
    print("附加成功，session:", sid)

    def call(method, path, data=None):
        return http(method, f"http://127.0.0.1:{DRIVER_PORT}/session/{sid}{path}", data)["value"]

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

    def click_button(label):
        ok = js("""
            const label = arguments[0];
            const b = [...document.querySelectorAll('button')]
              .find(x => x.innerText.trim() === label);
            if (!b) return false;
            b.click();
            return true;
        """, [label])
        if not ok:
            raise RuntimeError("找不到按钮: " + label)
        print(f"  点击「{label}」")
        time.sleep(1.3)

    def set_input(element_id, value):
        js("""
            const el = document.getElementById(arguments[0]);
            const setter = Object.getOwnPropertyDescriptor(
                window.HTMLInputElement.prototype, 'value').set;
            setter.call(el, arguments[1]);
            el.dispatchEvent(new Event('input', { bubbles: true }));
        """, [element_id, value])

    def find(xpath):
        return call("POST", "/element", {"using": "xpath", "value": xpath})[
            "element-6066-11e4-a52e-4f735466cecf"]

    def shot(name):
        png = call("GET", "/screenshot")
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(base64.b64decode(png))
        print("  截图:", name)

    call("POST", "/url", {"url": APP})
    print("\n[1] 打开页面")
    wait_text("EP-H205")
    time.sleep(1.5)
    print("  title:", call("GET", "/title"))
    print("  UA:", js("return navigator.userAgent;"))
    shot("01-login.png")

    print("\n[2] 登录")
    set_input("username", "admin")
    set_input("password", "admin")
    click_button("登录")
    wait_text("管理员")
    time.sleep(1.3)
    shot("02-shell.png")

    print("\n[3] 仿真 + 连接")
    if "仿真中" not in text():
        click_button("启动仿真")
    wait_text("仿真中")
    click_button("连接")
    wait_text("已连接 COM7")
    time.sleep(1.3)
    shot("03-connected.png")

    print("\n[4] 导入 CSV")
    js("const f=document.querySelector('input[type=file]');"
       "f.style.display='block';f.style.opacity='1';return true;")
    el = find("//input[@type='file']")
    call("POST", f"/element/{el}/value", {"text": CSV_PATH, "value": list(CSV_PATH)})
    wait_text("导入 7 项")
    time.sleep(1.3)
    shot("04-import.png")

    print("\n[5] 测量")
    click_button("开始测试")
    wait_text("已完成")
    time.sleep(1.5)
    stats = js("""
        const v = [...document.querySelectorAll('.stat-value')].map(e => e.textContent);
        const l = [...document.querySelectorAll('.stat-label')].map(e => e.textContent);
        return Object.fromEntries(l.map((k, i) => [k, v[i]]));
    """)
    print("  统计:", stats)
    shot("05-measure.png")

    print("\n[6] 三维视图")
    click_button("帮助")
    click_button("三维视图")
    wait_text("已加载")
    time.sleep(2.0)
    info = js("""
        const c = document.querySelector('.viewer-host canvas');
        let gl = null;
        try { gl = c && (c.getContext('webgl2') || c.getContext('webgl')); } catch (e) {}
        let renderer = null;
        try {
          const g = gl || (c && c.getContext('webgl'));
          const ext = g && g.getExtension('WEBGL_debug_renderer_info');
          renderer = ext ? g.getParameter(ext.UNMASKED_RENDERER_WEBGL) : null;
        } catch (e) {}
        return { hasCanvas: !!c, w: c ? c.width : 0, h: c ? c.height : 0,
                 gl: gl ? gl.getParameter(gl.VERSION) : null, renderer };
    """)
    print("  canvas:", info["w"], "x", info["h"], "| WebGL:", info["gl"])
    print("  渲染器:", info["renderer"])
    shot("06-3d.png")

    print("\n[7] 历史")
    click_button("历史")
    time.sleep(1.5)
    shot("07-history.png")

    print("\n[8] 页面错误")
    errs = js("return window.__errors || [];")
    if errs:
        for e in errs[:20]:
            print("  ✗", e[:200])
    else:
        print("  没有 JS 错误")

    print("\n完成。CEF 窗口保持打开，服务端 :%d，driver :%d" % (PORT, DRIVER_PORT))


if __name__ == "__main__":
    main()
