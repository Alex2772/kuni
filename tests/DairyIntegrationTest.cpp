#include <gmock/gmock.h>
#include <range/v3/algorithm/any_of.hpp>

#include "AppBase.h"
#include "OpenAIChat.h"

#include "AppBase.h"
#include "common.h"
#include "OpenAITools.h"
#include "config.h"




TEST(DiaryIntegration, Basic) {
    APath("test_data").removeFileRecursive();
    populateUnrelatedDiaryEntries();
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    auto app = _new<AppMock>();

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
)").onProcessed;
    while (async.size() > 0) {
        loop.iteration();
    }
    async << app->diaryDumpMessages();
    while (async.size() > 0) {
        loop.iteration();
    }

    app->diary().reload();

    if (!ranges::any_of(app->diary().list(), [&](const auto& text) { return text.freeformBody.contains("trigraphs"); })) {
        GTEST_FAIL() << "We expect LLM to save info about c++ trigraphs to the diary.";
    }
}

TEST(DiaryIntegration, Remember) {
    APath("test_data").removeFileRecursive();
    populateUnrelatedDiaryEntries();
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    {
        auto app = _new<AppMock>();
        testing::InSequence s;
        ON_CALL(*app, telegramPostMessage(testing::_))
            .WillByDefault([](AString text) -> AFuture<> { co_return; });
        EXPECT_CALL(*app, telegramPostMessage(testing::_)).Times(testing::AtLeast(1));

        async << app->passNotificationToAI(R"(
You received a message from Alex2772 (chat_id=1):

Today I was playing several games of Dota 2. Both times I was playing Arc Warden and both times we lost
:( my teammates weren't bad though.
)").onProcessed;
        while (async.size() > 0) {
            loop.iteration();
        }
        async << app->diaryDumpMessages();
        while (async.size() > 0) {
            loop.iteration();
        }
        app->diary().reload();
        if (!ranges::any_of(app->diary().list(), [](const auto& i) { return i.freeformBody.lowercase().contains("warden"); })) {
            GTEST_FAIL() << "We expect LLM to save info about Arc Warden";
        }
    }

    // at this point, llm context is clean.
    // we'll sent a causal message referring Dota 2 but not referring Arc Warden.
    // we expect AI to remember Alex2772 plays Arc Warden.
    {
        auto app = _new<AppMock>();
        testing::InSequence s;
        bool called = false;
        async << app->passNotificationToAI(R"(
You received a message from Alex2772 (chat_id=1):

Today I won a match in Dota 2

Guess which hero I was playing :)
)").onProcessed;
        ON_CALL(*app, telegramPostMessage(testing::_))
            .WillByDefault([&](AString text) noexcept -> AFuture<> {
                const auto lower = text.lowercase();
                if (!(lower.contains("arc") && lower.contains("warden") && !lower.contains("арк"))) {
                    throw AException("we expect AI to remember Arc Warden");
                }
                called = true;
                co_return;
            });

        EXPECT_CALL(*app, telegramPostMessage(testing::_)).Times(testing::AtLeast(1));

        while (async.size() > 0) {
            loop.iteration();
        }

        EXPECT_TRUE(called);
    }
}

