#pragma once

#include <amqpcpp.h>
#include <amqpcpp/linux_tcp.h>
#include <trantor/net/Channel.h>
#include <trantor/net/EventLoop.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace messaging {

class AmqpTrantorHandler : public AMQP::TcpHandler {
  public:
    using LoopResolver = std::function<trantor::EventLoop *(AMQP::TcpConnection *)>;
    using ConnectionCallback = std::function<void(AMQP::TcpConnection *)>;
    using ErrorCallback =
        std::function<void(AMQP::TcpConnection *, const std::string &message)>;

    AmqpTrantorHandler(LoopResolver loopResolver,
                       ConnectionCallback readyCallback,
                       ConnectionCallback connectedCallback,
                       ConnectionCallback lostCallback,
                       ConnectionCallback detachedCallback,
                       ErrorCallback errorCallback);

    void monitor(AMQP::TcpConnection *connection, int fd, int flags) override;
    void onConnected(AMQP::TcpConnection *connection) override;
    void onReady(AMQP::TcpConnection *connection) override;
    void onError(AMQP::TcpConnection *connection, const char *message) override;
    void onLost(AMQP::TcpConnection *connection) override;
    void onDetached(AMQP::TcpConnection *connection) override;
    std::uint16_t onNegotiate(AMQP::TcpConnection *connection,
                              std::uint16_t interval) override;

  private:
    struct WatchedSocket {
        trantor::EventLoop *loop{nullptr};
        std::unordered_map<int, std::unique_ptr<trantor::Channel>> channels;
    };

    void applyMonitor(AMQP::TcpConnection *connection, int fd, int flags);
    void teardown(AMQP::TcpConnection *connection);

    LoopResolver loopResolver_;
    ConnectionCallback readyCallback_;
    ConnectionCallback connectedCallback_;
    ConnectionCallback lostCallback_;
    ConnectionCallback detachedCallback_;
    ErrorCallback errorCallback_;

    std::mutex mutex_;
    std::unordered_map<AMQP::TcpConnection *, std::shared_ptr<WatchedSocket>> sockets_;
};

}  // namespace messaging
