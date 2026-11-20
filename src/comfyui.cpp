#include "comfyui.h"

#include "AUI/Curl/ACurl.h"
#include "AUI/Curl/ACurlMulti.h"
#include "AUI/Curl/AFormMultipart.h"
#include "AUI/Curl/AWebsocket.h"
#include "AUI/IO/AByteBufferInputStream.h"
#include "AUI/Image/png/PngImageLoader.h"
#include "AUI/Json/Conversion.h"
#include "AUI/Logging/ALogger.h"
#include "AUI/Util/ARandom.h"
#include "config.h"

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

static constexpr auto LOG_TAG = "comfyui";

using namespace std::chrono_literals;


namespace {

AString wsUrl(const AString& baseUrl, const AString& clientId) {
    AUI_ASSERT(baseUrl.endsWith("/"));
    return  "{}ws?clientId={}"_format(baseUrl, clientId);
}

AVector<AString> authHeaders(const Endpoint& endpoint, AVector<AString> extra = {}) {
    if (!endpoint.bearerKey.empty()) {
        extra << "Authorization: Bearer {}"_format(endpoint.bearerKey);
    }
    return extra;
}

}   // namespace

AFuture<comfy::PromptResult> comfy::prompt(AJson workflow) {
    ALOG_TRACE(LOG_TAG) << "prompt";
    if (workflow.isObject()) {
        // A regular "Save" (UI/front-end) workflow export has top-level keys like "nodes"/"links"/"groups"/
        // "last_node_id", where node entries are objects nested under "nodes" (an array), not top-level
        // "<id>": {"class_type": ..., "inputs": ...} entries. Sending that straight to /prompt makes ComfyUI's
        // server (and some custom nodes, e.g. Impact-Pack) blow up with cryptic errors like
        // "argument of type 'int' is not a container or iterable" because it iterates top-level values expecting
        // node objects and finds plain ints/arrays (e.g. "revision": 0, "last_node_id": 116) instead.
        // You need to export via ComfyUI's "Save (API format)" (or "Export (API)") button instead.
        if (workflow.contains("nodes") || workflow.contains("links") || workflow.contains("last_node_id")) {
            throw AException(
                "comfy::prompt: workflow looks like a UI export (has \"nodes\"/\"links\"/\"last_node_id\" keys), "
                "not an API format workflow. Re-export it using ComfyUI's \"Save (API format)\" button.");
        }
    }
    const auto endpoint = config().comfyEndpoint;

    static ARandom r;
    auto clientId = r.nextUuid().toString();

    const auto url = wsUrl(endpoint.baseUrl, clientId);

    AString promptId;
    AJson body = AJson::Object{
        {"prompt", std::move(workflow)},
        {"client_id", clientId},
    };

    auto queueResponse = AJson::fromBuffer((co_await ACurl::Builder(endpoint.baseUrl + "prompt")
                                                 .withMethod(ACurl::Method::HTTP_POST)
                                                 .withHeaders(authHeaders(endpoint, {"Content-Type: application/json"}))
                                                 .withBody(AJson::toString(body).toStdString())
                                                 .withTimeout(config().requestTimeoutSecs)
                                                 .runAsync())
                                                .body);

    if (queueResponse.contains("error")) {
        throw AException("comfy::prompt: {}"_format(AJson::toString(queueResponse["error"])));
    }

    promptId = queueResponse["prompt_id"].asString();
    ALogger::info(LOG_TAG) << "Queued prompt_id=" << promptId;



    auto history = co_await [&]() -> AFuture<AJson> {
        for (size_t maxTrials = 100; maxTrials > 0; --maxTrials) {
            auto historyResponse = AJson::fromBuffer((co_await ACurl::Builder(endpoint.baseUrl + "history/{}"_format(promptId))
                                                           .withHeaders(authHeaders(endpoint))
                                                           .withTimeout(config().requestTimeoutSecs)
                                                           .runAsync())
                                                          .body);
            if (auto r = historyResponse.containsOpt(promptId)) {
                co_return *r;
            }
            co_await AThread::asyncSleep(1s);
        }
        throw AException("timeout");
    }();

    PromptResult result;
    result.history = history;

    const auto& outputs = history["outputs"];
    if (outputs.isObject()) {
        for (const auto& [nodeId, nodeOutput] : outputs.asObject()) {
            if (!nodeOutput.contains("images")) {
                continue;
            }
            for (const auto& image : nodeOutput["images"].asArray()) {
                auto filename = image["filename"].asString();
                auto subfolder = image["subfolder"].asStringOpt().valueOr("");
                auto type = image["type"].asStringOpt().valueOr("output");

                auto imageResponse = co_await ACurl::Builder(endpoint.baseUrl + "view")
                                          .withParams({
                                              {"filename", filename},
                                              {"subfolder", subfolder},
                                              {"type", type},
                                          })
                                          .withHeaders(authHeaders(endpoint))
                                          .withTimeout(config().requestTimeoutSecs)
                                          .runAsync();

                result.images << AImage::fromBuffer(imageResponse.body);
            }
        }
    }

    co_return result;
}

AFuture<AString> comfy::uploadImage(const AImage& image, AString filename) {
    ALOG_TRACE(LOG_TAG) << "uploadImage: " << filename;
    const auto endpoint = config().comfyEndpoint;

    AByteBuffer png;
    PngImageLoader::save(png, image);

    AFormMultipart form;
    form["image"] = { .value = std::move(png), .filename = filename, .mimeType = "image/png" };
    form["overwrite"] = { .value = AString("true") };

    auto response = co_await ACurl::Builder(endpoint.baseUrl + "upload/image")
                        .withMethod(ACurl::Method::HTTP_POST)
                        .withHeaders(authHeaders(endpoint))
                        .withMultipart(form)
                        .withTimeout(config().requestTimeoutSecs)
                        .runAsync();

    if (response.code != ACurl::ResponseCode::HTTP_200_OK) {
        throw AException("comfy::uploadImage: HTTP {}: {}"_format(int(response.code), AString::fromUtf8(response.body)));
    }

    auto json = AJson::fromBuffer(response.body);
    co_return json["name"].asStringOpt().valueOr(std::move(filename));
}

AFuture<> comfy::unload() {
    co_await ACurl::Builder(config().comfyEndpoint.baseUrl + "free")
        .withBody(R"({"unload_models": true, "free_memory": true})")
        .withMethod(ACurl::Method::HTTP_POST)
        .runAsync();
}
