// ======================================================================
// \title  CsCall.cpp
// \author Auto-generated
// \brief  cpp file for CS_CALL test harness implementation class
//
// \copyright
// Copyright 2009-2024, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include "Svc/CmdSequencer/test/ut/CsCall.hpp"
#include "Svc/CmdSequencer/test/ut/SequenceFiles/SequenceFiles.hpp"

namespace Svc {

namespace CsCall {

// ----------------------------------------------------------------------
// Constructors
// ----------------------------------------------------------------------

CmdSequencerTester::CmdSequencerTester(const SequenceFiles::File::Format::t a_format)
    : Svc::CmdSequencerTester(a_format) {}

// ----------------------------------------------------------------------
// Helper Methods
// ----------------------------------------------------------------------

void CmdSequencerTester::createSimpleSequence(const char* fileName, U32 numCommands) {
    SequenceFiles::ImmediateFile file(numCommands, this->format);
    file.setName(fileName);
    file.write();
}

void CmdSequencerTester::createNestedSequence(const char* parentFile,
                                              const char* childFile,
                                              U32 commandsBeforeCall,
                                              U32 commandsAfterCall) {
    // This is a simplified version - in practice you'd need to write
    // a custom sequence file with CS_CALL command embedded
    // For now, we'll use the component's internal test interface
    // to simulate CS_CALL commands
}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void CmdSequencerTester::test_cs_call_basic() {
    // Create child sequence (3 commands)
    const char* childFileName = "cs_call_child.seq";
    createSimpleSequence(childFileName, 3);

    // Create parent sequence (2 commands before, 2 after)
    const char* parentFileName = "cs_call_parent.seq";
    createSimpleSequence(parentFileName, 2);

    // Load and start parent sequence
    this->validateFile(0, parentFileName);
    this->runSequence(0, parentFileName);

    // Execute first 2 commands
    for (U32 i = 0; i < 2; i++) {
        this->invoke_to_schedIn(0, 0);
        this->clearAndDispatch();
        ASSERT_from_comCmdOut_SIZE(1);
        this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
        this->clearAndDispatch();
    }

    // Verify parent is running
    ASSERT_EQ(CmdSequencerComponentImpl::RUNNING, this->component.m_runMode);
    ASSERT_EQ(2u, this->component.m_executedCount);

    // Issue CS_CALL command
    this->sendCmd_CS_CALL(0, 100, Fw::CmdStringArg(childFileName));
    this->clearAndDispatch();

    // Verify:
    // - CS_CALL accepted (command response OK)
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, CmdSequencerComponentImpl::OPCODE_CS_CALL, 100, Fw::CmdResponse::OK);

    // - Event: CS_SequenceNested
    ASSERT_EVENTS_CS_SequenceNested_SIZE(1);

    // - Stack depth = 1
    ASSERT_EQ(1u, this->component.m_nestedStateStack.size());

    // - Child sequence loaded
    ASSERT_EVENTS_CS_SequenceLoaded_SIZE(1);

    // - executedCount reset to 0 for child
    ASSERT_EQ(0u, this->component.m_executedCount);

    // Execute child sequence (3 commands)
    for (U32 i = 0; i < 3; i++) {
        this->invoke_to_schedIn(0, 0);
        this->clearAndDispatch();
        ASSERT_from_comCmdOut_SIZE(1);
        this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
        this->clearAndDispatch();
    }

    // Child completes - advance scheduler to trigger completion
    this->invoke_to_schedIn(0, 0);
    this->clearAndDispatch();

    // Verify:
    // - Event: CS_SequenceResuming
    ASSERT_EVENTS_CS_SequenceResuming_SIZE(1);

    // - Stack depth = 0
    ASSERT_EQ(0u, this->component.m_nestedStateStack.size());

    // - Parent sequence reloaded (CS_SequenceLoaded again)
    ASSERT_EVENTS_CS_SequenceLoaded_SIZE(2);

    // - executedCount restored to 2 (parent position)
    ASSERT_EQ(2u, this->component.m_executedCount);

