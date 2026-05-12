#include "util/json_utils.h"
#include <gmock/gmock.h>

// =====================================================================
// util::jsonAsLongInt — coerces JSON values to long long
// =====================================================================

// --- Integer inputs (should pass through directly) ---

TEST(JsonAsLongIntUnit, IntegerSmall) {
    AJson v(42);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 42);
}

TEST(JsonAsLongIntUnit, IntegerZero) {
    AJson v(0);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 0);
}

TEST(JsonAsLongIntUnit, IntegerNegative) {
    AJson v(-123456789);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, -123456789);
}

TEST(JsonAsLongIntUnit, IntegerLarge) {
    AJson v(625207005);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 625207005);
}

TEST(JsonAsLongIntUnit, IntegerMaxInt) {
    AJson v(2147483647);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 2147483647);
}

// --- Floating-point inputs (scientific notation coercion) ---

TEST(JsonAsLongIntUnit, ScientificNotationPositive) {
    // Qwen 3.5 9B bug: outputs integers as e.g. 6.25207005e+8
    AJson v(6.25207005e+8);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 625207005);
}

TEST(JsonAsLongIntUnit, ScientificNotationSmall) {
    AJson v(1.0e+2);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 100);
}

TEST(JsonAsLongIntUnit, ScientificNotationZero) {
    AJson v(0.0e+0);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 0);
}

TEST(JsonAsLongIntUnit, ScientificNotationNegativeExponent) {
    // Values < 1 should truncate to 0
    AJson v(1.23e-5);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 0);
}

TEST(JsonAsLongIntUnit, FloatTruncation) {
    AJson v(3.999);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 3); // truncation, not rounding
}

TEST(JsonAsLongIntUnit, FloatNegative) {
    AJson v(-7.5e+1);
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, -75);
}

// --- String inputs (parsed via stod) ---

TEST(JsonAsLongIntUnit, StringScientificNotation) {
    AJson v("6.25207005e+8");
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 625207005);
}

TEST(JsonAsLongIntUnit, StringPlainInteger) {
    AJson v("625207005");
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 625207005);
}

TEST(JsonAsLongIntUnit, StringNegative) {
    AJson v("-100");
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, -100);
}

TEST(JsonAsLongIntUnit, StringFloat) {
    AJson v("3.14");
    auto result = util::jsonAsLongInt(v);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result, 3);
}

// --- Edge cases: null, bool, array, object ---

TEST(JsonAsLongIntUnit, NullReturnsNullopt) {
    AJson v(nullptr);
    auto result = util::jsonAsLongInt(v);
    EXPECT_FALSE(result.hasValue());
}

TEST(JsonAsLongIntUnit, BoolReturnsNullopt) {
    AJson v(true);
    auto result = util::jsonAsLongInt(v);
    EXPECT_FALSE(result.hasValue());
}

TEST(JsonAsLongIntUnit, ArrayReturnsNullopt) {
    AJson v = AJson::Array();
    auto result = util::jsonAsLongInt(v);
    EXPECT_FALSE(result.hasValue());
}

TEST(JsonAsLongIntUnit, ObjectReturnsNullopt) {
    AJson v = AJson::Object();
    auto result = util::jsonAsLongInt(v);
    EXPECT_FALSE(result.hasValue());
}

// --- Unparseable string ---

TEST(JsonAsLongIntUnit, StringGarbageReturnsNullopt) {
    AJson v("not a number");
    auto result = util::jsonAsLongInt(v);
    EXPECT_FALSE(result.hasValue());
}

TEST(JsonAsLongIntUnit, StringEmptyReturnsNullopt) {
    AJson v("");
    auto result = util::jsonAsLongInt(v);
    EXPECT_FALSE(result.hasValue());
}
