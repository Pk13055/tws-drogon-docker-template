#include "ibkr/connection_slot.h"

#include <Contract.h>
#include <TagValue.h>
#include <trantor/utils/Logger.h>

#include <cmath>
#include <cstdlib>

namespace ibkr {

ConnectionSlot::ConnectionSlot() : signal_(200), client_socket_(this, &signal_) {}

ConnectionSlot::~ConnectionSlot() {
    disconnect();
}

bool ConnectionSlot::connect(const std::string &host, int port, int client_id) {
    disconnect();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connecting_ = true;
        host_ = host;
        port_ = port;
        client_id_ = client_id;
    }

    LOG_INFO << "IBKR connection slot connecting host=" << host << " port=" << port
             << " clientId=" << client_id;

    if (!client_socket_.eConnect(host.c_str(), port, client_id, false)) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_code_ = -1;
        last_error_message_ = "IBKR socket eConnect failed";
        connecting_ = false;
        LOG_WARN << "IBKR connection slot eConnect failed host=" << host << " port=" << port;
        return false;
    }

    if (!client_socket_.isConnected()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_code_ = -1;
        last_error_message_ = "IBKR socket connection refused";
        connecting_ = false;
        LOG_WARN << "IBKR connection slot socket was not connected after eConnect host=" << host
                 << " port=" << port;
        return false;
    }

    auto reader = std::make_unique<EReader>(&client_socket_, &signal_);
    reader->start();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        reader_ = std::move(reader);
        connected_ = true;
        connecting_ = false;
        server_version_ = client_socket_.EClient::serverVersion();
        connection_time_ = client_socket_.EClient::TwsConnectionTime();
        last_error_code_ = 0;
        last_error_message_.clear();
        next_req_id_ = req_id_start_;
    }

    reader_running_.store(true);
    reader_thread_ = std::thread(&ConnectionSlot::readerLoop, this);

    LOG_INFO << "IBKR connection slot connected host=" << host << " port=" << port
             << " clientId=" << client_id
             << " serverVersion=" << server_version_;
    return true;
}

void ConnectionSlot::disconnect() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected_ && !connecting_ && !reader_running_.load()) {
            return;
        }
        connected_ = false;
        connecting_ = false;
    }

    reader_running_.store(false);
    signal_.issueSignal();

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    if (client_socket_.isConnected()) {
        client_socket_.eDisconnect();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        reader_.reset();
        server_version_ = 0;
        connection_time_.clear();
        // Clear pending requests
        for (auto &pair : pending_stk_) {
            if (pair.second.onError) {
                pair.second.onError("Slot disconnected");
            }
        }
        pending_stk_.clear();
    }

    LOG_INFO << "IBKR connection slot disconnected";
}

bool ConnectionSlot::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

EClientSocket *ConnectionSlot::client() {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ ? &client_socket_ : nullptr;
}

SlotStatus ConnectionSlot::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    SlotStatus s;
    s.connected = connected_;
    s.connecting = connecting_;
    s.host = host_;
    s.port = port_;
    s.clientId = client_id_;
    s.serverVersion = server_version_;
    s.connectionTime = connection_time_;
    s.lastErrorCode = last_error_code_;
    s.lastErrorMessage = last_error_message_;
    s.pendingRequests = pending_stk_.size();
    return s;
}

