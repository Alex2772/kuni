#include "WebSearch.h"

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

AFuture<AString> web::searchAI(AString query) {
    ALOG_TRACE(LOG_TAG) << "web::searchAI: " << query;
    OpenAITools tools {
        OpenAITools::Tool {
            .name = "query",
            .description = "perform search in internet via search engine",
            .parameters = {
                .properties = {
                    {"text", {.type = "string", .description =
                        "Query optimized for a web search engine."
                    }},
                },
                .required = {"text"},
            },
            .handler = [](OpenAITools::Ctx ctx) -> AFuture<AString> {
                auto cue = ctx.args["text"].asStringOpt().valueOrException("text is required string");
                auto response = co_await web::search(cue);
                AString formattedResponse;
                ALOG_DEBUG(LOG_TAG) << "searchAI cue=\"" << cue << "\" found=" << (response | ranges::view::transform([&](const Result& e) -> AString {
                    return e.title;
                }));
                for (const auto& i : response) {
                    formattedResponse += R"(<search_result title="{}">
{}
</search_result>
)"_format(i.title, i.content);
                }
                if (formattedResponse.empty()) {
                    co_return "No data was found";
                }
                co_return formattedResponse;
            },
        },
    };
    OpenAIChat chat {
        .systemPrompt = R"(
You are a internet researcher.

You are pro at forming Google queries.

The user asks you a question. Your job is to retrieve data solely from #query tool. Your job is to output data that
fully satisfies user's query and would be helpful.

Also, please include additional details that does not necessarily address the question but might be helpful to improve
quality of subsequent processing of your response.

Do not alter facts.

Do not make up facts. Rely exclusively on provided context.
)",
        .tools = tools.asJson(),
    };

    AVector<OpenAIChat::Message> messages = {
        OpenAIChat::Message {
            .role = OpenAIChat::Message::Role::USER,
            .content = query,
        },
    };

    bool toolCallHappened = false;

    for (;;) {
        auto botAnswer = (co_await chat.chat(messages)).choices.at(0).message;
        messages << botAnswer;
        if (botAnswer.tool_calls.empty()) {
            if (!toolCallHappened) {
                ALogger::warn(LOG_TAG) << "searchAI: no tool call happened, pointing that out to the LLM and trying again";
                messages << OpenAIChat::Message {
                    .role = OpenAIChat::Message::Role::USER,
                    .content = "you must perform at least one call to #query",
                };
                continue;
            }
            co_return botAnswer.content;
        }
        toolCallHappened = true;
        auto toolCalls = co_await tools.handleToolCalls(botAnswer.tool_calls);
        messages << toolCalls;
    }
}
