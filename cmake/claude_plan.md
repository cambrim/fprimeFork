# Integration Plan: Sequence Queueing into F Prime

## Context

This plan addresses integrating sequence queueing functionality into F Prime as a core feature. Currently, when a sequencer is busy executing a sequence and receives another CS_RUN command, it returns an execution error. This prevents nested sequencing and requires manual retry logic from operators.

The user has built a working `SequenceQueue` component for a project-specific deployment that solves this problem by maintaining a FIFO queue of sequences and automatically executing them one after another. The goal is to make this functionality generic and integrate it properly into F Prime's existing sequencer architecture.

**Key Requirements:**
1. Support queueing sequences instead of rejecting them when sequencer is busy
2. Work with CS_RUN command (either extend it or create new command)
3. Support multiple sequencers (via SeqDispatcher architecture)
4. Stretch goal: Support pausing parent sequences while child sequences execute

**Why this matters:** This enables more flexible ground operations, allows sequences to call other sequences without timing coordination, and reduces operator workload. It also makes F Prime deployments more robust by eliminating a common failure mode.

---

## F Prime Sequencer Architecture Summary

### Current Components

1. **CmdSequencer** (`Svc/CmdSequencer/`)
   - Executes one sequence at a time
   - States: STOPPED, RUNNING (with AUTO/MANUAL modes)
   - Commands: CS_RUN, CS_CANCEL, CS_VALIDATE, CS_START, CS_STEP, CS_AUTO, CS_MANUAL, CS_JOIN_WAIT
   - Port interfaces:
     - `seqRunIn` (Svc.CmdSeqIn): Accepts sequence run requests
     - `seqDone` (Fw.CmdResponse): Notifies when sequence completes
     - `seqStartOut` (Svc.CmdSeqIn): Notifies when sequence starts
     - `cmdResponseIn` (Fw.CmdResponse): Receives command completion from dispatcher
     - `comCmdOut` (Fw.Com): Sends commands to dispatcher
   - CS_RUN rejects new sequences when RUNNING (returns EXECUTION_ERROR)

2. **SeqDispatcher** (`Svc/SeqDispatcher/`)
   - Coordinates multiple CmdSequencer instances
   - First-available dispatching algorithm
   - Commands: RUN, RUN_ARGS, LOG_STATUS, CANCEL_NAME, CANCEL_ALL
   - Port arrays connect to N sequencers (configured via `SeqDispatcherSequencerPorts`)
   - Tracks state of each sequencer: AVAILABLE, RUNNING_SEQUENCE_BLOCK, RUNNING_SEQUENCE_NO_BLOCK
   - Single point of entry for operators - no per-sequencer CS_RUN commands

3. **Current SequenceQueue Implementation** (`SequenceQueue/`)
   - Project-specific component (OxusFsw namespace)
   - Maintains `std::queue<Fw::String>` of sequence paths
   - States: STOPPED, RUNNING, PAUSED
   - Commands: QUEUE_SEQUENCE, CLEAR_QUEUE, GET_QUEUE_STATUS, PAUSE_QUEUE, RESUME_QUEUE, LIST_QUEUE
   - Connects to single sequencer via `seqRunOut` (Svc.CmdSeqIn) and `seqDoneIn` (Fw.CmdResponse)
   - Automatically runs next sequence when `seqDoneIn` fires

---

## Selected Integration Approach: Queue in SeqDispatcher

**Architecture:**
```
Ground Command (RUN) → SeqDispatcher (with queue) → CmdSequencer[N]
```

**Why this approach:**
- Transparent to operators - RUN command behavior improves without new commands
- Natural fit: dispatcher already tracks sequencer availability
- Single command interface (no separate QUEUE_SEQUENCE command needed)
- Works seamlessly with multiple sequencers
- Backward compatible (queue can be disabled by setting MAX_QUEUE_DEPTH = 0)
- Single global queue more efficient than N per-sequencer queues

**Design Decisions:**
- Default MAX_QUEUE_DEPTH: 20 (configurable via parameter)
- Overflow behavior: Reject with EXECUTION_ERROR (fail-fast, no silent drops)
- Queue order: FIFO (First In, First Out)
- Queue location: SeqDispatcher component

---

## Implementation Design: Queue in SeqDispatcher

### High-Level Design

**Core Idea:** Enhance SeqDispatcher to queue sequence requests when all sequencers are busy, then automatically dispatch queued sequences as sequencers become available.

**Key Changes:**
1. Add `std::queue<QueueEntry>` to SeqDispatcher
2. Modify RUN/RUN_ARGS handlers to queue instead of error when all busy
3. Modify `seqDoneIn_handler` to dispatch from queue when sequencer frees
4. Add queue management commands (CLEAR_QUEUE, LIST_QUEUE, etc.)
5. Add telemetry for queue depth and statistics
6. Add parameter for MAX_QUEUE_DEPTH (optional, default enabled)

