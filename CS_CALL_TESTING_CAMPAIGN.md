# CS_CALL Testing Campaign

## Overview

This document provides a comprehensive testing campaign for the CS_CALL nested sequencing feature in CmdSequencer. CS_CALL enables true nested sequence execution by pausing the parent sequence, loading a child sequence on the same sequencer, and resuming the parent after the child completes.

**Implementation Status:** Complete (C++ code + FPP definitions)

**Test Environment:** F Prime CmdSequencer component

---

## Test Environment Setup

### Prerequisites

1. Build CmdSequencer with CS_CALL changes:
   ```bash
   cd Svc/CmdSequencer
   fprime-util build
   ```

2. Run unit tests:
   ```bash
   fprime-util check
   ```

3. For integration testing, build a deployment (e.g., Ref):
   ```bash
   cd Ref
   fprime-util build
   fprime-util run
   ```

### Test Sequences to Create

Create these test sequences in your sequence directory:

**`parent.seq`** (calls child sequence):
```
; Parent sequence
R00:00:00.000 NO_OP
R00:00:00.500 NO_OP
R00:00:01.000 cmdSeq.CS_CALL child.seq
R00:00:01.100 NO_OP  ; Should execute AFTER child completes
R00:00:02.000 NO_OP
```

**`child.seq`** (simple child):
```
; Child sequence
R00:00:00.000 NO_OP
R00:00:00.500 NO_OP
R00:00:01.000 NO_OP
```

**`multi_level_parent.seq`** (calls child that calls grandchild):
```
; Multi-level parent
R00:00:00.000 NO_OP
R00:00:01.000 cmdSeq.CS_CALL multi_level_child.seq
R00:00:01.100 NO_OP  ; After child returns
```

**`multi_level_child.seq`** (child that calls grandchild):
```
; Child that calls grandchild
R00:00:00.000 NO_OP
R00:00:00.500 cmdSeq.CS_CALL grandchild.seq
R00:00:00.600 NO_OP  ; After grandchild returns
```

**`grandchild.seq`**:
```
; Grandchild sequence
R00:00:00.000 NO_OP
R00:00:00.200 NO_OP
```

**`recursive.seq`** (calls itself - for depth limit testing):
```
; Recursive sequence
R00:00:00.000 NO_OP
R00:00:00.100 cmdSeq.CS_CALL recursive.seq
```

**`error_child.seq`** (contains invalid command):
```
; Child with error
R00:00:00.000 NO_OP
R00:00:00.500 INVALID_COMMAND_DOESNT_EXIST
R00:00:01.000 NO_OP
```

**`timer_parent.seq`** (tests relative timing across nesting):
```
; Parent with relative timing
R00:00:00.000 NO_OP
R00:00:02.000 cmdSeq.CS_CALL timer_child.seq
R00:00:02.100 NO_OP  ; Should be ~5s from start (2s + 3s child)
R00:00:05.000 NO_OP  ; Should be ~8s from start
```

**`timer_child.seq`** (child with relative timing):
```
; Child with relative timing
R00:00:00.000 NO_OP
R00:00:01.500 NO_OP
R00:00:03.000 NO_OP  ; 3s duration total
```

---

## Unit Test Suite

### Test 1: Basic Single-Level Nesting

**Test Name:** `testCsCallBasic`

**Objective:** Verify parent pauses, child executes, parent resumes

**Steps:**
1. Load parent sequence
2. Start execution in AUTO mode
3. Execute until CS_CALL command
4. Verify parent state pushed to stack
5. Verify child sequence loaded
6. Execute child to completion
7. Verify parent state restored
8. Verify parent continues from correct position

**Expected Results:**
- ✅ Event: `CS_SequenceNested(child.seq, 1)`
- ✅ Stack depth = 1 after CS_CALL
- ✅ Child sequence loads and executes
- ✅ Event: `CS_SequenceResuming(parent.seq, child.seq, 0)`
- ✅ Stack depth = 0 after child completes
- ✅ Parent continues with next command (not from beginning)
- ✅ Parent m_executedCount preserved
- ✅ Both sequences complete successfully

**Verification Points:**
- Parent fileName saved correctly
- Parent executedCount = number of commands before CS_CALL
- Child starts with executedCount = 0
- Parent resumes with correct executedCount

