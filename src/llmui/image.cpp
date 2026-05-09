//
// Created by alex2772 on 5/9/26.
//

#include "image.h"

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
            co_return AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(cache)));
        }

        AString context = "<context>\n";
        for (const auto& i : temporaryContext) {
            context += "<context_item>\n";
            context += i.content;
            context += "\n</context_item>\n";
        }

        context += "\n\n</context>\n\nPhoto:\n\n";
        auto image = AImage::fromFile(pathToImage);
        if (image == nullptr) {
            co_return "<{} description>\nThis media type is not supported\n</{}>"_format(xmlTag, xmlTag);
        }
        context += IOpenAIChat::embedImage(*image);
        context += "\n\nDescribe the last photo.";

        auto response = co_await openAI.chat({
            .systemPrompt = config::PHOTO_TO_TEXT_PROMPT,
            .config = config::ENDPOINT_PHOTO_TO_TEXT,
            .seed = 1,
        }, { { .role = IOpenAIChat::Message::Role::USER, .content = std::move(context) }});
        auto out = "<{} description>\n{}\n</{}>"_format(xmlTag, response.choices.at(0).message.content, xmlTag);
        AFileOutputStream(cache) << out;
        co_return out;
    } catch (const AException& e)
    {
        ALogger::err(LOG_TAG) << "Can't describe photo"  << e;
        co_return "<{} description>\nThis media type is not supported\n</{}>"_format(xmlTag, xmlTag);
    }
}