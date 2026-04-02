#include "messaging/broker_config.h"

#include <trantor/utils/Logger.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace messaging {
namespace {

std::string requireEnv(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(std::string("missing required environment variable: ") +
                                 name);
    }
    return value;
}

std::string envOrDefault(const char *name, std::string defaultValue) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return defaultValue;
    }
    return value;
}

bool envFlag(const char *name, bool defaultValue) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return defaultValue;
    }

    std::string text{value};
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    return text == "1" || text == "true" || text == "yes" || text == "on";
}

std::size_t envSize(const char *name, std::size_t defaultValue) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return defaultValue;
    }

    return static_cast<std::size_t>(std::stoull(value));
}

std::uint16_t envUint16(const char *name, std::uint16_t defaultValue) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return defaultValue;
    }

    const auto parsed = std::stoul(value);
    if (parsed > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error(std::string("environment value out of range: ") + name);
    }
    return static_cast<std::uint16_t>(parsed);
}

QueueConfig buildTopicConfig(const char *topic,
                             const char *queueEnv,
                             const char *bindingEnv,
                             const BrokerConfig &config) {
    QueueConfig queueConfig;
    queueConfig.topic = topic;
    queueConfig.queueName = requireEnv(queueEnv);
    queueConfig.bindingKey = envOrDefault(bindingEnv, topic);
    queueConfig.prefetch = config.prefetch;
    return queueConfig;
}

}  // namespace

BrokerConfig BrokerConfig::fromEnvironment() {
    BrokerConfig config;
    config.url = requireEnv("RABBITMQ_URL");
    config.exchange.name = requireEnv("RABBITMQ_EXCHANGE");
    config.exchange.type = envOrDefault("RABBITMQ_EXCHANGE_TYPE", "topic");
    config.exchange.durable = envFlag("RABBITMQ_EXCHANGE_DURABLE", true);
    config.exchange.autoDelete = envFlag("RABBITMQ_EXCHANGE_AUTO_DELETE", false);
    config.connectionPoolSize = std::max<std::size_t>(
        1, envSize("RABBITMQ_CONNECTION_POOL_SIZE", config.connectionPoolSize));
    config.channelsPerConnection =
        std::max<std::size_t>(1, envSize("RABBITMQ_CHANNELS_PER_CONNECTION",
                                         config.channelsPerConnection));
    config.publisherPoolSize =
        std::max<std::size_t>(1, envSize("RABBITMQ_PUBLISHER_POOL_SIZE",
                                         config.publisherPoolSize));
    config.prefetch = std::max<std::uint16_t>(
        static_cast<std::uint16_t>(1),
        envUint16("RABBITMQ_PREFETCH", config.prefetch));

    config.subscriptions.push_back(buildTopicConfig("trade.execute",
                                                    "RABBITMQ_QUEUE_TRADE_EXECUTE",
                                                    "RABBITMQ_BINDING_KEY_TRADE_EXECUTE",
                                                    config));
    config.subscriptions.push_back(buildTopicConfig("pricing.update",
                                                    "RABBITMQ_QUEUE_PRICING_UPDATE",
                                                    "RABBITMQ_BINDING_KEY_PRICING_UPDATE",
                                                    config));

    LOG_INFO << "Loaded RabbitMQ config exchange=" << config.exchange.name
             << " type=" << config.exchange.type
             << " poolSize=" << config.connectionPoolSize
             << " channelsPerConnection=" << config.channelsPerConnection
             << " publisherPoolSize=" << config.publisherPoolSize
             << " prefetch=" << config.prefetch
             << " subscriptions=" << config.subscriptions.size();

    return config;
}

const QueueConfig *BrokerConfig::findTopic(std::string_view topic) const {
    const auto found = std::find_if(subscriptions.begin(),
                                    subscriptions.end(),
                                    [&topic](const QueueConfig &candidate) {
                                        return candidate.topic == topic;
                                    });
    if (found == subscriptions.end()) {
        return nullptr;
    }
    return &(*found);
}

}  // namespace messaging
