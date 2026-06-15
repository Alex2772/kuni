//
// Created by alex2772 on 6/8/26.
//

#include "proxy_server/message_injector.h"

#include <gtest/gtest.h>
#include <AUI/Json/AJson.h>

using namespace proxy_server;

// inject "injected" after 1 → [0, 1, "injected", 2, 3]
TEST(MessageInjector, Basic) {
    MessageInjector injector;
    injector.after(AJson(1)) << AJson("injected");

    EXPECT_EQ(AJson::toString(injector.merge({0, 1, 2, 3})), "[0,1,\"injected\",2,3]");
}

// no match → array unchanged
TEST(MessageInjector, NoMatch) {
    MessageInjector injector;
    injector.after(AJson(99)) << AJson("injected");

    EXPECT_EQ(AJson::toString(injector.merge({0, 1, 2})), "[0,1,2]");
}

// empty injector → array unchanged
TEST(MessageInjector, EmptyInjector) {
    MessageInjector injector;
    EXPECT_EQ(AJson::toString(injector.merge({0, 1, 2})), "[0,1,2]");
}

// empty array → still empty
TEST(MessageInjector, EmptyArray) {
    MessageInjector injector;
    injector.after(AJson(1)) << AJson("injected");
    EXPECT_TRUE(injector.merge({}).empty());
}

// inject multiple items after same anchor → [0, 1, "a", "b", 2]
TEST(MessageInjector, MultipleItemsAfterOne) {
    MessageInjector injector;
    injector.after(AJson(1)) << AJson("a") << AJson("b");

    EXPECT_EQ(AJson::toString(injector.merge({0, 1, 2})), "[0,1,\"a\",\"b\",2]");
}

// calling after() twice for the same anchor accumulates
TEST(MessageInjector, AfterAccumulates) {
    MessageInjector injector;
    injector.after(AJson(1)) << AJson("a");
    injector.after(AJson(1)) << AJson("b");

    EXPECT_EQ(AJson::toString(injector.merge({0, 1, 2})), "[0,1,\"a\",\"b\",2]");
}

// two different anchors each get their own injection
TEST(MessageInjector, TwoAnchors) {
    MessageInjector injector;
    injector.after(AJson(1)) << AJson("after1");
    injector.after(AJson(3)) << AJson("after3");

    EXPECT_EQ(AJson::toString(injector.merge({0, 1, 2, 3, 4})), "[0,1,\"after1\",2,3,\"after3\",4]");
}

// duplicate anchor in array → injection after every occurrence
TEST(MessageInjector, DuplicateAnchor) {
    MessageInjector injector;
    injector.after(AJson(1)) << AJson("x");

    EXPECT_EQ(AJson::toString(injector.merge({1, 1})), "[1,\"x\",1,\"x\"]");
}