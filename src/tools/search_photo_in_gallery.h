#pragma once
#include "IStableDiffusionClient.h"
#include "OpenAITools.h"

namespace tools {
OpenAITools::Tool searchPhotoInGallery(_<IOpenAIChat> openAI, const IOpenAIChat::Session& session);
}