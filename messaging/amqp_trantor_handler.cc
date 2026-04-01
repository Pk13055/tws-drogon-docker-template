#include "messaging/amqp_trantor_handler.h"

#include <algorithm>
#include <utility>

namespace messaging {

AmqpTrantorHandler::AmqpTrantorHandler(LoopResolver loopResolver,
                                       ConnectionCallback readyCallback,
                                       ConnectionCallback connectedCallback,
                                       ConnectionCallback lostCallback,
                                       ConnectionCallback detachedCallback,
                                       ErrorCallback errorCallback)
    : loopResolver_(std::move(loopResolver)),
      readyCallback_(std::move(readyCallback)),
      connectedCallback_(std::move(connectedCallback)),
      lostCallback_(std::move(lostCallback)),
      detachedCallback_(std::move(detachedCallback)),
      errorCallback_(std::move(errorCallback)) {}

void AmqpTrantorHandler::monitor(AMQP::TcpConnection *connection, int fd, int flags) {
    auto *loop = loopResolver_(connection);
    if (loop == nullptr) {
        return;
    }

    loop->runInLoop([this, connection, fd, flags]() { applyMonitor(connection, fd, flags); });
}

void AmqpTrantorHandler::onConnected(AMQP::TcpConnection *connection) {
    if (connectedCallback_) {
        connectedCallback_(connection);
    }
}

void AmqpTrantorHandler::onReady(AMQP::TcpConnection *connection) {
    if (readyCallback_) {
        readyCallback_(connection);
    }
}

void AmqpTrantorHandler::onError(AMQP::TcpConnection *connection, const char *message) {
    if (errorCallback_) {
        errorCallback_(connection, message != nullptr ? message : "unknown AMQP error");
    }
}

void AmqpTrantorHandler::onLost(AMQP::TcpConnection *connection) {
    if (lostCallback_) {
        lostCallback_(connection);
    }
}

void AmqpTrantorHandler::onDetached(AMQP::TcpConnection *connection) {
    teardown(connection);
    if (detachedCallback_) {
        detachedCallback_(connection);
    }
}

std::uint16_t AmqpTrantorHandler::onNegotiate(AMQP::TcpConnection *, std::uint16_t interval) {
    return std::max<std::uint16_t>(interval, 30);
}

void AmqpTrantorHandler::applyMonitor(AMQP::TcpConnection *connection, int fd, int flags) {
    std::shared_ptr<WatchedSocket> watchedSocket;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &entry = sockets_[connection];
        if (!entry) {
            entry = std::make_shared<WatchedSocket>();
            entry->loop = loopResolver_(connection);
        }
        watchedSocket = entry;
    }

    if (watchedSocket->loop == nullptr) {
        return;
    }

    auto &channel = watchedSocket->channels[fd];
    if (!channel) {
        channel = std::make_unique<trantor::Channel>(watchedSocket->loop, fd);
        channel->setReadCallback(
            [connection, fd]() { connection->process(fd, AMQP::readable); });
        channel->setWriteCallback(
            [connection, fd]() { connection->process(fd, AMQP::writable); });
        channel->setCloseCallback([connection, fd]() {
            connection->process(fd, AMQP::readable | AMQP::writable);
        });
        channel->setErrorCallback([connection, fd]() {
            connection->process(fd, AMQP::readable | AMQP::writable);
        });
    }

    if ((flags & AMQP::readable) != 0) {
        channel->enableReading();
    } else if (channel->isReading()) {
        channel->disableReading();
    }

    if ((flags & AMQP::writable) != 0) {
        channel->enableWriting();
    } else if (channel->isWriting()) {
        channel->disableWriting();
    }

    if (flags == 0) {
        channel->disableAll();
        channel->remove();
        watchedSocket->channels.erase(fd);
    }
}

void AmqpTrantorHandler::teardown(AMQP::TcpConnection *connection) {
    std::shared_ptr<WatchedSocket> watchedSocket;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = sockets_.find(connection);
        if (found == sockets_.end()) {
            return;
        }
        watchedSocket = found->second;
        sockets_.erase(found);
    }

    if (!watchedSocket || watchedSocket->loop == nullptr) {
        return;
    }

    watchedSocket->loop->runInLoop([watchedSocket]() {
        for (auto &[fd, channel] : watchedSocket->channels) {
            (void)fd;
            if (!channel) {
                continue;
            }
            channel->disableAll();
            channel->remove();
        }
        watchedSocket->channels.clear();
    });
}

}  // namespace messaging
