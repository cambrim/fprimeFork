// ======================================================================
// \title  SeqDispatcher.cpp
// \author zimri.leisher
// \brief  cpp file for SeqDispatcher component implementation class
// ======================================================================

#include <Svc/SeqDispatcher/SeqDispatcher.hpp>

namespace Svc {

// ----------------------------------------------------------------------
// Construction, initialization, and destruction
// ----------------------------------------------------------------------

SeqDispatcher ::SeqDispatcher(const char* const compName) : SeqDispatcherComponentBase(compName) {}

SeqDispatcher ::~SeqDispatcher() {}

FwIndexType SeqDispatcher::getNextAvailableSequencerIdx() {
    for (FwIndexType i = 0; i < SeqDispatcherSequencerPorts; i++) {
        if (this->isConnected_seqRunOut_OutputPort(i) &&
            this->m_entryTable[i].state == SeqDispatcher_CmdSequencerState::AVAILABLE) {
            return i;
        }
    }
    return -1;
}

void SeqDispatcher::runSequence(FwIndexType sequencerIdx,
                                const Fw::ConstStringBase& fileName,
                                BlockState block,
                                const Svc::SeqArgs& args) {
    // this function is only designed for internal usage
    // we can guarantee it cannot be called with input that would fail
    FW_ASSERT(sequencerIdx >= 0 && sequencerIdx < SeqDispatcherSequencerPorts,
              static_cast<FwAssertArgType>(sequencerIdx));
    FW_ASSERT(this->isConnected_seqRunOut_OutputPort(sequencerIdx));
    FW_ASSERT(this->m_entryTable[sequencerIdx].state == SeqDispatcher_CmdSequencerState::AVAILABLE,
              static_cast<FwAssertArgType>(this->m_entryTable[sequencerIdx].state));

    if (block == BlockState::NO_BLOCK) {
        this->m_entryTable[sequencerIdx].state = SeqDispatcher_CmdSequencerState::RUNNING_SEQUENCE_NO_BLOCK;
    } else {
        this->m_entryTable[sequencerIdx].state = SeqDispatcher_CmdSequencerState::RUNNING_SEQUENCE_BLOCK;
    }

    this->m_sequencersAvailable--;
    this->tlmWrite_sequencersAvailable(this->m_sequencersAvailable);
    this->m_entryTable[sequencerIdx].sequenceRunning = fileName;

    this->m_dispatchedCount++;
    this->tlmWrite_dispatchedCount(this->m_dispatchedCount);
    this->seqRunOut_out(sequencerIdx, this->m_entryTable[sequencerIdx].sequenceRunning, args);
}

void SeqDispatcher::seqStartIn_handler(FwIndexType portNum,             //!< The port number
                                       const Fw::StringBase& fileName,  //!< The sequence file name
                                       const Svc::SeqArgs& args         //!< Sequence arguments (not currently used)
) {
    (void)args;  // Suppress unused parameter warning
    FW_ASSERT(portNum >= 0 && portNum < SeqDispatcherSequencerPorts, static_cast<FwAssertArgType>(portNum));
    if (this->m_entryTable[portNum].state == SeqDispatcher_CmdSequencerState::RUNNING_SEQUENCE_BLOCK ||
        this->m_entryTable[portNum].state == SeqDispatcher_CmdSequencerState::RUNNING_SEQUENCE_NO_BLOCK) {
        // we were aware of this sequencer running a sequence
        if (this->m_entryTable[portNum].sequenceRunning != fileName) {
            // uh oh. entry table is wrong
            // let's just update it to be correct. nothing we can do about
            // it except raise a warning and update our state
            this->log_WARNING_HI_ConflictingSequenceStarted(static_cast<U16>(portNum), fileName,
                                                            this->m_entryTable[portNum].sequenceRunning);
            this->m_entryTable[portNum].sequenceRunning = fileName;
        }
    } else {
        // we were not aware that this sequencer was running. ground must have
        // directly commanded that specific sequencer

        // warn because this may be unintentional
        this->log_WARNING_LO_UnexpectedSequenceStarted(static_cast<U16>(portNum), fileName);

        // update the state
        this->m_entryTable[portNum].state = SeqDispatcher_CmdSequencerState::RUNNING_SEQUENCE_NO_BLOCK;
        this->m_entryTable[portNum].sequenceRunning = fileName;
        this->m_sequencersAvailable--;
        this->tlmWrite_sequencersAvailable(this->m_sequencersAvailable);
    }
}

void SeqDispatcher::seqDoneIn_handler(FwIndexType portNum,             //!< The port number
                                      FwOpcodeType opCode,             //!< Command Op Code
                                      U32 cmdSeq,                      //!< Command Sequence
                                      const Fw::CmdResponse& response  //!< The command response argument
) {
    FW_ASSERT(portNum >= 0 && portNum < SeqDispatcherSequencerPorts, static_cast<FwAssertArgType>(portNum));
    if (this->m_entryTable[portNum].state != SeqDispatcher_CmdSequencerState::RUNNING_SEQUENCE_BLOCK &&
        this->m_entryTable[portNum].state != SeqDispatcher_CmdSequencerState::RUNNING_SEQUENCE_NO_BLOCK) {
        // this sequencer was not running a sequence that we were aware of.

        // we should have caught this in seqStartIn and updated the state
        // accordingly, but somehow we didn't? very sad and shouldn't happen

        // anyways, don't have to do anything cuz now that this seq we didn't know
        // about is done, the sequencer is available again (which is its current
        // state in our internal entry table already)
        this->log_WARNING_LO_UnknownSequenceFinished(static_cast<U16>(portNum));
    } else {
        // ok, a sequence has finished that we knew about
        if (this->m_entryTable[portNum].state == SeqDispatcher_CmdSequencerState::RUNNING_SEQUENCE_BLOCK) {
            // we need to give a cmd response cuz some other sequence is being blocked
            // by this
            this->cmdResponse_out(this->m_entryTable[portNum].opCode, this->m_entryTable[portNum].cmdSeq, response);

            if (response == Fw::CmdResponse::EXECUTION_ERROR) {
                // dispatched sequence errored
                this->m_errorCount++;
                this->tlmWrite_errorCount(this->m_errorCount);
            }
        }
    }

    // all command responses mean the sequence is no longer running
    // so component should be available
    this->m_entryTable[portNum].state = SeqDispatcher_CmdSequencerState::AVAILABLE;
    this->m_entryTable[portNum].sequenceRunning = "<no seq>";
    this->m_sequencersAvailable++;
    this->tlmWrite_sequencersAvailable(this->m_sequencersAvailable);

    // Try to dispatch from queue
    this->tryDispatchFromQueue();
}

//! Handler for input port seqRunIn
void SeqDispatcher::seqRunIn_handler(FwIndexType portNum, const Fw::StringBase& fileName, const Svc::SeqArgs& args) {
    FwIndexType idx = this->getNextAvailableSequencerIdx();
    // no available sequencers
    if (idx == -1) {
        this->log_WARNING_HI_NoAvailableSequencers();
        return;
    }

    this->runSequence(idx, fileName, BlockState::NO_BLOCK, args);
}
// ----------------------------------------------------------------------
// Command handler implementations
// ----------------------------------------------------------------------

// RUN command delegates to RUN_ARGS with empty arguments for backward compatibility
void SeqDispatcher ::RUN_cmdHandler(const FwOpcodeType opCode,
                                    const U32 cmdSeq,
                                    const Fw::CmdStringArg& fileName,
                                    const BlockState& block) {
    // Create empty args and delegate to RUN_ARGS handler
    Svc::SeqArgs emptyArgs{0, 0};
    this->RUN_ARGS_cmdHandler(opCode, cmdSeq, fileName, block, emptyArgs);
}

// RUN_ARGS command dispatches a sequence with optional arguments to the first available sequencer
void SeqDispatcher ::RUN_ARGS_cmdHandler(const FwOpcodeType opCode,
                                         const U32 cmdSeq,
                                         const Fw::CmdStringArg& fileName,
                                         const BlockState& block,
                                         const Svc::SeqArgs& buffer) {
    // Load max queue depth parameter
    Fw::ParamValid valid;
    U32 maxDepth = paramGet_MAX_QUEUE_DEPTH(valid);
    if (valid == Fw::ParamValid::VALID) {
        this->m_maxQueueDepth = maxDepth;
    }

    FwIndexType idx = this->getNextAvailableSequencerIdx();

    // Try direct dispatch if sequencer available
    if (idx != -1) {
        this->runSequence(idx, fileName, block, buffer);

        if (block == BlockState::NO_BLOCK) {
            // return instantly
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
        } else {
            // otherwise don't return a response yet. just save the opCode and cmdSeq
            // so we can return a response later
            this->m_entryTable[idx].opCode = opCode;
            this->m_entryTable[idx].cmdSeq = cmdSeq;
        }
        return;
    }

    // No available sequencers - try to queue if enabled
    if (this->m_maxQueueDepth > 0 && this->m_sequenceQueue.size() < this->m_maxQueueDepth) {
        // Queue the sequence
        QueueEntry entry;
        entry.fileName = fileName.toChar();
        entry.args = buffer;
        entry.blockState = block;
        entry.opCode = opCode;
        entry.cmdSeq = cmdSeq;
        entry.queueTime = this->getTime();

        this->m_sequenceQueue.push(entry);
        this->m_queuedTotal++;

        this->log_ACTIVITY_HI_SequenceQueued(Fw::LogStringArg(fileName),
                                             static_cast<U32>(this->m_sequenceQueue.size()));
        this->tlmWrite_queueDepth(static_cast<U32>(this->m_sequenceQueue.size()));
        this->tlmWrite_queuedTotal(this->m_queuedTotal);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
    } else {
        // Queue disabled or full
        if (this->m_maxQueueDepth == 0) {
            this->log_WARNING_HI_NoAvailableSequencers();
        } else {
            this->log_WARNING_HI_QueueOverflow(Fw::LogStringArg(fileName));
            this->m_queueOverflows++;
            this->tlmWrite_queueOverflows(this->m_queueOverflows);
        }
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    }
}

void SeqDispatcher::LOG_STATUS_cmdHandler(const FwOpcodeType opCode, /*!< The opcode*/
                                          const U32 cmdSeq) {        /*!< The command sequence number*/
    for (FwIndexType idx = 0; idx < SeqDispatcherSequencerPorts; idx++) {
        this->log_ACTIVITY_LO_LogSequencerStatus(static_cast<U16>(idx), this->m_entryTable[idx].state,
                                                 Fw::LogStringArg(this->m_entryTable[idx].sequenceRunning));
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SeqDispatcher::CANCEL_NAME_cmdHandler(
    const FwOpcodeType opCode, /*!< The opcode*/
    const U32 cmdSeq,          /*!< The command sequence number*/
    const Fw::CmdStringArg& fileName) /*!< The name of the sequence file to cancel*/ {
    bool canceled = false;
    for (FwIndexType idx = 0; idx < SeqDispatcherSequencerPorts; idx++) {
        // only slots actively running the named sequence are candidates
        const bool running = this->m_entryTable[idx].state != SeqDispatcher_CmdSequencerState::AVAILABLE;
        if (running && this->m_entryTable[idx].sequenceRunning == fileName) {
            if (this->isConnected_seqCancelOut_OutputPort(idx)) {
                this->seqCancelOut_out(idx);
                // Entry table is cleared via seqDoneIn_handler
                this->log_ACTIVITY_HI_SequenceCanceled(static_cast<U16>(idx),
                                                       Fw::LogStringArg(this->m_entryTable[idx].sequenceRunning));
                this->tlmWrite_canceledCount(++this->m_canceledCount);
                canceled = true;
            }
        }
    }

    if (canceled) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
    } else {
        this->log_WARNING_LO_CancelSequenceNotFound(Fw::LogStringArg(fileName));
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    }
}

//! Broadcast a cancel to every running sequencer.
//! This does not exclude the caller!
//! A sequence issuing CANCEL_ALL will cancel itself is connected to this seqDispatcher.
void SeqDispatcher::CANCEL_ALL_cmdHandler(const FwOpcodeType opCode, /*!< The opcode*/
                                          const U32 cmdSeq) {        /*!< The command sequence number*/
    for (FwIndexType idx = 0; idx < SeqDispatcherSequencerPorts; idx++) {
        const bool running = this->m_entryTable[idx].state != SeqDispatcher_CmdSequencerState::AVAILABLE;
        if (running && this->isConnected_seqCancelOut_OutputPort(idx)) {
            this->seqCancelOut_out(idx);
            // Entry table is cleared via seqDoneIn_handler
            this->log_ACTIVITY_HI_SequenceCanceled(static_cast<U16>(idx),
                                                   Fw::LogStringArg(this->m_entryTable[idx].sequenceRunning));
            this->tlmWrite_canceledCount(++this->m_canceledCount);
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SeqDispatcher::tryDispatchFromQueue() {
    // Don't dispatch if queue is paused or empty
    if (this->m_queuePaused || this->m_sequenceQueue.empty()) {
        return;
    }

    // Get next available sequencer
    FwIndexType idx = this->getNextAvailableSequencerIdx();
    if (idx == -1) {
        return;  // No sequencer available
    }

    // Get next queued sequence
    QueueEntry entry = this->m_sequenceQueue.front();
    this->m_sequenceQueue.pop();

    this->log_ACTIVITY_HI_StartingQueuedSequence(Fw::LogStringArg(entry.fileName));
    this->m_executedFromQueue++;
    this->tlmWrite_sequencesExecutedFromQueue(this->m_executedFromQueue);
    this->tlmWrite_queueDepth(static_cast<U32>(this->m_sequenceQueue.size()));

    // Check if queue is now empty
    if (this->m_sequenceQueue.empty()) {
        this->log_ACTIVITY_LO_QueueEmpty();
    }

    // Run the sequence
    this->runSequence(idx, entry.fileName, entry.blockState, entry.args);

    if (entry.blockState == BlockState::NO_BLOCK) {
        // Original command already got response when queued
        // Nothing more to do
    } else {
        // Save opCode and cmdSeq for deferred response
        this->m_entryTable[idx].opCode = entry.opCode;
        this->m_entryTable[idx].cmdSeq = entry.cmdSeq;
    }
}

void SeqDispatcher::CLEAR_QUEUE_cmdHandler(const FwOpcodeType opCode, const U32 cmdSeq) {
    U32 numCleared = static_cast<U32>(this->m_sequenceQueue.size());

    // Clear the queue
    while (!this->m_sequenceQueue.empty()) {
        this->m_sequenceQueue.pop();
    }

    this->log_ACTIVITY_HI_QueueCleared(numCleared);
    this->tlmWrite_queueDepth(0);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SeqDispatcher::LIST_QUEUE_cmdHandler(const FwOpcodeType opCode, const U32 cmdSeq) {
    U32 queueDepth = static_cast<U32>(this->m_sequenceQueue.size());

    if (queueDepth == 0) {
        this->log_ACTIVITY_LO_QueueEmpty();
    } else {
        // For now, just log the queue depth
        // Future enhancement: iterate and log each entry
        // (Need to be careful not to spam events)
        this->log_ACTIVITY_HI_SequenceQueued(Fw::LogStringArg("Queue status"), queueDepth);
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SeqDispatcher::GET_QUEUE_STATUS_cmdHandler(const FwOpcodeType opCode, const U32 cmdSeq) {
    // Report current queue status via telemetry
    this->tlmWrite_queueDepth(static_cast<U32>(this->m_sequenceQueue.size()));
    this->tlmWrite_queuedTotal(this->m_queuedTotal);
    this->tlmWrite_sequencesExecutedFromQueue(this->m_executedFromQueue);
    this->tlmWrite_queueOverflows(this->m_queueOverflows);

    if (this->m_sequenceQueue.empty()) {
        this->log_ACTIVITY_LO_QueueEmpty();
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SeqDispatcher::PAUSE_QUEUE_cmdHandler(const FwOpcodeType opCode, const U32 cmdSeq) {
    if (!this->m_queuePaused) {
        this->m_queuePaused = true;
        this->log_ACTIVITY_HI_QueuePaused();
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void SeqDispatcher::RESUME_QUEUE_cmdHandler(const FwOpcodeType opCode, const U32 cmdSeq) {
    if (this->m_queuePaused) {
        this->m_queuePaused = false;
        this->log_ACTIVITY_HI_QueueResumed();

        // Try to dispatch from queue immediately
        this->tryDispatchFromQueue();
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}
}  // namespace Svc
