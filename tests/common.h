#pragma once
#include "AUI/IO/APath.h"
#include "AppBase.h"
#include "OpenAIChatImpl.h"

#include <gmock/gmock.h>

static const auto TEST_DATA = APath(__FILE__).parent() / "data";


class AppMock : public AppBase {
public:
    AppMock(): AppBase({
        .openAI = _new<OpenAIChatImpl>(),
    }) {
    }

    MOCK_METHOD(void, telegramPostMessage, (const AString& message), ());
    MOCK_METHOD(AString, openChat, (), ());

protected:
    void updateTools(OpenAITools& actions) override {
        AppBase::updateTools(actions);
        actions.insert({
            .name = "send_telegram_message",
            .description = "Sends a message to the chat",
            .parameters =
                {
                    .properties =
                        {
                            {"text", {.type = "string", .description = "Contents of the message"}},
                        },
                    .required = {"text"},
                },
            .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                const auto& object = ctx.args.asObjectOpt().valueOrException("object expected");
                auto message = object["text"].asStringOpt().valueOrException("`text` string expected");
                telegramPostMessage(message);
                co_return "Message sent successfully. Warning: you have sent a message. Consider not spamming by using `wait` call.";
            },
        });
        actions.insert({
            .name = "open_chat",
            .description = "Get chat messages",
            .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                co_return openChat();
            },
        });
    }
};


static void populateUnrelatedDiaryEntries() {
    // copy md files from tests/data/random_diary_entries.
    // these diary entries are actual output of real Kuni instance, roughly from March 2026, slightly distilled
    // to avoid duplication and compromising some personal information.
    // these entries are needed specifically to add real world data to these unit tests and make the task
    // of quering a bit more challenging.

    auto diaryDir = APath("test_data") / "diary";
    diaryDir.makeDirs();

    for (const auto& f: (TEST_DATA / "random_diary_entries").listDir()) {
        APath::copy(f, diaryDir / f.filename());
    }
}
