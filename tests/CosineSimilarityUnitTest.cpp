#include "util/cosine_similarity.h"
#include <gmock/gmock.h>

// =====================================================================
// cosine_similarity (valarray version, as used by Diary)
// =====================================================================

TEST(CosineSimilarityUnit, IdenticalVectors) {
    std::valarray<double> a = {1.0, 2.0, 3.0};
    auto result = util::cosine_similarity(a, a);
    EXPECT_DOUBLE_EQ(result, 1.0);
}

TEST(CosineSimilarityUnit, OrthogonalVectors) {
    std::valarray<double> a = {1.0, 0.0};
    std::valarray<double> b = {0.0, 1.0};
    auto result = util::cosine_similarity(a, b);
    EXPECT_DOUBLE_EQ(result, 0.0);
}

TEST(CosineSimilarityUnit, OppositeVectors) {
    std::valarray<double> a = {1.0, 0.0};
    std::valarray<double> b = {-1.0, 0.0};
    auto result = util::cosine_similarity(a, b);
    EXPECT_DOUBLE_EQ(result, -1.0);
}

TEST(CosineSimilarityUnit, ArbitraryAngle) {
    std::valarray<double> a = {3.0, 4.0};
    std::valarray<double> b = {4.0, 3.0};
    // dot = 3*4 + 4*3 = 24, |a| = 5, |b| = 5, cos = 24/25 = 0.96
    auto result = util::cosine_similarity(a, b);
    EXPECT_DOUBLE_EQ(result, 0.96);
}

TEST(CosineSimilarityUnit, ZeroVector) {
    std::valarray<double> a = {0.0, 0.0};
    std::valarray<double> b = {1.0, 2.0};
    auto result = util::cosine_similarity(a, b);
    EXPECT_DOUBLE_EQ(result, 0.0);
}

TEST(CosineSimilarityUnit, SizeMismatch) {
    std::valarray<double> a = {1.0, 2.0, 3.0};
    std::valarray<double> b = {1.0, 2.0};
    EXPECT_THROW(util::cosine_similarity(a, b), AException);
}

TEST(CosineSimilarityUnit, SingleElementVectors) {
    std::valarray<double> a = {5.0};
    std::valarray<double> b = {5.0};
    EXPECT_DOUBLE_EQ(util::cosine_similarity(a, b), 1.0);
}

TEST(CosineSimilarityUnit, NegativeValues) {
    std::valarray<double> a = {-1.0, -2.0, -3.0};
    std::valarray<double> b = {1.0, 2.0, 3.0};
    auto result = util::cosine_similarity(a, b);
    EXPECT_DOUBLE_EQ(result, -1.0);
}
