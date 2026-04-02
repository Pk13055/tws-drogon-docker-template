#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include "ibkr/client_pool.h"
#include "messaging/broker_config.h"
#include "messaging/messaging_registry.h"
#include "messaging/rabbitmq_broker.h"

#include <cstdlib>
#include <json/json.h>
#include <memory>
#include <string>

namespace {

/// Strip this prefix from incoming paths before routing (see registerPreRoutingAdvice).
/// Public URLs remain e.g. /tws/ping; handlers register /ping.
const char *kRoutePrefixEnv = "TWS_ROUTE_PREFIX";

std::string envOrDefault(const char *name, std::string default_value) {
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::move(default_value);
}

int envOrDefaultInt(const char *name, int default_value) {
    const char *value = std::getenv(name);
    if (!value) {
        return default_value;
    }
    return std::atoi(value);
}

/// If path begins with `prefix` followed by end or `/`, rewrite to the remainder (or `/`).
void stripPathPrefix(const drogon::HttpRequestPtr &req,
                     const std::string &prefix) {
    if (prefix.empty() || prefix[0] != '/') {
        return;
    }
    const std::string &path = req->getPath();
    if (path.size() < prefix.size()) {
        return;
    }
    if (path.compare(0, prefix.size(), prefix) != 0) {
        return;
    }
    if (path.size() > prefix.size() && path[prefix.size()] != '/') {
        return;
    }
    if (path.size() == prefix.size()) {
        req->setPath("/");
        return;
    }
    req->setPath(path.substr(prefix.size()));
}

}  // namespace

int main() {
    drogon::app().setLogLevel(trantor::Logger::kInfo);
    LOG_INFO << "Starting tws service bootstrap";

    const std::string routePrefix = envOrDefault(kRoutePrefixEnv, "/tws");
    drogon::app().registerPreRoutingAdvice(
        [routePrefix](const drogon::HttpRequestPtr &req) {
            stripPathPrefix(req, routePrefix);
        });

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
        "/ping",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            LOG_DEBUG << "Received /ping request";
            const auto snapshot = messaging::MessagingRegistry::instance().statusJson();
            const auto poolStatus = ibkr::ClientPool::instance().status();

            Json::Value payload;
            payload["ping"] = "pong";
            payload["rabbitmq_ready"] = snapshot["ready_connections"].asUInt64() > 0;
            payload["rabbitmq_exchange"] = snapshot["exchange"];
            
            payload["ibkr"]["any_connected"] = poolStatus.anyConnected;
            payload["ibkr"]["total_slots"] = static_cast<Json::Value::UInt64>(poolStatus.totalSlots);
            payload["ibkr"]["connected_slots"] = static_cast<Json::Value::UInt64>(poolStatus.connectedSlots);
            
            Json::Value slotsJson(Json::arrayValue);
            for (const auto &s : poolStatus.slots) {
                Json::Value slot;
                slot["connected"] = s.connected;
                slot["connecting"] = s.connecting;
                slot["host"] = s.host;
                slot["port"] = s.port;
                slot["clientId"] = s.clientId;
                slot["serverVersion"] = s.serverVersion;
                slot["connectionTime"] = s.connectionTime;
                slot["errorCode"] = s.lastErrorCode;
                slot["error"] = s.lastErrorMessage;
                slot["pendingRequests"] = static_cast<Json::Value::UInt64>(s.pendingRequests);
                slotsJson.append(slot);
            }
            payload["ibkr"]["slots"] = slotsJson;

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
        LOG_INFO << "Process exit handler: shutting down IBKR client pool";
        ibkr::ClientPool::instance().shutdown();
    });

    const std::string ibkr_host = envOrDefault("IBKR_HOST", "ibkr");
    const int ibkr_port = envOrDefaultInt("IBKR_PORT", 8888);
    const int ibkr_client_id = envOrDefaultInt("IBKR_CLIENT_ID", 30);
    const int ibkr_slots = envOrDefaultInt("IBKR_SLOTS", 1);
    
    ibkr::ClientPool::instance().initialize(ibkr_host, ibkr_port, ibkr_client_id, ibkr_slots);

    drogon::app().setThreadNum(1);
    drogon::app().addListener("0.0.0.0", 8080);
    LOG_INFO << "tws listening on 0.0.0.0:8080";
    drogon::app().run();
}
