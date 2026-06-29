//
// Created by alex2772 on 6/28/26.
//

#include "telegram_voice_messages.h"
#include <optional>

#include "audio.h"
#include "malicious_payloads.h"
#include "telegram.h"
#include "AUI/Thread/AFuture.h"
#include "AUI/Util/kAUI.h"

static AFuture<AOptional<AString>> tryTelegramPremiumTranscription(
    ITelegramClient& telegram,
    td::td_api::messageVoiceNote& voiceNote,
    int64_t chatId,
    int64_t messageId) {
    // Check if transcription is already available in the cached object
    auto& srr = voiceNote.voice_note_->speech_recognition_result_;
    if (srr && srr->get_id() == td::td_api::speechRecognitionResultText::ID) {
        auto& text = static_cast<td::td_api::speechRecognitionResultText&>(*srr);
        if (!text.text_.empty()) {
            llmui::checkForMaliciousPayloads(text.text_);
            co_return text.text_;
        }
    }


    // Check if the account has Telegram Premium before attempting transcription
    {
        auto our = co_await telegram.getUser(telegram.myId());
        if (!our->is_premium_) {
            co_return std::nullopt;
        }
    }

    // Wait for updateMessageContent with the transcription result.
    // TDLib will eventually push speechRecognitionResultText or speechRecognitionResultError.
    AFuture<AString> transcriptionFuture;
    auto eventSubscription = AObject::connect(telegram.onEvent, AObject::GENERIC_OBSERVER, [=](AArc<const td::td_api::Object> event) {
        auto update = ITelegramClient::tryCastTo<const td::td_api::updateMessageContent>(*event);
        if (!update) {
            return;
        }
        if (update->chat_id_ != chatId || update->message_id_ != messageId) {
            return;
        }
        if (!update->new_content_ || update->new_content_->get_id() != td::td_api::messageVoiceNote::ID) {
            return;
        }
        auto updateVoice = ITelegramClient::tryCastTo<const td::td_api::messageVoiceNote>(*update->new_content_);
        if (!updateVoice->voice_note_ || !updateVoice->voice_note_->speech_recognition_result_) {
            return;
        }
        if (auto srr = ITelegramClient::tryCastTo<const td::td_api::speechRecognitionResultText>(*updateVoice->voice_note_->speech_recognition_result_)) {
            transcriptionFuture.supplyValue(srr->text_);
        } else if (auto error = ITelegramClient::tryCastTo<const td::td_api::speechRecognitionResultError>(*updateVoice->voice_note_->speech_recognition_result_)) {
            transcriptionFuture.supplyValue(error->error_->message_);
        }
        // speechRecognitionResultPending — keep waiting
    });
    AUI_DEFER { eventSubscription->disconnect(); };

    // Request transcription via Telegram Premium
    try {
        co_await telegram.sendQueryWithResult(
            ITelegramClient::toPtr(td::td_api::recognizeSpeech(chatId, messageId)));
    } catch (const AException&) {
        // No premium or other error
        co_return std::nullopt;
    }

    auto text = co_await transcriptionFuture;
    llmui::checkForMaliciousPayloads(text);
    co_return text;
}

AFuture<AString> llmui::voiceMessageTranscription(
    ITelegramClient& telegram,
    td::td_api::messageVoiceNote& voiceNote,
    int64_t chatId,
    int64_t messageId) {
    auto text = co_await tryTelegramPremiumTranscription(telegram, voiceNote, chatId, messageId);
    if (text.hasValue()) {
        co_return "[voice transcription]: " + *text + "\n";
    }
    // Fall back to local Whisper transcription
    co_return co_await llmui::voiceMessage(co_await llmui::fetchMedia(telegram, voiceNote.voice_note_->voice_));
}


