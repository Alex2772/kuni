# AFuture & co_await в проекте kuni

## APromise не существует

В AUI нет `APromise`. Есть только `AFuture<T>`. Не ищи.

---

## Что такое AFuture

`AFuture<T>` — асинхронный результат, который будет готов когда-нибудь потом.  
Включается через `#include <AUI/Thread/AFuture.h>`.

Корутинная поддержка (`co_await`) подключается автоматически вместе с этим хедером
(через `AFutureCpp20Coro.h`).

---

## Базовые паттерны

### 1. Корутина, возвращающая AFuture

Самый распространённый паттерн в проекте — функция с `-> AFuture<T>` и `co_await` / `co_return` внутри:

```cpp
AFuture<AString> llmui::voiceMessageTranscription(
    ITelegramClient& telegram,
    td::td_api::messageVoiceNote& voiceNote,
    int64_t chatId,
    int64_t messageId)
{
    auto text = co_await tryTelegramPremiumTranscription(telegram, voiceNote, chatId, messageId);
    if (text.hasValue()) {
        co_return "[voice transcription]: " + *text + "\n";
    }
    // fallback
    co_return co_await llmui::voiceMessage(co_await llmui::fetchMedia(telegram, voiceNote.voice_note_->voice_));
}
```

`AFuture<>` (без параметра) — это `AFuture<void>`, используется когда результат не нужен:

```cpp
AFuture<> AppBase::diaryDumpMessages() {
    // ...
    co_return;
}
```

### 2. co_await внутри лямбды

Лямбда тоже может быть корутиной — нужно явно указать возвращаемый тип:

```cpp
mAsync << [](AppBase& self) -> AFuture<> {
    while (true) {
        co_await self.mNotificationsSignal;
        // ...
    }
}(*this);
```

```cpp
.handler = [telegram, chat](OpenAITools::Ctx ctx) -> AFuture<AString> {
    auto result = co_await telegram->sendQueryWithResult(...);
    co_return "done";
};
```

### 3. co_await co_await (цепочки)

Можно вкладывать `co_await` прямо в аргументы:

```cpp
co_return co_await llmui::voiceMessage(co_await llmui::fetchMedia(telegram, file));

result += co_await llmui::image(
    temporaryContext, openAI,
    co_await fetchMedia(telegram, sticker.sticker_->sticker_),
    xmlTag);
```

### 4. supplyValue — ручное завершение future

Когда результат приходит не через корутину, а через колбэк/событие — создаём `AFuture<T>` вручную
и вызываем `.supplyValue()` когда данные готовы:

```cpp
AFuture<AString> transcriptionFuture;

auto eventSubscription = AObject::connect(telegram.onEvent, AObject::GENERIC_OBSERVER,
    [=](AArc<const td::td_api::Object> event) {
        // ...
        if (auto srr = ITelegramClient::tryCastTo<const td::td_api::speechRecognitionResultText>(...)) {
            transcriptionFuture.supplyValue(srr->text_);
        }
    });

// потом ждём:
auto text = co_await transcriptionFuture;
```

Для `AFuture<>` (void): `transcriptionFuture.supplyValue()` без аргументов.

### 5. AFuture как сигнал/event

`AFuture<>` используется как одноразовый сигнал — аналог `std::promise<void>`:

```cpp
// В AppBase.h:
AFuture<> mNotificationsSignal;

// Ждём сигнала:
co_await self.mNotificationsSignal;

// Сбрасываем (создаём новый незавершённый future):
self.mNotificationsSignal = AFuture<>();

// Стреляем сигналом:
mNotificationsSignal.supplyValue();
```

Аналогично — `AFuture<> completed{}` в `IOpenAIChat::Response` сигнализирует о завершении стриминга:

```cpp
co_await streaming->completed;
```

### 6. onSuccess — колбэк без co_await

Когда не нужно ждать результата в корутине, а просто хочется среагировать:

```cpp
sendQueryWithResult(td::td_api::make_object<td::td_api::getOption>("version"))
    .onSuccess([](const td::td_api::object_ptr<td::td_api::OptionValue>& object) {
        // вызывается на потоке-провайдере результата
        ALogger::info(LOG_TAG) << "Tdlib version: ...";
    });
```

```cpp
result->completed.onSuccess([this, self = weak_from_this(), model = params.config.model] {
    // метрики после завершения стриминга
});
```

**Важно:** колбэк `onSuccess` выполняется на **потоке-провайдере** (том, кто вызвал `supplyValue`).  
`co_await` же возобновляет корутину на **исходном потоке** (через `AThread::current()->enqueue(...)`).

---

## AAsyncHolder — управление временем жизни