**QueueEntry Structure:**
```cpp
struct QueueEntry {
    Fw::String fileName;
    Svc::SeqArgs args;
    Svc::BlockState blockState;
    FwOpcodeType opCode;
    U32 cmdSeq;
    // For tracking command responses
};
```

### Detailed Implementation Steps

#### 1. Modify SeqDispatcher.fpp

**Add to SeqDispatcher.fpp:**
- Parameter: `MAX_QUEUE_DEPTH` (U32, default 20, 0 = disabled)
- Commands:
  - `CLEAR_QUEUE` - Clear all queued sequences
  - `LIST_QUEUE` - List all queued sequences
  - `GET_QUEUE_STATUS` - Report queue statistics
  - `PAUSE_QUEUE` - Pause queue processing (still dispatch running sequences)
  - `RESUME_QUEUE` - Resume queue processing
- Telemetry:
  - `QueueDepth` (U32) - Current queue size
  - `QueuedTotal` (U32) - Total sequences queued this session
  - `SequencesExecutedFromQueue` (U32) - Count executed from queue
  - `QueueOverflows` (U32) - Times queue was full
- Events:
  - `SequenceQueued` - Sequence added to queue (with queue depth)
  - `QueueOverflow` - Sequence rejected, queue full
  - `QueueCleared` - Queue manually cleared
  - `StartingQueuedSequence` - Starting sequence from queue
  - `QueueEmpty` - Queue drained
  - `QueuePaused/Resumed` - Queue state change

#### 2. Modify SeqDispatcher Implementation

**Files:** `Svc/SeqDispatcher/SeqDispatcher.hpp`, `Svc/SeqDispatcher/SeqDispatcher.cpp`

**Data Members (SeqDispatcher.hpp):**
```cpp
struct QueueEntry {
    Fw::String fileName;
    Svc::SeqArgs args;
    Svc::BlockState blockState;
    FwOpcodeType opCode;
    U32 cmdSeq;
    Fw::Time queueTime;  // For telemetry/debugging
};

std::queue<QueueEntry> m_sequenceQueue;
bool m_queuePaused;
U32 m_queuedTotal;
U32 m_executedFromQueue;
U32 m_queueOverflows;
U32 m_maxQueueDepth;
```

**Modified RUN_cmdHandler Logic (SeqDispatcher.cpp):**
```cpp
void SeqDispatcher::RUN_cmdHandler(...) {
    // Load max queue depth parameter
    U32 maxDepth = paramGet_MAX_QUEUE_DEPTH(valid);
    if (valid) m_maxQueueDepth = maxDepth;
    
    // Try to find available sequencer
    FwIndexType idx = getNextAvailableSequencerIdx();
    
    if (idx >= 0) {
        // Direct dispatch (existing logic)
        dispatchSequence(idx, fileName, args, block, opCode, cmdSeq);
        cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
    } else if (m_maxQueueDepth > 0 && m_sequenceQueue.size() < m_maxQueueDepth) {
        // Queue the sequence
        QueueEntry entry = {fileName, args, block, opCode, cmdSeq, getTime()};
        m_sequenceQueue.push(entry);
        m_queuedTotal++;
        
        log_ACTIVITY_HI_SequenceQueued(fileName, m_sequenceQueue.size());
        cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
    } else {
        // Queue disabled or full
        log_WARNING_HI_QueueOverflow(fileName);
        m_queueOverflows++;
        cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    }
}
```

**Modified seqDoneIn_handler Logic:**
```cpp
void SeqDispatcher::seqDoneIn_handler(FwIndexType portNum, ...) {
    // Update sequencer state to AVAILABLE (existing logic)
    m_entryTable[portNum].state = AVAILABLE;
    m_entryTable[portNum].sequenceRunning = "";
    
    // ... existing response handling ...
    
    // Try to dispatch from queue if not paused
    if (!m_queuePaused && !m_sequenceQueue.empty()) {
        QueueEntry entry = m_sequenceQueue.front();
        m_sequenceQueue.pop();
        
        log_ACTIVITY_HI_StartingQueuedSequence(entry.fileName);
        dispatchSequence(portNum, entry.fileName, entry.args, 
                         entry.blockState, entry.opCode, entry.cmdSeq);
        m_executedFromQueue++;
    } else if (m_sequenceQueue.empty() && someQueuesExecutedThisSession) {
        log_ACTIVITY_LO_QueueEmpty();
    }
}
```

