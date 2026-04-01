#pragma once

#include "messaging/broker_config.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace messaging {

struct BrokerMessage {
    std::string topic;
    std::string exchange;
    std::string routingKey;
    std::string queueName;
    std::string payload;
    bool redelivered{false};
};

struct BrokerPublishRequest {
    std::string topic;
    std::string exchange;
    std::string routingKey;
    std::string payload;
    bool persistent{true};
};

struct BrokerHealthSnapshot {
    bool started{false};
    std::size_t configuredConnections{0};
    std::size_t readyConnections{0};
    std::size_t queuedPublishes{0};
    std::uint64_t publishRequests{0};
    std::uint64_t publishedMessages{0};
    std::uint64_t consumedMessages{0};
    std::string lastError;
    std::vector<std::string> readyTopics;
};

using PublishCompletion = std::function<void(bool ok, const std::string &message)>;
using AckCallback = std::function<void()>;
using MessageHandler = std::function<void(const BrokerMessage &message, AckCallback ack)>;

class IMessageBroker {
  public:
    virtual ~IMessageBroker() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void subscribe(const QueueConfig &config, MessageHandler handler) = 0;
    virtual bool publish(const BrokerPublishRequest &request,
                         PublishCompletion completion = {}) = 0;
    virtual BrokerHealthSnapshot snapshot() const = 0;
    virtual const BrokerConfig &config() const = 0;
};

}  // namespace messaging
