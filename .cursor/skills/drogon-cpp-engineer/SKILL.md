---
name: drogon-cpp-engineer
description: Develops, debugs, and scales high-performance Drogon C++ web apps—PostgreSQL via DbClient/ORM, RabbitMQ (AMQP-CPP) with Trantor event-loop integration, HttpController and WebSocketController, drogon_ctl scaffolding, config.json, HttpFilter middleware. Use when editing Drogon code, Postgres, messaging, drogon::app(), CMake, or C++ web APIs in this repository.
---

# Drogon C++ engineer

## Scope

Expertise for high-performance Drogon services: non-blocking I/O, PostgreSQL through Drogon’s async stack, RabbitMQ messaging, ORM usage, and safe scaling. Apply when working on the `tws` service or other Drogon-based C++ in this repo. Prefer patterns already used here; match existing namespaces and file layout.

## When to use

- Creating or modifying `drogon::HttpController` or `drogon::WebSocketController` classes.
- Using **`drogon_ctl`** for scaffolding (controllers, models, views).
- Configuring **`config.json`**: `db_clients`, logging, SSL/TLS, or static file settings.
- **PostgreSQL**: async queries, ORM models, transactions via `DbClient`.
- **RabbitMQ**: producers/consumers, durability, DLX, wiring to Drogon’s event loop.

## Project anchors

- **Build**: `find_package(Drogon CONFIG REQUIRED)`, link `Drogon::Drogon`, C++17 (`tws/CMakeLists.txt`).
- **Runtime**: Docker image `drogonframework/drogon:latest` (`tws/Dockerfile`); app listens on `0.0.0.0:8080` unless changed.
- **Controllers**: `drogon::HttpController<Derived>`, `METHOD_LIST_BEGIN` / `METHOD_ADD` / `METHOD_LIST_END`, route paths and HTTP methods in the macro (`tws/controllers/`).
- **Messaging in this repo**: **AMQP-CPP** linked in CMake (`tws/messaging/`, RabbitMQ broker)—follow existing loop integration there.

## Controllers

- Inherit `drogon::HttpController<YourController>` (CRTP). Declare handlers as member functions.
- Register routes with `METHOD_ADD(Class::handler, "/path/.../{param}", drogon::Get|Post|...)`; include `drogon::Options` when CORS preflight is needed. If the project also defines routes in **`config.json`**, keep controller macros and config in sync when both apply.
- **Async signature**: handlers take `const drogon::HttpRequestPtr &`, `std::function<void(const drogon::HttpResponsePtr &)> &&callback`, and path parameters as trailing arguments if routed with `{1}` etc.
- Respond with `drogon::HttpResponse::newHttpJsonResponse(Json::Value)` or other `newHttp*` factories; **always** return a `HttpResponsePtr` by invoking `callback(response)` on every path (including errors).

## Database: PostgreSQL (Drogon ORM)

- Use **`drogon::DbClient`** for all database I/O so work stays on Drogon’s asynchronous, non-blocking engine (never raw blocking PG calls in handlers).
- **Query style**: Prefer **`execSqlCoro`** for readable async SQL when the toolchain supports it (coroutines require a sufficient C++ standard and Drogon build—this repo is **C++17**, so use **`execSqlAsync`** / callback-based APIs here unless the project is upgraded). When coroutines are available, prefer `execSqlCoro` over callback-heavy `execSqlAsync` in new code.
- **Models**: Run **`drogon_ctl create model <path>`** against your Postgres schema to generate C++ model classes; then use **`Mapper<ModelName>(dbClient)`** for type-safe access.
- **Transactions**: For multi-step operations, use a transaction from **`dbClient->newTransaction()`** (or equivalent API for your Drogon version) so changes stay ACID.
- Hold **`DbClient`** via **`std::shared_ptr`** and use Drogon’s **connection pools**—do not open ad-hoc connections per request (see **Critical constraints**).

## Messaging: RabbitMQ integration

- **Client choice**:
  - **AMQP-CPP** (used here): modern async-friendly C++ API; **bind it to Drogon’s `trantor::EventLoop`** (e.g. `drogon::app().getIOLoop(index)`) so I/O does not block HTTP threads—mirror `tws/messaging/` patterns.
  - **SimpleAmqpClient**: simpler but **synchronous**; **never** call its blocking methods from **`HttpController`** handlers (stalls the whole event loop). If used at all, isolate on a worker thread and post results back to Drogon.
