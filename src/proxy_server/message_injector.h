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
    using Messages = AVector<IOpenAIChat::Message>;

    /**
     * @brief Record hidden messages that follow a visible assistant turn.
     *
     * @param visibleAssistantContent  The content of the assistant message as
     *                                 seen by the client (used as lookup key).
     * @param hiddenMessages           The messages to insert right before the
     *                                 visible assistant message:
     *                                 [assistant{tool_calls}, tool{result}, ...].
     */
    void store(const AString& visibleAssistantContent, Messages hiddenMessages);

    /**
     * @brief Splice stored hidden messages into @p messages.
     *
     * Scans @p messages for assistant turns whose content matches a stored key
     * and inserts the corresponding hidden messages immediately before each
     * such assistant turn (so the LLM sees: ... hidden_tool_call, tool_result,
     * assistant_final_answer ...).
     *
     * Each stored entry is applied at most once (first match wins).
     *
     * @return New message array with all applicable hidden messages spliced in.
     */
    [[nodiscard]] Messages merge(Messages messages) const;

private:
    // key: content of the visible assistant message
    AMap<AString, Messages> mStore;
};

} // namespace proxy_server

