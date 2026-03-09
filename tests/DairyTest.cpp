#include <gmock/gmock.h>
#include <range/v3/algorithm/any_of.hpp>

#include "AppBase.h"
#include "OpenAIChat.h"

#include "AppBase.h"
#include "OpenAITools.h"
#include "config.h"


namespace {
    class AppMock : public AppBase {
    public:
        MOCK_METHOD(void, diarySave, (const AppBase::DiaryEntry& message), (override));
        MOCK_METHOD(AVector<AppBase::DiaryEntry>, diaryRead, (), (const override));
        MOCK_METHOD(void, telegramPostMessage, (const AString& message), ());
        MOCK_METHOD(void, onDiaryEntryIsRelated, ((const std::valarray<float>& context), (const DiaryEntryEx& entry), (aui::float_within_0_1 relatence)), ());

    protected:
        AFuture<aui::float_within_0_1> diaryEntryIsRelated(const std::valarray<float>& context,
                                                           DiaryEntryEx& entry) override {
            auto response = co_await AppBase::diaryEntryIsRelated(context, entry);
            onDiaryEntryIsRelated(context, entry, response);
            co_return response;
        }

        void updateTools(OpenAITools& actions) override {
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
                    co_return "Message sent successfully.";
                },
            });
        }
    };
} // namespace

TEST(Diary, Basic) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    AVector<AString> diary;
    auto app = _new<AppMock>();
    ON_CALL(*app, diarySave(testing::_)).WillByDefault([&](const AppBase::DiaryEntry& message) {
        ALogger::info("AppMock") << "diarySave: " << message.text;
        diary << message.text;
    });
    EXPECT_CALL(*app, diarySave(testing::_)).Times(testing::AtLeast(1));

    async << app->passNotificationToAI(R"(
Today you read an article. Contents below.

The source character set of C source programs is contained within the 7-bit ASCII character set but is a superset of the
ISO 646-1983 Invariant Code Set. Trigraph sequences allow C programs to be written using only the ISO (International
Standards Organization) Invariant Code Set. Trigraphs are sequences of three characters (introduced by two consecutive
question marks) that the compiler replaces with their corresponding punctuation characters. You can use trigraphs in C
source files with a character set that does not contain convenient graphic representations for some punctuation
characters.

C++17 removes trigraphs from the language. Implementations may continue to support trigraphs as part of the
implementation-defined mapping from the physical source file to the basic source character set, though the standard
encourages implementations not to do so. Through C++14, trigraphs are supported as in C.

Visual C++ continues to support trigraph substitution, but it's disabled by default. For information on how to enable
trigraph substitution, see /Zc:trigraphs (Trigraphs Substitution).
)");
    while (async.size() > 0) {
        loop.iteration();
    }
    async << app->diaryDumpMessages();
    while (async.size() > 0) {
        loop.iteration();
    }

    if (!ranges::any_of(diary, [&](const auto& text) { return text.contains("trigraphs"); })) {
        GTEST_FAIL() << "We expect LLM to save info about c++ trigraphs to the diary. Diary: " << diary;
    }
}

TEST(Diary, Remember) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    AVector<AppBase::DiaryEntry> diary;

    {
        auto app = _new<AppMock>();
        testing::InSequence s;
        ON_CALL(*app, telegramPostMessage(testing::_))
            .WillByDefault([](AString text) -> AFuture<> { co_return; });
        EXPECT_CALL(*app, diarySave(testing::_)).Times(testing::AtLeast(1));
        ON_CALL(*app, diarySave(testing::_)).WillByDefault([&](const AppBase::DiaryEntry& message) noexcept {
            ALogger::info("AppMock") << "diarySave: " << message.text;
            diary << message;
        });

        async << app->passNotificationToAI(R"(
You received a message from Alex2772 (chat_id=1):

Today I was playing several games of Dota 2. Both times I was playing Arc Warden and both times we lost
:( my teammates weren't bad though.
)");
        while (async.size() > 0) {
            loop.iteration();
        }
        async << app->diaryDumpMessages();
        while (async.size() > 0) {
            loop.iteration();
        }
        if (!ranges::any_of(diary, [](const auto& i) { return i.text.lowercase().contains("warden"); })) {
            GTEST_FAIL() << "We expect LLM to save info about Arc Warden";
        }
    }

    // at this point, llm context is clean.
    // we'll sent a causal message referring Dota 2 but not referring Arc Warden.
    // we expect AI to remember Alex2772 plays Arc Warden.
    {
        auto app = _new<AppMock>();
        ON_CALL(*app, diaryRead()).WillByDefault([&] { return testing::Return(diary); }());
        testing::InSequence s;
        async << app->passNotificationToAI(R"(
You received a message from Alex2772 (chat_id=1):

Today I won a match in Dota 2

Guess which hero I was playing :)
)");
        ON_CALL(*app, telegramPostMessage(testing::_))
            .WillByDefault([](AString text) noexcept -> AFuture<> {
                const auto lower = text.lowercase();
                if (!(lower.contains("arc") && lower.contains("warden"))) {
                    throw AException("we expect AI to remember Arc Warden");
                }
                co_return;
            });

        ON_CALL(*app, onDiaryEntryIsRelated(testing::_, testing::_, testing::_)).WillByDefault([](const std::valarray<float>& context, const AppMock::DiaryEntryEx& entry, aui::float_within_0_1 relatence) {
            ALogger::info("AppMock") << "onDiaryEntryIsRelated: ";

        });
        EXPECT_CALL(*app, onDiaryEntryIsRelated(testing::_, testing::_, testing::_)).Times(diary.size());
        EXPECT_CALL(*app, telegramPostMessage(testing::_));

        while (async.size() > 0) {
            loop.iteration();
        }
    }
}
