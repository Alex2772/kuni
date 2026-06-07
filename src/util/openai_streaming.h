#pragma once

#include <functional>
#include "AUI/Util/AYieldGenerator.h"

namespace util::openai_streaming {

AYieldGenerator<std::string_view> lineByLine(std::function<size_t(char* dst, size_t size)> read);

}
