//
// Unit tests for NotificationManager.
//
// Tests:
//   1. Priority ordering — higher-priority notifications are dequeued first
//      via nextNotification().
//   2. Notification removal — removeNotifications() removes by substring.
//   3. Worker pin accumulation — after taking a pinned notification, the
//      worker's pin set is updated.
//   4. Unpinned notification preferred over pinned — nextNotification picks
//      an unpinned notification before a pinned one owned by another worker.
//   5. Multiple notifications processed in priority order across multiple
//      passes of the worker loop.
//

#include "NotificationManager.h"
#include <gmock/gmock.h>
#include <AUI/Thread/AEventLoop.h>
#include <AUI/Thread/AAsyncHolder.h>

using namespace std::chrono_literals;

// ============================================================================
// Fixture
// ============================================================================
class NotificationManagerUnitTest : public ::testing::Test {
protected:
    AEventLoop mLoop;
    AAsyncHolder mAsync;
    IEventLoop::Handle mLoopHandle{&mLoop};
};

// ============================================================================
// Helper: construct an empty OpenAITools value.
// ============================================================================
static OpenAITools emptyActions() {
    return OpenAITools({});
}

// ============================================================================
// 1. Priority ordering
//
//   Three notifications with priorities 1, 5, 10 are inserted in reverse
//   priority order.  The worker must receive them in descending priority
//   order (10 → 5 → 1), confirming that passNotificationToAI inserts at the
//   correct position and nextNotification pops from the front.
// ============================================================================
TEST_F(NotificationManagerUnitTest, PriorityOrdering) {
    NotificationManager manager;
    ASet<AString> workerPins;
    AVector<AString> receivedMessages;

    // Worker: collect up to 3 messages then stop.
    mAsync << manager.run(workerPins, [&](NotificationManager::Notification notification) -> AFuture<bool> {
        receivedMessages << notification.message;
        co_return receivedMessages.size() < 3;
    });

    // Insert in reverse priority order.
    manager.passNotificationToAI({
        .message = "low",
        .actions = emptyActions(),
        .priority = 1,
    });
    manager.passNotificationToAI({
        .message = "high",
        .actions = emptyActions(),
        .priority = 10,
    });
    manager.passNotificationToAI({
        .message = "medium",
        .actions = emptyActions(),
        .priority = 5,
    });

    while (!mAsync.empty()) {
        mLoop.iteration();
    }

    ASSERT_EQ(receivedMessages.size(), 3u);
    // The message has a timestamp appended, so just check the prefix.
    EXPECT_THAT(receivedMessages[0], ::testing::StartsWith("high"));
    EXPECT_THAT(receivedMessages[1], ::testing::StartsWith("medium"));
    EXPECT_THAT(receivedMessages[2], ::testing::StartsWith("low"));
}

// ============================================================================
// 2. Notification removal by substring
//
//   removeNotifications removes all notifications whose message contains
//   the given substring, without affecting the rest.
// ============================================================================
TEST_F(NotificationManagerUnitTest, RemoveNotifications) {
    NotificationManager manager;

    manager.passNotificationToAI({
        .message = "hello world",
        .actions = emptyActions(),
    });
    manager.passNotificationToAI({
        .message = "goodbye world",
        .actions = emptyActions(),
    });
    manager.passNotificationToAI({
        .message = "hello everyone",
        .actions = emptyActions(),
    });

    // Remove all that contain "goodbye"
    manager.removeNotifications("goodbye");

    // Verify by having a worker drain the queue
    ASet<AString> workerPins;
    AVector<AString> remainingMessages;

    mAsync << manager.run(workerPins, [&](NotificationManager::Notification notification) -> AFuture<bool> {
        remainingMessages << notification.message;
        co_return remainingMessages.size() < 2;
    });

    // Wake up the worker with an unpinned notification
    manager.passNotificationToAI({
        .message = "wakeup",
        .actions = emptyActions(),
    });

    while (!mAsync.empty()) {
        mLoop.iteration();
    }

    ASSERT_EQ(remainingMessages.size(), 2u);
    EXPECT_THAT(remainingMessages[0], ::testing::StartsWith("hello world"));
    EXPECT_THAT(remainingMessages[1], ::testing::StartsWith("hello everyone"));
}

// ============================================================================
// 3. Next notification queue order with mixed priorities
//
//   After inserting many notifications and letting the worker process them,
//   verify that the highest-priority items come out first regardless of
//   insertion order.
// ============================================================================
TEST_F(NotificationManagerUnitTest, QueueOrder) {
    NotificationManager manager;
    ASet<AString> workerPins;
    AVector<int> receivedPriorities;

    // Worker: collect up to 5 notifications then stop.
    mAsync << manager.run(workerPins, [&](NotificationManager::Notification notification) -> AFuture<bool> {
        receivedPriorities << notification.priority;
        co_return receivedPriorities.size() < 5;
    });

    // Insert in arbitrary order.
    manager.passNotificationToAI({
        .message = "a",
        .actions = emptyActions(),
        .priority = 3,
    });
    manager.passNotificationToAI({
        .message = "b",
        .actions = emptyActions(),
        .priority = 7,
    });
    manager.passNotificationToAI({
        .message = "c",
        .actions = emptyActions(),
        .priority = 1,
    });
    manager.passNotificationToAI({
        .message = "d",
        .actions = emptyActions(),
        .priority = 9,
    });
    manager.passNotificationToAI({
        .message = "e",
        .actions = emptyActions(),
        .priority = 5,
    });

    while (!mAsync.empty()) {
        mLoop.iteration();
    }

    ASSERT_EQ(receivedPriorities.size(), 5u);
    EXPECT_EQ(receivedPriorities[0], 9);
    EXPECT_EQ(receivedPriorities[1], 7);
    EXPECT_EQ(receivedPriorities[2], 5);
    EXPECT_EQ(receivedPriorities[3], 3);
    EXPECT_EQ(receivedPriorities[4], 1);
}

