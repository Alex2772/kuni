//
// Created by alex2772 on 5/9/26.
//

#include "llmui/telegram.h"
#include "../common.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"

#include <gmock/gmock.h>

using namespace testing;

// ===========================================================================
// extractMessageTypeAndText – Plain text message
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_PlainText) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 1;
    msg->content_ = td::td_api::make_object<td::td_api::messageText>();
    auto& text = static_cast<td::td_api::messageText&>(*msg->content_);
    text.text_ = td::td_api::make_object<td::td_api::formattedText>();
    text.text_->text_ = "Hello, world!";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "Hello, world!");
}

// ===========================================================================
// extractMessageTypeAndText – Photo with caption
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_PhotoWithCaption) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 2;
    msg->content_ = td::td_api::make_object<td::td_api::messagePhoto>();
    auto& photo = static_cast<td::td_api::messagePhoto&>(*msg->content_);
    photo.caption_ = td::td_api::make_object<td::td_api::formattedText>();
    photo.caption_->text_ = "A sunny day at the beach";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[photo]\nA sunny day at the beach");
}

// ===========================================================================
// extractMessageTypeAndText – Photo without caption
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_PhotoNoCaption) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 3;
    msg->content_ = td::td_api::make_object<td::td_api::messagePhoto>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[photo]");
}

// ===========================================================================
// extractMessageTypeAndText – Sticker
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Sticker) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 4;
    msg->content_ = td::td_api::make_object<td::td_api::messageSticker>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[sticker]");
}

// ===========================================================================
// extractMessageTypeAndText – Animation with caption
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_AnimationWithCaption) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 5;
    msg->content_ = td::td_api::make_object<td::td_api::messageAnimation>();
    auto& anim = static_cast<td::td_api::messageAnimation&>(*msg->content_);
    anim.caption_ = td::td_api::make_object<td::td_api::formattedText>();
    anim.caption_->text_ = "Funny cat gif";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[animation]\nFunny cat gif");
}

// ===========================================================================
// extractMessageTypeAndText – Audio with title
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Audio) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 6;
    msg->content_ = td::td_api::make_object<td::td_api::messageAudio>();
    auto& audio = static_cast<td::td_api::messageAudio&>(*msg->content_);
    audio.audio_ = td::td_api::make_object<td::td_api::audio>();
    audio.audio_->title_ = "Bohemian Rhapsody";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[audio] Bohemian Rhapsody");
}

// ===========================================================================
// extractMessageTypeAndText – Audio with title and caption
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_AudioWithCaption) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 7;
    msg->content_ = td::td_api::make_object<td::td_api::messageAudio>();
    auto& audio = static_cast<td::td_api::messageAudio&>(*msg->content_);
    audio.audio_ = td::td_api::make_object<td::td_api::audio>();
    audio.audio_->title_ = "Song Title";
    audio.caption_ = td::td_api::make_object<td::td_api::formattedText>();
    audio.caption_->text_ = "Audio caption";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[audio] Song Title\nAudio caption");
}

// ===========================================================================
// extractMessageTypeAndText – Document with filename
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Document) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 8;
    msg->content_ = td::td_api::make_object<td::td_api::messageDocument>();
    auto& doc = static_cast<td::td_api::messageDocument&>(*msg->content_);
    doc.document_ = td::td_api::make_object<td::td_api::document>();
    doc.document_->file_name_ = "report.pdf";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[document] report.pdf");
}

// ===========================================================================
// extractMessageTypeAndText – Document without filename
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_DocumentNoName) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 9;
    msg->content_ = td::td_api::make_object<td::td_api::messageDocument>();
    auto& doc = static_cast<td::td_api::messageDocument&>(*msg->content_);
    doc.document_ = td::td_api::make_object<td::td_api::document>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[document] <unnamed>");
}

// ===========================================================================
// extractMessageTypeAndText – Video
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Video) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 10;
    msg->content_ = td::td_api::make_object<td::td_api::messageVideo>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[video]");
}

// ===========================================================================
// extractMessageTypeAndText – Video with caption
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_VideoWithCaption) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 11;
    msg->content_ = td::td_api::make_object<td::td_api::messageVideo>();
    auto& video = static_cast<td::td_api::messageVideo&>(*msg->content_);
    video.caption_ = td::td_api::make_object<td::td_api::formattedText>();
    video.caption_->text_ = "My vacation video";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[video]\nMy vacation video");
}

