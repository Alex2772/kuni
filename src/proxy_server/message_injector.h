#pragma once
#include <AUI/Json/AJson.h>
#include <AUI/Common/AVector.h>
#include <AUI/Common/AMap.h>
#include <AUI/Common/AString.h>
#include "IOpenAIChat.h"

namespace proxy_server {

/**
 * @brief Stores hidden messages (tool_call + tool results) associated with an
 *        assistant turn, keyed by that turn's visible content.
 *
 * When the proxy intercepts a tool call and resolves it internally, it records
 * the hidden messages here.  On the next client request the proxy calls
 * merge() to transparently splice them back into the message array before
 * forwarding to the LLM, so the LLM always sees a consistent history.
 *
 * Key = content of the assistant message that the client received after the
 *       hidden tool-call round-trip completed.  This string is stable: the
 *       client echoes it back verbatim in subsequent requests.
 */
class MessageInjector {
public:

    /**
     * @brief Returns an array of messages that should be inserted after `msg`.
     * @param msg JSON object that represents a message
     * @details
     * Users can modify it to insert new hidden messages after `msg`.
     */
    AVector<AJson>& after(const AJson& msg);

    /**
     * @brief Insert stored hidden messages into`messages`.
     * @param messages JSON array of messages to insert in.
     * @return New message array with all applicable hidden messages spliced in.
     */
    [[nodiscard]] AJson::Array merge(AJson::Array messages) const;

private:
    // key: content of the visible assistant message
    std::map<AString /* fingerprint */, AVector<AJson>> mMessagesToInsertAfter;
};

} // namespace proxy_server

