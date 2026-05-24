//
// Created by alex2772 on 5/14/26.
//

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <prometheus/CivetServer.h>

#include "Prometheus.h"
#include "AppBase.h"

#include <range/v3/all.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

// convience: merge two prometheus::Labels
template<typename K, typename V>
std::map<K, V> operator+(std::map<K, V> lhs, const std::map<K, V>& rhs) {
    lhs.insert(rhs.begin(), rhs.end());
    return lhs;
}

static auto civetCallbacks() {
    CivetCallbacks result;
    aui::zero(result);
    result.log_access = [](const mg_connection*, const char* message) {
        ALOG_TRACE("Prometheus") << message;
        return 1;
    };
    result.log_message = [](const mg_connection*, const char* message) {
        ALOG_TRACE("Prometheus") << message;
        return 1;
    };
    return result;
}

struct PrometheusImpl: AObject, prometheus::IExporter {
    CivetCallbacks civetCallbacks = ::civetCallbacks();
    prometheus::Exposer exposer{"0.0.0.0:9464", 1, &civetCallbacks};
    _<prometheus::Registry> registry = _new<prometheus::Registry>();
    _<MetricsBreadcumbs> metricBreadcumbs;

    PrometheusImpl(_<MetricsBreadcumbs> metricBreadcumbs): metricBreadcumbs(std::move(metricBreadcumbs)) {
        exposer.RegisterCollectable(registry);
    }

    static prometheus::Labels fromMap(const AMap<AString, AString>& map) {
        prometheus::Labels out;
        for (const auto&[k,v] : map) {
            if (v.empty()) {
                continue;
            }
            out[k] = v;
        }
        return out;
    }

    prometheus::Labels breadcumbsLabels() const {
        return fromMap(metricBreadcumbs->value());
    }

    void registerOpenAI(OpenAIChatMeasurable& chat) override {
        {
            auto& input = prometheus::BuildCounter()
                .Name("llm_usage_input")
                .Help("OpenAI endpoint token usage (input tokens)")
                .Register(*registry)
            ;
            auto& input_cache_hit = prometheus::BuildCounter()
                .Name("llm_usage_input_cache_hit")
                .Help("OpenAI endpoint token usage (input tokens that were cached; cheap)")
                .Register(*registry)
            ;
            auto& input_cache_miss = prometheus::BuildCounter()
                .Name("llm_usage_input_cache_miss")
                .Help("OpenAI endpoint token usage (input tokens that weren't; expensive)")
                .Register(*registry)
            ;
            auto& output = prometheus::BuildCounter()
                .Name("llm_usage_output")
                .Help("OpenAI endpoint token usage (output tokens; expensive)")
                .Register(*registry)
            ;
            connect(chat.responseMetrics, [&](OpenAIChatMeasurable::Metrics usage) {
                auto base = breadcumbsLabels() + prometheus::Labels {
                    {"model", usage.model},
                };
                input.Add(base).Increment(usage.usage.prompt_tokens);
                input_cache_hit.Add(base).Increment(usage.usage.prompt_cache_hit_tokens);
                input_cache_miss.Add(base).Increment(usage.usage.prompt_cache_miss_tokens);
                output.Add(base).Increment(usage.usage.completion_tokens);
            });
        }
        {

            auto& input = prometheus::BuildGauge()
                .Name("llm_usage_input_gauge")
                .Help("OpenAI endpoint last query input tokens")
                .Register(*registry)
            ;
            auto& input_cache_hit = prometheus::BuildGauge()
                .Name("llm_usage_input_cache_hit_gauge")
                .Help("OpenAI endpoint last query input tokens that were cached; cheap")
                .Register(*registry)
            ;
            auto& input_cache_miss = prometheus::BuildGauge()
                .Name("llm_usage_input_cache_miss_gauge")
                .Help("OpenAI endpoint last query input tokens that weren't cached; expensive")
                .Register(*registry)
            ;
            auto& output = prometheus::BuildGauge()
                .Name("llm_usage_output_gauge")
                .Help("OpenAI endpoint last query output tokens; expensive")
                .Register(*registry)
            ;
            connect(chat.responseMetrics, [&](OpenAIChatMeasurable::Metrics usage) {
                auto base = prometheus::Labels {
                    {"model", usage.model},
                };
                input.Add(base).Set(usage.usage.prompt_tokens);
                input_cache_hit.Add(base).Set(usage.usage.prompt_cache_hit_tokens);
                input_cache_miss.Add(base).Set(usage.usage.prompt_cache_miss_tokens);
                output.Add(base).Set(usage.usage.completion_tokens);
            });
        }
    }

    void registerAppBase(AppBase& app) override {
        auto& toolCallCounter = prometheus::BuildCounter()
            .Name("tool_call_total")
            .Help("Number of tool calls dispatched by the LLM, labelled by tool name, chat, and scenario")
            .Register(*registry)
        ;
        // Buckets cover: 1min, 5min, 15min, 30min, 1h, 2h, 4h, 8h, 1d, 3d, 7d, 14d, 30d
        auto& responseTimeHistogram = prometheus::BuildHistogram()
            .Name("tool_call_response_time_seconds")
            .Help("Time between the last message in the chat and the bot's tool call dispatch, as a histogram")
            .Register(*registry)
        ;
        auto& responseTimeGauge = prometheus::BuildGauge()
            .Name("tool_call_response_time_seconds_gauge")
            .Help("Time between the last message in the chat and the bot's tool call dispatch, as a gauge")
            .Register(*registry)
        ;
        static const prometheus::Histogram::BucketBoundaries kResponseTimeBuckets = {
            5,
            10,
            15,
            20,
            30,
            40,
            50,
            60, 300, 900, 1800, 3600, 7200, 14400, 28800, 86400, 259200, 604800, 1209600, 2592000
        };
        connect(app.toolCallFired, [&](AppBase::ToolCallEvent ev) {
            // ALogger::info("Prometheus") << "toolCallFired: " << ev.toolName << ", chat=" << ev.breadcrumbLabels["chat"];
            auto labels = fromMap(ev.breadcrumbLabels);
            labels["tool"] = ev.toolName;
            toolCallCounter.Add(labels).Increment();
            if (ev.lastOpenedChatLastMessageTime) {
                responseTimeHistogram.Add(labels, kResponseTimeBuckets).Observe(static_cast<double>(ev.lastOpenedChatLastMessageTime->count()));
                responseTimeGauge.Add(labels).Set(static_cast<double>(ev.lastOpenedChatLastMessageTime->count()));
            }
        });
    }
};

}

_<prometheus::IExporter> prometheus::setup(_<MetricsBreadcumbs> metricBreadcumbs) {
    return _new<PrometheusImpl>(metricBreadcumbs);
}