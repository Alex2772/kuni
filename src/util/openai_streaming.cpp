#include "openai_streaming.h"

#include "AUI/Logging/ALogger.h"
#include <range/v3/all.hpp>

AYieldGenerator<std::string_view>
util::openai_streaming::lineByLine(std::function<size_t(char* dst, size_t size)> read) {
    std::vector<char> buffer;
    buffer.resize(0x100);
    auto endOfValidData = ranges::begin(buffer);
    for (;;) {
        if (endOfValidData == ranges::end(buffer)) {
            // increase the buffer 2 times.
            endOfValidData = buffer.insert(endOfValidData, buffer.size(), 0);
        }
        auto readBytes = read(endOfValidData.base(), std::distance(endOfValidData, ranges::end(buffer)));

        ALOG_TRACE("lineByLine") << std::string_view(endOfValidData.base(), std::distance(endOfValidData, ranges::end(buffer)));
        endOfValidData += readBytes;

        auto validDataSpan = std::string_view(buffer.begin(), endOfValidData);
        size_t cursor = 0;
        for (;;) {
            auto lineEnd = validDataSpan.find("\n\n", cursor);
            if (lineEnd == std::string_view::npos) {
                break;
            }
            co_yield validDataSpan.substr(cursor, lineEnd - cursor);
            // + 2: consume \n\n
            cursor = lineEnd + 2;
        }
        {
            auto endOfValidDataIdx = std::distance(buffer.begin(), endOfValidData);
            buffer.erase(buffer.begin(), buffer.begin() + cursor);
            buffer.resize(buffer.capacity()); // keep the buffer at capacity size.
            endOfValidData = buffer.begin() + endOfValidDataIdx - cursor;
        }

        if (readBytes == 0) {
            break;
        }
    }
}