---

### Test 2: Multi-Level Nesting (3 Levels)

**Test Name:** `testCsCallMultiLevel`

**Objective:** Verify parent → child → grandchild nesting

**Steps:**
1. Load multi_level_parent
2. Execute until parent calls child (CS_CALL)
3. Verify stack depth = 1
4. Execute until child calls grandchild (CS_CALL)
5. Verify stack depth = 2
6. Execute grandchild to completion
7. Verify child resumes (stack depth = 1)
8. Execute child to completion
9. Verify parent resumes (stack depth = 0)
10. Execute parent to completion

**Expected Results:**
- ✅ Event: `CS_SequenceNested(multi_level_child.seq, 1)` (parent calls child)
- ✅ Event: `CS_SequenceNested(grandchild.seq, 2)` (child calls grandchild)
- ✅ Stack depth: 0 → 1 → 2 → 1 → 0
- ✅ Event: `CS_SequenceResuming(multi_level_child.seq, grandchild.seq, 1)` (child resumes)
- ✅ Event: `CS_SequenceResuming(multi_level_parent.seq, multi_level_child.seq, 0)` (parent resumes)
- ✅ All three sequences complete in correct order
- ✅ Each sequence resumes at correct position

**Verification Points:**
- Stack contains 2 entries at deepest point
- Each level restores correct state
- Execution order: P1 → P2 → C1 → C2 → G1 → G2 → C3 → P3
- Timer states preserved at each level

---

### Test 3: Nesting Depth Limit (5 Levels Max)

**Test Name:** `testCsCallDepthLimit`

**Objective:** Verify maximum nesting depth enforced

**Steps:**
1. Load recursive sequence (calls itself)
2. Execute until 5th level of nesting
3. Attempt 6th level CS_CALL
4. Verify rejection

**Expected Results:**
- ✅ First 5 CS_CALL commands: Success
- ✅ Stack depth reaches 5
- ✅ 6th CS_CALL: Command response = EXECUTION_ERROR
- ✅ Event: `CS_NestedTooDeep(5)`
- ✅ Stack depth remains 5 (not 6)
- ✅ Sequences unwind correctly (5 → 4 → 3 → 2 → 1 → 0)

**Verification Points:**
- MAX_NESTING_DEPTH constant = 5
- Stack size never exceeds 5
- Error doesn't corrupt stack state
- Sequences complete after rejection

---

### Test 4: Child Sequence Error Propagation

**Test Name:** `testCsCallChildError`

**Objective:** Verify child errors abort parent

**Steps:**
1. Load parent that calls error_child
2. Execute until CS_CALL
3. Execute child until error occurs
4. Verify both child and parent abort

**Expected Results:**
- ✅ Parent calls child successfully
- ✅ Child sequence loads and starts
- ✅ Child encounters invalid command
- ✅ Child sequence aborts with error
- ✅ Parent sequence does NOT resume
- ✅ Stack cleared (depth = 0)
- ✅ Sequencer returns to STOPPED state
- ✅ Error event logged

**Verification Points:**
- Stack cleared on child error
- Parent does not execute commands after CS_CALL
- m_runMode = STOPPED after error
- Telemetry: error count incremented

---

### Test 5: CS_CANCEL with Nested Sequences

**Test Name:** `testCsCallCancel`

**Objective:** Verify CS_CANCEL clears entire stack

**Steps:**
1. Load parent sequence with nested calls
2. Execute to 2-level nesting (parent → child active)
3. Issue CS_CANCEL command
4. Verify stack cleared and all sequences aborted

**Expected Results:**
- ✅ Before cancel: Stack depth = 2
- ✅ CS_CANCEL issued
- ✅ Event: `CS_SequenceCanceled`
- ✅ Stack cleared: depth = 0
- ✅ Child sequence aborted
- ✅ Parent sequences NOT resumed
- ✅ Sequencer state: STOPPED
- ✅ m_nestedStateStack.empty() = true

**Verification Points:**
- `while (!m_nestedStateStack.empty()) pop()` executed
- No parent sequences resume after cancel
- Clean state for next sequence

---

### Test 6: State Restore Accuracy

**Test Name:** `testCsCallStateRestore`

**Objective:** Verify all parent state variables restored correctly

