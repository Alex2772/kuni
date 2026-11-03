#pragma once

#include <chrono>

/**
 * @brief Timeouts for operations that can hang forever (a missing reply from TDLib, a stuck LLM backend, etc.).
 * @details
 * Use with AFuture::withTimeout(): `co_await somethingAsync().withTimeout(timeouts::TELEGRAM_QUERY)`.
 */
namespace timeouts {
using namespace std::chrono_literals;

/// @brief Simple TDLib queries (getUser, getChat, getChatMember, ...).
inline constexpr std::chrono::seconds TELEGRAM_QUERY = 10s;

/// @brief Downloading a file (voice, video, photo) through TDLib.
inline constexpr std::chrono::seconds TELEGRAM_DOWNLOAD = 2min;

/// @brief Waiting for Telegram Premium speech recognition result which is delivered as an update event.
inline constexpr std::chrono::seconds TELEGRAM_SPEECH_RECOGNITION = 2min;

/// @brief Requests to LLM backends that involve inference.
inline constexpr std::chrono::seconds LLM_INFERENCE = 5min;

/// @brief How many times the diary saving is asked again when the LLM returns an empty response.
inline constexpr int LLM_EMPTY_RESPONSE_ATTEMPTS = 5;
}   // namespace timeouts
