#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace orders {
class OrdersController : public drogon::HttpController<OrdersController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(OrdersController::status, "/orders/status", Get);
    METHOD_LIST_END

    void status(const drogon::HttpRequestPtr &,
                std::function<void(const drogon::HttpResponsePtr &)> &&);
};
}  // namespace orders
