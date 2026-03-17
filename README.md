# Function Mesh PoC

This directory contains the proof of concept for function mesh routing using:

- Docker Compose orchestration
- A standalone C++ routing hub
- Hardcoded function workers
- A MetaCall client test harness

## Scope

PoC intentionally keeps scope narrow:

- Single-language flow for mesh behavior validation
- Hardcoded function registry and worker endpoints
- No Kubernetes or Function Mesh operator in this phase
- No compiler/extractor automation in this phase

## Layout

```text
function_mesh/
  docker-compose.yml
  .env.example
  services/
    routing_hub_cpp/
    worker_a_py/
    worker_b_py/
    worker_c_py/
  client/
```

## Quick Start

1. Start the PoC stack:
   - `docker compose up --build`
2. Hub endpoint:
   - `http://localhost:8080`
3. Worker health endpoints:
   - `http://localhost:7777/`
   - `http://localhost:7778/`
   - `http://localhost:7779/`

The intended client flow is:

- Client points to hub URL descriptor
- Hub resolves and forwards function calls to workers
- Workers expose `/inspect`, `/call/<fn>`, and `/await/<fn>`
