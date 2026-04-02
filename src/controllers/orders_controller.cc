#include "controllers/orders_controller.h"

#include "ibkr/client_pool.h"

#include <json/json.h>
#include <trantor/utils/Logger.h>

using namespace orders;

void OrdersController::status(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
    auto poolStatus = ibkr::ClientPool::instance().status();
    Json::Value payload;
    payload["anyConnected"] = poolStatus.anyConnected;
    payload["totalSlots"] = static_cast<Json::Value::UInt64>(poolStatus.totalSlots);
    payload["connectedSlots"] = static_cast<Json::Value::UInt64>(poolStatus.connectedSlots);

    const bool sdk_ready = poolStatus.anyConnected;
    payload["sdkReady"] = sdk_ready;

    if (sdk_ready) {
        LOG_DEBUG << "OrdersController: IBKR client pool available for order placement";
    } else {
        LOG_WARN << "OrdersController: IBKR client pool not available";
    }

    callback(drogon::HttpResponse::newHttpJsonResponse(payload));
}
