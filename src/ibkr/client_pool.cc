#include "ibkr/client_pool.h"
#include <trantor/utils/Logger.h>
#include <chrono>

namespace ibkr {

ClientPool &ClientPool::instance() {
    static ClientPool pool;
    return pool;
}

ClientPool::~ClientPool() {
    shutdown();
}

void ClientPool::initialize(const std::string &host, int port, int startingClientId, int numSlots) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) return;

    host_ = host;
    port_ = port;
    starting_client_id_ = startingClientId;
    running_.store(true);

    LOG_INFO << "Initializing IBKR Client Pool with host=" << host 
             << " port=" << port << " slots=" << numSlots;

    for (int i = 0; i < numSlots; ++i) {
        auto slot = std::make_unique<ConnectionSlot>();
        // Divide reqId space (e.g. 1-100k, 100k-200k, etc.)
        slot->setReqIdRange((i + 1) * 100000, (i + 2) * 100000 - 1);
        slots_.push_back(std::move(slot));
    }

    monitor_thread_ = std::thread(&ClientPool::checkConnections, this);
}

void ClientPool::shutdown() {
    running_.store(false);
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &slot : slots_) {
        slot->disconnect();
    }
    slots_.clear();
    LOG_INFO << "IBKR Client Pool shut down";
}

bool ClientPool::isAnyConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &slot : slots_) {
        if (slot->isConnected()) return true;
    }
    return false;
}

PoolStatus ClientPool::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolStatus s;
    s.totalSlots = slots_.size();
    for (const auto &slot : slots_) {
        auto ss = slot->status();
        if (ss.connected) s.connectedSlots++;
        s.slots.push_back(std::move(ss));
    }
    s.anyConnected = (s.connectedSlots > 0);
    return s;
}

void ClientPool::requestStockSnapshot(
    const std::string &symbol,
    std::function<void(Json::Value)> onSuccess,
    std::function<void(std::string)> onError) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    if (slots_.empty()) {
        if (onError) onError("Pool not initialized");
        return;
    }

    // Try to find a connected slot, starting from next_slot_index_
    std::size_t startIdx = next_slot_index_.fetch_add(1) % slots_.size();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        std::size_t idx = (startIdx + i) % slots_.size();
        if (slots_[idx]->isConnected()) {
            slots_[idx]->requestStockSnapshot(symbol, onSuccess, onError);
            return;
        }
    }

    if (onError) onError("No connected IBKR slots available");
}

EClientSocket *ClientPool::getPrimarySocket() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &slot : slots_) {
        if (slot->isConnected()) return slot->client();
    }
    return nullptr;
}

void ClientPool::checkConnections() {
    while (running_.load()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (std::size_t i = 0; i < slots_.size(); ++i) {
                if (!slots_[i]->isConnected()) {
                    // Step client ID if multiple slots are used to avoid collision
                    int clientId = starting_client_id_ + static_cast<int>(i);
                    slots_[i]->connect(host_, port_, clientId);
                }
            }
        }
        
        // Check every 5 seconds
        for (int i = 0; i < 50 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

}  // namespace ibkr
