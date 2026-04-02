#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace pricing {
class PricingController : public drogon::HttpController<PricingController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(PricingController::status, "/pricing/status", Get);
    ADD_METHOD_TO(PricingController::bySecType, "/pricing/{secType}", Get);
    METHOD_LIST_END

    void status(const drogon::HttpRequestPtr &,
                std::function<void(const drogon::HttpResponsePtr &)> &&);
    void bySecType(const drogon::HttpRequestPtr &,
                   std::function<void(const drogon::HttpResponsePtr &)> &&,
                   const std::string &secType);
};
}  // namespace pricing
