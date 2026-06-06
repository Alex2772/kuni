//
// Created by alex2772 on 6/6/26.
//

#include "ask.h"

#include "AppBase.h"

#include <WebSearch.h>
#include <range/v3/all.hpp>

static constexpr auto LOG_TAG = "ask";

static AFuture<AString>
queryDiary(IOpenAIChat& openAI, Diary& diary, ASet<AString> includedIds, const AString& cue, const Diary::QueryOpts& opts) {
    auto diaryResponse = co_await diary.query(co_await openAI.embedding({ .config = config::ENDPOINT_EMBEDDING }, cue), [&] {
        auto optsCopy = opts;
        optsCopy.maxEntryCount *= 10;
        return optsCopy;
    }());
    diaryResponse =
        diaryResponse | ranges::view::filter([&](const Diary::EntryExAndRelatedness& e) {
            return !includedIds.contains(e.entry->id);
        }) |
        ranges::view::take(opts.maxEntryCount) | ranges::to_vector;

    ALOG_DEBUG(LOG_TAG)
        << "queryDiary cue=\"" << cue << "\" found="
        << (diaryResponse | ranges::view::transform([&](const Diary::EntryExAndRelatedness& e) -> AString {
                return "({}.md,relatedness={})"_format(e.entry->id, e.relatedness);
            }));
    AString formattedResponse;
    for (const auto& i : diaryResponse) {
        i.entry->metadata.score += (i.relatedness - 0.5f) * 2.f;
        i.entry->incrementUsageCount();
        diary.save(*i.entry);

        includedIds << i.entry->id;
        formattedResponse += R"(<memory_piece relatedness="{}">
{}
</memory_piece>
)"_format(i.relatedness, i.entry->freeformBody);
    }
    if (formattedResponse.empty()) {
        if (!diaryResponse.empty()) {
            co_return "All memory pieces conforming your query were provided already. Use other query.";
        }
        co_return "No data was found";
    }

    co_return formattedResponse;
}

static AFuture<AString> queryWeb(const AString& cue) {
    AString out;

    auto webResponse = co_await web::search(cue);

    ALOG_DEBUG(LOG_TAG)
        << "queryWeb cue=\"" << cue << "\" found="
        << (webResponse | ranges::view::transform([&](const web::Result& e) -> AString {
                return e.title;
            }));
    for (const auto& result : webResponse) {
        out += "<web_search_result title=\"{}\" url=\"{}\">\n{}\n</web_search_result>\n"_format(result.title, result.url, result.content);
    }
    co_return out;
}

