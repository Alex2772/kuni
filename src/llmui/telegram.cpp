//
// Created by alex2772 on 5/9/26.
//

#include "telegram.h"

#include "audio.h"
#include "config.h"
#include "image.h"
#include "malicious_payloads.h"
#include "AUI/Util/kAUI.h"
#include "tools/stickers.h"
#include "util/is_accessible_from_lockdown.h"
#include "util/time_ago.h"

#include <range/v3/algorithm/max_element.hpp>

static AFuture<AString> extractSenderName(ITelegramClient& telegram, int64_t senderId) {
    AString senderName;
    if (senderId == telegram.myId()) {
        senderName = "You (Kuni)";
    } else if (senderId != 0) {
        try {
            auto sender = co_await telegram.getUser(senderId);
            senderName = sender->td::td_api::user::first_name_ + " " + sender->td::td_api::user::last_name_;
            if (sender->td::td_api::user::usernames_) {
                if (!sender->td::td_api::user::usernames_->active_usernames_.empty()) {
                    senderName += " (@" + sender->td::td_api::user::usernames_->active_usernames_.at(0) + ")";
                }
            }
        } catch (const AException&) {
        }
        if (senderName.empty()) {
            try {
                auto sender = co_await telegram.getChat(senderId);
                senderName = sender->td::td_api::chat::title_;
            } catch (const AException&) {
            }
        }
    }

    llmui::checkForMaliciousPayloads(senderName);
    co_return senderName;
}

AFuture<AString> llmui::extractSenderName(ITelegramClient& telegram, td::td_api::MessageSender& sender) {
    int64_t senderId {};
    td::td_api::downcast_call(
        sender,
        aui::lambda_overloaded {
          [&](const td::td_api::messageSenderUser& user) { senderId = user.user_id_; },
          [&](const td::td_api::messageSenderChat& chat) { senderId = chat.chat_id_; },
        });
    return extractSenderName(telegram, senderId);
}

AFuture<APath> llmui::fetchMedia(ITelegramClient& telegram, td::td_api::object_ptr<td::td_api::file>& file) {
    if (!file->local_ || !file->local_->is_downloading_completed_) {
        file = co_await telegram.sendQueryWithResult(
            ITelegramClient::toPtr(td::td_api::downloadFile(file->id_, 16, 0, 0, true)));
    }
    AUI_ASSERT(file->local_ != nullptr);
    AUI_ASSERT(!file->local_->path_.empty());
    co_return file->local_->path_;
}

[[nodiscard]]
AFuture<>
llmui::formatChatSingle(ITelegramClient& telegram, AString& result, td::td_api::chat& chat) {
    if (! co_await util::isAccessibleFromLockdown(telegram, chat.id_)) {
        co_return;
    }

    auto type = [&]() -> AStringView {
        switch (chat.type_->get_id()) {
            case td::td_api::chatTypePrivate::ID:
                return "direct messages";
            case td::td_api::chatTypeBasicGroup::ID:
                return "group chat";
            case td::td_api::chatTypeSupergroup::ID:
                return "channel";
            default:
                return "unknown";
        }
    }();
    AString preview;
    if (chat.last_message_) {
        preview = co_await extractSenderName(telegram, *chat.last_message_->sender_id_);
        preview += ": ";
        preview += extractMessageTypeAndText(*chat.last_message_);
        preview.replaceAll("\n", " ");

        if (preview.utf8().length() > 80) {
            preview = preview.utf8().substr(0, 30).str + "..." + preview.utf8().substr(preview.utf8().length() - 30).str;
        }
    }
    AString lastMessageAgo;
    if (chat.last_message_) {
        auto tp = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>(
            std::chrono::seconds(chat.last_message_->date_));
        lastMessageAgo = util::timeAgo(tp);
    }

    result += "<chat chat_id=\"{}\" title=\"{}\" preview=\"{}\" type=\"{}\" last_message=\"{}\""_format(
        chat.id_, chat.title_, preview, type, lastMessageAgo);
    if (chat.unread_count_ > 0) {
        result += " unread_count=\"{}\""_format(chat.unread_count_);
    }
    result += " />\n";
}