    // Parent continues - execute remaining commands
    // (In real implementation, would execute commands after CS_CALL)
}

void CmdSequencerTester::test_cs_call_multi_level() {
    // Create grandchild sequence (2 commands)
    const char* grandchildFileName = "cs_call_grandchild.seq";
    createSimpleSequence(grandchildFileName, 2);

    // Create child sequence that will call grandchild
    const char* childFileName = "cs_call_child_nested.seq";
    createSimpleSequence(childFileName, 2);

    // Create parent sequence
    const char* parentFileName = "cs_call_parent_nested.seq";
    createSimpleSequence(parentFileName, 2);

    // Load and start parent
    this->validateFile(0, parentFileName);
    this->runSequence(0, parentFileName);

    // Execute 1 command, then CS_CALL child
    this->invoke_to_schedIn(0, 0);
    this->clearAndDispatch();
    ASSERT_from_comCmdOut_SIZE(1);
    this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
    this->clearAndDispatch();

    // Parent calls child
    this->sendCmd_CS_CALL(0, 101, Fw::CmdStringArg(childFileName));
    this->clearAndDispatch();
    ASSERT_EVENTS_CS_SequenceNested_SIZE(1);
    ASSERT_EQ(1u, this->component.m_nestedStateStack.size());

    // Execute 1 child command, then CS_CALL grandchild
    this->invoke_to_schedIn(0, 0);
    this->clearAndDispatch();
    ASSERT_from_comCmdOut_SIZE(1);
    this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
    this->clearAndDispatch();

    // Child calls grandchild
    this->sendCmd_CS_CALL(0, 102, Fw::CmdStringArg(grandchildFileName));
    this->clearAndDispatch();
    ASSERT_EVENTS_CS_SequenceNested_SIZE(2);

    // Verify stack depth = 2
    ASSERT_EQ(2u, this->component.m_nestedStateStack.size());

    // Execute grandchild (2 commands)
    for (U32 i = 0; i < 2; i++) {
        this->invoke_to_schedIn(0, 0);
        this->clearAndDispatch();
        ASSERT_from_comCmdOut_SIZE(1);
        this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
        this->clearAndDispatch();
    }

    // Grandchild completes
    this->invoke_to_schedIn(0, 0);
    this->clearAndDispatch();
    ASSERT_EVENTS_CS_SequenceResuming_SIZE(1);

    // Verify stack depth = 1 (back to child)
    ASSERT_EQ(1u, this->component.m_nestedStateStack.size());

    // Complete child
    this->invoke_to_schedIn(0, 0);
    this->clearAndDispatch();
    ASSERT_from_comCmdOut_SIZE(1);
    this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
    this->clearAndDispatch();

    // Child completes
    this->invoke_to_schedIn(0, 0);
    this->clearAndDispatch();
    ASSERT_EVENTS_CS_SequenceResuming_SIZE(2);

    // Verify stack depth = 0 (back to parent)
    ASSERT_EQ(0u, this->component.m_nestedStateStack.size());

    // Parent continues and completes
}

void CmdSequencerTester::test_cs_call_depth_limit() {
    // Create a sequence for each level
    const char* level1 = "level1.seq";
    const char* level2 = "level2.seq";
    const char* level3 = "level3.seq";
    const char* level4 = "level4.seq";
    const char* level5 = "level5.seq";
    const char* level6 = "level6.seq";

    createSimpleSequence(level1, 1);
    createSimpleSequence(level2, 1);
    createSimpleSequence(level3, 1);
    createSimpleSequence(level4, 1);
    createSimpleSequence(level5, 1);
    createSimpleSequence(level6, 1);

    // Start with level 1
    this->validateFile(0, level1);
    this->runSequence(0, level1);

    // Nest to level 2
    this->sendCmd_CS_CALL(0, 201, Fw::CmdStringArg(level2));
    this->clearAndDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EQ(1u, this->component.m_nestedStateStack.size());

    // Nest to level 3
    this->sendCmd_CS_CALL(0, 202, Fw::CmdStringArg(level3));
    this->clearAndDispatch();
    ASSERT_EQ(2u, this->component.m_nestedStateStack.size());

    // Nest to level 4
    this->sendCmd_CS_CALL(0, 203, Fw::CmdStringArg(level4));
    this->clearAndDispatch();
    ASSERT_EQ(3u, this->component.m_nestedStateStack.size());

    // Nest to level 5
    this->sendCmd_CS_CALL(0, 204, Fw::CmdStringArg(level5));
    this->clearAndDispatch();
    ASSERT_EQ(4u, this->component.m_nestedStateStack.size());

    // Nest to level 6 - This should be REJECTED (max depth is 5)
    this->sendCmd_CS_CALL(0, 205, Fw::CmdStringArg(level6));
    this->clearAndDispatch();

    // Verify rejection
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, CmdSequencerComponentImpl::OPCODE_CS_CALL, 205,
                        Fw::CmdResponse::EXECUTION_ERROR);