`AAsyncHolder` хранит запущенные futures и отменяет их при уничтожении.  
Аналог Kotlin `CoroutineScope`.

```cpp
// Как поле класса:
AAsyncHolder mAsync;  // в AppBase, Diary, VoiceCallManager, ...

// Запуск корутины:
mAsync << someCoroutine();
mAsync << [](AppBase& self) -> AFuture<> { ... }(*this);

// При уничтожении AAsyncHolder — все futures отменяются автоматически.
```

Оператор `<<` регистрирует future в holder'е. Завершённые futures удаляются автоматически.

---

## AThread::asyncSleep — асинхронный sleep

Не блокирует поток, возобновляет корутину через event loop:

```cpp
co_await AThread::asyncSleep(500ms);
co_await AThread::asyncSleep(1s);
co_await AThread::asyncSleep(1min / 2);
```

Используется в typing-индикаторе, retry-логике, задержках между сообщениями.

---

## await_synchronously — синхронное ожидание с event loop

Когда нужно дождаться `AFuture` в синхронном контексте (не в корутине), но при этом не заблокировать
event loop намертво:

```cpp
// src/util/await_synchronously.h
template <typename T>
static T await_synchronously(const AFuture<T>& future) {
    AEventLoop loop;
    IEventLoop::Handle h(&loop);
    AAsyncHolder async;
    async << future;
    while (!async.empty()) {
        loop.iteration();
    }
    AThread::processMessages();
    return *future;
}
```

В отличие от `AFuture<T>::get()` — крутит event loop, давая другим потокам возможность
выполнить enqueue.

---

## ITelegramClient::sendQueryWithResult — типизированный TDLib запрос

Шаблонная обёртка над `sendQuery`, которая сама кастует результат и бросает исключение при ошибке:

```cpp
template<aui::derived_from<td::td_api::Function> F>
AFuture<td::td_api::object_ptr<typename F::ReturnType::element_type>>
sendQueryWithResult(td::td_api::object_ptr<F> f) {
    auto object = co_await sendQuery(std::move(f));
    if (object->get_id() == td::td_api::error::ID) {
        throw AException(error->message_);
    }
    co_return td::move_tl_object_as<typename F::ReturnType::element_type>(std::move(object));
}
```

Использование:

```cpp
auto file = co_await telegram.sendQueryWithResult(
    ITelegramClient::toPtr(td::td_api::downloadFile(fileId, 1, 0, 0, true)));

auto contacts = co_await telegram.sendQueryWithResult(
    ITelegramClient::toPtr(td::td_api::getContacts()));
```

---

## OpenAITools::Handler — tool handler как корутина

Tool handler — это `std::function<AFuture<AString>(Ctx ctx)>`, то есть корутина:

```cpp
struct OpenAITools::Tool {
    AString name;
    AString description;
    // ...
    std::function<AFuture<AString>(Ctx ctx)> handler;
};

// Регистрация:
OpenAITools tools = {
    {
        .name = "send_telegram_message",
        .description = "...",
        .parameters = { ... },
        .handler = [telegram, chat](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto result = co_await telegram->sendQueryWithResult(...);
            co_return "Message sent";
        }
    }
};
```

`handleToolCalls` вызывает все handler'ы последовательно через `co_await`:

```cpp
auto toolCalls = co_await tools.handleToolCalls(botAnswer.tool_calls);
```

---

## ITelegramClient::Cached — кэш с populated future

Паттерн для кэширования TDLib объектов с ленивой загрузкой:

```cpp
template<typename TgObject>
struct Cached {
    TgObject tg;
    AFuture<> populated;  // завершается когда tg заполнен
};

// Использование:
co_await v->second->populated;  // ждём пока объект загрузится
```

---

## Типичные сигнатуры в проекте

```cpp
// Простые async функции
AFuture<AString> extractSenderName(ITelegramClient& telegram, td::td_api::MessageSender& sender);
AFuture<APath>   fetchMedia(ITelegramClient& telegram, td::td_api::object_ptr<td::td_api::file>& file);
AFuture<bool>    isAccessibleFromLockdown(ITelegramClient& telegram, int64_t chatId);

// Void async (только сигнализируют о завершении)
AFuture<> formatChatSingle(ITelegramClient& telegram, AString& result, td::td_api::chat& chat);
AFuture<> diaryDumpMessages();
AFuture<> sleepingConsolidation();

// Сложные типы
AFuture<td::td_api::object_ptr<td::td_api::message>> telegramPostMessage(...);
AFuture<AVector<Diary::EntryEx>> diarySaveEntries(Diary& diary, AString llmSummarization);
AFuture<IOpenAIChat::Session> handleToolCalls(const AVector<IOpenAIChat::Message::ToolCall>& toolCalls);
```
