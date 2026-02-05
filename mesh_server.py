#!/usr/bin/env python3
"""Simple RPC server for Function Mesh PoC"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import time

# Functions to expose
def add(a, b):
    return a + b

def multiply(a, b):
    return a * b

def slow_add(a, b):
    """Async function - simulates slow computation"""
    time.sleep(0.5)
    return a + b

FUNCTIONS = {
    "add": {
        "func": add,
        "async": False,
        "args": [
            {"name": "a", "type": {"name": "float", "id": 6}},
            {"name": "b", "type": {"name": "float", "id": 6}}
        ],
        "ret": {"type": {"name": "float", "id": 6}}
    },
    "multiply": {
        "func": multiply,
        "async": False,
        "args": [
            {"name": "a", "type": {"name": "float", "id": 6}},
            {"name": "b", "type": {"name": "float", "id": 6}}
        ],
        "ret": {"type": {"name": "float", "id": 6}}
    },
    "slow_add": {
        "func": slow_add,
        "async": True,  # Async function!
        "args": [
            {"name": "a", "type": {"name": "float", "id": 6}},
            {"name": "b", "type": {"name": "float", "id": 6}}
        ],
        "ret": {"type": {"name": "float", "id": 6}}
    }
}

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/inspect":
            funcs = []
            for name, info in FUNCTIONS.items():
                funcs.append({
                    "name": name,
                    "signature": {"ret": info["ret"], "args": info["args"]},
                    "async": info["async"]
                })
            data = {"py": [{"name": "mesh.py", "scope": {"name": "global_namespace", "funcs": funcs, "classes": [], "objects": []}}]}
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(data).encode())
        else:
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")

    def do_POST(self):
        # Handle both /call/func and /await/func
        parts = self.path.strip("/").split("/")
        if len(parts) >= 1:
            func_name = parts[-1]
            call_type = parts[0] if len(parts) > 1 else "call"
            
            if func_name in FUNCTIONS:
                length = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(length).decode() if length else "[]"
                args = json.loads(body) if body else []
                
                result = FUNCTIONS[func_name]["func"](*args)
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps(result).encode())
                print(f"[{call_type}] {func_name}({args}) = {result}")
                return
        
        self.send_response(404)
        self.end_headers()

if __name__ == "__main__":
    print("Server running on http://localhost:7777")
    print("Functions: add, multiply (sync), slow_add (async)")
    HTTPServer(("localhost", 7777), Handler).serve_forever()