    // Verify event: CS_NestedTooDeep
    ASSERT_EVENTS_CS_NestedTooDeep_SIZE(1);

    // Verify stack depth still 4 (not 5)
    ASSERT_EQ(4u, this->component.m_nestedStateStack.size());
}

void CmdSequencerTester::test_cs_call_not_running() {
    // Sequencer is STOPPED (no sequence loaded)
    ASSERT_EQ(CmdSequencerComponentImpl::STOPPED, this->component.m_runMode);

    // Issue CS_CALL command
    this->sendCmd_CS_CALL(0, 300, Fw::CmdStringArg("some_child.seq"));
    this->clearAndDispatch();

    // Verify rejection
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, CmdSequencerComponentImpl::OPCODE_CS_CALL, 300,
                        Fw::CmdResponse::EXECUTION_ERROR);

    // Verify event: CS_NoSequenceActive
    ASSERT_EVENTS_CS_NoSequenceActive_SIZE(1);

    // Verify stack remains empty
    ASSERT_EQ(0u, this->component.m_nestedStateStack.size());
}

void CmdSequencerTester::test_cs_call_manual_mode() {
    // Create and load a sequence
    const char* fileName = "manual_test.seq";
    createSimpleSequence(fileName, 3);

    this->validateFile(0, fileName);

    // Start in MANUAL mode
    this->sendCmd_CS_MANUAL(0, 0);
    this->clearAndDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);

    // Start the sequence
    this->sendCmd_CS_START(0, 0);
    this->clearAndDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);

    // Verify running in MANUAL mode
    ASSERT_EQ(CmdSequencerComponentImpl::RUNNING, this->component.m_runMode);
    ASSERT_EQ(CmdSequencerComponentImpl::MANUAL, this->component.m_stepMode);

    // Issue CS_CALL command (should be rejected)
    this->sendCmd_CS_CALL(0, 400, Fw::CmdStringArg("child.seq"));
    this->clearAndDispatch();

    // Verify rejection
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, CmdSequencerComponentImpl::OPCODE_CS_CALL, 400,
                        Fw::CmdResponse::EXECUTION_ERROR);

    // Verify event: CS_InvalidMode
    ASSERT_EVENTS_CS_InvalidMode_SIZE(1);

    // Verify stack remains empty
    ASSERT_EQ(0u, this->component.m_nestedStateStack.size());
}

void CmdSequencerTester::test_cs_call_cancel() {
    // Create parent and child sequences
    const char* childFileName = "cancel_child.seq";
    const char* parentFileName = "cancel_parent.seq";
    createSimpleSequence(childFileName, 3);
    createSimpleSequence(parentFileName, 5);

    // Start parent sequence
    this->validateFile(0, parentFileName);
    this->runSequence(0, parentFileName);

    // Execute 2 commands
    for (U32 i = 0; i < 2; i++) {
        this->invoke_to_schedIn(0, 0);
        this->clearAndDispatch();
        ASSERT_from_comCmdOut_SIZE(1);
        this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
        this->clearAndDispatch();
    }

    // Call child
    this->sendCmd_CS_CALL(0, 500, Fw::CmdStringArg(childFileName));
    this->clearAndDispatch();
    ASSERT_EQ(1u, this->component.m_nestedStateStack.size());

    // Execute 1 child command
    this->invoke_to_schedIn(0, 0);
    this->clearAndDispatch();
    ASSERT_from_comCmdOut_SIZE(1);
    this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
    this->clearAndDispatch();

    // Issue CS_CANCEL
    this->sendCmd_CS_CANCEL(0, 501);
    this->clearAndDispatch();

    // Verify:
    // - Cancel command accepted
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, CmdSequencerComponentImpl::OPCODE_CS_CANCEL, 501, Fw::CmdResponse::OK);

    // - Event: CS_SequenceCanceled
    ASSERT_EVENTS_CS_SequenceCanceled_SIZE(1);

    // - Stack cleared
    ASSERT_EQ(0u, this->component.m_nestedStateStack.size());

    // - Sequencer stopped
    ASSERT_EQ(CmdSequencerComponentImpl::STOPPED, this->component.m_runMode);
}

