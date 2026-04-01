#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include "messaging/broker_config.h"
#include "ibkr/ibkr_probe.h"
#include "messaging/messaging_registry.h"
#include "messaging/rabbitmq_broker.h"

#include <cstdlib>
#include <memory>

int main() {
    drogon::app().setLogLevel(trantor::Logger::kInfo);
    LOG_INFO << "Starting tws service bootstrap";

    auto brokerConfig = messaging::BrokerConfig::fromEnvironment();
    auto broker = std::make_shared<messaging::RabbitMqBroker>(brokerConfig);
    messaging::MessagingRegistry::instance().initialize(broker);
    LOG_INFO << "Messaging registry initialized";

    for (const auto &subscription : brokerConfig.subscriptions) {
        LOG_INFO << "Registering subscription topic=" << subscription.topic
                 << " queue=" << subscription.queueName
                 << " bindingKey=" << subscription.bindingKey;
        broker->subscribe(subscription,
                          [](const messaging::BrokerMessage &message,
                             messaging::AckCallback ack) {
                              LOG_TRACE << "Observed broker message topic=" << message.topic
                                        << " queue=" << message.queueName
                                        << " payloadSize=" << message.payload.size();
                              messaging::MessagingRegistry::instance().recordObservedMessage(
                                  message);
                              ack();
                          });
    }

    drogon::app().registerHandler(
        "/tws/ping",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            LOG_DEBUG << "Received /tws/ping request";
            const auto snapshot = messaging::MessagingRegistry::instance().statusJson();
            const auto ibkr = ibkr::probeConnection();
            Json::Value payload;
            payload["ping"] = "pong";
            payload["rabbitmq_ready"] = snapshot["ready_connections"].asUInt64() > 0;
            payload["rabbitmq_exchange"] = snapshot["exchange"];
            payload["ibkr"]["connected"] = ibkr.connected;
            payload["ibkr"]["host"] = ibkr.host;
            payload["ibkr"]["port"] = ibkr.port;
            payload["ibkr"]["clientId"] = ibkr.client_id;
            payload["ibkr"]["attempts"] = ibkr.attempts;
            payload["ibkr"]["serverVersion"] = ibkr.server_version;
            payload["ibkr"]["connectionTime"] = ibkr.connection_time;
            payload["ibkr"]["currentTime"] = Json::Int64(ibkr.current_time);
            payload["ibkr"]["errorCode"] = ibkr.error_code;
            payload["ibkr"]["error"] = ibkr.error_message;

            if (ibkr.connected) {
                LOG_INFO << "IBKR probe connected on /tws/ping with clientId="
                         << ibkr.client_id << " serverVersion=" << ibkr.server_version;
            } else {
                LOG_WARN << "IBKR probe failed on /tws/ping for " << ibkr.host << ':'
                         << ibkr.port << " errorCode=" << ibkr.error_code
                         << " error=" << ibkr.error_message;
            }
            callback(drogon::HttpResponse::newHttpJsonResponse(payload));
        },
        {drogon::Get});

    drogon::app().registerBeginningAdvice([]() {
        LOG_INFO << "Drogon beginning advice: starting messaging registry";
        messaging::MessagingRegistry::instance().start();
    });

    std::atexit([]() {
        LOG_INFO << "Process exit handler: stopping messaging registry";
        messaging::MessagingRegistry::instance().stop();
    });

    drogon::app().setThreadNum(1);
    drogon::app().addListener("0.0.0.0", 8080);
    LOG_INFO << "tws listening on 0.0.0.0:8080";
    drogon::app().run();
}
