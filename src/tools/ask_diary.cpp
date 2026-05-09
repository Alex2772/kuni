//
// Created by alex2772 on 5/9/26.
//

#include "ask_diary.h"

OpenAITools::Tool tools::askDiary(const AVector<IOpenAIChat::Message>& temporaryContext, Diary& diary) {
    return {
        .name = "ask_diary",
        .description = "Consult with Kuni's main knowledge database (subagent). Use this to retrieve additional "
            "pages from diary. USE THIS PROACTIVELY — especially when someone shares personal news, asks about past "
            "events, or mentions people/activities you might know about.\n\n"
            "Examples of when to call:\n"
            "- User says \"I wrote a song today\" → query: \"[sender name] said they wrote a song today. What do I "
            "  know about them and songs? Do they participate in a band? Which songs do they write? What music do they "
            "  listen to?\"\n"
            "- User asks \"what songs am I writing?\" → query: \"What songs does [sender name] write? What do I know "
            "  about their musical activities?\"\n- User says \"I'm going to the gym\" → query: \"Does [sender name] go "
            "  to the gym? Any related habits or routines?\"\n"
            "- You want to ask them a question - check yourself with #ask_diary first\n"
        ,
        .parameters = {
            .properties =
                {
                    {"query", {.type = "string", .description = "Freeform question to diary. Provide as much context as possible — include sender name, topic, and what you want to know."}},
                },
            .required = {"query"},
        },
        .handler = [&temporaryContext, &diary](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto query = ctx.args["query"].asStringOpt().valueOrException("\"query\" string is required");
            if (query.length() < 10) {
                // Alex2772 16-04-2026:
                // changed from throw AException to co_return.
                // AException is a technical error and the engine would load additional diary entries
                // based on embedding search, which LLM might mistakenly interpret as a success call to ask_diary,
                // losing guiderail to provide more context.
                // if we return the string as is, the engine would not include diary entries; so the llm
                // will see a clean response guiding it to provide more context.
                /* throw AException */ co_return (R"(error: too short query! please provide more context to ask_diary:
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
            co_return (co_await diary.queryAI(query, {.confidenceFactor = 0.f})) + "\nIf response above is dismissive, try rephrasing your query and include other details";
        },
    };
}