**Steps:**
1. Load parent sequence with specific state:
   - Set timer values
   - Set specific opCode/cmdSeq
   - Set blockState
2. Execute until CS_CALL
3. Capture parent state before CS_CALL
4. Execute child
5. Verify parent state after restore

**Expected Results:**
- ✅ Parent fileName matches original
- ✅ Parent executedCount preserved
- ✅ Parent opCode/cmdSeq preserved
- ✅ Parent blockState preserved
- ✅ Parent runMode = RUNNING
- ✅ Parent stepMode preserved (AUTO)
- ✅ Parent timers restored
- ✅ Parent record restored
- ✅ Sequence file reloaded from disk
- ✅ Fast-forward to correct position

**Verification Points:**
```cpp
// Verify each field in SequenceState
state.fileName == expected
state.executedCount == expected
state.opCode == expected
state.cmdSeq == expected
state.blockState == expected
state.runMode == RUNNING
state.stepMode == AUTO
state.cmdTimer == expected
state.cmdTimeoutTimer == expected
```

---

### Test 7: CS_CALL in MANUAL Mode (Should Reject)

**Test Name:** `testCsCallManualMode`

**Objective:** Verify CS_CALL requires AUTO mode

**Steps:**
1. Load sequence
2. Start in MANUAL mode (CS_MANUAL)
3. Step to CS_CALL command
4. Attempt to execute CS_CALL
5. Verify rejection

**Expected Results:**
- ✅ CS_CALL: Command response = EXECUTION_ERROR
- ✅ Event: `CS_InvalidMode("CS_CALL")`
- ✅ Sequence does NOT nest
- ✅ Stack remains empty
- ✅ Parent continues in MANUAL mode

---

### Test 8: CS_CALL When Not Running (Should Reject)

**Test Name:** `testCsCallNotRunning`

**Objective:** Verify CS_CALL requires active sequence

**Steps:**
1. Sequencer in STOPPED state (no sequence loaded)
2. Issue CS_CALL command directly
3. Verify rejection

**Expected Results:**
- ✅ CS_CALL: Command response = EXECUTION_ERROR
- ✅ Event: `CS_NoSequenceActive`
- ✅ No child sequence loaded
- ✅ Stack remains empty

---

### Test 9: Child Sequence Load Failure

**Test Name:** `testCsCallLoadFailure`

**Objective:** Verify parent restored if child fails to load

**Steps:**
1. Load parent sequence
2. Execute until CS_CALL with invalid child filename
3. Verify parent state restored (popped from stack)
4. Verify parent continues

**Expected Results:**
- ✅ Parent state pushed to stack
- ✅ Child load fails (file not found)
- ✅ Event: `CS_FileReadError(invalid_child.seq)`
- ✅ Parent state popped from stack
- ✅ Stack depth back to 0
- ✅ CS_CALL: Command response = EXECUTION_ERROR
- ✅ Parent sequence does NOT continue (error state)

**Verification Points:**
- Stack pop on load failure prevents memory leak
- Error handling doesn't corrupt state

---

### Test 10: Relative Timing Across Nesting

**Test Name:** `testCsCallRelativeTiming`

**Objective:** Verify timers behave correctly across nesting

**Steps:**
1. Load timer_parent sequence (with relative time commands)
2. Execute with real time progression
3. Verify child execution time doesn't affect parent timers
4. Verify parent resumes with correct timing

**Expected Results:**
- ✅ Parent command at R00:00:02.000 executes ~2s from parent start
- ✅ CS_CALL occurs at 2s
- ✅ Child executes (takes 3s)
- ✅ Parent resumes at ~5s total elapsed
- ✅ Parent command at R00:00:02.100 executes immediately after resume
- ✅ Parent command at R00:00:05.000 executes ~3s after resume (8s total)

**Verification Points:**
- Parent cmdTimer saved and restored
- Parent cmdTimeoutTimer saved and restored
- Child timer state independent
- Relative timing relative to parent start, not child

---

### Test 11: BlockState Preservation

**Test Name:** `testCsCallBlockState`

**Objective:** Verify parent blockState preserved across nesting

**Steps:**
1. Load parent sequence with BLOCK mode
2. Execute until CS_CALL
3. Verify parent blockState saved
4. Execute child
5. Verify parent blockState restored

