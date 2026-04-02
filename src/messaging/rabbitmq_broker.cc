#include "messaging/rabbitmq_broker.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>
#include <trantor/utils/Logger.h>

#include <algorithm>
#include <functional>
#include <sstream>
#include <utility>

namespace messaging {
namespace {

std::string joinError(std::size_t slotIndex, const std::string &reason) {
    std::ostringstream stream;
    stream << "slot " << slotIndex << ": " << reason;
    return stream.str();
}

}  // namespace

RabbitMqBroker::RabbitMqBroker(BrokerConfig config) : config_(std::move(config)) {}

RabbitMqBroker::~RabbitMqBroker() {
    stop();
}

void RabbitMqBroker::start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        LOG_DEBUG << "RabbitMqBroker start ignored; already started";
        return;
    }

    LOG_INFO << "RabbitMqBroker starting with connectionPoolSize="
             << config_.connectionPoolSize;
    initializeSlots();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        connectSlot(i);
    }
}

void RabbitMqBroker::stop() {
    if (!started_.exchange(false)) {
        LOG_DEBUG << "RabbitMqBroker stop ignored; already stopped";
        return;
    }
    LOG_INFO << "RabbitMqBroker stopping";

    for (auto &slot : slots_) {
        if (slot == nullptr || slot->loop == nullptr) {
            continue;
        }

        slot->loop->queueInLoop([this, rawSlot = slot.get()]() {
            rawSlot->ready = false;
            rawSlot->reconnectScheduled = false;
            rawSlot->publisherChannels.clear();
            rawSlot->consumerChannels.clear();
            queuedPublishes_.fetch_sub(
                static_cast<std::uint64_t>(rawSlot->pendingPublishes.size()),
                std::memory_order_relaxed);
            rawSlot->pendingPublishes.clear();
            if (rawSlot->connection) {
                rawSlot->connection->close();
                rawSlot->connection.reset();
            }
            rawSlot->handler.reset();
        });
    }
}

void RabbitMqBroker::subscribe(const QueueConfig &config, MessageHandler handler) {
    auto subscription = std::make_shared<Subscription>();
    subscription->config = config;
    subscription->handler = std::move(handler);
    subscription->slotIndex =
        std::hash<std::string>{}(subscription->config.topic) %
        std::max<std::size_t>(std::size_t{1}, config_.connectionPoolSize);
    subscription->consumerTag =
        subscription->config.topic + "-" +
        std::to_string(static_cast<unsigned long long>(subscription->slotIndex));
    LOG_INFO << "RabbitMqBroker subscribe topic=" << subscription->config.topic
             << " queue=" << subscription->config.queueName
             << " slot=" << subscription->slotIndex;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_.push_back(subscription);
    }

    if (started_ && subscription->slotIndex < slots_.size()) {
        auto *loop = slots_[subscription->slotIndex]->loop;
        if (loop != nullptr) {
            loop->queueInLoop([this, subscription]() {
                if (subscription->slotIndex < slots_.size()) {
                    startConsumer(subscription->slotIndex, subscription);
                }
            });
        }
    }
}

bool RabbitMqBroker::publish(const BrokerPublishRequest &request, PublishCompletion completion) {
    if (!started_) {
        LOG_WARN << "publish rejected; broker not running topic=" << request.topic;
        if (completion) {
            completion(false, "broker is not running");
        }
        return false;
    }

    if (slots_.empty()) {
        LOG_WARN << "publish rejected; slots unavailable topic=" << request.topic;
        if (completion) {
            completion(false, "broker slots are not initialized");
        }
        return false;
    }

    publishRequests_.fetch_add(1, std::memory_order_relaxed);
    const std::size_t slotIndex =
        publishCursor_.fetch_add(1, std::memory_order_relaxed) % slots_.size();
    auto *loop = slots_[slotIndex]->loop;
    if (loop == nullptr) {
        LOG_WARN << "publish rejected; slot loop unavailable topic=" << request.topic
                 << " slot=" << slotIndex;
        if (completion) {
            completion(false, "broker slot loop unavailable");
        }
        return false;
    }

    PublishTask task{request, std::move(completion)};
    loop->queueInLoop([this, slotIndex, task = std::move(task)]() mutable {
        publishOnLoop(slotIndex, std::move(task));
    });

    return true;
}

