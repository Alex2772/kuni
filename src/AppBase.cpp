//
// Created by alex2772 on 2/27/26.
//

#include "AppBase.h"

#include "IOpenAIChat.h"

#include <random>
#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/take.hpp>
#include <range/v3/view/take_last.hpp>
#include <range/v3/view/iota.hpp>

#include "AUI/Logging/ALogger.h"
#include "AUI/Util/kAUI.h"
#include "config.h"
#include "MetricsBreadcumbs.h"
#include "WebSearch.h"
#include "AUI/IO/AFileInputStream.h"
#include "tools/ask.h"
#include "util/diary_save_entries.h"
#include "util/important_things_to_remember.h"

#include <range/v3/view/transform.hpp>

using namespace std::chrono_literals;

static constexpr auto LOG_TAG = "App";
static const auto WORKING_MEMORY_PATH = "working_memory.md";

extern std::default_random_engine gRandomEngine;


AppBase::AppBase(Init init): mInit(std::move(init)), mDiary({
    .diaryDir = mInit.workingDir / "diary",
    .openAI = mInit.openAI,
}), mWakeupTimer(_new<ATimer>(27min)) {
    // mWakeupTimer fires on the timer thread; without this, its signal would be invoked directly on the
    // timer thread instead of being safely queued to AppBase's own thread, racing with mNotificationsSignal/
    // mNotifications access in the main coroutine below and causing a null AFuture dereference crash.
    setSlotsCallsOnlyOnMyThread(true);

    // mTools.addTool({
    //     .name = "send_telegram_message",
    //     .description = "Sends a message to a Telegram user.",
    //     .parameters = {
    //         .properties = {
    //             {"chat_id", { .type = "integer", .description = "The ID of the Telegram chat" }},
    //             {"message", { .type = "string", .description = "Contents of the message" }},
    //         },
    //         .required = {"chat_id", "message"},
    //     },
    // }, [this](const AJson& args) -> AFuture<AString> {
    //     const auto& object = args.asObjectOpt().valueOrException("object expected");
    //     auto chatId = object["chat_id"].asLongIntOpt().valueOrException("`chat_id` integer expected");
    //     auto message = object["message"].asStringOpt().valueOrException("`message` string expected");
    //     co_await telegramPostMessage(chatId, message);
    //     co_return "Message sent successfully.";
    // });

    connect(mWakeupTimer->fired, [this] {
        if (std::uniform_real_distribution<double>(0.0, 1.0)(gRandomEngine) < 0.5) {
            return;
        }
        actProactively();
    });
    mWakeupTimer->start();

    auto fetchConfig = [this] {
        mWorkerCount = config().workerCount;
    };
    fetchConfig();
    connect(gConfigUpdated, fetchConfig);

    connect(mWorkerCount, [this] {
        if (mWorkerCount > mWorkers.size()) {
            for (size_t i = mWorkerCount - mWorkers.size(); i > 0; --i) {
                mWorkers << _new<Worker>(mWorkers.size(), *this);
            }
            return;
        }
        if (mWorkerCount < mWorkers.size()) {
            mWorkers.erase(mWorkers.end() - mWorkerCount, mWorkers.end());
        }
    });
}

AFuture<> AppBase::diaryDumpMessages(IOpenAIChat::Session& temporaryContext) {
    std::unique_lock lock(mWorkingMemoryLock, std::defer_lock);
    while (!lock.try_lock()) {
        co_await AThread::asyncSleep(1s);
    }
    MetricsBreadcumbs::Point metric(metricBreadcumbs(), "function", "diaryDumpMessages");
    ALOG_TRACE(LOG_TAG) << "diaryDumpMessages";
    // mDiary.reload(); // will find plagiarism against all entries. // commented out: exclude plagiarism checks for
    // included entries
    AUI_DEFER { mDiary.reload(); };
    if (temporaryContext.empty()) {
        co_return;
    }
    AString previousWorkingMemory;
    if ((mInit.workingDir / WORKING_MEMORY_PATH).isRegularFileExists()) {
        AByteBuffer buf;
        buf << AFileInputStream(mInit.workingDir / WORKING_MEMORY_PATH);
        previousWorkingMemory = AStringView(buf.data(), buf.size());
    }
    auto importantThingsToRemember = util::importantThingsToRemember(*this, *openAI(), temporaryContext, previousWorkingMemory);

    co_await util::diarySaveEntries(mDiary, temporaryContext, {
        .systemPrompt = getSystemPrompt(),
        // no tools should be involved.
    });
    {
        // do it in separate lines: first, we wait for LLM response, second, we overwrite file (destructive operation).
        auto workingMemoryMd = co_await importantThingsToRemember;
        AFileOutputStream(mInit.workingDir / WORKING_MEMORY_PATH) << workingMemoryMd;
    }
    temporaryContext.clear();
    mSystemPromptSuffix.clear();
}

