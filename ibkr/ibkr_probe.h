#pragma once

#include <chrono>
#include <string>

namespace ibkr {

struct ProbeResult {
    bool connected{false};
    std::string host{"ibkr"};
    int port{8888};
    int client_id{0};
    int attempts{0};
    int server_version{0};
    std::string connection_time;
    long long current_time{0};
    int error_code{0};
    std::string error_message;
};

ProbeResult probeConnection(
    const std::string &host = "ibkr",
    int port = 8888,
    int starting_client_id = 30,
    int min_client_id = 1,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(1500));

}  // namespace ibkr
