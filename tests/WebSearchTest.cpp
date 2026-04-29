#include <gtest/gtest.h>
#include "WebSearch.h"

#include "common.h"
#include "config.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"

#include <gmock/gmock-spec-builders.h>

TEST(WebSearch, Basic) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    AVector<web::Result> results;
    async << [&]() -> AFuture<> {
        results = co_await web::search("aui framework c++", 3);
    }();

    while (async.size() > 0) {
        loop.iteration();
    }

    EXPECT_FALSE(results.empty());
    EXPECT_TRUE(results.at(0).title.contains("aui-framework")) << results.at(0).title;
}

TEST(WebSearch, SearchAI) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    AString results;
    async << [&]() -> AFuture<> {
        results = co_await web::searchAI("what is c++ aui framework?");
    }();

    while (async.size() > 0) {
        loop.iteration();
    }

    EXPECT_FALSE(results.empty());
    EXPECT_TRUE(results.lowercase().contains("alex2772") || results.lowercase().contains("aui::core")) << results;
}

TEST(WebSearch, SearchAppAI) {

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
Вот давай простой пример. погугли что такое aui c++ framework?
</message message_id="5002756098" date="2026-04-02 01:05:40" unread sender="Alex2772  (@alex2772sc)">
)OLOLO";

    auto app = _new<AppMock>();

    ON_CALL(*app, openChat())
        .WillByDefault([&]() noexcept -> AString {
            return AString(CHAT_HISTORY) + AString(fmt::format(config::INSTRUCTIONS_DM, "Alex2772"));
        });

    ON_CALL(*app, telegramPostMessage(testing::_))
        .WillByDefault([&](AString text) noexcept -> AFuture<> {
            ALogger::info("WebSearchTest") << "telegramPostMessage: " << text;
            const auto lower = text.lowercase();
            if (!(lower.contains("aui::core"))) {
                throw AException("we expect AI to research for AYE");
            }
            co_return;
        });

    EXPECT_CALL(*app, openChat()).Times(testing::AtLeast(1));
    EXPECT_CALL(*app, telegramPostMessage(testing::_)).Times(testing::AtLeast(1));

    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;

    async << app->passNotificationToAI("You received a notification. Use #openChat").onProcessed;

    while (async.size() > 0) {
        loop.iteration();
    }
}