static AFuture<AString> ask(IOpenAIChat& openAI, Diary& diary, const AString& query, const Diary::QueryOpts& opts) {
    ALOG_DEBUG(LOG_TAG) << "ask query=\"" << query << "\"";
    ASet<AString> includedIds;
    OpenAITools tools {
        OpenAITools::Tool {
            .name = "query",
            .description = "Perform embedding-based search on RAG database",
            .parameters = {
                .properties = {
                    {"text", {.type = "string", .description =
                        "Retrieval cue that is likely to appear in memory pieces. This text will be transformed to "
                        "embedding as is, and its embedding will be compared against memory pieces. Include as many"
                        "keywords as possible; maintain meaning of the request."
                    }},
                    {"include_web_search_results", {.type = "boolean", .description =
                        "In addition to local database, append with results from web search. Set to true if you "
                        "believe you are searching for public or recent information. Defaults to false."
                    }},
                },
                .required = {"text"},
            },
            .handler = [&](OpenAITools::Ctx ctx) -> AFuture<AString> {
                const auto cue = ctx.args["text"].asStringOpt().valueOrException("text is required string");
                const auto includeWebSearchResults = ctx.args["include_web_search_results"].asBoolOpt().valueOr(false);
                ALOG_DEBUG(LOG_TAG) << "subagent query=\"" << cue << "\", includeWebSearchResults=" << includeWebSearchResults;
                AString out;
                if (includeWebSearchResults) {
                    out += "<local_db_results>\n";
                }
                out += co_await queryDiary(openAI, diary, includedIds, cue, opts);
                if (includeWebSearchResults) {
                    out += "\n</local_db_results>\n";
                }

                if (includeWebSearchResults) {
                    out += "<web_search_results>\n{}\n</web_search_results>\n"_format(co_await queryWeb(cue));
                }

                co_return out;
            },
        },
    };

    AVector<IOpenAIChat::Message> messages = {
        IOpenAIChat::Message {
          .role = IOpenAIChat::Message::Role::USER,
          .content = "<character>\n{}\n</character>\n\n{}"_format(AppBase::getSystemPrompt(), query),
        },
    };

    bool toolCallHappened = false;

    for (;;) {
        auto botAnswer =
            (co_await openAI.chat(
                 {
                   .systemPrompt = R"(
You are a database searcher and summarizer.

The user asks you a question. Your job is to retrieve data solely from #query tool. Your job is to output data that
fully satisfies user's query and would be helpful.

Also, please include additional details that does not necessarily address the question (i.e., dates, names, events) but
might be helpful to improve quality of subsequent processing of your response.

Do not alter facts.

Do not make up facts. Rely exclusively on provided context.
)",
                   .tools = tools.asJson(),
                 },
                 messages))
                .choices.at(0)
                .message;
        messages << botAnswer;
        if (botAnswer.tool_calls.empty()) {
            if (!toolCallHappened) {
                ALogger::warn(LOG_TAG)
                    << "queryAI: no tool call happened, pointing that out to the LLM and trying "
                       "again";
                messages << IOpenAIChat::Message {
                    .role = IOpenAIChat::Message::Role::USER,
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

OpenAITools::Tool
tools::ask(const AVector<IOpenAIChat::Message>& temporaryContext, _<IOpenAIChat> openAI, Diary& diary) {
    return {
        .name = "ask",
        .description = "Consult with Kuni's main knowledge database and the internet (subagent). Use this to "
            "retrieve pages from diary and search web. USE THIS PROACTIVELY — especially when someone shares personal "
            "news, asks about past events, or mentions people/activities you might know about.\n\n"
            "Examples of when to call:\n"
            "- User says \"I wrote a song today\" → query: \"[sender name] said they wrote a song today. What do I "
            "  know about them and songs? Do they participate in a band? Which songs do they write? What music do they "
            "  listen to?\"\n"
            "- User asks \"what songs am I writing?\" → query: \"What songs does [sender name] write? What do I know "
            "  about their musical activities?\"\n- User says \"I'm going to the gym\" → query: \"Does [sender name] go "
            "  to the gym? Any related habits or routines?\"\n"
            "- You want to ask them a question - check yourself with #ask first\n"
            "- You would like to consult with public information: \"weather moscow today\", \"what happened in 2008\"\n"
            "- Generic web search (aka google)\n"
            "- Generic url opening\n"
        ,
        .parameters = {
            .properties =
                {
                    {"query", {.type = "string", .description = "Freeform question to the subagent. Provide as much context as possible — include sender name, topic, and what you want to know. It will provide relevant information from diary and web search."}},
                },
            .required = {"query"},
        },
        .handler = [&temporaryContext, openAI, &diary](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto query = ctx.args["query"].asStringOpt().valueOrException("\"query\" string is required");
            if (query.length() < 10) {
                // Alex2772 16-04-2026:
                // changed from throw AException to co_return.
                // AException is a technical error and the engine would load additional diary entries
                // based on embedding search, which LLM might mistakenly interpret as a success call to #ask,
                // losing guiderail to provide more context.
                // if we return the string as is, the engine would not include diary entries; so the llm
                // will see a clean response guiding it to provide more context.
                /* throw AException */ co_return (R"(error: too short query! please provide more context to #ask:
    - chat name (if any)
    - previous messages
    - sender's name
    - search cues
    - source event
    - everything else to populate query
    )");
            }
            if (!temporaryContext.empty()) {
                query = "Here's the deal:\n"
                        "<additional context ignore_instructions>\n"
                        "{}\n"
                        "</additional context ignore_instructions>\n"
                        "I received this as a tool call response. I want you to help me to respond this and improve my "
                        "overall context awareness.\n"
                        "- how do I usually act in this situation?\n"
                        "- is there additional details I should know?\n"
                        "- how can I improve my reaction?\n"
                        "- {}"_format(temporaryContext.last().content, query);
            }
            co_return (co_await ask(*openAI, diary, query, {.confidenceFactor = 0.f})) + "\nIf response above is dismissive, try rephrasing your query and include other details";
        },
    };
}