BrokerHealthSnapshot RabbitMqBroker::snapshot() const {
    BrokerHealthSnapshot brokerSnapshot;
    brokerSnapshot.started = started_;
    brokerSnapshot.configuredConnections = config_.connectionPoolSize;
    brokerSnapshot.readyConnections = readyConnectionCount();
    brokerSnapshot.publishRequests = publishRequests_.load(std::memory_order_relaxed);
    brokerSnapshot.publishedMessages = publishedMessages_.load(std::memory_order_relaxed);
    brokerSnapshot.consumedMessages = consumedMessages_.load(std::memory_order_relaxed);
    brokerSnapshot.queuedPublishes = queuedPublishes_.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mutex_);
    brokerSnapshot.lastError = lastError_;

    for (const auto &subscription : subscriptions_) {
        if (!subscription) {
            continue;
        }

        if (subscription->slotIndex < slots_.size() && slots_[subscription->slotIndex] &&
            slots_[subscription->slotIndex]->ready) {
            brokerSnapshot.readyTopics.push_back(subscription->config.topic);
        }
    }

    return brokerSnapshot;
}

const BrokerConfig &RabbitMqBroker::config() const {
    return config_;
}

void RabbitMqBroker::initializeSlots() {
    if (!slots_.empty()) {
        return;
    }

    const std::size_t configuredConnections =
        std::max<std::size_t>(std::size_t{1}, config_.connectionPoolSize);
    const std::size_t ioLoops =
        std::max<std::size_t>(std::size_t{1}, drogon::app().getThreadNum());

    slots_.reserve(configuredConnections);
    for (std::size_t index = 0; index < configuredConnections; ++index) {
        auto slot = std::make_unique<Slot>();
        slot->index = index;
        slot->loop = drogon::app().getIOLoop(index % ioLoops);
        LOG_DEBUG << "Initialized broker slot=" << index
                  << " ioLoopIndex=" << (index % ioLoops);
        slots_.push_back(std::move(slot));
    }
}

void RabbitMqBroker::connectSlot(std::size_t index) {
    auto &slot = *slots_[index];
    if (slot.loop == nullptr) {
        LOG_WARN << "connectSlot skipped; null loop slot=" << index;
        return;
    }

    slot.loop->queueInLoop([this, &slot]() {
        LOG_INFO << "Connecting RabbitMQ slot=" << slot.index;
        slot.ready = false;
        slot.publisherChannels.clear();
        slot.consumerChannels.clear();
        slot.reconnectScheduled = false;

        slot.handler = std::make_shared<AmqpTrantorHandler>(
            [loop = slot.loop](AMQP::TcpConnection *) { return loop; },
            [this, slotIndex = slot.index](AMQP::TcpConnection *) { handleSlotReady(slotIndex); },
            [](AMQP::TcpConnection *) {},
            [this, slotIndex = slot.index](AMQP::TcpConnection *) {
                handleSlotFailure(slotIndex, "TCP connection lost");
            },
            [this, slotIndex = slot.index](AMQP::TcpConnection *) {
                handleSlotFailure(slotIndex, "AMQP connection detached");
            },
            [this, slotIndex = slot.index](AMQP::TcpConnection *,
                                           const std::string &message) {
                handleSlotFailure(slotIndex, message);
            });

        slot.connection =
            std::make_unique<AMQP::TcpConnection>(slot.handler.get(), AMQP::Address(config_.url));
    });
}

void RabbitMqBroker::handleSlotReady(std::size_t index) {
    auto &slot = *slots_[index];
    slot.loop->queueInLoop([this, &slot]() {
        LOG_INFO << "RabbitMQ slot ready slot=" << slot.index;
        slot.ready = true;
        slot.reconnectDelaySeconds = 1.0;
        rebuildChannels(slot);
        flushPendingPublishes(slot.index);
    });
}

void RabbitMqBroker::handleSlotFailure(std::size_t index, const std::string &reason) {
    if (index >= slots_.size()) {
        LOG_WARN << "handleSlotFailure ignored; invalid slot index=" << index
                 << " reason=" << reason;
        return;
    }

    auto &slot = *slots_[index];
    if (slot.loop == nullptr) {
        LOG_WARN << "handleSlotFailure skipped; null loop slot=" << index
                 << " reason=" << reason;
        return;
    }

    slot.loop->queueInLoop([this, &slot, reason]() {
        LOG_WARN << "RabbitMQ slot failure slot=" << slot.index << " reason=" << reason;
        slot.ready = false;
        slot.publisherChannels.clear();
        slot.consumerChannels.clear();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastError_ = joinError(slot.index, reason);
        }
        scheduleReconnect(slot.index);
    });
}

void RabbitMqBroker::scheduleReconnect(std::size_t index) {
    auto &slot = *slots_[index];
    if (!started_ || slot.loop == nullptr || slot.reconnectScheduled) {
        return;
    }

    slot.reconnectScheduled = true;
    const auto reconnectDelay = slot.reconnectDelaySeconds;
    slot.reconnectDelaySeconds = std::min(30.0, reconnectDelay * 2.0);
    LOG_INFO << "Scheduling RabbitMQ reconnect slot=" << index
             << " delaySeconds=" << reconnectDelay;

    slot.loop->runAfter(reconnectDelay, [this, index]() {
        if (!started_) {
            return;
        }
        connectSlot(index);
    });
}