**New Queue Management Commands:**
- `CLEAR_QUEUE_cmdHandler`: Clear `m_sequenceQueue`, log count
- `LIST_QUEUE_cmdHandler`: Iterate queue, log each entry (or just depth)
- `GET_QUEUE_STATUS_cmdHandler`: Log telemetry values via event
- `PAUSE_QUEUE_cmdHandler`: Set `m_queuePaused = true`
- `RESUME_QUEUE_cmdHandler`: Set `m_queuePaused = false`, try dispatch

**Telemetry Updates (schedIn or rate group handler):**
Update queue telemetry channels periodically.

#### 3. Update Unit Tests

**Files:** `Svc/SeqDispatcher/test/ut/`

**New Test Cases:**
- Test queueing when all sequencers busy
- Test queue overflow (exceed MAX_QUEUE_DEPTH)
- Test queue draining as sequencers complete
- Test CLEAR_QUEUE command
- Test PAUSE_QUEUE/RESUME_QUEUE
- Test queue disabled (MAX_QUEUE_DEPTH = 0)
- Test BlockState preservation for queued sequences
- Test command response handling for queued commands

#### 4. Update Documentation

**Files:**
- `Svc/SeqDispatcher/docs/sdd.md` - Update design doc with queue feature
- `docs/user-manual/framework/sequencing.md` - Document queue behavior for users

---

## Multiple Sequencer Support (Requirement 2)

**Good News:** Option 2 naturally supports multiple sequencers because:
1. SeqDispatcher already coordinates N sequencers
2. Queue sits at dispatcher level, so it works with any number of sequencers
3. As any sequencer completes, queue automatically dispatches to it
4. No changes needed to individual CmdSequencer instances

**Configuration:**
Operators set `SeqDispatcherSequencerPorts` in `AcConstants.fpp` to desired count (e.g., 2, 4, 8).

---

## Nested Sequencing Support (Requirement 3)

### Initial Release: CS_JOIN_WAIT Pattern (Already Exists)

**How it works:**
```
; Parent sequence (parent.seq)
COMMAND_1
COMMAND_2
CS_RUN child.seq NO_BLOCK    ; Start child sequence, continue immediately
CS_JOIN_WAIT                  ; Block until all NO_BLOCK sequences complete
COMMAND_3                     ; Continue after child finishes
COMMAND_4
```

**Timeline:**
```
Parent Sequencer:
├─ Command 1
├─ Command 2
├─ CS_RUN child.seq NO_BLOCK  ← Dispatches to queue/available sequencer
├─ CS_JOIN_WAIT                ← Parent blocks, waits for child
│  [Parent waiting...]
│  
Child Sequencer (parallel):
    ├─ Child Command 1
    ├─ Child Command 2
    └─ Complete → Parent unblocks
│
├─ Command 3  ← Parent resumes
└─ Command 4
```

**Key Points:**
- **No code changes needed** - CS_JOIN_WAIT already implemented in CmdSequencer
- Parent sequence occupies its sequencer while waiting
- Requires 2+ sequencers for nested execution (SeqDispatcher with N≥2)
- Child sequence can be queued if all sequencers busy (queue handles it!)
- Can nest multiple levels: parent calls child, child calls grandchild, etc.

**Interaction with Queue:**
When parent calls `CS_RUN child.seq NO_BLOCK`:
1. SeqDispatcher receives RUN command
2. If sequencer available → dispatch immediately
3. If all busy → add to queue (queue feature handles this)
4. Parent's CS_JOIN_WAIT waits until child completes
5. Queue continues processing other sequences

**Benefits:**
- Works today with existing F Prime
- Simple operator mental model
- Queue makes it robust (no "sequencer busy" errors)
- Predictable execution order

**Limitations:**
- Parent sequencer is blocked during child execution
- Requires multiple sequencers (deadlock with single sequencer)
- Parent cannot be interrupted while waiting

**Documentation Deliverable:**
- Update user manual with CS_JOIN_WAIT pattern examples
- Document best practices for nested sequences
- Include queueing behavior with nested calls

---

### Future Enhancement: CS_CALL Command (Phase 2 - After Queue Stabilizes)

**Concept:** True nested sequencing where parent pauses, freeing the sequencer for child execution.

**High-Level Design (Vague Plan for Future):**
```cpp
// In CmdSequencer
std::stack<SequenceState> m_stateStack;

struct SequenceState {
    Sequence* sequence;
    U32 executedCount;
    BlockState blockState;
    Timer cmdTimer;
    // ... other state variables
};

// New command: CS_CALL
void CS_CALL_cmdHandler(fileName) {
    // Push current state onto stack
    m_stateStack.push(captureCurrentState());
    
    // Load and run child sequence
    loadFile(fileName);
    performCmd_Step();
}

// Modified sequenceComplete()
void sequenceComplete() {
    if (!m_stateStack.empty()) {
        // Pop parent state
        SequenceState parent = m_stateStack.top();
        m_stateStack.pop();
        
        // Restore parent state
        restoreState(parent);
        
        // Resume parent execution
        performCmd_Step();
    } else {
        // No parent, truly complete
        // ... existing completion logic
    }
}
```

