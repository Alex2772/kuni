//
// Created by alex2772 on 5/9/26.
//

#include "image.h"

#include "prompts.h"
#include "video.h"
#include "AUI/IO/AFileInputStream.h"
#include "AUI/Util/kAUI.h"

static constexpr auto LOG_TAG = "llmui::image";

AFuture<AString> llmui::image(std::span<const IOpenAIChat::Message> temporaryContext, IOpenAIChat& openAI, AStringView pathToImage, AStringView xmlTag) {
    static const APath CACHE_DIR = APath("cache") / "images";
    AUI_DO_ONCE { CACHE_DIR.makeDirs(); }

    APath cache = CACHE_DIR / "{}.md"_format(APath(pathToImage).filename());

    try
    {
        if (cache.isRegularFileExists()) {
            auto content = AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(cache)));
            co_return "<{} description>\n{}\n</{}>"_format(xmlTag, std::move(content), xmlTag);
        }

        AString context = "<context>\n";
        context += "<character name=\"{}\">\n"_format(config().characterName);
        context += prompts().characterAppearance;
        context += "\n</character name=\"{}\">\n"_format(config().characterName);
        for (const auto& i : temporaryContext) {
            context += "<context_item>\n";
            context += i.content;
            context += "\n</context_item>\n";
        }

        // Route video formats to the video pipeline
        static const AVector<AString> VIDEO_EXTS = {"webm", "mp4", "mkv", "avi", "mov", "m4v", "flv", "3gp"};
        if (VIDEO_EXTS.contains(AString(APath(pathToImage).extension()).lowercase())) {
            co_return co_await llmui::video(temporaryContext, openAI, pathToImage, xmlTag);
        }

        context += "\n\n</context>\n\nPhoto:\n\n";
        auto image = AImage::fromFile(pathToImage);
        if (image == nullptr) {
            co_return "<{} description>\nThis media type is not supported\n</{}>"_format(xmlTag, xmlTag);
        }
        context += IOpenAIChat::embedImage(*image);
        context += "\n\nDescribe the last photo.";

        // hack: ask for shorter descriptions for stickers.
        AString prompt = [&] {
            if (xmlTag.contains("sticker")) {
                return prompts().stickerToText;
            }
            return prompts().photoToText;
        }();
        tryAgain:
        auto response = co_await openAI.chat({
            .systemPrompt = prompt,
            .config = config().llmImageToText,
        }, { { .role = IOpenAIChat::Message::Role::USER, .content = context }});
        auto content = std::move(response.choices.at(0).message.content);
        if (content.trim().empty()) {
            // probably local model overflown its reasoning so we will try again
            goto tryAgain;
        }
        AFileOutputStream(cache) << static_cast<AString&>(content);
        co_return "<{} description>\n{}\n</{}>"_format(xmlTag, std::move(content), xmlTag);
    } catch (const AException& e)
    {
        ALogger::err(LOG_TAG) << "Can't describe photo"  << e;
        co_return "<{} description>\nThis media type is not supported\n</{}>"_format(xmlTag, xmlTag);
    }
}