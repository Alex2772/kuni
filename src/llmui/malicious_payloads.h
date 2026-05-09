#pragma once

#include <string>


namespace llmui {
/**
 * @brief Checks the input string for attempts of malicious actions.
 * @details
 * All strings that come to LLM from outside (i.e., message contents, user names and everything else) must be
 * checked first.
 *
 * If a violation is caused, the string is replaced with "malicious".
 */
void checkForMaliciousPayloads(std::string& string);
}   // namespace llmui