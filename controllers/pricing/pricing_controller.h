#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace pricing {
class PricingController : public drogon::HttpController<PricingController> {
  public:
    METHOD_LIST_BEGIN
    // Add pricing routes here as the module grows.
    METHOD_LIST_END
};
}  // namespace pricing
