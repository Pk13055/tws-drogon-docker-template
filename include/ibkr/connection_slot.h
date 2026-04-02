#pragma once

#include <DefaultEWrapper.h>
#include <TickAttrib.h>
#include <EClientSocket.h>
#include <EReader.h>
#include <EReaderOSSignal.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ibkr {

struct SlotStatus {
    bool connected{false};
    bool connecting{false};
    std::string host;
    int port{0};
    int clientId{0};
    int serverVersion{0};
    std::string connectionTime;
    int lastErrorCode{0};
    std::string lastErrorMessage;
    std::size_t pendingRequests{0};
};

class ConnectionSlot final : public DefaultEWrapper {
  public:
    ConnectionSlot();
    ~ConnectionSlot() override;

    bool connect(const std::string &host, int port, int client_id);
    void disconnect();
    
    bool isConnected() const;
    EClientSocket *client();
    SlotStatus status() const;

    /// Generic request for a US stock snapshot (SMART/USD). 
    /// Returns reqId assigned if successful, 0 if not connected.
    int requestStockSnapshot(
        const std::string &symbol,
        std::function<void(Json::Value)> onSuccess,
        std::function<void(std::string)> onError);

    void cancelMktData(int reqId);

    // Configuration for request ID management
    void setReqIdRange(int start, int end);

  private:
    struct StkSnapshotPending {
        std::string symbol;
        double bid{0};
        double ask{0};
        double last{0};
        std::int64_t volume{0};
        bool hasBid{false};
        bool hasAsk{false};
        bool hasLast{false};
        bool hasVolume{false};
        std::function<void(Json::Value)> onSuccess;
        std::function<void(std::string)> onError;
    };

    void readerLoop();
    static Json::Value buildStkSnapshotJson(const StkSnapshotPending &p);

    // EWrapper overrides
    void error(int id, time_t error_time, int error_code,
               const std::string &error_string,
               const std::string &advanced_order_reject_json) override;
    void connectionClosed() override;
    void tickPrice(int reqId, TickType field, double price,
                   const TickAttrib &attrib) override;
    void tickSnapshotEnd(int reqId) override;

    std::atomic<bool> reader_running_{false};
    mutable std::mutex mutex_;
    EReaderOSSignal signal_;
    EClientSocket client_socket_;
    std::unique_ptr<EReader> reader_;
    std::thread reader_thread_;
    
    bool connected_{false};
    bool connecting_{false};
    std::string host_;
    int port_{0};
    int client_id_{0};
    int server_version_{0};
    std::string connection_time_;
    int last_error_code_{0};
    std::string last_error_message_;

    int req_id_start_{1};
    int req_id_end_{999999};
    int next_req_id_{0};

    std::unordered_map<int, StkSnapshotPending> pending_stk_;
};

}  // namespace ibkr
