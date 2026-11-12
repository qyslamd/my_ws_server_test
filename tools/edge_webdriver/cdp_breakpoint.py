import base64
import json
import os
import socket
import struct
import subprocess
import sys
import time
import urllib.request
from urllib.parse import urlparse

sys.stdout.reconfigure(encoding="utf-8")
DEBUG_HOST = "127.0.0.1"
DEBUG_PORT = 9222
APP_URL = "http://127.0.0.1:8780/"
BREAK_LINE = 7  # app.js 第 8 行 (0-based)，log() 内
SHOT = r"D:\trae_workspace\my_ws_server_test\temp\cdp_paused.png"
DESKTOP = r"D:\trae_workspace\my_ws_server_test\temp\cdp_devtools.png"


def http_json(url, timeout=10):
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return json.loads(resp.read())


def wait_targets():
    for _ in range(80):
        try:
            targets = http_json(f"http://{DEBUG_HOST}:{DEBUG_PORT}/json")
            for t in targets:
                if t.get("type") == "page" and "8780" in t.get("url", ""):
                    return t
        except Exception:
            pass
        time.sleep(0.25)
    raise RuntimeError(
        "no page target; start Edge with "
        "--remote-debugging-port=9222 --auto-open-devtools-for-tabs")


def ws_connect(ws_url):
    u = urlparse(ws_url)
    sock = socket.create_connection((u.hostname, u.port), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET {u.path} HTTP/1.1\r\n"
        f"Host: {u.hostname}:{u.port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += sock.recv(4096)
    return sock


def send_text(sock, text):
    data = text.encode("utf-8")
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    header = bytes([0x81])
    n = len(masked)
    if n < 126:
        header += bytes([0x80 | n])
    elif n < 65536:
        header += bytes([0x80 | 126]) + struct.pack(">H", n)
    else:
        header += bytes([0x80 | 127]) + struct.pack(">Q", n)
    sock.sendall(header + mask + masked)


def recv_frame(sock):
    h = sock.recv(2)
    if len(h) < 2:
        raise RuntimeError("ws closed")
    b1, b2 = h[0], h[1]
    op = b1 & 0x0F
    ln = b2 & 0x7F
    if ln == 126:
        ln = struct.unpack(">H", sock.recv(2))[0]
    elif ln == 127:
        ln = struct.unpack(">Q", sock.recv(8))[0]
    data = b""
    while len(data) < ln:
        chunk = sock.recv(ln - len(data))
        if not chunk:
            break
        data += chunk
    return op, data


class Cdp:
    def __init__(self, sock):
        self.sock = sock
        self.next_id = 0
        self.events = []

    def send(self, method, params=None):
        self.next_id += 1
        msg = {"id": self.next_id, "method": method}
        if params is not None:
            msg["params"] = params
        send_text(self.sock, json.dumps(msg))
        return self.next_id

    def recv_one(self):
        while True:
            op, data = recv_frame(self.sock)
            if op == 0x9:
                continue
            if op == 0x8:
                raise RuntimeError("ws close")
            if op in (0x1, 0x2):
                return json.loads(data.decode("utf-8"))

    def wait_id(self, msg_id):
        while True:
            msg = self.recv_one()
            if msg.get("id") == msg_id:
                if "error" in msg:
                    raise RuntimeError(str(msg["error"]))
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def call(self, method, params=None):
        return self.wait_id(self.send(method, params))

    def wait_event(self, name, timeout=15):
        for i, e in enumerate(self.events):
            if e.get("method") == name:
                return self.events.pop(i)
        end = time.time() + timeout
        while time.time() < end:
            msg = self.recv_one()
            if msg.get("method") == name:
                return msg
            if "method" in msg:
                self.events.append(msg)
        raise RuntimeError("timeout waiting " + name)


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
                       timeout=20, check=False)
        print("desktop screenshot:", DESKTOP)
    except Exception as e:
        print("desktop screenshot failed:", e)


def dump_pause(event, cdp, label):
    pause = event.get("params", event)
    frames = pause.get("callFrames", [])
    print(f"\n--- {label}: paused ({pause.get('reason')}) ---")
    for i, f in enumerate(frames[:4]):
        loc = f.get("location", {})
        print(f"  #{i} {f.get('functionName') or '(anonymous)'} "
              f"@ {loc.get('lineNumber')}")
    if frames:
        res = cdp.call("Debugger.evaluateOnCallFrame", {
            "callFrameId": frames[0]["callFrameId"],
            "expression": "JSON.stringify({text: (typeof text !== 'undefined' ? text : null)})",
            "returnByValue": True,
            "silent": True,
        })
        print("  locals:", res.get("result", {}).get("value"))


target = wait_targets()
print("target:", target["url"])
sock = ws_connect(target["webSocketDebuggerUrl"])
cdp = Cdp(sock)
cdp.call("Page.enable")
cdp.call("Runtime.enable")
cdp.call("Debugger.enable")

bp = cdp.call("Debugger.setBreakpointByUrl", {
    "lineNumber": BREAK_LINE,
    "url": APP_URL + "app.js",
})
print("breakpoint:", json.dumps(bp, ensure_ascii=False))
breakpoint_id = bp.get("breakpointId")

# 不等待 evaluate 的响应：命中后进程暂停，响应会在 resume 后返回。
cdp.send("Runtime.evaluate", {"expression": "log('CDP breakpoint demo')"})
pause = cdp.wait_event("Debugger.paused", timeout=15)
dump_pause(pause, cdp, "hit")
time.sleep(4)
capture_desktop()
time.sleep(4)

for step in range(2):
    cdp.call("Debugger.stepOver")
    pause = cdp.wait_event("Debugger.paused", timeout=10)
    dump_pause(pause, cdp, f"step {step + 1}")
    time.sleep(6)

shot = cdp.call("Page.captureScreenshot", {"format": "png"})
with open(SHOT, "wb") as f:
    f.write(base64.b64decode(shot["data"]))
print("screenshot:", SHOT)

if breakpoint_id:
    cdp.call("Debugger.removeBreakpoint", {"breakpointId": breakpoint_id})
cdp.call("Debugger.resume")
print("resumed; holds 30s")
time.sleep(30)
sock.close()
