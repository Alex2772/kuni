#include "AUI/Common/AByteBuffer.h"
#include "AUI/IO/AFileInputStream.h"
#include "AUI/Platform/Entry.h"
#include "AUI/Util/kAUI.h"
#include "AppBase.h"
#include "telegram/TelegramClient.h"

namespace {

    constexpr auto LOG_TAG = "App";
    constexpr auto DAIRY_DIR = "dairy";

    AEventLoop gEventLoop;

    class App : public AppBase {
    public:
        App() {
            mTelegram->onEvent = [this](td::td_api::object_ptr<td::td_api::Object> event) {
                td::td_api::downcast_call(*event,
                                          [this](auto& u) { mAsync << this->handleTelegramEvent(std::move(u)); });
            };
        }

        [[nodiscard]] _<TelegramClient> telegram() const { return mTelegram; }


    protected:
        AFuture<> telegramPostMessage(int64_t chatId, AString text) override {
            co_await telegram()->sendQueryWithResult([&] {
                auto msg = td::td_api::make_object<td::td_api::sendMessage>();
                msg->chat_id_ = chatId;
                msg->input_message_content_ = [&] {
                    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
                    content->text_ = [&] {
                        auto t = td::td_api::make_object<td::td_api::formattedText>();
                        t->text_ = text;
                        return t;
                    }();
                    return content;
                }();
                return msg;
            }());
        }

        // void telegramSetupLongPoll() override {
        //     mAsync << [](App& app) -> AFuture<> {
        //         AUI_DEFER { app.telegramSetupLongPoll(); };
        //         // auto j = co_await telegram::longPoll();
        //         //
        //         // for (const auto& entry  : j.asArray()) {
        //         //     const auto& message = entry["message"];
        //         //     auto fromId = message["chat"]["id"].asLongInt();
        //         //     app.passEventToAI("You received a message from {} {} (chat_id =
        //         {}):\n\n{}"_format(message["from"]["first_name"].asStringOpt().valueOr(""),
        //         message["from"]["last_name"].asStringOpt().valueOr(""), fromId, message["text"].asString()));
        //         // }
        //     }(*this);
        // }

        AVector<AString> dairyRead() const override {
            APath dairyDir(DAIRY_DIR);
            if (!dairyDir.isDirectoryExists()) {
                return {};
            }
            AVector<AString> dairy;
            for (const auto& file: dairyDir.listDir()) {
                if (file.isRegularFileExists() && file.extension() == "md") {
                    dairy << AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(file)));
                }
            }
            return dairy;
        }

        void dairySave(const AString& message) override {
            APath dairyDir(DAIRY_DIR);
            dairyDir.makeDirs();
            auto dairyFile = dairyDir / "{}.md"_format(std::chrono::system_clock::now().time_since_epoch().count());
            AFileOutputStream(dairyFile) << message;
            ALogger::info("App") << "dairySave: " << dairyFile;
        }

    private:
        _<TelegramClient> mTelegram = _new<TelegramClient>();


        AFuture<> handleTelegramEvent(auto u) {
            TelegramClient::StubHandler{}(u);
            co_return;
        }

        AFuture<> handleTelegramEvent(td::td_api::updateNewMessage u) {
            int64_t userId = 0;
            td::td_api::downcast_call(*u.message_->sender_id_,
                                      aui::lambda_overloaded{
                                          [&](td::td_api::messageSenderUser& user) { userId = user.user_id_; },
                                          [&](auto&) {},
                                      });
            if (userId == 0) {
                co_return;
            }
            if (userId == mTelegram->myId()) {
                co_return;
            }
            auto chat = co_await mTelegram->sendQueryWithResult(
                td::td_api::make_object<td::td_api::getChat>(u.message_->chat_id_));
            if (userId == u.message_->chat_id_) {
                passEventToAI("You received a direct message from {} (chat_id = {}):\n\n{}"_format(
                    chat->title_, chat->id_, to_string(u.message_->content_)));
                co_return;
            }
            auto user = co_await mTelegram->sendQueryWithResult(td::td_api::make_object<td::td_api::getUser>(userId));
            passEventToAI("{} {} (user_id = {}) sent a message in {} (chat_id = {}):\n\n{}"_format(
                user->first_name_, user->last_name_, userId, chat->title_, chat->id_, to_string(u.message_->content_)));
            co_return;
        }
    };
} // namespace


AUI_ENTRY {
    using namespace std::chrono_literals;
    auto app = _new<App>();

    _new<AThread>([] {
        std::cin.get();
        gEventLoop.stop();
    })->start();

    AAsyncHolder async;
    async << [](_<App> app) -> AFuture<> {
        co_await app->telegram()->waitForConnection();
        app->actProactively(); // for tests
    }(app);

    IEventLoop::Handle h(&gEventLoop);
    gEventLoop.loop();

    ALogger::info(LOG_TAG) << "Bot is shutting down. Please give some time to dump remaining context";
    auto d = app->dairyDumpMessages();
    while (!d.hasResult()) {
        AThread::processMessages();
    }

    return 0;
}
