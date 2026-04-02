#include "controllers/pricing_controller.h"

#include "ibkr/client_pool.h"

#include <drogon/drogon.h>
#include <json/json.h>
#include <trantor/utils/Logger.h>

#include <cctype>
#include <memory>
#include <string>

using namespace pricing;

namespace {

std::string toUpper(std::string s) {
    for (auto &c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

void postToAppLoop(std::function<void()> fn) {
    auto *loop = drogon::app().getIOLoop(0);
    if (loop) {
        loop->runInLoop(std::move(fn));
    } else {
        fn();
    }
}

}  // namespace

void PricingController::status(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
    auto poolStatus = ibkr::ClientPool::instance().status();
    Json::Value payload;
    payload["anyConnected"] = poolStatus.anyConnected;
    payload["totalSlots"] = static_cast<Json::Value::UInt64>(poolStatus.totalSlots);
    payload["connectedSlots"] = static_cast<Json::Value::UInt64>(poolStatus.connectedSlots);

    Json::Value slotsJson(Json::arrayValue);
    for (const auto &s : poolStatus.slots) {
        Json::Value slot;
        slot["connected"] = s.connected;
        slot["host"] = s.host;
        slot["port"] = s.port;
        slot["clientId"] = s.clientId;
        slot["serverVersion"] = s.serverVersion;
        slot["connectionTime"] = s.connectionTime;
        slot["errorCode"] = s.lastErrorCode;
        slot["errorMessage"] = s.lastErrorMessage;
        slotsJson.append(slot);
    }
    payload["slots"] = slotsJson;

    LOG_DEBUG << "PricingController: reporting IBKR client pool status";

    callback(drogon::HttpResponse::newHttpJsonResponse(payload));
}

void PricingController::bySecType(
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    const std::string &secType) {
    const std::string kind = toUpper(secType);

    auto sendJson = [&callback](const Json::Value &body, drogon::HttpStatusCode code) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(code);
        callback(resp);
    };

    if (kind == "FUT") {
        Json::Value e;
        e["error"] = "Futures pricing is not supported";
        e["secType"] = "FUT";
        sendJson(e, drogon::k400BadRequest);
        return;
    }
    if (kind == "OPT") {
        Json::Value e;
        e["error"] = "Option pricing is not implemented yet";
        e["secType"] = "OPT";
        sendJson(e, drogon::k501NotImplemented);
        return;
    }
    if (kind != "STK") {
        Json::Value e;
        e["error"] = "Unsupported secType";
        e["secType"] = secType;
        sendJson(e, drogon::k400BadRequest);
        return;
    }

    const auto &params = request->getParameters();
    const auto symIt = params.find("symbol");
    const std::string symbol =
        (symIt != params.end()) ? std::string(symIt->second) : std::string();
    if (symbol.empty()) {
        Json::Value e;
        e["error"] = "Missing required query parameter: symbol";
        e["secType"] = "STK";
        sendJson(e, drogon::k400BadRequest);
        return;
    }

    auto cb = std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
        std::move(callback));
    ibkr::ClientPool::instance().requestStockSnapshot(
        symbol,
        [cb](Json::Value json) mutable {
            postToAppLoop([cb, j = std::move(json)]() mutable {
                (*cb)(drogon::HttpResponse::newHttpJsonResponse(j));
            });
        },
        [cb](std::string err) mutable {
            postToAppLoop([cb, e = std::move(err)]() mutable {
                Json::Value body;
                body["error"] = e;
                body["secType"] = "STK";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(drogon::k502BadGateway);
                (*cb)(resp);
            });
        });

    LOG_DEBUG << "PricingController: STK snapshot requested symbol=" << symbol;
}
