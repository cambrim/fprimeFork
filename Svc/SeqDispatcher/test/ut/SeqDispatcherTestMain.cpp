// ----------------------------------------------------------------------
// TestMain.cpp
// ----------------------------------------------------------------------

#include "SeqDispatcherTester.hpp"

TEST(Nominal, testDispatch) {
    Svc::SeqDispatcherTester tester;
    tester.testDispatch();
}

TEST(Nominal, testLogStatus) {
    Svc::SeqDispatcherTester tester;
    tester.testLogStatus();
}

TEST(RunArgs, testRunArgsWithValidArguments) {
    Svc::SeqDispatcherTester tester;
    tester.testRunArgsWithValidArguments();
}

TEST(RunArgs, testRunArgsWithMaxSizedArguments) {
    Svc::SeqDispatcherTester tester;
    tester.testRunArgsWithMaxSizedArguments();
}

TEST(RunArgs, testRunArgsNoSequencersAvailable) {
    Svc::SeqDispatcherTester tester;
    tester.testRunArgsNoSequencersAvailable();
}

TEST(RunArgs, testRunArgsBlockingVsNonBlocking) {
    Svc::SeqDispatcherTester tester;
    tester.testRunArgsBlockingVsNonBlocking();
}

TEST(CancelName, testCancelName) {
    Svc::SeqDispatcherTester tester;
    tester.testCancelName();
}

TEST(CancelName, testCancelNameNotFound) {
    Svc::SeqDispatcherTester tester;
    tester.testCancelNameNotFound();
}

TEST(CancelAll, testCancelAll) {
    Svc::SeqDispatcherTester tester;
    tester.testCancelAll();
}

TEST(CancelAll, testCancelAllNoneRunning) {
    Svc::SeqDispatcherTester tester;
    tester.testCancelAllNoneRunning();
}

TEST(Queue, testQueueWhenBusy) {
    Svc::SeqDispatcherTester tester;
    tester.testQueueWhenBusy();
}

TEST(Queue, testQueueOverflow) {
    Svc::SeqDispatcherTester tester;
    tester.testQueueOverflow();
}

TEST(Queue, testQueueDispatch) {
    Svc::SeqDispatcherTester tester;
    tester.testQueueDispatch();
}

TEST(Queue, testClearQueue) {
    Svc::SeqDispatcherTester tester;
    tester.testClearQueue();
}

TEST(Queue, testPauseResumeQueue) {
    Svc::SeqDispatcherTester tester;
    tester.testPauseResumeQueue();
}

TEST(Queue, testQueueDisabled) {
    Svc::SeqDispatcherTester tester;
    tester.testQueueDisabled();
}

TEST(Queue, testQueueTelemetry) {
    Svc::SeqDispatcherTester tester;
    tester.testQueueTelemetry();
}

TEST(Queue, testBlockStatePreservation) {
    Svc::SeqDispatcherTester tester;
    tester.testBlockStatePreservation();
}

TEST(Queue, testQueueWithErrors) {
    Svc::SeqDispatcherTester tester;
    tester.testQueueWithErrors();
}

TEST(Queue, testMultipleSequencers) {
    Svc::SeqDispatcherTester tester;
    tester.testMultipleSequencers();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
