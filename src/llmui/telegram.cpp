//
// Created by alex2772 on 5/9/26.
//

#include "telegram.h"

#include "audio.h"
#include "config.h"
#include "image.h"
#include "video.h"
#include "malicious_payloads.h"
#include "telegram_video_messages.h"
#include "telegram_voice_messages.h"
#include "AUI/Util/kAUI.h"
#include "tools/stickers.h"
#include "util/is_accessible_from_lockdown.h"
#include "util/time_ago.h"

#include <range/v3/algorithm/max_element.hpp>

static constexpr auto LOG_TAG = "llmui::telegram";

/**
 * @brief Escapes a string to be safely placed inside a double-quoted XML attribute.
 */
static AString xmlAttr(AString s) {
    s.replaceAll("&", "&amp;");
    s.replaceAll("\"", "&quot;");
    s.replaceAll("<", "&lt;");
    s.replaceAll(">", "&gt;");
    return s;
}

static AFuture<AString> extractSenderName(ITelegramClient& telegram, int64_t senderId) {
    AString senderName;
    if (senderId == telegram.myId()) {
        senderName = "You ({})"_format(config().characterName);
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
        ALOG_TRACE(LOG_TAG) << "fetchMedia: downloadFile file_id=" << file->id_ << " (awaiting)";
        file = co_await telegram.sendQueryWithResult(
            ITelegramClient::toPtr(td::td_api::downloadFile(file->id_, 16, 0, 0, true)),
            timeouts::TELEGRAM_DOWNLOAD);
        ALOG_TRACE(LOG_TAG) << "fetchMedia: downloadFile done";
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
        chat.id_, xmlAttr(chat.title_), xmlAttr(preview), type, lastMessageAgo);
    if (chat.unread_count_ > 0) {
        result += " unread_count=\"{}\""_format(chat.unread_count_);
    }
    result += " />\n";
}

AFuture<>
llmui::formatChatList(ITelegramClient& telegram, AString& result, std::span<_<td::td_api::chat>> chats) {
    for (auto& chat : chats) {
        if (chat == nullptr) {
            continue;
        }
        co_await formatChatSingle(telegram, result, *chat);
    }
}

