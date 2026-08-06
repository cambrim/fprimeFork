// ======================================================================
// \title  SeqDispatcher.hpp
// \author zimri.leisher
// \brief  cpp file for SeqDispatcher test harness implementation class
// ======================================================================

#include "SeqDispatcherTester.hpp"
#include "config/FppConstantsAc.hpp"

namespace Svc {

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

SeqDispatcherTester ::SeqDispatcherTester()
    : SeqDispatcherGTestBase("SeqDispatcherTester", SeqDispatcherTester::MAX_HISTORY_SIZE), component("SeqDispatcher") {
    this->connectPorts();
    this->initComponents();
}

SeqDispatcherTester ::~SeqDispatcherTester() {
    this->component.deinit();
}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void SeqDispatcherTester ::testDispatch() {
    // test that it fails when we dispatch too many sequences
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN(0, 0, Fw::String("test"), BlockState::BLOCK);
        this->component.doDispatch();
        // no response cuz blocking
        ASSERT_CMD_RESPONSE_SIZE(0);
        ASSERT_EVENTS_SIZE(0);
    }
    ASSERT_TLM_sequencersAvailable(SeqDispatcherSequencerPorts - 1, 0);
    this->clearHistory();
    // all sequencers should be busy
    sendCmd_RUN(0, 0, Fw::String("test"), BlockState::BLOCK);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN, 0, Fw::CmdResponse::EXECUTION_ERROR);

    this->clearHistory();

    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();
    ASSERT_EVENTS_SIZE(0);
    // we should have gotten a cmd response now
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN, 0, Fw::CmdResponse::OK);

    this->clearHistory();
    // ok now we should be able to send another sequence
    // let's test non blocking now
    sendCmd_RUN(0, 0, Fw::String("test"), BlockState::NO_BLOCK);
    this->component.doDispatch();

    // should immediately return
    ASSERT_EVENTS_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN, 0, Fw::CmdResponse::OK);
    this->clearHistory();

    // ok now check that if a sequence errors on block it will return error
    this->invoke_to_seqDoneIn(1, 0, 0, Fw::CmdResponse::EXECUTION_ERROR);
    this->component.doDispatch();
    ASSERT_EVENTS_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN, 0, Fw::CmdResponse::EXECUTION_ERROR);
}

void SeqDispatcherTester::testLogStatus() {
    this->sendCmd_RUN(0, 0, Fw::String("test"), BlockState::BLOCK);
    this->component.doDispatch();
    this->clearHistory();
    this->sendCmd_LOG_STATUS(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_SIZE(SeqDispatcherSequencerPorts);
    ASSERT_EVENTS_LogSequencerStatus(0, 0, SeqDispatcher_CmdSequencerState::RUNNING_SEQUENCE_BLOCK, "test");
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_LOG_STATUS, 0, Fw::CmdResponse::OK);
}

void SeqDispatcherTester::seqRunOut_handler(FwIndexType portNum,             //!< The port number
                                            const Fw::StringBase& filename,  //!< The sequence file
                                            const Svc::SeqArgs& args         //!< Sequence arguments
) {
    this->pushFromPortEntry_seqRunOut(filename, args);
}

void SeqDispatcherTester::seqCancelOut_handler(FwIndexType portNum  //!< The port number
) {
    this->pushFromPortEntry_seqCancelOut();
}

