#!/usr/bin/env python3
"""End-to-end smoke test for the DEVWORLD C++ server against the real stores."""
import base64
import json
import os
import socket
import sys
import time
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:8091"
PW = "devworld_dev_pw"
ADMIN_KEY = os.environ.get("ADMIN_KEY", "")
if not ADMIN_KEY:
    sys.exit("e2e: set ADMIN_KEY to the server's admin key (admin routes are disabled without one)")


def call(method, path, body=None, token=None, headers=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def resp_cmd(sock, *args):
    parts = []
    for a in args:
        b = a if isinstance(a, bytes) else str(a).encode()
        parts.append(b)
    wire = b"*%d\r\n" % len(parts) + b"".join(
        b"$%d\r\n%s\r\n" % (len(p), p) for p in parts
    )
    sock.sendall(wire)
    return sock.recv(1 << 20)


def redis_get(port, key):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    resp_cmd(s, "AUTH", PW)
    reply = resp_cmd(s, "GET", key)
    s.close()
    if reply.startswith(b"$-1"):
        return None
    # $<len>\r\n<data>\r\n
    head, rest = reply.split(b"\r\n", 1)
    n = int(head[1:])
    return rest[:n]


def clickhouse(sql):
    data = sql.encode()
    req = urllib.request.Request(
        "http://127.0.0.1:8123/?database=devworld", data=data, method="POST"
    )
    req.add_header("X-ClickHouse-User", "devworld")
    req.add_header("X-ClickHouse-Key", PW)
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read().decode().strip()


def read_frame(sock, buf):
    while len(buf) < 2:
        buf += sock.recv(65536)
    b0, b1 = buf[0], buf[1]
    n = b1 & 0x7F
    off = 2
    if n == 126:
        while len(buf) < 4:
            buf += sock.recv(65536)
        n = int.from_bytes(buf[2:4], "big")
        off = 4
    elif n == 127:
        while len(buf) < 10:
            buf += sock.recv(65536)
        n = int.from_bytes(buf[2:10], "big")
        off = 10
    while len(buf) < off + n:
        buf += sock.recv(65536)
    return b0 & 0x0F, buf[off:off + n], buf[off + n:]


def send_masked(sock, payload):
    msg = payload.encode()
    mask = os.urandom(4)
    frame = bytes([0x81, 0x80 | len(msg)]) + mask + bytes(
        b ^ mask[i % 4] for i, b in enumerate(msg)
    )
    sock.sendall(frame)


def main():
    ok = 0
    user = "e2e_%d" % (int(time.time()) % 1_000_000)

    st, health = call("GET", "/health")
    assert st == 200 and health["ok"], health
    print("health:", health)
    ok += 1

    st, reg = call(
        "POST",
        "/auth/register",
        {"username": user, "password": "password123", "displayName": "E2E Runner"},
    )
    assert st == 201, (st, reg)
    token, pid = reg["token"], reg["player"]["id"]
    assert "passwordHash" not in json.dumps(reg)
    print("register: 201 id=%s token=%d chars" % (pid, len(token)))
    ok += 1

    st, dup = call("POST", "/auth/register",
                   {"username": user, "password": "password123"})
    assert st == 409, (st, dup)
    st, bad = call("POST", "/auth/login",
                   {"username": user, "password": "wrong-password"})
    assert st == 401, (st, bad)
    st, login = call("POST", "/auth/login",
                     {"username": user, "password": "password123"})
    assert st == 200 and login["token"], login
    print("login: wrong=401 correct=200")
    ok += 1

    st, me = call("GET", "/me", token=token)
    assert st == 200 and me["username"] == user and me["progression"]["level"] == 1
    print("me:", {"id": me["id"], "level": me["progression"]["level"],
                  "balance": me["balance"]})
    ok += 1

    st, missions = call("GET", "/missions")
    assert st == 200 and len(missions["missions"]) == 53
    hidden = [t for m in missions["missions"] for t in m["tests"] if t["hidden"]]
    assert hidden and all("expected" not in t for t in hidden)
    print("missions: %d, hidden tests leak nothing" % len(missions["missions"]))
    ok += 1

    entries = [
        {"ref": "e2e-1", "playerId": pid, "account": "system", "amount": -25},
        {"ref": "e2e-1", "playerId": pid, "account": "wallet", "amount": 25},
    ]
    st, led = call("POST", "/admin/ledger", {"entries": entries},
                   headers={"X-Admin-Key": ADMIN_KEY})
    assert st == 200, (st, led)
    st, bal = call("GET", "/ledger/balance", token=token)
    assert st == 200 and bal["balance"] == 25, (st, bal)
    bad_entries = [
        {"ref": "e2e-bad", "playerId": pid, "account": "system", "amount": -5},
        {"ref": "e2e-bad", "playerId": pid, "account": "wallet", "amount": 4},
    ]
    st, _ = call("POST", "/admin/ledger", {"entries": bad_entries},
                 headers={"X-Admin-Key": ADMIN_KEY})
    assert st == 400, st
    print("ledger: balanced=200 balance=25 unbalanced=400")
    ok += 1

    # Unauthenticated and CORS preflight.
    st, _ = call("GET", "/me")
    assert st == 401
    req = urllib.request.Request(BASE + "/auth/login", method="OPTIONS")
    with urllib.request.urlopen(req, timeout=5) as r:
        assert r.status == 204
        assert r.headers["access-control-allow-origin"] == "*"
    print("auth guard + CORS preflight: ok")
    ok += 1

    # World event log: admin writes, an authenticated player reads it back.
    st, _ = call("POST", "/events", {"type": "company.founded", "title": "Acme"})
    assert st == 401
    st, _ = call("GET", "/events")
    assert st == 401
    st, created = call(
        "POST",
        "/events",
        {"type": "company.founded", "title": "Acme", "city": "startup_city"},
        headers={"X-Admin-Key": ADMIN_KEY},
    )
    assert st == 201 and created["type"] == "company.founded", created
    st, log = call("GET", "/events?type=company.founded&limit=5", token=token)
    assert st == 200 and log["events"][0]["title"] == "Acme", log
    st, world = call("GET", "/events?limit=100", token=token)
    types = [e["type"] for e in world["events"]]
    assert "company.founded" in types and "player.joined" in types, types
    print("world events: admin record + filtered, newest-first read-back")
    ok += 1

    # WebSocket: handshake, hello frame, ping/pong.
    key = base64.b64encode(os.urandom(16)).decode()
    s = socket.create_connection(("127.0.0.1", 8091), timeout=10)
    s.sendall(
        (
            "GET /ws?token=%s HTTP/1.1\r\n"
            "Host: 127.0.0.1:8091\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n" % (token, key)
        ).encode()
    )
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    head, rest = buf.split(b"\r\n\r\n", 1)
    assert b"101" in head.split(b"\r\n")[0], head
    accept = [
        line.split(b": ", 1)[1].decode()
        for line in head.split(b"\r\n")[1:]
        if line.lower().startswith(b"sec-websocket-accept:")
    ][0]
    expected = base64.b64encode(
        __import__("hashlib").sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()
        ).digest()
    ).decode()
    assert accept == expected, (accept, expected)
    conn = [
        line.split(b": ", 1)[1].decode()
        for line in head.split(b"\r\n")[1:]
        if line.lower().startswith(b"connection:")
    ][0]
    assert conn.lower() == "upgrade", conn  # a browser rejects 101 + keep-alive
    op, payload, rest = read_frame(s, rest)
    hello = json.loads(payload)
    assert hello["type"] == "hello" and hello["playerId"] == pid, hello
    # A newcomer immediately gets the shared-world snapshot, then pong.
    op, payload, rest = read_frame(s, rest)
    snapshot = json.loads(payload)
    assert snapshot["type"] == "world.snapshot", snapshot
    assert isinstance(snapshot.get("players"), list), snapshot
    send_masked(s, '{"type":"ping"}')
    op, payload, rest = read_frame(s, rest)
    pong = json.loads(payload)
    assert pong["type"] == "pong", pong
    s.close()
    print("websocket: 101 + hello + snapshot + pong")
    ok += 1

    # Durable state in Kvrocks.
    raw = redis_get(6666, "player:" + pid)
    assert raw and json.loads(raw)["username"] == user
    index = redis_get(6666, "username:" + user)
    assert index and index.decode() == pid
    print("kvrocks: player record + username index persisted")
    ok += 1

    # Append-only history in ClickHouse.
    events = clickhouse(
        "SELECT event FROM events WHERE player_id = '%s' FORMAT TSV" % pid
    )
    assert "player.registered" in events, events
    ledger = clickhouse(
        "SELECT sum(balance) FROM mv_player_balance WHERE player_id = '%s' "
        "AND account = 'wallet' FORMAT TSV" % pid
    )
    assert int(ledger) == 25, ledger
    print("clickhouse: registered event + wallet balance %s" % ledger)
    ok += 1

    print("\nE2E PASS: %d checks" % ok)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as e:
        print("E2E FAIL:", e)
        sys.exit(1)
