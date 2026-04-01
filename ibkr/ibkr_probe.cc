#include "ibkr/ibkr_probe.h"

#include <DefaultEWrapper.h>
#include <EClientSocket.h>
#include <EReader.h>
#include <EReaderOSSignal.h>
#include <trantor/utils/Logger.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

namespace ibkr {
namespace {

constexpr int kClientIdAlreadyInUse = 326;

class ProbeClient final : public DefaultEWrapper {
  public:
    ProbeClient() : signal_(200), client_(this, &signal_) {}

    ProbeResult run(
        const std::string &host,
        const int port,
        const int client_id,
        const std::chrono::milliseconds timeout) {
        reset();

        ProbeResult result;
        result.host = host;
        result.port = port;
        result.client_id = client_id;
        LOG_DEBUG << "IBKR probe connect attempt host=" << host
                  << " port=" << port
                  << " clientId=" << client_id;

        const bool connected = client_.eConnect(host.c_str(), port, client_id, false);
        result.server_version = client_.EClient::serverVersion();
        result.connection_time = client_.EClient::TwsConnectionTime();

        if (!connected || !client_.isConnected()) {
            result.error_message = "SDK socket connect failed";
            LOG_WARN << "IBKR probe socket connect failed host=" << host
                     << " port=" << port
                     << " clientId=" << client_id;
            cleanup();
            return result;
        }

        reader_ = std::make_unique<EReader>(&client_, &signal_);
        reader_->start();
        client_.reqCurrentTime();

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            signal_.waitForSignal();
            if (reader_) {
                reader_->processMsgs();
            }

            bool received_current_time = false;
            bool fatal_error = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                received_current_time = received_current_time_;
                fatal_error = fatal_error_;
                if (received_current_time) {
                    result.connected = true;
                    result.current_time = current_time_;
                    if (last_error_code_ != 0) {
                        result.error_code = last_error_code_;
                        result.error_message = last_error_message_;
                    }
                } else if (fatal_error) {
                    result.error_code = last_error_code_;
                    result.error_message = last_error_message_;
                }
            }

            if (received_current_time || fatal_error) {
                LOG_DEBUG << "IBKR probe completed clientId=" << client_id
                          << " connected=" << result.connected
                          << " errorCode=" << result.error_code
                          << " error=" << result.error_message;
                cleanup();
                return result;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            result.error_code = last_error_code_;
            result.error_message = last_error_message_.empty()
                                       ? "Timed out waiting for IBKR SDK response"
                                       : last_error_message_;
        }
        cleanup();
        LOG_WARN << "IBKR probe timed out clientId=" << client_id
                 << " errorCode=" << result.error_code
                 << " error=" << result.error_message;
        return result;
    }

  private:
    void error(
        int id,
        time_t error_time,
        int error_code,
        const std::string &error_string,
        const std::string &advanced_order_reject_json) override {
        (void)id;
        (void)error_time;
        (void)advanced_order_reject_json;

        std::lock_guard<std::mutex> lock(mutex_);
        last_error_code_ = error_code;
        last_error_message_ = error_string;
        if (error_code == kClientIdAlreadyInUse) {
            fatal_error_ = true;
        }
        LOG_DEBUG << "IBKR probe wrapper error code=" << error_code
                  << " message=" << error_string;
    }

    void currentTime(long long time) override {
        std::lock_guard<std::mutex> lock(mutex_);
        current_time_ = time;
        received_current_time_ = true;
    }

    void connectionClosed() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!received_current_time_) {
            fatal_error_ = true;
            if (last_error_message_.empty()) {
                last_error_message_ = "IBKR connection closed before currentTime response";
            }
        }
    }

    void reset() {
        cleanup();
        std::lock_guard<std::mutex> lock(mutex_);
        received_current_time_ = false;
        fatal_error_ = false;
        current_time_ = 0;
        last_error_code_ = 0;
        last_error_message_.clear();
    }

    void cleanup() {
        if (reader_) {
            reader_->stop();
            reader_.reset();
        }
        if (client_.isConnected()) {
            client_.eDisconnect();
        }
    }

    std::mutex mutex_;
    EReaderOSSignal signal_;
    EClientSocket client_;
    std::unique_ptr<EReader> reader_;
    bool received_current_time_{false};
    bool fatal_error_{false};
    long long current_time_{0};
    int last_error_code_{0};
    std::string last_error_message_;
};

}  // namespace

ProbeResult probeConnection(
    const std::string &host,
    const int port,
    const int starting_client_id,
    const int min_client_id,
    const std::chrono::milliseconds timeout) {
    ProbeResult last_result;
    last_result.host = host;
    last_result.port = port;

    for (int client_id = starting_client_id; client_id >= min_client_id; --client_id) {
        ProbeClient probe_client;
        ProbeResult result = probe_client.run(host, port, client_id, timeout);
        result.attempts = starting_client_id - client_id + 1;
        if (result.connected) {
            LOG_INFO << "IBKR probe connected host=" << host
                     << " port=" << port
                     << " clientId=" << client_id
                     << " attempts=" << result.attempts;
            return result;
        }

        last_result = result;
        if (result.error_code != kClientIdAlreadyInUse) {
            LOG_WARN << "IBKR probe failed without retry host=" << host
                     << " port=" << port
                     << " clientId=" << client_id
                     << " errorCode=" << result.error_code
                     << " error=" << result.error_message;
            break;
        }
        LOG_INFO << "IBKR probe retrying with lower clientId after collision at clientId="
                 << client_id;
    }

    LOG_WARN << "IBKR probe failed host=" << host
             << " port=" << port
             << " attempts=" << last_result.attempts
             << " errorCode=" << last_result.error_code
             << " error=" << last_result.error_message;
    return last_result;
}

}  // namespace ibkr
