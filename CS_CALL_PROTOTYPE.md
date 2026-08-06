# CS_CALL Prototype Implementation

Based on exploration findings, here's a concrete implementation plan.

---

## Key Findings Summary

1. **Sequence is abstract** - Can't copy it directly
2. **Solution:** Save filename + execution position, reload on restore
3. **Timer state is simple** - Easy to save/restore
4. **CS_JOIN_WAIT pattern** - Can reuse opCode/cmdSeq saving approach
5. **All needed state identified** - See SequenceState struct below

---

## Step 1: Add SequenceState Struct

**File:** `Svc/CmdSequencer/CmdSequencerImpl.hpp`

**Location:** Add after the Timer class (around line 463)

```cpp
//! \class SequenceState
//! \brief Saved state for nested sequence execution (CS_CALL)
class SequenceState {
  public:
    //! Construct a SequenceState object
    SequenceState()
        : executedCount(0),
          opCode(0),
          cmdSeq(0),
          blockState(Svc::BlockState::NO_BLOCK),
          runMode(STOPPED),
          stepMode(AUTO) {}

  public:
    //! The sequence file name (reload from disk on restore)
    Fw::CmdStringArg fileName;

    //! Commands executed before pause
    U32 executedCount;

    //! Calling command context
    FwOpcodeType opCode;
    U32 cmdSeq;
    Svc::BlockState::t blockState;

    //! Timer state
    Timer cmdTimer;
    Timer cmdTimeoutTimer;

    //! Execution mode
    RunMode runMode;
    StepMode stepMode;

    //! Current record (saved for resumption)
    Sequence::Record record;
};
```

**Add member variable** (around line 707, after `m_join_waiting`):

```cpp
//! Stack of nested sequence states for CS_CALL
std::stack<SequenceState> m_nestedStateStack;
```

**Add include** at top of file:

```cpp
#include <stack>
```

---

## Step 2: Add CS_CALL Command Definition

**File:** `Svc/CmdSequencer/CmdSequencerCommands.fppi`

Add after CS_JOIN_WAIT:

```fpp
@ Call a nested sequence (pauses current, executes child, resumes parent)
@ Requires AUTO mode. Works with single sequencer.
async command CS_CALL(
    fileName: string size 100 @< The sequence file to call
)
```

---

## Step 3: Implement State Capture

**File:** `Svc/CmdSequencer/CmdSequencerImpl.cpp`

Add this private method:

```cpp
CmdSequencerComponentImpl::SequenceState 
CmdSequencerComponentImpl::captureCurrentState() {
    SequenceState state;
    
    // Save sequence filename (will reload on restore)
    state.fileName = this->m_sequence->getFileName();
    
    // Save execution position
    state.executedCount = this->m_executedCount;
    
    // Save command context
    state.opCode = this->m_opCode;
    state.cmdSeq = this->m_cmdSeq;
    state.blockState = this->m_blockState;
    
    // Save timer state
    state.cmdTimer = this->m_cmdTimer;
    state.cmdTimeoutTimer = this->m_cmdTimeoutTimer;
    
    // Save execution mode
    state.runMode = this->m_runMode;
    state.stepMode = this->m_stepMode;
    
    // Save current record
    state.record = this->m_record;
    
    return state;
}
```

**Add declaration** to `CmdSequencerImpl.hpp` private section:

```cpp
//! Capture current sequencer state for CS_CALL
//! \return The captured state
SequenceState captureCurrentState();
```

---

## Step 4: Implement State Restore

**File:** `Svc/CmdSequencer/CmdSequencerImpl.cpp`

Add this private method:

