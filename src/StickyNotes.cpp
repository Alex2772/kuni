//
// Created by alex2772 on 8/3/26.
//

#include "StickyNotes.h"

#include "AUI/IO/AFileInputStream.h"
#include "AUI/IO/AFileOutputStream.h"
#include "AUI/Json/AJson.h"
#include "AUI/Logging/ALogger.h"
#include "util/json_utils.h"
#include "util/time_ago.h"

#include <range/v3/algorithm/find_if.hpp>

static constexpr auto LOG_TAG = "StickyNotes";
static const auto STICKY_NOTES_PATH = "sticky_notes.json";

template<>
struct AJsonConv<std::chrono::system_clock::time_point> {
    static AJson toJson(std::chrono::system_clock::time_point v) {
        return static_cast<int64_t>(std::chrono::system_clock::to_time_t(v));
    }

    static void fromJson(const AJson& json, std::chrono::system_clock::time_point& out) {
        out = std::chrono::system_clock::from_time_t(static_cast<time_t>(util::jsonAsLongInt(json).valueOr(0)));
    }
};

AJSON_FIELDS(StickyNotes::Entry,
             AJSON_FIELDS_ENTRY(id)
             AJSON_FIELDS_ENTRY(text)
             AJSON_FIELDS_ENTRY(lastUpdateAt))

AJSON_FIELDS(StickyNotes::StateSlot,
             AJSON_FIELDS_ENTRY(text)
             AJSON_FIELDS_ENTRY(lastUpdateAt))

StickyNotes::StickyNotes(Init init): mPath(std::move(init.workingDir) / STICKY_NOTES_PATH) {
    load();
}

void StickyNotes::load() {
    if (!mPath.isRegularFileExists()) {
        return;
    }
    try {
        auto json = AJson::fromStream(AFileInputStream(mPath));
        mEntries = aui::from_json<AVector<Entry>>(json["entries"]);
        for (const auto& entry : mEntries) {
            mNextId = std::max(mNextId, entry.id + 1);
        }
        if (json.contains("emotional_state")) {
            mEmotionalState = aui::from_json<StateSlot>(json["emotional_state"]);
        }
        if (json.contains("physical_state")) {
            mPhysicalState = aui::from_json<StateSlot>(json["physical_state"]);
        }
    } catch (const AException& e) {
        ALogger::warn(LOG_TAG) << "Failed to load " << mPath << ": " << e;
    }
}

void StickyNotes::save() const {
    AJson::Object root;
    root["entries"] = aui::to_json(mEntries);
    root["emotional_state"] = aui::to_json(mEmotionalState);
    root["physical_state"] = aui::to_json(mPhysicalState);
    AFileOutputStream(mPath) << AJson::toString(root);
}

AString StickyNotes::readMemory() const {
    AString out;
    for (const auto& entry : mEntries) {
        out += "- [id={}] {}"_format(entry.id, entry.text);
        out += " — last updated: {}\n"_format(util::timeAgo(entry.lastUpdateAt));
    }
    if (!mEmotionalState.text.empty()) {
        out += "Emotional state: {} — last updated: {}\n"_format(mEmotionalState.text, util::timeAgo(mEmotionalState.lastUpdateAt));
    }
    if (!mPhysicalState.text.empty()) {
        out += "Physical state: {} — last updated: {}\n"_format(mPhysicalState.text, util::timeAgo(mPhysicalState.lastUpdateAt));
    }
    return out;
}

AOptional<StickyNotes::Entry> StickyNotes::add(AString text) {
    if (mEntries.size() >= MAX_ENTRIES) {
        return std::nullopt;
    }
    Entry entry {
        .id = mNextId++,
        .text = std::move(text),
        .lastUpdateAt = std::chrono::system_clock::now(),
    };
    mEntries << entry;
    save();
    return entry;
}

bool StickyNotes::update(int id, AOptional<AString> text) {
    auto it = ranges::find_if(mEntries, [&](const Entry& e) { return e.id == id; });
    if (it == mEntries.end()) {
        return false;
    }
    if (text) {
        it->text = std::move(*text);
    }
    it->lastUpdateAt = std::chrono::system_clock::now();
    save();
    return true;
}

bool StickyNotes::markDone(int id) {
    auto it = ranges::find_if(mEntries, [&](const Entry& e) { return e.id == id; });
    if (it == mEntries.end()) {
        return false;
    }
    mEntries.erase(it);
    save();
    return true;
}

