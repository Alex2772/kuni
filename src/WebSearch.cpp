#include "WebSearch.h"

#include "IOpenAIChat.h"
#include "OpenAIChatImpl.h"
#include "OpenAITools.h"
#include "AUI/Json/Conversion.h"
#include "AUI/Curl/ACurl.h"
#include "AUI/Logging/ALogger.h"
#include "config.h"
#include "util/secrets.h"

#include <range/v3/view/transform.hpp>

using namespace std::chrono_literals;

static constexpr auto LOG_TAG = "WebSearch";

AJSON_FIELDS(web::Result,
    AJSON_FIELDS_ENTRY(title)
    AJSON_FIELDS_ENTRY(url)
    AJSON_FIELDS_ENTRY(content)
    )

AFuture<AVector<web::Result>> web::search(AString query, int maxResults) {
    ALOG_TRACE(LOG_TAG) << "web::search: " << query;
    // Build JSON body
    AJson body = AJson::Object{{"query", std::move(query)} };
    if (maxResults> 0) {
        body["max_results"] = maxResults;
    }
    AVector<AString> headers = {
        "Content-Type: application/json",
        "Authorization: Bearer {}"_format(util::secrets()["ollama"]["bearer_key"].as_string()),
    };
    auto response = AJson::fromBuffer((co_await ACurl::Builder("https://ollama.com/api/web_search")
                                           .withMethod(ACurl::Method::HTTP_POST)
                                           .withHeaders(std::move(headers))
                                           .withBody(AJson::toString(body))
                                           .runAsync())
                                          .body);
    if (response.contains("error")) {
        throw AException("Ollama web search error: " + AJson::toString(response["error"]));
    }
    ALOG_TRACE(LOG_TAG) << "Response: " << AJson::toString(response);
    co_return aui::from_json<AVector<Result>>(response["results"]);
}
