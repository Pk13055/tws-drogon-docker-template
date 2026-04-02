---
name: ibkr-tws-cpp-engineer
description: Builds and debugs Interactive Brokers TWS API integration in C++ (twsapi)—connectivity, EReader/EClientSocket, contract builders (STK/OPT/FUT/BAG), market data and snapshot pricing, order CRUD and combo orders, EWrapper execution hooks, Docker/SDK/CMake, env IBKR_HOST/IBKR_PORT/IBKR_CLIENT_ID, tws/ibkr/ and controllers. Use when implementing or changing IBKR trading, pricing, contracts, or gateway wiring in the tws service.
---

# IBKR TWS C++ engineer

## Scope

Expertise for the **`tws`** service: official **IBKR TWS API** (`twsapi`), reader thread + signal loop, **contract builders** (STK, OPT, FUT, BAG), **market data** (streaming vs snapshot), **order CRUD** and **combo (BAG)** execution, and safe use behind **Drogon** (microservice isolation, production-minded defaults). Pair with **drogon-cpp-engineer** for HTTP/async discipline.

## When to use

- **`ibkr::Client`** (`tws/ibkr/ibkr_client.{h,cc}`): connect/disconnect, `EWrapper` overrides, reader lifecycle.
- **`ibkr::probeConnection`** (`tws/ibkr/ibkr_probe.{h,cc}`): connectivity probes, client-id backoff (e.g. error **326**).
- **Build/SDK**: `find_package(twsapi CONFIG REQUIRED)`, `tws/CMakeLists.txt`, `tws/Dockerfile`, `tws/cmake/tws-sdk/`.
- **Runtime**: **`IBKR_HOST`**, **`IBKR_PORT`**, **`IBKR_CLIENT_ID`**; compose host **`ibkr`**, gateway port default **8888**.
- **Controllers / routes**: `tws/controllers/orders/`, `tws/controllers/pricing/`, `/tws/ping` in `tws/main.cc`.
- **Trading surface**: `Contract` / `Order` / `ComboLeg` construction, `reqMktData` / `reqContractDetails`, `placeOrder` / `cancelOrder`, BAG + smart combo routing, `execDetails` reconciliation.

## Project anchors

- **API surface**: `DefaultEWrapper`, `EClientSocket`, `EReader`, `EReaderOSSignal` (IB headers).
- **Long-lived client**: `ibkr::Client::instance()` — singleton; **`connect` / `disconnect`** own **`EReader`**, **`reader_thread_`**, **`EReaderOSSignal`** (`waitForSignal` → `processMsgs`).
- **Obtaining the socket for `req*` / `placeOrder`**: `EClientSocket *sock = ibkr::Client::instance().client();` then **`if (!sock) return;`** (only valid while connected; see **Threading and Drogon** before calling from HTTP threads).
- **Includes (Docker)**: `/usr/local/include/twsapi`, `protobufUnix` — see `tws/CMakeLists.txt`.
- **Linking**: **`twsapi`**; repo uses **`-Wl,--allow-shlib-undefined`** for optional symbols in the IB shared library.
- **Shutdown**: `std::atexit` in `main.cc` calls **`ibkr::Client::instance().disconnect()`**.

---

## Trading API reference (copy-paste patterns)

Patterns below use raw **`EClientSocket* client`**; in this repo, pass **`ibkr::Client::instance().client()`** after a null check, or extend **`ibkr::Client`** so all **`req*`** calls share one threading policy.

### 1. Contract builders

**Base contract**

```cpp
Contract BaseContract(int conId,
                      const std::string& symbol,
                      const std::string& secType,
                      const std::string& exchange = "SMART",
                      const std::string& currency = "USD") {
    Contract c;
    c.conId = conId;
    c.symbol = symbol;
    c.secType = secType;
    c.exchange = exchange;
    c.currency = currency;
    return c;
}
```

**STK**

```cpp
Contract BuildStock(int conId, const std::string& symbol) {
    return BaseContract(conId, symbol, "STK");
}
```

**OPT**

```cpp
Contract BuildOption(int conId,
                     const std::string& symbol,
                     const std::string& expiry,
                     double strike,
                     const std::string& right) {
    Contract c = BaseContract(conId, symbol, "OPT");
    c.lastTradeDateOrContractMonth = expiry;
    c.strike = strike;
    c.right = right;  // "C" or "P"
    c.multiplier = "100";
    return c;
}
```

