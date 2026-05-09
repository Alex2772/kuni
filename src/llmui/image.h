#pragma once
#include "IOpenAIChat.h"
#include "AUI/Thread/AFuture.h"

namespace llmui {

/**
 * @brief Loads image specified at pathToImage, and converts it to textual representation using IMG2TEXT endpoint.
 * @param temporaryContext additional context to provide to the model.
 * @param pathToImage path to image to describe.
 * @param xmlTag xml tag to use.
 * @return textural representation of image.
 */
AFuture<AString> image(std::span<const IOpenAIChat::Message> temporaryContext, IOpenAIChat& openAI, AStringView pathToImage, AStringView xmlTag = "photo");
}