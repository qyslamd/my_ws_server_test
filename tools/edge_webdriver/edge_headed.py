import json
import sys
import time
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")
DRIVER = "http://127.0.0.1:9515"
SERVER = "http://127.0.0.1:8780/"
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"


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


wait(SERVER + "api/v1/health")
wait(DRIVER + "/status")
caps = {"capabilities": {"alwaysMatch": {
    "browserName": "MicrosoftEdge",
    "ms:edgeOptions": {"binary": EDGE, "args": [
        "--auto-open-devtools-for-tabs", "--no-first-run",
        "--window-size=1400,900", "--remote-allow-origins=*"]}}}}
sid = http("POST", DRIVER + "/session", caps)["value"]["sessionId"]
print("session:", sid)
try:
    call(sid, "POST", "/url", {"url": SERVER})
    time.sleep(4)
    state = call(sid, "POST", "/execute/sync", {
        "script": "return {status: document.getElementById('status').textContent};",
        "args": []})
    print("state:", json.dumps(state, ensure_ascii=False))
    call(sid, "POST", "/execute/sync", {
        "script": "console.log('[WebDriver] headed Edge + DevTools');",
        "args": []})
    print("headed Edge + DevTools open; hold 60s")
    time.sleep(60)
finally:
    call(sid, "DELETE", "")