```cpp
void CmdSequencerComponentImpl::restoreParentState(const SequenceState& state) {
    // Reload sequence file from disk
    const bool loaded = this->loadFile(state.fileName);
    FW_ASSERT(loaded, state.fileName.toChar());  // Sequence must reload successfully
    
    // Fast-forward to execution position by consuming records
    for (U32 i = 0; i < state.executedCount; i++) {
        if (this->m_sequence->hasMoreRecords()) {
            Sequence::Record dummy;
            this->m_sequence->nextRecord(dummy);
        } else {
            // Sequence has fewer records than before - file changed?
            FW_ASSERT(false, state.executedCount, i);
        }
    }
    
    // Restore execution position
    this->m_executedCount = state.executedCount;
    
    // Restore command context
    this->m_opCode = state.opCode;
    this->m_cmdSeq = state.cmdSeq;
    this->m_blockState = state.blockState;
    
    // Restore timer state
    this->m_cmdTimer = state.cmdTimer;
    this->m_cmdTimeoutTimer = state.cmdTimeoutTimer;
    
    // Restore execution mode
    this->m_runMode = state.runMode;
    this->m_stepMode = state.stepMode;
    
    // Restore current record
    this->m_record = state.record;
}
```

**Add declaration** to `CmdSequencerImpl.hpp` private section:

```cpp
//! Restore sequencer state after CS_CALL child completes
//! \param state The state to restore
void restoreParentState(const SequenceState& state);
```

---

## Step 5: Implement CS_CALL Command Handler

**File:** `Svc/CmdSequencer/CmdSequencerImpl.cpp`

Add this command handler:

```cpp
void CmdSequencerComponentImpl::CS_CALL_cmdHandler(
    const FwOpcodeType opCode,
    const U32 cmdSeq,
    const Fw::CmdStringArg& fileName
) {
    // 1. Verify preconditions
    if (this->m_runMode != RUNNING) {
        // Not currently running a sequence
        this->log_WARNING_HI_CS_NoSequenceActive();
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    
    if (this->m_stepMode != AUTO) {
        // CS_CALL only works in AUTO mode
        this->log_WARNING_HI_CS_InvalidMode("CS_CALL");
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    
    // 2. Check stack depth limit (prevent infinite recursion)
    const U32 MAX_NESTING_DEPTH = 5;
    if (this->m_nestedStateStack.size() >= MAX_NESTING_DEPTH) {
        this->log_WARNING_HI_CS_NestedTooDeep(MAX_NESTING_DEPTH);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    
    // 3. Capture current state
    SequenceState parentState = this->captureCurrentState();
    this->m_nestedStateStack.push(parentState);
    
    // 4. Load child sequence
    const bool loaded = this->loadFile(fileName);
    if (!loaded) {
        // Failed to load child, restore parent
        this->m_nestedStateStack.pop();
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    
    // 5. Reset execution state for child
    this->m_executedCount = 0;
    this->m_totalExecutedCount = 0;  // Or keep cumulative?
    this->m_runMode = RUNNING;
    this->m_stepMode = AUTO;
    this->m_blockState = Svc::BlockState::NO_BLOCK;  // Child runs in NO_BLOCK mode
    this->m_cmdTimer.clear();
    this->m_cmdTimeoutTimer.clear();
    
    // 6. Log event
    Fw::LogStringArg& logFileName = this->m_sequence->getLogFileName();
    this->log_ACTIVITY_HI_CS_SequenceNested(logFileName, this->m_nestedStateStack.size());
    
    // 7. Send immediate OK response (like NO_BLOCK)
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
    
    // 8. Start executing child
    // The child will execute on next schedIn call
}
```

**Add declaration** to `CmdSequencerImpl.hpp`:

```cpp
//! Handler for command CS_CALL
void CS_CALL_cmdHandler(
    FwOpcodeType opCode,              //!< The opcode
    U32 cmdSeq,                       //!< The command sequence number
    const Fw::CmdStringArg& fileName  //!< The sequence file to call
) override;
```

---

## Step 6: Modify Sequence Completion Logic

**File:** `Svc/CmdSequencer/CmdSequencerImpl.cpp`

Find the `sequenceComplete()` method and modify it:

```cpp
void CmdSequencerComponentImpl::sequenceComplete() {
    // NEW: Check if there's a parent sequence on the stack
    if (!this->m_nestedStateStack.empty()) {
        // We just finished a child sequence called via CS_CALL
        // Pop and restore parent state
        SequenceState parent = this->m_nestedStateStack.top();
        this->m_nestedStateStack.pop();
        
        // Log event
        Fw::LogStringArg& childFileName = this->m_sequence->getLogFileName();
        this->log_ACTIVITY_HI_CS_SequenceResuming(
            parent.fileName.toChar(), 
            childFileName.toChar(),
            this->m_nestedStateStack.size()
        );
        
        // Restore parent state
        this->restoreParentState(parent);
        
        // Continue parent execution on next schedIn
        return;  // Don't execute normal completion logic
    }
    
    // Normal completion (no parent on stack)
    // ... existing completion logic ...
}
```

