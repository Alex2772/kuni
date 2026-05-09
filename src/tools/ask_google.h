#pragma once

#include <OpenAITools.h>
#include <IOpenAIChat.h>

namespace tools {
OpenAITools::Tool askGoogle(_<IOpenAIChat> openAI);
}
