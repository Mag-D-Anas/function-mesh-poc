# Function Mesh PoC

This directory contains the proof of concept for function mesh routing using MetaCall's
RPC loader as the invocation engine inside the routing hub.

- Docker Compose orchestration
- A C++ routing hub powered by MetaCall's RPC loader
- Python function workers
- A MetaCall client test (`mesh_client.c`)

## Architecture

The routing hub uses MetaCall's RPC loader internally to invoke functions on workers.
Each worker is loaded into a separate MetaCall handle during startup. Round-robin
selects which handle (and therefore which worker) serves each request.

```text
                         +-----------------+
  mesh_client ------>    | routing-hub     |
  (RPC loader)   HTTP    | :8080           |
                         |                 |
                         | metacall_init   |
                         | load handle_a --+---> worker-a:7777 (add, multiply)
                         | load handle_b --+---> worker-b:7778 (add, slow_add)
                         | load handle_c --+---> worker-c:7779 (add, multiply, slow_add)
                         |                 |
                         | Registry:       |
                         |   add -> [a,b,c]|  round-robin
                         |   multiply->[a,c]|
                         |   slow_add->[b,c]|
                         +-----------------+
```

### Startup Sequence

1. **Workers boot** -- Python HTTP servers exposing `/inspect`, `/call/<fn>`, `/await/<fn>`
2. **Hub calls `metacall_initialize()`** -- loads the serial plugin system
3. **Hub loads each worker via RPC loader**:
   - Writes worker URL to a temp `.url` file
   - `metacall_load_from_file("rpc", &url_file, 1, &handle)` triggers:
     - `rpc_loader.so` is loaded (which loads `rapid_json_serial.so`)
     - RPC loader GETs `<worker_url>/inspect` to discover functions
     - Functions are registered in the handle, each bound to that worker's URL
4. **Hub populates the Registry** -- maps function names to handles with round-robin

### Request Flow (e.g, `POST /call/add [100,200]`)

1. Hub receives the HTTP request
2. `registry_.next_handle("add")` picks a handle via round-robin (e.g., `handle_b`)
3. `metacall_handle_function(handle_b, "add")` gets the function bound to `worker-b:7778`
4. `metacallfs(func, "[100,200]", 10, allocator)`:
   - Deserializes `[100,200]` into MetaCall values via `rapid_json_serial`
   - Calls `metacallfv_s(func, args, 2)` which dispatches to `function_rpc_interface_invoke`
   - RPC loader serializes args -> curl POST to `http://worker-b:7778/call/add` -> deserializes response
5. Hub serializes the return value (`300`) back to JSON via `metacall_serialize`
6. Hub sends `300` to the client

### Important Notes

- **Hub does not use curl directly** for worker invocation -- all HTTP to workers goes through the RPC loader's `function_rpc_interface_invoke` inside `metacallfs`
- **Round-robin is on handles, not URLs** -- each handle's functions are bound to a specific worker during the discover phase
- **`/inspect` bypasses MetaCall** -- the hub's `Registry::inspect_payload()` returns its own JSON so the client can discover what the hub exposes

## Scope

PoC keeps scope narrow:

- Single-language flow for mesh behavior validation
- Hardcoded function registry and worker endpoints
- No Kubernetes or Function Mesh operator
- No compiler/extractor automation
## Layout

```text
function_mesh/
  docker-compose.yml
  README.md
  mesh_client.c              # MetaCall client test harness
  client/
    hub.url                  # URL file pointing to the hub
  services/
    routing_hub_cpp/
      CMakeLists.txt         # Links against libmetacall + libcurl
      Dockerfile             # ubuntu:24.04, installs MetaCall
      include/
        registry.hpp         # fn_name -> [handles] with round-robin
        router.hpp           # Routes requests through metacallfs
        policy.hpp           # Round-robin index helper
      src/
        main.cpp             # metacall_initialize, load workers, accept loop
        router.cpp           # metacallfs invoke path
        registry.cpp         # Handle-based registry
        policy_round_robin.cpp
    worker_a_py/             # Python worker (add, multiply)
    worker_b_py/             # Python worker (add, slow_add)
    worker_c_py/             # Python worker (add, multiply, slow_add)
```

## Quick Start

1. Start the docker services (`routing-hub`, `worker-a`, `worker-b`, `worker-c`)
```bash
docker compose up -d
```

2. Test:

```bash
# Insure successful docker build
docker ps

# Health check
curl http://localhost:8080/health

# Inspect (hub's own function catalog)
curl http://localhost:8080/inspect

# Sync call
curl -X POST http://localhost:8080/call/add -H "Content-Type: application/json" -d "[100,200]"

# Async call
curl -X POST http://localhost:8080/await/slow_add -H "Content-Type: application/json" -d "[10,20]"
```

3. `mesh_client` test (requires MetaCall installed on host):

```bash
cd function_mesh/
cmake -S client -B build
cmake --build build
./build/mesh_client
```

## Runtime Dependencies (Hub)

- `libmetacall.so` -- MetaCall core library
- `rpc_loader.so` -- loaded on demand when `metacall_load_from_file("rpc", ...)` is called
- `rapid_json_serial.so` -- loaded by the RPC loader via `serial_create("rapid_json")`
- `libcurl` -- used internally by the RPC loader for HTTP

Environment variables:
- `LOADER_LIBRARY_PATH` -- directory containing `rpc_loader.so`
- `SERIAL_LIBRARY_PATH` -- directory containing `rapid_json_serial.so`
- `WORKER_A_URL`, `WORKER_B_URL`, `WORKER_C_URL` -- worker base URLs
