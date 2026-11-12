"""三维视图冒烟：打开帮助页 → 三维视图，确认模型加载与画布渲染。

用法：python -I tools/edge_webdriver/eph205_3d_check.py
没有服务端时会自己拉起，退出时结束。
"""

import base64
import json
import os
import subprocess
import sys
import time
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")

DRIVER = "http://127.0.0.1:9515"
APP = "http://127.0.0.1:8790/eph205/"
HEALTH = "http://127.0.0.1:8790/api/v1/health"
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
ROOT = r"D:\trae_workspace\my_ws_server_test"
SHOT = os.path.join(ROOT, "temp", "eph205_3d.png")


def http(method, url, data=None):
    body = json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(url, data=body, method=method,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        raw = resp.read()
    return json.loads(raw) if raw else None


def call(sid, method, path, data=None):
    return http(method, f"{DRIVER}/session/{sid}{path}", data)["value"]


def js(sid, script):
    return call(sid, "POST", "/execute/sync", {"script": script, "args": []})


def find(sid, xpath):
    return call(sid, "POST", "/element",
                {"using": "xpath", "value": xpath})["element-6066-11e4-a52e-4f735466cecf"]


def wait_xpath(sid, xpath, tries=120):
    for _ in range(tries):
        try:
            return find(sid, xpath)
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("not found " + xpath)


def click_text(sid, text):
    el = wait_xpath(sid, f"//li[normalize-space(.)='{text}']")
    call(sid, "POST", f"/element/{el}/click", {})


def main():
    proc = None
    try:
        http("GET", HEALTH)
        print("复用已在运行的服务端")
    except Exception:
        proc = subprocess.Popen(
            [os.path.join(ROOT, "build", "eph205_server.exe"), "8790"],
            cwd=os.path.join(ROOT, "build"))
        for _ in range(40):
            try:
                http("GET", HEALTH)
                break
            except Exception:
                time.sleep(0.25)
        print("已启动服务端 pid=", proc.pid)

    caps = {"capabilities": {"alwaysMatch": {
        "browserName": "MicrosoftEdge",
        "ms:edgeOptions": {"binary": EDGE, "args": [
            "--headless=new", "--disable-gpu", "--no-first-run",
            "--use-angle=swiftshader", "--enable-unsafe-swiftshader",
            "--window-size=1400,900", "--remote-allow-origins=*"]}}}}
    sid = http("POST", DRIVER + "/session", caps)["value"]["sessionId"]
    print("session:", sid)
    try:
        call(sid, "POST", "/url", {"url": APP})
        wait_xpath(sid, "//span[contains(., '通讯正常')]")
        click_text(sid, "帮助")
        wait_xpath(sid, "//div[@role='tab'][normalize-space(.)='三维视图']")
        el = find(sid, "//div[@role='tab'][normalize-space(.)='三维视图']")
        call(sid, "POST", f"/element/{el}/click", {})

        wait_xpath(sid, "//span[contains(., '已加载')]", tries=200)
        info = js(sid, """
            const c = document.querySelector('canvas');
            let gl = null;
            try { gl = c && (c.getContext('webgl2') || c.getContext('webgl')); }
            catch (e) { gl = null; }
            const tags = [...document.querySelectorAll('.ant-tag')].map(t => t.innerText);
            return {
              hasCanvas: !!c,
              w: c ? c.width : 0,
              h: c ? c.height : 0,
              gl: gl ? (gl.getParameter(gl.VERSION) || 'ok') : null,
              tags: tags
            };
        """)
        print("canvas:", info["w"], "x", info["h"], "| webgl:", info["gl"])
        print("标签:", info["tags"])
        if not info["hasCanvas"] or not info["gl"]:
            raise RuntimeError("画布或 WebGL 不可用")

        time.sleep(1.5)
        png = call(sid, "GET", "/screenshot")
        with open(SHOT, "wb") as f:
            f.write(base64.b64decode(png))
        print("截图:", SHOT)
    finally:
        call(sid, "DELETE", "")
        if proc is not None:
            proc.terminate()
            proc.wait(timeout=10)


if __name__ == "__main__":
    main()
