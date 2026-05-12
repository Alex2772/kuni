#pragma once
#include <AUI/Json/AJson.h>
#include <AUI/Logging/ALogger.h>

namespace util {
// Qwen 3.5 9B bug: outputs integers as scientific notation (e.g. 7.576e+08)
// AJson::asLongIntOpt() returns nullopt for floats/strings, so we coerce manually.
inline AOptional<long long> jsonAsLongInt(const AJson& v) {
    if (auto i = v.asLongIntOpt()) return i;
    if (auto d = v.asNumberOpt()) {
        auto result = static_cast<long long>(*d);
        ALogger::warn("jsonAsLongInt") << "coerced number " << AJson::toString(v) << " to long long: " << result;
        return result;
    }
    if (auto s = v.asStringOpt()) {
        try {
            auto result = static_cast<long long>(std::stod(s->toStdString()));
            ALogger::warn("jsonAsLongInt") << "coerced string " << AJson::toString(v) << " to long long: " << result;
            return result;
        } catch (...) {}
    }
    ALogger::warn("jsonAsLongInt") << "can't convert JSON value to long long: " << AJson::toString(v);
    return std::nullopt;
}
}