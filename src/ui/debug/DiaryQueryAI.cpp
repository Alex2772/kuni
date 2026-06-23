//
// Created by alex2772 on 4/10/26.
//

#include <range/v3/view/transform.hpp>

#include <AUI/ASS/ASS.h>
#include <AUI/Thread/AAsyncHolder.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/View/AButton.h>
#include <AUI/View/AForEachUI.h>
#include <AUI/View/AScrollArea.h>
#include <AUI/View/ASpacerFixed.h>
#include <AUI/View/ASpinnerV2.h>
#include <AUI/View/AText.h>
#include <AUI/View/ATextField.h>
#include <AUI/View/Dynamic.h>

#include "DiaryQueryAI.h"

#include <Diary.h>
#include <Diary.h>
#include <IOpenAIChat.h>
#include <OpenAIChatImpl.h>
#include <OpenAITools.h>

using namespace declarative;
using namespace ass;

namespace {

_<AView> spoiler(_<AView> content,
                 AString title = "Spoiler",
                 bool expandedByDefault = false) {
    struct State {
        AProperty<bool> isExpanded;
    };
    auto state = _new<State>();
    state->isExpanded = expandedByDefault;
    return Vertical {
        Horizontal {
          Label {
            AUI_REACT("{}"_format(state->isExpanded ? 'v' : '>')),
          },
          AText::fromString(title),
        } AUI_LET {
            AObject::connect(it->clicked, AObject::GENERIC_OBSERVER, [state] {
                state->isExpanded = !state->isExpanded;
            });
        },
        Centered::Expanding { std::move(content) } AUI_LET {
            AObject::connect(
                AUI_REACT(state->isExpanded ? Visibility::VISIBLE : Visibility::GONE),
                AUI_SLOT(it)::setVisibility);
        },
    };
}

_<AView> messageView(const IOpenAIChat::Session& allMessages,
                     const IOpenAIChat::Message& msg) {
    const auto bg =
        msg.role == IOpenAIChat::Message::Role::USER
            ? AStylesheet::getOsThemeColor()
            : AColor::WHITE;
    const auto gradient =
        msg.role == IOpenAIChat::Message::Role::USER
            ? BackgroundGradient(bg.lighter(0.3f), bg.darker(0.2f), 147_deg)
            : BackgroundGradient(bg, bg, 0_deg);
    if (msg.role == IOpenAIChat::Message::Role::TOOL) {
        return _new<AView>() AUI_OVERRIDE_STYLE { Margin { -4_dp } };
    }
    return Vertical {
        Horizontal {
          SpacerFixed { 32_dp } AUI_OVERRIDE_STYLE {
              msg.role == IOpenAIChat::Message::Role::USER
                  ? Visibility::VISIBLE
                  : Visibility::GONE,
          },
          Vertical::Expanding {
            spoiler(AText::fromMarkdown(msg.reasoning + msg.reasoning_content),
                    "Reasoning", msg.content.empty()) AUI_OVERRIDE_STYLE {
                  Opacity { 0.5f },
                  (!msg.reasoning.empty() || !msg.reasoning_content.empty())
                      ? Visibility::VISIBLE
                      : Visibility::GONE,
            },
            AText::fromMarkdown(msg.content),
          } AUI_OVERRIDE_STYLE {
              gradient,
              TextColor(bg.readableBlackOrWhite()),
              BorderRadius { 8_px },
              Padding { 8_dp },
          },
          SpacerFixed { 32_dp } AUI_OVERRIDE_STYLE{
              msg.role != IOpenAIChat::Message::Role::USER
                  ? Visibility::VISIBLE
                  : Visibility::GONE },
        },
        AUI_DECLARATIVE_FOR(tool, msg.tool_calls, AVerticalLayout) {
            auto title =
                "Tool call: {}({})"_format(tool.function.name, tool.function.arguments);
            return Vertical {
                [&]() -> _<AView> {
                    for (const auto& m : allMessages) {
                        if (m.tool_call_id != tool.id) {
                            continue;
                        }
                        return spoiler(AText::fromString(m.content), std::move(title));
                    }
                    return Horizontal {
                        Centered { _new<ASpinnerV2>() },
                        Vertical { AText::fromString(title) } AUI_OVERRIDE_STYLE {
                            Expanding(1, 0),
                        },
                    } AUI_OVERRIDE_STYLE { LayoutSpacing { 4_dp } };
                },
            } AUI_OVERRIDE_STYLE { Padding { 8_dp } };
        },
    };
}
struct State {
    _<IOpenAIChat> openAI = _new<OpenAIChatImpl>();
    AAsyncHolder async;
    AProperty<IOpenAIChat::Session> messages;
    AProperty<AString> query = "who is alex2772?";
    AProperty<_<IOpenAIChat::StreamingResponse>> lastStreaming;
    Diary diary = Diary::Init{ .diaryDir = "data/diary", .openAI = openAI };

