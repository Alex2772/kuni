#pragma once

#include <AUI/Common/AString.h>
#include <range/v3/view/filter.hpp>
#include <range/v3/to_container.hpp>

namespace proxy_server {

/**
 * @brief Makes a fingerprint of a JSON object (message or array of messages) in a form of string.
 * @param object object to generate a fingerprint from.
 * @details
 * This fingerprint can be used with AString::startsWith, which allows to heuristically determine if an array starts
 * with another array (used to determine if a request is a continuation of a dialog or starts a new dialog,
 * ContextBridge).
 *
 * Additionally, this function is used to capture fingerprints of a messages to inject after (MessageInjector).
 */
static AString fingerprint(const AJson& object) {
    auto result = AJson::toString(object);
    return result | ranges::views::filter([](char c) {
               switch (c) {
                   case '(':
                   case ')':
                   case '{':
                   case '}':
                   case '[':
                   case ']':
                       return false;
                   default:
                       return true;
               }
           }) |
           ranges::to<AString>();
}
}