//
// Created by alex2772 on 5/14/26.
//

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/CivetServer.h>

#include "Prometheus.h"

#include <range/v3/all.hpp>

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

    prometheus::Labels breadcumbsLabels() const {
        prometheus::Labels out;
        for (const auto&[k,v] : metricBreadcumbs->value()) {
            if (v.empty()) {
                continue;
            }
            out[k] = v;
        }
        return out;
    }

    void registerOpenAI(OpenAIChatMeasurable& chat) override {
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
            .Help("OpenAI endpoint token usage (output tokens)")
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
};

}

_<prometheus::IExporter> prometheus::setup(_<MetricsBreadcumbs> metricBreadcumbs) {
    return _new<PrometheusImpl>(metricBreadcumbs);
}