#pragma once
#include "IStableDiffusionClient.h"
#include "OpenAITools.h"

namespace tools {
OpenAITools::Tool takePhoto(_<IStableDiffusionClient> stableDiffusion, _<IOpenAIChat> openAI);
}