// ===========================================================================
// extractMessageTypeAndText – Video note
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_VideoNote) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 12;
    msg->content_ = td::td_api::make_object<td::td_api::messageVideoNote>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[video note]");
}

// ===========================================================================
// extractMessageTypeAndText – Voice note (no caption)
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_VoiceNote) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 13;
    msg->content_ = td::td_api::make_object<td::td_api::messageVoiceNote>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    // Voice note without caption produces empty output (caption branch is commented out)
    EXPECT_EQ(result, "");
}

// ===========================================================================
// extractMessageTypeAndText – Voice note with caption
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_VoiceNoteWithCaption) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 14;
    msg->content_ = td::td_api::make_object<td::td_api::messageVoiceNote>();
    auto& voice = static_cast<td::td_api::messageVoiceNote&>(*msg->content_);
    voice.caption_ = td::td_api::make_object<td::td_api::formattedText>();
    voice.caption_->text_ = "Voice caption";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "\nVoice caption");
}

// ===========================================================================
// extractMessageTypeAndText – Location
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Location) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 15;
    msg->content_ = td::td_api::make_object<td::td_api::messageLocation>();
    auto& loc = static_cast<td::td_api::messageLocation&>(*msg->content_);
    loc.location_ = td::td_api::make_object<td::td_api::location>();
    loc.location_->latitude_ = 55.7558;
    loc.location_->longitude_ = 37.6173;

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_TRUE(result.contains("[location]"));
    EXPECT_TRUE(result.contains("lat=55.7"));
    EXPECT_TRUE(result.contains("lon=37.6"));
}

// ===========================================================================
// extractMessageTypeAndText – Venue
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Venue) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 16;
    msg->content_ = td::td_api::make_object<td::td_api::messageVenue>();
    auto& venue = static_cast<td::td_api::messageVenue&>(*msg->content_);
    venue.venue_ = td::td_api::make_object<td::td_api::venue>();
    venue.venue_->title_ = "Central Park";
    venue.venue_->address_ = "New York, NY";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[venue] Central Park — New York, NY");
}

// ===========================================================================
// extractMessageTypeAndText – Contact
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Contact) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 17;
    msg->content_ = td::td_api::make_object<td::td_api::messageContact>();
    auto& contact = static_cast<td::td_api::messageContact&>(*msg->content_);
    contact.contact_ = td::td_api::make_object<td::td_api::contact>();
    contact.contact_->first_name_ = "John";
    contact.contact_->last_name_ = "Doe";
    contact.contact_->phone_number_ = "+1234567890";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[contact] John Doe (+1234567890)");
}

// ===========================================================================
// extractMessageTypeAndText – Poll
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Poll) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 18;
    msg->content_ = td::td_api::make_object<td::td_api::messagePoll>();
    auto& poll = static_cast<td::td_api::messagePoll&>(*msg->content_);
    poll.poll_ = td::td_api::make_object<td::td_api::poll>();
    poll.poll_->question_ = td::td_api::make_object<td::td_api::formattedText>();
    poll.poll_->question_->text_ = "Favorite color?";
    auto opt1 = td::td_api::make_object<td::td_api::pollOption>();
    opt1->text_ = td::td_api::make_object<td::td_api::formattedText>();
    opt1->text_->text_ = "Red";
    poll.poll_->options_.push_back(std::move(opt1));
    auto opt2 = td::td_api::make_object<td::td_api::pollOption>();
    opt2->text_ = td::td_api::make_object<td::td_api::formattedText>();
    opt2->text_->text_ = "Blue";
    poll.poll_->options_.push_back(std::move(opt2));

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[poll] Favorite color?\n- Red\n- Blue\n");
}

// ===========================================================================
// extractMessageTypeAndText – Game
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Game) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 19;
    msg->content_ = td::td_api::make_object<td::td_api::messageGame>();
    auto& game = static_cast<td::td_api::messageGame&>(*msg->content_);
    game.game_ = td::td_api::make_object<td::td_api::game>();
    game.game_->title_ = "Super Mario";
    game.game_->description_ = "Jump and run!";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[game] Super Mario — Jump and run!");
}

