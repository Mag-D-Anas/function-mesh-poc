#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
import time


def add(a, b):
    return a + b


def multiply(a, b):
    return a * b


def slow_add(a, b):
    time.sleep(0.5)
    return a + b


PORT = int(os.getenv("PORT", "7777"))
WORKER_NAME = os.getenv("WORKER_NAME", "worker")
ENABLE_ADD = os.getenv("ENABLE_ADD", "1") == "1"
ENABLE_MULTIPLY = os.getenv("ENABLE_MULTIPLY", "1") == "1"
ENABLE_SLOW_ADD = os.getenv("ENABLE_SLOW_ADD", "1") == "1"

FUNCTIONS = {}

if ENABLE_ADD:
    FUNCTIONS["add"] = {
        "func": add,
        "async": False,
        "args": [{"name": "a", "type": {"name": "float", "id": 6}}, {"name": "b", "type": {"name": "float", "id": 6}}],
        "ret": {"type": {"name": "float", "id": 6}},
    }

if ENABLE_MULTIPLY:
    FUNCTIONS["multiply"] = {
        "func": multiply,
        "async": False,
        "args": [{"name": "a", "type": {"name": "float", "id": 6}}, {"name": "b", "type": {"name": "float", "id": 6}}],
        "ret": {"type": {"name": "float", "id": 6}},
    }

if ENABLE_SLOW_ADD:
    FUNCTIONS["slow_add"] = {
        "func": slow_add,
        "async": True,
        "args": [{"name": "a", "type": {"name": "float", "id": 6}}, {"name": "b", "type": {"name": "float", "id": 6}}],
        "ret": {"type": {"name": "float", "id": 6}},
    }


class Handler(BaseHTTPRequestHandler):
    def _send_json(self, code, payload):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(payload).encode())

    def do_GET(self):
        if self.path == "/inspect":
            funcs = []
            for name, info in FUNCTIONS.items():
                funcs.append({"name": name, "signature": {"ret": info["ret"], "args": info["args"]}, "async": info["async"]})
            data = {"py": [{"name": f"{WORKER_NAME}.py", "scope": {"name": "global_namespace", "funcs": funcs, "classes": [], "objects": []}}]}
            self._send_json(200, data)
            return
        if self.path == "/health":
            self._send_json(200, {"status": "ok", "worker": WORKER_NAME})
            return
        self._send_json(200, {"worker": WORKER_NAME, "status": "up"})

    def do_POST(self):
        parts = self.path.strip("/").split("/")
        if len(parts) >= 2 and parts[0] in ("call", "await"):
            func_name = parts[1]
            if func_name in FUNCTIONS:
                length = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(length).decode() if length else "[]"
                args = json.loads(raw) if raw else []
                result = FUNCTIONS[func_name]["func"](*args)
                self._send_json(200, result)
                return
        self._send_json(404, {"error": "function-not-found"})


if __name__ == "__main__":
    print(f"[{WORKER_NAME}] listening on {PORT}, functions: {list(FUNCTIONS.keys())}")
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