**Expected Results:**
- ✅ Parent blockState = BLOCK before CS_CALL
- ✅ Parent blockState saved in stack
- ✅ Child blockState = NO_BLOCK (default)
- ✅ Parent blockState = BLOCK after restore

---

### Test 12: Command Response Routing

**Test Name:** `testCsCallCmdResponse`

**Objective:** Verify command responses routed to correct caller

**Steps:**
1. Load parent with BLOCK mode
2. Execute until CS_CALL
3. Verify parent opCode/cmdSeq saved
4. Execute child commands
5. Verify child command responses dispatched
6. Verify parent response when parent completes

**Expected Results:**
- ✅ Parent opCode/cmdSeq saved on CS_CALL
- ✅ Child commands send responses with child context
- ✅ Parent resumes with correct opCode/cmdSeq
- ✅ Parent completion sends response to original caller

---

### Test 13: Fast-Forward Mechanism

**Test Name:** `testCsCallFastForward`

**Objective:** Verify parent sequence reloads and fast-forwards correctly

**Steps:**
1. Load parent sequence with 10 commands before CS_CALL
2. Execute until CS_CALL (executedCount = 10)
3. Execute child
4. Monitor parent restore process

**Expected Results:**
- ✅ Parent sequence reloaded from disk
- ✅ `nextRecord()` called 10 times (fast-forward)
- ✅ Parent resumes at command 11 (after CS_CALL)
- ✅ No commands re-executed during fast-forward
- ✅ Sequence CRC/validation passes on reload

**Verification Points:**
```cpp
// In restoreParentState()
for (U32 i = 0; i < state.executedCount; i++) {
    this->m_sequence->nextRecord(dummy);
}
// executedCount tracks position accurately
```

---

### Test 14: Stack Memory Management

**Test Name:** `testCsCallStackMemory`

**Objective:** Verify stack doesn't leak memory

**Steps:**
1. Execute 100 parent→child sequences sequentially
2. Monitor stack size
3. Verify stack empty after each completion

**Expected Results:**
- ✅ Stack depth oscillates: 0 → 1 → 0 → 1 → 0...
- ✅ Stack never grows beyond expected depth
- ✅ No memory leaks (valgrind clean)
- ✅ Stack clear after 100 iterations

---

### Test 15: Concurrent CS_CALL (Multiple Sequencers)

**Test Name:** `testCsCallMultipleSequencers`

**Objective:** Verify multiple sequencers can nest independently

**Setup:** Requires 2+ CmdSequencer instances (e.g., via SeqDispatcher)

**Steps:**
1. Load parent1 on sequencer 0
2. Load parent2 on sequencer 1
3. Execute both until CS_CALL
4. Verify independent stacks
5. Execute children
6. Verify parents resume independently

**Expected Results:**
- ✅ Sequencer 0 stack depth = 1 (independent)
- ✅ Sequencer 1 stack depth = 1 (independent)
- ✅ Children execute on respective sequencers
- ✅ Parents resume independently
- ✅ No cross-sequencer interference

---

## Integration Test Suite

### Integration Test 1: End-to-End with GDS

**Objective:** Verify CS_CALL works in deployed system

**Setup:**
1. Build Ref deployment with CS_CALL changes
2. Start fprime-gds
3. Upload test sequences

**Steps:**
1. Command: `cmdSeq.CS_RUN parent.seq BLOCK`
2. Monitor events in GDS
3. Verify sequence completion

**Expected Results:**
- ✅ Event: `CS_SequenceLoaded(parent.seq)`
- ✅ Event: `CS_SequenceNested(child.seq, 1)`
- ✅ Event: `CS_SequenceResuming(parent.seq, child.seq, 0)`
- ✅ Event: `CS_SequenceComplete(parent.seq)`
- ✅ All commands execute in correct order
- ✅ Telemetry updates correctly

---

### Integration Test 2: CS_CALL with SeqDispatcher Queue

**Objective:** Verify CS_CALL works with queued sequences

**Setup:** Requires SeqDispatcher with queue feature

**Steps:**
1. Start slow sequence on both sequencers
2. Queue parent sequence (with CS_CALL)
3. Wait for sequencer to become available
4. Verify parent dispatches from queue
5. Verify CS_CALL executes correctly

