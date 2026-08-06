# CS_CALL Design Document

## Overview

CS_CALL is an enhancement to CmdSequencer that enables true nested sequencing by pausing the parent sequence and freeing the sequencer to execute the child sequence. This differs from CS_JOIN_WAIT, which blocks the parent but keeps the sequencer occupied.

## Current State: CS_JOIN_WAIT

**How it works:**
- Parent sequence calls `seqDispatcher.RUN child.seq NO_BLOCK`
- Parent executes `cmdSeq.CS_JOIN_WAIT` to block until child completes
- **Problem:** Parent sequencer remains BUSY while waiting
- **Limitation:** Requires multiple sequencers to avoid deadlock

**Timeline:**
```
Parent Sequencer (BUSY):
├─ Command 1
├─ Command 2
├─ seqDispatcher.RUN child.seq NO_BLOCK  ← Dispatches to queue or another sequencer
├─ CS_JOIN_WAIT                          ← Parent BLOCKS but sequencer OCCUPIED
│  [Parent waiting, sequencer BUSY...]
│  
Child Sequencer (running in parallel):
    ├─ Child Command 1
    ├─ Child Command 2
    └─ Complete → Parent unblocks
│
├─ Command 3  ← Parent resumes
└─ Command 4
```

## Proposed: CS_CALL

**How it should work:**
- Parent sequence executes `cmdSeq.CS_CALL child.seq`
- CmdSequencer pushes parent state onto stack and marks sequencer AVAILABLE
- Child sequence loads and executes on the same sequencer
- When child completes, parent state is popped and execution resumes
- **Benefit:** Works with single sequencer, more efficient resource usage

**Timeline:**
```
Single Sequencer:
├─ Parent Command 1
├─ Parent Command 2
├─ CS_CALL child.seq                     ← Parent state pushed to stack
│  [Sequencer now AVAILABLE]
│
├─ Child Command 1                       ← Child executes on same sequencer
├─ Child Command 2
└─ Child Complete → Pop parent state
│
├─ Parent Command 3                      ← Parent resumes where it left off
└─ Parent Command 4
```

---

## Implementation Design

### 1. State Stack Data Structure

Add to `CmdSequencer.hpp`:

```cpp
// Saved state for nested sequence execution
struct SequenceState {
    Fw::EightyCharString sequenceName;
    CmdSequencer::Sequence sequence;        // Copy of sequence data
    U32 executedCount;                      // Commands executed before pause
    BlockState blockState;                  // BLOCK or NO_BLOCK
    Fw::Time cmdTimer;                      // Timer state
    FwOpcodeType opCode;                    // Calling command opcode
    U32 cmdSeq;                             // Calling command sequence number
    // TODO: What else needs to be saved?
};

std::stack<SequenceState> m_nestedStateStack;
```

**Key Questions:**
- What exactly needs to be captured in SequenceState?
- Should we limit stack depth (e.g., max 5 levels)?
- How do we handle timer state across pause/resume?

### 2. CS_CALL Command Handler

Add to `CmdSequencerCommands.fppi`:

```fpp
@ Call a nested sequence (pauses current, executes child, resumes parent)
async command CS_CALL(
    fileName: string size 100 @< The sequence file to call
)
```

Add handler to `CmdSequencer.cpp`:

```cpp
void CmdSequencer::CS_CALL_cmdHandler(
    FwOpcodeType opCode,
    U32 cmdSeq,
    const Fw::CmdStringArg& fileName
) {
    // 1. Verify we're in AUTO mode and RUNNING state
    if (this->m_runMode != MANUAL && this->m_stepMode == AUTO) {
        // Cannot call nested sequence in manual mode
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    if (this->m_runMode != RUNNING) {
        // Not currently running a sequence
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    // 2. Capture current state
    SequenceState parentState = captureCurrentState();
    m_nestedStateStack.push(parentState);

    // 3. Load child sequence
    Fw::CmdStringArg childFileName(fileName);
    CmdSequencer::Sequence childSequence;
    const Os::File::Status status = childSequence.loadFile(childFileName.toChar());
    
    if (status != Os::File::OP_OK) {
        // Failed to load child, restore parent
        m_nestedStateStack.pop();
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    // 4. Replace current sequence with child
    this->m_sequence = childSequence;
    this->m_executedCount = 0;
    this->m_runMode = RUNNING;
    
    // 5. Start executing child
    this->performCmd_Step();
    
    // 6. Immediate OK response (like NO_BLOCK)
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}
```