void StickyNotes::setEmotionalState(AString text) {
    mEmotionalState = StateSlot {
        .text = std::move(text),
        .lastUpdateAt = std::chrono::system_clock::now(),
    };
    save();
}

void StickyNotes::setPhysicalState(AString text) {
    mPhysicalState = StateSlot {
        .text = std::move(text),
        .lastUpdateAt = std::chrono::system_clock::now(),
    };
    save();
}

void StickyNotes::updateTools(OpenAITools& actions) {
    if (!actions.handlers().contains("sticky_note_add")) {
        actions.insert({
            .name = "sticky_note_add",
            .description = "Adds a new sticky note (\"middle\" memory) - tasks, promises, reminders that "
                            "matter for the next few days. Limited to {} items total; mark old ones done via "
                            "#sticky_note_mark_done to free up space."_format(MAX_ENTRIES),
            .parameters = {
                .properties = {
                    {"text", {.type = "string", .description = "Freeform text of the item to remember."}},
                },
                .required = {"text"},
            },
            .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                auto text = ctx.args["text"].asStringOpt().valueOrException("text is required string");
                auto entry = add(std::move(text));
                if (!entry) {
                    co_return "Sticky notes are full ({} items). Mark something done via #sticky_note_mark_done first."_format(MAX_ENTRIES);
                }
                co_return "Added sticky note with id={}."_format(entry->id);
            },
        });
    }
    if (!actions.handlers().contains("sticky_note_update")) {
        actions.insert({
            .name = "sticky_note_update",
            .description = "Updates an existing sticky note's text by id.",
            .parameters = {
                .properties = {
                    {"id", {.type = "integer", .description = "id of the item to update."}},
                    {"text", {.type = "string", .description = "New text. Leave unset (null) to keep unchanged.", .nullable = true}},
                },
                .required = {"id", "text"},
            },
            .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                auto id = static_cast<int>(util::jsonAsLongInt(ctx.args["id"]).valueOrException("id is required integer"));
                AOptional<AString> text;
                if (auto textRaw = ctx.args["text"]; !textRaw.isEmpty()) {
                    text = textRaw.asStringOpt();
                }
                if (!update(id, std::move(text))) {
                    co_return "No such sticky note with id={}."_format(id);
                }
                co_return "Updated sticky note {}."_format(id);
            },
        });
    }
    if (!actions.handlers().contains("sticky_note_mark_done")) {
        actions.insert({
            .name = "sticky_note_mark_done",
            .description = "Marks a sticky note as done, removing it from your active reminders list.",
            .parameters = {
                .properties = {
                    {"id", {.type = "integer", .description = "id of the item to mark as done."}},
                },
                .required = {"id"},
            },
            .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                auto id = static_cast<int>(util::jsonAsLongInt(ctx.args["id"]).valueOrException("id is required integer"));
                if (!markDone(id)) {
                    co_return "No such sticky note with id={}."_format(id);
                }
                co_return "Marked sticky note {} as done."_format(id);
            },
        });
    }
    if (!actions.handlers().contains("set_emotional_state")) {
        actions.insert({
            .name = "set_emotional_state",
            .description = "Sets your current emotional state (e.g. anger, amused, sad, joy, love, annoyed, ...). "
                            "This is persisted and injected back into your context, highly influencing your behaviour.",
            .parameters = {
                .properties = {
                    {"emotional_state", {.type = "string", .description = "Freeform emotional state, e.g. \"anger\"."}},
                },
                .required = {"emotional_state"},
            },
            .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                auto text = ctx.args["emotional_state"].asStringOpt().valueOrException("emotional_state is required string");
                setEmotionalState(std::move(text));
                co_return "Emotional state updated.";
            },
        });
    }
    if (!actions.handlers().contains("set_physical_state")) {
        actions.insert({
            .name = "set_physical_state",
            .description = "Sets your current physical state (e.g. tired, energetic, sleepy, hungry, ...). "
                            "This is persisted and injected back into your context, highly influencing your behaviour.",
            .parameters = {
                .properties = {
                    {"physical_state", {.type = "string", .description = "Freeform physical state, e.g. \"tired\"."}},
                },
                .required = {"physical_state"},
            },
            .handler = [this](OpenAITools::Ctx ctx) -> AFuture<AString> {
                auto text = ctx.args["physical_state"].asStringOpt().valueOrException("physical_state is required string");
                setPhysicalState(std::move(text));
                co_return "Physical state updated.";
            },
        });
    }
}
