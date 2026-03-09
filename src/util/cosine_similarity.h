#pragma once

#include <valarray>
#include <AUI/Common/AException.h>

namespace util {

    template<typename T>
    double cosine_similarity(const std::valarray<T>& a,
                             const std::valarray<T>& b) {
        if (a.size() != b.size()) throw AException("size mismatch: {} and {}"_format(a.size(), b.size()));
        const T dot = (a * b).sum();
        const T na = std::sqrt((a * a).sum());
        const T nb = std::sqrt((b * b).sum());
        if (na == 0.0 || nb == 0.0) return 0.0; // or throw, depending on needs
        return dot / (na * nb);
    }
}
