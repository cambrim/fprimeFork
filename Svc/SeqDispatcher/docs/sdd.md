# Svc::SeqDispatcher Component

## 1 Introduction

The SeqDispatcher component dispatches command sequences to available command sequencers, allowing spacecraft operators to run multiple sequences concurrently without manually managing which CmdSequencer instances execute each sequence. The dispatcher automatically routes sequences to the first available sequencer and includes optional queueing when all sequencers are busy.

## 2 Requirements

The requirements for `Svc::SeqDispatcher` are as follows:

Requirement | Description | Verification Method
----------- | ----------- | -------------------
ISF-SEQD-001 | The `Svc::SeqDispatcher` component shall dispatch sequences to available sequencers | Unit Test
ISF-SEQD-002 | The `Svc::SeqDispatcher` component shall track the state of connected sequencers | Unit Test
ISF-SEQD-003 | The `Svc::SeqDispatcher` component shall support blocking and non-blocking sequence execution | Unit Test
ISF-SEQD-004 | The `Svc::SeqDispatcher` component shall provide commands to cancel running sequences | Unit Test
ISF-SEQD-005 | The `Svc::SeqDispatcher` component shall optionally queue sequences when all sequencers are busy | Unit Test
ISF-SEQD-006 | The `Svc::SeqDispatcher` component shall automatically dispatch queued sequences when sequencers become available | Unit Test
ISF-SEQD-007 | The `Svc::SeqDispatcher` component shall provide commands to manage the queue | Unit Test

## 3 Design

### 3.1 Topology Integration

The SeqDispatcher component coordinates multiple CmdSequencer instances. A typical topology includes:
- One SeqDispatcher instance
- Two or more CmdSequencer instances
- Connections between SeqDispatcher and all CmdSequencer instances

#### 3.1.1 Adding SeqDispatcher to a Topology

To add SeqDispatcher to your deployment:

**Step 1: Add instances to `instances.fpp`**

Add a second CmdSequencer and the SeqDispatcher instance:

```fpp
instance cmdSeq: Svc.CmdSequencer base id 0x0E00 \
  queue size Default.QUEUE_SIZE \
  stack size Default.STACK_SIZE \
  priority 100

instance cmdSeq2: Svc.CmdSequencer base id 0x0E10 \
  queue size Default.QUEUE_SIZE \
  stack size Default.STACK_SIZE \
  priority 100

instance seqDispatcher: Svc.SeqDispatcher base id 0x0E20 \
  queue size Default.QUEUE_SIZE \
  stack size Default.STACK_SIZE \
  priority 101
```

Note: Adjust base IDs to match your deployment's ID allocation scheme.

**Step 2: Wire connections in `topology.fpp`**

Replace direct cmdSeq connections with SeqDispatcher routing:

```fpp
connections Sequencing {
  # SeqDispatcher to CmdSequencers
  seqDispatcher.seqRunOut[0] -> cmdSeq.seqRunIn
  seqDispatcher.seqRunOut[1] -> cmdSeq2.seqRunIn
  cmdSeq.seqDone -> seqDispatcher.seqDoneIn[0]
  cmdSeq2.seqDone -> seqDispatcher.seqDoneIn[1]
  cmdSeq.seqStartOut -> seqDispatcher.seqStartIn[0]
  cmdSeq2.seqStartOut -> seqDispatcher.seqStartIn[1]
  seqDispatcher.seqCancelOut[0] -> cmdSeq.seqCancelIn
  seqDispatcher.seqCancelOut[1] -> cmdSeq2.seqCancelIn

  # CmdSequencers to CommandDispatcher
  cmdSeq.comCmdOut -> cmdDisp.seqCmdBuff
  cmdSeq2.comCmdOut -> cmdDisp.seqCmdBuff
  cmdDisp.seqCmdStatus -> cmdSeq.cmdResponseIn
  cmdDisp.seqCmdStatus -> cmdSeq2.cmdResponseIn
}
```

Add cmdSeq2 to a rate group for periodic scheduling:

```fpp
rateGroup2.RateGroupMemberOut[0] -> cmdSeq.schedIn
rateGroup2.RateGroupMemberOut[N] -> cmdSeq2.schedIn
```

**Step 3: Configure sequencer buffers in topology configuration**

In your deployment's topology configuration file (e.g., `YourDeploymentTopology.cpp`), allocate buffers for both sequencers:

```cpp
void configureTopology() {
    // Existing configuration...
    
    cmdSeq.allocateBuffer(0, mallocator, 5 * 1024);
    cmdSeq2.allocateBuffer(0, mallocator, 5 * 1024);
}
```

**Step 4: Update telemetry packets (if using packet definitions)**

If your deployment uses custom telemetry packet definitions, add SeqDispatcher channels to your packets file:

```fpp
packet YourPacket id N group 1 {
  seqDispatcher.dispatchedCount
  seqDispatcher.sequencersAvailable
  seqDispatcher.queueDepth
  seqDispatcher.queuedTotal
  seqDispatcher.sequencesExecutedFromQueue
  seqDispatcher.errorCount
  seqDispatcher.canceledCount
  seqDispatcher.queueOverflows
}
```

