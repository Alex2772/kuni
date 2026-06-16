#pragma once

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "IOpenAIChat.h"

/**
 * @brief Pretty TUI printer for streaming LLM responses.
 *
 * Prints reasoning in dark grey, content in white, tool calls in yellow -
 * incrementally as chunks arrive, like Claude's terminal output.
 *
 * Usage:
 *   TuiStreamingPrinter printer;
 *   printer.update(response);  // call each time response changes
 *   printer.finish();          // call once streaming is done
 */
struct TuiStreamingPrinter {
    // ANSI color codes
    static constexpr const char* RESET  = "\033[0m";
    static constexpr const char* GREY   = "\033[90m";  // dark grey -- reasoning
    static constexpr const char* WHITE  = "\033[97m";  // bright white -- content
    static constexpr const char* YELLOW = "\033[33m";  // yellow -- tool call args
    static constexpr const char* CYAN   = "\033[36m";  // cyan -- tool call name
    static constexpr const char* DIM    = "\033[2m";   // dim -- separators

    TuiStreamingPrinter() {
        std::cout << GREY << "\xe2\x96\xb8 Thinking...\n" << RESET;
        mStartTime = std::chrono::steady_clock::now();
    }

    void update(const IOpenAIChat::Response& response) {
        if (response.choices.empty()) return;
        const auto& msg = response.choices.at(0).message;

        // --- reasoning_content (DeepSeek style) ---
        {
            const std::string rc = msg.reasoning_content.toStdString();
            if (rc.length() > mPrintedReasoningContent) {
                if (mPrintedReasoningContent == 0) {
                    printThinkingTime();
                    std::cout << GREY << "\xe2\x96\xb8 reasoning\n" << RESET;
                    mInReasoning = true;
                }
                std::cout << GREY;
                printDelta(rc, mPrintedReasoningContent);
                std::cout << RESET;
            }
        }

        // --- reasoning (standard field) ---
        {
            const std::string r = msg.reasoning.toStdString();
            if (r.length() > mPrintedReasoning) {
                if (mPrintedReasoning == 0 && !mInReasoning) {
                    printThinkingTime();
                    std::cout << GREY << "\xe2\x96\xb8 reasoning\n" << RESET;
                    mInReasoning = true;
                }
                std::cout << GREY;
                printDelta(r, mPrintedReasoning);
                std::cout << RESET;
            }
        }

        // --- content ---
        {
            const std::string content = msg.content.toStdString();
            if (content.length() > mPrintedContent) {
                if (mPrintedContent == 0) {
                    if (!mInReasoning) printThinkingTime();
                    if (mInReasoning) {
                        // separator between reasoning and answer
                        std::cout << "\n" << DIM
                                  << "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n"
                                  << RESET;
                        mInReasoning = false;
                    }
                }
                std::cout << WHITE;
                printDelta(content, mPrintedContent);
                std::cout << RESET;
            }
        }

        // --- tool calls ---
        for (const auto& tc : msg.tool_calls) {
            const auto idx = static_cast<size_t>(tc.index);
            if (idx >= mToolCallState.size()) {
                mToolCallState.resize(idx + 1);
            }
            auto& state = mToolCallState[idx];

            // print header once name is known
            if (!state.headerPrinted && !tc.function.name.empty()) {
                printThinkingTime();
                std::cout << "\n" << CYAN << "\xe2\x9a\x99 " << tc.function.name.toStdString()
                          << RESET << YELLOW << "(";
                state.headerPrinted = true;
            }

            if (state.headerPrinted) {
                const std::string args = tc.function.arguments.toStdString();
                if (args.length() > state.printedArgs) {
                    std::cout << YELLOW;
                    printDelta(args, state.printedArgs);
                    std::cout << RESET;
                }
            }
        }

        std::cout.flush();
    }

    void finish() {
        // close any open tool call parens
        for (const auto& state : mToolCallState) {
            if (state.headerPrinted) {
                std::cout << YELLOW << ")" << RESET << "\n";
            }
        }
        if (mPrintedContent > 0 || mPrintedReasoning > 0 || mPrintedReasoningContent > 0 || !mToolCallState.empty()) {
            std::cout << "\n";
        }
        std::cout << RESET;
        std::cout.flush();
    }

private:
    size_t mPrintedReasoningContent = 0;
    size_t mPrintedReasoning        = 0;
    size_t mPrintedContent          = 0;
    bool   mInReasoning             = false;
    bool   mThinkingTimePrinted     = false;
    std::chrono::steady_clock::time_point mStartTime;

    struct ToolCallState {
        bool   headerPrinted = false;
        size_t printedArgs   = 0;
    };
    std::vector<ToolCallState> mToolCallState;

    void printThinkingTime() {
        if (mThinkingTimePrinted) return;
        mThinkingTimePrinted = true;
        const auto elapsed = std::chrono::steady_clock::now() - mStartTime;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        std::cout << DIM << "  (" << ms << "ms)" << RESET << "\n";
    }

    // Print only the new suffix of `str` starting from `printed`, update `printed`.
    void printDelta(std::string_view str, size_t& printed) {
        if (str.length() > printed) {
            std::cout << str.substr(printed, str.length() - printed);
            printed = str.length();
        }
    }
};