**Implementation Challenges:**
1. State capture/restore for all CmdSequencer variables
2. Timer state preservation
3. Command response tracking with nested contexts
4. Error handling (what if child fails? propagate to parent?)
5. Stack depth limits (prevent infinite recursion)
6. Memory management for paused sequences
7. Extensive testing of edge cases

**Benefits over CS_JOIN_WAIT:**
- Works with single sequencer (no deadlock)
- Parent doesn't occupy sequencer while paused
- More efficient resource usage
- Natural programming model (like function calls)

**Recommendation:**
- Defer to Phase 2 after queue functionality is stable and tested
- Estimate 2-4 weeks of development + testing
- Consider as separate PR/feature branch
- Not essential for initial queue integration

---


## Implementation Plan Summary

### Phase 1: Core Queueing (Essential)
1. Modify `Svc/SeqDispatcher/SeqDispatcher.fpp`:
   - Add parameter `MAX_QUEUE_DEPTH`
   - Add commands: CLEAR_QUEUE, LIST_QUEUE, GET_QUEUE_STATUS
   - Add telemetry: QueueDepth, QueuedTotal, ExecutedFromQueue, QueueOverflows
   - Add events: SequenceQueued, QueueOverflow, QueueCleared, StartingQueuedSequence, QueueEmpty

2. Modify `Svc/SeqDispatcher/SeqDispatcher.hpp`:
   - Add `QueueEntry` struct
   - Add `m_sequenceQueue` and related state variables

3. Modify `Svc/SeqDispatcher/SeqDispatcher.cpp`:
   - Update `RUN_cmdHandler` to queue when all sequencers busy
   - Update `RUN_ARGS_cmdHandler` similarly
   - Update `seqDoneIn_handler` to dispatch from queue
   - Implement queue management command handlers
   - Add telemetry updates

4. Add unit tests for queue functionality

5. Update documentation

### Phase 2: Queue Control (Nice-to-have)
1. Add PAUSE_QUEUE/RESUME_QUEUE commands
2. Add telemetry and events for pause state
3. Test pause/resume behavior

### Phase 3: Enhanced Features (Future)
1. Queue priority levels
2. Queue inspection/reordering commands
3. Per-sequence timeout in queue
4. Queue persistence across reboot

### Phase 4: CS_CALL Nested Sequencing (Future Phase 2)
1. Design state capture/restore mechanism for CmdSequencer
2. Implement state stack data structure
3. Add CS_CALL command handler
4. Modify sequence completion logic to check stack
5. Add error handling for nested failures
6. Extensive testing of nested scenarios
7. Performance testing with deep nesting

**Note:** This is a separate future enhancement, not part of initial queue integration.

---

## Files to Modify

**Critical Files:**
- `Svc/SeqDispatcher/SeqDispatcher.fpp` - Component definition
- `Svc/SeqDispatcher/SeqDispatcherCommands.fppi` - Add queue commands
- `Svc/SeqDispatcher/SeqDispatcherTelemetry.fppi` - Add queue telemetry
- `Svc/SeqDispatcher/SeqDispatcherEvents.fppi` - Add queue events
- `Svc/SeqDispatcher/SeqDispatcher.hpp` - Add queue data structures
- `Svc/SeqDispatcher/SeqDispatcher.cpp` - Implement queue logic
- `Svc/SeqDispatcher/docs/sdd.md` - Update design documentation
- `default/config/AcConstants.fpp` - Document queue parameters

**Test Files:**
- `Svc/SeqDispatcher/test/ut/SeqDispatcherTester.hpp` - Add test helpers
- `Svc/SeqDispatcher/test/ut/SeqDispatcherTester.cpp` - Add queue tests

**Documentation Files:**
- `docs/user-manual/framework/sequencing.md` - User guide updates

**Reference Files (to be deleted before PR):**
- `SequenceQueue/` - Current project-specific implementation

---

## Verification Plan

### Unit Tests
1. Queue sequence when all sequencers busy
2. Dispatch from queue when sequencer becomes available
3. Queue overflow behavior
4. Queue clearing
5. Queue with BlockState variants (BLOCK/NO_BLOCK)
6. Queue with multiple sequencers
7. Queue disabled (MAX_QUEUE_DEPTH = 0)
8. Pause/resume queue

