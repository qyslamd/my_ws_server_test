"""EPH205 全功能演示（EdgeDriver，有头浏览器）。

覆盖：登录、仿真、连接、CSV/Excel 导入、测试项编辑、测量、实时读数、
只看失败、单条导出、调试页（单点/参数/原始命令/点检）、历史（详情/对比/
批量导出）、工具（仿真点值/日志）、帮助与三维、退出并切到操作员看角色差异。

用法：
  python -I tools/edge_webdriver/eph205_demo.py

前置：服务端已在 8790 运行（建议 --auth），EdgeDriver 在 9515。
跑完不删除会话，浏览器留在桌面上。
"""

import base64
import json
import os
import re
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
OUT = os.path.join(ROOT, "temp", "demo")
DOWNLOADS = os.path.join(OUT, "downloads")
DESKTOP = os.path.join(ROOT, "temp", "eph205_demo_desktop.png")
CSV_PATH = os.path.join(ROOT, "temp", "eph205_items.csv")
XLSX_PATH = os.path.join(ROOT, "temp", "eph205_items.xlsx")
# 每一步之间的停顿，肉眼跟得上为准；可用 DEMO_PACE 覆盖
PACE = float(os.environ.get("DEMO_PACE", "1.6"))

CSV = """序号,P,N,线制,量程,下限,上限,测量
1,1,2,二线,0,90,110,1
2,2,3,二线,0,90,110,1
3,3,4,二线,0,90,110,1
4,4,5,二线,0,90,110,1
5,5,6,四线,1,50,150,1
6,6,7,二线,0,500,,1
7,7,8,二线,0,90,110,0
"""