// ===========================================================================
// extractMessageTypeAndText – Dice
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Dice) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 20;
    msg->content_ = td::td_api::make_object<td::td_api::messageDice>();
    auto& dice = static_cast<td::td_api::messageDice&>(*msg->content_);
    dice.emoji_ = "\xF0\x9F\x8E\xB2"; // 🎲
    dice.value_ = 4;

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_TRUE(result.contains("[dice]"));
}

// ===========================================================================
// extractMessageTypeAndText – Call
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Call) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 21;
    msg->content_ = td::td_api::make_object<td::td_api::messageCall>();
    auto& call = static_cast<td::td_api::messageCall&>(*msg->content_);
    call.is_video_ = false;

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[call] voice call");
}

// ===========================================================================
// extractMessageTypeAndText – Video call
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_VideoCall) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 22;
    msg->content_ = td::td_api::make_object<td::td_api::messageCall>();
    auto& call = static_cast<td::td_api::messageCall&>(*msg->content_);
    call.is_video_ = true;

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[call] video call");
}

// ===========================================================================
// extractMessageTypeAndText – Members added
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_ChatAddMembers) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 23;
    msg->content_ = td::td_api::make_object<td::td_api::messageChatAddMembers>();
    auto& add = static_cast<td::td_api::messageChatAddMembers&>(*msg->content_);
    add.member_user_ids_ = {100, 200, 300};

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[members added] 3 member(s)");
}

// ===========================================================================
// extractMessageTypeAndText – Join by link
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_JoinByLink) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 24;
    msg->content_ = td::td_api::make_object<td::td_api::messageChatJoinByLink>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[joined via link]");
}

// ===========================================================================
// extractMessageTypeAndText – Join by request
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_JoinByRequest) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 25;
    msg->content_ = td::td_api::make_object<td::td_api::messageChatJoinByRequest>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[joined by request]");
}

// ===========================================================================
// extractMessageTypeAndText – Member removed
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_ChatDeleteMember) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 26;
    msg->content_ = td::td_api::make_object<td::td_api::messageChatDeleteMember>();
    auto& del = static_cast<td::td_api::messageChatDeleteMember&>(*msg->content_);
    del.user_id_ = 42;

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[member removed] user_id=42");
}

// ===========================================================================
// extractMessageTypeAndText – Group created
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_BasicGroupCreated) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 27;
    msg->content_ = td::td_api::make_object<td::td_api::messageBasicGroupChatCreate>();
    auto& cg = static_cast<td::td_api::messageBasicGroupChatCreate&>(*msg->content_);
    cg.title_ = "Study Group";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[group created] Study Group");
}

// ===========================================================================
// extractMessageTypeAndText – Supergroup created
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_SupergroupCreated) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 28;
    msg->content_ = td::td_api::make_object<td::td_api::messageSupergroupChatCreate>();
    auto& cg = static_cast<td::td_api::messageSupergroupChatCreate&>(*msg->content_);
    cg.title_ = "Big Channel";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[supergroup created] Big Channel");
}

// ===========================================================================
// extractMessageTypeAndText – Title changed
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_ChatChangeTitle) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 29;
    msg->content_ = td::td_api::make_object<td::td_api::messageChatChangeTitle>();
    auto& ct = static_cast<td::td_api::messageChatChangeTitle&>(*msg->content_);
    ct.title_ = "New Chat Name";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[title changed] New Chat Name");
}

// ===========================================================================
// extractMessageTypeAndText – Chat photo changed
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_ChatChangePhoto) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 30;
    msg->content_ = td::td_api::make_object<td::td_api::messageChatChangePhoto>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[chat photo changed]");
}

// ===========================================================================
// extractMessageTypeAndText – Message pinned
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_PinMessage) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 31;
    msg->content_ = td::td_api::make_object<td::td_api::messagePinMessage>();
    auto& pin = static_cast<td::td_api::messagePinMessage&>(*msg->content_);
    pin.message_id_ = 123;

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[message pinned] message_id=123");
}

// ===========================================================================
// extractMessageTypeAndText – Screenshot taken
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_ScreenshotTaken) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 32;
    msg->content_ = td::td_api::make_object<td::td_api::messageScreenshotTaken>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[screenshot taken]");
}

