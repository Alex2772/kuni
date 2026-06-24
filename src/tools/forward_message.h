#pragma once
#include "OpenAITools.h"
#include "telegram/ITelegramClient.h"

namespace tools {
/**
 * @brief Tool that forwards a message from the currently opened chat to another chat.
 *
 * Useful when reading Telegram channels — Kuni can forward interesting posts to other chats
 * (e.g., to a friend, a group, or to herself as a saved message).
 *
 * @param telegram  The Telegram client.
 * @param fromChat  The chat currently opened (source of the message).
 */
OpenAITools::Tool forwardMessage(_<ITelegramClient> telegram, _<td::td_api::chat> fromChat);
}
