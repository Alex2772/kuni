#pragma once

#include "AUI/Common/AObject.h"
#include "AUI/Thread/AAsyncHolder.h"
#include "AUI/Thread/AEventLoop.h"
#include "AUI/Util/kAUI.h"
#include "OpenAIChat.h"
#include "util/cosine_similarity.h"

#include <chrono>
#include <filesystem>
#include <list>
#include <string>
#include <valarray>
#include <vector>

/**
 * @brief Represents a diary that stores entries as markdown files.
 *
 * The diary is backed by a directory on disk. Each entry is stored in a
 * separate markdown file named `<id>.md`. The file may optionally contain a
 * JSON metadata block surrounded by `---` delimiters. The metadata
 * block is serialized to JSON and stored in the `EntryEx::Metadata` struct.
 *
 * The class provides synchronous read/write operations as well as
 * asynchronous query capabilities that compute similarity between a query
 * vector and the embeddings stored in the metadata.
 */
class Diary {
public:
    /**
     * @brief Simple representation of a diary entry.
     *
     * The struct holds the identifier and raw markdown text of an
     * entry.  It is used by the synchronous read/write API.
     */
    struct Entry {
        /**
         * @brief Identifier of the entry.
         *
         * The identifier is the file name without the `.md` extension.
         * It is used as the key for reading and writing the entry.
         */
        AString id;

        /**
         * @brief Raw markdown content of the entry.
         *
         * The content may include an optional metadata block at the
         * beginning of the file.  The block is surrounded by `---`
         * delimiters and contains JSON that is deserialized into
         * {@link EntryEx::Metadata}.
         */
        AString text;
    };

    /**
     * @brief Extended representation of a diary entry.
     *
     * In addition to the raw text, this struct contains parsed
     * metadata and a free‑form body.  It is used by the asynchronous
     * query API and for persisting metadata.
     */
    struct EntryEx {
        /**
         * @brief Identifier of the entry.
         */
        AString id;
        
        /**
         * @brief Metadata associated with the entry.
         *
         * The metadata is stored in the YAML‑style block at the top of the
         * markdown file.  It is serialized to JSON for persistence.
         */
        struct Metadata {
            /**
             * @brief Similarity score used for ranking queries.
             */
            float score = 0.f;

            /**
             * @brief Confidence factor, ∈ {-1..1}: -1 lie, 0 theory/default, 1 ground truth (immutable by sleep).
             */
            float confidence = 0.f;

            /**
             * @brief Human‑readable timestamp of the last access.
             */
            AString lastUsed = "never";

            /**
             * @brief Number of times the entry has been accessed.
             */
            int usageCount = 0;

            /**
             * @brief Embedding vector used for similarity calculations.
             */
            std::valarray<double> embedding;
        } metadata;

        /**
         * @brief Body of the entry without the metadata block.
         */
        AString freeformBody;

        /**
         * @brief Increment the usage counter and update the last used time.
         */
        void incrementUsageCount() {
            metadata.usageCount++;
            metadata.lastUsed = "{}"_format(std::chrono::system_clock::now());
        }
    };

    struct EntryExAndRelatedness {
        /**
         * @brief Iterator pointing to the entry in the cached diary.
         */
        std::list<EntryEx>::iterator entry;

        /**
         * @brief Normalized similarity score around 0 = unrelated and 1 = copypasta.
         */
        double relatedness{};

        auto operator<=>(const EntryExAndRelatedness&) const = default;
    };
    
    /**
     * @brief Construct a new Diary object.
     *
     * @param diaryDir Path to the directory that stores the markdown
     *                 files.  The directory is created if it does not
     *                 already exist.
     */
    Diary(APath diaryDir);

    /**
     * @brief Persist a simple entry to disk.
     *
     * The entry is written to a file named `<id>.md`.
     */
    void save(const Entry& entry);

    /**
     * @brief Persist an extended entry with metadata.
     *
     * The metadata block is serialized to JSON and written before the
     * freeform body, surrounded by `---` delimiters.
     */
    void save(const EntryEx& entry);

    /**
     * @brief Remove an entry from the in‑memory cache.
     *
     * The entry is first written back to disk and then erased from the
     * cached list.
     */
    void unload(std::list<EntryEx>::const_iterator it);

    struct QueryOpts {
        aui::float_within_0_1 confidenceFactor = 0.01f;
        size_t maxEntryCount = 10;
        std::function<bool(const EntryEx&)> filter = [](const EntryEx&) { return true; };
    };

    /**
     * @brief Asynchronously query the diary for entries related to a
     *        vector of embeddings.
     *
     * The method computes the cosine similarity between the query
     * vector and each entry's embedding, normalizes it to the range
     * [0,1], and returns a sorted vector of {@link EntryExAndRelatedness}
     * objects.
     */
    AFuture<AVector<EntryExAndRelatedness>> query(const std::valarray<double>& query, QueryOpts opts);

    /**
     * @brief Compute the relatedness of a single entry to a context vector.
     *
     * If the entry does not yet have an embedding, it is generated via
     * {@link OpenAIChat::embedding} and persisted.
     */
    AFuture<double> entryIsRelated(const std::valarray<double>& context, EntryEx& entry, QueryOpts opts);

    /**
     * @brief Parse raw markdown entries into extended entries.
     *
     * The function extracts the optional metadata block and converts it
     * into {@link EntryEx::Metadata}.  The remaining text becomes the
     * freeform body.
     */
    static std::list<EntryEx> parse(AVector<Entry> diary);

    /**
     * @brief Retrieve the cached list of extended entries.
     *
     * The list is lazily initialized on first access.
     */
    [[nodiscard]]
    const std::list<EntryEx>& list() const {
        return *mCachedDiary;
    }

    /**
     * @brief Invalidate the cached diary so that it will be reloaded on
     *        next access.
     */
    void reload() { ALOG_TRACE("Diary") << "reload"; mCachedDiary.reset(); }


    /**
     * @brief Performs "maintenance" of the diary contents.
     * @details
     * Like person's sleeping
     */
    AFuture<> sleepingConsolidation();

    AFuture<AString> queryAI(const AString& query, QueryOpts opts);

private:
    /**
     * @brief Path to the directory containing the markdown files.
     */

    APath mDiaryDir;
    /**
     * @brief Holds asynchronous tasks for the diary.
     */

    AAsyncHolder mAsync;
    /**
     * @brief Lazily cached list of parsed diary entries.
     */
    aui::lazy<std::list<EntryEx>> mCachedDiary = [this] { return parse(read(mDiaryDir)); };


    /**
     * @brief Read all markdown files from the diary directory.
     *
     * The method scans the directory for files with the `.md` extension
     * and returns a vector of {@link Entry} objects containing the file
     * name (without extension) and the raw file content.
     */
    static AVector<Entry> read(const APath& path);
};

AJSON_FIELDS(Diary::EntryEx::Metadata, AJSON_FIELDS_ENTRY(score) (confidence, "confidence", AJsonFieldFlags::OPTIONAL) AJSON_FIELDS_ENTRY(lastUsed)
                                           AJSON_FIELDS_ENTRY(usageCount) AJSON_FIELDS_ENTRY(embedding))
