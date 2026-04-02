#pragma once

#include <drogon/HttpController.h>

namespace tws::controllers {

class MessagingController : public drogon::HttpController<MessagingController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MessagingController::status, "/messaging/status", drogon::Get);
    ADD_METHOD_TO(MessagingController::publish,
                  "/messaging/publish/{1}",
                  drogon::Post,
                  drogon::Options);
    METHOD_LIST_END

    void status(const drogon::HttpRequestPtr &request,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    void publish(const drogon::HttpRequestPtr &request,
                 std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                 std::string topic) const;
};

}  // namespace tws::controllers
