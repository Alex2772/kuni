#pragma once
#include "IOpenAIChat.h"
#include "AUI/Common/AString.h"
#include "AUI/Thread/AFuture.h"

namespace web {

struct Result {
    /**
     * @brief Title of the web page
     */
    AString title;

    /**
     * @brief Url of the web page
     */
    AString url;

    /**
     * @brief Relevant content snippet from the web page
     */
    AString content;
};

AFuture<AVector<Result>> search(AString query, int maxResult = 0);
}   // namespace web