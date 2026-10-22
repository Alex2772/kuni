//
// Created by alex2772 on 5/9/26.
//

#include "search_photo_in_gallery.h"

#include "ImageGenerator.h"
#include "AUI/IO/AFileInputStream.h"
#include "AUI/Util/kAUI.h"
#include "llmui/image.h"
#include "util/cosine_similarity.h"

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/view/take.hpp>

namespace {
struct GalleryEntry {
    AString description;
    std::valarray<double> embedding;
};
constexpr size_t MAX_RESULT_COUNT = 5;
constexpr size_t MIN_DESC_LENGTH = 70;
AMap<APath /* file */, GalleryEntry> gCachedDatabase;
}

OpenAITools::Tool tools::searchPhotoInGallery(_<IOpenAIChat> openAI, const IOpenAIChat::Session& session) {
    return {
        .name = "search_photo_in_gallery",
        .description = "Searches for previously taken photos by the given query. "
                         "Use this before taking a new photo with #take_photo."
                         "The result of this tool is a set of photo descriptions and a filename. "
                         "The filename can then be sent to someone else using #send_telegram_message.",
        .parameters =
            {
                .properties =
                    {
                        {"photo_desc", {
                            .type = "string",
                            .description = "Freeform photo description to search for.",
                        },
                        },
                    },
                .required = {"photo_desc"},
            },
        .handler = [openAI = std::move(openAI), &session](OpenAITools::Ctx ctx) -> AFuture<AString> {
            auto photoDesc = ctx.args["photo_desc"].asStringOpt().valueOrException("photo_desc is required");
            const auto queryEmbedding = co_await openAI->embedding({ .config = config().embedding }, photoDesc);
            const auto files = APath("data/gallery").listDir(AFileListFlags::REGULAR_FILES);

            struct Result {
                APath filename;
                AString description;
                double similarity{};
            };
            AVector<AFuture<Result>> results;
            for (const auto& i : files) {
                if (!(i.endsWith(".png") || i.endsWith(".jpg") || i.endsWith(".jpeg"))) {
                    continue;
                }
                auto& entry = gCachedDatabase[i];
                if (entry.description.empty()) {
                    // cached description.
                    entry.description = co_await llmui::image({}, *openAI, i);
                }
                if (entry.embedding.size() != queryEmbedding.size()) {
                    APath path("cache/images/{}.emb.json"_format(i.filename()));
                    try {
                        if (path.isRegularFileExists()) {
                            entry.embedding = aui::from_json<std::valarray<double>>(AJson::fromStream(AFileInputStream(path)));
                        }
                    } catch (const AException& ){}
                    if (entry.embedding.size() != queryEmbedding.size()) {
                        entry.embedding = co_await openAI->embedding({.config = config().embedding }, entry.description);
                        AFileOutputStream(path) << aui::to_json(entry.embedding);
                    }
                }

                results << AUI_THREADPOOL_X [&queryEmbedding, i = i, entry = entry] {
                    return Result {
                        .filename = i,
                        .description = entry.description,
                        .similarity = util::cosine_similarity(queryEmbedding, entry.embedding),
                    };
                };
            }

            AVector<Result> resultsAwaited;
            for (const auto& i : results) {
                resultsAwaited << co_await i;
            }
            ranges::sort(resultsAwaited, [](const auto& a, const auto& b) { return a.similarity > b.similarity; });

            AString output;
            size_t count = 0;
            for (const auto& i : resultsAwaited) {
                if (count >= MAX_RESULT_COUNT) {
                    break;
                }
                if (i.description.length() <= MIN_DESC_LENGTH) {
                    // bug: short description like
                    // <photo description>
                    // Description complete.
                    // </photo>
                    // skip
                    continue;
                }
                AString tag = "<result filename=\"{}\">"_format(i.filename.filename());
                if (ranges::any_of(session, [&](const IOpenAIChat::Message& msg) {
                    return msg.content.contains(tag);
                })) {
                    continue;
                }
                output += tag;
                output += "\n{}\n</result>\n"_format(i.description);
                ++count;
            }

            co_return output;
        },
    };
}