AFuture<>
llmui::formatChatList(ITelegramClient& telegram, AString& result, std::span<_<td::td_api::chat>> chats) {
    for (auto& chat : chats) {
        co_await formatChatSingle(telegram, result, *chat);
    }
}

AString llmui::extractMessageTypeAndText(td::td_api::message& msg) {
    AString out;
    td::td_api::downcast_call(
        *msg.content_,
        aui::lambda_overloaded {
          [&](td::td_api::messageText& text) {
              checkForMaliciousPayloads(text.text_->text_);
              out += text.text_->text_;
              if (text.link_preview_) {
                  out += "\n" + formatLinkPreview(*text.link_preview_);
              }
          },
          [&](td::td_api::messagePhoto& photo) {
              out += "[photo]";
              if (photo.caption_) {
                  checkForMaliciousPayloads(photo.caption_->text_);
                  out += "\n" + photo.caption_->text_;
              }
          },
          [&](td::td_api::messageAnimation& anim) {
              out += "[animation]";
              if (anim.caption_) {
                  checkForMaliciousPayloads(anim.caption_->text_);
                  out += "\n" + anim.caption_->text_;
              }
          },
          [&](td::td_api::messageAudio& audio) {
              out += "[audio] " + audio.audio_->title_;
              if (audio.caption_) {
                  checkForMaliciousPayloads(audio.caption_->text_);
                  out += "\n" + audio.caption_->text_;
              }
          },
          [&](td::td_api::messageDocument& doc) {
              out += "[document] " + (doc.document_->file_name_.empty() ? "<unnamed>" : doc.document_->file_name_);
              if (doc.caption_) {
                  checkForMaliciousPayloads(doc.caption_->text_);
                  out += "\n" + doc.caption_->text_;
              }
          },
          [&](td::td_api::messageVideo& video) {
              out += "[video]";
              if (video.caption_) {
                  checkForMaliciousPayloads(video.caption_->text_);
                  out += "\n" + video.caption_->text_;
              }
          },
          [&](td::td_api::messageVideoNote&) { out += "[video note]"; },
          [&](td::td_api::messageVoiceNote& voice) {
              //   out += "[voice message]";
              if (voice.caption_) {
                  checkForMaliciousPayloads(voice.caption_->text_);
                  out += "\n" + voice.caption_->text_;
              }
          },
          [&](td::td_api::messageSticker& st) { out += "[sticker]"; },
          [&](td::td_api::messageLocation& loc) {
              out += "[location] lat=" + AString::number(loc.location_->latitude_) +
                     " lon=" + AString::number(loc.location_->longitude_);
          },
          [&](td::td_api::messageVenue& ven) { out += "[venue] " + ven.venue_->title_ + " — " + ven.venue_->address_; },
          [&](td::td_api::messageContact& c) {
              out += "[contact] " + c.contact_->first_name_ + " " + c.contact_->last_name_ + " (" +
                     c.contact_->phone_number_ + ")";
          },
          [&](td::td_api::messagePoll& p) {
              out += "[poll] " + p.poll_->question_->text_ + "\n";
              for (const auto& o : p.poll_->options_) {
                  out += "- " + o->text_->text_ + "\n";
              }
          },
          [&](td::td_api::messageInvoice& inv) { out += "[invoice]"; },
          [&](td::td_api::messageGame& game) {
              out += "[game] " + game.game_->title_ + " — " + game.game_->description_;
          },
          [&](td::td_api::messageDice& dice) { out += "[dice] {} = "_format(dice.emoji_, dice.value_); },
          [&](td::td_api::messageCall& call) {
              out += "<call is_video=\"{}\" duration=\"{}\" />"_format(call.is_video_, call.duration_);
          },
          [&](td::td_api::messageChatAddMembers& add) {
              out += "[members added] " + AString::number(add.member_user_ids_.size()) + " member(s)";
          },
          [&](td::td_api::messageChatJoinByLink&) { out += "[joined via link]"; },
          [&](td::td_api::messageChatJoinByRequest&) { out += "[joined by request]"; },
          [&](td::td_api::messageChatDeleteMember& del) {
              out += "[member removed] user_id=" + AString::number(del.user_id_);
          },
          [&](td::td_api::messageBasicGroupChatCreate& cg) { out += "[group created] " + cg.title_; },
          [&](td::td_api::messageSupergroupChatCreate& cg) { out += "[supergroup created] " + cg.title_; },
          [&](td::td_api::messageChatChangeTitle& ct) { out += "[title changed] " + ct.title_; },
          [&](td::td_api::messageChatChangePhoto&) { out += "[chat photo changed]"; },
          [&](td::td_api::messagePinMessage& pin) {
              out += "[message pinned] message_id=" + AString::number(pin.message_id_);
          },
          [&](td::td_api::messageChatSetTheme& th) { out += "[chat theme set] "; },
          [&](td::td_api::messageChatSetBackground& ttl) { out += "[chat background set]"; },
          [&](td::td_api::messageScreenshotTaken&) { out += "[screenshot taken]"; },
          [&](td::td_api::messageProximityAlertTriggered&) { out += "[proximity alert]"; },
          [&](td::td_api::messageUnsupported&) { out += "[unsupported message]"; },
          // Media messages
          [&](td::td_api::messagePaidMedia&) { out += "[paid media]"; },
          // Expired media
          [&](td::td_api::messageExpiredPhoto&) { out += "[expired photo]"; },
          [&](td::td_api::messageExpiredVideo&) { out += "[expired video]"; },
          [&](td::td_api::messageExpiredVideoNote&) { out += "[expired video note]"; },
          [&](td::td_api::messageExpiredVoiceNote&) { out += "[expired voice note]"; },
          // Emoji messages
          [&](td::td_api::messageAnimatedEmoji&) { out += "[animated emoji]"; },
          // Game/stakes messages
          [&](td::td_api::messageStakeDice&) { out += "[stake dice]"; },
          [&](td::td_api::messageStory&) { out += "[story]"; },
          // Checklist
          [&](td::td_api::messageChecklist&) { out += "[checklist]"; },
          // Group calls
          [&](td::td_api::messageGroupCall&) { out += "[group call]"; },
          [&](td::td_api::messageVideoChatScheduled&) { out += "[video chat scheduled]"; },
          [&](td::td_api::messageVideoChatStarted&) { out += "[video chat started]"; },
          [&](td::td_api::messageVideoChatEnded&) { out += "[video chat ended]"; },
          [&](td::td_api::messageInviteVideoChatParticipants&) { out += "[invited to video chat]"; },
          // Chat management
          [&](td::td_api::messageChatDeletePhoto&) { out += "[chat photo deleted]"; },
          [&](td::td_api::messageChatOwnerLeft&) { out += "[chat owner left]"; },
          [&](td::td_api::messageChatOwnerChanged&) { out += "[chat owner changed]"; },
          [&](td::td_api::messageChatHasProtectedContentToggled&) { out += "[protected content toggled]"; },
          [&](td::td_api::messageChatHasProtectedContentDisableRequested&) { out += "[protected content disable requested]"; },
          [&](td::td_api::messageChatUpgradeTo&) { out += "[upgraded to supergroup]"; },
          [&](td::td_api::messageChatUpgradeFrom&) { out += "[upgraded from group]"; },
          [&](td::td_api::messageChatSetMessageAutoDeleteTime&) { out += "[auto-delete time set]"; },
          [&](td::td_api::messageChatBoost&) { out += "[chat boost]"; },
          // Forum topics
          [&](td::td_api::messageForumTopicCreated&) { out += "[forum topic created]"; },
          [&](td::td_api::messageForumTopicEdited&) { out += "[forum topic edited]"; },
          [&](td::td_api::messageForumTopicIsClosedToggled&) { out += "[forum topic closed toggled]"; },
          [&](td::td_api::messageForumTopicIsHiddenToggled&) { out += "[forum topic hidden toggled]"; },
          // User suggestions
          [&](td::td_api::messageSuggestProfilePhoto&) { out += "[suggest profile photo]"; },
          [&](td::td_api::messageSuggestBirthdate&) { out += "[suggest birthdate]"; },
          // Service messages
          [&](td::td_api::messageCustomServiceAction&) { out += "[custom service action]"; },
          [&](td::td_api::messageGameScore& score) {
              out += "<game_score score=\"{}\" />"_format(score.score_);
          },
          // Payment messages
          [&](td::td_api::messagePaymentSuccessful& payment) {
              out += "<payment_successful currency=\"{}\" amount=\"{}\" is_recurring=\"{}\" />"_format(payment.currency_, payment.total_amount_, payment.is_recurring_);
          },
          [&](td::td_api::messagePaymentSuccessfulBot& payment) {
              out += "<payment_successful_bot currency=\"{}\" amount=\"{}\" is_recurring=\"{}\" />"_format(payment.currency_, payment.total_amount_, payment.is_recurring_);
          },
          [&](td::td_api::messagePaymentRefunded& refund) { out += "[payment refunded]"; },
          // Premium/gifts
          [&](td::td_api::messageGiftedPremium& premium) {
              out += "<gifted_premium months=\"{}\" days=\"{}\" amount=\"{}\" currency=\"{}\" />"_format(premium.month_count_, premium.day_count_, premium.amount_, premium.currency_);
          },
          [&](td::td_api::messagePremiumGiftCode& code) { out += "[premium gift code]"; },
          // Giveaways
          [&](td::td_api::messageGiveawayCreated&) { out += "[giveaway created]"; },
          [&](td::td_api::messageGiveaway&) { out += "[giveaway]"; },
          [&](td::td_api::messageGiveawayCompleted& completed) {
              out += "<giveaway_completed winners=\"{}\" unclaimed=\"{}\" is_star_giveaway=\"{}\" />"_format(completed.winner_count_, completed.unclaimed_prize_count_, completed.is_star_giveaway_);
          },
          [&](td::td_api::messageGiveawayWinners& winners) {
              out += "<giveaway_winners count=\"{}\" unclaimed=\"{}\" additional_chats=\"{}\" />"_format(winners.winner_count_, winners.unclaimed_prize_count_, winners.additional_chat_count_);
          },
          // Stars/gifts
          [&](td::td_api::messageGiftedStars& stars) {
              out += "<gifted_stars count=\"{}\" />"_format(stars.star_count_);
          },
          [&](td::td_api::messageGiftedTon& ton) {
              out += "<gifted_ton amount=\"{}\" />"_format(ton.ton_amount_);
          },
          [&](td::td_api::messageGiveawayPrizeStars& prize) {
              out += "<giveaway_prize_stars count=\"{}\" />"_format(prize.star_count_);
          },
          [&](td::td_api::messageGift& gift) {
              out += "[gift]";
          },
          [&](td::td_api::messageUpgradedGift&) { out += "[upgraded gift]"; },
          [&](td::td_api::messageRefundedUpgradedGift&) { out += "[refunded upgraded gift]"; },
          [&](td::td_api::messageUpgradedGiftPurchaseOffer&) { out += "[upgraded gift purchase offer]"; },
          [&](td::td_api::messageUpgradedGiftPurchaseOfferRejected&) { out += "[upgraded gift purchase offer rejected]"; },
          // Paid messages
          [&](td::td_api::messagePaidMessagesRefunded& refund) {
              out += "<paid_messages_refunded count=\"{}\" stars=\"{}\" />"_format(refund.message_count_, refund.star_count_);
          },
          [&](td::td_api::messagePaidMessagePriceChanged& price) {
              out += "<paid_message_price_changed stars=\"{}\" />"_format(price.paid_message_star_count_);
          },
          [&](td::td_api::messageDirectMessagePriceChanged& price) {
              out += "<direct_message_price_changed enabled=\"{}\" stars=\"{}\" />"_format(price.is_enabled_, price.paid_message_star_count_);
          },
          // Checklist tasks
          [&](td::td_api::messageChecklistTasksDone& done) {
              out += "<checklist_tasks_done done=\"{}\" not_done=\"{}\" />"_format(done.marked_as_done_task_ids_.size(), done.marked_as_not_done_task_ids_.size());
          },
          [&](td::td_api::messageChecklistTasksAdded& added) {
              out += "<checklist_tasks_added count=\"{}\" />"_format(added.tasks_.size());
          },
          // Suggested posts
          [&](td::td_api::messageSuggestedPostApprovalFailed&) { out += "[suggested post approval failed]"; },
          [&](td::td_api::messageSuggestedPostApproved&) { out += "[suggested post approved]"; },
          [&](td::td_api::messageSuggestedPostDeclined&) { out += "[suggested post declined]"; },
          [&](td::td_api::messageSuggestedPostPaid&) { out += "[suggested post paid]"; },
          [&](td::td_api::messageSuggestedPostRefunded&) { out += "[suggested post refunded]"; },
          // User interactions
          [&](td::td_api::messageContactRegistered&) { out += "[contact registered]"; },
          [&](td::td_api::messageUsersShared& shared) {
              out += "<users_shared count=\"{}\" />"_format(shared.users_.size());
          },
          [&](td::td_api::messageChatShared&) { out += "[chat shared]"; },
          [&](td::td_api::messageBotWriteAccessAllowed&) { out += "[bot write access allowed]"; },
          // Web app
          [&](td::td_api::messageWebAppDataSent&) { out += "[web app data sent]"; },
          [&](td::td_api::messageWebAppDataReceived&) { out += "[web app data received]"; },
          // Passport
          [&](td::td_api::messagePassportDataReceived&) { out += "[passport data received]"; },
          [&](td::td_api::messagePassportDataSent&) { out += "[passport data sent]"; },
          []<typename T>(T&) { static_assert(sizeof(T) == 0, "Missing handlers"); },
        });
    return out;
}