void RabbitMqBroker::rebuildChannels(Slot &slot) {
    slot.publisherChannels.clear();
    slot.consumerChannels.clear();

    const auto publisherChannelCount =
        std::min<std::size_t>(config_.channelsPerConnection, config_.publisherPoolSize);
    for (std::size_t i = 0; i < std::max<std::size_t>(std::size_t{1}, publisherChannelCount);
         ++i) {
        auto channel = std::make_unique<AMQP::TcpChannel>(slot.connection.get());
        channel->onError([this, slotIndex = slot.index](const char *message) {
            handleSlotFailure(slotIndex,
                              message != nullptr ? message : "publisher channel error");
        });
        channel->declareExchange(config_.exchange.name,
                                 exchangeType(),
                                 exchangeFlags(),
                                 AMQP::Table{});
        slot.publisherChannels.push_back(std::move(channel));
    }

    std::vector<std::shared_ptr<Subscription>> localSubscriptions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        localSubscriptions = subscriptions_;
    }

    for (const auto &subscription : localSubscriptions) {
        if (subscription && subscription->slotIndex == slot.index) {
            startConsumer(slot.index, subscription);
        }
    }
}

void RabbitMqBroker::startConsumer(std::size_t slotIndex,
                                   const std::shared_ptr<Subscription> &subscription) {
    if (!subscription || slotIndex >= slots_.size()) {
        return;
    }

    auto &slot = *slots_[slotIndex];
    if (!slot.ready || !slot.connection) {
        return;
    }

    auto channel = std::make_unique<AMQP::TcpChannel>(slot.connection.get());
    auto *channelPtr = channel.get();
    channel->onError([this, slotIndex, topic = subscription->config.topic](const char *message) {
        handleSlotFailure(
            slotIndex,
            (message != nullptr ? std::string(message) : std::string("consumer error")) +
                " (" + topic + ")");
    });

    slot.consumerChannels[subscription->config.topic] = std::move(channel);

    channelPtr->setQos(subscription->config.prefetch)
        .onSuccess([this, slotIndex, subscription, channelPtr]() {
            channelPtr
                ->declareExchange(config_.exchange.name,
                                  exchangeType(),
                                  exchangeFlags(),
                                  AMQP::Table{})
                .onSuccess([this, slotIndex, subscription, channelPtr]() {
                    channelPtr
                        ->declareQueue(subscription->config.queueName,
                                       queueFlags(subscription->config))
                        .onSuccess([this, slotIndex, subscription, channelPtr](
                                       const std::string &, uint32_t, uint32_t) {
                            channelPtr
                                ->bindQueue(config_.exchange.name,
                                            subscription->config.queueName,
                                            subscription->config.bindingKey)
                                .onSuccess([this, slotIndex, subscription, channelPtr]() {
                                    channelPtr
                                        ->consume(subscription->config.queueName,
                                                  subscription->consumerTag)
                                        .onReceived([this, subscription, channelPtr](
                                                        const AMQP::Message &message,
                                                        std::uint64_t deliveryTag,
                                                        bool redelivered) {
                                            BrokerMessage brokerMessage;
                                            brokerMessage.topic = subscription->config.topic;
                                            brokerMessage.exchange = config_.exchange.name;
                                            brokerMessage.routingKey =
                                                subscription->config.bindingKey;
                                            brokerMessage.queueName =
                                                subscription->config.queueName;
                                            brokerMessage.payload.assign(
                                                message.body(),
                                                static_cast<std::size_t>(message.bodySize()));
                                            brokerMessage.redelivered = redelivered;

                                            subscription->handler(
                                                brokerMessage,
                                                [this, channelPtr, deliveryTag]() {
                                                    consumedMessages_.fetch_add(
                                                        1, std::memory_order_relaxed);
                                                    channelPtr->ack(deliveryTag);
                                                });
                                        })
                                        .onSuccess([](const std::string &) {})
                                        .onCancelled([this, slotIndex,
                                                      topic = subscription->config.topic](
                                                         const std::string &) {
                                            handleSlotFailure(slotIndex,
                                                              "consumer cancelled for " + topic);
                                        })
                                        .onError([this, slotIndex,
                                                  topic = subscription->config.topic](
                                                     const char *message) {
                                            handleSlotFailure(
                                                slotIndex,
                                                (message != nullptr
                                                     ? std::string(message)
                                                     : std::string("consume failed")) +
                                                    " (" + topic + ")");
                                        });
                                })
                                .onError([this, slotIndex,
                                          topic = subscription->config.topic](const char *message) {
                                    handleSlotFailure(
                                        slotIndex,
                                        (message != nullptr ? std::string(message)
                                                            : std::string("bind failed")) +
                                            " (" + topic + ")");
                                });
                        })
                        .onError([this, slotIndex,
                                  topic = subscription->config.topic](const char *message) {
                            handleSlotFailure(
                                slotIndex,
                                (message != nullptr ? std::string(message)
                                                    : std::string("queue declare failed")) +
                                    " (" + topic + ")");
                        });
                })
                .onError([this, slotIndex](const char *message) {
                    handleSlotFailure(slotIndex,
                                      message != nullptr ? message : "exchange declare failed");
                });
        })
        .onError([this, slotIndex](const char *message) {
            handleSlotFailure(slotIndex, message != nullptr ? message : "qos setup failed");
        });
}