// ============================================================================
// 4. Equal priorities — FIFO among equal-priority items
//
//   Notifications with the same priority should be processed in the order
//   they were inserted (FIFO).
// ============================================================================
TEST_F(NotificationManagerUnitTest, EqualPriorityFifo) {
    NotificationManager manager;
    ASet<AString> workerPins;
    AVector<AString> receivedMessages;

    mAsync << manager.run(workerPins, [&](NotificationManager::Notification notification) -> AFuture<bool> {
        receivedMessages << notification.message;
        co_return receivedMessages.size() < 3;
    });

    // All same priority — should be FIFO.
    manager.passNotificationToAI({
        .message = "first",
        .actions = emptyActions(),
        .priority = 5,
    });
    manager.passNotificationToAI({
        .message = "second",
        .actions = emptyActions(),
        .priority = 5,
    });
    manager.passNotificationToAI({
        .message = "third",
        .actions = emptyActions(),
        .priority = 5,
    });

    while (!mAsync.empty()) {
        mLoop.iteration();
    }

    ASSERT_EQ(receivedMessages.size(), 3u);
    EXPECT_THAT(receivedMessages[0], ::testing::StartsWith("first"));
    EXPECT_THAT(receivedMessages[1], ::testing::StartsWith("second"));
    EXPECT_THAT(receivedMessages[2], ::testing::StartsWith("third"));
}

// ============================================================================
// 5. Worker pin accumulation
//
//   When a worker processes a pinned notification, the pin is added to the
//   worker's pin set (the external set passed to run()).
// ============================================================================
TEST_F(NotificationManagerUnitTest, PinAccumulation) {
    NotificationManager manager;
    ASet<AString> workerPins;
    bool pinAdded = false;

    mAsync << manager.run(workerPins, [&](NotificationManager::Notification notification) -> AFuture<bool> {
        // After processing, check that the pin was added.
        if (notification.pin) {
            pinAdded = true;
        }
        co_return false; // stop after one notification
    });

    // Pass a pinned notification.  No worker has the pin yet, so pass an
    // unpinned one first to wake the worker.
    manager.passNotificationToAI({
        .message = "pinned msg",
        .actions = emptyActions(),
        .priority = 0,
        .pin = "test-chat",
    });
    manager.passNotificationToAI({
        .message = "wakeup",
        .actions = emptyActions(),
        .priority = 0,
    });

    while (!mAsync.empty()) {
        mLoop.iteration();
    }

    // The worker should have taken the pinned notification (since no other
    // worker owns the pin) and added it to the set.
    EXPECT_TRUE(workerPins.contains("test-chat"));
    EXPECT_TRUE(pinAdded);
}

// ============================================================================
// 6. Empty queue does not crash
//
//   Calling passNotificationToAI and removeNotifications on an empty
//   manager, or starting a worker with no notifications, should not crash.
// ============================================================================
TEST_F(NotificationManagerUnitTest, EmptyQueue) {
    NotificationManager manager;

    // Should not crash.
    manager.removeNotifications("nothing");

    // Should not crash.
    {
        ASet<AString> workerPins;
        // Start and immediately stop a worker (pass an unpinned notification
        // that tells the worker to stop).
        mAsync << manager.run(workerPins, [&](NotificationManager::Notification) -> AFuture<bool> {
            co_return false;
        });

        manager.passNotificationToAI({
            .message = "stop",
            .actions = emptyActions(),
        });

        while (!mAsync.empty()) {
            mLoop.iteration();
        }
    }

    // Second passNotificationToAI after worker stopped.
    manager.passNotificationToAI({
        .message = "after",
        .actions = emptyActions(),
    });
    // Ensure no crash when removing notifications that do/don't exist.
    manager.removeNotifications("after");
    manager.removeNotifications("nonexistent");
}