**Expected Results:**
- ✅ Parent queued successfully
- ✅ Parent dispatches when sequencer available
- ✅ CS_CALL works from queued sequence
- ✅ Child executes on same sequencer
- ✅ Parent resumes and completes

---

### Integration Test 3: CS_CALL with Real Commands

**Objective:** Verify CS_CALL with actual component commands

**Setup:** Create sequences with real commands (not just NO_OP)

**Test Sequences:**
- Parent sends telemetry commands
- Child sends parameter set commands
- Verify both execute correctly

**Expected Results:**
- ✅ Parent commands execute
- ✅ Child commands execute
- ✅ Telemetry/parameters updated correctly
- ✅ Command responses routed correctly
- ✅ No interference between parent/child commands

---

### Integration Test 4: Long-Running Nested Sequences

**Objective:** Verify CS_CALL with realistic sequence durations

**Setup:**
- Parent: 5-minute sequence
- Child: 2-minute sequence called at 2-minute mark

**Expected Results:**
- ✅ Parent runs for 2 minutes before CS_CALL
- ✅ Child runs for 2 minutes
- ✅ Parent resumes and runs remaining 3 minutes
- ✅ Total duration: 7 minutes
- ✅ No timeouts or errors

---

### Integration Test 5: CS_CALL with Validation

**Objective:** Verify CS_VALIDATE works with nested sequences

**Steps:**
1. Validate parent.seq (contains CS_CALL)
2. Verify child.seq referenced in CS_CALL is also validated
3. Catch broken references

**Expected Results:**
- ✅ CS_VALIDATE parent.seq: Success
- ✅ Warning if child.seq doesn't exist
- ✅ Validation catches file-not-found early

---

## Performance Test Suite

### Performance Test 1: Nested Call Overhead

**Objective:** Measure performance impact of nesting

**Setup:**
- Sequence A: 100 commands (no nesting)
- Sequence B: 50 commands + CS_CALL(50 commands) (1-level nesting)

**Metrics:**
- Execution time A vs B
- Memory usage
- CPU usage

**Expected Results:**
- ✅ Overhead < 10ms per CS_CALL
- ✅ Memory overhead < 1KB per stack entry
- ✅ No memory leaks

---

### Performance Test 2: Deep Nesting Performance

**Objective:** Verify performance at maximum depth

**Setup:** 5-level nested sequences (max depth)

**Metrics:**
- Total execution time
- Stack memory usage
- State capture/restore time

**Expected Results:**
- ✅ Total overhead < 50ms for 5 levels
- ✅ Stack memory < 5KB
- ✅ No exponential slowdown

---

### Performance Test 3: File Reload Performance

**Objective:** Measure parent sequence reload time

**Setup:** Large parent sequence (1000 commands)

**Metrics:**
- Reload time after child completes
- Fast-forward time (nextRecord() calls)

**Expected Results:**
- ✅ Reload < 100ms for 1000-command sequence
- ✅ Fast-forward linear in executedCount

---

## Regression Test Suite

### Regression Test 1: Non-Nested Sequences Still Work

**Objective:** Verify CS_CALL doesn't break normal sequences

**Steps:**
1. Run sequences WITHOUT CS_CALL
2. Verify same behavior as before

**Expected Results:**
- ✅ Normal sequences execute correctly
- ✅ No performance regression
- ✅ Stack remains empty (unused)

---

### Regression Test 2: Existing Commands Unaffected

**Objective:** Verify CS_RUN, CS_CANCEL, CS_VALIDATE unchanged

**Steps:**
1. Test all existing CmdSequencer commands
2. Verify behavior matches pre-CS_CALL

**Expected Results:**
- ✅ CS_RUN works as before
- ✅ CS_CANCEL works as before
- ✅ CS_VALIDATE works as before
- ✅ CS_START, CS_STEP, CS_AUTO, CS_MANUAL work
- ✅ CS_JOIN_WAIT unaffected

---

### Regression Test 3: Unit Tests Pass

**Objective:** Verify existing CmdSequencer unit tests still pass

**Steps:**
```bash
cd Svc/CmdSequencer
fprime-util check
```

**Expected Results:**
- ✅ All existing tests pass
- ✅ No test failures introduced by CS_CALL

