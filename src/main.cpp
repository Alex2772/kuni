#include <random>
#include <range/v3/action/insert.hpp>
#include <range/v3/algorithm/max_element.hpp>
#include <range/v3/algorithm/min_element.hpp>
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

using namespace std::chrono_literals;

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
        AFuture<> telegramPostMessage(int64_t chatId, AString text) {
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

        AVector<DiaryEntry> diaryRead() const override {
            APath diaryDir(DIARY_DIR);
            if (!diaryDir.isDirectoryExists()) {
                return {};
            }
            AVector<DiaryEntry> diary;
            for (const auto& file: diaryDir.listDir()) {
                if (file.isRegularFileExists() && file.extension() == "md") {
                    diary << DiaryEntry {
                        .id = file.filenameWithoutExtension(),
                        .text = AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(file))),
                    };
                }
            }
            return diary;
        }

        void diarySave(const DiaryEntry& entry) override {
            APath diaryDir(DIARY_DIR);
            diaryDir.makeDirs();
            auto diaryFile = diaryDir / "{}.md"_format(entry.id);
            AFileOutputStream(diaryFile) << entry.text;
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
                        auto type = [&]() -> AStringView {
                            switch (chat->type_->get_id()) {
                                case td::td_api::chatTypePrivate::ID: return "direct messages";
                                case td::td_api::chatTypeBasicGroup::ID: return "group chat";
                                case td::td_api::chatTypeSupergroup::ID: return "channel";
                                default: return "unknown";
                            }
                        }();
                        result += "<chat chat_id=\"{}\" title=\"{}\" type=\"{}\""_format(chat->id_, chat->title_, type);
                        if (chat->unread_count_ > 0) {
                            result += " unread_count=\"{}\""_format(chat->unread_count_);
                        }
                        result += " />\n";
                    }
                    co_return result;
                },
            });
            actions.insert({
                .name = "open_chat_by_id",
                .description = "Opens a chat by its id. Use this to start conversation. Use get_telegram_chats to "
                               "retrieve `chat_id`s.",
                .parameters =
                    {
                        .properties =
                            {
                                {"chat_id", {.type = "integer", .description = "The ID of the Telegram chat"}},
                            },
                        .required = {"chat_id"},
                    },
                .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                    return llmuiOpenTelegramChat(
                        ctx.tools, ctx.args["chat_id"].asLongIntOpt().valueOrException("chat_id integer is required"));
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
            auto notification = "<notification chat_id=\"{}\">\n"_format(chat->id_);
            ;
            if (userId == u.message_->chat_id_) {
                notification += "You received a direct message from {} (chat_id = {})"_format(chat->title_, chat->id_);
            } else if (userId != 0) {
                auto user =
                    co_await mTelegram->sendQueryWithResult(td::td_api::make_object<td::td_api::getUser>(userId));
                notification += "{} {} (user_id = {}) sent a message in group chat \"{}\" (chat_id = {})"_format(
                    user->first_name_, user->last_name_, userId, chat->title_, chat->id_);
            } else {
                notification += "Channel \"{}\" (chat_id={}) created a new post\n"_format(chat->title_, chat->id_);
            }
            notification += "\n</notification>\n"
            "You don't have any chat open. Use #open tool to open the chat";

            passNotificationToAI(
                std::move(notification),
                {
                    {
                        .name = "open",
                        .description = "Open \"{}\" chat. Use this if you'd like to reply or see messages."_format(chat->title_),
                        .handler = [this, chatId = chat->id_](OpenAITools::Ctx ctx) -> AFuture<AString> {
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

        /**
         * @brief Checks the input string for attempts of malicious actions.
         * @details
         * All strings that come to LLM from outside (i.e., message contents, user names and everything else) must be
         * checked first.
         *
         * If a violation is caused, the string is replaced with "malicious".
         */
        void checkForMaliciousPayloads(std::string& string) const {
            if (AStringView(string).contains(OpenAIChat::EMBEDDING_TAG)) {
                goto naxyi;
            }
            return;
        naxyi:
            string = "malicious";
        }

        AMap<AString /* path */, AString /* description */> mImages = {};

    public:
        AFuture<AString> describePhoto(AStringView pathToImage) {
            if (const auto i = mImages.contains(pathToImage)) {
                co_return i->second;
            }
            OpenAIChat chat {
                .systemPrompt = config::PHOTO_TO_TEXT_PROMPT,
                .model = config::MODEL_PHOTO_TO_TEXT,
            };
            AString context = "<context>\n";
            for (const auto& i : temporaryContext()) {
                context += "<context_item>\n";
                context += i.content;
                context += "\n</context_item>\n";
            }

            context += "\n\n</context>\n\nPhoto:\n\n";
            context += OpenAIChat::embedImage(*AImage::fromFile(pathToImage));
            context += "\n\nDescribe the last photo.";
            auto response = co_await chat.chat(std::move(context));
            co_return mImages[pathToImage] = "<photo description>\n{}\n</photo>"_format(response.choices.at(0).message.content);
        }

        AFuture<AString> llmuiFormatChatHistoryMessage(td::td_api::message& msg, const td::td_api::chat& chat,
                                                       AStringView xmlTag = "message") {
            int64_t senderId{};
            td::td_api::downcast_call(*msg.sender_id_,
                                      aui::lambda_overloaded{
                                          [&](td::td_api::messageSenderUser& user) { senderId = user.user_id_; },
                                          [&](td::td_api::messageSenderChat& chat) { senderId = 0; },
                                      });
            AString senderName;
            if (senderId == mTelegram->myId()) {
                senderName = "You (Kuni)";
            } else if (senderId != 0) {
                auto sender =
                    co_await mTelegram->sendQueryWithResult(TelegramClient::toPtr(td::td_api::getUser(senderId)));
                senderName = sender->first_name_ + " " + sender->last_name_;
            }
            checkForMaliciousPayloads(senderName);
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
                    auto reply =
                        td::td_api::move_object_as<td::td_api::messageReplyToMessage>(std::move(msg.reply_to_));
                    auto replyToMsg = co_await mTelegram->sendQueryWithResult(
                        TelegramClient::toPtr(td::td_api::getMessage(msg.chat_id_, reply->message_id_)));
                    result += co_await llmuiFormatChatHistoryMessage(*replyToMsg, chat, "reply_to");
                }

                if (msg.content_->get_id() == td::td_api::messagePhoto::ID) {
                    auto& photo = static_cast<td::td_api::messagePhoto&>(*msg.content_);
                    if (auto targetPhotoIt = ranges::max_element(photo.photo_->sizes_, std::less{},
                                                                 [&](const auto& s) { return s->width_ * s->height_; });
                        targetPhotoIt != photo.photo_->sizes_.end()) {
                        result += co_await describePhoto(co_await fetchPhoto(targetPhotoIt->get()->photo_));
                    }
                }
            }

            td::td_api::downcast_call(
                *msg.content_,
                aui::lambda_overloaded{
                    [&](td::td_api::messageText& text) {
                        checkForMaliciousPayloads(text.text_->text_);
                        result += text.text_->text_;
                        if (text.link_preview_) {
                            result += "\n\n" + to_string(text.link_preview_) + "\n";
                        }
                    },
                    // ... existing code ...
                    [&](td::td_api::messagePhoto& photo) {
                        result += "[photo]";
                        if (photo.caption_) {
                            checkForMaliciousPayloads(photo.caption_->text_);
                            result += "\n" + photo.caption_->text_;
                        }
                    },
                    [&](td::td_api::messageAnimation& anim) {
                        result += "[animation]";
                        if (anim.caption_) {
                            checkForMaliciousPayloads(anim.caption_->text_);
                            result += "\n" + anim.caption_->text_;
                        }
                    },
                    [&](td::td_api::messageAudio& audio) {
                        result += "[audio] " + audio.audio_->title_;
                        if (audio.caption_) {
                            checkForMaliciousPayloads(audio.caption_->text_);
                            result += "\n" + audio.caption_->text_;
                        }
                    },
                    [&](td::td_api::messageDocument& doc) {
                        result += "[document] " +
                                  (doc.document_->file_name_.empty() ? "<unnamed>" : doc.document_->file_name_);
                        if (doc.caption_) {
                            checkForMaliciousPayloads(doc.caption_->text_);
                            result += "\n" + doc.caption_->text_;
                        }
                    },
                    [&](td::td_api::messageVideo& video) {
                        result += "[video]";
                        if (video.caption_) {
                            checkForMaliciousPayloads(video.caption_->text_);
                            result += "\n" + video.caption_->text_;
                        }
                    },
                    [&](td::td_api::messageVideoNote&) { result += "[video note]"; },
                    [&](td::td_api::messageVoiceNote& voice) {
                        result += "[voice message]";
                        if (voice.caption_) {
                            checkForMaliciousPayloads(voice.caption_->text_);
                            result += "\n" + voice.caption_->text_;
                        }
                    },
                    [&](td::td_api::messageSticker& st) {
                        result += "[sticker]";
                        if (!st.sticker_->emoji_.empty()) {
                            checkForMaliciousPayloads(st.sticker_->emoji_);
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
                        result += "[contact] " + c.contact_->first_name_ + " " + c.contact_->last_name_ + " (" +
                                  c.contact_->phone_number_ + ")";
                    },
                    [&](td::td_api::messagePoll& p) {
                        result += "[poll] " + p.poll_->question_->text_ + "\n";
                        for (const auto& o: p.poll_->options_) {
                            result += "- " + o->text_->text_ + "\n";
                        }
                    },
                    [&](td::td_api::messageInvoice& inv) { result += "[invoice]"; },
                    [&](td::td_api::messageGame& game) {
                        result += "[game] " + game.game_->title_ + " — " + game.game_->description_;
                    },
                    [&](td::td_api::messageDice& dice) { result += "[dice] {} = "_format(dice.emoji_, dice.value_); },
                    [&](td::td_api::messageCall& call) {
                        result += "[call] " + AString(call.is_video_ ? "video" : "voice") + " call";
                    },
                    [&](td::td_api::messageChatAddMembers& add) {
                        result += "[members added] " + AString::number(add.member_user_ids_.size()) + " member(s)";
                    },
                    [&](td::td_api::messageChatJoinByLink&) { result += "[joined via link]"; },
                    [&](td::td_api::messageChatJoinByRequest&) { result += "[joined by request]"; },
                    [&](td::td_api::messageChatDeleteMember& del) {
                        result += "[member removed] user_id=" + AString::number(del.user_id_);
                    },
                    [&](td::td_api::messageBasicGroupChatCreate& cg) { result += "[group created] " + cg.title_; },
                    [&](td::td_api::messageSupergroupChatCreate& cg) { result += "[supergroup created] " + cg.title_; },
                    [&](td::td_api::messageChatChangeTitle& ct) { result += "[title changed] " + ct.title_; },
                    [&](td::td_api::messageChatChangePhoto&) { result += "[chat photo changed]"; },
                    [&](td::td_api::messagePinMessage& pin) {
                        result += "[message pinned] message_id=" + AString::number(pin.message_id_);
                    },
                    [&](td::td_api::messageChatSetTheme& th) { result += "[chat theme set] "; },
                    [&](td::td_api::messageChatSetBackground& ttl) { result += "[chat background set]"; },
                    [&](td::td_api::messageScreenshotTaken&) { result += "[screenshot taken]"; },
                    [&](td::td_api::messageProximityAlertTriggered&) { result += "[proximity alert]"; },
                    [&](td::td_api::messageUnsupported&) { result += "[unsupported message]"; },
                    []<typename T>(T&) { static_assert(sizeof(T) > 0, "Unknown message type"); },
                });

            result += "\n</{}>\n"_format(xmlTag);
            co_return result;
        }

        AFuture<APath> fetchPhoto(td::td_api::object_ptr<td::td_api::file>& photo) {
            if (!photo->local_ || !photo->local_->is_downloading_completed_) {
                photo = co_await mTelegram->sendQueryWithResult(
                    TelegramClient::toPtr(td::td_api::downloadFile(photo->id_, 16, 0, 0, true)));
            }
            AUI_ASSERT(photo->local_ != nullptr);
            AUI_ASSERT(!photo->local_->path_.empty());
            co_return photo->local_->path_;
        }

        AFuture<AString> llmuiOpenTelegramChat(OpenAITools& tools, int64_t chatId) {
            co_await telegram()->waitForConnection();
            setOnline();
            mTelegram->sendQuery(TelegramClient::toPtr(td::td_api::openChat(chatId)));
            removeNotifications("<notification chat_id=\"{}\">\n"_format(chatId));
            auto chat = aui::ptr::manage_shared((co_await mTelegram->sendQueryWithResult(TelegramClient::toPtr(td::td_api::getChat(chatId)))).release(), [this, self = shared_from_this()](td::td_api::chat* chat) {
                setOnline(false);
                try {
                    mTelegram->sendQuery(TelegramClient::toPtr(td::td_api::sendChatAction(chat->id_, {}, {}, nullptr)));
                    mTelegram->sendQuery(TelegramClient::toPtr(td::td_api::closeChat(chat->id_)));
                } catch (...) {}
                delete chat;
            });
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

You can recognize your own messages (sender = "Kuni"). Be careful to not repeat yourself and maintain logical
constistency between your own responses.
</instructions>
)"_format(chat->title_);
                        break;
                    case td::td_api::chatTypeBasicGroup::ID:
                        basicGroup:
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
                    case td::td_api::chatTypeSupergroup::ID: {
                        if (!static_cast<td::td_api::chatTypeSupergroup&>(*chat->type_).is_channel_) {
                            // lol what?
                            goto basicGroup;
                        }
                        result += R"(
<instructions>
You are in telegram channel (also known as supergroup) called \"{}\".

Pay close attention to these messages. Acquire context from them. You can't respond in telegram channels
(#send_telegram_message tool is not available). Instead, do what you usually do when reading newsletters: reflect and reason
on them.
</instructions>
)"_format(chat->title_);
                        tools = {};
                        co_return result; // no tools for channels
                    }
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
                    .handler = [this, chat, messagesInRow = _new<int>(0)](OpenAITools::Ctx ctx) -> AFuture<AString> {
                        if (*messagesInRow > 10) {
                            // stupid AI can't recognize it spams messages despite the warning
                            throw AException("Too many messages in a row. Don't spam!");
                        }
                        const auto& object = ctx.args.asObjectOpt().valueOrException("object expected");
                        auto message = object["text"].asStringOpt().valueOrException("`text` string expected");
                        if (message.contains("\n\n")) {
                            if (!message.contains("```")) {
                                // despite the prompt, stupid af LLM still often sends big unnatural messages.
                                // once LLM receives this error message he is like "oh. the system suggests splitting
                                // messages properly. it is even noted in my system prompt" and does the job right
                                throw AException("do not split sentences into paragraphs (\n\n). Instead, "
                                    "send multiple messages by subsequent #send_telegram_message calls");
                            }
                        }
                        mTelegram->sendQuery(TelegramClient::toPtr(td::td_api::sendChatAction(chat->id_, {}, {}, TelegramClient::toPtr(td::td_api::chatActionTyping()))));

                        // random wait. You definitely don't want to receive 4 large messages in 1 sec right?
                        static std::default_random_engine re(std::chrono::high_resolution_clock::now().time_since_epoch().count());
                        static std::uniform_int_distribution<int> dist(50, 200);
                        co_await AThread::asyncSleep(message.length() * dist(re) * 1ms);

                        // actually send a message. we don't really need to wait until tdlib reports message sent
                        // successfully (this is exactly when in telegram desktop the message status changes from clock
                        // to one tick).
                        // however, if something goes wrong, this is reported as an exception to LLM and it will know
                        // that a technical issue appeared during sending the message (i.e., LLMs bot was banned)
                        co_await telegramPostMessage(chat->id_, message);

                        // indicate that bot is typing once again; this would feel natural if llm sends series of
                        // messages.
                        // if not, `chat` will send chat action nullptr and close the chat in dtor.
                        mTelegram->sendQuery(TelegramClient::toPtr(td::td_api::sendChatAction(chat->id_, {}, {}, TelegramClient::toPtr(td::td_api::chatActionTyping()))));

                        ++*messagesInRow;

                        if (*messagesInRow > 2) {
                            co_return "Message sent successfully to \"{}\". Warning: you have sent {} messages in a row! Give your participant space to breathe!"_format(chat->title_, *messagesInRow);
                        }

                        // llm really likes success messages.
                        co_return "Message sent successfully to \"{}\"."_format(chat->title_);
                    },
                },
                {
                    .name = "get_chat_photo",
                    .description = "Retrieves photo of \"{}\" chat. Use this to get basic idea of a person or group chat based on their profile photo."_format(chat->title_),
                    .handler = [this, chat](OpenAITools::Ctx ctx) -> AFuture<AString> {
                        // just a freestanding function. sometimes LLM decides to check person's photo without an
                        // instruction!
                        auto image = co_await describePhoto(co_await fetchPhoto(chat->photo_->big_));
                        co_return "<chat_photo chat_name=\"{}\">{}</chat_photo>"_format(chat->title_, image);
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