// ============================================================================
// 7. Pin stays with the worker that first processed it (2 workers)
//
//   With two competing workers, a pinned notification that gets picked up by
//   the first (idle) worker must keep being routed to that same worker for
//   every subsequent notification carrying the same pin — even though the
//   second worker is sitting idle the whole time and would normally be a
//   candidate for unpinned notifications.
// ============================================================================
TEST_F(NotificationManagerUnitTest, PinStaysWithWorkerThatFirstProcessedIt) {
    NotificationManager manager;
    ASet<AString> workerPinsA;
    ASet<AString> workerPinsB;
    // Give worker B an unrelated "control" pin so we can deterministically
    // stop it later without affecting the "chat1" routing being tested.
    workerPinsB << "control-b";

    AVector<AString> receivedByA;
    AVector<AString> receivedByB;

    // Register worker A first, then worker B - both start out idle.
    mAsync << manager.run(workerPinsA, [&](NotificationManager::Notification notification) -> AFuture<bool> {
        receivedByA << notification.message;
        co_return !notification.message.contains("stopA");
    });
    mAsync << manager.run(workerPinsB, [&](NotificationManager::Notification notification) -> AFuture<bool> {
        receivedByB << notification.message;
        co_return !notification.message.contains("stopB");
    });

    // 1. A pinned notification with no current owner is queued...
    manager.passNotificationToAI({
        .message = "first",
        .actions = emptyActions(),
        .pin = "chat1",
    });
    // 2. ...and an unpinned "kick" wakes the first idle worker (A), which
    //    drains the queue: it takes "first" (claiming the "chat1" pin) then
    //    "kick".
    manager.passNotificationToAI({
        .message = "kick",
        .actions = emptyActions(),
    });

    for (int i = 0; i < 1000 && receivedByA.size() < 2; ++i) {
        mLoop.iteration();
    }
    ASSERT_EQ(receivedByA.size(), 2u);
    EXPECT_TRUE(workerPinsA.contains("chat1"));

    // 3. A second notification with the same pin must go straight to worker
    //    A, even though worker B has been idle this whole time.
    manager.passNotificationToAI({
        .message = "second",
        .actions = emptyActions(),
        .pin = "chat1",
    });

    for (int i = 0; i < 1000 && receivedByA.size() < 3; ++i) {
        mLoop.iteration();
    }

    ASSERT_EQ(receivedByA.size(), 3u);
    EXPECT_THAT(receivedByA[0], ::testing::StartsWith("first"));
    EXPECT_THAT(receivedByA[1], ::testing::StartsWith("kick"));
    EXPECT_THAT(receivedByA[2], ::testing::StartsWith("second"));
    // Worker B never saw any of the "chat1" traffic.
    EXPECT_TRUE(receivedByB.empty());

    // Cleanup: stop both workers deterministically via their owned pins.
    manager.passNotificationToAI({
        .message = "stopA",
        .actions = emptyActions(),
        .pin = "chat1",
    });
    manager.passNotificationToAI({
        .message = "stopB",
        .actions = emptyActions(),
        .pin = "control-b",
    });

    while (!mAsync.empty()) {
        mLoop.iteration();
    }
}

// ============================================================================
// 8. Two workers, each keeps its own pinned chat (isolation)
//
//   When two workers each own a distinct pin, notifications for those pins
//   must always be routed to their respective owner, never crossing over,
//   regardless of send order or which worker happens to be idle.
// ============================================================================
TEST_F(NotificationManagerUnitTest, TwoWorkersEachKeepOwnPinnedChat) {
    NotificationManager manager;
    ASet<AString> workerPinsA;
    ASet<AString> workerPinsB;
    workerPinsA << "chatA";
    workerPinsB << "chatB";

    AVector<AString> receivedByA;
    AVector<AString> receivedByB;

    mAsync << manager.run(workerPinsA, [&](NotificationManager::Notification notification) -> AFuture<bool> {
        receivedByA << notification.message;
        co_return !notification.message.contains("stopA");
    });
    mAsync << manager.run(workerPinsB, [&](NotificationManager::Notification notification) -> AFuture<bool> {
        receivedByB << notification.message;
        co_return !notification.message.contains("stopB");
    });

    // Interleave notifications for both pinned chats.
    manager.passNotificationToAI({ .message = "a1", .actions = emptyActions(), .pin = "chatA" });
    manager.passNotificationToAI({ .message = "b1", .actions = emptyActions(), .pin = "chatB" });
    manager.passNotificationToAI({ .message = "a2", .actions = emptyActions(), .pin = "chatA" });
    manager.passNotificationToAI({ .message = "b2", .actions = emptyActions(), .pin = "chatB" });

    for (int i = 0; i < 1000 && (receivedByA.size() < 2 || receivedByB.size() < 2); ++i) {
        mLoop.iteration();
    }

    ASSERT_EQ(receivedByA.size(), 2u);
    ASSERT_EQ(receivedByB.size(), 2u);
    EXPECT_THAT(receivedByA[0], ::testing::StartsWith("a1"));
    EXPECT_THAT(receivedByA[1], ::testing::StartsWith("a2"));
    EXPECT_THAT(receivedByB[0], ::testing::StartsWith("b1"));
    EXPECT_THAT(receivedByB[1], ::testing::StartsWith("b2"));

    // Cleanup.
    manager.passNotificationToAI({ .message = "stopA", .actions = emptyActions(), .pin = "chatA" });
    manager.passNotificationToAI({ .message = "stopB", .actions = emptyActions(), .pin = "chatB" });

    while (!mAsync.empty()) {
        mLoop.iteration();
    }
}