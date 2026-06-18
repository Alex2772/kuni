//
// Created by alex2772 on 5/9/26.
//

#include "post_message.h"

#include "config.h"
#include "is_accessible_from_lockdown.h"
#include "AUI/Image/jpg/JpgImageLoader.h"

static constexpr auto LOG_TAG = "util::post_message";

AFuture<td::td_api::object_ptr<td::td_api::message>> util::telegramPostMessage(
    ITelegramClient& telegram, int64_t chatId, AString text, AOptional<_<AImage>> photo, AOptional<APath> audioPath, int64_t replyTo) {
    ALOG_TRACE(LOG_TAG)
        << "telegramPostMessage: chat_id" << chatId << " text=" << text << " photo=" << photo
        << " audioPath=" << audioPath << " replyTo=" << replyTo;
    // Check lockdown mode - only allow PAPIK_CHAT_ID if lockdown is enabled
    if (! co_await util::isAccessibleFromLockdown(telegram, chatId)) {
        throw AException("Lockdown mode is enabled. You can only send messages to chat with ID {} (PAPIK_CHAT_ID)."_format(
            config::PAPIK_CHAT_ID));
    }

    co_return co_await telegram.sendQueryWithResult([&] {
        auto msg = td::td_api::make_object<td::td_api::sendMessage>();
        msg->chat_id_ = chatId;
        msg->input_message_content_ = [&]() -> td::td_api::object_ptr<td::td_api::InputMessageContent> {
            if (photo) {
                auto content = td::td_api::make_object<td::td_api::inputMessagePhoto>();
                content->caption_ = [&] {
                    auto t = td::td_api::make_object<td::td_api::formattedText>();
                    t->text_ = text;
                    return t;
                }();
                content->width_ = photo->get()->width();
                content->height_ = photo->get()->height();
                auto tempPath = "temp_{}.jpg"_format(std::chrono::system_clock::now().time_since_epoch().count());
                JpgImageLoader::save(AFileOutputStream(tempPath), **photo);
                content->photo_ = ITelegramClient::toPtr(td::td_api::inputFileLocal(tempPath));
                return content;
            }

            if (audioPath) {
                auto content = td::td_api::make_object<td::td_api::inputMessageVoiceNote>();
                content->voice_note_ = ITelegramClient::toPtr(td::td_api::inputFileLocal(audioPath->absolute().toStdString()));
                // content->album_cover_thumbnail_ = nullptr;
                content->duration_ = 0;
                // content->title_ = audioPath->filename();
                // content->performer_ = "";
                if (!text.empty()) {
                    content->caption_ = [&] {
                        auto t = td::td_api::make_object<td::td_api::formattedText>();
                        t->text_ = text;
                        return t;
                    }();
                }
                return content;
            }

            auto content = td::td_api::make_object<td::td_api::inputMessageText>();
            content->text_ = [&] {
                auto t = td::td_api::make_object<td::td_api::formattedText>();
                t->text_ = text;
                return t;
            }();
            return content;
        }();
        if (replyTo != 0) {
            msg->reply_to_ = ITelegramClient::toPtr(td::td_api::inputMessageReplyToMessage(replyTo, nullptr, 0));
        }
        return msg;
    }());
}