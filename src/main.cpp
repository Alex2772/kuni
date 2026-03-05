#include <range/v3/action/insert.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/reverse.hpp>
#include <range/v3/view/transform.hpp>
#include <simdutf/encoding_types.h>


#include "AUI/Common/AByteBuffer.h"
#include "AUI/IO/AFileInputStream.h"
#include "AUI/Platform/Entry.h"
#include "AUI/Util/ASharedRaiiHelper.h"
#include "AUI/Util/kAUI.h"
#include "AppBase.h"
#include "telegram/TelegramClient.h"

namespace {

    constexpr auto LOG_TAG = "App";
    constexpr auto DIARY_DIR = "diary";

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

        AVector<AString> diaryRead() const override {
            APath diaryDir(DIARY_DIR);
            if (!diaryDir.isDirectoryExists()) {
                return {};
            }
            AVector<AString> diary;
            for (const auto& file: diaryDir.listDir()) {
                if (file.isRegularFileExists() && file.extension() == "md") {
                    diary << AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(file)));
                }
            }
            return diary;
        }

        void diarySave(const AString& message) override {
            APath diaryDir(DIARY_DIR);
            diaryDir.makeDirs();
            auto diaryFile = diaryDir / "{}.md"_format(std::chrono::system_clock::now().time_since_epoch().count());
            AFileOutputStream(diaryFile) << message;
            ALogger::info("App") << "diarySave: " << diaryFile;
        }

        void updateTools(OpenAITools& actions) override {
            actions.insert({
                .name = "get_telegram_chats",
                .description = "Returns a list of Telegram chats. Use this to seek chat_ids, looking for existing "
                               "chats and unread chats, or to start a new conversation.",
                .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                    co_await telegram()->waitForConnection();
                    auto chats = co_await telegram()->sendQueryWithResult(TelegramClient::toPtr(
                        td::td_api::getChats(TelegramClient::toPtr(td::td_api::chatListMain()), 100)));
                    AString result =
                        "You are currently looking at Telegram's main screen. Use see the following chats:\n";
                    for (const auto& chatId: chats->chat_ids_) {
                        auto chat = co_await telegram()->sendQueryWithResult(
                            TelegramClient::toPtr(td::td_api::getChat(chatId)));
                        result += "<chat chat_id=\"{}\" title=\"{}\" type=\"{}\""_format(chat->id_, chat->title_,
                                                                                         to_string(chat->type_));
                        if (chat->unread_count_ > 0) {
                            result += " unread_count=\"{}\""_format(chat->unread_count_);
                        }
                        result += " />\n";
                    }
                    ctx.tools = {}; // reset
                    co_return result;
                },
            });
            actions.insert({
                .name = "open_chat_by_id",
                .description = "Opens a chat by its id. Use this to start conversation. Use get_telegram_chats to retrieve `chat_id`s.",
                .parameters = {
                    .properties = {
                        {"chat_id", { .type = "integer", .description = "The ID of the Telegram chat" }},
                    },
                    .required = {"chat_id"},
                },
                .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                    return llmuiOpenTelegramChat(ctx.tools, ctx.args["chat_id"].asLongIntOpt().valueOrException("chat_id integer is required"));
                },
            });
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
            if (userId == mTelegram->myId()) {
                co_return;
            }
            auto chat = co_await mTelegram->sendQueryWithResult(
                td::td_api::make_object<td::td_api::getChat>(u.message_->chat_id_));
            auto notification = "<notification chat_id=\"{}\">\n"_format(chat->id_);;
            if (userId == u.message_->chat_id_) {
                notification += "You received a direct message from {} (chat_id = {}):\n\n{}"_format(
                    chat->title_, chat->id_, to_string(u.message_->content_));
            } else if (userId != 0) {
                auto user =
                    co_await mTelegram->sendQueryWithResult(td::td_api::make_object<td::td_api::getUser>(userId));
                notification += "{} {} (user_id = {}) sent a message in group chat \"{}\" (chat_id = {}):\n\n{}"_format(
                    user->first_name_, user->last_name_, userId, chat->title_, chat->id_,
                    to_string(u.message_->content_));
            } else {
                notification += "Channel \"{}\" (chat_id={}) created a new post\n{}"_format(
                    chat->title_, chat->id_,
                    to_string(u.message_->content_));
            }
            notification += "\n</notification>\n";

            passNotificationToAI(
                std::move(notification),
                {
                    {
                        .name = "open",
                        .description = "Opens notification. Use this if you'd like to reply.",
                        .handler = [this, chatId = chat->id_](
                                       OpenAITools::Ctx ctx) -> AFuture<AString> {
                            return llmuiOpenTelegramChat(ctx.tools, chatId);
                        },
                    },

                });

            co_return;
        }

        void setOnline(bool online = true) {
            mTelegram->sendQuery(TelegramClient::toPtr(
                td::td_api::setOption("online", TelegramClient::toPtr(td::td_api::optionValueBoolean(online)))));
        }
    public:

        AFuture<AString> llmuiFormatChatHistoryMessage(td::td_api::message& msg, const td::td_api::chat& chat, AStringView xmlTag = "message") {
            int64_t senderId{};
            td::td_api::downcast_call(
                *msg.sender_id_, aui::lambda_overloaded{
                                      [&](td::td_api::messageSenderUser& user) { senderId = user.user_id_; },
                                      [&](td::td_api::messageSenderChat& chat) { senderId = 0; },
                                  });
            AString senderName;
            if (senderId == mTelegram->myId()) {
                senderName = "You (Kuni)";
            } else if (senderId != 0) {
                auto sender = co_await mTelegram->sendQueryWithResult(
                    TelegramClient::toPtr(td::td_api::getUser(senderId)));
                senderName = sender->first_name_ + " " + sender->last_name_;
            }
            AString result = "<{} message_id=\"{}\""_format(xmlTag, msg.id_);
            if (!senderName.empty()) {
                result += " sender=\"{}\""_format(senderName);
            }
            if (chat.last_read_outbox_message_id_ < msg.id_) {
                result += " unread";
            }
            result += ">\n";
            if (xmlTag != "reply_to") {
                if (msg.reply_to_ && msg.reply_to_->get_id() == td::td_api::messageReplyToMessage::ID) {
                    auto reply = td::td_api::move_object_as<td::td_api::messageReplyToMessage>(std::move(msg.reply_to_));
                    auto replyToMsg = co_await mTelegram->sendQueryWithResult(TelegramClient::toPtr(td::td_api::getMessage(msg.chat_id_, reply->message_id_)));
                    result += co_await llmuiFormatChatHistoryMessage(*replyToMsg, chat, "reply_to");
                }
            }

            td::td_api::downcast_call(*msg.content_, aui::lambda_overloaded{
                [&](td::td_api::messageText& text) {
                    result += text.text_->text_;
                    if (text.link_preview_) {
                        result += "\n\n" + to_string(text.link_preview_) + "\n";
                    }
                },
                // ... existing code ...
                [&](td::td_api::messagePhoto& photo) {
                    result += "[photo]";
                    if (photo.caption_) {
                        result += "\n" + photo.caption_->text_;
                    }
                },
                [&](td::td_api::messageAnimation& anim) {
                    result += "[animation]";
                    if (anim.caption_) {
                        result += "\n" + anim.caption_->text_;
                    }
                },
                [&](td::td_api::messageAudio& audio) {
                    result += "[audio] " + audio.audio_->title_;
                    if (audio.caption_) {
                        result += "\n" + audio.caption_->text_;
                    }
                },
                [&](td::td_api::messageDocument& doc) {
                    result += "[document] " + (doc.document_->file_name_.empty() ? "<unnamed>" : doc.document_->file_name_);
                    if (doc.caption_) {
                        result += "\n" + doc.caption_->text_;
                    }
                },
                [&](td::td_api::messageVideo& video) {
                    result += "[video]";
                    if (video.caption_) {
                        result += "\n" + video.caption_->text_;
                    }
                },
                [&](td::td_api::messageVideoNote&) {
                    result += "[video note]";
                },
                [&](td::td_api::messageVoiceNote& voice) {
                    result += "[voice message]";
                    if (voice.caption_) {
                        result += "\n" + voice.caption_->text_;
                    }
                },
                [&](td::td_api::messageSticker& st) {
                    result += "[sticker]";
                    if (!st.sticker_->emoji_.empty()) {
                        result += " " + st.sticker_->emoji_;
                    }
                },
                [&](td::td_api::messageLocation& loc) {
                    result += "[location] lat=" + AString::number(loc.location_->latitude_) +
                              " lon=" + AString::number(loc.location_->longitude_);
                },
                [&](td::td_api::messageVenue& ven) {
                    result += "[venue] " + ven.venue_->title_ + " — " + ven.venue_->address_;
                },
                [&](td::td_api::messageContact& c) {
                    result += "[contact] " + c.contact_->first_name_ + " " + c.contact_->last_name_ +
                              " (" + c.contact_->phone_number_ + ")";
                },
                [&](td::td_api::messagePoll& p) {
                    result += "[poll] " + p.poll_->question_->text_ + "\n";
                    for (const auto& o : p.poll_->options_) {
                        result += "- " + o->text_->text_ + "\n";
                    }
                },
                [&](td::td_api::messageInvoice& inv) {
                    result += "[invoice]";
                },
                [&](td::td_api::messageGame& game) {
                    result += "[game] " + game.game_->title_ + " — " + game.game_->description_;
                },
                [&](td::td_api::messageDice& dice) {
                    result += "[dice] {} = "_format(dice.emoji_, dice.value_);
                },
                [&](td::td_api::messageCall& call) {
                    result += "[call] " + AString(call.is_video_ ? "video" : "voice") + " call";
                },
                [&](td::td_api::messageChatAddMembers& add) {
                    result += "[members added] " + AString::number(add.member_user_ids_.size()) + " member(s)";
                },
                [&](td::td_api::messageChatJoinByLink&) {
                    result += "[joined via link]";
                },
                [&](td::td_api::messageChatJoinByRequest&) {
                    result += "[joined by request]";
                },
                [&](td::td_api::messageChatDeleteMember& del) {
                    result += "[member removed] user_id=" + AString::number(del.user_id_);
                },
                [&](td::td_api::messageBasicGroupChatCreate& cg) {
                    result += "[group created] " + cg.title_;
                },
                [&](td::td_api::messageSupergroupChatCreate& cg) {
                    result += "[supergroup created] " + cg.title_;
                },
                [&](td::td_api::messageChatChangeTitle& ct) {
                    result += "[title changed] " + ct.title_;
                },
                [&](td::td_api::messageChatChangePhoto&) {
                    result += "[chat photo changed]";
                },
                [&](td::td_api::messagePinMessage& pin) {
                    result += "[message pinned] message_id=" + AString::number(pin.message_id_);
                },
                [&](td::td_api::messageChatSetTheme& th) {
                    result += "[chat theme set] ";
                },
                [&](td::td_api::messageChatSetBackground& ttl) {
                    result += "[chat background set]";
                },
                [&](td::td_api::messageScreenshotTaken&) {
                    result += "[screenshot taken]";
                },
                [&](td::td_api::messageProximityAlertTriggered&) {
                    result += "[proximity alert]";
                },
                [&](td::td_api::messageUnsupported&) {
                    result += "[unsupported message]";
                },
                []<typename T>(T&) {
                    static_assert(sizeof(T) > 0, "Unknown message type");
                },
            });

            result += "\n</{}>\n"_format(xmlTag);
            co_return result;
        }

        AFuture<AString> llmuiOpenTelegramChat(OpenAITools& tools, int64_t chatId) {
            co_await telegram()->waitForConnection();
            setOnline();
            mTelegram->sendQuery(TelegramClient::toPtr(td::td_api::openChat(chatId)));
            auto chatCloseMarker = ASharedRaiiHelper::make([this, self = shared_from_this(), chatId] {
                mTelegram->sendQuery(TelegramClient::toPtr(td::td_api::closeChat(chatId)));
                setOnline(false);
            });
            removeNotifications("<notification chat_id=\"{}\">\n"_format(chatId));
            auto chat = co_await mTelegram->sendQueryWithResult(TelegramClient::toPtr(td::td_api::getChat(chatId)));
            AString result = "You opened the chat \"{}\" in Telegram. You see last messages:\n"_format(chat->title_);

            td::td_api::array<td::td_api::object_ptr<td::td_api::message>> messages;
            {
                const size_t targetMessageCount = chat->unread_count_ + 10;
                int64_t fromMessage = 0;
                while (messages.size() < targetMessageCount) {
                    auto response =
                        co_await mTelegram->sendQueryWithResult(TelegramClient::toPtr(td::td_api::getChatHistory(
                            chatId, fromMessage, 0, glm::min(targetMessageCount - messages.size(), size_t(100)),
                            false)));
                    if (response->messages_.empty()) {
                        result += "No messages found.";
                        goto naxyi;
                    }
                    fromMessage = response->messages_.back()->id_ + 1;
                    messages.insert(messages.end(), std::make_move_iterator(response->messages_.begin()),
                                    std::make_move_iterator(response->messages_.end()));
                }
            }
            {
                td::td_api::array<td::td_api::int53> readMessages;
                for (auto& msg: messages | ranges::view::reverse) {
                    readMessages.push_back(msg->id_);
                    result += co_await llmuiFormatChatHistoryMessage(*msg, *chat);
                }

                mTelegram->sendQuery(
                    TelegramClient::toPtr(td::td_api::viewMessages(chatId, std::move(readMessages), nullptr, false)));

                switch (chat->type_->get_id()) {
                    case td::td_api::chatTypeSecret::ID:
                    case td::td_api::chatTypePrivate::ID:
                        result += R"(
<instructions>
You are in private chat with {} (also known as direct messages or DM).

Pay close attention to these messages, contents and sender. Acquire context from them and respond accordingly. Or, if
instructed to "act proactively", you can share your recent thoughts and emotions instead.

Real people, whom you are interacting via Telegram with, will not see your "plain text" responses. They'll see
#send_telegram_message only. Be careful distinguishing between chat with "user", and real people from Telegram.

You do not need to greet each time you receive a new message.

DM is a private tet-a-tet dialogue with you and one specific person. Both participants are likely to respond to each
other, through you still are not obligated to reply to each message.
</instructions>
)"_format(chat->title_);
                        break;
                    case td::td_api::chatTypeBasicGroup::ID:
                        result += R"(
<instructions>
You are in group chat called \"{}\".

Pay close attention to these messages, contents and sender. Acquire context from them and respond accordingly. Or, if
instructed to "act proactively", you can share your recent thoughts and emotions instead.

Real people, whom you are interacting via Telegram with, will not see your "plain text" responses. They'll see
#send_telegram_message only. Be careful distinguishing between chat with "user", and real people from Telegram.

You do not need to greet each time you receive a new message.
</instructions>
)"_format(chat->title_);
                        break;
                    case td::td_api::chatTypeSupergroup::ID:
                        result += R"(
<instructions>
You are in telegram channel (also known as supergroup) called \"{}\".

Pay close attention to these messages. Acquire context from them. You can't respond in telegram channels
(#send_chat_message tool is not available). Instead, do what you usually do when reading newsletters: reflect and reason
on them.
</instructions>
)"_format(chat->title_);
                        tools = {};
                        co_return result; // no tools for channels
                }

            }

        naxyi:
            tools = OpenAITools{
                {
                    .name = "send_telegram_message",
                    .description = "Sends a message to the \"{}\" chat"_format(chat->title_),
                    .parameters =
                        {
                            .properties =
                                {
                                    {"text", {.type = "string", .description = "Contents of the message"}},
                                },
                            .required = {"text"},
                        },
                    .handler = [this, chatId, chatCloseMarker, chatName = chat->title_](OpenAITools::Ctx ctx) -> AFuture<AString> {
                        const auto& object = ctx.args.asObjectOpt().valueOrException("object expected");
                        auto message = object["text"].asStringOpt().valueOrException("`text` string expected");
                        co_await telegramPostMessage(chatId, message);
                        co_return "Message sent successfully."_format(chatName);
                    },
                },
            };

            co_return result;
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
    auto d = app->diaryDumpMessages();
    while (!d.hasResult()) {
        AThread::processMessages();
    }

    return 0;
}
