"""EPH205 页面端到端自动化（EdgeDriver，无第三方依赖）。

流程：
  打开 /eph205/ → 启动仿真 → 连接 → 导入 CSV → 开始测试 → 等完成
  → 读统计 → 截图

前置：
  build\\eph205_server.exe 8790  已运行
  3rdparty\\edgedriver_win64\\msedgedriver.exe --port=9515  已运行

用法：
  python -I tools/edge_webdriver/eph205_e2e.py
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
CSV_PATH = os.path.join(ROOT, "temp", "eph205_items.csv")
XLSX_PATH = os.path.join(ROOT, "temp", "eph205_items.xlsx")
SHOT = os.path.join(ROOT, "temp", "eph205_e2e.png")

CSV = """序号,P,N,线制,量程,下限,上限,测量
1,1,2,二线,0,90,110,1
2,2,3,二线,0,90,110,1
3,3,4,二线,0,90,110,1
4,4,5,二线,0,90,110,1
5,5,6,四线,1,50,150,1
6,6,7,二线,0,500,,1
7,7,8,二线,0,90,110,0
"""


def http(method, url, data=None, timeout=25):
    body = json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(url, data=body, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as exc:
        raw = exc.read()
        raise RuntimeError(f"{exc.code} {raw.decode('utf-8', 'replace')[:200]}")
    return json.loads(raw) if raw else None


def wait(url, tries=100):
    for _ in range(tries):
        try:
            return http("GET", url)
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("timeout " + url)


def call(sid, method, path, data=None):
    return http(method, f"{DRIVER}/session/{sid}{path}", data)["value"]


def js(sid, script):
    return call(sid, "POST", "/execute/sync", {"script": script, "args": []})


def find(sid, xpath):
    value = call(sid, "POST", "/element",
                 {"using": "xpath", "value": xpath})
    return value["element-6066-11e4-a52e-4f735466cecf"]


def click_js(sid, element_id):
    """隐藏/被遮挡的元素用 JS 直接 click，绕开 interactability 检查。"""
    return call(sid, "POST", "/execute/sync", {
        "script": "arguments[0].click(); return true;",
        "args": [{"element-6066-11e4-a52e-4f735466cecf": element_id}],
    })


def find_all(sid, xpath):
    items = call(sid, "POST", "/elements",
                 {"using": "xpath", "value": xpath})
    return [i["element-6066-11e4-a52e-4f735466cecf"] for i in items]


def wait_xpath(sid, xpath, tries=100, label=None):
    for _ in range(tries):
        try:
            return find(sid, xpath)
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("找不到元素: " + (label or xpath))


def click_text(sid, text):
    el = wait_xpath(sid, f"//button[normalize-space(.)='{text}']", label=text)
    call(sid, "POST", f"/element/{el}/click", {})
    print(f"  点击「{text}」")


def set_input(sid, element_id, value):
    """AntD 是受控输入，WebDriver 的 clear 不触发 React onChange，
    这里用原生 setter + input 事件把值真正写进去。"""
    call(sid, "POST", "/execute/sync", {
        "script": """
        const el = document.getElementById(arguments[0]);
        const setter = Object.getOwnPropertyDescriptor(
            window.HTMLInputElement.prototype, 'value').set;
        setter.call(el, arguments[1]);
        el.dispatchEvent(new Event('input', { bubbles: true }));
        return el.value;
        """,
        "args": [element_id, value],
    })


def login(sid, user, password):
    wait_xpath(sid, "//button[normalize-space(.)='登录']", label="登录页")
    set_input(sid, "username", user)
    set_input(sid, "password", password)
    btn = find(sid, "//button[normalize-space(.)='登录']")
    call(sid, "POST", f"/element/{btn}/click", {})
    print(f"  以 {user} 登录")


SERVER_PROC = None


def ensure_server():
    """没有实例就自己拉起一个，脚本退出时一并结束。"""
    global SERVER_PROC
    try:
        wait(HEALTH, tries=4)
        print("复用已在运行的服务端")
        return
    except Exception:
        pass
    exe = os.path.join(ROOT, "build", "eph205_server.exe")
    # --auth 打开服务端的角色校验（admin 才能执行调试/维护命令）
    SERVER_PROC = subprocess.Popen([exe, "8790", "--auth"],
                                   cwd=os.path.join(ROOT, "build"))
    wait(HEALTH)
    print("已启动服务端 pid=", SERVER_PROC.pid)


def main():
    ensure_server()
    wait(DRIVER + "/status")

    with open(CSV_PATH, "w", encoding="utf-8") as f:
        f.write(CSV)
    print("测试项 CSV:", CSV_PATH)

    caps = {"capabilities": {"alwaysMatch": {
        "browserName": "MicrosoftEdge",
        "ms:edgeOptions": {"binary": EDGE, "args": [
            "--headless=new", "--disable-gpu", "--no-first-run",
            "--window-size=1400,900", "--remote-allow-origins=*"]}}}}
    sid = http("POST", DRIVER + "/session", caps)["value"]["sessionId"]
    print("session:", sid)

    try:
        call(sid, "POST", "/url", {"url": APP})
        print("打开", APP)

        login(sid, "admin", "admin")
        wait_xpath(sid, "//span[contains(., '通讯正常')]", label="通讯正常")
        wait_xpath(sid, "//span[contains(@class,'tag')][contains(normalize-space(.), '管理员')]",
                   label="管理员标识")
        print("  WS 已连接，当前为管理员")

        try:
            find(sid, "//button[normalize-space(.)='停止仿真']")
            print("  仿真器已在运行，跳过启动")
        except Exception:
            click_text(sid, "启动仿真")
        wait_xpath(sid, "//span[contains(., '仿真中')]", label="仿真中")

        click_text(sid, "连接")
        wait_xpath(sid, "//*[contains(text(), '已连接 COM7')]", label="已连接 COM7")
        print("  设备已连接")

        # AntD Upload 的 input 默认隐藏，先取消隐藏再投递文件
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
        print("  已投递 CSV")

        wait_xpath(sid, "//div[@data-page='items']//tbody/tr", label="导入后的表格行")

        # Excel 导入：读第一张（或名为“测试项”的）工作表
        print("[3b] Excel 导入")
        el = find(sid, "//input[@type='file']")
        call(sid, "POST", f"/element/{el}/value",
             {"text": XLSX_PATH, "value": list(XLSX_PATH)})
        for _ in range(60):
            if js(sid, "return [...document.querySelectorAll('input')]"
                      ".some(i => i.value === '21');"):
                break
            time.sleep(0.25)
        else:
            raise RuntimeError("Excel 导入没有生效")
        rows = js(sid, """return document.querySelector("[data-page='items']").querySelectorAll("tbody tr").length;""")
        print("  Excel 导入后表格行数:", rows)
        if rows != 4:
            raise RuntimeError(f"Excel 导入行数不对: {rows}")

        # 换回 CSV，后面的统计按 7 项校验
        el = find(sid, "//input[@type='file']")
        call(sid, "POST", f"/element/{el}/value",
             {"text": CSV_PATH, "value": list(CSV_PATH)})
        for _ in range(60):
            back = js(sid, """
                const vals = [...document.querySelectorAll('input')].map(i => i.value);
                return !vals.includes('21') && vals.includes('6');
            """)
            if back:
                break
            time.sleep(0.25)
        else:
            raise RuntimeError("换回 CSV 失败")
        time.sleep(0.5)

        # 导入完成后把临时取消隐藏的文件框收回去
        js(sid, "const f=document.querySelector('input[type=file]'); if(f) f.style.display='none'; return true;")
        click_text(sid, "开始测试")
        print("  等待测量完成…")

        for _ in range(120):
            body = js(sid, "return document.body.innerText;") or ""
            if "已完成" in body and "记录号" in body:
                break
            time.sleep(0.25)
        else:
            raise RuntimeError("测量没有在 30 秒内完成")

        stats = js(sid, """
            const vals = [...document.querySelectorAll('.stat-value')].map(e => e.textContent);
            const labels = [...document.querySelectorAll('.stat-label')].map(e => e.textContent);
            const rows = [...document.querySelectorAll('table')].pop().innerText;
            return {stats: vals, labels: labels, tail: rows.slice(0, 400)};
        """)
        print("  统计:", dict(zip(stats["labels"], stats["stats"])))
        print("  结果表尾部:")
        for line in stats["tail"].splitlines()[:8]:
            print("   ", line)

        # 滚到实时读数曲线，单独留一张图
        js(sid, """
            const c = document.querySelector('canvas');
            if (c) c.scrollIntoView({ block: 'center' });
            return true;
        """)
        time.sleep(0.6)
        png = call(sid, "GET", "/screenshot")
        live_shot = os.path.join(ROOT, "temp", "eph205_live.png")
        with open(live_shot, "wb") as f:
            f.write(base64.b64decode(png))
        print("  实时读数截图:", live_shot)
        summary = dict(zip(stats["labels"], stats["stats"]))
        expect = {"进度": "7/7", "通过": "5", "失败": "1", "测量失败": "0"}
        bad = {k: (summary.get(k), v) for k, v in expect.items()
               if summary.get(k) != v}
        if bad:
            raise RuntimeError(f"统计不符: {bad}")
        print("UI 统计符合预期")

        # WS 数据流：测量过程中服务端逐条推送，页面应显示推送条数
        live = js(sid, """
            const m = document.body.innerText.match(/已推送 (\\d+) 条/);
            return m ? Number(m[1]) : -1;
        """)
        print("  实时读数推送:", live, "条")
        if live <= 0:
            raise RuntimeError("WS 数据流没有收到读数")

        # 其余页面冒烟：逐个点开，确认没有白屏/异常
        for tab, marker in (
            ("调试", "单点测量"),
            ("历史", "历史记录"),
            ("工具", "运行信息"),
            ("帮助", "使用说明"),
        ):
            el = wait_xpath(sid, f"//button[normalize-space(.)='{tab}']", label=tab)
            call(sid, "POST", f"/element/{el}/click", {})
            wait_xpath(sid, f"//*[normalize-space(text())='{marker}']",
                       label=marker)
            print(f"  页面「{tab}」正常")
            if tab == "历史":
                for _ in range(80):
                    count = js(sid, """return document.querySelectorAll(
                        "[data-page='history'] tbody tr input[type=checkbox]").length;""")
                    if count >= 2:
                        break
                    time.sleep(0.25)
                else:
                    raise RuntimeError("历史记录不足两条，无法对比")
                # 一次性在页面里点两个复选框，避免两次点击之间表格重建导致元素失效
                js(sid, """
                    const boxes = document.querySelectorAll(
                        "[data-page='history'] tbody tr input[type=checkbox]");
                    boxes[0].click();
                    boxes[1].click();
                    return true;
                """)
                for _ in range(40):
                    if js(sid, "return document.body.innerText.includes('已选 2 条');"):
                        break
                    time.sleep(0.25)
                btn = wait_xpath(sid, "//button[normalize-space(.)='对比']")
                call(sid, "POST", f"/element/{btn}/click", {})
                wait_xpath(sid, "//*[normalize-space(text())='测量对比']",
                           label="对比抽屉")
                wait_xpath(sid, "//*[contains(text(), '最大差值')]",
                           label="最大差值")
                print("  两条记录对比打开正常")
                close = wait_xpath(sid, "//button[contains(@class,'close')]")
                click_js(sid, close)
                time.sleep(0.6)
            if tab == "调试":
                # 点检测位 0x07：仿真器未实现，应返回"命令不支持"
                btn = wait_xpath(sid, "//button[normalize-space(.)='点检']")
                call(sid, "POST", f"/element/{btn}/click", {})
                wait_xpath(sid, "//*[contains(text(), '命令不支持')]",
                           label="命令不支持")
                print("  点检测位返回「命令不支持」")
        el = wait_xpath(sid, "//button[normalize-space(.)='测试']")
        call(sid, "POST", f"/element/{el}/click", {})
        time.sleep(0.4)

        png = call(sid, "GET", "/screenshot")
        with open(SHOT, "wb") as f:
            f.write(base64.b64decode(png))
        print("截图:", SHOT)

        # 角色：退出后以 operator 登录，调试页应消失、仿真按钮应禁用
        print("\n[7] 角色校验")
        click_text(sid, "退出")
        login(sid, "operator", "operator")
        wait_xpath(sid, "//span[contains(@class,'tag')][contains(normalize-space(.), '操作员')]",
                   label="操作员标识")
        try:
            find(sid, "//button[normalize-space(.)='调试']")
        except Exception:
            print("  操作员看不到「调试」页")
        else:
            raise RuntimeError("操作员不应看到调试页")
        disabled = js(sid, """
            const b = [...document.querySelectorAll('button')]
              .find(x => x.innerText.trim() === '停止仿真' || x.innerText.trim() === '启动仿真');
            return b ? b.disabled : null;
        """)
        if disabled is not True:
            raise RuntimeError("操作员的仿真按钮应被禁用")
        print("  操作员的仿真按钮已禁用")
        png = call(sid, "GET", "/screenshot")
        with open(os.path.join(ROOT, "temp", "eph205_operator.png"), "wb") as f:
            f.write(base64.b64decode(png))

    finally:
        call(sid, "DELETE", "")
        if SERVER_PROC is not None:
            SERVER_PROC.terminate()
            SERVER_PROC.wait(timeout=10)


if __name__ == "__main__":
    main()
