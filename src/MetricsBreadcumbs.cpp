//
// Created by alex2772 on 5/14/26.
//

#include "MetricsBreadcumbs.h"
#include "AUI/Logging/ALogger.h"

static constexpr auto LOG_TAG = "MetricsBreadcumbs";

MetricsBreadcumbs::Point::Point(_<MetricsBreadcumbs> breadcumbs, AString key, AString value):
    mBreadcumbs(std::move(breadcumbs)),
    mKey(std::move(key)),
    mPrevValue(std::exchange(mBreadcumbs->mValue[mKey], value)) {
    // ALogger::trace(LOG_TAG) << "MetricsBreadcumbs::Point: key=" << mKey << "; value=" << value;

}

MetricsBreadcumbs::Point::~Point() {
    if (mKey.empty()) {
        return;
    }
    if (mBreadcumbs == nullptr) {
        return;
    }
    // ALogger::trace(LOG_TAG) << "MetricsBreadcumbs::~Point: key=" << mKey << "; value=" << mPrevValue;
    mBreadcumbs->mValue[mKey] = std::move(mPrevValue);
}