    AFuture<> search(Diary::QueryOpts opts = {}) {
        messages.writeScope()->clear();
        messages << IOpenAIChat::Message {
            .role = IOpenAIChat::Message::Role::USER, .content = std::move(query.raw)
        };
        query.notify();

        ALOG_DEBUG("Diary") << "queryAI query=\"" << query << "\"";
        ASet<AString> includedIds;
        OpenAITools tools {
            OpenAITools::Tool {
                .name = "query",
                .description = "Perform embedding-based search on RAG database",
                .parameters = {
                    .properties = {
                        {"text", {.type = "string", .description =
                            "Retrieval cue that is likely to appear in memory pieces. "
                            "This text will be transformed to embedding as is, and its "
                            "embedding will be compared against memory pieces. Include "
                            "as many keywords as possible; maintain meaning of the "
                            "request."
                        }},
                    },
                    .required = {"text"},
                },
                .handler = [this, opts, &includedIds](OpenAITools::Ctx ctx) -> AFuture<AString> {
                    auto cue = ctx.args["text"].asStringOpt().valueOrException("text is required string");
                    auto diaryResponse = co_await diary.query(co_await openAI->embedding({ .config = config::ENDPOINT_EMBEDDING }, cue), opts);
                    AString formattedResponse;
                    ALOG_DEBUG("Diary")
                        << "queryAI cue=\""
                        << cue << "\" found="
                        << (diaryResponse | ranges::view::transform([&](const Diary::EntryExAndRelatedness& e) -> AString {
                        if (includedIds.contains(e.entry->id)) {
                            return "";
                        }
                        return "({}.md,relatedness={})"_format(e.entry->id, e.relatedness);
                    }));
                    for (const auto& i : diaryResponse) {
                        if (includedIds.contains(i.entry->id)) {
                            continue;
                        }
                        includedIds << i.entry->id;
                        formattedResponse += R"(<memory_piece source="{}.md" relatedness="{}">
{}
</memory_piece>
)"_format(i.entry->id, i.relatedness, i.entry->freeformBody);
                    }
                    if (formattedResponse.empty()) {
                        if (!diaryResponse.empty()) {
                            co_return "All memory pieces conforming your query were "
                                "provided already. Use other query.";
                        }
                        co_return "No data was found";
                    }
                    formattedResponse += "\n# IMPORTANT\n\n"
                                         "Responses above may be incomplete. \n"
                                         "You must call #query again before answering.\n"
                                         "Call #query again to collect details, resolve contradictions, and improve "
                                         "overall quality of the response.\n"
                    ;
                    co_return formattedResponse;
                },
            },
        };
        IOpenAIChat::Params chatParams{
            .systemPrompt = R"(
You are a database searcher and summarizer.

The user asks you a question. Your job is to retrieve data solely from #query tool. Your
job is to output data that fully satisfies user's query and would be helpful.

ALWAYS strengthen your response by mentioning source file names (`*.md`)

You should call #query at least 2-3 times before making answer.

Also, please include additional details that does not necessarily address the question
(i.e., dates, names, events) but might be helpful to improve quality of subsequent
processing of your response.

Do not alter facts.

Do not make up facts. Rely exclusively on provided context.
    )",
            .tools = tools.asJson(),
        };

        bool toolCallHappened = false;

        for (;;) {
            auto streaming = openAI->chatStreaming(chatParams, messages);
            lastStreaming = streaming;
            co_await streaming->completed;
            const auto& botAnswer = streaming->response->choices.at(0).message;
            messages << botAnswer;
            lastStreaming = nullptr;
            if (botAnswer.tool_calls.empty()) {
                if (!toolCallHappened) {
                    ALogger::warn("Diary")
                        << "queryAI: no tool call happened, pointing that out "
                           "to the LLM and trying "
                           "again";
                    messages << IOpenAIChat::Message {
                        .role = IOpenAIChat::Message::Role::USER,
                        .content = "you must perform at least one call to #query",
                    };
                    continue;
                }
                co_return;
            }
            toolCallHappened = true;
            auto toolCalls = co_await tools.handleToolCalls(botAnswer.tool_calls);
            messages << toolCalls;
        }
    }
};
}   // namespace

_<AView> ui::debug::DiaryQueryAI::operator()() {
    auto state = _new<State>();
    return AScrollArea::Builder()
               .withContents(
                   Vertical::Expanding {
                     SpacerFixed { 8_dp },
                     AUI_DECLARATIVE_FOR(i, *state->messages, AVerticalLayout) {
                         return messageView(*state->messages, i);
                     } AUI_OVERRIDE_STYLE { LayoutSpacing { 8_dp } },
                     experimental::Dynamic {
                       AUI_REACT(
                           state->lastStreaming == nullptr
                               ? _<AView>(nullptr)
                               : messageView(*state->messages,
                                             state->lastStreaming->value()
                                                 .response->choices.at(0)
                                                 .message)),
                     },
                     _new<ASpinnerV2>() AUI_LET {
                         auto visible = AUI_REACT(
                             state->lastStreaming != nullptr &&
                                     state->lastStreaming->value()
                                         .response->choices.empty()
                                 ? Visibility::VISIBLE
                                 : Visibility::GONE);
                         AObject::connect(visible, AUI_SLOT(it)::setVisibility);
                     },
                     SpacerExpanding {},
                     Horizontal {
                       (_new<ATextField>() && state->query) AUI_OVERRIDE_STYLE {
                           Expanding {},
                       },
                       Button {
                         .content = Label { "Send" },
                         .onClick = [=] { state->async << state->search(); },
                       },
                     } AUI_OVERRIDE_STYLE { LayoutSpacing { 4_dp } },
                   } AUI_OVERRIDE_STYLE { LayoutSpacing { 8_dp } }
               ).withExpanding().build() AUI_LET {
                   it->setStickToEnd(true);
               };
}
