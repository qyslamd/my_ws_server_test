"""EPH205 有头浏览器演示（EdgeDriver）。

和前两个脚本不同，这里故意不删会话：跑完浏览器留在桌面上，
可以继续手动操作。服务端需要事先用 CDB 或直接运行起来。

用法：
  python -I tools/edge_webdriver/eph205_demo.py

每一步停 1.5~2 s，方便肉眼跟；同时存 WebDriver 截图到 temp/demo/。
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

DRIVER = "http://127.0.0.1:9515"
APP = "http://127.0.0.1:8790/eph205/"
HEALTH = "http://127.0.0.1:8790/api/v1/health"
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
ROOT = r"D:\trae_workspace\my_ws_server_test"
OUT = os.path.join(ROOT, "temp", "demo")
PROFILE = os.path.join(ROOT, "temp", "edge-demo-profile")
CSV_PATH = os.path.join(ROOT, "temp", "eph205_items.csv")
DESKTOP = os.path.join(ROOT, "temp", "eph205_demo_desktop.png")

CSV = """序号,P,N,线制,量程,下限,上限,测量
1,1,2,二线,0,90,110,1
2,2,3,二线,0,90,110,1
3,3,4,二线,0,90,110,1
4,4,5,二线,0,90,110,1
5,5,6,四线,1,50,150,1
6,6,7,二线,0,500,,1
7,7,8,二线,0,90,110,0
"""


def http(method, url, data=None):
    body = json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(url, data=body, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as exc:
        raw = exc.read()
        raise RuntimeError(f"{exc.code} {raw.decode('utf-8', 'replace')[:200]}")
    return json.loads(raw) if raw else None


def call(sid, method, path, data=None):
    return http(method, f"{DRIVER}/session/{sid}{path}", data)["value"]


def js(sid, script):
    return call(sid, "POST", "/execute/sync", {"script": script, "args": []})


def find(sid, xpath):
    value = call(sid, "POST", "/element", {"using": "xpath", "value": xpath})
    return value["element-6066-11e4-a52e-4f735466cecf"]


def wait_xpath(sid, xpath, tries=120):
    for _ in range(tries):
        try:
            return find(sid, xpath)
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("找不到元素: " + xpath)


def click_text(sid, text):
    el = wait_xpath(sid, f"//button[normalize-space(.)='{text}']")
    call(sid, "POST", f"/element/{el}/click", {})
    print(f"  点击「{text}」")


def shot(sid, name):
    png = call(sid, "GET", "/screenshot")
    path = os.path.join(OUT, name)
    with open(path, "wb") as f:
        f.write(base64.b64decode(png))
    print("  截图:", name)


def capture_desktop():
    ps = rf'''
$p = Get-Process msedge | Where-Object {{ $_.MainWindowTitle -ne '' }} | Select-Object -First 1
if ($p) {{
  $ws = New-Object -ComObject WScript.Shell
  $ws.AppActivate($p.Id) | Out-Null
  Start-Sleep -Milliseconds 900
}}
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$b = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size)
$bmp.Save('{DESKTOP}')
'''
    try:
        subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                       timeout=25, check=False)
        print("桌面截图:", DESKTOP)
    except Exception as exc:
        print("桌面截图失败:", exc)


def main():
    http("GET", HEALTH)
    print("服务端在线（CDB 下运行）")
    os.makedirs(OUT, exist_ok=True)
    with open(CSV_PATH, "w", encoding="utf-8") as f:
        f.write(CSV)

    caps = {"capabilities": {"alwaysMatch": {
        "browserName": "MicrosoftEdge",
        "ms:edgeOptions": {"binary": EDGE, "args": [
            f"--user-data-dir={PROFILE}",
            "--no-first-run", "--no-default-browser-check",
            "--window-size=1400,900", "--window-position=60,40",
            "--remote-allow-origins=*"]}}}}
    sid = http("POST", DRIVER + "/session", caps)["value"]["sessionId"]
    print("浏览器会话:", sid)

    call(sid, "POST", "/url", {"url": APP})
    print("\n[1] 登录并打开测试页")
    wait_xpath(sid, "//button[normalize-space(.)='登录']")
    time.sleep(0.8)
    shot(sid, "00-login.png")
    btn = find(sid, "//button[normalize-space(.)='登录']")
    call(sid, "POST", f"/element/{btn}/click", {})
    wait_xpath(sid, "//span[contains(., '通讯正常')]")
    wait_xpath(sid, "//*[contains(text(), 'admin · 管理员')]")
    time.sleep(1.2)
    shot(sid, "01-open.png")

    print("\n[2] 启动仿真 + 连接设备")
    try:
        find(sid, "//button[normalize-space(.)='停止仿真']")
        print("  仿真器已在运行")
    except Exception:
        click_text(sid, "启动仿真")
        time.sleep(1.2)
    click_text(sid, "连接")
    wait_xpath(sid, "//*[contains(text(), '已连接 COM7')]")
    time.sleep(1.2)
    shot(sid, "02-connected.png")

    print("\n[3] 导入测试项")
    js(sid, """
        const el = document.querySelector('input[type=file]');
        el.style.display = 'block';
        el.style.opacity = '1';
        el.style.width = '200px';
        el.style.height = '30px';
        return true;
    """)
    el = find(sid, "//input[@type='file']")
    call(sid, "POST", f"/element/{el}/value",
         {"text": CSV_PATH, "value": list(CSV_PATH)})
    wait_xpath(sid, "//td[contains(., '二线')]")
    time.sleep(1.5)
    shot(sid, "03-import.png")

    print("\n[4] 开始测量")
    click_text(sid, "开始测试")
    for _ in range(160):
        body = js(sid, "return document.body.innerText;") or ""
        if "completed" in body and "记录号" in body:
            break
        time.sleep(0.25)
    else:
        raise RuntimeError("测量没有完成")
    time.sleep(1.5)
    shot(sid, "04-result.png")
    stats = js(sid, """
        const vals = [...document.querySelectorAll('.stat-value')].map(e => e.textContent);
        const labels = [...document.querySelectorAll('.stat-label')].map(e => e.textContent);
        return Object.fromEntries(labels.map((l, i) => [l, vals[i]]));
    """)
    print("  统计:", stats)

    print("\n[5] 历史记录")
    el = wait_xpath(sid, "//li[normalize-space(.)='历史']")
    call(sid, "POST", f"/element/{el}/click", {})
    time.sleep(1.5)
    shot(sid, "05-history.png")

    print("\n[6] 三维视图")
    el = wait_xpath(sid, "//li[normalize-space(.)='帮助']")
    call(sid, "POST", f"/element/{el}/click", {})
    el = wait_xpath(sid, "//div[@role='tab'][normalize-space(.)='三维视图']")
    call(sid, "POST", f"/element/{el}/click", {})
    wait_xpath(sid, "//span[contains(., '已加载')]", tries=200)
    time.sleep(2.0)
    shot(sid, "06-3d.png")

    print("\n[7] 回到测试页")
    el = wait_xpath(sid, "//li[normalize-space(.)='测试']")
    call(sid, "POST", f"/element/{el}/click", {})
    time.sleep(1.0)

    capture_desktop()
    print("\n演示结束，浏览器保持打开（会话 %s 未删除）" % sid)


if __name__ == "__main__":
    main()