### Integration Tests
1. End-to-end sequence queueing in Ref deployment
2. Multiple sequences queued and executed in order
3. Queue behavior with 2+ sequencers
4. Queue telemetry accuracy
5. Queue commands from ground interface

### Performance Tests
1. Large queue depth (100+ sequences)
2. Rapid queue/dequeue cycles
3. Memory usage with full queue

---

## Backward Compatibility

**Fully backward compatible:**
- If `MAX_QUEUE_DEPTH` parameter is set to 0, queueing is disabled
- RUN command behavior reverts to returning EXECUTION_ERROR when all busy
- Existing topologies work without changes
- New commands/telemetry are additive

---

## Design Tradeoffs

### Why SeqDispatcher over CmdSequencer?
- **Single point of control:** Dispatcher already manages multiple sequencers
- **Operator simplicity:** One queue, not N queues to manage
- **Efficient dispatching:** Queue dispatches to first available sequencer
- **Natural fit:** Dispatcher already tracks availability

### Why FIFO Queue?
- **Predictable:** Sequences execute in order received
- **Simple:** Easy to reason about behavior
- **Standard:** Matches operator expectations

Future enhancements could add priority queues or queue reordering.

### Why Optional (Parameterized)?
- **Conservative:** Some missions may prefer fail-fast behavior
- **Configurable:** Different deployments have different needs
- **Gradual adoption:** Can enable after testing

---

## Key Design Decisions Summary

| Aspect | Decision | Rationale |
|--------|----------|-----------|
| Integration location | SeqDispatcher | Natural fit, single queue, works with multiple sequencers |
| Default MAX_QUEUE_DEPTH | 20 | Small footprint, sufficient for typical ops, configurable via parameter |
| Overflow behavior | EXECUTION_ERROR | Fail-fast, no silent drops, operator explicitly retries |
| Queue order | FIFO | Predictable, simple, matches operator expectations |
| Command response | Immediate OK on queue | Quick feedback, operator knows sequence is queued |
| Nested sequencing (initial) | CS_JOIN_WAIT pattern | Already exists, no code changes, works today |
| Nested sequencing (future) | CS_CALL command | True pausing, deferred to Phase 2 after queue stabilizes |
| Queue disabled mode | MAX_QUEUE_DEPTH = 0 | Backward compatible, opt-in feature |
| LIST_QUEUE visibility | Event with count + top N entries | Avoid event spam, provide useful info |

---

## Default Ref Topology Integration Decision

**Current State:**
The default Ref topology (`TestDeploymentsProject/Ref/Top/`) currently has:
- Single `cmdSeq` instance (Svc.CmdSequencer)
- No SeqDispatcher
- Direct command connections to cmdSeq

**Recommendation: Add SeqDispatcher to Ref Topology**

**Why:**
1. **Demonstrates best practice** - Shows users how to set up multi-sequencer architecture
2. **Enables testing** - Allows comprehensive testing of queue functionality
3. **Matches documentation** - User manual already documents SeqDispatcher architecture
4. **Low impact** - Existing single-sequencer deployments unaffected (they can ignore it)
5. **Future-proof** - Many missions need multiple sequencers eventually

**Implementation:**
1. Add SeqDispatcher instance to `instances.fpp`
2. Add second CmdSequencer instance (`cmdSeq2`)
3. Wire SeqDispatcher to both sequencers in `topology.fpp`
4. Configure `SeqDispatcherSequencerPorts = 2` in `AcConstants.fpp`
5. Update Ref documentation/README