void AppBase::actProactively() {
    ALOG_TRACE(LOG_TAG) << "actProactively";
    AString prompt = "<your_diary_page just_for_reasoning no_plagiarism no_copy>\n";
    if (!mDiary.list().empty()) {
        auto idx = gRandomEngine() % mDiary.list().size();
        auto entry = mDiary.list().begin();
        while (idx--) {
            entry++;
        }
        prompt += entry->freeformBody;
        mDiary.unload(entry);
    }
    prompt += R"(
</your_diary_page>

It's time to reflect on your thoughts!
  - maybe make some reasoning?\n"
  - maybe do some reflection?\n"
  - maybe write to a person and initiate a dialogue? whom you would like to write? maybe call #get_telegram_chats? You
    can open one chat at a time - choose wisely!\n"
Act proactively!
)";
    const auto& notification = notificationManager().passNotificationToAI({
        .message = std::move(prompt),
        .pin = "<act_proactively />",
    });
    struct State {
        AOptional<MetricsBreadcumbs::Point> metric;
    };
    auto state = _new<State>();
    notification.onStartedProcessing.onSuccess([this, state] {
        mActingProactively = true;
        state->metric.emplace(metricBreadcumbs(), "function", "actProactively");
    });
    notification.onProcessed.onSuccess([this, state] {
        mActingProactively = false;
        state->metric = std::nullopt;
    });
}

AString AppBase::onCleanContext() const {
    if ((mInit.workingDir / WORKING_MEMORY_PATH).isRegularFileExists()) {
        AByteBuffer workingMemory;
        workingMemory << AFileInputStream(mInit.workingDir / WORKING_MEMORY_PATH);
        return R"(<things_to_remember>
{}
</things_to_remember>
<instructions>
Your behaviour must be highly influenced by "physical state" and "emotional state" mentioned above.

<example>
Emotional state: anger
...
send_telegram_message("text":"иди нахуй заебал")
</example>
<example>
Emotional state: amused
...
send_telegram_message("text":"мррр~")
</example>
</instruction>
)"_format(AStringView(workingMemory.data(), workingMemory.size()));
    }
    return "";
}


void AppBase::reportPhaseTiming(AString phase, std::chrono::milliseconds duration) {
    emit phaseTimingFired(AppBase::PhaseTimingEvent{
        .phase = std::move(phase),
        .breadcrumbLabels = metricBreadcumbs()->value(),
        .duration = duration,
    });
}

void AppBase::updateTools(OpenAITools& actions, const IOpenAIChat::Session& temporaryContext) {
    ALOG_TRACE(LOG_TAG) << "updateTools";
    if (!actions.handlers().contains("ask")) {
        actions.insert(toolAsk(temporaryContext));
    }
    actions.onAfterToolCall << [this](const AString& toolName, std::chrono::milliseconds duration) {
        if (toolName == "wait") {
            return;
        }
        if (toolName == "pause") {
            return;
        }
        reportPhaseTiming(toolName, duration);
        auto labels = metricBreadcumbs()->value();
        emit toolCallFired(AppBase::ToolCallEvent{
            .toolName = toolName,
            .breadcrumbLabels = std::move(labels),
            .lastOpenedChatLastMessageTime = mLastOpenedChatLastMessageTime.map([](std::chrono::system_clock::time_point t) {
                return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - t);
            }),
            .toolCallDuration = duration,
        });
    };

}

void AppBase::wakeUpIfSleeping() {
    for (const auto& worker : mWorkers) {
        worker->wakeUpIfSleeping();
    }
}

OpenAITools::Tool AppBase::toolAsk(const IOpenAIChat::Session& temporaryContext) {
    return tools::ask([&temporaryContext] {
            AString out;
            for (const auto& msg : temporaryContext | ranges::view::take_last(2)) {
                out += msg.content;
                out += "\n";
            }
            return out;
        }, openAI(), mDiary);
}

AString AppBase::getSystemPrompt()
{
    if (mSystemPromptSuffix.empty()) {
        // this thing emulates "middle" memory of human - tasks, promises and other stuff
        // in timespan 1-3d.
        // reset in diaryDumpMessages.
        mSystemPromptSuffix = onCleanContext();
    }
    auto prompt = "{}\n\n{}\n\n<your_appearance>\n{}\n</your_appearance>\n{}"_format(prompts().system, prompts().characterBase, prompts().characterAppearance, mSystemPromptSuffix);
    if (config().lockdown != Config::LockdownMode::NONE) {
        prompt += "\nYou are currently under lockdown mode. This means you can access to subset of chats only.";
    }
    return prompt;
}

AFuture<> AppBase::diaryDumpMessages() {
    for (const auto& worker : mWorkers) {
        co_await worker->diaryDumpMessages();
    }
}
