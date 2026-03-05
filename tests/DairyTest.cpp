#include "OpenAIChat.h"
#include <gmock/gmock.h>

#include "AppBase.h"
#include "OpenAITools.h"
#include "config.h"


namespace {
    class AppMock: public AppBase {
    public:
        AppMock() {
            ON_CALL(*this, diaryEntryIsRelatedToCurrentContext(testing::_)).WillByDefault([this](const AString& s) -> AFuture<bool> {
                auto b = co_await AppBase::diaryEntryIsRelatedToCurrentContext(s);
                if (!b) {
                    throw AException("We expect AI to remember the diary page");
                }
                co_return b;
            });
        }

        MOCK_METHOD(void, diarySave, (const AString& message), (override));
        MOCK_METHOD(AVector<AString>, diaryRead, (), (const override));
        MOCK_METHOD(AFuture<>, telegramPostMessage, (int64_t id, AString text), (override));
        MOCK_METHOD(AFuture<bool>, diaryEntryIsRelatedToCurrentContext, (const AString& diaryEntry), (override));
    };
}

TEST(Diary, Basic) {
    auto app = _new<AppMock>();
    ON_CALL(*app, diarySave(testing::_)).WillByDefault([](const AString& message) {
        ALogger::info("AppMock") << "diarySave: " << message;
        if (!message.contains("trigraphs")) {
            throw AException("We expect LLM to save info about c++ trigraphs to the diary");
        }
    });
    EXPECT_CALL(*app, diarySave(testing::_));

    app->passNotificationToAI(R"(
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
    app->diaryDumpMessages();
    AThread::processMessages();
}

TEST(Diary, Remember) {
    AVector<AString> diary;

    {
        auto app = _new<AppMock>();
        testing::InSequence s;
        ON_CALL(*app, telegramPostMessage(testing::_, testing::_)).WillByDefault([](int64_t id, AString text) -> AFuture<> {
            co_return;
        });
        EXPECT_CALL(*app, diarySave(testing::_));
        ON_CALL(*app, diarySave(testing::_)).WillByDefault([&](const AString& message) noexcept {
            ALogger::info("AppMock") << "diarySave: " << message;
            const auto lower = message.lowercase();
            if (!(lower.contains("arc") && lower.contains("warden"))) {
                throw AException("we expect AI to remember Arc Warden");
            }
            diary << message;
        });

        app->passNotificationToAI(R"(
You received a message from Alex2772 (chat_id=1):

Today I was playing several games of Dota 2. Both times I was playing Arc Warden and both times we lost
:( my teammates weren't bad though.
)");
        AThread::processMessages();
        *app->diaryDumpMessages();
        AThread::processMessages();
    }

    // at this point, llm context is clean.
    // we'll sent a causal message referring Dota 2 but not referring Arc Warden.
    // we expect AI to remember Alex2772 plays Arc Warden.
    {
        auto app = _new<AppMock>();
        ON_CALL(*app, diaryRead()).WillByDefault([&] {
            return testing::Return(diary);
        }());
        testing::InSequence s;
        app->passNotificationToAI(R"(
You received a message from Alex2772 (chat_id=1):

Today I won a match in Dota 2

Guess which hero I was playing :)
)");
        ON_CALL(*app, telegramPostMessage(testing::_, testing::_)).WillByDefault([](int64_t id, AString text) noexcept -> AFuture<> {
            const auto lower = text.lowercase();
              if (!(lower.contains("arc") && lower.contains("warden"))) {
                  throw AException("we expect AI to remember Arc Warden");
              }
            co_return;
        });
        EXPECT_CALL(*app, telegramPostMessage(1, testing::_));
        AThread::processMessages();
    }
}