// ===========================================================================
// extractMessageTypeAndText – Proximity alert
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_ProximityAlert) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 33;
    msg->content_ = td::td_api::make_object<td::td_api::messageProximityAlertTriggered>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[proximity alert]");
}

// ===========================================================================
// extractMessageTypeAndText – Unsupported
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Unsupported) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 34;
    msg->content_ = td::td_api::make_object<td::td_api::messageUnsupported>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[unsupported message]");
}

// ===========================================================================
// extractMessageTypeAndText – Invoice
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_Invoice) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 35;
    msg->content_ = td::td_api::make_object<td::td_api::messageInvoice>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[invoice]");
}

// ===========================================================================
// extractMessageTypeAndText – Chat theme set
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_ChatSetTheme) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 36;
    msg->content_ = td::td_api::make_object<td::td_api::messageChatSetTheme>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[chat theme set] ");
}

// ===========================================================================
// extractMessageTypeAndText – Chat background set
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_ChatSetBackground) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 37;
    msg->content_ = td::td_api::make_object<td::td_api::messageChatSetBackground>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[chat background set]");
}

// ===========================================================================
// extractMessageTypeAndText – Text with malicious payload
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_MaliciousPayload) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 38;
    msg->content_ = td::td_api::make_object<td::td_api::messageText>();
    auto& text = static_cast<td::td_api::messageText&>(*msg->content_);
    text.text_ = td::td_api::make_object<td::td_api::formattedText>();
    text.text_->text_ = std::string(IOpenAIChat::EMBEDDING_TAG);

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "malicious");
}

// ===========================================================================
// extractMessageTypeAndText – Text with link preview
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_LinkPreview) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 39;
    msg->content_ = td::td_api::make_object<td::td_api::messageText>();
    auto& text = static_cast<td::td_api::messageText&>(*msg->content_);
    text.text_ = td::td_api::make_object<td::td_api::formattedText>();
    text.text_->text_ = "Check this out";
    text.link_preview_ = td::td_api::make_object<td::td_api::linkPreview>();
    text.link_preview_->url_ = "https://example.com";
    text.link_preview_->title_ = "Example Site";

    auto result = llmui::extractMessageTypeAndText(*msg);
    // Should contain the text and the link preview string representation
    EXPECT_TRUE(result.contains("Check this out"));
    EXPECT_TRUE(result.contains("https://example.com"));
}

// ===========================================================================
// extractMessageTypeAndText – Document with caption
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_DocumentWithCaption) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 40;
    msg->content_ = td::td_api::make_object<td::td_api::messageDocument>();
    auto& doc = static_cast<td::td_api::messageDocument&>(*msg->content_);
    doc.document_ = td::td_api::make_object<td::td_api::document>();
    doc.document_->file_name_ = "notes.txt";
    doc.caption_ = td::td_api::make_object<td::td_api::formattedText>();
    doc.caption_->text_ = "My notes";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[document] notes.txt\nMy notes");
}

// ===========================================================================
// extractMessageTypeAndText – Animation without caption
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_AnimationNoCaption) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 41;
    msg->content_ = td::td_api::make_object<td::td_api::messageAnimation>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[animation]");
}

// ===========================================================================
// extractMessageTypeAndText – Audio without title
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_AudioNoTitle) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 42;
    msg->content_ = td::td_api::make_object<td::td_api::messageAudio>();
    auto& audio = static_cast<td::td_api::messageAudio&>(*msg->content_);
    audio.audio_ = td::td_api::make_object<td::td_api::audio>();

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "[audio] ");
}

// ===========================================================================
// extractMessageTypeAndText – Empty text message
// ===========================================================================
TEST(TelegramTest, ExtractMessageTypeAndText_EmptyText) {
    auto msg = td::td_api::make_object<td::td_api::message>();
    msg->id_ = 43;
    msg->content_ = td::td_api::make_object<td::td_api::messageText>();
    auto& text = static_cast<td::td_api::messageText&>(*msg->content_);
    text.text_ = td::td_api::make_object<td::td_api::formattedText>();
    text.text_->text_ = "";

    auto result = llmui::extractMessageTypeAndText(*msg);
    EXPECT_EQ(result, "");
}