---

## Manual Testing Checklist

Use this checklist for manual GDS testing:

- [ ] Basic nesting: Parent calls child, child completes, parent resumes
- [ ] Multi-level: Parent → Child → Grandchild (3 levels)
- [ ] Depth limit: 6th level rejected with CS_NestedTooDeep event
- [ ] Child error: Error in child aborts parent
- [ ] CS_CANCEL: Clears entire nested stack
- [ ] Manual mode: CS_CALL rejected in MANUAL mode
- [ ] Not running: CS_CALL rejected when no sequence active
- [ ] File not found: Invalid child filename handled gracefully
- [ ] Telemetry: Events logged correctly (CS_SequenceNested, CS_SequenceResuming)
- [ ] Command responses: Routed to correct caller
- [ ] Timing: Relative timing preserved across nesting
- [ ] Queue integration: CS_CALL works with SeqDispatcher queue
- [ ] Validation: CS_VALIDATE catches missing child sequences
- [ ] Real commands: Works with actual component commands (not just NO_OP)
- [ ] Long sequences: No issues with long-running nested sequences

---

## Success Criteria

**Unit Tests:**
- ✅ All 15 unit tests pass
- ✅ Code coverage > 90% for new CS_CALL code

**Integration Tests:**
- ✅ All 5 integration tests pass
- ✅ Works in deployed system (GDS)

**Performance Tests:**
- ✅ Overhead < 10ms per nesting level
- ✅ No memory leaks

**Regression Tests:**
- ✅ All existing CmdSequencer tests pass
- ✅ No behavior changes for non-nested sequences

**Manual Tests:**
- ✅ All manual checklist items verified

**Documentation:**
- ✅ Events logged correctly
- ✅ Error messages clear
- ✅ User guide updated

---

## Test Execution Order

1. **Phase 1: Unit Tests** (automated)
   - Run all 15 unit tests
   - Fix any failures
   - Achieve >90% code coverage

2. **Phase 2: Regression Tests** (automated)
   - Verify existing tests still pass
   - Check for performance regressions

3. **Phase 3: Integration Tests** (GDS)
   - Run integration tests 1-5
   - Verify in deployed system

4. **Phase 4: Manual Testing** (GDS)
   - Work through manual checklist
   - Test edge cases
   - Exploratory testing

5. **Phase 5: Performance Tests** (benchmarking)
   - Run performance suite
   - Profile memory usage
   - Check for leaks (valgrind)

6. **Phase 6: Long-Running Tests** (soak testing)
   - Run nested sequences for extended periods
   - Monitor for memory leaks
   - Verify stability

---

## Known Limitations

**Design Constraints:**
1. **Sequence file must not change:** Parent sequence reloaded from disk on restore. If file modified during child execution, behavior is undefined (FW_ASSERT may trigger).

2. **Maximum nesting depth: 5 levels** - Hardcoded limit prevents infinite recursion. Configurable in future if needed.

3. **AUTO mode only:** CS_CALL rejected in MANUAL mode (too complex for MVP).

4. **Child errors abort parent:** No try/catch mechanism. Child error propagates to parent.

5. **Single sequencer:** CS_CALL works on single sequencer. For multi-sequencer nested execution, use CS_JOIN_WAIT pattern.

**Document these in user manual.**

---

## Test Data Summary

**Total Tests:** 31
- Unit Tests: 15
- Integration Tests: 5
- Performance Tests: 3
- Regression Tests: 3
- Manual Tests: 15 items

**Estimated Test Execution Time:**
- Unit tests: 5 minutes
- Integration tests: 15 minutes
- Performance tests: 10 minutes
- Manual tests: 30 minutes
- **Total: ~60 minutes**

---

## Next Steps

1. ✅ Implement CS_CALL (COMPLETE)
2. ⏳ Write unit tests (testCsCallBasic, testCsCallMultiLevel, etc.)
3. ⏳ Build and verify compilation
4. ⏳ Run unit test suite
5. ⏳ Create test sequences for integration testing
6. ⏳ Run integration tests in Ref deployment
7. ⏳ Execute manual test checklist
8. ⏳ Run performance benchmarks
9. ⏳ Update user documentation
10. ⏳ Create PR with test results

**Current Status:** Ready for unit test implementation