**FUT**

```cpp
Contract BuildFuture(int conId,
                     const std::string& symbol,
                     const std::string& expiry) {
    Contract c = BaseContract(conId, symbol, "FUT");
    c.lastTradeDateOrContractMonth = expiry;
    return c;
}
```

### 2. Combo (BAG) contracts

**Combo leg**

```cpp
ComboLeg BuildComboLeg(int conId,
                       int ratio,
                       const std::string& action,
                       const std::string& exchange = "SMART") {
    ComboLeg leg;
    leg.conId = conId;
    leg.ratio = ratio;
    leg.action = action;  // BUY / SELL
    leg.exchange = exchange;
    return leg;
}
```

**BAG (mixed STK / OPT / FUT legs)**

```cpp
Contract BuildBag(const std::string& symbol,
                  const std::vector<ComboLeg>& legs,
                  const std::string& currency = "USD") {
    Contract c;
    c.symbol = symbol;
    c.secType = "BAG";
    c.exchange = "SMART";
    c.currency = currency;
    c.comboLegs = legs;
    return c;
}
```

### 3. Pricing (market data)

**Streaming**

```cpp
void RequestMarketData(EClientSocket* client,
                       int reqId,
                       const Contract& contract) {
    client->reqMktData(reqId, contract, "", false, false, TagValueListSPtr());
}
```

**Snapshot (preferred for deterministic execution)**

```cpp
void RequestSnapshot(EClientSocket* client,
                     int reqId,
                     const Contract& contract) {
    client->reqMktData(reqId, contract, "", true, false, TagValueListSPtr());
}
```

**Cancel**

```cpp
void CancelMarketData(EClientSocket* client, int reqId) {
    client->cancelMktData(reqId);
}
```

**Contract enrichment**

```cpp
void RequestContractDetails(EClientSocket* client,
                            int reqId,
                            const Contract& contract) {
    client->reqContractDetails(reqId, contract);
}
```

### 4. Order builders

**Base order**

```cpp
Order BuildBaseOrder(const std::string& action,
                     int quantity,
                     const std::string& orderType = "LMT",
                     double limitPrice = 0.0) {
    Order o;
    o.action = action;
    o.totalQuantity = quantity;
    o.orderType = orderType;
    if (orderType == "LMT") {
        o.lmtPrice = limitPrice;
    }
    return o;
}
```

**Market**

```cpp
Order BuildMarketOrder(const std::string& action, int qty) {
    return BuildBaseOrder(action, qty, "MKT");
}
```

**Limit**

```cpp
Order BuildLimitOrder(const std::string& action,
                      int qty,
                      double price) {
    return BuildBaseOrder(action, qty, "LMT", price);
}
```

### 5. Order CRUD

**Create**

```cpp
void CreateOrder(EClientSocket* client,
                 int orderId,
                 const Contract& contract,
                 const Order& order) {
    client->placeOrder(orderId, contract, order);
}
```

**Read — open orders**

```cpp
void ReadOpenOrders(EClientSocket* client) {
    client->reqOpenOrders();
}
```

**Read — all open orders**

```cpp
void ReadAllOrders(EClientSocket* client) {
    client->reqAllOpenOrders();
}
```

**Update** (same API as create)

```cpp
void UpdateOrder(EClientSocket* client,
                 int orderId,
                 const Contract& contract,
                 const Order& updatedOrder) {
    client->placeOrder(orderId, contract, updatedOrder);
}
```

**Delete**

```cpp
void CancelOrder(EClientSocket* client, int orderId) {
    client->cancelOrder(orderId);
}
```

### 6. Combo orders (BAG)

**Combo limit order**

```cpp
Order BuildComboLimitOrder(const std::string& action,
                           int qty,
                           double limitPrice) {
    Order o;
    o.action = action;
    o.orderType = "LMT";
    o.totalQuantity = qty;
    o.lmtPrice = limitPrice;
    return o;
}
```

**Place combo**

```cpp
void CreateComboOrder(EClientSocket* client,
                      int orderId,
                      const Contract& bagContract,
                      const Order& order) {
    client->placeOrder(orderId, bagContract, order);
}
```

