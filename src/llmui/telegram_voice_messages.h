#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Thread/AFuture.h"
#include <telegram/ITelegramClient.h>
#include <td/telegram/td_api.h>

struct IOpenAIChat;

namespace llmui {

/**
 * @brief Transcribes a voice note, preferring Telegram Premium speech recognition.
 *
 * First tries Telegram's built-in speech recognition (free for Premium users):
 * - if a result is already cached in the voiceNote object, uses it directly;
 * - otherwise calls recognizeSpeech and waits for updateMessageContent.
 * Falls back to local Whisper transcription if Premium is unavailable,
 * the request errors out, or the result times out.
 *
 * @returns Transcribed text wrapped in a "[voice transcription]: ..." string.
 */
[[nodiscard]]
AFuture<AString> voiceMessageTranscription(
    ITelegramClient& telegram,
    td::td_api::messageVoiceNote& voiceNote,
    int64_t chatId,
    int64_t messageId,
    IOpenAIChat& openAI);

}   // namespace llmui
