#include "messaging_controller.h"

#include "messaging/messaging_registry.h"

#include <drogon/HttpResponse.h>

namespace tws::controllers {

void MessagingController::status(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const {
    callback(drogon::HttpResponse::newHttpJsonResponse(
        messaging::MessagingRegistry::instance().statusJson()));
}

void MessagingController::publish(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string topic) const {
    const std::string payload = request->body().empty()
                                    ? std::string("{\"source\":\"http\",\"topic\":\"") +
                                          topic + "\",\"message\":\"test\"}"
                                    : std::string(request->body());

    Json::Value response;
    response["topic"] = topic;
    response["accepted"] =
        messaging::MessagingRegistry::instance().publishTopic(topic, payload);
    response["payload"] = payload;

    callback(drogon::HttpResponse::newHttpJsonResponse(response));
}

}  // namespace tws::controllers