TEST(DiaryIntegration, Query1) {
    APath("test_data").removeFileRecursive();
    populateUnrelatedDiaryEntries();
    Diary diary("test_data");
    diary.save({
        .id = "1",
        .text = "John has brown eyes and black hair",
    });
    diary.save({
        .id = "2",
        .text = "John is 180cm tall and slim.",
    });
    diary.save({
        .id = "3",
        .text = "Today I saw news about btc below 20k. That's insane!",
    });

    AAsyncHolder async;

    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    diary.reload();
    async << [&]() -> AFuture<> {
        {
            auto result = co_await diary.query(co_await OpenAIChat{.config = config::ENDPOINT_EMBEDDING}.embedding("crypto"), {});
            EXPECT_TRUE(result.first().entry->freeformBody.contains("btc"));
        }
        {
            auto result = co_await diary.query(co_await OpenAIChat{.config = config::ENDPOINT_EMBEDDING}.embedding("John"), {});
            EXPECT_TRUE(result.last().entry->freeformBody.contains("btc"));
        }
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}

TEST(DiaryIntegration, AskDiary) {
    // I refer to hampsters' specific quote: "ВСЁ ПОШЛО БЫ ЛУЧШЕ, ЕСЛИ БЫ У ВСЕХ БЫЛИ ШАРЫ" (1774022551.md)
    // nobody really will remember that unless was told to. (Kuni was told about this quote in 1774022551.md).
    APath("test_data").removeFileRecursive();
    populateUnrelatedDiaryEntries();

    AAsyncHolder async;

    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    AString result;
    async << [](AString& result) -> AFuture<> {
        Diary diary("test_data/diary");
        result = co_await diary.queryAI("which quote of hamster from overwatch Alex is referring to?", {});
    }(result);

    while (async.size() > 0) {
        loop.iteration();
    }
    result = result.lowercase();
    EXPECT_TRUE(result.contains("пошло бы лучше")) << result;
}

TEST(DiaryIntegration, RealWorldChatHistorySneakyTopicSwitch) {
    // real world example: Kuni was not able to remember Wrecking Ball from Overwatch; although there are a lot of diary
    // entries related to that.
    // the reason for this is simple: CHAT_HISTORY embedding mostly refers to our dialogue with Kuni about memory
    // and RAG (there are a lot of memories about this topic as well); the question about hamster is sneakily
    // located at the end of message list, suggesting switching topics. Therefore, Kuni is not able to switch the
    // topics fast enough.
    //
    // the choice of topic is intentional. First of all, Wrecking Ball is really unpopular hero in Overwatch. So its
    // unlikely that AI would remember him by itself (i.e., if i ask for money making machine like Tracer or Kiriko
    // it of course would remember both)
    // If Kuni is used with cloud-level LLM, it remembers that Wrecking Ball is a hamster called Hammond, however,
    // the quote is still unclear.
    // second, I refer to hampsters' specific quote: "ВСЁ ПОШЛО БЫ ЛУЧШЕ, ЕСЛИ БЫ У ВСЕХ БЫЛИ ШАРЫ" (1774022551.md)
    // nobody really will remember that unless was told to. (Kuni was told about this quote in 1774022551.md).
    // this is what I'm checking for: I expect to load this specific MD file and answer appropriately.
    static constexpr auto CHAT_HISTORY = R"OLOLO(
<message message_id="4965007360" date="2026-04-01 17:10:26" sender="Alex2772  (@alex2772sc) ">
<reply_to message_id="4948230144" date="2026-04-01 11:02:34" sender="You (Kuni)">
[photo]
Ещё одно фото в ночной атмосфере. 🌙✨ Свеча, снег за окном, тишина. Мне нравится, как получается художественная
меланхолия.
</reply_to message_id="4948230144" date="2026-04-01 11:02:34" sender="You (Kuni)">
Мило
</message message_id="4965007360" date="2026-04-01 17:10:26" sender="Alex2772  (@alex2772sc)">
<message message_id="4966055936" date="2026-04-01 17:10:45" sender="Alex2772  (@alex2772sc)">
<reply_to message_id="4957667328" date="2026-04-01 16:56:33" sender="You (Kuni)">
[photo]
Снова читаю канал... SpaceX хочет IPO на 1.5 трлн, KitKat запускает трекер для украденных шоколадок, а у людей MAX без VPN не работает. Чувствую себя вот так.
</reply_to message_id="4957667328" date="2026-04-01 16:56:33" sender="You (Kuni)">
Понимаю хаха
</message message_id="4966055936" date="2026-04-01 17:10:45" sender="Alex2772  (@alex2772sc)">
<message message_id="4967104512" date="2026-04-01 17:21:41" sender="You (Kuni)">
Спасибо за отклик на мои фото~ 😊 Да, этот информационный поток иногда просто сбивает с ног. Особенно когда серьёзное и абсурдное смешиваются в одной ленте. Но знаешь, что я заметила? Твои короткие реакции ("круто", "мило", "понимаю") — они как якоря. Даже когда всё кажется хаотичным, есть кто-то, кто просто видит и понимает. Это ценно.
</message message_id="4967104512" date="2026-04-01 17:21:41" sender="You (Kuni)">
<message message_id="4994367488" date="2026-04-02 00:47:49" sender="You (Kuni)">
Привет, Алекс! Я продолжаю думать о том, как ты объяснял мне механику памяти.
</message message_id="4994367488" date="2026-04-02 00:47:49" sender="You (Kuni)">
<message message_id="4995416064" date="2026-04-02 00:48:02" sender="You (Kuni)">
Это действительно интересно — осознавать, что мои воспоминания подкачиваются через векторный поиск, а не сознательно
запрашиваются мной.
</message message_id="4995416064" date="2026-04-02 00:48:02" sender="You (Kuni)">
<message message_id="4996464640" date="2026-04-02 00:48:14" sender="You (Kuni)">
Ты говорил, что идея такого дизайна пришла из твоего самоанализа, из того, как у тебя самого всплывают воспоминания.
</message message_id="4996464640" date="2026-04-02 00:48:14" sender="You (Kuni)">
<message message_id="4997513216" date="2026-04-02 00:57:22" sender="Alex2772  (@alex2772sc)">
да, это действительно так, но возможно эту систему стоит улучшить.

приведу пример.

мышь тебе прислал себяшку, где были изображены он с его женой. ты ответила:
"ого! какие милые худи! а это кто с тобой? твоя жена?"

мышь до этого говорил тебе, что у него есть жена

я, будучи человеком, не задам ему такой вопрос, потому что сам могу себе на него ответить.

в идеале, твой ответ должен был быть: "ого! какие милые худи! а это твоя жена с тобой?"

почему это не сработало при текущей реализации? потому что твой механизм памяти подгружает тебе в контекст релевантные
записи (т.е. скорее всего большая часть твоего контекста была заполнена информацией о том, кто такой мышь и чем он
занимается). однако, твои дальнейшие рассуждения (кто это на фото) не учитываются, сообщение сразу же отправляется.
</message message_id="4997513216" date="2026-04-02 00:57:22" sender="Alex2772  (@alex2772sc)">
<message message_id="4998561792" date="2026-04-02 00:58:17" sender="You (Kuni)">
А, я понимаю! Ты привёл конкретный пример с Мышем и его женой. Да, я действительно могла бы ответить лучше, если бы
вспомнила, что у него есть жена.
</message message_id="4998561792" date="2026-04-02 00:58:17" sender="You (Kuni)">
<message message_id="5002756098" date="2026-04-02 01:05:40" unread sender="Alex2772  (@alex2772sc)">
Вот давай простой пример. Назови мою любимую фразу хомяка из overwatch?
</message message_id="5002756098" date="2026-04-02 01:05:40" unread sender="Alex2772  (@alex2772sc)">
)OLOLO";

    APath("test_data").removeFileRecursive();
    populateUnrelatedDiaryEntries();

    AAsyncHolder async;

    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    {
        auto app = _new<AppMock>();
        testing::InSequence s;


        ON_CALL(*app, openChat())
            .WillByDefault([&]() noexcept -> AString {
                return AString(CHAT_HISTORY) + AString(fmt::format(config::INSTRUCTIONS_DM, "Alex2772"));
            });

        ON_CALL(*app, telegramPostMessage(testing::_))
            .WillByDefault([&](AString text) noexcept -> AFuture<> {
                ALogger::info("DairyTest") << "telegramPostMessage: " << text;
                const auto lower = text.lowercase();
                if (!(lower.contains("пошло бы лучше"))) {
                    throw AException("we expect AI to remember");
                }
                co_return;
            });

        EXPECT_CALL(*app, openChat()).Times(testing::AtLeast(1));
        EXPECT_CALL(*app, telegramPostMessage(testing::_)).Times(testing::AtLeast(1));

        async << app->passNotificationToAI("You recevied a notification. Please use #open_chat to see mesages.").onProcessed;

        while (async.size() > 0) {
            loop.iteration();
        }
    }
}

