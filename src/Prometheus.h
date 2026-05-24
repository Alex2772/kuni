#pragma once
#include "AppBase.h"
#include "OpenAIChatMeasurable.h"

namespace prometheus {

class IExporter {
public:
    virtual ~IExporter() = default;

    virtual void registerOpenAI(OpenAIChatMeasurable& chat) = 0;
    virtual void registerAppBase(AppBase& app) = 0;
};

_<IExporter> setup(_<MetricsBreadcumbs> metricBreadcumbs);
}