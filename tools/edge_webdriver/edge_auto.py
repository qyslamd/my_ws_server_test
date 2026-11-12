import base64
import json
import sys
import time
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")
DRIVER = "http://127.0.0.1:9515"
SERVER = "http://127.0.0.1:8780/"
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
SHOT = r"D:\trae_workspace\my_ws_server_test\temp\edge_auto.png"


def http(method, url, data=None, timeout=20):
    body = json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(url, data=body, method=method,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read()
    return json.loads(raw) if raw else None


def wait(url, tries=80):
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


wait(SERVER + "api/v1/health")
wait(DRIVER + "/status")
caps = {"capabilities": {"alwaysMatch": {
    "browserName": "MicrosoftEdge",
    "ms:edgeOptions": {"binary": EDGE, "args": [
        "--headless=new", "--disable-gpu", "--no-first-run",
        "--window-size=1280,800", "--remote-allow-origins=*"]}}}}
sid = http("POST", DRIVER + "/session", caps)["value"]["sessionId"]
print("session:", sid)
try:
    call(sid, "POST", "/url", {"url": SERVER})
    for _ in range(40):
        state = js(sid, """
            return {
              status: document.getElementById('status').textContent,
              capabilities: document.getElementById('capabilities').textContent,
              modules: document.getElementById('modules').textContent,
              streams: document.getElementById('streams').textContent,
              log: document.getElementById('log').textContent
            };
        """)
        if state["status"] not in ("未连接", "连接中…"):
            break
        time.sleep(0.25)
    print("title:", call(sid, "GET", "/title"))
    print("status:", state["status"])
    print("capabilities:", state["capabilities"][:120].replace("\n", " "))
    print("modules:", state["modules"].replace("\n", " "))
    print("streams:", state["streams"].replace("\n", " "))
    print("log:", state["log"].split("\n")[0])
    png = call(sid, "GET", "/screenshot")
    with open(SHOT, "wb") as f:
        f.write(base64.b64decode(png))
    print("screenshot:", SHOT)
finally:
    call(sid, "DELETE", "")