AString llmui::extractMessageTypeAndText(td::td_api::message& msg, bool includeMediaMarker) {
    AString out;
    // the media marker is omitted when the caller has already inserted a rich xml representation of the media
    // (e.g. <photo description>...</photo>), so the LLM does not see the same media twice.
    auto marker = [&](AStringView xml) {
        if (includeMediaMarker) {
            out += xml;
        }
    };
    auto caption = [&](td::td_api::object_ptr<td::td_api::formattedText>& c) {
        if (!c || c->text_.empty()) {
            return;
        }
        checkForMaliciousPayloads(c->text_);
        if (!out.empty()) {
            out += "\n";
        }
        out += c->text_;
    };
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
              marker("<photo />");
              caption(photo.caption_);
          },
          [&](td::td_api::messageAnimation& anim) {
              marker("<animation />");
              caption(anim.caption_);
          },
          [&](td::td_api::messageAudio& audio) {
              if (audio.audio_->title_.empty()) {
                  out += "<audio />";
              } else {
                  out += "<audio title=\"{}\" />"_format(xmlAttr(audio.audio_->title_));
              }
              caption(audio.caption_);
          },
          [&](td::td_api::messageDocument& doc) {
              if (doc.document_->file_name_.empty()) {
                  out += "<document />";
              } else {
                  out += "<document file_name=\"{}\" />"_format(xmlAttr(doc.document_->file_name_));
              }
              caption(doc.caption_);
          },
          [&](td::td_api::messageVideo& video) {
              marker("<video />");
              caption(video.caption_);
          },
          [&](td::td_api::messageVideoNote&) { marker("<video_note />"); },
          [&](td::td_api::messageVoiceNote& voice) {
              marker("<voice_message />");
              caption(voice.caption_);
          },
          [&](td::td_api::messageSticker& st) { marker("<sticker />"); },
          [&](td::td_api::messageLocation& loc) {
              out += "<location lat=\"{}\" lon=\"{}\" />"_format(loc.location_->latitude_, loc.location_->longitude_);
          },
          [&](td::td_api::messageVenue& ven) {
              out += "<venue title=\"{}\" address=\"{}\" />"_format(xmlAttr(ven.venue_->title_), xmlAttr(ven.venue_->address_));
          },
          [&](td::td_api::messageContact& c) {
              out += "<contact first_name=\"{}\" last_name=\"{}\" phone_number=\"{}\" />"_format(
                  xmlAttr(c.contact_->first_name_), xmlAttr(c.contact_->last_name_), xmlAttr(c.contact_->phone_number_));
          },
          [&](td::td_api::messagePoll& p) {
              out += "<poll question=\"{}\">\n"_format(xmlAttr(p.poll_->question_->text_));
              for (const auto& o : p.poll_->options_) {
                  out += "<option text=\"{}\" />\n"_format(xmlAttr(o->text_->text_));
              }
              out += "</poll>";
          },
          [&](td::td_api::messageInvoice& inv) { out += "<invoice />"; },
          [&](td::td_api::messageGame& game) {
              out += "<game title=\"{}\" description=\"{}\" />"_format(xmlAttr(game.game_->title_), xmlAttr(game.game_->description_));
          },
          [&](td::td_api::messageDice& dice) {
              out += "<dice emoji=\"{}\" value=\"{}\" />"_format(xmlAttr(dice.emoji_), dice.value_);
          },
          [&](td::td_api::messageCall& call) {
              out += "<call is_video=\"{}\" duration=\"{}\" />"_format(call.is_video_, call.duration_);
          },
          [&](td::td_api::messageChatAddMembers& add) {
              out += "<members_added count=\"{}\" />"_format(add.member_user_ids_.size());
          },
          [&](td::td_api::messageChatJoinByLink&) { out += "<joined_via_link />"; },
          [&](td::td_api::messageChatJoinByRequest&) { out += "<joined_by_request />"; },
          [&](td::td_api::messageChatDeleteMember& del) {
              out += "<member_removed user_id=\"{}\" />"_format(del.user_id_);
          },
          [&](td::td_api::messageBasicGroupChatCreate& cg) { out += "<group_created title=\"{}\" />"_format(xmlAttr(cg.title_)); },
          [&](td::td_api::messageSupergroupChatCreate& cg) { out += "<supergroup_created title=\"{}\" />"_format(xmlAttr(cg.title_)); },
          [&](td::td_api::messageChatChangeTitle& ct) { out += "<title_changed title=\"{}\" />"_format(xmlAttr(ct.title_)); },
          [&](td::td_api::messageChatChangePhoto&) { out += "<chat_photo_changed />"; },
          [&](td::td_api::messagePinMessage& pin) {
              out += "<message_pinned message_id=\"{}\" />"_format(pin.message_id_);
          },
          [&](td::td_api::messageChatSetTheme& th) { out += "<chat_theme_set />"; },
          [&](td::td_api::messageChatSetBackground& ttl) { out += "<chat_background_set />"; },
          [&](td::td_api::messageScreenshotTaken&) { out += "<screenshot_taken />"; },
          [&](td::td_api::messageProximityAlertTriggered&) { out += "<proximity_alert />"; },
          [&](td::td_api::messageUnsupported&) { out += "<unsupported_message />"; },
          // Media messages
          [&](td::td_api::messagePaidMedia&) { out += "<paid_media />"; },
          // Expired media
          [&](td::td_api::messageExpiredPhoto&) { out += "<expired_photo />"; },
          [&](td::td_api::messageExpiredVideo&) { out += "<expired_video />"; },
          [&](td::td_api::messageExpiredVideoNote&) { out += "<expired_video_note />"; },
          [&](td::td_api::messageExpiredVoiceNote&) { out += "<expired_voice_note />"; },
          // Emoji messages
          [&](td::td_api::messageAnimatedEmoji&) { out += "<animated_emoji />"; },
          // Game/stakes messages
          [&](td::td_api::messageStakeDice&) { out += "<stake_dice />"; },
          [&](td::td_api::messageStory&) { out += "<story />"; },
          // Checklist
          [&](td::td_api::messageChecklist&) { out += "<checklist />"; },
          // Group calls
          [&](td::td_api::messageGroupCall&) { out += "<group_call />"; },
          [&](td::td_api::messageVideoChatScheduled&) { out += "<video_chat_scheduled />"; },
          [&](td::td_api::messageVideoChatStarted&) { out += "<video_chat_started />"; },
          [&](td::td_api::messageVideoChatEnded&) { out += "<video_chat_ended />"; },
          [&](td::td_api::messageInviteVideoChatParticipants&) { out += "<invited_to_video_chat />"; },
          // Chat management
          [&](td::td_api::messageChatDeletePhoto&) { out += "<chat_photo_deleted />"; },
          [&](td::td_api::messageChatOwnerLeft&) { out += "<chat_owner_left />"; },
          [&](td::td_api::messageChatOwnerChanged&) { out += "<chat_owner_changed />"; },
          [&](td::td_api::messageChatHasProtectedContentToggled&) { out += "<protected_content_toggled />"; },
          [&](td::td_api::messageChatHasProtectedContentDisableRequested&) { out += "<protected_content_disable_requested />"; },
          [&](td::td_api::messageChatUpgradeTo&) { out += "<upgraded_to_supergroup />"; },
          [&](td::td_api::messageChatUpgradeFrom&) { out += "<upgraded_from_group />"; },
          [&](td::td_api::messageChatSetMessageAutoDeleteTime&) { out += "<auto_delete_time_set />"; },
          [&](td::td_api::messageChatBoost&) { out += "<chat_boost />"; },
          // Forum topics
          [&](td::td_api::messageForumTopicCreated&) { out += "<forum_topic_created />"; },
          [&](td::td_api::messageForumTopicEdited&) { out += "<forum_topic_edited />"; },
          [&](td::td_api::messageForumTopicIsClosedToggled&) { out += "<forum_topic_closed_toggled />"; },
          [&](td::td_api::messageForumTopicIsHiddenToggled&) { out += "<forum_topic_hidden_toggled />"; },
          // User suggestions
          [&](td::td_api::messageSuggestProfilePhoto&) { out += "<suggest_profile_photo />"; },
          [&](td::td_api::messageSuggestBirthdate&) { out += "<suggest_birthdate />"; },
          // Service messages
          [&](td::td_api::messageCustomServiceAction&) { out += "<custom_service_action />"; },
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
          [&](td::td_api::messagePaymentRefunded& refund) { out += "<payment_refunded />"; },
          // Premium/gifts
          [&](td::td_api::messageGiftedPremium& premium) {
              out += "<gifted_premium months=\"{}\" days=\"{}\" amount=\"{}\" currency=\"{}\" />"_format(premium.month_count_, premium.day_count_, premium.amount_, premium.currency_);
          },
          [&](td::td_api::messagePremiumGiftCode& code) { out += "<premium_gift_code />"; },
          // Giveaways
          [&](td::td_api::messageGiveawayCreated&) { out += "<giveaway_created />"; },
          [&](td::td_api::messageGiveaway&) { out += "<giveaway />"; },
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
          [&](td::td_api::messageGift& gift) { marker("<gift />"); },
          [&](td::td_api::messageUpgradedGift&) { out += "<upgraded_gift />"; },
          [&](td::td_api::messageRefundedUpgradedGift&) { out += "<refunded_upgraded_gift />"; },
          [&](td::td_api::messageUpgradedGiftPurchaseOffer&) { out += "<upgraded_gift_purchase_offer />"; },
          [&](td::td_api::messageUpgradedGiftPurchaseOfferRejected&) { out += "<upgraded_gift_purchase_offer_rejected />"; },
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
          [&](td::td_api::messageSuggestedPostApprovalFailed&) { out += "<suggested_post_approval_failed />"; },
          [&](td::td_api::messageSuggestedPostApproved&) { out += "<suggested_post_approved />"; },
          [&](td::td_api::messageSuggestedPostDeclined&) { out += "<suggested_post_declined />"; },
          [&](td::td_api::messageSuggestedPostPaid&) { out += "<suggested_post_paid />"; },
          [&](td::td_api::messageSuggestedPostRefunded&) { out += "<suggested_post_refunded />"; },
          // User interactions
          [&](td::td_api::messageContactRegistered&) { out += "<contact_registered />"; },
          [&](td::td_api::messageUsersShared& shared) {
              out += "<users_shared count=\"{}\" />"_format(shared.users_.size());
          },
          [&](td::td_api::messageChatShared&) { out += "<chat_shared />"; },
          [&](td::td_api::messageBotWriteAccessAllowed&) { out += "<bot_write_access_allowed />"; },
          // Web app
          [&](td::td_api::messageWebAppDataSent&) { out += "<web_app_data_sent />"; },
          [&](td::td_api::messageWebAppDataReceived&) { out += "<web_app_data_received />"; },
          // Passport
          [&](td::td_api::messagePassportDataReceived&) { out += "<passport_data_received />"; },
          [&](td::td_api::messagePassportDataSent&) { out += "<passport_data_sent />"; },
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
    ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: message_id=" << msg.id_
                           << " content_id=" << msg.content_->get_id() << " extractSenderName (awaiting)";
    AString senderName = co_await extractSenderName(telegram, *msg.sender_id_);
    ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: message_id=" << msg.id_ << " extractSenderName done";
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
        ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: forwarded extractSenderName (awaiting)";
        formattedXmlTag += co_await extractSenderName(telegram, forwardedFromChatId);
        formattedXmlTag += "\"";

        if (!senderName.empty()) {
            formattedXmlTag += " forwarded_by=\"{}\""_format(senderName);
        }
    } else {
        if (!senderName.empty()) {
            formattedXmlTag += " sender=\"{}\""_format(senderName);
        }
        if (chat.type_->get_id() == td::td_api::chatTypeBasicGroup::ID || chat.type_->get_id() == td::td_api::chatTypeSupergroup::ID) {
            // for bans.
            formattedXmlTag += " sender_id=\"{}\""_format(senderId);
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
                    ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: reaction extractSenderName (awaiting)";
                    reactionsAttr += co_await extractSenderName(telegram, *sender);
                }
            }
            if (!reactionsAttr.empty()) {
                formattedXmlTag += " reactions=\"{}\""_format(reactionsAttr);
            }
        }
    }

    if (msg.reply_to_ && msg.reply_to_->get_id() == td::td_api::messageReplyToMessage::ID) {
        try {
            auto reply = td::td_api::move_object_as<td::td_api::messageReplyToMessage>(std::move(msg.reply_to_));
            formattedXmlTag += " reply_to=\"{}\""_format(reply->message_id_);
        } catch (const AException& e) {
        }
    }
    auto result = "<{}>\n"_format(formattedXmlTag);
    // used to detect whether the media was expanded into a rich xml representation below
    const auto headerLength = result.length();

    if (msg.content_->get_id() == td::td_api::messagePhoto::ID) {
        auto& photo = static_cast<td::td_api::messagePhoto&>(*msg.content_);
        if (auto targetPhotoIt = ranges::max_element(
                photo.photo_->sizes_, std::less {}, [&](const auto& s) { return s->width_ * s->height_; });
            targetPhotoIt != photo.photo_->sizes_.end()) {
            ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: photo (awaiting)";
            result += co_await llmui::image(temporaryContext, openAI, co_await fetchMedia(telegram, targetPhotoIt->get()->photo_));
            ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: photo done";
        }
    }

    if (msg.content_->get_id() == td::td_api::messageSticker::ID) {
        auto& sticker = static_cast<td::td_api::messageSticker&>(*msg.content_);
        AString xmlTag = "sticker";
        if (!sticker.sticker_->emoji_.empty()) {
            checkForMaliciousPayloads(sticker.sticker_->emoji_);
            xmlTag += " emoji=\"{}\""_format(sticker.sticker_->emoji_);
        }
        if (config().capabilityUseStickers) {
            xmlTag += " sticker_id=\"{}\""_format(fmt::group_digits(sticker.sticker_->id_));
        }
        if (sticker.sticker_->sticker_) {
            ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: sticker (awaiting)";
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

            ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: gift (awaiting)";
            result += co_await llmui::image(
                temporaryContext, openAI, co_await fetchMedia(telegram, gift.gift_->sticker_->sticker_), xmlTag);
        } else {
            result += "<{} />"_format(xmlTag);
        }
    }

    if (msg.content_->get_id() == td::td_api::messageAnimation::ID) {
        auto& animation = static_cast<td::td_api::messageAnimation&>(*msg.content_);
        if (animation.animation_->thumbnail_) {
            ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: animation (awaiting)";
            result += co_await llmui::image(
                temporaryContext,
                openAI,
                co_await fetchMedia(telegram, animation.animation_->thumbnail_->file_),
                "animation");
        }
    }

    if (msg.content_->get_id() == td::td_api::messageVideo::ID) {
        auto& videoMsg = static_cast<td::td_api::messageVideo&>(*msg.content_);
        if (videoMsg.video_) {
            ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: video (awaiting)";
            result += co_await llmui::video(
                temporaryContext,
                openAI,
                co_await fetchMedia(telegram, videoMsg.video_->video_),
                "video");
        }
    }

    if (msg.content_->get_id() == td::td_api::messageVoiceNote::ID) {
        auto& voiceNote = static_cast<td::td_api::messageVoiceNote&>(*msg.content_);
        if (voiceNote.voice_note_) {
            ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: voice note (awaiting)";
            result += co_await llmui::voiceMessageTranscription(telegram, voiceNote, msg.chat_id_, msg.id_, openAI);
            ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: voice note done";
        }
    }

    if (msg.content_->get_id() == td::td_api::messageVideoNote::ID) {
        auto& videoNote = static_cast<td::td_api::messageVideoNote&>(*msg.content_);
        if (videoNote.video_note_) {
            ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: video note (awaiting)";
            result += co_await llmui::videoNoteTranscription(telegram, openAI, videoNote, msg.chat_id_, msg.id_);
        }
    }

    // do not duplicate the media marker (<photo />, etc.) if the media is already described above
    result += extractMessageTypeAndText(msg, /* includeMediaMarker = */ result.length() == headerLength);

    result += "\n</{}>\n"_format(formattedXmlTag);
    ALOG_TRACE(LOG_TAG) << "formatChatHistoryMessage: message_id=" << msg.id_ << " done";
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
