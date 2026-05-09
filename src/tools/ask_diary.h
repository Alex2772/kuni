#pragma once

#include <Diary.h>
#include <OpenAITools.h>

namespace tools {
OpenAITools::Tool askDiary(const AVector<IOpenAIChat::Message>& temporaryContext, Diary& diary);
}