### 3. State Capture Function

```cpp
CmdSequencer::SequenceState CmdSequencer::captureCurrentState() {
    SequenceState state;
    
    // Copy sequence name
    state.sequenceName = this->m_sequence.m_fileName;
    
    // Deep copy sequence data
    state.sequence = this->m_sequence;  // Does Sequence class support copy?
    
    // Save execution position
    state.executedCount = this->m_executedCount;
    
    // Save block state
    state.blockState = this->m_blockState;
    
    // Save timer state
    state.cmdTimer = this->m_cmdTimer;  // How to capture Fw::Time?
    
    // Save calling command context
    state.opCode = this->m_opCode;
    state.cmdSeq = this->m_cmdSeq;
    
    return state;
}
```

**Critical Question:** Does the `Sequence` class support deep copying? We may need to implement copy constructor.

### 4. State Restore Function

```cpp
void CmdSequencer::restoreParentState(const SequenceState& state) {
    // Restore sequence
    this->m_sequence = state.sequence;
    
    // Restore execution position
    this->m_executedCount = state.executedCount;
    
    // Restore block state
    this->m_blockState = state.blockState;
    
    // Restore timer
    this->m_cmdTimer = state.cmdTimer;
    
    // Restore command context
    this->m_opCode = state.opCode;
    this->m_cmdSeq = state.cmdSeq;
    
    // Restore run mode
    this->m_runMode = RUNNING;
}
```

### 5. Modified Sequence Completion Logic

Update `sequenceComplete()` in `CmdSequencer.cpp`:

```cpp
void CmdSequencer::sequenceComplete() {
    // Check if there's a parent sequence on the stack
    if (!m_nestedStateStack.empty()) {
        // Pop parent state
        SequenceState parent = m_nestedStateStack.top();
        m_nestedStateStack.pop();
        
        // Restore parent state
        restoreParentState(parent);
        
        // Log event
        this->log_ACTIVITY_HI_CS_SequenceResumed(parent.sequenceName);
        
        // Continue parent execution from where it left off
        this->performCmd_Step();
        
        return;  // Don't execute normal completion logic
    }
    
    // Normal completion (no parent)
    // ... existing completion logic ...
}
```

---

## Open Design Questions

### 1. Sequence Data Deep Copy
**Problem:** The `Sequence` class may not support deep copying. We need to:
- Check if `Sequence` class has copy constructor
- If not, implement one or manually copy all members
- Ensure allocated buffers are properly duplicated

### 2. Timer State Management
**Problem:** How do we save/restore timer state?
- Can we serialize `Fw::Time`?
- Should we pause timers or let them expire?
- What happens to relative time commands in parent after child executes?

### 3. Command Response Handling
**Problem:** How do we handle command responses for nested sequences?
- Parent had a pending command response before CS_CALL
- Child sequence may have its own command responses
- How do we route responses correctly?

**Proposed:** Each SequenceState saves the command context (opCode, cmdSeq), and responses are directed to the appropriate level.

### 4. Stack Depth Limit
**Problem:** Infinite recursion protection
- Limit stack depth (e.g., 5 levels)?
- What error do we return on overflow?
- Should this be configurable?

### 5. Error Handling in Child
**Problem:** What if child sequence fails?
- Does parent resume or abort?
- How do we propagate error status?
- Can parent catch and handle child errors?

**Proposed:** Child error aborts both child and parent (safest option).

### 6. CS_CANCEL Behavior
**Problem:** What happens if operator issues CS_CANCEL during nested execution?
- Cancel only current (child)?
- Cancel entire stack?
- Configurable behavior?

**Proposed:** CS_CANCEL clears the entire stack and aborts all sequences.

### 7. Memory Management
**Problem:** Stack growth with deep nesting
- Each SequenceState holds a full Sequence copy
- Could be large (5KB buffer × stack depth)
- Need memory bounds checking

---

## Implementation Phases

