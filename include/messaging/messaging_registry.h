#pragma once

#include "messaging/message_broker.h"

#include <json/json.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace messaging {

class MessagingRegistry {
  public:
    static MessagingRegistry &instance();

    void initialize(std::shared_ptr<IMessageBroker> broker);
    void start();
    void stop();
    bool publishTopic(const std::string &topic, const std::string &payload);
    void recordObservedMessage(const BrokerMessage &message);
    Json::Value statusJson() const;
    std::shared_ptr<IMessageBroker> broker() const;

  private:
    struct ObservedMessage {
        std::uint64_t count{0};
        std::string queueName;
        std::string routingKey;
        std::string payload;
        std::string updatedAt;
    };

    static std::string currentTimestamp();

    mutable std::mutex mutex_;
    std::shared_ptr<IMessageBroker> broker_;
    std::unordered_map<std::string, ObservedMessage> observedMessages_;
};

}  // namespace messaging