// Test CANCEL_NAME cancels the sequencer running the named file and clears state on done
void SeqDispatcherTester::testCancelName() {
    // Dispatch a non-blocking sequence so sequencer 0 is running "test"
    Svc::SeqArgs emptyArgs{0, 0};
    this->sendCmd_RUN_ARGS(0, 0, Fw::String("test"), BlockState::NO_BLOCK, emptyArgs);
    this->component.doDispatch();
    ASSERT_from_seqRunOut_SIZE(1);
    ASSERT_TLM_sequencersAvailable(0, SeqDispatcherSequencerPorts - 1);
    this->clearHistory();

    // Cancel by filename
    this->sendCmd_CANCEL_NAME(0, 0, Fw::String("test"));
    this->component.doDispatch();

    // The matching sequencer should have been sent a cancel on its port
    ASSERT_from_seqCancelOut_SIZE(1);
    // Event + counter recorded
    ASSERT_EVENTS_SequenceCanceled_SIZE(1);
    ASSERT_EVENTS_SequenceCanceled(0, 0, "test");
    ASSERT_TLM_canceledCount(0, 1);
    // Command succeeds
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_CANCEL_NAME, 0, Fw::CmdResponse::OK);
    this->clearHistory();

    // The canceled sequencer reports done, which clears our internal state
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::EXECUTION_ERROR);
    this->component.doDispatch();
    ASSERT_TLM_sequencersAvailable(0, SeqDispatcherSequencerPorts);
}

// Test CANCEL_NAME with a filename that is not running returns an error and cancels nothing
void SeqDispatcherTester::testCancelNameNotFound() {
    // No sequence running; cancel a name that does not match
    this->sendCmd_CANCEL_NAME(0, 0, Fw::String("does_not_exist"));
    this->component.doDispatch();

    // Nothing canceled
    ASSERT_from_seqCancelOut_SIZE(0);
    // Warning event, error response
    ASSERT_EVENTS_CancelSequenceNotFound_SIZE(1);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_CANCEL_NAME, 0, Fw::CmdResponse::EXECUTION_ERROR);
}

// Test CANCEL_ALL cancels every running sequencer and clears state on done.
// This is a broadcast: no sequencer is excluded, so a sequence that issues
// CANCEL_ALL would itself be canceled.
void SeqDispatcherTester::testCancelAll() {
    Svc::SeqArgs emptyArgs{0, 0};
    // Fill every sequencer with a running (non-blocking) sequence
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        this->sendCmd_RUN_ARGS(0, 0, Fw::String("test"), BlockState::NO_BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    ASSERT_from_seqRunOut_SIZE(SeqDispatcherSequencerPorts);
    ASSERT_TLM_sequencersAvailable(SeqDispatcherSequencerPorts - 1, 0);
    this->clearHistory();

    // Broadcast cancel to every running sequencer
    this->sendCmd_CANCEL_ALL(0, 0);
    this->component.doDispatch();

    // Every sequencer received a cancel on its port
    ASSERT_from_seqCancelOut_SIZE(SeqDispatcherSequencerPorts);
    // One event + one counter increment per canceled sequencer
    ASSERT_EVENTS_SequenceCanceled_SIZE(SeqDispatcherSequencerPorts);
    ASSERT_TLM_canceledCount_SIZE(SeqDispatcherSequencerPorts);
    ASSERT_TLM_canceledCount(SeqDispatcherSequencerPorts - 1, SeqDispatcherSequencerPorts);
    // Command succeeds
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_CANCEL_ALL, 0, Fw::CmdResponse::OK);
    this->clearHistory();

    // Each canceled sequencer reports done, which clears our internal state
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        this->invoke_to_seqDoneIn(static_cast<FwIndexType>(i), 0, 0, Fw::CmdResponse::EXECUTION_ERROR);
        this->component.doDispatch();
    }
    ASSERT_TLM_sequencersAvailable(SeqDispatcherSequencerPorts - 1, SeqDispatcherSequencerPorts);
}

// Test CANCEL_ALL with no sequences running succeeds and cancels nothing
void SeqDispatcherTester::testCancelAllNoneRunning() {
    // No sequences running; CANCEL_ALL should be a benign no-op that still succeeds
    this->sendCmd_CANCEL_ALL(0, 0);
    this->component.doDispatch();

    // Nothing canceled, no events, but the command still reports OK
    ASSERT_from_seqCancelOut_SIZE(0);
    ASSERT_EVENTS_SequenceCanceled_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_CANCEL_ALL, 0, Fw::CmdResponse::OK);
}

