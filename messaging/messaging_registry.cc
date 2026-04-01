#include "messaging/messaging_registry.h"

#include <trantor/utils/Logger.h>

#include <iomanip>
#include <sstream>

namespace messaging {

MessagingRegistry &MessagingRegistry::instance() {
    static MessagingRegistry registry;
    return registry;
}

void MessagingRegistry::initialize(std::shared_ptr<IMessageBroker> broker) {
    std::lock_guard<std::mutex> lock(mutex_);
    broker_ = std::move(broker);
    LOG_INFO << "MessagingRegistry initialized";
}

void MessagingRegistry::start() {
    auto brokerInstance = broker();
    if (brokerInstance) {
        LOG_INFO << "MessagingRegistry starting broker";
        brokerInstance->start();
    } else {
        LOG_WARN << "MessagingRegistry start requested without broker";
    }
}

void MessagingRegistry::stop() {
    auto brokerInstance = broker();
    if (brokerInstance) {
        LOG_INFO << "MessagingRegistry stopping broker";
        brokerInstance->stop();
    } else {
        LOG_WARN << "MessagingRegistry stop requested without broker";
    }
}

bool MessagingRegistry::publishTopic(const std::string &topic, const std::string &payload) {
    auto brokerInstance = broker();
    if (!brokerInstance) {
        LOG_WARN << "publishTopic skipped; no broker for topic=" << topic;
        return false;
    }

    const auto *topicConfig = brokerInstance->config().findTopic(topic);

    BrokerPublishRequest request;
    request.topic = topic;
    request.exchange = brokerInstance->config().exchange.name;
    request.routingKey = topicConfig != nullptr ? topicConfig->bindingKey : topic;
    request.payload = payload;

    const bool published = brokerInstance->publish(request);
    if (!published) {
        LOG_WARN << "publishTopic failed topic=" << topic
                 << " exchange=" << request.exchange
                 << " routingKey=" << request.routingKey;
    }
    return published;
}

void MessagingRegistry::recordObservedMessage(const BrokerMessage &message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto &observed = observedMessages_[message.topic];
    observed.count += 1;
    observed.queueName = message.queueName;
    observed.routingKey = message.routingKey;
    observed.payload = message.payload;
    observed.updatedAt = currentTimestamp();
    LOG_TRACE << "Recorded observed message topic=" << message.topic
              << " count=" << observed.count;
}

Json::Value MessagingRegistry::statusJson() const {
    Json::Value payload;
    auto brokerInstance = broker();
    if (!brokerInstance) {
        payload["configured"] = false;
        return payload;
    }

    const auto snapshot = brokerInstance->snapshot();
    payload["configured"] = true;
    payload["started"] = snapshot.started;
    payload["configured_connections"] =
        Json::UInt64(snapshot.configuredConnections);
    payload["ready_connections"] = Json::UInt64(snapshot.readyConnections);
    payload["queued_publishes"] = Json::UInt64(snapshot.queuedPublishes);
    payload["publish_requests"] = Json::UInt64(snapshot.publishRequests);
    payload["published_messages"] = Json::UInt64(snapshot.publishedMessages);
    payload["consumed_messages"] = Json::UInt64(snapshot.consumedMessages);
    payload["last_error"] = snapshot.lastError;
    payload["exchange"] = brokerInstance->config().exchange.name;

    Json::Value readyTopics{Json::arrayValue};
    for (const auto &topic : snapshot.readyTopics) {
        readyTopics.append(topic);
    }
    payload["ready_topics"] = readyTopics;

    Json::Value topics{Json::objectValue};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &[topic, observed] : observedMessages_) {
            Json::Value item;
            item["count"] = Json::UInt64(observed.count);
            item["queue"] = observed.queueName;
            item["routing_key"] = observed.routingKey;
            item["payload"] = observed.payload;
            item["updated_at"] = observed.updatedAt;
            topics[topic] = item;
        }
    }
    payload["observed_topics"] = topics;

    Json::Value configuredTopics{Json::arrayValue};
    for (const auto &subscription : brokerInstance->config().subscriptions) {
        Json::Value item;
        item["topic"] = subscription.topic;
        item["queue"] = subscription.queueName;
        item["routing_key"] = subscription.bindingKey;
        configuredTopics.append(item);
    }
    payload["configured_topics"] = configuredTopics;

    return payload;
}

std::shared_ptr<IMessageBroker> MessagingRegistry::broker() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return broker_;
}

std::string MessagingRegistry::currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &time);
#else
    gmtime_r(&time, &utcTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

}  // namespace messaging