---

## Step 7: Add Events

**File:** `Svc/CmdSequencer/CmdSequencerEvents.fppi`

Add these events:

```fpp
@ A nested sequence was called via CS_CALL
event CS_SequenceNested(
    fileName: string size 100 @< The child sequence file name
    nestingLevel: U32 @< Current nesting depth (1 = first level)
) \
  severity activity high \
  format "Nested sequence: {} (level {})"

@ Resuming parent sequence after CS_CALL child completed
event CS_SequenceResuming(
    parentFile: string size 100 @< The parent sequence file name
    childFile: string size 100 @< The child sequence that just completed
    nestingLevel: U32 @< Current nesting depth after resume
) \
  severity activity high \
  format "Resuming parent sequence: {} after {} (level {})"

@ CS_CALL rejected: nesting too deep
event CS_NestedTooDeep(
    maxDepth: U32 @< Maximum nesting depth
) \
  severity warning high \
  format "CS_CALL rejected: nesting depth exceeds maximum ({})"

@ CS_CALL rejected: invalid mode
event CS_InvalidMode(
    command: string size 40 @< The command name
) \
  severity warning high \
  format "{} rejected: must be in AUTO mode"
```

---

## Step 8: Handle Errors in Child Sequences

**Modify `performCmd_Cancel()` to handle nested state:**

```cpp
void CmdSequencerComponentImpl::performCmd_Cancel() {
    // ... existing cancel logic ...
    
    // NEW: If nested, clear the stack (cancel aborts all levels)
    while (!this->m_nestedStateStack.empty()) {
        this->m_nestedStateStack.pop();
    }
    
    // ... rest of existing logic ...
}
```

---

## Testing Plan

### Unit Test 1: Basic Nesting

```cpp
void testCsCallBasic() {
    // Load parent.seq, execute until CS_CALL
    // Verify state captured
    // Verify child loads
    // Run child to completion
    // Verify parent restored
    // Verify parent continues from correct position
}
```

### Unit Test 2: Multi-Level Nesting

```cpp
void testCsCallMultiLevel() {
    // Parent calls child, child calls grandchild
    // Verify stack has 2 entries
    // Complete grandchild -> child resumes
    // Complete child -> parent resumes
}
```

### Unit Test 3: Nesting Depth Limit

```cpp
void testCsCallDepthLimit() {
    // Try to nest 6 levels deep
    // Verify 6th level rejected with CS_NestedTooDeep event
}
```

### Unit Test 4: Child Error Propagation

```cpp
void testCsCallChildError() {
    // Parent calls child
    // Child encounters command error
    // Verify both parent and child abort
    // Verify stack cleared
}
```

---

## Open Questions / TODOs

1. **Timer behavior:** Should parent timers continue ticking while child executes?
   - **Current approach:** Timers saved/restored, so they pause

2. **Telemetry:** Should we add `CS_NestingDepth` telemetry channel?
   - **Recommendation:** Yes, helps operators monitor nesting

3. **Manual mode:** Should CS_CALL work in MANUAL mode?
   - **Current approach:** Reject in MANUAL (too complex for MVP)

4. **Error handling:** If child fails, should parent be able to recover?
   - **Current approach:** Abort both (safest)

5. **Stack memory:** With 5KB buffers, 5-level stack = 25KB. Is this acceptable?
   - **Note:** We're not copying buffers (just filenames), so actually minimal

---

## Next Steps for Prototype

1. **Add SequenceState struct** to .hpp
2. **Add stack member variable**
3. **Implement captureCurrentState()**
4. **Implement restoreParentState()**
5. **Test save/restore** with simple sequence
6. **Add CS_CALL command** definition
7. **Implement CS_CALL handler**
8. **Test basic nesting** with 2 test sequences
9. **Add events**
10. **Write unit tests**

**Start with Steps 1-5** to validate the capture/restore mechanism before adding the full CS_CALL command.
