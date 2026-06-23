//
// Created by alex2772 on 5/9/26.
//

#include "get_chat_photo.h"

#include "llmui/image.h"
#include "llmui/telegram.h"

OpenAITools::Tool tools::getChatPhoto(_<ITelegramClient> telegram,
                                      _<IOpenAIChat> openAI,
                                      _<td::td_api::chat> chat,
                                      const IOpenAIChat::Session& temporaryContext) {
    return {
        .name = "get_chat_photo",
        .description = "Retrieves photo of \"{}\" chat. Use this to get basic idea of a person or group chat "
                       "based on their profile photo."_format(chat->title_),
        .handler = [telegram = std::move(telegram),
                    openAI = std::move(openAI),
                    chat = std::move(chat),
                    &temporaryContext](OpenAITools::Ctx ctx) -> AFuture<AString> {
            // just a freestanding function. sometimes LLM decides to check person's photo without an
            // instruction!
            if (chat->photo_ == nullptr) {
                co_return "<chat_photo chat_name=\"{}\">Chat \"{}\" has no photo.</chat_photo>"_format(chat->title_, chat->title_);
            }
            auto image = co_await llmui::image(temporaryContext, *openAI, co_await llmui::fetchMedia(*telegram, chat->photo_->big_));
            co_return "<chat_photo chat_name=\"{}\">{}</chat_photo>\nThis is avatar photo of \"{}\". When referring to it, let the person know that you are referring to their avatar."_format(chat->title_, image, chat->title_);
        },
    };
}
