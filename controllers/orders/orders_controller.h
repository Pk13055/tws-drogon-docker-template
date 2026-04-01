#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace orders {
class OrdersController : public drogon::HttpController<OrdersController> {
  public:
    METHOD_LIST_BEGIN
    // Add order routes here as the module grows.
    METHOD_LIST_END
};
}  // namespace orders