void ConnectionSlot::readerLoop() {
    while (reader_running_.load()) {
        signal_.waitForSignal();

        if (!reader_running_.load()) {
            break;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        auto *reader = reader_.get();
        lock.unlock();

        if (reader) {
            reader->processMsgs();
        }
    }
}

Json::Value ConnectionSlot::buildStkSnapshotJson(const StkSnapshotPending &p) {
    Json::Value j;
    j["secType"] = "STK";
    j["symbol"] = p.symbol;
    j["bid"] = p.hasBid ? Json::Value(p.bid) : Json::nullValue;
    j["ask"] = p.hasAsk ? Json::Value(p.ask) : Json::nullValue;
    if (p.hasBid && p.hasAsk) {
        j["mid"] = (p.bid + p.ask) * 0.5;
    } else {
        j["mid"] = Json::nullValue;
    }
    j["last"] = p.hasLast ? Json::Value(p.last) : Json::nullValue;
    j["volume"] = p.hasVolume ? Json::Value(Json::Int64(p.volume)) : Json::nullValue;
    return j;
}

int ConnectionSlot::requestStockSnapshot(
    const std::string &symbol,
    std::function<void(Json::Value)> onSuccess,
    std::function<void(std::string)> onError) {
    
    int tickerId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected_) {
            if (onError) onError("Slot not connected");
            return 0;
        }

        tickerId = next_req_id_++;
        if (next_req_id_ > req_id_end_) {
            next_req_id_ = req_id_start_;
        }

        StkSnapshotPending pending;
        pending.symbol = symbol;
        pending.onSuccess = std::move(onSuccess);
        pending.onError = std::move(onError);
        pending_stk_[tickerId] = std::move(pending);

        Contract c;
        c.symbol = symbol;
        c.secType = "STK";
        c.exchange = "SMART";
        c.currency = "USD";

        client_socket_.reqMktData(tickerId, c, "", true, false, TagValueListSPtr());
    }
    return tickerId;
}

void ConnectionSlot::cancelMktData(int reqId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
        client_socket_.cancelMktData(reqId);
    }
    pending_stk_.erase(reqId);
}

void ConnectionSlot::setReqIdRange(int start, int end) {
    std::lock_guard<std::mutex> lock(mutex_);
    req_id_start_ = start;
    req_id_end_ = end;
    if (next_req_id_ < start || next_req_id_ > end) {
        next_req_id_ = start;
    }
}

void ConnectionSlot::tickPrice(
    int reqId,
    TickType field,
    double price,
    const TickAttrib &) {
    if (price < 0.0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_stk_.find(reqId);
    if (it == pending_stk_.end()) {
        return;
    }
    StkSnapshotPending &p = it->second;
    switch (field) {
    case BID:
        p.bid = price;
        p.hasBid = true;
        break;
    case ASK:
        p.ask = price;
        p.hasAsk = true;
        break;
    case LAST:
        p.last = price;
        p.hasLast = true;
        break;
    default:
        break;
    }
}

void ConnectionSlot::tickSnapshotEnd(int reqId) {
    std::function<void(Json::Value)> ok;
    std::function<void(std::string)> err;
    Json::Value json;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_stk_.find(reqId);
        if (it == pending_stk_.end()) {
            return;
        }
        StkSnapshotPending pending = std::move(it->second);
        pending_stk_.erase(it);
        if (connected_) {
            client_socket_.cancelMktData(reqId);
        }
        if (!pending.hasBid && !pending.hasAsk && !pending.hasLast) {
            err = std::move(pending.onError);
        } else {
            ok = std::move(pending.onSuccess);
            json = buildStkSnapshotJson(pending);
        }
    }
    if (ok) {
        ok(json);
    } else if (err) {
        err("No market data returned for symbol");
    }
}

void ConnectionSlot::error(
    int id,
    time_t,
    int error_code,
    const std::string &error_string,
    const std::string &) {
    std::function<void(std::string)> pendingErr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_code_ = error_code;
        last_error_message_ = error_string;
        if (id > 0) {
            auto it = pending_stk_.find(id);
            if (it != pending_stk_.end()) {
                if (connected_) {
                    client_socket_.cancelMktData(id);
                }
                pendingErr = std::move(it->second.onError);
                pending_stk_.erase(it);
            }
        }
    }
    if (pendingErr) {
        pendingErr(error_string);
        return;
    }
    LOG_WARN << "IBKR connection slot error code=" << error_code
             << " message=" << error_string;
}

void ConnectionSlot::connectionClosed() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    reader_running_.store(false);
    last_error_message_ = last_error_message_.empty() ? "IBKR connection closed" : last_error_message_;
    signal_.issueSignal();
    
    // Clear pending requests
    for (auto &pair : pending_stk_) {
        if (pair.second.onError) {
            pair.second.onError("Slot disconnected");
        }
    }
    pending_stk_.clear();

    LOG_INFO << "IBKR connection slot connection closed";
}

}  // namespace ibkr
