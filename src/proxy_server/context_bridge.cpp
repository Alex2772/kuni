#include "context_bridge.h"

#include "OpenAIChatImpl.h"
#include "json_fingerprint.h"
#include "util/diary_save_entries.h"

#include <range/v3/algorithm/remove_if.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/transform.hpp>

using namespace std::chrono_literals;

static constexpr auto LOG_TAG = "ContextBridge";

static AString requestSalt(const AJson& request) {
    return proxy_server::fingerprint(request["messages"]);
}

proxy_server::ContextBridge::ContextBridge(Config config)
  : mUpdateTimer(_new<ATimer>(1min)), mConfig(std::move(config)) {
    setSlotsCallsOnlyOnMyThread(true);
    connect(mUpdateTimer->fired, [this] {
        mAsyncHolder << update();
    });
    mUpdateTimer->start();
}

AFuture<> proxy_server::ContextBridge::update() {
    co_await collectAndSaveSessionsNotNewerThan(std::chrono::system_clock::now() - mConfig.timeout);
}

AFuture<AString> proxy_server::ContextBridge::processChatHistoryMessage(
    const td::td_api::chat& chat, const td::td_api::message& msg, AString formatted) {
    try {
        if (chat.id_ != config::PAPIK_CHAT_ID) {
            // early return - this feature is locked down to the papik only.
            co_return formatted;
        }

        AString workItemsStr;
        for (auto workItem : co_await collectAndSaveSessionsNotNewerThan(std::chrono::system_clock::from_time_t(msg.date_))) {
            workItemsStr += "<work_item>\n{}\n</work_item instructions=\"This describes a project or a task you were working together with {}\">\n"_format(workItem, chat.title_);
        }

        formatted.insert(0, workItemsStr);
    } catch (const AException& e) {
        ALogger::err(LOG_TAG) << "processChatHistoryMessage: " << e;
    }
    co_return formatted;
}

AFuture<AVector<AString>> proxy_server::ContextBridge::collectAndSaveSessionsNotNewerThan(std::chrono::system_clock::time_point tp) {
    AVector<AJson> sessions;
    mPendingSessions.erase(
        ranges::remove_if(
            mPendingSessions,
            [&](PendingSession& s) {
                if (s.updateTime <= tp) {
                    sessions << std::move(s.request);
                    return true;
                }
                return false;
            }),
        mPendingSessions.end());

    AVector<AString> result;
    for (auto& session : sessions) {
        session["stream"] = false;
        session["messages"].asArray() << aui::to_json(IOpenAIChat::Message {
            .role = IOpenAIChat::Message::Role::USER,
            .content = config::DIARY_PROMPT,
        });
        session.asObject().removeIf([](const auto& pair) {
            return pair.first == "tools";
        });
        naxyi:
        auto sessionId = session["session_id"].asStringOpt().valueOr("context_save");
        session["session_id"] = sessionId;
        auto response = aui::from_json<IOpenAIChat::Response>(co_await OpenAIChatImpl::makeHttpRequest(mConfig.endpoint, AJson::toString(session), sessionId));
        auto savedEntries = co_await util::diarySaveEntries(*mConfig.diary, response.choices.at(0).message.content);
        if (savedEntries.empty()) {
            ALogger::warn(LOG_TAG) << "LLM returned empty response";
            session["messages"].asArray() << aui::to_json(response.choices.at(0).message);
            session["messages"].asArray() << aui::to_json(IOpenAIChat::Message {
                .role = IOpenAIChat::Message::Role::USER,
                .content = "You should provide a response with long entries, no tool calls.",
            });
            goto naxyi;
        }
        auto savedEntriesStrings = savedEntries
            | ranges::views::transform([](const Diary::EntryEx& entry) {
                return entry.freeformBody;
              })
            | ranges::to<AStringVector>();
        result << savedEntriesStrings.join("\n---\n");
    }

    co_return result;
}

void proxy_server::ContextBridge::collectRequestToLLM(AJson request) {
    PendingSession incomingSession {
        .salt = requestSalt(request),
        .request = std::move(request),
        .updateTime = std::chrono::system_clock::now(),
    };

    for (auto& pendingSession : mPendingSessions) {
        if (incomingSession.salt.startsWith(pendingSession.salt)) {
            // an existing session was updated.
            pendingSession = std::move(incomingSession);
            return;
        }
    }

    // a new session.
    mPendingSessions << std::move(incomingSession);
}