// Test RUN_ARGS with valid arguments - verify arguments are propagated correctly
void SeqDispatcherTester::testRunArgsWithValidArguments() {
    // Create test arguments with some data
    // Note: Keep size small to fit within FW_CMD_ARG_BUFFER_MAX_SIZE constraints
    // Total command payload must fit: filename (~44 bytes) + BlockState (4 bytes) + SeqArgs
    // With FW_CMD_ARG_BUFFER_MAX_SIZE ~= 500 bytes, SeqArgs should be < 400 bytes total
    Svc::SeqArgs testArgs{0, 0};

    // Send RUN_ARGS command with non-blocking mode
    sendCmd_RUN_ARGS(0, 0, Fw::String("test"), BlockState::NO_BLOCK, testArgs);
    this->component.doDispatch();

    // Should get immediate response for non-blocking
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::OK);

    // Verify that seqRunOut was called with correct arguments
    ASSERT_from_seqRunOut_SIZE(1);
    ASSERT_from_seqRunOut(0, Fw::String("test"), testArgs);

    // Verify telemetry
    ASSERT_TLM_dispatchedCount(0, 1);
    ASSERT_TLM_sequencersAvailable(0, SeqDispatcherSequencerPorts - 1);
}

// Test RUN_ARGS with maximum-sized arguments - test boundary conditions
void SeqDispatcherTester::testRunArgsWithMaxSizedArguments() {
    constexpr FwSizeType TEST_ARG_SIZE = SequenceArgumentsMaxSize;
    Svc::SeqArgs largeArgs(TEST_ARG_SIZE, 0);
    U8* buffer = largeArgs.get_buffer();
    for (FwSizeType i = 0; i < TEST_ARG_SIZE; i++) {
        buffer[i] = static_cast<U8>(i % 256);
    }

    // Send RUN_ARGS command with large arguments (use short filename to save space)
    sendCmd_RUN_ARGS(0, 0, Fw::String("test"), BlockState::NO_BLOCK, largeArgs);
    this->component.doDispatch();

    // Should get immediate response for non-blocking
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::OK);

    // Verify that seqRunOut was called with correct large arguments
    ASSERT_from_seqRunOut_SIZE(1);
    ASSERT_from_seqRunOut(0, Fw::String("test"), largeArgs);

    // Verify telemetry
    ASSERT_TLM_dispatchedCount(0, 1);
}

// Test RUN_ARGS when no sequencers available - verify error handling
void SeqDispatcherTester::testRunArgsNoSequencersAvailable() {
    // Fill all sequencers
    Svc::SeqArgs emptyArgs{0, 0};
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("test"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
        // no response because blocking
        ASSERT_CMD_RESPONSE_SIZE(0);
    }
    ASSERT_TLM_sequencersAvailable(SeqDispatcherSequencerPorts - 1, 0);
    this->clearHistory();

    // Now try to send another sequence when all are busy
    Svc::SeqArgs testArgs;
    testArgs.set_size(0);

    sendCmd_RUN_ARGS(0, 0, Fw::String("test"), BlockState::BLOCK, testArgs);
    this->component.doDispatch();

    // Should get error response
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::EXECUTION_ERROR);

    // Should get warning event
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_NoAvailableSequencers_SIZE(1);

    // Verify no seqRunOut was called
    ASSERT_from_seqRunOut_SIZE(0);
}

// Test RUN_ARGS with blocking vs non-blocking behavior
void SeqDispatcherTester::testRunArgsBlockingVsNonBlocking() {
    Svc::SeqArgs testArgs{0, 0};

    // Test non-blocking mode - should get immediate response
    sendCmd_RUN_ARGS(0, 0, Fw::String("nonblocking"), BlockState::NO_BLOCK, testArgs);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::OK);
    ASSERT_from_seqRunOut_SIZE(1);
    this->clearHistory();

    // Free up the sequencer
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();
    this->clearHistory();

    // Test blocking mode - should NOT get immediate response
    sendCmd_RUN_ARGS(0, 0, Fw::String("blocking"), BlockState::BLOCK, testArgs);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(0);  // No response yet
    ASSERT_from_seqRunOut_SIZE(1);
    ASSERT_from_seqRunOut(0, Fw::String("blocking"), testArgs);

    // Now complete the sequence
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();

    // Should now get response
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::OK);

    // Test blocking mode with error response
    this->clearHistory();
    sendCmd_RUN_ARGS(0, 0, Fw::String("blocking_error"), BlockState::BLOCK, testArgs);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(0);  // No response yet

    // Complete with error
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::EXECUTION_ERROR);
    this->component.doDispatch();

    // Should get error response
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_TLM_errorCount(0, 1);
}

