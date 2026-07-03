#pragma once
#include "IOpenAIChat.h"
#include "AUI/Common/AString.h"
#include "AUI/Thread/AFuture.h"
#include <telegram/ITelegramClient.h>
#include <td/telegram/td_api.h>

namespace llmui {

/**
 * @brief Transcribes a video note.
 * in video note, order is reverse compared to a voice note:
 *
 * 1. we attempt llmui::video transcription first
 * 2. we attempt telegram's transcription if (1) fails
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
    IOpenAIChat& openAI,
    td::td_api::messageVideoNote& videoNote,
    int64_t chatId,
    int64_t messageId);

}   // namespace llmui