#### 3.1.2 Example Topology

See `TestDeploymentsProject/Ref/Top/` for a complete working example with:
- Two CmdSequencer instances (cmdSeq, cmdSeq2)
- SeqDispatcher with queue enabled
- Full telemetry packet integration

### 3.2 Component Diagram

![State diagram of the SeqDispatcher](seq_dispatcher_model.png "SeqDispatcher model")

### 3.3 Component Interface

#### 3.3.1 Port Description

Port Name|Type|Direction|Usage
---------|----|---------|-----
seqRunIn|Svc::CmdSeqIn|async input|Receives sequence run requests (equivalent to RUN command)
seqRunOut|Svc::CmdSeqIn|output array|Sends sequence run requests to CmdSequencers
seqCancelOut|Svc::CmdSeqCancel|output array|Sends cancel requests to CmdSequencers
seqDoneIn|Fw::CmdResponse|async input array|Receives completion status from CmdSequencers
seqStartIn|Svc::CmdSeqIn|async input array|Receives sequence start notifications from CmdSequencers
cmdRegOut|Fw::CmdReg|output|Framework command registration
cmdIn|Fw::Cmd|async input|Framework command input
cmdResponseOut|Fw::CmdResponse|output|Framework command response
logOut|Fw::Log|output|Framework event output
tlmOut|Fw::Tlm|output|Framework telemetry output
timeCaller|Fw::Time|output|Framework time port
prmGetOut|Fw::PrmGet|output|Parameter get port
prmSetOut|Fw::PrmSet|output|Parameter set port

#### 3.3.2 Parameters

Parameter | Type | Description | Default
--------- | ---- | ----------- | -------
MAX_QUEUE_DEPTH | U32 | Maximum number of sequences that can be queued when all sequencers are busy. Set to 0 to disable queueing. | 20

#### 3.3.3 Commands

##### 3.3.3.1 RUN
Dispatches a sequence to the first available sequencer. If all sequencers are busy and queueing is enabled, the sequence is added to the queue.

Arguments:
- `fileName` - Sequence file name
- `block` - BlockState (BLOCK or NO_BLOCK)

##### 3.3.3.2 RUN_ARGS
Dispatches a sequence with arguments to the first available sequencer. Behavior matches RUN command but includes sequence arguments.

Arguments:
- `fileName` - Sequence file name
- `block` - BlockState (BLOCK or NO_BLOCK)
- `buffer` - Sequence arguments

##### 3.3.3.3 LOG_STATUS
Logs the current state of each connected sequencer via events.

##### 3.3.3.4 CANCEL_NAME
Cancels any running sequence matching the given file name.

Arguments:
- `fileName` - Sequence file name to cancel

##### 3.3.3.5 CANCEL_ALL
Cancels every currently running sequence on all connected sequencers. This is a broadcast command that does not exclude the caller; a sequence issuing CANCEL_ALL will cancel itself.

##### 3.3.3.6 CLEAR_QUEUE
Clears all queued sequences. Currently running sequences are not affected.

##### 3.3.3.7 LIST_QUEUE
Lists all queued sequences via events.

##### 3.3.3.8 GET_QUEUE_STATUS
Reports current queue depth and statistics via events.

##### 3.3.3.9 PAUSE_QUEUE
Pauses queue processing. Running sequences continue, but no new sequences will be dispatched from the queue until resumed.

##### 3.3.3.10 RESUME_QUEUE
Resumes queue processing after a pause.

#### 3.3.4 Events

Event | Severity | Description
----- | -------- | -----------
InvalidSequencer | WARNING_HI | The given sequencer index is invalid
NoAvailableSequencers | WARNING_HI | No available sequencers to dispatch a sequence (emitted when queue is disabled or full)
UnknownSequenceFinished | WARNING_HI | Received seqDoneIn without corresponding seqStartIn call
UnexpectedSequenceStarted | WARNING_HI | Received seqStartIn without prior seqDoneIn call
LogSequencerStatus | ACTIVITY_HI | Shows current state and sequence filename for a sequencer (produced by LOG_STATUS)
SequenceCanceled | ACTIVITY_HI | A running sequence was canceled (by CANCEL_NAME or CANCEL_ALL)
CancelSequenceNotFound | WARNING_HI | No running sequence matched the CANCEL_NAME file name
SequenceQueued | ACTIVITY_HI | A sequence was added to the queue (reports filename and queue depth)
QueueOverflow | WARNING_HI | A sequence was rejected because the queue is full
QueueCleared | ACTIVITY_HI | The queue was manually cleared (reports number cleared)
StartingQueuedSequence | ACTIVITY_HI | A queued sequence is being dispatched
QueueEmpty | ACTIVITY_LO | The queue has been fully drained
QueuePaused | ACTIVITY_HI | Queue processing has been paused
QueueResumed | ACTIVITY_HI | Queue processing has been resumed