def http(method, url, data=None, timeout=40):
    body = json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(url, data=body, method=method,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read()
    return json.loads(raw) if raw else None


def call(sid, method, path, data=None):
    return http(method, f"{DRIVER}/session/{sid}{path}", data)["value"]


def js(sid, script, args=None):
    return call(sid, "POST", "/execute/sync", {"script": script, "args": args or []})


def find(sid, xpath):
    return call(sid, "POST", "/element", {"using": "xpath", "value": xpath})[
        "element-6066-11e4-a52e-4f735466cecf"]


def find_all(sid, xpath):
    items = call(sid, "POST", "/elements", {"using": "xpath", "value": xpath})
    return [i["element-6066-11e4-a52e-4f735466cecf"] for i in items]


def click_js(sid, element_id):
    js(sid, "arguments[0].click(); return true;",
       [{"element-6066-11e4-a52e-4f735466cecf": element_id}])


def wait_xpath(sid, xpath, tries=120, label=None):
    for _ in range(tries):
        try:
            return find(sid, xpath)
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("找不到元素: " + (label or xpath))


def wait_text(sid, text, tries=160):
    for _ in range(tries):
        if js(sid, f"return document.body.innerText.includes({json.dumps(text)});"):
            return
        time.sleep(0.25)
    raise RuntimeError("等不到文本: " + text)


def pace(seconds=None):
    time.sleep(PACE if seconds is None else seconds)


def flash(sid, element_id):
    """点之前描个红框，让眼睛有落点。"""
    js(sid, """
        const e = arguments[0];
        const outline = e.style.outline;
        const offset = e.style.outlineOffset;
        e.style.outline = '3px solid #ff3b30';
        e.style.outlineOffset = '2px';
        setTimeout(() => { e.style.outline = outline; e.style.outlineOffset = offset; }, 1100);
        return true;
    """, [{"element-6066-11e4-a52e-4f735466cecf": element_id}])


def banner(title):
    print(f"\n===== {title} =====")
    pace(1.2)


def click(sid, xpath, label=None):
    el = wait_xpath(sid, xpath, label=label)
    flash(sid, el)
    time.sleep(0.55)
    call(sid, "POST", f"/element/{el}/click", {})
    print(f"  点击「{label or xpath}」")
    pace()


def btn(sid, text):
    click(sid, f"//button[normalize-space(.)='{text}']", text)


def tab(sid, text):
    click(sid, f"//button[normalize-space(.)='{text}']", text)
    pace(1.0)


def set_input(sid, xpath, value):
    el = wait_xpath(sid, xpath)
    js(sid, """
        const el = arguments[0];
        const setter = Object.getOwnPropertyDescriptor(
            window.HTMLInputElement.prototype, 'value').set;
        setter.call(el, arguments[1]);
        el.dispatchEvent(new Event('input', { bubbles: true }));
        el.dispatchEvent(new Event('change', { bubbles: true }));
        return el.value;
    """, [{"element-6066-11e4-a52e-4f735466cecf": el}, str(value)])
    pace(0.8)


def upload(sid, path):
    js(sid, "const f=document.querySelector('input[type=file]');"
            "if(f){f.style.display='block';f.style.opacity='1';}return true;")
    el = find(sid, "//input[@type='file']")
    call(sid, "POST", f"/element/{el}/value", {"text": path, "value": list(path)})
    time.sleep(0.8)
    js(sid, "const f=document.querySelector('input[type=file]');"
            "if(f) f.style.display='none';return true;")
    pace(1.2)


def shot(sid, name):
    png = call(sid, "GET", "/screenshot")
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(base64.b64decode(png))
    print("  截图:", name)


def desktop(name=DESKTOP):
    ps = rf'''
$p = Get-Process msedge | Where-Object {{ $_.MainWindowTitle -like '*EP-H205*' }} | Select-Object -First 1
if (-not $p) {{ $p = Get-Process msedge | Where-Object {{ $_.MainWindowTitle -ne '' }} | Select-Object -First 1 }}
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
$bmp.Save('{name}')
'''
    try:
        subprocess.run(["powershell", "-NoProfile", "-Command", ps], timeout=25, check=False)
        print("  桌面截图:", name)
    except Exception as exc:
        print("  桌面截图失败:", exc)


def wait_download(pattern, before, timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        new = [f for f in set(os.listdir(DOWNLOADS)) - before if re.search(pattern, f)]
        if new:
            return new[0]
        time.sleep(0.3)
    return None


def login(sid, user, password):
    wait_xpath(sid, "//button[normalize-space(.)='登录']", label="登录页")
    set_input(sid, "//input[@id='username']", user)
    set_input(sid, "//input[@id='password']", password)
    btn(sid, "登录")
    wait_text(sid, "通讯正常")


def main():
    http("GET", HEALTH)
    print("服务端在线")
    os.makedirs(DOWNLOADS, exist_ok=True)
    with open(CSV_PATH, "w", encoding="utf-8") as f:
        f.write(CSV)

    caps = {"capabilities": {"alwaysMatch": {
        "browserName": "MicrosoftEdge",
        "ms:edgeOptions": {
            "binary": EDGE,
            "args": [
                "--no-first-run", "--no-default-browser-check",
                "--window-size=1400,900", "--window-position=60,40",
                "--remote-allow-origins=*"],
            "prefs": {
                "download.default_directory": DOWNLOADS,
                "download.prompt_for_download": False,
            },
        }}}}
    sid = http("POST", DRIVER + "/session", caps)["value"]["sessionId"]
    print("浏览器会话:", sid)

    call(sid, "POST", "/url", {"url": APP})

    print("\n[1] 登录")
    wait_xpath(sid, "//button[normalize-space(.)='登录']")
    time.sleep(1.0)
    shot(sid, "01-login.png")
    login(sid, "admin", "admin")
    wait_text(sid, "管理员")
    time.sleep(1.0)
    shot(sid, "02-shell.png")

    print("\n[2] 启动仿真并连接设备")
    try:
        find(sid, "//button[normalize-space(.)='停止仿真']")
        print("  仿真器已在运行")
    except Exception:
        btn(sid, "启动仿真")
        wait_text(sid, "仿真中")
    time.sleep(0.8)
    btn(sid, "连接")
    wait_text(sid, "已连接 COM7")
    time.sleep(1.0)
    shot(sid, "03-connected.png")

    print("\n[3] 导入 CSV（7 项）")
    upload(sid, CSV_PATH)
    wait_text(sid, "导入 7 项")
    pace(1.6)
    shot(sid, "04-import-csv.png")

    print("\n[4] 导入 Excel（4 项，验证 SheetJS）")
    upload(sid, XLSX_PATH)
    wait_text(sid, "导入 4 项")
    time.sleep(0.8)
    shot(sid, "05-import-excel.png")
    upload(sid, CSV_PATH)
    wait_text(sid, "导入 7 项")
    time.sleep(0.6)

    print("\n[5] 编辑测试项：加一行再删掉")
    btn(sid, "添加一行")
    time.sleep(0.6)
    count = js(sid, 'return document.querySelectorAll("[data-page=items] tbody tr").length;')
    print("  加行后行数:", count)
    rows = find_all(sid, "//div[@data-page='items']//button[normalize-space(.)='删除']")
    click_js(sid, rows[-1])
    time.sleep(0.6)
    count = js(sid, 'return document.querySelectorAll("[data-page=items] tbody tr").length;')
    print("  删行后行数:", count)

    print("\n[6] 开始测试")
    btn(sid, "开始测试")
    wait_text(sid, "已完成")
    pace(2.0)
    stats = js(sid, """
        const v = [...document.querySelectorAll('.stat-value')].map(e => e.textContent);
        const l = [...document.querySelectorAll('.stat-label')].map(e => e.textContent);
        return Object.fromEntries(l.map((k, i) => [k, v[i]]));
    """)
    print("  统计:", stats)
    js(sid, "const c=document.querySelector('canvas');"
            "if(c) c.scrollIntoView({block:'center'});return true;")
    time.sleep(0.8)
    shot(sid, "06-measure.png")
    desktop(os.path.join(OUT, "desktop-measure.png"))

    print("\n[7] 只看失败")
    only_fail = "//input[@type='checkbox'][following-sibling::span[normalize-space(.)='只看失败']]"
    click(sid, only_fail, "只看失败")
    time.sleep(0.8)
    rows = js(sid, 'return document.querySelectorAll("[data-page=results] tbody tr").length;')
    print("  只看失败行数:", rows)
    shot(sid, "07-only-fail.png")
    click(sid, only_fail, "只看失败")
    time.sleep(0.4)

    print("\n[8] 导出 CSV")
    before = set(os.listdir(DOWNLOADS))
    btn(sid, "导出 CSV")
    print("  下载文件:", wait_download(r"measurement-\d+\.csv", before))
    time.sleep(0.6)

    print("\n[9] 调试页")
    tab(sid, "调试")
    time.sleep(0.8)
    btn(sid, "测量")
    time.sleep(1.0)
    btn(sid, "读取参数")
    time.sleep(0.8)
    btn(sid, "下发")
    time.sleep(0.6)
    js(sid, """
        for (const sel of document.querySelectorAll('select')) {
          if ([...sel.options].some(o => o.text.includes('0x05'))) {
            sel.value = '5';
            sel.dispatchEvent(new Event('change', {bubbles: true}));
            break;
          }
        }
        return true;
    """)
    btn(sid, "发送")
    wait_text(sid, "应答")
    time.sleep(0.6)
    btn(sid, "点检")
    time.sleep(1.0)
    shot(sid, "08-debug.png")

    print("\n[10] 历史记录")
    tab(sid, "历史")
    time.sleep(1.2)
    shot(sid, "09-history.png")

    print("\n[11] 详情抽屉")
    click(sid, "//button[normalize-space(.)='详情']", "详情")
    wait_text(sid, "通过率")
    time.sleep(1.0)
    shot(sid, "10-history-detail.png")
    click(sid, "//button[contains(@class,'close')]", "关闭")

    print("\n[12] 两条记录对比")
    js(sid, """
        const boxes = document.querySelectorAll(
          "[data-page='history'] tbody tr input[type=checkbox]");
        boxes[0].click(); boxes[1].click(); return true;
    """)
    wait_text(sid, "已选 2 条")
    time.sleep(0.4)
    btn(sid, "对比")
    wait_text(sid, "最大差值")
    pace(2.4)
    shot(sid, "11-compare.png")
    desktop(os.path.join(OUT, "desktop-compare.png"))
    click(sid, "//button[contains(@class,'close')]", "关闭")

    print("\n[13] 批量导出")
    before = set(os.listdir(DOWNLOADS))
    btn(sid, "批量导出 CSV")
    print("  下载文件:", wait_download(r"measurements-\d+\.csv", before))
    time.sleep(0.8)

    print("\n[14] 工具页：仿真点值")
    tab(sid, "工具")
    time.sleep(1.0)
    shot(sid, "12-tools.png")
    sim_panel = "//div[contains(@class,'panel')][.//div[normalize-space(text())='仿真器']]"
    set_input(sid, f"{sim_panel}//input[@type='number'][1]", 120)
    btn(sid, "应用")
    time.sleep(0.8)
    set_input(sid, f"{sim_panel}//input[@type='number'][3]", 80)
    btn(sid, "设置点值")
    time.sleep(0.8)
    shot(sid, "13-tools-sim.png")
    btn(sid, "清除覆盖")
    time.sleep(0.6)

    print("\n[15] 帮助与三维视图")
    tab(sid, "帮助")
    time.sleep(0.8)
    shot(sid, "14-help.png")
    tab(sid, "协议速查")
    time.sleep(0.8)
    shot(sid, "15-protocol.png")
    tab(sid, "三维视图")
    wait_text(sid, "已加载")
    pace(2.4)
    shot(sid, "16-3d.png")
    desktop(os.path.join(OUT, "desktop-3d.png"))

    print("\n[16] 退出并切到操作员")
    btn(sid, "退出")
    login(sid, "operator", "operator")
    wait_text(sid, "操作员")
    time.sleep(1.0)
    has_debug = js(sid, """return [...document.querySelectorAll('button')]
        .some(b => b.innerText.trim() === '调试');""")
    sim_disabled = js(sid, """const b = [...document.querySelectorAll('button')]
        .find(x => x.innerText.trim().endsWith('仿真'));
        return b ? b.disabled : null;""")
    print("  调试页可见:", has_debug, "| 仿真按钮禁用:", sim_disabled)
    shot(sid, "17-operator.png")
    desktop()

    print("\n演示结束，浏览器保持打开（会话 %s）" % sid)


if __name__ == "__main__":
    main()