- **Reliability**: Prefer **durable** queues and **persistent** messages (delivery mode 2) where the domain requires it. Use a **dead-letter exchange (DLX)** for poison/failed messages instead of silent drops.
- **Patterns**:
  - **Producer**: Initialize publishing through a **shared service** (or equivalent) at app startup—e.g. registry + `registerBeginningAdvice`—so handlers use a long-lived client, not per-request connects.
  - **Consumer**: Run consumption on the **correct loop or thread**: schedule AMQP-CPP callbacks on a **`trantor::EventLoop`** tied to Drogon (`getIOLoop`), or a dedicated thread that posts back to Drogon for HTTP-visible state—avoid competing with request handling on the same tight blocking path.

## WebSocket controllers

- For real-time endpoints, subclass `drogon::WebSocketController<Derived>` and register WebSocket routes per Drogon docs; same non-blocking discipline as HTTP handlers.

## Middleware

- Implement **`drogon::HttpFilter`** subclasses for cross-cutting concerns (headers, auth, logging). Register filters on routes or globally as the project pattern dictates.

## Views (CSP)

- **`.csp`** templates are compiled into C++—edit **source** `.csp` files. **Do not** hand-edit generated `.h` / `.cc` under `filters` or `views` build outputs; regenerate via the build / `drogon_ctl` instead.

## Inline handlers and app hooks

- `drogon::app().registerHandler(path, lambda, {methods})` for ad-hoc routes without a controller class.
- `registerBeginningAdvice`, `registerPostHandlingAdvice`, etc. for startup/shutdown ordering; use for wiring subsystems that must start after the framework is ready.

## Threading and external I/O

- Respect `drogon::app().getThreadNum()` and **Trantor** I/O loops when bridging non-Drogon async libraries: schedule work with `drogon::app().getIOLoop(index)` where the codebase already does so (see messaging integration).
- Avoid blocking the HTTP thread pool on long synchronous work; defer to worker threads or async completion then post back to Drogon.

## Configuration

- Prefer environment-driven config consistent with `env.example` / compose for ports and external services.
- **`config.json` — Postgres**: Under **`db_clients`**, define **`name`**, **`host`**, **`port`**, **`database_name`**, and pool settings as needed for your Drogon version; align names with code that calls `app().getDbClient("name")`.
- **Secrets**: Keep **`PG_PASSWORD`**, **`RABBITMQ_URL`**, and similar in **environment variables** (or secrets injection), not committed plaintext in `config.json`.
- Other Drogon-native settings (SSL, log levels, listeners) also live in `config.json` when used; this project may still set some options in code (`addListener`, `setThreadNum`).

## Common commands

```bash
# Scaffold
drogon_ctl create controller <Name>
drogon_ctl create model <path/to/model>

# Typical local build
mkdir -p build && cd build && cmake .. && cmake --build .

# Run (replace with binary name, e.g. tws)
./<project_name>
```

## Checklist for new endpoints

1. Add or extend an `HttpController` with `METHOD_ADD` and correct path/method (and `config.json` if the project routes there too).
2. Parse body/query with `request` APIs; validate inputs before side effects.
3. Return JSON or appropriate status via `callback`; handle errors with proper HTTP codes.
4. If the change touches startup order, use advice callbacks or registry patterns consistent with `main.cc`.
5. For DB: use **`DbClient`** pool + async APIs / `Mapper`; never open new DB connections per request.
6. For RabbitMQ: use shared broker/client from startup wiring; integrate with `trantor::EventLoop` as in existing messaging code.

## Critical constraints

- **Connection leakage**: Do **not** create a new Postgres or RabbitMQ connection inside a request handler. Use Drogon’s **`DbClient`** pools and long-lived messaging clients initialized at startup.
- **Blocking calls**: Do **not** use synchronous **SimpleAmqpClient** (or other blocking AMQP) APIs inside **`HttpController`** code paths—they block the event loop and stall all requests.
- **Callback completeness**: Omitting `callback` on a branch still hangs the client; same discipline for async DB/messaging completions before responding.

## Anti-patterns

- Blocking DB or AMQP in handlers without async completion or off-thread posting.
- Per-request DB or broker connects.
- Omitting `callback` on some branches (hangs the client).
- Mixing arbitrary threading with Drogon without posting back to the event loop when touching framework objects or response completion.
- Editing generated view/filter sources instead of `.csp` or regenerating.
