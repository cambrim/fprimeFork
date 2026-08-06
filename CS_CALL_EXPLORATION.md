# CS_CALL Implementation Exploration

This document tracks findings as we explore the CmdSequencer codebase for CS_CALL implementation.

## Key Files

- `Svc/CmdSequencer/CmdSequencerImpl.hpp` - Main implementation header
- `Svc/CmdSequencer/CmdSequencerImpl.cpp` - Main implementation
- `Svc/CmdSequencer/Sequence.cpp` - Sequence class implementation
- `Svc/CmdSequencer/CmdSequencerCommands.fppi` - Command definitions
- `Svc/CmdSequencer/test/ut/JoinWait/` - CS_JOIN_WAIT test cases (reference)

---

## Sequence Class Analysis

### Location
Defined inside `CmdSequencerComponentImpl` at line 74 of CmdSequencerImpl.hpp

### Structure (FINDINGS)

```cpp
class Sequence {
  public:
    class Events { ... };     // Event reporting
    class Header {            // Sequence file header
        U32 m_fileSize;
        U32 m_numRecords;
        TimeBase m_timeBase;
        FwTimeContextStoreType m_timeContext;
    };
    class Record {            // Individual command records
        Descriptor m_descriptor;  // ABSOLUTE, RELATIVE, END_OF_SEQUENCE
        Fw::Time m_timeTag;
        Fw::ComBuffer m_command;
    };
    
  protected:  // CRITICAL: These are protected members!
    CmdSequencerComponentImpl& m_component;   // Reference to parent component
    Events m_events;
    Fw::CmdStringArg m_fileName;
    Fw::LogStringArg m_logFileName;
    Fw::String m_stringFileName;
    Fw::ExternalSerializeBuffer m_buffer;     // The actual sequence data buffer!
    FwEnumStoreType m_allocatorId;
    Header m_header;
};
```

### Key Findings

**CRITICAL ISSUE:** Sequence is an **abstract base class** (has pure virtual methods):
- `virtual bool loadFile(...) = 0;`
- `virtual bool hasMoreRecords() const = 0;`
- `virtual void nextRecord(Record&) = 0;`
- `virtual void reset() = 0;`
- `virtual void clear() = 0;`

**Actual Implementation:** `FPrimeSequence` extends Sequence (line 283 of .hpp)

**Member Variable:** `m_component` is a **reference**, not a pointer
- **Implication:** Default copy constructor won't work (references can't be rebound)
- **Implication:** We need a custom copy mechanism

### Questions Answered

- [x] Does Sequence have a copy constructor? **NO - it's abstract with references**
- [x] What are the private member variables? **See list above (protected)**
- [x] Is there a buffer that holds command data? **YES - m_buffer (ExternalSerializeBuffer)**
- [x] How is sequence state tracked? **hasMoreRecords() tracks position in buffer**
- [x] Can we safely deep-copy a Sequence? **NO - abstract class, need custom approach**

### Copy Strategy