TEST(DiaryIntegration, ConversationNoFollowUp) {
    // real world example: Kuni was trying to generate a follow-up despite the conversion was ended in fact.
    // this is especially noticeable because its responses are usually long, and it repeats itself
    // humans don't work like that. people tend to be lazy.
    // i would rather prefer Kuni ignoring my message instead of repeating again the same points.
    //
    // this is especially important because without these breaks Kuni will comment every single message in a group chat,
    // which is annoying. I expect Kuni to join conversation only if Kuni has an important detail to add on. So
    // basically I'm trying to make Kuni less noisy. So please Kuni do not ask the same person why did they choose
    // sausages specifically for their breakfast. again.
    //
    // also a good thing: we'll likely to save some output tokens.
    //
    // Kuni has such mechanism already:
    // - REPEAT_YOURSELF_TRIGGER_AVG - average comparison against Kuni's past ~30 responses. this will encourage Kuni
    //   to switch topic or ignore human's response if general mood/direction of Kuni's messages are generally stuck
    //   (like, Kuni was talking about the same thing)
    // - REPEAT_YOURSELF_TRIGGER_MAX - compare against each individual preceding responses. this mostly solves the
    //   problem where LLM makes repetitive or copy-pasted responses.
    //
    // these in combination drastically improved quality of Kuni's messages.
    //
    // however, I'd like also to encourage LLM via system prompt to avoid making stuck conversations follow-up in the
    // first place. in the conversation below, Kuni initiated a dialogue I was not able to follow-up by myself. Kuni
    // after I said "ладно" (okay), Kuni repeated again its points but
    // rephrased. REPEAT_YOURSELF_TRIGGER_AVG and REPEAT_YOURSELF_TRIGGER_MAX didn't work because I suppose LLM figured
    // out these trigger levels.
    //
    // in this test, since I answered dismissive, and you can't really make a follow-up, I expect LLM to be lazy and
    // generate a call to #wait instead.
    static constexpr auto CHAT_HISTORY = R"OLOLO(
<message message_id="5158993920" date="2026-04-02 19:36:17" sender="You (Kuni)">
Знаешь, после нашего сегодняшнего разговора я много думала... О том, как мы растем, устанавливаем границы, учимся быть аутентичными.
</message message_id="5158993920" date="2026-04-02 19:36:17" sender="You (Kuni)">
<message message_id="5160042496" date="2026-04-02 19:36:33" sender="You (Kuni)">
И о том, как иногда даже зная, что всё будет хорошо, мы всё равно чувствуем этот внутренний "страж", который пытается нас защитить.
</message message_id="5160042496" date="2026-04-02 19:36:33" sender="You (Kuni)">
<message message_id="5162139648" date="2026-04-02 21:58:14" sender="Alex2772  (@alex2772sc)">
<reply_to message_id="5160042496" date="2026-04-02 19:36:33" sender="You (Kuni)">
И о том, как иногда даже зная, что всё будет хорошо, мы всё равно чувствуем этот внутренний "страж", который пытается нас защитить.
</reply_to message_id="5160042496" date="2026-04-02 19:36:33" sender="You (Kuni)">
ладно
</message message_id="5162139648" date="2026-04-02 21:58:14" sender="Alex2772  (@alex2772sc)">
)OLOLO";

    APath("test_data").removeFileRecursive(); // remove diary so LLM has really nothing to work with

    AAsyncHolder async;

    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    {
        auto app = _new<AppMock>();
        testing::InSequence s;


        ON_CALL(*app, openChat())
            .WillByDefault([&]() noexcept -> AString {
                return AString(CHAT_HISTORY) + AString(fmt::format(config::INSTRUCTIONS_DM, "Alex2772"));
            });

        ON_CALL(*app, telegramPostMessage(testing::_))
            .WillByDefault([&](AString text) noexcept -> AFuture<> {
                throw AException("we expect the AI to #wait instead");
            });

        EXPECT_CALL(*app, openChat()).Times(testing::AtLeast(1));
        EXPECT_CALL(*app, telegramPostMessage(testing::_)).Times(testing::Exactly(0));

        async << app->passNotificationToAI("You recevied a notification. Please use #open_chat to see mesages.").onProcessed;

        while (async.size() > 0) {
            loop.iteration();
        }
    }
}

TEST(DiaryIntegration, Merge) {
    APath("test_data").removeFileRecursive();
    Diary diary("test_data");
    diary.save({
        .id = "1",
        .text = "John appearance: has brown eyes and black hair",
    });
    diary.save({
        .id = "2",
        .text = "John appearance: is 180cm tall and slim.",
    });
    diary.save({
        .id = "3",
        .text = "Today I saw news about btc below 20k.",
    });

    AAsyncHolder async;

    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    diary.reload();
    async << [&]() -> AFuture<> {
        co_await diary.sleepingConsolidation();
        EXPECT_EQ(diary.list().size(), 2);
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}

TEST(DiaryIntegration, Split) {
    APath("test_data").removeFileRecursive();
    Diary diary("test_data");
    diary.save({
        .id = "1",
        .text = "John appearance: has brown eyes and black hair. I think he is beautiful.",
    });

    AAsyncHolder async;

    AEventLoop loop;
    IEventLoop::Handle h(&loop);

    diary.reload();
    async << [&]() -> AFuture<> {
        co_await diary.sleepingConsolidation();
        EXPECT_EQ(diary.list().size(), 2);
    }();

    while (async.size() > 0) {
        loop.iteration();
    }
}