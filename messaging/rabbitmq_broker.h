#pragma once

#include "messaging/amqp_trantor_handler.h"
#include "messaging/message_broker.h"

#include <amqpcpp.h>
#include <amqpcpp/linux_tcp.h>
#include <trantor/net/EventLoop.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace messaging {

class RabbitMqBroker : public IMessageBroker,
                       public std::enable_shared_from_this<RabbitMqBroker> {
  public:
    explicit RabbitMqBroker(BrokerConfig config);
    ~RabbitMqBroker() override;

    void start() override;
    void stop() override;
    void subscribe(const QueueConfig &config, MessageHandler handler) override;
    bool publish(const BrokerPublishRequest &request,
                 PublishCompletion completion = {}) override;
    BrokerHealthSnapshot snapshot() const override;
    const BrokerConfig &config() const override;

  private:
    struct Subscription {
        QueueConfig config;
        MessageHandler handler;
        std::size_t slotIndex{0};
        std::string consumerTag;
    };

    struct PublishTask {
        BrokerPublishRequest request;
        PublishCompletion completion;
    };

    struct Slot {
        std::size_t index{0};
        trantor::EventLoop *loop{nullptr};
        std::shared_ptr<AmqpTrantorHandler> handler;
        std::unique_ptr<AMQP::TcpConnection> connection;
        std::vector<std::unique_ptr<AMQP::TcpChannel>> publisherChannels;
        std::unordered_map<std::string, std::unique_ptr<AMQP::TcpChannel>> consumerChannels;
        std::deque<PublishTask> pendingPublishes;
        std::size_t nextPublisherChannel{0};
        std::atomic<bool> ready{false};
        std::atomic<bool> reconnectScheduled{false};
        double reconnectDelaySeconds{1.0};
    };

    void initializeSlots();
    void connectSlot(std::size_t index);
    void handleSlotReady(std::size_t index);
    void handleSlotFailure(std::size_t index, const std::string &reason);
    void scheduleReconnect(std::size_t index);
    void rebuildChannels(Slot &slot);
    void startConsumer(std::size_t slotIndex, const std::shared_ptr<Subscription> &subscription);
    void publishOnLoop(std::size_t slotIndex, PublishTask task);
    void flushPendingPublishes(std::size_t slotIndex);
    const QueueConfig *resolveTopic(std::string_view topic) const;
    AMQP::ExchangeType exchangeType() const;
    int exchangeFlags() const;
    int queueFlags(const QueueConfig &config) const;
    std::size_t readyConnectionCount() const;

    BrokerConfig config_;

    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Subscription>> subscriptions_;
    std::vector<std::unique_ptr<Slot>> slots_;
    std::string lastError_;

    std::atomic<bool> started_{false};
    std::atomic<std::size_t> publishCursor_{0};
    std::atomic<std::uint64_t> publishRequests_{0};
    std::atomic<std::uint64_t> publishedMessages_{0};
    std::atomic<std::uint64_t> consumedMessages_{0};
    std::atomic<std::uint64_t> queuedPublishes_{0};
};

}  // namespace messaging
