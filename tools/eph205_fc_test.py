"""流控回归：window:0 不推数据；额度耗尽暂停；归还额度后恢复。"""

import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.request
from urllib.parse import urlparse

sys.stdout.reconfigure(encoding="utf-8")

BASE = "http://127.0.0.1:8790"
SID = "fc-test"


def ws_connect():
    u = urlparse(BASE)
    sock = socket.create_connection((u.hostname, u.port), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall((f"GET /ws?sid={SID} HTTP/1.1\r\nHost: {u.hostname}:{u.port}\r\n"
                  "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += sock.recv(4096)
    return sock


def ws_send(sock, text):
    data = text.encode()
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    n = len(masked)
    header = bytes([0x81])
    if n < 126:
        header += bytes([0x80 | n])
    elif n < 65536:
        header += bytes([0x80 | 126]) + struct.pack(">H", n)
    else:
        header += bytes([0x80 | 127]) + struct.pack(">Q", n)
    sock.sendall(header + mask + masked)


def ws_recv(sock, timeout=1.0):
    sock.settimeout(timeout)
    try:
        h = sock.recv(2)
        if len(h) < 2:
            return None, b""
        op = h[0] & 0x0F
        ln = h[1] & 0x7F
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
    except socket.timeout:
        return None, b""


def api(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    headers = {"X-Session-Id": SID}
    if data:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(BASE + path, data=data, headers=headers,
                                 method=method)
    with urllib.request.urlopen(req, timeout=15) as resp:
        raw = resp.read()
        return json.loads(raw) if raw else None


def command(action, payload=None):
    return api("POST", f"/api/v1/commands/eph205/{action}", payload or {})["result"]


def count_frames(sock, seconds):
    """数一段时间的二进制帧，返回 (帧数, 负载字节数)。"""
    deadline = time.time() + seconds
    frames = 0
    payload = 0
    while time.time() < deadline:
        op, data = ws_recv(sock, timeout=max(0.05, deadline - time.time()))
        if op == 0x2:
            frames += 1
            payload += len(data)
    return frames, payload


def run_items(count):
    items = [{"seq": i + 1, "p": i + 1, "n": 2, "wire": 0, "range": 0,
              "minOhms": 0, "maxOhms": 0, "measure": True} for i in range(count)]
    return api("POST", "/api/v1/jobs", {
        "ns": "eph205", "type": "measure_run",
        "input": {"product": {"model": "FC"}, "operator": "t",
                  "items": items, "deviceSource": "simulation"}})["jobId"]


def main():
    sock = ws_connect()
    api("POST", "/api/v1/auth/login", {"username": "admin", "password": "admin"})
    if "仿真中" not in json.dumps(api("GET", "/api/v1/state")):
        pass
    command("sim_start", {"port": "COM8"})
    command("connect", {"port": "COM7"})

    print("[1] window:0 订阅 → 不应有任何数据帧")
    api("POST", "/api/v1/subscriptions", {"streams": [{"id": 1, "window": 0}]})
    run_items(200)
    time.sleep(2)
    frames, _ = count_frames(sock, 1.5)
    print("    收到帧数:", frames)
    assert frames == 0, "window:0 仍然在推数据"

    print("\n[2] window=1000 订阅，不归还额度 → 推一帧后暂停")
    api("DELETE", "/api/v1/subscriptions/1")
    out = api("POST", "/api/v1/subscriptions", {"streams": [{"id": 1, "window": 1000}]})
    print("    订阅窗口:", out["subscriptions"][0]["window"])
    run_items(200)
    frames, payload = count_frames(sock, 2.0)
    print("    收到帧数:", frames, "负载字节:", payload)
    assert frames >= 1, "有额度却一帧都没发"

    frames2, _ = count_frames(sock, 1.5)
    print("    额度耗尽后 1.5s 内新增帧数:", frames2)
    assert frames2 == 0, "额度耗尽后仍在推数据"

    print("\n[3] 归还额度 → 恢复推送")
    ws_send(sock, json.dumps({"type": "credit",
                              "streams": [{"id": 1, "bytes": 100000}]}))
    frames3, _ = count_frames(sock, 2.0)
    print("    归还后收到帧数:", frames3)
    assert frames3 >= 1, "归还额度后没有恢复推送"

    print("\n全部通过")
    sock.close()


if __name__ == "__main__":
    main()