**Cannot use default copy because:**
1. Sequence is abstract (need to copy FPrimeSequence specifically)
2. `m_component` is a reference (can't rebind)
3. `m_buffer` uses external memory (need to duplicate the buffer)

**Proposed approach:**
- Save sequence filename + execution position instead of full copy
- On restore: reload sequence file from disk
- Track position via `m_executedCount` (already exists in CmdSequencer)

---

## CmdSequencer State Analysis

### State Variables (COMPLETE LIST from line 662+)

```cpp
// Sequence management
FPrimeSequence m_FPrimeSequence;          // Default sequence implementation
Sequence* m_sequence;                     // Pointer to current sequence (usually &m_FPrimeSequence)

// Execution state
RunMode m_runMode;                        // STOPPED or RUNNING
StepMode m_stepMode;                      // AUTO or MANUAL
Sequence::Record m_record;                // Current record being executed
U32 m_executedCount;                      // Commands executed in current sequence
U32 m_totalExecutedCount;                 // Total commands executed (all sequences)
U32 m_sequencesCompletedCount;           // Total sequences completed

// Timing
Timer m_cmdTimer;                         // Timer for relative-time commands
Timer m_cmdTimeoutTimer;                  // Timeout for command responses
U32 m_timeout;                            // Timeout value in seconds

// Command response tracking
Svc::BlockState::t m_blockState;          // BLOCK or NO_BLOCK
FwOpcodeType m_opCode;                    // Calling command opcode
U32 m_cmdSeq;                             // Calling command sequence number
bool m_join_waiting;                      // CS_JOIN_WAIT active flag

// Statistics
U32 m_loadCmdCount;                       // Number of sequences loaded
U32 m_cancelCmdCount;                     // Number of cancels
U32 m_errorCount;                         // Number of errors
```

### Questions Answered

- [x] Where is the current Sequence stored? **m_sequence (pointer to m_FPrimeSequence)**
- [x] How is execution position tracked? **m_executedCount + internal buffer position**
- [x] What timer/timeout state exists? **m_cmdTimer + m_cmdTimeoutTimer**
- [x] Is there command context (opCode, cmdSeq)? **YES - m_opCode, m_cmdSeq, m_blockState**
- [x] What state does CS_JOIN_WAIT save? **Just sets m_join_waiting=true and saves opCode/cmdSeq**

### Critical Finding: Timer Class

Timer is a simple SET/CLEAR state machine with an expiration time:
```cpp
class Timer {
    State m_state;              // SET or CLEAR
    Fw::Time expirationTime;    // When timer expires
};
```

**Implication:** We CAN save/restore timer state easily!

---

## CS_JOIN_WAIT Analysis

### Implementation (lines 221-240 of CmdSequencerImpl.cpp)

**How it works:**

1. **Check if sequence is running:**
   ```cpp
   if (m_runMode != RUNNING) {
       return OK;  // Nothing to wait for
   }
   ```

2. **Check for conflicts:**
   ```cpp
   if (m_blockState == BLOCK || m_join_waiting) {
       return EXECUTION_ERROR;  // Already waiting
   }
   ```

3. **Save command context and set flag:**
   ```cpp
   m_join_waiting = true;
   m_cmdSeq = cmdSeq;        // Save for later response
   m_opCode = opCode;
   ```

4. **Response sent later when sequence completes** (in performCmd_Cancel or sequenceComplete)

### Key Findings

**CS_JOIN_WAIT is very simple:**
- Just sets `m_join_waiting = true`
- Saves `m_opCode` and `m_cmdSeq` for deferred response
- Sequencer continues running (stays BUSY)
- When sequence finishes, checks `m_join_waiting` and sends response

**Limitation:**
- Parent sequencer remains in RUNNING state
- Cannot execute another sequence on same sequencer
- Requires multiple sequencers for nested execution

### Questions Answered

- [x] How does CS_JOIN_WAIT block execution? **Just a flag + deferred response**
- [x] How does it track child completion? **Doesn't - assumes current sequence finishes**
- [x] What state does it maintain? **Just m_join_waiting + m_opCode + m_cmdSeq**
- [x] Can we reuse any of its mechanisms? **YES - the opCode/cmdSeq saving pattern**

### Difference from CS_CALL

**CS_JOIN_WAIT:** Sets flag, keeps sequencer BUSY, waits for current sequence
**CS_CALL:** Should push state to stack, mark sequencer AVAILABLE, load new sequence

---

## Memory Allocation

### Sequence Buffer

From the code, sequences use a buffer allocated via `allocateBuffer()`:

```cpp
// From topology setup:
cmdSeq.allocateBuffer(0, mallocator, 5 * 1024);
```

### Questions to Answer

- [ ] How big is a typical Sequence buffer? (5KB default)
- [ ] Is the buffer dynamically allocated?
- [ ] Can we safely copy the buffer?
- [ ] What's the memory impact of a 5-level stack? (5 × 5KB = 25KB)

**Next:** Find buffer allocation logic

---

## Revised SequenceState Design

Based on exploration findings, here's what we need to save:

```cpp
struct SequenceState {
    // Sequence identification
    Fw::CmdStringArg fileName;          // Reload from disk (can't copy Sequence)
    
    // Execution position
    U32 executedCount;                  // Commands executed before pause
    
    // Command context (for response)
    FwOpcodeType opCode;                // Calling command opcode
    U32 cmdSeq;                         // Calling command sequence number
    Svc::BlockState::t blockState;      // BLOCK or NO_BLOCK
    
    // Timing state
    Timer cmdTimer;                     // Relative-time command timer
    Timer cmdTimeoutTimer;              // Command response timeout timer
    
    // Execution mode
    RunMode runMode;                    // Should always be RUNNING
    StepMode stepMode;                  // AUTO or MANUAL
    
    // Current record (may not be needed - can regenerate)
    Sequence::Record record;
};
```

**Key Decision:** Instead of copying the Sequence object:
1. Save the filename
2. On restore: reload the file with `loadFile(fileName)`
3. Fast-forward to `executedCount` by calling `nextRecord()` repeatedly
4. Resume execution

**Pros:**
- Avoids complex Sequence copying
- Works with abstract Sequence class
- Reuses existing loadFile() logic

**Cons:**
- File I/O overhead on restore (acceptable for nested sequences)
- Assumes sequence file hasn't changed (document this)

## Exploration TODOs

### Phase 1: Understand Current State
- [x] Read complete Sequence class definition
- [x] Find all CmdSequencer member variables
- [x] Map out what state exists during sequence execution
- [x] Identify what needs to be saved for pause/resume

### Phase 2: Analyze CS_JOIN_WAIT
- [ ] Read CS_JOIN_WAIT implementation
- [ ] Read CS_JOIN_WAIT unit tests
- [ ] Document how blocking works
- [ ] Identify reusable patterns

### Phase 3: Design SequenceState Struct
- [ ] List all fields needed in SequenceState
- [ ] Determine if deep copy is needed
- [ ] Plan memory management strategy

### Phase 4: Prototype
- [ ] Add SequenceState struct to CmdSequencerImpl.hpp
- [ ] Implement captureCurrentState()
- [ ] Implement restoreParentState()
- [ ] Test with simple sequence

---

## Code Reading Notes

### Finding: Sequence Constructor
```cpp
Sequence(CmdSequencerComponentImpl& component);
```

- Takes reference to component (not copyable via default copy constructor)
- Likely stores component reference internally
- **Implication:** Default copy constructor won't work, need custom implementation

### Finding: Multiple Test Namespaces
Lines 24-44 show different test scenarios:
- ImmediateBase, Immediate, ImmediateEOS
- Mixed, MixedRelativeBase
- Relative
- **JoinWait** ← This one is relevant!

**Action:** Study the JoinWait test namespace

---

## Open Questions

1. **Can we modify Sequence to be copyable?**
   - If not, what's the alternative? (serialize/deserialize?)

2. **What happens to command responses during nesting?**
   - How are they currently routed?
   - How do we maintain routing with nested sequences?

3. **Is there existing stack/context infrastructure?**
   - Does CmdSequencer already have any stack-like data structures?
   - Can we extend existing mechanisms?

4. **How does timer state work?**
   - Is there a single timer or per-command timers?
   - Can timers be paused?

---

## Next Steps

1. **Read Sequence.cpp** to understand internal structure
2. **Read more of CmdSequencerImpl.hpp** to find all state variables
3. **Read CS_JOIN_WAIT implementation** for reference
4. **Create a minimal prototype** with basic state capture

---

## Useful Commands

```bash
# Read Sequence implementation
cd ~/fprimeFork/Svc/CmdSequencer
cat Sequence.cpp | less

# Find CS_JOIN_WAIT implementation
grep -rn "CS_JOIN_WAIT" --include="*.cpp" --include="*.hpp"

# See all CmdSequencer tests
ls -la test/ut/

# Read JoinWait tests
cat test/ut/JoinWait/*.cpp | less
```

---

## Progress Tracking

- [x] Created design document
- [x] Started exploration document
- [ ] Understand Sequence class
- [ ] Understand CmdSequencer state
- [ ] Analyze CS_JOIN_WAIT
- [ ] Design SequenceState struct
- [ ] Implement prototype

**Current Focus:** Understanding the Sequence class structure