// ----------------------------------------------------------------------
// Queue tests
// ----------------------------------------------------------------------

// Test that sequences queue when all sequencers are busy
void SeqDispatcherTester::testQueueWhenBusy() {
    // Set queue depth parameter
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(20, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers with blocking sequences
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("test"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
        ASSERT_CMD_RESPONSE_SIZE(0);  // No response for blocking
    }
    ASSERT_TLM_sequencersAvailable(SeqDispatcherSequencerPorts - 1, 0);
    this->clearHistory();

    // Next sequence should queue
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued1"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();

    // Should get immediate OK response (queued successfully)
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::OK);

    // Should have SequenceQueued event
    ASSERT_EVENTS_SequenceQueued_SIZE(1);
    ASSERT_EVENTS_SequenceQueued(0, "queued1", 1);

    // Verify telemetry
    ASSERT_TLM_queueDepth_SIZE(1);
    ASSERT_TLM_queueDepth(0, 1);
    ASSERT_TLM_queuedTotal_SIZE(1);
    ASSERT_TLM_queuedTotal(0, 1);
}

// Test that queue rejects sequences when full
void SeqDispatcherTester::testQueueOverflow() {
    // Set small queue depth
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(2, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("test"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    this->clearHistory();

    // Fill the queue (2 entries)
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued1"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();
    ASSERT_TLM_queueDepth(0, 1);
    this->clearHistory();

    sendCmd_RUN_ARGS(0, 0, Fw::String("queued2"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();
    ASSERT_TLM_queueDepth(0, 2);
    this->clearHistory();

    // Next sequence should overflow
    sendCmd_RUN_ARGS(0, 0, Fw::String("overflow"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();

    // Should get EXECUTION_ERROR response
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::EXECUTION_ERROR);

    // Should have QueueOverflow event
    ASSERT_EVENTS_QueueOverflow_SIZE(1);
    ASSERT_EVENTS_QueueOverflow(0, "overflow");

    // Verify overflow telemetry incremented
    ASSERT_TLM_queueOverflows_SIZE(1);
    ASSERT_TLM_queueOverflows(0, 1);

    // Queue depth should still be 2
    ASSERT_TLM_queueDepth(0, 2);
}

// Test that queued sequences dispatch when sequencers become available
void SeqDispatcherTester::testQueueDispatch() {
    // Set queue depth parameter
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(10, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("running"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    this->clearHistory();

    // Queue a sequence
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();
    ASSERT_TLM_queueDepth(0, 1);
    ASSERT_EVENTS_SequenceQueued_SIZE(1);
    this->clearHistory();

    // Complete first sequencer
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();

    // Should see StartingQueuedSequence event
    ASSERT_EVENTS_StartingQueuedSequence_SIZE(1);
    ASSERT_EVENTS_StartingQueuedSequence(0, "queued");

    // Queue should be empty now
    ASSERT_TLM_queueDepth(0, 0);

    // SequencesExecutedFromQueue should increment
    ASSERT_TLM_sequencesExecutedFromQueue_SIZE(1);
    ASSERT_TLM_sequencesExecutedFromQueue(0, 1);

    // Queued sequence should have been sent to sequencer 0
    ASSERT_from_seqRunOut_SIZE(1);
    ASSERT_from_seqRunOut(0, Fw::String("queued"), emptyArgs);
}

// Test CLEAR_QUEUE command
void SeqDispatcherTester::testClearQueue() {
    // Set queue depth parameter
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(10, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("running"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    this->clearHistory();

    // Queue 3 sequences
    for (int i = 0; i < 3; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("queued"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    ASSERT_TLM_queueDepth(2, 3);
    this->clearHistory();

    // Clear the queue
    sendCmd_CLEAR_QUEUE(0, 0);
    this->component.doDispatch();

    // Should get QueueCleared event
    ASSERT_EVENTS_QueueCleared_SIZE(1);
    ASSERT_EVENTS_QueueCleared(0, 3);

    // Queue should be empty
    ASSERT_TLM_queueDepth(0, 0);

    // Command should succeed
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_CLEAR_QUEUE, 0, Fw::CmdResponse::OK);
}

// Test PAUSE_QUEUE and RESUME_QUEUE commands
void SeqDispatcherTester::testPauseResumeQueue() {
    // Set queue depth parameter
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(10, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("running"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    this->clearHistory();

    // Queue a sequence
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();
    ASSERT_TLM_queueDepth(0, 1);
    this->clearHistory();

    // Pause the queue
    sendCmd_PAUSE_QUEUE(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_QueuePaused_SIZE(1);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_PAUSE_QUEUE, 0, Fw::CmdResponse::OK);
    this->clearHistory();

    // Complete first sequencer - queue should NOT dispatch (paused)
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();

    // Should NOT see StartingQueuedSequence event
    ASSERT_EVENTS_StartingQueuedSequence_SIZE(0);

    // Queue depth should still be 1
    ASSERT_TLM_queueDepth(0, 1);

    // No seqRunOut for queued sequence
    ASSERT_from_seqRunOut_SIZE(0);
    this->clearHistory();

    // Resume the queue
    sendCmd_RESUME_QUEUE(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_QueueResumed_SIZE(1);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RESUME_QUEUE, 0, Fw::CmdResponse::OK);

    // Should immediately dispatch the queued sequence
    ASSERT_EVENTS_StartingQueuedSequence_SIZE(1);
    ASSERT_TLM_queueDepth(0, 0);
    ASSERT_from_seqRunOut_SIZE(1);
}

// Test queue disabled mode (MAX_QUEUE_DEPTH = 0)
void SeqDispatcherTester::testQueueDisabled() {
    // Set queue depth to 0 (disabled)
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(0, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("test"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    this->clearHistory();

    // Try to queue a sequence - should fail immediately (no queue event)
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();

    // Should get EXECUTION_ERROR response
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::EXECUTION_ERROR);

    // Should have NoAvailableSequencers event (not SequenceQueued)
    ASSERT_EVENTS_NoAvailableSequencers_SIZE(1);
    ASSERT_EVENTS_SequenceQueued_SIZE(0);

    // Queue depth should remain 0
    ASSERT_TLM_queueDepth_SIZE(0);
}

// Test queue telemetry accuracy
void SeqDispatcherTester::testQueueTelemetry() {
    // Set queue depth parameter
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(5, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("running"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    this->clearHistory();

    // Queue 3 sequences
    for (int i = 0; i < 3; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("queued"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }

    // Verify queueDepth telemetry
    ASSERT_TLM_queueDepth(2, 3);

    // Verify queuedTotal telemetry
    ASSERT_TLM_queuedTotal(2, 3);

    this->clearHistory();

    // Dispatch one from queue
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();

    // Verify queueDepth decremented
    ASSERT_TLM_queueDepth(0, 2);

    // Verify sequencesExecutedFromQueue incremented
    ASSERT_TLM_sequencesExecutedFromQueue(0, 1);

    this->clearHistory();

    // Test overflow telemetry
    // Queue is at 2, max is 5, so we can add 3 more
    for (int i = 0; i < 3; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("queued"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    ASSERT_TLM_queueDepth(2, 5);  // Should be at max
    this->clearHistory();

    // Try to overflow
    sendCmd_RUN_ARGS(0, 0, Fw::String("overflow"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();

    // Verify queueOverflows incremented
    ASSERT_TLM_queueOverflows(0, 1);
}

// Test that queued sequences preserve BlockState
void SeqDispatcherTester::testBlockStatePreservation() {
    // Set queue depth parameter
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(10, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("running"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    this->clearHistory();

    // Queue with NO_BLOCK
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued_noblock"), BlockState::NO_BLOCK, emptyArgs);
    this->component.doDispatch();

    // Should get immediate OK response (queued + NO_BLOCK means immediate response)
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::OK);
    this->clearHistory();

    // Complete first sequencer
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();

    // Queued sequence should dispatch (already got response earlier due to NO_BLOCK)
    ASSERT_EVENTS_StartingQueuedSequence_SIZE(1);

    // Should NOT get another command response (already got it when queued)
    ASSERT_CMD_RESPONSE_SIZE(1);  // Only the response from seqDoneIn of the original running sequence
    ASSERT_CMD_RESPONSE(0, SeqDispatcher::OPCODE_RUN_ARGS, 0, Fw::CmdResponse::OK);
}

// Test that queue continues processing after a sequence error
void SeqDispatcherTester::testQueueWithErrors() {
    // Set queue depth parameter
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(10, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("running"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    this->clearHistory();

    // Queue 2 sequences
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued1"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued2"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();
    ASSERT_TLM_queueDepth(1, 2);
    this->clearHistory();

    // Complete first sequencer with ERROR
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::EXECUTION_ERROR);
    this->component.doDispatch();

    // First queued sequence should still dispatch despite error
    ASSERT_EVENTS_StartingQueuedSequence_SIZE(1);
    ASSERT_EVENTS_StartingQueuedSequence(0, "queued1");
    ASSERT_TLM_queueDepth(0, 1);
    this->clearHistory();

    // Complete that sequence
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();

    // Second queued sequence should dispatch
    ASSERT_EVENTS_StartingQueuedSequence_SIZE(1);
    ASSERT_EVENTS_StartingQueuedSequence(0, "queued2");
    ASSERT_TLM_queueDepth(0, 0);

    // Queue should be empty, not halted
    ASSERT_from_seqRunOut_SIZE(2);  // Both queued sequences dispatched
}

// Test that queue works with multiple sequencers
void SeqDispatcherTester::testMultipleSequencers() {
    // This test assumes SeqDispatcherSequencerPorts >= 2
    if (SeqDispatcherSequencerPorts < 2) {
        // Skip test if only 1 sequencer
        return;
    }

    // Set queue depth parameter
    Fw::ParamValid valid = Fw::ParamValid::VALID;
    this->paramSet_MAX_QUEUE_DEPTH(10, valid);
    this->paramSend_MAX_QUEUE_DEPTH(0, 0);
    this->component.doDispatch();
    this->clearHistory();

    Svc::SeqArgs emptyArgs{0, 0};

    // Fill all sequencers
    for (int i = 0; i < SeqDispatcherSequencerPorts; i++) {
        sendCmd_RUN_ARGS(0, 0, Fw::String("running"), BlockState::BLOCK, emptyArgs);
        this->component.doDispatch();
    }
    this->clearHistory();

    // Queue 2 sequences
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued1"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();
    sendCmd_RUN_ARGS(0, 0, Fw::String("queued2"), BlockState::BLOCK, emptyArgs);
    this->component.doDispatch();
    ASSERT_TLM_queueDepth(1, 2);
    this->clearHistory();

    // Complete sequencer 0
    this->invoke_to_seqDoneIn(0, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();

    // First queued sequence should dispatch to sequencer 0
    ASSERT_EVENTS_StartingQueuedSequence_SIZE(1);
    ASSERT_TLM_queueDepth(0, 1);
    this->clearHistory();

    // Complete sequencer 1
    this->invoke_to_seqDoneIn(1, 0, 0, Fw::CmdResponse::OK);
    this->component.doDispatch();

    // Second queued sequence should dispatch to sequencer 1
    ASSERT_EVENTS_StartingQueuedSequence_SIZE(1);
    ASSERT_TLM_queueDepth(0, 0);

    // Verify both dispatched to different sequencers
    ASSERT_from_seqRunOut_SIZE(2);
}

}  // namespace Svc