#### 3.3.5 Telemetry

Channel | Type | Description
------- | ---- | -----------
dispatchedCount | U32 | Number of sequences dispatched
errorCount | U32 | Number of sequences that returned an error
sequencersAvailable | U32 | Number of sequencers ready to run a sequence
canceledCount | U32 | Number of sequences canceled
queueDepth | U32 | Current number of sequences in the queue
queuedTotal | U32 | Total number of sequences queued (cumulative)
sequencesExecutedFromQueue | U32 | Number of sequences dispatched from queue
queueOverflows | U32 | Number of times a sequence was rejected due to full queue

### 3.4 Queueing Feature

#### 3.4.1 Overview

When all CmdSequencer instances are busy executing sequences, SeqDispatcher can optionally queue incoming sequence requests instead of rejecting them. This feature is controlled by the MAX_QUEUE_DEPTH parameter.

#### 3.4.2 Queue Behavior

- **Enabled by default**: MAX_QUEUE_DEPTH = 20
- **Queue order**: FIFO (First In, First Out)
- **Overflow behavior**: Sequences are rejected with EXECUTION_ERROR when queue is full
- **Automatic dispatch**: Queued sequences automatically dispatch when any sequencer completes
- **BlockState preservation**: Queued sequences maintain their BLOCK/NO_BLOCK mode
- **Disabled mode**: Set MAX_QUEUE_DEPTH = 0 for backward compatible behavior (sequences rejected when all busy)

#### 3.4.3 Queue Management

Operators can manage the queue using these commands:
- `CLEAR_QUEUE` - Remove all queued sequences
- `LIST_QUEUE` - View queued sequences
- `GET_QUEUE_STATUS` - View queue statistics
- `PAUSE_QUEUE` - Temporarily stop dispatching from queue
- `RESUME_QUEUE` - Resume queue processing

#### 3.4.4 Queue Telemetry

The following telemetry channels provide queue status:
- `queueDepth` - Current queue size
- `queuedTotal` - Cumulative count of queued sequences
- `sequencesExecutedFromQueue` - Count dispatched from queue
- `queueOverflows` - Count of rejected sequences due to full queue

## 4 Operational Usage

### 4.1 Basic Operation

To run a sequence through SeqDispatcher:

```
seqDispatcher.RUN yoursequence.seq BLOCK
```

The dispatcher will:
1. Find an available CmdSequencer
2. Route the sequence to that sequencer
3. If all sequencers are busy and queue is enabled, queue the sequence
4. Automatically dispatch queued sequences when sequencers become available

### 4.2 Multi-Sequencer Benefits

With multiple CmdSequencer instances connected to SeqDispatcher:
- Sequences can execute in parallel
- Operators do not need to track which sequencer is available
- Sequences automatically queue when all sequencers are busy
- Failed sequences do not block other sequences from executing

### 4.3 Configuring Queue Depth

To change the queue size:

```
seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 10
seqDispatcher.PRM_SAVE
```

To disable queueing entirely:

```
seqDispatcher.PRM_SET MAX_QUEUE_DEPTH 0
seqDispatcher.PRM_SAVE
```

When disabled, sequences are rejected with EXECUTION_ERROR if all sequencers are busy (original behavior).

## 5 Unit Tests

Test | Description
---- | -----------
testDispatch | Tests basic dispatch functionality
testLogStatus | Tests LOG_STATUS command
testCancelName | Tests CANCEL_NAME cancels matching sequencer and clears state
testCancelNameNotFound | Tests CANCEL_NAME with unmatched filename errors
testCancelAll | Tests CANCEL_ALL cancels every running sequencer
testCancelAllNoneRunning | Tests CANCEL_ALL with no running sequences succeeds
testRunArgsWithValidArguments | Tests RUN_ARGS with valid arguments
testRunArgsWithMaxSizedArguments | Tests RUN_ARGS with maximum-sized arguments
testRunArgsNoSequencersAvailable | Tests RUN_ARGS error handling when all busy
testRunArgsBlockingVsNonBlocking | Tests blocking and non-blocking modes
testQueueWhenBusy | Tests sequences queue when all sequencers busy
testQueueOverflow | Tests sequences rejected when queue full
testQueueDispatch | Tests queued sequences automatically dispatch
testClearQueue | Tests CLEAR_QUEUE command
testPauseResumeQueue | Tests PAUSE_QUEUE and RESUME_QUEUE commands
testQueueDisabled | Tests queueing can be disabled (MAX_QUEUE_DEPTH = 0)
testQueueTelemetry | Tests all queue telemetry channels update correctly
testBlockStatePreservation | Tests queued sequences preserve BLOCK/NO_BLOCK mode
testQueueWithErrors | Tests queue continues processing after sequence error
testMultipleSequencers | Tests queue dispatches to first available sequencer

## 6 Change Log

Date | Description
---- | -----------
2025-XX-XX | Added queue functionality with MAX_QUEUE_DEPTH parameter, queue management commands, and queue telemetry
