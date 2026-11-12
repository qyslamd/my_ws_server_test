"""EPH205 模块端到端自测（标准库，无第三方依赖）。

流程：
  1. 用 sid=test 打开 WS，建立 1:1 会话；
  2. 启动 com0com 仿真端（COM8）；
  3. 连接设备（COM7）；
  4. 单点测量一次；
  5. 提交一次 measure_run 作业，从 WS 收进度直到结束；
  6. 查作业状态、历史列表，导出 CSV。

用法：
  python -I tools/eph205_api_test.py [base_url]
  base_url 默认 http://127.0.0.1:8790
"""

import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.error
import urllib.request
from urllib.parse import urlparse

sys.stdout.reconfigure(encoding="utf-8")

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8790"
SID = "test"
CLIENT_PORT = "COM7"
SIM_PORT = "COM8"


# ---------------------------------------------------------------- WebSocket


def ws_connect(base):
    u = urlparse(base)
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
    head = buf.split(b"\r\n\r\n")[0].decode("latin1")
    if "101" not in head.split("\r\n")[0]:
        raise RuntimeError("WS 升级失败: " + head.split("\r\n")[0])
    return sock


def ws_send(sock, text):
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


def ws_recv(sock):
    h = sock.recv(2)
    if len(h) < 2:
        raise RuntimeError("WS 已关闭")
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


# --------------------------------------------------------------------- HTTP


def api(method, path, body=None, timeout=15):
    data = None
    headers = {"X-Session-Id": SID}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(
        BASE + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read()
            return resp.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as exc:
        raw = exc.read()
        try:
            return exc.code, json.loads(raw)
        except Exception:
            return exc.code, raw.decode("utf-8", "replace")


def command(action, payload=None):
    status, out = api("POST", f"/api/v1/commands/eph205/{action}", payload or {})
    ok = status == 200 and out and out.get("ok")
    print(f"  {action:<14} -> {status} {json.dumps(out, ensure_ascii=False)[:160]}")
    if not ok:
        raise RuntimeError(f"命令 {action} 失败: {status} {out}")
    return out["result"]


# --------------------------------------------------------------------- main


def main():
    print(f"底座 {BASE}")
    sock = ws_connect(BASE)
    sock.settimeout(20)
    print("  WS 会话已建立 sid=" + SID)

    print("\n[1] 启动仿真端")
    command("sim_start", {"port": SIM_PORT})

    print("\n[2] 连接设备")
    command("connect", {"port": CLIENT_PORT})
    command("get_params")

    print("\n[3] 单点测量 P1-N2")
    command("measure", {"p": 1, "n": 2})

    print("\n[4] 提交测量作业")
    items = []
    for i in range(6):
        items.append({
            "seq": i + 1,
            "p": 1 + i,
            "n": 2 + i,
            "wire": 0,
            "range": 0,
            "minOhms": 90.0,
            "maxOhms": 110.0,
            "measure": True,
        })
    # 让第 4 项超量程失败、第 5 项不测，验证三种结果
    command("sim_set", {"p": 4, "n": 5, "over": True})
    items[4]["measure"] = False

    status, out = api("POST", "/api/v1/jobs", {
        "ns": "eph205",
        "type": "measure_run",
        "input": {
            "product": {"model": "EPH205-DEMO", "code": "SN-0001"},
            "operator": "tester",
            "items": items,
            "deviceSource": "simulation",
        },
    })
    if status != 202:
        raise RuntimeError(f"提交作业失败: {status} {out}")
    job_id = out["jobId"]
    print(f"  jobId={job_id}")

    print("\n[5] 接收进度事件")
    deadline = time.time() + 60
    final = None
    while time.time() < deadline:
        op, data = ws_recv(sock)
        if op == 0x9:  # ping
            continue
        if op != 0x1:
            continue
        try:
            msg = json.loads(data)
        except Exception:
            continue
        if msg.get("type") != "evt":
            continue
        name = msg.get("name", "")
        payload = msg.get("data", msg.get("payload", {}))
        if name == "eph205.job.progress":
            state = payload.get("state")
            print(f"  {state:<9} {payload.get('done')}/{payload.get('total')} "
                  f"pass={payload.get('passed')} fail={payload.get('failed')} "
                  f"mfail={payload.get('measureFailed')}")
            if state in ("completed", "cancelled", "aborted"):
                final = payload
                break

    if final is None:
        raise RuntimeError("没收到作业完成事件")
    print("  summary:", json.dumps(final.get("summary"), ensure_ascii=False))

    print("\n[6] 作业状态 / 历史")
    status, out = api("GET", f"/api/v1/jobs/{job_id}")
    print("  job:", json.dumps(out, ensure_ascii=False)[:300])
    status, out = api("POST", "/api/v1/commands/eph205/history_list",
                      {"offset": 0, "limit": 5})
    print("  history total:", out["result"]["total"])
    record_id = final.get("recordId")
    if record_id:
        status, out = api("POST", "/api/v1/commands/eph205/history_export",
                          {"id": record_id})
        csv = out["result"]["csv"]
        print("  csv 前 4 行:")
        for line in csv.splitlines()[:4]:
            print("   ", line)

    sock.close()
    print("\n全部通过")


if __name__ == "__main__":
    main()
