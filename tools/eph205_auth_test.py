"""角色权限校验（服务端强制，不只是界面隐藏）。

前提：服务端以 --auth 启动。

  python -I tools/eph205_auth_test.py
"""

import base64
import json
import os
import socket
import sys
import urllib.error
import urllib.request
from urllib.parse import urlparse

sys.stdout.reconfigure(encoding="utf-8")

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8790"
SID = "auth-test"


def open_ws():
    u = urlparse(BASE)
    sock = socket.create_connection((u.hostname, u.port or 80), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET /ws?sid={SID} HTTP/1.1\r\n"
        f"Host: {u.hostname}:{u.port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("WS 握手失败")
        buf += chunk
    return sock


def api(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    headers = {"X-Session-Id": SID}
    if data is not None:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(BASE + path, data=data, headers=headers,
                                 method=method)
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            raw = resp.read()
            return resp.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as exc:
        raw = exc.read()
        try:
            return exc.code, json.loads(raw)
        except Exception:
            return exc.code, raw.decode("utf-8", "replace")


def command(action, payload=None):
    return api("POST", f"/api/v1/commands/eph205/{action}", payload or {})


def main():
    sock = open_ws()
    print("WS 会话:", SID)

    status, me = api("GET", "/api/v1/auth/me")
    print("未登录 /auth/me:", status, me)

    status, out = api("POST", "/api/v1/auth/login",
                      {"username": "operator", "password": "operator"})
    print("operator 登录:", status, out)
    assert out["role"] == "operator"

    status, out = command("raw", {"cmd": 1, "data": ""})
    print("operator raw:", status, out.get("msg"))
    assert status == 403, "operator 不应能发原始命令"

    status, out = command("ports")
    print("operator ports:", status)
    assert status == 200, "operator 应该能查串口"

    status, out = api("POST", "/api/v1/auth/login",
                      {"username": "admin", "password": "admin"})
    print("admin 登录:", status, out["role"])

    # 设备没连接时 raw 会失败，但不应是 403
    status, out = command("raw", {"cmd": 5, "data": ""})
    print("admin raw:", status, out)
    assert status != 403, "admin 不应被权限拦截"

    status, out = api("POST", "/api/v1/auth/logout")
    print("登出:", status, out)

    sock.close()
    print("\n角色校验通过")


if __name__ == "__main__":
    main()
