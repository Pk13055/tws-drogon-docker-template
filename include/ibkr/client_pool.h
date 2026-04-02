#pragma once

#include "ibkr/connection_slot.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>

namespace ibkr {

struct PoolStatus {
    bool anyConnected{false};
    std::size_t totalSlots{0};
    std::size_t connectedSlots{0};
    std::vector<SlotStatus> slots;
};

class ClientPool {
  public:
    static ClientPool &instance();

    void initialize(const std::string &host, int port, int startingClientId, int numSlots = 1);
    void shutdown();

    bool isAnyConnected() const;
    PoolStatus status() const;

    /// Request a stock snapshot. Will round-robin across connected slots.
    void requestStockSnapshot(
        const std::string &symbol,
        std::function<void(Json::Value)> onSuccess,
        std::function<void(std::string)> onError);

    /// Get the first available socket (for legacy or direct use)
    EClientSocket *getPrimarySocket();

  private:
    ClientPool() = default;
    ~ClientPool();

    void checkConnections();

    std::string host_;
    int port_{0};
    int starting_client_id_{0};
    
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<ConnectionSlot>> slots_;
    std::atomic<std::size_t> next_slot_index_{0};
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
};

}  // namespace ibkr
