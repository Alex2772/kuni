//
// Created by alex2772 on 3/14/26.
//

#include "DiaryEmbedding.h"

#include "Diary.h"
#include "IOpenAIChat.h"
#include "OpenAIChatImpl.h"

#include <Diary.h>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/iota.hpp>

#include "AUI/Util/AImageDrawable.h"
#include "AUI/Util/UIBuildingHelpers.h"
#include "AUI/View/ADrawableView.h"
#include "AUI/View/AForEachUI.h"
#include "AUI/View/AGroupBox.h"
#include "AUI/View/AScrollArea.h"
#include "AUI/View/ASpinnerV2.h"
#include "AUI/View/ATabView.h"
#include "AUI/View/AText.h"
#include "AUI/View/ATextArea.h"

#include <Diary.h>

using namespace declarative;

namespace {

    struct State: AObject {
        _<IOpenAIChat> chat = _new<OpenAIChatImpl>();
        AProperty<AString> query;
        AProperty<Diary> diary = Diary::Init { .diaryDir = APath("data/diary"), .openAI = chat };
        AProperty<AVector<Diary::EntryExAndRelatedness>> queriedEntries;
        AProperty<std::valarray<double>> queryEmbedding;
        AProperty<bool> isLoading = false;
        AFuture<> async;

        State() {
            AObject::connect(query.changed, [this] {
                async = updateQueriedEntries();
            });
        }

        AFuture<> updateQueriedEntries() {
            if (query->empty()) {
                co_return;
            }
            isLoading = true;
            AUI_DEFER { isLoading = false; };
            auto filter = [](const Diary::EntryEx& e) {
                // if (e.id.startsWith("msg_")) {
                //     return false;
                // }
                if (e.freeformBody.contains("<important_note")) {
                    return false;
                }
                return true;
            };
            queryEmbedding.raw = co_await chat->embedding({ .config = config().embedding }, query);
            queryEmbedding.notify();
            queriedEntries = co_await diary.raw.query(queryEmbedding, { .confidenceFactor = 0.f, .filter = filter });
        }
    };


    _<AView> embeddingView(_<AProperty<std::valarray<double>>> embedding) {
        auto makeDrawable = [=]() -> _<IDrawable> {
            if (embedding->value().size() == 0) {
                return nullptr;
            }
            auto scale = glm::max(embedding->value().max(), -embedding->value().min());
            auto side = size_t(glm::floor(glm::sqrt(embedding->value().size())));
            using Image = AFormattedImage<APixelFormat::RGBA_BYTE>;
            using Color = Image::Color;
            Image image({side, side});
            for (const auto[index, i] : *embedding | ranges::view::enumerate) {
                auto value = embedding->value()[index] / scale;
                AColor color{0, 0, 0, 0};
                // just like in pycharm;
                // -1 = blue
                // 0 = black
                // 1 = red
                color += AColor::BLUE * glm::clamp(-value, 0.0, 1.0);
                color += AColor::RED * glm::clamp(value, 0.0, 1.0);
                color.a = 1.f;
                image.set(glm::ivec2(index % side, index / side), AFormattedColorConverter(color));
            }
            return _new<AImageDrawable>(_new<AImage>(image));
        };
        auto drawable = _new<ADrawableView>();
        AObject::connect(AUI_REACT(makeDrawable()), AUI_SLOT(drawable)::setDrawable);
        return drawable AUI_OVERRIDE_STYLE {
            ImageRendering::PIXELATED,
            FixedSize { 128_dp, 128_dp },
        };
    }

    struct EntryView {
        const Diary::EntryEx& entry;
        AOptional<double> relatedness;

        _<AView> operator()() {
            return Horizontal {
                embeddingView(_new<AProperty<std::valarray<double>>>(entry.metadata.embedding)),
                Vertical::Expanding {
                    Horizontal{
                        Label{entry.id} AUI_OVERRIDE_STYLE{ATextOverflow::ELLIPSIS, Expanding()},
                        Label{relatedness ? "relatedness={}"_format(*relatedness) : ""} AUI_OVERRIDE_STYLE{
                            TextColor{
                                glm::mix(glm::vec4(AColor::RED), glm::vec4(AColor::GREEN), relatedness.valueOr(0))},
                        },
                    },
                    AText::fromString(entry.freeformBody.trim(' ')) AUI_OVERRIDE_STYLE{
                        ATextOverflow::ELLIPSIS,
                        Expanding(1, 0),
                        // MaxSize { {}, 12_pt * 5.f },
                        Opacity{0.5f},
                        AOverflow::HIDDEN_FROM_THIS,
                        Margin{0},
                        Padding{0},
                    },
                },
            } AUI_OVERRIDE_STYLE{
                Padding{4_dp},
                BackgroundSolid{AColor::WHITE},
                BorderRadius{4_dp},
            };
        }
    };
}

_<AView> ui::debug::DiaryEmbedding::operator()() {
    auto state = _new<State>();
    return Horizontal::Expanding {
        GroupBox {
            Label { "Query" },
            AScrollArea::Builder().withContents(
                Vertical::Expanding {
                    _new<ATextArea>() && state->query,
                    embeddingView(AUI_PTR_ALIAS(state, queryEmbedding)),
                }
            ),
        } AUI_OVERRIDE_STYLE { Expanding() },
        GroupBox {
            Label { "Entries" },
            Stacked {
                AScrollArea::Builder().withContents(
                AUI_DECLARATIVE_FOR(i, state->diary->list(), AVerticalLayout) {
                    return EntryView { .entry = i };
                } AUI_OVERRIDE_STYLE { LayoutSpacing {4_dp } }).build() AUI_LET {
                    AObject::connect(AUI_REACT(state->query->empty() ? Visibility::VISIBLE : Visibility::GONE), AUI_SLOT(it)::setVisibility);
                },
                AScrollArea::Builder().withContents(Vertical {
                  AUI_DECLARATIVE_FOR(i, *state->queriedEntries, AVerticalLayout) {
                    return EntryView { .entry = *i.entry, .relatedness = i.relatedness };
                  } AUI_OVERRIDE_STYLE { LayoutSpacing {4_dp } },
                }).build() AUI_LET {
                    AObject::connect(AUI_REACT(!state->query->empty() ? Visibility::VISIBLE : Visibility::GONE), AUI_SLOT(it)::setVisibility);
                },
                _new<ASpinnerV2>() AUI_LET {
                    AObject::connect(AUI_REACT(state->isLoading ? Visibility::VISIBLE : Visibility::GONE), AUI_SLOT(it)::setVisibility);
                },
            },
        } AUI_OVERRIDE_STYLE { Expanding() },
    };
}