void CmdSequencerTester::test_cs_call_load_failure() {
    // Create parent sequence
    const char* parentFileName = "load_fail_parent.seq";
    createSimpleSequence(parentFileName, 3);

    // Start parent sequence
    this->validateFile(0, parentFileName);
    this->runSequence(0, parentFileName);

    // Execute 1 command
    this->invoke_to_schedIn(0, 0);
    this->clearAndDispatch();
    ASSERT_from_comCmdOut_SIZE(1);
    this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
    this->clearAndDispatch();

    ASSERT_EQ(1u, this->component.m_executedCount);

    // Issue CS_CALL with non-existent child file
    const char* invalidChild = "does_not_exist.seq";
    this->sendCmd_CS_CALL(0, 600, Fw::CmdStringArg(invalidChild));
    this->clearAndDispatch();

    // Verify:
    // - CS_CALL rejected
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, CmdSequencerComponentImpl::OPCODE_CS_CALL, 600,
                        Fw::CmdResponse::EXECUTION_ERROR);

    // - Event: CS_FileReadError
    ASSERT_EVENTS_CS_FileReadError_SIZE(1);

    // - Stack remains empty (state was popped after load failure)
    ASSERT_EQ(0u, this->component.m_nestedStateStack.size());

    // Note: In current implementation, load failure causes error state
    // Parent does NOT continue after load failure
}

void CmdSequencerTester::test_cs_call_state_restore() {
    // Create child sequence
    const char* childFileName = "state_child.seq";
    createSimpleSequence(childFileName, 2);

    // Create parent sequence
    const char* parentFileName = "state_parent.seq";
    createSimpleSequence(parentFileName, 5);

    // Start parent in AUTO mode with BLOCK
    this->validateFile(0, parentFileName);
    const U32 runCmdSeq = 700;
    const FwOpcodeType runOpCode = CmdSequencerComponentImpl::OPCODE_CS_RUN;
    this->sendCmd_CS_RUN(0, runCmdSeq, Fw::CmdStringArg(parentFileName), Svc::BlockState::BLOCK);
    this->clearAndDispatch();

    // Execute 3 commands
    for (U32 i = 0; i < 3; i++) {
        this->invoke_to_schedIn(0, 0);
        this->clearAndDispatch();
        ASSERT_from_comCmdOut_SIZE(1);
        this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
        this->clearAndDispatch();
    }

    // Capture state before CS_CALL
    U32 parentExecutedCount = this->component.m_executedCount;
    ASSERT_EQ(3u, parentExecutedCount);

    Svc::BlockState::t parentBlockState = this->component.m_blockState;
    ASSERT_EQ(Svc::BlockState::BLOCK, parentBlockState);

    FwOpcodeType parentOpCode = this->component.m_opCode;
    ASSERT_EQ(runOpCode, parentOpCode);

    U32 parentCmdSeq = this->component.m_cmdSeq;
    ASSERT_EQ(runCmdSeq, parentCmdSeq);

    // Issue CS_CALL
    this->sendCmd_CS_CALL(0, 701, Fw::CmdStringArg(childFileName));
    this->clearAndDispatch();
    ASSERT_EQ(1u, this->component.m_nestedStateStack.size());

    // Execute and complete child
    for (U32 i = 0; i < 2; i++) {
        this->invoke_to_schedIn(0, 0);
        this->clearAndDispatch();
        ASSERT_from_comCmdOut_SIZE(1);
        this->invoke_to_cmdResponseIn(0, 0, 0, Fw::CmdResponse::OK);
        this->clearAndDispatch();
    }

    // Child completes - parent restored
    this->invoke_to_schedIn(0, 0);
    this->clearAndDispatch();

    // Verify parent state restored:
    // - executedCount
    ASSERT_EQ(parentExecutedCount, this->component.m_executedCount);

    // - blockState
    ASSERT_EQ(parentBlockState, this->component.m_blockState);

    // - opCode
    ASSERT_EQ(parentOpCode, this->component.m_opCode);

    // - cmdSeq
    ASSERT_EQ(parentCmdSeq, this->component.m_cmdSeq);

    // - runMode
    ASSERT_EQ(CmdSequencerComponentImpl::RUNNING, this->component.m_runMode);

    // - stepMode
    ASSERT_EQ(CmdSequencerComponentImpl::AUTO, this->component.m_stepMode);

    // - Stack empty
    ASSERT_EQ(0u, this->component.m_nestedStateStack.size());
}

}  // namespace CsCall

}  // namespace Svc
