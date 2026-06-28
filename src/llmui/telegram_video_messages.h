#pragma once
#include "AUI/Common/AString.h"
#include "AUI/Thread/AFuture.h"
#include <telegram/ITelegramClient.h>
#include <td/telegram/td_api.h>

namespace llmui {

/**
 * @brief Transcribes a video note using Telegram Premium speech recognition only.
 *
 * Tries Telegram's built-in speech recognition (Premium only):
 * - if a result is already cached in the videoNote object, uses it directly;
 * - otherwise calls recognizeSpeech and waits for updateMessageContent.
 * Returns nullopt if Premium is unavailable or transcription fails.
 *
 * @returns Transcribed text wrapped in a "[video note transcription]: ..." string, or empty string.
 */
[[nodiscard]]
AFuture<AString> videoNoteTranscription(
    ITelegramClient& telegram,
    td::td_api::messageVideoNote& videoNote,
    int64_t chatId,
    int64_t messageId);

}   // namespace llmui
