#pragma once
#include "AUI/Common/AMap.h"

struct MetricsBreadcumbs {
public:
    [[nodiscard]] AMap<AString, AString> value() const { return mValue; }

    struct Point: aui::noncopyable {
        Point(_<MetricsBreadcumbs> breadcumbs, AString key, AString value);
        ~Point();

    private:
        _<MetricsBreadcumbs> mBreadcumbs;
        AString mKey;
        AString mPrevValue;
    };

private:
    AMap<AString, AString> mValue;
};