### Phase 1: Basic Infrastructure
- [ ] Add SequenceState struct to CmdSequencer.hpp
- [ ] Add m_nestedStateStack member
- [ ] Implement captureCurrentState()
- [ ] Implement restoreParentState()
- [ ] Add CS_CALL command definition to FPP

### Phase 2: CS_CALL Command
- [ ] Implement CS_CALL_cmdHandler()
- [ ] Modify sequenceComplete() to check stack
- [ ] Add events (CS_SequenceNested, CS_SequenceResumed)

### Phase 3: Deep Copy Support
- [ ] Verify/implement Sequence copy constructor
- [ ] Test state capture/restore with real sequences

### Phase 4: Error Handling
- [ ] Handle child sequence load failures
- [ ] Propagate child execution errors to parent
- [ ] Implement stack depth limit
- [ ] Handle CS_CANCEL with nested sequences

### Phase 5: Edge Cases
- [ ] Timer state management
- [ ] Command response routing
- [ ] Manual mode compatibility (likely disallow)
- [ ] Validate vs CS_JOIN_WAIT interactions

### Phase 6: Testing
- [ ] Unit tests for state capture/restore
- [ ] Unit tests for single-level nesting
- [ ] Unit tests for multi-level nesting (parent→child→grandchild)
- [ ] Unit tests for error cases
- [ ] Integration tests with real sequences

---

## Testing Strategy

### Unit Tests

1. **testCsCallBasic** - Single level nesting
2. **testCsCallMultiLevel** - Parent→Child→Grandchild (3 levels)
3. **testCsCallChildError** - Child fails, verify parent aborts
4. **testCsCallStackLimit** - Verify depth limit enforced
5. **testCsCallCancel** - CS_CANCEL during nested execution
6. **testCsCallStateRestore** - Verify parent resumes correctly
7. **testCsCallWithTimers** - Relative time commands in parent/child

### Integration Tests

1. Create test sequences using CS_CALL
2. Verify with SeqDispatcher integration
3. Test with queue (child calls grandchild, grandchild queues)

---

## Migration Path

### For Users

**Before (CS_JOIN_WAIT):**
```
R00:00:00.000 seqDispatcher.RUN child.seq NO_BLOCK
R00:00:00.100 cmdSeq.CS_JOIN_WAIT
```

**After (CS_CALL):**
```
R00:00:00.000 cmdSeq.CS_CALL child.seq
```

Much simpler syntax, works with single sequencer.

### Backward Compatibility

- CS_JOIN_WAIT continues to work (don't remove it)
- CS_CALL is additive, not breaking
- Users can migrate incrementally

---

## Timeline Estimate

- Phase 1-2 (Basic infrastructure + command): **1 week**
- Phase 3 (Deep copy): **3-5 days** (depends on Sequence class complexity)
- Phase 4-5 (Error handling + edge cases): **1 week**
- Phase 6 (Testing): **1 week**

**Total: 3-4 weeks** (matches original estimate)

---

## Next Steps

1. **Explore CmdSequencer codebase:**
   - Understand Sequence class implementation
   - Check for existing copy constructors
   - Identify all state that needs capture

2. **Prototype state capture:**
   - Create minimal SequenceState struct
   - Implement capture/restore for basic state

3. **Prototype CS_CALL handler:**
   - Minimal implementation that saves/restores state
   - Test with simple sequences

4. **Iterate on edge cases**

---

## Resources

- **CmdSequencer source:** `Svc/CmdSequencer/CmdSequencer.{hpp,cpp}`
- **Sequence class:** `Svc/CmdSequencer/CmdSequencerImpl.{hpp,cpp}`
- **CS_JOIN_WAIT implementation:** Reference for blocking behavior
- **Plan document:** `cmake/claude_plan.md` (has original CS_CALL notes)

---

## Questions for Exploration

- [ ] Does Sequence class support copy constructor?
- [ ] What is the exact structure of Sequence::m_buffer?
- [ ] How does CS_JOIN_WAIT track completion?
- [ ] Can we reuse any CS_JOIN_WAIT logic?
- [ ] What telemetry should we add for nested sequences?

---

**Start here:** Explore the Sequence class and understand what state needs to be captured.