AFuture<AString> llmui::formatChatHistoryMessage(
    ITelegramClient& telegram,
    td::td_api::message& msg,
    const td::td_api::chat& chat,
    IOpenAIChat& openAI,
    std::span<const IOpenAIChat::Message> temporaryContext,
    AStringView xmlTag) {
    AString senderName = co_await extractSenderName(telegram, *msg.sender_id_);
    AString formattedXmlTag = "{} message_id=\"{}\" date=\"{}\""_format(
        xmlTag, msg.id_, std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>(std::chrono::seconds(msg.date_)));
    int64_t senderId {};
    td::td_api::downcast_call(
        *msg.sender_id_,
        aui::lambda_overloaded {
          [&](td::td_api::messageSenderUser& user) { senderId = user.user_id_; },
          [&](td::td_api::messageSenderChat& chat) { senderId = chat.chat_id_; },
        });

    if (!msg.sender_tag_.empty()) {
        formattedXmlTag += " chat_tag=\"{}\""_format(msg.sender_tag_);
    }
    if (!msg.author_signature_.empty()) {
        formattedXmlTag += " author_signature=\"{}\""_format(msg.author_signature_);
    }

    if (senderId != telegram.myId() && chat.last_read_inbox_message_id_ <= msg.id_) {
        formattedXmlTag += " unread";
    }
    if (msg.forward_info_) {
        // explanation: from perspective of telegram, sender is the one who shared a message to you.
        //
        // <message sender="John" forwarded_from="Fox News">
        // btc is 100k$t
        // </message>
        //
        // The message above means that a person named "John" forwarded a post from Fox News about btc hitting
        // 100k$t.
        //
        // However, LLM doesn't seem care about `forwarded_from` attribute, and responds to you as if
        // `btc is 100k$t` was authored by John.
        //
        // This branch solves this problem: we swap sender and forward authors:
        //
        // <message sender="Fox News" forwarded_by="John">
        // btc is 100k$t
        // </message>
        //
        // So the LLM knows that author of this post is Fox News and it was shared by John.
        //
        formattedXmlTag += " sender=\"";
        auto forwardedFromChatId = [&] {
            switch (msg.forward_info_->origin_->get_id()) {
                case td::td_api::messageOriginChannel::ID:
                    return static_cast<td::td_api::messageOriginChannel&>(*msg.forward_info_->origin_).chat_id_;
                case td::td_api::messageOriginUser::ID:
                    return static_cast<td::td_api::messageOriginUser&>(*msg.forward_info_->origin_).sender_user_id_;
                case td::td_api::messageOriginChat::ID:
                    return static_cast<td::td_api::messageOriginChat&>(*msg.forward_info_->origin_).sender_chat_id_;
                default:
                    return td::td_api::int53(0);
            }
        }();
        formattedXmlTag += co_await extractSenderName(telegram, forwardedFromChatId);
        formattedXmlTag += "\"";

        if (!senderName.empty()) {
            formattedXmlTag += " forwarded_by=\"{}\""_format(senderName);
        }
    } else {
        if (!senderName.empty()) {
            formattedXmlTag += " sender=\"{}\""_format(senderName);
        }
    }
    if (msg.interaction_info_) {
        if (msg.interaction_info_->reactions_) {
            AString reactionsAttr;
            for (auto& reaction : msg.interaction_info_->reactions_->reactions_) {
                AString emoji;
                td::td_api::downcast_call(
                    *reaction->type_,
                    aui::lambda_overloaded {
                      [&](const td::td_api::reactionTypeEmoji& v) { emoji += v.emoji_; },
                      [](const td::td_api::reactionTypePaid& v) {
                          // don't care
                      },
                      [](const td::td_api::reactionTypeCustomEmoji& v) {
                          // don't care
                      },
                    });
                if (emoji.empty()) {
                    continue;
                }
                if (!reactionsAttr.empty()) {
                    reactionsAttr += ";";
                }
                reactionsAttr += "({} "_format(emoji);
                AUI_DEFER { reactionsAttr += ")"; };
                if (reaction->total_count_ > 3) {
                    // if reactions above 3, format as emoji + react counts, just like regular telegram clients
                    // do.
                    reactionsAttr += "{}"_format(reaction->total_count_);
                    continue;
                }
                reactionsAttr += " by ";
                bool first = true;
                for (auto& sender : reaction->recent_sender_ids_) {
                    if (first) {
                        first = false;
                    } else {
                        reactionsAttr += ", ";
                    }
                    reactionsAttr += co_await extractSenderName(telegram, *sender);
                }
            }
            if (!reactionsAttr.empty()) {
                formattedXmlTag += " reactions=\"{}\""_format(reactionsAttr);
            }
        }
    }

    auto result = "<{}>\n"_format(formattedXmlTag);
    if (xmlTag != "reply_to") {
        if (msg.reply_to_ && msg.reply_to_->get_id() == td::td_api::messageReplyToMessage::ID) {
            try {
                auto reply = td::td_api::move_object_as<td::td_api::messageReplyToMessage>(std::move(msg.reply_to_));
                auto replyToMsg = co_await telegram.getMessage(msg.chat_id_, reply->message_id_);
                result += co_await llmui::formatChatHistoryMessage(telegram, *replyToMsg, chat, openAI, temporaryContext, "reply_to");
            } catch (const AException& e) {
                if (e.getMessage().contains("Not Found")) {
                    result += "<reply_to>Deleted Message</reply_to>";
                } else {
                    ALogger::err("formatChatHistoryMessage") << e;
                }
            }
        }

        if (msg.content_->get_id() == td::td_api::messagePhoto::ID) {
            auto& photo = static_cast<td::td_api::messagePhoto&>(*msg.content_);
            if (auto targetPhotoIt = ranges::max_element(
                    photo.photo_->sizes_, std::less {}, [&](const auto& s) { return s->width_ * s->height_; });
                targetPhotoIt != photo.photo_->sizes_.end()) {
                result += co_await llmui::image(temporaryContext, openAI, co_await fetchMedia(telegram, targetPhotoIt->get()->photo_));
            }
        }

        if (msg.content_->get_id() == td::td_api::messageSticker::ID) {
            auto& sticker = static_cast<td::td_api::messageSticker&>(*msg.content_);
            AString xmlTag = "sticker";
            if (!sticker.sticker_->emoji_.empty()) {
                checkForMaliciousPayloads(sticker.sticker_->emoji_);
                xmlTag += " emoji=\"{}\""_format(sticker.sticker_->emoji_);
            }
            if constexpr (config::CAPABILITY_USE_STICKERS) {
                xmlTag += " sticker_id=\"{}\""_format(sticker.sticker_->id_);
            }
            if (sticker.sticker_->sticker_) {
                result += co_await llmui::image(
                    temporaryContext, openAI, co_await fetchMedia(telegram, sticker.sticker_->sticker_), xmlTag);
            }
            const auto id = sticker.sticker_->id_;
            tools::stickers::knownStickers()[id] = std::move(sticker.sticker_);
        }

        if (msg.content_->get_id() == td::td_api::messageGift::ID) {
            auto& gift = static_cast<td::td_api::messageGift&>(*msg.content_);
            auto xmlTag = "gift cost=\"{} stars\""_format(gift.gift_->star_count_);
            if (gift.text_) {
                checkForMaliciousPayloads(gift.text_->text_);
                xmlTag += " text=\"" + gift.text_->text_ + "\"";
            }

            if (gift.gift_->sticker_) {
                if (!gift.gift_->sticker_->emoji_.empty()) {
                    checkForMaliciousPayloads(gift.gift_->sticker_->emoji_);
                    xmlTag += " emoji=\"{}\""_format(gift.gift_->sticker_->emoji_);
                }

                result += co_await llmui::image(
                    temporaryContext, openAI, co_await fetchMedia(telegram, gift.gift_->sticker_->sticker_), xmlTag);
            } else {
                result += "<{} />"_format(xmlTag);
            }
        }

        if (msg.content_->get_id() == td::td_api::messageAnimation::ID) {
            auto& animation = static_cast<td::td_api::messageAnimation&>(*msg.content_);
            if (animation.animation_->thumbnail_) {
                result += co_await llmui::image(
                    temporaryContext,
                    openAI,
                    co_await fetchMedia(telegram, animation.animation_->thumbnail_->file_),
                    "animation");
            }
        }

        if (msg.content_->get_id() == td::td_api::messageVoiceNote::ID) {
            auto& voiceNote = static_cast<td::td_api::messageVoiceNote&>(*msg.content_);
            if (voiceNote.voice_note_) {
                result += co_await llmui::voiceMessage(co_await fetchMedia(telegram, voiceNote.voice_note_->voice_));
            }
        }
    }

    result += extractMessageTypeAndText(msg);

    result += "\n</{}>\n"_format(formattedXmlTag);
    co_return result;
}

AString llmui::formatLinkPreview(const td::td_api::linkPreview& preview) {
    AString out = "<link_preview";
    if (!preview.url_.empty()) {
        out += " url=\"{}\""_format(preview.url_);
    }
    if (!preview.site_name_.empty()) {
        out += " site=\"{}\""_format(preview.site_name_);
    }
    if (!preview.title_.empty()) {
        out += " title=\"{}\""_format(preview.title_);
    }
    if (!preview.author_.empty()) {
        out += " author=\"{}\""_format(preview.author_);
    }
    if (preview.description_ && !preview.description_->text_.empty()) {
        out += ">\n{}\n</link_preview>"_format(preview.description_->text_);
    } else {
        out += " />";
    }
    out += "\n";
    return out;
}