**Smart routing (optional)**

```cpp
void AddSmartComboRouting(Order& order) {
    order.smartComboRoutingParams.reset(new TagValueList());
    order.smartComboRoutingParams->push_back(
        TagValueSPtr(new TagValue("NonGuaranteed", "1")));
}
```

### 7. Execution hook (fill tracking)

Override on your **`EWrapper`** (e.g. extend **`ibkr::Client`** or a dedicated wrapper):

```cpp
void execDetails(int reqId,
                 const Contract& contract,
                 const Execution& execution) override {
    // Persist execution.execId; map to internal trade + order ids
}
```

---

## Lifecycle and domain principles

- **Pipeline mental model**: Contract → pricing → (optional execution pricing) → order → broker order → execution.
- **`conId`**: treat as **lookup hint**, not a substitute for a full contract definition for new instruments.
- **Before execution**: **rehydrate** full `Contract` fields; use **`reqContractDetails`** to backfill missing metadata.
- **Pricing**: prefer **snapshot** `reqMktData` (`snapshot=true`) when you need a **deterministic** price point before sending an order.
- **Combos**: **normalize combo legs** in your domain model — critical for reconciliation and BAG correctness.
- **Persistence**: store **`conId`**, **full contract fields**, **execution ids** (`execId`, etc.) for audit and replay.
- **Alignment** (broader product model): concepts map cleanly to domain types such as pricing by `conId`, watchlist/trade entities, **`broker_orders` / `executions`** — keep API ids and internal ids explicitly mapped.

---

## Patterns to follow

1. **Wrapper**: Derive from **`DefaultEWrapper`**; implement **`error`**, **`connectionClosed`**, and trading callbacks you subscribe to (`tickPrice`, `orderStatus`, **`execDetails`**, etc.).
2. **Reader loop**: After **`eConnect`**, **`EReader::start()`**, then **`waitForSignal`** → **`processMsgs`** on the reader thread.
3. **State**: Use **`std::mutex`** for shared fields consistent with `ibkr_client.cc`.
4. **HTTP handlers**: Status JSON via **`isConnected()`**, connection metadata, **`lastError*`**, **`client() != nullptr`**. Always invoke Drogon **`callback`**.
5. **Probe**: **`ibkr::probeConnection`** for one-off checks and client-id stepping.

## Threading and Drogon

- **`processMsgs`** runs on the **reader thread** only.
- **Never** block Drogon handlers on IB read loops.
- **`req*` / `placeOrder` from HTTP threads**: IB APIs are **not thread-safe** by default — centralize calls inside **`ibkr::Client`** (mutex + same thread as socket, or **command queue + reader thread drain**) before scaling trading features. The next production hardening step is typically a **thread-safe wrapper + async event bus** between IB callbacks and the rest of the service.

## Configuration

- **Defaults** (`tws/main.cc`): **`ibkr`**, **8888**, client id **30** — override with env.
- **Docker SDK**: `TWS_SDK_ZIP` / `TWS_SDK_URL` in `tws/Dockerfile`; overlay `tws/cmake/tws-sdk/` before `protoc` / install.

## Checklist for IBKR changes

1. Connect/disconnect cleans **`EReader`**, joins reader thread, **`eDisconnect`** when connected; **`issueSignal`** on shutdown.
2. Logging: **`trantor/utils/Logger.h`**.
3. New `.cc` files: add to **`tws/CMakeLists.txt`** `add_executable`.
4. Compose: **`IBKR_HOST`** matches the gateway container/service name.

## Critical constraints

- No blocking IB loops in **`HttpController`** paths.
- No omitted Drogon **`callback`** on error/success branches.
- **SDK bumps**: refresh Dockerfile zip/URL and **`tws/cmake/tws-sdk/`** overlay compatibility.
- For execution-critical paths: **snapshot pricing** + **contract details** when metadata is incomplete; do not rely on **`conId` alone** for unfamiliar instruments.

## Anti-patterns

- Per-request **`EClientSocket`** (except isolated probes like **`ibkr_probe`**).
- Unsynchronized **`req*`** / **`placeOrder`** from multiple threads.
- Removing **`-Wl,--allow-shlib-undefined`** without verifying the **`twsapi`** binary you link.
