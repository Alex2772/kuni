//
// Created by alex2772 on 5/9/26.
//

#include "tools/get_chat_photo.h"
#include "../common.h"
#include "IOpenAIChat.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "util/await_synchronously.h"

#include <gmock/gmock.h>

namespace {
// ---------------------------------------------------------------------------
// Mock ITelegramClient
// ---------------------------------------------------------------------------
class TelegramMock : public ITelegramClient {
public:
    MOCK_METHOD(AFuture<Object>, sendQuery, (td::td_api::object_ptr<td::td_api::Function> f), (override));
    MOCK_METHOD(const AFuture<>&, waitForConnection, (), (const, noexcept, override));
    MOCK_METHOD(int64_t, myId, (), (const, override));
    AFuture<> ready;

    TelegramMock() {
        ready.supplyValue();
        ON_CALL(*this, waitForConnection).WillByDefault([this]() -> const AFuture<>& {
            return ready;
        });
        ON_CALL(*this, myId).WillByDefault(testing::Return(0));
    }
};

// ---------------------------------------------------------------------------
// Mock IOpenAIChat
// ---------------------------------------------------------------------------
class OpenAIMock : public IOpenAIChat {
public:
    MOCK_METHOD(_<StreamingResponse>, chatStreaming, (Params params, IOpenAIChat::Session messages), (override));
    MOCK_METHOD(AFuture<std::valarray<double>>, embedding, (Params params, AString input), (override));
};
}

// ---------------------------------------------------------------------------
// Helper: create mock chat objects
// ---------------------------------------------------------------------------
static _<td::td_api::chat> makeChatWithPhoto(int64_t id, const AString& title, int32_t fileId) {
    auto chat = _new<td::td_api::chat>();
    chat->id_ = id;
    chat->title_ = title.toStdString();
    chat->type_ = td::td_api::make_object<td::td_api::chatTypePrivate>();

    auto photo = td::td_api::make_object<td::td_api::chatPhotoInfo>();
    auto bigFile = td::td_api::make_object<td::td_api::file>();
    bigFile->id_ = fileId;
    bigFile->size_ = 1024;
    bigFile->expected_size_ = 1024;
    bigFile->local_ = td::td_api::make_object<td::td_api::localFile>();
    bigFile->local_->is_downloading_completed_ = false;
    bigFile->local_->path_ = "";
    photo->big_ = std::move(bigFile);

    auto smallFile = td::td_api::make_object<td::td_api::file>();
    smallFile->id_ = fileId + 1;
    smallFile->local_ = td::td_api::make_object<td::td_api::localFile>();
    smallFile->local_->is_downloading_completed_ = true;
    smallFile->local_->path_ = "/tmp/dummy_small.png";
    photo->small_ = std::move(smallFile);

    chat->photo_ = std::move(photo);
    return chat;
}

static _<td::td_api::chat> makeChatWithoutPhoto(int64_t id, const AString& title) {
    auto chat = _new<td::td_api::chat>();
    chat->id_ = id;
    chat->title_ = title.toStdString();
    chat->type_ = td::td_api::make_object<td::td_api::chatTypePrivate>();
    chat->photo_ = nullptr;
    return chat;
}

// ===========================================================================
// getChatPhoto – No photo (chat->photo_ is nullptr)
// ===========================================================================
TEST(GetChatPhotoTest, NoPhoto) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChatWithoutPhoto(12345, "No Photo Chat");

    EXPECT_CALL(*telegram, sendQuery(testing::_)).Times(0);
    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_)).Times(0);

    IOpenAIChat::Session temporaryContext;
    auto tool = tools::getChatPhoto(std::move(telegram), std::move(openAI), std::move(chat), temporaryContext);

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("has no photo")) << "result = " << result;
    EXPECT_TRUE(result.contains("No Photo Chat")) << "result = " << result;
}

// ===========================================================================
// getChatPhoto – Has photo, download succeeds, image description succeeds
// ===========================================================================
TEST(GetChatPhotoTest, HasPhotoSuccess) {
    auto telegram = _new<TelegramMock>();
    auto openAI = _new<OpenAIMock>();
    auto chat = makeChatWithPhoto(12345, "Photo Chat", 999);

    // Use the existing test image
    auto dummyImagePath = TEST_DATA / "sussybaka.jpg";

    // Expect downloadFile query → return a file with local path set
    EXPECT_CALL(*telegram, sendQuery(testing::_))
        .WillOnce([&](td::td_api::object_ptr<td::td_api::Function> f) -> AFuture<ITelegramClient::Object> {
            EXPECT_EQ(f->get_id(), td::td_api::downloadFile::ID);
            auto* dl = static_cast<td::td_api::downloadFile*>(f.get());
            EXPECT_EQ(dl->file_id_, 999);

            auto file = td::td_api::make_object<td::td_api::file>();
            file->id_ = 999;
            file->local_ = td::td_api::make_object<td::td_api::localFile>();
            file->local_->is_downloading_completed_ = true;
            file->local_->path_ = dummyImagePath.toStdString();
            co_return file;
        });

    // Expect chat call for image description
    IOpenAIChat::Message fakeMsg;
    fakeMsg.role = IOpenAIChat::Message::Role::ASSISTANT;
    fakeMsg.content = "A profile photo with a cute cat.";

    auto fakeStreaming = _new<IOpenAIChat::StreamingResponse>();
    fakeStreaming->response.raw = IOpenAIChat::Response{
        .choices = {
            IOpenAIChat::Response::Choice{
                .index = 0,
                .message = std::move(fakeMsg),
                .finish_reason = "stop",
            },
        },
        .usage = { .prompt_tokens = 10, .completion_tokens = 5, .total_tokens = 15 },
    };
    fakeStreaming->completed.supplyValue();

    EXPECT_CALL(*openAI, chatStreaming(testing::_, testing::_))
        .Times(1)
        .WillOnce(testing::Return(std::move(fakeStreaming)));

    IOpenAIChat::Session temporaryContext;
    auto tool = tools::getChatPhoto(std::move(telegram), std::move(openAI), std::move(chat), temporaryContext);

    OpenAITools tools{};
    auto result = util::await_synchronously(tool.handler({
        .tools = tools,
        .args = AJson::Object{},
        .allToolCalls = {},
    }));

    EXPECT_TRUE(result.contains("Photo Chat")) << "result = " << result;
    EXPECT_TRUE(result.contains("A profile photo with a cute cat.")) << "result = " << result;
    EXPECT_TRUE(result.contains("avatar")) << "result = " << result;

    // Cleanup cache file created by llmui::image
    APath cacheFile = APath("cache") / "images" / "sussybaka.jpg.md";
    if (cacheFile.isRegularFileExists()) {
        cacheFile.removeFile();
    }
}
