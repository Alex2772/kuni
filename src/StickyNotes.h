#pragma once

#include "AUI/Common/AOptional.h"
#include "AUI/Common/AString.h"
#include "AUI/Common/AVector.h"
#include "AUI/IO/APath.h"
#include "OpenAITools.h"

#include <chrono>

/**
 * @brief "Middle" (working) memory - a small persistent to-do/reminder board the LLM can manipulate directly via
 * tools, replacing the old LLM-summarized `working_memory.md` approach.
 * @details
 * See README.md, section "Working memory" for the reasoning behind this "middle" layer of memory.
 *
 * Unlike the old approach (where an LLM call re-wrote the entire working memory blob on every diary dump),
 * StickyNotes is a simple, explicit CRUD-like store that the LLM manages itself via tools (#sticky_note_add,
 * #sticky_note_update, #sticky_note_mark_done). This is cheaper (no extra LLM call needed) and less lossy (no
 * re-summarization drift).
 *
 * Entries are persisted to a single JSON file (`sticky_notes.json`) inside the app's working directory, and are
 * capped at #MAX_ENTRIES items - once the limit is reached, #add fails asking the LLM to mark something as done
 * or update an existing entry instead.
 */
class StickyNotes {
public:
    /**
     * @brief Hard cap on the number of entries kept (done or not).
     */
    static constexpr size_t MAX_ENTRIES = 50;

    struct Entry {
        int id = 0;
        AString text;

        /**
         * @brief When the entry was last modified (created, text change).
         */
        std::chrono::system_clock::time_point lastUpdateAt = std::chrono::system_clock::now();
    };

    /**
     * @brief A single-value slot (as opposed to #Entry, of which there can be many) with a "last updated" timestamp,
     * used for #mEmotionalState and #mPhysicalState.
     */
    struct StateSlot {
        AString text;
        std::chrono::system_clock::time_point lastUpdateAt = std::chrono::system_clock::now();
    };

    struct Init {
        /**
         * @brief Directory the sticky_notes.json file is stored in.
         */
        APath workingDir = "test_data";
    };

    StickyNotes(Init init);

    /**
     * @brief Formats the current sticky notes state for injection into the end of the system prompt.
     * @details
     * Returns an empty string if there are no entries.
     */
    [[nodiscard]] AString readMemory() const;

    /**
     * @brief Registers #sticky_note_add, #sticky_note_update, #sticky_note_mark_done, #set_emotional_state and
     * #set_physical_state tools.
     */
    void updateTools(OpenAITools& actions);

    /**
     * @brief Adds a new entry. Fails (returns nullopt) if #MAX_ENTRIES is reached.
     */
    AOptional<Entry> add(AString text);

    /**
     * @brief Updates an existing entry's text by id.
     */
    bool update(int id, AOptional<AString> text);

    /**
     * @brief Marks an entry as done by removing it entirely from the board.
     */
    bool markDone(int id);

    [[nodiscard]] const AVector<Entry>& entries() const { return mEntries; }

    [[nodiscard]] const StateSlot& emotionalState() const { return mEmotionalState; }
    [[nodiscard]] const StateSlot& physicalState() const { return mPhysicalState; }

    /**
     * @brief Sets Kuni's current emotional state (e.g. "anger", "amused") and refreshes its "last updated" timestamp.
     */
    void setEmotionalState(AString text);

    /**
     * @brief Sets Kuni's current physical state (e.g. "tired", "energetic") and refreshes its "last updated" timestamp.
     */
    void setPhysicalState(AString text);

private:
    APath mPath;
    AVector<Entry> mEntries;
    int mNextId = 1;

    /**
     * @brief Kuni's current emotional state (e.g. "anger", "amused"), settable by the LLM via #set_emotional_state.
     */
    StateSlot mEmotionalState;

    /**
     * @brief Kuni's current physical state (e.g. "tired", "energetic"), settable by the LLM via #set_physical_state.
     */
    StateSlot mPhysicalState;

    void load();
    void save() const;
};