**Backward Compatibility:**
- Users with custom topologies: No impact (they don't use Ref)
- Users extending Ref: SeqDispatcher is optional, can still command cmdSeq directly
- Clear documentation on single vs multi-sequencer setups

---

## Manual Testing Campaign

This section provides step-by-step manual tests to verify all queue functionality after implementation.

### Test Environment Setup

**Prerequisites:**
1. Build and run Ref deployment: `fprime-util build && fprime-util run`
2. Connect ground station (GDS): `fprime-gds`
3. Create test sequences in appropriate sequence directory

**Test Sequences to Create:**

Create these simple test sequences for testing:

**`test_quick.seq`** (completes in ~1 second):
```
; Quick test sequence
R00:00:00.000 NO_OP
R00:00:00.100 NO_OP
R00:00:00.200 NO_OP
```

**`test_slow.seq`** (completes in ~5 seconds):
```
; Slow test sequence
R00:00:00.000 NO_OP
R00:00:02.000 NO_OP
R00:00:04.000 NO_OP
```

**`test_parent.seq`** (calls child sequence):
```
; Parent sequence for nested testing
R00:00:00.000 NO_OP
R00:00:00.500 seqDispatcher.RUN test_child.seq NO_BLOCK
R00:00:00.600 cmdSeq.CS_JOIN_WAIT
R00:00:02.000 NO_OP
```

**`test_child.seq`** (child sequence):
```
; Child sequence
R00:00:00.000 NO_OP
R00:00:01.000 NO_OP
```

**`test_error.seq`** (causes error):
```
; Sequence that will error
R00:00:00.000 INVALID_COMMAND_THAT_DOESNT_EXIST
```

---

### Test Case 1: Basic Queue Operation

**Objective:** Verify sequences queue when all sequencers busy

**Steps:**
1. Set MAX_QUEUE_DEPTH parameter: `seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 20`
2. Start first sequence: `seqDispatcher.RUN test_slow.seq BLOCK`
3. While first is running, queue second: `seqDispatcher.RUN test_quick.seq BLOCK`
4. Queue third: `seqDispatcher.RUN test_quick.seq BLOCK`

**Expected Results:**
- ✅ First RUN: Event "Dispatching sequence to sequencer 0"
- ✅ Second RUN: Event "SequenceQueued test_quick.seq (queue depth: 1)"
- ✅ Third RUN: Event "SequenceQueued test_quick.seq (queue depth: 2)"
- ✅ Telemetry: QueueDepth = 2
- ✅ After first completes: Event "StartingQueuedSequence test_quick.seq"
- ✅ Queue drains: QueueDepth goes 2 → 1 → 0
- ✅ All sequences complete successfully

---

### Test Case 2: Queue Overflow

**Objective:** Verify queue rejects sequences when full

**Steps:**
1. Set small queue: `seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 3`
2. Start slow sequence to occupy both sequencers: 
   - `seqDispatcher.RUN test_slow.seq BLOCK`
   - `seqDispatcher.RUN test_slow.seq BLOCK` (second sequencer)
3. Queue 3 more sequences:
   - `seqDispatcher.RUN test_quick.seq BLOCK` (queued 1)
   - `seqDispatcher.RUN test_quick.seq BLOCK` (queued 2)
   - `seqDispatcher.RUN test_quick.seq BLOCK` (queued 3)
4. Try to queue 4th: `seqDispatcher.RUN test_quick.seq BLOCK`

**Expected Results:**
- ✅ First 3 queued sequences: Success, queue depth reaches 3
- ✅ 4th sequence: Command response = EXECUTION_ERROR
- ✅ Event: "QueueOverflow test_quick.seq"
- ✅ Telemetry: QueueOverflows increments by 1
- ✅ Queue depth never exceeds 3

---

### Test Case 3: Queue Disabled Mode

**Objective:** Verify queue can be disabled for backward compatibility

**Steps:**
1. Disable queue: `seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 0`
2. Start sequence on sequencer 0: `seqDispatcher.RUN test_slow.seq BLOCK`
3. Start sequence on sequencer 1: `seqDispatcher.RUN test_slow.seq BLOCK`
4. Try third sequence: `seqDispatcher.RUN test_quick.seq BLOCK`

**Expected Results:**
- ✅ First two sequences: Dispatch to sequencers 0 and 1
- ✅ Third sequence: Command response = EXECUTION_ERROR (not queued)
- ✅ Event: "All sequencers busy" (no queue event)
- ✅ Telemetry: QueueDepth = 0 (never increases)

---

### Test Case 4: CLEAR_QUEUE Command

**Objective:** Verify queue can be manually cleared

**Steps:**
1. Enable queue: `seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 20`
2. Start slow sequence: `seqDispatcher.RUN test_slow.seq BLOCK`
3. Queue 5 sequences while first is running
4. Execute: `seqDispatcher.CLEAR_QUEUE`
5. Check telemetry

**Expected Results:**
- ✅ Queue depth reaches 5 before CLEAR_QUEUE
- ✅ Event: "QueueCleared (numCleared: 5)"
- ✅ Telemetry: QueueDepth = 0
- ✅ Only currently running sequence completes
- ✅ Cleared sequences never execute

---

### Test Case 5: LIST_QUEUE Command

**Objective:** Verify queue contents can be inspected

**Steps:**
1. Start slow sequence: `seqDispatcher.RUN test_slow.seq BLOCK`
2. Queue 3 different sequences:
   - `seqDispatcher.RUN test_quick.seq BLOCK`
   - `seqDispatcher.RUN test_child.seq BLOCK`
   - `seqDispatcher.RUN test_parent.seq BLOCK`
3. Execute: `seqDispatcher.LIST_QUEUE`

**Expected Results:**
- ✅ Event showing queue depth: 3
- ✅ Events listing queued sequences (or at least count + first few)
- ✅ Order matches FIFO (quick, child, parent)

---

### Test Case 6: PAUSE_QUEUE / RESUME_QUEUE

**Objective:** Verify queue can be paused and resumed

**Steps:**
1. Start slow sequence: `seqDispatcher.RUN test_slow.seq BLOCK`
2. Queue 2 sequences:
   - `seqDispatcher.RUN test_quick.seq BLOCK`
   - `seqDispatcher.RUN test_quick.seq BLOCK`
3. Execute: `seqDispatcher.PAUSE_QUEUE`
4. Wait for first sequence to complete
5. Verify queue doesn't dispatch
6. Execute: `seqDispatcher.RESUME_QUEUE`

**Expected Results:**
- ✅ Before pause: QueueDepth = 2
- ✅ Event: "QueuePaused"
- ✅ First sequence completes, but second doesn't start
- ✅ QueueDepth remains 2 (not dispatching)
- ✅ Event: "QueueResumed"
- ✅ Second sequence immediately dispatches
- ✅ Queue drains normally

---

### Test Case 7: GET_QUEUE_STATUS

**Objective:** Verify status reporting

**Steps:**
1. Queue several sequences (mixed states: running + queued)
2. Execute: `seqDispatcher.GET_QUEUE_STATUS`
3. Check GDS for telemetry and events

**Expected Results:**
- ✅ Event with current queue depth
- ✅ Telemetry shows:
  - QueueDepth (current)
  - QueuedTotal (cumulative)
  - SequencesExecutedFromQueue
  - QueueOverflows (if any occurred)

---

### Test Case 8: Multiple Sequencers Working

**Objective:** Verify queue dispatches to both sequencers

**Steps:**
1. Queue 4 quick sequences rapidly:
   - `seqDispatcher.RUN test_quick.seq BLOCK`
   - `seqDispatcher.RUN test_quick.seq BLOCK`
   - `seqDispatcher.RUN test_quick.seq BLOCK`
   - `seqDispatcher.RUN test_quick.seq BLOCK`
2. Monitor events

**Expected Results:**
- ✅ First two sequences dispatch immediately (one to each sequencer)
- ✅ Third and fourth sequences queue
- ✅ As sequencers complete, queue dispatches to whichever becomes available first
- ✅ Total execution time ~2x sequence time (not 4x, due to parallelism)

---

### Test Case 9: Nested Sequencing with CS_JOIN_WAIT

**Objective:** Verify parent-child sequence pattern works with queue

**Steps:**
1. Ensure 2 sequencers available
2. Run parent sequence: `seqDispatcher.RUN test_parent.seq BLOCK`
3. Monitor sequence execution

**Expected Results:**
- ✅ Parent starts on sequencer 0
- ✅ Parent calls child (CS_RUN test_child.seq NO_BLOCK)
- ✅ Child dispatches to sequencer 1 (or queues if busy)
- ✅ Parent blocks at CS_JOIN_WAIT
- ✅ Child executes and completes
- ✅ Parent resumes after child completion
- ✅ Both complete successfully

---

### Test Case 10: Nested Sequencing with Queue Full

**Objective:** Verify nested sequences queue correctly

**Steps:**
1. Set small queue: `seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 2`
2. Start parent sequence: `seqDispatcher.RUN test_parent.seq BLOCK`
3. While parent running, queue 2 more sequences to fill queue
4. Parent calls child sequence

**Expected Results:**
- ✅ Parent starts on sequencer 0
- ✅ Two manual sequences fill the queue
- ✅ Parent calls child (CS_RUN test_child.seq NO_BLOCK)
- ✅ Child queues (queue full, but still accepts - might need overflow handling)
- ✅ Parent waits at CS_JOIN_WAIT
- ✅ Queue drains: manual sequences execute first (FIFO)
- ✅ Child eventually executes
- ✅ Parent completes after child

---

### Test Case 11: Error Handling

**Objective:** Verify queue continues after sequence error

**Steps:**
1. Start slow sequence: `seqDispatcher.RUN test_slow.seq BLOCK`
2. Queue error sequence: `seqDispatcher.RUN test_error.seq BLOCK`
3. Queue valid sequence: `seqDispatcher.RUN test_quick.seq BLOCK`

**Expected Results:**
- ✅ First sequence completes normally
- ✅ Error sequence dispatches from queue
- ✅ Error sequence fails (invalid command)
- ✅ Telemetry: SequencesFailed increments
- ✅ Queue continues: third sequence dispatches
- ✅ Third sequence completes successfully
- ✅ Queue doesn't halt on error

---

### Test Case 12: BlockState Preservation

**Objective:** Verify queued sequences preserve BLOCK vs NO_BLOCK mode

**Steps:**
1. Start slow sequence: `seqDispatcher.RUN test_slow.seq BLOCK`
2. Queue with NO_BLOCK: `seqDispatcher.RUN test_quick.seq NO_BLOCK`
3. Check command response timing

**Expected Results:**
- ✅ First RUN (BLOCK): Command response deferred until sequence completes
- ✅ Second RUN (NO_BLOCK, queued): Command response immediate (OK)
- ✅ When queued sequence dispatches: Runs in NO_BLOCK mode
- ✅ Sequence completion behavior matches original BlockState

---

### Test Case 13: Telemetry Accuracy

**Objective:** Verify all telemetry channels update correctly

**Steps:**
1. Execute various queue operations (queue, execute, clear, overflow)
2. Monitor telemetry in GDS

**Expected Results:**
- ✅ **QueueDepth**: Updates in real-time as queue grows/shrinks
- ✅ **QueuedTotal**: Increments each time sequence is queued
- ✅ **SequencesExecutedFromQueue**: Increments when queued sequence dispatches
- ✅ **QueueOverflows**: Increments when queue rejects sequence
- ✅ Telemetry rate: Updates at rate group frequency (1 Hz)

---

### Test Case 14: Rapid Queueing Stress Test

**Objective:** Verify queue handles rapid command bursts

**Steps:**
1. Set large queue: `seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 20`
2. Start slow sequence to block sequencers
3. Send 10 queue commands rapidly (scripted or manual burst)
4. Monitor queue depth

**Expected Results:**
- ✅ All 10 sequences accepted into queue
- ✅ QueueDepth reaches 10
- ✅ No lost commands
- ✅ Sequences execute in FIFO order
- ✅ Queue drains to 0 after all complete

---

### Test Case 15: CANCEL_ALL with Queue

**Objective:** Verify cancel behavior with queued sequences

**Steps:**
1. Start slow sequence
2. Queue 3 sequences
3. Execute: `seqDispatcher.CANCEL_ALL`

**Expected Results:**
- ✅ Currently running sequences canceled
- ✅ Queued sequences: Either remain in queue OR cleared (design decision)
- ✅ System returns to idle state
- ✅ No hung state

---

### Test Case 16: Parameter Changes

**Objective:** Verify MAX_QUEUE_DEPTH parameter updates

**Steps:**
1. Set: `seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 5`
2. Queue 5 sequences
3. Set: `seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 3`
4. Try to queue more

**Expected Results:**
- ✅ Parameter change accepted
- ✅ Already-queued sequences remain (no truncation)
- ✅ New limit enforced for future queues
- ✅ Queue depth can temporarily exceed new limit (grandfathered)

---

## Automated Unit Test Requirements

In addition to manual testing, implement these unit tests:

**SeqDispatcherTester.cpp tests:**
1. `test_queueWhenBusy()` - Queue when all sequencers occupied
2. `test_queueOverflow()` - Reject when queue full
3. `test_queueDispatch()` - Dispatch from queue on sequencer available
4. `test_clearQueue()` - CLEAR_QUEUE command
5. `test_pauseResumeQueue()` - Pause/resume behavior
6. `test_queueDisabled()` - MAX_QUEUE_DEPTH = 0 mode
7. `test_queueTelemetry()` - Telemetry accuracy
8. `test_blockStatePreservation()` - BLOCK/NO_BLOCK preserved
9. `test_queueWithErrors()` - Continue after sequence error
10. `test_multipleSequencers()` - Dispatch to both sequencers

---

## Success Criteria

**All manual tests pass** + **All unit tests pass** + **No regressions in existing sequencer functionality**

After testing, you should be able to confidently say:
- ✅ Sequences queue when sequencers busy
- ✅ Queue dispatches automatically
- ✅ Queue management commands work
- ✅ Telemetry is accurate
- ✅ Multiple sequencers coordinate correctly
- ✅ Nested sequencing works with CS_JOIN_WAIT
- ✅ Errors don't halt queue
- ✅ Backward compatible (queue can be disabled)

---

## Open Questions / Future Considerations

1. **Queue Persistence:** Should queue survive component restart/reboot? (Probably not for initial release)

2. **Queue Priority:** Should certain sequences jump the queue? (FIFO for now, priority in future)

3. **Queue Inspection:** Should operators be able to remove specific queued sequences? (CLEAR_QUEUE clears all for now)

4. **Timeout in Queue:** Should queued sequences expire after timeout? (No timeout for initial release)

5. **Telemetry Rate:** How often to update queue telemetry? (On rate group tick, probably 1 Hz)

6. **CANCEL_ALL behavior:** Should CANCEL_ALL clear the queue or just cancel running sequences? (Design decision needed)