void RabbitMqBroker::publishOnLoop(std::size_t slotIndex, PublishTask task) {
    auto &slot = *slots_[slotIndex];
    if (!slot.ready || slot.publisherChannels.empty()) {
        LOG_DEBUG << "Queueing publish while slot not ready slot=" << slotIndex
                  << " topic=" << task.request.topic;
        slot.pendingPublishes.push_back(std::move(task));
        queuedPublishes_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto *topicConfig = resolveTopic(task.request.topic);
    const std::string exchange =
        !task.request.exchange.empty() ? task.request.exchange : config_.exchange.name;
    const std::string routingKey = !task.request.routingKey.empty()
                                       ? task.request.routingKey
                                       : (topicConfig != nullptr ? topicConfig->bindingKey
                                                                 : task.request.topic);

    auto &channel = *slot.publisherChannels[slot.nextPublisherChannel %
                                            slot.publisherChannels.size()];
    slot.nextPublisherChannel =
        (slot.nextPublisherChannel + 1) % slot.publisherChannels.size();

    AMQP::Envelope envelope(task.request.payload.data(), task.request.payload.size());
    envelope.setPersistent(task.request.persistent);

    if (channel.publish(exchange, routingKey, envelope)) {
        publishedMessages_.fetch_add(1, std::memory_order_relaxed);
        LOG_TRACE << "Published message topic=" << task.request.topic
                  << " exchange=" << exchange
                  << " routingKey=" << routingKey;
        if (task.completion) {
            task.completion(true, "queued for broker delivery");
        }
        return;
    }

    LOG_WARN << "publishOnLoop failed to queue publish to channel slot=" << slotIndex
             << " topic=" << task.request.topic;
    slot.pendingPublishes.push_front(std::move(task));
    queuedPublishes_.fetch_add(1, std::memory_order_relaxed);
}

void RabbitMqBroker::flushPendingPublishes(std::size_t slotIndex) {
    if (slotIndex >= slots_.size()) {
        return;
    }

    auto &slot = *slots_[slotIndex];
    while (slot.ready && !slot.publisherChannels.empty() && !slot.pendingPublishes.empty()) {
        auto task = std::move(slot.pendingPublishes.front());
        slot.pendingPublishes.pop_front();
        queuedPublishes_.fetch_sub(1, std::memory_order_relaxed);
        publishOnLoop(slotIndex, std::move(task));
    }
}

const QueueConfig *RabbitMqBroker::resolveTopic(std::string_view topic) const {
    return config_.findTopic(topic);
}

AMQP::ExchangeType RabbitMqBroker::exchangeType() const {
    if (config_.exchange.type == "direct") {
        return AMQP::direct;
    }
    if (config_.exchange.type == "fanout") {
        return AMQP::fanout;
    }
    if (config_.exchange.type == "headers") {
        return AMQP::headers;
    }
    if (config_.exchange.type == "consistent_hash") {
        return AMQP::consistent_hash;
    }
    if (config_.exchange.type == "message_deduplication") {
        return AMQP::message_deduplication;
    }
    return AMQP::topic;
}

int RabbitMqBroker::exchangeFlags() const {
    int flags = 0;
    if (config_.exchange.durable) {
        flags |= AMQP::durable;
    }
    if (config_.exchange.autoDelete) {
        flags |= AMQP::autodelete;
    }
    return flags;
}

int RabbitMqBroker::queueFlags(const QueueConfig &config) const {
    int flags = 0;
    if (config.durable) {
        flags |= AMQP::durable;
    }
    if (config.autoDelete) {
        flags |= AMQP::autodelete;
    }
    if (config.exclusive) {
        flags |= AMQP::exclusive;
    }
    return flags;
}

std::size_t RabbitMqBroker::readyConnectionCount() const {
    std::size_t readyCount = 0;
    for (const auto &slot : slots_) {
        if (slot && slot->ready) {
            ++readyCount;
        }
    }
    return readyCount;
}

}  // namespace messaging
