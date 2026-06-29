//
// Created by alex2772 on 5/9/26.
//

#include "send_telegram_message.h"

#include "prompts.h"
#include "AUI/IO/AFileInputStream.h"
#include "AUI/Util/kAUI.h"
#include "llmui/telegram.h"
#include "util/cosine_similarity.h"
#include "util/post_message.h"

#include <random>
#include <range/v3/algorithm/contains.hpp>
#include <range/v3/algorithm/count_if.hpp>

#include "util/json_utils.h"
#include "util/typos.h"

static constexpr auto LOG_TAG = "tools::sendTelegramMessage";

using namespace std::chrono_literals;
extern std::default_random_engine gRandomEngine;

OpenAITools::Tool tools::sendTelegramMessage(
    _<ITelegramClient> telegram,
    _<IOpenAIChat> openAI,
    _<td::td_api::chat> chat,
    _<td::td_api::array<td::td_api::object_ptr<td::td_api::message>>> messages) {

    struct State {
        int messagesInARow = 0;
        int64_t lastReplyToMessageId = 0;
    };

    return {
        .name = "send_telegram_message",
        .description = "Sends a message to the \"{}\" chat. Requirements:\n"
           "- before asking them a question, double-check yourself with #ask_query\n"
           "- you should send multiple small short messgaes. "
           "Example: (1) \"hi~\", (2) \"how are you?~\". 1-5 words."_format(chat->title_),
        .parameters =
            {
                .properties =
                    {
                        {"text", {
                            .type = "string",
                            .description = "Text of the message. May not be specified if photo_filename is set"},
                        },
                        {"photo_filename", {
                            .type = "string",
                            .description = "Attaches a photo with the given filename. Filename can be "
                            "obtained by #take_photo tool; althrough you can attach any file as soon as "
                            "their filename is correct."},
                        },
                        {"audio_filename", {
                            .type = "string",
                            .description = "Attaches an audio file with the given filename from Kuni's voice gallery."},
                        },
                        {"reply_to_message_id", {
                            .type = "integer",
                            .description = "If specified, the message will be rendered as a reply to the "
                            "message with given message id. You must use it if there are multiple messages "
                            "or to clearly address specific message."},
                        },
                    },
                .required = {},
            },
        .handler = [telegram = std::move(telegram),
                    openAI = std::move(openAI),
                    chat = std::move(chat),
                    state = _new<State>(0),
                    messages = std::move(messages)
                    ](OpenAITools::Ctx ctx) -> AFuture<AString> {
            if (state->messagesInARow > 10) {
                // stupid AI can't recognize it spams messages despite the warning
                throw AException("Too many messages in a row. Don't spam!");
            }

            auto isTyping = _new<std::atomic_bool>(true);
            auto typingCoro = [](_weak<ITelegramClient> telegram, int64_t chatId, _<std::atomic_bool> isTyping) -> AFuture<> {
                while (isTyping->load()) {
                    {
                        auto tg = telegram.lock();
                        if (tg == nullptr) {
                            // we don't want to prolong lifetime of telegram (mostly because of unit tests)
                            break;
                        }
                        tg->sendQuery(ITelegramClient::toPtr(td::td_api::sendChatAction(chatId, {}, {}, ITelegramClient::toPtr(td::td_api::chatActionTyping()))));
                    }
                    co_await AThread::asyncSleep(500ms);
                }
            }(telegram, chat->id_, isTyping);
            AUI_DEFER { isTyping->store(false); };

            if (ctx.args.contains("chat_id")) {
                if (ctx.args["chat_id"].asLongInt() != chat->id_) {
                    co_return "Error: you can't send messages to other chats. Open them first. You are currently in chat \"{}\""_format(chat->title_);
                }
            }
            auto message = ctx.args["text"].asStringOpt().valueOr("").replaceAll("\r", "");
            const auto photoFilename = ctx.args["photo_filename"].asStringOpt().valueOr("");
            const auto audioFilename = ctx.args["audio_filename"].asStringOpt().valueOr("");
            // intentionally not listed in arguments, just to disable in tests
            const auto allowTypos = ctx.args["allow_typos"].asBoolOpt().valueOr(true);
            const auto replyTo = [&]() -> int64_t {
                const auto value = util::jsonAsLongInt(ctx.args["reply_to_message_id"]).valueOr(0);
                if (state->lastReplyToMessageId == value) {
                    // we don't need to reply to the same message multiple times in a row.
                    return 0;
                }
                state->lastReplyToMessageId = value;
                return value;
            }();

            if (message.empty() && photoFilename.empty() && audioFilename.empty()) {
                co_return "Error: At least one of \"text\", \"photo_filename\" or \"audio_filename\" must be populated";
            }
            if (!photoFilename.empty() && !audioFilename.empty()) {
                co_return "Error: cannot attach both photo and audio in a single message";
            }
            if (!audioFilename.empty() && !message.empty()) {
                co_return "Error: cannot attach both voice message and text in a single message";
            }
            const bool messageContainsCode = message.contains("```");
            if (!messageContainsCode && ranges::count_if(ctx.allToolCalls, [](const IOpenAIChat::Message::ToolCall& tc) {
                return tc.function.name == "send_telegram_message";
            }) == 1) {
                if (glm::clamp((message.utf8().length() - 50.f) / 200.f, 0.f, 1.f) * 0.3f > std::uniform_real_distribution(0.f, 1.f)(gRandomEngine)) {
                    AString msg = "Error: you must split your response into small separate messages.\n"
                        "Example:\n"
                        "- ахахаххаа\n"
                        "- ты смешной\n"
                        "- научишь также?\n";
                    if (config().capabilityUseStickers) {
                        msg += "Alternatively, you can use a sticker (#sticker_send)\n";
                    }
                    co_return msg;
                }
            }

#ifndef AUI_TESTS_MODULE
            if (photoFilename.empty() && audioFilename.empty()) {
                bool shouldRemind = std::uniform_real_distribution<>(0.0, 1.0)(gRandomEngine) < config().toolReminderProbability;
                if (shouldRemind) {
                    bool usePhoto = std::uniform_real_distribution<>(0.0, 1.0)(gRandomEngine) < 0.5;
                    AString reminderMessage = "Constant texting is too dull for the user!\n";

                    if (config().capabilityUseStickers) {
                        reminderMessage += "- Consider using stickers! #sticker_send\n";
                    }

                    if (usePhoto && config().capabilityTakePhoto) {
                        reminderMessage += "- Consider sending photos from your gallery or generated by #take_photo tool to make the conversation more lively and engaging!\n";
                        throw AException(reminderMessage);
                    }
                    if (!usePhoto && config().capabilityRecordVoice) {
                        reminderMessage += "- Consider recording voice notes by #record_audio tool and sending them to make the conversation more lively and engaging!\n";
                        throw AException(reminderMessage);
                    }
                }
            }
#endif

            // handle photo_filename
            AOptional<_<AImage>> photo;
            if (!photoFilename.empty()) {
                if (photoFilename.contains("/")) {
                    throw AException("Invalid photo filename: \"{}\". Filename must not contain \"/\". ");
                }
                if (photoFilename.contains("..")) {
                    throw AException("Invalid photo filename: \"{}\". Filename must not contain \"..\". ");
                }
                photo = AImage::fromBuffer(AByteBuffer::fromStream(AFileInputStream(APath("data") / "gallery" / photoFilename)));
            }

            AOptional<APath> audioPath;
            if (!audioFilename.empty()) {
                if (audioFilename.contains("/")) {
                    throw AException("Invalid audio filename: \"{}\". Filename must not contain \"/\". ");
                }
                if (audioFilename.contains("..")) {
                    throw AException("Invalid audio filename: \"{}\". Filename must not contain \"..\". ");
                }
                APath candidatePath = APath("data") / "voice_messages" / audioFilename;
                if (!candidatePath.isRegularFileExists()) {
                    throw AException("Audio file not found: {}"_format(candidatePath.absolute()));
                }
                audioPath = candidatePath;
            }

            // Alex2772 (Apr 23 2026):
            //
            // After the introduction of reply_to_message_id, Kuni started to confuse between chats. Opening
            // a chat, it tries to reply to a message from another chat by specifying reply_to_message_id.
            if (replyTo != 0) {
                if (!ranges::contains(messages, replyTo, [](const auto& m) { return m->id_; })) {
                    // I'm not exactly sure how we should handle this.
                    // first, if LLM is confused between chats, this means a high privacy violation
                    // risk.
                    // second, ideally, I should crash the application.
                    throw AException("You are trying to send a message to another chat!");
                }
            }

            // verify that kuni does not repeat itself.
            // after introducing this quality of dialogs with LLM was significantly increased:
            // - LLM does not copypaste its prior responses
            // - LLM inclined to switch topics or respond nothing "if it has nothing to say", which is more
            //   natural.
            //
            // dirty fix: skip similarity checks if a photo was attached: llm's comment on photo is not much
            // important
            if (!message.empty() && photoFilename.empty()) {
                auto target = co_await openAI->embedding({ .config = config().embedding }, message);
                static AMap<AString, std::valarray<double>> embeddings;
                double maxSimilarity = 0.0;
                double avgSimilarity = 0.0;

                static double giveAHeadStart = 0.0;
                size_t countOfKunisMessages = 0;
                for (auto& i : *messages) {
                    if (i->sender_id_->get_id() != td::td_api::messageSenderUser::ID) {
                        continue;
                    }
                    if (static_cast<const td::td_api::messageSenderUser&>(*i->sender_id_).user_id_ != telegram->myId()) {
                        continue;
                    }
                    ++countOfKunisMessages;
                    auto text = llmui::extractMessageTypeAndText(*i);
                    auto& embedding = embeddings[text];
                    if (embedding.size() != target.size()) {
                        embedding = co_await openAI->embedding({ .config = config().embedding }, text);
                    }
                    const auto similiarity = util::cosine_similarity(target, embedding);
                    avgSimilarity += similiarity;
                    maxSimilarity = std::max(maxSimilarity, similiarity);
                    if (similiarity > config().antiRepeatTriggerMax + giveAHeadStart) {
                        giveAHeadStart += 0.07; // relax repeating after itself check
                        ALogger::warn(LOG_TAG) << "LLM is repeating itself: (maxSimilarity=" << maxSimilarity << ")" << message;
                        // If LLM generates a follow-up that repeats meaning of its previous responses,
                        // this usually means the conversation has reached to its logical end. In such case,
                        // a human will not do a follow-up whatsoever.
                        //
                        // Alex2772 (apr 19 2026):
                        // Changed "You are repeating yourself. Please rephrase" to
                        // "You are repeating yourself, which usually means you have "
                        // "nothing to put in. Suggestion: close the chat
                        //
                        // Recently Kuni has adopted this behaviour: if Kuni receives several messages
                        // about repeating itself, it makes a photo instead. No thanks photo generation
                        // is too expensive.
                        //
                        // I'm trying to make Kuni more lazy by suggesting closing a chat on a low-quality
                        // follow-up.

                        throw AException(prompts().antiRepeatPrompt);
                    }
                }
                avgSimilarity /= countOfKunisMessages;
                if (avgSimilarity > config().antiRepeatTriggerAvg + giveAHeadStart) {
                    giveAHeadStart += 0.07; // relax repeating after itself check
                    // LLM figured out threshold of REPEAT_YOURSELF_TRIGGER_MAX and indeed it generates
                    // slightly more variative responses, but their general direction and structure feels
                    // the same, stalling the dialogue.
                    //
                    // Kuni: звезды не спешат, даже если путь неясен... я здесь, чтобы просто быть твоим
                    //       ориентиром, даже если это только на мгновение... 🌟
                    // Kuni: горы стоят твердо, даже если путь неясен... я здесь, чтобы просто быть твоим
                    //       ориентиром, даже если это только на мгновение... 🏔️
                    //
                    // maxSimilarity=0.73 (threshold 0.75)
                    // avgSimilarity=0.61
                    //
                    // to force LLM from hyperfixating on one thing, let's motivate it to stay silent or
                    // switch topic

                    ALogger::warn(LOG_TAG) << "LLM is repeating itself: (avgSimilarity=" << avgSimilarity << ")" << message;
                    throw AException(prompts().antiRepeatPrompt);
                }

                giveAHeadStart = 0.0; // reset indulgence

                if (embeddings.size() >= config().antiRepeatMaxHistory) {
                    ALOG_DEBUG(LOG_TAG) << "Dropped \"anti repeat yourself\" history";
                    embeddings.clear();
                }
                ALOG_DEBUG(LOG_TAG) << "\"repeat yourself\" maxSimilarity=" << maxSimilarity << " avgSimilarity=" << avgSimilarity;
                embeddings.emplace(message, std::move(target));
            }


            auto simulateTypingDelay = [](size_t messageLength) -> AFuture<> {
                static std::chrono::high_resolution_clock::time_point prevMessageSendTime;
                const auto now = std::chrono::high_resolution_clock::now();
                AUI_DEFER { prevMessageSendTime = now; };
                if (now - prevMessageSendTime > 5s) {
                    // the prev message is old enough we can skip the wait.
                    co_return;
                }
                // random wait. You definitely don't want to receive 4 large messages in 1 sec right?
                static std::default_random_engine re(std::chrono::high_resolution_clock::now().time_since_epoch().count());
                static constexpr auto MIN_WPM = 120;
                static constexpr auto MAX_WPM = 150;
                static constexpr auto wpmToPerCharacterMillis = [](auto wordsPerMinute) {
                    const auto charactersPerMinute = wordsPerMinute * 5;
                    const auto charactersPerSecond = charactersPerMinute / 60;
                    const auto millisecondsPerCharacter = 1000 / charactersPerSecond;
                    return millisecondsPerCharacter;
                };
                static constexpr auto MIN_DELAY = wpmToPerCharacterMillis(MIN_WPM);
                static constexpr auto MAX_DELAY = wpmToPerCharacterMillis(MAX_WPM);
                static std::uniform_int_distribution<int> dist(MAX_DELAY, MIN_DELAY);
                const auto delay = (messageLength + 1) * dist(re) * 1ms;
                ALogger::info("send_telegram_message") << "Synthetic typing delay: " << delay.count() << "ms";
                co_await AThread::asyncSleep(delay);
                co_return;
            };

            auto trySimulateTypos = [&](AString str) {
                if (!allowTypos) {
                    return str;
                }
                if (std::uniform_real_distribution(0.0, 1.0)(gRandomEngine) < 0.02) {
                    str = util::swapAdjacentChars(std::move(str), gRandomEngine);
                }
                if (std::uniform_real_distribution(0.0, 1.0)(gRandomEngine) < 0.02) {
                    str = util::replaceWithKeyboardNeighbor(std::move(str), gRandomEngine);
                }
                return str;
            };
            message = trySimulateTypos(std::move(message));

            AString result;
            // actually send a message. we don't really need to wait until tdlib reports message sent
            // successfully (this is exactly when in telegram desktop the message status changes from clock
            // to one tick).
            // however, if something goes wrong, this is reported as an exception to LLM and it will know
            // that a technical issue appeared during sending the message (i.e., LLMs bot was banned)
            if (message.contains("\n") && !messageContainsCode) {
                // despite the prompt, stupid af LLM still often sends big unnatural messages.
                // we will split manually.

                for (auto line : message.split("\n")) {
                    co_await simulateTypingDelay(line.length());
                    // std::exchange: we want all attachments go to the first message.
                    auto sent = co_await util::telegramPostMessage(*telegram,
                                                       chat->id_,
                                                       std::move(line),
                                                       std::exchange(photo, {}),
                                                       std::exchange(audioPath, {}),
                                                       replyTo);
                    result += "Message sent successfully to \"{}\"; message_id={}, text=\"{}\".\n"_format(chat->title_, sent->id_, llmui::extractMessageTypeAndText(*sent));
                }
            } else {
                co_await simulateTypingDelay(message.length());
                auto sent = co_await util::telegramPostMessage(
                    *telegram, chat->id_, message, std::move(photo), std::move(audioPath), replyTo);
                result += "Message sent successfully to \"{}\"; message_id={}, text=\"{}\"."_format(chat->title_, sent->id_, llmui::extractMessageTypeAndText(*sent));
            }


            ALOG_DEBUG(LOG_TAG) << "Sent message: " << message;

            ++state->messagesInARow;

            if (state->messagesInARow > 5) {
                result += "\n\nWarning: you have sent {} messages in a row! Give your participant space to breathe!"_format(state->messagesInARow);
            } else if (state->messagesInARow < 3) {
                // in addition to prompt, we'll encourage llm to add a follow-up messages to make dialogs more
                // natural:
                // - (1) hi~
                // - (2) how are you?
                // it is still up to LLM to decide whether or not to add follow-ups.
                result += "\n\nYou should add a follow-up #send_telegram_message.";
            }

            // llm really likes success messages.
            co_return result;
        },
    };
}
