#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace messaging {

struct ExchangeConfig {
    std::string name;
    std::string type{"topic"};
    bool durable{true};
    bool autoDelete{false};
};

struct QueueConfig {
    std::string topic;
    std::string queueName;
    std::string bindingKey;
    bool durable{true};
    bool autoDelete{false};
    bool exclusive{false};
    std::uint16_t prefetch{10};
};

struct BrokerConfig {
    std::string url;
    ExchangeConfig exchange;
    std::size_t connectionPoolSize{1};
    std::size_t channelsPerConnection{2};
    std::size_t publisherPoolSize{2};
    std::uint16_t prefetch{10};
    std::vector<QueueConfig> subscriptions;

    static BrokerConfig fromEnvironment();
    const QueueConfig *findTopic(std::string_view topic) const;
};

}  // namespace messaging
